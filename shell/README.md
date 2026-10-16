# Shell workspace (KineticWE 2.0)

This directory holds the desktop-shell side of KineticWE 2.0. It is a monorepo
housing the **Noctalia fork** (vendored) plus our own **Kinetic Settings** module.

## Layout

| Path | What it is |
|---|---|
| `noctalia/` | The Noctalia v5 shell fork, vendored as a **git subtree** from `https://github.com/noctalia-dev/noctalia` (branch `main`). Read-only to us by convention — see "Upstream sync". |
| `kinetic-settings/` | Our iteration-1 settings module (Kinetic Settings). Lives **outside** the subtree prefix so upstream pulls can never clobber it. |

## Upstream sync (Noctalia)

The subtree is tracked with a squashed history from upstream. To pull the newest
Noctalia `main`:

```sh
git subtree pull --prefix=shell/noctalia noctalia-upstream main --squash
```

Notes:

- Prefer additive changes (new files, extra modules, `kinetic-settings/`). We do
  carry in-place patches to upstream files; these are the usual sources of
  conflicts on a pull:
  - rebranding: install paths, app/desktop ID, XDG dirs (`FileUtils::kAppDirName`)
  - Origami default palette (`src/theme/builtin_palettes.cpp`, `config_types.h`)
  - keybind recorder / key chords (custom keybinds, function keys)
  - window switcher + dock on KWin (`KwinThumbnailCapture`, plasma window geometry,
    switcher close fade)
  - terminal launch, KDE color scheme export, settings window wiring
- To see the full patch set: `git diff <last upstream squash commit> HEAD:shell/noctalia`
  (the squash commits are the ones titled `Squashed 'shell/noctalia/' changes ...`).
- If you later want to publish Noctalia as its own fork repo (`kineticwe/noctalia`),
  split it back out with `git subtree split --prefix=shell/noctalia -b noctalia-fork`
  — the 2.0 plan anticipated a standalone fork repo, and subtree keeps that option open.

## Why a monorepo?

The 2.0 plan (`docs/kineticwe-2.0.md`) originally said the Noctalia fork "must be
its own repo." For iteration 1 we vendor it here for simplicity and to reduce what
has to be maintained; `git subtree` keeps it upstream-pullable and reversible.
