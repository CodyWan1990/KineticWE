# Fixing PowerDevil Not Starting (CachyOS / KineticWE)

## Symptoms

- No power management: brightness, idle/DPMS, suspend, or power profiles don't work.
- System Settings → Power Management shows "Power Management is not running".
- `org_kde_powerdevil` process may appear briefly then die, or a process is
  alive but `org.kde.Solid.PowerManagement` is **not** on the D-Bus bus.
- Journal shows PowerDevil-related errors such as:
  - `org.kde.powerdevil.backlighthelper.brightness failed`
  - `Charge thresholds are not supported by the kernel for this hardware`
  - `Battery conservation mode is not supported`
  - `Failed to register with host portal ... App info not found for 'org.kde.org_kde_powerdevil'`

> The `backlighthelper` / `chargethresholdhelper` / portal messages are
> **harmless** on desktops without built-in panels, battery charge-threshold
> support, or a configured portal. They do not prevent PowerDevil from running.

## Root Cause

### 1. Process not launched at all

On a systemd-managed Plasma session, PowerDevil is launched two ways:

- The XDG autostart file `/etc/xdg/autostart/powerdevil.desktop`, which is
  marked `X-systemd-skip=true` — so on systemd sessions it is **skipped**.
- The systemd unit `plasma-powerdevil.service`, which is `static` (no
  `[Install]` section) and is only pulled in by `plasma-workspace.target`.

After a logout/relogin where `plasma-workspace.target` is not fully
re-triggered, the unit never starts and PowerDevil stays dead. Under
KineticWE, `start-kineticwe` is supposed to launch PowerDevil directly in its
startup payload, but its `$POWERDEVIL` resolution can fail or the detached
`nohup` child can be cleaned up, leaving PowerDevil unstarted.

### 2. Process launches but crashes (the real "not running" cause)

Even when launched, PowerDevil **aborts (core dump) if `WAYLAND_DISPLAY` is
not set in its environment**. It never registers the D-Bus service
`org.kde.Solid.PowerManagement`, so System Settings reports "not running".

This is reproducible:

- `WAYLAND_DISPLAY` unset  → PowerDevil aborts, never on bus.
- `WAYLAND_DISPLAY=wayland-0` → PowerDevil runs and registers correctly.

kded6's autostart (and the `start-kineticwe` payload) spawn PowerDevil without
guaranteeing `WAYLAND_DISPLAY`, so it dies on every launch under Wayland.

## Fix

The reliable fix is a **user systemd unit** that sets `WAYLAND_DISPLAY`
explicitly, binds to the session, and restarts on failure. This works under
both Plasma and KineticWE and survives relogin.

### Create the unit

`~/.config/systemd/user/powerdevil-user.service`:

```ini
[Unit]
Description=Powerdevil (KDE Power Management)
PartOf=graphical-session.target
After=graphical-session.target

[Service]
Type=dbus
BusName=org.kde.Solid.PowerManagement
ExecStart=/usr/lib/org_kde_powerdevil
Environment=WAYLAND_DISPLAY=wayland-0
Restart=on-failure
TimeoutStopSec=5s

[Install]
WantedBy=graphical-session.target
```

### Enable and start

```bash
systemctl --user daemon-reload
systemctl --user enable --now powerdevil-user.service
```

If you already had an XDG autostart file at
`~/.config/autostart/powerdevil.desktop`, remove it to avoid a double launch:

```bash
rm -f ~/.config/autostart/powerdevil.desktop
```

### Optional: patch start-kineticwe instead

If you prefer PowerDevil to be launched by the KineticWE session script itself
(so it works even without the unit above), add an `export` right before its
PowerDevil launch in `/usr/bin/start-kineticwe` (requires `sudo`):

```bash
# before: nohup "$POWERDEVIL" >"$HOME/.local/share/powerdevil.log" 2>&1 &
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
nohup "$POWERDEVIL" >"$HOME/.local/share/powerdevil.log" 2>&1 &
```

## Verify

```bash
systemctl --user is-active powerdevil-user.service   # should print: active
busctl --user list | grep org.kde.Solid.PowerManagement   # should list the service
pgrep -f org_kde_powerdevil                            # should list a PID
```

Then open **System Settings → Power Management** — it should now be active.

Self-heal test (optional): kill the process and confirm systemd revives it:

```bash
pkill -f /usr/lib/org_kde_powerdevil
sleep 5
systemctl --user is-active powerdevil-user.service    # active again
```

## Notes

- Do **not** enable the stock `plasma-powerdevil.service` manually
  (`systemctl --user enable plasma-powerdevil.service` fails — it is `static`
  with no `[Install]` section).
- If you previously added an autostart file, remove it to avoid double-launch
  conflicts.
- `WAYLAND_DISPLAY` may differ on your system (e.g. `wayland-1`); check with
  `echo $WAYLAND_DISPLAY`. The unit above defaults to `wayland-0`, which
  matches the standard KineticWE/Plasma Wayland session.
