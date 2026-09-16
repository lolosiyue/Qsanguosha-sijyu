"""Focused Browser Solo package-v1 preparation contracts."""
from __future__ import annotations

import hashlib
import importlib.util
import json
from argparse import Namespace
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "packages"))
spec = importlib.util.spec_from_file_location("package_web_solo", ROOT / "tools" / "package-web-solo.py")
assert spec is not None and spec.loader is not None
solo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(solo)


def entry(path: str, role: str, payload: bytes) -> dict:
    return {"path": path, "role": role, "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest()}


def create_package(root: Path, *, extensions: list[dict], files: dict[str, tuple[str, bytes]],
                   assets: dict[str, str] | None = None) -> Path:
    package = root / "packages" / "alpha-pack"
    package.mkdir(parents=True)
    records = []
    for relative, (role, payload) in files.items():
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        records.append(entry(relative, role, payload))
    manifest = {"schema_version": 1, "id": "alpha-pack", "version": "0.0.0-local",
        "engine_api": 1, "dependencies": [], "extensions": extensions,
        "assets": assets or {}, "files": records}
    (package / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return package


def extension() -> dict:
    return {"name": "alpha", "script": "lua/alpha.lua", "dependencies": [],
            "libs": [], "lang": [], "ai": []}


def write_file(root: Path, relative: str, payload: bytes) -> None:
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)


def create_full_package_fixture(root: Path) -> Namespace:
    asset_root = root / "runtime"
    web_dist = root / "web-dist"
    web_rules = web_dist / "rules"
    package_extension = extension()
    package_extension.update(libs=["lua/helper.lua"], lang=["translation/alpha.lua"],
                             ai=["lua/ai/alpha_ai.lua"])
    package_files = {
        "lua/alpha.lua": ("rules", b"return true\n"),
        "lua/helper.lua": ("rules", b"return 'helper'\n"),
        "translation/alpha.lua": ("presentation", b"return {}\n"),
        "lua/ai/alpha_ai.lua": ("ai", b"return {}\n"),
    }
    create_package(asset_root, extensions=[package_extension], files=package_files)
    core = {
        "lua/config.lua": b'extension_names = { "extensions/alpha.lua" }\n',
        "lua/sanguosha.lua": b"core sanguosha\n",
        "lua/utilities.lua": b"core utilities\n",
        "lua/sgs_ex.lua": b"core extensions\n",
        "lua/lib/json.lua": b"core json\n",
    }
    bundle_entries = [entry(path, "rules", data) for path, data in core.items()]
    bundle_entries.extend(entry(f"packages/alpha-pack/{path}", role, data)
                          for path, (role, data) in package_files.items() if role != "ai")
    package_descriptor = {"id": "alpha-pack", "version": "0.0.0-local",
                          "dependencies": [], "assets": {}}
    runtime_extension = {"name": "alpha", "script": "packages/alpha-pack/lua/alpha.lua",
                         "dependencies": [], "libs": ["packages/alpha-pack/lua/helper.lua"],
                         "lang": ["packages/alpha-pack/translation/alpha.lua"],
                         "ai": ["packages/alpha-pack/lua/ai/alpha_ai.lua"]}
    bundle_path = root / "rules-bundle.json"
    bundle_path.write_text(json.dumps({"rules_content": {
        "schema_version": 3, "profile": "packages-v1",
        "runtime_content": {"schema_version": 3, "profile": "packages-v1",
                            "extensions": [runtime_extension], "packages": [package_descriptor]},
        "files": bundle_entries,
    }}), encoding="utf-8")
    for path, data in core.items():
        write_file(asset_root, path, data)
    write_file(asset_root, "lua/ai/global.lua", b"return {}\n")
    write_file(asset_root, "lua/lib/middleclass.lua", b"return {}\n")
    web_dist.mkdir(parents=True)
    write_file(web_dist, "index.html", b"<html></html>")
    client_module = b"client module"
    client_wasm = b"\x00asm\x01\x00\x00\x00client"
    write_file(web_dist, "rules/qsanguosha_client_wasm.mjs", client_module)
    write_file(web_dist, "rules/qsanguosha_client_wasm.wasm", client_wasm)
    write_file(web_rules, "qsanguosha_client_wasm.bundle.json", json.dumps({
        "schema_version": 1, "bridge_schema": 1,
        "files": {"qsanguosha_client_wasm.mjs": hashlib.sha256(client_module).hexdigest(),
                  "qsanguosha_client_wasm.wasm": hashlib.sha256(client_wasm).hexdigest()},
    }).encode())
    module = root / "solo" / "qsanguosha_solo_wasm.mjs"
    write_file(module.parent, module.name, b"solo module")
    write_file(module.parent, "qsanguosha_solo_wasm.wasm", b"\x00asm\x01\x00\x00\x00solo")
    solo.seal_runtime(module)
    launcher = root / "StartGame.exe"
    launcher.write_bytes(b"launcher fixture")
    return Namespace(web_dist=web_dist, asset_root=asset_root, launcher=launcher,
                     rules_bundle=bundle_path, solo_module=module, destination=root / "output")


class PackageWebSoloTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_code_only_package_preserves_unsealed_optional_media_aliases(self) -> None:
        create_package(self.root, extensions=[extension()], files={
            "lua/alpha.lua": ("rules", b"return true\n")},
            assets={"image/optional.png": "image/optional.png"})

        [package] = solo._load_packages(self.root / "packages")

        self.assertEqual(package["assets"], {"image/optional.png": "image/optional.png"})
        self.assertEqual(package["extensions"][0]["script"], "packages/alpha-pack/lua/alpha.lua")
        self.assertNotIn("package", package["extensions"][0])

    def test_all_aliases_for_one_sealed_media_file_are_retained(self) -> None:
        create_package(self.root, extensions=[], files={
            "image/icon.png": ("data", b"png bytes")}, assets={
                "image/legacy-a.png": "image/icon.png",
                "image/legacy-b.png": "image/icon.png"})

        [package] = solo._load_packages(self.root / "packages")

        self.assertEqual(package["assets"], {
            "image/legacy-a.png": "image/icon.png",
            "image/legacy-b.png": "image/icon.png"})

    def test_data_role_accepts_native_data_root_without_legacy_alias(self) -> None:
        create_package(self.root, extensions=[], files={
            "data/payload.bin": ("data", b"data")})

        [package] = solo._load_packages(self.root / "packages")
        self.assertEqual(package["assets"], {})

    def test_unlisted_physical_file_is_rejected(self) -> None:
        package_root = create_package(self.root, extensions=[], files={
            "image/icon.png": ("data", b"png")})
        (package_root / "image/extra.png").write_bytes(b"unsealed")

        with self.assertRaisesRegex(ValueError, "unlisted package file"):
            solo._load_packages(self.root / "packages")

    def test_unreferenced_code_file_record_is_rejected(self) -> None:
        create_package(self.root, extensions=[extension()], files={
            "lua/alpha.lua": ("rules", b"return true\n"),
            "lua/extra.lua": ("rules", b"return false\n")})

        with self.assertRaisesRegex(ValueError, "not declared by an extension"):
            solo._load_packages(self.root / "packages")

    def test_package_loader_preserves_libs_lang_and_ai_declarations(self) -> None:
        declared = extension()
        declared.update(libs=["lua/helper.lua"], lang=["translation/alpha.lua"],
                        ai=["lua/ai/alpha_ai.lua"])
        create_package(self.root, extensions=[declared], files={
            "lua/alpha.lua": ("rules", b"return true\n"),
            "lua/helper.lua": ("rules", b"return 'helper'\n"),
            "translation/alpha.lua": ("presentation", b"return {}\n"),
            "lua/ai/alpha_ai.lua": ("ai", b"return {}\n"),
        })

        [package] = solo._load_packages(self.root / "packages")

        self.assertEqual(package["extensions"][0]["libs"], ["packages/alpha-pack/lua/helper.lua"])
        self.assertEqual(package["extensions"][0]["lang"], ["packages/alpha-pack/translation/alpha.lua"])
        self.assertEqual(package["extensions"][0]["ai"], ["packages/alpha-pack/lua/ai/alpha_ai.lua"])

    def test_prepare_content_stages_v3_package_manifest_and_declared_code(self) -> None:
        source = self.root / "runtime"
        payloads = {
            "lua/config.lua": b"extension_names = {\n}\n",
            "lua/sanguosha.lua": b"core sanguosha\n",
            "lua/utilities.lua": b"core utilities\n",
            "lua/sgs_ex.lua": b"core extensions\n",
            "lua/lib/json.lua": b"core json\n",
        }
        for relative, payload in payloads.items():
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
        create_package(source, extensions=[extension()], files={
            "lua/alpha.lua": ("rules", b"return true\n")})
        destination = self.root / "prepared"

        solo.prepare_content(source, destination)

        self.assertEqual((destination / "lua/config.lua").read_bytes(), payloads["lua/config.lua"])
        self.assertTrue((destination / "packages/alpha-pack/manifest.json").is_file())
        self.assertEqual((destination / "packages/alpha-pack/lua/alpha.lua").read_bytes(),
                         b"return true\n")

    def test_prepare_content_skips_missing_legacy_entry_replaced_by_package(self) -> None:
        source = self.root / "runtime"
        payloads = {
            "lua/config.lua": b'extension_names = { "extensions/alpha.lua" }\n',
            "lua/sanguosha.lua": b"core sanguosha\n",
            "lua/utilities.lua": b"core utilities\n",
            "lua/sgs_ex.lua": b"core extensions\n",
            "lua/lib/json.lua": b"core json\n",
        }
        for relative, payload in payloads.items():
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
        create_package(source, extensions=[extension()], files={
            "lua/alpha.lua": ("rules", b"return true\n")})

        destination = self.root / "prepared-migrated"
        solo.prepare_content(source, destination)

        self.assertTrue((destination / "packages/alpha-pack/lua/alpha.lua").is_file())
        self.assertFalse((destination / "extensions/alpha.lua").exists())

    def test_full_package_preserves_native_v3_catalog_without_legacy_media(self) -> None:
        args = create_full_package_fixture(self.root)

        solo.package(args)

        manifest = json.loads((args.destination / "solo/manifest.json").read_bytes())
        runtime = manifest["content"]["runtime_content"]
        self.assertEqual(runtime["schema_version"], 3)
        self.assertEqual(runtime["packages"][0]["id"], "alpha-pack")
        self.assertEqual(runtime["extensions"][0]["libs"], [
            "packages/alpha-pack/lua/helper.lua"])
        self.assertTrue((args.destination / "packages/alpha-pack/lua/alpha.lua").is_file())
        self.assertTrue((args.destination / "rules/content").is_dir())
        ai = [row for row in manifest["content"]["files"]
              if row["path"] == "packages/alpha-pack/lua/ai/alpha_ai.lua"]
        self.assertEqual(len(ai), 1)
        self.assertEqual(ai[0]["role"], "ai")
        self.assertEqual((args.destination / "rules/content" / ai[0]["sha256"]).read_bytes(), b"return {}\n")
        self.assertFalse(any((args.destination / "assets").rglob("*")))
        self.assertFalse((args.destination / "audio").exists())

    def test_full_package_rejects_v2_bundle_when_local_packages_exist(self) -> None:
        args = create_full_package_fixture(self.root)
        bundle = json.loads(args.rules_bundle.read_bytes())
        bundle["rules_content"]["schema_version"] = 2
        bundle["rules_content"]["profile"] = "declared-v2"
        bundle["rules_content"]["runtime_content"] = {
            "schema_version": 2, "profile": "declared-v2", "extensions": []}
        args.rules_bundle.write_text(json.dumps(bundle), encoding="utf-8")

        with self.assertRaisesRegex(ValueError, "fresh packages-v1 rules bundle"):
            solo.package(args)


if __name__ == "__main__":
    unittest.main()
