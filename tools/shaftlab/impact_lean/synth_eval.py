import json,math,statistics as st,sys,glob,os
C='/mnt/swingdata/corpus'
roots=sys.argv[1:]
def wrap(d): return (d+180)%360-180
def region(t,ev):
    if 'p4_s' in ev and t<ev['p4_s']*1e6: return 'P1-P4'
    if 'p6_s' in ev and t<ev['p6_s']*1e6: return 'P4-P6'
    if 'p7_s' in ev and t<=ev['p7_s']*1e6: return 'P6-P7'
    if 'p8_s' in ev and t<=ev['p8_s']*1e6: return 'P7-P8'
    return 'after P8'
REG=['P1-P4','P4-P6','P6-P7','P7-P8','after P8']
res={}
for root in roots:
    err={r:[] for r in REG}; acc=[]; metr={}; lp=0
    for d in sorted(glob.glob(root+'/*/')):
        id=os.path.basename(d.rstrip('/')); sess,sw=id.split('__')
        try: A=json.load(open(d+'result.json'))['analysis']
        except Exception: continue
        T=json.load(open(f'{C}/swings/{sess}/{sw}/truth.json')); ev=T.get('events',{})
        sy=A['club'].get('synth',[])
        if len(sy)<3: continue
        ts=[s['t_us'] for s in sy]; th=[math.degrees(s['theta']) for s in sy]
        for i in range(1,len(th)): th[i]=th[i-1]+wrap(th[i]-th[i-1])
        for m in T.get('shaft') or []:
            t=m['t_us']
            k=next((i for i in range(1,len(ts)) if ts[i-1]<=t<=ts[i] and ts[i]-ts[i-1]<20000),None)
            if k is None: continue
            u=(t-ts[k-1])/max(1,ts[k]-ts[k-1]); v=th[k-1]+u*(th[k]-th[k-1])
            err[region(t,ev)].append(wrap(v-math.degrees(m['theta'])))
        # plausibility: peak |θ̈| within ±150 ms of impact (deg/s²)
        imp=next((p['t_us'] for p in A['phases'] if p.get('phase')==5),None)
        if imp:
            for i in range(1,len(ts)-1):
                if abs(ts[i]-imp)>150000: continue
                h0=(ts[i]-ts[i-1])*1e-6; h1=(ts[i+1]-ts[i])*1e-6
                if h0<=0 or h1<=0 or h0>0.02 or h1>0.02: continue
                a=2*((th[i+1]-th[i])/h1-(th[i]-th[i-1])/h0)/(h0+h1)
                if abs(ts[i]-imp)>6000: acc.append(abs(a))
        g=lambda k:next((p['value'] for m in A['metrics'] if m.get('key')==k for p in m.get('phaseSamples',[])),None)
        metr[id]={k:g(k) for k in ('clubheadSpeed','handSpeed','lagAngle','impactShaftLean')}
        lp+=any(m.get('key')=='lowPointAhead' for m in A['metrics'])
    res[root]=(err,acc,metr,lp)
q=lambda v:f"{st.median(v):+5.1f} |{st.median([abs(x) for x in v]):4.1f}| p90 {sorted(abs(x) for x in v)[int(0.9*(len(v)-1))]:5.1f} n={len(v)}" if v else "n=0"
name=lambda r:os.path.basename(r.rstrip('/'))
print("SYNTH θ vs hand marks: median |median| p90|.|")
for r in REG:
    print(f"  {r:9s}"+"".join(f"  {name(root)[:16]:16s} {q(res[root][0][r]):32s}" for root in roots))
print("\nPLAUSIBILITY: peak |θ̈| within ±150 ms of impact, excluding the contact break (°/s²): median / p90")
for root in roots:
    a=res[root][1]
    if a: print(f"  {name(root):20s} median {st.median(a):9.0f}  p90 {sorted(a)[int(0.9*(len(a)-1))]:9.0f}")
base=roots[0]
print("\nMETRIC SHIFTS vs",name(base),"(median, |median|)")
for root in roots[1:]:
    for k in ('clubheadSpeed','handSpeed','lagAngle','impactShaftLean'):
        d=[res[root][2][i][k]-res[base][2][i][k] for i in res[base][2] if i in res[root][2] and res[root][2][i][k] is not None and res[base][2][i][k] is not None]
        if d: print(f"  {name(root):20s} {k:16s} {st.median(d):+6.2f} |{st.median([abs(x) for x in d]):5.2f}|  n={len(d)}")
    print(f"  {name(root):20s} lowPointAhead present on {res[root][3]} swings (baseline {res[base][3]})")
