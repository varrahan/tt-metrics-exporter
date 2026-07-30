#!/usr/bin/env bash
set -euo pipefail

VM_HOST="${VM_HOST:-127.0.0.1}"
VM_PORT="${TTSIM_SSH_PORT:-2222}"
VM_USER="${VM_USER:-ubuntu}"
VM_KEY="${VM_SSH_KEY:-$HOME/.ssh/ttsim_vm_ed25519}"
VM_REPO="${VM_REPO_PATH:-/home/ubuntu/tt-telemetry}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_TEST_VMS="${SCRIPT_DIR}/../test/vm"

SSH_OPTS=(-i "$VM_KEY" -o StrictHostKeyChecking=no -p "$VM_PORT")

ssh "${SSH_OPTS[@]}" "$VM_USER@$VM_HOST" "rm -rf '$VM_REPO/test/vm' && mkdir -p '$VM_REPO/test'"
scp "${SSH_OPTS[@]}" -r "$LOCAL_TEST_VMS" "$VM_USER@$VM_HOST:$VM_REPO/test/"
