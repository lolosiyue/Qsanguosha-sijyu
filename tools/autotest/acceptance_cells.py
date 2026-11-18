#!/usr/bin/env python3
"""Per-cell completion for one general's headless acceptance directory.

general_acceptance.py --resume and standard-accept-driver.sh both need the
answer to "which modes of this round's matrix are already done?". They used
to answer it separately (resume read logs, the driver read the newest CSV),
and the two answers drifted apart: resume skipped cells the driver then
counted as missing, so a general could never be recorded as done. Both now
call cell_states() here.

Layout (written by headless_runner.py --log-dir <dir>):
  <dir>/modes.txt                          this round's matrix, one mode per line
  <dir>/summary-headless-<stamp>.csv       one row per mode of a run
  <dir>/headless/<stamp>-<pid>/<mode>.log            process output (the CSV `log`)
  <dir>/headless/<stamp>-<pid>/<mode>-headless.log   game log, has the finish line

A mode is decided by its LATEST attempt, i.e. the newest run directory that has
a log for it. An older finished attempt does not count once a newer one exists.
  - A CSV row that belongs to that attempt decides: finished == expected and no
    budget stop means done. The row wins over the log text.
  - With no such row (the run was killed before it wrote its CSV), the attempt
    counts as done when its -headless.log has ">>> All games completed".
    standard_matrix_judge.py grades such a cell from the log too
    (standard_matrix_judge.py:466-474). Its exit code is unknown, so these
    cells are reported as log-only.

CLI (used by the driver):
  python3 acceptance_cells.py coverage <dir>
prints one line whose first word is ok, nomodes, nocsv or incomplete:...
"""

import csv
import os
import re
import sys
from pathlib import Path

FINISH = ">>> All games completed"
CSV_NAME = re.compile(r"^summary-headless-(\d{8}-\d{6})\.csv$")
RUN_DIR = re.compile(r"^\d{8}-\d{6}(?:-\d+)?$")
HEADLESS_SUFFIX = "-headless.log"


def read_matrix(directory):
    """Modes listed in <dir>/modes.txt, or None when the file is missing or empty."""
    try:
        text = (Path(directory) / "modes.txt").read_text(encoding="utf-8")
    except OSError:
        return None
    modes = [line.strip() for line in text.splitlines() if line.strip()]
    return modes or None


def write_matrix(directory, modes):
    path = Path(directory) / "modes.txt"
    tmp = path.with_name(".modes.txt.tmp")
    tmp.write_text("".join(m + "\n" for m in modes), encoding="utf-8")
    os.replace(tmp, path)


def _attempts(directory):
    """{mode: newest run directory name that has a log for that mode}."""
    latest = {}
    for log in Path(directory).rglob("*.log"):
        run = log.parent.name
        if not RUN_DIR.match(run):
            continue
        name = log.name
        mode = name[: -len(HEADLESS_SUFFIX)] if name.endswith(HEADLESS_SUFFIX) else name[: -len(".log")]
        # Run directory names start with YYYYMMDD-HHMMSS, so string order is time order.
        if mode not in latest or run > latest[mode][0]:
            latest[mode] = (run, log.parent)
    return latest


def _rows(directory):
    """{(run directory name, mode): CSV row}, newest CSV last so it wins a tie."""
    found = []
    for path in Path(directory).rglob("summary-headless-*.csv"):
        match = CSV_NAME.match(path.name)
        if match:
            found.append((match.group(1), str(path), path))
    rows = {}
    for _stamp, _key, path in sorted(found):
        try:
            with path.open(newline="", encoding="utf-8-sig") as handle:
                for row in csv.DictReader(handle):
                    log = (row.get("log") or "").replace("\\", "/")
                    if not log:
                        continue
                    # Match on run directory name + mode, not the full path: the
                    # path is absolute and the tree may have been copied or moved.
                    run = log.rstrip("/").split("/")[-2] if "/" in log else ""
                    rows[(run, row.get("mode", ""))] = row
        except (OSError, csv.Error):
            continue
    return rows, bool(found)


def row_done(row):
    """Same completion rule as standard_matrix_judge.py collect_csv_stats() "done".

    finished >= expected, not ==: impasse_fight starts a second game after a draw and
    reports finished=2/expected=1 (JUDGE-SPEC.md). failed_games == 0 and a marker
    status other than invalid keep a broken run from passing through the >=.
    """
    try:
        covers = int(row.get("finished", "")) >= int(row.get("expected", "")) > 0
    except (TypeError, ValueError):
        return False
    return (covers and (row.get("failed_games") or "").strip() in ("", "0")
            and (row.get("marker_status") or "").strip().lower() != "invalid")


def cell_states(directory, modes):
    """{mode: (state, detail)}; state is done, log-only, unfinished or missing."""
    attempts = _attempts(directory)
    rows, _ = _rows(directory)
    states = {}
    for mode in modes:
        if mode not in attempts:
            states[mode] = ("missing", "no log for this mode")
            continue
        run, run_dir = attempts[mode]
        row = rows.get((run, mode))
        if row is not None:
            finished, expected = row.get("finished", ""), row.get("expected", "")
            budget = (row.get("budget") or "").strip()
            if row_done(row) and not budget:
                states[mode] = ("done", run)
            else:
                states[mode] = ("unfinished", "%s finished=%s/%s%s" % (
                    run, finished or "?", expected or "?", " budget=" + budget if budget else ""))
            continue
        game_log = run_dir / (mode + HEADLESS_SUFFIX)
        try:
            text = game_log.read_text(encoding="utf-8", errors="replace")
        except OSError:
            text = ""
        if FINISH in text:
            states[mode] = ("log-only", run)
        else:
            states[mode] = ("unfinished", "%s no CSV row and no finish line" % run)
    return states


def completed(directory, modes):
    """Modes whose latest attempt finished (CSV row or log-only)."""
    return {m for m, (state, _) in cell_states(directory, modes).items() if state in ("done", "log-only")}


def coverage(directory):
    matrix = read_matrix(directory)
    if matrix is None:
        return "nomodes (modes.txt missing or empty: general_acceptance.py did not record this round's matrix)"
    _, any_csv = _rows(directory)
    attempts = _attempts(directory)
    if not any_csv and not any(m in attempts for m in matrix):
        return "nocsv"
    states = cell_states(directory, matrix)
    unfinished = [m for m in matrix if states[m][0] == "unfinished"]
    missing = [m for m in matrix if states[m][0] == "missing"]
    log_only = [m for m in matrix if states[m][0] == "log-only"]
    if unfinished:
        return "incomplete:" + ",".join(unfinished) + (
            ";missing=" + ",".join(missing) if missing else "")
    if missing:
        covered = len(matrix) - len(missing)
        return "incomplete:rows=%d/expected=%d,missing=%s" % (covered, len(matrix), ",".join(missing))
    return "ok" + (" (log-only=%d: %s)" % (len(log_only), ",".join(log_only)) if log_only else "")


def main(argv):
    if len(argv) == 3 and argv[1] == "coverage":
        print(coverage(argv[2]))
        return 0
    if len(argv) == 3 and argv[1] == "states":
        matrix = read_matrix(argv[2]) or []
        for mode, (state, detail) in cell_states(argv[2], matrix).items():
            print("%s\t%s\t%s" % (mode, state, detail))
        return 0
    print("usage: acceptance_cells.py coverage|states <headless dir>", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
