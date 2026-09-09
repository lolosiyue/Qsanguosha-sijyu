// Snapshot-only parity harness. Production Worker uses stream ingress; this
// file deliberately exercises the legacy C ABI for ClientRulesSession parity.
const enc = new TextEncoder(), dec = new TextDecoder('utf-8', { fatal: true });
const hash = async bytes => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
  .map(value => value.toString(16).padStart(2, '0')).join('');
const fail = message => { throw new Error(message); };
async function get(path) {
  const response = await fetch(path, { cache: 'no-store', redirect: 'error', credentials: 'omit' });
  if (!response.ok) fail(`snapshot asset HTTP ${response.status}: ${path}`);
  return new Uint8Array(await response.arrayBuffer());
}
self.onmessage = async ({ data }) => {
  let url;
  try {
    if (typeof document !== 'undefined' || typeof self.close !== 'function' || self.window !== undefined)
      fail('snapshot harness is not an isolated Worker');
    const [javascript, wasmBinary] = await Promise.all([get('/rules/qsanguosha_client_wasm.mjs'),
      get('/rules/qsanguosha_client_wasm.wasm')]);
    url = URL.createObjectURL(new Blob([javascript], { type: 'text/javascript' }));
    const { default: factory } = await import(url);
    if (typeof factory !== 'function') fail('missing Emscripten factory');
    const options = { noInitialRun: true, noExitRuntime: true, wasmBinary,
      print() {}, printErr() {}, locateFile: name => { if (!name.endsWith('.wasm')) fail('unexpected dependency'); return '/rules/qsanguosha_client_wasm.wasm'; } };
    options.preInit = [() => { Object.assign(options.ENV, { HOME: '/userdata', XDG_CONFIG_HOME: '/userdata/config', XDG_DATA_HOME: '/userdata/data', TMPDIR: '/tmp' }); }];
    options.preRun = [module => { const fs = (module || options).FS; for (const path of ['/work', '/userdata/config', '/userdata/data', '/tmp']) fs.mkdirTree(path); fs.chdir('/work'); }];
    const module = await factory(options);
    // Qt's WASM event dispatcher expects the production Worker's timer bridge.
    self.window = Object.freeze({ setTimeout: self.setTimeout.bind(self),
      clearTimeout: self.clearTimeout.bind(self) });
    const fs = module.FS;
    fs.mkdirTree('/assets');
    for (const entry of (data.content?.files || [])) {
      if (entry.role === 'ai') continue;
      const bytes = await get('/rules/content/' + entry.sha256);
      if (bytes.length !== entry.size || await hash(bytes) !== entry.sha256) fail('content hash mismatch');
      const path = '/assets/' + entry.path;
      fs.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      fs.writeFile(path, bytes);
    }
    if (module._qsan_client_initialize() !== 0) fail('native initialize failed');
    const registry = JSON.parse(dec.decode(module.FS.readFile('/work/init.json')));
    const records = [];
    for (const item of data.requests) {
      module.FS.writeFile('/work/request.json', enc.encode(JSON.stringify(item.request)));
      if (module._qsan_client_evaluate() !== 0) fail(`snapshot evaluate failed: ${item.label}`);
      records.push({ label: item.label, response_utf8: dec.decode(new Uint8Array(module.FS.readFile('/work/result.json'))) });
    }
    let transportError;
    if (data.mode === 'malformed') {
      module.FS.writeFile('/work/request.json', enc.encode('{'));
      const status = module._qsan_client_evaluate();
      if (status !== 2) fail(`malformed evaluate returned ${status}`);
      transportError = `WASM client evaluation failed (${status})`;
    }
    if (module._qsan_client_shutdown() !== 0 || module._qsan_client_shutdown() !== 0) fail('native shutdown failed');
    self.postMessage({ registry, records, transportError,
      events: ['ready', ...records.map(() => 'result'), 'disposed'], dedicatedWorker: true, documentAbsent: true });
  } catch (error) { self.postMessage({ error: String(error?.stack || error) }); }
  finally { if (url) URL.revokeObjectURL(url); self.close(); }
};
