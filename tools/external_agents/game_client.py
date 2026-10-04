"""Separate-process paid client for the synthetic loopback native fixture.

A bounded policy offers printed-card actions plus legal pass, and bounded
selections. Unsupported prompts stop the game; there is no SmartAI fallback.
Bootstrap capabilities stay in private pipes and are never part of API state.
"""
import argparse
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import providers as p
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'autotest'))
from external_agent_mock import Client

RULES = ('Synthetic Qsanguosha two-player duel. Defeat the opponent; survive at positive HP. '
         'Slash deals 1 damage unless answered with Jink. Peach heals 1 HP; equipment grants '
         'its printed effect. Nullification cancels a trick. Only supplied actions are allowed. '
         'Pass declines an optional response or ends the play phase. Card names describe their '
         'standard effects. No hidden hands or draw order are available.')


def envelope(q):
    world = q['worldView']
    if world['self']['objectName'] != q['viewerObjectName']:
        raise p.DecisionError('viewer_mismatch')
    fields = ('objectName', 'hp', 'maxHp', 'handcardCount', 'phase', 'alive',
              'faceUp', 'chained', 'generalName', 'kingdom', 'publicMarks')
    players = []
    for player in world['players']:
        if not player['roleVisible'] and player['role']:
            raise p.DecisionError('hidden_role_exposed')
        if player['privateFlags'] or any(s['state'] for s in player['skills']):
            raise p.DecisionError('private_opponent_state_exposed')
        players.append({k: player[k] for k in fields if k in player})
    cards = [{k: card[k] for k in ('cardId', 'objectName', 'suit', 'number')}
             for card in world['handCards']]
    return {'synthetic': True, 'seat_visible': True, 'rules': RULES,
            'observation': {'self': {k: world['self'][k] for k in fields if k in world['self']},
                'players': players, 'own_hand': cards, 'decision_kind': q['kind'],
                'pattern': q['pattern'], 'prompt': q['prompt']}}


def actions(q):
    result, descriptions = {}, {}
    def add(kind, action, description):
        if len(result) >= 16:
            return
        key = 'action_' + str(len(result))
        result[key] = {'decisionId': q['decisionId'], 'stateRevision': q['stateRevision'],
                       'kind': kind, 'action': action}
        descriptions[key] = description[:512]
    options = q['choiceOptions']
    kind = q['kind']
    if kind in (0, 1) or options['optional']:
        add('pass', {}, 'Pass this optional decision / end the play phase.')
    if kind in (0, 1):
        cards = {c['cardId']: c['objectName'] for c in q['worldView']['handCards']}
        for c in q['cardCandidates']:
            if (c['cardId'] not in cards or not c['available'] or c['limited']
                    or not c['completeCoverage']):
                continue
            targets = c['targetCombinations']
            if c['feasibleWithNoTarget']:
                targets = [[]] + targets
            seen_targets = set()
            for names in targets:
                if tuple(names) in seen_targets:
                    continue
                seen_targets.add(tuple(names))
                add('useCard', {'candidateId': c['candidateId'], 'useCardId': c['cardId'],
                    'selectedTargetNames': names}, 'Use ' + cards.get(c['cardId'], str(c['cardId']))
                    + ' on ' + (', '.join(names) or 'its automatic targets'))
    elif kind == 14:
        field = 'bottomCardIds' if options['defaultChoice'] == '2' else 'selectedCardIds'
        add('answer', {field: options['cardIds']}, 'Keep supplied card order in the permitted pile.')
    elif kind == 10:
        for card in options['cardIds']:
            for player in options['playerNames']:
                add('answer', {'selectedCardIds': [card], 'selectedTargetNames': [player]},
                    f'Give card {card} to {player}.')
    elif kind in (11, 12):
        for count in range(options['minCount'], options['maxCount'] + 1):
            for group in itertools.islice(itertools.combinations(options['playerNames'], count), 16):
                add('answer', {'selectedTargetNames': list(group)}, 'Select players: ' + ', '.join(group))
    elif options['cardIds']:
        minimum = 1 if kind == 13 else options['minCount']
        maximum = 1 if kind == 13 else options['maxCount']
        for count in range(minimum, maximum + 1):
            for group in itertools.islice(itertools.combinations(options['cardIds'], count), 16):
                add('answer', {'selectedCardIds': list(group)}, 'Select card IDs: ' + str(group))
    elif options['choices']:
        for choice in options['choices']:
            add('answer', {'userString': choice}, 'Choose: ' + choice)
    if not result:
        raise p.DecisionError('unsupported_native_prompt')
    return result, descriptions


def play(bootstrap, adapter, mode, limit):
    client = Client(bootstrap)
    rows = []
    deadline = time.monotonic() + 900
    terminal = 'segment_limit'
    winner = ''
    try:
        if not client.hello.get('ok'):
            raise p.DecisionError('transport_unauthorized')
        while len(rows) < limit and time.monotonic() < deadline:
            state = client.call({'op': 'poll'})
            if state['status'] == 'finished':
                winner = state['winner']
                terminal = 'completed_game' if winner else 'cancelled_game'
                break
            if state['status'] == 'smart-ai-fallback':
                raise p.DecisionError('unexpected_smartai_fallback')
            q = state.get('request')
            if q is None:
                time.sleep(.01)
                continue
            observation = envelope(q)
            answers, descriptions = actions(q)
            if len(answers) == 1:
                choice = next(iter(answers))
                provider = 'forced_legal_action'
            elif mode == 'combined':
                routed = adapter.route(observation, descriptions)
                choice, provider = routed['choice'], routed['provider']
            else:
                provider = mode
                choice = adapter.choose(provider, observation, descriptions)
            if not client.call({'op': 'submit', 'result': answers[choice]}).get('ok'):
                raise p.DecisionError('native_validation_rejected')
            rows.append({'kind': q['kind'], 'provider': provider, 'options': len(answers),
                         'choice': choice, 'submitted_kind': answers[choice]['kind']})
    except p.DecisionError as error:
        terminal = str(error)
    finally:
        if terminal != 'completed_game':
            client.call({'op': 'cancel'})
        client.close()
    return {'terminal': terminal, 'complete_game': terminal == 'completed_game',
            'winner': winner, 'decisions': rows, 'clientPid': __import__('os').getpid(),
            'hostPid': bootstrap['hostPid']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--provider', choices=('deepseek', 'jev', 'combined'), default='combined')
    parser.add_argument('--max-decisions', type=int, default=100)
    args = parser.parse_args()
    host = None
    with tempfile.TemporaryFile(mode='w+b') as errors:
        try:
            host = subprocess.Popen([args.host], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                    stderr=errors, text=True)
            bootstrap = None
            for line in host.stdout:
                if line.startswith('QSAN_AGENT_BOOTSTRAP '):
                    bootstrap = json.loads(line.split(' ', 1)[1])
                    break
            if bootstrap is None:
                raise p.DecisionError('host_bootstrap_missing')
            result = play(bootstrap, p.ProviderAdapters(), args.provider, args.max_decisions)
            host.communicate(timeout=10)
            errors.seek(0)
            log = errors.read().decode(errors='replace')
            result['host_returncode'] = host.returncode
            result['callback_errors'] = log.count('[AI_CALLBACK_ERROR]')
            result['explicit_fallback'] = '[EXTERNAL_AGENT] explicit SmartAI fallback' in log
            result['cleanup_verified'] = 'workers_stopped=true room_destroyed=true' in log
            result['terminal_log'] = next((line for line in log.splitlines()
                if 'EXTERNAL_HOST_TERMINAL' in line), '')
            print(json.dumps(result, indent=2))
            return 0 if result['cleanup_verified'] and host.returncode == 0 else 2
        except p.DecisionError as e:
            print(json.dumps({'terminal': str(e), 'complete_game': False}))
            return 2
        finally:
            if host is not None and host.poll() is None:
                host.terminate()
                try:
                    host.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait()

if __name__ == '__main__':
    raise SystemExit(main())
