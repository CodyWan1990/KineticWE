# Kinetic Settings (iteration 1)

The compositor/desktop settings module for KineticWE 2.0. In iteration 1 it is
**not** a separate desktop app: it opens a full window from the shell (the same way
Noctalia's own settings window does) — no standalone binary, no `.desktop` entry.

## Placement

This module lives at `shell/kinetic-settings/`, **outside** the vendored
`shell/noctalia/` subtree prefix. That keeps `git subtree pull` for Noctalia from
ever touching our code, matching the plan's "additive-only, clean upstream sync"
rule. It is still built/loaded by the Noctalia fork; it just never edits upstream
files.

## Scope

Maps to the "Phase 4 — Kinetic Settings module" table in `docs/kineticwe-2.0.md`:

- Display (resolution/refresh/rotation/arrangement/scale/brightness)
- Shortcuts / keybinds (compositor + global app)
- Cursor (theme/size)
- Icons (+ `kdeglobals` mirror for KDE apps)
- Input (mouse / touchpad / keyboard layouts)
- Fonts
- Effects / animations
- Tiling / window rules
- Workspaces / virtual desktops
- Tabbox / screen edges / OSD
- XWayland

Those settings intentionally do **not** live in Noctalia's own settings app
(`shell/noctalia/src/shell/settings`), which keeps shell look-and-feel concerns
separate and shrinks our fork diff.

## Directory layout

```
kinetic-settings/
└── src/
    ├── pages/    # one directory per settings area (the Phase 4 modules)
    ├── core/     # ~/.config/kineticwe/*.kwe read/write, compositor D-Bus client,
    │             #   change-signal emission (e.g. org.kde.KGlobalSettings notifyChange(5))
    └── window/   # the full-window UI surface, opened like the shell settings window
```

## Iteration 2 (deferred)

When Kinetic Settings should become a launcher app (own binary + `.desktop` entry,
native performance, independent packaging), extract `src/` into the standalone
`kineticwe/kinetic-settings` repo. Keeping the module self-contained from day one
makes that a nearly verbatim move.
