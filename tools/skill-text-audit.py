#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Skill text audit: compare what a skill's description claims with what its
registered definition declares.

The description (the ":skill" translation) is the player-facing spec; the
definition is what the engine enforces. Only claims with a mechanical
counterpart are checked:

  leading tag   锁定技 / 限定技 / 觉醒技 / 主公技 / 转换技 / 使命技 / 隐匿技
                <-> Skill frequency, isLordSkill, isChangeSkill, isLimitedSkill,
                    isShiMingSkill, isHideSkill
  usage phrase  出牌阶段限 / 每回合限 / 每轮限 <-> a declared LimitScope
                (only skills that declare a scope; older skills count usage
                in their own callbacks, which a static pass cannot see)

Each finding is a candidate for human review, not a verdict: a skill whose
frequency changes at runtime, or whose text is shared with a variant, can
legitimately differ. Hidden and ownerless skills (equipment, helpers) are
checked only in the text -> code direction.

Input is the metadata dump of `qsanguosha_skill_scenario --describe-skills`;
by default this script runs it.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = Path(os.path.dirname(os.path.abspath(__file__))).parent

# Leading tag -> claim kind (see code_has). Traditional spellings appear in a
# few extensions.
TAGS = {
    "锁定技": "compulsory", "鎖定技": "compulsory",
    "限定技": "limited",
    "觉醒技": "wake", "覺醒技": "wake",
    "主公技": "lord", "君主技": "lord",
    "转换技": "change", "轉換技": "change",
    "使命技": "shiming",
    "隐匿技": "hide", "隱匿技": "hide",
}
# Any short "X技" word followed by a delimiter is a tag; unknown ones (君主技,
# 持恒技, 强化技, ...) are skipped so a known tag after them is still read.
TAG_RE = re.compile(r"^([^，,。、；;：:\s（）()你]{1,6}技)[，,、。：:；;\s]+")
# Status skills get Compulsory from their constructor, not from a design
# choice, and their text rarely tags it; only trigger skills are held to it.
COMPULSORY_TEXT_CLASSES = re.compile(r"TriggerSkill")
HTML_RE = re.compile(r"<[^>]+>")

# Usage phrase -> LimitScope enum key.
SCOPE_PHRASES = (
    (re.compile(r"(出牌阶段|出牌階段|阶段|階段)限[一二两兩三四五0-9X]+次"), "Limit_Phase"),
    (re.compile(r"每(个|個)?回合(各)?限[一二两兩三四五0-9X]+次"), "Limit_Turn"),
    (re.compile(r"每(一)?轮(各)?限[一二两兩三四五0-9X]+次|每(一)?輪(各)?限[一二两兩三四五0-9X]+次"), "Limit_Round"),
    (re.compile(r"每局(游戏|遊戲)?(各)?限[一二两兩三四五0-9X]+次"), "Limit_Game"),
)


def code_has(skill, kind):
    freq = skill["frequency"]
    return {
        "compulsory": freq == "Compulsory",
        "limited": bool(skill["limited"]),
        "wake": freq == "Wake",
        "lord": bool(skill["lord"]),
        "change": bool(skill["change"]),
        "shiming": bool(skill["shiming"]),
        "hide": bool(skill["hide"]),
    }[kind]


def plain_text(description):
    text = HTML_RE.sub("", description or "")
    return text.replace("&nbsp;", " ").replace("　", " ").strip()


def leading_tags(text):
    tags = []
    while True:
        m = TAG_RE.match(text)
        if not m:
            return tags
        if m.group(1) in TAGS:
            tags.append(TAGS[m.group(1)])
        text = text[m.end():]


def has_text(skill):
    desc = skill.get("description") or ""
    return bool(desc.strip()) and desc != ":" + skill["name"]


def audit(skills):
    findings = []
    stats = Counter()
    for skill in skills:
        name = skill["name"]
        if name.startswith("#") or not has_text(skill):
            stats["skipped_no_text"] += 1
            continue
        stats["checked"] += 1
        text = plain_text(skill["description"])
        tags = leading_tags(text)
        # Ownerless or hidden skills (equipment, attached helpers) often carry
        # flags their text never mentions; only check what the text claims.
        both_ways = skill["visible"] and bool(skill["owners"])

        def add(rule, detail):
            findings.append({
                "rule": rule, "skill": name, "translation": skill.get("translation", ""),
                "owners": skill["owners"], "class": skill["class"], "detail": detail,
                "text_tags": tags, "frequency": skill["frequency"],
                "limit_scope": skill["limit_scope"], "text": text[:120],
            })

        for kind in sorted(set(TAGS.values())):
            claimed, declared = kind in tags, code_has(skill, kind)
            if claimed and not declared:
                add("text_%s_code_missing" % kind, "text tags %s; definition does not" % kind)
            elif declared and not claimed and both_ways:
                # A wake skill is also limited in effect; a limited/wake skill
                # needs no separate 锁定技 tag.
                if kind == "limited" and "wake" in tags:
                    continue
                if kind == "compulsory" and not COMPULSORY_TEXT_CLASSES.search(skill["class"]):
                    continue
                add("code_%s_text_missing" % kind, "definition is %s; text has no tag" % kind)

        if "limited" in tags and not skill["limit_mark"]:
            add("limited_without_mark", "限定技 without a limit mark")

        scope = skill["limit_scope"]
        if scope not in ("Limit_None", "Limit_Custom", ""):
            phrase_scopes = {key for regex, key in SCOPE_PHRASES if regex.search(text)}
            if phrase_scopes and scope not in phrase_scopes:
                add("usage_scope_mismatch",
                    "declared %s; text says %s" % (scope, ", ".join(sorted(phrase_scopes))))
    stats["findings"] = len(findings)
    return findings, stats


def run_host(exe):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "skills.json")
        proc = subprocess.run([str(exe), "--describe-skills", "--report", out],
                              cwd=str(Path(exe).parent), capture_output=True, timeout=600)
        if proc.returncode != 0 or not os.path.exists(out):
            sys.stderr.write(proc.stderr.decode("utf-8", "replace")[-2000:])
            raise SystemExit("describe-skills failed with exit code %d" % proc.returncode)
        with open(out, encoding="utf-8") as f:
            return json.load(f)


def write_summary(path, findings, stats, limit):
    by_rule = Counter(f["rule"] for f in findings)
    lines = ["# Skill text audit", "",
             "Candidates for review, not verdicts. Generated by `tools/skill-text-audit.py`.", "",
             "- skills checked: %d" % stats["checked"],
             "- skipped (no text / hidden helper): %d" % stats["skipped_no_text"],
             "- findings: %d" % stats["findings"], "",
             "| rule | count |", "| --- | --- |"]
    lines += ["| %s | %d |" % (rule, count) for rule, count in by_rule.most_common()]
    for rule, _count in by_rule.most_common():
        lines += ["", "## %s" % rule, "", "| skill | owners | detail | text |", "| --- | --- | --- | --- |"]
        for f in [f for f in findings if f["rule"] == rule][:limit]:
            lines.append("| %s %s | %s | %s | %s |" % (
                f["skill"], f["translation"], ", ".join(f["owners"][:3]), f["detail"],
                f["text"][:60].replace("|", "\\|")))
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description="Compare skill descriptions with their definitions")
    p.add_argument("--skills", help="existing --describe-skills JSON (default: run the host)")
    p.add_argument("--exe", default=str(ROOT / "release" / "qsanguosha_skill_scenario.exe"),
                   help="qsanguosha_skill_scenario executable")
    p.add_argument("-o", "--output", default=str(ROOT / "artifacts" / "skill-text-audit"),
                   help="output directory for report.json + summary.md")
    p.add_argument("--rule", action="append", help="only report these rule ids")
    p.add_argument("--limit", type=int, default=30, help="rows per rule in summary.md")
    args = p.parse_args()

    if args.skills:
        with open(args.skills, encoding="utf-8") as f:
            dump = json.load(f)
    else:
        dump = run_host(args.exe)
    findings, stats = audit(dump["skills"])
    if args.rule:
        findings = [f for f in findings if f["rule"] in args.rule]
        stats["findings"] = len(findings)

    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    (out / "report.json").write_text(json.dumps(
        {"meta": dict(stats), "findings": findings}, ensure_ascii=False, indent=1), encoding="utf-8")
    write_summary(out / "summary.md", findings, stats, args.limit)

    print("checked %d skills, %d findings -> %s" % (stats["checked"], stats["findings"], out))
    for rule, count in Counter(f["rule"] for f in findings).most_common():
        print("  %-32s %d" % (rule, count))


if __name__ == "__main__":
    main()
