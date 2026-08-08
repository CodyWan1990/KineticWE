# NixOS module for KineticWE.
#
# Usage (flake-based configuration):
#   imports = [ inputs.kineticwe.nixosModules.default ];
#   programs.kineticwe.enable = true;
#
# The flake's nixosModules.default also applies the kineticwe overlay so
# pkgs.kineticwe / pkgs.kwin-we / pkgs.noctalia resolve automatically.
#
# This installs the session package system-wide and registers it with
# services.displayManager.sessionPackages so SDDM / greetd offer a real
# Wayland session (not an X11 wrapper from displayManager.session).
#
{ config, lib, pkgs, ... }:

{
  imports = [ ./options.nix ];

  config = lib.mkIf config.programs.kineticwe.enable {
    environment.systemPackages = [
      config.programs.kineticwe.package
      pkgs.kwin-we
      pkgs.noctalia
      pkgs.kdecoration
      pkgs.kglobalacceld
    ];

    # Register as a Wayland session. Requires passthru.providedSessions on
    # the package (see nix/session.nix). Do NOT use the legacy
    # services.xserver.displayManager.session list — that generates an X11
    # session wrapper and login fails for a Wayland compositor.
    services.displayManager.sessionPackages = [ config.programs.kineticwe.package ];

    # KDE portal backend for screenshots / screencast / file dialogs.
    xdg.portal = {
      enable = true;
      extraPortals = [ pkgs.kdePackages.xdg-desktop-portal-kde ];
      config.common.default = [ "kde" ];
    };
  };
}
