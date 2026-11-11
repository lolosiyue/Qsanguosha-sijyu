#!/usr/bin/env python3
"""NOS 21 將在大局／村民／劇情模式下的 headless 矩陣。

與 nos_headless_matrix.py 的分工：
  * 那一支跑 21 將 × {03_1v2, 05p, 02p, 03p, 04p, 06p, 08p, 10p}（以 --test-general 強制主公）。
  * 這一支補三個使用者點名的範圍：
      A. 20p / 50p（超大局）：--test-general 仍有效，逐將跑。
      B. 村民模式：06pv / 08pv / 09pv / 10pv（身份局加入 villager 角色 V），逐將跑。
      C. 劇情：08_jiangshi（僵尸模式 8 人，1 主 + 7 忠，第二輪 2 名忠臣變僵尸；
         extensions/jiangshi.lua:19-29）與 impasse_fight（絕境之戰，roles "ZFFFFFFF"＝1 打 7，
         src/scenario/boss-mode-scenario.cpp:540-566）。兩者 generalSelection() 為 true，
         所以 --test-general 有效。
  2026-10-23 使用者指示移除：6_ctg（陳塘關）、etc/customScenes 的 _mini_N 場景。

輸出：
  artifacts/nos-more-modes/state.json
  artifacts/nos-more-modes/evidence/<mode>/<general|seed>/game.log
  artifacts/nos-more-modes/REPORT.md

子命令：
  plan            重建 cell 清單（保留已跑結果）
  run [--minutes N] [--parallel K] [--only-mode M ...]
  status
  report
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import fcntl
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERALS_FILE = ROOT / "tools/autotest/nos_generals.txt"
BIN = ROOT / "relwithdebinfo/QSanguosha"
OUT = ROOT / "artifacts/nos-more-modes"
STATE = OUT / "state.json"
LOCK = OUT / "run.lock"
LOCK_STALE_S = 90 * 60

# A/B：逐將模式。值＝(估計秒數, timeout 秒數)；None 表示等實測後填。
GENERAL_MODES = {
    # 村民模式實測比同人數身份局慢約 5 倍（1.3 vs 6.2 log lines/s），單局可超過 900 秒。
    # 2026-10-23 實測：06pv 單局 151–1674 秒；08pv 有兩局在 3000 秒仍未收局（rc=124），
    # 村民模式的成長率不是線性的，timeout 再上一級。
    "06pv": (900, 3600),
    "08pv": (1800, 5400),
    "09pv": (2400, 6000),
    "10pv": (2400, 6000),
    "20p": (600, 4200),
    # 2026-10-24 使用者指示續跑 50p：上一輪 90 分鐘只走到 1162 行（0.22 行/秒）仍未收局，
    # 逾時上限由 3 小時上調到 5 小時，讓單局有機會自然結束。
    "50p": (1800, 18000),
    "08_jiangshi": (300, 2400),
    "impasse_fight": (400, 7200),
}
# 50p 一局可能超過半小時，預設只抽三個代表性武將（有主動技、觸發技、主公技各一）。
# 2026-10-24：20p 取消抽樣，改跑全部 21 將（每局約 9 分鐘，三路並行約一小時）。
GENERAL_LIMITS = {"50p": 1, "08pv": 5, "09pv": 5, "10pv": 5}
DEFAULT_GENERALS = ["nos_caocao", "nos_zhangliao", "nos_ganning"]

# C 已併入 GENERAL_MODES（08_jiangshi 可強制主公）；座次固定的模式與 customScenes 場景
# 使用者 2026-10-23 明確表示不需要，故不再建立 cell。


def generals() -> list[str]:
    return [line.strip() for line in GENERALS_FILE.read_text().splitlines() if line.strip()]


def cells() -> list[dict]:
    out: list[dict] = []
    idx = 0
    gens = generals()
    for mode, (est, tmo) in GENERAL_MODES.items():
        limit = GENERAL_LIMITS.get(mode, len(gens))
        picked = gens if limit >= len(gens) else DEFAULT_GENERALS[:limit]
        for gen in picked:
            idx += 1
            out.append({
                "id": f"{mode}/{gen}", "kind": "general", "general": gen, "mode": mode,
                "seed": 20267000 + idx, "estimate_s": est, "timeout_s": tmo,
                "status": "pending", "attempts": 0,
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


def _unique_seeds(store: dict) -> None:
    """同一 seed 不可以同時代表兩個 cell。

    plan() 每次都以整份 cell 清單重算 seed，但已完成的格會保留舊 seed（見 plan 的
    {**c, **(cur or {})}）。清單一擴充（例如 20p 由 3 將放寬到 21 將），新格的
    idx seed 就會撞上舊格留下的號，日後回查分不清是同局還是兩局。這裡讓已定案
    的格不動，只把還沒跑過的新格往上挪到空號。
    """
    settled = {c["seed"] for c in store.values()
               if c.get("status") != "pending" or c.get("attempts", 0)}
    for cid in sorted(store):
        c = store[cid]
        if c.get("status") != "pending" or c.get("attempts", 0):
            continue
        while c["seed"] in settled:
            c["seed"] += 1
        settled.add(c["seed"])


def plan(reset: bool = False) -> None:
    state = load_state() if not reset else {"cells": {}, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z")}
    store = state.setdefault("cells", {})
    for c in cells():
        cur = store.get(c["id"])
        if cur and cur.get("status") == "pass":
            continue
        store[c["id"]] = {**c, **(cur or {})}
    _unique_seeds(store)
    save_state(state)
    print(f"planned {len(store)} cells -> {STATE}")


def parse_log(path: Path, general: str) -> dict:
    if not path.exists():
        return {"seated": False, "seat": "", "players": 0, "skills": {}, "events": 0, "winner": ""}
    text = path.read_text(errors="replace")
    seated = re.findall(r"AUTOTEST\] seated (\S+) general='([^']*)'", text)
    mine = [(s, g) for s, g in seated if g == general] if general else []
    invoked = re.findall(r"AUTOTEST\] skill-invoked (\S+) (\S+)", text)
    skills: dict[str, int] = {}
    if general:
        seats = {s for s, g in mine}
        for seat, skill in invoked:
            if seat in seats:
                skills[skill] = skills.get(skill, 0) + 1
    winners = re.findall(r"Winner: ([A-Za-z+_0-9.-]+)", text)
    return {
        "seated": bool(mine) if general else True,
        "seat": mine[0][0] if mine else "",
        "players": len({s for s, _ in seated}),  # seated 行會重覆（chooseGenerals 多次），要數唯一席位
        "skills": skills,
        "events": sum(skills.values()),
        "winner": winners[-1] if winners else "",
        "log_lines": text.count("\n"),
    }


def run_cell(cell: dict) -> dict:
    tag = cell["general"] or f"seed{cell['seed']}"
    art = OUT / "evidence" / cell["mode"] / tag
    art.mkdir(parents=True, exist_ok=True)
    log = art / "game.log"
    if log.exists():
        # --headless-log 是 append：同一個 seed 重跑會把兩次的內容疊在一起，
        # 解析時技能／出牌次數會重複計（2026-10-25 的 50p 就踩過）。先搬走舊檔。
        log.replace(art / "game.log.prev")
    cmd = [str(BIN), "--headless", "--game-mode", cell["mode"], "--games", "1",
           "--seed", str(cell["seed"]), "--headless-log", str(log)]
    if cell["general"]:
        cmd += ["--test-general", cell["general"]]
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
    status = "pass" if rc == 0 else ("timeout" if rc == 124 else "fail")
    out = {"rc": rc, "duration_s": round(dur, 1), "status": status, **info}
    if rc < 0:
        # 訊號死亡不一定確定性：08_jiangshi/nos_xuchu seed 20267051 在 3 路並行時
        # 崩過一次（-11），單獨重跑兩次都 rc=0。因此交給一般重試流程（最多 3 次），
        # 只有連續失敗才定案，並把訊號號碼留在 note。
        out["note"] = f"signal {-rc}"
    return out


def run(minutes: int, parallel: int, only_mode: list[str] | None, only_kind: str | None = None) -> int:
    state = load_state()
    store = state.setdefault("cells", {})
    # 既有 cell 會留著上調前的 timeout（run 只讀一次 state，改常數不會回頭修正舊 cell），
    # 每輪起跑前以 GENERAL_MODES 為準重設，避免用過短的舊上限跑出假 timeout。
    for c in store.values():
        mode = c.get("mode")
        if mode in GENERAL_MODES:
            c["timeout_s"] = GENERAL_MODES[mode][1]
    todo = [c for c in store.values()
            if c["status"] in {"pending", "retry", "fail", "timeout"} and c["attempts"] < 3]
    if only_mode:
        todo = [c for c in todo if c["mode"] in only_mode]
    if only_kind:
        todo = [c for c in todo if c["kind"] == only_kind]
    if not todo:
        print("no work")
        return 0
    deadline = time.time() + minutes * 60 if minutes else float("inf")
    OUT.mkdir(parents=True, exist_ok=True)
    lock = open(LOCK, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("another round is running (run.lock held)")
        return 0

    def work(c: dict) -> dict:
        res = run_cell(c)
        res["id"] = c["id"]
        return res

    done = 0
    with ThreadPoolExecutor(max_workers=parallel) as pool:
        futs = {}
        for c in todo:
            if time.time() > deadline:
                break
            futs[pool.submit(work, c)] = c
        for fut in futs:
            res = fut.result()
            c = futs[fut]
            c.update(res)
            c["attempts"] = 3 if c.get("final") else c.get("attempts", 0) + 1
            if c["status"] != "pass" and not c.get("final") and c["attempts"] < 3:
                c["status"] = "retry"
            save_state(state)
            done += 1
            print(f"[{done}/{len(futs)}] {c['id']} {c['status']} rc={c['rc']} {c['duration_s']}s", flush=True)
    lock.close()
    return 0


def status() -> None:
    state = load_state()
    store = state.get("cells", {})
    counts: dict[str, int] = {}
    for c in store.values():
        counts[c["status"]] = counts.get(c["status"], 0) + 1
    print(f"items: {len(store)}  updated: {time.strftime('%Y-%m-%dT%H:%M:%S%z')}")
    for k in ("pass", "pass-crash", "unbounded", "fail", "timeout", "retry", "pending"):
        if counts.get(k):
            print(f"  {k:8s} {counts[k]}")
    rem = sum(counts.get(k, 0) for k in ("pending", "retry", "fail", "timeout"))
    print(f"  remaining work: {rem} items")
    for c in store.values():
        if c["status"] in {"fail", "timeout"} and c["attempts"] >= 3:
            print(f"  - {c['id']}: rc={c['rc']} {c.get('note', '')}")


def report() -> None:
    state = load_state()
    store = state.get("cells", {})
    lines = ["# NOS 武將：20p / 50p / 村民 / 劇情 headless 矩陣", ""]
    for kind, title in (("general", "逐將模式（--test-general 生效）"),
                        ("fixed", "座次固定的模式"), ("scene", "劇情場景")):
        rows = [c for c in store.values() if c["kind"] == kind]
        if not rows:
            continue
        lines += [f"## {title}", "", "| cell | 狀態 | 時長 | 人數 | 座次生效 | 技能次數 | 勝方 |", "|---|---|---|---|---|---|---|"]
        for c in sorted(rows, key=lambda x: x["id"]):
            lines.append("| {id} | {status} | {dur}s | {players} | {seated} | {events} | {winner} |".format(
                id=c["id"], status=c["status"], dur=c.get("duration_s", "-"),
                players=c.get("players", "-"), seated="是" if c.get("seated") else "否",
                events=c.get("events", "-"), winner=c.get("winner") or "-"))
        lines.append("")
    # 模式彙總
    from collections import Counter, defaultdict
    by_mode = defaultdict(list)
    for c in store.values():
        by_mode[c["mode"]].append(c)
    lines += ["## 模式彙總", "",
              "| 模式 | 格數 | 完成 | 平均時長 | 技能事件合計 | 冠軍分佈 | 最常見技能 |",
              "|---|---|---|---|---|---|---|"]
    for mode in sorted(by_mode):
        rows = by_mode[mode]
        done = [c for c in rows if c["status"] in ("pass", "pass-crash")]
        durs = [c.get("duration_s", 0) for c in done]
        skills: Counter = Counter()
        for c in rows:
            for k, v in (c.get("skills") or {}).items():
                skills[k] += v
        wins = Counter(c.get("winner") or "-" for c in done)
        top = ", ".join(f"{k} {v}" for k, v in skills.most_common(3)) or "-"
        lines.append(f"| {mode} | {len(rows)} | {len(done)} | "
                     f"{(sum(durs) / max(len(durs), 1)):.0f}s | {sum(skills.values())} | "
                     f"{', '.join(f'{k}×{v}' for k, v in wins.most_common(3)) or '-'} | {top} |")
    lines += ["", "## 與 03_1v2／05p 的差異（人工觀察）", "",
              "1. **座次**：20p／村民／僵尸／絕境都用 `--test-general` 生效，但村民局 `AUTOTEST] seated` 會重覆輸出"
              "（`chooseGenerals` 多次），人數要數唯一席位，不能數行數。",
              "2. **村民（06pv/08pv/09pv/10pv）**：對局長度失控，單局 2 分鐘到 >2 小時；`nos_ganning` 三格全部逾時不"
              "收局（3600/5400/10800 s），log 尾部仍是正常出牌。AI 互相補血（青囊、急救、桃）令劣勢方無法收尾。",
              "3. **絕境之戰（impasse_fight）**：1 主打 7 反，`huangyueying` 兩次 7200 s 不收局（log 4.7 MB）；"
              "`lvbu` 兩局都是「先印 Winner 再 SIGSEGV」，屬**收尾崩**，對局本身完成；其餘 19 格收尾正常。",
              "4. **僵尸（08_jiangshi）**：8 人局、每局 1–3 分鐘，`nos_xuchu` 曾並行下連續 3 次 signal 11，"
              "單獨重跑即 pass → 並行壓力下的偶發崩，非技能缺陷。",
              "5. **20p**：主公技可用（`hujia` 在 20 人局會觸發，03_1v2 被 `ban_gm` 禁用），但每局約 9 分鐘。",
              "6. **50p**：50 人全員上桌，90 分鐘只寫 1162 行（0.22 行/秒），暫 deferred；這是規模而非 bug。", ""]
    odd = [c for c in store.values() if c["status"] != "pass"]
    if odd:
        lines += ["## 非 pass 的格與理由", "",
                  "| cell | 狀態 | 說明 |", "|---|---|---|"]
        for c in sorted(odd, key=lambda x: x["id"]):
            lines.append("| {id} | {st} | {note} |".format(
                id=c["id"], st=c["status"], note=c.get("note") or "-"))
        lines += ["", "狀態用語：`pass-crash`＝對局出現 Winner 但收尾 SIGSEGV；"
                  "`unbounded`＝撞逾時上限仍未收局（非卡死，log 尾部仍正常出牌）；"
                  "`pending`＝deferred，另行長跑。", ""]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "REPORT.md").write_text("\n".join(lines) + "\n")
    print(f"report -> {OUT / 'REPORT.md'}")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan"); p.add_argument("--reset", action="store_true")
    r = sub.add_parser("run")
    r.add_argument("--minutes", type=int, default=0)
    r.add_argument("--parallel", type=int, default=3)
    r.add_argument("--only-mode", nargs="*")
    r.add_argument("--kind", choices=["general", "fixed", "scene"])
    sub.add_parser("status")
    sub.add_parser("report")
    a = ap.parse_args()
    if a.cmd == "plan":
        plan(a.reset)
    elif a.cmd == "run":
        return run(a.minutes, a.parallel, a.only_mode, a.kind)
    elif a.cmd == "status":
        status()
    elif a.cmd == "report":
        report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
