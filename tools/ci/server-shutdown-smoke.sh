#!/usr/bin/env bash

set -euo pipefail

if (( $# < 1 || $# > 2 )); then
    echo "Usage: $0 <qsanguosha_server> [log-file]" >&2
    exit 2
fi

server=$1
log_file=${2:-server-shutdown-smoke.log}
timeout_seconds=${QSAN_SERVER_SMOKE_TIMEOUT_SECONDS:-30}

if [[ ! -x "$server" ]]; then
    echo "Server executable does not exist or is not executable: $server" >&2
    exit 2
fi
if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "QSAN_SERVER_SMOKE_TIMEOUT_SECONDS must be a positive integer: $timeout_seconds" >&2
    exit 2
fi

mkdir -p "$(dirname "$log_file")"
temp_base=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
config_root=$(mktemp -d "$temp_base/qsanguosha-server-smoke.XXXXXX")
game_state_path="$PWD/g.json"
game_state_preexisting=0
if [[ -e "$game_state_path" ]]; then
    game_state_preexisting=1
fi
cleanup()
{
    rm -rf -- "$config_root"
    if (( game_state_preexisting == 0 )); then
        rm -f -- "$game_state_path"
    fi
}
trap cleanup EXIT

# Port 0 asks the kernel for an unused port and keeps parallel CI jobs isolated.
mkdir -p "$config_root/QSanguosha.org"
printf '[General]\nServerPort=0\n' > "$config_root/QSanguosha.org/QSanguosha.conf"

set +e
XDG_CONFIG_HOME="$config_root" timeout \
    --preserve-status \
    --signal=TERM \
    --kill-after=10s \
    "${timeout_seconds}s" \
    "$server" >"$log_file" 2>&1
status=$?
set -e

cat "$log_file"

if (( status != 0 )); then
    echo "Server did not shut down cleanly after SIGTERM (exit=$status)" >&2
    exit 1
fi
if ! grep -Fq 'Binding port number is 0' "$log_file"; then
    echo 'Server never reached the listening state' >&2
    exit 1
fi
if ! grep -Fq 'Shutdown requested by signal 15' "$log_file"; then
    echo 'Server did not acknowledge SIGTERM' >&2
    exit 1
fi

echo '[server-shutdown-smoke] startup and SIGTERM shutdown passed'
