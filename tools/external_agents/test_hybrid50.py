"""Offline full-space, seat privacy and paid budget guard regressions."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import hybrid50 as h
import providers as p
from test_game_client import question


def q50(kind=0):
    q = question(kind)
    q['conversionsEnumerated'] = True
    q['worldView']['players'] = [dict(q['worldView']['players'][0], objectName=f'sgs{i+1}',
        generalName='standard_general', seat=i+1, alive=True, phase=0,
        equips=[], judgingArea=[]) for i in range(50)]
    q['worldView']['self'] = dict(q['worldView']['players'][0], role='rebel', roleVisible=True)
    q['worldView']['players'] = q['worldView']['players'][1:]
    q['worldView']['modeId'] = '50p'
    q['cardCandidates'][0]['targetCombinations'] = [[f'sgs{i}'] for i in range(2,51)]
    q['choiceOptions']['defaultChoice'] = ''
    return q


class HybridTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'ledger.json'
        self.adapter = p.ProviderAdapters(p.BudgetLedger(self.path, max_requests=20000),
                                           allowed_providers=('jev',))
    def tearDown(self): self.tmp.cleanup()

    def test_all_50_actions_preserved_and_jev_eligible(self):
        q = q50()
        answers, desc = h.actions(q)
        self.assertEqual(len(answers), 50)
        self.assertEqual({tuple(a['action'].get('selectedTargetNames', [])) for a in answers.values()},
                         {()} | {(f'sgs{i}',) for i in range(2,51)})
        self.assertEqual(h.route(q, self.adapter)[0], 'jev')
        self.assertFalse(self.path.exists())

    def test_50_independent_seats_have_identical_explicit_routes(self):
        counts = {'jev': 0, 'local_native': 0, 'local_forced': 0}
        for seat in range(50):
            q = q50()
            roster = [q['worldView']['self']] + q['worldView']['players']
            for v in roster: v.update(role='', roleVisible=False)
            me = roster[seat]; me.update(role='rebel', roleVisible=True)
            q['worldView']['self'] = me
            q['worldView']['players'] = [v for v in roster if v is not me]
            q['viewerObjectName'] = me['objectName']
            q['cardCandidates'][0]['targetCombinations'] = [[v['objectName']] for v in roster if v is not me]
            counts[h.route(q, self.adapter)[0]] += 1
            q['kind'] = 6; q['choiceOptions']['choices'] = ['a', 'b', 'c', 'd', 'e']
            counts[h.route(q, self.adapter)[0]] += 1
            q['choiceOptions']['choices'] = ['one']
            counts[h.route(q, self.adapter)[0]] += 1
        self.assertEqual(counts, {'jev': 50, 'local_native': 50, 'local_forced': 50})
        self.assertFalse(self.path.exists())

    def test_incomplete_or_skill_space_never_claims_sole_pass(self):
        for edit in ('conversion', 'targets', 'skill', 'unknown_card'):
            q = q50()
            if edit == 'conversion': q['conversionsEnumerated'] = False
            if edit == 'targets': q['cardCandidates'][0]['completeCoverage'] = False
            if edit == 'skill': q['cardConversions'] = [{'conversionId': 1}]
            if edit == 'unknown_card': q['worldView']['handCards'][0]['objectName'] = 'extension_unknown'
            self.assertEqual(h.route(q, self.adapter)[0], 'local_native')
        self.assertFalse(self.path.exists())

    def test_complete_unique_choice_local_no_inference(self):
        q = q50(6); q['choiceOptions']['choices'] = ['general_one']
        self.assertEqual(h.route(q, self.adapter)[0], 'local_forced')
        q['choiceOptions']['choices'].append('general_two')
        self.assertEqual(h.route(q, self.adapter)[0], 'local_native')
        self.assertFalse(self.path.exists())

    def test_more_than_255_options_and_payload_whole_native(self):
        q = q50(12)
        q['choiceOptions'].update(playerNames=[f'sgs{i}' for i in range(1,51)], minCount=2, maxCount=2)
        self.assertEqual(h.route(q, self.adapter)[:2], ('local_native', 'complete_options_over_255'))
        q = q50(); q['prompt'] = 'x' * p.MAX_INPUT_BYTES
        self.assertEqual(h.route(q, self.adapter)[:2], ('local_native', 'model_input_limit'))
        self.assertFalse(self.path.exists())

    def test_each_seat_only_own_hand_role_and_public_fields(self):
        base = q50()
        for seat in (1, 2, 17, 50):
            q = copy.deepcopy(base)
            all_players = [q['worldView']['self']] + q['worldView']['players']
            for player in all_players:
                player['role'] = ''; player['roleVisible'] = False
            me = all_players[seat-1]
            me['role'] = 'loyalist'; me['roleVisible'] = True
            q['viewerObjectName'] = me['objectName']; q['worldView']['self'] = me
            q['worldView']['players'] = [v for v in all_players if v is not me]
            q['worldView']['handCards'] = [{'cardId': 1000+seat, 'objectName': 'jink', 'suit': 0, 'number': 1}]
            q['worldView']['other_seat_secrets'] = {'hidden_hands': [7777]}
            q['worldView']['events'] = [{'privateEvent': True, 'cardName': 'secret-event'}]
            state = h.envelope(q); encoded = p.encode(state)
            self.assertEqual(state['observation']['own_hand'][0]['cardId'], 1000+seat)
            self.assertEqual(len(state['observation']['players']), 50)
            self.assertEqual(state['observation']['self']['role'], 'loyalist')
            self.assertNotIn(b'7777', encoded); self.assertNotIn(b'secret-event', encoded)
            self.assertNotIn(b'other_seat_secrets', encoded)
            p._state(state)
        q = q50(); q['worldView']['players'][1]['role'] = 'rebel'
        with self.assertRaisesRegex(p.DecisionError, 'hidden_role'): h.envelope(q)
        q = q50(); q['worldView']['players'][1]['privateFlags'] = ['private-flag']
        with self.assertRaisesRegex(p.DecisionError, 'private_opponent'): h.envelope(q)

    def test_jev_only_rejects_deepseek_before_reservation(self):
        with self.assertRaisesRegex(p.DecisionError, 'provider_not_authorized'):
            self.adapter.choose('deepseek', h.envelope(q50()), {'a': 'one', 'b': 'two'})
        self.assertFalse(self.path.exists())

    def test_hidden_card_choice_is_not_a_sole_visible_card(self):
        q = q50(9)
        q['choiceOptions'].update(cardIds=[123], candidatesComplete=False)
        self.assertEqual(h.route(q, self.adapter)[:2], ('local_native', 'hidden_card_choice_native'))
        q['choiceOptions']['candidatesComplete'] = True
        self.assertEqual(h.route(q, self.adapter)[0], 'local_forced')

    def test_request_limit_persists_and_unknown_budget_stops_before_io(self):
        ledger = self.adapter.ledger
        data = ledger._read()
        self.assertEqual(data['request_limit'], 20000)
        data['prior_upper_bound_nanodollars'] = p.CAP_NANODOLLARS - p.reservation('jev') + 1
        ledger._save(data)
        self.adapter.transport = lambda *_: self.fail('network must not happen')
        with self.assertRaises(p.BudgetExhausted):
            self.adapter.choose('jev', h.envelope(q50()), {'a': 'one', 'b': 'two'})
        self.assertEqual(p.BudgetLedger(self.path)._read()['request_limit'], 20000)
        self.assertEqual(ledger._total(ledger._read()), data['prior_upper_bound_nanodollars'])
        self.assertEqual(ledger._read()['attempts'], [])

    def test_full_50_option_paid_receipt_offline_transport(self):
        calls = []
        def transport(provider, body):
            calls.append(provider)
            options = json.loads(body)['questions']['decision']['criteria']
            self.assertEqual(len(options), 50)
            self.assertEqual(self.adapter.ledger._read()['attempts'][-1]['status'], 'reserved')
            choice = 'action_1'
            return {'model': 'jev-1.13.0', 'usage': {'input_tokens': 1000, 'output_tokens': 400},
                'answers': {'decision': {'type': 'choice', 'choice': choice, 'confidence': .9,
                    'probabilities': {k: 1.0 if k == choice else 0.0 for k in options}}}}
        self.adapter.transport = transport
        route, _, answers, data = h.route(q50(), self.adapter)
        self.assertEqual(self.adapter.choose('jev', *data), 'action_1')
        self.assertEqual(calls, ['jev'])
        self.assertEqual(self.adapter.ledger._total(self.adapter.ledger._read()), 42000)

    def test_budget_stop_cancels_and_closes_all_50_clients(self):
        data = self.adapter.ledger._read()
        data['prior_upper_bound_nanodollars'] = p.CAP_NANODOLLARS - 1
        self.adapter.ledger._save(data)
        calls, closed = [], []
        class Socket:
            def settimeout(self, value): pass
        class Client:
            hello = {'ok': True}
            def __init__(self, seat): self.name = seat['objectName']; self.socket = Socket()
            def call(self, command):
                calls.append((self.name, command))
                if command['op'] == 'poll' and self.name == 'sgs1':
                    return {'status': 'waiting-no-clock', 'request': q50()}
                return {'status': 'idle', 'ok': True}
            def close(self): closed.append(self.name)
        class Host:
            def poll(self): return None
        bootstrap = {'version': 2, 'hostPid': 'offline', 'seats': [
            {'objectName': f'sgs{i}', 'port': i, 'host': '127.0.0.1', 'version': 1}
            for i in range(1,51)]}
        with patch.object(h.g, 'Client', Client), patch.object(h.signal, 'signal'), \
                patch.object(p, 'https_post', side_effect=AssertionError('must not call')):
            result = h.play(bootstrap, self.adapter, Host(), Path(self.tmp.name), True, 2)
        self.assertEqual(result['terminal'], 'shared_budget_exhausted')
        self.assertFalse(result['complete_game'])
        self.assertEqual(len([c for _, c in calls if c['op'] == 'cancel']), 50)
        self.assertEqual(len(closed), 50)
        self.assertEqual(self.adapter.ledger._read()['attempts'], [])


if __name__ == '__main__': unittest.main()
