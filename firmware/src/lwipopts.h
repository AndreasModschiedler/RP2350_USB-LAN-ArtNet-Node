/**
 * @file lwipopts.h
 * @brief lwIP configuration for RP2350 USB-LAN ArtNet Node
 *
 * Minimal lwIP configuration for a UDP-only ArtNet application.
 * TCP is not required; the stack is driven by polling.
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ── Operating system ────────────────────────────────────────────────────── */
#define NO_SYS                      1   /* bare-metal, no RTOS */
#define SYS_LIGHTWEIGHT_PROT        0

/* ── Memory ──────────────────────────────────────────────────────────────── */
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (8 * 1024)

/* ── ARP ─────────────────────────────────────────────────────────────────── */
#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              4
#define ARP_QUEUEING                0

/* ── IP ──────────────────────────────────────────────────────────────────── */
#define LWIP_IPV4                   1
#define IP_FORWARD                  0
#define IP_OPTIONS_ALLOWED          1
#define IP_REASSEMBLY               0
#define IP_FRAG                     0
#define IP_REASS_MAXAGE             0

/* ── ICMP ────────────────────────────────────────────────────────────────── */
#define LWIP_ICMP                   1

/* ── UDP ─────────────────────────────────────────────────────────────────── */
#define LWIP_UDP                    1
#define UDP_TTL                     64
#define MEMP_NUM_UDP_PCB            4

/* ── TCP – not needed ────────────────────────────────────────────────────── */
#define LWIP_TCP                    0

/* ── DHCP client – not needed (we run the server side) ───────────────────── */
#define LWIP_DHCP                   0

/* ── DNS – not needed ────────────────────────────────────────────────────── */
#define LWIP_DNS                    0

/* ── Buffers / pools ─────────────────────────────────────────────────────── */
#define PBUF_POOL_SIZE              16
#define PBUF_POOL_BUFSIZE           1536
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_NETBUF             8

/* ── Callbacks / hooks ───────────────────────────────────────────────────── */
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1

/* ── Checksum ────────────────────────────────────────────────────────────── */
#define LWIP_CHECKSUM_CTRL_PER_NETIF 0

/* ── Stats / debug ───────────────────────────────────────────────────────── */
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#endif /* LWIPOPTS_H */
