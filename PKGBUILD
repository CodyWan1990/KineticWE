# Maintainer: TheBlackDon <theblackdon@codeberg.org>
# Contributor: KineticWE project

pkgname=kineticwe
pkgver=6.7.80
pkgrel=18
# epoch: AUR previously shipped 6.7.81-x; the project re-aligned on the COPR
# 6.7.80-N numbering, which pacman would otherwise see as a downgrade.
epoch=1
pkgdesc="KineticWE - A tiling KWin Wayland compositor with native window tiling"
arch=('x86_64')
url="https://gitlab.com/theblackdon/kineticwe"
license=('GPL-2.0-or-later' 'LGPL-2.0-or-later' 'MIT' 'BSD-3-Clause' 'CC0-1.0')

depends=(
    'qt6-base'
    'qt6-declarative'
    'qt6-svg'
    'qt6-wayland'
    'kglobalaccel'
    'kirigami'
    'kwayland'
    'kscreenlocker'
    'xorg-xwayland'
    'hwdata'
    'noctalia-git'
    'plasma-workspace'
    'milou'
    # aurorae: built from pinned KDE master commit for ABI compat with our own
    # kdecoration; the system aurorae package would fail to load.
    # 'aurorae'
    'breeze'
    'kscreen'
    'systemsettings'
    'plasma-desktop'
    'xdg-desktop-portal'
    # KineticWE's own portal backend (rebranded xdg-desktop-portal-kde) is
    # built and installed from this package; the KDE backend conflicts.
    'kiconthemes'
    'kitemviews'
    'kstatusnotifieritem'
    'libqaccessibilityclient-qt6'
    'pipewire'
    'libevdev'
    'libcanberra'
    'procps-ng'
    'psmisc'
)

makedepends=(
    'cmake'
    'ninja'
    'gcc'
    'git'
    'pkgconf'
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
    'vulkan-headers'
    # X11
    'libx11'
    'libxcb'
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
)

optdepends=(
    'bluedevil: Bluetooth device integration'
    'plasma-nm: Network management applet'
    'plasma-pa: Audio volume management'
    'kinfocenter: System information center'
    'kde-gtk-config: GTK application styling'
    'upower: Battery and power-device integration for Noctalia'
)
# KineticWE installs beside KWin instead of replacing it: no
# kwin/kdecoration/kglobalacceld provides, replaces, or conflicts. The portal
# backend keeps its virtual provide so packages can depend on
# xdg-desktop-portal-kwe.
provides=('xdg-desktop-portal-kwe')


# Source: kinetic-we from GitLab + kglobalacceld/kdecoration from KDE invent
# kinetic-we is pinned to a commit (not a tag) to match the COPR 6.7.80-N
# releases, which package master HEAD via `git archive`. Bump _commit and
# reset pkgrel together on each upstream sync.
# kglobalacceld, kdecoration and aurorae are pinned to KDE master commits:
# kinetic-we needs the post-6.7 KGlobalAccelD API, the KDecoration3::Style
# enum (server-side drop shadows for CSD windows), and an aurorae plugin
# compiled against our pinned kdecoration (ABI compatibility with distro-
# packaged aurorae is not guaranteed). Bump all three pins together with
# _commit on each sync.
_commit=c368d2965b4392194d79080403d7e502fa9ae1e2
_kglobalacceld_commit=5cc88399d8e2a7951798f85127e23f73f1fa0889
_kdecoration_commit=d13049250c0ea1afc279aa8dc99243565c0d83e8
_aurorae_commit=6d55109d6e768263653456b539fb25d6d0d3a3b9
_sourcebase="$pkgname-$pkgver"
source=(
    "$url/-/archive/$_commit/$pkgname-$_commit.tar.gz"
    "kglobalacceld::git+https://invent.kde.org/plasma/kglobalacceld.git#commit=$_kglobalacceld_commit"
    "kdecoration::git+https://invent.kde.org/plasma/kdecoration.git#commit=$_kdecoration_commit"
    "aurorae::git+https://invent.kde.org/plasma/aurorae.git#commit=$_aurorae_commit"
)
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP')

prepare() {
    cd "$srcdir"

    echo "==> Extracting kinetic-we source..."
    # GitLab commit archive extracts to 'kineticwe-$_commit', rename to $_sourcebase
    local extract_dir="$pkgname-$_commit"
    if [[ -d "$extract_dir" && ! -d $_sourcebase ]]; then
        mv "$extract_dir" "$_sourcebase"
    fi
    # Fallback: also handle plain 'kineticwe' extraction
    if [[ -d kineticwe && ! -d $_sourcebase ]]; then
        mv kineticwe "$_sourcebase"
    fi

    echo "==> Preparing kglobalacceld and kdecoration..."
    # Both are already cloned at their pinned commits via git sources;
    # no extra prep needed
    cd "$srcdir"

    # KineticWE coexistence: private sonames for the vendored runtime libraries
    # so a stock kdecoration/kglobalacceld can stay installed. The CMake target
    # names (and thus kinetic-we's link lines) are unchanged.
    cat >> kglobalacceld/CMakeLists.txt <<'KWE_KGA'

set_target_properties(KGlobalAccelD PROPERTIES OUTPUT_NAME kineticwe-globalacceld)
KWE_KGA
    cat >> kdecoration/CMakeLists.txt <<'KWE_KDECO'

set_target_properties(kdecorations3 PROPERTIES OUTPUT_NAME kineticwe-decoration)
set_target_properties(kdecorations3private PROPERTIES OUTPUT_NAME kineticwe-decoration-private)
KWE_KDECO

    # KineticWE coexistence: namespace the vendored aurorae artifacts so they do
    # not collide with the distro aurorae package.
    find aurorae -type f \( -name 'CMakeLists.txt' -o -name '*.cpp' -o -name '*.h' -o -name '*.qml' -o -name '*.json' -o -name '*.desktop' -o -name '*.in' -o -name '*.xml' \) -print0 \
        | xargs -0 sed -i \
            -e 's/org\.kde\.kdecoration3/org.kineticwe.decoration/g' \
            -e 's/org\.kde\.kwin\.decoration/org.kineticwe.decoration/g' \
            -e 's#\bkwin/aurorae#kineticwe/aurorae#g' \
            -e 's#\bkwin/decorations#kineticwe/decorations#g' \
            -e 's/plasma-apply-aurorae/kineticwe-apply-aurorae/g'
}

build() {
    cd "$srcdir"

    # --- Phase 1: Build kglobalacceld ---
    echo "==> Building kglobalacceld (pinned KDE master commit)..."

    mkdir -p kglobalacceld/build
    pushd kglobalacceld/build

    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -Wno-dev

    cmake --build . --parallel "$(nproc)"
    popd

    # Install kglobalacceld to a temp staging dir so kinetic-we CMake finds it
    mkdir -p "$srcdir/kga-staging"
    DESTDIR="$srcdir/kga-staging" cmake --install kglobalacceld/build

    # --- Phase 2: Build kdecoration (pinned KDE master commit) ---
    # kinetic-we uses KDecoration3::Style (server-side drop shadows for CSD
    # windows), which only exists in kdecoration master — the released 6.7.x
    # in the repos is too old. Build it and stage it next to kglobalacceld.
    echo "==> Building kdecoration (pinned KDE master commit)..."

    mkdir -p kdecoration/build
    pushd kdecoration/build

    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -Wno-dev

    cmake --build . --parallel "$(nproc)"
    popd

    DESTDIR="$srcdir/kga-staging" cmake --install kdecoration/build

    # --- Phase 2.5: Build aurorae (pinned KDE master commit) ---
    # KineticWE replaces the system kdecoration package with a newer build
    # from a pinned commit. The distro-packaged aurorae plugin was compiled
    # against the old kdecoration ABI and may fail to load. Build it from
    # source against our own kdecoration so the KDecoration3 plugin is always
    # ABI-compatible.
    echo "==> Building aurorae (pinned KDE master commit)..."

    mkdir -p aurorae/build
    pushd aurorae/build

    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_PREFIX_PATH="$srcdir/kga-staging/usr" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -Wno-dev

    cmake --build . --parallel "$(nproc)"
    popd

    DESTDIR="$srcdir/kga-staging" cmake --install aurorae/build

    # --- Phase 3: Build kinetic-we ---
    echo "==> Building kinetic-we..."

    mkdir -p "$_sourcebase/build"
    cd "$_sourcebase/build"

    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_PREFIX_PATH="$srcdir/kga-staging/usr" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DKWIN_BUILD_GLOBALSHORTCUTS=ON \
        -DKWIN_BUILD_KCMS=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_KF6DocTools=ON \
        -Wno-dev

    cmake --build . --parallel "$(nproc)"

    # --- Phase 4: Build the KineticWE portal backend (rebranded xdg-desktop-portal-kde) ---
    echo "==> Building xdg-desktop-portal-kwe..."

    mkdir -p "$_sourcebase/portal/xdg-desktop-portal-kwe/build"
    cd "$_sourcebase/portal/xdg-desktop-portal-kwe/build"

    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -Wno-dev

    cmake --build . --parallel "$(nproc)"
}

package() {
    cd "$srcdir"

    echo "==> Installing kglobalacceld..."
    DESTDIR="$pkgdir" cmake --install kglobalacceld/build

    echo "==> Installing kdecoration..."
    DESTDIR="$pkgdir" cmake --install kdecoration/build

    echo "==> Installing aurorae..."
    DESTDIR="$pkgdir" cmake --install aurorae/build

    echo "==> Installing kinetic-we..."
    cd "$_sourcebase/build"
    DESTDIR="$pkgdir" cmake --install .

    echo "==> Installing xdg-desktop-portal-kwe..."
    cd "$_sourcebase/portal/xdg-desktop-portal-kwe/build"
    DESTDIR="$pkgdir" cmake --install .

    # Remove static libs and cmake files from main package
    rm -f "$pkgdir/usr/lib/cmake/KGlobalAccelD/"*.cmake 2>/dev/null || true
    rm -f "$pkgdir/usr/lib/"*.a 2>/dev/null || true

    # Strip kglobalacceld development files (matching COPR — keep only runtime lib)
    rm -rf "$pkgdir/usr/include/KGlobalAccelD" 2>/dev/null || true
    rm -f "$pkgdir/usr/lib/libkineticwe-globalacceld.so" 2>/dev/null || true

    # Strip kdecoration development files (matching COPR — keep only runtime libs)
    rm -rf "$pkgdir/usr/include/KDecoration3" 2>/dev/null || true
    rm -f "$pkgdir/usr/include/KF6/kdecoration3_version.h" 2>/dev/null || true
    rm -rf "$pkgdir/usr/lib/cmake/KDecoration3" 2>/dev/null || true
    rm -f "$pkgdir/usr/lib/libkineticwe-decoration.so" 2>/dev/null || true
    rm -f "$pkgdir/usr/lib/libkineticwe-decoration-private.so" 2>/dev/null || true

    # Strip the libkineticwe.so devel symlink and every KDE System Settings module
    rm -f "$pkgdir/usr/lib/libkineticwe.so" 2>/dev/null || true
    rm -rf "$pkgdir/usr/lib/qt6/plugins/org.kde.kdecoration3.kcm" 2>/dev/null || true
    rm -f "$pkgdir/usr/share/applications/kcm_"*.desktop 2>/dev/null || true

    # Strip aurorae development files (keep only runtime plugin + themes)
    rm -rf "$pkgdir/usr/lib/cmake/Aurorae" 2>/dev/null || true
    rm -rf "$pkgdir/usr/include/Aurorae" 2>/dev/null || true

    # Install license
    install -Dm644 "$srcdir/$_sourcebase/LICENSES/"* \
        -t "$pkgdir/usr/share/licenses/$pkgname/"

    # Install kglobalacceld license
    if [[ -d "$srcdir/kglobalacceld/LICENSES" ]]; then
        install -Dm644 "$srcdir/kglobalacceld/LICENSES/"* \
            -t "$pkgdir/usr/share/licenses/$pkgname-kglobalacceld/" 2>/dev/null || true
    fi

    # Install kdecoration license
    if [[ -d "$srcdir/kdecoration/LICENSES" ]]; then
        install -Dm644 "$srcdir/kdecoration/LICENSES/"* \
            -t "$pkgdir/usr/share/licenses/$pkgname-kdecoration/" 2>/dev/null || true
    fi

    # Install aurorae license
    if [[ -d "$srcdir/aurorae/LICENSES" ]]; then
        install -Dm644 "$srcdir/aurorae/LICENSES/"* \
            -t "$pkgdir/usr/share/licenses/$pkgname-aurorae/" 2>/dev/null || true
    fi

    # Install the portal backend license
    if [[ -d "$srcdir/$_sourcebase/portal/xdg-desktop-portal-kwe/LICENSES" ]]; then
        install -Dm644 "$srcdir/$_sourcebase/portal/xdg-desktop-portal-kwe/LICENSES/"* \
            -t "$pkgdir/usr/share/licenses/$pkgname-xdg-desktop-portal-kwe/" 2>/dev/null || true
    fi

    # Install Wayland session launcher (start-kineticwe) from the source tree
    # Single canonical launcher: scripts/start-kineticwe.sh is installed by
    # both this package and the COPR RPM. Older revisions embedded a copy
    # here that drifted out of sync (and broke logout -> greeter return).
    echo "==> Installing start-kineticwe session launcher..."
    mkdir -p "$pkgdir/usr/bin"
    sed -e "s|@INSTALL_PREFIX@|/usr|g" \
        "$srcdir/$_sourcebase/scripts/start-kineticwe.sh" \
        > "$pkgdir/usr/bin/start-kineticwe"
    chmod 0755 "$pkgdir/usr/bin/start-kineticwe"


    # Install Wayland session desktop entry (for SDDM, greetd, etc.)
    echo "==> Installing wayland-sessions desktop entry..."
    mkdir -p "$pkgdir/usr/share/wayland-sessions"
    sed -e "s|@INSTALL_PREFIX@|/usr|g" \
        "$srcdir/$_sourcebase/scripts/kineticwe.desktop.in" \
        > "$pkgdir/usr/share/wayland-sessions/kineticwe.desktop"
    chmod 0644 "$pkgdir/usr/share/wayland-sessions/kineticwe.desktop"
    # Install systemd drop-in to mask the standalone kglobalacceld daemon
    # KineticWE embeds kglobalacceld in-process; the standalone daemon would
    # conflict on D-Bus (org.kde.kglobalaccel) and break global shortcuts.
    echo "==> Installing kglobalacceld systemd mask..."
    mkdir -p "$pkgdir/etc/systemd/user/plasma-kglobalaccel.service.d"
    cat > "$pkgdir/etc/systemd/user/plasma-kglobalaccel.service.d/kineticwe-mask.conf" << MASKEOF
[Unit]
# KineticWE has kglobalacceld built in. Prevent the standalone daemon from
# starting so it does not conflict with the compositor's D-Bus registration.
ConditionPathExists=/nonexistent

[Service]
ExecStart=
MASKEOF
    chmod 0644 "$pkgdir/etc/systemd/user/plasma-kglobalaccel.service.d/kineticwe-mask.conf"

    # Install the KineticWE session target: a curated plasma-workspace.target
    # replacement that pulls graphical-session.target up via dependency and
    # starts the shell-independent Plasma session services (KWallet PAM,
    # polkit agent, kcminit, session restore, XDG autostart, ...).
    echo "==> Installing kineticwe-workspace.target systemd unit..."
    install -Dm644 "$srcdir/$_sourcebase/scripts/kineticwe-workspace.target" "$pkgdir/usr/lib/systemd/user/kineticwe-workspace.target"

    # Install the 2.0 session target: pulls graphical-session.target up so the
    # portal frontend unit (Requisite=graphical-session.target) can start.
    echo "==> Installing kineticwe-session.target systemd unit..."
    install -Dm644 "$srcdir/$_sourcebase/scripts/kineticwe-session.target" "$pkgdir/usr/lib/systemd/user/kineticwe-session.target"

    # Install D-Bus service activation file for org.kde.kglobalaccel
    # The original kglobalacceld package provides this file, but since kineticwe
    # replaces/conflicts with kglobalacceld, the file is missing. Without it,
    # D-Bus activation fails and shortcuts KCM shows
    # "Error while communicating with the global shortcuts service".
    echo "==> Installing D-Bus activation for org.kde.kglobalaccel..."
    mkdir -p "$pkgdir/usr/share/dbus-1/services"
    cat > "$pkgdir/usr/share/dbus-1/services/org.kde.kglobalaccel.service" << DBUSEOF
[D-BUS Service]
Name=org.kde.kglobalaccel
Exec=/usr/libexec/kineticwe-kglobalacceld-bridge
DBUSEOF
    chmod 0644 "$pkgdir/usr/share/dbus-1/services/org.kde.kglobalaccel.service"

    # Install the bridge script for D-Bus activation
    echo "==> Installing kineticwe-kglobalacceld-bridge..."
    mkdir -p "$pkgdir/usr/libexec"
    cat > "$pkgdir/usr/libexec/kineticwe-kglobalacceld-bridge" << 'BRIDGEEOF'
#!/bin/bash
# KineticWE kglobalacceld bridge for D-Bus activation.
# The real kglobalacceld is embedded in-process in kinetic-we.
# This script handles D-Bus activation requests gracefully.
for i in 1 2 3 4 5; do
    if busctl get-property org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel 2>/dev/null >/dev/null; then
        exit 0
    fi
    sleep 1
done
echo "kineticwe-kglobalacceld-bridge: kinetic-we not running or kglobalaccel service not registered." >&2
exit 1
BRIDGEEOF
    chmod 0755 "$pkgdir/usr/libexec/kineticwe-kglobalacceld-bridge"
}

# ---------------------------------------------------------------------------
# Post-install hooks (like COPR %%post / %%postun)
# Reload user daemon so the kglobalacceld mask takes effect immediately.
# ---------------------------------------------------------------------------
post_install() {
    # Reload user daemon so the kglobalacceld mask takes effect
    systemctl --user daemon-reload 2>/dev/null || true
    # Explicitly mask the standalone kglobalacceld service to prevent D-Bus activation
    systemctl --user mask plasma-kglobalaccel.service 2>/dev/null || true
}

post_upgrade() {
    post_install
}

post_remove() {
    # Clean up: unmask and reload daemon after removing the mask
    systemctl --user unmask plasma-kglobalaccel.service 2>/dev/null || true
    systemctl --user daemon-reload 2>/dev/null || true
}
