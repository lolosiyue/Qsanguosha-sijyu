"""Small provider decision smoke, not a native game or game simulation.

Describe is offline. Live first checks BOTH configured hosts, then sends at most
one inference request to each. It never retries or changes model/endpoint.
"""
import argparse
import json
import os
import re
import urllib.error
import urllib.request

import providers as p


def readiness():
    checks = []
    for provider, url in [('deepseek', 'https://api.deepseek.com/models'),
                          ('jev', 'https://api.typesafe.ai/v1/models')]:
        key = os.environ.get(p.POLICY[provider]['secret'])
        check = {'provider': provider, 'secret_present': bool(key), 'ready': False}
        if key:
            request = urllib.request.Request(url, headers={'Authorization': 'Bearer ' + key})
            try:
                with urllib.request.build_opener(p._NoRedirect()).open(request, timeout=15) as r:
                    data = json.loads(r.read(p.MAX_RESPONSE_BYTES + 1))
                    models = [m.get('id', m.get('name')) for m in
                              data.get('data', data.get('models', [])) if isinstance(m, dict)]
                    expected = 'deepseek-flash' if provider == 'deepseek' else 'jev-latest'
                    check['ready'] = expected in models
                    check['status'] = 'model_available' if check['ready'] else 'model_unavailable'
            except urllib.error.HTTPError as e:
                check['status'] = 'http_' + str(e.code)
            except urllib.error.URLError as e:
                match = re.search(r'Tunnel connection failed: (\d{3})', str(e.reason))
                check['status'] = 'proxy_connect_' + match.group(1) if match else 'transport_failure'
            except Exception:
                check['status'] = 'response_or_transport_failure'
        else:
            check['status'] = 'secret_missing'
        checks.append(check)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true', help='Use approved APIs after both pass readiness')
    args = parser.parse_args()
    plan = {'models': {q: policy['model'] for q, policy in p.POLICY.items()},
            'shared_cap_usd': p.CAP_NANODOLLARS / 1e9,
            'worst_case_reservation_usd': {q: p.reservation(q)/1e9 for q in p.POLICY},
            'inference_request_cap': 2, 'retries': 0, 'complete_game': False}
    if not args.live:
        print(json.dumps({'mode': 'offline_description', **plan}, indent=2))
        return 0
    checks = readiness()
    if not all(c['ready'] for c in checks):
        print(json.dumps({'status': 'blocked_before_inference', 'checks': checks,
                          'inference_requests': 0, 'complete_game': False}, indent=2))
        return 2
    state = {'synthetic': True, 'seat_visible': True,
             'rules': 'Slash causes one damage. An own Jink cancels it. '
                      'A player at zero HP needs rescue. Choose a legal response.',
             'observation': {'seat': 'seat_1', 'hp': 1, 'own_hand': ['Jink'],
                             'pending_card': 'Slash'}}
    options = {'jink': 'Play own Jink to avoid damage.', 'take_damage': 'Decline Jink and lose 1 HP.'}
    adapters = p.ProviderAdapters()
    decisions = []
    try:
        for provider in ('jev', 'deepseek'):
            decisions.append({'provider': provider, 'choice': adapters.choose(provider, state, options)})
    except p.DecisionError as e:
        print(json.dumps({'status': 'terminated_on_error', 'error_code': str(e),
                          'decisions': decisions, 'complete_game': False}, indent=2))
        return 3
    print(json.dumps({'status': 'provider_smoke_complete', 'decisions': decisions,
                      'complete_game': False}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
