%global __cmake_in_source_build 0
%{!?kglobalacceld_ref:%global kglobalacceld_ref master}

Name:           kineticwe
Version:        6.7.80
Release:        1%{?dist}
Summary:        Tiling KWin Wayland compositor

License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND MIT AND BSD-3-Clause AND CC0-1.0
URL:            https://codeberg.org/theblackdon/kineticwe
Source0:        %{name}-%{version}.tar.gz
Source1:        kglobalacceld-%{kglobalacceld_ref}.tar.gz
Source2:        start-kineticwe
Source3:        kineticwe.desktop

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconf-pkg-config

BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qt5compat-devel
BuildRequires:  qt6-qtwayland-devel
BuildRequires:  qt6-qttools-devel

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

BuildRequires:  kwayland-devel
BuildRequires:  kdecoration-devel
BuildRequires:  kscreenlocker-devel
BuildRequires:  knighttime-devel
BuildRequires:  plasma-wayland-protocols-devel
BuildRequires:  plasma-activities-devel
BuildRequires:  libplasma-devel

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

BuildRequires:  libX11-devel
BuildRequires:  libxcb-devel
BuildRequires:  xcb-util-keysyms-devel
BuildRequires:  xcb-util-cursor-devel
BuildRequires:  xcb-util-devel
BuildRequires:  xcb-util-wm-devel
BuildRequires:  xcb-util-image-devel
BuildRequires:  xcb-util-renderutil-devel

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

Requires:       plasma-workspace
Requires:       plasma-milou
Requires:       aurorae
Requires:       plasma-breeze
Requires:       kscreen
Requires:       (noctalia-shell-v5 >= 5.0.0 or noctalia-git >= 5.0.0)
Requires:       xorg-x11-server-Xwayland
Requires:       hwdata
Requires:       xdg-desktop-portal
Requires:       xdg-desktop-portal-kde
Requires:       bash
Requires:       systemd
Requires:       procps-ng
Requires:       psmisc
Provides:       kwin = %{version}-%{release}
Provides:       kwin%{?_isa} = %{version}-%{release}
Provides:       kwin-common = %{version}-%{release}
Provides:       kwin-common%{?_isa} = %{version}-%{release}
Provides:       kwin-libs = %{version}-%{release}
Provides:       kwin-libs%{?_isa} = %{version}-%{release}
Obsoletes:      kwin < %{version}-%{release}
Obsoletes:      kwin-common < %{version}-%{release}
Obsoletes:      kwin-libs < %{version}-%{release}

%description
KineticWE is a fork of KWin that adds native tiling support for Wayland.

This package installs the compositor, runtime plugins, helpers, and a Wayland
session entry, and the private kglobalacceld runtime library that KineticWE
links against. Noctalia v5 (noctalia-shell-v5 or noctalia-git) from the
lionheartp/Hyprland COPR is required for the full desktop shell.

%prep
%autosetup -n %{name}-%{version}

rm -rf ../kglobalacceld
mkdir -p ../kglobalacceld
tar xf %{SOURCE1} -C ../kglobalacceld --strip-components=1

%build
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

mkdir -p "${KGA_TMP_PREFIX}"
DESTDIR="${KGA_TMP_PREFIX}" cmake --install ../kglobalacceld/build --prefix %{_prefix}

%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_PREFIX_PATH="${KGA_TMP_PREFIX}%{_prefix}" \
    -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    -DKWIN_BUILD_GLOBALSHORTCUTS=ON
%cmake_build

%install
DESTDIR=%{buildroot} cmake --install ../kglobalacceld/build --prefix %{_prefix}
%cmake_install

install -Dpm 0755 %{SOURCE2} %{buildroot}%{_bindir}/start-kineticwe
install -Dpm 0644 %{SOURCE3} %{buildroot}%{_datadir}/wayland-sessions/kineticwe.desktop

# Drop development files. This COPR package intentionally ships runtime only.
rm -rf %{buildroot}%{_includedir}/KGlobalAccelD
rm -rf %{buildroot}%{_includedir}/kwin
rm -rf %{buildroot}%{_libdir}/cmake/KGlobalAccelD
rm -rf %{buildroot}%{_libdir}/cmake/KWin
rm -rf %{buildroot}%{_libdir}/cmake/KWinDBusInterface
rm -f %{buildroot}%{_libdir}/libKGlobalAccelD.so
rm -f %{buildroot}%{_libdir}/libkwin.so
rm -f %{buildroot}%{_libdir}/libkcmkwincommon.so

# Avoid owning files that conflict with Fedora's stock KWin notification data.
rm -f %{buildroot}%{_datadir}/knotifications6/*.notifyrc
rm -rf %{buildroot}%{_datadir}/dbus-1/interfaces

%files
%license LICENSES/* ../kglobalacceld/LICENSES/*
%doc README.md CONTRIBUTING.md
%{_bindir}/kinetic-we
%{_bindir}/kwindowprop
%{_bindir}/kwin-we_wayland_wrapper
%{_bindir}/start-kineticwe
%{_libdir}/libKGlobalAccelD.so.*
%{_libdir}/libkwin.so.*
%{_libdir}/libkcmkwincommon.so.*
%{_qt6_plugindir}/kwin/
%{_qt6_plugindir}/plasma/kcms/
%{_qt6_plugindir}/kf6/packagestructure/
%{_qt6_qmldir}/org/kde/kwin/
%{_libdir}/libexec/kwin*
%{_libdir}/kconf_update_bin/kwin*
%{_datadir}/applications/kcm_*.desktop
%{_datadir}/applications/org.kde.kwin.*.desktop
%{_datadir}/config.kcfg/
%{_datadir}/icons/hicolor/*/apps/kwin.*
%{_datadir}/wayland-sessions/kineticwe.desktop
%{_datadir}/kconf_update/
%dir %{_datadir}/knotifications6/
%{_datadir}/knsrcfiles/
%{_datadir}/qlogging-categories6/
%{_datadir}/kwin-wayland/
%{_datadir}/krunner/
%{_datadir}/locale/
%{_userunitdir}/plasma-kwin_wayland.service

%changelog
* Sun Jun 28 2026 LGL <lgl@localhost> - 6.7.80-1
- Initial COPR package: KineticWE compositor with bundled kglobalacceld runtime
- Install RPM-safe session launcher and Wayland session desktop file
- Require Noctalia v5 from the lionheartp/Hyprland COPR package set
- Keep development outputs out of the runtime package
