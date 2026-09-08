#!/usr/bin/env node
// Trusted local fixture host, not a game client or an arbitrary-module sandbox.
import { createHash, randomUUID } from 'node:crypto';
import { readFile, realpath, rename, stat, unlink, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';
import { setTimeout, clearTimeout } from 'node:timers';
import { INPUT_LIMIT as LIMIT, embeddedAssetFiles, executeFixture as executeHostedFixture,
  parseManifest } from './wasm-fixture-host.mjs';

export { parseManifest };
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

// Qt 6.11.1 QWasmTimer names window directly and expects numeric timer IDs.
// Forward only that timer contract to Node; DOM/storage APIs stay absent.
export function createQtTimerHost() {
  return Object.freeze({
    setTimeout(callback, delay) { return Number(setTimeout(callback, delay)); },
    clearTimeout(id) { clearTimeout(id); },
  });
}

// Retain the Node adapter's synchronous public contract for existing callers.
export function verifyEmbeddedAssets(fs, manifestBytes) {
  for (const entry of embeddedAssetFiles(fs, manifestBytes)) {
    if (sha256(entry.bytes) !== entry.sha256) {
      throw new Error(`WASM embedded asset hash mismatch: ${entry.path}`);
    }
  }
}

export async function executeFixture(factory, input, manifestBytes, {
  printErr = line => process.stderr.write(`${line}\n`), ...options
} = {}) {
  return Buffer.from(await executeHostedFixture(factory, input, manifestBytes, {
    ...options, printErr, hashBytes: sha256,
  }));
}

export async function main(argv = process.argv.slice(2)) {
  if (!Array.isArray(globalThis.navigator?.languages)) {
    throw new Error('Node 22+ with built-in navigator.languages is required by Qt WASM');
  }
  const { values } = parseArgs({ args: argv, options: {
    module: { type: 'string' }, manifest: { type: 'string' },
    fixture: { type: 'string' }, output: { type: 'string' },
  }, strict: true, allowPositionals: false });
  for (const key of ['module', 'manifest', 'fixture', 'output']) {
    if (!values[key]) throw new Error(`--${key} is required`);
  }
  const modulePath = await realpath(values.module);
  const fixturePath = await realpath(values.fixture);
  const manifestPath = await realpath(values.manifest);
  const wasmPath = modulePath.replace(/\.mjs$/, '.wasm');
  if (wasmPath === modulePath) throw new Error('--module must name the generated .mjs');
  const output = path.join(await realpath(path.dirname(path.resolve(values.output))),
    path.basename(values.output));
  let existingOutput;
  try { existingOutput = await realpath(output); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  if ([modulePath, fixturePath, manifestPath, wasmPath].includes(existingOutput || output)) {
    throw new Error('output must not overwrite fixture/module/manifest inputs');
  }
  if ((await stat(fixturePath)).size > LIMIT) {
    throw new Error('fixture must be a schema_version=1 JSON object of at most 1 MiB');
  }
  const [input, manifestBytes, wasmBinary] = await Promise.all([
    readFile(fixturePath), readFile(manifestPath), readFile(wasmPath),
  ]);
  if (!wasmBinary.subarray(0, 8).equals(Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]))) {
    throw new Error('missing/invalid WebAssembly binary');
  }
  const { default: factory } = await import(pathToFileURL(modulePath).href);
  if (typeof factory !== 'function') throw new Error('module must export an Emscripten factory');
  if (globalThis.window !== undefined) throw new Error('fixture host requires an isolated Node process');
  globalThis.window = createQtTimerHost();
  const result = await executeFixture(factory, input, manifestBytes, {
    wasmBinary, hashSeed: process.env.QT_HASH_SEED, loggingRules: process.env.QT_LOGGING_RULES,
  });
  const temporary = `${output}.${randomUUID()}.tmp`;
  try {
    await writeFile(temporary, result, { flag: 'wx' });
    await rename(temporary, output);
  } finally {
    await unlink(temporary).catch(error => { if (error.code !== 'ENOENT') throw error; });
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main().catch(error => {
    process.stderr.write(`WASM fixture host failed: ${error.stack || error}\n`);
    process.exitCode = 1;
  });
}
