# -*- coding: utf-8 -*-
"""Real-network test runner (serial).

Flow (per mode):
  1. Start qsanguosha_server.exe --game-mode <mode> --autotest-log <file> once (persistent)
  2. Spawn one GUI client per game:
     QSanguosha.exe -connect:127.0.0.1 --test-general <general> --auto-robots
     (the client picks a general, fills the AI lobby and enables trust on its own)
  3. Detect game start/end from the server's [AUTOTEST] marker files, kill the client by PID
  4. After a mode finishes, kill the server and move on to the next mode

Cross-platform: executable names, process spawn/cleanup and exit-code
interpretation all go through runner_common, so the same runner works on
Windows and Linux. The GUI client on Linux needs an available DISPLAY (WSLg
or an external Xvfb); for single-game contract validation use
gui_network_smoke.py instead — this runner's job is soak.

Usage:
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
MAX_START_RETRIES = 2         # server 開局前閃退時, 同一局最多重試次數
CLIENT_EXIT_GRACE = 10        # client 退出後等局末標記的寬限 (秒)


def read_markers(log_path, offset):
    """讀標記檔, 回傳 (新 offset, [自 offset 起的新增行])。"""
    if not os.path.isfile(log_path):
        return offset, []
    with open(log_path, "rb") as f:
        f.seek(offset)
        data = f.read()
    new_offset = offset + len(data)
    return new_offset, data.decode("utf-8", errors="replace").splitlines()


def wait_for_marker(log_path, predicate, timeout, start_offset=0, server_proc=None,
                    client_proc=None):
    offset = start_offset
    deadline = time.time() + timeout
    client_deadline = None
    while time.time() < deadline:
        # 自動化測試: 等待期間監控 server 存活 — 閃退時提前回傳 SERVER_DIED。
        # 先取存活狀態再讀標記: 死前寫下的標記仍算數, 開局後隨即閃退才不會被當成開局前閃退
        died = server_proc is not None and server_proc.poll() is not None
        client_gone = client_proc is not None and client_proc.poll() is not None
        offset, lines = read_markers(log_path, offset)
        for line in lines:
            if predicate(line):
                return line, offset
        if died:
            return "SERVER_DIED", offset
        # client 退出 (如 WSLg compositor 崩潰斷線) 時 server 仍會託管打完該局;
        # 給一段寬限等標記, 以免把「局末 client 先退出」誤判為局中斷線
        if client_gone:
            if client_deadline is None:
                client_deadline = time.time() + CLIENT_EXIT_GRACE
            elif time.time() >= client_deadline:
                return "CLIENT_DIED", offset
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
        run_id = 0
        start_retries = 0
        while run_id < runs:
            if start_retries == 0:
                run_id += 1
            # 重試用獨立檔名, 保留閃退那次嘗試的 client log
            client_log = os.path.join(run_dir, "run%d%s.log" % (
                run_id, "-retry%d" % start_retries if start_retries else ""))
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
                # 自動化測試: server 閃退 — 記一筆失敗, 重啟後重試本局 (有上限)
                terminate_tree(client)
                close_proc(client)
                ctx = tail_lines(marker_file, 20)
                for line in ctx:
                    print("          %s" % line)
                results.append({"run": run_id, "ok": False,
                                "note": "server crashed before game start (attempt %d)"
                                        % (start_retries + 1),
                                "exit_name": "server"})
                if start_retries < MAX_START_RETRIES:
                    start_retries += 1
                    reason = "server 閃退, 重啟後重試本局 (%d/%d)" % (
                        start_retries, MAX_START_RETRIES)
                else:
                    start_retries = 0
                    print("  [FAIL] 局 %d: server 開局前連續閃退, 放棄本局" % run_id)
                    reason = "server 開局前連續閃退, 重啟後繼續下一局"
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe, reason)
                if proc is None:
                    break
                marker_offset = 0
                time.sleep(1)
                continue
            start_retries = 0
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

            game_started_at = time.time()
            over_line, marker_offset = wait_for_marker(
                marker_file, lambda l: MARK_GAME_OVER.search(l),
                GAME_TIMEOUT, marker_offset, server_proc=proc, client_proc=client)
            lost_note = ""
            if over_line == "CLIENT_DIED":
                # 自動化測試: client 局中退出 — server 會託管該座位打完;
                # 記下斷線, 等局末再開下一局, 以免新 client 撞上仍在進行的房間
                lost_code = client.poll()
                lost_note = "client 局中退出 exit=%s %s" % (lost_code, describe_exit(lost_code))
                print("  [WARN] 局 %d: %s, 等 server 打完本局" % (run_id, lost_note))
                remaining = max(1, GAME_TIMEOUT - (time.time() - game_started_at))
                over_line, marker_offset = wait_for_marker(
                    marker_file, lambda l: MARK_GAME_OVER.search(l),
                    remaining, marker_offset, server_proc=proc)
            if over_line == "SERVER_DIED":
                # 自動化測試: server 閃退 — 該局記失敗, 重啟後繼續下一局
                terminate_tree(client)
                close_proc(client)
                ctx = tail_lines(marker_file, 20)
                for line in ctx:
                    print("          %s" % line)
                results.append({"run": run_id, "ok": False,
                                "note": "; ".join(n for n in ("server crashed mid-game", lost_note) if n),
                                "exit_name": "server"})
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
                results.append({"run": run_id, "ok": False,
                                "note": "; ".join(n for n in ("game timeout", lost_note) if n),
                                "exit_name": describe_exit(ccode)})
                print("  [FAIL] 局 %d: 對局逾時 (%ds), 已殺 client" % (run_id, GAME_TIMEOUT))
            else:
                m = MARK_GAME_OVER.search(over_line)
                winner = m.group(1) or "none"
                if lost_note and not crashed:
                    results.append({"run": run_id, "ok": False,
                                    "note": "winner=%s; %s" % (winner, lost_note),
                                    "exit_name": describe_exit(ccode)})
                    print("  [FAIL] 局 %d: 結束 winner=%s, 但 %s" % (run_id, winner, lost_note))
                elif crashed:
                    results.append({"run": run_id, "ok": False,
                                    "note": "winner=%s; client 閃退 %s" % (winner, describe_exit(ccode)),
                                    "exit_name": describe_exit(ccode)})
                    print("  [FAIL] 局 %d: 結束 winner=%s, 但 client 閃退 %s"
                          % (run_id, winner, describe_exit(ccode)))
                else:
                    results.append({"run": run_id, "ok": True, "note": "winner=%s" % winner,
                                    "exit_name": ""})
                    print("  [PASS] 局 %d: 結束, winner=%s" % (run_id, winner))
            if crashed or over_line is None:
                for line in ctx:
                    print("          %s" % line)
                # A timed-out room keeps running on the server with the killed client's
                # seat held for reconnect, so the next client's signup is rejected as a
                # duplicate screen name until that room ends. Restart the server instead.
                reason = ("client 閃退, 重啟 server 後繼續" if crashed
                          else "對局逾時, 重啟 server 後繼續")
                proc = restart_server(args, exe_root, workdir, mode, proc,
                                      marker_file, server_log, server_exe, reason)
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
        # Success and failure paths both run the same bounded cleanup; no orphan server is left behind.
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
    parser.set_defaults(modes="10p,20p,02_1v1,05p,06_3v3,04_1v3")
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
