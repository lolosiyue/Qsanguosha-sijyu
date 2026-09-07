// Shared Node/browser fixture hosting. No Node APIs, DOM, gameplay, or wire rules.
export const INPUT_LIMIT = 1024 * 1024;
export const OUTPUT_LIMIT = 8 * 1024 * 1024;
const decoder = new TextDecoder('utf-8', { fatal: true });

export function equalBytes(left, right) {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

export function parseManifest(bytes) {
  const value = JSON.parse(decoder.decode(bytes));
  if (!value || value.schema_version !== 1 || value.profile !== 'builtin-v1'
      || !Array.isArray(value.files) || value.files.length === 0) {
    throw new Error('unsupported/empty WASM asset manifest');
  }
  const seen = new Set();
  for (const entry of value.files) {
    if (!entry || typeof entry.path !== 'string' || !entry.path.startsWith('lua/')
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

// Validate inventory synchronously; hosts supply their real SHA-256 implementation.
export function embeddedAssetFiles(fs, manifestBytes) {
  const manifest = parseManifest(manifestBytes);
  if (!equalBytes(fs.readFile('/assets/fixture-assets.json'), manifestBytes)) {
    throw new Error('WASM embedded asset manifest differs from its sidecar');
  }
  const files = [];
  function inventory(directory) {
    for (const name of fs.readdir(directory)) {
      if (name === '.' || name === '..') continue;
      const file = `${directory}/${name}`;
      const mode = fs.lstat(file).mode;
      if (fs.isDir(mode)) inventory(file);
      else if (fs.isFile(mode)) files.push(file.slice('/assets/'.length));
      else throw new Error(`unexpected WASM embedded asset type: ${file}`);
    }
  }
  inventory('/assets');
  const expected = ['fixture-assets.json', ...manifest.files.map(entry => entry.path)].sort();
  if (JSON.stringify(files.sort()) !== JSON.stringify(expected)) {
    throw new Error('WASM embedded asset inventory differs from its sidecar');
  }
  return manifest.files.map(entry => {
    const bytes = new Uint8Array(fs.readFile(`/assets/${entry.path}`));
    if (bytes.length !== entry.size) {
      throw new Error(`WASM embedded asset hash mismatch: ${entry.path}`);
    }
    return { ...entry, bytes };
  });
}

// A passing injected factory test is hosting evidence, not native-rule execution.
export async function executeFixture(factory, input, manifestBytes, {
  hashBytes, hashSeed, loggingRules, printErr = () => {}, wasmBinary,
} = {}) {
  if (!(input instanceof Uint8Array) || input.length > INPUT_LIMIT) {
    throw new Error('fixture must be a schema_version=1 JSON object of at most 1 MiB');
  }
  parseManifest(manifestBytes);
  if (typeof hashBytes !== 'function') throw new Error('SHA-256 implementation is required');
  let aborted = false;
  const options = {
    noInitialRun: true, noExitRuntime: true,
    ...(wasmBinary ? { wasmBinary } : {}),
    print: printErr, printErr,
    onAbort(reason) { aborted = true; printErr(`WASM abort: ${reason}`); },
  };
  // Settings Config is static: set ENV before native constructors, not in main.
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
    for (const directory of ['/work', '/home/fixture/config', '/home/fixture/data', '/tmp']) {
      fs.mkdirTree(directory);
    }
    fs.chdir('/work');
    fs.writeFile('/work/input.json', input);
  }];
  const module = await factory(options);
  if (aborted || typeof module._qsan_run_fixture !== 'function' || !module.FS) {
    throw new Error('WASM initialization failed or fixture export is missing');
  }
  // Emscripten installs embedded data AFTER preRun. Verify before EngineBootstrap.
  for (const entry of embeddedAssetFiles(module.FS, manifestBytes)) {
    if (await hashBytes(entry.bytes) !== entry.sha256) {
      throw new Error(`WASM embedded asset hash mismatch: ${entry.path}`);
    }
  }
  if (aborted) throw new Error('WASM initialization aborted');
  const status = module._qsan_run_fixture();
  if (aborted || !Number.isInteger(status) || status !== 0) {
    throw new Error(`WASM fixture exit ${status}`);
  }
  // Own the copied bytes, not a view into live/growable WASM memory.
  const result = new Uint8Array(module.FS.readFile('/work/output.json'));
  if (result.length > OUTPUT_LIMIT) throw new Error('WASM fixture output exceeds 8 MiB');
  const parsed = JSON.parse(decoder.decode(result));
  if (!parsed || Array.isArray(parsed) || parsed.schema_version !== 1) {
    throw new Error('WASM fixture returned no valid result object');
  }
  return result; // Never reserialize JSON or normalize registry/IDs/array order.
}
