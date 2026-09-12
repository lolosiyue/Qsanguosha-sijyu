// Test host for the REAL production C ABI; no snapshot projector/gameplay code.
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
let used = false;
const logs = [];
const log = value => { logs.push(String(value).slice(0, 2048)); if (logs.length > 32) logs.shift(); };
const hash = async bytes => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
  .map(value => value.toString(16).padStart(2, '0')).join('');
function require(condition, message) { if (!condition) throw new Error(message); }
async function bytes(path) {
  const response = await fetch(new URL(path, import.meta.url), { cache: 'no-store', redirect: 'error', credentials: 'omit' });
  require(response.ok, `ingress test asset HTTP ${response.status}`);
  return new Uint8Array(await response.arrayBuffer());
}
self.onmessage = async event => {
  if (used) return;
  used = true;
  let url;
  try {
    require(typeof document === 'undefined' && typeof self.close === 'function'
      && self.window === undefined, 'not an isolated Dedicated Worker');
    const plan = event.data;
    require(Array.isArray(plan.operations) && plan.operations.length > 0, 'missing stream corpus');
    const [javascript, wasmBinary] = await Promise.all([
      bytes('./qsanguosha_client_wasm.mjs'), bytes('./qsanguosha_client_wasm.wasm')
    ]);
    for (const [name, content] of [['module', javascript], ['binary', wasmBinary]]) {
      require(await hash(content) === plan.hashes[name], `test deployment hash mismatch: ${name}`);
    }
    // Compile the exact bytes checked by this harness, not a second HTTP fetch.
    url = URL.createObjectURL(new Blob([javascript], { type: 'text/javascript' }));
    const { default: factory } = await import(url);
    require(typeof factory === 'function', 'missing Emscripten factory');
    self.window = Object.freeze({ setTimeout: self.setTimeout.bind(self), clearTimeout: self.clearTimeout.bind(self) });
    let aborted = false;
    const options = { noInitialRun: true, noExitRuntime: true, wasmBinary,
      print: log, printErr: log, onAbort(reason) { aborted = true; log(reason); },
      locateFile(name) { require(name.endsWith('.wasm'), 'unexpected module dependency');
        return new URL('./qsanguosha_client_wasm.wasm', import.meta.url).href; },
    };
    options.preInit = [() => {
      require(options.ENV, 'missing ENV');
      Object.assign(options.ENV, { HOME: '/userdata', XDG_CONFIG_HOME: '/userdata/config',
        XDG_DATA_HOME: '/userdata/data', TMPDIR: '/tmp' });
      delete options.ENV.QT_HASH_SEED;
    }];
    options.preRun = [module => {
      const fs = (module || options).FS;
      for (const path of ['/work', '/userdata/config', '/userdata/data', '/tmp']) fs.mkdirTree(path);
      fs.chdir('/work');
    }];
    const module = await factory(options);
    require(!aborted && typeof module._qsan_client_stream === 'function', 'stream export missing/initialization aborted');
    const content = plan.content;
    require(content?.schema_version === 2 && content.profile === 'declared-v2'
      && content.runtime_content?.schema_version === 2
      && content.runtime_content.profile === 'declared-v2'
      && Array.isArray(content.files), 'missing declared content manifest');
    module.FS.mkdirTree('/assets');
    for (const entry of content.files) {
      if (entry.role === 'ai') continue;
      const response = await fetch('/rules/content/' + entry.sha256, { cache: 'force-cache', redirect: 'error' });
      require(response.ok && !response.redirected, 'content download failed: ' + entry.path);
      const contentBytes = new Uint8Array(await response.arrayBuffer());
      require(contentBytes.length === entry.size && await hash(contentBytes) === entry.sha256,
        'declared rules bytes differ: ' + entry.path);
      const path = '/assets/' + entry.path;
      module.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      module.FS.writeFile(path, contentBytes);
    }
    module.FS.writeFile('/assets/runtime-content.json',
      new TextEncoder().encode(JSON.stringify(content.runtime_content)));
    const initialized = module._qsan_client_initialize();
    if (initialized !== 0) {
      let reason = '';
      try { reason = decoder.decode(module.FS.readFile('/work/init.json')); } catch {}
      require(false, 'native initialize failed: ' + reason);
    }
    const registry = JSON.parse(decoder.decode(module.FS.readFile('/work/init.json')));
    const records = [];
    for (const { label, operation } of plan.operations) {
      module.FS.writeFile('/work/stream.json', encoder.encode(JSON.stringify(operation)));
      require(module._qsan_client_stream() === 0 && !aborted, `stream ABI failed: ${label}`);
      const output = new Uint8Array(module.FS.readFile('/work/stream-result.json'));
      require(output.length > 0 && output.length <= 8 * 1024 * 1024, 'output length invalid');
      records.push({ label, response_utf8: decoder.decode(output) });
    }
    require(module._qsan_client_evaluate() === 2, 'external snapshots still accepted');
    require(module._qsan_client_shutdown() === 0 && module._qsan_client_shutdown() === 0 && !aborted,
      'native shutdown failed');
    let remains = false;
    try { module.FS.readFile('/work/stream-result.json'); remains = true; } catch { /* expected missing file */ }
    require(!remains, 'stale output after shutdown');
    require(module._qsan_client_stream() === 3, 'closed host accepted stream operations');
    self.postMessage({ registry, records, disposed: true, dedicatedWorker: true, documentAbsent: true });
  } catch (error) {
    self.postMessage({ error: String(error?.stack || error), logs });
  } finally {
    if (url) URL.revokeObjectURL(url);
    self.close();
  }
};
