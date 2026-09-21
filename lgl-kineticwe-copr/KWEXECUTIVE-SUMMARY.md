# KineticWE Packaging Status

## What This Project Does

This project builds KineticWE as Fedora RPM packages. The packages include:

- The KineticWE compositor and session
- The Noctalia KWE shell
- The KineticWE desktop portal
- Session and workspace support
- An optional Noctalia greeter

The packaging files are in:

```text
/home/lgl/kineticwe/lgl-kineticwe-copr/
```

The source checkout is in:

```text
/home/lgl/kineticwe/
```

## Why This Work Exists

KineticWE is not installed as a single upstream project. It needs several
components, services, configuration files, and runtime packages to work as a
Fedora desktop session.

The packaging work makes those parts installable together, keeps KineticWE
settings separate from normal application settings, and prevents KineticWE's
files from replacing files owned by standard Fedora packages.

## How The Build Works

`build-rpms.sh` exports the committed source checkout, adds packaging-local
files, and builds an SRPM inside the Fedora 44 build container. The binary RPMs
are then built from that SRPM.

The spec controls the final RPM layout. It defines package names, dependencies,
file ownership, install actions, and subpackages.

## Current Target

The next package release is:

```text
kineticwe 0.0.1-7
```

Release 7 now contains the RPM database lock fix and has not been built yet.
RPM files already in `results/` are from older releases and do not show the
current result.

## What Is Fixed In The Packaging

The session launcher now:

- Keeps KineticWE's compositor settings separate from normal application settings.
- Uses the user's normal config directory for Qt and GTK settings.
- Keeps existing Qt and QML search paths instead of replacing them.
- Includes the runtime packages needed for the intended Qt, GTK, and screenshot setup.

Why this matters: the compositor needs a private configuration directory, but
applications such as Discover, Dolphin, and terminals must continue using the
user's normal configuration. Preserving existing Qt and QML paths also avoids
breaking paths supplied by Fedora or the user.

How it works: the packaging-local launcher sets the private paths only when it
starts KineticWE, while it resolves normal user paths for Qt, GTK, and KDE
settings.

The package metadata also now:

- Recommends `alacritty`, `qt5ct`, `qt6ct`, and `spectacle` without making them required.
- Requires matching versions of `kineticwe-noctalia` and
  `xdg-desktop-portal-kwe`.
- Uses `greetd:greetd` for the greeter tmpfiles configuration.
- Prints final install guidance from `%posttrans`, after package installation is complete.

Why this matters: optional desktop tools should not make the base session
uninstallable, and install instructions are more useful after all package
triggers have finished.

### Dependency Fix

**What:** `alacritty`, `qt5ct`, `qt6ct`, and `spectacle` are recommendations,
not hard requirements.

**Why:** They are useful desktop tools, but KineticWE can start without any one
of them. Making them required would make the base session harder to install and
would pull in software that every user may not want.

**How:** The spec uses `Recommends:` for these packages and keeps only the
packages needed by the session in `Requires:`.

### Matching Subpackage Updates

**What:** The main package requires the Noctalia shell and KineticWE portal at
the same version-release as the main package. The optional greeter also requires
that matching main package.

**Why:** The main package, shell, and portal are built as one release. Updating
only one of them can leave mismatched files or behavior.

**How:** The spec uses exact requirements such as
`kineticwe-noctalia = %{version}-%{release}`. When a later release is available,
DNF must update the installed required subpackages to satisfy those
requirements. The greeter remains optional, but if it is installed it is tied
to the matching main package.

**Same-release limitation:** Rebuilding a package with the same NEVRA,
`0.0.1-7`, cannot force RPM to replace an already-installed older `0.0.1-7`.
That is why an existing broken `-7` requires an explicit reinstall. Future
release changes will update normally.

### Configuration Fix

**What:** KineticWE uses `~/.config/kineticwe` for compositor settings, while
normal applications keep using the user's normal XDG config directory.

**Why:** A global `XDG_CONFIG_HOME=~/.config/kineticwe` would make applications
such as Discover, Dolphin, and terminals read and write settings in the wrong
place.

**How:** The launcher sets `XDG_CONFIG_HOME` only on the compositor command. It
uses `KWE_REAL_CONFIG_HOME` when it needs to read or create shared user files.

### Qt And QML Path Fix

**What:** The launcher adds KineticWE's Qt plugin and QML directories without
discarding existing paths.

**Why:** Replacing `QT_PLUGIN_PATH`, `QML_IMPORT_PATH`, or `QML2_IMPORT_PATH`
can hide Fedora or user-installed plugins. Missing KineticWE paths can also
leave compositor effects or portal features unavailable.

**How:** The launcher puts its required directories first, then appends the
existing environment values.

### Greeter Ownership Fix

**What:** The greeter tmpfiles entry uses `greetd:greetd` instead of the generic
`greeter:greeter` account.

**Why:** Fedora's `greetd` package creates and runs the greeter account as
`greetd`. Using a different account can prevent tmpfiles from creating or
updating the required directory.

**How:** The spec replaces the upstream account names while installing the
greeter tmpfiles file.

### GTK Theme Dependency

**What:** `adw-gtk3-theme` is a required package.

**Why:** The GTK setup selects the Adwaita GTK3 theme files. Without the
package, the generated GTK settings can refer to a theme that is not installed.

**How:** The spec lists `adw-gtk3-theme` in `Requires:` so a fresh install has
the theme files available.

### Install Message

**What:** The package prints the session and greeter setup instructions after
the transaction finishes.

**Why:** Printing during an individual package script can show incomplete or
misleading instructions while other packages are still being installed.

**How:** The spec uses `%posttrans` to print one final message after package
triggers have run.

### RPM Database Lock Fix

**What:** Release 7 removes `rpm -q` calls from the package `%posttrans`
scripts.

**Why:** RPM already holds the transaction lock while running `%posttrans`.
Starting another `rpm` query from inside that scriptlet tried to acquire the
same lock and caused fresh installs of release 7 to fail.

**How:** The main package checks whether the greeter setup command exists on
disk. The greeter package prints its own message without querying RPM. This
avoids opening a nested RPM transaction while the original transaction is
still running.

## Portal Conflict Fix

The KineticWE portal is based on the KDE portal, so it originally generated
files with KDE names. That caused file conflicts when both portals were
installed.

What was wrong: both packages tried to install files such as
`xdg-desktop-portal-kde.mo` and `xdp-kde.categories`.

Why it matters: RPM cannot safely install two packages that own the same file,
even when the files come from different portal implementations.

The release 7 spec changes the KineticWE portal names to:

- `xdg-desktop-portal-kwe.mo` for translations
- `xdp-kwe.categories` for logging categories

The main package excludes these files, and the
`xdg-desktop-portal-kwe` package owns them. This should allow KineticWE and the
stock KDE portal to be installed together.

This fix is in the spec, but still needs a release 7 build and install test.

How it works: `%prep` changes the names in the unpacked portal source, and the
`%files` sections give the renamed files to the portal subpackage instead of the
main package.

### Release 7 Versus Long-Term Support

The **coexistence design is long-term**: KDE and KineticWE use different
backend names, D-Bus services, portal metadata, translation files, and logging
category files. KDE sessions select the KDE backend, while KineticWE sessions
select the KWE backend.

The **release 7 implementation is temporary**: the spec uses text substitutions
against the current portal source during `%prep`. It should work with the
current pinned source layout, but release 7 has not been built yet. A future
source update could change the files or text that the substitutions expect.

Portal source changes are managed separately and are not part of the
`build-rpms.sh` work. The current packaging workaround remains in the spec.

## Source Safety

The build script creates the main source archive from committed Git `HEAD` on
the `kineticwe-2.0` branch.

The build uses the committed source `HEAD` from `kineticwe-2.0`. Portal source
maintenance is separate from the packaging files.

## Build Status

Completed:

- Release 7 spec changes
- Release 7 RPM database lock fix
- Packaging-local launcher changes
- Build script syntax checks from earlier work
- Release 7 SRPM generation and spec validation

Still needed:

- Build the release 7 binary RPMs.
- Check the generated file lists.
- Install the new packages alongside the stock KDE portal.
- Confirm that no `xdg-desktop-portal-kde.mo` or `xdp-kde.categories` files are shipped.

## Remaining Issue

Dolphin and Konsole can still look different when started through one Kinetic
Settings or KWin shortcut path. They look correct when started from Noctalia.

An earlier launch-environment patch did not fix this and was removed. The exact
component that owns the shortcut must be identified before another change is
made.
