#!/usr/bin/env python3
"""Validate the Linux GUI M1 startup smoke output.

The GUI writes one ``UI_STARTUP_STAGE`` line per startup stage and exactly one
``UI_STARTUP_RESULT`` line before exiting.  This validator is the CI-side half of
that contract: it refuses to pass when the result marker is missing, when a
required stage never reported ``ok``, or when the process exit code disagrees
with the marker.

Optional art assets (icons, splash art, audio) are deliberately not committed, so
the GUI logs warnings for them on a clean checkout.  Those warnings are reported
but never fail the run - a real QML component failure surfaces as a failed
``home_scene`` stage instead.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

STAGE_MARKER = "UI_STARTUP_STAGE"
RESULT_MARKER = "UI_STARTUP_RESULT"
SCHEMA_VERSION = 1

REQUIRED_STAGES = (
    "application",
    "engine",
    "main_window",
    "event_loop",
    "home_scene",
    "shutdown",
)


def parse_markers(text: str) -> tuple[list[dict], list[dict]]:
    stages: list[dict] = []
    results: list[dict] = []
    for line in text.splitlines():
        line = line.strip()
        for marker, sink in ((STAGE_MARKER, stages), (RESULT_MARKER, results)):
            prefix = marker + " "
            if not line.startswith(prefix):
                continue
            payload = line[len(prefix):].strip()
            try:
                sink.append(json.loads(payload))
            except json.JSONDecodeError as error:
                raise SystemExit(f"{marker} payload is not valid JSON: {error}\n  {payload}")
            break
    return stages, results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="combined stdout+stderr of the startup smoke")
    parser.add_argument("--exit-code", type=int, required=True,
                        help="exit code the startup smoke process returned")
    parser.add_argument("--expect", choices=("pass", "fail"), default="pass")
    parser.add_argument("--expect-stage", default=None,
                        help="with --expect fail: the stage the result must blame")
    parser.add_argument("--expect-reason", default=None,
                        help="with --expect fail: ok / stage_failed / timeout")
    arguments = parser.parse_args()

    text = arguments.log.read_text(encoding="utf-8", errors="replace")
    stages, results = parse_markers(text)

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

    if arguments.expect == "pass":
        if arguments.exit_code != 0:
            problems.append(f"expected exit code 0, got {arguments.exit_code}")
        if not ok:
            problems.append(f"startup smoke reported failure at stage "
                            f"{result.get('stage')!r}: {result.get('error')!r}")
        for stage in REQUIRED_STAGES:
            if stage not in stage_status:
                problems.append(f"stage {stage!r} was never reported")
            elif not stage_status[stage]:
                problems.append(f"stage {stage!r} reported ok=false")
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
    if result:
        print(f"result: ok={result.get('ok')} stage={result.get('stage')!r} "
              f"reason={result.get('reason')!r} exit_code={result.get('exit_code')!r}")
        warnings = result.get("optional_asset_warnings")
        if warnings:
            print(f"optional asset warnings (not fatal): {warnings}")
        criticals = result.get("qt_critical_messages")
        if criticals:
            print(f"Qt critical messages: {criticals}")

    if problems:
        print("\nUI startup smoke contract violated:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("\nUI startup smoke contract OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
