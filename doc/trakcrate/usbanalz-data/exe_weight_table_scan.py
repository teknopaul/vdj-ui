import numpy as np, json, sys, time
S='/tmp/claude-1000/-home-teknopaul-bzr-workspace-vdj-ui/f0b98ced-4719-4adb-83cc-f3ba6cfa87f7/scratchpad'
d=json.load(open(S+'/e4rows.json')); Pa,Ya=d['base']; rows={j:(b,c) for j,b,c in d['rows']}
M=200003; K=102874
db={j:(rows[j][0][1]-Ya)%M for j in rows}
def dist(j): return 144-j if j>=96 else (144-j-1 if j>=48 else 144-j-2)     # 1-based distance of the letter from the end of the export4 path
res={}                                    # residue value -> list of distances
for j in rows:
    for r in (db[j],(db[j]+K)%M): res.setdefault(r,[]).append(dist(j))
keys=np.array(sorted(res),dtype=np.int64)
data=np.fromfile('/var/tmp/rekordbox.exe',dtype=np.uint8)
print('file bytes',len(data),flush=True)
found=[]
for align in range(4):
    n=(len(data)-align)//4
    w=data[align:align+4*n].view('<u4').astype(np.int64)
    r=w%M
    hit=np.isin(r,keys)
    idx=np.nonzero(hit)[0]
    print(f'alignment {align}: {n} words, {len(idx)} words with a matching residue (chance ~{n*len(keys)/M:.0f})',flush=True)
    # runs: word i has distance set D_i, word i+s has D_{i+s} with distance +-1 each step
    pos={int(i):set(res[int(r[i])]) for i in idx}
    for stride in (1,2):                  # stride 2 = 64-bit table entries
        for i,D in pos.items():
            for step in (1,-1):
                cur=D; k=0; length=1
                while True:
                    nxt=pos.get(i+stride*(k+1))
                    if nxt is None: break
                    cur2={x+step for x in cur}&nxt
                    if not cur2: break
                    cur=cur2; k+=1; length+=1
                if length>=3: found.append((length,align*1+i*4,stride,step,sorted(cur)[:3]))
found.sort(reverse=True)
print('runs of >=3 consecutive table entries that fit the measured weights:',found[:10] if found else 'none')
