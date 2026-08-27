#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///

from __future__ import annotations

import argparse
import ctypes
import json
import sys
from dataclasses import asdict
from pathlib import Path

from skill_ui_runner_support import (
    CaseResult,
    LaunchConfig,
    Result,
    build_gui,
    configure_environment,
    probe_capabilities,
    run_case,
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Run or inspect local Room askFor response UI cases."
    )
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--case", dest="case_files", type=Path, action="append")
    parser.add_argument("--cases", type=Path, default=root / "tests" / "skill_ui_runner" / "cases")
    parser.add_argument("--artifact-root", type=Path, default=root / "artifacts" / "skill-ui")
    parser.add_argument("--runtime-root", type=Path, default=root)
    parser.add_argument("--qt-root", type=Path)
    parser.add_argument("--case-timeout-ms", type=int, default=5000)
    parser.add_argument("--process-timeout-seconds", type=float, default=30.0)
    parser.add_argument("--show-ui", action="store_true")
    parser.add_argument("--inspect", metavar="CASE_STEM")
    parser.add_argument("--list-cases", action="store_true")
    parser.add_argument("--build", action="store_true")
    return parser.parse_args()


def case_name(path: Path) -> str:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return path.stem
    name = value.get("name")
    return name if isinstance(name, str) and name else path.stem


def discover_cases(args: argparse.Namespace) -> list[Path]:
    if args.case_files:
        return [path.resolve() for path in args.case_files]
    return sorted(path.resolve() for path in args.cases.rglob("*.json"))


def resolve_inspect_case(cases: list[Path], stem: str) -> Path:
    matches = [path for path in cases if path.stem == stem]
    if not matches:
        raise SystemExit(f"no case matches stem {stem!r}")
    if len(matches) > 1:
        joined = "\n  ".join(str(path) for path in matches)
        raise SystemExit(f"multiple cases match stem {stem!r}:\n  {joined}")
    return matches[0]


def discover_executable(explicit: Path | None) -> Path:
    if explicit:
        candidate = explicit.resolve()
        if candidate.is_file():
            return candidate
        raise SystemExit(f"runner executable not found: {candidate}")
    root = repo_root()
    candidates = (
        root / "builds" / "cmake-vs2026" / "Debug" / "QSanguosha.exe",
        root / "debug" / "QSanguosha.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise SystemExit("runner executable not found; use --build or --exe")


def write_summary(artifact_root: Path, results: list[CaseResult]) -> Path:
    artifact_root.mkdir(parents=True, exist_ok=True)
    path = artifact_root / "summary.json"
    path.write_text(
        json.dumps({"results": [asdict(item) for item in results]}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    return path


def main() -> int:
    args = parse_args()
    cases = discover_cases(args)
    if args.list_cases:
        for path in cases:
            print(path.stem)
        return 0
    if not cases:
        raise SystemExit("no case files found")
    if args.build:
        build_gui(repo_root())

    executable = discover_executable(args.exe)
    selected = [resolve_inspect_case(cases, args.inspect)] if args.inspect else cases
    visible = bool(args.inspect or args.show_ui)
    environment = configure_environment(executable, args.qt_root, visible)
    probe_capabilities(executable, args.runtime_root, environment)
    if sys.platform == "win32" and not args.inspect:
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)

    config = LaunchConfig(
        executable=executable,
        artifact_root=args.artifact_root,
        runtime_root=args.runtime_root,
        case_timeout_ms=args.case_timeout_ms,
        process_timeout_seconds=args.process_timeout_seconds,
        show_ui=args.show_ui,
    )
    results = [
        run_case(path, case_name(path), config, environment, inspect=bool(args.inspect))
        for path in selected
    ]
    summary_path = write_summary(args.artifact_root, results)
    for item in results:
        detail = f" - {item.error}" if item.error else ""
        print(f"{item.result.value} {item.name}{detail}")
    print(f"SUMMARY {summary_path.resolve()}")
    accepted = {Result.PASS, Result.INSPECTED} if args.inspect else {Result.PASS}
    return 0 if all(item.result in accepted for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
