#!/usr/bin/env bash
# Rebuild the pacman repo database in <repo-dir>, keeping the newest
# $KEEP_BUILDS builds of EACH package and removing older ones.
#
# The PKGBUILD is a split package, so the repo holds several package names
# (kineticwe, xdg-desktop-portal-kwe, kineticwe-noctalia, kineticwe-greeter).
# Package names are read from each package's .PKGINFO rather than parsed out
# of the filename: "kineticwe-*" would also match "kineticwe-greeter-*", and
# the epoch (e.g. "2:0.0.1-14") makes filename parsing fragile.
set -euo pipefail

repo_dir="${1:?usage: repo.sh <repo-dir>}"
keep="${KEEP_BUILDS:-4}"
main_pkg=kineticwe

cd "$repo_dir"
shopt -s nullglob

all=(*.pkg.tar.zst)
if ((${#all[@]} == 0)); then
    echo "repo.sh: no packages found in $repo_dir" >&2
    exit 1
fi

# Read pkgname + builddate of every package (no subshell, so a bad package
# aborts the script), then group by pkgname, newest build first.
rows=()
for f in "${all[@]}"; do
    info="$(bsdtar -xOf "$f" .PKGINFO)"
    name="$(sed -n 's/^pkgname = //p' <<<"$info")"
    bdate="$(sed -n 's/^builddate = //p' <<<"$info")"
    if [[ -z "$name" ]]; then
        echo "repo.sh: cannot read pkgname from $f" >&2
        exit 1
    fi
    rows+=("$name"$'\t'"${bdate:-0}"$'\t'"$f")
done

declare -A by_name=()
while IFS=$'\t' read -r name _bdate file; do
    by_name[$name]+="$file"$'\n'
done < <(printf '%s\n' "${rows[@]}" | sort -t $'\t' -k1,1 -k2,2nr)

if [[ -z "${by_name[$main_pkg]:-}" ]]; then
    echo "repo.sh: no $main_pkg package found in $repo_dir" >&2
    exit 1
fi

# repo-add keeps whichever entry of a pkgname it adds LAST, so add each
# package's kept builds oldest -> newest.
kept=()
for name in "${!by_name[@]}"; do
    mapfile -t files < <(printf '%s' "${by_name[$name]}")
    if ((${#files[@]} > keep)); then
        echo "Pruning $(( ${#files[@]} - keep )) old $name build(s):"
        printf '  %s\n' "${files[@]:keep}"
        rm -f -- "${files[@]:keep}"
        files=("${files[@]:0:keep}")
    fi
    for ((i = ${#files[@]} - 1; i >= 0; i--)); do
        kept+=("${files[i]}")
    done
done

rm -f kineticwe.db kineticwe.db.tar.gz kineticwe.files kineticwe.files.tar.gz
repo-add kineticwe.db.tar.gz "${kept[@]}"

echo 'Repo database rebuilt with:'
tar -tf kineticwe.db.tar.gz | grep '/desc$'
