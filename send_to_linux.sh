#!/bin/bash
# Deploy pkgj_cli or pkgj_sim from ci/buildhost to a local Linux destination.
#
# Usage:
#   ./send_to_linux.sh                  # interactive: pick from ci/buildhost/
#   ./send_to_linux.sh ci/buildhost/pkgj_cli
#   ./send_to_linux.sh ci/buildhost/pkgj_sim
#   DEPLOY_DIR=/opt/pkgj ./send_to_linux.sh

set -e

DEPLOY_DIR="${DEPLOY_DIR:-/home/ubuntu/.local/bin}"
BINARY="${1:-}"
USED_DEFAULT=0

COLOR_RESET="\033[0m"
COLOR_GREEN="\033[1;32m"
COLOR_YELLOW="\033[1;33m"
COLOR_CYAN="\033[1;36m"
COLOR_RED="\033[1;31m"

get_file_timestamp() {
    local path="$1"
    local modified
    modified=$(stat -c '%y' "$path" 2>/dev/null || true)
    if [ -n "$modified" ]; then
        printf '%s' "${modified%%.*}"
    else
        printf '%s' "unknown"
    fi
}

choose_binary_interactively() {
    local choices=()
    local choice
    local index=1
    local newest_path=""
    local newest_mtime=0
    local path_mtime

    while IFS= read -r path; do
        choices+=("$path")
        path_mtime=$(stat -c '%Y' "$path" 2>/dev/null || printf '0')
        if [ "$path_mtime" -gt "$newest_mtime" ]; then
            newest_mtime="$path_mtime"
            newest_path="$path"
        fi
    done < <({
        find ci/build ci/buildtest -maxdepth 1 -type f -name '*.vpk'
        find ci/buildhost -maxdepth 1 -type f \( -name 'pkgj_cli' -o -name 'pkgj_sim' \)
    } | sort)

    if [ ${#choices[@]} -eq 0 ]; then
        echo -e "${COLOR_RED}[!] No supported build outputs found.${COLOR_RESET}"
        echo "    Expected .vpk packages in ci/build or ci/buildtest, or pkgj_cli/pkgj_sim in ci/buildhost."
        exit 1
    fi

    echo "[*] No binary argument provided. Available builds:"
    for path in "${choices[@]}"; do
        if [ "$path" = "$newest_path" ]; then
            printf '    %d) %b%s%b  [%s]  %b%s%b\n' \
                "$index" \
                "$COLOR_GREEN" "$path" "$COLOR_RESET" \
                "$(get_file_timestamp "$path")" \
                "$COLOR_YELLOW" "<-- newest" "$COLOR_RESET"
        else
            printf '    %d) %s  [%s]\n' \
                "$index" "$path" "$(get_file_timestamp "$path")"
        fi
        index=$((index + 1))
    done

    while true; do
        printf '[?] Choose a binary to deploy [1-%d] (Enter for newest): ' "${#choices[@]}"
        read -r choice

        if [ -z "$choice" ]; then
            BINARY="$newest_path"
            return
        fi

        if [[ "$choice" =~ ^[0-9]+$ ]] && \
           [ "$choice" -ge 1 ] && \
           [ "$choice" -le "${#choices[@]}" ]; then
            BINARY="${choices[$((choice - 1))]}"
            return
        fi

        echo -e "${COLOR_YELLOW}[!] Invalid choice.${COLOR_RESET} Enter a number from the list or press Enter for newest."
    done
}

# ── Pick binary ───────────────────────────────────────────────────────────────

if [ $# -eq 0 ]; then
    USED_DEFAULT=1
    choose_binary_interactively
fi

if [ ! -f "$BINARY" ]; then
    echo -e "${COLOR_RED}[!] File not found:${COLOR_RESET} $BINARY"
    echo "    Usage: $0 [ci/buildhost/pkgj_cli | ci/buildhost/pkgj_sim]"
    exit 1
fi

DEST_NAME="$(basename "$BINARY")"
DEST_PATH="$DEPLOY_DIR/$DEST_NAME"

REMOTE_HOST="192.168.0.199"

# Removendo sshpass e ajustando para usar apenas ssh com chave
SSH_USER="ubuntu"

# ── Deploy ────────────────────────────────────────────────────────────────────

echo -e "[*] Binary  : ${COLOR_CYAN}${BINARY}${COLOR_RESET}  [$(get_file_timestamp "$BINARY")]"
echo -e "[*] Remote : ${COLOR_GREEN}${SSH_USER}@${REMOTE_HOST}:${DEST_PATH}${COLOR_RESET}"
if [ "$USED_DEFAULT" -eq 1 ]; then
    echo -e "${COLOR_YELLOW}[!] Nenhum argumento fornecido: usando seleção interativa.${COLOR_RESET}"
fi

ssh "$SSH_USER@$REMOTE_HOST" "mkdir -p \"$DEPLOY_DIR\""
scp "$BINARY" "$SSH_USER@$REMOTE_HOST:$DEST_PATH"
ssh "$SSH_USER@$REMOTE_HOST" "chmod +x \"$DEST_PATH\""

echo -e "${COLOR_GREEN}[OK]${COLOR_RESET} Copiado para $SSH_USER@$REMOTE_HOST:$DEST_PATH"

# Inform user if DEPLOY_DIR is not in PATH on the remote machine
if ! echo ":$PATH:" | grep -q ":$DEPLOY_DIR:"; then
    echo -e "\033[1;33m[!] Aviso:\033[0m '$DEPLOY_DIR' não está no seu PATH."
    echo "    Adicione ao ~/.bashrc ou ~/.profile:"
    echo "        export PATH=\"\$PATH:$DEPLOY_DIR\""
fi
