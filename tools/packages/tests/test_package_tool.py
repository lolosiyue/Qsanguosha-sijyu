"""Focused contracts for the package CLI. Run explicitly by the maintainer."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_tool


class PackageToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for name in ("extensions/alpha.lua", "lua/lib/alpha.lua", "lang/zh_CN/alpha.lua",
                     "lua/ai/alpha-ai.lua", "image/alpha/icon.png"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        (self.root / "lua/config.lua").parent.mkdir(parents=True, exist_ok=True)
        (self.root / "lua/config.lua").write_text(
            'extension_names = {\n "extensions/alpha.lua;libs=lua/lib/alpha.lua;'
            'lang=lang/zh_CN/alpha.lua;ai=lua/ai/alpha-ai.lua",\n}\n', encoding="utf-8")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def mapping(self) -> dict:
        return {"manifest": {"schema_version": 1, "id": "alpha-pack", "version": "1.0.0",
                "engine_api": 1, "dependencies": [], "extensions": [{"name": "alpha",
                "script": "lua/alpha.lua", "dependencies": [], "libs": ["lua/lib/alpha.lua"],
                "lang": ["translation/zh_CN/alpha.lua"], "ai": ["lua/ai/alpha-ai.lua"]}],
                "assets": {"image/alpha/icon.png": "image/alpha/icon.png"}},
                "files": {"extensions/alpha.lua": "lua/alpha.lua",
                          "lang/zh_CN/alpha.lua": "translation/zh_CN/alpha.lua"},
                "asset_owners": {"image/alpha/icon.png": "alpha"}}

    def test_inspect_preserves_declaration_order_and_reports_unowned_assets(self) -> None:
        result = package_tool.inventory(self.root)
        self.assertEqual(result["declaration_order"][0].split(";")[0], "extensions/alpha.lua")
        self.assertEqual(result["unresolved_assets"][0]["source"], "image/alpha/icon.png")

    def test_inspect_can_use_runtime_content_json_without_config(self) -> None:
        (self.root / "lua/config.lua").unlink()
        (self.root / "runtime-content.json").write_text(json.dumps({"schema_version": 2,
            "profile": "declared-v2", "extensions": [{"name": "alpha",
            "script": "extensions/alpha.lua", "dependencies": [], "libs": [], "lang": [],
            "ai": ["lua/ai/alpha-ai.lua"]}]}), encoding="utf-8")
        result = package_tool.inventory(self.root)
        self.assertEqual(result["declared_files"][0], "extensions/alpha.lua")

    def test_migrate_copies_and_seals_explicit_content(self) -> None:
        mapping = self.root / "mapping.json"
        mapping.write_text(json.dumps(self.mapping()), encoding="utf-8")
        destination = self.root.parent / (self.root.name + "-package")
        try:
            package_tool.migrate(self.root, mapping, destination)
            manifest = json.loads((destination / "manifest.json").read_text(encoding="utf-8"))
            record = next(entry for entry in manifest["files"] if entry["path"].endswith("icon.png"))
            self.assertEqual(record["sha256"], hashlib.sha256(b"image/alpha/icon.png").hexdigest())
            self.assertTrue((self.root / "extensions/alpha.lua").is_file())
            self.assertTrue((destination / "lua/alpha.lua").is_file())
        finally:
            if destination.exists():
                import shutil
                shutil.rmtree(destination)

    def test_migrate_requires_asset_ownership(self) -> None:
        mapping = self.root / "mapping.json"
        value = self.mapping()
        value["asset_owners"] = {}
        mapping.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(package_tool.PackageError, "ownership"):
            package_tool.migrate(self.root, mapping, self.root.parent / (self.root.name + "-bad"))

    def test_native_asset_owner_can_be_the_package_id(self) -> None:
        mapping = self.root / "native-mapping.json"
        value = {"manifest": {"schema_version": 1, "id": "standard", "version": "TODO",
                  "engine_api": 1, "dependencies": [], "extensions": [],
                  "assets": {"image/alpha/icon.png": "image/alpha/icon.png"}},
                  "asset_owners": {"image/alpha/icon.png": "standard"}}
        mapping.write_text(json.dumps(value), encoding="utf-8")
        destination = self.root.parent / (self.root.name + "-native")
        try:
            package_tool.migrate(self.root, mapping, destination)
            manifest = json.loads((destination / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["files"][0]["role"], "data")
        finally:
            if destination.exists():
                import shutil
                shutil.rmtree(destination)


if __name__ == "__main__":
    unittest.main()
