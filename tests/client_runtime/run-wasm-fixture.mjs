#!/usr/bin/env node
// Trusted local fixture host, not a game client or an arbitrary-module sandbox.
import { createHash, randomUUID } from 'node:crypto';
import { readFile, realpath, rename, stat, unlink, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';

const LIMIT = 1024 * 1024;
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

export function parseManifest(bytes) {
  const value = JSON.parse(Buffer.from(bytes).toString('utf8'));
  if (value.schema_version !== 1 || value.profile !== 'builtin-v1'
      || !Array.isArray(value.files) || value.files.length === 0) {
    throw new Error('unsupported/empty WASM asset manifest');
  }
  const seen = new Set();
  for (const entry of value.files) {
    if (typeof entry.path !== 'string' || !entry.path.startsWith('lua/')
        || !/^[A-Za-z0-9_./-]+\.lua$/.test(entry.path)
        || entry.path.split('/').some(part => !part || part === '.' || part === '..')
        || seen.has(entry.path) || !/^[0-9a-f]{64}$/.test(entry.sha256)
        || !Number.isSafeInteger(entry.size) || entry.size <= 0) {
      throw new Error('invalid/duplicate WASM asset manifest entry');
    }
    seen.add(entry.path);
  }
  return value;
}

export function verifyEmbeddedAssets(fs, manifestBytes) {
  const manifest = parseManifest(manifestBytes);
  const embedded = Buffer.from(fs.readFile('/assets/fixture-assets.json'));
  if (!embedded.equals(Buffer.from(manifestBytes))) {
    throw new Error('WASM embedded asset manifest differs from its sidecar');
  }
  for (const entry of manifest.files) {
    const bytes = Buffer.from(fs.readFile(`/assets/${entry.path}`));
    if (bytes.length !== entry.size || sha256(bytes) !== entry.sha256) {
      throw new Error(`WASM embedded asset hash mismatch: ${entry.path}`);
    }
  }
}

// factory is injectable solely to test this host without a Qt toolchain. A mock
// passing these tests is not evidence that a real WASM module compiles or runs.
export async function executeFixture(factory, input, manifestBytes, {
  hashSeed, loggingRules, printErr = line => process.stderr.write(`${line}\n`),
  wasmBinary,
} = {}) {
  if (input.length > LIMIT) {
    throw new Error('fixture must be a schema_version=1 JSON object of at most 1 MiB');
  }
  parseManifest(manifestBytes);
  let aborted = false;
  const options = {
    noInitialRun: true,
    noExitRuntime: true,
    ...(wasmBinary ? { wasmBinary } : {}),
    print: printErr,
    printErr,
    onAbort(reason) { aborted = true; printErr(`WASM abort: ${reason}`); },
  };
  // ENV must be set before static Settings Config is constructed. No NODEFS,
  // host home-directory mounts, external extensions, or browser DOM shims.
  options.preInit = [() => {
    if (!options.ENV) throw new Error('WASM module does not export ENV before initialization');
    Object.assign(options.ENV, {
      HOME: '/home/fixture', XDG_CONFIG_HOME: '/home/fixture/config',
      XDG_DATA_HOME: '/home/fixture/data', TMPDIR: '/tmp',
    });
    if (hashSeed === '0') options.ENV.QT_HASH_SEED = '0';
    else delete options.ENV.QT_HASH_SEED;
    if (loggingRules !== undefined) options.ENV.QT_LOGGING_RULES = loggingRules;
  }];
  options.preRun = [module => {
    const fs = (module || options).FS;
    if (!fs) throw new Error('WASM module does not export FS');
    verifyEmbeddedAssets(fs, manifestBytes);
    fs.mkdirTree('/work');
    fs.mkdirTree('/home/fixture/config');
    fs.mkdirTree('/home/fixture/data');
    fs.mkdirTree('/tmp');
    fs.chdir('/work');
    fs.writeFile('/work/input.json', input);
  }];
  const module = await factory(options);
  if (aborted || typeof module._qsan_run_fixture !== 'function' || !module.FS) {
    throw new Error('WASM initialization failed or fixture export is missing');
  }
  const status = module._qsan_run_fixture();
  if (aborted || !Number.isInteger(status) || status !== 0) {
    throw new Error(`WASM fixture exit ${status}`);
  }
  const result = Buffer.from(module.FS.readFile('/work/output.json'));
  const parsed = JSON.parse(result.toString('utf8'));
  if (!parsed || Array.isArray(parsed) || parsed.schema_version !== 1) {
    throw new Error('WASM fixture returned no valid result object');
  }
  // Preserve C++ canonical bytes. In particular, do not coerce uint64 strings,
  // sort arrays, drop fields, or normalize away a registry difference.
  return result;
}

export async function main(argv = process.argv.slice(2)) {
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
