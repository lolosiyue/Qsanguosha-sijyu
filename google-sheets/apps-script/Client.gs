/** Player-scoped transport; game rules and hidden information stay native. */
const QSAN = Object.freeze({API: 1, VERSION: 'sheets-v1', FIRST: 7, COLS: 11,
  SHEETS: ['QSAN Board', 'QSAN Actions', 'QSAN Room', 'QSAN Catalog', 'QSAN Details', 'QSAN Log']});
function digest_(text) { return Utilities.computeDigest(Utilities.DigestAlgorithm.SHA_256, String(text)).map(x => ('0' + ((x + 256) % 256).toString(16)).slice(-2)).join(''); }
function prefix_() { return 'QSAN_' + digest_(SpreadsheetApp.getActive().getId()).slice(0, 24) + '_'; }
function get_(key, fallback) { return PropertiesService.getUserProperties().getProperty(prefix_() + key) || fallback; }
function put_(key, value) { PropertiesService.getUserProperties().setProperty(prefix_() + key, String(value)); }
function drop_(key) { PropertiesService.getUserProperties().deleteProperty(prefix_() + key); }
function locked_(fn) {
  const lock = LockService.getDocumentLock();
  if (!lock.tryLock(1000)) throw new Error(qsanText_('busy'));
  try { return fn(); } finally { lock.releaseLock(); }
}
function json_(key, fallback) { const raw = get_(key, ''); return raw ? JSON.parse(raw) : fallback; }
function saveJson_(key, value) {
  const raw = JSON.stringify(value);
  if (Utilities.newBlob(raw).getBytes().length > 8000) throw new Error(qsanText_('stateTooLarge'));
  put_(key, raw);
}
function storePending_(body) {
  const text = Utilities.base64Encode(Utilities.newBlob(JSON.stringify(body)).getBytes());
  if (text.length > 180000) throw new Error(qsanText_('pendingTooLarge'));
  const chunks = Math.ceil(text.length / 6000), values = {}, p = prefix_();
  for (let i = 0; i < chunks; ++i) values[p + 'pending_' + i] = text.slice(i * 6000, (i + 1) * 6000);
  PropertiesService.getUserProperties().setProperties(values);
  // Publish only after all chunks exist. No request is sent before this.
  saveJson_('pending', {chunks: chunks, hash: digest_(text)});
}
function pending_() {
  const meta = json_('pending', null); if (!meta) return null;
  let text = ''; for (let i = 0; i < meta.chunks; ++i) text += get_('pending_' + i, '');
  if (digest_(text) !== meta.hash) throw new Error(qsanText_('pendingCorrupt'));
  return JSON.parse(Utilities.newBlob(Utilities.base64Decode(text)).getDataAsString('UTF-8'));
}
function clearPending_() {
  const meta = json_('pending', null); drop_('pending');
  if (meta) for (let i = 0; i < meta.chunks; ++i) drop_('pending_' + i);
}
function decimal_(value) { return typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value); }
function nextId_() {
  const old = get_('command_id', '0'); if (!decimal_(old)) throw new Error(qsanText_('commandIdCorrupt'));
  // Never round request identities through Number / JSON numeric values.
  const chars = old.split(''); let carry = 1;
  for (let i = chars.length - 1; i >= 0 && carry; --i) { const n = Number(chars[i]) + carry; chars[i] = String(n % 10); carry = n > 9 ? 1 : 0; }
  if (carry) chars.unshift('1'); const id = chars.join('');
  if (id.length > 20 || (id.length === 20 && id > '18446744073709551615')) throw new Error(qsanText_('commandIdFull'));
  put_('command_id', id); return id;
}
function endpoint_(input) {
  const value = String(input === undefined ? get_('endpoint', '') : input).trim().replace(/\/+$/, '');
  if (!/^https:\/\/[A-Za-z0-9.-]+(?::[0-9]{1,5})?(?:\/[A-Za-z0-9_~.-]+)*$/.test(value)) throw new Error(qsanText_('endpointInvalid'));
  return value;
}
function requirePaired_() { if (!get_('token', '') || !get_('session', '')) throw new Error(qsanText_('pairRequired')); }
function fetch_(path, body, base) {
  const anonymous = base !== undefined; if (!anonymous) requirePaired_();
  const headers = {Accept: 'application/json'};
  if (!anonymous) { headers.Authorization = 'Bearer ' + get_('token', ''); headers['X-QSan-Session'] = get_('session', ''); }
  const args = {method: body === undefined ? 'get' : 'post', muteHttpExceptions: true,
    followRedirects: false, validateHttpsCertificates: true, headers: headers, contentType: 'application/json'};
  if (body !== undefined) args.payload = JSON.stringify(body);
  let response;
  try { response = UrlFetchApp.fetch(endpoint_(base) + path, args); }
  catch (_) {
    if (body === undefined && path.indexOf('/v1/updates?') === 0)
      throw new Error(qsanText_('updateFailed'));
    if (path === '/v1/commands')
      throw new Error(qsanText_('commandUnknown'));
    if (path === '/v1/shutdown')
      throw new Error(qsanText_('shutdownUnknown'));
    if (path === '/v1/pair')
      throw new Error(qsanText_('pairUnknown'));
    throw new Error(qsanText_('requestFailed'));
  }
  const status = response.getResponseCode();
  if (status >= 300 && status < 400) throw new Error(qsanText_('redirectRejected'));
  let value; try { value = JSON.parse(response.getContentText()); } catch (_) { throw new Error(qsanText_('invalidResponse')); }
  if (status < 200 || status >= 300) { const error = new Error(String(value.error || qsanText_('serviceUnavailable'))); error.httpStatus = status; throw error; }
  return value;
}
function getClientState() { return {paired: !!get_('token', ''), endpoint: get_('endpoint', ''), pending: !!get_('pending', ''), lastError: get_('last_error', '')}; }
function outcome_(status, extra) { return Object.assign({state: getClientState(), status: status}, extra || {}); }
function onOpen() {
  SpreadsheetApp.getUi().createMenu(qsanText_('menuRoot')).addItem(qsanText_('menuSetup'), 'setupWorkbook')
    .addItem(qsanText_('menuConnect'), 'showSidebar').addSeparator().addItem(qsanText_('menuCatalog'), 'catalogFromSheet')
    .addItem(qsanText_('menuPreview'), 'previewSheetDraft').addItem(qsanText_('menuSubmit'), 'submitSheetDraft')
    .addItem(qsanText_('menuCancel'), 'cancelDraft').addItem(qsanText_('menuRetry'), 'retryPending')
    .addItem(qsanText_('menuDetails'), 'detailsFromSheet').addItem(qsanText_('menuDisconnect'), 'disconnect').addToUi();
}
function showSidebar() { SpreadsheetApp.getUi().showSidebar(HtmlService.createHtmlOutputFromFile('Sidebar').setTitle(qsanText_('sidebarTitle'))); }
function pair(base, code) {
  return locked_(function() {
    if (get_('token', '')) throw new Error(qsanText_('alreadyPaired'));
    base = endpoint_(base); code = String(code || '').trim();
    if (!/^[A-Za-z0-9_-]{16,128}$/.test(code)) throw new Error(qsanText_('invalidPairCode'));
    const fingerprint = digest_(base + '\n' + code); let pending = json_('pair', null);
    if (pending && pending.fingerprint !== fingerprint) throw new Error(qsanText_('retryPair'));
    if (!pending) { pending = {fingerprint: fingerprint, nonce: Utilities.getUuid()}; saveJson_('pair', pending); }
    const reply = fetch_('/v1/pair', {code: code, pair_nonce: pending.nonce}, base);
    if (reply.api_version !== 1 || typeof reply.session !== 'string' || !/^[A-Za-z0-9_-]{32,128}$/.test(reply.token || '')) throw new Error(qsanText_('invalidPairResponse'));
    clearPending_(); ['receipt', 'meta', 'preflight', 'actions_signature'].forEach(drop_);
    put_('endpoint', base); put_('token', reply.token); put_('session', reply.session);
    put_('command_id', '0'); put_('sequence', '0'); drop_('pair'); drop_('last_error');
    setup_(); return getClientState();
  });
}
function abandonPair() { return locked_(function() { if (get_('token', '')) throw new Error(qsanText_('pairedLeave')); drop_('pair'); return outcome_(qsanText_('pairCleared')); }); }
function command_(name, args, identity) {
  requirePaired_(); if (pending_()) throw new Error(qsanText_('commandPending'));
  const meta = identity || json_('meta', {generation: '0', revision: '0'});
  const body = {api_version: 1, session: get_('session', ''), id: nextId_(), generation: meta.generation,
    revision: meta.revision, name: name, args: args || {}};
  storePending_(body); return finishCommand_(body);
}
function finishCommand_(body) {
  let reply;
  try { reply = fetch_('/v1/commands', body); }
  catch (e) {
    // 5xx/transport failures may follow execution. Keep their original envelope.
    if ([400, 401, 403, 404, 410, 413, 415].indexOf(e.httpStatus) >= 0) clearPending_();
    drop_('preflight'); put_('last_error', e.message); throw e;
  }
  if (reply.api_version !== 1 || reply.id !== body.id || reply.session !== body.session || typeof reply.ok !== 'boolean') throw new Error(qsanText_('identityMismatch'));
  // Retain a compact receipt for a lost google.script.run response, never a
  // full catalog/snapshot in the limited user property store.
  saveJson_('receipt', {id: body.id, name: body.name, ok: reply.ok, error: String(reply.error || '')});
  clearPending_();
  if (!reply.ok) { drop_('preflight'); throw new Error(String(reply.error || qsanText_('nativeRejected'))); }
  drop_('last_error'); return reply.result || {};
}
function retryPending() {
  return locked_(function() {
    const body = pending_();
    if (body) { const result = finishCommand_(body); if (body.name === 'select' && result.selection) applySelection_(result.selection, body.args.draft); return outcome_(qsanText_('retryConfirmed')); }
    const receipt = json_('receipt', null);
    return outcome_(receipt ? (receipt.ok ? qsanText_('lastSuccess') : qsanText_('lastRejected') + receipt.error) : qsanText_('noPending'));
  });
}
function poll() {
  return locked_(function() {
    const reply = fetch_('/v1/updates?after=' + get_('sequence', '0'));
    if (reply.api_version !== 1 || reply.session !== get_('session', '') || !decimal_(reply.sequence) || !reply.snapshot) throw new Error(qsanText_('updateIdentityMismatch'));
    render_(reply.snapshot); put_('sequence', reply.sequence);
    const snap = reply.snapshot, game = (snap.state || {}).game || {};
    return outcome_(snap.connection || qsanText_('updated'), {prompt: interactionPrompt_(snap), gameOver: game.game_over === true, winner: text_(game.result || '')});
  });
}
function applySelection_(selection, draft) {
  const meta = json_('meta', {});
  if (selection.generation !== meta.generation || selection.revision !== meta.revision || selection.request_id !== meta.request_id) throw new Error(qsanText_('preflightExpired'));
  saveJson_('preflight', {hash: digest_(JSON.stringify({meta: meta, draft: draft})), can_confirm: selection.can_confirm === true});
  renderPreflight_(qsanText_('preflight') + (selection.can_confirm ? qsanText_('canConfirm') : String(selection.reason || qsanText_('continueSelecting'))));
  if (selection.ui) renderActions_(meta, selection.ui, true);
}
function previewSheetDraft() {
  return locked_(function() { const selected = readDraft_(), result = command_('select', {request_id: selected.meta.request_id, draft: selected.draft}, selected.meta);
    applySelection_(result.selection || {}, selected.draft); return outcome_(result.selection.can_confirm ? qsanText_('preflightPassed') : String(result.selection.reason || qsanText_('continueSelecting'))); });
}
function submitSheetDraft() {
  return locked_(function() {
    const selected = readDraft_(), result = command_('select', {request_id: selected.meta.request_id, draft: selected.draft}, selected.meta);
    applySelection_(result.selection || {}, selected.draft);
    if (!result.selection.can_confirm) throw new Error(String(result.selection.reason || qsanText_('incompleteSelection')));
    // Submit the exact captured identity and draft that native preflight saw.
    command_('submit', {request_id: selected.meta.request_id, draft: selected.draft}, selected.meta);
    drop_('preflight'); return outcome_(qsanText_('submitted'));
  });
}
function cancelDraft() { return locked_(function() { const meta = json_('meta', {}); if (!meta.cancelable) throw new Error(qsanText_('cannotCancel')); command_('cancel', {request_id: meta.request_id}, meta); drop_('preflight'); return outcome_(qsanText_('cancelSent')); }); }
function control_(name, args) { return locked_(function() { command_(name, args); return outcome_(qsanText_('operationSent')); }); }
function readyFromSheet() { return control_('ready', {ready: true}); }
function unreadyFromSheet() { return control_('ready', {ready: false}); }
function trustFromSheet() { return control_('trust', {enabled: true}); }
function untrustFromSheet() { return control_('trust', {enabled: false}); }
function surrenderFromSheet() { return control_('surrender', {}); }
function reconnect() { return control_('reconnect', {}); }
function roomValue_(row) { return sheet_('QSAN Room').getRange(row, 2).getValue(); }
function addRobotFromSheet() { return control_('add_robot', {count: integer_(roomValue_(6), 0)}); }
function chatFromSheet() { return control_('chat', {text: String(roomValue_(8))}); }
function connectFromSheet() { return control_('connect', {host: String(roomValue_(4)), port: integer_(roomValue_(5), 1), name: String(roomValue_(2)), avatar: String(roomValue_(3))}); }
function hostFromSheet() {
  return locked_(function() {
    const sheet = sheet_('QSAN Room'), rows = sheet.getRange(12, 1, Math.max(1, sheet.getLastRow() - 11), 3).getValues(), settings = {};
    rows.forEach(r => { if (!r[0]) return; const key = String(r[0]), type = String(r[1]), value = r[2];
      if (qsanSettingType_(type, 'keep')) return;
      if (qsanSettingType_(type, 'bool')) settings[key] = boolean_(value);
      else if (qsanSettingType_(type, 'integer')) settings[key] = integer_(value, -2147483648);
      else if (qsanSettingType_(type, 'list')) settings[key] = String(value).split('\n').filter(x => x !== '');
      else settings[key] = String(value);
    });
    command_('host', {private: true, name: String(roomValue_(2)), avatar: String(roomValue_(3)), robots: integer_(roomValue_(6), 0), settings: settings});
    return outcome_(qsanText_('hosting'));
  });
}
function disconnect() {
  return locked_(function() {
    if (!get_('token', '')) return {ok: true, paired: false};
    let reply; try { reply = fetch_('/v1/shutdown', {}); } catch (e) { put_('last_error', e.message); return {ok: false, paired: true, error: e.message}; }
    if (reply.api_version !== 1 || reply.session !== get_('session', '') || typeof reply.ok !== 'boolean') {
      put_('last_error', qsanText_('shutdownIdentityMismatch')); return {ok: false, paired: true, error: get_('last_error', '')};
    }
    if (reply.ok === true) {
      clearPending_(); ['token', 'session', 'meta', 'preflight', 'receipt', 'command_id', 'sequence', 'last_error'].forEach(drop_);
      return {ok: true, paired: false};
    }
    const terminal = reply.closed === true || (reply.closed === undefined && Number.isInteger(reply.native_exit_code));
    const error = terminal ? qsanText_('sessionEndedUnclean') : qsanText_('sessionEndUnknown');
    put_('last_error', error);
    if (terminal) {
      clearPending_(); ['token', 'session', 'meta', 'preflight', 'receipt', 'command_id', 'sequence'].forEach(drop_);
      return {ok: false, paired: false, error: error};
    }
    return {ok: false, paired: true, error: error};
  });
}
