# Vendored icon theme (Breeze-Round-Chameleon Dark Icons)

Upstream: <https://github.com/L4ki/Breeze-Chameleon-Icons>
License: GPL-3.0 (upstream `LICENSE`; upstream `AUTHORS` for attribution).

`Breeze-Round-Chameleon Dark Icons` is the upstream `Breeze-Round-Chameleon
Dark Icons/` directory from that repo. It is shipped as a compressed archive
rather than ~9k loose files (the extracted tree is ~120 MB), so the source
checkout stays small:

```
Breeze-Round-Chameleon-Dark-Icons.tar.xz   # upstream theme dir, verbatim
sha256 ebeeb1ee6c870da04a9784e0d9521f0b31c9c334dc79f6feae93adb0578c3149
```

The theme inherits `breeze,hicolor`, so the `kineticwe` package keeps its
`Requires: breeze-icons`. The compositor's CMake (`data/CMakeLists.txt`)
extracts the archive into the build tree and installs it to
`$datadir/icons/Breeze-Round-Chameleon Dark Icons/`, where it is picked up by
Qt/KDE/GTK apps. The session (`scripts/start-kineticwe.sh`) seeds it as the
default icon theme for `kdeglobals`, `appearance.kwe` and the qt5ct/qt6ct
configs.

Only the extracted theme directory is installed — this README, `LICENSE` and
`AUTHORS` stay in the source tree (RPM picks `LICENSE` up via `%license`).

## Updating to a newer upstream release

```sh
git clone --depth 1 https://github.com/L4ki/Breeze-Chameleon-Icons.git /tmp/breeze-chameleon
( cd /tmp/breeze-chameleon && git log -1 --format='%H %ad' )   # record the commit
rm -f data/icon-themes/Breeze-Round-Chameleon-Dark-Icons.tar.xz
tar cJf data/icon-themes/Breeze-Round-Chameleon-Dark-Icons.tar.xz \
    -C /tmp/breeze-chameleon "Breeze-Round-Chameleon Dark Icons"
cp /tmp/breeze-chameleon/{LICENSE,AUTHORS} data/icon-themes/
sha256sum data/icon-themes/Breeze-Round-Chameleon-Dark-Icons.tar.xz  # bump the hash above
```

Packaging: the files are installed by the compositor's CMake, so the RPM
`%files` (`kineticwe.spec` and `lgl-kineticwe-copr/kineticwe.spec`) list
`%{_datadir}/icons/Breeze-Round-Chameleon*`, and `%license` references
`data/icon-themes/LICENSE`. PKGBUILD/debian pick the files up from the same
`cmake --install`.
