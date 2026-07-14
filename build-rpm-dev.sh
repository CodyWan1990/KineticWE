#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Dev Build & Test Script
# =============================================================================
# Builds KineticWE from local source using a Fedora distrobox and installs it
# to a local testing prefix so you can run it from a TTY immediately.
#
# Usage:
#   ./build-rpm-dev.sh                          # Default: RPM build + extract for TTY testing
#   ./build-rpm-dev.sh --quick                  # Quick cmake build (incremental, no RPM)
#   ./build-rpm-dev.sh --install                # Same as default (alias)
#   ./build-rpm-dev.sh --ostree                 # Build RPM + layer via rpm-ostree (needs reboot)
#   ./build-rpm-dev.sh --ostree --reboot        # Build + ostree + reboot
#   ./build-rpm-dev.sh --reset                  # Revert rpm-ostree override
#
# All build modes (default/--install/--quick) install the binary to the same
# TEST_PREFIX so you always test with a single startup script.
#
# Environment variables:
#   TEST_PREFIX     - Where to install for TTY testing (default: $HOME/.local/kineticwe-dev)
#   DISTROBOX_NAME  - Name of the distrobox container (default: kineticwe-build)
# =============================================================================

TEST_PREFIX="${TEST_PREFIX:-$HOME/.local/kineticwe-dev}"
DISTROBOX_NAME="${DISTROBOX_NAME:-kineticwe-build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPEC_DIR="${SCRIPT_DIR}/lgl-kineticwe-copr"
RESULTS_DIR="${SPEC_DIR}/results"
DISTROBOX_IMAGE="fedora:44"

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
# Parse spec metadata
# ---------------------------------------------------------------------------
SPEC_FILE="${SPEC_DIR}/kineticwe.spec"
SPEC_VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC_FILE")
SPEC_RELEASE=$(awk '/^Release:/ {print $2; exit}' "$SPEC_FILE" | sed 's/%{?dist}//')
PACKAGE=$(awk '/^Name:/ {print $2; exit}' "$SPEC_FILE")

NEXT_DEV=1
if [[ -d "$RESULTS_DIR" ]]; then
    EXISTING_DEVS=$(find "$RESULTS_DIR" -type f -name "*.rpm" -printf '%f\n' 2>/dev/null | grep -oP "\.dev\d+" | grep -oP "\d+" || true)
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
    if ! command -v rpm-ostree &>/dev/null && [[ "${1:-}" == "ostree" ]]; then
        error "rpm-ostree is required for --ostree but not found."
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
    if distrobox list 2>/dev/null | grep -q "^${DISTROBOX_NAME}[[:space:]]"; then
        info "Container '${DISTROBOX_NAME}' already exists."
    else
        info "Creating distrobox from ${DISTROBOX_IMAGE}..."
        distrobox create \
            --name "$DISTROBOX_NAME" \
            --image "$DISTROBOX_IMAGE" \
            --additional-flags "--userns=keep-id" \
            --yes
        info "Container created."
    fi

    step "Ensuring build tools are installed inside the container..."
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        if ! rpm -q rpm-build &>/dev/null; then
            # Ignore scriptlet failures (systemd/udev scripts fail in containers)
            sudo dnf install -y rpm-build rpmdevtools dnf-plugins-core curl wget git || true
            if ! rpm -q rpm-build &>/dev/null; then
                echo 'ERROR: rpm-build failed to install' >&2
                exit 1
            fi
        fi
    "

    local buildreqs
    buildreqs=$(grep '^BuildRequires:' "$SPEC_FILE" | sed 's/BuildRequires:\s*//' | tr '\n' ' ')
    info "Installing build dependencies (one-time, cached across builds)..."
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        # Ignore scriptlet failures; container cannot run systemd/udev scripts
        sudo dnf install -y --skip-unavailable $buildreqs || true
        # Verify cmake at least made it
        if ! command -v cmake &>/dev/null; then
            echo 'ERROR: cmake failed to install' >&2
            exit 1
        fi
    "
    info "Build environment ready."
}

# ---------------------------------------------------------------------------
# Build RPMs inside the distrobox
# ---------------------------------------------------------------------------
build_rpms() {
    step "Building RPMs inside distrobox (Release: ${DEV_RELEASE})..."
    mkdir -p "$RESULTS_DIR"

    # Generate SRPM (which also prepares all source files)
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        set -euo pipefail
        cd '$SCRIPT_DIR'
        make -f '${SPEC_DIR}/Makefile' srpm
        echo 'SRPM and sources ready.'
    "

    local srpm_file
    srpm_file=$(ls -1t "$RESULTS_DIR"/*.src.rpm 2>/dev/null | head -1)
    if [[ -z "$srpm_file" ]]; then
        error "No SRPM was generated!"
        exit 1
    fi
    info "SRPM: $(basename "$srpm_file")"

    # Rebuild SRPM into binary RPMs
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        set -euo pipefail
        # rpmbuild --rebuild places binary RPMs in arch subdirectories (e.g. x86_64/),
        # so we need to remove from any level
        find '${RESULTS_DIR}' -type f -name '${PACKAGE}*.rpm' ! -name '*.src.rpm' -delete
        rpmbuild --rebuild \
            --define '_rpmdir ${RESULTS_DIR}' \
            --define 'dist .fc44' \
            '${srpm_file}'
    "

    # Rename RPMs to include dev release suffix
    local rpm_count=0
    while IFS= read -r -d '' rpm_file; do
        local base
        base=$(basename "$rpm_file")
        if [[ "$base" == *".src.rpm" ]]; then
            continue
        fi
        local escaped_release
        escaped_release=$(printf '%s\n' "$SPEC_RELEASE" | sed 's/\./\\./g')
        local new_name
        new_name=$(echo "$base" | sed "s/${escaped_release}\.fc44/${DEV_RELEASE}.fc44/")
        if [[ "$base" != "$new_name" && -n "$new_name" ]]; then
            mv "$rpm_file" "$RESULTS_DIR/$new_name"
            info "Renamed: $base -> $new_name"
        fi
        rpm_count=$((rpm_count + 1))
    done < <(find "$RESULTS_DIR" -type f -name "${PACKAGE}*.rpm" ! -name "*.src.rpm" -print0)

    if [[ "$rpm_count" -eq 0 ]]; then
        error "No binary RPMs were produced!"
        exit 1
    fi

    echo
    info "Binary RPMs produced:"
    find "$RESULTS_DIR" -type f -name "${PACKAGE}*.rpm" ! -name "*.src.rpm" -exec ls -lh {} \;
    echo
}

# ---------------------------------------------------------------------------
# Quick build: cmake directly inside the distrobox (no RPM)
# ---------------------------------------------------------------------------
quick_build() {
    local build_dir="${SCRIPT_DIR}/build.rpm-quick"

    step "Quick-building KineticWE in distrobox (prefix: ${TEST_PREFIX})..."

    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        set -euo pipefail
        cd '$SCRIPT_DIR'

        # Build kglobalacceld if not already installed
        if ! pkg-config --exists KGlobalAccelD 2>/dev/null; then
            echo '[STEP] Building kglobalacceld first...'
            KGA_BUILD=\$(mktemp -d /tmp/kga-build.XXXXXX)
            git clone --depth 1 https://invent.kde.org/plasma/kglobalacceld '\$KGA_BUILD'
            cd '\$KGA_BUILD'
            cmake -B build -S . \\
                -DCMAKE_INSTALL_PREFIX='${TEST_PREFIX}' \\
                -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
                -DBUILD_TESTING=OFF
            cmake --build build --parallel \$(nproc)
            cmake --install build
            cd '$SCRIPT_DIR'
            rm -rf '\$KGA_BUILD'
        fi

        echo '[STEP] Configuring KineticWE...'
        cmake -B '$build_dir' -S . \\
            -DCMAKE_INSTALL_PREFIX='${TEST_PREFIX}' \\
            -DCMAKE_PREFIX_PATH='${TEST_PREFIX}' \\
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
            -DBUILD_TESTING=OFF \\
            -DKWIN_BUILD_GLOBALSHORTCUTS=ON

        echo '[STEP] Building KineticWE...'
        cmake --build '$build_dir' --parallel \$(nproc)

        echo '[STEP] Installing to ${TEST_PREFIX}...'
        cmake --install '$build_dir'
    "

    if [[ ! -x "$TEST_PREFIX/bin/kinetic-we" ]]; then
        error "Quick build did not produce $TEST_PREFIX/bin/kinetic-we"
        exit 1
    fi
    info "Quick build installed to $TEST_PREFIX"
}

# ---------------------------------------------------------------------------
# Install quick-build startup script
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Install RPM via rpm-ostree (for --ostree)
extract_for_testing() {
    step "Extracting RPM contents to ${TEST_PREFIX} for TTY testing..."
    local rpm_file
    rpm_file=$(find "$RESULTS_DIR" -type f -name "${PACKAGE}-[0-9]*.rpm" \
        ! -name '*.src.rpm' \
        ! -name '*kglobalacceld*' \
        -printf '%T@ %p\n' 2>/dev/null \
        | sort -rn | head -1 | awk '{print $2}')
    if [[ -z "$rpm_file" ]]; then
        error "No binary RPM found to extract."
        exit 1
    fi

    rm -rf "$TEST_PREFIX"
    mkdir -p "$TEST_PREFIX"
    rpm2cpio "$rpm_file" | cpio -idm -D "$TEST_PREFIX" 2>/dev/null

    if [[ ! -x "$TEST_PREFIX/usr/bin/kinetic-we" ]]; then
        warn "Extraction produced no kinetic-we binary at expected path."
    fi
    info "Extracted to $TEST_PREFIX"
    info "  Binary: $TEST_PREFIX/usr/bin/kinetic-we"
}

# ---------------------------------------------------------------------------
# Install the single dev startup script for TTY testing
# ---------------------------------------------------------------------------
# Arguments:
#   $1  - Layout type: "rpm" (default, binary at $TEST_PREFIX/usr/bin/) or
#         "quick" (binary at $TEST_PREFIX/bin/)
install_startup_script() {
    local layout="${1:-rpm}"
    step "Installing dev startup script (layout: ${layout})..."

    mkdir -p "$TEST_PREFIX/bin"

    if [[ ! -f "$SCRIPT_DIR/scripts/start-kineticwe.sh" ]]; then
        error "Missing template: $SCRIPT_DIR/scripts/start-kineticwe.sh"
        exit 1
    fi

    if [[ "$layout" == "quick" ]]; then
        local prefix_override="$TEST_PREFIX"
    else
        local prefix_override="$TEST_PREFIX/usr"
    fi

    # Generate the single startup script from the template.
    # This provides the full session experience (portals, noctalia, services)
    # just like the COPR /usr/bin/start-kineticwe, but points at the dev build.
    sed -e "s|@INSTALL_PREFIX@|$prefix_override|g" \
        "$SCRIPT_DIR/scripts/start-kineticwe.sh" \
        > "$TEST_PREFIX/bin/start-kineticwe"
    chmod +x "$TEST_PREFIX/bin/start-kineticwe"
    info "  $TEST_PREFIX/bin/start-kineticwe"
}

# ---------------------------------------------------------------------------
# Install via rpm-ostree (override replace)
# ---------------------------------------------------------------------------
install_via_ostree() {
    step "Preparing rpm-ostree override..."
    local rpm_files
    rpm_files=$(find "$RESULTS_DIR" -type f -name "${PACKAGE}*.rpm" \
        ! -name '*.src.rpm' \
        -printf '%p\n' 2>/dev/null | sort)
    if [[ -z "$rpm_files" ]]; then
        error "No binary RPMs found in $RESULTS_DIR"
        exit 1
    fi

    echo "The following RPMs will be layered:"
    echo "$rpm_files" | while read -r f; do echo "  * $(basename "$f")"; done
    echo

    read -r -p "Proceed with rpm-ostree override replace? [y/N] " reply
    if [[ ! "$reply" =~ ^[Yy]$ ]]; then
        warn "Install cancelled."
        return 1
    fi

    info "Running: rpm-ostree override replace [${PACKAGE} RPMs]"
    # shellcheck disable=SC2086
    sudo rpm-ostree override replace $rpm_files

    echo
    info "Override applied. A new deployment has been created."
}

# ---------------------------------------------------------------------------
# Reset rpm-ostree override (revert to COPR version)
# ---------------------------------------------------------------------------
reset_override() {
    step "Resetting rpm-ostree override for kineticwe..."
    if ! rpm-ostree override list 2>/dev/null | grep -q "${PACKAGE}"; then
        info "No current override for ${PACKAGE} found."
        return 0
    fi

    read -r -p "Reset kineticwe override back to COPR version? [y/N] " reply
    if [[ ! "$reply" =~ ^[Yy]$ ]]; then
        warn "Reset cancelled."
        return 1
    fi

    sudo rpm-ostree override reset "${PACKAGE}"
    echo
    info "Override reset. Reboot to revert to the COPR version."
    info "  sudo systemctl reboot"
}

# ---------------------------------------------------------------------------
# Reboot prompt
# ---------------------------------------------------------------------------
prompt_reboot() {
    echo
    read -r -p "Reboot now to boot into the new deployment? [y/N] " reply
    if [[ "$reply" =~ ^[Yy]$ ]]; then
        info "Rebooting..."
        sudo systemctl reboot
    else
        echo
        info "You can reboot later:"
        info "  sudo systemctl reboot"
        echo
        info "To check deployments:"
        info "  rpm-ostree status"
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

    # Detect which layout was built
    local bin_path=""
    local bin_label=""
    if [[ -x "$TEST_PREFIX/usr/bin/kinetic-we" ]]; then
        bin_path="$TEST_PREFIX/usr/bin"
        bin_label="RPM-extracted"
    elif [[ -x "$TEST_PREFIX/bin/kinetic-we" ]]; then
        bin_path="$TEST_PREFIX/bin"
        bin_label="Quick-build"
    fi

    echo "  ┌─────────────────────────────────────────────────────┐"
    if [[ -n "$bin_path" ]]; then
        echo "  │  DEV BUILD  (${bin_label})                        │"
        echo "  │    $TEST_PREFIX/bin/start-kineticwe         │"
    else
        echo "  │  DEV BUILD  (none)                                 │"
        echo "  │    (run ./build-rpm-dev.sh first)                  │"
    fi
    echo "  ├─────────────────────────────────────────────────────┤"
    echo "  │  STABLE  (COPR system install)                      │"
    echo "  │    /usr/bin/start-kineticwe                         │"
    echo "  └─────────────────────────────────────────────────────┘"
    echo

    if rpm -q "${PACKAGE}" &>/dev/null; then
        local stable_ver
        stable_ver=$(rpm -q "${PACKAGE}" --qf "%{VERSION}-%{RELEASE}" 2>/dev/null || echo "unknown")
        info "  COPR stable version : ${stable_ver}"
    fi

    info "  Dev build release tag : ${DEV_RELEASE}"
    echo

    info "Rebuild after making changes:"
    info "  ./build-rpm-dev.sh"
    echo

    if find "$RESULTS_DIR" -type f -name "${PACKAGE}*.rpm" ! -name '*.src.rpm' -print0 2>/dev/null | grep -qz .; then
        info "RPM packages:"
        find "$RESULTS_DIR" -type f -name "${PACKAGE}*.rpm" ! -name '*.src.rpm' -exec ls -lh {} \; 2>/dev/null | sed 's/^/  /'
    fi
    echo

    info "To layer via rpm-ostree (requires reboot):"
    info "  ./build-rpm-dev.sh --ostree"
    echo

    info "To revert to the COPR version:"
    info "  ./build-rpm-dev.sh --reset"
    echo

    info "RPMs are cached in:"
    info "  $RESULTS_DIR"
    echo
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    local do_install=false
    local do_ostree=false
    local do_reboot=false
    local do_reset=false
    local do_quick=false

    for arg in "$@"; do
        case "$arg" in
            --install) do_install=true ;;
            --ostree)  do_ostree=true ;;
            --reboot)  do_reboot=true ;;
            --reset)   do_reset=true ;;
            --quick)   do_quick=true ;;
            --help|-h) echo "Usage: $0 [--install] [--ostree] [--reboot] [--quick] [--reset]"; exit 0 ;;
            *)         warn "Unknown option: $arg";;
        esac
    done

    if $do_reset; then
        echo "=========================================="
        echo "  KineticWE RPM - Reset Override"
        echo "=========================================="
        echo
        reset_override
        exit 0
    fi

    if $do_quick; then
        echo "=========================================="
        echo "  KineticWE Quick Dev Build (no RPM)"
        echo "  Install prefix: ${TEST_PREFIX}"
        echo "=========================================="
        echo

        check_prereqs
        setup_distrobox
        quick_build
        install_startup_script "quick"
        print_usage
        exit 0
    fi

    if $do_install; then
        echo "=========================================="
        echo "  KineticWE RPM Dev Install"
        echo "  Version: ${SPEC_VERSION}-${DEV_RELEASE}"
        echo "  Install prefix: ${TEST_PREFIX}"
        echo "=========================================="
        echo

        check_prereqs
        setup_distrobox
        build_rpms
        extract_for_testing
        install_startup_script
        print_usage

        if $do_ostree; then
            echo
            echo "=========================================="
            echo "  Installing via rpm-ostree..."
            echo "=========================================="
            echo

            if install_via_ostree; then
                if $do_reboot; then
                    prompt_reboot
                else
                    echo
                    info "Run with --reboot to reboot after install,"
                    info "or reboot manually: sudo systemctl reboot"
                fi
            fi
        fi
        exit 0
    fi

    # Default: build RPMs only, no install
    echo "=========================================="
    echo "  KineticWE RPM Dev Build"
    echo "  Version: ${SPEC_VERSION}-${DEV_RELEASE}"
    echo "  Test prefix: ${TEST_PREFIX}"
    echo "=========================================="
    echo

    check_prereqs "$@"
    setup_distrobox
    build_rpms
    extract_for_testing
    install_startup_script
    print_usage

    if $do_ostree; then
        echo
        echo "=========================================="
        echo "  Installing via rpm-ostree..."
        echo "=========================================="
        echo

        if install_via_ostree; then
            if $do_reboot; then
                prompt_reboot
            else
                echo
                info "Run with --reboot to reboot after install,"
                info "or reboot manually: sudo systemctl reboot"
            fi
        fi
    fi
}

main "$@"
