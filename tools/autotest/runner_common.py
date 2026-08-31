# -*- coding: utf-8 -*-
"""autotest 共用工具: 執行檔定位、子行程 spawn、日誌解析、結果統計。

跨平台: Windows 與 Linux 共用同一套遊戲流程與判定邏輯, 只有「執行檔名、
process 啟動方式、process tree 清理、exit code 解讀」四件事按平台分歧, 全部
集中在本檔, runner 本身不再直接呼叫 taskkill 或假設 .exe 副檔名。
"""
import argparse
import csv
import os
import re
import signal
import socket
import subprocess
import time
from collections import Counter, defaultdict

IS_WINDOWS = os.name == "nt"

# ── 執行檔定位 ──────────────────────────────────────────────

# Windows 的多組態產生器輸出到 release\ / debug\; Linux 的 CMakePresets 用
# relwithdebinfo/ (CI 基線) 或 debug/。次序 = 優先次序: 先 release, 再
# RelWithDebInfo, 最後先至係 debug, 免得倉庫入面一份舊 debug build 靜靜蓋過
# 剛剛編好的 CI 組態。
_EXE_SUBDIRS = ("release", "relwithdebinfo", "RelWithDebInfo", "debug", "")


def executable_name(base):
    """把不帶副檔名的程式名轉成本平台的執行檔名。"""
    if base.endswith(".exe"):
        base = base[:-4]
    return base + ".exe" if IS_WINDOWS else base


def find_exe(exe_root, name):
    """依序搜尋 <root>/{release,debug,relwithdebinfo,} 下的執行檔。

    name 可帶或不帶 .exe; 實際搜尋的檔名一律按本平台正規化, 令同一份 runner
    設定可以直接在 Windows 與 Linux 上重用。"""
    resolved = executable_name(name)
    candidates = [os.path.join(exe_root, sub, resolved) if sub
                  else os.path.join(exe_root, resolved)
                  for sub in _EXE_SUBDIRS]
    for path in candidates:
        if os.path.isfile(path) and os.access(path, os.X_OK if not IS_WINDOWS else os.F_OK):
            return path
    raise FileNotFoundError(
        "找不到執行檔 %s (搜尋過: %s)" % (resolved, ", ".join(candidates))
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


# ── TCP port ────────────────────────────────────────────────

def free_tcp_port(host="127.0.0.1"):
    """向 OS 借一個當下空閒的 TCP port 並立即歸還。

    平行 CI job 之間不可共用固定 port; 借-還之間仍有理論上的 race, 但 runner
    緊接著就會啟動 server 並確認它真的 listen, 搶不到會即時失敗而不是靜靜掛住。"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return sock.getsockname()[1]


def port_open(port, host="127.0.0.1", timeout=0.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_port(port, timeout, host="127.0.0.1", proc=None):
    """等 port 進入 listen; proc 提供時, 行程提早死亡即刻回 False。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False
        if port_open(port, host):
            return True
        time.sleep(0.2)
    return False


def wait_port_released(port, timeout, host="127.0.0.1"):
    """等 port 不再有人 listen (確認 server 真的放開了 socket)。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not port_open(port, host):
            return True
        time.sleep(0.2)
    return not port_open(port, host)

# ── 日誌標記解析 ────────────────────────────────────────────

# Windows 常見 exit code 翻譯 (閃退摘要用)
EXIT_NAMES = {
    0xC0000005: "STATUS_ACCESS_VIOLATION (存取違規)",
    0xC0000409: "STATUS_STACK_BUFFER_OVERRUN (fail-fast / GS cookie)",
    0xC00000FD: "STATUS_STACK_OVERFLOW (堆疊溢位)",
    0xC0000094: "STATUS_INTEGER_DIVIDE_BY_ZERO",
    0xC0000374: "STATUS_HEAP_CORRUPTION",
    0xC000001D: "STATUS_ILLEGAL_INSTRUCTION",
    0xC000000D: "STATUS_INVALID_CRUNTIME_PARAMETER",
    0x80000003: "STATUS_BREAKPOINT",
    0xC0000135: "STATUS_DLL_NOT_FOUND",
    0xC0000139: "STATUS_ENTRYPOINT_NOT_FOUND",
}

def hex_exit(code):
    """exit code 轉 16 進位表示 (Python 回傳帶符號)。"""
    if code is None:
        return "n/a"
    return "0x%08X" % (code & 0xFFFFFFFF)

# POSIX: Popen.returncode 對被訊號殺死的行程回傳 -N (N = 訊號編號)。
#
# 用 getattr 而唔係直接寫 signal.SIGBUS: Windows 的 signal module 冇 SIGBUS,
# 直接 attribute access 會喺 import 期間就 AttributeError, 令每一個 import 呢個
# module 的 Windows runner (headless smoke、CTest 的 runner 契約測試) 即刻死,
# 同 exit code 翻譯本身完全無關。
_POSIX_CRASH_SIGNALS = {
    getattr(signal, name)
    for name in ("SIGILL", "SIGABRT", "SIGFPE", "SIGSEGV", "SIGBUS")
    if hasattr(signal, name)
}


def describe_exit(code):
    """exit code 翻譯。Windows 給 NTSTATUS 名, POSIX 給訊號名。"""
    if code is None:
        return "n/a"
    if not IS_WINDOWS:
        if code == 0:
            return "0 (正常結束)"
        if code < 0:
            try:
                name = signal.Signals(-code).name
            except ValueError:
                name = "SIG%d" % -code
            return "%s (被訊號終止)" % name
        return "%d (應用程式退出碼, 非崩潰)" % code
    h = code & 0xFFFFFFFF
    if h == 0:
        return "0 (正常結束)"
    if h >= 0x80000000:
        return EXIT_NAMES.get(h, hex_exit(h))
    return "%s (應用程式退出碼, 非崩潰)" % hex_exit(h)

def is_crash_code(code):
    """判斷 exit code 是否代表閃退。

    Windows: NTSTATUS 高位 0xC0000000+ 或 0x80000003 斷點。
    POSIX: 被 SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE 終止 (returncode 為 -N)。
    兩邊的 exit=1 等小值都是應用程式自行 return, 不是閃退。"""
    if code is None:
        return False
    if not IS_WINDOWS:
        return code < 0 and -code in {int(s) for s in _POSIX_CRASH_SIGNALS}
    h = code & 0xFFFFFFFF
    return h == 0x80000003 or h >= 0xC0000000

def tail_lines(path, n=20):
    """讀檔尾最後 n 行 (檔案可能數百 KB, 只讀尾部 64KB)。"""
    if not os.path.isfile(path):
        return []
    with open(path, "rb") as f:
        f.seek(0, 2)
        size = f.tell()
        if size == 0:
            return []
        chunk = min(size, 65536)
        f.seek(size - chunk)
        data = f.read().decode("utf-8", errors="replace")
    lines = data.splitlines()
    if size > chunk and lines:
        lines = lines[1:]  # 第一行可能被截斷
    return lines[-n:]

# smart-ai / Lua AI 載入失敗的 log 標記 (runner 偵測用)
SMART_AI_FAIL_MARKERS = (
    "LuaAI script failed to load",
    "LuaAI 载失败",
    "Lua state is null",
    "AI script load failed",
    "Scenario AI load failed",
)

def log_has_smart_ai_failure(path):
    """log 檔是否含 smart-ai 載入失敗標記。
    失敗發生在啟動階段 (標記檔頭部), 讀尾部 256KB 已足夠涵蓋。"""
    if not os.path.isfile(path):
        return False
    with open(path, "rb") as f:
        f.seek(0, 2)
        size = f.tell()
        if size == 0:
            return False
        chunk = min(size, 262144)
        f.seek(size - chunk)
        data = f.read().decode("utf-8", errors="replace")
    return any(m in data for m in SMART_AI_FAIL_MARKERS)

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

def _group_kwargs():
    """讓子行程自成一個 process group / job, 之後可以整棵樹一次過清理。

    GUI client 會再開 helper (Linux 上 xvfb-run 會開 Xvfb), 只殺直接子行程會
    留下孤兒污染下一個 CI step。"""
    if IS_WINDOWS:
        return {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
    return {"start_new_session": True}


def spawn(cmd, cwd, log_path, console=False, env=None):
    """spawn 子行程, stdout/stderr 合流寫入 log_path, 回傳 Popen。
    console=True 時輸出直接繼承終端 (不寫 log), 供 --console 模式用。
    env 提供時作為子行程環境 (如 QT_LOGGING_TO_CONSOLE 讓 GUI client 的
    qDebug/qWarning 進 log, 否則 Windows GUI 子系統訊息走 OutputDebugString)。

    子行程一律自成 process group, 清理走 terminate_tree()。"""
    kwargs = dict(_group_kwargs())
    if console:
        proc = subprocess.Popen(
            cmd, cwd=cwd, stdout=None, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
            env=env, **kwargs,
        )
        proc._logfile = None  # type: ignore[attr-defined]
        return proc
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    logf = open(log_path, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen(
        cmd, cwd=cwd, stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
        env=env, **kwargs,
    )
    proc._logfile = logf  # type: ignore[attr-defined]
    return proc

def qt_console_env():
    """GUI 子系統 exe 的 Qt 訊息 (qDebug/qWarning) 導向 stderr 的環境。
    QT_LOGGING_TO_CONSOLE 已棄用; 改用 QT_ASSUME_STDERR_HAS_CONSOLE +
    QT_FORCE_STDERR_LOGGING (Qt 6 建議組合)。"""
    env = dict(os.environ)
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    return env

def close_proc(proc):
    logf = getattr(proc, "_logfile", None)
    if logf:
        try:
            logf.close()
        except Exception:
            pass

def kill_pid(pid):
    """精準結束指定 PID (不依賴同名映像, 避免誤殺)。"""
    if IS_WINDOWS:
        subprocess.run(
            ["taskkill", "/F", "/PID", str(pid)], capture_output=True, timeout=30
        )
        return
    try:
        os.kill(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def _kill_tree(pid, sig):
    """對整個 process group 送訊號 (POSIX) 或整棵 process tree (Windows)。"""
    if IS_WINDOWS:
        flags = ["/T", "/F"] if sig == "kill" else ["/T"]
        subprocess.run(["taskkill", *flags, "/PID", str(pid)],
                       capture_output=True, timeout=30)
        return
    try:
        os.killpg(os.getpgid(pid), sig)
    except (ProcessLookupError, PermissionError, OSError):
        pass


def _send_windows_console_break(process_group_id):
    """向 CREATE_NEW_PROCESS_GROUP 子行程發送可攔截的 CTRL_BREAK_EVENT。"""
    import ctypes
    from ctypes import wintypes

    generate_console_ctrl_event = ctypes.WinDLL(
        "kernel32", use_last_error=True).GenerateConsoleCtrlEvent
    generate_console_ctrl_event.argtypes = (wintypes.DWORD, wintypes.DWORD)
    generate_console_ctrl_event.restype = wintypes.BOOL
    return bool(generate_console_ctrl_event(1, process_group_id))


def request_shutdown(proc):
    """要求行程自行收工 (POSIX: SIGTERM; Windows: CTRL_BREAK_EVENT)。

    dedicated server 會在主事件迴圈處理對應的 shutdown flag 並走 app.quit()，
    所以「乾淨關機」與「被斬」是分得清的兩件事。發送 Windows control event
    失敗時保留原有 taskkill 後備，後續 timeout 仍會強制清理。"""
    if proc is None or proc.poll() is not None:
        return
    if IS_WINDOWS:
        try:
            if _send_windows_console_break(proc.pid):
                return
        except (AttributeError, OSError):
            pass
        _kill_tree(proc.pid, "term")
        return
    _kill_tree(proc.pid, signal.SIGTERM)


def terminate_tree(proc, graceful_timeout=10, kill_timeout=5):
    """有界的三段式清理: 先請求收工, 再 terminate, 最後 kill 整棵樹。

    回傳 (exit_code, how)，how ∈ {"already", "graceful", "terminated", "killed",
    "unresponsive"}; 成功與失敗路徑都應該呼叫它, 才不會留下孤兒。"""
    if proc is None:
        return None, "already"
    if proc.poll() is not None:
        code = proc.poll()
        _kill_tree(proc.pid, "kill")  # 收拾可能仍在的孫行程
        return code, "already"

    request_shutdown(proc)
    code = wait_exit(proc, graceful_timeout)
    if code is not None:
        _kill_tree(proc.pid, "kill")
        return code, "graceful"

    _kill_tree(proc.pid, "kill" if IS_WINDOWS else signal.SIGTERM)
    code = wait_exit(proc, kill_timeout)
    if code is not None:
        return code, "terminated"

    _kill_tree(proc.pid, "kill" if IS_WINDOWS else signal.SIGKILL)
    code = wait_exit(proc, kill_timeout)
    return code, "killed" if code is not None else "unresponsive"


def process_group_alive(proc):
    """process group 內是否仍有行程存活 (孤兒偵測)。

    POSIX 用 killpg(0) 探測; Windows 沒有等價的便宜查詢, 只能看行程本身。"""
    if proc is None:
        return False
    if IS_WINDOWS:
        return proc.poll() is None
    try:
        os.killpg(os.getpgid(proc.pid), 0)
        return True
    except (ProcessLookupError, PermissionError, OSError):
        return False

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
                        help="log 輸出目錄 (預設: <exe-root>/tools/autotest/autotest-logs)")
    parser.add_argument("--modes", default="10p,20p,02_1v1,05p",
                        help="遊戲模式, 逗號分隔 (預設: 10p,20p,02_1v1,05p)")
    parser.add_argument("--label", default="",
                        help="結果檔名標籤")
    return parser

def log_dir_for(args):
    """log 目錄: 集中於 tools/autotest/autotest-logs (不再散在 exe 根)。"""
    if args.log_dir:
        return args.log_dir
    return os.path.join(args.exe_root, "tools", "autotest", "autotest-logs")

def stamp():
    return time.strftime("%Y%m%d-%H%M%S")
