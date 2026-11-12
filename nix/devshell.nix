{
  pkgs,
  kineticWe,
  noctalia,
}:

pkgs.mkShell {
  name = "kineticwe-devshell";

  # Reuse the exact dependency sets both packages build with. The package
  # outputs are deliberately not referenced: `nix develop` would build them.
  nativeBuildInputs = kineticWe.nativeBuildInputs ++ noctalia.nativeBuildInputs ++ [
    pkgs.ccache
    pkgs.git
  ];
  buildInputs = kineticWe.buildInputs ++ noctalia.buildInputs;

  shellHook = ''
    export CCACHE_DIR="''${XDG_CACHE_HOME:-$HOME/.cache}/ccache"
    export CCACHE_COMPRESS=1
    export CCACHE_SLOPPINESS=random_seed
    export CMAKE_C_COMPILER_LAUNCHER=ccache
    export CMAKE_CXX_COMPILER_LAUNCHER=ccache
    export CMAKE_EXPORT_COMPILE_COMMANDS=ON
    export CMAKE_GENERATOR=Ninja
    mkdir -p "$CCACHE_DIR"

    echo "KineticWE development shell"
    echo "  source: $(pwd)"
    echo
    echo "Persistent incremental builds (only changed files recompile):"
    echo "  cmake -S . -B build-devnix -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF"
    echo "  cmake --build build-devnix"
    echo "  cmake --install build-devnix --prefix \"$HOME/.local\""
    echo
    echo "  meson setup build-devnix-noctalia shell/noctalia --prefix \"$HOME/.local\" --buildtype=release -Dtests=disabled"
    echo "  meson compile -C build-devnix-noctalia"
    echo "  meson install -C build-devnix-noctalia"
  '';
}
