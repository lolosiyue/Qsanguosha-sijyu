#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""external_agent_driver.py — hybrid external-agent driver over the v1 local transport.

Five consumption optimizations implemented on top of docs/external-agents.md:

1. Mechanical adjudicator — conservative local answering: only single-action
   prompts are answered locally (exact discard, single choice/card/target,
   opaque random-card domains at index 1, zero-action passes). Anything
   ambiguous escalates; a signature that is silently re-asked after an
   accepted submission is poisoned and never answered locally again
   (audited as ``auto_rejected``).
2. worldView incremental summary — per-seat cursors (``last_revision``,
   ``last_event_sequence``); every escalated decision writes
   ``pending-<n>.json`` (full DTO, authority for legality) plus
   ``decision-<n>.md`` (the brief the LLM reads).
3. Stable-prefix rendering — brief section order is fixed
   (契約備忘 → 座位背景 → 筆記 → 本題 → 局面增量 → 尾註), template text is
   byte-identical across briefs, JSON key order is preserved as parsed, and
   volatile content (decision serial, timestamps) is confined to the tail.
4. Battle-notes protocol — ``game/notes.md`` is maintained by the LLM session
   between decisions; the bridge only cites the path and quotes an excerpt.
5. Escalation hooks / hybrid routing — ``--escalate-kinds`` and ``--fallback
   mock|file`` decide where escalated questions go; the escalation reason is
   in the brief title line. The driver never calls any LLM API.

Every decision (local or escalated) is appended to ``auto-answers.jsonl``.
Escalated questions always get briefs, even under ``--fallback mock``, so the
full-mechanical run doubles as a token-visibility simulation.
"""
import argparse
import json
import os
import re
import socket
import glob as globmod
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone

KIND_NAMES = {0: 'Activate', 1: 'UseCard', 2: 'SkillInvoke', 3: 'Choice', 4: 'Suit',
              5: 'Kingdom', 6: 'General', 7: 'Discard', 8: 'AmazingGrace',
              9: 'CardChosen', 10: 'Yiji', 11: 'PlayerChosen', 12: 'PlayersChosen',
              13: 'RespondCard', 14: 'Guanxing', 15: 'TriggerOrder'}

CONTRACT_MEMO = """（本節為位元組級固定模板：kind 答法表與硬規則，逐份簡報完全一致）

result 答法表（提交 {"kind": ..., "action": {...}}；decisionId/stateRevision 由橋接回填）：
- kind:"pass"    action:{}                                                             放棄／不發動／不響應
- kind:"answer"  action:{"userString":"<choices 內的原始字串>"}                          kind 2/3/15 單選
- kind:"answer"  action:{"selectedCardIds":[<cardId>,...]}                              kind 7/8/9/13 選牌（按序）
- kind:"answer"  action:{"selectedTargetNames":["sgsN",...]}                            kind 11/12 選玩家（按序）
- kind:"answer"  action:{"randomCardChoice":{"token":"...","index":N}}                  kind 9 暗牌域（token 取 choiceOptions.context.random_card_choices）
- kind:"useCard" action:{"candidateId":<id>,"useCardId":<cardId>,"selectedTargetNames":[...]}   kind 0/1 出牌
- kind:"useCard" action:{"useCardId":<cardId>}                                          targetFixed 卡：不得帶 selectedTargetNames

硬規則：
- limited:true 的 cardCandidates 一律不可用（不看 available）。
- 牌的來源僅限 choiceOptions.cardIds、可用 cardCandidates.cardId、cardConversions.eligibleSubcardIds。
- selectedTargetNames 必須逐字取自該候選的 targetCombinations / legalTargets（或 playerNames），順序不可改。
- targetFixed:true 且 targetCombinations [[]] → 提交 kind:"useCard" 只帶 useCardId（可另帶 candidateId）；帶 selectedTargetNames 必非法。
- 響應題（kind 13）只收 kind:"pass"/"answer"；包成 useCard 一律 invalid。
- kind 0 發動技能須依本題 skillActions 的宣告結構（activationRef/sourceRef）；無 skillActions 時只能 pass。

triggerEvent 對照（選錄，數值→語義）：21 HpChanged、40 ConfirmDamage、41 Predamage、42 DamageForseen、43 DamageCaused、44 DamageInflicted、45 PreDamageDone、46 DamageDone、47 Damage、48 Damaged、49 DamageComplete、50 EnterDying、51 Dying、55 Death、81 BeforeCardsMove、82 CardsMoveOneTime、83 PreCardUsed、84 CardUsed、85 TargetSpecifying、86 TargetConfirming、87 TargetSpecified、88 TargetConfirmed、89 CardEffect、90 CardEffected、95 CardOnEffect、97 ChoiceMade、98 MarkChange、99 MarkChanged、113 InvokeSkill、131 EventAskForChoice、132 EventSkillWillInvoke、133 EventSkillPay、134 EventSkillTargetConfirming、135 EventSkillInvoking、136 EventSkillEffect、153 BeforeCardsMoveBatch、154 PreCardsMoveBatch、155 CardsMoveBatch。

答案檔協議：把上述單一 JSON 物件寫進尾註指定的 answer 檔；橋接讀到後即刪除並提交。非法答案會被引擎以 lastError 重開同一題，橋接會重寫本簡報並標注重試次數。
"""

# Subset of event fields rendered in the brief increment. Fields keep the
# dict's own parse order (never reordered), satisfying the diff/cache rule.
EVENT_FIELDS = {'sequence', 'kind', 'triggerEvent', 'cardName', 'cardClass', 'from',
                'to', 'targets', 'amount', 'good', 'reason', 'details', 'nature',
                'place', 'chain', 'transfer', 'byUser', 'privateCardIds'}
MAX_EVENTS_PER_BRIEF = 80


def now_iso():
    return datetime.now(timezone.utc).astimezone().isoformat(timespec='seconds')


_LANG_KEY_RE = re.compile(r'^\s*\["(.+?)"\]\s*=\s*(.*)$')
_LANG_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def _lua_string_value(rhs):
    """Value of a Lua string assignment: concatenate string literals (handles
    "a" .. "b" and trailing -- comments), strip HTML markup and whitespace."""
    literals = _LANG_LITERAL_RE.findall(rhs)
    if not literals:
        return ''
    value = ''.join(literals)
    value = re.sub(r'<[^>]+>', '', value)
    return ' '.join(value.split())


def load_lang_table(asset_dir):
    """Parse the runtime's zh_CN Lua language tables into {key: text}.

    Covers prompt translations ("@shensu2"), skill/card names ("shensu",
    "savage_assault") and skill/card descriptions (":shensu"). Comment lines
    are skipped; single-line concatenations and trailing Lua comments are
    handled. HTML markup in descriptions is stripped and whitespace
    collapsed; first file seen wins. The values are verbatim zh_CN text
    (simplified, as shipped).
    """
    table = {}
    root = os.path.join(asset_dir, 'lang', 'zh_CN')
    if not os.path.isdir(root):
        return table
    for path in sorted(globmod.glob(os.path.join(root, '**', '*.lua'), recursive=True)):
        try:
            with open(path, 'r', encoding='utf-8', errors='replace') as fh:
                for line in fh:
                    stripped = line.lstrip()
                    if stripped.startswith('--'):
                        continue
                    m = _LANG_KEY_RE.match(line)
                    if not m:
                        continue
                    key = m.group(1)
                    value = _lua_string_value(m.group(2))
                    if value and key not in table:
                        table[key] = value
        except OSError:
            continue
    return table


def translate_candidates(key):
    """Lookup keys for composite prompts: exact, then before ':', then digit-stripped."""
    out = [key]
    head = key.split(':', 1)[0]
    if head != key:
        out.append(head)
    stripped = re.sub(r'\d+$', '', head)
    if stripped and stripped not in out:
        out.append(stripped)
    return out


def lookup_lang(lang, key, cap=220):
    """Return 'name｜description' for a key, or None. Multi-stage key fallback."""
    if not lang or not key:
        return None
    for candidate in translate_candidates(key):
        name = lang.get(candidate)
        desc = lang.get(':' + candidate)
        if name or desc:
            text = name or ''
            if desc:
                text = (text + '｜' if text else '') + desc[:cap]
            return text
    return None


def lookup_lang_pair(lang, key):
    """Return (matched_key, text) honouring the composite-key fallback, so the
    caller knows which part of a 'key:arg' prompt is the translation key."""
    if not lang or not key:
        return None, None
    for candidate in translate_candidates(key):
        name = lang.get(candidate)
        desc = lang.get(':' + candidate)
        if name or desc:
            text = name or ''
            if desc:
                text = (text + '｜' if text else '') + desc[:220]
            return candidate, text
    return None, None


def substitute_prompt(text, args, names):
    """Engine-style prompt rendering: %src/%dest/%arg[2,3] placeholders are
    replaced in template appearance order with the colon-args, mapped through
    the player-name table (matching the client's getPlayerName behaviour)."""
    if not text:
        return text
    out = text
    idx = 0
    for m in re.finditer(r'%(?:dest|arg3|arg2|arg|src)', text):
        token = m.group(0)
        if idx < len(args):
            out = out.replace(token, names.get(args[idx], args[idx]), 1)
        idx += 1
    return out


def card_names(cards):
    out = []
    for c in cards or []:
        text = str(c.get('objectName', '?'))
        suit = c.get('suit')
        number = c.get('number')
        if isinstance(suit, int) and 0 <= suit < 4:
            text += '/' + 'cdhs'[suit]
        if isinstance(number, int) and 0 < number < 14:
            text += str(number)
        out.append(text)
    return out


def usable_candidates(req):
    return [c for c in (req.get('cardCandidates') or [])
            if c.get('available') and not c.get('limited')]


def usable_conversions(req):
    return [c for c in (req.get('cardConversions') or []) if c.get('available')]


def random_token(req, domain):
    """Return (token, count) of the opaque random-card domain, or (None, 0)."""
    ctx = ((req.get('choiceOptions') or {}).get('context') or {})
    domains = ctx.get('random_card_choices') or {}
    entry = domains.get(domain)
    if isinstance(entry, dict) and entry.get('token'):
        return str(entry['token']), int(entry.get('count') or 0)
    return None, 0


def action_sig(req):
    """Canonical fingerprint of the declared action space (same-shape test)."""
    core = {
        'kind': req.get('kind'), 'pattern': req.get('pattern'),
        'prompt': req.get('prompt'), 'choiceOptions': req.get('choiceOptions'),
        'cardCandidates': req.get('cardCandidates'),
        'cardConversions': req.get('cardConversions'),
        'skillActions': req.get('skillActions'),
    }
    return json.dumps(core, sort_keys=True, ensure_ascii=False)


def max_seq(wv):
    seqs = [int(e['sequence']) for e in (wv.get('events') or [])
            if e.get('sequence') is not None]
    return max(seqs) if seqs else 0


# ---------------------------------------------------------------------------
# §1 mechanical adjudicator — every rule is "answer only when the legal action
# space has exactly one element"; everything else escalates.
# ---------------------------------------------------------------------------

def local_answer(req):
    kind = req.get('kind')
    opt = req.get('choiceOptions') or {}
    optional = bool(opt.get('optional'))
    choices = opt.get('choices') or []
    card_ids = [int(x) for x in (opt.get('cardIds') or [])]
    player_names = opt.get('playerNames') or []
    visible_cards = opt.get('cards') or []
    min_c = opt.get('minCount')
    max_c = opt.get('maxCount')
    one = (min_c in (None, 1)) and (max_c in (None, 1))

    def answer(action, basis):
        return basis, {'kind': 'answer', 'action': action}

    def passthrough(basis):
        return basis, {'kind': 'pass', 'action': {}}

    if optional:
        # Rule 2: optional with zero legal actions -> pass. Optional with any
        # candidate/choice is a real decision (hard rule) -> escalate.
        # For response kinds, playerNames is metadata (e.g. the dying-peach
        # target list), not part of the action space. A non-empty skillActions
        # means the declared space includes skill activations.
        if kind in (1, 13):
            space = (choices or card_ids or visible_cards
                     or usable_candidates(req) or usable_conversions(req)
                     or req.get('skillActions'))
        else:
            space = (choices or card_ids or player_names or visible_cards
                     or usable_candidates(req) or usable_conversions(req))
        if not space:
            return passthrough('optional-zero-actions')
        return None
    if kind in (1, 13):
        # Rule 3: response questions with zero usable candidates -> pass.
        # Never for skill asks: their space is the skill activation, and a
        # pass can bounce at use-time (the @shensu2 silent re-ask lesson).
        if (not (card_ids or usable_candidates(req) or usable_conversions(req))
                and not req.get('skillActions')):
            return passthrough('response-zero-candidates')
        if kind == 13 and len(card_ids) == 1 and one:
            return answer({'selectedCardIds': [card_ids[0]]}, 'respond-single-card')
        return None
    if kind in (2, 3, 15):
        if len(choices) == 1:
            return answer({'userString': choices[0]}, 'choices-single')
        return None
    if kind == 7:
        if card_ids and min_c is not None and len(card_ids) == min_c == max_c:
            return answer({'selectedCardIds': list(card_ids)}, 'discard-all-exact')
        return None
    if kind == 8:
        if len(card_ids) == 1 and one:
            return answer({'selectedCardIds': [card_ids[0]]}, 'amazinggrace-single')
        return None
    if kind == 9:
        if len(card_ids) == 1 and one:
            return answer({'selectedCardIds': [card_ids[0]]}, 'cardchosen-single')
        if len(visible_cards) == 1 and one:
            cid = int(visible_cards[0].get('cardId'))
            return answer({'selectedCardIds': [cid]}, 'cardchosen-single')
        # Opaque domain: faces hidden (no visible cards, no cardIds) so any
        # pick is equivalent -> declared token at index 1.
        if (not visible_cards and not card_ids and one and len(choices) == 1):
            token, count = random_token(req, choices[0])
            if token and count >= 1:
                return answer({'randomCardChoice': {'token': token, 'index': 1}},
                              'cardchosen-opaque-index1')
        if card_ids and min_c is not None and len(card_ids) == min_c == max_c:
            # Forced pick-all: the action space is still exactly one element.
            return answer({'selectedCardIds': list(card_ids)}, 'cardchosen-pick-all')
        return None
    if kind in (11, 12):
        if len(player_names) == 1 and one:
            return answer({'selectedTargetNames': [player_names[0]]}, 'players-single')
        if player_names and min_c is not None and len(player_names) == min_c == max_c:
            return answer({'selectedTargetNames': list(player_names)}, 'players-pick-all')
        return None
    if kind == 0:
        # Play phase with nothing usable: pass is the only expressible action
        # (same class as response-zero-candidates).
        if (not (usable_candidates(req) or usable_conversions(req))
                and not choices and not card_ids):
            return passthrough('play-zero-usable')
        return None
    return None


def escalation_reason(req):
    """Reason token for the brief title line (§5 routing hook)."""
    kind = req.get('kind')
    opt = req.get('choiceOptions') or {}
    if bool(opt.get('optional')):
        return 'optional'
    choices = opt.get('choices') or []
    card_ids = opt.get('cardIds') or []
    player_names = opt.get('playerNames') or []
    if kind in (1, 13):
        return 'kind:%d' % kind
    if kind in (2, 3, 15):
        return 'space>1' if len(choices) > 1 else 'kind:%d' % kind
    if kind == 7:
        return 'space>1'          # reached only when cardIds count != min == max
    if kind in (8, 9):
        return 'space>1' if len(card_ids) > 1 else 'kind:%d' % kind
    if kind in (11, 12):
        return 'space>1' if len(player_names) > 1 else 'kind:%d' % kind
    return 'kind:%d' % kind


def mock_answer(req, attempt):
    """Deterministic fallback for escalated questions.

    attempt 0 mirrors multiseat_probe/mockExternalAgentAnswer semantics
    (pass play/optional, first mandatory choice); attempt 1 passes;
    attempt 2 picks the alternate end. None means the ladder is exhausted.
    """
    kind = req.get('kind')
    opt = req.get('choiceOptions') or {}
    optional = bool(opt.get('optional'))
    choices = opt.get('choices') or []
    card_ids = [int(x) for x in (opt.get('cardIds') or [])]
    player_names = opt.get('playerNames') or []
    min_c = opt.get('minCount')

    def ans(action):
        return {'kind': 'answer', 'action': action}

    if attempt == 0:
        if kind in (0, 1) or optional:
            return {'kind': 'pass', 'action': {}}
        if kind == 14:
            field = 'bottomCardIds' if opt.get('defaultChoice') == '2' else 'selectedCardIds'
            return ans({field: list(card_ids)})
        if kind == 9 and choices and not card_ids:
            token, count = random_token(req, choices[0])
            if token and count >= 1:
                return ans({'randomCardChoice': {'token': token, 'index': 1}})
        if kind in (11, 12):
            return ans({'selectedTargetNames': list(player_names[:min_c])})
        if card_ids:
            return ans({'selectedCardIds': list(card_ids[:min_c])})
        if choices:
            return ans({'userString': choices[0]})
        return {'kind': 'pass', 'action': {}}
    if attempt == 1:
        return {'kind': 'pass', 'action': {}}
    if attempt == 2:
        if len(choices) > 1:
            return ans({'userString': choices[1]})
        if len(card_ids) > 1:
            return ans({'selectedCardIds': [card_ids[-1]]})
        if len(player_names) > 1:
            return ans({'selectedTargetNames': [player_names[-1]]})
        return {'kind': 'pass', 'action': {}}
    return None


# ---------------------------------------------------------------------------
# Contract self-check applied before every submission (advisory for mock,
# blocking for local answers and LLM answers).
# ---------------------------------------------------------------------------

def contract_check(req, result):
    errs = []
    kind = req.get('kind')
    act = result.get('action') or {}
    opt = req.get('choiceOptions') or {}
    if kind == 13:
        if result.get('kind') == 'useCard':
            errs.append('respond-must-not-useCard')
        if 'useCardId' in act:
            errs.append('respond-no-useCardId')
    allowed_cards = set(int(x) for x in (opt.get('cardIds') or []))
    allowed_targets = set(opt.get('playerNames') or [])
    for c in req.get('cardCandidates') or []:
        if c.get('available') and not c.get('limited'):
            allowed_cards.add(int(c['cardId']))
        allowed_targets.update(c.get('legalTargets') or [])
        if (c.get('targetFixed') and result.get('kind') == 'useCard'
                and act.get('candidateId') == c.get('candidateId')
                and act.get('selectedTargetNames')):
            errs.append('targetFixed-with-targets')
    if result.get('kind') == 'useCard' and act.get('candidateId') is not None:
        # Cross-check the candidateId/useCardId pairing and keep target
        # sequences inside the declared targetCombinations (the run2b n=26
        # mismatch reached the engine because this check was missing).
        cand = next((c for c in (req.get('cardCandidates') or [])
                     if c.get('candidateId') == act.get('candidateId')), None)
        if cand is None:
            errs.append('candidateId-not-declared:%s' % act.get('candidateId'))
        else:
            if (act.get('useCardId') is not None
                    and int(act['useCardId']) != int(cand.get('cardId'))):
                errs.append('candidateId-cardId-mismatch')
            combos = [list(x) for x in (cand.get('targetCombinations') or [])]
            if combos:
                targets = list(act.get('selectedTargetNames') or [])
                if targets not in combos:
                    errs.append('targets-outside-targetCombinations')
    for cv in usable_conversions(req):
        allowed_cards.update(int(x) for x in (cv.get('eligibleSubcardIds') or []))
        allowed_targets.update(cv.get('legalTargets') or [])
    # Skill asks own their material/target space (e.g. ShensuViewAsSkill::
    # canSelectCard accepts hand equips that the candidate projection never
    # offers), so the declaration checks apply only to non-skill asks.
    skill_ask = bool(req.get('skillActions'))
    for cid in act.get('selectedCardIds') or []:
        if not skill_ask and int(cid) not in allowed_cards:
            errs.append('card-source-not-declared:%s' % cid)
    if act.get('userString') is not None and act['userString'] not in (opt.get('choices') or []):
        errs.append('choice-not-declared:%s' % act['userString'])
    for t in act.get('selectedTargetNames') or []:
        if not skill_ask and t not in allowed_targets:
            errs.append('target-not-declared:%s' % t)
    rcc = act.get('randomCardChoice')
    if rcc is not None:
        domains = ((opt.get('context') or {}).get('random_card_choices') or {})
        token_ok = any(isinstance(v, dict) and v.get('token') == rcc.get('token')
                       for v in domains.values() if isinstance(v, dict))
        if not token_ok:
            errs.append('randomCard-token-not-declared')
        elif not isinstance(rcc.get('index'), int):
            errs.append('randomCard-index-not-int')
    return errs


# ---------------------------------------------------------------------------
# Brief rendering (§2/§3): fixed section order, byte-identical template,
# JSON key order preserved, volatile content tailed.
# ---------------------------------------------------------------------------

def seat_bg_payload(wv):
    selfv = wv.get('self') or {}
    return {
        'general': selfv.get('generalName'), 'general2': selfv.get('general2Name'),
        'kingdom': selfv.get('kingdom'), 'lord': selfv.get('lord'),
        'skills': [s.get('skillName') for s in selfv.get('skills') or []],
        'equips': card_names(selfv.get('equips')),
        'hand': card_names(wv.get('handCards')),
    }


def render_seat_background(wv, prev_key, lang=None):
    key = json.dumps(seat_bg_payload(wv), ensure_ascii=False)
    if key == prev_key:
        return ['同上'], key
    selfv = wv.get('self') or {}
    general = selfv.get('generalName') or '?'
    general_t = lookup_lang(lang, general, cap=60) if lang else None
    lines = ['- general: %s（kingdom %s%s）' % (
        '%s(%s)' % (general_t, general) if general_t else general,
        selfv.get('kingdom') or '?',
        '，lord' if selfv.get('lord') else '')]
    if selfv.get('general2Name'):
        lines[0] = '- general: %s/%s（kingdom %s%s）' % (
            selfv.get('generalName'), selfv.get('general2Name'),
            selfv.get('kingdom') or '?', '，lord' if selfv.get('lord') else '')
    skills = [s.get('skillName') for s in selfv.get('skills') or []]
    if lang and skills:
        named = ['%s(%s)' % (s, lookup_lang(lang, s) or '?') if lookup_lang(lang, s)
                 else s for s in skills]
        lines.append('- skills: %s' % ('、'.join(named) if named else '無'))
    else:
        lines.append('- skills: %s' % ('、'.join(skills) if skills else '無'))
    equips = card_names(selfv.get('equips'))
    lines.append('- equips: %s' % ('、'.join(equips) if equips else '無'))
    hand = card_names(wv.get('handCards'))
    lines.append('- hand: %s（%d 張）' % ('、'.join(hand) if hand else '空', len(hand)))
    return lines, key


def player_record(p):
    return {
        'hp': p.get('hp'), 'maxHp': p.get('maxHp'),
        'hand': p.get('handcardCount'), 'alive': p.get('alive'),
        'judging': card_names(p.get('judgingArea')),
        'equips': card_names(p.get('equips')),
    }


def _is_card_object(obj):
    return (isinstance(obj, dict)
            and ('id' in obj or 'cardId' in obj)
            and ('name' in obj or 'objectName' in obj)
            and 'kind_of' in obj)


def _compact_cards(obj, table, depth=0):
    """Replace nested card objects with cardRef entries collected in `table`
    (first appearance wins). Key order of everything kept is preserved."""
    if depth > 4:
        return obj
    if isinstance(obj, dict):
        if _is_card_object(obj) and obj.get('id') is not None:
            cid = int(obj['id'])
            table.setdefault(cid, obj)
            return {'cardRef': cid}
        return {k: _compact_cards(v, table, depth + 1) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_compact_cards(v, table, depth + 1) for v in obj]
    return obj


def _is_maintenance_event(e):
    details = e.get('details')
    return (e.get('kind') == 'other' and not e.get('amount')
            and not e.get('cardName') and not e.get('cardClass')
            and isinstance(details, dict) and set(details) <= {'player'})


def _compact_context(ctx, table):
    """Dedup nested card objects inside choiceOptions.context into `table`
    and drop the redundant `original:*` mirror domains of random_card_choices
    (base-domain tokens are the answerable ones). Key order preserved."""
    compacted = _compact_cards(ctx, table)
    rcc = compacted.get('random_card_choices')
    if isinstance(rcc, dict):
        compacted['random_card_choices'] = {
            key: val for key, val in rcc.items()
            if not key.startswith('original:')}
    return compacted


def render_increment(req, prev_req, last_seq, table):
    wv = req.get('worldView') or {}
    mseq = max_seq(wv)
    lines = ['- 事件游標: 上次決策 max sequence=%s → 本題=%s' % (last_seq, mseq)]
    events = [e for e in (wv.get('events') or [])
              if e.get('sequence') is not None and int(e['sequence']) > last_seq]
    events.sort(key=lambda e: int(e['sequence']))
    kept = [e for e in events if not _is_maintenance_event(e)]
    noise = [e for e in events if _is_maintenance_event(e)]
    lines.append('- 新增 events（sequence > %s，共 %d 筆；維護事件 %d 筆折疊）:'
                 % (last_seq, len(events), len(noise)))
    if noise:
        codes = sorted({int(e['triggerEvent']) for e in noise
                        if e.get('triggerEvent') is not None})
        lines.append('  - （折疊 %d 筆維護事件：kind=other、amount=0、無牌面；'
                     'triggerEvent=%s；完整內容見 pending DTO）'
                     % (len(noise), codes))
    shown = kept[:MAX_EVENTS_PER_BRIEF]
    for e in shown:
        compact = {k: _compact_cards(e[k], table) for k in e if k in EVENT_FIELDS}
        lines.append('  - %s' % json.dumps(compact, ensure_ascii=False))
    if len(kept) > len(shown):
        lines.append('  - …（其餘 %d 筆略，完整內容見 pending DTO）' % (len(kept) - len(shown)))
    if not events:
        lines.append('  - （無）')
    lines.append('- 玩家變動（hp／手牌數／判定區／裝備／存活）:')
    records = {}
    for p in wv.get('players') or []:
        records[p.get('objectName')] = player_record(p)
    # worldView.players excludes the viewer; track self hp/hand explicitly.
    selfv = wv.get('self') or {}
    self_name = req.get('viewerObjectName') or selfv.get('controller') or 'self'
    if selfv:
        records[self_name] = player_record(selfv)
    prev_records = {}
    if prev_req is not None:
        prev_wv = prev_req.get('worldView') or {}
        for p in prev_wv.get('players') or []:
            prev_records[p.get('objectName')] = player_record(p)
        prev_self = prev_wv.get('self') or {}
        if prev_self:
            prev_records[prev_req.get('viewerObjectName') or prev_self.get('controller') or 'self'] = player_record(prev_self)
    changes = []
    for name, cur in records.items():
        prev = prev_records.get(name)
        if prev is None:
            changes.append('%s: 初見 hp=%s/%s hand=%s alive=%s judging=%s equips=%s' % (
                name, cur['hp'], cur['maxHp'], cur['hand'], cur['alive'],
                cur['judging'], cur['equips']))
            continue
        bits = []
        if (prev['hp'], prev['maxHp']) != (cur['hp'], cur['maxHp']):
            bits.append('hp %s/%s→%s/%s' % (prev['hp'], prev['maxHp'], cur['hp'], cur['maxHp']))
        if prev['hand'] != cur['hand']:
            bits.append('hand %s→%s' % (prev['hand'], cur['hand']))
        if prev['alive'] != cur['alive']:
            bits.append('alive %s→%s' % (prev['alive'], cur['alive']))
        for field, label in (('judging', '判定區'), ('equips', '裝備')):
            added = [x for x in cur[field] if x not in prev[field]]
            removed = [x for x in prev[field] if x not in cur[field]]
            if added:
                bits.append('%s +%s' % (label, added))
            if removed:
                bits.append('%s -%s' % (label, removed))
        if bits:
            changes.append('%s: %s' % (name, '、'.join(str(b) for b in bits)))
    lines.extend('  - %s' % c for c in changes)
    if not changes:
        lines.append('  - （無）')
    return lines, mseq


def render_notes_section(notes_path):
    lines = ['- 筆記檔路徑: %s（由 LLM 自行維護；橋接只讀不寫）' % notes_path]
    try:
        with open(notes_path, 'r', encoding='utf-8') as fh:
            content = fh.read()
    except OSError:
        lines.append('- 目前狀態: 尚未建立')
        return lines
    lines.append('- 目前狀態: 存在（%d bytes），摘錄如下' % len(content.encode('utf-8')))
    excerpt = content
    if len(excerpt) > 6000:
        excerpt = '（前段省略）\n' + excerpt[-6000:]
    lines.append('<notes>')
    lines.extend(excerpt.splitlines())
    lines.append('</notes>')
    return lines


def render_brief(req, seat_name, n, reason, notes_path, prev_req, last_seq,
                 prev_bg_key, retry_notes, lang=None):
    wv = req.get('worldView') or {}
    kind = req.get('kind')
    opt = req.get('choiceOptions') or {}
    lines = ['# 決策簡報（升級：%s）' % reason, '', '## 契約備忘', '']
    lines.append(CONTRACT_MEMO)
    lines.append('## 座位背景')
    bg_lines, bg_key = render_seat_background(wv, prev_bg_key, lang)
    lines.extend(bg_lines)
    lines.append('')
    lines.append('## 筆記')
    lines.extend(render_notes_section(notes_path))
    lines.append('')
    table = {}
    inc_lines, mseq = render_increment(req, prev_req, last_seq, table)
    compact_opt = {k: (_compact_context(v, table) if k == 'context' else v)
                   for k, v in opt.items()}
    lines.append('## 本題')
    if table:
        lines.append('- 卡牌對照（本簡報去重；choiceOptions 與事件行以 cardRef 引用）:')
        for cid, c in table.items():
            cname = c.get('name') or c.get('objectName') or '?'
            cname_t = lookup_lang(lang, cname, cap=120) if lang else None
            if cname_t:
                lines.append('  - card %d: %s (%s, suit %s, number %s) → %s' % (
                    cid, cname, c.get('class_name') or '?', c.get('suit'),
                    c.get('number'), cname_t))
            else:
                lines.append('  - card %d: %s (%s, suit %s, number %s)' % (
                    cid, cname, c.get('class_name') or '?', c.get('suit'), c.get('number')))
    if lang:
        # Player display names: engine-style translation of general names, so
        # %src/%dest/%arg placeholders render like the client's getPlayerName.
        names = {}
        for p in wv.get('players') or []:
            g = p.get('generalName')
            if g:
                names[p.get('objectName')] = lang.get(g, g)
        selfv = wv.get('self') or {}
        g = selfv.get('generalName')
        if g:
            names[req.get('viewerObjectName') or selfv.get('controller') or ''] = lang.get(g, g)
        trans_rows = []
        seen = set()

        def add_key(key):
            if not key:
                return
            for candidate in translate_candidates(str(key)):
                if candidate not in seen and lookup_lang(lang, candidate):
                    seen.add(candidate)
                    trans_rows.append((candidate, lookup_lang(lang, candidate)))
                    break

        prompt = req.get('prompt')
        if prompt:
            mk, text = lookup_lang_pair(lang, prompt)
            if mk:
                args = prompt[len(mk) + 1:].split(':') if prompt.startswith(mk + ':') else []
                trans_rows.append((prompt, substitute_prompt(text, args, names)))
        add_key(req.get('pattern'))
        add_key(opt.get('reason'))
        add_key(opt.get('question'))
        for sa in req.get('skillActions') or []:
            for ref in (sa.get('activationRef') or {}, sa.get('sourceRef') or {}):
                add_key((ref.get('key') or {}).get('skillName') or ref.get('skillName'))
        if kind in (2, 3) and opt.get('choices') and opt.get('reason'):
            # Choice options use exact 'skill:option' keys only — falling back
            # to the bare skill name would mislabel plain yes/no options.
            reason_key = str(opt.get('reason'))
            for ch in opt.get('choices') or []:
                base, _, rest = str(ch).partition('=')
                okey = '%s:%s' % (reason_key, base)
                name = lang.get(okey)
                desc = lang.get(':' + okey)
                if not (name or desc):
                    continue
                text = name or ''
                if desc:
                    text = (text + '｜' if text else '') + desc[:220]
                args = [rest] if rest else []
                trans_rows.append((str(ch), substitute_prompt(text, args, names)))
        if trans_rows:
            lines.append('- 譯名對照（引擎 lang/zh_CN 原文照錄，簡體）:')
            for label, rendered in trans_rows:
                lines.append('  - %s → %s' % (label, rendered))
    lines.append('- kind: %s (%s)' % (kind, KIND_NAMES.get(kind, '?')))
    lines.append('- pattern: %s' % (req.get('pattern') if req.get('pattern') else '無'))
    lines.append('- prompt: %s' % (req.get('prompt') if req.get('prompt') else '無'))
    lines.append('- 升級原因: %s' % reason)
    for note in retry_notes:
        lines.append('- ⚠ %s' % note)
    lines.append('- choiceOptions:')
    lines.append('```json')
    lines.append(json.dumps(compact_opt, ensure_ascii=False, indent=1))
    lines.append('```')
    lines.append('- cardCandidates:')
    lines.append('```json')
    lines.append(json.dumps(req.get('cardCandidates') or [], ensure_ascii=False, indent=1))
    lines.append('```')
    lines.append('- cardConversions:')
    lines.append('```json')
    lines.append(json.dumps(req.get('cardConversions') or [], ensure_ascii=False, indent=1))
    lines.append('```')
    if req.get('skillActions'):
        lines.append('- skillActions:')
        lines.append('```json')
        lines.append(json.dumps(req.get('skillActions'), ensure_ascii=False, indent=1))
        lines.append('```')
    lines.append('')
    lines.append('## 局面增量（上次決策以來）')
    lines.extend(inc_lines)
    lines.append('')
    lines.append('## 尾註')
    lines.append('- 決策編號: %d' % n)
    lines.append('- 座位: %s' % seat_name)
    lines.append('- stateRevision: %s / worldView.revision: %s / 事件游標: %s' % (
        req.get('stateRevision'), wv.get('revision'), mseq))
    lines.append('- pending 完整 DTO: pending-%d.json（與本簡報同目錄）' % n)
    lines.append('- 答案檔（寫入此路徑）: answer-%d.json（與本簡報同目錄）' % n)
    lines.append('- 產生時間: %s（易變內容僅存證，不入前綴）' % now_iso())
    return '\n'.join(lines) + '\n', mseq, bg_key


# ---------------------------------------------------------------------------
# Per-seat runtime state
# ---------------------------------------------------------------------------

class SeatState:
    def __init__(self, name, call, sock, fh):
        self.name = name
        self.call = call
        self.sock = sock
        self.fh = fh
        self.n = 0                    # accepted decision counter (all routes)
        self.last_revision = '0'
        self.last_event_sequence = 0
        self.prev_request = None      # previous accepted DTO (increment diff)
        self.prev_bg_key = None       # seat-background reprint logic
        self.last_submit = None       # {'did','sig','seq','route','n'}
        self.last_attempt = None      # decisionId of the last submit attempt
        self.last_attempt_route = None
        self.last_attempt_resolver = None
        self.poison = set()           # signatures permanently escalated
        self.mock_attempts = {}       # sig -> mock ladder attempt
        self.brief_notes = {}         # n -> [retry note strings]
        self.reopen_counts = {}       # decisionId -> reopen count (bounded)
        self.replayed = 0
        self.local = 0
        self.escalated = 0
        self.auto_rejected = 0
        self.reopens = 0


def write_atomic(path, text):
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text)
    os.replace(tmp, path)


class Driver:
    def __init__(self, args):
        self.args = args
        self.run_dir = os.path.abspath(args.run_dir)
        self.game_dir = os.path.join(self.run_dir, 'game')
        self.brief_dir = os.path.abspath(args.brief_dir or os.path.join(self.run_dir, 'briefs'))
        self.userdata = os.path.join(self.run_dir, 'userdata')
        self.auto_log = os.path.abspath(args.auto_log or os.path.join(self.run_dir, 'auto-answers.jsonl'))
        self.notes_path = os.path.abspath(args.notes or os.path.join(self.game_dir, 'notes.md'))
        self.lang = load_lang_table(os.path.abspath(args.asset))
        for d in (self.run_dir, self.game_dir, self.brief_dir, self.userdata):
            os.makedirs(d, exist_ok=True)
        self.audit_fh = open(self.auto_log, 'a', encoding='utf-8', newline='\n')
        self.deadline = time.monotonic() + args.max_seconds
        self.shared_seq = 0
        self.stats = {'deaths': [], 'damage': {}, 'last_damage': {},
                      'skill_activations': [], 'max_round': 0}
        self.stats_seen = set()
        self.replay = {}
        self.replay_winner = None
        if args.replay:
            with open(os.path.abspath(args.replay), encoding='utf-8') as fh:
                for line in fh:
                    try:
                        row = json.loads(line)
                    except ValueError:
                        continue
                    if row.get('decisionId') and row.get('answer'):
                        self.replay[row['decisionId']] = row
                    if row.get('event') == 'game_over':
                        self.replay_winner = row.get('winner')
            self.log('REPLAY-LOADED decisions=%d recorded_winner=%s' % (
                len(self.replay), self.replay_winner))

    # -- audit --------------------------------------------------------------
    def audit(self, **row):
        row_full = {'time': now_iso()}
        row_full.update(row)
        self.audit_fh.write(json.dumps(row_full, ensure_ascii=False) + '\n')
        self.audit_fh.flush()

    def _board(self, req):
        """Board snapshot for the audit trail: round number and living players."""
        wv = req.get('worldView') or {}
        return {'round': wv.get('turnCount'),
                'alive': wv.get('alivePlayerOrder') or []}

    def tally_decision(self, seat, req):
        """Accumulate post-game review facts (damage ledger, deaths, skill
        activations) from an accepted decision's event delta and board diff.
        Events deduplicate by global sequence across seats. Returns the delta
        summary for the audit row."""
        wv = req.get('worldView') or {}
        cursor = seat.last_event_sequence
        rnd = wv.get('turnCount')
        try:
            if rnd is not None and int(rnd) > self.stats['max_round']:
                self.stats['max_round'] = int(rnd)
        except (TypeError, ValueError):
            pass
        events = [e for e in (wv.get('events') or [])
                  if e.get('sequence') is not None and int(e['sequence']) > cursor]
        delta = {'new_events': len(events), 'damage': 0}
        for e in events:
            seq = str(e.get('sequence'))
            if seq in self.stats_seen:
                continue
            self.stats_seen.add(seq)
            if e.get('kind') != 'damage':
                continue
            details = e.get('details') or {}
            amount = e.get('amount')
            if not isinstance(amount, int) or amount <= 0 or details.get('prevented'):
                continue
            src, dst = e.get('from'), e.get('to')
            card = e.get('cardName') or (details.get('card_name') or '?')
            entry = self.stats['damage'].setdefault((src, dst), {'total': 0, 'cards': {}})
            entry['total'] += amount
            delta['damage'] += amount
            if card and card != '?':
                entry['cards'][card] = entry['cards'].get(card, 0) + amount
            self.stats['last_damage'][dst] = {'round': rnd, 'by': src, 'card': card}
        prev = seat.prev_request
        if prev is not None:
            prev_alive = [p.get('objectName')
                          for p in (prev.get('worldView') or {}).get('players') or []
                          if p.get('alive')]
            cur_alive = [p.get('objectName') for p in (wv.get('players') or []) if p.get('alive')]
            for name in prev_alive:
                if name not in cur_alive and not any(d['name'] == name
                                                     for d in self.stats['deaths']):
                    self.stats['deaths'].append({
                        'round': rnd, 'name': name,
                        'last_damage': dict(self.stats['last_damage'].get(name) or {})})
        self.stats['last_board'] = {'round': rnd,
                                    'alive': [p.get('objectName')
                                              for p in (wv.get('players') or []) if p.get('alive')]}
        return delta

    def render_game_review(self, summary):
        """Post-game review document: ending, death ledger, damage ledger,
        external-seat activations — data derived from the audited stream."""
        st = self.stats
        lines = ['# 賽後分析',
                 '',
                 '- 結果: winner=%s（模式 %s、seed %s、統計窗口至第 %s 回合；自外席首次決策起計）' % (
                     summary.get('winner'), summary.get('mode'), summary.get('seed'),
                     st['max_round'] or '?'),
                 '- 決策: %s' % json.dumps(summary.get('seats') or {}, ensure_ascii=False)]
        lines.append('')
        lines.append('## 陣亡簿')
        last_board = st.get('last_board') or {}
        if st['deaths']:
            for d in st['deaths']:
                last = d.get('last_damage') or {}
                tail = ('（最後重創: 第 %s 回合 %s 的 %s）' % (
                    last.get('round'), last.get('by'), last.get('card'))
                    if last.get('by') else '（統計窗口內無傷害記錄）')
                lines.append('- 第 %s 回合: %s 陣亡 %s' % (d.get('round'), d.get('name'), tail))
        else:
            lines.append('- （統計窗口內無陣亡記錄）')
        if last_board:
            lines.append('- 末次決策時盤面（第 %s 回合）: %s 存活' % (
                last_board.get('round'), '、'.join(last_board.get('alive') or [])))
        lines.append('- 註: 身份局終局以主公死亡為前提；發生於末次決策之後的死亡不在統計窗口'
                     '（v1 協議終局僅回 winner，不回傳終局盤面）。')
        lines.append('')
        lines.append('## 傷害帳本（前 12，來源→目標）')
        ranked = sorted(st['damage'].items(), key=lambda kv: -kv[1]['total'])[:12]
        if ranked:
            for (src, dst), entry in ranked:
                top_cards = sorted(entry['cards'].items(), key=lambda kv: -kv[1])[:3]
                cards = '、'.join('%s×%s' % (c, n) for c, n in top_cards) or '無牌面記錄'
                lines.append('- %s → %s: 累計 %s 點（主要: %s）' % (src, dst, entry['total'], cards))
        else:
            lines.append('- （統計窗口內無傷害事件）')
        lines.append('')
        lines.append('## 外席技能發動')
        if st['skill_activations']:
            for a in st['skill_activations']:
                lines.append('- 第 %s 回合 %s 發動 %s' % (a.get('round'), a.get('seat'), a.get('skill')))
        else:
            lines.append('- （無 skillActionContext 型技能發動）')
        return '\n'.join(lines) + '\n'

    def log(self, msg):
        print(msg, flush=True)

    # -- brief / answer file plumbing ---------------------------------------
    def seat_brief_dir(self, seat):
        d = os.path.join(self.brief_dir, seat.name)
        os.makedirs(d, exist_ok=True)
        return d

    def write_briefs(self, seat, req, reason):
        d = self.seat_brief_dir(seat)
        n = seat.n
        pending_path = os.path.join(d, 'pending-%d.json' % n)
        write_atomic(pending_path, json.dumps(req, ensure_ascii=False, indent=1) + '\n')
        # prev_bg_key holds the seat background as *last printed in a brief*:
        # local decisions produce no brief, so the next brief must reprint
        # the full background instead of printing a dangling 同上.
        text, mseq, bg_key = render_brief(
            req, seat.name, n, reason, self.notes_path, seat.prev_request,
            seat.last_event_sequence, seat.prev_bg_key, seat.brief_notes.get(n, []),
            self.lang)
        seat.prev_bg_key = bg_key
        brief_path = os.path.join(d, 'decision-%d.md' % n)
        write_atomic(brief_path, text)
        answer_path = os.path.join(d, 'answer-%d.json' % n)
        return brief_path, answer_path, mseq, bg_key

    def wait_answer(self, seat, answer_path, n):
        per_deadline = time.monotonic() + self.args.answer_wait_seconds
        deadline = min(per_deadline, self.deadline)
        while time.monotonic() < deadline:
            if time.monotonic() > self.deadline:
                return None
            if os.path.exists(answer_path):
                time.sleep(0.08)  # let the writer finish
                try:
                    with open(answer_path, 'r', encoding='utf-8') as fh:
                        result = json.load(fh)
                    if not isinstance(result, dict) or 'kind' not in result:
                        raise ValueError('answer must be a JSON object with "kind"')
                    os.remove(answer_path)  # consumed; a reopen must not re-read it
                    return result
                except (ValueError, OSError) as exc:
                    self.audit(event='bad-answer-file', seat=seat.name, n=n,
                               error=str(exc))
                    self.log('BAD-ANSWER seat=%s n=%d error=%s' % (seat.name, n, exc))
                    try:
                        os.remove(answer_path)
                    except OSError:
                        pass
                    return {'__bad__': True}
            time.sleep(0.15)
        return None

    # -- submission ---------------------------------------------------------
    def submit(self, seat, req, result, route, resolver=None, basis=None,
               reason=None, brief=None, attempt=None):
        result.setdefault('decisionId', req['decisionId'])
        result.setdefault('stateRevision', req['stateRevision'])
        seat.last_attempt = req['decisionId']
        seat.last_attempt_route = route
        seat.last_attempt_resolver = resolver
        errs = contract_check(req, result)
        check_note = 'ok' if not errs else 'violations:' + ';'.join(errs)
        if errs and route == 'local':
            self.audit(**self._board(req), seat=seat.name, n=seat.n, decisionId=req['decisionId'],
                       kind=req.get('kind'), route='local', basis=basis,
                       selfcheck=check_note, submitted='blocked-by-selfcheck')
            self.log('LOCAL-BLOCKED seat=%s n=%d %s' % (seat.name, seat.n, check_note))
            return {'status': 'selfcheck-blocked', 'errors': errs}
        if errs and resolver == 'llm':
            self.audit(**self._board(req), seat=seat.name, n=seat.n, decisionId=req['decisionId'],
                       kind=req.get('kind'), route='escalated', resolver='llm',
                       reason=reason, brief=brief, selfcheck=check_note,
                       submitted='blocked-by-selfcheck')
            self.log('LLM-BLOCKED seat=%s n=%d %s' % (seat.name, seat.n, check_note))
            return {'status': 'selfcheck-blocked', 'errors': errs}
        reply = seat.call({'op': 'submit', 'result': result})
        if reply.get('ok'):
            wv = req.get('worldView') or {}
            delta = self.tally_decision(seat, req)
            seat.n += 1
            seat.last_revision = str(wv.get('revision') or seat.last_revision)
            seq = max_seq(wv)
            if self.args.shared_cursor:
                # Single-LLM-session multi-seat: every seat inherits the shared
                # cursor so later seats skip events an earlier brief showed.
                self.shared_seq = max(self.shared_seq, seq)
                for other in self.seats:
                    other.last_event_sequence = self.shared_seq
            else:
                seat.last_event_sequence = max(seat.last_event_sequence, seq)
            seat.prev_request = req
            act_ctx = (result.get('action') or {}).get('skillActionContext') or {}
            skill_name = (act_ctx.get('activationRef') or {}).get('skillName')
            if skill_name:
                self.stats['skill_activations'].append({
                    'seat': seat.name, 'round': wv.get('turnCount'),
                    'skill': skill_name})
            if route == 'local':
                seat.local += 1
            elif route == 'replay':
                seat.replayed += 1
            else:
                seat.escalated += 1
            seat.last_submit = {'did': req['decisionId'], 'sig': action_sig(req),
                                'seq': seat.last_event_sequence,
                                'rev': req.get('stateRevision'), 'route': route,
                                'n': seat.n - 1}
            self.audit(**self._board(req), **delta, seat=seat.name, n=seat.n - 1, decisionId=req['decisionId'],
                       kind=req.get('kind'), route=route,
                       **({'basis': basis} if route == 'local' else
                          {'reason': reason, 'resolver': resolver, 'brief': brief,
                           'attempt': attempt}),
                       selfcheck=check_note, answer=result, submitted='ok')
            self.log('SUBMIT-OK seat=%s n=%d route=%s %s' % (
                seat.name, seat.n - 1, route, basis or ('reason=%s resolver=%s' % (reason, resolver))))
            return {'status': 'ok'}
        self.audit(**self._board(req), seat=seat.name, n=seat.n, decisionId=req['decisionId'],
                   kind=req.get('kind'), route=route,
                   **({'basis': basis} if route == 'local' else
                      {'reason': reason, 'resolver': resolver, 'brief': brief}),
                   selfcheck=check_note, answer=result,
                   submitted='rejected', error=reply.get('error'))
        self.log('SUBMIT-REJECTED seat=%s n=%d error=%s' % (seat.name, seat.n, reply.get('error')))
        return {'status': 'rejected', 'error': reply.get('error')}

    # -- one decision -------------------------------------------------------
    def handle_decision(self, seat, req, state):
        wv = req.get('worldView') or {}
        kind = req.get('kind')
        did = req.get('decisionId')
        sig = action_sig(req)
        mseq = max_seq(wv)
        lasterr = state.get('lastError')
        retry_notes = []

        if self.args.replay and not lasterr:
            row = self.replay.get(did)
            if (row is not None
                    and (self.args.replay_scope == 'all'
                         or row.get('route') == 'escalated')):
                self.submit(seat, req, row['answer'], route='replay',
                            resolver='replay',
                            reason='replay:%s' % row.get('route'))
                return
            self.log('REPLAY-MISS seat=%s did=%s -> normal flow' % (seat.name, did))

        if lasterr:
            seat.reopens += 1
            seat.reopen_counts[did] = seat.reopen_counts.get(did, 0) + 1
            if seat.reopen_counts[did] > 4:
                raise SystemExit('REOPEN-EXHAUSTED seat=%s did=%s lastError=%s'
                                 % (seat.name, did, lasterr))
            self.audit(event='reopen', seat=seat.name, n=seat.n, decisionId=did,
                       kind=kind, lastError=lasterr)
            self.log('REOPENED seat=%s n=%d did=%s lastError=%s' % (seat.name, seat.n, did, lasterr))
            if seat.last_attempt == did:
                retry_notes.append('重開 #%d：上次提交被引擎判定非法（lastError=%s）' % (
                    seat.reopens, lasterr))
                if seat.last_attempt_route == 'local':
                    # A gate-rejected local answer is an adjudicator defect:
                    # poison the signature (寧可升級不可誤答).
                    seat.poison.add(sig)
                    seat.auto_rejected += 1
                    self.audit(event='auto_rejected', seat=seat.name, n=seat.n,
                               decisionId=did, kind=kind, cause='illegal-action-reopen')
                    self.log('AUTO_REJECTED seat=%s sig poisoned (illegal-action reopen)' % seat.name)
                elif (seat.last_attempt_route == 'escalated'
                      and seat.last_attempt_resolver == 'mock'):
                    seat.mock_attempts[sig] = seat.mock_attempts.get(sig, 0) + 1
            seat.brief_notes.setdefault(seat.n, []).extend(retry_notes)
            local = None
            reason = 'reopen'
        else:
            # Silent re-ask: accepted submission followed by a same-shape
            # request with a fresh decisionId, no new events AND an unchanged
            # stateRevision (consecutive identical asks with advancing
            # revisions are legitimate separate decisions).
            prev = seat.last_submit
            if (prev and prev['did'] != did and prev['sig'] == sig
                    and prev['seq'] == mseq
                    and prev['rev'] == req.get('stateRevision')):
                seat.poison.add(sig)
                seat.auto_rejected += 1
                self.audit(event='auto_rejected', seat=seat.name, n=seat.n,
                           decisionId=did, kind=kind, cause='silent-reask',
                           prev_decisionId=prev['did'], prev_n=prev['n'])
                self.log('AUTO_REJECTED seat=%s n=%d silent re-ask after n=%d' % (
                    seat.name, seat.n, prev['n']))
            local = None if sig in seat.poison else local_answer(req)
            reason = 'auto_rejected' if sig in seat.poison else escalation_reason(req)

        if local is not None:
            basis, result = local
            self.log('LOCAL seat=%s n=%d kind=%s basis=%s' % (seat.name, seat.n, kind, basis))
            outcome = self.submit(seat, req, result, route='local', basis=basis)
            if outcome['status'] == 'selfcheck-blocked':
                self.handle_escalated(seat, req, kind, 'selfcheck-failed',
                                      forced_resolver=None,
                                      precheck_errors=outcome['errors'])
            return

        escalate_set = self.args.escalate_kinds
        routed = escalate_set is None or kind in escalate_set
        self.handle_escalated(seat, req, kind, reason, forced_resolver='mock' if not routed else None)

    def handle_escalated(self, seat, req, kind, reason, forced_resolver=None,
                         precheck_errors=None):
        sig = action_sig(req)
        if precheck_errors:
            seat.brief_notes.setdefault(seat.n, []).append(
                '前次本地答案自檢失敗：%s' % ';'.join(precheck_errors))
        resolver = forced_resolver or ('llm' if self.args.fallback == 'file' else 'mock')
        brief_path, answer_path, _mseq, _bg = self.write_briefs(seat, req, reason)
        self.log('BRIEF seat=%s n=%d reason=%s brief=%s' % (seat.name, seat.n, reason, brief_path))

        if resolver == 'mock':
            attempt = seat.mock_attempts.get(sig, 0)
            result = mock_answer(req, attempt)
            if result is None:
                raise SystemExit('MOCK-LADDER-EXHAUSTED seat=%s n=%d sig=%s'
                                 % (seat.name, seat.n, sig[:64]))
            self.submit(seat, req, result, route='escalated', resolver='mock',
                        reason=reason, brief=brief_path, attempt=attempt)
            return

        # LLM file-queue route: wait for answer-<n>.json, self-check, submit.
        tries = 0
        while True:
            tries += 1
            if tries > 4:
                raise SystemExit('LLM-RETRY-EXHAUSTED seat=%s n=%d' % (seat.name, seat.n))
            result = self.wait_answer(seat, answer_path, seat.n)
            if result is None:
                raise SystemExit('ANSWER-WAIT-TIMEOUT seat=%s n=%d path=%s'
                                 % (seat.name, seat.n, answer_path))
            if '__bad__' in result:
                seat.brief_notes.setdefault(seat.n, []).append(
                    '第 %d 次答案檔格式錯誤（需單一 JSON 物件，含 kind 欄位）' % tries)
                self.write_briefs(seat, req, reason)
                continue
            outcome = self.submit(seat, req, result, route='escalated',
                                  resolver='llm', reason=reason, brief=brief_path)
            if outcome['status'] == 'selfcheck-blocked':
                seat.brief_notes.setdefault(seat.n, []).append(
                    '第 %d 次答案自檢失敗：%s' % (tries, ';'.join(outcome['errors'])))
                self.write_briefs(seat, req, reason)
                continue
            break

    # -- host plumbing ------------------------------------------------------
    def spawn_host(self):
        args = self.args
        env = dict(os.environ)
        env['PATH'] = args.qt_bin + os.pathsep + env.get('PATH', '')
        env['QSAN_ASSET_ROOT'] = os.path.abspath(args.asset)
        env['QSAN_USER_DATA_ROOT'] = self.userdata
        err_f = open(os.path.join(self.game_dir, 'host-stderr.log'), 'w',
                     encoding='utf-8', errors='replace')
        out_f = open(os.path.join(self.game_dir, 'host-stdout.log'), 'w',
                     encoding='utf-8', errors='replace')
        proc = subprocess.Popen(
            [args.host, '--mode', args.mode, '--external-seats', str(args.external_seats),
             '--seed', str(args.seed)],
            stdout=subprocess.PIPE, stderr=err_f, text=True, encoding='utf-8',
            errors='replace', env=env, cwd=os.path.abspath(args.asset))
        self.host_stdout = []
        self.host_lock = threading.Lock()

        def drain():
            for line in proc.stdout:
                with self.host_lock:
                    self.host_stdout.append(line)
                out_f.write(line)
                out_f.flush()
            out_f.close()

        threading.Thread(target=drain, daemon=True).start()
        self.host_stderr_file = err_f
        return proc

    def collect_bootstraps(self, proc):
        bootstraps = []
        deadline = time.monotonic() + 30
        while len(bootstraps) < self.args.external_seats:
            if proc.poll() is not None:
                raise SystemExit('HOST-EXITED-EARLY rc=%s stdout=%s' % (
                    proc.returncode, ''.join(self.host_stdout)[:2000]))
            with self.host_lock:
                lines = [l for l in self.host_stdout if l.startswith('QSAN_AGENT_BOOTSTRAP ')]
            for line in lines[len(bootstraps):]:
                bootstraps.append(json.loads(line.split(' ', 1)[1]))
            if time.monotonic() > deadline:
                raise SystemExit('BOOTSTRAP-TIMEOUT')
            time.sleep(0.05)
        return bootstraps

    def connect_seats(self, bootstraps):
        seats = []
        for idx, b in enumerate(bootstraps):
            sock = socket.create_connection(('127.0.0.1', b['port']), timeout=5)
            f = sock.makefile('rwb')

            def call(obj, f=f):
                f.write(json.dumps(obj, separators=(',', ':')).encode() + b'\n')
                f.flush()
                return json.loads(f.readline(8 * 1024 * 1024 + 1))

            hello = call({'op': 'hello', 'version': 1, 'token': b['token']})
            if not hello.get('ok'):
                raise SystemExit('HELLO-FAILED seat=%d reply=%s' % (idx, hello))
            name = b.get('objectName') or ('seat%d' % idx)
            seats.append(SeatState(name, call, sock, f))
            self.log('CONNECTED seat=%s objectName=%s' % (idx, name))
        return seats

    def terminal_checks(self):
        with self.host_lock:
            stdout_text = ''.join(self.host_stdout)
        stderr_path = os.path.join(self.game_dir, 'host-stderr.log')
        stderr_text = ''
        try:
            with open(stderr_path, 'r', encoding='utf-8', errors='replace') as fh:
                stderr_text = fh.read()
        except OSError:
            pass
        corpus = stdout_text + '\n' + stderr_text
        out = {}
        m = re.search(r'callback_errors\s*[=:]\s*(\d+)', corpus)
        out['callback_errors'] = int(m.group(1)) if m else None
        for key in ('workers_stopped', 'room_destroyed'):
            m = re.search(r'%s\s*[=:]\s*(true|false)' % key, corpus, re.IGNORECASE)
            out[key] = m.group(1).lower() if m else None
        out['found'] = all(v is not None for v in
                           (out['callback_errors'], out['workers_stopped'], out['room_destroyed']))
        return out

    def run(self):
        args = self.args
        proc = self.spawn_host()
        bootstraps = self.collect_bootstraps(proc)
        seats = self.connect_seats(bootstraps)
        self.seats = seats
        self.log('DRIVER-START mode=%s seed=%s seats=%s fallback=%s escalate_kinds=%s' % (
            args.mode, args.seed, [s.name for s in seats], args.fallback,
            'all' if args.escalate_kinds is None else sorted(args.escalate_kinds)))
        finished = False
        winner = None
        try:
            while time.monotonic() < self.deadline:
                progressed = False
                for seat in seats:
                    state = seat.call({'op': 'poll'})
                    status = state.get('status')
                    if status == 'finished':
                        finished = True
                        winner = state.get('winner')
                        break
                    req = state.get('request')
                    if req is None:
                        continue
                    progressed = True
                    self.handle_decision(seat, req, state)
                if finished:
                    break
                if not progressed:
                    time.sleep(0.1)
            if not finished:
                raise SystemExit('DRIVER-BUDGET-EXPIRED max_seconds=%d' % args.max_seconds)
        except BaseException:
            # Any abort path must not leave the host process orphaned.
            try:
                proc.terminate()
            except OSError:
                pass
            raise
        finally:
            for seat in seats:
                try:
                    seat.fh.close()
                    seat.sock.close()
                except OSError:
                    pass
        summary = {
            'mode': args.mode, 'seed': args.seed,
            'fallback': args.fallback,
            'escalate_kinds': 'all' if args.escalate_kinds is None else sorted(args.escalate_kinds),
            'winner': winner, 'elapsed_seconds': round(time.monotonic() + args.max_seconds - self.deadline, 1),
            'seats': {s.name: {'decisions': s.n, 'local': s.local, 'escalated': s.escalated,
                               'replayed': s.replayed, 'auto_rejected': s.auto_rejected,
                               'reopens': s.reopens}
                      for s in seats},
            'notes_path': self.notes_path, 'auto_log': self.auto_log,
            'brief_dir': self.brief_dir,
        }
        if self.args.replay:
            summary['replay_source'] = os.path.abspath(self.args.replay)
            summary['replay_winner_recorded'] = self.replay_winner
            summary['replay_winner_match'] = (winner == self.replay_winner)
        host_rc = None
        try:
            host_rc = proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            proc.terminate()
            summary['host_terminated'] = True
        summary['host_exit'] = host_rc
        summary['terminal'] = self.terminal_checks()
        with open(os.path.join(self.game_dir, 'GAME_OVER.json'), 'w', encoding='utf-8') as fh:
            json.dump(summary, fh, ensure_ascii=False, indent=1)
        review_path = os.path.join(self.game_dir, 'game-review.md')
        write_atomic(review_path, self.render_game_review(summary))
        self.log('REVIEW %s' % review_path)
        self.audit(event='game_over', **{k: summary[k] for k in
                                         ('winner', 'host_exit')})
        self.log('GAME_OVER winner=%s decisions=%s' % (
            winner, {s.name: s.n for s in seats}))
        if self.args.replay:
            self.log('REPLAY-COMPARE recorded_winner=%s got=%s match=%s' % (
                self.replay_winner, winner, winner == self.replay_winner))
        self.log('DRIVER-RESULT %s' % json.dumps(summary, ensure_ascii=False))
        ok = (host_rc == 0 and summary['terminal']['found']
              and summary['terminal']['callback_errors'] == 0
              and summary['terminal']['workers_stopped'] == 'true'
              and summary['terminal']['room_destroyed'] == 'true')
        return 0 if ok else 2


def parse_args(argv):
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--mode', default='02p')
    ap.add_argument('--seed', type=int, default=20261003)
    ap.add_argument('--external-seats', type=int, default=1)
    ap.add_argument('--host', default=os.path.join(
        repo, 'builds', 'cmake-vs2026', 'Release', 'qsanguosha_external_agent_host.exe'))
    ap.add_argument('--asset', default=os.path.join(
        repo, 'builds', 'agent-play-20261114', 'agent-runtime'))
    ap.add_argument('--qt-bin', default=r'H:\Qt6111\6.11.1\msvc2022_64\bin')
    ap.add_argument('--run-dir', default=os.path.join(repo, 'builds', 'agent-driver', 'last'))
    ap.add_argument('--fallback', choices=('mock', 'file'), default='mock',
                    help='resolver for escalated questions: deterministic mock or'
                         ' answer-<n>.json files written by the LLM session')
    ap.add_argument('--escalate-kinds', default='all',
                    help='"all" (default) or comma list of decision kinds routed to'
                         ' the brief queue; unlisted kinds fall back to mock')
    ap.add_argument('--auto-log', default=None)
    ap.add_argument('--brief-dir', default=None)
    ap.add_argument('--notes', default=None)
    ap.add_argument('--max-seconds', type=int, default=1800)
    ap.add_argument('--answer-wait-seconds', type=int, default=600)
    ap.add_argument('--replay', default=None,
                    help='auto-answers.jsonl from a prior run: submit its recorded'
                         ' answers verbatim by decisionId; misses fall through to'
                         ' the normal flow')
    ap.add_argument('--replay-scope', choices=('all', 'llm'), default='all',
                    help='all: replay every recorded answer; llm: replay only'
                         ' escalated answers and let the current adjudicator'
                         ' handle the rest (regression on a live stream)')
    ap.add_argument('--shared-cursor', action='store_true',
                    help='multi-seat + single LLM session: share the event cursor'
                         ' across seats so later seats skip events an earlier'
                         ' seat already showed')
    args = ap.parse_args(argv)
    if args.escalate_kinds == 'all':
        args.escalate_kinds = None
    else:
        args.escalate_kinds = set(int(x) for x in args.escalate_kinds.split(',') if x.strip())
    return args


def main(argv=None):
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    args = parse_args(sys.argv[1:] if argv is None else argv)
    return Driver(args).run()


if __name__ == '__main__':
    sys.exit(main())
