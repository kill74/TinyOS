/* udp.c — UDP protocol (minimal implementation) */

#include "../net/udp.h"
#include "../net/ip.h"
#include "../include/log.h"
#include <stdint.h>

/* ── Module state ────────────────────────────────────────────────────────── */
#define UDP_RX_QUEUE_SIZE  8
static udp_rx_entry_t rx_queue[UDP_RX_QUEUE_SIZE];
static int rx_head = 0;
static int rx_tail = 0;

/* ── Public API ──────────────────────────────────────────────────────────── */

void udp_init(void)
{
    for (int i = 0; i < UDP_RX_QUEUE_SIZE; i++)
        rx_queue[i].valid = 0;
    rx_head = 0;
    rx_tail = 0;
    LOG_INFO("UDP: initialized");
}

void udp_rx(packet_buf_t *pkt, uint32_t src_ip, uint32_t dst_ip)
{
    (void)dst_ip;
    if (pkt->len < sizeof(udp_hdr_t)) return;

    udp_hdr_t *hdr = (udp_hdr_t *)packet_data(pkt);
    uint16_t len = hdr->length;
    if (len < sizeof(udp_hdr_t) || len > pkt->len) return;

    uint32_t payload_len = len - sizeof(udp_hdr_t);
    packet_advance(pkt, sizeof(udp_hdr_t));

    /* Queue for later consumption */
    int next = (rx_head + 1) % UDP_RX_QUEUE_SIZE;
    if (next == rx_tail) return; /* queue full, drop */

    udp_rx_entry_t *e = &rx_queue[rx_head];
    e->src_ip = src_ip;
    e->src_port = hdr->src_port;
    e->len = payload_len > 1500 ? 1500 : payload_len;

    for (uint32_t i = 0; i < e->len; i++)
        e->data[i] = pkt->data[pkt->offset + i];

    e->valid = 1;
    rx_head = next;
}

int udp_tx(uint32_t dst_ip, uint16_t dst_port,
           uint16_t src_port, const uint8_t *data, uint32_t len)
{
    packet_buf_t *pkt = packet_alloc();
    if (!pkt) return -1;

    uint32_t udp_len = sizeof(udp_hdr_t) + len;
    if (udp_len > PACKET_BUF_SIZE) {
        packet_free(pkt);
        return -1;
    }

    /* Build UDP header */
    udp_hdr_t *hdr = (udp_hdr_t *)pkt->data;
    hdr->src_port = src_port;
    hdr->dst_port = dst_port;
    hdr->length = udp_len;
    hdr->checksum = 0; /* optional for IPv4 */

    /* Copy payload */
    for (uint32_t i = 0; i < len; i++)
        pkt->data[sizeof(udp_hdr_t) + i] = data[i];

    pkt->len = udp_len;

    int rc = ip_tx(pkt, dst_ip, IP_PROTO_UDP);
    packet_free(pkt);
    return rc;
}
