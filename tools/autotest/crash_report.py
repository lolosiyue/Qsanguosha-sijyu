# -*- coding: utf-8 -*-
"""集中閃退資訊工具: 掃描 PROD 環境的 dmp / autotest-logs / record,
建立「閃退局 ↔ 檔案」對應, 產出 crash report 到
<exe-root>/tools/autotest/autotest-logs/crash-report/<時間戳>/。

可持續執行 (不刪改任何既有檔案): 每次執行新增時間戳目錄。
含 minidump 結構化解析 (例外碼/位址/崩潰模組), 不需 cdb/windbg。

用法:
    python crash_report.py --exe-root \\\\DESKTOP-VON1J9F\\game\\sgs\\QSanguoshaFinal
    python crash_report.py --exe-root <倉庫根> --log-dir <log 目錄>
"""
import argparse
import datetime
import os
import sys

from runner_common import log_dir_for, stamp, write_csv
from minidump_parse import parse_dump

# DIA 符號解析為可選依賴 (需 VS msdia140.dll + 對應 PDB)
try:
    from dia_symbol import DiaPdb
    HAS_DIA = True
except Exception:
    HAS_DIA = False

MARK_GAME_START = "[AUTOTEST] game start"
MARK_GAME_OVER = "[AUTOTEST] game over"
HEADLESS_HEADER = "Headless stress test started"


def mtime(path):
    """檔案修改時間 (datetime)。"""
    try:
        return datetime.datetime.fromtimestamp(os.path.getmtime(path))
    except OSError:
        return None


def diff_seconds(a, b):
    if a is None or b is None:
        return None
    return int(abs((a - b).total_seconds()))


def collect_dumps(exe_root):
    """掃描 <root>/dmp/*.dmp, 逐顆結構化解析。"""
    dmp_dir = os.path.join(exe_root, "dmp")
    out = []
    if not os.path.isdir(dmp_dir):
        return out
    for name in sorted(os.listdir(dmp_dir)):
        if not name.lower().endswith(".dmp"):
            continue
        p = os.path.join(dmp_dir, name)
        info = parse_dump(p)
        info["path"] = p
        info["ts"] = mtime(p)
        info["size"] = os.path.getsize(p)
        out.append(info)
    return out


def collect_logs(log_dir):
    """掃描 autotest-logs 下所有 .log, 回傳 [(path, rel, ts, size)]。"""
    out = []
    if not os.path.isdir(log_dir):
        return out
    for root, _dirs, files in os.walk(log_dir):
        if os.sep + "crash-report" in root or root.endswith("crash-report"):
            continue  # 排除 crash-report 本身 (可持續執行不自我掃描)
        for f in files:
            if f.endswith(".log"):
                p = os.path.join(root, f)
                rel = os.path.relpath(p, log_dir)
                out.append((p, rel, mtime(p), os.path.getsize(p)))
    return out


def collect_records(exe_root):
    """掃描 <root>/record/*.txt (排除 debug.txt 共用 log)。"""
    rec_dir = os.path.join(exe_root, "record")
    out = []
    if not os.path.isdir(rec_dir):
        return out
    for name in sorted(os.listdir(rec_dir)):
        if not name.lower().endswith(".txt") or name.lower() == "debug.txt":
            continue
        p = os.path.join(rec_dir, name)
        out.append((name, mtime(p)))
    return out


def collect_debug_backups(log_dir):
    """掃描 autotest-logs 下 network 批次的局前備份檔:
    debug-before-runN.txt (record/debug.txt 複本, 閃退局即時記錄)
    ai-cstring{,Event}-before-runN.txt (lua/ai/cstring 複本)。
    回傳 {type: [(rel, ts)]}, type ∈ debug / ai_cstring / ai_cstringEvent。"""
    out = {"debug": [], "ai_cstring": [], "ai_cstringEvent": []}
    if not os.path.isdir(log_dir):
        return out
    for root, _dirs, files in os.walk(log_dir):
        if os.sep + "crash-report" in root or root.endswith("crash-report"):
            continue
        for f in files:
            if not f.endswith(".txt"):
                continue
            if f.startswith("debug-before-run"):
                out["debug"].append(
                    (os.path.relpath(os.path.join(root, f), log_dir),
                     mtime(os.path.join(root, f))))
            elif f.startswith("ai-cstringEvent-before-run"):
                out["ai_cstringEvent"].append(
                    (os.path.relpath(os.path.join(root, f), log_dir),
                     mtime(os.path.join(root, f))))
            elif f.startswith("ai-cstring-before-run"):
                out["ai_cstring"].append(
                    (os.path.relpath(os.path.join(root, f), log_dir),
                     mtime(os.path.join(root, f))))
    return out


def parse_markers(path):
    """讀 autotest.log 的 [AUTOTEST] 標記行: [(時間, 行)]。"""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return []
    out = []
    for line in data.decode("utf-8", errors="replace").splitlines():
        if "[AUTOTEST]" in line or "Headless stress test" in line:
            out.append(line)
    return out


def game_events(markers):
    """標記行整理成局事件: [(start_t, over_t, winner, kind)]"""
    games = []
    cur = None
    for m in markers:
        t = m[:23]
        if MARK_GAME_START in m:
            cur = [t, None, None, "network"]
        elif MARK_GAME_OVER in m:
            w = m.split(MARK_GAME_OVER, 1)[1].strip() or "(empty)"
            if cur is not None:
                cur[1] = t
                cur[2] = w
                games.append(cur)
                cur = None
            else:
                games.append([t, t, w, "network"])
    if cur is not None:
        cur[2] = cur[2] or "(no game over)"
        games.append(cur)
    return games


def summarize_batches(log_dir):
    """每個批次的局數/結果摘要 (供 crash report 附加)。"""
    rows = []
    for kind in ("headless", "network"):
        base = os.path.join(log_dir, kind)
        if not os.path.isdir(base):
            continue
        if kind == "network":
            for batch in sorted(os.listdir(base)):
                bp = os.path.join(base, batch)
                if not os.path.isdir(bp):
                    continue
                for mode in sorted(os.listdir(bp)):
                    mp = os.path.join(bp, mode)
                    if not os.path.isdir(mp):
                        continue
                    marker = os.path.join(mp, "autotest.log")
                    evs = game_events(parse_markers(marker)) \
                        if os.path.isfile(marker) else []
                    run_logs = [f for f in os.listdir(mp)
                                if f.startswith("run") and f.endswith(".log")]
                    rows.append([kind, batch, mode, len(run_logs),
                                 len(evs),
                                 "; ".join("%s->%s" % (g[0][11:19], g[2])
                                           for g in evs)])
        else:
            for f in sorted(os.listdir(base)):
                if f.endswith("-headless.log"):
                    p = os.path.join(base, f)
                    rows.append([kind, f, "-", 1, 0,
                                 "last=%s" % (mtime(p).strftime("%H:%M:%S")
                                              if mtime(p) else "-")])
    return rows


def match_nearest(candidates, t):
    """回傳 (最近者, 差秒); candidates: [(payload, ts)]。"""
    best, best_d = None, None
    for payload, ct in candidates:
        d = diff_seconds(t, ct)
        if d is None:
            continue
        if best_d is None or d < best_d:
            best, best_d = payload, d
    return best, best_d


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(
        description="集中閃退資訊: dmp ↔ log ↔ record 對應 + crash report")
    parser.add_argument("--exe-root", default=os.getcwd(),
                        help="遊戲根目錄 (內含 dmp/ record/, log 在 tools/autotest/autotest-logs)")
    parser.add_argument("--log-dir", default=None,
                        help="log 目錄 (預設: <exe-root>/tools/autotest/autotest-logs)")
    parser.add_argument("--pdb", default=None,
                        help="PDB 檔 (預設: <exe-root>/release/QSanguosha.pdb; 提供時以 DIA 附符號)")
    parser.add_argument("--symbol-map", action="append", default=[],
                        metavar="MODULE=PDB",
                        help="外部模組符號對應 (可重複), 例: VCRUNTIME140.dll=C:\\x\\vcruntime140.amd64.pdb")
    args = parser.parse_args()

    exe_root = args.exe_root.rstrip("\\/")
    log_dir = log_dir_for(args)
    report_dir = os.path.join(log_dir, "crash-report", stamp())
    os.makedirs(report_dir, exist_ok=True)

    print("exe-root : %s" % exe_root)
    print("log-dir  : %s" % log_dir)
    print("report   : %s" % report_dir)

    dmps = collect_dumps(exe_root)
    logs = [(p, rel, ts, size) for p, rel, ts, size in collect_logs(log_dir)
            if ts is not None]
    recs = collect_records(exe_root)
    dbg_backups = collect_debug_backups(log_dir)
    n_dbg = sum(len(v) for v in dbg_backups.values())
    print("dmp=%d log=%d record=%d debug_backup=%d"
          % (len(dmps), len(logs), len(recs), n_dbg))

    # 符號解析 (可選): 崩潰模組為 QSanguosha.exe 時附函式名+行號
    pdb = None
    pdb_path = args.pdb or os.path.join(exe_root, "release", "QSanguosha.pdb")
    extra_pdbs = {}  # module 名 (basename) -> DiaPdb
    if not os.path.isfile(pdb_path):
        print("symbols : 無 PDB (%s), 不附符號 (可用 --pdb 指定)" % pdb_path)
    elif HAS_DIA:
        try:
            pdb = DiaPdb(pdb_path)
            print("symbols : %s" % pdb_path)
        except Exception as e:
            print("symbols : 無法載入 PDB (%s), 不附符號" % e)
    else:
        print("symbols : 缺 msdia140.dll (VS DIA SDK), 不附符號")
    if HAS_DIA:
        for item in args.symbol_map:
            if "=" not in item:
                print("symbols : 忽略 --symbol-map 格式錯誤: %s" % item)
                continue
            mod, spath = item.split("=", 1)
            if os.path.isfile(spath):
                try:
                    extra_pdbs[mod] = DiaPdb(spath)
                    print("symbols : %s -> %s" % (mod, spath))
                except Exception as e:
                    print("symbols : %s 無法載入 (%s)" % (spath, e))
            else:
                print("symbols : %s 不存在 (%s)" % (spath, mod))

    # 每顆 dmp: 匹配最近 log / 最近 record
    rows = []
    for d in dmps:
        t = d["ts"]
        if t is None:
            continue
        # log 匹配: 優先 runN.log / server.log (局專屬), 其次 autotest.log 等
        run_cands = [(rel, lt) for _p, rel, lt, _s in logs
                     if rel.split(os.sep)[-1].startswith(("run", "server"))]
        best_log, lg = match_nearest(run_cands, t)
        if best_log is None or (lg is not None and lg > 120):
            best_log, lg = match_nearest(
                [(rel, lt) for _p, rel, lt, _s in logs], t)
        best_rec, rg = match_nearest(recs, t)
        # debug-before-runN.txt 在 run N 開始前複製 = 含 run N-1 內容;
        # 閃退局 runN 的備份 = 時間在 dmp 之後的第一個備份檔
        def _first_after(cands):
            after = sorted(
                [(rel, dt) for rel, dt in cands
                 if dt is not None and dt >= t], key=lambda x: x[1])
            if after:
                return after[0][0], diff_seconds(after[0][1], t)
            return "-", "-"
        dbg_rel, dbg_diff = _first_after(dbg_backups["debug"])
        ai_rel, ai_diff = _first_after(dbg_backups["ai_cstring"]
                                       + dbg_backups["ai_cstringEvent"])
        # 符號: 崩潰模組為 QSanguosha.exe 時用 DIA 查函式; 外部模組用 --symbol-map
        sym_name, sym_disp, sym_line = "-", "-", "-"
        fmod = d.get("fault_module", "")
        frva = d.get("fault_rva", -1)
        if frva >= 0:
            sympdb = None
            if fmod == "QSanguosha.exe":
                sympdb = pdb
            elif fmod in extra_pdbs:
                sympdb = extra_pdbs[fmod]
            if sympdb:
                name, disp, line = sympdb.lookup(frva)
                sym_name = name or "<no-sym>"
                sym_disp = "0x%X" % disp
                sym_line = line or ""
        rows.append([
            d["file"],
            t.strftime("%Y-%m-%d %H:%M:%S"),
            d["size"],
            d.get("exc_code", "-"),
            d.get("exc_name", "-"),
            d.get("exc_addr", "-"),
            d.get("fault_module", "-"),
            "0x%X" % d["fault_rva"] if d.get("fault_rva", -1) >= 0 else "-",
            d.get("exc_thread", "-"),
            d.get("n_threads", "-"),
            d.get("n_modules", "-"),
            best_log or "-",
            lg if lg is not None else "-",
            best_rec or "-",
            rg if rg is not None else "-",
            dbg_rel,
            dbg_diff,
            ai_rel,
            ai_diff,
            sym_name,
            sym_disp,
            sym_line,
        ])

    header = ["dmp_file", "dmp_time", "size", "exc_code", "exc_name",
              "exc_addr", "fault_module", "fault_rva", "exc_thread",
              "n_threads", "n_modules", "nearest_log", "log_diff_s",
              "nearest_record", "record_diff_s", "debug_backup",
              "debug_backup_diff_s", "ai_backup", "ai_backup_diff_s",
              "symbol", "symbol_disp", "symbol_line"]
    csv_path = os.path.join(report_dir, "inventory.csv")
    write_csv(csv_path, header, rows)
    print("inventory: %s (%d dmp)" % (csv_path, len(rows)))

    # 批次摘要
    rows2 = summarize_batches(log_dir)
    csv2 = os.path.join(report_dir, "batches.csv")
    if rows2:
        write_csv(csv2, ["kind", "batch", "mode", "run_logs", "games",
                         "detail"], rows2)
        print("batches  : %s (%d rows)" % (csv2, len(rows2)))

    # 控制台摘要
    print("-" * 100)
    for r in rows:
        print("%s | %s | %s | %s | %s | %s | %s | %s | log=%s(%ss)" % (
            r[0], r[1], r[3], r[6], r[7], r[19], r[21], r[2],
            r[11], r[12]))
    if pdb:
        pdb.close()
    for ep in extra_pdbs.values():
        ep.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
