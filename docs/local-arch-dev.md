# Local Arch development builds

For regular use, install the packages (the binary repo or `./install-arch.sh`, see the [README](../README.md#arch-linux)). This page covers `install-arch.sh --local-dev`, which installs a development build into `~/.local`, and how to move from it back to the packages.

## Development loop (`--local-dev`)

> **For testing and development only.** `--local-dev` installs a development build into `~/.local` that overrides the packages. It's not recommended for regular use, and unless you pass `--no-greeter` it also reconfigures your display manager. For daily use, install the packages (`./install-arch.sh` or the binary repo). Undo a dev build with `./install-arch.sh --reset-local`.

```bash
./install-arch.sh --local-dev                         # compositor, portal, shell + greeter setup
./install-arch.sh --local-dev --update                # only the components that changed
./install-arch.sh --local-dev --no-greeter            # no greeter: your display manager is left alone
./install-arch.sh --local-dev --no-greeter --update   # both
```

`--no-greeter` works the same way as for the packages (see [Build from source](../README.md#build-from-source)): it keeps your current display manager.

## Migrating from a local (`~/.local`) build to the packages

If you used `install-arch.sh --local-dev`, remove those local files after installing the packages, otherwise the dev copies take priority. `./install-arch.sh` ends with a read-only check that lists any leftovers.

1. **Build and install the packages**

   ```bash
   ./install-arch.sh
   ```

2. **Log out and pick "KineticWE"** (not "KineticWE-Dev") at the login screen. Don't remove the dev install while it is running.

3. **Remove the local dev install** (from the packaged session):

   ```bash
   ./install-arch.sh --reset-local
   pkill -f 'libexec/xdg-desktop-portal-kwe'      # stop the old portal backend
   systemctl --user restart xdg-desktop-portal.service
   ```

   `--reset-local` removes only the files the script recorded when it installed them: the dev compositor, shell and portal (including its D-Bus activation file and user units) and the KineticWE-Dev login entry. Files you edited since are kept. Your settings (`~/.config/kineticwe`, `~/.local/state/noctalia`) are not touched.

4. **If you used `--local-dev` without the `kineticwe-greeter` package**, the greeter was built into `/usr/local`. Install the packaged greeter, remove the old copy and point greetd at the package:

   ```bash
   sudo pacman -S --needed kineticwe-greeter    # skip if ./install-arch.sh already installed it
   sudo rm -f /usr/local/bin/noctalia-greeter{,-apply-appearance,-compositor,-session,-print-greetd-config} \
              /usr/local/lib/tmpfiles.d/noctalia-greeter.conf \
              /usr/local/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy
   sudo rm -rf /usr/local/share/noctalia-greeter
   sudo /usr/share/noctalia-greeter/setup_greeter_system.sh
   kineticwe-setup-greeter    # points greetd at /usr/bin/noctalia-greeter-session (backs up config.toml)
   ```

   Then reboot. Don't restart greetd while logged in; that ends your session. If the greeter doesn't come up, switch to a TTY (Ctrl+Alt+F2) and restore `/etc/greetd/config.toml.bak`. With the package installed, `--local-dev` uses the packaged greeter and this step isn't needed.

5. **Verify**

   ```bash
   pacman -Qkk kineticwe xdg-desktop-portal-kwe kineticwe-noctalia kineticwe-greeter
   for p in kinetic-we noctalia-kwe; do readlink /proc/$(pgrep -x $p)/exe; done   # both /usr/bin/...
   grep '^command' /etc/greetd/config.toml   # /usr/bin/noctalia-greeter-session
   systemctl --user status xdg-desktop-portal.service   # active (running)
   ```

Used the old `build-arch-native.sh`? See [arch-migration.md](arch-migration.md) instead.
