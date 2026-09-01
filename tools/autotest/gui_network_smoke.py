#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Linux GUI M2: 一局真實 TCP 網絡對局的 GUI runtime smoke。

為何唔直接擴 network_runner.py
------------------------------
`network_runner.py` 的責任是 **soak**：常駐 server、每個模式連跑 N 局、遇到閃退
就重啟 server 重試、以「通過局數 / 總局數」作結論。M2 要的是相反的東西——**一局
固定 seed 的合約驗證**：任何一個 stage 缺失即失敗、禁止 retry-until-pass、必須
確認 client/server 各自乾淨退出、必須留低結構化 result。把兩種語意塞進同一個
runner 只會令「retry 直到偶然 PASS」變成一個 flag 之遙。

真正共用的部分（執行檔定位、跨平台 spawn / process-tree 清理 / exit code 解讀、
空閒 port、log 標記解析）已經抽到 `runner_common.py`，兩個 runner 都用同一份；
本檔只保留 M2 專屬的流程與判定。

流程
----
    1. 借一個空閒 TCP port
    2. 起 qsanguosha_server --port P --game-mode M --seed S --autotest-log ...
    3. 等 port 真的 listen
    4. 起 GUI client（可選 Xvfb）：
           QSanguosha -connect:127.0.0.1:P --auto-robots
                      --network-ui-smoke --network-ui-smoke-result <json>
       client 自己會跑到 game over 然後正常退出
    5. 驗 client 的 NETWORK_UI_STAGE / NETWORK_UI_RESULT 契約
    6. 驗 server 的 [AUTOTEST] game start / game over 標記
    7. 有界地請 server 收工（POSIX SIGTERM），確認冇孤兒、port 已釋放
    8. 寫低結構化 summary

用法：
    python3 tools/autotest/gui_network_smoke.py \\
        --exe-root . --mode 02p --seed 20260828 \\
        --artifact-dir gui-network-artifacts --xvfb
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from runner_common import (IS_WINDOWS, MARK_GAME_OVER, MARK_GAME_START,  # noqa: E402
                           describe_exit, find_exe, free_tcp_port, is_crash_code,
                           process_group_alive, resolve_workdir, spawn,
                           tail_lines, terminate_tree, wait_exit, wait_port,
                           wait_port_released)

SERVER_EXE = "qsanguosha_server"
CLIENT_EXE = "QSanguosha"

STAGE_MARKER = "NETWORK_UI_STAGE"
RESULT_MARKER = "NETWORK_UI_RESULT"
SCHEMA_VERSION = 1

# 必須全部出現且 ok=true，次序同 NetworkUiSmokeReport::stageOrder() 一致。
REQUIRED_STAGES = (
    "connected",
    "signed_up",
    "room_scene",
    "dashboard",
    "general_selected",
    "game_started",
    "game_over",
    "shutdown",
)

DEFAULT_REQUIRED_INTERACTIONS = ("choose_general", "play_phase")

# 已知的 base 缺陷。列在這裡不等於可以忽略:runner 一定照樣偵測、照樣列印、照樣
# 寫入 summary["known_base_defects"];--known-base-defect 只是把「這一項」由
# problems 降級為警告,而且每一項都有明確的復原條件。
#
# server-teardown-crash
#   現象 : 對局結束、client 正常離開之後, qsanguosha_server 在拆房時 SIGSEGV/SIGABRT。
#   證據 : Room::~Room() → GameSnapshotService::~GameSnapshotService()
#          → GlobalSnapshot::~GlobalSnapshot() → QMap<QString,QVariant>::~QMap()
#          → CardUseStruct::~CardUseStruct() → QSharedPointer<Card> deref
#          → Card::deleteLater() → CardLifetimeManager::observeCard()
#          → QObject::thread() 讀到已釋放的 Card。
#          全部 frame 都在本分支沒有改過的檔案裡; 以 M1 merge base 編出來的
#          qsanguosha_server 一樣重現; 用舊有的 --auto-robots 托管流程(完全不經
#          本分支的 UI responder)一樣重現。
#   界線 : 只有在「server 已經寫出帶勝方的 game over」而且「client 已經 exit 0」
#          之後發生的 server 崩潰才會被降級。對局途中的 server 崩潰永遠是失敗。
#   移除 : card-lifetime / GameSnapshot 的擁有權修好之後, 拿掉這個 flag 即可。
KNOWN_BASE_DEFECTS = {"server-teardown-crash"}


def parse_markers(text):
    """由 client log 抽出 stage / result marker。"""
    stages, results = [], []
    for line in text.splitlines():
        line = line.strip()
        for marker, sink in ((STAGE_MARKER, stages), (RESULT_MARKER, results)):
            prefix = marker + " "
            if not line.startswith(prefix):
                continue
            payload = line[len(prefix):].strip()
            try:
                sink.append(json.loads(payload))
            except json.JSONDecodeError as error:
                raise SystemExit("%s payload is not valid JSON: %s\n  %s"
                                 % (marker, error, payload))
            break
    return stages, results


def read_text(path):
    if not os.path.isfile(path):
        return ""
    with open(path, "rb") as handle:
        return handle.read().decode("utf-8", errors="replace")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extensions_commit(repo_root):
    """extensions/ 是 fetch 落嚟的外部內容，記錄它的來源 commit 以便重現。"""
    for candidate in (os.path.join(repo_root, "extensions"),
                      os.path.join(repo_root, "lua", "ai")):
        if not os.path.isdir(candidate):
            continue
        try:
            out = subprocess.run(["git", "-C", candidate, "rev-parse", "HEAD"],
                                 capture_output=True, text=True, timeout=15)
            if out.returncode == 0 and out.stdout.strip():
                return out.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass
    stamp = os.path.join(repo_root, "extensions", ".fetch-commit")
    if os.path.isfile(stamp):
        return read_text(stamp).strip()
    return ""


def write_server_config(path, args):
    """產生確定性的 server 設定 overlay。

    唔靠開發機留低嘅 config.ini：M2 要求同一個 seed 喺 CI 同本機得出同一局，
    所以隨機座位、雙將、作弊等會改變牌局的開關全部喺度寫死。"""
    lines = [
        "[General]",
        "RandomSeat=false",
        "Enable2ndGeneral=false",
        "EnableCheat=false",
        "FreeChoose=false",
        "EnableBasara=false",
        "EnableHegemony=false",
        "EnableSame=false",
        "EnableLuckCard=false",
        "EnableAI=true",
        "AIHumanized=false",
        "OperationNoLimit=false",
        "CountDownSeconds=0",
        "OperationTimeout=%d" % args.operation_timeout,
        "OriginAIDelay=%d" % args.ai_delay,
        "AlterAIDelayAD=false",
        "AIDelayAD=0",
        "[serverconfig]",
        "upnp=false",
        "addtolistserver=false",
        "",
    ]
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    return path


def client_environment(args, artifact_dir):
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = args.platform
    env.setdefault("QT_QUICK_BACKEND", "software")
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    # GUI 子系統的 qDebug/qWarning 導向 stderr，否則 Windows 上會走
    # OutputDebugString、log 會係空的。
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    if not IS_WINDOWS:
        runtime_dir = os.path.abspath(os.path.join(artifact_dir, "xdg-runtime"))
        os.makedirs(runtime_dir, exist_ok=True)
        os.chmod(runtime_dir, 0o700)
        env.setdefault("XDG_RUNTIME_DIR", runtime_dir)
    return env


def build_client_command(args, client_exe, port, result_path, screenshot_path):
    command = [
        client_exe,
        "-connect:127.0.0.1:%d" % port,
        "--auto-robots",
        "--network-ui-smoke",
        "--network-ui-smoke-result", result_path,
        "--network-ui-smoke-timeout-ms", str(args.client_timeout_ms),
        "--network-ui-smoke-stall-ms", str(args.stall_ms),
        "--network-ui-smoke-screenshot", screenshot_path,
    ]
    if args.effects_profile:
        command += ["--effects-profile", args.effects_profile]
    if args.xvfb:
        # -a 自動揀空閒 display number，令平行 CI job 唔會爭同一個 :99。
        command = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24"] + command
    return command


def evaluate(args, summary, stages, results, client_code, server_markers):
    problems = []
    downgraded = summary.setdefault("known_base_defects", [])
    allowed = set(getattr(args, "known_base_defect", []) or [])
    stage_status = {}
    for stage in stages:
        name = stage.get("stage")
        # 同一個 stage 只可以報一次；重複代表契約壞咗。
        if name in stage_status:
            problems.append("stage %r was reported more than once" % name)
        stage_status[name] = bool(stage.get("ok"))

    if not results:
        problems.append(
            "no %s line - the client never reached a conclusion (crash, killed by "
            "the runner timeout, or the marker contract broke)" % RESULT_MARKER)
    elif len(results) > 1:
        problems.append("expected exactly one %s line, got %d"
                        % (RESULT_MARKER, len(results)))

    result = results[-1] if results else {}
    if result:
        if result.get("schema_version") != SCHEMA_VERSION:
            problems.append("unexpected result schema_version %r (this runner "
                            "implements %d)" % (result.get("schema_version"), SCHEMA_VERSION))
        if result.get("exit_code") != client_code:
            problems.append("client exit code %r disagrees with the marker's "
                            "exit_code %r" % (client_code, result.get("exit_code")))
        if not result.get("ok"):
            problems.append("client reported failure at stage %r (reason=%r): %r"
                            % (result.get("stage"), result.get("reason"),
                               result.get("error")))

    if client_code != 0:
        problems.append("expected the client to exit 0, got %s (%s)"
                        % (client_code, describe_exit(client_code)))
    if is_crash_code(client_code):
        problems.append("the client crashed: %s" % describe_exit(client_code))

    for stage in REQUIRED_STAGES:
        if stage not in stage_status:
            problems.append("stage %r was never reported" % stage)
        elif not stage_status[stage]:
            problems.append("stage %r reported ok=false" % stage)

    # M2B-B: 要求咗邊個 profile 就一定要真係行嗰個。默默退返 full 會令
    # 「NONE 完成一局」變成一個只證明咗 full 嘅綠色 job。
    effects = (result.get("effects") or {})
    summary["effects"] = {
        "requested": args.effects_profile,
        "resolved": effects.get("profile"),
        "source": effects.get("source"),
        "counters": result.get("effects_counters") or {},
        "completion": result.get("effects_completion") or {},
    }
    if args.effects_profile:
        if effects.get("profile") != args.effects_profile:
            problems.append("asked for effects profile %r but the client resolved %r"
                            % (args.effects_profile, effects.get("profile")))
        elif effects.get("source") != "cli":
            problems.append("--effects-profile did not become the resolution source "
                            "(got %r)" % effects.get("source"))
        if args.effects_profile == "none":
            # NONE 嘅硬性定義:一局打完都唔准建立呢啲物件。
            counters = result.get("effects_counters") or {}
            for key in ("spine_items", "movie_objects", "qml_overlays", "video_objects"):
                created = counters.get(key)
                if isinstance(created, (int, float)) and created > 0:
                    problems.append("effects profile 'none' created %d %s during the game"
                                    % (int(created), key))

    if not server_markers["game_start"]:
        problems.append("the server never logged '%s'" % MARK_GAME_START)
    if not server_markers["game_over"]:
        problems.append("the server never logged '[AUTOTEST] game over'")

    responder = summary.get("responder") or {}
    covered = set((responder.get("interactions") or {}).keys())
    for required in args.require_interactions:
        if required not in covered:
            problems.append("required interaction %r never reached the client UI "
                            "(covered: %s)" % (required, ", ".join(sorted(covered)) or "none"))
    if responder.get("trustee_engaged") and not args.allow_trustee_fallback:
        problems.append("the client fell back to trustee (%s); the UI responder "
                        "must answer every request unless "
                        "--allow-trustee-fallback is set"
                        % responder.get("trustee_reason"))

    lifecycle = summary["lifecycle"]
    server_issues = []
    # dedicated server 係常駐的:對局完咗佢應該仍然企喺度,等我哋開口先收工。
    # "already" 代表佢喺我哋出聲之前就已經走咗 —— 即係佢自己死咗。
    if lifecycle["server_shutdown"] != "graceful":
        server_issues.append("the server did not shut down cleanly on request (%s, exit=%s)"
                             % (lifecycle["server_shutdown"],
                                describe_exit(lifecycle["server_exit"])))
    if is_crash_code(lifecycle["server_exit"]):
        server_issues.append("the server crashed: %s"
                             % describe_exit(lifecycle["server_exit"]))

    # 只有「完整打完一局、client 亦已經乾淨退出」之後的 server 崩潰,先至符合
    # server-teardown-crash 的形狀。對局途中死掉的 server 永遠是失敗。
    teardown_only = (server_markers["game_over"] not in (None, "none")
                     and client_code == 0)
    if server_issues and "server-teardown-crash" in allowed and teardown_only:
        downgraded.append({"defect": "server-teardown-crash",
                           "detail": server_issues,
                           "server_exit": lifecycle["server_exit"]})
    else:
        problems += server_issues
    if lifecycle["orphans"]:
        problems.append("processes survived the smoke: %s" % lifecycle["orphans"])
    if not lifecycle["port_released"]:
        problems.append("TCP port %d was still accepting connections after shutdown"
                        % summary["run"]["port"])

    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe-root", default=os.getcwd(),
                        help="倉庫根目錄 (預設: 目前目錄)")
    parser.add_argument("--workdir", default=None,
                        help="子行程 cwd (預設: 自動由 --exe-root 推導)")
    parser.add_argument("--server-exe", default=None,
                        help="直接指定 server 執行檔 (預設: 由 --exe-root 搜尋)")
    parser.add_argument("--client-exe", default=None,
                        help="直接指定 GUI client 執行檔 (預設: 由 --exe-root 搜尋)")
    parser.add_argument("--mode", required=True, help="遊戲模式 ID (例: 02p / 05p)")
    parser.add_argument("--seed", required=True,
                        help="固定遊戲 seed (unsigned 十進位整數)")
    parser.add_argument("--artifact-dir", default="gui-network-artifacts")
    parser.add_argument("--label", default=None, help="artifact 檔名前綴 (預設: 模式名)")
    parser.add_argument("--platform", default="xcb", choices=("xcb", "offscreen"),
                        help="Qt platform plugin (預設: xcb)")
    parser.add_argument("--xvfb", dest="xvfb", action="store_true", default=False,
                        help="用 xvfb-run 起 client (CI 用)")
    parser.add_argument("--no-xvfb", dest="xvfb", action="store_false",
                        help="用現有 DISPLAY 起 client (WSLg / 本機桌面)")
    parser.add_argument("--process-timeout", type=int, default=900,
                        help="runner 層的 client 上限 (秒, 預設 900)")
    parser.add_argument("--client-timeout-ms", type=int, default=600000,
                        help="client 內部的 smoke 上限 (毫秒, 預設 600000)")
    parser.add_argument("--stall-ms", type=int, default=20000,
                        help="單一 request 未經 UI 回覆的容忍時間 (毫秒)")
    parser.add_argument("--server-startup-timeout", type=int, default=120,
                        help="等 server listen 的上限 (秒)")
    parser.add_argument("--operation-timeout", type=int, default=15,
                        help="server 端每次操作的倒數 (秒)")
    parser.add_argument("--ai-delay", type=int, default=0,
                        help="server AI 思考延遲 (毫秒, CI 用 0)")
    parser.add_argument("--require-interactions",
                        default=",".join(DEFAULT_REQUIRED_INTERACTIONS),
                        help="必須經真 UI 覆過的互動名, 逗號分隔")
    parser.add_argument("--allow-trustee-fallback", action="store_true",
                        help="容許 responder 中途切 trustee (預設: 視為失敗)")
    parser.add_argument("--effects-profile", default=None,
                        choices=("full", "reduced", "none"),
                        help="M2B-B: 用邊個效果 profile 跑呢一局。三個 profile 必須有"
                             "完全相同嘅遊戲規則同網絡回覆,所以呢個 flag 唔准改任何"
                             "通過條件——只係換咗畫面上做啲乜。")
    parser.add_argument("--known-base-defect", action="append", default=[],
                        choices=sorted(KNOWN_BASE_DEFECTS),
                        help="把指定的已知 base 缺陷降級為警告 (仍然會偵測、列印同"
                             "寫入 summary)。只可以用喺已經對照過 base 並且證實"
                             "同本分支無關的缺陷")
    args = parser.parse_args()

    args.require_interactions = [name.strip() for name in
                                 args.require_interactions.split(",") if name.strip()]
    label = args.label or args.mode
    artifact_dir = os.path.abspath(args.artifact_dir)
    os.makedirs(artifact_dir, exist_ok=True)

    exe_root = os.path.abspath(args.exe_root)
    workdir = os.path.abspath(args.workdir) if args.workdir else resolve_workdir(exe_root)
    server_exe = (os.path.abspath(args.server_exe) if args.server_exe
                  else find_exe(exe_root, SERVER_EXE))
    client_exe = (os.path.abspath(args.client_exe) if args.client_exe
                  else find_exe(exe_root, CLIENT_EXE))

    prefix = os.path.join(artifact_dir, "network-ui-smoke-%s" % label)
    server_log = prefix + "-server.log"
    client_log = prefix + "-client.log"
    marker_file = prefix + "-autotest.log"
    result_path = prefix + "-result.json"
    screenshot_path = prefix + "-failure.png"
    summary_path = prefix + "-summary.json"
    config_path = write_server_config(prefix + "-server.ini", args)
    for stale in (marker_file, result_path, screenshot_path):
        if os.path.isfile(stale):
            os.remove(stale)

    port = free_tcp_port()
    summary = {
        "schema_version": SCHEMA_VERSION,
        "ok": False,
        "run": {
            "mode": args.mode,
            "seed": args.seed,
            "port": port,
            "platform": args.platform,
            "xvfb": args.xvfb,
            "workdir": workdir,
            "server_exe": server_exe,
            "client_exe": client_exe,
            "server_sha256": sha256_of(server_exe),
            "client_sha256": sha256_of(client_exe),
            "extensions_commit": extensions_commit(exe_root),
            "operation_timeout": args.operation_timeout,
            "ai_delay": args.ai_delay,
            "process_timeout": args.process_timeout,
            "client_timeout_ms": args.client_timeout_ms,
            "stall_ms": args.stall_ms,
            "effects_profile": args.effects_profile,
        },
        "lifecycle": {
            "server_started": False,
            "client_exit": None,
            "client_exit_name": None,
            "server_exit": None,
            "server_shutdown": "not-started",
            "orphans": [],
            "port_released": False,
        },
        "problems": [],
    }

    print("== Linux GUI M2 network smoke (%s) ==" % label)
    print("mode            : %s" % args.mode)
    print("seed            : %s" % args.seed)
    print("port            : %d" % port)
    print("workdir         : %s" % workdir)
    print("server          : %s" % server_exe)
    print("client          : %s" % client_exe)
    print("platform        : %s (xvfb=%s)" % (args.platform, args.xvfb))

    server_command = [
        server_exe,
        "--port", str(port),
        "--websocket-port", "0",
        "--bind-address", "127.0.0.1",
        "--game-mode", args.mode,
        "--seed", str(args.seed),
        "--config", config_path,
        "--autotest-log", marker_file,
        "--operation-timeout", str(args.operation_timeout),
        "--ai-delay", str(args.ai_delay),
        "--ai", "on",
        "--log-level", "debug",
    ]
    server = spawn(server_command, workdir, server_log)
    client = None
    started_client = False
    try:
        if not wait_port(port, args.server_startup_timeout, proc=server):
            code = wait_exit(server, 5)
            summary["problems"].append(
                "the server never started listening on port %d (exit=%s)"
                % (port, describe_exit(code)))
        else:
            summary["lifecycle"]["server_started"] = True
            print("server ready on port %d" % port)

            client_command = build_client_command(args, client_exe, port, result_path,
                                                  screenshot_path)
            client = spawn(client_command, workdir, client_log,
                           env=client_environment(args, artifact_dir))
            started_client = True
            print("client started (pid %d)" % client.pid)

            client_code = wait_exit(client, args.process_timeout)
            if client_code is None:
                summary["problems"].append(
                    "the client did not exit within the runner timeout of %ds"
                    % args.process_timeout)
                client_code, how = terminate_tree(client)
                print("client killed by the runner timeout (%s)" % how)
            summary["lifecycle"]["client_exit"] = client_code
            summary["lifecycle"]["client_exit_name"] = describe_exit(client_code)
            print("client exit     : %s" % describe_exit(client_code))
    finally:
        # 成功、失敗、被 Ctrl-C 都走同一條清理路徑，唔會留低孤兒或者佔住 port。
        code = finalize(summary, summary_path, server, client, port, args,
                        client_log=client_log if started_client else None,
                        marker_file=marker_file, result_path=result_path)
    return code


def finalize(summary, summary_path, server, client, port, args,
             client_log=None, marker_file=None, result_path=None):
    """收尾：關 server、檢查孤兒、驗契約、寫 summary。成功與失敗路徑共用。"""
    lifecycle = summary["lifecycle"]

    # client 一定要已經走；正常路徑佢自己退出，異常路徑喺度斬埋成棵樹。
    if client is not None:
        code, how = terminate_tree(client)
        if lifecycle["client_exit"] is None:
            lifecycle["client_exit"] = code
            lifecycle["client_exit_name"] = describe_exit(code)
        if how not in ("already", "graceful"):
            lifecycle["orphans"].append("client (%s)" % how)

    if server is not None:
        server_code, how = terminate_tree(server)
        lifecycle["server_exit"] = server_code
        lifecycle["server_shutdown"] = how
        print("server shutdown : %s (exit=%s)" % (how, describe_exit(server_code)))
        if process_group_alive(server):
            lifecycle["orphans"].append("server process group")
    if client is not None and process_group_alive(client):
        lifecycle["orphans"].append("client process group")

    lifecycle["port_released"] = wait_port_released(port, 15)

    stages, results = [], []
    server_markers = {"game_start": False, "game_over": None}
    if client_log:
        stages, results = parse_markers(read_text(client_log))
    if marker_file:
        marker_text = read_text(marker_file)
        server_markers["game_start"] = MARK_GAME_START in marker_text
        # 對局結束之後 client 會正常斷線,server 會為咗收拾房間再寫多一行冇 winner
        # 的 "game over"。真正的結果係第一行有 winner 嗰個,唔可以被收尾嗰行蓋過。
        for line in marker_text.splitlines():
            match = MARK_GAME_OVER.search(line)
            if not match:
                continue
            winner = (match.group(1) or "").strip()
            if server_markers["game_over"] is None:
                server_markers["game_over"] = winner or "none"
            elif winner and server_markers["game_over"] == "none":
                server_markers["game_over"] = winner
    summary["stages"] = stages
    summary["result"] = results[-1] if results else None
    summary["server_markers"] = server_markers
    if result_path and os.path.isfile(result_path):
        try:
            summary["responder"] = json.loads(read_text(result_path)).get("responder")
        except json.JSONDecodeError:
            summary["responder"] = None

    if lifecycle["client_exit"] is not None or stages:
        summary["problems"] += evaluate(args, summary, stages, results,
                                        lifecycle["client_exit"], server_markers)
    summary["ok"] = not summary["problems"]

    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, ensure_ascii=False, indent=2)

    print("\nstages reported : %d" % len(stages))
    for stage in stages:
        print("  [%s] %s" % ("PASS" if stage.get("ok") else "FAIL", stage.get("stage")))
    responder = summary.get("responder") or {}
    if responder:
        print("interactions    : %s"
              % ", ".join("%s=%d" % item
                          for item in sorted((responder.get("interactions") or {}).items())))
        print("ui actions      : %s"
              % ", ".join("%s=%d" % item
                          for item in sorted((responder.get("ui_actions") or {}).items())))
        print("trustee         : %s" % responder.get("trustee_engaged"))
    print("server game over: %s" % server_markers["game_over"])
    for entry in summary.get("known_base_defects", []):
        print("KNOWN BASE DEFECT (downgraded, still recorded): %s" % entry["defect"])
        for detail in entry["detail"]:
            print("  ! %s" % detail)
    print("summary         : %s" % summary_path)

    if summary["problems"]:
        print("\nLinux GUI M2 network smoke FAILED:", file=sys.stderr)
        for problem in summary["problems"]:
            print("  - %s" % problem, file=sys.stderr)
        if client_log:
            print("\n---- last 40 client log lines ----", file=sys.stderr)
            for line in tail_lines(client_log, 40):
                print("  %s" % line, file=sys.stderr)
        return 1

    print("\nLinux GUI M2 network smoke PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
