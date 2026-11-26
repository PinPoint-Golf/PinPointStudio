#!/usr/bin/env python3
"""tone_ab_inputs - #183's analysis A/B: the population manifest and the input trees (plan section 4).

    python3 -I tools/swinglab/tone_ab_inputs.py --share <share> --library <session folder> [--library ...]
                                                [--root <share>/scratch/i183-ab] [--manifest-only | --verify-sources]

--share is the share's mount point (or the environment variable TONE_AB_SHARE); --library names each library
session folder to add, relative to the share or absolute (the run of 25 Nov added that day's six sessions,
9 swings). Nothing private is a default: both come from the caller.

Reads (never writes) the corpus (<share>/corpus/swings, the 139 swings with video) and the --library sessions,
and writes under --root (default <share>/scratch/i183-ab):
  manifest.json / manifest.csv  one row per swing: id, session, swing, golfer, cameras (face-on, DTL file),
                                raw sidecars, stored ball baseline, pin source (parity / pose3 / b1) and pin
                                paths, truth, launch-monitor clubhead speed, impactUs (parity_set.json)
  in/<session>/<swing>/         a copy of every swing folder without *.raw, plus each session's top-level files
  in_nobase/<session>/<swing>/  the same for the swings with a stored Face-On.ballbase.f32, without it
  sources.json                  size and mtime of every source file read, so a later check can show the
                                corpus and the library were untouched (--verify-sources)
The copies skip the excluded files while copying; nothing is ever deleted from a source.

Golfers are letters, never names: each athlete id (a session folder's second field, <date>_<athlete>_<kind>_<n>)
gets the next letter, A, B, C..., in the order of its first session, the corpus's in date order and then the
--library sessions. On the 25 Nov population that gives the report's A, B and C.
"""
import argparse
import csv
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SHARE = ""            # set from --share in main()
CORPUS = ""
LIB_SESSIONS = []
PARITY_SET = os.path.join(REPO, "tools", "parity", "parity_set.json")
GOLFERS = {}          # athlete id -> letter, in order of first session (golfer_of)


def log(m):
    print(m, flush=True)


def golfer_of(session):
    """The letter of the session's athlete: the next free one the first time an athlete is seen."""
    parts = session.split("_")
    if len(parts) < 3:
        return "?"
    if parts[1] not in GOLFERS:
        GOLFERS[parts[1]] = chr(ord("A") + len(GOLFERS))
    return GOLFERS[parts[1]]


def lm_reading(ppsw):
    """The document's launchMonitor clubheadSpeed (mph), or None. The .ppsw is a zstd-compressed CBOR document."""
    sys.path.insert(0, HERE)    # -I leaves the script's own folder off the path
    try:
        import ppsw_peek
        d = ppsw_peek.read_doc(ppsw)
    except Exception as e:  # noqa: BLE001 - an unreadable document has no reading, said so in the manifest
        return None, "unreadable: %s" % e
    lm = d.get("launchMonitor")
    if not isinstance(lm, dict):
        return None, "none"
    v = lm.get("clubheadSpeed")
    return (float(v) if isinstance(v, (int, float)) and v > 0 else None), lm.get("kind") or "?"


def swing_rows():
    pset = json.load(open(PARITY_SET))
    pins = {w["swing"]: w for w in pset["swings"]}
    rows = []
    sessions = []
    cs = os.path.join(CORPUS, "swings")
    for s in sorted(os.listdir(cs)):
        if os.path.isdir(os.path.join(cs, s)):
            sessions.append(("corpus", os.path.join(cs, s)))
    for s in LIB_SESSIONS:
        sessions.append(("library", s))
    for origin, sd in sessions:
        sname = os.path.basename(sd)
        for w in sorted(os.listdir(sd)):
            wd = os.path.join(sd, w)
            if not (w.startswith("swing_") and os.path.isdir(wd)):
                continue
            files = sorted(os.listdir(wd))
            if "Face-On.mp4" not in files:
                continue                      # 2026-08-04: no video
            vids = [f for f in files if f.lower().endswith(".mp4") and f != "Face-On.mp4"]
            rel = os.path.relpath(wd, SHARE).replace(os.sep, "/")
            sid = "%s__%s" % (sname, w)
            r = {"id": sid, "origin": origin, "session": sname, "swing": w, "src": rel, "golfer": golfer_of(sname),
                 "dtlFile": vids[0] if vids else "", "twoCamera": bool(vids),
                 "raw": any(f.endswith(".raw") for f in files),
                 "baseline": "Face-On.ballbase.f32" in files,
                 "truth": "truth.json" in files}
            key = "swings/%s/%s" % (sname, w)
            if origin == "corpus" and key in pins:
                p = pins[key]
                r.update(pinSource="parity", pin="corpus/" + p["pose"],
                         dtlPin=("corpus/" + p["dtlPose"]) if p.get("dtlPose") else "",
                         impactUs=p.get("impactUs"))
            elif origin == "corpus" and os.path.exists(os.path.join(CORPUS, "pose3", sid + ".json")):
                r.update(pinSource="pose3", pin="corpus/pose3/%s.json" % sid, dtlPin="", impactUs=None)
            else:
                r.update(pinSource="b1", pin="", dtlPin="", impactUs=None)
            if r["twoCamera"] and r["pinSource"] != "b1" and not r["dtlPin"]:
                r["dtlPinNote"] = "two cameras but no pinned DTL pose"
            chs, kind = lm_reading(os.path.join(wd, "swing.ppsw"))
            r["lmClubheadMph"] = chs
            r["lmKind"] = kind
            rows.append(r)
    return rows


def copy_tree(src_swing, dst_swing, skip_suffixes, record):
    os.makedirs(dst_swing, exist_ok=True)
    n = 0
    for f in sorted(os.listdir(src_swing)):
        sp = os.path.join(src_swing, f)
        if not os.path.isfile(sp) or os.path.islink(sp):
            continue
        st = os.stat(sp)
        record[os.path.relpath(sp, SHARE)] = [st.st_size, st.st_mtime]
        if any(f.endswith(s) for s in skip_suffixes):
            continue
        shutil.copy2(sp, os.path.join(dst_swing, f))
        n += st.st_size
    return n


def copy_session_files(src_session, dst_session, record):
    os.makedirs(dst_session, exist_ok=True)
    for f in sorted(os.listdir(src_session)):
        sp = os.path.join(src_session, f)
        if os.path.isfile(sp) and not os.path.islink(sp) and f.endswith(".json") and ".bak" not in f:
            st = os.stat(sp)
            record[os.path.relpath(sp, SHARE)] = [st.st_size, st.st_mtime]
            dp = os.path.join(dst_session, f)
            if not os.path.exists(dp):
                shutil.copy2(sp, dp)


def main():
    global SHARE, CORPUS, LIB_SESSIONS
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--share", default=os.environ.get("TONE_AB_SHARE", ""),
                    help="the share's mount point (default: $TONE_AB_SHARE)")
    ap.add_argument("--library", action="append", default=[],
                    help="a library session folder to add, relative to the share or absolute; repeatable")
    ap.add_argument("--root", help="the run's root (default <share>/scratch/i183-ab)")
    ap.add_argument("--manifest-only", action="store_true")
    ap.add_argument("--verify-sources", action="store_true",
                    help="re-stat every source file in sources.json and report any change")
    a = ap.parse_args()
    if not a.share or not os.path.isdir(os.path.join(a.share, "scratch")):
        log("refusing: --share (or TONE_AB_SHARE) must name the mounted share, the folder that holds scratch/")
        return 2
    SHARE = os.path.realpath(a.share)
    CORPUS = os.path.join(SHARE, "corpus")
    LIB_SESSIONS = [os.path.normpath(os.path.join(SHARE, s)) for s in a.library]
    root = os.path.realpath(a.root or os.path.join(SHARE, "scratch", "i183-ab"))
    if not root.startswith(os.path.join(os.path.realpath(SHARE), "scratch") + os.sep):
        log("refusing: --root must be inside the share's scratch")
        return 2
    if a.verify_sources:
        rec = json.load(open(os.path.join(root, "sources.json")))
        bad = 0
        for rel, (size, mtime) in sorted(rec.items()):
            st = os.stat(os.path.join(SHARE, rel))
            if st.st_size != size or abs(st.st_mtime - mtime) > 1e-3:
                bad += 1
                log("CHANGED %s" % rel)
        log("sources: %d files checked, %d changed" % (len(rec), bad))
        return 1 if bad else 0
    os.makedirs(root, exist_ok=True)
    rows = swing_rows()
    log("swings: %d (two-camera %d, face-on only %d; raw %d; baseline %d; truth %d; LM %d; pins parity %d, pose3 %d, b1 %d)"
        % (len(rows), sum(r["twoCamera"] for r in rows), sum(not r["twoCamera"] for r in rows),
           sum(r["raw"] for r in rows), sum(r["baseline"] for r in rows), sum(r["truth"] for r in rows),
           sum(r["lmClubheadMph"] is not None for r in rows),
           sum(r["pinSource"] == "parity" for r in rows), sum(r["pinSource"] == "pose3" for r in rows),
           sum(r["pinSource"] == "b1" for r in rows)))
    log("sessions: %d" % len({r["session"] for r in rows}))
    man = {"created": time.strftime("%Y-%m-%dT%H:%M:%S"), "swings": rows}
    with open(os.path.join(root, "manifest.json"), "w") as f:
        json.dump(man, f, indent=1, sort_keys=True)
    cols = ["id", "origin", "golfer", "twoCamera", "dtlFile", "raw", "baseline", "truth", "pinSource", "pin", "dtlPin",
            "impactUs", "lmClubheadMph", "lmKind"]
    with open(os.path.join(root, "manifest.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    if a.manifest_only:
        return 0
    record = {}
    t0 = time.time()
    tot = {"in": 0, "in_nobase": 0}
    for r in rows:
        src = os.path.join(SHARE, r["src"])
        for tree, skip, want in (("in", (".raw",), True), ("in_nobase", (".raw", ".ballbase.f32"), r["baseline"])):
            if not want:
                continue
            dst_sess = os.path.join(root, tree, r["session"])
            dst = os.path.join(dst_sess, r["swing"])
            if os.path.exists(dst):
                log("refusing: %s exists (a used root?)" % dst)
                return 2
            copy_session_files(os.path.dirname(src), dst_sess, record)
            tot[tree] += copy_tree(src, dst, skip, record)
        log("copied %s" % r["id"])
    with open(os.path.join(root, "sources.json"), "w") as f:
        json.dump(record, f, indent=0, sort_keys=True)
    log("copied in/ %.2f GB, in_nobase/ %.2f GB in %.0f s; %d source files recorded"
        % (tot["in"] / 1e9, tot["in_nobase"] / 1e9, time.time() - t0, len(record)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
