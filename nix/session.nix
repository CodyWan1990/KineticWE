{
  lib,
  stdenv,
  makeWrapper,
  kwinWe,
  noctalia,
  xdg-desktop-portal,
  xwayland,
  kdePackages,
  psmisc,
  upower,
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

    # Desktop entry so SDDM / greetd can list KineticWE as a Wayland session.
    # The basename (KineticWE) must match passthru.providedSessions below.
    cat > "$out/share/wayland-sessions/KineticWE.desktop" << DESKTOPEOF
[Desktop Entry]
Name=KineticWE
Comment=KineticWE Wayland session
Type=Application
Exec=$out/bin/start-kineticwe
DesktopNames=KDE
DESKTOPEOF

    cat > "$out/bin/start-kineticwe" << 'SCRIPTEOF'
#!/usr/bin/env bash
# KineticWE — session launcher (Nix-built).
# Mirrors scripts/start-kineticwe.sh / the COPR RPM launcher.
set -e

KINETIC_WE="${kwinWe}/bin/kinetic-we"

export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=KDE
export KDE_SESSION_VERSION=6
# Marks this as a full KDE session (startplasma sets this too). KDE apps use
# it to pick their desktop file identity — e.g. System Settings uses
# systemsettings.desktop instead of kdesystemsettings.desktop, which is what
# docks/launchers need to resolve its icon correctly.
export KDE_FULL_SESSION=true
# Without this, kbuildsycoca6 fails on minimal installs (Arch, Nix, etc.).
export XDG_MENU_PREFIX=plasma-

# Prefer kwin-we data (effects, scripts, configs) over anything else.
export XDG_DATA_DIRS="${kwinWe}/share:${dl}{XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

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
#!/usr/bin/env bash
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=KDE
export KDE_SESSION_VERSION=6
export KDE_FULL_SESSION=true
export XDG_MENU_PREFIX=plasma-
export XDG_DATA_DIRS="@kwin_share@:${dl}{XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# KWin exports WAYLAND_DISPLAY and DISPLAY (XWayland) before launching this
# payload; the fallback guards against the payload being started other ways.
export WAYLAND_DISPLAY="${dl}{WAYLAND_DISPLAY:-wayland-0}"

# Publish the session environment to D-Bus/systemd-activated services
# (e.g. Spectacle via its global shortcut). Without this, activated services
# get no WAYLAND_DISPLAY/DISPLAY and crash: "startup job failed".
if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment --systemd --all 2>/dev/null || true
fi

# KDE session daemons: kded6 loads plasma-pa's audioshortcutsservice module
# (volume/mute keys via the "kmix" kglobalaccel component); ksmserver provides
# session management. Best-effort on systems without plasma-workspace units.
systemctl --user start plasma-kded6.service 2>/dev/null || true
systemctl --user start plasma-ksmserver.service 2>/dev/null || true

# Rebuild KDE service cache so shortcuts / System Settings find components.
# Backgrounded: a cold rebuild can take 15-25s and must not stall the shell.
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null &
fi

# Portals: KDE backend first so the frontend discovers Screenshot/ScreenCast.
killall -q xdg-desktop-portal-kde xdg-desktop-portal xdg-desktop-portal-gtk xdg-document-portal 2>/dev/null || true

# Wait for a stale backend (if any) to release its D-Bus name; instant on a
# fresh login (replaces the fixed sleep 1).
for _ in $(seq 1 20); do
    busctl --user status org.freedesktop.impl.portal.desktop.kde >/dev/null 2>&1 || break
    sleep 0.1
done

mkdir -p "$HOME/.local/share" 2>/dev/null || true

# xdg-desktop-portal-kde needs XDG_CURRENT_DESKTOP=KDE to register correctly.
XDG_CURRENT_DESKTOP=KDE nohup @xdg_desktop_portal_kde@ >"$HOME/.local/share/xdg-desktop-portal-kde.log" 2>&1 &

# Start the frontend once the backend owns its D-Bus name (replaces the
# fixed sleep 2; normally instant). Skip the wait if busctl is missing.
if command -v busctl >/dev/null 2>&1; then
    for _ in $(seq 1 50); do
        busctl --user status org.freedesktop.impl.portal.desktop.kde >/dev/null 2>&1 && break
        sleep 0.1
    done
fi
nohup @xdg_desktop_portal@ >"$HOME/.local/share/xdg-desktop-portal.log" 2>&1 &

# Power management (best-effort; paths are Nix store binaries).
# org_kde_powerdevil aborts at startup if WAYLAND_DISPLAY is unset; it is
# exported at the top of this payload.
nohup @upowerd@ >/dev/null 2>&1 &
nohup @powerdevil@ >/dev/null 2>&1 &

# Run noctalia under a supervisor loop: if it crashes or is killed it is
# restarted automatically instead of taking the whole session down. The
# session ends only when the login session itself is terminated (logout),
# which tears down this payload and KWin together. Keep the loop in the
# foreground: --exit-with-session ends the compositor when this payload
# exits.
while true; do
    @noctalia@
    echo "noctalia exited (code ${dl}?), restarting in 2s..."
    sleep 2
done
PAYLOADEOF

chmod +x "${dl}STARTUP_PAYLOAD"

# Stop any competing global-shortcuts daemon so kwin-we can own the D-Bus name.
systemctl --user stop plasma-kglobalaccel.service 2>/dev/null || true
pkill -x kglobalacceld 2>/dev/null || true

# Publish the pre-compositor session environment to D-Bus/systemd now, so
# services D-Bus-activated during compositor startup (portals, kwalletd6,
# ksecretd) see XDG_CURRENT_DESKTOP & co. The payload re-publishes the full
# environment (including WAYLAND_DISPLAY/DISPLAY) once KWin is up.
if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment --systemd XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP KDE_SESSION_VERSION KDE_FULL_SESSION XDG_MENU_PREFIX XDG_DATA_DIRS 2>/dev/null || true
fi

log "Launching: ${dl}KINETIC_WE --xwayland --exit-with-session ${dl}STARTUP_PAYLOAD"
exec "${dl}KINETIC_WE" --xwayland --exit-with-session "${dl}STARTUP_PAYLOAD"
SCRIPTEOF
    chmod +x "$out/bin/start-kineticwe"

    # Substitute store paths into the payload (so children of kinetic-we
    # find the binaries without relying on PATH propagation).
    substituteInPlace "$out/bin/start-kineticwe" \
      --replace-fail "@noctalia@" "${noctalia}/bin/noctalia" \
      --replace-fail "@kwin_share@" "${kwinWe}/share" \
      --replace-fail "@xdg_desktop_portal@" "${xdg-desktop-portal}/libexec/xdg-desktop-portal" \
      --replace-fail "@xdg_desktop_portal_kde@" "${kdePackages.xdg-desktop-portal-kde}/libexec/xdg-desktop-portal-kde" \
      --replace-fail "@upowerd@" "${upower}/libexec/upowerd" \
      --replace-fail "@powerdevil@" "${kdePackages.powerdevil}/libexec/org_kde_powerdevil"

    # Wrap so helpers (xwayland, killall, kbuildsycoca6) are on PATH.
    wrapProgram "$out/bin/start-kineticwe" \
      --prefix PATH : "${lib.makeBinPath [
        xwayland
        psmisc
        kdePackages.kservice
      ]}"

    runHook postInstall
  '';

  # Required by services.displayManager.sessionPackages — basename of the
  # wayland-sessions desktop file without .desktop.
  passthru.providedSessions = [ "KineticWE" ];

  meta = with lib; {
    description = "KineticWE session entry (start-kineticwe launcher)";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "start-kineticwe";
  };
}
