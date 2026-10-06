"""Offline room-worker routing checks; no socket, credential, or HTTP access."""
import unittest
from unittest.mock import patch

import providers as p
import room_hybrid_worker as worker


REQUEST = {'decisionId': '4', 'stateRevision': '19'}


class Client:
    def __init__(self):
        self.calls = []
    def call(self, value):
        self.calls.append(value)
        return {'ok': True}


class Adapter:
    def __init__(self, error=None):
        self.error = error
        self.calls = 0
    def choose(self, provider, *data):
        self.calls += 1
        if self.error:
            raise self.error
        return 'action_0'


class RoomWorkerTest(unittest.TestCase):
    def test_open_circuit_uses_exact_local_ticket_without_route_or_http(self):
        client, adapter = Client(), Adapter()
        with patch.object(worker.h, 'route', side_effect=AssertionError('route called')):
            ack, route, disabled, code = worker.decide(client, REQUEST, adapter, True)
        self.assertTrue(ack['ok'])
        self.assertEqual((route, disabled, code), ('local_native', True, None))
        self.assertEqual(adapter.calls, 0)
        self.assertEqual(client.calls, [{'op': 'local', **REQUEST}])

    def test_budget_and_invalid_response_disable_paid_for_whole_worker(self):
        for error, expected in ((p.BudgetExhausted('game_budget_exhausted'), 'budget'),
                                (p.DecisionError('probabilities_invalid'), 'provider')):
            client, adapter = Client(), Adapter(error)
            options = {'action_0': {'decisionId': '4', 'stateRevision': '19',
                                    'kind': 'pass', 'action': {}}}
            with patch.object(worker.h, 'route', return_value=(
                    'jev', 'supported', options, ('state', 'options'))):
                ack, route, disabled, code = worker.decide(client, REQUEST, adapter, False)
            self.assertTrue(ack['ok'])
            self.assertEqual((route, disabled, code), ('local_native', True, expected))
            self.assertEqual(client.calls, [{'op': 'local', **REQUEST}])
            self.assertEqual(adapter.calls, 1)

    def test_validated_choice_submits_exact_native_candidate(self):
        client, adapter = Client(), Adapter()
        answer = {'decisionId': '4', 'stateRevision': '19', 'kind': 'pass', 'action': {}}
        with patch.object(worker.h, 'route', return_value=(
                'jev', 'supported', {'action_0': answer}, ('state', 'options'))):
            ack, route, disabled, code = worker.decide(client, REQUEST, adapter, False)
        self.assertTrue(ack['ok'])
        self.assertEqual((route, disabled, code), ('jev', False, None))
        self.assertEqual(client.calls, [{'op': 'submit', 'result': answer}])


if __name__ == '__main__':
    unittest.main()
