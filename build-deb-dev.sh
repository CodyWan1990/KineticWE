#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Debian Dev Build & Test Script
# =============================================================================
# Builds KineticWE from local source using a Debian sid distrobox and installs
# it to a local testing prefix so you can run it from a TTY immediately.
#
# Usage:
#   ./build-deb-dev.sh                          # Quick cmake build (default)
#   ./build-deb-dev.sh --quick                  # Same as default (alias)
#   ./build-deb-dev.sh --deb                    # Build .deb packages
#   ./build-deb-dev.sh --deb --no-extract       # Build .debs only, don't install
#   ./build-deb-dev.sh --no-session             # Skip login-screen session entry
#   ./build-deb-dev.sh --reset-local            # Remove dev files from TEST_PREFIX
#
# All build modes install into TEST_PREFIX so you can test with a single command.
#
# Environment variables:
#   TEST_PREFIX     - Where to install for TTY testing (default: $HOME/.local)
#   DISTROBOX_NAME  - Name of the distrobox container (default: kineticwe-deb-build)
#   DEBIAN_IMAGE    - Debian image for distrobox (default: debian:sid)
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local}"
DISTROBOX_NAME="${DISTROBOX_NAME:-kineticwe-deb-build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPEC_DIR="${SCRIPT_DIR}/lgl-kineticwe-copr"
RESULTS_DIR="${SPEC_DIR}/results"
DISTROBOX_IMAGE="${DEBIAN_IMAGE:-debian:sid}"

SESSION_DIR="/usr/local/share/wayland-sessions"
SESSION_DESKTOP="kineticwe-dev.desktop"

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
# Parse spec metadata (same source of truth as the Fedora build)
# ---------------------------------------------------------------------------
SPEC_FILE="${SPEC_DIR}/kineticwe.spec"
SPEC_VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC_FILE")
SPEC_RELEASE=$(awk '/^Release:/ {print $2; exit}' "$SPEC_FILE" | sed 's/%{?dist}//')
PACKAGE=$(awk '/^Name:/ {print $2; exit}' "$SPEC_FILE")

# Pinned KDE-source refs — the spec is the single source of truth.
SPEC_KGA_REF=$(sed -n 's/.*%global kglobalacceld_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
SPEC_KDECO_REF=$(sed -n 's/.*%global kdecoration_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
SPEC_KCONFIG_REF=$(sed -n 's/.*%global kconfig_compiler_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)

NEXT_DEV=1
if [[ -d "$RESULTS_DIR" ]]; then
    EXISTING_DEVS=$(find "$RESULTS_DIR" -type f -name "*.deb" -printf '%f\n' 2>/dev/null | grep -oP "\.dev\d+" | grep -oP "\d+" || true)
    if [[ -n "$EXISTING_DEVS" ]]; then
        NEXT_DEV=$(( $(echo "$EXISTING_DEVS" | sort -n | tail -1) + 1 ))
    fi
fi
DEV_RELEASE="${SPEC_RELEASE}.dev${NEXT_DEV}"

ARCH=$(uname -m)

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
check_prereqs() {
    step "Checking prerequisites..."
    if ! command -v distrobox &>/dev/null; then
        error "distrobox is required but not found."
        exit 1
    fi
    if ! command -v podman &>/dev/null && ! command -v docker &>/dev/null; then
        error "podman or docker is required but not found."
        exit 1
    fi
    if [[ ! -f "$SPEC_FILE" ]]; then
        error "Could not find spec file: $SPEC_FILE"
        exit 1
    fi
    info "All prerequisites met."
    info "Package: ${PACKAGE} ${SPEC_VERSION}-${DEV_RELEASE}"
}

# ---------------------------------------------------------------------------
# Distrobox management
# ---------------------------------------------------------------------------
setup_distrobox() {
    step "Setting up distrobox container '${DISTROBOX_NAME}'..."
    if distrobox list 2>/dev/null | grep -w "$DISTROBOX_NAME" > /dev/null; then
        info "Container '${DISTROBOX_NAME}' already exists."
    else
        info "Creating distrobox from ${DISTROBOX_IMAGE}..."
        # Debian sid (systemd >= 261) runs `systemd-tmpfiles --create` in its
        # postinst, which chowns the host's bind-mounted /tmp and fails inside a
        # rootless podman container (fchownat: Operation not permitted). That
        # aborts distrobox's "Installing basic packages" step (Debian bug
        # #1140336). Divert `systemd-tmpfiles` to a no-op so the postinst
        # completes; the real binary is preserved at .../systemd-tmpfiles.distrib.
        distrobox create \
            --name "$DISTROBOX_NAME" \
            --image "$DISTROBOX_IMAGE" \
            --additional-flags "--userns=keep-id" \
            --pre-init-hooks "mkdir -p /usr/bin && printf '#!/bin/sh\nexit 0\n' > /usr/bin/systemd-tmpfiles && chmod 0755 /usr/bin/systemd-tmpfiles && dpkg-divert --local --add /usr/bin/systemd-tmpfiles" \
            --yes
        info "Container created."
    fi

    step "Installing build dependencies (one-time, cached across builds)..."
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        export DEBIAN_FRONTEND=noninteractive
        sudo ./debian-deps.sh
    "
    info "Build environment ready."
}

# ---------------------------------------------------------------------------
# Build .deb packages inside the distrobox
# ---------------------------------------------------------------------------
build_debs() {
    step "Building .deb packages inside distrobox (Release: ${DEV_RELEASE})..."
    mkdir -p "$RESULTS_DIR"

    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        set -euo pipefail
        cd '$SCRIPT_DIR'

        export DEB_VERSION='${SPEC_VERSION}-${DEV_RELEASE}'

        # Build kglobalacceld from pinned commit
        echo '[STEP] Building kglobalacceld...'
        KGA_BUILD=\$(mktemp -d /tmp/kga-build.XXXXXX)
        curl -fsSL https://invent.kde.org/plasma/kglobalacceld/-/archive/${SPEC_KGA_REF}/kglobalacceld-${SPEC_KGA_REF}.tar.gz \
            | tar xz -C \"\$KGA_BUILD\" --strip-components=1
        cd \"\$KGA_BUILD\"
        cmake -B build -S . \
            -DCMAKE_INSTALL_PREFIX=/usr \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_TESTING=OFF
        cmake --build build --parallel \$(nproc)
        sudo cmake --install build
        cd '$SCRIPT_DIR'
        rm -rf \"\$KGA_BUILD\"

        # Build kdecoration from pinned commit
        echo '[STEP] Building kdecoration...'
        KDECO_BUILD=\$(mktemp -d /tmp/kdeco-build.XXXXXX)
        curl -fsSL https://invent.kde.org/plasma/kdecoration/-/archive/${SPEC_KDECO_REF}/kdecoration-${SPEC_KDECO_REF}.tar.gz \
            | tar xz -C \"\$KDECO_BUILD\" --strip-components=1
        cmake -B \"\$KDECO_BUILD/build\" -S \"\$KDECO_BUILD\" \
            -DCMAKE_INSTALL_PREFIX=/usr \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_TESTING=OFF
        cmake --build \"\$KDECO_BUILD/build\" --parallel \$(nproc)
        sudo cmake --install \"\$KDECO_BUILD/build\"
        rm -rf \"\$KDECO_BUILD\"

        # Build the .deb packages using dpkg-buildpackage
        echo '[STEP] Building .deb packages...'
        if [[ ! -f debian/control ]]; then
            echo 'ERROR: debian/control not found.' >&2
            exit 1
        fi

        dch -v \"${DEB_VERSION}\" --distribution unstable 'Dev build' 2>/dev/null || true

        dpkg-buildpackage -us -uc -b
    "

    # Copy .deb files to results dir
    local deb_count=0
    while IFS= read -r -d '' deb_file; do
        cp "$deb_file" "$RESULTS_DIR/"
        deb_count=$((deb_count + 1))
    done < <(find "${SCRIPT_DIR}/.." -maxdepth 1 -type f -name "${PACKAGE}*.deb" -print0 2>/dev/null)

    find "${SCRIPT_DIR}/.." -maxdepth 1 -type f \( -name "*.changes" -o -name "*.buildinfo" \) -exec cp {} "$RESULTS_DIR/" \; 2>/dev/null || true

    if [[ "$deb_count" -eq 0 ]]; then
        error "No .deb packages were produced!"
        exit 1
    fi

    echo
    info "Binary .deb packages produced:"
    find "$RESULTS_DIR" -type f -name "${PACKAGE}*.deb" -exec ls -lh {} \;
    echo
}

# ---------------------------------------------------------------------------
# Quick build: cmake directly inside the distrobox (no .deb)
# ---------------------------------------------------------------------------
quick_build() {
    local build_dir="${SCRIPT_DIR}/build.deb-quick"

    step "Quick-building KineticWE in distrobox (prefix: ${TEST_PREFIX})..."

    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        set -euo pipefail

        unset NIX_REMOTE
        unset NIX_PATH
        unset NIX_SSL_CERT_FILE
        unset NIX_PROFILE
        unset LD_LIBRARY_PATH
        unset XDG_DATA_DIRS
        unset PKG_CONFIG_PATH
        export PATH='/usr/local/bin:/usr/bin:/bin'

        cd '$SCRIPT_DIR'

        # Build kglobalacceld if not already installed.
        if [[ ! -d '${TEST_PREFIX}/lib/cmake/KGlobalAccelD' \
              && ! -d '${TEST_PREFIX}/lib/${ARCH}-linux-gnu/cmake/KGlobalAccelD' ]]; then
            echo '[STEP] Building kglobalacceld first...'
            KGA_BUILD=\$(mktemp -d /tmp/kga-build.XXXXXX)
            curl -fsSL https://invent.kde.org/plasma/kglobalacceld/-/archive/${SPEC_KGA_REF}/kglobalacceld-${SPEC_KGA_REF}.tar.gz \
                | tar xz -C \"\$KGA_BUILD\" --strip-components=1
            cd \"\$KGA_BUILD\"
            cmake -B build -S . \\
                -DCMAKE_INSTALL_PREFIX='${TEST_PREFIX}' \\
                -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
                -DBUILD_TESTING=OFF
            cmake --build build --parallel \$(nproc)
            cmake --install build
            cd '$SCRIPT_DIR'
            rm -rf \"\$KGA_BUILD\"
        fi

        # Build kdecoration if not already installed.
        if [[ ! -d '${TEST_PREFIX}/lib/cmake/KDecoration3' \
              && ! -d '${TEST_PREFIX}/lib/${ARCH}-linux-gnu/cmake/KDecoration3' ]]; then
            echo '[STEP] Building kdecoration (pinned master for KDecoration3::Style)...'
            KDECO_BUILD=\$(mktemp -d /tmp/kdeco-build.XXXXXX)
            curl -fsSL https://invent.kde.org/plasma/kdecoration/-/archive/${SPEC_KDECO_REF}/kdecoration-${SPEC_KDECO_REF}.tar.gz \
                | tar xz -C \"\$KDECO_BUILD\" --strip-components=1
            cmake -B \"\$KDECO_BUILD/build\" -S \"\$KDECO_BUILD\" \\
                -DCMAKE_INSTALL_PREFIX='${TEST_PREFIX}' \\
                -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
                -DBUILD_TESTING=OFF
            cmake --build \"\$KDECO_BUILD/build\" --parallel \$(nproc)
            cmake --install \"\$KDECO_BUILD/build\"
            rm -rf \"\$KDECO_BUILD\"
        fi

        # kconfig_compiler from KConfig < 6.30 generates uncompilable setters
        KCFG_COMPILER_OVERRIDE=""
        KCFG_TEST=\$(mktemp -d /tmp/kcfg-test.XXXXXX)
        /usr/libexec/kf6/kconfig_compiler_kf6 src/rulesettings.kcfg src/rulesettings.kcfgc -d \"\$KCFG_TEST\" 2>/dev/null || true
        if ! grep -q 'mDecorationpolicy = int(v)' \"\$KCFG_TEST/rulesettings.h\" 2>/dev/null; then
            echo '[STEP] System kconfig_compiler is broken (KConfig <6.30); building fixed one...'
            KCFG_FIX=\$(mktemp -d /tmp/kconfig-fix.XXXXXX)
            mkdir -p \"\$KCFG_FIX/src\"
            curl -fsSL https://invent.kde.org/frameworks/kconfig/-/archive/${SPEC_KCONFIG_REF}/kconfig-${SPEC_KCONFIG_REF}.tar.gz \
                | tar xz -C \"\$KCFG_FIX/src\" --strip-components=1
            cmake -B \"\$KCFG_FIX/build\" -S \"\$KCFG_FIX/src\" \\
                -DCMAKE_BUILD_TYPE=Release \\
                -DBUILD_TESTING=OFF
            cmake --build \"\$KCFG_FIX/build\" --target kconfig_compiler --parallel \$(nproc)
            KCFG_COMPILER_OVERRIDE=\"\$KCFG_FIX/build/bin/kconfig_compiler_kf6\"
        fi
        rm -rf \"\$KCFG_TEST\"

        echo '[STEP] Configuring KineticWE...'
        rm -rf '$build_dir'
        cmake -B '$build_dir' -S . \\
            -DCMAKE_INSTALL_PREFIX='${TEST_PREFIX}' \\
            -DCMAKE_PREFIX_PATH='${TEST_PREFIX}' \\
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
            -DBUILD_TESTING=OFF \\
            -DKWIN_KCONFIG_COMPILER=\$KCFG_COMPILER_OVERRIDE \\
            -DKWIN_BUILD_GLOBALSHORTCUTS=ON

        echo '[STEP] Building KineticWE...'
        cmake --build '$build_dir' --parallel \$(nproc)

        echo '[STEP] Installing to ${TEST_PREFIX}...'
        cmake --install '$build_dir'

        echo '[STEP] Building xdg-desktop-portal-kwe...'
        PORTAL_BUILD=\$(mktemp -d /tmp/portal-build.XXXXXX)
        cmake -B \"\$PORTAL_BUILD\" -S 'portal/xdg-desktop-portal-kwe' \\
            -DCMAKE_INSTALL_PREFIX='${TEST_PREFIX}' \\
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
            -DBUILD_TESTING=OFF
        cmake --build \"\$PORTAL_BUILD\" --parallel \$(nproc)
        cmake --install \"\$PORTAL_BUILD\"
        rm -rf \"\$PORTAL_BUILD\"

        echo '[STEP] Building noctalia-kwe (Noctalia fork shell)...'
        SHELL_BUILD='${SCRIPT_DIR}/build.deb-quick-noctalia'
        rm -rf \"\$SHELL_BUILD\"
        meson setup \"\$SHELL_BUILD\" 'shell/noctalia' \\
            --prefix='${TEST_PREFIX}' \\
            --buildtype=debugoptimized \\
            -Dtests=disabled
        meson compile -C \"\$SHELL_BUILD\" --jobs \$(nproc)
        meson install -C \"\$SHELL_BUILD\"
    "

    if [[ ! -x "$TEST_PREFIX/bin/kinetic-we" ]]; then
        error "Quick build did not produce $TEST_PREFIX/bin/kinetic-we"
        exit 1
    fi
    if [[ ! -x "$TEST_PREFIX/bin/noctalia-kwe" ]]; then
        error "Quick build did not produce $TEST_PREFIX/bin/noctalia-kwe"
        exit 1
    fi
    info "Quick build installed to $TEST_PREFIX"
}

# ---------------------------------------------------------------------------
# Verify host runtime dependencies of the installed dev binaries
# ---------------------------------------------------------------------------
verify_host_deps() {
    step "Verifying host runtime dependencies..."

    local -a binaries=(
        "$TEST_PREFIX/bin/kinetic-we"
        "$TEST_PREFIX/bin/noctalia-kwe"
        "$TEST_PREFIX/libexec/xdg-desktop-portal-kwe"
    )

    local -a missing_libs=()
    local binary
    for binary in "${binaries[@]}"; do
        [[ -x "$binary" ]] || continue
        local missing_in_binary
        missing_in_binary=$(LD_LIBRARY_PATH="${TEST_PREFIX}/lib/x86_64-linux-gnu:${TEST_PREFIX}/lib64" \
            ldd "$binary" 2>/dev/null | awk '/not found/ {print $1}' || true)
        if [[ -n "$missing_in_binary" ]]; then
            info "  $(basename "$binary") needs libraries the host does not have:"
            while IFS= read -r lib; do
                info "    $lib"
                missing_libs+=("$lib")
            done <<< "$missing_in_binary"
        fi
    done

    if [[ ${#missing_libs[@]} -eq 0 ]]; then
        info "All runtime libraries resolve on the host."
        return 0
    fi

    echo
    error "The host is missing runtime libraries needed by the dev session."
    echo
    echo "  Missing library -> providing package:"
    local -a pkgs=()
    local lib
    for lib in $(printf '%s\n' "${missing_libs[@]}" | sort -u); do
        local pkg
        pkg=$(dpkg -S "$lib" 2>/dev/null | head -1 | sed 's/:.*//' || true)
        if [[ -n "$pkg" ]]; then
            echo "    $lib -> $pkg"
            pkgs+=("$pkg")
        else
            echo "    $lib -> (no package found; try: apt-file search $lib)"
        fi
    done
    echo
    if [[ ${#pkgs[@]} -gt 0 ]]; then
        echo "  Install with: sudo apt install ${pkgs[*]}"
    fi
    echo
    warn "The build succeeded but the session will fail to start until these"
    warn "libraries are installed on the host."
    exit 1
}

# ---------------------------------------------------------------------------
# Extract .deb packages to TEST_PREFIX for testing
# ---------------------------------------------------------------------------
extract_for_testing() {
    step "Extracting .deb contents to ${TEST_PREFIX} for TTY testing..."

    local deb_file
    deb_file=$(find "$RESULTS_DIR" -type f -name "${PACKAGE}_${SPEC_VERSION}*.deb" \
        ! -name '*kglobalacceld*' \
        -printf '%T@ %p\n' 2>/dev/null \
        | sort -rn | head -1 | awk '{print $2}')
    if [[ -z "$deb_file" ]]; then
        deb_file=$(find "$RESULTS_DIR" -type f -name "${PACKAGE}*.deb" \
            ! -name '*kglobalacceld*' \
            -printf '%T@ %p\n' 2>/dev/null \
            | sort -rn | head -1 | awk '{print $2}')
    fi
    if [[ -z "$deb_file" ]]; then
        error "No .deb package found to extract."
        exit 1
    fi

    local tmpdir
    tmpdir=$(mktemp -d /tmp/kineticwe-deb-extract.XXXXXX)
    dpkg -x "$deb_file" "$tmpdir"

    local portal_deb
    portal_deb=$(find "$RESULTS_DIR" -type f -name "xdg-desktop-portal-kwe*.deb" \
        -printf '%T@ %p\n' 2>/dev/null \
        | sort -rn | head -1 | awk '{print $2}')
    if [[ -n "$portal_deb" ]]; then
        dpkg -x "$portal_deb" "$tmpdir"
        info "  Portal backend: $TEST_PREFIX/libexec/xdg-desktop-portal-kwe"
    else
        warn "No xdg-desktop-portal-kwe .deb found; the session will run without a portal backend."
    fi

    local shell_deb
    shell_deb=$(find "$RESULTS_DIR" -type f -name "kineticwe-noctalia*.deb" \
        -printf '%T@ %p\n' 2>/dev/null \
        | sort -rn | head -1 | awk '{print $2}')
    if [[ -n "$shell_deb" ]]; then
        dpkg -x "$shell_deb" "$tmpdir"
        info "  Shell: $TEST_PREFIX/bin/noctalia-kwe"
    else
        warn "No kineticwe-noctalia .deb found; the session will run without the shell."
    fi

    rsync -a "$tmpdir/usr/" "$TEST_PREFIX/"
    rm -rf "$tmpdir"

    info "Extracted to $TEST_PREFIX"
    info "  Binary: $TEST_PREFIX/bin/kinetic-we"
}

# ---------------------------------------------------------------------------
# Install the single dev startup script for TTY testing
# ---------------------------------------------------------------------------
install_startup_script() {
    step "Installing dev startup script..."

    mkdir -p "$TEST_PREFIX/bin"

    if [[ ! -f "$SCRIPT_DIR/scripts/start-kineticwe.sh" ]]; then
        error "Missing template: $SCRIPT_DIR/scripts/start-kineticwe.sh"
        exit 1
    fi

    sed -e "s|@INSTALL_PREFIX@|$TEST_PREFIX|g" \
        "$SCRIPT_DIR/scripts/start-kineticwe.sh" \
        > "$TEST_PREFIX/bin/start-kineticwe"
    chmod +x "$TEST_PREFIX/bin/start-kineticwe"
    info "  $TEST_PREFIX/bin/start-kineticwe"

    mkdir -p "$TEST_PREFIX/share/kineticwe"
    install -m 0644 "$SCRIPT_DIR/scripts/kineticwe-session.target" \
        "$TEST_PREFIX/share/kineticwe/kineticwe-session.target"
    info "  $TEST_PREFIX/share/kineticwe/kineticwe-session.target"
}

# ---------------------------------------------------------------------------
# Install the "KineticWE-Dev" login-screen session entry
# ---------------------------------------------------------------------------
install_dev_session_entry() {
    step "Installing 'KineticWE-Dev' login-screen session entry..."

    local template="$SCRIPT_DIR/scripts/kineticwe-dev.desktop.in"
    if [[ ! -f "$template" ]]; then
        error "Missing template: $template"
        exit 1
    fi

    local tmp_file
    tmp_file=$(mktemp --suffix=.desktop /tmp/kineticwe-dev.XXXXXX)
    sed -e "s|@INSTALL_PREFIX@|$TEST_PREFIX|g" \
        "$template" \
        > "$tmp_file"

    if command -v desktop-file-validate &>/dev/null; then
        local validate_out
        validate_out=$(desktop-file-validate "$tmp_file" 2>&1 | grep -v 'DesktopNames' || true)
        if [[ -n "$validate_out" ]]; then
            warn "desktop-file-validate reported issues (continuing anyway):"
            warn "$validate_out"
        fi
    fi

    local target="$SESSION_DIR/$SESSION_DESKTOP"

    if [[ -f "$target" ]] && cmp -s "$tmp_file" "$target"; then
        info "  $target (already up to date)"
        rm -f "$tmp_file"
    elif sudo mkdir -p "$SESSION_DIR" \
        && sudo install -m 0644 "$tmp_file" "$target"; then
        rm -f "$tmp_file"
        info "  $target"
        info "  Select 'KineticWE-Dev' at the login screen (appears after logout)."
    else
        rm -f "$tmp_file"
        warn "Could not install the session entry (sudo declined/failed)."
        warn "The dev build still works from a TTY: start-kineticwe"
    fi

    local stale="$TEST_PREFIX/share/wayland-sessions/kineticwe.desktop"
    if [[ -f "$stale" ]]; then
        rm -f "$stale"
        info "  Removed stale user-local entry: $stale"
    fi
}

# ---------------------------------------------------------------------------
# Reset local dev install
# ---------------------------------------------------------------------------
reset_local() {
    step "Removing kineticwe dev files from ${TEST_PREFIX}..."

    local removed_any=false

    local -a known_paths=(
        "$TEST_PREFIX/bin/kinetic-we"
        "$TEST_PREFIX/bin/kwin_wayland"
        "$TEST_PREFIX/bin/kwin_wayland_wrapper"
        "$TEST_PREFIX/bin/kinetic-we_wayland"
        "$TEST_PREFIX/bin/kinetic-we_wayland_wrapper"
        "$TEST_PREFIX/bin/kwindowprop"
        "$TEST_PREFIX/bin/start-kineticwe"
        "$TEST_PREFIX/lib64/libKGlobalAccelD.so"*
        "$TEST_PREFIX/lib64/libkwin.so"*
        "$TEST_PREFIX/lib64/libkcmkwincommon.so"*
        "$TEST_PREFIX/lib/x86_64-linux-gnu/libKGlobalAccelD.so"*
        "$TEST_PREFIX/lib/x86_64-linux-gnu/libkwin.so"*
        "$TEST_PREFIX/lib/x86_64-linux-gnu/libkcmkwincommon.so"*
        "$TEST_PREFIX/lib64/libexec/kwin"*
        "$TEST_PREFIX/libexec/kwin"*
        "$TEST_PREFIX/lib64/kconf_update_bin/kwin"*
        "$TEST_PREFIX/lib64/qt6/plugins/kwin"
        "$TEST_PREFIX/lib64/qt6/plugins/plasma/kcms"
        "$TEST_PREFIX/lib64/qt6/plugins/kf6/packagestructure"
        "$TEST_PREFIX/lib64/qt6/qml/org/kde/kwin"
        "$TEST_PREFIX/share/kwin-wayland"
        "$TEST_PREFIX/share/config.kcfg"
        "$TEST_PREFIX/share/kconf_update"
        "$TEST_PREFIX/share/knsrcfiles"
        "$TEST_PREFIX/share/qlogging-categories6"
        "$TEST_PREFIX/share/krunner"
        "$TEST_PREFIX/share/wayland-sessions/kineticwe.desktop"
        "$TEST_PREFIX/libexec/xdg-desktop-portal-kwe"
        "$TEST_PREFIX/share/xdg-desktop-portal"
        "$TEST_PREFIX/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kwe.service"
        "$TEST_PREFIX/share/applications/org.freedesktop.impl.portal.desktop.kwe.desktop"
        "$TEST_PREFIX/lib64/systemd/user/plasma-xdg-desktop-portal-kwe.service"
        "$TEST_PREFIX/lib64/qt6/qml/org/kde/xdgdesktopportal"
        "$TEST_PREFIX/lib/qt6/qml/org/kde/xdgdesktopportal"
        "$TEST_PREFIX/bin/noctalia-kwe"
        "$TEST_PREFIX/share/noctalia"
        "$TEST_PREFIX/share/applications/dev.noctalia.Noctalia.desktop"
        "$TEST_PREFIX/share/icons/hicolor/scalable/apps/noctalia.svg"
        "$TEST_PREFIX/share/applications/dev.kineticwe.Settings.desktop"
        "$TEST_PREFIX/share/icons/hicolor/256x256/apps/kineticwe-logo.png"
    )

    for pattern in "${known_paths[@]}"; do
        # shellcheck disable=SC2086
        for f in $pattern; do
            if [[ -e "$f" || -L "$f" ]]; then
                rm -rf "$f"
                info "  Removed: $f"
                removed_any=true
            fi
        done
    done

    find "$TEST_PREFIX/lib/.build-id" -type f -name '*.debug' 2>/dev/null \
        -exec sh -c 'readelf -n "$1" 2>/dev/null | grep -q "kineticwe\|kwin"' _ {} \; \
        -delete 2>/dev/null || true

    local session_entry="$SESSION_DIR/$SESSION_DESKTOP"
    if [[ -f "$session_entry" ]]; then
        if sudo rm -f "$session_entry"; then
            info "  Removed: $session_entry"
            removed_any=true
        else
            warn "Could not remove $session_entry (sudo declined/failed)."
        fi
    fi

    if $removed_any; then
        echo
        info "Dev files removed from $TEST_PREFIX."
    else
        info "No kineticwe dev files found in $TEST_PREFIX."
    fi
}

# ---------------------------------------------------------------------------
# Print usage info
# ---------------------------------------------------------------------------
print_usage() {
    echo
    echo "=========================================="
    echo "  Build complete! TTY Usage:"
    echo "=========================================="
    echo

    if [[ -x "$TEST_PREFIX/bin/kinetic-we" ]]; then
        echo "  ┌─────────────────────────────────────────────────────┐"
        echo "  │  DEV BUILD  (local install)                         │"
        echo "  │    TTY:          start-kineticwe                    │"
        echo "  │    Login screen: KineticWE-Dev                      │"
        echo "  └─────────────────────────────────────────────────────┘"
    else
        echo "  No dev build found at $TEST_PREFIX"
        echo "  (run ./build-deb-dev.sh --quick first)"
    fi
    echo

    if [[ -f "$SESSION_DIR/$SESSION_DESKTOP" ]]; then
        info "  Login-screen entry: $SESSION_DIR/$SESSION_DESKTOP"
    else
        info "  No login-screen entry installed (skipped or sudo declined)."
    fi
    echo

    info "  Dev build release tag : ${DEV_RELEASE}"
    echo

    info "  Since TEST_PREFIX=$TEST_PREFIX is in your PATH, just type:"
    info "    start-kineticwe"
    echo

    info "Rebuild after making changes:"
    info "  ./build-deb-dev.sh --quick"
    echo

    if find "$RESULTS_DIR" -type f -name "${PACKAGE}*.deb" -print0 2>/dev/null | grep -qz .; then
        info ".deb packages:"
        find "$RESULTS_DIR" -type f -name "${PACKAGE}*.deb" -exec ls -lh {} \; 2>/dev/null | sed 's/^/  /'
        echo
    fi

    info "To remove local dev files:"
    info "  ./build-deb-dev.sh --reset-local"
    echo
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    local do_quick=false
    local do_deb=false
    local do_session=true
    local do_reset_local=false
    local do_extract=true

    for arg in "$@"; do
        case "$arg" in
            --quick)       do_quick=true ;;
            --deb)         do_deb=true ;;
            --no-extract)  do_extract=false ;;
            --no-session)  do_session=false ;;
            --reset-local) do_reset_local=true ;;
            --help|-h)     echo "Usage: $0 [--quick] [--deb] [--no-extract] [--no-session] [--reset-local]"; exit 0 ;;
            *)             warn "Unknown option: $arg";;
        esac
    done

    if $do_reset_local; then
        echo "=========================================="
        echo "  KineticWE Debian - Reset Local Dev Install"
        echo "=========================================="
        echo
        reset_local
        exit 0
    fi

    if $do_deb; then
        echo "=========================================="
        echo "  KineticWE Debian Dev Build (.deb)"
        echo "  Version: ${SPEC_VERSION}-${DEV_RELEASE}"
        echo "  Install prefix: ${TEST_PREFIX}"
        echo "=========================================="
        echo

        check_prereqs
        setup_distrobox
        build_debs
        if $do_extract; then
            extract_for_testing
        fi
        verify_host_deps
        install_startup_script
        if $do_session; then
            install_dev_session_entry
        fi
        print_usage
        exit 0
    fi

    if ! $do_quick && ! $do_deb; then
        do_quick=true
    fi

    echo "=========================================="
    echo "  KineticWE Quick Dev Build (Debian)"
    echo "  Install prefix: ${TEST_PREFIX}"
    echo "=========================================="
    echo

    check_prereqs
    setup_distrobox
    quick_build
    verify_host_deps
    install_startup_script
    if $do_session; then
        install_dev_session_entry
    fi
    print_usage
    exit 0
}

main "$@"