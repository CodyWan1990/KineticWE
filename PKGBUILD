# Maintainer: TheBlackDon <theblackdon@codeberg.org>
# Contributor: KineticWE project
#
# Arch split package mirroring the Fedora COPR spec
# (lgl-kineticwe-copr/kineticwe.spec is the source of truth). Keep pkgver,
# pkgrel and every pinned commit below in sync with the spec's Version,
# Release and %global *_ref defaults.

pkgbase=kineticwe
pkgname=(kineticwe xdg-desktop-portal-kwe kineticwe-noctalia kineticwe-greeter)
pkgver=0.0.1
pkgrel=14
# epoch: the 1.x packaging shipped 1:6.7.80-N. KineticWE 2.0 restarted the
# version at 0.0.1 (matching the COPR spec), which pacman would otherwise see
# as a downgrade for existing installs.
epoch=2
pkgdesc="KineticWE - A tiling KWin Wayland compositor with native window tiling"
arch=('x86_64')
url="https://gitlab.com/theblackdon/kineticwe"
license=('GPL-2.0-or-later' 'LGPL-2.0-or-later' 'MIT' 'BSD-3-Clause' 'CC0-1.0')

# Everything needed to build all four packages. Runtime dependencies live in
# each package_*() function; CI installs only this array.
makedepends=(
    'cmake'
    'ninja'
    'gcc'
    'git'
    'pkgconf'
    'meson'
    'extra-cmake-modules'
    # Qt6
    'qt6-base'
    'qt6-declarative'
    'qt6-svg'
    'qt6-5compat'
    'qt6-wayland'
    'qt6-tools'
    # KF6
    'kauth'
    'kcolorscheme'
    'kconfig'
    'kcoreaddons'
    'kcrash'
    'kdbusaddons'
    'kglobalaccel'
    'kio'
    'kjobwidgets'
    'kguiaddons'
    'ki18n'
    'kidletime'
    'kpackage'
    'kservice'
    'ksvg'
    'kwidgetsaddons'
    'kwindowsystem'
    'kdeclarative'
    'kcmutils'
    'knewstuff'
    'kxmlgui'
    'krunner'
    'knotifications'
    'kirigami'
    # Portal backend (xdg-desktop-portal-kwe)
    'kiconthemes'
    'kitemviews'
    'kstatusnotifieritem'
    # Plasma / KDE
    'kwayland'
    'kscreenlocker'
    'knighttime'
    'plasma-wayland-protocols'
    'plasma-activities'
    'libplasma'
    # System / Wayland
    'libepoxy'
    'libglvnd'
    'wayland'
    'wayland-protocols'
    'libxkbcommon'
    'libxkbcommon-x11'
    'libinput'
    'libdrm'
    'mesa'
    'libdisplay-info'
    'lcms2'
    'libxcvt'
    'libcanberra'
    'libevdev'
    'systemd'
    'pipewire'
    'freetype2'
    'fontconfig'
    'vulkan-icd-loader'
    'vulkan-headers'
    # X11
    'libx11'
    'libxcb'
    'xcb-util'
    'xcb-util-keysyms'
    'xcb-util-cursor'
    'xcb-util-wm'
    'xcb-util-image'
    'xcb-util-renderutil'
    # Misc
    'libqaccessibilityclient-qt6'
    'cairo'
    'pango'
    'harfbuzz'
    'glib2'
    'pam'
    'polkit'
    'curl'
    'libwebp'
    'librsvg'
    'libqalculate'
    'libxml2'
    'jemalloc'
    'nlohmann-json'
    # Noctalia fork shell (kineticwe-noctalia; vendored at shell/noctalia)
    'libsodium'
    'libsecret'
    'sdbus-cpp'
    'wireplumber'
    'libsndfile'
    'libjxl'
    'md4c'
    'tomlplusplus'
    'libical'
    'stb'
    # Noctalia greeter (kineticwe-greeter)
    'wlroots0.20'
)

# Pinned commits. Values mirror the spec's %global defaults; bump them there
# and here together on each upstream sync.
# _commit is ONLY the fallback remote source (a GitLab archive of the
# kineticwe repo). Release builds pass KWE_LOCAL_TARBALL instead (see below).
_commit=c368d2965b4392194d79080403d7e502fa9ae1e2
# kinetic-we needs the post-6.7 KGlobalAccelD API and the KDecoration3::Style
# enum (server-side drop shadows for CSD windows), so both are built from
# pinned KDE master commits and shipped under private sonames.
_kglobalacceld_commit=5cc88399d8e2a7951798f85127e23f73f1fa0889
_kdecoration_commit=d13049250c0ea1afc279aa8dc99243565c0d83e8
# kconfig_compiler from KConfig < 6.30 generates uncompilable setters for
# enum-class kcfg entries (fixed in KConfig commit 41592cc). Only the fixed
# compiler is built from this commit; nothing from it is installed.
_kconfig_commit=41592ccfd9748be82c83ce98912037bffb73954e
# Noctalia greeter (kineticwe-greeter).
_greeter_commit=b3e3bc0268fc5aaf346a250faac70570d15e3bef

# Main source: a local tarball (single top-level dir kineticwe-$pkgver/) when
# KWE_LOCAL_TARBALL is set, otherwise the GitLab archive of $_commit.
if [[ -n "${KWE_LOCAL_TARBALL:-}" ]]; then
    _mainsource="$pkgbase-$pkgver.tar.gz::file://$KWE_LOCAL_TARBALL"
else
    _mainsource="$pkgbase-$pkgver.tar.gz::$url/-/archive/$_commit/$pkgbase-$_commit.tar.gz"
fi
source=(
    "$_mainsource"
    "kglobalacceld-$_kglobalacceld_commit.tar.gz::https://invent.kde.org/plasma/kglobalacceld/-/archive/$_kglobalacceld_commit/kglobalacceld-$_kglobalacceld_commit.tar.gz"
    "kdecoration-$_kdecoration_commit.tar.gz::https://invent.kde.org/plasma/kdecoration/-/archive/$_kdecoration_commit/kdecoration-$_kdecoration_commit.tar.gz"
    "kconfig-$_kconfig_commit.tar.gz::https://invent.kde.org/frameworks/kconfig/-/archive/$_kconfig_commit/kconfig-$_kconfig_commit.tar.gz"
    "noctalia-greeter-kwe-$_greeter_commit.tar.gz::https://gitlab.com/theblackdon/noctalia-greeter-kwe/-/archive/$_greeter_commit/noctalia-greeter-kwe-$_greeter_commit.tar.gz"
)
# GitLab archive tarballs are regenerated on demand and are not byte-stable,
# so checksums are skipped; every source is pinned to an exact commit instead.
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')

_sourcebase="$pkgbase-$pkgver"

# Move a freshly extracted <name>-<commit> dir to its normalized name. A fresh
# extraction always wins so re-running makepkg starts from pristine sources;
# with makepkg -e the already-normalized dir is kept as-is.
_kwe_normalize() {
    local from="$1" to="$2"
    if [[ -d "$from" ]]; then
        rm -rf "$to"
        mv "$from" "$to"
    fi
}

prepare() {
    cd "$srcdir"

    echo "==> Normalizing source directories..."
    # GitLab commit archive extracts to 'kineticwe-$_commit'
    if [[ -d "$pkgbase-$_commit" && ! -d "$_sourcebase" ]]; then
        mv "$pkgbase-$_commit" "$_sourcebase"
    fi
    _kwe_normalize "kglobalacceld-$_kglobalacceld_commit" kglobalacceld
    _kwe_normalize "kdecoration-$_kdecoration_commit" kdecoration
    _kwe_normalize "kconfig-$_kconfig_commit" kconfig
    _kwe_normalize "noctalia-greeter-kwe-$_greeter_commit" noctalia-greeter

    # Rebrand kdecoration translations so they coexist with the stock package.
    sed -i \
        's/TRANSLATION_DOMAIN=\\"kdecoration\\"/TRANSLATION_DOMAIN=\\"kineticwe-decoration\\"/' \
        kdecoration/src/CMakeLists.txt
    local po_file
    for po_file in kdecoration/po/*/kdecoration.po; do
        [[ -e "$po_file" ]] || continue
        mv "$po_file" "${po_file%/kdecoration.po}/kineticwe-decoration.po"
    done
    for po_file in kdecoration/po/*/kineticwe-decoration.po; do
        [[ -e "$po_file" ]] || continue
        sed -i \
            's/Project-Id-Version: kdecoration/Project-Id-Version: kineticwe-decoration/' \
            "$po_file"
    done

    # REQUIRED PACKAGING PATCH: PORTAL LOCALE DOMAIN. Keep the portal
    # translation domain separate from the stock KDE portal so both packages
    # can be installed without locale-file ownership conflicts. Applied at
    # package time; the upstream source tree is intentionally left unchanged.
    local portal_src="$srcdir/$_sourcebase/portal/xdg-desktop-portal-kwe"
    sed -i \
        's/TRANSLATION_DOMAIN="xdg-desktop-portal-kde"/TRANSLATION_DOMAIN="xdg-desktop-portal-kwe"/' \
        "$portal_src/src/CMakeLists.txt"
    sed -i \
        's/xdg-desktop-portal-kde\.pot/xdg-desktop-portal-kwe.pot/' \
        "$portal_src/Messages.sh"
    sed -i \
        's/FILE xdp-kde\.categories/FILE xdp-kwe.categories/' \
        "$portal_src/src/CMakeLists.txt"
    for po_file in "$portal_src"/po/*/xdg-desktop-portal-kde.po; do
        [[ -e "$po_file" ]] || continue
        mv "$po_file" "${po_file%/xdg-desktop-portal-kde.po}/xdg-desktop-portal-kwe.po"
    done
    for po_file in "$portal_src"/po/*/xdg-desktop-portal-kwe.po; do
        [[ -e "$po_file" ]] || continue
        sed -i \
            's/Project-Id-Version: xdg-desktop-portal-kde/Project-Id-Version: xdg-desktop-portal-kwe/' \
            "$po_file"
    done

    # KineticWE coexistence: private sonames for the vendored runtime libraries
    # so a stock kdecoration/kglobalacceld can stay installed. The CMake target
    # names (and thus kinetic-we's link lines) are unchanged. Guarded so
    # re-running prepare() (makepkg -e) does not append twice.
    if ! grep -q 'OUTPUT_NAME kineticwe-decoration)' kdecoration/CMakeLists.txt; then
        cat >> kdecoration/CMakeLists.txt <<'KWE_KDECO'

# KineticWE: private soname so a stock kdecoration can stay installed.
set_target_properties(kdecorations3 PROPERTIES OUTPUT_NAME kineticwe-decoration)
set_target_properties(kdecorations3private PROPERTIES OUTPUT_NAME kineticwe-decoration-private)
KWE_KDECO
    fi
    if ! grep -q 'OUTPUT_NAME kineticwe-globalacceld)' kglobalacceld/CMakeLists.txt; then
        cat >> kglobalacceld/CMakeLists.txt <<'KWE_KGA'

# KineticWE: private soname so a stock kglobalacceld can stay installed.
set_target_properties(KGlobalAccelD PROPERTIES OUTPUT_NAME kineticwe-globalacceld)
KWE_KGA
    fi
}

build() {
    cd "$srcdir"

    # --- Phase 1: kglobalacceld (pinned KDE master commit) ---
    echo "==> Building kglobalacceld..."
    cmake -S kglobalacceld -B kga-build -GNinja \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_SKIP_RPATH=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF \
        -Wno-dev
    cmake --build kga-build

    # Stage it so kinetic-we's CMake finds it
    mkdir -p "$srcdir/kga-staging"
    DESTDIR="$srcdir/kga-staging" cmake --install kga-build --prefix /usr

    # --- Phase 2: kdecoration (pinned KDE master commit) ---
    # kinetic-we uses KDecoration3::Style, which the released kdecoration in
    # the repos does not have. Build it and stage it next to kglobalacceld.
    echo "==> Building kdecoration..."
    cmake -S kdecoration -B kdeco-build -GNinja \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_SKIP_RPATH=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF \
        -Wno-dev
    cmake --build kdeco-build
    DESTDIR="$srcdir/kga-staging" cmake --install kdeco-build --prefix /usr

    # --- Phase 3: fixed kconfig_compiler (build-time tool only) ---
    echo "==> Building kconfig_compiler..."
    cmake -S kconfig -B kconfig-build -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -Wno-dev
    cmake --build kconfig-build --target kconfig_compiler

    # --- Phase 4: kinetic-we compositor ---
    echo "==> Building kinetic-we..."
    cmake -S "$_sourcebase" -B kwin-build -GNinja \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_PREFIX_PATH="$srcdir/kga-staging/usr" \
        -DCMAKE_SKIP_RPATH=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF \
        -DKWIN_KCONFIG_COMPILER="$srcdir/kconfig-build/bin/kconfig_compiler_kf6" \
        -DKWIN_BUILD_GLOBALSHORTCUTS=ON \
        -DKWIN_BUILD_KCMS=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_KF6DocTools=ON \
        -Wno-dev
    cmake --build kwin-build

    # --- Phase 5: portal backend (rebranded xdg-desktop-portal-kde) ---
    # KDE_INSTALL_LIBEXECDIR is left at its default, like the spec.
    echo "==> Building xdg-desktop-portal-kwe..."
    cmake -S "$_sourcebase/portal/xdg-desktop-portal-kwe" -B portal-build -GNinja \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_SKIP_RPATH=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF \
        -Wno-dev
    cmake --build portal-build

    # --- Phase 6: Noctalia fork shell (noctalia-kwe) ---
    echo "==> Building kineticwe-noctalia..."
    arch-meson "$_sourcebase/shell/noctalia" shell-build \
        -Dtests=disabled
    meson compile -C shell-build

    # --- Phase 7: Noctalia greeter (kineticwe-greeter) ---
    # -Dcpp_args/-Dc_args replace the environment flags in meson, so makepkg's
    # CFLAGS/CXXFLAGS are passed through explicitly alongside -fPIE.
    echo "==> Building kineticwe-greeter..."
    arch-meson noctalia-greeter greeter-build \
        -Dcpp_args="${CXXFLAGS:-} -fPIE" \
        -Dc_args="${CFLAGS:-} -fPIE"
    meson compile -C greeter-build
}

package_kineticwe() {
    pkgdesc="KineticWE - A tiling KWin Wayland compositor with native window tiling"
    license=('GPL-2.0-or-later' 'LGPL-2.0-or-later' 'MIT' 'BSD-3-Clause' 'CC0-1.0')
    install=kineticwe.install
    depends=(
        "kineticwe-noctalia=$epoch:$pkgver-$pkgrel"
        "xdg-desktop-portal-kwe=$epoch:$pkgver-$pkgrel"
        'milou'
        # The window decoration is the first-party org.kineticwe.decoration
        # plugin; neither breeze nor the distro kdecoration is required.
        'breeze-icons'
        'xorg-xwayland'
        'hwdata'
        'xdg-desktop-portal'
        'bash'
        'systemd'
        'procps-ng'
        'psmisc'
        'adw-gtk-theme'
        # Shared-library runtime deps
        'qt6-base'
        'qt6-declarative'
        'qt6-svg'
        'qt6-wayland'
        'qt6-5compat'
        'kauth'
        'kcolorscheme'
        'kconfig'
        'kcoreaddons'
        'kcrash'
        'kdbusaddons'
        'kglobalaccel'
        'kguiaddons'
        'ki18n'
        'kidletime'
        'kio'
        'kpackage'
        'kservice'
        'kwidgetsaddons'
        'kwindowsystem'
        'kdeclarative'
        'knewstuff'
        'kxmlgui'
        'krunner'
        'knotifications'
        'kirigami'
        'kwayland'
        'kscreenlocker'
        'libqaccessibilityclient-qt6'
        'pipewire'
        'libevdev'
        'libcanberra'
        'kiconthemes'
        'kitemviews'
        'kstatusnotifieritem'
        'libplasma'
        'plasma-activities'
        'ksvg'
        'kcmutils'
        'kitemmodels'
        'knighttime'
        'libdisplay-info'
        'libxcvt'
        'lcms2'
        'libinput'
        'mesa'
        'libepoxy'
        'libdrm'
        'wayland'
        'libxkbcommon'
        'libxkbcommon-x11'
        'freetype2'
        'fontconfig'
        'libx11'
        'libxcb'
        'xcb-util-keysyms'
        'xcb-util-cursor'
        'xcb-util-wm'
    )
    optdepends=(
        'xdg-user-dirs: create standard user directories on first login'
        'alacritty: default terminal emulator'
        'qt5ct: Qt5 theming in the KineticWE session'
        'qt6ct: Qt6 theming in the KineticWE session'
        'spectacle: screenshot tool'
        'qqc2-desktop-style: desktop style for Qt Quick Controls apps'
    )
    # KineticWE installs beside KWin instead of replacing it: no
    # kwin/kdecoration/kglobalacceld provides, replaces, or conflicts.

    cd "$srcdir"

    echo "==> Installing kglobalacceld, kdecoration and kinetic-we..."
    DESTDIR="$pkgdir" cmake --install kga-build --prefix /usr
    DESTDIR="$pkgdir" cmake --install kdeco-build --prefix /usr
    DESTDIR="$pkgdir" cmake --install kwin-build --prefix /usr

    # Session launcher: the packaging-local copy the spec ships (SOURCE10),
    # which intentionally overrides scripts/start-kineticwe.sh.
    sed -e 's|@INSTALL_PREFIX@|/usr|g' \
        "$srcdir/$_sourcebase/lgl-kineticwe-copr/start-kineticwe.sh" \
        > "$srcdir/start-kineticwe"
    install -Dm755 "$srcdir/start-kineticwe" "$pkgdir/usr/bin/start-kineticwe"
    install -Dm644 "$srcdir/$_sourcebase/lgl-kineticwe-copr/SOURCES/kineticwe.desktop" \
        "$pkgdir/usr/share/wayland-sessions/kineticwe.desktop"
    # KineticWE embeds kglobalacceld in-process; keep the standalone daemon off.
    install -Dm644 "$srcdir/$_sourcebase/lgl-kineticwe-copr/SOURCES/kglobalacceld-mask.conf" \
        "$pkgdir/etc/systemd/user/plasma-kglobalaccel.service.d/kineticwe-mask.conf"
    install -Dm644 "$srcdir/$_sourcebase/scripts/kineticwe-workspace.target" \
        "$pkgdir/usr/lib/systemd/user/kineticwe-workspace.target"
    install -Dm644 "$srcdir/$_sourcebase/scripts/kineticwe-session.target" \
        "$pkgdir/usr/lib/systemd/user/kineticwe-session.target"

    # Drop development files: runtime only, like the spec.
    rm -rf "$pkgdir/usr/include/KGlobalAccelD"
    rm -rf "$pkgdir/usr/include/KDecoration3"
    rm -f "$pkgdir/usr/include/KF6/kdecoration3_version.h"
    rm -rf "$pkgdir/usr/include/kwin"
    rm -rf "$pkgdir/usr/lib/cmake/KGlobalAccelD"
    rm -rf "$pkgdir/usr/lib/cmake/KDecoration3"
    rm -rf "$pkgdir/usr/lib/cmake/KWin"
    rm -rf "$pkgdir/usr/lib/cmake/KWinDBusInterface"
    rm -f "$pkgdir/usr/lib/libkineticwe-globalacceld.so"
    rm -f "$pkgdir/usr/lib/libkineticwe-decoration.so"
    rm -f "$pkgdir/usr/lib/libkineticwe-decoration-private.so"
    rm -f "$pkgdir/usr/lib/libkineticwe.so"
    # Remove now-empty include/cmake dirs
    rmdir --ignore-fail-on-non-empty "$pkgdir/usr/include/KF6" "$pkgdir/usr/include" \
        "$pkgdir/usr/lib/cmake" 2>/dev/null || true

    # Avoid owning files that conflict with stock KWin notification data.
    rm -f "$pkgdir/usr/share/knotifications6/"*.notifyrc
    rm -rf "$pkgdir/usr/share/dbus-1/interfaces"

    # Kinetic Settings owns all settings; drop every KDE System Settings module.
    rm -rf "$pkgdir/usr/lib/qt6/plugins/org.kde.kdecoration3.kcm"
    rm -f "$pkgdir/usr/share/applications/kcm_"*.desktop

    # Licenses and docs
    install -Dm644 "$srcdir/$_sourcebase/LICENSES/"* \
        -t "$pkgdir/usr/share/licenses/$pkgname/"
    install -Dm644 "$srcdir/kglobalacceld/LICENSES/"* \
        -t "$pkgdir/usr/share/licenses/$pkgname/kglobalacceld/"
    install -Dm644 "$srcdir/kdecoration/LICENSES/"* \
        -t "$pkgdir/usr/share/licenses/$pkgname/kdecoration/"
    install -Dm644 "$srcdir/$_sourcebase/data/icon-themes/LICENSE" \
        "$pkgdir/usr/share/licenses/$pkgname/icon-themes/LICENSE"
    install -Dm644 "$srcdir/$_sourcebase/README.md" "$srcdir/$_sourcebase/CONTRIBUTING.md" \
        -t "$pkgdir/usr/share/doc/$pkgname/"
}

package_xdg-desktop-portal-kwe() {
    pkgdesc="KineticWE portal backend (rebranded xdg-desktop-portal-kde)"
    license=('LGPL-2.0-or-later')
    depends=(
        'kirigami'
        'kitemmodels'
        'ki18n'
        'kpipewire'
        'kiconthemes'
        'kcmutils'
        'kdeclarative'
        'kio'
        'kstatusnotifieritem'
        'kcoreaddons'
        'kconfig'
        'kcolorscheme'
        'kglobalaccel'
        'knotifications'
        'kservice'
        'kwidgetsaddons'
        'kwindowsystem'
        'kcrash'
        'kwayland'
        'wayland'
        'pipewire'
        'xdg-desktop-portal'
        'qt6-base'
        'qt6-declarative'
        'qt6-wayland'
    )

    cd "$srcdir"
    DESTDIR="$pkgdir" cmake --install portal-build --prefix /usr

    install -Dm644 "$srcdir/$_sourcebase/portal/xdg-desktop-portal-kwe/LICENSES/"* \
        -t "$pkgdir/usr/share/licenses/$pkgname/"
}

package_kineticwe-noctalia() {
    pkgdesc="Noctalia fork shell for KineticWE (noctalia-kwe)"
    license=('MIT' 'BSD-3-Clause' 'CC0-1.0')
    depends=(
        # qdbus6 (from qt6-tools) is used at runtime by the kineticwe-layouts
        # plugin to read/switch the compositor's /Tiling layouts.
        'qt6-tools'
        'sdbus-cpp'
        'wayland'
        'libxkbcommon'
        'freetype2'
        'fontconfig'
        'cairo'
        'pango'
        'harfbuzz'
        'librsvg'
        'glib2'
        'libsecret'
        'libsodium'
        'polkit'
        'pipewire'
        'wireplumber'
        'curl'
        'libqalculate'
        'libxml2'
        'md4c'
        'tomlplusplus'
        'libical'
        'pam'
        'jemalloc'
        'libglvnd'
        'libepoxy'
        'libwebp'
        'libjxl'
        'libsndfile'
    )

    cd "$srcdir"
    meson install -C shell-build --no-rebuild --destdir "$pkgdir"

    install -Dm644 "$srcdir/$_sourcebase/shell/noctalia/LICENSE" \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

package_kineticwe-greeter() {
    pkgdesc="Noctalia greeter for KineticWE (greetd)"
    license=('MIT')
    depends=(
        "kineticwe=$epoch:$pkgver-$pkgrel"
        'greetd'
        'wlroots0.20'
        'wayland'
        'libxkbcommon'
        'libinput'
        'freetype2'
        'fontconfig'
        'cairo'
        'pango'
        'librsvg'
        'libxml2'
        'glib2'
        'tomlplusplus'
        'libwebp'
        'libglvnd'
        'polkit'
    )

    cd "$srcdir"
    meson install -C greeter-build --no-rebuild --destdir "$pkgdir"
    # No tmpfiles user rewrite here: Arch's greetd runs the greeter as the
    # 'greeter' user, which the upstream tmpfiles entry already uses (the spec
    # rewrites it to greetd:greetd for Fedora only).
    install -Dm755 "$srcdir/$_sourcebase/lgl-kineticwe-copr/kineticwe-setup-greeter.sh" \
        "$pkgdir/usr/bin/kineticwe-setup-greeter"

    install -Dm644 "$srcdir/noctalia-greeter/LICENSE" \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
