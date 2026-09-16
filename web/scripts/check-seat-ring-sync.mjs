#!/usr/bin/env node
// Hold web/src/ui-seat-layout.ts to the shared seat ring table in
// src/client/core/seat-ring-table.h. The web shell cannot include the C++ header, so the
// copy is checked instead of shared; docs/ui-roadmap.md 2.1 requires the seat order to be
// identical across shells, and nothing else enforces that.
//
// The C++ rows are ragged -- regularSeatRegions is declared [][20] and zero-pads every row
// past the opponent count -- so only the first `row index + 1` entries are compared.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(webRoot, "..");
const headerPath = path.join(repoRoot, "src", "client", "core", "seat-ring-table.h");
const seatLayoutPath = path.join(webRoot, "src", "ui-seat-layout.ts");

const errors = [];

function read(file) {
  return fs.readFileSync(file, "utf8");
}

// Strip comments so a "{ 1, 2 }" inside prose never reaches the row parser.
function stripComments(source) {
  return source.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
}

function parseRows(body) {
  const rows = [];
  for (const match of body.matchAll(/\{([^{}]*)\}/g)) {
    const values = match[1].split(",").map((entry) => entry.trim()).filter((entry) => entry !== "");
    rows.push(values.map(Number));
  }
  return rows;
}

function cppTable(source, name) {
  // Rows end with "}," so the first "};" is the end of the table, never a row.
  const declaration = new RegExp(`${name}\\s*(?:\\[[^\\]]*\\])+\\s*=\\s*\\{([\\s\\S]*?)\\s*\\};`);
  const block = source.match(declaration);
  if (!block)
    throw new Error(`seat-ring-table.h: ${name} not found`);
  return parseRows(block[1]);
}

function tsTable(source, name) {
  const declaration = new RegExp(`const ${name}[^=]*=\\s*\\[([\\s\\S]*?)\\s*\\];`);
  const block = source.match(declaration);
  if (!block)
    throw new Error(`ui-seat-layout.ts: ${name} not found`);
  return parseRows(block[1].replace(/\[/g, "{").replace(/\]/g, "}"));
}

function compare(label, cppRows, tsRows) {
  if (cppRows.length !== tsRows.length) {
    errors.push(`${label}: C++ has ${cppRows.length} rows, TypeScript has ${tsRows.length}`);
    return;
  }
  cppRows.forEach((cppRow, index) => {
    const tsRow = tsRows[index];
    // Ragged C++ rows: the TypeScript copy carries only the meaningful prefix.
    const cppTrimmed = cppRow.slice(0, tsRow.length);
    if (cppTrimmed.length !== tsRow.length || cppTrimmed.some((value, i) => value !== tsRow[i])) {
      errors.push(`${label} row ${index} (${index + 1} opponents):`
        + ` C++ [${cppTrimmed.join(", ")}] vs TypeScript [${tsRow.join(", ")}]`);
      return;
    }
    // Everything past the prefix must be the zero padding, not a dropped seat.
    const padding = cppRow.slice(tsRow.length);
    if (padding.some((value) => value !== 0))
      errors.push(`${label} row ${index}: TypeScript drops non-zero C++ entries [${padding.join(", ")}]`);
  });
}

const header = stripComments(read(headerPath));
const seatLayout = stripComments(read(seatLayoutPath));

compare("regular", cppTable(header, "regularSeatRegions"), tsTable(seatLayout, "REGULAR"));
compare("hulao", cppTable(header, "hulaoSeatRegions"), tsTable(seatLayout, "HULAO"));
compare("3v3", cppTable(header, "threeVThreeSeatRegions"), tsTable(seatLayout, "KOF"));

if (errors.length > 0) {
  console.error("seat ring table drift between C++ and the web shell:");
  for (const error of errors)
    console.error(`  ${error}`);
  console.error("\nsrc/client/core/seat-ring-table.h is authoritative; mirror it into"
    + " web/src/ui-seat-layout.ts.");
  process.exit(1);
}

console.log("seat ring tables match src/client/core/seat-ring-table.h");
