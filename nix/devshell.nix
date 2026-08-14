{
  lib,
  stdenv,
  cmake,
  ninja,
  just,
  meson,
  pkg-config,
  kdePackages,
  kineticWe,
  noctalia,
}:

stdenv.mkDerivation {
  pname = "kineticwe-devshell";
  version = "unstable";

  src = lib.cleanSource ../.;

  dontConfigure = true;
  dontBuild = true;
  dontInstall = true;

  nativeBuildInputs = [
    cmake
    ninja
    just
    meson
    pkg-config
    kdePackages.extra-cmake-modules
  ];

  # Build env: kinetic-we + every KDE/Plasma dep it needs at build time,
  # plus the noctalia binary so `just configure && just build` works.
  buildInputs = kdePackages.kwin.buildInputs or [];

  shellHook = ''
    echo "KineticWE development shell"
    echo "  kinetic-we source: $(pwd)"
    echo "  kinetic-we:    ${kineticWe}/bin/kinetic-we"
    echo "  noctalia:      ${noctalia}/bin/noctalia"
    echo
    echo "Build & install into a custom prefix:"
    echo "  just configure release \"\$HOME/.local\""
    echo "  just build release"
    echo "  just install release"
    echo
    echo "Or build via the Nix flake:"
    echo "  nix build .#kinetic-we"
    echo "  nix build .#session"
  '';
}
