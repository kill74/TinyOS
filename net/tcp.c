/* tcp.c — TCP protocol implementation with state machine
 *
 * Implements the core TCP operations: 3-way handshake, data transfer with
 * sequence-number tracking, and connection teardown. Retransmission is
 * timer-driven with a fixed 200 ms timeout (20 ticks at 100 Hz).
 */

#include "../net/tcp.h"
#include "../net/ip.h"
#include "../net/socket.h"
#include "../include/log.h"
#include "../include/timer.h"
#include <stdint.h>
#include <stddef.h>

/* ── Constants ───────────────────────────────────────────────────────────── */
#define TCP_RTX_TIMEOUT  20   /* 200 ms at 100 Hz */
#define TCP_RTX_MAX      5
#define TCP_TW_TIMEOUT   200  /* 2 seconds TIME-WAIT */
#define TCP_ISN_BASE     0x12345678

/* ── Module state ────────────────────────────────────────────────────────── */
tcb_t tcbs[TCP_MAX_CONN];
static uint32_t isn_counter;

/* ── Internal helpers ────────────────────────────────────────────────────── */

uint32_t tcp_next_isn(void)
{
    isn_counter += 64000;  /* advance by ~ MSS per call */
    return TCP_ISN_BASE + isn_counter;
}

/* Ring buffer helpers */
static uint32_t ring_used(uint32_t head, uint32_t tail, uint32_t size)
{
    if (head >= tail) return head - tail;
    return size - (tail - head);
}

static uint32_t ring_free(uint32_t head, uint32_t tail, uint32_t size)
{
    return size - ring_used(head, tail, size) - 1;
}

/* Compute TCP checksum */
static uint16_t tcp_checksum(uint32_t src, uint32_t dst,
                             const tcp_hdr_t *tcp, uint32_t tcp_len)
{
    tcp_pseudo_t pseudo;
    pseudo.src = src;
    pseudo.dst = dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_len = tcp_len;

    /* Checksum over pseudo-header + TCP segment */
    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)&pseudo;
    for (uint32_t i = 0; i < sizeof(pseudo) / 2; i++)
        sum += p[i];

    p = (const uint16_t *)tcp;
    for (uint32_t i = 0; i < tcp_len / 2; i++)
        sum += p[i];
    if (tcp_len & 1)
        sum += (uint16_t)(((const uint8_t *)tcp)[tcp_len - 1]) << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void tcp_init(void)
{
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        tcbs[i].in_use = 0;
        tcbs[i].state = TCP_ST_CLOSED;
    }
    isn_counter = 0;
    LOG_INFO("TCP: initialized (%d TCBs)", TCP_MAX_CONN);
}

int tcp_alloc(void)
{
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (!tcbs[i].in_use) {
            tcbs[i].in_use = 1;
            tcbs[i].state = TCP_ST_CLOSED;
            tcbs[i].socket_id = -1;
            tcbs[i].snd_head = 0;
            tcbs[i].snd_tail = 0;
            tcbs[i].rcv_head = 0;
            tcbs[i].rcv_tail = 0;
            tcbs[i].rtx_timer = 0;
            tcbs[i].rtx_count = 0;
            return i;
        }
    }
    return -1;
}

void tcp_free(int idx)
{
    if (idx >= 0 && idx < TCP_MAX_CONN) {
        tcbs[idx].in_use = 0;
        tcbs[idx].state = TCP_ST_CLOSED;
    }
}

int tcp_lookup(uint32_t src_ip, uint16_t src_port,
               uint32_t dst_ip, uint16_t dst_port)
{
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (!tcbs[i].in_use) continue;
        if (tcbs[i].local_ip == dst_ip &&
            tcbs[i].local_port == dst_port &&
            tcbs[i].remote_ip == src_ip &&
            tcbs[i].remote_port == src_port)
            return i;
    }
    return -1;
}

int tcp_listen_lookup(uint16_t port)
{
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (tcbs[i].in_use &&
            tcbs[i].state == TCP_ST_LISTEN &&
            tcbs[i].local_port == port)
            return i;
    }
    return -1;
}

/* ── Send a TCP segment ──────────────────────────────────────────────────── */

int tcp_send_segment(int idx, uint8_t flags, const uint8_t *data, uint32_t len)
{
    if (idx < 0 || idx >= TCP_MAX_CONN || !tcbs[idx].in_use)
        return -1;

    tcb_t *tcb = &tcbs[idx];
    uint32_t tcp_hdr_len = sizeof(tcp_hdr_t);
    uint32_t total_len = tcp_hdr_len + len;

    /* Allocate packet buffer */
    packet_buf_t *pkt = packet_alloc();
    if (!pkt) return -1;

    /* Write TCP header + data */
    tcp_hdr_t *hdr = (tcp_hdr_t *)pkt->data;
    hdr->src_port = tcb->local_port;
    hdr->dst_port = tcb->remote_port;
    hdr->seq = tcb->snd_nxt;
    hdr->ack = tcb->rcv_nxt;
    hdr->off = (uint8_t)((tcp_hdr_len / 4) << 4);
    hdr->flags = flags;
    hdr->window = TCP_WINDOW;
    hdr->checksum = 0;
    hdr->urgent = 0;

    if (data && len > 0) {
        for (uint32_t i = 0; i < len; i++)
            pkt->data[tcp_hdr_len + i] = data[i];
    }

    pkt->len = total_len;

    /* Compute checksum */
    hdr->checksum = tcp_checksum(tcb->local_ip, tcb->remote_ip,
                                 hdr, total_len);

    /* Send via IP */
    int rc = ip_tx(pkt, tcb->remote_ip, IP_PROTO_TCP);
    packet_free(pkt);

    /* Update send state */
    if (rc == 0 && (flags & TCP_SYN || flags & TCP_FIN))
        tcb->snd_nxt++;
    if (rc == 0 && len > 0)
        tcb->snd_nxt += len;

    return rc;
}

/* ── Handle incoming TCP segment ─────────────────────────────────────────── */

void tcp_rx(packet_buf_t *pkt, uint32_t src_ip, uint32_t dst_ip)
{
    if (pkt->len < sizeof(tcp_hdr_t)) return;

    tcp_hdr_t *hdr = (tcp_hdr_t *)packet_data(pkt);
    uint16_t src_port = hdr->src_port;
    uint16_t dst_port = hdr->dst_port;
    uint8_t flags = hdr->flags;
    uint32_t seq = hdr->seq;
    uint32_t ack = hdr->ack;
    uint32_t hdr_len = ((hdr->off >> 4) & 0x0F) * 4;

    if (hdr_len < sizeof(tcp_hdr_t) || pkt->len < hdr_len) return;

    /* Data in this segment */
    packet_advance(pkt, hdr_len);
    uint32_t data_len = packet_remaining(pkt);

    /* Look up connection */
    int idx = tcp_lookup(src_ip, src_port, dst_ip, dst_port);
    if (idx < 0) {
        /* Check for LISTEN socket */
        idx = tcp_listen_lookup(dst_port);
        if (idx >= 0) {
            tcb_t *tcb = &tcbs[idx];
            /* Create child TCB for this connection */
            int child = tcp_alloc();
            if (child < 0) {
                tcp_send_rst(pkt, src_ip, dst_ip);
                return;
            }
            tcb_t *ct = &tcbs[child];
            ct->local_ip = dst_ip;
            ct->local_port = dst_port;
            ct->remote_ip = src_ip;
            ct->remote_port = src_port;
            ct->state = TCP_ST_SYN_RCVD;
            ct->snd_una = tcp_next_isn();
            ct->snd_nxt = ct->snd_una;
            ct->rcv_nxt = seq + 1;
            ct->snd_wnd = hdr->window;
            ct->rcv_wnd = TCP_WINDOW;
            ct->rtx_timer = timer_get_ticks();
            ct->rtx_timeout = TCP_RTX_TIMEOUT;
            ct->socket_id = tcb->socket_id;

            /* Send SYN-ACK */
            tcp_send_segment(child, TCP_SYN | TCP_ACK, NULL, 0);

            /* Queue for accept */
            extern socket_t sockets[];
            for (int s = 0; s < SOCKET_MAX; s++) {
                if (sockets[s].state == SOCK_LISTENING &&
                    sockets[s].tcp_idx == idx) {
                    int next = (sockets[s].accept_tail + 1) % 4;
                    if (next != sockets[s].accept_head) {
                        sockets[s].accept_queue[sockets[s].accept_tail] = child;
                        sockets[s].accept_tail = next;
                        ct->socket_id = s;
                    }
                    break;
                }
            }
            return;
        }

        /* No listener — send RST if not a RST itself */
        if (!(flags & TCP_RST))
            tcp_send_rst(pkt, src_ip, dst_ip);
        return;
    }

    tcb_t *tcb = &tcbs[idx];

    /* Handle RST */
    if (flags & TCP_RST) {
        LOG_DEBUG("TCP: RST on conn %d", idx);
        if (tcb->state == TCP_ST_SYN_SENT || tcb->state == TCP_ST_SYN_RCVD ||
            tcb->state == TCP_ST_ESTABLISHED) {
            tcb->state = TCP_ST_CLOSED;
            tcp_free(idx);
        }
        return;
    }

    /* State machine */
    switch (tcb->state) {
    case TCP_ST_SYN_SENT:
        if ((flags & TCP_ACK) && (flags & TCP_SYN)) {
            if (ack == tcb->snd_nxt) {
                tcb->snd_una = ack;
                tcb->rcv_nxt = seq + 1;
                tcb->snd_wnd = hdr->window;
                tcb->state = TCP_ST_ESTABLISHED;
                /* Send ACK to complete handshake */
                tcp_send_segment(idx, TCP_ACK, NULL, 0);
                LOG_INFO("TCP: connection %d established", idx);
            }
        } else if (flags & TCP_SYN) {
            /* Simultaneous open */
            tcb->rcv_nxt = seq + 1;
            tcb->state = TCP_ST_SYN_RCVD;
            tcp_send_segment(idx, TCP_SYN | TCP_ACK, NULL, 0);
        }
        break;

    case TCP_ST_SYN_RCVD:
        if (flags & TCP_ACK) {
            if (ack == tcb->snd_nxt) {
                tcb->snd_una = ack;
                tcb->state = TCP_ST_ESTABLISHED;
                LOG_INFO("TCP: connection %d established (passive)", idx);
            }
        }
        break;

    case TCP_ST_ESTABLISHED:
        /* Process ACK */
        if (flags & TCP_ACK) {
            uint32_t acked = ack - tcb->snd_una;
            if (acked > 0 && acked <= ring_used(tcb->snd_head, tcb->snd_tail, TCP_SND_BUF)) {
                tcb->snd_tail = (tcb->snd_tail + acked) % TCP_SND_BUF;
                tcb->snd_una = ack;
            }
            tcb->snd_wnd = hdr->window;
            tcb->rtx_timer = timer_get_ticks(); /* reset rtx timer on ack */
        }

        /* Process data */
        if (data_len > 0) {
            uint32_t space = ring_free(tcb->rcv_head, tcb->rcv_tail, TCP_RCV_BUF);
            uint32_t to_copy = data_len < space ? data_len : space;

            for (uint32_t i = 0; i < to_copy; i++) {
                tcb->rcv_buf[tcb->rcv_head] = pkt->data[pkt->offset + i];
                tcb->rcv_head = (tcb->rcv_head + 1) % TCP_RCV_BUF;
            }
            tcb->rcv_nxt += data_len;

            /* Send ACK for received data */
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
        }

        /* Handle FIN */
        if (flags & TCP_FIN) {
            tcb->rcv_nxt++;
            tcb->state = TCP_ST_CLOSE_WAIT;
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
            LOG_DEBUG("TCP: conn %d → CLOSE_WAIT", idx);
        }
        break;

    case TCP_ST_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            uint32_t acked = ack - tcb->snd_una;
            if (acked > 0) {
                tcb->snd_una = ack;
            }
            if (flags & TCP_FIN) {
                tcb->rcv_nxt++;
                tcp_send_segment(idx, TCP_ACK, NULL, 0);
                tcb->state = TCP_ST_TIME_WAIT;
                tcb->tw_timer = timer_get_ticks();
            } else {
                tcb->state = TCP_ST_FIN_WAIT_2;
            }
        }
        break;

    case TCP_ST_FIN_WAIT_2:
        if (flags & TCP_FIN) {
            tcb->rcv_nxt++;
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
            tcb->state = TCP_ST_TIME_WAIT;
            tcb->tw_timer = timer_get_ticks();
        }
        break;

    case TCP_ST_LAST_ACK:
        if (flags & TCP_ACK) {
            tcb->state = TCP_ST_CLOSED;
            tcp_free(idx);
            LOG_DEBUG("TCP: conn %d closed", idx);
        }
        break;

    default:
        break;
    }
}

/* ── Timer tick — retransmissions and cleanup ────────────────────────────── */

void tcp_tick(void)
{
    uint32_t now = timer_get_ticks();

    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (!tcbs[i].in_use) continue;

        tcb_t *tcb = &tcbs[i];

        /* TIME-WAIT expiry */
        if (tcb->state == TCP_ST_TIME_WAIT) {
            if ((now - tcb->tw_timer) > TCP_TW_TIMEOUT) {
                tcb->state = TCP_ST_CLOSED;
                tcp_free(i);
            }
            continue;
        }

        /* Retransmission check */
        if (tcb->state == TCP_ST_SYN_SENT || tcb->state == TCP_ST_SYN_RCVD ||
            tcb->state == TCP_ST_ESTABLISHED || tcb->state == TCP_ST_FIN_WAIT_1) {
            if (tcb->rtx_count > 0 && (now - tcb->rtx_timer) > tcb->rtx_timeout) {
                if (tcb->rtx_count >= TCP_RTX_MAX) {
                    LOG_WARN("TCP: conn %d max retransmit, closing", i);
                    tcb->state = TCP_ST_CLOSED;
                    tcp_free(i);
                    continue;
                }
                /* Retransmit: resend unacknowledged data */
                uint32_t unsent = ring_used(tcb->snd_head, tcb->snd_tail, TCP_SND_BUF);
                if (unsent > 0) {
                    uint32_t start = tcb->snd_tail;
                    uint32_t len = unsent < TCP_MSS ? unsent : TCP_MSS;
                    uint8_t data[TCP_MSS];
                    for (uint32_t j = 0; j < len; j++)
                        data[j] = tcb->snd_buf[(start + j) % TCP_SND_BUF];
                    tcp_send_segment(i, TCP_ACK | TCP_PSH, data, len);
                }
                tcb->rtx_timer = now;
                tcb->rtx_count++;
            }
        }
    }
}

/* ── Send / Recv data ────────────────────────────────────────────────────── */

int tcp_send_data(int idx, const uint8_t *data, uint32_t len)
{
    if (idx < 0 || idx >= TCP_MAX_CONN || !tcbs[idx].in_use)
        return -1;
    if (tcbs[idx].state != TCP_ST_ESTABLISHED)
        return -1;

    tcb_t *tcb = &tcbs[idx];
    uint32_t free = ring_free(tcb->snd_head, tcb->snd_tail, TCP_SND_BUF);
    uint32_t to_copy = len < free ? len : free;

    if (to_copy == 0) return 0;

    for (uint32_t i = 0; i < to_copy; i++) {
        tcb->snd_buf[tcb->snd_head] = data[i];
        tcb->snd_head = (tcb->snd_head + 1) % TCP_SND_BUF;
    }

    /* Send the data as a segment */
    uint32_t seg_len = to_copy < TCP_MSS ? to_copy : TCP_MSS;
    uint8_t seg_data[TCP_MSS];
    uint32_t pos = tcb->snd_tail;
    for (uint32_t i = 0; i < seg_len; i++) {
        seg_data[i] = tcb->snd_buf[pos];
        pos = (pos + 1) % TCP_SND_BUF;
    }

    tcp_send_segment(idx, TCP_ACK | TCP_PSH, seg_data, seg_len);
    tcb->rtx_timer = timer_get_ticks();
    tcb->rtx_count = 1;

    return (int)to_copy;
}

int tcp_recv_data(int idx, uint8_t *buf, uint32_t maxlen)
{
    if (idx < 0 || idx >= TCP_MAX_CONN || !tcbs[idx].in_use)
        return -1;

    tcb_t *tcb = &tcbs[idx];
    uint32_t avail = ring_used(tcb->rcv_head, tcb->rcv_tail, TCP_RCV_BUF);

    if (avail == 0) {
        if (tcb->state == TCP_ST_CLOSE_WAIT) return 0; /* EOF */
        return -1; /* no data yet */
    }

    uint32_t to_read = maxlen < avail ? maxlen : avail;
    for (uint32_t i = 0; i < to_read; i++) {
        buf[i] = tcb->rcv_buf[tcb->rcv_tail];
        tcb->rcv_tail = (tcb->rcv_tail + 1) % TCP_RCV_BUF;
    }

    return (int)to_read;
}

int tcp_send_rst(packet_buf_t *pkt, uint32_t src_ip, uint32_t dst_ip)
{
    (void)pkt;
    (void)src_ip;
    (void)dst_ip;
    return 0;
}

int tcp_set_listen(int idx, uint16_t port, int socket_id)
{
    if (idx < 0 || idx >= TCP_MAX_CONN) return -1;

    tcb_t *tcb = &tcbs[idx];
    tcb->local_port = port;
    tcb->local_ip = local_ip;
    tcb->state = TCP_ST_LISTEN;
    tcb->socket_id = socket_id;
    return 0;
}
