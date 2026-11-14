#!/usr/bin/env python3
"""parity_select - choose the refactor's parity set and write tools/parity/parity_set.json.

    python3 -I tools/parity/parity_select.py [--library /mnt/swingdata] [--corpus /mnt/swingdata/corpus]
                                             [--out tools/parity/parity_set.json] [--dry-run]

Reads the athlete library and the corpus READ-ONLY (build/refactor/step-G/BRIEF.md section 2) and
writes two lists:

  sessions  the 8 whole sessions the session and athlete levels are taken on, one per category
            below, as paths relative to the library root, each with why it was chosen;
  swings    the 61 pinned-pose swings of corpus/pose2 (the analysis level), each with its swing
            folder, pinned face-on pose, pinned DTL pose when the corpus has one (corpus/pose2_dtl),
            and an explicit impact only when the swing document carries none (swinglab's rescue
            from truth.json P7), so the cabin PC needs nothing but this file to run them.

Categories (one session each; among several candidates the one with most swing documents wins,
then the earliest folder name):
  cam_imu_lm      an 18 Aug session where every swing has a camera, an IMU and a launch monitor
  lm_only         the 4 Aug session: every swing launch monitor only
  premier_lm      a session where every swing has a Premier launch monitor (lm:premier) - its
                  own connector and import path, the one LM other than the GCQuad
  untaped_6i      the 9 Sep session: every swing a 6 iron with no band centres (untaped shaft)
  jess_oct20      a Jess-Ratcliffe 20 Oct session
  nick_oct20      a Nick-Henn 20 Oct session
  chris           a Chris-Marriott session (kept for its processing failures)
  multi_club      a session with more than one club, not already chosen above

If any category has no candidate the tool writes nothing and exits 1, naming it. Needs Python
3.14 (stdlib zstd) and the libppswing sibling checkout, through tools/pp_swingdoc.py.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))          # tools/, for pp_swingdoc
import parity_common as pc                             # noqa: E402
from pp_swingdoc import load_swing, has_swing          # noqa: E402

CAMERAS = {"faceOn", "dtl"}


def device_kinds(devices):
    """summary.devices -> the instrument mix: camera / imu / lm. A device that is neither a
    camera nor 'lm:*' is an IMU (hackmotion, witmotion)."""
    kinds = set()
    for d in devices:
        if d in CAMERAS:
            kinds.add("camera")
        elif d.startswith("lm:"):
            kinds.add("lm")
        else:
            kinds.add("imu")
    return kinds


def survey_session(path):
    swings = []
    for name in sorted(os.listdir(path)):
        sd = os.path.join(path, name)
        if not name.startswith("swing_") or not os.path.isdir(sd) or not has_swing(sd):
            continue
        doc = load_swing(sd)
        sm = doc.get("summary", {})
        bands = ((doc.get("capture", {}) or {}).get("club") or {}).get("bandCentersMm") or []
        swings.append({"devices": sorted(sm.get("devices", [])), "club": sm.get("club", ""),
                       "taped": bool(bands)})
    return swings


def survey_library(library):
    out = []
    for athlete in sorted(os.listdir(library)):
        ad = os.path.join(library, athlete)
        if athlete.startswith(".") or athlete in ("corpus", "scratch") or not os.path.isdir(ad):
            continue
        for session in sorted(os.listdir(ad)):
            sd = os.path.join(ad, session)
            if session.startswith(".") or not os.path.isdir(sd):
                continue
            swings = survey_session(sd)
            if not swings:
                continue
            out.append({"athlete": athlete, "session": session, "date": session[:10],
                        "path": athlete + "/" + session, "swings": swings})
    return out


def every(s, pred):
    return all(pred(w) for w in s["swings"])


def describe(s):
    mix = {}
    for w in s["swings"]:
        key = ",".join(w["devices"])
        mix[key] = mix.get(key, 0) + 1
    clubs = {}
    for w in s["swings"]:
        clubs[w["club"]] = clubs.get(w["club"], 0) + 1
    return mix, clubs


CATEGORIES = [
    ("cam_imu_lm", "camera + IMU + launch monitor on every swing (18 Aug Wrist session)",
     lambda s: s["date"] == "2026-08-18"
     and every(s, lambda w: device_kinds(w["devices"]) >= {"camera", "imu", "lm"})),
    ("lm_only", "launch monitor only on every swing (4 Aug)",
     lambda s: s["date"] == "2026-08-04" and every(s, lambda w: device_kinds(w["devices"]) == {"lm"})),
    ("premier_lm", "Premier launch monitor on every swing (its own connector and import path)",
     lambda s: every(s, lambda w: "lm:premier" in w["devices"])),
    ("untaped_6i", "untaped 6 iron: every swing a 6 iron with no band centres (9 Sep)",
     lambda s: s["date"] == "2026-09-09"
     and every(s, lambda w: w["club"] == "6 IRON" and not w["taped"])),
    ("jess_oct20", "Jess-Ratcliffe, 20 Oct",
     lambda s: s["athlete"] == "Jess-Ratcliffe" and s["date"] == "2026-10-20"),
    ("nick_oct20", "Nick-Henn, 20 Oct",
     lambda s: s["athlete"] == "Nick-Henn" and s["date"] == "2026-10-20"),
    ("chris", "Chris-Marriott, kept for its processing failures",
     lambda s: s["athlete"] == "Chris-Marriott"),
    ("multi_club", "more than one club in the session, not already chosen",
     lambda s: len({w["club"] for w in s["swings"]}) > 1),
]


def choose_sessions(survey):
    chosen, missing = [], []
    taken = set()
    for cat, why, pred in CATEGORIES:
        cands = [s for s in survey if s["path"] not in taken and pred(s)]
        if not cands:
            missing.append(cat)
            continue
        cands.sort(key=lambda s: (-len(s["swings"]), s["path"]))
        s = cands[0]
        taken.add(s["path"])
        mix, clubs = describe(s)
        chosen.append({"category": cat, "path": s["path"], "athlete": s["athlete"],
                       "session": s["session"], "swingCount": len(s["swings"]),
                       "devices": mix, "clubs": clubs,
                       "why": why + ("; the only candidate" if len(cands) == 1 else
                                     "; most swing documents of %d candidates" % len(cands)),
                       "candidates": [c["path"] for c in cands]})
    return chosen, missing


def impact_rescue(swing_dir, doc):
    """swinglab.core.run_one's rule: only when the document has neither capture.impactUs nor an
    Impact phase (5), take truth.json's P7 (t0_us + p7_s)."""
    iu = (doc.get("capture", {}) or {}).get("impactUs")
    if isinstance(iu, (int, float)) and iu > 0:
        return None
    for p in (doc.get("analysis", {}) or {}).get("phases", []) or []:
        if p.get("phase") == 5:
            return None
    tp = os.path.join(swing_dir, "truth.json")
    if not os.path.exists(tp):
        return None
    ev = (pc.read_json(tp) or {}).get("events", {}) or {}
    t0 = ev.get("t0_us")
    for key in ("p7_s", "impact_s"):
        if t0 is not None and ev.get(key) is not None:
            return int(t0) + int(round(float(ev[key]) * 1e6))
    return None


def choose_swings(corpus):
    pose_dir = os.path.join(corpus, "pose2")
    out, problems = [], []
    for f in sorted(os.listdir(pose_dir)):
        if not f.endswith(".json"):
            continue
        sid = f[:-5]
        if "__" not in sid:
            problems.append("%s: not <session>__<swing>" % f)
            continue
        session, swing = sid.split("__", 1)
        rel = "swings/%s/%s" % (session, swing)
        sd = os.path.join(corpus, "swings", session, swing)
        if not has_swing(sd):
            problems.append("%s: no swing document" % rel)
            continue
        dtl = "pose2_dtl/%s__dtl.json" % sid
        out.append({"id": sid, "swing": rel, "pose": "pose2/" + f,
                    "dtlPose": dtl if os.path.exists(os.path.join(corpus, dtl)) else None,
                    "impactUs": impact_rescue(sd, load_swing(sd))})
    return out, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--library", default=pc.LIBRARY, help="athlete library root (default %(default)s)")
    ap.add_argument("--corpus", default=pc.CORPUS, help="corpus root (default %(default)s)")
    ap.add_argument("--out", default=pc.DEFAULT_SET, help="where to write (default %(default)s)")
    ap.add_argument("--dry-run", action="store_true", help="print the choice, write nothing")
    a = ap.parse_args()

    sessions, missing = choose_sessions(survey_library(a.library))
    swings, problems = choose_swings(a.corpus)
    for s in sessions:
        print("%-11s %-55s %3d swings  %s  %s" % (s["category"], s["path"], s["swingCount"],
                                                 s["devices"], s["clubs"]))
    print("swings: %d pinned-pose swings from corpus/pose2 (%d with a pinned DTL pose, %d with an "
          "explicit impact)" % (len(swings), sum(1 for w in swings if w["dtlPose"]),
                                sum(1 for w in swings if w["impactUs"] is not None)))
    if missing or problems:
        for m in missing:
            print("STOP: no candidate session for category '%s'" % m, file=sys.stderr)
        for p in problems:
            print("STOP: %s" % p, file=sys.stderr)
        return 1
    if a.dry_run:
        return 0
    pc.write_canonical(a.out, {
        "schema": "pinpoint.parityset/1",
        "libraryRoot": "the athlete library root (/mnt/swingdata on the Mac, C:\\PinPointStudio on the cabin PC)",
        "corpusRoot": "the corpus root (<library root>/corpus)",
        "sessions": sessions,
        "swings": swings,
    })
    print("wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
