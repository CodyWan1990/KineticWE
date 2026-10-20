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
#     - kbuildsycoca6 as a kded6 module (a one-shot rebuild now runs at login,
#       see step 4c, so newly-installed apps' global shortcuts are discovered)
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
# 1b. Config root: the compositor's settings live in ~/.config/kineticwe
#     (kineticwe.kwe, rules.kwe, kglobalaccelrc, kineticweoutputconfig.json, ...).
#     We do NOT redirect the session's XDG_CONFIG_HOME globally: doing so
#     leaked into every user application (yazi, zed, fish, kitty, ...), forcing
#     them to read/write configs inside the kineticwe root instead of the real
#     ~/.config. Only the compositor process (launched in step 5) gets the
#     redirect via a per-command env prefix; the shell, kinetic-settings,
#     D-Bus-activated services and user apps all keep the real config home.
#     The compositor natively resolves KWIN_CONFIG ("kineticwe.kwe"), rules.kwe,
#     kglobalaccelrc and kineticweoutputconfig.json inside the root, and every
#     KConfigWatcher hot-reload path keeps working. KWE_REAL_CONFIG_HOME lets
#     the compositor reach the user's real config home for shared files
#     (kdeglobals) without hardcoding ~/.config.
# ---------------------------------------------------------------------------
KWE_CONFIG_HOME="$HOME/.config/kineticwe"
export KWE_CONFIG_HOME
# The compositor's private KSycoca cache root (see step 5).
KWE_CACHE_HOME="$HOME/.cache/kineticwe"
# Defensive: if the launcher inherited a stale XDG_CONFIG_HOME / XDG_CACHE_HOME
# pointing at the kineticwe roots (e.g. exported in a shell profile during
# earlier testing, or left in the systemd user manager by an earlier 2.0 build
# that redirected the whole session), drop it so the shell and user apps get
# the real homes. KWE_REAL_* carry the real homes to the compositor, which
# hands them back to the processes it starts.
if [[ "${XDG_CONFIG_HOME:-}" == "$KWE_CONFIG_HOME" ]]; then
    unset XDG_CONFIG_HOME
fi
if [[ "${XDG_CACHE_HOME:-}" == "$KWE_CACHE_HOME" ]]; then
    unset XDG_CACHE_HOME
fi
export KWE_REAL_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
export KWE_REAL_CACHE_HOME="${XDG_CACHE_HOME:-$HOME/.cache}"
# Only clear values that point at the kineticwe roots, so a user's own
# XDG_CONFIG_HOME / XDG_CACHE_HOME in the user manager survives.
_kwe_stale=()
while IFS='=' read -r _name _value; do
    if [[ "$_name" == XDG_CONFIG_HOME && "$_value" == "$KWE_CONFIG_HOME" ]] ||
        [[ "$_name" == XDG_CACHE_HOME && "$_value" == "$KWE_CACHE_HOME" ]]; then
        _kwe_stale+=("$_name")
    fi
done < <(systemctl --user show-environment 2>/dev/null || true)
if ((${#_kwe_stale[@]})); then
    echo "kineticwe: clearing stale ${_kwe_stale[*]} from the systemd user manager" >&2
    systemctl --user unset-environment "${_kwe_stale[@]}" 2>/dev/null || true
fi
unset _kwe_stale _name _value
mkdir -p "$KWE_CONFIG_HOME"

# ---------------------------------------------------------------------------
# 2. Session / desktop identity (2.0: hard cut to KineticWE)
# ---------------------------------------------------------------------------
# 1. Locate KineticWE binaries / libraries / plugins / data
# ---------------------------------------------------------------------------
export PATH="$INSTALL_PREFIX/bin:$PATH"

# --- Prefix library/plugin/QML dirs. The libdir name depends on the distro
# --- and prefix: Fedora RPM installs to /usr with lib64, while an Arch
# --- $HOME/.local dev prefix gets plain lib. Reference only the dirs that
# --- actually exist, lib64 first, so both layouts work and a fresh build is
# --- still preferred over any stale copy.
_KWE_LIBDIRS=()
for _d in "$INSTALL_PREFIX/lib64" "$INSTALL_PREFIX/lib" "$INSTALL_PREFIX/lib/x86_64-linux-gnu"; do
    [[ -d "$_d" ]] && _KWE_LIBDIRS+=("$_d")
done
# KWE_SCOPED_ENV=1: for prefix builds whose Qt/KF6 differ from the distro's
# (e.g. Debian stable). The prefix paths below are then NOT exported to the
# session -- distro Qt apps would otherwise load the prefix's libraries/QML
# and fail. Our binaries find their libraries via RUNPATH; kinetic-we gets its
# plugin/QML dirs in-process from KWE_QT_PLUGIN_PATH / KWE_QML_IMPORT_PATH.
if [[ "${KWE_SCOPED_ENV:-0}" != 1 ]]; then
    export LD_LIBRARY_PATH="$(IFS=:; echo "${_KWE_LIBDIRS[*]}")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# Qt plugin roots. KWin discovers its plugins (including the screencast plugin
# that exports zkde_screencast_unstable_v1 -- required for portal screen
# sharing) through KPluginMetaData::findPlugins("kwin/plugins"), which searches
# QT_PLUGIN_PATH. Both the Qt6 plugin dir and the KDE plugin dir must be
# present or the screencast global is never advertised and screen sharing
# fails with "zkde_screencast_unstable_v1 does not seem to be available".
_qt_plugin_paths=()
for _d in "${_KWE_LIBDIRS[@]}"; do
    _qt_plugin_paths+=("$_d/qt6/plugins" "$_d/plugins")
done
if [[ "${KWE_SCOPED_ENV:-0}" == 1 ]]; then
    export KWE_QT_PLUGIN_PATH="$(IFS=:; echo "${_qt_plugin_paths[*]}")"
else
    export QT_PLUGIN_PATH="$(IFS=:; echo "${_qt_plugin_paths[*]}")${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi

# QML import roots. ECM installs KWin's private QML modules
# (org.kde.kwin.private.effects -- WindowHeap/WindowHeapDelegate, imported by
# Overview/Grid View -- and org.kde.kwin.private.kdecoration) under
# KDE_INSTALL_QMLDIR = <libdir>/qml, while the rest of the KDE/Qt QML stack
# lives under <libdir>/qt6/qml. Both roots must be on the import path or the
# private effects module fails to resolve and Overview/Grid View come up blank
# on a fresh install. The KDE root comes first so a fresh build wins over any
# stale copy left under qt6/qml by an older install. QML_IMPORT_PATH is the Qt 6
# name; QML2_IMPORT_PATH is the legacy name the session already exported (Qt 6
# still honors it) -- keep both in sync.
_qml_import_paths=()
for _d in "${_KWE_LIBDIRS[@]}"; do
    _qml_import_paths+=("$_d/qml" "$_d/qt6/qml")
done
if [[ "${KWE_SCOPED_ENV:-0}" == 1 ]]; then
    export KWE_QML_IMPORT_PATH="$(IFS=:; echo "${_qml_import_paths[*]}")"
else
    export QML_IMPORT_PATH="$(IFS=:; echo "${_qml_import_paths[*]}")${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}"
    export QML2_IMPORT_PATH="$(IFS=:; echo "${_qml_import_paths[*]}")${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
fi
unset _qt_plugin_paths _qml_import_paths _KWE_LIBDIRS _d
export XDG_DATA_DIRS="$INSTALL_PREFIX/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
# Traditional-WM Qt theming (Hyprland/Niri style): tell Qt apps to load the
# qt6ct platform theme plugin, which reads ~/.config/qt6ct/qt6ct.conf and
# applies the Noctalia color scheme. Both the Qt6 qt6ct plugin and the Qt5
# qt5ct plugin register the qt6ct key, so Qt5 apps stay themed too (they load
# the Qt5 plugin and read ~/.config/qt5ct/qt5ct.conf).
export QT_QPA_PLATFORMTHEME=qt6ct

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
# D-Bus-activated services at startup see the KineticWE identity. XDG_CONFIG_HOME
# is deliberately NOT published: services must keep the real config home.
#
# systemd is a hard dependency, so systemctl is always available;
# dbus-update-activation-environment lives in dbus-tools and may be absent on
# minimal images. Import into the systemd user manager directly so services
# activated through SystemdService= units inherit the session identity even
# without dbus-tools installed.
systemctl --user import-environment \
    XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP XDG_DATA_DIRS \
    2>/dev/null || true
if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment --systemd \
        XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP XDG_DATA_DIRS \
        2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 4b. Config root setup: scripts/kineticwe-config-setup (installed to
#     $INSTALL_PREFIX/lib/kineticwe/) migrates legacy KDE / flat 2.0 files into
#     the config root, prunes stray entries out of it (moved to
#     ~/.local/state/kineticwe/pruned/, never deleted), runs the one-shot
#     shortcut migrations, seeds defaults (Breeze Dark, cursor/icons, qt5ct/
#     qt6ct, Meta+T), links the shared kdeglobals into the root and keeps the
#     qt5ct/qt6ct icon theme in step. Files it changes are backed up to
#     ~/.local/state/kineticwe/backups/ first; everything it does is logged to
#     ~/.local/state/kineticwe/config-setup.log. Every step is non-fatal; run
#     it by hand with --dry-run to preview.
# ---------------------------------------------------------------------------
_kwe_setup="$INSTALL_PREFIX/lib/kineticwe/kineticwe-config-setup"
if [[ -f "$_kwe_setup" ]]; then
    python3 "$_kwe_setup" --config-root "$KWE_CONFIG_HOME" --real-config "$KWE_REAL_CONFIG_HOME" --prefix "$INSTALL_PREFIX" \
        || echo "kineticwe: config setup failed (continuing)" >&2
else
    echo "kineticwe: $_kwe_setup missing; config seeding/migrations skipped" >&2
fi
unset _kwe_setup


# ---------------------------------------------------------------------------
# 4c. Rebuild the KSycoca service cache before the compositor starts. The
#     embedded kglobalaccel daemon discovers apps that ship global shortcuts
#     (e.g. Spectacle) through KSycoca (detectAppsWithShortcuts), so a cache
#     that predates a newly-installed app means the app's keybinds never show
#     up in Kinetic Settings -> Keybinds. Plasma kept this fresh via
#     kbuildsycoca6, which KineticWE 2.0 dropped; a one-shot rebuild at login
#     is cheap and re-syncs the cache with newly installed applications.
#     --noincremental forces a full rebuild so a brand-new app is always seen.
#     The compositor keeps its own cache under KWE_CACHE_HOME (step 5), so that
#     one is rebuilt with the compositor's XDG homes; the regular ~/.cache one
#     is still rebuilt for the shell and KDE apps, as no kded keeps it fresh.
# ---------------------------------------------------------------------------
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$KWE_CONFIG_HOME" XDG_CACHE_HOME="$KWE_CACHE_HOME" \
        kbuildsycoca6 --noincremental 2>/dev/null || true
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi


# ---------------------------------------------------------------------------
# 5. Start kinetic-we (the compositor) and wait for its Wayland socket.
#    Only the compositor process gets XDG_CONFIG_HOME and XDG_CACHE_HOME
#    pointed at the kineticwe root (per-command env prefix); everything else
#    in the session keeps the real config/cache home so user apps read/write
#    ~/.config and ~/.cache as usual.  The cache redirect keeps the
#    compositor's KSycoca service cache in a private file — without it, a
#    process with a different XDG_CONFIG_HOME that shares the same cache file
#    (the filename hash covers XDG_DATA_DIRS but not XDG_CONFIG_HOME)
#    would continuously invalidate the other's db (different mimeapps.list
#    tracking), causing an infinite ksycoca rebuild ping-pong at ~2 s/cycle.
# ---------------------------------------------------------------------------
# 5a. DRM debug (output-config / HDR troubleshooting).
#     Opt-out via KWE_DRM_DEBUG=0 (default: on). When enabled we turn on the
#     compositor's kwin_wayland_drm logging category so the "Atomic modeset
#     test failed!" line together with the errno (e.g. "Invalid argument" /
#     EINVAL) is emitted whenever a modeset is rejected, and we capture the
#     compositor's stderr to a dedicated log file for easy grepping. Set
#     KWE_DRM_DEBUG=0 to return to the quiet default.
# ---------------------------------------------------------------------------
mkdir -p "$HOME/.local/share"
KWE_DRM_DEBUG="${KWE_DRM_DEBUG:-1}"
_KWE_DRM_LOG=""
if [[ "$KWE_DRM_DEBUG" != "0" ]]; then
    _KWE_DRM_LOG="$HOME/.local/share/kineticwe-drm.log"
    : > "$_KWE_DRM_LOG"
    echo "kineticwe: DRM debug on -> $_KWE_DRM_LOG (set KWE_DRM_DEBUG=0 to disable)" >&2
fi
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Snapshot existing sockets so we can tell which one kinetic-we creates — the
# greeter (or a previous session) may already own wayland-0 / :0.
_before_wayland="$(ls -1 "$RUNTIME_DIR" 2>/dev/null | grep '^wayland-' || true)"
_before_x11="$(ls -1 /tmp/.X11-unix 2>/dev/null | grep '^X[0-9]' || true)"

if [[ -n "$_KWE_DRM_LOG" ]]; then
    QT_LOGGING_RULES="kwin_wayland_drm=true" XDG_CONFIG_HOME="$KWE_CONFIG_HOME" XDG_CACHE_HOME="$KWE_CACHE_HOME" "$INSTALL_PREFIX/bin/kinetic-we" --xwayland --no-lockscreen 2>>"$_KWE_DRM_LOG" &
    KWIN_PID=$!
else
    XDG_CONFIG_HOME="$KWE_CONFIG_HOME" XDG_CACHE_HOME="$KWE_CACHE_HOME" "$INSTALL_PREFIX/bin/kinetic-we" --xwayland --no-lockscreen &
    KWIN_PID=$!
fi

# If this script dies (logout, Ctrl-C, logind teardown), take the compositor
# down, and the shell unit with the session target (a no-op before step 6b).
trap 'kill "$KWIN_PID" 2>/dev/null || true; systemctl --user stop kineticwe-session.target 2>/dev/null || true' EXIT

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
# The compositor's display sockets and Qt theming environment must reach the
# systemd user manager: D-Bus services that activate through SystemdService=
# units (Spectacle, portals, other KDE apps) inherit the user manager's
# environment — not this script's — so without this they start with no
# WAYLAND_DISPLAY/DISPLAY and without QT_QPA_PLATFORMTHEME and fall back to the
# unthemed default Qt style. systemctl is always present; dbus-update-activation-environment lives
# in dbus-tools, which is not a hard dependency.
_kwe_prefix_env_vars=(QT_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH LD_LIBRARY_PATH)
if [[ "${KWE_SCOPED_ENV:-0}" == 1 ]]; then
    # Keep the prefix paths out of the user manager, including stale values
    # left by an earlier unscoped login.
    systemctl --user unset-environment "${_kwe_prefix_env_vars[@]}" 2>/dev/null || true
    _kwe_prefix_env_vars=()
fi
systemctl --user import-environment \
    WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP \
    XDG_SESSION_TYPE XDG_DATA_DIRS \
    KWE_CONFIG_HOME KWE_REAL_CONFIG_HOME KWE_REAL_CACHE_HOME \
    QT_QPA_PLATFORMTHEME "${_kwe_prefix_env_vars[@]}" PATH \
    2>/dev/null || true
unset _kwe_prefix_env_vars
if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment --systemd --all 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 6b. Session target: pull graphical-session.target up (RefuseManualStart=yes,
#     so it can only be started by a dependency). The portal frontend unit
#     has Requisite=graphical-session.target and fails to start without it.
# ---------------------------------------------------------------------------
_kwe_user_units="${XDG_DATA_HOME:-$HOME/.local/share}/systemd/user"
if ! systemctl --user cat kineticwe-session.target >/dev/null 2>&1; then
    # Dev/raw-checkout fallback: install the unit into the user unit dir.
    mkdir -p "$_kwe_user_units"
    install -m 0644 "$INSTALL_PREFIX/share/kineticwe/kineticwe-session.target" \
        "$_kwe_user_units/" 2>/dev/null \
        || true
fi
# Prefix (dev) installs stage the shell unit, rendered for their prefix, next to
# the target. Keep the user unit dir's copy in sync with it, so a rebuilt prefix
# never runs a stale unit. Packaged installs ship it in the system unit dir.
_kwe_shell_unit="$INSTALL_PREFIX/share/kineticwe/kineticwe-shell.service"
if [[ "$INSTALL_PREFIX" != /usr && -f "$_kwe_shell_unit" ]] &&
    ! cmp -s "$_kwe_shell_unit" "$_kwe_user_units/kineticwe-shell.service"; then
    mkdir -p "$_kwe_user_units"
    install -m 0644 "$_kwe_shell_unit" "$_kwe_user_units/" 2>/dev/null || true
fi
unset _kwe_user_units _kwe_shell_unit
systemctl --user daemon-reload 2>/dev/null || true
systemctl --user start kineticwe-session.target 2>/dev/null || true

# ---------------------------------------------------------------------------
# 6bb. PipeWire media stack (screen capture + audio). The compositor's
#      screencast plugin and the xdg-desktop-portal-kwe backend need a live
#      PipeWire daemon and a session manager (wireplumber) to create and route
#      the capture node. These are user units installed by the distro's
#      pipewire/wireplumber packages; they are NOT enabled by default on a
#      fresh "from nothing" install, so the dev machine (where they were
#      enabled long ago) screenshares fine while a cold box does not. Starting
#      them here is best-effort and idempotent: any unit that is already
#      running is a no-op, and any unit that is not installed is skipped.
# ---------------------------------------------------------------------------
systemctl --user start pipewire.socket pipewire.service wireplumber.service \
    pipewire-pulse.socket pipewire-pulse.service 2>/dev/null || true

# 6c. Portals: start the freedesktop frontend so the KineticWE backend is
#     selected and D-Bus-activated under our identity. The frontend matches
#     XDG_CURRENT_DESKTOP=KineticWE against kineticwe-portals.conf
#     ([preferred] default=kwe), which resolves to the xdg-desktop-portal-kwe
#     backend (org.freedesktop.impl.portal.desktop.kwe) — never the KDE one.
#
#     --no-block: never hold up the shell on the portal. The frontend is
#     Type=dbus with TimeoutStartSec=15s, so a backend that hangs while the
#     frontend activates it would otherwise block this script for the full
#     15s before noctalia-kwe is ever launched. Nothing in the session needs
#     the portal before the shell is up, and D-Bus activation starts it on
#     demand anyway, so start the job and move on.
systemctl --user start --no-block xdg-desktop-portal.service 2>/dev/null || true

# Power management is initialized inside noctalia-kwe. No PowerDevil or
# upowerd process is started by the session launcher.

# ---------------------------------------------------------------------------
# 7. Start the shell (Noctalia fork, noctalia-kwe) as kineticwe-shell.service:
#    systemd restarts it (including Meta+Alt+R's pkill), logs it to the journal
#    (journalctl --user -u kineticwe-shell) and stops it with the session
#    target. restart, not start: a unit left running by a session that died
#    uncleanly would still point at the old WAYLAND_DISPLAY.
#    Fallback when the unit is not installed: supervise it from here, as before.
# ---------------------------------------------------------------------------
SHELL_PID=""
if ! systemctl --user restart kineticwe-shell.service 2>/dev/null; then
    echo "kineticwe: kineticwe-shell.service unavailable; supervising noctalia-kwe from the launcher" >&2
    (
        while true; do
            noctalia-kwe >>"$HOME/.local/share/noctalia-kwe.log" 2>&1
            echo "noctalia-kwe exited (code $?), restarting in 2s..." >&2
            sleep 2
        done
    ) &
    SHELL_PID=$!
fi

# ---------------------------------------------------------------------------
# 8. Wait on the compositor; its exit ends the session
# ---------------------------------------------------------------------------
wait "$KWIN_PID" || true

# Compositor exited — take the session down. Stopping the session target stops
# the shell unit (PartOf=) and lets graphical-session.target, which nothing else
# holds, go down with it; then the greeter returns.
systemctl --user stop kineticwe-session.target 2>/dev/null || true
if [[ -n "$SHELL_PID" ]]; then
    kill "$SHELL_PID" 2>/dev/null || true
    pkill -x noctalia-kwe 2>/dev/null || true
fi
trap - EXIT
