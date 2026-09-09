#!/usr/bin/env python3
"""Static gate: config.lua declares exactly the extensions on disk, in order."""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]

_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "extension_manifest_generator", ROOT / "tools/generate-extension-manifest.py")
if _GENERATOR_SPEC is None or _GENERATOR_SPEC.loader is None:
    raise RuntimeError("missing extension manifest generator")
_GENERATOR = importlib.util.module_from_spec(_GENERATOR_SPEC)
_GENERATOR_SPEC.loader.exec_module(_GENERATOR)
presentation_files = _GENERATOR.presentation_files


def declared_entries(config_text: str) -> list[str]:
    block = re.search(r"\n\textension_names = \{\n(.*?)\t\},\n", config_text, re.S)
    if not block:
        raise AssertionError("lua/config.lua has no extension_names block")
    lines = block.group(1).splitlines()
    entries = re.findall(r'^\t\t"([^"]+)",$', block.group(1), re.M)
    if len(entries) != len([line for line in lines if line.strip()]):
        raise AssertionError("extension_names contains a line that is not a quoted entry")
    return entries


def on_disk(root: Path) -> list[str]:
    extensions = root / "extensions"
    if extensions.is_symlink():
        raise AssertionError("extensions/ must not be a symlink")
    if not extensions.is_dir():
        raise AssertionError("extensions/ is missing")
    children = list(extensions.iterdir())
    paths = [path for path in children if path.is_file() and path.name.endswith(".lua")]
    if any(path.is_symlink() for path in children):
        raise AssertionError("extensions/ contains a symlink")
    names = [path.name for path in paths]
    lowered = [name.lower() for name in names]
    if len(set(lowered)) != len(lowered):
        raise AssertionError("extensions/ has names that collide case-insensitively")
    return ["extensions/" + name for name in sorted(names, key=str.lower)]


def check(root: Path) -> None:
    declared = declared_entries((root / "lua/config.lua").read_text(encoding="utf-8"))
    scripts = [entry.split(";", 1)[0] for entry in declared]
    lang = []
    for entry in declared:
        for field in entry.split(";")[1:]:
            if field.startswith("lang="):
                lang.extend(path for path in field[5:].split(",") if path)
    disk = on_disk(root)
    if len(scripts) != len(set(scripts)):
        raise AssertionError("extension_names contains duplicate scripts")
    missing = sorted(set(disk) - set(scripts))
    extra = sorted(set(scripts) - set(disk))
    if missing:
        raise AssertionError("extensions on disk are not declared: " + ", ".join(missing))
    if extra:
        raise AssertionError("declared extensions are missing from disk: " + ", ".join(extra))
    if scripts != disk:
        raise AssertionError("declared order does not match the migrated filename order")
    if len(lang) != len(set(lang)):
        raise AssertionError("extension_names contains duplicate lang paths")
    expected_lang = presentation_files(root)
    if sorted(lang) != expected_lang:
        missing = sorted(set(expected_lang) - set(lang))
        extra = sorted(set(lang) - set(expected_lang))
        detail = []
        if missing: detail.append("missing " + ", ".join(missing))
        if extra: detail.append("undeclared " + ", ".join(extra))
        raise AssertionError("declared lang files do not match disk: " + "; ".join(detail))


class SelfTest(unittest.TestCase):
    @staticmethod
    def config(entries: list[str]) -> str:
        body = "".join(f'\t\t"{entry}",\n' for entry in entries)
        return f"\n\textension_names = {{\n{body}\t}},\n"

    def test_declared_entries_parses_satellites(self):
        text = '\n\textension_names = {\n\t\t"extensions/a.lua",\n\t\t"extensions/b.lua;ai=lua/ai/b-ai.lua",\n\t},\n'
        self.assertEqual(declared_entries(text),
                         ["extensions/a.lua", "extensions/b.lua;ai=lua/ai/b-ai.lua"])

    def test_missing_block_is_an_error(self):
        with self.assertRaisesRegex(AssertionError, "no extension_names block"):
            declared_entries("config = {}\n")

    def test_case_insensitive_collision_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/Ab.lua").write_text("")
            (root / "extensions/aB.lua").write_text("")
            with self.assertRaisesRegex(AssertionError, "collide case-insensitively"):
                on_disk(root)

    def test_symlink_directory_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "real").mkdir()
            (root / "extensions").symlink_to(root / "real", target_is_directory=True)
            with self.assertRaisesRegex(AssertionError, "must not be a symlink"):
                on_disk(root)

    def test_symlink_file_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "real.lua").write_text("")
            (root / "extensions/link.lua").symlink_to(root / "real.lua")
            with self.assertRaisesRegex(AssertionError, "contains a symlink"):
                on_disk(root)

    def test_generator_rejects_case_collision(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/Ab.lua").write_text("")
            (root / "extensions/aB.lua").write_text("")
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/generate-extension-manifest.py"), str(root)],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("collide case-insensitively", result.stderr)

    def test_empty_manifest_matches_empty_directory(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "lua").mkdir()
            (root / "lua/config.lua").write_text(self.config([]))
            check(root)

    def test_presentation_files_rejects_dangling_symlink(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "lang/zh_CN").mkdir(parents=True)
            (root / "lang/zh_CN/missing.lua").symlink_to(root / "gone.lua")
            with self.assertRaisesRegex(ValueError, "contains a symlink"):
                presentation_files(root)

    def test_presentation_files_rejects_symlink_parent(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "real").mkdir()
            (root / "lang").symlink_to(root / "real", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "must not be a symlink"):
                presentation_files(root)

    def test_presentation_files_rejects_symlink_above_asset_root(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "real/assets/lang").mkdir(parents=True)
            (root / "alias").symlink_to(root / "real", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symlinked parents"):
                presentation_files(root / "alias/assets")

    def test_generator_attaches_sorted_language_files_to_first_script(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/a.lua").write_text("")
            (root / "extensions/b.lua").write_text("")
            (root / "lang/zh_CN/Package").mkdir(parents=True)
            (root / "lang/zh_CN/Z.lua").write_text("")
            (root / "lang/zh_CN/A.lua").write_text("")
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/generate-extension-manifest.py"), str(root)],
                capture_output=True, text=True, check=True)
            lines = result.stdout.splitlines()
            self.assertIn("extensions/a.lua;lang=lang/zh_CN/A.lua,lang/zh_CN/Z.lua", lines[1])
            self.assertNotIn(";lang=", lines[2])

    def test_declared_language_set_must_match_disk(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/a.lua").write_text("")
            (root / "lang/zh_CN").mkdir(parents=True)
            (root / "lang/zh_CN/Common.lua").write_text("")
            (root / "lua").mkdir()
            (root / "lua/config.lua").write_text(self.config(["extensions/a.lua"]))
            with self.assertRaisesRegex(AssertionError, "declared lang files"):
                check(root)

    def test_missing_extra_duplicate_and_order_are_errors(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "lua").mkdir()
            for name in ("a.lua", "b.lua"):
                (root / "extensions" / name).write_text("")
            with self.assertRaisesRegex(AssertionError, "not declared"):
                (root / "lua/config.lua").write_text(self.config(["extensions/a.lua"]))
                check(root)
            with self.assertRaisesRegex(AssertionError, "missing from disk"):
                (root / "lua/config.lua").write_text(self.config(["extensions/a.lua", "extensions/b.lua", "extensions/c.lua"]))
                check(root)
            with self.assertRaisesRegex(AssertionError, "duplicate scripts"):
                (root / "lua/config.lua").write_text(self.config(["extensions/a.lua", "extensions/a.lua"]))
                check(root)
            with self.assertRaisesRegex(AssertionError, "order"):
                (root / "lua/config.lua").write_text(self.config(["extensions/b.lua", "extensions/a.lua"]))
                check(root)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.main(argv=[sys.argv[0]], exit=False).result.wasSuccessful() else 1
    check(args.root)
    print("extension manifest: declaration matches extensions/ exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
