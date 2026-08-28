#!/usr/bin/env python3
"""Validate the Linux GUI M2B-A multimedia smoke output.

The GUI writes one ``MULTIMEDIA_STAGE`` line per stage, exactly one
``VIDEO_BACKEND_RESULT`` line, and exactly one ``MULTIMEDIA_RESULT`` line before
exiting.  This validator is the CI-side half of that contract.

Two things it deliberately does *not* do:

* it never treats "no console error" as success - every stage has to report
  itself, and the video path has to report a *classified* reason; and
* it never requires audible output.  GitHub runners have no audio device, so
  ``output_device: false`` is an expected, recorded state rather than a failure.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

STAGE_MARKER = "MULTIMEDIA_STAGE"
RESULT_MARKER = "MULTIMEDIA_RESULT"
VIDEO_MARKER = "VIDEO_BACKEND_RESULT"
SCHEMA_VERSION = 1

REQUIRED_STAGES = (
    "backend",
    "ui_effect",
    "voice",
    "bgm",
    "missing_asset",
    "video",
    "shutdown",
)

# Every outcome the home page may report for its background video.  An
# unclassified reason is a contract violation, not a pass.
KNOWN_VIDEO_REASONS = {
    "ok",
    "not_requested",
    "disabled",
    "asset_missing",
    "backend_unavailable",
    "codec_unsupported",
    "playback_error",
    "fallback_ok",
}


def parse_markers(text: str) -> tuple[list[dict], list[dict], list[dict]]:
    stages: list[dict] = []
    results: list[dict] = []
    videos: list[dict] = []
    for line in text.splitlines():
        line = line.strip()
        for marker, sink in ((STAGE_MARKER, stages), (RESULT_MARKER, results),
                             (VIDEO_MARKER, videos)):
            prefix = marker + " "
            if not line.startswith(prefix):
                continue
            payload = line[len(prefix):].strip()
            try:
                sink.append(json.loads(payload))
            except json.JSONDecodeError as error:
                raise SystemExit(f"{marker} payload is not valid JSON: {error}\n  {payload}")
            break
    return stages, results, videos


def stage_by_name_of(stages: list[dict], name: str) -> dict:
    for stage in stages:
        if stage.get("stage") == name:
            return stage
    return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="combined stdout+stderr of the smoke")
    parser.add_argument("--exit-code", type=int, required=True)
    parser.add_argument("--expect", choices=("pass", "fail"), default="pass")
    parser.add_argument("--expect-stage", default=None)
    parser.add_argument("--expect-reason", default=None,
                        help="with --expect fail: ok / stage_failed / timeout")
    parser.add_argument("--expect-backend", default=None,
                        help="audio backend the binary must have selected, e.g. qt")
    parser.add_argument("--expect-video-reason", default=None,
                        help="the classified video outcome the home page must report")
    arguments = parser.parse_args()

    text = arguments.log.read_text(encoding="utf-8", errors="replace")
    stages, results, videos = parse_markers(text)

    problems: list[str] = []
    if not results:
        problems.append(f"no {RESULT_MARKER} line - the GUI never reached a conclusion "
                        "(crash, killed by the runner timeout, or the marker contract broke)")
    elif len(results) > 1:
        problems.append(f"expected exactly one {RESULT_MARKER} line, got {len(results)}")

    result = results[-1] if results else {}
    if result:
        if result.get("schema_version") != SCHEMA_VERSION:
            problems.append(f"unexpected result schema_version {result.get('schema_version')!r} "
                            f"(this validator implements {SCHEMA_VERSION})")
        if result.get("exit_code") != arguments.exit_code:
            problems.append(f"process exit code {arguments.exit_code} disagrees with the "
                            f"marker's exit_code {result.get('exit_code')!r}")

    ok = bool(result.get("ok"))
    stage_status = {stage.get("stage"): bool(stage.get("ok")) for stage in stages}
    stage_by_name = {stage.get("stage"): stage for stage in stages}
    audio = result.get("audio", {}) or {}
    # The result marker is written after Audio::quit(), so its diagnostics have no
    # backend details left.  The last pre-shutdown snapshot is the useful one.
    shutdown_audio = (stage_by_name_of(stages, "shutdown").get("audio_before", {}) or {})
    audio_details = shutdown_audio.get("details", {}) or {}
    video = videos[-1] if videos else {}

    if arguments.expect == "pass":
        if arguments.exit_code != 0:
            problems.append(f"expected exit code 0, got {arguments.exit_code}")
        if not ok:
            problems.append(f"multimedia smoke reported failure at stage "
                            f"{result.get('stage')!r}: {result.get('error')!r}")
        for stage in REQUIRED_STAGES:
            if stage not in stage_status:
                problems.append(f"stage {stage!r} was never reported")
            elif not stage_status[stage]:
                problems.append(f"stage {stage!r} reported ok=false")

        if not videos:
            problems.append(f"no {VIDEO_MARKER} line - the home page never classified its "
                            "background video path")
        elif len(videos) > 1:
            problems.append(f"expected exactly one {VIDEO_MARKER} line, got {len(videos)}")
        reason = video.get("reason")
        if reason not in KNOWN_VIDEO_REASONS:
            problems.append(f"video background reported an unclassified reason {reason!r}; "
                            f"expected one of {sorted(KNOWN_VIDEO_REASONS)}")
        elif reason != "ok" and not video.get("fallback"):
            problems.append(f"video reason {reason!r} did not fall back to a static "
                            "background")
        # A real failure has to be *observed* falling back, not merely labelled.
        # "not_requested" means the backdrop is an image, so nothing fell back.
        if reason not in ("ok", "not_requested") and not video.get("fallback_confirmed"):
            problems.append(f"video reason {reason!r} never confirmed that the static "
                            "background took over")
        if arguments.expect_video_reason and reason != arguments.expect_video_reason:
            problems.append(f"expected video reason {arguments.expect_video_reason!r}, "
                            f"got {reason!r}")

        # The audio facade must be torn down; a surviving backend means a leaked
        # decoder thread or QObject.
        shutdown = stage_by_name.get("shutdown", {})
        if shutdown and shutdown.get("initialized_after_quit"):
            problems.append("Audio::quit() left the facade initialised")
        if shutdown and shutdown.get("backend_after_quit") not in (None, "none"):
            problems.append(f"audio backend {shutdown.get('backend_after_quit')!r} survived "
                            "shutdown")

        backend_stage = stage_by_name.get("backend", {})
        selected = backend_stage.get("backend")
        if arguments.expect_backend and selected != arguments.expect_backend:
            problems.append(f"expected the {arguments.expect_backend!r} audio backend, "
                            f"got {selected!r} (configured: "
                            f"{backend_stage.get('configured_backend')!r})")

        # Fixtures are what make the audio stages meaningful; a silently missing
        # fixture would turn them into no-ops that always pass.
        for stage in ("ui_effect", "voice", "bgm"):
            payload = stage_by_name.get(stage, {})
            if payload and not payload.get("fixture_available"):
                problems.append(f"stage {stage!r} ran without its media fixture "
                                f"({payload.get('fixture')!r}); the stage proves nothing")
        effect_stage = stage_by_name.get("ui_effect", {})
        if effect_stage and not effect_stage.get("classified_as_effect"):
            problems.append("the ui_effect fixture was not classified as a short UI effect, "
                            "so the low-latency path was never exercised")
        voice_stage = stage_by_name.get("voice", {})
        if voice_stage and not voice_stage.get("classified_as_voice"):
            problems.append("the voice fixture was not classified as voice, so the player "
                            "pool was never exercised")

        # The voice stage deliberately asks for more concurrent playbacks than the
        # pool holds.  A pool that "grew" instead of recycling would leak a
        # QMediaPlayer + QAudioOutput per request.
        pool_size = audio_details.get("voice_pool_size")
        if isinstance(pool_size, int) and pool_size > 0:
            requested = voice_stage.get("play_requests") or 0
            if requested > pool_size and pool_size > 16:
                problems.append(f"voice pool grew to {pool_size} slots; it must stay bounded")
    else:
        if arguments.exit_code == 0:
            problems.append("expected a non-zero exit code")
        if ok:
            problems.append("expected the result marker to report ok=false")
        if arguments.expect_stage and result.get("stage") != arguments.expect_stage:
            problems.append(f"expected failure at stage {arguments.expect_stage!r}, "
                            f"got {result.get('stage')!r}")
        if arguments.expect_reason and result.get("reason") != arguments.expect_reason:
            problems.append(f"expected reason {arguments.expect_reason!r}, "
                            f"got {result.get('reason')!r}")

    print(f"stages reported: {len(stages)}")
    for stage in stages:
        print(f"  [{'PASS' if stage.get('ok') else 'FAIL'}] {stage.get('stage')}")
    if audio:
        print(f"audio backend  : {audio.get('backend')!r} "
              f"(configured {result.get('configured_backend')!r})")
    if audio_details:
        # Informational: CI runners have no audio device and that is fine.
        print(f"output device  : {audio_details.get('output_device')} "
              f"{audio_details.get('output_device_name')!r}")
        print(f"voice pool     : size={audio_details.get('voice_pool_size')} "
              f"started={audio_details.get('voice_started')} "
              f"evicted={audio_details.get('voice_evicted')}")
        print(f"effect pool    : size={audio_details.get('effect_pool_size')} "
              f"started={audio_details.get('effect_started')} "
              f"evicted={audio_details.get('effect_evicted')}")
        print(f"missing files  : {audio_details.get('missing_files')} (expected > 0)")
        print(f"effect preload : {audio_details.get('preloaded_effects')} "
              f"fallbacks={audio_details.get('effect_fallbacks')}")
    if video:
        print(f"video          : reason={video.get('reason')!r} "
              f"available={video.get('available')} loaded={video.get('loaded')} "
              f"fallback={video.get('fallback')} error={video.get('error')!r}")
    if result:
        print(f"result: ok={result.get('ok')} stage={result.get('stage')!r} "
              f"reason={result.get('reason')!r} exit_code={result.get('exit_code')!r}")
        criticals = result.get("qt_critical_messages")
        if criticals:
            print(f"Qt critical messages: {criticals}")

    if problems:
        print("\nMultimedia smoke contract violated:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("\nMultimedia smoke contract OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
