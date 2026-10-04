"""Offline financial/safety contract tests; never contact a provider."""
import json
import multiprocessing
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import providers as p


STATE = {'synthetic': True, 'seat_visible': True,
         'rules': 'Slash causes 1 damage. Jink avoids it. Choose one legal response.',
         'observation': {'seat': 'seat_1', 'hp': 1, 'own_hand': ['Jink'],
                         'pending_card': 'Slash'}}
OPTIONS = {'jink': 'Play own Jink to avoid damage.', 'pass_turn': 'Take the damage.'}


def response(provider, choice='jink'):
    if provider == 'deepseek':
        return {'model': 'deepseek-flash', 'usage': {'prompt_tokens': 100, 'completion_tokens': 9},
                'choices': [{'finish_reason': 'stop',
                             'message': {'content': json.dumps({'choice': choice})}}]}
    return {'model': 'jev-1.13.0', 'usage': {'input_tokens': 100, 'output_tokens': 20},
            'answers': {'decision': {'type': 'choice', 'choice': choice, 'confidence': 0.9,
                        'probabilities': {'jink': 0.95, 'pass_turn': 0.05}}}}


def worker(path, crash=False, provider='deepseek'):
    def operation(attempt):
        if crash:
            import os
            os._exit(7)
        return 'jink'
    p.BudgetLedger(path).execute(provider, operation)


class ProviderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'ledger.json'
        self.calls = []
        self.ledger = p.BudgetLedger(self.path)
        def transport(provider, body):
            self.calls.append((provider, json.loads(body)))
            self.assertTrue(self.path.exists())
            self.assertEqual(self.read()['attempts'][-1]['status'], 'reserved')
            return response(provider)
        self.adapters = p.ProviderAdapters(self.ledger, transport)

    def tearDown(self):
        self.tmp.cleanup()

    def read(self):
        return json.loads(self.path.read_text())

    def test_each_provider_and_combined_routing(self):
        self.assertEqual(self.adapters.route(STATE, OPTIONS), {'provider': 'jev', 'choice': 'jink'})
        larger = dict(OPTIONS, a='Other legal action', b='Other legal action', c='Other legal action')
        self.assertEqual(self.adapters.route(STATE, larger), {'provider': 'deepseek', 'choice': 'jink'})
        rows = self.read()['attempts']
        self.assertEqual([r['provider'] for r in rows], ['jev', 'deepseek'])
        self.assertEqual(sum(r['input_tokens'] for r in rows), 200)
        self.assertTrue(all(r['provider_reported_billed_usd'] is None for r in rows))
        self.assertEqual(rows[1]['local_peak_usage_estimate_nanodollars'], 40800)

    def test_request_schemas_and_only_authorized_models(self):
        for provider in ('deepseek', 'jev'):
            self.adapters.choose(provider, STATE, OPTIONS)
        flash, jev = [c[1] for c in self.calls]
        self.assertEqual(flash['model'], 'deepseek-flash')
        self.assertEqual(flash['max_tokens'], 256)
        self.assertEqual(flash['thinking'], {'type': 'disabled'})
        self.assertNotIn('messages', jev)
        self.assertEqual(jev['model'], 'jev-1.13.0')
        self.assertEqual(jev['questions']['decision']['type'], 'choice')
        self.assertEqual(set(jev['questions']['decision']['criteria']), set(OPTIONS))
        with self.assertRaises(p.DecisionError):
            self.adapters.choose('other', STATE, OPTIONS)
        self.assertEqual(len(self.calls), 2)

    def test_failed_calls_and_retries_keep_full_reservations(self):
        def fail(provider, body):
            raise p.DecisionError('http_529')
        adapters = p.ProviderAdapters(self.ledger, fail)
        for _ in range(1):
            with self.assertRaises(p.DecisionError):
                adapters.choose('deepseek', STATE, OPTIONS)
        with self.assertRaises(p.BudgetExhausted):
            self.adapters.choose('deepseek', STATE, OPTIONS)
        rows = self.read()['attempts']
        self.assertEqual(len(rows), 1)
        self.assertEqual(sum(r['reserved_nanodollars'] for r in rows), p.reservation('deepseek'))
        self.assertEqual(self.calls, [])
        # The other provider shares the remaining budget, not a separate allowance.
        self.adapters.choose('jev', STATE, OPTIONS)
        self.assertLess(self.ledger._total(self.read()), p.CAP_NANODOLLARS)

    def test_request_count_cap_and_ledger_reopen(self):
        for _ in range(p.MAX_REQUESTS):
            self.adapters.choose('jev', STATE, OPTIONS)
        reopened = p.ProviderAdapters(p.BudgetLedger(self.path), lambda *a: self.fail('must not call'))
        with self.assertRaises(p.BudgetExhausted):
            reopened.choose('jev', STATE, OPTIONS)
        self.assertEqual(len(self.read()['attempts']), 64)

    def test_invalid_decision_has_no_fallback_and_retains_usage(self):
        for provider in ('jev', 'deepseek'):
            adapters = p.ProviderAdapters(self.ledger, lambda q, b: response(q, 'illegal'))
            with self.assertRaises(p.DecisionError):
                adapters.choose(provider, STATE, OPTIONS)
        rows = self.read()['attempts']
        self.assertTrue(all(r['status'] == 'failed' and r['input_tokens'] == 100 for r in rows))
        self.assertTrue(all('decision' not in r for r in rows))

    def test_untrusted_state_and_oversize_fail_before_reservation(self):
        bad = dict(STATE, seat_visible=False)
        for state in (bad, dict(STATE, secret='not-a-key'),
                      dict(STATE, observation={'hidden_hands': []}),
                      dict(STATE, rules='x'*p.MAX_INPUT_BYTES)):
            with self.assertRaises(p.DecisionError):
                self.adapters.choose('jev', state, OPTIONS)
        self.assertFalse(self.path.exists())
        self.assertEqual(self.calls, [])

    def test_model_usage_finish_and_probability_validation(self):
        def altered(provider, body):
            r = response(provider)
            r['model'] = 'other-model'
            return r
        with self.assertRaises(p.DecisionError):
            p.ProviderAdapters(self.ledger, altered).choose('jev', STATE, OPTIONS)
        for mode in ('usage', 'finish', 'probabilities', 'confidence'):
            provider = 'deepseek' if mode == 'finish' else 'jev'
            def altered(q, body):
                r = response(q)
                if mode == 'usage': r['usage']['input_tokens'] = 65537
                if mode == 'finish': r['choices'][0]['finish_reason'] = 'length'
                if mode == 'probabilities': r['answers']['decision']['probabilities']['jink'] = 2
                if mode == 'confidence': r['answers']['decision']['confidence'] = float('nan')
                return r
            with self.assertRaises(p.DecisionError):
                p.ProviderAdapters(self.ledger, altered).choose(provider, STATE, OPTIONS)
        self.assertEqual(len(self.read()['attempts']), 5)

    def test_corrupted_ledger_fails_closed(self):
        self.path.write_text('{bad')
        with self.assertRaises(p.DecisionError):
            self.adapters.choose('jev', STATE, OPTIONS)
        self.assertEqual(self.calls, [])

    def test_failed_reservation_write_prevents_network(self):
        with patch.object(self.ledger, '_save', side_effect=OSError('offline disk failure')):
            with self.assertRaises(OSError):
                self.adapters.choose('jev', STATE, OPTIONS)
        self.assertEqual(self.calls, [])

    def test_deepseek_output_and_usage_over_limit_fail(self):
        def over_limit(provider, body):
            r = response(provider)
            r['usage']['completion_tokens'] = p.MAX_OUTPUT_TOKENS + 1
            return r
        with self.assertRaisesRegex(p.DecisionError, 'usage_invalid'):
            p.ProviderAdapters(self.ledger, over_limit).choose('deepseek', STATE, OPTIONS)
        self.assertEqual(self.read()['attempts'][0]['status'], 'failed')

    def test_credentials_stay_in_memory_and_redirects_denied(self):
        class Reply:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, n): return p.encode(response('jev'))
        class Opener:
            def open(inner, req, timeout):
                self.assertEqual(req.full_url, 'https://api.typesafe.ai/v1/systemone')
                self.assertEqual(req.get_header('Authorization'), 'Bearer fake-unit-test-key')
                self.assertNotIn(b'fake-unit-test-key', req.data)
                return Reply()
        with patch.dict('os.environ', {'TYPESAFE_API_KEY': 'fake-unit-test-key'}), \
                patch.object(p.urllib.request, 'build_opener', return_value=Opener()):
            p.ProviderAdapters(self.ledger).choose('jev', STATE, OPTIONS)
        self.assertNotIn('fake-unit-test-key', self.path.read_text())
        self.assertIsNone(p._NoRedirect().redirect_request(None))

    def test_process_crash_remains_reserved(self):
        ctx = multiprocessing.get_context('fork')
        proc = ctx.Process(target=worker, args=(str(self.path), True))
        proc.start(); proc.join(5)
        self.assertEqual(proc.exitcode, 7)
        self.assertEqual(self.read()['attempts'][0]['status'], 'reserved')
        self.adapters.choose('jev', STATE, OPTIONS)
        self.assertEqual(len(self.read()['attempts']), 2)

    def test_cross_process_shared_cap(self):
        ctx = multiprocessing.get_context('fork')
        processes = [ctx.Process(target=worker, args=(str(self.path), False, 'jev')) for _ in range(4)]
        for proc in processes: proc.start()
        for proc in processes:
            proc.join(5)
            self.assertEqual(proc.exitcode, 0)
        self.assertEqual(len(self.read()['attempts']), 4)
        self.adapters.choose('deepseek', STATE, OPTIONS)
        self.adapters.choose('deepseek', STATE, OPTIONS)
        self.assertLess(self.ledger._total(self.read()), p.CAP_NANODOLLARS)

    def test_validated_receipts_settle_and_reopen_without_resetting(self):
        for _ in range(10):
            self.adapters.choose('deepseek', STATE, OPTIONS)
        rows = self.read()['attempts']
        self.assertEqual(len(rows), 10)
        self.assertTrue(all(r['reserved_nanodollars'] == p.reservation('deepseek') for r in rows))
        self.assertTrue(all(r['settled_peak_nanodollars'] == 40800 for r in rows))
        self.assertEqual(self.ledger._total(self.read()), 408000)
        reopened = p.BudgetLedger(self.path)
        self.assertEqual(reopened._total(reopened._read()), 408000)

    def test_unknown_outcome_after_settlement_keeps_full_retry_charge(self):
        self.adapters.choose('deepseek', STATE, OPTIONS)
        def fail(q, body):
            raise p.DecisionError('proxy_or_transport_failure')
        adapters = p.ProviderAdapters(self.ledger, fail)
        with self.assertRaises(p.DecisionError):
            adapters.choose('deepseek', STATE, OPTIONS)
        with self.assertRaises(p.BudgetExhausted):
            adapters.choose('deepseek', STATE, OPTIONS)
        self.assertEqual(self.ledger._total(self.read()), 40800 + p.reservation('deepseek'))

    def test_invalid_decision_and_unexpected_reasoning_never_settle(self):
        for mode in ('illegal', 'reasoning', 'reasoning_usage', 'inconsistent_usage'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                ledger = p.BudgetLedger(Path(tmp) / 'ledger.json')
                def altered(q, body):
                    r = response(q, 'illegal' if mode == 'illegal' else 'jink')
                    if mode == 'reasoning':
                        r['choices'][0]['message']['reasoning_content'] = 'unexpected'
                    if mode == 'reasoning_usage':
                        r['usage']['completion_tokens_details'] = {'reasoning_tokens': 1}
                    if mode == 'inconsistent_usage':
                        r['usage']['total_tokens'] = 1
                    return r
                with self.assertRaises(p.DecisionError):
                    p.ProviderAdapters(ledger, altered).choose('deepseek', STATE, OPTIONS)
                data = ledger._read()
                self.assertEqual(ledger._total(data), p.reservation('deepseek'))
                self.assertNotIn('settled_peak_nanodollars', data['attempts'][0])

    def test_settled_ledger_corruption_fails_closed(self):
        self.adapters.choose('jev', STATE, OPTIONS)
        data = self.read()
        data['attempts'][0]['settled_peak_nanodollars'] = 0
        self.path.write_text(json.dumps(data))
        with self.assertRaisesRegex(p.DecisionError, 'ledger_invalid'):
            p.BudgetLedger(self.path)._read()

    def test_legacy_validated_rows_are_not_released(self):
        self.adapters.choose('deepseek', STATE, OPTIONS)
        data = self.read()
        del data['attempts'][0]['settlement_schema']
        del data['attempts'][0]['settled_peak_nanodollars']
        data['prior_upper_bound_nanodollars'] = 100000
        self.path.write_text(json.dumps(data))
        reopened = p.BudgetLedger(self.path)
        self.assertEqual(reopened._total(reopened._read()), p.reservation('deepseek') + 100000)

    def test_failed_final_write_leaves_durable_full_reservation(self):
        original = self.ledger._save
        calls = 0
        def save(data):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('offline disk failure')
            original(data)
        with patch.object(self.ledger, '_save', side_effect=save):
            with self.assertRaises(OSError):
                self.adapters.choose('deepseek', STATE, OPTIONS)
        data = p.BudgetLedger(self.path)._read()
        self.assertEqual(data['attempts'][0]['status'], 'reserved')
        self.assertNotIn('settled_peak_nanodollars', data['attempts'][0])
        self.assertEqual(self.ledger._total(data), p.reservation('deepseek'))


if __name__ == '__main__':
    unittest.main()
