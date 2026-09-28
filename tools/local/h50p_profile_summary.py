#!/usr/bin/env python3
"""Summarize existing H50P runner artifacts without changing a running game.

Usage: python3 tools/local/h50p_profile_summary.py /path/to/run-directory
Lua sampler percentages describe instruction samples, NOT wall-clock time.
"""
import collections
import datetime
import json
from pathlib import Path
import re
import sys


def summarize(directory):
    runs = []
    for log in sorted(Path(directory).glob('headless/*/50p.log')):
        text = log.read_text(errors='replace')
        samples = 0
        locations = collections.Counter()
        for line in text.splitlines():
            match = re.search(r'\[LUA_PROFILE\] samples=(\d+) (.*)', line)
            if not match:
                continue
            samples += int(match[1])
            for location, count in re.findall(r'(\S+:\d+\([^)]*\))=(\d+)\(', match[2]):
                locations[location] += int(count)
        headless = log.with_name('50p-headless.log')
        timestamps = []
        if headless.exists():
            for line in headless.read_text(errors='replace').splitlines():
                if '$AppendSeparator' in line:
                    timestamps.append(datetime.datetime.fromisoformat(line[:23]))
        probes = []
        for line in text.splitlines():
            if '[AI_PROBE]' in line:
                probes.append(line)
        mobile_samples = sum(count for location, count in locations.items()
                             if location.endswith('(n_mobile_ensure_instances)'))
        runs.append({
            'log': str(log),
            'sampler_total': samples,
            'sampler_mobile_ensure_samples': mobile_samples,
            'sampler_mobile_ensure_percent': 100 * mobile_samples / samples if samples else 0,
            'sampler_top_reported_locations': locations.most_common(15),
            'turn_markers': [x.isoformat() for x in timestamps],
            'turn_gaps_seconds': [(b-a).total_seconds() for a,b in zip(timestamps, timestamps[1:])],
            'ai_probes': probes,
            'ai_load_failures': [line for line in text.splitlines()
                                 if 'AI script load failed' in line or 'smart-ai.lua:' in line and 'error' in line.lower()],
        })
    return runs


if __name__ == '__main__':
    print(json.dumps(summarize(sys.argv[1]), indent=2))
