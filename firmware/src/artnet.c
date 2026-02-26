/**
 * @file artnet.c
 * @brief Art-Net 4 protocol handler for RP2350 USB-LAN ArtNet Node
 *
 * Receives Art-Net datagrams on UDP port 6454 via lwIP and dispatches them
 * to the appropriate sub-systems (DMX driver, RDM driver).
 * All pbufs are freed after processing; no packet is held across calls.
 */

#include "artnet.h"
#include "dmx.h"
#include "rdm.h"
#include "config.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"   /* reset_usb_boot() for firmware update */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── Art-Net packet header ────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  id[ARTNET_ID_LEN];  /* "Art-Net\0" */
    uint16_t opcode;              /* little-endian */
    uint16_t proto_ver;           /* big-endian, must be >= 14 */
} artnet_hdr_t;

/* ── ArtPollReply (partial – fields needed for basic node announcement) ─── */
typedef struct __attribute__((packed)) {
    uint8_t  id[ARTNET_ID_LEN];
    uint16_t opcode;
    uint8_t  ip[4];
    uint16_t port;
    uint16_t vers_info;
    uint8_t  net_switch;
    uint8_t  sub_switch;
    uint16_t oem;
    uint8_t  ubea_version;
    uint8_t  status1;
    uint16_t esta_man;
    char     short_name[18];
    char     long_name[64];
    char     node_report[64];
    uint16_t num_ports;
    uint8_t  port_types[4];
    uint8_t  good_input[4];
    uint8_t  good_output[4];
    uint8_t  sw_in[4];
    uint8_t  sw_out[4];
    uint8_t  sw_video;
    uint8_t  sw_macro;
    uint8_t  sw_remote;
    uint8_t  spare[3];
    uint8_t  style;
    uint8_t  mac[6];
    uint8_t  bind_ip[4];
    uint8_t  bind_index;
    uint8_t  status2;
    uint8_t  filler[26];
} artnet_poll_reply_t;

/* ── ArtTodData header ────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  id[ARTNET_ID_LEN];
    uint16_t opcode;
    uint8_t  proto_ver_hi;
    uint8_t  proto_ver_lo;
    uint8_t  rd_count;
    uint8_t  spare[7];
    uint8_t  net;
    uint8_t  command_response;
    uint8_t  address;
    uint16_t uid_total_be;
    uint8_t  block_count;
    uint8_t  uid_count;
    /* Followed by uid_count × 6-byte UIDs */
} artnet_tod_data_t;

/* ── Module state ────────────────────────────────────────────────────────── */
static struct udp_pcb *s_pcb    = NULL;
static artnet_mode_t   s_mode   = MODE_DMX;

/* MAC address (must match usb_descriptors.c) */
static const uint8_t s_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static bool artnet_id_ok(const uint8_t *id) {
    return memcmp(id, ARTNET_ID, ARTNET_ID_LEN) == 0;
}

/* ── ArtPollReply ─────────────────────────────────────────────────────────── */
static void send_poll_reply_to(const ip4_addr_t *dest) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(artnet_poll_reply_t), PBUF_RAM);
    if (!p) return;

    artnet_poll_reply_t *rep = (artnet_poll_reply_t *)p->payload;
    memset(rep, 0, sizeof(*rep));

    memcpy(rep->id, ARTNET_ID, ARTNET_ID_LEN);
    /* Opcode is little-endian on the wire; RP2350 is LE so assign directly */
    rep->opcode        = OP_POLL_REPLY;

    /* Our IP */
    rep->ip[0] = NODE_IP_ADDR_B0;
    rep->ip[1] = NODE_IP_ADDR_B1;
    rep->ip[2] = NODE_IP_ADDR_B2;
    rep->ip[3] = NODE_IP_ADDR_B3;

    rep->port          = lwip_htons(ARTNET_PORT);
    rep->vers_info     = lwip_htons(0x0001u);
    rep->net_switch    = 0u;
    rep->sub_switch    = 0u;
    rep->oem           = lwip_htons((uint16_t)((ARTNET_OEM_HI << 8u) | ARTNET_OEM_LO));
    rep->esta_man      = lwip_htons(ARTNET_ESTA_MAN);

    strncpy(rep->short_name, ARTNET_SHORT_NAME, sizeof(rep->short_name) - 1u);
    strncpy(rep->long_name,  ARTNET_LONG_NAME,  sizeof(rep->long_name)  - 1u);

    snprintf(rep->node_report, sizeof(rep->node_report),
             "#0001 [%s] OK", s_mode == MODE_RDM ? "RDM" : "DMX");

    rep->num_ports     = lwip_htons(1u);
    rep->port_types[0] = 0x80u; /* output DMX */
    rep->good_output[0]= 0x80u; /* data being transmitted */
    rep->sw_out[0]     = (uint8_t)(ARTNET_UNIVERSE & 0x0Fu);

    memcpy(rep->mac, s_mac, 6u);
    rep->bind_ip[0] = NODE_IP_ADDR_B0;
    rep->bind_ip[1] = NODE_IP_ADDR_B1;
    rep->bind_ip[2] = NODE_IP_ADDR_B2;
    rep->bind_ip[3] = NODE_IP_ADDR_B3;
    rep->bind_index  = 1u;
    rep->status2     = 0x08u; /* DHCP capable */

    udp_sendto(s_pcb, p, dest, ARTNET_PORT);
    pbuf_free(p);
}

/* ── ArtTodData ──────────────────────────────────────────────────────────── */
static void send_tod_data(const ip4_addr_t *dest) {
    const rdm_tod_entry_t *entries;
    uint16_t count;
    rdm_get_tod(&entries, &count);

    uint16_t pkt_size = sizeof(artnet_tod_data_t) + (uint16_t)(count * 6u);
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, pkt_size, PBUF_RAM);
    if (!p) return;

    artnet_tod_data_t *tod = (artnet_tod_data_t *)p->payload;
    memset(tod, 0, sizeof(*tod));

    memcpy(tod->id, ARTNET_ID, ARTNET_ID_LEN);
    /* Opcode is little-endian on the wire */
    tod->opcode         = OP_TOD_DATA;
    tod->proto_ver_hi   = 0u;
    tod->proto_ver_lo   = 14u;
    tod->net            = 0u;
    tod->command_response = 0u; /* TOD full */
    tod->address        = (uint8_t)(ARTNET_UNIVERSE & 0x0Fu);
    tod->uid_total_be   = lwip_htons(count);
    tod->block_count    = 0u;
    tod->uid_count      = (uint8_t)(count > 255u ? 255u : count);

    uint8_t *uid_ptr = (uint8_t *)p->payload + sizeof(artnet_tod_data_t);
    for (uint16_t i = 0u; i < tod->uid_count; i++) {
        memcpy(uid_ptr + i * 6u, entries[i].uid.bytes, 6u);
    }

    udp_sendto(s_pcb, p, dest, ARTNET_PORT);
    pbuf_free(p);
}

/* ── ArtRDM response wrapper ────────────────────────────────────────────── */
void artnet_send_rdm_response(const uint8_t *data, uint16_t length,
                               uint32_t dst_ip_u32, uint16_t dst_port) {
    if (!s_pcb) return;

    /* Build an ArtRDM reply packet */
    uint16_t pkt_size = 12u + length; /* ArtRDM header = 12 bytes */
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, pkt_size, PBUF_RAM);
    if (!p) return;

    uint8_t *buf = (uint8_t *)p->payload;
    memset(buf, 0, pkt_size);

    /* Header */
    memcpy(buf, ARTNET_ID, ARTNET_ID_LEN);
    /* Opcode is little-endian on the wire */
    buf[8]  = (uint8_t)(OP_RDM & 0xFFu);
    buf[9]  = (uint8_t)(OP_RDM >> 8u);
    buf[10] = 0u;   /* proto ver hi */
    buf[11] = 14u;  /* proto ver lo */

    if (data && length > 0u) {
        memcpy(buf + 12u, data, length);
    }

    ip4_addr_t dst;
    dst.addr = dst_ip_u32;
    udp_sendto(s_pcb, p, &dst, dst_port);
    pbuf_free(p);
}

/* ── RDM response callback (called by rdm.c) ─────────────────────────────── */
static void rdm_resp_callback(const uint8_t *data, uint16_t length,
                               uint32_t dst_ip, uint16_t dst_port) {
    artnet_send_rdm_response(data, length, dst_ip, dst_port);
}

/* ── UDP receive callback ─────────────────────────────────────────────────── */
static void artnet_recv(void *arg, struct udp_pcb *pcb,
                        struct pbuf *p, const ip_addr_t *src, u16_t src_port) {
    (void)arg; (void)pcb;

    if (!p || p->tot_len < sizeof(artnet_hdr_t)) {
        if (p) pbuf_free(p);
        return;
    }

    /* Copy header */
    artnet_hdr_t hdr;
    pbuf_copy_partial(p, &hdr, sizeof(hdr), 0);

    /* Validate Art-Net ID */
    if (!artnet_id_ok(hdr.id)) {
        pbuf_free(p);
        return;
    }

    uint16_t opcode = (uint16_t)(hdr.opcode); /* LE on wire */

    ip4_addr_t src4;
    src4.addr = ip4_addr_get_u32(ip_2_ip4(src));

    switch (opcode) {

    /* ── ArtPoll ──────────────────────────────────────────────────────── */
    case OP_POLL:
        send_poll_reply_to(&src4);
        break;

    /* ── ArtDMX ───────────────────────────────────────────────────────── */
    case OP_DMX: {
        if (p->tot_len < 18u) break;  /* minimum ArtDMX header */
        uint8_t  uni_hi, uni_lo, len_hi, len_lo;
        pbuf_copy_partial(p, &uni_hi, 1, 14u);
        pbuf_copy_partial(p, &uni_lo, 1, 15u);
        pbuf_copy_partial(p, &len_hi, 1, 16u);
        pbuf_copy_partial(p, &len_lo, 1, 17u);

        uint16_t universe = (uint16_t)((uni_hi << 8u) | uni_lo);
        uint16_t data_len = (uint16_t)((len_hi << 8u) | len_lo);

        if (universe != ARTNET_UNIVERSE) break;
        if (data_len > DMX_CHANNELS) data_len = DMX_CHANNELS;
        if (p->tot_len < (uint16_t)(18u + data_len)) break;

        uint8_t dmx_data[DMX_CHANNELS];
        pbuf_copy_partial(p, dmx_data, data_len, 18u);
        dmx_update(dmx_data, data_len);
        break;
    }

    /* ── ArtCommand ───────────────────────────────────────────────────── */
    case OP_COMMAND: {
        if (p->tot_len < 14u) break;
        uint16_t data_len;
        pbuf_copy_partial(p, &data_len, 2, 12u);
        data_len = lwip_htons(data_len);
        if (data_len > 512u) data_len = 512u;

        char cmd[513];
        pbuf_copy_partial(p, cmd, data_len, 14u);
        cmd[data_len] = '\0';

        if (strcmp(cmd, "MODE=DMX") == 0) {
            artnet_set_mode(MODE_DMX);
        } else if (strcmp(cmd, "MODE=RDM") == 0) {
            artnet_set_mode(MODE_RDM);
        } else if (strcmp(cmd, "FirmwareUpdate") == 0) {
            /* Reboot into USB mass-storage / BOOTSEL mode */
            reset_usb_boot(0, 0);
        }
        break;
    }

    /* ── ArtRDM ───────────────────────────────────────────────────────── */
    case OP_RDM: {
        if (s_mode != MODE_RDM) break;  /* ignore RDM in DMX mode */
        if (p->tot_len < 12u) break;

        uint16_t payload_len = p->tot_len > 12u ? (uint16_t)(p->tot_len - 12u) : 0u;
        if (payload_len == 0u) break;

        uint8_t rdm_pkt[257u];
        if (payload_len > sizeof(rdm_pkt)) payload_len = sizeof(rdm_pkt);
        pbuf_copy_partial(p, rdm_pkt, payload_len, 12u);

        if (!rdm_enqueue_request(rdm_pkt, payload_len,
                                  src4.addr, src_port)) {
            /* Buffer full – refuse by ignoring (no response) */
        }
        break;
    }

    /* ── ArtTodRequest ────────────────────────────────────────────────── */
    case OP_TOD_REQUEST:
        send_tod_data(&src4);
        break;

    /* ── ArtTodControl ────────────────────────────────────────────────── */
    case OP_TOD_CONTROL: {
        if (p->tot_len < 14u) break;
        uint8_t command;
        pbuf_copy_partial(p, &command, 1u, 13u);
        if (command == 0x01u) { /* AtcFlush */
            rdm_flush_tod();
        }
        send_tod_data(&src4);
        break;
    }

    default:
        /* Unknown or unsupported opcode – discard */
        break;
    }

    pbuf_free(p);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void artnet_init(void) {
    s_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!s_pcb) return;

    udp_bind(s_pcb, IP4_ADDR_ANY, ARTNET_PORT);
    udp_recv(s_pcb, artnet_recv, NULL);

    /* Register RDM response callback */
    rdm_set_response_callback(rdm_resp_callback);
}

artnet_mode_t artnet_get_mode(void) {
    return s_mode;
}

void artnet_set_mode(artnet_mode_t mode) {
    s_mode = mode;
    if (mode == MODE_DMX) {
        dmx_set_rate(DMX_TARGET_HZ);
    } else {
        dmx_set_rate(DMX_MIN_REFRESH_HZ);
    }
}

void artnet_send_poll_reply(const ip4_addr_t *dest) {
    send_poll_reply_to(dest);
}

void artnet_task(void) {
    /* lwIP polling is driven from main(); nothing extra needed here.
     * TOD updates are sent on request only. */
}
