#!/usr/bin/env bash
# Run the GUI multimedia contract with bounded app and process timeouts.
# CI may have no audio device; report that state without failing.
#

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
# The game changes to its data directory; make report paths absolute before launching it.
ARTIFACT_DIR="$(cd "$ARTIFACT_DIR" && pwd)"
EXECUTABLE="$(cd "$(dirname "$EXECUTABLE")" && pwd)/$(basename "$EXECUTABLE")"
LOG="$ARTIFACT_DIR/multimedia-smoke-$LABEL.log"
REPORT="$ARTIFACT_DIR/multimedia-smoke-$LABEL.json"
DIAG="$ARTIFACT_DIR/multimedia-plugins-$LABEL.txt"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Regenerate media fixtures when they are absent from a bundle-only checkout.
if [ ! -f "$REPO_ROOT/tools/ci/fixtures/media/button-down.wav" ]; then
    python3 "$SCRIPT_DIR/make-media-fixtures.py" "$REPO_ROOT/tools/ci/fixtures/media"
fi

# Resolve video source paths from the repository because the game changes its working directory.
if [ -n "${VIDEO_SOURCE:-}" ]; then
    case "$VIDEO_SOURCE" in
        /*) ;;
        *) VIDEO_SOURCE="$REPO_ROOT/$VIDEO_SOURCE" ;;
    esac
fi

# CI uses software rendering; this smoke checks behavior, not pixels.
export QT_QPA_PLATFORM="$PLATFORM"
export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$ARTIFACT_DIR/xdg-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Capture visible multimedia plugins before shutdown so backend failures are diagnosable.
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
APP_ARGS+=(--multimedia-fixtures "$REPO_ROOT/tools/ci/fixtures/media")
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

# A passing result marker cannot override a crash during process teardown.
# Keep the original log/report/validation and only reproduce signal exits under
# GDB when package CI explicitly opts in. This diagnostic cannot make CI pass.
if [ "${QSAN_MULTIMEDIA_CRASH_DIAGNOSTICS:-0}" = 1 ] \
    && { [ "$STATUS" -eq 134 ] || [ "$STATUS" -eq 139 ]; }; then
    DEBUG_LOG="$ARTIFACT_DIR/multimedia-gdb-$LABEL.log"
    DEBUG_META="$ARTIFACT_DIR/multimedia-gdb-$LABEL-metadata.txt"
    DEBUG_REPORT="$ARTIFACT_DIR/multimedia-gdb-$LABEL.json"
    DEBUG_ARGS=("${APP_ARGS[@]}")
    for ((i = 0; i < ${#DEBUG_ARGS[@]} - 1; ++i)); do
        if [ "${DEBUG_ARGS[$i]}" = --multimedia-report ]; then
            DEBUG_ARGS[$((i + 1))]="$DEBUG_REPORT"
            break
        fi
    done
    {
        echo "original_exit_code=$STATUS"
        echo "original_validation_exit_code=$VALIDATION"
        echo "executable=$EXECUTABLE"
        printf 'diagnostic_arguments:'
        printf ' %q' "${DEBUG_ARGS[@]}"
        printf '\n'
        uname -a
        sha256sum "$EXECUTABLE"
        # Whitelist runtime paths/backend/rendering settings; never dump env.
        for name in QT_QPA_PLATFORM QT_QUICK_BACKEND LIBGL_ALWAYS_SOFTWARE \
            QT_MEDIA_BACKEND QT_ROOT_DIR QT_PLUGIN_PATH QML2_IMPORT_PATH \
            LD_LIBRARY_PATH XDG_RUNTIME_DIR DISPLAY; do
            printf '%s=%s\n' "$name" "${!name-<unset>}"
        done
        ldd "$EXECUTABLE"
        readelf -n "$EXECUTABLE"
        readelf -d "$EXECUTABLE"
    } >"$DEBUG_META" 2>&1

    GDB_ARGS=(--nx --quiet --batch --return-child-result
        -ex 'set pagination off' -ex 'set confirm off'
        -ex 'set debuginfod enabled off' -ex 'set print elements 32'
        -ex 'set print max-depth 3')
    # Shipping binaries are stripped. Use the build's matching symbols without
    # executing the build-tree binary or changing the package's Qt/plugin paths.
    SYMBOL_FILE="${QSAN_MULTIMEDIA_DEBUG_SYMBOLS:-}"
    if [ -n "$SYMBOL_FILE" ] && [ -f "$SYMBOL_FILE" ]; then
        PACKAGE_BUILD_ID=$(readelf -n "$EXECUTABLE" 2>/dev/null | awk '/Build ID:/ { print $3; exit }')
        SYMBOL_BUILD_ID=$(readelf -n "$SYMBOL_FILE" 2>/dev/null | awk '/Build ID:/ { print $3; exit }')
        if [ -n "$PACKAGE_BUILD_ID" ] && [ "$PACKAGE_BUILD_ID" = "$SYMBOL_BUILD_ID" ]; then
            # --args resets GDB's symbol argument. Load symbols as a command
            # after option parsing, while retaining the package as exec-file.
            GDB_SYMBOL_PATH="${SYMBOL_FILE//\\/\\\\}"
            GDB_SYMBOL_PATH="${GDB_SYMBOL_PATH//\"/\\\"}"
            GDB_ARGS+=(-ex "symbol-file \"$GDB_SYMBOL_PATH\"")
            printf 'symbol_file=%s\nbuild_id=%s\n' "$SYMBOL_FILE" "$PACKAGE_BUILD_ID" >>"$DEBUG_META"
        else
            echo 'symbol_file_skipped=build_id_mismatch' >>"$DEBUG_META"
        fi
    fi
    GDB_ARGS+=(-ex run -ex 'info files' -ex 'info sharedlibrary'
        -ex 'info proc mappings' -ex 'thread apply all bt full')

    if command -v gdb >/dev/null 2>&1 && [ -n "$SETSID" ]; then
        echo "Capturing failure-only multimedia GDB diagnostics: $DEBUG_LOG"
        if [ "$USE_XVFB" -eq 1 ]; then
            $SETSID timeout --kill-after=10s "${PROCESS_TIMEOUT}s" \
                xvfb-run -a -s '-screen 0 1280x720x24' \
                gdb "${GDB_ARGS[@]}" --args "$EXECUTABLE" "${DEBUG_ARGS[@]}" \
                >"$DEBUG_LOG" 2>&1 &
        else
            $SETSID timeout --kill-after=10s "${PROCESS_TIMEOUT}s" \
                gdb "${GDB_ARGS[@]}" --args "$EXECUTABLE" "${DEBUG_ARGS[@]}" \
                >"$DEBUG_LOG" 2>&1 &
        fi
        DEBUG_CHILD=$!
        wait "$DEBUG_CHILD"
        DEBUG_STATUS=$?
        echo "debugger_exit_code=$DEBUG_STATUS" >>"$DEBUG_META"
        # GDB gives its inferior a separate process group. Both remain in the
        # isolated setsid session, so clean that session, not just GDB's group.
        live_debug_session_pids() {
            ps -eo pid=,sid=,stat= | awk -v session="$DEBUG_CHILD" \
                '$2 == session && $3 !~ /^[ZX]/ { print $1 }'
        }
        if [ -n "$(live_debug_session_pids)" ]; then
            ps -o pid,ppid,pgid,sid,comm -s "$DEBUG_CHILD" >>"$DEBUG_META" 2>&1
            mapfile -t DEBUG_PIDS < <(live_debug_session_pids)
            if [ "${#DEBUG_PIDS[@]}" -gt 0 ]; then
                kill -TERM "${DEBUG_PIDS[@]}" 2>/dev/null
            fi
            sleep 2
            mapfile -t DEBUG_PIDS < <(live_debug_session_pids)
            if [ "${#DEBUG_PIDS[@]}" -gt 0 ]; then
                kill -KILL "${DEBUG_PIDS[@]}" 2>/dev/null
            fi
            for _ in $(seq 1 20); do
                [ -n "$(live_debug_session_pids)" ] || break
                sleep 0.1
            done
        fi
        # Unreaped zombies are already dead and cannot be killed again. Report
        # live survivors separately rather than misclassifying a dead inferior.
        if [ -n "$(live_debug_session_pids)" ]; then
            echo 'diagnostic_session_cleanup_failed=1' >>"$DEBUG_META"
        else
            echo 'diagnostic_session_live_remaining=0' >>"$DEBUG_META"
        fi
        tail -n 80 "$DEBUG_LOG"
    else
        echo 'gdb or setsid is unavailable; original smoke failure remains authoritative.' >"$DEBUG_LOG"
    fi
fi

exit "$VALIDATION"
