/**
 * @file rdm.c
 * @brief E1.20 RDM driver for RP2350 USB-LAN ArtNet Node
 *
 * Architecture:
 *  - UART1 is shared with DMX.  When RDM mode is active the DMX driver
 *    hands over control of the RS485 bus for individual RDM transactions.
 *  - A FIFO of RDM_REQUEST_BUFFER_SIZE slots queues incoming host requests.
 *  - A state machine in rdm_task() drains the queue one request at a time:
 *      IDLE → SENDING → WAITING_RESPONSE → (RETRY) → DONE → IDLE
 *  - Background discovery runs every RDM_DISCOVERY_INTERVAL_MS when idle.
 */

#include "rdm.h"
#include "pins.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include <string.h>
#include <stdio.h>

/* ── Shared bus-busy flag – set while an RDM transaction is in progress.
 *    Checked by dmx.c before sending a DMX frame.                       */
volatile bool g_rdm_bus_busy = false;

/* ── UART configuration (shared with dmx.c, initialised there) ───────────── */
#define RDM_UART        uart1
/* RX ring buffer size */
#define RDM_RX_BUF_SIZE 512u

/* ── Internal types ──────────────────────────────────────────────────────── */
typedef enum {
    SM_IDLE,
    SM_SENDING,
    SM_WAITING_RESPONSE,
    SM_RETRY,
    SM_DISCOVERY,
    SM_DONE
} rdm_state_t;

/* ── Module state ────────────────────────────────────────────────────────── */
static rdm_request_t    s_req_buf[RDM_REQUEST_BUFFER_SIZE];
static uint8_t          s_req_head  = 0u;  /* next slot to dequeue */
static uint8_t          s_req_tail  = 0u;  /* next free slot */
static uint8_t          s_req_count = 0u;

static rdm_state_t      s_state     = SM_IDLE;
static uint8_t          s_retry     = 0u;
static uint32_t         s_timeout_start_ms = 0u;

/* Currently active request index (while SM_SENDING/WAITING_RESPONSE) */
static uint8_t          s_active_idx = 0u;

/* Response buffer */
static uint8_t          s_resp_buf[RDM_MAX_PACKET_SIZE];
static uint16_t         s_resp_len  = 0u;

/* RX ring buffer (interrupt fills, task drains) */
static uint8_t          s_rx_buf[RDM_RX_BUF_SIZE];
static volatile uint16_t s_rx_head  = 0u;
static volatile uint16_t s_rx_tail  = 0u;

/* Table of Devices */
static rdm_tod_entry_t  s_tod[RDM_TOD_MAX_DEVICES];
static uint16_t         s_tod_count = 0u;
static bool             s_tod_changed = false;
static rdm_tod_entry_t  s_tod_prev[RDM_TOD_MAX_DEVICES];
static uint16_t         s_tod_prev_count = 0u;

/* Discovery state */
static uint32_t         s_discovery_last_ms = 0u;
static bool             s_discovery_active  = false;
static uint16_t         s_disc_tod_count    = 0u;

/* Response callback */
static rdm_response_cb_t s_resp_cb = NULL;

/* ── RDM checksum: 16-bit arithmetic sum of message bytes (E1.20 §3.12) ──── */
static uint16_t rdm_checksum(const uint8_t *data, uint16_t len) {
    uint16_t sum = 0u;
    for (uint16_t i = 0u; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/* ── RX ring buffer helpers ───────────────────────────────────────────────── */
static void rx_push(uint8_t b) {
    uint16_t next = (s_rx_tail + 1u) % RDM_RX_BUF_SIZE;
    if (next != s_rx_head) {
        s_rx_buf[s_rx_tail] = b;
        s_rx_tail = next;
    }
}

static int rx_pop(void) {
    if (s_rx_head == s_rx_tail) return -1;
    uint8_t b = s_rx_buf[s_rx_head];
    s_rx_head = (s_rx_head + 1u) % RDM_RX_BUF_SIZE;
    return (int)(uint8_t)b;
}

static uint16_t rx_available(void) {
    return (s_rx_tail - s_rx_head + RDM_RX_BUF_SIZE) % RDM_RX_BUF_SIZE;
}

static void rx_flush(void) {
    s_rx_head = s_rx_tail = 0u;
}

/* ── UART IRQ handler ────────────────────────────────────────────────────── */
static void uart1_irq_handler(void) {
    while (uart_is_readable(RDM_UART)) {
        rx_push(uart_getc(RDM_UART));
    }
}

/* ── Send raw bytes to RS485 bus ─────────────────────────────────────────── */
static void rdm_bus_send(const uint8_t *data, uint16_t len) {
    g_rdm_bus_busy = true;

    /* Drive RS485 in transmit direction, then generate BREAK */
    DMX_RS485_DIR_TX();
    uart_set_break(RDM_UART, true);
    busy_wait_us_32(DMX_BREAK_US);
    uart_set_break(RDM_UART, false);
    busy_wait_us_32(DMX_MAB_US);

    /* Transmit bytes */
    uart_write_blocking(RDM_UART, data, len);

    /* Wait for transmission to complete */
    uart_tx_wait_blocking(RDM_UART);
    DMX_RS485_DIR_RX();

    /* Flush any echo received during TX */
    busy_wait_us_32(50u);
    rx_flush();
    /* g_rdm_bus_busy cleared in rdm_task() after response received */
}

/* ── Read RDM response from bus ───────────────────────────────────────────── */
static uint16_t rdm_bus_receive(uint8_t *buf, uint16_t max_len,
                                 uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint16_t n = 0u;

    while (n < max_len) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((now - start) > timeout_ms) break;

        int b = rx_pop();
        if (b < 0) {
            tight_loop_contents();
            continue;
        }
        buf[n++] = (uint8_t)b;

        /* Minimal end-of-packet heuristic: stop code + message length known */
        if (n >= 3u && buf[0] == RDM_SC_RDM && buf[1] == RDM_SC_SUB_MESSAGE) {
            uint8_t msg_len = buf[2];
            if (n == (uint16_t)(msg_len + 2u)) break; /* +2 for checksum */
        }
    }
    return n;
}

/* ── Validate RDM response checksum ──────────────────────────────────────── */
static bool rdm_validate_response(const uint8_t *buf, uint16_t len) {
    if (len < 4u) return false;
    if (buf[0] != RDM_SC_RDM || buf[1] != RDM_SC_SUB_MESSAGE) return false;
    uint8_t msg_len = buf[2];
    if (len < (uint16_t)(msg_len + 2u)) return false;

    /* Checksum is the last 2 bytes; sum of all preceding bytes */
    uint16_t calc = rdm_checksum(buf, msg_len);
    uint16_t recv = ((uint16_t)buf[msg_len] << 8u) | buf[msg_len + 1u];
    return calc == recv;
}

/* ── Discovery: binary-tree search ───────────────────────────────────────── */

/* Broadcast UID range for DISC_UNIQUE_BRANCH */
static uint8_t s_disc_lower[6] = {0x00,0x00,0x00,0x00,0x00,0x00};
static uint8_t s_disc_upper[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/* Build a DISC_UNIQUE_BRANCH request */
static uint16_t build_disc_unique_branch(uint8_t *buf,
                                          const uint8_t lower[6],
                                          const uint8_t upper[6]) {
    memset(buf, 0, RDM_MAX_PACKET_SIZE);
    buf[0]  = RDM_SC_RDM;
    buf[1]  = RDM_SC_SUB_MESSAGE;
    /* msg_len = 24 (fixed header incl. PDL byte) + 12 (PD) = 36 */
    buf[2]  = 36u;                              /* message length */
    /* Destination UID: broadcast */
    memset(&buf[3], 0xFF, 6u);
    /* Source UID: our UID (all zeros = controller) */
    memset(&buf[9], 0x00, 6u);
    buf[15] = 0u;                               /* transaction number */
    buf[16] = 0u;                               /* port / response type */
    buf[17] = 0u;                               /* message count */
    buf[18] = 0u; buf[19] = 0u;                /* sub-device */
    buf[20] = RDM_CC_DISC_COMMAND;
    buf[21] = (uint8_t)(RDM_PID_DISC_UNIQUE_BRANCH >> 8u);
    buf[22] = (uint8_t)(RDM_PID_DISC_UNIQUE_BRANCH & 0xFFu);
    buf[23] = 12u;                              /* PDL */
    memcpy(&buf[24], lower, 6u);
    memcpy(&buf[30], upper, 6u);
    /* Checksum over all 36 message bytes (0..35) */
    uint16_t chk = rdm_checksum(buf, 36u);
    buf[36] = (uint8_t)(chk >> 8u);
    buf[37] = (uint8_t)(chk & 0xFFu);
    return 38u;
}

/* Build a DISC_MUTE request */
static uint16_t build_disc_mute(uint8_t *buf, const uint8_t uid[6]) {
    memset(buf, 0, RDM_MAX_PACKET_SIZE);
    buf[0]  = RDM_SC_RDM;
    buf[1]  = RDM_SC_SUB_MESSAGE;
    /* msg_len = 24 (header + PDL byte, no parameter data) */
    buf[2]  = 24u;
    memcpy(&buf[3], uid, 6u);
    memset(&buf[9], 0x00, 6u);
    buf[15] = 0u; buf[16] = 0u; buf[17] = 0u;
    buf[18] = 0u; buf[19] = 0u;
    buf[20] = RDM_CC_DISC_COMMAND;
    buf[21] = (uint8_t)(RDM_PID_DISC_MUTE >> 8u);
    buf[22] = (uint8_t)(RDM_PID_DISC_MUTE & 0xFFu);
    buf[23] = 0u;  /* PDL */
    /* Checksum over all 24 message bytes (0..23) */
    uint16_t chk = rdm_checksum(buf, 24u);
    buf[24] = (uint8_t)(chk >> 8u);
    buf[25] = (uint8_t)(chk & 0xFFu);
    return 26u;
}

/* Add a UID to the in-progress TOD */
static void disc_tod_add(const uint8_t uid[6]) {
    if (s_disc_tod_count >= RDM_TOD_MAX_DEVICES) return;
    memcpy(s_tod[s_disc_tod_count++].uid.bytes, uid, 6u);
}

/* Simple iterative discovery (mute-based, not full recursive binary tree) */
static void run_discovery_cycle(void) {
    uint8_t pkt[RDM_MAX_PACKET_SIZE];
    uint8_t resp[RDM_MAX_PACKET_SIZE];

    s_disc_tod_count = 0u;
    g_rdm_bus_busy   = true;

    for (uint8_t attempt = 0u; attempt < 64u; attempt++) {
        uint16_t pkt_len = build_disc_unique_branch(pkt,
                                                     s_disc_lower,
                                                     s_disc_upper);
        rdm_bus_send(pkt, pkt_len);
        uint16_t resp_len = rdm_bus_receive(resp, sizeof(resp), 30u);

        if (resp_len < 7u) break; /* no (more) devices */

        /* Decode encoded UID from DISC_UNIQUE_BRANCH response (E1.20 §8.8).
         * The response preamble is 7 bytes; each of the 6 UID bytes follows
         * as two nibbles: byte_hi (bits 7-4) and byte_lo (bits 3-0),
         * so uid[i] = (preamble[1+i*2] & 0x0F) | ((preamble[2+i*2] & 0x0F) << 4).
         * Minimum valid response length: 7 preamble + 4 ECC = 17 bytes.    */
#define DISC_RESP_MIN_LEN   17u
#define DISC_RESP_PREAMBLE  1u   /* preamble separator is at index 0 */
        uint8_t uid[6];
        for (uint8_t i = 0u; i < 6u; i++) {
            if (resp_len >= DISC_RESP_MIN_LEN) {
                uid[i] = (resp[DISC_RESP_PREAMBLE + i * 2u] & 0x0Fu) |
                         ((resp[DISC_RESP_PREAMBLE + 1u + i * 2u] & 0x0Fu) << 4u);
            } else {
                memset(uid, 0, 6u);
                break;
            }
        }
#undef DISC_RESP_MIN_LEN
#undef DISC_RESP_PREAMBLE

        /* Mute this device */
        uint16_t mute_len = build_disc_mute(pkt, uid);
        rdm_bus_send(pkt, mute_len);
        rdm_bus_receive(resp, sizeof(resp), 30u); /* consume mute response */

        disc_tod_add(uid);
    }
}

/* ── Initialisation ──────────────────────────────────────────────────────── */

void rdm_init(void) {
    /* UART1 is already initialised by dmx_init(); just enable the RX interrupt */
    irq_set_exclusive_handler(UART1_IRQ, uart1_irq_handler);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(RDM_UART, true, false);

    rx_flush();
    memset(s_req_buf, 0, sizeof(s_req_buf));
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool rdm_enqueue_request(const uint8_t *data, uint16_t length,
                          uint32_t src_ip, uint16_t src_port) {
    if (s_req_count >= RDM_REQUEST_BUFFER_SIZE) return false;
    if (length > RDM_MAX_PACKET_SIZE) return false;

    rdm_request_t *slot = &s_req_buf[s_req_tail];
    memcpy(slot->data, data, length);
    slot->length   = length;
    slot->src_ip   = src_ip;
    slot->src_port = src_port;
    slot->in_use   = true;

    s_req_tail = (s_req_tail + 1u) % RDM_REQUEST_BUFFER_SIZE;
    s_req_count++;
    return true;
}

void rdm_set_response_callback(rdm_response_cb_t cb) {
    s_resp_cb = cb;
}

void rdm_get_tod(const rdm_tod_entry_t **out_entries, uint16_t *out_count) {
    *out_entries = s_tod;
    *out_count   = s_tod_count;
    s_tod_changed = false;
}

void rdm_flush_tod(void) {
    s_tod_count = 0u;
    s_tod_changed = true;
    s_discovery_last_ms = 0u; /* trigger immediate re-discovery */
}

bool rdm_tod_changed(void) {
    return s_tod_changed;
}

/* ── Main-loop task ──────────────────────────────────────────────────────── */

void rdm_task(void) {
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    /* ── Process queued host requests ────────────────────────────────── */
    if (s_state == SM_IDLE && s_req_count > 0u) {
        s_active_idx = s_req_head;
        s_retry      = 0u;
        s_state      = SM_SENDING;
    }

    if (s_state == SM_SENDING) {
        rdm_request_t *req = &s_req_buf[s_active_idx];
        rx_flush();
        rdm_bus_send(req->data, req->length);
        s_timeout_start_ms = to_ms_since_boot(get_absolute_time());
        s_resp_len = 0u;
        s_state = SM_WAITING_RESPONSE;
        return;
    }

    if (s_state == SM_WAITING_RESPONSE) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - s_timeout_start_ms;

        /* Collect bytes */
        while (rx_available() > 0u && s_resp_len < RDM_MAX_PACKET_SIZE) {
            int b = rx_pop();
            if (b >= 0) s_resp_buf[s_resp_len++] = (uint8_t)b;
        }

        bool valid = rdm_validate_response(s_resp_buf, s_resp_len);

        if (valid || elapsed >= RDM_RESPONSE_TIMEOUT_MS) {
            if (!valid && s_retry < RDM_RETRY_COUNT) {
                s_retry++;
                s_state = SM_SENDING; /* retry */
                return;
            }

            /* Forward response (or failure indication) to requester */
            rdm_request_t *req = &s_req_buf[s_active_idx];
            if (s_resp_cb) {
                if (valid) {
                    s_resp_cb(s_resp_buf, s_resp_len,
                              req->src_ip, req->src_port);
                } else {
                    /* Send an empty response to signal failure */
                    s_resp_cb(NULL, 0u, req->src_ip, req->src_port);
                }
            }

            /* Free the buffer slot */
            req->in_use = false;
            s_req_head = (s_req_head + 1u) % RDM_REQUEST_BUFFER_SIZE;
            s_req_count--;
            s_state = SM_IDLE;
            g_rdm_bus_busy = false;
        }
        return;
    }

    /* ── Background discovery ─────────────────────────────────────────── */
    if (s_state == SM_IDLE && !s_discovery_active) {
        if ((now_ms - s_discovery_last_ms) >= RDM_DISCOVERY_INTERVAL_MS) {
            s_discovery_active = true;
        }
    }

    if (s_state == SM_IDLE && s_discovery_active) {
        /* Save previous TOD for change detection */
        memcpy(s_tod_prev, s_tod, sizeof(rdm_tod_entry_t) * s_tod_count);
        s_tod_prev_count = s_tod_count;

        run_discovery_cycle();

        s_tod_count = s_disc_tod_count;

        /* Check if TOD changed */
        if (s_tod_count != s_tod_prev_count ||
            memcmp(s_tod, s_tod_prev,
                   sizeof(rdm_tod_entry_t) * s_tod_count) != 0) {
            s_tod_changed = true;
        }

        s_discovery_last_ms = to_ms_since_boot(get_absolute_time());
        s_discovery_active  = false;
        g_rdm_bus_busy      = false;
    }
}
