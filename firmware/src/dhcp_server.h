/**
 * @file dhcp_server.h
 * @brief Minimal DHCP server for the USB virtual network
 *
 * Assigns a single fixed IP address (10.0.0.2) to the connected PC host.
 * The server IP is 10.0.0.1.
 */

#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include "lwip/ip_addr.h"

/** Initialise and start the DHCP server on the given network interface IP.
 *
 *  @param gw   Gateway / server IP (10.0.0.1)
 *  @param mask Subnet mask (255.255.255.0)
 */
void dhcp_server_init(ip4_addr_t *gw, ip4_addr_t *mask);

/** Deinit and free all resources held by the DHCP server. */
void dhcp_server_deinit(void);

#endif /* DHCP_SERVER_H */
