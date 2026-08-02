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
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

from runner_common import (HEADLESS_HEADER, common_args, describe_exit,
                           find_exe, hex_exit, is_crash_code, kill_pid,
                           log_dir_for, parse_headless_log, resolve_workdir,
                           spawn, stamp, tail_lines, wait_exit, write_csv)

EXE_NAME = "QSanguosha.exe"
PER_GAME_TIMEOUT = 600  # 每局預估上限 (秒, 20p 大規模對局較慢)


CUR_GAME_RE = re.compile(r">>> Starting headless game (\d+) <<<")


def current_game(headless_log):
    """解析最後一次 header 之後的 'Starting headless game N' (目前進行到第幾局)。"""
    m = None
    if os.path.isfile(headless_log):
        with open(headless_log, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        start = 0
        for i, line in enumerate(lines):
            if HEADLESS_HEADER.search(line):
                start = i
        for line in lines[start:]:
            m = CUR_GAME_RE.search(line)
    return int(m.group(1)) if m else 0


def report_progress(label, games, prev, finished, failed):
    """印出進度變化: 局完成。回傳更新後的 prev dict。"""
    now = time.strftime("%H:%M:%S")
    total = sum(finished.values())
    if total != prev["total"] or failed != prev["failed"]:
        top = sorted(finished.items(), key=lambda kv: -kv[1])[:3]
        dist = ", ".join("%s x%d" % (w, c) for w, c in top) if top else "-"
        print("  [%s] [%s] 局 %d/%d 完成 | 勝方: %s%s" % (
            label, now, total, games, dist,
            " | 失敗 %d 局" % failed if failed else ""))
        return {"total": total, "failed": failed}
    return prev


def run_mode(args, exe, workdir, mode, games, tag=""):
    # 同一模式多份平行時, 各自獨立 log 檔 (tag 為 process 編號)
    suffix = "-%s" % tag if tag else ""
    log_path = os.path.join(log_dir_for(args), "headless", "%s%s.log" % (mode, suffix))
    headless_log = os.path.join(log_dir_for(args), "headless", "%s%s-headless.log" % (mode, suffix))
    label = mode + ("" if not tag else "#%s" % tag)
    cmd = [exe, "--headless", "--game-mode", mode, "--games", str(games),
           "--headless-log", headless_log]
    # 自動化測試: 指定主公武將 (--test-general/--test-general2), 反覆測同一武將找 bug
    if getattr(args, "general", ""):
        cmd += ["--test-general", args.general]
    if getattr(args, "general2", ""):
        cmd += ["--test-general2", args.general2]
    # 清除舊 log, 避免跨執行殘留污染進度/標記解析
    for p in (log_path, headless_log):
        if os.path.isfile(p):
            os.remove(p)
    proc = spawn(cmd, workdir, log_path)
    timeout = games * PER_GAME_TIMEOUT + 120
    deadline = time.time() + timeout
    timed_out = False
    code = None
    # 輪詢標記檔, 每局開始/完成即在 CMD 印進度
    prev = {"total": 0, "failed": 0}
    prev_current = 0
    while True:
        code = proc.poll()
        if code is not None:
            break
        if time.time() > deadline:
            timed_out = True
            kill_pid(proc.pid)
            code = proc.wait()
            break
        finished, failed, done = parse_headless_log(headless_log)
        cur = current_game(headless_log)
        if cur != prev_current:
            if cur > 0 and sum(finished.values()) == prev["total"]:
                print("  [%s] [%s] 第 %d/%d 局進行中..." % (label, time.strftime("%H:%M:%S"), cur, games))
            prev_current = cur
        prev = report_progress(label, games, prev, finished, failed)
        time.sleep(2)
    close_proc(proc)
    finished, failed, done = parse_headless_log(headless_log)
    n_finished = sum(finished.values())
    ok = (not timed_out) and done and (n_finished == games) and (failed == 0) and (code == 0)
    # 閃退摘要: 非逾時且 exit code 是 Windows 崩潰碼 (0xC0000005 等) 才算閃退;
    # exit=1 等小值是應用程式自行退出 (如啟動失敗), 不算崩潰
    crashed = (not timed_out) and is_crash_code(code)
    context = tail_lines(headless_log, 20) if crashed else []
    return {
        "mode": mode + ("" if not tag else "#%s" % tag),
        "exit": code, "exit_name": describe_exit(code), "exit_hex": hex_exit(code),
        "timeout": timed_out, "crashed": crashed, "crash_context": context,
        "finished": n_finished, "expected": games, "failed_games": failed,
        "done_marker": done, "ok": ok, "winners": dict(finished), "log": log_path,
    }


def close_proc(proc):
    from runner_common import close_proc as _cp
    _cp(proc)


def main():
    # log 行含中文, console 編碼 (cp950) 印不出時以 ? 取代, 避免 runner 自己炸掉
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description="QSanguosha headless 壓力測試 runner")
    common_args(parser)
    parser.add_argument("--games", type=int, default=5,
                        help="每個模式要跑的局數 (預設 5)")
    parser.add_argument("--parallel", type=int, default=2,
                        help="同時執行的 process 數 (預設 2)")
    parser.add_argument("--general", default="",
                        help="指定主公武將, 反覆測試同武將找 bug (空 = 隨機)")
    parser.add_argument("--general2", default="",
                        help="雙將模式指定主公副將 (空 = 隨機)")
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

    # 任務展開:
    #   - 模式數 >= parallel: 每個模式 1 份任務 (全部都要跑), 同時最多 parallel 個
    #   - 模式數 <  parallel: 同一模式 round-robin 補到 parallel 份任務, 全部同時執行
    parallel = max(1, args.parallel)
    if len(modes) >= parallel:
        tasks = [(m, "") for m in modes]
    else:
        tasks = [(modes[i % len(modes)], str(i + 1)) for i in range(parallel)]
    workers = min(parallel, len(tasks))
    print("模式  : %s, 每 process %d 局, 並行上限 %d (共 %d 個 process)"
          % (", ".join(modes), args.games, parallel, len(tasks)))

    results = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(run_mode, args, exe, workdir, m, args.games, tag): m
                   for (m, tag) in tasks}
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
                # 閃退摘要: exit 翻譯 + 崩潰前 20 行 log
                if r.get("crashed"):
                    print("        閃退: %s (%s)" % (r.get("exit_name"), r.get("exit_hex")))
                    for line in r.get("crash_context", []):
                        print("          %s" % line)

    csv_path = os.path.join(log_root, "summary-headless-%s.csv" % stamp())
    header = ["mode", "ok", "exit", "exit_name", "timeout", "crashed",
              "finished", "expected", "failed_games", "done_marker", "log"]
    write_csv(csv_path, header, [
        [r.get("mode"), r.get("ok"), r.get("exit"), r.get("exit_name"),
         r.get("timeout"), r.get("crashed"),
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
