#!/usr/bin/env bash

set -euo pipefail

if (( $# < 1 || $# > 2 )); then
    echo "Usage: $0 <qsanguosha_server> [log-file]" >&2
    exit 2
fi

server=$1
log_file=${2:-server-console-smoke.log}
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
config_root=$(mktemp -d "$temp_base/qsanguosha-console-smoke.XXXXXX")
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

# Exercise the INI overlay and the CLI ephemeral-port override together.
server_config="$config_root/server.ini"
printf '[General]\nGameMode=02p\nBindAddress=127.0.0.1\n' > "$server_config"

set +e
printf '%s\n' \
    help \
    status \
    players \
    rooms \
    'say console smoke' \
    'kick missing' \
    shutdown \
    | XDG_CONFIG_HOME="$config_root" timeout \
        --preserve-status \
        --signal=TERM \
        --kill-after=10s \
        "${timeout_seconds}s" \
        "$server" --config "$server_config" --port 0 --websocket-port 0 >"$log_file" 2>&1
status=${PIPESTATUS[1]}
set -e

cat "$log_file"

if (( status != 0 )); then
    echo "Server console smoke failed (exit=$status)" >&2
    exit 1
fi
for expected in \
    'Available commands:' \
    'Listening:' \
    'No players connected.' \
    'ID  STATE' \
    'Administrator broadcast: console smoke' \
    'Broadcast sent.' \
    'Player not found: missing' \
    'Shutdown requested by console.'
do
    if ! grep -Fq "$expected" "$log_file"; then
        echo "Missing console output: $expected" >&2
        exit 1
    fi
done
if ! grep -Eq '^QSanguosha Server [0-9]+$' "$log_file"; then
    echo 'Missing versioned server console header' >&2
    exit 1
fi
if ! grep -Eq 'Listening on 127\.0\.0\.1:[1-9][0-9]*' "$log_file"; then
    echo 'Server did not report its actual ephemeral endpoint' >&2
    exit 1
fi

echo '[server-console-smoke] all seven commands passed'
