# -*- coding: utf-8 -*-
"""同 seed 同模式，比對兩個 binary 的盤面與啟動耗時。

用途：改了「不該影響遊戲語義」的東西（例如跳過預建房間）之後，證明盤面沒變，
並量出省了多少啟動時間。玩法與 headless_runner.py 完全一致，本腳本只負責
跑起來、讀 log、比對，不另寫一套開局邏輯。

三件事：
  1. 啟動耗時 = `[AUTOTEST] forced general:` 那行到 `>>> Headless stress test started` 的差。
  2. 去掉 `YYYY-MM-DD HH:MM:SS.mmm ` 時間戳後逐行 diff。
  3. 區分噪聲與真差異 —— 同一 binary 連跑兩次也會變的行是噪聲，
     只出現在跨 binary 的行才是真差異。

用法 A：兩顆 binary 各跑一次、B 再跑一次
    python3 tools/autotest/compare_binary_run.py \
        --a-binary /path/old/QSanguosha --b-binary /path/new/QSanguosha \
        --seed 20261119 --mode 02p

用法 B：直接讀現成的 log 目錄（不重跑）
    python3 tools/autotest/compare_binary_run.py \
        --a-log-dir /tmp/real-02p \
        --b-log-dir .maint/regress-perf-20261119 \
        --b2-log-dir .maint/regress-perf-20261119b --mode 02p
"""
import argparse
import difflib
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUNNER = HERE / "headless_runner.py"

# `<mode>-headless.log` 每行都以本地時間戳開頭
TIMESTAMP = re.compile(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} ")
STAMP_PARSE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) ")
START_MARK = "[AUTOTEST] forced general:"
STRESS_MARK = ">>> Headless stress test started"
# AI 決策評估行（例：`孔融[国] 24 反贼 忠值:-482 内值:0 叛逆,1.993`）。
# 末尾浮點每次執行都會抖，跨過門檻時連前面的決策標籤（叛逆/两难）也會跟著換，
# 所以這類行只能當噪聲來源，不能拿來判斷語義有沒有變。
AI_EVAL = re.compile(r"忠值:.*内值:")


@dataclass
class Run:
    label: str
    log: Path
    startup: float | None = None
    lines: list[str] = field(default_factory=list)


def strip_timestamp(line: str) -> str:
    return TIMESTAMP.sub("", line.rstrip("\n"))


def seconds_of(text: str) -> float | None:
    from datetime import datetime
    try:
        return datetime.strptime(text, "%Y-%m-%d %H:%M:%S.%f").timestamp()
    except ValueError:
        return None


def parse_log(path: Path, label: str) -> Run:
    run = Run(label=label, log=path)
    first = None
    stress = None
    for raw in path.open(encoding="utf-8", errors="replace"):
        m = STAMP_PARSE.match(raw)
        stripped = strip_timestamp(raw)
        run.lines.append(stripped)
        if m is None:
            continue
        if first is None and START_MARK in stripped:
            first = seconds_of(m.group(1))
        elif stress is None and STRESS_MARK in stripped:
            stress = seconds_of(m.group(1))
    if first is not None and stress is not None:
        run.startup = round(stress - first, 3)
    return run


def find_log(log_dir: Path, mode: str) -> Path:
    """在 headless/<stamp>-<pid>/ 下找最新一份 <mode>-headless.log。"""
    if log_dir.is_file():
        return log_dir
    hits = sorted(log_dir.glob(f"headless/*/{mode}-headless.log"))
    if not hits:
        hits = sorted(log_dir.glob(f"**/{mode}-headless.log"))
    if not hits:
        raise FileNotFoundError(f"{log_dir} 底下找不到 {mode}-headless.log")
    return hits[-1]


def launch(binary: Path, seed: int, mode: str, log_dir: Path, exe_root: Path) -> Path:
    log_dir.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(RUNNER),
           "--exe", str(binary), "--exe-root", str(exe_root),
           "--seed", str(seed), "--modes", mode, "--games", "1",
           "--parallel", "1", "--log-dir", str(log_dir)]
    print("  $ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=False)
    return find_log(log_dir, mode)


def indexed_diff(a: list[str], b: list[str]) -> tuple[set[int], bool]:
    """回傳 (不同的行索引集合, 行數是否相同)。

    行數不同時改用 SequenceMatcher 標記 replaced 區塊，整段算差異，
    因為多一行或少一行本身就是語義層面的變化，不該被當成噪聲吃掉。
    """
    if len(a) == len(b):
        return {i for i, (x, y) in enumerate(zip(a, b)) if x != y}, True
    diff: set[int] = set()
    matcher = difflib.SequenceMatcher(None, a, b, autojunk=False)
    for tag, i1, i2, _j1, _j2 in matcher.get_opcodes():
        if tag != "equal":
            diff.update(range(i1, max(i2, i1 + 1)))
    return diff, False


def show(lines: list[str], indexes: set[int], other: list[str], sign: str) -> None:
    for i in sorted(indexes):
        mine = lines[i] if i < len(lines) else "<缺行>"
        theirs = other[i] if i < len(other) else "<缺行>"
        tag = "  ← AI 評估值，浮點抖動" if AI_EVAL.search(mine) else ""
        print(f"    [{i:>3}] {sign} {mine}{tag}")
        print(f"          {sign} {theirs}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a-binary", type=Path, help="舊 binary（基準）")
    ap.add_argument("--b-binary", type=Path, help="新 binary（被驗的改動）")
    ap.add_argument("--a-log-dir", type=Path, help="直接讀既有的 A log 目錄")
    ap.add_argument("--b-log-dir", type=Path, help="直接讀既有的 B log 目錄")
    ap.add_argument("--b2-log-dir", type=Path, help="直接讀既有的 B 第二次 log 目錄")
    ap.add_argument("--seed", type=int, default=20261119)
    ap.add_argument("--mode", default="02p")
    ap.add_argument("--b-repeats", type=int, default=2,
                    help="B 跑幾次（>=2 才能分辨噪聲，預設 2）")
    ap.add_argument("--work-dir", type=Path, default=Path("/tmp/binary-compare"),
                    help="重新跑時 log 的落地根目錄")
    ap.add_argument("--exe-root", type=Path, default=Path.cwd())
    args = ap.parse_args(argv)

    a: Run | None = None
    b: Run | None = None
    b2: Run | None = None

    if args.a_log_dir and args.b_log_dir:
        a = parse_log(find_log(args.a_log_dir, args.mode), "A(舊)")
        b = parse_log(find_log(args.b_log_dir, args.mode), "B(新)")
        if args.b2_log_dir:
            b2 = parse_log(find_log(args.b2_log_dir, args.mode), "B2(新,第二次)")
    else:
        missing = [n for n in ("a_binary", "b_binary") if getattr(args, n) is None]
        if missing:
            ap.error("要嘛給 --a-log-dir/--b-log-dir，要嘛給 --a-binary/--b-binary")
        for binary in (args.a_binary, args.b_binary):
            if not Path(binary).is_file():
                ap.error(f"binary 不存在：{binary}")
        print(f"[跑 A] {args.a_binary}")
        a = parse_log(launch(args.a_binary, args.seed, args.mode,
                             args.work_dir / "a", args.exe_root), "A(舊)")
        b = parse_log(launch(args.b_binary, args.seed, args.mode,
                             args.work_dir / "b1", args.exe_root), "B(新)")
        if args.b_repeats >= 2:
            print(f"[跑 B 第二次] {args.b_binary}")
            b2 = parse_log(launch(args.b_binary, args.seed, args.mode,
                                  args.work_dir / "b2", args.exe_root), "B2(新,第二次)")

    print()
    print("=" * 72)
    print(f"mode={args.mode}  seed={args.seed}")
    print("=" * 72)
    for run in (a, b, b2):
        if run is None:
            continue
        took = f"{run.startup:.3f} s" if run.startup is not None else "量不到（缺時間戳標記）"
        print(f"{run.label:<16} log={run.log}")
        print(f"{'':<16} 行數={len(run.lines)}  啟動耗時={took}")

    print()
    print("--- 盤面比對（已去除時間戳）---")
    cross, same_len = indexed_diff(a.lines, b.lines)
    if not same_len:
        print(f"  警告：行數不同 A={len(a.lines)} B={len(b.lines)}，差異整段計入跨 binary")
    print(f"  A vs B  差異行數 = {len(cross)}")
    if cross:
        show(a.lines, cross, b.lines, "-/+")

    real = set(cross)
    if b2 is not None:
        self_diff, self_same_len = indexed_diff(b.lines, b2.lines)
        if not self_same_len:
            print(f"  警告：行數不同 B={len(b.lines)} B2={len(b2.lines)}")
        print(f"  B vs B2 差異行數 = {len(self_diff)}")
        if self_diff:
            show(b.lines, self_diff, b2.lines, "-/+")
        real = cross - self_diff
        print(f"  同 binary 兩次也在變（判為噪聲）的行數 = {len(cross & self_diff)}")

    if cross:
        ai = [i for i in cross if AI_EVAL.search(a.lines[i] if i < len(a.lines) else "")]
        if ai:
            print(f"  註：A vs B 的 {len(cross)} 行差異裡有 {len(ai)} 行落在 AI 決策評估值行；")
            print("      這類行末尾浮點每次都抖，跨門檻時連決策標籤也會跟著換，屬同一種噪聲。")

    print()
    print("--- 結論 ---")
    if b2 is None:
        print("  只跑了 B 一次，無法區分噪聲與真差異。要下判定請用 --b-repeats 2 或給 --b2-log-dir。")
    elif not real:
        print("  真差異 0 行 → 盤面一致，改動不影響遊戲語義。")
    else:
        print(f"  真差異 {len(real)} 行 → 需要人工判讀（可能是語義變化，不是噪聲）：")
        show(a.lines, real, b.lines, "-/+")

    if a.startup is not None and b.startup is not None:
        saved = a.startup - b.startup
        print(f"  啟動耗時 A={a.startup:.3f}s B={b.startup:.3f}s → 省 {saved:.3f}s/格")
        if b2 is not None and b2.startup is not None:
            avg = (b.startup + b2.startup) / 2
            print(f"  B 兩次平均 {avg:.3f}s，省 {a.startup - avg:.3f}s/格，48 格約省 {(a.startup - avg) * 48 / 60:.1f} 分鐘/將")

    if b2 is None:
        return 0
    return 1 if real else 0


if __name__ == "__main__":
    sys.exit(main())
