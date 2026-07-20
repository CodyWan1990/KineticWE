%global __cmake_in_source_build 0
%{!?kglobalacceld_ref:%global kglobalacceld_ref master}

Name:           kineticwe
Version:        6.7.80
Release:        9%{?dist}
Summary:        Tiling KWin Wayland compositor

License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND MIT AND BSD-3-Clause AND CC0-1.0
URL:            https://gitlab.com/theblackdon/kineticwe
Source0:        %{name}-%{version}.tar.gz
Source1:        kglobalacceld-%{kglobalacceld_ref}.tar.gz
Source2:        start-kineticwe
Source3:        kineticwe.desktop
Source4:        kglobalacceld-mask.conf

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
Recommends:     (noctalia-shell-v5 >= 5.0.0 or noctalia-git >= 5.0.0)
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
Provides:       kwin-wayland = %{version}-%{release}
Provides:       kwin-wayland%{?_isa} = %{version}-%{release}
Provides:       kglobalacceld = %{version}-%{release}
Provides:       kglobalacceld%{?_isa} = %{version}-%{release}
Obsoletes:      kwin < %{version}-%{release}
Obsoletes:      kwin-common < %{version}-%{release}
Obsoletes:      kwin-libs < %{version}-%{release}
Obsoletes:      kwin-wayland < %{version}-%{release}
Obsoletes:      kglobalacceld < %{version}-%{release}
Obsoletes:      kglobalacceld-devel < %{version}-%{release}

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
KGA_SRC=%{_builddir}/kglobalacceld
KGA_BUILD=%{_builddir}/kglobalacceld-build
KGA_TMP_PREFIX=%{_builddir}/kga-install

cmake -S "${KGA_SRC}" \
    -B "${KGA_BUILD}" \
    -GNinja \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF
cmake --build "${KGA_BUILD}"

mkdir -p "${KGA_TMP_PREFIX}"
DESTDIR="${KGA_TMP_PREFIX}" cmake --install "${KGA_BUILD}" --prefix %{_prefix}

%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_PREFIX_PATH="${KGA_TMP_PREFIX}%{_prefix}" \
    -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    -DKWIN_BUILD_GLOBALSHORTCUTS=ON
%cmake_build

%install
KGA_BUILD=%{_builddir}/kglobalacceld-build
DESTDIR=%{buildroot} cmake --install "${KGA_BUILD}" --prefix %{_prefix}
%cmake_install

install -Dpm 0755 %{SOURCE2} %{buildroot}%{_bindir}/start-kineticwe
install -Dpm 0644 %{SOURCE3} %{buildroot}%{_datadir}/wayland-sessions/kineticwe.desktop
install -Dpm 0644 %{SOURCE4} %{buildroot}%{_sysconfdir}/systemd/user/plasma-kglobalaccel.service.d/kineticwe-mask.conf

# Compatibility symlinks so the Plasma session can find its compositor
ln -s kinetic-we %{buildroot}%{_bindir}/kwin_wayland
ln -s kwin-we_wayland_wrapper %{buildroot}%{_bindir}/kwin_wayland_wrapper

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

%post
systemctl daemon-reload 2>/dev/null || true

%postun
systemctl daemon-reload 2>/dev/null || true

%files
%license LICENSES/* ../kglobalacceld/LICENSES/*
%doc README.md CONTRIBUTING.md
%{_bindir}/kinetic-we
%{_bindir}/kwin_wayland
%{_bindir}/kwin_wayland_wrapper
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
%dir %{_sysconfdir}/systemd/user/plasma-kglobalaccel.service.d/
%{_sysconfdir}/systemd/user/plasma-kglobalaccel.service.d/kineticwe-mask.conf

%changelog
* Mon Jul 20 2026 LGL <lgl@localhost> - 6.7.80-9
- Fix New Columns layout window spawning and resizing issues
- Add gap-aware column width rebalancing to ColumnsLayoutEngine
- Sync start-kineticwe session launcher with upstream power management fix:
  look up upowerd/org_kde_powerdevil across Fedora/Arch/Debian libexec
  paths instead of a hardcoded Fedora-only path, export a WAYLAND_DISPLAY
  fallback so powerdevil doesn't abort at startup, and log their output to
  files instead of discarding it

* Wed Jul 15 2026 LGL <lgl@localhost> - 6.7.80-8
- Rebuild from latest master: adds the Columns tiling layout, the AutoGrid
  layout engine, and KCM shortcuts for swapping the focused window
  up/down/left/right
- Fix ghost window left behind on masterstack when a window is moved to a
  different monitor
- Fix autogrid layout windows floating when moved with the mouse
- Sync start-kineticwe session launcher with upstream start-kineticwe.sh
  fixes: correct QT_PLUGIN_PATH to include the qt6/plugins subdirectory
  (was missing it, so KWin's own plugins could fail to load on Fedora's
  Qt6 layout), export XDG_MENU_PREFIX=plasma- (fixes kbuildsycoca6/
  Application Picker breakage), stop clobbering LD_LIBRARY_PATH with a
  stale inherited value, and pass --exit-with-session to kinetic-we with
  noctalia running in the foreground so the session ends cleanly when
  noctalia exits instead of leaving kinetic-we running detached

* Mon Jul 06 2026 LGL <lgl@localhost> - 6.7.80-7
- Start upowerd and org_kde_powerdevil from the RPM session launcher so power
  management controls are available in KineticWE sessions
- Rebuild with the latest Noctalia/KDE settings integration, including
  Noctalia shortcut registration and Noctalia color source options in the
  Tiling System Settings module

* Mon Jul 06 2026 LGL <lgl@localhost> - 6.7.80-6
- Rebuild from latest master after README/COPR/AUR docs and Arch packaging
  metadata updates

* Sat Jul 04 2026 LGL <lgl@localhost> - 6.7.80-5
- Rebuild to pick up the latest KineticWE changes from master

* Fri Jul 03 2026 LGL <lgl@localhost> - 6.7.80-4
- Rebuild to pick up the Meta+Scroll / Meta+Alt+Scroll swap option
  (InvertScrollDesktopSwitch), which was already merged before build 3 but
  missing from it because lgl-kineticwe-copr/results/kineticwe-6.7.80.tar.gz
  was stale: the Makefile only regenerates that tarball when the file is
  absent, so builds 2 and 3 silently reused the June 28 source snapshot

* Thu Jul 02 2026 LGL <lgl@localhost> - 6.7.80-3
- Fix crash in CenterTileLayoutEngine::cancelMoveWindow: removing a leaf from
  its column list without removing the matching entry from the parallel
  weight list desynced the two, causing an out-of-bounds/empty-list read in
  reflow() on a later addWindow() (segfault in kwin_wayland, kicking the
  session back to the login screen)

* Tue Jun 30 2026 LGL <lgl@localhost> - 6.7.80-2
- Fix xdg-desktop-portal-kde launch: override XDG_CURRENT_DESKTOP=KDE for
  that process so ScreenCast/Screenshot/RemoteDesktop/Clipboard/
  GlobalShortcuts/InputCapture/Background/Wallpaper portals register
  (xdg-desktop-portal-kde requires an exact "KDE" match, not a colon list)
- Obsolete kglobalacceld-devel: it requires the exact-matching system
  kglobalacceld runtime, which kineticwe already replaces, so leaving it
  installed blocks the kineticwe transaction with an unresolvable conflict

* Sun Jun 28 2026 LGL <lgl@localhost> - 6.7.80-1
- Initial COPR package: KineticWE compositor with bundled kglobalacceld runtime
- Install RPM-safe session launcher and Wayland session desktop file
- Require Noctalia v5 from the lionheartp/Hyprland COPR package set
- Keep development outputs out of the runtime package
