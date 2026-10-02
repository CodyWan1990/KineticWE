# KineticWE

> **This repository is a fork of [KWin](https://github.com/KDE/kwin) from the [KDE](https://kde.org) project.**
>
> KineticWE adds native tiling support while keeping the Destktop Enviroment Feel.**KineticWE** — a KWin Window Environment that pairs the [Noctalia](https://github.com/noctalia-dev/noctalia) shell with this compositor.


## ❤ Support the Project

If you want to support this project, you can donate here: [Ko-fi — theblackdon](https://ko-fi.com/theblackdon)

---

> ## ⚠ BETA Stage
>
> **KineticWE is currently in BETA.**
>
> Expect:
> - Bugs, crashes, and visual glitches.
> - Breaking changes between commits (config keys, scripts, CLI flags).
> - Features that work today may be removed or rewritten tomorrow.
>
> Please do **note** use KineticWE as your daily driver on a production system. Feedback and bug reports are welcome, but you are running pre-release software.

> # LLM-Generated Code Disclaimer
>
> Portions of this project — including (but not limited to) installer scripts, build glue, configuration files, documentation, and some C++/QML/JS source files — were **authored with the assistance of LLM tools** (large language models coding assistants).
>
> All LLM-generated content has been reviewed by the maintainers, but it has **not** been audited to the same standard as hand-written code. You may encounter:
> - Subtle bugs or edge cases that a human author would have caught.
> - Stylistic inconsistencies with the surrounding KWin codebase.
>
> **If you are not comfortable running code that was written (in whole or in part) by an LLM, do not use this project and keep it movin.** By using KineticWE you acknowledge and accept this.

---

## Installing KineticWE

### COPR (Fedora) — Recommended - Use Fedora Linux for the best and tested experience 

The recommended way to install KineticWE on Fedora is via [COPR](https://copr.fedorainfracloud.org/).
COPR provides pre-built RPMs so you don't need to compile from source.

Enable the required COPR repositories, then install:

```bash
sudo dnf copr enable lionheartp/Hyprland
sudo dnf copr enable theblackdon/kineticwe
sudo dnf install kineticwe noctalia-git
```

After installation, select **KineticWE** from your display manager's session menu
or run `start-kineticwe` from a TTY.

The `kineticwe` package provides and obsoletes Fedora's stock `kwin`, `kwin-common`,
`kwin-libs`, `kglobalacceld`, and `kdecoration` packages — it replaces the stock
compositor rather than installing alongside it.

> **Note:** The first time you install, reboot before logging in to a KineticWE session. On subsequent logins Noctalia will load automatically.
>
> **Tip:** Press **Meta+Space** to open the app drawer.

### Bazzite / Universal Blue (rpm-ostree)

> **Prerequisite:** The kineticwe COPR package requires **Qt ≥ 6.11**.
> Your base image must include Qt 6.11 or newer — update your system first:
> `sudo bootc upgrade && systemctl reboot`

On rpm-ostree based systems (Bazzite, Bluefin, Aurora, Silverblue, Kinoite),
`kineticwe` replaces the system's stock `kwin`, `kwin-common`, `kwin-libs`,
`kglobalacceld`, and `kdecoration`. These base packages must be removed from
the immutable image first via `override remove`:

```bash
sudo dnf copr enable lionheartp/Hyprland
sudo dnf copr enable theblackdon/kineticwe
sudo rpm-ostree override remove kwin kwin-common kwin-libs kglobalacceld kdecoration \
    --install kineticwe --install noctalia-git
systemctl reboot
```

### Arch Linux

#### Binary repo (recommended)

The fastest way to install KineticWE on Arch-based distros (Arch, CachyOS, EndeavourOS, Manjaro, etc.) is the pre-built binary repo hosted on GitLab Pages — no compiling needed.

Add the repo to `/etc/pacman.conf` (e.g. `sudo nano /etc/pacman.conf`):

```ini
[kineticwe]
SigLevel = Never
Server = https://theblackdon.gitlab.io/kineticwe/$arch
```

and install:

```bash
sudo pacman -Sy kineticwe
```

This also pulls in the `xdg-desktop-portal-kwe` portal backend and the `kineticwe-noctalia` shell.

Optionally, install the Noctalia greeter (greetd) and run its interactive setup (it asks for `sudo` itself when needed):

```bash
sudo pacman -S kineticwe-greeter
kineticwe-setup-greeter
```

#### Build from source

> The script was renamed from `build-arch-native.sh` to `install-arch.sh`, and `--full` is now `--local-dev` (`--full` still works but prints a deprecation warning).

To build the split packages (`kineticwe`, `xdg-desktop-portal-kwe`, `kineticwe-noctalia`, `kineticwe-greeter`) from your working tree and install them with pacman:

```bash
./install-arch.sh
```

For a development loop, the same script can instead install into `~/.local`:

```bash
./install-arch.sh --binary     # compositor, portal and shell
./install-arch.sh --kwin-only  # compositor only (no portal/shell)
./install-arch.sh --noct-only  # Noctalia shell only
./install-arch.sh --local-dev  # everything, and also sets up the greeter
```

> **Note:** KWin uses Qt private APIs, so a source build is tied to the exact Qt version it was built against. After every Arch Qt update, rebuild (the binary repo is rebuilt automatically for this).

> **Note:** The Fedora spec (`lgl-kineticwe-copr/kineticwe.spec`) is the source of truth for the version, release and pinned commits. Builds always use the spec's values; if the committed `PKGBUILD` lags behind, the build prints a warning and continues. Run `arch/sync-from-spec.sh PKGBUILD` to update it. Changes beyond version and pins (dependencies, files, patches) still need a manual `PKGBUILD` edit.

#### Setting up the greeter

Installing `kineticwe-greeter` does not change your display manager. Run the setup once, then reboot:

```bash
kineticwe-setup-greeter
```

Enabling `greetd.service` on its own is not enough: Arch's default greetd config runs `agreety`, a text login that drops you into a shell.

#### Migrating from a local (`~/.local`) build to the packages

> **Used the old `build-arch-native.sh`?** It didn't record what it installed, so `--reset-local` can't remove it, and its leftovers can break every portal. Follow [docs/arch-migration.md](docs/arch-migration.md) instead of the steps below.

If you used `install-arch.sh --binary`, `--kwin-only`, `--noct-only` or `--local-dev`, remove those local files after installing the packages, otherwise the dev copies take priority. `./install-arch.sh` ends with a read-only check that lists any leftovers.

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

### NixOS / Nix

> **⚠ Work in progress:** The Nix flake is experimental — it is not fully tested or complete. **Use at your own risk**; things may be broken or change without notice.

A [Nix flake](https://nixos.wiki/wiki/Flakes) provides a reproducible build of kinetic-we, the full KineticWE session (kinetic-we + noctalia), and Home Manager / NixOS modules. Like the AUR and COPR packages, it builds `kdecoration` and `kglobalacceld` from pinned KDE master commits (kinetic-we needs `KDecoration3::Style`, which only exists in kdecoration master) and bundles the runtime library in the kinetic-we closure.

**Add to your flake.nix inputs:**
```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    kineticwe = {
      url = "gitlab:theblackdon/kineticwe";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };
}
```

**NixOS module (recommended — registers the Wayland session with SDDM/greetd):**
```nix
{
  imports = [ inputs.kineticwe.nixosModules.default ];
  programs.kineticwe.enable = true;
}
```

**Home Manager module** (puts `start-kineticwe` on PATH and installs the session desktop entry; use the NixOS module so the greeter lists the session):
```nix
{
  imports = [ inputs.kineticwe.homeModules.default ];
  nixpkgs.overlays = [ inputs.kineticwe.overlays.default ];
  programs.kineticwe.enable = true;
}
```

Both modules share a single option surface: `programs.kineticwe.enable` installs everything (kinetic-we + session + decorations), with optional `programs.kineticwe.package` and `programs.kineticwe.autostart`.

> **Note:** On the first build, Nix may error with a hash mismatch for the pinned `kdecoration` or `kglobalacceld` sources. Copy the expected hash from the error into `nix/kdecoration-git.nix` / `nix/kglobalacceld-git.nix` and rebuild.

### Debian / Ubuntu (distrobox build)

> **Prerequisite:** KineticWE requires **Qt ≥ 6.10** and **KF6 ≥ 6.27**. Debian **sid** already ships these (Qt6 6.10, KF6 6.26–6.28). On older suites (e.g. trixie with Qt6 6.8) enable Debian **experimental** inside the container for the needed packages.

On Debian/Ubuntu-based systems, use `build-deb-dev.sh` to build KineticWE from source inside a **`debian:sid` distrobox** — the host stays clean and the result installs to `~/.local` for immediate TTY testing:

```bash
git clone https://gitlab.com/theblackdon/kineticwe
cd kineticwe

# Prerequisites (once):
sudo apt install distrobox podman

# Quick cmake build (recommended for dev):
./build-deb-dev.sh --quick

# Or produce .deb packages:
./build-deb-dev.sh --deb

# Test from a TTY:
start-kineticwe
```

The first run creates the `kineticwe-deb-build` container (from `debian:sid`), installs every build dependency via `debian-deps.sh`, then builds `kglobalacceld` and `kdecoration` from pinned KDE master commits before compiling `kinetic-we`, the `xdg-desktop-portal-kwe` portal backend, and the `noctalia-kwe` shell.

Useful flags: `--no-session` (skip the login-screen entry), `--no-extract` (build `.debs` only), `--reset-local` (remove dev files from `~/.local`). Override the image/container via `DEBIAN_IMAGE` / `DISTROBOX_NAME`.

---

## Troubleshooting

### Power management not working (brightness, idle/DPMS, suspend, power profiles)

Power management is provided by the Noctalia process inside the KineticWE
session. It handles idle actions, lock-before-suspend, DPMS, brightness, and
session power actions without starting KDE's PowerDevil daemon. UPower and
`power-profiles-daemon` are optional freedesktop services used for battery and
power-profile information.

Check the Noctalia log for power-service initialization:

```bash
grep -E 'idle|lockscreen|logind|brightness|power profiles|upower' ~/.local/share/noctalia.log
```

The idle/DPMS settings live in `~/.config/noctalia/config.toml` under
`[lockscreen]`, `[idle]`, and `[idle.behavior.<name>]`. You can test the
compositor output path directly with:

```bash
noctalia-kwe msg dpms-off
noctalia-kwe msg dpms-on
```

---

## KWin is not...

 * a standalone Wayland compositor (c.f. labwc, sway) and does not provide any functionality belonging to a Desktop Shell.
 * a replacement for window managers designed for use with a specific Desktop Shell (e.g. GNOME Shell)
 * a minimalistic window manager
 * designed for use with network transparency, though it is possible (with e.g. waypipe).

# Contributing to KWin

Please refer to the [contributing document](CONTRIBUTING.md) for everything you need to know to get started contributing to KWin.

# Contacting KWin development team

 * IRC: #kde-kwin on irc.libera.chat
 * Matrix: [#kwin:kde.org](https://go.kde.org/matrix/#/#kwin:kde.org)

# Support
## Application Developer
If you are an application developer having questions regarding windowing systems (either X11 or Wayland) please do not hesitate to contact us.

## End user
Please contact the support channels of your Linux distribution for user support. The KWin development team does not provide end user support.

# Reporting bugs

Please use [KDE's bugtracker](https://bugs.kde.org) and report for [product KWin](https://bugs.kde.org/enter_bug.cgi?product=kwin). For **KineticWE-specific** issues, please open an issue on this repository instead.

## Guidelines for new features

A new Feature can only be added to KWin if:

 * it does not violate the primary missions as stated at the start of this document
 * it does not introduce instabilities
 * it is maintained, that is bugs are fixed in a timely manner (second next minor release) if it is not a corner case.
 * it works together with all existing features
 * it supports both single and multi screen
 * it adds a significant advantage
 * it is feature complete, that is supports at least all useful features from competitive implementations
 * it is not a special case for a small user group
 * it does not increase code complexity significantly
 * it does not affect KWin's license (GPLv2+)

All new added features are under probation, that is if any of the non-functional requirements as listed above do not hold true in the next two feature releases, the added feature will be removed again.

The same non functional requirements hold true for any kind of plugins (effects, scripts, etc.). It is suggested to use scripted plugins and distribute them separately.
