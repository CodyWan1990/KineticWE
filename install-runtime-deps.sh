#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Install Host Runtime Dependencies
# =============================================================================
# The dev build installs into TEST_PREFIX without RPM/DPKG dependency
# resolution, so a fresh host may be missing the shared libraries the
# KineticWE binaries link against. That only surfaces at login as a cryptic
# "library not found" loader error.
#
# This script inspects the actual installed binaries and installs exactly the
# runtime libraries the host is missing. It is the engine behind
#   ./build-rpm-dev.sh --install-host-deps
# and can also be run directly after a build to top up deps:
#   ./install-runtime-deps.sh
#
# On a fresh minimal host (an install from TTY with no desktop packages) the
# KineticWE session would otherwise come up with no Breeze icon/cursor theme
# and no terminal. So in addition to the library resolution below it also
# installs the default session packages: the Breeze icon theme, the Breeze
# color schemes, the Breeze cursor theme and Alacritty. These installs are
# idempotent — any package the host already has is skipped.
#
# How it works:
#   1. Detect the host package manager (dnf / apt).
#   2. ldd the dev binaries (kinetic-we, noctalia-kwe, xdg-desktop-portal-kwe)
#      with the dev LD_LIBRARY_PATH and collect any "not found" libraries.
#   3. Map each missing library to the package that provides it:
#        - dnf: dnf repoquery --whatprovides <lib>
#        - apt: apt-file search <lib>
#   4. Install the resolved packages (sudo may prompt).
#   5. Re-ldd; if anything is still missing, exit 1 with the list.
#
# If no dev binaries are installed yet (fresh clone before a build), it falls
# back to the verified baseline runtime package set for Fedora. On Debian it
# instead asks you to build first so auto-detection can run.
#
# Environment variables:
#   TEST_PREFIX  - Where the dev build is installed (default: $HOME/.local)
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local}"
DEV_LD_LIBRARY_PATH="${TEST_PREFIX}/lib64:${TEST_PREFIX}/lib/x86_64-linux-gnu"

# The dev binaries to inspect. The portal backend is a separate subpackage and
# may not be present on a partial build; we skip binaries that are not there.
DEV_BINARIES=(
    "${TEST_PREFIX}/bin/kinetic-we"
    "${TEST_PREFIX}/bin/noctalia-kwe"
    "${TEST_PREFIX}/libexec/xdg-desktop-portal-kwe"
)

# Baseline runtime packages verified to be needed on a fresh Fedora host.
# Used only when no dev binaries are present yet (pre-build). After a build,
# the ldd-based detection above fills in anything beyond this list.
BASELINE_RPM_PKGS=(
    kf6-kstatusnotifieritem
    kwayland
    kf6-kglobalaccel
    kf6-kio-core
    kf6-kio-gui
    kf6-kio-widget
    kf6-kio-file-widgets
    kf6-kjobwidgets
    kf6-kcompletion
    kf6-kitemviews
    kf6-solid
    kf6-kbookmark
    kf6-kservice
    kf6-kcrash
)

# Default session packages so a fresh install from TTY actually has: the
# Breeze icon theme (breeze-icon-theme), the Breeze color schemes that the
# session seeds (BreezeDark/BreezeLight — plasma-breeze-common), the Breeze
# cursor theme, and a terminal the user can launch. Installed idempotently by
# install_default_session_packages.
DEFAULT_RPM_PKGS=(
    breeze-icon-theme
    plasma-breeze-common
    breeze-cursor-theme
    alacritty
)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

PM=""

detect_pkgmgr() {
    if command -v dnf &>/dev/null; then
        PM=dnf
    elif command -v apt-get &>/dev/null; then
        PM=apt
    else
        error "No supported package manager found (need dnf or apt-get)."
        exit 1
    fi
    info "Package manager: ${PM}"
}

# Collect the "not found" libraries for every installed dev binary into the
# global array missing_libs.
missing_libs=()
collect_missing() {
    missing_libs=()
    local binary out lib
    for binary in "${DEV_BINARIES[@]}"; do
        [[ -x "$binary" ]] || continue
        out=$(LD_LIBRARY_PATH="$DEV_LD_LIBRARY_PATH" ldd "$binary" 2>/dev/null \
              | awk '/not found/ {print $1}' || true)
        [[ -z "$out" ]] && continue
        while IFS= read -r lib; do
            [[ -n "$lib" ]] && missing_libs+=("$lib")
        done <<< "$out"
    done
}

# Map each unique missing library to its providing package, then install the
# resolved set.
resolve_and_install() {
    local -a pkgs=()
    local lib pkg
    while IFS= read -r lib; do
        [[ -z "$lib" ]] && continue
        if [[ "$PM" == dnf ]]; then
            pkg=$(dnf repoquery --whatprovides "$lib" --queryformat '%{name}' 2>/dev/null \
                  | sort -u | head -1)
        else
            if ! command -v apt-file &>/dev/null; then
                warn "    $lib -> apt-file not installed; cannot auto-map."
                warn "            Install apt-file and update its index, then re-run."
                continue
            fi
            pkg=$(apt-file search "$lib" 2>/dev/null | head -1 | awk -F: '{print $1}')
        fi
        if [[ -n "$pkg" ]]; then
            info "    $lib -> $pkg"
            pkgs+=("$pkg")
        else
            warn "    $lib -> (no package found)"
        fi
    done < <(printf '%s\n' "${missing_libs[@]}" | sort -u)

    if [[ ${#pkgs[@]} -eq 0 ]]; then
        warn "No packages resolved for the missing libraries."
        return 1
    fi

    echo
    info "Installing: ${pkgs[*]}"
    if [[ "$PM" == dnf ]]; then
        sudo dnf install -y "${pkgs[@]}"
    else
        sudo apt-get install -y "${pkgs[@]}"
    fi
}

verify_resolved() {
    collect_missing
    [[ ${#missing_libs[@]} -eq 0 ]]
}

# Install the default session packages (Breeze icon theme, Breeze cursor theme,
# Alacritty) on a fresh host. Skips anything already installed, so re-runs are
# no-ops. This is what makes a fresh-from-TTY install come up with icons, a
# cursor theme and a terminal instead of a bare desktop.
install_default_session_packages() {
    local -a missing=()
    local pkg
    for pkg in "${DEFAULT_RPM_PKGS[@]}"; do
        if [[ "$PM" == dnf ]]; then
            if ! rpm -q "$pkg" &>/dev/null; then
                missing+=("$pkg")
            fi
        elif ! dpkg-query -W "$pkg" &>/dev/null 2>&1; then
            missing+=("$pkg")
        fi
    done
    if [[ ${#missing[@]} -eq 0 ]]; then
        info "Default session packages already installed."
        return 0
    fi
    echo
    info "Fresh host: installing default session packages (Breeze icons, cursor theme, terminal):"
    printf '    %s\n' "${missing[@]}"
    if [[ "$PM" == dnf ]]; then
        sudo dnf install -y "${missing[@]}"
    else
        sudo apt-get install -y "${missing[@]}"
    fi
    info "Default session packages installed."
}

main() {
    detect_pkgmgr

    install_default_session_packages

    local has_binaries=false binary
    for binary in "${DEV_BINARIES[@]}"; do
        if [[ -x "$binary" ]]; then
            has_binaries=true
            break
        fi
    done

    if ! $has_binaries; then
        if [[ "$PM" == dnf ]]; then
            echo
            info "No dev binaries found in ${TEST_PREFIX}; installing baseline runtime deps..."
            sudo dnf install -y "${BASELINE_RPM_PKGS[@]}"
            echo
            info "Baseline runtime deps installed. After running a build, re-run this"
            info "script to auto-detect and install anything beyond this list."
            return 0
        fi
        warn "No KineticWE dev binaries found in ${TEST_PREFIX}; cannot auto-detect deps."
        warn "Run a build first (e.g. ./build-rpm-dev.sh --quick), then re-run this script."
        exit 1
    fi

    collect_missing
    if [[ ${#missing_libs[@]} -eq 0 ]]; then
        step "All runtime libraries resolve on the host."
        return 0
    fi

    echo
    info "Missing runtime libraries (for dev build in ${TEST_PREFIX}):"
    printf '    %s\n' "${missing_libs[@]}" | sort -u
    echo

    resolve_and_install

    echo
    step "Re-verifying..."
    if ! verify_resolved; then
        error "Some runtime libraries are still missing:"
        printf '    %s\n' "${missing_libs[@]}" | sort -u
        error "Investigate with: dnf repoquery --whatprovides '<lib>' (Fedora)"
        error "               or: apt-file search '<lib>' (Debian)"
        exit 1
    fi

    info "All runtime libraries now resolve on the host."
}

main "$@"
