#!/usr/bin/env python3
"""Print the request/reply layouts of the captured dbserver streams (stdlib only).

usage: dump_stream.py [dir]   (dir holds client_stream.bin and server_stream.bin)
Pairs every client request with the server messages that carry the same transaction id.
"""
import struct
import sys
import os

d = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))


def parse(b):
    o = 5 if b[:5] == bytes.fromhex('1100000001') else 0
    out = []
    while o < len(b):
        assert b[o] == 0x11 and struct.unpack('>I', b[o + 1:o + 5])[0] == 0x872349ae, o
        o += 5
        txid = struct.unpack('>I', b[o + 1:o + 5])[0]; o += 5
        typ = struct.unpack('>H', b[o + 1:o + 3])[0]; o += 3
        ac = b[o + 1]; o += 2
        o += 5
        tags = b[o:o + 12]; o += 12
        args = []
        for i in range(ac):
            t = tags[i]
            if t == 6:
                args.append(('n', struct.unpack('>I', b[o + 1:o + 5])[0])); o += 5
            elif t == 2:
                n = struct.unpack('>I', b[o + 1:o + 5])[0]
                s = b[o + 5:o + 5 + 2 * n].decode('utf-16-be'); o += 5 + 2 * n
                args.append(('s', s))
            elif t == 3:
                if args and args[-1] == ('n', 0):
                    args.append(('b', b''))
                else:
                    n = struct.unpack('>I', b[o + 1:o + 5])[0]
                    args.append(('b', b[o + 5:o + 5 + n])); o += 5 + n
            else:
                raise ValueError(t)
        out.append((txid, typ, args))
    return out


def show(a):
    k, v = a
    if k == 'n':
        return '%x' % v
    if k == 's':
        return repr(v)
    return 'blob(%d)%s' % (len(v), v[:6].hex())


c = parse(open(os.path.join(d, 'client_stream.bin'), 'rb').read())
s = parse(open(os.path.join(d, 'server_stream.bin'), 'rb').read())
for txid, typ, args in c:
    print('REQ  tx=%x type=%04x %s' % (txid, typ, ' '.join(show(a) for a in args)))
    rep = [m for m in s if m[0] == txid]
    shown = 0
    for _t, rt, ra in rep:
        if rt == 0x4101 and shown >= 4:
            continue
        print('     REP type=%04x %s' % (rt, ' '.join(show(a) for a in ra)))
        if rt == 0x4101:
            shown += 1
    n = len([m for m in rep if m[1] == 0x4101])
    if n > 4:
        print('     (%d x 4101 in total, first 4 shown)' % n)
