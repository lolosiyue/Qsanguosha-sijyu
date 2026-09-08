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
  const session = { generation: 1, revision: 1, synchronizing: false, phase: 'active',
    interaction: { messageId: '18446744073709551615', command: Command.PLAY_CARD, payload: {} },
    state: { connection: {}, setup: {}, game: {}, selfName: 'a', playerNames: ['a', 'b'],
      players: new Map([['a', { object_name: 'a' }], ['b', { object_name: 'b' }]]),
      cards: new Map(), cardIdSpace: 1 } };
  const selection = { card_ids: [0], targets: ['b'], skill_name: '', skill_instance_id: 0, user_string: '' };
  function ready(worker = workers.at(-1), generation = session.generation) {
    worker.emit({ schema_version: 1, type: 'ready', generation,
      info: { schema_version: RULES_BRIDGE_SCHEMA, card_count: 1, rules_bundle: bundle,
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
  return { controller, session, selection, workers, timers, ready, result };
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
