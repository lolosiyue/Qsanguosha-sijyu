/** Pure worksheet -> typed draft conversion; native owns final validation. */
function integer_(value, minimum) {
  const text = String(value).trim();
  if (!/^-?[0-9]+$/.test(text)) throw new Error(qsanText_('integerRequired'));
  const n = Number(text);
  if (!Number.isSafeInteger(n) || n < minimum || n > 2147483647) throw new Error(qsanText_('integerRange'));
  return n;
}
function boolean_(value) {
  if (value === true || /^(true|是)$/i.test(String(value))) return true;
  if (value === false || /^(false|否)$/i.test(String(value))) return false;
  throw new Error(qsanText_('booleanInvalid'));
}
function chosen_(value) { return value === true || /^(true|是)$/i.test(String(value)); }
function shape_(request) {
  if (request.type === 'choose_general' && (request.payload || {}).general_candidates
      && request.payload.general_candidates.length) return 'general_pair';
  const groups = {
    option: ['choose_general', 'choose_direction', 'choice', 'choose_suit', 'choose_kingdom', 'skill_invoke', 'trigger_order', 'choose_order', 'choose_role_3v3', 'surrender', 'luck_card', 'ask_general'],
    cards: ['exchange_card', 'ask_peach', 'skill_gongxin', 'play_card', 'response_card', 'discard_card', 'nullification', 'show_card', 'amazing_grace', 'pindian', 'choose_card'],
    players: ['choose_player'], assignment: ['choose_role'], rearrangement: ['skill_guanxing'],
    distribution: ['skill_yiji'], general_arrangement: ['arrange_general']
  };
  for (const shape of Object.keys(groups)) if (groups[shape].indexOf(request.type) >= 0) return shape;
  return request.type && request.type !== 'none' ? 'unsupported' : 'none';
}
function ordered_(rows, bank, repeats) {
  const items = [];
  rows.forEach((r, index) => {
    if (r[0] !== bank || !chosen_(r[3])) return;
    if (!boolean_(r[6])) throw new Error(qsanText_('unavailablePicked'));
    const raw = String(r[4]).trim();
    const places = raw ? raw.split(',').map(x => integer_(x.trim(), 1)) : [1000000 + index];
    if (!repeats && places.length !== 1) throw new Error(qsanText_('multiOrderTargets'));
    places.forEach(position => items.push({row: r, position: position}));
  });
  items.sort((a, b) => a.position - b.position);
  for (let i = 1; i < items.length; ++i) if (items[i].position === items[i - 1].position) throw new Error(qsanText_('orderDuplicate'));
  return items.map(x => x.row);
}
function draftFromRows_(meta, rows) {
  const selected = bank => ordered_(rows, bank, bank === 'player');
  const only = bank => { const list = selected(bank); if (list.length > 1) throw new Error(qsanText_('singleChoice')); return list[0]; };
  const cardIds = () => selected('card').map(r => integer_(r[1], -1));
  const targets = () => selected('player').map(r => String(r[1]));
  switch (meta.shape) {
    case 'general_pair': {
      const generals = selected('general');
      // Seat order is explicit and remains editable until native preflight/submit.
      if (generals.length !== 2 || String(generals[0][4]) !== '1' || String(generals[1][4]) !== '2')
        throw new Error(qsanText_('generalPairOrder'));
      const pair = String(generals[0][1]) + '+' + String(generals[1][1]);
      if ((meta.general_pairs || []).indexOf(pair) < 0) throw new Error(qsanText_('generalPairInvalid'));
      return {option: pair};
    }
    case 'option': {
      const row = only('option'); if (!row) throw new Error(qsanText_('optionRequired'));
      // Option schemas explicitly reject unrelated cards/targets/skill fields.
      return {option: String(row[1])};
    }
    case 'players': return {targets: targets()};
    case 'cards': {
      const draft = {cards: cardIds(), targets: targets()}, skill = only('skill'), declaration = only('declaration');
      if (skill) { draft.skill_name = String(skill[8]); draft.skill_instance_id = integer_(skill[9] || 0, 0); }
      if (declaration) draft.declaration = String(declaration[1]);
      return draft;
    }
    case 'assignment': {
      const assignments = selected('assignment').map(r => {
        const role = String(r[5]); if ((meta.roles || []).indexOf(role) < 0) throw new Error(qsanText_('roleRequired'));
        return {name: String(r[1]), value: role};
      });
      return {assignments: assignments};
    }
    case 'rearrangement': {
      const top = [], bottom = [];
      selected('rearrange').forEach(r => {
        const side = String(r[5]).toLowerCase();
        if (side !== 'top' && side !== 'bottom') throw new Error(qsanText_('guanxingSide'));
        (side === 'top' ? top : bottom).push(integer_(r[1], 0));
      });
      return {top: top, bottom: bottom};
    }
    case 'distribution': {
      const names = targets(); if (names.length !== 1) throw new Error(qsanText_('distributionTarget'));
      return {cards: cardIds(), target: names[0]};
    }
    case 'general_arrangement': return {order: selected('general').map(r => String(r[1]))};
    default: throw new Error(qsanText_('unsupportedShape'));
  }
}
function readDraft_() {
  requirePaired_(); const meta = json_('meta', null);
  if (!meta || !decimal_(meta.request_id) || meta.request_id === '0' || meta.shape === 'none') throw new Error(qsanText_('noInteraction'));
  const count = Number(get_('action_rows', '0'));
  if (!count) throw new Error(qsanText_('rowsNotLoaded'));
  const rows = sheet_('QSAN Actions').getRange(actionFirst_(), 1, count, QSAN.COLS).getValues();
  // Candidate metadata is presentation, not authorization. Every draft is
  // revalidated by native against this exact request/generation/revision.
  return {meta: meta, draft: draftFromRows_(meta, rows)};
}
