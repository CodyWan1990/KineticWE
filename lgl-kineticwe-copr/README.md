# LGL KineticWE COPR Packaging

This folder contains the COPR/RPM packaging payload for KineticWE with the
KineticWE runtime, the private kglobalacceld runtime library, and a dependency
on Noctalia v5 from `lionheartp/Hyprland`.

It is intended to be used from the repository root:

```bash
make -f lgl-kineticwe-copr/Makefile srpm
```

To rebuild binary RPMs from inside the Fedora distrobox:

```bash
./lgl-kineticwe-copr/rebuild-rpms-in-distrobox.sh
```

For COPR SCM builds, set the build command to:

```bash
make -f lgl-kineticwe-copr/Makefile srpm
```

The build creates:

- `kineticwe`: KineticWE binaries, plugins, QML, helpers, Wayland session entry,
  `/usr/bin/start-kineticwe`, and the runtime kglobalacceld library KineticWE
  links against for the direct `KGlobalAccelD` API.

Noctalia is not bundled into the KineticWE RPM. It is pulled as the
`noctalia-shell-v5 >= 5.0.0` dependency from the `lionheartp/Hyprland` COPR
repo, which provides `/usr/bin/noctalia`.

Enable both the KineticWE COPR and the Noctalia COPR before installing from a
repo. For local RPM testing, enable the Noctalia COPR first:

```bash
sudo dnf copr enable lionheartp/Hyprland
```

Install the single runtime RPM:

```bash
sudo dnf install ./kineticwe-*.rpm
```

The `kineticwe` package provides and obsoletes Fedora's `kwin`, `kwin-common`,
and `kwin-libs` packages. This is intentional: KineticWE installs the same KWin
runtime paths, so it must replace the stock compositor package set rather than
co-install beside it.

`/usr/bin/start-kineticwe` starts Noctalia during session startup and logs it to
`~/.local/share/noctalia.log`.

## kglobalacceld source

By default the Makefile fetches kglobalacceld from `master`, matching the
current project build scripts. For a reproducible COPR release, override it with
a known-good commit or tag that contains:

- `KGlobalAccelD::keyEvent()`
- `KGlobalAccelD::pointerPressed()`
- `KGlobalAccelD::axisTriggered()`
- `KGlobalAccelD::resetModifierOnlyState()`

Example:

```bash
make -f lgl-kineticwe-copr/Makefile srpm KGLOBALACCELD_REF=<commit-or-tag>
```

Use the same `KGLOBALACCELD_REF` in COPR project build options if you pin the
source.

## Local checks

After building RPMs, inspect the results:

```bash
rpmlint kineticwe-*.rpm
rpm -qpR kineticwe-*.rpm
rpm -qpl kineticwe-*.rpm
```

Install-test on a clean Fedora VM:

```bash
sudo dnf copr enable lionheartp/Hyprland
sudo dnf install ./kineticwe-*.rpm
rpm -qa | grep -- '-devel$' || true
```

Installing `kineticwe` should not pull development packages.
