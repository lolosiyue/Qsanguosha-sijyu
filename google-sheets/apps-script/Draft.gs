/** Pure worksheet -> typed draft conversion; native owns final validation. */
function integer_(value, minimum) {
  const text = String(value).trim();
  if (!/^-?[0-9]+$/.test(text)) throw new Error('請輸入整數。');
  const n = Number(text);
  if (!Number.isSafeInteger(n) || n < minimum || n > 2147483647) throw new Error('整數超出範圍。');
  return n;
}
function boolean_(value) {
  if (value === true || /^(true|是)$/i.test(String(value))) return true;
  if (value === false || /^(false|否)$/i.test(String(value))) return false;
  throw new Error('布林欄位請填 TRUE 或 FALSE。');
}
function chosen_(value) { return value === true || /^(true|是)$/i.test(String(value)); }
function shape_(request) {
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
    if (!boolean_(r[6])) throw new Error('選到了不可用項目，請重新預檢。');
    const raw = String(r[4]).trim();
    const places = raw ? raw.split(',').map(x => integer_(x.trim(), 1)) : [1000000 + index];
    if (!repeats && places.length !== 1) throw new Error('只有目標可以填入多個順序，例如 1,3。');
    places.forEach(position => items.push({row: r, position: position}));
  });
  items.sort((a, b) => a.position - b.position);
  for (let i = 1; i < items.length; ++i) if (items[i].position === items[i - 1].position) throw new Error('順序不可重複。');
  return items.map(x => x.row);
}
function draftFromRows_(meta, rows) {
  const selected = bank => ordered_(rows, bank, bank === 'player');
  const only = bank => { const list = selected(bank); if (list.length > 1) throw new Error('此項只能選一個。'); return list[0]; };
  const cardIds = () => selected('card').map(r => integer_(r[1], -1));
  const targets = () => selected('player').map(r => String(r[1]));
  switch (meta.shape) {
    case 'option': {
      const row = only('option'); if (!row) throw new Error('請選擇一個選項。');
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
        const role = String(r[5]); if ((meta.roles || []).indexOf(role) < 0) throw new Error('請替玩家選擇原生提供的角色。');
        return {name: String(r[1]), value: role};
      });
      return {assignments: assignments};
    }
    case 'rearrangement': {
      const top = [], bottom = [];
      selected('rearrange').forEach(r => {
        const side = String(r[5]).toLowerCase();
        if (side !== 'top' && side !== 'bottom') throw new Error('觀星側別請填 top 或 bottom。');
        (side === 'top' ? top : bottom).push(integer_(r[1], 0));
      });
      return {top: top, bottom: bottom};
    }
    case 'distribution': {
      const names = targets(); if (names.length !== 1) throw new Error('每次分牌請選一名接收者。');
      return {cards: cardIds(), target: names[0]};
    }
    case 'general_arrangement': return {order: selected('general').map(r => String(r[1]))};
    default: throw new Error('此互動尚未支援，沒有送出任何回覆。');
  }
}
function readDraft_() {
  requirePaired_(); const meta = json_('meta', null);
  if (!meta || !decimal_(meta.request_id) || meta.request_id === '0' || meta.shape === 'none') throw new Error('目前沒有待回覆互動。');
  const count = Number(get_('action_rows', '0'));
  if (!count) throw new Error('互動選項尚未載入，請重新整理。');
  const rows = sheet_('QSAN Actions').getRange(QSAN.FIRST, 1, count, QSAN.COLS).getValues();
  // Candidate metadata is presentation, not authorization. Every draft is
  // revalidated by native against this exact request/generation/revision.
  return {meta: meta, draft: draftFromRows_(meta, rows)};
}
