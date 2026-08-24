#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from tempfile import TemporaryDirectory
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final, TypeAlias

JsonValue: TypeAlias = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]


class Category(StrEnum):
    RAW_DELETE = "raw_delete"; DEFERRED_DELETE = "deferred_delete"
    QDELETEALL = "qdeleteall"; UPCAST_DELETE = "upcast_delete"
    MEMBER_POINTER = "member_pointer"; MACRO_DELETE = "macro_delete"
    DEFAULT_DELETER = "default_deleter"; PARENT_CHILD = "parent_child"
    FACTORY = "factory"; SINK = "sink"


@dataclass(frozen=True, slots=True)
class Finding:
    location: str; category: Category; detail: str


@dataclass(frozen=True, slots=True)
class Rule:
    value: str; category: Category; regex: re.Pattern[str] | None


@dataclass(frozen=True, slots=True)
class Allowlist:
    sites: tuple[Rule, ...]; patterns: tuple[Rule, ...]; symbols: tuple[str, ...]; ledger_sites: tuple[str, ...]


class CheckerError(Exception): pass

CATEGORIES: Final = frozenset(Category); SOURCE_SUFFIXES: Final = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"})
ENFORCED_CATEGORIES: Final = frozenset({
    Category.RAW_DELETE, Category.QDELETEALL, Category.UPCAST_DELETE,
    Category.MEMBER_POINTER, Category.MACRO_DELETE, Category.DEFAULT_DELETER,
})
TRUSTED_SOURCE_BOUNDARIES: Final = frozenset({"card-lifetime-manager.cpp"})
CARD_DECL = re.compile(r"\b(?:const\s+)?(?:[A-Za-z_]\w*Card|Card)\s*(?:\*+|&+)\s*([A-Za-z_]\w*)")
CARD_CONTAINER = re.compile(r"\b(?:QList|QVector|QSet|std::vector)\s*<\s*(?:const\s+)?(?:[A-Za-z_]\w*Card|Card)\s*\*[^>]*>\s*([A-Za-z_]\w*)")


def mapping(value: JsonValue, label: str) -> dict[str, JsonValue]:
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value): raise CheckerError(f"{label} must be an object")
    return value


def string(value: JsonValue, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise CheckerError(f"{label} must be a non-empty string")
    return value


def category(value: JsonValue, label: str) -> Category:
    raw = string(value, label)
    try:
        return Category(raw)
    except ValueError as exc:
        raise CheckerError(f"{label} has unknown category {raw!r}") from exc


def rule_list(value: JsonValue, label: str, patterns: bool) -> tuple[Rule, ...]:
    if not isinstance(value, list):
        raise CheckerError(f"{label} must be a list")
    seen: set[tuple[str, Category]] = set()
    result: list[Rule] = []
    for index, raw in enumerate(value):
        row = mapping(raw, f"{label}[{index}]")
        expected = {"site", "category", "reason"} if not patterns else {"pattern", "category", "reason"}
        if set(row) != expected:
            raise CheckerError(f"{label}[{index}] has malformed fields")
        value_key = "pattern" if patterns else "site"
        item = string(row[value_key], f"{label}[{index}].{value_key}")
        category_value = category(row["category"], f"{label}[{index}].category")
        string(row["reason"], f"{label}[{index}].reason")
        key = (item, category_value)
        if key in seen:
            raise CheckerError(f"{label}[{index}] duplicates {item!r}/{category_value.value}")
        seen.add(key)
        compiled: re.Pattern[str] | None = None
        if patterns:
            try:
                compiled = re.compile(item)
            except re.error as exc:
                raise CheckerError(f"{label}[{index}] has invalid regex: {exc}") from exc
        result.append(Rule(item, category_value, compiled))
    return tuple(result)


def symbol_list(value: JsonValue, label: str) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise CheckerError(f"{label} must be a list")
    result = tuple(string(item, f"{label}[{index}]") for index, item in enumerate(value))
    if len(result) != len(set(result)): raise CheckerError(f"{label} contains duplicate symbols")
    return result


def require_finite_patterns(patterns: tuple[Rule, ...]) -> None:
    if patterns:
        raise CheckerError("allowlist.legacy_patterns must be empty; use finite exact sites")


def load_allowlist(path: Path) -> Allowlist:
    try:
        raw: JsonValue = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CheckerError(f"invalid allowlist: {exc}") from exc
    data = mapping(raw, "allowlist")
    expected = {"version", "categories", "legacy_sites", "legacy_patterns", "known_factories", "known_sources", "known_sinks", "known_opaque_variants", "ledger_sites"}
    if set(data) != expected:
        raise CheckerError("allowlist has unknown or missing fields")
    if data["version"] != 2:
        raise CheckerError("allowlist version must be 2")
    raw_categories = data["categories"]
    if not isinstance(raw_categories, list):
        raise CheckerError("allowlist.categories must be a list")
    parsed_categories = tuple(category(item, f"allowlist.categories[{index}]") for index, item in enumerate(raw_categories))
    if len(parsed_categories) != len(set(parsed_categories)) or set(parsed_categories) != CATEGORIES:
        raise CheckerError("allowlist.categories must contain every category exactly once")
    sites = rule_list(data["legacy_sites"], "allowlist.legacy_sites", False)
    patterns = rule_list(data["legacy_patterns"], "allowlist.legacy_patterns", True)
    require_finite_patterns(patterns)
    symbols = symbol_list(data["known_factories"], "allowlist.known_factories") + symbol_list(data["known_sources"], "allowlist.known_sources") + symbol_list(data["known_sinks"], "allowlist.known_sinks")
    symbol_list(data["known_opaque_variants"], "allowlist.known_opaque_variants")
    ledger_sites = symbol_list(data["ledger_sites"], "allowlist.ledger_sites")
    return Allowlist(sites, patterns, symbols, ledger_sites)


def load_ledger(path: Path) -> tuple[str, tuple[str, ...]]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CheckerError(f"cannot read ledger: {exc}") from exc
    rows = [line for line in text.splitlines() if line.lstrip().startswith("|")]
    if len(rows) < 2:
        raise CheckerError("ownership ledger has no classified rows")
    header = [cell.strip() for cell in rows[0].strip().strip("|").split("|")]
    expected = ["Site", "Representation", "Owner", "Lease/release", "Affinity", "Implementation"]
    if header != expected:
        raise CheckerError("ownership ledger header is malformed")
    sites: set[str] = set()
    for index, row in enumerate(rows[2:], 3):
        cells = [cell.strip() for cell in row.strip().strip("|").split("|")]
        if len(cells) != len(expected) or any(not cell for cell in cells):
            raise CheckerError(f"ownership ledger row {index} is malformed")
        if cells[0] in sites:
            raise CheckerError(f"ownership ledger row {index} duplicates site {cells[0]!r}")
        sites.add(cells[0])
    if len(rows) == 2:
        raise CheckerError("ownership ledger has no classified rows")
    return text, tuple(sorted(sites))


def add(finding: list[Finding], path: str, line: int, kind: Category, detail: str) -> None:
    item = Finding(f"{path}:{line}", kind, detail)
    if item not in finding: finding.append(item)


def mask_non_code(lines: list[str]) -> list[str]:
    masked: list[str] = []
    block_comment = False
    quote: str | None = None
    escaped = False
    for line in lines:
        output: list[str] = []
        index = 0
        while index < len(line):
            if block_comment:
                end = line.find("*/", index)
                if end < 0:
                    output.extend(" " for _ in line[index:])
                    index = len(line)
                else:
                    output.extend(" " for _ in line[index:end + 2])
                    index = end + 2
                    block_comment = False
            elif quote is not None:
                character = line[index]
                output.append(" ")
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == quote:
                    quote = None
                index += 1
            elif line.startswith("//", index):
                output.extend(" " for _ in line[index:])
                index = len(line)
            elif line.startswith("/*", index):
                output.extend("  ")
                index += 2
                block_comment = True
            elif line[index] in {"'", '"'}:
                quote = line[index]
                escaped = False
                output.append(" ")
                index += 1
            else:
                output.append(line[index])
                index += 1
        masked.append("".join(output))
    return masked


def scan_text(path: str, text: str) -> list[Finding]:
    lines = text.splitlines()
    code_lines = mask_non_code(lines)
    names = {match.group(1) for line in lines for match in CARD_DECL.finditer(line)}
    code_names = {match.group(1) for line in code_lines for match in CARD_DECL.finditer(line)}
    containers = {match.group(1) for line in lines for match in CARD_CONTAINER.finditer(line)}
    for line in lines:
        match = re.search(r"\b([A-Za-z_]\w*)\s*=\s*new\s+[A-Za-z_]\w*Card\b", line)
        if match:
            names.add(match.group(1))
    for line in code_lines:
        match = re.search(r"\b([A-Za-z_]\w*)\s*=\s*new\s+[A-Za-z_]\w*Card\b", line)
        if match:
            code_names.add(match.group(1))
    upcasts: set[str] = set()
    macros: set[str] = set()
    card_scopes: dict[int, set[str]] = {}
    scope_depth = 0
    findings: list[Finding] = []
    def has_name(line: str) -> bool:
        return any(re.search(rf"\b{re.escape(name)}\b", line) for name in names)

    for number, (line, code_line) in enumerate(zip(lines, code_lines), 1):
        scope_names = card_scopes.setdefault(scope_depth, set())
        scope_names.update(match.group(1) for match in CARD_DECL.finditer(code_line))
        stripped = line.strip()
        macro = re.match(r"#\s*define\s+([A-Za-z_]\w*)[^\n]*(?:deleteLater|\bdelete\b|operator\s+delete)", stripped)
        if macro:
            macros.add(macro.group(1))
            add(findings, path, number, Category.MACRO_DELETE, "macro expands to deletion")
        qobject_casted = bool(re.search(r"(?:\(\s*QObject\s*\*\s*\)|static_cast\s*<\s*QObject\s*\*>)", code_line))
        card_casted = bool(re.search(r"(?:\(\s*Card\s*\*\s*\)|static_cast\s*<\s*Card\s*\*>)", code_line))
        upcast = re.search(r"\bQObject\s*\*\s*([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)", code_line)
        if upcast and (upcast.group(2) in code_names or "Card" in code_line):
            upcasts.add(upcast.group(1))
        delete_match = re.search(r"\bdelete\s+([A-Za-z_]\w*)", code_line)
        if delete_match and (delete_match.group(1) in code_names or qobject_casted or card_casted or delete_match.group(1) in upcasts):
            kind = Category.UPCAST_DELETE if qobject_casted or delete_match.group(1) in upcasts else Category.RAW_DELETE
            add(findings, path, number, kind, "direct Card delete")
        delete_later = re.search(
            r"\b([A-Za-z_]\w*)\s*(?:->|\.)\s*(?:(QObject|Card)\s*::\s*)?deleteLater\s*\(",
            code_line,
        )
        if delete_later and (
            qobject_casted
            or delete_later.group(1) in upcasts
            or delete_later.group(2) == "QObject"
        ):
            add(findings, path, number, Category.UPCAST_DELETE,
                "Card bypasses policy through QObject::deleteLater")
        member_pointer = re.search(r"=\s*&\s*(?:QObject|Card)\s*::\s*(?:deleteLater|operator\s+delete)", code_line)
        if member_pointer and ("Card" in code_line or bool(scope_names)):
            add(findings, path, number, Category.MEMBER_POINTER, "qualified Card deletion member")
        smart_card = re.search(r"(?:QSharedPointer|std::unique_ptr|std::shared_ptr)\s*<\s*(?:const\s+)?Card\s*>", line)
        if "std::default_delete" in line or (smart_card and ("m_ownedCard" in line or ("(" in line and ", &" not in line))):
            add(findings, path, number, Category.DEFAULT_DELETER, "default smart-pointer Card deleter")
        if "qDeleteAll" in line and (any(container in line for container in containers) or "Card" in line):
            add(findings, path, number, Category.QDELETEALL, "bulk Card deletion")
        if re.search(r"\b(?:setParent|setParentFile)\s*\(", line) and (has_name(line) or "Card" in line):
            add(findings, path, number, Category.PARENT_CHILD, "QObject parent owns Card")
        if re.search(r"\bnew\s+[A-Za-z_]\w*Card\s*\([^)]*\bthis\b", line):
            add(findings, path, number, Category.PARENT_CHILD, "Card constructed with QObject parent")
        if re.search(r"\breturn\s+new\s+[A-Za-z_]\w*Card\b", line):
            add(findings, path, number, Category.FACTORY, "Card factory return")
        if "QVariant<const Card*>" in line or re.search(r"QVariant::fromValue\s*\(\s*(?:const\s+)?(?:static_cast\s*<\s*)?\(?\s*Card\s*\*", line):
            add(findings, path, number, Category.SINK, "Card-bearing QVariant sink")
        invocation = re.match(r"\s*([A-Z_]\w*)\s*\((.*)\)", line)
        if invocation and invocation.group(1) in macros and has_name(invocation.group(2)):
            add(findings, path, number, Category.MACRO_DELETE, "Card passed to deletion macro")
        if invocation and "DELETE" in invocation.group(1) and has_name(invocation.group(2)):
            add(findings, path, number, Category.MACRO_DELETE, "unknown Card deletion macro")
        scope_depth = max(0, scope_depth + code_line.count("{") - code_line.count("}"))
    return findings


def scan_sources(root: Path) -> list[Finding]:
    findings: list[Finding] = []
    source_root = root / "src"
    if not source_root.is_dir():
        raise CheckerError("source root is missing")
    for path in sorted(source_root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or path.name in TRUSTED_SOURCE_BOUNDARIES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            raise CheckerError(f"cannot read source {path}: {exc}") from exc
        findings.extend(scan_text(path.relative_to(root).as_posix(), text))
    return sorted(set(findings), key=lambda item: (item.location, item.category.value))


def allowed(finding: Finding, rules: Allowlist) -> bool:
    return any(rule.value == finding.location and rule.category == finding.category for rule in rules.sites)


def self_test() -> None:
    fixtures = {
        Category.RAW_DELETE: "Card *card; delete card;",
        Category.QDELETEALL: "QList<Card *> cards; qDeleteAll(cards);", Category.UPCAST_DELETE: "Card *card; QObject *base = card; delete base;",
        Category.MEMBER_POINTER: "Card *card; auto fn = &QObject::deleteLater;", Category.MACRO_DELETE: "#define DROP(x) delete x\nCard *card; DROP(card);",
        Category.DEFAULT_DELETER: "std::unique_ptr<Card> card(new Card);", Category.PARENT_CHILD: "Card *card = new DummyCard(this);",
        Category.FACTORY: "Card *makeCard() { return new DummyCard; }", Category.SINK: "QVariant<const Card*> payload;",
    }
    missing = [kind.value for kind, text in fixtures.items() if not any(item.category == kind for item in scan_text(f"self-test/{kind.value}.cpp", text))]
    if missing: raise CheckerError("self-test missed categories: " + ", ".join(missing))
    regressions = {
        "base_deleteLater_split": (Category.UPCAST_DELETE, "Card *card;\nQObject *base = card;\nbase->deleteLater();"),
        "qualified_base_deleteLater": (Category.UPCAST_DELETE, "Card *card;\nQObject *base = card;\nbase->QObject::deleteLater();"),
        "qualified_card_deleteLater": (Category.UPCAST_DELETE, "Card *card;\ncard->QObject::deleteLater();"),
        "member_pointer_split": (Category.MEMBER_POINTER, "Card *card;\nauto fn = &QObject::deleteLater;"),
    }
    missing_regressions = [
        fixture_id
        for fixture_id, (kind, fixture) in regressions.items()
        if not any(item.category == kind for item in scan_text(f"self-test/{fixture_id}.cpp", fixture))
    ]
    if missing_regressions:
        raise CheckerError("self-test missed regressions: " + ", ".join(missing_regressions))
    safe_ingress = scan_text("self-test/managed-delete.cpp", "Card *card;\ncard->deleteLater();")
    if any(item.category in {Category.DEFERRED_DELETE, Category.UPCAST_DELETE} for item in safe_ingress):
        raise CheckerError("self-test rejected manager-aware Card::deleteLater")
    comment_findings = scan_text("self-test/comment.cpp", "// Card *card; delete card;")
    if any(item.category in {Category.RAW_DELETE, Category.UPCAST_DELETE} for item in comment_findings):
        raise CheckerError("self-test classified a commented Card delete")
    finite_rules = Allowlist(
        (Rule("self-test/approved.cpp:1", Category.RAW_DELETE, None),), (), (), ()
    )
    negative_cases = {
        "unknown_raw": "Card *card; delete card;",
        "unknown_upcast": "Card *card;\nQObject *base = card;\nbase->deleteLater();",
        "unknown_qualified": "Card *card;\ncard->QObject::deleteLater();",
    }
    for fixture_id, fixture in negative_cases.items():
        findings = scan_text(f"self-test/{fixture_id}.cpp", fixture)
        if not findings or any(allowed(item, finite_rules) for item in findings):
            raise CheckerError(f"self-test allowed negative case: {fixture_id}")
    rejected = 0
    try:
        category("unknown", "self-test.category")
    except CheckerError:
        rejected += 1
    duplicate = [{"site": "x:1", "category": "raw_delete", "reason": "x"}] * 2
    try:
        rule_list(duplicate, "self-test.duplicate", False)
    except CheckerError:
        rejected += 1
    with TemporaryDirectory() as temporary:
        malformed = Path(temporary) / "ledger.md"
        malformed.write_text("| Site | broken |\n| --- | --- |\n| unknown | row |\n", encoding="utf-8")
        try:
            load_ledger(malformed)
        except CheckerError:
            rejected += 1
    broad_pattern = [{"pattern": r"^src/[^:]+:\d+$", "category": "raw_delete", "reason": "broad"}]
    try:
        require_finite_patterns(rule_list(broad_pattern, "self-test.pattern", True))
    except CheckerError:
        rejected += 1
    if rejected != 4: raise CheckerError(f"self-test rejected={rejected}, expected=4")
    print(f"card-lifetime: self-test fixtures={len(fixtures)} negatives={len(negative_cases)} rejected={rejected}")
    print("card-lifetime: self-test regressions=" + ",".join(regressions))


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail-closed Card lifetime source checker")
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--ledger", type=Path, required=True)
    parser.add_argument("--allowlist", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        ledger, ledger_sites = load_ledger((root / args.ledger).resolve())
        rules = load_allowlist((root / args.allowlist).resolve())
        if set(ledger_sites) != set(rules.ledger_sites):
            raise CheckerError("allowlist ledger_sites do not exactly match ownership ledger")
        missing = [symbol for symbol in rules.symbols if symbol not in ledger]
        if missing: raise CheckerError("allowlist references unknown ledger symbols: " + ", ".join(missing))
        if args.self_test:
            self_test()
        findings = scan_sources(root)
    except CheckerError as exc:
        print(f"card-lifetime: ERROR: {exc}")
        return 2
    enforced = [item for item in findings if item.category in ENFORCED_CATEGORIES]
    unclassified = [item for item in enforced if not allowed(item, rules)]
    if unclassified:
        details = ", ".join(f"{item.location}[{item.category.value}]" for item in unclassified[:30])
        print(f"card-lifetime: unclassified={len(unclassified)}: {details}")
        return 1
    print(f"card-lifetime: scanned findings={len(findings)}; enforced={len(enforced)}; unclassified=0")
    print("card-lifetime: static scan complete; runtime counters/tests remain required")
    return 0


if __name__ == "__main__":
    sys.exit(main())
