// Isolated tests of the REAL production RulesController. Worker/catalog are
// explicit doubles; these tests are not WASM execution evidence.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { webcrypto } from 'node:crypto';
import vm from 'node:vm';
import test from 'node:test';

const require = createRequire(new URL('../../web/package.json', import.meta.url));
const ts = require(process.env.QSAN_TYPESCRIPT || 'typescript');

// Pure-logic production modules are linked for real: the doubles below stand in
// only for the browser Worker, the timers and the i18n card catalog. Command
// numbers, the bridge schema and the identity predicate therefore come from the
// shipped sources instead of a copy that can silently drift out of date.
const REAL_MODULES = new Set(['./protocol', './replies', './rules-identity']);

function transpile(name) {
  const source = readFileSync(new URL(`../../web/src/${name}.ts`, import.meta.url), 'utf8');
  return ts.transpileModule(source, { compilerOptions: {
    target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022,
  } }).outputText;
}

function sourceModule(name, context) {
  return new vm.SourceTextModule(transpile(name), { context,
    initializeImportMeta(meta) { meta.url = `http://localhost/${name}.js`; } });
}

const REQUEST = '18446744073709551615';
const EXCHANGE_CARD = 6;
const DISCARD_CARD = 7;

// Worker messages are built inside the vm realm; compare their shape, not the
// realm their prototypes came from.
const plain = value => JSON.parse(JSON.stringify(value));

async function setup(options = {}) {
  const workers = [], timers = new Map();
  const storage = new Map();
  const localStorage = {
    getItem(key) { return storage.has(key) ? storage.get(key) : null; },
    setItem(key, value) { storage.set(key, String(value)); },
    removeItem(key) { storage.delete(key); },
  };
  let timerId = 0;
  class Worker {
    listeners = new Map(); sent = []; terminated = false;
    constructor() { workers.push(this); }
    addEventListener(type, fn) {
      this.listeners.set(type, [...(this.listeners.get(type) || []), fn]);
    }
    postMessage(value) {
      this.sent.push(value);
      if (value.type === 'prepare') {
        this.emit({ schema_version: 1, type: 'prepared', generation: value.generation, code });
      } else if (value.type === 'initialize') {
        this.emit({ schema_version: 1, type: 'ready', generation: value.generation,
          info: { schema_version: RULES_BRIDGE_SCHEMA, card_count: 1, rules_bundle: bundle,
            translations: { native_key: 'native' },
            registry: [{ id: 0, object_name: 'slash', suit: 0, number: 7 }] } });
      }
    }
    terminate() { this.terminated = true; }
    emit(data, type = 'message') {
      for (const fn of this.listeners.get(type) || []) fn(type === 'message' ? { data } : data);
    }
  }
  const context = vm.createContext({ Worker, URL, TextEncoder, TextDecoder, ArrayBuffer, crypto: webcrypto,
    localStorage,
    setTimeout(fn, ms) { const id = ++timerId; timers.set(id, { fn, ms }); return id; },
    clearTimeout(id) { timers.delete(id); }, console });
  const translationCalls = [];
  const catalog = new vm.SyntheticModule(['cardRecord', 'installRulesCardCatalog', 'installRulesTranslations', 'resetRulesTranslations', 'validateRulesTranslations'], function () {
    this.setExport('cardRecord', () => undefined);
    this.setExport('installRulesCardCatalog', () => {});
    this.setExport('installRulesTranslations', value => translationCalls.push(value));
    this.setExport('resetRulesTranslations', () => translationCalls.push('reset'));
    this.setExport('validateRulesTranslations', value => {
      if (!value || typeof value !== 'object' || Array.isArray(value)
          || Object.values(value).some(item => typeof item !== 'string'))
        throw new Error('rules_identity_invalid');
      return value;
    });
  }, { context });
  const real = new Map();
  const module = sourceModule('rules-client', context);
  await module.link(name => {
    if (name === './i18n') return catalog;
    if (!REAL_MODULES.has(name)) throw new Error('Unmocked production dependency: ' + name);
    if (!real.has(name)) real.set(name, sourceModule(name.slice(2), context));
    return real.get(name);
  });
  await module.evaluate();
  const { Command } = real.get('./protocol').namespace;
  const { RULES_BRIDGE_SCHEMA } = real.get('./rules-identity').namespace;
  // Build a genuinely sealed server identity: verifyNativeIdentity is the real
  // production predicate, so tests cannot accidentally accept fake hashes.
  const digest = 'a'.repeat(64);
  const code = { protocol_version: 2, bridge_schema: RULES_BRIDGE_SCHEMA, cpp_hash: digest,
    bindings_abi: digest,
    interaction_schemas: { [Command.PLAY_CARD]: digest, [Command.RESPONSE_CARD]: digest,
      [Command.DISCARD_CARD]: digest, [Command.EXCHANGE_CARD]: digest } };
  const sha = async bytes => [...new Uint8Array(await webcrypto.subtle.digest('SHA-256', bytes))]
    .map(value => value.toString(16).padStart(2, '0')).join('');
  const canonical = value => Array.isArray(value) ? `[${value.map(canonical).join(',')}]`
    : value !== null && typeof value === 'object'
      ? `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${canonical(value[key])}`).join(',')}}`
      : JSON.stringify(value);
  code.code_id = await sha(new TextEncoder().encode(`qsan-rules-code-v1\0${canonical({ ...code })}`));
  const bundle = { schema_version: 1, protocol_version: 2, bridge_schema: RULES_BRIDGE_SCHEMA,
    ruleset: 'standard', content_profile: 'declared-v1',
    cpp_hash: digest,
    card_registry_hash: digest, lua_hash: digest, bindings_abi: digest, packages: ['standard'],
    interaction_schemas: code.interaction_schemas, code_id: code.code_id };
  bundle.bundle_id = await sha(new TextEncoder().encode(
    `qsan-rules-bundle-v1\0${canonical(bundle)}`));
  const content = { schema_version: 1, profile: 'declared-v1', files: [] };
  if (options.cached)
    localStorage.setItem('qsan-rules-content-v1', JSON.stringify({ identity: bundle, content }));
  const controller = new module.namespace.RulesController(() => {});
  let sink = null;
  // The controller owns rules state entirely; the session only supplies frames,
  // the visible request and the connection identity it already had.
  const session = { generation: 1, synchronizing: false, phase: 'active',
    setFrameSink(value) { sink = value; },
    interaction: { messageId: REQUEST, command: Command.PLAY_CARD, payload: {} },
    state: { cardIdSpace: 1 } };
  const selection = { card_ids: [0], targets: ['b'], skill_name: '', skill_instance_id: 0,
    user_string: '', top: [], bottom: [] };
  const status = { generation: 1, revision: 0, request_id: '', active: true,
    synchronizing: false, failed: false };
  function ready(worker = workers.at(-1), generation = session.generation) {
    worker.emit({ schema_version: 1, type: 'ready', generation,
      info: { schema_version: RULES_BRIDGE_SCHEMA, card_count: 1, rules_bundle: bundle,
        translations: { native_key: 'native' },
        registry: [{ id: 0, object_name: 'slash', suit: 0, number: 7 }] } });
  }
  function frame(text, outgoing = false, generation = session.generation) {
    sink(generation, outgoing, text);
  }
  function pending(worker = workers.at(-1)) {
    return worker.sent.filter(value => value.type === 'stream').at(-1);
  }
  // Answer the newest batch exactly as the Worker does: one result per applied
  // operation, the last one carrying the runtime's committed status.
  function ack({ worker = workers.at(-1), success = true, reason = '', evaluation,
    known = true, ...changes } = {}) {
    const message = pending(worker);
    Object.assign(status, changes);
    const query = message.ops.at(-1).action === 'query' ? message.ops.at(-1) : null;
    const body = query && success && evaluation !== null ? {
      schema_version: 1, generation: query.generation, revision: query.revision,
      request_id: query.request_id, known, reason: '', can_confirm: false, card_text: '',
      selectable_cards: [0], card_zones: { 0: 'hand' }, selection_min: 1, selection_max: 1,
      interaction: { type: 'PlayCard', payload: {} }, skills: [], declarations: [],
      declaration_dialog: {},
      next_targets: { candidates: ['b'], max_votes: { b: 1 } }, wire: null, ...evaluation,
    } : null;
    const results = message.ops.map((operation, index) => ({ schema_version: 1,
      success: index === message.ops.length - 1 ? success : true,
      reason: index === message.ops.length - 1 ? reason : '', status: { ...status },
      ...(body && index === message.ops.length - 1 ? { evaluation: body } : {}) }));
    worker.emit({ schema_version: 1, type: 'stream', generation: message.generation,
      id: message.id, results });
    return message;
  }
  // Reach a queryable connection: one observed frame acknowledged with the
  // request the runtime itself committed.
  async function connect() {
    await controller.initialize(session);
    const initialized = controller.initialize(session, { rules_bundle: bundle, rules_content: content });
    // verifyNativeIdentity performs two asynchronous WebCrypto checks before
    // posting initialize; let those checks install identityWait first.
    await Promise.resolve();
    await Promise.resolve();
    await Promise.resolve();
    await initialized;
    frame('{"hello":1}');
    ack({ revision: 7, request_id: REQUEST });
  }
  async function mismatchedBundle() {
    const { code_id: ignored, ...unsignedCode } = code;
    const alteredCode = { ...unsignedCode, cpp_hash: 'b'.repeat(64) };
    alteredCode.code_id = await sha(new TextEncoder().encode(
      `qsan-rules-code-v1\0${canonical({ ...unsignedCode, cpp_hash: alteredCode.cpp_hash })}`));
    const altered = { ...bundle, cpp_hash: alteredCode.cpp_hash, code_id: alteredCode.code_id };
    delete altered.bundle_id;
    altered.bundle_id = await sha(new TextEncoder().encode(
      `qsan-rules-bundle-v1\0${canonical(altered)}`));
    return altered;
  }
  return { controller, session, selection, workers, timers, translationCalls, ready, frame, pending, ack, connect,
    bundle, content, code, mismatchedBundle };
}

test('frames reach the runtime in transport order and carry no browser snapshot', async () => {
  const s = await setup();
  await s.connect();
  s.frame('{"in":1}');
  s.frame('{"out":1}', true);
  const first = s.pending();
  assert.deepEqual(plain(first.ops), [{ schema_version: 1, action: 'frame', generation: 1,
    direction: 'incoming', frame: '{"in":1}' }]);
  s.ack({ revision: 1 });
  assert.deepEqual(plain(s.pending().ops), [{ schema_version: 1, action: 'frame', generation: 1,
    direction: 'outgoing', frame: '{"out":1}' }]);
  s.ack({ revision: 2 });
  assert.equal(s.workers.length, 1);
  s.controller.dispose();
});

test('a matching local content cache prepares one Worker and skips negotiation fetch', async () => {
  const s = await setup({ cached: true });
  const identity = await s.controller.initialize(s.session);
  assert.equal(identity.bundle_id, s.bundle.bundle_id);
  assert.equal(s.workers.length, 1);
  assert.equal(s.workers[0].sent.filter(value => value.type === 'initialize').length, 1);
  assert.equal(s.translationCalls.some(value => value && value.native_key === 'native'), false);
  s.controller.dispose();
});

test('Hello code mismatch is rejected before content or Worker initialize dispatch', async () => {
  const s = await setup();
  await s.controller.initialize(s.session);
  const worker = s.workers[0];
  await assert.rejects(s.controller.initialize(s.session,
    { rules_bundle: await s.mismatchedBundle(), rules_content: s.content }),
    /rules_version_mismatch/);
  assert.equal(worker.sent.some(value => value.type === 'initialize'), false);
  s.controller.dispose();
});

test('frames remain queued until content negotiation succeeds', async () => {
  const s = await setup();
  const prepared = s.controller.initialize(s.session);
  s.frame('{"before":1}');
  await prepared;
  assert.equal(s.workers[0].sent.some(value => value.type === 'initialize'), false);
  const initialized = s.controller.initialize(s.session,
    { rules_bundle: s.bundle, rules_content: s.content });
  await initialized;
  assert.equal(s.pending().ops[0].action, 'frame');
  s.controller.dispose();
});

test('changed content replaces the Worker and stale replies cannot mutate the new generation', async () => {
  const s = await setup();
  await s.connect();
  assert.equal(s.translationCalls.filter(value => value && value.native_key === 'native').length, 1);
  const old = s.workers[0];
  s.session.generation = 2;
  const changed = { ...s.content, files: [{ path: 'lua/config.lua', role: 'rules', size: 1, sha256: 'c'.repeat(64) }] };
  await s.controller.initialize(s.session);
  const initialized = s.controller.initialize(s.session,
    { rules_bundle: s.bundle, rules_content: changed });
  await Promise.resolve();
  await Promise.resolve();
  await Promise.resolve();
  old.emit({ schema_version: 1, type: 'ready', generation: 1,
    info: { schema_version: 2, card_count: 1, rules_bundle: s.bundle, registry: [{ id: 0, object_name: 'stale', suit: 0, number: 1 }] } });
  await initialized;
  assert.equal(s.workers.length, 3);
  assert.equal(s.controller.status, 'ready');
  assert.equal(s.translationCalls.filter(value => value && value.native_key === 'native').length, 2);
  s.controller.dispose();
});

test('queries correlate on the runtime revision and request, not the reducer', async () => {
  const s = await setup();
  await s.connect();
  s.controller.update(s.session, s.selection);
  const query = s.pending().ops.at(-1);
  assert.deepEqual(plain(query), { schema_version: 1, action: 'query', generation: 1, revision: 7,
    request_id: '18446744073709551615', selection: s.selection });
  s.ack({});
  assert.equal(s.controller.current(s.session, s.selection), true);
  assert.equal(s.controller.result.request_id, '18446744073709551615');
  s.controller.dispose();
});

test('discard and exchange prompts are queried through native selectable sets', async () => {
  for (const command of [DISCARD_CARD, EXCHANGE_CARD]) {
    const s = await setup();
    s.session.interaction = { messageId: REQUEST, command, payload: { pattern: "basic" } };
    await s.connect();
    assert.equal(s.controller.supports(command), true);
    s.controller.update(s.session, s.selection);
    assert.equal(s.pending().ops.at(-1).action, 'query');
    s.ack({ evaluation: { selectable_cards: [0], selection_min: 1, selection_max: 1 } });
    assert.equal(s.controller.current(s.session, s.selection), true);
    s.controller.dispose();
  }
});

test('an unacknowledged frame or an uncommitted STATE_SYNC blocks every query', async () => {
  const s = await setup();
  await s.connect();
  s.frame('{"in":2}');
  s.controller.update(s.session, s.selection);
  assert.equal(s.pending().ops.at(-1).action, 'frame');
  s.ack({ revision: 8, synchronizing: true });
  // The sync is open: no query may run against the private accumulator.
  assert.equal(s.pending().ops.at(-1).action, 'frame');
  s.frame('{"in":3}');
  s.ack({ revision: 9, synchronizing: false });
  assert.equal(s.pending().ops.at(-1).action, 'query');
  s.controller.dispose();
});

test('a newer frame invalidates a displayed preview before the query is reissued', async () => {
  const s = await setup();
  await s.connect();
  s.controller.update(s.session, s.selection);
  s.ack({});
  assert.equal(s.controller.current(s.session, s.selection), true);
  s.frame('{"in":3}');
  assert.equal(s.controller.current(s.session, s.selection), false);
  s.ack({ revision: 8 });
  assert.equal(s.controller.current(s.session, s.selection), false);
  assert.equal(s.pending().ops.at(-1).revision, 8);
  s.ack({});
  assert.equal(s.controller.current(s.session, s.selection), true);
  s.controller.dispose();
});

test('a refused frame disables the preview only and never restarts an Engine', async () => {
  const s = await setup();
  await s.connect();
  s.frame('{"in":4}');
  s.ack({ success: false, reason: 'stream_decode:bad frame', failed: true });
  assert.equal(s.controller.status, 'failed');
  assert.equal(s.controller.result, null);
  assert.equal(s.session.phase, 'active');
  s.frame('{"in":5}');
  s.controller.update(s.session, s.selection);
  assert.equal(s.workers.length, 1);
  assert.equal(s.controller.current(s.session, s.selection), false);
  s.controller.dispose();
});

test('a refused query keeps the stream and is never repeated at the same revision', async () => {
  const s = await setup();
  await s.connect();
  s.controller.update(s.session, s.selection);
  s.ack({ success: false, reason: 'stream_stale_query' });
  assert.equal(s.controller.status, 'unsupported');
  assert.equal(s.controller.result, null);
  const count = s.workers[0].sent.length;
  s.controller.update(s.session, s.selection);
  assert.equal(s.workers[0].sent.length, count);
  s.frame('{"in":6}');
  s.ack({ revision: 8 });
  assert.equal(s.pending().ops.at(-1).revision, 8);
  s.ack({});
  assert.equal(s.controller.status, 'ready');
  assert.equal(s.workers.length, 1);
  s.controller.dispose();
});

test('a known=false answer stays displayable and does not fail the runtime', async () => {
  const s = await setup();
  await s.connect();
  s.controller.update(s.session, s.selection);
  s.ack({ known: false, evaluation: { known: false, reason: 'invalid_state' } });
  assert.equal(s.controller.status, 'unsupported');
  assert.equal(s.controller.error, 'invalid_state');
  assert.equal(s.workers.length, 1);
  s.controller.dispose();
});

test('frames from an obsolete connection are dropped', async () => {
  const s = await setup();
  await s.connect();
  const count = s.workers[0].sent.length;
  s.frame('{"in":7}', false, 2);
  assert.equal(s.workers[0].sent.length, count);
  s.controller.dispose();
});

test('a request the runtime has not committed is never previewed', async () => {
  const s = await setup();
  await s.connect();
  s.session.interaction = { ...s.session.interaction, messageId: '2' };
  s.controller.update(s.session, s.selection);
  assert.equal(s.pending().ops.at(-1).action, 'frame');
  assert.equal(s.controller.current(s.session, s.selection), false);
  s.controller.dispose();
});

test('timeout invalidates pending results rather than returning cached success', async () => {
  const s = await setup();
  await s.connect();
  s.controller.update(s.session, s.selection);
  [...s.timers.values()].find(value => value.ms === 10000).fn();
  assert.equal(s.controller.status, 'failed');
  assert.equal(s.controller.result, null);
  s.controller.dispose();
});

test('graceful disposal acknowledgement terminates once; later updates cannot restart', async () => {
  const s = await setup();
  await s.connect();
  s.controller.update(s.session, s.selection);
  s.ack({});
  s.controller.dispose();
  const worker = s.workers[0];
  assert.equal(worker.sent.at(-1).type, 'dispose');
  worker.emit({ schema_version: 1, type: 'disposed', generation: 1 });
  assert.equal(worker.terminated, true);
  s.controller.update(s.session, s.selection);
  assert.equal(s.workers.length, 1);
  assert.equal(s.controller.result, null);
});
