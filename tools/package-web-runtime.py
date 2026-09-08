#!/usr/bin/env python3
"""Publish the three generated Web client runtime artifacts (stdlib only)."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile


def package(module: Path, destination: Path) -> None:
    if module.name != "qsanguosha_client_wasm.mjs":
        raise ValueError("--module must name qsanguosha_client_wasm.mjs")
    if module.is_symlink():
        raise ValueError("runtime module must not be a symlink")
    module = module.resolve(strict=True)
    sources = [module, module.with_suffix(".wasm"), module.with_suffix(".assets.json")]
    for source in sources:
        if not source.is_file() or source.is_symlink() or source.stat().st_size == 0:
            raise ValueError(f"missing, empty or symlinked runtime artifact: {source}")
    with sources[1].open("rb") as binary:
        if binary.read(8) != b"\x00asm\x01\x00\x00\x00":
            raise ValueError("invalid WebAssembly binary")

    # Reuse the build's manifest schema; packaging never runs a fixture or
    # loads the generated JS/WASM. The Worker checks embedded bytes on startup.
    harness = Path(__file__).resolve().parents[1] / "tests/client_runtime/check-wasm-fixtures.py"
    spec = importlib.util.spec_from_file_location("qsan_wasm_asset_manifest", harness)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load the shared WASM asset manifest validator")
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    validator.validate_manifest(json.loads(sources[2].read_bytes()))

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
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    try:
        package(args.module, args.destination)
    except (AssertionError, OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Web runtime packaging failed: {error}\n")


if __name__ == "__main__":
    main()
