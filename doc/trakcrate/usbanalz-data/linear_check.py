#!/usr/bin/env python3
"""Shows that the second folder number is H mod 200003 for a wrapping 32-bit H that is linear in the path's characters.
Input: export3-controlled-49.json (path -> [P, Y]). For each of the 24 letter positions the base letter 'a' was changed to 'b' and to 'c'.
If Y = H mod M with H linear mod 2^32, then q = H div M satisfies  M*(qc - 2*qb + qa) = 2*Yb - Yc - Ya  (mod 2^32), and (qc - 2qb + qa) must be tiny."""
import json, sys
M = 200003; T = 1 << 32; Minv = pow(M, -1, T)
o = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'export3-controlled-49.json'))
base = '/Contents/aaaaaaaa/aaaaaaaa/aaaaaaaa.mp3'
def path(j, ch):
    s = list('a' * 24); s[j] = ch; s = ''.join(s); return f'/Contents/{s[:8]}/{s[8:16]}/{s[16:]}.mp3'
Pa, Ya = o[base]
print('2^32 mod 200003 =', T % M)
for j in range(24):
    Pb, Yb = o[path(j, 'b')]; Pc, Yc = o[path(j, 'c')]
    D = ((2 * Yb - Yc - Ya) * Minv) % T
    if D > T // 2: D -= T
    print('position %2d  q-second-difference %7d   (Yc-2Yb+Ya) mod M = %6d' % (j, D, (Yc - 2 * Yb + Ya) % M))
