#!/usr/bin/env python3
"""Group failed standard-acceptance headless cells by crash fingerprint (read-only).

For each general the newest summary-headless-YYYYMMDD-HHMMSS.csv is used, chosen by
the timestamp inside the file name (standard-accept-driver.sh took glob's first match,
the oldest CSV, and kept re-judging luxun from a stale run). Every row whose ok column
is not "True" is fingerprinted from the log its `log` column names:

  signal  - exit_name from the CSV (SIGSEGV, SIGABRT, SIGKILL ...), or BUDGET:<kind>
  cxx     - a C++ abort marker (terminate/assert/glibc heap message) or the first two
            non-libc backtrace frames, when the log carries them
  lua     - the first Lua error line, reduced to file:line: message
  ai      - the last [AI_PHASE_END] line, reduced to kind/phase

Cells with the same four parts form one class. A cell whose log is missing or
unreadable is listed as unclassified. Nothing is written.
"""

import argparse
import csv
import glob
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STAMP = re.compile(r"summary-headless-(\d{8}-\d{6})\.csv$")
LUA_ERROR = re.compile(r"AI_CALLBACK_ERROR|AI_LUA_ERROR|AI_RUNTIME_ERROR|stack overflow"
                       r"|attempt to |\berror:")
LUA_LOCATION = re.compile(r"((?:lua|extensions)/[\w./-]+\.lua):(\d+):\s*(.*)")
CXX_MARKER = re.compile(r"terminate called after throwing an instance of '[^']+'"
                        r"|Assertion [`'].*?failed|ASSERT(?: failure)?:.*"
                        r"|double free[^\n]*|free\(\): [^\n]*|malloc\(\): [^\n]*"
                        r"|corrupted [^\n]*|pure virtual method called")
FRAME = re.compile(r"^#\d+\s+(?:0x[0-9a-fA-F]+ in )?([^\s(]+)")
LIBC_FRAME = ("raise", "abort", "__GI_", "__pthread", "gsignal", "__restore_rt",
              "std::terminate", "__cxa_", "__gnu_cxx::", "operator new", "_Unwind_")
PHASE = re.compile(r"\[AI_PHASE_END\].*?kind=\s*(\d+).*?phase=(\S+)")
EVENT = re.compile(r"^(\[LOG\]|\[AUTOTEST\])")


def latest_csv(general_dir):
    found = []
    for path in glob.glob(os.path.join(general_dir, "headless", "summary-headless-*.csv")):
        match = STAMP.search(os.path.basename(path))
        if match:
            found.append((match.group(1), path))
    # YYYYMMDD-HHMMSS compares chronologically as a string.
    return max(found)[1] if found else None


def reduce_lua(line):
    match = LUA_LOCATION.search(line)
    if match:
        return "%s:%s: %s" % (match.group(1), match.group(2), match.group(3).strip()[:120])
    return re.sub(r"\d+", "N", line.strip())[:140]


def scan(log_path):
    first_lua = cxx = last_phase = last_event = None
    frames = []
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if first_lua is None and LUA_ERROR.search(line):
                first_lua = reduce_lua(line)
            if cxx is None:
                marker = CXX_MARKER.search(line)
                if marker:
                    cxx = marker.group(0).strip()[:140]
            frame = FRAME.match(line.strip())
            if frame and len(frames) < 2 and not frame.group(1).startswith(LIBC_FRAME):
                frames.append(frame.group(1))
            phase = PHASE.search(line)
            if phase:
                last_phase = "kind=%s phase=%s" % (phase.group(1), phase.group(2))
            if EVENT.match(line):
                last_event = line
    if cxx is None and frames:
        cxx = " <- ".join(frames)
    return {"cxx": cxx, "lua": first_lua, "ai": last_phase, "last_event": last_event}


def collect(date):
    base = os.path.join(ROOT, "artifacts", "standard-accept-%s" % date, "raw")
    if not os.path.isdir(base):
        sys.exit("no raw directory: %s" % base)
    cells, unclassified = [], []
    for general in sorted(os.listdir(base)):
        summary = latest_csv(os.path.join(base, general))
        if not summary:
            continue
        with open(summary, encoding="utf-8-sig", newline="") as handle:
            rows = list(csv.DictReader(handle))
        for row in rows:
            if row.get("ok") == "True":
                continue
            budget = (row.get("budget") or "").strip()
            signal = ("BUDGET:" + budget) if budget else (row.get("exit_name") or "").split(" ")[0] \
                or "exit=%s" % row.get("exit")
            cell = {"general": general, "mode": row.get("mode"), "exit": row.get("exit"),
                    "signal": signal, "log": row.get("log") or "", "csv": summary}
            if not cell["log"] or not os.path.isfile(cell["log"]):
                cell["reason"] = "log missing"
                unclassified.append(cell)
                continue
            try:
                cell.update(scan(cell["log"]))
            except OSError as error:
                cell["reason"] = str(error)
                unclassified.append(cell)
                continue
            cell["fingerprint"] = " | ".join([
                signal, "cxx=" + (cell["cxx"] or "-"), "lua=" + (cell["lua"] or "-"),
                "ai=" + (cell["ai"] or "-")])
            cells.append(cell)
    return cells, unclassified


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--date", required=True, help="artifacts/standard-accept-<date>")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args()
    cells, unclassified = collect(args.date)
    groups = {}
    for cell in cells:
        groups.setdefault(cell["fingerprint"], []).append(cell)
    ordered = sorted(groups.items(), key=lambda item: (-len(item[1]), item[0]))
    if args.json:
        json.dump({"classes": [{"fingerprint": key, "members": members} for key, members in ordered],
                   "unclassified": unclassified}, sys.stdout, ensure_ascii=False, indent=2)
        print()
        return 0
    print("failed cells: %d, classes: %d, unclassified: %d"
          % (len(cells) + len(unclassified), len(ordered), len(unclassified)))
    for index, (key, members) in enumerate(ordered, 1):
        head = members[0]
        print("\n[class %d] %d cell(s)" % (index, len(members)))
        print("  signal : %s" % head["signal"])
        print("  cxx    : %s" % (head["cxx"] or "-"))
        print("  lua    : %s" % (head["lua"] or "-"))
        print("  ai     : %s" % (head["ai"] or "-"))
        for cell in members:
            print("  member : %s/%s exit=%s" % (cell["general"], cell["mode"], cell["exit"]))
        print("  last   : %s" % (head["last_event"] or "-")[:160])
        print("  log    : %s" % os.path.relpath(head["log"], ROOT))
    if unclassified:
        print("\n[unclassified]")
        for cell in unclassified:
            print("  %s/%s exit=%s %s: %s" % (cell["general"], cell["mode"], cell["exit"],
                                             cell["reason"], cell["log"] or "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
