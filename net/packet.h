/* packet.h — Network packet buffer management */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* Maximum packet size (Ethernet MTU + headers) */
#define PACKET_BUF_SIZE 1600

/* Packet buffer — owns a contiguous region for network I/O */
typedef struct packet_buf {
    uint8_t  data[PACKET_BUF_SIZE];
    uint32_t len;       /* Number of valid bytes in data[] */
    uint32_t offset;    /* Read cursor (used during header parsing) */
} packet_buf_t;

/* Allocate a fresh packet buffer (returns NULL if pool exhausted) */
packet_buf_t *packet_alloc(void);

/* Return a packet buffer to the free pool */
void packet_free(packet_buf_t *pkt);

/* Reset offset to 0 (re-parse from start) */
void packet_reset(packet_buf_t *pkt);

/* Advance the read cursor by `n` bytes */
void packet_advance(packet_buf_t *pkt, uint32_t n);

/* Return a pointer to the current read position */
uint8_t *packet_data(packet_buf_t *pkt);

/* Remaining readable bytes from cursor */
uint32_t packet_remaining(packet_buf_t *pkt);

/* Append `len` bytes from `src` at the end of the packet */
int packet_write(packet_buf_t *pkt, const void *src, uint32_t len);

/* Initialise the packet buffer pool (call once at boot) */
void packet_init(void);
