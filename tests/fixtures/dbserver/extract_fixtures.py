#!/usr/bin/env python3
"""Extract dbserver byte-stream fixtures from a classic pcap (stdlib only).

usage: extract_fixtures.py [pcap] [outdir]
Writes client_stream.bin, server_stream.bin, port_query_req.bin, port_query_resp.bin
for the first TCP flow to port 1051 and the first flow to port 12523 that carries data.
"""
import struct
import sys
import os

pcap = sys.argv[1] if len(sys.argv) > 1 else '/var/tmp/prolink-w-tcp.pcap'
outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))

d = open(pcap, 'rb').read()
off = 24
flows = {}          # (src, sport, dst, dport) -> {seq: payload}
while off < len(d):
    _ts, _tu, cl, _ol = struct.unpack('<IIII', d[off:off + 16])
    off += 16
    p = d[off:off + cl]
    off += cl
    if p[12:14] != b'\x08\x00' or p[23] != 6:
        continue
    ihl = (p[14] & 15) * 4
    base = 14 + ihl
    src = '.'.join(map(str, p[26:30]))
    dst = '.'.join(map(str, p[30:34]))
    sport, dport = struct.unpack('>HH', p[base:base + 4])
    seq = struct.unpack('>I', p[base + 4:base + 8])[0]
    doff = (p[base + 12] >> 4) * 4
    total = struct.unpack('>H', p[16:18])[0]
    pl = p[base + doff:14 + total]
    if pl:
        flows.setdefault((src, sport, dst, dport), {})[seq] = pl


def stream(key):
    segs = flows[key]
    return b''.join(segs[s] for s in sorted(segs))


def find(port, from_client):
    for k in flows:
        if from_client and k[3] == port:
            return k
        if not from_client and k[1] == port:
            return k
    raise SystemExit('no flow for port %d' % port)


for name, port, from_client in (('client_stream.bin', 1051, True), ('server_stream.bin', 1051, False),
                                ('port_query_req.bin', 12523, True), ('port_query_resp.bin', 12523, False)):
    data = stream(find(port, from_client))
    open(os.path.join(outdir, name), 'wb').write(data)
    print(name, len(data))
