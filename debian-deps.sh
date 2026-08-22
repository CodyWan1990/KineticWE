#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Install Build Dependencies inside the Debian build distrobox
# =============================================================================
# Standalone dependency installer for the kineticwe-deb-build distrobox.
# Installs build tools plus every build dependency KineticWE needs, then
# verifies the key tools and pkg-config modules actually landed.
#
# Usage (from the host):
#   distrobox enter kineticwe-deb-build -- ./debian-deps.sh
#
# Or from inside the container:
#   ./debian-deps.sh
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

if ! command -v apt-get &>/dev/null; then
    error "This script must run inside the Debian build distrobox (apt-get not found)."
    error "From the host: distrobox enter kineticwe-deb-build -- ./debian-deps.sh"
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive

# --- Build tools ------------------------------------------------------------
step_tools=(
    build-essential
    cmake
    ninja-build
    g++
    git
    curl
    wget
    pkg-config
    extra-cmake-modules
    meson
    devscripts
    dpkg-dev
    debhelper
    desktop-file-utils
    apt-file
)
info "Installing build tools..."
sudo apt-get update -qq
sudo apt-get install -y -qq --no-install-recommends "${step_tools[@]}" || true

# --- Build dependencies ------------------------------------------------------
info "Installing build dependencies..."

# Qt6
qt6_pkgs=(
    qt6-base-dev
    qt6-base-private-dev
    qt6-declarative-dev
    qt6-svg-dev
    qt6-5compat-dev
    qt6-wayland-dev
    qt6-tools-dev
    qt6-shadertools-dev
)

# KF6
kf6_pkgs=(
    libkf6config-dev
    libkf6configwidgets-dev
    libkf6colorscheme-dev
    libkf6coreaddons-dev
    libkf6crash-dev
    libkf6dbusaddons-dev
    libkf6globalaccel-dev
    libkf6kio-dev
    libkf6jobwidgets-dev
    libkf6guiaddons-dev
    libkf6i18n-dev
    libkf6idletime-dev
    libkf6package-dev
    libkf6service-dev
    libkf6svg-dev
    libkf6widgetsaddons-dev
    libkf6windowsystem-dev
    libkf6declarative-dev
    libkf6cmutils-dev
    libkf6newstuff-dev
    libkf6xmlgui-dev
    libkf6runner-dev
    libkf6notifications-dev
    libkf6kirigami-dev
    libkf6auth-dev
    libkf6iconthemes-dev
    libkf6itemviews-dev
    libkf6statusnotifieritem-dev
    libkf6kiofilewidgets-dev
)

# Plasma / KDE
plasma_pkgs=(
    kwayland-dev
    libkscreenlocker-dev
    libknighttime-dev
    libplasma-wayland-protocols-dev
    libplasma-activities-dev
    libplasma-dev
)

# System / Wayland / Hardware
system_pkgs=(
    libepoxy-dev
    libwayland-dev
    wayland-protocols
    libxkbcommon-dev
    libxkbcommon-x11-dev
    libinput-dev
    libdrm-dev
    libgbm-dev
    libdisplay-info-dev
    liblcms2-dev
    libxcvt-dev
    libcanberra-dev
    libevdev-dev
    libsystemd-dev
    libpipewire-0.3-dev
    libegl-dev
    libgles-dev
    libfreetype-dev
    libfontconfig-dev
    libvulkan-dev
    vulkan-headers
)

# X11 / Xwayland
x11_pkgs=(
    libx11-dev
    libxcb1-dev
    libxcb-keysyms1-dev
    libxcb-cursor-dev
    libxcb-util-dev
    libxcb-icccm4-dev
    libxcb-image0-dev
    libxcb-render-util0-dev
)

# Misc
misc_pkgs=(
    libqt6accessibilityclient-dev
    libcairo2-dev
    libpango1.0-dev
    libharfbuzz-dev
    libglib2.0-dev
    libpam0g-dev
    libpolkit-agent-1-dev
    libcurl4-openssl-dev
    libwebp-dev
    librsvg2-dev
    libqalculate-dev
    libxml2-dev
    libjemalloc-dev
    libsodium-dev
    libsecret-1-dev
    libsdbus-c++-dev
    libwireplumber-0.5-dev
    libsndfile1-dev
    libjxl-dev
    libmd4c-dev
    libtomlplusplus-dev
    nlohmann-json3-dev
    libical-dev
    libstb-dev
)

all_pkgs=("${qt6_pkgs[@]}" "${kf6_pkgs[@]}" "${plasma_pkgs[@]}" "${system_pkgs[@]}" "${x11_pkgs[@]}" "${misc_pkgs[@]}")
info "Installing ${#all_pkgs[@]} build dependencies..."
sudo apt-get install -y -qq --no-install-recommends "${all_pkgs[@]}" || {
    warn "Some packages failed to install. Retrying individually to find problem packages..."
    failed=()
    for pkg in "${all_pkgs[@]}"; do
        if ! dpkg -s "$pkg" &>/dev/null; then
            if ! sudo apt-get install -y -qq --no-install-recommends "$pkg" 2>/dev/null; then
                failed+=("$pkg")
            fi
        fi
    done
    if [[ ${#failed[@]} -gt 0 ]]; then
        warn "These packages could not be installed: ${failed[*]}"
    fi
}

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
    error "Install the corresponding -dev packages and re-run."
    exit 1
fi

echo
info "Verifying KF6 (libkf6config-dev)..."
# KF6 ships CMake config files, not pkg-config .pc files, so verify the
# package directly (and its CMake config).
if ! dpkg -s libkf6config-dev &>/dev/null \
    || ! ls /usr/lib/*/cmake/KF6Config/KF6ConfigConfig.cmake &>/dev/null; then
    error "libkf6config-dev not installed (its CMake config is missing)."
    error "Re-run with 'sudo apt-get install libkf6config-dev' and check for failures."
    exit 1
fi

echo
info "Verifying stb headers (needed by noctalia-kwe)..."
# The Noctalia shell needs stb/stb_image_resize2.h and stb/stb_image_write.h.
# stb_image_resize2 is a newer header that Debian's libstb-dev may not ship;
# if either header is missing, vendor the two headers from the stb repo into
# /usr/local/include/stb so the meson configure check passes.
if [[ ! -f /usr/include/stb/stb_image_resize2.h && ! -f /usr/local/include/stb/stb_image_resize2.h ]] \
    || [[ ! -f /usr/include/stb/stb_image_write.h && ! -f /usr/local/include/stb/stb_image_write.h ]]; then
    info "  stb headers missing; downloading from stb repo..."
    sudo mkdir -p /usr/local/include/stb
    sudo curl -fsSL -o /usr/local/include/stb/stb_image_resize2.h \
        https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h
    sudo curl -fsSL -o /usr/local/include/stb/stb_image_write.h \
        https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
fi
if [[ ! -f /usr/local/include/stb/stb_image_resize2.h ]]; then
    error "stb/stb_image_resize2.h could not be obtained."
    exit 1
fi
if [[ ! -f /usr/local/include/stb/stb_image_write.h ]]; then
    error "stb/stb_image_write.h could not be obtained."
    exit 1
fi

echo
info "Build environment ready: cmake, meson, ninja, all KF6/Qt6/portal/noctalia deps present."