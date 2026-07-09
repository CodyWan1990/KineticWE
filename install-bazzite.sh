#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - A Kwin Window Environment
# Bazzite / Universal Blue Install Script (rpm-ostree)
# =============================================================================
# This script enables the KineticWE and Noctalia COPR repositories and layers
# the packages via rpm-ostree on any Fedora Atomic / Universal Blue system
# (Bazzite, Bluefin, Aurora, Silverblue, Kinoite, etc.).
#
# Usage:
#   ./install-bazzite.sh
#
# After installation, reboot to apply:
#   systemctl reboot
# =============================================================================

# Colors
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
# Preflight checks
# ---------------------------------------------------------------------------
check_ostree() {
    if ! command -v rpm-ostree &>/dev/null; then
        error "rpm-ostree not found. This system does not appear to be an"
        error "Fedora Atomic / Universal Blue immutable system."
        error "This script is for Bazzite, Bluefin, Aurora, Silverblue, Kinoite, etc."
        exit 1
    fi
}

check_staged() {
    if rpm-ostree status --json | python3 -c "
import sys, json
data = json.load(sys.stdin)
for dep in data['deployments']:
    if dep.get('booted'):
        stashed = dep.get('staged', False)
        if stashed:
            sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
        warn "A system update is staged (pending reboot)."
        warn "Reboot first before installing packages:"
        warn "  systemctl reboot"
        exit 1
    fi
}

check_qt() {
    local qt_ver
    qt_ver=$(rpm -q qt6-qtbase --qf "%{VERSION}" 2>/dev/null || echo "0")
    # The kineticwe COPR package is built against Qt ≥ 6.11. If the base image
    # has Qt 6.10, the transaction will fail because rpm-ostree can't upgrade
    # base packages on bootc/image-mode systems.
    if [[ "$(printf '%s\n' "$qt_ver" "6.11" | sort -V | head -1)" == "$qt_ver" && "$qt_ver" != "6.11" ]]; then
        error "Base image has Qt $qt_ver, but kineticwe COPR requires Qt ≥ 6.11."
        error "Update your system image first, then retry:"
        error "  sudo bootc upgrade && systemctl reboot"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Enable COPR repositories
# ---------------------------------------------------------------------------
enable_coprs() {
    local coprs=("lionheartp/Hyprland" "theblackdon/kineticwe")

    for copr in "${coprs[@]}"; do
        local repo_file="/etc/yum.repos.d/_copr:copr.fedorainfracloud.org:$(echo "$copr" | tr '/' ':').repo"
        if [[ -f "$repo_file" ]]; then
            info "COPR $copr already enabled, skipping."
        else
            step "Enabling COPR $copr ..."
            sudo dnf copr enable -y "$copr"
        fi
    done
}

# ---------------------------------------------------------------------------
# Check if packages are already layered
# ---------------------------------------------------------------------------
layered_package() {
    local pkg="$1"
    rpm-ostree status --json | python3 -c "
import sys, json
data = json.load(sys.stdin)
for dep in data['deployments']:
    if dep.get('booted'):
        pkgs = dep.get('packages', [])
        if '$pkg' in pkgs:
            sys.exit(0)
sys.exit(1)
" 2>/dev/null
}

# ---------------------------------------------------------------------------
# Check if a package is from the base image (not layered)
# ---------------------------------------------------------------------------
base_package() {
    local pkg="$1"
    rpm -q "$pkg" &>/dev/null && ! layered_package "$pkg"
}

# ---------------------------------------------------------------------------
# Install packages via rpm-ostree
# ---------------------------------------------------------------------------
install_packages() {
    local packages=("kineticwe" "noctalia-git")
    local to_install=()

    for pkg in "${packages[@]}"; do
        if layered_package "$pkg"; then
            info "$pkg is already layered, skipping."
        else
            to_install+=("$pkg")
        fi
    done

    if [[ ${#to_install[@]} -eq 0 ]]; then
        info "All packages are already layered. Nothing to do."
        return 0
    fi

    # kineticwe provides and obsoletes these base packages;
    # they must be removed from the base image via override remove
    local base_conflicts=()
    for pkg in kwin kwin-common kwin-libs kglobalacceld; do
        if base_package "$pkg"; then
            base_conflicts+=("$pkg")
        fi
    done

    if [[ ${#base_conflicts[@]} -gt 0 ]]; then
        step "Replacing base packages: ${base_conflicts[*]} ..."
        step "Layering: ${to_install[*]} ..."
        sudo rpm-ostree override remove "${base_conflicts[@]}" \
            $(printf -- '--install=%s ' "${to_install[@]}")
    else
        step "Layering packages: ${to_install[*]} ..."
        sudo rpm-ostree install -y "${to_install[@]}"
    fi

    echo
    info "Layering complete! Reboot to apply:"
    info "  systemctl reboot"
}

# ---------------------------------------------------------------------------
# Post-install tips
# ---------------------------------------------------------------------------
post_install_hints() {
    echo
    echo "=========================================="
    echo "  Installation complete!"
    echo "=========================================="
    echo
    info "After reboot, select 'KineticWE' from the SDDM session menu."
    info "Or start from a TTY with: start-kineticwe"
    echo
    info "Press Meta+Space to open the app drawer (Noctalia)."
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo "=========================================="
    echo "  KineticWE Bazzite / uBlue Installer"
    echo "=========================================="
    echo

    check_ostree
    check_staged
    check_qt
    enable_coprs
    install_packages
    post_install_hints
}

main "$@"
