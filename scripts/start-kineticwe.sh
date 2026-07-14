#!/bin/bash
# =============================================================================
# KineticWE - A Kwin Window Environment
# Session startup script
# =============================================================================
# This script launches the KineticWE Wayland session. It can be run directly
# from a TTY after logging in, or invoked by a display manager / greeter.
#
# Environment:
#   INSTALL_PREFIX  - Override the installation prefix (default: $HOME/.local)
# =============================================================================

set -e

# Installation prefix for KineticWE binaries and libraries.
# When installed via install-fedora.sh this is set automatically.
# When run manually it defaults to $HOME/.local.
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

# ---------------------------------------------------------------------------
# 3. Helper: locate xdg-desktop-portal executables
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

PORTAL_KDE="$(find_portal xdg-desktop-portal-kde)"
PORTAL="$(find_portal xdg-desktop-portal)"

# ---------------------------------------------------------------------------
# 4. Build the KWin startup payload (runs as KWin's child)
# ---------------------------------------------------------------------------
STARTUP_PAYLOAD_DIR="${XDG_RUNTIME_DIR:-/tmp}/kineticwe-$USER"
mkdir -p "$STARTUP_PAYLOAD_DIR"
STARTUP_PAYLOAD="$STARTUP_PAYLOAD_DIR/startup.sh"

# Write payload script
cat > "$STARTUP_PAYLOAD" << EOF
#!/bin/bash
# KineticWE startup payload — run by KWin as a detached child process.

export PATH="$INSTALL_PREFIX/bin:\$PATH"
export LD_LIBRARY_PATH="$INSTALL_PREFIX/lib64:$INSTALL_PREFIX/lib/x86_64-linux-gnu"
export QT_PLUGIN_PATH="$INSTALL_PREFIX/lib64/qt6/plugins:$INSTALL_PREFIX/lib64/plugins"
export QML2_IMPORT_PATH="$INSTALL_PREFIX/lib64/qt6/qml"
export XDG_DATA_DIRS="$INSTALL_PREFIX/share:\$XDG_DATA_DIRS"
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=KDE
export KDE_SESSION_VERSION=6

# ---------------------------------------------------------------------------
# 1. Rebuild KDE service cache so System Settings finds installed KCMs
# ---------------------------------------------------------------------------
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 2. Start XDG Desktop Portals
# ---------------------------------------------------------------------------
# The KDE portal backend MUST start before the generic portal frontend so that
# the frontend can discover the backend's capabilities (Screenshot, ScreenCast).

killall -q xdg-desktop-portal-kde xdg-desktop-portal xdg-desktop-portal-gtk xdg-document-portal 2>/dev/null || true
sleep 1

mkdir -p "\$HOME/.local/share"

if [[ -x "$PORTAL_KDE" ]]; then
    nohup "$PORTAL_KDE" >"\$HOME/.local/share/xdg-desktop-portal-kde.log" 2>&1 &
else
    echo "Warning: xdg-desktop-portal-kde not found" >&2
fi

sleep 2

if [[ -x "$PORTAL" ]]; then
    nohup "$PORTAL" >"\$HOME/.local/share/xdg-desktop-portal.log" 2>&1 &
else
    echo "Warning: xdg-desktop-portal not found" >&2
fi

# ---------------------------------------------------------------------------
# 3. Start power management
# ---------------------------------------------------------------------------
# upowerd provides battery and power device monitoring via D-Bus
if [[ -x /usr/libexec/upowerd ]]; then
    /usr/libexec/upowerd &>/dev/null &
fi

# org_kde_powerdevil provides idle timeouts, DPMS, suspend, brightness,
# and power profile management
if [[ -x /usr/libexec/org_kde_powerdevil ]]; then
    /usr/libexec/org_kde_powerdevil &>/dev/null &
fi

# ---------------------------------------------------------------------------
# 4. Start user applications
# ---------------------------------------------------------------------------
noctalia
EOF

chmod +x "$STARTUP_PAYLOAD"

# ---------------------------------------------------------------------------
# 5. Stop any competing global-shortcuts daemon
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
# 6. Launch KWin
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# KWin will run the startup payload as a child process. The child inherits
# the environment captured here (including QT_PLUGIN_PATH).
exec "$INSTALL_PREFIX/bin/kinetic-we" --xwayland --exit-with-session "$STARTUP_PAYLOAD"
