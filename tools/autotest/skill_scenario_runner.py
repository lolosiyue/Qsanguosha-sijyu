"""Run skill scenario cases through qsanguosha_skill_scenario and check expectations.

Each case builds a real Room from a JSON board, fires a Lua trigger, and the host
reports the history facts and final board. This runner normalizes that report
(seat numbers, card specs) and matches the case's "expect" against it.
See tools/autotest/README.md for the case format.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PLACES = ["hand", "equip", "delayed_trick", "judge", "special", "discard_pile",
          "draw_pile", "table", "unknown", "wugu", "draw_pile_bottom"]
SUITS = ["spade", "club", "heart", "diamond", "no_suit_black", "no_suit_red", "no_suit"]
NATURES = ["normal", "fire", "thunder", "ice", "god", "poison"]


def pick(table, index):
    return table[index] if isinstance(index, int) and 0 <= index < len(table) else index


def card_spec(card):
    if not card:
        return None
    return f"{card.get('name')}/{pick(SUITS, card.get('suit'))}/{card.get('number')}"


def normalize(report):
    """Turn the host report into the view that "expect" is matched against."""
    seat_of = {seat["name"]: seat["seat"] for seat in report.get("seats", [])}

    def seat(name):
        return seat_of.get(name, name or None)

    view = {
        "error": report.get("error"),
        "winner": report.get("winner"),
        "seats": {str(s["seat"]): {k: v for k, v in s.items() if k not in ("seat", "name")}
                  for s in report.get("seats", [])},
        "invoked": [], "used": [], "responded": [], "recover": [], "moves": [],
        "damage": [],
    }
    for fact in report.get("facts", []):
        kind, data = fact.get("kind"), fact.get("data", {})
        if kind == "skill_invoked":
            if str(data.get("invoked_skill", "")).startswith("#"):
                continue  # hidden helper skills are implementation detail
            view["invoked"].append({"seat": seat(data.get("player")), "skill": data.get("invoked_skill")})
        elif kind == "use_card":
            view["used"].append({"seat": seat(data.get("from")), "card": card_spec(data.get("card")),
                                 "targets": [seat(t) for t in data.get("targets", [])]})
        elif kind == "respond_card":
            view["responded"].append({"seat": seat(data.get("player")), "card": card_spec(data.get("card"))})
        elif kind == "actual_recover":
            view["recover"].append({"from": seat(data.get("from")), "to": seat(data.get("to")),
                                    "amount": data.get("amount")})
        elif kind == "move":
            view["moves"].append({
                "card": card_spec(data.get("card")), "from": seat(data.get("from")),
                "to": seat(data.get("to")), "from_place": pick(PLACES, data.get("from_place")),
                "to_place": pick(PLACES, data.get("to_place")),
                "skill": data.get("reason_skill") or data.get("skill_name") or None})
    for fact in report.get("damage", []):
        data = fact.get("data", {})
        view["damage"].append({
            "from": seat(data.get("from")), "to": seat(data.get("to")), "amount": data.get("amount"),
            "hp_loss": data.get("hp_loss"), "absorbed": data.get("absorbed"),
            "nature": pick(NATURES, data.get("nature")), "card": card_spec(data.get("card"))})
    return view


def match(expected, actual, path="expect"):
    """Dicts match as subsets, lists as ordered subsequences ([] means none), scalars by equality."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return [f"{path}: expected an object, got {json.dumps(actual, ensure_ascii=False)}"]
        problems = []
        for key, value in expected.items():
            if key not in actual:
                problems.append(f"{path}.{key}: missing")
            else:
                problems += match(value, actual[key], f"{path}.{key}")
        return problems
    if isinstance(expected, list):
        if not isinstance(actual, list):
            return [f"{path}: expected a list, got {json.dumps(actual, ensure_ascii=False)}"]
        if not expected:
            return [] if not actual else [f"{path}: expected none, got {json.dumps(actual, ensure_ascii=False)}"]
        position = 0
        for index, item in enumerate(expected):
            while position < len(actual) and match(item, actual[position]):
                position += 1
            if position == len(actual):
                return [f"{path}[{index}]: no later entry matches {json.dumps(item, ensure_ascii=False)}"]
            position += 1
        return []
    if isinstance(expected, str) and isinstance(actual, str) and actual.count("/") == 2:
        # Card specs: "peach" or "peach/heart" match "peach/heart/3".
        parts = expected.split("/")
        if parts == actual.split("/")[:len(parts)]:
            return []
    if expected != actual:
        return [f"{path}: expected {json.dumps(expected, ensure_ascii=False)}, "
                f"got {json.dumps(actual, ensure_ascii=False)}"]
    return []


def run_case(case_path, args, env):
    case = json.loads(case_path.read_text(encoding="utf-8"))
    out_dir = Path(args.out_dir) if args.out_dir else case_path.parent / "results"
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / f"{case_path.stem}.report.json"
    log_path = out_dir / f"{case_path.stem}.log"
    report_path.unlink(missing_ok=True)
    command = [str(args.exe), "--case", str(case_path.resolve()), "--report", str(report_path.resolve()),
               "--seed", str(args.seed), "--timeout", str(args.timeout)]
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        try:
            code = subprocess.run(command, cwd=args.exe.parent, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=args.timeout + 30).returncode
        except subprocess.TimeoutExpired:
            code = None
    if not report_path.exists():
        return case_path, "ERROR", [f"no report (exit {code}); see {log_path}"]
    view = normalize(json.loads(report_path.read_text(encoding="utf-8")))
    (out_dir / f"{case_path.stem}.view.json").write_text(
        json.dumps(view, ensure_ascii=False, indent=2), encoding="utf-8")
    expect = dict(case.get("expect", {}))
    expected_error = expect.pop("error", None)
    problems = match(expect, view)
    # The error is a Lua message with traceback; the case names a substring of it.
    actual_error = view["error"]
    if expected_error is None and actual_error is not None:
        problems.insert(0, f"error: {actual_error}")
    elif expected_error is not None and expected_error not in (actual_error or ""):
        problems.insert(0, f"expect.error: expected {expected_error!r} in {actual_error!r}")
    if code != 0 and not problems:
        problems = [f"host exit {code}; see {log_path}"]
    return case_path, "PASS" if not problems else "FAIL", problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("cases", nargs="+", type=Path, help="case JSON files")
    parser.add_argument("--exe", type=Path, required=True, help="qsanguosha_skill_scenario executable")
    parser.add_argument("--seed", type=int, required=True, help="unsigned game seed, e.g. yyyyMMdd")
    parser.add_argument("--qt-bin", help="Qt bin directory to prepend to PATH")
    parser.add_argument("--out-dir", help="where reports go (default: results/ next to each case)")
    parser.add_argument("--timeout", type=int, default=120, help="seconds per case")
    parser.add_argument("--jobs", type=int, default=1, help="cases run in parallel")
    args = parser.parse_args()
    args.exe = args.exe.resolve()
    env = dict(os.environ)
    if args.qt_bin:
        env["PATH"] = args.qt_bin + os.pathsep + env.get("PATH", "")

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(lambda case: run_case(case, args, env), args.cases))
    for case_path, status, problems in results:
        print(f"[{status}] {case_path}")
        for problem in problems:
            print(f"    {problem}")
    statuses = {status for _, status, _ in results}
    sys.exit(2 if "ERROR" in statuses else 1 if "FAIL" in statuses else 0)


if __name__ == "__main__":
    main()
