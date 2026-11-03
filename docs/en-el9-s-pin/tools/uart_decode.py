import numpy as np, sys
sys.argv=['x']
exec(open('edges.py').read().split("for k in 'efghij'")[0])
BT=10.4167
def bits_of(ev,i0,i1):
    ev=ev[i0:i1]; s=''
    t0=ev[0][0]
    for a,b in zip(ev,ev[1:]):
        n=int(round((b[0]-a[0])*100/BT))
        s+=('1' if a[1]=='U' else '0')*n
    return s
def uart(s,inv=False,msb=False):
    if inv: s=''.join('1' if c=='0' else '0' for c in s)
    out=[];i=0
    while i<len(s)-9:
        if s[i]=='0' and s[i+9]=='1':
            b=s[i+1:i+9]; b=b if msb else b[::-1]
            out.append(int(b,2));i+=10
        else:i+=1
    return out
for k in 'fghij':
    ev=[x for x in edges(k) if 64<=x[0]<=100]
    # split on long gap
    gaps=[(ev[i+1][0]-ev[i][0],i) for i in range(len(ev)-1)]
    cut=max(gaps)[1]+1
    for name,(a,b) in {'B1':(0,cut),'B2':(cut,len(ev))}.items():
        s=bits_of(ev,a,b)
        print(k,name,len(s));print(' ',s)
        print('  uart',' '.join(f'{x:02X}' for x in uart(s)))
