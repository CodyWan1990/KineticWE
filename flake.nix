{
  description = "KineticWE — a KWin window environment (KWin fork) bundled with the noctalia shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    flake-utils.url = "github:numtide/flake-utils";

    # Track the upstream noctalia-shell flake. Its outputs include the
    # binary, dev shell, home-manager and NixOS modules.
    noctalia = {
      url = "github:noctalia-dev/noctalia";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { self
    , nixpkgs
    , flake-utils
    , noctalia
    }:
    {
      # ---- cross-system modules --------------------------------------------
      # Apply the overlay automatically so pkgs.kineticwe resolves when the
      # module is imported from a flake-based NixOS config.
      nixosModules.default = { ... }: {
        imports = [ ./nix/nixos-module.nix ];
        nixpkgs.overlays = [ self.overlays.default ];
      };
      homeModules.default = import ./nix/home-module.nix;

      overlays.default = final: prev: {
        kineticwe = self.packages.${prev.stdenv.hostPlatform.system}.default
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
        kinetic-we = self.packages.${prev.stdenv.hostPlatform.system}.kinetic-we
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
        kdecoration = self.packages.${prev.stdenv.hostPlatform.system}.kdecoration
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
        kglobalacceld = self.packages.${prev.stdenv.hostPlatform.system}.kglobalacceld
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
        noctalia = self.packages.${prev.stdenv.hostPlatform.system}.noctalia
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
      };
    }
    //     flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          # KWin + noctalia build a lot of native code; keep the closure
          # to linux only.
          config.allowUnfree = true;
        };

        kglobalacceldGit = pkgs.callPackage ./nix/kglobalacceld-git.nix { };
        kdecorationGit = pkgs.callPackage ./nix/kdecoration-git.nix { };

        kineticWe = pkgs.callPackage ./nix/kinetic-we.nix {
          inherit kglobalacceldGit kdecorationGit;
        };

        # The KineticWE portal backend (rebranded xdg-desktop-portal-kde).
        xdg-desktop-portal-kwe = pkgs.callPackage ./nix/portal-kwe.nix { };

        # The session package wires kinetic-we + noctalia + xdg-desktop-portal
        # together and produces `bin/start-kineticwe`.
        session = pkgs.callPackage ./nix/session.nix {
          inherit kineticWe;
          noctalia = noctalia.packages.${system}.default;
          xwayland = pkgs.xwayland;
        };
      in
      {
        # ---- packages ---------------------------------------------------------
        packages = {
          default = session;
          kinetic-we = kineticWe;
          session = session;
          kdecoration = kdecorationGit;
          kglobalacceld = kglobalacceldGit;
          noctalia = noctalia.packages.${system}.default;
          inherit xdg-desktop-portal-kwe;
        };

        # ---- apps ------------------------------------------------------------
        apps = {
          default = {
            type = "app";
            program = "${session}/bin/start-kineticwe";
            meta.description = "Start a KineticWE Wayland session (kinetic-we + noctalia)";
          };
          kinetic-we = {
            type = "app";
            program = "${kineticWe}/bin/kinetic-we";
            meta.description = "Run the kinetic-we Wayland compositor directly";
          };
        };

        # ---- dev shell -------------------------------------------------------
        devShells = {
          default = pkgs.callPackage ./nix/devshell.nix {
            inherit kineticWe;
            noctalia = noctalia.packages.${system}.default;
          };
        };
      }
    );
}
