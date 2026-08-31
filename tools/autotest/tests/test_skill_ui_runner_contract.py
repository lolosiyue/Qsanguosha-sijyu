from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


RUNNER = Path(__file__).parents[1] / "skill_ui_runner.py"


def write_case(directory: Path, name: str) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{name}.json"
    path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "name": name,
                "bootstrap": {},
                "request": {"api": "askForChoice", "args": {}},
                "actions": [],
            }
        ),
        encoding="utf-8",
    )
    return path


def write_fake_runner(directory: Path) -> Path:
    body = (
        "from pathlib import Path\n"
        "import json\n"
        "import sys\n"
        "args = sys.argv[1:]\n"
        "if '--local-response-ui-capabilities' in args:\n"
        "    print(json.dumps({'schema_version': 1, 'auto': True, 'show': True, 'inspect': True}))\n"
        "    raise SystemExit(0)\n"
        "report = Path(args[args.index('--local-response-ui-report') + 1])\n"
        "report.parent.mkdir(parents=True, exist_ok=True)\n"
        "report.write_text(json.dumps({'mode': 'inspect', 'result': 'INSPECTED', "
        "'reply_received': False, 'closed_by_user': True}), encoding='utf-8')\n"
        "Path(report.parent / 'argv.txt').write_text('\\n'.join(args), encoding='utf-8')\n"
    )
    child = directory / "fake_skill_ui.py"
    if os.name == "nt":
        # Windows：批次檔 launcher 轉發到 python child
        child.write_text(body, encoding="utf-8")
        launcher = directory / "fake_skill_ui.cmd"
        launcher.write_text(
            f'@"{sys.executable}" "%~dp0fake_skill_ui.py" %*\n',
            encoding="utf-8",
        )
        return launcher
    # POSIX：無 .cmd，child 直接加 shebang + 執行位元當 exe
    launcher = child
    launcher.write_text("#!/usr/bin/env python3\n" + body, encoding="utf-8")
    mode = launcher.stat().st_mode
    launcher.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return launcher


def invoke(*arguments: str, environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(RUNNER), *arguments],
        capture_output=True,
        text=True,
        check=False,
        env=environment,
    )


def test_lists_case_stems_without_requiring_executable(tmp_path: Path) -> None:
    cases = tmp_path / "cases"
    write_case(cases, "ask_for_choice")
    write_case(cases, "ask_for_card_response")

    result = invoke("--cases", str(cases), "--list-cases")

    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines() == ["ask_for_card_response", "ask_for_choice"]


def test_inspect_resolves_one_stem_and_forwards_interactive_mode(tmp_path: Path) -> None:
    cases = tmp_path / "cases"
    write_case(cases, "ask_for_choice")
    executable = write_fake_runner(tmp_path)
    artifacts = tmp_path / "artifacts"

    result = invoke(
        "--exe",
        str(executable),
        "--cases",
        str(cases),
        "--artifact-root",
        str(artifacts),
        "--runtime-root",
        str(tmp_path),
        "--inspect",
        "ask_for_choice",
    )

    assert result.returncode == 0, result.stderr
    argv = (artifacts / "ask_for_choice" / "argv.txt").read_text(encoding="utf-8").splitlines()
    assert "--inspect-ui" in argv
    assert "--show-ui" not in argv
    assert "INSPECTED ask_for_choice" in result.stdout


def test_inspect_rejects_duplicate_stems_before_launch(tmp_path: Path) -> None:
    cases = tmp_path / "cases"
    write_case(cases / "one", "ask_for_choice")
    write_case(cases / "two", "ask_for_choice")
    executable = write_fake_runner(tmp_path)

    result = invoke(
        "--exe",
        str(executable),
        "--cases",
        str(cases),
        "--runtime-root",
        str(tmp_path),
        "--inspect",
        "ask_for_choice",
    )

    assert result.returncode != 0
    assert "multiple cases" in result.stderr.lower()


def test_rejects_old_executable_without_starting_lobby(tmp_path: Path) -> None:
    cases = tmp_path / "cases"
    write_case(cases, "ask_for_choice")
    executable = tmp_path / "old-QSanguosha.exe"
    executable.write_bytes(b"not a runner executable")

    result = invoke(
        "--exe",
        str(executable),
        "--cases",
        str(cases),
        "--runtime-root",
        str(tmp_path),
        "--inspect",
        "ask_for_choice",
    )

    assert result.returncode != 0
    assert "does not include the local response UI runner" in result.stderr


def test_build_runs_only_incremental_gui_target(tmp_path: Path) -> None:
    if os.name != "nt":
        # build_gui 硬編碼 VS2026 build tree 與 .cmd PATH 慣例，僅在 Windows 有意義
        return
    cases = tmp_path / "cases"
    write_case(cases, "ask_for_choice")
    executable = write_fake_runner(tmp_path)
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    cmake_log = tmp_path / "cmake-argv.txt"
    fake_cmake = fake_bin / "cmake.cmd"
    fake_cmake.write_text(
        f'@echo %* > "{cmake_log}"\n@exit /b 0\n',
        encoding="utf-8",
    )
    cache_dir = tmp_path / "builds" / "cmake-vs2026"
    cache_dir.mkdir(parents=True)
    (cache_dir / "CMakeCache.txt").write_text(
        f"CMAKE_COMMAND:INTERNAL={fake_cmake}\n",
        encoding="utf-8",
    )
    environment = dict(os.environ)
    environment["PATH"] = str(fake_bin) + os.pathsep + environment.get("PATH", "")

    result = invoke(
        "--exe",
        str(executable),
        "--cases",
        str(cases),
        "--runtime-root",
        str(tmp_path),
        "--build-root",
        str(tmp_path),
        "--artifact-root",
        str(tmp_path / "artifacts"),
        "--build",
        "--inspect",
        "ask_for_choice",
        environment=environment,
    )

    assert result.returncode == 0, result.stderr
    build_arguments = cmake_log.read_text(encoding="utf-8").strip()
    assert build_arguments == "--build --preset debug --target QSanguosha --parallel 8"
    assert "ctest" not in build_arguments
    assert "configure" not in build_arguments


def main() -> int:
    tests = (
        test_lists_case_stems_without_requiring_executable,
        test_inspect_resolves_one_stem_and_forwards_interactive_mode,
        test_inspect_rejects_duplicate_stems_before_launch,
        test_rejects_old_executable_without_starting_lobby,
        test_build_runs_only_incremental_gui_target,
    )
    for test in tests:
        with tempfile.TemporaryDirectory() as directory:
            test(Path(directory))
        print(f"PASS {test.__name__}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
