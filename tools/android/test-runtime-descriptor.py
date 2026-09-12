"""APK declaration coverage regressions; does not execute external Lua."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("descriptor", Path(__file__).with_name("create-runtime-descriptor.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.entries = module.entries('extension_names = {\n"extensions/base.lua",\n},')
        self.files = {"lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua",
                      "lua/sgs_ex.lua", "lua/lib/json.lua", "extensions/base.lua"}

    def test_support_is_hashed_ai_stays_outside(self):
        module.include_bundled_support(self.entries, self.files | {
            "lua/chat_config.lua", "lua/lib/sqlite3.lua", "lua/ai/smart-ai.lua",
            "lua/lib/middleclass.lua", "lang/zh_CN/shared.lua"})
        self.assertEqual(self.entries[0]["libs"], ["lua/chat_config.lua", "lua/lib/sqlite3.lua"])
        self.assertEqual(self.entries[0]["lang"], ["lang/zh_CN/shared.lua"])
        self.assertEqual(self.entries[0]["ai"], [])

    def test_missing_core_or_declared_file_rejected(self):
        for missing in ("lua/config.lua", "extensions/base.lua"):
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                module.include_bundled_support(copy.deepcopy(self.entries), self.files - {missing})

    def test_unlisted_extension_is_not_silently_enabled(self):
        with self.assertRaises(ValueError):
            module.include_bundled_support(self.entries, self.files | {"extensions/unknown.lua"})

    def test_path_traversal_rejected(self):
        with self.assertRaises(ValueError):
            module.include_bundled_support(self.entries, self.files | {"lua/../bad.lua"})

    def test_repeated_coverage_preserves_order_and_no_duplicates(self):
        module.include_bundled_support(self.entries, self.files | {"lua/chat_config.lua"})
        once = copy.deepcopy(self.entries)
        module.include_bundled_support(self.entries, self.files | {"lua/chat_config.lua"})
        self.assertEqual(self.entries, once)


if __name__ == "__main__":
    unittest.main()
