# KineticWE Independence Plan — Coexisting With KDE

Status: Draft, awaiting implementation approval.
Scope: Make KineticWE install **beside** KDE instead of replacing KWin, own every
artifact it ships (including window decorations), and hard-fork upstream KWin.
Branch: `kineticwe-2.0`.
Related: `docs/kineticwe-2.0.md`, `PACKAGING-COPR.md`, `lgl-kineticwe-copr/*`.

---

# Part A — Plain English

## What problem we are solving

KineticWE is a rebuilt version of KWin. Right now the installer is built to
**take KWin's place**: it claims to *be* KWin, it installs its files into the
same folders KWin uses, and the system is forced to remove KDE's KWin to make
room. That is why installing KineticWE beside KDE fails.

We are changing the installer so KineticWE is its **own thing** that sits next
to KDE. Both can be installed at once. You pick one at the login screen.

## What we are changing

1. **Stop pretending to be KWin.** The package stops saying "I am KWin" and
   stops asking the system to remove KWin.
2. **Give every installed file its own name and folder.** Nothing KineticWE
   installs will land on a file KDE owns. Same idea as giving two people in a
   shared house their own labelled shelves instead of both using the same shelf.
3. **Own our window decorations.** Instead of borrowing KDE's "Breeze" title-bar
   style, we build our own decoration called **Kinetic**. It is written in QML
   (the same style of code as the Noctalia shell), so we fully control how it
   looks, and it can recolor itself in real time as the theme changes.
4. **Drop the KDE Settings pages.** KineticWE has its own settings app; the
   KDE-style settings pages only cause collisions, so they go.
5. **Hard-fork the base.** We stop chasing KWin releases and freeze a known
   good version. New KWin features are pulled in one at a time, on purpose.

## What we are deliberately NOT changing

Some names say "KDE" but are invisible plumbing that other software depends on.
Renaming them would break things and would not help us coexist. We keep:

- **The compositor's runtime "phone extension" names** (`org.kde.KWin`, etc.).
  These are not files, so they cause zero install conflicts. Keeping them is
  what lets **Spectacle screenshots**, **KDE display tools**, and **KDE global
  shortcuts** keep working exactly as they do today. (Konsole and Dolphin never
  used these anyway.)
- **The desktop's wire language** (protocol names like
  `org_kde_plasma_virtual_desktop`). The Noctalia shell and the portal speak
  this. Renaming it would break them for no benefit.
- **The shared KDE libraries** (KF6). These are shared "spare parts"; they do
  not clash.

## The decoration decision (important)

We looked at three kinds of window decorations:

- **Breeze** (KDE's own) recolors live because it reads the desktop's shared
  color file. But it is KDE's, and we would have to keep rebuilding it.
- **Aurorae themes** (like the bundled Layan) bake their colors into pictures,
  so they **never** recolor live — this is true of every Aurorae theme, not just
  Layan.
- **QML decorations** (like "Plastik") are small programs, so they *can* read
  the shared color file and recolor live.

**We built our own decoration ("Kinetic").** It is a first-party KDecoration3
C++ plugin (`src/plugins/kineticdecoration`, plugin id `kineticwe`) compiled
into the compositor. This gives us:

- A standard-looking title bar we fully own.
- **Real-time recoloring** as the theme changes (Noctalia already writes the
  color file and signals the desktop).
- Server-side window **shadows** preserved.
- No dependency on KDE's Breeze package, the **aurorae** engine, or any other
  decoration theme engine.

The Aurorae engine (previously built from a pinned commit), its vendored Layan
themes and the Plastik QML decoration it shipped have been **removed**; the
Kinetic plugin is the only window decoration.

## What we lose

- **Third-party KDE add-ons** — downloaded KWin effects, KWin scripts, and
  third-party title-bar styles won't fit KineticWE anymore. We accept this.
- **The `kwin_wayland` program names.** Only KDE tooling and greeters that
  hardcode those names care; our own session uses `kinetic-we`.
- **Plasma-session integration** (running a full KDE Plasma desktop on our
  compositor). Not a goal.
- **KDE Settings pages** (already intentionally dropped).

## What we do NOT lose

- Spectacle screenshots and KDE display tools (we keep the phone-extension name).
- All **built-in desktop effects** — wobbly windows, blur, magic lamp, glide,
  overview, and the rest — because they are compiled into the fork and ship
  with it.
- **Portals** (screen sharing, screenshots, file dialogs, notifications for
  Firefox, Chromium, OBS, Flatpak apps). Portals are desktop-neutral and never
  depended on the "replace KWin" trick.
- Konsole, Dolphin, Kate and other apps.
- Global keyboard shortcuts.
- The Noctalia shell.

## How we will know it worked

On a clean Fedora machine with KDE already installed, we can install KineticWE
and:

- See no file conflicts and no "remove KWin" prompts.
- Log into either the KDE session or the KineticWE session.
- Take a screenshot with Spectacle inside KineticWE.
- See the Kinetic title bars recolor when the theme changes.
- See wobbly windows and the other effects still available.

---

# Part B — Technical Plan

## B0. Locked decisions

| Decision | Choice | Reason |
|---|---|---|
| Overall model | Namespaced **coexistence** (Option B), all-or-nothing, one flavor | User decision |
| Package identity | Drop `Provides`/`Obsoletes` for `kwin*`, `kglobalacceld`, `kdecoration` | These force removal of KDE |
| D-Bus interface names | **Keep** `org.kde.KWin*`, `org.kde.kwin.*`, `org.kde.kglobalaccel` | Preserves Spectacle/kscreen/scripts; no file conflict |
| Wire protocols | **Keep** `org_kde_plasma_virtual_desktop`, `kde-output-device-v2`, `zkde_screencast_unstable_v1`, etc. | Noctlalia/portal depend on them |
| KF6 libraries | **Keep** linking | Shared libs, no conflict, from-scratch rewrite out of scope |
| KCMs | **Drop** | Owned settings app; large collision surface |
| Decorations | First-party **Kinetic C++ decoration** (`org.kineticwe.decoration`); drop Breeze requirement | Full ownership + live recolor |
| Decoration engine | **Drop aurorae** entirely; the first-party **Kinetic** KDecoration3 plugin is the only window decoration | No external engine needed |
| kdecoration library | Vendor + **rename** (keep `KDecoration3::Style` shadow support) | Cross-distro consistency; keep server-side shadows |
| kglobalacceld | Vendor + **rename** the private lib | Only used by us; keep `org.kde.kglobalaccel` API |
| Upstream strategy | **Hard fork** — freeze base, cherry-pick deliberately | User decision |
| `kwin_wayland`/`kwin_wayland_wrapper` | **Drop** the names; ship `kineticwe.service` | KDE-only tooling |
| KGlobalAccel component | **Rename** `kwin` → `kineticwe` (+ display "KineticWE"), with migration | Full ownership; no coexistence cost |
| Keyboard-layout component | **Rename** to `KineticWE Keyboard Layout Switcher` | Ours |
| `kaccess` component | **Keep** | Accessibility convention; KDE-app expectation |
| App identity (`KAboutData("kwin")`) | **Defer** (Option 3) until after Phase 5 | Broad blast radius; needs audit |
| `KWIN_*` env vars | **Keep** | User/dev escape hatches; rename later if desired |
| `kwinoutputconfig.json` | **Rename** → `kineticweoutputconfig.json` | Consistency (private config root) |
| `org.kde.kwin.Scripting` D-Bus | **Keep** | Third-party script compatibility |
| Helper desktop files (`org.kde.kwin.dialoghelper`, `.killer`) | **Rename** files/ids | They are files |

## B1. Package identity (RPM/DEB/PKG/ Nix)

Remove from `lgl-kineticwe-copr/kineticwe.spec` and mirror in
`kineticwe.spec`, `PKGBUILD`, `debian/control`:

- `Provides: kwin`, `kwin-common`, `kwin-libs`, `kwin-wayland`,
  `kglobalacceld`, `kglobalacceld-devel`, `kdecoration`, `kdecoration-devel`
- `Obsoletes: kwin`, `kwin-common`, `kwin-libs`, `kwin-wayland`,
  `kglobalacceld`, `kglobalacceld-devel`, `kdecoration`, `kdecoration-devel`
- PKGBUILD `replaces=(...)`, `provides=(...)`, `conflicts=(...)` for the same
  names.

Keep `Requires: breeze-icons` only if still used for icons; **remove**
`Requires: plasma-breeze`, `plasma-breeze-common` once the Kinetic decoration
lands (they currently supply the Breeze decoration plugin).

Add (only if a distro package would otherwise want to pull KDE's KWin in):
nothing. Two compositors can be installed side by side; there is no
`Conflicts: kwin`.

## B2. Filesystem rename inventory (the core work)

Rule: **rename anything that is an installed file, directory, or plugin
identifier that the distro KWin/aurorae/kdecoration packages also install.**
Everything else stays.

### B2.1 Executables

| Current | New | Source |
|---|---|---|
| `kinetic-we` | (unchanged) | already renamed |
| `kinetic-we_wayland_wrapper` | (unchanged) | `src/helpers/wayland_wrapper/` |
| `kwin_wayland` (symlink) | **drop** | spec `%install`, `PKGBUILD` |
| `kwin_wayland_wrapper` (symlink) | **drop** | spec `%install`, `PKGBUILD` |
| `kwindowprop` | `kineticwe_windowprop` | `src/helpers/kwindowprop/` |
| `kwin_dialog_helper` | `kineticwe_dialog_helper` | `src/helpers/dialoghelper/` |
| `kwin_eis_prompter` | `kineticwe_eis_prompter` | `src/helpers/` |
| `kwin_killer_helper` | `kineticwe_killer_helper` | `src/helpers/killer/` |
| `kwin-applywindowdecoration` | `kineticwe-applywindowdecoration` | `src/kcms/decoration/` |
| `kwin-tabbox-preview` | `kineticwe-tabbox-preview` | `src/kcms/tabbox/` (drop with KCMs) |

### B2.2 Libraries (soname + target rename)

| Current | New | Notes |
|---|---|---|
| `libkwin.so.*` | `libkineticwe.so.*` | CMake target `kwin` → `kineticwe`; all in-tree plugins link it |
| `libkcmkwincommon.so.*` | `libkineticwe-kcmcommon.so.*` | dropped with KCMs |
| `libkdecorations3.so.*` | `libkineticwe-decoration.so.*` | vendored kdecoration, renamed |
| `libkdecorations3private.so.*` | `libkineticwe-decoration-private.so.*` | vendored |
| `libKGlobalAccelD.so.*` | `libkineticwe-globalacceld.so.*` | vendored kglobalacceld |

`libkwin` rename is safe: only the compositor and its in-tree plugins link it.
No stock KDE application links `libkwin`.

### B2.3 Plugin / QML directories

| Current | New |
|---|---|
| `%{_qt6_plugindir}/kwin/` | `%{_qt6_plugindir}/kineticwe/` |
| `%{_qt6_plugindir}/org.kde.kdecoration3/` | `%{_qt6_plugindir}/org.kineticwe.decoration/` |
| `%{_qt6_plugindir}/org.kde.kdecoration3.kcm/` | dropped with KCMs |
| `%{_qt6_plugindir}/plasma/kcms/` | dropped with KCMs |
| `%{_qt6_plugindir}/kf6/packagestructure/` | `%{_qt6_plugindir}/kf6/packagestructure/kineticwe/` (package structures) |
| `%{_qt6_qmldir}/org/kde/kwin/` | `%{_qt6_qmldir}/org/kineticwe/kineticwe/` |

Corresponding source changes:
- `KDE_INSTALL_PLUGINDIR/kwin` → `.../kineticwe` in every `install()` and
  `kcoreaddons_add_plugin(... INSTALL_NAMESPACE ...)`.
- `src/decorations/decorationbridge.cpp:38` `s_pluginName` `"org.kde.kdecoration3"`
  → `"org.kineticwe.decoration"`.
- `src/kcms/decoration/declarative-plugin/previewbridge.cpp` `s_pluginName` /
  `s_kcmName` (KCM dropped, but the decoration model also references it).
- QML URIs: `org.kde.kwin.private.kdecoration`,
  `org.kde.kwin.overview`, `org.kde.kwin.outputlocator`, etc. → `org.kineticwe.*`
  (both the `ecm_add_qml_module(URI ...)` and every `import` statement).
- `X-KDE-ConfigModule: kwin/effects/configs/kcm_kwin4_genericscripted` in
  effect metadata → `kineticwe/effects/configs/...` (or drop; KCM dropped).

### B2.4 Data directories

| Current | New | Source |
|---|---|---|
| `%{_datadir}/kwin-wayland/` | `%{_datadir}/kineticwe-wayland/` | `src/plugins/CMakeLists.txt:30`, effect/script data |
| `%{_datadir}/kwin/decorations/...` | `%{_datadir}/kineticwe/decorations/...` | decoration packages (see B3) |
| `%{_datadir}/krunner/` | `%{_datadir}/krunner/kineticwe-*` | unique filenames only |

(The `%{_datadir}/kwin/aurorae/` and `%{_datadir}/aurorae/themes/Layan{,-solid}/`
entries are gone — the Aurorae engine and its vendored themes were removed, see
B3.3.)

The compositor resolves `kwin-wayland` data through `QStandardPaths`; the
search path/name must be updated in-tree where it hardcodes `kwin-wayland`.

### B2.5 Translations, logging, notifications, updates

| Current | New | Notes |
|---|---|---|
| `TRANSLATION_DOMAIN="kwin"` | `"kineticwe"` | `src/CMakeLists.txt:28,448`, `src/plugins/CMakeLists.txt` |
| `po/*/kwin.po` | `po/*/kineticwe.po` | plus `po/CMakeLists.txt` / `ki18n_install(po)` |
| `kcm_kwin*.po` / `kcmkwm.po` etc. | **dropped** | KCMs removed |
| `.../qlogging-categories6/kwin*.categories` | `kineticwe*.categories` | file rename; identifier strings may stay |
| `.../knotifications6/kwin.notifyrc` | `kineticwe.notifyrc` | `src/kwin.notifyrc` |
| `.../kconf_update/kwin*`, `kwin.upd` | **drop** | retired in favor of `.kwe` first-run seeding |
| `.../knsrcfiles/kwin*` | **drop** | KNewStuff for KCMs |
| `.../dbus-1/interfaces/org.kde.kwin*.xml` | install under `.../kineticwe/` or drop | build-time codegen only; currently deleted |
| `icons/hicolor/*/apps/kwin.*` | `kineticwe.*` | `%{_datadir}/icons/...` |
| `applications/org.kde.kwin.*.desktop` | `applications/org.kineticwe.*.desktop` | dialog helper, killer |
| `applications/kcm_*.desktop` | **drop** | KCMs |
| `%{_datadir}/config.kcfg/` | **drop** | KCM-only KConfigXT data |

### B2.6 systemd / session units

| Current | New |
|---|---|
| `plasma-kwin_wayland.service` | `kineticwe.service` (`ExecStart=…/kinetic-we_wayland_wrapper --xwayland`) |
| `plasma-xdg-desktop-portal-kwe.service` | `kineticwe-xdg-desktop-portal-kwe.service` |
| `kineticwe-session.target`, `kineticwe-workspace.target` | unchanged |

`plasma-kwin_wayland.service.in` → `kineticwe.service.in`, `BusName`
`org.kde.KWinWrapper` kept or renamed to `org.kineticwe.KWinWrapper` (runtime
only; keep is fine).

### B2.7 Keep list (do NOT rename)

- D-Bus interface/name strings `org.kde.KWin*`, `org.kde.kwin.*`,
  `org.kde.kglobalaccel` (defined in `src/dbusinterface.h`, `src/*.xml`,
  `src/globalshortcuts.cpp`).
- Wire protocols and their `kde-output-*.xml`, `org_kde_plasma_*`.
- `org_kde_plasma_virtual_desktop` usage in `shell/noctalia`.
- KF6 libraries and their build deps.
- `KWin/Effect`, `KWin/Script`, `KWin/Decoration` KPackage structure **keys**
  (metadata values, not paths) — unless we choose to rename them; not required.
- Effect plugin IDs such as `kwin4_effect_*` — **keep as-is** (they say
  "kwin", not "kde", and are just config strings).

## B3. First-party Kinetic decoration (C++)

### B3.0 Why C++, not QML

KWin's `DecorationBridge` loads only C++ `KPluginFactory` plugins under the
decoration plugin namespace (`src/decorations/decorationbridge.cpp:130`), and
the picker discovers them the same way (`src/kcms/decoration/decorationmodel.cpp`).
Nothing in the tree calls `KPackage::PackageLoader` for decorations, and
`KDecoration3` has no QML loader, so a QML KPackage decoration would not load.
The Kinetic decoration is therefore a small C++ `KDecoration3::Decoration`
plugin. (Option A, chosen over the thin-C++-loader/QML hybrid.)

### B3.1 What we build

In-tree plugin at `src/plugins/kineticdecoration/`:

- `kineticdecoration.{h,cpp}` — `KDecoration3::Decoration` subclass: paints the
  frame and title bar, draws the caption, hosts the button groups.
- `kineticbutton.{h,cpp}` — `KDecoration3::DecorationButton` subclass drawing
  flat minimize/maximize/close/menu/... glyphs.
- `kineticwe.json` — KPlugin metadata: `Id=kineticwe`, `Name=Kinetic`, and the
  `org.kineticwe.decoration` settings key declaring `titled` + `shadow` styles.

Installed via `kcoreaddons_add_plugin(... INSTALL_NAMESPACE
"org.kineticwe.decoration")` to `.../plugins/org.kineticwe.decoration/`.

Live recolor: colors come from `window()->color(ColorGroup, ColorRole)`
(Frame/TitleBar/Foreground), which KWin resolves from the shared color scheme
through `DecorationPalette` (watching `kdeglobals`) and republishes through
`DecoratedWindow::paletteChanged`; the plugin repaints on that signal.

### B3.2 Wiring changes

- `src/decorations/decorationbridge.cpp`: `s_pluginName =
  "org.kineticwe.decoration"`, `s_defaultPlugin = "kineticwe"`, with an
  unloadable configured plugin falling back to `kineticwe` (no Aurorae
  fallback).
- `src/plugins/kpackage/decoration/decoration.cpp`: default package root
  `kwin/decorations/` to `kineticwe/decorations/`.
- `shell/noctalia/kinetic-settings/src/appearance_page.cpp`: default decoration
  becomes `("org.kineticwe.decoration", "kineticwe")` and the tile label is
  "Kinetic".
- Drop `Requires: plasma-breeze` and `plasma-breeze-common` (both specs and
  `install-runtime-deps.sh`).

### B3.3 Decoration engine removal (done)

The Aurorae engine is no longer built or shipped. It used to be vendored from a
pinned KDE commit and namespaced in `%prep`/prep (plugin namespace, QML URI,
data dirs, helper, KNS) to coexist with Fedora's `aurorae`; its vendored Layan
themes and the Plastik QML decoration it shipped are gone too. The first-party
Kinetic plugin (B3.1/B3.2) is now the only window decoration.

Applied across both RPM specs, `lgl-kineticwe-copr/build-rpms.sh`, `PKGBUILD`,
`.SRCINFO`, `build-arch-native.sh`, `debian/control`, the Nix package,
`install-runtime-deps.sh`, `PACKAGING-COPR.md`, `.kde-ci.yml`, the integration
tests and `data/kineticwe.categories`.

## B4. Session & config identity

Already designed in `docs/kineticwe-2.0.md` Part 6 / Phase 5:

- `XDG_CURRENT_DESKTOP=KineticWE`, `XDG_SESSION_DESKTOP=KineticWE`.
- Config root `~/.config/kineticwe/` with `kineticwe.kwe`, `rules.kwe`,
  `kglobalaccelrc`, `kglobalshortcutsrc`, `kwinoutputconfig.json`.
- Compositor-only `XDG_CONFIG_HOME` redirect in `scripts/start-kineticwe.sh`.
- Remove `KDE_FULL_SESSION`, `KDE_SESSION_VERSION`, `XDG_MENU_PREFIX=plasma-`.
- Retire `kconf_update/` in favor of first-run seeding.

### B4.1 KGlobalAccel component rename (`kwin` → `kineticwe`)

Full ownership of the compositor's own shortcut component. No coexistence
impact (config keys in a private config root), but requires a migration.

Source sites:
- `src/useractions.cpp:811,1229`
- `src/tabbox/tabbox.cpp:309`
- `src/virtualdesktops.cpp:964,976,988`
- `src/tablet_input.cpp:118`
- `src/scene/workspacescene.cpp:113`
- `src/plugins/nightlight/nightlightmanager.cpp:75`
- `src/plugins/eis/eisinputcapturemanager.cpp:138`
- `src/helpers/dialoghelper/dialoghelper.cpp:27` (lookup)
- `src/keyboard_layout.cpp:45,53,135` → `KineticWE Keyboard Layout Switcher`
- live `i18n("KWin")` display names → `i18n("KineticWE")`

Defaults and migration:
- `KineticWE Default Binds.kksrc`: `[kwin][Global Shortcuts]` →
  `[kineticwe][Global Shortcuts]`.
- One-time session-script migration: `~/.config/kineticwe/kglobalshortcutsrc`
  group `[kwin]` → `[kineticwe]` (same pattern as the Spectacle migration).

**Deferred (Option 3, post-Phase 5):** rename `KAboutData("kwin")` app identity
(which would also rename the default component). Audit
`QCoreApplication::applicationName()`, desktop-file name, default `KSharedConfig`
filenames, crash ID, and window identity before doing it.

## B5. Upstream hard-fork policy

1. Freeze the base at the current KWin version (6.7.80 line). Record the exact
   upstream commit in `docs/upstream-sync.md`.
2. Keep an `upstream` git remote for reference; **never** rebase/merge wholesale.
3. Cherry-pick upstream fixes/features individually into `src/`, each as its own
   commit with a note in `docs/upstream-sync.md`.
4. Keep the vendored subprojects (kglobalacceld, kdecoration, kconfig
   compiler, greeter) pinned by commit in the spec/PKGBUILD/debian/nix; bump
   deliberately, not automatically.
5. Add a CI check that fails if a build installs any path owned by the distro
   `kwin`, `kwin-libs`, `kdecoration`, or `kglobalacceld` packages.

## B6. Phased task breakdown

### Phase 0 — Freeze & document
- [ ] Create `docs/upstream-sync.md` with base commit and cherry-pick log.
- [ ] Add the CI "no KWin-owned paths" check (B5.5).

### Phase 1 — Package identity
- [ ] Remove `Provides`/`Obsoletes` for KWin/kdecoration/kglobalacceld in all
      four packagings.
- [ ] Remove `kwin_wayland`/`kwin_wayland_wrapper` symlinks.
- [ ] Replace `plasma-kwin_wayland.service` with `kineticwe.service`.
- [ ] Drop KCM build/install (`%files` entries, `kwincms` targets, locale).

### Phase 2 — Filesystem namespacing
- [ ] Rename CMake targets: `kwin` → `kineticwe`, kdecoration, kglobalacceld.
- [ ] Change all `install()` destinations per B2.3–B2.6.
- [ ] Rename helper executables per B2.1.
- [ ] Rename translation domain and `po/` files (B2.5).
- [ ] Rename logging/notification/icon/desktop files (B2.5).
- [ ] Update `%files` in both specs, `PKGBUILD`, `debian/*`, `nix/*`.
- [ ] Update `build-rpm-dev.sh` cleanup paths that reference old names.

### Phase 3 — Kinetic decoration (implemented)
- [x] Add the first-party C++ plugin at `src/plugins/kineticdecoration/`
      (`kineticdecoration.*`, `kineticbutton.*`, `kineticwe.json`).
- [x] `decorationbridge.cpp`: `s_pluginName`/`s_defaultPlugin` -> `org.kineticwe.decoration`/`kineticwe`.
- [x] `kpackage/decoration` root -> `kineticwe/decorations/`.
- [x] Kinetic Settings default -> `("org.kineticwe.decoration", "kineticwe")`, label "Kinetic".
- [x] Drop `plasma-breeze*` (specs + `install-runtime-deps.sh`).
- [x] Remove the Aurorae engine from all packagings and dev scripts, its
      vendored Layan themes, the Plastik QML decoration it shipped, the aurorae
      KPackage structure and the `org.kde.kwin.aurorae.v2` fallback (which now
      resolves to `kineticwe`).
- [ ] Verified by the user's next build (the removal is verified only by a
      build).

### Phase 4 — Identity & session
- [ ] Session env changes (B4).
- [ ] Rename the KGlobalAccel component `kwin` → `kineticwe` + keyboard-layout
      component, re-key `.kksrc`, add the `kglobalshortcutsrc` migration (B4.1).
- [ ] Rename `kwinoutputconfig.json` → `kineticweoutputconfig.json`.
- [ ] Rename the dialoghelper/killer desktop files (B2.5).
- [ ] Retire `kconf_update`; add first-run seeding if not already present.
- [ ] (Deferred) App identity `KAboutData` rename — after Phase 5 + audit.

### Phase 5 — Verification & packaging
- [ ] Clean-VM coexistence test (B7).
- [ ] Update `ci/smoke.sh` assertions (no KDE-named files shipped).
- [ ] Update docs (`PACKAGING-COPR.md`, `README.md`, `docs/kineticwe-2.0.md`).

## B7. Verification & acceptance criteria

On a clean Fedora VM with **stock KDE installed**:

1. `sudo dnf install ./kineticwe-*.rpm` completes with:
   - no `kwin`/`kdecoration`/`kglobalacceld` removal, and
   - no file-conflict errors.
2. `rpm -qpl kineticwe*.rpm | grep -E '/(kwin|kdecorations|kglobalaccel)'` is
   empty (no KDE-owned paths shipped).
3. KDE session still boots unchanged after installing KineticWE.
4. KineticWE session boots; `kinetic-we` runs.
5. Spectacle screenshot works inside KineticWE.
6. Kinetic title bars recolor live when Noctalia's theme changes.
7. Wobbly windows and other built-in effects are available.
8. `xdg-desktop-portal-kwe` is the portal backend; no `xdg-desktop-portal-kde`
   process runs.
9. `rpmlint` clean; `rpm -qpR` shows no `*-devel` runtime deps.

## B8. Decisions & remaining risks

### Resolved (locked)

- **QML engine URI** → rename to `org.kineticwe.decoration` (full ownership).
- **Aurorae themes / Plastik** → dropped. The Aurorae engine, its vendored
  Layan themes and the Plastik QML decoration are gone; the Kinetic plugin is
  the only window decoration.
- **`kconf_update` / `knsrcfiles` / `config.kcfg`** → drop (KCM-only / retired).
- **Effect plugin IDs** (`kwin4_effect_*`) → leave as-is; they contain "kwin",
  not "kde", and are config strings.
- **KGlobalAccel component** → rename `kwin` → `kineticwe` (Option 2) + migration.
- **Keyboard-layout component** → rename to `KineticWE Keyboard Layout Switcher`;
  keep `kaccess`.
- **App identity (`KAboutData`)** → deferred (Option 3) until after Phase 5 and
  an audit.
- **`KWIN_*` env vars** → keep; **`kwinoutputconfig.json`** → rename;
  `org.kde.kwin.Scripting` D-Bus → keep.
- **Nix `kdecoration` inconsistency** → handle later, not in this pass.

### Testing ownership

All building and testing is done by the user locally. Agents write and
statically verify code only; they never build, install, or run sessions or
autotests (`AGENTS.md` Section 1). Each phase ends with the user-run commands
in B9.

### Remaining risks

1. **`libkwin` rename breadth** — many in-tree plugins link it; verify no
   out-of-tree consumer in the distro links `libkwin.so` (none expected).
2. **Decorations search-root move** — requires an in-tree loader change; confirm
   the `KPackage` root override works with the renamed install path.
3. **KGlobalAccel migration correctness** — the `[kwin]` → `[kineticwe]` group
   move must run before the daemon reads the file, or users lose overrides.

## B9. Build / test commands (run locally by the user)

Per `AGENTS.md`, agents do not build. After each phase run:

- KWin-only (compositor fast path): `./build-rpm-dev.sh --kwin-only`
- Noctalia-only (shell/settings): `./build-rpm-dev.sh --noct-only`
- Full: `./build-rpm-dev.sh --binary` then `./build-rpm-dev.sh --full`
- RPM + coexistence test: `./build-rpm-dev.sh` and install the resulting RPMs
  on a clean Fedora VM with KDE present.

Static checks that may be run by agents: `bash -n`, `shellcheck`, `qmllint`,
`qmlformat --check`, `cmake -S . -B <fresh-tmp> -G Ninja` (configure only),
`rpmspec -P`, `cppcheck`.
