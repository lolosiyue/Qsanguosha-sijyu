#!/usr/bin/env node
// Narrow batch-A gate: inspect only the UI files being migrated in this batch.
// This is deliberately lexical; it does not attempt to parse C++, QML, VBA or Lua.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(webRoot, "..");

export const UI_FILES = [
  "src/tui/tui-application-controller.cpp",
  "src/tui/tui-board-view.cpp",
  "web/src/ui-interaction.ts",
  "src/ui/room-replay-controller.cpp",
  "src/ui/clientlogbox.cpp",
  "ui-script/game/chongxu/chongxu.qml",
  "excel/vba/QsanUi.bas",
  "google-sheets/apps-script/Client.gs"
];

// These are wire/input tokens, not user-facing prose. Keep this list exact.
const protocolAliases = new Set(["過", "过", "確定", "确定", "不出"]);
const replayDatePattern = "yyyy年MM月dd日HH时mm分ss秒";
const worksheetNames = new Set(["首頁", "房間設定", "牌桌", "詳情", "戰報"]);

function hasCjk(value) { return /[\u3400-\u9fff]/u.test(value); }
function decodeString(value) { return value.replace(/\\n/g, "\n").replace(/\\r/g, "\r").replace(/\\t/g, "\t").replace(/\\\\/g, "\\"); }

function scanStrings(text, { vba = false } = {}) {
  const result = [];
  let i = 0; let line = 1; let state = "code";
  while (i < text.length) {
    const c = text[i]; const n = text[i + 1];
    if (state === "line") { if (c === "\n") { state = "code"; line++; } i++; continue; }
    if (state === "block") { if (c === "*" && n === "/") { state = "code"; i += 2; } else { if (c === "\n") line++; i++; } continue; }
    if (c === "\n") { line++; i++; continue; }
    if (state === "code") {
      if (c === "/" && n === "/") { state = "line"; i += 2; continue; }
      if (c === "/" && n === "*") { state = "block"; i += 2; continue; }
      // VBA comments begin with apostrophe only outside a quoted string.
      if (vba && c === "'") { state = "line"; i++; continue; }
      if (c !== "'" && c !== '"' && c !== "`") { i++; continue; }
      const quote = c; const start = i; const startLine = line; let value = ""; i++;
      while (i < text.length) {
        if (text[i] === "\n") line++;
        if (text[i] === "\\" && i + 1 < text.length) { value += text[i] + text[i + 1]; i += 2; continue; }
        if (text[i] === quote) {
          // VBA escapes a double quote by doubling it.
          if (vba && quote === '"' && text[i + 1] === '"') { value += '"'; i += 2; continue; }
          i++; break;
        }
        value += text[i++];
      }
      result.push({ value, offset: start, line: startLine, lineText: text.slice(text.lastIndexOf("\n", start) + 1, text.indexOf("\n", start) < 0 ? text.length : text.indexOf("\n", start)) });
    }
  }
  return result;
}

function literals(text, file = "") { return scanStrings(text, { vba: file.endsWith(".bas") }).filter((item) => hasCjk(item.value)); }

function allowedLiteral(file, item, source) {
  if (file === "src/tui/tui-application-controller.cpp" && protocolAliases.has(item.value)) return true;
  if (file === "src/ui/room-replay-controller.cpp" && item.value === replayDatePattern) return true;
  if (file === "google-sheets/apps-script/Client.gs"
      && new Set(["保留", "布林", "整數", "清單"]).has(item.value)
      && /\btype\s*===\s*['"]/u.test(item.lineText)) return true;
  if (file === "excel/vba/QsanUi.bas" && worksheetNames.has(item.value)) {
    // Worksheet identity is a stable API. Only permit it in identity positions.
    const line = item.lineText;
    return new RegExp(`GetSheet\\s*\\(\\s*["']${item.value}["']\\s*\\)`, "u").test(line)
      || line.includes("names = Array")
      || new RegExp(`QsanUi_Button\\s+["']${item.value}["']\\s*,`, "u").test(line)
      || new RegExp(`QsanUi_Bank.*["']${item.value}["']`, "u").test(line)
      || new RegExp(`Case\\s+["']${item.value}["']\\s*:`, "iu").test(line)
      || new RegExp(`Optional\\s+sheetName\\s+As\\s+String\\s*=\\s*["']${item.value}["']`, "iu").test(line);
  }
  return false;
}

export function findHardcodedUiText({ root = repoRoot, files = UI_FILES } = {}) {
  const findings = [];
  for (const relative of files) {
    const file = path.join(root, relative);
    if (!fs.existsSync(file)) continue;
    const source = fs.readFileSync(file, "utf8");
    for (const item of literals(source, relative)) {
      if (!allowedLiteral(relative, item, source))
        findings.push({ file: relative, line: item.line, text: item.value });
    }
  }
  return findings;
}

function xmlSources(file) {
  if (!fs.existsSync(file)) return new Map();
  const map = new Map();
  const text = fs.readFileSync(file, "utf8");
  const decode = (value) => value.replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&apos;/g, "'").replace(/&quot;/g, '"');
  const re = /<context>[\s\S]*?<name>([^<]+)<\/name>([\s\S]*?)(?=<\/context>)/g;
  let m;
  while ((m = re.exec(text))) {
    for (const message of m[2].matchAll(/<message>([\s\S]*?)<\/message>/g)) {
      const source = message[1].match(/<source>([\s\S]*?)<\/source>/)?.[1];
      const translation = message[1].match(/<translation([^>]*)>([\s\S]*?)<\/translation>/);
      if (!source || !translation || /\b(?:vanished|unfinished|obsolete)\b/u.test(translation[1])) continue;
      const value = decode(translation[2]).trim();
      if (value) {
        const key = decode(source);
        const entries = map.get(key) ?? [];
        entries.push({ context: m[1].trim(), value });
        map.set(key, entries);
      }
    }
  }
  return map;
}

function placeholders(value) { return [...value.matchAll(/%(?:\d+|from|to|arg\d*)/gu)].map((m) => m[0]).sort().join(" "); }
function expectedContext(file, source, offset) {
  if (file.endsWith("room-replay-controller.cpp")) {
    // This source contains two QObject classes. tr() uses the enclosing class,
    // not the filename and not a class mentioned in a signal connection.
    const definitions = [...source.slice(0, offset).matchAll(
      /^(?:[\w:<>,*&]+\s+)*(ReplayerControlBar|RoomReplayController)::[~\w]+\s*\(/gm)];
    return definitions.at(-1)?.[1] ?? null;
  }
  if (file.endsWith("clientlogbox.cpp")) return "ClientLogBox";
  if (file.endsWith("chongxu.qml")) return "chongxu";
  return null;
}

function referencedQtKeys(root) {
  const keys = [];
  for (const relative of UI_FILES.filter((f) => f.endsWith(".cpp") || f.endsWith(".qml"))) {
    const file = path.join(root, relative);
    if (!fs.existsSync(file)) continue;
    const source = fs.readFileSync(file, "utf8");
    for (const token of scanStrings(source)) {
      const prefix = source.slice(Math.max(0, token.offset - 20), token.offset);
      if (/(?:\btr|\bqsTr)\s*\(\s*$/u.test(prefix))
        keys.push({ file: relative, key: decodeString(token.value),
          context: expectedContext(relative, source, token.offset) });
    }
  }
  return keys;
}

export function checkTranslationResources({ root = repoRoot } = {}) {
  const catalog = xmlSources(path.join(root, "builds", "sanguosha.ts"));
  const qtKeys = referencedQtKeys(root);
  const missingQt = qtKeys.filter(({ key, context }) => {
    const entries = catalog.get(key) ?? [];
    return !context || !entries.some((entry) => entry.context === context && entry.value.trim());
  }).map(({ key, context }) => `${context ?? "unknown context"}: ${key}`);
  const placeholderErrors = qtKeys.flatMap(({ key, context }) => (catalog.get(key) ?? [])
    .filter(entry => entry.context === context && placeholders(key) !== placeholders(entry.value))
    .map(() => `${context}: ${key}`));
  // Check the tracked Lua source maps. translations.json is a generated dump
  // and is intentionally left stale until the build checkpoint is authorized.
  const luaKeys = new Set();
  for (const relative of ["lang/zh_CN/Common.lua", "lang/zh_CN/TUICommon.lua"]) {
    const file = path.join(root, relative);
    if (!fs.existsSync(file)) continue;
    for (const match of fs.readFileSync(file, "utf8").matchAll(/\["([^"\n]+)"\]\s*=/g)) luaKeys.add(match[1]);
  }
  const missingWeb = [];
  const missingTui = [];
  for (const relative of ["src/tui/tui-application-controller.cpp", "src/tui/tui-board-view.cpp"]) {
    const text = fs.readFileSync(path.join(root, relative), "utf8");
    for (const token of scanStrings(text)) if (/^tui_/u.test(token.value) && !luaKeys.has(token.value)) missingTui.push(token.value);
  }
  const webFiles = ["web/src/ui-interaction.ts"];
  for (const relative of webFiles) {
    const text = fs.readFileSync(path.join(root, relative), "utf8");
    for (const token of scanStrings(text)) if (/^web\./u.test(token.value) && !token.value.includes("${") && !luaKeys.has(token.value)) missingWeb.push(token.value);
  }
  const missingOffice = [];
  // The bridge declares the exported suffixes in a bounded list. Check both
  // the authoritative Lua translations and VBA's mapping into that list.
  const viewSource = fs.readFileSync(path.join(root, "src/excel/excel-view.cpp"), "utf8");
  const keyList = viewSource.match(/QJsonObject uiPhrases\(\)[\s\S]*?keys\s*=\s*\{([\s\S]*?)\};/u)?.[1];
  if (!keyList) missingOffice.push("native uiPhrases key list");
  const bridgeKeys = new Set(scanStrings(keyList ?? "").map(token => token.value));
  for (const key of bridgeKeys) if (!luaKeys.has(`qsan_ui_${key}`)) missingOffice.push(`qsan_ui_${key}`);
  const vba = fs.readFileSync(path.join(root, "excel/vba/QsanUi.bas"), "utf8");
  for (const match of vba.matchAll(/Case\s+"[^"]+":\s*QsanUi_BridgePhraseKey\s*=\s*"([^"]+)"/gu))
    if (!bridgeKeys.has(match[1])) missingOffice.push(`bridge: ${match[1]}`);
  const locale = fs.readFileSync(path.join(root, "google-sheets/apps-script/Locale.gs"), "utf8");
  const sheetKeys = new Set([...locale.matchAll(/^\s*([A-Za-z]\w*)\s*:/gm)].map(match => match[1]));
  const sheetClient = fs.readFileSync(path.join(root, "google-sheets/apps-script/Client.gs"), "utf8");
  for (const token of scanStrings(sheetClient)) {
    if (/\bqsanText_\s*\(\s*$/u.test(sheetClient.slice(Math.max(0, token.offset - 24), token.offset))
        && !sheetKeys.has(token.value)) missingOffice.push(`Sheets: ${token.value}`);
  }
  return { missingQt: [...new Set(missingQt)], missingWeb: [...new Set(missingWeb)], missingTui: [...new Set(missingTui)],
    missingOffice: [...new Set(missingOffice)], placeholderErrors: [...new Set(placeholderErrors)] };
}

export function checkUiLocalization(options = {}) {
  const hardcoded = findHardcodedUiText(options);
  const resources = checkTranslationResources(options);
  if (hardcoded.length || resources.missingQt.length || resources.missingWeb.length || resources.missingTui.length || resources.missingOffice.length || resources.placeholderErrors.length) {
    const lines = ["UI localization gate failed:"];
    for (const item of hardcoded) lines.push(`  ${item.file}:${item.line}: hardcoded CJK literal ${JSON.stringify(item.text)}`);
    for (const key of resources.missingQt) lines.push(`  missing Qt catalog key: ${key}`);
    for (const key of resources.missingWeb) lines.push(`  missing web translation key: ${key}`);
    for (const key of resources.missingTui) lines.push(`  missing TUI translation key: ${key}`);
    for (const key of resources.missingOffice) lines.push(`  missing Office translation key: ${key}`);
    for (const key of resources.placeholderErrors) lines.push(`  placeholder mismatch in Qt catalog key: ${key}`);
    throw new Error(lines.join("\n"));
  }
  return resources;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url))) {
  try { checkUiLocalization(); console.log("ui localization: ok"); }
  catch (error) { console.error(error.message); process.exit(1); }
}
