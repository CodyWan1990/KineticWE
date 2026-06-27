# =============================================================================
# KineticWE - A tiling KWin compositor for Wayland
# COPR spec file
# =============================================================================
#
# This spec builds kglobalacceld from git master first (as a sub-package),
# then builds kwin-we against it. The system kglobalacceld package is too
# old (6.7.0) and lacks the direct KGlobalAccelD API that kwin-we requires.
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
Release:        2%{?dist}
Summary:        KineticWE - A tiling KWin Wayland compositor

License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND MIT AND BSD-3-Clause AND CC0-1.0
URL:            https://codeberg.org/theblackdon/kineticwe
Source0:        %{url}/archive/v%{version}.tar.gz
Source1:        https://invent.kde.org/plasma/kglobalacceld/-/archive/master/kglobalacceld-master.tar.gz

# =============================================================================
# Build Dependencies (from install-fedora.sh, devel packages only)
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
BuildRequires:  kf6-kirigami

# Plasma / KDE
BuildRequires:  kwayland-devel
BuildRequires:  kdecoration-devel
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

# Runtime dependencies (for the installed package)
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtsvg
Requires:       qt6-qtwayland
Requires:       kf6-kglobalaccel
Requires:       kf6-kirigami
Requires:       kwayland
Requires:       kdecoration
Requires:       kscreenlocker
Requires:       xorg-x11-server-Xwayland
Requires:       hwdata

# Noctalia is a soft dependency — users must install it from lionheartp/Hyprland COPR
Recommends:     noctalia-git

%description
KineticWE is a fork of KWin that adds native tiling support for Wayland.
It is designed to pair with the Noctalia shell to form a complete desktop
experience called "KineticWE — A Kwin Window Environment."

KineticWE bundles an in-process global shortcuts daemon (kglobalacceld)
built from git master for API compatibility.

%package -n kineticwe-kglobalacceld
Summary:        KDE Global Shortcuts Daemon (for KineticWE)
License:        LGPL-2.0-or-later

%description -n kineticwe-kglobalacceld
A newer build of kglobalacceld from git master, required by KineticWE.
This provides the in-process global shortcuts daemon that kwin-we
links against directly via the KGlobalAccelD API (keyEvent, pointerPressed,
axisTriggered, resetModifierOnlyState) which is not available in the
system kglobalacceld package (6.7.0 or older).

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

# --- Phase 2: Build kwin-we against the kglobalacceld we just built ---
# We install kglobalacceld into a temp dir so kwin-we's CMake can find it
mkdir -p ${KGA_TMP_PREFIX}
DESTDIR=${KGA_TMP_PREFIX} ninja -C ../kglobalacceld/build install

%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_PREFIX_PATH="${KGA_TMP_PREFIX}%{_prefix}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    -DKWIN_BUILD_GLOBALSHORTCUTS=ON
%cmake_build

# =============================================================================
# Install
# =============================================================================

%install
# Install kglobalacceld to the buildroot
DESTDIR=%{buildroot} ninja -C ../kglobalacceld/build install

# Install kwin-we to the buildroot
%cmake_install

# Remove unnecessary devel files from main package (they go in -devel if needed)
rm -f %{buildroot}%{_libdir}/cmake/KGlobalAccelD/*.cmake 2>/dev/null || true
rm -f %{buildroot}%{_datadir}/knotifications6/*.notifyrc 2>/dev/null || true

# =============================================================================
# Files
# =============================================================================

%files
%license LICENSES/*
%doc README.md CONTRIBUTING.md
%{_bindir}/kinetic-we
%{_libdir}/libkwin.so.*
%{_libdir}/libkwin*.so.*
%{_libdir}/qt6/plugins/kwin/
%{_libdir}/qt6/plugins/org.kde.kwin.platforms/
%{_libdir}/qt6/plugins/plasma/kcms/
%{_libdir}/qt6/plugins/kf6/kded/
%{_libdir}/qt6/plugins/kf5/kded/
%{_libdir}/qt6/qml/org/kde/kwin/
%{_libdir}/qt6/qml/org/kde/plasma/private/kwin/
%{_datadir}/kconf_update/
%{_datadir}/kwin/
%{_datadir}/knotifications6/
%{_datadir}/knsrcfiles/
%{_datadir}/qlogging-categories6/
%{_datadir}/wayland-sessions/kineticwe.desktop 2>/dev/null || true
%{_userunitdir}/plasma-kwin_wayland.service
%{_datadir}/dbus-1/interfaces/

%files -n kineticwe-kglobalacceld
%license ../kglobalacceld/LICENSES/*
%{_libdir}/libKF6GlobalAccelD.so.*
%{_libdir}/libkglobalacceld.so.*

# =============================================================================
# Changelog
# =============================================================================

%changelog
* Fri Jun 27 2026 TheBlackDon <theblackdon@codeberg.org> - 6.7.80-1
- Initial COPR packaging of KineticWE
- Bundled kglobalacceld from git master (post-6.7.0 API)
- Binary renamed from kwin-we_wayland to kinetic-we
