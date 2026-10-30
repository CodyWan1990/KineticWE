#!/bin/bash
# =============================================================================
# KineticWE - Debian Dev Build Script (trixie + sid)
# =============================================================================
# Builds KineticWE from local source on Debian and installs it into an isolated
# user prefix. Two flavors:
#
# Debian 13 (trixie), default:
#   Debian 13 ships Qt 6.8 / KF6 6.13 / Plasma 6.3, which are too old for this
#   compositor (needs Qt >= 6.10, KF6 >= 6.27, Plasma libs >= 6.6.90,
#   wayland >= 1.24, libdrm >= 2.4.127), so the newer stack is built into the
#   prefix as well:
#
#     1. Qt            - official qt.io binaries via aqtinstall
#     2. KDE stack     - KF6 frameworks + Plasma libs + wayland/libdrm/libei via
#                        kde-builder (exact kwin dependency set, pinned tags)
#     3. kinetic-we    - this checkout
#     4. Portal        - portal/xdg-desktop-portal-kwe
#     5. Noctalia      - shell/noctalia (noctalia-kwe, incl. Kinetic Settings),
#                        against Debian's own packages
#     6. Session       - start-kineticwe + KineticWE-Dev session entry
#
# Debian sid (--sid), including sid-based distros such as PikaOS:
#   sid already ships Qt 6.11 / KF6 6.30 / Plasma 6.7, so only the two KDE
#   pieces the distro does not have yet are built from source into the prefix,
#   pinned to the same commits as the COPR package:
#
#     - kdecoration    - KDecoration3::Style (server-side drop shadows for CSD)
#     - kglobalacceld  - post-6.7 KGlobalAccelD input API
#
#   Everything else (Qt, KF6, Plasma, Spectacle, wayland, libdrm, libei) comes
#   from the distro, which makes a --sid build much faster. The distro versions
#   are checked first; if sid is older than what kinetic-we needs the script
#   stops and asks for an update (the default mode is tuned for Debian 13, not
#   for a lagging sid snapshot).
#
#   Pass --greeter (sid only) to also build the Noctalia login greeter from
#   source and make it your login screen: greetd is installed, /etc/greetd/
#   config.toml is written (backup kept), and greetd becomes the display
#   manager, replacing the current one. One confirmation prompt at the end;
#   --no-setup-greeter only prints the root steps instead. Trixie does not
#   package wlroots-0.20, which the greeter needs, so the option is ignored
#   there.
#
# Missing Debian packages are installed for you: check_host lists them, asks
# once on the TTY, and runs 'sudo apt-get install -y' (sudo still prompts for
# its password). Pass --no-install-deps to only print the apt command instead,
# which is also what happens when stdin is not a TTY. The remaining root steps
# (the login-screen session entry outside --greeter, the portal's .portal file)
# are printed for you to run yourself.
#
# Usage:
#   ./build-deb-stable.sh                 # Full trixie build (first run: 1-3 hours)
#   ./build-deb-stable.sh --sid           # Full sid build (uses the distro's Qt/KF6/Plasma)
#   ./build-deb-stable.sh --deps-only     # Only the trixie Qt + KDE stack / the sid pins
#   ./build-deb-stable.sh --quick         # Incremental kinetic-we + portal + Noctalia rebuild
#   ./build-deb-stable.sh --update        # Like --quick, but only components whose sources changed
#   ./build-deb-stable.sh --no-session    # Skip the session-entry hint
#   ./build-deb-stable.sh --no-install-deps  # Only print missing apt packages
#   ./build-deb-stable.sh --reset-local   # Delete KWE_PREFIX and the build dirs
#   ./build-deb-stable.sh --sid --greeter # Build + configure the login greeter (sid only)
#   ./build-deb-stable.sh --sid --greeter --no-setup-greeter  # Only print the setup steps
#
# Environment variables:
#   KWE_PREFIX   - Install prefix (default: $HOME/.local/kineticwe)
#   QT_VERSION   - Qt release fetched by aqtinstall (trixie only, default: 6.11.3)
#   JOBS         - Parallel build jobs (default: nproc)
#   NOCTALIA_GREETER_REPO - Git URL of the greeter fork (default: KineticWE fork)
#   NOCTALIA_GREETER_REF  - Branch/tag/commit to build (default: noctalia_greeter_ref from the spec)
#
# Incremental updates (--update):
#   A fingerprint per component (kinetic-we, portal, Noctalia, and the greeter
#   with --greeter) is stored in $KWE_PREFIX/share/kineticwe/deb-state. It
#   covers the component's source tree (uncommitted edits included, git blob
#   ids), the flavor, the KDE pins the component links against, and this script
#   itself, so a changed build flag also counts. Full and --quick builds record
#   fingerprints too; an install without a record is rebuilt once, then is
#   incremental. --update never touches the trixie Qt/kde-builder stack and, on
#   sid, only rebuilds kdecoration/kglobalacceld when their pinned revision in
#   the spec changed (use --deps-only to refresh the stack itself). A distro
#   Qt/KF6 upgrade is not source: after one, run --quick to relink everything.
#
# Switching flavors reuses the same prefix; run --reset-local first, or the
# sid build would pick up the trixie Qt/KF6 stack from the prefix.
# =============================================================================

set -euo pipefail

KWE_PREFIX="${KWE_PREFIX:-$HOME/.local/kineticwe}"
QT_VERSION="${QT_VERSION:-6.11.3}"
JOBS="${JOBS:-$(nproc)}"
# Native architecture, used to qualify every dpkg-query below. A Multi-Arch:
# same package installed for more than one architecture (PikaOS enables i386 for
# Steam and Proton) makes an unqualified query print one line per architecture,
# so "${Status}" becomes "install ok installedinstall ok installed" and never
# equals "install ok installed". "${Version}" is mangled the same way.
DPKG_ARCH="$(dpkg --print-architecture)"

# dpkg-query needs both forms handled:
#   * Multi-Arch: same packages installed for several architectures print one
#     line per architecture for an unqualified query ("install ok installedinstall
#     ok installed"), which never equals a single status;
#   * arch: all packages (meson, hwdata, docbook-*) do not answer the
#     architecture-qualified query at all.
# Try the native-arch query first, then fall back to the unqualified one.
pkg_installed() {
    dpkg-query -W -f='${Status}\n' "$1:${DPKG_ARCH}" 2>/dev/null \
        | grep -qx 'install ok installed' && return 0
    dpkg-query -W -f='${Status}\n' "$1" 2>/dev/null \
        | grep -qx 'install ok installed'
}

pkg_version() {
    local v
    v=$(dpkg-query -W -f='${Version}\n' "$1:${DPKG_ARCH}" 2>/dev/null | head -1)
    [[ -n "$v" ]] || v=$(dpkg-query -W -f='${Version}\n' "$1" 2>/dev/null | head -1)
    printf '%s' "$v"
}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPEC_FILE="${SCRIPT_DIR}/lgl-kineticwe-copr/kineticwe.spec"
# Flavor-dependent (set_flavor): trixie uses build-deb/, sid build-deb-sid/ so
# a stale cache from the other flavor is never picked up.
BUILD_DIR="${SCRIPT_DIR}/build-deb"
FLAVOR="trixie"

QT_DIR="${KWE_PREFIX}/qt/${QT_VERSION}/gcc_64"
TOOLS_DIR="${KWE_PREFIX}/tools"
KDE_BUILDER_RC="${KWE_PREFIX}/kde-builder.yaml"
# Set by set_flavor(): trixie adds the prefix Qt, sid uses the distro's Qt.
# (Debian's CMake never searches <prefix>/lib64 for packages - its platform
# file turns FIND_LIBRARY_USE_LIB64_PATHS off - so lib64/cmake is listed as its
# own prefix: find_package() also matches <prefix>/<Name>*/<Name>Config.cmake.)
CMAKE_SEARCH_PATH=""

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
SPEC_GREETER_REF=$(sed -n 's/.*%global noctalia_greeter_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)

# Noctalia login greeter (built only with --greeter, sid only). The greeter is
# not vendored here: fetch the KineticWE fork at the pinned COPR revision, or
# point NOCTALIA_GREETER_REPO/REF at another checkout.
NOCTALIA_GREETER_REPO="${NOCTALIA_GREETER_REPO:-https://gitlab.com/theblackdon/noctalia-greeter-kwe}"
NOCTALIA_GREETER_REPO="${NOCTALIA_GREETER_REPO%/}"
NOCTALIA_GREETER_REF="${NOCTALIA_GREETER_REF:-$SPEC_GREETER_REF}"
# Greeter packages: the compositor part needs wlroots-0.20, which sid packages
# as libwlroots-0.20-dev (trixie only has 0.18); greetd is the display manager
# the setup below configures. Both go through check_host's apt prompt.
GREETER_PACKAGES=(libwlroots-0.20-dev greetd)

# Login-screen session entry. /usr/local/share/wayland-sessions is searched by
# SDDM, GDM and the Noctalia greeter; installing there needs root, so it is
# only staged and printed unless the --greeter automatic setup installs it.
SESSION_DIR="/usr/local/share/wayland-sessions"
SESSION_DESKTOP="kineticwe-dev.desktop"

# Packages every flavor needs: toolchain, X11/Wayland/hardware libs, Noctalia's
# build deps, and everything the portal links against. The trixie flavor builds
# KF6/Plasma/kinetic-we against its own stack; the sid flavor gets those from
# the distro, see DEB_PACKAGES_SID.
DEB_PACKAGES_COMMON=(
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
    # Noctalia (shell/noctalia/meson.build). No md4c-html here: Noctalia only
    # uses md4c, and the html dev package is named differently per suite.
    libwayland-dev libwayland-bin wayland-protocols libsdbus-c++-dev
    libcairo2-dev libpango1.0-dev libharfbuzz-dev librsvg2-dev libglib2.0-dev
    libsecret-1-dev libsodium-dev libwireplumber-0.5-dev libqalculate-dev
    libxml2-dev libmd4c-dev nlohmann-json3-dev
    libtomlplusplus-dev libical-dev libjemalloc-dev libwebp-dev libjxl-dev
    libsndfile1-dev libstb-dev
    # portal: the distro Qt (sid) and qt.io's Qt (trixie) are built with CUPS,
    # so Qt::PrintSupportPrivate links Cups::Cups
    libcups2-dev
)

# Debian 13: the KDE stack is built from source, and Debian's Spectacle 6.3
# cannot talk to KWin >= 6.5, so Spectacle 6.7 and its OCR deps are needed too.
DEB_PACKAGES_TRIXIE=(
    libmd4c-html0-dev   # renamed to libmd4c-html-dev in sid
    libopencv-dev libtesseract-dev libzxing-dev libdmtx-dev
)

# Debian sid: Qt/KF6/Plasma come from the distro, so install the dev packages
# the KDE stack build used to provide. kdecoration and kglobalacceld are still
# built from source (build_kde_pins) but only need generic Qt6/KF6 devel plus
# ECM.
DEB_PACKAGES_SID=(
    libmd4c-html-dev
    extra-cmake-modules
    # Qt 6 (kinetic-we needs Qt6GuiPrivate; the portal Qt6PrintSupportPrivate)
    qt6-base-dev qt6-base-private-dev qt6-declarative-dev
    qt6-declarative-private-dev qt6-svg-dev qt6-wayland-dev
    qt6-wayland-private-dev qt6-5compat-dev qt6-tools-dev
    # KF6
    libkf6auth-dev libkf6colorscheme-dev libkf6config-dev
    libkf6coreaddons-dev libkf6crash-dev libkf6dbusaddons-dev
    libkf6globalaccel-dev libkf6guiaddons-dev libkf6i18n-dev
    libkf6iconthemes-dev libkf6idletime-dev libkf6kio-dev
    libkf6notifications-dev libkf6package-dev libkf6runner-dev
    libkf6service-dev libkf6statusnotifieritem-dev libkf6svg-dev
    libkf6widgetsaddons-dev libkf6windowsystem-dev
    # kglobalacceld master links KF6::JobWidgets
    libkf6jobwidgets-dev
    libkirigami-dev
    # Plasma 6.7 libs (CMake configs + QML runtime modules)
    kwayland-dev kscreenlocker-dev knighttime-dev
    libplasma-dev libplasmaactivities-dev
    # libeis-dev for Libeis-1.0 (input capture); it pulls libei-dev.
    libei-dev libeis-dev
    # Optional feature the trixie kde-builder stack enables: zoom effect
    # accessibility support (CMake skips it cleanly if absent).
    libqaccessibilityclient-qt6-dev
)

# Runtime-only packages the session uses from Debian (not built here):
#   qt6ct                 - the platform theme plugin behind
#                           QT_QPA_PLATFORMTHEME=qt6ct; without it no Qt app
#                           reads the Noctalia palette at all
#   breeze                - the Qt6 Breeze widget style (kde-style-breeze) that
#                           the seeded qt5ct/qt6ct configs select, plus cursor
#                           theme and Breeze color schemes
#   breeze-icon-theme     - the vendored icon theme declares
#                           Inherits=breeze,hicolor; without Breeze installed
#                           the inheritance chain dead-ends at hicolor and KDE
#                           apps show missing icons
#   xdg-desktop-portal    - the portal frontend the kwe backend registers
#                           with; also what qt6ct's
#                           standard_dialogs=xdgdesktopportal talks to
#   gtk-update-icon-cache - builds the icon-theme.cache for the vendored
#                           theme; no distro trigger runs for an unmanaged
#                           prefix, so KDE apps otherwise stat-walk ~9k files
#                           on every start
#   kde-spectacle         - the distro screenshot tool (sid's 6.7 build matches
#                           KWin 6.7; the trixie flavor builds its own instead)
#   tesseract-ocr-eng     - Spectacle's OCR language data
#   qml6-module-org-kde-* - QML modules the portal dialogs import; without
#   pipewire, wireplumber   org.kde.pipewire the ScreenCast picker cannot load,
#   rtkit                   PipeWire needs a session manager to hand out
#                           capture nodes, and rtkit provides the realtime
#                           scheduling xdg-desktop-portal looks for
#   pipewire-audio,       - PipeWire audio routing, Qt6 SVG image plugins, the
#   qt6-svg-plugins,        Adwaita GTK3 theme and the Noto fonts (core, CJK,
#   adw-gtk3-theme,         colour emoji) the session and its apps expect
#   fonts-noto-*
# Missing ones go through the same apt prompt as the build deps, but are
# optional: a package this suite/derivative does not ship only warns. Set by
# set_flavor().
DEB_RUNTIME_PACKAGES=()

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

# Compose the flavor-dependent settings once the flags are parsed.
set_flavor() {
    if [[ "$FLAVOR" == trixie ]]; then
        CMAKE_SEARCH_PATH="${QT_DIR};${KWE_PREFIX};${KWE_PREFIX}/lib64/cmake"
        BUILD_DIR="${SCRIPT_DIR}/build-deb"
        DEB_PACKAGES=("${DEB_PACKAGES_COMMON[@]}" "${DEB_PACKAGES_TRIXIE[@]}")
        DEB_RUNTIME_PACKAGES=(qt6ct tesseract-ocr-eng)
        return
    fi

    CMAKE_SEARCH_PATH="${KWE_PREFIX};${KWE_PREFIX}/lib64/cmake"
    BUILD_DIR="${SCRIPT_DIR}/build-deb-sid"
    DEB_PACKAGES=("${DEB_PACKAGES_COMMON[@]}" "${DEB_PACKAGES_SID[@]}")
    DEB_RUNTIME_PACKAGES=(
        qt6ct
        kde-spectacle
        breeze breeze-icon-theme
        xdg-desktop-portal
        gtk-update-icon-cache
        tesseract-ocr-eng
        # ScreenCast / RemoteDesktop / Screenshot portal. The backend's QML
        # imports org.kde.pipewire (stream preview) plus the Kirigami stack,
        # and PipeWire only hands out capture nodes when a session manager is
        # running. Missing any of these leaves the portal up but the screen
        # picker dialog dead.
        qml6-module-org-kde-pipewire
        qml6-module-org-kde-kirigami
        qml6-module-org-kde-ki18n
        qml6-module-org-kde-kitemmodels
        qml6-module-org-kde-kcmutils
        qml6-module-org-kde-iconthemes
        qml6-module-org-kde-kquickcontrols
        qml6-module-org-kde-desktop
        qml6-module-qt5compat-graphicaleffects
        qml6-module-qt-labs-platform
        qml6-module-qtquick-controls
        pipewire wireplumber
        # xdg-desktop-portal logs "Failed to load RealtimeKit property" on every
        # start without it; PipeWire capture wants realtime scheduling.
        rtkit
        # Desktop runtime extras: PipeWire's audio routing, Qt6's SVG image
        # plugins (SVG icons render blank without them), the Adwaita-for-GTK3
        # theme the GTK apps are themed with (Debian calls this package
        # adw-gtk3; PikaOS/Fedora call it adw-gtk3-theme), and the Noto fonts
        # (core, CJK, colour emoji) the shell and browsers expect.
        pipewire-audio
        qt6-svg-plugins
        adw-gtk3-theme
        fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji
    )
    if [[ "$GREETER" == "yes" ]]; then
        DEB_PACKAGES+=("${GREETER_PACKAGES[@]}")
    fi

    # A prefix built by the trixie flavor contains its own Qt/KF6; the sid build
    # would link against those instead of the distro's. Refuse early (except for
    # --reset-local, whose whole job is to get rid of that prefix).
    if [[ "$MODE" != "reset" ]] \
        && [[ -x "$QT_DIR/bin/qmake" || -d "$KWE_PREFIX/lib64/cmake/KF6CoreAddons" ]]; then
        error "$KWE_PREFIX already contains the trixie Qt/KF6 stack."
        error "Run '$0 --reset-local' first, or use a different KWE_PREFIX."
        exit 1
    fi
}

# Environment for everything built against the prefix (kinetic-we, Noctalia).
prefix_env() {
    if [[ "$FLAVOR" == trixie ]]; then
        export PATH="$KWE_PREFIX/bin:$QT_DIR/bin:$PATH"
    else
        export PATH="$KWE_PREFIX/bin:$PATH"
    fi
    export PKG_CONFIG_PATH="$KWE_PREFIX/lib64/pkgconfig:$KWE_PREFIX/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export CMAKE_PREFIX_PATH="$CMAKE_SEARCH_PATH"
    export CMAKE_LIBRARY_PATH="$KWE_PREFIX/lib64"
    export XDG_DATA_DIRS="$KWE_PREFIX/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    export QML_IMPORT_PATH="$KWE_PREFIX/lib64/qml"
    export QML2_IMPORT_PATH="$KWE_PREFIX/lib64/qml"
}

# Minimum distro versions for --sid. Mirrors the find_package/pkg-config
# requirements: Qt 6.10, KF6 6.30 (kconfig_compiler's enum-class kcfg fix),
# Plasma 6.6.90, wayland 1.24, wayland-protocols 1.48,
# plasma-wayland-protocols 1.21, libinput 1.28, libdrm 2.4.127, libei 1.4,
# libdisplay-info 0.2, pipewire 1.0.9.
# Epochs are stripped before comparing: Debian's KDE packages use a "4:" epoch,
# which would otherwise make an old 6.3 look newer than 6.6.90.
SID_MIN_VERSIONS=(
    "qt6-base-dev 6.10.0"
    "libkf6config-dev 6.30.0"
    "libplasma-dev 6.6.90"
    "kwayland-dev 6.6.90"
    "knighttime-dev 6.6.90"
    "kscreenlocker-dev 6.6.90"
    "libwayland-dev 1.24"
    "wayland-protocols 1.48"
    "plasma-wayland-protocols 1.21"
    "libinput-dev 1.28"
    "libdrm-dev 2.4.127"
    "libei-dev 1.4"
    "libeis-dev 1.4"
    "libdisplay-info-dev 0.2.0"
    "libpipewire-0.3-dev 1.0.9"
)

check_sid_versions() {
    local entry pkg want ver
    local too_old=()
    for entry in "${SID_MIN_VERSIONS[@]}"; do
        pkg="${entry%% *}"
        want="${entry##* }"
        ver=$(pkg_version "$pkg")
        [[ -n "$ver" ]] || continue
        dpkg --compare-versions "${ver#*:}" ge "$want" \
            || too_old+=("$pkg $ver (need >= $want)")
    done
    if (( ${#too_old[@]} )); then
        error "This system's KDE/Qt stack is too old for kinetic-we:"
        printf '  %s\n' "${too_old[@]}"
        echo
        error "Update the system (sid-based distros such as PikaOS sync with sid"
        error "periodically) and re-run. The full-stack mode without --sid is"
        error "tuned for Debian 13, so it is not a fallback on sid."
        exit 1
    fi
    info "  Distro Qt/KF6/Plasma stack is new enough for --sid."
}

# ---------------------------------------------------------------------------
# 0. Host check (missing apt packages are installed after a prompt)
# ---------------------------------------------------------------------------
# apt_install_missing required|optional <package>...
#
# Lists the missing packages, asks once on the TTY, and installs them with
# 'sudo apt-get install -y'. Names the package lists do not know at all are
# split off first, so one renamed package (adw-gtk3 vs adw-gtk3-theme) cannot
# make apt abort the whole batch. With --no-install-deps or a non-interactive
# stdin only the apt command is printed. "required" returns 1 when packages
# are still missing afterwards (the caller stops); "optional" only warns, so a
# package this suite/derivative does not ship never blocks a valid build.
apt_install_missing() {
    local severity="$1"; shift
    local -a wanted=("$@") known=() unknown=() still=()
    local pkg reply="" install="no"

    (( ${#wanted[@]} )) || return 0

    for pkg in "${wanted[@]}"; do
        if apt-cache show --no-all-versions "${pkg%%/*}" >/dev/null 2>&1; then
            known+=("$pkg")
        else
            unknown+=("$pkg")
        fi
    done
    if (( ${#unknown[@]} )); then
        warn "Not in the package lists, skipped: ${unknown[*]}"
        warn "  Run 'sudo apt-get update' if they should exist."
    fi

    if (( ${#known[@]} )); then
        warn "Missing ${#known[@]} Debian package(s):"
        echo
        echo "  ${known[*]}"
        echo

        if [[ "$INSTALL_DEPS" == "no" ]]; then
            error "Automatic installation disabled (--no-install-deps). Install them yourself:"
        elif [[ ! -t 0 || ! -t 1 ]]; then
            error "No TTY for the install prompt. Install them yourself:"
        elif ! command -v sudo >/dev/null; then
            error "sudo not found. Install them as root yourself:"
        elif read -r -p "Install them now with apt? [Y/n] " reply \
            && [[ "$reply" =~ ^([Yy]|)$ ]]; then
            install="yes"
        else
            error "Not installing. Install them yourself:"
        fi

        if [[ "$install" == "yes" ]]; then
            # apt prints its own progress; the status re-check below is the
            # source of truth, not apt's exit code.
            sudo apt-get install -y -- "${known[@]}" \
                || warn "apt-get did not finish cleanly; checking what is installed."
        else
            echo "  sudo apt install ${known[*]}"
            echo
        fi
    fi

    for pkg in "${wanted[@]}"; do
        pkg_installed "${pkg%%/*}" || still+=("$pkg")
    done
    if (( ${#still[@]} )); then
        if [[ "$severity" == required ]]; then
            error "Still missing: ${still[*]}"
            error "Update the package lists, install them, then re-run:"
            echo "  sudo apt-get update && sudo apt install ${still[*]}"
            echo
            return 1
        fi
        warn "Session runtime package(s) still missing (build continues):"
        echo "  sudo apt-get update && sudo apt install ${still[*]}"
        return 0
    fi
    if [[ "$install" == "yes" ]]; then
        info "  Installed ${#known[@]} package(s)."
    fi
    return 0
}

check_host() {
    step "Checking host..."

    if [[ ! -r /etc/debian_version ]]; then
        warn "This does not look like Debian; continuing anyway."
    fi
    if [[ -z "$SPEC_KGA_REF" || -z "$SPEC_KDECORATION_REF" ]]; then
        error "Could not read kglobalacceld_ref/kdecoration_ref from $SPEC_FILE"
        exit 1
    fi
    if [[ "$GREETER" == "yes" && -z "$NOCTALIA_GREETER_REF" ]]; then
        error "Could not resolve the greeter revision: set NOCTALIA_GREETER_REF"
        error "or add noctalia_greeter_ref to $SPEC_FILE."
        exit 1
    fi

    # Mesa from trixie-backports: libgbm-dev must match the installed libgbm1.
    # sid has no backports; libgbm-dev comes from the same suite as libgbm1.
    local gbm_bpo="no"
    if [[ "$FLAVOR" == trixie ]]; then
        [[ "$(pkg_version libgbm1)" == *bpo* ]] && gbm_bpo="yes"
    fi

    local missing=() pkg
    for pkg in "${DEB_PACKAGES[@]}"; do
        if ! pkg_installed "$pkg"; then
            [[ "$pkg" == libgbm-dev && "$gbm_bpo" == yes ]] && pkg="libgbm-dev/trixie-backports"
            missing+=("$pkg")
        fi
    done

    if (( ${#missing[@]} )); then
        apt_install_missing required "${missing[@]}" || exit 1
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

    if [[ "$FLAVOR" == sid ]]; then
        check_sid_versions
    fi

    local runtime_missing=()
    for pkg in "${DEB_RUNTIME_PACKAGES[@]}"; do
        if ! pkg_installed "$pkg"; then
            runtime_missing+=("$pkg")
        fi
    done
    if (( ${#runtime_missing[@]} )); then
        apt_install_missing optional "${runtime_missing[@]}"
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
# 2b. sid: kdecoration + kglobalacceld from the spec's pinned commits
# ---------------------------------------------------------------------------
# Debian sid already ships Qt 6.11 / KF6 6.30 / Plasma 6.7, but not the two
# post-6.7 KDE pieces kinetic-we needs, so build exactly those into the prefix.
# kdecoration master adds KDecoration3::Style; kglobalacceld master adds the
# keyEvent()/pointerPressed()/axisTriggered()/resetModifierOnlyState() input
# API the embedded daemon integration uses.
build_pinned_kde_repo() {
    local name="$1" ref="$2"
    local src="$KWE_PREFIX/src/$name"
    local build="$KWE_PREFIX/build/$name"
    local stamp="$build/.kwe-commit"
    local cmake_pkg

    case "$name" in
        kdecoration)   cmake_pkg=KDecoration3 ;;
        kglobalacceld) cmake_pkg=KGlobalAccelD ;;
        *) error "Unknown KDE pin: $name"; exit 1 ;;
    esac

    # Already installed at this exact revision: no fetch, no rebuild. That is
    # what keeps --update cheap; a changed pin still rebuilds (and, through the
    # compositor fingerprint, the compositor too).
    if [[ "$(cat "$stamp" 2>/dev/null)" == "$ref" \
        && -f "$KWE_PREFIX/lib64/cmake/$cmake_pkg/${cmake_pkg}Config.cmake" ]]; then
        info "  $name $ref already installed."
        return
    fi

    if [[ ! -d "$src/.git" ]]; then
        step "Fetching $name..."
        git clone --quiet "https://invent.kde.org/plasma/$name.git" "$src"
    fi
    git -C "$src" fetch --quiet origin
    if [[ "$(git -C "$src" rev-parse HEAD 2>/dev/null)" != "$ref" ]]; then
        step "Checking out $name $ref..."
        git -C "$src" checkout --quiet --force "$ref"
    fi

    # Reconfigure from scratch when the pin changes so no stale objects survive.
    if [[ "$(cat "$stamp" 2>/dev/null)" != "$ref" ]]; then
        step "Configuring $name..."
        rm -rf "$build"
        cmake -B "$build" -S "$src" -G Ninja \
            -DCMAKE_INSTALL_PREFIX="$KWE_PREFIX" \
            -DCMAKE_PREFIX_PATH="$CMAKE_SEARCH_PATH" \
            -DCMAKE_LIBRARY_PATH="$KWE_PREFIX/lib64" \
            -DCMAKE_INSTALL_LIBDIR=lib64 \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_TESTING=OFF
    fi

    step "Building $name..."
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    echo "$ref" > "$stamp"
}

build_kde_pins() {
    prefix_env
    step "Building kdecoration and kglobalacceld (sid pins)..."
    build_pinned_kde_repo kdecoration "$SPEC_KDECORATION_REF"
    build_pinned_kde_repo kglobalacceld "$SPEC_KGA_REF"
}

# ---------------------------------------------------------------------------
# 2c. Incremental update (--update): per-component source fingerprints
# ---------------------------------------------------------------------------
# Same model as install-arch.sh --update: a component is rebuilt only when its
# fingerprint differs from the one stored at its last successful install, when
# none is stored, or when it is no longer installed. Fingerprints are written
# after every build (full or quick), so the next --update has a baseline.
#
# A fingerprint covers the component's own files (git blob ids, so uncommitted
# edits count and deleted files drop out), the flavor, the pins it links
# against, and this script itself (the build flags live here). Nothing is ever
# written to the repository.
UPDATE_COMPONENTS=()
declare -A COMPONENT_LABEL=(
    [compositor]="kinetic-we"
    [portal]="xdg-desktop-portal-kwe"
    [shell]="Noctalia"
    [greeter]="Noctalia greeter"
)
DO_UPDATE="no"
# Filled by plan_update: rebuild reason per component ("" = up to date).
declare -A UPDATE_REASON=()

STATE_DIR="$KWE_PREFIX/share/kineticwe/deb-state"
# sha256 of this script, so a changed build flag invalidates the fingerprints.
SCRIPT_FINGERPRINT=""
if [[ -r "$0" ]]; then
    SCRIPT_FINGERPRINT=$(sha256sum -- "$0")
    SCRIPT_FINGERPRINT="${SCRIPT_FINGERPRINT%% *}"
else
    warn "Cannot read $0; --update cannot detect changes to this script's build flags."
fi

# component_pathspecs <component>: git pathspecs of the component's own files.
# The compositor is "everything else" so a new top-level file is never missed;
# only paths that cannot change any installed component are excluded.
component_pathspecs() {
    case "$1" in
        compositor)
            printf '%s\n' . \
                ':(exclude)shell' ':(exclude)portal' ':(exclude)docs' ':(exclude)ci' \
                ':(exclude)debian' ':(exclude)nix' ':(exclude)arch' \
                ':(exclude)lgl-kineticwe-copr' ':(exclude)scripts' \
                ':(glob,exclude)flake.*' ':(glob,exclude)build-*.sh' \
                ':(glob,exclude)*-deps.sh' ':(exclude)install-arch.sh' \
                ':(exclude)PKGBUILD' ':(glob,exclude)*.install' \
                ':(exclude)AGENTS.md' ':(exclude)CLAUDE.md' ':(exclude).gitlab-ci.yml'
            ;;
        portal) printf '%s\n' portal/xdg-desktop-portal-kwe ;;
        shell)  printf '%s\n' shell/noctalia ;;
        greeter) : ;;   # not vendored: fingerprinted by its pinned ref only
        *) error "Unknown component: $1"; exit 1 ;;
    esac
}

# source_fingerprint_lines <pathspec>...: one "<blob id> <path>" line per file.
source_fingerprint_lines() {
    local f p line blob
    local -A dirty=()
    while IFS= read -r -d '' f; do
        dirty["$f"]=1
    done < <(git -C "$SCRIPT_DIR" diff-files --name-only -z -- "$@"
             git -C "$SCRIPT_DIR" ls-files -o --exclude-standard -z -- "$@")

    while IFS= read -r -d '' line; do
        f="${line#*$'\t'}"
        [[ -z "${dirty[$f]:-}" ]] || continue
        blob="${line#* }"
        printf '%s %s\n' "${blob%% *}" "$f"
    done < <(git -C "$SCRIPT_DIR" ls-files -s -z -- "$@")

    for f in "${!dirty[@]}"; do
        p="$SCRIPT_DIR/$f"
        if [[ -L "$p" ]]; then
            blob=$(readlink -- "$p" | tr -d '\n' | git -C "$SCRIPT_DIR" hash-object --stdin)
        elif [[ -f "$p" ]]; then
            blob=$(git -C "$SCRIPT_DIR" hash-object -- "$p")
        else
            continue    # deleted in the working tree: not part of the build
        fi
        printf '%s %s\n' "$blob" "$f"
    done
}

# component_fingerprint <component>: sha256 over the flavor, this script, the
# component's pinned refs and its sorted file list.
component_fingerprint() {
    local component="$1" sum
    local -a specs=()
    mapfile -t specs < <(component_pathspecs "$component")
    sum=$(
        {
            printf 'flavor %s\nscript %s\n' "$FLAVOR" "$SCRIPT_FINGERPRINT"
            case "$component" in
                compositor)
                    printf 'refs kdecoration %s kglobalacceld %s\n' \
                        "$SPEC_KDECORATION_REF" "$SPEC_KGA_REF"
                    ;;
                greeter)
                    printf 'ref greeter %s\n' "$NOCTALIA_GREETER_REF"
                    ;;
            esac
            if (( ${#specs[@]} )); then
                source_fingerprint_lines "${specs[@]}" | LC_ALL=C sort
            fi
        } | sha256sum
    )
    printf '%s\n' "${sum%% *}"
}

fingerprint_load() {
    local file="$STATE_DIR/$1.fingerprint"
    [[ -f "$file" && ! -L "$file" ]] || return 0
    head -n 1 -- "$file"
}

# fingerprint_store <component>: recorded after every successful install, with
# or without --update, so the next --update has a baseline.
fingerprint_store() {
    local component="$1" file="$STATE_DIR/$1.fingerprint" tmp
    mkdir -p -- "$STATE_DIR"
    if [[ -L "$file" ]]; then
        warn "Symlinked fingerprint record ignored: $file"
        return
    fi
    tmp=$(mktemp "$STATE_DIR/.fingerprint.XXXXXXXX")
    component_fingerprint "$component" > "$tmp"
    mv -f -- "$tmp" "$file"
}

# The portal installs its binary outside bin/; read its path from the installed
# D-Bus service file instead of assuming libexec's exact layout.
portal_backend_path() {
    local service="$KWE_PREFIX/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kwe.service"
    [[ -f "$service" ]] || return 0
    sed -n 's/^Exec=//p' "$service" | head -1
}

component_installed() {
    local exec_path
    case "$1" in
        compositor) [[ -x "$KWE_PREFIX/bin/kinetic-we" ]] ;;
        portal)
            exec_path=$(portal_backend_path)
            [[ -n "$exec_path" && -x "$exec_path" ]]
            ;;
        shell)   [[ -x "$KWE_PREFIX/bin/noctalia-kwe" ]] ;;
        greeter) [[ -x "$KWE_PREFIX/bin/noctalia-greeter-session" ]] ;;
        *)       return 1 ;;
    esac
}

# plan_update: read-only. Fills UPDATE_COMPONENTS and UPDATE_REASON.
plan_update() {
    local component stored fp
    UPDATE_COMPONENTS=(compositor portal shell)
    [[ "$GREETER" == "yes" ]] && UPDATE_COMPONENTS+=(greeter)
    UPDATE_REASON=()
    for component in "${UPDATE_COMPONENTS[@]}"; do
        if ! component_installed "$component"; then
            UPDATE_REASON[$component]="not installed"
            continue
        fi
        stored=$(fingerprint_load "$component")
        if [[ -z "$stored" ]]; then
            UPDATE_REASON[$component]="no record of the last build"
            continue
        fi
        fp=$(component_fingerprint "$component")
        if [[ "$stored" != "$fp" ]]; then
            UPDATE_REASON[$component]="source changed"
        else
            UPDATE_REASON[$component]=""
        fi
    done
}

# component_planned <component>: true when the component will be built.
component_planned() {
    [[ "$DO_UPDATE" != "yes" ]] && return 0
    [[ -n "${UPDATE_REASON[$1]:-}" ]]
}

print_update_plan() {
    local component
    step "Incremental update plan (--update):"
    for component in "${UPDATE_COMPONENTS[@]}"; do
        if [[ -n "${UPDATE_REASON[$component]:-}" ]]; then
            info "  ${COMPONENT_LABEL[$component]}: rebuild (${UPDATE_REASON[$component]})"
        else
            info "  ${COMPONENT_LABEL[$component]}: up to date"
        fi
    done
}

print_update_result() {
    local component
    local -a rebuilt=() skipped=()
    for component in "${UPDATE_COMPONENTS[@]}"; do
        if [[ -n "${UPDATE_REASON[$component]:-}" ]]; then
            rebuilt+=("${COMPONENT_LABEL[$component]}")
        else
            skipped+=("${COMPONENT_LABEL[$component]}")
        fi
    done
    info "Rebuilt: ${rebuilt[*]:-nothing}"
    info "Up to date: ${skipped[*]:-nothing}"
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
    fingerprint_store compositor
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
    fingerprint_store portal
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
    fingerprint_store shell
}

# ---------------------------------------------------------------------------
# 6. Noctalia greeter (sid only, opt-in via --greeter)
# ---------------------------------------------------------------------------
build_noctalia_greeter() {
    local src="$KWE_PREFIX/build/noctalia-greeter-src"
    local build="$KWE_PREFIX/build/noctalia-greeter"
    prefix_env

    if [[ -z "$NOCTALIA_GREETER_REF" ]]; then
        error "No greeter revision: set NOCTALIA_GREETER_REF or add noctalia_greeter_ref to $SPEC_FILE."
        exit 1
    fi

    # The greeter is not vendored: cache the source per ref so --quick reuses
    # it, and re-download (plus clean build) only when the ref changes.
    if [[ ! -f "$src/meson.build" || "$(cat "$src/.kwe-ref" 2>/dev/null || true)" != "$NOCTALIA_GREETER_REF" ]]; then
        step "Fetching noctalia-greeter ${NOCTALIA_GREETER_REF}..."
        rm -rf "$src" "$build"
        mkdir -p "$src" "$KWE_PREFIX/build"
        local archive
        archive=$(mktemp "$KWE_PREFIX/build/noctalia-greeter.XXXXXX")
        curl --fail --location --proto '=https' \
            "$NOCTALIA_GREETER_REPO/-/archive/$NOCTALIA_GREETER_REF/noctalia-greeter-kwe-$NOCTALIA_GREETER_REF.tar.gz" \
            -o "$archive"
        tar xzf "$archive" -C "$src" --strip-components=1
        rm -f "$archive"
        if [[ ! -f "$src/meson.build" ]]; then
            error "Incomplete noctalia-greeter source for $NOCTALIA_GREETER_REF."
            exit 1
        fi
        printf '%s\n' "$NOCTALIA_GREETER_REF" > "$src/.kwe-ref"
    fi

    if [[ ! -f "$build/build.ninja" ]]; then
        step "Configuring noctalia-greeter..."
        rm -rf "$build"
        meson setup "$build" "$src" \
            --prefix="$KWE_PREFIX" \
            --buildtype=release
    fi

    step "Building noctalia-greeter..."
    meson compile -C "$build" -j "$JOBS"
    meson install -C "$build" --quiet

    if [[ ! -x "$KWE_PREFIX/bin/noctalia-greeter-session" ]]; then
        error "noctalia-greeter-session was not installed in $KWE_PREFIX/bin."
        exit 1
    fi
    fingerprint_store greeter
    info "  $KWE_PREFIX/bin/noctalia-greeter-session"
}

# ---------------------------------------------------------------------------
# 7. Session wiring
# ---------------------------------------------------------------------------
install_startup_script() {
    step "Installing dev startup script..."
    mkdir -p "$KWE_PREFIX/bin"
    # KWE_SCOPED_ENV=1 (trixie): keep the prefix's Qt 6.11/KF6 paths out of the
    # session so Debian's own (Qt 6.8) KDE apps keep working. sid uses the
    # distro's Qt/KF6, so the session must see prefix/lib64 for kdecoration,
    # kglobalacceld and kinetic-we's own libraries -> 0.
    local scoped="1"
    [[ "$FLAVOR" == sid ]] && scoped="0"
    sed -e "s|@INSTALL_PREFIX@|$KWE_PREFIX|g" \
        -e "1a export KWE_SCOPED_ENV=\"\${KWE_SCOPED_ENV:-$scoped}\"" \
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

    # Smoke-test the backend before D-Bus ever touches it. A binary built in a
    # different prefix (or by the other flavor) links against a Qt this host
    # does not have; the only symptoms are "exited with status 1" in the
    # journal and a portal that silently exports no ScreenCast interface.
    # ldd -r reports the versioned-symbol mismatch behind that.
    local ldd_err=""
    if [[ -x "$exec_path" ]]; then
        local ldd_libdir="$KWE_PREFIX/lib64"
        [[ "$FLAVOR" == trixie ]] && ldd_libdir="$ldd_libdir:$QT_DIR/lib"
        ldd_err=$(LD_LIBRARY_PATH="$ldd_libdir" ldd -r "$exec_path" 2>&1 \
            | grep -E 'undefined symbol|not found' | head -3 || true)
    else
        warn "  Portal backend missing: $exec_path"
    fi

    if [[ -n "$ldd_err" ]]; then
        error "  Portal backend cannot be loaded by the dynamic linker:"
        printf '%s\n' "$ldd_err" | sed 's/^/    /'
        error "  Screen sharing/remote desktop will not work until the portal"
        error "  is rebuilt for this prefix (full build, or --reset-local first)."
    fi

    local portal_src="$KWE_PREFIX/share/xdg-desktop-portal/portals/kwe.portal"
    local portal_dst="/usr/share/xdg-desktop-portal/portals/kwe.portal"
    local portal_stale="no"
    if [[ -f "$portal_dst" ]] && cmp -s "$portal_src" "$portal_dst"; then
        info "  $portal_dst (already up to date)"
    else
        portal_stale="yes"
        warn "For screenshots/screen sharing via the portal, run once yourself:"
        echo
        echo "  sudo install -m0644 $portal_src $portal_dst"
        echo
    fi

    if [[ -z "$ldd_err" && -x "$exec_path" ]]; then
        # The frontend creates its backend proxies once at startup and keeps the
        # failed ones, so rewriting the service file alone does nothing for an
        # already-running session. Restart last, once the wiring is in place;
        # the frontend also re-reads the .portal files at startup.
        if systemctl --user is-active --quiet xdg-desktop-portal.service 2>/dev/null; then
            if systemctl --user try-restart xdg-desktop-portal.service 2>/dev/null; then
                info "  Restarted xdg-desktop-portal.service"
                if [[ "$portal_stale" == yes ]]; then
                    warn "  Restart it again after the sudo install above."
                fi
            else
                warn "  Could not restart the portal; run it yourself:"
                echo "    systemctl --user restart xdg-desktop-portal.service"
            fi
        fi
        info "  Verify ScreenCast with:"
        echo "    gdbus call --session -d org.freedesktop.portal.Desktop \\"
        echo "      -o /org/freedesktop/portal/desktop \\"
        echo "      -m org.freedesktop.DBus.Properties.Get \\"
        echo "      org.freedesktop.portal.ScreenCast version"
    fi
}

# Display managers only scan system session dirs, so the entry is staged in
# the prefix. auto=yes (the --greeter automatic setup) installs it with sudo;
# otherwise the (root) install command is printed instead of run.
stage_session_entry() {
    local auto="${1:-no}"
    step "Staging 'KineticWE-Dev' login-screen session entry..."
    local staged="$KWE_PREFIX/share/wayland-sessions/$SESSION_DESKTOP"
    local target="$SESSION_DIR/$SESSION_DESKTOP"

    mkdir -p "$(dirname "$staged")"
    sed -e "s|@INSTALL_PREFIX@|$KWE_PREFIX|g" \
        "$SCRIPT_DIR/scripts/kineticwe-dev.desktop.in" > "$staged"

    if [[ -f "$target" ]] && cmp -s "$staged" "$target"; then
        info "  $target (already up to date)"
    elif [[ "$auto" == "yes" ]]; then
        step "Installing 'KineticWE-Dev' login-screen session entry..."
        if sudo install -Dm0644 "$staged" "$target"; then
            info "  $target"
        else
            warn "Could not install $target. Install it yourself:"
            echo "  sudo install -Dm0644 $staged $target"
        fi
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

# greetd runs the greeter as its config `user`; Debian's greetd package uses
# "_greetd". Read the value greetd would use, then fall back to whichever
# conventional greeter account exists.
resolve_greeter_user() {
    local cfg="/etc/greetd/config.toml"
    local user=""
    if [[ -f "$cfg" ]]; then
        user=$(awk '
            /^\[default_session\]/  { in_block=1; next }
            /^\[/                   { in_block=0 }
            in_block && /^[[:space:]]*user[[:space:]]*=/ {
                sub(/^[[:space:]]*user[[:space:]]*=[[:space:]]*"/, "")
                sub(/".*$/, "")
                print; exit
            }
        ' "$cfg" 2>/dev/null || true)
    fi
    if [[ -z "$user" ]]; then
        local candidate
        for candidate in _greetd greetd greeter; do
            if id "$candidate" &>/dev/null; then
                user="$candidate"
                break
            fi
        done
    fi
    printf '%s\n' "$user"
}

# Print the root steps when the automatic setup is skipped (--no-setup-greeter
# or a non-interactive run). The greeter's own setup helper patches PAM,
# creates /var/lib/noctalia-greeter and installs greeter.toml; it then prints
# the greetd config block. The display-manager switch is always manual here.
stage_greeter_setup() {
    step "Staging Noctalia greeter login setup..."
    local session_bin="$KWE_PREFIX/bin/noctalia-greeter-session"
    local apply_appearance="$KWE_PREFIX/bin/noctalia-greeter-apply-appearance"
    local setup_script="$KWE_PREFIX/share/noctalia-greeter/setup_greeter_system.sh"
    local policy="$KWE_PREFIX/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy"
    local policy_target="/usr/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy"
    local tmpfiles="$KWE_PREFIX/lib/tmpfiles.d/noctalia-greeter.conf"
    local greeter_user current_dm
    greeter_user=$(resolve_greeter_user)
    current_dm=$(readlink -f /etc/systemd/system/display-manager.service 2>/dev/null || true)

    if [[ ! -x "$setup_script" ]]; then
        warn "  $setup_script not found; skipping the greetd setup hint."
        return
    fi

    warn "The greeter runs from $KWE_PREFIX, so greetd needs root setup. Run once:"
    echo
    if ! command -v greetd >/dev/null; then
        echo "  sudo apt install greetd"
    fi
    if [[ -f "$policy" ]]; then
        # polkit only reads /usr/share/polkit-1/actions; the prefix policy's
        # Exec path still points at this prefix.
        echo "  sudo install -Dm0644 \"$policy\" \"$policy_target\""
    fi
    if [[ -f "$tmpfiles" ]]; then
        echo "  sudo install -Dm0644 \"$tmpfiles\" /etc/tmpfiles.d/noctalia-greeter.conf"
    fi
    printf '  sudo env%s NOCTALIA_GREETER_SESSION_BIN="%s" NOCTALIA_GREETER_APPLY_APPEARANCE="%s" "%s"\n' \
        "${greeter_user:+ GREETER_USER=$greeter_user}" \
        "$session_bin" "$apply_appearance" "$setup_script"
    echo
    echo "Then make greetd the display manager:"
    echo "  sudo systemctl set-default graphical.target"
    if [[ "$current_dm" != */greetd.service ]]; then
        if [[ -n "$current_dm" && "$current_dm" != /dev/null ]]; then
            echo "  sudo systemctl disable $(basename "$current_dm")"
        fi
        echo "  sudo systemctl enable greetd.service"
        echo "  sudo ln -sfn /usr/lib/systemd/system/greetd.service \\"
        echo "    /etc/systemd/system/display-manager.service"
    fi
    echo
    info "  The setup script prints the /etc/greetd/config.toml block to paste;"
    info "  save the old file first, then reboot. Make sure the 'KineticWE-Dev'"
    info "  session entry above is installed, or the greeter has nothing to offer."
}

# Decide once, at the end of the build, whether the greeter setup is applied.
# Sets GREETER_AUTO; the actual root work happens in apply_greeter_setup().
# Asking here (not before the build) keeps a long build from blocking on a
# prompt, and a declined/non-TTY run falls back to the printed steps.
ask_greeter_setup() {
    GREETER_AUTO="no"
    [[ "$SETUP_GREETER" == "yes" ]] || return 0

    if [[ ! -t 0 || ! -t 1 ]]; then
        warn "No TTY: printing the greeter setup commands instead."
        return 0
    fi
    if ! command -v sudo >/dev/null; then
        warn "sudo not found: printing the greeter setup commands instead."
        return 0
    fi

    warn "The greeter needs root setup: greetd, /etc/greetd/config.toml (backup"
    warn "kept), and greetd as the display manager, replacing the current one."
    local reply=""
    if read -r -p "Apply the greeter setup now? [Y/n] " reply \
        && [[ "$reply" =~ ^([Yy]|)$ ]]; then
        GREETER_AUTO="yes"
    else
        info "Skipping the automatic greeter setup."
    fi
}

# write_greetd_config <session-bin> <greeter-user>: point [default_session] at
# the prefix wrapper, preserving every other key and section. A .bak copy of
# the previous file is kept; an already-correct file is left untouched.
write_greetd_config() {
    local session_bin="$1" greeter_user="$2"
    local cfg="/etc/greetd/config.toml" tmp base
    tmp=$(mktemp) || { warn "Could not create a temporary file."; return 1; }
    base="$tmp.base"

    if [[ -f "$cfg" ]]; then
        if ! cp -- "$cfg" "$base" 2>/dev/null; then
            warn "Cannot read $cfg; leaving it untouched."
            rm -f "$tmp" "$base"
            return 1
        fi
    else
        cat > "$base" <<EOF
[terminal]
vt = 1

[default_session]
command = "$session_bin"
user = "$greeter_user"
EOF
    fi

    if grep -q '^\[default_session\]' "$base"; then
        # Same merge as build-rpm-dev.sh: replace command/user inside the
        # block, leave comments and other keys alone, and add a missing key
        # rather than leaving an unusable [default_session] behind.
        awk -v session="$session_bin" -v guser="$greeter_user" '
            function close_block() {
                if (!seen_cmd) print "command = \"" session "\""
                if (!seen_user) print "user = \"" guser "\""
                in_block = 0
            }
            /^\[default_session\]/ {
                in_block = 1; seen_cmd = 0; seen_user = 0; print; next
            }
            /^\[/ { if (in_block) close_block() }
            in_block && /^[[:space:]]*command[[:space:]]*=/ {
                print "command = \"" session "\""; seen_cmd = 1; next
            }
            in_block && /^[[:space:]]*user[[:space:]]*=/ {
                print "user = \"" guser "\""; seen_user = 1; next
            }
            { print }
            END { if (in_block) close_block() }
        ' "$base" > "$tmp"
    else
        cat -- "$base" > "$tmp"
        printf '\n[default_session]\ncommand = "%s"\nuser = "%s"\n' \
            "$session_bin" "$greeter_user" >> "$tmp"
    fi

    if [[ -f "$cfg" ]] && cmp -s "$tmp" "$cfg"; then
        info "  $cfg (already up to date)"
    else
        if [[ -f "$cfg" ]] && ! sudo cp -a -- "$cfg" "$cfg.bak"; then
            warn "Could not back up $cfg; not changing it."
            rm -f "$tmp" "$base"
            return 1
        fi
        if sudo install -Dm0644 "$tmp" "$cfg"; then
            info "  $cfg"
        else
            warn "Could not write $cfg; run the greeter setup manually."
            rm -f "$tmp" "$base"
            return 1
        fi
    fi
    rm -f "$tmp" "$base"
}

# Apply the greeter's root setup. Every step is idempotent and guarded, so a
# failure warns instead of discarding the already-built prefix.
apply_greeter_setup() {
    step "Configuring the Noctalia greeter (greetd)..."
    local session_bin="$KWE_PREFIX/bin/noctalia-greeter-session"
    local apply_bin="$KWE_PREFIX/bin/noctalia-greeter-apply-appearance"
    local setup_script="$KWE_PREFIX/share/noctalia-greeter/setup_greeter_system.sh"
    local policy="$KWE_PREFIX/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy"
    local policy_target="/usr/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy"
    local tmpfiles_target="/etc/tmpfiles.d/noctalia-greeter.conf"
    local dm_link="/etc/systemd/system/display-manager.service"
    local greeter_user="" nologin grp current_dm unit dm tmpfiles

    if ! command -v greetd >/dev/null; then
        warn "  greetd is missing; printing the setup steps instead."
        stage_greeter_setup
        return
    fi
    if [[ ! -x "$session_bin" || ! -x "$apply_bin" || ! -x "$setup_script" ]]; then
        warn "  Greeter binaries/scripts are missing in $KWE_PREFIX; skipping."
        stage_greeter_setup
        return
    fi

    greeter_user=$(resolve_greeter_user)
    [[ -n "$greeter_user" ]] || greeter_user="_greetd"
    info "  Greeter user: $greeter_user"

    if ! id "$greeter_user" >/dev/null 2>&1; then
        nologin=/usr/sbin/nologin
        [[ -x "$nologin" ]] || nologin=/bin/false
        step "Creating the greeter user '$greeter_user'..."
        sudo useradd -r -s "$nologin" -d /var/lib/noctalia-greeter "$greeter_user" \
            || warn "  Could not create '$greeter_user'; the greeter may not start."
    fi

    for grp in video input; do
        getent group "$grp" >/dev/null 2>&1 || continue
        [[ " $(id -Gn "$greeter_user" 2>/dev/null || true) " == *" $grp "* ]] && continue
        sudo usermod -a -G "$grp" "$greeter_user" \
            || warn "  Could not add '$greeter_user' to '$grp'."
    done

    if [[ -f "$policy" ]]; then
        if [[ -f "$policy_target" ]] && cmp -s "$policy" "$policy_target"; then
            info "  $policy_target (already up to date)"
        elif sudo install -Dm0644 "$policy" "$policy_target"; then
            info "  $policy_target"
        else
            warn "  Could not install $policy_target."
        fi
    fi

    # tmpfiles recreates and chowns the state dir on boot. Generate the line
    # for the resolved user instead of copying the prefix's hardcoded _greetd
    # file; /etc/tmpfiles.d is the admin-scanned location.
    tmpfiles=$(mktemp) || tmpfiles=""
    if [[ -n "$tmpfiles" ]]; then
        printf 'd /var/lib/noctalia-greeter 0750 %s %s -\n' \
            "$greeter_user" "$greeter_user" > "$tmpfiles"
        if [[ -f "$tmpfiles_target" ]] && cmp -s "$tmpfiles" "$tmpfiles_target"; then
            info "  $tmpfiles_target (already up to date)"
        elif sudo install -Dm0644 "$tmpfiles" "$tmpfiles_target"; then
            info "  $tmpfiles_target"
        else
            warn "  Could not install $tmpfiles_target."
        fi
        rm -f "$tmpfiles"
    fi

    # The helper patches PAM, creates/chowns the state dir and writes
    # greeter.toml. On success its output (including the copy/paste greetd
    # block, which this script writes itself) is noise; on failure it is the
    # diagnosis, so keep it and print it.
    step "Running the greeter system setup (PAM, state dir, greeter.toml)..."
    local setup_log
    setup_log=$(mktemp) || setup_log=""
    if [[ -n "$setup_log" ]]; then
        if sudo env \
            GREETER_USER="$greeter_user" \
            NOCTALIA_GREETER_SESSION_BIN="$session_bin" \
            NOCTALIA_GREETER_APPLY_APPEARANCE="$apply_bin" \
            "$setup_script" >"$setup_log" 2>&1; then
            info "  PAM, state dir and greeter.toml are set up."
        else
            warn "  Greeter system setup failed:"
            sed 's/^/    /' "$setup_log"
        fi
        rm -f "$setup_log"
    else
        sudo env \
            GREETER_USER="$greeter_user" \
            NOCTALIA_GREETER_SESSION_BIN="$session_bin" \
            NOCTALIA_GREETER_APPLY_APPEARANCE="$apply_bin" \
            "$setup_script" || warn "  Greeter system setup failed; see the output above."
    fi

    write_greetd_config "$session_bin" "$greeter_user"

    current_dm=$(readlink -f "$dm_link" 2>/dev/null || true)
    [[ "$current_dm" == /dev/null ]] && current_dm=""
    if [[ "$current_dm" == */greetd.service ]]; then
        info "  greetd is already the display manager."
    else
        if [[ -n "$current_dm" ]]; then
            unit=$(basename "$current_dm")
            warn "Disabling the current display manager: $unit"
            sudo systemctl disable "$unit" || warn "  Could not disable $unit."
        else
            for dm in sddm gdm gdm3 lightdm lxdm xdm; do
                if systemctl is-enabled --quiet "$dm.service" 2>/dev/null; then
                    warn "Disabling the current display manager: $dm.service"
                    sudo systemctl disable "$dm.service" \
                        || warn "  Could not disable $dm.service."
                fi
            done
        fi
        sudo systemctl enable greetd.service \
            || warn "  Could not enable greetd.service."
        # Debian's unit carries Alias=display-manager.service, but upstream's
        # (Arch/Fedora) does not, so create the link explicitly. Resolve the
        # unit's real path instead of assuming /usr/lib.
        local greetd_unit="/usr/lib/systemd/system/greetd.service" fragment
        fragment=$(systemctl show -p FragmentPath --value greetd.service 2>/dev/null || true)
        [[ -n "$fragment" ]] && greetd_unit="$fragment"
        if sudo ln -sfn "$greetd_unit" "$dm_link"; then
            info "  $dm_link -> greetd.service"
        else
            warn "  Could not link $dm_link."
        fi
    fi

    if [[ "$(systemctl get-default 2>/dev/null || true)" != graphical.target ]]; then
        sudo systemctl set-default graphical.target \
            || warn "  Could not set graphical.target as the default."
    fi

    if [[ "$SESSION" != "yes" ]]; then
        warn "  --no-session: the greeter has no KineticWE-Dev session entry to offer."
    fi
    info "  Reboot to be presented with the Noctalia greeter."
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

    warn "This deletes $KWE_PREFIX and the build dirs (build-deb, build-deb-sid)"
    read -r -p "Continue? [y/N] " reply
    [[ "$reply" =~ ^[Yy]$ ]] || { info "Aborted."; exit 0; }

    rm -rf "$KWE_PREFIX" "${SCRIPT_DIR}/build-deb" "${SCRIPT_DIR}/build-deb-sid"
    info "Removed $KWE_PREFIX and the build dirs"

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
    local greeter_policy="/usr/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy"
    if [[ -f "$greeter_policy" ]] && grep -Fq -- "$KWE_PREFIX" "$greeter_policy" 2>/dev/null; then
        warn "Remove the greeter polkit action yourself:"
        echo "  sudo rm -f $greeter_policy"
    fi
    if [[ -f /etc/greetd/config.toml ]] \
        && grep -Fq -- "$KWE_PREFIX" /etc/greetd/config.toml 2>/dev/null; then
        warn "greetd still launches the greeter from the removed prefix."
        if [[ -f /etc/greetd/config.toml.bak ]]; then
            echo "  sudo cp -a /etc/greetd/config.toml.bak /etc/greetd/config.toml"
        else
            echo "  sudoedit /etc/greetd/config.toml   # restore the old command/user"
        fi
    fi
    if [[ -f /etc/tmpfiles.d/noctalia-greeter.conf ]]; then
        warn "Remove the greeter tmpfiles drop-in yourself:"
        echo "  sudo rm -f /etc/tmpfiles.d/noctalia-greeter.conf"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
MODE="full"
SESSION="yes"
GREETER="no"
# "yes": check_host offers to apt-install missing packages; "no"
# (--no-install-deps): only prints the command, like older versions did.
INSTALL_DEPS="yes"
# "yes" (default with --greeter): apply the greetd setup after the build;
# "no" (--no-setup-greeter): only print the root steps.
SETUP_GREETER="yes"
# Set by ask_greeter_setup(): "yes" when the setup will be applied.
GREETER_AUTO="no"
while (( $# )); do
    case "$1" in
        --sid)         FLAVOR="sid" ;;
        --deps-only)   MODE="deps" ;;
        --quick)       MODE="quick" ;;
        --update)      DO_UPDATE="yes" ;;
        --reset-local) MODE="reset" ;;
        --no-session)  SESSION="no" ;;
        --no-install-deps) INSTALL_DEPS="no" ;;
        --no-setup-greeter) SETUP_GREETER="no" ;;
        --greeter)     GREETER="yes" ;;
        -h|--help)     usage; exit 0 ;;
        *) error "Unknown option: $1"; usage; exit 1 ;;
    esac
    shift
done

# --update is --quick plus change detection; it never represents a dependency
# build (the trixie stack is far too expensive to enter by accident).
if [[ "$DO_UPDATE" == "yes" ]]; then
    case "$MODE" in
        full)  MODE="quick" ;;
        deps)  error "--update does not apply to --deps-only; use --quick --update."; exit 1 ;;
        reset) error "--update does not apply to --reset-local."; exit 1 ;;
    esac
fi

# The greeter's compositor needs wlroots-0.20, which trixie does not package.
# Ignore the flag there rather than failing an otherwise valid trixie build.
if [[ "$GREETER" == "yes" && "$FLAVOR" != "sid" ]]; then
    warn "--greeter needs Debian sid (wlroots-0.20 is not packaged in trixie); skipping the greeter."
    GREETER="no"
fi

set_flavor

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

# trixie builds the whole newer stack into the prefix; sid only needs the two
# KDE pins on top of the distro's Qt/KF6/Plasma (and no Spectacle: sid's 6.7
# already works with KWin >= 6.5).
build_dependencies() {
    if [[ "$FLAVOR" == trixie ]]; then
        install_qt
        build_kde_stack
        build_spectacle
    else
        build_kde_pins
    fi
}

case "$MODE" in
    full)
        build_dependencies
        build_kinetic_we yes
        build_portal yes
        build_noctalia yes
        [[ "$GREETER" == "yes" ]] && build_noctalia_greeter
        ;;
    deps)
        build_dependencies
        exit 0
        ;;
    quick)
        if [[ "$FLAVOR" == trixie ]]; then
            if [[ ! -x "$QT_DIR/bin/qmake" ]]; then
                error "No Qt in $QT_DIR - run a full build first."
                exit 1
            fi
        elif [[ ! -d "$KWE_PREFIX/lib64/cmake/KDecoration3" ]]; then
            error "kdecoration is not installed in $KWE_PREFIX - run a full --sid build first."
            exit 1
        fi
        if [[ "$DO_UPDATE" == "yes" ]]; then
            plan_update
            print_update_plan
            # sid: the pins are cheap to verify (unchanged ones are skipped) and
            # a changed pin is part of the compositor fingerprint, so it also
            # forces the compositor rebuild below.
            [[ "$FLAVOR" == "sid" ]] && build_kde_pins
        fi
        if component_planned compositor; then
            build_kinetic_we no
        else
            info "kinetic-we up to date, skipped."
        fi
        if component_planned portal; then
            build_portal no
        else
            info "xdg-desktop-portal-kwe up to date, skipped."
        fi
        if component_planned shell; then
            build_noctalia no
        else
            info "Noctalia up to date, skipped."
        fi
        if [[ "$GREETER" == "yes" ]]; then
            if component_planned greeter; then
                build_noctalia_greeter
            else
                info "Noctalia greeter up to date, skipped."
            fi
        fi
        [[ "$DO_UPDATE" == "yes" ]] && print_update_result
        ;;
esac

install_startup_script
install_kwe_run
wire_portal
# sid uses the distro's Spectacle (kde-spectacle 6.7); nothing to rewire.
[[ "$FLAVOR" == trixie ]] && wire_spectacle
# One prompt at the end decides whether the greeter's root setup is applied;
# the session entry follows the same decision so it is not printed twice.
[[ "$GREETER" == "yes" ]] && ask_greeter_setup
[[ "$SESSION" == "yes" ]] && stage_session_entry "$GREETER_AUTO"
if [[ "$GREETER" == "yes" ]]; then
    if [[ "$GREETER_AUTO" == "yes" ]]; then
        apply_greeter_setup
    else
        stage_greeter_setup
    fi
fi

if [[ "$GREETER_AUTO" == "yes" ]]; then
    info "Done. Reboot to be presented with the Noctalia greeter."
else
    info "Done. Start from a TTY with: $KWE_PREFIX/bin/start-kineticwe"
fi
