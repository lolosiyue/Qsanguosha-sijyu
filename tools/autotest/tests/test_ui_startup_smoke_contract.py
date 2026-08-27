"""Source-level contract for the Linux GUI M1 startup smoke.

The C++ half of the contract (marker schema, exit codes, optional-asset
classification) lives in ``tests/ui_startup_smoke``.  What that test cannot see
is *where* ``--ui-startup-smoke`` is wired into ``main()``: the whole point of M1
is that this entry point does **not** short-circuit before ``QApplication`` the
way ``--local-response-ui-capabilities`` does.  These checks pin that down, plus
the runner-level timeout that keeps a hung Qt event loop from wedging CI.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MAIN = ROOT / "src" / "main.cpp"
CONTROLLER_H = ROOT / "src" / "ui" / "testing" / "ui-startup-smoke-controller.h"
CONTROLLER_CPP = ROOT / "src" / "ui" / "testing" / "ui-startup-smoke-controller.cpp"
REPORT_H = ROOT / "src" / "ui" / "testing" / "ui-startup-smoke-report.h"
MAINWINDOW_H = ROOT / "src" / "dialog" / "mainwindow.h"
MAINWINDOW_CPP = ROOT / "src" / "dialog" / "mainwindow.cpp"
RUNNER = ROOT / "tools" / "ci" / "linux-gui-startup-smoke.sh"
VALIDATOR = ROOT / "tools" / "ci" / "validate-ui-startup-smoke.py"
WORKFLOWS = ROOT / ".github" / "workflows"
DOCS = ROOT / "docs" / "linux-development-environment.md"

FLAG = "--ui-startup-smoke"
TIMEOUT_FLAG = "--ui-startup-timeout-ms"


def read(path: Path) -> str:
    assert path.exists(), f"missing file: {path.relative_to(ROOT)}"
    return path.read_text(encoding="utf-8")


def test_startup_smoke_does_not_return_before_qapplication() -> None:
    text = read(MAIN)
    application_index = text.index("new QApplication(argc, argv)")

    # --local-response-ui-capabilities is the M0 binary capability query; it
    # answers and returns before any Q*Application exists.  The M1 startup smoke
    # must never join that block: no argv literal comparison, and no return
    # statement gated on the smoke, before QApplication exists.
    early_block = text[:application_index]
    assert f'"{FLAG}"' not in early_block, (
        f"{FLAG} is compared against argv before QApplication is created; that is "
        "the M0 capability-query shape, which would not prove the GUI starts"
    )
    for number, line in enumerate(early_block.splitlines(), start=1):
        code = line.split("//", 1)[0]
        if "return" not in code:
            continue
        assert "uiStartupSmoke" not in code and FLAG not in code, (
            f"src/main.cpp:{number} returns for the startup smoke before "
            "QApplication is created"
        )
    assert "--local-response-ui-capabilities" in early_block, (
        "the M0 capability query is expected to stay a pre-QApplication early return"
    )

    # The only pre-QApplication mention allowed is the headless guard, which must
    # steer the smoke *towards* the QApplication branch.
    guard = early_block
    assert "UiStartupSmokeController::isRequested" in guard, (
        "main() must detect the startup smoke before choosing between "
        "QCoreApplication and QApplication"
    )
    assert re.search(r"headlessApp\s*=\s*!uiStartupSmoke", guard), (
        "the startup smoke must be excluded from the headless QCoreApplication path"
    )

    begin_index = text.index("UiStartupSmokeController::begin")
    assert begin_index > application_index, (
        "UiStartupSmokeController::begin() must run after QApplication is constructed"
    )

    run_index = text.index("UiStartupSmokeController::run()")
    engine_index = text.index("EngineBootstrap::initialize()")
    assert run_index > engine_index, (
        "the startup smoke must run after the engine/runtime is initialized"
    )
    print("PASS test_startup_smoke_does_not_return_before_qapplication")


def test_startup_smoke_reuses_the_product_startup_path() -> None:
    text = read(CONTROLLER_CPP)
    assert "new MainWindow" in text, (
        "the startup smoke must create the real MainWindow, not a stand-in"
    )
    assert "qApp->exec()" in text, "the startup smoke must enter the Qt event loop"
    assert "homeSceneView()" in text, (
        "the startup smoke must inspect the real HomeScene QQuickWidget"
    )
    assert "rootObject()" in text, (
        "the ready condition must require a live QML root object"
    )

    # No duplicated HomeScene bootstrap: the smoke observes MainWindow, it does
    # not load HomeScene.qml itself.
    code = "\n".join(line.split("//", 1)[0] for line in text.splitlines())
    assert "HomeScene.qml" not in code, (
        "the startup smoke must not load HomeScene.qml on its own; it has to go "
        "through MainWindow's normal startup path"
    )
    assert "->setSource(" not in code, (
        "the startup smoke must not drive QQuickWidget::setSource itself"
    )
    print("PASS test_startup_smoke_reuses_the_product_startup_path")


def test_ready_condition_is_not_a_bare_timer() -> None:
    text = read(CONTROLLER_CPP)
    # A queued singleShot is how the smoke proves the event loop runs, but it may
    # never be the thing that declares success on its own.
    assert "isHomeSceneReady()" in text, (
        "the ready condition must consult MainWindow's HomeScene state"
    )
    assert "homeSceneFailed" in text, (
        "the smoke must react to a HomeScene load failure, not just to a timer"
    )
    assert "onSettled" in text, (
        "the smoke must keep running the event loop after HomeScene is ready to "
        "prove the top-level GUI object survives startup"
    )
    assert re.search(r"QTimer::singleShot\(0, qApp, &QCoreApplication::quit\)", text), (
        "the smoke must ask the application to quit normally rather than exiting "
        "from inside a callback"
    )
    print("PASS test_ready_condition_is_not_a_bare_timer")


def test_mainwindow_exposes_a_home_scene_seam_without_changing_startup() -> None:
    header = read(MAINWINDOW_H)
    for member in ("isHomeSceneReady", "homeSceneError", "homeSceneView",
                   "homeSceneReady", "homeSceneFailed"):
        assert member in header, f"MainWindow is missing the {member} startup seam"

    source = read(MAINWINDOW_CPP)
    # The seam has to hang off the pre-existing statusChanged handler; it must not
    # introduce a second QML load or change what normal startup does.
    assert source.count("homeView->setSource(homeUrl)") == 1, (
        "setupHomePage() must still load HomeScene exactly once"
    )
    assert "emit homeSceneReady()" in source
    assert "emit homeSceneFailed(m_homeSceneError)" in source
    print("PASS test_mainwindow_exposes_a_home_scene_seam_without_changing_startup")


def test_timeout_contract_is_dual_layered() -> None:
    header = read(REPORT_H)
    assert TIMEOUT_FLAG in header, f"{TIMEOUT_FLAG} must be part of the contract"

    controller = read(CONTROLLER_CPP)
    assert "failIfDeadlineExceeded" in controller, (
        "the app-level timeout must also cover the synchronous startup phases, "
        "where a QTimer cannot fire"
    )
    assert "UiStartupSmokeReport::Timeout" in controller, (
        "a timeout must report the timeout exit code"
    )
    assert "reportUnfinishedAtExit" in controller, (
        "every exit path must leave a UI_STARTUP_RESULT marker behind"
    )

    runner = read(RUNNER)
    assert "timeout --kill-after" in runner, (
        "the runner needs a process-level timeout on top of the app-level one"
    )
    assert "--ui-startup-timeout-ms" in runner
    assert "pgrep -g" in runner and "kill -KILL" in runner, (
        "the runner must not leak QSanguosha/Xvfb orphans into later CI steps, and "
        "must clean up by its own process group so it never kills an unrelated GUI"
    )
    print("PASS test_timeout_contract_is_dual_layered")


STAGES = ("application", "engine", "main_window", "event_loop", "home_scene", "shutdown")


def stage_line(stage: str, ok: bool = True) -> str:
    return "UI_STARTUP_STAGE " + json.dumps(
        {"schema_version": 1, "stage": stage, "ok": ok}, separators=(",", ":"))


def result_line(ok: bool, stage: str, exit_code: int, reason: str) -> str:
    payload = {"schema_version": 1, "ok": ok, "stage": stage,
               "exit_code": exit_code, "reason": reason}
    if not ok:
        payload["error"] = "synthetic failure"
    return "UI_STARTUP_RESULT " + json.dumps(payload, separators=(",", ":"))


def run_validator(log_text: str, *arguments: str) -> subprocess.CompletedProcess[str]:
    with tempfile.NamedTemporaryFile("w", suffix=".log", encoding="utf-8",
                                     delete=False) as handle:
        handle.write(log_text)
        log_path = handle.name
    try:
        return subprocess.run([sys.executable, str(VALIDATOR), log_path, *arguments],
                              capture_output=True, text=True, check=False)
    finally:
        Path(log_path).unlink(missing_ok=True)


def test_validator_accepts_a_complete_successful_run() -> None:
    log = "\n".join([*(stage_line(stage) for stage in STAGES),
                      result_line(True, "shutdown", 0, "ok")])
    result = run_validator(log, "--exit-code", "0")
    assert result.returncode == 0, result.stdout + result.stderr
    print("PASS test_validator_accepts_a_complete_successful_run")


def test_validator_fails_on_a_missing_result_marker() -> None:
    # A crash or a runner-level kill leaves the stage lines but no conclusion.
    log = "\n".join(stage_line(stage) for stage in STAGES)
    result = run_validator(log, "--exit-code", "0")
    assert result.returncode != 0, "a missing result marker must not pass"
    assert "UI_STARTUP_RESULT" in result.stderr
    print("PASS test_validator_fails_on_a_missing_result_marker")


def test_validator_fails_when_a_stage_is_missing() -> None:
    partial = [stage for stage in STAGES if stage != "home_scene"]
    log = "\n".join([*(stage_line(stage) for stage in partial),
                      result_line(True, "shutdown", 0, "ok")])
    result = run_validator(log, "--exit-code", "0")
    assert result.returncode != 0, "a skipped startup stage must not pass"
    assert "home_scene" in result.stderr
    print("PASS test_validator_fails_when_a_stage_is_missing")


def test_validator_fails_when_the_exit_code_disagrees_with_the_marker() -> None:
    log = "\n".join([*(stage_line(stage) for stage in STAGES),
                      result_line(True, "shutdown", 0, "ok")])
    result = run_validator(log, "--exit-code", "1")
    assert result.returncode != 0, "the process exit code must match the marker"
    print("PASS test_validator_fails_when_the_exit_code_disagrees_with_the_marker")


def test_validator_enforces_the_expected_failure_shape() -> None:
    log = "\n".join([stage_line("application"), stage_line("engine", ok=False),
                      result_line(False, "engine", 3, "timeout")])
    ok = run_validator(log, "--exit-code", "3", "--expect", "fail",
                       "--expect-reason", "timeout", "--expect-stage", "engine")
    assert ok.returncode == 0, ok.stdout + ok.stderr

    # A timeout must not be accepted where a plain stage failure was expected.
    mismatch = run_validator(log, "--exit-code", "3", "--expect", "fail",
                             "--expect-reason", "stage_failed")
    assert mismatch.returncode != 0, "the failure reason must be checked"

    # A failure result must never satisfy the passing expectation.
    passing = run_validator(log, "--exit-code", "3")
    assert passing.returncode != 0, "a failure must not satisfy --expect pass"
    print("PASS test_validator_enforces_the_expected_failure_shape")


def test_ci_runs_the_xcb_startup_smoke_under_xvfb() -> None:
    assert WORKFLOWS.is_dir(), f"missing directory: {WORKFLOWS.relative_to(ROOT)}"
    hosting = [path for path in sorted(WORKFLOWS.glob("*.yml"))
               if "linux-gui-startup-smoke.sh" in path.read_text(encoding="utf-8")]
    assert hosting, "no workflow runs tools/ci/linux-gui-startup-smoke.sh"

    combined = "\n".join(path.read_text(encoding="utf-8") for path in hosting)
    assert "xvfb" in combined.lower(), "CI must run the startup smoke under Xvfb"
    # Xvfb has to exercise the real windowing path; offscreen is only a secondary
    # smoke, never the primary M1 evidence.
    assert "--platform xcb" in combined, (
        "the primary M1 runtime smoke must use the xcb platform plugin"
    )
    print("PASS test_ci_runs_the_xcb_startup_smoke_under_xvfb "
          f"({', '.join(path.name for path in hosting)})")


def test_documentation_pins_the_startup_smoke() -> None:
    docs = read(DOCS)
    assert FLAG in docs, "the startup smoke entry point must be documented"
    assert TIMEOUT_FLAG in docs
    assert "UI_STARTUP_RESULT" in docs
    print("PASS test_documentation_pins_the_startup_smoke")


def main() -> int:
    tests = (
        test_startup_smoke_does_not_return_before_qapplication,
        test_startup_smoke_reuses_the_product_startup_path,
        test_ready_condition_is_not_a_bare_timer,
        test_mainwindow_exposes_a_home_scene_seam_without_changing_startup,
        test_timeout_contract_is_dual_layered,
        test_validator_accepts_a_complete_successful_run,
        test_validator_fails_on_a_missing_result_marker,
        test_validator_fails_when_a_stage_is_missing,
        test_validator_fails_when_the_exit_code_disagrees_with_the_marker,
        test_validator_enforces_the_expected_failure_shape,
        test_ci_runs_the_xcb_startup_smoke_under_xvfb,
        test_documentation_pins_the_startup_smoke,
    )
    for test in tests:
        test()
    return 0


if __name__ == "__main__":
    sys.exit(main())
