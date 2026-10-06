"""ONE 50-seat game with explicit local/JEV decisions and durable paid budget.

No secret/capability/request-body logging. All model state is rebuilt from a
whitelist of native seat-visible fields. Incomplete or oversized action spaces
go whole to native SmartAI; legal choices are never truncated for inference.
"""
import argparse
import collections
import itertools
import json
import math
import os
from pathlib import Path
import queue
import signal
import subprocess
import sys
import threading
import time

import game_client as g
import providers as p

LIMIT = 255
POLICY = {
    'version': 2, 'all_seats': 50, 'sole_complete_choice': 'local_forced',
    'native_projection': 'explicit LocalDecisions; JEV and CardChosen retain full view',
    'jev': ['complete standard printed-card Activate/UseCard',
            'complete PlayerChosen/PlayersChosen with at least 8 options'],
    'local': ['simple decisions', 'skill conversions', 'unknown card effects',
              'incomplete native candidates', 'over 255 complete options',
              'over 16384 encoded input bytes'],
    'failure': 'cancel, preserve evidence, no retry',
    'local_native_before_jev': False,
    'model': 'jev-1.13.0', 'max_requests': 20_000,
    'internal_cap_usd': 1.5, 'user_cap_usd': 2,
}
EFFECTS = {
    'slash': 'Deal 1 damage; target may play Jink.',
    'fire_slash': 'Slash with fire damage; chains may spread elemental damage.',
    'thunder_slash': 'Slash with thunder damage; chains may spread elemental damage.',
    'jink': 'Avoid one Slash.', 'peach': 'Heal one HP or rescue a dying player.',
    'analeptic': 'Boost next Slash damage, or save self when dying.',
    'duel': 'Exchange Slash responses; first unable takes one damage.',
    'snatch': 'Take one target card.', 'dismantlement': 'Discard one target card.',
    'fire_attack': 'Target shows a hand card; matching suit discard deals fire damage.',
    'iron_chain': 'Toggle targets chained status, or recast for one card.',
    'ex_nihilo': 'Draw two cards.', 'amazing_grace': 'All alive players select a card.',
    'god_salvation': 'Heal all wounded players.',
    'savage_assault': 'All other players must respond Slash or take damage.',
    'archery_attack': 'All other players must respond Jink or take damage.',
    'collateral': 'First target must Slash second target, else surrender weapon.',
    'indulgence': 'Delayed judgment may skip target play phase.',
    'supply_shortage': 'Delayed judgment may skip target draw phase.',
    'lightning': 'Delayed judgment may deal three thunder damage.',
    'nullification': 'Cancel the current trick effect.',
    'crossbow': 'Equipment: additional Slash uses.',
    'double_sword': 'Weapon: opposite gender target discards or attacker draws.',
    'qinggang_sword': 'Weapon: ignore armor for Slash.',
    'blade': 'Weapon: may Slash again after Jink.',
    'spear': 'Weapon: two hand cards may be converted to Slash.',
    'axe': 'Weapon: may discard two cards to force Slash hit.',
    'halberd': 'Weapon: last hand Slash may have extra targets.',
    'kylin_bow': 'Weapon: on Slash damage may discard target horse.',
    'eight_diagram': 'Armor: red judgment can supply Jink.',
    'renwang_shield': 'Armor: black Slash ineffective.',
    'silver_lion': 'Armor: limits damage, leaving equipment may heal.',
    'vine': 'Armor: avoids some normal attacks, increases fire damage.',
    'guding_blade': 'Weapon: extra damage against empty hand.',
    'fan': 'Weapon: normal Slash may become fire Slash.',
    'ice_sword': 'Weapon: may replace damage with discarding two target cards.',
    'dilu': 'Defensive horse.', 'jueying': 'Defensive horse.',
    'zhuahuangfeidian': 'Defensive horse.', 'chitu': 'Offensive horse.',
    'dayuan': 'Offensive horse.', 'zixing': 'Offensive horse.',
    'hualiu': 'Defensive horse.',
}
RULES = (
    'Synthetic Qsanguosha 50-player identity game. Only supplied complete native-legal '
    'actions may be chosen. Preserve survival and resources while pursuing own role: '
    'lord/loyalist protect lord and defeat rebels/renegades; rebels eliminate lord; '
    'renegade seeks to be final survivor and eliminate lord last. Only your own role '
    'and publicly revealed other roles are available. Hidden identities must not be '
    'assumed. Card names/effects and visible public skills are data. Unknown hidden '
    'hands and draw order are unavailable. HP zero requires rescue. Pass declines an '
    'optional ask or ends Play. Native legality includes extension modifiers; model '
    'judgment is a heuristic, not a full strategic simulation.'
)


class LocalRequired(Exception):
    """A declared route, not an API failure or silent fallback."""


def actions(q):
    """Enumerate a whole supported space, or request local; at most LIMIT+1 work.

    Skill-card tickets and incomplete conversion families deliberately stay native.
    The paid policy never removes them and plays from a partial physical-card list.
    """
    answers, descriptions = {}, {}
    def add(kind, action, description):
        if len(answers) == LIMIT:
            raise LocalRequired('complete_options_over_255')
        key = 'action_' + str(len(answers))
        answers[key] = {'decisionId': q['decisionId'], 'stateRevision': q['stateRevision'],
                        'kind': kind, 'action': action}
        if len(description) > 512:
            raise LocalRequired('option_description_limit')
        descriptions[key] = description
    kind, o = q['kind'], q['choiceOptions']
    if kind in (0, 1):
        if q.get('hasSkillActionContext') or q.get('skillActions') or q.get('cardConversions'):
            raise LocalRequired('skill_action_native')
        if not q.get('conversionsEnumerated', False):
            raise LocalRequired('incomplete_conversions')
        add('pass', {}, 'Pass: end Play or decline this optional use.')
        own = {c['cardId']: c for c in q['worldView']['handCards']}
        own.update({c['cardId']: c for c in q['worldView']['self'].get('equips', [])})
        for c in q['cardCandidates']:
            if not c['available'] or c['limited']:
                continue
            if not c['completeCoverage']:
                raise LocalRequired('incomplete_target_projection')
            card = own.get(c['cardId'])
            if card is None:
                raise p.DecisionError('candidate_not_owned')
            name = card['objectName']
            if name not in EFFECTS:
                raise LocalRequired('unknown_card_effect')
            targets = c['targetCombinations']
            if c['feasibleWithNoTarget']:
                targets = [[]] + targets
            seen = set()
            for group in targets:
                group = tuple(group)
                if group in seen:
                    continue
                seen.add(group)
                add('useCard', {'candidateId': c['candidateId'], 'useCardId': c['cardId'],
                    'selectedTargetNames': list(group)},
                    f'Use {name} (card {c["cardId"]}, suit {card["suit"]}, '
                    f'number {card["number"]}) on {list(group) or "automatic targets"}. '
                    + EFFECTS[name])
    elif kind == 14:
        raise LocalRequired('guanxing_order_native')
    elif kind in (2, 3, 4, 5, 6, 15):
        if o['optional']:
            add('pass', {}, 'Decline the optional choice.')
        for choice in o['choices']:
            add('answer', {'userString': choice}, 'Choose: ' + choice)
    elif kind in (11, 12, 7, 8, 9):
        if kind == 9 and not o.get('candidatesComplete', False):
            raise LocalRequired('hidden_card_choice_native')
        if o['optional']:
            add('pass', {}, 'Decline optional selection.')
        pool = o['playerNames'] if kind in (11, 12) else o['cardIds']
        field = 'selectedTargetNames' if kind in (11, 12) else 'selectedCardIds'
        minimum, maximum = o['minCount'], o['maxCount']
        if not 0 <= minimum <= maximum <= len(pool):
            raise LocalRequired('selection_bounds_unknown')
        for count in range(minimum, maximum + 1):
            for group in itertools.combinations(pool, count):
                add('answer', {field: list(group)}, 'Select: ' + str(list(group)))
    else:
        raise LocalRequired('category_native')
    if not answers:
        raise LocalRequired('no_complete_supported_actions')
    return answers, descriptions


def envelope(q):
    """Rebuild only whitelisted synthetic own/public facts, never a raw packet."""
    w, viewer = q['worldView'], q['viewerObjectName']
    if w['self']['objectName'] != viewer:
        raise p.DecisionError('viewer_mismatch')
    all_players = [w['self']] + w['players']
    if (w.get('modeId') != '50p' or len(all_players) != 50
            or len({v['objectName'] for v in all_players}) != 50):
        raise p.DecisionError('world_not_50p')
    fields = ('objectName', 'seat', 'hp', 'maxHp', 'handcardCount', 'phase', 'alive',
              'faceUp', 'chained', 'generalName', 'kingdom')
    def card(c):
        return {k: c[k] for k in ('cardId', 'objectName', 'suit', 'number') if k in c}
    def player(v):
        if not v['roleVisible'] and v['role']:
            raise p.DecisionError('hidden_role_exposed')
        if v['objectName'] != viewer and (v.get('privateFlags')
                or any(s.get('state') or s.get('correctState') for s in v.get('skills', []))):
            raise p.DecisionError('private_opponent_state_exposed')
        result = {k: v[k] for k in fields if k in v}
        if v['roleVisible']:
            result['role'] = v['role']
        result['equips'] = [c['objectName'] for c in v.get('equips', [])]
        result['judging'] = [c['objectName'] for c in v.get('judgingArea', [])]
        result['skills'] = [s['skillName'] for s in v.get('skills', []) if not s.get('invalid')]
        return result
    # Native public events are already seat projected. Copy only public card ids,
    # no privateCardIds/privateViewer/private metadata or arbitrary details tree.
    events = [{k: e[k] for k in ('sequence', 'kind', 'from', 'to', 'targets',
        'cardName', 'amount', 'nature') if k in e}
        for e in w.get('events', []) if not e.get('privateEvent')]
    public_players = [player(v) for v in all_players]
    columns = fields + ('role', 'equips', 'judging', 'skills')
    obs = {'self': player(w['self']), 'player_columns': list(columns),
           'players': [[v.get(k) for k in columns] for v in public_players],
           'own_hand': [card(c) for c in w['handCards']], 'decision_kind': q['kind'],
           'pattern': q['pattern'], 'prompt': q['prompt'],
           'selection_reason': q['choiceOptions'].get('reason', ''),
           'recent_public_events': events[-12:], 'public_event_window': 12,
           'native_projected_public_event_count': len(events),
           'currentPlayer': w.get('currentPlayer'), 'currentPhase': w.get('currentPhase')}
    return {'synthetic': True, 'seat_visible': True, 'rules': RULES, 'observation': obs}


def route(q, adapter):
    try:
        answers, descriptions = actions(q)
    except LocalRequired as e:
        return 'local_native', str(e), None, None
    if len(answers) == 1:
        return 'local_forced', 'sole_complete_legal_action', answers, None
    eligible = q['kind'] in (0, 1) or (q['kind'] in (11, 12) and len(answers) >= 8)
    if not eligible:
        return 'local_native', 'simple_category', None, None
    state = envelope(q)
    try:
        adapter.prepare_payload('jev', state, descriptions)
    except p.DecisionError as e:
        if str(e) == 'input_too_large':
            return 'local_native', 'model_input_limit', None, None
        raise
    return 'jev', 'complete_expensive_category', answers, (state, descriptions)


def atomic_json(path, data):
    tmp = path.with_suffix('.tmp')
    with tmp.open('wb') as f:
        f.write(p.encode(data) + b'\n'); f.flush(); os.fsync(f.fileno())
    os.replace(tmp, path)


def play(bootstrap, adapter, host, out, live, max_seconds):
    if bootstrap['version'] != 2 or len(bootstrap['seats']) != 50:
        raise p.DecisionError('bootstrap_not_50_seats')
    seats = bootstrap['seats']
    names = [s['objectName'] for s in seats]
    if len(set(names)) != 50 or len({s['port'] for s in seats}) != 50:
        raise p.DecisionError('bootstrap_seats_not_unique')
    clients, pending = {}, {}
    counters = collections.Counter(); kinds = collections.Counter(); reasons = collections.Counter()
    coverage = collections.defaultdict(collections.Counter)
    start = time.monotonic(); last_save = 0; last_action = None
    terminal, winner = 'running', ''
    count, api_wait = 0, 0.0
    cancellation = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: cancellation.set())
    def transport(provider, body):
        nonlocal api_wait
        if provider != 'jev':
            raise p.DecisionError('provider_not_authorized')
        begin = time.monotonic()
        try:
            return p.https_post(provider, body)
        finally:
            api_wait += time.monotonic() - begin
    if live:
        adapter.transport = transport
    def status():
        data = adapter.ledger._read()
        return {'terminal': terminal, 'complete_game': terminal == 'completed_game',
            'winner': winner, 'elapsed_seconds': time.monotonic() - start,
            'api_wait_seconds': api_wait, 'decisions': count, 'routes': dict(counters),
            'kinds': dict(kinds), 'reasons': dict(reasons),
            'seat_coverage': {k: dict(v) for k, v in coverage.items()},
            'cost_conservative_usd': adapter.ledger._total(data) / 1e9,
            'paid_attempts': len(data['attempts']), 'unknown_reservations': sum(
                'settled_peak_nanodollars' not in a for a in data['attempts']),
            'provider_reported_billed_usd': None, 'last_action': last_action,
            'host_pid': bootstrap['hostPid'], 'client_pid': os.getpid(), 'live': live,
            'policy': POLICY, 'pending_decisions': list(pending)}
    try:
        for seat in seats:
            client = g.Client(seat)
            client.socket.settimeout(60)
            if not client.hello.get('ok'):
                raise p.DecisionError('seat_unauthorized')
            clients[seat['objectName']] = client
        atomic_json(out / 'progress.json', status())
        with (out / 'decisions.jsonl').open('a') as log:
            while time.monotonic() - start < max_seconds:
                if cancellation.is_set():
                    terminal = 'user_or_process_cancel'; break
                if host.poll() is not None:
                    terminal = 'host_exited_before_observed_game_over'; break
                for name, client in clients.items():
                    s = client.call({'op': 'poll'})
                    if s['status'] == 'finished':
                        winner = s['winner']; terminal = 'completed_game' if winner else 'cancelled_game'
                        break
                    if s['status'] in ('smart-ai-fallback', 'paused-disconnected', 'cancelled'):
                        raise p.DecisionError('unexpected_endpoint_' + s['status'])
                    q = s.get('request')
                    old = pending.get(name)
                    if old and ((q and q['decisionId'] != old['decisionId']) or s['status'] == 'idle'):
                        pending.pop(name)
                    if q is None:
                        continue
                    if old and q['decisionId'] == old['decisionId']:
                        raise p.DecisionError('native_validation_rejected')
                    if q['viewerObjectName'] != name:
                        raise p.DecisionError('seat_capability_violation')
                    if s.get('lastError'):
                        raise p.DecisionError('native_' + s['lastError'])
                    begin = time.monotonic()
                    chosen_route, reason, answers, data = route(q, adapter)
                    row = {'seat': name, 'decisionId': q['decisionId'],
                        'revision': q['stateRevision'], 'kind': q['kind'],
                        'pattern': q['pattern'], 'prompt': q['prompt'],
                        'route': chosen_route, 'reason': reason,
                        'options': len(answers) if answers else None,
                        'game_elapsed_seconds': time.monotonic() - start}
                    # Persist the latest pending action before any possible paid call.
                    last_action = row; atomic_json(out / 'progress.json', status())
                    if chosen_route == 'local_native':
                        ack = client.call({'op': 'local', 'decisionId': q['decisionId'],
                                           'stateRevision': q['stateRevision']})
                    else:
                        if chosen_route == 'local_forced':
                            choice = next(iter(answers))
                        elif live:
                            before = api_wait
                            choice = adapter.choose('jev', *data)
                            row['api_wait_ms'] = (api_wait - before) * 1000
                        else:
                            choice = next((k for k in answers if answers[k]['kind'] != 'pass'), next(iter(answers)))
                        row['selected_action'] = answers[choice]
                        ack = client.call({'op': 'submit', 'result': answers[choice]})
                    if not ack.get('ok'):
                        raise p.DecisionError('native_queue_rejected')
                    row['client_route_ms'] = (time.monotonic() - begin) * 1000
                    row['native_queued'] = True
                    pending[name] = row
                    count += 1; counters[chosen_route] += 1
                    kinds[str(q['kind']) + ':' + chosen_route] += 1
                    reasons[reason] += 1; coverage[name][chosen_route] += 1
                    last_action = row
                    log.write(json.dumps(row, ensure_ascii=False) + '\n'); log.flush()
                if terminal != 'running':
                    break
                now = time.monotonic()
                if now - last_save > 5:
                    atomic_json(out / 'progress.json', status()); last_save = now
                time.sleep(.005)
            if terminal == 'running':
                terminal = 'explicit_wall_budget_segment'
    except p.BudgetExhausted:
        terminal = 'shared_budget_exhausted'
    except p.DecisionError as e:
        terminal = str(e)
    except (OSError, ValueError, KeyError, AssertionError):
        terminal = 'local_transport_or_shape_failure'
    finally:
        # All endpoints are cancelled; there is no implicit fallthrough or paid retry.
        if terminal != 'completed_game':
            for client in clients.values():
                try: client.call({'op': 'cancel'})
                except (OSError, ValueError, AssertionError): pass
        for client in clients.values():
            client.close()
    result = status(); atomic_json(out / 'progress.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--seed', required=True, type=int)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--gdb', action='store_true')
    parser.add_argument('--max-seconds', type=int, default=10_800)
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    guard = out / ('live-launch.json' if args.live else 'offline-launch.json')
    # Exclusive persistent launch marker prevents duplicated games after handoff.
    with guard.open('x') as f:
        json.dump({'pid': os.getpid(), 'seed': args.seed, 'live': args.live,
                   'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}, f)
        f.flush(); os.fsync(f.fileno())
    ledger = p.BudgetLedger(max_requests=POLICY['max_requests'])
    ledger._read()  # fail closed before starting a game
    atomic_json(out / 'routing-policy.json', POLICY)
    cmd = [args.host, '--hybrid-50p', '--seed', str(args.seed),
           '--settings-file', str(out / 'native-settings.json')]
    if args.gdb:
        cmd = ['gdb', '-batch', '-ex', 'set pagination off', '-ex', 'set print frame-arguments none',
               '-ex', 'set debuginfod enabled off', '-ex', 'run', '-ex', 'thread apply all bt', '--args'] + cmd
    wall_start = time.monotonic()
    host = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, start_new_session=True)
    messages = queue.Queue()
    def reader():
        with (out / 'native.log').open('a') as log:
            for line in host.stdout:
                if line.startswith('QSAN_HYBRID_BOOTSTRAP '):
                    messages.put(json.loads(line.split(' ', 1)[1]))
                else:
                    log.write(line); log.flush()
            messages.put(None)
    t = threading.Thread(target=reader, daemon=True); t.start()
    result = {'terminal': 'bootstrap_missing', 'complete_game': False}
    try:
        bootstrap = messages.get(timeout=600)
        if bootstrap:
            adapter = p.ProviderAdapters(ledger, allowed_providers=('jev',))
            result = play(bootstrap, adapter, host, out, args.live, args.max_seconds)
        try:
            host.wait(timeout=60)
        except subprocess.TimeoutExpired:
            result['cleanup_timeout'] = True
            os.killpg(host.pid, signal.SIGTERM)
            try: host.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(host.pid, signal.SIGKILL); host.wait()
        t.join(timeout=5)
    except (queue.Empty, p.DecisionError, OSError):
        result['terminal'] = 'bootstrap_or_preflight_failure'
    finally:
        if host.poll() is None:
            os.killpg(host.pid, signal.SIGTERM)
            try: host.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(host.pid, signal.SIGKILL); host.wait()
        result['host_returncode'] = host.returncode
        result['total_seconds_including_startup_cleanup'] = time.monotonic() - wall_start
        log = (out / 'native.log').read_text(errors='replace')
        result['cleanup_verified'] = '"workers_stopped":true' in log and '"room_destroyed":true' in log
        result['callback_error_log_count'] = log.count('[AI_CALLBACK_ERROR]')
        result['native_terminal_line'] = next((s for s in reversed(log.splitlines()) if 'HYBRID_TERMINAL' in s), '')
        if result['native_terminal_line']:
            native = json.loads(result['native_terminal_line'].split('HYBRID_TERMINAL ', 1)[1])
            result['winner'] = native.get('winner', '')
            result['native_terminal'] = native
            if native.get('natural_finished') and native.get('winner') and result['cleanup_verified']:
                result['terminal'] = 'completed_game'
                result['complete_game'] = True
        if host.returncode != 0 or not result['cleanup_verified']:
            result['complete_game'] = False
            if result['terminal'] == 'completed_game': result['terminal'] = 'unclean_native_terminal'
        atomic_json(out / 'result.json', result)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['complete_game'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
