/* ethernet.c — Ethernet frame handling */

#include "../net/ethernet.h"
#include "../net/rtl8139.h"
#include "../net/arp.h"
#include "../net/ip.h"
#include "../include/log.h"
#include <stdint.h>
#include <stddef.h>

/* ── Module state ────────────────────────────────────────────────────────── */
static eth_addr_t local_mac;

const eth_addr_t eth_broadcast = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };

/* ── Public API ──────────────────────────────────────────────────────────── */

void ethernet_init(eth_addr_t mac)
{
    local_mac = mac;
    LOG_INFO("Ethernet: local MAC %x:%x:%x:%x:%x:%x",
             mac.bytes[0], mac.bytes[1], mac.bytes[2],
             mac.bytes[3], mac.bytes[4], mac.bytes[5]);
}

const eth_addr_t *ethernet_get_mac(void)
{
    return &local_mac;
}

void ethernet_rx(packet_buf_t *pkt)
{
    if (pkt->len < ETH_HEADER_LEN) return;

    eth_hdr_t *hdr = (eth_hdr_t *)pkt->data;

    /* Filter: accept unicast to us, broadcast, and multicast IPv4 */
    if (!eth_addr_eq(&hdr->dst, &local_mac) &&
        !eth_addr_eq(&hdr->dst, &eth_broadcast)) {
        return; /* Not for us */
    }

    /* Dispatch by EtherType */
    uint16_t etype = hdr->ethertype;
    packet_advance(pkt, ETH_HEADER_LEN);

    if (etype == ETH_TYPE_ARP) {
        arp_rx(pkt);
    } else if (etype == ETH_TYPE_IPV4) {
        ip_rx(pkt);
    } else {
        /* Unknown ethertype — silently drop */
    }
}

int ethernet_tx(packet_buf_t *pkt, eth_addr_t dst, uint16_t ethertype)
{
    if (!pkt) return -1;

    /* Prepend Ethernet header — shift existing data right */
    if (pkt->len + ETH_HEADER_LEN > PACKET_BUF_SIZE) return -1;

    /* Move payload forward to make room for header */
    for (int32_t i = (int32_t)pkt->len - 1; i >= 0; i--)
        pkt->data[i + ETH_HEADER_LEN] = pkt->data[i];
    pkt->len += ETH_HEADER_LEN;
    pkt->offset = 0;

    /* Fill header */
    eth_hdr_t *hdr = (eth_hdr_t *)pkt->data;
    hdr->dst = dst;
    hdr->src = local_mac;
    hdr->ethertype = ethertype;

    /* Hand to NIC driver */
    return rtl8139_tx(pkt->data, pkt->len);
}
