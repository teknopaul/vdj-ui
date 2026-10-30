#!/usr/bin/env python3
"""The partition P (first folder, P000..P07F) is seven selected bits of the slot Y (the second folder, 0..200002):
    P bit 0 = Y bit 0,  P bit 1 = Y bit 2,  P bit 2 = Y bit 6,  P bit 3 = Y bit 7,
    P bit 4 = Y bit 9,  P bit 5 = Y bit 13, P bit 6 = Y bit 16
Verified on 7401 (folder) pairs from real rekordbox exports and the PC library with 0 mismatches."""
BITS = [0, 2, 6, 7, 9, 13, 16]
def p_of_y(y):
    return sum(((y >> b) & 1) << k for k, b in enumerate(BITS))
if __name__ == '__main__':
    import sys
    for y in map(int, sys.argv[1:] or ['69769']):            # 0x11089? e.g. P049/00010C89 -> Y = 0x10C89 = 68745
        print('Y=%d (0x%08X) -> P%03X' % (y, y, p_of_y(y)))
