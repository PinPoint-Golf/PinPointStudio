#!/usr/bin/env python3
"""A/B a session's down-the-line track between two write-back trees (docs/research/data/markerless/
dtl_track_fragility_20261005.md §4).

  dtl_session_ab_grade.py RUN_ROOT [CTL_ARM] [ON_ARM] [SESSION_REL]

RUN_ROOT/<arm>/<SESSION_REL>/swing_NNNN are library-shaped copies written back by swinglab_run with
two parameter sets. Per swing: published frames pointing down the lead arm near the top (θ 0–90°
between P3 + 50 ms and P5 − 30 ms), published frames in impact … + 150 ms and those at θ ≥ 75° (the
trail leg), drawn / measured length, frames whose tier or θ changed outside those two windows, the
metric count, and the 3-D club's worst consecutive-frame kink.
"""
import sys,math,glob,os,collections,statistics as st,json
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
import pp_swingdoc as P
R=sys.argv[1]; CA=sys.argv[2] if len(sys.argv)>2 else 't_ctl'; OA=sys.argv[3] if len(sys.argv)>3 else 't_on'; SES=sys.argv[4] if len(sys.argv)>4 else 'Mark-Liversedge/2026-10-05_Mark-Liversedge_Wrist_01'
pub=lambda f:f.get('theta') is not None
def ang(a,b): return math.degrees(math.acos(max(-1,min(1,sum(x*y for x,y in zip(a,b))))))
T=collections.Counter(); per=[]; kink={'ctl':{'top':[],'post':[]},'on':{'top':[],'post':[]}}; mets=[]
for sd in sorted(glob.glob(f'{R}/{CA}/{SES}/swing_*')):
    n=os.path.basename(sd)
    try: A=P.load_swing(sd)['analysis']; B=P.load_swing(f'{R}/{OA}/{SES}/{n}')['analysis']
    except Exception as e: print(n,'load fail',e); continue
    ph={p['phase']:p['t_us'] for p in A.get('phases',[])}
    if not (A.get('clubDtl') or {}).get('frames') or not all(k in ph for k in (8,2,13,5)): print(n,'skipped (no track/phases)'); continue
    fa,fb=A['clubDtl']['frames'],B['clubDtl']['frames']
    row={'n':n}
    for lab,fr in (('ctl',fa),('on',fb)):
        top=[f for f in fr if ph[8]+50000<f['t_us']<ph[13]-30000 and pub(f)]
        arm=[f for f in top if 0<=math.degrees(f['theta'])%360<=90]
        post=[f for f in fr if ph[5]<f['t_us']<=ph[5]+150000 and pub(f)]
        leg=[f for f in post if math.degrees(f['theta'])%360>=75]
        row.setdefault('len',{})[lab]=st.median([f['lenPx'] for f in post if f.get('lenPx')]) if post else None
        ratio=[f['lenPx']/f['runPx'] for f in post if f.get('runPx') and f.get('lenPx')]
        row[lab]=dict(arm=len(arm),top=len(top),post=len(post),leg=len(leg),ratio=st.median(ratio) if ratio else None,pubAll=sum(map(pub,fr)))
        T[lab+'_arm']+=len(arm); T[lab+'_leg']+=len(leg); T[lab+'_post']+=len(post); T[lab+'_pub']+=sum(map(pub,fr)); T[lab+'_armSw']+=1 if arm else 0; T[lab+'_legSw']+=1 if leg else 0
    coll=0
    for a,b in zip(fa,fb):
        if (a['tier'],a.get('theta'))==(b['tier'],b.get('theta')): continue
        t=a['t_us']
        if ph[8]+50000<t<ph[13]-30000 or ph[5]<t<=ph[5]+150000: continue
        coll+=1
    row['collateral']=coll; T['collateral']+=coll
    mets.append((n,len(A.get('metrics') or []),len(B.get('metrics') or [])))
    for lab,X in (('ctl',A),('on',B)):
        sk=X.get('skeleton3d') or {}
        if not sk.get('valid'): continue
        fr=sk['frames']
        def k(lo,hi):
            v=[]
            for i in range(1,len(fr)-1):
                if not lo<=fr[i]['t']<=hi: continue
                p,c,q=fr[i-1],fr[i],fr[i+1]
                w1=[(c['u'][j]-p['u'][j])/max(1,(c['t']-p['t'])/1000) for j in range(3)]; w2=[(q['u'][j]-c['u'][j])/max(1,(q['t']-c['t'])/1000) for j in range(3)]
                v.append(math.degrees(math.sqrt(sum((x-y)**2 for x,y in zip(w1,w2)))))
            return max(v) if v else None
        a_=k(ph[2]-80000,ph[2]+80000); b_=k(ph[5]+10000,ph[5]+150000)
        if a_ is not None: kink[lab]['top'].append(a_)
        if b_ is not None: kink[lab]['post'].append(b_)
    per.append(row)
print('swing | arm-at-top frames ctl→on | post-impact published ctl→on | leg (θ≥75°) ctl→on | drawn/run ctl→on | changed elsewhere')
for r in per:
    c,o=r['ctl'],r['on']; f=lambda v: f'{v:.2f}' if v else ' - '
    print(f"{r['n'][-4:]} | {c['arm']:2d}→{o['arm']:2d} | {c['post']:2d}→{o['post']:2d} | {c['leg']:2d}→{o['leg']:2d} | {f(c['ratio'])}→{f(o['ratio'])} | {r['collateral']}")
print('TOTALS',dict(T))
lc=[r['len']['ctl'] for r in per if r['len'].get('ctl')]; lo=[r['len']['on'] for r in per if r['len'].get('on')]
print('post-impact drawn length, median of per-swing medians: ctl %.0f px  on %.0f px'%(st.median(lc),st.median(lo)))
print('metric count changes:',[m for m in mets if m[1]!=m[2]] or 'none')
for w in ('top','post'):
    print(f"3-D club worst kink {w}: ctl median {st.median(kink['ctl'][w]):.2f} max {max(kink['ctl'][w]):.2f} | on median {st.median(kink['on'][w]):.2f} max {max(kink['on'][w]):.2f}  (deg/ms, n={len(kink['on'][w])})")
