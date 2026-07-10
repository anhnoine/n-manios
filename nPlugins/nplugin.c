/*
 * nplugin.c — Manios Plugin Manager
 *
 * 1 file, 2 modes:
 *   Plugin:  gcc -shared -fPIC -I/usr/local/share/manios/include -o nplugin.so nplugin.c
 *   CLI:     gcc -DNPLUGIN_CLI -o nplugin nplugin.c
 *
 * Plugin developers: copy this pattern! Just wrap your CLI in #ifdef NPLUGIN_CLI.
 */

#ifdef NPLUGIN_CLI
/* ========================================================================
 * CLI MODE — standalone executable, runs on terminal/cmd/termux
 * ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0 ||
        strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("nplugin - Manios Plugin Manager\n");
        printf("  nplugin install <name>       Cai tu GitHub\n");
        printf("  nplugin install -f <file>    Cai tu file local (.c/.so)\n");
        printf("  nplugin install -g <url>     Cai tu URL\n");
        printf("  nplugin list                 Liet ke plugin\n");
        printf("  nplugin uninstall <name>     Go plugin\n");
        return 0;
    }

    /* Kiem tra mno co san khong */
    if (system("command -v mno >/dev/null 2>&1") != 0) {
        fprintf(stderr, "nplugin: 'mno' khong tim thay tren PATH. Cai Manios truoc.\n");
        return 1;
    }

    char cmd[8192];
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/_nplugin_cli_%d.mno", getpid());

    if (strcmp(argv[1], "install") == 0) {
        if (argc >= 4 && strcmp(argv[2], "-f") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "echo \"nplugin_install_file('%s')\" > %s && mno %s && rm -f %s",
                     argv[3], tmpfile, tmpfile, tmpfile);
        } else if (argc >= 4 && strcmp(argv[2], "-g") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "echo \"nplugin_install_url('%s')\" > %s && mno %s && rm -f %s",
                     argv[3], tmpfile, tmpfile, tmpfile);
        } else if (argc >= 3) {
            snprintf(cmd, sizeof(cmd),
                     "echo \"nplugin_install('%s')\" > %s && mno %s && rm -f %s",
                     argv[2], tmpfile, tmpfile, tmpfile);
        } else {
            printf("Usage: nplugin install <name> | -f <file> | -g <url>\n");
            return 1;
        }
    } else if (strcmp(argv[1], "list") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "echo 'yell nplugin_list()' > %s && mno %s && rm -f %s",
                 tmpfile, tmpfile, tmpfile);
    } else if (strcmp(argv[1], "uninstall") == 0) {
        if (argc >= 3) {
            snprintf(cmd, sizeof(cmd),
                     "echo \"nplugin_uninstall('%s')\" > %s && mno %s && rm -f %s",
                     argv[2], tmpfile, tmpfile, tmpfile);
        } else {
            printf("Usage: nplugin uninstall <name>\n");
            return 1;
        }
    } else {
        snprintf(cmd, sizeof(cmd),
                 "echo \"yell 'nplugin: unknown cmd'\" > %s && mno %s && rm -f %s",
                 tmpfile, tmpfile, tmpfile);
    }

    int rc = system(cmd);
    /* cleanup in case mno crash, rm -f won't run inside system() */
    unlink(tmpfile);

    /* Tu dong chay lai mno sau khi install thanh cong */
    if (rc == 0 && (strcmp(argv[1], "install") == 0 || strcmp(argv[1], "uninstall") == 0)) {
        fprintf(stderr, "\n[nplugin] Dang khoi dong lai mno...\n");
        sleep(1);
        execlp("mno", "mno", NULL);
        /* Neu exec that bai, fallback */
        system("mno");
    }

    return rc == 0 ? 0 : 1;
}

#else
/* ========================================================================
 * PLUGIN MODE — .so loaded by Manios, provides builtin functions
 * ======================================================================== */
#include "mnos_ext.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define GITHUB_RAW "https://raw.githubusercontent.com/anhnoine/n-manios/refs/heads/main/nPlugins/plugins"
#define GITHUB_RAW_SRC "https://raw.githubusercontent.com/anhnoine/n-manios/refs/heads/main/nPlugins"

/* ── nplugin() — hien thi thong tin ── */
static Val np_info(Val *a, int n) {
    return val_str("Author: IdlerHa - Version: 1.0 - Manios");
}

/* ── nplugin_list() — liet ke plugin ── */
static Val np_list(Val *a, int n) {
    Val r = val_list();
    char buf[1024];
    const char *home = getenv("HOME") ? getenv("HOME") : "/tmp";
    snprintf(buf, sizeof(buf), "%s/.manios/nplugin", home);
    DIR *d = opendir(buf);
    if (!d) return r;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (r.llen >= r.lcap) { r.lcap *= 2; r.li = (Val*)realloc(r.li, sizeof(Val) * r.lcap); }
        r.li[r.llen++] = val_str(e->d_name);
    }
    closedir(d);
    return r;
}

/* ── nplugin_install(name) — cai tu GitHub (chi can ten) ── */
static Val np_install(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) {
        fprintf(stderr, "[nplugin] install <name>\n");
        return val_bool(0);
    }
    const char *name = a[0].sval;
    char url[2048];
    snprintf(url, sizeof(url), "%s/%s.c", GITHUB_RAW, name);

    const char *home = getenv("HOME") ? getenv("HOME") : "/tmp";
    char dest[1024];
    snprintf(dest, sizeof(dest), "%s/.manios/nplugin/%s.so", home, name);

    char tmpfile[1024];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/nplugin_dl_%s.XXXXXX.c", name);
    int fd = mkstemp(tmpfile);
    if (fd < 0) { fprintf(stderr, "[nplugin] Khong tao duoc file temp\n"); return val_bool(0); }
    close(fd);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "curl -sL '%s' -o '%s' 2>/dev/null", url, tmpfile);
    if (system(cmd) != 0) {
        fprintf(stderr, "[nplugin] Khong tim thay plugin '%s' tren GitHub\n", name);
        unlink(tmpfile);
        return val_bool(0);
    }

    struct stat st;
    if (stat(tmpfile, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "[nplugin] Download that bai\n");
        unlink(tmpfile);
        return val_bool(0);
    }

    snprintf(cmd, sizeof(cmd), "gcc -shared -fPIC -I/usr/local/share/manios/include -o '%s' '%s' -lpthread 2>/dev/null",
             dest, tmpfile);
    if (system(cmd) != 0) {
        fprintf(stderr, "[nplugin] Compile that bai: %s\n", name);
        unlink(tmpfile);
        return val_bool(0);
    }

    /* Try compile CLI mode */
    {
        char cli_dest[1024];
        snprintf(cli_dest, sizeof(cli_dest), "%s/.local/bin/%s", home, name);
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p %s/.local/bin 2>/dev/null && "
                 "gcc -DNPLUGIN_CLI -o '%s' '%s' -lpthread 2>/dev/null && "
                 "chmod +x '%s'",
                 home, cli_dest, tmpfile, cli_dest);
        if (system(cmd) == 0) {
            fprintf(stderr, "[nplugin] CLI: %s -> %s\n", name, cli_dest);
        }
    }

    unlink(tmpfile);
    fprintf(stderr, "[nplugin] OK: %s -> %s\n", name, dest);
    return val_bool(1);
}

/* ── nplugin_install_file(path) — cai tu file local (.c/.so) ── */
static Val np_install_file(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) {
        fprintf(stderr, "[nplugin] install_file <path>\n");
        return val_bool(0);
    }
    const char *path = a[0].sval;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *dot = strrchr(name, '.');

    if (!dot || (strcmp(dot, ".c") != 0 && strcmp(dot, ".so") != 0)) {
        fprintf(stderr, "[nplugin] File phai la .c hoac .so\n");
        return val_bool(0);
    }

    const char *home = getenv("HOME") ? getenv("HOME") : "/tmp";
    char dest[1024];
    snprintf(dest, sizeof(dest), "%s/.manios/nplugin/%.*s.so",
             home, (int)(dot - name), name);

    if (strcmp(dot, ".so") == 0) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", path, dest);
        system(cmd);
        fprintf(stderr, "[nplugin] Copied: %s -> %s\n", name, dest);
    } else {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "gcc -shared -fPIC -I/usr/local/share/manios/include -o '%s' '%s' -lpthread 2>/dev/null",
                 dest, path);
        if (system(cmd) != 0) {
            fprintf(stderr, "[nplugin] Compile that bai: %s\n", name);
            return val_bool(0);
        }
        fprintf(stderr, "[nplugin] Compiled: %s -> %s\n", name, dest);
    }

    /* Try compile CLI mode (only for .c) */
    if (strcmp(dot, ".c") == 0) {
        char cli_dest[1024];
        snprintf(cli_dest, sizeof(cli_dest), "%s/.local/bin/%.*s",
                 home, (int)(dot - name), name);
        char cli_cmd[4096];
        snprintf(cli_cmd, sizeof(cli_cmd),
                 "mkdir -p %s/.local/bin 2>/dev/null && "
                 "gcc -DNPLUGIN_CLI -o '%s' '%s' -lpthread 2>/dev/null && "
                 "chmod +x '%s'",
                 home, cli_dest, path, cli_dest);
        if (system(cli_cmd) == 0) {
            fprintf(stderr, "[nplugin] CLI: %.*s -> %s\n",
                    (int)(dot - name), name, cli_dest);
        }
    }

    return val_bool(1);
}

/* ── nplugin_install_url(url) — cai tu URL ── */
static Val np_install_url(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) {
        fprintf(stderr, "[nplugin] install_url <url>\n");
        return val_bool(0);
    }
    const char *url = a[0].sval;

    char *url_clean = strdup(url);
    char *q = strchr(url_clean, '?');
    if (q) *q = 0;

    const char *name = strrchr(url_clean, '/');
    name = name ? name + 1 : url_clean;

    char *name_copy = strdup(name);
    char *dot = strrchr(name_copy, '.');
    char base[256] = {0};
    if (dot) {
        int len = (int)(dot - name_copy);
        if (len > 255) len = 255;
        memcpy(base, name_copy, len);
    } else {
        strncpy(base, name_copy, 255);
        base[255] = 0;
    }

    const char *home = getenv("HOME") ? getenv("HOME") : "/tmp";
    char dest[1024];
    snprintf(dest, sizeof(dest), "%s/.manios/nplugin/%s.so", home, base);

    char tmpfile[1024];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/nplugin_dl_%s.XXXXXX.c", base);
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        fprintf(stderr, "[nplugin] Khong tao duoc file temp\n");
        free(url_clean); free(name_copy);
        return val_bool(0);
    }
    close(fd);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "curl -sL '%s' -o '%s' 2>/dev/null", url, tmpfile);
    if (system(cmd) != 0) {
        fprintf(stderr, "[nplugin] Download that bai\n");
        unlink(tmpfile);
        free(url_clean); free(name_copy);
        return val_bool(0);
    }

    snprintf(cmd, sizeof(cmd), "gcc -shared -fPIC -I/usr/local/share/manios/include -o '%s' '%s' -lpthread 2>/dev/null",
             dest, tmpfile);
    if (system(cmd) != 0) {
        fprintf(stderr, "[nplugin] Compile that bai\n");
        unlink(tmpfile);
        free(url_clean); free(name_copy);
        return val_bool(0);
    }

    /* Try compile CLI mode */
    {
        char cli_dest[1024];
        snprintf(cli_dest, sizeof(cli_dest), "%s/.local/bin/%s", home, base);
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p %s/.local/bin 2>/dev/null && "
                 "gcc -DNPLUGIN_CLI -o '%s' '%s' -lpthread 2>/dev/null && "
                 "chmod +x '%s'",
                 home, cli_dest, tmpfile, cli_dest);
        if (system(cmd) == 0) {
            fprintf(stderr, "[nplugin] CLI: %s -> %s\n", base, cli_dest);
        }
    }

    unlink(tmpfile);
    fprintf(stderr, "[nplugin] OK: %s -> %s\n", name, dest);
    free(url_clean); free(name_copy);
    return val_bool(1);
}

/* ── nplugin_uninstall(name) — go plugin ── */
static Val np_uninstall(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) {
        fprintf(stderr, "[nplugin] uninstall <name>\n");
        return val_bool(0);
    }
    const char *name = a[0].sval;
    const char *home = getenv("HOME") ? getenv("HOME") : "/tmp";
    char target[1024];

    const char *dot = strrchr(name, '.');
    if (dot && strcmp(dot, ".so") == 0) {
        snprintf(target, sizeof(target), "%s/.manios/nplugin/%s", home, name);
    } else {
        snprintf(target, sizeof(target), "%s/.manios/nplugin/%s.so", home, name);
    }

    if (unlink(target) == 0) {
        fprintf(stderr, "[nplugin] Da go .so: %s\n", name);

        /* Xoa luon CLI binary — dung ten plugin, khong hardcode */
        char base[256];
        const char *ndot = strrchr(name, '.');
        if (ndot && strcmp(ndot, ".so") == 0) {
            int len = (int)(ndot - name);
            if (len > 255) len = 255;
            memcpy(base, name, len);
            base[len] = 0;
        } else {
            strncpy(base, name, 255);
            base[255] = 0;
        }
        char rm_cli[4096];
        snprintf(rm_cli, sizeof(rm_cli),
                 "rm -f %s/.local/bin/%s 2>/dev/null", home, base);
        system(rm_cli);

        return val_bool(1);
    } else {
        fprintf(stderr, "[nplugin] Khong the go: %s\n", name);
        return val_bool(0);
    }
}

/* ── Auto-compile CLI on first load ── */
__attribute__((constructor))
static void nplugin_auto_cli(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    char cli[1024];
    snprintf(cli, sizeof(cli), "%s/.local/bin/nplugin", home);

    struct stat st;
    if (stat(cli, &st) == 0) return;  /* CLI da co san */

    /* Tim nplugin.c source */
    const char *src = NULL;
    if (stat("nplugin.c", &st) == 0) src = "nplugin.c";
    else if (stat("manios/nplugin.c", &st) == 0) src = "manios/nplugin.c";

    char cmd[4096];
    if (src) {
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p %s/.local/bin 2>/dev/null && "
                 "gcc -DNPLUGIN_CLI -o '%s' '%s' 2>/dev/null && "
                 "chmod +x '%s'",
                 home, cli, src, cli);
    } else {
        /* Download tu GitHub */
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p %s/.local/bin 2>/dev/null && "
                 "curl -sL '%s/nplugin.c' 2>/dev/null | "
                 "gcc -x c -DNPLUGIN_CLI -o '%s' - 2>/dev/null && "
                 "chmod +x '%s'",
                 home, GITHUB_RAW_SRC, cli, cli);
    }
    system(cmd);
}

/* ── Dang ky ── */
MNOS_EXT_BEGIN(nplugin_cmd)
    MNOS_EXT_FUNC("nplugin",              np_info,          "Hien thi thong tin nplugin")
    MNOS_EXT_FUNC("nplugin_list",         np_list,          "Liet ke plugin da cai")
    MNOS_EXT_FUNC("nplugin_install",      np_install,       "Cai plugin tu GitHub (ten)")
    MNOS_EXT_FUNC("nplugin_install_file", np_install_file,  "Cai plugin tu file local (.c/.so)")
    MNOS_EXT_FUNC("nplugin_install_url",  np_install_url,   "Cai plugin tu URL")
    MNOS_EXT_FUNC("nplugin_uninstall",    np_uninstall,     "Go plugin khoi he thong")
MNOS_EXT_END

#endif /* NPLUGIN_CLI */
