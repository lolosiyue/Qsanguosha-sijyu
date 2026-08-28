#!/usr/bin/env bash
#
# Linux GUI M1 runtime smoke.
#
# Drives the real GUI startup path (QApplication -> engine -> MainWindow ->
# HomeScene/QML -> Qt event loop) and validates the structured markers the
# binary prints.  Two independent timeouts guard the run:
#
#   * the app-level --ui-startup-timeout-ms, which reports a failure marker; and
#   * the process-level `timeout` below, which kills a Qt event loop that hangs
#     hard enough to never reach its own timer.
#
# Usage:
#   tools/ci/linux-gui-startup-smoke.sh <executable> <artifact-dir> [options]
#
# Options:
#   --platform <xcb|offscreen>   Qt platform plugin (default: xcb, under Xvfb)
#   --no-xvfb                    run against the current DISPLAY (WSLg, X11)
#   --timeout-ms <ms>            app-level timeout (default: 30000)
#   --process-timeout <seconds>  runner-level timeout (default: 90)
#   --expect <pass|fail>         expected outcome (default: pass)
#   --expect-stage <stage>       with --expect fail: the stage to blame
#   --expect-reason <reason>     with --expect fail: stage_failed | timeout
#   --label <name>               artifact filename prefix (default: platform)
#   --page <home|cards>          embedded page to verify (default: home)

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
# The game now resolves its data directory and chdir()s into it, so a
# relative report path would land inside the install tree instead of the
# artifact directory.  Absolutise before handing anything to the binary.
ARTIFACT_DIR="$(cd "$ARTIFACT_DIR" && pwd)"
EXECUTABLE="$(cd "$(dirname "$EXECUTABLE")" && pwd)/$(basename "$EXECUTABLE")"
LOG="$ARTIFACT_DIR/ui-startup-smoke-$LABEL.log"
REPORT="$ARTIFACT_DIR/ui-startup-smoke-$LABEL.json"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Qt/Mesa software rendering: the CI runner has no GPU, and pixel output is not
# what this smoke asserts.
export QT_QPA_PLATFORM="$PLATFORM"
export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-}"
# A missing XDG_RUNTIME_DIR makes Qt warn on every start; give it a private one.
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

# Run in a private session so cleanup can address exactly the processes this
# script started - never a GUI the developer happens to have open.
SETSID=""
command -v setsid >/dev/null 2>&1 && SETSID="setsid"

if [ "$USE_XVFB" -eq 1 ]; then
    # -a picks a free display number; --kill-after guarantees the child dies even
    # if SIGTERM is swallowed.
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

# No orphan may outlive the smoke: a leaked QSanguosha or Xvfb would poison the
# next CI step.  Scoped to this run's own process group, so a developer running
# the game in another window is never touched.
LEAKED=0
# `wait` returns as soon as the app exits, but xvfb-run still has to reap its
# Xvfb, and a process group does not empty instantaneously.  Checking at that
# exact moment turns a normal few-hundred-millisecond teardown into a failure,
# so give the group a bounded grace period first.  A process that is genuinely
# stuck is still caught - it simply never leaves.
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
