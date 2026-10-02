#!/bin/bash
set -euo pipefail

if (( EUID == 0 )); then
    echo "Run this script as your normal user; sudo is requested only for system changes." >&2
    exit 1
fi

# =============================================================================
# KineticWE - Arch Linux Native Build Script
# =============================================================================
# Builds KineticWE directly on the host using pacman/makepkg (no distrobox).
#
# Package mode (default) builds real pacman packages from the current working
# tree with the repo's PKGBUILD and installs them with pacman -U. Dev modes
# build incrementally with cmake/meson into TEST_PREFIX (~/.local by default)
# and record manifests so --reset-local can remove exactly what was installed.
#
# Usage:
#   ./install-arch.sh                 # Default (= --pkg)
#   ./install-arch.sh --pkg           # makepkg from the working tree, then pacman -U
#   ./install-arch.sh --binary        # Dev: compositor + portal + shell into TEST_PREFIX
#   ./install-arch.sh --local-dev     # Dev: --binary + Noctalia greeter setup (--full is a deprecated alias)
#   ./install-arch.sh --kwin-only     # Dev: incremental compositor-only build
#   ./install-arch.sh --noct-only     # Dev: incremental Noctalia shell-only build
#   ./install-arch.sh --reset-local   # Remove unchanged dev files from TEST_PREFIX + session entry
#   ./install-arch.sh --clean-deps    # Report dependencies (never removes packages)
#   ./install-arch.sh --help          # Show usage
#
# Modifiers:
#   --no-session         Dev modes: skip installing the KineticWE-Dev login-screen entry
#   --no-greeter         --pkg only: do not install the kineticwe-greeter package
#   --yes, -y            Skip the confirmation prompt (required when not run
#                        from a terminal)
#   --install-host-deps  Deprecated alias for --binary
#
# Every mode except --clean-deps and --help first shows what it will do and
# asks to continue ([Y/n] for builds; [y/N] for --local-dev and --reset-local).
#
# Only one mode may be given; --local-dev together with --binary means --local-dev.
#
# Dependencies are read from PKGBUILD: top-level makedepends are the build
# deps; the depends of kineticwe, xdg-desktop-portal-kwe and kineticwe-noctalia
# (plus kineticwe-greeter for --local-dev), together with kineticwe's optdepends,
# are the dev-session runtime deps. Builds always use the version, release and
# pinned commits from lgl-kineticwe-copr/kineticwe.spec (the source of truth);
# if the committed PKGBUILD lags behind, the build warns and continues
# (arch/sync-from-spec.sh PKGBUILD updates it).
#
# Environment variables:
#   TEST_PREFIX           - Dev-mode install prefix (default: $HOME/.local)
#   NOCTALIA_GREETER_REPO - Git URL of the noctalia-greeter fork (default: KineticWE fork)
#   NOCTALIA_GREETER_REF  - Greeter revision for --local-dev (default: the spec's
#                           noctalia_greeter_ref; a non-commit value is cloned
#                           as a branch/tag)
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPEC_DIR="${SCRIPT_DIR}/lgl-kineticwe-copr"
PKGBUILD_FILE="${SCRIPT_DIR}/PKGBUILD"
PKGBUILD_INSTALL_FILE="${SCRIPT_DIR}/kineticwe.install"
PKGBUILD_GREETER_INSTALL_FILE="${SCRIPT_DIR}/kineticwe-greeter.install"
PKG_WORK_DIR="${SCRIPT_DIR}/build.arch-pkg"
NOCTALIA_GREETER_REPO="${NOCTALIA_GREETER_REPO:-https://gitlab.com/theblackdon/noctalia-greeter-kwe}"

SESSION_DIR="/usr/local/share/wayland-sessions"
SESSION_DESKTOP="kineticwe-dev.desktop"

# Split packages produced by PKGBUILD (never treated as dependencies).
KWE_PACKAGES=(kineticwe xdg-desktop-portal-kwe kineticwe-noctalia kineticwe-greeter)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

# ---------------------------------------------------------------------------
# Parse spec metadata (source of truth for version and pinned revisions)
# ---------------------------------------------------------------------------
SPEC_FILE="${SPEC_DIR}/kineticwe.spec"
SPEC_VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC_FILE")
SPEC_RELEASE=$(awk '/^Release:/ {print $2; exit}' "$SPEC_FILE" | sed 's/%{?dist}//')

spec_ref() {
    sed -n "s/.*%global $1 \\([0-9a-f]\\{40\\}\\).*/\\1/p" "$SPEC_FILE" | head -1
}
SPEC_KGA_REF=$(spec_ref kglobalacceld_ref)
SPEC_KDECO_REF=$(spec_ref kdecoration_ref)
SPEC_KCONFIG_REF=$(spec_ref kconfig_compiler_ref)
SPEC_GREETER_REF=$(spec_ref noctalia_greeter_ref)

NOCTALIA_GREETER_REF="${NOCTALIA_GREETER_REF:-$SPEC_GREETER_REF}"

# Extra dev-session runtime packages that PKGBUILD does not list: a live
# PipeWire daemon plus session manager are needed for screen capture, and
# pipewire-pulse for audio (dropped automatically if pulseaudio is installed).
MEDIA_RUNTIME_PKGS=(
    pipewire
    wireplumber
    pipewire-pulse
)

# Tools the dev modes themselves use (makepkg assumes base-devel; rsync is
# used for staged installs).
DEV_TOOL_PKGS=(base-devel git curl rsync)

# QML modules the built-in effects import, as paths relative to a Qt QML
# import root. ldd cannot see QML modules, so verify_qml_modules() checks
# these by path and reports the Arch package that provides each one.
EFFECT_QML_MODULES=(
    org/kde/milou
    org/kde/plasma/components
    org/kde/plasma/extras
    org/kde/plasma/core
    Qt5Compat/GraphicalEffects
    org/kde/kirigami
    org/kde/ksvg
    org/kde/kcmutils
    org/kde/kitemmodels
)

# QML modules the xdg-desktop-portal-kwe backend imports at runtime (screen
# chooser, file dialog, ...). These are the *module URIs*, which do not always
# match the Arch package name: the `kiconthemes` package ships org.kde.iconthemes
# and `kdeclarative` ships org.kde.kquickcontrols. Also invisible to ldd; a
# missing one only shows up when the dialog is opened, so verify by path too.
# org.kde.xdgdesktopportal is built and installed from this repo with the portal.
PORTAL_QML_MODULES=(
    org/kde/pipewire
    org/kde/kirigami
    org/kde/kitemmodels
    org/kde/ki18n
    org/kde/iconthemes
    org/kde/kcmutils
    org/kde/kquickcontrols
)

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
die() { error "$*"; exit 1; }

# Temporary staging/probe directories only; persistent caches are never cleaned.
TEMP_DIRS=()
ACTIVE_INSTALL_PENDING=""
cleanup_temps() {
    local dir
    if [[ -n "$ACTIVE_INSTALL_PENDING" && -f "$ACTIVE_INSTALL_PENDING" ]]; then
        warn "Installation incomplete; recovery journal retained: $ACTIVE_INSTALL_PENDING" >&2
    fi
    for dir in "${TEMP_DIRS[@]}"; do rm -rf -- "$dir"; done
}
trap cleanup_temps EXIT
new_temp() {
    TEMP_DIR=$(mktemp -d /tmp/kineticwe-arch.XXXXXXXX)
    TEMP_DIRS+=("$TEMP_DIR")
}

remove_temp() {
    local dir="$1" index
    rm -rf -- "$dir"
    for index in "${!TEMP_DIRS[@]}"; do
        if [[ "${TEMP_DIRS[index]}" == "$dir" ]]; then
            unset 'TEMP_DIRS[index]'
        fi
    done
}

check_bootstrap() {
    local tool
    for tool in pacman sudo sha256sum realpath flock; do
        command -v "$tool" >/dev/null || die "Missing bootstrap tool: $tool"
    done
}

check_prereqs() {
    local tool
    for tool in cmake meson ninja git curl rsync; do
        command -v "$tool" >/dev/null || die "Missing build tool after dependency installation: $tool"
    done
}

prepare_prefix() {
    TEST_PREFIX=$(realpath -m -- "$TEST_PREFIX")
    [[ "$TEST_PREFIX" != / && "$TEST_PREFIX" != *$'\n'* && "$TEST_PREFIX" != *$'\t'* ]] || die "Unsafe installation prefix"
    validate_template_prefix
    mkdir -p -- "$TEST_PREFIX" || die "Cannot create user installation prefix: $TEST_PREFIX"
    [[ -w "$TEST_PREFIX" ]] || die "Installation prefix is not writable: $TEST_PREFIX"
    STATE_DIR="$TEST_PREFIX/share/kineticwe/arch-native-state"
    safe_path "share/kineticwe/arch-native-state/.lock"
    mkdir -p -m 0700 -- "$STATE_DIR"
    [[ ! -L "$STATE_DIR/.lock" ]] || die "State lock must not be a symlink"
    exec 9>"$STATE_DIR/.lock"
    flock -n 9 || die "Another operation is using this installation prefix"
}

# Validate parents, but deliberately do not follow the final symlink: it is
# itself the installed object. Manifest paths are data, never shell input.
safe_path() {
    local rel="$1" parent
    [[ -n "$rel" && "$rel" != /* && "$rel" != *$'\n'* && "$rel" != *$'\t'* ]] || die "Invalid manifest path"
    case "/$rel/" in */../*|*/./*) die "Invalid manifest traversal: $rel" ;; esac
    parent="${rel%/*}"
    [[ "$parent" != "$rel" ]] || parent=.
    parent=$(realpath -m -- "$TEST_PREFIX/$parent")
    [[ "$parent" == "$TEST_PREFIX" || "$parent" == "$TEST_PREFIX/"* ]] || die "Path escapes prefix: $rel"
}

file_record() {
    local file="$1" rel="$2" value
    [[ "$rel" != *$'\t'* && "$rel" != *$'\n'* ]] || die "Unsupported filename in installation"
    if [[ -L "$file" ]]; then
        value=$(readlink -- "$file")
        [[ "$value" != *$'\t'* && "$value" != *$'\n'* ]] || die "Unsupported symlink target"
        printf 'L\t%s\t%s\n' "$value" "$rel"
    elif [[ -f "$file" ]]; then
        value=$(sha256sum < "$file")
        printf 'F\t%s\t%s\n' "${value%% *}" "$rel"
    else
        die "Unsupported installed object: $file"
    fi
}

# Journals contain both old and incoming fingerprints until publication, so
# reset can recover either side of an interrupted replacement.
promote_stage() {
    local component="$1" stage="$2" root="$2$TEST_PREFIX" file rel kind value parent resolved state_root
    local manifest="$STATE_DIR/$1.manifest" pending="$STATE_DIR/$1.pending"
    local started=$SECONDS last_progress=$SECONDS count=0 regular_count=0 hash_count=0
    local -A parents=()
    [[ ! -L "$manifest" && ! -L "$pending" ]] || die "Symlinked installation state"
    state_root=$(realpath -e -- "$STATE_DIR") || die "Cannot resolve ownership state"
    new_temp
    local work="$TEMP_DIR" records="$TEMP_DIR/records"
    : > "$records"
    : > "$work/regular"
    : > "$work/transfer"
    [[ -d "$root" && ! -L "$root" ]] || die "No staged installation under requested prefix"
    step "$component: inspecting staged files..."
    if ! find "$stage" -mindepth 1 ! -type d -printf '%p\0%y\0%l\0' > "$work/entries"; then
        die "$component: cannot enumerate staged installation"
    fi
    while IFS= read -r -d '' file && IFS= read -r -d '' kind && IFS= read -r -d '' value; do
        [[ "$file" == "$root/"* ]] || die "Staged output escapes prefix: $file"
        rel="${file#"$root/"}"
        [[ -n "$rel" && "$rel" != /* && "$rel" != *$'\n'* && "$rel" != *$'\t'* ]] || die "Unsupported filename in installation"
        case "/$rel/" in */../*|*/./*) die "Invalid manifest traversal: $rel" ;; esac
        [[ "$rel" != share/kineticwe/arch-native-state && "$rel" != share/kineticwe/arch-native-state/* ]] || die "Component overwrites ownership state"
        [[ ! -d "$TEST_PREFIX/$rel" || -L "$TEST_PREFIX/$rel" ]] || die "Directory conflicts with installed file: $rel"
        [[ ! -e "$TEST_PREFIX/$rel" || -f "$TEST_PREFIX/$rel" || -L "$TEST_PREFIX/$rel" ]] || die "Unsupported destination object: $rel"
        parent="${rel%/*}"
        [[ "$parent" != "$rel" ]] || parent=.
        # Cache every ancestor as well, so non-directory parents are rejected
        # before rsync starts and each canonical lookup is performed only once.
        while [[ ! ${parents[$parent]+known} ]]; do
            resolved=$(realpath -m -- "$TEST_PREFIX/$parent") || die "Cannot resolve destination parent: $parent"
            [[ "$resolved" == "$TEST_PREFIX" || "$resolved" == "$TEST_PREFIX/"* ]] || die "Path escapes prefix: $rel"
            [[ "$resolved" != "$state_root" && "$resolved" != "$state_root/"* ]] || die "Component overwrites ownership state"
            [[ ! -e "$TEST_PREFIX/$parent" && ! -L "$TEST_PREFIX/$parent" || -d "$TEST_PREFIX/$parent" ]] || die "Non-directory destination parent: $parent"
            parents["$parent"]="$resolved"
            [[ "$parent" != . ]] || break
            if [[ "$parent" == */* ]]; then parent="${parent%/*}"; else parent=.; fi
        done
        case "$kind" in
            f)
                printf './%s\0' "$rel" >> "$work/regular"
                regular_count=$((regular_count + 1))
                ;;
            l)
                [[ "$value" != *$'\t'* && "$value" != *$'\n'* ]] || die "Unsupported symlink target"
                printf 'L\t%s\t%s\n' "$value" "$rel" >> "$records"
                ;;
            *) die "Unsupported staged object: $file" ;;
        esac
        printf '%s\0' "$rel" >> "$work/transfer"
        count=$((count + 1))
        if (( count % 500 == 0 || SECONDS - last_progress >= 2 )); then
            info "$component: inspected $count entries"
            last_progress=$SECONDS
        fi
    done < "$work/entries"
    step "$component: hashing $regular_count regular files ($count total entries)..."
    # The pipeline streams NUL-delimited GNU checksum output; pipefail makes
    # enumeration/reader errors and any failed xargs batch fatal before install.
    if ! (cd -- "$root" && xargs -0 -r sha256sum --zero -- < "$work/regular") | {
        while IFS= read -r -d '' value; do
            [[ "$value" =~ ^[0-9a-f]{64}\ \  ]] || exit 1
            rel="${value:66}"
            [[ "$rel" == ./* ]] || exit 1
            rel="${rel#./}"
            printf 'F\t%s\t%s\n' "${value:0:64}" "$rel" >> "$records" || exit 1
            hash_count=$((hash_count + 1))
            if (( hash_count % 500 == 0 || SECONDS - last_progress >= 2 )); then
                info "$component: hashed $hash_count/$regular_count files" >&2
                last_progress=$SECONDS
            fi
        done
        [[ "$hash_count" -eq "$regular_count" ]]
    }; then
        die "$component: hashing failed; no installed files changed"
    fi
    LC_ALL=C sort -z -u -o "$work/transfer" "$work/transfer"
    local merged
    merged=$(mktemp "$STATE_DIR/.merge.XXXXXXXX")
    cat "$records" > "$merged"
    [[ ! -f "$manifest" ]] || cat "$manifest" >> "$merged"
    [[ ! -f "$pending" ]] || cat "$pending" >> "$merged"
    sort -u -o "$merged" "$merged"
    mv -f -- "$merged" "$pending"
    ACTIVE_INSTALL_PENDING="$pending"
    step "$component: comparing/installing $count entries (unchanged content is skipped)..."
    # Revalidate cached parents immediately before transfer. Refuse changed
    # aliases even if they now point somewhere else within the prefix.
    for parent in "${!parents[@]}"; do
        resolved=$(realpath -m -- "$TEST_PREFIX/$parent") || die "Cannot resolve destination parent: $parent"
        [[ "$resolved" == "${parents[$parent]}" ]] || die "Destination parent changed during inspection: $parent"
        [[ ! -e "$TEST_PREFIX/$parent" && ! -L "$TEST_PREFIX/$parent" || -d "$TEST_PREFIX/$parent" ]] || die "Non-directory destination parent: $parent"
    done
    # No --inplace, --delete, or link dereferencing: rsync writes regular files
    # to destination-side temporaries and renames them only when complete.
    if ! rsync --archive --checksum --from0 --files-from="$work/transfer" \
        --no-implied-dirs --info=progress2 --stats -- "$root/" "$TEST_PREFIX/"; then
        die "$component: transfer failed; recovery journal retained at $pending"
    fi
    step "$component: publishing ownership records..."
    # Successful publication keeps historical paths, not obsolete hashes for
    # paths replaced by this installation. The pending journal kept both.
    local final
    final=$(mktemp "$STATE_DIR/.final.XXXXXXXX")
    awk -F '\t' 'FILENAME == ARGV[1] {updated[$3] = 1; print; next} !($3 in updated)' "$records" "$pending" > "$final"
    mv -f -- "$final" "$manifest"
    rm -f -- "$pending"
    ACTIVE_INSTALL_PENDING=""
    remove_temp "$work"
    info "$component: installation complete ($count entries, $((SECONDS - started)) seconds)"
}

stage_cmake_install() {
    local component="$1" build="$2"
    new_temp
    local stage="$TEMP_DIR"
    DESTDIR="$stage" cmake --install "$build"
    promote_stage "$component" "$stage"
    remove_temp "$stage"
}

# ---------------------------------------------------------------------------
# PKGBUILD <-> spec drift guard
# ---------------------------------------------------------------------------
# Plain text parsing only: PKGBUILD is not sourced for this check.
pkgbuild_value() {
    sed -n "s/^$1=[\"']\\{0,1\\}\\([^\"'[:space:]#]*\\).*/\\1/p" "$PKGBUILD_FILE" | head -1
}

# Compare the committed PKGBUILD with the spec. Drift only warns: builds use
# the spec's values (pkg mode syncs its build copy with arch/sync-from-spec.sh;
# dev modes read the spec's pins directly). Only an unreadable spec stops.
PKGBUILD_DRIFT=()
check_pkgbuild_drift() {
    [[ -f "$PKGBUILD_FILE" ]] || die "PKGBUILD not found: $PKGBUILD_FILE"
    [[ -f "$SPEC_FILE" ]] || die "Spec not found: $SPEC_FILE"
    local key spec_value pkg_value
    local -a pairs=(
        "pkgver:$SPEC_VERSION"
        "pkgrel:$SPEC_RELEASE"
        "_kglobalacceld_commit:$SPEC_KGA_REF"
        "_kdecoration_commit:$SPEC_KDECO_REF"
        "_kconfig_commit:$SPEC_KCONFIG_REF"
        "_greeter_commit:$SPEC_GREETER_REF"
    )
    local pair
    PKGBUILD_DRIFT=()
    for pair in "${pairs[@]}"; do
        key="${pair%%:*}"
        spec_value="${pair#*:}"
        [[ -n "$spec_value" ]] || die "Cannot read $key from $SPEC_FILE"
        pkg_value=$(pkgbuild_value "$key")
        if [[ "$pkg_value" != "$spec_value" ]]; then
            PKGBUILD_DRIFT+=("$key ${pkg_value:-<missing>} -> $spec_value")
        fi
    done
    if (( ${#PKGBUILD_DRIFT[@]} )); then
        warn "PKGBUILD is behind $SPEC_FILE; building with the spec's values:"
        printf '    %s\n' "${PKGBUILD_DRIFT[@]}"
        warn "To update the committed PKGBUILD: arch/sync-from-spec.sh PKGBUILD"
    else
        info "PKGBUILD matches spec: ${SPEC_VERSION}-${SPEC_RELEASE}, pinned revisions agree."
    fi
}

# ---------------------------------------------------------------------------
# Dependency lists (from PKGBUILD)
# ---------------------------------------------------------------------------
# pkgbuild_deps <kind>...  prints the union of the requested lists, one bare
# package name per line (version constraints and optdepends descriptions
# stripped, split package names excluded). Kinds:
#   make              top-level makedepends
#   depends:<pkg>     depends=(...) of package_<pkg>()
#   optdepends:<pkg>  optdepends=(...) of package_<pkg>()
# PKGBUILD is sourced in a separate bash process; package functions are never
# executed -- only their depends/optdepends array assignments are evaluated.
pkgbuild_deps() {
    [[ -f "$PKGBUILD_FILE" ]] || die "PKGBUILD not found: $PKGBUILD_FILE"
    CARCH="$(uname -m)" bash -c '
        pkgbuild="$1"; shift
        source "$pkgbuild" >/dev/null || { echo "Cannot source $pkgbuild" >&2; exit 1; }
        own=" ${pkgname[*]} "
        emit() {
            local d
            for d in "$@"; do
                d="${d%%:*}"
                d="${d%%[<>=]*}"
                [[ -n "$d" && "$own" != *" $d "* ]] && printf "%s\n" "$d"
            done
            return 0
        }
        for kind in "$@"; do
            case "$kind" in
                make)
                    emit "${makedepends[@]}"
                    ;;
                depends:*|optdepends:*)
                    var="${kind%%:*}"
                    pkg="${kind#*:}"
                    if ! declare -F "package_$pkg" >/dev/null; then
                        echo "PKGBUILD has no package_$pkg() function" >&2
                        exit 1
                    fi
                    body=$(declare -f "package_$pkg" | grep -E "^[[:space:]]*${var}\+?=\(") || body=""
                    (
                        depends=()
                        optdepends=()
                        eval "$body"
                        if [[ "$var" == depends ]]; then emit "${depends[@]}"; else emit "${optdepends[@]}"; fi
                    ) || exit 1
                    ;;
                *)
                    echo "Unknown dependency kind: $kind" >&2
                    exit 1
                    ;;
            esac
        done
    ' kwe-pkgbuild-deps "$PKGBUILD_FILE" "$@" | sort -u
}

# Read one of the lists into the named array; dies if PKGBUILD cannot be read.
load_deps() {
    local -n _target="$1"
    shift
    local out
    out=$(pkgbuild_deps "$@") || die "Cannot read dependency list(s) from PKGBUILD: $*"
    _target=()
    [[ -z "$out" ]] || mapfile -t _target <<< "$out"
}

# Packages from the arguments that are not satisfied (pacman -T exits 127
# when something is unmet; that is not an error here).
unmet_pkgs() {
    (( $# )) || return 0
    pacman -T "$@" || true
}

# install_missing <label> <pkg>...
install_missing() {
    local label="$1"
    shift
    local -a missing=()
    mapfile -t missing < <(unmet_pkgs "$@")
    if (( ${#missing[@]} )); then
        info "Installing ${#missing[@]} missing $label package(s):"
        printf '    %s\n' "${missing[@]}"
        sudo pacman -S --needed --noconfirm "${missing[@]}"
    else
        info "All $label packages already installed."
    fi
}

install_build_deps() {
    step "Installing build dependencies (PKGBUILD makedepends)..."
    local -a make_deps=()
    load_deps make_deps make
    install_missing build "${DEV_TOOL_PKGS[@]}" "${make_deps[@]}"
}

# Dev-session runtime packages: depends of the compositor, portal and shell
# packages (plus the greeter for --local-dev), the compositor's optdepends
# (PKGBUILD optdepends mirror the spec's Recommends: spectacle, qt5ct, ...),
# and the media extras.
runtime_pkgs() {
    local with_greeter="${1:-false}"
    local -a kinds=(depends:kineticwe depends:xdg-desktop-portal-kwe depends:kineticwe-noctalia optdepends:kineticwe)
    [[ "$with_greeter" != true ]] || kinds+=(depends:kineticwe-greeter)
    local out
    out=$(pkgbuild_deps "${kinds[@]}") || die "Cannot read runtime dependencies from PKGBUILD"
    printf '%s\n' "$out" "${MEDIA_RUNTIME_PKGS[@]}" | sed '/^$/d' | sort -u
}

# ---------------------------------------------------------------------------
# Shared-library resolution (ldd runs once per invocation)
# ---------------------------------------------------------------------------
MISSING_LIBS=()
MISSING_LIBS_COLLECTED=false
FILES_DB_WARNED=false
declare -A LIB_PKG_CACHE=()

dev_binaries() {
    printf '%s\n' \
        "$TEST_PREFIX/bin/kinetic-we" \
        "$TEST_PREFIX/bin/noctalia-kwe" \
        "$TEST_PREFIX/libexec/xdg-desktop-portal-kwe"
}

collect_missing_libs() {
    $MISSING_LIBS_COLLECTED && return 0
    MISSING_LIBS_COLLECTED=true
    local binary out lib
    local -a found=()
    while IFS= read -r binary; do
        [[ -x "$binary" ]] || continue
        out=$(LD_LIBRARY_PATH="${TEST_PREFIX}/lib64:${TEST_PREFIX}/lib" \
            ldd "$binary" 2>/dev/null | awk '/not found/ {print $1}' || true)
        [[ -n "$out" ]] || continue
        info "  $(basename "$binary") needs libraries the host does not have:"
        while IFS= read -r lib; do
            [[ -n "$lib" ]] || continue
            info "    $lib"
            found+=("$lib")
        done <<< "$out"
    done < <(dev_binaries)
    MISSING_LIBS=()
    (( ${#found[@]} )) || return 0
    mapfile -t MISSING_LIBS < <(printf '%s\n' "${found[@]}" | sort -u)
}

files_db_available() {
    local db
    for db in /var/lib/pacman/sync/*.files; do
        [[ -f "$db" ]] && return 0
    done
    if ! $FILES_DB_WARNED; then
        warn "pacman files database is missing; cannot map libraries to packages." >&2
        warn "Run: sudo pacman -Fy to enable library->package lookup" >&2
        FILES_DB_WARNED=true
    fi
    return 1
}

# resolve_lib_pkg <soname>: set RESOLVED_PKG to the repo package shipping
# usr/lib/<soname>; returns 1 if none. Called directly (not in $(...)) so the
# lookup cache and the one-time files-DB warning persist.
RESOLVED_PKG=""
resolve_lib_pkg() {
    local soname="$1" repo name ver path
    RESOLVED_PKG=""
    if [[ ! ${LIB_PKG_CACHE[$soname]+known} ]]; then
        files_db_available || return 1
        LIB_PKG_CACHE["$soname"]=""
        # --machinereadable: repo\0pkgname\0pkgver\0path\n per match.
        while IFS= read -r -d '' repo && IFS= read -r -d '' name && IFS= read -r -d '' ver && IFS= read -r path; do
            if [[ "$path" == *usr/lib/"$soname" ]]; then
                LIB_PKG_CACHE["$soname"]="$name"
                break
            fi
        done < <(pacman -F --machinereadable -- "$soname" 2>/dev/null || true)
    fi
    RESOLVED_PKG="${LIB_PKG_CACHE[$soname]}"
    [[ -n "$RESOLVED_PKG" ]]
}

lib_now_present() {
    local lib="$1" dir
    for dir in "$TEST_PREFIX/lib64" "$TEST_PREFIX/lib" /usr/lib /usr/lib64; do
        [[ -e "$dir/$lib" ]] && return 0
    done
    return 1
}

# ---------------------------------------------------------------------------
# Install host runtime dependencies (dev modes)
# ---------------------------------------------------------------------------
install_host_deps_arch() {
    local with_greeter="${1:-false}"
    step "Installing host runtime dependencies..."

    local out
    out=$(runtime_pkgs "$with_greeter") || die "Cannot compute runtime dependencies"
    local -a all_pkgs=() missing=() filtered=()
    [[ -z "$out" ]] || mapfile -t all_pkgs <<< "$out"
    mapfile -t missing < <(unmet_pkgs "${all_pkgs[@]}")

    local pkg
    for pkg in "${missing[@]}"; do
        if [[ "$pkg" == pipewire-pulse && -z "$(unmet_pkgs pulseaudio)" ]]; then
            warn "pulseaudio is installed; not installing pipewire-pulse (they conflict)."
            continue
        fi
        filtered+=("$pkg")
    done

    if (( ${#filtered[@]} )); then
        info "Installing missing session/runtime packages:"
        printf '    %s\n' "${filtered[@]}"
        sudo pacman -S --needed --noconfirm "${filtered[@]}"
    else
        info "All session/runtime packages already installed."
    fi

    # Auto-detect missing shared libraries from the built binaries.
    collect_missing_libs
    if (( ${#MISSING_LIBS[@]} )); then
        echo
        info "Missing runtime libraries:"
        local lib
        local -a resolve_pkgs=()
        for lib in "${MISSING_LIBS[@]}"; do
            if resolve_lib_pkg "$lib"; then
                info "    $lib -> $RESOLVED_PKG"
                resolve_pkgs+=("$RESOLVED_PKG")
            else
                warn "    $lib -> (no package found)"
            fi
        done
        if (( ${#resolve_pkgs[@]} )); then
            mapfile -t resolve_pkgs < <(printf '%s\n' "${resolve_pkgs[@]}" | sort -u)
            sudo pacman -S --needed --noconfirm "${resolve_pkgs[@]}"
        fi
    fi

    info "Host runtime dependencies installed."
}

# ---------------------------------------------------------------------------
# Dependency report
# ---------------------------------------------------------------------------
clean_build_deps() {
    step "Dependency report (no packages will be removed)"
    info "Locally installed binaries are not tracked by pacman; dependency removal is unsafe."
    local -a make_deps=() runtime=()
    load_deps make_deps make
    local out
    out=$(runtime_pkgs true) || die "Cannot compute runtime dependencies"
    [[ -z "$out" ]] || mapfile -t runtime <<< "$out"
    info "Installed build requirements (PKGBUILD makedepends):"
    if (( ${#make_deps[@]} )); then pacman -Q "${make_deps[@]}" 2>/dev/null || true; fi
    info "Installed runtime requirements (retain these for the local session):"
    if (( ${#runtime[@]} )); then pacman -Q "${runtime[@]}" 2>/dev/null || true; fi
    local -a unmet=()
    mapfile -t unmet < <(unmet_pkgs "${make_deps[@]}" "${runtime[@]}")
    if (( ${#unmet[@]} )); then
        info "Not installed:"
        printf '    %s\n' "${unmet[@]}"
    fi
}

cached_source() {
    local project="$1" group="$2" ref="$3"
    [[ "$ref" =~ ^[0-9a-f]{40}$ ]] || die "Invalid pinned revision for $project"
    SOURCE_CACHE="$SCRIPT_DIR/build.arch-native-deps/$project/$ref"
    if [[ ! -f "$SOURCE_CACHE/src/CMakeLists.txt" ]]; then
        new_temp
        local unpack="$TEMP_DIR"
        curl --fail --location --proto '=https' \
            "https://invent.kde.org/$group/$project/-/archive/$ref/$project-$ref.tar.gz" -o "$unpack/source.tar.gz"
        mkdir "$unpack/src"
        tar xzf "$unpack/source.tar.gz" -C "$unpack/src" --strip-components=1
        [[ -f "$unpack/src/CMakeLists.txt" ]] || die "Incomplete source archive: $project"
        mkdir -p "$SOURCE_CACHE"
        [[ ! -e "$SOURCE_CACHE/src" ]] || die "Incomplete source cache: $SOURCE_CACHE/src"
        mv "$unpack/src" "$SOURCE_CACHE/src"
    fi
}

configure_cmake() {
    local build="$1" source="$2"
    shift 2
    if [[ -f "$build/CMakeCache.txt" ]]; then
        local old_source
        old_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$build/CMakeCache.txt")
        [[ "$old_source" == "$source" ]] || die "Incompatible CMake source in $build; choose or move the cache manually"
    fi
    cmake -B "$build" -S "$source" \
        -DCMAKE_INSTALL_PREFIX="$TEST_PREFIX" -DCMAKE_PREFIX_PATH="$TEST_PREFIX" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
        -DKDE_INSTALL_LIBDIR=lib -DKDE_INSTALL_LIBEXECDIR=libexec \
        -DKDE_INSTALL_QMLDIR=lib/qml -DKDE_INSTALL_PLUGINDIR=lib/qt6/plugins \
        -DKDE_INSTALL_SYSTEMDUSERUNITDIR=lib/systemd/user "$@"
}

build_pinned_deps() {
    local project ref package record
    for ref in "$SPEC_KGA_REF" "$SPEC_KDECO_REF" "$SPEC_KCONFIG_REF"; do
        [[ "$ref" =~ ^[0-9a-f]{40}$ ]] || die "Missing or invalid KDE revision in $SPEC_FILE"
    done
    for project in kglobalacceld kdecoration; do
        if [[ "$project" == kglobalacceld ]]; then
            ref="$SPEC_KGA_REF"; package=KGlobalAccelD
        else
            ref="$SPEC_KDECO_REF"; package=KDecoration3
        fi
        record="$STATE_DIR/$project.revision"
        [[ ! -L "$record" ]] || die "Symlinked revision state"
        if [[ -f "$record" && "$(cat "$record")" == "$ref" && -f "$TEST_PREFIX/lib/cmake/$package/${package}Config.cmake" && -f "$STATE_DIR/$project.manifest" ]] && manifest_intact "$STATE_DIR/$project.manifest"; then
            info "$project revision $ref already installed"
            continue
        fi
        cached_source "$project" plasma "$ref"
        configure_cmake "$SOURCE_CACHE/build" "$SOURCE_CACHE/src"
        cmake --build "$SOURCE_CACHE/build" --parallel "$(nproc)"
        stage_cmake_install "$project" "$SOURCE_CACHE/build"
        local revision_tmp
        revision_tmp=$(mktemp "$STATE_DIR/.revision.XXXXXXXX")
        printf '%s\n' "$ref" > "$revision_tmp"
        mv "$revision_tmp" "$record"
    done
    KCFG_COMPILER_OVERRIDE=""
    local compiler="" candidate
    compiler=$(command -v kconfig_compiler_kf6 || true)
    if [[ -z "$compiler" ]]; then
        while IFS= read -r candidate; do
            if [[ "$candidate" == */kconfig_compiler_kf6 && -x "$candidate" ]]; then compiler="$candidate"; break; fi
        done < <(pacman -Qlq kconfig)
    fi
    new_temp
    local probe="$TEMP_DIR"
    if [[ -n "$compiler" ]]; then
        "$compiler" "$SCRIPT_DIR/src/rulesettings.kcfg" "$SCRIPT_DIR/src/rulesettings.kcfgc" -d "$probe" || true
    fi
    if ! grep -q 'mDecorationpolicy = int(v)' "$probe/rulesettings.h" 2>/dev/null; then
        cached_source kconfig frameworks "$SPEC_KCONFIG_REF"
        configure_cmake "$SOURCE_CACHE/build" "$SOURCE_CACHE/src" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$SOURCE_CACHE/build" --target kconfig_compiler --parallel "$(nproc)"
        KCFG_COMPILER_OVERRIDE="$SOURCE_CACHE/build/bin/kconfig_compiler_kf6"
        [[ -x "$KCFG_COMPILER_OVERRIDE" ]] || die "Fixed KConfig compiler missing"
    fi
}

binary_build() {
    local build_portal="${1:-true}" build_shell="${2:-true}"
    local build_dir="$SCRIPT_DIR/build.arch-native"
    build_pinned_deps
    configure_cmake "$build_dir" "$SCRIPT_DIR" \
        -DKWIN_KCONFIG_COMPILER="$KCFG_COMPILER_OVERRIDE" -DKWIN_BUILD_GLOBALSHORTCUTS=ON \
        -DKGlobalAccelD_DIR="$TEST_PREFIX/lib/cmake/KGlobalAccelD" \
        -DKDecoration3_DIR="$TEST_PREFIX/lib/cmake/KDecoration3"
    cmake --build "$build_dir" --parallel "$(nproc)"
    stage_cmake_install compositor "$build_dir"
    if [[ "$build_portal" == true ]]; then
        local portal_build="$SCRIPT_DIR/build.arch-native-portal"
        configure_cmake "$portal_build" "$SCRIPT_DIR/portal/xdg-desktop-portal-kwe"
        cmake --build "$portal_build" --parallel "$(nproc)"
        stage_cmake_install portal "$portal_build"
    fi
    if [[ "$build_shell" == true ]]; then noctalia_only_build; fi
    [[ -x "$TEST_PREFIX/bin/kinetic-we" ]] || die "Compositor installation is missing"
}

kwin_only_build() { binary_build false false; }

# stb headers come from the Arch `stb` package (/usr/include/stb/*), which is
# part of PKGBUILD makedepends.
noctalia_only_build() {
    local build="$SCRIPT_DIR/build.arch-native-noctalia"
    local -a setup=()
    [[ ! -f "$build/meson-private/coredata.dat" ]] || setup+=(--reconfigure)
    meson setup "${setup[@]}" "$build" "$SCRIPT_DIR/shell/noctalia" \
        --prefix="$TEST_PREFIX" --buildtype=debugoptimized -Dtests=disabled
    meson compile -C "$build" --jobs "$(nproc)"
    new_temp
    local stage="$TEMP_DIR"
    DESTDIR="$stage" meson install --no-rebuild -C "$build"
    promote_stage shell "$stage"
    remove_temp "$stage"
    [[ -x "$TEST_PREFIX/bin/noctalia-kwe" ]] || die "Shell installation is missing"
}

# ---------------------------------------------------------------------------
# QML runtime module verification
# ---------------------------------------------------------------------------

# Map a QML module path to the Arch package that ships it (for the error
# message only).
qml_module_package() {
    case "$1" in
        org/kde/milou)               echo milou ;;
        org/kde/plasma/*)            echo libplasma ;;
        Qt5Compat/GraphicalEffects)  echo qt6-5compat ;;
        org/kde/kirigami)            echo kirigami ;;
        org/kde/ksvg)                echo ksvg ;;
        org/kde/kcmutils)            echo kcmutils ;;
        org/kde/kitemmodels)         echo kitemmodels ;;
        org/kde/pipewire)            echo kpipewire ;;
        org/kde/ki18n)               echo ki18n ;;
        org/kde/iconthemes)          echo kiconthemes ;;
        org/kde/kquickcontrols)      echo kdeclarative ;;
        *)                           echo "$1" ;;
    esac
}

# QML import roots searched on the host (dev prefix first, then the system Qt).
# ECM installs KWin's private modules under <libdir>/qml while the Qt/KDE stack
# lives under <libdir>/qt6/qml; the libdir is lib64 on some prefixes and lib on
# others, so cover both for each layout.
qml_import_roots() {
    local -a roots=(
        "${TEST_PREFIX}/lib64/qml"
        "${TEST_PREFIX}/lib64/qt6/qml"
        "${TEST_PREFIX}/lib/qml"
        "${TEST_PREFIX}/lib/qt6/qml"
        /usr/lib64/qml
        /usr/lib64/qt6/qml
        /usr/lib/qml
        /usr/lib/qt6/qml
    )
    local root
    for root in "${roots[@]}"; do
        [[ -d "$root" ]] && printf '%s\n' "$root"
    done
}

# Hard gate: every QML module the compositor's built-in effects and the portal
# backend import must be present. A missing module never fails ldd; it only
# shows up at runtime as a blank Overview/Grid View (effects) or a screen
# chooser that fails to open (portal), so check explicitly and fail loudly.
verify_qml_modules() {
    local -a roots=()
    mapfile -t roots < <(qml_import_roots)
    if [[ ${#roots[@]} -eq 0 ]]; then
        warn "No Qt QML import roots found; cannot verify effect/portal QML modules."
        return 0
    fi

    local -a missing_modules=()
    local -a missing_pkgs=()
    local module root found pkg
    for module in "${EFFECT_QML_MODULES[@]}" "${PORTAL_QML_MODULES[@]}"; do
        found=false
        for root in "${roots[@]}"; do
            if [[ -e "$root/$module/qmldir" || -e "$root/$module" ]]; then
                found=true
                break
            fi
        done
        if ! $found; then
            missing_modules+=("$module")
            pkg=$(qml_module_package "$module")
            missing_pkgs+=("$pkg")
        fi
    done

    if [[ ${#missing_modules[@]} -eq 0 ]]; then
        return 0
    fi

    error "Missing QML modules required by the compositor effects and/or portal:"
    local i
    for i in "${!missing_modules[@]}"; do
        error "    ${missing_modules[$i]} (${missing_pkgs[$i]})"
    done
    local -a unique_pkgs
    mapfile -t unique_pkgs < <(printf '%s\n' "${missing_pkgs[@]}" | sort -u)
    error "Install with: sudo pacman -S ${unique_pkgs[*]}"
    return 1
}

# ---------------------------------------------------------------------------
# Verify host runtime dependencies of the installed dev binaries
# ---------------------------------------------------------------------------
# Reuses the ldd scan from install_host_deps_arch; libraries installed since
# then are filtered out by a file check instead of a second ldd pass.
verify_host_deps() {
    step "Verifying host runtime dependencies..."

    if ! verify_qml_modules; then
        exit 1
    fi

    collect_missing_libs
    local -a still_missing=()
    local lib
    for lib in "${MISSING_LIBS[@]}"; do
        lib_now_present "$lib" || still_missing+=("$lib")
    done

    if (( ${#still_missing[@]} == 0 )); then
        info "All runtime libraries resolve on the host."
        return 0
    fi

    echo
    error "The host is missing runtime libraries needed by the dev session."
    echo
    echo "  Missing library -> providing package:"
    local -a pkgs=()
    for lib in "${still_missing[@]}"; do
        if resolve_lib_pkg "$lib"; then
            echo "    $lib -> $RESOLVED_PKG"
            pkgs+=("$RESOLVED_PKG")
        else
            echo "    $lib -> (no package found; try: pacman -F $lib)"
        fi
    done
    echo
    if (( ${#pkgs[@]} )); then
        mapfile -t pkgs < <(printf '%s\n' "${pkgs[@]}" | sort -u)
        echo "  Install with: sudo pacman -S ${pkgs[*]}"
    fi
    echo
    warn "The build succeeded but the session will fail to start until these"
    warn "libraries are installed on the host."
    exit 1
}

# ---------------------------------------------------------------------------
# Install the single dev startup script for TTY testing
# ---------------------------------------------------------------------------
validate_template_prefix() {
    # Templates contain shell assignments and desktop Exec values. Restrict
    # metacharacters rather than injecting executable text into either format.
    case "$TEST_PREFIX" in *'\'*|*'"'*|*'`'*|*'$'*|*'%'*) die "Prefix contains unsupported template metacharacters" ;; esac
}

render_prefix_template() {
    validate_template_prefix
    local replacement="${TEST_PREFIX//&/\\&}"
    replacement="${replacement//|/\\|}"
    sed "s|@INSTALL_PREFIX@|$replacement|g" "$1"
}

install_startup_script() {
    new_temp
    local stage="$TEMP_DIR"
    mkdir -p "$stage$TEST_PREFIX/bin" "$stage$TEST_PREFIX/share/kineticwe"
    render_prefix_template "$SCRIPT_DIR/scripts/start-kineticwe.sh" > "$stage$TEST_PREFIX/bin/start-kineticwe"
    chmod +x "$stage$TEST_PREFIX/bin/start-kineticwe"
    install -m 0644 "$SCRIPT_DIR/scripts/kineticwe-session.target" "$stage$TEST_PREFIX/share/kineticwe/kineticwe-session.target"
    promote_stage launcher "$stage"
    remove_temp "$stage"
}

validate_session_parent() {
    [[ "$(realpath -m -- "$SESSION_DIR")" == "$SESSION_DIR" ]] || die "System session directory is redirected by a symlink"
    [[ ! -L "$SESSION_DIR/$SESSION_DESKTOP" ]] || die "System session entry is a symlink; refusing privileged modification"
}

install_dev_session_entry() {
    validate_session_parent
    new_temp
    local tmp_file="$TEMP_DIR/session.desktop" target="$SESSION_DIR/$SESSION_DESKTOP"
    local record="$STATE_DIR/system-session.manifest" pending="$STATE_DIR/system-session.pending"
    [[ ! -L "$record" && ! -L "$pending" ]] || die "Symlinked session state"
    render_prefix_template "$SCRIPT_DIR/scripts/kineticwe-dev.desktop.in" | sed 's/^Exec=\(.*\)$/Exec="\1"/' > "$tmp_file"
    local hash
    hash=$(sha256sum < "$tmp_file")
    new_temp
    local records="$TEMP_DIR/session-records"
    printf 'F\t%s\t%s\n' "${hash%% *}" "$target" > "$records"
    [[ ! -f "$record" ]] || cat "$record" >> "$records"
    [[ ! -f "$pending" ]] || cat "$pending" >> "$records"
    local session_tmp
    session_tmp=$(mktemp "$STATE_DIR/.session.XXXXXXXX")
    sort -u "$records" > "$session_tmp"
    mv "$session_tmp" "$pending"
    if [[ -f "$target" && ! -L "$target" ]] && cmp -s "$tmp_file" "$target"; then
        info "$target is up to date"
    elif sudo mkdir -p "$SESSION_DIR" && sudo install -m 0644 "$tmp_file" "$target"; then
        info "Installed $target"
    else
        warn "Session entry installation failed; recovery record retained. TTY startup remains available."
        return
    fi
    session_tmp=$(mktemp "$STATE_DIR/.session.XXXXXXXX")
    printf 'F\t%s\t%s\n' "${hash%% *}" "$target" > "$session_tmp"
    mv "$session_tmp" "$record"
    rm -f "$pending"
}

# ---------------------------------------------------------------------------
# Resolve the greetd greeter user
# ---------------------------------------------------------------------------
resolve_greeter_user() {
    local cfg="/etc/greetd/config.toml"
    local user=""
    if [[ -f "$cfg" ]]; then
        user=$(awk '
            /^\[default_session\]/  { in_block=1; next }
            /^\[/                   { in_block=0 }
            in_block && /^[[:space:]]*user[[:space:]]*=/ {
                sub(/^[[:space:]]*user[[:space:]]*=[[:space:]]*"/, "")
                sub(/".*$/, "")
                print; exit
            }
        ' "$cfg" 2>/dev/null || true)
    fi
    if [[ -z "$user" ]]; then
        if id greetd &>/dev/null; then
            user="greetd"
        else
            user="greeter"
        fi
    fi
    printf '%s\n' "$user"
}

# ---------------------------------------------------------------------------
# Print the greetd [default_session] command value (quotes stripped)
# ---------------------------------------------------------------------------
# Read-only. Prints nothing when the file, the block or an active (uncommented)
# command line is missing. Shared by --local-dev and the package-mode migration
# check so both agree on what "configured" means.
greetd_default_command() {
    local cfg="${1:-/etc/greetd/config.toml}"
    [[ -f "$cfg" && -r "$cfg" ]] || return 0
    awk '
        /^[[:space:]]*\[default_session\][[:space:]]*(#.*)?$/ { in_block = 1; next }
        /^[[:space:]]*\[/ { in_block = 0 }
        in_block && /^[[:space:]]*command[[:space:]]*=/ {
            v = $0
            sub(/^[[:space:]]*command[[:space:]]*=[[:space:]]*/, "", v)
            if (v ~ /^"/) {
                v = substr(v, 2); sub(/".*$/, "", v)
            } else if (v ~ /^\047/) {
                v = substr(v, 2); sub(/\047.*$/, "", v)
            } else {
                sub(/[[:space:]]*#.*$/, "", v); sub(/[[:space:]]+$/, "", v)
            }
            print v; exit
        }
    ' "$cfg" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Build/install the Noctalia greeter at the pinned revision (--local-dev)
# ---------------------------------------------------------------------------
# Pinned commits are fetched as a GitLab archive; a non-commit ref (branch or
# tag from NOCTALIA_GREETER_REF) is shallow-cloned and always rebuilt. The
# installed revision is recorded in the user-owned state directory.
build_noctalia_greeter() {
    local ref="$NOCTALIA_GREETER_REF" repo="${NOCTALIA_GREETER_REPO%/}"
    local record="$STATE_DIR/greeter.revision" installed=""
    [[ -n "$ref" ]] || die "No greeter revision: spec lacks noctalia_greeter_ref and NOCTALIA_GREETER_REF is unset"
    [[ ! -L "$record" ]] || die "Symlinked revision state"
    [[ -f "$record" ]] && installed=$(cat "$record")

    local pinned=false
    [[ "$ref" =~ ^[0-9a-f]{40}$ ]] && pinned=true

    if $pinned && [[ "$installed" == "$ref" ]] && command -v noctalia-greeter-session &>/dev/null; then
        info "noctalia-greeter revision $ref already installed."
        return 0
    fi

    new_temp
    local work="$TEMP_DIR" src="$TEMP_DIR/source" revision
    if $pinned; then
        repo="${repo%.git}"
        local name="${repo##*/}"
        info "Fetching noctalia-greeter $ref..."
        curl --fail --location --proto '=https' \
            "$repo/-/archive/$ref/$name-$ref.tar.gz" -o "$work/greeter.tar.gz"
        mkdir "$src"
        tar xzf "$work/greeter.tar.gz" -C "$src" --strip-components=1
        revision="$ref"
    else
        info "Cloning ${NOCTALIA_GREETER_REPO} (${ref})..."
        git clone --depth=1 --branch "$ref" "$NOCTALIA_GREETER_REPO" "$src"
        revision="$ref@$(git -C "$src" rev-parse HEAD)"
    fi
    [[ -f "$src/meson.build" ]] || die "Incomplete noctalia-greeter source ($ref)"

    info "Configuring noctalia-greeter..."
    meson setup "$src/build" "$src" --prefix=/usr/local --buildtype=release
    info "Building noctalia-greeter..."
    meson compile -C "$src/build" --jobs "$(nproc)"
    info "Installing noctalia-greeter to /usr/local..."
    sudo meson install --no-rebuild -C "$src/build"

    local revision_tmp
    revision_tmp=$(mktemp "$STATE_DIR/.revision.XXXXXXXX")
    printf '%s\n' "$revision" > "$revision_tmp"
    mv -f -- "$revision_tmp" "$record"
    remove_temp "$work"
}

# ---------------------------------------------------------------------------
# Install and activate the Noctalia greeter (greetd + noctalia-greeter)
# ---------------------------------------------------------------------------
# When the kineticwe-greeter package is installed it is used as-is: nothing is
# built into /usr/local (which would shadow the packaged greeter). Otherwise
# the pinned source revision is built and installed into /usr/local.
setup_noctalia_greeter() {
    install_missing greetd greetd

    local session_bin setup_script
    if pacman -Q kineticwe-greeter &>/dev/null; then
        step "Setting up Noctalia greeter (greetd + packaged kineticwe-greeter)..."
        info "kineticwe-greeter package is installed; using it instead of building into /usr/local."
        session_bin="/usr/bin/noctalia-greeter-session"
        setup_script="/usr/share/noctalia-greeter/setup_greeter_system.sh"
        if [[ ! -x "$session_bin" ]]; then
            error "$session_bin is missing although kineticwe-greeter is installed (reinstall the package)."
            return 1
        fi
    else
        step "Setting up Noctalia greeter (greetd + noctalia-greeter from source)..."
        build_noctalia_greeter
        session_bin="/usr/local/bin/noctalia-greeter-session"
        setup_script="/usr/local/share/noctalia-greeter/setup_greeter_system.sh"
        if [[ ! -x "$session_bin" ]]; then
            error "$session_bin was not found after installation."
            return 1
        fi
    fi

    local greeter_user
    greeter_user=$(resolve_greeter_user)
    info "Greeter session user: ${greeter_user}"

    if ! id "$greeter_user" &>/dev/null; then
        info "Creating greetd greeter user '${greeter_user}'..."
        sudo useradd -r -s /usr/bin/nologin -d /var/lib/noctalia-greeter "$greeter_user" \
            || warn "Could not create user '${greeter_user}'; the greeter may not start."
    fi

    local -a need_groups=()
    local grp
    for grp in video input; do
        if getent group "$grp" &>/dev/null; then
            need_groups+=("$grp")
        else
            warn "Group '${grp}' does not exist; skipping (greeter may lack DRM/input access)."
        fi
    done
    if [[ ${#need_groups[@]} -gt 0 ]] && id "$greeter_user" &>/dev/null; then
        local user_groups
        user_groups=$(id -Gn "$greeter_user" 2>/dev/null || true)
        local -a add_groups=()
        for grp in "${need_groups[@]}"; do
            [[ " $user_groups " != *" $grp "* ]] && add_groups+=("$grp")
        done
        if [[ ${#add_groups[@]} -gt 0 ]]; then
            local group_list
            group_list=$(IFS=,; echo "${add_groups[*]}")
            info "Adding '${greeter_user}' to groups: ${add_groups[*]}"
            sudo usermod -a -G "$group_list" "$greeter_user" \
                || warn "Could not add '${greeter_user}' to ${add_groups[*]}."
        else
            info "'${greeter_user}' is already in the required groups."
        fi
    fi

    local greetd_cfg="/etc/greetd/config.toml" current_cmd
    current_cmd=$(greetd_default_command "$greetd_cfg")
    if [[ "$current_cmd" == "$session_bin" ]]; then
        info "greetd already configured to use ${session_bin}."
    else
        if [[ -n "$current_cmd" ]]; then
            info "greetd default_session currently runs: ${current_cmd}"
        fi
        info "Configuring greetd to launch the Noctalia greeter (${session_bin})..."
        new_temp
        local tmp_cfg="$TEMP_DIR/config.toml"
        if [[ -f "$greetd_cfg" ]]; then
            cp "$greetd_cfg" "$tmp_cfg"
        else
            cat > "$tmp_cfg" <<EOF
[terminal]
vt = 1

[default_session]
command = ""
user = "${greeter_user}"
EOF
        fi

        # Rewrite command/user inside [default_session]; append a command line
        # when an existing block has none.
        awk -v session="$session_bin" -v guser="$greeter_user" '
            function close_block() {
                if (in_block && !seen_cmd) print "command = \"" session "\""
                in_block = 0
            }
            BEGIN { in_block = 0; seen_cmd = 0 }
            /^\[default_session\]/ { close_block(); in_block = 1; seen_cmd = 0; print; next }
            /^\[/ { close_block() }
            in_block && /^[[:space:]]*command[[:space:]]*=/ {
                print "command = \"" session "\""; seen_cmd = 1; next
            }
            in_block && /^[[:space:]]*user[[:space:]]*=/ {
                print "user = \"" guser "\""; next
            }
            { print }
            END { close_block() }
        ' "$tmp_cfg" > "$tmp_cfg.new"

        if ! grep -q '^\[default_session\]' "$tmp_cfg.new"; then
            printf '\n[default_session]\ncommand = "%s"\nuser = "%s"\n' "$session_bin" "$greeter_user" >> "$tmp_cfg.new"
        fi

        if [[ -f "$greetd_cfg" && ! -e "$greetd_cfg.kwe-bak" ]]; then
            info "Backing up ${greetd_cfg} to ${greetd_cfg}.kwe-bak"
            sudo cp -a -- "$greetd_cfg" "$greetd_cfg.kwe-bak"
        fi
        sudo mkdir -p "$(dirname "$greetd_cfg")"
        sudo install -m 0644 "$tmp_cfg.new" "$greetd_cfg"
        remove_temp "$TEMP_DIR"
        info "Wrote ${greetd_cfg}: default_session -> ${session_bin}"
    fi

    if [[ -x "$setup_script" ]]; then
        info "Running Noctalia greeter system setup..."
        sudo "$setup_script"
    else
        warn "Noctalia greeter setup script not found: $setup_script"
    fi

    if [[ "$(systemctl get-default)" != "graphical.target" ]]; then
        info "Setting default boot target to graphical.target..."
        sudo systemctl set-default graphical.target
    else
        info "Default boot target is already graphical.target."
    fi

    local current_dm
    current_dm=$(readlink -f /etc/systemd/system/display-manager.service 2>/dev/null || true)
    if [[ "$current_dm" == *greetd.service* ]]; then
        info "greetd is already the active display manager."
    else
        if [[ -n "$current_dm" ]]; then
            local current_unit
            current_unit=$(basename "$current_dm")
            info "Disabling current display manager: ${current_unit}"
            sudo systemctl disable "$current_unit"
        fi
        info "Enabling greetd as the display manager..."
        sudo systemctl enable greetd.service
        echo
        info "The Noctalia greeter is configured."
        info "Log out or reboot to use it."
    fi
}

# ---------------------------------------------------------------------------
# Reset local dev install
# ---------------------------------------------------------------------------
record_matches() {
    local kind="$1" value="$2" file="$3" actual
    if [[ "$kind" == L && -L "$file" ]]; then
        [[ "$(readlink -- "$file")" == "$value" ]]
    elif [[ "$kind" == F && -f "$file" && ! -L "$file" ]]; then
        actual=$(sha256sum < "$file")
        [[ "${actual%% *}" == "$value" ]]
    else
        return 1
    fi
}

manifest_intact() {
    local manifest="$1" kind value rel
    [[ ! -L "$manifest" ]] || die "Symlinked manifest"
    while IFS=$'\t' read -r kind value rel; do
        safe_path "$rel"
        record_matches "$kind" "$value" "$TEST_PREFIX/$rel" || return 1
    done < "$manifest"
}

# --reset-local found no install records: either nothing was ever installed,
# or the files came from the old build-arch-native.sh, which kept no records
# (and whose files --reset-local therefore can't identify safely).
reset_without_records() {
    if [[ -e "$TEST_PREFIX/bin/kinetic-we" || -e "$TEST_PREFIX/bin/noctalia-kwe" \
          || -e "$HOME/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kwe.service" ]]; then
        error "Found KineticWE dev files in $TEST_PREFIX, but no install records."
        error "They come from the old build-arch-native.sh, which --reset-local can't remove safely."
        error "Follow docs/arch-migration.md to remove them (it also fixes broken portals)."
        exit 1
    fi
    info "Nothing to reset: no local dev install found in $TEST_PREFIX."
    exit 0
}

reset_local() {
    # Do not create an installation prefix just to discover there is no record.
    TEST_PREFIX=$(realpath -m -- "$TEST_PREFIX")
    STATE_DIR="$TEST_PREFIX/share/kineticwe/arch-native-state"
    safe_path "share/kineticwe/arch-native-state/.lock"
    [[ -d "$STATE_DIR" ]] || reset_without_records
    local -a manifests=()
    local manifest kind value rel file parent
    shopt -s nullglob
    manifests=("$STATE_DIR/"*.manifest "$STATE_DIR/"*.pending)
    shopt -u nullglob
    (( ${#manifests[@]} )) || reset_without_records
    prepare_prefix
    # Validate every record before deleting anything.
    for manifest in "${manifests[@]}"; do
        [[ ! -L "$manifest" ]] || die "Symlinked manifest"
        while IFS=$'\t' read -r kind value rel; do
            [[ "$kind" == F || "$kind" == L ]] || die "Invalid manifest record"
            if [[ "$manifest" == "$STATE_DIR/system-session."* ]]; then
                [[ "$kind" == F && "$rel" == "$SESSION_DIR/$SESSION_DESKTOP" ]] || die "Invalid system session record"
            else
                safe_path "$rel"
                [[ "$rel" != share/kineticwe/arch-native-state && "$rel" != share/kineticwe/arch-native-state/* ]] || die "Manifest targets ownership state"
            fi
        done < "$manifest"
    done
    for manifest in "${manifests[@]}"; do
        while IFS=$'\t' read -r kind value rel; do
            if [[ "$manifest" == "$STATE_DIR/system-session."* ]]; then
                file="$rel"
                if record_matches "$kind" "$value" "$file"; then
                    validate_session_parent
                    sudo rm -f -- "$file"
                elif [[ -e "$file" || -L "$file" ]]; then warn "Preserving modified session entry: $file"; fi
                continue
            fi
            safe_path "$rel"
            file="$TEST_PREFIX/$rel"
            if record_matches "$kind" "$value" "$file"; then
                rm -f -- "$file"
                parent=$(dirname -- "$file")
                while [[ "$parent" != "$TEST_PREFIX" && "$parent" == "$TEST_PREFIX/"* ]]; do
                    rmdir -- "$parent" 2>/dev/null || break
                    parent=$(dirname -- "$parent")
                done
            elif [[ -e "$file" || -L "$file" ]]; then
                warn "Preserving modified file: $file"
            fi
        done < "$manifest"
    done
    # Keep ownership records for preserved files and interrupted operations.
    # Invalidate dependency stamps so the next build restores removed outputs.
    local revision
    for revision in "$STATE_DIR/kglobalacceld.revision" "$STATE_DIR/kdecoration.revision"; do
        [[ ! -L "$revision" ]] || die "Symlinked revision state"
        rm -f -- "$revision"
    done
    info "Manifest reset complete; modified files, ownership records, build caches and greeter configuration retained."
}

# ---------------------------------------------------------------------------
# Qt ABI guard
# ---------------------------------------------------------------------------
# KWin uses Qt private APIs, so binaries built against one qt6-base must be
# rebuilt after a Qt update. Record the version at build time and warn later.
current_qt() {
    pacman -Q qt6-base 2>/dev/null || true
}

record_qt_abi() {
    local file="$1" qt tmp
    qt=$(current_qt)
    [[ -n "$qt" ]] || return 0
    [[ ! -L "$file" ]] || die "Symlinked Qt ABI record: $file"
    tmp=$(mktemp "$(dirname -- "$file")/.qt-abi.XXXXXXXX")
    printf '%s\n' "$qt" > "$tmp"
    mv -f -- "$tmp" "$file"
}

check_qt_abi() {
    local file="$1" old new
    [[ -f "$file" && ! -L "$file" ]] || return 0
    old=$(<"$file")
    new=$(current_qt)
    if [[ -n "$new" && "$old" != "$new" ]]; then
        warn "Qt changed since last build ($old -> $new); KWin uses Qt private APIs — rebuild before starting the session"
    fi
}

# ---------------------------------------------------------------------------
# Migration check (package mode): report ~/.local and /usr/local leftovers
# ---------------------------------------------------------------------------
# Strictly read-only: never deletes, moves or rewrites anything, takes no lock
# and creates no directories. Each finding prints a warning plus the command
# that fixes it.
# Findings of check_migration_leftovers, collected so print_usage can group them.
MIGRATION_LEFTOVERS=()
leftover() { MIGRATION_LEFTOVERS+=("$*"); }

check_migration_leftovers() {
    local prefix data_home found=0 p
    MIGRATION_LEFTOVERS=()
    prefix=$(realpath -m -- "$TEST_PREFIX")
    data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
    local greeter_pkg=false
    pacman -Q kineticwe-greeter &>/dev/null && greeter_pkg=true

    # a. Recorded dev install in TEST_PREFIX.
    local -a manifests=()
    if [[ -d "$prefix/share/kineticwe/arch-native-state" ]]; then
        for p in "$prefix/share/kineticwe/arch-native-state/"*.manifest; do
            if [[ -f "$p" ]]; then manifests+=("$p"); fi
        done
    fi
    if (( ${#manifests[@]} )) && [[ -e "$prefix/bin/kinetic-we" || -e "$prefix/bin/noctalia-kwe" ]]; then
        found=1
        leftover "A recorded dev install is still present in $prefix (it shadows the packages)."
        leftover "  Fix: log into 'KineticWE' (not 'KineticWE-Dev') and run: ./install-arch.sh --reset-local"
    fi

    # b. User D-Bus service overriding the packaged portal.
    local portal_svc="$HOME/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kwe.service"
    if [[ -e "$portal_svc" || -L "$portal_svc" ]]; then
        found=1
        leftover "User D-Bus service overrides the packaged portal: $portal_svc"
        if (( ${#manifests[@]} )); then
            leftover "  Fix: ./install-arch.sh --reset-local, then: pkill -f 'libexec/xdg-desktop-portal-kwe'"
            leftover "       and: systemctl --user restart xdg-desktop-portal.service"
        else
            leftover "  From the old build-arch-native.sh; it can break every portal (203/EXEC)."
            leftover "  Fix: docs/arch-migration.md, Step 1 (removes it and restarts the portal)."
        fi
    fi

    # c. Pre-manifest dev leftovers (not tracked by --reset-local).
    local -a stale=()
    if [[ -d "$prefix/lib/libexec" ]]; then
        for p in "$prefix/lib/libexec/"kineticwe_* "$prefix/lib/libexec/xdg-desktop-portal-kwe"; do
            if [[ -e "$p" || -L "$p" ]]; then stale+=("$p"); fi
        done
    fi
    for p in "$prefix/lib/plugins/kineticwe" "$prefix/lib/plugins/org.kineticwe.decoration"; do
        if [[ -e "$p" || -L "$p" ]]; then stale+=("$p"); fi
    done
    # Old-script user units: inert (systemd never reads ~/.local/lib/systemd/user)
    # but they point at removed files. Recorded installs remove their own copies.
    if (( ! ${#manifests[@]} )); then
        for p in "$prefix/lib/systemd/user/kineticwe.service" \
                 "$prefix/lib/systemd/user/plasma-xdg-desktop-portal-kwe.service"; do
            if [[ -e "$p" || -L "$p" ]]; then stale+=("$p"); fi
        done
    fi
    if (( ${#stale[@]} )); then
        found=1
        leftover "Untracked files from the old build-arch-native.sh (not removed by --reset-local):"
        for p in "${stale[@]}"; do leftover "    $p"; done
        leftover "  Fix: follow docs/arch-migration.md (removes these and the rest of the old install)."
    fi

    # d. User systemd unit overriding the packaged session target.
    local user_target="$data_home/systemd/user/kineticwe-session.target"
    if [[ -e "$user_target" || -L "$user_target" ]] && [[ -e /usr/lib/systemd/user/kineticwe-session.target ]]; then
        found=1
        leftover "User unit overrides the packaged kineticwe-session.target: $user_target"
        leftover "  Fix: rm -f '$user_target' && systemctl --user daemon-reload"
    fi

    if [[ "$greeter_pkg" == true ]]; then
        # e. Source-built greeter in /usr/local shadowing the package.
        local -a local_greeter=()
        for p in /usr/local/bin/noctalia-greeter* \
                 /usr/local/share/noctalia-greeter \
                 /usr/local/lib/tmpfiles.d/noctalia-greeter.conf \
                 /usr/local/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy; do
            if [[ -e "$p" || -L "$p" ]]; then local_greeter+=("$p"); fi
        done
        if (( ${#local_greeter[@]} )); then
            found=1
            leftover "A source-built greeter in /usr/local shadows the kineticwe-greeter package:"
            for p in "${local_greeter[@]}"; do leftover "    $p"; done
            leftover "  Fix: sudo rm -rf$(printf " '%s'" "${local_greeter[@]}")"
        fi
    fi

    # greetd not using the packaged greeter is shown by print_usage's greeter
    # box instead of here.
    (( ! found ))
}

# ---------------------------------------------------------------------------
# Print usage info
# ---------------------------------------------------------------------------
PKG_DEV_RELEASE=""
PKG_INSTALLED=()

# ---------------------------------------------------------------------------
# End-of-run summary
# ---------------------------------------------------------------------------
# Colour and box-drawing glyphs only on a UTF-8 terminal without NO_COLOR;
# plain ASCII otherwise, so logs and pipes stay readable.
SUMMARY_WIDTH=60
ui_init() {
    local charmap="${LC_ALL:-${LC_CTYPE:-${LANG:-}}}"
    if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
        U_B=$'\033[1m' U_DIM=$'\033[2m' U_OK=$'\033[1;32m' U_WARN=$'\033[1;33m'
        U_CMD=$'\033[1;36m' U_R=$'\033[0m'
    else
        U_B='' U_DIM='' U_OK='' U_WARN='' U_CMD='' U_R=''
    fi
    if [[ -t 1 && "${charmap,,}" == *utf-8* || -t 1 && "${charmap,,}" == *utf8* ]]; then
        G_BULLET='•' G_OK='✔' G_WARN='⚠' G_RULE='─' G_H='═' G_V='║' G_TL='╔' G_TR='╗' G_BL='╚' G_BR='╝'
    else
        G_BULLET='-' G_OK='[ok]' G_WARN='[!]' G_RULE='-' G_H='-' G_V='|' G_TL='+' G_TR='+' G_BL='+' G_BR='+'
    fi
}

# repeat <char> <count>
repeat() { local out='' i; for (( i = 0; i < $2; i++ )); do out+="$1"; done; printf '%s' "$out"; }

summary_rule() { printf '%s%s%s\n' "$U_DIM" "$(repeat "$G_RULE" "$SUMMARY_WIDTH")" "$U_R"; }

# summary_title <text>: a heading between two rules.
summary_title() {
    echo
    summary_rule
    printf '  %s%s %s%s\n' "$U_OK" "$G_OK" "$1" "$U_R"
    summary_rule
}

# summary_box <line>...: an emphasised box. A line starting with "$ " is shown
# as a command. Padding is computed on the plain text before colour is added,
# so the right border always lines up.
summary_box() {
    local inner=$(( SUMMARY_WIDTH - 6 )) line text colour pad
    printf '  %s%s%s%s%s\n' "$U_WARN" "$G_TL" "$(repeat "$G_H" $(( inner + 2 )))" "$G_TR" "$U_R"
    for line in "$@"; do
        colour="$U_B"
        text="$line"
        if [[ "$line" == '$ '* ]]; then
            text="     ${line#\$ }"
            colour="$U_CMD"
        fi
        pad=$(( inner - ${#text} ))
        (( pad >= 0 )) || pad=0
        printf '  %s%s%s %s%s%s%*s %s%s%s\n' "$U_WARN" "$G_V" "$U_R" \
            "$colour" "$text" "$U_R" "$pad" '' "$U_WARN" "$G_V" "$U_R"
    done
    printf '  %s%s%s%s%s\n' "$U_WARN" "$G_BL" "$(repeat "$G_H" $(( inner + 2 )))" "$G_BR" "$U_R"
}

summary_ok()   { printf '  %s%s%s %s\n' "$U_OK" "$G_OK" "$U_R" "$*"; }
summary_warn() { printf '  %s%s %s%s\n' "$U_WARN" "$G_WARN" "$*" "$U_R"; }
# summary_row <label> <value>
summary_row()  { printf '    %-14s %s\n' "$1" "$2"; }

# greeter_state <expected session binary>: prints none | ok | todo.
greeter_state() {
    pacman -Q kineticwe-greeter &>/dev/null || [[ -x "${1:-}" ]] || { echo none; return 0; }
    if [[ "$(greetd_default_command /etc/greetd/config.toml)" == "$1" ]]; then echo ok; else echo todo; fi
}

# summary_greeter <expected session binary> [setup command]
summary_greeter() {
    local setup="${2:-kineticwe-setup-greeter}"
    case "$(greeter_state "$1")" in
        ok)
            summary_ok "Greeter configured: log out or reboot to use it"
            ;;
        todo)
            summary_box \
                "ONE MORE STEP: set up the login screen" \
                "" \
                "\$ $setup" \
                "" \
                "Then reboot. Enabling greetd.service alone is not" \
                "enough: Arch's default greetd config is a text" \
                "login (agreety) that drops you into a shell."
            ;;
    esac
}

summary_qt() {
    local file="$1"
    [[ -f "$file" && ! -L "$file" ]] || return 0
    local built now
    built=$(<"$file")
    now=$(current_qt)
    if [[ -n "$now" && "$built" != "$now" ]]; then
        summary_warn "Qt changed since this build ($built -> $now): rebuild before starting the session."
    else
        summary_row "Built for" "$built  (rebuild after Qt updates)"
    fi
}

summary_leftovers() {
    if check_migration_leftovers; then
        summary_ok "No leftovers from older local builds"
        return 0
    fi
    summary_warn "Leftovers from older local builds (they override the packages):"
    local line
    for line in "${MIGRATION_LEFTOVERS[@]}"; do printf '    %s\n' "$line"; done
    printf '    See docs/arch-migration.md (old build-arch-native.sh installs) or README:\n'
    printf '    "Migrating from a local (~/.local) build to the packages".\n'
}

# ---------------------------------------------------------------------------
# Welcome screen + confirmation (before any build, sudo or change)
# ---------------------------------------------------------------------------
welcome_title() {
    echo
    summary_rule
    printf '  %sKineticWE for Arch: %s%s\n' "$U_B" "$1" "$U_R"
    summary_rule
}

# confirm_start <mode> <assume_yes> <do_greeter>
# Shows what the chosen mode is about to do and asks before continuing.
# Default answer: Y for builds, N for --local-dev (display manager) and
# --reset-local (deletes files). Without a terminal, --yes is required.
confirm_start() {
    local mode="$1" assume_yes="$2" do_greeter="$3" default=Y
    ui_init
    case "$mode" in
        pkg)
            welcome_title "install packages"
            summary_row "Version" "${SPEC_VERSION}-${SPEC_RELEASE} (from lgl-kineticwe-copr/kineticwe.spec)"
            summary_row "Source" "this checkout ($SCRIPT_DIR)"
            echo
            echo "  This will:"
            echo "    ${G_BULLET} build kineticwe, xdg-desktop-portal-kwe, kineticwe-noctalia"
            if [[ "$do_greeter" == true ]]; then
                echo "      and kineticwe-greeter with makepkg"
            else
                echo "      with makepkg (kineticwe-greeter is skipped: --no-greeter)"
            fi
            echo "    ${G_BULLET} install missing build dependencies and the packages"
            echo "      with pacman (asks for your sudo password)"
            echo
            echo "  A first build takes roughly 10-30 minutes."
            ;;
        binary|kwin-only|noct-only|local-dev)
            local what
            case "$mode" in
                binary)    what="compositor, portal and shell" ;;
                kwin-only) what="compositor only (incremental)" ;;
                noct-only) what="Noctalia shell only (incremental)" ;;
                local-dev) what="compositor, portal, shell + greeter setup" ;;
            esac
            welcome_title "development build"
            summary_row "Installs" "$what"
            summary_row "Into" "$TEST_PREFIX (overrides the packages)"
            echo
            summary_warn "For testing and development only, not for daily use."
            echo "    For daily use, install the packages: ./install-arch.sh"
            if [[ "$mode" == binary || "$mode" == local-dev ]]; then
                echo "    Adds a \"KineticWE-Dev\" login entry (asks for sudo)."
            fi
            echo "    Undo later with: ./install-arch.sh --reset-local"
            if [[ "$mode" == local-dev ]]; then
                default=N
                echo
                summary_box \
                    "This also changes your display manager:" \
                    "" \
                    "  - sets up greetd with the Noctalia greeter" \
                    "  - disables your current display manager" \
                    "  - edits /etc/greetd/config.toml (backup kept)" \
                    "" \
                    "Uses sudo. Reboot afterwards."
            fi
            ;;
        reset-local)
            default=N
            welcome_title "remove local dev build"
            echo "  This will remove:"
            echo "    ${G_BULLET} the dev build files this script recorded in $TEST_PREFIX"
            echo "    ${G_BULLET} the \"KineticWE-Dev\" login entry (asks for sudo)"
            echo
            echo "  Kept: your settings (~/.config/kineticwe, ~/.local/state/noctalia),"
            echo "  files you edited since, the packages and the greeter."
            ;;
        *)
            return 0
            ;;
    esac
    echo

    if [[ "$assume_yes" == true ]]; then
        return 0
    fi
    if [[ ! -t 0 ]]; then
        error "Not running in a terminal; re-run with --yes to proceed without confirmation."
        exit 1
    fi
    local prompt='[Y/n]' answer
    [[ "$default" == Y ]] || prompt='[y/N]'
    read -r -p "  Continue? $prompt " answer || answer=""
    answer="${answer:-$default}"
    case "${answer,,}" in
        y|yes) echo ;;
        *)
            echo
            info "Cancelled. Nothing was changed."
            exit 0
            ;;
    esac
}

# print_usage pkg | dev [with_greeter]
print_usage() {
    local mode="${1:-dev}" with_greeter="${2:-false}"
    ui_init

    if [[ "$mode" == pkg ]]; then
        local version
        version=$(pacman -Q kineticwe 2>/dev/null | awk '{print $2}')
        summary_title "KineticWE ${version:-$PKG_DEV_RELEASE} installed"
        # Package names in split order, two per row.
        local -a names=() rows=()
        local name f
        for name in "${KWE_PACKAGES[@]}"; do
            for f in "${PKG_INSTALLED[@]}"; do
                if [[ "${f##*/}" == "$name"-[0-9]* ]]; then names+=("$name"); break; fi
            done
        done
        local i
        for (( i = 0; i < ${#names[@]}; i += 2 )); do
            rows+=("${names[i]}${names[i+1]:+  ${names[i+1]}}")
        done
        summary_row "Packages" "${rows[0]:-}"
        for (( i = 1; i < ${#rows[@]}; i++ )); do summary_row "" "${rows[i]}"; done
        summary_qt "$PKG_WORK_DIR/qt-abi"
        if (( ${#PKGBUILD_DRIFT[@]} )); then
            summary_warn "PKGBUILD is behind the spec; built with the spec's values."
            printf '    Update it: %sarch/sync-from-spec.sh PKGBUILD%s\n' "$U_CMD" "$U_R"
        fi
        echo

        summary_greeter /usr/bin/noctalia-greeter-session
        echo

        printf '  %sStart KineticWE%s\n' "$U_B" "$U_R"
        summary_row "Login screen" "choose \"KineticWE\""
        summary_row "From a TTY" "/usr/bin/start-kineticwe"
        local resolved
        resolved=$(command -v start-kineticwe 2>/dev/null || true)
        if [[ -n "$resolved" && "$resolved" != /usr/bin/start-kineticwe ]]; then
            summary_warn "'start-kineticwe' in your PATH is a dev build: $resolved"
        fi
        echo

        summary_leftovers
        echo
        printf '  More options: %s./install-arch.sh --help%s\n' "$U_CMD" "$U_R"
    else
        summary_title "KineticWE dev build installed to $TEST_PREFIX"
        if [[ -n "${STATE_DIR:-}" ]]; then summary_qt "$STATE_DIR/qt-abi"; fi
        echo

        if [[ "$with_greeter" == true ]]; then
            if pacman -Q kineticwe-greeter &>/dev/null; then
                summary_greeter /usr/bin/noctalia-greeter-session
            else
                # Source-built greeter: no kineticwe-setup-greeter to point at.
                summary_greeter /usr/local/bin/noctalia-greeter-session "./install-arch.sh --local-dev"
            fi
            echo
        fi

        printf '  %sStart the dev build%s\n' "$U_B" "$U_R"
        if [[ -f "$SESSION_DIR/$SESSION_DESKTOP" ]]; then
            summary_row "Login screen" "choose \"KineticWE-Dev\""
        else
            summary_row "Login screen" "no KineticWE-Dev entry (skipped or sudo declined)"
        fi
        if [[ ":$PATH:" == *":$TEST_PREFIX/bin:"* ]]; then
            summary_row "From a TTY" "start-kineticwe"
        else
            summary_row "From a TTY" "$TEST_PREFIX/bin/start-kineticwe"
        fi
        echo
        printf '  Back to the packages: %s./install-arch.sh --reset-local%s\n' "$U_CMD" "$U_R"
        printf '  More options:         %s./install-arch.sh --help%s\n' "$U_CMD" "$U_R"
    fi
    echo
}

# ---------------------------------------------------------------------------
# Package mode: makepkg from the working tree, then pacman -U
# ---------------------------------------------------------------------------
pkg_build() {
    local install_greeter="$1"
    local work="$PKG_WORK_DIR" pkgver tool
    # The spec's Version, not the committed PKGBUILD's: the build copy is synced
    # to the spec, and the tarball prefix must match its pkgver.
    pkgver="$SPEC_VERSION"
    [[ -n "$pkgver" ]] || die "Cannot read Version from $SPEC_FILE"
    [[ "$SPEC_RELEASE" =~ ^[0-9]+$ ]] || die "Spec Release must be an integer for dev pkgrel numbering: $SPEC_RELEASE"
    [[ -f "$PKGBUILD_INSTALL_FILE" ]] || die "Missing $PKGBUILD_INSTALL_FILE (PKGBUILD install= file)"
    [[ -f "$PKGBUILD_GREETER_INSTALL_FILE" ]] || die "Missing $PKGBUILD_GREETER_INSTALL_FILE (kineticwe-greeter install= file)"

    install_missing "packaging tool" base-devel git
    for tool in makepkg git tar; do
        command -v "$tool" >/dev/null || die "Missing packaging tool: $tool"
    done

    mkdir -p -- "$work/pkgs"
    [[ ! -L "$work/.lock" ]] || die "Package build lock must not be a symlink"
    exec 8>"$work/.lock"
    flock -n 8 || die "Another package build is using $work"

    # Dev pkgrel: <spec release>.<N>, N one past the highest already built.
    # (makepkg only accepts pkgrel of the form integer[.integer].)
    local n=0 f base num
    local re="-${SPEC_RELEASE}\\.([0-9]+)-[^-]+\\.pkg\\.tar\\.zst$"
    for f in "$work/pkgs/"*.pkg.tar.zst; do
        [[ -f "$f" ]] || continue
        base="${f##*/}"
        if [[ "$base" =~ $re ]]; then
            num=$((10#${BASH_REMATCH[1]}))
            if (( num > n )); then n=$num; fi
        fi
    done
    PKG_DEV_RELEASE="${SPEC_RELEASE}.$((n + 1))"
    info "Package release: ${pkgver}-${PKG_DEV_RELEASE}"

    step "Creating source tarball from the working tree..."
    local tarball="$work/kineticwe-src.tar.gz"
    rm -f -- "$tarball.tmp"
    # Tracked + untracked-but-not-ignored files; tracked files deleted in the
    # working tree are skipped. Symlink targets are not rewritten (S flag).
    git -C "$SCRIPT_DIR" ls-files -z -co --exclude-standard |
        while IFS= read -r -d '' f; do
            if [[ -e "$SCRIPT_DIR/$f" || -L "$SCRIPT_DIR/$f" ]]; then printf '%s\0' "$f"; fi
        done |
        tar -C "$SCRIPT_DIR" --null --no-recursion -T - \
            --transform "s,^,kineticwe-$pkgver/,S" -czf "$tarball.tmp"
    mv -f -- "$tarball.tmp" "$tarball"

    cp -f -- "$PKGBUILD_FILE" "$work/PKGBUILD"
    # Build what the spec says even if the committed PKGBUILD lags behind.
    "$SCRIPT_DIR/arch/sync-from-spec.sh" "$work/PKGBUILD" >/dev/null \
        || die "Cannot sync $work/PKGBUILD with $SPEC_FILE"
    cp -f -- "$PKGBUILD_INSTALL_FILE" "$work/kineticwe.install"
    cp -f -- "$PKGBUILD_GREETER_INSTALL_FILE" "$work/kineticwe-greeter.install"
    sed -i "s/^pkgrel=.*/pkgrel=${PKG_DEV_RELEASE}/" "$work/PKGBUILD"
    grep -qx "pkgrel=${PKG_DEV_RELEASE}" "$work/PKGBUILD" || die "Could not set pkgrel in $work/PKGBUILD"

    # makepkg reuses a source file that already exists in SRCDEST instead of
    # fetching it again, so a cached kineticwe-$pkgver.tar.gz from an earlier
    # run would silently replace the fresh working-tree tarball. Pin SRCDEST
    # to $work (a makepkg.conf SRCDEST would move the cache elsewhere) and drop
    # the cached copy. The pinned KDE/greeter tarballs stay cached.
    rm -f -- "$work/kineticwe-$pkgver.tar.gz"

    step "Building packages with makepkg (installs missing makedepends via sudo)..."
    # -C: start from a clean srcdir so files deleted from the tree never linger.
    (cd "$work" && KWE_LOCAL_TARBALL="$tarball" SRCDEST="$work" PKGDEST="$work/pkgs" makepkg -sfC --noconfirm)

    local list
    list=$(cd "$work" && KWE_LOCAL_TARBALL="$tarball" PKGDEST="$work/pkgs" makepkg --packagelist) \
        || die "makepkg --packagelist failed"
    local -a built=()
    [[ -z "$list" ]] || mapfile -t built <<< "$list"
    PKG_INSTALLED=()
    for f in "${built[@]}"; do
        [[ -f "$f" ]] || continue
        base="${f##*/}"
        if [[ "$base" == *-debug-[0-9]* ]]; then continue; fi
        if [[ "$install_greeter" != true && "$base" == kineticwe-greeter-[0-9]* ]]; then continue; fi
        PKG_INSTALLED+=("$f")
    done
    (( ${#PKG_INSTALLED[@]} )) || die "makepkg produced no installable packages in $work/pkgs"

    step "Installing packages..."
    printf '    %s\n' "${PKG_INSTALLED[@]##*/}"
    sudo pacman -U --needed "${PKG_INSTALLED[@]}"

    record_qt_abi "$work/qt-abi"
    print_usage pkg
}

# ---------------------------------------------------------------------------
# Dev modes: incremental build into TEST_PREFIX
# ---------------------------------------------------------------------------
dev_build() {
    local mode="$1" do_session="$2" title
    case "$mode" in
        binary)    title="Binary Dev Build" ;;
        local-dev) title="Local Dev Build (binaries + Noctalia greeter)" ;;
        kwin-only) title="KWin-Only Dev Build" ;;
        noct-only) title="Noctalia-Only Dev Build" ;;
        *)         die "Unknown dev mode: $mode" ;;
    esac
    step "KineticWE ${title} into ${TEST_PREFIX}"

    check_bootstrap
    check_pkgbuild_drift
    prepare_prefix
    check_qt_abi "$STATE_DIR/qt-abi"
    install_build_deps
    check_prereqs

    case "$mode" in
        noct-only)
            # Shell-only: build requirements only; the compositor was not
            # rebuilt, so the Qt ABI record is left untouched.
            noctalia_only_build
            return 0
            ;;
        kwin-only)
            kwin_only_build
            # Effect QML modules (Overview/Grid View, Window View, Tiles
            # Editor) are hard runtime requirements that ldd cannot see, so
            # install and verify them even on an incremental kwin-only build.
            install_host_deps_arch false
            verify_host_deps
            record_qt_abi "$STATE_DIR/qt-abi"
            return 0
            ;;
    esac

    local with_greeter=false
    [[ "$mode" != local-dev ]] || with_greeter=true
    binary_build
    install_host_deps_arch "$with_greeter"
    verify_host_deps
    record_qt_abi "$STATE_DIR/qt-abi"
    install_startup_script
    if [[ "$do_session" == true ]]; then
        install_dev_session_entry
    fi
    if [[ "$with_greeter" == true ]]; then
        # Hard-fail: a broken greeter must abort --local-dev instead of leaving
        # the session without a login screen.
        setup_noctalia_greeter
    fi
    print_usage dev "$with_greeter"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $0 [MODE] [MODIFIERS]

Modes (at most one; default --pkg):
  --pkg                Build pacman packages from the working tree with PKGBUILD
                       (makepkg -s) in build.arch-pkg/, then install with pacman -U.
  --binary             Dev build: compositor + portal + shell into TEST_PREFIX
                       (default \$HOME/.local), incremental.
  --local-dev          --binary plus build/activate the Noctalia greeter (greetd,
                       /usr/local, or the installed kineticwe-greeter package).
  --kwin-only          Dev build: compositor only (incremental).
  --noct-only          Dev build: Noctalia shell only (incremental).
  --reset-local        Remove only unchanged manifest-owned files from TEST_PREFIX;
                       sudo is used only for the recorded system login entry.
  --clean-deps         Report build/runtime dependencies (never removes packages).
  -h, --help           Show this help.
  --local-dev together with --binary is accepted and means --local-dev.

Modifiers:
  --no-session         Dev modes: do not install the KineticWE-Dev login-screen entry.
  --no-greeter         --pkg only: do not install the kineticwe-greeter package.
  --yes, -y            Skip the confirmation prompt (required when not run
                       from a terminal).
  --install-host-deps  Deprecated alias for --binary.
  --full               Deprecated alias for --local-dev.

Dependencies come from PKGBUILD (makedepends; depends of the split packages).
PKGBUILD must match lgl-kineticwe-copr/kineticwe.spec (version, release,
pinned commits) or the build stops. Run as your normal user.

Environment: TEST_PREFIX, NOCTALIA_GREETER_REPO, NOCTALIA_GREETER_REF.
EOF
}

main() {
    local do_session=true do_greeter=true assume_yes=false arg
    local -a modes=()

    for arg in "$@"; do
        case "$arg" in
            --pkg)         modes+=(pkg) ;;
            --binary)      modes+=(binary) ;;
            --local-dev)   modes+=(local-dev) ;;
            --full)
                warn "--full is deprecated; use --local-dev."
                modes+=(local-dev)
                ;;
            --kwin-only)   modes+=(kwin-only) ;;
            --noct-only)   modes+=(noct-only) ;;
            --reset-local) modes+=(reset-local) ;;
            --clean-deps)  modes+=(clean-deps) ;;
            --install-host-deps)
                warn "--install-host-deps is deprecated; use --binary."
                modes+=(binary)
                ;;
            --no-session)  do_session=false ;;
            --no-greeter)  do_greeter=false ;;
            --yes|-y)      assume_yes=true ;;
            --help|-h)     usage; exit 0 ;;
            *)
                error "Unknown option: $arg"
                usage >&2
                exit 1
                ;;
        esac
    done

    if (( ${#modes[@]} )); then
        mapfile -t modes < <(printf '%s\n' "${modes[@]}" | sort -u)
    fi
    if (( ${#modes[@]} == 2 )) && [[ " ${modes[*]} " == *" binary "* && " ${modes[*]} " == *" local-dev "* ]]; then
        modes=(local-dev)
    fi
    if (( ${#modes[@]} > 1 )); then
        error "Only one mode may be given (got: ${modes[*]})"
        usage >&2
        exit 1
    fi
    local mode="${modes[0]:-pkg}"

    if [[ "$do_greeter" != true && "$mode" != pkg ]]; then
        die "--no-greeter only applies to --pkg"
    fi

    confirm_start "$mode" "$assume_yes" "$do_greeter"

    case "$mode" in
        reset-local)
            reset_local
            ;;
        clean-deps)
            echo "=========================================="
            echo "  KineticWE Arch - Dependency Report"
            echo "=========================================="
            echo
            clean_build_deps
            ;;
        pkg)
            [[ "$do_session" == true ]] || warn "--no-session has no effect in --pkg mode."
            check_bootstrap
            check_pkgbuild_drift
            pkg_build "$do_greeter"
            ;;
        *)
            dev_build "$mode" "$do_session"
            ;;
    esac
}

main "$@"
