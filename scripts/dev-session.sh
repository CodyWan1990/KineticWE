#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Dev Session Glue (no build)
# =============================================================================
# Refreshes ONLY the pieces a display manager needs to offer the dev build as
# a login choice:
#
#   1. $TEST_PREFIX/bin/start-kineticwe  - the dev session launcher,
#      regenerated from scripts/start-kineticwe.sh with the current branch's
#      session logic (2.0: KineticWE identity, no KDE services).
#   2. /usr/local/share/wayland-sessions/kineticwe-dev.desktop
#      - the "KineticWE-Dev" greeter entry pointing at that launcher.
#
# It does NOT build anything. Run ./build-rpm-dev.sh --quick after code
# changes; run this after session-script / desktop-template changes, or any
# time you want the greeter entry to track this branch.
#
# Environment:
#   TEST_PREFIX  - Dev install prefix (default: $HOME/.local)
#
# Usage:
#   ./scripts/dev-session.sh            # refresh start script + greeter entry
#   ./scripts/dev-session.sh --no-session   # only the start script
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Login-screen session entry for the dev build. /usr/local/share is in the
# default session search path of SDDM / plasma-login-manager and is writable
# on rpm-ostree systems, so the entry survives deployments without touching
# the COPR-managed /usr.
SESSION_DIR="/usr/local/share/wayland-sessions"
SESSION_DESKTOP="kineticwe-dev.desktop"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

# ---------------------------------------------------------------------------
# 1. Install the dev startup script for the current branch
# ---------------------------------------------------------------------------
install_startup_script() {
    step "Installing dev startup script..."

    local template="$SCRIPT_DIR/start-kineticwe.sh"
    if [[ ! -f "$template" ]]; then
        error "Missing template: $template"
        exit 1
    fi

    mkdir -p "$TEST_PREFIX/bin"

    sed -e "s|@INSTALL_PREFIX@|$TEST_PREFIX|g" \
        "$template" \
        > "$TEST_PREFIX/bin/start-kineticwe"
    chmod +x "$TEST_PREFIX/bin/start-kineticwe"
    info "  $TEST_PREFIX/bin/start-kineticwe"

    # The 2.0 session target (pulls graphical-session.target up for the
    # portal frontend). The launcher installs it into the user unit dir on
    # first start; this keeps packaged and dev flows identical.
    mkdir -p "$TEST_PREFIX/share/kineticwe"
    install -m 0644 "$SCRIPT_DIR/kineticwe-session.target" \
        "$TEST_PREFIX/share/kineticwe/kineticwe-session.target"
    info "  $TEST_PREFIX/share/kineticwe/kineticwe-session.target"
}

# ---------------------------------------------------------------------------
# 2. Install the "KineticWE-Dev" login-screen session entry
# ---------------------------------------------------------------------------
# Display managers (SDDM / plasma-login-manager) only scan system session
# dirs — never ~/.local/share/wayland-sessions — so the entry must live in
# SESSION_DIR (see above). The entry's Exec points at the same launcher used
# for TTY testing, so both login paths run identical code.
#
# One-time sudo: skipped on later runs while the installed file is identical.
install_dev_session_entry() {
    step "Installing 'KineticWE-Dev' login-screen session entry..."

    local template="$SCRIPT_DIR/kineticwe-dev.desktop.in"
    if [[ ! -f "$template" ]]; then
        error "Missing template: $template"
        exit 1
    fi

    local tmp_file
    # .desktop suffix: desktop-file-validate rejects files without it.
    tmp_file=$(mktemp --suffix=.desktop /tmp/kineticwe-dev.XXXXXX)
    sed -e "s|@INSTALL_PREFIX@|$TEST_PREFIX|g" \
        "$template" \
        > "$tmp_file"

    # DesktopNames is a valid key for session files (desktop entry spec 1.5)
    # but desktop-file-validate still flags it — even Fedora's shipped
    # plasma.desktop "fails" on it — so ignore that false positive.
    if command -v desktop-file-validate &>/dev/null; then
        local validate_out
        validate_out=$(desktop-file-validate "$tmp_file" 2>&1 | grep -v 'DesktopNames' || true)
        if [[ -n "$validate_out" ]]; then
            warn "desktop-file-validate reported issues (continuing anyway):"
            warn "$validate_out"
        fi
    fi

    local target="$SESSION_DIR/$SESSION_DESKTOP"

    # Skip sudo entirely when the installed entry is already up to date.
    # -n: fail fast instead of hanging when there is no tty to prompt on.
    if [[ -f "$target" ]] && cmp -s "$tmp_file" "$target"; then
        info "  $target (already up to date)"
        rm -f "$tmp_file"
    elif sudo -n mkdir -p "$SESSION_DIR" \
        && sudo -n install -m 0644 "$tmp_file" "$target"; then
        rm -f "$tmp_file"
        info "  $target"
        info "  Select 'KineticWE-Dev' at the login screen (appears after logout)."
    else
        rm -f "$tmp_file"
        warn "Could not install the session entry (sudo declined/failed)."
        warn "The dev build still works from a TTY: $TEST_PREFIX/bin/start-kineticwe"
        warn "To install the login entry manually:"
        warn "  sudo install -m 0644 <(sed 's|@INSTALL_PREFIX@|$TEST_PREFIX|' '$template') $target"
    fi
}

# ---------------------------------------------------------------------------
# Warn about missing pieces that would make the dev session incomplete
# ---------------------------------------------------------------------------
check_prereqs() {
    if [[ ! -x "$TEST_PREFIX/bin/kinetic-we" ]]; then
        warn "No dev build found at $TEST_PREFIX/bin/kinetic-we."
        warn "The session entry is ready, but run ./build-rpm-dev.sh --quick"
        warn "before logging into 'KineticWE-Dev'."
    fi
    if ! command -v noctalia-kwe &>/dev/null && [[ ! -x "$TEST_PREFIX/bin/noctalia-kwe" ]]; then
        warn "noctalia-kwe not found in PATH or $TEST_PREFIX/bin."
        warn "The session will run compositor-only until the shell is installed."
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    local do_session=true

    for arg in "$@"; do
        case "$arg" in
            --no-session) do_session=false ;;
            --help|-h)
                echo "Usage: $0 [--no-session]"
                echo "  TEST_PREFIX overrides the dev prefix (default: \$HOME/.local)"
                exit 0 ;;
            *) warn "Unknown option: $arg" ;;
        esac
    done

    echo "=========================================="
    echo "  KineticWE Dev Session Glue (no build)"
    echo "  Prefix: ${TEST_PREFIX}"
    echo "=========================================="
    echo

    install_startup_script
    if $do_session; then
        install_dev_session_entry
    fi
    check_prereqs

    echo
    info "To test: log out, pick 'KineticWE-Dev' at the greeter."
    info "Session log: journalctl --user -b -f   (compositor output, SDDM session scope)"
    info "Shell log:   ~/.local/share/noctalia-kwe.log"
    echo
}

main "$@"
