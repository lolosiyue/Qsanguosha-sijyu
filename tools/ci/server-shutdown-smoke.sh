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
server_pid=
if [[ -e "$game_state_path" ]]; then
    game_state_preexisting=1
fi
cleanup()
{
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill -TERM "$server_pid" 2>/dev/null || true
        for (( cleanup_attempt = 0; cleanup_attempt < 20; ++cleanup_attempt )); do
            if ! kill -0 "$server_pid" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        if kill -0 "$server_pid" 2>/dev/null; then
            kill -KILL "$server_pid" 2>/dev/null || true
        fi
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf -- "$config_root"
    if (( game_state_preexisting == 0 )); then
        rm -f -- "$game_state_path"
    fi
}
trap cleanup EXIT

# Exercise the INI overlay and the CLI ephemeral-port override together.
server_config="$config_root/server.ini"
printf '[General]\nGameMode=02p\nBindAddress=127.0.0.1\n' > "$server_config"

XDG_CONFIG_HOME="$config_root" \
    "$server" --config "$server_config" --port 0 >"$log_file" 2>&1 &
server_pid=$!

listen_port=
for (( startup_attempt = 0; startup_attempt < timeout_seconds * 10; ++startup_attempt )); do
    endpoint_line=$(grep -oEm1 'Listening on 127\.0\.0\.1:[1-9][0-9]*' "$log_file" || true)
    if [[ -n "$endpoint_line" ]]; then
        listen_port=${endpoint_line##*:}
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [[ -z "$listen_port" || "$listen_port" -gt 65535 ]]; then
    cat "$log_file"
    echo 'Server did not report a valid ephemeral listening endpoint' >&2
    exit 1
fi
if ! timeout 5s bash -c 'exec 3<>"/dev/tcp/$1/$2"' bash 127.0.0.1 "$listen_port"; then
    cat "$log_file"
    echo "TCP connection to 127.0.0.1:$listen_port failed" >&2
    exit 1
fi
if ! kill -0 "$server_pid" 2>/dev/null; then
    cat "$log_file"
    echo 'Server exited after the TCP client disconnected' >&2
    exit 1
fi

kill -TERM "$server_pid"
(
    sleep 10
    if kill -0 "$server_pid" 2>/dev/null; then
        kill -KILL "$server_pid" 2>/dev/null || true
    fi
) &
watchdog_pid=$!
set +e
wait "$server_pid"
status=$?
set -e
server_pid=
kill "$watchdog_pid" 2>/dev/null || true
wait "$watchdog_pid" 2>/dev/null || true

cat "$log_file"

if (( status != 0 )); then
    echo "Server did not shut down cleanly after SIGTERM (exit=$status)" >&2
    exit 1
fi
if ! grep -Fq 'Shutdown requested by signal 15' "$log_file"; then
    echo 'Server did not acknowledge SIGTERM' >&2
    exit 1
fi

echo "[server-shutdown-smoke] connected to 127.0.0.1:$listen_port and passed SIGTERM shutdown"
