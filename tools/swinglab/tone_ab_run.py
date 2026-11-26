#!/usr/bin/env python3
"""tone_ab_run - #183's analysis A/B: the arms runner on the studio PC (plan sections 5 and 6).

    python -I -X utf8 tone_ab_run.py --phase one|corpus|digest|strata --share <share> --root <share>\\scratch\\i183-ab
                                     --exe <swinglab_run.exe> [--one-id <swing id>]

--share is the share's root as the studio PC sees it; --one-id (phase one only) is the manifest id of the
one-swing step's swing. Neither has a default: both come from the caller.

Run through tone_ab_studio.cmd, detached, from the Mac (tone_ab.sh). Standard library, plus numpy and cv2
(the swinglab venv) for the digest's frame sizes and the strata.

--phase one      the one-swing step (plan section 6 step 2) on a copy of the --one-id swing at
                 <root>/one/: B1, B2, C1, PB, PB2 (PB again), PC, PBnt (PB without --trace/--dtl), and the two
                 write-back refusals on the copy (each must exit non-zero with the copy's swing.ppsw unchanged).
                 Writes <root>/one/one.json with every check and each arm's seconds.
--phase corpus   the arms over the manifest, in the plan's order: B1, B2, C1, [C2], PB, PC, [PB2], PBs, PCs.
                 C2 runs when any swing's B2 differs from its B1 outside the wall times; PB2 when the one-swing
                 step's PB and PB2 differed (one.json). B1 writes the pins for the swings whose pinned pose is
                 B1's own (pins/<id>.json, pins/<id>__dtl.json). Then the digests and the strata.
--phase digest   (re)digest every finished run; --phase strata: the strata from B1 only.

Every run: its own fresh --out folder runs/<arm>/<id>/, a 900 s ceiling (the child is killed; nothing else
is), and a done.json (rc, seconds, the checks). A run with a done.json is never re-run, so a stopped phase
resumes where it stopped. Before every run the studio PC is checked idle: PinPointStudio.exe, or an image whose
name holds Foresight, FSX or GSPro, or a jom.exe/ctest.exe, stops the phase cleanly (exit 20, state BUSY),
after the run in hand. Nothing is ever written into an athlete folder or the corpus: the inputs are the copies
in <root>/in and <root>/in_nobase, and --write-back is used only in the one-swing refusal checks, on the copy.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

SHARE = ""      # set from --share in main()
GAMMA = "0.80"
TIMEOUT = 900
WALL_TIMES = (("timings",), ("skeleton3d", "diagnostics", "ms"), ("skeleton3d", "diagnostics", "plane", "branchMs"))
BUSY_IMAGES = ("pinpointstudio.exe",)
BUSY_PARTS = ("foresight", "fsx", "gspro")
BUILD_IMAGES = ("jom.exe", "ctest.exe")


def log(msg):
    print(time.strftime("%H:%M:%S ") + msg, flush=True)


def read_json(p):
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)


def write_json(p, obj, indent=None):
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=indent, sort_keys=True, separators=(",", ":") if indent is None else None)
    os.replace(tmp, p)


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


# ---------------------------------------------------------------- the idle check

def busy():
    """None when idle, else a short reason. A failed tasklist counts as busy (fail closed)."""
    try:
        out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True, timeout=60).stdout
    except Exception as e:  # noqa: BLE001
        return "tasklist failed: %s" % e
    imgs = set()
    for line in out.splitlines():
        if line.startswith('"'):
            imgs.add(line.split('"')[1].lower())
    if not imgs:
        return "tasklist listed nothing"
    hit = sorted(i for i in imgs if i in BUSY_IMAGES or any(p in i for p in BUSY_PARTS) or i in BUILD_IMAGES)
    return " ".join(hit) if hit else None


# ---------------------------------------------------------------- one run

def strip_wall(obj):
    obj = json.loads(json.dumps(obj))
    an = obj.get("analysis")
    if isinstance(an, dict):
        for path in WALL_TIMES:
            node = an
            for k in path[:-1]:
                node = node.get(k) if isinstance(node, dict) else None
            if isinstance(node, dict):
                node.pop(path[-1], None)
    src = obj.get("source")
    if isinstance(src, dict):
        src.pop("swingDir", None)
    return obj


def leaf_diffs(a, b, path="", out=None, cap=50):
    """Count of differing leaves between two JSON values; the first `cap` paths kept."""
    if out is None:
        out = {"n": 0, "paths": []}
    if isinstance(a, dict) and isinstance(b, dict):
        for k in set(a) | set(b):
            if k not in a or k not in b:
                out["n"] += 1
                if len(out["paths"]) < cap:
                    out["paths"].append(path + "/" + k + " (only in one)")
            else:
                leaf_diffs(a[k], b[k], path + "/" + k, out, cap)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out["n"] += 1
            if len(out["paths"]) < cap:
                out["paths"].append(path + " (length %d vs %d)" % (len(a), len(b)))
        for i, (x, y) in enumerate(zip(a, b)):
            leaf_diffs(x, y, path + "[%d]" % i, out, cap)
    elif a != b:
        out["n"] += 1
        if len(out["paths"]) < cap:
            out["paths"].append(path)
    return out


def run_checks(rd, arm, row):
    """The plan's run checks (sections 4 and 5): frames mp4 for every camera; the curve on iff the arm is curved;
    pose ran in an unpinned arm (runmeta pose.faceOn, and pose.dtl on a two-camera swing); the pins injected in
    a pinned arm."""
    c = {"ok": True, "why": []}
    mp = os.path.join(rd, "runmeta.json")
    if not os.path.exists(mp):
        return {"ok": False, "why": ["no runmeta.json"]}
    m = read_json(mp)
    tone = m.get("tone") or {}
    cams = tone.get("cameras") or []
    curved = arm_curved(arm)
    want_exp = float(GAMMA) if curved else 1.0
    if abs(float(tone.get("exponent", -1)) - want_exp) > 1e-9:
        c["why"].append("tone.exponent %s, expected %s" % (tone.get("exponent"), want_exp))
    if not cams:
        c["why"].append("no tone cameras")
    for cam in cams:
        if cam.get("frames") != "mp4":
            c["why"].append("camera %s frames=%s" % (cam.get("alias"), cam.get("frames")))
        if bool(cam.get("toned")) != curved:
            c["why"].append("camera %s toned=%s" % (cam.get("alias"), cam.get("toned")))
    pose = m.get("pose") or {}
    inj = sorted(tone.get("injected") or [])
    if arm_pinned(arm):
        want = ["pose"] + (["dtlPose"] if row.get("twoCamera") else [])
        if inj != sorted(want):
            c["why"].append("tone.injected %s, expected %s" % (inj, sorted(want)))
    else:
        if inj:
            c["why"].append("tone.injected %s in an unpinned arm" % inj)
        if "faceOn" not in pose:
            c["why"].append("pose.faceOn missing: face-on pose did not run")
        if row.get("twoCamera") and "dtl" not in pose:
            c["why"].append("pose.dtl missing: DTL pose did not run")
    if not m.get("ok"):
        c["why"].append("runmeta ok=false: %s" % (m.get("error") or ""))
    c["ok"] = not c["why"]
    c["poseFrames"] = {k: (v or {}).get("frames") for k, v in pose.items()}
    c["injected"] = inj
    return c


def arm_curved(arm):
    return arm in ("C1", "C2", "PC", "PCs")


def arm_pinned(arm):
    return arm.startswith("P")


def arm_tree(arm):
    return "in_nobase" if arm in ("PBs", "PCs") else "in"


def command(exe, root, arm, row, out, pins_dir, swing_dir=None):
    sd = swing_dir or os.path.join(root, arm_tree(arm), row["session"], row["swing"])
    cmd = [exe, sd, "--out", out]
    if arm_curved(arm):
        cmd += ["--tone-curve", GAMMA]
    if arm_pinned(arm):
        pin, dpin = pin_paths(row, pins_dir)
        cmd += ["--pose", pin]
        if row.get("twoCamera"):
            cmd += ["--dtl-pose", dpin]
        if arm in ("PB", "PC", "PB2"):
            cmd += ["--trace", "--doc-blocks"]
            if row.get("twoCamera"):
                cmd += ["--dtl"]
            if row.get("impactUs") is not None:
                cmd += ["--impact-us", str(int(row["impactUs"]))]
    return cmd


def pin_paths(row, pins_dir):
    if row["pinSource"] == "b1":
        return os.path.join(pins_dir, row["id"] + ".json"), os.path.join(pins_dir, row["id"] + "__dtl.json")
    p = os.path.join(SHARE, *row["pin"].split("/"))
    d = os.path.join(SHARE, *row["dtlPin"].split("/")) if row.get("dtlPin") else ""
    return p, d


def write_pins(rd, row, pins_dir):
    """B1's own pose as the pinned pose of PB/PC (plan section 3.1): {"frames": analysis.pose2d.frames} and,
    on a two-camera swing, {"frames": analysis.poseDtl.frames} - the shape of corpus/pose2."""
    r = read_json(os.path.join(rd, "result.json"))["analysis"]
    os.makedirs(pins_dir, exist_ok=True)
    write_json(os.path.join(pins_dir, row["id"] + ".json"), {"frames": r["pose2d"]["frames"]})
    if row.get("twoCamera"):
        write_json(os.path.join(pins_dir, row["id"] + "__dtl.json"), {"frames": r["poseDtl"]["frames"]})


def do_run(exe, cmd, rd, arm, row):
    if os.path.exists(rd):
        shutil.rmtree(rd)        # our own half-finished run: a fresh folder every time
    os.makedirs(rd)
    t0 = time.time()
    with open(os.path.join(rd, "runner.log"), "w", encoding="utf-8", errors="replace") as lf:
        lf.write("$ " + " ".join(cmd) + "\n")
        lf.flush()
        try:
            rc = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT, timeout=TIMEOUT).returncode
        except subprocess.TimeoutExpired:
            lf.write("\ntone_ab: killed after %d s\n" % TIMEOUT)
            rc = -9
    secs = round(time.time() - t0, 1)
    chk = run_checks(rd, arm, row) if rc != -9 else {"ok": False, "why": ["killed by the watchdog"]}
    if rc != 0:
        chk["ok"] = False
        chk["why"].append("exit %d" % rc)
    if not os.path.exists(os.path.join(rd, "result.json")):
        chk["ok"] = False
        chk["why"].append("no result.json")
    done = {"arm": arm, "id": row["id"], "rc": rc, "seconds": secs, "checks": chk, "cmd": cmd,
            "finished": time.strftime("%Y-%m-%dT%H:%M:%S")}
    return done


# ---------------------------------------------------------------- the digest

def mp4_dims(path):
    try:
        import cv2
        cap = cv2.VideoCapture(path)
        w, h = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)), int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        cap.release()
        return [w, h] if w > 0 and h > 0 else None
    except Exception:  # noqa: BLE001
        return None


def r5(x):
    return None if x is None else round(float(x), 5)


def digest_pose(p):
    if not isinstance(p, dict):
        return None
    out = []
    for f in p.get("frames") or []:
        kp = f.get("kp") or []
        body = [r5(v) for v in kp[:51]]
        hands = [kp[3 * j + 2] for j in range(91, 133) if 3 * j + 2 < len(kp)]
        out.append([f.get("t_us"), body, r5(sum(hands) / len(hands)) if hands else None,
                    sum(1 for c in hands if c < 0.3), len(hands)])
    return {"frames": out, "keypointCount": p.get("keypointCount"), "camera": p.get("camera"),
            "cropRect": p.get("cropRect")}


def digest_trace(path):
    if not os.path.exists(path):
        return None
    lines, summ = [], None
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            r = json.loads(line)
            if "summary" in r:
                summ = r
                continue
            if "tier" in r:
                lines.append([r.get("frame"), r.get("tier"), r5(r.get("theta_out")), r5(r.get("theta_dp")),
                              r5(r.get("seg_theta")) if r.get("seg_mode", 0) else None, r5(r.get("band_theta")),
                              r.get("t_us")])
    s = (summ or {}).get("summary", {})
    keep = {k: s.get(k) for k in ("refused", "projLenRung", "spanLo", "spanHi", "top", "impact", "bs0", "fin0",
                                  "onsetRule", "phaseSuspect", "ballTrusted", "handsRetried")}
    keep["poseFrames"] = (summ or {}).get("poseFrames")
    return {"lines": lines, "summary": keep}


def digest_trace_dtl(path):
    if not os.path.exists(path):
        return None
    tiers, summ = [], None
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            r = json.loads(line)
            if "summary" in r:
                summ = r["summary"]
                continue
            tiers.append([r.get("t_us"), r.get("tier")])
    return {"tiers": tiers, "ballFound": ((summ or {}).get("ball") or {}).get("found")}


def digest_run(rd, row, root):
    done = read_json(os.path.join(rd, "done.json"))
    d = {"arm": done["arm"], "id": row["id"], "rc": done["rc"], "seconds": done["seconds"], "checks": done["checks"]}
    mp = os.path.join(rd, "runmeta.json")
    if os.path.exists(mp):
        m = read_json(mp)
        d["runmeta"] = {"ok": m.get("ok"), "error": m.get("error"), "frames": m.get("frames"),
                        "tone": m.get("tone"), "pose": m.get("pose"), "analyzeMs": m.get("analyzeMs")}
    rp = os.path.join(rd, "result.json")
    if os.path.exists(rp):
        res = read_json(rp)
        an = res.get("analysis") or {}
        stripped = strip_wall(res)
        d["contentSha1"] = hashlib.sha1(json.dumps(stripped, sort_keys=True).encode()).hexdigest()
        d["blockSha1"] = {k: hashlib.sha1(json.dumps(v, sort_keys=True).encode()).hexdigest()
                          for k, v in (stripped.get("analysis") or {}).items()}
        d["phases"] = {str(p["phase"]): p["t_us"] for p in an.get("phases") or [] if p.get("t_us", -1) >= 0}
        d["pose"] = {"faceOn": digest_pose(an.get("pose2d")), "dtl": digest_pose(an.get("poseDtl"))}
        b = an.get("ball") or {}
        d["ball"] = {"valid": b.get("valid"), "launchTUs": b.get("launchTUs"), "camera": b.get("camera"),
                     "samples": [[s.get("t_us"), 1 if s.get("found") else 0, r5(s.get("x")), r5(s.get("y")),
                                  r5(s.get("r"))] for s in b.get("samples") or []]}
        c = an.get("club")
        if isinstance(c, dict):
            d["club"] = {"valid": c.get("valid"), "frameWidth": c.get("frameWidth"), "frameHeight": c.get("frameHeight"),
                         "samples": [[s.get("t_us"), r5(s.get("theta")), r5(s.get("conf")), s.get("flags")]
                                     for s in c.get("samples") or []]}
        mets = {}
        for mt in an.get("metrics") or []:
            v = mt.get("value")
            scalar = v[0] if isinstance(v, list) and len(v) == 1 else (v if isinstance(v, (int, float)) else None)
            mets[mt.get("key")] = {"unit": mt.get("unit"), "sigma": mt.get("sigma"), "scalar": scalar,
                                   "ph": {str(s["phase"]): s.get("value") for s in mt.get("phaseSamples") or []
                                          if isinstance(s.get("value"), (int, float))},
                                   "phSigma": {str(s["phase"]): s.get("sigma") for s in mt.get("phaseSamples") or []
                                               if isinstance(s.get("sigma"), (int, float))}}
        d["metrics"] = mets
        d["score"] = (an.get("score") or {}).get("overall")
    d["trace"] = digest_trace(os.path.join(rd, "trace.jsonl"))
    d["traceDtl"] = digest_trace_dtl(os.path.join(rd, "trace_dtl.jsonl"))
    sd = os.path.join(root, "in", row["session"], row["swing"])
    d["dims"] = {"faceOn": mp4_dims(os.path.join(sd, "Face-On.mp4")),
                 "dtl": mp4_dims(os.path.join(sd, row["dtlFile"])) if row.get("dtlFile") else None}
    return d


# ---------------------------------------------------------------- the strata (plan section 3.2), from B1 only

def strata(root, rows, dig_dir):
    import cv2
    import numpy as np
    out_rows = []
    sheet = []
    for row in rows:
        p = os.path.join(dig_dir, "B1", row["id"] + ".json")
        if not os.path.exists(p):
            out_rows.append({"id": row["id"], "why": "no B1 digest"})
            continue
        d = read_json(p)
        sd = os.path.join(root, "in", row["session"], row["swing"])
        cap = cv2.VideoCapture(os.path.join(sd, "Face-On.mp4"))
        n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        ts = [s[0] for s in (d.get("ball") or {}).get("samples") or []]
        t0 = ts[0] if ts else 0
        dt = (sorted(b - a for a, b in zip(ts, ts[1:]))[len(ts) // 2] if len(ts) > 2 else 6696)

        def frame_at(t_us):
            idx = max(0, min(n - 1, int(round((t_us - t0) / dt))))
            cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
            ok, fr = cap.read()
            return (idx, fr) if ok else (idx, None)

        ph = d.get("phases") or {}
        p1 = ph.get("0", ph.get("1"))
        p4 = ph.get("2")
        rec = {"id": row["id"], "golfer": row["golfer"], "session": row["session"],
               "camera": "two" if row["twoCamera"] else "faceOn", "exposure": ""}
        if p1 is not None:
            idx, fr = frame_at(p1)
            if fr is not None:
                y = cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY)
                lo = np.percentile(y, 0.5)
                rec.update(p1Frame=idx, sceneMedianLuma=float(np.median(y)),
                           crushedShare=float(np.mean(np.abs(y.astype(np.int16) - lo) <= 1)))
        if p4 is not None:
            idx, fr = frame_at(p4)
            pose = ((d.get("pose") or {}).get("faceOn") or {}).get("frames") or []
            if fr is not None and pose:
                f = min(pose, key=lambda q: abs(q[0] - p4))
                kp = f[1]
                h, w = fr.shape[:2]
                pts = []
                for j in (5, 6, 12, 11):
                    pts.append([kp[3 * j] * w, kp[3 * j + 1] * h])
                confs = [kp[3 * j + 2] for j in (5, 6, 11, 12)]
                poly = np.array(pts, np.int32)
                mask = np.zeros(fr.shape[:2], np.uint8)
                cv2.fillPoly(mask, [poly], 255)
                y = cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY)
                vals = y[mask > 0]
                if vals.size > 50:
                    rec.update(p4Frame=idx, torsoMedianLuma=float(np.median(vals)), torsoPx=int(vals.size),
                               torsoMinConf=round(min(confs), 3))
                    x0, y0 = max(0, poly[:, 0].min() - 20), max(0, poly[:, 1].min() - 20)
                    x1, y1 = min(w, poly[:, 0].max() + 20), min(h, poly[:, 1].max() + 20)
                    crop = fr[y0:y1, x0:x1].copy()
                    if crop.size:
                        cv2.polylines(crop, [poly - [x0, y0]], True, (0, 255, 255), 1)
                        sheet.append((rec["torsoMedianLuma"], row["id"], crop))
        cap.release()
        out_rows.append(rec)
    write_json(os.path.join(root, "strata.json"), {"rows": out_rows}, indent=1)
    # The contact sheet: every swing's torso crop at the top, darkest first, labelled with its luma.
    if sheet:
        sheet.sort(key=lambda s: s[0])
        tile = 120
        cols = 12
        rws = (len(sheet) + cols - 1) // cols
        img = np.full((rws * (tile + 18), cols * tile, 3), 255, np.uint8)
        for k, (lum, sid, crop) in enumerate(sheet):
            ch, cw = crop.shape[:2]
            s = tile / max(ch, cw)
            cr = cv2.resize(crop, (max(1, int(cw * s)), max(1, int(ch * s))))
            r, c = divmod(k, cols)
            y, x = r * (tile + 18), c * tile
            img[y:y + cr.shape[0], x:x + cr.shape[1]] = cr
            cv2.putText(img, "%d %s" % (lum, sid[5:10] + sid[-4:]), (x + 2, y + tile + 13), cv2.FONT_HERSHEY_SIMPLEX,
                        0.33, (0, 0, 0), 1, cv2.LINE_AA)
        cv2.imwrite(os.path.join(root, "torso_sheet.png"), img)
    log("strata: %d rows, %d crops" % (len(out_rows), len(sheet)))


# ---------------------------------------------------------------- the phases

def arm_rows(arm, rows):
    return [r for r in rows if r["baseline"]] if arm in ("PBs", "PCs") else rows


def run_arm(a, arm, rows, root, runs_dir, pins_dir, state):
    n = len(rows)
    for i, row in enumerate(rows):
        rd = os.path.join(runs_dir, arm, row["id"])
        if os.path.exists(os.path.join(rd, "done.json")):
            continue
        why = busy()
        if why:
            log("BUSY (%s): stopping after the run in hand; resume later" % why)
            state["busy"] = why
            return 20
        cmd = command(a.exe, root, arm, row, rd, pins_dir)
        done = do_run(a.exe, cmd, rd, arm, row)
        if arm == "B1" and row["pinSource"] == "b1" and os.path.exists(os.path.join(rd, "result.json")):
            try:
                write_pins(rd, row, pins_dir)
            except Exception as e:  # noqa: BLE001
                done["checks"]["ok"] = False
                done["checks"]["why"].append("pin write failed: %s" % e)
        if arm in ("B2", "PB2", "C2"):
            ref = {"B2": "B1", "PB2": "PB", "C2": "C1"}[arm]
            rp = os.path.join(runs_dir, ref, row["id"], "result.json")
            mp = os.path.join(rd, "result.json")
            if os.path.exists(rp) and os.path.exists(mp):
                dd = leaf_diffs(strip_wall(read_json(rp)), strip_wall(read_json(mp)))
                done["vsRef"] = {"ref": ref, "diffLeaves": dd["n"], "firstPaths": dd["paths"][:20]}
        write_json(os.path.join(rd, "done.json"), done, indent=1)
        try:
            os.makedirs(os.path.join(a.dig_dir, arm), exist_ok=True)
            write_json(os.path.join(a.dig_dir, arm, row["id"] + ".json"), digest_run(rd, row, root))
        except Exception as e:  # noqa: BLE001
            log("digest failed %s %s: %s" % (arm, row["id"], e))
        flag = "ok  " if done["checks"]["ok"] else "FAIL"
        extra = ("" if done["checks"]["ok"] else " " + "; ".join(done["checks"]["why"])[:200])
        if "vsRef" in done:
            extra += " vs%s %d leaves" % (done["vsRef"]["ref"], done["vsRef"]["diffLeaves"])
        log("[%s %3d/%d] %s %-55s rc %d %6.1f s%s" % (arm, i + 1, n, flag, row["id"], done["rc"], done["seconds"], extra))
        if i == 0:
            # The first swing's gate lines (AD-24): its frames and reuse lines, read before going on.
            try:
                with open(os.path.join(rd, "runner.log"), encoding="utf-8", errors="replace") as f:
                    for line in f:
                        if "window rebuilt" in line or "reuse" in line.lower() or "Mp4Reader" in line \
                                or "TONE" in line or "tone curve" in line:
                            log("  gate: " + line.rstrip()[:220])
            except OSError:
                pass
    return 0


def phase_corpus(a, root):
    rows = read_json(os.path.join(root, "manifest.json"))["swings"]
    if a.only:
        rows = [r for r in rows if r["id"] in set(a.only)]
    runs_dir = os.path.join(root, "runs")
    pins_dir = os.path.join(root, "pins")
    os.makedirs(runs_dir, exist_ok=True)
    state = {}
    one = os.path.join(root, "one", "one.json")
    pb2 = read_json(one).get("pb2Needed", False) if os.path.exists(one) else True
    arms = ["B1", "B2", "C1", "C2", "PB", "PC", "PB2", "PBs", "PCs"]
    for arm in arms:
        if arm == "C2":
            diff = 0
            for r in rows:
                p = os.path.join(runs_dir, "B2", r["id"], "done.json")
                if os.path.exists(p) and read_json(p).get("vsRef", {}).get("diffLeaves", 0) > 0:
                    diff += 1
            if diff == 0:
                log("C2 not needed: every B2 equals its B1 outside the wall times")
                continue
            log("C2 needed: %d swings' B2 differ from B1" % diff)
        if arm == "PB2" and not pb2:
            log("PB2 not needed: the one-swing PB repeat was identical")
            continue
        t0 = time.time()
        log("== arm %s: %d swings" % (arm, len(arm_rows(arm, rows))))
        rc = run_arm(a, arm, arm_rows(arm, rows), root, runs_dir, pins_dir, state)
        log("== arm %s ended rc %d in %.0f s" % (arm, rc, time.time() - t0))
        if rc:
            return rc
        if arm == "B1" and not os.path.exists(os.path.join(root, "strata.json")):
            # The strata come from B1 alone, written before any curved arm runs (plan section 3.2).
            try:
                strata(root, rows, a.dig_dir)
            except Exception as e:  # noqa: BLE001
                log("strata failed (re-run with --phase strata): %s" % e)
    return 0


def phase_one(a, root):
    rows = read_json(os.path.join(root, "manifest.json"))["swings"]
    row = dict(next(r for r in rows if r["id"] == a.one_id))
    od = os.path.join(root, "one")
    src = os.path.join(root, "in", row["session"], row["swing"])
    copy = os.path.join(od, row["session"], row["swing"])
    if not os.path.exists(copy):
        shutil.copytree(os.path.dirname(src), os.path.dirname(copy),
                        ignore=lambda d, names: [n for n in names if os.path.isdir(os.path.join(d, n))
                                                 and n.startswith("swing_") and n != row["swing"]])
    runs_dir = os.path.join(od, "runs")
    pins_dir = os.path.join(od, "pins")
    res = {"swing": row["id"], "arms": {}}
    why = busy()
    if why:
        log("BUSY (%s)" % why)
        return 20
    plan = [("B1", "B1", []), ("B2", "B2", []), ("C1", "C1", []), ("PB", "PB", []), ("PB2", "PB", []),
            ("PC", "PC", []), ("PBnt", "PB", ["notrace"])]
    for label, arm, opts in plan:
        rd = os.path.join(runs_dir, label)
        cmd = command(a.exe, root, arm, row, rd, pins_dir, swing_dir=copy)
        if "notrace" in opts:
            cmd = [c for c in cmd if c not in ("--trace", "--dtl")]
        done = do_run(a.exe, cmd, rd, arm, row)
        if label == "B1":
            write_pins(rd, row, pins_dir)
        write_json(os.path.join(rd, "done.json"), done, indent=1)
        res["arms"][label] = {"rc": done["rc"], "seconds": done["seconds"], "checks": done["checks"]}
        log("one %-5s rc %d %6.1f s %s" % (label, done["rc"], done["seconds"],
                                          "ok" if done["checks"]["ok"] else "; ".join(done["checks"]["why"])))

    def cmp(x, y):
        px, py = os.path.join(runs_dir, x, "result.json"), os.path.join(runs_dir, y, "result.json")
        if not (os.path.exists(px) and os.path.exists(py)):
            return {"n": -1, "paths": ["missing result"]}
        dd = leaf_diffs(strip_wall(read_json(px)), strip_wall(read_json(py)))
        return {"n": dd["n"], "paths": dd["paths"][:15]}
    res["B1vsB2"] = cmp("B1", "B2")
    res["B1vsC1"] = cmp("B1", "C1")
    res["PBvsPB2"] = cmp("PB", "PB2")
    res["PBvsPBnt"] = cmp("PB", "PBnt")
    res["PBvsPC"] = cmp("PB", "PC")
    # PB with B1's own pose must reproduce B1's ball and club (the pin's time base is right).
    try:
        b1 = strip_wall(read_json(os.path.join(runs_dir, "B1", "result.json")))["analysis"]
        pbnt = strip_wall(read_json(os.path.join(runs_dir, "PBnt", "result.json")))["analysis"]
        res["B1vsPBnt"] = {k: leaf_diffs(b1.get(k), pbnt.get(k))["n"] for k in ("pose2d", "poseDtl", "ball", "club",
                                                                                "clubDtl", "phases", "metrics")}
    except Exception as e:  # noqa: BLE001
        res["B1vsPBnt"] = {"error": str(e)}
    # The histogram check: C1's first-frame histogram is B1's mapped through the table.
    lut = [min(255, int(255.0 * (v / 255.0) ** float(GAMMA) + 0.5)) for v in range(256)]
    hc = {}
    try:
        hb = {c["alias"]: c.get("firstFrameHist") for c in read_json(os.path.join(runs_dir, "B1", "runmeta.json"))["tone"]["cameras"]}
        hcur = {c["alias"]: c.get("firstFrameHist") for c in read_json(os.path.join(runs_dir, "C1", "runmeta.json"))["tone"]["cameras"]}
        for al, h in hb.items():
            mapped = [0] * 256
            for v, n in enumerate(h or []):
                mapped[lut[v]] += n
            hc[al] = {"mappedEqualsCurved": mapped == hcur.get(al), "curvedDiffersFromPlain": h != hcur.get(al)}
    except Exception as e:  # noqa: BLE001
        hc["error"] = str(e)
    res["histogram"] = hc
    # The write-back refusals, on the copy only.
    ppsw = os.path.join(copy, "swing.ppsw")
    h0 = sha256_file(ppsw)
    params = os.path.join(od, "tone_params.json")
    write_json(params, {"decode": {"toneExponent": 0.8}})
    ref = {}
    for name, extra in (("toneCurve", ["--tone-curve", "0.8"]), ("params", ["--params", params])):
        cmd = [a.exe, copy, "--write-back"] + extra
        lp = os.path.join(od, "refuse_%s.log" % name)
        with open(lp, "w", encoding="utf-8", errors="replace") as lf:
            lf.write("$ " + " ".join(cmd) + "\n")
            lf.flush()
            try:
                rc = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT, timeout=TIMEOUT).returncode
            except subprocess.TimeoutExpired:
                rc = -9
        ref[name] = {"rc": rc, "refused": rc != 0, "ppswUnchanged": sha256_file(ppsw) == h0}
        log("refusal %s: rc %d, swing.ppsw %s" % (name, rc, "unchanged" if ref[name]["ppswUnchanged"] else "CHANGED"))
    res["refusals"] = ref
    res["pb2Needed"] = res["PBvsPB2"]["n"] != 0
    res["c2Needed"] = res["B1vsB2"]["n"] != 0
    res["allChecksOk"] = (all(v["checks"]["ok"] for v in res["arms"].values())
                          and all(v.get("mappedEqualsCurved") and v.get("curvedDiffersFromPlain")
                                  for k, v in hc.items() if k != "error") and "error" not in hc
                          and all(v["refused"] and v["ppswUnchanged"] for v in ref.values())
                          and res["PBvsPBnt"]["n"] == 0)
    write_json(os.path.join(od, "one.json"), res, indent=1)
    log("one: B1~B2 %d leaves, PB~PB2 %d, PB~PBnt %d, B1~C1 %d, PB~PC %d; histogram %s; all checks %s"
        % (res["B1vsB2"]["n"], res["PBvsPB2"]["n"], res["PBvsPBnt"]["n"], res["B1vsC1"]["n"], res["PBvsPC"]["n"],
           json.dumps(hc), "OK" if res["allChecksOk"] else "FAILED"))
    return 0 if res["allChecksOk"] else 6


def phase_digest(a, root):
    rows = {r["id"]: r for r in read_json(os.path.join(root, "manifest.json"))["swings"]}
    runs_dir = os.path.join(root, "runs")
    n = 0
    for arm in sorted(os.listdir(runs_dir)):
        os.makedirs(os.path.join(a.dig_dir, arm), exist_ok=True)
        for sid in sorted(os.listdir(os.path.join(runs_dir, arm))):
            rd = os.path.join(runs_dir, arm, sid)
            if os.path.exists(os.path.join(rd, "done.json")):
                write_json(os.path.join(a.dig_dir, arm, sid + ".json"), digest_run(rd, rows[sid], root))
                n += 1
    log("digested %d runs" % n)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--phase", required=True, choices=("one", "corpus", "digest", "strata"))
    ap.add_argument("--share", required=True, help="the share's root as this machine sees it")
    ap.add_argument("--root", required=True)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--one-id", help="phase one: the manifest id of the one-swing step's swing")
    ap.add_argument("--only", nargs="+")
    a = ap.parse_args()
    global SHARE
    SHARE = os.path.abspath(a.share)
    if a.phase == "one" and not a.one_id:
        log("refusing: phase one needs --one-id")
        return 2
    root = os.path.abspath(a.root)
    if not root.lower().startswith(os.path.join(SHARE, "scratch").lower() + os.sep.lower()):
        log("refusing: --root must be inside %s" % os.path.join(SHARE, "scratch"))
        return 2
    a.dig_dir = os.path.join(root, "digests")
    if a.phase in ("one", "corpus") and not os.path.isfile(a.exe):
        log("refusing: no swinglab_run at %s" % a.exe)
        return 3
    log("tone_ab_run %s, root %s, exe %s (%s bytes, mtime %s)" % (
        a.phase, root, a.exe, os.path.getsize(a.exe) if os.path.exists(a.exe) else "?",
        time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(a.exe))) if os.path.exists(a.exe) else "?"))
    if a.phase == "one":
        return phase_one(a, root)
    if a.phase == "corpus":
        return phase_corpus(a, root)
    if a.phase == "digest":
        return phase_digest(a, root)
    rows = read_json(os.path.join(root, "manifest.json"))["swings"]
    strata(root, rows, a.dig_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
