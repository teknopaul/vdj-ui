"""Recover exact 32-bit position weights from  Y_k = ((H_a + k*w) mod 2^32) mod M  for several letter offsets k (k = code - 97)."""
import random
M=200003; T=1<<32; K=T%M; Kinv=pow(K,-1,M)
def floordiv(a,b): return a//b
def small(z,lo,hi):                 # z known mod M -> the integer in [lo,hi] (or None)
    for v in range(lo,hi+1):
        if (v-z)%M==0: return v
    return None
def position_options(Ya,obs):
    """obs: {k: Y_k}. returns list of (u, {k: z_k}) consistent options (u = w mod M)"""
    out=[]
    db=(obs[1]-Ya)%M
    for u in {db,(db+K)%M}:
        zs={}
        for k,Yk in obs.items():
            z=((Ya+k*u-Yk)*Kinv)%M
            lo,hi=(0,k) if k>0 else (k,0)
            v=small(z,lo,hi)
            if v is None: zs=None; break
            zs[k]=v
        if zs is not None: out.append((u,zs))
    return out
def t_interval(Ha,u,zs):
    lo,hi=0,T//M            # w = u + M t, w < 2^32
    hi=(T-1-u)//M
    for k,z in zs.items():
        if k>0:   # z*T <= Ha + k*u + k*M*t < (z+1)*T
            a=z*T-Ha-k*u; b=(z+1)*T-Ha-k*u
            tl=-((-a)//(k*M)); th=-((-b)//(k*M))-1
        else:
            kk=-k     # z*T <= Ha - kk*u - kk*M*t < (z+1)*T
            a=Ha-kk*u-(z+1)*T; b=Ha-kk*u-z*T      # kk*M*t in (a, b]
            tl=a//(kk*M)+1; th=b//(kk*M)
        lo=max(lo,tl); hi=min(hi,th)
        if lo>hi: return None
    return lo,hi
def simulate(KS,npos=30,seed=1):
    random.seed(seed)
    Ha=random.randrange(T); Ya=Ha%M
    ws=[random.randrange(T) for _ in range(npos)]
    obs=[{k:((Ha+k*w)%T)%M for k in KS} for w in ws]
    return Ha,Ya,ws,obs
def solve(Ya,obs,verbose=False):
    opts=[position_options(Ya,o) for o in obs]
    if any(not o for o in opts): return None,'no consistent u at some position'
    survivors=[]
    for q in range(T//M+1):
        Ha=Ya+M*q
        if Ha>=T: break
        ok=True; widths=[]
        for o in opts:
            best=None
            for u,zs in o:
                iv=t_interval(Ha,u,zs)
                if iv: best=(u,iv); break
            if best is None: ok=False; break
            widths.append(best[1][1]-best[1][0]+1)
        if ok: survivors.append((Ha,widths))
    return survivors,opts
if __name__=='__main__':
    import sys
    for KS in ([1,2],[1,2,25,-49,-7],[1,2,25,-49,-7,12,-20,-40],[1,2,25,-49,-7,12,-20,-40,-30,-10,5]):
        Ha,Ya,ws,obs=simulate(KS,npos=40)
        surv,opts=solve(Ya,obs)
        if surv is None: print(KS,opts); continue
        trueq=(Ha-Ya)//M
        print('letter offsets',KS,'-> surviving base values:',len(surv),'true one among them:',any(s[0]==Ha for s in surv),
              ' widths at the true one:',[s[1] for s in surv if s[0]==Ha][:1])
