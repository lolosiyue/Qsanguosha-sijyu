"""Synthetic JEV Choice regressions. Transport is an in-memory fixture only."""
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import providers as p


OPTIONS = {f'action_{i}': f'Synthetic legal action {i}' for i in range(50)}
STATE = {'synthetic': True, 'seat_visible': True,
         'rules': 'Select one supplied legal option.', 'observation': {'players': 50}}


def response():
    return {'model': 'jev-1.13.0', 'usage': {'input_tokens': 5000, 'output_tokens': 500},
            'answers': {'decision': {'type': 'choice', 'choice': 'action_0',
                'probabilities': {key: .02 for key in OPTIONS}, 'confidence': 0.0}}}


class JevProbabilityTests(unittest.TestCase):
    def run_fixture(self, fixture, error=None, issue=None, options=None):
        options = OPTIONS if options is None else options
        with tempfile.TemporaryDirectory(prefix='offline-jev-choice-') as tmp:
            ledger = p.BudgetLedger(Path(tmp) / 'fixture-ledger.json')
            calls = []
            def memory_transport(provider, body):
                self.assertEqual(provider, 'jev')
                self.assertEqual(ledger._read()['attempts'][-1]['status'], 'reserved')
                calls.append(provider)
                return copy.deepcopy(fixture)
            adapter = p.ProviderAdapters(ledger, memory_transport, allowed_providers=('jev',))
            if error:
                with self.assertRaisesRegex(p.DecisionError, '^' + error + '$'):
                    adapter.choose('jev', STATE, options)
            else:
                self.assertEqual(adapter.choose('jev', STATE, options),
                                 fixture['answers']['decision']['choice'])
            self.assertEqual(calls, ['jev'])
            row = ledger._read()['attempts'][0]
            if error:
                self.assertEqual(row['status'], 'failed')
                self.assertNotIn('settled_peak_nanodollars', row)
                self.assertEqual(ledger._total(ledger._read()), p.reservation('jev'))
                if issue:
                    self.assertIn(issue, row['probability_validation']['issues'])
            else:
                self.assertEqual(row['status'], 'validated')
                self.assertEqual(row['probability_validation']['issues'], [])
                self.assertEqual(row['settled_peak_nanodollars'],
                                 fixture['usage']['input_tokens'] * 42)
            diagnostic = row.get('probability_validation', {})
            encoded = json.dumps(diagnostic, allow_nan=False)
            self.assertNotIn('action_', encoded)
            self.assertNotIn('Synthetic legal', encoded)
            self.assertNotIn('observation', encoded)
            return row

    def test_full_fifty_option_distribution_and_tied_maximum(self):
        row = self.run_fixture(response())
        self.assertEqual(row['probability_validation']['probability_count'], 50)
        self.assertAlmostEqual(row['probability_validation']['probability_sum'], 1)

    def test_complete_fifty_option_small_drift_is_bounded_and_normalized(self):
        for first, total in ((.03, 1.01), (.01, .99)):
            with self.subTest(total=total):
                fixture = response()
                fixture['answers']['decision']['probabilities']['action_0'] = first
                if first == .01:
                    fixture['answers']['decision']['choice'] = 'action_1'
                row = self.run_fixture(fixture)
                self.assertAlmostEqual(row['probability_validation']['probability_sum'], total)
                diagnostic = row['probability_validation']
                self.assertTrue(diagnostic['normalization_applied'])
                self.assertTrue(diagnostic['ranking_preserved'])
                self.assertAlmostEqual(diagnostic['normalization_scale'], 1 / total)
                self.assertAlmostEqual(diagnostic['normalized_sum'], 1)

    def test_strict_band_keeps_original_values_and_compatibility_is_explicit(self):
        fixture = response()
        fixture['answers']['decision']['probabilities']['action_0'] += .0009
        row = self.run_fixture(fixture)
        self.assertFalse(row['probability_validation']['normalization_applied'])
        fixture['answers']['decision']['probabilities']['action_0'] += .0002
        row = self.run_fixture(fixture)
        self.assertTrue(row['probability_validation']['normalization_applied'])

    def test_observed_b_aggregate_49_options_point_99(self):
        # Reproduce the retained B aggregate/usage, not its unavailable raw vector.
        options = {k: v for k, v in OPTIONS.items() if k != 'action_49'}
        fixture = response()
        fixture['usage'] = {'input_tokens': 5662, 'output_tokens': 499}
        probabilities = fixture['answers']['decision']['probabilities']
        probabilities.pop('action_49')
        probabilities['action_0'] = .03  # .03 + 48*.02 = .99
        row = self.run_fixture(fixture, options=options)
        diagnostic = row['probability_validation']
        self.assertEqual(diagnostic['expected_count'], 49)
        self.assertEqual(diagnostic['probability_count'], 49)
        self.assertEqual(diagnostic['probability_sum'], .99)
        self.assertTrue(diagnostic['normalization_applied'])
        self.assertEqual(row['decision'], 'action_0')

    def distribution(self, total, count=50):
        options = {f'action_{i}': f'Synthetic legal action {i}' for i in range(count)}
        fixture = response()
        fixture['answers']['decision']['probabilities'] = {key: 0 for key in options}
        fixture['answers']['decision']['probabilities'].update(
            action_0=total / 2, action_1=total / 2)
        return fixture, options

    def test_inclusive_compatibility_edges_and_one_ulp_outside(self):
        for boundary, direction in ((.99, -math.inf), (1.01, math.inf)):
            with self.subTest(boundary=boundary):
                fixture, options = self.distribution(boundary)
                row = self.run_fixture(fixture, options=options)
                self.assertTrue(row['probability_validation']['normalization_applied'])
                fixture, options = self.distribution(math.nextafter(boundary, direction))
                self.run_fixture(fixture, 'probabilities_invalid', 'probability_sum_invalid', options)

    def test_inclusive_strict_edges_and_just_outside_are_audited(self):
        for boundary, direction in ((.999, -math.inf), (1.001, math.inf)):
            with self.subTest(boundary=boundary):
                fixture, options = self.distribution(boundary)
                row = self.run_fixture(fixture, options=options)
                self.assertFalse(row['probability_validation']['normalization_applied'])
                fixture, options = self.distribution(math.nextafter(boundary, direction))
                row = self.run_fixture(fixture, options=options)
                self.assertTrue(row['probability_validation']['normalization_applied'])

    def test_zero_and_materially_inconsistent_sums_are_not_normalized(self):
        for total in (0, .5, .98, 1.02, 1.5, 2):
            with self.subTest(total=total):
                fixture, options = self.distribution(total)
                row = self.run_fixture(fixture, 'probabilities_invalid', 'probability_sum_invalid', options)
                self.assertFalse(row['probability_validation']['normalization_applied'])

    def test_tolerance_does_not_grow_with_option_count(self):
        for count in (2, 49, 50, 255):
            with self.subTest(count=count):
                fixture, options = self.distribution(.99, count)
                self.run_fixture(fixture, options=options)
                fixture, options = self.distribution(.98, count)
                self.run_fixture(fixture, 'probabilities_invalid', 'probability_sum_invalid', options)

    def test_normalization_preserves_all_orderings_ties_and_input(self):
        probabilities = {f'action_{i}': (i+1)*.99/1275 for i in range(50)}
        original = probabilities.copy()
        normalized, diagnostic = p._validate_choice_probabilities(probabilities, .5, OPTIONS)
        self.assertEqual(probabilities, original)
        self.assertTrue(diagnostic['normalization_applied'])
        for left in probabilities:
            for right in probabilities:
                self.assertEqual(probabilities[left] < probabilities[right],
                                 normalized[left] < normalized[right])
                self.assertEqual(probabilities[left] == probabilities[right],
                                 normalized[left] == normalized[right])
        fixture, options = self.distribution(.99)
        self.run_fixture(fixture, options=options)  # tied maximum keeps supplied choice

    def test_floating_point_rank_collapse_is_rejected(self):
        fixture = response()
        tiny = math.ulp(0.0)
        probabilities = {key: 0 for key in OPTIONS}
        probabilities.update(action_0=.51, action_1=.5,
                             action_2=50*tiny, action_3=51*tiny)
        self.assertLess(probabilities['action_2'], probabilities['action_3'])
        self.assertEqual(probabilities['action_2']/1.01, probabilities['action_3']/1.01)
        fixture['answers']['decision']['probabilities'] = probabilities
        self.run_fixture(fixture, 'probabilities_invalid', 'normalization_rank_changed')

    def test_duplicate_wire_keys_are_rejected_before_collapsing_or_settlement(self):
        for before, after in (
            ('"action_0":0.02', '"action_0":0.02,"action_0":0.02'),
            ('"choice":"action_0"', '"choice":"action_0","choice":"action_0"')):
            with self.subTest(duplicate=before), tempfile.TemporaryDirectory() as tmp:
                raw = p.encode(response()).replace(before.encode(), after.encode(), 1)
                self.assertIn(after.encode(), raw)
                class Reply:
                    def __enter__(self): return self
                    def __exit__(self, *args): pass
                    def read(self, n): return raw
                class Opener:
                    def open(self, request, timeout): return Reply()
                ledger = p.BudgetLedger(Path(tmp)/'fixture-ledger.json')
                with patch.dict('os.environ', {'TYPESAFE_API_KEY': 'offline-fixture-not-a-key'}), \
                        patch.object(p.urllib.request, 'build_opener', return_value=Opener()):
                    with self.assertRaisesRegex(p.DecisionError, '^response_duplicate_key$'):
                        p.ProviderAdapters(ledger).choose('jev', STATE, OPTIONS)
                row = ledger._read()['attempts'][0]
                self.assertEqual(row['status'], 'failed')
                self.assertNotIn('settled_peak_nanodollars', row)
                self.assertEqual(ledger._total(ledger._read()), p.reservation('jev'))
                self.assertNotIn('offline-fixture-not-a-key', ledger.path.read_text())

    def test_compatibility_does_not_repair_illegal_or_nonmaximal_choice(self):
        fixture, options = self.distribution(.99)
        fixture['answers']['decision']['choice'] = 'not_a_legal_action'
        self.run_fixture(fixture, 'choice_not_legal', options=options)
        fixture['answers']['decision']['choice'] = 'action_2'
        self.run_fixture(fixture, 'choice_not_highest_probability', options=options)

    def test_missing_unexpected_and_wrong_container_keys(self):
        for mode in ('missing', 'unexpected', 'list'):
            with self.subTest(mode=mode):
                fixture = response(); answer = fixture['answers']['decision']
                if mode == 'missing':
                    answer['probabilities'].pop('action_49')
                elif mode == 'unexpected':
                    answer['probabilities']['untrusted_extra_key'] = 0
                else:
                    answer['probabilities'] = list(answer['probabilities'].values())
                self.run_fixture(fixture, 'probabilities_invalid',
                    'probabilities_not_object' if mode == 'list' else 'probability_keys_mismatch')

    def test_invalid_numeric_probability_is_diagnosable_and_json_safe(self):
        for value in (True, '0.02', None, -.01, 1.01, float('nan'), float('inf'), 10**400):
            with self.subTest(value_type=type(value).__name__):
                fixture = response()
                fixture['answers']['decision']['probabilities']['action_0'] = value
                row = self.run_fixture(fixture, 'probabilities_invalid', 'probability_value_invalid')
                self.assertEqual(row['probability_validation']['invalid_value_count'], 1)
                self.assertNotIn('probability_sum', row['probability_validation'])

    def test_invalid_confidence_preserves_reservation(self):
        for value in (True, '0.5', None, -.01, 1.01, float('nan'), float('inf'), 10**400):
            with self.subTest(value_type=type(value).__name__):
                fixture = response(); fixture['answers']['decision']['confidence'] = value
                self.run_fixture(fixture, 'probabilities_invalid', 'confidence_invalid')

    def test_missing_answer_fields_and_wrong_model_remain_rejected(self):
        for field in ('probabilities', 'confidence', 'choice'):
            with self.subTest(field=field):
                fixture = response(); fixture['answers']['decision'].pop(field)
                self.run_fixture(fixture, 'decision_shape_invalid')
        fixture = response(); fixture['model'] = 'different-model'
        self.run_fixture(fixture, 'response_model_mismatch')

    def test_illegal_or_nonmaximal_choice_is_never_settled(self):
        fixture = response(); fixture['answers']['decision']['choice'] = 'not_a_legal_action'
        self.run_fixture(fixture, 'choice_not_legal')
        fixture = response(); answer = fixture['answers']['decision']
        answer['probabilities']['action_0'] = .01; answer['probabilities']['action_1'] = .03
        self.run_fixture(fixture, 'choice_not_highest_probability')


if __name__ == '__main__':
    unittest.main()
