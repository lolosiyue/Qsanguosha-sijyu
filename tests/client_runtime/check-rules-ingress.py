#!/usr/bin/env python3
"""W3 raw Protocol V2 ingress: production native stream and WASM ABI parity."""
from __future__ import annotations
import argparse
import contextlib
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
CHECKS = ['native_handshake', 'native_reducer_query', 'stale_revision', 'atomic_sync', 'snapshot_rejected',
          'failure_rollback', 'generation_isolation', 'uint64', 'reply_invalidation', 'terminal_shutdown']
QUERIES = ['select_slash', 'query_after_mark', 'query_after_sync', 'uint64_request']
NEGATIVE = ['stale_revision', 'query_during_sync', 'old_request_after_sync', 'reject_snapshot',
            'reject_frame', 'failed_stream_blocks_query', 'old_generation_ignored', 'reply_invalidates_request']

def require(condition, message):
    if not condition:
        raise AssertionError(message)

def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), HERE / name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def digest(data):
    return hashlib.sha256(data).hexdigest()

def verify_native(report):
    require(type(report.get('schema_version')) is int and report['schema_version'] == 1
            and report.get('status') == 'PASS', 'native gate did not pass')
    require(report.get('checks') == CHECKS, 'native checks missing/changed')
    records = report.get('records')
    require(isinstance(records, list) and records, 'empty stream corpus')
    by_label = {}
    for record in records:
        require(isinstance(record, dict) and isinstance(record.get('operation'), dict)
                and isinstance(record.get('response_utf8'), str), 'invalid stream record')
        by_label.setdefault(record['label'], []).append(json.loads(record['response_utf8']))
    for label in QUERIES + NEGATIVE:
        require(len(by_label.get(label, [])) == 1, 'required case missing/duplicated: ' + label)
    for label in QUERIES:
        result = by_label[label][0]
        require(result.get('success') is True and result.get('evaluation', {}).get('known') is True
                and result['evaluation'].get('can_confirm') is True, 'native card query failed: ' + label)
        require(isinstance(result['evaluation'].get('wire'), dict), 'canonical reply missing')
    require(by_label['uint64_request'][0]['evaluation']['wire']['reply_to'] == '18446744073709551615',
            'uint64 identity drift')
    for label in NEGATIVE:
        value = by_label[label][0]
        require(value.get('success') is False and value.get('can_confirm') is False
                and 'wire' in value and value['wire'] is None, 'negative operation published a reply')
    require(by_label['old_generation_ignored'][0]['status']['failed'] is False, 'obsolete generation poisoned state')
    for left, right in [('committed_view', 'view_during_sync'), ('view_after_sync', 'rollback_after_failure')]:
        require(len(by_label.get(left, [])) == len(by_label.get(right, [])) == 1, 'state barrier evidence missing')
        require(by_label[left][0]['state'] == by_label[right][0]['state'], 'uncommitted or failed state leaked')

def verify_browser(report, baseline):
    verify_native(baseline)
    require(report.get('status') == 'COMPLETE', 'browser failed: ' + str(report.get('error')))
    require(isinstance(report.get('rounds'), list) and len(report['rounds']) == 2, 'missing fresh Worker replay')
    for run in report['rounds']:
        require(run.get('disposed') is True and run.get('dedicatedWorker') is True
                and run.get('documentAbsent') is True, 'missing Worker/lifecycle evidence')
        require(json.dumps(run['registry'], sort_keys=True) == json.dumps(baseline['registry'], sort_keys=True),
                'native/WASM identity drift')
        records = run.get('records', [])
        require([r['label'] for r in records] == [r['label'] for r in baseline['records']],
                'stream cases omitted/duplicated/reordered')
        for actual, expected in zip(records, baseline['records']):
            require(actual.get('response_utf8') == expected['response_utf8'],
                    'native/WASM canonical bytes differ: ' + expected['label'])

def native_run(args, assets, fixed):
    directory = args.artifacts / ('native-fixed' if fixed else 'native-random')
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / 'report.json'
    output.unlink(missing_ok=True)
    env = os.environ.copy()
    env.pop('QT_HASH_SEED', None)
    if fixed:
        env['QT_HASH_SEED'] = '0'
    with tempfile.TemporaryDirectory(prefix='ingress-', dir=directory) as temporary:
        root = Path(temporary)
        for key in ['HOME', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'APPDATA', 'LOCALAPPDATA']:
            env[key] = str(root / key.lower())
        child = subprocess.run([str(args.native_runner), '--asset-root', str(assets), '--output', str(output)],
                               cwd=root, env=env, capture_output=True, timeout=120, check=False)
    (directory / 'stdout.log').write_bytes(child.stdout)
    (directory / 'stderr.log').write_bytes(child.stderr)
    require(child.returncode == 0 and output.is_file(), child.stderr.decode('utf-8', 'replace')[-6000:])
    report = json.loads(output.read_bytes())
    verify_native(report)
    return report

def browser_run(args, baseline, assets):
    server_class = load('check-rules-session.py').Server
    module, binary = args.wasm_module, args.wasm_module.with_suffix('.wasm')
    require(binary.read_bytes()[:8] == b'\0asm\1\0\0\0', 'invalid real WASM binary')
    content_files = []
    for path in sorted(assets.rglob('*.lua')):
        relative = path.relative_to(assets).as_posix()
        if relative.startswith('lua/ai/') or relative == 'lua/lib/middleclass.lua':
            continue
        data = path.read_bytes()
        content_files.append({'path': relative, 'role': 'rules', 'size': len(data), 'sha256': digest(data)})
    content = {'schema_version': 1, 'profile': 'declared-v1', 'files': content_files}
    plan = {'operations': [{'label': r['label'], 'operation': r['operation']} for r in baseline['records']],
            'hashes': {'module': digest(module.read_bytes()), 'binary': digest(binary.read_bytes()),
                       }, 'content': content}
    # Never serve baseline responses: the page must call the actual C exports.
    routes = {'/index.html': b'<!doctype html><meta charset="utf-8"><script type="module" src="/probe.mjs"></script>',
              '/probe.mjs': HERE / 'browser/rules-ingress-page.mjs',
              '/browser/rules-ingress-worker.mjs': HERE / 'browser/rules-ingress-worker.mjs',
              '/wasm-fixture-host.mjs': HERE / 'wasm-fixture-host.mjs',
              '/input.json': json.dumps(plan).encode(),
              '/browser/qsanguosha_client_wasm.mjs': module,
              '/browser/qsanguosha_client_wasm.wasm': binary}
    for entry in content_files:
        root_path = assets / entry['path']
        routes['/rules/content/' + entry['sha256']] = root_path
    with server_class(routes) as server, tempfile.TemporaryDirectory(prefix='chrome-', dir=args.artifacts) as profile:
        command = [str(args.browser), '--headless=new', '--disable-gpu', '--no-first-run',
                   '--no-default-browser-check', '--disable-background-networking', '--disable-extensions',
                   '--disable-dev-shm-usage', '--user-data-dir=' + profile, *args.browser_arg,
                   server.origin + '/index.html?token=' + server.token]
        with (args.artifacts / 'browser.stdout.log').open('wb') as out, (args.artifacts / 'browser.stderr.log').open('wb') as err:
            process = subprocess.Popen(command, stdout=out, stderr=err, start_new_session=os.name == 'posix')
            try:
                deadline = time.monotonic() + 300
                while not server.done.wait(.1):
                    require(process.poll() is None, 'browser exited before report')
                    require(time.monotonic() < deadline, 'browser ingress timeout')
            finally:
                if os.name == 'posix':
                    with contextlib.suppress(ProcessLookupError): os.killpg(process.pid, signal.SIGTERM)
                elif process.poll() is None: process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    if os.name == 'posix':
                        with contextlib.suppress(ProcessLookupError): os.killpg(process.pid, signal.SIGKILL)
                    else: process.kill()
                    process.wait(timeout=5)
                (args.artifacts / 'http-requests.json').write_text(json.dumps(server.requests), encoding='utf-8')
        require(server.report is not None, 'no browser evidence')
        (args.artifacts / 'browser-report.json').write_text(json.dumps(server.report), encoding='utf-8')
        verify_browser(server.report, baseline)
    return plan['hashes']

class VerifierTests(unittest.TestCase):
    def sample(self):
        records = [];
        for label in QUERIES + NEGATIVE + ['committed_view', 'view_during_sync', 'view_after_sync', 'rollback_after_failure']:
            value = {'success': label not in NEGATIVE, 'status': {'failed': False}, 'wire': None,
                     'can_confirm': False, 'state': {'committed': True}}
            if label in QUERIES:
                value['evaluation'] = {'known': True, 'can_confirm': True, 'wire': {'reply_to': '18446744073709551615'}}
            records.append({'label': label, 'operation': {'synthetic': True}, 'response_utf8': json.dumps(value)})
        baseline = {'schema_version': 1, 'status': 'PASS', 'checks': CHECKS, 'records': records, 'registry': {'count': 1}}
        run = {'registry': {'count': 1}, 'disposed': True, 'dedicatedWorker': True, 'documentAbsent': True,
               'records': [{'label': r['label'], 'response_utf8': r['response_utf8']} for r in records]}
        return baseline, {'status': 'COMPLETE', 'rounds': [copy.deepcopy(run), copy.deepcopy(run)]}
    def test_valid_synthetic_verifier_input(self):
        baseline, report = self.sample(); verify_browser(report, baseline)
    def test_byte_difference(self):
        baseline, report = self.sample(); report['rounds'][0]['records'][0]['response_utf8'] += '\n'
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_omitted_case(self):
        baseline, report = self.sample(); report['rounds'][1]['records'].pop()
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_registry_type_drift(self):
        baseline, report = self.sample(); report['rounds'][0]['registry']['count'] = True
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_integer_is_not_shutdown_evidence(self):
        baseline, report = self.sample(); report['rounds'][0]['disposed'] = 1
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_false_positive_native(self):
        baseline, _ = self.sample(); baseline['records'] = []
        with self.assertRaises(AssertionError): verify_native(baseline)
    def test_rollback_leak(self):
        baseline, _ = self.sample(); value = json.loads(baseline['records'][-1]['response_utf8']); value['state'] = {}
        baseline['records'][-1]['response_utf8'] = json.dumps(value)
        with self.assertRaises(AssertionError): verify_native(baseline)
    def test_missing_second_worker(self):
        baseline, report = self.sample(); report['rounds'].pop()
        with self.assertRaises(AssertionError): verify_browser(report, baseline)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    for name in ['native-runner', 'asset-root', 'artifacts', 'wasm-module', 'manifest', 'browser']:
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--browser-arg', action='append', default=[])
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(VerifierTests)).wasSuccessful() else 1
    for name in ['native_runner', 'asset_root', 'artifacts']:
        if getattr(args, name) is None: parser.error('--' + name.replace('_', '-') + ' required')
    browser_mode = any([args.wasm_module, args.manifest, args.browser])
    if browser_mode and not all([args.wasm_module, args.browser]):
        parser.error('browser mode requires --wasm-module and --browser')
    for key, value in vars(args).items():
        if isinstance(value, Path): setattr(args, key, value.resolve())
    args.artifacts.mkdir(parents=True, exist_ok=True)
    summary = {'schema_version': 1, 'status': 'NOT_RUN', 'browser': 'NOT_RUN'}
    output = args.artifacts / 'ingress-summary.json'
    output.write_text(json.dumps(summary), encoding='utf-8')
    try:
        native = load('check-fixtures.py')
        with tempfile.TemporaryDirectory(prefix='assets-', dir=args.artifacts) as temporary:
            assets = Path(temporary)
            native.stage_builtin_assets(args.asset_root, assets)
            baseline = native_run(args, assets, True)
            repeated = native_run(args, assets, False)
            require(json.dumps(baseline, sort_keys=True) == json.dumps(repeated, sort_keys=True), 'native hash-seed drift')
            summary['native'] = 'PASS'
            summary['operations_per_run'] = len(baseline['records'])
            summary['native_runner_sha256'] = digest(args.native_runner.read_bytes())
            if browser_mode:
                summary['hashes'] = browser_run(args, baseline, assets)
                summary['browser'] = 'PASS'
        summary['status'] = 'PASS'
        return 0
    except Exception as error:
        summary['status'] = 'FAIL'; summary['error'] = str(error)
        print(str(error))
        return 1
    finally:
        output.write_text(json.dumps(summary, indent=2), encoding='utf-8')

if __name__ == '__main__':
    raise SystemExit(main())
