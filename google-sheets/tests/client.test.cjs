// Focused source contracts; execute only after the checkpoint is approved.
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function context() {
  const properties = new Map(); let doc = 'player-a-document';
  const store = {getProperty: k => properties.get(k), setProperty: (k, v) => properties.set(k, v),
    deleteProperty: k => properties.delete(k), getProperties: () => Object.fromEntries(properties),
    setProperties: obj => Object.entries(obj).forEach(([k, v]) => properties.set(k, v))};
  const c = vm.createContext({
    SpreadsheetApp: {getActive: () => ({getId: () => doc}), WrapStrategy: {CLIP: 'CLIP'}},
    PropertiesService: {getUserProperties: () => store},
    Utilities: {DigestAlgorithm: {SHA_256: 'sha256'},
      computeDigest: (_, s) => [...crypto.createHash('sha256').update(s).digest()],
      newBlob: v => { const b = Buffer.from(v); return {getBytes: () => [...b], getDataAsString: () => b.toString('utf8')}; },
      base64Encode: bytes => Buffer.from(bytes).toString('base64'), base64Decode: s => [...Buffer.from(s, 'base64')]},
    LockService: {getDocumentLock: () => ({tryLock: () => true, releaseLock() {}})}
  });
  for (const file of ['Client.gs', 'Draft.gs', 'Table.gs']) vm.runInContext(fs.readFileSync(path.join(__dirname, '../apps-script', file), 'utf8'), c);
  c.changeDocument = value => { doc = value; }; return c;
}
function sidebarContext() {
  const elements = new Map(); let pending = false, nextResult = null, success, failure, runner;
  const document = {getElementById: id => {
      if (!elements.has(id)) elements.set(id, {value: '', textContent: '', className: '', hidden: false, src: '', disabled: false});
      return elements.get(id);
    }, querySelectorAll: () => []};
  const api = {
    withSuccessHandler(fn) { success = fn; return runner; },
    withFailureHandler(fn) { failure = fn; return runner; }
  };
  runner = new Proxy(api, {get(target, key) {
    if (key in target) return target[key].bind(target);
    return () => {
      const result = nextResult; nextResult = null;
      if (key === 'getClientState') return success({paired: false, endpoint: ''});
      if (result && result.error) return failure({message: result.error});
      return success(result ? result.value : {state: {pending}, status: 'connected'});
    };
  }});
  const c = vm.createContext({document, google: {script: {run: runner}}, setTimeout: () => 1, clearTimeout() {}});
  const html = fs.readFileSync(path.join(__dirname, '../apps-script/Sidebar.html'), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/);
  assert.ok(script, 'Sidebar script block exists');
  vm.runInContext(script[1], c);
  c.setPending = value => { pending = value; };
  c.respondWithError = error => { nextResult = {error}; };
  c.respondWith = value => { nextResult = {value}; };
  c.element = id => elements.get(id);
  return c;
}
function plain(value) { return JSON.parse(JSON.stringify(value)); }
function row(bank, id, options = {}) {
  return [bank, String(id), 'display label', options.selected !== false, options.order || '', options.side || '',
    options.enabled !== false, 'detail', options.skill || '', options.instance || 0, ''];
}

test('option replies contain no card fields; zero-valued options remain strings', () => {
  const c = context();
  assert.deepEqual(plain(c.draftFromRows_({shape: 'option'}, [row('option', 0)])), {option: '0'});
});
test('ordered repeated targets and skill instance survive without wire card text', () => {
  const c = context();
  const draft = c.draftFromRows_({shape: 'cards'}, [row('card', 0), row('skill', 'skill:12', {skill: 'skill', instance: 12}),
    row('player', 'p1', {order: '1,3'}), row('player', 'p2', {order: '2'}), row('declaration', 'slash')]);
  assert.deepEqual(plain(draft), {cards: [0], targets: ['p1', 'p2', 'p1'], skill_name: 'skill', skill_instance_id: 12, declaration: 'slash'});
});
test('Guanxing preserves top/bottom order including every selected card', () => {
  const c = context();
  assert.deepEqual(plain(c.draftFromRows_({shape: 'rearrangement'}, [row('rearrange', 7, {order: '3', side: 'top'}),
    row('rearrange', 8, {order: '1', side: 'top'}), row('rearrange', 9, {order: '2', side: 'bottom'})])), {top: [8, 7], bottom: [9]});
});
test('role assignments use player-name/role-value pairs; Yiji uses one target', () => {
  const c = context();
  assert.deepEqual(plain(c.draftFromRows_({shape: 'assignment', roles: ['lord', 'rebel']}, [row('assignment', 'p1', {side: 'lord'}),
    row('assignment', 'p2', {side: 'rebel'})])), {assignments: [{name: 'p1', value: 'lord'}, {name: 'p2', value: 'rebel'}]});
  assert.deepEqual(plain(c.draftFromRows_({shape: 'distribution'}, [row('card', 4), row('card', 6), row('player', 'p2')])), {cards: [4, 6], target: 'p2'});
  assert.throws(() => c.draftFromRows_({shape: 'distribution'}, [row('player', 'p1'), row('player', 'p2')]));
});
test('general order differs from candidate row order; QML does not fabricate a reply', () => {
  const c = context();
  assert.deepEqual(plain(c.draftFromRows_({shape: 'general_arrangement'}, [row('general', 'caocao', {order: '2'}),
    row('general', 'liubei', {order: '1'})])), {order: ['liubei', 'caocao']});
  assert.throws(() => c.draftFromRows_({shape: 'unsupported'}, []));
});
test('non-numeric order, disabled selections and duplicate positions fail visibly', () => {
  const c = context();
  assert.throws(() => c.draftFromRows_({shape: 'cards'}, [row('card', 1, {enabled: false})]));
  const disabledCell = row('card', 1); disabledCell[6] = 'FALSE';
  assert.throws(() => c.draftFromRows_({shape: 'cards'}, [disabledCell]), /不可用/);
  assert.throws(() => c.draftFromRows_({shape: 'players'}, [row('player', 'p1', {order: 'NaN'})]));
  assert.throws(() => c.draftFromRows_({shape: 'players'}, [row('player', 'p1', {order: '1'}), row('player', 'p2', {order: '1'})]));
});
test('literal cell values cannot become formulas', () => {
  const c = context();
  assert.equal(c.safe_('=IMPORTXML("https://example.test", "//a")').charAt(0), "'");
  assert.equal(c.safe_('  +SUM(A1:A2)').charAt(0), "'");
  assert.equal(c.safe_(true), true);
});
test('skill choice persists when initial view transitions to preflight IDs', () => {
  const c = context(); let storedRows = [];
  const range = {setNumberFormat() { return this; }, setValues(rows) {
      if (rows.length && rows[0].length === 1) rows.forEach((r, i) => { storedRows[i][3] = r[0]; });
      else storedRows = plain(rows); return this; }, setWrapStrategy() { return this; },
    getValues() { return storedRows; }, clearContent() { return this; }, clearDataValidations() { return this; },
    setDataValidation() { return this; }, setBackground() { return this; }};
  c.sheet_ = () => ({getRange: () => range}); c.ensureRange_ = () => range;
  c.SpreadsheetApp.newDataValidation = () => ({requireCheckbox() { return this; }, build() { return {}; }});
  const meta = {shape: 'cards', generation: '1', request_id: '5'};
  c.renderActions_(meta, {skills: [{id: 'zhiheng', name: 'zhiheng', instance_id: 9, label: '制衡'}]}, false);
  storedRows[0][3] = true;
  c.renderActions_(meta, {skills: [{id: 'zhiheng:9', name: 'zhiheng', instance_id: 9, label: '制衡'}]}, true);
  assert.equal(storedRows[0][3], true);
  assert.equal(storedRows[0][1], 'zhiheng:9');
});
test('copied document does not inherit credentials from the same property store', () => {
  const c = context(); c.put_('token', 'secret'); c.put_('session', 'session-a');
  c.changeDocument('copy-b'); assert.equal(c.getClientState().paired, false);
});
test('uncertain command persists and retry sends the original id and payload', () => {
  const c = context(); c.put_('token', 'token'); c.put_('session', 'session-a'); c.put_('command_id', '9007199254740992');
  c.fetch_ = () => { throw new Error('network unknown'); };
  assert.throws(() => c.command_('chat', {text: '一次'}));
  const pending = plain(c.pending_()); assert.equal(pending.id, '9007199254740993');
  assert.throws(() => c.command_('chat', {text: '不可取代'}));
  let received;
  c.fetch_ = (_, body) => { received = plain(body); return {api_version: 1, id: body.id, session: body.session, ok: true, result: {}}; };
  c.retryPending(); assert.deepEqual(received, pending); assert.equal(c.pending_(), null);
  received = null; c.retryPending(); assert.equal(received, null);
});

test('read-only update transport failure is safe to retry without creating a pending command', () => {
  const c = context(); c.put_('token', 'token'); c.put_('session', 'session-a'); c.put_('endpoint', 'https://host.test');
  c.UrlFetchApp = {fetch() { throw new Error('network down'); }};
  assert.throws(() => c.fetch_('/v1/updates?after=0'), /安全重試更新，沒有送出遊戲指令/);
  assert.equal(c.pending_(), null);
});

test('uncertain command transport failure keeps pending payload and directs retry of the same command', () => {
  const c = context(); c.put_('token', 'token'); c.put_('session', 'session-a'); c.put_('endpoint', 'https://host.test');
  c.UrlFetchApp = {fetch() { throw new Error('network down'); }};
  assert.throws(() => c.command_('chat', {text: 'hello'}), /重試待確認指令.*不要重新操作/);
  assert.equal(c.pending_().name, 'chat');
  assert.equal(c.pending_().args.text, 'hello');
});

test('successful polling clears read errors but keeps unresolved command warning visible', () => {
  const c = sidebarContext();
  vm.runInContext("lastError = '牌桌更新失敗'; lastErrorKind = 'poll'; call('poll', [], show, true);", c);
  assert.equal(c.element('status').className, 'status ok');
  assert.doesNotMatch(c.element('status').textContent, /牌桌更新失敗/);
  c.setPending(true);
  vm.runInContext("lastError = '牌桌更新失敗'; lastErrorKind = 'poll'; call('poll', [], show, true);", c);
  assert.equal(c.element('status').className, 'status error');
  assert.match(c.element('status').textContent, /前一遊戲指令仍待確認/);
});

test('successful polling preserves action rejection until a successful explicit action', () => {
  const c = sidebarContext();
  c.respondWithError('原生拒絕：選擇不合法');
  vm.runInContext("call('submitSheetDraft', [], show, false);", c);
  assert.match(c.element('status').textContent, /原生拒絕：選擇不合法/);
  c.respondWithError('牌桌更新失敗：連線逾時');
  vm.runInContext("call('poll', [], show, true);", c);
  assert.equal(c.element('status').className, 'status error');
  assert.match(c.element('status').textContent, /原生拒絕：選擇不合法/);
  assert.match(c.element('status').textContent, /牌桌更新失敗：連線逾時/);
  c.respondWith({state: {pending: false}, status: 'connected'});
  vm.runInContext("call('poll', [], show, true);", c);
  assert.equal(c.element('status').className, 'status error');
  assert.match(c.element('status').textContent, /原生拒絕：選擇不合法/);
  c.respondWith({state: {pending: false}, status: '操作成功'});
  vm.runInContext("call('readyFromSheet', [], show, false);", c);
  assert.equal(c.element('status').className, 'status ok');
  assert.doesNotMatch(c.element('status').textContent, /原生拒絕：選擇不合法/);
});

test('non-clean closed shutdown clears credentials but preserves visible failure', () => {
  const c = context(); c.put_('token', 'token'); c.put_('session', 'session-a'); c.put_('endpoint', 'https://host.test');
  c.fetch_ = () => ({api_version: 1, session: 'session-a', ok: false, closed: true, native_exit_code: 86});
  const result = c.disconnect();
  assert.deepEqual(plain(result), {ok: false, paired: false, error: '會話已結束，但未正常退出，請檢查主機診斷。'});
  assert.equal(c.get_('token', ''), ''); assert.equal(c.get_('session', ''), '');
  assert.equal(c.get_('last_error', ''), '會話已結束，但未正常退出，請檢查主機診斷。');
});

test('legacy non-clean shutdown with native exit code is terminal; unknown outcome retains credentials', () => {
  const c = context(); c.put_('token', 'token'); c.put_('session', 'session-a');
  c.fetch_ = () => ({api_version: 1, session: 'session-a', ok: false, native_exit_code: 86});
  assert.equal(c.disconnect().paired, false); assert.equal(c.get_('token', ''), '');
  const d = context(); d.put_('token', 'token'); d.put_('session', 'session-a');
  d.fetch_ = () => ({api_version: 1, session: 'session-a', ok: false});
  assert.equal(d.disconnect().paired, true); assert.equal(d.get_('token', ''), 'token');
});

test('onOpen installs the complete operational menu with bound handlers', () => {
  const calls = []; let menu;
  const c = context();
  c.SpreadsheetApp.getUi = () => ({createMenu(name) {
    menu = {name, addItem(label, handler) { calls.push(['item', label, handler]); return menu; },
      addSeparator() { calls.push(['separator']); return menu; },
      addToUi() { calls.push(['install']); return menu; }};
    return menu;
  }});
  c.onOpen();
  assert.equal(menu.name, 'QSanGuosha');
  assert.deepEqual(calls, [
    ['item', '建立專用工作表', 'setupWorkbook'],
    ['item', '連線與操作控制', 'showSidebar'],
    ['separator'],
    ['item', '載入房間目錄', 'catalogFromSheet'],
    ['item', '預檢選擇', 'previewSheetDraft'],
    ['item', '提交選擇', 'submitSheetDraft'],
    ['item', '取消／結束出牌', 'cancelDraft'],
    ['item', '重試待確認指令', 'retryPending'],
    ['item', '檢視目前列詳情', 'detailsFromSheet'],
    ['item', '離開並關閉會話', 'disconnect'],
    ['install']
  ]);
});

test('render projects native snapshot state and GAME_OVER winner into worksheet blocks', () => {
  const c = context(); const blocks = [];
  c.writeBlock_ = (...args) => blocks.push(args);
  c.renderActions_ = () => {};
  c.saveJson_ = () => {};
  c.json_ = () => null;
  c.render_({generation: '4', revision: '8', request_id: '12', connection: 'connected',
    interaction: {type: 'choose_player', payload: {players: []}, min: 1, max: 1},
    view: {prompt: '@resolution', prompt_text: '結算', players: [{id: 'p1', label: '主公', hp: 2, max_hp: 4, hand_count: 3,
      alive: true, general_label: '曹操', equip: ['青釭劍'], marks: {忠: 1}}],
      hand: [{id: 0, label: '殺', detail: 'basic'}], cards: [], skills: [], logs: ['勝負已定']},
    state: {game: {game_over: true, result: '主公勝'}}});
  const board = blocks.find(x => x[0] === 'QSAN Board')[4];
  assert.equal(blocks.find(x => x[1] === 'action_title')[4][1][1], '結算');
  assert.deepEqual(plain(board[1].slice(0, 3)), ['狀態', '', 'connected']);
  assert.equal(board[1][6], 'GAME_OVER');
  assert.equal(board[3][0], '勝方');
  assert.equal(board[3][2], '主公勝');
  assert.deepEqual(plain(board[4].slice(0, 7)), ['player', 'p1', '主公', '2/4', 3, '曹操；青釭劍；忠：1', '存活']);
  assert.deepEqual(plain(board[5].slice(0, 3)), ['card', 0, '殺']);
});

test('render leaves native pre-selection hp fields blank', () => {
  const c = context(); const blocks = [];
  c.writeBlock_ = (...args) => blocks.push(args); c.renderActions_ = () => {}; c.saveJson_ = () => {};
  c.json_ = () => null;
  c.render_({generation: '1', revision: '1', request_id: '1', connection: 'connected', interaction: {},
    view: {players: [{id: 'p1', label: '未選將', equip: [{label: '青釭劍', image: '/opaque'}]}]}, state: {game: {}}});
  const board = blocks.find(x => x[0] === 'QSAN Board')[4];
  assert.deepEqual(plain(board[4].slice(0, 5)), ['player', 'p1', '未選將', '', '']);
  assert.equal(board[4][5], '青釭劍');
});

test('skill invocation prompt resolves the native skill identity and keeps explicit prompts', () => {
  const c = context();
  assert.equal(c.interactionPrompt_({interaction: {type: 'skill_invoke', skill: 's4_jiushi'},
    view: {skills: [{name: 's4_jiushi', label: '酒詩'}]}}), '是否發動技能「酒詩」？');
  assert.equal(c.interactionPrompt_({interaction: {type: 'skill_invoke', skill: 'unknown_skill'}, view: {skills: []}}),
    '是否發動技能「unknown_skill」？');
  assert.equal(c.interactionPrompt_({interaction: {type: 'skill_invoke', skill: 's4_jiushi', prompt: '原生提示'},
    view: {prompt_text: '格式化提示', skills: [{name: 's4_jiushi', label: '酒詩'}]}}), '格式化提示');
});

test('poll returns the same skill-aware prompt used by the worksheet', () => {
  const c = context(); c.put_('token', 'token'); c.put_('session', 'session-a');
  c.render_ = () => {};
  c.fetch_ = () => ({api_version: 1, session: 'session-a', sequence: '1', connection: 'connected',
    snapshot: {generation: '1', revision: '2', request_id: '3', interaction: {type: 'skill_invoke', skill: 's4_jiushi'},
      view: {skills: [{name: 's4_jiushi', label: '酒詩'}]}, state: {game: {}}}});
  assert.equal(c.poll().prompt, '是否發動技能「酒詩」？');
});

test('actions checkbox writes booleans with General number format', () => {
  const c = context(); let storedRows = [], formats = [];
  const range = {setNumberFormat(value) { formats.push(value); return this; }, setValues(rows) {
      if (rows.length && rows[0].length === 1 && storedRows.length) rows.forEach((r, i) => { storedRows[i][3] = r[0]; });
      else storedRows = plain(rows); return this; }, getValues() { return storedRows; },
    clearContent() { return this; }, clearDataValidations() { return this; }, setDataValidation() { return this; }, setBackground() { return this; }, setWrapStrategy() { return this; }};
  c.sheet_ = () => ({getRange() { return range; }}); c.ensureRange_ = () => range;
  c.SpreadsheetApp.newDataValidation = () => ({requireCheckbox() { return this; }, build() { return {}; }});
  c.renderActions_({shape: 'option', generation: '1', request_id: '1'}, {options: [{id: 'caocao', label: '曹操'}]}, false);
  assert.equal(storedRows[0][3], false); assert.ok(formats.includes('General'));
});

test('board marks summary bounds internal turn marks without changing source data', () => {
  const c = context();
  const marks = {'turn-a': 1, '@visible': 2, 'turn-b': 3, 'turn-c': 4, 'turn-d': 5};
  const summary = c.marksSummary_(marks);
  assert.ok(summary.startsWith('@visible：2'));
  assert.ok(summary.length <= 180);
  assert.match(summary, /共5項，詳情可查/);
  assert.deepEqual(marks, {'turn-a': 1, '@visible': 2, 'turn-b': 3, 'turn-c': 4, 'turn-d': 5});
});

test('submit uses the preflight identity and exact draft for native select then submit', () => {
  const c = context(); const calls = []; const selected = {
    meta: {generation: '7', revision: '9', request_id: '11'},
    draft: {cards: [0], targets: ['p2'], skill_name: 'zhiheng', skill_instance_id: 3}
  };
  c.readDraft_ = () => selected;
  c.command_ = (name, args, identity) => {
    calls.push({name, args: plain(args), identity: plain(identity)});
    return name === 'select' ? {selection: {generation: '7', revision: '9', request_id: '11', can_confirm: true}} : {};
  };
  c.applySelection_ = () => {};
  c.outcome_ = (status) => ({status});
  const result = c.submitSheetDraft();
  assert.equal(result.status, '已提交，等待伺服器更新。');
  assert.equal(calls.length, 2);
  for (const call of calls) assert.deepEqual(call.identity, selected.meta);
  assert.deepEqual(calls[0].args, {request_id: '11', draft: selected.draft});
  assert.deepEqual(calls[1].args, {request_id: '11', draft: selected.draft});
});
