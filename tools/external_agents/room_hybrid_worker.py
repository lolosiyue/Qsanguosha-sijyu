"""Room-owned JEV adapter. Receives seat capabilities only via private stdin.

The Qt room owns the game ID and process lifetime. A lost worker causes the
native endpoint's SmartAI fallback; this process never retries a paid request.
"""
import json
import os
import select
import sys
import time

import hybrid50 as h
import providers as p


def notice(code):
    if code in ('budget', 'provider'):
        sys.stdout.write('NOTICE ' + code + '\n')
        sys.stdout.flush()


def local(client, request):
    return client.call({'op': 'local', 'decisionId': request['decisionId'],
                        'stateRevision': request['stateRevision']})


def decide(client, request, adapter, paid_disabled):
    """Queue one exact native ticket; return (ack, route, disable_paid, notice)."""
    if paid_disabled:
        return local(client, request), 'local_native', True, None
    route, _, answers, data = h.route(request, adapter)
    if route == 'local_native':
        return local(client, request), 'local_native', paid_disabled, None
    if route == 'local_forced':
        choice = next(iter(answers))
    else:
        try:
            choice = adapter.choose('jev', *data)
        except p.BudgetExhausted:
            return local(client, request), 'local_native', True, 'budget'
        except p.DecisionError:
            return local(client, request), 'local_native', True, 'provider'
    # Exact result produced from native legal candidates. No stale re-stamping.
    return client.call({'op': 'submit', 'result': answers[choice]}), route, paid_disabled, None


def main():
    fd = sys.stdin.fileno()
    os.set_blocking(fd, False)
    incoming = bytearray()
    clients = {}
    pending = {}
    adapter = None
    paid_disabled = False
    while True:
        readable, _, _ = select.select([fd], [], [], 0)
        if readable:
            part = os.read(fd, 65536)
            if not part:
                break
            incoming.extend(part)
            if len(incoming) > 262144:
                break
            while b'\n' in incoming:
                line, _, rest = incoming.partition(b'\n')
                incoming = bytearray(rest)
                try:
                    message = json.loads(line)
                    if message.get('op') == 'init' and adapter is None:
                        game_id = message['game_id']
                        if not isinstance(game_id, str) or not game_id:
                            raise ValueError('game_id')
                        adapter = p.ProviderAdapters(
                            p.BudgetLedger(max_requests=h.POLICY['max_requests']),
                            allowed_providers=('jev',), game_id=game_id)
                    elif message.get('op') == 'seat' and adapter is not None:
                        seat = message['bootstrap']
                        name = seat['objectName']
                        if name in clients or len(clients) >= 50:
                            raise ValueError('duplicate_seat')
                        client = h.g.Client(seat)
                        client.socket.settimeout(5)
                        if not client.hello.get('ok'):
                            raise ValueError('seat_unauthorized')
                        clients[name] = client
                except (ValueError, KeyError, TypeError, OSError, p.DecisionError):
                    # A malformed private bootstrap is a setup failure. Exiting
                    # disconnects every endpoint into native SmartAI.
                    return 2
        if adapter is None or not clients:
            time.sleep(.01)
            continue
        for name, client in list(clients.items()):
            try:
                state = client.call({'op': 'poll'})
                if state.get('status') == 'finished':
                    return 0
                if state.get('status') in ('smart-ai-fallback', 'cancelled'):
                    # The authority has already moved this seat to native AI.
                    # A late model result must never be sent to that ticket.
                    raise ValueError('native_fallback')
                request = state.get('request')
                old = pending.get(name)
                if not request:
                    if state.get('status') == 'idle':
                        pending.pop(name, None)
                    continue
                if request.get('viewerObjectName') != name:
                    raise ValueError('viewer_mismatch')
                ticket = (request['decisionId'], request['stateRevision'])
                if ticket == old:
                    continue
                ack, _, paid_disabled, code = decide(client, request, adapter, paid_disabled)
                if code:
                    notice(code)
                if not ack.get('ok'):
                    # Native authority rejected this ticket; disconnect atomically
                    # to its SmartAI fallback. Never retry or restamp the result.
                    raise ValueError('native_rejected')
                pending[name] = ticket
            except (OSError, ValueError, KeyError, TypeError, p.DecisionError):
                paid_disabled = True
                try:
                    client.close()
                except OSError:
                    pass
                clients.pop(name, None)
                pending.pop(name, None)
                notice('provider')
        time.sleep(.005)
    for client in clients.values():
        try:
            client.close()
        except OSError:
            pass
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
