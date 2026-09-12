#!/usr/bin/env python3
"""Build the Android external media package without changing its source tree.

The package is deliberately ZIP_STORED: most game media is already compressed,
and storing it keeps the operation bounded by a single streaming read/write.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import zipfile


CHUNK_SIZE = 1024 * 1024
PROGRESS_BYTES = 256 * 1024 * 1024

MEDIA_DIRS = ("image", "audio", "font")
EXCLUDED_PARTS = {".git", "logs", "log", "backup", "backups", "tmp", "temp"}
BACKUP_SUFFIXES = (".bak", ".old", ".orig", "~")


def is_excluded(relative_path: Path) -> bool:
    parts = {part.casefold() for part in relative_path.parts}
    if parts & EXCLUDED_PARTS:
        return True
    name = relative_path.name.casefold()
    return (
        any(name.endswith(suffix) for suffix in BACKUP_SUFFIXES)
        or name.startswith("backup.")
        or name.startswith(".~")
    )


def collect_files(source_root: Path, output_path: Path) -> list[tuple[str, Path]]:
    output_resolved = output_path.resolve(strict=False)
    candidates: list[tuple[str, Path]] = []

    for media_dir in MEDIA_DIRS:
        directory = source_root / media_dir
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.is_symlink():
                continue
            relative = path.relative_to(source_root)
            if is_excluded(relative) or path.resolve(strict=False) == output_resolved:
                continue
            candidates.append((relative.as_posix(), path))

    candidates.sort(key=lambda item: (item[0].casefold(), item[0]))
    seen: dict[str, str] = {}
    for archive_path, _ in candidates:
        collision_key = archive_path.casefold()
        previous = seen.get(collision_key)
        if previous is not None:
            raise ValueError(
                f"archive path collision (case-insensitive): {previous!r} and {archive_path!r}"
            )
        seen[collision_key] = archive_path
    return candidates


def file_record(path: Path, archive_path: str, progress: dict[str, int]) -> dict[str, object]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while True:
            block = source.read(CHUNK_SIZE)
            if not block:
                break
            digest.update(block)
            size += len(block)
            progress["bytes_since_report"] += len(block)
            if progress["bytes_since_report"] >= PROGRESS_BYTES:
                print(f"progress: {progress['bytes'] + size} bytes", file=sys.stderr)
                progress["bytes_since_report"] = 0
    progress["bytes"] += size
    return {"path": archive_path, "size": size, "sha256": digest.hexdigest()}


def make_manifest(files: list[tuple[str, Path]]) -> dict[str, object]:
    progress = {"bytes": 0, "bytes_since_report": 0}
    records = [file_record(path, archive_path, progress) for archive_path, path in files]
    return {
        "format": 1,
        "role": "media",
        "files": records,
        "expandedBytes": progress["bytes"],
    }


def write_package(output_path: Path, files: list[tuple[str, Path]], manifest: dict[str, object]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        with zipfile.ZipFile(
            temporary_path, mode="w", compression=zipfile.ZIP_STORED, allowZip64=True
        ) as package:
            for index, (archive_path, source_path) in enumerate(files):
                info = zipfile.ZipInfo(archive_path, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED
                info.create_system = 0
                info.external_attr = 0o100644 << 16
                digest = hashlib.sha256()
                size = 0
                with source_path.open("rb") as source, package.open(
                    info, mode="w", force_zip64=True
                ) as destination:
                    while True:
                        block = source.read(CHUNK_SIZE)
                        if not block:
                            break
                        digest.update(block)
                        size += len(block)
                        destination.write(block)
                expected = manifest["files"][index]
                if size != expected["size"] or digest.hexdigest() != expected["sha256"]:
                    raise IOError(f"source changed while packaging: {archive_path}")
            manifest_bytes = (
                json.dumps(manifest, ensure_ascii=False, separators=(",", ":"), sort_keys=False)
                + "\n"
            ).encode("utf-8")
            info = zipfile.ZipInfo("qsan-media.json", date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 0
            info.external_attr = 0o100644 << 16
            package.writestr(info, manifest_bytes)
        os.replace(temporary_path, output_path)
    except BaseException:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass
        raise


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(CHUNK_SIZE)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def write_inventory(output_path: Path, files: list[tuple[str, Path]]) -> None:
    inventory = {
        "format": 1,
        "role": "media-inventory",
        "files": [archive_path for archive_path, _ in files],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        temporary_path.write_text(
            json.dumps(inventory, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary_path, output_path)
    except BaseException:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sourceRoot", type=Path, help="project root containing image/, audio/, and font/")
    parser.add_argument("out", type=Path, help="output ZIP path (required even for inspection modes)")
    parser.add_argument(
        "--manifest-only",
        action="store_true",
        help="scan and hash sources, then print qsan-media.json without writing a ZIP",
    )
    parser.add_argument(
        "--inspect",
        action="store_true",
        help="scan sources and print counts, expanded bytes, and the manifest; no ZIP is written",
    )
    parser.add_argument(
        "--inventory-output",
        type=Path,
        help="write media-inventory JSON containing paths only; skips hashing and ZIP creation",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_root = args.sourceRoot.resolve()
    output_path = args.out.resolve()
    if not source_root.is_dir():
        raise SystemExit(f"sourceRoot is not a directory: {source_root}")
    if sum(bool(value) for value in (args.manifest_only, args.inspect, args.inventory_output)) > 1:
        raise SystemExit("--manifest-only, --inspect, and --inventory-output are mutually exclusive")

    files = collect_files(source_root, output_path)
    if args.inventory_output:
        write_inventory(args.inventory_output.resolve(), files)
        print(f"created {args.inventory_output} files={len(files)}")
        return 0
    manifest = make_manifest(files)
    if args.manifest_only:
        print(json.dumps(manifest, ensure_ascii=False, separators=(",", ":")))
        return 0
    if args.inspect:
        print(json.dumps({"fileCount": len(files), **manifest}, ensure_ascii=False, separators=(",", ":")))
        return 0

    write_package(output_path, files, manifest)
    print(
        f"created {output_path} files={len(files)} expandedBytes={manifest['expandedBytes']} "
        f"sha256={sha256_file(output_path)}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("cancelled; temporary package removed", file=sys.stderr)
        raise SystemExit(130)
