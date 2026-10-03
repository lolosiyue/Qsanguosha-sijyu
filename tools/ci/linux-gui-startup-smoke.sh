#!/usr/bin/env bash
# Run the GUI startup contract with bounded app and process timeouts.
#

set -uo pipefail

EXECUTABLE=""
ARTIFACT_DIR=""
PLATFORM="xcb"
USE_XVFB=1
TIMEOUT_MS=30000
PROCESS_TIMEOUT=90
EXPECT="pass"
EXPECT_STAGE=""
EXPECT_REASON=""
LABEL=""
PAGE="home"

while [ $# -gt 0 ]; do
    case "$1" in
        --platform) PLATFORM="$2"; shift 2 ;;
        --no-xvfb) USE_XVFB=0; shift ;;
        --timeout-ms) TIMEOUT_MS="$2"; shift 2 ;;
        --process-timeout) PROCESS_TIMEOUT="$2"; shift 2 ;;
        --expect) EXPECT="$2"; shift 2 ;;
        --expect-stage) EXPECT_STAGE="$2"; shift 2 ;;
        --expect-reason) EXPECT_REASON="$2"; shift 2 ;;
        --label) LABEL="$2"; shift 2 ;;
        --page) PAGE="$2"; shift 2 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            if [ -z "$EXECUTABLE" ]; then EXECUTABLE="$1"
            elif [ -z "$ARTIFACT_DIR" ]; then ARTIFACT_DIR="$1"
            else echo "Unexpected argument: $1" >&2; exit 2
            fi
            shift ;;
    esac
done

if [ -z "$EXECUTABLE" ] || [ -z "$ARTIFACT_DIR" ]; then
    echo "usage: $0 <executable> <artifact-dir> [options]" >&2
    exit 2
fi
if [ ! -x "$EXECUTABLE" ]; then
    echo "Not an executable: $EXECUTABLE" >&2
    exit 2
fi
if [ "$PAGE" != "home" ] && [ "$PAGE" != "cards" ]; then
    echo "Unsupported startup page: $PAGE" >&2
    exit 2
fi

[ -n "$LABEL" ] || LABEL="$PLATFORM"
mkdir -p "$ARTIFACT_DIR"
# The game changes to its data directory; make report paths absolute before launching it.
ARTIFACT_DIR="$(cd "$ARTIFACT_DIR" && pwd)"
EXECUTABLE="$(cd "$(dirname "$EXECUTABLE")" && pwd)/$(basename "$EXECUTABLE")"
LOG="$ARTIFACT_DIR/ui-startup-smoke-$LABEL.log"
REPORT="$ARTIFACT_DIR/ui-startup-smoke-$LABEL.json"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# CI uses software rendering; this smoke checks startup behavior, not pixels.
export QT_QPA_PLATFORM="$PLATFORM"
export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-}"
# Provide a private runtime directory to avoid Qt warnings when XDG_RUNTIME_DIR is unset.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$ARTIFACT_DIR/xdg-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

APP_ARGS=(
    --ui-startup-smoke
    --ui-startup-timeout-ms "$TIMEOUT_MS"
    --ui-startup-report "$REPORT"
)
if [ "$PAGE" != "home" ]; then
    APP_ARGS+=(--ui-startup-page "$PAGE")
fi

echo "== Linux GUI startup smoke ($LABEL) =="
echo "executable       : $EXECUTABLE"
echo "QT_QPA_PLATFORM  : $QT_QPA_PLATFORM"
echo "QT_QUICK_BACKEND : $QT_QUICK_BACKEND"
echo "app timeout      : ${TIMEOUT_MS}ms"
echo "process timeout  : ${PROCESS_TIMEOUT}s"
echo "xvfb             : $([ "$USE_XVFB" -eq 1 ] && echo yes || echo no)"
echo "startup page     : $PAGE"

# Use a private session so cleanup targets only processes started by this script.
SETSID=""
command -v setsid >/dev/null 2>&1 && SETSID="setsid"

if [ "$USE_XVFB" -eq 1 ]; then
    # Choose a free display and force child cleanup if SIGTERM is ignored.
    $SETSID timeout --kill-after=10s "${PROCESS_TIMEOUT}s" \
        xvfb-run -a -s "-screen 0 1280x720x24" \
        "$EXECUTABLE" "${APP_ARGS[@]}" >"$LOG" 2>&1 &
else
    $SETSID timeout --kill-after=10s "${PROCESS_TIMEOUT}s" \
        "$EXECUTABLE" "${APP_ARGS[@]}" >"$LOG" 2>&1 &
fi
CHILD=$!
wait "$CHILD"
STATUS=$?

echo "exit code        : $STATUS"
if [ "$STATUS" -eq 124 ] || [ "$STATUS" -eq 137 ]; then
    echo "The startup smoke was killed by the runner-level timeout." >&2
fi

echo "---- last 40 log lines ----"
tail -n 40 "$LOG"
echo "---------------------------"

VALIDATE_ARGS=("$LOG" --exit-code "$STATUS" --expect "$EXPECT")
[ -n "$EXPECT_STAGE" ] && VALIDATE_ARGS+=(--expect-stage "$EXPECT_STAGE")
[ -n "$EXPECT_REASON" ] && VALIDATE_ARGS+=(--expect-reason "$EXPECT_REASON")

python3 "$SCRIPT_DIR/validate-ui-startup-smoke.py" "${VALIDATE_ARGS[@]}"
VALIDATION=$?

# Clean up only this run's process group; leave any developer GUI untouched.
LEAKED=0
# Allow a bounded grace period for xvfb-run to reap Xvfb after the app exits.
if [ -n "$SETSID" ]; then
    for _ in $(seq 1 20); do
        pgrep -g "$CHILD" >/dev/null 2>&1 || break
        sleep 0.5
    done
fi
if [ -n "$SETSID" ] && pgrep -g "$CHILD" >/dev/null 2>&1; then
    echo "Processes survived the startup smoke:" >&2
    ps -o pid,pgid,comm -g "$CHILD" >&2 2>/dev/null || pgrep -ag "$CHILD" >&2
    kill -TERM -- "-$CHILD" 2>/dev/null
    sleep 2
    if pgrep -g "$CHILD" >/dev/null 2>&1; then
        kill -KILL -- "-$CHILD" 2>/dev/null
    fi
    LEAKED=1
fi
if [ "$LEAKED" -ne 0 ]; then
    echo "Orphan processes survived the startup smoke." >&2
    exit 1
fi

exit "$VALIDATION"
