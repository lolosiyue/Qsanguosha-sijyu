// Adapter tests use a fake Emscripten factory. They are not a WASM execution gate.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import test from 'node:test';
import { createQtTimerHost, executeFixture, parseManifest, verifyEmbeddedAssets } from './run-wasm-fixture.mjs';

const content = Buffer.from('return {}\n');
const manifest = Buffer.from(JSON.stringify({ schema_version: 1, profile: 'builtin-v1', files: [
  { path: 'lua/config.lua', size: content.length,
    sha256: createHash('sha256').update(content).digest('hex') },
] }) + '\n');
const result = Buffer.from('{"schema_version":1,"request_id":"18446744073709551615","targets":["b","a","b"]}\n');

function fakeFactory({ status = 0, payload = result, damage, missingExport = false,
  abort = false, onRun } = {}) {
  return async options => {
    const embedded = new Map([
      ['/assets/fixture-assets.json', manifest], ['/assets/lua/config.lua', content],
    ]);
    damage?.(embedded);
    const files = new Map();
    options.ENV = {};
    options.FS = {
      mkdirTree() {}, chdir(directory) { assert.equal(directory, '/work'); },
      readdir(directory) {
        return ['.', '..', ...new Set([...files.keys()]
          .filter(name => name.startsWith(`${directory}/`))
          .map(name => name.slice(directory.length + 1).split('/')[0]))];
      },
      lstat(name) { return { mode: files.has(name) ? 'file' : 'directory' }; },
      isDir(mode) { return mode === 'directory'; },
      isFile(mode) { return mode === 'file'; },
      writeFile(name, bytes) { files.set(name, Buffer.from(bytes)); },
      readFile(name) {
        if (!files.has(name)) throw new Error(`missing ${name}`);
        return files.get(name);
      },
    };
    for (const callback of options.preInit) callback();
    for (const callback of options.preRun) callback(options);
    // Real Emscripten 4.0.7 installs --embed-file data in initRuntime, after
    // preRun. Eager fake assets previously hid the host's lifecycle bug.
    for (const [name, bytes] of embedded) files.set(name, bytes);
    if (!missingExport) options._qsan_run_fixture = () => {
      assert.equal(options.noInitialRun, true);
      assert.equal(options.ENV.HOME, '/home/fixture');
      onRun?.(options, files);
      if (abort) options.onAbort('deliberate test abort');
      if (payload !== null) files.set('/work/output.json', payload);
      return status;
    };
    return options;
  };
}
const quiet = { printErr() {} };

test('Qt timer bridge schedules asynchronously and cancels numeric Node timer IDs', async () => {
  const host = createQtTimerHost();
  assert.deepEqual(Object.keys(host).sort(), ['clearTimeout', 'setTimeout']);
  let cancelledRan = false;
  const cancelled = host.setTimeout(() => { cancelledRan = true; }, 0);
  assert.equal(typeof cancelled, 'number');
  host.clearTimeout(cancelled);
  let fired = false;
  const done = new Promise(resolve => {
    const id = host.setTimeout(() => { fired = true; resolve(); }, 0);
    assert.equal(typeof id, 'number');
  });
  assert.equal(fired, false);
  await done;
  assert.equal(cancelledRan, false);
});

test('preserves all native bytes, large ID strings, target order and duplicate votes', async () => {
  assert.deepEqual(await executeFixture(fakeFactory(), Buffer.from('{}'), manifest, quiet), result);
});
test('passes malformed input verbatim to the real CLI boundary', async () => {
  await assert.rejects(executeFixture(fakeFactory({ status: 2, onRun(options, files) {
    assert.deepEqual(files.get('/work/input.json'), Buffer.from('{'));
  } }), Buffer.from('{'), manifest, quiet), /exit 2/);
});
test('does not publish output after nonzero native return', async () => {
  await assert.rejects(executeFixture(fakeFactory({ status: 4 }), Buffer.from('{}'), manifest, quiet), /exit 4/);
});
test('rejects absent or non-integer return status', async () => {
  await assert.rejects(executeFixture(fakeFactory({ status: '0' }), Buffer.from('{}'), manifest, quiet), /exit/);
});
test('rejects runtime abort even when export returns zero', async () => {
  await assert.rejects(executeFixture(fakeFactory({ abort: true }), Buffer.from('{}'), manifest, quiet), /exit/);
});
test('rejects missing C export', async () => {
  await assert.rejects(executeFixture(fakeFactory({ missingExport: true }), Buffer.from('{}'), manifest, quiet), /export/);
});
test('rejects missing result after zero return', async () => {
  await assert.rejects(executeFixture(fakeFactory({ payload: null }), Buffer.from('{}'), manifest, quiet), /missing/);
});
test('rejects malformed output instead of fabricating JSON', async () => {
  await assert.rejects(executeFixture(fakeFactory({ payload: Buffer.from('{') }), Buffer.from('{}'), manifest, quiet));
});
test('rejects mismatched embedded content', async () => {
  await assert.rejects(executeFixture(fakeFactory({ damage(files) {
    files.set('/assets/lua/config.lua', Buffer.from('changed'));
  } }), Buffer.from('{}'), manifest, quiet), /asset hash mismatch/);
});
test('rejects mismatched sidecar', () => {
  assert.throws(() => verifyEmbeddedAssets({ readFile() { return Buffer.from('{}'); } }, manifest), /sidecar/);
});
test('rejects unlisted embedded Lua content before invoking the CLI', async () => {
  let invoked = false;
  await assert.rejects(executeFixture(fakeFactory({ damage(files) {
    files.set('/assets/lua/extra.lua', Buffer.from('unexpected bootstrap code'));
  }, onRun() { invoked = true; } }), Buffer.from('{}'), manifest, quiet), /asset inventory/);
  assert.equal(invoked, false);
});
test('rejects invalid and duplicate manifest paths', () => {
  const parsed = JSON.parse(manifest);
  parsed.files[0].path = 'lua/../secret.lua';
  assert.throws(() => parseManifest(Buffer.from(JSON.stringify(parsed))), /manifest entry/);
  parsed.files[0].path = 'lua/config.lua';
  parsed.files.push(parsed.files[0]);
  assert.throws(() => parseManifest(Buffer.from(JSON.stringify(parsed))), /duplicate/);
});
test('rejects oversized input before loading native code', async () => {
  let created = false;
  await assert.rejects(executeFixture(async () => { created = true; },
    Buffer.alloc(1024 * 1024 + 1), manifest, quiet), /at most 1 MiB/);
  assert.equal(created, false);
});
test('supplies hash seed and logging filter before native static initialization', async () => {
  let checked = false;
  await executeFixture(fakeFactory({ onRun(options) {
    assert.equal(options.ENV.QT_HASH_SEED, '0');
    assert.equal(options.ENV.QT_LOGGING_RULES, '*.critical=false');
    checked = true;
  } }), Buffer.from('{}'), manifest, { ...quiet, hashSeed: '0', loggingRules: '*.critical=false' });
  assert.equal(checked, true);
});
test('randomized run does not inherit deterministic Qt seed', async () => {
  await executeFixture(fakeFactory({ onRun(options) {
    assert.equal(options.ENV.QT_HASH_SEED, undefined);
  } }), Buffer.from('{}'), manifest, quiet);
});
