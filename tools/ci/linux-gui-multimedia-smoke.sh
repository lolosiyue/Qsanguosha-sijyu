#!/usr/bin/env bash
#
# Linux GUI M2B-A multimedia smoke.
#
# Drives the real GUI path (QApplication -> engine -> MainWindow -> HomeScene/QML
# -> Qt event loop) and then the real Audio facade: backend selection, the short
# UI effect path, the voice player pool, BGM, missing-asset fallback, the QML
# media component, and a clean media shutdown.  Two independent timeouts guard
# the run:
#
#   * the app-level --multimedia-timeout-ms, which reports a failure marker; and
#   * the process-level `timeout` below, which kills a Qt event loop that hangs
#     hard enough to never reach its own timer (a stuck media decoder does that).
#
# The runner never requires audible output: CI has no audio device, so
# "no output device" is a recorded state, not a failure.
#
# Usage:
#   tools/ci/linux-gui-multimedia-smoke.sh <executable> <artifact-dir> [options]
#
# Options:
#   --platform <xcb|offscreen>   Qt platform plugin (default: xcb, under Xvfb)
#   --no-xvfb                    run against the current DISPLAY (WSLg, X11)
#   --timeout-ms <ms>            app-level timeout (default: 45000)
#   --process-timeout <seconds>  runner-level timeout (default: 120)
#   --expect <pass|fail>         expected outcome (default: pass)
#   --expect-stage <stage>       with --expect fail: the stage to blame
#   --expect-reason <reason>     with --expect fail: stage_failed | timeout
#   --expect-backend <name>      audio backend that must be selected (e.g. qt)
#   --video-source <path>        force the home backdrop to this file, so the
#                                video failure -> static background path runs
#   --expect-video-reason <r>    the classified video outcome that must be reported
#   --label <name>               artifact filename prefix (default: platform)

set -uo pipefail

EXECUTABLE=""
ARTIFACT_DIR=""
PLATFORM="xcb"
USE_XVFB=1
TIMEOUT_MS=45000
PROCESS_TIMEOUT=120
EXPECT="pass"
EXPECT_STAGE=""
EXPECT_REASON=""
EXPECT_BACKEND=""
VIDEO_SOURCE=""
EXPECT_VIDEO_REASON=""
LABEL=""

while [ $# -gt 0 ]; do
    case "$1" in
        --platform) PLATFORM="$2"; shift 2 ;;
        --no-xvfb) USE_XVFB=0; shift ;;
        --timeout-ms) TIMEOUT_MS="$2"; shift 2 ;;
        --process-timeout) PROCESS_TIMEOUT="$2"; shift 2 ;;
        --expect) EXPECT="$2"; shift 2 ;;
        --expect-stage) EXPECT_STAGE="$2"; shift 2 ;;
        --expect-reason) EXPECT_REASON="$2"; shift 2 ;;
        --expect-backend) EXPECT_BACKEND="$2"; shift 2 ;;
        --video-source) VIDEO_SOURCE="$2"; shift 2 ;;
        --expect-video-reason) EXPECT_VIDEO_REASON="$2"; shift 2 ;;
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

[ -n "$LABEL" ] || LABEL="$PLATFORM"
mkdir -p "$ARTIFACT_DIR"
# The game now resolves its data directory and chdir()s into it, so a
# relative report path would land inside the install tree instead of the
# artifact directory.  Absolutise before handing anything to the binary.
ARTIFACT_DIR="$(cd "$ARTIFACT_DIR" && pwd)"
EXECUTABLE="$(cd "$(dirname "$EXECUTABLE")" && pwd)/$(basename "$EXECUTABLE")"
LOG="$ARTIFACT_DIR/multimedia-smoke-$LABEL.log"
REPORT="$ARTIFACT_DIR/multimedia-smoke-$LABEL.json"
DIAG="$ARTIFACT_DIR/multimedia-plugins-$LABEL.txt"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# The audio stages are only meaningful with their fixtures present; regenerate
# them if this is a bundle-only checkout.
if [ ! -f "$REPO_ROOT/tests/fixtures/media/button-down.wav" ]; then
    python3 "$SCRIPT_DIR/make-media-fixtures.py" "$REPO_ROOT/tests/fixtures/media"
fi

# Same reason as the artifact directory: a relative --video-source would be
# resolved against the data directory the game chdir()s into, not the repository.
if [ -n "${VIDEO_SOURCE:-}" ]; then
    case "$VIDEO_SOURCE" in
        /*) ;;
        *) VIDEO_SOURCE="$REPO_ROOT/$VIDEO_SOURCE" ;;
    esac
fi

# Qt/Mesa software rendering: the CI runner has no GPU, and pixel output is not
# what this smoke asserts.
export QT_QPA_PLATFORM="$PLATFORM"
export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$ARTIFACT_DIR/xdg-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Qt multimedia plugin diagnostics: which media backend plugins the binary can
# actually see.  Captured up front so a "backend_unavailable" verdict later is
# explainable rather than mysterious.
{
    echo "== QT_MEDIA_BACKEND =="
    echo "${QT_MEDIA_BACKEND:-<unset>}"
    echo
    echo "== Qt plugin directories =="
    for root in "${QT_ROOT_DIR:-}" "$(dirname "$EXECUTABLE")"; do
        [ -n "$root" ] || continue
        find "$root" -maxdepth 4 -type d -name multimedia -print 2>/dev/null
    done
    echo
    echo "== multimedia plugins found =="
    for root in "${QT_ROOT_DIR:-}" "$(dirname "$EXECUTABLE")"; do
        [ -n "$root" ] || continue
        find "$root" -maxdepth 5 -name '*mediaplugin*' -print 2>/dev/null
    done
    echo
    echo "== audio devices (informational; CI usually has none) =="
    ls -l /dev/snd 2>/dev/null || echo "no /dev/snd"
    aplay -l 2>/dev/null || echo "no aplay"
} | tee "$DIAG"

APP_ARGS=(
    --multimedia-smoke
    --multimedia-timeout-ms "$TIMEOUT_MS"
    --multimedia-report "$REPORT"
)
APP_ARGS+=(--multimedia-fixtures "$REPO_ROOT/tests/fixtures/media")
[ -n "$VIDEO_SOURCE" ] && APP_ARGS+=(--multimedia-video-source "$VIDEO_SOURCE")

echo "== Linux GUI multimedia smoke ($LABEL) =="
echo "executable       : $EXECUTABLE"
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
    echo "The multimedia smoke was killed by the runner-level timeout." >&2
fi

echo "---- last 60 log lines ----"
tail -n 60 "$LOG"
echo "---------------------------"

VALIDATE_ARGS=("$LOG" --exit-code "$STATUS" --expect "$EXPECT")
[ -n "$EXPECT_STAGE" ] && VALIDATE_ARGS+=(--expect-stage "$EXPECT_STAGE")
[ -n "$EXPECT_REASON" ] && VALIDATE_ARGS+=(--expect-reason "$EXPECT_REASON")
[ -n "$EXPECT_BACKEND" ] && VALIDATE_ARGS+=(--expect-backend "$EXPECT_BACKEND")
[ -n "$EXPECT_VIDEO_REASON" ] && VALIDATE_ARGS+=(--expect-video-reason "$EXPECT_VIDEO_REASON")

python3 "$SCRIPT_DIR/validate-multimedia-smoke.py" "${VALIDATE_ARGS[@]}"
VALIDATION=$?

# No orphan may outlive the smoke: a leaked QSanguosha, Xvfb or media decoder
# would poison the next CI step.  Scoped to this run's own process group.
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
    echo "Processes survived the multimedia smoke:" >&2
    ps -o pid,pgid,comm -g "$CHILD" >&2 2>/dev/null || pgrep -ag "$CHILD" >&2
    kill -TERM -- "-$CHILD" 2>/dev/null
    sleep 2
    if pgrep -g "$CHILD" >/dev/null 2>&1; then
        kill -KILL -- "-$CHILD" 2>/dev/null
    fi
    LEAKED=1
fi
if [ "$LEAKED" -ne 0 ]; then
    echo "Orphan processes survived the multimedia smoke." >&2
    exit 1
fi

exit "$VALIDATION"
