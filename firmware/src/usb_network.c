/**
 * @file usb_network.c
 * @brief USB CDC-NCM ↔ lwIP Ethernet bridge for RP2350
 *
 * TinyUSB calls:
 *   tud_network_recv_cb()   – host sent us an Ethernet frame
 *   tud_network_xmit_cb()   – TinyUSB is ready to send a buffered frame
 *   tud_network_init_cb()   – NCM interface came up
 *
 * lwIP calls:
 *   ncm_netif_output()      – IP stack wants to transmit a frame
 */

#include "usb_network.h"
#include "config.h"

#include "tusb.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#include <string.h>
#include <stdbool.h>

/* ── Internal state ──────────────────────────────────────────────────────── */
static struct netif *s_netif   = NULL;
static bool          s_link_up = false;

/* Queue for a single outbound frame (pbuf reference) */
static struct pbuf *s_pending_tx = NULL;

/* MAC address – must match USB descriptor string STRID_MAC */
static const uint8_t s_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

/* ── TinyUSB NCM callbacks ────────────────────────────────────────────────── */

/**
 * Called by TinyUSB when an Ethernet frame has arrived from the USB host.
 * We copy it into a lwIP pbuf and hand it to the netif input function.
 */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (!s_netif || !s_link_up) return false;

    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (!p) return false;

    pbuf_take(p, src, size);

    if (s_netif->input(p, s_netif) != ERR_OK) {
        pbuf_free(p);
    }

    /* Allow TinyUSB to give us the next packet */
    tud_network_recv_renew();
    return true;
}

/**
 * Called by TinyUSB after tud_network_xmit() to fill its internal buffer.
 * @ref  is the pbuf we passed to tud_network_xmit().
 * @arg  is the number of bytes TinyUSB is willing to receive.
 */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *)ref;
    if (!p) return 0;
    uint16_t len = pbuf_copy_partial(p, dst, arg, 0);
    pbuf_free(p);
    s_pending_tx = NULL;
    return len;
}

/**
 * Called by TinyUSB when the NCM interface is ready (host has connected).
 */
void tud_network_init_cb(void) {
    if (s_netif) {
        netif_set_link_up(s_netif);
        s_link_up = true;
    }
}

/* ── lwIP netif glue ─────────────────────────────────────────────────────── */

/**
 * Called by lwIP when it needs to transmit an Ethernet frame.
 * We hand the pbuf to TinyUSB; TinyUSB will call tud_network_xmit_cb()
 * to actually read the bytes.
 */
static err_t ncm_netif_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    /* If TinyUSB cannot accept the frame right now, return ERR_BUF.
     * lwIP owns the pbuf and will free it; we have not held a reference. */
    if (!tud_network_can_xmit(p->tot_len)) return ERR_BUF;

    /* Ref-count the pbuf so it lives until tud_network_xmit_cb frees it */
    pbuf_ref(p);
    s_pending_tx = p;
    tud_network_xmit(p, p->tot_len);
    return ERR_OK;
}

/**
 * lwIP netif initialisation callback.
 */
static err_t ncm_netif_init_cb(struct netif *netif) {
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->output     = etharp_output;
    netif->linkoutput = ncm_netif_output;
    netif->mtu        = 1500u;
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                        NETIF_FLAG_ETHERNET  | NETIF_FLAG_IGMP;
    memcpy(netif->hwaddr, s_mac, 6u);
    netif->hwaddr_len = 6u;
    return ERR_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void usb_network_init(struct netif *netif,
                      const ip4_addr_t *ip,
                      const ip4_addr_t *mask,
                      const ip4_addr_t *gw) {
    s_netif   = netif;
    s_link_up = false;

    netif_add(netif, ip, mask, gw, NULL, ncm_netif_init_cb, ethernet_input);
    netif_set_default(netif);
    netif_set_up(netif);
}

void usb_network_task(void) {
    /* Nothing to do: TinyUSB callbacks are triggered by tud_task() */
}

bool usb_network_is_up(void) {
    return s_link_up;
}
