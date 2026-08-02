# -*- coding: utf-8 -*-
"""headless 壓力測試 runner。

以 QSanguosha.exe --headless --game-mode <模式> --games <N> 執行純 AI 對局
(單一 process 內連續 N 局), 依模式平行多開, 以 exit code + log 標記判定結果。

用法:
    python headless_runner.py --exe-root L:\\finaldebug\\QSanguosha-v2 ^
        --modes 10p,20p,02_1v1,05p --games 5 --parallel 2
"""
import argparse
import os
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

from runner_common import (common_args, find_exe, kill_pid, log_dir_for,
                           parse_headless_log, resolve_workdir, spawn, stamp,
                           wait_exit, write_csv)

EXE_NAME = "QSanguosha.exe"
PER_GAME_TIMEOUT = 600  # 每局預估上限 (秒, 20p 大規模對局較慢)


def run_mode(args, exe, workdir, mode, games):
    log_path = os.path.join(log_dir_for(args), "headless", "%s.log" % mode)
    headless_log = os.path.join(log_dir_for(args), "headless", "%s-headless.log" % mode)
    cmd = [exe, "--headless", "--game-mode", mode, "--games", str(games),
           "--headless-log", headless_log]
    proc = spawn(cmd, workdir, log_path)
    timeout = games * PER_GAME_TIMEOUT + 120
    code = wait_exit(proc, timeout)
    timed_out = code is None
    if timed_out:
        kill_pid(proc.pid)
        code = proc.wait()
    close_proc(proc)
    finished, failed, done = parse_headless_log(headless_log)
    n_finished = sum(finished.values())
    ok = (not timed_out) and done and (n_finished == games) and (failed == 0) and (code == 0)
    return {
        "mode": mode, "exit": code, "timeout": timed_out,
        "finished": n_finished, "expected": games, "failed_games": failed,
        "done_marker": done, "ok": ok, "winners": dict(finished), "log": log_path,
    }


def close_proc(proc):
    from runner_common import close_proc as _cp
    _cp(proc)


def main():
    parser = argparse.ArgumentParser(description="QSanguosha headless 壓力測試 runner")
    common_args(parser)
    parser.add_argument("--games", type=int, default=5,
                        help="每個模式要跑的局數 (預設 5)")
    parser.add_argument("--parallel", type=int, default=2,
                        help="同時執行的 process 數 (預設 2)")
    args = parser.parse_args()

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    if not modes:
        print("錯誤: 沒有指定模式", file=sys.stderr)
        return 1

    exe = find_exe(args.exe_root, EXE_NAME)
    workdir = resolve_workdir(args.exe_root)
    log_root = log_dir_for(args)
    print("執行檔: %s" % exe)
    print("cwd   : %s" % workdir)
    print("模式  : %s, 每模式 %d 局, 平行 %d" % (", ".join(modes), args.games, args.parallel))

    results = []
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futures = {pool.submit(run_mode, args, exe, workdir, m, args.games): m
                   for m in modes}
        for f in as_completed(futures):
            mode = futures[f]
            try:
                r = f.result()
            except Exception as e:
                r = {"mode": mode, "error": str(e), "ok": False}
            results.append(r)
            status = "PASS" if r.get("ok") else "FAIL"
            if r.get("error"):
                print("[%s] %s: %s" % (status, mode, r["error"]))
            else:
                extra = ""
                if r.get("done_marker") and r.get("exit") != 0:
                    extra = " (局數完成但 exit=%s, 結束時崩潰)" % r.get("exit")
                print("[%s] %s: exit=%s finished=%s/%s failed_games=%s done=%s%s" % (
                    status, mode, r.get("exit"), r.get("finished"), r.get("expected"),
                    r.get("failed_games"), r.get("done_marker"), extra))
                if r.get("winners"):
                    top = sorted(r["winners"].items(), key=lambda kv: -kv[1])[:3]
                    print("        勝方分布: %s" % ", ".join(
                        "%s x%d" % (w, c) for w, c in top))

    csv_path = os.path.join(log_root, "summary-headless-%s.csv" % stamp())
    header = ["mode", "ok", "exit", "timeout", "finished", "expected",
              "failed_games", "done_marker", "log"]
    write_csv(csv_path, header, [
        [r.get("mode"), r.get("ok"), r.get("exit"), r.get("timeout"),
         r.get("finished"), r.get("expected"), r.get("failed_games"),
         r.get("done_marker"), r.get("log", "").replace("\\", "/")]
        for r in results
    ])
    print("結果: %s" % csv_path)

    ok = sum(1 for r in results if r.get("ok"))
    print("總計: %d/%d 通過" % (ok, len(results)))
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
