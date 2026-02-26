/**
 * @file usb_network.h
 * @brief USB CDC-NCM to lwIP bridge
 *
 * Implements the TinyUSB NCM callbacks and provides a lwIP netif
 * that forwards Ethernet frames between the USB host and the lwIP stack.
 */

#ifndef USB_NETWORK_H
#define USB_NETWORK_H

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

/** Initialise and register the USB NCM lwIP netif.
 *
 *  @param netif  Pre-allocated netif structure
 *  @param ip     Static IP address for this node
 *  @param mask   Subnet mask
 *  @param gw     Default gateway (same as ip for a /24 point-to-point link)
 */
void usb_network_init(struct netif *netif,
                      const ip4_addr_t *ip,
                      const ip4_addr_t *mask,
                      const ip4_addr_t *gw);

/** Must be called from the main loop – processes any queued USB network events. */
void usb_network_task(void);

/** Return true once the host has brought the NCM interface up. */
bool usb_network_is_up(void);

#endif /* USB_NETWORK_H */
