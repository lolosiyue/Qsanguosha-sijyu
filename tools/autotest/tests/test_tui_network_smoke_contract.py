"""Fast evidence-contract tests for the production TUI network runner."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "autotest"))
RUNNER_COMMON = ROOT / "tools" / "autotest" / "runner_common.py"
LINUX_SERVER_CI = ROOT / ".github" / "workflows" / "linux-server-ci.yml"

import tui_network_smoke as smoke  # noqa: E402


def lifecycle(client_exit=0, shutdown="graceful", released=True):
    return {
        "client_exit": client_exit,
        "client_exit_name": "0",
        "server_shutdown": shutdown,
        "orphans": [],
        "port_released": released,
    }


def test_complete_game_requires_both_client_and_server_evidence():
    summary = {"lifecycle": lifecycle(), "problems": []}
    problems, evidence = smoke.evaluate_evidence(
        summary,
        "[TUI_EVENT] GAME_OVER\n",
        "",
        "[AUTOTEST] game start\n[AUTOTEST] game over rebel\n",
        False,
    )
    assert problems == []
    assert evidence["server_markers"]["game_over"] == "rebel"


def test_reconnect_run_requires_atomic_snapshot_activation_marker():
    summary = {"lifecycle": lifecycle(), "problems": []}
    problems, _ = smoke.evaluate_evidence(
        summary,
        "[TUI_EVENT] GAME_OVER\n",
        "",
        "[AUTOTEST] game start\n[AUTOTEST] game over lord\n",
        True,
    )
    assert "atomic reconnect snapshot was not committed" in problems


def test_connection_only_is_distinct_from_full_game_evidence():
    summary = {"lifecycle": lifecycle(), "problems": []}
    problems, evidence = smoke.evaluate_evidence(
        summary,
        "Protocol V2 session active\n",
        "",
        "",
        False,
        True,
    )
    assert problems == []
    assert evidence["connection_only"] is True
    assert evidence["server_markers"]["game_over"] is None


def test_errors_or_unclean_lifecycle_cannot_be_masked_by_game_over():
    state = lifecycle(client_exit=7, shutdown="already", released=False)
    state["orphans"] = ["server process group"]
    summary = {"lifecycle": state, "problems": []}
    problems, _ = smoke.evaluate_evidence(
        summary,
        "[TUI_EVENT] STATE_SYNC_COMMITTED\n[TUI_EVENT] GAME_OVER\nTUI_ERROR bad\n",
        "",
        "[AUTOTEST] game start\n[AUTOTEST] game over renegade\n",
        True,
    )
    assert any("did not exit cleanly" in problem for problem in problems)
    assert "TUI emitted TUI_ERROR" in problems
    assert any("shutdown was not graceful" in problem for problem in problems)
    assert any("orphan processes" in problem for problem in problems)
    assert "server port was not released" in problems


def test_generated_reconnect_script_orders_lifecycle_checkpoints():
    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "reconnect.txt"
        smoke.write_tui_script(str(path), True)
        lines = path.read_text(encoding="utf-8").splitlines()
    assert lines == [
        "wait active 30000",
        "/trust on",
        "/addrobot all",
        "wait game_started 60000",
        "/reconnect",
        "wait sync_complete 30000",
        "wait game_over 600000",
        "assert state game.game_over true",
        "assert state game.status game_over",
    ]


def test_connection_only_disables_unneeded_ai_runtime():
    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "server.ini"
        smoke.write_server_config(str(path), 15, 0, False)
        text = path.read_text(encoding="utf-8")
    assert "EnableAI=false" in text


def test_windows_shutdown_uses_a_console_control_event():
    text = RUNNER_COMMON.read_text(encoding="utf-8")
    assert "CREATE_NEW_PROCESS_GROUP" in text
    assert "GenerateConsoleCtrlEvent" in text
    assert "_send_windows_console_break(proc.pid)" in text
    assert "taskkill /T 不帶 /F" not in text


def test_evidence_records_binary_identity():
    import hashlib
    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "binary"
        path.write_bytes(b"production-tui")
        assert smoke.sha256_file(path) == hashlib.sha256(b"production-tui").hexdigest()


def test_remote_full_game_uses_an_isolated_core_runtime():
    text = LINUX_SERVER_CI.read_text(encoding="utf-8")
    assert "QSAN_TUI_RUNTIME" in text
    assert 'mkdir -p "$tui_runtime/extensions"' in text
    assert text.count('--workdir "$QSAN_TUI_RUNTIME"') == 2


def main():
    tests = (
        test_complete_game_requires_both_client_and_server_evidence,
        test_reconnect_run_requires_atomic_snapshot_activation_marker,
        test_connection_only_is_distinct_from_full_game_evidence,
        test_errors_or_unclean_lifecycle_cannot_be_masked_by_game_over,
        test_generated_reconnect_script_orders_lifecycle_checkpoints,
        test_connection_only_disables_unneeded_ai_runtime,
        test_windows_shutdown_uses_a_console_control_event,
        test_evidence_records_binary_identity,
        test_remote_full_game_uses_an_isolated_core_runtime,
    )
    for test in tests:
        test()
        print("PASS %s" % test.__name__)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
