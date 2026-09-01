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
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from runner_common import (  # noqa: E402
    MARK_GAME_OVER,
    MARK_GAME_START,
    close_proc,
    describe_exit,
    find_exe,
    free_tcp_port,
    process_group_alive,
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


def write_server_config(path, operation_timeout, ai_delay, enable_ai=True):
    lines = [
        "[General]",
        "RandomSeat=false",
        "Enable2ndGeneral=false",
        "EnableCheat=false",
        "FreeChoose=false",
        "EnableBasara=false",
        "EnableHegemony=false",
        "EnableSame=false",
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


def write_tui_script(path, reconnect, connection_only=False):
    lines = [
        "wait active 30000",
    ]
    if connection_only:
        lines.append("assert state connection.state active")
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
            "wait game_over 600000",
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
    parser.add_argument("--reconnect", action="store_true")
    parser.add_argument("--connection-only", action="store_true",
                        help="short real-TCP signup/setup/ready smoke; never full-game evidence")
    parser.add_argument("--operation-timeout", type=int, default=15)
    parser.add_argument("--ai-delay", type=int, default=0)
    parser.add_argument("--server-startup-timeout", type=int, default=30)
    parser.add_argument("--process-timeout", type=int, default=900)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
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
                        not args.connection_only)
    write_tui_script(paths["script"], args.reconnect, args.connection_only)

    port = free_tcp_port()
    summary = {
        "schema_version": SCHEMA_VERSION,
        "ok": False,
        "run": {
            "mode": args.mode,
            "seed": args.seed,
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
    }

    server_cmd = [
        server_exe,
        "--port", str(port),
        "--websocket-port", "0",
        "--bind-address", "127.0.0.1",
        "--game-mode", args.mode,
        "--seed", str(args.seed),
        "--config", paths["config"],
        "--autotest-log", paths["marker_log"],
        "--operation-timeout", str(args.operation_timeout),
        "--ai-delay", str(args.ai_delay),
        "--ai", "off" if args.connection_only else "on",
        "--log-level", "debug",
    ]
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

    server = spawn(server_cmd, workdir, paths["server_log"])
    tui = None
    try:
        if not wait_port(port, args.server_startup_timeout, proc=server):
            summary["problems"].append("server did not listen on port %d" % port)
        else:
            summary["lifecycle"]["server_started"] = True
            tui = spawn(tui_cmd, workdir, paths["process_log"])
            client_code = wait_exit(tui, args.process_timeout)
            if client_code is None:
                summary["problems"].append("TUI exceeded %ds timeout" % args.process_timeout)
                client_code, _ = terminate_tree(tui)
            summary["lifecycle"]["client_exit"] = client_code
            summary["lifecycle"]["client_exit_name"] = describe_exit(client_code)
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
    with open(paths["summary"], "w", encoding="utf-8", newline="\n") as handle:
        json.dump(summary, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    status = "PASS" if summary["ok"] else "FAIL"
    print("[AUTOTEST] TUI_REAL_TCP_RESULT status=%s connection_only=%s reconnect=%s game_over=%s summary=%s"
          % (status, str(args.connection_only).lower(), str(args.reconnect).lower(),
             evidence["server_markers"]["game_over"], paths["summary"]))
    for problem in problems:
        print("  - %s" % problem, file=sys.stderr)
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
