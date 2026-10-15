"""Accept one general with Trustee playback by default; full skill/mode coverage is opt-in.

Every game stage gives the general to the room owner through the server's
--force-general, so all clients play the same general without client changes.
Crashes from every stage (exit codes, minidumps, Android crash logcat, XP dumps)
land in one crash report: crashes/ plus crashes.json and the summary's Crashes section.
See tools/autotest/README.md (general_acceptance.py) for the stages and evidence.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from runner_common import (  # noqa: E402
    MARK_GAME_OVER, describe_exit, find_exe, free_tcp_port, hex_exit, is_crash_code, resolve_workdir, spawn,
    terminate_tree, wait_port,
)
from headless_runner import SCENARIO_MODES  # noqa: E402
from minidump_parse import parse_dump  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
LOCAL_STAGES = ["describe", "skill", "ui", "headless", "gui", "gui_heg", "tui"]
TRUSTEE_STAGES = ["gui", "tui"]
DEVICE_STAGES = ["android", "xp"]
INDEPENDENT_STAGES = ["web", "sheets"]
UNAVAILABLE_STAGES = ["linux"]
PLATFORM_MODE = "03_1v2"
HEGEMONY_MODE = "08p"
SCREENSHOT_ROOT = HERE / "autotest-logs" / "skill-ui-latest"
LUA_ERROR_RE = re.compile(r"attempt to |stack traceback|\[AI_CALLBACK_ERROR\]|lua_error|Lua error", re.I)
FORCE_RE = re.compile(r"\[AUTOTEST\] force general (\S+) (\S+)")
ANDROID_PACKAGE = "org.qsanguosha.game"
# AGENTS.md section 9: the H working tree is the authority for these L directories.
EXTENSIONS_AUTHORITY = r"H:\Program file\Game\sgs\Qsgs\working\extensions"
EXTENSIONS_MAP = (("ai", "lua/ai"), ("extensions", "extensions"), ("lua", "lua"))
SYNC_SKIP_DIRS = {".git", "logs", "data", "temp"}
# Stages that load the L Lua tree; graded evidence (xp/web/sheets) brings its own runtime.
LUA_STAGES = set(LOCAL_STAGES) | {"android"}
# Built-in skill case: a hidden skill-less general must stay hidden after an ordinary use.
# It catches helper skills that extensions bind to every general's head (they force a reveal).
HEGEMONY_BASELINE_CASE = {
    "hegemony": True,
    "seats": [{"general": "sujiang", "hidden": True, "hand": ["slash"]}, {"general": "sujiang"}],
    "trigger": "select_use('slash', P[1], {P[2]}); "
               "error('HEG_BASELINE shown=' .. tostring(P[1]:hasShownGeneral()))",
    "expect": {"error": "HEG_BASELINE shown=false"},
}
ANDROID_ACTIVITY = ANDROID_PACKAGE + "/org.qtproject.qt.android.bindings.QtActivity"


def run(command, log_path, env, cwd=ROOT, timeout=None):
    """Run a child to completion with stdout/stderr in log_path; returns the exit code (None on timeout)."""
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        log.write("$ " + subprocess.list2cmdline([str(c) for c in command]) + "\n")
        log.flush()
        try:
            return subprocess.run([str(c) for c in command], cwd=cwd, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            return None


def read(path):
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def game_over(path):
    return any(MARK_GAME_OVER.search(line) for line in read(path).splitlines())


def tail(path, count=20):
    return read(path).splitlines()[-count:]


def logcat_smoke_result(adb, since, log_path):
    """Rebuild the client result from the NETWORK_UI_* lines the Android client logs."""
    log_path.write_bytes(subprocess.run(adb + ["logcat", "-d", "-T", since], capture_output=True).stdout)
    client = {"stages": []}
    for line in read(log_path).splitlines():
        for marker in ("NETWORK_UI_STAGE ", "NETWORK_UI_RESULT "):
            if marker not in line:
                continue
            try:
                payload = json.loads(line.split(marker, 1)[1])
            except ValueError:
                continue
            if marker.startswith("NETWORK_UI_STAGE"):
                client["stages"].append(payload)
            else:
                client.update(ok=payload.get("ok"), exit_code=payload.get("exit_code"),
                              errors=[payload["error"]] if payload.get("error") else [])
    return client


def exit_crash(stage, process, code, context=(), files=()):
    """A crash seen only as a process exit code."""
    return {"stage": stage, "process": process, "source": "exit code",
            "what": f"exit {hex_exit(code)} {describe_exit(code)}", "files": [str(f) for f in files],
            "context": list(context)[-20:]}


def dump_crash(stage, dump, files):
    """A CrashHandler minidump plus its crash-*.txt summary (native and Lua stacks)."""
    info = parse_dump(str(dump))
    rva = info.get("fault_rva", -1)
    where = f"{info.get('fault_module', '?')}+0x{rva:X}" if rva >= 0 else info.get("exc_addr", "?")
    return {"stage": stage, "process": dump.stem, "source": "minidump",
            "what": f"{info.get('exc_code', '-')} {info.get('exc_name', '?')} at {where}",
            "files": [str(f) for f in files], "context": read(dump.with_suffix(".txt")).splitlines()[:80]}


def dump_dirs():
    """CrashHandler writes dmp/ under each child's working directory."""
    return {ROOT / "dmp", Path(resolve_workdir(str(ROOT))) / "dmp", ROOT / "release" / "dmp"}


def dump_snapshot():
    return {p.resolve() for d in dump_dirs() if d.is_dir() for p in d.iterdir() if p.is_file()}


def scan_logs(paths, general, skills):
    """Evidence shared by every game stage: forced seat, skill mentions and Lua errors."""
    forced, errors, mentions = [], [], {skill: 0 for skill in skills}
    for path in paths:
        for line in read(path).splitlines():
            match = FORCE_RE.search(line)
            if match and match.group(2) == general:
                forced.append(match.group(1))
            if LUA_ERROR_RE.search(line):
                errors.append(f"{Path(path).name}: {line.strip()[:240]}")
            for skill in skills:
                if skill in line:
                    mentions[skill] += 1
    return {"forced_seats": forced, "skill_mentions": mentions, "lua_errors": errors[:20],
            "lua_error_count": len(errors)}


def game_status(ok, evidence):
    if not ok:
        return "FAIL"
    if not evidence["forced_seats"]:
        return "FAIL"  # the game ran without the general under test
    return "WARN" if evidence["lua_error_count"] else "PASS"


class Acceptance:
    def __init__(self, args):
        self.args = args
        self.general = args.general
        self.out = Path(args.out_dir or ROOT / "tools/autotest/autotest-logs/general-acceptance"
                        / f"{args.general}-{time.strftime('%Y%m%d-%H%M%S')}").resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.env = dict(os.environ)
        if args.qt_bin:
            self.env["PATH"] = args.qt_bin + os.pathsep + self.env.get("PATH", "")
            self.env.setdefault("QT_PLUGIN_PATH", str(Path(args.qt_bin).parent / "plugins"))
        self.env["PYTHONIOENCODING"] = "utf-8"
        self.info = {}
        self.results = {}
        self.crashes = []

    def stage_dir(self, name):
        path = self.out / name
        path.mkdir(exist_ok=True)
        return path

    def exe(self, name):
        return Path(find_exe(str(ROOT), name)).resolve()

    @property
    def visible_skills(self):
        return [s["name"] for s in self.info.get("skills", []) if s.get("visible")]

    # -- stages ---------------------------------------------------------------

    def describe(self):
        directory = self.stage_dir("describe")
        host = self.exe("qsanguosha_skill_scenario")
        info_path = directory / "general.json"
        code = run([host, "--describe-general", self.general, "--report", info_path],
                   directory / "describe.log", self.env, cwd=host.parent, timeout=300)
        if code != 0 or not info_path.exists():
            return "FAIL", f"describe exit {code}", {}
        self.info = json.loads(info_path.read_text(encoding="utf-8"))
        return "PASS", f"package {self.info['package']}, skills {', '.join(self.visible_skills)}", \
            {"general": str(info_path)}

    def skill(self):
        if not self.args.cases:
            return "NOT RUN", "no --cases directory (agent writes scratchpad cases from the skill text)", {}
        cases = sorted(Path(self.args.cases).glob("*.json"))
        if not cases:
            return "NOT RUN", f"no case files in {self.args.cases}", {}
        directory = self.stage_dir("skill")
        builtin = directory / "builtin" / "heg_hidden_basic_use_baseline.json"
        builtin.parent.mkdir(exist_ok=True)
        builtin.write_text(json.dumps(HEGEMONY_BASELINE_CASE, ensure_ascii=False, indent=2), encoding="utf-8")
        cases.append(builtin)
        code = run([sys.executable, HERE / "skill_scenario_runner.py", *cases,
                    "--exe", self.exe("qsanguosha_skill_scenario"), "--seed", self.args.seed,
                    "--out-dir", directory, "--jobs", self.args.jobs], directory / "runner.log", self.env)
        lines = [l for l in read(directory / "runner.log").splitlines() if l.startswith("[")]
        failed = [l for l in lines if not l.startswith("[PASS]")]
        crashes = []
        for match in re.finditer(r"no report \(exit (-?\d+)\); see (.+)", read(directory / "runner.log")):
            if is_crash_code(int(match.group(1))):
                log = Path(match.group(2).strip())
                crashes.append(exit_crash("skill", "qsanguosha_skill_scenario " + log.stem,
                                          int(match.group(1)), tail(log), [log]))
        status = "PASS" if code == 0 else "FAIL"
        return status, f"{len(lines) - len(failed)}/{len(lines)} cases pass", {"failed": failed, "crashes": crashes}

    def ui(self):
        if not self.visible_skills:
            return "NOT RUN", "no visible skills", {}
        directory = self.stage_dir("ui")
        cases = []
        for index, skill in enumerate(self.visible_skills):
            case = directory / f"invoke_{skill}.json"
            case.write_text(json.dumps(self.invoke_case(skill, 1100 + index), ensure_ascii=False, indent=2),
                            encoding="utf-8")
            cases.append(case)
        # Screenshots stay in a fixed place for the human check until the next ui run replaces them.
        artifacts = SCREENSHOT_ROOT / self.general
        shutil.rmtree(artifacts, ignore_errors=True)
        command = [sys.executable, HERE / "skill_ui_runner.py", "--exe", self.exe("QSanguosha"),
                   "--artifact-root", artifacts, "--runtime-root", resolve_workdir(str(ROOT))]
        for case in cases:
            command += ["--case", case]
        if self.args.qt_bin:
            command += ["--qt-root", Path(self.args.qt_bin).parent]
        code = run(command, directory / "runner.log", self.env)
        summary = artifacts / "summary.json"
        results = json.loads(read(summary) or "{}").get("results", [])
        crashes = [exit_crash("ui", "QSanguosha " + r["name"], r["return_code"], [r.get("error", "")],
                              [r["artifact_dir"]]) for r in results if r.get("result") == "CRASH"]
        screenshots = sorted(str(p) for p in artifacts.rglob("*.png"))
        return ("PASS" if code == 0 else "FAIL"), \
            f"{len(cases)} invoke prompts, {len(screenshots)} screenshots in {artifacts}, exit {code}", \
            {"summary": str(summary), "screenshots": screenshots, "crashes": crashes}

    def invoke_case(self, skill, serial):
        """The skill's invoke prompt must show its translated name, never the raw id."""
        return {
            "schema_version": 1,
            "name": f"invoke_{skill}",
            "packages": [self.info["package"]],
            "bootstrap": {
                "mode": "02p",
                "self": {"object_name": "self", "screen_name": "Tester", "general": self.general,
                         "hp": self.info["start_hp"], "max_hp": self.info["max_hp"], "phase": "NotActive",
                         "skills": [skill]},
                "players": [{"object_name": "target", "screen_name": "Target", "general": "sujiang",
                             "hp": 4, "max_hp": 4, "alive": True}],
                "cards": [],
            },
            "request": {"command": "S_COMMAND_INVOKE_SKILL", "serial": serial, "raw_body": [skill, ""]},
            "expect_presented": {
                "client_status": "AskForSkillInvoke",
                "prompt": {"translation_required": True, "not_contains": [skill]},
                "skills": {"visible": [skill]},
                "buttons": {"ok": {"enabled": True}, "cancel": {"enabled": True}},
            },
            # presented.png is taken as the prompt appears; these record the settled prompt and the result.
            "actions": [{"type": "take_screenshot", "name": "prompt", "settle_ms": 1500},
                        {"type": "invoke_skill_yes"},
                        {"type": "take_screenshot", "name": "after_invoke", "settle_ms": 1500}],
            "expect_reply": {"command": "S_COMMAND_INVOKE_SKILL", "local_serial": serial, "invoke": True},
            "expect_final": {"client_status": "NotActive", "open_dialog_count": 0},
        }

    def headless_modes(self):
        server = self.exe("qsanguosha_server")
        listing = subprocess.run([str(server), "--list-game-modes"], capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", cwd=resolve_workdir(str(ROOT)),
                                 env=self.env, timeout=120).stdout
        modes = [line.split("\t")[0].strip() for line in listing.splitlines()[1:] if "\t" in line]
        modes += sorted(SCENARIO_MODES)
        excluded = {m.strip() for m in self.args.exclude_modes.split(",") if m.strip()}
        # emjr Cao Cao stage modes are not part of general acceptance.
        return [m for m in modes if m and m not in excluded and "_emjr_cc" not in m]

    def headless(self):
        directory = self.stage_dir("headless")
        modes = self.headless_modes()
        code = run([sys.executable, HERE / "headless_runner.py", "--exe", self.exe("QSanguosha"),
                    "--exe-root", ROOT, "--seed", self.args.seed, "--modes", ",".join(modes), "--games", 1,
                    "--parallel", self.args.jobs, "--force-general", self.general, "--log-dir", directory,
                    "--deadline-minutes", self.args.headless_minutes],
                   directory / "runner.log", self.env)
        summaries = sorted(directory.glob("summary-headless-*.csv"))
        rows = list(csv.DictReader(open(summaries[-1], encoding="utf-8-sig"))) if summaries else []
        run_dirs = sorted((directory / "headless").glob("*/"))
        per_mode, bad, crashes, budget = {}, [], [], {"stopped": [], "not_run": []}
        for row in rows:
            if row.get("crashed") in ("True", "true", "1") and run_dirs:
                log = run_dirs[-1] / f"{row['mode']}-headless.log"
                crashes.append(exit_crash("headless", f"QSanguosha --headless {row['mode']}",
                                          int(row["exit"]), tail(log), [log]))
        for mode in modes:
            logs = [run_dirs[-1] / f"{mode}.log", run_dirs[-1] / f"{mode}-headless.log"] if run_dirs else []
            evidence = scan_logs(logs, self.general, self.visible_skills)
            row = next((r for r in rows if r.get("mode") == mode), {})
            if row.get("budget") in budget:
                # Unfinished because the agreed budget ran out: report it, never grade it.
                budget[row["budget"]].append(mode)
                per_mode[mode] = {"status": "STOPPED" if row["budget"] == "stopped" else "NOT RUN"}
                continue
            passed = str(row.get("result", row.get("status", ""))).upper().startswith("PASS") \
                or row.get("ok") in ("True", "1", "true")
            status = game_status(passed, evidence)
            per_mode[mode] = {"status": status, **evidence}
            if status != "PASS":
                bad.append(f"{mode}:{status}")
        status = "PASS" if not bad and code == 0 else ("FAIL" if any(":FAIL" in b for b in bad) or code else "WARN")
        unfinished = budget["stopped"] + budget["not_run"]
        if unfinished and status == "PASS":
            status = "WARN"  # a partial matrix is not full coverage
        graded = len(modes) - len(unfinished)
        detail = f"{graded - len(bad)}/{graded} graded modes clean" + (f"; {', '.join(bad)}" if bad else "")
        if unfinished:
            def listed(names):  # full lists stay in summary.json
                return f"{len(names)}" + (f" ({', '.join(names[:8])}{', ...' if len(names) > 8 else ''})"
                                          if names else "")
            detail += (f"; budget {self.args.headless_minutes:g} min: stopped {listed(budget['stopped'])}"
                       f", NOT RUN {listed(budget['not_run'])}")
        return status, detail, {"modes": per_mode, "budget": budget,
                                "summary_csv": str(summaries[-1]) if summaries else None, "crashes": crashes}

    def gui(self):
        return self.network_game("gui", PLATFORM_MODE, [])

    def gui_heg(self):
        """Hegemony is a server setting: an in-memory --config overlay, never written to config.ini."""
        overlay = self.stage_dir("gui_heg") / "hegemony.ini"
        overlay.write_text("[General]\nEnableHegemony=true\n", encoding="utf-8")
        return self.network_game("gui_heg", HEGEMONY_MODE, ["--server-config", overlay])

    def network_game(self, stage, mode, extra):
        directory = self.stage_dir(stage)
        code = run([sys.executable, HERE / "network_runner.py", "--exe-root", ROOT, "--modes", mode,
                    "--runs", 1, "--general", self.general, "--force-general", self.general,
                    "--log-dir", directory, *extra], directory / "runner.log", self.env)
        logs = list(directory.rglob("*.log"))
        evidence = scan_logs(logs, self.general, self.visible_skills)
        summaries = sorted(directory.glob("summary-network-*.csv"))
        rows = list(csv.DictReader(open(summaries[-1], encoding="utf-8-sig"))) if summaries else []
        ok = code == 0 and bool(rows) and all(r.get("ok") in ("True", "true", "1") for r in rows)
        evidence["crashes"] = [
            {"stage": stage, "process": "qsanguosha_server" if "server crashed" in r.get("note", "")
             else "QSanguosha client", "source": "exit code",
             "what": f"{r.get('note', '')} {r.get('exit_name', '')}".strip(), "files": [str(directory)],
             "context": tail(directory / "runner.log")}
            for r in rows if "crash" in r.get("note", "") or "閃退" in r.get("note", "")]
        return game_status(ok, evidence), f"{mode} GUI client over TCP, exit {code}", evidence

    def tui(self):
        directory = self.stage_dir("tui")
        code = run([sys.executable, HERE / "tui_network_smoke.py", "--exe-root", ROOT, "--mode", PLATFORM_MODE,
                    "--seed", self.args.seed, "--artifact-dir", directory / "artifacts",
                    "--force-general", self.general], directory / "runner.log", self.env)
        evidence = scan_logs(list((directory / "artifacts").rglob("*.log")), self.general, self.visible_skills)
        evidence["crashes"] = []
        for summary in (directory / "artifacts").rglob("*-summary.json"):
            lifecycle = json.loads(read(summary) or "{}").get("lifecycle", {})
            for process, key, suffix in (("qsanguosha_tui", "client_exit", "-tui-process.log"),
                                         ("qsanguosha_server", "server_exit", "-server.log")):
                if lifecycle.get(key) is not None and is_crash_code(lifecycle[key]):
                    log = Path(str(summary).replace("-summary.json", suffix))
                    evidence["crashes"].append(exit_crash("tui", process, lifecycle[key], tail(log), [log]))
        return game_status(code == 0, evidence), f"{PLATFORM_MODE} TUI over TCP, exit {code}", evidence

    def android(self):
        if not self.args.android_serial:
            return "NOT RUN", "no --android-serial", {}
        directory = self.stage_dir("android")
        adb = [self.args.adb, "-s", self.args.android_serial]
        port = free_tcp_port()
        marker = directory / "server-autotest.log"
        server_log = directory / "server.log"
        server = spawn([str(self.exe("qsanguosha_server")), "--port", str(port), "--websocket-port", "0",
                        "--game-mode", PLATFORM_MODE, "--seed", str(self.args.seed), "--ai", "on",
                        "--ai-delay", "0", "--operation-timeout", "15", "--autotest-log", str(marker),
                        "--force-general", self.general],
                       resolve_workdir(str(ROOT)), str(server_log), env=self.env)
        remote = f"/data/user/0/{ANDROID_PACKAGE}/files/general-acceptance-{int(time.time())}.json"
        since = subprocess.run(adb + ["shell", "date", "+'%m-%d %H:%M:%S.000'"], capture_output=True,
                               text=True).stdout.strip()
        reversed_port = False
        try:
            if not wait_port(port, 120, proc=server):
                return "FAIL", "server did not listen", {"server_log": str(server_log)}
            if run(adb + ["reverse", f"tcp:{port}", f"tcp:{port}"], directory / "reverse.log", self.env) != 0:
                return "FAIL", "adb reverse failed", {}
            reversed_port = True
            client_args = (f"-connect:127.0.0.1:{port} --auto-robots --network-ui-smoke "
                           f"--network-ui-smoke-result {remote} --network-ui-smoke-timeout-ms 1800000 "
                           f"--network-ui-smoke-stall-ms 600000")
            run(adb + ["shell", "am", "start", "-W", "-n", ANDROID_ACTIVITY, "--es", "applicationArguments",
                       f"'{client_args}'"], directory / "launch.log", self.env)
            deadline = time.time() + 35 * 60
            while time.time() < deadline and not game_over(marker):
                if server.poll() is not None:
                    break
                time.sleep(5)
            # Let the client write its result and exit on its own.
            for _ in range(60):
                pid = subprocess.run(adb + ["shell", "pidof", ANDROID_PACKAGE], capture_output=True, text=True)
                if not pid.stdout.strip():
                    break
                time.sleep(2)
            result_path = directory / "client-result.json"
            pulled = subprocess.run(adb + ["exec-out", "run-as", ANDROID_PACKAGE, "cat",
                                           remote.split(ANDROID_PACKAGE + "/", 1)[1]], capture_output=True)
            result_path.write_bytes(pulled.stdout)
            client = json.loads(pulled.stdout or b"{}") if pulled.stdout.strip().startswith(b"{") else {}
            if not client:
                # Release APKs are not debuggable, so run-as fails; read the logcat markers instead.
                client = logcat_smoke_result(adb, since, directory / "logcat-smoke.txt")
        finally:
            if reversed_port:
                run(adb + ["reverse", "--remove", f"tcp:{port}"], directory / "reverse-remove.log", self.env)
            server_code = server.poll()
            terminate_tree(server)
        evidence = scan_logs([server_log, marker], self.general, self.visible_skills)
        evidence["crashes"] = []
        if server_code is not None and is_crash_code(server_code):
            evidence["crashes"].append(exit_crash("android", "qsanguosha_server", server_code, tail(server_log),
                                                  [server_log]))
        crash_log = directory / "logcat-crash.txt"
        crash_log.write_bytes(subprocess.run(adb + ["logcat", "-b", "crash", "-d", "-T", since],
                                             capture_output=True).stdout)
        lines = read(crash_log).splitlines()
        if any(ANDROID_PACKAGE in line for line in lines):
            evidence["crashes"].append({"stage": "android", "process": ANDROID_PACKAGE, "source": "logcat crash",
                                        "what": next((l for l in lines if "signal" in l or "FATAL" in l), lines[0]),
                                        "files": [str(crash_log)], "context": lines[:80]})
        ok = game_over(marker) and bool(client.get("ok"))
        evidence["client_result"] = {"ok": client.get("ok"), "exit_code": client.get("exit_code"),
                                     "errors": client.get("errors"),
                                     "stages": [s.get("stage") for s in client.get("stages", []) if s.get("ok")]}
        return game_status(ok, evidence), f"{PLATFORM_MODE} Android client via adb reverse", evidence

    def xp(self):
        if not self.args.xp_evidence:
            return "NOT RUN", ("XP runs in the VM per docs/windows-xp-legacy-build.md; pass --xp-evidence "
                               "<dir with server.log/client.log> to grade it"), {}
        logs = list(Path(self.args.xp_evidence).rglob("*.log"))
        evidence = scan_logs(logs, self.general, self.visible_skills)
        text = "\n".join(read(p) for p in logs)
        ok = "GAME_STARTED" in text and "GAME_OVER" in text
        evidence["crashes"] = [dump_crash("xp", dump, [dump, dump.with_suffix(".txt")])
                               for dump in Path(self.args.xp_evidence).rglob("*.dmp")]
        return game_status(ok, evidence), f"graded from {self.args.xp_evidence}", evidence

    def web(self):
        return self.independent_client("web", self.args.web_evidence)

    def sheets(self):
        return self.independent_client("sheets", self.args.sheets_evidence)

    def independent_client(self, stage, source):
        """Grade saved live-client observations; this never starts a browser, tunnel or game."""
        if not source:
            return "NOT RUN", f"no --{stage}-evidence; execute the skill's live-client procedure first", {}
        if self.args.profile != "trustee":
            return "NOT RUN", "saved Trustee playback does not cover full skill/interaction acceptance", {}
        directory = Path(source).resolve()
        required = ["client-observation.json", "client-state.txt", "host.json", "cleanup.json",
                    "process-check.json", "artifact-manifest.json", "server.log", "server-autotest.log"]
        if stage == "sheets":
            required.append("bridge-exit.json")
        missing = [name for name in required if not (directory / name).is_file()]
        if missing:
            return "NOT RUN", "missing evidence: " + ", ".join(missing), {"source": str(directory)}
        def document(name):
            return json.loads((directory / name).read_text(encoding="utf-8-sig"))
        observation = document("client-observation.json")
        host, cleanup = document("host.json"), document("cleanup.json")
        processes, artifacts = document("process-check.json"), document("artifact-manifest.json")
        logs = [directory / "server.log", directory / "server-autotest.log"]
        evidence = scan_logs(logs, self.general, self.visible_skills)
        evidence.update(source=str(directory), validation_source="saved live-client evidence",
                        skill_branch_coverage="NOT RUN", crashes=[])
        issues = []
        expected = {"general": self.general, "seed": self.args.seed, "mode": PLATFORM_MODE}
        if any(observation.get(key) != value or artifacts.get(key) != value for key, value in expected.items()):
            issues.append("general/seed/mode mismatch")
        if (type(observation.get("schema_version")) is not int or observation["schema_version"] != 1
                or observation.get("profile") != "trustee"):
            issues.append("unsupported observation schema/profile")
        if any(observation.get(key) is not True for key in ("trustee_enabled", "runtime_ready", "game_over")):
            issues.append("Trustee/runtime/GAME_OVER was not observed")
        if observation.get("runtime_errors") != []:
            issues.append("runtime errors or missing error capture")
        snapshot = (directory / "client-state.txt").read_bytes()
        text = snapshot.decode("utf-8-sig")
        if hashlib.sha256(snapshot).hexdigest() != observation.get("snapshot_sha256"):
            issues.append("client snapshot hash mismatch")
        owner, name = observation.get("player_id"), observation.get("general_name")
        if not owner or owner not in evidence["forced_seats"] or owner not in text or not name or name not in text:
            issues.append("forced owner/displayed general evidence mismatch")
        if not re.search(r"GAME_OVER|游戏(?:已)?结束|对局结束", text):
            issues.append("client snapshot has no natural end")
        winner = observation.get("winner")
        winner_text = observation.get("winner_text")
        roles = {"lord": "主公", "loyalist": "忠臣", "rebel": "反贼", "renegade": "内奸"}
        tokens = {token for token in re.split(r"[+,]", winner) if token} if isinstance(winner, str) else set()
        if (not tokens or not isinstance(winner_text, str) or not winner_text or winner_text not in text
                or any(token not in winner_text and roles.get(token, token) not in winner_text for token in tokens)):
            issues.append("client winner evidence mismatch")
        native = "\n".join(read(path) for path in logs)
        winners = re.findall(r"(?:game_over[^\r\n]*winner=|\[AUTOTEST\] game over )([^\s]+)", native)
        if not re.search(r"game_started|\[AUTOTEST\] game start", native) or not winners:
            issues.append("server start/end markers missing")
        elif set(re.split(r"[+,]", winners[-1])) != tokens:
            issues.append("server/client winner mismatch")
        arguments = host.get("server_args", [])
        for flag, value in (("--force-general", self.general), ("--seed", str(self.args.seed)),
                            ("--game-mode", PLATFORM_MODE)):
            if flag not in arguments or arguments.index(flag) + 1 >= len(arguments) or arguments[arguments.index(flag) + 1] != value:
                issues.append(f"server {flag} mismatch")
        # Cleanup is independent of GAME_OVER; a dead bridge alone is insufficient.
        owned = host.get("processes", {})
        exits = cleanup.get("processes", {})
        if "server" not in owned or set(owned) != set(exits):
            issues.append("owned process exit inventory mismatch")
        reason = cleanup.get("reason")
        if (not isinstance(reason, str) or not reason.strip()
                or any(word in reason.lower() for word in ("timeout", "unexpected", "forced", "crash"))):
            issues.append("cleanup followed a stop/failure boundary")
        graceful = {"graceful-console", "graceful-gateway-finally", "console-break", "already"}
        for process, pid in owned.items():
            result = exits.get(process, {})
            code = result.get("exit_code")
            if (type(pid) is not int or pid <= 0 or result.get("pid") != pid or result.get("alive") is not False
                    or type(code) is not int or code != 0 or result.get("shutdown") not in graceful):
                issues.append(f"unclean {process} exit")
            if isinstance(code, int) and is_crash_code(code):
                evidence["crashes"].append(exit_crash(stage, process, code, tail(logs[0]), logs))
        ports = host.get("ports", [])
        released = cleanup.get("ports_released", {})
        if not ports or set(released) != {str(port) for port in ports} or any(value is not True for value in released.values()):
            issues.append("owned ports not released")
        for flag in ("--port", "--websocket-port"):
            if (flag not in arguments or arguments.index(flag) + 1 >= len(arguments)
                    or arguments[arguments.index(flag) + 1] not in {str(port) for port in ports}):
                issues.append(f"server {flag} absent from port cleanup")
        host_pid = host.get("host_pid")
        if type(host_pid) is not int or host_pid <= 0:
            issues.append("host process inventory missing")
        expected_pids = set(owned.values()) | {host_pid}
        if stage == "web" and artifacts.get("rules_identity_match") is not True:
            issues.append("native/WASM identity not verified")
        if stage == "sheets":
            bridge = document("bridge-exit.json")
            bridge_code = bridge.get("native_exit_code")
            if (type(bridge_code) is not int or bridge_code != 0 or bridge.get("clean_native_exit") is not True
                    or bridge.get("forced_termination") is not False or "cleanup_error" not in bridge
                    or bridge["cleanup_error"] is not None):
                issues.append("unclean Sheets bridge exit")
            if type(bridge_code) is int and is_crash_code(bridge_code):
                evidence["crashes"].append(exit_crash(stage, "SheetsBridge", bridge_code,
                    files=[directory / "bridge-exit.json"]))
            bridge_pid, helper_pids = observation.get("bridge_pid"), observation.get("helper_pids")
            if (type(bridge_pid) is not int or bridge_pid <= 0 or not isinstance(helper_pids, list)
                    or any(type(pid) is not int or pid <= 0 for pid in helper_pids)):
                issues.append("Sheets bridge/helper inventory missing")
            else:
                expected_pids.update([bridge_pid, *helper_pids])
                if any(pid not in owned.values() for pid in helper_pids):
                    issues.append("Sheets helper exit evidence missing")
            if artifacts.get("scripts_match") is not True or observation.get("https_paired") is not True:
                issues.append("Sheets script/pairing evidence missing")
        if (None in expected_pids or not expected_pids.issubset(set(processes.get("owned_pids", [])))
                or processes.get("alive_owned_pids") != []):
            issues.append("owned orphan check missing or failed")
        evidence.update(winner=winner, issues=issues)
        return game_status(not issues, evidence), ("; ".join(issues) if issues else
            "graded saved Trustee playback; manual branches NOT RUN"), evidence

    def sync(self):
        """Compare L's Lua trees with the H authority by size and SHA-256 (AGENTS.md section 9)."""
        authority = Path(self.args.extensions_authority)
        if not authority.is_dir():
            return "NOT RUN", f"authority {authority} not found", {}

        def digest(path):
            return hashlib.sha256(path.read_bytes()).hexdigest()

        stale, ahead, missing, checked = [], [], [], 0
        held_paths = {}
        for source, target in EXTENSIONS_MAP:
            for root, dirs, files in os.walk(authority / source):
                dirs[:] = [d for d in dirs if d not in SYNC_SKIP_DIRS]
                for name in files:
                    if name == ".stignore" or ".bak" in name:
                        continue
                    held = Path(root) / name
                    local = ROOT / target / held.relative_to(authority / source)
                    checked += 1
                    if not local.is_file():
                        missing.append(local.relative_to(ROOT).as_posix())
                    elif held.stat().st_size != local.stat().st_size or digest(held) != digest(local):
                        # A newer L file is a local edit not yet written back; an older one is stale.
                        newer = local.stat().st_mtime > held.stat().st_mtime
                        (ahead if newer else stale).append(local.relative_to(ROOT).as_posix())
                        held_paths[local.relative_to(ROOT).as_posix()] = (held, local)
        git = ["git", "-C", str(authority)]
        head = subprocess.run(git + ["log", "-1", "--format=%h %s"], capture_output=True, text=True,
                              encoding="utf-8", errors="replace").stdout.strip()
        dirty = [line[3:] for line in subprocess.run(git + ["status", "--porcelain"], capture_output=True, text=True,
                                                     encoding="utf-8", errors="replace").stdout.splitlines()
                 if line.strip()]
        # A newer L file is only "ahead" when H has not moved since: an uncommitted H
        # edit or an H commit after the L edit means both sides changed (merge first).
        diverged = []
        for name in list(ahead):
            held, local = held_paths[name]
            relative = held.relative_to(authority).as_posix()
            committed = subprocess.run(git + ["log", "-1", "--format=%ct", "--", relative], capture_output=True,
                                       text=True, encoding="utf-8", errors="replace").stdout.strip()
            if relative in dirty or (committed.isdigit() and int(committed) > local.stat().st_mtime):
                ahead.remove(name)
                diverged.append(name)
        evidence = {"authority": str(authority), "head": head, "authority_dirty": dirty, "diverged": diverged,
                    "stale_in_l": stale, "l_ahead_of_h": ahead, "missing_in_l": missing, "checked": checked}
        (self.stage_dir("sync") / "sync.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2),
                                                          encoding="utf-8")
        note = f"H {head}" + (f", {len(dirty)} uncommitted in H" if dirty else "")
        if diverged:
            return "FAIL", (f"{len(diverged)} file(s) changed in both L and H; merge onto H, then sync "
                            f"({', '.join(diverged[:5])}); {note}"), evidence
        if stale or missing:
            return "FAIL", (f"L stale vs H: {len(stale)} older, {len(missing)} missing "
                            f"(first: {', '.join((stale + missing)[:5])}); sync H to L; {note}"), evidence
        if ahead:
            return "WARN", (f"{len(ahead)} L file(s) newer than H, not written back "
                            f"({', '.join(ahead[:5])}); {note}"), evidence
        return "PASS", f"{checked} files match H; {note}", evidence

    # -- driver ---------------------------------------------------------------

    def run(self, stages):
        if set(stages) & LUA_STAGES and not self.args.skip_sync_check:
            self.results["sync"] = self.record("sync")
            print(f"[{self.results['sync']['status']}] sync: {self.results['sync']['detail']}", flush=True)
        stale = self.results.get("sync", {}).get("status") == "FAIL"
        for name in stages:
            if stale and name in LUA_STAGES:
                # Results on stale Lua are not acceptance evidence; sync H to L first.
                self.results[name] = {"status": "BLOCKED", "detail": "L Lua trees stale vs H (see sync)"}
                print(f"[BLOCKED] {name}: L Lua trees stale vs H", flush=True)
                continue
            if name in UNAVAILABLE_STAGES:
                self.results[name] = {"status": "NOT RUN", "detail": "environment unavailable on this host"}
                continue
            if self.args.profile == "full" and name not in INDEPENDENT_STAGES + ["describe"] and not self.info and "describe" not in self.results:
                self.results["describe"] = self.record("describe")
            self.results[name] = self.record(name)
            print(f"[{self.results[name]['status']}] {name}: {self.results[name]['detail']}", flush=True)
        summary = {"general": self.general, "seed": self.args.seed, "platform_mode": PLATFORM_MODE,
                   "profile": self.args.profile,
                   "skill_branch_coverage": "NOT RUN" if self.args.profile == "trustee" else "see skill stage",
                   "stages": self.results, "crashes": self.crashes}
        (self.out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        (self.out / "crashes.json").write_text(json.dumps(self.crashes, ensure_ascii=False, indent=2),
                                               encoding="utf-8")
        lines = [f"# {self.general} acceptance (seed {self.args.seed}, {self.args.profile})", "",
                 "| stage | status | detail |", "|---|---|---|"]
        lines += [f"| {k} | {v['status']} | {v['detail']} |" for k, v in self.results.items()]
        if self.args.profile == "trustee":
            lines += ["", "Skill branches: NOT RUN (Trustee client playback)."]
        lines += ["", "## Crashes", ""]
        if not self.crashes:
            lines.append("None.")
        else:
            lines += ["| # | stage | process | source | what |", "|---|---|---|---|---|"]
            lines += [f"| {i} | {c['stage']} | {c['process']} | {c['source']} | {c['what']} |"
                      for i, c in enumerate(self.crashes, 1)]
            for i, c in enumerate(self.crashes, 1):
                lines += ["", f"### {i}. {c['stage']} / {c['process']}", ""]
                lines += [f"- {f}" for f in c["files"]]
                lines += ["", "```", *c["context"], "```"]
        (self.out / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"summary: {self.out / 'summary.md'} ({len(self.crashes)} crashes)")
        return 1 if any(v["status"] in ("FAIL", "CRASH") for v in self.results.values()) else 0

    def collect_dumps(self, stage, before):
        """Move the dumps this stage left in dmp/ into crashes/<stage>/ of the run directory."""
        new = sorted(dump_snapshot() - before)
        crashes = []
        for dump in (p for p in new if p.suffix.lower() == ".dmp"):
            target = self.out / "crashes" / stage
            target.mkdir(parents=True, exist_ok=True)
            moved = [Path(shutil.move(str(p), str(target / p.name))) for p in new if p.name.startswith(dump.stem)]
            crashes.append(dump_crash(stage, target / dump.name, moved))
        return crashes

    def record(self, name):
        started = time.time()
        before = dump_snapshot() if name not in INDEPENDENT_STAGES else set()
        try:
            status, detail, evidence = getattr(self, name)()
        except Exception as error:  # a broken stage must not hide the other platforms
            status, detail, evidence = "FAIL", f"{type(error).__name__}: {error}", {}
        crashes = evidence.pop("crashes", [])
        dumps = self.collect_dumps(name, before) if name not in INDEPENDENT_STAGES else []
        if len(crashes) == 1 and len(dumps) == 1:  # the same crash seen twice
            exited, dump = crashes[0], dumps[0]
            crashes = [{**dump, "process": exited["process"], "source": "exit code + minidump",
                        "what": f"{exited['what']}; {dump['what']}", "files": exited["files"] + dump["files"],
                        "context": dump["context"] + (["", "-- log tail --", *exited["context"]]
                                                      if exited["context"] else [])}]
        else:
            crashes += dumps
        if crashes:
            self.crashes += crashes
            status = "CRASH"
            detail += f"; {len(crashes)} crash(es), see Crashes"
        return {"status": status, "detail": detail, "seconds": round(time.time() - started), "evidence": evidence}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--general", required=True)
    parser.add_argument("--seed", type=int, required=True, help="unsigned seed, e.g. yyyyMMdd")
    parser.add_argument("--profile", choices=("trustee", "full"), default="trustee",
                        help="trustee: client playback (default gui,tui); full: skill/UI/all modes")
    parser.add_argument("--stages",
                        help="comma list from: " + ", ".join(LOCAL_STAGES + DEVICE_STAGES + INDEPENDENT_STAGES + UNAVAILABLE_STAGES))
    parser.add_argument("--cases", help="skill scenario case directory (scratchpad)")
    parser.add_argument("--qt-bin", help="Qt bin directory for every child process")
    parser.add_argument("--jobs", type=int, default=4, help="parallel skill cases / headless modes")
    parser.add_argument("--exclude-modes", default="", help="headless modes to skip, comma separated")
    parser.add_argument("--headless-minutes", type=float, default=60,
                        help="headless matrix budget in minutes (0 = unlimited); unfinished modes are reported")
    parser.add_argument("--extensions-authority", default=EXTENSIONS_AUTHORITY,
                        help="H working tree that L's lua/ai, extensions and lua must match")
    parser.add_argument("--skip-sync-check", action="store_true",
                        help="skip the H-to-L Lua freshness preflight")
    parser.add_argument("--out-dir")
    parser.add_argument("--android-serial", help="adb serial of the verified device")
    parser.add_argument("--adb", default=str(Path(os.environ.get("LOCALAPPDATA", "")) /
                                             "Android/Sdk/platform-tools/adb.exe"))
    parser.add_argument("--xp-evidence", help="directory holding the XP VM run's server/client logs")
    parser.add_argument("--web-evidence", help="saved live Web Trustee evidence directory (read-only grading)")
    parser.add_argument("--sheets-evidence", help="saved live Sheets Trustee evidence directory (read-only grading)")
    args = parser.parse_args()
    # Explicit client selection must not expand into unrelated platforms or manual skill cases.
    selected = args.stages if args.stages is not None else ",".join(
        TRUSTEE_STAGES if args.profile == "trustee" else LOCAL_STAGES)
    stages = [s.strip() for s in selected.split(",") if s.strip()]
    unknown = set(stages) - set(LOCAL_STAGES + DEVICE_STAGES + INDEPENDENT_STAGES + UNAVAILABLE_STAGES)
    if unknown:
        parser.error(f"unknown stages: {', '.join(sorted(unknown))}")
    if args.profile == "trustee" and set(stages) & {"skill", "ui"}:
        parser.error("skill/ui stages require --profile full; trustee does not cover skill branches")
    sys.exit(Acceptance(args).run(stages))


if __name__ == "__main__":
    main()
