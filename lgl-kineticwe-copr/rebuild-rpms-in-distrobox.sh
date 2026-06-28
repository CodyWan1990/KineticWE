#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

results_dir="$repo_root/lgl-kineticwe-copr/results"
arch="$(rpm --eval '%{_arch}')"
rpm_out_dir="${HOME}/rpmbuild/RPMS/${arch}"

make -f lgl-kineticwe-copr/Makefile srpm

srpm="$(find "$results_dir" -maxdepth 1 -type f -name 'kineticwe-*.src.rpm' -printf '%T@ %p\n' | sort -nr | awk 'NR == 1 {print $2}')"
if [[ -z "${srpm:-}" ]]; then
    echo "No kineticwe source RPM was generated in $results_dir" >&2
    exit 1
fi

mkdir -p "$results_dir"
find "$results_dir" -maxdepth 1 -type f -name 'kineticwe*.rpm' ! -name '*.src.rpm' -delete
mkdir -p "$rpm_out_dir"
find "$rpm_out_dir" -maxdepth 1 -type f -name 'kineticwe*.rpm' -delete

rpmbuild --rebuild "$srpm"

find "$rpm_out_dir" -maxdepth 1 -type f -name 'kineticwe*.rpm' -exec cp -f {} "$results_dir/" \;

echo
echo "RPMs written to:"
find "$results_dir" -maxdepth 1 -type f -name 'kineticwe*.rpm' | sort
