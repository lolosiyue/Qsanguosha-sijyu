#!/usr/bin/env python3
"""Plan, resume and grade package acceptance using real server/TUI games.

No synthetic scenarios or gameplay answers: tui_network_smoke owns production
Trustee playback; verification_report owns decision and skill-branch grading.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
from contextlib import contextmanager
from pathlib import Path

from general_acceptance import scan_logs
from runner_common import (spawn, close_proc, request_shutdown, verification_env, is_crash_code,
                           process_birth_token, recorded_process_alive, kill_recorded_process, terminate_tree,
                           isolated_settings_env)
from skill_branch_report import validate_specification
from tui_network_smoke import sha256_file
from verification_report import build_report, collect, decision_summary, write_report

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SKIP_DIRS = {".git", "__pycache__", "logs", "data", "temp", "node_modules", "autotest-logs", ".pytest_cache"}
SOURCE_DIRS = ("src", "swig", "cmake", "lua", "extensions", "lang", "packages", "tools/autotest")
SUFFIXES = {".cpp", ".c", ".h", ".hpp", ".i", ".cmake", ".lua", ".json", ".py", ".ini"}
RESUMABLE = {"PENDING", "INTERRUPTED"}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def safe_name(value):
    # Used for artifact paths as well as native IDs; never accept path traversal.
    return (isinstance(value, str) and bool(value) and value not in (".", "..")
            and all(c.isascii() and (c.isalnum() or c in "_-+") for c in value))


def csv_values(value):
    result = value.split(",")
    if not result or any(not safe_name(v) for v in result) or len(set(result)) != len(result):
        raise ValueError("expected distinct comma-separated IDs")
    return result


def source_manifest(root):
    """Include ignored Lua and uncommitted source, not merely Git HEAD."""
    files = {}
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for current, dirs, names in os.walk(base):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
            for name in sorted(names):
                path = Path(current) / name
                allowed = path.suffix == ".py" if directory == "tools/autotest" else path.suffix.lower() in SUFFIXES
                if allowed and ".bak" not in name:
                    files[path.relative_to(root).as_posix()] = sha256_file(path)
    for name in ("CMakeLists.txt", "CMakePresets.json", "QSanguoshaSources.cmake", "config.ini"):
        if (root / name).is_file():
            files[name] = sha256_file(root / name)
    if not any(p.startswith("lua/ai/isolated/") for p in files):
        raise ValueError("runtime has no isolated AI sources; synchronize the external AI repository first")
    return dict(sorted(files.items()))


def environment_identity():
    # Record only hashes, never API credentials. These overrides can change AI
    # or loaded rules without changing an executable or a source file.
    return {key: hashlib.sha256(value.encode()).hexdigest()
            for key, value in sorted(os.environ.items())
            if key.startswith(("QSAN_AI", "QSAN_RULES", "QSAN_EXTENSIONS", "QSAN_HEG"))
            and key != "QSAN_AI_UNSUPPORTED_TRACE"}


@contextmanager
def batch_lock(out):
    """An OS lock is released on crash; never infer ownership from an old PID."""
    out.mkdir(parents=True, exist_ok=True)
    handle = (out / "run.lock").open("a+b")
    handle.seek(0, os.SEEK_END)
    if not handle.tell():
        handle.write(b"0")
        handle.flush()
    handle.seek(0)
    try:
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        handle.close()
        raise ValueError("another process owns this batch output directory")
    try:
        yield
    finally:
        handle.close()


def select_groups(catalog, groups):
    selected, seen = [], set()
    for group in groups:
        if group == "standard":
            members = [g for g in catalog["generals"] if g["package"] == "standard"
                       and not g["name"].startswith("nos")]
        elif group == "nos":
            # NOS is a name family, not a single registered package. Keep the
            # actual package in the manifest rather than inventing one.
            members = [g for g in catalog["generals"] if g["name"].startswith("nos")]
        else:
            members = [g for g in catalog["generals"] if g["package"] == group]
        if not members:
            raise ValueError(f"group {group!r} has no loaded generals; inspect catalog.json")
        for general in sorted(members, key=lambda g: g["name"]):
            if general["name"] not in seen:
                if not safe_name(general["name"]):
                    raise ValueError("unsafe general ID in native catalog")
                seen.add(general["name"])
                selected.append({**general, "group": group})
    return selected


def load_specifications(directory, generals):
    result = {}
    if directory is None:
        return result
    by_name = {g["name"]: g for g in generals}
    for path in sorted(directory.rglob("*.json")):
        spec = read_json(path)
        if not isinstance(spec, dict):
            raise ValueError(f"{path}: specification must be an object")
        name, skill = spec.get("general_id"), spec.get("skill_id")
        if name not in by_name:
            continue
        if skill not in {s["name"] for s in by_name[name]["skills"]}:
            raise ValueError(f"{path}: skill is absent from the loaded general")
        key = name + "/" + skill
        if key in result:
            raise ValueError(f"duplicate specification for {key}")
        if spec.get("reviewed") is True:
            validate_specification(spec)
            for branch in spec["branches"]:
                if "$subject" not in branch["when"]["where"].values():
                    raise ValueError(f"{path}: every branch must bind its trigger to $subject")
        result[key] = spec
    return result


def plan_batch(args):
    root, out = Path(args.root).resolve(), Path(args.out).resolve()
    if any(out.is_relative_to(root / name) for name in SOURCE_DIRS):
        raise ValueError("output must be outside source/runtime input directories (use artifacts/ or memory/work/)")
    server, tui = Path(args.server).resolve(), Path(args.tui).resolve()
    for path in (server, tui):
        if not path.is_file():
            raise ValueError(f"executable not found: {path}")
    groups, modes = csv_values(args.groups), csv_values(args.modes)
    seeds = [int(s) for s in args.seeds.split(",")]
    if not seeds or len(set(seeds)) != len(seeds) or any(s < 0 or s >= 2**64 for s in seeds):
        raise ValueError("seeds must be distinct unsigned 64-bit integers")
    with batch_lock(out):
        if (out / "plan.json").exists():
            raise ValueError("plan already exists; run/status it or use a new output directory")
        env = isolated_settings_env(os.environ, out / "catalog-userdata")
        env["QSAN_USER_DATA_ROOT"] = str(out / "catalog-userdata")
        command = [str(server), "--asset-root", str(root), "--acceptance-catalog"]
        try:
            process = subprocess.run(command, cwd=root, env=env, capture_output=True,
                                     timeout=args.catalog_seconds)
        except subprocess.TimeoutExpired as error:
            # Preserve real startup evidence even when the catalog never emits JSON.
            (out / "catalog.log").write_bytes(error.stderr or b"")
            (out / "catalog-partial.json").write_bytes(error.stdout or b"")
            raise ValueError(f"native catalog exceeded {args.catalog_seconds}s; see catalog.log") from error
        (out / "catalog.log").write_bytes(process.stderr)
        if process.returncode:
            raise ValueError(f"native catalog failed ({process.returncode}); see catalog.log")
        catalog = json.loads(process.stdout)
        if catalog.get("schema") != 1 or not catalog.get("generals"):
            raise ValueError("native catalog schema is unsupported or empty")
        atomic_json(out / "catalog.json", catalog)
        generals = select_groups(catalog, groups)
        if args.only:
            wanted = set(csv_values(args.only))
            missing = wanted - {g["name"] for g in generals}
            if missing:
                raise ValueError("generals outside selected groups: " + ", ".join(sorted(missing)))
            generals = [g for g in generals if g["name"] in wanted]
        available = {m["id"] for m in catalog["modes"]}
        if set(modes) - available:
            raise ValueError("unregistered modes: " + ", ".join(sorted(set(modes) - available)))
        specifications = load_specifications(Path(args.spec_dir).resolve() if args.spec_dir else None, generals)
        manifest = source_manifest(root)
        atomic_json(out / "source-manifest.json", manifest)
        cells = [{"id": f"{g['group']}/{g['name']}/{mode}/{seed}", "group": g["group"],
                  "general": g["name"], "mode": mode, "seed": seed}
                 for g in generals for mode in modes for seed in seeds]
        plan = {"schema": 1, "root": str(root), "server": str(server), "tui": str(tui),
                "groups": groups, "modes": modes, "seeds": seeds, "generals": generals,
                "cells": cells, "game_seconds": args.game_seconds, "startup_seconds": args.startup_seconds,
                "catalog_sha256": sha256_file(out / "catalog.json"),
                "source_digest": digest(manifest), "server_sha256": sha256_file(server),
                "tui_sha256": sha256_file(tui), "environment": environment_identity(),
                "specifications": specifications, "code_identity": catalog.get("code_identity")}
        plan["id"] = digest(plan)
        atomic_json(out / "plan.json", plan)
        atomic_json(out / "state.json", {"schema": 1, "plan_id": plan["id"],
            "cells": {c["id"]: {"status": "PENDING", "attempts": []} for c in cells}})
        # A draft is a checklist for an independent rule review, never an oracle
        # generated from implementation behavior or an automatic PASS condition.
        for general in generals:
            for skill in general["skills"]:
                if skill["visible"] and skill["direct"] and safe_name(skill["name"]):
                    draft = out / "spec-drafts" / general["name"] / (skill["name"] + ".json")
                    atomic_json(draft, {"version": 1, "general_id": general["name"],
                        "skill_id": skill["name"], "spec_source": skill["description"] or "REVIEW REQUIRED",
                        "reviewed": False, "branches": []})
        summarize(out, plan, read_json(out / "state.json"))
        print(f"Prepared {len(generals)} generals / {len(cells)} games; no games started: {out / 'plan.json'}")
    return 0


def load_plan(out):
    plan, state = read_json(out / "plan.json"), read_json(out / "state.json")
    identity = plan.get("id")
    if plan.get("schema") != 1 or digest({k: v for k, v in plan.items() if k != "id"}) != identity:
        raise ValueError("plan has changed or has an unsupported schema; create a new plan")
    if state.get("plan_id") != identity or set(state.get("cells", {})) != {c["id"] for c in plan["cells"]}:
        raise ValueError("state does not belong to this plan")
    return plan, state


def check_identity(out, plan):
    if (sha256_file(plan["server"]) != plan["server_sha256"]
            or sha256_file(plan["tui"]) != plan["tui_sha256"]
            or digest(source_manifest(Path(plan["root"]))) != plan["source_digest"]
            or environment_identity() != plan["environment"]
            or sha256_file(out / "catalog.json") != plan["catalog_sha256"]):
        raise ValueError("source/runtime/binary/environment changed; preserve this batch and make a new plan")


def bind_subject(value, seat):
    if isinstance(value, dict):
        return {k: bind_subject(v, seat) for k, v in value.items()}
    if isinstance(value, list):
        return [bind_subject(v, seat) for v in value]
    return seat if value == "$subject" else value


def ai_status(summary):
    if summary["status"] == "FAIL":
        return "FAIL"
    if summary["status"] == "UNKNOWN":
        return "UNKNOWN"
    if (summary["status"] == "REVIEW"
            or any(k != "isolated" and v.get("accepted", 0) for k, v in summary["sources"].items())):
        return "REVIEW"
    return "PASS" if summary["sources"].get("isolated", {}).get("accepted", 0) else "UNKNOWN"


def grade_attempt(out, plan, cell, attempt):
    directory = out / attempt["directory"]
    evidence_dir = directory / "evidence"
    logs = list((directory / "game").glob("*.log"))
    observed = scan_logs(logs, cell["general"], [])
    seats = sorted(set(observed["forced_seats"]))
    subject = seats[0] if len(seats) == 1 else None
    spec_paths = []
    specifications = {key.split("/", 1)[1]: value for key, value in plan["specifications"].items()
                      if key.startswith(cell["general"] + "/")}
    for skill, spec in specifications.items():
        if subject and spec.get("reviewed") is True:
            path = directory / "bound-specs" / (hashlib.sha256(skill.encode()).hexdigest()[:16] + ".json")
            atomic_json(path, bind_subject(spec, subject))
            spec_paths.append(path)
    report = build_report([directory / "game", evidence_dir], spec_paths, ["isolated"],
                          run_ids=[attempt["run_id"]])
    write_report(report, directory / "verification")
    evidence = collect([directory / "game", evidence_dir])
    # General selection is setup, not evidence that the forced seat can play.
    decisions = [r for r in evidence["decisions"] if r.get("trace_run_id") == attempt["run_id"]
                 and r.get("player") == subject and r.get("callback") != "askForGeneral"]
    subject_ai = decision_summary(decisions, 3)
    subject_status = ai_status(subject_ai) if subject else "UNKNOWN"
    room_status = ai_status(report["decisions"])
    if observed["lua_error_count"] and room_status == "PASS":
        room_status = "REVIEW"
    if report["input_errors"]:
        subject_status = room_status = "UNKNOWN"
    summary_path = directory / "game" / "complete-game-summary.json"
    game = read_json(summary_path) if summary_path.exists() else {}
    playback = "PASS" if game.get("ok") is True and subject and attempt.get("runner_exit") == 0 else "FAIL"
    if game.get("timed_out"):
        playback = "TIMEOUT"
    if any(is_crash_code(code) for code in [attempt.get("runner_exit"),
            game.get("lifecycle", {}).get("server_exit"), game.get("lifecycle", {}).get("client_exit")]
           if type(code) is int):
        playback = "CRASH"
    if game.get("interrupted") or attempt.get("cancelled"):
        playback = "INTERRUPTED"
    definitions = next(g for g in plan["generals"] if g["name"] == cell["general"])
    branch_results = {r.get("branches", [{}])[0].get("skill_id"): r
                      for r in report["skill_branches"] if r.get("branches")}
    skills = []
    for skill in definitions["skills"]:
        if not (skill["visible"] and skill["direct"]):
            continue
        spec = specifications.get(skill["name"])
        graded = branch_results.get(skill["name"])
        status = ("NOT_SPECIFIED" if spec is None else "UNREVIEWED" if spec.get("reviewed") is not True
                  else "UNKNOWN" if graded is None else graded["status"])
        skills.append({"skill": skill["name"], "v2": skill["v2"], "status": status,
                       "branches": graded.get("branches", []) if graded else []})
    row = {"playback": playback, "subject": subject, "subject_ai": subject_status,
           "room_ai": room_status, "subject_decisions": subject_ai,
           "skills": skills, "lua_errors": observed["lua_errors"],
           "lifecycle": game.get("lifecycle", {}), "winner": game.get("evidence", {}).get("server_markers", {}).get("game_over"),
           "verification": str((directory / "verification" / "verification.json").relative_to(out))}
    atomic_json(directory / "result.json", row)
    return row


def run_cell(out, plan, cell, attempt, budget_deadline, cancelled):
    directory = out / attempt["directory"]
    directory.mkdir(parents=True, exist_ok=False)
    env = verification_env(os.environ, directory / "evidence")
    attempt["run_id"] = env["QSAN_TRACE_RUN_ID"]
    env.update(QSAN_USER_DATA_ROOT=str(directory / "userdata"), QSAN_AI_UNSUPPORTED_TRACE="1",
               PYTHONIOENCODING="utf-8")
    total = min(plan["startup_seconds"] + plan["game_seconds"], budget_deadline - time.monotonic())
    command = [sys.executable, str(HERE / "tui_network_smoke.py"),
               "--server", plan["server"], "--tui", plan["tui"], "--workdir", plan["root"],
               "--artifact-dir", str(directory / "game"), "--mode", cell["mode"],
               "--seed", str(cell["seed"]), "--force-general", cell["general"],
               "--server-startup-timeout", str(plan["startup_seconds"]),
               "--process-timeout", str(plan["game_seconds"]),
               "--game-timeout-ms", str(plan["game_seconds"] * 1000),
               "--deadline-seconds", str(max(0.01, total))]
    atomic_json(directory / "attempt.json", {**attempt, "cell": cell, "command": command})
    process = spawn(command, plan["root"], str(directory / "runner.log"), env=env)
    attempt.update(host=socket.gethostname(), runner={"pid": process.pid, "birth": process_birth_token(process.pid)})
    atomic_json(directory / "attempt.json", {**attempt, "cell": cell, "command": command})
    try:
        sent_cancel = False
        watchdog = time.monotonic() + total + 90  # bounded native cleanup allowance
        while process.poll() is None:
            if cancelled.is_set() and not sent_cancel:
                attempt["cancelled"] = True
                request_shutdown(process)
                sent_cancel = True
                watchdog = min(watchdog, time.monotonic() + 90)
            if time.monotonic() >= watchdog:
                summary_path = directory / "game" / "complete-game-summary.json"
                game = read_json(summary_path) if summary_path.exists() else {}
                owner = game.get("ownership", {})
                cleanup = []
                if owner.get("host") == socket.gethostname():
                    for record in reversed(owner.get("processes", [])):
                        if record.get("role") != "runner":
                            cleanup.append({**record, "cleanup": kill_recorded_process(record)})
                attempt["watchdog_cleanup"] = cleanup
                attempt["watchdog_expired"] = True
                terminate_tree(process)
                break
            time.sleep(0.2)
        attempt["runner_exit"] = process.returncode
    finally:
        # A parsing/I/O error in the watchdog must still stop the owned runner.
        if process.poll() is None:
            request_shutdown(process)
            terminate_tree(process, graceful_timeout=60)
        close_proc(process)
    attempt["finished_at"] = time.time()
    result = grade_attempt(out, plan, cell, attempt)
    if attempt.get("watchdog_expired"):
        result["playback"] = "ERROR"
        atomic_json(directory / "result.json", result)
    attempt["result"] = result
    attempt["status"] = result["playback"] if result["playback"] != "PASS" else "COMPLETED"
    atomic_json(directory / "attempt.json", {**attempt, "cell": cell, "command": command})
    return attempt


def merge_branch_status(statuses):
    if "FAIL" in statuses:
        return "FAIL"
    if "UNKNOWN" in statuses:
        return "UNKNOWN"
    if "PASS" in statuses:
        return "PASS"
    return "NOT_COVERED"


def summarize(out, plan, state):
    rows, general_rows = [], []
    for cell in plan["cells"]:
        entry = state["cells"][cell["id"]]
        last = entry["attempts"][-1] if entry["attempts"] else {}
        rows.append({**cell, "status": entry["status"], "attempts": len(entry["attempts"]),
                     "previous_attempts": [{"status": a.get("status"), "directory": a["directory"],
                                            "result": a.get("result")} for a in entry["attempts"][:-1]],
                     "directory": last.get("directory"), **last.get("result", {})})
    for general in plan["generals"]:
        games = [r for r in rows if r["general"] == general["name"]]
        coverage = []
        for skill in general["skills"]:
            if not (skill["visible"] and skill["direct"]):
                continue
            spec = plan["specifications"].get(general["name"] + "/" + skill["name"])
            observed = [s for g in games for s in g.get("skills", []) if s["skill"] == skill["name"]]
            branches = []
            if spec and spec.get("reviewed") is True:
                for branch in spec["branches"]:
                    statuses = [b["status"] for s in observed for b in s["branches"] if b["branch_id"] == branch["id"]]
                    branches.append({"id": branch["id"], "text": branch["text"],
                                     "status": merge_branch_status(statuses) if statuses else "UNKNOWN"})
            status = ("NOT_SPECIFIED" if spec is None else "UNREVIEWED" if spec.get("reviewed") is not True
                      else "PASS" if branches and all(b["status"] == "PASS" for b in branches)
                      else "FAIL" if any(b["status"] == "FAIL" for b in branches) else "INCOMPLETE")
            coverage.append({"skill": skill["name"], "v2": skill["v2"], "status": status, "branches": branches})
        settled = all(g["status"] not in RESUMABLE | {"RUNNING"} for g in games)
        passed = settled and all(g.get("playback") == "PASS" and g.get("subject_ai") == "PASS"
                                and g.get("room_ai") == "PASS" for g in games)
        passed = passed and all(s["v2"] and s["status"] == "PASS" for s in coverage)
        failed = any(g.get("playback") in {"FAIL", "CRASH"} or g["status"] == "ERROR"
                     or g.get("subject_ai") == "FAIL" or g.get("room_ai") == "FAIL" for g in games)
        failed = failed or any(s["status"] == "FAIL" for s in coverage)
        review = any(g.get("subject_ai") == "REVIEW" or g.get("room_ai") == "REVIEW" for g in games)
        general_rows.append({"general": general["name"], "group": general["group"],
                             "status": "FAIL" if failed else "REVIEW" if review else "PASS" if passed else "INCOMPLETE",
                             "skills": coverage})
    summary = {"schema": 1, "plan_id": plan["id"], "counts": dict(Counter(r["status"] for r in rows)),
               "status": "INVALIDATED" if state.get("invalidated") else
                         "FAIL" if any(g["status"] == "FAIL" for g in general_rows) else
                         "REVIEW" if any(g["status"] == "REVIEW" for g in general_rows) else
                         "PASS" if general_rows and all(g["status"] == "PASS" for g in general_rows) else "INCOMPLETE",
               "generals": general_rows, "games": rows}
    atomic_json(out / "summary.json", summary)
    lines = ["# Package acceptance", "", f"Overall: {summary['status']}", "",
             "| Group | General | Mode | Seed | Run | Playback | Subject AI | Room AI | Evidence |",
             "|---|---|---|---|---|---|---|---|---|"]
    for row in rows:
        link = f"[result]({row['directory']}/result.json)" if row.get("directory") else ""
        lines.append("| " + " | ".join(str(row.get(k, "NOT_RUN")) for k in
                     ("group", "general", "mode", "seed", "status", "playback", "subject_ai", "room_ai"))
                     + f" | {link} |")
    lines += ["", "## Skill coverage", "", "| General | Skill | V2 definition | Branch coverage |",
              "|---|---|---|---|"]
    for general in general_rows:
        for skill in general["skills"]:
            lines.append(f"| {general['general']} | {skill['skill']} | {skill['v2']} | {skill['status']} |")
    lines += ["", "Coverage combines independently observed branches across the declared games; failures and unknown evidence remain visible.",
              "Missing/unreviewed specifications never pass. V2 classification is a definition inventory, not proof of behavior.",
              "Every retry keeps its original attempt directory. Read state.json for all attempts, including earlier failures.", ""]
    (out / "summary.md").write_text("\n".join(lines), encoding="utf-8")
    return summary


def recover_interrupted(out, entry):
    if not entry["attempts"]:
        entry["status"] = "INTERRUPTED"
        return
    attempt = entry["attempts"][-1]
    directory = out / attempt["directory"]
    saved = read_json(directory / "attempt.json") if (directory / "attempt.json").exists() else attempt
    summary_path = directory / "game" / "complete-game-summary.json"
    game = read_json(summary_path) if summary_path.exists() else {}
    owner = game.get("ownership", {})
    host = saved.get("host", owner.get("host"))
    processes = [saved.get("runner", {}), *owner.get("processes", [])]
    if host and host != socket.gethostname():
        raise ValueError(f"interrupted attempt belongs to another host: {directory}; inspect cleanup there first")
    if any(recorded_process_alive(record) for record in processes):
        raise ValueError(f"interrupted attempt still has live recorded PIDs: {directory}; no duplicate game started")
    # The child may have finished after its orchestrator disappeared. Preserve
    # its durable result instead of unnecessarily replaying a completed game.
    if saved.get("status") != "RUNNING" and saved.get("result"):
        entry["attempts"][-1] = {k: v for k, v in saved.items() if k not in {"command", "cell"}}
        entry["status"] = saved["status"]
    else:
        entry["status"] = "INTERRUPTED"
        entry["attempts"][-1]["status"] = "INTERRUPTED"


def run_batch(args):
    out = Path(args.out).resolve()
    with batch_lock(out):
        plan, state = load_plan(out)
        if state.get("invalidated"):
            raise ValueError("batch was invalidated by a changed runtime; create a new plan")
        check_identity(out, plan)
        if args.only and set(csv_values(args.only)) - {g["name"] for g in plan["generals"]}:
            raise ValueError("--only includes generals outside this plan")
        for entry in state["cells"].values():
            if entry["status"] == "RUNNING":
                recover_interrupted(out, entry)
        if set(args.retry) - set(state["cells"]):
            raise ValueError("--retry must name a cell ID from summary.json")
        for identity in args.retry:
            state["cells"][identity]["status"] = "PENDING"
        cancelled = threading.Event()
        old_signals = {}
        for sig in (signal.SIGINT, signal.SIGTERM):
            old_signals[sig] = signal.signal(sig, lambda *_: cancelled.set())
        budget_deadline = time.monotonic() + args.budget_minutes * 60
        started = 0
        try:
            # Preserve requested package order while allowing bounded concurrency
            # within a package; never enqueue the entire unbounded matrix.
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                for group in plan["groups"]:
                    todo = [c for c in plan["cells"] if c["group"] == group
                            and state["cells"][c["id"]]["status"] in RESUMABLE
                            and (not args.only or c["general"] in csv_values(args.only))]
                    active = {}
                    while todo or active:
                        while (todo and len(active) < args.jobs and not cancelled.is_set()
                               and time.monotonic() < budget_deadline
                               and (args.max_games is None or started < args.max_games)):
                            cell = todo.pop(0)
                            entry = state["cells"][cell["id"]]
                            attempt = {"status": "RUNNING", "started_at": time.time(),
                                "directory": "runs/" + cell["id"] + f"/attempt-{len(entry['attempts']) + 1:03d}"}
                            entry["status"] = "RUNNING"
                            entry["attempts"].append(attempt)
                            atomic_json(out / "state.json", state)
                            future = pool.submit(run_cell, out, plan, cell, dict(attempt), budget_deadline, cancelled)
                            active[future] = cell
                            started += 1
                            print(f"[RUNNING] {cell['id']}", flush=True)
                        if not active:
                            break
                        completed, _ = wait(active, timeout=1, return_when=FIRST_COMPLETED)
                        for future in completed:
                            cell = active.pop(future)
                            entry = state["cells"][cell["id"]]
                            try:
                                entry["attempts"][-1] = future.result()
                            except Exception as error:
                                entry["attempts"][-1].update(status="ERROR", error=f"{type(error).__name__}: {error}")
                            entry["status"] = entry["attempts"][-1]["status"]
                            atomic_json(out / "state.json", state)
                            summarize(out, plan, state)
                            print(f"[{entry['status']}] {cell['id']}", flush=True)
                    if cancelled.is_set() or time.monotonic() >= budget_deadline:
                        break
        finally:
            for sig, handler in old_signals.items():
                signal.signal(sig, handler)
            atomic_json(out / "state.json", state)
            summary = summarize(out, plan, state)
        # A changed runtime during a batch invalidates its unqualified results.
        try:
            check_identity(out, plan)
        except ValueError as error:
            state["invalidated"] = str(error)
            atomic_json(out / "state.json", state)
            summarize(out, plan, state)
            raise
        return 0 if summary["status"] == "PASS" else 1 if summary["status"] == "FAIL" else 2


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan", help="query native registry and freeze a resumable plan; starts no games")
    plan.add_argument("--root", default=str(ROOT))
    plan.add_argument("--server", required=True)
    plan.add_argument("--tui", required=True)
    plan.add_argument("--out", required=True)
    plan.add_argument("--groups", default="standard,nos,wind,fire")
    plan.add_argument("--modes", default="03_1v2")
    plan.add_argument("--seeds", default="20261113")
    plan.add_argument("--only", help="optional comma-separated general IDs within the selected groups")
    plan.add_argument("--spec-dir", help="reviewed specifications; use $subject for the forced seat")
    plan.add_argument("--game-seconds", type=int, default=1800)
    plan.add_argument("--startup-seconds", type=int, default=120)
    plan.add_argument("--catalog-seconds", type=int, default=120)
    run = commands.add_parser("run", help="resume pending games; no automatic retries of failures/timeouts")
    run.add_argument("--out", required=True)
    run.add_argument("--budget-minutes", type=float, required=True)
    run.add_argument("--jobs", type=int, default=1)
    run.add_argument("--max-games", type=int)
    run.add_argument("--only")
    run.add_argument("--retry", action="append", default=[], help="explicit cell ID; keeps all previous attempts")
    status = commands.add_parser("status", help="regenerate JSON/Markdown summaries without starting a game")
    status.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    for name in ("game_seconds", "startup_seconds", "catalog_seconds", "budget_minutes", "jobs", "max_games"):
        if (hasattr(args, name) and getattr(args, name) is not None
                and (not math.isfinite(getattr(args, name)) or getattr(args, name) <= 0)):
            parser.error(f"--{name.replace('_', '-')} must be positive")
    try:
        if args.command == "plan":
            return plan_batch(args)
        if args.command == "run":
            return run_batch(args)
        out = Path(args.out).resolve()
        with batch_lock(out):
            plan, state = load_plan(out)
            summary = summarize(out, plan, state)
        print(json.dumps({"status": summary["status"], "counts": summary["counts"],
                          "report": str(out / "summary.md")}, ensure_ascii=False))
        return 0 if summary["status"] == "PASS" else 1 if summary["status"] == "FAIL" else 2
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"package acceptance: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
