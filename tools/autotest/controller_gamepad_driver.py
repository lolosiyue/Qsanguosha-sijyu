#!/usr/bin/env python3
"""Bounded SDL virtual-gamepad driver; never writes a game protocol reply.

The running GUI must opt in with --controller-virtual-input and --controller-trace.
Only physical button/axis/device JSON is accepted by that local diagnostic channel.
The policy reads the same authorized GUI model exposed to the controller user.
This verifies virtual input + the actual GUI, not physical controller hardware.
"""
import argparse
import collections
import json
import socket
import time
from pathlib import Path


class Driver:
    def __init__(self, path, trace):
        self.channel = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.channel.settimeout(5)
        self.channel.connect(path)
        self.replies = self.channel.makefile('rb')
        self.trace = Path(trace)
        self.events = []
        self.offset = 0
        self.observation = {}

    def inject(self, physical):
        self.channel.sendall((json.dumps(physical) + '\n').encode())
        reply = json.loads(self.replies.readline())
        if reply != {'ok': True}:
            raise RuntimeError(f'Physical input rejected: {physical}: {reply}')

    def press(self, button, hold=0.065):
        self.inject({'button': button, 'down': True})
        time.sleep(hold)
        self.inject({'button': button, 'down': False})
        time.sleep(0.14)
        self.read()

    def read(self):
        with self.trace.open(encoding='utf-8') as stream:
            stream.seek(self.offset)
            while True:
                start = stream.tell()
                line = stream.readline()
                if not line or not line.endswith('\n'):
                    self.offset = start
                    break
                event = json.loads(line)
                self.events.append(event)
                if event['kind'] == 'observation':
                    self.observation = event
                if event['kind'] in ('forbidden-input', 'diagnostic-error'):
                    raise RuntimeError(f"{event['kind']}: {event}")
        if self.observation.get('unsupported_controller_interaction'):
            raise RuntimeError('Opaque/invalid custom interaction is outside coverage')
        return self.observation

    def table(self, kind, identifier):
        """Navigate by physical shoulders/D-pad, then activate by physical South."""
        for _ in range(12):
            current = self.read().get('table_focus', '')
            if current.startswith(kind + ':'):
                break
            self.press('rightshoulder')
        else:
            raise RuntimeError(f'Cannot reach table group {kind}')
        for _ in range(1002):
            if self.read().get('table_focus') == kind + ':' + str(identifier):
                self.press('south')
                return
            self.press('dpright')
        raise RuntimeError(f'Cannot reach table entry {kind}:{identifier}')

    def widget(self, predicate):
        for _ in range(160):
            controls = self.read().get('controls', [])
            if any(c.get('focused') and predicate(c) for c in controls):
                self.press('south')
                return
            self.press('rightshoulder')
        raise RuntimeError('Cannot reach required native widget through focus traversal')

    def play_basic(self, seconds):
        """Finite basic-room policy, intended for 02p standard packages.

        All human-side replies must still pass native UI/Core/protocol validation.
        The opponent may be a normal in-process Lua robot. No trustee is allowed
        for the GUI-controlled seat. Complex fixtures need their own physical
        scripts; the policy fails rather than silently generating a reply.
        """
        deadline = time.monotonic() + seconds
        attempts = collections.Counter()
        while time.monotonic() < deadline:
            o = self.read()
            if o.get('title') in ('Network error', 'Connection failed'):
                raise RuntimeError('GUI connection failed; wait for server Listening before launching the GUI')
            if any(e['kind'] in ('game-over', 'game-standoff') for e in self.events):
                # Observe the native result dialog as well as the notification.
                time.sleep(0.4)
                self.read()
                return
            scope = o.get('scope', '')
            controls = o.get('controls', [])
            if scope == 'controllerGameMenu':
                self.widget(lambda c: c.get('id') == 'controllerStartGame')
                continue
            if controls and any(c.get('class') == 'OptionButton' for c in controls):
                self.widget(lambda c: c.get('class') == 'OptionButton')
                continue
            if controls and scope not in ('MainWindow', ''):
                # Standard native choices use their keyboard activation path.
                if any(c.get('class') == 'QPushButton' for c in controls):
                    self.widget(lambda c: c.get('class') == 'QPushButton')
                    continue
            model = o.get('actions', {})
            request = model.get('request', {})
            request_id = model.get('request_id', '0')
            if request_id == '0':
                # Normal waiting room: Start is an explicit menu command.
                if not any(e['kind'] == 'game-start' for e in self.events):
                    self.press('start')
                else:
                    time.sleep(0.12)
                continue
            attempts[request_id] += 1
            if attempts[request_id] > 180:
                raise RuntimeError(f'No progress for request {request_id}: {model}')
            cards = [c for c in model.get('cards', []) if c.get('enabled')]
            players = [p for p in model.get('players', []) if p.get('enabled')]
            options = [a for a in model.get('actions', []) if a.get('enabled')]
            context = model.get('action_context', '')
            if model.get('can_confirm'):
                self.press('west')
            elif model.get('can_finish'):
                # Try each available play once before ending this phase.
                index = attempts[request_id] - 1
                if index < len(cards):
                    self.table('card', cards[index]['id'])
                    latest = self.read().get('actions', {})
                    targets = [p for p in latest.get('players', []) if p.get('enabled')]
                    if targets and not latest.get('can_confirm'):
                        self.table('player', targets[0]['id'])
                    if self.read().get('actions', {}).get('can_confirm'):
                        self.press('west')
                    else:
                        self.table('card', cards[index]['id'])
                else:
                    self.table('command', 'finish')
            elif model.get('can_cancel'):
                self.press('east')
            elif options:
                self.table('option', options[0]['id'])
            elif cards:
                unselected = [c for c in cards if not c.get('selected')]
                if not unselected:
                    raise RuntimeError(f'All cards selected but mandatory confirm unavailable: {request}')
                self.table('card', unselected[0]['id'])
            elif players:
                self.table('player', players[0]['id'])
            elif not model.get('supported'):
                # The native one-dimensional AG/trigger/ChooseCard path uses
                # Right to establish a cursor, then South to native Return.
                self.press('dpright')
                self.press('south')
            else:
                raise RuntimeError(f'Mandatory request has no reachable action: {request}')
        raise RuntimeError('Bounded run reached its deadline before native game over')


def evidence(events, server_trace, require_game):
    kinds = collections.Counter(e['kind'] for e in events)
    server = []
    if server_trace and Path(server_trace).is_file():
        server = [json.loads(line) for line in Path(server_trace).read_text().splitlines() if line]
    failures = []
    if server_trace and not Path(server_trace).is_file():
        failures.append('server evidence file was not created')
    for forbidden in ('forbidden-input', 'diagnostic-error', 'core-rejected'):
        if kinds[forbidden]:
            failures.append(forbidden)
    if not kinds['hardware'] or not kinds['action']:
        failures.append('no hardware-to-semantic evidence')
    replies = [e for e in events if e['kind'] == 'wire-reply']
    accepted = [e for e in server if e['kind'] == 'server-envelope-validated' and e.get('accepted')]
    for reply in replies:
        if server_trace and not any(a.get('message_id') == reply['message_id']
                                   and a.get('reply_to') == reply['reply_to']
                                   and a.get('command') == reply['command'] for a in accepted):
            failures.append('uncorrelated server reply ' + reply['message_id'])
        if require_game and not any(e['kind'] == 'hardware' and e.get('value') == 1
                                    and e.get('request_id') == reply['reply_to']
                                    and 0 <= int(reply['time_ms']) - int(e['time_ms']) < 1500 for e in events):
            failures.append('reply without preceding physical input ' + reply['message_id'])
    if require_game:
        for kind in ('game-start', 'core-accepted', 'wire-reply', 'game-over'):
            if not kinds[kind]:
                failures.append('missing ' + kind)
        human_players = {e.get('player') for e in accepted}
        if not server_trace or not any(e['kind'] == 'native-card-resolution' and e.get('accepted')
                                       and e.get('player') in human_players for e in server):
            failures.append('missing human native card resolution')
        if kinds['core-accepted'] != len(replies) or kinds['request-start'] != len(replies):
            failures.append('human request/Core/wire counts do not match')
        if len({e['reply_to'] for e in replies}) != len(replies):
            failures.append('duplicate wire response')
        if any(e['kind'] == 'action' and not e.get('handled') for e in events):
            failures.append('unhandled controller action')
        if kinds['request-cancelled']:
            failures.append('request cancellation requires review (timeout/supersession)')
        if any(e['kind'] == 'observation' and e.get('self_state') in ('trust', 'robot') for e in events):
            failures.append('GUI seat entered trustee/robot state')
        # A game-over protocol notification alone does not prove the result UI.
        post_game = False
        result_scope = False
        for event in events:
            if event['kind'] == 'game-over':
                post_game = True
            if post_game and event['kind'] == 'observation' and any(
                    c.get('id') == 'winner_table' for c in event.get('controls', [])):
                result_scope = True
        if not result_scope:
            failures.append('missing visible native result controls')
    return {'level': 'SDL virtual input + live Qt GUI' if require_game else 'SDL virtual input',
            'physical_hardware_tested': False, 'passed': not failures,
            'counts': dict(kinds), 'server_counts': dict(collections.Counter(e['kind'] for e in server)),
            'failures': sorted(set(failures))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', required=True)
    parser.add_argument('--trace', required=True)
    parser.add_argument('--server-trace')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--script', help='JSONL physical inputs, optional wait_ms between entries')
    mode.add_argument('--play-basic', action='store_true')
    parser.add_argument('--seconds', type=int, default=120)
    parser.add_argument('--result', required=True)
    parser.add_argument('--return-menu', action='store_true', help='After game over, activate the native Return to main menu button')
    args = parser.parse_args()
    driver = Driver(args.socket, args.trace)
    error = None
    try:
        time.sleep(0.4)  # Allow neutral ownership and passive observer attachment.
        if args.play_basic:
            driver.play_basic(args.seconds)
            if args.return_menu:
                driver.widget(lambda c: c.get('label') == 'Return to main menu')
                time.sleep(0.4)
                if driver.read().get('scope') != 'MainWindow':
                    raise RuntimeError('Native result menu did not return to MainWindow')
                if not any(c.get('focused') for c in driver.read().get('controls', [])):
                    raise RuntimeError('Returned window has no focused usable control')
        else:
            for line in Path(args.script).read_text().splitlines():
                item = json.loads(line)
                wait = item.pop('wait_ms', 80)
                driver.inject(item)
                time.sleep(wait / 1000)
                driver.read()
    except (OSError, ValueError, RuntimeError) as exc:
        error = str(exc)
    result = evidence(driver.events, args.server_trace, args.play_basic)
    if error:
        result['passed'] = False
        result['failures'].append(error)
    Path(args.result).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
