# xdg-desktop-portal-kwe

A KineticWE backend implementation for [xdg-desktop-portal](http://github.com/flatpak/xdg-desktop-portal),
forked from KDE's `xdg-desktop-portal-kde`. The only upstream changes are the
rename (binary, D-Bus name `org.freedesktop.impl.portal.desktop.kwe`, `.portal`
file with `UseIn=KineticWE`, and the `kineticwe-portals.conf` selection config);
all portal interfaces and behavior are stock KDE 6.7.4.

## Building xdg-desktop-portal-kwe

### Dependencies:
 - xdg-desktop-portal (runtime dependency)
 - Qt 6 (build dependency)
 - KDE Frameworks - KCoreAddons, KI18n, KNotifications(build dependency)

### Build instructions:
```
$ mkdir build && cd build
$ cmake .. [your_options]
$ make -j5
$ make install
```

## Upstream

Vendored from `https://github.com/KDE/xdg-desktop-portal-kde` tag `v6.7.4`
(remote `portal-upstream`). Sync with
`git subtree pull --prefix=portal/xdg-desktop-portal-kwe portal-upstream <tag> --squash`.

## Testing xdg-desktop-portal-kwe

The repository [xdg-portal-test-kde](https://invent.kde.org/libraries/xdg-portal-test-kde) contains a simple test application.