#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - One-Shot Installer (dev branch)
# Auto-detects Arch Linux or Fedora and installs accordingly.
# =============================================================================
# Environment variables:
#   INSTALL_PREFIX - Where to install (default: $HOME/.local)
#   REPO_URL       - Git URL for kineticwe (default: GitLab)
#   REPO_BRANCH    - Branch to install from (default: dev)
# =============================================================================

INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local}"
REPO_URL="${REPO_URL:-https://gitlab.com/theblackdon/kineticwe.git}"
REPO_BRANCH="${REPO_BRANCH:-dev}"
KINETICWE_DIR="${KINETICWE_DIR:-$HOME/Kinetic/kineticwe}"
NOCTALIA_URL="${NOCTALIA_URL:-https://github.com/noctalia-dev/noctalia.git}"
KGLOBALACCELD_URL="${KGLOBALACCELD_URL:-https://invent.kde.org/plasma/kglobalacceld.git}"
BUILD_JOBS="$(nproc)"

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
# Detect distribution
# ---------------------------------------------------------------------------
detect_distro() {
    if [[ -f /etc/arch-release ]]; then
        echo "arch"
    elif [[ -f /etc/fedora-release ]]; then
        echo "fedora"
    else
        error "Unsupported distribution. Currently supported: Arch Linux, Fedora."
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Install system dependencies (Arch)
# ---------------------------------------------------------------------------
install_deps_arch() {
    step "Installing system dependencies (pacman)..."
    sudo pacman -S --needed --noconfirm \
        cmake ninja gcc git meson just pkgconf \
        qt6-base qt6-declarative qt6-svg qt6-5compat qt6-wayland qt6-tools \
        extra-cmake-modules \
        kauth kcolorscheme kconfig kcoreaddons kcrash kdbusaddons \
        kglobalaccel kglobalacceld kguiaddons ki18n kidletime kpackage \
        kservice ksvg kwidgetsaddons kwindowsystem kdeclarative kcmutils \
        knewstuff kxmlgui krunner knotifications kirigami \
        kio kjobwidgets \
        kwayland kdecoration kscreenlocker knighttime plasma-wayland-protocols \
        plasma-activities libplasma plasma-workspace kscreen milou aurorae breeze \
        libepoxy vulkan-headers vulkan-icd-loader libglvnd \
        wayland wayland-protocols \
        libxkbcommon libxkbcommon-x11 \
        libinput libdrm mesa \
        libdisplay-info lcms2 libxcvt \
        libcanberra \
        libx11 libxcb xcb-util-keysyms xcb-util-cursor xcb-util-wm \
        xcb-util-image xcb-util-renderutil \
        xorg-xwayland \
        systemd pipewire libevdev \
        libqaccessibilityclient-qt6 hwdata \
        freetype2 fontconfig \
        cairo pango harfbuzz glib2 \
        sdbus-cpp pam polkit curl libwebp librsvg libqalculate libxml2 \
        jemalloc \
        xdg-desktop-portal xdg-desktop-portal-kde
    info "System dependencies installed."
}

# ---------------------------------------------------------------------------
# Install system dependencies (Fedora)
# ---------------------------------------------------------------------------
install_deps_fedora() {
    step "Installing system dependencies (dnf)..."
    sudo dnf install -y --skip-unavailable \
        cmake ninja-build gcc-c++ git meson just \
        qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel \
        qt6-qtsvg-devel qt6-qt5compat-devel qt6-qtwayland-devel \
        qt6-qttools-devel \
        extra-cmake-modules \
        kf6-kauth-devel kf6-kcolorscheme-devel kf6-kconfig-devel \
        kf6-kcoreaddons-devel kf6-kcrash-devel kf6-kdbusaddons-devel \
        kf6-kglobalaccel-devel kglobalacceld-devel \
        kf6-kio-devel kf6-kjobwidgets-devel \
        kf6-kguiaddons-devel kf6-ki18n-devel \
        kf6-kidletime-devel kf6-kpackage-devel kf6-kservice-devel \
        kf6-ksvg-devel kf6-kwidgetsaddons-devel kf6-kwindowsystem-devel \
        kf6-kdeclarative-devel kf6-kcmutils-devel kf6-knewstuff-devel \
        kf6-kxmlgui-devel kf6-krunner-devel kf6-knotifications-devel \
        kf6-kirigami \
        kwayland-devel kdecoration-devel kscreenlocker-devel \
        knighttime-devel plasma-wayland-protocols-devel \
        plasma-activities-devel libplasma-devel plasma-workspace-devel \
        plasma-milou aurorae plasma-breeze kscreen \
        libepoxy-devel vulkan-loader-devel vulkan-headers \
        wayland-devel wayland-protocols-devel \
        libxkbcommon-devel libxkbcommon-x11-devel \
        libinput-devel libdrm-devel mesa-libgbm-devel \
        libdisplay-info-devel lcms2-devel libxcvt-devel \
        libcanberra-devel \
        libX11-devel libxcb-devel xcb-util-keysyms-devel \
        xcb-util-cursor-devel xcb-util-devel xcb-util-wm-devel \
        xcb-util-image-devel xcb-util-renderutil-devel \
        xorg-x11-server-Xwayland \
        systemd-devel pipewire-devel libevdev-devel \
        qaccessibilityclient-qt6-devel pkgconf-pkg-config hwdata \
        libEGL-devel mesa-libGLES-devel freetype-devel fontconfig-devel \
        cairo-devel pango-devel harfbuzz-devel glib2-devel \
        sdbus-cpp-devel pam-devel polkit-devel libcurl-devel \
        libwebp-devel librsvg2-devel libqalculate-devel libxml2-devel \
        jemalloc-devel \
        xdg-desktop-portal xdg-desktop-portal-kde
    info "System dependencies installed."
}

# ---------------------------------------------------------------------------
# Build & install kglobalacceld from git master
# ---------------------------------------------------------------------------
build_kglobalacceld() {
    step "Building kglobalacceld (global shortcuts daemon)..."
    local kga_dir
    kga_dir="$(mktemp -d /tmp/kglobalacceld-build.XXXXXX)"
    trap "rm -rf '$kga_dir'" EXIT

    info "Cloning kglobalacceld..."
    git clone --depth 1 "$KGLOBALACCELD_URL" "$kga_dir"
    cd "$kga_dir"

    cmake -B build -S . \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF
    cmake --build build --parallel "$BUILD_JOBS"
    cmake --install build

    cd /
    rm -rf "$kga_dir"
    trap - EXIT
    info "kglobalacceld installed to $INSTALL_PREFIX"
}

# ---------------------------------------------------------------------------
# Clone kineticwe from dev branch, build & install
# ---------------------------------------------------------------------------
build_kwin_we() {
    local repo_dir="$KINETICWE_DIR"

    if [[ -d "$repo_dir/.git" ]]; then
        step "Updating kineticwe ($REPO_BRANCH branch)..."
        cd "$repo_dir"
        git fetch origin "$REPO_BRANCH"
        git checkout "$REPO_BRANCH"
        git pull origin "$REPO_BRANCH"
    else
        step "Cloning kineticwe ($REPO_BRANCH branch)..."
        mkdir -p "$(dirname "$repo_dir")"
        git clone --branch "$REPO_BRANCH" "$REPO_URL" "$repo_dir"
        cd "$repo_dir"
    fi

    info "Building kwin-we..."
    cmake -B build -S . \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF
    cmake --build build --parallel "$BUILD_JOBS"
    cmake --install build

    cd /
    info "kwin-we built and installed to $INSTALL_PREFIX"
}

# ---------------------------------------------------------------------------
# Clone, build & install noctalia-shell
# ---------------------------------------------------------------------------
build_noctalia() {
    step "Building noctalia-shell..."
    local noctalia_dir
    noctalia_dir="$(mktemp -d /tmp/noctalia-build.XXXXXX)"

    cleanup_noctalia() {
        [[ -n "${noctalia_dir:-}" && -d "$noctalia_dir" ]] && rm -rf "$noctalia_dir"
    }
    trap cleanup_noctalia EXIT

    git clone --depth 1 "$NOCTALIA_URL" "$noctalia_dir"
    cd "$noctalia_dir"

    just configure release "$INSTALL_PREFIX"
    just build release
    just install release

    cd /
    cleanup_noctalia
    trap - EXIT
    info "noctalia-shell installed to $INSTALL_PREFIX"
}

# ---------------------------------------------------------------------------
# Install startup script from cloned repo templates
# ---------------------------------------------------------------------------
install_startup_script() {
    step "Installing startup script..."
    local repo_dir="$KINETICWE_DIR"

    mkdir -p "$INSTALL_PREFIX/bin"
    sed "s|@INSTALL_PREFIX@|$INSTALL_PREFIX|g" \
        "$repo_dir/scripts/start-kineticwe.sh" \
        > "$INSTALL_PREFIX/bin/start-kineticwe"
    chmod +x "$INSTALL_PREFIX/bin/start-kineticwe"

    info "Installed $INSTALL_PREFIX/bin/start-kineticwe"
}

# ---------------------------------------------------------------------------
# Install desktop entry for display managers
# ---------------------------------------------------------------------------
install_desktop_file() {
    step "Installing desktop session entry..."
    local sessions_dir="/usr/share/wayland-sessions"
    sudo mkdir -p "$sessions_dir"

    sudo tee "$sessions_dir/kineticwe.desktop" > /dev/null << EOF
[Desktop Entry]
Name=KineticWE
Comment=A Kwin Window Environment
Exec=$INSTALL_PREFIX/bin/start-kineticwe
Type=Application
DesktopNames=KDE
EOF

    info "Installed $sessions_dir/kineticwe.desktop"
}

# ---------------------------------------------------------------------------
# PATH hint
# ---------------------------------------------------------------------------
ensure_path_hint() {
    if [[ ":$PATH:" != *":$INSTALL_PREFIX/bin:"* ]]; then
        warn "$INSTALL_PREFIX/bin is not in your PATH."
        warn "Add to your shell profile (~/.bashrc or ~/.zshrc):"
        warn "  export PATH=\"$INSTALL_PREFIX/bin:\$PATH\""
        if [[ -d "$INSTALL_PREFIX/lib64" ]]; then
            warn "  export LD_LIBRARY_PATH=\"$INSTALL_PREFIX/lib64:\$LD_LIBRARY_PATH\""
        else
            warn "  export LD_LIBRARY_PATH=\"$INSTALL_PREFIX/lib:\$LD_LIBRARY_PATH\""
        fi
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo "=========================================="
    echo "  KineticWE One-Shot Installer (dev)"
    echo "  Install prefix: $INSTALL_PREFIX"
    echo "=========================================="
    echo

    DISTRO="$(detect_distro)"
    info "Detected distribution: $DISTRO"

    if [[ "$DISTRO" == "arch" ]]; then
        install_deps_arch
    else
        install_deps_fedora
    fi

    build_kglobalacceld
    build_kwin_we
    build_noctalia
    install_startup_script
    install_desktop_file
    ensure_path_hint

    echo
    echo "=========================================="
    echo "  Installation complete!"
    echo "=========================================="
    echo
    info "To start KineticWE from a TTY, run:"
    info "  $INSTALL_PREFIX/bin/start-kineticwe"
    echo
    info "To select KineticWE from SDDM or another greeter:"
    info "  Choose 'KineticWE' from the session menu at login."
    echo
    warn "You must log out or reboot for changes to take effect."
}

main "$@"
