#!/usr/bin/env python3
"""Generate config.lua's declared extension list from the extensions tree."""
from __future__ import annotations

from pathlib import Path
import sys
import os

ROOT = Path(__file__).resolve().parents[1]

def presentation_files(root: Path) -> list[str]:
    """Return every language Lua file as a sorted, root-relative path.

    The walk is deliberately filesystem based so a staged release tree works
    without Git metadata.  Symlinks are never part of a release closure,
    including dangling links which Path.is_file() would otherwise hide.
    """
    if any(part.is_symlink() for part in (root, *root.parents)):
        raise ValueError("asset root must not have symlinked parents")
    lang = root / "lang"
    if lang.is_symlink():
        raise ValueError("lang/ must not be a symlink")
    if not lang.exists():
        return []
    if not lang.is_dir():
        raise ValueError("lang/ is not a directory")
    result: list[str] = []
    for directory, dirnames, filenames in os.walk(lang, followlinks=False):
        current = Path(directory)
        for name in list(dirnames) + list(filenames):
            path = current / name
            if path.is_symlink():
                raise ValueError("lang/ contains a symlink: " + path.relative_to(root).as_posix())
        for name in filenames:
            path = current / name
            if path.suffix == ".lua":
                result.append(path.relative_to(root).as_posix())
    return sorted(result)


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
    language = presentation_files(root)
    print("\textension_names = {")
    for index, name in enumerate(names):
        extra = satellites(root, name[:-4])
        if index == 0 and language:
            extra += ";lang=" + ",".join(language)
        print(f'\t\t"extensions/{name}{extra}",')
    print("\t},")
    return 0


if __name__ == "__main__":
    sys.exit(main())
