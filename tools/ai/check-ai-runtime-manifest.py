#!/usr/bin/env python3
"""Fail-closed checker for docs/ai-runtime-manifest.json.

The isolated AI runtime's Lua lives in an untracked tree fetched from another
repository, so nothing in this repository's history says which files must be
present.  The manifest says it; this checker makes the manifest binding.

Which scripts load is the Lua tree's decision, not the host's: isolated-bootstrap.lua
declares its own dispatcher core, and a package's handlers load because that package is
enabled and lua/ai/isolated/<package>-ai.lua exists.  These checks hold that line.

1. every required file exists on disk
2. the manifest's core list equals ai_isolated_core, and no host-side list came back
3. every lua/ai/isolated/*.lua on disk appears in the manifest
4. no package handler is marked required
5. both fetchers refuse a fetch that cannot satisfy the core
6. the CMake asset manifest lists the isolated core as required
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

FETCH_HINT = "run tools/ci/fetch-extensions.sh (or .ps1) to populate lua/ai/"


class CheckError(Exception):
    pass


def read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise CheckError(f"{relative} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def lua_core_scripts(root: Path) -> list[str]:
    """ai_isolated_core, as lua/ai/isolated-bootstrap.lua declares it.

    The list lives in the Lua tree on purpose - which scripts the runtime needs is
    the AI's business, not the host's - so this is the authority the manifest and
    the fetch gates are checked against.
    """
    source = read(root, "lua/ai/isolated-bootstrap.lua")
    block = re.search(r"^ai_isolated_core\s*=\s*\{(.*?)\}", source,
                      re.DOTALL | re.MULTILINE)
    if not block:
        raise CheckError(
            "lua/ai/isolated-bootstrap.lua declares no ai_isolated_core list; the "
            "isolated runtime cannot say what it needs, so nothing can verify it")
    names = re.findall(r'"([^"]+)"', block.group(1))
    if not names:
        raise CheckError("lua/ai/isolated-bootstrap.lua: ai_isolated_core is empty")
    return names


def check_required_present(root: Path, files: list[dict]) -> list[str]:
    missing = [entry["path"] for entry in files
               if entry.get("required") and not (root / entry["path"]).is_file()]
    if not missing:
        return []
    return [f"required file absent: {path} ({FETCH_HINT})" for path in missing]


def check_core_list(root: Path, manifest: dict) -> list[str]:
    declared = manifest["isolated_core_scripts"]
    actual = lua_core_scripts(root)
    problems = []
    if declared != actual:
        problems.append(f"isolated_core_scripts is {declared} but "
                        f"lua/ai/isolated-bootstrap.lua declares {actual}")
    # A hardcoded host-side script list is the thing this design removed: which AI
    # files load is decided by the Lua declaration and the enabled packages.
    if re.search(r"defaultScripts", read(root, "src/server/ai-runtime.cpp")):
        problems.append("src/server/ai-runtime.cpp names AI scripts again "
                        "(defaultScripts); the list belongs to "
                        "lua/ai/isolated-bootstrap.lua")
    return problems


def check_package_handlers(root: Path, files: list[dict]) -> list[str]:
    """A <package>-ai.lua is loaded because its package is enabled, so it can never
    be required: gating one would make an optional handler a deployment blocker."""
    return [f"{entry['path']} is a package handler yet marked required; it loads "
            f"only when its package is enabled" for entry in files
            if entry.get("kind") == "package_handler" and entry.get("required")]


def check_isolated_inventory(root: Path, files: list[dict]) -> list[str]:
    directory = root / "lua/ai/isolated"
    if not directory.is_dir():
        return [f"lua/ai/isolated/ does not exist ({FETCH_HINT})"]
    known = {entry["path"] for entry in files}
    unrecorded = sorted(
        f"lua/ai/isolated/{path.name}" for path in directory.glob("*.lua")
        if f"lua/ai/isolated/{path.name}" not in known)
    return [f"on disk but absent from the manifest: {path}; record what loads "
            f"it and what breaks without it" for path in unrecorded]


def check_fetchers_gate_core(root: Path, core: list[str]) -> list[str]:
    """A fetch that cannot satisfy the core must fail at fetch time.

    loadConfiguredScripts() aborts initialize() on the first core script it cannot
    load, so an upstream rename of any one takes every Room's AI down.  Package
    handlers are deliberately absent from this check: a package with no isolated
    handler is the normal case, not a broken fetch.
    """
    problems = []
    for fetcher in ("tools/ci/fetch-extensions.sh", "tools/ci/fetch-extensions.ps1"):
        body = read(root, fetcher)
        for script in core:
            if f"isolated/{script}" not in body and f"isolated\\{script}" not in body:
                problems.append(
                    f"{fetcher} does not verify isolated/{script}, which "
                    f"lua/ai/isolated-bootstrap.lua declares as core")
    return problems


def check_asset_required(root: Path, files: list[dict]) -> list[str]:
    # The block ends at a ")" alone on its line; the comments inside it contain
    # parentheses of their own, so a non-greedy ".*?\)" would stop short.
    block = re.search(r"set\(QSAN_ASSET_REQUIRED\b(.*?)^\s*\)\s*$",
                      read(root, "CMakeLists.txt"), re.DOTALL | re.MULTILINE)
    if not block:
        raise CheckError("CMakeLists.txt: cannot find set(QSAN_ASSET_REQUIRED ...)")
    listed = [word for line in block.group(1).splitlines()
              if not line.lstrip().startswith("#") for word in line.split()]
    return [f"CMakeLists.txt QSAN_ASSET_REQUIRED omits {entry['path']}, which "
            f"every AI decision needs" for entry in files
            if entry.get("required") and entry["path"].startswith("lua/ai")
            and entry["path"] not in listed
            # A listed parent directory covers the file; keep it POSIX so the
            # comparison does not depend on the host's path separator.
            and Path(entry["path"]).parent.as_posix() not in listed]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail-closed isolated AI runtime source checker")
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--manifest", type=Path,
                        default=Path("docs/ai-runtime-manifest.json"))
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    try:
        manifest = json.loads(read(root, str(arguments.manifest).replace("\\", "/")))
        files = manifest["files"]
        core = manifest["isolated_core_scripts"]
        problems = (check_required_present(root, files)
                    + check_core_list(root, manifest)
                    + check_isolated_inventory(root, files)
                    + check_package_handlers(root, files)
                    + check_fetchers_gate_core(root, core)
                    + check_asset_required(root, files))
    except (CheckError, KeyError, json.JSONDecodeError) as error:
        print(f"ai-runtime-manifest: ERROR: {error}")
        return 2

    if problems:
        for problem in problems:
            print(f"ai-runtime-manifest: {problem}")
        print(f"ai-runtime-manifest: {len(problems)} problem(s)")
        return 1

    required = sum(1 for entry in files if entry.get("required"))
    packages = sum(1 for entry in files if entry.get("kind") == "package_handler")
    print(f"ai-runtime-manifest: files={len(files)} required={required} "
          f"core={len(core)} package_handlers={packages}; the Lua declaration, the "
          f"fetch gates and the asset manifest agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
