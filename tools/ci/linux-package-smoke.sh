#!/usr/bin/env bash
#
# Linux package smoke (M3).
#
# Runs the M1 startup, M2B-A multimedia and M2B-B effects contracts against a
# *package artifact* rather than against the build tree.  That distinction is
# the whole point: a build-tree binary finds its Qt through an absolute RPATH
# and its data through the repository it was built in, so it can pass every
# earlier milestone while the shipped package fails to start on a player's
# machine.
#
# Two package shapes are supported and both are exercised the same way:
#
#   portable  a directory extracted from QSanguosha-*.tar.zst
#   appimage  the squashfs-root/ produced by --appimage-extract
#
# Usage:
#   tools/ci/linux-package-smoke.sh <bundle-root> <artifact-dir> [options]
#
# Options:
#   --kind <portable|appimage>   layout of <bundle-root> (default: autodetect)
#   --label <name>               artifact filename prefix
#   --platform <xcb|offscreen>   Qt platform plugin (default: offscreen)
#   --no-xvfb                    run against the current DISPLAY
#   --profiles "a b c"           effects profiles to run (default: "none reduced full")
#   --skip-multimedia            skip the multimedia contract
#   --process-timeout <seconds>  per-run runner timeout (default: 200)

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

# ---------------------------------------------------------------------------
# Layout: the package has to describe itself correctly before anything else is
# worth running.  --asset-report is also the first thing a player is asked for
# when they report "it does not start".
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

# ---------------------------------------------------------------------------
# M1 startup, from the package.
# ---------------------------------------------------------------------------
echo
echo "-- M1 startup smoke --"
bash "$SCRIPT_DIR/linux-gui-startup-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
    --platform "$PLATFORM" --label "pkg-$LABEL-startup" $XVFB_ARG \
    --timeout-ms 60000 --process-timeout "$PROCESS_TIMEOUT" \
    || note_failure "startup smoke from the package"

# ---------------------------------------------------------------------------
# M2B-A multimedia, from the package.  This is the one that proves the Qt
# multimedia plugin and its FFmpeg libraries were actually deployed: a bundle
# missing them still starts, and only fails when something asks for audio.
# ---------------------------------------------------------------------------
if [ "$SKIP_MULTIMEDIA" -eq 0 ]; then
    echo
    echo "-- M2B-A multimedia smoke --"
    bash "$SCRIPT_DIR/linux-gui-multimedia-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --platform "$PLATFORM" --label "pkg-$LABEL-multimedia" $XVFB_ARG \
        --expect-backend qt --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT" \
        || note_failure "multimedia smoke from the package"

    echo
    echo "-- M2B-A video fallback --"
    bash "$SCRIPT_DIR/linux-gui-multimedia-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --platform "$PLATFORM" --label "pkg-$LABEL-video-missing" $XVFB_ARG \
        --expect-backend qt --video-source tests/fixtures/media/no-such-clip.mp4 \
        --expect-video-reason asset_missing \
        --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT" \
        || note_failure "video fallback from the package"
fi

# ---------------------------------------------------------------------------
# M2B-B effects profiles, from the package.
# ---------------------------------------------------------------------------
for profile in $PROFILES; do
    echo
    echo "-- M2B-B effects smoke ($profile) --"
    bash "$SCRIPT_DIR/linux-gui-effects-smoke.sh" "$CLIENT" "$ARTIFACT_DIR" \
        --profile "$profile" --platform "$PLATFORM" \
        --label "pkg-$LABEL-effects-$profile" $XVFB_ARG \
        --timeout-ms 90000 --process-timeout "$PROCESS_TIMEOUT" \
        || note_failure "effects smoke ($profile) from the package"
done

# ---------------------------------------------------------------------------
# The dedicated server has to work out of the same package.
# ---------------------------------------------------------------------------
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
