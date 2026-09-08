#!/usr/bin/env python3
"""Production session lifecycle/native-to-real-Worker parity. No fixture evaluator."""
from __future__ import annotations

import argparse
import contextlib
import copy
import hashlib
import http.server
import importlib.util
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = Path(__file__).resolve().parent
LABELS = ['A', 'B_incomplete', 'A_after_B', 'invalid_scene', 'A_after_invalid',
          'numeric_request_id', 'wrong_schema', 'wusheng_response', 'A_after_ViewAs', 'A_after_bad_json']
CHECKS = ['preinit', 'idempotent_init', 'same_engine_lua', 'server_info_restore',
          'self_context_cleanup', 'deferred_delete', 'A_B_A', 'invalid_scene_recovery',
          'ViewAs_cleanup', 'bad_json_recovery', 'graceful_shutdown', 'terminal_shutdown']


def load(name: str):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), HERE / name)
    if spec is None or spec.loader is None:
        raise RuntimeError('missing existing harness: ' + name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_native(value: dict) -> None:
    assert type(value.get('schema_version')) is int and value['schema_version'] == 1 and value.get('status') == 'PASS', 'native gate did not pass'
    assert value.get('native_checks') == CHECKS, 'native lifecycle coverage changed'
    calls = value.get('calls', [])
    assert [item['label'] for item in calls] == LABELS, 'native corpus changed or was truncated'
    for item in calls:
        result = json.loads(item['response_utf8'])
        expected_known = item['label'] not in ('invalid_scene', 'numeric_request_id', 'wrong_schema')
        expected_confirm = expected_known and item['label'] != 'B_incomplete'
        assert result['known'] is expected_known and result['can_confirm'] is expected_confirm
        assert result['request_id'] == item['request']['request_id'], 'request identity drift'
        if expected_confirm:
            assert result['wire']['reply_to'] == '18446744073709551615', 'uint64 wire identity drift'
        else:
            assert result['wire'] is None, 'unusable selection published a reply'
    for index in (2, 4, 8, 9):
        assert calls[index]['response_utf8'] == calls[0]['response_utf8'], 'A-B-A/recovery drift'


def verify_browser(value: dict, baseline: dict) -> None:
    verify_native(baseline)
    assert value.get('status') == 'COMPLETE', 'browser failed: ' + str(value.get('error'))
    rounds = value.get('rounds')
    assert isinstance(rounds, list) and len(rounds) == 2, 'missing fresh Worker round'
    for run in rounds:
        assert json.dumps(run['registry'], sort_keys=True) == json.dumps(baseline['registry'], sort_keys=True), 'production registry drift'
        records = run['records']
        assert [item['label'] for item in records] == LABELS, 'missing/reordered/duplicate browser query'
        assert run['events'] == ['ready'] + ['result'] * len(LABELS) + ['disposed'], 'no graceful disposal'
        for actual, expected in zip(records, baseline['calls']):
            assert actual['response_utf8'] == expected['response_utf8'], 'native/Worker bytes differ: ' + actual['label']
    assert 'evaluation failed (2)' in value.get('transportError', ''), 'wrong fatal transport failure'
    assert value.get('recovery') == baseline['calls'][0]['response_utf8'], 'fresh Worker recovery mismatch'


def native_run(runner: Path, assets: Path, artifacts: Path, fixed: bool) -> dict:
    artifacts.mkdir(parents=True, exist_ok=True)
    output = artifacts / 'result.json'
    output.unlink(missing_ok=True)
    env = os.environ.copy()
    if fixed: env['QT_HASH_SEED'] = '0'
    else: env.pop('QT_HASH_SEED', None)
    with tempfile.TemporaryDirectory(prefix='session-', dir=artifacts) as directory:
        root = Path(directory)
        for key, sub in (('HOME', 'home'), ('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                         ('APPDATA', 'appdata'), ('LOCALAPPDATA', 'localappdata')):
            env[key] = str(root / sub)
        child = subprocess.run([str(runner), '--asset-root', str(assets), '--output', str(output)],
                               cwd=root, env=env, capture_output=True, timeout=120, check=False)
    (artifacts / 'stdout.log').write_bytes(child.stdout)
    (artifacts / 'stderr.log').write_bytes(child.stderr)
    assert child.returncode == 0 and output.is_file(), child.stderr.decode('utf-8', 'replace')[-6000:]
    result = json.loads(output.read_bytes())
    verify_native(result)
    return result


class Server:
    """Exact allowlist, loopback only, tokened same-origin single report. No checkout mount."""
    def __init__(self, routes: dict):
        self.routes, self.token = routes, secrets.token_hex(24)
        self.done, self.lock = threading.Event(), threading.Lock()
        self.report, self.requests = None, []
        owner = self
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args): pass
            def reply(self, status, body=b'', mime='text/plain'):
                self.send_response(status)
                for name, value in [('Content-Type', mime), ('Content-Length', str(len(body))),
                                    ('Cache-Control', 'no-store'), ('X-Content-Type-Options', 'nosniff')]:
                    self.send_header(name, value)
                self.end_headers()
                with contextlib.suppress(BrokenPipeError, ConnectionResetError): self.wfile.write(body)
            def do_GET(self):
                key = self.path
                if key == '/index.html?token=' + owner.token: key = '/index.html'
                with owner.lock: owner.requests.append(self.path)
                item = owner.routes.get(key)
                if item is None: return self.reply(404)
                body = item.read_bytes() if isinstance(item, Path) else item
                mime = {'.html': 'text/html', '.mjs': 'text/javascript', '.json': 'application/json',
                        '.wasm': 'application/wasm'}.get(Path(key).suffix, 'application/octet-stream')
                self.reply(200, body, mime)
            def do_POST(self):
                if self.path != '/report/' + owner.token or self.headers.get('Origin') != owner.origin:
                    return self.reply(403)
                try:
                    size = int(self.headers.get('Content-Length', '-1'))
                    if not 0 < size <= 16 * 1024 * 1024 or self.headers.get('Content-Type') != 'application/json':
                        raise ValueError('invalid report')
                    value = json.loads(self.rfile.read(size))
                    if not isinstance(value, dict) or type(value.get('schema_version')) is not int or value['schema_version'] != 1:
                        raise ValueError('invalid report schema')
                except (ValueError, UnicodeError): return self.reply(400)
                with owner.lock:
                    if owner.report is not None: return self.reply(409)
                    owner.report = value
                self.reply(200)
                owner.done.set()
        self.http = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.http.daemon_threads = True
        self.origin = f'http://127.0.0.1:{self.http.server_port}'
        self.thread = threading.Thread(target=self.http.serve_forever, daemon=True)
    def __enter__(self):
        self.thread.start()
        return self
    def __exit__(self, *args):
        self.http.shutdown()
        self.http.server_close()
        self.thread.join(timeout=5)


def browser_run(args, baseline: dict) -> dict:
    module = args.wasm_module
    binary = module.with_suffix('.wasm')
    assert binary.read_bytes()[:8] == b'\0asm\1\0\0\0', 'missing WASM binary'
    requests = [{'label': item['label'], 'request': item['request']} for item in baseline['calls']]
    # Do not serve expected outputs to the browser. It must call the native exports.
    routes = {'/index.html': b'<!doctype html><meta charset="utf-8"><body>Running production session<script type="module" src="/probe.mjs"></script>',
        '/probe.mjs': HERE / 'browser/rules-session-page.mjs', '/rules-worker.mjs': args.worker_script,
        '/input.json': json.dumps(requests).encode(), '/rules/qsanguosha_client_wasm.mjs': module,
        '/rules/qsanguosha_client_wasm.wasm': binary,
        '/rules/qsanguosha_client_wasm.assets.json': args.manifest}
    with Server(routes) as server, tempfile.TemporaryDirectory(prefix='chrome-', dir=args.artifacts) as profile:
        command = [str(args.browser), '--headless=new', '--disable-gpu', '--no-first-run',
            '--no-default-browser-check', '--disable-background-networking', '--disable-extensions',
            '--disable-dev-shm-usage', '--user-data-dir=' + profile, *args.browser_arg,
            server.origin + '/index.html?token=' + server.token]
        with (args.artifacts / 'browser.stdout.log').open('wb') as out, (args.artifacts / 'browser.stderr.log').open('wb') as err:
            process = subprocess.Popen(command, stdout=out, stderr=err, start_new_session=os.name == 'posix')
            try:
                deadline = time.monotonic() + 300
                while not server.done.wait(.1):
                    assert process.poll() is None, 'browser exited before report'
                    assert time.monotonic() < deadline, 'browser timed out'
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
                (args.artifacts / 'http-requests.json').write_text(json.dumps(server.requests))
        assert server.report is not None, 'no browser evidence'
        (args.artifacts / 'browser-report.json').write_text(json.dumps(server.report, ensure_ascii=True))
        verify_browser(server.report, baseline)
        return server.report


class VerifierTests(unittest.TestCase):
    def sample(self):
        # Deliberately synthetic data: these tests exercise only the verifier.
        calls = []
        for label in LABELS:
            known = label not in ('invalid_scene', 'numeric_request_id', 'wrong_schema')
            confirm = known and label != 'B_incomplete'
            result = {'known': known, 'can_confirm': confirm,
                      'request_id': '18446744073709551615',
                      'wire': {'reply_to': '18446744073709551615'} if confirm else None}
            calls.append({'label': label, 'request': {'request_id': '18446744073709551615'},
                          'response_utf8': json.dumps(result)})
        baseline = {'schema_version': 1, 'status': 'PASS', 'native_checks': CHECKS,
                    'registry': {'card_count': 1}, 'calls': calls}
        run = {'registry': baseline['registry'],
               'records': [{'label': item['label'], 'response_utf8': item['response_utf8']} for item in calls],
               'events': ['ready'] + ['result'] * len(LABELS) + ['disposed']}
        report = {'status': 'COMPLETE', 'rounds': [copy.deepcopy(run), copy.deepcopy(run)],
                  'transportError': 'WASM client evaluation failed (2)',
                  'recovery': calls[0]['response_utf8']}
        return baseline, report
    def test_valid_report(self):
        baseline, report = self.sample()
        verify_browser(report, baseline)
    def test_reject_registry_type_drift(self):
        baseline, report = self.sample()
        report['rounds'][0]['registry']['card_count'] = True
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_reject_output_normalization(self):
        baseline, report = self.sample()
        report['rounds'][0]['records'][0]['response_utf8'] += '\n'
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_reject_duplicate_query(self):
        baseline, report = self.sample()
        report['rounds'][0]['records'][1] = report['rounds'][0]['records'][0]
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_reject_missing_dispose(self):
        baseline, report = self.sample()
        report['rounds'][0]['events'].pop()
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_reject_wrong_error(self):
        baseline, report = self.sample()
        report['transportError'] = 'unrelated initialization failure'
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_reject_stale_recovery(self):
        baseline, report = self.sample()
        report['recovery'] = report['rounds'][0]['records'][1]['response_utf8']
        with self.assertRaises(AssertionError): verify_browser(report, baseline)
    def test_reject_integer_boolean(self):
        baseline, report = self.sample()
        value = json.loads(baseline['calls'][0]['response_utf8'])
        value['known'] = 1
        baseline['calls'][0]['response_utf8'] = json.dumps(value)
        with self.assertRaises(AssertionError): verify_native(baseline)
    def test_reject_missing_native_checks(self):
        with self.assertRaises(AssertionError): verify_native({'schema_version': 1, 'status': 'PASS'})
    def test_reject_browser_complete_only(self):
        with self.assertRaises(AssertionError): verify_browser({'status': 'COMPLETE'}, {})
    def test_server_allowlist_and_report(self):
        import urllib.request
        import urllib.error
        with Server({'/allowed.json': b'{}'}) as server:
            self.assertEqual(urllib.request.urlopen(server.origin + '/allowed.json').read(), b'{}')
            with self.assertRaises(urllib.error.HTTPError): urllib.request.urlopen(server.origin + '/etc/passwd')
            data = b'{"schema_version":1,"status":"COMPLETE"}'
            url = server.origin + '/report/' + server.token
            with self.assertRaises(urllib.error.HTTPError): urllib.request.urlopen(urllib.request.Request(url, data=data))
            headers = {'Origin': server.origin, 'Content-Type': 'application/json'}
            self.assertEqual(urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers)).status, 200)
            self.assertTrue(server.done.wait(1))
            with self.assertRaises(urllib.error.HTTPError): urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers))


def main() -> int:
    if not __debug__:
        raise RuntimeError('Validation must not run with Python -O/PYTHONOPTIMIZE')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--native-only', action='store_true')
    for key in ('native-runner', 'asset-root', 'artifacts', 'wasm-module', 'manifest', 'worker-script', 'browser'):
        parser.add_argument('--' + key, type=Path)
    parser.add_argument('--browser-arg', action='append', default=[])
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(VerifierTests)).wasSuccessful() else 1
    required = ['native_runner', 'asset_root', 'artifacts']
    if not args.native_only: required += ['wasm_module', 'manifest', 'worker_script', 'browser']
    for name in required:
        if getattr(args, name) is None: parser.error('--' + name.replace('_', '-') + ' is required')
        setattr(args, name, getattr(args, name).resolve())
    args.artifacts.mkdir(parents=True, exist_ok=True)
    summary_path = args.artifacts / 'session-summary.json'
    summary_path.write_text('{"schema_version":1,"status":"NOT_RUN"}\n')
    try:
        with tempfile.TemporaryDirectory(prefix='assets-', dir=args.artifacts) as directory:
            assets = Path(directory)
            load('check-fixtures.py').stage_builtin_assets(args.asset_root, assets)
            if not args.native_only:
                load('check-wasm-fixtures.py').check_assets(assets, json.loads(args.manifest.read_bytes()))
            baseline = native_run(args.native_runner, assets, args.artifacts / 'native-first', True)
            second = native_run(args.native_runner, assets, args.artifacts / 'native-second', False)
            assert (args.artifacts / 'native-first/result.json').read_bytes() == (args.artifacts / 'native-second/result.json').read_bytes(), 'fresh-process/hash-seed production output drift'
            if not args.native_only: browser_run(args, baseline)
        summary = {'schema_version': 1, 'status': 'PASS', 'scope': 'production-session',
                   'native_processes': 2, 'queries_per_process': len(LABELS),
                   'browser': 'NOT_RUN' if args.native_only else 'PASS'}
        for name in ['native_runner'] + ([] if args.native_only else ['wasm_module', 'manifest', 'worker_script']):
            summary[name + '_sha256'] = hashlib.sha256(getattr(args, name).read_bytes()).hexdigest()
        if not args.native_only:
            summary['wasm_sha256'] = hashlib.sha256(args.wasm_module.with_suffix('.wasm').read_bytes()).hexdigest()
    except (AssertionError, OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
        summary_path.write_text(json.dumps({'schema_version': 1, 'status': 'FAIL', 'error': str(error)}))
        print(f'RULES_SESSION status=FAIL detail={error}', file=sys.stderr)
        return 1
    summary_path.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
