import json,glob,math,statistics as st,sys
ROOT=sys.argv[1] if len(sys.argv)>1 else '/mnt/swingdata/scratch/lean_rows_win'; C='/mnt/swingdata/corpus'
def wrap(d): return (d+180.0)%360.0-180.0
def lean_of_rad(th): return math.degrees(math.remainder(th-math.pi/2,2*math.pi))

def method_e0(deg,prof,thresh=50.0):
    # today's rule: best contiguous above-threshold run by energy
    best=None;run=None
    for j in range(len(prof)+1):
        up=j<len(prof) and prof[j]>=thresh
        if up:
            if run is None: run=[j,j,0.0]
            run[1]=j; run[2]+=prof[j]
        elif run is not None:
            if best is None or run[2]>best[2]: best=run
            run=None
    if not best: return None
    a,b,_=best
    w=sum(prof[j] for j in range(a,b+1)); c=sum(prof[j]*wrap(deg[j]-deg[a]) for j in range(a,b+1))/w
    return dict(cen=deg[a]+c, lo=deg[a], hi=deg[b], touch=(a==0 or b==len(prof)-1))

def method_half(deg,prof,base='p10',frac=0.5):
    n=len(prof)
    if n<5: return None
    s=sorted(prof)
    B={'p10':s[int(0.10*(n-1))],'p25':s[int(0.25*(n-1))],'min':s[0]}[base]
    k=max(range(n),key=lambda j:prof[j]); Pk=prof[k]
    if Pk<=B: return None
    T=B+frac*(Pk-B)
    a=k
    while a>0 and prof[a-1]>=T: a-=1
    b=k
    while b<n-1 and prof[b+1]>=T: b+=1
    # sub-bin linear interpolation of each crossing
    def xcross(j0,j1):
        p0,p1=prof[j0],prof[j1]
        if p1==p0: return deg[j0]
        return deg[j0]+ (T-p0)/(p1-p0)*wrap(deg[j1]-deg[j0])
    lo=xcross(a-1,a) if a>0 else deg[a]
    hi=xcross(b+1,b) if b<n-1 else deg[b]
    inner=[j for j in range(a,b+1)]
    w=sum(prof[j]-B for j in inner); c=sum((prof[j]-B)*wrap(deg[j]-deg[a]) for j in inner)/w if w>0 else 0
    return dict(cen=deg[a]+c, lo=lo, hi=hi, touch=(a==0 or b==n-1))

METHODS={
 'E0 today (>50)':lambda d,r,f:method_e0(d,[max(x,y) for x,y in zip(r,f)] if f else r),
 'E1 half-max p10 max(raw,dif)':lambda d,r,f:method_half(d,[max(x,y) for x,y in zip(r,f)] if f else r,'p10',0.5),
 'E1 half-max p10 raw':lambda d,r,f:method_half(d,r,'p10',0.5),
 'E1 half-max p10 dif':lambda d,r,f:method_half(d,f,'p10',0.5) if f else None,
 'E2 half-max min dif':lambda d,r,f:method_half(d,f,'min',0.5) if f else None,
 'E2 0.3-max p10 dif':lambda d,r,f:method_half(d,f,'p10',0.3) if f else None,
 'E2 0.7-max p10 dif':lambda d,r,f:method_half(d,f,'p10',0.7) if f else None,
}
swings={}
for d in sorted(glob.glob(ROOT+'/*/')):
    id=d.rstrip('/').split('/')[-1]
    try:
        tr=[json.loads(l) for l in open(d+'trace.jsonl') if l.startswith('{"')]
        A=json.load(open(d+'result.json'))['analysis']
    except Exception as e: continue
    tr=[t for t in tr if 'frame' in t]
    cs=A['club']['samples']
    swings[id]=(tr,cs)
print('swings',len(swings))
for name,fn in METHODS.items():
    widths=[];steps=[];gaps=[];touch=0;nfr=0;lead_err=[];mid_err=[];trail_err=[]
    for id,(tr,cs) in swings.items():
        sess,sw=id.split('__')
        T=json.load(open(f'{C}/swings/{sess}/{sw}/truth.json'))
        marks={m['t_us']:m for m in (T.get('shaft') or [])}
        res={}
        for t in tr:
            if 'wrow_deg' not in t: continue
            r=fn(t['wrow_deg'],t['wrow_raw'],t.get('wrow_dif'))
            if not r: continue
            nfr+=1; touch+=r['touch']; res[t['frame']]=r
        fr=sorted(res)
        for f in fr:
            if f-1 in res and f+1 in res and f < len(cs):
                step=wrap(res[f+1]['cen']-res[f-1]['cen'])/2
                if abs(step)<2 or abs(step)>60: continue
                sg=1 if step>0 else -1
                r=res[f]; lead=r['hi'] if sg>0 else r['lo']; trail=r['lo'] if sg>0 else r['hi']
                width=abs(wrap(r['hi']-r['lo']))
                widths.append(width); steps.append(abs(step))
                p=res[f-1]; plead=p['hi'] if sg>0 else p['lo']
                gaps.append(sg*wrap(trail-plead))
                # truth at this frame's time
                t_us=cs[f]['t_us']
                m=min(marks.values(),key=lambda m:abs(m['t_us']-t_us)) if marks else None
                if m and abs(m['t_us']-t_us)<1500:
                    tl=lean_of_rad(m['theta'])
                    lead_err.append(wrap(lead-90)-tl); mid_err.append(wrap(r['cen']-90)-tl); trail_err.append(wrap(trail-90)-tl)
    if not widths: print(name,'no data'); continue
    n=len(widths); mx=sum(steps)/n; my=sum(widths)/n
    slope=sum((x-mx)*(y-my) for x,y in zip(steps,widths))/max(1e-9,sum((x-mx)**2 for x in steps)); icpt=my-slope*mx
    q=lambda v: f"med {st.median(v):+6.1f} |med| {st.median([abs(x) for x in v]):5.1f}" if v else 'n=0'
    print(f"\n{name}: frames {nfr}, envelope-edge hits {100*touch/max(1,nfr):.0f}%")
    print(f"   width = {icpt:5.1f} + {slope:4.2f}×rotation   (want ≈ 0 + 0.99)   width/rotation median {st.median([w/s for w,s in zip(widths,steps)]):.2f}")
    print(f"   trail(N) − lead(N−1): {q(gaps)}   (want ≈ 0)")
    print(f"   vs hand marks (n={len(lead_err)}): lead {q(lead_err)}   centre {q(mid_err)}   trail {q(trail_err)}")
