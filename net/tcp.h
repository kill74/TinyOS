/* tcp.h — TCP protocol with state machine */
#pragma once
#include <stdint.h>
#include "packet.h"

/* TCP flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* TCP connection states */
typedef enum {
    TCP_ST_CLOSED,
    TCP_ST_LISTEN,
    TCP_ST_SYN_SENT,
    TCP_ST_SYN_RCVD,
    TCP_ST_ESTABLISHED,
    TCP_ST_FIN_WAIT_1,
    TCP_ST_FIN_WAIT_2,
    TCP_ST_CLOSE_WAIT,
    TCP_ST_CLOSING,
    TCP_ST_LAST_ACK,
    TCP_ST_TIME_WAIT
} tcp_state_t;

/* Buffer sizes per connection */
#define TCP_SND_BUF   4096
#define TCP_RCV_BUF   4096
#define TCP_MSS       1460

/* Max simultaneous TCP connections */
#define TCP_MAX_CONN  8

/* TCP receive window (exposed for socket layer) */
#define TCP_WINDOW       (TCP_RCV_BUF - 1)

/* TCP header (20 bytes without options) */
typedef struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  off;          /* Data offset (4 bits) + reserved (4 bits) */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

/* Pseudo-header for TCP checksum */
typedef struct tcp_pseudo {
    uint32_t src;
    uint32_t dst;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_len;
} __attribute__((packed)) tcp_pseudo_t;

/* TCP Control Block — one per connection */
typedef struct tcb {
    tcp_state_t state;
    uint8_t     in_use;

    /* Socket index that owns this TCB (-1 = unassigned) */
    int         socket_id;

    /* 4-tuple */
    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;

    /* Sequence numbers */
    uint32_t    snd_una;    /* Oldest unacknowledged */
    uint32_t    snd_nxt;    /* Next to send */
    uint32_t    snd_wnd;    /* Send window */
    uint32_t    rcv_nxt;    /* Next expected */
    uint32_t    rcv_wnd;    /* Receive window */

    /* Send buffer (ring) */
    uint8_t     snd_buf[TCP_SND_BUF];
    uint32_t    snd_head;
    uint32_t    snd_tail;

    /* Receive buffer (ring) */
    uint8_t     rcv_buf[TCP_RCV_BUF];
    uint32_t    rcv_head;
    uint32_t    rcv_tail;

    /* Retransmission timer */
    uint32_t    rtx_timer;
    uint32_t    rtx_timeout;
    uint8_t     rtx_count;

    /* Time-wait timer */
    uint32_t    tw_timer;

    /* Pending SYN for passive open */
    uint32_t    pending_syn_seq;
    uint16_t    pending_syn_port;
} tcb_t;

/* Initialise the TCP subsystem */
void tcp_init(void);

/* Handle an incoming TCP segment (IP payload) */
void tcp_rx(packet_buf_t *pkt, uint32_t src_ip, uint32_t dst_ip);

/* Periodic tick — retransmissions, keep-alive, time-wait cleanup */
void tcp_tick(void);

/* Allocate a TCB (returns index, or -1) */
int tcp_alloc(void);

/* Free a TCB by index */
void tcp_free(int idx);

/* Find TCB by 4-tuple (returns index, or -1) */
int tcp_lookup(uint32_t src_ip, uint16_t src_port,
               uint32_t dst_ip, uint16_t dst_port);

/* Find a TCB in LISTEN state bound to a port (returns index, or -1) */
int tcp_listen_lookup(uint16_t port);

/* Queue data into the send buffer */
int tcp_send_data(int idx, const uint8_t *data, uint32_t len);

/* Read data from the receive buffer */
int tcp_recv_data(int idx, uint8_t *buf, uint32_t maxlen);

/* Send a segment (with flags) */
int tcp_send_segment(int idx, uint8_t flags, const uint8_t *data, uint32_t len);

/* Send RST in response to an unexpected segment */
int tcp_send_rst(packet_buf_t *pkt, uint32_t src_ip, uint32_t dst_ip);

/* Set a TCB to LISTEN state, bound to `port` */
int tcp_set_listen(int idx, uint16_t port, int socket_id);

/* Get next initial sequence number */
uint32_t tcp_next_isn(void);

/* Enqueue an accepted child TCB on a listening TCB's socket */
void tcp_accept_enqueue(int listen_tcb, int child_tcb);

/* TCB array — accessible from socket layer for direct state manipulation */
extern tcb_t tcbs[];
