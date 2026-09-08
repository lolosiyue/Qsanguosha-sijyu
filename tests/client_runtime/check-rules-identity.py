#!/usr/bin/env python3
"""W2 bootstrap identity/HELLO contract. Uses the real native probe, not a simulator."""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
HASH_KEYS = ('source_sha256', 'bindings_sha256', 'lua_sha256', 'card_registry_sha256')


def validate(result: dict, available: bool) -> dict:
    if (type(result.get('schema_version')) is not int or result['schema_version'] != 1
            or result.get('status') != 'PASS' or type(result.get('failures')) is not int
            or result['failures'] != 0 or type(result.get('checks')) is not int
            or result['checks'] < (16 if available else 7)):
        raise AssertionError('missing/failed native identity checks')
    identity = result.get('identity')
    if not isinstance(identity, dict) or type(identity.get('schema_version')) is not int or identity['schema_version'] != 1 or identity.get('available') is not available:
        raise AssertionError('wrong identity availability')
    if available:
        if (type(identity.get('protocol_version')) is not int or type(identity.get('bridge_schema')) is not int
                or identity.get('profile') != 'builtin-v1' or identity.get('protocol_version') != 2
                or identity.get('bridge_schema') != 1 or identity.get('rules_abi') != 'qsan-client-rules-v1'
                or type(identity.get('card_count')) is not int or identity['card_count'] <= 0
                or not isinstance(identity.get('packages'), list) or not identity['packages']
                or identity.get('interaction_schemas') != {'card-selection': 1}):
            raise AssertionError('invalid identity contract')
        for key in HASH_KEYS:
            if not isinstance(identity.get(key), str) or not re.fullmatch('[0-9a-f]{64}', identity[key]):
                raise AssertionError('invalid identity hash: ' + key)
    elif not identity.get('reason'):
        raise AssertionError('unsupported identity requires a diagnostic')
    return identity


def native_harness():
    spec = importlib.util.spec_from_file_location('native_rules_harness', HERE / 'check-fixtures.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check(args):
    args.artifacts.mkdir(parents=True, exist_ok=True)
    summary = args.artifacts / 'identity-summary.json'
    summary.write_text('{"schema_version":1,"status":"NOT_RUN"}\n')
    identities = {}
    for mode in ('first', 'second', 'lua-change', 'extra-content'):
        with tempfile.TemporaryDirectory(prefix='identity-', dir=args.artifacts) as tmp:
            scratch = Path(tmp)
            assets = scratch / 'assets'
            native_harness().stage_builtin_assets(args.asset_root, assets)
            # Audit the explicit W2 profile against the original staging source.
            # A changed closure must fail, not quietly gain an untested profile.
            cpp = (args.asset_root / 'src/core/rules-bundle-identity.cpp').read_text(encoding='utf-8')
            section = cpp.split('const QStringList expected{', 1)[1].split('};', 1)[0]
            declared = re.findall(r'QStringLiteral\("([^"]+)"\)', section)
            actual = sorted(p.relative_to(assets).as_posix() for p in assets.rglob('*') if p.is_file())
            if actual != declared:
                raise AssertionError('builtin profile differs from the native fixture bootstrap closure')
            if mode == 'lua-change':
                with (assets / 'lua/config.lua').open('ab') as output:
                    output.write(b'\n-- identical registry, different rules content\n')
            if mode == 'extra-content':
                (assets / 'lua/undeclared.txt').write_text('not part of audited profile')
            result_path = args.artifacts / f'{mode}.json'
            result_path.unlink(missing_ok=True)
            env = os.environ.copy()
            for name, directory in (('HOME', 'home'), ('XDG_CONFIG_HOME', 'config'),
                                    ('XDG_DATA_HOME', 'data'), ('APPDATA', 'appdata'),
                                    ('LOCALAPPDATA', 'localappdata'), ('QSAN_USER_DATA_ROOT', 'userdata')):
                env[name] = str(scratch / directory)
            if mode == 'second': env.pop('QT_HASH_SEED', None)
            else: env['QT_HASH_SEED'] = '0'
            command = [str(args.runner), '--asset-root', str(assets), '--output', str(result_path)]
            if mode == 'extra-content': command.append('--expect-unavailable')
            else: command.append('--allow-staged-mutation')
            child = subprocess.run(command, cwd=scratch, env=env, capture_output=True, timeout=90)
            (args.artifacts / f'{mode}.stdout.log').write_bytes(child.stdout)
            (args.artifacts / f'{mode}.stderr.log').write_bytes(child.stderr)
            if child.returncode != 0 or not result_path.is_file():
                raise AssertionError(f'{mode}: native probe failed ({child.returncode}): {child.stderr[-4000:]!r}')
            identities[mode] = validate(json.loads(result_path.read_bytes()), mode != 'extra-content')
    if identities['first'] != identities['second']:
        raise AssertionError('identity changes across fresh processes/hash seeds')
    first, changed = identities['first'], identities['lua-change']
    if first['lua_sha256'] == changed['lua_sha256']:
        raise AssertionError('Lua content drift was not fingerprinted')
    if {k: v for k, v in first.items() if k != 'lua_sha256'} != {k: v for k, v in changed.items() if k != 'lua_sha256'}:
        raise AssertionError('comment-only content change altered non-Lua identity fields')
    summary.write_text(json.dumps({'schema_version': 1, 'status': 'PASS', 'processes': 4,
                                  'identity': first}, sort_keys=True) + '\n')
    print('[AUTOTEST] RULES_IDENTITY_RESULT status=PASS processes=4')


class Tests(unittest.TestCase):
    def result(self):
        return {'schema_version': 1, 'status': 'PASS', 'checks': 16, 'failures': 0,
                'identity': {'schema_version': 1, 'available': True, 'profile': 'builtin-v1', 'protocol_version': 2,
                             'bridge_schema': 1, 'rules_abi': 'qsan-client-rules-v1', 'card_count': 1,
                             'packages': ['standard'], 'interaction_schemas': {'card-selection': 1},
                             **{key: 'a' * 64 for key in HASH_KEYS}}}
    def test_valid(self): validate(self.result(), True)
    def test_not_run(self):
        with self.assertRaises(AssertionError): validate({'status': 'NOT_RUN'}, True)
    def test_missing_checks(self):
        value = self.result(); value['checks'] = 1
        with self.assertRaises(AssertionError): validate(value, True)
    def test_false_pass(self):
        value = self.result(); value['failures'] = 1
        with self.assertRaises(AssertionError): validate(value, True)
    def test_wrong_availability(self):
        value = self.result(); value['identity']['available'] = 1
        with self.assertRaises(AssertionError): validate(value, True)
    def test_hash_required(self):
        for key in HASH_KEYS:
            value = self.result(); value['identity'][key] = ''
            with self.assertRaises(AssertionError): validate(value, True)
    def test_numeric_coercion(self):
        value = self.result(); value['identity']['card_count'] = True
        with self.assertRaises(AssertionError): validate(value, True)
    def test_unsupported_needs_reason(self):
        value = self.result(); value['identity'] = {'schema_version': 1, 'available': False}
        with self.assertRaises(AssertionError): validate(value, False)
        value['identity']['reason'] = 'unsupported_content'; validate(value, False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--asset-root', type=Path)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(Tests)).wasSuccessful() else 1
    for key in ('runner', 'asset_root', 'artifacts'):
        if getattr(args, key) is None: parser.error('--' + key.replace('_', '-') + ' required')
        setattr(args, key, getattr(args, key).resolve())
    try:
        check(args)
        return 0
    except (AssertionError, OSError, ValueError, IndexError, subprocess.TimeoutExpired) as error:
        args.artifacts.mkdir(parents=True, exist_ok=True)
        (args.artifacts / 'identity-summary.json').write_text(json.dumps(
            {'schema_version': 1, 'status': 'FAIL', 'error': str(error)}) + '\n')
        print(str(error), file=__import__('sys').stderr)
        return 1

if __name__ == '__main__': raise SystemExit(main())
