{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  kdePackages,
  qt6,
  pipewire,
  wayland,
  wayland-protocols,
  libepoxy,
  libxkbcommon,
  version ? "6.7.4",
}:

let
  inherit (kdePackages)
    kcoreaddons kconfig ki18n kglobalaccel kio kitemmodels kirigami
    qqc2-desktop-style
    knotifications kservice kwidgetsaddons kwindowsystem
    kiconthemes kstatusnotifieritem kcrash kwayland
    kpipewire
    extra-cmake-modules
    kio-fuse
    plasma-wayland-protocols
    ;
in
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-kwe";
  inherit version;

  src = lib.cleanSource ../portal/xdg-desktop-portal-kwe;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
    extra-cmake-modules
    wayland-protocols
  ];

  buildInputs = [
    # Qt6
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtwayland

    # KDE Frameworks 6
    kcoreaddons
    kconfig
    ki18n
    kglobalaccel
    kio
    kitemmodels
    kpipewire
    kirigami
    qqc2-desktop-style # org.kde.desktop style forced by the portal
    knotifications
    kservice
    kwidgetsaddons
    kwindowsystem
    kiconthemes
    kstatusnotifieritem
    kcrash
    kwayland

    # Wayland / other
    plasma-wayland-protocols
    wayland
    pipewire
    libepoxy
    kio-fuse
    libxkbcommon
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DBUILD_TESTING=OFF"
  ];

  qtQmlPrefix = "lib/qt-6";

  meta = with lib; {
    description = "KineticWE portal backend (rebranded xdg-desktop-portal-kde)";
    homepage = "https://codeberg.org/theblackdon/kineticwe";
    license = licenses.lgpl21Plus;
    platforms = platforms.linux;
    mainProgram = "xdg-desktop-portal-kwe";
  };
}