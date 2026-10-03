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
#       see step 4f, so newly-installed apps' global shortcuts are discovered)
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
# Defensive: if the launcher inherited a stale XDG_CONFIG_HOME pointing at the
# kineticwe root (e.g. exported in a shell profile during earlier testing),
# treat the real config home as ~/.config, not the root.
if [[ "${XDG_CONFIG_HOME:-}" == "$KWE_CONFIG_HOME" ]]; then
    export KWE_REAL_CONFIG_HOME="$HOME/.config"
else
    export KWE_REAL_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
fi
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
    export QT_PLUGIN_PATH="$(IFS=:; echo "${_qt_plugin_paths[*]}")"
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
    export QML_IMPORT_PATH="$(IFS=:; echo "${_qml_import_paths[*]}")"
    export QML2_IMPORT_PATH="$QML_IMPORT_PATH"
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
# 4b. First-run config seeding: 2.0 relocated all compositor settings into
#     the config root ($KWE_CONFIG_HOME = ~/.config/kineticwe/;
#     kineticwe.kwe + rules.kwe — Part 6 of the plan). On first run after the
#     2.0 upgrade, move the legacy KDE rc files over so existing settings keep
#     applying. Full defaults seeding ships later.
# ---------------------------------------------------------------------------
if [[ ! -e "$KWE_CONFIG_HOME/kineticwe.kwe" ]]; then
    if [[ -e "$HOME/.config/kwinrc" ]]; then
        cp "$HOME/.config/kwinrc" "$KWE_CONFIG_HOME/kineticwe.kwe"
    fi
    if [[ -e "$HOME/.config/kwinrulesrc" ]]; then
        cp "$HOME/.config/kwinrulesrc" "$KWE_CONFIG_HOME/rules.kwe"
    fi
    # 2.0 merged the small kwin-read rc files into kineticwe.kwe; append their
    # settings (KConfig merges duplicate group sections).
    for _legacy in kaccessrc kcminputrc kxkbrc klaunchrc plasmarc; do
        if [[ -e "$HOME/.config/$_legacy" ]]; then
            cat "$HOME/.config/$_legacy" >> "$KWE_CONFIG_HOME/kineticwe.kwe"
        fi
    done
fi

# Seed minimal qt5ct/qt6ct configs pointing at the Noctalia color scheme, so Qt
# apps (which load these via QT_QPA_PLATFORMTHEME=qt6ct) are themed out of the
# box. Only created when absent so a user's existing config (fonts, interface
# tweaks) is never clobbered. The color scheme files themselves are regenerated
# by the shell's theme templates whenever the palette changes (the Qt
# color-scheme template).
for _qttool in qt5ct qt6ct; do
    _qtconf="${HOME}/.config/${_qttool}/${_qttool}.conf"
    if [[ ! -e "$_qtconf" ]]; then
        mkdir -p "$(dirname "$_qtconf")"
        cat > "$_qtconf" << EOF
[Appearance]
color_scheme_path=${HOME}/.config/${_qttool}/colors/noctalia.conf
custom_palette=true
icon_theme=Breeze-Round-Chameleon Dark Icons
standard_dialogs=xdgdesktopportal
style=Breeze
EOF
    fi
done

# ---------------------------------------------------------------------------
# 4c. One-time migration: earlier 2.0 builds resolved KWIN_CONFIG against the
#     unredirected ~/.config, leaving compositor runtime state in FLAT
#     ~/.config/kineticwe.kwe + rules.kwe that the redirected compositor would
#     no longer see. Fold those files into the new root (appending only groups
#     the canonical file lacks), then retire the flat leftovers. Idempotent:
#     the flat files are gone after the first run.
# ---------------------------------------------------------------------------
if [[ -e "$HOME/.config/kineticwe.kwe" ]]; then
    if [[ ! -e "$KWE_CONFIG_HOME/kineticwe.kwe" ]]; then
        cp "$HOME/.config/kineticwe.kwe" "$KWE_CONFIG_HOME/kineticwe.kwe"
    else
        { printf '\n'; awk '
            FNR == NR { if ($0 ~ /^\[/) have[$0] = 1; next }
            /^\[/     { keep = !($0 in have); if (keep) print; next }
                      { if (keep) print }
        ' "$KWE_CONFIG_HOME/kineticwe.kwe" "$HOME/.config/kineticwe.kwe"; } \
            >> "$KWE_CONFIG_HOME/kineticwe.kwe"
    fi
    rm -f "$HOME/.config/kineticwe.kwe"
fi
if [[ -e "$HOME/.config/rules.kwe" && ! -e "$KWE_CONFIG_HOME/rules.kwe" ]]; then
    cp "$HOME/.config/rules.kwe" "$KWE_CONFIG_HOME/rules.kwe"
fi
rm -f "$HOME/.config/rules.kwe"
for _shared in kglobalaccelrc kineticweoutputconfig.json; do
    if [[ -e "$HOME/.config/$_shared" && ! -e "$KWE_CONFIG_HOME/$_shared" ]]; then
        cp "$HOME/.config/$_shared" "$KWE_CONFIG_HOME/$_shared"
    fi
done
# 2.0 renamed the output config (kwinoutputconfig.json ->
# kineticweoutputconfig.json). Carry over the in-root 2.0 file, or the legacy
# flat ~/.config file, so the user's monitor layout survives the upgrade.
if [[ ! -e "$KWE_CONFIG_HOME/kineticweoutputconfig.json" ]]; then
    if [[ -e "$KWE_CONFIG_HOME/kwinoutputconfig.json" ]]; then
        mv "$KWE_CONFIG_HOME/kwinoutputconfig.json" "$KWE_CONFIG_HOME/kineticweoutputconfig.json"
    elif [[ -e "$HOME/.config/kwinoutputconfig.json" ]]; then
        cp "$HOME/.config/kwinoutputconfig.json" "$KWE_CONFIG_HOME/kineticweoutputconfig.json"
    fi
fi

# ---------------------------------------------------------------------------
# 4d. Prune stale user-app state from the config root. Earlier 2.0 builds
#     redirected XDG_CONFIG_HOME for the whole session, so user applications
#     (yazi, zed, fish, kitty, ...) wrote their configs inside the root. With
#     the compositor-only redirect those entries are dead weight — apps now
#     read the real $KWE_REAL_CONFIG_HOME, so the copies would silently drift.
#     Keep only compositor-owned files; delete the rest. The real configs in
#     $KWE_REAL_CONFIG_HOME are untouched. Idempotent.
#
#     Note: keybinds.kwe AND custom-keybinds.kwe are both compositor-owned —
#     keybinds.kwe is the portable keybinds mirror (chords) and
#     custom-keybinds.kwe is the kinetic-settings register of the user-added
#     Applications / Custom Commands actions (component + action + exec).
#     If custom-keybinds.kwe is dropped here, the shell loses the user's
#     Application / Custom Command keybinds on every login because the
#     custom-action registration step in CustomKeybinds::initialize() finds
#     no definitions to register. Keep both files in lockstep.
# ---------------------------------------------------------------------------
_KWE_KEEP=(kineticwe.kwe rules.kwe keybinds.kwe custom-keybinds.kwe appearance.kwe kinetic-settings.kwe kglobalaccelrc kglobalshortcutsrc kineticweoutputconfig.json kactivitymanagerdrc session)
for _entry in "$KWE_CONFIG_HOME"/* "$KWE_CONFIG_HOME"/.[!.]*; do
    [[ -e "$_entry" ]] || continue
    _name="${_entry##*/}"
    for _keep in "${_KWE_KEEP[@]}"; do
        [[ "$_name" == "$_keep" ]] && continue 2
    done
    echo "kineticwe: pruning stale user-app config from root: $_entry"
    rm -rf "$_entry"
done
# ---------------------------------------------------------------------------
# 4d.5. One-shot migration: clear ALL of Spectacle's global-shortcut defaults
#     (_launch, the screenshot actions and the Start/Stop * Recording actions)
#     in the live kglobalaccelrc so none collide with KineticWE's own binds or
#     the user's plans. The KineticWE Default Binds.kksrc clears them in fresh
#     installs, but on an existing install the defaults were already written
#     by Spectacle before the .kwe override could land. This migration runs
#     once per version, marks the file with a `[Migration]
#     SpectacleRecordingUnbound=` sentinel, and never touches user-set values
#     after that.
#
#     One-time disruption: any custom keybind the user has previously set on a
#     Spectacle action is cleared the first time this runs. That's intentional
#     per the user request ("unbind by default"); post-migration, the user can
#     rebind any Spectacle action in Kinetic Settings -> Keybinds -> Spectacle,
#     and subsequent startups leave the rebind alone (sentinel suppresses
#     re-clearing).
#
#     Implementation note: the migration is a small Python helper inline
#     below (rather than a separate file) so the migration logic lives in
#     exactly one place -- the session script that also contains every
#     other kineticwe config migration. Atomic write (tmp + os.replace) so
#     the daemon never sees a half-written file. No-op when the file
#     already has the sentinel at the current version.
# ---------------------------------------------------------------------------
if [[ -f "$KWE_CONFIG_HOME/kglobalaccelrc" || -d "$KWE_CONFIG_HOME" ]]; then
    KWE_SPECTACLE_MIG_VERSION="kineticwe-2.1"
    export KWE_CONFIG_HOME KWE_SPECTACLE_MIG_VERSION
    python3 - "$KWE_CONFIG_HOME/kglobalaccelrc" "$KWE_SPECTACLE_MIG_VERSION" <<'PY' || \
        echo "kineticwe: Spectacle keybind migration failed (continuing)" >&2
import os, sys, tempfile

SPECTACLE = "[org.kde.spectacle.desktop][Global Shortcuts]"
# Every action id from /usr/share/kglobalaccel/org.kde.spectacle.desktop
# (`Actions=` list plus the app-launch action `_launch`).
SPECTACLE_ACTIONS = (
    "_launch",
    "FullScreenScreenShot",
    "CurrentMonitorScreenShot",
    "ActiveWindowScreenShot",
    "RectangularRegionScreenShot",
    "WindowUnderCursorScreenShot",
    "OpenWithoutScreenshot",
    "RecordRegion",
    "RecordScreen",
    "RecordWindow",
)
MIGRATION_GROUP = "[Migration]"
SENTINEL_KEY = "SpectacleRecordingUnbound"


def _have_spectacle():
    for d in os.environ.get("PATH", "").split(":"):
        if not d:
            continue
        candidate = os.path.join(d, "spectacle")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return True
    return False


def _strip_existing_sentinels(lines):
    out, skip = [], False
    for line in lines:
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            skip = s == MIGRATION_GROUP
            if not skip:
                out.append(line)
            continue
        if not skip:
            out.append(line)
    return out


def _read_sentinels(lines):
    out, group = set(), None
    for line in lines:
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            group = s
            continue
        if group == MIGRATION_GROUP and s.startswith(SENTINEL_KEY + "="):
            out.add(s.split("=", 1)[1].strip())
    return out


def main():
    if len(sys.argv) != 3:
        print("usage: spectacle_migration.py <kglobalaccelrc> <version>",
              file=sys.stderr)
        sys.exit(2)
    path, version = sys.argv[1], sys.argv[2]

    lines = []
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

    if version in _read_sentinels(lines):
        return  # already migrated at this version

    # Drop every existing [Migration] group so the file does not accumulate
    # sentinels across version bumps; re-add a single canonical one at end.
    lines = _strip_existing_sentinels(lines)

    # Force every Spectacle action to empty inside the Spectacle group.
    # Only create the group if Spectacle is actually installed (so users
    # without Spectacle don't end up with a stray empty group in their
    # kglobalaccelrc).
    group, saw_spectacle, actions_seen, out = None, False, set(), []
    for line in lines:
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            if group == SPECTACLE:
                for action in SPECTACLE_ACTIONS:
                    if action not in actions_seen:
                        out.append(f"{action}=")
                        actions_seen.add(action)
            group = s
            if group == SPECTACLE:
                saw_spectacle = True
            out.append(line)
        elif group == SPECTACLE and s and "=" in s and not s.startswith("#"):
            key = s.split("=", 1)[0].strip()
            if key in SPECTACLE_ACTIONS:
                out.append(f"{key}=")
                actions_seen.add(key)
            else:
                out.append(line)
        else:
            out.append(line)
    if group == SPECTACLE:
        for action in SPECTACLE_ACTIONS:
            if action not in actions_seen:
                out.append(f"{action}=")
                actions_seen.add(action)

    if not saw_spectacle and _have_spectacle():
        if out and out[-1].strip() != "":
            out.append("")
        out.append(SPECTACLE)
        for action in SPECTACLE_ACTIONS:
            out.append(f"{action}=")

    out.append("")
    out.append(MIGRATION_GROUP)
    out.append(f"{SENTINEL_KEY}={version}")

    new_contents = "\n".join(out) + "\n"
    parent = os.path.dirname(path) or "."
    tmp_fd, tmp_path = tempfile.mkstemp(dir=parent,
                                        prefix=".kglobalaccelrc.",
                                        suffix=".tmp")
    try:
        with os.fdopen(tmp_fd, "w", encoding="utf-8") as f:
            f.write(new_contents)
        os.replace(tmp_path, path)
    except Exception:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise


if __name__ == "__main__":
    main()
PY
fi


# ---------------------------------------------------------------------------
# 4d.6. One-shot migration: rename the KGlobalAccel components to KineticWE
#     identity (Phase 4). The compositor's own shortcut component moved from
#     "kwin" to "kineticwe", and the keyboard-layout component from
#     "KDE Keyboard Layout Switcher" to "KineticWE Keyboard Layout Switcher".
#     The shortcuts live in kglobalshortcutsrc / kglobalaccelrc, keyed by that
#     component name, so an existing user's binds would be lost unless the
#     groups are carried over. Kinetic Settings mirrors the compositor's binds
#     into keybinds.kwe, so that file is migrated too. Idempotent: after the
#     rename the old groups no longer exist.
# ---------------------------------------------------------------------------
python3 - "$KWE_CONFIG_HOME/kglobalshortcutsrc" "$KWE_CONFIG_HOME/kglobalaccelrc" \
    "$KWE_CONFIG_HOME/keybinds.kwe" <<'PY' || \
    echo "kineticwe: shortcut component migration failed (continuing)" >&2
import os, re, sys, tempfile

# Component unique-name renames. Group headers may be "[name]" or
# "[name][Global Shortcuts]".
RENAMES = {
    "kwin": "kineticwe",
    "KDE Keyboard Layout Switcher": "KineticWE Keyboard Layout Switcher",
}
HEADER = re.compile(r"^\[([^\]]+)\](.*)$")


def split_groups(text):
    out = []
    for line in text.splitlines(keepends=True):
        if HEADER.match(line.rstrip("\n")):
            out.append([line.rstrip("\n"), []])
        elif out:
            out[-1][1].append(line)
        else:
            out.append([None, [line]])
    return out


def key_of(line):
    return line.split("=", 1)[0].strip() if "=" in line else None


def rewrite(path):
    if not os.path.isfile(path):
        return
    with open(path, "r", encoding="utf-8") as f:
        original = f.read()
    groups = split_groups(original)
    by_header = {}
    for group in groups:
        if group[0] is not None:
            by_header.setdefault(group[0], group)
    changed = False
    out = []
    for group in groups:
        if group[0] is None:
            out.append("".join(group[1]))
            continue
        match = HEADER.match(group[0])
        new_name = RENAMES.get(match.group(1))
        if new_name is None:
            out.append(group[0] + "\n" + "".join(group[1]))
            continue
        new_header = "[" + new_name + "]" + match.group(2)
        target = by_header.get(new_header)
        if target is not None and target is not group:
            # Merge into the group that is already present, keeping its values.
            existing = {key_of(line) for line in target[1]}
            for line in group[1]:
                if key_of(line) not in existing:
                    target[1].append(line)
            changed = True
            continue  # drop the old group
        group[0] = new_header
        out.append(new_header + "\n" + "".join(group[1]))
        changed = True
    if not changed:
        return
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path) or ".")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write("".join(out))
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


for _path in sys.argv[1:]:
    rewrite(_path)
PY


# ---------------------------------------------------------------------------
# 4e. Default color scheme: Breeze Dark.
#     The Breeze window decoration (and KDE apps) take their colors from the
#     shared $KWE_REAL_CONFIG_HOME/kdeglobals color scheme. When no scheme is
#     configured the decoration falls back to KColorScheme's built-in default,
#     which is Breeze *Light*. Until the Kinetic Settings decorations/theming
#     section lands, make the out-of-the-box default Breeze Dark for both new
#     and existing users.
#
#     Gate: only apply when no color scheme is configured yet (no [Colors:*]
#     groups and no non-empty ColorScheme= entry). This seeds new users and
#     migrates existing users who are still on the bare light default, while
#     never overwriting a scheme the user (or the Noctalia theme service) chose.
#     The gate makes the step idempotent: once applied the scheme is set, so
#     later logins skip it. No sentinel needed.
#
#     Implementation note: the merge is a small inline Python helper (rather
#     than a separate file) so it lives with the rest of the session's config
#     migrations. It mirrors the merge semantics of Noctalia's
#     mergeKdeColorScheme (same group/key rules), preserving every non-color
#     setting already in the user's kdeglobals. Atomic write (tmp + os.replace)
#     so no reader ever sees a half-written file. No-op when kdeglobals already
#     carries a scheme.
# ---------------------------------------------------------------------------
_KWE_BREEZE_DARK=""
for _kwe_dir in "$HOME/.local/share" ${XDG_DATA_DIRS//:/ } "$INSTALL_PREFIX/share" /usr/local/share /usr/share; do
    [[ -z "$_kwe_dir" ]] && continue
    if [[ -f "$_kwe_dir/color-schemes/BreezeDark.colors" ]]; then
        _KWE_BREEZE_DARK="$_kwe_dir/color-schemes/BreezeDark.colors"
        break
    fi
done

if [[ -n "$_KWE_BREEZE_DARK" ]]; then
    KWE_KDE_GLOBALS="$KWE_REAL_CONFIG_HOME/kdeglobals"
    export KWE_KDE_GLOBALS _KWE_BREEZE_DARK
    python3 - "$KWE_KDE_GLOBALS" "$_KWE_BREEZE_DARK" <<'PY' || \
        echo "kineticwe: Breeze Dark default seeding failed (continuing)" >&2
import os
import re
import stat
import sys
import tempfile

COLOR_PREFIX = "Colors:"

# Name[xx] translation keys bloat kdeglobals; Plasma only writes the plain
# Name= when applying a scheme, so skip them.
_NAME_TRANSLATION = re.compile(r"^Name\[.*\]$")


def _is_comment_or_blank(line):
    stripped = line.strip()
    return stripped == "" or stripped.startswith("#") or stripped.startswith(";")


def _parse(path):
    doc = {
        "prefix": [],
        "sections": [],       # list of {name, header, lines, keys, key_index}
        "section_index": {},  # name -> index into sections
    }
    current = None
    try:
        with open(path, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
    except FileNotFoundError:
        return doc
    for raw in lines:
        line = raw[:-1] if raw.endswith("\r") else raw
        stripped = line.strip()
        if _is_comment_or_blank(stripped):
            (current["lines"] if current else doc["prefix"]).append(line)
            continue
        if stripped.startswith("[") and stripped.endswith("]"):
            name = stripped[1:-1]
            section = {"name": name, "header": line, "lines": [], "keys": [], "key_index": {}}
            doc["section_index"][name] = len(doc["sections"])
            doc["sections"].append(section)
            current = section
            continue
        equals = stripped.find("=")
        if current is None or equals == -1:
            continue
        key = stripped[:equals].strip()
        value = stripped[equals + 1:].strip()
        if key == "" or key in current["key_index"]:
            continue
        line_index = len(current["lines"])
        current["lines"].append(line)
        current["key_index"][key] = len(current["keys"])
        current["keys"].append({"name": key, "value": value, "line_index": line_index})
    return doc


def _append_key(section, key):
    insert_at = len(section["lines"])
    while insert_at > 0 and _is_comment_or_blank(section["lines"][insert_at - 1]):
        insert_at -= 1
    section["lines"].insert(insert_at, key["name"] + "=" + key["value"])


def _merge(destination, source):
    for source_section in source["sections"]:
        name = source_section["name"]
        index = destination["section_index"].get(name)
        if index is None:
            new_section = {"name": name, "header": "[" + name + "]", "lines": [], "keys": [], "key_index": {}}
            for key in source_section["keys"]:
                if _NAME_TRANSLATION.match(key["name"]):
                    continue
                _append_key(new_section, key)
            destination["section_index"][name] = len(destination["sections"])
            destination["sections"].append(new_section)
            continue
        destination_section = destination["sections"][index]
        for source_key in source_section["keys"]:
            if _NAME_TRANSLATION.match(source_key["name"]):
                continue
            destination_key_index = destination_section["key_index"].get(source_key["name"])
            if destination_key_index is None:
                _append_key(destination_section, source_key)
            else:
                destination_key = destination_section["keys"][destination_key_index]
                destination_section["lines"][destination_key["line_index"]] = source_key["name"] + "=" + source_key["value"]


def _serialize(doc):
    out = []
    for line in doc["prefix"]:
        out.append(line)
    for i, section in enumerate(doc["sections"]):
        if i > 0 and out and (not out[-1].strip() == ""):
            out.append("")
        out.append(section["header"])
        for line in section["lines"]:
            out.append(line)
    return "\n".join(out) + "\n"


def _has_scheme(doc):
    for section in doc["sections"]:
        if section["name"].startswith(COLOR_PREFIX):
            return True
        if section["name"] in ("General", "KDE"):
            for key in section["keys"]:
                if key["name"] == "ColorScheme" and key["value"] != "":
                    return True
    return False


def main():
    if len(sys.argv) != 3:
        print("usage: breeze_dark_default.py <kdeglobals> <BreezeDark.colors>", file=sys.stderr)
        sys.exit(2)
    globals_path, scheme_path = sys.argv[1], sys.argv[2]

    globals_doc = _parse(globals_path)
    if _has_scheme(globals_doc):
        return  # a scheme is already configured; do not touch it

    scheme_doc = _parse(scheme_path)
    if not scheme_doc["sections"]:
        print("no sections in " + scheme_path, file=sys.stderr)
        sys.exit(1)

    _merge(globals_doc, scheme_doc)
    new_contents = _serialize(globals_doc)

    parent = os.path.dirname(globals_path) or "."
    os.makedirs(parent, exist_ok=True)
    mode = None
    if os.path.exists(globals_path):
        mode = stat.S_IMODE(os.stat(globals_path).st_mode)
    tmp_fd, tmp_path = tempfile.mkstemp(dir=parent, prefix=".kdeglobals.", suffix=".tmp")
    try:
        with os.fdopen(tmp_fd, "w", encoding="utf-8") as f:
            f.write(new_contents)
        if mode is not None:
            os.chmod(tmp_path, mode)
        os.replace(tmp_path, globals_path)
    except Exception:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise


if __name__ == "__main__":
    main()
PY
fi


# ---------------------------------------------------------------------------
# 4f. Default desktop-content seeding.
#     A fresh install from TTY has no KDE desktop packages, so even after
#     installing the Breeze packages the session would come up with no cursor
#     theme, no icon theme and no terminal default (and nothing to launch with
#     a shortcut). Seed those — plus a Meta+T -> Alacritty shortcut — only when
#     they are unset, so a user's existing choices (icons, cursor, terminal,
#     keybinds) are never clobbered. Idempotent;
#
#     The compositor config keys ([Mouse] cursorTheme/cursorSize) are the ones
#     KWin and the launch-time cursor actually read; kdeglobals carries the
#     shared Qt/KDE settings (icon theme, cursor, default terminal);
#     appearance.kwe is the portable mirror the Kinetic Settings Icons and
#     Cursors pages drive; and the custom-command definition + chord are what
#     the shell registers/replays at start so Meta+T fires Alacritty.
# ---------------------------------------------------------------------------
# Only seed the default Meta+T -> Alacritty shortcut on the first run (when the
# user has no custom keybinds yet). Once custom-keybinds.kwe exists the user has
# interacted with custom keybinds, so re-seeding the shortcut on every login
# would resurrect a bind they deliberately unbound or reassigned.
KWE_SEED_KEYBINDS=""
if [[ ! -e "$KWE_CONFIG_HOME/custom-keybinds.kwe" ]]; then
    KWE_SEED_KEYBINDS="1"
fi
export KWE_CONFIG_HOME KWE_SEED_KEYBINDS
python3 - "$KWE_CONFIG_HOME" "$KWE_REAL_CONFIG_HOME" <<'PY' || \
    echo "kineticwe: desktop-default seeding failed (continuing)" >&2
import os
import stat
import sys
import tempfile


def _write(path, contents):
    parent = os.path.dirname(path) or "."
    os.makedirs(parent, exist_ok=True)
    mode = None
    if os.path.exists(path):
        mode = stat.S_IMODE(os.stat(path).st_mode)
    fd, tmp = tempfile.mkstemp(dir=parent, prefix=".kwe-seed.", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(contents)
        if mode is not None:
            os.chmod(tmp, mode)
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def _ensure(path, header, key, value):
    # Ensure `key=value` exists inside the `[header]` section of a KConfig-style
    # file, preserving every other line. Creates the file/section if missing;
    # leaves the file untouched when the key is already present.
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            lines = [l[:-1] if l.endswith("\r") else l for l in f.read().splitlines()]
    else:
        lines = []
    sections, preamble, cur = [], [], None
    for line in lines:
        if line.startswith("[") and line.endswith("]"):
            cur = [line, []]
            sections.append(cur)
        else:
            (cur[1] if cur is not None else preamble).append(line)
    keyline = key + "=" + value
    target = next((s for s in sections if s[0] == header), None)
    if target is not None:
        for line in target[1]:
            if line.split("=", 1)[0].strip() == key:
                return  # already present; leave untouched
        target[1].insert(0, keyline)
    else:
        sections.append([header, [keyline]])
    out = list(preamble)
    for i, (h, body) in enumerate(sections):
        if i > 0 and out and out[-1].strip() != "":
            out.append("")
        out.append(h)
        out.extend(body)
    _write(path, "\n".join(out) + "\n")


def main():
    if len(sys.argv) != 3:
        print("usage: kwe_seed_defaults.py <configHome> <realConfigHome>", file=sys.stderr)
        sys.exit(2)
    config_home, real_config_home = sys.argv[1], sys.argv[2]
    cfg = lambda name: os.path.join(config_home, name)

    # Portable Appearance settings (drive the Kinetic Settings pages).
    _ensure(cfg("appearance.kwe"), "[Appearance]", "IconTheme", "Breeze-Round-Chameleon Dark Icons")
    _ensure(cfg("appearance.kwe"), "[Appearance]", "CursorTheme", "breeze_cursors")
    _ensure(cfg("appearance.kwe"), "[Appearance]", "CursorSize", "24")

    # What the compositor actually reads for the pointer cursor.
    _ensure(cfg("kineticwe.kwe"), "[Mouse]", "cursorTheme", "breeze_cursors")
    _ensure(cfg("kineticwe.kwe"), "[Mouse]", "cursorSize", "24")

    # On NVIDIA proprietary drivers, the KWin blur effect is extremely
    # CPU-heavy on the compositor while a blurred window is on screen, which
    # makes the whole desktop slow. Disable blur by default there; users can
    # re-enable it later in Kinetic Settings.
    if os.path.exists("/proc/driver/nvidia/version"):
        _ensure(cfg("kineticwe.kwe"), "[Plugins]", "blurEnabled", "false")

    # Shared Qt/KDE settings so apps see icons and the cursor and can open a
    # terminal.
    kde = os.path.join(real_config_home, "kdeglobals")
    _ensure(kde, "[Icons]", "Theme", "Breeze-Round-Chameleon Dark Icons")
    _ensure(kde, "[KDE]", "cursorTheme", "breeze_cursors")
    _ensure(kde, "[KDE]", "cursorSize", "24")
    _ensure(kde, "[General]", "TerminalApplication", "alacritty")

    # Meta+T -> Alacritty, so a fresh user has a terminal to start from. The
    # action is registered with the daemon (custom-keybinds.kwe) and its chord
    # is replayed (keybinds.kwe) at session start by the shell. First-run only
    # (see KWE_SEED_KEYBINDS in the shell wrapper) so a user's later changes to
    # this bind are never undone.
    if os.environ.get("KWE_SEED_KEYBINDS") == "1":
        _ensure(cfg("custom-keybinds.kwe"), "[custom-commands]", "cmd-1", "Terminal")
        _ensure(cfg("custom-keybinds.kwe"), "[custom-commands:exec]", "cmd-1", "alacritty")
        _ensure(cfg("keybinds.kwe"), "[custom-commands][Global Shortcuts]", "cmd-1", "Meta+T")


if __name__ == "__main__":
    main()
PY


# ---------------------------------------------------------------------------
# 4f. Rebuild the KSycoca service cache before the compositor starts. The
#     embedded kglobalaccel daemon discovers apps that ship global shortcuts
#     (e.g. Spectacle) through KSycoca (detectAppsWithShortcuts), so a cache
#     that predates a newly-installed app means the app's keybinds never show
#     up in Kinetic Settings -> Keybinds. Plasma kept this fresh via
#     kbuildsycoca6, which KineticWE 2.0 dropped; a one-shot rebuild at login
#     is cheap and re-syncs the cache with newly installed applications.
#     --noincremental forces a full rebuild so a brand-new app is always seen.
# ---------------------------------------------------------------------------
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi


# ---------------------------------------------------------------------------
# 4g. The compositor redirects XDG_CONFIG_HOME to the kineticwe root, so its
#     QML theme stack (Kirigami/Plasma.Theme -> KColorScheme -> "kdeglobals")
#     resolves kdeglobals inside the root — where none exists — and falls back
#     to Breeze Light for the Overview/Grid View and other QML effects. The
#     C++ side already opens the shared file by path (kineticweKdeGlobalsPath),
#     but QML uses the standard config location. Symlink the shared kdeglobals
#     into the root so both agree on the single source of truth. A symlink is
#     not a mirror: atomic renames of the real file stay visible through it.
#     Only link when the real file exists: on a minimal host that has no seeded
#     color scheme yet (no BreezeDark.colors, so step 4e is a no-op) a dangling
#     symlink is worse than leaving the root without a kdeglobals at all.
# ---------------------------------------------------------------------------
if [[ -e "$KWE_REAL_CONFIG_HOME/kdeglobals" && ! -e "$KWE_CONFIG_HOME/kdeglobals" && ! -L "$KWE_CONFIG_HOME/kdeglobals" ]]; then
    ln -s "$KWE_REAL_CONFIG_HOME/kdeglobals" "$KWE_CONFIG_HOME/kdeglobals"
fi


# ---------------------------------------------------------------------------
# 4h. Keep the qt5ct/qt6ct tool configs in step with the session's icon theme.
#     Those configs are seeded once (first run only), so a user who picks
#     another icon theme in Kinetic Settings -> Icons would still get the seeded
#     one in non-KDE Qt apps: they read the platform-theme config, not
#     kdeglobals. Patch the two keys that must follow the session:
#       * [Appearance] icon_theme       <- appearance.kwe [Appearance] IconTheme
#                                          (fallback: kdeglobals [Icons] Theme=)
#       * [Appearance] standard_dialogs <- xdgdesktopportal, but only where the
#                                          legacy gtk3/gtk2 value is still set
#                                          (the session ships its own portal)
#     Only existing configs are touched, only those keys change, and the write
#     is atomic and skipped when nothing differs.
# ---------------------------------------------------------------------------
python3 - "$KWE_CONFIG_HOME/appearance.kwe" "$KWE_REAL_CONFIG_HOME/kdeglobals" \
    "$HOME/.config/qt5ct/qt5ct.conf" "$HOME/.config/qt6ct/qt6ct.conf" <<'PY' || \
    echo "kineticwe: qt5ct/qt6ct appearance sync failed (continuing)" >&2
import os
import stat
import sys
import tempfile

APPEARANCE = "Appearance"
LEGACY_DIALOGS = ("gtk3", "gtk2")


def read_key(path, section, key):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return ""
    current = None
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith(("#", ";")):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            continue
        if current != section or "=" not in line:
            continue
        name, value = line.split("=", 1)
        if name.strip() == key:
            return value.strip()
    return ""


def patch(path, updates, replace_only=None):
    """Set `updates` in the file's [Appearance] section.

    Keys listed in `replace_only` are only rewritten while they still hold one
    of the listed (legacy) values, so a deliberate user choice survives.
    """
    if not os.path.exists(path):
        return
    with open(path, "r", encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    out = []
    changed = False
    seen = set()
    in_section = False

    def flush_missing():
        nonlocal changed
        # Keep the section's trailing blank lines where they are
        trailing = []
        while out and out[-1].strip() == "":
            trailing.append(out.pop())
        for name, value in updates.items():
            if name not in seen:
                out.append(f"{name}={value}")
                seen.add(name)
                changed = True
        out.extend(reversed(trailing))

    for raw in lines:
        stripped = raw.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_section:
                flush_missing()
            in_section = stripped[1:-1] == APPEARANCE
            out.append(raw)
            continue
        if in_section and "=" in stripped and not stripped.startswith(("#", ";")):
            name, value = stripped.split("=", 1)
            name, value = name.strip(), value.strip()
            if name in updates:
                seen.add(name)
                allowed = (replace_only or {}).get(name)
                if (allowed is None or value in allowed) and value != updates[name]:
                    out.append(f"{name}={updates[name]}")
                    changed = True
                    continue
        out.append(raw)
    if in_section:
        flush_missing()
    if not changed:
        return

    parent = os.path.dirname(path) or "."
    fd, tmp = tempfile.mkstemp(dir=parent, prefix="." + os.path.basename(path) + ".", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write("\n".join(out) + "\n")
        os.chmod(tmp, stat.S_IMODE(os.stat(path).st_mode))
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def main():
    appearance, kdeglobals = sys.argv[1], sys.argv[2]
    icon_theme = read_key(appearance, APPEARANCE, "IconTheme") or read_key(kdeglobals, "Icons", "Theme")
    updates = {"standard_dialogs": "xdgdesktopportal"}
    if icon_theme:
        updates["icon_theme"] = icon_theme
    for path in sys.argv[3:]:
        patch(path, updates, replace_only={"standard_dialogs": LEGACY_DIALOGS})


if __name__ == "__main__":
    main()
PY


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
    QT_LOGGING_RULES="kwin_wayland_drm=true" XDG_CONFIG_HOME="$KWE_CONFIG_HOME" XDG_CACHE_HOME="$HOME/.cache/kineticwe" "$INSTALL_PREFIX/bin/kinetic-we" --xwayland --no-lockscreen 2>>"$_KWE_DRM_LOG" &
    KWIN_PID=$!
else
    XDG_CONFIG_HOME="$KWE_CONFIG_HOME" XDG_CACHE_HOME="$HOME/.cache/kineticwe" "$INSTALL_PREFIX/bin/kinetic-we" --xwayland --no-lockscreen &
    KWIN_PID=$!
fi

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
if ! systemctl --user cat kineticwe-session.target >/dev/null 2>&1; then
    # Dev/raw-checkout fallback: install the unit into the user unit dir.
    mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/systemd/user"
    install -m 0644 "$INSTALL_PREFIX/share/kineticwe/kineticwe-session.target" \
        "${XDG_DATA_HOME:-$HOME/.local/share}/systemd/user/" 2>/dev/null \
        || true
fi
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
# 7. Start the shell (Noctalia fork, noctalia-kwe) — supervised, in the background
# ---------------------------------------------------------------------------
(
    while true; do
        noctalia-kwe >>"$HOME/.local/share/noctalia-kwe.log" 2>&1
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
