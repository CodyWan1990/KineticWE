# Configuration And Theming Findings

## Scope

These findings describe the packaging-local KineticWE session launcher. They
do not describe changes to the upstream source project. Portal source
maintenance is managed separately.

## Problems Found

- The launcher used to replace existing Qt and QML search paths. This could
  hide paths needed by other applications.
- Qt settings were seeded under `$HOME/.config` even when the user selected a
  different `XDG_CONFIG_HOME`.
- GTK, KDE, and Qt settings could therefore point to different configuration
  locations.
- GTK theme selection needs the `adw-gtk3-theme` package.
- The Noctalia launcher and Kinetic Settings may use different launch paths.

The common cause was path ownership: KineticWE needs private compositor paths,
while normal applications need the user's standard XDG paths. Replacing one
with the other changes where applications find themes, plugins, and settings.

## Fixes In The Launcher

The packaging-local `start-kineticwe.sh` now:

- Preserves `QT_PLUGIN_PATH`.
- Preserves `QML_IMPORT_PATH` and `QML2_IMPORT_PATH`.
- Uses the resolved user config directory for Qt settings.
- Keeps KineticWE's private compositor config separate from normal application
  config.
- Keeps the relevant GTK, KDE, and Qt paths aligned.

These fixes are included in the release 7 packaging state.

What the launcher does: it gives KineticWE private paths only when starting the
compositor and keeps the normal paths available to applications.

Why this matters: Discover, Dolphin, terminals, and GTK applications can then
continue using their normal configuration while still receiving the KineticWE
theme setup.

### Qt And QML Search Paths

**What:** KineticWE's plugin and QML directories are added before existing
search paths.

**Why:** KineticWE needs to find its compositor plugins and private QML modules,
but other applications may also need paths already supplied by Fedora or the
user.

**How:** The launcher builds the KineticWE path list and appends the previous
`QT_PLUGIN_PATH`, `QML_IMPORT_PATH`, and `QML2_IMPORT_PATH` values instead of
replacing them.

### Qt Theme Configuration

**What:** `qt5ct` and `qt6ct` are configured to use Noctalia's generated color
scheme.

**Why:** Qt applications need to read the same color scheme that Noctalia
generates. Otherwise Qt applications can use a different theme from the shell.

**How:** The launcher creates each config only if it is missing and writes the
resolved user config path into `color_scheme_path`. Existing font and interface
settings are left alone.

## Package Settings

The main package requires the core runtime packages, including:

- `adw-gtk3-theme`
- `plasma-breeze`
- `plasma-breeze-common`
- `breeze-icons`
- `xdg-desktop-portal`
- `xdg-desktop-portal-kwe`

The following are recommended, but are not required:

- `alacritty`
- `qt5ct`
- `qt6ct`
- `spectacle`

The required packages provide the session, portal, Breeze resources, and GTK
theme support. The recommended packages add common desktop tools but are not
needed by the compositor itself.

The split keeps the base package small and installable while still suggesting
the tools used by the default KineticWE setup.

The required KineticWE subpackages are version-matched to the main package, so
future package upgrades keep the compositor, shell, and portal in sync. The
optional greeter is version-matched only when it is installed.

The portal package follows the same short-term versus long-term distinction as
the main packaging summary: the separate backend and desktop-selection design
is intended to be permanent, while the current release 7 source rename is a
spec-time patch that still needs a tested, fail-safe build-script implementation.

## Remaining Shortcut Issue

Applications launched from Noctalia use the expected theme. Applications
launched through one Kinetic Settings or KWin shortcut path may use another
theme.

An experiment that reset the compositor's environment for child processes did
not fix the issue and was removed. The next step is to identify which component
owns the shortcut before changing the launch environment again.
