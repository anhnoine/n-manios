/* ========================================================================
 * nPack.c — Manios Binary Packet Builder Plugin
 *
 * Plugin mode:
 *   gcc -shared -fPIC -I/usr/local/share/manios/include \
 *       -o nPack.so nPack.c
 *
 * Load trong Manios:
 *   load_ext("nPack.so")
 *
 * Ham:
 *   p8(val)              -> bytes (1 byte)
 *   p16be(val)           -> bytes (2 byte big-endian)
 *   p32be(val)           -> bytes (4 byte big-endian)
 *   p_varint(val)        -> bytes (VarInt encoding)
 *   p_str(s)             -> bytes (VarInt-prefixed string)
 *   p_concat(a, b)       -> bytes (noi 2 bytes array)
 *   pkt_handshake(ip, port, proto_ver)
 *                        -> bytes (handshake packet)
 *   pkt_ping()           -> bytes (ping request)
 *   pkt_status()         -> bytes (status request packet)
 *   pkt_full_status(ip, port, proto_ver)
 *                        -> bytes (handshake + status)
 *   pkt_full_ping(ip, port, proto_ver)
 *                        -> bytes (handshake + ping)
 * ======================================================================== */

#include "mnos_ext.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Tao bytes tu raw data ── */
static Val make_bytes(const char *data, int len) {
    Val v;
    memset(&v, 0, sizeof(v));
    v.type = V_BYTES;
    v.sval = malloc(len);
    if (v.sval) {
        memcpy(v.sval, data, len);
        v.slen = len;
    } else {
        v.slen = 0;
    }
    return v;
}

/* ── p8(val) -> 1 byte ── */
static Val p8_fn(Val *a, int n) {
    if (n < 1 || (a[0].type != V_INT && a[0].type != V_FLOAT)) {
        return make_bytes("", 0);
    }
    unsigned char c = (unsigned char)(a[0].ival & 0xFF);
    return make_bytes((const char*)&c, 1);
}

/* ── p16be(val) -> 2 byte big-endian ── */
static Val p16be_fn(Val *a, int n) {
    if (n < 1 || (a[0].type != V_INT && a[0].type != V_FLOAT)) {
        return make_bytes("", 0);
    }
    unsigned short v = (unsigned short)(a[0].ival & 0xFFFF);
    unsigned char buf[2];
    buf[0] = (v >> 8) & 0xFF;
    buf[1] = v & 0xFF;
    return make_bytes((const char*)buf, 2);
}

/* ── p32be(val) -> 4 byte big-endian ── */
static Val p32be_fn(Val *a, int n) {
    if (n < 1 || (a[0].type != V_INT && a[0].type != V_FLOAT)) {
        return make_bytes("", 0);
    }
    unsigned int v = (unsigned int)(a[0].ival & 0xFFFFFFFF);
    unsigned char buf[4];
    buf[0] = (v >> 24) & 0xFF;
    buf[1] = (v >> 16) & 0xFF;
    buf[2] = (v >> 8) & 0xFF;
    buf[3] = v & 0xFF;
    return make_bytes((const char*)buf, 4);
}

/* ── p_varint(val) -> VarInt encoding ── */
static Val p_varint_fn(Val *a, int n) {
    if (n < 1 || (a[0].type != V_INT && a[0].type != V_FLOAT)) {
        return make_bytes("", 0);
    }
    unsigned int val = (unsigned int)(a[0].ival);
    unsigned char buf[10];
    int i = 0;
    while (val > 0x7F) {
        buf[i++] = (val & 0x7F) | 0x80;
        val >>= 7;
    }
    buf[i++] = val & 0x7F;
    return make_bytes((const char*)buf, i);
}

/* ── p_str(s) -> VarInt-prefixed UTF-8 string ── */
static Val p_str_fn(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) {
        return make_bytes("", 0);
    }
    const char *s = a[0].sval;
    int slen = a[0].slen;

    unsigned int val = (unsigned int)slen;
    unsigned char varint_buf[10];
    int vi = 0;
    while (val > 0x7F) {
        varint_buf[vi++] = (val & 0x7F) | 0x80;
        val >>= 7;
    }
    varint_buf[vi++] = val & 0x7F;

    int total = vi + slen;
    unsigned char *result = malloc(total);
    if (!result) return make_bytes("", 0);
    memcpy(result, varint_buf, vi);
    memcpy(result + vi, s, slen);
    Val r = make_bytes((const char*)result, total);
    free(result);
    return r;
}

/* ── p_concat(a, b) -> noi 2 bytes array ── */
static Val p_concat_fn(Val *a, int n) {
    if (n < 2) return make_bytes("", 0);
    const char *d1 = NULL;
    int l1 = 0;
    const char *d2 = NULL;
    int l2 = 0;
    if (a[0].type == V_BYTES || a[0].type == V_STR) {
        d1 = a[0].sval; l1 = a[0].slen;
    }
    if (a[1].type == V_BYTES || a[1].type == V_STR) {
        d2 = a[1].sval; l2 = a[1].slen;
    }
    if (!d1 || !d2) return make_bytes("", 0);

    int total = l1 + l2;
    char *buf = malloc(total);
    if (!buf) return make_bytes("", 0);
    memcpy(buf, d1, l1);
    memcpy(buf + l1, d2, l2);
    Val r = make_bytes(buf, total);
    free(buf);
    return r;
}

/* ── pkt_handshake(ip, port, proto_ver) -> handshake packet ── */
/*
 * Packet structure:
 *   VarInt(length) + [VarInt(0) + VarInt(proto_ver)
 *   + VarInt(len(ip)) + ip + u16be(port) + VarInt(next_state)]
 *
 *   next_state = 1 (status)
 */
static Val pkt_handshake_fn(Val *a, int n) {
    if (n < 2 || a[0].type != V_STR) {
        fprintf(stderr, "[nPack] pkt_handshake(ip, port, proto_ver)\n");
        return make_bytes("", 0);
    }
    const char *ip = a[0].sval;
    int ip_len = a[0].slen;
    int port = (int)a[1].ival;
    int proto_ver = (n >= 3 && a[2].type == V_INT) ? (int)a[2].ival : 754;

    unsigned char payload[4096];
    int pos = 0;

    /* packet ID = 0x00 */
    payload[pos++] = 0x00;

    /* Protocol version -> varint */
    {
        unsigned int val = (unsigned int)proto_ver;
        while (val > 0x7F) {
            payload[pos++] = (val & 0x7F) | 0x80;
            val >>= 7;
        }
        payload[pos++] = val & 0x7F;
    }

    /* Server address: varint length + string */
    {
        unsigned int val = (unsigned int)ip_len;
        while (val > 0x7F) {
            payload[pos++] = (val & 0x7F) | 0x80;
            val >>= 7;
        }
        payload[pos++] = val & 0x7F;
        memcpy(payload + pos, ip, ip_len);
        pos += ip_len;
    }

    /* Port: 2 byte big-endian */
    payload[pos++] = (port >> 8) & 0xFF;
    payload[pos++] = port & 0xFF;

    /* next_state = 1 (status) */
    payload[pos++] = 0x01;

    /* Packet: varint(length) + payload */
    unsigned int pkt_len = pos;
    unsigned char header[10];
    int hlen = 0;
    {
        unsigned int val = pkt_len;
        while (val > 0x7F) {
            header[hlen++] = (val & 0x7F) | 0x80;
            val >>= 7;
        }
        header[hlen++] = val & 0x7F;
    }

    int total = hlen + pkt_len;
    unsigned char *result = malloc(total);
    if (!result) return make_bytes("", 0);
    memcpy(result, header, hlen);
    memcpy(result + hlen, payload, pkt_len);
    Val r = make_bytes((const char*)result, total);
    free(result);
    return r;
}

/* ── pkt_ping() -> ping request ── */
static Val pkt_ping_fn(Val *a, int n) {
    (void)a; (void)n;
    unsigned char buf[9];
    buf[0] = 0x01;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long t = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    for (int i = 0; i < 8; i++) {
        buf[1 + i] = (t >> (56 - i * 8)) & 0xFF;
    }
    unsigned char pkt[11];
    pkt[0] = 0x09;
    memcpy(pkt + 1, buf, 9);
    return make_bytes((const char*)pkt, 10);
}

/* ── pkt_status() -> status request packet ── */
static Val pkt_status_fn(Val *a, int n) {
    (void)a; (void)n;
    unsigned char buf[2] = { 0x01, 0x00 };
    return make_bytes((const char*)buf, 2);
}

/* ── pkt_full_status(ip, port, proto_ver) -> handshake + status ── */
static Val pkt_full_status_fn(Val *a, int n) {
    if (n < 2 || a[0].type != V_STR) {
        return make_bytes("", 0);
    }
    Val handshake = pkt_handshake_fn(a, n);
    Val status = pkt_status_fn(a, n);

    if (handshake.type != V_BYTES || status.type != V_BYTES ||
        !handshake.sval || !status.sval) {
        val_free(&handshake);
        val_free(&status);
        return make_bytes("", 0);
    }
    int total = handshake.slen + status.slen;
    char *buf = malloc(total);
    if (!buf) {
        val_free(&handshake);
        val_free(&status);
        return make_bytes("", 0);
    }
    memcpy(buf, handshake.sval, handshake.slen);
    memcpy(buf + handshake.slen, status.sval, status.slen);
    Val r = make_bytes(buf, total);
    free(buf);
    val_free(&handshake);
    val_free(&status);
    return r;
}

/* ── pkt_full_ping(ip, port, proto_ver) -> handshake + ping ── */
static Val pkt_full_ping_fn(Val *a, int n) {
    if (n < 2 || a[0].type != V_STR) {
        return make_bytes("", 0);
    }
    Val handshake = pkt_handshake_fn(a, n);
    Val ping = pkt_ping_fn(a, n);
    if (handshake.type != V_BYTES || ping.type != V_BYTES ||
        !handshake.sval || !ping.sval) {
        val_free(&handshake);
        val_free(&ping);
        return make_bytes("", 0);
    }
    int total = handshake.slen + ping.slen;
    char *buf = malloc(total);
    if (!buf) {
        val_free(&handshake);
        val_free(&ping);
        return make_bytes("", 0);
    }
    memcpy(buf, handshake.sval, handshake.slen);
    memcpy(buf + handshake.slen, ping.sval, ping.slen);
    Val r = make_bytes(buf, total);
    free(buf);
    val_free(&handshake);
    val_free(&ping);
    return r;
}

/* ── Dang ky ── */
MNOS_EXT_BEGIN(npack)
    MNOS_EXT_FUNC("p8",               p8_fn,              "1 byte: p8(val)")
    MNOS_EXT_FUNC("p16be",            p16be_fn,           "2 byte big-endian: p16be(val)")
    MNOS_EXT_FUNC("p32be",            p32be_fn,           "4 byte big-endian: p32be(val)")
    MNOS_EXT_FUNC("p_varint",         p_varint_fn,        "VarInt encoding: p_varint(val)")
    MNOS_EXT_FUNC("p_str",            p_str_fn,           "VarInt-prefixed string: p_str(s)")
    MNOS_EXT_FUNC("p_concat",         p_concat_fn,        "Noi 2 bytes: p_concat(a, b)")
    MNOS_EXT_FUNC("pkt_handshake",    pkt_handshake_fn,   "Handshake packet: pkt_handshake(ip, port, proto_ver)")
    MNOS_EXT_FUNC("pkt_ping",         pkt_ping_fn,        "Ping request: pkt_ping()")
    MNOS_EXT_FUNC("pkt_status",       pkt_status_fn,      "Status request: pkt_status()")
    MNOS_EXT_FUNC("pkt_full_status",  pkt_full_status_fn, "Handshake + status: pkt_full_status(ip, port, proto_ver)")
    MNOS_EXT_FUNC("pkt_full_ping",    pkt_full_ping_fn,   "Handshake + ping: pkt_full_ping(ip, port, proto_ver)")
MNOS_EXT_END
