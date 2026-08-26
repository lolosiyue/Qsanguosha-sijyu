#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final


class Result(StrEnum):
    PASS = "PASS"
    FAIL = "FAIL"
    CRASH = "CRASH"
    TIMEOUT = "TIMEOUT"


@dataclass(frozen=True, slots=True)
class CaseResult:
    name: str
    result: Result
    return_code: int | None
    report: str
    artifact_dir: str
    error: str = ""


RUNNER_EXIT_CODES: Final[set[int]] = set(range(7))


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Run local Room askFor response UI cases, one GUI process per case."
    )
    parser.add_argument("--exe", type=Path, default=root / "debug" / "QSanguosha.exe")
    parser.add_argument("--case", dest="case_files", type=Path, action="append")
    parser.add_argument("--cases", type=Path, default=root / "tests" / "skill_ui_runner" / "cases")
    parser.add_argument("--artifact-root", type=Path, default=root / "artifacts" / "skill-ui")
    parser.add_argument("--runtime-root", type=Path, default=root)
    parser.add_argument("--qt-root", type=Path)
    parser.add_argument("--case-timeout-ms", type=int, default=5000)
    parser.add_argument("--process-timeout-seconds", type=float, default=30.0)
    parser.add_argument("--show-ui", action="store_true")
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
    return sorted(path.resolve() for path in args.cases.glob("*.json"))


def configure_environment(args: argparse.Namespace) -> dict[str, str]:
    env = os.environ.copy()
    if os.name != "nt":
        if not args.show_ui:
            env.setdefault("QT_QPA_PLATFORM", "offscreen")
        return env

    env["QT_QPA_PLATFORM"] = "windows"
    qt_root = args.qt_root or (Path(env["QTDIR"]) if env.get("QTDIR") else None)
    candidates: list[Path] = []
    if qt_root:
        candidates.append(qt_root.resolve() / "plugins")
        qt_bin = str(qt_root.resolve() / "bin")
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")
    candidates.append(args.exe.resolve().parent)
    for candidate in candidates:
        platform_dir = candidate / "platforms"
        if (platform_dir / "qwindowsd.dll").is_file() or (platform_dir / "qwindows.dll").is_file():
            env["QT_PLUGIN_PATH"] = str(candidate)
            env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(platform_dir)
            break
    return env


def decode_output(value: bytes | str | None) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return value.decode("utf-8", errors="replace")


def dump_files(runtime_root: Path) -> set[Path]:
    dump_dir = runtime_root / "dmp"
    if not dump_dir.is_dir():
        return set()
    return {path.resolve() for path in dump_dir.glob("*.dmp")}


def copy_new_dumps(before: set[Path], runtime_root: Path, artifact_dir: Path) -> None:
    for dump in dump_files(runtime_root) - before:
        shutil.copy2(dump, artifact_dir / dump.name)


def run_case(path: Path, args: argparse.Namespace, env: dict[str, str]) -> CaseResult:
    name = case_name(path)
    artifact_dir = (args.artifact_root / name).resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)
    report = artifact_dir / "report.json"
    command = [
        str(args.exe.resolve()),
        "--local-response-ui-case",
        str(path),
        "--local-response-ui-report",
        str(report),
        "--screenshot-dir",
        str(artifact_dir),
        "--case-timeout-ms",
        str(args.case_timeout_ms),
        "--screenshot-on-failure",
    ]
    if args.show_ui:
        command.append("--show-ui")

    before_dumps = dump_files(args.runtime_root)
    timeout = max(args.process_timeout_seconds, args.case_timeout_ms / 1000.0 + 5.0)
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    try:
        completed = subprocess.run(
            command,
            cwd=args.runtime_root,
            env=env,
            capture_output=True,
            check=False,
            timeout=timeout,
            creationflags=creationflags,
        )
        stdout = decode_output(completed.stdout)
        stderr = decode_output(completed.stderr)
        return_code: int | None = completed.returncode
        if completed.returncode == 0:
            result = Result.PASS
            error = ""
        elif completed.returncode in RUNNER_EXIT_CODES:
            result = Result.FAIL
            error = f"runner exited {completed.returncode}"
        else:
            result = Result.CRASH
            error = f"process crashed/exited {completed.returncode}"
    except subprocess.TimeoutExpired as exc:
        stdout = decode_output(exc.stdout)
        stderr = decode_output(exc.stderr)
        return_code = None
        result = Result.TIMEOUT
        error = f"process exceeded {timeout:.1f}s"

    (artifact_dir / "stdout.txt").write_text(stdout, encoding="utf-8")
    (artifact_dir / "stderr.txt").write_text(stderr, encoding="utf-8")
    copy_new_dumps(before_dumps, args.runtime_root, artifact_dir)
    if result is Result.PASS:
        try:
            report_result = json.loads(report.read_text(encoding="utf-8")).get("result")
        except (OSError, json.JSONDecodeError) as exc:
            result = Result.FAIL
            error = f"missing or invalid report: {exc}"
        else:
            if report_result != "PASS":
                result = Result.FAIL
                error = f"report result is {report_result!r}"
    return CaseResult(name, result, return_code, str(report), str(artifact_dir), error)


def main() -> int:
    args = parse_args()
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    cases = discover_cases(args)
    if not args.exe.is_file():
        raise SystemExit(f"runner executable not found: {args.exe}")
    if not cases:
        raise SystemExit("no case files found")
    env = configure_environment(args)
    results = [run_case(path, args, env) for path in cases]
    args.artifact_root.mkdir(parents=True, exist_ok=True)
    summary_path = args.artifact_root / "summary.json"
    summary_path.write_text(
        json.dumps({"results": [asdict(item) for item in results]}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    for item in results:
        detail = f" - {item.error}" if item.error else ""
        print(f"{item.result.value} {item.name}{detail}")
    print(f"SUMMARY {summary_path.resolve()}")
    return 0 if all(item.result is Result.PASS for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
