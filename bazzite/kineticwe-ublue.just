# vim: set ft=make :

# Install KineticWE KWin fork with Noctalia shell on Universal Blue systems
# (Bazzite, Bluefin, Aurora, Kinoite, Silverblue).
# Enables COPR repos and layers via rpm-ostree.
# Options: status | install | uninstall
install-kineticwe ACTION="":
    #!/usr/bin/env bash
    set -euo pipefail

    source /usr/lib/ujust/ujust.sh

    COPR_NOCTALIA="lionheartp/Hyprland"
    COPR_KINETICWE="theblackdon/kineticwe"
    PACKAGES=("kineticwe" "noctalia-git")

    repo_enabled() {
        local copr="$1"
        local repo_file="/etc/yum.repos.d/_copr:copr.fedorainfracloud.org:$(echo "$copr" | tr '/' ':').repo"
        [[ -f "$repo_file" ]]
    }

    layered_package() {
        local pkg="$1"
        rpm-ostree status --json | jq -e ".deployments[] | select(.booted == true) | .packages | index(\"$pkg\")" > /dev/null 2>&1
    }

    base_package() {
        local pkg="$1"
        rpm -q "$pkg" &>/dev/null && ! layered_package "$pkg"
    }

    BASE_CONFLICTS=(kwin kwin-common kwin-libs kglobalacceld)

    all_layered() {
        for pkg in "${PACKAGES[@]}"; do
            if ! layered_package "$pkg"; then
                return 1
            fi
        done
        return 0
    }

    any_layered() {
        for pkg in "${PACKAGES[@]}"; do
            if layered_package "$pkg"; then
                return 0
            fi
        done
        return 1
    }

    get_status_token() {
        if all_layered; then
            echo "install"
        else
            echo "uninstall"
        fi
    }

    get_current_status() {
        if all_layered; then
            echo "Installed"
        elif any_layered; then
            echo "Partially Installed"
        else
            echo "Not Installed"
        fi
    }

    enable_coprs() {
        for copr in "$COPR_NOCTALIA" "$COPR_KINETICWE"; do
            if repo_enabled "$copr"; then
                echo "COPR $copr is already enabled, skipping."
            else
                echo "Enabling COPR $copr ..."
                sudo dnf copr enable -y "$copr"
            fi
        done
    }

    install_kineticwe() {
        echo '{{ YELLOW }}This will layer KineticWE + Noctalia via rpm-ostree.{{ NORMAL }}'
        echo '{{ YELLOW }}A reboot is required after installation.{{ NORMAL }}'

        enable_coprs

        local to_install=()
        for pkg in "${PACKAGES[@]}"; do
            if layered_package "$pkg"; then
                echo "$pkg is already layered, skipping."
            else
                to_install+=("$pkg")
            fi
        done

        if [[ ${#to_install[@]} -gt 0 ]]; then
            local base_conflicts=()
            for pkg in "${BASE_CONFLICTS[@]}"; do
                if base_package "$pkg"; then
                    base_conflicts+=("$pkg")
                fi
            done

            if [[ ${#base_conflicts[@]} -gt 0 ]]; then
                echo "Replacing base packages: ${base_conflicts[*]}"
                echo "Installing: ${to_install[*]}"
                sudo rpm-ostree override remove "${base_conflicts[@]}" \
                    $(printf -- '--install=%s ' "${to_install[@]}")
            else
                echo "Installing: ${to_install[*]}"
                sudo rpm-ostree install -y "${to_install[@]}"
            fi
            echo '{{ BOLD }}Complete! Reboot to apply: systemctl reboot{{ NORMAL }}'
        else
            echo "No changes were made."
        fi
    }

    uninstall_kineticwe() {
        local to_remove=()
        for pkg in "${PACKAGES[@]}"; do
            if layered_package "$pkg"; then
                to_remove+=("$pkg")
            fi
        done

        if [[ ${#to_remove[@]} -gt 0 ]]; then
            echo "Uninstalling: ${to_remove[*]}"
            sudo rpm-ostree uninstall -y "${to_remove[@]}"
            echo '{{ YELLOW }}Note: overridden base packages (kwin, kglobalacceld) are{{ NORMAL }}'
            echo '{{ YELLOW }}not restored automatically. Run to restore defaults:{{ NORMAL }}'
            echo '{{ BOLD }}  sudo rpm-ostree override reset kwin kwin-common kwin-libs kglobalacceld{{ NORMAL }}'
            echo '{{ BOLD }}Complete! Reboot to apply: systemctl reboot{{ NORMAL }}'
        else
            echo "KineticWE is not layered."
        fi
    }

    OPTION="{{ ACTION }}"
    if [[ "$OPTION" == "help" ]]; then
        echo "Usage: ujust install-kineticwe <option>"
        echo "  <option>: Specify the quick option to skip the prompt"
        echo "  Use 'status' to print the current status token"
        echo "  Use 'install' to install KineticWE + Noctalia"
        echo "  Use 'uninstall' to uninstall KineticWE + Noctalia"
        exit 0
    elif [[ "$OPTION" == "status" ]]; then
        get_status_token
        exit 0
    elif [[ "$OPTION" == "install" ]]; then
        install_kineticwe
        exit 0
    elif [[ "$OPTION" == "uninstall" ]]; then
        uninstall_kineticwe
        exit 0
    elif [[ -z "$OPTION" ]]; then
        current_status=$(get_current_status)
        echo "{{ BOLD }}KineticWE{{ NORMAL }}"
        echo "Current status: $current_status"
        local choice
        choice=$(ugum choose "Install" "Uninstall" "Exit without changes")
        case "$choice" in
            "Install")
                install_kineticwe
                ;;
            "Uninstall")
                uninstall_kineticwe
                ;;
            *)
                echo "No changes made."
                ;;
        esac
        exit 0
    fi

# Install SDDM display manager (replaces plasmalogin) on Universal Blue systems.
# KineticWE works best with SDDM.
# Options: status | install | uninstall
install-sddm ACTION="":
    #!/usr/bin/env bash
    set -euo pipefail

    source /usr/lib/ujust/ujust.sh

    layered_package() {
        local pkg="$1"
        rpm-ostree status --json | jq -e ".deployments[] | select(.booted == true) | .packages | index(\"$pkg\")" > /dev/null 2>&1
    }

    get_status_token() {
        if layered_package sddm; then
            echo "install"
        else
            echo "uninstall"
        fi
    }

    get_current_status() {
        if systemctl is-active sddm &>/dev/null; then
            echo "Active (sddm)"
        elif layered_package sddm; then
            echo "Layered but plasmalogin still active"
        else
            echo "Not installed"
        fi
    }

    install_sddm() {
        echo '{{ YELLOW }}This will layer SDDM and switch from plasmalogin.{{ NORMAL }}'
        echo '{{ YELLOW }}A reboot is required after installation.{{ NORMAL }}'

        if layered_package sddm; then
            echo "SDDM is already layered."
        else
            echo "Layering SDDM and SDDM KCM..."
            sudo rpm-ostree install -y sddm sddm-kcm
            echo '{{ YELLOW }}Reboot first, then run again to activate SDDM.{{ NORMAL }}'
            return 0
        fi

        if systemctl is-active plasmalogin &>/dev/null; then
            echo "Stopping plasmalogin..."
            sudo systemctl disable --now plasmalogin
        fi

        if [[ ! -L /etc/systemd/system/display-manager.service ]] || \
           [[ "$(readlink /etc/systemd/system/display-manager.service)" != "/usr/lib/systemd/system/sddm.service" ]]; then
            echo "Switching display manager to SDDM..."
            sudo ln -sf /usr/lib/systemd/system/sddm.service /etc/systemd/system/display-manager.service
            sudo systemctl enable sddm
        fi

        if ! systemctl is-active sddm &>/dev/null; then
            echo "Starting SDDM..."
            sudo systemctl start sddm
        fi

        echo '{{ BOLD }}SDDM is now active! Log out to see the SDDM login screen.{{ NORMAL }}'
    }

    uninstall_sddm() {
        if layered_package sddm; then
            echo "Switching back to plasmalogin..."
            sudo systemctl disable --now sddm 2>/dev/null || true
            sudo ln -sf /usr/lib/systemd/system/plasmalogin.service /etc/systemd/system/display-manager.service
            sudo systemctl enable plasmalogin
            echo "Removing SDDM packages. Reboot to apply."
            sudo rpm-ostree uninstall -y sddm sddm-kcm
        else
            echo "SDDM is not layered."
        fi
    }

    OPTION="{{ ACTION }}"
    if [[ "$OPTION" == "help" ]]; then
        echo "Usage: ujust install-sddm <option>"
        echo "  Use 'status' to check current display manager"
        echo "  Use 'install' to install and activate SDDM"
        echo "  Use 'uninstall' to revert to plasmalogin"
        exit 0
    elif [[ "$OPTION" == "status" ]]; then
        get_status_token
        exit 0
    elif [[ "$OPTION" == "install" ]]; then
        install_sddm
        exit 0
    elif [[ "$OPTION" == "uninstall" ]]; then
        uninstall_sddm
        exit 0
    elif [[ -z "$OPTION" ]]; then
        current_status=$(get_current_status)
        echo "{{ BOLD }}Display Manager{{ NORMAL }}"
        echo "Current status: $current_status"
        local choice
        choice=$(ugum choose "Install SDDM" "Uninstall SDDM" "Exit without changes")
        case "$choice" in
            "Install SDDM")
                install_sddm
                ;;
            "Uninstall SDDM")
                uninstall_sddm
                ;;
            *)
                echo "No changes made."
                ;;
        esac
        exit 0
    fi
