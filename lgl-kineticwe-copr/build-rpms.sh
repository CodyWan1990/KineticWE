#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE RPM Build Script
# =============================================================================
# Builds SRPM and binary RPMs from the kineticwe-2.0 branch using a Fedora
# distrobox container.
#
# Usage:
#   ./build-rpms.sh              # Full build (SRPM + binary RPMs)
#   ./build-rpms.sh --srpm-only  # Only build the SRPM
#   ./build-rpms.sh --clean      # Remove old remnants only (no build)
#
# All generated output goes to the local results/ directory.
# The results/ directory is ignored by the packaging repository.
# =============================================================================

DISTROBOX_NAME="${DISTROBOX_NAME:-kineticwe-build}"
DISTROBOX_IMAGE="fedora:44"
OUTPUT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "${OUTPUT_DIR}/.." rev-parse --show-toplevel)"
RPM_DIR="${RPM_DIR:-${OUTPUT_DIR}/results}"
SPEC_FILE="${OUTPUT_DIR}/kineticwe.spec"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

# ---------------------------------------------------------------------------
# Parse spec metadata
# ---------------------------------------------------------------------------
SPEC_VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC_FILE")
SPEC_RELEASE=$(awk '/^Release:/ {print $2; exit}' "$SPEC_FILE" | sed 's/%{?dist}//')
ARCH=$(rpm --eval '%{_arch}' 2>/dev/null || echo "x86_64")

# ---------------------------------------------------------------------------
# Remove old remnants (safe: never touches results/ directory)
# ---------------------------------------------------------------------------
clean_local_build() {
    step "Removing old local build remnants..."

    rm -f ~/.local/lib64/libkwin.so* \
          ~/.local/lib64/libKGlobalAccelD.so* \
          ~/.local/lib64/libkdecorations3*.so* \
          ~/.local/lib64/libkcmkwincommon.so*
    rm -rf ~/.local/lib64/plugins/kwin \
           ~/.local/lib64/plugins/org.kde.kdecoration3 \
           ~/.local/lib64/qml/org/kde/kwin \
           ~/.local/lib64/libexec/xdg-desktop-portal-kwe \
           ~/.local/lib64/libexec/plasma-apply-aurorae \
           ~/.local/lib64/kconf_update_bin
    rm -rf ~/.local/lib64/cmake/KGlobalAccelD \
           ~/.local/lib64/cmake/KDecoration3 \
           ~/.local/lib64/cmake/KWin \
           ~/.local/lib64/cmake/Aurorae
    rm -f ~/.local/bin/kinetic-we \
          ~/.local/bin/kinetic-we_wayland_wrapper \
          ~/.local/bin/kwin_wayland \
          ~/.local/bin/kwin_wayland_wrapper \
          ~/.local/bin/kwindowprop \
          ~/.local/bin/noctalia-kwe \
          ~/.local/bin/start-kineticwe
    rm -rf ~/.local/share/kwin-wayland \
           ~/.local/share/knsrcfiles \
           ~/.local/share/qlogging-categories6 \
           ~/.local/share/kconf_update \
           ~/.local/share/config.kcfg \
           ~/.local/share/kineticwe \
           ~/.local/share/noctalia \
           ~/.local/share/noctalia.log \
           ~/.local/share/dbus-1/interfaces
    rm -f ~/.local/share/knotifications6/kwin.notifyrc \
          ~/.local/share/knotifications6/xdg-desktop-portal-kwe.notifyrc
    rm -f ~/.local/share/applications/kcm_*.desktop \
          ~/.local/share/applications/org.kde.kwin.*.desktop \
          ~/.local/share/applications/dev.noctalia.Noctalia.desktop \
          ~/.local/share/applications/dev.kineticwe.Settings.desktop
    rm -f ~/.local/share/icons/hicolor/*/apps/kwin.* \
          ~/.local/share/icons/hicolor/*/apps/kineticwe-logo.* \
          ~/.local/share/icons/hicolor/scalable/apps/noctalia.svg
    rm -f ~/.local/share/color-schemes/noctalia.colors
    rm -rf ~/.local/share/aurorae
    rm -f ~/.local/share/systemd/user/kineticwe-session.target \
          ~/.local/share/systemd/user/kineticwe-workspace.target
    rm -rf ~/.local/lib/systemd/user/kineticwe-*.target
    rm -f ~/.local/share/locale/*/LC_MESSAGES/kwin.mo \
          ~/.local/share/locale/*/LC_MESSAGES/kcm_*.mo \
          ~/.local/share/locale/*/LC_MESSAGES/kdecoration.mo \
          ~/.local/share/locale/*/LC_MESSAGES/xdg-desktop-portal-kde.mo
    rm -rf ~/.config/kineticwe
    rm -f /usr/local/share/wayland-sessions/kineticwe-dev.desktop

    info "Old local build remnants removed."
}

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
check_prereqs() {
    step "Checking prerequisites..."
    command -v distrobox &>/dev/null || error "distrobox not found."
    command -v podman &>/dev/null || command -v docker &>/dev/null || error "podman/docker not found."
    [[ -f "$SPEC_FILE" ]] || error "Spec not found: $SPEC_FILE"
    info "Package: kineticwe ${SPEC_VERSION}-${SPEC_RELEASE}"
    info "Output:  ${RPM_DIR}"
}

# ---------------------------------------------------------------------------
# Distrobox setup
# ---------------------------------------------------------------------------
setup_distrobox() {
    step "Setting up distrobox '${DISTROBOX_NAME}'..."

    if ! distrobox list 2>/dev/null | grep -w "$DISTROBOX_NAME" > /dev/null; then
        info "Creating distrobox from ${DISTROBOX_IMAGE}..."
        distrobox create --name "$DISTROBOX_NAME" --image "$DISTROBOX_IMAGE" \
            --additional-flags "--userns=keep-id" --yes
    fi

    step "Ensuring build tools are installed..."
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        if ! rpm -q rpm-build &>/dev/null; then
            sudo dnf install -y rpm-build rpmdevtools dnf-plugins-core curl wget git || true
        fi
    "

    local buildreqs
    buildreqs=$(grep '^BuildRequires:' "$SPEC_FILE" | sed 's/BuildRequires:\s*//' | tr '\n' ' ')

    step "Installing build dependencies..."
    distrobox enter "$DISTROBOX_NAME" -- bash -c "
        sudo dnf install -y --skip-unavailable $buildreqs || true
        missing=()
        for tool in cmake meson ninja; do
            command -v \$tool &>/dev/null || missing+=(\$tool)
        done
        if [[ \${#missing[@]} -gt 0 ]]; then
            echo 'ERROR: missing tools:' >&2
            printf '  %s\n' \"\${missing[@]}\" >&2
            exit 1
        fi
    "
    info "Container ready."
}

# ---------------------------------------------------------------------------
# Build SRPM
# ---------------------------------------------------------------------------
download_archive() {
    local output="$1"
    local url="$2"

    if [[ -s "$output" ]] && tar -tzf "$output" >/dev/null 2>&1; then
        info "Using cached source: $(basename "$output")"
        return
    fi

    rm -f "$output"
    curl -fL --connect-timeout 15 --max-time 600 -o "$output" "$url"
}

build_srpm() {
    step "Building SRPM..."
    mkdir -p "$RPM_DIR"

    # Source archive from git HEAD
    git -C "$REPO_DIR" archive \
        --prefix="kineticwe-${SPEC_VERSION}/" \
        -o "${RPM_DIR}/kineticwe-${SPEC_VERSION}.tar.gz" \
        HEAD

    # Pinned dependency sources
    local kga_ref kdeco_ref aurorae_ref kconfig_ref
    kga_ref=$(sed -n 's/.*%global kglobalacceld_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
    kdeco_ref=$(sed -n 's/.*%global kdecoration_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
    aurorae_ref=$(sed -n 's/.*%global aurorae_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
    kconfig_ref=$(sed -n 's/.*%global kconfig_compiler_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)

    download_archive "${RPM_DIR}/kglobalacceld-${kga_ref}.tar.gz" \
        "https://invent.kde.org/plasma/kglobalacceld/-/archive/${kga_ref}/kglobalacceld-${kga_ref}.tar.gz"
    download_archive "${RPM_DIR}/kdecoration-${kdeco_ref}.tar.gz" \
        "https://invent.kde.org/plasma/kdecoration/-/archive/${kdeco_ref}/kdecoration-${kdeco_ref}.tar.gz"
    download_archive "${RPM_DIR}/aurorae-${aurorae_ref}.tar.gz" \
        "https://invent.kde.org/plasma/aurorae/-/archive/${aurorae_ref}/aurorae-${aurorae_ref}.tar.gz"
    download_archive "${RPM_DIR}/kconfig-${kconfig_ref}.tar.gz" \
        "https://invent.kde.org/frameworks/kconfig/-/archive/${kconfig_ref}/kconfig-${kconfig_ref}.tar.gz"

    # Auxiliary sources from the repo
    cp "${REPO_DIR}/lgl-kineticwe-copr/SOURCES/kineticwe.desktop" "${RPM_DIR}/"
    cp "${REPO_DIR}/lgl-kineticwe-copr/SOURCES/kglobalacceld-mask.conf" "${RPM_DIR}/"

    # Greeter sources
    local greeter_ref
    greeter_ref=$(sed -n 's/.*%global noctalia_greeter_ref \([0-9a-f]\{40\}\).*/\1/p' "$SPEC_FILE" | head -1)
    download_archive "${RPM_DIR}/noctalia-greeter-${greeter_ref}.tar.gz" \
        "https://gitlab.com/theblackdon/noctalia-greeter-kwe/-/archive/${greeter_ref}/noctalia-greeter-${greeter_ref}.tar.gz"
    cp "${OUTPUT_DIR}/kineticwe-setup-greeter.sh" "${RPM_DIR}/"
    cp "${OUTPUT_DIR}/start-kineticwe.sh" "${RPM_DIR}/"

    # Build SRPM inside distrobox
    distrobox enter "$DISTROBOX_NAME" -- bash -lc "
        rpmbuild -bs \
            --define '_sourcedir ${RPM_DIR}' \
            --define '_srcrpmdir ${RPM_DIR}' \
            --define '_specdir ${OUTPUT_DIR}' \
            --define 'kglobalacceld_ref ${kga_ref}' \
            --define 'kdecoration_ref ${kdeco_ref}' \
            --define 'kconfig_compiler_ref ${kconfig_ref}' \
            '${SPEC_FILE}'
    "

    info "SRPM built."
}

# ---------------------------------------------------------------------------
# Build binary RPMs
# ---------------------------------------------------------------------------
build_rpms() {
    step "Building binary RPMs from SRPM..."

    local srpm
    srpm=$(ls -1 "${RPM_DIR}"/kineticwe-"${SPEC_VERSION}"-"${SPEC_RELEASE}"*.src.rpm 2>/dev/null | head -1)
    [[ -f "$srpm" ]] || error "SRPM not found: ${RPM_DIR}/kineticwe-${SPEC_VERSION}-${SPEC_RELEASE}*.src.rpm"

    distrobox enter "$DISTROBOX_NAME" -- bash -lc "
        rpmbuild --rebuild \
            --define '_rpmdir ${RPM_DIR}' \
            '${srpm}'
    "

    info "Binary RPMs built."
    find "$RPM_DIR" -type f \( -name '*.rpm' ! -name '*.src.rpm' \) -exec ls -lh {} \;
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    local mode="full"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --clean)
                check_prereqs
                clean_local_build
                exit 0 ;;
            --srpm-only)
                mode="srpm-only" ;;
            *)
                error "Unknown option: $1" ;;
        esac
        shift
    done

    check_prereqs
    setup_distrobox
    build_srpm
    [[ "$mode" != "srpm-only" ]] && build_rpms
}

main "$@"
