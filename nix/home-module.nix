# Home Manager module for KineticWE.
#
# Usage:
#   imports = [ inputs.kineticwe.homeModules.default ];
#   nixpkgs.overlays = [ inputs.kineticwe.overlays.default ];
#   programs.kineticwe.enable = true;   # optional: .package, .autostart
#
# This will:
#   - Put `start-kineticwe` on the user's PATH
#   - Install a wayland-sessions desktop entry under ~/.local/share
#
# Note: most display managers (SDDM) only scan the system session
# directories. Prefer the NixOS module (programs.kineticwe.enable) so the
# session appears in the greeter. The home-manager desktop file is useful
# for greeters configured to read XDG_DATA_DIRS / user data dirs.
#
{ config, lib, pkgs, ... }:

{
  imports = [ ./options.nix ];

  config = lib.mkIf config.programs.kineticwe.enable {
    home.packages = [ config.programs.kineticwe.package ];

    # wayland-sessions entry — picked up by greeters that search XDG data dirs.
    xdg.dataFile."wayland-sessions/KineticWE.desktop".text = ''
      [Desktop Entry]
      Name=KineticWE
      Comment=A Kwin Window Environment
      Type=Application
      Exec=${config.programs.kineticwe.package}/bin/start-kineticwe
      DesktopNames=KDE
    '';

    # Optional: autostart from within an existing DE (rare).
    xdg.configFile."autostart/kineticwe.desktop" = lib.mkIf config.programs.kineticwe.autostart {
      text = ''
        [Desktop Entry]
        Name=KineticWE
        Comment=Start KineticWE (kwin-we + noctalia)
        Type=Application
        Exec=${config.programs.kineticwe.package}/bin/start-kineticwe
        Terminal=false
        X-GNOME-Autostart-enabled=true
      '';
    };
  };
}
