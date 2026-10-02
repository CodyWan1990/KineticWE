#!/bin/bash
set -euo pipefail

# =============================================================================
# arch/sync-from-spec.sh <PKGBUILD>
# =============================================================================
# Copy the version, release and pinned commits from the Fedora spec
# (lgl-kineticwe-copr/kineticwe.spec, the source of truth) into a PKGBUILD:
#   Version                -> pkgver
#   Release                -> pkgrel
#   %global *_ref          -> _kglobalacceld_commit, _kdecoration_commit,
#                             _kconfig_commit, _greeter_commit
# Only those column-0 assignments are rewritten; running it twice is a no-op.
# Prints each value it changes. Exits non-zero (and writes nothing) if the spec
# can't be read or a value is missing or malformed.
#
# Used by install-arch.sh (on its build copy) and CI (on the builder's copy).
# Run it on the repo's PKGBUILD to update the committed file:
#   arch/sync-from-spec.sh PKGBUILD
# Changes beyond version/pins (dependencies, files, patches) are not synced.
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPEC_FILE="${SPEC_FILE:-$SCRIPT_DIR/../lgl-kineticwe-copr/kineticwe.spec}"
pkgbuild="${1:-}"

die() { echo "sync-from-spec: $*" >&2; exit 1; }

[[ -n "$pkgbuild" ]] || die "usage: $0 <PKGBUILD>"
[[ -f "$pkgbuild" && ! -L "$pkgbuild" ]] || die "not a regular file: $pkgbuild"
[[ -r "$SPEC_FILE" ]] || die "cannot read spec: $SPEC_FILE"

spec_ref() {
    sed -n "s/.*%global $1 \\([0-9a-f]\\{40\\}\\).*/\\1/p" "$SPEC_FILE" | head -1
}

version=$(awk '/^Version:/ {print $2; exit}' "$SPEC_FILE")
release=$(awk '/^Release:/ {print $2; exit}' "$SPEC_FILE" | sed 's/%{?dist}//')
[[ "$version" =~ ^[0-9A-Za-z._+]+$ ]] || die "invalid Version in spec: '${version}'"
[[ "$release" =~ ^[0-9]+$ ]] || die "invalid Release in spec: '${release}' (makepkg needs an integer pkgrel)"

declare -A want=(
    [pkgver]="$version"
    [pkgrel]="$release"
    [_kglobalacceld_commit]="$(spec_ref kglobalacceld_ref)"
    [_kdecoration_commit]="$(spec_ref kdecoration_ref)"
    [_kconfig_commit]="$(spec_ref kconfig_compiler_ref)"
    [_greeter_commit]="$(spec_ref noctalia_greeter_ref)"
)
for key in _kglobalacceld_commit _kdecoration_commit _kconfig_commit _greeter_commit; do
    [[ "${want[$key]}" =~ ^[0-9a-f]{40}$ ]] || die "missing or invalid pin for $key in spec"
done

tmp=$(mktemp "${pkgbuild}.sync.XXXXXX")
trap 'rm -f -- "$tmp"' EXIT
cp -p -- "$pkgbuild" "$tmp"

changed=0
for key in pkgver pkgrel _kglobalacceld_commit _kdecoration_commit _kconfig_commit _greeter_commit; do
    grep -q "^${key}=" "$tmp" || die "PKGBUILD has no '${key}=' line"
    old=$(sed -n "s/^${key}=//p" "$tmp" | head -1)
    if [[ "$old" != "${want[$key]}" ]]; then
        sed -i "s/^${key}=.*/${key}=${want[$key]}/" "$tmp"
        echo "  ${key}: ${old} -> ${want[$key]}"
        changed=1
    fi
done

if (( changed )); then
    mv -f -- "$tmp" "$pkgbuild"
    trap - EXIT
fi
