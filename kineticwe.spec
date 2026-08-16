# =============================================================================
# KineticWE - A tiling KWin compositor for Wayland
# COPR spec file
# =============================================================================
#
# NOTE: the authoritative COPR spec is lgl-kineticwe-copr/kineticwe.spec
# (used by .copr/Makefile and build-rpm-dev.sh). This file is kept in sync
# for manual builds only.
#
# This spec builds kglobalacceld and kdecoration from pinned KDE master
# commits first, then builds kinetic-we against them. The system kglobalacceld
# package is too old (6.7.0) and lacks the direct KGlobalAccelD API that
# kinetic-we requires; the system kdecoration (6.7.x) lacks KDecoration3::Style
# (server-side drop shadows for CSD windows).
#
# To trigger a build in COPR:
#   1. Tag a release:  git tag -a v6.7.80 -m "Release 6.7.80"
#   2. Push the tag:   git push origin v6.7.80
#   3. Build in COPR:  copr-cli build kineticwe --nowait \
#                         https://codeberg.org/theblackdon/kineticwe/archive/v6.7.80.tar.gz
#
# Or use COPR SCM auto-build pointing at the Codeberg repo.
# =============================================================================

%global __cmake_in_source_build 0
%global _vpath_builddir %{_vpath_builddir}

Name:           kineticwe
Version:        6.7.80
Release:        10%{?dist}
Summary:        KineticWE - A tiling KWin Wayland compositor

License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND MIT AND BSD-3-Clause AND CC0-1.0
URL:            https://gitlab.com/theblackdon/kineticwe
Source0:        %{url}/archive/v%{version}.tar.gz
Source1:        https://invent.kde.org/plasma/kglobalacceld/-/archive/5cc88399d8e2a7951798f85127e23f73f1fa0889/kglobalacceld-5cc88399d8e2a7951798f85127e23f73f1fa0889.tar.gz
# kconfig_compiler from KConfig < 6.30 generates uncompilable setters for
# enum-class kcfg entries (fixed in KConfig commit 41592cc, first released in
# 6.30). Build the fixed compiler from this pinned commit (build-time only).
# Drop this once the buildroot ships KConfig >= 6.30.
Source2:        https://invent.kde.org/frameworks/kconfig/-/archive/41592ccfd9748be82c83ce98912037bffb73954e/kconfig-41592ccfd9748be82c83ce98912037bffb73954e.tar.gz
Source3:        https://invent.kde.org/plasma/kdecoration/-/archive/d13049250c0ea1afc279aa8dc99243565c0d83e8/kdecoration-d13049250c0ea1afc279aa8dc99243565c0d83e8.tar.gz

# =============================================================================
# Build Dependencies (devel packages only)
# =============================================================================

BuildRequires:  cmake ninja-build gcc-c++ git
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconf-pkg-config

# Qt6
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qt5compat-devel
BuildRequires:  qt6-qtwayland-devel
BuildRequires:  qt6-qttools-devel

# KF6
BuildRequires:  kf6-kauth-devel
BuildRequires:  kf6-kcolorscheme-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kcrash-devel
BuildRequires:  kf6-kdbusaddons-devel
BuildRequires:  kf6-kglobalaccel-devel
BuildRequires:  kf6-kio-devel
BuildRequires:  kf6-kjobwidgets-devel
BuildRequires:  kf6-kguiaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kidletime-devel
BuildRequires:  kf6-kpackage-devel
BuildRequires:  kf6-kservice-devel
BuildRequires:  kf6-ksvg-devel
BuildRequires:  kf6-kwidgetsaddons-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  kf6-kdeclarative-devel
BuildRequires:  kf6-kcmutils-devel
BuildRequires:  kf6-knewstuff-devel
BuildRequires:  kf6-kxmlgui-devel
BuildRequires:  kf6-krunner-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kirigami-devel
# xdg-desktop-portal-kwe subpackage (rebranded xdg-desktop-portal-kde)
BuildRequires:  kf6-kiconthemes-devel
BuildRequires:  kf6-kitemviews-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
BuildRequires:  kf6-kio-filewidgets-devel

# Plasma / KDE
BuildRequires:  kwayland-devel
BuildRequires:  kscreenlocker-devel
BuildRequires:  knighttime-devel
BuildRequires:  plasma-wayland-protocols-devel
BuildRequires:  plasma-activities-devel
BuildRequires:  libplasma-devel

# System / Wayland / Hardware
BuildRequires:  libepoxy-devel
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  libxkbcommon-devel
BuildRequires:  libxkbcommon-x11-devel
BuildRequires:  libinput-devel
BuildRequires:  libdrm-devel
BuildRequires:  mesa-libgbm-devel
BuildRequires:  libdisplay-info-devel
BuildRequires:  lcms2-devel
BuildRequires:  libxcvt-devel
BuildRequires:  libcanberra-devel
BuildRequires:  libevdev-devel
BuildRequires:  systemd-devel
BuildRequires:  pipewire-devel
BuildRequires:  libEGL-devel
BuildRequires:  mesa-libGLES-devel
BuildRequires:  freetype-devel
BuildRequires:  fontconfig-devel
BuildRequires:  vulkan-loader-devel
BuildRequires:  vulkan-headers

# X11 / Xwayland
BuildRequires:  libX11-devel
BuildRequires:  libxcb-devel
BuildRequires:  xcb-util-keysyms-devel
BuildRequires:  xcb-util-cursor-devel
BuildRequires:  xcb-util-devel
BuildRequires:  xcb-util-wm-devel
BuildRequires:  xcb-util-image-devel
BuildRequires:  xcb-util-renderutil-devel

# Misc
BuildRequires:  qaccessibilityclient-qt6-devel
BuildRequires:  cairo-devel
BuildRequires:  pango-devel
BuildRequires:  harfbuzz-devel
BuildRequires:  glib2-devel
BuildRequires:  pam-devel
BuildRequires:  polkit-devel
BuildRequires:  libcurl-devel
BuildRequires:  libwebp-devel
BuildRequires:  librsvg2-devel
BuildRequires:  libqalculate-devel
BuildRequires:  libxml2-devel
BuildRequires:  jemalloc-devel

# Noctalia fork shell (kineticwe-noctalia / noctalia-kwe; vendored at shell/noctalia)
BuildRequires:  meson
BuildRequires:  libsodium-devel
BuildRequires:  libsecret-devel
BuildRequires:  sdbus-cpp-devel
BuildRequires:  wireplumber-devel
BuildRequires:  libsndfile-devel
BuildRequires:  libjxl-devel
BuildRequires:  md4c-devel
BuildRequires:  tomlplusplus-devel
BuildRequires:  libical-devel
BuildRequires:  nlohmann-json-devel
BuildRequires:  stb_image_resize2-devel
BuildRequires:  stb_image_write-devel

# Runtime dependencies (for the installed package)
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtsvg
Requires:       qt6-qtwayland
Requires:       kf6-kglobalaccel
Requires:       kf6-kio
Requires:       kf6-kidletime
Requires:       kf6-ksvg
Requires:       kf6-krunner
Requires:       kf6-kirigami
Requires:       kwayland
Requires:       kscreenlocker
Requires:       knighttime
Requires:       libplasma
Requires:       plasma-workspace
Requires:       plasma-milou
Requires:       aurorae
Requires:       plasma-breeze
Requires:       kscreen
Requires:       qaccessibilityclient-qt6
Requires:       xorg-x11-server-Xwayland
Requires:       hwdata
Requires:       xdg-desktop-portal
Requires:       xdg-desktop-portal-kwe
Requires:       kineticwe-noctalia

# Bundled kdecoration runtime (pinned KDE master) replaces the system package
Provides:       kdecoration = %{version}-%{release}
Provides:       kdecoration%{?_isa} = %{version}-%{release}
Obsoletes:      kdecoration < %{version}-%{release}
Obsoletes:      kdecoration-devel < %{version}-%{release}

# Soft deps — nice to have for full experience
# (2.0: the Noctalia fork shell ships in-tree as the kineticwe-noctalia subpackage;
# the 1.x Recommends: noctalia-git is gone.)

%description
KineticWE is a fork of KWin that adds native tiling support for Wayland.
It is designed to pair with the Noctalia fork shell, which ships in-tree as
the kineticwe-noctalia subpackage (noctalia-kwe binary), to form a complete
desktop experience called "KineticWE — A Kwin Window Environment."

KineticWE bundles an in-process global shortcuts daemon (kglobalacceld)
and the kdecoration runtime libraries, both built from pinned KDE master
commits for API compatibility.

%package -n kineticwe-kglobalacceld
Summary:        KDE Global Shortcuts Daemon (for KineticWE)
License:        LGPL-2.0-or-later

%description -n kineticwe-kglobalacceld
A newer build of kglobalacceld from git master, required by KineticWE.
This provides the in-process global shortcuts daemon that kinetic-we
links against directly via the KGlobalAccelD API (keyEvent, pointerPressed,
axisTriggered, resetModifierOnlyState) which is not available in the
system kglobalacceld package (6.7.0 or older).

%package -n xdg-desktop-portal-kwe
Summary:        KineticWE portal backend (rebranded xdg-desktop-portal-kde)
License:        LGPL-2.0-or-later

%description -n xdg-desktop-portal-kwe
The xdg-desktop-portal backend for KineticWE sessions: a fork of KDE's
xdg-desktop-portal-kde (v6.7.4) rebranded to export the desktop as KineticWE.
Registers the org.freedesktop.impl.portal.desktop.kwe D-Bus service and is
selected by the freedesktop frontend via kineticwe-portals.conf
([preferred] default=kwe) when XDG_CURRENT_DESKTOP=KineticWE. All stock
interfaces are kept — FileChooser (KDE file dialog), Screenshot, ScreenCast,
Notification, Settings, GlobalShortcuts, etc. The KDE backend is never
started in a KineticWE session.

%package -n kineticwe-noctalia
Summary:        Noctalia fork shell for KineticWE (noctalia-kwe)
License:        MIT AND BSD-3-Clause AND CC0-1.0

%description -n kineticwe-noctalia
The Noctalia v5 shell fork for KineticWE, vendored at shell/noctalia and
built in-tree. The binary is renamed to noctalia-kwe so it can coexist with
an upstream noctalia install. Provides the bar, dock, launcher, control
center, notification daemon, lock screen, and settings UI; paired with the
compositor it renders the KineticWE desktop. Session identity is KineticWE
(the shell treats it like KDE for its compositor backends).

# =============================================================================
# Prep
# =============================================================================

%prep
%setup -q -n kineticwe

# Extract kglobalacceld source alongside
mkdir -p ../kglobalacceld
cd ../kglobalacceld
tar xf %{SOURCE1} --strip-components=1
cd -

# Extract kdecoration source alongside
mkdir -p ../kdecoration
cd ../kdecoration
tar xf %{SOURCE3} --strip-components=1
cd -

# Extract kconfig source alongside (for the fixed kconfig_compiler)
mkdir -p ../kconfig
cd ../kconfig
tar xf %{SOURCE2} --strip-components=1
cd -

# =============================================================================
# Build
# =============================================================================

%build

# --- Phase 1: Build and install kglobalacceld to a temporary prefix ---
KGA_TMP_PREFIX=%{_builddir}/kga-install

mkdir -p ../kglobalacceld/build
pushd ../kglobalacceld/build
%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    ..
%cmake_build
popd

# --- Phase 2: Build the fixed kconfig_compiler (build-time tool only) ---
mkdir -p ../kconfig/build
pushd ../kconfig/build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
%cmake_build --target kconfig_compiler
popd

# --- Phase 2b: Build and stage kdecoration (pinned KDE master commit) ---
# kinetic-we uses KDecoration3::Style (server-side drop shadows for CSD
# windows), which is not present in the buildroot's kdecoration 6.7.x.
mkdir -p ../kdecoration/build
pushd ../kdecoration/build
%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    ..
%cmake_build
popd

# --- Phase 3: Build kinetic-we against the kglobalacceld we just built ---
# We install kglobalacceld into a temp dir so kinetic-we's CMake can find it
mkdir -p ${KGA_TMP_PREFIX}
DESTDIR=${KGA_TMP_PREFIX} cmake --install ../kglobalacceld/build --prefix %{_prefix}
DESTDIR=${KGA_TMP_PREFIX} cmake --install ../kdecoration/build --prefix %{_prefix}

%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_PREFIX_PATH="${KGA_TMP_PREFIX}%{_prefix}" \
    -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    -DKWIN_KCONFIG_COMPILER=%{_builddir}/kconfig/build/bin/kconfig_compiler_kf6 \
    -DKWIN_BUILD_GLOBALSHORTCUTS=ON
%cmake_build

# --- Phase 4: Build the KineticWE portal backend (rebranded xdg-desktop-portal-kde) ---
PORTAL_SRC=%{_builddir}/kineticwe/portal/xdg-desktop-portal-kwe
PORTAL_BUILD=%{_builddir}/portal-build

cmake -S "${PORTAL_SRC}" \
    -B "${PORTAL_BUILD}" \
    -GNinja \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF
cmake --build "${PORTAL_BUILD}"

# --- Phase 5: Build the Noctalia fork shell (noctalia-kwe) ---
SHELL_SRC=%{_builddir}/kineticwe/shell/noctalia
SHELL_BUILD=%{_builddir}/shell-build

meson setup "${SHELL_BUILD}" "${SHELL_SRC}" \
    --prefix %{_prefix} \
    --buildtype=release \
    -Dtests=disabled
meson compile -C "${SHELL_BUILD}"

# =============================================================================
# Install
# =============================================================================

%install
# Install kglobalacceld to the buildroot
DESTDIR=%{buildroot} cmake --install ../kglobalacceld/build --prefix %{_prefix}

# Install kdecoration to the buildroot
DESTDIR=%{buildroot} cmake --install ../kdecoration/build --prefix %{_prefix}

# Install kinetic-we to the buildroot
%cmake_install

# Install the KineticWE portal backend to the buildroot
DESTDIR=%{buildroot} cmake --install %{_builddir}/portal-build --prefix %{_prefix}

# Install the Noctalia fork shell to the buildroot
DESTDIR=%{buildroot} meson install -C %{_builddir}/shell-build

# Install the 2.0 session target (pulls graphical-session.target up so the
# portal frontend unit can start; see scripts/kineticwe-session.target)
install -Dpm 0644 %{_builddir}/kineticwe/scripts/kineticwe-session.target %{buildroot}%{_userunitdir}/kineticwe-session.target

# Remove unnecessary devel files from main package (they go in -devel if needed)
rm -f %{buildroot}%{_libdir}/cmake/KGlobalAccelD/*.cmake 2>/dev/null || true
rm -rf %{buildroot}%{_libdir}/cmake/KDecoration3/ 2>/dev/null || true

# Remove .so symlinks (devel files) — prevents conflict with system kwin-devel
rm -f %{buildroot}%{_libdir}/libKGlobalAccelD.so 2>/dev/null || true
rm -f %{buildroot}%{_libdir}/libkdecorations3.so 2>/dev/null || true
rm -f %{buildroot}%{_libdir}/libkdecorations3private.so 2>/dev/null || true

# Remove header files installed by kglobalacceld, kdecoration and kinetic-we (devel content)
rm -rf %{buildroot}%{_includedir}/KGlobalAccelD/ 2>/dev/null || true
rm -rf %{buildroot}%{_includedir}/KDecoration3/ 2>/dev/null || true
rm -f %{buildroot}%{_includedir}/KF6/kdecoration3_version.h 2>/dev/null || true
rm -rf %{buildroot}%{_includedir}/kwin/ 2>/dev/null || true

# Remove knotifications6 .notifyrc file to avoid conflict with system kwin-notifications
# The directory will be owned via %dir in %files
rm -f %{buildroot}%{_datadir}/knotifications6/*.notifyrc 2>/dev/null || true

# Remove .so symlink (devel file) from libkwin — only versioned .so.* belongs in main pkg
rm -f %{buildroot}%{_libdir}/libkwin.so 2>/dev/null || true
rm -f %{buildroot}%{_libdir}/libkcmkwincommon.so 2>/dev/null || true

# =============================================================================
# Files
# =============================================================================

%files
%license LICENSES/* ../kdecoration/LICENSES/*
%doc README.md CONTRIBUTING.md
%{_bindir}/kinetic-we
%{_bindir}/kwindowprop
%{_bindir}/kinetic-we_wayland_wrapper
%{_libdir}/libkwin.so.*
%{_libdir}/libkcmkwincommon.so.*
%{_libdir}/libkdecorations3.so.*
%{_libdir}/libkdecorations3private.so.*
%{_libdir}/qt6/plugins/kwin/
%{_libdir}/qt6/plugins/plasma/kcms/
%{_libdir}/qt6/plugins/kf6/packagestructure/
%{_libdir}/qt6/qml/org/kde/kwin/
%{_libdir}/libexec/kwin*
%{_libdir}/kconf_update_bin/kwin*
%{_datadir}/kconf_update/
%dir %{_datadir}/knotifications6/
%{_datadir}/knsrcfiles/
%{_datadir}/qlogging-categories6/
%{_datadir}/kwin-wayland/
%{_datadir}/krunner/
%{_datadir}/locale/
%{_userunitdir}/plasma-kwin_wayland.service
%{_userunitdir}/kineticwe-session.target
%{_datadir}/dbus-1/interfaces/

%files -n kineticwe-kglobalacceld
%license ../kglobalacceld/LICENSES/*
%{_libdir}/libKGlobalAccelD.so.*

%files -n xdg-desktop-portal-kwe
%license portal/xdg-desktop-portal-kwe/LICENSES/*
%{_libexecdir}/xdg-desktop-portal-kwe
%{_datadir}/xdg-desktop-portal/portals/kwe.portal
%{_datadir}/xdg-desktop-portal/kineticwe-portals.conf
%{_datadir}/dbus-1/services/org.freedesktop.impl.portal.desktop.kwe.service
%{_datadir}/applications/org.freedesktop.impl.portal.desktop.kwe.desktop
%{_userunitdir}/plasma-xdg-desktop-portal-kwe.service
%{_qt6_qmldir}/org/kde/xdgdesktopportal/
%{_datadir}/locale/

%files -n kineticwe-noctalia
%license shell/noctalia/LICENSE
%{_bindir}/noctalia-kwe
%{_datadir}/noctalia/
%{_datadir}/applications/dev.noctalia.Noctalia.desktop
%{_datadir}/applications/kineticwe-settings.desktop
%{_datadir}/icons/hicolor/scalable/apps/noctalia.svg
%{_datadir}/icons/hicolor/scalable/apps/kineticwe.svg

# =============================================================================
# Changelog
# =============================================================================

%changelog
* Sat Aug 15 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-12
- New subpackage: kineticwe-noctalia — the Noctalia fork shell, built
  in-tree from shell/noctalia (Phase 5 meson build, -Dtests=disabled) and
  shipped as the noctalia-kwe binary so it can coexist with an upstream
  noctalia install. Main package now Requires kineticwe-noctalia instead of
  Recommending noctalia-git.
- Shell compositor detection treats the KineticWE desktop identity as KDE,
  so the workspace backend (org_kde_plasma_virtual_desktop) activates and
  the bar shows the virtual desktops.

* Sat Aug 15 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-11
- New subpackage: xdg-desktop-portal-kwe — rebranded fork of
  xdg-desktop-portal-kde (v6.7.4) exporting the session as KineticWE
  (org.freedesktop.impl.portal.desktop.kwe, UseIn=KineticWE,
  kineticwe-portals.conf [preferred] default=kwe). Main package now
  Requires xdg-desktop-portal-kwe instead of xdg-desktop-portal-kde.

* Tue Jul 28 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-10
- Bundle kdecoration built from pinned KDE master commit d1304925
  (KDecoration3::Style for server-side CSD drop shadows; not in 6.7.x);
  Provides/Obsoletes kdecoration and kdecoration-devel
- Pin kglobalacceld to master commit 5cc88399 for reproducible builds
- Note that lgl-kineticwe-copr/kineticwe.spec is the authoritative COPR spec

* Sat Jun 28 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-9
- Fix %%files qt6/ prefix for plugin/qml paths (required by COPR build env)
- Update URL and Source0 from codeberg.org to gitlab.com

* Sat Jun 27 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-8
- Remove unpackaged devel headers (%%{_includedir}/KGlobalAccelD/ and
  %%{_includedir}/kwin/) that caused "Installed (but unpackaged) file(s)"
  errors on COPR
- Remove libKGlobalAccelD.so symlink from main package
- Fix bogus changelog dates (Fri → Sat)

* Sat Jun 27 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-7
- Fix %%files: remove bogus qt6/ prefix from plugin/qml paths
- Add missing %%files entries: kwindowprop, kwin-we_wayland_wrapper,
  libkcmkwincommon, libexec/kwin*, kconf_update_bin/kwin*, plugins/kf6/,
  kwin-wayland/, krunner/, locale/
- Fix BuildRequires: kf6-kirigami → kf6-kirigami-devel (cmake config)
- Clean up .so symlinks (libkwin.so, libkcmkwincommon.so) from main package
- Fix knotifications6/ to use %%dir (empty after .notifyrc cleanup)

* Sat Jun 27 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-1
- Initial COPR packaging of KineticWE
- Bundled kglobalacceld from git master (post-6.7.0 API)
- Binary renamed from kwin-we_wayland to kinetic-we
