"""Run one bridge game with a real isolated Excel parent; not VBA UI acceptance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import socket
import time
import urllib.request
import uuid

import psutil
import win32com.client
import win32process


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    nonce = str(uuid.uuid4())
    private = Path(os.environ['TEMP']) / 'QSanguoshaExcel' / nonce
    private.mkdir(parents=True)
    (private / 'data').mkdir()
    (private / 'config.ini').write_text('[General]\nMasterVolume=0\n', encoding='utf-8')
    bootstrap = private / 'bootstrap.json'
    env = {key.upper(): value for key, value in os.environ.items()}
    fmod_root = root / 'TODO/anime'
    if not (fmod_root / 'fmodexL64.dll').is_file():
        raise RuntimeError('matching Debug FMOD runtime is missing')
    env['PATH'] = 'H:/Qt6111/6.11.1/msvc2022_64/bin;' + str(fmod_root) + ';' + env.get('PATH', '')
    env['QT_PLUGIN_PATH'] = 'H:/Qt6111/6.11.1/msvc2022_64/plugins'
    env['QSAN_SESSION_SETTINGS'] = str(private / 'config.ini')
    env['QSAN_USER_DATA_ROOT'] = str(private / 'data')
    existing = {p.pid for p in psutil.process_iter(['name']) if (p.info['name'] or '').lower() == 'excel.exe'}
    excel = None
    process = None
    ready = None
    owned_excel = False
    report = {'driver': 'Python HTTP / real Excel parent', 'vba_ui': 'not-tested', 'mode': '05p', 'game_over': False}
    native_log = (output / 'bridge.log').open('wb')
    transcript = (output / 'ipc.jsonl').open('w', encoding='utf-8')
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    sequence = '0'
    snapshot = {'generation': '0', 'revision': '0'}
    command_id = 0

    def http(path, body=None):
        # Credentials stay in memory/private bootstrap, never in the transcript.
        request = urllib.request.Request('http://127.0.0.1:%s%s' % (ready['port'], path),
            data=None if body is None else json.dumps(body).encode('utf-8'),
            headers={'Authorization': 'Bearer ' + ready['token'], 'X-QSan-Session': ready['session'], 'Content-Type': 'application/json'})
        with opener.open(request, timeout=10) as response:
            return json.load(response)

    def command(name, command_args):
        nonlocal command_id
        command_id += 1
        body = dict(api_version=1, session=ready['session'], id=str(command_id),
            generation=snapshot['generation'], revision=snapshot['revision'], name=name, args=command_args)
        result = http('/v1/commands', body)
        transcript.write(json.dumps({'command': name, 'args': command_args, 'result': result}, ensure_ascii=False) + '\n')
        transcript.flush()
        return result

    try:
        excel = win32com.client.DispatchEx('Excel.Application')
        _, excel_pid = win32process.GetWindowThreadProcessId(excel.Hwnd)
        if excel_pid in existing:
            raise RuntimeError('Excel automation did not create an isolated process')
        owned_excel = True
        excel.Visible = False
        report.update(excel_pid=excel_pid, excel_created=psutil.Process(excel_pid).create_time(), excel_version=excel.Version)
        executable = root / 'excel-debug/QSanguoshaExcelBridge.exe'
        report['bridge_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
        report['helper_sha256'] = hashlib.sha256((root / 'excel-debug/QSanguoshaExcelServer.exe').read_bytes()).hexdigest()
        process = subprocess.Popen([str(executable), '--bootstrap', str(bootstrap), '--nonce', nonce,
            '--parent-pid', str(excel_pid), '--asset-root', str(root)], cwd=private, env=env,
            stdout=native_log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        report['bridge_pid'] = process.pid
        print('START Excel=%s bridge=%s' % (excel_pid, process.pid), flush=True)
        deadline = time.monotonic() + 180
        while not bootstrap.exists():
            if process.poll() is not None:
                raise RuntimeError('bridge exited before ready: %s' % process.returncode)
            if time.monotonic() > deadline:
                raise TimeoutError('bridge startup timeout')
            time.sleep(.25)
        ready = json.loads(bootstrap.read_text(encoding='utf-8'))
        if ready.get('status') != 'ready':
            raise RuntimeError('bridge startup: ' + str(ready.get('error')))
        print('BRIDGE_READY', flush=True)
        catalog = command('catalog', {})
        (output / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False), encoding='utf-8')
        host = command('host', {'private': True, 'name': 'Excel QA', 'avatar': 'caocao', 'robots': 0,
            'settings': {'ServerName': 'Excel QA 05P', 'GameMode': '05p', 'OperationTimeout': 3, 'OperationNoLimit': False,
                'CountDownSeconds': 0, 'OriginAIDelay': 20, 'AIHumanized': False,
                'AIChat': False, 'EnableLuckCard': False, 'serverconfig/upnp': False}})
        if not host.get('ok'):
            raise RuntimeError('host rejected: ' + str(host.get('error')))
        deadline = time.monotonic() + args.timeout
        trusted = False
        selected = set()
        next_progress = 0
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('bridge exited during game: %s' % process.returncode)
            update = http('/v1/updates?after=' + sequence)
            sequence = update['sequence']
            snapshot = update['snapshot']
            transcript.write(json.dumps({'update': update}, ensure_ascii=False) + '\n')
            transcript.flush()
            view = snapshot.get('view', {})
            interaction = snapshot.get('interaction', {})
            request_id = interaction.get('request_id', '0')
            if request_id != '0' and request_id not in selected:
                options = interaction.get('ui', {}).get('options', [])
                enabled = [row for row in options if row.get('enabled', True)]
                if enabled:
                    selection_args = {'request_id': request_id, 'draft': {'option': enabled[0]['id']}}
                    preflight = command('select', selection_args)
                    if preflight.get('result', {}).get('selection', {}).get('can_confirm'):
                        submitted = command('submit', selection_args)
                        if submitted.get('ok'):
                            selected.add(request_id)
                            report['submitted_options'] = len(selected)
            if not trusted and snapshot.get('connection') == 'active':
                trusted = bool(command('trust', {'enabled': True}).get('ok'))
                report['trust_submitted'] = trusted
            game = snapshot.get('state', {}).get('game', {})
            if game.get('started') or game.get('game_started') or view.get('status') in ('playing', 'started'):
                report['game_started'] = True
            if view.get('game_over'):
                report['game_over'] = True
                report['final_game'] = game
                (output / 'final-snapshot.json').write_text(json.dumps(snapshot, ensure_ascii=False, indent=2), encoding='utf-8')
                print('GAME_OVER ' + json.dumps(game, ensure_ascii=False), flush=True)
                break
            for event in update.get('events', []):
                if event.get('kind') == 'error':
                    print('BRIDGE_ERROR ' + json.dumps(event['data']), flush=True)
                    raise RuntimeError('bridge error: ' + str(event['data'].get('code')))
            if time.monotonic() >= next_progress:
                print('PROGRESS ' + json.dumps({'connection': snapshot.get('connection'), 'status': view.get('status'),
                    'request': request_id, 'players': len(view.get('players', [])), 'game': game}, ensure_ascii=False), flush=True)
                next_progress = time.monotonic() + 20
            time.sleep(.5)
        if not report['game_over']:
            raise TimeoutError('game did not reach GAME_OVER')
    except Exception as error:
        report['error'] = str(error)
        print('FAILED ' + str(error), flush=True)
    finally:
        owned_children = []
        if process is not None and process.poll() is None:
            owned_children = psutil.Process(process.pid).children(recursive=True)
            try:
                if ready and ready.get('status') == 'ready':
                    report['shutdown_ack'] = http('/v1/shutdown', {}).get('ok')
                process.wait(timeout=45)
            except Exception as error:
                report['shutdown_error'] = str(error)
                process.kill()
                process.wait(timeout=10)
            report['bridge_exit'] = process.returncode
        elif process is not None:
            report['bridge_exit'] = process.returncode
        report['owned_children'] = [p.pid for p in owned_children]
        report['remaining_children'] = [p.pid for p in owned_children if p.is_running()]
        # Emergency cleanup is recorded as failure, never graceful acceptance.
        for child in owned_children:
            if child.is_running():
                child.kill()
        if owned_excel:
            for attempt in range(10):
                try:
                    excel.Quit()
                    break
                except Exception as error:
                    report['excel_quit_error'] = str(error)
                    time.sleep(.5)
            else:
                owned = psutil.Process(excel_pid)
                if owned.create_time() == report['excel_created']:
                    owned.terminate()
                    owned.wait(10)
                    report['excel_forced_stop'] = True
        excel = None
        logs = private / 'data/server/logs'
        if logs.is_dir():
            target = output / 'helper-logs'
            target.mkdir(exist_ok=True)
            for log in logs.iterdir():
                if log.is_file() and log.suffix.lower() in ('.log', '.jsonl', '.txt'):
                    shutil.copy2(log, target / log.name)
        if ready and ready.get('port'):
            try:
                with socket.create_connection(('127.0.0.1', ready['port']), timeout=1):
                    report['ipc_port_released'] = False
            except OSError:
                report['ipc_port_released'] = True
        report['bootstrap_removed'] = not bootstrap.exists()
        report['passed'] = bool(report['game_over'] and report.get('bridge_exit') == 0 and not report['remaining_children'] and not report.get('excel_forced_stop'))
        (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        native_log.close()
        transcript.close()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
