#!/bin/bash
set -euo pipefail

# =============================================================================
# fix-locale-domain.sh
# =============================================================================
# Renames the xdg-desktop-portal-kwe translation domain from the upstream
# KDE name (xdg-desktop-portal-kde) to avoid file conflicts when both
# xdg-desktop-portal-kwe and xdg-desktop-portal-kde are installed.
#
# Run this after pulling from upstream:
#   ./fix-locale-domain.sh
#
# Operates on: /home/lgl/kineticwe/portal/xdg-desktop-portal-kwe/
# =============================================================================

PORTAL_DIR="/home/lgl/kineticwe/portal/xdg-desktop-portal-kwe"
OLD_DOMAIN="xdg-desktop-portal-kde"
NEW_DOMAIN="xdg-desktop-portal-kwe"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

[[ -d "$PORTAL_DIR" ]] || error "Portal source not found: $PORTAL_DIR"

# --- 1. Fix TRANSLATION_DOMAIN in src/CMakeLists.txt ---
CMAKE_FILE="${PORTAL_DIR}/src/CMakeLists.txt"
if grep -q "TRANSLATION_DOMAIN=\"${OLD_DOMAIN}\"" "$CMAKE_FILE"; then
    sed -i "s/TRANSLATION_DOMAIN=\"${OLD_DOMAIN}\"/TRANSLATION_DOMAIN=\"${NEW_DOMAIN}\"/" "$CMAKE_FILE"
    info "Fixed TRANSLATION_DOMAIN in src/CMakeLists.txt"
else
    warn "TRANSLATION_DOMAIN already set or not found in src/CMakeLists.txt"
fi

# --- 2. Fix Messages.sh ---
MESSAGES_FILE="${PORTAL_DIR}/Messages.sh"
if grep -q "${OLD_DOMAIN}\.pot" "$MESSAGES_FILE"; then
    sed -i "s/${OLD_DOMAIN}\.pot/${NEW_DOMAIN}.pot/" "$MESSAGES_FILE"
    info "Fixed .pot filename in Messages.sh"
else
    warn ".pot filename already fixed or not found in Messages.sh"
fi

# --- 3. Rename .po files ---
PO_DIR="${PORTAL_DIR}/po"
renamed=0
if [[ -d "$PO_DIR" ]]; then
    while IFS= read -r -d '' po_file; do
        new_file="${po_file%/${OLD_DOMAIN}.po}/${NEW_DOMAIN}.po"
        if [[ "$po_file" != "$new_file" ]]; then
            mv "$po_file" "$new_file"
            renamed=$((renamed + 1))
        fi
    done < <(find "$PO_DIR" -name "${OLD_DOMAIN}.po" -print0)
    info "Renamed ${renamed} .po files"
else
    warn "po/ directory not found"
fi

# --- 4. Update Project-Id-Version inside .po files ---
updated=0
if [[ -d "$PO_DIR" ]]; then
    while IFS= read -r -d '' po_file; do
        if grep -q "Project-Id-Version: ${OLD_DOMAIN}" "$po_file"; then
            sed -i "s/Project-Id-Version: ${OLD_DOMAIN}/Project-Id-Version: ${NEW_DOMAIN}/" "$po_file"
            updated=$((updated + 1))
        fi
    done < <(find "$PO_DIR" -name "${NEW_DOMAIN}.po" -print0)
    info "Updated Project-Id-Version in ${updated} .po files"
fi

echo
info "Done. Translation domain renamed: ${OLD_DOMAIN} -> ${NEW_DOMAIN}"
info "Build the portal to verify: the .mo files will now be installed as ${NEW_DOMAIN}.mo"
