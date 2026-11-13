"""Plane stack D2: summarise d2_upper_arm.py's rows (argv[1]): per golfer, golfer-weighted percentiles,
within-session SD, the shoulder-views check, coverage, and candidate norms' firing counts."""
import json, sys, numpy as np, collections
R=[json.loads(l) for l in open(sys.argv[1])]
G=['Mark','Nick','Jess','Paddy','Chris']
def q(v):
    v=np.array(v); 
    return f"{len(v)}: {np.median(v):+.1f} [{np.percentile(v,25):+.1f}, {np.percentile(v,75):+.1f}] p10 {np.percentile(v,10):+.1f} p90 {np.percentile(v,90):+.1f}" if len(v) else '-'
for key in ['b_el','b_wr','a2_el','a2_wr','b_el_sigma','b_trailConf','bend3d','bend3d_t3','bend3d_t3_p1','svd']:
    print(key)
    for g in G:
        v=[r[key] for r in R if r['golfer']==g and key in r]
        print(f"   {g:6s} {q(v)}")
# golfer-weighted percentiles (each golfer equal weight), excl Chris
def gw(key, excl=('Chris',)):
    vals=[];w=[]
    for g in G:
        if g in excl: continue
        v=[r[key] for r in R if r['golfer']==g and key in r]
        for x in v: vals.append(x); w.append(1.0/len(v))
    o=np.argsort(vals); vals=np.array(vals)[o]; w=np.array(w)[o]; c=np.cumsum(w)/w.sum()
    p=lambda f: vals[np.searchsorted(c,f)]
    return f"golfer-weighted median {p(0.5):+.1f} p10 {p(0.1):+.1f} p90 {p(0.9):+.1f} p2.5 {p(0.025):+.1f} p97.5 {p(0.975):+.1f}"
for key in ['b_el','a2_el','b_wr','a2_wr']:
    print(key, gw(key))
# pooled
for key in ['b_el','a2_el']:
    v=[r[key] for r in R if key in r and r['golfer']!='Chris']; v=np.array(v)
    print(key,'pooled excl Chris', len(v), 'p2.5 %.1f p10 %.1f med %.1f p90 %.1f p97.5 %.1f'%tuple(np.percentile(v,[2.5,10,50,90,97.5])))
# within-session SD
for key in ['b_el','a2_el']:
    by=collections.defaultdict(list)
    for r in R:
        if key in r and r['golfer']!='Chris': by[r['session']].append(r[key])
    sds=[np.std(v,ddof=1) for v in by.values() if len(v)>=3]
    print(key,'within-session SD median %.1f over %d sessions'%(np.median(sds),len(sds)))
# b_el - a2_el
d=[r['b_el']-r['a2_el'] for r in R if 'b_el' in r and 'a2_el' in r and r['golfer']!='Chris']
print('b_el - a2_el', np.median(d), np.percentile(d,[25,75]))
# shoulderViewsDisagree
for g in G:
    v=[r for r in R if r['golfer']==g and 'a2_el' in r]
    f=[r for r in v if r.get('svd',0)>7]
    print(g,'a2_el swings',len(v),'svd fires',len(f), 'no svd', sum(1 for r in v if 'svd' not in r))
# coverage
for g in G:
    rr=[r for r in R if r['golfer']==g]
    print(g,'swings',len(rr),'b_el',sum('b_el' in r for r in rr),'a2_el',sum('a2_el' in r for r in rr),
      'image missing but 3d',sum(('a2_el' in r) and ('b_el' not in r) for r in rr),
      'bend t3',sum('bend3d_t3' in r for r in rr),'bend t3 both',sum('bend3d_t3' in r and 'bend3d_t3_p1' in r for r in rr),
      'top frames t3 share %.0f%%'%(100*sum(r.get('bend_top_t3',0) for r in rr)/max(1,sum(r.get('bend_top_frames',0) for r in rr))))

def count(key, mu, lo, hi):
    out=[]
    for g in G:
        v=np.array([r[key] for r in R if r['golfer']==g and key in r])
        if not len(v): out.append(f"{g} -"); continue
        fl=(v<mu-2*lo).sum(); fh=(v>mu+2*hi).sum(); al=((v<mu-lo)&(v>=mu-2*lo)).sum(); ah=((v>mu+hi)&(v<=mu+2*hi)).sum()
        out.append(f"{g} {len(v)}: fire low {fl} / high {fh}; amber {al}/{ah}")
    return '; '.join(out)
for mu,lo,hi in [(6,6,6),(5,7,7),(6,7,6),(5,8,7),(4,7,7.5),(6,8,7)]:
    print('image', mu,lo,hi, f"corridor {mu-lo}..{mu+hi} fires <{mu-2*lo} >{mu+2*hi}"); print('   ',count('b_el',mu,lo,hi))
for mu,lo,hi in [(7,8,8),(6,9,9),(7,9,9),(6,8,9),(7,9,8)]:
    print('3d', mu,lo,hi, f"corridor {mu-lo}..{mu+hi} fires <{mu-2*lo} >{mu+2*hi}"); print('   ',count('a2_el',mu,lo,hi))
for mu,t in [(15,7),(15,8),(12,8),(15,6),(18,6)]:
    v=[(r['golfer'],r['bend3d_t3']) for r in R if 'bend3d_t3' in r]
    c=collections.Counter(g for g,x in v if x>mu+2*t); a=collections.Counter(g for g,x in v if mu+t<x<=mu+2*t)
    print('bend3d ceiling',mu,t,'fires >',mu+2*t,dict(c),'amber',dict(a))
# within-session rank corr b_el vs a2_el
import scipy.stats as st
by=collections.defaultdict(list)
for r in R:
    if 'b_el' in r and 'a2_el' in r and r['golfer']!='Chris': by[r['session']].append((r['b_el'],r['a2_el']))
xs=[];ys=[]
for s,v in by.items():
    if len(v)<3: continue
    a=st.rankdata([x for x,_ in v]); b=st.rankdata([y for _,y in v])
    xs+=list((a-a.mean())/a.std()); ys+=list((b-b.mean())/b.std())
print('within-session rho b_el vs a2_el', np.corrcoef(xs,ys)[0,1], len(xs))
# per-golfer median b_el - a2_el
for g in G:
    d=[r['b_el']-r['a2_el'] for r in R if r['golfer']==g and 'b_el' in r and 'a2_el' in r]
    print(g,'b_el - a2_el median %.1f'%np.median(d) if d else '-')
# bent elbow vs wrist effect: b_wr - b_el and a2_wr - a2_el vs bend3d
for g in G:
    d=[(r['b_wr']-r['b_el']) for r in R if r['golfer']==g and 'b_wr' in r and 'b_el' in r]
    e=[(r['a2_wr']-r['a2_el']) for r in R if r['golfer']==g and 'a2_wr' in r and 'a2_el' in r]
    print(g,'wrist minus elbow: image %.1f  3d %.1f'%(np.median(d),np.median(e)))
# Mark gap wedge session
for key in ['b_el','a2_el']:
    for sess in sorted(set(r['session'] for r in R if r['golfer']=='Mark')):
        v=[r[key] for r in R if r['session']==sess and key in r]
        if v: print(key, sess, len(v), '%.1f'%np.median(v))
