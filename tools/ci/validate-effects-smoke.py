#!/usr/bin/env python3
"""Validate the Linux GUI M2B-B effects smoke output.

The GUI writes one ``EFFECTS_STAGE`` line per stage, exactly one
``EFFECTS_PROFILE_RESULT`` line, and exactly one ``EFFECTS_RESULT`` line before
exiting.  This validator is the CI-side half of that contract.

Three things it deliberately does *not* do:

* it never treats "no console error" as success - every stage has to report
  itself, and the profile has to report the gates it actually resolved;
* it never compares pixels.  Screenshots are failure artifacts, not gates; and
* it never requires production art.  A clean checkout has none, so "a missing
  asset degrades to a static UI" is the assertion, not "the asset was there".
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

STAGE_MARKER = "EFFECTS_STAGE"
RESULT_MARKER = "EFFECTS_RESULT"
PROFILE_MARKER = "EFFECTS_PROFILE_RESULT"
SCHEMA_VERSION = 1

REQUIRED_STAGES = (
    "policy",
    "completion",
    "animation",
    "gif",
    "spine",
    "budget",
    "shutdown",
)

KNOWN_PROFILES = ("full", "reduced", "none")

# Which high-cost objects each profile is allowed to construct.  This mirrors
# EffectsSmokeReport::budgetFor(); keeping it here as well means a regression
# that also "fixes" the C++ side of the budget still fails CI.
OBJECT_BUDGET = {
    "none": {"spine_items": 0, "movie_objects": 0, "qml_overlays": 0, "video_objects": 0},
    "reduced": {"spine_items": 0, "qml_overlays": 0, "video_objects": 0},
    "full": {},
}

# Gates that must be off for a given profile, no matter what the settings say.
FORBIDDEN_GATES = {
    "none": ("animations", "spine", "gif", "video", "qml_effects", "decorative_delay"),
    "reduced": ("spine", "video", "qml_effects"),
    "full": (),
}


def parse_markers(text: str) -> tuple[list[dict], list[dict], list[dict]]:
    stages: list[dict] = []
    results: list[dict] = []
    profiles: list[dict] = []
    for line in text.splitlines():
        line = line.strip()
        for marker, sink in ((STAGE_MARKER, stages), (RESULT_MARKER, results),
                             (PROFILE_MARKER, profiles)):
            prefix = marker + " "
            if not line.startswith(prefix):
                continue
            payload = line[len(prefix):].strip()
            try:
                sink.append(json.loads(payload))
            except json.JSONDecodeError as error:
                raise SystemExit(f"{marker} payload is not valid JSON: {error}\n  {payload}")
            break
    return stages, results, profiles


def check_budget(profile: str, counters: dict, problems: list[str]) -> None:
    budget = OBJECT_BUDGET.get(profile, {})
    for key, limit in budget.items():
        actual = counters.get(key)
        if not isinstance(actual, (int, float)):
            problems.append(f"the {key!r} counter is missing from the budget stage")
            continue
        if actual > limit:
            problems.append(f"profile {profile!r} created {int(actual)} {key} "
                            f"(budget {limit})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="combined stdout+stderr of the smoke")
    parser.add_argument("--exit-code", type=int, required=True)
    parser.add_argument("--expect", choices=("pass", "fail"), default="pass")
    parser.add_argument("--expect-profile", default=None, choices=KNOWN_PROFILES,
                        help="the profile the run was asked for")
    parser.add_argument("--expect-stage", default=None)
    parser.add_argument("--expect-reason", default=None,
                        help="with --expect fail: ok / stage_failed / timeout")
    arguments = parser.parse_args()

    text = arguments.log.read_text(encoding="utf-8", errors="replace")
    stages, results, profiles = parse_markers(text)

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
    profile_payload = profiles[-1] if profiles else {}
    resolved = profile_payload.get("profile")

    if arguments.expect == "pass":
        if arguments.exit_code != 0:
            problems.append(f"expected exit code 0, got {arguments.exit_code}")
        if not ok:
            problems.append(f"effects smoke reported failure at stage "
                            f"{result.get('stage')!r}: {result.get('error')!r}")
        for stage in REQUIRED_STAGES:
            if stage not in stage_status:
                problems.append(f"stage {stage!r} was never reported")
            elif not stage_status[stage]:
                problems.append(f"stage {stage!r} reported ok=false")

        if not profiles:
            problems.append(f"no {PROFILE_MARKER} line - the run never reported which "
                            "profile it actually resolved")
        elif len(profiles) > 1:
            problems.append(f"expected exactly one {PROFILE_MARKER} line, got {len(profiles)}")

        if resolved not in KNOWN_PROFILES:
            problems.append(f"unknown resolved profile {resolved!r}")
        if arguments.expect_profile and resolved != arguments.expect_profile:
            problems.append(f"asked for profile {arguments.expect_profile!r} but the GUI "
                            f"resolved {resolved!r}")
        # An explicit --effects-profile must register as the source; if it fell
        # back to settings the matrix is silently running one profile three times.
        if arguments.expect_profile and profile_payload.get("source") != "cli":
            problems.append(f"--effects-profile did not become the resolution source "
                            f"(got {profile_payload.get('source')!r})")

        for gate in FORBIDDEN_GATES.get(resolved or "", ()):
            if profile_payload.get(gate):
                problems.append(f"profile {resolved!r} reported gate {gate!r} enabled")

        # exactly-once completion, in the run's own numbers.
        completion = stage_by_name.get("completion", {})
        for key in ("finished_animation", "skipped_animation",
                    "destroyed_during_animation", "stalled_animation_watchdog"):
            value = completion.get(key)
            if value != 1:
                problems.append(f"completion case {key!r} delivered {value!r} callback(s), "
                                "expected exactly 1")
        if completion.get("dead_context") not in (0, None):
            problems.append("a completion was delivered to a destroyed context")

        # Missing/broken assets must degrade, never hang or vanish.
        animation = stage_by_name.get("animation", {})
        if animation and not animation.get("missing_emotion_returns_null"):
            problems.append("a missing emotion did not return nullptr; the lightbox "
                            "fallback that depends on it cannot fire")
        gif = stage_by_name.get("gif", {})
        for name, entry in (gif.get("cases", {}) or {}).items():
            if not entry.get("has_visible_content"):
                problems.append(f"GIF case {name!r} left the widget with nothing to show")
        spine = stage_by_name.get("spine", {})
        for name, entry in (spine.get("cases", {}) or {}).items():
            if entry.get("loaded"):
                problems.append(f"Spine case {name!r} claimed to load a broken asset")

        budget_stage = stage_by_name.get("budget", {})
        check_budget(resolved or "", budget_stage.get("counters", {}) or {}, problems)

        shutdown = stage_by_name.get("shutdown", {})
        if shutdown and not shutdown.get("main_window_hidden"):
            problems.append("the MainWindow was still visible after shutdown")
        # The anti-hang property, stated as a number: every completion the run
        # issued must have settled (delivered or cancelled) by the time it ends.
        pending = shutdown.get("completion_pending")
        if shutdown and pending not in (0, None):
            problems.append(f"{pending} completion(s) never settled - a flow is still "
                            "waiting for a callback that will never arrive")
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

    print(f"resolved profile: {resolved!r} (source {profile_payload.get('source')!r})")
    print(f"stages reported : {len(stages)}")
    for stage in stages:
        print(f"  [{'PASS' if stage.get('ok') else 'FAIL'}] {stage.get('stage')}")
    counters = (stage_by_name.get("budget", {}) or {}).get("counters", {}) or {}
    if counters:
        print("objects created :")
        for key in sorted(counters):
            print(f"  {key} = {counters[key]}")
    completion = stage_by_name.get("completion", {}) or {}
    if completion:
        print(f"completion      : delivered={completion.get('delivered_total')} "
              f"cancelled={completion.get('cancelled_total')}")

    if problems:
        print("\nEffects smoke validation failed:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("\nEffects smoke validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
