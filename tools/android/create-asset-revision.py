#!/usr/bin/env python3
"""Create a deterministic revision for the files bundled in an Android APK.

The input is a UTF-8 text file containing one ``alias<TAB>source`` pair per
line.  Aliases are the stable resource paths, so build-directory paths never
enter the revision identity.  File contents are streamed into SHA-256.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


CHUNK_SIZE = 1024 * 1024


def read_inputs(path: Path) -> list[tuple[str, Path]]:
    entries: list[tuple[str, Path]] = []
    seen: set[str] = set()
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line.strip():
            continue
        alias, separator, source = raw_line.partition("\t")
        if not separator or not alias or not source:
            raise ValueError(f"invalid input-list line {line_number}")
        if alias in seen:
            raise ValueError(f"duplicate resource alias: {alias}")
        source_path = Path(source)
        if not source_path.is_file():
            raise ValueError(f"resource input is not a file: {source_path}")
        seen.add(alias)
        entries.append((alias, source_path))
    return sorted(entries, key=lambda entry: (entry[0].casefold(), entry[0]))


def revision(entries: list[tuple[str, Path]]) -> str:
    digest = hashlib.sha256()
    for alias, source in entries:
        alias_bytes = alias.encode("utf-8")
        # Length framing keeps names and file bytes unambiguous while retaining
        # a single streaming digest over the complete resource inventory.
        digest.update(len(alias_bytes).to_bytes(8, "big"))
        digest.update(alias_bytes)
        with source.open("rb") as stream:
            while block := stream.read(CHUNK_SIZE):
                digest.update(len(block).to_bytes(8, "big"))
                digest.update(block)
        digest.update((0).to_bytes(8, "big"))
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_list", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    marker = revision(read_inputs(args.input_list))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Keep the bounded resource identical on Windows and Unix (no CRLF).
    args.output.write_bytes((marker + "\n").encode("ascii"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
