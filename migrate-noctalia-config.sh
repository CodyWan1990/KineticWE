#!/bin/bash
set -euo pipefail

if (( EUID == 0 )); then
    echo "Run this script as your normal user, not root." >&2
    exit 1
fi

# =============================================================================
# KineticWE - Import upstream Noctalia settings into the Noctalia fork
# =============================================================================
# TODO(full-release): BETA-ONLY. Delete this script at the full KineticWE launch;
# release installs are fresh and have nothing to import.
#
# Manual, opt-in replacement for the startup migration that used to live in
# FileUtils::migrateLegacyDirs() (shell/noctalia/src/util/file_utils.h). Copies
# upstream Noctalia's dirs into the fork's own dirs:
#
#   <XDG_CONFIG_HOME or ~/.config>/noctalia      -> .../noctalia-kwe
#   <XDG_STATE_HOME  or ~/.local/state>/noctalia -> .../noctalia-kwe
#   <XDG_DATA_HOME   or ~/.local/share>/noctalia -> .../noctalia-kwe (minus assets/)
#
# The originals are only read. Files already present in the target are never
# overwritten, symlinks are copied as symlinks, and sockets/FIFOs/device nodes
# are skipped. A dir whose NOCTALIA_*_HOME override is set is skipped, as is the
# cache dir.
#
# A running noctalia-kwe could overwrite the copied settings.toml, so after you
# confirm it is stopped for the copy and restarted afterwards. Under
# start-kineticwe its restart loop is paused (SIGSTOP) while the shell is down
# and resumed afterwards (always, via an EXIT trap), so the loop relaunches the
# shell with the imported settings. A shell started some other way is relaunched
# with setsid when WAYLAND_DISPLAY is set; otherwise you are told to start it.
# The script then waits for the shell to come back and, if it does not (or it
# exits right after starting), prints the command to start it manually and
# exits with status 1. The copied files are kept either way.
#
# A settings.toml written by a newer upstream Noctalia (config_version above
# what the fork supports) makes the fork refuse to load it, so that one file is
# skipped unless --force-settings is given.
#
# Shows what it will copy and asks before changing anything ([y/N]).
#
# Usage:
#   ./migrate-noctalia-config.sh                   # Show the plan, ask, then copy
#   ./migrate-noctalia-config.sh --dry-run         # Show the plan and file list only
#   ./migrate-noctalia-config.sh --force-settings  # Also copy a too-new settings.toml
#   ./migrate-noctalia-config.sh --yes, -y         # Skip the confirmation prompt
#                                                  # (required when not run from a terminal)
#   ./migrate-noctalia-config.sh --help            # Show usage
# =============================================================================

# Highest config_version the vendored shell understands. Keep in sync with
# currentConfigVersion() in shell/noctalia/src/config/config_migrations.cpp
# (the last entry's toVersion) after each upstream merge.
SUPPORTED_CONFIG_VERSION=14

# Seconds to wait for the shell to exit after SIGTERM.
SHELL_STOP_TIMEOUT=10

LEGACY_DIR_NAME="noctalia"
APP_DIR_NAME="noctalia-kwe"

DRY_RUN=false
FORCE_SETTINGS=false
ASSUME_YES=false

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

usage() {
    sed -n '/^# Usage:/,/^# ====/p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'
}

# ---------------------------------------------------------------------------
# Output helpers (same look as install-arch.sh). Colour and box-drawing glyphs
# only on a UTF-8 terminal without NO_COLOR; plain ASCII otherwise.
# ---------------------------------------------------------------------------
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

welcome_title() {
    echo
    summary_rule
    printf '  %sKineticWE: %s%s\n' "$U_B" "$1" "$U_R"
    summary_rule
}

# summary_title <text>: a heading between two rules.
summary_title() {
    echo
    summary_rule
    printf '  %s%s %s%s\n' "$U_OK" "$G_OK" "$1" "$U_R"
    summary_rule
}

# summary_box <line>...: an emphasised box. A line starting with "$ " is shown
# as a command. The box is at least SUMMARY_WIDTH wide and grows to fit the
# longest line, so a command is never wrapped and stays copy-pasteable.
summary_box() {
    local inner=$(( SUMMARY_WIDTH - 6 )) line text colour pad
    for line in "$@"; do
        text="$line"
        [[ "$line" != '$ '* ]] || text="     ${line#\$ }"
        (( ${#text} <= inner )) || inner=${#text}
    done
    printf '  %s%s%s%s%s\n' "$U_WARN" "$G_TL" "$(repeat "$G_H" $(( inner + 2 )))" "$G_TR" "$U_R"
    for line in "$@"; do
        colour="$U_B"
        text="$line"
        if [[ "$line" == '$ '* ]]; then
            text="     ${line#\$ }"
            colour="$U_CMD"
        fi
        pad=$(( inner - ${#text} ))
        printf '  %s%s%s %s%s%s%*s %s%s%s\n' "$U_WARN" "$G_V" "$U_R" \
            "$colour" "$text" "$U_R" "$pad" '' "$U_WARN" "$G_V" "$U_R"
    done
    printf '  %s%s%s%s%s\n' "$U_WARN" "$G_BL" "$(repeat "$G_H" $(( inner + 2 )))" "$G_BR" "$U_R"
}

summary_ok()   { printf '  %s%s%s %s\n' "$U_OK" "$G_OK" "$U_R" "$*"; }
summary_warn() { printf '  %s%s %s%s\n' "$U_WARN" "$G_WARN" "$*" "$U_R"; }
# summary_row <label> <value>
summary_row()  { printf '    %-14s %s\n' "$1" "$2"; }

# files <n>: "1 file" / "<n> files".
files() { if (( $1 == 1 )); then printf '1 file'; else printf '%s files' "$1"; fi; }

# tilde <path>: shortens $HOME to ~ for display.
tilde() {
    if [[ -n "${HOME:-}" && "$1" == "$HOME"/* ]]; then
        printf '~/%s' "${1#"$HOME"/}"
    else
        printf '%s' "$1"
    fi
}

# ---------------------------------------------------------------------------
# Scan (read-only): fills the DIR_* and FILE_* arrays.
# ---------------------------------------------------------------------------
DIR_LABEL=() DIR_LEGACY=() DIR_TARGET=() DIR_STATE=() DIR_COUNT=() DIR_FAILURE=() DIR_COPIED=()
FILE_DIR=() FILE_SRC=() FILE_DEST=()
MKDIR_DIR=() MKDIR_PATH=()
SETTINGS_SKIPPED="" SETTINGS_SKIPPED_VERSION=""

# Prints the integer config_version of a settings.toml, or nothing if absent.
config_version_of() {
    sed -n 's/^[[:space:]]*config_version[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$1" | head -n 1
}

# scan_dir <label> <override env> <xdg env> <home suffix> <skip assets: true|false>
scan_dir() {
    local label="$1" override_env="$2" xdg_env="$3" home_suffix="$4" skip_assets="$5"
    local idx=${#DIR_LABEL[@]}
    DIR_LABEL+=("$label") DIR_LEGACY+=("") DIR_TARGET+=("") DIR_COUNT+=(0) DIR_FAILURE+=("") DIR_COPIED+=(0)

    if [[ -n "${!override_env:-}" ]]; then
        DIR_STATE+=("override:${override_env}")
        return 0
    fi

    local base
    if [[ -n "${!xdg_env:-}" ]]; then
        base="${!xdg_env}"
    elif [[ -n "${HOME:-}" ]]; then
        base="${HOME}/${home_suffix}"
    else
        DIR_STATE+=("missing")
        return 0
    fi

    local legacy="${base}/${LEGACY_DIR_NAME}"
    local target="${base}/${APP_DIR_NAME}"
    DIR_LEGACY[idx]="$legacy"
    DIR_TARGET[idx]="$target"
    if [[ ! -d "$legacy" ]]; then
        DIR_STATE+=("missing")
        return 0
    fi
    DIR_STATE+=("ok")

    local find_args=("$legacy" -mindepth 1)
    if [[ "$skip_assets" == true ]]; then
        find_args+=( \( -path "${legacy}/assets" -prune \) -o )
    fi
    find_args+=( -print0 )

    local count=0 src rel dest version
    while IFS= read -r -d '' src; do
        rel="${src#"${legacy}"/}"
        dest="${target}/${rel}"

        if [[ -d "$src" && ! -L "$src" ]]; then
            if [[ ! -d "$dest" ]]; then
                MKDIR_DIR+=("$idx") MKDIR_PATH+=("$dest")
            fi
            continue
        fi

        # Only regular files and symlinks; skip sockets, FIFOs and device nodes.
        if [[ ! -L "$src" && ! -f "$src" ]]; then
            continue
        fi

        # Never overwrite (or follow a dangling symlink in) the target.
        if [[ -e "$dest" || -L "$dest" ]]; then
            continue
        fi

        if [[ "$rel" == "settings.toml" && ! -L "$src" && "$FORCE_SETTINGS" == false ]]; then
            version="$(config_version_of "$src")"
            if [[ -n "$version" ]] && (( version > SUPPORTED_CONFIG_VERSION )); then
                SETTINGS_SKIPPED="$src" SETTINGS_SKIPPED_VERSION="$version"
                continue
            fi
        fi

        FILE_DIR+=("$idx") FILE_SRC+=("$src") FILE_DEST+=("$dest")
        count=$((count + 1))
    done < <(find "${find_args[@]}")
    DIR_COUNT[idx]=$count
}

# ---------------------------------------------------------------------------
# Running shell: stop for the copy, restart afterwards
# ---------------------------------------------------------------------------
SHELL_PIDS=() SUPERVISOR_PIDS=() SUPERVISOR_PIDS_RESUMED=()
SHELL_UNSUPERVISED=false SHELL_RELAUNCH=""
# After a restart: running | missing (never came back) | crashed (exited right
# after starting) | manual (not relaunched: no Wayland session here).
SHELL_RESULT=""
SHELL_LOG="${HOME}/.local/share/${APP_DIR_NAME}.log"

detect_shell() {
    mapfile -t SHELL_PIDS < <(pgrep -u "$EUID" -x "$APP_DIR_NAME" || true)
}

# is_supervisor <pid>: true if <pid> is start-kineticwe's shell restart loop.
is_supervisor() {
    local args
    args="$(ps -o args= -p "$1" 2>/dev/null || true)"
    [[ "$args" == *start-kineticwe* ]]
}

# proc_gone <pid>: true once <pid> has exited. A paused parent cannot reap it,
# so a zombie counts as gone.
proc_gone() {
    local stat
    stat="$(cat "/proc/$1/stat" 2>/dev/null)" || return 0
    stat="${stat##*) }"
    [[ "${stat%% *}" == Z ]]
}

resume_supervisors() {
    local pid
    for pid in "${SUPERVISOR_PIDS[@]}"; do
        kill -CONT "$pid" 2>/dev/null || true
    done
    SUPERVISOR_PIDS=()
}

stop_shell() {
    local pid ppid i alive
    for pid in "${SHELL_PIDS[@]}"; do
        ppid="$(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' ')"
        if [[ -n "$ppid" ]] && is_supervisor "$ppid"; then
            kill -STOP "$ppid"
            SUPERVISOR_PIDS+=("$ppid")
        else
            SHELL_UNSUPERVISED=true
        fi
    done

    info "Stopping the shell for the copy..."
    kill -TERM "${SHELL_PIDS[@]}" 2>/dev/null || true
    for (( i = 0; i < SHELL_STOP_TIMEOUT * 5; i++ )); do
        alive=false
        for pid in "${SHELL_PIDS[@]}"; do
            proc_gone "$pid" || alive=true
        done
        [[ "$alive" == true ]] || return 0
        sleep 0.2
    done

    resume_supervisors
    error "Could not stop the shell. Exiting No Changes Made"
    exit 1
}

restart_shell() {
    if (( ${#SUPERVISOR_PIDS[@]} > 0 )); then
        SUPERVISOR_PIDS_RESUMED=("${SUPERVISOR_PIDS[@]}")
        resume_supervisors
        SHELL_RELAUNCH="supervised"
    fi
    if [[ "$SHELL_UNSUPERVISED" == true ]]; then
        if [[ -n "${WAYLAND_DISPLAY:-}" ]] && command -v "$APP_DIR_NAME" >/dev/null 2>&1; then
            setsid -f "$APP_DIR_NAME" >>"$SHELL_LOG" 2>&1 </dev/null
            SHELL_RELAUNCH="${SHELL_RELAUNCH:+$SHELL_RELAUNCH+}relaunched"
        else
            SHELL_RELAUNCH="${SHELL_RELAUNCH:+$SHELL_RELAUNCH+}manual"
        fi
    fi
}

# wait_for_shell: sets SHELL_RESULT. Waits up to 10 s for a new shell process
# (the restart loop sleeps 2 s first), then 2 s more to catch one that exits
# right after starting.
wait_for_shell() {
    if [[ "$SHELL_RELAUNCH" == manual ]]; then
        SHELL_RESULT="manual"
        return 0
    fi

    info "Waiting for the shell to come back..."
    local i pid old is_old new_pid=""
    for (( i = 0; i < 50; i++ )); do
        while read -r pid; do
            [[ -n "$pid" ]] || continue
            is_old=false
            for old in "${SHELL_PIDS[@]}"; do
                [[ "$pid" != "$old" ]] || is_old=true
            done
            if [[ "$is_old" == false ]] && ! proc_gone "$pid"; then
                new_pid="$pid"
                break
            fi
        done < <(pgrep -u "$EUID" -x "$APP_DIR_NAME" || true)
        [[ -z "$new_pid" ]] || break
        sleep 0.2
    done

    if [[ -z "$new_pid" ]]; then
        SHELL_RESULT="missing"
        return 0
    fi
    sleep 2
    if proc_gone "$new_pid"; then
        SHELL_RESULT="crashed"
    else
        SHELL_RESULT="running"
    fi
}

# manual_start_box [extra line]...: how to start the shell by hand.
manual_start_box() {
    local log
    log="$(tilde "$SHELL_LOG")"
    summary_box \
        "START THE SHELL MANUALLY" \
        "" \
        "\$ setsid -f ${APP_DIR_NAME} >>${log} 2>&1" \
        "" \
        "Check the log if it does not start:" \
        "\$ tail -n 50 ${log}" \
        "$@"
}

# ---------------------------------------------------------------------------
# Plan screen + confirmation
# ---------------------------------------------------------------------------
show_plan() {
    local i
    welcome_title "import upstream Noctalia settings"
    for i in "${!DIR_LABEL[@]}"; do
        case "${DIR_STATE[i]}" in
            override:*) summary_row "${DIR_LABEL[i]}" "skipped (${DIR_STATE[i]#override:} is set)" ;;
            missing)    summary_row "${DIR_LABEL[i]}" "no upstream dir, nothing to copy" ;;
            *)
                if (( DIR_COUNT[i] == 0 )); then
                    summary_row "${DIR_LABEL[i]}" "nothing to copy"
                else
                    summary_row "${DIR_LABEL[i]}" "$(files "${DIR_COUNT[i]}")"
                    summary_row "" "from $(tilde "${DIR_LEGACY[i]}")"
                    summary_row "" "to   $(tilde "${DIR_TARGET[i]}")"
                fi
                ;;
        esac
    done
    if [[ -n "$SETTINGS_SKIPPED" ]]; then
        echo
        summary_warn "settings.toml will not be copied: config_version ${SETTINGS_SKIPPED_VERSION} is newer"
        echo "    than the supported ${SUPPORTED_CONFIG_VERSION} and the shell would refuse to load it."
        echo "    Re-run with --force-settings to copy it anyway."
    fi
    if (( ${#SHELL_PIDS[@]} > 0 )); then
        echo
        summary_warn "The shell is running: it will be stopped for the copy and restart"
        echo "    automatically (the bar disappears for a few seconds)."
    fi
}

show_will_do() {
    echo
    echo "  This will:"
    echo "    ${G_BULLET} copy the files above into the noctalia-kwe dirs"
    echo "    ${G_BULLET} never overwrite files that already exist there"
    echo "    ${G_BULLET} leave the original noctalia dirs untouched"
    echo
}

show_file_list() {
    local i
    echo
    echo "  Files that would be copied:"
    for i in "${!FILE_SRC[@]}"; do
        printf '    %s\n' "$(tilde "${FILE_DEST[i]}")"
    done
    echo
}

confirm_start() {
    if [[ "$ASSUME_YES" == true ]]; then
        return 0
    fi
    if [[ ! -t 0 ]]; then
        error "Not running in a terminal; re-run with --yes to proceed without confirmation."
        exit 1
    fi
    local answer
    read -r -p "  Continue? [y/N] " answer || answer=""
    case "${answer,,}" in
        y|yes) echo ;;
        *)
            echo
            info "Exiting No Changes Made"
            exit 0
            ;;
    esac
}

# ---------------------------------------------------------------------------
# Copy + summary
# ---------------------------------------------------------------------------
do_copy() {
    local i idx err
    for i in "${!MKDIR_PATH[@]}"; do
        idx="${MKDIR_DIR[i]}"
        if ! err=$(mkdir -p -- "${MKDIR_PATH[i]}" 2>&1); then
            [[ -n "${DIR_FAILURE[idx]}" ]] || DIR_FAILURE[idx]="$err"
        fi
    done
    for i in "${!FILE_SRC[@]}"; do
        idx="${FILE_DIR[i]}"
        if ! err=$(mkdir -p -- "$(dirname -- "${FILE_DEST[i]}")" 2>&1 \
                && cp -P --preserve=mode,timestamps -- "${FILE_SRC[i]}" "${FILE_DEST[i]}" 2>&1); then
            [[ -n "${DIR_FAILURE[idx]}" ]] || DIR_FAILURE[idx]="$err"
            continue
        fi
        DIR_COPIED[idx]=$(( DIR_COPIED[idx] + 1 ))
    done
}

show_summary() {
    local i any_failure=false
    for i in "${!DIR_LABEL[@]}"; do
        [[ -z "${DIR_FAILURE[i]}" ]] || any_failure=true
    done

    if [[ "$any_failure" == true ]]; then
        summary_title "Noctalia settings partially imported"
    else
        summary_title "Noctalia settings imported"
    fi
    for i in "${!DIR_LABEL[@]}"; do
        (( DIR_COUNT[i] > 0 )) || continue
        if [[ -n "${DIR_FAILURE[i]}" ]]; then
            summary_warn "${DIR_LABEL[i]}: copied ${DIR_COPIED[i]} of $(files "${DIR_COUNT[i]}")"
            echo "    first error: ${DIR_FAILURE[i]}"
        else
            summary_ok "${DIR_LABEL[i]}: copied $(files "${DIR_COPIED[i]}") to $(tilde "${DIR_TARGET[i]}")"
        fi
    done
    if [[ -n "$SETTINGS_SKIPPED" ]]; then
        summary_warn "settings.toml not copied (config_version ${SETTINGS_SKIPPED_VERSION} > ${SUPPORTED_CONFIG_VERSION})"
    fi
    case "$SHELL_RESULT" in
        "")
            echo "    Originals left untouched. Start KineticWE to use the imported settings."
            ;;
        running)
            summary_ok "Shell restarted with the imported settings"
            echo "    Originals left untouched."
            ;;
        *)
            case "$SHELL_RESULT" in
                manual)  summary_warn "Shell not restarted: no Wayland session in this terminal" ;;
                missing) summary_warn "Shell did not restart" ;;
                crashed) summary_warn "Shell exited right after starting" ;;
            esac
            echo "    Originals left untouched; the imported files are in place."
            echo
            if [[ "$SHELL_RESULT" == manual ]]; then
                manual_start_box "" "Run it from a terminal inside the KineticWE session."
            elif [[ "$SHELL_RESULT" == crashed ]] && (( ${#SUPERVISOR_PIDS_RESUMED[@]} > 0 )); then
                manual_start_box "" "The session keeps retrying every 2 s; check the log first."
            else
                manual_start_box
            fi
            ;;
    esac
    summary_rule
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=true ;;
        --force-settings) FORCE_SETTINGS=true ;;
        --yes|-y) ASSUME_YES=true ;;
        -h|--help) usage; exit 0 ;;
        *) error "Unknown option: $arg" >&2; usage >&2; exit 1 ;;
    esac
done

# Whatever happens after this point, never leave a paused restart loop behind.
trap resume_supervisors EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

ui_init
detect_shell
scan_dir "Config" NOCTALIA_CONFIG_HOME XDG_CONFIG_HOME ".config" false
scan_dir "State"  NOCTALIA_STATE_HOME  XDG_STATE_HOME  ".local/state" false
scan_dir "Data"   NOCTALIA_DATA_HOME   XDG_DATA_HOME   ".local/share" true

show_plan

if (( ${#FILE_SRC[@]} == 0 )); then
    echo
    info "Nothing to import. Exiting No Changes Made"
    exit 0
fi

if [[ "$DRY_RUN" == true ]]; then
    show_file_list
    info "Dry run: no changes made."
    exit 0
fi

show_will_do
confirm_start
if (( ${#SHELL_PIDS[@]} > 0 )); then
    stop_shell
fi
do_copy
if (( ${#SHELL_PIDS[@]} > 0 )); then
    restart_shell
    wait_for_shell
fi
show_summary

if [[ -n "$SHELL_RESULT" && "$SHELL_RESULT" != running ]]; then
    exit 1
fi
