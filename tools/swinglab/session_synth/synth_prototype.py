"""synth_prototype.py - the session-synthesis prototype behind
docs/design/swing_3d_session_synthesis_design.md. Read-only: it reads one session's swing documents
and prints the numbers the design note quotes. Run it through run.sh, one session at a time.

    tools/swinglab/session_synth/run.sh 2026-10-08_Mark-Liversedge_Wrist_01

The sections before "APPROVED RECIPE" are the exploration that shaped the recipe; the last section
is the recipe as approved, and is the reference the C++ synthesis is checked against.
"""
import sys,time,os,json
import numpy as np
sys.path.insert(0,os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')))
from pp_swingdoc import load_swing
R=json.load(open(sys.argv[2])); N=R['N']; names=[j['name'] for j in R['joints']]; J={n:i for i,n in enumerate(names)}
ND=len(R['dofs'])
def qm(a,b):
    aw,ax,ay,az=a;bw,bx,by,bz=b
    return np.array([aw*bw-ax*bx-ay*by-az*bz,aw*bx+ax*bw+ay*bz-az*by,aw*by-ax*bz+ay*bw+az*bx,aw*bz+ax*by-ay*bx+az*bw])
def qrot(q,v):
    w,u=q[0],q[1:]; t=2*np.cross(u,v); return v+w*t+np.cross(u,t)
def qaa(ax,ang):
    ax=np.asarray(ax,float); ax=ax/np.linalg.norm(ax); s=np.sin(ang/2); return np.array([np.cos(ang/2),*(ax*s)])
def norm(q): return q/np.linalg.norm(q)
def scene_rot(yawdeg): return norm(qm(qaa([1,0,0],-np.pi/2),qaa([0,0,1],-np.radians(yawdeg))))
def root_q_world(th):
    r=np.array([1.,0,0,0])
    j0=R['joints'][0]
    for k in range(j0['first'],j0['first']+j0['nd']):
        d=R['dofs'][k]
        if d['kind']==1: r=qm(r,qaa(d['axis'],th[k]))
    return r
def root_local(q_outer):  # outer r * rigToWorld * restQ*neutral
    j0=R['joints'][0]
    return norm(qm(qm(qm(q_outer,R['rigToWorld']),j0['q']),j0['n']))
def fk_scene(th,scale,root_pos,root_rot_local):
    """th: DOFs; root given in the scene frame. Returns positions N×3, world rots."""
    pos=np.zeros((N,3));rot=[None]*N
    pos[0]=root_pos;rot[0]=root_rot_local
    for j in range(1,N):
        Jj=R['joints'][j];p=Jj['parent'];g=Jj['group'];s=scale[g] if g>=0 else 1.
        pos[j]=pos[p]+qrot(rot[p],np.array(Jj['t'])*s)
        w=qm(qm(rot[p],Jj['q']),Jj['n'])
        for k in range(Jj['first'],Jj['first']+Jj['nd']) if Jj['nd']>0 and Jj['first']>=0 else []:
            w=qm(w,qaa(R['dofs'][k]['axis'],th[k]))
        rot[j]=norm(w)
    return pos,rot
def markley(qs,w):
    A=np.zeros((4,4))
    for q,wi in zip(qs,w): A+=wi*np.outer(q,q)
    v=np.linalg.eigh(A)[1][:,-1]
    return v if v[0]>=0 else -v
def load(path):
    t0=time.time(); d=load_swing(path); tl=time.time()-t0
    a=d['analysis'];s=a['skeleton3d']
    if not s.get('valid'): return None
    frames=s['frames']; T=np.array([f['t'] for f in frames],float)
    D=np.array([f['d'] for f in frames],float)
    P=np.array([f['p'] for f in frames],float).reshape(len(frames),N,3)/1000.
    tier=np.array([[int(c) for c in f['tier']] for f in frames])
    G=np.array([f['g'] for f in frames],float); U=np.array([f['u'] for f in frames],float); ST=np.array([f['s'] for f in frames])
    yaw=s['display']['stanceYawDeg']; org=np.array(s['display']['originWorld'],float)
    q=scene_rot(yaw)
    def sp(p): return np.array([qrot(q,x-org) for x in p.reshape(-1,3)]).reshape(p.shape)
    def sd(p): return np.array([qrot(q,x) for x in p.reshape(-1,3)]).reshape(p.shape)
    lens=[s['lengths'][g] for g in R['groups']]
    pos={int(x['p']):float(x['t_us']) for x in (a.get('club') or {}).get('positions',[]) if x.get('t_us',-1)>=0}
    ball=np.array(s['display']['ball']) if s['display'].get('ball') else None
    return dict(id=os.path.basename(path),T=T,D=D,P=sp(P),tier=tier,G=sp(G),U=sd(U),ST=ST,q=q,org=org,lens=np.array(lens),pos=pos,cl=s['grip']['clubLengthM'],grip=s['grip'],gripoff=np.array(s['grip']['offset']),ball=sp(ball) if ball is not None else None,tl=tl,
        dtl=s['dtl'],plane=a.get('club3d',{}).get('planes',{}).get('down',{}).get('offered'),club=d.get('review',{}).get('club'),rating=d.get('review',{}).get('rating'),reproj=s['diagnostics'].get('reprojMedPxFo'),cam=s['camera'])
sess=sys.argv[1]
root='/mnt/swingdata/Mark-Liversedge/'+sess
dirs=sorted(x for x in os.listdir(root) if x.startswith('swing_'))
t0=time.time();S=[]
for d in dirs:
    x=load(os.path.join(root,d))
    if x is not None:S.append(x)
print(f"{sess}: {len(S)} valid swings, load {time.time()-t0:.1f}s; plane offered {sum(bool(s['plane']) for s in S)}")
KEYS=[1,2,3,4,5,6,7,8]
ok=[s for s in S if all(k in s['pos'] for k in KEYS)]
print('swings with all P1..P8:',len(ok),'missing:',{s['id']:[k for k in KEYS if k not in s['pos']] for s in S if s not in ok})
# per-swing FK self-check
s=ok[0];i=len(s['T'])//2;th=s['D'][i]
rpos=s['P'][i,0];rl=norm(qm(s['q'],root_local(root_q_world(th))))
p,_=fk_scene(th,s['lens'],rpos,rl);print('FK self-check max joint err mm',round(1000*np.max(np.linalg.norm(p-s['P'][i],axis=1)),2))
print('length-scale spread (CV%) across swings:',{g:round(100*np.std([s['lens'][i] for s in ok])/np.mean([s['lens'][i] for s in ok]),2) for i,g in enumerate(R['groups'])})
print('club length: med %.3f min %.3f max %.3f'%(np.median([s['cl'] for s in ok]),min(s['cl'] for s in ok),max(s['cl'] for s in ok)))
T=np.array([[s['pos'][k] for k in KEYS] for s in ok])/1000.
dur=np.diff(T,axis=1)
print('segment ms median:',dict(zip(['P1-2','P2-3','P3-4','P4-5','P5-6','P6-7','P7-8'],np.round(np.median(dur,axis=0)))))
print('segment ms SD    :',dict(zip(['P1-2','P2-3','P3-4','P4-5','P5-6','P6-7','P7-8'],np.round(np.std(dur,axis=0)))))
print('backswing P1-P4 ms: med %.0f SD %.0f | down P4-P7 ms: med %.0f SD %.0f'%(np.median(T[:,3]-T[:,0]),np.std(T[:,3]-T[:,0]),np.median(T[:,6]-T[:,3]),np.std(T[:,6]-T[:,3])))
print('fit dt ms median',np.median(np.diff(ok[0]['T']))/1000)
SEG=40
canon=np.median(dur,axis=0)
ug=[];seg=[]
for i in range(7):
    ug+=list(np.linspace(0,1,SEG,endpoint=False)+i);
ug=np.array(ug+[7.])
cT=np.concatenate([[0],np.cumsum(canon)])
def warpT(key,u):   # u in [0,7] → time
    return np.interp(u,np.arange(8),key)
def resamp(s,key,u):
    t=warpT(key,u);T0=s['T']
    def f(A):
        sh=A.shape;B=A.reshape(sh[0],-1);out=np.array([np.interp(t,T0,B[:,c]) for c in range(B.shape[1])]).T
        return out.reshape((len(t),)+sh[1:])
    return dict(D=f(s['D']),P=f(s['P']),G=f(s['G']),U=f(s['U']),tier=np.array([[s['tier'][np.searchsorted(T0,x).clip(0,len(T0)-1)]] for x in t]).squeeze(1))
def head(G,U,cl):
    return G-0.04*U+U*cl[...,None] if np.ndim(cl) else G-0.04*U+U*cl
A=[];B=[]
for s in ok:
    key=np.array([s['pos'][k] for k in KEYS])
    a=resamp(s,key,ug);A.append(a)
    # impact-only alignment: same time grid relative to impact, using canon durations
    kb=cT-cT[6]+key[6]/1000.
    b=resamp(s,kb*1000,ug);B.append(b)
def stack(L,k): return np.stack([x[k] for x in L])
def report(L,lab):
    Pj=stack(L,'P');G=stack(L,'G');U=stack(L,'U');cl=np.array([s['cl'] for s in ok])[:,None,None]
    H=G-0.04*U+U*cl
    def spread(X): 
        m=np.median(X,axis=0);d=np.linalg.norm(X-m,axis=-1);return np.sqrt(np.mean(d**2,axis=0))*100
    out={}
    for nm,X in [('head',Pj[:,:,J['Head']]),('pelvis',Pj[:,:,J['Hips']]),('Lhand',Pj[:,:,J['LeftHand']]),('clubhead',H)]:
        sp=spread(X); out[nm]=[round(float(np.mean(sp[i*SEG:(i+1)*SEG])),1) for i in range(7)]
    print(lab,'RMS spread cm by segment P1-2…P7-8:');[print('  ',k,v) for k,v in out.items()]
report(A,'LANDMARK-warped');report(B,'IMPACT-only')
# robust weights on landmark-warped: distance in DOF-joint space to the median trajectory
Dall=stack(A,'D')[:,:,6:]   # non-root DOFs
md=np.median(Dall,axis=0)
Pall=stack(A,'P');mP=np.median(Pall,axis=0)
dist=np.sqrt(np.mean(np.sum((Pall-mP)**2,axis=-1),axis=(1,2)))*100
c=np.median(dist)*1.5
w=1/(1+(dist/c)**2)
ord_=np.argsort(dist)
print('swing distance to session median (cm RMS over joints/time): min %.1f med %.1f max %.1f'%(dist.min(),np.median(dist),dist.max()))
print('Cauchy weights: min %.2f med %.2f max %.2f ; effective N %.1f of %d'%(w.min(),np.median(w),w.max(),w.sum()**2/np.sum(w**2),len(ok)))
print('furthest 3:',[(ok[i]['id'],round(dist[i],1)) for i in ord_[-3:]],' medoid:',ok[ord_[0]]['id'])
# synthesis
W=w/np.sum(w)
th=np.sum(W[:,None,None]*stack(A,'D'),axis=0)
scale=np.median([s['lens'] for s in ok],axis=0); cl=float(np.median([s['cl'] for s in ok]))
# root: mean scene pos and Markley mean of scene-frame root rotations
rp=np.sum(W[:,None,None]*Pall[:,:,0],axis=0)
K=len(ug);syn=np.zeros((K,N,3));rots=[]
for i in range(K):
    qs=[norm(qm(s['q'],root_local(root_q_world(a['D'][i])))) for s,a in zip(ok,A)]
    qs=[q if q[0]>=0 else -q for q in qs]
    rl=markley(qs,w);p,r=fk_scene(th[i],scale,rp[i],rl);syn[i]=p;rots.append(r)
G=np.sum(W[:,None,None]*stack(A,'G'),axis=0);U=np.sum(W[:,None,None]*stack(A,'U'),axis=0);U/=np.linalg.norm(U,axis=1,keepdims=True)
offs=np.median([s['gripoff'] for s in ok],axis=0)
# hand-to-grip gap: per swing (from stored FK and own offset) vs synthesis
def rotatedoff(rot,off): return qrot(rot,off)
# per-swing gap
def per_swing_gap(s,a):
    gaps=[]
    for i in range(0,K,5):
        rl=norm(qm(s['q'],root_local(root_q_world(a['D'][i]))))
        p,r=fk_scene(a['D'][i],s['lens'],a['P'][i,0],rl)
        gaps.append(np.linalg.norm(p[J['LeftHand']]+qrot(r[J['LeftHand']],s['gripoff'])-a['G'][i]))
    return np.array(gaps)*1000
pg=np.array([per_swing_gap(s,a) for s,a in zip(ok,A)])
syng=np.array([np.linalg.norm(syn[i][J['LeftHand']]+qrot(rots[i][J['LeftHand']],offs)-G[i])*1000 for i in range(0,K,5)])
print('lead-hand↔grip gap mm: per-swing med %.1f p90 %.1f max %.1f | SYNTH med %.1f p90 %.1f max %.1f'%(np.median(pg),np.percentile(pg,90),pg.max(),np.median(syng),np.percentile(syng,90),syng.max()))
# trail hand to shaft line
def linegap(P,G,U):
    v=P-G;return np.linalg.norm(v-np.sum(v*U,axis=-1,keepdims=True)*U,axis=-1)*1000
pt=np.array([linegap(a['P'][:,J['RightHand']],a['G'],a['U']) for a in A])
st=linegap(syn[:,J['RightHand']],G,U)
print('trail wrist↔shaft line mm: per-swing med %.0f p90 %.0f | SYNTH med %.0f p90 %.0f'%(np.median(pt),np.percentile(pt,90),np.median(st),np.percentile(st,90)))
# feet: displacement of heels P1→P7 (planted) and height
def footmove(Pj,j):
    i0,i1=0,6*SEG;return np.linalg.norm(Pj[...,i1,j,:]-Pj[...,i0,j,:],axis=-1)*1000
for nm in ('LeftFoot','RightFoot'):
    print(nm,'P1→impact move mm: per-swing med %.0f max %.0f | SYNTH %.0f'%(np.median(footmove(Pall,J[nm])),footmove(Pall,J[nm]).max(),footmove(syn,J[nm])))
# jitter: RMS 2nd difference of hand and club head per grid step (mm), downswing excluded from impact spike
def jit(X): return float(np.sqrt(np.mean(np.sum(np.diff(X,2,axis=-2)**2,axis=-1)))*1000)
ch=G-0.04*U+U*cl
for nm,X,Y in [('Lhand',Pall[:,:,J['LeftHand']],syn[:,J['LeftHand']]),('Head',Pall[:,:,J['Head']],syn[:,J['Head']])]:
    print(nm,'2nd-diff RMS mm/grid²: median swing %.2f | SYNTH %.2f'%(np.median([jit(x) for x in X]),jit(Y)))
# head at impact vs ball
imp=6*SEG
H_syn=ch[imp]; balls=np.array([s['ball'] for s in ok if s['ball'] is not None])
print('synth clubhead at impact, scene m:',np.round(H_syn,3),' median ball:',np.round(np.median(balls,axis=0),3),' |gap| cm %.1f'%(100*np.linalg.norm(H_syn-np.median(balls,axis=0))))
pH=np.array([a['G'][imp]-0.04*a['U'][imp]+a['U'][imp]*s['cl'] for s,a in zip(ok,A)])
print('per-swing head↔own ball gap cm: med %.1f p90 %.1f'%(np.median(np.linalg.norm(pH-np.array([s['ball'] for s in ok]),axis=1)*100),np.percentile(np.linalg.norm(pH-np.array([s['ball'] for s in ok]),axis=1)*100,90)))
# synthesis vs medoid
md_i=ord_[0];
print('synth vs medoid swing: RMS joint gap cm %.1f; synth vs furthest: %.1f'%(100*np.sqrt(np.mean(np.sum((syn-Pall[md_i])**2,axis=-1))),100*np.sqrt(np.mean(np.sum((syn-Pall[ord_[-1]])**2,axis=-1)))))
# tiers
trs=stack(A,'tier');print('share of joint-frames tier≥2 per swing:',round(float(np.mean(trs>=2)),3))
# timing cost of the computation itself
t1=time.time()
print('(computation incl. python FK: %.1fs)'%(t1-t0))

print('\n===== FOLLOW-UP =====')
seglab=['P1-2','P2-3','P3-4','P4-5','P5-6','P6-7','P7-8']
def segmean(x): return [round(float(np.mean(x[i*SEG:(i+1)*SEG])),1) for i in range(7)]
# 1. Is the flat 7 cm a per-swing placement offset?
off=Pall[:,:,J['Hips']].mean(axis=1,keepdims=True)   # per-swing mean pelvis
Prel=Pall-off[:,:,None,:]
print('placement offset (per-swing mean pelvis) SD cm per axis x/y/z:',np.round(100*np.std(off[:,0,:],axis=0),1))
for nm in ('Head','Hips','LeftHand'):
    X=Prel[:,:,J[nm]];m=np.median(X,axis=0);print(' after removing placement,',nm,'RMS cm',segmean(np.sqrt(np.mean(np.sum((X-m)**2,axis=-1),axis=0))*100))
Gs=stack(A,'G');Us=stack(A,'U');cls=np.array([s['cl'] for s in ok])[:,None,None]
Hs=Gs-0.04*Us+Us*cls;Hrel=Hs-off
m=np.median(Hrel,axis=0);d=np.linalg.norm(Hrel-m,axis=-1)*100
print(' after removing placement, clubhead RMS cm',segmean(np.sqrt(np.mean(d**2,axis=0))),'\n   clubhead MEDIAN dev cm',segmean(np.median(d,axis=0)))
# 2. what's in P7-8
late=d[:,6*SEG:7*SEG].mean(axis=1);o=np.argsort(late)[::-1]
print('P7-8 clubhead dev cm, worst 8:',[(ok[i]['id'][-2:],round(late[i])) for i in o[:8]],' median',round(float(np.median(late)),1))
ud=np.degrees(np.arccos(np.clip(np.sum(Us*np.median(Us,axis=0)/np.linalg.norm(np.median(Us,axis=0),axis=-1,keepdims=True),axis=-1),-1,1)))
print('shaft direction dev deg, median over swings by segment',segmean(np.median(ud,axis=0)),'\n   p90 over swings',segmean(np.percentile(ud,90,axis=0)))
st=stack(A,'ST') if 'ST' in A[0] else None
print('P8 time after impact ms: med %.0f min %.0f max %.0f'%(np.median(T[:,7]-T[:,6]),(T[:,7]-T[:,6]).min(),(T[:,7]-T[:,6]).max()))
# 3. outlier identity
for i in ord_[-3:]:
    s=ok[i];print(' outlier',s['id'],'dist',round(dist[i],1),'reproj',s['reproj'],'placement dev cm',np.round(100*(off[i,0]-np.median(off[:,0],axis=0)),1),'plane',s['plane'],'P1-P4 ms',round((s['pos'][4]-s['pos'][1])/1000),'rating',s['rating'])
distr=np.sqrt(np.mean(np.sum((Prel-np.median(Prel,axis=0))**2,axis=-1),axis=(1,2)))*100
print('placement-free distance: min %.1f med %.1f max %.1f; worst 3'%(distr.min(),np.median(distr),distr.max()),[(ok[i]['id'][-2:],round(distr[i],1)) for i in np.argsort(distr)[-3:]])
# 4. re-anchor club on synthetic hand
Ga=np.array([syn[i][J['LeftHand']]+qrot(rots[i][J['LeftHand']],offs) for i in range(K)])
print('re-anchored club: shift of grip mm med %.1f p90 %.1f max %.1f (0 lead-hand gap by construction)'%(np.median(np.linalg.norm(Ga-G,axis=1)*1000),np.percentile(np.linalg.norm(Ga-G,axis=1)*1000,90),np.max(np.linalg.norm(Ga-G,axis=1)*1000)))
g=np.linalg.norm(Ga-G,axis=1)*1000;print(' grip shift by segment mm',segmean(g))
print(' trail wrist↔shaft line after re-anchor mm: med %.0f p90 %.0f'%(np.median(linegap(syn[:,J['RightHand']],Ga,U)),np.percentile(linegap(syn[:,J['RightHand']],Ga,U),90)))
Ha=Ga-0.04*U+U*cl;print(' re-anchored head↔ball at impact cm %.1f'%(100*np.linalg.norm(Ha[imp]-np.median(balls,axis=0))))
# 5. warps: P1,P4,P7 + fixed after-impact window vs all
def alt(keys_idx):
    L=[]
    for s in ok:
        key=np.array([s['pos'][k] for k in KEYS],float)
        k2=key.copy()
        xs=np.array(keys_idx);k2=np.interp(np.arange(7),xs,key[xs]);k2=np.append(k2,key[6]+canon[6]*1000)
        L.append(resamp(s,k2,ug))
    return L
for lab,idx in [('all P1..P7 + fixed P8',[0,1,2,3,4,5,6]),('P1,P4,P7 + fixed P8',[0,3,6])]:
    L=alt(idx);Xh=stack(L,'P')[:,:,J['LeftHand']]-off;m=np.median(Xh,axis=0);r1=np.sqrt(np.mean(np.sum((Xh-m)**2,axis=-1),axis=0))*100
    H_=stack(L,'G')-0.04*stack(L,'U')+stack(L,'U')*cls-off;m=np.median(H_,axis=0);r2=np.median(np.linalg.norm(H_-m,axis=-1),axis=0)*100
    print(lab,'Lhand RMS',segmean(r1),'\n   clubhead median dev',segmean(r2))
# 6. does averaging flatten the swing? canonical time base
L=alt([0,1,2,3,4,5,6])
ut=np.interp(ug,np.arange(8),cT)
def speed(Hx): return np.linalg.norm(np.gradient(Hx,ut/1000,axis=0),axis=1)
perH=stack(L,'G')-0.04*stack(L,'U')+stack(L,'U')*cls
pk=[speed(perH[i])[3*SEG:6*SEG+3].max() for i in range(len(ok))]
th2=np.sum(W[:,None,None]*stack(L,'D'),axis=0)
G2=np.sum(W[:,None,None]*stack(L,'G'),axis=0);U2=np.sum(W[:,None,None]*stack(L,'U'),axis=0);U2/=np.linalg.norm(U2,axis=1,keepdims=True)
H2=G2-0.04*U2+U2*cl
print('peak clubhead speed m/s on canonical time P4→impact: per-swing med %.1f (SD %.1f) | SYNTH %.1f'%(np.median(pk),np.std(pk),speed(H2)[3*SEG:6*SEG+3].max()))
ang=lambda v,ref: np.degrees(np.arctan2(v[...,1],v[...,0]))
SR=stack(L,'P')[:,:,J['LeftArm']]-stack(L,'P')[:,:,J['RightArm']];PR=stack(L,'P')[:,:,J['LeftUpLeg']]-stack(L,'P')[:,:,J['RightUpLeg']]
def yaw(v): return np.degrees(np.arctan2(v[...,2],v[...,0]))
syn2=syn
for nm,a_,b_ in [('shoulder turn','LeftArm','RightArm'),('pelvis turn','LeftUpLeg','RightUpLeg')]:
    v=stack(L,'P')[:,:,J[a_]]-stack(L,'P')[:,:,J[b_]];sv=syn2[:,J[a_]]-syn2[:,J[b_]]
    for tag,idx in (('top',3*SEG),('impact',6*SEG)):
        print(f'{nm} at {tag} deg (signed yaw): per-swing med {np.median(yaw(v[:,idx])):.1f} SD {np.std(yaw(v[:,idx])):.1f} | SYNTH {yaw(sv[idx]):.1f}')
# 7. equal vs robust vs median
thm=np.mean(stack(L,'D'),axis=0)
print('robust-weighted vs plain-mean DOF difference (deg, non-root): med %.2f max %.2f'%(np.median(np.abs(np.degrees(th2[:,6:]-thm[:,6:]))),np.max(np.abs(np.degrees(th2[:,6:]-thm[:,6:])))))

print('\n===== PLANE GROUPS =====')
hp=np.array([bool(s['plane']) for s in ok]);print('plane offered',hp.sum(),'of',len(hp))
L=alt([0,1,2,3,4,5,6]);Us2=stack(L,'U');Pz=stack(L,'P')
def mdir(X): 
    m=np.median(X,axis=0);return m/np.linalg.norm(m,axis=-1,keepdims=True)
gap=np.degrees(np.arccos(np.clip(np.sum(mdir(Us2[hp])*mdir(Us2[~hp]),axis=-1),-1,1)))
print('shaft direction, plane-group median vs no-plane-group median, deg by segment',segmean(gap))
def scat(X):
    m=mdir(X);a=np.degrees(np.arccos(np.clip(np.sum(X*m,axis=-1),-1,1)));return segmean(np.median(a,axis=0))
print(' within-group scatter deg: plane',scat(Us2[hp]),'\n                          none ',scat(Us2[~hp]))
for nm in ('LeftHand','Head'):
    X=Pz[:,:,J[nm]]-off;print(nm,'group-median gap cm',segmean(np.linalg.norm(np.median(X[hp],axis=0)-np.median(X[~hp],axis=0),axis=-1)*100))

print('\n===== APPROVED RECIPE =====')
AFTER=90000.0; BLEND=10000.0
def approved():
    out=[]
    for s in ok:
        key=np.array([s['pos'][k] for k in KEYS],float);key[7]=key[6]+AFTER
        out.append(resamp(s,key,ug))
    return out
L=approved()
can=np.append(canon[:6],AFTER/1000.);cTa=np.concatenate([[0],np.cumsum(can)]);tg=np.interp(ug,np.arange(8),cTa)
Pj=stack(L,'P');pel=Pj[:,:,J['Hips']].mean(axis=1,keepdims=True);Pr=Pj-pel[:,:,None,:]
dd=np.sqrt(np.mean(np.sum((Pr-np.median(Pr,axis=0))**2,axis=-1),axis=(1,2)))*100
cc=1.5*np.median(dd);wt=1/(1+(dd/cc)**2);Wt=wt/wt.sum()
print('weights (ball position removed): min %.2f med %.2f max %.2f; effective N %.1f of %d'%(wt.min(),np.median(wt),wt.max(),wt.sum()**2/np.sum(wt**2),len(ok)))
print('lowest weights:',[(ok[i]['id'][-2:],round(float(wt[i]),2)) for i in np.argsort(wt)[:3]])
Ds=stack(L,'D');tha=np.sum(Wt[:,None,None]*Ds,axis=0)
thm=np.mean(Ds,axis=0);print('weighted vs plain mean joint DOFs deg: med %.2f max %.2f'%(np.median(np.abs(np.degrees(tha[:,6:]-thm[:,6:]))),np.max(np.abs(np.degrees(tha[:,6:]-thm[:,6:])))))
rpa=np.sum(Wt[:,None,None]*Pj[:,:,0],axis=0);synA=np.zeros((K,N,3));rotA=[]
for i in range(K):
    qs=[norm(qm(s['q'],root_local(root_q_world(a['D'][i])))) for s,a in zip(ok,L)]
    qs=[q if q[0]>=0 else -q for q in qs];p,r=fk_scene(tha[i],scale,rpa[i],markley(qs,wt));synA[i]=p;rotA.append(r)
Ga=np.array([synA[i][J['LeftHand']]+qrot(rotA[i][J['LeftHand']],offs) for i in range(K)])
Us=stack(L,'U');imp=6*SEG;tI=tg[imp]
Ua=np.zeros((K,3))
for i in range(K):
    f=np.ones(len(ok)) if tg[i]<=tI else np.where(hp,1.,1-min(1.,(tg[i]-tI)*1000/BLEND))
    v=np.sum((wt*f)[:,None]*Us[:,i],axis=0);Ua[i]=v/np.linalg.norm(v)
Ha=Ga-0.04*Ua+Ua*cl
ball=np.median(np.array([s['ball'] for s in ok]),axis=0)
print('lead hand on grip: 0 mm by construction; trail wrist↔shaft line mm med %.0f p90 %.0f (single swings med %.0f p90 %.0f)'%(np.median(linegap(synA[:,J['RightHand']],Ga,Ua)),np.percentile(linegap(synA[:,J['RightHand']],Ga,Ua),90),np.median(pt),np.percentile(pt,90)))
print('clubhead↔ball at impact cm %.1f'%(100*np.linalg.norm(Ha[imp]-ball)))
step=np.degrees(np.arccos(np.clip(np.sum(Ua[1:]*Ua[:-1],axis=1),-1,1)));print('shaft step per grid frame deg: P6-7 max %.2f | P7-8 max %.2f | at impact %.2f'%(step[5*SEG:6*SEG].max(),step[6*SEG:].max(),step[imp-1:imp+2].max()))
speed2=lambda H:np.linalg.norm(np.gradient(H,tg/1000,axis=0),axis=1)
Hp=stack(L,'G')-0.04*Us+Us*cls;pk2=[speed2(Hp[i])[3*SEG:6*SEG+3].max() for i in range(len(ok))]
print('peak clubhead speed m/s P4→impact: single swings med %.1f SD %.1f | synth %.1f'%(np.median(pk2),np.std(pk2),speed2(Ha)[3*SEG:6*SEG+3].max()))
PA=Pj[:,:,J['Head']];LH=Pj[:,:,J['LeftHand']]
print('wobble (2nd diff mm/grid²): Head single med %.2f synth %.2f; Lhand single med %.2f synth %.2f'%(np.median([jit(x) for x in PA]),jit(synA[:,J['Head']]),np.median([jit(x) for x in LH]),jit(synA[:,J['LeftHand']])))
md2=np.argmin(dd);print('synth vs most typical swing (%s) RMS joint gap cm %.1f (ball position removed)'%(ok[md2]['id'],100*np.sqrt(np.mean(np.sum((synA-(Pj[md2]-pel[md2][:,None,:]+synA[:,0:1].mean(axis=0,keepdims=True)))**2,axis=-1)))))
for nm,a_,b_ in [('shoulder turn','LeftArm','RightArm'),('pelvis turn','LeftUpLeg','RightUpLeg')]:
    v=Pj[:,:,J[a_]]-Pj[:,:,J[b_]];sv=synA[:,J[a_]]-synA[:,J[b_]]
    for tag,idx in (('top',3*SEG),('impact',6*SEG)):
        print(f'{nm} at {tag} deg: single med {np.median(yaw(v[:,idx])):.1f} SD {np.std(yaw(v[:,idx])):.1f} | synth {yaw(sv[idx]):.1f}')
