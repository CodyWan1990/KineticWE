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

The repo only ships the `kineticwe` package itself; its shell dependency `noctalia-git` comes from the AUR, so install that first with your AUR helper:

```bash
paru -S noctalia-git   # or: yay -S noctalia-git
```

Then add the repo to `/etc/pacman.conf` (e.g. `sudo nano /etc/pacman.conf`):

```ini
[kineticwe]
SigLevel = Never
Server = https://theblackdon.gitlab.io/kineticwe/$arch
```

and install:

```bash
sudo pacman -Sy kineticwe
```

#### AUR (alternative)

If you prefer to build from source, the [AUR package](https://aur.archlinux.org/packages/kineticwe) remains available — the AUR helper resolves `noctalia-git` automatically:

```bash
yay -S kineticwe
```

### NixOS / Nix

> **⚠ Work in progress:** The Nix flake is experimental — it is not fully tested or complete. **Use at your own risk**; things may be broken or change without notice.

A [Nix flake](https://nixos.wiki/wiki/Flakes) provides a reproducible build of kwin-we, the full KineticWE session (kwin-we + noctalia), and Home Manager / NixOS modules. Like the AUR and COPR packages, it builds `kdecoration` and `kglobalacceld` from pinned KDE master commits (kwin-we needs `KDecoration3::Style`, which only exists in kdecoration master) and bundles the runtime library in the kwin-we closure.

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

Both modules share a single option surface: `programs.kineticwe.enable` installs everything (kwin-we + session + decorations), with optional `programs.kineticwe.package` and `programs.kineticwe.autostart`.

> **Note:** On the first build, Nix may error with a hash mismatch for the pinned `kdecoration` or `kglobalacceld` sources. Copy the expected hash from the error into `nix/kdecoration-git.nix` / `nix/kglobalacceld-git.nix` and rebuild.

---

## Troubleshooting

### Power management not working (brightness, idle/DPMS, suspend, power profiles)

The `start-kineticwe` session launcher starts `upowerd` and `org_kde_powerdevil` automatically. It resolves distro-specific paths (`/usr/libexec` on Fedora/RHEL, `/usr/lib` on Arch) and guarantees `WAYLAND_DISPLAY` is set — PowerDevil aborts at startup if that variable is missing.

Verify PowerDevil is running and registered on D-Bus:

```bash
pgrep -f org_kde_powerdevil
busctl --user list | grep org.kde.Solid.PowerManagement
cat ~/.local/share/powerdevil.log
```

> **Note:** If you previously created a manual `powerdevil-user.service` systemd unit as a workaround, disable it to avoid launching PowerDevil twice:
>
> ```bash
> systemctl --user disable --now powerdevil-user.service
> ```

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

