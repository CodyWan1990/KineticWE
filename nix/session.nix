{
  lib,
  stdenv,
  makeWrapper,
  kwinWe,
  noctalia,
  coreutils,
  findutils,
  gnused,
  gnugrep,
  systemd,
  xdg-desktop-portal,
  kdePackages,
}:
stdenv.mkDerivation {
  pname = "kineticwe-session";
  version = "unstable";

  dontUnpack = true;
  dontConfigure = true;
  dontBuild = true;

  nativeBuildInputs = [ makeWrapper ];

  installPhase = ''
    runHook preInstall

    mkdir -p "$out/bin" "$out/share/wayland-sessions"

    # Desktop entry so SDDM / greetd can list KineticWE.
    cat > "$out/share/wayland-sessions/KineticWE.desktop" << DESKTOPEOF
[Desktop Entry]
Name=KineticWE
Comment=A Kwin Window Environment
Type=Application
Exec=$out/bin/start-kineticwe
DesktopNames=KDE
DESKTOPEOF

    cat > "$out/bin/start-kineticwe" << 'SCRIPTEOF'
#!/bin/bash
# KineticWE — session startup script (Nix-built).
set -e

KWIN_WE_WRAPPER="${kwinWe}/bin/kwin-we_wayland_wrapper"
NOCTALIA_BIN="${noctalia}/bin/noctalia"

export XDG_CURRENT_DESKTOP="''${XDG_CURRENT_DESKTOP:-KDE}"
export XDG_SESSION_TYPE="''${XDG_SESSION_TYPE:-wayland}"
export XDG_SESSION_DESKTOP="''${XDG_SESSION_DESKTOP:-KDE}"

# Launch kwin-we via the wayland wrapper with noctalia as the
# client application. --xwayland enables XWayland support.
exec "$KWIN_WE_WRAPPER" --xwayland "$NOCTALIA_BIN"
SCRIPTEOF
    chmod +x "$out/bin/start-kineticwe"

    # Wrap so noctalia and portal binaries are on PATH.
    wrapProgram "$out/bin/start-kineticwe" \
      --prefix PATH : "${lib.makeBinPath [ noctalia xdg-desktop-portal kdePackages.xdg-desktop-portal-kde ]}"

    runHook postInstall
  '';

  meta = with lib; {
    description = "KineticWE session entry (start-kineticwe wrapper)";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "start-kineticwe";
  };
}
