#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Install Host Runtime Dependencies
# =============================================================================
# The dev build installs into TEST_PREFIX without RPM/DPKG dependency
# resolution, so a fresh host may be missing the shared libraries the
# KineticWE binaries link against. That only surfaces at login as a cryptic
# "library not found" loader error.
#
# This script inspects the actual installed binaries and installs exactly the
# runtime libraries the host is missing. It is the engine behind
#   ./build-rpm-dev.sh --install-host-deps
# and can also be run directly after a build to top up deps:
#   ./install-runtime-deps.sh
#
# On a fresh minimal host (an install from TTY with no desktop packages) the
# KineticWE session would otherwise come up with no Breeze icon/cursor theme,
# no Qt theming, no X server (Xwayland), a crashing portal backend, and no
# terminal. So in addition to the library resolution below it also installs
# the default session packages: the Breeze icon theme, the Breeze color
# schemes, the Breeze cursor theme, the Qt5/Qt6 configuration tools
# (qt5ct / qt6ct), Xwayland, the portal's QML runtime modules, the PipeWire
# media stack (pipewire/wireplumber/pipewire-pulse — needed for screen capture
# and audio), and Alacritty.
# These installs are idempotent — any package the host already has is skipped.
#
# How it works:
#   1. Detect the host package manager (dnf / apt).
#   2. ldd the dev binaries (kinetic-we, noctalia-kwe, xdg-desktop-portal-kwe)
#      with the dev LD_LIBRARY_PATH and collect any "not found" libraries.
#   3. Map each missing library to the package that provides it:
#        - dnf: dnf repoquery --whatprovides <lib>
#        - apt: apt-file search <lib>
#   4. Install the resolved packages (sudo may prompt).
#   5. Re-ldd; if anything is still missing, exit 1 with the list.
#
# If no dev binaries are installed yet (fresh clone before a build), it falls
# back to the verified baseline runtime package set for Fedora. On Debian it
# instead asks you to build first so auto-detection can run.
#
# Environment variables:
#   TEST_PREFIX  - Where the dev build is installed (default: $HOME/.local)
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local}"
DEV_LD_LIBRARY_PATH="${TEST_PREFIX}/lib64:${TEST_PREFIX}/lib/x86_64-linux-gnu"
# Repository root, so the vendored icon theme archive can be located.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# The dev binaries to inspect. The portal backend is a separate subpackage and
# may not be present on a partial build; we skip binaries that are not there.
DEV_BINARIES=(
    "${TEST_PREFIX}/bin/kinetic-we"
    "${TEST_PREFIX}/bin/noctalia-kwe"
    "${TEST_PREFIX}/libexec/xdg-desktop-portal-kwe"
    # Noctalia greeter (--full mode only). The client needs EGL/GLES and the
    # Cairo/Pango stack; the compositor links wlroots and libinput.
    "${TEST_PREFIX}/bin/noctalia-greeter"
    "${TEST_PREFIX}/bin/noctalia-greeter-compositor"
)

# Baseline runtime packages verified to be needed on a fresh Fedora host.
# Used only when no dev binaries are present yet (pre-build). After a build,
# the ldd-based detection above fills in anything beyond this list.
BASELINE_RPM_PKGS=(
    kf6-kstatusnotifieritem
    kwayland
    kf6-kglobalaccel
    kf6-kio-core
    kf6-kio-gui
    kf6-kio-widget
    kf6-kio-file-widgets
    kf6-kjobwidgets
    kf6-kcompletion
    kf6-kitemviews
    kf6-solid
    kf6-kbookmark
    kf6-kservice
    kf6-kcrash
)

# Default session packages so a fresh install from TTY actually has: the
# Breeze icon theme (breeze-icon-theme), the Breeze color schemes that the
# session seeds (BreezeDark/BreezeLight — plasma-breeze-common), the Breeze
# cursor theme, the Qt5/Qt6 configuration tools that theme Qt apps via
# QT_QPA_PLATFORMTHEME=qt5ct, Xwayland (so X11 clients and Electron apps can
# open windows), and a terminal the user can launch. The window decoration is
# the first-party org.kineticwe.decoration plugin built with KineticWE, so
# neither plasma-breeze nor any other window-decoration engine is needed.
# Installed idempotently by
# install_default_session_packages.
DEFAULT_RPM_PKGS=(
    breeze-icon-theme
    plasma-breeze-common
    breeze-cursor-theme
    qt5ct
    qt6ct
    xorg-x11-server-Xwayland
    alacritty
)

# QML runtime modules the xdg-desktop-portal-kwe backend loads from its
# dialogs. Unlike linked libraries these are resolved at runtime by the QML
# engine, so ldd cannot see them; install them explicitly so the portal's
# dialogs (screen chooser, file dialog, user-info, ...) never crash on a
# fresh host with a missing QML module.
PORTAL_QML_PKGS=(
    kf6-kirigami
    kf6-kitemmodels
    kf6-ki18n
    kpipewire
    kf6-kiconthemes
    kf6-kcmutils
    kf6-kdeclarative
)

# QML runtime modules the compositor's built-in effects load (Overview/Grid
# View, Window View, Tiles Editor). Resolved by the QML engine at runtime,
# so ldd cannot see them; install them explicitly so grid view, overview,
# window view and the tiling editor work on a fresh host. These are hard
# requirements: without them the effect's QML fails to load and the overlay
# comes up blank/black.
KWIN_QML_PKGS=(
    plasma-milou          # org.kde.milou (Overview/Grid View search results)
    libplasma             # org.kde.plasma.components / extras / core
    qt6-qt5compat         # Qt5Compat.GraphicalEffects (FastBlur)
    kf6-kirigami          # org.kde.kirigami (Overview, Window Heap, Tiles Editor)
    kf6-ksvg              # org.kde.ksvg (WindowHeapDelegate, Tiles Editor, OSD)
    kf6-kcmutils          # org.kde.kcmutils (Overview search UI)
    kf6-kitemmodels       # org.kde.kitemmodels (Tiles Editor)
)

# QML modules the built-in effects import, as paths relative to a Qt QML
# import root. ldd cannot see QML modules, so verify_qml_modules() checks
# these by path and reports the Fedora package that provides each one.
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

# PipeWire media stack (screen capture + audio). The compositor's screencast
# plugin and the portal backend need a live PipeWire daemon and a session
# manager (wireplumber) to create and route the capture node. This list is for
# a fresh Fedora host: unlike its dev machine (where these were installed and
# enabled long ago) a cold box has only kpipewire (the Qt/DBus glue) and no
# daemon/session-manager, so screensharing fails. Installed idempotently below.
MEDIA_RPM_PKGS=(
    pipewire
    wireplumber
    pipewire-pulseaudio
)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

PM=""

detect_pkgmgr() {
    if command -v dnf &>/dev/null; then
        PM=dnf
    elif command -v apt-get &>/dev/null; then
        PM=apt
    else
        error "No supported package manager found (need dnf or apt-get)."
        exit 1
    fi
    info "Package manager: ${PM}"
}

# Collect the "not found" libraries for every installed dev binary into the
# global array missing_libs.
missing_libs=()
collect_missing() {
    missing_libs=()
    local binary out lib
    for binary in "${DEV_BINARIES[@]}"; do
        [[ -x "$binary" ]] || continue
        out=$(LD_LIBRARY_PATH="$DEV_LD_LIBRARY_PATH" ldd "$binary" 2>/dev/null \
              | awk '/not found/ {print $1}' || true)
        [[ -z "$out" ]] && continue
        while IFS= read -r lib; do
            [[ -n "$lib" ]] && missing_libs+=("$lib")
        done <<< "$out"
    done
}

# Map each unique missing library to its providing package, then install the
# resolved set.
resolve_and_install() {
    local -a pkgs=()
    local lib pkg
    while IFS= read -r lib; do
        [[ -z "$lib" ]] && continue
        if [[ "$PM" == dnf ]]; then
            pkg=$(dnf repoquery --whatprovides "$lib" --queryformat '%{name}' 2>/dev/null \
                  | sort -u | head -1)
        else
            if ! command -v apt-file &>/dev/null; then
                warn "    $lib -> apt-file not installed; cannot auto-map."
                warn "            Install apt-file and update its index, then re-run."
                continue
            fi
            pkg=$(apt-file search "$lib" 2>/dev/null | head -1 | awk -F: '{print $1}')
        fi
        if [[ -n "$pkg" ]]; then
            info "    $lib -> $pkg"
            pkgs+=("$pkg")
        else
            warn "    $lib -> (no package found)"
        fi
    done < <(printf '%s\n' "${missing_libs[@]}" | sort -u)

    if [[ ${#pkgs[@]} -eq 0 ]]; then
        warn "No packages resolved for the missing libraries."
        return 1
    fi

    echo
    info "Installing: ${pkgs[*]}"
    if [[ "$PM" == dnf ]]; then
        sudo dnf install -y "${pkgs[@]}"
    else
        sudo apt-get install -y "${pkgs[@]}"
    fi
}

verify_resolved() {
    collect_missing
    [[ ${#missing_libs[@]} -eq 0 ]]
}

# Map a QML module path to the Fedora package that ships it (used only for the
# error message; on Debian the module path is still accurate).
qml_module_package() {
    case "$1" in
        org/kde/milou)               echo plasma-milou ;;
        org/kde/plasma/*)            echo libplasma ;;
        Qt5Compat/GraphicalEffects)  echo qt6-qt5compat ;;
        org/kde/kirigami)            echo kf6-kirigami ;;
        org/kde/ksvg)                echo kf6-ksvg ;;
        org/kde/kcmutils)            echo kf6-kcmutils ;;
        org/kde/kitemmodels)         echo kf6-kitemmodels ;;
        *)                           echo "$1" ;;
    esac
}

# QML import roots searched on the host (dev prefix first, then the system Qt).
qml_import_roots() {
    local -a roots=(
        "${TEST_PREFIX}/lib64/qt6/qml"
        "${TEST_PREFIX}/lib/qt6/qml"
        /usr/lib64/qt6/qml
        /usr/lib/qt6/qml
        /usr/lib/x86_64-linux-gnu/qt6/qml
        /usr/local/lib64/qt6/qml
    )
    local root
    for root in "${roots[@]}"; do
        [[ -d "$root" ]] && printf '%s\n' "$root"
    done
}

# Hard gate: every QML module the compositor's built-in effects import must be
# present. A missing module never fails ldd; it only shows up at runtime as a
# blank Overview/Grid View, so check it explicitly and fail loudly.
verify_qml_modules() {
    local -a roots=()
    mapfile -t roots < <(qml_import_roots)
    if [[ ${#roots[@]} -eq 0 ]]; then
        warn "No Qt QML import roots found; cannot verify effect QML modules."
        return 0
    fi

    local -a missing_modules=()
    local -a missing_pkgs=()
    local module root found pkg
    for module in "${EFFECT_QML_MODULES[@]}"; do
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

    error "Missing QML modules required by Overview/Grid View (and other effects):"
    local i
    for i in "${!missing_modules[@]}"; do
        error "    ${missing_modules[$i]} (${missing_pkgs[$i]})"
    done
    if [[ "$PM" == dnf ]]; then
        local -a unique_pkgs
        mapfile -t unique_pkgs < <(printf '%s\n' "${missing_pkgs[@]}" | sort -u)
        error "Install with: sudo dnf install ${unique_pkgs[*]}"
    else
        error "Install the packages providing the modules above."
    fi
    return 1
}

# Install the default session packages (Breeze icon theme, Breeze cursor theme,
# Qt5/Qt6 configuration tools, Xwayland, portal and compositor QML modules,
# Alacritty) on a fresh host. Skips anything already installed, so re-runs are
# no-ops. This is what makes a fresh-from-TTY install come up with icons, a
# cursor theme, Qt theming, working grid view/overview/window view/tiling
# editor effects, a working X server and a terminal instead of a bare desktop.
install_default_session_packages() {
    local -a missing=()
    local pkg
    for pkg in "${DEFAULT_RPM_PKGS[@]}" "${PORTAL_QML_PKGS[@]}" "${KWIN_QML_PKGS[@]}" "${MEDIA_RPM_PKGS[@]}"; do
        if [[ "$PM" == dnf ]]; then
            if ! rpm -q "$pkg" &>/dev/null; then
                missing+=("$pkg")
            fi
        elif ! dpkg-query -W "$pkg" &>/dev/null 2>&1; then
            missing+=("$pkg")
        fi
    done
    if [[ ${#missing[@]} -eq 0 ]]; then
        info "Default session packages already installed."
        return 0
    fi
    # Some packages appear in more than one list (e.g. kf6-kirigami is needed by
    # both the portal dialogs and the compositor effects); de-duplicate them so
    # the install command and its output stay clean.
    local -a unique_missing
    mapfile -t unique_missing < <(printf '%s\n' "${missing[@]}" | sort -u)
    echo
    info "Fresh host: installing default session packages (Breeze icons, cursor theme, Qt tools, Xwayland, portal/compositor QML modules, terminal):"
    printf '    %s\n' "${unique_missing[@]}"
    if [[ "$PM" == dnf ]]; then
        sudo dnf install -y "${unique_missing[@]}"
    else
        sudo apt-get install -y "${unique_missing[@]}"
    fi
    info "Default session packages installed."
}

# The default icon theme (Breeze-Round-Chameleon Dark Icons) is vendored in the
# repo, not a Fedora/COPR package, so it cannot be pulled in with dnf. Install
# it from the vendored archive into the user's icon dir when it is not already
# resolvable. The dev `cmake --install` already puts it in
# ${TEST_PREFIX}/share/icons (=$HOME/.local/share/icons for the default
# prefix), so this is a no-op after a build. Idempotent.
install_default_icon_theme() {
    local theme_name="Breeze-Round-Chameleon Dark Icons"
    local archive="${SCRIPT_DIR}/data/icon-themes/Breeze-Round-Chameleon-Dark-Icons.tar.xz"
    local -a roots=(
        "${TEST_PREFIX}/share/icons"
        "${HOME}/.local/share/icons"
        "${HOME}/.icons"
        /usr/share/icons
    )
    local root
    for root in "${roots[@]}"; do
        if [[ -f "${root}/${theme_name}/index.theme" ]]; then
            info "Default icon theme '${theme_name}' already installed (${root})."
            return 0
        fi
    done
    if [[ ! -f "$archive" ]]; then
        warn "Vendored icon theme archive not found: ${archive}"
        warn "        (run from a checkout of the repo, or run a build first)"
        return 1
    fi
    local dest="${TEST_PREFIX}/share/icons"
    info "Installing default icon theme '${theme_name}' -> ${dest}"
    mkdir -p "$dest"
    tar xJf "$archive" -C "$dest"
}

main() {
    detect_pkgmgr

    install_default_session_packages
    install_default_icon_theme || true

    # Hard gate: the effect QML modules must resolve even though ldd cannot
    # see them. install_default_session_packages() installs KWIN_QML_PKGS on
    # Fedora; a miss here means the session would come up with a blank
    # Overview/Grid View, so fail now with the exact command instead.
    if ! verify_qml_modules; then
        exit 1
    fi

    local has_binaries=false binary
    for binary in "${DEV_BINARIES[@]}"; do
        if [[ -x "$binary" ]]; then
            has_binaries=true
            break
        fi
    done

    if ! $has_binaries; then
        if [[ "$PM" == dnf ]]; then
            echo
            info "No dev binaries found in ${TEST_PREFIX}; installing baseline runtime deps..."
            sudo dnf install -y "${BASELINE_RPM_PKGS[@]}"
            echo
            info "Baseline runtime deps installed. After running a build, re-run this"
            info "script to auto-detect and install anything beyond this list."
            return 0
        fi
        warn "No KineticWE dev binaries found in ${TEST_PREFIX}; cannot auto-detect deps."
        warn "Run a build first (e.g. ./build-rpm-dev.sh --quick), then re-run this script."
        exit 1
    fi

    collect_missing
    if [[ ${#missing_libs[@]} -eq 0 ]]; then
        step "All runtime libraries resolve on the host."
        return 0
    fi

    echo
    info "Missing runtime libraries (for dev build in ${TEST_PREFIX}):"
    printf '    %s\n' "${missing_libs[@]}" | sort -u
    echo

    resolve_and_install

    echo
    step "Re-verifying..."
    if ! verify_resolved; then
        error "Some runtime libraries are still missing:"
        printf '    %s\n' "${missing_libs[@]}" | sort -u
        error "Investigate with: dnf repoquery --whatprovides '<lib>' (Fedora)"
        error "               or: apt-file search '<lib>' (Debian)"
        exit 1
    fi

    info "All runtime libraries now resolve on the host."
}

main "$@"
