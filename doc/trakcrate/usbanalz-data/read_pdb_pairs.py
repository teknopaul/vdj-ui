import struct,sys
def s_at(d,p):
    k=d[p]
    if k&1: return d[p+1:p+(k>>1)].decode('latin1')
    if k==0x40: ln=struct.unpack('<H',d[p+1:p+3])[0]; return d[p+4:p+ln].decode('latin1')
    if k==0x90: ln=struct.unpack('<H',d[p+1:p+3])[0]; return d[p+4:p+ln].decode('utf-16le','replace')
    return None
def pairs(path):
    d=open(path,'rb').read(); ps=struct.unpack('<I',d[4:8])[0]
    t,e,f,l=struct.unpack('<IIII',d[28:44])
    out=[]; pg=f; seen=set()
    while pg and pg not in seen and pg*ps<len(d):
        seen.add(pg); o=pg*ps
        nxt=struct.unpack('<I',d[o+12:o+16])[0]
        if d[o+8]==0 and not (d[o+0x1b]&0x40):
            nrow=d[o+0x18]; end=o+ps
            for g in range((nrow+15)//16):
                base=end-g*0x24; pres=struct.unpack('<H',d[base-4:base-2])[0]
                for r in range(16):
                    i=g*16+r
                    if i>=nrow: break
                    if pres>>r&1:
                        ro=o+0x28+struct.unpack('<H',d[base-6-2*r:base-4-2*r])[0]
                        if struct.unpack('<H',d[ro:ro+2])[0]!=0x24: continue
                        offs=struct.unpack_from('<21H',d,ro+94)
                        a=s_at(d,ro+offs[14]); b=s_at(d,ro+offs[20]); tid=struct.unpack_from('<I',d,ro+72)[0]
                        out.append((tid,a,b))
        if pg==l: break
        pg=nxt
    return out
if __name__=='__main__':
    for p in sys.argv[1:]:
        r=pairs(p); print(p,len(r))
        for x in r[:4]: print('  ',x)
