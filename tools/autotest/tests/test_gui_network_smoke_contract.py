"""Source-level contract for the Linux GUI M2 network UI smoke.

The C++ half (marker schema, exit codes, command→interaction mapping) lives in
``tests/network_ui_smoke``.  What that test cannot see is *how* the smoke is
wired into the product and the runner:

* the smoke must ride the product's real network path (``-connect:`` → ``Client``
  → ``MainWindow::enterRoom`` → ``RoomScene``), never a fake socket or a private
  copy of the join flow;
* the test-only behaviour must be behind explicit flags, so an ordinary player
  launch is untouched;
* the runner must use real TCP on a free port, a fixed recorded seed, bounded
  timeouts, and a cleanup path that leaves no orphan behind;
* the runner must never retry a game until it happens to pass.

Those are exactly the properties that rot silently, so they are pinned here.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "autotest"))

MAIN = ROOT / "src" / "main.cpp"
CONTROLLER_H = ROOT / "src" / "ui" / "testing" / "network-ui-smoke-controller.h"
CONTROLLER_CPP = ROOT / "src" / "ui" / "testing" / "network-ui-smoke-controller.cpp"
RESPONDER_CPP = ROOT / "src" / "ui" / "testing" / "network-ui-smoke-responder.cpp"
REPORT_H = ROOT / "src" / "ui" / "testing" / "network-ui-smoke-report.h"
CLIENT_H = ROOT / "src" / "client" / "client.h"
CLIENT_CPP = ROOT / "src" / "client" / "client.cpp"
MAINWINDOW_H = ROOT / "src" / "dialog" / "mainwindow.h"
MAINWINDOW_CPP = ROOT / "src" / "dialog" / "mainwindow.cpp"
ROOMSCENE_CPP = ROOT / "src" / "ui" / "roomscene.cpp"
RUNNER = ROOT / "tools" / "autotest" / "gui_network_smoke.py"
RUNNER_COMMON = ROOT / "tools" / "autotest" / "runner_common.py"
NETWORK_RUNNER = ROOT / "tools" / "autotest" / "network_runner.py"
WORKFLOWS = ROOT / ".github" / "workflows"
DOCS = ROOT / "docs" / "linux-development-environment.md"

FLAG = "--network-ui-smoke"


def read(path: Path) -> str:
    assert path.exists(), f"missing file: {path.relative_to(ROOT)}"
    return path.read_text(encoding="utf-8")


def strip_comments(text: str) -> str:
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def test_smoke_rides_the_product_network_path() -> None:
    text = read(MAIN)
    connect_index = text.index('arg.startsWith("-connect:")')
    begin_index = text.index("NetworkUiSmokeController::begin")
    assert begin_index > connect_index, (
        "the network smoke must attach after -connect: has created the real Client, "
        "so it observes the product's own connection rather than making its own"
    )

    controller = strip_comments(read(CONTROLLER_CPP))
    # The controller observes; it must not build a client, a socket or a scene.
    assert "new Client" not in controller, (
        "the network smoke must not create its own Client"
    )
    assert "TestClientSocket" not in controller, (
        "the M2 network smoke must use real TCP, not the in-process test socket"
    )
    assert "new RoomScene" not in controller, (
        "the network smoke must observe MainWindow's RoomScene, not build its own"
    )
    for signal in ("Client::socket_connected", "Client::server_connected",
                   "Client::game_started", "Client::game_over",
                   "MainWindow::roomSceneCreated"):
        assert signal in controller, f"the smoke must observe {signal}"
    print("PASS test_smoke_rides_the_product_network_path")


def test_test_only_behaviour_is_behind_explicit_flags() -> None:
    text = read(MAIN)
    # Every network-smoke branch in main() must be guarded by the explicit flag.
    for fragment in ("NetworkUiSmokeController::isRequested(arguments)",):
        assert fragment in text, f"main() must gate the smoke on {fragment}"

    report = read(REPORT_H)
    for flag in (FLAG, "--network-ui-smoke-result", "--network-ui-smoke-timeout-ms",
                 "--network-ui-smoke-stall-ms", "--network-ui-smoke-screenshot"):
        assert flag in report, f"{flag} must be part of the documented contract"

    responder = read(RESPONDER_CPP)
    assert "isActive()" in responder, (
        "the responder must expose an explicit active check for its RoomScene hook"
    )

    # The RoomScene hook must be inert unless the responder is actually running.
    scene = read(ROOMSCENE_CPP)
    assert "NetworkUiSmokeResponder::isActive()" in scene, (
        "RoomScene's general-selection hook must be gated on the responder being "
        "active, never on an implicit heuristic"
    )
    hook_index = scene.index("NetworkUiSmokeResponder::isActive()")
    autopick_index = scene.index("if (!Config.AutoPickGeneral.isEmpty())")
    assert hook_index < autopick_index, (
        "the list-driven smoke pick must run before the --test-general path, which "
        "can silently fall back to the server's default general"
    )
    print("PASS test_test_only_behaviour_is_behind_explicit_flags")


def test_client_exposes_transport_observation_without_changing_transport() -> None:
    header = read(CLIENT_H)
    for signal in ("socket_connected", "socket_disconnected", "server_request",
                   "server_reply"):
        assert signal in header, f"Client is missing the {signal} observation seam"

    source = read(CLIENT_CPP)
    assert "emit server_request(static_cast<int>(command));" in source, (
        "every server request must be observable at the single dispatch funnel"
    )
    assert "emit server_reply(static_cast<int>(command));" in source, (
        "every client reply must be observable at the single reply funnel"
    )
    # M2 explicitly must not touch the wire format: the observation seam is an
    # extra emit, never a second way out to the socket.  Protocol V2 replies
    # leave through the live session, correlated with the request being served.
    code = strip_comments(source)
    assert "m_liveSession->sendReply(command, arg, m_dispatchingRequestId, &error)" in code, (
        "the reply packet must still leave through the live session's single "
        "correlated reply path"
    )
    assert "Refusing uncorrelated Protocol V2 reply" in code, (
        "an uncorrelated reply must be refused rather than sent with a guessed "
        "request id"
    )
    print("PASS test_client_exposes_transport_observation_without_changing_transport")


def test_mainwindow_exposes_a_roomscene_seam_without_changing_entry() -> None:
    header = read(MAINWINDOW_H)
    assert "roomSceneCreated" in header, "MainWindow is missing the RoomScene seam"

    source = read(MAINWINDOW_CPP)
    assert source.count("RoomScene *room_scene = new RoomScene(this);") == 1, (
        "enterRoom() must still build exactly one RoomScene"
    )
    assert "emit roomSceneCreated(room_scene);" in source
    print("PASS test_mainwindow_exposes_a_roomscene_seam_without_changing_entry")


def test_responder_answers_through_the_real_ui() -> None:
    text = strip_comments(read(RESPONDER_CPP))
    # Replies must come from RoomScene's own buttons/slots, not from hand-built
    # protocol calls that would bypass the UI the smoke is supposed to prove.
    assert "button->click()" in text, "the responder must click real UI buttons"
    assert "clickItem()" in text, "the responder must click real CardItems"
    assert "setSelected(true)" in text, "the responder must select real targets"
    assert "doTimeout()" in text, (
        "unhandled statuses must fall back to RoomScene's own safe-default reply"
    )
    assert "replyToServer" not in text, (
        "the responder must never build replies itself; it drives the UI"
    )
    # The trustee fallback is allowed but must always be recorded.
    assert "ActionTrusteeFallback" in text, (
        "engaging the trustee must be recorded in the report, never silent"
    )
    print("PASS test_responder_answers_through_the_real_ui")


def test_controller_reports_every_exit_path() -> None:
    text = read(CONTROLLER_CPP)
    assert "reportUnfinishedAtExit" in text, (
        "every exit path must leave a NETWORK_UI_RESULT marker behind"
    )
    assert "std::atexit" in text
    assert "NetworkUiSmokeReport::Timeout" in text
    assert "NetworkUiSmokeReport::Disconnected" in text
    assert "QTimer::singleShot(0, qApp, &QCoreApplication::quit)" in text, (
        "the client must exit through a normal application quit, so that a clean "
        "client exit is itself part of what the smoke proves"
    )
    assert "onDisconnectVerdict" in text, (
        "Client::gameOver() disconnects before it emits game_over(); the smoke must "
        "not read that as a mid-game disconnect"
    )
    print("PASS test_controller_reports_every_exit_path")


def test_runner_common_is_cross_platform() -> None:
    import runner_common as rc

    assert rc.executable_name("qsanguosha_server").endswith(
        ".exe" if rc.IS_WINDOWS else "qsanguosha_server")
    # The search order must not let a stale debug tree shadow the CI configuration.
    order = list(rc._EXE_SUBDIRS)
    assert order.index("relwithdebinfo") < order.index("debug"), (
        "relwithdebinfo (the Linux CI baseline) must win over a stale debug build"
    )

    port = rc.free_tcp_port()
    assert 1024 < port < 65536, "free_tcp_port must return a usable ephemeral port"
    assert not rc.port_open(port), "the borrowed port must be released again"

    for name in ("terminate_tree", "request_shutdown", "process_group_alive",
                 "wait_port", "wait_port_released"):
        assert hasattr(rc, name), f"runner_common is missing {name}"

    # Exit-code interpretation must be normalized per platform.
    assert rc.is_crash_code(1) is False, "a plain exit=1 is not a crash"
    if not rc.IS_WINDOWS:
        assert rc.is_crash_code(-11) is True, "SIGSEGV must be reported as a crash"
        assert "SIGSEGV" in rc.describe_exit(-11)
    else:
        assert rc.is_crash_code(0xC0000005 - (1 << 32)) is True

    text = read(RUNNER_COMMON)
    assert "start_new_session" in text and "CREATE_NEW_PROCESS_GROUP" in text, (
        "children must be spawned into their own group so the whole tree can be "
        "cleaned up, including xvfb-run's Xvfb"
    )
    print("PASS test_runner_common_is_cross_platform")


def test_legacy_network_runner_is_no_longer_windows_only() -> None:
    text = read(NETWORK_RUNNER)
    assert '"qsanguosha_server.exe"' not in text and '"QSanguosha.exe"' not in text, (
        "network_runner.py must resolve executables through runner_common so it "
        "runs on Linux too"
    )
    assert "taskkill" not in text, (
        "process cleanup must go through runner_common, not a hardcoded taskkill"
    )
    print("PASS test_legacy_network_runner_is_no_longer_windows_only")


def _drive_network_runner(start_outcomes, runs):
    """Run network_runner.run_mode against scripted fakes.

    start_outcomes is consumed once per client launch: "start" means the game
    started (and then finishes normally), "died" means the server exited
    before the start marker, and "client_lost" means the game started but the
    client exited before game over (the server still finishes the game), and
    "timeout" means the game started but game over never arrived in time.
    Returns (results, client launch count, restart_server call count)."""
    import tempfile
    import types

    import network_runner as nr

    outcomes = list(start_outcomes)
    launches = []
    pending_over = []
    restarts = []

    class Proc:
        def poll(self):
            return None

    def fake_spawn(cmd, cwd, log_path, console=False, env=None):
        if "--auto-robots" in cmd:
            launches.append(cmd)
        return Proc()

    def fake_wait_for_marker(log_path, predicate, timeout, start_offset=0, server_proc=None,
                             client_proc=None):
        if pending_over:
            if pending_over[-1] == "client_lost" and client_proc is not None:
                pending_over[-1] = "start"
                return "CLIENT_DIED", start_offset
            if pending_over.pop() == "timeout":
                return None, start_offset
            return "[AUTOTEST] game over lord", start_offset
        outcome = outcomes.pop(0) if outcomes else "start"
        if outcome == "died":
            return "SERVER_DIED", start_offset
        pending_over.append(outcome)
        return "[AUTOTEST] game start", start_offset

    def fake_restart_server(*args, **kwargs):
        restarts.append(args[-1] if args else kwargs.get("reason"))
        return Proc()

    patches = {
        "find_exe": lambda root, name: name,
        "spawn": fake_spawn,
        "wait_port": lambda port, timeout, proc=None: True,
        "wait_for_marker": fake_wait_for_marker,
        "wait_exit": lambda proc, timeout: 1,
        "terminate_tree": lambda proc: None,
        "close_proc": lambda proc: None,
        "tail_lines": lambda path, n: [],
        "log_has_smart_ai_failure": lambda path: False,
        "_backup_runtime_files": lambda workdir, run_dir, run_id: None,
        "restart_server": fake_restart_server,
        "time": types.SimpleNamespace(sleep=lambda s: None, time=lambda: 0),
    }
    saved = {name: getattr(nr, name) for name in patches}
    try:
        for name, value in patches.items():
            setattr(nr, name, value)
        with tempfile.TemporaryDirectory() as tmp:
            args = types.SimpleNamespace(log_dir=tmp, port=1, console=False, general2="")
            results = nr.run_mode(args, tmp, tmp, "08p", runs, "zhenji")
    finally:
        for name, value in saved.items():
            setattr(nr, name, value)
    return results, len(launches), len(restarts)


def test_network_runner_retries_the_same_game_after_a_pre_start_server_crash() -> None:
    results, launches, _ = _drive_network_runner(["died", "start", "start"], runs=2)
    passed = [r["run"] for r in results if r["ok"]]
    assert passed == [1, 2], (
        "a server crash before game start must retry that game, not consume it: "
        f"got results {results}"
    )
    assert launches == 3, f"expected 2 games + 1 retry, got {launches} client launches"
    crashed = [r for r in results if not r["ok"]]
    assert len(crashed) == 1 and crashed[0]["run"] == 1, (
        f"the pre-start crash must still be reported as a failure: {results}"
    )
    print("PASS test_network_runner_retries_the_same_game_after_a_pre_start_server_crash")


def test_network_runner_bounds_pre_start_crash_retries() -> None:
    results, launches, _ = _drive_network_runner(["died"] * 50, runs=1)
    assert launches <= 1 + nr_max_start_retries(), (
        f"pre-start crash retries must be bounded, got {launches} client launches"
    )
    assert results and not any(r["ok"] for r in results), results
    print("PASS test_network_runner_bounds_pre_start_crash_retries")


def test_wait_for_marker_reads_markers_written_before_the_server_died() -> None:
    import tempfile

    import network_runner as nr

    class Dead:
        def poll(self):
            return -11

    with tempfile.TemporaryDirectory() as tmp:
        marker = Path(tmp) / "autotest.log"
        marker.write_text("[AUTOTEST] addRobot\n[AUTOTEST] game start\n", encoding="utf-8")
        line, _ = nr.wait_for_marker(str(marker), lambda l: nr.MARK_GAME_START in l,
                                     5, 0, server_proc=Dead())
        assert line == "[AUTOTEST] game start", (
            "a server that wrote the start marker and then crashed must be seen as "
            f"started (so the crash is classified mid-game), got {line!r}"
        )
        marker.write_text("[AUTOTEST] addRobot\n", encoding="utf-8")
        line, _ = nr.wait_for_marker(str(marker), lambda l: nr.MARK_GAME_START in l,
                                     5, 0, server_proc=Dead())
        assert line == "SERVER_DIED", f"no start marker + dead server must be SERVER_DIED, got {line!r}"
    print("PASS test_wait_for_marker_reads_markers_written_before_the_server_died")


def test_network_runner_reports_a_client_lost_mid_game() -> None:
    results, launches, restarts = _drive_network_runner(["client_lost", "start"], runs=2)
    assert [r["run"] for r in results] == [1, 2], results
    lost, normal = results
    assert not lost["ok"] and "client" in lost["note"] and "winner=lord" in lost["note"], (
        f"a client that exits before game over must fail that game, got {lost}"
    )
    assert normal["ok"], f"the next game must still run and pass, got {normal}"
    assert launches == 2 and restarts == 0, (
        "a client lost without a crash signal leaves the server healthy: no retry, "
        f"no restart (launches={launches}, restarts={restarts})"
    )
    print("PASS test_network_runner_reports_a_client_lost_mid_game")


def test_network_runner_restarts_the_server_after_a_game_timeout() -> None:
    results, launches, restarts = _drive_network_runner(["timeout", "start"], runs=2)
    assert [r["run"] for r in results] == [1, 2], results
    timed_out, normal = results
    assert not timed_out["ok"] and "timeout" in timed_out["note"], (
        f"a game that never reaches game over must fail, got {timed_out}"
    )
    assert normal["ok"], f"the next game must still run and pass, got {normal}"
    assert launches == 2 and restarts == 1, (
        "a timed-out room keeps running on the server and holds the killed client's "
        "screen name, so the server must be restarted before the next game "
        f"(launches={launches}, restarts={restarts})"
    )
    print("PASS test_network_runner_restarts_the_server_after_a_game_timeout")


class _FakeClock:
    """Deterministic stand-in for network_runner.time: sleep() advances now."""

    def __init__(self, on_sleep=None):
        self.now = 1000.0
        self.sleeps = 0
        self.on_sleep = on_sleep

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.sleeps += 1
        self.now += seconds
        if self.on_sleep:
            self.on_sleep(self.sleeps)


def _wait_with_clock(clock, marker, **kwargs):
    import network_runner as nr

    saved = nr.time
    nr.time = clock
    try:
        return nr.wait_for_marker(str(marker), lambda l: nr.MARK_GAME_OVER.search(l),
                                  3600, 0, **kwargs)
    finally:
        nr.time = saved


def test_wait_for_marker_reports_a_client_that_exits_before_the_marker() -> None:
    import tempfile

    import network_runner as nr

    class Alive:
        def poll(self):
            return None

    class Exited:
        def poll(self):
            return 1

    with tempfile.TemporaryDirectory() as tmp:
        marker = Path(tmp) / "autotest.log"
        marker.write_text("[AUTOTEST] game start\n", encoding="utf-8")

        clock = _FakeClock()
        line, _ = _wait_with_clock(clock, marker, server_proc=Alive(), client_proc=Exited())
        assert line == "CLIENT_DIED", f"an exited client with no game over must be reported, got {line!r}"
        assert clock.now - 1000.0 >= nr.CLIENT_EXIT_GRACE, (
            "the client verdict must wait out the grace period for a game-over marker"
        )

        # A client that quits as the game ends: the marker lands inside the grace.
        def write_over(sleeps):
            if sleeps == 2:
                with open(marker, "a", encoding="utf-8") as f:
                    f.write("[AUTOTEST] game over rebel\n")

        line, _ = _wait_with_clock(_FakeClock(write_over), marker,
                                   server_proc=Alive(), client_proc=Exited())
        assert line == "[AUTOTEST] game over rebel", (
            f"a game-over marker inside the grace period is a normal end, got {line!r}"
        )

        # A dead server still wins over a lost client.
        class Dead:
            def poll(self):
                return -11

        marker.write_text("[AUTOTEST] game start\n", encoding="utf-8")
        line, _ = _wait_with_clock(_FakeClock(), marker, server_proc=Dead(), client_proc=Exited())
        assert line == "SERVER_DIED", f"a dead server must be reported first, got {line!r}"
    print("PASS test_wait_for_marker_reports_a_client_that_exits_before_the_marker")


def nr_max_start_retries() -> int:
    import network_runner as nr

    return nr.MAX_START_RETRIES


def test_runner_uses_real_tcp_with_a_fixed_recorded_seed() -> None:
    text = read(RUNNER)
    assert "free_tcp_port()" in text, (
        "the runner must take a free port so parallel CI jobs cannot collide"
    )
    assert '"--seed", str(args.seed)' in text, "the seed must reach the server"
    assert '"--port", str(port)' in text, "the chosen port must reach the server"
    assert 'required=True' in text.split('"--seed"', 1)[1][:200], (
        "the seed must be mandatory; an accidental default is not reproducible"
    )
    for field in ("server_sha256", "client_sha256", "extensions_commit", "seed",
                  "mode", "port"):
        assert f'"{field}"' in text, f"the summary must record {field}"

    assert "TestClientSocket" not in text
    assert "-connect:127.0.0.1:%d" in text, "the client must join over real TCP"

    # Bounded, layered timeouts - and no retry loop.
    assert "--process-timeout" in text and "--client-timeout-ms" in text, (
        "the runner needs both a process-level and an app-level bound"
    )
    # No retry-until-pass: exactly one server and one client are started, and
    # there is no restart/attempt loop around them.
    assert text.count("spawn(client_command") == 1, (
        "the M2 smoke must start the GUI client exactly once"
    )
    assert text.count("spawn(server_command") == 1, (
        "the M2 smoke must start the server exactly once"
    )
    assert "restart_server" not in text, (
        "restarting the server mid-run is soak behaviour, not contract behaviour"
    )
    for construct in ("for attempt", "while attempt", "max_retries", "retries ="):
        assert construct not in text, (
            f"the M2 smoke must never retry a game until it happens to pass "
            f"({construct!r} found)"
        )

    # Cleanup and orphan/port verification on both paths.
    assert "terminate_tree" in text and "process_group_alive" in text
    assert "wait_port_released" in text
    assert "finally:" in text, "cleanup must run on the failure path too"
    print("PASS test_runner_uses_real_tcp_with_a_fixed_recorded_seed")


STAGES = ("connected", "signed_up", "room_scene", "dashboard", "general_selected",
          "game_started", "game_over", "shutdown")


def stage_line(stage: str, ok: bool = True) -> str:
    return "NETWORK_UI_STAGE " + json.dumps(
        {"schema_version": 1, "stage": stage, "ok": ok}, separators=(",", ":"))


def result_line(ok: bool, stage: str, exit_code: int, reason: str,
                effects: dict | None = None, counters: dict | None = None) -> str:
    payload = {"schema_version": 1, "ok": ok, "stage": stage,
               "exit_code": exit_code, "reason": reason}
    if not ok:
        payload["error"] = "synthetic failure"
    if effects is not None:
        payload["effects"] = effects
    if counters is not None:
        payload["effects_counters"] = counters
    return "NETWORK_UI_RESULT " + json.dumps(payload, separators=(",", ":"))


def evaluate_log(log_text: str, *, client_code: int = 0, game_start: bool = True,
                 game_over: str | None = "lord", responder: dict | None = None,
                 required=("choose_general",), allow_trustee: bool = False,
                 shutdown: str = "graceful", server_exit: int = 0, orphans=(),
                 port_released: bool = True, known_base_defect=(),
                 effects_profile: str | None = None):
    """Drive the runner's own evaluator over a synthetic run."""
    import gui_network_smoke as smoke

    class Args:
        pass

    args = Args()
    args.require_interactions = list(required)
    args.allow_trustee_fallback = allow_trustee
    args.known_base_defect = list(known_base_defect)
    args.effects_profile = effects_profile

    stages, results = smoke.parse_markers(log_text)
    summary = {
        "run": {"port": 12345},
        "responder": responder if responder is not None else {
            "interactions": {"choose_general": 1}, "trustee_engaged": False},
        "lifecycle": {"server_shutdown": shutdown, "server_exit": server_exit,
                      "orphans": list(orphans), "port_released": port_released},
    }
    return smoke.evaluate(args, summary, stages, results,
                          client_code,
                          {"game_start": game_start, "game_over": game_over})


def complete_log(effects: dict | None = None, counters: dict | None = None) -> str:
    return "\n".join([stage_line(stage) for stage in STAGES]
                     + [result_line(True, "shutdown", 0, "ok", effects, counters)])


NO_EFFECT_OBJECTS = {"spine_items": 0, "movie_objects": 0,
                     "qml_overlays": 0, "video_objects": 0}


def test_evaluator_accepts_a_complete_successful_run() -> None:
    sys.path.insert(0, str(ROOT / "tools" / "autotest"))
    problems = evaluate_log(complete_log())
    assert problems == [], problems
    print("PASS test_evaluator_accepts_a_complete_successful_run")


def test_evaluator_rejects_a_missing_result_marker() -> None:
    log = "\n".join(stage_line(stage) for stage in STAGES)
    problems = evaluate_log(log)
    assert any("NETWORK_UI_RESULT" in problem for problem in problems), problems
    print("PASS test_evaluator_rejects_a_missing_result_marker")


def test_evaluator_rejects_a_missing_stage() -> None:
    log = "\n".join([stage_line(stage) for stage in STAGES if stage != "dashboard"]
                    + [result_line(True, "shutdown", 0, "ok")])
    problems = evaluate_log(log)
    assert any("'dashboard' was never reported" in problem for problem in problems), problems
    print("PASS test_evaluator_rejects_a_missing_stage")


def test_evaluator_rejects_a_crashed_client() -> None:
    problems = evaluate_log(complete_log(), client_code=-11)
    assert any("crashed" in problem for problem in problems), problems
    print("PASS test_evaluator_rejects_a_crashed_client")


def test_evaluator_rejects_a_missing_server_game_over() -> None:
    problems = evaluate_log(complete_log(), game_over=None)
    assert any("game over" in problem for problem in problems), problems
    problems = evaluate_log(complete_log(), game_start=False)
    assert any("game start" in problem for problem in problems), problems
    print("PASS test_evaluator_rejects_a_missing_server_game_over")


def test_evaluator_rejects_a_silent_trustee_fallback() -> None:
    responder = {"interactions": {"choose_general": 1}, "trustee_engaged": True,
                 "trustee_reason": "synthetic"}
    problems = evaluate_log(complete_log(), responder=responder)
    assert any("trustee" in problem for problem in problems), problems
    # ...unless it was explicitly allowed for that run.
    assert evaluate_log(complete_log(), responder=responder, allow_trustee=True) == []
    print("PASS test_evaluator_rejects_a_silent_trustee_fallback")


def test_evaluator_rejects_an_uncovered_required_interaction() -> None:
    problems = evaluate_log(complete_log(), required=("ask_for_card",))
    assert any("ask_for_card" in problem for problem in problems), problems
    print("PASS test_evaluator_rejects_an_uncovered_required_interaction")


def test_evaluator_rejects_a_dirty_shutdown() -> None:
    assert any("shut down cleanly" in problem
               for problem in evaluate_log(complete_log(), shutdown="killed"))
    # A persistent server that was already gone before we asked died on its own.
    assert any("shut down cleanly" in problem
               for problem in evaluate_log(complete_log(), shutdown="already"))
    assert any("the server crashed" in problem
               for problem in evaluate_log(complete_log(), shutdown="already",
                                           server_exit=-11))
    assert any("survived the smoke" in problem
               for problem in evaluate_log(complete_log(), orphans=("client",)))
    assert any("still accepting connections" in problem
               for problem in evaluate_log(complete_log(), port_released=False))
    print("PASS test_evaluator_rejects_a_dirty_shutdown")


def test_known_base_defect_downgrade_is_narrow_and_never_silent() -> None:
    """--known-base-defect must be an audit trail, not a mute button."""
    import gui_network_smoke as smoke

    # Without the flag, a teardown crash still fails the run.
    assert any("the server crashed" in problem
               for problem in evaluate_log(complete_log(), shutdown="already",
                                           server_exit=-11))

    # With the flag, it is downgraded - but only after a real game over and a
    # clean client exit, and it is always recorded.
    summary = {
        "run": {"port": 1}, "responder": {"interactions": {"choose_general": 1},
                                          "trustee_engaged": False},
        "lifecycle": {"server_shutdown": "already", "server_exit": -11,
                      "orphans": [], "port_released": True},
    }

    class Args:
        require_interactions = ["choose_general"]
        allow_trustee_fallback = False
        known_base_defect = ["server-teardown-crash"]
        effects_profile = None

    stages, results = smoke.parse_markers(complete_log())
    problems = smoke.evaluate(Args(), summary, stages, results, 0,
                              {"game_start": True, "game_over": "rebel"})
    assert problems == [], problems
    assert summary["known_base_defects"], (
        "a downgraded defect must still be recorded in the summary"
    )

    # A server that died before the game finished is never downgraded, no matter
    # what the flag says - that would hide a genuine mid-game failure.
    summary2 = {
        "run": {"port": 1}, "responder": {"interactions": {"choose_general": 1},
                                          "trustee_engaged": False},
        "lifecycle": {"server_shutdown": "already", "server_exit": -11,
                      "orphans": [], "port_released": True},
    }
    problems = smoke.evaluate(Args(), summary2, stages, results, 0,
                              {"game_start": True, "game_over": None})
    assert any("the server crashed" in problem for problem in problems), problems

    # Nor is it downgraded when the client itself crashed.
    summary3 = dict(summary2)
    summary3["known_base_defects"] = []
    summary3["lifecycle"] = dict(summary2["lifecycle"])
    problems = smoke.evaluate(Args(), summary3, stages, results, -11,
                              {"game_start": True, "game_over": "rebel"})
    assert any("the server crashed" in problem for problem in problems), problems

    assert smoke.KNOWN_BASE_DEFECTS == {"server-teardown-crash"}, (
        "adding a known-base-defect id must be a deliberate, reviewed change"
    )
    print("PASS test_known_base_defect_downgrade_is_narrow_and_never_silent")


def test_evaluator_rejects_an_exit_code_that_disagrees_with_the_marker() -> None:
    log = "\n".join([stage_line(stage) for stage in STAGES]
                    + [result_line(True, "shutdown", 0, "ok")])
    problems = evaluate_log(log, client_code=3)
    assert any("disagrees" in problem for problem in problems), problems
    print("PASS test_evaluator_rejects_an_exit_code_that_disagrees_with_the_marker")


def test_gui_runtime_smoke_is_a_local_gate_not_a_ci_gate() -> None:
    """The GUI network smoke is deliberately kept out of CI.

    A GitHub runner has none of the (large, unshipped) art or audio assets, and
    without them the client and server crash inside the rendering path - a
    pre-existing in-game phenomenon that also reproduces on Windows and has
    nothing to do with the change under test.  Gating on it only produces red
    builds that nobody can act on, so the runner is a *local* tool: run it on a
    machine that has the full asset tree.  See AGENTS.md.

    This test still pins the decision: the runner must keep existing and stay
    documented, and it must not quietly creep back into a workflow.
    """
    assert RUNNER.exists(), (
        "the GUI network smoke runner must keep existing; it is the local gate"
    )
    # Match the invocation, not a prose mention: the workflow is allowed - and
    # expected - to say in a comment why the smoke is not run there.
    hosting = [path.name for path in sorted(WORKFLOWS.glob("*.yml"))
               if "python3 tools/autotest/gui_network_smoke.py" in read(path)
               or "python tools\\autotest\\gui_network_smoke.py" in read(path)]
    assert not hosting, (
        "the GUI network smoke must not be a CI gate; it reproduces a "
        f"pre-existing asset-dependent crash on runners (found in: {hosting}). "
        "If this is being reinstated on purpose, update AGENTS.md and this test "
        "together."
    )
    print("PASS test_gui_runtime_smoke_is_a_local_gate_not_a_ci_gate")


def test_documentation_pins_the_network_smoke() -> None:
    docs = read(DOCS)
    assert FLAG in docs, "the network smoke entry point must be documented"
    assert "NETWORK_UI_RESULT" in docs
    assert "gui_network_smoke.py" in docs
    assert "02p" in docs and "05p" in docs, "the mode IDs must be documented"
    print("PASS test_documentation_pins_the_network_smoke")


def test_effects_profile_matrix_must_actually_run_the_requested_profile() -> None:
    """M2B-B: `--effects-profile none` that silently ran `full` is the whole risk.

    A mistyped or ignored override turns a three-profile matrix into the same
    profile three times, and every "NONE completes a full game" claim becomes a
    claim about FULL.  So the runner checks both the resolved profile *and* that
    the CLI is what resolved it.
    """
    sys.path.insert(0, str(ROOT / "tools" / "autotest"))

    honoured = complete_log({"profile": "none", "source": "cli"}, NO_EFFECT_OBJECTS)
    assert evaluate_log(honoured, effects_profile="none") == [], \
        "a run that honoured --effects-profile none must pass"

    ignored = complete_log({"profile": "full", "source": "settings"}, NO_EFFECT_OBJECTS)
    problems = evaluate_log(ignored, effects_profile="none")
    assert any("resolved" in problem for problem in problems), problems

    fell_back = complete_log({"profile": "none", "source": "settings"}, NO_EFFECT_OBJECTS)
    problems = evaluate_log(fell_back, effects_profile="none")
    assert any("resolution source" in problem for problem in problems), problems

    # Without the flag the runner must not invent an expectation.
    assert evaluate_log(complete_log()) == []
    print("PASS test_effects_profile_matrix_must_actually_run_the_requested_profile")


def test_none_profile_must_not_construct_effect_objects_during_a_game() -> None:
    """The executable definition of NONE, asserted over a whole finished game."""
    sys.path.insert(0, str(ROOT / "tools" / "autotest"))
    for key in NO_EFFECT_OBJECTS:
        counters = dict(NO_EFFECT_OBJECTS, **{key: 3})
        log = complete_log({"profile": "none", "source": "cli"}, counters)
        problems = evaluate_log(log, effects_profile="none")
        assert any(key in problem for problem in problems), (key, problems)
    print("PASS test_none_profile_must_not_construct_effect_objects_during_a_game")


def main() -> int:
    tests = (
        test_smoke_rides_the_product_network_path,
        test_test_only_behaviour_is_behind_explicit_flags,
        test_client_exposes_transport_observation_without_changing_transport,
        test_mainwindow_exposes_a_roomscene_seam_without_changing_entry,
        test_responder_answers_through_the_real_ui,
        test_controller_reports_every_exit_path,
        test_runner_common_is_cross_platform,
        test_legacy_network_runner_is_no_longer_windows_only,
        test_network_runner_retries_the_same_game_after_a_pre_start_server_crash,
        test_network_runner_bounds_pre_start_crash_retries,
        test_wait_for_marker_reads_markers_written_before_the_server_died,
        test_network_runner_reports_a_client_lost_mid_game,
        test_network_runner_restarts_the_server_after_a_game_timeout,
        test_wait_for_marker_reports_a_client_that_exits_before_the_marker,
        test_runner_uses_real_tcp_with_a_fixed_recorded_seed,
        test_evaluator_accepts_a_complete_successful_run,
        test_evaluator_rejects_a_missing_result_marker,
        test_evaluator_rejects_a_missing_stage,
        test_evaluator_rejects_a_crashed_client,
        test_evaluator_rejects_a_missing_server_game_over,
        test_evaluator_rejects_a_silent_trustee_fallback,
        test_evaluator_rejects_an_uncovered_required_interaction,
        test_evaluator_rejects_a_dirty_shutdown,
        test_known_base_defect_downgrade_is_narrow_and_never_silent,
        test_evaluator_rejects_an_exit_code_that_disagrees_with_the_marker,
        test_effects_profile_matrix_must_actually_run_the_requested_profile,
        test_none_profile_must_not_construct_effect_objects_during_a_game,
        test_gui_runtime_smoke_is_a_local_gate_not_a_ci_gate,
        test_documentation_pins_the_network_smoke,
    )
    for test in tests:
        test()
    return 0


if __name__ == "__main__":
    sys.exit(main())
