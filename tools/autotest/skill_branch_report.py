"""Grade reviewed skill-branch expectations against exported ResolutionHistory V2."""

from __future__ import annotations

import math
import re
from typing import Any


_SCOPES = {"event", "turn", "phase", "round"}
_COLLECTIONS = {"events", "facts"}
_SCOPE_KEYS = {"turn": "turn_id", "phase": "phase_id", "round": "round_id"}
_ID = re.compile(r"^[0-9]+$")
_MAX_RECORDS = 200_000
_MAX_OPPORTUNITIES = 20_000
_MAX_COMPARISONS = 5_000_000


class SpecificationError(ValueError):
    """A malformed or unsupported branch specification."""


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _path(obj: Any, path: str) -> tuple[bool, Any]:
    value = obj
    for part in path.split("."):
        if isinstance(value, dict) and part in value:
            value = value[part]
        else:
            return False, None
    return True, value


def _incomplete_flag(record: dict[str, Any]) -> bool:
    if record.get("attribution_complete") is False:
        return True
    exists, value = _path(record, "data.attribution_complete")
    return exists and type(value) is bool and value is False


def _selector(selector: Any, label: str, allow_empty: bool = False) -> tuple[str, dict[str, Any]]:
    if not isinstance(selector, dict) or set(selector) != {"collection", "where"}:
        raise SpecificationError(f"{label} requires collection and where")
    collection, where = selector["collection"], selector["where"]
    if collection not in _COLLECTIONS or not isinstance(where, dict) or (not where and not allow_empty):
        raise SpecificationError(f"invalid {label} selector")
    for path in where:
        if not isinstance(path, str) or not path or any(not p for p in path.split(".")):
            raise SpecificationError(f"invalid field path in {label}")
    return collection, where


def _strict_keys(value: dict[str, Any], allowed: set[str], label: str) -> None:
    unknown = set(value) - allowed
    if unknown:
        raise SpecificationError(f"unknown {label} key(s): {', '.join(sorted(map(str, unknown)))}")


def _resolved_expected(expected: Any, trigger: dict[str, Any]) -> tuple[bool, Any]:
    if isinstance(expected, str) and expected.startswith("$trigger."):
        return _path(trigger, expected[len("$trigger."):])
    return True, expected


def _matches(record: dict[str, Any], where: dict[str, Any], trigger: dict[str, Any]) -> bool | None:
    unknown = False
    for path, raw_expected in where.items():
        exists, actual = _path(record, path)
        expected_exists, expected = _resolved_expected(raw_expected, trigger)
        if not exists or not expected_exists:
            unknown = True
        elif path in ("data.skill_owner", "data.skill_name") and actual == "" and expected:
            unknown = True
        elif type(actual) is not type(expected) or actual != expected:
            return False
    return None if unknown else True


def _covered(history: dict[str, Any], collection: str, kind: Any) -> bool:
    coverage = history.get("coverage")
    if not isinstance(coverage, dict) or not isinstance(coverage.get(collection), list):
        return False
    return isinstance(kind, str) and kind in coverage[collection]


def _id(value: Any) -> str | None:
    if _is_int(value) and value >= 0:
        return str(value)
    if isinstance(value, str) and _ID.fullmatch(value):
        return value.lstrip("0") or "0"
    return None


def _prepare(document: Any) -> tuple[dict[str, Any], dict[str, Any], dict[str, list[str]],
                                      dict[str, dict[str, Any]], dict[str, set[str]]]:
    if not isinstance(document, dict):
        raise SpecificationError("history document must be an object")
    history = document.get("history", document)
    if (not isinstance(history, dict) or not _is_int(history.get("version"))
            or history.get("version") != 2):
        raise SpecificationError("expected ResolutionHistory version 2")
    events, facts = history.get("events"), history.get("facts")
    if not isinstance(events, list) or not isinstance(facts, list):
        raise SpecificationError("history events and facts must be arrays")
    if not all(isinstance(x, dict) for x in events + facts):
        raise SpecificationError("history records must be objects")
    if len(events) + len(facts) > _MAX_RECORDS:
        raise SpecificationError("history exceeds the report record limit")
    event_by_id: dict[str, dict[str, Any]] = {}
    fact_ids: set[str] = set()
    fact_sequences: set[str] = set()
    for event in events:
        eid = _id(event.get("id"))
        parent = _id(event.get("parent_id"))
        if eid is None or eid == "0" or eid in event_by_id:
            raise SpecificationError("duplicate or invalid event ID")
        if parent is None or (parent != "0" and (parent not in event_by_id or int(parent) >= int(eid))):
            raise SpecificationError("invalid event parent reference or ordering")
        if event.get("status") not in ("active", "finished"):
            raise SpecificationError("invalid event status")
        if (event.get("status") == "active") != (event.get("outcome") in (None, "")):
            raise SpecificationError("event status and outcome disagree")
        event_by_id[eid] = event
    for fact in facts:
        fid, sequence, event_id = _id(fact.get("id")), _id(fact.get("sequence")), _id(fact.get("event_id"))
        if fid is None or fid == "0" or fid in fact_ids or sequence is None or sequence == "0" or sequence in fact_sequences:
            raise SpecificationError("duplicate or invalid fact ID/sequence")
        if event_id not in event_by_id:
            raise SpecificationError("fact references a missing event")
        fact_ids.add(fid)
        fact_sequences.add(sequence)
    active = history.get("active")
    if not isinstance(active, list):
        raise SpecificationError("history active IDs must be an array")
    active_ids = [_id(item) for item in active]
    if any(item is None for item in active_ids) or len(set(active_ids)) != len(active_ids):
        raise SpecificationError("invalid or duplicate active ID")
    for event in events:
        eid = _id(event["id"])
        round_id = _id(history.get("round_scope_id"))
        round_scope_exception = eid == round_id and event.get("kind") == "round"
        if event["status"] == "active" and eid not in active_ids and not round_scope_exception:
            raise SpecificationError("active event is absent from active stack")
        if event["status"] == "finished" and eid in active_ids:
            raise SpecificationError("active stack and event statuses disagree")
    children: dict[str, list[str]] = {}
    for event in events:
        eid, parent = _id(event["id"]), _id(event["parent_id"])
        if parent != "0":
            children.setdefault(parent, []).append(eid)
    return document, history, children, event_by_id, {}


def validate_specification(spec: Any) -> None:
    if not isinstance(spec, dict) or not _is_int(spec.get("version")) or spec.get("version") != 1:
        raise SpecificationError("expected specification version 1")
    _strict_keys(spec, {"version", "general_id", "skill_id", "spec_source", "reviewed", "branches"}, "specification")
    for key in ("general_id", "skill_id", "spec_source"):
        if not isinstance(spec.get(key), str) or not spec[key].strip():
            raise SpecificationError(f"{key} must be a non-empty string")
    if type(spec.get("reviewed")) is not bool:
        raise SpecificationError("reviewed must be boolean")
    branches = spec.get("branches")
    if not isinstance(branches, list) or not branches:
        raise SpecificationError("branches must be a non-empty array")
    seen = set()
    for branch in branches:
        if not isinstance(branch, dict):
            raise SpecificationError("branch must be an object")
        _strict_keys(branch, {"id", "text", "when", "scope", "expect", "allowed_outcomes"}, "branch")
        if not isinstance(branch.get("id"), str) or not branch["id"] or branch["id"] in seen:
            raise SpecificationError("branch IDs must be unique non-empty strings")
        seen.add(branch["id"])
        if not isinstance(branch.get("text"), str) or not branch["text"].strip():
            raise SpecificationError(f"branch {branch['id']} needs explanatory text")
        when_collection, when_where = _selector(branch.get("when"), "when")
        if "kind" not in when_where or not isinstance(when_where["kind"], str):
            raise SpecificationError("when.where must include a literal kind")
        if when_collection == "events" and when_where.get("kind") == "skill":
            if (not isinstance(when_where.get("data.skill_name"), str) or not when_where["data.skill_name"]
                    or not isinstance(when_where.get("data.skill_owner"), str) or not when_where["data.skill_owner"]):
                raise SpecificationError("skill opportunities must identify skill_name and skill_owner")
        if any(isinstance(value, str) and value.startswith("$trigger.") for value in when_where.values()):
            raise SpecificationError("$trigger bindings are only supported in expect.where")
        if branch.get("scope") not in _SCOPES:
            raise SpecificationError(f"branch {branch['id']} has invalid scope")
        outcomes = branch.get("allowed_outcomes", ["completed"])
        if (not isinstance(outcomes, list) or not outcomes
                or any(not isinstance(value, str) or not value for value in outcomes)
                or len(set(outcomes)) != len(outcomes)):
            raise SpecificationError("allowed_outcomes must be a non-empty list of unique strings")
        expect = branch.get("expect")
        if not isinstance(expect, dict):
            raise SpecificationError(f"branch {branch['id']} needs expect")
        _strict_keys(expect, {"collection", "where", "count", "sum"}, "expect")
        _, expect_where = _selector({"collection": expect.get("collection"), "where": expect.get("where")}, "expect", allow_empty=True)
        if "kind" not in expect_where or not isinstance(expect_where["kind"], str):
            raise SpecificationError("expect.where must include a literal kind for coverage")
        count = expect.get("count")
        if not isinstance(count, dict) or not count or set(count) - {"min", "max"}:
            raise SpecificationError(f"branch {branch['id']} count needs min and/or max")
        for bound in count.values():
            if not _is_int(bound) or bound < 0:
                raise SpecificationError("count bounds must be non-negative integers")
        if "min" in count and "max" in count and count["min"] > count["max"]:
            raise SpecificationError("count min exceeds max")
        if "sum" in expect:
            total = expect["sum"]
            if not isinstance(total, dict) or set(total) - {"field", "min", "max"} or "field" not in total:
                raise SpecificationError("sum requires field and optional bounds")
            if not isinstance(total["field"], str) or not total["field"]:
                raise SpecificationError("sum field must be a dotted path")
            if not any(k in total for k in ("min", "max")):
                raise SpecificationError("sum requires min and/or max")
            for key in ("min", "max"):
                if key in total and (isinstance(total[key], bool) or not isinstance(total[key], (int, float))
                                     or not math.isfinite(total[key])):
                    raise SpecificationError("sum bounds must be finite numbers")
            if "min" in total and "max" in total and total["min"] > total["max"]:
                raise SpecificationError("sum min exceeds max")


def _descendants(anchor: str, children: dict[str, list[str]]) -> set[str]:
    found, pending = {anchor}, [anchor]
    while pending:
        for child in children.get(pending.pop(), []):
            if child not in found:
                found.add(child)
                pending.append(child)
    return found


def _cached_descendants(anchor: str, children: dict[str, list[str]], cache: dict[str, set[str]]) -> set[str]:
    if anchor not in cache:
        cache[anchor] = _descendants(anchor, children)
    return cache[anchor]


def _within(record: dict[str, Any], trigger: dict[str, Any], scope: str,
            descendants: set[str] | None, trigger_collection: str) -> bool | None:
    if scope == "event":
        anchor_value = trigger.get("event_id") if trigger_collection == "facts" else trigger.get("id")
        anchor = _id(anchor_value)
        if anchor is None:
            return None
        if "event_id" in record:
            target = _id(record.get("event_id"))
            return None if target is None else target in (descendants or set())
        rid = _id(record.get("id"))
        return None if rid is None else rid in (descendants or set())
    key = _SCOPE_KEYS[scope]
    anchor = _id(trigger.get(key))
    value = _id(record.get(key))
    if anchor in (None, "0") or value is None:
        return None
    return anchor == value


def _grade_occurrence(history: dict[str, Any], branch: dict[str, Any], trigger: dict[str, Any],
                      trigger_collection: str, children: dict[str, list[str]],
                      event_by_id: dict[str, dict[str, Any]], descendant_cache: dict[str, set[str]],
                      document: dict[str, Any], spec_source: str) -> dict[str, Any]:
    expect = branch["expect"]
    collection, where = _selector({"collection": expect["collection"], "where": expect["where"]}, "expect")
    records = history[collection]
    desc = None
    scope_anchor = trigger.get("event_id") if trigger_collection == "facts" and branch["scope"] == "event" else trigger.get("id")
    if branch["scope"] == "event":
        tid = _id(scope_anchor)
        desc = _cached_descendants(tid, children, descendant_cache) if tid is not None else set()
    else:
        tid = _id(trigger.get(_SCOPE_KEYS[branch["scope"]]))
        desc = _cached_descendants(tid, children, descendant_cache) if tid not in (None, "0") else set()
    scope_event = event_by_id.get(tid)
    expected_scope_kind = {"turn": "turn", "phase": "phase", "round": "round"}.get(branch["scope"])
    allowed_outcomes = branch.get("allowed_outcomes", ["completed"])
    scope_closed = bool(scope_event and scope_event.get("status") == "finished"
                        and scope_event.get("outcome") in allowed_outcomes
                        and (expected_scope_kind is None or scope_event.get("kind") == expected_scope_kind)
                        and all(event_by_id[eid].get("status") == "finished"
                                and event_by_id[eid].get("outcome") in allowed_outcomes for eid in desc))
    matched, uncertain, missing_sum = [], False, False
    for record in records:
        within = _within(record, trigger, branch["scope"], desc, trigger_collection)
        if within is False:
            continue
        kind_exists, record_kind = _path(record, "kind")
        if kind_exists and type(record_kind) is type(where.get("kind")) and record_kind == where["kind"] and _incomplete_flag(record):
            uncertain = True
            continue
        if within is None:
            uncertain = True
            continue
        match = _matches(record, where, trigger)
        if match is True:
            matched.append(record)
        elif match is None:
            uncertain = True
    observed_count = len(matched)
    count_bounds = expect["count"]
    count_below_min = "min" in count_bounds and observed_count < count_bounds["min"]
    count_above_max = "max" in count_bounds and observed_count > count_bounds["max"]
    observed_sum = None
    sum_bad = False
    if "sum" in expect:
        path = expect["sum"]["field"]
        values = []
        for record in matched:
            exists, value = _path(record, path)
            if not exists or isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                missing_sum = True
            else:
                values.append(value)
        observed_sum = sum(values) if len(values) == len(matched) else None
        bounds = expect["sum"]
    target_kind = where.get("kind")
    coverage_ok = _covered(history, collection, target_kind)
    attribution_ok = (document.get("attribution_complete", history.get("attribution_complete", True)) is not False
                      and not _incomplete_flag(trigger)
                      and all(not _incomplete_flag(record) for record in matched))
    fully_known = (history.get("complete") is True and coverage_ok and scope_closed and attribution_ok
                   and not uncertain and not missing_sum)
    if observed_sum is not None:
        bounds = expect["sum"]
        sum_bad = (fully_known and ("max" in bounds and observed_sum > bounds["max"] or
                                    "min" in bounds and observed_sum < bounds["min"]))
    attributed_matches = bool(matched) and all(not _incomplete_flag(record) for record in matched)
    if ((count_above_max and attributed_matches and attribution_ok)
            or (sum_bad and scope_closed and history.get("complete") is True and coverage_ok)
            or (fully_known and count_below_min)):
        status = "FAIL"
    elif fully_known:
        status = "PASS"
    else:
        status = "UNKNOWN"
    return {"status": status, "room_id": document.get("room_id"), "generation": document.get("generation"),
            "spec_source": spec_source,
            "trigger_event_id": (_id(trigger.get("event_id")) if trigger_collection == "facts"
                                 else _id(trigger.get("id"))),
            "trigger_fact_id": _id(trigger.get("id")) if "sequence" in trigger else None,
            "observed_count": observed_count, "observed_sum": observed_sum,
            "effect_ids": [_id(r.get("id")) for r in matched],
            "reason": "" if fully_known else "history, selector attribution, or kind coverage incomplete"}


def evaluate_branches(history_documents: list[dict[str, Any]], specification: dict[str, Any]) -> dict[str, Any]:
    """Evaluate each reviewed branch independently against each supplied room history."""
    try:
        if not isinstance(history_documents, list) or not history_documents:
            raise SpecificationError("at least one history document is required")
        validate_specification(specification)
        prepared = [_prepare(doc) for doc in history_documents]
    except (SpecificationError, TypeError) as exc:
        return {"status": "UNKNOWN", "error": str(exc), "branches": []}
    if specification["reviewed"] is not True:
        return {"status": "UNKNOWN", "error": "specification is not reviewed", "branches": []}
    output = []
    for index, (document, history, children, event_by_id, descendant_cache) in enumerate(prepared):
        for branch in specification["branches"]:
            trigger_collection, trigger_where = _selector(branch["when"], "when")
            triggers, uncertain_trigger = [], False
            for record in history[trigger_collection]:
                kind_exists, record_kind = _path(record, "kind")
                if (kind_exists and type(record_kind) is type(trigger_where["kind"])
                        and record_kind == trigger_where["kind"] and _incomplete_flag(record)):
                    uncertain_trigger = True
                    continue
                match = _matches(record, trigger_where, record)
                if match is True:
                    triggers.append(record)
                elif match is None:
                    uncertain_trigger = True
            if not triggers:
                trigger_kind = trigger_where.get("kind")
                covered = _covered(history, trigger_collection, trigger_kind)
                status = "NOT_COVERED" if (history.get("complete") is True and document.get("terminal") is True
                                            and covered and not uncertain_trigger) else "UNKNOWN"
                occurrences = []
            else:
                if (len(triggers) > _MAX_OPPORTUNITIES
                        or len(triggers) * len(history[branch["expect"]["collection"]]) > _MAX_COMPARISONS):
                    output.append({"document_index": index, "room_id": document.get("room_id"),
                                   "generation": document.get("generation"), "general_id": specification["general_id"],
                                   "skill_id": specification["skill_id"], "branch_id": branch["id"],
                                   "branch_text": branch["text"], "status": "UNKNOWN",
                                   "opportunity_count": len(triggers), "error": "evaluation budget exceeded", "occurrences": []})
                    continue
                occurrences = [_grade_occurrence(history, branch, trigger, trigger_collection, children,
                                                 event_by_id, descendant_cache, document, specification["spec_source"])
                               for trigger in triggers]
                if any(x["status"] == "FAIL" for x in occurrences):
                    status = "FAIL"
                elif any(x["status"] == "UNKNOWN" for x in occurrences) or uncertain_trigger:
                    status = "UNKNOWN"
                else:
                    status = "PASS"
            output.append({"document_index": index, "room_id": document.get("room_id"),
                           "generation": document.get("generation"), "general_id": specification["general_id"],
                           "skill_id": specification["skill_id"], "branch_id": branch["id"],
                           "branch_text": branch["text"], "status": status,
                           "opportunity_count": len(triggers), "occurrences": occurrences})
    statuses = [x["status"] for x in output]
    overall = "FAIL" if "FAIL" in statuses else "UNKNOWN" if "UNKNOWN" in statuses else \
              "NOT_COVERED" if "NOT_COVERED" in statuses else "PASS"
    return {"status": overall, "spec_source": specification["spec_source"], "reviewed": True,
            "branches": output}
