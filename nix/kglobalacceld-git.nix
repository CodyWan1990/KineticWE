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
    rev = "master";
    # nix-prefetch-git or leave empty then fill after first attempt
    hash = "";
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
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
