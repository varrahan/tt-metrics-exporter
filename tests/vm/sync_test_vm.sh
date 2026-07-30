#!/usr/bin/env bash
set -euo pipefail

VM_HOST="${VM_HOST:-127.0.0.1}"
VM_PORT="${TTSIM_SSH_PORT:-2222}"
VM_USER="${VM_USER:-ubuntu}"
VM_KEY="${VM_SSH_KEY:-$HOME/.ssh/ttsim_vm_ed25519}"
VM_REPO="${VM_REPO_PATH:-/home/ubuntu/tt-telemetry}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_TEST_VMS="${SCRIPT_DIR}"

SSH_OPTS=(-i "$VM_KEY" -o StrictHostKeyChecking=no -p "$VM_PORT")

SSH_OPTS=( -i "$VM_KEY" -o StrictHostKeyChecking=no -p "$VM_PORT" )
SCP_OPTS=( -i "$VM_KEY" -o StrictHostKeyChecking=no -P "$VM_PORT" )

ssh "${SSH_OPTS[@]}" "$VM_USER@$VM_HOST" "rm -rf '$VM_REPO/tests/vm' && mkdir -p '$VM_REPO/tests'"
scp "${SCP_OPTS[@]}" -r "$LOCAL_TEST_VMS" "$VM_USER@$VM_HOST:$VM_REPO/tests/"
