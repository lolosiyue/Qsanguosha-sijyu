#!/usr/bin/env python3
"""08 人國戰（HEG）headless 矩陣。

國戰不是獨立 mode id，而是設定項 `EnableHegemony=true` + 08p 席位：
  * 生效點：`src/core/settings.cpp:392`（Config.EnableHegemony），
    並連帶把 Enable2ndGeneral 設為 true（每人雙將、暗置）。
  * 武將選擇走 `RoomThreadHegemony::chooseGenerals`（src/server/roomthread-hegemony.cpp:37），
    與身份局不是同一條路徑，因此 **沒有** `[AUTOTEST] seated ... general2=` 那批座次紀錄；
    能看到的證據是 `#HegemonyReveal | <頭將> | <副將> | from: sgsN`（暗將翻開）與
    `[AUTOTEST] skill-invoked`。
  * `--test-general` 在國戰仍然有效，但只強制「座位 0 的頭將」，副將照配對規則走
    （roomthread-hegemony.cpp:71-77、190-191）。
  * Linux 的設定檔是 QSettings("QSanguosha.org","QSanguosha") →
    `~/.config/QSanguosha.org/QSanguosha.conf`；QSanguosha 客戶端沒有 `--config`
    覆寫旗標（只有 qsanguosha_server 有），而本沙箱對 ~/.config 是唯讀，所以開關
    要走 wsl.exe 到真實系統改檔，跑完再還原。
  * 開關是全域的：國戰開著時任何身份局都會裝國戰 AI（AGENTS.md 的 sgs.GetConfig
    陷阱），所以只能在沒有其他對局時跑。本腳本會先檢查其他矩陣的 run.lock。

用法：
  python3 tools/autotest/nos_heg.py run [--generals a,b,...] [--seeds N] [--parallel K] [--minutes M]
  python3 tools/autotest/nos_heg.py status
  python3 tools/autotest/nos_heg.py report
"""
from __future__ import annotations

import argparse
import fcntl
import json
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "relwithdebinfo/QSanguosha"
OUT = ROOT / "artifacts/nos-heg"
STATE = OUT / "state.json"
CONF = "~/.config/QSanguosha.org/QSanguosha.conf"
WSL = "/mnt/c/Windows/System32/wsl.exe"
HEG_MODE = "08p"
TIMEOUT_S = 2400
OTHER_LOCKS = [ROOT / "artifacts/nos-more-modes/run.lock",
               ROOT / "artifacts/nos-headless-matrix/run.lock"]
DEFAULT_GENERALS = ["nos_caocao", "nos_zhangliao", "nos_ganning", "nos_huatuo"]


def wsl(script: str) -> str:
    proc = subprocess.run([WSL, "-d", "Ubuntu", "-u", "user", "-e", "bash", "-c", script],
                          capture_output=True, text=True, timeout=180)
    if proc.returncode != 0:
        raise RuntimeError(f"wsl failed rc={proc.returncode}: {proc.stderr.strip()[:400]}")
    return proc.stdout


def set_hegemony(on: bool) -> str:
    value = "true" if on else "false"
    out = wsl(
        f'C={CONF}; cp -n "$C" "$C.hegbak" 2>/dev/null; '
        f'sed -i "s/^EnableHegemony=.*/EnableHegemony={value}/" "$C"; '
        f'grep "^EnableHegemony=" "$C"'
    )
    return out.strip()


def current_flag() -> str:
    return wsl(f'grep "^EnableHegemony=" {CONF}').strip()


LOCAL_XDG = ROOT / ".maint/hegconf"
POOL_REJECT_MARKS = ("is not in the hegemony pool", "cannot reserve a legal distinct pair")


def prepare_local_config() -> str:
    """把真實設定檔複製一份到工作區，只在那份副本打開 EnableHegemony。

    Linux 的 QSettings 先看 $XDG_CONFIG_HOME（沒設才用 ~/.config），所以給子行程不同的
    XDG_CONFIG_HOME 就等於換一份設定。這樣跑國戰不必動全域設定檔，也不會有
    「跑到一半被殺、國戰開關留在 true」的風險。
    """
    target = LOCAL_XDG / "QSanguosha.org/QSanguosha.conf"
    target.parent.mkdir(parents=True, exist_ok=True)
    lines, seen = [], False
    for line in wsl(f"cat {CONF}").splitlines():
        if line.startswith("EnableHegemony="):
            lines.append("EnableHegemony=true")
            seen = True
        else:
            lines.append(line)
    if not seen:
        lines.append("EnableHegemony=true")
    target.write_text("\n".join(lines) + "\n")
    return str(LOCAL_XDG)


def other_round_running() -> bool:
    """真正的判定是 flock 有沒有被持有，不是 mtime。

    nos_more_modes.py 用 `fcntl.flock(LOCK_EX|LOCK_NB)` 鎖 `run.lock`，但檔案用完不會刪，
    mtime 會停在上一輪結束的時刻；用 mtime 判會把「剛跑完」誤判成「正在跑」。
    """
    for lock in OTHER_LOCKS:
        if not lock.exists():
            continue
        try:
            with lock.open("a") as fh:
                fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(fh, fcntl.LOCK_UN)
        except BlockingIOError:
            return True
    return False


def load_state() -> dict:
    if STATE.exists():
        return json.loads(STATE.read_text())
    return {"cells": {}, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z")}


def save_state(state: dict) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = STATE.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(state, ensure_ascii=False, indent=1))
    tmp.replace(STATE)


REVEAL = re.compile(r"#HegemonyReveal \| (\w+) \| (\w+) \| from: (\S+)")
SEATED_BANNER = re.compile(r"mode (\S+), (\d+) players")
# --test-general 在國戰只強制座位 0 的頭將，這行是「真的套用了」的直接證據；
# #HegemonyReveal 只會在有暗將翻開時出現，所以兩者要分開記。
FORCED_LINE = re.compile(r"\[AUTOTEST\] forced general: main='([^']*)'")


def parse_log(path: Path, general: str) -> dict:
    if not path.exists():
        return {"players": 0, "mode_banner": "", "pairs": [], "reveals": 0,
                "revealed_generals": [], "forced_seen": False, "skills": {},
                "events": 0, "winner": "", "errors": [], "log_lines": 0}
    text = path.read_text(errors="replace")
    banner = SEATED_BANNER.search(text)
    pairs = REVEAL.findall(text)
    invoked = re.findall(r"AUTOTEST\] skill-invoked (\S+) (\S+)", text)
    skills: dict[str, int] = {}
    for _seat, skill in invoked:
        skills[skill] = skills.get(skill, 0) + 1
    errors = sorted({line.strip() for line in text.splitlines()
                     if "AI_CALLBACK_ERROR" in line or "SIGSEGV" in line})
    winners = re.findall(r"Winner: ([A-Za-z+_0-9.-]+)", text)
    heads = {head for head, _deputy, _seat in pairs}
    deputies = {deputy for _head, deputy, _seat in pairs}
    return {
        "players": int(banner.group(2)) if banner else 0,
        "mode_banner": banner.group(1) if banner else "",
        "reveals": len(pairs),
        "pairs": [f"{h}+{d}@{s}" for h, d, s in pairs][:40],
        "revealed_generals": sorted(heads | deputies),
        "forced_applied": bool(general) and (FORCED_LINE.search(text).group(1) == general
                                             if FORCED_LINE.search(text) else False),
        "forced_seen": bool(general) and general in (heads | deputies),
        "skills": skills,
        "events": sum(skills.values()),
        "winner": winners[-1] if winners else "",
        "errors": errors[:5],
        "log_lines": text.count("\n"),
    }


def run_cell(cell: dict) -> dict:
    seed = cell["seed"]
    general = cell["general"]
    art = OUT / "evidence" / f"seed{seed}"
    art.mkdir(parents=True, exist_ok=True)
    log = art / "game.log"
    if log.exists():
        # 同 nos_more_modes：log 是 append，重跑同一個 seed 會把兩次內容疊在一起。
        log.replace(art / "game.log.prev")
    cmd = [str(BIN), "--headless", "--game-mode", HEG_MODE, "--games", "1",
           "--seed", str(seed), "--headless-log", str(log)]
    if general:
        cmd += ["--test-general", general]
    env = dict(os.environ)
    env.pop("QT_QPA_PLATFORM", None)
    env["XDG_CONFIG_HOME"] = cell.get("xdg") or str(LOCAL_XDG)
    start = time.time()
    stderr = b""
    proc = subprocess.Popen(cmd, cwd=str(ROOT), stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env)
    try:
        _out, stderr = proc.communicate(timeout=cell.get("timeout_s", TIMEOUT_S))
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        proc.kill()
        _out, stderr = proc.communicate()
        rc = 124
    dur = time.time() - start
    # 逾時也要留 stderr：國戰池擋掉強制武將時，唯一的線索就在 stderr 的 qWarning。
    (art / "stderr.log").write_bytes(stderr or b"")
    info = parse_log(log, general)
    rejected = any(mark in (stderr or b"").decode("utf-8", "replace")
                   for mark in POOL_REJECT_MARKS)
    status = "invalid" if rejected else ("pass" if rc == 0 else ("timeout" if rc == 124 else "fail"))
    return {"rc": rc, "duration_s": round(dur, 1), "status": status,
            "pool_reject": rejected, **info}


def cells(generals: list[str], seeds: int, seed_base: int = 20270100) -> list[dict]:
    """seed 由 (seed_base, 名單序號, 第幾顆) 決定；換一批武將時要換 seed_base，
    否則新格會撞到舊格的 id 而被當成已跑完跳過。"""
    out = []
    for gi, gen in enumerate(generals):
        for k in range(seeds):
            seed = seed_base + gi * 10 + k
            out.append({"id": f"seed{seed}", "seed": seed, "general": gen,
                        "mode": HEG_MODE, "timeout_s": TIMEOUT_S,
                        "status": "pending", "attempts": 0})
    return out


def run(generals: list[str], seeds: int, parallel: int, keep_on: bool, minutes: int,
        seed_base: int = 20270100) -> int:
    if other_round_running():
        print("另一個矩陣正在跑（lock 被持有）；國戰要獨佔機器，先讓它跑完。")
        return 0
    state = load_state()
    store = state.setdefault("cells", {})
    done_status = ("pass", "invalid")
    todo = [c for c in cells(generals, seeds, seed_base)
            if store.get(c["id"], {}).get("status") not in done_status]
    for c in todo:
        store.setdefault(c["id"], c)
    if not todo:
        print("no work")
        return 0
    xdg = prepare_local_config()
    for c in todo:
        c["xdg"] = xdg
    # 先把待跑格落盤，否則跑到一半被殺就完全沒有 resume 紀錄。
    save_state(state)
    print("國戰設定用副本：", xdg, flush=True)
    deadline = time.time() + minutes * 60 if minutes else float("inf")
    done = 0
    try:
        with ThreadPoolExecutor(max_workers=parallel) as pool:
            futs = {}
            for c in todo:
                if time.time() > deadline:
                    break
                futs[pool.submit(run_cell, c)] = c
            for fut in futs:
                res = fut.result()
                c = futs[fut]
                c.update(res)
                c["attempts"] = c.get("attempts", 0) + 1
                store[c["id"]] = c
                save_state(state)
                done += 1
                print(f"[{done}/{len(futs)}] {c['id']} {c['status']} rc={c['rc']} "
                      f"{c['duration_s']}s players={c['players']} reveals={c['reveals']} "
                      f"forced={c['general']} applied={c.get('forced_applied')} seen={c['forced_seen']} "
                      f"skills={c['events']} winner={c['winner'] or '-'} "
                      f"errors={len(c['errors'])}", flush=True)
    finally:
        if keep_on:
            print("保留副本設定：", xdg, flush=True)
        else:
            print("全域設定檔未被改動：", current_flag(), flush=True)
    return 0


def status() -> None:
    state = load_state()
    store = state.get("cells", {})
    print(f"cells: {len(store)}  目前設定檔: {current_flag()}")
    for cid, c in sorted(store.items()):
        print(f"  {cid:16s} {str(c.get('status')):7s} forced={str(c.get('general')):14s} "
              f"players={c.get('players')} reveals={c.get('reveals')} "
              f"applied={c.get('forced_applied')} seen={c.get('forced_seen')} skills={c.get('events')} "
              f"winner={c.get('winner') or '-'} errors={len(c.get('errors') or [])} "
              f"{c.get('duration_s')}s")


def report() -> None:
    state = load_state()
    store = state.get("cells", {})
    lines = ["# 08 人國戰（EnableHegemony + 08p）headless 矩陣", "",
             "國戰走 `RoomThreadHegemony::chooseGenerals`，不發身份局那批 `[AUTOTEST] seated`，"
             "所以座次證據取自 `#HegemonyReveal | 頭將 | 副將 | from: sgsN`（暗將翻開）與 "
             "`[AUTOTEST] skill-invoked`。`--test-general` 在國戰只強制座位 0 的頭將。", "",
             "| cell | 強制頭將 | 狀態 | 人數 | 翻開次數 | 強制將出現 | 技能次數 | 勝方 | AI 錯誤 | 時長 |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for cid, c in sorted(store.items()):
        lines.append("| {cid} | {g} | {st} | {p} | {r} | {se} | {e} | {w} | {er} | {d}s |".format(
            cid=cid, g=c.get("general") or "-", st=c.get("status") or "-",
            p=c.get("players", "-"), r=c.get("reveals", "-"),
            se="是" if c.get("forced_seen") else "否", e=c.get("events", "-"),
            w=c.get("winner") or "-", er=len(c.get("errors") or []),
            d=c.get("duration_s", "-")))
    errs = sorted({e for c in store.values() for e in (c.get("errors") or [])})
    if errs:
        lines += ["", "## 對局中出現的 AI／引擎錯誤", ""]
        lines += [f"- `{e}`" for e in errs]
    revealed = sorted({g for c in store.values() for g in (c.get("revealed_generals") or [])})
    if revealed:
        lines += ["", f"## 出現過的武將（{len(revealed)}）", "", ", ".join(revealed)]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "REPORT.md").write_text("\n".join(lines) + "\n")
    print(f"report -> {OUT / 'REPORT.md'}")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--generals", default=",".join(DEFAULT_GENERALS))
    r.add_argument("--seeds", type=int, default=2)
    r.add_argument("--parallel", type=int, default=2)
    r.add_argument("--minutes", type=int, default=0)
    r.add_argument("--seed-base", type=int, default=20270100,
                   help="新一批武將要換這個號，否則撞到既有 cell id")
    r.add_argument("--keep-on", action="store_true", help="保留副本設定（除錯用）")
    sub.add_parser("status")
    sub.add_parser("report")
    a = ap.parse_args()
    if a.cmd == "run":
        gens = [g.strip() for g in a.generals.split(",") if g.strip()]
        return run(gens, a.seeds, a.parallel, a.keep_on, a.minutes, a.seed_base)
    if a.cmd == "status":
        status()
    elif a.cmd == "report":
        report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
