#!/usr/bin/env bash
# ============================================================================
#  Manios v1.0 — Universal Installer (Termux + Linux)
#  1 lệnh duy nhất: bash install.sh
# ============================================================================
set -e

# ---------- ANSI colors (fallback nếu không hỗ trợ) ----------
if [ -t 1 ] && command -v tput >/dev/null 2>&1 && [ "$(tput colors 2>/dev/null || echo 0)" -ge 8 ]; then
    R='\033[0;31m' G='\033[0;32m' Y='\033[1;33m' C='\033[0;36m' B='\033[1m' NC='\033[0m'
else
    R='' G='' Y='' C='' B='' NC=''
fi

info()  { printf "${G}  v${NC} %s\n" "$1"; }
warn()  { printf "${Y}  !${NC} %s\n" "$1"; }
err()   { printf "${R}  x${NC} %s\n" "$1"; }
ok()    { printf "\n${C}  +-----------------------------+${NC}\n"; printf "${C}  |  ${B}Cai dat xong!${NC}${C}              |${NC}\n"; printf "${C}  +-----------------------------+${NC}\n"; }

# ---------- Banner ----------
printf "\n${C}  +-----------------------------+${NC}\n"
printf "${C}  |  ${B}Manios v1.0 Installer${NC}${C}       |${NC}\n"
printf "${C}  +-----------------------------+${NC}\n\n"

# ---------- Detect platform ----------
IS_TERMUX=0
if [ -n "$PREFIX" ] && [ -d "$PREFIX" ] && [ "$PREFIX" != "/usr" ]; then
    IS_TERMUX=1
fi

# ---------- Determine directories ----------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$SCRIPT_DIR" in
    /sdcard*|/storage/*)
        # Android /sdcard — không thể compile trực tiếp, phải copy sang $HOME
        BUILD_DIR="$HOME/manios-build"
        info "Copy sang $BUILD_DIR ..."
        rm -rf "$BUILD_DIR"
        cp -r "$SCRIPT_DIR" "$BUILD_DIR"
        SRC_DIR="$BUILD_DIR"
        CLEAN_BUILD=1
        ;;
    *)
        BUILD_DIR="$SCRIPT_DIR"
        SRC_DIR="$SCRIPT_DIR"
        CLEAN_BUILD=0
        ;;
esac

# Verify source exists
MAIN_C="$SRC_DIR/src/mnos_main.c"
if [ ! -f "$MAIN_C" ]; then
    err "Khong tim thay source: $MAIN_C"
    exit 1
fi
info "Tim thay source"

# ---------- Detect / install compiler ----------
CC=""
if command -v clang >/dev/null 2>&1; then
    CC=clang
elif command -v gcc >/dev/null 2>&1; then
    CC=gcc
fi

if [ -z "$CC" ]; then
    printf "\n  -> Chua co compiler, dang cai dat...\n"
    if [ "$IS_TERMUX" -eq 1 ]; then
        pkg update -y 2>/dev/null || true
        pkg install -y clang 2>/dev/null || true
    else
        if command -v apt-get >/dev/null 2>&1; then
            sudo apt-get update -y -qq 2>/dev/null || true
            sudo apt-get install -y -qq gcc 2>/dev/null || true
        elif command -v yum >/dev/null 2>&1; then
            sudo yum install -y gcc 2>/dev/null || true
        elif command -v apk >/dev/null 2>&1; then
            sudo apk add gcc musl-dev 2>/dev/null || true
        elif command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --noconfirm gcc 2>/dev/null || true
        fi
    fi
    # Re-check
    if command -v clang >/dev/null 2>&1; then CC=clang
    elif command -v gcc >/dev/null 2>&1; then CC=gcc
    fi
    if [ -z "$CC" ]; then
        err "Khong the cai compiler. Vui long cai thu cong:"
        if [ "$IS_TERMUX" -eq 1 ]; then
            err "  pkg install clang"
        else
            err "  sudo apt install gcc   (Debian/Ubuntu)"
            err "  sudo yum install gcc   (RHEL/CentOS)"
        fi
        exit 1
    fi
fi
info "Compiler: $CC"

# ---------- Compile ----------
printf "\n  -> Dang bien dich...\n"

# All source files — explicitly listed to avoid glob issues on /sdcard
SRCS=(
    "$SRC_DIR/src/mnos_val.c"
    "$SRC_DIR/src/mnos_env.c"
    "$SRC_DIR/src/mnos_lexer.c"
    "$SRC_DIR/src/mnos_parser.c"
    "$SRC_DIR/src/mnos_runtime.c"
    "$SRC_DIR/src/mnos_mnos.c"
    "$SRC_DIR/src/mnos_tool.c"
    "$SRC_DIR/src/mnos_main.c"
)

# Verify every .c file exists
for f in "${SRCS[@]}"; do
    if [ ! -f "$f" ]; then
        err "Thieu file: $f"
        exit 1
    fi
done

# Install directories — with permission fallback
if [ "$IS_TERMUX" -eq 1 ]; then
    BINDIR="$PREFIX/bin"
    SHAREDIR="$PREFIX/share/manios"
else
    # Try /usr/local first, fallback to ~/.local
    if mkdir -p /usr/local/share/manios 2>/dev/null && [ -w /usr/local/share/manios ]; then
        BINDIR="/usr/local/bin"
        SHAREDIR="/usr/local/share/manios"
    elif mkdir -p "$HOME/.local/share/manios" 2>/dev/null; then
        BINDIR="$HOME/.local/bin"
        SHAREDIR="$HOME/.local/share/manios"
        mkdir -p "$BINDIR"
        # Ensure ~/.local/bin is in PATH
        case ":$PATH:" in
            *":$HOME/.local/bin:"*) ;;
            *) export PATH="$HOME/.local/bin:$PATH" ;;
        esac
    else
        # Last resort: use HOME directly
        BINDIR="$HOME/.manios/bin"
        SHAREDIR="$HOME/.manios"
    fi
fi

mkdir -p "$BINDIR" 2>/dev/null || true
mkdir -p "$SHAREDIR" 2>/dev/null || true
mkdir -p "$SHAREDIR/lib" 2>/dev/null || true
mkdir -p "$SHAREDIR/lib/requests" 2>/dev/null || true
mkdir -p "$SHAREDIR/examples" 2>/dev/null || true
mkdir -p "$HOME/.manios/lib" 2>/dev/null || true

# Compile command — explicit -Wno flags to suppress known safe warnings
CFLAGS="-O2 -Wall \
    -Wno-unused-function \
    -Wno-unused-variable \
    -Wno-stringop-truncation \
    -Wno-format-truncation \
    -Wno-misleading-indentation"

LDFLAGS="-lm"

# On Termux with clang, no extra flags needed
# On some Linux systems, -ldl may be needed for dlopen
if [ "$IS_TERMUX" -ne 1 ]; then
    LDFLAGS="$LDFLAGS -ldl"
    # Check if -ldl works (Alpine/musl doesn't have it)
    if ! $CC -o /dev/null -xc - <<< 'int main(){return 0;}' -ldl 2>/dev/null; then
        LDFLAGS="-lm"
    fi
fi

$CC $CFLAGS -o "$SHAREDIR/manios" "${SRCS[@]}" $LDFLAGS

if [ ! -f "$SHAREDIR/manios" ]; then
    err "Bien dich that bai!"
    exit 1
fi
chmod 755 "$SHAREDIR/manios"
info "Bien dich xong ($(du -h "$SHAREDIR/manios" | cut -f1))"

# ---------- Symlinks ----------
ln -sf "$SHAREDIR/manios" "$BINDIR/manios" 2>/dev/null || true
ln -sf "$SHAREDIR/manios" "$BINDIR/mno" 2>/dev/null || true
info "Tao lenh: manios, mno"

# ---------- Install mib ----------
if [ -f "$SRC_DIR/mib" ]; then
    cp "$SRC_DIR/mib" "$BINDIR/mib"
    chmod +x "$BINDIR/mib"
    info "Tao lenh: mib"
fi

# ---------- Copy lib ----------
if [ -d "$SRC_DIR/lib" ]; then
    cp "$SRC_DIR/lib/"*.mno "$SHAREDIR/lib/" 2>/dev/null || true
    if [ -d "$SRC_DIR/lib/requests" ]; then
        cp "$SRC_DIR/lib/requests/"*.mno "$SHAREDIR/lib/requests/" 2>/dev/null || true
    fi
fi

# ---------- Copy examples ----------
if [ -d "$SRC_DIR/examples" ]; then
    cp "$SRC_DIR/examples/"*.mno "$SHAREDIR/examples/" 2>/dev/null || true
fi

# ---------- Copy lib to ~/.manios/lib ----------
if [ -d "$SRC_DIR/lib" ]; then
    cp "$SRC_DIR/lib/"*.mno "$HOME/.manios/lib/" 2>/dev/null || true
    if [ -d "$SRC_DIR/lib/requests" ]; then
        mkdir -p "$HOME/.manios/lib/requests" 2>/dev/null || true
        cp "$SRC_DIR/lib/requests/"*.mno "$HOME/.manios/lib/requests/" 2>/dev/null || true
    fi
fi
info "Tao ~/.manios/lib/"

# ---------- Clean build dir if copied ----------
if [ "$CLEAN_BUILD" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

# ---------- Test ----------
printf "\n"
TEST_OUT=$("$SHAREDIR/manios" "$SHAREDIR/examples/hello.mno" 2>&1) || true
if echo "$TEST_OUT" | grep -q "Hello World" 2>/dev/null; then
    info "Test: $TEST_OUT"
else
    warn "Test output: $TEST_OUT"
fi

# ---------- Done ----------
ok
printf "\n  ${B}Go:${NC}  manios file.mno\n"
printf "        mno file.mno\n"
printf "        mib help\n"
printf "\n  ${B}Binary:${NC} $SHAREDIR/manios\n"
printf "  ${B}Lib:${NC}    $SHAREDIR/lib/\n"
printf "  ${B}Path:${NC}   $BINDIR\n"
if [ -n "${PATH_UPDATE:-}" ]; then
    printf "\n  ${Y}!${NC} Them dong nay vao ~/.bashrc hoac ~/.zshrc:\n"
    printf "    export PATH=\$HOME/.local/bin:\$PATH\n"
fi
printf "\n"
