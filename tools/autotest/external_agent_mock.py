#!/usr/bin/env python3
"""A separate-process JSON-lines mock; no imports from, or pointers into, the host.

Run with --host PATH to start the supplied native host via private pipes, or pass
one bootstrap JSON line on stdin to connect to an already running local host.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


class Client:
    def __init__(self, bootstrap, token=None):
        assert bootstrap['host'] == '127.0.0.1' and bootstrap['version'] == 1
        self.socket = socket.create_connection(('127.0.0.1', bootstrap['port']), timeout=5)
        self.file = self.socket.makefile('rwb')
        self.hello = self.call({'op': 'hello', 'version': 1, 'token': token or bootstrap['token']})

    def write(self, value):
        self.file.write(json.dumps(value, separators=(',', ':')).encode() + b'\n')
        self.file.flush()

    def read(self):
        line = self.file.readline(8 * 1024 * 1024 + 1)
        assert line.endswith(b'\n') and len(line) <= 8 * 1024 * 1024
        return json.loads(line)

    def call(self, value):
        self.write(value)
        return self.read()

    def close(self):
        self.file.close()
        self.socket.close()


def answer(q):
    options = q['choiceOptions']
    result = {'decisionId': q['decisionId'], 'stateRevision': q['stateRevision'],
              'kind': 'pass', 'action': {}}
    if q['kind'] in (0, 1) or options['optional']:
        return result
    result['kind'] = 'answer'
    if q['kind'] == 14:
        field = 'bottomCardIds' if options['defaultChoice'] == '2' else 'selectedCardIds'
        result['action'][field] = options['cardIds']
    elif q['kind'] in (11, 12):
        result['action']['selectedTargetNames'] = options['playerNames'][:options['minCount']]
    elif options['cardIds']:
        result['action']['selectedCardIds'] = options['cardIds'][:options['minCount']]
    elif options['choices']:
        result['action']['userString'] = options['choices'][0]
    else:
        raise AssertionError('Unsupported mock question: ' + str(q['kind']))
    return result


def play(bootstrap, exercise=False, cancel=False):
    assert str(os.getpid()) != bootstrap.get('hostPid')
    if exercise:
        bad = Client(bootstrap, 'wrong-capability')
        assert bad.hello == {'ok': False, 'error': 'unauthorized'}
        bad.close()
        time.sleep(.05)
    client = Client(bootstrap)
    assert client.hello['ok']
    count, exercised, cancelled = 0, False, False
    patterns = set()
    deadline = time.monotonic() + 120  # test/client budget; never a host game deadline
    try:
        while time.monotonic() < deadline:
            state = client.call({'op': 'poll'})
            if state['status'] == 'finished':
                assert count > 0 or cancelled
                if not cancel: assert state['winner']
                return {'decisions': count, 'winner': state['winner'], 'patterns': sorted(patterns),
                        'reconnect_stale_duplicate_tested': exercised, 'cancelled': cancelled,
                        'clientPid': os.getpid(), 'hostPid': bootstrap.get('hostPid')}
            assert state['status'] != 'smart-ai-fallback'
            q = state.get('request')
            if q is None:
                time.sleep(.005)
                continue
            # The wire has the same viewer-scoped DTO; opponent identities must be gated.
            assert q['viewerObjectName'] == q['worldView']['self']['objectName']
            for other in q['worldView']['players']:
                if not other['roleVisible']: assert not other['role']
                assert not other['privateFlags']
                assert all(not skill['state'] for skill in other['skills'])
            if cancel:
                assert client.call({'op': 'cancel'})['ok']
                cancelled = True
                continue
            if exercise and not exercised:
                client.close()
                time.sleep(.2)
                client = Client(bootstrap)
                assert client.hello['ok']
                resumed = client.call({'op': 'poll'})
                assert resumed['status'] == 'waiting-no-clock'
                assert resumed['request'] == q  # entire snapshot, IDs and revision retained
                forged = answer(q)
                forged['decisionId'] = str(int(q['decisionId']) + 1)
                rejected = client.call({'op': 'submit', 'result': forged})
                assert not rejected['ok'] and rejected['error'] == 'stale'
                forged = answer(q)
                forged['stateRevision'] = str(int(q['stateRevision']) + 1)
                assert client.call({'op': 'submit', 'result': forged})['error'] == 'stale'
                malformed = answer(q)
                malformed['action']['legacyCardString'] = 'injected'
                assert client.call({'op': 'submit', 'result': malformed})['error'] == 'invalid-result'
                # Arbitrary seat selection is not a transport operation.
                assert client.call({'op': 'poll', 'seat': 'opponent'})['error'] == 'unknown-operation'
                submitted = {'op': 'submit', 'result': answer(q)}
                client.write(submitted)
                client.write(submitted)
                assert client.read()['ok']
                assert not client.read()['ok']
                exercised = True
            else:
                accepted = client.call({'op': 'submit', 'result': answer(q)})
                assert accepted['ok'], accepted
            count += 1
            if q['pattern']: patterns.add(q['pattern'])
        raise AssertionError('Mock client test budget expired')
    except Exception:
        print("Mock failure after decisions:", count, "exercised:", exercised, file=sys.stderr)
        raise
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host')
    parser.add_argument('--exercise', action='store_true')
    parser.add_argument('--cancel', action='store_true')
    args = parser.parse_args()
    host = None
    with tempfile.TemporaryFile(mode='w+b') as errors:
        try:
            stream = sys.stdin
            if args.host:
                host = subprocess.Popen([args.host], stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=errors, text=True)
                stream = host.stdout
            line = stream.readline()
            while line and not line.startswith('QSAN_AGENT_BOOTSTRAP '):
                line = stream.readline()
            assert line, 'Host did not provide a bootstrap'
            bootstrap = json.loads(line.split(' ', 1)[1])
            result = play(bootstrap, args.exercise, args.cancel)
            if host:
                host.communicate(timeout=10)
                errors.seek(0)
                log = errors.read().decode(errors='replace')
                assert host.returncode == 0, log[-8000:]
                assert '[EXTERNAL_AGENT] explicit SmartAI fallback' not in log
                assert '[AI_CALLBACK_ERROR]' not in log
                if not args.cancel:
                    assert '[AUTOTEST] pre-game AI: SmartAI=2 fallback=0' in log
                assert 'workers_stopped=true room_destroyed=true' in log
                print(next(line for line in log.splitlines() if 'EXTERNAL_HOST_TERMINAL' in line))
            print('PASS out-of-process mock:', json.dumps(result, sort_keys=True))
        finally:
            if host and host.poll() is None:
                host.terminate()
                try: host.wait(timeout=5)
                except subprocess.TimeoutExpired: host.kill(); host.wait()
                errors.seek(0)
                sys.stderr.write(errors.read().decode(errors='replace')[-8000:])


if __name__ == '__main__':
    main()
