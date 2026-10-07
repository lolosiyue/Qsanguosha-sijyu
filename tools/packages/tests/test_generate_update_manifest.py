from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


TOOL = Path(__file__).resolve().parents[2] / "generate-update-manifest.py"
SPEC = importlib.util.spec_from_file_location("generate_update_manifest", TOOL)
assert SPEC and SPEC.loader
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)

BASE = "https://updates.example.invalid/materials/"
GAME_VERSION = "20251231"


def package_manifest(payload: bytes = b"offline package", *, package_id: str = "material-test",
                     version: str = "1.2.0", digest: str | None = None,
                     name: str | None = "Small material shard",
                     notes: str | None = "Generated in a local test") -> dict:
    entry = {
        "path": "image/test.png",
        "role": "data",
        "size": len(payload),
        "sha256": digest or hashlib.sha256(payload).hexdigest(),
    }
    result = {
        "schema_version": 1,
        "engine_api": 1,
        "id": package_id,
        "version": version,
        "dependencies": [],
        "extensions": [],
        "assets": {"image/test.png": "image/test.png"},
        "files": [entry],
    }
    if name is not None:
        result["name"] = name
    if notes is not None:
        result["notes"] = notes
    return result


def write_package_zip(path: Path, *, manifest: dict | bytes | None = None,
                      payload: bytes = b"offline package", extra: dict[str, bytes] | None = None,
                      wrapper: str = "", payload_compression: int = zipfile.ZIP_STORED,
                      unsafe_member: str | None = None, symlink_member: str | None = None) -> None:
    if manifest is None:
        manifest = package_manifest(payload)
    raw_manifest = manifest if isinstance(manifest, bytes) else json.dumps(manifest).encode("utf-8")
    prefix = wrapper.rstrip("/") + "/" if wrapper else ""
    with zipfile.ZipFile(path, "w", allowZip64=True) as archive:
        if wrapper:
            directory = zipfile.ZipInfo(prefix)
            directory.create_system = 3
            directory.external_attr = (stat.S_IFDIR | 0o755) << 16
            archive.writestr(directory, b"")
        manifest_info = zipfile.ZipInfo(prefix + "manifest.json")
        manifest_info.compress_type = zipfile.ZIP_STORED
        archive.writestr(manifest_info, raw_manifest)
        if unsafe_member is not None:
            archive.writestr(unsafe_member, b"outside")
        elif symlink_member is not None:
            link = zipfile.ZipInfo(prefix + symlink_member)
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(link, b"../outside")
        else:
            payload_info = zipfile.ZipInfo(prefix + "image/test.png")
            payload_info.compress_type = payload_compression
            archive.writestr(payload_info, payload)
        for name, data in (extra or {}).items():
            archive.writestr(prefix + name, data)


class GenerateUpdateManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.input_dir = self.root / "packages"
        self.input_dir.mkdir()
        self.output = self.root / "catalog.json"

    def tearDown(self):
        self.temp.cleanup()

    def generate(self, base_url: str = BASE) -> dict:
        return generator.generate(self.input_dir, self.output, base_url, GAME_VERSION)

    def test_generates_parser_compatible_catalog_and_encodes_filename(self):
        archive = self.input_dir / "art shard #1.zip"
        write_package_zip(archive, wrapper="material-test")
        document = self.generate()

        self.assertEqual(document["schema_version"], 1)
        self.assertEqual(len(document["packages"]), 1)
        package = document["packages"][0]
        self.assertEqual(set(package), {"id", "version", "game_version", "url", "size", "sha256", "name", "notes"})
        self.assertEqual(package["id"], "material-test")
        self.assertEqual(package["version"], "1.2.0")
        self.assertEqual(package["game_version"], GAME_VERSION)
        self.assertEqual(package["url"], BASE + "art%20shard%20%231.zip")
        self.assertEqual(package["size"], archive.stat().st_size)
        self.assertEqual(package["sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(package["name"], "Small material shard")
        self.assertEqual(json.loads(self.output.read_text(encoding="utf-8")), document)

    def test_rejects_loose_inputs_with_packaging_guidance_without_overwriting_output(self):
        self.output.write_text("keep this catalog", encoding="utf-8")
        (self.input_dir / "portrait.png").write_bytes(b"not an owned package")
        with self.assertRaisesRegex(generator.ManifestGenerationError, "existing package tools"):
            self.generate()
        self.assertEqual(self.output.read_text(encoding="utf-8"), "keep this catalog")

    def test_rejects_unsafe_or_credentialed_base_urls(self):
        write_package_zip(self.input_dir / "one.zip")
        for url in (
            "http://updates.example.invalid/materials/",
            "https://user:secret@updates.example.invalid/materials/",
            "https://updates.example.invalid/materials/#fragment",
            "https://updates.example.invalid:444/materials/",
            "https://updates.example.invalid/materials/?token=secret",
        ):
            with self.subTest(url=url), self.assertRaises(generator.ManifestGenerationError):
                self.generate(url)
        self.assertFalse(self.output.exists())

    def test_rejects_duplicate_ids_across_archives(self):
        write_package_zip(self.input_dir / "first.zip", manifest=package_manifest(package_id="same-id"))
        write_package_zip(self.input_dir / "second.zip", manifest=package_manifest(package_id="same-id"))
        with self.assertRaisesRegex(generator.ManifestGenerationError, "duplicate package ID"):
            self.generate()
        self.assertFalse(self.output.exists())

    def test_rejects_bad_manifest_version_or_payload_hash(self):
        write_package_zip(self.input_dir / "bad-version.zip",
                          manifest=package_manifest(version="1.02.0"))
        with self.assertRaisesRegex(generator.ManifestGenerationError, "supported version"):
            self.generate()

        (self.input_dir / "bad-version.zip").unlink()
        manifest = package_manifest(digest="0" * 64)
        write_package_zip(self.input_dir / "bad-hash.zip", manifest=manifest)
        with self.assertRaisesRegex(generator.ManifestGenerationError, "SHA-256 mismatch"):
            self.generate()

    def test_rejects_traversal_symlinks_unlisted_members_and_corrupt_zip(self):
        write_package_zip(self.input_dir / "traversal.zip", unsafe_member="../outside.txt")
        with self.assertRaisesRegex(generator.ManifestGenerationError, "unsafe ZIP member"):
            self.generate()
        (self.input_dir / "traversal.zip").unlink()

        write_package_zip(self.input_dir / "symlink.zip", symlink_member="image/link.png")
        with self.assertRaisesRegex(generator.ManifestGenerationError, "symlink or special"):
            self.generate()
        (self.input_dir / "symlink.zip").unlink()

        write_package_zip(self.input_dir / "unlisted.zip", extra={"data/unlisted.bin": b"extra"})
        with self.assertRaisesRegex(generator.ManifestGenerationError, "unlisted package file"):
            self.generate()
        (self.input_dir / "unlisted.zip").unlink()

        (self.input_dir / "broken.zip").write_bytes(b"not a zip")
        with self.assertRaises(generator.ManifestGenerationError):
            self.generate()

    def test_checks_archive_limits_before_expanding_large_data(self):
        write_package_zip(self.input_dir / "bounded.zip", payload=b"A" * 2000,
                          payload_compression=zipfile.ZIP_DEFLATED)
        cases = (
            ("MAX_ARCHIVE_BYTES", 1),
            ("MAX_EXPANDED_BYTES", 1),
            ("MAX_MEMBER_BYTES", 4),
            ("MAX_ARCHIVE_ENTRIES", 1),
            ("MAX_ZIP_METADATA_BYTES", 1),
            ("MAX_PACKAGE_MANIFEST_BYTES", 4),
            ("MAX_COMPRESSION_RATIO", 1),
            ("MAX_CATALOG_BYTES", 1),
        )
        for constant, value in cases:
            with self.subTest(constant=constant), mock.patch.object(generator, constant, value):
                with self.assertRaises(generator.ManifestGenerationError):
                    self.generate()
        self.assertFalse(self.output.exists())

    def test_cli_requires_explicit_input_output_base_and_game_version(self):
        archive = self.input_dir / "local.zip"
        write_package_zip(archive)
        result = subprocess.run(
            [sys.executable, str(TOOL), "--input-dir", str(self.input_dir),
             "--output", str(self.output), "--base-url", BASE,
             "--game-version", GAME_VERSION],
            check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.output.is_file())
        self.assertEqual(json.loads(self.output.read_text(encoding="utf-8"))["packages"][0]["id"],
                         "material-test")


if __name__ == "__main__":
    unittest.main()
