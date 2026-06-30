# NixOS module for KineticWE.
#
# Usage (configuration.nix):
#   {
#     imports = [ inputs.kineticwe.nixosModules.default ];
#     programs.kineticwe.enable = true;
#   }
#
# This installs the session package system-wide and drops a
# wayland-sessions desktop entry so SDDM / greetd can offer
# "KineticWE" to all users.
#
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.kineticwe;
in
{
  options.programs.kineticwe = {
    enable = lib.mkEnableOption "KineticWE — KWin window environment with noctalia";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.kineticwe or cfg.package;
      defaultText = lib.literalExpression "pkgs.kineticwe";
      description = ''
        The KineticWE session package providing `bin/start-kineticwe`.
        Typically the output of the kineticwe flake.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    # Register the session with the display manager so SDDM / greetd
    # picks up share/wayland-sessions/kineticwe.desktop from the package.
    services.xserver.displayManager.sessionPackages = [ cfg.package ];
  };
}
