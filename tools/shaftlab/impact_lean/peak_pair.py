import json,glob,math,statistics as st,sys
ROOT=sys.argv[1]; C='/mnt/swingdata/corpus'
def wrap(d): return (d+180.0)%360.0-180.0
def L(th): return math.degrees(math.remainder(th-math.pi/2,2*math.pi))
def peaks(deg,prof,minsep=4.0,rel=0.35):
    n=len(prof)
    sm=[(prof[max(0,j-1)]+2*prof[j]+prof[min(n-1,j+1)])/4 for j in range(n)]
    mx=max(sm) or 1
    pk=[]
    for j in range(1,n-1):
        if sm[j]>=sm[j-1] and sm[j]>sm[j+1] and sm[j]>=rel*mx:
            # parabolic sub-bin
            a,b,c=sm[j-1],sm[j],sm[j+1]; den=a-2*b+c
            off=0.5*(a-c)/den if den!=0 else 0
            step=wrap(deg[j+1]-deg[j])
            pk.append((deg[j]+off*step,sm[j]))
    pk.sort(key=lambda p:-p[1])
    out=[]
    for p in pk:
        if all(abs(wrap(p[0]-q[0]))>=minsep for q in out): out.append(p)
    return out
rows={}
for d in sorted(glob.glob(ROOT+'/*/')):
    id=d.rstrip('/').split('/')[-1]
    try:
        A=json.load(open(d+'result.json'))['analysis']
        tr={}
        for l in open(d+'trace.jsonl'):
            if '"frame"' in l[:400]:
                t=json.loads(l)
                if 'frame' in t: tr[t['frame']]=t
    except Exception: continue
    rows[id]=(A,tr)
sep=[];rot=[];lead_err=[];trail_err=[];strong_err=[];cont=[];tracker_err=[]
for id,(A,tr) in rows.items():
    sess,sw=id.split('__')
    T=json.load(open(f'{C}/swings/{sess}/{sw}/truth.json')); marks=T.get('shaft') or []
    cs=A['club']['samples']
    th=[math.degrees(s['theta'])%360 for s in cs]
    pp={}
    for f,t in tr.items():
        if 'wrow_deg' not in t or f>=len(cs): continue
        prof=[max(a,b) for a,b in zip(t['wrow_raw'],t.get('wrow_dif') or t['wrow_raw'])]
        pk=peaks(t['wrow_deg'],prof)
        if len(pk)<2: continue
        # direction of rotation from the tracker's own θ over ±1 frame
        if f-1<0 or f+1>=len(th): continue
        w=wrap(th[f+1]-th[f-1])/2
        if abs(w)<2: continue
        sg=1 if w>0 else -1
        a,b=pk[0][0],pk[1][0]
        lead,trail=(a,b) if sg*wrap(a-b)>0 else (b,a)
        pp[f]=(lead,trail,abs(w),pk[0][0])
    for f,(lead,trail,w,strong) in pp.items():
        sep.append(abs(wrap(lead-trail))); rot.append(w)
        if f-1 in pp: cont.append(wrap(trail-pp[f-1][0])*(1 if wrap(lead-trail)>0 else -1))
        m=min(marks,key=lambda m:abs(m['t_us']-cs[f]['t_us'])) if marks else None
        if m and abs(m['t_us']-cs[f]['t_us'])<1500:
            tl=L(m['theta'])
            lead_err.append(wrap(lead-90)-tl); trail_err.append(wrap(trail-90)-tl); strong_err.append(wrap(strong-90)-tl)
            tracker_err.append(wrap(th[f]-90)-tl)
n=len(sep); mx=sum(rot)/n; my=sum(sep)/n
slope=sum((x-mx)*(y-my) for x,y in zip(rot,sep))/sum((x-mx)**2 for x in rot); icpt=my-slope*mx
q=lambda v:f"n={len(v):3d} med {st.median(v):+6.1f} |med| {st.median([abs(x) for x in v]):5.1f}"
print(f"frames with a peak pair: {n}")
print(f"peak separation = {icpt:.1f} + {slope:.2f} × rotation/frame   (a blur's two ends: ≈ 0 + 0.99)")
for lo,hi in ((2,6),(6,10),(10,16),(16,40)):
    g=[(x,y) for x,y in zip(rot,sep) if lo<=x<hi]
    if g: print(f"   rotation {lo:2d}-{hi:2d}°/frame n={len(g):4d}: median separation {st.median(y for _,y in g):5.1f}°, median rotation {st.median(x for x,_ in g):5.1f}°")
print("trail(N) − lead(N−1):",q(cont))
print("vs hand marks: lead",q(lead_err)," | trail",q(trail_err)," | strongest",q(strong_err)," | tracker today",q(tracker_err))
# ── by rotation band: error vs marks, and separation vs rotation ──
print("\nby rotation band (marked frames):")
band=[]
for id,(A,tr) in rows.items():
    sess,sw=id.split('__')
    T=json.load(open(f'{C}/swings/{sess}/{sw}/truth.json')); marks=T.get('shaft') or []
    cs=A['club']['samples']; th=[math.degrees(s['theta'])%360 for s in cs]
    for f,t in tr.items():
        if 'wrow_deg' not in t or f<1 or f+1>=len(cs): continue
        prof=[max(a,b) for a,b in zip(t['wrow_raw'],t.get('wrow_dif') or t['wrow_raw'])]
        pk=peaks(t['wrow_deg'],prof)
        w=wrap(th[f+1]-th[f-1])/2
        if len(pk)<1 or abs(w)<2: continue
        sg=1 if w>0 else -1
        cand=[p[0] for p in pk[:2]]
        lead=max(cand,key=lambda a:sg*wrap(a-th[f]))  if len(cand)>1 else cand[0]
        m=min(marks,key=lambda m:abs(m['t_us']-cs[f]['t_us'])) if marks else None
        if m and abs(m['t_us']-cs[f]['t_us'])<1500:
            tl=L(m['theta']); band.append((abs(w),wrap(lead-90)-tl,wrap(th[f]-90)-tl,len(pk)>=2))
for lo,hi in ((2,6),(6,10),(10,16),(16,40)):
    g=[b for b in band if lo<=b[0]<hi]
    if g: print(f"  rotation {lo:2d}-{hi:2d}°/frame: n={len(g):3d}  lead med {st.median(b[1] for b in g):+5.1f} |med| {st.median(abs(b[1]) for b in g):4.1f}   tracker today med {st.median(b[2] for b in g):+5.1f} |med| {st.median(abs(b[2]) for b in g):4.1f}")
