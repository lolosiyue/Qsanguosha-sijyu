"""Offline boundary checks for the native-to-provider bridge."""
import unittest
from unittest.mock import patch
import game_client as g
import providers as p


def question(kind=0):
    player = {'objectName': 'sgs1', 'hp': 3, 'maxHp': 4, 'roleVisible': False,
              'role': '', 'privateFlags': [], 'skills': []}
    return {'decisionId': '12', 'stateRevision': '42', 'viewerObjectName': 'sgs1',
            'kind': kind, 'pattern': '', 'prompt': '',
            'worldView': {'self': player, 'players': [player],
                          'handCards': [{'cardId': 3, 'objectName': 'slash', 'suit': 0, 'number': 7}],
                          'hidden_hands': 'must not forward'},
            'choiceOptions': {'optional': False, 'choices': [], 'cardIds': [],
                              'playerNames': [], 'minCount': 1, 'maxCount': 1},
            'cardCandidates': [{'candidateId': 1, 'cardId': 3, 'available': True,
                                'limited': False, 'completeCoverage': True,
                                'feasibleWithNoTarget': False, 'targetCombinations': [['sgs2']]}]}


class BridgeTests(unittest.TestCase):
    def test_only_authorized_native_card_ticket_and_targets(self):
        q = question()
        answers, descriptions = g.actions(q)
        self.assertEqual(len(answers), 2)
        card = answers['action_1']
        self.assertEqual(card['kind'], 'useCard')
        self.assertEqual(card['action'], {'candidateId': 1, 'useCardId': 3,
                                         'selectedTargetNames': ['sgs2']})
        self.assertEqual(card['decisionId'], '12')
        self.assertEqual(card['stateRevision'], '42')
        self.assertIn('slash', descriptions['action_1'])
        q['cardCandidates'][0]['cardId'] = 310
        self.assertEqual(len(g.actions(q)[0]), 1)
        q['cardCandidates'][0]['cardId'] = 3
        q['cardCandidates'][0]['limited'] = True
        self.assertEqual(len(g.actions(q)[0]), 1)

    def test_projection_is_explicit_and_private_state_rejected(self):
        q = question()
        envelope = g.envelope(q)
        p._state(envelope)
        self.assertNotIn('hidden_hands', p.encode(envelope).decode())
        q['worldView']['players'][0]['role'] = 'rebel'
        with self.assertRaisesRegex(p.DecisionError, 'hidden_role'):
            g.envelope(q)

    def test_unsupported_prompt_does_not_choose_fallback(self):
        with self.assertRaisesRegex(p.DecisionError, 'unsupported_native_prompt'):
            g.actions(question(9))

    def test_action_offer_is_bounded(self):
        q = question(7)
        q['choiceOptions'].update(cardIds=list(range(100)), minCount=2, maxCount=3)
        self.assertEqual(len(g.actions(q)[0]), 16)

    def test_provider_failure_cancels_without_submission(self):
        q = question()
        calls = []
        class Client:
            hello = {'ok': True}
            def __init__(self, bootstrap): pass
            def call(self, value):
                calls.append(value)
                return {'status': 'waiting-no-clock', 'request': q}
            def close(self): pass
        class Adapter:
            def route(self, state, options):
                raise p.DecisionError('http_529')
        with patch.object(g, 'Client', Client):
            result = g.play({'hostPid': 'other'}, Adapter(), 'combined', 10)
        self.assertEqual(result['terminal'], 'http_529')
        self.assertFalse(result['complete_game'])
        self.assertEqual([c['op'] for c in calls], ['poll', 'cancel'])
        self.assertEqual(result['decisions'], [])

if __name__ == '__main__':
    unittest.main()
