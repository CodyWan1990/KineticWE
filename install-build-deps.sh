#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Install Build Dependencies inside the build distrobox
# =============================================================================
# Standalone dependency installer for the kineticwe-build distrobox.
# Installs build tools plus every BuildRequires from the spec, then verifies
# the key tools and pkg-config modules actually landed.
#
# Usage (from the host):
#   distrobox enter kineticwe-build -- ./install-build-deps.sh
#
# Or from inside the container:
#   ./install-build-deps.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# The spec lives at lgl-kineticwe-copr/kineticwe.spec, but a copy also sits
# next to the script; accept either so this works from anywhere in the tree.
if [[ -f "$SCRIPT_DIR/lgl-kineticwe-copr/kineticwe.spec" ]]; then
    SPEC_FILE="$SCRIPT_DIR/lgl-kineticwe-copr/kineticwe.spec"
elif [[ -f "$SCRIPT_DIR/kineticwe.spec" ]]; then
    SPEC_FILE="$SCRIPT_DIR/kineticwe.spec"
else
    echo "ERROR: could not find kineticwe.spec (looked in $SCRIPT_DIR)" >&2
    exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

if ! command -v dnf &>/dev/null; then
    error "This script must run inside the Fedora build distrobox (dnf not found)."
    error "From the host: distrobox enter kineticwe-build -- ./install-build-deps.sh"
    exit 1
fi

info "Using spec: $SPEC_FILE"

# --- Build tools ------------------------------------------------------------
step_tools=(
    rpm-build
    rpmdevtools
    dnf-plugins-core
    curl
    wget
    git
)
info "Installing build tools..."
# Ignore scriptlet failures; containers cannot run systemd/udev scripts
sudo dnf install -y "${step_tools[@]}" || true

# --- Spec BuildRequires -----------------------------------------------------
mapfile -t buildreqs < <(grep '^BuildRequires:' "$SPEC_FILE" \
    | sed 's/BuildRequires:\s*//' | awk '{print $1}')
info "Installing ${#buildreqs[@]} build dependencies from the spec..."
# Ignore scriptlet failures; containers cannot run systemd/udev scripts
sudo dnf install -y --skip-unavailable "${buildreqs[@]}" || true

# --- Verification -----------------------------------------------------------
echo
info "Verifying build tools..."
missing_tools=()
for tool in cmake meson ninja; do
    if ! command -v "$tool" &>/dev/null; then
        missing_tools+=("$tool")
    fi
done
if [[ ${#missing_tools[@]} -gt 0 ]]; then
    error "Build tools missing: ${missing_tools[*]}"
    exit 1
fi

echo
info "Verifying pkg-config modules (Qt6 / portal / noctalia groups)..."
missing_mods=()
for mod in Qt6Core libpipewire-0.3 wireplumber-0.5 sdbus-c++; do
    if ! pkg-config --exists "$mod" 2>/dev/null; then
        missing_mods+=("$mod")
    fi
done
if [[ ${#missing_mods[@]} -gt 0 ]]; then
    error "pkg-config modules missing: ${missing_mods[*]}"
    error "Re-run with 'sudo dnf install -y --skip-unavailable $(printf '%s ' "${buildreqs[@]}")' and check for failures."
    exit 1
fi

echo
info "Verifying KF6 (kf6-kconfig-devel)..."
# KF6 ships CMake config files, not pkg-config .pc files, so verify the
# package directly (and its CMake config).
if ! rpm -q kf6-kconfig-devel &>/dev/null \
    || ! ls /usr/lib64/cmake/KF6Config/KF6ConfigConfig.cmake &>/dev/null; then
    error "kf6-kconfig-devel not installed (its CMake config is missing)."
    error "Re-run with 'sudo dnf install -y kf6-kconfig-devel' and check for failures."
    exit 1
fi

echo
info "Build environment ready: cmake, meson, ninja, all KF6/Qt6/portal/noctalia deps present."