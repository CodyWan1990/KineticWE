# Vendored Aurorae decoration themes (Layan)

Upstream: <https://github.com/vinceliuice/Layan-kde>
Pinned release: tag `2025-02-13` — commit `ace0b1d93e5f08c650630c146b2d637e1e2e6cd1`
License: GPL-3.0 (upstream `LICENSE`; upstream `AUTHORS` for attribution). The
themes keep their own license — they are data loaded by the Aurorae
KDecoration plugin, not linked code, and are copied verbatim (no local edits).

`Layan` and `Layan-solid` are upstream `aurorae/themes/` dirs (upstream also
ships `Layan-light`, which KineticWE does not vendor). The compositor's CMake
(`data/CMakeLists.txt`) installs them into `$datadir/aurorae/themes/<Name>/`,
where the Aurorae plugin (shipped in the `kineticwe` package) and Kinetic
Settings → Title Bars both find them. Kinetic Settings shows `Layan` as
**Neutral** (the transparent dark variant) and `Layan-solid` as **Solid**; any
other discovered theme keeps its own `Name=`.

Only the theme directories are installed — this README, `LICENSE` and `AUTHORS`
stay in the source tree (RPM picks `LICENSE` up via `%license`).

## Updating to a newer upstream release

```sh
git clone --depth 1 --branch <tag> https://github.com/vinceliuice/Layan-kde.git /tmp/layan-kde
rm -rf data/aurorae-themes/Layan data/aurorae-themes/Layan-solid
cp -r /tmp/layan-kde/aurorae/themes/{Layan,Layan-solid} data/aurorae-themes/
cp /tmp/layan-kde/{LICENSE,AUTHORS} data/aurorae-themes/
git -C /tmp/layan-kde log -1 --format='%H %ad'   # bump the pin above
```

Packaging: the files are installed by the compositor's CMake, so the RPM
`%files` (`kineticwe.spec` and `lgl-kineticwe-copr/kineticwe.spec`) list
`%{_datadir}/aurorae/themes/Layan/` and `…/Layan-solid/`. PKGBUILD/debian pick
the files up from the same `cmake --install`.
