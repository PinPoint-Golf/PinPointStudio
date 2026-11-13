import json, collections, numpy as np, sys
sys.argv=['x']; exec(open('analyze.py').read().split("out = []")[0])
V4=[r for r in R if r["golfer"] in VALID4]
for x,y in [("arm_incl3d_P4","stvb"),("spa3d_P4","armlift_a1"),("spa3d_P4","armlift_a2_plane"),("spa3d_P4","armlift_b_dtl"),("spa3d_P4","stvb"),
            ("armlift_b_dtl","spa3d_P4"),("takeaway_P2_P1","climb_P3_P1"),("takeaway_P2_P1","armlift_b_dtl"),("takeaway_P2_P1","armlift_a2_plane"),
            ("climb_P3_P1","L_m_handPathLoop"),("climb_P3_P1","L_m_clubPathAtImpact"),("climb_P3_P1","L_m_spinAxis"),
            ("armlift_a1","L_m_trailElbowRise"),("armlift_a2_plane","L_m_trailElbowRise"),("armlift_b_dtl","L_m_trailElbowRise"),
            ("armlift_a1","L_m_leadUpperArmToChest"),("armlift_a2_plane","L_m_leadUpperArmToChest"),("armlift_b_dtl","L_m_leadUpperArmToChest"),
            ("L_m_clubPathAtImpact","L_m_spinAxis"),("L_m_handPathLoop","L_m_clubPathAtImpact")]:
    ws=within_session(V4,x,y,nperm=2000)
    print(f"{x} vs {y}:", f"rho {ws['rho']:+.2f} n={ws['n']} sessions={ws['sessions']} p={ws['p']:.3f}" if ws else "-")
# reliability: pooled within-session SD vs SD of golfer medians
for k in ["takeaway_P2_P1","climb_P3_P1","spc_P3","armlift_a1","armlift_a2_plane","armlift_b_dtl","spa3d_P4"]:
    g=collections.defaultdict(list)
    for r in V4:
        if ok(r.get(k)): g[r["session"]].append(r[k])
    num=sum((len(v)-1)*np.var(v,ddof=1) for v in g.values() if len(v)>=3); den=sum(len(v)-1 for v in g.values() if len(v)>=3)
    wsd=np.sqrt(num/den)
    meds=[np.median([r[k] for r in V4 if r["golfer"]==gg and ok(r.get(k))]) for gg in VALID4 if any(ok(r.get(k)) for r in V4 if r["golfer"]==gg)]
    smeds=[np.median(v) for v in g.values() if len(v)>=3]
    print(f"{k}: within-session SD {wsd:.1f}; range of golfer medians {min(meds):.1f}..{max(meds):.1f}; SD of session medians {np.std(smeds,ddof=1):.1f} over {len(smeds)} sessions")
# Mark per-session medians for the arm-lift versions + spa3d
for s in sorted(set(r["session"] for r in V4)):
    ss=[r for r in V4 if r["session"]==s and ok(r.get("armlift_a1"))]
    if len(ss)>=3:
        md=lambda k: np.median([r[k] for r in ss if ok(r.get(k))]) if any(ok(r.get(k)) for r in ss) else float('nan')
        print(f"{s}: n={len(ss)} a1 {md('armlift_a1'):.1f} a2 {md('armlift_a2_plane'):.1f} b {md('armlift_b_dtl'):.1f} tilt {md('spa3d_P4'):.1f} armIncl {md('arm_incl3d_P4'):.1f} take {md('takeaway_P2_P1'):.1f} climb {md('climb_P3_P1'):.1f} clubs {collections.Counter(r['club'] for r in ss).most_common(2)}")
# Chris detail
for r in R:
    if r["golfer"]=="Chris":
        print("Chris", r["swing"], r["club"], "tilt", f1(r.get("spa3d_P4")), "bend", f1(r.get("spineForwardBend_P1")), "stvb", f1(r.get("stvb")), "a1", f1(r.get("armlift_a1")), "a2", f1(r.get("armlift_a2_plane")), "b", f1(r.get("armlift_b_dtl")), "armIncl", f1(r.get("arm_incl3d_P4")))
# DTL low-confidence swings
for r in R:
    if r.get("dtl_top_frames") and (r.get("dtl_top_ok")==0 or min(r.get("dtl_conf_leadSh",1),r.get("dtl_conf_trailSh",1),r.get("dtl_conf_leadWr",1))<0.5):
        print("lowconf", r["session"], r["swing"], r.get("dtl_top_ok"), f1(r.get("dtl_conf_leadSh")), f1(r.get("dtl_conf_trailSh")), f1(r.get("dtl_conf_leadWr")), "b", f1(r.get("armlift_b_dtl")))
# Nick b without a1
print([ (r["session"],r["swing"]) for r in R if ok(r.get("armlift_b_dtl")) and not ok(r.get("armlift_a1"))])
print("clubs per golfer", {g: collections.Counter(r["club"] for r in R if r["golfer"]==g and ok(r.get("armlift_a1"))).most_common(4) for g in GOLFERS})
