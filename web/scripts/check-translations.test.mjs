import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { validateTranslationFile } from "./check-translations.mjs";

test("translation validator rejects malformed and incomplete dumps", () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qsan-translations-"));
  try {
    const file = path.join(root, "translations.json");
    for (const value of [null, {}, { slash: 1 }, { slash: "" }, { slash: "杀", bad: 1 }]) {
      fs.writeFileSync(file, JSON.stringify(value));
      assert.throws(() => validateTranslationFile(file));
    }
    fs.writeFileSync(file, JSON.stringify({ slash: "杀", "__proto__": "原生" }));
    validateTranslationFile(file);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
