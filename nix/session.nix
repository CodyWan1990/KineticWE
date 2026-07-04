{
  lib,
  stdenv,
  makeWrapper,
  kwinWe,
  noctalia,
  xdg-desktop-portal,
  xwayland,
  kdePackages,
}:

let
  # Literal dollar — needed so Nix doesn't interpret shell ${...}
  # as Nix interpolation inside the script heredoc.
  dl = "$";
in
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
Comment=KineticWE Wayland session
Type=Application
Exec=$out/bin/start-kineticwe
DesktopNames=KineticWE:KDE
DESKTOPEOF

    cat > "$out/bin/start-kineticwe" << 'SCRIPTEOF'
#!/bin/bash
# KineticWE — RPM-style session launcher (Nix-built).

KINETIC_WE="${kwinWe}/bin/kinetic-we"
export XDG_CURRENT_DESKTOP=KineticWE:KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=kineticwe
export KDE_SESSION_VERSION=6

SESSION_LOG_DIR="${dl}{XDG_RUNTIME_DIR:-/tmp}/kineticwe-${dl}USER"
mkdir -p "${dl}SESSION_LOG_DIR" 2>/dev/null || true
SESSION_LOG="${dl}SESSION_LOG_DIR/session.log"

log() {
    echo "[$(date '+%H:%M:%S')] ${dl}*" >> "${dl}SESSION_LOG"
}

log "=== KineticWE session starting ==="
log "KINETIC_WE=${dl}KINETIC_WE"

# Build the KWin startup payload (runs as KWin's child process).
STARTUP_PAYLOAD_DIR="${dl}{XDG_RUNTIME_DIR:-/tmp}/kineticwe-${dl}USER"
mkdir -p "${dl}STARTUP_PAYLOAD_DIR" 2>/dev/null || true
STARTUP_PAYLOAD="${dl}STARTUP_PAYLOAD_DIR/startup.sh"

cat > "${dl}STARTUP_PAYLOAD" << 'PAYLOADEOF'
#!/bin/bash
export XDG_SESSION_TYPE=wayland
export KDE_SESSION_VERSION=6

killall -q @xdg_desktop_portal_kde@ @xdg_desktop_portal@ xdg-desktop-portal-gtk 2>/dev/null || true
sleep 1

mkdir -p "$HOME/.local/share" 2>/dev/null || true

# xdg-desktop-portal-kde needs XDG_CURRENT_DESKTOP=KDE exactly to register
XDG_CURRENT_DESKTOP=KDE nohup @xdg_desktop_portal_kde@ >"$HOME/.local/share/xdg-desktop-portal-kde.log" 2>&1 &
sleep 2
nohup @xdg_desktop_portal@ >"$HOME/.local/share/xdg-desktop-portal.log" 2>&1 &

# Launch Noctalia shell
nohup @noctalia@ >"$HOME/.local/share/noctalia.log" 2>&1 &

# Test terminal
nohup @konsole@ >"$HOME/.local/share/konsole.log" 2>&1 &

# Give everything a moment to start
sleep 5
PAYLOADEOF

chmod +x "${dl}STARTUP_PAYLOAD"

# Stop any competing global-shortcuts daemon
systemctl --user stop plasma-kglobalaccel.service 2>/dev/null || true
pkill -x kglobalacceld 2>/dev/null || true

log "Launching: ${dl}KINETIC_WE --xwayland ${dl}STARTUP_PAYLOAD"
exec "${dl}KINETIC_WE" --xwayland "${dl}STARTUP_PAYLOAD"
SCRIPTEOF
    chmod +x "$out/bin/start-kineticwe"

    # Substitute store paths into the payload (so children of kinetic-we
    # find the binaries without relying on PATH propagation).
    substituteInPlace "$out/bin/start-kineticwe" \
      --replace "@noctalia@" "${noctalia}/bin/noctalia" \
      --replace "@konsole@" "${kdePackages.konsole}/bin/konsole" \
      --replace "@xdg_desktop_portal@" "${xdg-desktop-portal}/bin/xdg-desktop-portal" \
      --replace "@xdg_desktop_portal_kde@" "${kdePackages.xdg-desktop-portal-kde}/bin/xdg-desktop-portal-kde"

    # Wrap so xwayland is on PATH for kinetic-we at runtime.
    wrapProgram "$out/bin/start-kineticwe" \
      --prefix PATH : "${lib.makeBinPath [ xwayland ]}"

    runHook postInstall
  '';

  meta = with lib; {
    description = "KineticWE session entry (start-kineticwe launcher)";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "start-kineticwe";
  };
}
