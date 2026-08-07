#!/bin/bash
# =============================================================================
# KineticWE - A Kwin Window Environment
# Session startup script
# =============================================================================
# Launches the KineticWE Wayland session. It can be run directly from a TTY
# after logging in, or invoked by a display manager / greeter.
#
# Session model: kinetic-we runs in the background and this script waits on
# it. When the compositor exits (logout, crash, or logind tearing down the
# session) the script ends and the display manager returns to the greeter.
# Noctalia runs supervised in the background so a shell crash or manual
# restart does not end the session.
#
# Older revisions exec'd kinetic-we with --exit-with-session running a
# separate payload script; that coupling stalled session teardown and left a
# black screen on logout instead of returning to the greeter.
#
# Environment:
#   INSTALL_PREFIX  - Override the installation prefix (default: $HOME/.local)
# =============================================================================

set -e

# Installation prefix for KineticWE binaries and libraries.
# Set at package/install time; when run from a raw source checkout
# it defaults to $HOME/.local.
_INSTALL_PREFIX_="@INSTALL_PREFIX@"
if [[ "${_INSTALL_PREFIX_%%@*}" == "" ]]; then
    # @INSTALL_PREFIX@ was NOT replaced by sed (raw template) —
    # use default: $HOME/.local.
    INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local}"
else
    # @INSTALL_PREFIX@ was replaced with a real path by sed —
    # use that path as the install prefix.
    INSTALL_PREFIX="${INSTALL_PREFIX:-$_INSTALL_PREFIX_}"
fi

# ---------------------------------------------------------------------------
# 1. Core paths for the custom KWin build
# ---------------------------------------------------------------------------
export PATH="$INSTALL_PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$INSTALL_PREFIX/lib64:$INSTALL_PREFIX/lib/x86_64-linux-gnu"

# KWin needs this to find its own plugins (screencast.so, screenshot.so, etc.).
# On RPM-based distros plugins live under lib64/qt6/plugins/.
export QT_PLUGIN_PATH="$INSTALL_PREFIX/lib64/qt6/plugins:$INSTALL_PREFIX/lib64/plugins"

# QML import path: ensures KWin QML modules (tiling UI, etc.) load from the
# dev build instead of falling through to the COPR system install.
export QML2_IMPORT_PATH="$INSTALL_PREFIX/lib64/qt6/qml"

# XDG data dirs: ensures KWin scripts, effects, and config data (kwin-wayland/
# scripts/, tabbox/, etc.) are found from the dev build first.
export XDG_DATA_DIRS="$INSTALL_PREFIX/share:$XDG_DATA_DIRS"

# ---------------------------------------------------------------------------
# 2. Session / Desktop identity
# ---------------------------------------------------------------------------
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=KDE
# Tell xdg-open (used by Electron apps like Discord/Firebot) that this is a
# Plasma 6 session, so it calls kde-open instead of the long-removed kfmclient.
# Without this, URL and folder clicks in Electron apps silently do nothing.
export KDE_SESSION_VERSION=6
# Marks this as a full KDE session (startplasma sets this too). KDE apps use
# it to pick their desktop file identity — e.g. System Settings uses
# systemsettings.desktop instead of kdesystemsettings.desktop, which is what
# docks/launchers need to resolve its icon correctly.
export KDE_FULL_SESSION=true
# Without this, kbuildsycoca6 fails and KDE tools can't find the application
# menu database, breaking kglobalacceld shortcut registration and the
# Application Picker (blank on Arch, Nix, etc.).
export XDG_MENU_PREFIX=plasma-

# ---------------------------------------------------------------------------
# 3. Helpers: locate portal and system executables (paths differ by distro)
# ---------------------------------------------------------------------------
find_portal() {
    local name="$1"
    for path in \
        "/usr/libexec/$name" \
        "/usr/lib/$name" \
        "$INSTALL_PREFIX/libexec/$name"
    do
        if [[ -x "$path" ]]; then
            printf '%s\n' "$path"
            return 0
        fi
    done
    command -v "$name" 2>/dev/null || true
}

# Fedora/RHEL ship helper daemons under /usr/libexec, Arch under /usr/lib,
# and Debian/Ubuntu under the multiarch libexec dir. Search all known
# locations (Fedora first, so resolution there is unchanged).
find_bin() {
    local name="$1"
    local path
    for path in \
        "/usr/libexec/$name" \
        "/usr/lib/$name" \
        "/usr/bin/$name" \
        "$INSTALL_PREFIX/libexec/$name" \
        /usr/lib/*/libexec/"$name"
    do
        if [[ -x "$path" ]]; then
            printf '%s\n' "$path"
            return 0
        fi
    done
    command -v "$name" 2>/dev/null || true
}

PORTAL_KDE="$(find_portal xdg-desktop-portal-kde)"
PORTAL="$(find_portal xdg-desktop-portal)"
POWERDEVIL="$(find_bin org_kde_powerdevil)"
UPOWERD="$(find_bin upowerd)"

# ---------------------------------------------------------------------------
# 4. Stop any competing global-shortcuts daemon
# ---------------------------------------------------------------------------
# kwin-we embeds kglobalacceld and registers the org.kde.kglobalaccel D-Bus
# service at startup (KGlobalAccelD::init).  If a Plasma session was
# previously active, the standalone plasma-kglobalaccel.service (which runs
# /usr/libexec/kglobalacceld) may still own that D-Bus name.  When that
# happens kwin-we's init() fails to register the service, m_kglobalAccel is
# reset, and ALL keyboard shortcuts stop working with the error
# "error communicating with global shortcuts service".
#
# Stop any competing daemon so kwin-we can claim the name cleanly.
systemctl --user stop plasma-kglobalaccel.service 2>/dev/null || true
pkill -x kglobalacceld 2>/dev/null || true

# ---------------------------------------------------------------------------
# 5. Make sure there is a D-Bus session bus (TTY logins may not have one)
# ---------------------------------------------------------------------------
if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
    echo "No D-Bus session bus found; wrapping in dbus-run-session..."
    exec dbus-run-session -- "$0" "$@"
fi

# Publish the pre-compositor session environment to D-Bus/systemd NOW, so
# services D-Bus-activated during compositor startup (xdg-desktop-portal-kde,
# kwalletd6, ksecretd) see XDG_CURRENT_DESKTOP & friends. WAYLAND_DISPLAY and
# DISPLAY only exist once the compositor is up; the full --all publish in
# section 7 covers those.
dbus-update-activation-environment --systemd \
    XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP \
    KDE_SESSION_VERSION KDE_FULL_SESSION XDG_MENU_PREFIX XDG_DATA_DIRS \
    2>/dev/null || true

# ---------------------------------------------------------------------------
# 6. Start kinetic-we in the background and wait for its Wayland socket
# ---------------------------------------------------------------------------
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Snapshot existing sockets so we can tell which one kinetic-we creates —
# the greeter (or a previous session) may already own wayland-0 / :0.
_before_wayland="$(ls -1 "$RUNTIME_DIR" 2>/dev/null | grep '^wayland-' || true)"
_before_x11="$(ls -1 /tmp/.X11-unix 2>/dev/null | grep '^X[0-9]' || true)"

"$INSTALL_PREFIX/bin/kinetic-we" --xwayland &
KWIN_PID=$!

# If this script dies for any reason (logout, Ctrl-C, logind teardown), make
# sure the compositor goes with it.
trap 'kill "$KWIN_PID" 2>/dev/null || true' EXIT

WAYLAND_SOCK=""
for _ in $(seq 1 20); do
    sleep 0.5
    for _sock in "$RUNTIME_DIR"/wayland-*; do
        [[ -S "$_sock" ]] || continue
        _name="$(basename "$_sock")"
        if ! grep -qx "$_name" <<< "$_before_wayland"; then
            WAYLAND_SOCK="$_name"
            break 2
        fi
    done
done

if [[ -z "$WAYLAND_SOCK" ]]; then
    echo "kinetic-we did not create a Wayland socket within 10s; aborting." >&2
    exit 1
fi

export WAYLAND_DISPLAY="$WAYLAND_SOCK"

# Best-effort: discover the XWayland display so X11 clients (Electron apps,
# etc.) work. Uses the same before/after snapshot trick as the Wayland socket.
for _ in $(seq 1 20); do
    for _x in /tmp/.X11-unix/X*; do
        [[ -S "$_x" ]] || continue
        _name="$(basename "$_x")"
        if ! grep -qx "$_name" <<< "$_before_x11"; then
            export DISPLAY=":${_name#X}"
            break 2
        fi
    done
    sleep 0.5
done

# ---------------------------------------------------------------------------
# 7. Session services (this ran as the KWin startup payload in older revisions)
# ---------------------------------------------------------------------------
mkdir -p "$HOME/.local/share"

# Publish the session environment to D-Bus/systemd-activated services.
# Without this, on-demand services (e.g. Spectacle via its global shortcut)
# start without WAYLAND_DISPLAY/DISPLAY and crash at launch:
# "could not activate remote peer org.kde.spectacle: startup job failed".
dbus-update-activation-environment --systemd --all 2>/dev/null || true

# KDE session daemons. kded6 loads plasma-pa's audioshortcutsservice module,
# which registers the volume/mute media keys under the "kmix" kglobalaccel
# component; ksmserver provides session management. Without them the media
# keys and other kded-registered global shortcuts never fire. Best-effort:
# minimal installs without plasma-workspace units skip this.
systemctl --user start plasma-kded6.service 2>/dev/null || true
systemctl --user start plasma-ksmserver.service 2>/dev/null || true

# Bring up the curated session target. This pulls graphical-session.target
# in as a dependency (the only way targets with RefuseManualStart=yes become
# active), which starts the shell-independent Plasma session services
# (KWallet PAM unlock, polkit agent, kcminit, session restore, XDG autostart,
# ...) and any user unit with WantedBy=graphical-session.target.
# Re-run the systemd user generators before bringing the session up: the
# user manager can survive quick logout/login cycles (UserStopDelaySec),
# so autostart entries added since it started would otherwise never get
# generated units and silently not start.
systemctl --user daemon-reload 2>/dev/null || true

systemctl --user start kineticwe-workspace.target 2>/dev/null || true

# Rebuild the KDE service cache so System Settings finds installed KCMs.
# Backgrounded: a cold rebuild can take 15-25s and must not stall the shell;
# nothing in the startup path depends on it having completed.
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null &
fi

# XDG Desktop Portals. The KDE backend MUST start before the generic portal
# frontend so the frontend can discover its capabilities (Screenshot,
# ScreenCast).
killall -q xdg-desktop-portal-kde xdg-desktop-portal xdg-desktop-portal-gtk xdg-document-portal 2>/dev/null || true

# Wait for a stale backend (if any) to release its D-Bus name instead of a
# fixed sleep 1; on a fresh login the name is free and this is instant.
for _ in $(seq 1 20); do
    busctl --user status org.freedesktop.impl.portal.desktop.kde >/dev/null 2>&1 || break
    sleep 0.1
done

if [[ -x "$PORTAL_KDE" ]]; then
    nohup "$PORTAL_KDE" >"$HOME/.local/share/xdg-desktop-portal-kde.log" 2>&1 &
else
    echo "Warning: xdg-desktop-portal-kde not found" >&2
fi

# Start the frontend only once the backend owns its D-Bus name, replacing
# the fixed sleep 2; normally instant. Skip the wait if busctl is missing.
if [[ -x "$PORTAL_KDE" ]] && command -v busctl >/dev/null 2>&1; then
    for _ in $(seq 1 50); do
        busctl --user status org.freedesktop.impl.portal.desktop.kde >/dev/null 2>&1 && break
        sleep 0.1
    done
fi

if [[ -x "$PORTAL" ]]; then
    nohup "$PORTAL" >"$HOME/.local/share/xdg-desktop-portal.log" 2>&1 &
else
    echo "Warning: xdg-desktop-portal not found" >&2
fi

# Power management. org_kde_powerdevil aborts at startup if WAYLAND_DISPLAY
# is unset; it is exported above.
if [[ -n "$UPOWERD" ]]; then
    nohup "$UPOWERD" >"$HOME/.local/share/upowerd.log" 2>&1 &
fi

# org_kde_powerdevil provides idle timeouts, DPMS, suspend, brightness,
# and power profile management. Where the Plasma user units exist it is
# started via plasma-powerdevil.service by kineticwe-workspace.target (the
# unit is Type=dbus and claims org.kde.Solid.PowerManagement); launch the
# binary directly only when the unit is absent (e.g. NixOS).
if systemctl --user cat plasma-powerdevil.service >/dev/null 2>&1; then
    : # started via kineticwe-workspace.target
elif [[ -n "$POWERDEVIL" ]]; then
    nohup "$POWERDEVIL" >"$HOME/.local/share/powerdevil.log" 2>&1 &
else
    echo "Warning: org_kde_powerdevil not found; power management unavailable" >&2
fi

# ---------------------------------------------------------------------------
# 8. Start the shell (supervised, in the background)
# ---------------------------------------------------------------------------
# Run noctalia under a supervisor loop: if it crashes or is killed (e.g.
# pkill noctalia to recover a broken shell), it restarts automatically
# instead of taking the whole session down. The session ends when the
# compositor exits (logout -> loginctl terminate-session, or a crash),
# which logind propagates to every process in the session.
(
    while true; do
        noctalia >"$HOME/.local/share/noctalia.log" 2>&1
        echo "noctalia exited (code $?), restarting in 2s..."
        sleep 2
    done
) &
SHELL_PID=$!

# ---------------------------------------------------------------------------
# 9. Wait on the compositor; its exit ends the session
# ---------------------------------------------------------------------------
wait "$KWIN_PID" || true

# Compositor exited — take the session services down with it. Stopping
# graphical-session.target cascades to every PartOf= unit (kded6, ksmserver,
# portals-era services, autostart apps) and via BindsTo stops our target.
# (Manual stop is allowed; RefuseManualStart only blocks manual starts.)
systemctl --user stop kineticwe-workspace.target graphical-session.target 2>/dev/null || true

# Stop the supervised shell and let the greeter return.
kill "$SHELL_PID" 2>/dev/null || true
pkill -x noctalia 2>/dev/null || true
trap - EXIT
