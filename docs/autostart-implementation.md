# XDG Autostart for KineticWE

## Goal

Get the System Settings → Autostart KCM working with KineticWE. Currently the only way to autostart apps is to manually edit the `start-kineticwe` script.

## What Already Exists

| Component | Location | Status |
|-----------|----------|--------|
| Autostart KCM (`kcm_autostart.so`) | `/usr/lib64/qt6/plugins/plasma/kcms/systemsettings/` | ⚠️ From `plasma-workspace` (not a KineticWE dep; install separately if needed) |
| User autostart dir | `~/.config/autostart/` | ✅ Writable |
| System autostart dir | `/etc/xdg/autostart/` | ✅ Has entries |
| `systemd-xdg-autostart-generator` | `/usr/lib/systemd/user-generators/` | ✅ Available |
| `xdg-desktop-autostart.target` | `/usr/lib/systemd/user/` | ✅ Available |
| `kded6` (KDE daemon) | `/usr/bin/kded6` | ✅ Available but not needed |

## The Gap

The startup payload (the heredoc script launched via `--exit-with-session`) only starts:
- `kbuildsycoca6`
- `xdg-desktop-portal-kde` / `xdg-desktop-portal`
- `upowerd` / `org_kde_powerdevil`
- `noctalia`

It does **not** process any XDG autostart `.desktop` files.

## Attempted Approaches

### 1. systemd `xdg-desktop-autostart.target` ❌

**What we tried:** Add `systemctl --user start xdg-desktop-autostart.target` to the startup payload.

**Result:** The systemd generator runs at user login (via SDDM or TTY), **before** KWin/compositor is ready. The generated services for `/etc/xdg/autostart/` either fail or start at the wrong time. Also, user entries in `~/.config/autostart/` were **not** picked up by the generator at all on this system. By the time the startup payload runs, the target is already active — so the bash fallback was skipped and no user autostart entries were processed.

**Lesson:** systemd's autostart generator runs too early in the session lifecycle for a standalone compositor session like KineticWE. It assumes a full Plasma session where autostart apps should start early.

### 2. Bash XDG Autostart Parser (in progress) ⚠️

**What we tried:** Add a bash loop inside the startup payload heredoc that reads `~/.config/autostart/` and `/etc/xdg/autostart/`, parses `.desktop` files, checks conditions (`Hidden`, `OnlyShowIn`, `NotShowIn`, `TryExec`), strips field codes, and launches each app.

**Bug encountered:** The startup payload is written via an **unquoted heredoc** (`cat > "$STARTUP_PAYLOAD" << EOF`). In an unquoted heredoc, `$variable` references are expanded immediately by the outer script. Variables intended for the inner payload script (e.g., `$_kwe_ad`, `$_kwe_f`) must be escaped as `\$_kwe_ad` or they'll expand to empty strings in the generated payload. This is the same pattern already used for `\$PATH` and `\$HOME` elsewhere in the template.

**Status:** The parser logic is correct; it just needs the `\$` escaping fixed.

## The Fix (when ready)

### Files to modify

1. **`scripts/start-kineticwe.sh`** — the dev template
2. **`lgl-kineticwe-copr/SOURCES/start-kineticwe`** — the RPM version

### What to add

Inside the heredoc payload (between the power management section and `noctalia`), add:

```bash
# ---------------------------------------------------------------------------
# XDG autostart applications
# ---------------------------------------------------------------------------
# Process auto-start .desktop files from ~/.config/autostart/ and
# /etc/xdg/autostart/.  KWin is already running at this point so
# apps that need a compositor will connect successfully.
for _kwe_ad in "\$HOME/.config/autostart" "/etc/xdg/autostart"; do
    [ -d "\$_kwe_ad" ] || continue
    for _kwe_f in "\$_kwe_ad"/*.desktop; do
        [ -f "\$_kwe_f" ] || continue
        # Skip if Hidden=true
        grep -qi '^Hidden=true' "\$_kwe_f" && continue
        # Check OnlyShowIn / NotShowIn against XDG_CURRENT_DESKTOP=KDE
        _kwe_osi=\$(grep '^OnlyShowIn=' "\$_kwe_f" | sed 's/^OnlyShowIn=//')
        _kwe_nsi=\$(grep '^NotShowIn=' "\$_kwe_f" | sed 's/^NotShowIn=//')
        if [ -n "\$_kwe_nsi" ] && echo "\$_kwe_nsi" | grep -qiE "(^|;)KDE(;|$)"; then continue; fi
        if [ -n "\$_kwe_osi" ] && ! echo "\$_kwe_osi" | grep -qiE "(^|;)KDE(;|$)"; then continue; fi
        # Check TryExec
        _kwe_te=\$(grep '^TryExec=' "\$_kwe_f" | sed 's/^TryExec=//')
        if [ -n "\$_kwe_te" ] && ! command -v "\$_kwe_te" &>/dev/null; then continue; fi
        # Extract and clean the Exec line
        _kwe_ex=\$(grep '^Exec=' "\$_kwe_f" | sed 's/^Exec=//')
        [ -n "\$_kwe_ex" ] || continue
        # Strip desktop-file field codes
        _kwe_ex=\$(printf '%s' "\$_kwe_ex" | sed 's/%[fFuUdDnNickvm]//g')
        # Trim whitespace
        _kwe_ex=\$(printf '%s' "\$_kwe_ex" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        [ -n "\$_kwe_ex" ] || continue
        nohup sh -c "\$_kwe_ex" &>/dev/null &
        unset _kwe_ex
    done
done
unset _kwe_ad _kwe_f _kwe_osi _kwe_nsi _kwe_te _kwe_ex
```

**Crucially:** All `$` signs for variables meant for the **inner** payload script must be escaped as `\$` so they pass through the unquoted heredoc literally. This includes:
- `\$HOME` → rendered as `$HOME` in the payload
- `\$_kwe_ad`, `\$_kwe_f`, etc. → rendered as `$_kwe_ad`, `$_kwe_f` in the payload
- `\$(grep ...)` → rendered as `$(grep ...)` (command substitution) in the payload

### Deployment

After editing the source, re-deploy to `~/.local`:

```bash
sed -e "s|@INSTALL_PREFIX@|$HOME/.local|g" \
    /var/home/don/Projects/kineticwe/scripts/start-kineticwe.sh \
    > $HOME/.local/bin/start-kineticwe && \
chmod +x $HOME/.local/bin/start-kineticwe
```

Then log out and back in.

### Verification

Check that the generated payload at `/run/user/$UID/kineticwe-$USER/startup.sh` has literal variable names (not empty strings) in the autostart section:

```bash
cat /run/user/1000/kineticwe-don/startup.sh | grep -A5 'for _kwe_ad'
```

Should show `\$_kwe_ad` literally, not empty `""`.
