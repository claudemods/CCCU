#!/usr/bin/env bash
# CCCU - Claudemods Container Creator Unity: build script.
#
# Installs the build and runtime dependencies for this machine (Arch, Debian/Ubuntu, Fedora and openSUSE
# families), then builds the terminal tool (cccu) and the Qt6 desktop version (cccu-gui).
#
#   ./build.sh                 install dependencies, build cccu + cccu-gui into ./build
#   ./build.sh --install       ... and install them system-wide (adds CCCU to the application menu)
#   ./build.sh --no-gui        terminal tool only (no Qt6 needed)
#   ./build.sh --deps-only     only install the dependencies
#   ./build.sh --no-deps       do not touch the package manager, just build
#   ./build.sh --clean         remove ./build first
#   ./build.sh -j <N>          parallel build jobs (default: all cores)

set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

GUI=1
INSTALL=0
DEPS=1
BUILD=1
CLEAN=0
JOBS="$(nproc 2>/dev/null || echo 2)"
BUILD_DIR=build

usage() { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --gui) GUI=1 ;;
        --no-gui) GUI=0 ;;
        --install) INSTALL=1 ;;
        --deps-only) BUILD=0 ;;
        --no-deps) DEPS=0 ;;
        --clean) CLEAN=1 ;;
        -j) shift; JOBS="${1:?-j needs a number}" ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if [ -t 1 ]; then R=$'\033[1;31m'; G=$'\033[1;32m'; Y=$'\033[1;33m'; C=$'\033[1;36m'; N=$'\033[0m'; else R=; G=; Y=; C=; N=; fi
info() { printf '%s[ INFO ]%s %s\n' "$C" "$N" "$*"; }
ok()   { printf '%s[  OK  ]%s %s\n' "$G" "$N" "$*"; }
warn() { printf '%s[ WARN ]%s %s\n' "$Y" "$N" "$*"; }
die()  { printf '%s[ FAIL ]%s %s\n' "$R" "$N" "$*" >&2; exit 1; }
step() { printf '\n%s== %s ==%s\n' "$G" "$*" "$N"; }

[ "$(uname -s)" = "Linux" ] || die "CCCU builds and runs on Linux only."

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 && SUDO=sudo
fi
need_root() {
    [ "$(id -u)" -eq 0 ] || [ -n "$SUDO" ] || die "Installing packages needs root: run this script as root or install sudo."
}

# ---- which system is this ---------------------------------------------------------------------

FAMILY=""
NAME="unknown"
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    NAME="${PRETTY_NAME:-${NAME:-unknown}}"
    for id in ${ID:-} ${ID_LIKE:-}; do
        case "$id" in
            arch|manjaro|endeavouros|cachyos|garuda|arcolinux) FAMILY=arch ;;
            debian|ubuntu|linuxmint|pop|neon|mx) FAMILY=debian ;;
            fedora|rhel|centos|rocky|almalinux|nobara) FAMILY=fedora ;;
            opensuse*|suse|sles) FAMILY=suse ;;
        esac
        [ -n "$FAMILY" ] && break
    done
fi

# ---- dependencies ------------------------------------------------------------------------------
# build: compiler (GCC 14+ or Clang 17+, C++23), CMake 3.25+, make
# tools the program calls: rsync tar zstd squashfs-tools (images), parted dosfstools e2fsprogs btrfs-progs (drives)
# gui:   Qt6 Widgets + polkit (the GUI asks for the administrator password through pkexec)

install_dependencies() {
    step "Installing dependencies for $NAME"
    [ -n "$FAMILY" ] || die "Unsupported system ($NAME). Install by hand: a C++23 compiler (GCC 14+), CMake 3.25+, make, rsync, tar, zstd, squashfs-tools, parted, dosfstools, e2fsprogs, btrfs-progs$([ "$GUI" -eq 1 ] && echo ', Qt6 Widgets dev files, polkit')."
    need_root

    local pkgs=()
    case "$FAMILY" in
        arch)
            pkgs=(base-devel cmake rsync tar zstd squashfs-tools parted dosfstools e2fsprogs btrfs-progs)
            [ "$GUI" -eq 1 ] && pkgs+=(qt6-base polkit)
            $SUDO pacman -S --needed --noconfirm "${pkgs[@]}"
            ;;
        debian)
            pkgs=(build-essential cmake rsync tar zstd squashfs-tools parted dosfstools e2fsprogs btrfs-progs)
            [ "$GUI" -eq 1 ] && pkgs+=(qt6-base-dev pkexec)
            $SUDO apt-get update
            # "pkexec" is its own package on newer releases, part of "policykit-1" on older ones.
            if [ "$GUI" -eq 1 ] && ! apt-cache show pkexec >/dev/null 2>&1; then
                pkgs=("${pkgs[@]/pkexec/policykit-1}")
            fi
            $SUDO apt-get install -y "${pkgs[@]}"
            ;;
        fedora)
            pkgs=(gcc-c++ make cmake rsync tar zstd squashfs-tools parted dosfstools e2fsprogs btrfs-progs)
            [ "$GUI" -eq 1 ] && pkgs+=(qt6-qtbase-devel polkit)
            $SUDO dnf install -y "${pkgs[@]}"
            ;;
        suse)
            pkgs=(gcc-c++ make cmake rsync tar zstd squashfs parted dosfstools e2fsprogs btrfsprogs)
            [ "$GUI" -eq 1 ] && pkgs+=(qt6-base-devel polkit)
            $SUDO zypper --non-interactive install "${pkgs[@]}"
            ;;
    esac
    ok "Dependencies installed."
}

# ---- compiler ----------------------------------------------------------------------------------

compiler_ok() {   # compiler_ok <c++ command>: GCC >= 14 or Clang >= 17
    command -v "$1" >/dev/null 2>&1 || return 1
    local macros major
    macros="$(echo | "$1" -dM -E -x c++ - 2>/dev/null)" || return 1
    if grep -q '__clang__' <<<"$macros"; then
        major="$(sed -n 's/^#define __clang_major__ //p' <<<"$macros")"
        [ "${major:-0}" -ge 17 ]
    else
        major="$(sed -n 's/^#define __GNUC__ //p' <<<"$macros")"
        [ "${major:-0}" -ge 14 ]
    fi
}

find_compiler() {
    local cxx
    for cxx in "${CXX:-}" g++-15 g++-14 g++ clang++-19 clang++-18 clang++-17 clang++; do
        [ -n "$cxx" ] || continue
        if compiler_ok "$cxx"; then CXX_FOUND="$cxx"; return 0; fi
    done
    return 1
}

CXX_FOUND=""

# ---- run ---------------------------------------------------------------------------------------

[ "$DEPS" -eq 1 ] && install_dependencies
[ "$BUILD" -eq 1 ] || { ok "Done (dependencies only)."; exit 0; }

step "Checking the toolchain"
if ! find_compiler && [ "$FAMILY" = debian ] && [ "$DEPS" -eq 1 ]; then
    warn "The default compiler is too old for C++23; trying g++-14."
    need_root
    $SUDO apt-get install -y g++-14 || true
    find_compiler || true
fi
[ -n "$CXX_FOUND" ] || die "No C++23 compiler found: CCCU needs GCC 14+ or Clang 17+. This system's compiler is too old - use a newer distribution release or install a newer GCC."
command -v cmake >/dev/null 2>&1 || die "cmake not found (run without --no-deps, or install CMake 3.25+)."
info "Compiler: $CXX_FOUND ($("$CXX_FOUND" --version | head -n1))"
info "CMake:    $(cmake --version | head -n1)"

[ "$CLEAN" -eq 1 ] && { info "Removing ./$BUILD_DIR"; rm -rf "$BUILD_DIR"; }

step "Configuring"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$CXX_FOUND" \
      -DCCCU_BUILD_GUI="$([ "$GUI" -eq 1 ] && echo ON || echo OFF)"

step "Building ($JOBS jobs)"
cmake --build "$BUILD_DIR" -j "$JOBS"

if [ "$INSTALL" -eq 1 ]; then
    step "Installing"
    need_root
    $SUDO cmake --install "$BUILD_DIR"
    command -v update-desktop-database >/dev/null 2>&1 && $SUDO update-desktop-database >/dev/null 2>&1 || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && $SUDO gtk-update-icon-cache -q /usr/local/share/icons/hicolor 2>/dev/null || true
fi

step "Done"
ok "Terminal tool:  $PWD/$BUILD_DIR/cccu        (run: sudo ./$BUILD_DIR/cccu)"
if [ "$GUI" -eq 1 ]; then
    ok "Desktop tool:   $PWD/$BUILD_DIR/cccu-gui    (double-click it, or find CCCU in the application menu after --install)"
fi
