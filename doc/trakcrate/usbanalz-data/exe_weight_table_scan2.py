#!/usr/bin/env python3
"""Wider search of rekordbox.exe (read as data only) for a table that matches the measured weights.

The weight at 1-based distance d from the end of the path must satisfy   w mod 200003 in {db, db+K}   (db measured in export4-positions-140.json).
Searches 32 bit entries at a stride of 1..32 words (so also entries inside structs), every byte alignment, distance step +-1, +-2, +-4 per entry, runs of >= 3 entries.
Chance: about 17000 single-word coincidences per alignment, a run of three has probability ~1e-10 per start."""
import numpy as np, json, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
EXE = sys.argv[1] if len(sys.argv) > 1 else '/var/tmp/rekordbox.exe'
d = json.load(open(os.path.join(HERE, 'export4-positions-140.json')))
Pa, Ya = d['base']; rows = {j: (b, c) for j, b, c in d['rows']}
M = 200003; K = 102874
db = {j: (rows[j][0][1] - Ya) % M for j in rows}
def dist(j): return 144 - j if j >= 96 else (144 - j - 1 if j >= 48 else 144 - j - 2)
res = {}
for j in rows:
    for r in (db[j], (db[j] + K) % M): res.setdefault(r, set()).add(dist(j))
keys = np.array(sorted(res), dtype=np.int64)
data = np.fromfile(EXE, dtype=np.uint8)
best = []
for align in range(4):
    n = (len(data) - align) // 4
    w = data[align:align + 4 * n].view('<u4').astype(np.int64)
    idx = np.nonzero(np.isin(w % M, keys))[0]
    pos = {int(i): res[int(w[i] % M)] for i in idx}
    print(f'alignment {align}: {len(pos)} candidate words', flush=True)
    for i, D in pos.items():
        for stride in range(1, 33):
            for step in (1, -1, 2, -2, 4, -4):
                cur = D; length = 1; j = i
                while True:
                    j += stride
                    nxt = pos.get(j)
                    if nxt is None: break
                    cur2 = {x + step for x in cur} & nxt
                    if not cur2: break
                    cur = cur2; length += 1
                if length >= 3: best.append((length, align, i * 4 + align, stride, step, sorted(cur)[:2]))
best.sort(reverse=True)
print('RUNS >= 3:', best[:10] if best else 'none')
