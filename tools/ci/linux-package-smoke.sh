#!/usr/bin/env bash
# Run startup, multimedia, effects, and server contracts against a packaged artifact.
#

set -uo pipefail

BUNDLE=""
ARTIFACT_DIR=""
KIND=""
LABEL=""
PLATFORM="offscreen"
XVFB_ARG="--no-xvfb"
PROFILES="none reduced full"
SKIP_MULTIMEDIA=0
PROCESS_TIMEOUT=200

while [ $# -gt 0 ]; do
    case "$1" in
        --kind) KIND="$2"; shift 2 ;;
        --label) LABEL="$2"; shift 2 ;;
        --platform) PLATFORM="$2"; shift 2 ;;
        --no-xvfb) XVFB_ARG="--no-xvfb"; shift ;;
        --xvfb) XVFB_ARG=""; shift ;;
        --profiles) PROFILES="$2"; shift 2 ;;
        --skip-multimedia) SKIP_MULTIMEDIA=1; shift ;;
        --process-timeout) PROCESS_TIMEOUT="$2"; shift 2 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            if [ -z "$BUNDLE" ]; then BUNDLE="$1"
            elif [ -z "$ARTIFACT_DIR" ]; then ARTIFACT_DIR="$1"
            else echo "Unexpected argument: $1" >&2; exit 2
            fi
            shift ;;
    esac
done

if [ -z "$BUNDLE" ] || [ -z "$ARTIFACT_DIR" ]; then
    echo "usage: $0 <bundle-root> <artifact-dir> [options]" >&2
    exit 2
fi
[ -d "$BUNDLE" ] || { echo "Not a directory: $BUNDLE" >&2; exit 2; }

BUNDLE="$(cd "$BUNDLE" && pwd)"
mkdir -p "$ARTIFACT_DIR"
ARTIFACT_DIR="$(cd "$ARTIFACT_DIR" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -z "$KIND" ]; then
    if [ -x "$BUNDLE/AppRun" ]; then KIND="appimage"; else KIND="portable"; fi
fi
case "$KIND" in
    portable) BIN_DIR="$BUNDLE/bin" ;;
    appimage) BIN_DIR="$BUNDLE/usr/bin" ;;
    *) echo "Unknown --kind: $KIND" >&2; exit 2 ;;
esac
[ -n "$LABEL" ] || LABEL="$KIND"

CLIENT="$BIN_DIR/QSanguosha"
SERVER="$BIN_DIR/qsanguosha_server"
for exe in "$CLIENT" "$SERVER"; do
    [ -x "$exe" ] || { echo "Missing executable in the package: $exe" >&2; exit 2; }
done

FAILURES=0
note_failure() {
    echo "FAIL: $1" >&2
    FAILURES=$((FAILURES + 1))
}

echo "== Linux package smoke ($LABEL) =="
echo "bundle           : $BUNDLE"
echo "kind             : $KIND"
echo "platform         : $PLATFORM"
echo "effects profiles : $PROFILES"

# Compare the inherited CI runtime with the package on the same executable.
# Baseline results are diagnostic; the isolated package checks below remain
# authoritative, including their exit-status and process-cleanup requirements.
RUNTIME_COMPARISON=""
if [ "${QSAN_MULTIMEDIA_CRASH_DIAGNOSTICS:-0}" = 1 ] \
    && [ "$KIND" = portable ] && [ "$SKIP_MULTIMEDIA" -eq 0 ]; then
    RUNTIME_COMPARISON="$ARTIFACT_DIR/package-runtime-comparison-$LABEL.txt"
    {
        echo "executable=$CLIENT"
        sha256sum "$CLIENT"
        echo "sdk_prefix=${QT_ROOT_DIR:-<unset>}"
    } >"$RUNTIME_COMPARISON"
    bash "$SCRIPT_DIR/linux-gui-multimedia-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --platform "$PLATFORM" --label "pkg-$LABEL-sdk-baseline" $XVFB_ARG \
        --expect-backend qt --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT"
    echo "inherited_multimedia_validation=$?" >>"$RUNTIME_COMPARISON"
    bash "$SCRIPT_DIR/linux-gui-multimedia-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --platform "$PLATFORM" --label "pkg-$LABEL-sdk-video-baseline" $XVFB_ARG \
        --expect-backend qt --video-source tools/ci/fixtures/media/no-such-clip.mp4 \
        --expect-video-reason asset_missing \
        --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT"
    echo "inherited_video_validation=$?" >>"$RUNTIME_COMPARISON"
fi

# setup-qt exports SDK search paths that take precedence over the package's
# RUNPATH and qt.conf. Remove only entries inside that SDK, retaining explicit
# bundle paths and other caller-provided entries.
remove_sdk_search_paths() {
    local sdk="${QT_ROOT_DIR:-}" name entry remaining kept removed has_kept
    [ -n "$sdk" ] || return 0
    sdk="${sdk%/}"
    [ -n "$sdk" ] || return 0
    for name in LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH; do
        [ -v "$name" ] || continue
        remaining="${!name}"
        kept="" removed=0 has_kept=0
        while :; do
            entry="${remaining%%:*}"
            case "$entry" in
                "$sdk"|"$sdk"/*) removed=1 ;;
                *)
                    if [ "$has_kept" -eq 0 ]; then kept="$entry"; else kept+=":$entry"; fi
                    has_kept=1 ;;
            esac
            [[ "$remaining" = *:* ]] || break
            remaining="${remaining#*:}"
        done
        if [ "$removed" -eq 1 ]; then
            if [ -n "$kept" ]; then export "$name=$kept"; else unset "$name"; fi
        fi
    done
}
remove_sdk_search_paths
{
    for name in LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH; do
        printf '%s=%s\n' "$name" "${!name-<unset>}"
    done
    ldd "$CLIENT"
} >"$ARTIFACT_DIR/package-runtime-$LABEL.txt" 2>&1

# Verify package metadata before running binaries.
# ---------------------------------------------------------------------------
echo
echo "-- asset report (from a directory outside the package) --"
REPORT_JSON="$ARTIFACT_DIR/package-asset-report-$LABEL.json"
( cd / && "$SERVER" --asset-report ) >"$REPORT_JSON" 2>"$ARTIFACT_DIR/package-asset-report-$LABEL.log"
ASSET_STATUS=$?
cat "$ARTIFACT_DIR/package-asset-report-$LABEL.log"
if [ "$ASSET_STATUS" -ne 0 ]; then
    note_failure "--asset-report reported an incomplete package (exit $ASSET_STATUS)"
fi
python3 - "$REPORT_JSON" "$BUNDLE" <<'PY'
import json, os, sys
report = json.load(open(sys.argv[1], encoding="utf-8"))
bundle = os.path.realpath(sys.argv[2])
paths = report["runtime_paths"]
assets = report["assets"]
problems = []
# The package must resolve its own data, not something it happened to find in
# the working directory - that is exactly the bug M3 exists to remove.
if paths["asset_root_source"] not in ("installed-prefix", "portable-bundle"):
    problems.append("asset root came from %r, not from the package layout"
                    % paths["asset_root_source"])
if os.path.realpath(paths["asset_root"]).startswith(bundle) is False:
    problems.append("asset root %r is outside the package" % paths["asset_root"])
if not paths["packaged"]:
    problems.append("the package did not classify itself as packaged")
# User data must never be written back into the package.
if os.path.realpath(paths["user_data_root"]).startswith(bundle):
    problems.append("user data root %r is inside the package"
                    % paths["user_data_root"])
if not assets["manifest_present"]:
    problems.append("the package ships no asset manifest")
if assets["missing_required"]:
    problems.append("missing required assets: %s" % assets["missing_required"])
for problem in problems:
    print("  - " + problem)
print("asset root      : %s (%s)" % (paths["asset_root"], paths["asset_root_source"]))
print("user data root  : %s" % paths["user_data_root"])
print("missing optional: %s" % (assets["missing_optional"] or "none"))
raise SystemExit(1 if problems else 0)
PY
[ $? -eq 0 ] || note_failure "the package does not resolve its own layout"

echo
echo "-- M1 startup smoke --"
bash "$SCRIPT_DIR/linux-gui-startup-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
    --platform "$PLATFORM" --label "pkg-$LABEL-startup" $XVFB_ARG \
    --timeout-ms 60000 --process-timeout "$PROCESS_TIMEOUT" \
    || note_failure "startup smoke from the package"

# Verify deployed multimedia plugins and FFmpeg libraries, which are used only at runtime.
# ---------------------------------------------------------------------------
if [ "$SKIP_MULTIMEDIA" -eq 0 ]; then
    echo
    echo "-- M2B-A multimedia smoke --"
    bash "$SCRIPT_DIR/linux-gui-multimedia-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --platform "$PLATFORM" --label "pkg-$LABEL-multimedia" $XVFB_ARG \
        --expect-backend qt --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT"
    MULTIMEDIA_STATUS=$?
    [ -z "$RUNTIME_COMPARISON" ] || echo "isolated_multimedia_validation=$MULTIMEDIA_STATUS" >>"$RUNTIME_COMPARISON"
    [ "$MULTIMEDIA_STATUS" -eq 0 ] || note_failure "multimedia smoke from the package"

    echo
    echo "-- M2B-A video fallback --"
    bash "$SCRIPT_DIR/linux-gui-multimedia-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --platform "$PLATFORM" --label "pkg-$LABEL-video-missing" $XVFB_ARG \
        --expect-backend qt --video-source tools/ci/fixtures/media/no-such-clip.mp4 \
        --expect-video-reason asset_missing \
        --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT"
    VIDEO_STATUS=$?
    [ -z "$RUNTIME_COMPARISON" ] || echo "isolated_video_validation=$VIDEO_STATUS" >>"$RUNTIME_COMPARISON"
    [ "$VIDEO_STATUS" -eq 0 ] || note_failure "video fallback from the package"
fi

for profile in $PROFILES; do
    echo
    echo "-- M2B-B effects smoke ($profile) --"
    bash "$SCRIPT_DIR/linux-gui-effects-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --profile "$profile" --platform "$PLATFORM" \
        --label "pkg-$LABEL-effects-$profile" $XVFB_ARG \
        --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT" \
        || note_failure "effects smoke ($profile) from the package"
done

# Verify the dedicated server from the same package.
echo
echo "-- dedicated server from the package --"
( cd / && "$SERVER" --check-config --list-game-modes ) \
    >"$ARTIFACT_DIR/package-server-$LABEL.log" 2>&1 \
    || note_failure "dedicated server --check-config from the package"
grep -qE '^02p\b' "$ARTIFACT_DIR/package-server-$LABEL.log" \
    || note_failure "the packaged server does not report the 02p game mode"
tail -n 5 "$ARTIFACT_DIR/package-server-$LABEL.log"

echo
if [ "$FAILURES" -ne 0 ]; then
    echo "package smoke FAILED ($FAILURES check(s))" >&2
    exit 1
fi
echo "package smoke OK ($LABEL)"
