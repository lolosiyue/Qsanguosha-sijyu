#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cache_hit_sim.py — offline provider prompt-cache simulation over a run's brief stream.

The external-agent driver never calls an LLM API, so real cache telemetry can
only come from a live provider session. This tool measures the *structural*
cache behaviour of the brief stream a session would consume, exactly and
reproducibly, under the two session topologies:

- accumulate (topology B, what chat-based sessions do): briefs are appended to
  one conversation; for decision k the cacheable prefix is everything sent
  before it, and only brief k is a cache miss.
- fresh (topology A): each decision is a standalone call with a shared static
  head (契約備忘 + 座位背景 + 筆記 sections, stable-first ordering, title
  moved to the tail); the cacheable prefix is the longest common prefix with
  any earlier call, floored to the provider's block/min-prefix rules.

Provider rules modelled: minimum cacheable prefix (tokens), block granularity
(tokens), cache-read multiplier, cache-write multiplier on misses. Byte counts
are exact; tokens are derived via --bytes-per-token (CJK-heavy UTF-8 defaults
to 3 bytes/token — an approximation, labelled as such).

Also computes the 全量 (full-DTO) counterfactual from pending-<n>.json files
when present, so the sim reports the cost ratio of brief mode against reading
whole DTOs under the same topology.
"""
import argparse
import glob
import json
import os
import re
import sys

SECTIONS = ('契約備忘', '座位背景', '筆記', '本題', '局面增量', '尾註')
STATIC_SECTIONS = ('契約備忘', '座位背景', '筆記')

# Provider parameter presets. openai/anthropic multipliers are their published
# standard ones; glm's are conservative placeholders — confirm on
# open.bigmodel.cn (context-caching pricing changed several times in 2026).
PRESETS = {
    'openai': {'min_tokens': 1024, 'block_tokens': 128, 'read_price': 0.5,
               'write_price': 1.0, 'ttl_seconds': 300},
    'anthropic': {'min_tokens': 1024, 'block_tokens': 128, 'read_price': 0.1,
                  'write_price': 1.25, 'ttl_seconds': 300},
    'glm': {'min_tokens': 1024, 'block_tokens': 128, 'read_price': 0.5,
            'write_price': 1.0, 'ttl_seconds': 3600,
            'note': 'placeholder multipliers, unverified — check open.bigmodel.cn'},
}


def split_sections(text):
    """Return {section name: body bytes} preserving the brief layout."""
    out = {}
    parts = re.split(r'^## ', text, flags=re.M)
    title = parts[0]
    for part in parts[1:]:
        name = part.split('\n', 1)[0].strip()
        if name in SECTIONS:
            out[name] = part.encode('utf-8')
    out['H1'] = title.encode('utf-8')
    return out


def load_briefs(brief_root, seat):
    d = os.path.join(brief_root, seat)
    ns = sorted(int(os.path.basename(p).split('-')[1].split('.')[0])
                for p in glob.glob(os.path.join(d, 'decision-*.md')))
    return [(n, open(os.path.join(d, 'decision-%d.md' % n), encoding='utf-8').read())
            for n in ns]


def blockfloor(tokens, block, minimum):
    if tokens < minimum:
        return 0
    return (tokens // block) * block


def lcp_bytes(a, b):
    i, n = 0, min(len(a), len(b))
    while i < n and a[i] == b[i]:
        i += 1
    return i


def simulate_accumulate(payloads, args, timestamps=None):
    """Topology B: append-only session. Optional TTL model: a gap above
    args.ttl_seconds between consecutive calls expires the whole cached
    prefix (sliding-window refresh on every hit; the expiry call re-writes
    everything and re-populates the cache)."""
    rows = []
    cumulative = args.static_prefix_bytes
    prev_ts = None
    for k, payload in enumerate(payloads):
        ts = timestamps[k] if timestamps else None
        cached = cumulative
        expired = False
        if (args.ttl_seconds and prev_ts is not None and ts is not None
                and (ts - prev_ts).total_seconds() > args.ttl_seconds):
            cached = 0
            expired = True
        cumulative += payload
        rows.append({'k': k + 1, 'cached': cached, 'new': payload,
                     'total': cached + payload, 'expired': expired})
        if ts is not None:
            prev_ts = ts
    return rows


def simulate_fresh(static_parts, volatile_parts, args):
    """Topology A: standalone calls, shared static head, LCP across calls."""
    head = b'\x00' * args.static_prefix_bytes  # placeholder for a constant session head
    payloads = []
    for static, volatile in zip(static_parts, volatile_parts):
        payloads.append(head + static + volatile)
    seen = []
    rows = []
    for k, payload in enumerate(payloads):
        best = 0
        for prev in seen:
            best = max(best, lcp_bytes(payload, prev))
        cached = blockfloor(int(best / args.bytes_per_token),
                            args.block_tokens, args.min_tokens) * args.bytes_per_token
        rows.append({'k': k + 1, 'cached': cached, 'new': len(payload) - cached,
                     'total': len(payload)})
        seen.append(payload)
    return rows


def cost_of(rows, args):
    bpt = args.bytes_per_token
    read = sum(r['cached'] for r in rows) / bpt * args.read_price
    write = sum(r['new'] for r in rows) / bpt * args.write_price
    return read + write


def plain_cost(rows):
    return sum(r['total'] for r in rows)


def report(name, rows, args, baseline_rows=None):
    bpt = args.bytes_per_token
    tot_cached = sum(r['cached'] for r in rows)
    tot_new = sum(r['new'] for r in rows)
    tot = tot_cached + tot_new
    hit = 100.0 * tot_cached / tot if tot else 0.0
    cost = cost_of(rows, args)
    plain = plain_cost(rows) / bpt  # no-cache baseline cost
    line = {
        'topology': name,
        'calls': len(rows),
        'input_tokens_total': int(tot / bpt),
        'hit_rate_pct': round(hit, 1),
        'cached_tokens': int(tot_cached / bpt),
        'miss_tokens': int(tot_new / bpt),
        'cost_ratio_vs_nocache': round(cost / plain, 3) if plain else None,
    }
    if args.ttl_seconds:
        line['ttl_seconds'] = args.ttl_seconds
        line['expired_calls'] = sum(1 for r in rows if r.get('expired'))
    if baseline_rows is not None:
        base_cost = cost_of(baseline_rows, args)
        line['cost_vs_fulldto_mode'] = round(cost / base_cost, 3) if base_cost else None
    return line


def seat_report(brief_root, seat, args, ts_map=None):
    briefs = load_briefs(brief_root, seat)
    if not briefs:
        return None
    secs = [split_sections(t) for _, t in briefs]
    memos = {s.get('契約備忘') for s in secs}
    static_parts = [b''.join(s.get(k, b'') for k in STATIC_SECTIONS) for s in secs]
    volatile_parts = [b''.join(s.get(k, b'') for k in ('本題', '局面增量', '尾註', 'H1'))
                      for s in secs]
    sizes = [len(t.encode('utf-8')) for _, t in briefs]
    timestamps = None
    if ts_map and args.ttl_seconds:
        timestamps = [ts_map.get(n) for n, _ in briefs]
    out = {
        'seat': seat,
        'briefs': len(briefs),
        'brief_bytes_total': sum(sizes),
        'contract_memo_distinct_variants': len(memos),
    }
    acc = simulate_accumulate(sizes, args, timestamps)
    out['accumulate'] = report('accumulate', acc, args)
    fresh = simulate_fresh(static_parts, volatile_parts, args)
    out['fresh'] = report('fresh', fresh, args)

    pending_dir = os.path.join(brief_root, seat)
    pend = []
    for n, _ in briefs:
        p = os.path.join(pending_dir, 'pending-%d.json' % n)
        pend.append(os.path.getsize(p) if os.path.exists(p) else None)
    if all(x is not None for x in pend):
        base_acc = simulate_accumulate(pend, args, timestamps)
        # Conservative full-DTO counterfactual: payloads modelled as distinct
        # content (unique leading byte), so fresh topology gets no cross-call hits.
        pend_payloads = [bytes([i & 0xFF]) + b'\x11' * max(0, x - 1)
                         for i, x in enumerate(pend)]
        base_fresh = simulate_fresh(
            [b''] * len(pend_payloads), pend_payloads, args)
        out['accumulate']['cost_vs_fulldto_mode'] = report(
            'accumulate', acc, args, base_acc)['cost_vs_fulldto_mode']
        out['fresh']['cost_vs_fulldto_mode'] = report(
            'fresh', fresh, args, base_fresh)['cost_vs_fulldto_mode']
        out['fulldto_bytes_total'] = sum(pend)
    return out


def main(argv=None):
    user_argv = list(sys.argv[1:] if argv is None else argv)
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--brief-dir', required=True,
                    help='run brief dir containing per-seat subdirs')
    ap.add_argument('--seats', default=None, help='comma list; default: all subdirs')
    ap.add_argument('--topology', choices=('both', 'accumulate', 'fresh'), default='both')
    ap.add_argument('--static-prefix-bytes', type=int, default=0,
                    help='constant session head (system prompt etc.) counted in every call')
    ap.add_argument('--preset', choices=sorted(PRESETS), default=None,
                    help='named provider parameter set; explicit flags override it')
    ap.add_argument('--min-tokens', type=int, default=1024,
                    help='provider minimum cacheable prefix in tokens')
    ap.add_argument('--block-tokens', type=int, default=128,
                    help='provider cache block granularity in tokens')
    ap.add_argument('--bytes-per-token', type=float, default=3.0,
                    help='approximate tokeniser ratio (CJK-heavy UTF-8 ~3; derived, not exact)')
    ap.add_argument('--read-price', type=float, default=0.1,
                    help='cache-read multiplier on base input price')
    ap.add_argument('--write-price', type=float, default=1.25,
                    help='cache-write multiplier applied to misses')
    ap.add_argument('--ttl-seconds', type=int, default=None,
                    help='provider cache idle expiry; 0/none disables. Needs'
                         ' --audit-log for per-decision timestamps')
    ap.add_argument('--audit-log', default=None,
                    help='auto-answers.jsonl of the run; supplies per-decision'
                         ' timestamps for the TTL model')
    args = ap.parse_args(user_argv)
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

    preset_note = None
    if args.preset:
        preset = PRESETS[args.preset]
        for key, val in preset.items():
            if key == 'note':
                preset_note = val
                continue
            if ('--' + key.replace('_', '-')) not in user_argv:
                setattr(args, key, val)
    if args.ttl_seconds is None:
        args.ttl_seconds = 0

    ts_map = {}
    if args.audit_log:
        from datetime import datetime
        with open(os.path.abspath(args.audit_log), encoding='utf-8') as fh:
            for line in fh:
                try:
                    row = json.loads(line)
                except ValueError:
                    continue
                if (row.get('route') in ('local', 'escalated', 'replay')
                        and row.get('seat') and row.get('n') is not None
                        and row.get('time')):
                    try:
                        ts_map.setdefault(row['seat'], {})[int(row['n'])] = \
                            datetime.fromisoformat(row['time'])
                    except ValueError:
                        pass

    root = os.path.abspath(args.brief_dir)
    seats = (args.seats.split(',') if args.seats
             else sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))))
    results = []
    for seat in seats:
        r = seat_report(root, seat, args, ts_map.get(seat))
        if r:
            results.append(r)
    summary = {
        'note_tokens': 'token counts are derived via --bytes-per-token=%.1f (approximation); byte counts are exact' % args.bytes_per_token,
        'preset': args.preset,
        'preset_note': preset_note,
        'provider_model': {'min_cacheable_prefix_tokens': args.min_tokens,
                           'block_tokens': args.block_tokens,
                           'cache_read_price': args.read_price,
                           'cache_write_price': args.write_price,
                           'ttl_seconds': args.ttl_seconds or None},
        'seats': results,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
