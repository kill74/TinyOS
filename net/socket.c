/* socket.c — BSD-style socket layer for TinyOS */

#include "../net/socket.h"
#include "../net/tcp.h"
#include "../net/udp.h"
#include "../net/ip.h"
#include "../include/log.h"
#include <stdint.h>

/* ── Module state ────────────────────────────────────────────────────────── */
socket_t sockets[SOCKET_MAX];

/* ── Internal helpers ────────────────────────────────────────────────────── */

static socket_t *sock_get(int fd)
{
    if (fd < 0 || fd >= SOCKET_MAX) return NULL;
    if (sockets[fd].state == SOCK_FREE) return NULL;
    return &sockets[fd];
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void socket_init(void)
{
    for (int i = 0; i < SOCKET_MAX; i++) {
        sockets[i].state = SOCK_FREE;
        sockets[i].tcp_idx = -1;
    }
    LOG_INFO("Socket: initialized (%d slots)", SOCKET_MAX);
}

int socket_create(int type)
{
    for (int i = 0; i < SOCKET_MAX; i++) {
        if (sockets[i].state == SOCK_FREE) {
            sockets[i].state = SOCK_CREATED;
            sockets[i].type = type;
            sockets[i].local_port = 0;
            sockets[i].local_addr = INADDR_ANY;
            sockets[i].remote_port = 0;
            sockets[i].remote_addr = 0;
            sockets[i].tcp_idx = -1;
            sockets[i].backlog = 0;
            sockets[i].accept_head = 0;
            sockets[i].accept_tail = 0;
            LOG_DEBUG("Socket: created fd %d (type %d)", i, type);
            return i;
        }
    }
    return -1;
}

int socket_bind(int fd, const sockaddr_in_t *addr)
{
    socket_t *s = sock_get(fd);
    if (!s || !addr) return -1;
    if (s->state != SOCK_CREATED) return -1;

    s->local_addr = addr->sin_addr;
    s->local_port = addr->sin_port;
    s->state = SOCK_BOUND;
    return 0;
}

int socket_listen(int fd, int backlog)
{
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->state != SOCK_BOUND && s->state != SOCK_CREATED) return -1;
    if (s->type != SOCK_STREAM) return -1;

    if (s->local_port == 0) {
        LOG_WARN("Socket %d: cannot listen without bind", fd);
        return -1;
    }

    /* Allocate a TCB for the listening socket */
    int idx = tcp_alloc();
    if (idx < 0) return -1;

    /* Configure TCB as LISTEN */
    tcp_set_listen(idx, s->local_port, fd);

    s->tcp_idx = idx;
    s->backlog = (backlog > 0 && backlog <= 4) ? backlog : 4;
    s->accept_head = 0;
    s->accept_tail = 0;
    s->state = SOCK_LISTENING;

    LOG_DEBUG("Socket %d: listening on port %u", fd, s->local_port);
    return 0;
}

int socket_accept(int fd, sockaddr_in_t *peer)
{
    socket_t *s = sock_get(fd);
    if (!s || s->state != SOCK_LISTENING) return -1;

    /* Check accept queue */
    if (s->accept_head == s->accept_tail)
        return -1; /* no pending connections */

    int child_tcb = s->accept_queue[s->accept_head];
    s->accept_head = (s->accept_head + 1) % 4;

    /* Create a new socket for this connection */
    int new_fd = socket_create(SOCK_STREAM);
    if (new_fd < 0) return -1;

    sockets[new_fd].tcp_idx = child_tcb;
    sockets[new_fd].state = SOCK_CONNECTED;

    if (peer) {
        peer->sin_family = AF_INET;
        peer->sin_addr = 0;
        peer->sin_port = 0;
    }

    LOG_DEBUG("Socket %d: accepted new conn on fd %d", fd, new_fd);
    return new_fd;
}

int socket_connect(int fd, const sockaddr_in_t *addr)
{
    socket_t *s = sock_get(fd);
    if (!s || !addr) return -1;
    if (s->state != SOCK_CREATED && s->state != SOCK_BOUND) return -1;
    if (s->type != SOCK_STREAM) return -1;

    /* Allocate a TCB */
    int idx = tcp_alloc();
    if (idx < 0) return -1;

    s->tcp_idx = idx;
    s->remote_addr = addr->sin_addr;
    s->remote_port = addr->sin_port;
    s->state = SOCK_CONNECTING;

    if (s->local_port == 0)
        s->local_port = 0xC000 + (uint16_t)(idx * 100);

    /* Configure TCB for active open */
    {
        extern tcb_t tcbs[];
        tcb_t *tcb = &tcbs[idx];
        tcb->local_ip = local_ip;
        tcb->local_port = s->local_port;
        tcb->remote_ip = addr->sin_addr;
        tcb->remote_port = addr->sin_port;
        tcb->snd_una = tcp_next_isn();
        tcb->snd_nxt = tcb->snd_una;
        tcb->rcv_nxt = 0;
        tcb->snd_wnd = 0;
        tcb->rcv_wnd = TCP_WINDOW;
        tcb->rtx_timer = 0;
        tcb->rtx_count = 0;
        tcb->socket_id = fd;
        tcb->state = TCP_ST_SYN_SENT;
    }

    /* Send SYN */
    tcp_send_segment(idx, TCP_SYN, NULL, 0);
    LOG_DEBUG("Socket %d: SYN sent to %x:%u", fd, addr->sin_addr, addr->sin_port);

    return 0;
}

int socket_send(int fd, const uint8_t *buf, uint32_t len)
{
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->state != SOCK_CONNECTED) return -1;
    if (s->tcp_idx < 0) return -1;

    return tcp_send_data(s->tcp_idx, buf, len);
}

int socket_recv(int fd, uint8_t *buf, uint32_t maxlen)
{
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->state != SOCK_CONNECTED && s->state != SOCK_CLOSING) return -1;
    if (s->tcp_idx < 0) return -1;

    return tcp_recv_data(s->tcp_idx, buf, maxlen);
}

int socket_close(int fd)
{
    socket_t *s = sock_get(fd);
    if (!s) return -1;

    if (s->type == SOCK_STREAM && s->tcp_idx >= 0) {
        extern tcb_t tcbs[];
        tcb_t *tcb = &tcbs[s->tcp_idx];

        if (tcb->state == TCP_ST_ESTABLISHED) {
            tcb->state = TCP_ST_FIN_WAIT_1;
            tcp_send_segment(s->tcp_idx, TCP_FIN | TCP_ACK, NULL, 0);
            s->state = SOCK_CLOSING;
        } else if (tcb->state == TCP_ST_CLOSE_WAIT) {
            tcb->state = TCP_ST_LAST_ACK;
            tcp_send_segment(s->tcp_idx, TCP_FIN | TCP_ACK, NULL, 0);
            s->state = SOCK_CLOSING;
        } else {
            tcp_free(s->tcp_idx);
            s->state = SOCK_FREE;
        }
    } else {
        s->state = SOCK_FREE;
    }

    return 0;
}
