#!/usr/bin/env python3
"""Extract Pro DJ Link UDP packet fixtures from the captures (stdlib only).

usage: extract_udp_fixtures.py [pc_pcap] [boot_pcap] [outdir]
Writes the first packet of each interesting kind as a raw UDP payload file.
"""
import os
import struct
import sys

pc = sys.argv[1] if len(sys.argv) > 1 else '/var/tmp/prolink-w-pc-rekordbox.pcap'
boot = sys.argv[2] if len(sys.argv) > 2 else '/var/tmp/prolink-w-boot-120s.pcap'
out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'udp')
HDR = bytes.fromhex('5173707431576d4a4f4c')


def packets(path):
    d = open(path, 'rb').read()
    off = 24
    t0 = None
    while off < len(d):
        ts, tu, cl, _ol = struct.unpack('<IIII', d[off:off + 16])
        off += 16
        p = d[off:off + cl]
        off += cl
        t = ts + tu / 1e6
        t0 = t0 or t
        if p[12:14] != b'\x08\x00' or p[23] != 17:
            continue
        base = 14 + (p[14] & 15) * 4
        sp, dp = struct.unpack('>HH', p[base:base + 4])
        pl = p[base + 8:]
        if pl[:10] != HDR:
            continue
        yield t - t0, '.'.join(map(str, p[26:30])), sp, dp, pl


def first(path, pred, name):
    for t, src, sp, dp, pl in packets(path):
        if pred(src, sp, dp, pl):
            open(os.path.join(out, name), 'wb').write(pl)
            print('%-28s %3d bytes  t=%.2f from %s' % (name, len(pl), t, src))
            return
    sys.exit('not found: ' + name)


PC = '169.254.51.135'
os.makedirs(out, exist_ok=True)
first(pc, lambda s, sp, dp, p: s == PC and dp == 50000 and p[10] == 0x00, 'rb_stage1.bin')
first(pc, lambda s, sp, dp, p: s == PC and dp == 50000 and p[10] == 0x02, 'rb_stage2.bin')
first(pc, lambda s, sp, dp, p: s == PC and dp == 50000 and p[10] == 0x06, 'rb_keepalive.bin')
first(pc, lambda s, sp, dp, p: s == PC and dp == 50002 and p[10] == 0x11, 'rb_name.bin')
first(pc, lambda s, sp, dp, p: s == PC and dp == 50002 and p[10] == 0x06, 'rb_media_response.bin')
first(pc, lambda s, sp, dp, p: s != PC and dp == 50002 and p[10] == 0x05, 'deck_media_query.bin')
first(pc, lambda s, sp, dp, p: s != PC and dp == 50000 and p[10] == 0x06, 'deck_keepalive_xdj700.bin')
first(pc, lambda s, sp, dp, p: s != PC and dp == 50002 and p[10] == 0x10, 'deck_poll.bin')
first(pc, lambda s, sp, dp, p: s == PC and dp == 50002 and p[10] == 0x16, 'rb_poll_reply.bin')
first(boot, lambda s, sp, dp, p: dp == 50000 and p[10] == 0x00, 'deck_stage1.bin')
first(boot, lambda s, sp, dp, p: dp == 50000 and p[10] == 0x02, 'deck_stage2.bin')
first(boot, lambda s, sp, dp, p: dp == 50000 and p[10] == 0x04, 'deck_stage3.bin')

# pacing of the PC's claim sequence
print('PC claim pacing (type, time):')
last = {}
for t, src, sp, dp, pl in packets(pc):
    if src == PC and dp == 50000 and pl[10] in (0, 2) and last.get(pl[10], 0) < 4:
        last[pl[10]] = last.get(pl[10], 0) + 1
        print('  type %d  t=%.3f' % (pl[10], t))
print('deck boot claim pacing:')
seen = {}
for t, src, sp, dp, pl in packets(boot):
    if dp == 50000 and pl[10] in (0, 2, 4) and seen.get((src, pl[10]), 0) < 4:
        seen[(src, pl[10])] = seen.get((src, pl[10]), 0) + 1
        print('  %s type %d  t=%.3f counter=%s' % (src, pl[10], t, pl[0x2f:0x30].hex() if pl[10] in (2, 4) else ''))
