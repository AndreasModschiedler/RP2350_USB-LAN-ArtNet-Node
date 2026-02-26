/**
 * @file dhcp_server.c
 * @brief Minimal DHCP server – assigns 10.0.0.2 to the connected PC.
 *
 * Only a single lease is managed.  The implementation handles:
 *  - DHCPDISCOVER → DHCPOFFER
 *  - DHCPREQUEST  → DHCPACK
 *  - DHCPRELEASE  → lease cleared
 *
 * All other DHCP message types are silently ignored.
 */

#include "dhcp_server.h"
#include "config.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include <string.h>
#include <stdint.h>

/* ── DHCP constants ──────────────────────────────────────────────────────── */
#define DHCP_SERVER_PORT    67u
#define DHCP_CLIENT_PORT    68u
#define DHCP_MAGIC_COOKIE   0x63825363ul

/* DHCP message types */
#define DHCP_DISCOVER   1u
#define DHCP_OFFER      2u
#define DHCP_REQUEST    3u
#define DHCP_DECLINE    4u
#define DHCP_ACK        5u
#define DHCP_NAK        6u
#define DHCP_RELEASE    7u
#define DHCP_INFORM     8u

/* DHCP options */
#define DHCP_OPT_SUBNET_MASK    1u
#define DHCP_OPT_ROUTER         3u
#define DHCP_OPT_DNS_SERVER     6u
#define DHCP_OPT_HOST_NAME      12u
#define DHCP_OPT_REQUESTED_IP   50u
#define DHCP_OPT_LEASE_TIME     51u
#define DHCP_OPT_MSG_TYPE       53u
#define DHCP_OPT_SERVER_ID      54u
#define DHCP_OPT_PARAM_LIST     55u
#define DHCP_OPT_MAX_MSG_SIZE   57u
#define DHCP_OPT_RENEWAL_TIME   58u
#define DHCP_OPT_REBIND_TIME    59u
#define DHCP_OPT_CLIENT_ID      61u
#define DHCP_OPT_END            255u

#define DHCP_LEASE_TIME_S       86400ul /* 24 hours */

/* ── DHCP packet layout ───────────────────────────────────────────────────── */
#define DHCP_CHADDR_LEN 16u
#define DHCP_SNAME_LEN  64u
#define DHCP_FILE_LEN   128u
#define DHCP_OPTIONS_LEN 312u

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[DHCP_CHADDR_LEN];
    uint8_t  sname[DHCP_SNAME_LEN];
    uint8_t  file[DHCP_FILE_LEN];
    uint32_t magic;
    uint8_t  options[DHCP_OPTIONS_LEN];
} dhcp_msg_t;

/* ── Internal state ──────────────────────────────────────────────────────── */
typedef struct {
    struct udp_pcb *pcb;
    ip4_addr_t      gw;          /* server / gateway IP */
    ip4_addr_t      mask;
    ip4_addr_t      client_ip;   /* the one address we hand out */
    uint8_t         client_mac[6];
    bool            lease_active;
} dhcp_server_t;

static dhcp_server_t s_dhcp;

/* ── Helper: write DHCP option ───────────────────────────────────────────── */
static uint8_t *write_opt(uint8_t *p, uint8_t code, uint8_t len, const void *data) {
    *p++ = code;
    *p++ = len;
    memcpy(p, data, len);
    return p + len;
}

/* ── Helper: find option value in options buffer ─────────────────────────── */
static const uint8_t *find_opt(const uint8_t *opts, size_t opts_len, uint8_t code) {
    size_t i = 0;
    while (i < opts_len) {
        if (opts[i] == DHCP_OPT_END) break;
        if (opts[i] == 0) { i++; continue; } /* pad */
        if (opts[i] == code) return &opts[i + 2];
        i += 2u + opts[i + 1];
    }
    return NULL;
}

/* ── Send DHCP reply ─────────────────────────────────────────────────────── */
static void send_reply(const dhcp_msg_t *req, uint8_t msg_type,
                       ip4_addr_t yiaddr) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(dhcp_msg_t), PBUF_RAM);
    if (!p) return;

    dhcp_msg_t *rep = (dhcp_msg_t *)p->payload;
    memset(rep, 0, sizeof(*rep));

    rep->op     = 2u; /* BOOTREPLY */
    rep->htype  = 1u; /* Ethernet */
    rep->hlen   = 6u;
    rep->xid    = req->xid;
    rep->flags  = req->flags;
    rep->yiaddr = yiaddr.addr;
    rep->siaddr = s_dhcp.gw.addr;
    memcpy(rep->chaddr, req->chaddr, 6u);
    rep->magic  = lwip_htonl(DHCP_MAGIC_COOKIE);

    uint8_t *o = rep->options;

    /* Option 53 – message type */
    o = write_opt(o, DHCP_OPT_MSG_TYPE, 1, &msg_type);

    /* Option 54 – server identifier */
    o = write_opt(o, DHCP_OPT_SERVER_ID, 4, &s_dhcp.gw.addr);

    /* Option 51 – lease time */
    uint32_t lease = lwip_htonl(DHCP_LEASE_TIME_S);
    o = write_opt(o, DHCP_OPT_LEASE_TIME, 4, &lease);

    /* Option 58 – renewal time (T1 = lease/2) */
    uint32_t renewal = lwip_htonl(DHCP_LEASE_TIME_S / 2u);
    o = write_opt(o, DHCP_OPT_RENEWAL_TIME, 4, &renewal);

    /* Option 59 – rebind time (T2 = lease * 7/8) */
    uint32_t rebind = lwip_htonl(DHCP_LEASE_TIME_S * 7u / 8u);
    o = write_opt(o, DHCP_OPT_REBIND_TIME, 4, &rebind);

    /* Option 1 – subnet mask */
    o = write_opt(o, DHCP_OPT_SUBNET_MASK, 4, &s_dhcp.mask.addr);

    /* Option 3 – router */
    o = write_opt(o, DHCP_OPT_ROUTER, 4, &s_dhcp.gw.addr);

    /* End option */
    *o++ = DHCP_OPT_END;

    /* Resize pbuf to actual used size */
    pbuf_realloc(p, (uint16_t)((uintptr_t)o - (uintptr_t)p->payload));

    /* Broadcast reply */
    ip4_addr_t dst;
    IP4_ADDR(&dst, 255, 255, 255, 255);
    udp_sendto(s_dhcp.pcb, p, &dst, DHCP_CLIENT_PORT);
    pbuf_free(p);
}

/* ── UDP receive callback ─────────────────────────────────────────────────── */
static void dhcp_recv(void *arg, struct udp_pcb *pcb,
                      struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr; (void)port;

    if (p->tot_len < (u16_t)sizeof(dhcp_msg_t)) {
        pbuf_free(p);
        return;
    }

    dhcp_msg_t req;
    pbuf_copy_partial(p, &req, sizeof(req), 0);
    pbuf_free(p);

    if (req.op != 1u) return;  /* only BOOTREQUEST */
    if (lwip_ntohl(req.magic) != DHCP_MAGIC_COOKIE) return;

    const uint8_t *mt = find_opt(req.options, sizeof(req.options), DHCP_OPT_MSG_TYPE);
    if (!mt) return;

    switch (*mt) {
    case DHCP_DISCOVER:
        memcpy(s_dhcp.client_mac, req.chaddr, 6u);
        send_reply(&req, DHCP_OFFER, s_dhcp.client_ip);
        break;

    case DHCP_REQUEST: {
        /* Accept if MAC matches or no active lease yet */
        bool mac_match = (memcmp(s_dhcp.client_mac, req.chaddr, 6u) == 0);
        if (!s_dhcp.lease_active || mac_match) {
            memcpy(s_dhcp.client_mac, req.chaddr, 6u);
            s_dhcp.lease_active = true;
            send_reply(&req, DHCP_ACK, s_dhcp.client_ip);
        } else {
            uint8_t nak = DHCP_NAK;
            send_reply(&req, nak, s_dhcp.client_ip);
        }
        break;
    }

    case DHCP_RELEASE:
        if (memcmp(s_dhcp.client_mac, req.chaddr, 6u) == 0) {
            s_dhcp.lease_active = false;
            memset(s_dhcp.client_mac, 0, 6u);
        }
        break;

    default:
        break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void dhcp_server_init(ip4_addr_t *gw, ip4_addr_t *mask) {
    memset(&s_dhcp, 0, sizeof(s_dhcp));
    s_dhcp.gw   = *gw;
    s_dhcp.mask = *mask;
    IP4_ADDR(&s_dhcp.client_ip,
             CLIENT_IP_ADDR_B0, CLIENT_IP_ADDR_B1,
             CLIENT_IP_ADDR_B2, CLIENT_IP_ADDR_B3);

    s_dhcp.pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!s_dhcp.pcb) return;

    udp_bind(s_dhcp.pcb, IP4_ADDR_ANY, DHCP_SERVER_PORT);
    udp_recv(s_dhcp.pcb, dhcp_recv, NULL);
}

void dhcp_server_deinit(void) {
    if (s_dhcp.pcb) {
        udp_remove(s_dhcp.pcb);
        s_dhcp.pcb = NULL;
    }
}
