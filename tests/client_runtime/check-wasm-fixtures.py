#!/usr/bin/env python3
"""Build-asset staging and strict native/WASM fixture parity (stdlib only)."""
from __future__ import annotations

import argparse
import difflib
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from typing import Any

HERE = Path(__file__).resolve().parent
MANIFEST = "fixture-assets.json"
# These are diagnostics, not rule implementations. Fail if the native negative
# corpus changes so this cross-platform gate cannot silently skip new cases.
NEGATIVE = {
    "malformed": "schema_version=1", "reason": "unknown card-use reason",
    "alias": "unknown fixture card key", "numeric-id": "request_id",
    "overflow-id": "request_id", "schema": "schema_version=1",
    "missing-card": "no registered card matches fixture key",
    "missing-general": "fixture requires general", "missing-skill": "fixture requires skill",
}


def native_harness():
    spec = importlib.util.spec_from_file_location("qsan_native_fixture_harness", HERE / "check-fixtures.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load the native fixture harness")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode()


def validate_manifest(manifest: Any) -> list[dict[str, Any]]:
    if (not isinstance(manifest, dict) or type(manifest.get("schema_version")) is not int
            or manifest["schema_version"] != 1 or manifest.get("profile") != "builtin-v1"):
        raise AssertionError("unsupported WASM asset manifest")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise AssertionError("empty WASM asset manifest")
    seen: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict):
            raise AssertionError("invalid WASM asset entry")
        name = entry.get("path")
        if (not isinstance(name, str) or not re.fullmatch(r"lua/[A-Za-z0-9_./-]+\.lua", name)
                or any(part in ("", ".", "..") for part in name.split("/"))
                or PurePosixPath(name).as_posix() != name or name in seen
                or type(entry.get("size")) is not int or entry["size"] <= 0
                or not isinstance(entry.get("sha256"), str)
                or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"])):
            raise AssertionError("invalid/duplicate WASM asset manifest entry")
        seen.add(name)
    return files


def inventory(directory: Path) -> list[dict[str, Any]]:
    result = []
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise AssertionError(f"symlinks are not allowed in fixture assets: {path}")
        if path.is_file() and path.relative_to(directory).as_posix() != MANIFEST:
            data = path.read_bytes()
            result.append({"path": path.relative_to(directory).as_posix(),
                           "size": len(data), "sha256": digest(data)})
    return result


def check_assets(directory: Path, manifest: Any) -> None:
    expected = validate_manifest(manifest)
    actual = inventory(directory)
    if actual != expected:
        raise AssertionError("ASSET_HASH_MISMATCH: native bootstrap closure differs from compiled WASM assets")


def prepare_assets(source: Path, destination: Path) -> None:
    """Use exactly the existing native harness's closure, not a second list."""
    source = source.resolve()
    if destination.is_symlink():
        raise AssertionError("asset staging destination must not be a symlink")
    destination = destination.resolve()
    if source == destination or source.is_relative_to(destination):
        raise AssertionError("asset staging must not replace the source tree or an ancestor")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="wasm-assets-", dir=destination.parent) as scratch:
        staged = Path(scratch) / "bundle"
        native_harness().stage_builtin_assets(source, staged)
        manifest = {"schema_version": 1, "profile": "builtin-v1", "files": inventory(staged)}
        validate_manifest(manifest)
        (staged / MANIFEST).write_bytes(json_bytes(manifest))
        if destination.exists():
            # Reconfigure may replace only an intact, previously generated
            # bundle. Do not recursively delete an arbitrary user directory.
            if not destination.is_dir() or not (destination / MANIFEST).is_file():
                raise AssertionError("refusing to replace unmanaged asset staging directory")
            old = json.loads((destination / MANIFEST).read_bytes())
            check_assets(destination, old)
            shutil.rmtree(destination)
        staged.rename(destination)


def compare_bytes(expected: bytes, actual: bytes, where: str) -> None:
    if expected != actual:
        diff = "\n".join(difflib.unified_diff(
            expected.decode("utf-8", "replace").splitlines(),
            actual.decode("utf-8", "replace").splitlines(),
            fromfile="native", tofile="wasm", lineterm=""))
        raise AssertionError(f"{where}: canonical output differs\n{diff[:6000]}")


def check_child(returncode: int, destination: Path, diagnostic: str | None, logs: bytes) -> bytes:
    if diagnostic is not None:
        if returncode == 0 or destination.exists():
            raise AssertionError("invalid fixture succeeded or published a result")
        if diagnostic not in logs.decode("utf-8", "replace"):
            raise AssertionError(f"failed for the wrong reason; expected {diagnostic!r}: {logs[-3000:]!r}")
        return b""
    if returncode != 0 or not destination.is_file():
        raise AssertionError(f"WASM fixture exit {returncode}: {logs[-4000:]!r}")
    return destination.read_bytes()


def invoke_wasm(args: argparse.Namespace, fixture: Path, tag: str, fixed_hash: bool,
                diagnostic: str | None = None) -> bytes:
    directory = args.artifacts / "wasm"
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / f"{tag}.json"
    output.unlink(missing_ok=True)
    env = os.environ.copy()
    if fixed_hash:
        env["QT_HASH_SEED"] = "0"
    else:
        env.pop("QT_HASH_SEED", None)
    if diagnostic is not None:
        env["QT_LOGGING_RULES"] = "*.critical=false"
    with tempfile.TemporaryDirectory(prefix="wasm-host-", dir=directory) as scratch:
        env["XDG_CONFIG_HOME"] = str(Path(scratch) / "config")
        env["XDG_DATA_HOME"] = str(Path(scratch) / "data")
        command = [str(args.node), str(HERE / "run-wasm-fixture.mjs"),
                   "--module", str(args.wasm_module), "--manifest", str(args.manifest),
                   "--fixture", str(fixture), "--output", str(output)]
        try:
            child = subprocess.run(command, cwd=scratch, env=env, capture_output=True,
                                   check=False, timeout=90)
        except subprocess.TimeoutExpired as error:
            (directory / f"{tag}.stdout.log").write_bytes(error.stdout or b"")
            (directory / f"{tag}.stderr.log").write_bytes(error.stderr or b"")
            raise
    (directory / f"{tag}.stdout.log").write_bytes(child.stdout)
    (directory / f"{tag}.stderr.log").write_bytes(child.stderr)
    return check_child(child.returncode, output, diagnostic, child.stdout + child.stderr)


def check(args: argparse.Namespace) -> dict[str, Any]:
    native = native_harness()
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    validate_manifest(manifest)
    wasm_binary = args.wasm_module.with_suffix(".wasm")
    with wasm_binary.open("rb") as stream:
        if stream.read(8) != b"\x00asm\x01\x00\x00\x00":
            raise AssertionError("missing/invalid WebAssembly binary")
    with tempfile.TemporaryDirectory(prefix="matched-assets-", dir=args.artifacts) as scratch:
        assets = Path(scratch)
        native.stage_builtin_assets(args.asset_root, assets)
        check_assets(assets, manifest)
        native_args = argparse.Namespace(runner=args.native_runner, fixtures=args.fixtures,
                                         asset_root=assets, artifacts=args.artifacts / "native")
        # Includes semantic assertions and nine negative cases, not just output
        # equality. Never use two agreeing implementations as the only oracle.
        native.check(native_args)
        paths = sorted(args.fixtures.glob("*.json"))
        if not paths:
            raise AssertionError("no checked-in fixtures")
        count = 0
        results = {}
        for path in paths:
            fixture = json.loads(path.read_bytes())
            native.validate_fixture(fixture)
            baseline = (native_args.artifacts / f"{path.stem}.first.json").read_bytes()
            for iteration, fixed in (("first", True), ("second", False)):
                actual = invoke_wasm(args, path, f"{path.stem}.{iteration}", fixed)
                native.verify_result(fixture, json.loads(actual))
                compare_bytes(baseline, actual, f"{path.stem}.{iteration}")
            count += len(fixture["queries"])
            results[path.stem] = {"fixture_sha256": digest(path.read_bytes()),
                                  "output_sha256": digest(baseline)}
        invalid = native_args.artifacts / "invalid"
        if {path.stem for path in invalid.glob("*.json")} != set(NEGATIVE):
            raise AssertionError("native negative corpus changed; update the WASM diagnostic contract")
        for name, diagnostic in NEGATIVE.items():
            invoke_wasm(args, invalid / f"{name}.json", f"negative-{name}", True, diagnostic)
    return {"schema_version": 1, "status": "PASS", "scope": "builtin-node-fixtures",
            "fixtures": len(paths), "queries": count, "native_positive_runs": len(paths) * 2,
            "wasm_positive_runs": len(paths) * 2, "negative_cases_per_backend": len(NEGATIVE),
            "results": results, "native_runner_sha256": digest(args.native_runner.read_bytes()),
            "wasm_module_sha256": digest(args.wasm_module.read_bytes()),
            "wasm_binary_sha256": digest(wasm_binary.read_bytes()),
            "asset_manifest_sha256": digest(manifest_bytes)}


class ParityTests(unittest.TestCase):
    def bundle(self, root: Path) -> dict[str, Any]:
        (root / "lua").mkdir()
        (root / "lua/config.lua").write_bytes(b"return {}\n")
        return {"schema_version": 1, "profile": "builtin-v1", "files": inventory(root)}

    def test_matching_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.bundle(root)
            check_assets(root, manifest)

    def test_content_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.bundle(root)
            (root / "lua/config.lua").write_bytes(b"return {changed=true}\n")
            with self.assertRaisesRegex(AssertionError, "ASSET_HASH_MISMATCH"):
                check_assets(root, manifest)

    def test_extra_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.bundle(root)
            (root / "lua/external.lua").write_bytes(b"extra")
            with self.assertRaisesRegex(AssertionError, "ASSET_HASH_MISMATCH"):
                check_assets(root, manifest)

    def test_path_traversal(self):
        value = {"schema_version": 1, "profile": "builtin-v1",
                 "files": [{"path": "lua/../secret.lua", "size": 1, "sha256": "0" * 64}]}
        with self.assertRaises(AssertionError):
            validate_manifest(value)

    def test_duplicate_entries(self):
        entry = {"path": "lua/a.lua", "size": 1, "sha256": "0" * 64}
        with self.assertRaises(AssertionError):
            validate_manifest({"schema_version": 1, "profile": "builtin-v1", "files": [entry, entry]})

    def test_boolean_schema_is_not_integer(self):
        with self.assertRaises(AssertionError):
            validate_manifest({"schema_version": True, "profile": "builtin-v1", "files": []})

    def test_equal_bytes(self):
        compare_bytes(b'{"id":"18446744073709551615"}\n', b'{"id":"18446744073709551615"}\n', "id")

    def test_no_normalization(self):
        for actual in (b'{"id":18446744073709551615}\n', b'{"id":"18446744073709551615"}',
                       b'{"id":"18446744073709551614"}\n'):
            with self.assertRaises(AssertionError):
                compare_bytes(b'{"id":"18446744073709551615"}\n', actual, "id")

    def test_array_order_and_duplicates(self):
        for actual in (b'["b","a","b"]', b'["a","b"]'):
            with self.assertRaises(AssertionError):
                compare_bytes(b'["a","b","b"]', actual, "targets")

    def test_registry_drift_is_not_ignored(self):
        with self.assertRaises(AssertionError):
            compare_bytes(b'{"card_registry_sha256":"a"}', b'{"card_registry_sha256":"b"}', "registry")

    def test_negative_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            self.assertEqual(check_child(4, output, "request_id", b"invalid request_id"), b"")
            with self.assertRaises(AssertionError):
                check_child(4, output, "request_id", b"runtime trap")
            output.write_bytes(b"{}")
            with self.assertRaises(AssertionError):
                check_child(4, output, "request_id", b"invalid request_id")

    def test_failed_child_cannot_pass_with_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            output.write_bytes(b"{}")
            with self.assertRaises(AssertionError):
                check_child(1, output, None, b"failed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument("--prepare-assets", action="store_true")
    parser.add_argument("--native-runner", type=Path)
    parser.add_argument("--wasm-module", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--fixtures", type=Path)
    parser.add_argument("--asset-root", type=Path)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ParityTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    required = ("asset_root", "artifacts") if args.prepare_assets else (
        "native_runner", "wasm_module", "manifest", "fixtures", "asset_root", "artifacts")
    for name in required:
        if getattr(args, name) is None:
            parser.error(f"--{name.replace('_', '-')} is required")
        # prepare_assets checks a destination symlink before resolving it.
        if name != "artifacts" or not args.prepare_assets:
            setattr(args, name, getattr(args, name).resolve())
    if args.prepare_assets:
        try:
            prepare_assets(args.asset_root, args.artifacts)
            return 0
        except (AssertionError, OSError, ValueError, RuntimeError) as error:
            print(f"WASM asset staging failed: {error}", file=sys.stderr)
            return 1
    node = shutil.which(args.node)
    if node is None:
        parser.error("Node executable does not exist")
    args.node = Path(node).resolve()
    for name in ("native_runner", "wasm_module", "manifest"):
        if not getattr(args, name).is_file():
            parser.error(f"{name} file does not exist")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    summary_path = args.artifacts / "parity-summary.json"
    summary_path.unlink(missing_ok=True)
    try:
        summary = check(args)
    except (AssertionError, OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        summary = {"schema_version": 1, "status": "FAIL", "detail": str(error)}
    summary_path.write_bytes(json_bytes(summary))
    print(f"[AUTOTEST] CLIENT_RULES_WASM_PARITY_RESULT status={summary['status']} "
          f"summary={summary_path}")
    if summary["status"] != "PASS":
        print(summary["detail"], file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
