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

relocated_log="$config_root/relocated.log"

set +e
printf '%s\n' \
    help \
    status \
    players \
    rooms \
    'status --json' \
    'players --json' \
    'rooms --json' \
    'log-level debug' \
    'log-format json' \
    'log-format text' \
    "log-file $relocated_log" \
    'maintenance on' \
    'maintenance off' \
    'log-file off' \
    'log-file /qsanguosha-console-smoke-missing/server.log' \
    'addrobot 1' \
    rooms \
    'close 0' \
    'end-game 42' \
    'close 42' \
    'addrobot 1 42' \
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
    'Maintenance:   disabled' \
    'Log level:     info' \
    'No players connected.' \
    'ID  STATE' \
    '{"players":[]}' \
    'Log level: debug' \
    'Log format: json' \
    'Log format: text' \
    "Log file: $relocated_log" \
    'Log file: (stdout)' \
    "Log configuration unchanged: unable to open log file" \
    'Maintenance mode enabled; new signups are refused.' \
    'Maintenance mode disabled.' \
    'Robots added: 1' \
    'Room closed: 0' \
    'Room not found: 42' \
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

# Every --json response must actually parse.
json_lines=$(grep -c '^{' "$log_file" || true)
if (( json_lines < 3 )); then
    echo "Expected at least three machine-readable console replies, saw $json_lines" >&2
    exit 1
fi
while IFS= read -r line; do
    if ! printf '%s' "$line" | python3 -m json.tool >/dev/null; then
        echo "Machine-readable console output is not valid JSON: $line" >&2
        exit 1
    fi
done < <(grep '^{' "$log_file")

# log-file must really move to the new destination; stdout must get no server records afterwards.
if [[ ! -s "$relocated_log" ]]; then
    echo "log-file did not redirect any record to $relocated_log" >&2
    exit 1
fi
if ! grep -q 'Maintenance mode enabled' "$relocated_log" \
    && ! grep -Eq '[0-9]{4}-[0-9]{2}-[0-9]{2}T' "$relocated_log"; then
    echo "Relocated log file holds no server record" >&2
    exit 1
fi
if ! grep -Eq '^QSanguosha Server [0-9]+$' "$log_file"; then
    echo 'Missing versioned server console header' >&2
    exit 1
fi
if ! grep -Eq 'Listening on 127\.0\.0\.1:[1-9][0-9]*' "$log_file"; then
    echo 'Server did not report its actual ephemeral endpoint' >&2
    exit 1
fi

echo '[server-console-smoke] every console command passed'
