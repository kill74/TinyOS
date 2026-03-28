/* packet.c — Network packet buffer management */

#include "../net/packet.h"
#include "../include/vga.h"
#include "../include/log.h"
#include <stddef.h>

/* ── Static pool ─────────────────────────────────────────────────────────── */
#define PACKET_POOL_SIZE  32

static packet_buf_t pool[PACKET_POOL_SIZE];
static uint8_t pool_used[PACKET_POOL_SIZE];

/* ── Public API ──────────────────────────────────────────────────────────── */

void packet_init(void)
{
    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        pool_used[i] = 0;
        pool[i].len = 0;
        pool[i].offset = 0;
    }
    LOG_INFO("Packet pool initialized (%d buffers x %d bytes)",
             PACKET_POOL_SIZE, PACKET_BUF_SIZE);
}

packet_buf_t *packet_alloc(void)
{
    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        if (!pool_used[i]) {
            pool_used[i] = 1;
            pool[i].len = 0;
            pool[i].offset = 0;
            return &pool[i];
        }
    }
    return NULL;
}

void packet_free(packet_buf_t *pkt)
{
    if (!pkt) return;
    int idx = (int)(pkt - pool);
    if (idx >= 0 && idx < PACKET_POOL_SIZE)
        pool_used[idx] = 0;
}

void packet_reset(packet_buf_t *pkt)
{
    if (pkt) pkt->offset = 0;
}

void packet_advance(packet_buf_t *pkt, uint32_t n)
{
    if (pkt) {
        pkt->offset += n;
        if (pkt->offset > pkt->len)
            pkt->offset = pkt->len;
    }
}

uint8_t *packet_data(packet_buf_t *pkt)
{
    return pkt ? &pkt->data[pkt->offset] : NULL;
}

uint32_t packet_remaining(packet_buf_t *pkt)
{
    if (!pkt || pkt->offset > pkt->len) return 0;
    return pkt->len - pkt->offset;
}

int packet_write(packet_buf_t *pkt, const void *src, uint32_t len)
{
    if (!pkt || !src) return -1;
    if (pkt->len + len > PACKET_BUF_SIZE) return -1;

    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < len; i++)
        pkt->data[pkt->len++] = s[i];
    return 0;
}
