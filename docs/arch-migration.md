# Migrating from the old `build-arch-native.sh` (Arch)

Before the Arch rework, `build-arch-native.sh` installed KineticWE into `~/.local` (and the greeter into `/usr/local`) **without keeping a record of what it installed**. The current script, `install-arch.sh`, installs pacman packages instead. Its `--reset-local` only removes files it recorded itself, so it can't clean up after the old script.

Old-script files take priority over the packages. The worst case is a stale portal file that **breaks every portal** (file dialogs, screen sharing, screenshots). Follow this guide once, after installing the packages.

## Do you need this?

You need this guide if either of these is true:

- `./install-arch.sh --reset-local` says the dev files came from the old script, or (older versions) "No installation manifest".
- `ls ~/.local/bin` shows `kinetic-we` or `noctalia-kwe`, but `~/.local/share/kineticwe/arch-native-state/` doesn't exist.

If you only ever used the packages, or only the current `install-arch.sh`, you don't need this. Use `./install-arch.sh --reset-local` instead (see the README).

## Before you start

1. **Install the packages** from the repo checkout:

   ```bash
   ./install-arch.sh
   ```

   If pacman reports `/usr/include/stb/... exists in filesystem`, the old script put those headers there by hand. Let the `stb` package take them over and build again:

   ```bash
   sudo pacman -S --overwrite '/usr/include/stb/*' stb
   ./install-arch.sh
   ```

2. **Log out and pick "KineticWE"** at the login screen, not "KineticWE-Dev". You can also switch to a TTY (Ctrl+Alt+F2). Don't remove files from a session that is running them.

Your settings are **not** touched by anything below: `~/.config/kineticwe` (keybinds, appearance, window rules) and `~/.local/state/noctalia` (shell settings, plugins).

## Step 1: Fix the portal first

The old script installed a D-Bus activation file for the portal backend into `~/.local`. D-Bus prefers it over the packaged one, but it points at `~/.local/lib/libexec/xdg-desktop-portal-kwe`, which no longer exists. Every portal request then fails with **`203/EXEC`**, so **all portals stop working**.

```bash
# The stale D-Bus activation file shadowing the packaged portal
rm -f ~/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kwe.service

# Dead dev units (systemd never reads ~/.local/lib/systemd/user, and they
# point at files that are gone)
rm -f ~/.local/lib/systemd/user/kineticwe.service \
      ~/.local/lib/systemd/user/plasma-xdg-desktop-portal-kwe.service
rmdir ~/.local/lib/systemd/user 2>/dev/null

# Stop any old backend and restart the portal host so it drops cached state
systemctl --user daemon-reload
pkill -f 'libexec/xdg-desktop-portal-kwe'
systemctl --user restart xdg-desktop-portal.service
```

Removing only the D-Bus file isn't always enough. Restarting `xdg-desktop-portal.service` is what clears the broken state.

## Step 2: See what's left over

This only lists files; it deletes nothing:

```bash
find ~/.local -maxdepth 6 -not -path "$HOME/.local/state/*" \( -iname '*kinetic*' -o -iname '*kwin*' -o -iname '*noctalia*' -o -iname '*kdecoration*' -o -iname '*globalaccel*' -o -iname '*portal*kwe*' -o -iname 'Breeze-Round-Chameleon*' \) 2>/dev/null | sort
ls /usr/local/bin | grep -i noctalia
ls /usr/local/share/wayland-sessions/
```

## Step 3: Remove the old `~/.local` install

`-v` prints each file as it is removed; paths that don't exist are skipped:

```bash
cd ~/.local && rm -rfv \
 bin/kinetic-we bin/kinetic-we_wayland* bin/kineticwe_windowprop bin/kwin_wayland* bin/kwindowprop \
 bin/noctalia-kwe bin/start-kineticwe \
 lib/libkineticwe.so* lib/libkdecorations3*.so* lib/libKGlobalAccelD.so* lib/libkwin.so* \
 lib/libexec/kineticwe_* lib/libexec/kwin* lib/libexec/xdg-desktop-portal-kwe libexec/xdg-desktop-portal-kwe \
 lib/plugins/kineticwe lib/plugins/org.kineticwe.decoration lib/plugins/kwin lib/plugins/org.kde.kdecoration3 \
 lib/plugins/kf6/packagestructure/kineticwe* lib/plugins/plasma/kcms/systemsettings/kcm_kwin* \
 lib/plugins/plasma/kcms/systemsettings/kcm_animations.so \
 lib/qt6/plugins/kineticwe lib/qt6/plugins/org.kineticwe.decoration \
 lib/qml/org/kineticwe lib/qml/org/kde/kwin lib/qml/org/kde/xdgdesktopportal \
 lib/cmake/KDecoration3 lib/cmake/KGlobalAccelD lib/cmake/KWin lib/cmake/KWinDBusInterface \
 include/kwin include/KDecoration3 include/KGlobalAccelD include/KF6/kdecoration3_version.h \
 share/kineticwe share/kineticwe-wayland share/noctalia share/icons/Breeze-Round-Chameleon* \
 share/icons/hicolor/scalable/apps/noctalia.svg share/icons/hicolor/*/apps/kineticwe* \
 share/applications/org.kineticwe.* share/applications/dev.noctalia.Noctalia.desktop \
 share/applications/dev.kineticwe.Settings.desktop share/applications/org.freedesktop.impl.portal.desktop.kwe.desktop \
 share/xdg-desktop-portal/portals/kwe.portal share/xdg-desktop-portal/kineticwe-portals.conf \
 share/systemd/user/kineticwe-session.target share/wayland-sessions/kineticwe.desktop
cd ~ && systemctl --user daemon-reload
```

`~/.local/share/noctalia` only held the dev shell's bundled assets. Noctalia's settings are in `~/.local/state/noctalia`, which this doesn't touch.

## Step 4: Remove the old dev login entry

```bash
sudo rm -f /usr/local/share/wayland-sessions/kineticwe-dev.desktop
```

## Step 5: Old greeter in `/usr/local` (only if Step 2 found one)

If `ls /usr/local/bin | grep -i noctalia` listed files, the old `--full` mode built the greeter into `/usr/local`, and greetd may still point there. Install the packaged greeter, remove the old copy, and point greetd at the package:

```bash
sudo pacman -S --needed kineticwe-greeter    # skip if ./install-arch.sh already installed it

sudo rm -f /usr/local/bin/noctalia-greeter{,-apply-appearance,-compositor,-session,-print-greetd-config} \
           /usr/local/lib/tmpfiles.d/noctalia-greeter.conf \
           /usr/local/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy
sudo rm -rf /usr/local/share/noctalia-greeter
sudo /usr/share/noctalia-greeter/setup_greeter_system.sh

kineticwe-setup-greeter    # points greetd at /usr/bin/noctalia-greeter-session (backs up config.toml)
```

Ignore the "copy and paste the block below" part of the `setup_greeter_system.sh` output. Don't restart greetd while you're logged in; that ends your session. Reboot instead.

<details>
<summary>Manual alternative (packages older than 2:0.0.1-14.4)</summary>

Older `kineticwe-setup-greeter` versions treat any config that mentions `noctalia-greeter-session` as already configured, even with the old `/usr/local` path. Use this instead of the last command above:

```bash
sudo cp -a /etc/greetd/config.toml /etc/greetd/config.toml.bak
sudo sed -i 's|/usr/local/bin/noctalia-greeter-session|/usr/bin/noctalia-greeter-session|' /etc/greetd/config.toml
grep -E '^(command|user)' /etc/greetd/config.toml   # command = "/usr/bin/noctalia-greeter-session"
```

</details>

## Step 6: Reboot and verify

```bash
for p in kinetic-we noctalia-kwe; do readlink /proc/$(pgrep -x $p)/exe; done   # both /usr/bin/...
grep '^command' /etc/greetd/config.toml          # /usr/bin/noctalia-greeter-session (if you use the greeter)
systemctl --user status xdg-desktop-portal.service   # active (running), no 203/EXEC
```

Then run the build once more. It ends with a read-only check that should report **✔ No leftovers from older local builds**:

```bash
./install-arch.sh
```

## Troubleshooting

- **Portals still broken** (no file dialogs, screen sharing fails):

  ```bash
  journalctl --user -u xdg-desktop-portal -b
  ls ~/.local/share/dbus-1/services/
  ```

  If you see `203/EXEC`, or the `org.freedesktop.impl.portal.desktop.kwe.service` file is still there, repeat Step 1.

- **No greeter after reboot / back to a text login:** switch to a TTY (Ctrl+Alt+F2), log in, and restore the backup:

  ```bash
  sudo cp -a /etc/greetd/config.toml.bak /etc/greetd/config.toml
  ```

  Then reboot, and run `kineticwe-setup-greeter` again from the KineticWE session.

- **`start-kineticwe` in a terminal still runs an old build:** `command -v start-kineticwe` should print `/usr/bin/start-kineticwe`. If it prints a `~/.local` path, Step 3 missed a file; repeat it.
