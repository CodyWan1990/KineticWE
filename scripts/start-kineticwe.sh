#!/bin/bash
# =============================================================================
# KineticWE 2.0 - Wayland session startup
# =============================================================================
# Launches the KineticWE 2.0 Wayland session with ONLY the compositor and the
# Noctalia shell. No KDE helper services/daemons are started:
#
#   DROPPED for 2.0 (were KDE deps, no longer launched):
#     - plasma-workspace.target / plasma-* services (kded6, ksmserver,
#       kwallet-pam, polkit-agent, kcminit, restoresession, baloo, ...)
#     - kbuildsycoca6
#     - xdg-desktop-portal-kde           (replaced by the xdg-desktop-portal-kwe
#                                         fork; the freedesktop frontend
#                                         D-Bus-activates it on demand)
#     - PowerDevil (org_kde_powerdevil)  (replaced by in-shell power service)
#     - upowerd                          (replaced by in-shell power service)
#     - standalone kglobalacceld daemon  (kinetic-we embeds KGlobalAccelD)
#
#   Session identity is a hard cut to KineticWE (no KDE_SESSION_VERSION,
#   KDE_FULL_SESSION, XDG_MENU_PREFIX=plasma-).
#
# Session model: kinetic-we runs in the background and this script waits on it.
# When the compositor exits (logout, crash, or logind tearing down the session)
# the script ends and the display manager returns to the greeter. Noctalia runs
# supervised in the background so a shell crash or manual restart does not end
# the session.
#
# Running from a TTY: if no D-Bus session bus is present we wrap ourselves in
# `dbus-run-session`.
#
# Environment:
#   INSTALL_PREFIX  - Override the installation prefix (default: $HOME/.local)
# =============================================================================

set -e

# --- Installation prefix. @INSTALL_PREFIX@ is substituted at package/build
# --- time (e.g. build-rpm-dev.sh); falls back to $HOME/.local for a raw checkout.
_INSTALL_PREFIX_="@INSTALL_PREFIX@"
if [[ "${_INSTALL_PREFIX_%%@*}" == "" ]]; then
    INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local}"
else
    INSTALL_PREFIX="${INSTALL_PREFIX:-$_INSTALL_PREFIX_}"
fi

# ---------------------------------------------------------------------------
# 1. Locate KineticWE binaries / libraries / plugins / data
# ---------------------------------------------------------------------------
export PATH="$INSTALL_PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$INSTALL_PREFIX/lib64:$INSTALL_PREFIX/lib/x86_64-linux-gnu"
export QT_PLUGIN_PATH="$INSTALL_PREFIX/lib64/qt6/plugins:$INSTALL_PREFIX/lib64/plugins"
export QML2_IMPORT_PATH="$INSTALL_PREFIX/lib64/qt6/qml"
export XDG_DATA_DIRS="$INSTALL_PREFIX/share:${XDG_DATA_DIRS:-}"

# ---------------------------------------------------------------------------
# 2. Session / desktop identity (2.0: hard cut to KineticWE)
# ---------------------------------------------------------------------------
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=KineticWE
export XDG_SESSION_DESKTOP=KineticWE
# 2.0 drops the Plasma identity: NO KDE_SESSION_VERSION / KDE_FULL_SESSION /
# XDG_MENU_PREFIX=plasma-. This is intentional so we stop advertising a Plasma
# session. KDE apps fall back gracefully without them; revisit in Phase 5's
# "audit fallout" if specific apps misbehave.

# ---------------------------------------------------------------------------
# 3. Defensive: let our embedded kglobalacceld claim the D-Bus name.
#    A stale standalone daemon from a previous Plasma login would otherwise hold
#    org.kde.kglobalaccel and break keyboard shortcuts.
# ---------------------------------------------------------------------------
systemctl --user stop plasma-kglobalaccel.service 2>/dev/null || true
pkill -x kglobalacceld 2>/dev/null || true

# ---------------------------------------------------------------------------
# 4. Make sure there is a D-Bus session bus (TTY logins may not have one)
# ---------------------------------------------------------------------------
if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
    echo "No D-Bus session bus found; wrapping in dbus-run-session..." >&2
    exec dbus-run-session -- "$0" "$@"
fi

# Publish the pre-compositor session environment to D-Bus/systemd NOW so
# D-Bus-activated services at startup see the KineticWE identity.
dbus-update-activation-environment --systemd \
    XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP XDG_DATA_DIRS \
    2>/dev/null || true

# ---------------------------------------------------------------------------
# 4b. First-run config seeding: 2.0 relocated all compositor settings into
#     ~/.config/kineticwe/ (kineticwe.kwe + rules.kwe — Part 6 of the plan).
#     On first run after the 2.0 upgrade, move the legacy KDE rc files over so
#     existing settings keep applying. Full defaults seeding ships later.
# ---------------------------------------------------------------------------
if [[ ! -e "$HOME/.config/kineticwe/kineticwe.kwe" ]]; then
    mkdir -p "$HOME/.config/kineticwe"
    if [[ -e "$HOME/.config/kwinrc" ]]; then
        cp "$HOME/.config/kwinrc" "$HOME/.config/kineticwe/kineticwe.kwe"
    fi
    if [[ -e "$HOME/.config/kwinrulesrc" ]]; then
        cp "$HOME/.config/kwinrulesrc" "$HOME/.config/kineticwe/rules.kwe"
    fi
    # 2.0 merged the small kwin-read rc files into kineticwe.kwe; append their
    # settings (KConfig merges duplicate group sections).
    for _legacy in kaccessrc kcminputrc kxkbrc klaunchrc plasmarc; do
        if [[ -e "$HOME/.config/$_legacy" ]]; then
            cat "$HOME/.config/$_legacy" >> "$HOME/.config/kineticwe/kineticwe.kwe"
        fi
    done
fi

# ---------------------------------------------------------------------------
# 5. Start kinetic-we (the compositor) and wait for its Wayland socket
# ---------------------------------------------------------------------------
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Snapshot existing sockets so we can tell which one kinetic-we creates — the
# greeter (or a previous session) may already own wayland-0 / :0.
_before_wayland="$(ls -1 "$RUNTIME_DIR" 2>/dev/null | grep '^wayland-' || true)"
_before_x11="$(ls -1 /tmp/.X11-unix 2>/dev/null | grep '^X[0-9]' || true)"

"$INSTALL_PREFIX/bin/kinetic-we" --xwayland &
KWIN_PID=$!

# If this script dies (logout, Ctrl-C, logind teardown), take the compositor down.
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

# Best-effort: discover the XWayland display so X11 clients (Electron apps, etc.)
# work. Same before/after snapshot trick as the Wayland socket.
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
# 6. Publish the full session environment to D-Bus / systemd
# ---------------------------------------------------------------------------
mkdir -p "$HOME/.local/share"
dbus-update-activation-environment --systemd --all 2>/dev/null || true

# ---------------------------------------------------------------------------
# 6b. Session target: pull graphical-session.target up (RefuseManualStart=yes,
#     so it can only be started by a dependency). The portal frontend unit
#     has Requisite=graphical-session.target and fails to start without it.
# ---------------------------------------------------------------------------
if ! systemctl --user cat kineticwe-session.target >/dev/null 2>&1; then
    # Dev/raw-checkout fallback: install the unit into the user unit dir.
    mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/systemd/user"
    install -m 0644 "$INSTALL_PREFIX/share/kineticwe/kineticwe-session.target" \
        "${XDG_DATA_HOME:-$HOME/.local/share}/systemd/user/" 2>/dev/null \
        || true
fi
systemctl --user daemon-reload 2>/dev/null || true
systemctl --user start kineticwe-session.target 2>/dev/null || true

# 6c. Portals: start the freedesktop frontend so the KineticWE backend is
#     selected and D-Bus-activated under our identity. The frontend matches
#     XDG_CURRENT_DESKTOP=KineticWE against kineticwe-portals.conf
#     ([preferred] default=kwe), which resolves to the xdg-desktop-portal-kwe
#     backend (org.freedesktop.impl.portal.desktop.kwe) — never the KDE one.
systemctl --user start xdg-desktop-portal.service 2>/dev/null || true

# 2.0 deferred hooks (add here as they land, no KDE deps):
#   - In-shell power service (replaces PowerDevil / upowerd).

# ---------------------------------------------------------------------------
# 7. Start the shell (Noctalia fork, noctalia-kwe) — supervised, in the background
# ---------------------------------------------------------------------------
(
    while true; do
        noctalia-kwe >>"$HOME/.local/share/noctalia.log" 2>&1
        echo "noctalia-kwe exited (code $?), restarting in 2s..." >&2
        sleep 2
    done
) &
SHELL_PID=$!

# ---------------------------------------------------------------------------
# 8. Wait on the compositor; its exit ends the session
# ---------------------------------------------------------------------------
wait "$KWIN_PID" || true

# Compositor exited — take the session down. (No plasma-workspace.target to stop
# in 2.0; just stop the supervised shell and let the greeter return.)
kill "$SHELL_PID" 2>/dev/null || true
pkill -x noctalia-kwe 2>/dev/null || true
trap - EXIT

