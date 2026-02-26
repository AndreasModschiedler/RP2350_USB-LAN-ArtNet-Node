/**
 * @file config.h
 * @brief Global configuration constants for RP2350 USB-LAN ArtNet Node
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ── Network / IP ─────────────────────────────────────────────────────────── */
/** Static IP address of this node on the USB virtual network */
#define NODE_IP_ADDR        "10.0.0.1"
#define NODE_IP_ADDR_B0     10
#define NODE_IP_ADDR_B1     0
#define NODE_IP_ADDR_B2     0
#define NODE_IP_ADDR_B3     1

/** Address assigned by DHCP to the connected PC */
#define CLIENT_IP_ADDR      "10.0.0.2"
#define CLIENT_IP_ADDR_B0   10
#define CLIENT_IP_ADDR_B1   0
#define CLIENT_IP_ADDR_B2   0
#define CLIENT_IP_ADDR_B3   2

#define SUBNET_MASK         "255.255.255.0"
#define GATEWAY_ADDR        NODE_IP_ADDR

/* ── ArtNet ───────────────────────────────────────────────────────────────── */
#define ARTNET_PORT         6454u
#define ARTNET_UNIVERSE     0u      /* default ArtNet universe */

/* Manufacturer and product strings (kept in sync with reference project) */
#define ARTNET_SHORT_NAME   "ArtNet Node"
#define ARTNET_LONG_NAME    "RP2350 USB-LAN ArtNet Node"
#define ARTNET_ESTA_MAN     0x0000u /* ESTA manufacturer code – placeholder */
#define ARTNET_OEM_HI       0x00u
#define ARTNET_OEM_LO       0x00u

/* ── DMX ──────────────────────────────────────────────────────────────────── */
#define DMX_CHANNELS        512u    /* maximum DMX channel count */
#define DMX_BAUD_RATE       250000u /* 250 kbps */
#define DMX_BREAK_US        176u    /* break duration in µs */
#define DMX_MAB_US          12u     /* mark-after-break in µs */

/** Minimum refresh rate mandated by ESTA for DMX (approx. 1 Hz in RDM mode) */
#define DMX_MIN_REFRESH_HZ  1u
/** Target refresh rate in DMX-only mode */
#define DMX_TARGET_HZ       40u

/* ── RDM ──────────────────────────────────────────────────────────────────── */
#define RDM_RESPONSE_TIMEOUT_MS     100u    /* wait up to 100 ms for response */
#define RDM_RETRY_COUNT             2u      /* retry twice on timeout */
#define RDM_DISCOVERY_INTERVAL_MS   10000u  /* background discovery every 10 s */

/* Maximum number of queued RDM requests */
#define RDM_REQUEST_BUFFER_SIZE     5u

/* ── Watchdog ─────────────────────────────────────────────────────────────── */
/** Watchdog timeout in milliseconds – must be fed within this period */
#define WATCHDOG_TIMEOUT_MS         5000u

/* ── USB ──────────────────────────────────────────────────────────────────── */
#define USB_VID                     0x2E8Au /* Raspberry Pi */
#define USB_PID                     0x000Fu /* custom product ID */
#define USB_MANUFACTURER_STR        "AndreasModschiedler"
#define USB_PRODUCT_STR             "RP2350 USB-LAN ArtNet Node"
#define USB_SERIAL_STR              "000000000001"

#endif /* CONFIG_H */
