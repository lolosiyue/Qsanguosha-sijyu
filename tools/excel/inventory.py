#!/usr/bin/env python3
"""Create a read-only, reproducible static inventory for the Excel client.

This command never copies, fetches, builds, launches, or modifies the project.
It only writes the explicitly requested JSON output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

IGNORED_DIRS = {".git", "builds", "debug", "release", "staging"}


def is_scannable(path: Path) -> bool:
    return not any(part in IGNORED_DIRS for part in path.parts)


def run_git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def files(root: Path, relative: str) -> list[dict[str, object]]:
    base = root / relative
    if not base.exists():
        return []
    result = []
    for path in sorted(p for p in base.rglob("*") if p.is_file() and is_scannable(p)):
        result.append({"path": path.relative_to(root).as_posix(), "sha256": sha256(path), "bytes": path.stat().st_size})
    return result


def literals(root: Path, patterns: dict[str, str]) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    candidates = [p for base, pattern in ((root / "src", "*.cpp"), (root / "src", "*.h"), (root / "extensions", "*.lua"), (root / "lua", "*.lua")) for p in base.rglob(pattern) if is_scannable(p)]
    for name, expression in patterns.items():
        found: set[str] = set()
        regex = re.compile(expression)
        for path in candidates:
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            found.update(regex.findall(text))
        result[name] = sorted(found)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Freeze a static Excel client inventory (read-only scan).")
    parser.add_argument("--root", type=Path, required=True, help="QSanguosha source root")
    parser.add_argument("--output", type=Path, required=True, help="JSON output path; parent is created")
    args = parser.parse_args()
    root = args.root.resolve()
    head = run_git(root, "rev-parse", "HEAD")
    dirty = run_git(root, "status", "--porcelain", "--untracked-files=all").splitlines()
    authority = Path(r"H:\Program file\Game\sgs\Qsgs\working\extensions")
    authority_git: dict[str, object] = {"root": str(authority), "read_only": True}
    if (authority / ".git").exists() or (authority / ".git").is_file():
        try:
            authority_git.update({"head": run_git(authority, "rev-parse", "HEAD"), "dirty": run_git(authority, "status", "--porcelain", "--untracked-files=all").splitlines()})
        except (OSError, subprocess.CalledProcessError) as error:
            authority_git["error"] = str(error)
    else:
        authority_git["inspection"] = "git-root-not-found"
    catalog = literals(root, {
        "mode_ids": r'\b(?:mode|addMode)\s*\(\s*[\"\']([^\"\']+)',
        "general_ids": r'\b(?:General|addGeneral)\s*\(\s*[\"\']([^\"\']+)',
        "skill_names": r'\b(?:TriggerSkill|ViewAsSkill|SkillCard)\s*\(\s*[\"\']([^\"\']+)',
    })
    result = {
        "schema": "qsanguosha.excel.inventory.v1",
        "status": "static-inventory-only",
        "source_root": str(root),
        "git": {"head": head, "branch": run_git(root, "branch", "--show-current"), "dirty": dirty, "dirty_count": len(dirty)},
        "external_extensions_authority": authority_git,
        "modes": {"modern": {"runtime_tier": "modern", "max_players": None, "target": "Win10/11 x64 full Qt6"},
                  "legacy": {"runtime_tier": "legacy", "max_players": 10, "target": "XP/7/8.1/Win10 x86 Qt5.6"}},
        "catalog": catalog,
        "catalog_evidence": {"method": "source-literal-regex", "coverage": "candidate-only", "complete_registration_claim": False, "manual_review_required": True},
        "artifacts": {"source": files(root, "src"), "lang": files(root, "lang"), "extensions": files(root, "extensions"), "lua_ai": files(root, "lua/ai"), "images": files(root, "image"), "audio": files(root, "audio")},
        "limitations": ["Candidate literals are not a complete modes/generals/skills registration inventory; generated registries, aliases, Lua and runtime registration require manual review.", "No executable, Excel, VBE, gameplay, or runtime acceptance was performed."],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
