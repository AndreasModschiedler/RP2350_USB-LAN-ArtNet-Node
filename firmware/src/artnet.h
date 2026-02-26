/**
 * @file artnet.h
 * @brief ArtNet protocol handler (Art-Net 4, UDP port 6454)
 *
 * Handles the following Art-Net opcodes:
 *  - ArtPoll       → reply with ArtPollReply
 *  - ArtDMX        → update DMX frame buffer
 *  - ArtCommand    → MODE=DMX, MODE=RDM, FirmwareUpdate
 *  - ArtRDM        → queue RDM request on the RS485 bus
 *  - ArtTodRequest → return Table of Devices (with optional flush)
 *  - ArtTodData    → send TOD to controller
 *
 * All other opcodes are silently discarded and their pbufs freed.
 */

#ifndef ARTNET_H
#define ARTNET_H

#include <stdint.h>
#include <stdbool.h>
#include "lwip/ip_addr.h"

/* ── Art-Net constants ────────────────────────────────────────────────────── */
#define ARTNET_ID           "Art-Net"   /* header string (7 bytes + NUL) */
#define ARTNET_ID_LEN       8u

/* Opcodes (little-endian on the wire) */
#define OP_POLL             0x2000u
#define OP_POLL_REPLY       0x2100u
#define OP_DMX              0x5000u
#define OP_COMMAND          0x2400u
#define OP_RDM              0x8300u
#define OP_RDM_SUB          0x8400u
#define OP_TOD_REQUEST      0x8000u
#define OP_TOD_DATA         0x8100u
#define OP_TOD_CONTROL      0x8200u

/* ── Operation mode ───────────────────────────────────────────────────────── */
typedef enum {
    MODE_DMX = 0,   /**< DMX-only: handle ArtDMX + ArtCommand, 40 Hz output */
    MODE_RDM        /**< RDM mode: handle ArtRDM; DMX at minimum ESTA rate   */
} artnet_mode_t;

/* ── API ──────────────────────────────────────────────────────────────────── */

/** Initialise ArtNet: bind the UDP socket to port 6454. */
void artnet_init(void);

/** Return current operation mode. */
artnet_mode_t artnet_get_mode(void);

/** Force a mode change (also reconfigures DMX refresh rate). */
void artnet_set_mode(artnet_mode_t mode);

/** Must be called regularly from the main loop.
 *  Polls TinyUSB + lwIP and processes any pending UDP datagrams. */
void artnet_task(void);

/** Send an unsolicited ArtPollReply (e.g. after boot or mode change). */
void artnet_send_poll_reply(const ip4_addr_t *dest);

/** Deliver an RDM response to the ArtRDM requester.
 *  Called by the RDM layer when a response is ready.
 *
 *  Passing data=NULL, length=0 signals a failed request.
 */
void artnet_send_rdm_response(const uint8_t *data, uint16_t length,
                               uint32_t dst_ip, uint16_t dst_port);

#endif /* ARTNET_H */
