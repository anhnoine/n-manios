#!/usr/bin/env bash
set -e
set -o pipefail
clear
apt update
sudo apt install make
clear

[ -t 1 ] && G='\033[0;32m' Y='\033[1;33m' R='\033[0;31m' C='\033[0;36m' B='\033[1m' NC='\033[0m' \
         || G='' Y='' R='' C='' B='' NC=''
info()  { printf "${G}  +${NC} %s\n" "$1"; }
warn()  { printf "${Y}  !${NC} %s\n" "$1"; }
err()   { printf "${R}  x${NC} %s\n" "$1"; }

echo ""
echo "  ======================================="
echo "    nPlugin installer"
echo "    repo: anhnoine/n-manios$"
echo "  ======================================="
echo ""

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MANIOS_REPO="${MANIOS_REPO:-https://github.com/anhnoine/n-manios.git}"
MANIOS_SRC="${MANIOS_SRC:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --repo)   MANIOS_REPO="$2"; shift 2 ;;
        --source) MANIOS_SRC="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--repo URL] [--source DIR]"
            echo "  --repo URL    Git repo (tu clone + patch + build)"
            echo "  --source DIR  Thu muc source co san"
            exit 0 ;;
        *) err "Unknown: $1"; exit 1 ;;
    esac
done

# ── 1. Tim/clone source ──
find_source() {
    [ -n "$MANIOS_SRC" ] && [ -f "$MANIOS_SRC/Makefile" ] && [ -f "$MANIOS_SRC/src/mnos.h" ] && { echo "$MANIOS_SRC"; return 0; }
    for d in "$SCRIPT_DIR" "$HOME/manios" "$HOME/n-manios" "/usr/local/share/Manios"; do
        [ -f "$d/Makefile" ] && [ -f "$d/src/mnos.h" ] && { echo "$d"; return 0; }
    done
    return 1
}

SRCDIR=""
if s="$(find_source)"; then
    SRCDIR="$s"
    info "Source: $SRCDIR"
else
    [ -z "$MANIOS_REPO" ] && { err "KHONG tim thay source. Dung --repo URL hoac --source DIR"; exit 1; }
    command -v git >/dev/null 2>&1 || { err "Can 'git'. Cai dat git truoc."; exit 1; }
    CLONEDIR="$(mktemp -d /tmp/manios_nplugin.XXXXXX)"
    trap "rm -rf '$CLONEDIR'" EXIT
    info "Cloning: $MANIOS_REPO"
    git clone --depth=1 "$MANIOS_REPO" "$CLONEDIR" 2>&1 | while IFS= read -r l; do printf "      %s\n" "$l"; done
    SRCDIR="$CLONEDIR"
    info "Da clone -> $CLONEDIR"
fi

[ -f "$SRCDIR/src/mnos.h" ]          || { err "Thieu src/mnos.h"; exit 1; }
[ -f "$SRCDIR/src/mnos_runtime.c" ]  || { err "Thieu src/mnos_runtime.c"; exit 1; }
[ -f "$SRCDIR/src/mnos_main.c" ]     || { err "Thieu src/mnos_main.c"; exit 1; }
[ -f "$SRCDIR/Makefile" ]            || { err "Thieu Makefile"; exit 1; }

HDR="$SRCDIR/src/mnos.h"
RT="$SRCDIR/src/mnos_runtime.c"
MAIN="$SRCDIR/src/mnos_main.c"
MKF="$SRCDIR/Makefile"

# ── 2. Kiem tra manios ──
MANIOS_BIN=""
for p in "$(command -v manios 2>/dev/null)" "$(command -v mno 2>/dev/null)" \
         "/usr/local/bin/manios" "$HOME/.local/bin/manios"; do
    [ -x "$p" ] && { MANIOS_BIN="$p"; break; }
done
[ -n "$MANIOS_BIN" ] && info "Binary: $MANIOS_BIN" || info "Manios chua cai. Se cai sau build."

# ── 3. Patch mnos.h ──
info "Patching: mnos.h"

if ! grep -q '#include <dlfcn.h>' "$HDR"; then
    sed -i '/^#include <limits.h>/a #include <dlfcn.h>' "$HDR"
    info "  + #include <dlfcn.h>"
else info "  (dlfcn.h da co)"; fi

if ! grep -q 'MnosExtFunc' "$HDR"; then
    sed -i '/^#include <dlfcn.h>/a \
\
/* nplugins */\
typedef struct Val (*MnosExtFn)(struct Val *args, int nargs);\
typedef struct { const char *name; const char *doc; MnosExtFn func; } MnosExtFunc;\
void nplugins(void);' "$HDR"
    info "  + MnosExtFn + MnosExtFunc + nplugins()"
else info "  (MnosExtFunc da co)"; fi

# ── 4. Patch mnos_runtime.c ──
info "Patching: mnos_runtime.c"

if grep -q 'bi_nplugin_dir' "$RT"; then
    info "  (nplugins da co)"
else
    # Add includes at top
    if ! grep -q '#include <dlfcn.h>' "$RT"; then
        sed -i '1i#include <dlfcn.h>' "$RT"
        info "  + #include <dlfcn.h>"
    fi

    # Insert nplugin code BEFORE register_builtins function
    TMP="$(mktemp /tmp/nplugin_code.XXXXXX)"
    cat > "$TMP" << 'NPLUGINS_CODE'

/* ======== nplugins ======== */
static void mkdir_p_nplugin(void) {
    char b[1024];
    snprintf(b, sizeof(b), "%s/.manios/nplugin",
             getenv("HOME") ? getenv("HOME") : "/tmp");
    char *p = b;
    while (*p) { if (*p == '/' && p > b) { *p = 0; mkdir(b, 0755); *p = '/'; } p++; }
    mkdir(b, 0755);
}

static void nplugin_load_so(const char *path) {
    void *h = dlopen(path, RTLD_NOW);
    if (!h) { fprintf(stderr, "[nplugin] dlopen(%s): %s\n", path, dlerror()); return; }
    int nfuncs;
    MnosExtFunc* (*init)(int*) = (MnosExtFunc*(*)(int*))dlsym(h, "mnos_ext_init");
    if (!init) { fprintf(stderr, "[nplugin] no mnos_ext_init in %s\n", path); dlclose(h); return; }
    MnosExtFunc *f = init(&nfuncs);
    for (int i = 0; i < nfuncs; i++) {
        if (nbuiltins < MAX_BUILTIN) {
            builtins[nbuiltins].name = strdup(f[i].name);
            builtins[nbuiltins].func = f[i].func;
            nbuiltins++;
        }
    }

}

static Val bi_load_ext(Val *a, int n) {
    if (n < 1 || a[0].type != V_STR) return val_none();
    nplugin_load_so(a[0].sval);
    return val_none();
}

static Val bi_list_ext(Val *a, int n) {
    (void)a; (void)n;
    Val r = val_list();
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s/.manios/nplugin",
             getenv("HOME") ? getenv("HOME") : "/tmp");
    DIR *d = opendir(buf);
    if (!d) return r;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (r.llen >= r.lcap) { r.lcap *= 2; r.li = realloc(r.li, sizeof(Val) * r.lcap); }
        r.li[r.llen++] = val_str(e->d_name);
    }
    closedir(d);
    return r;
}

static Val bi_nplugin_dir(Val *a, int n) {
    (void)a; (void)n;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s/.manios/nplugin",
             getenv("HOME") ? getenv("HOME") : "/tmp");
    return val_str(buf);
}

void nplugins(void) {
    mkdir_p_nplugin();
    reg_builtin("load_ext", bi_load_ext);
    reg_builtin("list_ext", bi_list_ext);
    reg_builtin("nplugin_dir", bi_nplugin_dir);
    const char *home = getenv("HOME") ? getenv("HOME") : "";
    if (!home[0]) return;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s/.manios/nplugin", home);
    DIR *d = opendir(buf);
    if (!d) return;
    char path[2048];
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        const char *n = e->d_name;
        int nl = (int)strlen(n);
        if (nl > 3 && n[nl-3]=='.' && n[nl-2]=='s' && n[nl-1]=='o') {
            snprintf(path, sizeof(path), "%s/%s", buf, n);
            nplugin_load_so(path);
        }
    }
    closedir(d);
}
NPLUGINS_CODE

    # Insert BEFORE "void register_builtins" (sed 'r' inserts AFTER match,
    # so we find the line number and insert after the line BEFORE it)
    lineno=$(grep -n '^void register_builtins' "$RT" | head -1 | cut -d: -f1)
    lineno=$((lineno - 1))
    sed -i "${lineno}r $TMP" "$RT"
    info "  + nplugins() + load_ext + list_ext + nplugin_dir + mkdir_p_nplugin"
    rm -f "$TMP"
fi

# ── 5. Patch mnos_main.c — call nplugins() before mnos_run_file() ──
info "Patching: mnos_main.c"

if grep -q 'nplugins()' "$MAIN"; then
    info "  (nplugins() da duoc goi)"
else
    # Insert nplugins() before the last "return mnos_run_file"
    sed -i '/return mnos_run_file/i\    nplugins();' "$MAIN"
    info "  + nplugins() call in main()"
fi

# ── 6. Patch Makefile ──
info "Patching: Makefile"

if ! grep -q 'LDFLAGS.*-rdynamic' "$MKF"; then
    if grep -q '^LDFLAGS ?= -lm$' "$MKF"; then
        sed -i 's/^LDFLAGS ?= -lm$/LDFLAGS ?= -lm -ldl -rdynamic/' "$MKF"
    elif grep -q '^LDFLAGS ?= -lm -ldl$' "$MKF"; then
        sed -i 's/^LDFLAGS ?= -lm -ldl$/LDFLAGS ?= -lm -ldl -rdynamic/' "$MKF"
    else
        sed -i '/^LDFLAGS/s/$/ -ldl -rdynamic/' "$MKF"
    fi
    info "  + -ldl -rdynamic"
else info "  (LDFLAGS da co -rdynamic)"; fi

echo ""
info "Source da patch xong!"

# ── 7. Build ──
clear
echo ""
info "Building Manios..."
cd "$SRCDIR"
make clean 2>/dev/null || true

if make 2>&1 | while IFS= read -r l; do printf "      %s\n" "$l"; done; then
    info "BUILD THANH CONG!"
else
    err "BUILD THAT BAI!"
    echo "  Chay thu cong: cd $SRCDIR && make"
    exit 1
fi

# ── 8. Install (de len binary hien tai) ──
echo ""

# Uu tien: cai de vao vi tri binary hien co
if [ -n "$MANIOS_BIN" ]; then
    INSTALL_BIN="$MANIOS_BIN"
    cp "$SRCDIR/manios" "$INSTALL_BIN" 2>/dev/null || {
        err "Khong the ghi vao $INSTALL_BIN. Dung sudo hoac chmod."
        exit 1
    }
elif [ -d "/usr/local/bin" ] && [ -w "/usr/local/bin" ] 2>/dev/null; then
    INSTALL_BIN="/usr/local/bin/manios"
    cp "$SRCDIR/manios" "$INSTALL_BIN"
elif [ -d "$HOME/.local/bin" ]; then
    mkdir -p "$HOME/.local/bin"
    INSTALL_BIN="$HOME/.local/bin/manios"
    cp "$SRCDIR/manios" "$INSTALL_BIN"
else
    INSTALL_BIN="$HOME/manios"
    cp "$SRCDIR/manios" "$INSTALL_BIN"
fi
chmod +x "$INSTALL_BIN" 2>/dev/null || true
info "Installed: $INSTALL_BIN"

# tao symlink mno -> manios
MNODIR="$(dirname "$INSTALL_BIN")"
if [ -d "$MNODIR" ] && [ -w "$MNODIR" ]; then
    ln -sf "$INSTALL_BIN" "$MNODIR/mno" 2>/dev/null || true
    info "Symlink: $MNODIR/mno -> manios"
fi

# ── 9. Setup include + nplugin dirs ──
if [ -d "$PREFIX/share/manios" ] && [ -w "$PREFIX/share/manios" ] 2>/dev/null; then
    SHAREDIR="$PREFIX/share/manios"
elif [ -d "/usr/local/share/manios" ] && [ -w "/usr/local/share/manios" ] 2>/dev/null; then
    SHAREDIR="/usr/local/share/manios"
else
    SHAREDIR="$HOME/.manios"
fi

mkdir -p "$HOME/.manios/nplugin"
INCDIR="$SHAREDIR/include"
mkdir -p "$INCDIR"

cat > "$INCDIR/mnos_ext.h" << 'EOF'
#ifndef MNOS_EXT_H
#define MNOS_EXT_H
#include "mnos.h"
/* MnosExtFn + MnosExtFunc da co san trong mnos.h (da patch) */
#define MNOS_EXT_EXPORT __attribute__((visibility("default")))
#define MNOS_EXT_BEGIN(extname) \
    MNOS_EXT_EXPORT MnosExtFunc* mnos_ext_init(int *nfuncs) { \
        static MnosExtFunc funcs[] = {
#define MNOS_EXT_FUNC(fname,cfunc,desc) { (fname),(desc),(cfunc) },
#define MNOS_EXT_END \
            {NULL,NULL,NULL} }; \
        *nfuncs=(int)(sizeof(funcs)/sizeof(MnosExtFunc))-1; return funcs; }
#endif
EOF
cp "$SRCDIR/src/mnos.h" "$INCDIR/mnos.h" 2>/dev/null
info "Include: $INCDIR/mnos_ext.h + mnos.h"
info "Plugin dir: ~/.manios/nplugin/"

# ── 10. Xong ──
clear
echo ""
echo "  ======================================="
echo "    nPlugins: SAN SANG!"
echo "  ======================================="
echo ""
echo "  Binary:     $INSTALL_BIN"
echo "  Plugin dir: ~/.manios/nplugin/"
echo "  Include:    $INCDIR/"
echo ""
echo "  Build plugin:"
echo "    nplugin"
echo ""

# ── 10. Auto-install nplugin.c tu GitHub ──
echo "  ${C}--- Auto-install nplugin core ---${NC}"
NPLUGIN_C_URL="https://raw.githubusercontent.com/anhnoine/n-manios/main/nPlugins/nplugin.c"
NPLUGIN_TMP="$(mktemp /tmp/nplugin_core.XXXXXX.c)"
if curl -sL "$NPLUGIN_C_URL" -o "$NPLUGIN_TMP" 2>/dev/null; then
    info "Downloaded: nplugin.c"

    # Compile as .so plugin (loadable by Manios)
    gcc -shared -fPIC -I"$INCDIR" -o "$HOME/.manios/nplugin/nplugin.so" "$NPLUGIN_TMP" 2>/dev/null && \
        info "Plugin: nplugin.so" || \
        warn "Compile nplugin.so that bai"

    # Compile as CLI executable (chay truc tiep tren terminal)
    gcc -DNPLUGIN_CLI -o "$MNODIR/nplugin" "$NPLUGIN_TMP" 2>/dev/null && \
        chmod +x "$MNODIR/nplugin" && \
        info "CLI: $MNODIR/nplugin" || \
        warn "Compile nplugin CLI that bai"

    rm -f "$NPLUGIN_TMP"
else
    warn "Khong the tai nplugin.c (bo qua)"
fi
echo ""
