#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - Update Script
# Pulls latest changes from the dev branch and rebuilds.
# =============================================================================
# Environment variables:
#   INSTALL_PREFIX - Where binaries are installed (default: $HOME/.local)
#   KINETICWE_DIR  - Where the repo is cloned (default: $HOME/Kinetic/kineticwe)
# =============================================================================

INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local}"
KINETICWE_DIR="${KINETICWE_DIR:-$HOME/Kinetic/kineticwe}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

if [[ ! -d "$KINETICWE_DIR/.git" ]]; then
    error "KineticWE repo not found at $KINETICWE_DIR"
    error "Run the install script first:"
    error "  bash <(curl -sSL https://gitlab.com/theblackdon/kineticwe/-/raw/dev/install.sh)"
    exit 1
fi

echo "=========================================="
echo "  KineticWE Updater"
echo "=========================================="
echo

cd "$KINETICWE_DIR"

info "Pulling latest changes..."
git pull

info "Building kwin-we..."
bash build-kwin-we.sh "$INSTALL_PREFIX"

echo
echo "=========================================="
echo "  Update complete!"
echo "=========================================="
echo
warn "You must log out or reboot for changes to take effect."
