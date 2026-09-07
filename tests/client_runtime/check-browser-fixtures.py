#!/usr/bin/env python3
"""Real Chromium Dedicated Worker/native fixture parity, with stdlib-only hosting."""
from __future__ import annotations

import argparse
import contextlib
import http.server
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from typing import Any

HERE = Path(__file__).resolve().parent
STALLED = object()  # Intentional transport fault, never a substitute rule implementation.
MAX_REPORT = 16 * 1024 * 1024


def parity_tools():
    spec = importlib.util.spec_from_file_location('qsan_wasm_parity', HERE / 'check-wasm-fixtures.py')
    if spec is None or spec.loader is None:
        raise RuntimeError('cannot load existing WASM parity tools')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProbeServer:
    """Serve an exact file allowlist on loopback, with a fresh unguessable run path."""
    def __init__(self, routes: dict[str, Any]):
        self.routes = routes
        self.prefix = '/' + secrets.token_hex(24)
        self.done = threading.Event()
        self.stopping = threading.Event()
        self.lock = threading.Lock()
        self.report: dict[str, Any] | None = None
        self.requests: list[str] = []
        probe = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, code, body=b'', mime='text/plain'):
                self.send_response(code)
                self.send_header('Content-Type', mime)
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.end_headers()
                with contextlib.suppress(BrokenPipeError, ConnectionResetError):
                    self.wfile.write(body)

            def do_GET(self):
                with probe.lock:
                    probe.requests.append(self.path)
                key = self.path[len(probe.prefix):] if self.path.startswith(probe.prefix + '/') else ''
                item = probe.routes.get(key)
                if item is STALLED:
                    probe.stopping.wait(30)
                    self.reply(503)
                    return
                if item is None:
                    self.reply(404)
                    return
                try:
                    body = item.read_bytes() if isinstance(item, Path) else item
                    mime = {'.mjs': 'text/javascript', '.json': 'application/json',
                            '.html': 'text/html', '.wasm': 'application/wasm'}.get(Path(key).suffix,
                                                                                 'application/octet-stream')
                    self.reply(200, body, mime)
                except OSError:
                    self.reply(500)

            def do_POST(self):
                if self.path != probe.prefix + '/report' or self.headers.get('Origin') != probe.origin:
                    self.reply(403)
                    return
                try:
                    length = int(self.headers.get('Content-Length', '-1'))
                    if not 0 < length <= MAX_REPORT:
                        raise ValueError('report size')
                    if self.headers.get('Content-Type') != 'application/json':
                        raise ValueError('report type')
                    data = json.loads(self.rfile.read(length))
                    if not isinstance(data, dict) or type(data.get('schema_version')) is not int \
                            or data['schema_version'] != 1:
                        raise ValueError('report schema')
                except (ValueError, UnicodeError):
                    self.reply(400)
                    return
                with probe.lock:
                    if probe.report is not None:
                        self.reply(409)
                        return
                    probe.report = data
                self.reply(200, b'OK')
                probe.done.set()

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.server.daemon_threads = True
        self.origin = f'http://127.0.0.1:{self.server.server_port}'
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.stopping.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)

    @property
    def url(self):
        return self.origin + self.prefix


def browser_report(browser: Path, routes: dict[str, Any], artifacts: Path,
                   timeout: int, browser_args: list[str]) -> dict[str, Any]:
    artifacts.mkdir(parents=True, exist_ok=True)
    with ProbeServer(routes) as server, tempfile.TemporaryDirectory(prefix='browser-', dir=artifacts) as profile:
        command = [str(browser), '--headless=new', '--disable-gpu', '--no-first-run',
                   '--no-default-browser-check', '--disable-background-networking', '--disable-extensions',
                   '--disable-dev-shm-usage', '--enable-logging=stderr', '--user-data-dir=' + profile, *browser_args,
                   server.url + '/browser/index.html']
        with (artifacts / 'browser.stdout.log').open('wb') as stdout, \
                (artifacts / 'browser.stderr.log').open('wb') as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=os.name == 'posix')
            try:
                deadline = time.monotonic() + timeout
                while not server.done.wait(0.1):
                    if process.poll() is not None:
                        raise AssertionError(f'browser exited {process.returncode} before publishing evidence')
                    if time.monotonic() >= deadline:
                        raise AssertionError('browser probe timed out; no successful report')
            finally:
                # Reap browser children as well as the launcher on Linux.
                if os.name == 'posix':
                    with contextlib.suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGTERM)
                elif process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    if os.name == 'posix':
                        with contextlib.suppress(ProcessLookupError):
                            os.killpg(process.pid, signal.SIGKILL)
                    else:
                        process.kill()
                    process.wait(timeout=5)
                (artifacts / 'http-requests.json').write_text(json.dumps(server.requests, indent=2), encoding='utf-8')
        if server.report is None:
            raise AssertionError('browser returned no report')
        return server.report


def check_records(report: dict[str, Any], cases: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if report.get('status') != 'COMPLETE':
        raise AssertionError('browser execution failed: ' + str(report.get('error', report))[:6000])
    records = report.get('records')
    if not isinstance(records, list) or not all(isinstance(item, dict) for item in records) \
            or [item.get('tag') for item in records] != [item['tag'] for item in cases]:
        raise AssertionError('browser omitted, duplicated or reordered cases')
    for case, record in zip(cases, records):
        diagnostic = case.get('diagnostic')
        if diagnostic is not None:
            if record.get('outcome') != 'error' or 'output' in record \
                    or not isinstance(record.get('error'), str) or diagnostic not in record['error']:
                raise AssertionError(f"{case['tag']}: failed for wrong reason or returned output: {record}")
        else:
            if record.get('outcome') != 'result' or not isinstance(record.get('output'), str):
                raise AssertionError(f"{case['tag']}: no result: {record}")
            env = record.get('environment')
            if not isinstance(env, dict) or env.get('dedicatedWorker') is not True \
                    or env.get('documentAbsent') is not True \
                    or env.get('timerBridge') != ['clearTimeout', 'setTimeout']:
                raise AssertionError('missing real Dedicated Worker environment evidence')
    return records


def check(args: argparse.Namespace) -> dict[str, Any]:
    tools = parity_tools()
    native = tools.native_harness()
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    tools.validate_manifest(manifest)
    binary = args.wasm_module.with_suffix('.wasm')
    with binary.open('rb') as stream:
        if stream.read(8) != b'\x00asm\x01\x00\x00\x00':
            raise AssertionError('missing/invalid WebAssembly binary')
    native_dir = args.artifacts / 'native'
    with tempfile.TemporaryDirectory(prefix='browser-assets-', dir=args.artifacts) as scratch:
        native.stage_builtin_assets(args.asset_root, Path(scratch))
        tools.check_assets(Path(scratch), manifest)
        native.check(argparse.Namespace(runner=args.native_runner, fixtures=args.fixtures,
                                       asset_root=Path(scratch), artifacts=native_dir))
    fixtures = sorted(args.fixtures.glob('*.json'))
    if not fixtures:
        raise AssertionError('no fixture files')
    cases = []
    expected = {}
    routes = {
        '/browser/index.html': b'<!doctype html><meta charset="utf-8"><title>Rules Worker probe</title>'
                               b'<body>Running fixture probe<script type="module" src="probe-page.mjs"></script>',
        '/wasm-fixture-host.mjs': HERE / 'wasm-fixture-host.mjs',
        '/browser/qsanguosha_rules_fixture_worker.mjs': args.wasm_module,
        '/browser/qsanguosha_rules_fixture_worker.wasm': binary,
        '/browser/qsanguosha_rules_fixture_worker.assets.json': args.manifest,
    }
    for name in ('probe-page.mjs', 'fixture-worker.mjs', 'fixture-worker-client.mjs'):
        routes['/browser/' + name] = HERE / 'browser' / name
    for path in fixtures:
        source = json.loads(path.read_bytes())
        baseline = (native_dir / f'{path.stem}.first.json').read_bytes()
        for iteration in ('first', 'second'):
            tag = path.stem + '.' + iteration
            case = {'tag': tag, 'fixture': '../fixtures/' + path.name}
            if iteration == 'first':
                case['hashSeed'] = '0'
            cases.append(case)
            expected[tag] = (source, baseline)
        routes['/fixtures/' + path.name] = path
    invalid = native_dir / 'invalid'
    if {path.stem for path in invalid.glob('*.json')} != set(tools.NEGATIVE):
        raise AssertionError('native negative corpus changed; audit browser diagnostic coverage')
    for name, diagnostic in tools.NEGATIVE.items():
        routes['/invalid/' + name + '.json'] = invalid / (name + '.json')
        cases.append({'tag': 'negative-' + name, 'fixture': '../invalid/' + name + '.json',
                      'diagnostic': diagnostic, 'hashSeed': '0'})
    first = fixtures[0]
    fixture_url = '../fixtures/' + first.name
    for fault in ('missing', 'corrupt', 'stall'):
        routes[f'/browser/{fault}/fixture-worker.mjs'] = HERE / 'browser/fixture-worker.mjs'
    routes['/browser/wasm-fixture-host.mjs'] = HERE / 'wasm-fixture-host.mjs'
    routes['/browser/corrupt/qsanguosha_rules_fixture_worker.wasm'] = b'not-wasm'
    routes['/browser/stall/qsanguosha_rules_fixture_worker.wasm'] = STALLED
    for tag, diagnostic, extra in (
        ('missing-binary', 'HTTP 404', {'fault': 'missing'}),
        ('corrupt-binary', 'invalid WebAssembly binary', {'fault': 'corrupt'}),
        ('manifest-mismatch', 'differs from its sidecar', {'operation': 'manifest-mismatch'}),
        ('timeout', 'timed out', {'fault': 'stall', 'timeoutMs': 1500}),
        ('cancel', 'cancelled', {'operation': 'cancel'}),
    ):
        cases.append({'tag': tag, 'fixture': fixture_url, 'diagnostic': diagnostic, **extra})
    cases.append({'tag': 'recovery', 'fixture': fixture_url, 'hashSeed': '0'})
    expected['recovery'] = (json.loads(first.read_bytes()), (native_dir / f'{first.stem}.first.json').read_bytes())
    cases.append({'tag': 'disposed', 'fixture': fixture_url, 'operation': 'disposed', 'diagnostic': 'disposed'})
    routes['/browser/config.json'] = tools.json_bytes({'cases': cases})
    report = browser_report(args.browser, routes, args.artifacts / 'browser', args.timeout, args.browser_arg)
    (args.artifacts / 'browser/report.json').write_bytes(tools.json_bytes(report))
    records = check_records(report, cases)
    outputs = {}
    for record in records:
        if record['tag'] not in expected:
            continue
        source, baseline = expected[record['tag']]
        actual = record['output'].encode('utf-8')
        (args.artifacts / 'browser' / (record['tag'] + '.json')).write_bytes(actual)
        native.verify_result(source, json.loads(actual))
        tools.compare_bytes(baseline, actual, record['tag'])
        outputs[record['tag']] = tools.digest(actual)
    return {'schema_version': 1, 'status': 'PASS', 'scope': 'builtin-chromium-dedicated-worker',
            'fixtures': len(fixtures), 'queries': sum(len(json.loads(path.read_bytes())['queries']) for path in fixtures),
            'worker_positive_runs': len(expected), 'native_positive_runs': len(fixtures) * 2,
            'negative_cases_per_backend': len(tools.NEGATIVE), 'worker_host_faults': 6,
            'browser': report.get('userAgent'), 'outputs': outputs,
            'native_runner_sha256': tools.digest(args.native_runner.read_bytes()),
            'wasm_module_sha256': tools.digest(args.wasm_module.read_bytes()),
            'wasm_binary_sha256': tools.digest(binary.read_bytes()),
            'asset_manifest_sha256': tools.digest(manifest_bytes)}


class HarnessTests(unittest.TestCase):
    def test_exact_routes_no_directory_or_traversal(self):
        with ProbeServer({'/file.mjs': b'export default 1;'}) as server:
            self.assertEqual(urllib.request.urlopen(server.url + '/file.mjs').read(), b'export default 1;')
            for path in ('/../file.mjs', '/', '/file.mjs?other=1'):
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    urllib.request.urlopen(server.url + path)
                self.assertEqual(caught.exception.code, 404)
            with self.assertRaises(urllib.error.HTTPError):
                urllib.request.urlopen(server.origin + '/file.mjs')

    def test_report_origin_and_single_completion(self):
        with ProbeServer({}) as server:
            data = b'{"schema_version":1,"status":"COMPLETE"}'
            request = urllib.request.Request(server.url + '/report', data=data,
                                             headers={'Content-Type': 'application/json'})
            with self.assertRaises(urllib.error.HTTPError) as caught:
                urllib.request.urlopen(request)
            self.assertEqual(caught.exception.code, 403)
            request.add_header('Origin', server.origin)
            self.assertEqual(urllib.request.urlopen(request).status, 200)
            self.assertTrue(server.done.wait(2))
            with self.assertRaises(urllib.error.HTTPError) as caught:
                urllib.request.urlopen(request)
            self.assertEqual(caught.exception.code, 409)

    def test_incomplete_report_fails(self):
        with self.assertRaises(AssertionError):
            check_records({'status': 'COMPLETE', 'records': []}, [{'tag': 'one'}])

    def test_wrong_negative_diagnostic_fails(self):
        with self.assertRaises(AssertionError):
            check_records({'status': 'COMPLETE', 'records': [
                {'tag': 'one', 'outcome': 'error', 'error': 'runtime trap'}]},
                [{'tag': 'one', 'diagnostic': 'request_id'}])

    def test_error_cannot_publish_output(self):
        with self.assertRaises(AssertionError):
            check_records({'status': 'COMPLETE', 'records': [
                {'tag': 'one', 'outcome': 'error', 'error': 'request_id', 'output': '{}'}]},
                [{'tag': 'one', 'diagnostic': 'request_id'}])

    def test_environment_required(self):
        with self.assertRaises(AssertionError):
            check_records({'status': 'COMPLETE', 'records': [
                {'tag': 'one', 'outcome': 'result', 'output': '{}'}]}, [{'tag': 'one'}])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    for name in ('native-runner', 'wasm-module', 'manifest', 'fixtures', 'asset-root', 'artifacts'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--browser', default=shutil.which('google-chrome') or shutil.which('chromium'))
    parser.add_argument('--browser-arg', action='append', default=[])
    parser.add_argument('--timeout', type=int, default=360)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(HarnessTests)).wasSuccessful() else 1
    for name in ('native_runner', 'wasm_module', 'manifest', 'fixtures', 'asset_root', 'artifacts'):
        if getattr(args, name) is None:
            parser.error('--' + name.replace('_', '-') + ' is required')
        setattr(args, name, getattr(args, name).resolve())
    if not args.browser or not 1 <= args.timeout <= 1800:
        parser.error('a Chromium executable and timeout between 1 and 1800 seconds are required')
    args.browser = Path(args.browser).resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    summary = {'schema_version': 1, 'status': 'NOT_RUN'}
    summary_path = args.artifacts / 'browser-parity-summary.json'
    summary_path.write_text(json.dumps(summary), encoding='utf-8')
    try:
        summary = check(args)
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        summary = {'schema_version': 1, 'status': 'FAIL', 'detail': str(error)}
    summary_path.write_text(json.dumps(summary, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    print('[AUTOTEST] BROWSER_RULES_FIXTURES_RESULT status=' + summary['status'])
    if summary['status'] != 'PASS':
        print(summary.get('detail', 'browser parity not run'), file=sys.stderr)
    return 0 if summary['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
