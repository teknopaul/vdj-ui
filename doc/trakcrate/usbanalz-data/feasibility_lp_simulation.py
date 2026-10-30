import numpy as np, random, time, sys
from scipy.optimize import linprog
rng=np.random.default_rng(7); random.seed(7)
L=150; W=21474
t_true=rng.integers(0,W+1,size=L)                       # t_p for p=1..L (distance from the end)
alpha='abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 '
def make_string(natural):
    if natural:
        a=''.join(random.choice('abcdefghijklmnopqrstuvwxyz ') for _ in range(random.randint(3,30)))
        b=''.join(random.choice('abcdefghijklmnopqrstuvwxyz ') for _ in range(random.randint(3,30)))
        c=''.join(random.choice('abcdefghijklmnopqrstuvwxyz_0123456789') for _ in range(random.randint(3,40)))
    else:
        a=''.join(random.choice(alpha) for _ in range(random.randint(1,48))); b=''.join(random.choice(alpha) for _ in range(random.randint(1,48))); c=''.join(random.choice(alpha) for _ in range(random.randint(1,44)))
    return f'/Contents/{a}/{b}/{c}.mp3'
def row(s):
    r=np.zeros(L)
    for k,ch in enumerate(reversed(s),1):
        if k<=L: r[k-1]=ord(ch)
    return r
def run(n,frac_random):
    S=[make_string(random.random()<frac_random==False if False else (random.random()>frac_random)) for _ in range(n)]
    A=np.array([row(s) for s in S]); X=A@t_true
    q=rng.integers(0,W+1,size=n); R=X-q                        # we only know R: X in [R, R+W]
    # constraints  R <= A t <= R+W
    Aub=np.vstack([A,-A]); bub=np.concatenate([R+W,-R])
    t0=time.time(); widths=[]; est=[]
    for p in range(L):
        c=np.zeros(L); c[p]=1
        lo=linprog(c,A_ub=Aub,b_ub=bub,bounds=(0,W),method='highs'); hi=linprog(-c,A_ub=Aub,b_ub=bub,bounds=(0,W),method='highs')
        widths.append(hi.x[p]-lo.x[p]); est.append((hi.x[p]+lo.x[p])/2)
    err=np.abs(np.array(est)-t_true)
    return np.array(widths),err,time.time()-t0
for n,fr in ((1000,0.5),(3000,0.5),(6000,0.5)):
    w,e,dt=run(n,fr)
    print(f'n={n:5d}: feasible-interval width per weight: median {np.median(w):9.1f} max {w.max():9.1f}   centre error: median {np.median(e):8.1f} max {e.max():8.1f}   ({dt:.0f}s)',flush=True)
