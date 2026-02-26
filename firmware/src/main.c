/**
 * @file main.c
 * @brief RP2350 USB-LAN ArtNet Node – main entry point
 *
 * Startup sequence:
 *  1. Initialise hardware (GPIO, watchdog)
 *  2. Initialise TinyUSB
 *  3. Wait for USB enumeration
 *  4. Bring up lwIP with a static IP (10.0.0.1) on the NCM netif
 *  5. Start DHCP server (assigns 10.0.0.2 to connected PC)
 *  6. Initialise DMX output and RDM driver
 *  7. Bind ArtNet UDP socket
 *  8. Send initial ArtPollReply (broadcast)
 *  9. Enter the main polling loop
 *
 * Main loop:
 *  - Feed the watchdog
 *  - Poll TinyUSB (tud_task)
 *  - Poll lwIP (sys_check_timeouts + netif input)
 *  - Run DMX frame scheduler (dmx_task)
 *  - Run RDM state machine (rdm_task)
 *  - Run ArtNet tasks (artnet_task)
 */

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"

#include "tusb.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "lwip/init.h"

#include "usb_network.h"
#include "dhcp_server.h"
#include "artnet.h"
#include "dmx.h"
#include "rdm.h"
#include "pins.h"
#include "config.h"

#include <string.h>
#include <stdio.h>

/* ── Global netif ────────────────────────────────────────────────────────── */
static struct netif s_netif;

/* ── LED blink helper ────────────────────────────────────────────────────── */
static void led_init(void) {
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);
}

static void led_set(bool on) {
    gpio_put(PIN_LED, on ? 1 : 0);
}

/* ── Network interface link callback ─────────────────────────────────────── */
static void netif_link_callback(struct netif *netif) {
    led_set(netif_is_link_up(netif));
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    /* Basic hardware init */
    stdio_init_all();
    led_init();

    /* Enable hardware watchdog – reset device if it hangs for more than
     * WATCHDOG_TIMEOUT_MS without calling watchdog_update().             */
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    /* Initialise TinyUSB */
    tusb_init();

    /* Wait for USB device to be mounted by the host */
    while (!tud_mounted()) {
        tud_task();
        watchdog_update();
    }

    /* ── Network stack ───────────────────────────────────────────────── */
    lwip_init();

    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   NODE_IP_ADDR_B0, NODE_IP_ADDR_B1,
                    NODE_IP_ADDR_B2, NODE_IP_ADDR_B3);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw,   NODE_IP_ADDR_B0, NODE_IP_ADDR_B1,
                    NODE_IP_ADDR_B2, NODE_IP_ADDR_B3);

    usb_network_init(&s_netif, &ip, &mask, &gw);
    netif_set_link_callback(&s_netif, netif_link_callback);

    /* DHCP server – hands out 10.0.0.2 to the PC */
    dhcp_server_init(&gw, &mask);

    /* ── DMX / RDM ───────────────────────────────────────────────────── */
    dmx_init();
    dmx_set_rate(DMX_TARGET_HZ);
    dmx_start();

    rdm_init();

    /* ── ArtNet ──────────────────────────────────────────────────────── */
    artnet_init();
    artnet_set_mode(MODE_DMX);

    /* Announce ourselves on the network */
    ip4_addr_t bcast;
    IP4_ADDR(&bcast, 255, 255, 255, 255);
    artnet_send_poll_reply(&bcast);

    /* ── Main polling loop ───────────────────────────────────────────── */
    while (true) {
        /* Feed the watchdog – must happen within WATCHDOG_TIMEOUT_MS */
        watchdog_update();

        /* TinyUSB polling – drives USB events and triggers NCM callbacks */
        tud_task();

        /* USB network task */
        usb_network_task();

        /* lwIP timers and deferred processing */
        sys_check_timeouts();

        /* DMX frame output */
        dmx_task();

        /* RDM state machine */
        rdm_task();

        /* ArtNet housekeeping */
        artnet_task();
    }

    /* Never reached */
    return 0;
}
