#!/usr/bin/env bash
# Run the GUI effects contract with bounded app and process timeouts.
#

set -uo pipefail

EXECUTABLE=""
ARTIFACT_DIR=""
PROFILE="full"
PLATFORM="xcb"
USE_XVFB=1
TIMEOUT_MS=45000
PROCESS_TIMEOUT=120
EXPECT="pass"
EXPECT_STAGE=""
EXPECT_REASON=""
FIXTURES=""
LABEL=""

while [ $# -gt 0 ]; do
    case "$1" in
        --profile) PROFILE="$2"; shift 2 ;;
        --platform) PLATFORM="$2"; shift 2 ;;
        --no-xvfb) USE_XVFB=0; shift ;;
        --timeout-ms) TIMEOUT_MS="$2"; shift 2 ;;
        --process-timeout) PROCESS_TIMEOUT="$2"; shift 2 ;;
        --expect) EXPECT="$2"; shift 2 ;;
        --expect-stage) EXPECT_STAGE="$2"; shift 2 ;;
        --expect-reason) EXPECT_REASON="$2"; shift 2 ;;
        --fixtures) FIXTURES="$2"; shift 2 ;;
        --label) LABEL="$2"; shift 2 ;;
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

[ -n "$LABEL" ] || LABEL="$PROFILE"
mkdir -p "$ARTIFACT_DIR"
# The game changes to its data directory; make report paths absolute before launching it.
ARTIFACT_DIR="$(cd "$ARTIFACT_DIR" && pwd)"
EXECUTABLE="$(cd "$(dirname "$EXECUTABLE")" && pwd)/$(basename "$EXECUTABLE")"
LOG="$ARTIFACT_DIR/effects-smoke-$LABEL.log"
REPORT="$ARTIFACT_DIR/effects-smoke-$LABEL.json"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

[ -n "$FIXTURES" ] || FIXTURES="tools/ci/fixtures/effects"
# Resolve fixture paths from the repository because the game changes its working directory.
case "$FIXTURES" in
    /*) ;;
    *) FIXTURES="$REPO_ROOT/$FIXTURES" ;;
esac

# Regenerate effects fixtures when they are absent from a bundle-only checkout.
# them if this is a bundle-only checkout.
if [ ! -f "$FIXTURES/animated.gif" ]; then
    python3 "$SCRIPT_DIR/make-effects-fixtures.py" "$FIXTURES"
fi

# CI uses software rendering; this smoke checks behavior, not pixels.
export QT_QPA_PLATFORM="$PLATFORM"
export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$ARTIFACT_DIR/xdg-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

APP_ARGS=(
    --effects-smoke
    --effects-profile "$PROFILE"
    --effects-timeout-ms "$TIMEOUT_MS"
    --effects-report "$REPORT"
    --effects-fixtures "$FIXTURES"
)

echo "== Linux GUI effects smoke ($LABEL) =="
echo "executable       : $EXECUTABLE"
echo "profile          : $PROFILE"
echo "fixtures         : $FIXTURES"
echo "QT_QPA_PLATFORM  : $QT_QPA_PLATFORM"
echo "QT_QUICK_BACKEND : $QT_QUICK_BACKEND"
echo "app timeout      : ${TIMEOUT_MS}ms"
echo "process timeout  : ${PROCESS_TIMEOUT}s"
echo "xvfb             : $([ "$USE_XVFB" -eq 1 ] && echo yes || echo no)"

SETSID=""
command -v setsid >/dev/null 2>&1 && SETSID="setsid"

if [ "$USE_XVFB" -eq 1 ]; then
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
    echo "The effects smoke was killed by the runner-level timeout." >&2
fi

echo "---- last 60 log lines ----"
tail -n 60 "$LOG"
echo "---------------------------"

VALIDATE_ARGS=("$LOG" --exit-code "$STATUS" --expect "$EXPECT" --expect-profile "$PROFILE")
[ -n "$EXPECT_STAGE" ] && VALIDATE_ARGS+=(--expect-stage "$EXPECT_STAGE")
[ -n "$EXPECT_REASON" ] && VALIDATE_ARGS+=(--expect-reason "$EXPECT_REASON")

python3 "$SCRIPT_DIR/validate-effects-smoke.py" "${VALIDATE_ARGS[@]}"
VALIDATION=$?

# Clean up only this run's process group so no smoke processes survive into the next CI step.
LEAKED=0
# Allow a bounded grace period for xvfb-run to reap Xvfb after the app exits.
if [ -n "$SETSID" ]; then
    for _ in $(seq 1 20); do
        pgrep -g "$CHILD" >/dev/null 2>&1 || break
        sleep 0.5
    done
fi
if [ -n "$SETSID" ] && pgrep -g "$CHILD" >/dev/null 2>&1; then
    echo "Processes survived the effects smoke:" >&2
    ps -o pid,pgid,comm -g "$CHILD" >&2 2>/dev/null || pgrep -ag "$CHILD" >&2
    kill -TERM -- "-$CHILD" 2>/dev/null
    sleep 2
    if pgrep -g "$CHILD" >/dev/null 2>&1; then
        kill -KILL -- "-$CHILD" 2>/dev/null
    fi
    LEAKED=1
fi
if [ "$LEAKED" -ne 0 ]; then
    echo "Orphan processes survived the effects smoke." >&2
    exit 1
fi

exit "$VALIDATION"
