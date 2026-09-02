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
const dumpHint = "debug\\qsanguosha_tui.exe --dump-translations web\\public\\translations.json";

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

if (!fs.existsSync(translations)) {
  console.error(`translations: missing ${path.relative(repoRoot, translations)}`);
  console.error(`translations: dump with ${dumpHint}`);
  process.exit(1);
}

const luaFiles = walkLua(langRoot);
if (luaFiles.length === 0) {
  console.error(`translations: no lua files under ${path.relative(repoRoot, langRoot)}`);
  process.exit(1);
}

const dumpMtime = fs.statSync(translations).mtimeMs;
let newestLua = luaFiles[0];
let newestMtime = fs.statSync(newestLua).mtimeMs;
for (const file of luaFiles) {
  const mtime = fs.statSync(file).mtimeMs;
  if (mtime > newestMtime) {
    newestLua = file;
    newestMtime = mtime;
  }
}

if (newestMtime > dumpMtime) {
  console.error(`translations: ${path.relative(repoRoot, translations)} is older than ${path.relative(repoRoot, newestLua)}`);
  console.error(`translations: re-dump with ${dumpHint}`);
  process.exit(1);
}

console.log(`translations: ok (dump newer than ${luaFiles.length} zh_CN lua files)`);
