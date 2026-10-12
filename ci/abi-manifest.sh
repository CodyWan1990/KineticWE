#!/usr/bin/env bash
# Write the ABI manifest of a set of built packages to stdout. Run in the build
# container right after makepkg, while the exact build dependencies are still
# installed. ci/abi-watch.sh compares the published manifest with current Arch
# to decide whether the repo needs a rebuild.
#
# Format (one record per line, space separated, '#' lines are comments):
#   ref <git ref built>                      tag or branch the rebuild targets
#   commit <sha>
#   version <pkg> <upstream version>         Tier 1: private-API dependencies,
#                                            any version change needs a rebuild
#   soname <pkg> <full version> <path>       Tier 2: every external library the
#                                            packages link; a rebuild is needed
#                                            only if <path> leaves <pkg>
#
# Tier 1 is qt6-base (QtCore/QtGui/QtPrintSupport private API and QPA in the
# compositor and portal) plus the KF6 frameworks whose private headers the
# compositor includes. Versions drop pkgrel, matching the PKGBUILD's Qt pin.
set -euo pipefail

(($# > 0)) || { echo "usage: abi-manifest.sh <pkg.tar.zst>..." >&2; exit 1; }

tier1=(qt6-base kwindowsystem kidletime)
# Toolchain libraries keep their sonames; never worth a rebuild.
skip_owners=(glibc gcc-libs libgcc libstdc++)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir "$work/root"
for pkg in "$@"; do
    bsdtar -xf "$pkg" -C "$work/root" --exclude '.PKGINFO' --exclude '.BUILDINFO' \
        --exclude '.MTREE' --exclude '.INSTALL'
done

# Libraries shipped by our own packages are not external.
declare -A own=()
while IFS= read -r f; do
    own[$(basename -- "$f")]=1
done < <(find "$work/root" -name '*.so*' \( -type f -o -type l \))

# DT_NEEDED of every ELF file in the packages.
declare -A needed=()
while IFS= read -r f; do
    [[ "$(head -c4 -- "$f" | tr -d '\0')" == $'\x7fELF' ]] || continue
    while IFS= read -r so; do
        needed[$so]=1
    done < <(readelf -d -- "$f" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
done < <(find "$work/root" -type f)

upstream_version() {
    local v
    v=$(pacman -Q "$1" | awk '{print $2}')
    printf '%s\n' "${v%-*}"
}

echo "# kineticwe ABI manifest v1, generated $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "ref ${CI_COMMIT_TAG:-${CI_COMMIT_REF_NAME:?CI_COMMIT_REF_NAME not set}}"
echo "commit ${CI_COMMIT_SHA:?CI_COMMIT_SHA not set}"
for pkg in "${tier1[@]}"; do
    echo "version $pkg $(upstream_version "$pkg")"
done

fail=0
for so in $(printf '%s\n' "${!needed[@]}" | sort); do
    [[ -z "${own[$so]:-}" ]] || continue
    path=/usr/lib/$so
    if [[ ! -e "$path" ]]; then
        path=$(ldconfig -p | awk -v s="$so" '$1 == s { print $NF; exit }')
    fi
    if [[ -z "$path" || ! -e "$path" ]]; then
        echo "abi-manifest: cannot resolve $so" >&2
        fail=1
        continue
    fi
    # Canonical directory (ldconfig may report /lib64 etc.), keeping the
    # soname symlink itself: Arch file lists contain the symlink path.
    path="$(readlink -f -- "$(dirname -- "$path")")/$so"
    if ! owner=$(pacman -Qoq -- "$path" 2>/dev/null); then
        echo "abi-manifest: no package owns $path ($so)" >&2
        fail=1
        continue
    fi
    if printf '%s\n' "${skip_owners[@]}" | grep -qxF -- "$owner"; then
        continue
    fi
    echo "soname $owner $(pacman -Q "$owner" | awk '{print $2}') ${path#/}"
done
exit "$fail"
