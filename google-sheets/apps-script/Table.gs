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
  const suffix = qsanFormat_('marksMore', ordered.length);
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
    throw new Error(qsanText_('ownedSheetRequired'));
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
function setupWorkbook() { return locked_(function() { setup_(); return outcome_(qsanText_('setupDone')); }); }
function setup_() {
  const ss = SpreadsheetApp.getActive();
  QSAN.SHEETS.forEach(name => {
    let sheet = ss.getSheetByName(name);
    if (sheet) {
      sheet_(name); sheet.setColumnWidth(2, 180); sheet.setColumnWidth(3, 260); sheet.setColumnWidth(8, 330); sheet.setColumnWidth(11, 90);
      sheet.getRange(1, 1, sheet.getMaxRows(), 10).setWrap(true);
      sheet.getRange(1, 11, sheet.getMaxRows(), 1).setWrapStrategy(SpreadsheetApp.WrapStrategy.CLIP);
      if (name === 'QSAN Log') sheet.setColumnWidth(1, 900);
      if (name === 'QSAN Room') {
        sheet.getRange(11, 1, 1, 3).setFontWeight('bold').setBackground('#e8eaed');
        sheet.setColumnWidth(1, 140); sheet.setColumnWidth(2, 260); sheet.setColumnWidth(3, 360);
      }
      return;
    }
    sheet = ss.insertSheet(name); sheet.addDeveloperMetadata('QSAN_OWNER', QSAN.VERSION);
    sheet.setFrozenRows(name === 'QSAN Actions' ? 6 : 1);
    sheet.setColumnWidths(1, 11, 110); sheet.setColumnWidth(2, 180); sheet.setColumnWidth(3, 260); sheet.setColumnWidth(8, 330); sheet.setColumnWidth(11, 90);
    if (name === 'QSAN Log') sheet.setColumnWidth(1, 900);
    if (name === 'QSAN Room') {
      sheet.getRange(11, 1, 1, 3).setFontWeight('bold').setBackground('#e8eaed');
      sheet.setColumnWidth(1, 140); sheet.setColumnWidth(2, 260); sheet.setColumnWidth(3, 360);
    }
    sheet.getRange(1, 1, 1, 11).setBackground('#17364b').setFontColor('#ffffff').setFontWeight('bold');
    sheet.getRange(1, 1, sheet.getMaxRows(), 10).setVerticalAlignment('top').setWrap(true);
    sheet.getRange(1, 11, sheet.getMaxRows(), 1).setVerticalAlignment('top').setWrapStrategy(SpreadsheetApp.WrapStrategy.CLIP);
    // A recreated sheet needs repopulation even if its old signatures survived.
    const props = PropertiesService.getUserProperties(), p = prefix_();
    Object.keys(props.getProperties()).filter(k => k.startsWith(p + 'block_')).forEach(k => props.deleteProperty(k));
    drop_('actions_signature');
  });
  // Upgrade the owned Actions sheet in place; the draft remains the same table.
  drop_('room_layout');
  const room = sheet_('QSAN Room');
  if (!room.getRange(1, 1).getValue()) {
    writeBlock_('QSAN Room', 'room_initial', 1, 1, [
      [qsanText_('roomHdrConnect'), qsanText_('roomHdrValue'), qsanText_('roomHdrNote')],
      [qsanText_('roomName'), 'Sheets', ''], [qsanText_('roomAvatar'), 'caocao', qsanText_('roomAvatarNote')],
      [qsanText_('roomServer'), '127.0.0.1', qsanText_('roomServerNote')], [qsanText_('roomPort'), 9527, ''],
      [qsanText_('roomRobots'), 4, qsanText_('roomRobotsNote')],
      [qsanText_('roomGuide'), qsanText_('roomGuideValue'), qsanText_('roomGuideNote')],
      [qsanText_('roomChat'), '', qsanText_('roomChatNote')], [qsanText_('roomSettings'), '', qsanText_('roomSettingsNote')],
      [qsanText_('roomCatalog'), qsanText_('roomCatalogValue'), qsanText_('roomCatalogNote')],
      [qsanText_('roomColName'), qsanText_('roomColType'), qsanText_('roomColValue')],
      ['ServerName', qsanText_('settingText'), 'Sheets'], ['GameMode', qsanText_('settingText'), '05p'],
      ['OperationNoLimit', qsanText_('settingBool'), true], ['CountDownSeconds', qsanText_('settingInteger'), 0]
    ], 3);
  }
  writeBlock_('QSAN Details', 'detail_header', 1, 1, [[qsanText_('detailTitle'), qsanText_('detailHint')]], 2);
}
function description_(item) {
  // Accept both native description fields; never show an untranslated lookup key.
  return [item.description, item.detail].map(text_).find(value => value.trim() && !/^:[^\s]+$/.test(value.trim())) || '';
}
function actionFirst_() { return Number(get_('action_first', String(QSAN.FIRST))); }
function roomPlan_(snapshot) {
  const view = snapshot.view || {}, state = snapshot.state || {};
  const self = view.self_name || state.self_name || '';
  const players = (view.players || []).slice().sort((a, b) => Number(a.seat || 0) - Number(b.seat || 0));
  const index = players.findIndex(p => p.id === self);
  // Preserve seat order, including dead players, and rotate only around self.
  const ordered = index < 0 ? players : players.slice(index + 1).concat(players.slice(0, index));
  const topCount = Math.min(ordered.length, ordered.length % 2 ? 3 : 2);
  const sideCount = (ordered.length - topCount) / 2;
  const bands = Math.max(2, sideCount), bottom = 12 + bands * 7;
  const topColumns = topCount === 1 ? [5] : topCount === 2 ? [1, 9] : [1, 5, 9];
  const seats = [];
  ordered.forEach((player, i) => {
    if (i < sideCount) seats.push({player, row: 12 + (sideCount - 1 - i) * 7, col: 1});
    else if (i < sideCount + topCount) seats.push({player, row: 5, col: topColumns[i - sideCount]});
    else seats.push({player, row: 12 + (i - sideCount - topCount) * 7, col: 9});
  });
  if (index >= 0) seats.push({player: players[index], row: bottom, col: 5, self: true});
  const hand = view.hand || [], handHeight = Math.max(3, Math.ceil(hand.length / 5));
  return {seats, bottom, handHeight, first: bottom + handHeight + 15};
}
function roomCardText_(card) {
  if (!card || card.hidden) return '暗牌';
  const suit = {spade: '♠', club: '♣', heart: '♥', diamond: '♦'}[card.suit] || '';
  const number = {1: 'A', 11: 'J', 12: 'Q', 13: 'K'}[card.number] || card.number || '';
  return (card.label || card.name || '未知牌') + (suit || number ? '[' + suit + number + ']' : '');
}
function roomPhase_(player) {
  return player.phase_label || ({round_start: '回合开始', start: '准备阶段', judge: '判定阶段', draw: '摸牌阶段',
    play: '出牌阶段', discard: '弃牌阶段', finish: '结束阶段', not_active: '', none: ''}[player.phase] || player.phase || '');
}
function connectionText_(state) {
  const key = {connecting: 'connConnecting', reconnecting: 'connReconnecting', handshake: 'connHandshake',
    active: 'connActive', disconnected: 'connDisconnected', failed: 'connFailed'}[String(state || '')];
  return key ? qsanText_(key) : (state ? String(state) : qsanText_('connNone'));
}
function factionText_(value) {
  const roles = {lord: '主公', loyalist: '忠臣', rebel: '反贼', renegade: '内奸'};
  const kingdoms = {wei: '魏', shu: '蜀', wu: '吴', qun: '群', jin: '晋', god: '神', careerist: '野心家'};
  const text = String(value || '');
  return roles[text] || kingdoms[text] || text;
}
function roomSeatText_(seat, view) {
  const p = seat.player;
  const role = factionText_(p.role), kingdom = factionText_(p.kingdom);
  const hp = p.hp == null || p.max_hp == null ? qsanText_('hpPending') : qsanText_('hpLabel') + ' ' + p.hp + '/' + p.max_hp;
  const judgments = (view.cards || []).filter(c => c.owner === p.id && Number(c.place) === 2);
  return [
    [p.general_label, p.deputy_general_label].filter(Boolean).join('／') || qsanText_('waitGeneral'),
    hp + '　' + qsanText_('handLabel') + ' ' + (p.hand_count == null ? '?' : p.hand_count),
    [p.alive === false ? qsanText_('deadLabel') : '', role && role !== kingdom ? role : '', kingdom ? qsanText_('kingdomLabel') + ' ' + kingdom : '', roomPhase_(p), p.chained ? qsanText_('chainedLabel') : '', p.face_up === false ? qsanText_('faceDownLabel') : ''].filter(Boolean).join(' · '),
    qsanText_('equipLabel') + '：' + (equipLabels_(p.equip) || qsanText_('noneLabel')),
    qsanText_('judgeLabel') + '：' + (judgments.map(roomCardText_).join('、') || qsanText_('noneLabel')),
    marksSummary_(p.marks)
  ].filter(Boolean).join('\n');
}
function roomZones_(plan) {
  return [{key: 'title', row: 1, col: 1, height: 1, width: 11},
    {key: 'status', row: 2, col: 1, height: 2, width: 11},
    {key: 'pile_title', row: 12, col: 5, height: 1, width: 3},
    {key: 'pile', row: 13, col: 5, height: 3, width: 3},
    {key: 'prompt_title', row: 17, col: 5, height: 1, width: 3},
    {key: 'prompt', row: 18, col: 5, height: plan.bottom - 21, width: 3},
    {key: 'preflight', row: plan.bottom - 3, col: 5, height: 2, width: 3},
    {key: 'hand_title', row: plan.bottom + 7, col: 1, height: 1, width: 11},
    {key: 'hand', row: plan.bottom + 8, col: 1, height: plan.handHeight, width: 11},
    {key: 'skills', row: plan.bottom + 8 + plan.handHeight, col: 1, height: 2, width: 11},
    {key: 'help', row: plan.first - 4, col: 1, height: 3, width: 11},
    {key: 'log_title', row: 4, col: 13, height: 1, width: 4}]
    .concat(plan.seats.flatMap((seat, i) => [
      {key: 'seat_title_' + i, row: seat.row, col: seat.col, height: 1, width: 3},
      {key: 'seat_' + i, row: seat.row + 1, col: seat.col, height: 5, width: 3}]))
    .concat(Array.from({length: Math.floor((plan.first - 6) / 2)}, (_, i) => ({key: 'log_' + i, row: i * 2 + 5, col: 13, height: 2, width: 4})));
}
function prepareRoom_(plan) {
  const signature = digest_(JSON.stringify({version: 2, first: plan.first, seats: plan.seats.map(s => [s.row, s.col]), handHeight: plan.handHeight}));
  if (get_('room_layout', '') === signature) return;
  const sheet = sheet_('QSAN Actions'), first = actionFirst_(), count = Number(get_('action_rows', '0'));
  drop_('room_seats'); drop_('room_seat_styles');
  // Capture the draft before changing merged ranges or moving its candidate table.
  const prior = count ? sheet.getRange(first, 1, count, QSAN.COLS).getValues() : [];
  const height = Math.max(first + count, plan.first + count, sheet.getLastRow());
  ensureRange_(sheet, 1, 1, height, 16).breakApart().clearContent().clearDataValidations().setBackground('#eef3f0').setFontColor('#20382e').setFontWeight('normal').setVerticalAlignment('top').setWrap(true);
  sheet.setFrozenRows(3); sheet.setHiddenGridlines(true);
  [85, 90, 155, 55, 95, 100, 70, 200, 90, 70, 65, 18, 95, 95, 95, 95].forEach((width, i) => sheet.setColumnWidth(i + 1, width));
  sheet.setRowHeights(1, height, 24);
  roomZones_(plan).forEach(zone => {
    const range = sheet.getRange(zone.row, zone.col, zone.height, zone.width).merge();
    const heading = /title/.test(zone.key);
    range.setBackground(heading ? '#254d3d' : '#ffffff').setFontColor(heading ? '#ffffff' : '#20382e').setFontWeight(heading ? 'bold' : 'normal');
  });
  sheet.getRange(18, 5, plan.bottom - 21, 3).setBackground('#fff0c7');
  sheet.getRange(plan.first - 1, 1, 1, 11).setBackground('#254d3d').setFontColor('#ffffff').setFontWeight('bold');
  const candidates = ensureRange_(sheet, plan.first, 1, 500, 11);
  sheet.clearConditionalFormatRules();
  sheet.setConditionalFormatRules([
    SpreadsheetApp.newConditionalFormatRule().whenFormulaSatisfied('=$D' + plan.first + '=TRUE')
      .setBackground('#d2e3fc').setRanges([candidates]).build(),
    SpreadsheetApp.newConditionalFormatRule().whenFormulaSatisfied('=$G' + plan.first + '=FALSE')
      .setFontColor('#9aa0a6').setRanges([candidates]).build()]);
  // Clear only rendering caches: request identity, credentials and pending commands survive.
  const props = PropertiesService.getUserProperties(), prefix = prefix_() + 'block_';
  Object.keys(props.getProperties()).filter(k => k.startsWith(prefix + 'room_') || k.startsWith(prefix + 'action_header')).forEach(k => props.deleteProperty(k));
  put_('action_first', plan.first); drop_('actions_signature');
  put_('room_preflight_row', plan.bottom - 3);
  if (prior.length) sheet.getRange(plan.first, 1, prior.length, QSAN.COLS).setNumberFormat('@').setValues(prior.map(r => r.map((v, i) => i === 10 ? '' : safe_(v))));
  put_('room_layout', signature);
}
function renderRoom_(snapshot, meta) {
  const plan = roomPlan_(snapshot), view = snapshot.view || {}, game = (snapshot.state || {}).game || {};
  prepareRoom_(plan);
  const zones = roomZones_(plan), put = (key, value) => {
    const zone = zones.find(z => z.key === key);
    writeBlock_('QSAN Actions', 'room_' + key, zone.row, zone.col, [[value]], 1);
  };
  put('title', qsanText_('roomTitle'));
  put('status', [game.game_over ? qsanText_('gameOverPrefix') + text_(game.result || '') : qsanFormat_('roundNumber', game.round || 0),
    game.draw_pile_count == null ? '' : qsanFormat_('pileRemain', game.draw_pile_count), connectionText_(snapshot.connection)].filter(Boolean).join('　｜　'));
  plan.seats.forEach((seat, i) => {
    const p = seat.player;
    put('seat_title_' + i, (roomPhase_(p) ? '▶ ' : '') + (seat.self ? qsanText_('selfPrefix') : '') + (p.seat ? qsanFormat_('seatPos', p.seat) : '') + (p.label || p.id));
    put('seat_' + i, roomSeatText_(seat, view));
  });
  // Seat title colors: gray = dead, orange = acting, blue = self, green = others.
  const seatStyles = plan.seats.map(seat => {
    const p = seat.player;
    if (p.alive === false) return ['#9e9e9e', '#f5f5f5'];
    if (roomPhase_(p)) return ['#c77800', '#ffffff'];
    if (seat.self) return ['#1b5e8a', '#ffffff'];
    return ['#254d3d', '#ffffff'];
  });
  const seatStyleSignature = digest_(JSON.stringify(seatStyles));
  if (get_('room_seat_styles', '') !== seatStyleSignature) {
    const roomSheet = sheet_('QSAN Actions');
    plan.seats.forEach((seat, i) => roomSheet.getRange(seat.row, seat.col, 1, 3)
      .setBackground(seatStyles[i][0]).setFontColor(seatStyles[i][1]));
    put_('room_seat_styles', seatStyleSignature);
  }
  // Map the displayed rectangles to player IDs, never to mutable seat numbers.
  saveJson_('room_seats', plan.seats.map(s => ({row: s.row, col: s.col, id: s.player.id})));
  // PlaceTable is 7; equipment, delayed tricks and discard pile are separate zones.
  put('pile_title', qsanText_('pileTitle'));
  put('pile', (view.cards || []).filter(c => Number(c.place) === 7).map(roomCardText_).join('、') || qsanText_('pileEmpty'));
  put('prompt_title', qsanText_('promptPrefix') + (view.interaction_label || meta.type));
  put('prompt', game.game_over ? qsanText_('gameOver') + text_(game.result || '') : interactionPrompt_(snapshot));
  put('preflight', get_('preflight_message', qsanText_('preflightIdle')));
  put('hand_title', qsanFormat_('handTitle', (view.hand || []).length));
  put('hand', (view.hand || []).map(roomCardText_).join('　｜　') || qsanText_('handEmpty'));
  put('skills', qsanText_('skillsLabel') + ((view.skills || []).map(s => s.label || s.name || s.id).join('、') || qsanText_('noneLabel')));
  put('help', qsanText_('roomHelp') + '\n' +
    (meta.shape === 'unsupported' ? qsanText_('unsupportedNote') : qsanText_('selectRange') + (meta.min == null ? '' : meta.min) + ' ～ ' + (meta.max == null ? '' : meta.max)) +
    (meta.cancelable ? qsanText_('cancelHint') : '') + '\n' + qsanText_('seatLegend'));
  put('log_title', qsanText_('logTitle'));
  const logCount = Math.floor((plan.first - 6) / 2), logs = (view.logs || []).slice(-logCount).reverse();
  for (let i = 0; i < logCount; ++i) put('log_' + i, logs[i] || '');
  writeBlock_('QSAN Actions', 'action_header', plan.first - 1, 1,
    [[qsanText_('hdrKind'), qsanText_('hdrId'), qsanText_('hdrName'), qsanText_('hdrCheck'), qsanText_('hdrOrder'),
      qsanText_('hdrSide'), qsanText_('hdrEnabled'), qsanText_('hdrDesc'), qsanText_('hdrSkill'), qsanText_('hdrInstance'), '']], 11);
}
function renderPreflight_(message) {
  // The central preflight panel has a fixed relation to the seat ring, independent of hand size.
  const row = Number(get_('room_preflight_row', '3'));
  put_('preflight_message', message);
  writeBlock_('QSAN Actions', 'room_preflight', row, row === 3 ? 1 : 5, [[message]], 1);
}
function meta_(snapshot) {
  const req = snapshot.interaction || {}, payload = req.payload || {};
  ['generation', 'revision', 'request_id'].forEach(k => { if (!decimal_(snapshot[k])) throw new Error(qsanText_('nativeSeqError')); });
  return {generation: snapshot.generation, revision: snapshot.revision, request_id: snapshot.request_id,
    type: req.type || 'none', shape: shape_(req), cancelable: req.cancelable === true,
    roles: payload.roles || [], generals: payload.generals || [], enumerated: payload.enumerated !== false,
    general_pairs: shape_(req) === 'general_pair'
      ? (payload.options || []).filter(x => x.enabled !== false).map(x => x.value) : [],
    min: req.min, max: req.max};
}
function interactionPrompt_(snapshot) {
  // Keep native prompt text; skill identity fills only an otherwise empty prompt.
  const req = snapshot.interaction || {}, view = snapshot.view || {};
  if (shape_(req) === 'general_pair') return qsanText_('generalPairHelp');
  if ((req.payload || {}).scheme === 'hegemony_seats') return qsanText_('assignSeatsHelp');
  const prompt = view.prompt_text || view.prompt || req.prompt || (req.ui && req.ui.prompt) || (req.payload && req.payload.prompt) || '';
  if (prompt) return prompt;
  const skill = String(req.skill || '').trim();
  if (!skill) return req.type === 'none' || !req.type ? qsanText_('waitInteraction') : qsanText_('yourTurn');
  const match = (view.skills || []).find(s => s && (s.name === skill || s.id === skill));
  const label = match && (match.label || match.name || match.id) || skill;
  return req.type === 'skill_invoke' ? qsanFormat_('skillInvoke', label) : qsanFormat_('skillPrompt', label);
}
function render_(snapshot) {
  const meta = meta_(snapshot), previous = json_('meta', null), req = snapshot.interaction || {}, view = snapshot.view || {};
  const game = (snapshot.state || {}).game || {};
  const sameRequest = previous && previous.generation === meta.generation && previous.request_id === meta.request_id;
  renderRoom_(snapshot, meta);
  if (!previous || previous.revision !== meta.revision || !sameRequest) {
    drop_('preflight'); renderPreflight_(qsanText_('preflightIdle'));
  }
  const ui = Object.assign({}, req.ui || {});
  if ((!ui.skills || !ui.skills.length) && meta.shape === 'cards') ui.skills = view.skills || [];
  renderActions_(meta, ui, !!sameRequest);
  const board = [[qsanText_('boardTitle'), qsanText_('hdrId'), qsanText_('hdrName'), qsanText_('hdrHp'), qsanText_('hdrHandCount'),
      qsanText_('hdrEquip'), qsanText_('hdrStatus'), qsanText_('hdrDesc'), qsanText_('hdrSkill'), qsanText_('hdrInstanceShort'), ''],
    [qsanText_('hdrStatus'), '', connectionText_(snapshot.connection), '', '', '', game.game_over ? 'GAME_OVER' : (view.status || game.status || ''), '', '', '', ''],
    [qsanText_('rowRoundPile'), game.round === undefined ? '' : game.round,
      game.draw_pile_count === undefined ? '' : qsanFormat_('pileRemainBoard', game.draw_pile_count), '', '', '', '', '', '', '', ''],
    [qsanText_('rowWinner'), '', text_(game.result || ''), '', '', '', '', '', '', '', '']];
  (view.players || []).forEach(p => {
    const hp = p.hp === undefined || p.hp === null ? '' : p.hp;
    const maxHp = p.max_hp === undefined || p.max_hp === null ? '' : p.max_hp;
    const handCount = p.hand_count === undefined || p.hand_count === null ? '' : p.hand_count;
    const label = p.label || p.id;
    const role = factionText_(p.role), kingdom = factionText_(p.kingdom), phase = roomPhase_(p);
    board.push(['player', p.id, label, (hp === '' && maxHp === '') ? '' : String(hp) + '/' + String(maxHp), handCount,
      [p.general_label, p.deputy_general_label, equipLabels_(p.equip), marksSummary_(p.marks)].filter(Boolean).join('；'),
      (p.alive === false ? qsanText_('deadLabel') : qsanText_('aliveLabel')) + (role && role !== kingdom ? qsanText_('roleSep') + role : '') + (kingdom ? qsanText_('kingdomSep') + kingdom : '') + (phase ? qsanText_('phaseSep') + phase : ''), description_(p), '', '', '']);
  });
  (view.hand || []).forEach(c => board.push(['card', c.id, c.label || c.name, '', '', qsanText_('selfHand'), '', description_(c), '', '', '']));
  (view.cards || []).forEach(c => board.push(['card', c.id, c.label || c.name, '', '', qsanText_('publicCards'), '', description_(c), '', '', '']));
  (view.skills || []).forEach(s => board.push(['skill', s.name || s.id, s.label, '', '', qsanText_('selfSkills'), '', description_(s), s.name || s.id, s.instance_id || s.skill_instance_id || 0, '']));
  writeBlock_('QSAN Board', 'board', 1, 1, board, 11);
  writeBlock_('QSAN Log', 'log', 1, 1, [[qsanText_('logBoardTitle')]].concat((view.logs || []).map(x => [x])), 1);
  // Commit the new identity only once its candidate rows were written.
  saveJson_('meta', meta);
}
function renderActions_(meta, ui, preserve) {
  const first = actionFirst_();
  const rows = [], add = (bank, items) => (items || []).forEach((x, index) => {
    if (typeof x === 'string' || typeof x === 'number') x = {id: String(x), label: String(x)};
    const instance = x.instance_id || x.skill_instance_id || 0;
    const skillName = x.name || x.skill_name || (bank === 'skill' ? String(x.id || '').replace(/:[0-9]+$/, '') : '');
    // view.skills uses name while preflight uses name:instance. Normalize both
    // so a legality query never clears the player's selected skill.
    const id = bank === 'skill' ? skillName + ':' + instance : (x.id === undefined ? String(x.name || '') : String(x.id));
    rows.push([bank, id, x.label || x.name || x.id || '',
      bank === 'rearrange' || bank === 'assignment', bank === 'rearrange' ? String(index + 1) : '', bank === 'rearrange' ? 'top' : '',
      x.enabled !== false, description_(x), skillName, instance, '']);
  });
  if (meta.shape === 'option') { add('option', ui.options); if (!meta.enumerated) add('option', [{id: '', label: qsanText_('freeOption')}]); }
  if (['cards', 'distribution'].indexOf(meta.shape) >= 0) add('card', ui.cards);
  if (['cards', 'distribution', 'players'].indexOf(meta.shape) >= 0) add('player', ui.players);
  if (meta.shape === 'cards') { add('skill', ui.skills); add('declaration', ui.declarations); }
  if (meta.shape === 'assignment') add('assignment', ui.players);
  if (meta.shape === 'rearrangement') add('rearrange', ui.cards);
  if (meta.shape === 'general_arrangement') add('general', ui.generals && ui.generals.length ? ui.generals : meta.generals);
  if (meta.shape === 'general_pair') add('general', ui.generals);
  const signature = digest_(JSON.stringify({schema: 'actions-checkbox-v2', generation: meta.generation, request: meta.request_id, rows: rows}));
  if (preserve && get_('actions_signature', '') === signature) return;
  const sheet = sheet_('QSAN Actions'), count = Number(get_('action_rows', '0')), old = {};
  if (preserve && count) sheet.getRange(first, 1, count, QSAN.COLS).getValues().forEach(r => { old[r[0] + '\n' + r[1] + '\n' + r[9]] = r; });
  rows.forEach(r => { const prior = old[r[0] + '\n' + r[1] + '\n' + r[9]]; if (prior) {
    r[3] = prior[3] === true || String(prior[3]).toLowerCase() === 'true' || String(prior[3]) === '是';
    r[4] = prior[4]; r[5] = prior[5];
  } else r[3] = Boolean(r[3]); });
  if (count) sheet.getRange(first, 1, count, QSAN.COLS).clearContent().clearDataValidations();
  if (rows.length) {
    ensureRange_(sheet, first, 1, rows.length, QSAN.COLS).setNumberFormat('@').setValues(rows.map(r => r.map(safe_)));
    // Column D is a real boolean checkbox column; text format turns false into a literal string.
    sheet.getRange(first, 4, rows.length, 1).setNumberFormat('General').setValues(rows.map(r => [Boolean(r[3])]));
    // Validation creates checkboxes without resetting already selected values.
    sheet.getRange(first, 4, rows.length, 1).setDataValidation(SpreadsheetApp.newDataValidation().requireCheckbox().build());
    sheet.getRange(first, 4, rows.length, 3).setBackground('#e9f3ff');
    sheet.getRange(first, 11, rows.length, 1).setWrapStrategy(SpreadsheetApp.WrapStrategy.CLIP);
    if (meta.shape === 'rearrangement') sheet.getRange(first, 6, rows.length, 1).setDataValidation(SpreadsheetApp.newDataValidation().requireValueInList(['top', 'bottom'], true).setAllowInvalid(false).build());
    if (meta.shape === 'assignment' && meta.roles.length) sheet.getRange(first, 6, rows.length, 1).setDataValidation(SpreadsheetApp.newDataValidation().requireValueInList(meta.roles, true).setAllowInvalid(false).build());
  }
  put_('action_rows', rows.length); put_('actions_signature', signature);
}
function catalogFromSheet() {
  return locked_(function() {
    const catalog = command_('catalog', {}), rows = [[qsanText_('catHdrType'), qsanText_('hdrId'), qsanText_('hdrName'), qsanText_('catHdrInfo')]];
    [['mode', catalog.modes], ['general', catalog.generals], ['package', catalog.packages]].forEach(bank => {
      (bank[1] || []).forEach(item => rows.push([bank[0], item.id, item.label, text_(item.metadata || {player_count: item.player_count || ''})]));
    });
    writeBlock_('QSAN Catalog', 'catalog', 1, 1, rows, 4);
    const room = sheet_('QSAN Room'), last = room.getLastRow(), existing = new Set(room.getRange(12, 1, Math.max(1, last - 11), 1).getValues().map(r => String(r[0])));
    const more = [];
    Object.keys(catalog.settings || {}).forEach(key => {
      if (existing.has(key)) return;
      const v = catalog.settings[key]; let type = qsanText_('settingText'), value = v;
      if (typeof v === 'boolean') type = qsanText_('settingBool');
      else if (typeof v === 'number' && Number.isInteger(v)) type = qsanText_('settingInteger');
      else if (Array.isArray(v) && v.every(x => typeof x === 'string')) { type = qsanText_('settingList'); value = v.join('\n'); }
      else if (typeof v === 'object' || typeof v === 'number') { type = qsanText_('settingKeep'); value = qsanText_('settingKeepNote'); }
      more.push([key, type, value]);
    });
    if (more.length) ensureRange_(room, last + 1, 1, more.length, 3).setNumberFormat('@').setValues(more.map(r => r.map(safe_)));
    return outcome_(qsanText_('catalogDone'));
  });
}
function roomSeatAt_(range) {
  const r = range.getRow(), col = range.getColumn();
  const endRow = r + range.getNumRows() - 1, endCol = col + range.getNumColumns() - 1;
  return json_('room_seats', []).find(s => r >= s.row && endRow < s.row + 6 && col >= s.col && endCol < s.col + 3);
}
function detailValues_(value) {
  // Asset references stay in the transport and image loader, including nested equipment.
  if (Array.isArray(value)) return value.map(detailValues_);
  if (!value || typeof value !== 'object') return value;
  const result = {};
  Object.keys(value).filter(k => k !== 'image' && !k.endsWith('_image')).forEach(k => { result[k] = detailValues_(value[k]); });
  return result;
}
function detailsFromSheet() {
  return locked_(function() {
    const range = SpreadsheetApp.getActiveRange(); if (!range) throw new Error(qsanText_('pickItemRow'));
    const sheet = range.getSheet(), name = sheet.getName();
    if (['QSAN Board', 'QSAN Actions', 'QSAN Catalog'].indexOf(name) < 0) throw new Error(qsanText_('pickSheet'));
    sheet_(name);
    const inRoom = name === 'QSAN Actions' && range.getRow() < actionFirst_();
    const seat = inRoom ? roomSeatAt_(range) : null;
    if (inRoom && !seat) throw new Error(qsanText_('pickSeat'));
    const row = seat ? ['player', seat.id, '', '', '', '', '', '', '', '', ''] : sheet.getRange(range.getRow(), 1, 1, 11).getValues()[0];
    let kind = String(row[0]), key = String(row[1]);
    if (kind === 'rearrange') kind = 'card';
    if (kind === 'assignment') kind = 'player';
    if (kind === 'skill') key = String(row[8] || key);
    // Ordinary yes/no or skill options are not general IDs.
    if (kind === 'option' && json_('meta', {}).type === 'choose_general') kind = 'general';
    const supported = ['card', 'player', 'skill', 'general', 'pile'];
    let result;
    if (supported.indexOf(kind) >= 0) result = command_('details', {kind: kind, key: key});
    else result = {label: row[2], description: row[7] || ''};
    const rows = [[qsanText_('labelItem'), result.label || row[2]], [qsanText_('hdrId'), key]];
    const visible = detailValues_(result), labels = {description: qsanText_('hdrDesc'), detail: qsanText_('hdrDesc'),
      marks: qsanText_('labelMarks'), equip: qsanText_('equipLabel'), hp: qsanText_('hpLabel'),
      max_hp: qsanText_('labelMaxHp'), hand_count: qsanText_('hdrHandCount')};
    Object.keys(visible).filter(k => k !== 'id' && k !== 'label').forEach(k => rows.push([labels[k] || k, text_(visible[k])]));
    writeBlock_('QSAN Details', 'details', 2, 1, rows, 2);
    const asset = result.image || result.general_image; let image = '';
    if (typeof asset === 'string' && /^\/v1\/assets\/[0-9a-f]{64}$/.test(asset)) {
      const response = UrlFetchApp.fetch(endpoint_() + asset, {followRedirects: false, validateHttpsCertificates: true,
        muteHttpExceptions: true, headers: {Authorization: 'Bearer ' + get_('token', ''), 'X-QSan-Session': get_('session', '')}});
      const headers = response.getAllHeaders(), mimeKey = Object.keys(headers).find(k => k.toLowerCase() === 'content-type'), mime = String(headers[mimeKey] || '');
      if (response.getResponseCode() === 200 && /^image\/(png|jpeg|gif)$/.test(mime)) image = 'data:' + mime + ';base64,' + Utilities.base64Encode(response.getContent());
    }
    return outcome_(qsanText_('detailsDone'), {detailImage: image,
      detailText: rows.map(r => r[0] + '：' + r[1]).join('\n')});
  });
}
