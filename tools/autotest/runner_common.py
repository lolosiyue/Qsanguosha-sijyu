# -*- coding: utf-8 -*-
"""autotest 共用工具: 執行檔定位、子行程 spawn、日誌解析、結果統計。"""
import argparse
import csv
import os
import re
import subprocess
import time
from collections import Counter, defaultdict

# ── 執行檔定位 ──────────────────────────────────────────────

def find_exe(exe_root, name):
    """依序搜尋 <root>/release, <root>/debug, <root> 下的執行檔。"""
    candidates = [
        os.path.join(exe_root, "release", name),
        os.path.join(exe_root, "debug", name),
        os.path.join(exe_root, name),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    raise FileNotFoundError(
        "找不到執行檔 %s (搜尋過: %s)" % (name, ", ".join(candidates))
    )

def resolve_workdir(exe_root):
    """執行檔 cwd 需含 config.ini (QSettings 相對路徑) 與遊戲資源 (font/image)。
    release/ 僅有 Qt 插件與 lua, 沒有完整資源, 故退回倉庫根目錄。"""
    for sub in ("release", "debug"):
        path = os.path.join(exe_root, sub)
        if (os.path.isfile(os.path.join(path, "config.ini"))
                and os.path.isdir(os.path.join(path, "font"))
                and os.path.isdir(os.path.join(path, "image"))):
            return path
    return exe_root

# ── 日誌標記解析 ────────────────────────────────────────────

MARK_GAME_START = "[AUTOTEST] game start"
MARK_GAME_OVER = re.compile(r"\[AUTOTEST\] game over ?(.*)$")

HEADLESS_FINISHED = re.compile(r">>> Game (\d+) finished\. Winner: (.+) <<<")
HEADLESS_FAILED = re.compile(r">>> Game (\d+) FAILED - (.+) <<<")
HEADLESS_DONE = re.compile(r">>> All games completed\. Exiting\. <<<")

HEADLESS_HEADER = re.compile(r">>> Headless stress test started -")

def parse_headless_log(path):
    """解析 headless log 檔, 回傳 (finished: Counter[winner], failed: int, done: bool)。
    檔案是 Append 累積的, 只解析最後一次 "Headless stress test started" 之後的內容。"""
    finished = Counter()
    failed = 0
    done = False
    if os.path.isfile(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        # 取最後一個 run header 之後的行
        start = 0
        for i, line in enumerate(lines):
            if HEADLESS_HEADER.search(line):
                start = i
        for line in lines[start:]:
            m = HEADLESS_FINISHED.search(line)
            if m:
                finished[m.group(2)] += 1
                continue
            if HEADLESS_FAILED.search(line):
                failed += 1
                continue
            if HEADLESS_DONE.search(line):
                done = True
    return finished, failed, done

# ── 子行程管理 ──────────────────────────────────────────────

def spawn(cmd, cwd, log_path):
    """spawn 子行程, stdout/stderr 合流寫入 log_path, 回傳 Popen。"""
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    logf = open(log_path, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen(
        cmd, cwd=cwd, stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
    )
    proc._logfile = logf  # type: ignore[attr-defined]
    return proc

def close_proc(proc):
    logf = getattr(proc, "_logfile", None)
    if logf:
        try:
            logf.close()
        except Exception:
            pass

def kill_pid(pid):
    """精準結束指定 PID (不依賴同名映像, 避免誤殺)。"""
    subprocess.run(
        ["taskkill", "/F", "/PID", str(pid)], capture_output=True, timeout=30
    )

def wait_exit(proc, timeout):
    """等待 process 結束; 逾時回傳 None。"""
    try:
        return proc.wait(timeout)
    except subprocess.TimeoutExpired:
        return None

def tail_markers(log_path, after_offset=0):
    """讀 log 檔尾部, 回傳 (offset, [marker 行])。只取 [AUTOTEST]/Game 標記行。"""
    markers = []
    if not os.path.isfile(log_path):
        return 0, markers
    with open(log_path, "rb") as f:
        f.seek(after_offset)
        data = f.read().decode("utf-8", errors="replace")
    offset = after_offset + len(data.encode("utf-8"))
    for line in data.splitlines():
        if "[AUTOTEST]" in line or line.startswith(">>> Game"):
            markers.append(line)
    return offset, markers

# ── 輸出 ────────────────────────────────────────────────────

def write_csv(path, header, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)

def common_args(parser):
    parser.add_argument("--exe-root", default=os.getcwd(),
                        help="倉庫根目錄 (預設: 目前目錄), 內含 release/")
    parser.add_argument("--log-dir", default=None,
                        help="log 輸出目錄 (預設: <exe-root>/autotest-logs)")
    parser.add_argument("--modes", default="10p,20p,02_1v1,05p",
                        help="遊戲模式, 逗號分隔 (預設: 10p,20p,02_1v1,05p)")
    parser.add_argument("--label", default="",
                        help="結果檔名標籤")
    return parser

def log_dir_for(args):
    return args.log_dir or os.path.join(args.exe_root, "autotest-logs")

def stamp():
    return time.strftime("%Y%m%d-%H%M%S")
