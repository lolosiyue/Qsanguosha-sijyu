#!/usr/bin/env python3
"""Generate the optional Android runtime-content.json descriptor.

The source of truth remains config.lua's ordered extension_names block.  This
tool preserves that order and declares support files from the exact APK input
list. It does not execute Lua or rewrite config.lua.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def entries(config: str) -> list[dict]:
    block = re.search(r"extension_names\s*=\s*\{(.*?)\n\s*\},", config, re.S)
    if block is None:
        raise ValueError("lua/config.lua has no extension_names declaration")
    body = block.group(1)
    # Match Lua's execution boundary: quoted strings count, line comments do
    # not.  This avoids treating an example declaration in a comment as data.
    uncommented: list[str] = []
    index = 0
    while index < len(body):
        if body.startswith("--", index):
            end = body.find("\n", index)
            index = len(body) if end < 0 else end
            continue
        if body[index] in "'\"":
            quote = body[index]
            end = index + 1
            while end < len(body):
                if body[end] == "\\":
                    end += 2
                elif body[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            uncommented.append(body[index:end])
            index = end
            continue
        index += 1
    raw = re.findall(r'"([^"\n]+)"', "".join(uncommented))
    result: list[dict] = []
    seen: set[str] = set()
    paths: set[str] = set()

    def validate_path(role: str, path: str) -> None:
        if not path or path.startswith(("/", "\\")) or "\\" in path:
            raise ValueError(f"invalid {role} path: {path}")
        if any(part in ("", ".", "..") for part in path.split("/")):
            raise ValueError(f"invalid {role} path: {path}")
        patterns = {
            "script": r"extensions/[A-Za-z0-9_.-]+\.lua",
            "libs": r"lua/[A-Za-z0-9_./-]+\.lua",
            "lang": r"lang/[A-Za-z0-9_./-]+\.lua",
            "ai": r"lua/ai/[A-Za-z0-9_./-]+\.lua|lua/lib/middleclass\.lua",
        }
        if not re.fullmatch(patterns[role], path):
            raise ValueError(f"invalid {role} path: {path}")
        if role == "libs" and (path.startswith("lua/ai/")
                                or path in {"lua/config.lua", "lua/sanguosha.lua",
                                             "lua/utilities.lua", "lua/sgs_ex.lua",
                                             "lua/lib/json.lua", "lua/lib/middleclass.lua"}):
            raise ValueError(f"invalid {role} path: {path}")
        if role == "ai" and path != "lua/lib/middleclass.lua" and not path.startswith("lua/ai/"):
            raise ValueError(f"invalid {role} path: {path}")
    for declaration in raw:
        fields = [field.strip() for field in declaration.split(";")]
        script = fields[0]
        match = re.fullmatch(r"extensions/([A-Za-z0-9_.-]+)\.lua", script)
        if match is None:
            raise ValueError(f"invalid extension script declaration: {script}")
        name = match.group(1)
        if name in seen:
            raise ValueError(f"duplicate extension name: {name}")
        seen.add(name)
        validate_path("script", script)
        if script in paths:
            raise ValueError(f"duplicate path: {script}")
        paths.add(script)
        values = {"libs": [], "lang": [], "ai": []}
        keys: set[str] = set()
        for field in fields[1:]:
            key, separator, value = field.partition("=")
            if not separator or key not in values or key in keys:
                raise ValueError(f"invalid extension declaration: {declaration}")
            keys.add(key)
            values[key] = [item.strip() for item in value.split(",")]
            if any(not item for item in values[key]):
                raise ValueError(f"invalid empty {key} path: {declaration}")
            for path in values[key]:
                validate_path(key, path)
                if path in paths:
                    raise ValueError(f"duplicate path: {path}")
                paths.add(path)
            values[key].sort()
        result.append({"name": name, "script": script, "dependencies": [], **values})
    return result


def include_bundled_support(declared: list[dict], bundled: set[str]) -> None:
    core = {"lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua",
            "lua/sgs_ex.lua", "lua/lib/json.lua"}
    owned = {path for entry in declared
             for path in [entry["script"], *entry["libs"], *entry["lang"]]}
    missing = (core | owned) - bundled
    if missing:
        raise ValueError(f"APK is missing declared Lua files: {sorted(missing)}")
    for path in sorted(bundled - core - owned):
        if path.startswith("lua/ai/") or path == "lua/lib/middleclass.lua":
            continue  # Server-owned policy is outside the shared rules hash.
        role = "libs" if re.fullmatch(r"lua/[A-Za-z0-9_./-]+\.lua", path) else "lang"
        if (not declared or role == "lang" and not re.fullmatch(r"lang/[A-Za-z0-9_./-]+\.lua", path)
                or any(part in ("", ".", "..") for part in path.split("/"))):
            raise ValueError(f"APK has undeclared or invalid Lua content: {path}")
        # Shared support files belong to the first existing package, as with
        # descriptor-free ZIP imports. They are hashed, never eagerly executed.
        declared[0][role].append(path)
    for entry in declared:
        entry["libs"].sort()
        entry["lang"].sort()


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    config = root / "lua/config.lua"
    output = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "assets/runtime-content-base.json"
    descriptor = {"schema_version": 2, "profile": "declared-v2",
                  "extensions": entries(config.read_text(encoding="utf-8"))}
    if len(sys.argv) > 3:
        bundled = set(Path(sys.argv[3]).read_text(encoding="utf-8").splitlines())
        include_bundled_support(descriptor["extensions"], bundled)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(descriptor, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
