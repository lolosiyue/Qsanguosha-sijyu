from __future__ import annotations

import json
import os
import shutil
import subprocess
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final


class Result(StrEnum):
    PASS = "PASS"
    INSPECTED = "INSPECTED"
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


@dataclass(frozen=True, slots=True)
class LaunchConfig:
    executable: Path
    artifact_root: Path
    runtime_root: Path
    case_timeout_ms: int
    process_timeout_seconds: float
    show_ui: bool


RUNNER_EXIT_CODES: Final[set[int]] = set(range(7))


def configure_environment(executable: Path, qt_root: Path | None, visible: bool) -> dict[str, str]:
    environment = os.environ.copy()
    if os.name != "nt":
        if not visible:
            environment.setdefault("QT_QPA_PLATFORM", "offscreen")
        return environment

    environment["QT_QPA_PLATFORM"] = "windows"
    resolved_qt = qt_root or (Path(environment["QTDIR"]) if environment.get("QTDIR") else None)
    candidates: list[Path] = []
    if resolved_qt:
        candidates.append(resolved_qt.resolve() / "plugins")
        environment["PATH"] = (
            str(resolved_qt.resolve() / "bin") + os.pathsep + environment.get("PATH", "")
        )
    candidates.append(executable.resolve().parent)
    for candidate in candidates:
        platform_dir = candidate / "platforms"
        if (platform_dir / "qwindowsd.dll").is_file() or (platform_dir / "qwindows.dll").is_file():
            environment["QT_PLUGIN_PATH"] = str(candidate)
            environment["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(platform_dir)
            break
    return environment


def probe_capabilities(executable: Path, runtime_root: Path, environment: dict[str, str]) -> None:
    if executable.suffix.lower() == ".exe":
        marker = b"--local-response-ui-capabilities"
        binary = executable.read_bytes()
        if marker not in binary and marker.decode().encode("utf-16-le") not in binary:
            raise SystemExit(
                f"runner executable does not include the local response UI runner: {executable}"
            )
    try:
        completed = subprocess.run(
            [str(executable), "--local-response-ui-capabilities"],
            cwd=runtime_root,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"could not probe local response UI runner: {exc}") from exc
    try:
        capabilities = json.loads(completed.stdout.strip())
    except json.JSONDecodeError as exc:
        raise SystemExit(
            f"runner executable does not include the local response UI runner: {executable}"
        ) from exc
    expected = {"schema_version": 1, "auto": True, "show": True, "inspect": True}
    if completed.returncode != 0 or any(capabilities.get(key) != value for key, value in expected.items()):
        raise SystemExit(f"runner capabilities are incompatible: {capabilities!r}")


def dump_files(runtime_root: Path) -> set[Path]:
    dump_dir = runtime_root / "dmp"
    return {path.resolve() for path in dump_dir.glob("*.dmp")} if dump_dir.is_dir() else set()


def copy_new_dumps(before: set[Path], runtime_root: Path, artifact_dir: Path) -> None:
    for dump in dump_files(runtime_root) - before:
        shutil.copy2(dump, artifact_dir / dump.name)


def load_report_result(report: Path) -> tuple[Result | None, str]:
    try:
        value = json.loads(report.read_text(encoding="utf-8")).get("result")
        return Result(value), ""
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        return None, f"missing or invalid report: {exc}"


def run_case(path: Path, name: str, config: LaunchConfig, environment: dict[str, str],
             inspect: bool = False) -> CaseResult:
    artifact_dir = (config.artifact_root / name).resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)
    report = artifact_dir / "report.json"
    command = [
        str(config.executable.resolve()),
        "--local-response-ui-case", str(path),
        "--local-response-ui-report", str(report),
        "--screenshot-dir", str(artifact_dir),
        "--case-timeout-ms", str(config.case_timeout_ms),
        "--screenshot-on-failure",
    ]
    if inspect:
        command.append("--inspect-ui")
    elif config.show_ui:
        command.append("--show-ui")

    before_dumps = dump_files(config.runtime_root)
    if inspect:
        completed = subprocess.run(
            command, cwd=config.runtime_root, env=environment, check=False, creationflags=0
        )
        copy_new_dumps(before_dumps, config.runtime_root, artifact_dir)
        report_result, report_error = load_report_result(report)
        allowed = {Result.PASS, Result.INSPECTED}
        if completed.returncode == 0 and report_result in allowed:
            return CaseResult(name, report_result, completed.returncode, str(report), str(artifact_dir))
        error = report_error or f"runner exited {completed.returncode}; report result is {report_result}"
        return CaseResult(name, Result.FAIL, completed.returncode, str(report), str(artifact_dir), error)

    timeout = max(config.process_timeout_seconds, config.case_timeout_ms / 1000.0 + 5.0)
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    try:
        completed = subprocess.run(
            command,
            cwd=config.runtime_root,
            env=environment,
            capture_output=True,
            check=False,
            timeout=timeout,
            creationflags=creationflags,
        )
        stdout = completed.stdout.decode("utf-8", errors="replace")
        stderr = completed.stderr.decode("utf-8", errors="replace")
        return_code: int | None = completed.returncode
        result = Result.PASS if completed.returncode == 0 else (
            Result.FAIL if completed.returncode in RUNNER_EXIT_CODES else Result.CRASH
        )
        error = "" if result is Result.PASS else f"runner exited {completed.returncode}"
    except subprocess.TimeoutExpired as exc:
        stdout = (exc.stdout or b"").decode("utf-8", errors="replace")
        stderr = (exc.stderr or b"").decode("utf-8", errors="replace")
        return_code = None
        result = Result.TIMEOUT
        error = f"process exceeded {timeout:.1f}s"
    (artifact_dir / "stdout.txt").write_text(stdout, encoding="utf-8")
    (artifact_dir / "stderr.txt").write_text(stderr, encoding="utf-8")
    copy_new_dumps(before_dumps, config.runtime_root, artifact_dir)
    if result is Result.PASS:
        report_result, report_error = load_report_result(report)
        if report_result is not Result.PASS:
            result = Result.FAIL
            error = report_error or f"report result is {report_result!r}"
    return CaseResult(name, result, return_code, str(report), str(artifact_dir), error)


def cmake_from_cache(cache: Path) -> Path | None:
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_COMMAND:INTERNAL="):
            candidate = Path(line.split("=", 1)[1])
            return candidate if candidate.is_file() else None
    return None


def build_gui(root: Path) -> None:
    cache = root / "builds" / "cmake-vs2026" / "CMakeCache.txt"
    if not cache.is_file():
        raise SystemExit(
            "No configured build tree was found under "
            f"{root / 'builds' / 'cmake-vs2026'}. "
            "Run the one-time command: cmake --preset vs2026-x64"
        )
    cmake = shutil.which("cmake") or cmake_from_cache(cache)
    if not cmake:
        raise SystemExit("CMake was not found on PATH or in the configured build tree")
    subprocess.run(
        [str(cmake), "--build", "--preset", "debug", "--target", "QSanguosha", "--parallel", "8"],
        cwd=root,
        check=True,
    )
