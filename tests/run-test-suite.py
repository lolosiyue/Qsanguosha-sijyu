#!/usr/bin/env python3
"""Run named test commands as one failure-isolating CTest suite."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SuiteCase:
    name: str
    command: list[str]


def parse_case(value: str) -> SuiteCase:
    parts = value.split("::")
    if len(parts) < 2 or not parts[0] or any(not part for part in parts[1:]):
        raise argparse.ArgumentTypeError(
            "case must use NAME::PROGRAM[::ARG...] syntax"
        )
    return SuiteCase(parts[0], parts[1:])


def write_output(output: str) -> None:
    if not output:
        return
    sys.stdout.write(output)
    if not output.endswith("\n"):
        sys.stdout.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", required=True)
    parser.add_argument("--case", action="append", type=parse_case, required=True)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--working-directory", type=Path, default=Path.cwd())
    args = parser.parse_args()

    results: list[tuple[SuiteCase, bool, str]] = []
    for test_case in args.case:
        passed = False
        detail = ""
        try:
            completed = subprocess.run(
                test_case.command,
                cwd=args.working_directory,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                timeout=args.timeout,
            )
            write_output(completed.stdout)
            passed = completed.returncode == 0
            if not passed:
                detail = f"exit code {completed.returncode}"
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            write_output(output)
            detail = f"timed out after {args.timeout:g}s"
        except OSError as error:
            detail = f"could not start: {error}"

        if passed:
            print(f"[PASS] {test_case.name}")
        else:
            print(f"[FAIL] {test_case.name}: {detail}", file=sys.stderr)
        results.append((test_case, passed, detail))

    passed_count = sum(1 for _, passed, _ in results if passed)
    print(f"\n{args.suite}\n")
    for test_case, passed, detail in results:
        suffix = "" if passed else f": {detail}"
        print(f"{'PASS' if passed else 'FAIL'} {test_case.name}{suffix}")
    print(f"\nTOTAL: {len(results)}")
    print(f"PASS: {passed_count}")
    print(f"FAIL: {len(results) - passed_count}")
    return 0 if passed_count == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
