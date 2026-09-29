import json,math,statistics as st,sys,glob,os
C='/mnt/swingdata/corpus'; B=sys.argv[1]; A=sys.argv[2]
def L(th): return math.degrees(math.remainder(th-math.pi/2,2*math.pi))
def load(root,id):
    try: return json.load(open(f'{root}/{id}/result.json'))['analysis']
    except Exception: return None
ids=sorted(os.path.basename(d.rstrip('/')) for d in glob.glob(A+'/*/'))
PN={0:'P1',12:'P2',8:'P3',2:'P4',13:'P5',9:'P6',5:'P7',14:'P8',11:'P9',7:'P10'}
rows=[]
frame_err={'before':[],'after':[]}; pos_err={'before':{},'after':{}}; lean={'before':[],'after':[]}
speed=[];mcount=[];obs=[]
for id in ids:
    sess,sw=id.split('__'); T=json.load(open(f'{C}/swings/{sess}/{sw}/truth.json'))
    ev=T.get('events',{}); marks=T.get('shaft') or []
    for tag,root in (('before',B),('after',A)):
        R=load(root,id)
        if not R: continue
        cs=R['club']['samples']
        # per-frame tracked θ vs every mark
        for m in marks:
            k=min(range(len(cs)),key=lambda k:abs(cs[k]['t_us']-m['t_us']))
            if abs(cs[k]['t_us']-m['t_us'])<1500:
                frame_err[tag].append((m['t_us'],(((math.degrees(cs[k]['theta'])-math.degrees(m['theta']))+180)%360)-180, ev))
        # P positions vs marked
        for p in R['phases']:
            nm=PN.get(p.get('phase'))
            if nm and f'{nm.lower()}_s' in ev:
                pos_err[tag].setdefault(nm,[]).append((p['t_us']-ev[f'{nm.lower()}_s']*1e6)/1e3)
        # reported impact lean vs truth at marked P7
        rep=next((p['value'] for m in R['metrics'] if m.get('key')=='impactShaftLean' for p in m.get('phaseSamples',[])),None)
        if 'p7_s' in ev and marks and rep is not None:
            p7=ev['p7_s']*1e6
            b=[m for m in marks if m['t_us']<=p7]; a=[m for m in marks if m['t_us']>=p7]
            if b and a and a[0]['t_us']-b[-1]['t_us']<=20000:
                m0,m1=b[-1],a[0]; w=(p7-m0['t_us'])/max(1,m1['t_us']-m0['t_us']); tv=L(m0['theta'])+w*(L(m1['theta'])-L(m0['theta']))
            else:
                m=min(marks,key=lambda m:abs(m['t_us']-p7)); tv=L(m['theta']) if abs(m['t_us']-p7)<8000 else None
            if tv is not None: lean[tag].append((id,rep-tv))
    Rb,Ra=load(B,id),load(A,id)
    if Rb and Ra:
        g=lambda R,k:next((p['value'] for m in R['metrics'] if m.get('key')==k for p in m.get('phaseSamples',[])),None)
        speed.append((id,g(Rb,'clubheadSpeed'),g(Ra,'clubheadSpeed')))
        mcount.append((id,len(Rb['metrics']),len(Ra['metrics'])))
        wo=Ra['club'].get('wedgeObs',[]); obs.append((id,len(wo),sum(1 for o in wo if o[2]==0)))
q=lambda v:f"n={len(v):3d} med {st.median(v):+6.1f} |med| {st.median([abs(x) for x in v]):5.1f} p90|.| {sorted(abs(x) for x in v)[int(0.9*(len(v)-1))]:5.1f}" if v else 'n=0'
print("IMPACT LEAN (reported at pipeline impact − marked lean at P7)")
for t in ('before','after'): print(f"  {t:6s}", q([e for _,e in lean[t]]))
bl=dict(lean['before']); al=dict(lean['after'])
print("  per swing (before → after):", ", ".join(f"{i.split('__')[0][5:10]}/{i[-2:]} {bl[i]:+.0f}→{al[i]:+.0f}" for i in bl if i in al))
print("\nTRACKED θ vs EVERY HAND-MARKED FRAME, by phase region")
def region(t,ev):
    if 'p4_s' in ev and t<ev['p4_s']*1e6: return 'P1-P4'
    if 'p6_s' in ev and t<ev['p6_s']*1e6: return 'P4-P6'
    if 'p8_s' in ev and t<=ev['p8_s']*1e6: return 'P6-P8'
    return 'after P8'
for reg in ('P1-P4','P4-P6','P6-P8','after P8'):
    for t in ('before','after'):
        v=[e for (tt,e,ev) in frame_err[t] if region(tt,ev)==reg]
        print(f"  {reg:8s} {t:6s}", q(v))
print("\nP-POSITION TIMES vs marked (ms): median |err|  before → after")
for nm in ('P1','P2','P3','P4','P5','P6','P7','P8','P10'):
    b=pos_err['before'].get(nm,[]); a=pos_err['after'].get(nm,[])
    if b and a: print(f"  {nm:3s} n={len(b):2d}/{len(a):2d}  med {st.median(b):+6.1f} → {st.median(a):+6.1f}   |med| {st.median([abs(x) for x in b]):5.1f} → {st.median([abs(x) for x in a]):5.1f}")
d=[(a-b) for _,b,a in speed if a is not None and b is not None]
print("\nCLUBHEAD SPEED after − before (mph):", q(d), " missing before/after:", sum(1 for _,b,a in speed if b is None), sum(1 for _,b,a in speed if a is None))
print("METRIC COUNT changes:", [(i[-18:],b,a) for i,b,a in mcount if a!=b] or 'none')
print("WEDGE OBSERVATIONS per swing: median", st.median(n for _,n,_ in obs), " total", sum(n for _,n,_ in obs), " with a trailing edge:", sum(t for _,_,t in obs))
