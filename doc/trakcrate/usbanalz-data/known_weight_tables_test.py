import json, math, sys
from math import isqrt
S='/tmp/claude-1000/-home-teknopaul-bzr-workspace-vdj-ui/f0b98ced-4719-4adb-83cc-f3ba6cfa87f7/scratchpad'
d=json.load(open(S+'/e4rows.json')); Pa,Ya=d['base']; rows={j:(b,c) for j,b,c in d['rows']}
M=200003; K=102874; T=1<<32
db={j:(rows[j][0][1]-Ya)%M for j in rows}; dc={j:(rows[j][1][1]-Ya)%M for j in rows}
def ok(j,w):
    if w%M not in (db[j],(db[j]+K)%M): return False
    r2=(2*w%T)%M
    return any(r2==(dc[j]+k*K)%M for k in (0,1,2))
# export4: stem letter j has 1-based distance d = 144 - j from the end; positions j=96..139 -> d=5..48 ; album j=48..95 -> d=50..97 ; artist 0..47 -> d=99..146 (separators in between)
def dist(j): return 144-j if j>=96 else (144-j-1 if j>=48 else 144-j-2)
J=sorted(rows)
def score(G, base):      # G[k] = weight for a given index; weight at distance d is G[base+d] (forward) 
    hits=0
    for j in J:
        d=dist(j); i=base+d
        if 0<=i<len(G) and ok(j,G[i]%T): hits+=1
    return hits
def best(G,name):
    res=[]
    for rev in (False,True):
        g=G[::-1] if rev else G
        for base in range(-60,len(g)-1):
            h=score(g,base)
            if h>=2: res.append((h,rev,base))
    res.sort(reverse=True)
    print(f'{name:38s} best: {res[:3] if res else "-"}',flush=True)
    return res
N=420
# ---- generators
def glibc_rand(seed,n):                   # glibc TYPE_3 (srand/rand), outputs 31 bit
    r=[0]*34; r[0]=seed or 1
    for i in range(1,31):
        hi,lo=divmod(r[i-1],127773); w=16807*lo-2836*hi
        r[i]=w+2147483647 if w<0 else w
    for i in range(31,34): r[i]=r[i-31]
    out=[]; r+= [0]*(n+400)
    for i in range(34,34+n+310): r[i]=(r[i-31]+r[i-3])&0xffffffff
    return [(r[i]>>1) for i in range(344,344+n)]
def mt19937(seed,n):
    mt=[0]*624; mt[0]=seed&0xffffffff
    for i in range(1,624): mt[i]=(1812433253*(mt[i-1]^(mt[i-1]>>30))+i)&0xffffffff
    idx=624; out=[]
    for _ in range(n):
        if idx>=624:
            for k in range(624):
                y=(mt[k]&0x80000000)|(mt[(k+1)%624]&0x7fffffff)
                mt[k]=mt[(k+397)%624]^(y>>1)^(0x9908b0df if y&1 else 0)
            idx=0
        y=mt[idx]; idx+=1
        y^=y>>11; y^=(y<<7)&0x9d2c5680; y^=(y<<15)&0xefc60000; y^=y>>18
        out.append(y&0xffffffff)
    return out
def lcg(a,c,seed,n,mod=1<<32,shift=0):
    x=seed; out=[]
    for _ in range(n): x=(a*x+c)%mod; out.append((x>>shift)&0xffffffff)
    return out
def xorshift32(seed,n):
    x=seed; out=[]
    for _ in range(n):
        x^=(x<<13)&0xffffffff; x^=x>>17; x^=(x<<5)&0xffffffff; out.append(x)
    return out
def pcg32(seed,inc,n):
    st=0; inc=(inc<<1)|1; MASK=(1<<64)-1; out=[]
    def step():
        nonlocal st
        old=st; st=(old*6364136223846793005+inc)&MASK
        xs=(((old>>18)^old)>>27)&0xffffffff; rot=old>>59
        return ((xs>>rot)|(xs<<((-rot)&31)))&0xffffffff
    step(); st=(st+seed)&MASK; step()
    return [step() for _ in range(n)]
# ---- constants: binary expansions in 32 bit chunks
def chunks(frac_num,frac_den_bits,n=N):
    # frac_num / 2^frac_den_bits is the fractional part of the constant
    out=[]
    x=frac_num
    for i in range(n):
        shift=frac_den_bits-32*(i+1)
        out.append((x>>shift)&0xffffffff)
    return out
BITS=32*N+64
def sqrt_frac(k): v=isqrt(k<<(2*BITS)); return v&((1<<BITS)-1)
def cbrt_frac(k):
    lo,hi=0,(k+1)<<BITS
    target=k<<(3*BITS)
    while lo<hi:
        mid=(lo+hi+1)//2
        if mid**3<=target: lo=mid
        else: hi=mid-1
    return lo&((1<<BITS)-1)
def pi_frac():
    # Machin: pi = 16 atan(1/5) - 4 atan(1/239) with BITS+64 guard bits
    P=BITS+64; one=1<<P
    def atan_inv(x):
        s=t=one//x; n=1; x2=x*x; sign=-1
        while t:
            t//=x2; n+=2; s+=sign*(t//n); sign=-sign
        return s
    pi=16*atan_inv(5)-4*atan_inv(239)
    return (pi>>64)&((1<<BITS)-1)
def e_frac():
    P=BITS+64; one=1<<P; s=0; t=one; n=0
    while t: s+=t; n+=1; t//=n
    return (s>>64)&((1<<BITS)-1)
def ln2_frac():
    P=BITS+64; one=1<<P
    # ln2 = sum 1/(k 2^k)
    s=0; k=1
    while True:
        term=one//(k<<k)
        if not term: break
        s+=term; k+=1
    return (s>>64)&((1<<BITS)-1)
tests={}
tests['glibc rand() srand(1)']=glibc_rand(1,N)
tests['glibc rand() srand(0)']=glibc_rand(0,N)
tests['glibc rand()<<1']=[(x<<1)&0xffffffff for x in glibc_rand(1,N)]
tests['mt19937 default seed 5489']=mt19937(5489,N)
tests['mt19937 seed 1']=mt19937(1,N); tests['mt19937 seed 0']=mt19937(0,N)
for s in (1,0,12345):
    tests[f'minstd 16807 seed {s or 1}']=lcg(16807,0,s or 1,N,2147483647)
    tests[f'minstd 48271 seed {s or 1}']=lcg(48271,0,s or 1,N,2147483647)
tests['ANSI C LCG 1103515245/12345']=lcg(1103515245,12345,1,N); tests['ANSI C LCG seed 0']=lcg(1103515245,12345,0,N)
tests['Numerical Recipes LCG']=lcg(1664525,1013904223,0,N); tests['NR LCG seed 1']=lcg(1664525,1013904223,1,N)
tests['MSVC LCG 214013/2531011']=lcg(214013,2531011,0,N); tests['MSVC LCG seed 1']=lcg(214013,2531011,1,N)
tests['Borland 22695477/1']=lcg(22695477,1,0,N); tests['Delphi 134775813/1']=lcg(134775813,1,0,N); tests['69069/1']=lcg(69069,1,0,N)
tests['xorshift32 seed 1']=xorshift32(1,N); tests['xorshift32 seed 2463534242']=xorshift32(2463534242,N)
tests['PCG32 seed 42']=pcg32(42,54,N)
for name,fr in (('pi',pi_frac()),('e',e_frac()),('ln2',ln2_frac()),('sqrt2',sqrt_frac(2)),('sqrt3',sqrt_frac(3)),('sqrt5 (phi)',sqrt_frac(5)),('cbrt2',cbrt_frac(2)),('cbrt3',cbrt_frac(3))):
    tests['digits of '+name]=chunks(fr,BITS)
tests['MD5 sine table floor(2^32|sin(n)|)']=[int(abs(math.sin(n))*2**32)&0xffffffff for n in range(1,N+1)]
primes=[p for p in range(2,3000) if all(p%q for q in range(2,int(p**0.5)+1))]
tests['SHA256 K: frac(cbrt(prime))']=[ (cbrt_frac(p)>>(BITS-32))&0xffffffff for p in primes[:N]]
tests['SHA256 H: frac(sqrt(prime))']=[ (sqrt_frac(p)>>(BITS-32))&0xffffffff for p in primes[:N]]
tests['frac(sqrt(n))']=[(sqrt_frac(n)>>(BITS-32))&0xffffffff for n in range(2,N+2)]
tests['sin(n^2)']=[int(abs(math.sin(n*n))*2**32)&0xffffffff for n in range(1,N+1)]
tests['n-th prime']=primes[:N]
tests['squares / cubes']=[(n**3)&0xffffffff for n in range(N)]
for name,G in tests.items(): best(G,name)
