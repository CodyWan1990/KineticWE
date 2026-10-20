#!/bin/bash
# =============================================================================
# KineticWE - Debian 13 (trixie) Dev Build Script
# =============================================================================
# Builds KineticWE from local source on Debian stable and installs it into an
# isolated user prefix. Debian 13 ships Qt 6.8 / KF6 6.13 / Plasma 6.3, which
# are too old for this compositor (needs Qt >= 6.10, KF6 >= 6.27, Plasma libs
# >= 6.6.90, wayland >= 1.24, libdrm >= 2.4.127), so the newer stack is built
# into the prefix as well:
#
#   1. Qt            - official qt.io binaries via aqtinstall
#   2. KDE stack     - KF6 frameworks + Plasma libs + wayland/libdrm/libei via
#                      kde-builder (exact kwin dependency set, pinned tags)
#   3. kinetic-we    - this checkout
#   4. Portal        - portal/xdg-desktop-portal-kwe
#   5. Noctalia      - shell/noctalia (noctalia-kwe, incl. Kinetic Settings),
#                      against Debian's own packages
#   6. Session       - start-kineticwe + KineticWE-Dev session entry
#
# This script NEVER uses sudo. Anything that needs root (apt packages, the
# login-screen session entry, the portal's .portal file) is printed for you to
# run yourself. (build-deb-dev.sh is upstream's separate Debian sid distrobox
# build.)
#
# Usage:
#   ./build-deb-stable.sh                 # Full build (first run: 1-3 hours)
#   ./build-deb-stable.sh --deps-only     # Only Qt + KDE stack
#   ./build-deb-stable.sh --quick         # Incremental kinetic-we + portal + Noctalia rebuild
#   ./build-deb-stable.sh --no-session    # Skip the session-entry hint
#   ./build-deb-stable.sh --reset-local   # Delete KWE_PREFIX and build-deb/
#
# Environment variables:
#   KWE_PREFIX   - Install prefix (default: $HOME/.local/kineticwe)
#   QT_VERSION   - Qt release fetched by aqtinstall (default: 6.11.3)
#   JOBS         - Parallel build jobs (default: nproc)
# =============================================================================

set -euo pipefail

KWE_PREFIX="${KWE_PREFIX:-$HOME/.local/kineticwe}"
QT_VERSION="${QT_VERSION:-6.11.3}"
JOBS="${JOBS:-$(nproc)}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPEC_FILE="${SCRIPT_DIR}/lgl-kineticwe-copr/kineticwe.spec"
BUILD_DIR="${SCRIPT_DIR}/build-deb"

QT_DIR="${KWE_PREFIX}/qt/${QT_VERSION}/gcc_64"
TOOLS_DIR="${KWE_PREFIX}/tools"
KDE_BUILDER_RC="${KWE_PREFIX}/kde-builder.yaml"
# Debian's CMake never searches <prefix>/lib64 for packages (its platform file
# turns FIND_LIBRARY_USE_LIB64_PATHS off), so lib64/cmake is listed as its own
# prefix: find_package() also matches <prefix>/<Name>*/<Name>Config.cmake.
CMAKE_SEARCH_PATH="${QT_DIR};${KWE_PREFIX};${KWE_PREFIX}/lib64/cmake"

# Pinned versions. Frameworks/Plasma use release tags; kdecoration and
# kglobalacceld come from the spec (single source of truth, see below).
AQTINSTALL_VERSION="3.3.0"
KDE_BUILDER_REF="c6b0e3b962153f48f18dab996b7ec184fa2e54a3"
KF6_TAG="v6.30.0"
PLASMA_TAG="v6.7.5"
PLASMA_WAYLAND_PROTOCOLS_TAG="v1.23.0"
WAYLAND_TAG="1.25.0"
WAYLAND_PROTOCOLS_TAG="1.49"
APPSTREAM_TAG="v1.1.6"
LIBDRM_TAG="libdrm-2.4.134"
LIBEI_TAG="1.6.0"
VULKAN_HEADERS_TAG="v1.4.364"
KQUICKIMAGEEDITOR_TAG="v0.7.0.1"
HIGHWAY_TAG="1.3.0"

# kinetic-we needs KDecoration3::Style, which only exists in kdecoration
# master, so use the exact commits the COPR package is built with.
SPEC_KGA_REF=$(sed -n 's/.*%global kglobalacceld_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
SPEC_KDECORATION_REF=$(sed -n 's/.*%global kdecoration_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)

# Login-screen session entry. /usr/local/share/wayland-sessions is searched by
# SDDM and GDM; installing there needs root, so we only stage it and print the
# command.
SESSION_DIR="/usr/local/share/wayland-sessions"
SESSION_DESKTOP="kineticwe-dev.desktop"

# Debian packages needed to build the whole stack (KDE deps, kinetic-we,
# Noctalia) and to run kde-builder / aqtinstall.
DEB_PACKAGES=(
    # toolchain + build tools
    build-essential cmake ninja-build meson pkgconf git curl gettext gperf
    bison flex itstool xsltproc
    python3-venv python3-yaml python3-setproctitle python3-jeepney
    python3-jinja2 python3-attr python3-lxml
    # KF6 (kdoctools, karchive, kio, solid, kwallet, qca, polkit-qt, appstream)
    libxml2-utils libxslt1-dev docbook-xsl docbook-xml liburi-perl iso-codes
    libstemmer-dev libhunspell-dev
    libfyaml-dev libxmlb-dev libyaml-dev libcurl4-gnutls-dev libzstd-dev
    libsystemd-dev libudev-dev libmount-dev libacl1-dev libbz2-dev liblzma-dev
    zlib1g-dev libssl-dev libgcrypt20-dev libgpgme-dev libgpgmepp-dev
    libpolkit-gobject-1-dev libpolkit-agent-1-dev libpam0g-dev libcanberra-dev
    libqrencode-dev
    # X11 / XCB / Xwayland
    libx11-dev libx11-xcb-dev libxfixes-dev libxrender-dev libxi-dev libxss-dev
    libxcursor-dev libxrandr-dev
    libxkbfile-dev libxcb1-dev libxcb-keysyms1-dev libxcb-cursor-dev
    libxcb-util-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-render-util0-dev
    libxcb-xfixes0-dev libxcb-composite0-dev libxcb-damage0-dev
    libxcb-randr0-dev libxcb-shape0-dev libxcb-res0-dev libxcb-xinput-dev
    libxcb-record0-dev libxcb-sync-dev libxcb-dri3-dev libxcb-present-dev
    libxcb-xinerama0-dev libxcb-dpms0-dev libxcb-xtest0-dev libxkbcommon-dev libxkbcommon-x11-dev xwayland
    # compositor
    libinput-dev libevdev-dev libgbm-dev libegl-dev libgles-dev libgl-dev
    libepoxy-dev libdisplay-info-dev liblcms2-dev libxcvt-dev hwdata
    libopengl-dev
    libvulkan-dev libcap-dev libfftw3-dev libpciaccess-dev
    libfreetype-dev libfontconfig-dev dbus-bin
    # kpipewire
    libpipewire-0.3-dev libavcodec-dev libavformat-dev libavfilter-dev
    libswscale-dev libva-dev
    # Noctalia (shell/noctalia/meson.build)
    libwayland-dev libwayland-bin wayland-protocols libsdbus-c++-dev
    libcairo2-dev libpango1.0-dev libharfbuzz-dev librsvg2-dev libglib2.0-dev
    libsecret-1-dev libsodium-dev libwireplumber-0.5-dev libqalculate-dev
    libxml2-dev libmd4c-dev libmd4c-html0-dev nlohmann-json3-dev
    libtomlplusplus-dev libical-dev libjemalloc-dev libwebp-dev libjxl-dev
    libsndfile1-dev libstb-dev
    # portal: qt.io's Qt was built with CUPS, so Qt::PrintSupportPrivate's
    # CMake config links Cups::Cups
    libcups2-dev
    # Spectacle 6.7 (OCR needs OpenCV + Tesseract; prison uses Debian's ZXing
    # and needs libdmtx)
    libopencv-dev libtesseract-dev libzxing-dev libdmtx-dev
)

# Runtime-only packages the session uses from Debian (not built here): the
# start script sets QT_QPA_PLATFORMTHEME=qt6ct; Spectacle's OCR needs a
# Tesseract language. Missing ones are only warned.
DEB_RUNTIME_PACKAGES=(qt6ct tesseract-ocr-eng)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

usage() {
    sed -n '/^# Usage:/,/^# =====/p' "$0" | sed -e 's/^# \{0,1\}//' -e '/^=====/d'
}

# Environment for everything built against the prefix (kinetic-we, Noctalia).
prefix_env() {
    export PATH="$KWE_PREFIX/bin:$QT_DIR/bin:$PATH"
    export PKG_CONFIG_PATH="$KWE_PREFIX/lib64/pkgconfig:$KWE_PREFIX/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export CMAKE_PREFIX_PATH="$QT_DIR:$KWE_PREFIX:$KWE_PREFIX/lib64/cmake"
    export CMAKE_LIBRARY_PATH="$KWE_PREFIX/lib64"
    export XDG_DATA_DIRS="$KWE_PREFIX/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    export QML_IMPORT_PATH="$KWE_PREFIX/lib64/qml"
    export QML2_IMPORT_PATH="$KWE_PREFIX/lib64/qml"
}

# ---------------------------------------------------------------------------
# 0. Host check (no sudo: print the apt line and stop)
# ---------------------------------------------------------------------------
check_host() {
    step "Checking host..."

    if [[ ! -r /etc/debian_version ]]; then
        warn "This does not look like Debian; continuing anyway."
    fi
    if [[ -z "$SPEC_KGA_REF" || -z "$SPEC_KDECORATION_REF" ]]; then
        error "Could not read kglobalacceld_ref/kdecoration_ref from $SPEC_FILE"
        exit 1
    fi

    # Mesa from trixie-backports: libgbm-dev must match the installed libgbm1.
    local gbm_bpo="no"
    [[ "$(dpkg-query -W -f='${Version}' libgbm1 2>/dev/null)" == *bpo* ]] && gbm_bpo="yes"

    local missing=() pkg
    for pkg in "${DEB_PACKAGES[@]}"; do
        if [[ "$(dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null)" != "install ok installed" ]]; then
            [[ "$pkg" == libgbm-dev && "$gbm_bpo" == yes ]] && pkg="libgbm-dev/trixie-backports"
            missing+=("$pkg")
        fi
    done

    if (( ${#missing[@]} )); then
        error "Missing ${#missing[@]} Debian package(s). Install them yourself, then re-run:"
        echo
        echo "  sudo apt install ${missing[*]}"
        echo
        exit 1
    fi

    # Build tools found via PATH (catches e.g. a package present but its
    # binary shadowed or missing).
    local tool
    for tool in bison flex itstool gperf xsltproc perl python3 meson ninja; do
        if ! command -v "$tool" >/dev/null; then
            error "Required build tool '$tool' not found in $PATH"
            exit 1
        fi
    done
    info "  All required Debian packages are installed."

    local runtime_missing=()
    for pkg in "${DEB_RUNTIME_PACKAGES[@]}"; do
        if [[ "$(dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null)" != "install ok installed" ]]; then
            runtime_missing+=("$pkg")
        fi
    done
    if (( ${#runtime_missing[@]} )); then
        warn "Session runtime package(s) missing (build continues):"
        echo "  sudo apt install ${runtime_missing[*]}"
    fi
}

# ---------------------------------------------------------------------------
# 1. Qt via aqtinstall (official qt.io binaries)
# ---------------------------------------------------------------------------
install_qt() {
    local modules=(qt5compat qtshadertools qtwaylandcompositor qtimageformats
                   qtmultimedia qtpositioning qttasktree qtspeech)
    local stamp="$QT_DIR/.kwe-modules"
    if [[ -x "$QT_DIR/bin/qmake" && "$(cat "$stamp" 2>/dev/null)" == "${modules[*]}" ]]; then
        info "Qt $QT_VERSION already present: $QT_DIR"
        return
    fi

    step "Installing Qt $QT_VERSION via aqtinstall..."
    local venv="$TOOLS_DIR/aqt-venv"
    if [[ ! -x "$venv/bin/aqt" ]]; then
        python3 -m venv "$venv"
        "$venv/bin/pip" install --quiet "aqtinstall==$AQTINSTALL_VERSION"
    fi

    # aqtinstall writes aqtinstall.log into the current directory.
    (cd "$TOOLS_DIR" && "$venv/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
        --outputdir "$KWE_PREFIX/qt" \
        --modules "${modules[@]}")
    echo "${modules[*]}" > "$stamp"
    info "  $QT_DIR"
}

# ---------------------------------------------------------------------------
# 2. KDE stack via kde-builder
# ---------------------------------------------------------------------------
setup_kde_builder() {
    local kb="$TOOLS_DIR/kde-builder"
    if [[ ! -d "$kb/.git" ]]; then
        step "Fetching kde-builder..."
        git clone --quiet https://invent.kde.org/sdk/kde-builder.git "$kb"
    fi
    git -C "$kb" fetch --quiet origin
    git -C "$kb" checkout --quiet "$KDE_BUILDER_REF"

    # Regenerated every run so pin changes in this script always apply.
    cat > "$KDE_BUILDER_RC" <<EOF
# Generated by build-deb-stable.sh - do not edit, changes are overwritten.
config-version: 2
global:
  include-dependencies: true
  source-dir: $KWE_PREFIX/src
  build-dir: $KWE_PREFIX/build
  install-dir: $KWE_PREFIX
  log-dir: $KWE_PREFIX/log
  qt-install-dir: $QT_DIR
  libname: lib64
  cmake-generator: Ninja
  cmake-options: >
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DCMAKE_INSTALL_LIBDIR=lib64
    -DBUILD_TESTING=OFF
    -DBUILD_QCH=OFF
    -DBUILD_PYTHON_BINDINGS=OFF
    -DCMAKE_PREFIX_PATH=$CMAKE_SEARCH_PATH
    -DCMAKE_LIBRARY_PATH=$KWE_PREFIX/lib64
  meson-options: --libdir=lib64 --buildtype=release
  num-cores: "$JOBS"
  num-cores-low-mem: "$(( JOBS > 1 ? JOBS / 2 : 1 ))"
  install-login-session: false
  hold-performance-profile: false
  stop-on-failure: true
  check-self-updates: false
  compile-commands-linking: false
  compile-commands-export: false
  directory-layout: flat

include \${build-configs-dir}/kde6.yaml: ""

# Not part of KDE's metadata; needed by kinetic-we (libdrm >= 2.4.127,
# libei >= 1.4 - Debian 13 has 2.4.124 / 1.3.901).
project libdrm:
  repository: https://gitlab.freedesktop.org/mesa/drm.git
  tag: "$LIBDRM_TAG"
  override-build-system: meson
  meson-options: -Dtests=false -Dman-pages=disabled -Dvalgrind=disabled -Dcairo-tests=disabled

project libei:
  repository: https://gitlab.freedesktop.org/libinput/libei.git
  tag: "$LIBEI_TAG"
  override-build-system: meson
  meson-options: -Dtests=disabled -Ddocumentation=[]

# Debian 13's Vulkan-Hpp (1.4.309) makes vk::raii return std::expected under
# C++23, which kinetic-we's structured bindings can't decompose; newer headers
# return vk::ResultValue unless VULKAN_HPP_USE_STD_EXPECTED is set.
project vulkan-headers:
  repository: https://github.com/KhronosGroup/Vulkan-Headers.git
  tag: "$VULKAN_HEADERS_TAG"
  cmake-options: -DVULKAN_HEADERS_ENABLE_MODULE=OFF

override kwe-frameworks:
  tag: "$KF6_TAG"
  use-projects:
    - frameworks/*

override kwe-plasma:
  tag: "$PLASMA_TAG"
  use-projects:
    - plasma/*

# "revision" takes precedence over the plasma/* tag above.
override kdecoration:
  revision: "$SPEC_KDECORATION_REF"

override kglobalacceld:
  revision: "$SPEC_KGA_REF"

override plasma-wayland-protocols:
  tag: "$PLASMA_WAYLAND_PROTOCOLS_TAG"

override wayland:
  tag: "$WAYLAND_TAG"

override wayland-protocols:
  tag: "$WAYLAND_PROTOCOLS_TAG"
  meson-options: -Dtests=false

override appstream:
  tag: "$APPSTREAM_TAG"

# Spectacle dependency outside frameworks/plasma: pin to a release.
override kquickimageeditor:
  tag: "$KQUICKIMAGEEDITOR_TAG"

# kquickimageeditor >= 0.7 needs Highway >= 1.3.0; Debian 13 has 1.2.0. Built
# static (PIC) so only kquickimageeditor links it and Debian's libhwy.so.1
# (used by libjxl) never meets a second copy in one process.
project highway:
  repository: https://github.com/google/highway.git
  tag: "$HIGHWAY_TAG"
  cmake-options: >
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DHWY_ENABLE_TESTS=OFF
    -DHWY_ENABLE_EXAMPLES=OFF
    -DHWY_ENABLE_CONTRIB=OFF

# Only the Qt6 style is wanted; BUILD_QT5 defaults to ON and needs Qt5.
override breeze:
  cmake-options: -DBUILD_QT5=OFF
EOF
}

build_kde_stack() {
    setup_kde_builder

    # kde-builder refuses a first clone pinned to a commit hash (it checks the
    # ref with "git ls-remote", which only knows branch/tag names), but checks
    # out a hash fine in an existing clone. So clone these two ourselves.
    local name
    for name in kdecoration kglobalacceld; do
        if [[ ! -d "$KWE_PREFIX/src/$name/.git" ]]; then
            git clone --quiet "https://invent.kde.org/plasma/$name.git" "$KWE_PREFIX/src/$name"
        fi
    done

    step "Building libdrm, libei, Vulkan-Headers and all kwin dependencies (kde-builder)..."
    info "  Logs: $KWE_PREFIX/log/latest"
    "$TOOLS_DIR/kde-builder/kde-builder" --rc-file "$KDE_BUILDER_RC" libdrm libei vulkan-headers
    # Not a kwin dependency, but needed by the portal (xdg-desktop-portal-kwe).
    "$TOOLS_DIR/kde-builder/kde-builder" --rc-file "$KDE_BUILDER_RC" kstatusnotifieritem
    # "kwin --stop-before kwin": build every dependency of kwin (it sorts
    # last), but not upstream kwin itself - we build kinetic-we from this
    # checkout instead. (--ignore-projects would drop kwin before its
    # dependencies are resolved.) kpipewire is an upstream-kwin dependency
    # that kinetic-we only uses for tests. (milou is needed: its QML module
    # is used at runtime by the overview effect.)
    "$TOOLS_DIR/kde-builder/kde-builder" --rc-file "$KDE_BUILDER_RC" \
        kwin --stop-before kwin --ignore-projects kpipewire
}

# Spectacle 6.7: Debian 13's Spectacle 6.3 checks for KWin's old "screenshot"
# effect, which KWin >= 6.5 no longer has (it is a plugin now), so it refuses
# to run on kinetic-we. zxing-cpp has no kde-builder recipe; prison uses
# Debian's libzxing-dev instead.
build_spectacle() {
    step "Building Spectacle and its dependencies (kde-builder)..."
    "$TOOLS_DIR/kde-builder/kde-builder" --rc-file "$KDE_BUILDER_RC" highway
    "$TOOLS_DIR/kde-builder/kde-builder" --rc-file "$KDE_BUILDER_RC" \
        spectacle --ignore-projects zxing-cpp
}

# ---------------------------------------------------------------------------
# 3. kinetic-we
# ---------------------------------------------------------------------------
build_kinetic_we() {
    local fresh="$1"
    prefix_env

    # build.ninja (not CMakeCache.txt): a failed configure leaves the cache.
    if [[ "$fresh" == "yes" || ! -f "$BUILD_DIR/build.ninja" ]]; then
        step "Configuring kinetic-we..."
        rm -rf "$BUILD_DIR"
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -G Ninja \
            -DCMAKE_INSTALL_PREFIX="$KWE_PREFIX" \
            -DCMAKE_PREFIX_PATH="$CMAKE_SEARCH_PATH" \
            -DCMAKE_LIBRARY_PATH="$KWE_PREFIX/lib64" \
            -DCMAKE_INSTALL_LIBDIR=lib64 \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_TESTING=OFF \
            -DKWIN_BUILD_GLOBALSHORTCUTS=ON \
            -DKWIN_BUILD_KCMS=OFF \
            -DCMAKE_DISABLE_FIND_PACKAGE_KF6DocTools=ON
    fi

    step "Building kinetic-we..."
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
    cmake --install "$BUILD_DIR"
}

# ---------------------------------------------------------------------------
# 4. Portal backend (portal/xdg-desktop-portal-kwe)
# ---------------------------------------------------------------------------
build_portal() {
    local fresh="$1"
    local build="$KWE_PREFIX/build/xdg-desktop-portal-kwe"
    prefix_env

    if [[ "$fresh" == "yes" || ! -f "$build/build.ninja" ]]; then
        step "Configuring xdg-desktop-portal-kwe..."
        rm -rf "$build"
        cmake -B "$build" -S "$SCRIPT_DIR/portal/xdg-desktop-portal-kwe" -G Ninja \
            -DCMAKE_INSTALL_PREFIX="$KWE_PREFIX" \
            -DCMAKE_PREFIX_PATH="$CMAKE_SEARCH_PATH" \
            -DCMAKE_LIBRARY_PATH="$KWE_PREFIX/lib64" \
            -DCMAKE_INSTALL_LIBDIR=lib64 \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_TESTING=OFF
    fi

    step "Building xdg-desktop-portal-kwe..."
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
}

# ---------------------------------------------------------------------------
# 5. Noctalia (shell/noctalia, Debian's own deps)
# ---------------------------------------------------------------------------
build_noctalia() {
    local fresh="$1"
    local build="$KWE_PREFIX/build/noctalia"
    prefix_env

    # Out-of-tree build dir: never write into the vendored subtree.
    if [[ "$fresh" == "yes" || ! -f "$build/build.ninja" ]]; then
        step "Configuring Noctalia..."
        rm -rf "$build"
        meson setup "$build" "$SCRIPT_DIR/shell/noctalia" \
            --prefix="$KWE_PREFIX" \
            --libdir=lib64 \
            --buildtype=debugoptimized \
            -Dtests=disabled \
            -Dcpp_link_args="-Wl,-rpath,$KWE_PREFIX/lib64"
    fi

    step "Building Noctalia..."
    meson compile -C "$build" -j "$JOBS"
    meson install -C "$build" --quiet
}

# ---------------------------------------------------------------------------
# 6. Session wiring
# ---------------------------------------------------------------------------
install_startup_script() {
    step "Installing dev startup script..."
    mkdir -p "$KWE_PREFIX/bin"
    # KWE_SCOPED_ENV=1: keep the prefix's Qt 6.11/KF6 paths out of the session
    # so Debian's own (Qt 6.8) KDE apps keep working; see start-kineticwe.sh.
    sed -e "s|@INSTALL_PREFIX@|$KWE_PREFIX|g" \
        -e '1a export KWE_SCOPED_ENV="${KWE_SCOPED_ENV:-1}"' \
        "$SCRIPT_DIR/scripts/start-kineticwe.sh" \
        > "$KWE_PREFIX/bin/start-kineticwe"
    chmod +x "$KWE_PREFIX/bin/start-kineticwe"
    info "  $KWE_PREFIX/bin/start-kineticwe"
    # The launcher's config setup helper (migrations, seeding, prune).
    install -Dm755 "$SCRIPT_DIR/scripts/kineticwe-config-setup" "$KWE_PREFIX/lib/kineticwe/kineticwe-config-setup"
    info "  $KWE_PREFIX/lib/kineticwe/kineticwe-config-setup"
    # The shell unit, rendered for this prefix; the launcher syncs it into the
    # user unit dir.
    mkdir -p "$KWE_PREFIX/share/kineticwe"
    sed -e "s|@INSTALL_PREFIX@|$KWE_PREFIX|g" "$SCRIPT_DIR/scripts/kineticwe-shell.service.in" \
        > "$KWE_PREFIX/share/kineticwe/kineticwe-shell.service"
    info "  $KWE_PREFIX/share/kineticwe/kineticwe-shell.service"
}

# kwe-run: start a prefix-built Qt app with the prefix's plugin/QML dirs. The
# session doesn't export them (KWE_SCOPED_ENV), so every entry point of such an
# app (desktop file, D-Bus service) goes through this wrapper.
install_kwe_run() {
    local lib="$KWE_PREFIX/lib64"
    cat > "$KWE_PREFIX/bin/kwe-run" <<EOF
#!/bin/sh
# Generated by build-deb-stable.sh
export QT_PLUGIN_PATH="$lib/plugins:$lib/qt6/plugins"
export QML_IMPORT_PATH="$lib/qml:$lib/qt6/qml"
export QML2_IMPORT_PATH="\$QML_IMPORT_PATH"
exec "\$@"
EOF
    chmod +x "$KWE_PREFIX/bin/kwe-run"
}

# Route Spectacle to the prefix build in KineticWE sessions only:
# - the prefix's desktop file (seen only by KineticWE, whose XDG_DATA_DIRS lists
#   the prefix first) launches through kwe-run;
# - D-Bus activation (DBusActivatable, used by the Print shortcut) finds user
#   service files first; they dispatch on XDG_CURRENT_DESKTOP so Plasma keeps
#   Debian's /usr/bin/spectacle.
wire_spectacle() {
    local desktop="$KWE_PREFIX/share/applications/org.kde.spectacle.desktop"
    if [[ ! -x "$KWE_PREFIX/bin/spectacle" || ! -f "$desktop" ]]; then
        warn "  Spectacle not installed in $KWE_PREFIX; skipping."
        return
    fi
    step "Wiring Spectacle..."
    # Idempotent: only rewrites Exec= lines that don't go through kwe-run yet.
    sed -i "s|^Exec=$KWE_PREFIX/bin/spectacle|Exec=$KWE_PREFIX/bin/kwe-run $KWE_PREFIX/bin/spectacle|" "$desktop"
    if grep -q "^Exec=$KWE_PREFIX/bin/spectacle" "$desktop" \
        || ! grep -q "^Exec=$KWE_PREFIX/bin/kwe-run " "$desktop"; then
        warn "  Could not route $desktop through kwe-run; check its Exec= lines."
    fi

    local dispatch="$KWE_PREFIX/bin/kwe-spectacle-dbus"
    cat > "$dispatch" <<EOF
#!/bin/sh
# Generated by build-deb-stable.sh: KineticWE -> prefix Spectacle 6.7,
# anything else (Plasma) -> Debian's Spectacle.
if [ "\$XDG_CURRENT_DESKTOP" = KineticWE ] && [ -x "$KWE_PREFIX/bin/spectacle" ]; then
    exec "$KWE_PREFIX/bin/kwe-run" "$KWE_PREFIX/bin/spectacle" "\$@"
fi
exec /usr/bin/spectacle "\$@"
EOF
    chmod +x "$dispatch"

    local dbus_dir="${XDG_DATA_HOME:-$HOME/.local/share}/dbus-1/services" name
    mkdir -p "$dbus_dir"
    for name in org.kde.Spectacle org.kde.spectacle; do
        rm -f "$dbus_dir/$name.service"
        printf '[D-BUS Service]\nName=%s\nExec=%s --dbus\n' "$name" "$dispatch" \
            > "$dbus_dir/$name.service"
        info "  $dbus_dir/$name.service"
    done
}

# Make the portal findable without root. D-Bus searches
# ~/.local/share/dbus-1/services (our copy of the service file starts the
# portal via kwe-run, since the session doesn't export the prefix's plugin/QML
# dirs - KWE_SCOPED_ENV), and xdg-desktop-portal reads
# ~/.config/xdg-desktop-portal/<desktop>-portals.conf, but Debian's
# xdg-desktop-portal 1.20 only loads .portal files from
# /usr/share/xdg-desktop-portal/portals, so that one is printed.
wire_portal() {
    step "Wiring xdg-desktop-portal-kwe..."
    local service="org.freedesktop.impl.portal.desktop.kwe.service"
    local dbus_dir="${XDG_DATA_HOME:-$HOME/.local/share}/dbus-1/services"
    local conf_dir="${XDG_CONFIG_HOME:-$HOME/.config}/xdg-desktop-portal"

    if [[ ! -f "$KWE_PREFIX/share/dbus-1/services/$service" ]]; then
        warn "  Portal not installed in $KWE_PREFIX; skipping."
        return
    fi
    mkdir -p "$dbus_dir" "$conf_dir"
    local exec_path
    exec_path=$(sed -n 's/^Exec=//p' "$KWE_PREFIX/share/dbus-1/services/$service")
    rm -f "$dbus_dir/$service"
    cat > "$dbus_dir/$service" <<EOF
[D-BUS Service]
Name=org.freedesktop.impl.portal.desktop.kwe
Exec=$KWE_PREFIX/bin/kwe-run $exec_path
EOF
    ln -sfn "$KWE_PREFIX/share/xdg-desktop-portal/kineticwe-portals.conf" \
        "$conf_dir/kineticwe-portals.conf"
    info "  $dbus_dir/$service"
    info "  $conf_dir/kineticwe-portals.conf"

    local portal_src="$KWE_PREFIX/share/xdg-desktop-portal/portals/kwe.portal"
    local portal_dst="/usr/share/xdg-desktop-portal/portals/kwe.portal"
    if [[ -f "$portal_dst" ]] && cmp -s "$portal_src" "$portal_dst"; then
        info "  $portal_dst (already up to date)"
    else
        warn "For screenshots/screen sharing via the portal, run once yourself:"
        echo
        echo "  sudo install -m0644 $portal_src $portal_dst"
        echo
    fi
}

# Display managers only scan system session dirs, so the entry is staged in
# the prefix and the (root) install command is printed instead of run.
stage_session_entry() {
    step "Staging 'KineticWE-Dev' login-screen session entry..."
    local staged="$KWE_PREFIX/share/wayland-sessions/$SESSION_DESKTOP"
    local target="$SESSION_DIR/$SESSION_DESKTOP"

    mkdir -p "$(dirname "$staged")"
    sed -e "s|@INSTALL_PREFIX@|$KWE_PREFIX|g" \
        "$SCRIPT_DIR/scripts/kineticwe-dev.desktop.in" > "$staged"

    if [[ -f "$target" ]] && cmp -s "$staged" "$target"; then
        info "  $target (already up to date)"
    else
        warn "To show 'KineticWE-Dev' on the login screen, run once yourself:"
        echo
        echo "  sudo install -Dm0644 $staged $target"
        echo
        info "Or start it from a TTY: $KWE_PREFIX/bin/start-kineticwe"
    fi

    # kscreenlocker authenticates via PAM service "kde"; without that file PAM
    # uses /etc/pam.d/other, which on Debian is the normal common-auth stack.
    if [[ ! -f /etc/pam.d/kde && ! -f /etc/pam.d/other ]]; then
        warn "No /etc/pam.d/kde or /etc/pam.d/other: the screen locker cannot unlock."
        warn "  Copy /etc/pam.d/login to /etc/pam.d/kde (as root)."
    fi
}

# ---------------------------------------------------------------------------
# Reset
# ---------------------------------------------------------------------------
reset_local() {
    case "$KWE_PREFIX" in
        ""|"/"|"$HOME"|"$HOME/"|"$HOME/.local"|"$HOME/.local/")
            error "Refusing to delete KWE_PREFIX=$KWE_PREFIX"
            exit 1 ;;
    esac

    warn "This deletes $KWE_PREFIX and $BUILD_DIR"
    read -r -p "Continue? [y/N] " reply
    [[ "$reply" =~ ^[Yy]$ ]] || { info "Aborted."; exit 0; }

    rm -rf "$KWE_PREFIX" "$BUILD_DIR"
    info "Removed $KWE_PREFIX and $BUILD_DIR"

    # User-level files pointing into the prefix (the D-Bus overrides would
    # otherwise break Plasma's Spectacle / portal activation).
    local dbus_dir="${XDG_DATA_HOME:-$HOME/.local/share}/dbus-1/services" f
    for f in org.freedesktop.impl.portal.desktop.kwe.service \
             org.kde.Spectacle.service org.kde.spectacle.service; do
        rm -f "$dbus_dir/$f"
    done
    rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/xdg-desktop-portal/kineticwe-portals.conf"
    info "Removed KineticWE D-Bus overrides and portal config link"
    if [[ -f "$SESSION_DIR/$SESSION_DESKTOP" ]]; then
        warn "Remove the login-screen entry yourself:"
        echo "  sudo rm -f $SESSION_DIR/$SESSION_DESKTOP"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
MODE="full"
SESSION="yes"
while (( $# )); do
    case "$1" in
        --deps-only)   MODE="deps" ;;
        --quick)       MODE="quick" ;;
        --reset-local) MODE="reset" ;;
        --no-session)  SESSION="no" ;;
        -h|--help)     usage; exit 0 ;;
        *) error "Unknown option: $1"; usage; exit 1 ;;
    esac
    shift
done

if [[ "$MODE" == "reset" ]]; then
    reset_local
    exit 0
fi

if [[ $EUID -eq 0 ]]; then
    error "Do not run this script as root; it builds into a user prefix."
    exit 1
fi

# Build with Debian's toolchain only: tools or libraries from e.g. Homebrew on
# PATH (perl without URI::Escape, foreign pkg-config/libs) break or silently
# contaminate the build.
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
unset PKG_CONFIG_PATH LD_LIBRARY_PATH CMAKE_PREFIX_PATH CMAKE_LIBRARY_PATH \
      CPATH LIBRARY_PATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH PYTHONPATH PERL5LIB

check_host
mkdir -p "$KWE_PREFIX" "$TOOLS_DIR"

case "$MODE" in
    full)
        install_qt
        build_kde_stack
        build_spectacle
        build_kinetic_we yes
        build_portal yes
        build_noctalia yes
        ;;
    deps)
        install_qt
        build_kde_stack
        build_spectacle
        exit 0
        ;;
    quick)
        if [[ ! -x "$QT_DIR/bin/qmake" ]]; then
            error "No Qt in $QT_DIR - run a full build first."
            exit 1
        fi
        build_kinetic_we no
        build_portal no
        build_noctalia no
        ;;
esac

install_startup_script
install_kwe_run
wire_portal
wire_spectacle
[[ "$SESSION" == "yes" ]] && stage_session_entry

info "Done. Start from a TTY with: $KWE_PREFIX/bin/start-kineticwe"
