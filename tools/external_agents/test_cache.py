"""Offline prefix/receipt/decision regression tests. Never use real transport."""
import copy
import json
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch

from tools.external_agents import providers as p


def state():
    return {'observation': {'self': {'objectName': 'seat_a', 'hp': 4},
            'player_columns': ['objectName', 'hp'], 'players': [['seat_a', 4]],
            'own_hand': [{'cardId': 17}], 'public_event_window': 12},
            'rules': '規則: choose a supplied legal action.',
            'seat_visible': True, 'synthetic': True}


OPTIONS = {'pass_action': 'Pass', 'play_action': 'Play'}


def response(provider):
    if provider == 'deepseek':
        return {'model': p.POLICY[provider]['model'],
                'usage': {'prompt_tokens': 100, 'completion_tokens': 10},
                'choices': [{'finish_reason': 'stop', 'message': {
                    'content': '{"choice":"play_action"}'}}]}
    return {'model': p.POLICY[provider]['model'],
            'usage': {'input_tokens': 100, 'output_tokens': 10},
            'answers': {'decision': {'type': 'choice', 'choice': 'play_action',
                'probabilities': {'pass_action': .2, 'play_action': .8}, 'confidence': .6}}}


class PrefixTests(unittest.TestCase):
    def wire(self, provider, value, options=OPTIONS):
        return p.ProviderAdapters(transport=lambda *_: self.fail('network forbidden')).prepare_payload(provider, value, options)

    def test_static_prefix_survives_seats_and_options(self):
        for provider in p.POLICY:
            a, b = state(), state()
            b['observation']['self'] = {'objectName': 'seat_b', 'hp': 1}
            b['observation']['own_hand'] = []
            left, right = self.wire(provider, a), self.wire(provider, b, dict(reversed(list(OPTIONS.items()))))
            def content(raw):
                payload = json.loads(raw)
                return (payload['messages'][1]['content'].encode() if provider == 'deepseek'
                        else p.encode(payload['state']))
            left, right = content(left), content(right)
            self.assertEqual(left.split(b'"self":')[0], right.split(b'"self":')[0])
            self.assertNotEqual(left, right)
            self.assertLess(left.index(b'"rules"'), left.index(b'"self"'))
            self.assertLess(left.index(b'"player_columns"'), left.index(b'"self"'))
            self.assertLess(left.index(b'"public_event_window"'), left.index(b'"self"'))

    def test_insertion_order_and_no_mutation(self):
        original = state()
        before = copy.deepcopy(original)
        reordered = dict(reversed(list(original.items())))
        self.assertEqual(p.prompt_state(original), p.prompt_state(reordered))
        for provider in p.POLICY:
            self.assertEqual(self.wire(provider, original), self.wire(provider, reordered))
            data = json.loads(self.wire(provider, original))
            wire = json.loads(data['messages'][1]['content'])['state'] if provider == 'deepseek' else data['state']
            del wire['prompt_contract']
            self.assertEqual(wire, original)
        self.assertEqual(original, before)

    def test_contract_invalidation(self):
        initial = p.prompt_state(state())['prompt_contract']
        for change in ('rules', 'columns', 'window'):
            changed = state()
            if change == 'rules': changed['rules'] += ' updated'
            if change == 'columns': changed['observation']['player_columns'].reverse()
            if change == 'window': changed['observation']['public_event_window'] = 6
            self.assertNotEqual(initial, p.prompt_state(changed)['prompt_contract'])
        for name, value in [('PROMPT_CONTRACT_VERSION', 2), ('DEEPSEEK_INSTRUCTION', 'new'), ('JEV_INSTRUCTION', 'new')]:
            with patch.object(p, name, value):
                self.assertNotEqual(initial, p.prompt_state(state())['prompt_contract'])

    def test_real_envelope_builders_keep_schema_ahead_of_private_data(self):
        # Import runtime builders only; no client, worker or native game is run.
        with patch.object(sys, 'path', [str(Path(__file__).parent.resolve()), *sys.path]):
            import game_client
            import hybrid50
        players = [{'objectName': 'seat_' + str(i), 'seat': i + 1, 'hp': 4,
                    'roleVisible': False, 'role': '', 'privateFlags': [], 'skills': []}
                   for i in range(50)]
        q = {'viewerObjectName': 'seat_0', 'kind': 0, 'pattern': '', 'prompt': '',
             'choiceOptions': {}, 'worldView': {'modeId': '50p', 'self': players[0],
             'players': players[1:], 'handCards': [], 'events': []}}
        for builder in (game_client.envelope, hybrid50.envelope):
            first = builder(q)
            changed = copy.deepcopy(q)
            changed['worldView']['self']['hp'] = 1
            second = builder(changed)
            self.assertEqual(p.prompt_state(first)['prompt_contract'], p.prompt_state(second)['prompt_contract'])
            for provider in p.POLICY:
                self.wire(provider, first)
                self.wire(provider, second)
        wire = p.encode(p.prompt_state(hybrid50.envelope(q)))
        self.assertLess(wire.index(b'"player_columns"'), wire.index(b'"self"'))
        self.assertLess(wire.index(b'"public_event_window"'), wire.index(b'"self"'))

    def test_wire_contract_and_options(self):
        for provider in p.POLICY:
            data = json.loads(self.wire(provider, state()))
            self.assertEqual(data['model'], p.POLICY[provider]['model'])
            if provider == 'deepseek':
                self.assertEqual(set(data), {'model', 'max_tokens', 'thinking', 'response_format', 'messages'})
                self.assertEqual(data['thinking'], {'type': 'disabled'})
                self.assertEqual(data['max_tokens'], p.MAX_OUTPUT_TOKENS)
                self.assertEqual(json.loads(data['messages'][1]['content'])['options'], OPTIONS)
            else:
                self.assertEqual(set(data), {'model', 'state', 'questions'})
                self.assertEqual(data['questions']['decision']['criteria'], OPTIONS)
            with self.assertRaises(p.DecisionError): self.wire(provider, state(), {'one': 'Only'})
            with self.assertRaises(p.DecisionError): self.wire(provider, state(), {'bad-key': 'Bad', 'ok': 'OK'})
            bad = state(); bad['observation']['hidden_hands'] = []
            with self.assertRaises(p.DecisionError): self.wire(provider, bad)
            bad = state(); bad['seat_visible'] = False
            with self.assertRaises(p.DecisionError): self.wire(provider, bad)
            bad = state(); bad['rules'] = 'x' * p.MAX_INPUT_BYTES
            with self.assertRaises(p.DecisionError): self.wire(provider, bad)


class UsageTests(unittest.TestCase):
    def usage(self, extra=None):
        r = response('deepseek'); r['usage'].update(extra or {})
        return p._usage('deepseek', r)

    def test_unknown_zero_and_formats(self):
        self.assertIsNone(self.usage()['cache_read_tokens'])
        for fields in ({'prompt_cache_hit_tokens': 0, 'prompt_cache_miss_tokens': 100},
                       {'prompt_tokens_details': {'cached_tokens': 0}}):
            self.assertEqual(self.usage(fields)['cache_read_tokens'], 0)
        receipt = self.usage({'prompt_cache_hit_tokens': 64, 'prompt_cache_miss_tokens': 36,
                              'prompt_tokens_details': {'cached_tokens': 64}})
        self.assertEqual(receipt['cache_read_tokens'], 64)
        self.assertEqual(receipt['cache_miss_tokens'], 36)
        self.assertIsNone(receipt['cache_write_tokens'])
        self.assertEqual(receipt['local_peak_usage_estimate_nanodollars'], 42000)
        jev = response('jev'); jev['usage']['prompt_cache_hit_tokens'] = 100
        self.assertIsNone(p._usage('jev', jev)['cache_read_tokens'])

    def test_reject_invalid_counters(self):
        for value in (True, -1, 1.5, '64', [], {}, 101):
            for field in ('prompt_cache_hit_tokens', 'prompt_cache_miss_tokens'):
                with self.subTest(value=value, field=field), self.assertRaises(p.DecisionError): self.usage({field: value})
            with self.assertRaises(p.DecisionError): self.usage({'prompt_tokens_details': {'cached_tokens': value}})
        for fields in ({'prompt_tokens_details': []},
                       {'prompt_cache_hit_tokens': 64, 'prompt_cache_miss_tokens': 35},
                       {'prompt_cache_hit_tokens': 64, 'prompt_tokens_details': {'cached_tokens': 63}}):
            with self.assertRaises(p.DecisionError): self.usage(fields)

    def test_missing_mandatory_usage_still_rejected(self):
        for provider in p.POLICY:
            with self.assertRaises(p.DecisionError): p._usage(provider, {})
            r = response(provider); r['usage'] = {}
            with self.assertRaises(p.DecisionError): p._usage(provider, r)

    def test_ledger_preserves_peak_settlement_and_no_response_reuse(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'ledger.json'
            calls = []
            r = response('deepseek'); r['usage'].update(prompt_cache_hit_tokens=100, prompt_cache_miss_tokens=0)
            def transport(provider, body):
                calls.append(body)
                return copy.deepcopy(r)
            ledger = p.BudgetLedger(path)
            adapter = p.ProviderAdapters(ledger=ledger, transport=transport)
            for _ in range(2): self.assertEqual(adapter.choose('deepseek', state(), OPTIONS), 'play_action')
            self.assertEqual(len(calls), 2)
            rows = ledger._read()['attempts']
            for row in rows:
                self.assertEqual(row['settled_peak_nanodollars'], 42000)
                self.assertEqual(row['cache_read_tokens'], 100)
                self.assertIsNone(row['provider_reported_billed_usd'])
                self.assertEqual(row['prompt_contract'], p.prompt_state(state())['prompt_contract'])
                self.assertNotIn('seat_a', json.dumps(row))
            # Existing schema-1 receipts with no cache metadata remain readable.
            data = json.loads(path.read_text())
            for row in data['attempts']:
                for key in list(row):
                    if key.startswith('cache_') or key == 'prompt_contract': del row[key]
            path.write_text(json.dumps(data))
            self.assertEqual(ledger._total(ledger._read()), 84000)

    def test_failed_validation_keeps_reservation(self):
        cases = []
        for provider in p.POLICY:
            r = response(provider); r['model'] = 'other'; cases.append((provider, r))
        r = response('deepseek'); r['usage']['prompt_cache_hit_tokens'] = 101; cases.append(('deepseek', r))
        r = response('deepseek'); r['choices'][0]['message']['content'] = '{"choice":"other"}'; cases.append(('deepseek', r))
        r = response('deepseek'); r['choices'][0]['message']['content'] = '{"choice":"play_action","extra":1}'; cases.append(('deepseek', r))
        r = response('jev'); r['answers']['decision']['choice'] = 'pass_action'; cases.append(('jev', r))
        r = response('jev'); r['answers']['decision']['probabilities']['play_action'] = .5; cases.append(('jev', r))
        for provider, r in cases:
            with tempfile.TemporaryDirectory() as directory:
                ledger = p.BudgetLedger(Path(directory) / 'ledger.json')
                adapter = p.ProviderAdapters(ledger=ledger, transport=lambda *_: r)
                with self.assertRaises(p.DecisionError): adapter.choose(provider, state(), OPTIONS)
                row = ledger._read()['attempts'][0]
                self.assertNotIn('settled_peak_nanodollars', row)
                self.assertEqual(ledger._total(ledger._read()), p.reservation(provider))

    def test_jev_choice_compatibility_unchanged(self):
        weights, diagnostics = p._validate_choice_probabilities({'pass_action': .2, 'play_action': .799}, .6, OPTIONS)
        self.assertIsNotNone(weights)
        self.assertFalse(diagnostics['issues'])
        weights, diagnostics = p._validate_choice_probabilities({'pass_action': .2, 'play_action': .795}, .6, OPTIONS)
        self.assertTrue(diagnostics['normalization_applied'])
        self.assertGreater(weights['play_action'], weights['pass_action'])


if __name__ == '__main__':
    unittest.main()
