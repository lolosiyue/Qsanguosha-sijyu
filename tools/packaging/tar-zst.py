#!/usr/bin/env python3
"""Create and extract .tar.zst archives.

`tar --zstd` shells out to the `zstd` binary, which is not installed
everywhere, and Python only grew `compression.zstd` in 3.14.  Packaging needs
this to work on both, so this prefers the `zstd` CLI and falls back to the
standard library, producing byte-identical semantics either way: a plain tar
stream compressed with zstd, extractable with `tar --zstd -xf`.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tarfile

ZSTD_LEVEL = 19


def _zstd_cli() -> str | None:
    return shutil.which("zstd")


def _compress(raw: bytes) -> bytes:
    cli = _zstd_cli()
    if cli:
        return subprocess.run([cli, f"-{ZSTD_LEVEL}", "-T0", "-q", "-c"],
                              input=raw, check=True, stdout=subprocess.PIPE).stdout
    try:
        from compression import zstd  # Python 3.14+
    except ImportError as error:  # pragma: no cover - depends on the host
        raise SystemExit(
            "neither the zstd CLI nor compression.zstd (Python 3.14+) is available"
        ) from error
    return zstd.compress(raw, level=ZSTD_LEVEL)


def _decompress(raw: bytes) -> bytes:
    cli = _zstd_cli()
    if cli:
        return subprocess.run([cli, "-d", "-q", "-c"], input=raw,
                              check=True, stdout=subprocess.PIPE).stdout
    try:
        from compression import zstd
    except ImportError as error:  # pragma: no cover - depends on the host
        raise SystemExit(
            "neither the zstd CLI nor compression.zstd (Python 3.14+) is available"
        ) from error
    return zstd.decompress(raw)


def create(source: pathlib.Path, output: pathlib.Path, top_level: str) -> None:
    """Archive `source` so that extracting produces a single `top_level/` directory."""
    import io

    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as archive:
        def reset(info: tarfile.TarInfo) -> tarfile.TarInfo:
            # Reproducibility and hygiene: no packager uid/gid or user name
            # travels with the artifact.
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = int(info.mtime)
            return info

        archive.add(source, arcname=top_level, filter=reset, recursive=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(_compress(buffer.getvalue()))


def extract(archive: pathlib.Path, destination: pathlib.Path) -> None:
    import io

    destination.mkdir(parents=True, exist_ok=True)
    raw = _decompress(archive.read_bytes())
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as handle:
        handle.extractall(destination, filter="tar")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    creator = subparsers.add_parser("create")
    creator.add_argument("--source", required=True, type=pathlib.Path)
    creator.add_argument("--output", required=True, type=pathlib.Path)
    creator.add_argument("--top-level", required=True)

    extractor = subparsers.add_parser("extract")
    extractor.add_argument("--archive", required=True, type=pathlib.Path)
    extractor.add_argument("--destination", required=True, type=pathlib.Path)

    arguments = parser.parse_args(argv)
    if arguments.command == "create":
        create(arguments.source, arguments.output, arguments.top_level)
        size = arguments.output.stat().st_size
        print(f"{arguments.output} ({size} bytes)")
    else:
        extract(arguments.archive, arguments.destination)
        print(f"extracted to {arguments.destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
