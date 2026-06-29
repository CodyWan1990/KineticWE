{
  lib,
  stdenv,
  fetchFromGitLab,
  cmake,
  ninja,
  pkg-config,
  kdePackages,
  qt6,
  libX11,
}:

let
  inherit (kdePackages)
    extra-cmake-modules
    kconfig
    kcoreaddons
    kcrash
    kdbusaddons
    kglobalaccel
    kio
    kjobwidgets
    kservice
    kwindowsystem
    ;
in

stdenv.mkDerivation rec {
  pname = "kglobalacceld";
  version = "git-master";

  src = fetchFromGitLab {
    domain = "invent.kde.org";
    owner = "plasma";
    repo = "kglobalacceld";
    rev = "099b1e9dcb52ea2524718ac56f4ce3339b534873";
    hash = "sha256-jdCHpWE+MxI4tu/BV1QI+eR0NcLX5ofZd0Z6F8gGprE=";
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
    extra-cmake-modules
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative
    kconfig
    kcoreaddons
    kcrash
    kdbusaddons
    kglobalaccel
    kio
    kjobwidgets
    kservice
    kwindowsystem
    libX11
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DBUILD_TESTING=OFF"
  ];

  meta = with lib; {
    description = "KDE Global Shortcuts Daemon (git master, for KineticWE)";
    homepage = "https://invent.kde.org/plasma/kglobalacceld";
    license = licenses.lgpl2Plus;
    platforms = platforms.linux;
    mainProgram = "kglobalacceld";
  };
}
