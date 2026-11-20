#!/usr/bin/env python3
"""Run one deterministic game through the production Protocol V2 TUI.

This is intentionally a remote/long-running gate.  It starts the real dedicated
server and the real ``qsanguosha_tui`` on an ephemeral loopback port, fills the
room with server AI, and waits for an authoritative GAME_OVER.  ``--reconnect``
adds a disconnect/reconnect plus atomic state-sync checkpoint after GAME_START.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from runner_common import (  # noqa: E402
    MARK_GAME_OVER,
    MARK_GAME_START,
    close_proc,
    describe_exit,
    find_exe,
    free_tcp_port,
    process_group_alive,
    process_birth_token,
    isolated_settings_env,
    resolve_workdir,
    spawn,
    terminate_tree,
    wait_exit,
    wait_port,
    wait_port_released,
)


SCHEMA_VERSION = 1


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_text(path):
    if not path or not os.path.isfile(path):
        return ""
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def write_server_config(path, operation_timeout, ai_delay, enable_ai=True,
                        free_choose=False):
    lines = [
        "[General]",
        "RandomSeat=false",
        "Enable2ndGeneral=false",
        # FreeChoose begins the game straight away, so the server never sends the owner a
        # FreeAssign (CHOOSE_ROLE) request; EnableCheat alone would make the TUI owner hang
        # on the role-assignment prompt.
        "FreeAssign=false",
        "EnableCheat=%s" % ("true" if free_choose else "false"),
        "FreeChoose=%s" % ("true" if free_choose else "false"),
        "EnableHegemony=false",
        "EnableLuckCard=false",
        "EnableAI=%s" % ("true" if enable_ai else "false"),
        "AIHumanized=false",
        "OperationNoLimit=false",
        "CountDownSeconds=0",
        "OperationTimeout=%d" % operation_timeout,
        "OriginAIDelay=%d" % ai_delay,
        "AlterAIDelayAD=false",
        "AIDelayAD=0",
        "[serverconfig]",
        "upnp=false",
        "addtolistserver=false",
        "",
    ]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def write_tui_script(path, reconnect, connection_only=False, game_timeout_ms=600000,
                     general=""):
    lines = [
        "wait active 30000",
    ]
    if connection_only:
        lines.append("assert state connection.state active")
    elif general:
        # The client must choose the general before trust is enabled, or server AI may answer first.
        lines += [
            "/addrobot all",
            "wait interaction choose_general 120000",
            general,
            "/trust on",
        ]
    else:
        lines += [
            "/trust on",
            "/addrobot all",
        ]
    if reconnect and not connection_only:
        lines += [
            "wait game_started 60000",
            "/reconnect",
            "wait sync_complete 30000",
        ]
    if not connection_only:
        lines += [
            "wait game_over %d" % game_timeout_ms,
            "assert state game.game_over true",
            "assert state game.status game_over",
        ]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def server_markers(marker_text):
    game_over = None
    for line in marker_text.splitlines():
        match = MARK_GAME_OVER.search(line)
        if match:
            winner = (match.group(1) or "").strip()
            if game_over is None or (winner and game_over == "none"):
                game_over = winner or "none"
    return {
        "game_start": MARK_GAME_START in marker_text,
        "game_over": game_over,
    }


def evaluate_evidence(summary, semantic_text, process_text, marker_text, reconnect,
                      connection_only=False):
    problems = list(summary.get("problems", []))
    lifecycle = summary["lifecycle"]
    markers = server_markers(marker_text)
    combined = semantic_text + "\n" + process_text

    if lifecycle.get("client_exit") != 0:
        problems.append("TUI did not exit cleanly (%s)" % lifecycle.get("client_exit_name"))
    if "TUI_ERROR" in combined:
        problems.append("TUI emitted TUI_ERROR")
    if not connection_only:
        if "[TUI_EVENT] GAME_OVER" not in semantic_text:
            problems.append("TUI never committed visible GAME_OVER state")
        if not markers["game_start"]:
            problems.append("server never logged GAME_START")
        if markers["game_over"] is None:
            problems.append("server never logged GAME_OVER")
        if reconnect and "[TUI_EVENT] STATE_SYNC_COMMITTED" not in semantic_text:
            problems.append("atomic reconnect snapshot was not committed")
    if lifecycle.get("server_shutdown") != "graceful":
        problems.append("server shutdown was not graceful (%s)" % lifecycle.get("server_shutdown"))
    if lifecycle.get("orphans"):
        problems.append("orphan processes remained: %s" % ", ".join(lifecycle["orphans"]))
    if not lifecycle.get("port_released"):
        problems.append("server port was not released")

    return problems, {
        "server_markers": markers,
        "tui_game_over": "[TUI_EVENT] GAME_OVER" in semantic_text,
        "reconnect_observed": "[TUI_EVENT] STATE_SYNC_COMMITTED" in semantic_text,
        "script_completed": lifecycle.get("client_exit") == 0,
        "connection_only": connection_only,
    }


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe-root", default=".")
    parser.add_argument("--server")
    parser.add_argument("--tui")
    parser.add_argument("--workdir")
    parser.add_argument("--artifact-dir", default="ci-logs/tui-real-tcp")
    parser.add_argument("--mode", default="03_1v2")
    parser.add_argument("--seed", type=int, default=20260831)
    parser.add_argument("--general", default="",
                        help="自由選將: server 開 EnableCheat+FreeChoose, TUI 親自回答"
                             " choose_general 後才開 trust (空 = AI 自選)")
    parser.add_argument("--force-general", default="",
                        help="server gives the TUI player (room owner) this general in any mode")
    parser.add_argument("--reconnect", action="store_true")
    parser.add_argument("--connection-only", action="store_true",
                        help="short real-TCP signup/setup/ready smoke; never full-game evidence")
    parser.add_argument("--operation-timeout", type=int, default=15)
    parser.add_argument("--ai-delay", type=int, default=0)
    parser.add_argument("--server-startup-timeout", type=int, default=30)
    parser.add_argument("--process-timeout", type=int, default=900)
    parser.add_argument("--game-timeout-ms", type=int, default=600000,
                        help="TUI script wait game_over timeout in milliseconds")
    parser.add_argument("--deadline-seconds", type=float,
                        help="total startup/game budget; cleanup is always performed afterwards")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if args.deadline_seconds is not None and args.deadline_seconds <= 0:
        raise SystemExit("--deadline-seconds must be positive")
    deadline = time.monotonic() + args.deadline_seconds if args.deadline_seconds else None

    def remaining(limit):
        return max(0, min(limit, deadline - time.monotonic())) if deadline else limit

    if args.reconnect and args.connection_only:
        raise SystemExit("--reconnect cannot be combined with --connection-only")
    exe_root = os.path.abspath(args.exe_root)
    artifact_dir = os.path.abspath(args.artifact_dir)
    os.makedirs(artifact_dir, exist_ok=True)
    server_exe = os.path.abspath(args.server) if args.server else find_exe(exe_root, "qsanguosha_server")
    tui_exe = os.path.abspath(args.tui) if args.tui else find_exe(exe_root, "qsanguosha_tui")
    workdir = os.path.abspath(args.workdir) if args.workdir else resolve_workdir(exe_root)
    label = ("connection" if args.connection_only
             else ("reconnect" if args.reconnect else "complete-game"))
    prefix = os.path.join(artifact_dir, label)
    paths = {
        "server_log": prefix + "-server.log",
        "process_log": prefix + "-tui-process.log",
        "semantic_log": prefix + "-tui-semantic.log",
        "marker_log": prefix + "-markers.log",
        "config": prefix + "-server.ini",
        "script": prefix + "-script.txt",
        "summary": prefix + "-summary.json",
    }
    for path in paths.values():
        if os.path.isfile(path):
            os.remove(path)
    write_server_config(paths["config"], args.operation_timeout, args.ai_delay,
                        not args.connection_only, bool(args.general))
    write_tui_script(paths["script"], args.reconnect, args.connection_only,
                     args.game_timeout_ms, args.general)

    port = free_tcp_port()
    summary = {
        "schema_version": SCHEMA_VERSION,
        "ok": False,
        "run": {
            "mode": args.mode,
            "seed": args.seed,
            "general": args.general,
            "force_general": args.force_general,
            "reconnect": args.reconnect,
            "connection_only": args.connection_only,
            "port": port,
            "workdir": workdir,
            "server_exe": server_exe,
            "server_sha256": sha256_file(server_exe),
            "tui_exe": tui_exe,
            "tui_sha256": sha256_file(tui_exe),
            "extensions_commit": os.environ.get("QSAN_EXTENSIONS_COMMIT", "unknown"),
        },
        "lifecycle": {
            "server_started": False,
            "client_exit": None,
            "client_exit_name": None,
            "server_exit": None,
            "server_shutdown": "not-started",
            "orphans": [],
            "port_released": False,
        },
        "problems": [],
        "interrupted": False,
        "timed_out": False,
    }

    def save_summary():
        temporary = paths["summary"] + ".tmp"
        with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(summary, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        os.replace(temporary, paths["summary"])

    summary["ownership"] = {"host": socket.gethostname(), "processes": [
        {"role": "runner", "pid": os.getpid(), "birth": process_birth_token(os.getpid())}]}
    save_summary()

    server_cmd = [
        server_exe,
        "--port", str(port),
        "--websocket-port", "0",
        "--bind-address", "127.0.0.1",
        "--game-mode", args.mode,
        "--seed", str(args.seed),
        "--config", paths["config"],
        "--asset-root", workdir,
        "--autotest-log", paths["marker_log"],
        "--operation-timeout", str(args.operation_timeout),
        "--ai-delay", str(args.ai_delay),
        "--ai", "off" if args.connection_only else "on",
        "--log-level", "debug",
    ]
    if args.force_general:
        server_cmd += ["--force-general", args.force_general]
    tui_cmd = [
        tui_exe,
        "--host", "127.0.0.1",
        "--port", str(port),
        "--name", "TUI_%s" % label,
        "--avatar", "caocao",
        "--plain",
        "--script", paths["script"],
        "--log-file", paths["semantic_log"],
        "--asset-root", workdir,
    ]

    server = spawn(server_cmd, workdir, paths["server_log"],
                   env=isolated_settings_env(os.environ, os.path.join(artifact_dir, "server-settings")))
    summary["ownership"]["processes"].append(
        {"role": "server", "pid": server.pid, "birth": process_birth_token(server.pid)})
    save_summary()
    tui = None
    try:
        if not wait_port(port, remaining(args.server_startup_timeout), proc=server):
            summary["problems"].append("server did not listen on port %d" % port)
            summary["timed_out"] = server.poll() is None
        else:
            summary["lifecycle"]["server_started"] = True
            tui = spawn(tui_cmd, workdir, paths["process_log"],
                        env=isolated_settings_env(os.environ, os.path.join(artifact_dir, "tui-settings")))
            summary["ownership"]["processes"].append(
                {"role": "tui", "pid": tui.pid, "birth": process_birth_token(tui.pid)})
            save_summary()
            client_wait = remaining(args.process_timeout)
            client_code = wait_exit(tui, client_wait)
            if client_code is None:
                summary["timed_out"] = True
                summary["problems"].append("TUI exceeded %.1fs remaining timeout" % client_wait)
                client_code, _ = terminate_tree(tui)
            summary["lifecycle"]["client_exit"] = client_code
            summary["lifecycle"]["client_exit_name"] = describe_exit(client_code)
    except KeyboardInterrupt:
        summary["interrupted"] = True
        summary["problems"].append("run interrupted; retained partial evidence")
        # A second termination request must not interrupt owned-process cleanup.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if hasattr(signal, "SIGBREAK"):
            signal.signal(signal.SIGBREAK, signal.SIG_IGN)
    finally:
        if tui is not None:
            code, how = terminate_tree(tui)
            if summary["lifecycle"]["client_exit"] is None:
                summary["lifecycle"]["client_exit"] = code
                summary["lifecycle"]["client_exit_name"] = describe_exit(code)
            if how not in ("already", "graceful"):
                summary["lifecycle"]["orphans"].append("TUI cleanup: %s" % how)
            if process_group_alive(tui):
                summary["lifecycle"]["orphans"].append("TUI process group")
            close_proc(tui)
        server_code, how = terminate_tree(server)
        summary["lifecycle"]["server_exit"] = server_code
        summary["lifecycle"]["server_shutdown"] = how
        if process_group_alive(server):
            summary["lifecycle"]["orphans"].append("server process group")
        close_proc(server)
        summary["lifecycle"]["port_released"] = wait_port_released(port, 15)

    problems, evidence = evaluate_evidence(
        summary,
        read_text(paths["semantic_log"]),
        read_text(paths["process_log"]),
        read_text(paths["marker_log"]),
        args.reconnect,
        args.connection_only,
    )
    summary["evidence"] = evidence
    summary["problems"] = problems
    summary["ok"] = not problems
    summary["ownership"]["cleanup_finished"] = True
    save_summary()

    status = "PASS" if summary["ok"] else "FAIL"
    print("[AUTOTEST] TUI_REAL_TCP_RESULT status=%s connection_only=%s reconnect=%s game_over=%s summary=%s"
          % (status, str(args.connection_only).lower(), str(args.reconnect).lower(),
             evidence["server_markers"]["game_over"], paths["summary"]))
    for problem in problems:
        print("  - %s" % problem, file=sys.stderr)
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    # Batch cancellation must unwind through the same owned-process cleanup.
    def interrupt_run(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupt_run)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, interrupt_run)
    raise SystemExit(main())
