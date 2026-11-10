#!/usr/bin/env python3
"""NOS 武將 headless 多模式矩陣。

用途：把標準 21 名 nos_* 武將丟進多個遊戲模式各跑一局，量測
  1) 該模式是否真的採用 --test-general 指定的武將（AUTOTEST seated）
  2) AI 是否叫得動技能（AUTOTEST skill-invoked，只覆蓋會 invoke 的技能）
  3) 勝負與否（Winner:）

既有證據（artifacts/nos-full/evidence/<general>/linux-headless/game-<mode>.log）
會一併納入報告的 03_1v2 / 05p 欄，避免重跑。

子命令：
  run [--minutes N] [--parallel 3] [--only-mode M ...]
  status
  report [--reuse-nos-full]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERALS_FILE = ROOT / "tools/autotest/nos_generals.txt"
BIN = ROOT / "relwithdebinfo/QSanguosha"
OUT = ROOT / "artifacts/nos-headless-matrix"
STATE = OUT / "state.json"
LOCK = OUT / "run.lock"
LOCK_STALE_S = 90 * 60
NATIVE_EVIDENCE = ROOT / "artifacts/nos-full/evidence"

# 模式 → (估計秒數, timeout 秒數)。估計值來自 2026-10-23 的模式可行性掃描。
MODES = {
    "03_1v2": (30, 600),
    "05p": (110, 900),
    "02p": (40, 600),
    "03p": (40, 600),
    "04p": (50, 600),
    "06p": (70, 900),
    "08p": (160, 1200),
    "10p": (90, 900),
}

# 無 --test-general 的模式已排除：02_1v1、04_1v3、04_2v2、04_boss、05_ol、
# 06_ol、08_defense、06_3v3、06_XMode 的座次由模式自己決定，強制武將不生效。


def generals() -> list[str]:
    return [line.strip() for line in GENERALS_FILE.read_text().splitlines() if line.strip()]


def cells() -> list[dict]:
    out = []
    gens = generals()
    idx = 0
    for mode, (est, tmo) in MODES.items():
        for gi, gen in enumerate(gens):
            idx += 1
            seed = 20265000 + idx
            out.append({
                "id": f"{mode}/{gen}",
                "general": gen,
                "mode": mode,
                "seed": seed,
                "estimate_s": est,
                "timeout_s": tmo,
                "status": "pending",
                "attempts": 0,
            })
    return out


def load_state() -> dict:
    if STATE.exists():
        return json.loads(STATE.read_text())
    return {"cells": {}, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z")}


def save_state(state: dict) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = STATE.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(state, ensure_ascii=False, indent=1))
    tmp.replace(STATE)


def lock_fresh() -> bool:
    if not LOCK.exists():
        return False
    return (time.time() - LOCK.stat().st_mtime) < LOCK_STALE_S


def touch_lock() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    LOCK.write_text(str(os.getpid()))


def parse_log(path: Path, general: str) -> dict:
    text = path.read_text(errors="replace") if path.exists() else ""
    seated = re.findall(r"AUTOTEST\] seated (\S+) general='([^']*)'", text)
    used = re.findall(r"AUTOTEST\] skill-invoked (\S+) (\S+)", text)
    winner = ""
    for m in re.finditer(r"Winner: ([A-Za-z+_0-9.-]+)", text):
        winner = m.group(1)
    forced = [seat for seat, gen in seated if gen == general]
    skills: dict[str, int] = {}
    for _seat, skill in used:
        skills[skill] = skills.get(skill, 0) + 1
    return {
        "seated": bool(forced),
        "seat": forced[0] if forced else "",
        "players": len(seated),
        "skills": skills,
        "events": sum(skills.values()),
        "winner": winner,
        "log_lines": text.count("\n"),
    }


def run_cell(cell: dict, parallel: int) -> dict:
    art = OUT / "evidence" / cell["mode"] / cell["general"]
    art.mkdir(parents=True, exist_ok=True)
    log = art / "game.log"
    cmd = [
        str(BIN), "--headless", "--game-mode", cell["mode"], "--games", "1",
        "--seed", str(cell["seed"]), "--headless-log", str(log),
        "--test-general", cell["general"],
    ]
    env = dict(os.environ)
    env.pop("QT_QPA_PLATFORM", None)
    env["QSAN_NO_HIDDEN_GENERAL"] = "1"
    start = time.time()
    try:
        proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, timeout=cell["timeout_s"], env=env)
        rc = proc.returncode
        (art / "stderr.log").write_bytes(proc.stderr or b"")
    except subprocess.TimeoutExpired:
        rc = 124
    dur = time.time() - start
    info = parse_log(log, cell["general"])
    return {
        "rc": rc,
        "duration_s": round(dur, 1),
        "status": "pass" if rc == 0 else ("timeout" if rc == 124 else "fail"),
        **info,
    }


REUSE_MODES = ("03_1v2", "05p")


def seed_reuse(state: dict) -> int:
    """把 artifacts/nos-full 已有的 linux-headless 證據直接算成 pass，避免重跑。"""
    store = state.setdefault("cells", {})
    seeded = 0
    for gen in generals():
        for mode in REUSE_MODES:
            cid = f"{mode}/{gen}"
            if store.get(cid, {}).get("status") == "pass":
                continue
            reused = native_cell(gen, mode)
            if not reused:
                continue
            reused.update({"general": gen, "mode": mode, "attempts": 1})
            store[cid] = reused
            seeded += 1
    if seeded:
        save_state(state)
        print(f"reused {seeded} cells from artifacts/nos-full evidence")
    return seeded


def run(minutes: float, parallel: int, only_modes: list[str] | None) -> int:
    state = load_state()
    store = state.setdefault("cells", {})
    seed_reuse(state)
    todo = []
    for cell in cells():
        if only_modes and cell["mode"] not in only_modes:
            continue
        cur = store.get(cell["id"])
        if cur and cur.get("status") == "pass":
            continue
        if cur and cur.get("status") in {"fail", "timeout"} and cur.get("attempts", 0) >= 3:
            continue
        todo.append(cell)
    if not todo:
        print("no runnable cells (all pass or exhausted retries)")
        return 0
    deadline = None if minutes <= 0 else time.time() + minutes * 60
    if lock_fresh():
        print("another run holds the lock; abort")
        return 2
    touch_lock()
    print(f"cells to run: {len(todo)} · parallel={parallel} · minutes={minutes}")
    done = 0
    try:
        with ThreadPoolExecutor(max_workers=parallel) as pool:
            futures = {}
            index = 0
            while index < len(todo) or futures:
                if deadline and time.time() > deadline:
                    print("time budget reached")
                    break
                while index < len(todo) and len(futures) < parallel:
                    cell = todo[index]
                    index += 1
                    futures[pool.submit(run_cell, cell, parallel)] = cell
                time.sleep(2)
                touch_lock()
                for fut in [f for f in futures if f.done()]:
                    cell = futures.pop(fut)
                    try:
                        res = fut.result()
                    except Exception as exc:  # noqa: BLE001
                        res = {"status": "fail", "rc": -1, "duration_s": 0, "error": str(exc)}
                    prev = store.get(cell["id"], {})
                    res["attempts"] = prev.get("attempts", 0) + 1
                    res.update({k: cell[k] for k in ("general", "mode", "seed")})
                    store[cell["id"]] = res
                    save_state(state)
                    done += 1
                    print(f"[{done}/{len(todo)}] {cell['id']} {res['status']} rc={res.get('rc')} "
                          f"{res.get('duration_s')}s seated={res.get('seated')} events={res.get('events')}")
    finally:
        LOCK.unlink(missing_ok=True)
        save_state(state)
    return 0


def status() -> int:
    state = load_state()
    store = state.get("cells", {})
    counts: dict[str, int] = {}
    for cell in cells():
        cur = store.get(cell["id"], {"status": "pending"})
        counts[cur.get("status", "pending")] = counts.get(cur.get("status", "pending"), 0) + 1
    total = len(cells())
    print(f"cells: {total}")
    for k in ("pass", "fail", "timeout", "pending"):
        print(f"  {k:8s} {counts.get(k, 0)}")
    rem = counts.get("pending", 0) + counts.get("fail", 0) + counts.get("timeout", 0)
    est = sum(c["estimate_s"] for c in cells() if store.get(c["id"], {}).get("status") != "pass")
    print(f"  remaining: {rem} cells, ~{est / 60:.0f} game-min (parallel 3 → ~{est / 180:.0f} min)")
    print(f"  lock: {'held' if lock_fresh() else 'free'}")
    return 0


GENERAL_LABEL = {
    "nos_caocao": "曹操", "nos_simayi": "司馬懿", "nos_xiahoudun": "夏侯惇", "nos_zhangliao": "張遼",
    "nos_xuchu": "許褚", "nos_guojia": "郭嘉", "nos_liubei": "劉備", "nos_guanyu": "關羽",
    "nos_zhangfei": "張飛", "nos_zhaoyun": "趙雲", "nos_machao": "馬超", "nos_huangyueying": "黃月英",
    "nos_ganning": "甘寧", "nos_lvmeng": "呂蒙", "nos_huanggai": "黃蓋", "nos_zhouyu": "周瑜",
    "nos_daqiao": "大喬", "nos_luxun": "陸遜", "nos_huatuo": "華佗", "nos_lvbu": "呂布",
    "nos_diaochan": "貂蟬",
}


def native_cell(general: str, mode: str) -> dict | None:
    """重用 artifacts/nos-full 的 linux-headless 證據。"""
    log = NATIVE_EVIDENCE / general / "linux-headless" / f"game-{mode}.log"
    if not log.exists():
        return None
    info = parse_log(log, general)
    return {"status": "pass", "reused": True, "duration_s": 0, **info}


def report(reuse: bool) -> int:
    state = load_state()
    store = state.get("cells", {})
    gens = generals()
    modes = list(MODES)
    rows: dict[str, dict[str, dict]] = {}
    for gen in gens:
        rows[gen] = {}
        for mode in modes:
            cur = store.get(f"{mode}/{gen}")
            if cur and cur.get("status") == "pass":
                rows[gen][mode] = cur
            elif reuse and mode in ("03_1v2", "05p"):
                reused = native_cell(gen, mode)
                if reused:
                    rows[gen][mode] = reused

    def fmt(cell: dict | None) -> str:
        if not cell:
            return "—"
        if not cell.get("seated"):
            return "未上場"
        return f"{cell.get('events', 0)}次/{cell.get('winner') or '?'}"

    lines = ["# NOS 武將 headless 多模式矩陣", ""]
    tested = sum(1 for g in rows.values() for c in g.values())
    lines.append(f"完成格數：{tested}／{len(gens) * len(modes)}（模式 × 武將）")
    lines.append("")
    lines.append("每格格式＝該局技能發動次數／勝方。技能數只計會走 `notifySkillInvoked` 的可選技能，"
                 "鎖定技與純轉化牌不計。")
    lines.append("")
    header = "| 武將 | " + " | ".join(modes) + " |"
    lines.append(header)
    lines.append("|" + "---|" * (len(modes) + 1))
    for gen in gens:
        cells_row = [fmt(rows[gen].get(m)) for m in modes]
        lines.append(f"| {GENERAL_LABEL.get(gen, gen)}（{gen}） | " + " | ".join(cells_row) + " |")

    lines.append("")
    lines.append("## 每模式彙總")
    lines.append("")
    lines.append("| 模式 | 完成局 | 強制武將生效 | 技能事件總數 | 勝方分布 |")
    lines.append("|---|---|---|---|---|")
    for mode in modes:
        done = [rows[g][mode] for g in gens if mode in rows[g]]
        seated = [c for c in done if c.get("seated")]
        events = sum(c.get("events", 0) for c in done)
        winners: dict[str, int] = {}
        for c in done:
            winners[c.get("winner") or "?"] = winners.get(c.get("winner") or "?", 0) + 1
        wtxt = "、".join(f"{k} {v}" for k, v in sorted(winners.items(), key=lambda kv: -kv[1]))
        lines.append(f"| {mode} | {len(done)} | {len(seated)} | {events} | {wtxt} |")

    lines.append("")
    lines.append("## 失敗／缺漏")
    lines.append("")
    bad = [(cid, c) for cid, c in store.items() if c.get("status") != "pass"]
    if not bad:
        lines.append("本次矩陣沒有失敗格。")
    else:
        for cid, c in sorted(bad):
            lines.append(f"- {cid}: {c.get('status')} rc={c.get('rc')} attempts={c.get('attempts')}")

    out = OUT / "REPORT.md"
    out.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nwritten: {out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    p_run = sub.add_parser("run")
    p_run.add_argument("--minutes", type=float, default=0)
    p_run.add_argument("--parallel", type=int, default=3)
    p_run.add_argument("--only-mode", action="append")
    sub.add_parser("status")
    p_rep = sub.add_parser("report")
    p_rep.add_argument("--reuse-nos-full", action="store_true")
    args = parser.parse_args()
    if args.cmd == "run":
        return run(args.minutes, args.parallel, args.only_mode)
    if args.cmd == "status":
        return status()
    return report(args.reuse_nos_full)


if __name__ == "__main__":
    sys.exit(main())
