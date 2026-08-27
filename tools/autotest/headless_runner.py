# -*- coding: utf-8 -*-
"""headless 壓力測試 runner。

以 QSanguosha.exe --headless --game-mode <模式> --games <N> 執行純 AI 對局
(單一 process 內連續 N 局), 依模式平行多開, 以 exit code + log 標記判定結果。

用法:
    python headless_runner.py --exe-root L:\\finaldebug\\QSanguosha-v2 ^
        --modes 10p,20p,02_1v1,05p --games 5 --parallel 2
"""
import argparse
import json
import os
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Final, TypeAlias

from runner_common import (HEADLESS_HEADER, common_args, describe_exit,
                           hex_exit, is_crash_code, kill_pid,
                           log_dir_for, log_has_smart_ai_failure,
                           parse_headless_log, qt_console_env,
                           resolve_workdir, spawn, stamp, tail_lines,
                           wait_exit, write_csv, HEADLESS_DONE,
                           HEADLESS_FINISHED)

MAX_SEED: Final[int] = (1 << 32) - 1
REGISTERED_REAL_MODES: Final[frozenset[str]] = frozenset({"08p"})
PER_GAME_TIMEOUT: Final[int] = int(
    os.environ.get("QSAN_HEADLESS_PER_GAME_TIMEOUT", "3600")
)  # 可由環境變數覆寫的每局有界上限 (秒)


CUR_GAME_RE = re.compile(r">>> Starting headless game (\d+) <<<")
FINAL_GAUGE_TOKEN_RE = re.compile(r"(?:CARD_LIFETIME_ZERO|\[CardLifetime\]\s+FINAL_GAUGE)")
LOG_TIMESTAMP_RE = re.compile(r"(?:^|\s)(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}\.\d{3})\s")
GAUGE_FIELDS: Final[tuple[str, ...]] = (
    "managed_live",
    "pending_delete",
    "adoption_reserved",
    "wrapper_leases",
    "native_leases",
    "lua_pins",
    "sidecar_edges",
    "entries",
    "active_scopes",
)

JsonValue: TypeAlias = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)


@dataclass(frozen=True, slots=True)
class GaugeObservation:
    stage: str
    game_id: int | None
    fields: tuple[tuple[str, int], ...]
    parse_failure: str
    nonzero_fields: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class GaugeRow:
    game: int
    marker_count: int
    status: str
    fields: str
    failure: str


@dataclass(frozen=True, slots=True)
class GaugeValidation:
    rows: tuple[GaugeRow, ...]
    marker_count: int
    status: str
    failure: str


def parse_seed(value: str) -> int:
    try:
        seed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "seed must be an unsigned 32-bit integer"
        ) from error
    if not 0 <= seed <= MAX_SEED:
        raise argparse.ArgumentTypeError("seed must be an unsigned 32-bit integer")
    return seed


def shutdown_marker_count(headless_log: str) -> int:
    if not os.path.isfile(headless_log):
        return 0
    with open(headless_log, encoding="utf-8", errors="replace") as stream:
        return sum(1 for line in stream if HEADLESS_DONE.search(line))


def _ordered_log_lines(*paths: str) -> list[str]:
    entries: list[tuple[str, int, int, str]] = []
    for source, path in enumerate(paths):
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as stream:
            for index, line in enumerate(stream):
                match = LOG_TIMESTAMP_RE.search(line)
                timestamp = match.group(1) if match else ""
                entries.append((timestamp, source, index, line))
    if any(timestamp for timestamp, _, _, _ in entries):
        entries.sort(key=lambda entry: (entry[0] or "9999", entry[1], entry[2]))
    return [line for _, _, _, line in entries]


def _nonzero_json_fields(value: JsonValue, path: str = "") -> tuple[str, ...]:
    if isinstance(value, dict):
        fields: list[str] = []
        for name, child in value.items():
            child_path = f"{path}.{name}" if path else name
            fields.extend(_nonzero_json_fields(child, child_path))
        return tuple(fields)
    if isinstance(value, list):
        fields = []
        for index, child in enumerate(value):
            fields.extend(_nonzero_json_fields(child, f"{path}[{index}]"))
        return tuple(fields)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return ()
    return (path,) if value != 0 else ()


def _marker_payload(line: str) -> tuple[bool, dict[str, JsonValue] | None, str]:
    token = FINAL_GAUGE_TOKEN_RE.search(line)
    if token is None:
        return False, None, ""
    payload_text = line[token.end():].lstrip()
    brace = payload_text.find("{")
    if brace < 0:
        return True, None, "malformed-json"
    try:
        payload = json.loads(payload_text[brace:])
    except json.JSONDecodeError:
        return True, None, "malformed-json"
    if not isinstance(payload, dict):
        return True, None, "malformed-json"
    return True, payload, ""


def _payload_game_id(payload: dict[str, JsonValue]) -> int | None:
    for name in ("game", "game_id", "game_number"):
        value = payload.get(name)
        if isinstance(value, bool):
            return None
        if isinstance(value, int):
            return value
    for name in ("domain", "runtime_id"):
        value = payload.get(name)
        if isinstance(value, str):
            match = re.search(r"(?:game|room)[-_ ](\d+)$", value)
            if match is not None:
                return int(match.group(1))
    return None


def _payload_fields(
    payload: dict[str, JsonValue],
) -> tuple[tuple[tuple[str, int], ...], str, tuple[str, ...]]:
    gauge = payload
    nested_name = ""
    for name in ("gauge", "final_gauge", "fields", "runtime_delta"):
        nested = payload.get(name)
        if isinstance(nested, dict) and any(field in nested for field in GAUGE_FIELDS):
            gauge = nested
            nested_name = name
            break
    values: list[tuple[str, int]] = []
    missing: list[str] = []
    invalid: list[str] = []
    for name in GAUGE_FIELDS:
        value = gauge.get(name)
        if value is None:
            missing.append(name)
        elif isinstance(value, bool) or not isinstance(value, int) or value < 0:
            invalid.append(name)
        else:
            values.append((name, value))
    failures: list[str] = []
    if missing:
        failures.append("missing-fields=" + ",".join(missing))
    if invalid:
        failures.append("invalid-fields=" + ",".join(invalid))
    if nested_name and nested_name != "gauge" and payload.get("gauge") is not None:
        explicit_gauge = payload.get("gauge")
        if not isinstance(explicit_gauge, dict):
            failures.append("invalid-gauge")
    event = payload.get("event")
    if event is not None and event != "[CardLifetime] FINAL_GAUGE":
        failures.append("wrong-event")
    runtime_delta = payload.get("runtime_delta")
    nonzero: tuple[str, ...] = ()
    if runtime_delta is not None:
        if not isinstance(runtime_delta, (dict, list)):
            failures.append("invalid-runtime-delta")
        nonzero = _nonzero_json_fields(runtime_delta, "runtime_delta")
    if not nonzero:
        nonzero = tuple(
            name for name, value in values if value != 0
        )
    return tuple(values), ";".join(failures), nonzero


def validate_final_gauges(
    headless_log: str,
    process_log: str,
    expected_games: int,
) -> GaugeValidation:
    observations: dict[int, list[GaugeObservation]] = {}
    orphan_failures: list[str] = []
    current_game: int | None = None
    completed: set[int] = set()
    done_seen = False
    marker_count = 0
    for line in _ordered_log_lines(headless_log, process_log):
        start = CUR_GAME_RE.search(line)
        if start is not None:
            current_game = int(start.group(1))
            done_seen = False
            continue
        finished = HEADLESS_FINISHED.search(line)
        if finished is not None:
            game = int(finished.group(1))
            completed.add(game)
            current_game = game
            continue
        if HEADLESS_DONE.search(line):
            done_seen = True
            current_game = None
            continue
        found, payload, parse_failure = _marker_payload(line)
        if not found:
            continue
        marker_count += 1
        if current_game is None or done_seen:
            orphan_failures.append("marker-outside-game")
            continue
        stage = "completed" if current_game in completed else "early"
        if payload is None:
            observation = GaugeObservation(
                stage=stage,
                game_id=None,
                fields=(),
                parse_failure=parse_failure,
                nonzero_fields=(),
            )
        else:
            fields, field_failure, nonzero = _payload_fields(payload)
            failure = ";".join(
                value for value in (field_failure,)
                if value
            )
            observation = GaugeObservation(
                stage=stage,
                game_id=_payload_game_id(payload),
                fields=fields,
                parse_failure=failure,
                nonzero_fields=nonzero,
            )
        observations.setdefault(current_game, []).append(observation)

    rows: list[GaugeRow] = []
    all_failures = list(orphan_failures)
    for game in range(1, expected_games + 1):
        game_observations = observations.get(game, [])
        failures: list[str] = []
        if not game_observations:
            failures.append("missing")
        if len(game_observations) > 1:
            failures.append("duplicate")
        fields = game_observations[0].fields if game_observations else ()
        field_values = {name: value for name, value in fields}
        for observation in game_observations:
            if observation.stage != "completed":
                failures.append("early")
            if observation.game_id is not None and observation.game_id != game:
                failures.append("wrong-game")
            if observation.parse_failure:
                failures.append("malformed" if "missing-fields" not in observation.parse_failure
                                and "invalid-fields" not in observation.parse_failure
                                else observation.parse_failure)
            if observation.nonzero_fields:
                failures.append("nonzero=" + ",".join(observation.nonzero_fields))
        unique_failures = list(dict.fromkeys(failures))
        status = "valid" if not unique_failures else "invalid"
        field_text = json.dumps(field_values, sort_keys=True, separators=(",", ":"))
        row = GaugeRow(
            game=game,
            marker_count=len(game_observations),
            status=status,
            fields=field_text,
            failure=";".join(unique_failures),
        )
        rows.append(row)
        all_failures.extend(
            f"game-{game}:{failure}" for failure in unique_failures
        )
    status = "valid" if not all_failures and marker_count == expected_games else "invalid"
    failure = ";".join(dict.fromkeys(all_failures))
    return GaugeValidation(tuple(rows), marker_count, status, failure)


def started_games(headless_log):
    """run 內所有已出現的 'Starting headless game N' 局號集合 (log 未寫入時為空)。"""
    s = set()
    if os.path.isfile(headless_log):
        with open(headless_log, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        start = 0
        for i, line in enumerate(lines):
            if HEADLESS_HEADER.search(line):
                start = i
        for line in lines[start:]:
            m = CUR_GAME_RE.search(line)
            if m:
                s.add(int(m.group(1)))
    return s


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
    # 自動化測試: --spawn-delay 讓多份 process 依索引分批啟動,
    # 避開「同時啟動大量 exe」被安全軟體行為攔截 (PROD 實測 10 個同時只有 2 個存活)
    delay = getattr(args, "spawn_delay", 0)
    if delay > 0 and tag:
        time.sleep(delay * (int(tag) - 1))
    # 同一模式多份平行時, 各自獨立 log 檔 (tag 為 process 編號)
    suffix = "-%s" % tag if tag else ""
    # 每次執行一個時間戳資料夾 (headless/<時間戳>/), 不再重名覆蓋
    run_dir = getattr(args, "run_dir", log_dir_for(args))
    log_path = os.path.join(run_dir, "%s%s.log" % (mode, suffix))
    headless_log = os.path.join(run_dir, "%s%s-headless.log" % (mode, suffix))
    label = mode + ("" if not tag else "#%s" % tag)
    cmd = [exe, "--headless", "--game-mode", mode, "--games", str(games),
           "--headless-log", headless_log]
    if getattr(args, "seed", None) is not None:
        cmd += ["--seed", str(args.seed)]
    # 自動化測試: 指定主公武將 (--test-general/--test-general2), 反覆測同一武將找 bug
    if getattr(args, "general", ""):
        cmd += ["--test-general", args.general]
    if getattr(args, "general2", ""):
        cmd += ["--test-general2", args.general2]
    proc = spawn(cmd, workdir, log_path, env=qt_console_env())
    timeout = games * PER_GAME_TIMEOUT + 120
    deadline = time.time() + timeout
    timed_out = False
    code = None
    # 輪詢標記檔, 每局開始/完成即在 CMD 印進度
    prev = {"total": 0, "failed": 0}
    prev_started = set()
    smart_ai_failed = False
    while True:
        code = proc.poll()
        if code is not None:
            break
        if time.time() > deadline:
            timed_out = True
            kill_pid(proc.pid)
            code = proc.wait()
            break
        # 自動化測試: smart-ai 載入失敗 — 同 VM 的後續局都會壞, 提前結束省時間
        if log_has_smart_ai_failure(headless_log):
            print("  [%s] [%s] smart-ai 載入失敗, 提前結束 (後續局無法正常進行)"
                  % (label, time.strftime("%H:%M:%S")))
            kill_pid(proc.pid)
            code = proc.wait()
            smart_ai_failed = True
            break
        finished, failed, done = parse_headless_log(headless_log)
        started = started_games(headless_log)
        for g in sorted(started - prev_started):
            print("  [%s] [%s] 第 %d/%d 局進行中..." % (label, time.strftime("%H:%M:%S"), g, games))
        prev_started = started
        prev = report_progress(label, games, prev, finished, failed)
        time.sleep(2)
    close_proc(proc)
    finished, failed, _ = parse_headless_log(headless_log)
    shutdown_count = shutdown_marker_count(headless_log)
    done = shutdown_count == 1
    n_finished = sum(finished.values())
    gauge_validation = validate_final_gauges(headless_log, log_path, games)
    process_failures: list[str] = []
    if timed_out:
        process_failures.append("timeout")
    if not done:
        process_failures.append(f"shutdown-marker-count={shutdown_count}")
    if n_finished != games:
        process_failures.append(f"finished={n_finished}/{games}")
    if failed != 0:
        process_failures.append(f"failed-games={failed}")
    if code != 0:
        process_failures.append(f"exit={code}")
    process_failure = ";".join(process_failures)
    ok = (not process_failures) and gauge_validation.status == "valid"
    # 閃退摘要: 非逾時且 exit code 是 Windows 崩潰碼 (0xC0000005 等) 才算閃退;
    # exit=1 等小值是應用程式自行退出 (如啟動失敗), 不算崩潰
    crashed = (not timed_out) and is_crash_code(code)
    context = tail_lines(headless_log, 20) if crashed else []
    return {
        "mode": mode + ("" if not tag else "#%s" % tag),
        "exit": code, "exit_name": describe_exit(code), "exit_hex": hex_exit(code),
        "timeout": timed_out, "crashed": crashed, "crash_context": context,
        "smart_ai_failed": smart_ai_failed,
        "finished": n_finished, "expected": games, "failed_games": failed,
        "done_marker": done, "shutdown_marker_count": shutdown_count,
        "marker_count": gauge_validation.marker_count,
        "marker_status": gauge_validation.status,
        "marker_failure": gauge_validation.failure,
        "gauge_rows": gauge_validation.rows,
        "process_failure": process_failure,
        "ok": ok, "winners": dict(finished), "log": log_path,
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
    parser.set_defaults(modes="20p")
    parser.add_argument("--games", type=int, default=5,
                        help="每個模式要跑的局數 (預設 5)")
    parser.add_argument("--repeat", dest="repeat", type=int, default=None)
    parser.add_argument("--parallel", type=int, default=1,
                        help="同時執行的 process 數 (預設 2)")
    parser.add_argument("--general", default="",
                        help="指定主公武將, 反覆測試同武將找 bug (空 = 隨機)")
    parser.add_argument("--general2", default="",
                        help="雙將模式指定主公副將 (空 = 隨機)")
    parser.add_argument("--spawn-delay", type=int, default=0,
                        help="多份 process 的啟動間隔秒數 (0 = 同時啟動)")
    parser.add_argument("--mode", default=None)
    parser.add_argument("--output", default=None)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--seed", required=True, type=parse_seed)
    args = parser.parse_args()
    if args.repeat is not None:
        args.games = args.repeat
    if args.games < 1:
        print("repeat must be a positive integer", file=sys.stderr)
        return 2

    raw_modes = args.mode if args.mode is not None else args.modes
    modes = [m.strip() for m in raw_modes.split(",") if m.strip()]
    if not modes:
        print("錯誤: 沒有指定模式", file=sys.stderr)
        return 1

    illegal_modes = sorted(set(modes) - REGISTERED_REAL_MODES)
    if illegal_modes:
        print(
            "unsupported product mode(s): %s; only 20p is registered"
            % ", ".join(illegal_modes),
            file=sys.stderr,
        )
        return 2

    exe = os.path.abspath(args.exe)
    if not os.path.isfile(exe):
        print("executable not found: %s" % exe, file=sys.stderr)
        return 2
    workdir = resolve_workdir(args.exe_root)
    log_root = log_dir_for(args)
    # 每次執行一個時間戳資料夾 (headless/<時間戳>/), 不再重名覆蓋
    args.run_dir = os.path.join(log_root, "headless", "%s-%d" % (stamp(), os.getpid()))
    os.makedirs(args.run_dir, exist_ok=True)
    print("執行檔: %s" % exe)
    print("cwd   : %s" % workdir)
    print("log   : %s" % args.run_dir)

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
                if r.get("smart_ai_failed"):
                    print("        smart-ai 載入失敗 (檔案/環境問題), 後續局未跑")

    csv_path = (
        os.path.abspath(args.output)
        if args.output
        else os.path.join(log_root, "summary-headless-%s.csv" % stamp())
    )
    header = ["mode", "game", "ok", "exit", "exit_name", "timeout", "crashed",
              "finished", "expected", "failed_games", "done_marker",
              "marker_count", "marker_status", "marker_fields", "marker_failure",
              "log"]
    csv_rows = []
    for result in results:
        gauge_rows = result.get("gauge_rows", ())
        process_failure = result.get("process_failure", "")
        marker_failure = result.get("marker_failure", "")
        if gauge_rows:
            for gauge in gauge_rows:
                failures = ";".join(
                    value for value in (gauge.failure, process_failure,
                                        marker_failure) if value
                )
                csv_rows.append([
                    result.get("mode"), gauge.game,
                    result.get("ok") and gauge.status == "valid",
                    result.get("exit"), result.get("exit_name"),
                    result.get("timeout"), result.get("crashed"),
                    result.get("finished"), result.get("expected"),
                    result.get("failed_games"), result.get("done_marker"),
                    gauge.marker_count, gauge.status, gauge.fields, failures,
                    result.get("log", "").replace("\\", "/"),
                ])
        else:
            csv_rows.append([
                result.get("mode"), "", result.get("ok"), result.get("exit"),
                result.get("exit_name"), result.get("timeout"),
                result.get("crashed"), result.get("finished"),
                result.get("expected"), result.get("failed_games"),
                result.get("done_marker"), 0, "invalid", "",
                process_failure or result.get("error", ""),
                result.get("log", "").replace("\\", "/"),
            ])
    write_csv(csv_path, header, csv_rows)
    print("結果: %s" % csv_path)

    ok = sum(1 for r in results if r.get("ok"))
    print("總計: %d/%d 通過" % (ok, len(results)))
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
