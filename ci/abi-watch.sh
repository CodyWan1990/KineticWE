#!/usr/bin/env bash
# Decide whether the published repo needs an ABI rebuild, and trigger one.
# Cheap enough to run daily: needs only bash, curl, jq and bsdtar (alpine).
#
#   abi-watch.sh <manifest-url or local file> [--dry-run]
#
# Compares the manifest written by ci/abi-manifest.sh at the last build with the
# current Arch core/extra databases:
#   * Tier 1 ("version" lines): any upstream version change -> rebuild
#     (Qt private API / KF6 private headers have no ABI guarantee).
#   * Tier 2 ("soname" lines): only for packages whose version changed, fetch
#     the package's file list and rebuild if the linked soname is gone.
# A missing manifest (first run, or a repo published before this script)
# also triggers a rebuild, which publishes one.
#
# Triggering needs KWE_TRIGGER_TOKEN (pipeline trigger token) plus GitLab's
# predefined CI_API_V4_URL and CI_PROJECT_ID. The rebuild runs on the ref
# recorded in the manifest (the release tag), with REBUILD=1.
# --dry-run prints the decision and never triggers.
# KWE_WATCH_FORCE=1 always requests a rebuild (tests the trigger path).
set -euo pipefail

manifest_url="${1:?usage: abi-watch.sh <manifest-url> [--dry-run]}"
dry_run=false
[[ "${2:-}" != --dry-run ]] || dry_run=true
mirror="${ARCH_MIRROR:-https://geo.mirror.pkgbuild.com}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

reasons=()
ref=""

trigger() {
    local reason
    reason=$(printf '%s; ' "${reasons[@]}")
    reason=${reason%; }
    echo ">> Rebuild needed: $reason"
    if $dry_run; then
        echo ">> --dry-run: not triggering (ref ${ref:-<schedule ref>})"
        return 0
    fi
    : "${KWE_TRIGGER_TOKEN:?KWE_TRIGGER_TOKEN not set; cannot trigger the rebuild}"
    : "${CI_API_V4_URL:?}" "${CI_PROJECT_ID:?}"
    ref=${ref:-${CI_COMMIT_REF_NAME:?no ref to rebuild}}
    echo ">> Triggering REBUILD=1 pipeline on $ref"
    curl -sSf -X POST \
        --form-string "token=$KWE_TRIGGER_TOKEN" \
        --form-string "ref=$ref" \
        --form-string "variables[REBUILD]=1" \
        --form-string "variables[KWE_REBUILD_REASON]=${reason:0:1000}" \
        "$CI_API_V4_URL/projects/$CI_PROJECT_ID/trigger/pipeline" >/dev/null
    echo ">> Rebuild pipeline created"
}

echo ">> Fetching manifest $manifest_url"
if [[ -f "$manifest_url" ]]; then
    # Local file (testing): no HTTP status to inspect.
    cp -- "$manifest_url" "$work/manifest"
    code=200
else
    code=$(curl -sSL -o "$work/manifest" -w '%{http_code}' "$manifest_url") || code=000
fi
case "$code" in
    200) ;;
    404)
        reasons+=("no ABI manifest published yet")
        trigger
        exit 0
        ;;
    *)
        echo "abi-watch: fetching the manifest failed (HTTP $code)" >&2
        exit 1
        ;;
esac
ref=$(awk '$1 == "ref" { print $2; exit }' "$work/manifest")

# name -> version and name -> repo from the current sync databases.
declare -A cur_ver=() cur_repo=()
for repo in core extra; do
    echo ">> Fetching $repo.db"
    curl -sSfL -o "$work/$repo.db" "$mirror/$repo/os/x86_64/$repo.db"
    while read -r name ver; do
        cur_ver[$name]=$ver
        cur_repo[$name]=$repo
    done < <(bsdtar -xOf "$work/$repo.db" '*/desc' |
        awk '$0 == "%NAME%" { getline; n = $0 } $0 == "%VERSION%" { getline; print n, $0 }')
done

# Tier 1: upstream version (pkgrel dropped) must match exactly.
while read -r _ pkg built; do
    now=${cur_ver[$pkg]:-}
    if [[ -z "$now" ]]; then
        reasons+=("$pkg no longer in Arch")
    elif [[ "${now%-*}" != "$built" ]]; then
        reasons+=("$pkg $built -> ${now%-*}")
    fi
done < <(awk '$1 == "version"' "$work/manifest")

# Tier 2: the soname file must still be in its (changed) package.
declare -A files_checked=()
while read -r _ pkg built path; do
    now=${cur_ver[$pkg]:-}
    if [[ -z "$now" ]]; then
        reasons+=("$pkg (provides ${path##*/}) no longer in Arch")
        continue
    fi
    [[ "$now" != "$built" ]] || continue
    list="$work/files-$pkg.txt"
    if [[ -z "${files_checked[$pkg]:-}" ]]; then
        echo ">> $pkg changed ($built -> $now); checking its file list"
        curl -sSfL "https://archlinux.org/packages/${cur_repo[$pkg]}/x86_64/$pkg/files/json/" |
            jq -r '.files[]' > "$list"
        files_checked[$pkg]=1
    fi
    if ! grep -qxF -- "$path" "$list"; then
        reasons+=("${path##*/} gone from $pkg $now")
    fi
done < <(awk '$1 == "soname"' "$work/manifest")

[[ "${KWE_WATCH_FORCE:-}" != 1 ]] || reasons+=("forced by KWE_WATCH_FORCE=1")

if ((${#reasons[@]} == 0)); then
    echo ">> Published packages match current Arch; no rebuild needed"
    exit 0
fi
trigger
