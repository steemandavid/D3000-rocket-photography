import numpy as np
D='/storage/fileshare/d3000-scope-captures/NewFile0%s.csv'
def edges(k,thr=0.45):
    d=np.loadtxt(D%k,delimiter=',',skiprows=2,usecols=(0,1));t,v=d[:,0]*1e3,d[:,1]
    dv=v[2:]-v[:-2]; ev=[];last=-9
    i=0
    while i<len(dv):
        if abs(dv[i])>thr and t[i+1]-last>0.03:
            # refine: the max |dv| in next 3 samples
            j=i+int(np.argmax(abs(dv[i:i+3])))
            ev.append((round(t[j+1],3),'U' if dv[j]>0 else 'D'));last=t[j+1];i=j+3
        else:i+=1
    return ev
for k in 'efghij':
    ev=edges(k)
    lo=-12 if k=='e' else 64
    ev=[x for x in ev if lo<=x[0]<=(30 if k=='e' else 100)]
    print(k,len(ev),ev[0][0],ev[-1][0])
    t0=ev[0][0]
    print(' '.join(f"{x[1]}{int(round((x[0]-t0)*100))}" for x in ev))
