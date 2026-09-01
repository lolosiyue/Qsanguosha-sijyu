# -*- coding: utf-8 -*-
"""真實網路測試 runner (串行)。

流程 (每個模式):
  1. 啟動一次 qsanguosha_server.exe --game-mode <模式> --autotest-log <檔> (常駐)
  2. 每局 spawn 一個 GUI client:
     QSanguosha.exe -connect:127.0.0.1 --test-general <武將> --auto-robots
     (client 自動選將、自動填 AI 開局、自動托管)
  3. 以 server 的 [AUTOTEST] 標記檔判定開局/結束, 按 PID 殺 client
  4. 模式跑完後殺 server, 進入下一個模式

跨平台: 執行檔名、process 啟動與清理、exit code 解讀全部走 runner_common,
所以同一份 runner 喺 Windows 同 Linux 都行得到。Linux 上 GUI client 需要一個
可用的 DISPLAY (WSLg 或者外部 Xvfb); 單局的合約驗證請改用 gui_network_smoke.py,
本 runner 的責任係 soak。

用法:
    python network_runner.py --exe-root L:\\finaldebug\\QSanguosha-v2 ^
        --modes 10p,20p,02_1v1,05p --runs 2 --general zhenji
"""
import argparse
import os
import shutil
import sys
import time
from typing import Final

from runner_common import (MARK_GAME_OVER, MARK_GAME_START, common_args,
                           describe_exit, find_exe, is_crash_code, log_dir_for,
                           log_has_smart_ai_failure, qt_console_env,
                           resolve_workdir, spawn, stamp, tail_lines,
                           terminate_tree, wait_exit, wait_port, write_csv)

SERVER_EXE = "qsanguosha_server"
CLIENT_EXE = "QSanguosha"
DEFAULT_SERVER_PORT = 9527  # 與 config.ini ServerPort 一致
SERVER_STARTUP_TIMEOUT = 60   # 等 server 就緒 (秒)
GAME_TIMEOUT: Final[int] = int(
    os.environ.get("QSAN_NETWORK_GAME_TIMEOUT", "3600")
)  # 可由環境變數覆寫的單局有界上限 (秒)
CLIENT_JOIN_TIMEOUT = 120     # 等 client 連上並開局 (秒)


def read_markers(log_path, offset):
    """讀標記檔, 回傳 (新 offset, [自 offset 起的新增行])。"""
    if not os.path.isfile(log_path):
        return offset, []
    with open(log_path, "rb") as f:
        f.seek(offset)
        data = f.read()
    new_offset = offset + len(data)
    return new_offset, data.decode("utf-8", errors="replace").splitlines()


def wait_for_marker(log_path, predicate, timeout, start_offset=0, server_proc=None):
    offset = start_offset
    deadline = time.time() + timeout
    while time.time() < deadline:
        # 自動化測試: 等待期間監控 server 存活 — 閃退時提前回傳 SERVER_DIED
        if server_proc is not None and server_proc.poll() is not None:
            return "SERVER_DIED", offset
        offset, lines = read_markers(log_path, offset)
        for line in lines:
            if predicate(line):
                return line, offset
        time.sleep(0.3)
    return None, offset


def _backup_runtime_files(workdir, run_dir, run_id):
    """局開始前複製會被覆寫的執行期記錄檔 (閃退局證據來源)。
    對應: record/debug.txt, lua/ai/cstring, lua/ai/cstringEvent。"""
    targets = [
        (os.path.join(workdir, "record", "debug.txt"),
         os.path.join(run_dir, "debug-before-run%d.txt" % run_id)),
        (os.path.join(workdir, "lua", "ai", "cstring"),
         os.path.join(run_dir, "ai-cstring-before-run%d.txt" % run_id)),
        (os.path.join(workdir, "lua", "ai", "cstringEvent"),
         os.path.join(run_dir, "ai-cstringEvent-before-run%d.txt" % run_id)),
    ]
    for src, dst in targets:
        try:
            if os.path.isfile(src) and os.path.getsize(src) > 0:
                shutil.copy2(src, dst)
        except OSError as e:
            print("  [WARN] 複製 %s 失敗: %s" % (os.path.basename(src), e))


def server_command(server_exe, mode, marker_file, port):
    return [server_exe, "--port", str(port), "--websocket-port", "0",
            "--game-mode", mode, "--autotest-log", marker_file]


def restart_server(args, exe_root, workdir, mode, proc, marker_file, server_log, server_exe, reason):
    """重啟常駐 server, 回傳新 proc。reason 用於 log 說明。"""
    exit_code = proc.poll()
    print("  %s (server exit=%s %s)" % (
        reason, exit_code, describe_exit(exit_code) if exit_code is not None else ""))
    terminate_tree(proc)
    close_proc(proc)
    if os.path.isfile(marker_file):
        os.remove(marker_file)  # 標記檔是 Append, 新 server 需從乾淨檔開始
    port = args.port
    proc = spawn(server_command(server_exe, mode, marker_file, port),
                 workdir, server_log,
                 console=getattr(args, "console", False))
    if not wait_port(port, SERVER_STARTUP_TIMEOUT, proc=proc):
        print("  [FAIL] %s: 重啟 server 未就緒" % mode)
        return None
    print("  server 已重啟 (port %d)" % port)
    return proc


def run_mode(args, exe_root, workdir, mode, runs, general):
    # 每次執行一個時間戳資料夾 (network/<時間戳>/<mode>/), 不再重名覆蓋
    run_dir = os.path.join(log_dir_for(args), "network", stamp(), mode)
    os.makedirs(run_dir, exist_ok=True)
    marker_file = os.path.join(run_dir, "autotest.log")

    server_exe = find_exe(exe_root, SERVER_EXE)
    client_exe = find_exe(exe_root, CLIENT_EXE)

    results = []
    server_log = os.path.join(run_dir, "server.log")
    port = args.port
    print("=== 模式 %s: 啟動 server ===" % mode)
    proc = spawn(server_command(server_exe, mode, marker_file, port),
                 workdir, server_log,
                 console=getattr(args, "console", False))
    try:
        if not wait_port(port, SERVER_STARTUP_TIMEOUT, proc=proc):
            code = wait_exit(proc, 5)
            print("[FAIL] %s: server 未就緒 (exit=%s)" % (mode, code))
            return [{"run": "-", "ok": False, "note": "server startup failed"}]
        print("  server 就緒 (port %d)" % port)

        marker_offset = 0
        for run_id in range(1, runs + 1):
            client_log = os.path.join(run_dir, "run%d.log" % run_id)
            # 自動化測試: 閃退局沒有完整 record; 唯一即時記錄是
            # <workdir>/record/debug.txt 與 lua/ai/cstring{,Event},
            # 下局開始即被覆寫。在 spawn 新 client 前各複製一份,
            # 保存上一局的遊戲/AI 內容。
            _backup_runtime_files(workdir, run_dir, run_id)
            print("  局 %d/%d: 啟動 client" % (run_id, runs))
            client_cmd = [client_exe, "-connect:127.0.0.1:%d" % port,
                          "--test-general", general]
            if getattr(args, "general2", ""):
                client_cmd += ["--test-general2", args.general2]
            client_cmd += ["--auto-robots"]
            # GUI client 的 qDebug/qWarning 導向 runN.log (QT_LOGGING_TO_CONSOLE)
            client = spawn(client_cmd, workdir, client_log, env=qt_console_env())

            start_line, marker_offset = wait_for_marker(
                marker_file, lambda l: MARK_GAME_START in l,
                CLIENT_JOIN_TIMEOUT, marker_offset, server_proc=proc)
            ccode = None
            if start_line == "SERVER_DIED":
                # 自動化測試: server 閃退 — 重啟後重試本局
                terminate_tree(client)
                close_proc(client)
                ctx = tail_lines(marker_file, 20)
                for line in ctx:
                    print("          %s" % line)
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe,
                                      "server 閃退, 重啟後重試本局")
                if proc is None:
                    break
                marker_offset = 0
                run_id -= 1  # 本局重試
                time.sleep(1)
                continue
            if start_line is None:
                ccode = wait_exit(client, 5)
                if ccode is None:
                    terminate_tree(client)
                close_proc(client)
                note = "no game start (client exit=%s %s)" % (
                    ccode, describe_exit(ccode) if ccode is not None else "")
                results.append({"run": run_id, "ok": False,
                                "note": note, "exit_name": describe_exit(ccode)})
                print("  [FAIL] 局 %d: 未偵測到開局 (client exit=%s)" % (run_id, ccode))
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe,
                                      "未開局, 重啟 server 後繼續")
                if proc is None:
                    break
                marker_offset = 0
                time.sleep(1)
                continue

            over_line, marker_offset = wait_for_marker(
                marker_file, lambda l: MARK_GAME_OVER.search(l),
                GAME_TIMEOUT, marker_offset, server_proc=proc)
            if over_line == "SERVER_DIED":
                # 自動化測試: server 閃退 — 該局記失敗, 重啟後繼續下一局
                terminate_tree(client)
                close_proc(client)
                ctx = tail_lines(marker_file, 20)
                for line in ctx:
                    print("          %s" % line)
                results.append({"run": run_id, "ok": False,
                                "note": "server crashed mid-game", "exit_name": "server"})
                print("  [FAIL] 局 %d: server 局中閃退" % run_id)
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe,
                                      "server 閃退, 重啟後繼續")
                if proc is None:
                    break
                marker_offset = 0
                time.sleep(1)
                continue
            # 先確認 client 是否已自行閃退, 再殺 (強制終止會蓋掉真正的閃退碼)
            ccode = wait_exit(client, 3)
            if ccode is None:
                terminate_tree(client)
            close_proc(client)
            crashed = is_crash_code(ccode)
            ctx = tail_lines(marker_file, 20) if crashed else []
            if over_line is None:
                results.append({"run": run_id, "ok": False, "note": "game timeout",
                                "exit_name": describe_exit(ccode)})
                print("  [FAIL] 局 %d: 對局逾時 (%ds), 已殺 client" % (run_id, GAME_TIMEOUT))
            else:
                m = MARK_GAME_OVER.search(over_line)
                winner = m.group(1) or "none"
                if crashed:
                    results.append({"run": run_id, "ok": False,
                                    "note": "winner=%s; client 閃退 %s" % (winner, describe_exit(ccode)),
                                    "exit_name": describe_exit(ccode)})
                    print("  [FAIL] 局 %d: 結束 winner=%s, 但 client 閃退 %s"
                          % (run_id, winner, describe_exit(ccode)))
                else:
                    results.append({"run": run_id, "ok": True, "note": "winner=%s" % winner,
                                    "exit_name": ""})
                    print("  [PASS] 局 %d: 結束, winner=%s" % (run_id, winner))
            if crashed:
                for line in ctx:
                    print("          %s" % line)
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe,
                                      "client 閃退, 重啟 server 後繼續")
                if proc is None:
                    break
                marker_offset = 0
                time.sleep(1)
                continue
            # 自動化測試: smart-ai 載入失敗偵測 — 常駐 server 的 Lua VM 已半壞,
            # 局間 delay 10 秒 + 重啟 server (新 server 的第一個 Room 會重載 smart-ai)
            if log_has_smart_ai_failure(marker_file) or log_has_smart_ai_failure(server_log):
                print("  smart-ai 載入失敗, delay 10s 後重啟 server 再開下局")
                time.sleep(10)
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe,
                                      "smart-ai 載入失敗")
                if proc is None:
                    break
                marker_offset = 0
            time.sleep(1)
    finally:
        # 成功同失敗路徑都行同一條有界清理, 唔會留低孤兒 server。
        terminate_tree(proc)
        close_proc(proc)
    return results


def close_proc(proc):
    from runner_common import close_proc as _cp
    _cp(proc)


def main():
    # log 行含中文, console 編碼 (cp950) 印不出時以 ? 取代, 避免 runner 自己炸掉
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description="QSanguosha 真實網路測試 runner")
    common_args(parser)
    parser.add_argument("--runs", type=int, default=2, help="每個模式要跑的局數 (預設 2)")
    parser.add_argument("--general", default="zhenji",
                        help="client 自動選將主將 (02_1v1 請用 x0; 預設 zhenji)")
    parser.add_argument("--general2", default="",
                        help="雙將模式副將 (空 = server 清單隨機)")
    parser.add_argument("--port", type=int, default=DEFAULT_SERVER_PORT,
                        help="server 監聽 port (預設 %d); 平行執行時請各自指定"
                             % DEFAULT_SERVER_PORT)
    parser.add_argument("--console", action="store_true",
                        help="server stdout 同步顯示在終端 (不寫 server.log, 標記檔照常)")
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
    header = ["mode", "run", "ok", "note", "exit_name"]
    write_csv(csv_path, header, [
        [r.get("mode"), r.get("run"), r.get("ok"), r.get("note"),
         r.get("exit_name", "")]
        for r in all_results
    ])
    ok = sum(1 for r in all_results if r.get("ok"))
    print("結果: %s" % csv_path)
    print("總計: %d/%d 通過" % (ok, len(all_results)))
    return 0 if ok == len(all_results) else 1


if __name__ == "__main__":
    sys.exit(main())
