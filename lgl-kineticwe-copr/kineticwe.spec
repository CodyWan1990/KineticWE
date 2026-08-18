%global __cmake_in_source_build 0
# Pinned KDE master commits. kinetic-we needs the post-6.7 KGlobalAccelD API and
# the KDecoration3::Style enum (server-side drop shadows for CSD windows);
# neither is in a released version yet. Bump both pins on each upstream sync.
%{!?kglobalacceld_ref:%global kglobalacceld_ref 5cc88399d8e2a7951798f85127e23f73f1fa0889}
%{!?kdecoration_ref:%global kdecoration_ref d13049250c0ea1afc279aa8dc99243565c0d83e8}
# kconfig_compiler from KConfig < 6.30 generates uncompilable setters for
# enum-class kcfg entries (fixed in KConfig commit 41592cc, first released
# in 6.30). Build the fixed compiler from this pinned KConfig commit and use
# it for kcfg code generation. Drop this once the buildroot ships KConfig >= 6.30.
%{!?kconfig_compiler_ref:%global kconfig_compiler_ref 41592ccfd9748be82c83ce98912037bffb73954e}

Name:           kineticwe
Version:        6.7.80
Release:        15%{?dist}
Summary:        Tiling KWin Wayland compositor

License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND MIT AND BSD-3-Clause AND CC0-1.0
URL:            https://gitlab.com/theblackdon/kineticwe
Source0:        %{name}-%{version}.tar.gz
Source1:        kglobalacceld-%{kglobalacceld_ref}.tar.gz
Source3:        kineticwe.desktop
Source4:        kglobalacceld-mask.conf
Source5:        kconfig-%{kconfig_compiler_ref}.tar.gz
Source6:        kdecoration-%{kdecoration_ref}.tar.gz

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
# xdg-desktop-portal-kwe subpackage (rebranded xdg-desktop-portal-kde)
BuildRequires:  kf6-kiconthemes-devel
BuildRequires:  kf6-kitemviews-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
# KIOFileWidgets dev files come from kf6-kio-devel (above); this is the
# runtime library the portal links against.
BuildRequires:  kf6-kio-file-widgets

BuildRequires:  kwayland-devel
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

Requires:       plasma-workspace
Requires:       plasma-milou
Requires:       aurorae
Requires:       plasma-breeze
Requires:       kscreen
Requires:       kineticwe-noctalia
Requires:       xorg-x11-server-Xwayland
Requires:       hwdata
Requires:       xdg-desktop-portal
Requires:       xdg-desktop-portal-kwe
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
Provides:       kdecoration = %{version}-%{release}
Provides:       kdecoration%{?_isa} = %{version}-%{release}
Obsoletes:      kwin < %{version}-%{release}
Obsoletes:      kwin-common < %{version}-%{release}
Obsoletes:      kwin-libs < %{version}-%{release}
Obsoletes:      kwin-wayland < %{version}-%{release}
Obsoletes:      kglobalacceld < %{version}-%{release}
Obsoletes:      kglobalacceld-devel < %{version}-%{release}
Obsoletes:      kdecoration < %{version}-%{release}
Obsoletes:      kdecoration-devel < %{version}-%{release}

%description
KineticWE is a fork of KWin that adds native tiling support for Wayland.

This package installs the compositor, runtime plugins, helpers, and a Wayland
session entry, plus the private kglobalacceld and kdecoration runtime
libraries (built from pinned KDE master commits) that KineticWE links
against. The Noctalia fork shell ships in-tree as the kineticwe-noctalia
subpackage (noctalia-kwe binary).

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

%prep
%autosetup -n %{name}-%{version}

rm -rf ../kglobalacceld
mkdir -p ../kglobalacceld
tar xf %{SOURCE1} -C ../kglobalacceld --strip-components=1

rm -rf ../kdecoration
mkdir -p ../kdecoration
tar xf %{SOURCE6} -C ../kdecoration --strip-components=1

rm -rf ../kconfig
mkdir -p ../kconfig
tar xf %{SOURCE5} -C ../kconfig --strip-components=1

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

# Build kdecoration (pinned KDE master commit) and stage it next to
# kglobalacceld. kinetic-we uses KDecoration3::Style (server-side drop shadows
# for CSD windows), which does not exist in the buildroot's kdecoration 6.7.x.
KDECO_SRC=%{_builddir}/kdecoration
KDECO_BUILD=%{_builddir}/kdecoration-build

cmake -S "${KDECO_SRC}" \
    -B "${KDECO_BUILD}" \
    -GNinja \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF
cmake --build "${KDECO_BUILD}"

DESTDIR="${KGA_TMP_PREFIX}" cmake --install "${KDECO_BUILD}" --prefix %{_prefix}

# Build the fixed kconfig_compiler (build-time tool only, nothing is installed)
KCFG_BUILD=%{_builddir}/kconfig-build
cmake -S %{_builddir}/kconfig \
    -B "${KCFG_BUILD}" \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "${KCFG_BUILD}" --target kconfig_compiler

%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_PREFIX_PATH="${KGA_TMP_PREFIX}%{_prefix}" \
    -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    -DKWIN_KCONFIG_COMPILER=${KCFG_BUILD}/bin/kconfig_compiler_kf6 \
    -DKWIN_BUILD_GLOBALSHORTCUTS=ON
%cmake_build

# --- Phase 4: Build the KineticWE portal backend (rebranded xdg-desktop-portal-kde) ---
PORTAL_SRC=%{_builddir}/%{name}-%{version}/portal/xdg-desktop-portal-kwe
PORTAL_BUILD=%{_builddir}/portal-build

cmake -S "${PORTAL_SRC}" \
    -B "${PORTAL_BUILD}" \
    -GNinja \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF
cmake --build "${PORTAL_BUILD}"

# --- Phase 5: Build the Noctalia fork shell (noctalia-kwe) ---
SHELL_SRC=%{_builddir}/%{name}-%{version}/shell/noctalia
SHELL_BUILD=%{_builddir}/shell-build

meson setup "${SHELL_BUILD}" "${SHELL_SRC}" \
    --prefix %{_prefix} \
    --buildtype=release \
    -Dtests=disabled
meson compile -C "${SHELL_BUILD}"

%install
KGA_BUILD=%{_builddir}/kglobalacceld-build
DESTDIR=%{buildroot} cmake --install "${KGA_BUILD}" --prefix %{_prefix}
DESTDIR=%{buildroot} cmake --install %{_builddir}/kdecoration-build --prefix %{_prefix}
%cmake_install
DESTDIR=%{buildroot} cmake --install %{_builddir}/portal-build --prefix %{_prefix}
DESTDIR=%{buildroot} meson install -C %{_builddir}/shell-build

# Install the canonical session launcher from the source tree (same file the
# AUR package ships). Older revisions packaged a forked copy from SOURCES/
# that drifted out of sync (and broke logout -> greeter return).
sed -e 's|@INSTALL_PREFIX@|%{_prefix}|g' \
    %{_builddir}/%{name}-%{version}/scripts/start-kineticwe.sh \
    > %{buildroot}%{_bindir}/start-kineticwe
chmod 0755 %{buildroot}%{_bindir}/start-kineticwe
install -Dpm 0644 %{SOURCE3} %{buildroot}%{_datadir}/wayland-sessions/kineticwe.desktop
install -Dpm 0644 %{SOURCE4} %{buildroot}%{_sysconfdir}/systemd/user/plasma-kglobalaccel.service.d/kineticwe-mask.conf
install -Dpm 0644 %{_builddir}/%{name}-%{version}/scripts/kineticwe-workspace.target %{buildroot}%{_userunitdir}/kineticwe-workspace.target
install -Dpm 0644 %{_builddir}/%{name}-%{version}/scripts/kineticwe-session.target %{buildroot}%{_userunitdir}/kineticwe-session.target

# Compatibility symlinks so the Plasma session can find its compositor
ln -s kinetic-we %{buildroot}%{_bindir}/kwin_wayland
ln -s kinetic-we_wayland_wrapper %{buildroot}%{_bindir}/kwin_wayland_wrapper

# Drop development files. This COPR package intentionally ships runtime only.
rm -rf %{buildroot}%{_includedir}/KGlobalAccelD
rm -rf %{buildroot}%{_includedir}/KDecoration3
rm -f %{buildroot}%{_includedir}/KF6/kdecoration3_version.h
rm -rf %{buildroot}%{_includedir}/kwin
rm -rf %{buildroot}%{_libdir}/cmake/KGlobalAccelD
rm -rf %{buildroot}%{_libdir}/cmake/KDecoration3
rm -rf %{buildroot}%{_libdir}/cmake/KWin
rm -rf %{buildroot}%{_libdir}/cmake/KWinDBusInterface
rm -f %{buildroot}%{_libdir}/libKGlobalAccelD.so
rm -f %{buildroot}%{_libdir}/libkdecorations3.so
rm -f %{buildroot}%{_libdir}/libkdecorations3private.so
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
%license LICENSES/* ../kglobalacceld/LICENSES/* ../kdecoration/LICENSES/*
%doc README.md CONTRIBUTING.md
%{_bindir}/kinetic-we
%{_bindir}/kwin_wayland
%{_bindir}/kwin_wayland_wrapper
%{_bindir}/kwindowprop
%{_bindir}/kinetic-we_wayland_wrapper
%{_bindir}/start-kineticwe
%{_userunitdir}/kineticwe-workspace.target
%{_userunitdir}/kineticwe-session.target
%{_libdir}/libKGlobalAccelD.so.*
%{_libdir}/libkdecorations3.so.*
%{_libdir}/libkdecorations3private.so.*
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

%changelog
* Sat Aug 15 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-17
- New subpackage: kineticwe-noctalia — the Noctalia fork shell, built
  in-tree from shell/noctalia (Phase 5 meson build, -Dtests=disabled) and
  shipped as the noctalia-kwe binary so it can coexist with an upstream
  noctalia install. Main package now Requires kineticwe-noctalia instead of
  Recommending noctalia-git / noctalia-shell-v5.
- Shell compositor detection treats the KineticWE desktop identity as KDE,
  so the workspace backend (org_kde_plasma_virtual_desktop) activates and
  the bar shows the virtual desktops.

* Sat Aug 15 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-16
- New subpackage: xdg-desktop-portal-kwe — rebranded fork of
  xdg-desktop-portal-kde (v6.7.4) exporting the session as KineticWE
  (org.freedesktop.impl.portal.desktop.kwe, UseIn=KineticWE,
  kineticwe-portals.conf [preferred] default=kwe). All stock interfaces
  kept (FileChooser, Screenshot, ScreenCast, Settings, GlobalShortcuts...).
  Main package now Requires xdg-desktop-portal-kwe instead of
  xdg-desktop-portal-kde; the KDE backend is never selected in KineticWE
  sessions.

* Sat Aug 08 2026 LGL <lgl@localhost> - 6.7.80-15
- Rebuild from latest master to pick up:
  - Expose live tiling layout state on the session bus at org.kde.KWin.Tiling
    /Tiling, so the Noctalia kineticwe-layouts bar plugin and other external
    consumers can read the active output's current layout, list enabled
    layouts, and request layout switches (setLayout/cycleLayout)

* Fri Aug 07 2026 LGL <lgl@localhost> - 6.7.80-14
- Rebuild from latest master to pick up:
  - Show current tiling layout in the desktop-switch on-screen display and
    in System Settings > Desktop Effects/Virtual Desktops

* Sat Aug 01 2026 LGL <lgl@localhost> - 6.7.80-13
- Rebuild from latest master to pick up:
  - kde-output-device-v2.xml cvt event fixed to since=24 (matching the
    interface version and the client-side plasma-wayland-protocols copy);
    the previous since=23 mismatch shifted event opcodes on the wire and
    crashed System Settings > Display & Monitor on open
  - KineticWE default global shortcuts for fresh installs (Window Close:
    Meta+Q, Overview: Meta+O, Tiling Toggle Floating: Meta+V, Swap Tiled
    Window: Meta+Shift+arrows, Switch to Desktop: Meta+N, NoctaliaWindow
    Switcher: Alt+Tab, several stale upstream defaults removed); registered
    via KGlobalAccel autoloading so existing user keybind configs are
    untouched
  - Fix wallpaper showing through in the Overview effect

* Wed Jul 29 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-12
- Work around a kconfig_compiler bug (KF6 <= 6.28, i.e. every released
  Fedora/Arch build) that misgenerates the RuleSettings mutator for the
  decorationpolicy window rule and fails to compile: store the setting as
  Int with explicit casts instead of the KWin::DecorationPolicy enum class;
  the Rules object API stays fully typed, only the KConfigXT storage layer
  changes. Revert once the buildroot's KConfig is >= 6.29.

* Wed Jul 29 2026 TheBlackDon <theblackdon@gitlab.com> - 6.7.80-11
- Ship the canonical scripts/start-kineticwe.sh from the source tree as the
  session launcher instead of the forked SOURCES/ copy: it runs kinetic-we
  in the background and waits on it instead of exec'ing with
  --exit-with-session, fixing logout stalling on a black screen instead of
  returning to the greeter (noctalia logout -> loginctl terminate-session)
- Session desktop entry now declares DesktopNames=KDE, matching the
  environment the launcher exports

- Rebuild from testing merge: ship the border color source fixes (live
  updates, ActiveOnly floating windows, system-surface skip) that the
  previous source snapshot predated, and exclude fullscreen/maximized
  windows from borders and corner radius

- Bundle kdecoration built from pinned KDE master commit d1304925: kwin-we
  now uses KDecoration3::Style (server-side drop shadows for CSD windows),
  which is not in the buildroot's kdecoration 6.7.x; Provides/Obsoletes
  kdecoration and kdecoration-devel like the bundled kglobalacceld runtime
- Pin kglobalacceld to master commit 5cc88399 instead of floating master so
  SRPM builds are reproducible

- Fix system surfaces (app launcher, logout menu, and other popup/overlay
  windows) incorrectly receiving a tiling border in ActiveOnly border mode
  when focused

* Wed Jul 22 2026 LGL <lgl@localhost> - 6.7.80-10
- Supervise noctalia in the session launcher: the shell is restarted after
  a crash or manual kill instead of tearing down the whole session, which
  now ends only when the login session itself is terminated
- Fix D-Bus/systemd-activated apps (e.g. Spectacle's shortcut) crashing on
  startup by exporting WAYLAND_DISPLAY and running
  dbus-update-activation-environment --systemd --all in the payload
- Export KDE_FULL_SESSION=true so System Settings resolves its dock icon
  instead of falling back to a generic one
- Add the "Columns" layout to the per-monitor/per-desktop override
  dropdowns, where it was missing since the layout was introduced

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
