# Shared `programs.kineticwe` option definitions.
#
# Both the NixOS module and the Home Manager module import this so the option
# surface is defined exactly once — `programs.kineticwe.enable` is the single
# switch that installs everything (kinetic-we + session + decorations).
#
# Usage (either context):
#   programs.kineticwe.enable = true;
#   # optional:
#   # programs.kineticwe.package = pkgs.kineticwe;
#   # programs.kineticwe.autostart = true;
{ lib, pkgs, ... }:

{
  options.programs.kineticwe = {
    enable = lib.mkEnableOption "KineticWE — KWin window environment with noctalia";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.kineticwe or (throw ''
        programs.kineticwe.package is not set and pkgs.kineticwe is not available.
        Did you forget to apply the kineticwe overlay? Use
        inputs.kineticwe.nixosModules.default / homeModules.default (which apply
        it), or add `kineticwe.overlays.default` to `nixpkgs.overlays`, or set
        programs.kineticwe.package explicitly.
      '');
      defaultText = lib.literalExpression "pkgs.kineticwe";
      description = ''
        The KineticWE session package providing `bin/start-kineticwe`.
        Typically the output of the kineticwe flake.
      '';
    };

    autostart = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = ''
        Whether to autostart KineticWE from the graphical session. Most users
        should leave this `false` and pick "KineticWE" from the display manager
        instead.
      '';
    };
  };
}
