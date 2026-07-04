{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  python3,
  kdePackages,           # all KF6 / Plasma / KDE deps live here
  kglobalacceldGit,      # git master build (replaces system kglobalacceld)
  # Qt6
  qt6,
  # Other libraries
  libei,
  libinput,
  libevdev,
  libdrm,
  mesa,
  libdisplay-info,
  lcms2,
  libxcvt,
  libcanberra,
  pipewire,
  wayland,
  wayland-protocols,
  systemd,
  sdbus-cpp,
  xwayland,
  libX11,
  libxcb,
  xcbutilkeysyms,
  xcbutilcursor,
  xcbutilwm,
  xcbutilimage,
  xcbutilrenderutil,
  polkit,
  pam,
  jemalloc,
  version ? "git",
}:

let
  inherit (kdePackages)
    kauth kcolorscheme kconfig kcoreaddons kcrash kdbusaddons
    kglobalaccel kguiaddons ki18n kidletime
    kcmutils knewstuff knotifications kpackage krunner
    kservice ksvg kwidgetsaddons kwindowsystem kdeclarative kxmlgui
    kirigami
    kdecoration kscreenlocker kwayland knighttime
    libplasma plasma-activities plasma-wayland-protocols
    aurorae breeze breeze-icons milou
    libqaccessibilityclient
    ;
in
stdenv.mkDerivation {
  pname = "kwin-we";
  inherit version;

  src = lib.cleanSource ../.;

  patches = [
    # Allow the KWin QPA plugin to recognise the binary when wrapped
    # by Nix's wrapQtAppsHook (which renames it to .kinetic-we-wrapped).
    ./patches/kwin-we-qpa-nixos-wrapper.patch
  ];

  # Scripts in the KWin source use #!/usr/bin/env python3; patchShebangs
  # in stdenv can miss them if coreutils (env) is missing from the sandbox.
  postPatch = ''
    patchShebangs src/plugins/
  '';

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    python3
    qt6.wrapQtAppsHook
    kdePackages.extra-cmake-modules
  ];

  buildInputs = [
    # Qt6
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg
    qt6.qt5compat
    qt6.qtwayland
    qt6.qttools

    # KDE Frameworks 6 + Kirigami
    kauth
    kcolorscheme
    kconfig
    kcoreaddons
    kcrash
    kdbusaddons
    kglobalaccel
    kglobalacceldGit
    kguiaddons
    ki18n
    kidletime
    kcmutils
    knewstuff
    knotifications
    kpackage
    krunner
    kservice
    ksvg
    kwidgetsaddons
    kwindowsystem
    kdeclarative
    kxmlgui
    kirigami

    # Plasma / Wayland
    kdecoration
    kscreenlocker
    kwayland
    knighttime
    libplasma
    plasma-activities
    plasma-wayland-protocols
    aurorae
    breeze
    breeze-icons
    milou

    # Other
    libqaccessibilityclient
    libei
    libinput
    libevdev
    libdrm
    mesa
    libdisplay-info
    lcms2
    libxcvt
    libcanberra
    pipewire
    wayland
    wayland-protocols
    systemd
    sdbus-cpp
    polkit
    pam
    jemalloc

    # X11 / Xwayland
    libX11
    libxcb
    xcbutilkeysyms
    xcbutilcursor
    xcbutilwm
    xcbutilimage
    xcbutilrenderutil
    xwayland
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DBUILD_TESTING=OFF"
  ];

  # TZDIR may be unset when running through the wrapper, but we need
  # it for the lockscreen clock to render.
  qtWrapperArgs = [
    "--set-default TZDIR /etc/zoneinfo"
  ];

  # KDE cmake installs QML modules under <lib>/qt-6/qml and plugins
  # under <lib>/qt-6/plugins. Tell wrapQtAppsHook where to find them.
  qtQmlPrefix = "lib/qt-6";

  meta = with lib; {
    description = "KineticWE — KWin window environment with native tiling";
    homepage = "https://codeberg.org/theblackdon/kineticwe";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "kinetic-we";
  };
}
