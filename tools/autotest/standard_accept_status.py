#!/usr/bin/env python3
"""驗收狀態總表：把每個武將最新一份 headless CSV 與 gui-tui 的 summary.json 合成一張表。

為什麼要這個工具
----------------
驗收狀態原本散在三處：`PROGRESS.md`、`driver.log`、以及每個武將各自的
`raw/<general>/headless/summary-headless-*.csv`。而 `standard-accept-driver.sh`
用 glob 取 CSV 時拿到的是字典序第一個（也就是最舊那份），所以 luxun 明明
11-11 02:22 之後每一份 CSV 都是 16 行全乾淨，卻一直被讀到 11-10 那份壞的
`04_boss`，連續重跑五輪。

本工具固定取「檔名時間戳最新」的那一份，並把 gui / gui_heg / tui 三個階段
一起納入，讓「最新紀錄其實是乾淨的」一眼可見。

只讀，不改遊戲碼，不寫 artifacts 以外的檔案。唯一輸出檔是 STATUS.tsv。
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import unicodedata
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# 層 A 的 16 個模式（驗收矩陣的固定欄位）。
LAYER_A = [
    "02_1v1", "02p", "03_1v2", "03p", "04_1v3", "04_2v2", "04_boss", "04p",
    "05p", "06_3v3", "06p", "08_defense", "08p", "08_jiangshi", "6_ctg", "7_happyrebel",
]

# 要驗收的 25 名武將（與 standard-accept-driver.sh 的順序一致）。
GENERALS = [
    "caocao", "simayi", "xiahoudun", "zhangliao", "xuchu", "guojia",
    "liubei", "guanyu", "zhangfei", "zhaoyun", "machao", "huangyueying",
    "ganning", "lvmeng", "huanggai", "zhouyu", "daqiao", "luxun",
    "huatuo", "lvbu", "diaochan",
    "zhenji", "zhugeliang", "sunquan", "sunshangxiang",
]

GUI_STAGES = ("gui", "gui_heg", "tui")

# 檔名裡的時間戳，用它排序；不是字典序，也不是 glob 順序。
CSV_RE = re.compile(r"^summary-headless-(\d{8}-\d{6})\.csv$")


# ---------------------------------------------------------------- 檔案挑選

def latest_csv(headless_dir: Path):
    """回傳 (stamp, path)：該目錄下最新的一份 summary CSV。

    `summary-headless-YYYYMMDD-HHMMSS.csv.superseded` 之類的備份不匹配正則，
    自然被排除。時間戳格式固定，字串比較等同時間比較。
    """
    best = None
    if not headless_dir.is_dir():
        return None
    for path in headless_dir.iterdir():
        m = CSV_RE.match(path.name)
        if not m:
            continue
        stamp = m.group(1)
        if best is None or stamp > best[0]:
            best = (stamp, path)
    return best


def all_csvs(headless_dir: Path):
    """回傳 [(stamp, path)]，新到舊。單格補跑只寫一行 CSV，所以不能只看最新一份。"""
    found = []
    if not headless_dir.is_dir():
        return found
    for path in headless_dir.iterdir():
        m = CSV_RE.match(path.name)
        if m:
            found.append((m.group(1), path))
    found.sort(key=lambda item: item[0], reverse=True)
    return found


def parse_stamp(stamp: str) -> str:
    """"20261113-094913" -> "2026-11-13 09:49:13"，取不到就原樣回。"""
    try:
        return datetime.strptime(stamp, "%Y%m%d-%H%M%S").strftime("%Y-%m-%d %H:%M:%S")
    except ValueError:
        return stamp


# ---------------------------------------------------------------- 讀 CSV

def read_csv(csv_path: Path) -> dict:
    """回傳 {mode: row}，同模式多局取第一行。

    CSV 帶 UTF-8 BOM，且 marker_fields 欄位裡有逗號與引號，必須用 DictReader。
    """
    rows = {}
    # utf-8-sig 才會吃掉 BOM，否則首欄名會變成 "\ufeffmode"。
    with csv_path.open(encoding="utf-8-sig", newline="") as fh:
        for row in csv.DictReader(fh):
            mode = (row.get("mode") or "").strip()
            if mode and mode not in rows:
                rows[mode] = row
    return rows


def _int(value: str, default: int = 0) -> int:
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return default


def judge_row(row: dict) -> tuple:
    """單格判定 -> (status, reason)。status 只會是 ok / bad。"""
    exit_name = (row.get("exit_name") or "").strip()
    exit_code = _int(row.get("exit", "0"))
    finished = _int(row.get("finished", "0"))
    expected = _int(row.get("expected", "0"))

    if str(row.get("crashed", "")).strip().lower() == "true":
        return "bad", f"崩潰 {exit_name or 'exit=' + str(exit_code)}"
    if str(row.get("ok", "")).strip().lower() != "true":
        return "bad", exit_name or f"exit={exit_code}"
    if expected > 0 and finished < expected:
        return "bad", f"finished={finished}/{expected}"
    if str(row.get("marker_status", "")).strip().lower() == "invalid":
        return "bad", "marker=invalid"
    return "ok", ""


# ---------------------------------------------------------------- 讀 summary.json

def read_gui_stages(root: Path, general: str) -> dict:
    """回傳 {gui: 'PASS'|'WARN'|...}；沒有這個階段就不會出現在結果裡。"""
    path = root / "gui-tui" / general / "summary.json"
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {"_error": f"summary.json 讀不到：{exc}"}
    stages = data.get("stages") or {}
    out = {}
    for name in GUI_STAGES:
        entry = stages.get(name)
        if isinstance(entry, dict):
            out[name] = (entry.get("status") or "?").strip()
    return out


# ---------------------------------------------------------------- 彙整

def collect(root: Path, general: str) -> dict:
    """逐格取最新的勝出記錄。

    早期版本只看目錄裡最新的一份 CSV。單格補跑（--modes 05p）只會寫一行，
    於是最新那份只有 05p，其餘 15 格就被誤判成缺格 —— diaochan 因此一度被
    報成「缺 15 格」。改法：把該目錄所有 CSV 由新到舊讀進來，每個模式取
    第一次出現的那一筆，也就是該模式最新的記錄。同一份 CSV 內同模式多局
    仍只取第一行（read_csv 既有行為）。
    """
    headless_dir = root / "raw" / general / "headless"
    result = {
        "general": general,
        "csv": None,
        "stamp": "",
        "updated": "",
        "updated_latest": "",
        "modes": {},
        "mode_stamp": {},
        "missing": [],
        "bad": [],
        "gui": {},
    }
    if not headless_dir.is_dir():
        result["missing"] = list(LAYER_A)
        result["gui"] = read_gui_stages(root, general)
        return result

    newest_stamp = ""
    newest_csv = None
    for stamp, csv_path in all_csvs(headless_dir):
        if newest_csv is None:
            newest_stamp, newest_csv = stamp, csv_path
        rows = read_csv(csv_path)
        for mode in LAYER_A:
            if mode in result["modes"] or mode not in rows:
                continue
            row = rows[mode]
            status, reason = judge_row(row)
            result["modes"][mode] = {
                "status": status,
                "reason": reason,
                "exit": _int(row.get("exit", "0")),
                "finished": _int(row.get("finished", "0")),
                "expected": _int(row.get("expected", "0")),
            }
            result["mode_stamp"][mode] = stamp
            if status == "bad":
                result["bad"].append(mode)

    result["csv"] = newest_csv
    result["stamp"] = newest_stamp
    result["updated"] = parse_stamp(newest_stamp)
    # 表上顯示「這一行最後一次被更新」的時間：各模式裡最新的那個 stamp，
    # 不是最新一份 CSV 的時間。否則單格補跑會讓整行看起來停在補跑時間。
    newest_mode = max(result["mode_stamp"].values(), default="")
    result["updated_latest"] = parse_stamp(newest_mode)
    result["missing"] = [m for m in LAYER_A if m not in result["modes"]]
    result["gui"] = read_gui_stages(root, general)
    return result


def classify(result: dict) -> str:
    """done / incomplete / bad 三選一。"""
    if result["bad"]:
        return "bad"
    if result["missing"]:
        return "incomplete"
    if not result["csv"]:
        return "incomplete"
    if not all(name in result["gui"] for name in GUI_STAGES):
        return "incomplete"
    return "done"


# ---------------------------------------------------------------- 輸出

def disp_width(text: str) -> int:
    """終端對齊用：全形字元算 2 格。"""
    return sum(2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1 for ch in text)


def pad(text: str, width: int) -> str:
    text = str(text)
    return text + " " * max(0, width - disp_width(text))


def render_table(results: list) -> list:
    header = ["武將", "headless", "gui", "gui_heg", "tui", "最後更新", "狀態"]
    widths = [14, 12, 8, 9, 8, 20, 12]
    lines = ["  ".join(pad(h, w) for h, w in zip(header, widths)).rstrip()]
    lines.append("  ".join("-" * w for w in widths))
    for res in results:
        if res["csv"] is None:
            headless = f"0/{len(LAYER_A)}（無 CSV）"
        else:
            good = len(LAYER_A) - len(res["bad"]) - len(res["missing"])
            headless = f"{good}/{len(LAYER_A)}"
            if res["bad"]:
                headless += f"（壞 {len(res['bad'])}）"
            if res["missing"]:
                headless += f"（缺 {len(res['missing'])}）"
        gui = res["gui"]
        state = classify(res)
        label = {"done": "完成", "incomplete": "未完成", "bad": "有失敗格"}[state]
        cells = [
            res["general"],
            headless,
            gui.get("gui", "—"),
            gui.get("gui_heg", "—"),
            gui.get("tui", "—"),
            res["updated_latest"] or "—",
            label,
        ]
        lines.append("  ".join(pad(c, w) for c, w in zip(cells, widths)).rstrip())
    return lines


def render_failures(results: list) -> list:
    lines = []
    for res in results:
        for mode in res["bad"]:
            info = res["modes"][mode]
            lines.append(
                f"  {res['general']:<14} {mode:<14} exit={info['exit']:<4} "
                f"{info['reason']}"
            )
        if res["missing"]:
            lines.append(
                f"  {res['general']:<14} {'（缺格）':<14} "
                + ",".join(res["missing"])
            )
    return lines


def write_tsv(path: Path, results: list) -> None:
    """長表：一格一行，機器材可用 awk/grep 直接聚合。UTF-8 無 BOM。"""
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh, delimiter="\t", lineterminator="\n")
        writer.writerow(["general", "stage", "item", "status", "exit", "reason", "updated"])
        for res in results:
            for mode in LAYER_A:
                info = res["modes"].get(mode)
                if info is None:
                    writer.writerow([res["general"], "headless", mode, "missing", "", "", res["updated"]])
                    continue
                writer.writerow([
                    res["general"], "headless", mode, info["status"],
                    info["exit"], info["reason"], res["updated"],
                ])
            for name in GUI_STAGES:
                writer.writerow([
                    res["general"], "gui", name,
                    res["gui"].get(name, "missing"), "", "", "",
                ])


def to_json(results: list) -> dict:
    out = []
    for res in results:
        out.append({
            "general": res["general"],
            "state": classify(res),
            "csv": str(res["csv"]) if res["csv"] else None,
            "stamp": res["stamp"],
            "updated": res["updated"],
            "headless": {
                mode: res["modes"].get(mode) for mode in LAYER_A
            },
            "bad_modes": res["bad"],
            "missing_modes": res["missing"],
            "gui": res["gui"],
        })
    return {"generals": out}


# ---------------------------------------------------------------- main

def find_latest_date(root_dir: Path) -> str:
    best = ""
    for child in root_dir.iterdir():
        if child.is_dir() and child.name.startswith("standard-accept-"):
            stamp = child.name[len("standard-accept-"):]
            if stamp.isdigit() and stamp > best:
                best = stamp
    return best


def main() -> int:
    parser = argparse.ArgumentParser(description="驗收狀態總表（只讀）")
    parser.add_argument("--date", default="", help="artifacts/standard-accept-<date> 的日期，預設取最新")
    parser.add_argument("--general", action="append", default=[], help="只看某個武將，可重複")
    parser.add_argument("--json", action="store_true", help="輸出 JSON")
    parser.add_argument("--root", default="", help="覆寫 artifacts 根目錄")
    args = parser.parse_args()

    artifacts = Path(args.root) if args.root else ROOT / "artifacts"
    date = args.date or find_latest_date(artifacts)
    if not date:
        print(f"找不到 artifacts/standard-accept-<date>（root={artifacts}）", file=sys.stderr)
        return 1
    root = artifacts / f"standard-accept-{date}"
    if not root.is_dir():
        print(f"目錄不存在：{root}", file=sys.stderr)
        return 1

    generals = args.general or GENERALS
    unknown = [g for g in generals if g not in GENERALS]
    if unknown:
        print(f"未知武將：{', '.join(unknown)}", file=sys.stderr)
        return 2

    results = [collect(root, g) for g in generals]

    if args.json:
        print(json.dumps(to_json(results), ensure_ascii=False, indent=2))
    else:
        print(f"驗收狀態總表  {root}")
        print()
        for line in render_table(results):
            print(line)
        fails = render_failures(results)
        if fails:
            print()
            print("失敗與缺格：")
            for line in fails:
                print(line)

    # 只有全量模式才寫 STATUS.tsv：單將模式若也寫，會把整張表覆蓋成一行武將。
    tsv_path = root / "STATUS.tsv"
    if not args.general:
        write_tsv(tsv_path, results)

    counts = {"done": 0, "incomplete": 0, "bad": 0}
    for res in results:
        counts[classify(res)] += 1

    if not args.json:
        print()
        if args.general:
            print("（單將模式，不寫 STATUS.tsv）")
        else:
            print(f"將 TSV 寫到 {tsv_path}（{len(results) * (len(LAYER_A) + len(GUI_STAGES))} 行）")
        print(
            f"done={counts['done']} incomplete={counts['incomplete']} "
            f"bad={counts['bad']} of {len(results)}"
        )

    all_good = (
        len(generals) == len(GENERALS)
        and counts["done"] == len(GENERALS)
    )
    return 0 if all_good else 1


if __name__ == "__main__":
    sys.exit(main())
