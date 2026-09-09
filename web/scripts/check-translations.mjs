#!/usr/bin/env node
// Fail if web/public/translations.json is missing or older than lang/zh_CN/*.lua.
// Does not run qsanguosha_tui --dump-translations.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(webRoot, "..");
const translations = path.join(webRoot, "public", "translations.json");
const langRoot = path.join(repoRoot, "lang", "zh_CN");
const dumpHint = "./relwithdebinfo/qsanguosha_tui --dump-translations web/public/translations.json";

function walkLua(dir, files = []) {
  if (!fs.existsSync(dir))
    return files;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory())
      walkLua(full, files);
    else if (entry.name.endsWith(".lua"))
      files.push(full);
  }
  return files;
}

export function validateTranslationFile(file) {
  let value;
  try { value = JSON.parse(fs.readFileSync(file, "utf8")); }
  catch { throw new Error("invalid JSON"); }
  if (value === null || typeof value !== "object" || Array.isArray(value))
    throw new Error("expected a JSON object");
  const entries = Object.entries(value);
  if (entries.length === 0)
    throw new Error("translation map is empty");
  if (typeof value.slash !== "string" || value.slash.length === 0)
    throw new Error("required key slash is missing");
  if (entries.some(([key, text]) => !key || typeof text !== "string"))
    throw new Error("translation map must contain strings");
}

export function checkTranslations({ translationFile = translations, languageRoot = langRoot } = {}) {
  if (!fs.existsSync(translationFile))
    throw new Error(`missing ${path.relative(repoRoot, translationFile)}`);
  validateTranslationFile(translationFile);
  const luaFiles = walkLua(languageRoot);
  if (luaFiles.length === 0)
    throw new Error(`no lua files under ${path.relative(repoRoot, languageRoot)}`);

  const dumpMtime = fs.statSync(translationFile).mtimeMs;
  let newestLua = luaFiles[0];
  let newestMtime = fs.statSync(newestLua).mtimeMs;
  for (const file of luaFiles) {
    const mtime = fs.statSync(file).mtimeMs;
    if (mtime > newestMtime) {
      newestLua = file;
      newestMtime = mtime;
    }
  }

  if (newestMtime > dumpMtime)
    throw new Error(`${path.relative(repoRoot, translationFile)} is older than ${path.relative(repoRoot, newestLua)}`);
  return luaFiles.length;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url))) {
  try {
    const count = checkTranslations();
    console.log(`translations: ok (dump newer than ${count} zh_CN lua files)`);
  } catch (error) {
    console.error(`translations: ${error.message}`);
    console.error(`translations: dump with ${dumpHint}`);
    process.exit(1);
  }
}
