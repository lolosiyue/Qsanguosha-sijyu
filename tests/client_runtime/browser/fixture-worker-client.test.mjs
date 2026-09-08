// Worker doubles test lifecycle/correlation only, never native/WASM rule parity.
import assert from 'node:assert/strict';
import test from 'node:test';
import { FixtureWorkerClient } from './fixture-worker-client.mjs';

const input = new Uint8Array([123, 125]);
const manifest = new Uint8Array([1, 2, 3]);
function harness() {
  const workers = [];
  const client = new FixtureWorkerClient({ workerFactory() {
    const worker = { terminated: 0, postMessage(message, transfers) {
      this.sent = message; this.transfers = transfers;
    }, terminate() { ++this.terminated; } };
    workers.push(worker);
    return worker;
  } });
  return { client, workers };
}
function finish(worker, changes = {}) {
  worker.onmessage({ data: { schema_version: 1, id: worker.sent.id, type: 'result',
    bytes: new Uint8Array([1, 2]).buffer, ...changes } });
}

test('owns transfer copies and terminates on successful result', async () => {
  const { client, workers } = harness();
  const pending = client.run(input, manifest);
  const worker = workers[0];
  assert.notEqual(worker.sent.input, input.buffer);
  assert.notEqual(worker.sent.manifest, manifest.buffer);
  assert.deepEqual(worker.transfers, [worker.sent.input, worker.sent.manifest]);
  assert.equal(input.byteLength, 2);
  finish(worker);
  assert.deepEqual((await pending).bytes, new Uint8Array([1, 2]));
  assert.equal(worker.terminated, 1);
  assert.equal(client.active, null);
});
test('cancel destroys the old generation; a fresh run succeeds', async () => {
  const { client, workers } = harness();
  const old = client.run(input, manifest);
  const late = workers[0].onmessage;
  client.cancel();
  await assert.rejects(old, /cancelled/);
  const fresh = client.run(input, manifest);
  late({ data: { schema_version: 1, type: 'result', id: workers[0].sent.id,
    bytes: new Uint8Array([9]).buffer } });
  finish(workers[1]);
  assert.deepEqual((await fresh).bytes, new Uint8Array([1, 2]));
  assert.equal(workers[0].terminated, 1);
  assert.notEqual(workers[0].sent.id, workers[1].sent.id);
});
test('supersession rejects only the old run and ignores delayed old callbacks', async () => {
  const { client, workers } = harness();
  const old = client.run(input, manifest);
  const lateError = workers[0].onerror;
  const fresh = client.run(input, manifest);
  await assert.rejects(old, /superseded/);
  lateError({ message: 'old failure' });
  finish(workers[1]);
  await fresh;
});
test('wrong correlation ID cannot complete a current run', async () => {
  const { client, workers } = harness();
  const pending = client.run(input, manifest);
  finish(workers[0], { id: 'old-id' });
  assert.notEqual(client.active, null);
  finish(workers[0]);
  await pending;
});
test('native error preserves bounded diagnostics and destroys the worker', async () => {
  const { client, workers } = harness();
  const pending = client.run(input, manifest);
  finish(workers[0], { type: 'error', error: 'WASM fixture exit 4', logs: ['invalid request_id'] });
  await assert.rejects(pending, /exit 4\ninvalid request_id/);
  assert.equal(workers[0].terminated, 1);
});
test('timeout destroys a worker that never replied', async () => {
  const { client, workers } = harness();
  await assert.rejects(client.run(input, manifest, { timeoutMs: 10 }), /timed out/);
  assert.equal(workers[0].terminated, 1);
});
test('AbortSignal destroys a loading worker', async () => {
  const { client, workers } = harness();
  const abort = new AbortController();
  const pending = client.run(input, manifest, { signal: abort.signal });
  abort.abort();
  await assert.rejects(pending, /aborted/);
  assert.equal(workers[0].terminated, 1);
});
test('pre-aborted input creates no worker', async () => {
  const { client, workers } = harness();
  const abort = new AbortController(); abort.abort();
  await assert.rejects(client.run(input, manifest, { signal: abort.signal }), /aborted/);
  assert.equal(workers.length, 0);
});
test('dispose rejects the current and all future operations', async () => {
  const { client, workers } = harness();
  const pending = client.run(input, manifest);
  client.dispose(); client.dispose();
  await assert.rejects(pending, /disposed/);
  await assert.rejects(client.run(input, manifest), /disposed/);
  assert.equal(workers.length, 1);
  assert.equal(workers[0].terminated, 1);
});
test('script error and message deserialization failure reject without output', async () => {
  for (const callback of ['onerror', 'onmessageerror']) {
    const { client, workers } = harness();
    const pending = client.run(input, manifest);
    workers[0][callback]({ message: 'broken script' });
    await assert.rejects(pending, /Worker/);
    assert.equal(workers[0].terminated, 1);
  }
});
test('invalid schema, missing bytes and excessive output fail closed', async () => {
  for (const changes of [{ schema_version: 2 }, { bytes: null }, { type: 'unknown' },
    { bytes: new ArrayBuffer(8 * 1024 * 1024 + 1) }]) {
    const { client, workers } = harness();
    const pending = client.run(input, manifest);
    finish(workers[0], changes);
    await assert.rejects(pending, /invalid fixture Worker/);
    assert.equal(workers[0].terminated, 1);
  }
});
test('invalid input does not start or supersede a worker', async () => {
  const { client, workers } = harness();
  const pending = client.run(input, manifest);
  await assert.rejects(client.run(new Uint8Array(1024 * 1024 + 1), manifest), /invalid/);
  await assert.rejects(client.run(input, manifest, { timeoutMs: 0 }), /invalid/);
  assert.equal(workers.length, 1);
  finish(workers[0]); await pending;
});
test('construction and postMessage errors leave no active operation', async () => {
  for (const failAt of ['construct', 'send']) {
    let terminated = 0;
    const client = new FixtureWorkerClient({ workerFactory() {
      if (failAt === 'construct') throw new Error('constructor error');
      return { postMessage() { throw new Error('send error'); }, terminate() { ++terminated; } };
    } });
    await assert.rejects(client.run(input, manifest), /error/);
    assert.equal(client.active, null);
    assert.equal(terminated, failAt === 'send' ? 1 : 0);
  }
});

test('shared host uses asynchronous Web Crypto and copies bytes out of the module', async () => {
  const { executeFixture } = await import('../wasm-fixture-host.mjs');
  const encoder = new TextEncoder();
  const content = encoder.encode('return {}');
  const digest = async bytes => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
    .map(value => value.toString(16).padStart(2, '0')).join('');
  const assets = encoder.encode(JSON.stringify({ schema_version: 1, profile: 'builtin-v1',
    files: [{ path: 'lua/config.lua', size: content.length, sha256: await digest(content) }] }));
  const result = encoder.encode('{"schema_version":1,"id":"18446744073709551615"}\n');
  const factory = async options => {
    const files = new Map();
    options.ENV = {};
    options.FS = { mkdirTree() {}, chdir() {}, writeFile(path, bytes) { files.set(path, bytes); },
      readFile(path) { return files.get(path); },
      readdir(path) { return [...new Set([...files.keys()].filter(key => key.startsWith(path + '/'))
        .map(key => key.slice(path.length + 1).split('/')[0]))]; },
      lstat(path) { return { mode: files.has(path) ? 'file' : 'dir' }; },
      isDir(mode) { return mode === 'dir'; }, isFile(mode) { return mode === 'file'; } };
    options.preInit.forEach(fn => fn());
    options.preRun.forEach(fn => fn(options));
    files.set('/assets/fixture-assets.json', assets);
    files.set('/assets/lua/config.lua', content);
    options._qsan_run_fixture = () => { files.set('/work/output.json', result); return 0; };
    return options;
  };
  const actual = await executeFixture(factory, input, assets, { hashBytes: digest });
  assert.deepEqual(actual, result);
  result.fill(0);
  assert.match(new TextDecoder().decode(actual), /18446744073709551615/);
});
