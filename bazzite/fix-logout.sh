#!/bin/bash
set -euo pipefail

# =============================================================================
# Fix KineticWE logout black screen on Bazzite / Universal Blue
# =============================================================================
# The stock start-kineticwe passes the startup payload as a positional argument
# (startDetached, no lifecycle tracking) and runs Noctalia in the background.
# When the user clicks "Logout", Noctalia exits but the compositor keeps
# running with no shell → black screen.
#
# This fix:
#   1. Switches to --exit-with-session so the compositor watches the payload
#   2. Runs Noctalia in the foreground so the payload blocks until logout
#
# Usage:
#   ./fix-logout.sh
#   Log out, log back in, then test "Logout" from Noctalia's menu.
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

# Create fixed launcher in /usr/local (persistent across reboots on bootc)
step "Creating fixed session launcher at /usr/local/bin/start-kineticwe ..."
sudo cp /usr/bin/start-kineticwe /usr/local/bin/start-kineticwe
sudo sed -i 's/noctalia &/noctalia/' /usr/local/bin/start-kineticwe
sudo sed -i 's|--xwayland "|--xwayland --exit-with-session "|' /usr/local/bin/start-kineticwe
sudo chmod +x /usr/local/bin/start-kineticwe

# Create session desktop file in /usr/local/share so it overrides the system one
step "Creating session entry at /usr/local/share/wayland-sessions/ ..."
sudo mkdir -p /usr/local/share/wayland-sessions
cat > /tmp/kw-session.desktop << 'EOF'
[Desktop Entry]
Name=KineticWE
Comment=KineticWE Wayland session
Exec=/usr/local/bin/start-kineticwe
Type=Application
DesktopNames=KineticWE:KDE
EOF
sudo cp /tmp/kw-session.desktop /usr/local/share/wayland-sessions/kineticwe.desktop
rm /tmp/kw-session.desktop

echo
info "Fix applied! Log out and log back in, then test Logout from Noctalia."
echo
info "If you see duplicate 'KineticWE' entries in the session menu,"
info "select the one from /usr/local/share (it uses the fixed launcher)."
