/* ========================================================================
 * nSocket.c — Manios Socket Plugin
 *
 * Plugin mode (khong phai CLI):
 *   gcc -shared -fPIC -I/usr/local/share/manios/include \
 *       -o nSocket.so nSocket.c -lpthread
 *
 * Load trong Manios:
 *   load_ext("nSocket.so")
 *
 * Ham:
 *   sock_connect(ip, port)  -> int fd (hoac -1 neu loi)
 *   sock_send(fd, data)     -> int bytes da gui (-1 neu loi)
 *   sock_close(fd)          -> bool
 *   sock_burst(ip, port, data, count)
 *                           -> int tong bytes gui duoc
 * ======================================================================== */

#include "mnos_ext.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* ── sock_connect(ip, port) -> int fd ── */
static Val sc_connect(Val *a, int n) {
    if (n < 2 || a[0].type != V_STR || (a[1].type != V_INT && a[1].type != V_FLOAT)) {
        fprintf(stderr, "[nSocket] sock_connect(ip, port)\n");
        return val_int(-1);
    }

    const char *ip = a[0].sval;
    int port = (int)a[1].ival;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[nSocket] socket(): %s\n", strerror(errno));
        return val_int(-1);
    }

    /* Tat Nagle algorithm */
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    /* Thu resolve DNS neu can */
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(ip);
        if (!he) {
            fprintf(stderr, "[nSocket] DNS fail: %s\n", ip);
            close(fd);
            return val_int(-2);
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[nSocket] connect(%s:%d): %s\n", ip, port, strerror(errno));
        close(fd);
        return val_int(-3);
    }

    return val_int(fd);
}

/* ── sock_send(fd, data) -> int bytes gui duoc ── */
static Val sc_send(Val *a, int n) {
    if (n < 2 || a[0].type != V_INT) {
        fprintf(stderr, "[nSocket] sock_send(fd, data)\n");
        return val_int(-1);
    }

    int fd = (int)a[0].ival;
    const char *data;
    int data_len;

    if (a[1].type == V_BYTES) {
        data = a[1].sval;
        data_len = a[1].slen;
    } else if (a[1].type == V_STR) {
        data = a[1].sval;
        data_len = a[1].slen;
    } else {
        fprintf(stderr, "[nSocket] sock_send: data phai la bytes hoac string\n");
        return val_int(-1);
    }

    int total = 0;
    while (total < data_len) {
        int sent = (int)send(fd, data + total, data_len - total, 0);
        if (sent < 0) {
            fprintf(stderr, "[nSocket] send(): %s\n", strerror(errno));
            return val_int(-1);
        }
        total += sent;
    }

    return val_int(total);
}

/* ── sock_close(fd) -> bool ── */
static Val sc_close(Val *a, int n) {
    if (n < 1 || a[0].type != V_INT) {
        return val_bool(0);
    }
    int fd = (int)a[0].ival;
    int r = close(fd);
    return val_bool(r == 0);
}

/* ── sock_burst(ip, port, data, count) -> int tong bytes ── */
static Val sc_burst(Val *a, int n) {
    if (n < 4 || a[0].type != V_STR || a[3].type != V_INT) {
        fprintf(stderr, "[nSocket] sock_burst(ip, port, data, count)\n");
        return val_int(0);
    }

    const char *ip = a[0].sval;
    int port = (int)a[1].ival;
    int count = (int)a[3].ival;
    int data_type = a[2].type;

    const char *data;
    int data_len;

    if (data_type == V_BYTES) {
        data = a[2].sval;
        data_len = a[2].slen;
    } else if (data_type == V_STR) {
        data = a[2].sval;
        data_len = a[2].slen;
    } else {
        fprintf(stderr, "[nSocket] sock_batch: data phai la bytes hoac string\n");
        return val_int(0);
    }

    if (count < 1) count = 1;
    if (data_len < 1) return val_int(0);

    /* Phan giai IP */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(ip);
        if (!he) {
            fprintf(stderr, "[nSocket] DNS fail: %s\n", ip);
            return val_int(0);
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    long long total_sent = 0;

    for (int i = 0; i < count; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            continue;
        }

        int sent = (int)send(fd, data, data_len, 0);
        if (sent > 0) total_sent += sent;

        close(fd);
    }

    return val_int(total_sent);
}

/* ── Dang ky ── */
MNOS_EXT_BEGIN(nsocket)
    MNOS_EXT_FUNC("sock_connect", sc_connect, "Mo ket noi TCP: sock_connect(ip, port) -> fd")
    MNOS_EXT_FUNC("sock_send",    sc_send,    "Gui du lieu: sock_send(fd, data) -> bytes sent")
    MNOS_EXT_FUNC("sock_close",   sc_close,   "Dong ket noi: sock_close(fd) -> bool")
    MNOS_EXT_FUNC("sock_burst",  sc_burst,  "Gui nhieu ket noi: sock_burst(ip, port, data, count) -> total")
MNOS_EXT_END
