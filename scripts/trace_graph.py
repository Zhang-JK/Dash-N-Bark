#!/usr/bin/env python3
"""Translate Dash-N-Bark trace log entries into Chrome trace JSON.

Two log shapes are recognised, both emitted by Trace.h in debug builds:

  [pool-trace] exit  tid=N task=X dur_us=Y channel=Z
      A stdexec sender (command handler, bg ticker, etc.) wrapped via
      dnb_trace::withPoolTrace finished. cat='<channel>' (value/error/
      stopped/dropped).

  [scope] exit  tid=N name=X dur_us=Y
      A DNB_TRACE_SCOPE RAII block (bg ticker tick, voice_receive, a
      ToolInterface method, etc.) exited. cat='scope'.

Open the output in chrome://tracing or https://ui.perfetto.dev for an
interactive swimlane of pool-worker activity over time.

Usage:
    scripts/trace_graph.py logs/output_*.log -o trace.json
    cat logs/output_*.log | scripts/trace_graph.py - -o trace.json
"""

import argparse
import json
import re
import sys


# Spdlog format: [%H:%M:%S.%e] [%^%l%$] [%s:%#] %v
# So both patterns expect a timestamp prefix and an arbitrary tail before
# the marker.
_TS = (r'\[(\d{2}):(\d{2}):(\d{2})\.(\d{3})\]')

POOL_RE = re.compile(
    _TS + r'.*?\[pool-trace\]\s+exit\s+'
    r'tid=(\d+)\s+task=(\S+)\s+dur_us=(\d+)\s+channel=(\S+)'
)

SCOPE_RE = re.compile(
    _TS + r'.*?\[scope\]\s+exit\s+'
    r'tid=(\d+)\s+name=(\S+)\s+dur_us=(\d+)'
)


def _end_us(h, mi, s, ms):
    return (int(h) * 3600 + int(mi) * 60 + int(s)) * 1_000_000 + int(ms) * 1000


def parse(streams):
    """Yield Chrome trace 'complete' (ph='X') events from spdlog input."""
    for f in streams:
        for line in f:
            m = POOL_RE.search(line)
            if m:
                h, mi, s, ms, tid, task, dur, channel = m.groups()
                end_us = _end_us(h, mi, s, ms)
                yield {
                    'name': task,
                    'cat': channel,
                    'ph': 'X',
                    'ts': end_us - int(dur),
                    'dur': int(dur),
                    'pid': 1,
                    'tid': int(tid),
                    'args': {'channel': channel},
                }
                continue
            m = SCOPE_RE.search(line)
            if m:
                h, mi, s, ms, tid, name, dur = m.groups()
                end_us = _end_us(h, mi, s, ms)
                yield {
                    'name': name,
                    'cat': 'scope',
                    'ph': 'X',
                    'ts': end_us - int(dur),
                    'dur': int(dur),
                    'pid': 1,
                    'tid': int(tid),
                }


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+', help='log files (use "-" for stdin)')
    ap.add_argument('-o', '--output', default='trace.json',
                    help='output path (default: trace.json)')
    args = ap.parse_args()

    streams = [sys.stdin if p == '-' else open(p) for p in args.logs]
    events = list(parse(streams))
    if not events:
        sys.exit('no [pool-trace] or [scope] exit lines found in input')

    with open(args.output, 'w') as f:
        json.dump({'traceEvents': events}, f)
    print(f'wrote {args.output} ({len(events)} events) — '
          f'open in chrome://tracing or ui.perfetto.dev',
          file=sys.stderr)


if __name__ == '__main__':
    main()
