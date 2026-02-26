/**
 * @file tusb_config.h
 * @brief TinyUSB configuration for RP2350 USB-LAN ArtNet Node
 *
 * Configures TinyUSB to expose a CDC-NCM (Network Control Model) device
 * so that the host PC obtains a virtual Ethernet interface.
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* ── Controller ─────────────────────────────────────────────────────────── */
#define CFG_TUSB_MCU                OPT_MCU_RP2040  /* also covers RP2350 */
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE
#define CFG_TUSB_OS                 OPT_OS_NONE     /* bare-metal polling */

/* ── Debug ───────────────────────────────────────────────────────────────── */
#define CFG_TUSB_DEBUG              0

/* ── Memory ──────────────────────────────────────────────────────────────── */
#define CFG_TUSB_MEM_SECTION        /* default */
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))

/* ── Device class ────────────────────────────────────────────────────────── */
/* Enable CDC-NCM network class */
#define CFG_TUD_NCM                 1
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* ── NCM specifics ───────────────────────────────────────────────────────── */
/** Number of NCM instances */
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE  (2 * 1024)
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE (2 * 1024)
/** Number of datagrams that can be batched in one NTB */
#define CFG_TUD_NCM_IN_NTB_N         8
#define CFG_TUD_NCM_OUT_NTB_N        8

#endif /* TUSB_CONFIG_H */
