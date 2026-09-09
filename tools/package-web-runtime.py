#!/usr/bin/env python3
"""Publish the three generated Web client runtime artifacts (stdlib only)."""
from __future__ import annotations

import argparse
import json
import hashlib
import re
from pathlib import Path
import shutil
import tempfile


def deployment_bundle(module: Path) -> dict:
    # Only the build tool emits artifact pairing metadata. The rules identity
    # itself is exported by C++; Web never supplies a package/content manifest.
    header = Path(__file__).resolve().parents[1] / "src/core/protocol/rules-bundle-identity.h"
    match = re.search(r"constexpr int BridgeSchema = (\d+);", header.read_text(encoding="utf-8"))
    if not match:
        raise ValueError("native bridge schema is missing")
    sources = [module, module.with_suffix(".wasm")]
    return {"schema_version": 1, "bridge_schema": int(match.group(1)),
            "files": {source.name: hashlib.sha256(source.read_bytes()).hexdigest() for source in sources}}


def write_bundle(module: Path) -> None:
    bundle = deployment_bundle(module)
    target = module.with_suffix(".bundle.json")
    if target.is_symlink():
        raise ValueError("runtime deployment manifest must not be a symlink")
    target.write_text(json.dumps(bundle, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")


def package(module: Path, destination: Path) -> None:
    if module.name != "qsanguosha_client_wasm.mjs":
        raise ValueError("--module must name qsanguosha_client_wasm.mjs")
    if module.is_symlink():
        raise ValueError("runtime module must not be a symlink")
    module = module.resolve(strict=True)
    sources = [module, module.with_suffix(".wasm")]
    for source in sources:
        if not source.is_file() or source.is_symlink() or source.stat().st_size == 0:
            raise ValueError(f"missing, empty or symlinked runtime artifact: {source}")
    with sources[1].open("rb") as binary:
        if binary.read(8) != b"\x00asm\x01\x00\x00\x00":
            raise ValueError("invalid WebAssembly binary")

    # Refuse changed artifacts after the build, rather than blessing a mixed
    # loader/binary by generating a fresh manifest during publication.
    bundle_path = module.with_suffix(".bundle.json")
    if bundle_path.is_symlink() or json.loads(bundle_path.read_bytes()) != deployment_bundle(module):
        raise ValueError("runtime artifact pairing changed; rebuild before packaging")
    sources.append(bundle_path)

    if destination.is_symlink():
        raise ValueError("runtime destination must not be a symlink")
    destination = destination.resolve()
    if destination == module.parent:
        raise ValueError("runtime destination must differ from the build output")
    destination.mkdir(parents=True, exist_ok=True)
    for source in sources:
        target = destination / source.name
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise ValueError(f"refusing to replace a non-file runtime destination: {target}")

    # Stage all input bytes before publishing. Only these generated names are
    # replaced; unrelated public files are never removed or copied recursively.
    with tempfile.TemporaryDirectory(prefix=".qsan-runtime-", dir=destination) as scratch:
        staged = Path(scratch)
        for source in sources:
            shutil.copyfile(source, staged / source.name)
        for source in sources:
            (staged / source.name).replace(destination / source.name)
    print(f"Packaged Web runtime: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--write-bundle", action="store_true")
    args = parser.parse_args()
    try:
        if args.write_bundle and args.destination is None:
            write_bundle(args.module)
        elif args.destination is not None and not args.write_bundle:
            package(args.module, args.destination)
        else:
            raise ValueError("specify either --destination or --write-bundle")
    except (AssertionError, OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Web runtime packaging failed: {error}\n")


if __name__ == "__main__":
    main()
