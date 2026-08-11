# core

Shared plumbing for the Kinetic Settings module (iteration-1 scaffold, no code yet):

- Read/write of `~/.config/kineticwe/kineticwe.kwe` and `rules.kwe` (KConfig INI; the
  compositor schema lives in this repo's `src/kcms/*/*.kcfg`).
- Compositor **D-Bus client**: `org.kde.KWin`, `VirtualDesktopManager`, `InputDevice`.
- Change-signal emission for runtime-only settings (e.g. cursor →
  `org.kde.KGlobalSettings` `notifyChange` type 5).
