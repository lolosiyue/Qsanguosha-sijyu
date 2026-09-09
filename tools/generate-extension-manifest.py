#!/usr/bin/env python3
"""Generate config.lua's declared extension list from the extensions tree."""
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def satellites(root: Path, name: str) -> str:
    fields: list[str] = []
    if name == "LuaOldEnemy" and (root / "lua/luaoldenemy_lib.lua").is_file():
        fields.append("libs=lua/luaoldenemy_lib.lua")
    ai = root / "lua/ai" / f"{name}-ai.lua"
    if ai.is_file():
        fields.append(f"ai=lua/ai/{ai.name}")
    return "".join(f";{field}" for field in fields)


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT
    extensions = root / "extensions"
    if extensions.is_symlink():
        raise SystemExit("extensions/ must not be a symlink")
    if not extensions.is_dir():
        raise SystemExit("extensions/ is missing")
    children = list(extensions.iterdir())
    paths = [path for path in children if path.is_file() and path.name.endswith(".lua")]
    if any(path.is_symlink() for path in children):
        raise SystemExit("extensions/ contains a symlink")
    lowered = [path.name.lower() for path in paths]
    if len(set(lowered)) != len(lowered):
        raise SystemExit("extensions/ has names that collide case-insensitively")
    names = sorted(
        (path.name for path in paths),
        key=str.lower,
    )
    print("\textension_names = {")
    for name in names:
        print(f'\t\t"extensions/{name}{satellites(root, name[:-4])}",')
    print("\t},")
    return 0


if __name__ == "__main__":
    sys.exit(main())
