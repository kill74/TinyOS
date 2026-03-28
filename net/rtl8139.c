/* rtl8139.c — Realtek RTL8139 NIC driver
 *
 * This driver initialises the RTL8139 PCI NIC, sets up DMA receive/transmit
 * buffers, handles IRQ-driven packet reception, and provides raw frame TX.
 *
 * Designed for QEMU's default RTL8139 emulation (I/O base 0xC000, IRQ 11).
 */

#include "../net/rtl8139.h"
#include "../net/ethernet.h"
#include "../include/irq.h"
#include "../include/vga.h"
#include "../include/log.h"
#include <stdint.h>
#include <stddef.h>

/* ── I/O helpers (defined in helpers.S) ──────────────────────────────────── */
extern void outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);
extern void outw(uint16_t port, uint16_t value);
extern uint16_t inw(uint16_t port);
extern void outl(uint16_t port, uint32_t value);
extern uint32_t inl(uint16_t port);

/* ── Register offsets (from I/O base) ────────────────────────────────────── */
#define REG_MAC0         0x00
#define REG_MAR0         0x08
#define REG_TSD0         0x10
#define REG_TSAD0        0x20
#define REG_RBSTART      0x30
#define REG_CMD          0x37
#define REG_CAPR         0x38
#define REG_IMR          0x3C
#define REG_ISR          0x3E
#define REG_TCR          0x40
#define REG_RCR          0x44
#define REG_CONFIG1      0x52
#define REG_MPC          0x4C

/* ── Bit masks ───────────────────────────────────────────────────────────── */
#define CMD_BUFE         0x01
#define CMD_TE           0x04
#define CMD_RE           0x08
#define CMD_RST          0x10

#define ISR_ROK          0x0001
#define ISR_TOK          0x0004
#define ISR_FOVW         0x0008
#define ISR_RXOVW        0x0010

#define IMR_ROK          0x0001
#define IMR_TOK          0x0004

#define RCR_WRAP         0x00000080
#define RCR_AB           0x00000001
#define RCR_AM           0x00000002
#define RCR_APM          0x00000004
#define RCR_MXDMA_UNLIM  0x00000700
#define RCR_RBLEN_64K    0x00000000
#define RX_BUF_SIZE      (0x10000 + 1536)  /* 64 KB + 1500 + slack */

#define TSD_TOK          0x00008000
#define TSD_TABT         0x00004000
#define OWN_BIT          0x00008000
#define TCR_MXDMA_UNLIM  0x00000700

#define CONFIG1_LWAKE    0x10  /* bit 4 — set to 0 to power on */

/* ── Module state ────────────────────────────────────────────────────────── */
static uint16_t io_base;
static eth_addr_t nic_mac;

/* DMA receive buffer — must be physically contiguous and below 4 MB.
 * We carve it out of the memory region between kernel image and heap. */
#define RX_BUF_PHYS  0x00300000   /* 3 MB — below heap start */
static uint8_t *rx_buf;           /* virtual pointer (identity-mapped) */

/* TX descriptor indices */
#define TX_BUF_COUNT  4
#define TX_BUF_SIZE   1536
#define TX_BUF_BASE   0x00310000   /* after RX buffer */
static uint8_t *tx_bufs[TX_BUF_COUNT];
static int tx_cur;

/* ── IRQ handler ─────────────────────────────────────────────────────────── */
static void rtl8139_irq_handler(registers_t *regs)
{
    (void)regs;
    uint16_t status = inw(io_base + REG_ISR);

    /* Acknowledge all pending interrupts by writing 1s to ISR */
    outw(io_base + REG_ISR, status);

    if (status & ISR_ROK) {
        /* Packet received — will be polled from timer tick */
    }
}

/* ── Internal: receive one packet ────────────────────────────────────────── */
static void rtl8139_rx_one(void)
{
    /* The RTL8139 writes frames into a 64 KB ring buffer.  Each frame is:
     *   uint16_t header   (status)
     *   uint16_t length
     *   uint8_t  payload[length]
     *   [padding to DWORD boundary]
     *
     * CAPR (Current Address of Packet Read) points 16 bytes before the
     * current read position (hardware quirk). */

    while (!(inb(io_base + REG_CMD) & CMD_BUFE)) {
        uint32_t capr = inw(io_base + REG_CAPR);
        uint32_t offset = (capr + 16) % 0x10000;  /* skip the +16 hw offset */

        /* Header is at rx_buf + offset */
        uint16_t header = *(uint16_t *)(rx_buf + offset);
        uint16_t length = *(uint16_t *)(rx_buf + offset + 2);

        /* Check header validity */
        if ((header & 0x01) == 0 || length < 60 || length > 1518) {
            /* Bad frame — reset CAPR and skip */
            outw(io_base + REG_CAPR, 0xFFF0);
            return;
        }

        /* Allocate a packet buffer and copy the frame */
        packet_buf_t *pkt = packet_alloc();
        if (pkt) {
            /* Copy from ring buffer, handling wrap-around */
            if (offset + 4 + length <= 0x10000) {
                /* No wrap */
                for (uint32_t i = 0; i < length; i++)
                    pkt->data[i] = rx_buf[offset + 4 + i];
            } else {
                /* Wraps around the 64 KB boundary */
                uint32_t first = 0x10000 - (offset + 4);
                uint32_t second = length - first;
                for (uint32_t i = 0; i < first; i++)
                    pkt->data[i] = rx_buf[offset + 4 + i];
                for (uint32_t i = 0; i < second; i++)
                    pkt->data[first + i] = rx_buf[i];
            }
            pkt->len = length;
            pkt->offset = 0;

            /* Pass up to Ethernet layer */
            ethernet_rx(pkt);
            packet_free(pkt);
        }

        /* Advance CAPR to next frame (DWORD aligned) */
        uint32_t advance = (4 + length + 3) & ~3u;
        capr = (capr + advance) % 0x10000;
        outw(io_base + REG_CAPR, (uint16_t)((capr - 16) & 0xFFFF));
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int rtl8139_init(void)
{
    io_base = RTL8139_DEFAULT_IOBASE;
    rx_buf = (uint8_t *)RX_BUF_PHYS;
    tx_cur = 0;

    LOG_INFO("RTL8139: init at I/O 0x%x", (unsigned)io_base);

    /* Power on: clear LWAKE bit in CONFIG1 */
    outb(io_base + REG_CONFIG1, inb(io_base + REG_CONFIG1) & ~CONFIG1_LWAKE);

    /* Software reset */
    outb(io_base + REG_CMD, CMD_RST);
    while (inb(io_base + REG_CMD) & CMD_RST) {
        /* spin until reset completes */
    }
    LOG_INFO("RTL8139: reset complete");

    /* Read MAC address (6 bytes starting at MAC0) */
    for (int i = 0; i < 6; i++)
        nic_mac.bytes[i] = inb(io_base + REG_MAC0 + i);

    LOG_INFO("RTL8139: MAC %x:%x:%x:%x:%x:%x",
             nic_mac.bytes[0], nic_mac.bytes[1], nic_mac.bytes[2],
             nic_mac.bytes[3], nic_mac.bytes[4], nic_mac.bytes[5]);

    /* Set up receive buffer (must be physically contiguous) */
    outl(io_base + REG_RBSTART, RX_BUF_PHYS);
    LOG_INFO("RTL8139: RX buffer at phys 0x%x", RX_BUF_PHYS);

    /* Set up TX buffer pointers */
    for (int i = 0; i < TX_BUF_COUNT; i++)
        tx_bufs[i] = (uint8_t *)(TX_BUF_BASE + i * TX_BUF_SIZE);

    /* Configure receive: wrap, accept broadcast, multicast, physical match */
    outl(io_base + REG_RCR,
         RCR_AB | RCR_AM | RCR_APM | RCR_MXDMA_UNLIM | RCR_RBLEN_64K);

    /* Configure transmit: unlimited DMA burst */
    outl(io_base + REG_TCR, TCR_MXDMA_UNLIM);

    /* Enable TX and RX */
    outb(io_base + REG_CMD, CMD_TE | CMD_RE);

    /* Enable RX-OK and TX-OK interrupts */
    outw(io_base + REG_IMR, IMR_ROK | IMR_TOK);

    /* Register IRQ handler */
    irq_register_handler(RTL8139_DEFAULT_IRQ - 8, rtl8139_irq_handler);
    irq_enable(RTL8139_DEFAULT_IRQ - 8);

    LOG_INFO("RTL8139: ready (IRQ %d)", RTL8139_DEFAULT_IRQ);
    return 0;
}

int rtl8139_tx(const uint8_t *data, uint32_t len)
{
    if (len > TX_BUF_SIZE || len < 60) {
        LOG_WARN("RTL8139: TX bad length %u", len);
        return -1;
    }

    /* Copy frame into current TX buffer */
    uint8_t *buf = tx_bufs[tx_cur];
    for (uint32_t i = 0; i < len; i++)
        buf[i] = data[i];

    /* Tell the NIC to transmit: write physical address to TSADn */
    uint32_t phys = TX_BUF_BASE + tx_cur * TX_BUF_SIZE;
    outl(io_base + REG_TSAD0 + tx_cur * 4, phys);

    /* Write length and ownership to TSDn (starts transmission) */
    outl(io_base + REG_TSD0 + tx_cur * 4, len);

    /* Move to next descriptor */
    tx_cur = (tx_cur + 1) % TX_BUF_COUNT;
    return 0;
}

void rtl8139_poll(void)
{
    rtl8139_rx_one();
}

const eth_addr_t *rtl8139_get_mac(void)
{
    return &nic_mac;
}

void rtl8139_dump_status(void)
{
    uint8_t cmd = inb(io_base + REG_CMD);
    uint16_t isr = inw(io_base + REG_ISR);
    uint16_t capr = inw(io_base + REG_CAPR);
    vga_printf("  RTL8139: CMD=0x%x ISR=0x%x CAPR=%u MAC=%x:%x:%x:%x:%x:%x\n",
               cmd, isr, capr,
               nic_mac.bytes[0], nic_mac.bytes[1], nic_mac.bytes[2],
               nic_mac.bytes[3], nic_mac.bytes[4], nic_mac.bytes[5]);
}
