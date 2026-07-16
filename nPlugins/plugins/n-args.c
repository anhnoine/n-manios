/*
 * nargs.c — nArgs: CLI Arguments Plugin for Manios
 * ============================================================================
 * Author:  IdlerHa
 * Version: 1.0.0
 *
 * 1 file, 2 modes (theo chuan nplugin.c):
 *   Plugin:  gcc -shared -fPIC -I/usr/local/share/manios/include -o nargs.so nargs.c
 *   CLI:     gcc -DNARGS_CLI -o nargs nargs.c
 *
 * Plugin doc args tu /proc/self/cmdline → scripts dung nargs_get(), nargs_count()...
 * KHONG can sua Manios core.
 * ============================================================================
 */

#ifdef NARGS_CLI
/* ========================================================================
 * CLI MODE — standalone executable (theo chuan nplugin.c)
 * Dung temp .mno file → goi mno → cleanup
 * ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0 ||
        strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("nargs - Manios CLI Args Wrapper\n");
        printf("  nargs <script.mno> [args...]  Chay script voi CLI args\n");
        printf("  nargs count <script.mno> ...  In ra so luong args\n");
        printf("  nargs list <script.mno> ...   In ra tat ca args\n");
        printf("\n");
        printf("  Script co the dung: nargs_count(), nargs_get(), nargs_all()\n");
        return 0;
    }

    /* Kiem tra mno co san */
    if (system("command -v mno >/dev/null 2>&1") != 0) {
        fprintf(stderr, "nargs: 'mno' khong tim thay. Cai Manios truoc.\n");
        return 1;
    }

    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/_nargs_cli_%d.mno", getpid());
    char cmd[16384];

    if (strcmp(argv[1], "count") == 0) {
        /* nargs count script.mno arg1 arg2 → in ra so luong args */
        if (argc < 3) {
            printf("Usage: nargs count <script.mno> [args...]\n");
            return 1;
        }
        /* Build command with all args quoted */
        int pos = snprintf(cmd, sizeof(cmd), "mno");
        for (int i = 2; i < argc; i++) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", argv[i]);
        }
        /* Create temp .mno that calls nargs_count */
        FILE *f = fopen(tmpfile, "w");
        if (!f) { perror("nargs"); return 1; }
        fprintf(f, "load_ext(\"%s/.manios/nplugin/nargs.so\")\n", getenv("HOME") ? getenv("HOME") : "/root");
        fprintf(f, "yell str(nargs_count())\n");
        fclose(f);
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", tmpfile);
        int rc = system(cmd);
        unlink(tmpfile);
        return rc;
    }

    if (strcmp(argv[1], "list") == 0) {
        /* nargs list script.mno arg1 arg2 → in ra list args */
        if (argc < 3) {
            printf("Usage: nargs list <script.mno> [args...]\n");
            return 1;
        }
        FILE *f = fopen(tmpfile, "w");
        if (!f) { perror("nargs"); return 1; }
        fprintf(f, "load_ext(\"%s/.manios/nplugin/nargs.so\")\n", getenv("HOME") ? getenv("HOME") : "/root");
        fprintf(f, "hold a = nargs_all()\n");
        fprintf(f, "hold i = 0\n");
        fprintf(f, "spin while i < len(a)\n");
        fprintf(f, "    yell str(i) + \": \" + a[i]\n");
        fprintf(f, "    set i = i + 1\n");
        fprintf(f, "end\n");
        fclose(f);

        int pos = snprintf(cmd, sizeof(cmd), "mno");
        for (int i = 2; i < argc; i++) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", argv[i]);
        }
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", tmpfile);
        int rc = system(cmd);
        unlink(tmpfile);
        return rc;
    }

    /* Default: nargs script.mno arg1 arg2 → just run mno with all args */
    int pos = snprintf(cmd, sizeof(cmd), "mno");
    for (int i = 1; i < argc; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", argv[i]);
    }
    return system(cmd);
}

#else
/* ========================================================================
 * PLUGIN MODE — .so loaded by Manios, provides nargs_* builtins
 * ======================================================================== */
#include "mnos_ext.h"

/* Fallback Val struct (khi compile standalone, ko co Manios headers) */
#ifndef MNOS_EXT_H
typedef enum {
    V_NONE, V_INT, V_FLOAT, V_STR, V_BOOL, V_LIST,
    V_DICT, V_CLASS, V_OBJ, V_TUPLE, V_BYTES
} VType;

typedef struct Val {
    VType type;
    long long ival;
    double fval;
    int bval;
    char *sval;
    int slen;
    struct Val *li;
    int llen, lcap;
    char **dkeys;
    struct Val *dvals;
    int dlen, dcap;
    struct Val *tu;
    int tlen;
    void *cls;
    void *obj;
} Val;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* ── Static argv storage (doc 1 lan tu /proc/self/cmdline) ── */
static char **nargs_argv = NULL;
static int    nargs_argc = 0;
static int    nargs_loaded = 0;

/* ── Helper: strdup ── */
static char *nargs_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *c = (char *)malloc(len + 1);
    if (c) { memcpy(c, s, len); c[len] = '\0'; }
    return c;
}

/* ── Val constructors ── */
static Val nargs_val_int(long long i) {
    Val v; memset(&v, 0, sizeof(v)); v.type = V_INT; v.ival = i; return v;
}
static Val nargs_val_bool(int b) {
    Val v; memset(&v, 0, sizeof(v)); v.type = V_BOOL; v.bval = b; return v;
}
static Val nargs_val_str(const char *s) {
    Val v; memset(&v, 0, sizeof(v)); v.type = V_STR;
    v.sval = nargs_strdup(s ? s : "");
    v.slen = (int)strlen(v.sval);
    return v;
}
static Val nargs_val_list(void) {
    Val v; memset(&v, 0, sizeof(v)); v.type = V_LIST;
    v.li = (Val *)calloc(16, sizeof(Val));
    v.lcap = v.li ? 16 : 0;
    return v;
}
static void nargs_list_append(Val *list, Val item) {
    if (!list || !list->li || list->type != V_LIST) return;
    if (list->llen >= list->lcap) {
        list->lcap *= 2;
        list->li = (Val *)realloc(list->li, sizeof(Val) * list->lcap);
    }
    if (list->li && list->llen < list->lcap) {
        list->li[list->llen++] = item;
    }
}

/* ── Core: doc /proc/self/cmdline (null-separated args)
   IMPORTANT: /proc files report size=0 via ftell(), must read sequentially! ── */
static void nargs_load_cmdline(void) {
    if (nargs_loaded) return;
    nargs_loaded = 1;

    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f) {
        fprintf(stderr, "[nargs] WARN: cannot open /proc/self/cmdline\n");
        return;
    }

    /* Read sequentially — /proc files don't support fseek/ftell */
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) { fclose(f); return; }

    size_t nr = 0;
    while (1) {
        if (nr + 4096 > cap) {
            cap *= 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); fclose(f); return; }
            buf = tmp;
        }
        size_t n = fread(buf + nr, 1, cap - nr - 1, f);
        if (n == 0) break;
        nr += n;
    }
    fclose(f);

    if (nr == 0) { free(buf); return; }
    buf[nr] = '\0';

    /* Dem args (null-separated) */
    int cnt = 0;
    for (size_t i = 0; i < nr; i++)
        if (buf[i] == '\0') cnt++;
    if (buf[nr - 1] != '\0') cnt++;

    /* Cap phat argv */
    nargs_argv = (char **)calloc(cnt + 1, sizeof(char *));
    if (!nargs_argv) { free(buf); return; }

    /* Tach theo \0 */
    int idx = 0;
    char *start = buf;
    for (size_t i = 0; i <= nr; i++) {
        if (buf[i] == '\0' || i == nr) {
            if (start < buf + i) {
                nargs_argv[idx++] = nargs_strdup(start);
            }
            start = buf + i + 1;
            if (idx >= cnt) break;
        }
    }
    nargs_argc = idx;

    fprintf(stderr, "[nargs] Loaded %d args from /proc/self/cmdline\n", nargs_argc);
    for (int i = 0; i < nargs_argc && i < 5; i++)
        fprintf(stderr, "[nargs]   argv[%d] = '%s'\n", i, nargs_argv[i] ? nargs_argv[i] : "(null)");

    free(buf);
}

/* ── Builtin: nargs_count() → int ── */
static Val nargs_count(Val *a, int n) {
    (void)a; (void)n;
    nargs_load_cmdline();
    return nargs_val_int(nargs_argc);
}

/* ── Builtin: nargs_get(index) → string ── */
static Val nargs_get(Val *a, int n) {
    nargs_load_cmdline();
    if (n < 1 || a[0].type != V_INT) return nargs_val_str("");
    int idx = (int)a[0].ival;
    if (idx < 0 || idx >= nargs_argc) return nargs_val_str("");
    return nargs_val_str(nargs_argv[idx]);
}

/* ── Builtin: nargs_all() → list ── */
static Val nargs_all(Val *a, int n) {
    (void)a; (void)n;
    nargs_load_cmdline();
    Val lst = nargs_val_list();
    for (int i = 0; i < nargs_argc; i++)
        nargs_list_append(&lst, nargs_val_str(nargs_argv[i]));
    return lst;
}

/* ── Builtin: nargs_script() → string (argv[1]) ── */
static Val nargs_script(Val *a, int n) {
    (void)a; (void)n;
    nargs_load_cmdline();
    return nargs_val_str(nargs_argc >= 2 ? nargs_argv[1] : "");
}

/* ── Builtin: nargs_script_args() → list (argv[2+]) ── */
static Val nargs_script_args(Val *a, int n) {
    (void)a; (void)n;
    nargs_load_cmdline();
    Val lst = nargs_val_list();
    for (int i = 2; i < nargs_argc; i++)
        nargs_list_append(&lst, nargs_val_str(nargs_argv[i]));
    return lst;
}

/* ── Builtin: nargs_has_flag(flag) → bool ── */
static Val nargs_has_flag(Val *a, int n) {
    nargs_load_cmdline();
    if (n < 1 || a[0].type != V_STR) return nargs_val_bool(0);
    for (int i = 2; i < nargs_argc; i++)
        if (strcmp(nargs_argv[i], a[0].sval) == 0)
            return nargs_val_bool(1);
    return nargs_val_bool(0);
}

/* ── Builtin: nargs_version() → string ── */
static Val nargs_version_fn(Val *a, int n) {
    (void)a; (void)n;
    return nargs_val_str("nArgs v1.0.0 - CLI Arguments Plugin for Manios");
}

/* ========================================================================
 * Plugin registration — MNOS_EXT_BEGIN/END (theo chuan nplugin.c + nsocks.c)
 * ======================================================================== */
MNOS_EXT_BEGIN(nargs)
    MNOS_EXT_FUNC("nargs_version",     nargs_version_fn,    "nArgs v1.0.0")
    MNOS_EXT_FUNC("nargs_count",       nargs_count,         "count() -> int")
    MNOS_EXT_FUNC("nargs_get",         nargs_get,           "get(idx) -> str")
    MNOS_EXT_FUNC("nargs_all",         nargs_all,           "all() -> list")
    MNOS_EXT_FUNC("nargs_script",      nargs_script,        "script() -> str")
    MNOS_EXT_FUNC("nargs_script_args", nargs_script_args,   "script_args() -> list")
    MNOS_EXT_FUNC("nargs_has_flag",    nargs_has_flag,      "has_flag(f) -> bool")
MNOS_EXT_END

/* ── Export init (goi boi Manios qua dlsym("mnos_ext_init"))
   CHI khi compile standalone (ko co mnos_ext.h).
   Khi co mnos_ext.h, MNOS_EXT_BEGIN da dinh nghia mnos_ext_init inline. ── */
#ifndef MNOS_EXT_H
MNOS_EXT_INIT_BODY()
#endif

#endif /* !NARGS_CLI */
