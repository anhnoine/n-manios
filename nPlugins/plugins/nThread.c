/* ========================================================================
 * nThread.c — Manios Worker Thread Plugin
 *
 * Plugin mode:
 *   gcc -shared -fPIC -I/usr/local/share/manios/include \
 *       -o nThread.so nThread.c -lpthread
 *
 * Load trong Manios:
 *   load_ext("nThread.so")
 *
 * Ham:
 *   thr_spawn(ip, port, count, delay_ms, data)
 *                           -> int worker_id
 *   thr_stop_all()          -> bool (ngung tat ca worker)
 *   thr_count()             -> int (so worker dang chay)
 *   thr_total_bytes()       -> int (tong bytes da gui)
 *   thr_reset()             -> bool (reset bo dem)
 * ======================================================================== */

#include "mnos_ext.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

/* ── Global state ── */
static volatile int g_running = 1;
static volatile int g_worker_count = 0;
static long long g_total_bytes = 0;
static int g_next_id = 1;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Thread argument ── */
typedef struct {
    char ip[64];
    int port;
    int send_count;          /* So lan send moi lan connect */
    int delay_ms;            /* Delay giua cac lan connect (ms) */
    char *data;              /* Du lieu gui */
    int data_len;
} WorkerArg;

/* ── Worker thread ── */
static void* worker_loop(void *arg) {
    WorkerArg *wa = (WorkerArg*)arg;

    /* Copy data ra khoi arg */
    char *buf = malloc(wa->data_len);
    if (!buf) { free(wa); return NULL; }
    memcpy(buf, wa->data, wa->data_len);
    int buf_len = wa->data_len;

    char ip[64];
    strncpy(ip, wa->ip, 63);
    ip[63] = 0;
    int port = wa->port;
    int snd_cnt = wa->send_count;
    int dms = wa->delay_ms;

    free(wa);

    /* Phan giai IP (1 lan) */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(ip);
        if (he) {
            memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        } else {
            free(buf);
            pthread_mutex_lock(&g_mutex);
            g_worker_count--;
            pthread_mutex_unlock(&g_mutex);
            return NULL;
        }
    }

    /* Vong lap ket noi */
    while (g_running) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            if (dms > 0) usleep(dms * 1000);
            continue;
        }

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        /* Non-blocking connect voi timeout 5s */
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
        if (rc < 0 && errno != EINPROGRESS) {
            close(fd);
            if (dms > 0) usleep(dms * 1000);
            continue;
        }
        if (rc < 0) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            rc = poll(&pfd, 1, 5000); /* 5 giay timeout */
            if (rc <= 0 || !(pfd.revents & POLLOUT)) {
                close(fd);
                if (dms > 0) usleep(dms * 1000);
                continue;
            }
            int err = 0;
            socklen_t elen = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
            if (err) {
                close(fd);
                if (dms > 0) usleep(dms * 1000);
                continue;
            }
        }
        fcntl(fd, F_SETFL, fl); /* Restore blocking mode */

        /* Gui du lieu */
        for (int i = 0; i < snd_cnt && g_running; i++) {
            int sent = (int)send(fd, buf, buf_len, 0);
            if (sent > 0) {
                pthread_mutex_lock(&g_mutex);
                g_total_bytes += sent;
                pthread_mutex_unlock(&g_mutex);
            }
        }

        close(fd);
        if (dms > 0) usleep(dms * 1000);
    }

    free(buf);

    pthread_mutex_lock(&g_mutex);
    g_worker_count--;
    pthread_mutex_unlock(&g_mutex);

    return NULL;
}

/* ── thr_spawn(ip, port, count, delay_ms, data) -> worker_id ── */
static Val thr_spawn(Val *a, int n) {
    if (n < 5 || a[0].type != V_STR || a[4].type != V_BYTES) {
        fprintf(stderr, "[nThread] thr_spawn(ip, port, count, delay_ms, data_bytes)\n");
        return val_int(-1);
    }

    WorkerArg *wa = (WorkerArg*)malloc(sizeof(WorkerArg));
    if (!wa) return val_int(-1);

    strncpy(wa->ip, a[0].sval, 63);
    wa->ip[63] = 0;
    wa->port = (int)a[1].ival;
    wa->send_count = (int)a[2].ival;
    wa->delay_ms = (int)a[3].ival;
    wa->data_len = a[4].slen;
    wa->data = malloc(wa->data_len);
    if (!wa->data) { free(wa); return val_int(-1); }
    memcpy(wa->data, a[4].sval, wa->data_len);

    if (wa->send_count < 1) wa->send_count = 1;
    if (wa->delay_ms < 0) wa->delay_ms = 0;

    pthread_t tid;
    if (pthread_create(&tid, NULL, worker_loop, wa) != 0) {
        free(wa->data);
        free(wa);
        fprintf(stderr, "[nThread] pthread_create failed\n");
        return val_int(-1);
    }
    pthread_detach(tid);

    pthread_mutex_lock(&g_mutex);
    g_worker_count++;
    int id = g_next_id++;
    pthread_mutex_unlock(&g_mutex);

    return val_int(id);
}

/* ── thr_stop_all() -> bool ── */
static Val thr_stop(Val *a, int n) {
    (void)a; (void)n;
    g_running = 0;
    return val_bool(1);
}

/* ── thr_count() -> int ── */
static Val thr_count(Val *a, int n) {
    (void)a; (void)n;
    pthread_mutex_lock(&g_mutex);
    int c = g_worker_count;
    pthread_mutex_unlock(&g_mutex);
    return val_int(c);
}

/* ── thr_total_bytes() -> int ── */
static Val thr_total(Val *a, int n) {
    (void)a; (void)n;
    pthread_mutex_lock(&g_mutex);
    long long t = g_total_bytes;
    pthread_mutex_unlock(&g_mutex);
    return val_int(t);
}

/* ── thr_reset() -> reset bo dem ── */
static Val thr_reset(Val *a, int n) {
    (void)a; (void)n;
    g_running = 1;
    pthread_mutex_lock(&g_mutex);
    g_total_bytes = 0;
    g_worker_count = 0;
    pthread_mutex_unlock(&g_mutex);
    return val_bool(1);
}

/* ── Dang ky ── */
MNOS_EXT_BEGIN(nthread)
    MNOS_EXT_FUNC("thr_spawn",       thr_spawn, "Spawn worker: thr_spawn(ip, port, count, delay_ms, data_bytes)")
    MNOS_EXT_FUNC("thr_stop_all",    thr_stop,  "Ngung tat ca worker")
    MNOS_EXT_FUNC("thr_count",       thr_count, "Dem so worker dang chay")
    MNOS_EXT_FUNC("thr_total_bytes", thr_total, "Tong bytes da gui")
    MNOS_EXT_FUNC("thr_reset",       thr_reset, "Reset bo dem + running flag")
MNOS_EXT_END
