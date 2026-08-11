# Settings pages (Phase 4 desktop modules)

One directory per compositor/desktop settings area. Placeholders for now (tracked
with `.gitkeep`); each will gain its own page implementation in Phase 4.

| Directory | Area | Primary config / mechanism |
|---|---|---|
| `display/` | resolution / refresh / rotation / arrangement / scale / brightness | `kde-output-device-v2` (compositor output management) |
| `shortcuts/` | compositor + global app shortcuts | `kineticwe.kwe [Shortcuts]` + `kglobalaccelrc` |
| `cursor/` | cursor theme / size | `kineticwe.kwe [Mouse]` + `org.kde.KGlobalSettings` `notifyChange(5)` |
| `icons/` | icon theme | `kineticwe.kwe` + `kdeglobals` mirror for KDE apps |
| `input/` | mouse / touchpad / keyboard layouts | `kineticwe.kwe [Libinput]`, `[Keyboard]`, `InputDevice` D-Bus |
| `fonts/` | font settings | `~/.config/fontconfig/fonts.conf` |
| `effects/` | effects / animations | `kineticwe.kwe [Plugins]`, `[Effect-*]`, `Plugins` D-Bus |
| `tiling/` | tiling layout + window rules | `kineticwe.kwe` tiling groups + `rules.kwe` |
| `workspaces/` | workspaces / virtual desktops | `kineticwe.kwe [Desktop]` + `VirtualDesktopManager` D-Bus |
| `tabbox_screenedges/` | tabbox / screen edges / OSD | `kineticwe.kwe [TabBox]`, `[ScreenEdges]` |
| `xwayland/` | XWayland options | `kineticwe.kwe` xwayland group (restart-only, documented) |

Lock screen, sounds, night light, and autostart remain **shell-owned** (Noctalia's
own config) and are intentionally not listed here.
