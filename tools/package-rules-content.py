#!/usr/bin/env python3
"""Package declared rules content into a content-addressed public directory."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
import tempfile
import unittest


ROLES = {"rules", "presentation"}


def _safe_path(root: Path, relative: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9_./-]+\.lua", relative):
        raise ValueError("content path traversal is not allowed")
    path = Path(relative)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in relative.split("/")):
        raise ValueError("content path traversal is not allowed")
    result = root.joinpath(*path.parts)
    if any(parent.is_symlink() for parent in (root, *result.parents)):
        raise ValueError("symlinked content path is not allowed")
    return result


def _validate_role_path(relative: str, role: str) -> None:
    if role == "presentation":
        valid = relative.startswith("lang/") and relative.endswith(".lua")
    else:
        valid = ((re.fullmatch(r"extensions/[A-Za-z0-9_.-]+\.lua", relative) is not None
                  or relative.startswith("lua/"))
                 and relative.endswith(".lua")
                 and not relative.startswith("lua/ai/")
                 and relative != "lua/lib/middleclass.lua")
    if not valid:
        raise ValueError("content path does not match its role")


def package(bundle_path: Path, asset_root: Path, destination: Path) -> int:
    bundle = json.loads(bundle_path.read_text(encoding="utf-8"))
    manifest = bundle.get("rules_content")
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1 \
            or manifest.get("profile") != "declared-v1" or not isinstance(manifest.get("files"), list):
        raise ValueError("invalid rules_content manifest")
    if destination.is_symlink() or any(parent.is_symlink() for parent in destination.parents):
        raise ValueError("destination must not be a symlink")
    destination.mkdir(parents=True, exist_ok=True)
    pending: list[tuple[Path, bytes, str]] = []
    seen: set[str] = set()
    for entry in manifest["files"]:
        if not isinstance(entry, dict) or set(entry) != {"path", "role", "size", "sha256"}:
            raise ValueError("invalid content manifest entry")
        relative, role, size, digest = entry["path"], entry["role"], entry["size"], entry["sha256"]
        if not isinstance(relative, str) or not isinstance(role, str) or role not in ROLES \
                or not isinstance(size, int) or isinstance(size, bool) or size < 0 \
                or not isinstance(digest, str) or len(digest) != 64 \
                or any(char not in "0123456789abcdef" for char in digest) or relative in seen:
            raise ValueError("invalid content manifest entry")
        seen.add(relative)
        _validate_role_path(relative, role)
        source = _safe_path(asset_root, relative)
        if not source.is_file() or source.is_symlink():
            raise ValueError("declared content file is missing or symlinked")
        data = source.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("declared content hash or size mismatch")
        target = destination / digest
        if target.is_symlink() or (target.exists() and target.read_bytes() != data):
            raise ValueError("content hash collision or symlink target")
        pending.append((target, data, digest))
    for target, data, _ in pending:
        if not target.exists():
            target.write_bytes(data)
    return len(pending)


class PackageTests(unittest.TestCase):
    def test_hash_and_unrelated_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); assets = root / "assets"; dest = root / "public"
            source = assets / "lua/config.lua"; source.parent.mkdir(parents=True)
            source.write_bytes(b"rules")
            digest = hashlib.sha256(b"rules").hexdigest()
            (dest).mkdir(); (dest / "keep").write_bytes(b"keep")
            bundle = root / "bundle.json"
            bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                "profile": "declared-v1", "files": [{"path": "lua/config.lua", "role": "rules",
                "size": 5, "sha256": digest}]}}), encoding="utf-8")
            self.assertEqual(package(bundle, assets, dest), 1)
            self.assertTrue((dest / "keep").is_file())
            self.assertEqual((dest / digest).read_bytes(), b"rules")

    def test_bad_manifest_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); assets = root / "assets"; assets.mkdir(); dest = root / "public"
            for path, role in (("../x", "rules"), ("x", "bad"),
                               ("lua\\x.lua", "rules"), ("lua//x.lua", "rules"),
                               ("lua/./x.lua", "rules"), ("lang/x.lua", "rules"),
                               ("lua/ai/x.lua", "rules"),
                               ("lua/lib/middleclass.lua", "rules"),
                               ("extensions/x.lua", "presentation")):
                bundle = root / "bundle.json"
                bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                    "profile": "declared-v1", "files": [{"path": path, "role": role,
                    "size": 0, "sha256": "0" * 64}]}}), encoding="utf-8")
                with self.assertRaises(ValueError): package(bundle, assets, dest)

            source = assets / "lua/x.lua"; source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(b"x")
            digest = hashlib.sha256(b"x").hexdigest()
            bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                "profile": "declared-v1", "files": [
                    {"path": "lua/x.lua", "role": "rules", "size": 1, "sha256": digest},
                    {"path": "lua/missing.lua", "role": "rules", "size": 0, "sha256": "0" * 64}]}}),
                encoding="utf-8")
            with self.assertRaises(ValueError): package(bundle, assets, dest)
            self.assertFalse((dest / digest).exists())

    def test_zero_byte_file_is_packaged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); assets = root / "assets"; dest = root / "public"
            source = assets / "lua/empty.lua"; source.parent.mkdir(parents=True); source.write_bytes(b"")
            digest = hashlib.sha256(b"").hexdigest()
            bundle = root / "bundle.json"
            bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                "profile": "declared-v1", "files": [{"path": "lua/empty.lua", "role": "rules",
                "size": 0, "sha256": digest}]}}), encoding="utf-8")
            self.assertEqual(package(bundle, assets, dest), 1)
            self.assertEqual((dest / digest).read_bytes(), b"")

    def test_byte_drift_and_symlink_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); assets = root / "assets"; assets.mkdir(); dest = root / "public"
            source = assets / "lua/x.lua"; source.parent.mkdir(parents=True); source.write_bytes(b"a")
            digest = hashlib.sha256(b"b").hexdigest()
            bundle = root / "bundle.json"
            bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                "profile": "declared-v1", "files": [{"path": "lua/x.lua", "role": "rules",
                "size": 1, "sha256": digest}]}}), encoding="utf-8")
            with self.assertRaises(ValueError): package(bundle, assets, dest)
            link = assets / "lua/link.lua"
            link.symlink_to(source)
            bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                "profile": "declared-v1", "files": [{"path": "lua/link.lua", "role": "rules",
                "size": 1, "sha256": hashlib.sha256(b"a").hexdigest()}]}}), encoding="utf-8")
            with self.assertRaises(ValueError): package(bundle, assets, dest)
            dangling = assets / "lua/dangling.lua"
            dangling.symlink_to(assets / "lua/no-such.lua")
            bundle.write_text(json.dumps({"rules_content": {"schema_version": 1,
                "profile": "declared-v1", "files": [{"path": "lua/dangling.lua", "role": "rules",
                "size": 1, "sha256": hashlib.sha256(b"a").hexdigest()}]}}), encoding="utf-8")
            with self.assertRaises(ValueError): package(bundle, assets, dest)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--asset-root", type=Path)
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(PackageTests)).wasSuccessful() else 1
    for name in ("bundle", "asset_root", "destination"):
        if getattr(args, name) is None:
            parser.error(f"--{name.replace('_', '-') } is required")
    try:
        print(f"packaged {package(args.bundle.resolve(), args.asset_root, args.destination)} files")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
