#!/usr/bin/env python3
"""Prepare a declared Google Sheets runtime directory.

The existing web packager owns the configured Lua closure.  This wrapper adds
the runtime AI Lua files and image assets without copying undeclared Lua or
overwriting an existing destination.

Example:
    python google-sheets/prepare-runtime.py --asset-root . \
        --destination builds/google-sheets-qa/declared-runtime
"""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil


def _load_packager():
    source = Path(__file__).resolve().parents[1] / "tools" / "package-web-solo.py"
    spec = importlib.util.spec_from_file_location("qsan_packager", source)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load existing packager: {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _copy_ai(packager, asset_root: Path, destination: Path) -> int:
    count = 0
    for relative, source in packager._collect_ai(asset_root):
        target = destination / relative
        if target.exists():
            raise ValueError(f"destination collision: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        count += 1
    return count


def _copy_images(packager, asset_root: Path, destination: Path) -> tuple[int, int]:
    source_root = packager._require_tree(asset_root / "image", "image asset root")
    target_root = destination / "image"
    hardlinks = 0
    copies = 0
    for source in sorted(source_root.rglob("*"), key=lambda item: item.as_posix()):
        if packager._unsafe(source):
            raise ValueError(f"image asset is symlinked or reparse-pointed: {source}")
        relative = source.relative_to(source_root)
        target = target_root / relative
        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        if not source.is_file():
            raise ValueError(f"unsupported image asset: {source}")
        if target.exists():
            raise ValueError(f"destination collision: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(source, target)
            hardlinks += 1
        except OSError:
            shutil.copy2(source, target)
            copies += 1
    return hardlinks, copies


def prepare(asset_root: Path, destination: Path) -> tuple[int, int, int]:
    packager = _load_packager()
    asset_root = packager._require_tree(asset_root, "asset root")
    destination = destination.absolute()
    if destination.exists() and (not destination.is_dir() or any(destination.iterdir())):
        raise ValueError(f"destination must be new or empty: {destination}")
    if packager._unsafe(destination):
        raise ValueError(f"destination must not be symlinked or reparse-pointed: {destination}")
    if any(packager._unsafe(parent) for parent in destination.parents):
        raise ValueError(f"destination has a symlinked or reparse parent: {destination}")

    # Existing packager validates and copies the exact configured Lua closure.
    packager.prepare_content(asset_root, destination)
    ai_count = _copy_ai(packager, asset_root, destination)
    hardlinks, copies = _copy_images(packager, asset_root, destination)
    return ai_count, hardlinks, copies


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    try:
        ai_count, hardlinks, copies = prepare(args.asset_root, args.destination)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"Google Sheets runtime preparation failed: {error}\n")
    print(f"Prepared runtime: {args.destination.resolve()}")
    print(f"AI Lua files: {ai_count}; image hardlinks: {hardlinks}; image copies: {copies}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
