"""Supplementary per-swing reads for the data-quality notes: 3-D lead-elbow flexion at P4, DTL lead-elbow
confidence at P4, leadArmToTorso at several phases, and why swingPlaneCurve is missing (gate records)."""
import sys, json, math, glob, os, gc, collections
import numpy as np
sys.path.insert(0, "/Users/markliversedge/Projects/PinPointStudio/tools")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pp_swingdoc import load_swing
import extract as E

HERE = os.path.dirname(os.path.abspath(__file__))
rows = [json.loads(l) for l in open(f"{HERE}/swings.jsonl")]
fo = open(f"{HERE}/supplement.jsonl", "w")
for r in rows:
    if "error" in r or not (r.get("sk_valid") or r["athlete"] == "Paddy-Marriott"):
        continue
    d = load_swing(r["path"]); an = d["analysis"]; lad = E.ladder_of(an); sm = E.series_map(an)
    o = {"path": r["path"], "athlete": r["athlete"], "session": r["session"]}
    tp = lad.get(2)
    sk = an.get("skeleton3d")
    if sk and sk.get("valid") and tp is not None:
        fl = []
        for f in sk["frames"]:
            if abs(int(f["t"]) - tp) <= E.W and min(int(f["tier"][j]) for j in (8, 9, 10)) >= 2:
                p = np.asarray(f["p"], float).reshape(-1, 3)
                u = p[8] - p[9]; v = p[10] - p[9]
                c = float(np.dot(u, v) / np.linalg.norm(u) / np.linalg.norm(v))
                fl.append(180 - math.degrees(math.acos(max(-1, min(1, c)))))
        if fl:
            o["leadElbowFlex3d_P4"] = float(np.median(fl))
    pdt = an.get("poseDtl")
    if pdt and tp is not None:
        cs = [f["kp"][3 * 7 + 2] for f in pdt["frames"] if abs(int(f["t_us"]) - tp) <= E.W]
        if cs:
            o["dtl_conf_leadElbow_P4"] = float(np.median(cs))
    lat = sm.get("leadArmToTorso")
    for nm, ph in [("P1", 0), ("P4", 2), ("P7", 5), ("P8", 14), ("Fin", 7)]:
        o[f"leadArmToTorso_{nm}"] = E.grader_at(lat, ph, lad)
    o["has_spc"] = "swingPlaneCurve" in sm and len(sm["swingPlaneCurve"].get("t_us") or []) > 0
    if not o["has_spc"]:
        g = [x for x in an.get("gates", []) if "plane" in json.dumps(x).lower() or "shaft" in json.dumps(x).lower()
             or "club" in json.dumps(x).lower()]
        o["spc_gates"] = [json.dumps(x)[:200] for x in g[:4]]
        o["clubDtl_keys"] = list((an.get("clubDtl") or {}).keys())[:10]
    fo.write(json.dumps(o) + "\n"); fo.flush()
    del d, an, sm; gc.collect()
fo.close()

S = [json.loads(l) for l in open(f"{HERE}/supplement.jsonl")]
ok = lambda x: x is not None and math.isfinite(x)
for a in ["Mark-Liversedge", "Nick-Henn", "Jess-Ratcliffe", "Paddy-Marriott", "Chris-Marriott"]:
    ss = [s for s in S if s["athlete"] == a]
    def st(k):
        v = [s[k] for s in ss if ok(s.get(k))]
        return f"{np.median(v):.1f} [{np.percentile(v,25):.1f},{np.percentile(v,75):.1f}] n={len(v)}" if v else "-"
    print(a, "| leadElbowFlex3d_P4", st("leadElbowFlex3d_P4"), "| DTL lead-elbow conf P4", st("dtl_conf_leadElbow_P4"))
    print("    leadArmToTorso P1", st("leadArmToTorso_P1"), "P4", st("leadArmToTorso_P4"), "P7", st("leadArmToTorso_P7"),
          "P8", st("leadArmToTorso_P8"), "Fin", st("leadArmToTorso_Fin"))
    miss = [s for s in ss if not s["has_spc"]]
    print("    no swingPlaneCurve:", len(miss), "of", len(ss))
    for s in miss[:2]:
        print("      ", s["session"], s.get("spc_gates"), s.get("clubDtl_keys"))
