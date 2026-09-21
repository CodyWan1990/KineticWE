#!/bin/bash
set -euo pipefail

# =============================================================================
# kineticwe-setup-greeter
# =============================================================================
# Interactive setup script for the Noctalia greeter (greetd).
# Shipped by the kineticwe-greeter RPM. Run manually after install.
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

GREETER_BIN="/usr/bin/noctalia-greeter-session"
GREETD_CFG="/etc/greetd/config.toml"

# --- Resolve the user greetd runs the greeter as ---
resolve_greeter_user() {
    local user=""
    if [[ -f /etc/greetd/config.toml ]]; then
        user=$(awk '
            /^\[default_session\]/  { in_block=1; next }
            /^\[/                   { in_block=0 }
            in_block && /^[[:space:]]*user[[:space:]]*=/ {
                sub(/^[[:space:]]*user[[:space:]]*=[[:space:]]*"/, "")
                sub(/".*$/, "")
                print; exit
            }
        ' /etc/greetd/config.toml 2>/dev/null || true)
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

# --- Prompt ---
echo
echo -e "${BLUE}=========================================="
echo "  Noctalia Greeter Setup"
echo -e "==========================================${NC}"
echo
echo "This will configure greetd to use the Noctalia greeter"
echo "as your display manager."
echo
echo -e "${YELLOW}WARNING: This will change your current display manager.${NC}"
echo
read -r -p "Do you want to install the noctalia greeter? [y/N] " answer

case "$answer" in
    [yY][eE][sS]|[yY])
        ;;
    *)
        info "Aborted."
        exit 0
        ;;
esac

# --- Check greeter binary exists ---
if [[ ! -x "$GREETER_BIN" ]]; then
    error "Greeter binary not found: $GREETER_BIN"
fi

# --- Install greetd if missing ---
if ! command -v greetd &>/dev/null; then
    info "Installing greetd..."
    sudo dnf install -y greetd || error "Failed to install greetd."
else
    info "greetd already installed."
fi

# --- Resolve greeter user ---
greeter_user=$(resolve_greeter_user)
info "Greeter user: ${greeter_user}"

# --- Create greeter user if needed ---
if ! id "$greeter_user" &>/dev/null; then
    info "Creating user '${greeter_user}'..."
    sudo useradd -r -s /usr/bin/nologin -d /var/lib/noctalia-greeter "$greeter_user" \
        || warn "Could not create user '${greeter_user}'."
fi

# --- Add to video/input groups ---
for grp in video input; do
    if getent group "$grp" &>/dev/null; then
        if ! id -Gn "$greeter_user" 2>/dev/null | grep -qw "$grp"; then
            info "Adding '${greeter_user}' to group '${grp}'..."
            sudo usermod -a -G "$grp" "$greeter_user" \
                || warn "Could not add '${greeter_user}' to ${grp}."
        fi
    fi
done

# --- Write greetd config ---
info "Writing ${GREETD_CFG}..."
sudo mkdir -p "$(dirname "$GREETD_CFG")"

if [[ -f "$GREETD_CFG" ]] && grep -q "noctalia-greeter-session" "$GREETD_CFG"; then
    info "greetd already configured for noctalia-greeter-session."
else
    tmp_cfg=$(mktemp)
    if [[ -f "$GREETD_CFG" ]]; then
        cp "$GREETD_CFG" "$tmp_cfg"
    else
        cat > "$tmp_cfg" <<EOF
[terminal]
vt = 1

[default_session]
command = ""
user = "${greeter_user}"
EOF
    fi

    awk -v session="$GREETER_BIN" -v guser="$greeter_user" '
        BEGIN { in_block = 0 }
        /^\[default_session\]/ { in_block = 1; print; next }
        /^\[/ { in_block = 0 }
        in_block && /^[[:space:]]*command[[:space:]]*=/ {
            print "command = \"" session "\""; next
        }
        in_block && /^[[:space:]]*user[[:space:]]*=/ {
            print "user = \"" guser "\""; next
        }
        { print }
    ' "$tmp_cfg" > "${tmp_cfg}.new"

    if ! grep -q '^\[default_session\]' "${tmp_cfg}.new"; then
        printf '\n[default_session]\ncommand = "%s"\nuser = "%s"\n' "$GREETER_BIN" "$greeter_user" >> "${tmp_cfg}.new"
    fi

    sudo install -m 0644 "${tmp_cfg}.new" "$GREETD_CFG"
    rm -f "$tmp_cfg" "${tmp_cfg}.new"
    info "Wrote ${GREETD_CFG}"
fi

# --- Set default boot target ---
if [[ "$(systemctl get-default)" != "graphical.target" ]]; then
    info "Setting default boot target to graphical.target..."
    sudo systemctl set-default graphical.target
fi

# --- Enable greetd ---
current_dm=$(readlink -f /etc/systemd/system/display-manager.service 2>/dev/null || true)
if [[ "$current_dm" == *greetd.service* ]]; then
    info "greetd is already the active display manager."
else
    if [[ -n "$current_dm" ]]; then
        unit=$(basename "$current_dm")
        info "Disabling current display manager: ${unit}"
        sudo systemctl disable "$unit" 2>/dev/null || true
    fi
    info "Enabling greetd..."
    sudo systemctl enable greetd.service
    # greetd.service doesn't declare Alias=display-manager.service, so
    # graphical.target never finds it. Create the symlink explicitly.
    sudo ln -sf /usr/lib/systemd/system/greetd.service \
        /etc/systemd/system/display-manager.service
    info "Linked display-manager.service -> greetd.service"
fi

echo
info "Done! Reboot to use the Noctalia greeter."
echo
