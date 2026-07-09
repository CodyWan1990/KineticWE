#!/bin/bash
set -euo pipefail

# =============================================================================
# SDDM Display Manager Installer for Bazzite / Universal Blue
# =============================================================================
# Replaces plasmalogin with SDDM for better KineticWE compatibility.
#
# Usage:
#   ./install-sddm-bazzite.sh
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

check_ostree() {
    if ! command -v rpm-ostree &>/dev/null; then
        error "rpm-ostree not found. This script only works on rpm-ostree systems."
        exit 1
    fi
}

check_staged() {
    if rpm-ostree status --json | python3 -c "
import sys, json
data = json.load(sys.stdin)
for dep in data['deployments']:
    if dep.get('booted'):
        if dep.get('staged', False):
            sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
        warn "A system update is staged (pending reboot)."
        warn "Reboot first: systemctl reboot"
        exit 1
    fi
}

layered_package() {
    local pkg="$1"
    rpm-ostree status --json | python3 -c "
import sys, json
data = json.load(sys.stdin)
for dep in data['deployments']:
    if dep.get('booted'):
        if '$pkg' in dep.get('packages', []):
            sys.exit(0)
sys.exit(1)
" 2>/dev/null
}

get_current_dm() {
    local dm
    dm=$(readlink /etc/systemd/system/display-manager.service 2>/dev/null || echo "none")
    case "$dm" in
        */sddm.service) echo "sddm" ;;
        */plasmalogin.service) echo "plasmalogin" ;;
        *) echo "unknown ($dm)" ;;
    esac
}

layer_sddm() {
    if layered_package sddm; then
        info "SDDM already layered."
        return 0
    fi
    step "Layering SDDM and SDDM KCM..."
    sudo rpm-ostree install -y sddm sddm-kcm
    echo
    info "SDDM layered. Reboot to apply the new deployment, then run this script again."
    info "  systemctl reboot"
    exit 0
}

activate_sddm() {
    if ! layered_package sddm; then
        error "SDDM is not layered. Run this script without --switch first."
        exit 1
    fi

    local current
    current=$(get_current_dm)
    if [[ "$current" == "sddm" ]]; then
        info "SDDM is already the active display manager."
        return 0
    fi

    step "Stopping plasmalogin..."
    sudo systemctl disable --now plasmalogin 2>/dev/null || true

    step "Switching display manager symlink to SDDM..."
    sudo ln -sf /usr/lib/systemd/system/sddm.service /etc/systemd/system/display-manager.service
    sudo systemctl enable sddm

    step "Starting SDDM..."
    sudo systemctl start sddm

    info "SDDM is now active! Log out to see the SDDM login screen."
}

main() {
    echo "=========================================="
    echo "  SDDM Installer for Bazzite / uBlue"
    echo "=========================================="
    echo

    check_ostree
    check_staged

    local current
    current=$(get_current_dm)
    info "Current display manager: $current"

    if [[ "$current" == "sddm" ]]; then
        info "SDDM is already active. Nothing to do."
        exit 0
    fi

    layer_sddm
    echo "---"
    info "After reboot, run again to activate SDDM:"
    info "  ./install-sddm-bazzite.sh"
}

main "$@"
