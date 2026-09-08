// Isolated tests of the REAL production RulesController. Worker/catalog are
// explicit doubles; these tests are not WASM execution evidence.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
import test from 'node:test';

const require = createRequire(new URL('../../web/package.json', import.meta.url));
const ts = require(process.env.QSAN_TYPESCRIPT || 'typescript');
const source = readFileSync(new URL('../../web/src/rules-client.ts', import.meta.url), 'utf8');
const compiled = ts.transpileModule(source, { compilerOptions: {
  target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022,
} }).outputText;
const identitySource = readFileSync(new URL('../../web/src/rules-identity.ts', import.meta.url), 'utf8');
const identityCompiled = ts.transpileModule(identitySource, { compilerOptions: {
  target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022,
} }).outputText;
const bundle = () => ({ schema_version: 1, available: true, profile: 'builtin-v1',
  protocol_version: 2, bridge_schema: 1, rules_abi: 'qsan-client-rules-v1',
  source_sha256: '1'.repeat(64), bindings_sha256: '2'.repeat(64),
  lua_sha256: '3'.repeat(64), card_registry_sha256: '4'.repeat(64), card_count: 1,
  packages: ['standard', 'standard_cards'], interaction_schemas: { 'card-selection': 1 } });
const Command = { PLAY_CARD: 1, RESPONSE_CARD: 2, ASK_PEACH: 3, NULLIFICATION: 4 };

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
  const protocol = new vm.SyntheticModule(['Command', 'asString', 'isObject'], function () {
    this.setExport('Command', Command);
    this.setExport('asString', value => typeof value === 'string' ? value : '');
    this.setExport('isObject', value => value !== null && typeof value === 'object' && !Array.isArray(value));
  }, { context });
  // Import the real compatibility comparator, not an always-true test stub.
  const identityModule = new vm.SourceTextModule(identityCompiled, { context });
  const module = new vm.SourceTextModule(compiled, { context,
    initializeImportMeta(meta) { meta.url = 'http://localhost/rules-client.js'; } });
  await module.link(name => {
    if (name === './i18n') return catalog;
    if (name === './protocol') return protocol;
    if (name === './rules-identity') return identityModule;
    throw new Error('Unmocked production dependency: ' + name);
  });
  await module.evaluate();
  const controller = new module.namespace.RulesController(() => {});
  const session = { generation: 1, revision: 1, synchronizing: false, phase: 'active',
    interaction: { messageId: '18446744073709551615', command: Command.PLAY_CARD, payload: {} },
    state: { connection: { rules_bundle: bundle() }, setup: {}, game: {}, selfName: 'a', playerNames: ['a', 'b'],
      players: new Map([['a', { object_name: 'a' }], ['b', { object_name: 'b' }]]),
      cards: new Map(), cardIdSpace: 1 } };
  const selection = { card_ids: [0], targets: ['b'], skill_name: '', skill_instance_id: 0, user_string: '' };
  function ready(worker = workers.at(-1), generation = session.generation, identity = bundle()) {
    worker.emit({ schema_version: 1, type: 'ready', generation,
      info: { schema_version: 1, card_count: 1, rules_bundle: identity,
        registry: [{ id: 0, object_name: 'slash', suit: 0, number: 7 }] } });
  }
  function result(worker = workers.at(-1), changes = {}) {
    const sent = worker.sent.filter(value => value.type === 'evaluate').at(-1);
    const input = JSON.parse(new TextDecoder().decode(sent.input));
    const body = { schema_version: 1, generation: input.generation, revision: input.revision,
      request_id: input.request_id, known: true, reason: '', can_confirm: false, card_text: '',
      selectable_cards: [0], skills: [], declarations: [],
      next_targets: { candidates: ['b'], max_votes: { b: 1 } }, wire: null, ...changes };
    worker.emit({ schema_version: 1, type: 'result', generation: input.generation, id: sent.id,
      bytes: new TextEncoder().encode(JSON.stringify(body)).buffer });
  }
  return { controller, session, selection, workers, timers, ready, result, identity: identityModule.namespace };
}

test('one persistent Worker and uint64 strings for successive selections', async () => {
  const s = await setup();
  s.controller.update(s.session, s.selection); s.ready(); s.result();
  assert.equal(s.controller.current(s.session, s.selection), true);
  assert.equal(s.controller.result.request_id, '18446744073709551615');
  s.controller.update(s.session, { ...s.selection, targets: [] }); s.result();
  assert.equal(s.workers.length, 1);
  s.controller.dispose();
});
test('only the newest queued selection is evaluated; stale result is not displayed', async () => {
  const s = await setup();
  s.controller.update(s.session, s.selection); s.ready();
  s.controller.update(s.session, { ...s.selection, user_string: 'B' });
  s.controller.update(s.session, { ...s.selection, user_string: 'C' });
  s.result(); assert.equal(s.controller.result, null);
  const evaluations = s.workers[0].sent.filter(item => item.type === 'evaluate');
  assert.equal(evaluations.length, 2);
  assert.equal(JSON.parse(new TextDecoder().decode(evaluations[1].input)).selection.user_string, 'C');
  s.result(); s.controller.dispose();
});
test('same Worker recovers from a semantic known=false result', async () => {
  const s = await setup();
  s.controller.update(s.session, s.selection); s.ready(); s.result(undefined, { known: false, reason: 'invalid_state' });
  assert.equal(s.controller.status, 'unsupported');
  s.session.revision++; s.controller.update(s.session, s.selection); s.result();
  assert.equal(s.controller.status, 'ready'); assert.equal(s.workers.length, 1); s.controller.dispose();
});
test('generation change detaches old Worker callbacks', async () => {
  const s = await setup();
  s.controller.update(s.session, s.selection); s.ready(); const old = s.workers[0];
  s.session.generation++; s.controller.update(s.session, s.selection); s.ready();
  s.result(old); assert.equal(s.controller.result, null);
  s.result(); assert.equal(s.controller.result.generation, 2); s.controller.dispose();
});
test('state changes and STATE_SYNC immediately invalidate previous preview', async () => {
  const s = await setup();
  s.controller.update(s.session, s.selection); s.ready(); s.result();
  s.session.synchronizing = true;
  assert.equal(s.controller.current(s.session, s.selection), false);
  s.controller.update(s.session, s.selection); assert.equal(s.controller.result, null);
  s.controller.dispose();
});
test('wrong request correlation fails closed and retry creates a fresh Worker', async () => {
  const s = await setup();
  s.controller.update(s.session, s.selection); s.ready(); s.result(undefined, { request_id: '2' });
  assert.equal(s.controller.status, 'failed'); assert.equal(s.controller.result, null);
  s.controller.retry(); s.controller.update(s.session, s.selection); s.ready(); s.result();
  assert.equal(s.workers.length, 2); assert.equal(s.controller.status, 'ready'); s.controller.dispose();
});
test('timeout invalidates pending results rather than returning cached success', async () => {
  const s = await setup(); s.controller.update(s.session, s.selection); s.ready();
  [...s.timers.values()].find(value => value.ms === 10000).fn();
  assert.equal(s.controller.status, 'failed'); assert.equal(s.controller.result, null); s.controller.dispose();
});
test('graceful disposal acknowledgement terminates once; later updates cannot restart', async () => {
  const s = await setup(); s.controller.update(s.session, s.selection); s.ready(); s.result();
  s.controller.dispose();
  const worker = s.workers[0]; assert.equal(worker.sent.at(-1).type, 'dispose');
  worker.emit({ schema_version: 1, type: 'disposed', generation: 1 });
  assert.equal(worker.terminated, true);
  s.controller.update(s.session, s.selection); s.controller.retry(); assert.equal(s.workers.length, 1);
  assert.equal(s.controller.result, null);
});

test('old or unsupported server identity never starts a rule query', async () => {
  for (const value of [undefined, null, {}, { schema_version: 1, available: false }]) {
    const s = await setup(); s.session.state.connection.rules_bundle = value;
    s.controller.update(s.session, s.selection);
    assert.equal(s.workers.length, 0); assert.equal(s.controller.status, 'failed');
    assert.match(s.controller.error, /server_rules_identity/); s.controller.dispose();
  }
});
test('same card count cannot conceal changed rules, Lua, bindings or registry', async () => {
  for (const key of ['source_sha256', 'bindings_sha256', 'lua_sha256', 'card_registry_sha256']) {
    const s = await setup(); s.controller.update(s.session, s.selection);
    s.ready(undefined, undefined, { ...bundle(), [key]: 'f'.repeat(64) });
    assert.equal(s.controller.status, 'failed'); assert.match(s.controller.error, new RegExp(key));
    assert.equal(s.workers[0].sent.some(item => item.type === 'evaluate'), false); s.controller.dispose();
  }
});
test('old WASM without bundle identity does not fall back to its valid card catalog', async () => {
  const s = await setup(); s.controller.update(s.session, s.selection); s.ready(undefined, undefined, null);
  assert.equal(s.controller.status, 'failed'); assert.match(s.controller.error, /runtime_rules_identity/);
  s.controller.dispose();
});
test('identity drift invalidates an already displayed result before another render', async () => {
  const s = await setup(); s.controller.update(s.session, s.selection); s.ready(); s.result();
  s.session.state.connection.rules_bundle.lua_sha256 = 'f'.repeat(64);
  assert.equal(s.controller.current(s.session, s.selection), false);
  s.controller.update(s.session, s.selection);
  assert.equal(s.controller.result, null); assert.equal(s.controller.status, 'failed'); s.controller.dispose();
});
test('identity drift during a query cannot publish the late result', async () => {
  const s = await setup(); s.controller.update(s.session, s.selection); s.ready();
  s.session.state.connection.rules_bundle.card_registry_sha256 = 'f'.repeat(64); s.result();
  assert.equal(s.controller.result, null); assert.equal(s.controller.status, 'failed'); s.controller.dispose();
});
test('identity parser rejects boolean/numeric/schema coercions and malformed hashes', async () => {
  const s = await setup();
  for (const change of [{ schema_version: true }, { available: 1 }, { card_count: true },
    { card_count: '1' }, { bridge_schema: 2 }, { protocol_version: 1 }, { rules_abi: 'other' },
    { profile: 'external' }, { source_sha256: 'X'.repeat(64) }, { packages: ['a', 'a'] },
    { interaction_schemas: { 'card-selection': true } }])
    assert.equal(s.identity.readRulesIdentity({ ...bundle(), ...change }), null);
  s.controller.dispose();
});
test('package order and extra interaction schemas are significant', async () => {
  const s = await setup();
  assert.match(s.identity.rulesIdentityError(bundle(), { ...bundle(), packages: bundle().packages.reverse() }), /packages/);
  assert.match(s.identity.rulesIdentityError(bundle(), { ...bundle(),
    interaction_schemas: { 'card-selection': 1, other: 1 } }), /interaction_schemas/);
  assert.equal(s.identity.rulesIdentityError(bundle(), bundle()), ''); s.controller.dispose();
});
test('HELLO card count must also match identity count before starting Worker', async () => {
  const s = await setup(); s.session.state.cardIdSpace = 2;
  s.controller.update(s.session, s.selection); assert.equal(s.workers.length, 0);
  assert.match(s.controller.error, /card_count/); s.controller.dispose();
});

async function liveSession() {
  // Exercise the real HELLO receiver. Socket/state collaborators are explicit
  // doubles; native codec compatibility is tested by rules-identity-probe.
  const sockets = [];
  class WebSocket {
    static OPEN = 1; readyState = 1; sent = []; callbacks = {};
    constructor() { sockets.push(this); }
    addEventListener(name, fn) { this.callbacks[name] = fn; }
    close() {}
    send(text) { this.sent.push(JSON.parse(text)); }
    message(message) { this.callbacks.message({ data: JSON.stringify(message) }); }
  }
  class State {
    connection = {};
    reset() { this.connection = {}; }
    setConnectionValue(key, value) { this.connection[key] = value; }
    setCardIdSpace(count) { this.cardIdSpace = count; }
  }
  const commands = { CHECK_VERSION: 9, SIGNUP: 10 };
  const context = vm.createContext({ WebSocket, URL, URLSearchParams, console });
  const helpers = new vm.SyntheticModule(['asBool', 'asNumber', 'asString', 'Command',
    'decodeMessage', 'encodeMessage', 'isObject', 'nextId'], function () {
    this.setExport('asBool', Boolean); this.setExport('asNumber', Number);
    this.setExport('asString', String); this.setExport('Command', commands);
    this.setExport('decodeMessage', JSON.parse); this.setExport('encodeMessage', JSON.stringify);
    this.setExport('isObject', value => value && typeof value === 'object');
    this.setExport('nextId', counter => String(++counter.value));
  }, { context });
  const state = new vm.SyntheticModule(['ClientGameState'], function () { this.setExport('ClientGameState', State); }, { context });
  const mocks = { './protocol': helpers, './state': state };
  for (const [file, name] of [['./reducer', 'applyNotification'], ['./log-text', 'appendSynthesizedLogs'], ['./replies', 'replyCommand']])
    mocks[file] = new vm.SyntheticModule([name], function () { this.setExport(name, () => {}); }, { context });
  const code = ts.transpileModule(readFileSync(new URL('../../web/src/session.ts', import.meta.url), 'utf8'),
    { compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 } }).outputText;
  const module = new vm.SourceTextModule(code, { context });
  await module.link(name => { if (!mocks[name]) throw new Error(name); return mocks[name]; });
  await module.evaluate();
  const session = new module.namespace.LiveSession();
  const connect = identity => {
    session.connect({ wsUrl: 'ws://localhost', screenName: 'test', avatar: 'test', reconnect: false });
    sockets.at(-1).message({ type: 'notification', source: 'lobby', command: 9,
      message_id: '1', payload: { schema_version: 1, card_count: 1, ...(identity ? { rules_bundle: identity } : {}) } });
  };
  return { session, sockets, connect };
}
test('real HELLO receiver retains identity without changing legacy SIGNUP schema', async () => {
  const s = await liveSession(); s.connect(bundle());
  assert.deepEqual(JSON.parse(JSON.stringify(s.session.state.connection.rules_bundle)), bundle());
  assert.equal(s.session.phase, 'signup'); assert.equal(s.sockets[0].sent[0].payload.schema_version, 2);
  assert.equal('rules_bundle' in s.sockets[0].sent[0].payload, false); s.session.disconnect();
});
test('new connection cannot inherit previous server identity, including legacy HELLO', async () => {
  const s = await liveSession(); s.connect(bundle()); s.connect(null);
  assert.equal(s.session.state.connection.rules_bundle, null);
  s.connect({ ...bundle(), lua_sha256: 'f'.repeat(64) });
  assert.equal(s.session.state.connection.rules_bundle.lua_sha256, 'f'.repeat(64)); s.session.disconnect();
});
