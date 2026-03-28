/* socket.h — BSD-style socket API for TinyOS */
#pragma once
#include <stdint.h>

/* Socket types */
#define SOCK_STREAM  1   /* TCP */
#define SOCK_DGRAM   2   /* UDP */

/* Address families */
#define AF_INET      2

/* Special values */
#define INADDR_ANY   0x00000000

/* Socket states */
typedef enum {
    SOCK_FREE,
    SOCK_CREATED,
    SOCK_BOUND,
    SOCK_LISTENING,
    SOCK_CONNECTING,
    SOCK_CONNECTED,
    SOCK_CLOSING
} sock_state_t;

/* IPv4 socket address (matches struct sockaddr_in layout) */
typedef struct sockaddr_in {
    uint16_t sin_family;   /* AF_INET */
    uint16_t sin_port;     /* Port (network byte order) */
    uint32_t sin_addr;     /* IPv4 address (network byte order) */
} sockaddr_in_t;

/* Maximum simultaneous sockets */
#define SOCKET_MAX  16

/* Per-socket descriptor */
typedef struct socket {
    sock_state_t state;
    int          type;         /* SOCK_STREAM or SOCK_DGRAM */
    uint16_t     local_port;
    uint32_t     local_addr;
    uint16_t     remote_port;
    uint32_t     remote_addr;
    int          tcp_idx;      /* Index into TCB array (-1 if UDP) */

    /* For listening sockets: queue of pending connections */
    int          backlog;
    int          accept_queue[4];
    int          accept_head;
    int          accept_tail;
} socket_t;

/* ── Public socket API ─────────────────────────────────────────────────── */

/* Initialise the socket layer */
void socket_init(void);

/* Create a new socket. Returns socket index (>= 0) or -1 on error. */
int socket_create(int type);

/* Bind a socket to a local address/port. Returns 0 on success. */
int socket_bind(int fd, const sockaddr_in_t *addr);

/* Mark a TCP socket as passive-open (listening). Returns 0 on success. */
int socket_listen(int fd, int backlog);

/* Accept a new connection on a listening socket. Returns new fd or -1. */
int socket_accept(int fd, sockaddr_in_t *peer);

/* Actively open a TCP connection. Returns 0 on success, -1 on error. */
int socket_connect(int fd, const sockaddr_in_t *addr);

/* Send data. Returns bytes sent or -1. */
int socket_send(int fd, const uint8_t *buf, uint32_t len);

/* Receive data. Returns bytes read or -1 (0 = connection closed). */
int socket_recv(int fd, uint8_t *buf, uint32_t maxlen);

/* Close a socket. Returns 0 on success. */
int socket_close(int fd);
