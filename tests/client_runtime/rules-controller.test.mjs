// Isolated tests of the REAL production RulesController. Worker/catalog are
// explicit doubles; these tests are not WASM execution evidence.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
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

// Worker messages are built inside the vm realm; compare their shape, not the
// realm their prototypes came from.
const plain = value => JSON.parse(JSON.stringify(value));

async function setup() {
  const workers = [], timers = new Map();
  let timerId = 0;
  class Worker {
    listeners = new Map(); sent = []; terminated = false;
    constructor() { workers.push(this); }
    addEventListener(type, fn) {
      this.listeners.set(type, [...(this.listeners.get(type) || []), fn]);
    }
    postMessage(value) { this.sent.push(value); }
    terminate() { this.terminated = true; }
    emit(data, type = 'message') {
      for (const fn of this.listeners.get(type) || []) fn(type === 'message' ? { data } : data);
    }
  }
  const context = vm.createContext({ Worker, URL, TextEncoder, TextDecoder, ArrayBuffer,
    setTimeout(fn, ms) { const id = ++timerId; timers.set(id, { fn, ms }); return id; },
    clearTimeout(id) { timers.delete(id); }, console });
  const catalog = new vm.SyntheticModule(['cardRecord', 'installRulesCardCatalog'], function () {
    this.setExport('cardRecord', () => undefined);
    this.setExport('installRulesCardCatalog', () => {});
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
  // Format-valid identity: isRulesIdentity() checks shape, not hash provenance,
  // and every declared interaction schema must name a supported command.
  const digest = 'a'.repeat(64);
  const bundle = { schema_version: 1, protocol_version: 2, bridge_schema: RULES_BRIDGE_SCHEMA,
    ruleset: 'standard', content_profile: 'builtin-v1', bundle_id: digest, cpp_hash: digest,
    card_registry_hash: digest, lua_hash: digest, bindings_abi: digest, packages: ['standard'],
    interaction_schemas: { [Command.PLAY_CARD]: digest, [Command.RESPONSE_CARD]: digest } };
  const controller = new module.namespace.RulesController(() => {});
  let sink = null;
  // The controller owns rules state entirely; the session only supplies frames,
  // the visible request and the connection identity it already had.
  const session = { generation: 1, synchronizing: false, phase: 'active',
    setFrameSink(value) { sink = value; },
    interaction: { messageId: REQUEST, command: Command.PLAY_CARD, payload: {} },
    state: { cardIdSpace: 1 } };
  const selection = { card_ids: [0], targets: ['b'], skill_name: '', skill_instance_id: 0, user_string: '' };
  const status = { generation: 1, revision: 0, request_id: '', active: true,
    synchronizing: false, failed: false };
  function ready(worker = workers.at(-1), generation = session.generation) {
    worker.emit({ schema_version: 1, type: 'ready', generation,
      info: { schema_version: RULES_BRIDGE_SCHEMA, card_count: 1, rules_bundle: bundle,
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
      selectable_cards: [0], skills: [], declarations: [],
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
    const initialized = controller.initialize(session);
    ready();
    await initialized;
    frame('{"hello":1}');
    ack({ revision: 7, request_id: REQUEST });
  }
  return { controller, session, selection, workers, timers, ready, frame, pending, ack, connect };
}

test('frames reach the runtime in transport order and carry no browser snapshot', async () => {
  const s = await setup();
  const initialized = s.controller.initialize(s.session);
  s.ready();
  await initialized;
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
