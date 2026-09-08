// No fake WASM/engine: these are pure contract tests of the actual TypeScript module.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

if (process.argv.length !== 4) throw new Error('usage: node check.mjs <compiled rules-identity.js> <vectors.json>');
const { compareRulesIdentity } = await import(pathToFileURL(resolve(process.argv[2])).href);
const vectors = JSON.parse(await readFile(process.argv[3], 'utf8'));
assert.equal(vectors.schema_version, 1);
assert.ok(vectors.cases.length > 0);
for (const item of vectors.cases) {
  const before = JSON.stringify(item);
  assert.deepEqual(compareRulesIdentity(item.local, item.server), item.expect, item.name);
  assert.equal(JSON.stringify(item), before, `${item.name}: comparison mutated the input`);
}
console.log(`RULES_IDENTITY_CONTRACT PASS cases=${vectors.cases.length}`);
