/**
 * @file rdm.h
 * @brief E1.20 RDM (Remote Device Management) handler
 *
 * Manages:
 *  - Sending RDM requests from the host (via ArtRDM) down the RS485 bus
 *  - Receiving and forwarding RDM responses back to the host
 *  - Background discovery (every 10 seconds)
 *  - Table of Devices (TOD) maintenance
 */

#ifndef RDM_H
#define RDM_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* ── RDM packet constants (E1.20) ──────────────────────────────────────────── */
#define RDM_SC_RDM          0xCCu   /* RDM start code */
#define RDM_SC_SUB_MESSAGE  0x01u   /* sub-start code */
#define RDM_MAX_PACKET_SIZE 257u    /* max bytes in an RDM packet */

/* Command classes */
#define RDM_CC_DISC_COMMAND          0x10u
#define RDM_CC_DISC_COMMAND_RESPONSE 0x11u
#define RDM_CC_GET_COMMAND           0x20u
#define RDM_CC_GET_COMMAND_RESPONSE  0x21u
#define RDM_CC_SET_COMMAND           0x30u
#define RDM_CC_SET_COMMAND_RESPONSE  0x31u

/* Parameter IDs used during discovery */
#define RDM_PID_DISC_UNIQUE_BRANCH  0x0001u
#define RDM_PID_DISC_MUTE           0x0002u
#define RDM_PID_DISC_UN_MUTE        0x0003u
#define RDM_PID_DEVICE_INFO         0x0060u

/* Response types */
#define RDM_RESPONSE_TYPE_ACK       0x00u
#define RDM_RESPONSE_TYPE_ACK_TIMER 0x01u
#define RDM_RESPONSE_TYPE_NACK_REASON 0x02u
#define RDM_RESPONSE_TYPE_ACK_OVERFLOW 0x03u

/* Maximum number of RDM devices tracked in TOD */
#define RDM_TOD_MAX_DEVICES         256u

/** Unique identifier (UID) of one RDM device */
typedef struct {
    uint8_t bytes[6];
} rdm_uid_t;

/** One entry in the Table of Devices */
typedef struct {
    rdm_uid_t uid;
} rdm_tod_entry_t;

/** Queued RDM request (one slot in the ring buffer) */
typedef struct {
    uint8_t  data[RDM_MAX_PACKET_SIZE];
    uint16_t length;
    uint32_t src_ip;    /* requester IP (for routing the response) */
    uint16_t src_port;  /* requester UDP port */
    bool     in_use;
} rdm_request_t;

/* ── API ──────────────────────────────────────────────────────────────────── */

/** Initialise RDM driver (UART, GPIO, timers). */
void rdm_init(void);

/** Queue an incoming ArtRDM request for transmission to the RS485 bus.
 *
 *  @return true  if the request was accepted (buffer slot available)
 *          false if the buffer is full (caller must refuse the request)
 */
bool rdm_enqueue_request(const uint8_t *data, uint16_t length,
                         uint32_t src_ip, uint16_t src_port);

/** Must be called from the main loop; drives the state machine. */
void rdm_task(void);

/** Return the current Table of Devices.
 *
 *  @param[out] out_entries  Pointer to internal TOD array (read-only)
 *  @param[out] out_count    Number of valid entries
 */
void rdm_get_tod(const rdm_tod_entry_t **out_entries, uint16_t *out_count);

/** Flush the TOD cache (forced re-discovery on next cycle). */
void rdm_flush_tod(void);

/** Return true if the TOD has changed since the last call to rdm_get_tod(). */
bool rdm_tod_changed(void);

/** Callback type invoked when an RDM response is ready to be sent back.
 *
 *  @param data     Response packet bytes
 *  @param length   Byte count
 *  @param dst_ip   Destination IP address
 *  @param dst_port Destination UDP port
 */
typedef void (*rdm_response_cb_t)(const uint8_t *data, uint16_t length,
                                  uint32_t dst_ip, uint16_t dst_port);

/** Register the callback that delivers RDM responses to the network layer. */
void rdm_set_response_callback(rdm_response_cb_t cb);

#endif /* RDM_H */
