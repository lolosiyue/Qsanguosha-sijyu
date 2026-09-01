#!/usr/bin/env bash

set -euo pipefail

if (( $# < 1 || $# > 2 )); then
    echo "Usage: $0 <qsanguosha_server> [diagnostic-log]" >&2
    exit 2
fi

server=$1
diagnostic_log=${2:-server-logging-smoke.log}
timeout_seconds=${QSAN_SERVER_SMOKE_TIMEOUT_SECONDS:-30}

if [[ ! -x "$server" ]]; then
    echo "Server executable does not exist or is not executable: $server" >&2
    exit 2
fi
if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "QSAN_SERVER_SMOKE_TIMEOUT_SECONDS must be a positive integer: $timeout_seconds" >&2
    exit 2
fi

mkdir -p "$(dirname "$diagnostic_log")"
temp_base=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
config_root=$(mktemp -d "$temp_base/qsanguosha-logging-smoke.XXXXXX")
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

server_config="$config_root/server.ini"
structured_log="$config_root/server.jsonl"
printf '[General]\nGameMode=02p\nBindAddress=127.0.0.1\n' > "$server_config"

set +e
printf '%s\n' shutdown \
    | XDG_CONFIG_HOME="$config_root" timeout \
        --preserve-status \
        --signal=TERM \
        --kill-after=10s \
        "${timeout_seconds}s" \
        "$server" --config "$server_config" --port 0 --websocket-port 0 \
            --log-level info --log-format json --log-file "$structured_log" \
            >"$diagnostic_log" 2>&1
status=${PIPESTATUS[1]}
set -e

cat "$diagnostic_log"
if (( status != 0 )); then
    echo "Structured logging smoke failed (exit=$status)" >&2
    exit 1
fi

python3 tests/validate-server-log.py --path "$structured_log"
echo '[server-logging-smoke] JSON lifecycle log passed'
