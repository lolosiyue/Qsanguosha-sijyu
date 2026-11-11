#!/usr/bin/env python3
"""原版 standard 25 武將 × 层 A 16 模式 headless 驗收判定。

輸入一個跑批輸出目錄（general_acceptance.py 或 headless_runner.py 產生的
headless 日誌樹），輸出 25 將 × 16 模式的 PASS/WARN/FAIL 表，每格附依據的
原始 log 行（檔案:行號 + 原文）。

判定優先序（見 judge_log()）：

  FAIL   log 顯示跑錯二進位（`Unknown options: headless`）
  FAIL   沒有 `[AUTOTEST] force general <seat> <general>`，也沒有對應的
         `[AUTOTEST] seated <seat> general='<general>'`（強制武將沒生效）
  WARN   強制生效，但該座位沒有任何 `[AUTOTEST] skill-invoked` 記錄
         （技能未被觀測到；照 nos 那輪標準不算過，但也不是缺陷）
  PASS   強制生效，且該座位至少觸發過一次 `[AUTOTEST] skill-invoked`

Lua 執行錯誤（extensions/、lua/ai/，跨武將共用）不改變上面的判定，只計數並
附在 evidence 裡；本輪規則明文不動那兩個目錄，讓它們拖垮武將判定沒有意義。

用法：
    python3 tools/autotest/standard_matrix_judge.py \
        --dir artifacts/standard-accept-20261110/calib \
        --out artifacts/standard-accept-20261110/calib/REPORT.md \
        --tsv artifacts/standard-accept-20261110/calib/summary.tsv
"""

import argparse
import csv
import json
import re
import sys
import time
from pathlib import Path

# 层 A：全员必跑的 16 個模式（來源：group goal stage 1 名單）
LAYER_A = [
    "02_1v1", "02p", "03_1v2", "03p", "04_1v3", "04_2v2", "04_boss", "04p",
    "05p", "06_3v3", "06p", "08_defense", "08p", "08_jiangshi", "6_ctg", "7_happyrebel",
]

# 實際要判的模式清單；預設是層 A，--modes 可改成層 B 的 35 個。
MODES = list(LAYER_A)

# 規模受限模式：40 人以上的局在 headless_runner 的 3600 秒單局上限內收不了局，
# 這是模式規模特性、不是武將缺陷；也不能讓 log 裡殘留的收尾行把它升成 WARN。
SIZE_LIMITED_MODES = {"50p"}

# 已知缺陷格：Chief 已定性、證據在 TRIAGE.md 對應節。
# 判 INCOMPLETE 而不是 RUNNING，免得一局被人工終止的卡死被讀成「還在跑」；
# 但 CSV 若報 crashed=True，仍以崩潰 FAIL 為準（硬事實優先）。
KNOWN_INCOMPLETE = {
    ("guanyu", "06pd"): "A7：CPU 忙等死循環，Chief 人工 kill -TERM（TRIAGE.md A7）",
}

# 原版 standard 25 將：21 個 strengthen 強化的原版人 + 4 個舊版
GENERALS = [
    "caocao", "simayi", "xiahoudun", "zhangliao", "xuchu", "guojia",
    "liubei", "guanyu", "zhangfei", "zhaoyun", "machao", "huangyueying",
    "ganning", "lvmeng", "huanggai", "zhouyu", "daqiao", "luxun",
    "huatuo", "lvbu", "diaochan",
    "zhenji", "zhugeliang", "sunquan", "sunshangxiang",
]

FORCE_RE = re.compile(r"\[AUTOTEST\] force general (\S+) (\S+)")
SEATED_RE = re.compile(r"\[AUTOTEST\] seated (\S+) general='([^']*)'")
SKILL_RE = re.compile(r"\[AUTOTEST\] skill-invoked (\S+) (\S+)")
# 模式臨時授與技能（impasse_fight 等劇情模式會用 #AcquireSkill 塞技能給座位），
# 被授與的技能不是武將本體技，判定時要剔除。
ACQUIRE_RE = re.compile(r"#AcquireSkill \| (\S+) \| from: (\S+)")
WINNER_RE = re.compile(r"Winner: (\S+)")
# runner 的 done marker 有時抓不到（跑完但沒有 marker），但這一行是對局自己印的。
ALL_DONE_RE = re.compile(r">>> All games completed\.")
LUA_ERR_RE = re.compile(
    r"(LUA ERROR|\[LUA\].*?(?:error|failed)|script error|"
    r"attempt to (?:index|call|compare)|\.lua:\d+:)",
    re.IGNORECASE,
)
UNKNOWN_OPT = "Unknown options: headless"

# 堆疊回溯的單幀行（`in method 'x'`、`in function <f:l>`）不是獨立錯誤，
# 歸類時要跳過，否則真正的錯誤會被這些幀擠出榜外。
TRACEBACK_RE = re.compile(r"\bin (?:method|function|local|field|upvalue|main chunk)\b")

# 一局跑完沒有，只看 headless_runner 落盤的 CSV（finished == expected 且
# done_marker）。log 的 mtime 不可靠：05p 判的時候 log 還沒 flush 完，
# 判成 0 次；等檔案寫完再讀，同一局冒出 2 次 jianxiong。

VERDICT_ORDER = {"FAIL": 0, "WARN": 1, "RUNNING": 2, "PASS": 3}


# 跑批外殼自己寫的日誌，不是對局 log，不能拿來判。
SKIP_LOG_NAMES = ("runner.log", "acceptance.log", "driver.log")


def general_root(root: Path, general: str) -> Path | None:
    """driver 的輸出根是 <out>/raw/<general>/，單將模式則直接給目錄。

    三種情況：
      1. ``<root>/raw/<general>`` 存在      → 用它（driver / batch 的輸出樹）
      2. ``root`` 的目錄名就是該武將        → 用它（直接指 raw/<general>）
      3. ``<root>/raw`` 存在但沒有該武將     → 回 None（這將還沒開始跑）
      4. 其他                              → 用 root 本身（舊行為，單將目錄）
    """
    cand = root / "raw" / general
    if cand.is_dir():
        return cand
    cand = root / general          # 層 B：<out>/layer-b/<general>/
    if cand.is_dir():
        return cand
    if root.name == general:
        return root
    if (root / "raw").is_dir():
        return None
    return root


def finished_covers_expected(finished: str, expected: str) -> bool:
    """完成判據是 `finished >= expected`，不是相等。

    `impasse_fight`（僵持模式判平手時會自己再開一局）實際回 finished=2 / expected=1，
    照相等判會把「跑多了」誤認成沒跑完（zhangliao 層 B 的實際案例）。
    """
    try:
        return int(finished) >= int(expected) > 0
    except (TypeError, ValueError):
        return False


def collect_csv_stats(groot: Path) -> dict[str, dict]:
    """headless_runner 跑完一將才寫 `summary-headless-<stamp>.csv`。

    exit / finished / expected 這三個數字以它為準；log 只用來判 verdict。
    還沒寫出來就回空 dict，呼叫端填空字串，不猜。
    """
    cands = sorted(groot.glob("summary-headless-*.csv")) or \
        sorted(groot.glob("**/summary-headless-*.csv"))
    if not cands:
        return {}
    stats: dict[str, dict] = {}
    # 這個 CSV 帶 UTF-8 BOM，不用 utf-8-sig 的話第一個欄位會變成 "\ufeffmode"。
    with cands[-1].open(newline="", encoding="utf-8-sig") as fh:
        for row in csv.DictReader(fh):
            mode = row.get("mode", "")
            if mode in MODES and mode not in stats:
                finished = row.get("finished", "")
                expected = row.get("expected", "")
                stats[mode] = {
                    "exit": row.get("exit", ""),
                    "exit_name": row.get("exit_name", ""),
                    "crashed": row.get("crashed", "").strip().lower() in ("true", "1", "yes"),
                    "timeout": row.get("timeout", "").strip().lower() in ("true", "1", "yes"),
                    "marker_failure": row.get("marker_failure", ""),
                    "marker_status": row.get("marker_status", "").strip().lower(),
                    "failed_games": row.get("failed_games", ""),
                    "finished": finished,
                    "expected": expected,
                    # 不採 CSV 的 `done_marker`：headless_runner 的 done 是
                    # `shutdown_count == 1`（:470），多打一局的模式永遠是 False。
                    "done": (finished_covers_expected(finished, expected)
                             and row.get("failed_games", "").strip() in ("", "0")
                             and row.get("marker_status", "").strip().lower() != "invalid"),
                }
    return stats


def collect_budget(groot: Path | None) -> tuple[list[str], list[str], str]:
    """讀該將的 summary.json，回傳 (stopped, not_run, detail)。

    `general_acceptance.py` 的 `--headless-minutes` 到點就把還沒開始的模式記成
    NOT RUN、正在跑的那個記成 STOPPED，但整體仍然 exit 0。只看 exit code 會把
    一整批缺格誤認成完成（guanyu 跑 3641 秒、16 格只出 5 格的實際案例），所以
    這裡把它單獨抓出來，讓報表自己講清楚。
    """
    if groot is None:
        return [], [], ""
    path = groot / "summary.json"
    if not path.is_file():
        return [], [], ""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return [], [], ""
    headless = (data.get("stages") or {}).get("headless") or {}
    budget = headless.get("budget") or {}
    return (list(budget.get("stopped") or []),
            list(budget.get("not_run") or []),
            str(headless.get("detail") or ""))


def collect_logs(root: Path) -> dict[str, Path]:
    """每個模式取最新一次跑批的主 log（非 `-headless.log` 優先）。"""
    best: dict[str, tuple[tuple, Path]] = {}
    for p in root.rglob("*.log"):
        if p.name in SKIP_LOG_NAMES:
            continue
        stem = p.stem
        is_side = stem.endswith("-headless")
        mode = stem[: -len("-headless")] if is_side else stem
        if mode not in MODES:
            continue
        # 主 log（0）優先於 -headless.log（1）；同一種取較新的時間戳目錄。
        cand = (0 if not is_side else 1, p.parent.name)
        cur = best.get(mode)
        if cur is None:
            better = True
        else:
            better = cand[0] < cur[0][0] or (cand[0] == cur[0][0] and cand[1] > cur[0][1])
        if better:
            best[mode] = (cand, p)
    return {mode: rank_path[1] for mode, rank_path in best.items()}


def judge_log(path: Path, general: str, own_skills: set[str] | None = None) -> dict:
    text = path.read_text(errors="replace")
    lines = text.splitlines()

    force = None  # (行號, 座位, 原文)
    seated = None
    skills: list[tuple[int, str, str, str]] = []
    forced_generals: list[str] = []
    # 模式臨時授與的技能（impasse_fight 這類會用 #AcquireSkill 給技能）。
    # 這些不是武將本體技，不能算命中。
    acquired: set[str] = set()
    lua_errors: list[tuple[int, str]] = []
    wrong_binary = False
    winners: list[str] = []

    for i, line in enumerate(lines, 1):
        if UNKNOWN_OPT in line:
            wrong_binary = True
        m = FORCE_RE.search(line)
        if m:
            forced_generals.append(m.group(2))
            if m.group(2) == general and force is None:
                force = (i, m.group(1), line.strip())
        m = SEATED_RE.search(line)
        if m and m.group(2) == general and seated is None:
            seated = (i, m.group(1), line.strip())
        m = ACQUIRE_RE.search(line)
        if m:
            acquired.add(m.group(1))
            continue
        m = SKILL_RE.search(line)
        if m:
            skills.append((i, m.group(1), m.group(2), line.strip()))
        if LUA_ERR_RE.search(line):
            lua_errors.append((i, line.strip()))
        m = WINNER_RE.search(line)
        if m:
            winners.append(m.group(1))

    lua_ev = [f"{path}:{i} {t}" for i, t in lua_errors[:2]]
    lua_note = (f"；另有 Lua 錯誤 {len(lua_errors)} 筆（extensions/、lua/ai/，跨武將共用）"
                if lua_errors else "")

    if wrong_binary:
        return {
            "verdict": "FAIL",
            "seat": "",
            "skills": "",
            "evidence": [f"{path}:1 {lines[0].strip() if lines else ''}"],
            "reason": "跑錯二進位（server 不吃 --headless）",
            "lua": 0,
        }

    seat = force[1] if force else (seated[1] if seated else "")
    if not seat:
        # 這個模式壓根沒跑這個武將：不是失敗，是未跑。
        if forced_generals:
            other = ", ".join(sorted(set(forced_generals)))
            return {
                "verdict": "NOT RUN",
                "seat": "",
                "skills": "",
                "evidence": [],
                "reason": f"這一模式跑的是 {other}，不是 {general}",
                "lua": len(lua_errors),
            "lua_lines": [t for _, t in lua_errors],
            }
        return {
            "verdict": "FAIL",
            "seat": "",
            "skills": "",
            "evidence": [],
            "reason": "log 裡既沒有強制武將記錄（強制沒生效，或這不是這一模式的 log）",
            "lua": len(lua_errors),
            "lua_lines": [t for _, t in lua_errors],
        }

    own = [s for s in skills if s[1] == seat]
    evidence = [f"{path}:{force[0]} {force[2]}"] + lua_ev

    if not own:
        return {
            "verdict": "WARN",
            "seat": seat,
            "skills": "",
            "evidence": evidence,
            "reason": f"強制生效，但這局 {seat} 這個座位沒有 skill-invoked 記錄（技能未被觀測到；別座位的技能不算）" + lua_note,
            "lua": len(lua_errors),
            "lua_lines": [t for _, t in lua_errors],
            "skill_count": 0,
        }

    # 只認武將本體技能：skill-invoked 也會記裝備技能（八卦陣、武器技），
    # 那些不算「這個武將的技能被觸發過」。
    if own_skills:
        hit = [s for s in own
               if s[2] in own_skills and s[2] not in acquired]
        if not hit:
            foreign = ", ".join(sorted({s[2] for s in own}))
            return {
                "verdict": "WARN",
                "seat": seat,
                "skills": foreign,
                "evidence": evidence + [f"{path}:{i} {t}" for i, _s, _k, t in own[:2]],
                "reason": f"座位 {seat} 只觸發了非本體技能（{foreign}），本體技能未觀測到" + lua_note,
                "lua": len(lua_errors),
            "lua_lines": [t for _, t in lua_errors],
                "skill_count": len(own),
            }
        own = hit

    evidence += [f"{path}:{i} {t}" for i, _s, _k, t in own[:2]]
    counts: dict[str, int] = {}
    for _i, _s, skill, _t in own:
        counts[skill] = counts.get(skill, 0) + 1
    return {
        "verdict": "PASS",
        "seat": seat,
        "skills": ", ".join(f"{k}×{v}" for k, v in sorted(counts.items())),
        "evidence": evidence,
        "reason": "強制生效且有技能觸發" + lua_note,
        "winner": winners[0] if winners else "",
        "lua": len(lua_errors),
            "lua_lines": [t for _, t in lua_errors],
        "skill_count": len(own),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dir", required=True, help="跑批輸出目錄")
    parser.add_argument("--out", default="", help="Markdown 報告輸出路徑")
    parser.add_argument("--tsv", default="", help="TSV 表輸出路徑")
    parser.add_argument("--general", action="append", default=[],
                        help="只判這些武將（可重複；預設全部 25 個）")
    parser.add_argument("--skills", action="append", default=[],
                        help="武將本體技能，格式 general=skill1,skill2（可重複）")
    parser.add_argument("--modes", default="",
                        help="要判的模式，逗號分隔；預設是層 A 的 16 個，"
                             "判層 B 時傳 layerB-exclude.txt 的內容")
    args = parser.parse_args()

    global MODES
    if args.modes.strip():
        MODES[:] = [m.strip() for m in args.modes.split(",") if m.strip()]

    skill_map: dict[str, set[str]] = {}
    for item in args.skills:
        for part in item.split(";"):
            if not part.strip():
                continue
            g, _, s = part.partition("=")
            skill_map[g.strip()] = {x.strip() for x in s.split(",") if x.strip()}

    root = Path(args.dir).resolve()
    if not root.is_dir():
        print(f"error: {root} 不是目錄", file=sys.stderr)
        return 2

    generals = args.general or GENERALS

    rows: dict[str, dict[str, dict]] = {
        gen: {
            mode: {"verdict": "NOT RUN", "seat": "", "skills": "",
                   "evidence": [], "reason": "這將還沒開始跑", "lua": 0}
            for mode in MODES
        }
        for gen in generals
    }
    for gen in generals:
        groot = general_root(root, gen)
        if groot is None:
            continue
        logs = collect_logs(groot)
        csv_stats = collect_csv_stats(groot)
        for mode in MODES:
            path = logs.get(mode)
            st = csv_stats.get(mode, {})
            if path is None:
                rows[gen][mode] = {
                    "verdict": "NOT RUN", "seat": "", "skills": "",
                    "evidence": [], "reason": "找不到該模式的 log", "lua": 0,
                }
                continue
            known = KNOWN_INCOMPLETE.get((gen, mode))
            if known and not st.get("crashed"):
                rows[gen][mode] = {
                    "verdict": "INCOMPLETE", "seat": "", "skills": "",
                    "evidence": [str(path)],
                    "reason": "已知缺陷：" + known,
                    "lua": 0, "exit_code": st.get("exit", ""),
                    "finished": st.get("finished", ""),
                    "expected": st.get("expected", ""),
                }
                continue
            # 這一將的 CSV 要整將跑完才落盤；沒有 CSV 時不能一律當成還在跑，
            # 層 B 一將要跑好幾小時，已經收局的那些模式得先判出來。
            # 只看 log 的 mtime 會誤判：log 沒 flush 完就讀，技能次數會少算，
            # 所以用「log 自己印的收尾行」當證據，不用時間。
            if not st:
                text = path.read_text(errors="replace")
                if mode not in SIZE_LIMITED_MODES and (
                        ALL_DONE_RE.search(text) or WINNER_RE.search(text)):
                    cell = judge_log(path, gen, skill_map.get(gen))
                    cell["reason"] = ("summary CSV 尚未落盤，但 log 已有收尾行／"
                                      "勝負行，按 log 採信；" + cell["reason"])
                    rows[gen][mode] = cell
                    continue
                rows[gen][mode] = {
                    "verdict": "RUNNING", "seat": "", "skills": "",
                    "evidence": [str(path)],
                    "reason": "這一將的 summary CSV 還沒落盤，代表還在跑，先不判",
                    "lua": 0,
                }
                continue
            # 崩潰是硬事實，優先於技能有沒有被觀測到：CSV 說 crashed=True 就是 FAIL。
            if st.get("crashed"):
                rows[gen][mode] = {
                    "verdict": "FAIL", "seat": "", "skills": "",
                    "evidence": [f"{path} -> direct exit {st.get('exit')}"],
                    "reason": f"崩潰：{st.get('exit_name', '')}（finished=0/{st.get('expected', '?')}）",
                    "lua": 0, "exit_code": st.get("exit", ""),
                    "finished": st.get("finished", ""),
                    "expected": st.get("expected", ""),
                }
                continue
            if not st.get("done"):
                # 這裡要分兩種，不能一律叫 RUNNING：
                #   真的還在跑     → 這將的 CSV 還沒落盤，上面已經攔掉了
                #   已經停了沒跑完 → INCOMPLETE，理由寫清楚是被殺、超時還是預算截斷
                text = path.read_text(errors="replace")
                if mode not in SIZE_LIMITED_MODES and (
                        ALL_DONE_RE.search(text) and WINNER_RE.search(text)):
                    # CSV 說沒跑完，但 log 自己印了收尾行與勝負：採信 log。
                    cell = judge_log(path, gen, skill_map.get(gen))
                    cell["reason"] = ("CSV done=False 與 log 內容矛盾，按 log 采信"
                                      "（log 有 'All games completed' 與 Winner）；"
                                      + cell["reason"])
                    cell["exit_code"] = st.get("exit", "")
                    cell["finished"] = st.get("finished", "")
                    cell["expected"] = st.get("expected", "")
                    rows[gen][mode] = cell
                    continue
                fails = st.get("marker_failure", "")
                exit_name = st.get("exit_name") or ("exit %s" % st.get("exit"))
                if st.get("timeout"):
                    how = "超時被 SIGKILL"
                elif "budget-stopped" in fails:
                    how = "被 --headless-minutes 的預算截斷"
                else:
                    how = "進程已停但這局沒跑完（%s）" % exit_name
                rows[gen][mode] = {
                    "verdict": "INCOMPLETE", "seat": "", "skills": "",
                    "evidence": [f"{path} -> {fails or 'no done marker'}"],
                    "reason": (f"{how}：finished={st.get('finished')}"
                               f"/{st.get('expected')}"),
                    "lua": 0, "exit_code": st.get("exit", ""),
                    "finished": st.get("finished", ""),
                    "expected": st.get("expected", ""),
                }
                continue
            cell = judge_log(path, gen, skill_map.get(gen))
            cell["exit_code"] = st.get("exit", "")
            cell["finished"] = st.get("finished", "")
            cell["expected"] = st.get("expected", "")
            rows[gen][mode] = cell

    # TSV
    tsv_lines = ["general\tmode\texit_code\tgames_finished\tgames_expected"
                 "\tskill_invocations\tverdict\tseat\tskills\tlua_errors\treason\tevidence"]
    for gen in generals:
        for mode in MODES:
            c = rows[gen][mode]
            ev = " | ".join(c["evidence"]).replace("\t", " ")
            tsv_lines.append(
                f"{gen}\t{mode}\t{c.get('exit_code', '')}\t{c.get('finished', '')}"
                f"\t{c.get('expected', '')}\t{c.get('skill_count', '')}"
                f"\t{c['verdict']}\t{c['seat']}\t{c['skills']}"
                f"\t{c.get('lua', 0)}\t{c['reason']}\t{ev}"
            )
    if args.tsv:
        Path(args.tsv).write_text("\n".join(tsv_lines) + "\n")
        print(f"written: {args.tsv}")

    # Markdown
    total = len(generals) * len(MODES)
    tally = {"PASS": 0, "WARN": 0, "FAIL": 0, "INCOMPLETE": 0,
             "RUNNING": 0, "NOT RUN": 0}
    for gen in generals:
        for mode in MODES:
            tally[rows[gen][mode]["verdict"]] += 1

    md = ["# 原版 standard 武將 headless 矩陣判定", ""]
    md.append(f"跑批目錄：`{root}`")
    md.append("")
    md.append(f"格數 {len(generals)} 將 × {len(MODES)} 模式 = {total}；"
              f"PASS {tally['PASS']}、WARN {tally['WARN']}、"
              f"FAIL {tally['FAIL']}、INCOMPLETE {tally['INCOMPLETE']}、"
              f"RUNNING {tally['RUNNING']}、NOT RUN {tally['NOT RUN']}")
    md.append("")
    md.append("判定：PASS＝強制武將生效且該座位有本體技能的 `[AUTOTEST] skill-invoked`；"
              "WARN＝強制生效但沒觀測到本體技能（或只觸發裝備技能）；"
              "FAIL＝強制沒生效／跑錯二進位；"
              "INCOMPLETE＝已停但沒跑完（崩潰以外的超時、預算截斷等，理由逐格寫）；"
              "RUNNING＝真的還有進程在跑。Lua 錯誤不影響武將判定，另列一節。")
    md.append("")
    # 口徑說明只寫在 TRIAGE.md 一處；這裡留一行指路，免得五份 MATRIX 各貼一段同樣的話。
    md.append("> 判定口徑，以及為什麼成績不看 `summary.json` 的 `graded modes clean`，"
              "見 [`artifacts/standard-accept-20261110/JUDGE-SPEC.md`]"
              "(artifacts/standard-accept-20261110/JUDGE-SPEC.md)。")
    md.append("")
    short = {"PASS": "PASS", "WARN": "WARN", "FAIL": "FAIL",
             "INCOMPLETE": "INC", "RUNNING": "…", "NOT RUN": "—"}
    md.append("| 武將 | " + " | ".join(MODES) + " |")
    md.append("|" + "---|" * (len(MODES) + 1))
    for gen in generals:
        cells = [short[rows[gen][m]["verdict"]] for m in MODES]
        md.append(f"| {gen} | " + " | ".join(cells) + " |")
    md.append("")

    md.append("## 各武將進度")
    md.append("")
    md.append("狀態：`完成`＝16 格都有判定；`未完成`＝有格子停在 INCOMPLETE；"
              "`進行中`＝CSV 尚未落盤；`未開始`＝還沒有 log。")
    md.append("")
    md.append("| 武將 | 完成格 | PASS | WARN | FAIL | INCOMPLETE | 進行中 | 未開始 | 狀態 |")
    md.append("|---|---|---|---|---|---|---|---|---|")
    budgets = {gen: collect_budget(general_root(root, gen)) for gen in generals}
    for gen in generals:
        c = {v: sum(1 for m in MODES if rows[gen][m]["verdict"] == v)
             for v in ("PASS", "WARN", "FAIL", "INCOMPLETE", "RUNNING", "NOT RUN")}
        graded = c["PASS"] + c["WARN"] + c["FAIL"]
        stopped, not_run, _detail = budgets[gen]
        if graded == len(MODES):
            state = "完成"
        elif stopped or not_run:
            state = "截斷（預算）"
        elif c["NOT RUN"] == len(MODES):
            state = "未開始"
        elif c["INCOMPLETE"]:
            state = "未完成"
        else:
            state = "進行中"
        md.append(f"| {gen} | {graded}/{len(MODES)} | {c['PASS']} | {c['WARN']} | "
                  f"{c['FAIL']} | {c['INCOMPLETE']} | {c['RUNNING']} | "
                  f"{c['NOT RUN']} | {state} |")
    md.append("")

    truncated = {g: b for g, b in budgets.items() if b[0] or b[1]}
    if truncated:
        md.append("## 被 `--headless-minutes` 截斷（不是完成）")
        md.append("")
        md.append("`general_acceptance.py` 的 `--headless-minutes` 預設 60 分鐘。"
                  "到點時它把還沒開始的模式記成 `NOT RUN`、正在跑的那個記成 `STOPPED`，"
                  "整體仍然 `exit 0`。只看 exit code 會把整批缺格誤認成完成"
                  "（guanyu 的實際案例：跑 3641 秒，16 格只出 5 格，卻記成 rc=0）。"
                  "這些武將必須補跑，不能算進驗收結果。")
        md.append("")
        md.append("| 武將 | STOPPED | NOT RUN | detail |")
        md.append("|---|---|---|---|")
        for gen, (stopped, not_run, detail) in truncated.items():
            md.append(f"| {gen} | {', '.join(stopped) or '—'} | "
                      f"{', '.join(not_run) or '—'} | {detail} |")
        md.append("")

    md.append("## 依據 log 行")
    md.append("")
    md.append("只列 WARN、FAIL 與 INCOMPLETE；PASS 與尚未判定的格（`RUNNING` / `NOT RUN`）"
              "不在此節，完整 400 行見 `summary.tsv`。")
    md.append("")
    for gen in generals:
        for mode in MODES:
            c = rows[gen][mode]
            if c["verdict"] in ("PASS", "RUNNING", "NOT RUN"):
                continue
            md.append(f"- **{gen} / {mode}** — {c['verdict']}：{c['reason']}")
            for ev in c["evidence"][:3]:
                md.append(f"  - `{ev}`")
    md.append("")

    lua_cells = [(g, m, rows[g][m]["lua"]) for g in generals for m in MODES
                 if rows[g][m].get("lua")]
    if lua_cells:
        md.append("## Lua 錯誤（跨武將共用，不影響武將判定）")
        md.append("")
        md.append("| 武將 | 模式 | 錯誤筆數 |")
        md.append("|---|---|---|")
        for g, m, n in lua_cells:
            md.append(f"| {g} | {m} | {n} |")
        md.append("")
        # 同一條錯誤常跨模式重現；按原文歸類，日後推上游時可以一次過處理。
        cats: dict[str, dict] = {}
        for g in rows:
            for m in MODES:
                for t in rows[g][m].get("lua_lines", []):
                    if TRACEBACK_RE.search(t):
                        continue
                    e = cats.setdefault(t, {"n": 0, "modes": set()})
                    e["n"] += 1
                    e["modes"].add(m)
        if cats:
            md.append("### 按原文歸類（跨模式去重）")
            md.append("")
            md.append("| 次數 | 模式數 | 錯誤原文 |")
            md.append("|---|---|---|")
            for t, e in sorted(cats.items(), key=lambda kv: -kv[1]["n"])[:20]:
                short = t if len(t) <= 200 else t[:197] + "..."
                md.append(f"| {e['n']} | {len(e['modes'])} | `{short.replace('|', '\\|')}` |")
            md.append("")
        md.append("錯誤原文見 `summary.tsv` 的 evidence 欄與各模式 log。")
        md.append("")

    out = Path(args.out) if args.out else root / "REPORT.md"
    out.write_text("\n".join(md) + "\n")
    print(f"written: {out}")
    print(f"PASS {tally['PASS']} WARN {tally['WARN']} "
          f"FAIL {tally['FAIL']} INCOMPLETE {tally['INCOMPLETE']} "
          f"RUNNING {tally['RUNNING']} NOT RUN {tally['NOT RUN']}")
    return 0 if tally["FAIL"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
