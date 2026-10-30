#!/usr/bin/env python3
"""Tests the loop found in rekordbox.exe: h = h*0x34f5501d + c*0x93b6 (mod 2^32) over the path's characters, slot = h mod 200003."""
import json, glob
K, W, M, T = 0x34f5501d, 0x93b6, 200003, 1 << 32
data = {}
for f in glob.glob('export*.json'):
    o = json.load(open(f))
    if not isinstance(o, dict): continue
    for k, v in o.items():
        if isinstance(v, list) and len(v) == 2: data[k] = v[1]
print(len(data), 'pairs')
def h(s, seed=0, lag=False):
    x = seed; prev = 0
    for ch in s:
        c = ord(ch)
        if lag: x = (x * K + prev * W) % T; prev = c
        else: x = (x * K + c * W) % T
    if lag: x = (x + prev * W) % T
    return x
for lag in (False, True):
    for tail in (False, True):
        ok = 0
        for p, y in data.items():
            s = p + ('\0' if tail else '')
            ok += h(s, 0, lag) % M == y
        print('lag', lag, 'terminator', tail, 'matches', ok)
print('mismatches:')
for p, y in data.items():
    if h(p) % M != y: print(repr(p), y, h(p) % M)
