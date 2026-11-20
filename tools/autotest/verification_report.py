"""Read real decision/provider traces and resolution histories; never start a game."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import sys

MARKERS = {"[AI_DECISION_TRACE]": "decisions", "[AI_PROVIDER_TRACE]": "providers"}
SOURCES = {"isolated", "legacy_direct", "native_local", "native_fallback", "external_agent"}
MAX_LINE = 1024 * 1024


def identity(row):
    return tuple(row.get(k, "") for k in ("trace_run_id", "decision_id", "revision", "player"))


def authority_identity(row):
    return identity(row) + (row.get("room_id", ""), row.get("generation", ""))


def valid_stamp(row):
    return (row.get("schema") == 1 and isinstance(row.get("trace_run_id"), str)
            and isinstance(row.get("player"), str) and bool(row["player"])
            and all(isinstance(row.get(k), str) and row[k].isascii() and row[k].isdigit()
                    for k in ("decision_id", "revision")))


def collect(inputs):
    """Deduplicate copies of the same log line while preserving conflicting evidence."""
    result = {"decisions": [], "providers": [], "histories": [], "errors": [], "inputs": []}
    files, seen = set(), set()
    for source in inputs:
        path = Path(source)
        if path.is_dir():
            files.update(p for p in path.rglob("*") if p.is_file()
                         and (p.suffix.lower() in (".log", ".txt", ".jsonl")
                              or (p.name.startswith("history-") and p.suffix == ".json")))
        elif path.is_file():
            files.add(path)
        else:
            result["errors"].append({"file": str(path), "error": "input_missing"})
    for path in sorted(files):
        path = path.resolve()
        try:
            if path.name.startswith("history-") and path.suffix == ".json":
                raw = path.read_bytes()
                document = json.loads(raw)
                if (not isinstance(document, dict) or document.get("schema") != 1
                        or not isinstance(document.get("history"), dict)):
                    raise ValueError("invalid_history_envelope")
                history = document["history"]
                for collection in ("events", "facts"):
                    records = history.get(collection)
                    if (not isinstance(records, list)
                            or any(not isinstance(row, dict) or not isinstance(row.get("data"), dict)
                                   for row in records)):
                        raise ValueError("invalid_history_records")
                digest = hashlib.sha256(raw).hexdigest()
                if ("history", digest) not in seen:
                    seen.add(("history", digest))
                    document["evidence_file"] = str(path)
                    result["histories"].append(document)
                result["inputs"].append({"file": str(path), "sha256": digest})
                continue
            digest = hashlib.sha256()
            with path.open("rb") as stream:
                for number, raw in enumerate(stream, 1):
                    digest.update(raw)
                    line = raw.decode("utf-8", errors="replace")
                    for marker, kind in MARKERS.items():
                        raw_provider = (kind == "providers" and path.name.startswith("provider-")
                                        and path.suffix == ".jsonl")
                        if marker not in line and not raw_provider:
                            continue
                        if len(raw) > MAX_LINE:
                            result["errors"].append({"file": str(path), "line": number,
                                                     "error": "trace_line_too_large"})
                            continue
                        try:
                            row = json.loads(line.strip() if raw_provider else line.split(marker, 1)[1].strip())
                            if not isinstance(row, dict) or not valid_stamp(row):
                                raise ValueError("invalid_trace_stamp")
                            if kind == "decisions" and (
                                    type(row.get("accepted")) is not bool
                                    or row.get("actual_source") not in SOURCES
                                    or type(row.get("kind")) is not int
                                    or not isinstance(row.get("room_id"), str)
                                    or not isinstance(row.get("generation"), str)
                                    or not isinstance(row.get("attempts", []), list)):
                                raise ValueError("invalid_authority_trace")
                            if kind == "providers" and (
                                    row.get("provider") not in {"deepseek", "jev", "local_forced", "local_native"}
                                    or row.get("stage") not in {"route", "provider_answer", "queued", "rejected", "provider_error"}
                                    or type(row.get("submitted")) is not bool
                                    or type(row.get("provider_call_count")) is not int
                                    or row["provider_call_count"] < 0):
                                raise ValueError("invalid_provider_trace")
                            key = kind, json.dumps(row, sort_keys=True, separators=(",", ":"))
                            if key not in seen:
                                seen.add(key)
                                row["evidence"] = {"file": str(path), "line": number}
                                result[kind].append(row)
                        except (ValueError, TypeError) as error:
                            result["errors"].append({"file": str(path), "line": number,
                                                     "error": str(error)[:120]})
            result["inputs"].append({"file": str(path), "sha256": digest.hexdigest()})
        except (OSError, ValueError) as error:
            result["errors"].append({"file": str(path), "error": type(error).__name__})
    return result


def decision_summary(rows, pass_turns):
    sources, failures, recovered, grouped = {}, [], [], defaultdict(list)
    conflicts = defaultdict(list)
    for row in rows:
        conflicts[authority_identity(row)].append(row)
        source = row["actual_source"]
        counters = sources.setdefault(source, Counter())
        counters["decisions"] += 1
        counters["accepted"] += int(row["accepted"])
        result = row.get("result_name", row.get("result", "unknown"))
        counters["accepted_pass"] += int(row["accepted"] and result == "pass")
        counters["accepted_use_card"] += int(row["accepted"] and result in ("use_card", "useCard"))
        if not row["accepted"]:
            failures.append(row)
        elif any(attempt.get("error") for attempt in row.get("attempts", []) if isinstance(attempt, dict)):
            recovered.append(row)
        if row["kind"] == 0:
            grouped[(row.get("trace_run_id"), row.get("room_id"), row.get("generation"), row["player"])].append(row)
    suspicious = []
    for seat, decisions in grouped.items():
        # A final pass after using cards is ordinary play, not an empty turn.
        turns = {}
        for row in decisions:
            turn = str(row.get("turn", "0"))
            if turn == "0":
                continue
            turns.setdefault(turn, []).append(row)
        streak = []
        for turn, entries in turns.items():
            used = any(r["accepted"] and r.get("result_name", r.get("result")) in ("use_card", "useCard") for r in entries)
            passes = [r for r in entries if r["accepted"]
                      and r.get("result_name", r.get("result")) == "pass"
                      and type(r.get("usable_offered_action_count")) is int
                      and (r["usable_offered_action_count"] > 0 or r.get("candidates_complete") is False)]
            if used or not passes:
                streak = []
                continue
            streak.append({"turn": turn, "decision_id": passes[-1]["decision_id"],
                           "offered_actions": passes[-1]["usable_offered_action_count"],
                           "candidates_complete": passes[-1].get("candidates_complete"),
                           "evidence": passes[-1]["evidence"]})
            if len(streak) == pass_turns:
                suspicious.append({"trace_run_id": seat[0], "room_id": seat[1], "generation": seat[2],
                                   "player": seat[3], "status": "REVIEW", "turns": list(streak),
                                   "reason": "repeated_empty_play_turn_with_offered_actions" if
                                   all(item["offered_actions"] > 0 for item in streak) else
                                   "repeated_empty_play_turn_with_unknown_opportunities"})
    duplicate_ids = [{"identity": list(key), "evidence": [row["evidence"] for row in values]}
                     for key, values in conflicts.items() if len(values) > 1]
    return {"status": "UNKNOWN" if not rows or duplicate_ids else "FAIL" if failures else "REVIEW" if suspicious or recovered else "OBSERVED",
            "decision_count": len(rows), "sources": sources, "rejected_or_unfinished": failures,
            "recovered_errors": recovered, "suspicious_empty_turns": suspicious,
            "conflicting_decision_ids": duplicate_ids,
            "interpretation": "Authority acceptance is separate from resolved skill effects. Empty turns are review candidates, not strategy failures."}


def provider_summary(decisions, providers):
    authority = defaultdict(list)
    for row in decisions:
        authority[identity(row)].append(row)
    groups = defaultdict(list)
    for row in providers:
        groups[identity(row)].append(row)
    totals = {name: Counter() for name in ("deepseek", "jev")}
    outcomes = []
    for key, records in groups.items():
        for provider in ("deepseek", "jev"):
            selected = [r for r in records if r.get("provider") == provider]
            if not selected:
                continue
            answered = [r for r in selected if r.get("stage") == "provider_answer"]
            queued = [r for r in selected if r.get("stage") == "queued"
                      and r.get("submitted") is True and r.get("provider_call_count", 0) > 0]
            native = authority.get(key, [])
            # Provider self-report alone cannot certify an action was accepted.
            confirmed = (bool(key[0]) and len(native) == 1 and native[0]["accepted"]
                         and native[0]["actual_source"] == "external_agent" and bool(answered) and bool(queued))
            totals[provider]["observed_decisions"] += 1
            totals[provider]["answered_decisions"] += bool(answered)
            totals[provider]["queued_decisions"] += bool(queued)
            totals[provider]["authority_confirmed_decisions"] += bool(confirmed)
            totals[provider]["provider_errors"] += sum(r.get("stage") == "provider_error" for r in selected)
            totals[provider]["accepted_use_card"] += int(bool(confirmed) and native[0].get("result_name", native[0].get("result")) in ("use_card", "useCard"))
            outcomes.append({"trace_run_id": key[0], "decision_id": key[1], "revision": key[2], "player": key[3],
                             "provider": provider, "status": "CONFIRMED" if confirmed else "UNCONFIRMED",
                             "reason": "authority_accepted_external_reply" if confirmed else
                             "missing_run_id" if not key[0] else
                             "missing_or_ambiguous_authority" if len(native) != 1 else
                             "provider_contribution_not_accepted",
                             "native_evidence": native[0]["evidence"] if len(native) == 1 else None,
                             "provider_evidence": [r["evidence"] for r in selected]})
    return {"providers": totals, "decisions": outcomes,
            "local_routing": dict(Counter(r.get("reason", "unknown") for r in providers
                                           if r.get("provider") == "local_native" and r.get("stage") == "route")),
            "interpretation": "CONFIRMED joins a successful provider answer and queued submission to unique native authority acceptance; API success alone is UNCONFIRMED."}


def skill_inventory(histories):
    inventory = Counter()
    for document in histories:
        for event in document["history"].get("events", []):
            if event.get("kind") != "skill":
                continue
            data = event.get("data", {})
            skill = data.get("skill_name", data.get("skill", "unknown"))
            inventory[(str(skill), event.get("outcome", "unknown"))] += 1
    return [{"skill": skill, "outcome": outcome, "observed_events": count,
             "branch_status": "NOT_SPECIFIED"} for (skill, outcome), count in sorted(inventory.items())]


def build_report(inputs, branch_paths=(), require_controllers=(), pass_turns=3, run_ids=()):
    evidence = collect(inputs)
    # Reusing an output directory must never let yesterday's takeover pass today.
    excluded = Counter()
    if run_ids:
        wanted = set(run_ids)
        for kind in ("decisions", "providers", "histories"):
            kept = [row for row in evidence[kind] if row.get("trace_run_id") in wanted]
            excluded[kind] = len(evidence[kind]) - len(kept)
            evidence[kind] = kept
    decisions = decision_summary(evidence["decisions"], pass_turns)
    providers = provider_summary(evidence["decisions"], evidence["providers"])
    branches = []
    if branch_paths:
        from skill_branch_report import evaluate_branches
        for path in branch_paths:
            try:
                raw = Path(path).read_bytes()
                result = evaluate_branches(evidence["histories"], json.loads(raw))
                result["specification"] = {"path": str(Path(path).resolve()), "sha256": hashlib.sha256(raw).hexdigest()}
                branches.append(result)
            except (OSError, ValueError, TypeError) as error:
                branches.append({"status": "UNKNOWN", "specification": str(path), "error": str(error)[:200]})
    requirements = []
    for name in require_controllers:
        count = (providers["providers"].get(name, {}).get("authority_confirmed_decisions", 0)
                 if name in ("deepseek", "jev") else decisions["sources"].get(name, {}).get("accepted", 0))
        requirements.append({"controller": name, "accepted_decisions": count,
                             "status": "PASS" if count else "NOT_OBSERVED"})
    return {"schema": 1, "selected_run_ids": list(run_ids), "excluded_other_runs": excluded,
            "decisions": decisions, "takeover": providers,
            "controller_requirements": requirements, "skill_branches": branches,
            "skill_inventory": skill_inventory(evidence["histories"]),
            "skill_branch_status": "NOT_SPECIFIED" if not branch_paths else "SEE_BRANCHES",
            "history_count": len(evidence["histories"]), "input_errors": evidence["errors"],
            "inputs": evidence["inputs"]}


def write_report(report, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    decision = report["decisions"]
    lines = ["# Verification evidence", "", f"Decision status: {decision['status']} ({decision['decision_count']} observations)",
             "", "| Actual decision source | Accepted | Pass | Use card |", "|---|---:|---:|---:|"]
    for source, counts in decision["sources"].items():
        lines.append(f"| {source} | {counts.get('accepted', 0)} | {counts.get('accepted_pass', 0)} | {counts.get('accepted_use_card', 0)} |")
    lines += ["", "| Provider | Answered decisions | Queued | Authority confirmed | Accepted use card |", "|---|---:|---:|---:|---:|"]
    for name, counts in report["takeover"]["providers"].items():
        lines.append(f"| {name} | {counts.get('answered_decisions', 0)} | {counts.get('queued_decisions', 0)} | {counts.get('authority_confirmed_decisions', 0)} | {counts.get('accepted_use_card', 0)} |")
    lines += ["", f"Suspicious empty-turn streaks: {len(decision['suspicious_empty_turns'])}",
              f"Rejected/unfinished decisions: {len(decision['rejected_or_unfinished'])}",
              f"Recovered errors: {len(decision['recovered_errors'])}",
              f"Resolution histories: {report['history_count']}", f"Input errors: {len(report['input_errors'])}"]
    for requirement in report["controller_requirements"]:
        lines.append(f"Required controller {requirement['controller']}: {requirement['status']} ({requirement['accepted_decisions']} accepted)")
    lines += ["", "## Skill branches", ""]
    if not report["skill_branches"]:
        lines.append("NOT_SPECIFIED — supply a reviewed branch specification; invocation counts are not branch acceptance.")
    else:
        lines += ["| Skill | Branch | Status | Opportunities | Pass / Fail / Unknown |",
                  "|---|---|---|---:|---|"]
        for specification in report["skill_branches"]:
            if specification.get("error"):
                lines.append("UNKNOWN: " + str(specification["error"]).replace("\n", " "))
            for branch in specification.get("branches", []):
                counts = Counter(row["status"] for row in branch.get("occurrences", []))
                skill = str(branch.get("skill_id", "?")).replace("|", "\\|")
                label = str(branch.get("branch_text", branch.get("branch_id", "?"))).replace("|", "\\|").replace("\n", " ")
                lines.append(f"| {skill} | {label} | {branch['status']} | {branch.get('opportunity_count')} | "
                             f"{counts['PASS']} / {counts['FAIL']} / {counts['UNKNOWN']} |")
    lines += ["", "See verification.json for decision IDs, input hashes and exact evidence file/line references.", ""]
    (output / "verification.md").write_text("\n".join(lines), encoding="utf-8")


def report_exit(report):
    if report["input_errors"] or any(r["status"] != "PASS" for r in report["controller_requirements"]):
        return 2
    if report["decisions"]["status"] == "FAIL" or any(b.get("status") == "FAIL" for b in report["skill_branches"]):
        return 1
    if (report["decisions"]["status"] in ("UNKNOWN", "REVIEW")
            or any(b.get("status") != "PASS" for b in report["skill_branches"])):
        return 2
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, help="run log/file or artifact directory; repeat to combine related process evidence")
    parser.add_argument("--output", required=True)
    parser.add_argument("--branches", action="append", default=[], help="reviewed skill branch specification JSON; repeat for multiple skills")
    parser.add_argument("--require-controller", action="append", choices=sorted(SOURCES | {"deepseek", "jev"}), default=[])
    parser.add_argument("--pass-turns", type=int, default=3, help="review threshold for consecutive observed empty Play turns")
    parser.add_argument("--run-id", action="append", default=[], help="restrict evidence to this trace run ID; repeat for related runs")
    args = parser.parse_args()
    if args.pass_turns < 2:
        parser.error("--pass-turns must be at least 2")
    report = build_report(args.input, args.branches, args.require_controller, args.pass_turns, args.run_id)
    write_report(report, args.output)
    print(f"Verification evidence: {Path(args.output).resolve() / 'verification.md'}")
    return report_exit(report)


if __name__ == "__main__":
    sys.exit(main())
