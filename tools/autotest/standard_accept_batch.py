#!/usr/bin/env python3
"""原版 standard 25 武將 × 层 A 16 模式：可中斷續跑的 headless 跑批入口。

為什麼另起一檔（而不是用 general_acceptance.py 或
tools/autotest/standard-accept-driver.sh）：

  general_acceptance.py 的 headless stage 用 self.exe("QSanguosha") 找二進位，
  而 runner_common.find_exe() 的搜尋順序是
  ("release", "relwithdebinfo", "RelWithDebInfo", "debug", "")。
  release/QSanguosha（2026-10-23 02:13）**不含** room.cpp 的
  `[AUTOTEST] skill-invoked` 與 game-session-controller.cpp 的
  `[AUTOTEST] seated` 兩個日誌點，只有 relwithdebinfo/QSanguosha
  （2026-10-23 04:58）才有。走 find_exe 會讓 25 個武將全部判成
  「技能未被觀測」，白跑一輪。

  本檔直接把 --exe 指到 relwithdebinfo/QSanguosha，不動任何既有腳本。

每個武將一個穩定 seed（BASE_SEED + 序號 × 10），
狀態寫在 <out>/state/<general>.status，完成即落地，
關機或中斷後重跑同一條命令只補未完成的武將。

用法：
    python3 tools/autotest/standard_accept_batch.py --status
    python3 tools/autotest/standard_accept_batch.py              # 續跑
    python3 tools/autotest/standard_accept_batch.py --only caocao
    python3 tools/autotest/standard_accept_batch.py --reset caocao
"""

import argparse
import json
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "artifacts" / "standard-accept-20261110"

DEFAULT_EXE = "relwithdebinfo/QSanguosha"
BASE_SEED = 20261110

# 层 A：group goal stage 1 固定的 16 個模式
LAYER_A = [
    "02_1v1", "02p", "03_1v2", "03p", "04_1v3", "04_2v2", "04_boss", "04p",
    "05p", "06_3v3", "06p", "08_defense", "08p", "08_jiangshi", "6_ctg", "7_happyrebel",
]

# 21 個 strengthen 強化的原版人 + zhenji/zhugeliang/sunquan/sunshangxiang
GENERALS = [
    "caocao", "simayi", "xiahoudun", "zhangliao", "xuchu",
    "guojia", "liubei", "guanyu", "zhangfei", "zhaoyun",
    "machao", "huangyueying", "ganning", "lvmeng", "huanggai",
    "zhouyu", "daqiao", "luxun", "huatuo", "lvbu",
    "diaochan", "zhenji", "zhugeliang", "sunquan", "sunshangxiang",
]


def seed_for(idx):
    return BASE_SEED + idx * 10


def run_one(general, idx, out, exe, modes):
    seed = seed_for(idx)
    gdir = out / "raw" / general
    gdir.mkdir(parents=True, exist_ok=True)
    logdir = gdir / "headless"
    started = time.time()
    cmd = [
        sys.executable, str(HERE / "headless_runner.py"),
        "--exe", exe,
        "--exe-root", str(ROOT),
        "--seed", str(seed),
        "--modes", ",".join(modes),
        "--games", "1",
        "--parallel", "1",
        "--force-general", general,
        "--log-dir", str(logdir),
    ]
    with open(gdir / "runner.log", "a") as fh:
        fh.write("$ " + " ".join(cmd) + "\n")
        fh.flush()
        rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
    elapsed = int(time.time() - started)
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    (out / "state" / f"{general}.status").write_text(
        f"{general}\trc={rc}\tseed={seed}\tseconds={elapsed}\t{stamp}\n"
    )
    return general, rc, elapsed


def read_state(out):
    st = {}
    for g in GENERALS:
        p = out / "state" / f"{g}.status"
        if p.exists():
            st[g] = p.read_text().strip()
    return st


def show_status(out):
    st = read_state(out)
    done = sum(1 for v in st.values() if "\trc=0\t" in v + "\t")
    print(f"{'GENERAL':<16}{'STATUS':<26}")
    for g in GENERALS:
        print(f"{g:<16}{st.get(g, 'pending')}")
    print(f"\ndone={done} nonzero={len(st) - done} pending={len(GENERALS) - len(st)} of {len(GENERALS)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--exe", default=DEFAULT_EXE,
                    help=f"headless 用的 GUI 二進位（預設 {DEFAULT_EXE}；release 缺 AUTOTEST 日誌點）")
    ap.add_argument("--jobs", type=int, default=3, help="同時跑幾個武將")
    ap.add_argument("--only", action="append", default=[], help="只跑指定武將（可重複）")
    ap.add_argument("--reset", default="", help="清掉某武將的狀態後退出")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--no-judge", action="store_true", help="跑完不自動執行判定")
    args = ap.parse_args()

    out = Path(args.out)
    (out / "state").mkdir(parents=True, exist_ok=True)
    (out / "raw").mkdir(parents=True, exist_ok=True)

    if args.reset:
        (out / "state" / f"{args.reset}.status").unlink(missing_ok=True)
        print(f"reset {args.reset}")
        return 0

    if args.status:
        show_status(out)
        return 0

    exe = str(ROOT / args.exe) if not Path(args.exe).is_absolute() else args.exe
    if not Path(exe).exists():
        print(f"exe not found: {exe}", file=sys.stderr)
        return 2

    todo = [(g, GENERALS.index(g)) for g in GENERALS]
    if args.only:
        todo = [(g, i) for g, i in todo if g in args.only]
    todo = [(g, i) for g, i in todo if not (out / "state" / f"{g}.status").exists()]

    print(f"exe={exe}")
    print(f"modes={len(LAYER_A)} generals_todo={len(todo)} jobs={args.jobs}")
    if not todo:
        print("nothing to do")
        return 0

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(run_one, g, i, out, exe, LAYER_A) for g, i in todo]
        for fut in as_completed(futs):
            g, rc, secs = fut.result()
            print(f"[{'OK' if rc == 0 else 'NONZERO'}] {g} rc={rc} {secs}s", flush=True)
            show_status(out)

    print(f"elapsed {int(time.time() - t0)}s")

    if not args.no_judge:
        judge = HERE / "standard_matrix_judge.py"
        cmd = [sys.executable, str(judge), "--dir", str(out),
               "--out", str(out / "REPORT.md"), "--tsv", str(out / "summary.tsv")]
        for g in GENERALS:
            if (out / "state" / f"{g}.status").exists():
                cmd += ["--general", g]
        print("$ " + " ".join(cmd))
        subprocess.call(cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
