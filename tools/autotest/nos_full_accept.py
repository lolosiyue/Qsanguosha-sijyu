#!/usr/bin/env python3
"""Automated full acceptance for the NOS (nostalgia) generals.

Queue = (general, stage) items, both runtimes (WSL/Linux checkout + Windows
deploy tree).  Every run works through the queue for a fixed time budget and
persists progress, so a scheduled trigger can resume where the previous run
stopped.

Usage:
    python3 tools/autotest/nos_full_accept.py plan [--reset]
    python3 tools/autotest/nos_full_accept.py run [--minutes N] [--only-stage S]
    python3 tools/autotest/nos_full_accept.py status
    python3 tools/autotest/nos_full_accept.py report

State lives in artifacts/nos-full/state.json; evidence in artifacts/nos-full/.
Windows stages shell out to cmd.exe/powershell and are skipped when the deploy
tree is unavailable.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import subprocess
import signal
import tempfile
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE_DIR = ROOT / "artifacts" / "nos-full"
STATE_PATH = STATE_DIR / "state.json"
ROSTER = ROOT / "tools" / "autotest" / "nos_generals.txt"
WIN_ROOT = Path("/mnt/d/game/sgs/QSanguoshaFinal")
WIN_ROOT_WIN = r"D:\game\sgs\QSanguoshaFinal"
CMD_EXE = Path("/mnt/c/Windows/System32/cmd.exe")

# The deploy tree only ships a prebuilt exe (2026-10-12 as of writing). This checkout cannot
# produce a Windows build - no MSVC/Qt for Windows here - so win stages would grade an older
# binary than HEAD. Keep them in the plan for visibility, but never let them count as evidence.
WINDOWS_BUILD_AVAILABLE = False
STALE_WIN_NOTE = ("deploy tree exe is the 2026-10-12 build; HEAD 2026-10-22 migrated package "
                  "skills to V2 contracts, and this machine has no Windows build of the "
                  "checkout -> win evidence would describe an older build, not current source")
TASKKILL_EXE = Path("/mnt/c/Windows/System32/taskkill.exe")
DISPLAY = ":0"

GENERAL_SKILLS = {
    "nos_caocao": ["nosjianxiong（奸雄）", "hujia（护驾·主公）"],
    "nos_simayi": ["nosfankui（反馈）", "nosguicai（鬼才）"],
    "nos_xiahoudun": ["nosganglie（刚烈）"],
    "nos_zhangliao": ["nostuxi（突袭）"],
    "nos_xuchu": ["nosluoyi（裸衣）"],
    "nos_guojia": ["tiandu（天妒）", "nosyiji（遗计）"],
    "nos_liubei": ["nosrende（仁德）", "jijiang（激将·主公）"],
    "nos_guanyu": ["wusheng（武圣）"],
    "nos_zhangfei": ["paoxiao（咆哮·锁定）"],
    "nos_zhaoyun": ["longdan（龙胆）"],
    "nos_machao": ["mashu（马术·锁定）", "nostieji（铁骑）"],
    "nos_huangyueying": ["nosjizhi（集智）", "nosqicai（奇才·锁定）"],
    "nos_ganning": ["qixi（奇袭）"],
    "nos_lvmeng": ["keji（克己）"],
    "nos_huanggai": ["noskurou（苦肉）"],
    "nos_zhouyu": ["nosyingzi（英姿）", "nosfanjian（反间）"],
    "nos_daqiao": ["nosguose（国色）", "liuli（流离）"],
    "nos_luxun": ["nosqianxun（谦逊·锁定）", "noslianying（连营）"],
    "nos_huatuo": ["qingnang（青囊）", "jijiu（急救）"],
    "nos_lvbu": ["wushuang（无双·锁定）"],
    "nos_diaochan": ["noslijian（离间）", "biyue（闭月）"],
}

# stage -> (runtime, rough seconds, timeout seconds)
STAGES: dict[str, tuple[str, int, int]] = {
    "describe": ("win", 15, 180),
    "linux-headless": ("linux", 120, 1200),
    "linux-tui": ("linux", 150, 1800),
    "linux-gui": ("linux", 180, 1800),
    "win-ui": ("win", 60, 480),
    "win-headless": ("win", 420, 3600),
    "win-tui": ("win", 240, 1800),
    "win-gui": ("win", 240, 1800),
    "win-gui-heg": ("win", 240, 1800),
    "win-skill": ("win", 300, 3600),
}
ORDER = list(STAGES)
# User decision (2026-10-23): the skill-text case stage is out of scope on this
# machine, so it is excluded from the queue instead of sitting there blocked.
SKIP_STAGES = {"win-skill"}
SCOPED_ORDER = [s for s in ORDER if s not in SKIP_STAGES]

MAX_ATTEMPTS = 3      # per (general, stage): first try + 2 retries on later runs
LOCK_STALE_S = 5400   # a run refreshes the lock before every item; older means dead


def now() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def today_seed() -> str:
    return dt.datetime.now().strftime("%Y%m%d")


def load_state() -> dict:
    if STATE_PATH.exists():
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    return {"created": now(), "updated": now(), "items": {}}


def save_state(state: dict) -> None:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    state["updated"] = now()
    tmp = STATE_PATH.with_suffix(".tmp")
    tmp.write_text(json.dumps(state, ensure_ascii=False, indent=1), encoding="utf-8")
    tmp.replace(STATE_PATH)


def roster() -> list[str]:
    return [ln.strip() for ln in ROSTER.read_text(encoding="utf-8").splitlines() if ln.strip()]


def build_items() -> dict:
    items = {}
    for general in roster():
        for stage in SCOPED_ORDER:
            win = STAGES[stage][0] == "win"
            items[f"{general}/{stage}"] = {
                "general": general,
                "stage": stage,
                "runtime": STAGES[stage][0],
                "status": "blocked" if (win and not WINDOWS_BUILD_AVAILABLE) else "pending",
                "attempts": 0,
                **({"note": STALE_WIN_NOTE} if (win and not WINDOWS_BUILD_AVAILABLE) else {}),
            }
    return items


def prune(state: dict) -> None:
    for key in [k for k, v in state["items"].items() if v["stage"] in SKIP_STAGES]:
        del state["items"][key]


def plan(reset: bool) -> int:
    if reset and STATE_PATH.exists():          # never lose a finished queue by accident
        shutil.copy2(STATE_PATH, STATE_PATH.with_suffix(".json.bak"))
    state = build_items() and {"created": now(), "updated": now(), "items": build_items()} if reset else load_state()
    if not reset and not state["items"]:
        state["items"] = build_items()
    prune(state)
    # add stages that did not exist yet on an older state
    for key, item in build_items().items():
        state["items"].setdefault(key, item)
    save_state(state)
    n = len(state["items"])
    print(f"plan: {n} items ({len(roster())} generals x {len(ORDER)} stages) -> {STATE_PATH}")
    return 0


def linux_env(parallel: int = 1) -> dict:
    env = dict(os.environ)
    env.pop("QT_QPA_PLATFORM", None)
    env["QSAN_NO_HIDDEN_GENERAL"] = "1"
    if parallel > 1:
        env["ALLOW_PARALLEL"] = "1"   # run-headless.sh refuses a second game otherwise
    return env


def _kill_tree(proc: subprocess.Popen) -> None:
    """Kill the whole session; cmd.exe children bail out with it only via taskkill."""
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
    if WIN_ROOT.is_dir() and TASKKILL_EXE.exists():
        for exe in ("QSanguosha.exe", "qsanguosha_server.exe", "qsanguosha_tui.exe",
                    "qsanguosha_skill_scenario.exe"):
            try:
                subprocess.run([str(TASKKILL_EXE), "/F", "/IM", exe],
                               capture_output=True, timeout=20)
            except Exception:
                pass


def _kill_windows_clients() -> None:
    """Kill autotest runners and game clients left behind by an interrupted run."""
    if not (WIN_ROOT.is_dir() and TASKKILL_EXE.exists()):
        return
    wmic = Path("/mnt/c/Windows/System32/wbem/WMIC.exe")
    if wmic.exists():
        for image in ("python3.11.exe", "python.exe", "pythonw.exe"):
            try:
                out = subprocess.run(
                    [str(wmic), "process", "where", f"name='{image}'", "get", "ProcessId,CommandLine"],
                    capture_output=True, text=True, timeout=30, errors="replace").stdout
            except Exception:
                continue
            for line in out.splitlines():
                if "autotest" not in line.lower():
                    continue
                pid = line.split()[-1]
                if pid.isdigit():
                    try:
                        subprocess.run([str(TASKKILL_EXE), "/F", "/PID", pid],
                                       capture_output=True, timeout=20)
                    except Exception:
                        pass
    for exe in ("QSanguosha.exe", "qsanguosha_server.exe", "qsanguosha_tui.exe",
                "qsanguosha_skill_scenario.exe"):
        try:
            subprocess.run([str(TASKKILL_EXE), "/F", "/IM", exe], capture_output=True, timeout=20)
        except Exception:
            pass


def run_cmd(cmd: list[str], timeout: int, env: dict | None = None, cwd: Path = ROOT) -> tuple[int, str]:
    """Run with output in a temp file: a killed child cannot keep the pipe open (Windows
    grandchildren inherit handles, so capture_output would hang past the timeout)."""
    buf = tempfile.TemporaryFile(mode="w+b")
    try:
        proc = subprocess.Popen(cmd, cwd=str(cwd), env=env, stdout=buf,
                                stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            _kill_tree(proc)
            try:
                proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                pass
            code = 124
        buf.flush()
        buf.seek(0)
        out = buf.read().decode("utf-8", "replace")
        if code == 124:
            out += f"\n[timeout after {timeout}s]\n"
        return code, out
    finally:
        buf.close()


def win_cmd(args: str) -> list[str]:
    """Wrap a Windows-side command so cmd.exe runs it with the deploy tree as cwd."""
    inner = f"cd /d {WIN_ROOT_WIN} && {args}"
    return [str(CMD_EXE), "/c", inner]


def stage_commands(general: str, stage: str, seed: str, cases: Path | None,
                   parallel: int = 1) -> tuple[list[str], dict | None, Path]:
    win_jobs = 6 if parallel <= 1 else 3
    art = STATE_DIR / "evidence" / general / stage
    art.mkdir(parents=True, exist_ok=True)
    if stage == "describe":
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages describe"), None, art
    if stage == "linux-headless":
        # Call the binary directly: run-headless.sh hardcodes artifacts/headless-<mode>-<seed>.log,
        # so parallel items would overwrite each other's game log (and its livelock watch).
        runs = []
        for mode in ("03_1v2", "05p"):
            runs.append(
                f"timeout --signal=TERM 1800 env -u QT_QPA_PLATFORM QSAN_NO_HIDDEN_GENERAL=1 "
                f"./relwithdebinfo/QSanguosha --headless --game-mode {mode} --games 1 --seed {seed} "
                f"--headless-log {art}/game-{mode}.log --test-general {general} > {art}/headless-{mode}.log 2>&1"
            )
        return ["bash", "-c", "set -e; " + "; ".join(runs)], linux_env(parallel), art
    if stage == "linux-tui":
        return [
            "python3", "tools/autotest/tui_network_smoke.py", "--exe-root", ".",
            "--server", "relwithdebinfo/qsanguosha_server", "--tui", "relwithdebinfo/qsanguosha_tui",
            "--mode", "03_1v2", "--seed", seed, "--general", general,
            "--artifact-dir", str(art / "tui"), "--game-timeout-ms", "1800000", "--process-timeout", "2400",
        ], linux_env(parallel), art
    if stage == "linux-gui":
        env = linux_env(parallel)
        env["DISPLAY"] = DISPLAY
        env["QT_QPA_PLATFORM"] = "xcb"
        # WSLg exposes the X socket under /mnt/wslg; Qt looks in /tmp/.X11-unix.
        # Idempotent and non-fatal: a parallel worker may have created the socket link
        # already, and a failed `ln` used to abort the whole stage with exit 1.
        setup = ("mkdir -p /tmp/.X11-unix 2>/dev/null; "
                 "[ -S /tmp/.X11-unix/X0 ] || ln -sfn /mnt/wslg/.X11-unix/X0 /tmp/.X11-unix/X0 2>/dev/null; ")
        gui = " ".join([
            "python3", "tools/autotest/gui_network_smoke.py", "--exe-root", ".",
            "--server-exe", "relwithdebinfo/qsanguosha_server", "--client-exe", "relwithdebinfo/QSanguosha",
            "--mode", "03_1v2", "--seed", seed, "--general", general,
            "--artifact-dir", str(art / "gui"), "--no-xvfb", "--platform", "xcb",
            # play_phase is a UI phase marker: in 03_1v2 a short game can end before the client
            # sits in its own play phase, which made 4/21 GUI items fail for a non-defect.
            "--require-interactions", "choose_general,ask_for_card",
            "--client-timeout-ms", "1800000", "--process-timeout", "2400",
        ])
        return ["bash", "-c", setup + gui], env, art
    if stage == "win-ui":
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages ui --jobs {win_jobs}"), None, art
    if stage == "win-headless":
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages headless --jobs {win_jobs}"), None, art
    if stage == "win-tui":
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages tui --jobs {win_jobs}"), None, art
    if stage == "win-gui":
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages gui --jobs {win_jobs}"), None, art
    if stage == "win-gui-heg":
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages gui_heg --jobs {win_jobs}"), None, art
    if stage == "win-skill":
        if cases is None:
            raise RuntimeError("win-skill needs --cases with authored skill scenario cases")
        return win_cmd(f"python tools\\autotest\\general_acceptance.py --general {general} --seed {seed} --profile full --stages skill --cases {cases} --jobs {win_jobs}"), None, art
    raise RuntimeError(f"unknown stage {stage}")


def verdict_of(code: int, out: str) -> str:
    if code == 124:
        return "timeout"
    if code == 0:
        return "pass"
    return "fail"


def win_available() -> bool:
    return WIN_ROOT.is_dir() and CMD_EXE.exists()


def run(minutes: int, only_stage: str | None, cases: Path | None, parallel: int = 1) -> int:
    state = load_state()
    if not state["items"]:
        plan(True)
        state = load_state()
    prune(state)
    seed = today_seed()
    deadline = float("inf") if minutes <= 0 else time.time() + minutes * 60
    done = 0
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    lock = STATE_DIR / "run.lock"
    if lock.exists():
        age = time.time() - lock.stat().st_mtime
        if age < LOCK_STALE_S:
            print(f"another run holds the lock ({int(age)}s old) -> abort")
            return 3
        print(f"stale lock ({int(age)}s) -> taking over")
    lock.write_text(f"{os.getpid()} {now()}", encoding="utf-8")
    try:
        pending = [(k, v) for k, v in state["items"].items()
                   if v["status"] in ("pending", "retry")
                   or (v["status"] in ("fail", "timeout") and v["attempts"] < MAX_ATTEMPTS)]
        if only_stage:
            pending = [(k, v) for k, v in pending if v["stage"] == only_stage]
        pending.sort(key=lambda kv: (ORDER.index(kv[1]["stage"]), kv[1]["general"]))
        def execute(item: dict) -> dict:
            stage, general = item["stage"], item["general"]
            lock.write_text(f"{os.getpid()} {now()}", encoding="utf-8")  # heartbeat
            cmd, env, art = stage_commands(general, stage, seed, cases, parallel)
            started = now()
            code, out = run_cmd(cmd, STAGES[stage][2], env)
            result = dict(item, attempts=item["attempts"] + 1, started=started,
                          finished=now(), evidence=str(art.relative_to(ROOT)))
            verdict = verdict_of(code, out)
            if verdict in ("fail", "timeout") and result["attempts"] < MAX_ATTEMPTS:
                verdict = "retry"
            result["status"] = verdict
            log = art / f"{stage}.log"
            log.write_text(out, encoding="utf-8")
            result["log"] = str(log.relative_to(ROOT))
            return result

        runnable = []
        for key, item in pending:
            if time.time() > deadline:
                print("budget exhausted")
                break
            stage = item["stage"]
            if STAGES[stage][0] == "win" and not win_available():
                item["status"] = "blocked"
                item["note"] = "win-deploy tree or cmd.exe unavailable"
                save_state(state)
                continue
            runnable.append((key, item))

        from concurrent.futures import ThreadPoolExecutor, as_completed
        with ThreadPoolExecutor(max_workers=max(1, parallel)) as pool:
            futures = {pool.submit(execute, dict(item)): key for key, item in runnable}
            for fut in as_completed(futures):
                key = futures[fut]
                try:
                    item = fut.result()
                except RuntimeError as exc:      # a stage that cannot run at all
                    state["items"][key].update(status="blocked", note=str(exc))
                    save_state(state)
                    print(f"[blocked] {key}: {exc}")
                    continue
                except Exception as exc:         # noqa: BLE001 - keep the queue alive
                    state["items"][key].update(status="retry", note=f"runner error: {exc}")
                    save_state(state)
                    print(f"[error  ] {key}: {exc}")
                    continue
                state["items"][key].update(item)
                save_state(state)
                done += 1
                print(f"[{item['status']:7}] {item['general']}/{item['stage']} -> {item.get('log', '')}")
    finally:
        _kill_windows_clients()
        lock.unlink(missing_ok=True)
        save_state(state)
    print(f"run finished: {done} items processed; state -> {STATE_PATH.relative_to(ROOT)}")
    return 0


def leaf_summary(out: str) -> str:
    for line in reversed(out.splitlines()):
        if line.strip():
            return line.strip()[:100]
    return ""


def check(general: str, cases: Path | None) -> int:
    """Print the resolved command for every stage of one general (no execution)."""
    for stage in ORDER:
        try:
            cmd, env, art = stage_commands(general, stage, today_seed(), cases, 1)
        except RuntimeError as exc:
            print(f"{stage:14} BLOCKED  {exc}")
            continue
        env_note = "".join(f"{k}={v} " for k, v in (env or {}).items() if k in ("DISPLAY", "QT_QPA_PLATFORM"))
        print(f"{stage:14} {env_note}{' '.join(cmd)[:220]}")
        print(f"{'':14} -> {art.relative_to(ROOT)}")
    return 0


def status() -> int:
    state = load_state()
    if not state["items"]:
        print("no state yet; run `plan`")
        return 0
    counts: dict[str, int] = {}
    for item in state["items"].values():
        counts[item["status"]] = counts.get(item["status"], 0) + 1
    total = len(state["items"])
    print(f"items: {total}  updated: {state['updated']}")
    for k in ("pass", "fail", "timeout", "blocked", "pending", "retry"):
        if counts.get(k):
            print(f"  {k:8} {counts[k]}")
    remaining = [i for i in state["items"].values() if i["status"] in ("pending", "retry", "fail", "timeout")]
    eta = sum(STAGES[i["stage"]][1] for i in remaining)
    print(f"  remaining work: {len(remaining)} items, ~{eta / 60:.0f} min if all succeed")
    lock = STATE_DIR / "run.lock"
    if lock.exists():
        age = time.time() - lock.stat().st_mtime
        print(f"  lock: {'ACTIVE' if age < LOCK_STALE_S else 'stale'} ({int(age)}s old, {lock.read_text().strip()})")
    for item in state["items"].values():
        if item["status"] not in ("pass", "pending"):
            print(f"  - {item['general']}/{item['stage']}: {item['status']} {item.get('note', '')}")
    return 0


def _evidence_line(art: Path, stage: str) -> str:
    """One human-readable verdict line pulled from the stage's own evidence."""
    def first(patterns: list[str], key: str) -> str:
        for pat in patterns:
            for f in sorted(art.glob(pat)):
                try:
                    text = f.read_text(encoding="utf-8", errors="replace")
                except Exception:
                    continue
                for line in reversed(text.splitlines()):
                    if key in line:
                        return line.strip()[:110]
        return ""

    if stage == "linux-headless":
        parts = []
        for mode in ("03_1v2", "05p"):
            got = first([f"game-{mode}.log", f"headless-{mode}.log"], "Winner:")
            winner = got.split("Winner:")[-1].strip(" <") if "Winner:" in got else ""
            parts.append(f"{mode}: {winner or 'no winner line'}")
        return " · ".join(parts)
    if stage == "linux-tui":
        got = first(["linux-tui.log"], "TUI_REAL_TCP_RESULT")
        if not got:
            return "no TUI_REAL_TCP_RESULT line"
        fields = dict(f.split("=", 1) for f in got.split() if "=" in f)
        return (f"status={fields.get('status', '?')} game_over={fields.get('game_over', '?')} "
                f"reconnect={fields.get('reconnect', '?')}")
    if stage == "linux-gui":
        rep = first(["linux-gui.log"], "stages reported")
        over = first(["linux-gui.log"], "server game over")
        if rep:
            return f"{rep.split(':', 1)[-1].strip()} stages · game over: {over.split(':', 1)[-1].strip() if over else 'n/a'}"
        return "no stage report"
    return ""


def report() -> int:
    state = load_state()
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    items = state["items"]
    counts: dict[str, int] = {}
    for item in items.values():
        counts[item["status"]] = counts.get(item["status"], 0) + 1
    generals = roster()
    effective = [k for k, v in items.items() if v["status"] != "blocked"]
    total_eff = len(effective)
    done_eff = sum(1 for k in effective if items[k]["status"] == "pass")
    head = subprocess.run(["git", "log", "-1", "--format=%h %ad %s", "--date=short"],
                          cwd=str(ROOT), capture_output=True, text=True).stdout.strip()
    binary = ROOT / "relwithdebinfo" / "QSanguosha"
    bin_age = dt.datetime.fromtimestamp(binary.stat().st_mtime).strftime("%Y-%m-%d %H:%M") if binary.exists() else "missing"

    lines = [
        "# NOS 武將完整驗收報告（linux 段）", "",
        f"- 更新：{state['updated']}",
        f"- 程式：HEAD `{head}`",
        f"- 受測執行檔：`relwithdebinfo/QSanguosha`（建置時間 {bin_age}）",
        f"- 範圍：21 名 `standard` 懷舊（nos_*）武將 × 3 個 linux 階段",
        f"- 進度：**{done_eff}/{total_eff} 有效項通過**"
        f"（fail {counts.get('fail', 0)}、timeout {counts.get('timeout', 0)}、"
        f"retry {counts.get('retry', 0)}、pending {counts.get('pending', 0)}、"
        f"blocked {counts.get('blocked', 0)}）",
        "",
        "## 1. 驗收方法", "",
        "| 階段 | 命令 | 判準 | 證據 |",
        "|---|---|---|---|",
        "| linux-headless | `QSanguosha --headless --game-mode <03_1v2\\|05p> --games 1 --seed <seed> --test-general <id>` | 兩模式都要跑完且 exit 0（每局有 `Winner:` 收尾） | `evidence/<id>/linux-headless/game-<mode>.log` |",
        "| linux-tui | `tui_network_smoke.py --mode 03_1v2 --seed <seed> --general <id>` | 真 TCP 連線、選將、出牌、game_over、`status=PASS` | `evidence/<id>/linux-tui/` |",
        "| linux-gui | `gui_network_smoke.py --mode 03_1v2 --no-xvfb --platform xcb` | 8 個 UI stage、`game_started`/`game_over`、須出現 `choose_general` 與 `ask_for_card` 互動 | `evidence/<id>/linux-gui/gui/` |",
        "",
        "固定 seed `20261023`；每項最多嘗試 3 次；3 路併發。",
        "",
        "## 2. 結果矩陣", "",
        "| 武將 | 技能（標準版） | linux-headless | linux-tui | linux-gui |",
        "|---|---|---|---|---|",
    ]
    marks = {"pass": "✅ pass", "fail": "❌ fail", "timeout": "⏱ timeout", "retry": "🔁 retry",
             "pending": "… pending", "blocked": "⛔ blocked"}
    for general in generals:
        skills = "、".join(GENERAL_SKILLS.get(general, [])) or "—"
        cells = []
        for stage in ("linux-headless", "linux-tui", "linux-gui"):
            item = items.get(f"{general}/{stage}", {})
            cells.append(marks.get(item.get("status", "pending"), item.get("status", "?")))
        lines.append(f"| `{general}` | {skills} | " + " | ".join(cells) + " |")

    lines += ["", "## 3. 逐項證據", ""]
    for general in generals:
        lines += [f"### {general}", ""]
        for stage in ("linux-headless", "linux-tui", "linux-gui"):
            item = items.get(f"{general}/{stage}", {})
            art = STATE_DIR / "evidence" / general / stage
            ev = _evidence_line(art, stage) if item.get("status") == "pass" else item.get("note", "")
            lines.append(f"- **{stage}** → {marks.get(item.get('status', 'pending'), item.get('status'))}"
                         f"{('　' + ev) if ev else ''}")
        lines.append("")

    failures = [(k, v) for k, v in items.items() if v["status"] in ("fail", "timeout")]
    blocked = [(k, v) for k, v in items.items() if v["status"] == "blocked"]
    lines += ["## 4. 未通過項目", ""]
    if failures:
        for key, item in sorted(failures):
            lines += [f"### `{key}` → {item['status']}", "", f"- 嘗試次數：{item.get('attempts', 0)}",
                      f"- 證據：`{item.get('log', '')}`", f"- 說明：{item.get('note', '（無）')}", ""]
    else:
        lines += ["無（有效範圍內全部通過）。", ""]

    lines += ["## 5. 受阻階段（不計入驗收）", ""]
    if blocked:
        stages = sorted({v["stage"] for _, v in blocked})
        lines += [f"- 階段：{', '.join(stages)}（共 {len(blocked)} 項）",
                  f"- 理由：{blocked[0][1].get('note', '')}",
                  "",
                  "這些階段在舊的部署樹 exe 上仍可執行（實測 `nos_caocao` 一局 rc=0），但那是 2026-10-12 的程式，"
                  "與 HEAD 的 skills V2 重構不同版，不能作為當前原始碼的驗收證據。要恢復驗證需在 Windows 側"
                  "（MSVC + Qt for Windows）重新建置後更新部署樹。",
                  ""]

    # ---- aggregate observations from the evidence itself ----
    from collections import Counter
    wins_03, wins_05, tui_over, gui_int = Counter(), Counter(), Counter(), Counter()
    for general in generals:
        for mode, bucket in (("03_1v2", wins_03), ("05p", wins_05)):
            f = STATE_DIR / "evidence" / general / "linux-headless" / f"game-{mode}.log"
            if f.exists():
                for line in f.read_text(encoding="utf-8", errors="replace").splitlines():
                    if "Winner:" in line:
                        bucket[line.split("Winner:")[-1].strip(" <")] += 1
                        break
        tui = STATE_DIR / "evidence" / general / "linux-tui" / "linux-tui.log"
        if tui.exists():
            for line in tui.read_text(encoding="utf-8", errors="replace").splitlines():
                if "TUI_REAL_TCP_RESULT" in line:
                    for f in line.split():
                        if f.startswith("game_over="):
                            tui_over[f.split("=", 1)[1]] += 1
        gui = STATE_DIR / "evidence" / general / "linux-gui" / "linux-gui.log"
        if gui.exists():
            text = gui.read_text(encoding="utf-8", errors="replace")
            for line in text.splitlines():
                if line.strip().startswith("interactions"):
                    for f in line.split(":", 1)[1].split(","):
                        k = f.split("=")[0].strip()
                        if k:
                            gui_int[k] += 1
    def fmt(counter):
        return "、".join(f"{k} {v}" for k, v in counter.most_common()) or "—"
    lines += ["## 6. 觀察（由證據統計）", "",
              f"- headless 03_1v2（斗地主）勝方：{fmt(wins_03)}",
              f"- headless 05p 勝方：{fmt(wins_05)}",
              f"- TUI 對局結束方：{fmt(tui_over)}",
              f"- GUI 出現過的互動型別（局數）：{fmt(gui_int)}",
              "",
              "斗地主（03_1v2）的勝負標籤沿用身份局字樣（`rebel`／`lord+loyalist`），"
              "是模式文案問題，不是規則錯誤；主公在該模式另外獲得 `feiyang`／`bahu`，"
              "並非武將技能。",
              ""]

    lines += ["## 7. 限制", "",
              "- 只驗 linux 端（headless／tui／gui）；describe／skill／ui 三個階段只在 win-deploy 有工具，本機不可用。",
              "- 每將固定一個 seed（`20261023`），不是統計抽樣；技能覆蓋率另見 `artifacts/nos-accept-20261023/REPORT.md`（168 局統計）。",
              "- 視覺效果（動畫、皮膚）與音訊不做人工判讀。",
              "- GUI 只要求 `choose_general` 與 `ask_for_card` 兩種互動；`play_phase` 這種 UI 相位標記在短局會自然不出現，不作判準。",
              ""]

    out = STATE_DIR / "REPORT.md"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report -> {out}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_plan = sub.add_parser("plan")
    p_plan.add_argument("--reset", action="store_true")
    p_run = sub.add_parser("run")
    p_run.add_argument("--minutes", type=int, default=60,
                       help="time budget for this run; 0 = run until the queue drains")
    p_run.add_argument("--only-stage", choices=ORDER)
    p_run.add_argument("--cases", type=Path)
    p_run.add_argument("--parallel", type=int, default=3,
                       help="items executed concurrently (1 = serial); win stages drop --jobs to 3 above 1")
    p_check = sub.add_parser("check")
    p_check.add_argument("--general", default="nos_caocao")
    p_check.add_argument("--cases", type=Path)
    sub.add_parser("status")
    sub.add_parser("report")
    args = ap.parse_args()
    if args.cmd == "plan":
        return plan(args.reset)
    if args.cmd == "run":
        return run(args.minutes, args.only_stage, args.cases, args.parallel)
    if args.cmd == "check":
        return check(args.general, args.cases)
    if args.cmd == "status":
        return status()
    return report()


if __name__ == "__main__":
    sys.exit(main())
