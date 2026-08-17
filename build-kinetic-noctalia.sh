#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Noctalia Shell Iteration Script (incremental)
# =============================================================================
# Rebuilds and reinstalls ONLY the Noctalia fork shell (shell/noctalia) into
# TEST_PREFIX. The meson build dir is kept across runs, so ninja rebuilds just
# the changed targets — a full `./build-rpm-dev.sh --quick` does
# `rm -rf` on the build dirs and reconfigure+builds kwin, the portal and the
# shell every time, which is overkill when you're only iterating on
# kinetic-settings or the shell itself.
#
# Usage:
#   ./build-kinetic-noctalia.sh                  # Incremental rebuild + install
#   ./build-kinetic-noctalia.sh --reconfigure    # Wipe build dir + full meson setup + build + install
#   ./build-kinetic-noctalia.sh --compile-only   # Build only, skip install
#   ./build-kinetic-noctalia.sh --help
#
# Requirements: run `./build-rpm-dev.sh --quick` once so the distrobox
# container and TEST_PREFIX exist.
#
# Environment variables:
#   TEST_PREFIX     - Install prefix (default: $HOME/.local, same as build-rpm-dev.sh)
#   DISTROBOX_NAME  - distrobox container name (default: kineticwe-build)
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local}"
DISTROBOX_NAME="${DISTROBOX_NAME:-kineticwe-build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHELL_BUILD="${SCRIPT_DIR}/build.rpm-quick-noctalia"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

print_usage() {
    echo "Usage: $0 [--reconfigure] [--compile-only] [--help]"
    echo
    echo "  (no flag)        Incremental rebuild of shell/noctalia + install to TEST_PREFIX"
    echo "  --reconfigure    Wipe the meson build dir, full configure + build + install"
    echo "  --compile-only   Build only, don't install"
    echo
    echo "Env: TEST_PREFIX=$TEST_PREFIX  DISTROBOX_NAME=$DISTROBOX_NAME"
}

main() {
    local do_reconfigure=false
    local do_install=true

    for arg in "$@"; do
        case "$arg" in
            --reconfigure) do_reconfigure=true ;;
            --compile-only) do_install=false ;;
            --help|-h) print_usage; exit 0 ;;
            *) warn "Unknown option: $arg";;
        esac
    done

    # No grep -q: it exits on first match, distrobox list dies with SIGPIPE,
    # and pipefail then reports the pipeline as failed even when it matched.
    if ! distrobox list 2>/dev/null | grep -w "$DISTROBOX_NAME" > /dev/null; then
        error "Distrobox container '$DISTROBOX_NAME' not found."
        error "Run './build-rpm-dev.sh --quick' once to create it."
        exit 1
    fi

    if [[ ! -d "${SHELL_BUILD}" && "$do_reconfigure" == false ]]; then
        warn "No existing build dir at $SHELL_BUILD — doing a full setup."
        do_reconfigure=true
    fi

    echo "=========================================="
    echo "  Noctalia Shell Iteration Build"
    echo "  Mode: $([[ $do_reconfigure == true ]] && echo 'full reconfigure' || echo 'incremental')"
    echo "  Install prefix: ${TEST_PREFIX}"
    echo "=========================================="
    echo

    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        set -euo pipefail

        # Strip Nix from the environment so meson/ninja don't pick up
        # Nix-store libraries that won't exist on the host at runtime
        # (same as quick_build in build-rpm-dev.sh).
        unset NIX_REMOTE
        unset NIX_PATH
        unset NIX_SSL_CERT_FILE
        unset NIX_PROFILE
        unset LD_LIBRARY_PATH
        unset XDG_DATA_DIRS
        unset PKG_CONFIG_PATH
        export PATH='/usr/local/bin:/usr/bin:/bin'

        cd '$SCRIPT_DIR'

        if [[ '$do_reconfigure' == true ]]; then
            echo '[STEP] Configuring noctalia-kwe (full setup)...'
            rm -rf '$SHELL_BUILD'
            meson setup '$SHELL_BUILD' 'shell/noctalia' \\
                --prefix='${TEST_PREFIX}' \\
                --buildtype=debugoptimized \\
                -Dtests=disabled
        fi

        echo '[STEP] Building noctalia-kwe...'
        meson compile -C '$SHELL_BUILD' --jobs \$(nproc)

        if [[ '$do_install' == true ]]; then
            echo '[STEP] Installing to ${TEST_PREFIX}...'
            meson install -C '$SHELL_BUILD'
        fi
    "

    if [[ ! -x "$TEST_PREFIX/bin/noctalia-kwe" ]]; then
        error "Build did not produce $TEST_PREFIX/bin/noctalia-kwe"
        exit 1
    fi

    echo
    info "Done. Shell binary updated: $TEST_PREFIX/bin/noctalia-kwe"
    info "Restart your TTY session to pick it up:"
    info "  start-kineticwe"
    echo
}

main "$@"