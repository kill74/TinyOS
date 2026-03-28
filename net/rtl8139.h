/* rtl8139.h — Realtek RTL8139 NIC driver interface */
#pragma once
#include <stdint.h>
#include "packet.h"
#include "ethernet.h"

/* Default I/O base (QEMU default for RTL8139) */
#define RTL8139_DEFAULT_IOBASE  0xC000

/* Default IRQ line */
#define RTL8139_DEFAULT_IRQ     11

/* Initialise the RTL8139 NIC.
 * Sets up DMA buffers, enables RX/TX, registers IRQ handler. */
int rtl8139_init(void);

/* Transmit a raw Ethernet frame.
 * `pkt` must contain the complete frame (dst MAC + src MAC + ethertype + payload).
 * Returns 0 on success, -1 on failure. */
int rtl8139_tx(const uint8_t *data, uint32_t len);

/* Poll for received packets (called from timer tick).
 * Dequeues all pending frames and passes them to ethernet_rx(). */
void rtl8139_poll(void);

/* Get the MAC address read from the NIC */
const eth_addr_t *rtl8139_get_mac(void);

/* Print NIC status (for debugging) */
void rtl8139_dump_status(void);
