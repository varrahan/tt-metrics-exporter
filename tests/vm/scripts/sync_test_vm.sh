#!/usr/bin/env bash
set -euo pipefail

readonly VM_HOST="127.0.0.1"
readonly VM_PORT=2222
readonly VM_USER="ubuntu"
readonly VM_KEY="$HOME/.ssh/ttsim_vm_ed25519"
readonly VM_REPO_PATH="/home/ubuntu/tt-telemetry"

VM_REPO="$VM_REPO_PATH"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_TEST_VMS="${SCRIPT_DIR}/.."

SSH_OPTS=(-i "$VM_KEY" -o StrictHostKeyChecking=no -p "$VM_PORT")

SSH_OPTS=( -i "$VM_KEY" -o StrictHostKeyChecking=no -p "$VM_PORT" )
SCP_OPTS=( -i "$VM_KEY" -o StrictHostKeyChecking=no -P "$VM_PORT" )

ssh "${SSH_OPTS[@]}" "$VM_USER@$VM_HOST" "rm -rf '$VM_REPO/tests/vm' && mkdir -p '$VM_REPO/tests'"
scp "${SCP_OPTS[@]}" -r "$LOCAL_TEST_VMS" "$VM_USER@$VM_HOST:$VM_REPO/tests/"
