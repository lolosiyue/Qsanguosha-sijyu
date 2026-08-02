# -*- coding: utf-8 -*-
"""真實網路測試 runner (串行)。

流程 (每個模式):
  1. 啟動一次 qsanguosha_server.exe --game-mode <模式> --autotest-log <檔> (常駐)
  2. 每局 spawn 一個 GUI client:
     QSanguosha.exe -connect:127.0.0.1 --test-general <武將> --auto-robots
     (client 自動選將、自動填 AI 開局、自動托管)
  3. 以 server 的 [AUTOTEST] 標記檔判定開局/結束, 按 PID 殺 client
  4. 模式跑完後殺 server, 進入下一個模式

用法:
    python network_runner.py --exe-root L:\\finaldebug\\QSanguosha-v2 ^
        --modes 10p,20p,02_1v1,05p --runs 2 --general zhenji
"""
import argparse
import os
import socket
import sys
import time

from runner_common import (MARK_GAME_OVER, MARK_GAME_START, common_args,
                           find_exe, kill_pid, log_dir_for, resolve_workdir,
                           spawn, stamp, wait_exit, write_csv)

SERVER_EXE = "qsanguosha_server.exe"
CLIENT_EXE = "QSanguosha.exe"
SERVER_PORT = 9527  # 與 config.ini ServerPort 一致
SERVER_STARTUP_TIMEOUT = 60   # 等 server 就緒 (秒)
GAME_TIMEOUT = 600            # 單局上限 (秒)
CLIENT_JOIN_TIMEOUT = 120     # 等 client 連上並開局 (秒)


def port_open(port, timeout=0.5):
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        s.close()
        return True
    except OSError:
        return False


def wait_port(port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if port_open(port):
            return True
        time.sleep(0.5)
    return False


def read_markers(log_path, offset):
    """讀標記檔, 回傳 (新 offset, [自 offset 起的新增行])。"""
    if not os.path.isfile(log_path):
        return offset, []
    with open(log_path, "rb") as f:
        f.seek(offset)
        data = f.read()
    new_offset = offset + len(data)
    return new_offset, data.decode("utf-8", errors="replace").splitlines()


def wait_for_marker(log_path, predicate, timeout, start_offset=0):
    offset = start_offset
    deadline = time.time() + timeout
    while time.time() < deadline:
        offset, lines = read_markers(log_path, offset)
        for line in lines:
            if predicate(line):
                return line, offset
        time.sleep(0.3)
    return None, offset


def run_mode(args, exe_root, workdir, mode, runs, general):
    log_root = log_dir_for(args)
    mode_dir = os.path.join(log_root, "network", mode)
    os.makedirs(mode_dir, exist_ok=True)
    marker_file = os.path.join(mode_dir, "autotest.log")
    if os.path.isfile(marker_file):
        os.remove(marker_file)

    server_exe = find_exe(exe_root, SERVER_EXE)
    client_exe = find_exe(exe_root, CLIENT_EXE)

    results = []
    print("=== 模式 %s: 啟動 server ===" % mode)
    proc = spawn([server_exe, "--game-mode", mode, "--autotest-log", marker_file],
                 workdir, os.path.join(mode_dir, "server.log"))
    try:
        if not wait_port(SERVER_PORT, SERVER_STARTUP_TIMEOUT):
            code = wait_exit(proc, 5)
            print("[FAIL] %s: server 未就緒 (exit=%s)" % (mode, code))
            return [{"run": "-", "ok": False, "note": "server startup failed"}]
        print("  server 就緒 (port %d)" % SERVER_PORT)

        marker_offset = 0
        for run_id in range(1, runs + 1):
            client_log = os.path.join(mode_dir, "run%d.log" % run_id)
            print("  局 %d/%d: 啟動 client" % (run_id, runs))
            client = spawn([client_exe, "-connect:127.0.0.1",
                            "--test-general", general, "--auto-robots"],
                           workdir, client_log)

            start_line, marker_offset = wait_for_marker(
                marker_file, lambda l: MARK_GAME_START in l,
                CLIENT_JOIN_TIMEOUT, marker_offset)
            if start_line is None:
                ccode = wait_exit(client, 5)
                kill_pid(client.pid)
                close_proc(client)
                results.append({"run": run_id, "ok": False,
                                "note": "no game start (client exit=%s)" % ccode})
                print("  [FAIL] 局 %d: 未偵測到開局 (client exit=%s)" % (run_id, ccode))
                time.sleep(1)
                continue

            over_line, marker_offset = wait_for_marker(
                marker_file, lambda l: MARK_GAME_OVER.search(l),
                GAME_TIMEOUT, marker_offset)
            close_proc(client)
            if over_line is None:
                kill_pid(client.pid)
                results.append({"run": run_id, "ok": False, "note": "game timeout"})
                print("  [FAIL] 局 %d: 對局逾時 (%ds), 已殺 client" % (run_id, GAME_TIMEOUT))
            else:
                m = MARK_GAME_OVER.search(over_line)
                winner = m.group(1) or "none"
                kill_pid(client.pid)
                results.append({"run": run_id, "ok": True, "note": "winner=%s" % winner})
                print("  [PASS] 局 %d: 結束, winner=%s" % (run_id, winner))
            time.sleep(1)
    finally:
        kill_pid(proc.pid)
        close_proc(proc)
    return results


def close_proc(proc):
    from runner_common import close_proc as _cp
    _cp(proc)


def main():
    parser = argparse.ArgumentParser(description="QSanguosha 真實網路測試 runner")
    common_args(parser)
    parser.add_argument("--runs", type=int, default=2, help="每個模式要跑的局數 (預設 2)")
    parser.add_argument("--general", default="zhenji",
                        help="client 自動選將 (02_1v1 請用 x0; 預設 zhenji)")
    args = parser.parse_args()

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    if not modes:
        print("錯誤: 沒有指定模式", file=sys.stderr)
        return 1

    exe_root = args.exe_root
    workdir = resolve_workdir(exe_root)
    print("cwd   : %s" % workdir)
    print("模式  : %s, 每模式 %d 局, 武將: %s" % (", ".join(modes), args.runs, args.general))

    all_results = []
    for mode in modes:
        if mode == "02_1v1" and args.general != "x0":
            print("提示: 02_1v1 模式建議 --general x0 (KOF 佔位選將), 目前用 %s" % args.general)
        results = run_mode(args, exe_root, workdir, mode, args.runs, args.general)
        for r in results:
            r["mode"] = mode
            all_results.append(r)

    csv_path = os.path.join(log_dir_for(args), "summary-network-%s.csv" % stamp())
    header = ["mode", "run", "ok", "note"]
    write_csv(csv_path, header, [
        [r.get("mode"), r.get("run"), r.get("ok"), r.get("note")] for r in all_results
    ])
    ok = sum(1 for r in all_results if r.get("ok"))
    print("結果: %s" % csv_path)
    print("總計: %d/%d 通過" % (ok, len(all_results)))
    return 0 if ok == len(all_results) else 1


if __name__ == "__main__":
    sys.exit(main())
