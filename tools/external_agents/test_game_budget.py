"""Offline tests for the shared per-game worst-case JEV budget."""
import multiprocessing
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import providers as p


def _reserve_one(path, game_id, queue):
    ledger = p.BudgetLedger(path, max_requests=20000)
    try:
        ledger.execute('jev', lambda a: (_ for _ in ()).throw(p.DecisionError('offline_failure')),
                       game_id=game_id)
    except p.DecisionError as exc:
        queue.put(str(exc))


class GameBudgetTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'ledger.json'
        self.ledger = p.BudgetLedger(self.path, max_requests=20000)

    def test_failed_reservations_retain_charge_and_block_before_transport(self):
        count = p.GAME_CAP_NANODOLLARS // p.reservation('jev')
        calls = []
        for _ in range(count):
            with self.assertRaisesRegex(p.DecisionError, 'offline_failure'):
                self.ledger.execute('jev', lambda a: calls.append(a['id']) or
                                    (_ for _ in ()).throw(p.DecisionError('offline_failure')),
                                    game_id='one-game')
        with self.assertRaisesRegex(p.BudgetExhausted, 'game_budget_exhausted'):
            self.ledger.execute('jev', lambda a: calls.append('unexpected'), game_id='one-game')
        self.assertEqual(len(calls), count)
        self.assertLessEqual(self.ledger._game_total(self.ledger._read(), 'one-game'),
                             p.GAME_CAP_NANODOLLARS)
        self.assertNotIn('unexpected', calls)
        self.assertEqual(self.ledger.execute('jev', lambda a: 'ok', game_id='another-game'), 'ok')

    def test_live_path_requires_room_game_id_before_any_operation(self):
        called = []
        with patch.object(p, 'LEDGER_PATH', self.path):
            with self.assertRaisesRegex(p.DecisionError, 'game_id_required'):
                self.ledger.execute('jev', lambda a: called.append(a))
        self.assertFalse(called)
        self.assertFalse(self.path.exists())

    def test_settlement_and_reopen_keep_same_game_charge(self):
        def validated(a):
            a.update(input_tokens=1000, output_tokens=2,
                     local_peak_usage_estimate_nanodollars=42000,
                     settlement_schema=1, settled_peak_nanodollars=42000)
            return 'a'
        self.ledger.execute('jev', validated, game_id='stable-room-id')
        reopened = p.BudgetLedger(self.path, max_requests=20000)
        self.assertEqual(reopened._game_total(reopened._read(), 'stable-room-id'), 42000)
        reopened.execute('jev', validated, game_id='stable-room-id')
        self.assertEqual(reopened._game_total(reopened._read(), 'stable-room-id'), 84000)

    def test_parallel_processes_serialize_reservations(self):
        ctx = multiprocessing.get_context('fork')
        queue = ctx.Queue()
        processes = [ctx.Process(target=_reserve_one,
                                 args=(self.path, 'one-game', queue)) for _ in range(40)]
        for proc in processes: proc.start()
        for proc in processes:
            proc.join(10)
            self.assertEqual(proc.exitcode, 0)
        results = [queue.get(timeout=2) for _ in processes]
        count = p.GAME_CAP_NANODOLLARS // p.reservation('jev')
        self.assertEqual(results.count('offline_failure'), count)
        self.assertEqual(results.count('game_budget_exhausted'), 40 - count)
        self.assertEqual(len(self.ledger._read()['attempts']), count)


if __name__ == '__main__':
    unittest.main()
