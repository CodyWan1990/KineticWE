# COPR/RPM Readiness Plan

This document tracks what needs to be done before KineticWE is ready to publish
as a COPR-built RPM. The COPR package should install a working KineticWE session
with the kglobalacceld fix included. It should not install Noctalia and should
not pull development packages onto user systems.

## Target Package Shape

Build one source RPM that produces these binary RPMs:

- `kineticwe`: the compositor, runtime plugins, QML, helpers, desktop session,
  and startup wrapper needed to log in to KineticWE.
- `kineticwe-kglobalacceld`: the private runtime library from a known
  kglobalacceld commit that provides the `KGlobalAccelD` API used by KineticWE.

`kineticwe` installs the same runtime paths as Fedora's stock KWin packages, so
it must provide and obsolete `kwin`, `kwin-common`, and `kwin-libs`. It should
replace those packages in one DNF transaction rather than co-installing beside
them.

Do not produce or install a `-devel` subpackage for the COPR user-facing build.
Any headers, cmake package files, pkgconfig files, and unversioned `.so` symlinks
installed by KWin or kglobalacceld should be removed from `%{buildroot}` during
`%install`.

## Scope Exclusions

Noctalia is not part of the COPR package scope.

The RPM must not:

- Require, recommend, clone, build, or launch `noctalia`.
- Enable the Noctalia COPR in user instructions.
- Claim that Noctalia is required for the package to be usable.

KineticWE may still work with Noctalia when installed separately by a user, but
the COPR package must stand on its own as a compositor/session package.

## Current State

The repository already contains:

- `lgl-kineticwe-copr/`, a standalone COPR packaging payload that builds an
  SRPM and binary RPMs without Noctalia.
- `kwin-we.spec`, which builds KineticWE and also builds kglobalacceld first.
- `copr/Makefile`, which creates an SRPM for COPR SCM builds.
- `scripts/start-kineticwe.sh` and `scripts/kineticwe.desktop.in`, which are
  used by the manual installer but are not currently installed by CMake or listed
  in the spec.
- `install-fedora.sh`, which is useful as dependency reference but includes
  build dependencies and Noctalia behavior that must not be copied into runtime
  RPM dependencies.

## Required Packaging Work

### 1. Pin the kglobalacceld fix

Do not use `master` as `Source1` for COPR. Pin kglobalacceld to a tag or commit
archive that contains the required `KGlobalAccelD` methods:

- `keyEvent()`
- `pointerPressed()`
- `axisTriggered()`
- `resetModifierOnlyState()`

Update both `kwin-we.spec` and `copr/Makefile` so the SRPM is reproducible.
Use a source name that includes the pinned ref, for example:

```spec
%global kglobalacceld_commit <commit>
Source1: https://invent.kde.org/plasma/kglobalacceld/-/archive/%{kglobalacceld_commit}/kglobalacceld-%{kglobalacceld_commit}.tar.gz
```

In `%prep`, unpack with the matching top-level directory instead of assuming
`master`.

### 2. Keep build dependencies separate from runtime dependencies

`BuildRequires` should contain only tools, headers, cmake config packages, and
other packages required in the COPR buildroot.

`Requires` should contain only packages required at install/run time. It should
not include `*-devel`, compilers, build systems, headers, or source control
tools.

Use Fedora automatic dependency generation for shared library dependencies where
possible. Keep explicit `Requires` only for runtime pieces that are loaded by
name, launched as helpers, or needed for a complete session but may not be found
from ELF metadata.

Likely explicit runtime dependencies:

- `plasma-workspace`
- `xorg-x11-server-Xwayland`
- `xdg-desktop-portal`
- `xdg-desktop-portal-kde`
- `hwdata`
- `kscreen`
- `aurorae`
- `plasma-breeze`
- `plasma-milou`
- `kineticwe-kglobalacceld%{?_isa} = %{version}-%{release}`

Review the current explicit `Requires` list and remove library packages that
RPM can infer automatically from linked objects unless they are plugin-only or
helper-only runtime requirements.

### 3. Install the session wrapper and desktop entry

The RPM needs to install:

- `/usr/bin/start-kineticwe`
- `/usr/share/wayland-sessions/kineticwe.desktop`

For an RPM install, the startup wrapper should not contain a user-local
`INSTALL_PREFIX` or `LD_LIBRARY_PATH` setup. It should assume `/usr` paths and
let the dynamic linker find packaged libraries normally.

The desktop file should use:

```ini
Exec=/usr/bin/start-kineticwe
```

Add these files in either CMake install rules or directly in `%install` from the
spec. If the existing templates are reused, substitute `@INSTALL_PREFIX@` with
`%{_prefix}`.

### 4. Remove Noctalia startup from the RPM session

For the COPR package, `scripts/start-kineticwe.sh` needs an RPM-safe variant or
a conditional template mode that does not run:

```bash
noctalia &
```

The startup payload should still:

- Set KDE/Wayland session environment.
- Start the KDE portal backend and generic portal frontend if needed.
- Stop a competing user `plasma-kglobalaccel.service` before launching
  `kinetic-we`.
- Execute `kinetic-we --xwayland "$STARTUP_PAYLOAD"`.

If the session has no bundled shell, document the expected user experience
plainly. KineticWE should still start and manage windows; a full panel/launcher
shell is intentionally outside the RPM scope.

### 5. Make file ownership complete and non-conflicting

Run local mock builds or COPR test builds until `%files` is complete. Important
areas to verify:

- Binaries under `%{_bindir}`.
- Helpers under `%{_libdir}/libexec` or `%{_libexecdir}` as installed by KDE
  macros.
- Versioned runtime libraries only, not unversioned devel symlinks.
- KWin plugins under `%{_libdir}/plugins`.
- QML modules under `%{_libdir}/qml`.
- KCM plugins under `%{_libdir}/plugins/plasma/kcms`.
- KPackage structures under `%{_libdir}/plugins/kf6/packagestructure`.
- Data under `%{_datadir}/kwin-wayland`, `%{_datadir}/krunner`,
  `%{_datadir}/kconf_update`, `%{_datadir}/knsrcfiles`,
  `%{_datadir}/qlogging-categories6`, and `%{_datadir}/dbus-1/interfaces`.
- Session file under `%{_datadir}/wayland-sessions`.
- User systemd unit under `%{_userunitdir}`.

Avoid accidental file conflicts with Fedora's KWin packages. KineticWE currently
uses the stock KWin runtime paths, so the RPM must advertise that it replaces
`kwin`, `kwin-common`, and `kwin-libs`. If the project later moves files under
KineticWE-specific paths, the replacement metadata should be revisited.

### 6. Add runtime dependency verification

After a successful build, inspect the RPM instead of relying on the manual
installer dependency list:

```bash
rpmlint kineticwe-*.rpm
rpm -qpR kineticwe-*.rpm
rpm -qpl kineticwe-*.rpm
```

Check binaries and shared objects from the buildroot:

```bash
ldd %{buildroot}%{_bindir}/kinetic-we
ldd %{buildroot}%{_libdir}/libkwin.so.*
find %{buildroot}%{_libdir}/plugins -type f -name '*.so' -exec ldd {} \;
```

Any missing runtime loader dependency should be fixed with a real runtime
`Requires`, not by installing a `-devel` package.

### 7. Test install behavior on a clean Fedora system

Use a clean VM or container-like test environment with a Fedora release supported
by the COPR target. Test the install path as a user would run it:

```bash
sudo dnf copr enable <owner>/kineticwe
sudo dnf install kineticwe
rpm -qa | grep -- '-devel$' || true
```

The final command should not show development packages newly pulled in by
`kineticwe`.

Then validate:

- `kinetic-we --version` runs.
- `/usr/share/wayland-sessions/kineticwe.desktop` exists.
- A display manager shows a `KineticWE` Wayland session.
- Logging into the session starts `kinetic-we`.
- Keyboard shortcuts work, proving the embedded kglobalacceld path is active.
- Xwayland clients start.
- Screenshot/screencast portal behavior works through
  `xdg-desktop-portal-kde`.

## Spec Change Checklist

- Replace `Source1` `master` tarball with a pinned kglobalacceld source.
- Add `Requires: kineticwe-kglobalacceld%{?_isa} = %{version}-%{release}`.
- Remove `Recommends: noctalia-git`.
- Remove Noctalia references from package descriptions.
- Install `/usr/bin/start-kineticwe`.
- Install `/usr/share/wayland-sessions/kineticwe.desktop`.
- Ensure startup script does not launch Noctalia in RPM builds.
- Keep all `*-devel` packages in `BuildRequires` only.
- Remove devel outputs from `%{buildroot}` if no `-devel` package is shipped.
- Run `rpmlint`, `rpm -qpR`, `rpm -qpl`, and a clean-system install test.

## Done Definition

KineticWE is COPR-ready when a Fedora user can run:

```bash
sudo dnf copr enable <owner>/kineticwe
sudo dnf install kineticwe
```

and then select `KineticWE` from the Wayland session menu or run
`start-kineticwe`, without manually building kglobalacceld, installing devel
packages, or installing Noctalia.
