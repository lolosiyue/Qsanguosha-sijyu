/** Cells are the playable UI. Remote text is always written as literal text. */
function text_(value) {
  if (value === null || value === undefined) return '';
  if (Array.isArray(value)) return value.map(text_).join('、');
  if (typeof value === 'object') return Object.keys(value).map(k => k + '：' + text_(value[k])).join('；');
  return String(value);
}
function equipLabels_(value) {
  return (Array.isArray(value) ? value : []).map(item => {
    if (item && typeof item === 'object') return item.label || item.name || '';
    return item == null ? '' : String(item);
  }).filter(Boolean).join('、');
}
function marksSummary_(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return '';
  const keys = Object.keys(value), ordered = keys.slice().sort((a, b) => {
    const ap = a.charAt(0) === '@', bp = b.charAt(0) === '@';
    return ap === bp ? 0 : (ap ? -1 : 1);
  });
  const parts = [];
  for (const key of ordered) {
    const item = key + '：' + text_(value[key]);
    if (parts.length >= 3 || (parts.join('；') + (parts.length ? '；' : '') + item).length > 180) break;
    parts.push(item);
  }
  const result = parts.join('；');
  if (ordered.length <= parts.length) return result;
  const suffix = '…共' + ordered.length + '項，詳情可查';
  const room = Math.max(0, 180 - suffix.length - (result ? 1 : 0));
  return result.slice(0, room) + (result ? '；' : '') + suffix;
}
function safe_(value) {
  if (typeof value === 'boolean' || typeof value === 'number') return value;
  const text = text_(value).slice(0, 30000);
  return /^\s*[=+@-]/.test(text) ? "'" + text : text;
}
function sheet_(name) {
  const sheet = SpreadsheetApp.getActive().getSheetByName(name);
  if (!sheet || !sheet.getDeveloperMetadata().some(x => x.getKey() === 'QSAN_OWNER' && x.getValue() === QSAN.VERSION))
    throw new Error('請先建立專用工作表；同名既有工作表不會被覆寫。');
  return sheet;
}
function ensureRange_(sheet, row, col, height, width) {
  if (sheet.getMaxRows() < row + height - 1) sheet.insertRowsAfter(sheet.getMaxRows(), row + height - 1 - sheet.getMaxRows());
  if (sheet.getMaxColumns() < col + width - 1) sheet.insertColumnsAfter(sheet.getMaxColumns(), col + width - 1 - sheet.getMaxColumns());
  return sheet.getRange(row, col, height, width);
}
function writeBlock_(name, key, row, col, rows, width) {
  const values = rows.map(r => Array.from({length: width}, (_, i) => safe_(r[i] === undefined ? '' : r[i])));
  const signature = digest_(JSON.stringify(values)), storage = 'block_' + key;
  if (get_(storage, '') === signature) return;
  const sheet = sheet_(name), old = Number(get_(storage + '_rows', '0'));
  if (old > values.length) sheet.getRange(row + values.length, col, old - values.length, width).clearContent().clearDataValidations();
  if (values.length) ensureRange_(sheet, row, col, values.length, width).setNumberFormat('@').setValues(values);
  put_(storage, signature); put_(storage + '_rows', values.length);
}
function setupWorkbook() { return locked_(function() { setup_(); return outcome_('專用工作表已準備。'); }); }
function setup_() {
  const ss = SpreadsheetApp.getActive();
  QSAN.SHEETS.forEach(name => {
    let sheet = ss.getSheetByName(name);
    if (sheet) {
      sheet_(name); sheet.setColumnWidth(2, 180); sheet.setColumnWidth(3, 260); sheet.setColumnWidth(8, 330); sheet.setColumnWidth(11, 90);
      sheet.getRange(1, 1, sheet.getMaxRows(), 10).setWrap(true);
      sheet.getRange(1, 11, sheet.getMaxRows(), 1).setWrapStrategy(SpreadsheetApp.WrapStrategy.CLIP);
      if (name === 'QSAN Log') sheet.setColumnWidth(1, 900); return;
    }
    sheet = ss.insertSheet(name); sheet.addDeveloperMetadata('QSAN_OWNER', QSAN.VERSION);
    sheet.setFrozenRows(name === 'QSAN Actions' ? 6 : 1);
    sheet.setColumnWidths(1, 11, 110); sheet.setColumnWidth(2, 180); sheet.setColumnWidth(3, 260); sheet.setColumnWidth(8, 330); sheet.setColumnWidth(11, 90);
    if (name === 'QSAN Log') sheet.setColumnWidth(1, 900);
    sheet.getRange(1, 1, 1, 11).setBackground('#17364b').setFontColor('#ffffff').setFontWeight('bold');
    sheet.getRange(1, 1, sheet.getMaxRows(), 10).setVerticalAlignment('top').setWrap(true);
    sheet.getRange(1, 11, sheet.getMaxRows(), 1).setVerticalAlignment('top').setWrapStrategy(SpreadsheetApp.WrapStrategy.CLIP);
    // A recreated sheet needs repopulation even if its old signatures survived.
    const props = PropertiesService.getUserProperties(), p = prefix_();
    Object.keys(props.getProperties()).filter(k => k.startsWith(p + 'block_')).forEach(k => props.deleteProperty(k));
    drop_('actions_signature');
  });
  const room = sheet_('QSAN Room');
  if (!room.getRange(1, 1).getValue()) {
    writeBlock_('QSAN Room', 'room_initial', 1, 1, [
      ['連線與房間', '可編輯值', '說明'], ['玩家名稱', 'Sheets', ''], ['頭像武將 ID', 'caocao', '目錄列出可用 ID'],
      ['遊戲伺服器', '127.0.0.1', '加入既有伺服器；須主機 allow-game 允許'], ['遊戲埠', 9527, ''],
      ['AI 人數', 4, '開房加入人數；0 表示補滿'], ['操作說明', '先開啟側邊欄', '配對網址與憑證不放儲存格'],
      ['聊天內容', '', '輸入後按側邊欄送出聊天'], ['房間設定', '', '清單每行一個 ID；設定由原生驗證'],
      ['目錄', 'QSanGuosha 選單 → 載入房間目錄', '提供模式、武將、牌包與其他設定'],
      ['設定名稱', '型別', '值'], ['ServerName', '文字', 'Sheets'], ['GameMode', '文字', '05p'],
      ['OperationNoLimit', '布林', true], ['CountDownSeconds', '整數', 0]
    ], 3);
  }
  writeBlock_('QSAN Actions', 'action_header', 6, 1,
    [['項目類型', '識別碼', '名稱', '選取', '順序（可填 1,3）', 'top／bottom／角色', '可用', '說明', '技能名稱', '技能實例', '圖片識別碼']], 11);
  writeBlock_('QSAN Details', 'detail_header', 1, 1, [['詳情', '在牌桌、互動或目錄選取一列，再按檢視詳情']], 2);
}
function meta_(snapshot) {
  const req = snapshot.interaction || {}, payload = req.payload || {};
  ['generation', 'revision', 'request_id'].forEach(k => { if (!decimal_(snapshot[k])) throw new Error('原生互動序號格式錯誤。'); });
  return {generation: snapshot.generation, revision: snapshot.revision, request_id: snapshot.request_id,
    type: req.type || 'none', shape: shape_(req), cancelable: req.cancelable === true,
    roles: payload.roles || [], generals: payload.generals || [], enumerated: payload.enumerated !== false,
    min: req.min, max: req.max};
}
function render_(snapshot) {
  const meta = meta_(snapshot), previous = json_('meta', null), req = snapshot.interaction || {}, view = snapshot.view || {};
  const game = (snapshot.state || {}).game || {};
  const sameRequest = previous && previous.generation === meta.generation && previous.request_id === meta.request_id;
  if (!previous || previous.revision !== meta.revision || !sameRequest) {
    drop_('preflight'); writeBlock_('QSAN Actions', 'preflight_text', 3, 1, [['預檢', '待重新預檢']], 2);
  }
  const prompt = view.prompt_text || view.prompt || req.prompt || (req.ui && req.ui.prompt) || (req.payload && req.payload.prompt) || '';
  writeBlock_('QSAN Actions', 'action_title', 1, 1,
    [['目前互動', meta.type], ['提示', prompt || (meta.type === 'none' ? '等待互動' : '目前輪到你處理此互動。')]], 2);
  writeBlock_('QSAN Actions', 'action_help', 4, 1,
    [['選擇範圍', String(meta.min === undefined ? '' : meta.min) + ' ～ ' + String(meta.max === undefined ? '' : meta.max)],
     ['操作', meta.shape === 'unsupported' ? '此互動不支援，未送出回覆。' : '勾選後可填順序；技能先預檢取得宣告選項。觀星填 top/bottom；角色分配填角色 ID。']], 2);
  const ui = Object.assign({}, req.ui || {});
  if ((!ui.skills || !ui.skills.length) && meta.shape === 'cards') ui.skills = view.skills || [];
  renderActions_(meta, ui, !!sameRequest);
  const board = [['牌桌', '識別碼', '名稱', '體力', '手牌數', '武將／裝備／標記', '狀態', '說明', '技能名稱', '實例', '圖片'],
    ['狀態', '', snapshot.connection || '', '', '', '', game.game_over ? 'GAME_OVER' : (view.status || game.status || ''), '', '', '', ''],
    ['回合／牌堆', game.round === undefined ? '' : game.round, game.draw_pile_count === undefined ? '' : '牌堆剩餘：' + game.draw_pile_count, '', '', '', '', '', '', '', ''],
    ['勝方', '', text_(game.result || ''), '', '', '', '', '', '', '', '']];
  (view.players || []).forEach(p => {
    const hp = p.hp === undefined || p.hp === null ? '' : p.hp;
    const maxHp = p.max_hp === undefined || p.max_hp === null ? '' : p.max_hp;
    const handCount = p.hand_count === undefined || p.hand_count === null ? '' : p.hand_count;
    const label = p.label || p.id;
    board.push(['player', p.id, label, (hp === '' && maxHp === '') ? '' : String(hp) + '/' + String(maxHp), handCount,
      [p.general_label, p.deputy_general_label, equipLabels_(p.equip), marksSummary_(p.marks)].filter(Boolean).join('；'),
      (p.alive === false ? '陣亡' : '存活') + (p.role ? '；身分：' + ({lord:'主公', loyalist:'忠臣', rebel:'反賊', renegade:'內奸'}[p.role] || p.role) : '') + (p.phase && p.phase !== 'not_active' ? '；階段：' + p.phase : ''), p.detail || '', '', '', p.general_image || '']);
  });
  (view.hand || []).forEach(c => board.push(['card', c.id, c.label || c.name, '', '', '本人手牌', '', c.detail || '', '', '', c.image || '']));
  (view.cards || []).forEach(c => board.push(['card', c.id, c.label || c.name, '', '', '公開牌區', '', c.detail || '', '', '', c.image || '']));
  (view.skills || []).forEach(s => board.push(['skill', s.name || s.id, s.label, '', '', '本人技能', '', s.detail || '', s.name || s.id, s.instance_id || s.skill_instance_id || 0, '']));
  writeBlock_('QSAN Board', 'board', 1, 1, board, 11);
  writeBlock_('QSAN Log', 'log', 1, 1, [['戰報']].concat((view.logs || []).map(x => [x])), 1);
  // Commit the new identity only once its candidate rows were written.
  saveJson_('meta', meta);
}
function renderActions_(meta, ui, preserve) {
  const rows = [], add = (bank, items) => (items || []).forEach((x, index) => {
    if (typeof x === 'string' || typeof x === 'number') x = {id: String(x), label: String(x)};
    const instance = x.instance_id || x.skill_instance_id || 0;
    const skillName = x.name || x.skill_name || (bank === 'skill' ? String(x.id || '').replace(/:[0-9]+$/, '') : '');
    // view.skills uses name while preflight uses name:instance. Normalize both
    // so a legality query never clears the player's selected skill.
    const id = bank === 'skill' ? skillName + ':' + instance : (x.id === undefined ? String(x.name || '') : String(x.id));
    rows.push([bank, id, x.label || x.name || x.id || '',
      bank === 'rearrange' || bank === 'assignment', bank === 'rearrange' ? String(index + 1) : '', bank === 'rearrange' ? 'top' : '',
      x.enabled !== false, x.detail || '', skillName, instance, x.image || '']);
  });
  if (meta.shape === 'option') { add('option', ui.options); if (!meta.enumerated) add('option', [{id: '', label: '自行輸入選項識別碼（第二欄）'}]); }
  if (['cards', 'distribution'].indexOf(meta.shape) >= 0) add('card', ui.cards);
  if (['cards', 'distribution', 'players'].indexOf(meta.shape) >= 0) add('player', ui.players);
  if (meta.shape === 'cards') { add('skill', ui.skills); add('declaration', ui.declarations); }
  if (meta.shape === 'assignment') add('assignment', ui.players);
  if (meta.shape === 'rearrangement') add('rearrange', ui.cards);
  if (meta.shape === 'general_arrangement') add('general', meta.generals);
  const signature = digest_(JSON.stringify({schema: 'actions-checkbox-v2', generation: meta.generation, request: meta.request_id, rows: rows}));
  if (preserve && get_('actions_signature', '') === signature) return;
  const sheet = sheet_('QSAN Actions'), count = Number(get_('action_rows', '0')), old = {};
  if (preserve && count) sheet.getRange(QSAN.FIRST, 1, count, QSAN.COLS).getValues().forEach(r => { old[r[0] + '\n' + r[1] + '\n' + r[9]] = r; });
  rows.forEach(r => { const prior = old[r[0] + '\n' + r[1] + '\n' + r[9]]; if (prior) {
    r[3] = prior[3] === true || String(prior[3]).toLowerCase() === 'true' || String(prior[3]) === '是';
    r[4] = prior[4]; r[5] = prior[5];
  } else r[3] = Boolean(r[3]); });
  if (count) sheet.getRange(QSAN.FIRST, 1, count, QSAN.COLS).clearContent().clearDataValidations();
  if (rows.length) {
    ensureRange_(sheet, QSAN.FIRST, 1, rows.length, QSAN.COLS).setNumberFormat('@').setValues(rows.map(r => r.map(safe_)));
    // Column D is a real boolean checkbox column; text format turns false into a literal string.
    sheet.getRange(QSAN.FIRST, 4, rows.length, 1).setNumberFormat('General').setValues(rows.map(r => [Boolean(r[3])]));
    // Validation creates checkboxes without resetting already selected values.
    sheet.getRange(QSAN.FIRST, 4, rows.length, 1).setDataValidation(SpreadsheetApp.newDataValidation().requireCheckbox().build());
    sheet.getRange(QSAN.FIRST, 4, rows.length, 3).setBackground('#e9f3ff');
    sheet.getRange(QSAN.FIRST, 11, rows.length, 1).setWrapStrategy(SpreadsheetApp.WrapStrategy.CLIP);
    if (meta.shape === 'rearrangement') sheet.getRange(QSAN.FIRST, 6, rows.length, 1).setDataValidation(SpreadsheetApp.newDataValidation().requireValueInList(['top', 'bottom'], true).setAllowInvalid(false).build());
    if (meta.shape === 'assignment' && meta.roles.length) sheet.getRange(QSAN.FIRST, 6, rows.length, 1).setDataValidation(SpreadsheetApp.newDataValidation().requireValueInList(meta.roles, true).setAllowInvalid(false).build());
  }
  put_('action_rows', rows.length); put_('actions_signature', signature);
}
function catalogFromSheet() {
  return locked_(function() {
    const catalog = command_('catalog', {}), rows = [['類型', '識別碼', '名稱', '資訊']];
    [['mode', catalog.modes], ['general', catalog.generals], ['package', catalog.packages]].forEach(bank => {
      (bank[1] || []).forEach(item => rows.push([bank[0], item.id, item.label, text_(item.metadata || {player_count: item.player_count || ''})]));
    });
    writeBlock_('QSAN Catalog', 'catalog', 1, 1, rows, 4);
    const room = sheet_('QSAN Room'), last = room.getLastRow(), existing = new Set(room.getRange(12, 1, Math.max(1, last - 11), 1).getValues().map(r => String(r[0])));
    const more = [];
    Object.keys(catalog.settings || {}).forEach(key => {
      if (existing.has(key)) return;
      const v = catalog.settings[key]; let type = '文字', value = v;
      if (typeof v === 'boolean') type = '布林';
      else if (typeof v === 'number' && Number.isInteger(v)) type = '整數';
      else if (Array.isArray(v) && v.every(x => typeof x === 'string')) { type = '清單'; value = v.join('\n'); }
      else if (typeof v === 'object' || typeof v === 'number') { type = '保留'; value = '沿用原生預設；此複合設定不可在儲存格編輯'; }
      more.push([key, type, value]);
    });
    if (more.length) ensureRange_(room, last + 1, 1, more.length, 3).setNumberFormat('@').setValues(more.map(r => r.map(safe_)));
    return outcome_('目錄已寫入 QSAN Catalog；額外設定加入 QSAN Room。');
  });
}
function detailsFromSheet() {
  return locked_(function() {
    const range = SpreadsheetApp.getActiveRange(); if (!range) throw new Error('請先選取一個項目列。');
    const sheet = range.getSheet(), name = sheet.getName();
    if (['QSAN Board', 'QSAN Actions', 'QSAN Catalog'].indexOf(name) < 0) throw new Error('請在牌桌、互動或目錄選取項目。');
    sheet_(name); const row = sheet.getRange(range.getRow(), 1, 1, 11).getValues()[0];
    let kind = String(row[0]), key = String(row[1]);
    if (kind === 'rearrange') kind = 'card';
    if (kind === 'assignment') kind = 'player';
    if (kind === 'skill') key = String(row[8] || key);
    if (kind === 'option') kind = 'general';
    const supported = ['card', 'player', 'skill', 'general', 'pile'];
    let result;
    if (supported.indexOf(kind) >= 0) result = command_('details', {kind: kind, key: key});
    else result = {label: row[2], detail: row[7] || row[3]};
    const rows = [['項目', result.label || row[2]], ['識別碼', key]];
    Object.keys(result).filter(k => k !== 'image').forEach(k => rows.push([k, text_(result[k])]));
    writeBlock_('QSAN Details', 'details', 2, 1, rows, 2);
    const asset = result.image || row[10]; let image = '';
    if (typeof asset === 'string' && /^\/v1\/assets\/[0-9a-f]{64}$/.test(asset)) {
      const response = UrlFetchApp.fetch(endpoint_() + asset, {followRedirects: false, validateHttpsCertificates: true,
        muteHttpExceptions: true, headers: {Authorization: 'Bearer ' + get_('token', ''), 'X-QSan-Session': get_('session', '')}});
      const headers = response.getAllHeaders(), mimeKey = Object.keys(headers).find(k => k.toLowerCase() === 'content-type'), mime = String(headers[mimeKey] || '');
      if (response.getResponseCode() === 200 && /^image\/(png|jpeg|gif)$/.test(mime)) image = 'data:' + mime + ';base64,' + Utilities.base64Encode(response.getContent());
    }
    return outcome_('詳情已寫入 QSAN Details。', {detailImage: image});
  });
}
