# assemble.py — PROTOTYPE of the SessionReportModel's output for one session (docs/design/session_report_design.md).
# Run through render-session.sh; see README.md.
#
# Reads the SessionDiagnosticsModel dump (probe/nick03.json), the swing documents' launch-monitor
# blocks (probe/nick03_lm.json), the pack (core.json, drills.json) and the sibling sessions'
# work_ons.json, and writes report.json: the ONE map the report register binds. Every sentence is
# assembled here from templates over model fields — the QML positions and paints, it writes no words.
#
# Content marked PROPOSED does not exist in the pack yet (cue, check, a drill for two conditions);
# it is what the design asks the pack to author, shown here so the page can be judged whole.
import json, os, statistics as st, glob, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import overlay

import argparse, datetime, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))   # tools/
import pp_swingdoc
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
ap = argparse.ArgumentParser()
ap.add_argument("--session", required=True, help="the session folder in the library")
ap.add_argument("--dump", required=True, help="the model dump from dump_model.qml, as JSON")
ap.add_argument("--note", default="", help="the coach's note (empty = none)")
ap.add_argument("--out", required=True, help="report.json to write; stills go in a stills/ folder beside it")
args = ap.parse_args()
SESSION_DIR = args.session.rstrip("/")
LIB = os.path.dirname(SESSION_DIR)
d = json.load(open(args.dump))
overlay.BASE = SESSION_DIR
overlay.S = os.path.join(os.path.dirname(os.path.abspath(args.out)), "stills"); os.makedirs(overlay.S, exist_ok=True)
# the swing documents, in folder order: launch monitor, athlete, clock, devices
docs = [pp_swingdoc.load_swing(p_) for p_ in sorted(glob.glob(SESSION_DIR + "/swing_*/swing.ppsw"))]
lm = [dict(doc_.get("launchMonitor") or {}) for doc_ in docs]
HAS_LM = any(r.get("carryDistance") is not None for r in lm)
pack = json.load(open(f"{REPO}/src/Resources/diagnostics/core.json"))
cond = {c["id"]: c for c in pack["conditions"]}
sig = {x["id"]: x for x in pack["signals"]}
meas = {x["id"]: x for x in pack["measures"]}
drills = {x["id"]: x for x in json.load(open(f"{REPO}/src/Resources/diagnostics/drills.json"))["drills"]}

WORDS = ["no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"]
def word(n): return WORDS[n] if 0 <= n < len(WORDS) else str(n)
def cap1(s): return s[:1].upper() + s[1:] if s else s

# ── swings, in order ─────────────────────────────────────────────────────────
readouts = sorted(d["readouts"], key=lambda r: r["swingDir"])
swing_dirs = [r["swingDir"] for r in readouts]
folder_no = [int(p[-4:]) for p in swing_dirs]
assert len(docs) == len(swing_dirs), (len(docs), len(swing_dirs))                  # swing_0012 -> 12 (the model's shot id)
ordinal = {n: i + 1 for i, n in enumerate(folder_no)}          # model shot id -> swing number in the report
N = len(swing_dirs)
def state_of(cid, i):
    cc = [c for c in readouts[i]["readout"]["conditions"] if c.get("id") == cid]
    return cc[0]["stateKind"] if cc else "notAssessable"

cards = {c["id"]: c for c in d["cards"]}
spread = lambda cid: d.get("spread." + cid, {})

# ── the work-on list: reduceSessionWorkOns over the model's ledger (class, resolving, root, score) ──
movement = [c for c in d["cards"] if cond[c["id"]]["kind"] in ("fault", "setup")]
delivery_cards = [c for c in d["cards"] if cond[c["id"]]["kind"] == "delivery"]
def score(c): return c["sessionExcess"]
# work-on semantics: a symptom goes behind its cause only when the session SUPPORTS the link
# (linkSupported). Every pattern here fired on every swing (range-restricted), so none is supported
# and every entry is a root — the card's rootHere (an authored-edge view) is not the work-on rule.
movement.sort(key=lambda c: (c["resolving"], -score(c), c["id"]))
workons = movement[:5]
if len(workons) < 3: workons += sorted(delivery_cards, key=lambda c: -score(c))[:3 - len(workons)]
maxEx = max([score(c) for c in workons] + [1e-9])

# ── PROPOSED pack content (cue, check, goal wording, and two drills the pack does not have) ──
PROPOSED = {
    "trail_knee_straighten": {
        "goal": "Keep the flex in your trail knee",
        "cue": "As you turn back, feel your trail knee stay soft and pointing at the ball while your trail pocket turns behind you.",
        "check": "Film from down the line. At the top, your trail knee should look about as bent as it did at address.",
        "camera": "DTL", "phase": "P4", "phaseWords": "at the top", "picture": "change", "line": "knee",
    },
    "hips_closed_at_impact": {
        "goal": "Have your hips open at impact",
        "cue": "Through the ball, feel your belt buckle already facing between the ball and the target.",
        "check": "Film face-on. At impact you should see the front of your lead hip, with your belt turned toward the target.",
        "drills": ["drill.impactHold"],
        "camera": "Face-On", "phase": "P7", "phaseWords": "at impact",
    },
    "early_extension": {
        "goal": "Stay in your posture through the ball",
        "cue": "Keep your backside on an imaginary wall behind you all the way to impact.",
        "check": "Film from down the line with a line drawn down the back of your hips at address. At impact your hips should still be touching it.",
        "camera": "DTL", "phase": "P7", "phaseWords": "at impact", "picture": "ghost", "line": "spine",
    },
    "reverse_spine_p4": {
        "goal": "Lean slightly away from the target at the top",
        "cue": "At the top, feel your sternum stay over your trail thigh.",
        "check": "Film face-on. At the top your head and chest should sit a little behind the ball, never toward the target.",
        "drills": ["drill.topTilt"],
        "camera": "Face-On", "phase": "P4", "phaseWords": "at the top",
    },
    "over_the_top": {
        "goal": "Bring your hands down inside the path they went up",
        "cue": "Start down by letting your trail elbow drop toward your trail hip pocket.",
        "check": "Film from down the line. Coming down, your hands should drop below the line they went up on.",
        "camera": "DTL", "phase": "P6", "phaseWords": "coming down",
    },
}
PROPOSED_TARGET = {"inconsistent_sequence": "Your peak timing repeating within twice the professionals' spread, over a session."}
PROPOSED_DRILLS = {
    "drill.impactHold": {
        "id": "drill.impactHold", "label": "Impact hold", "proposed": True,
        "instruction": "Make slow swings to impact and stop there. Check three things: your belt buckle points between the ball and the target, your trail heel is just off the ground, and your hands are level with your lead thigh. Hold for two seconds, then finish the swing slowly.",
        "targets": "To feel where your hips need to be when the club reaches the ball, so the turn has somewhere to go.",
        "equipment": ["a mirror or a phone on a stand"],
        "note": "If your hips will not open without your shoulders opening too, that is a mobility question before it is a practice one.",
    },
    "drill.topTilt": {
        "id": "drill.topTilt", "label": "Club across the chest", "proposed": True,
        "instruction": "Without a ball, hold a club across your chest. Turn to the top and check in a mirror that the shaft tilts down toward the target and your chest sits behind the ball. Ten slow turns.",
        "targets": "To feel the upper body staying behind the ball as it turns, rather than tipping toward the target.",
        "equipment": ["a mirror"],
        "note": "A short turn often hides inside this one: if the shoulders cannot turn further, the body tips instead.",
    },
}
def drills_for(cid):
    ids = (cond[cid].get("drills") or []) or PROPOSED.get(cid, {}).get("drills", [])
    out = []
    for i in ids:
        x = drills.get(i) or PROPOSED_DRILLS.get(i)
        if x: out.append({"label": x["label"], "instruction": x["instruction"], "targets": x.get("targets", ""),
                          "equipment": ", ".join(x.get("equipment", [])), "note": x.get("note", ""), "proposed": bool(x.get("proposed"))})
    return out

# ── figures ──────────────────────────────────────────────────────────────────
def run_figure(cid):
    s = spread(cid)
    if not s: return None
    pts = []
    for r in s.get("run", []):
        pts.append({"swing": ordinal[r["shotId"]], "value": r.get("value") if r.get("assessable") else None,
                    "state": r["state"], "text": r.get("valueText", "-")})
    pts.sort(key=lambda p: p["swing"])
    bands = [{"grade": b["grade"], "lo": b["lo"], "hi": b["hi"]} for b in s.get("bands", [])]
    return {"axisLo": s.get("axisLo"), "axisHi": s.get("axisHi"), "unit": s.get("unit", ""), "decimals": s.get("decimals", 0),
            "bands": bands, "points": pts, "medianText": s.get("medianText", "")}

def pass_edge(cid):
    # the edge the golfer is asked to get back inside: the nearer pass edge on the side they miss
    bands = spread(cid).get("bands", [])
    good = [b for b in bands if b["grade"] in ("ideal", "good")]
    if not good: return None
    lo, hi = min(b["lo"] for b in good), max(b["hi"] for b in good)
    med = spread(cid).get("medianValue")
    return lo if med is not None and med < lo else hi

# ── the ball (GC Quad) ───────────────────────────────────────────────────────
def col(k): return [r[k] for r in lm if r.get(k) is not None]
def med(k): return st.median(col(k)) if col(k) else float("nan")
def lr(v, unit):  # left/right words for a right-handed golfer
    if abs(v) < 0.5: return f"0 {unit}".strip()
    return f"{abs(v):.0f}{unit} {'right' if v > 0 else 'left'}"
ball_tiles = [] if not HAS_LM else [
    {"label": "Carry", "value": f"{med('carryDistance'):.0f}", "unit": "yd", "range": f"{min(col('carryDistance')):.0f}–{max(col('carryDistance')):.0f}"},
    {"label": "Ball speed", "value": f"{med('ballSpeed'):.0f}", "unit": "mph", "range": f"{min(col('ballSpeed')):.0f}–{max(col('ballSpeed')):.0f}"},
    {"label": "Club speed", "value": f"{med('clubheadSpeed'):.0f}", "unit": "mph", "range": f"{min(col('clubheadSpeed')):.0f}–{max(col('clubheadSpeed')):.0f}"},
    {"label": "Smash", "value": f"{med('smashFactor'):.2f}", "unit": "", "range": f"{min(col('smashFactor')):.2f}–{max(col('smashFactor')):.2f}"},
    {"label": "Launch", "value": f"{med('launchAngle'):.0f}°", "unit": "", "range": f"{min(col('launchAngle')):.0f}–{max(col('launchAngle')):.0f}"},
    {"label": "Spin", "value": f"{med('spinRate'):,.0f}", "unit": "rpm", "range": f"{min(col('spinRate')):,.0f}–{max(col('spinRate')):,.0f}"},
    {"label": "Club path", "value": f"{abs(med('clubPath')):.0f}°", "unit": "in-to-out" if med('clubPath') > 0 else "out-to-in", "range": f"{min(col('clubPath')):+.1f} to {max(col('clubPath')):+.1f}".replace("-", "−")},
    {"label": "Offline", "value": f"{max(abs(min(col('offline'))), abs(max(col('offline')))):.0f}", "unit": "yd", "range": "widest, either side"},
]
numbers_cols = [("Swing", None), ("Carry", "carryDistance", "yd", 0), ("Total", "totalDistance", "yd", 0), ("Ball", "ballSpeed", "mph", 0),
                ("Club", "clubheadSpeed", "mph", 0), ("Smash", "smashFactor", "", 2), ("Launch", "launchAngle", "°", 0),
                ("Spin", "spinRate", "rpm", 0), ("Path", "clubPath", "°", 1), ("Face", "faceAngle", "°", 1),
                ("Face to path", "faceToPath", "°", 1), ("Attack", "attackAngle", "°", 1), ("Offline", "offline", "yd", 0)]
num_rows = []
for i, r in enumerate(lm if HAS_LM else []):
    row = [str(i + 1)] + [(f"{r[k]:,.{dp}f}" if r.get(k) is not None else "-") for _, k, u, dp in numbers_cols[1:]]
    num_rows.append({"cells": row, "habits": sum(1 for c in workons if state_of(c["id"], i) == "fired")})
num_med = ["Typical"] + [(f"{st.median(col(k)):,.{dp}f}" if col(k) else "-") for _, k, u, dp in numbers_cols[1:]] if HAS_LM else []

# ── strengths: a quiet fault/outcome the golfer was clean on, most commonly seen in golfers first ──
PROM = {"ubiquitous": 5, "common": 4, "occasional": 3, "uncommon": 2, "rare": 1}
OUTCOME_WELL = {"slice": "No slices", "hook": "No hooks", "pull": "Nothing pulled", "push": "Nothing pushed",
                "strike_heel": "Nothing off the heel", "chunk": "No heavy contact"}
strengths = []
for cid, c in cond.items():
    states = [state_of(cid, i) for i in range(N)]
    judged = sum(1 for s in states if s in ("fired", "clean"))
    if judged < max(3, math.ceil(0.8 * N)) or any(s == "fired" for s in states): continue
    if c["kind"] == "fault" and c.get("golferWell"):
        strengths.append({"id": cid, "text": cap1(c["golferWell"]) + ".", "count": f"all {judged} swings" if judged == N else f"{judged} of {N} swings", "p": PROM[c["prominence"]], "k": 1})
    elif c["kind"] == "outcome" and cid in OUTCOME_WELL and not any(
            o.get("axis") == c.get("axis") and o["id"] != cid and "fired" in [state_of(o["id"], k) for k in range(N)] for o in cond.values()):
        strengths.append({"id": cid, "text": OUTCOME_WELL[cid] + ".", "count": f"0 of {judged} swings", "p": PROM[c["prominence"]], "k": 0})
wo_ids = {c["id"] for c in workons}
rank_of = {c["id"]: i + 1 for i, c in enumerate(workons)}
linked_to = {}
for e in pack["edges"]:
    if e["type"] != "causes": continue
    if e["from"] in wo_ids: linked_to[e["to"]] = max(linked_to.get(e["to"], 0), 20 - rank_of[e["from"]])  # a work-on's usual effect: it has not spread
    elif e["to"] in wo_ids: linked_to.setdefault(e["from"], 1)                                                # could have caused one: not the source
for x in strengths: x["linked"] = linked_to.get(x["id"], 0)
strengths.sort(key=lambda s: (-s["linked"], -s["p"], s["k"], s["id"]))
# one outcome + two movements, the most commonly missed first
pick = [s for s in strengths if s["k"] == 0][:1] + [s for s in strengths if s["k"] == 1][:2]

# ── generic content lookups: PROPOSED where it exists, the pack's own words otherwise, nothing invented ──
PHASE_WORDS = {"P1": "at address", "P2": "going back", "P3": "going back", "P4": "at the top", "P5": "coming down",
               "P6": "coming down", "P7": "at impact", "P8": "after impact", "P9": "after impact", "P10": "at the finish", "": "over the session"}
def goal_of(cid_):
    pr_ = PROPOSED.get(cid_, {})
    return pr_.get("goal") or cap1(cond[cid_].get("golferWell") or cond[cid_]["label"])
def phase_of(c): return PROPOSED.get(c["id"], {}).get("phase") or c.get("phase") or ""
def count_words(c):
    if c.get("sessionScope"): return "over the session"
    return f'{c["fired"]} of {c["assessable"]} swings'
def wo_rank(cid_): return next(i + 1 for i, c in enumerate(workons) if c["id"] == cid_)

# ── the swing grid: habits, then the watch list's movements and the ball's, each by how often ──
SHORT = {"pelvis_thrust_backswing": "Hips move toward the ball going back", "excessive_head_sway": "Head sways off the ball",
         "closed_face_to_path": "Face closed to the path", "lie_steep_at_impact": "Club returns toe-up at impact"}   # PROPOSED short golfer names
def row_name(cid_): return SHORT.get(cid_, cond[cid_]["label"])
def marks_of(cid_): return [state_of(cid_, i) for i in range(N)]
def count_of(m): return f'{m.count("fired")} of {sum(1 for x in m if x in ("fired", "clean"))}'
grid_rows = [{"group": "habit", "name": row_name(c["id"]), "marks": marks_of(c["id"]), "count": count_of(marks_of(c["id"]))} for c in workons if not c.get("sessionScope")]
grid_session_note = " ".join(f"{row_name(c['id'])} is read over the whole session, not swing by swing: see work-on {i + 1}." for i, c in enumerate(workons) if c.get("sessionScope"))
wo_ids_ = {c["id"] for c in workons}
others = [cid_ for cid_ in cond if cid_ not in wo_ids_ and cond[cid_]["kind"] != "capacity"]
def often(cid_): m = marks_of(cid_); return m.count("fired")
seen_ids = sorted([x for x in others if cond[x]["kind"] in ("fault", "setup") and often(x) >= max(2, math.ceil(0.3 * N))], key=lambda x: -often(x))[:3]
ball_ids = sorted([x for x in others if cond[x]["kind"] in ("outcome", "delivery") and often(x) >= max(2, math.ceil(0.3 * N))], key=lambda x: -often(x))[:3]
grid_rows += [{"group": "seen", "name": row_name(x), "marks": marks_of(x), "count": count_of(marks_of(x))} for x in seen_ids]
grid_rows += [{"group": "ball", "name": row_name(x), "marks": marks_of(x), "count": count_of(marks_of(x))} for x in ball_ids]
carry = [round(r["carryDistance"]) for r in lm] if HAS_LM else []
fired_n = [sum(1 for c in workons if state_of(c["id"], i) == "fired") for i in range(N)]
# the swing that came CLOSEST to the target ranges: summed miss past each work-on's pass edge, in
# units of that range's width (so a degree of knee and a % of hand rise add up fairly)
def pass_range(cid):
    good = [b for b in spread(cid).get("bands", []) if b["grade"] in ("ideal", "good")]
    return (min(b["lo"] for b in good), max(b["hi"] for b in good)) if good else None
def miss(cid, i):
    r = next((x for x in spread(cid).get("run", []) if ordinal[x["shotId"]] == i + 1), None)
    pr = pass_range(cid)
    if not r or not r.get("assessable") or pr is None: return 0.0
    if not spread(cid): return 0.0
    lo, hi = pr; v = r["value"]
    return (max(0.0, lo - v) + max(0.0, v - hi)) / (hi - lo)
miss_n = [sum(miss(c["id"], i) for c in workons) for i in range(N)]
best_i = min(range(N), key=lambda i: (miss_n[i], -(lm[i].get("carryDistance") or 0)))

# ── earlier today: the same day's sessions, oldest first, from their work-on records ──
day = os.path.basename(SESSION_DIR)[:10]
siblings = sorted(p_ for p_ in glob.glob(f"{LIB}/{day}_*") if os.path.isdir(p_))
today = []
for p_ in siblings:
    if p_ == SESSION_DIR:
        today.append({"label": f"Session {len(today) + 1}", "swings": N, "ev": None}); continue
    try: w = json.load(open(p_ + "/work_ons.json"))
    except Exception: continue
    if w.get("shotCount", 0) < 3: continue
    today.append({"label": f"Session {len(today) + 1}", "swings": w["shotCount"], "ev": {e["id"]: e for e in w["evidence"]}, "wo": [e["id"] for e in w["workOns"]]})
this_idx = next(i for i, t in enumerate(today) if t["ev"] is None)
earlier_wo = [x for t in today if t["ev"] for x in t["wo"]]
today_ids = [c["id"] for c in workons] + [x for x in dict.fromkeys(earlier_wo) if x not in wo_ids_][:2]
today_rows = []
for cid in today_ids:
    marks = []
    for t in today:
        if t["ev"] is None:
            m = marks_of(cid); f = m.count("fired"); a = sum(1 for x in m if x in ("fired", "clean"))
            scope = bool((cards.get(cid) or {}).get("sessionScope"))
            marks.append({"state": "pattern" if cid in cards else ("quiet" if a else "none"), "text": "" if scope else (f"{f}/{a}" if a else "-")}); continue
        e = t["ev"].get(cid)
        scope = bool((cards.get(cid) or {}).get("sessionScope"))
        marks.append({"state": e["state"], "text": "" if scope else f'{e["fired"]}/{e["assessable"]}'} if e else {"state": "none", "text": "-"})
    today_rows.append({"name": row_name(cid), "marks": marks})
# one generated sentence: what was already there in the first session, and what came down
first = today[0] if today and today[0]["ev"] else None
notes = []
if first:
    already = [c["id"] for c in workons if (first["ev"].get(c["id"]) or {}).get("state") == "pattern"]
    if already: notes.append(f"{cap1(word(len(already)))} of today's habits were already there in your first session.")
    for cid in today_ids:
        if cid in cards: continue
        was = [t for t in today if t["ev"] and (t["ev"].get(cid) or {}).get("state") == "pattern"]
        m = marks_of(cid)
        if not was: continue
        f_, a_ = m.count("fired"), sum(1 for x in m if x in ("fired", "clean"))
        if a_ < 3: notes.append(f"{row_name(cid)} was a habit earlier today; this session could only judge it on {word(a_)} swing{'s' if a_ != 1 else ''}, too few to say.")
        else: notes.append(f"{row_name(cid)} was a habit earlier today and came down to {f_} of {a_} here.")
today_note = " ".join(notes)
# ── why this order: every "today" line from what the reduction saw ──
all_every = all(c["fired"] >= c["assessable"] - 1 for c in workons if not c.get("sessionScope"))
n_res = sum(1 for c in workons if c["resolving"])
nw = len(workons)
order_items = []
for i, c in enumerate(workons):
    order_items.append({"rank": i + 1, "name": cond[c["id"]]["label"], "goal": goal_of(c["id"]),
                        "phase": phase_of(c), "share": round(score(c) / maxEx, 3),
                        "count": count_words(c)})
links = []
for e in pack["edges"]:
    if e["type"] == "causes" and e["from"] in wo_ids_ and e["to"] in wo_ids_:
        links.append({"from": cond[e["from"]]["label"], "to": cond[e["to"]]["label"],
                      "words": "Linked in the coaching model. " + ("This session cannot test it, because both happened on every swing." if all_every else "This session did not show the link.")})
rules = [
    {"title": "What is still happening comes first",
     "body": "A habit you started fixing during the session goes to the back of the list.",
     "today": (f"None of the {word(nw)} eased during the session, so this rule changed nothing today." if not n_res
               else f"{cap1(word(n_res))} eased during the session and moved to the back.")},
    {"title": "Causes before the things they cause",
     "body": "When one habit sets up another, working on the first often fixes the second for free. We only put one behind another when your swings show it: the second goes away on the swings where the first does.",
     "today": (f"It could not be tested today. All {word(nw)} showed on every swing (or all but one), so there is no swing without one to compare against."
               if all_every else "None of today's habits went away on the swings where another did, so this rule changed nothing today.")},
    {"title": "Then the biggest miss first",
     "body": "Each habit is measured against a target range. The one that missed by most, for its range, comes first.",
     "today": "This is what set today's order. The bars below show how far each one missed."},
]
not_listed = []
for c in d["cards"]:
    if c["id"] in wo_ids_: continue
    k = cond[c["id"]]["kind"]
    if k in ("delivery", "outcome"):
        par = [e["from"] for e in pack["edges"] if e["type"] == "causes" and e["to"] == c["id"] and e["from"] in wo_ids_]
        why = "A result, not a movement to practise" + (f": the coaching model links it to {cond[par[0]]['label'].lower()} (work-on {wo_rank(par[0])}). Fix that, and it should follow." if par else ". It is what the club or ball did, and it changes when the movements behind it do.")
        not_listed.append({"name": f"{row_name(c['id'])} ({c['fired']} of {c['assessable']})", "why": why})
for x in ball_ids[:2]:
    if cond[x]["kind"] == "outcome":
        m = marks_of(x)
        not_listed.append({"name": f"{row_name(x)} ({count_of(m)})", "why": "Something the ball did, not a movement to practise. It is on the watch list until it repeats often enough to call a habit."})
# ── each work-on in depth: EVERY sentence from a template over model + pack fields ──
# PROPOSED pack field on a measure: `golferName` (what the number is, in the golfer's words) and
# `golferSign` (+1: report the value as it is; -1: report its magnitude, the words carry the sign).
PROPOSED_MEASURE = {
    "m_trailKneeFlex":     {"golferName": "trail-knee bend lost by the top", "sign": -1},
    "m_pelvisRotP7":       {"golferName": "hip turn at impact", "sign": 1},
    "m_spineBendLossDown": {"golferName": "posture lost by impact", "sign": -1},
    "m_handPathLoop":      {"golferName": "hand rise coming down", "sign": 1},
    "m_axisTiltAtTop":     {"golferName": "lean away from the target at the top", "sign": 1},
}
TREND = {"stable": "steady from first swing to last", "improving": "easing as the session went on", "worsening": "growing as the session went on"}
def mword(mid):
    return PROPOSED_MEASURE.get(mid) or {"golferName": (meas.get(mid, {}).get("label") or "the reading").split(",")[0].lower(), "sign": 1}
def fmt(v, unit, dp=0):
    s = f"{v:.{dp}f}".replace("-", "−")
    if unit == "°": return s + "°"
    if unit.startswith("%"): return s + "% " + unit[1:].strip() if unit[1:].strip() else s + "%"
    return s + (" " + unit if unit else "")
def show(v, mid):  # the value in golfer words: magnitude when the words carry the sign
    return abs(v) if mword(mid)["sign"] < 0 else v
def range_words(cid, mid, unit):
    pr = pass_range(cid)
    if not pr: return ""
    lo, hi = pr; sp = spread(cid); shape = sp.get("shape")
    if mword(mid)["sign"] < 0:   # words carry the sign: "less than 12°"
        edge = lo if shape == "floor" or abs(lo) > abs(hi) else hi
        return f"less than {fmt(abs(edge), unit)}"
    if shape == "floor":   return f"{fmt(lo, unit)} or more"
    if shape == "ceiling": return f"{fmt(hi, unit)} or less"
    return f"{fmt(lo, unit)} to {fmt(hi, unit)}"
def swings_words(f, a): return f"all {a} swings" if f == a else f"{f} of {a} swings"
def judged(cid): return [state_of(cid, k) for k in range(N)]
PROPOSED_GOLFER = {"lie_steep_at_impact": "the club coming back to the ball toe-up"}
def and_list(xs): return xs[0] if len(xs) == 1 else ", ".join(xs[:-1]) + " and " + xs[-1]
def came_with(cid):
    """Authored effects the session judged: which followed, which did not. Co-occurrence, never cause."""
    shown, clean = [], []
    for e in pack["edges"]:
        if e["type"] != "causes" or e["from"] != cid or e["to"] in rank_of: continue
        t = cond[e["to"]]; st_ = judged(e["to"]); a = sum(1 for s in st_ if s in ("fired", "clean")); f = st_.count("fired")
        if a < 3: continue
        name = t.get("golfer") or t["label"].lower()
        if f >= max(2, a // 2): shown.append((e["to"], f"{SHORT.get(e['to'], '').lower() or PROPOSED_GOLFER.get(e['to'], t.get('golfer') or t['label'].lower())} ({f} of the {a} swings we could see)"))
        elif f == 0 and t.get("golferWell"): clean.append(t["golferWell"])
    out = []
    if shown:
        out.append("It came with " + and_list([x[1] for x in shown]) + ". That fits the coaching model, though showing up together is not proof that one causes the other.")
    if clean:
        lead = "It often drags other habits in with it. Not today — on every swing, " if not shown else "And on every swing, "
        out.append(lead + and_list(clean[:3]) + ".")
    return " ".join(out)
def linked_words(cid):
    out = []
    for e in pack["edges"]:
        if e["type"] != "causes": continue
        if e["to"] == cid and e["from"] in rank_of:
            out.append(f"May follow from {cond[e['from']]['label'].lower()} (work-on {rank_of[e['from']]}). The coaching model links them; today's swings cannot separate them, because both happened on every swing.")
        if e["from"] == cid and e["to"] in rank_of:
            out.append(f"May lead to {cond[e['to']]['label'].lower()} (work-on {rank_of[e['to']]}). The coaching model links them; today's swings cannot separate them, because both happened on every swing.")
    return " ".join(out)

def golfer_sign(fig, mid):
    # the chart reads in the golfer's sign: a measure whose words carry the sign is drawn as a magnitude
    if fig is None or mword(mid)["sign"] > 0: return fig
    fig = json.loads(json.dumps(fig))
    for p_ in fig["points"]:
        if p_["value"] is not None: p_["value"] = -p_["value"]
    fig["bands"] = [{"grade": b_["grade"], "lo": -b_["hi"], "hi": -b_["lo"]} for b_ in fig["bands"]]
    fig["axisLo"], fig["axisHi"] = -fig["axisHi"], -fig["axisLo"]
    return fig
# ── what might be behind each work-on: EVERY authored cause, sorted by what we know about it ──
# seen today · checked and not there · couldn't be seen today · can't be measured yet · a physical test would tell.
# A cause the session could not judge is NAMED, never dropped: when the evidence is thin, the unmeasured
# candidates are the most useful thing the report can give the coach.
screens = {x["id"]: x for x in json.load(open(f"{REPO}/src/Resources/diagnostics/screens.json"))["screens"]}
STRENGTH = {"strong": 0, "moderate": 1, "weak": 2}
def cause_name(cid_):
    x = cond[cid_]
    return cap1(x["golfer"]) if x.get("golfer") else x["label"]
def classify(cid_):
    x = cond[cid_]
    if x.get("observability") == "latent" and x.get("screenRef"): return "screen", screens.get(x["screenRef"], {}).get("label", "")
    # the session's own evidence first: a cause it judged is judged, whatever its measure's status says
    st_ = judged(cid_); a = sum(1 for s_ in st_ if s_ in ("fired", "clean")); f = st_.count("fired")
    ms = [m_ for s_ in x.get("detectedBy", []) for m_ in sig[s_]["measures"]]
    if a < 3 and (not ms or all(meas[m_].get("status") != "live" for m_ in ms)): return "cantMeasure", ""
    if a < 3: return "notSeen", ""
    if c_pattern(cid_): return "seen", f"{f} of {a} swings"
    if f == 0: return "ruledOut", f"0 of {a}"
    return "ruledOut", f"{f} of {a}"          # seen now and then, not a habit
def c_pattern(cid_): return any(c["id"] == cid_ for c in d["cards"])
# PROPOSED pack field: `golfer` on capacity conditions too (the screens' own labels are clinical)
CAPACITY_GOLFER = {
    "limited_lead_hip_ir": "Lead-hip mobility", "limited_trail_hip_ir": "Trail-hip mobility",
    "poor_pelvic_disassociation": "Turning your hips without your chest", "limited_thoracic_rotation": "Upper-back mobility",
    "poor_core_stability": "Core stability", "poor_glute_strength": "Glute strength",
    "limited_lead_ankle_dorsiflexion": "Lead-ankle mobility", "limited_trail_ankle_dorsiflexion": "Trail-ankle mobility",
    "limited_hip_extension": "Hip extension", "limited_hamstring_length": "Hamstring length",
}
HOW = {"screen": "A quick physical test would tell", "cantMeasure": "Your coach can look", "notSeen": "Hidden on today's swings"}
def causes_for(cid):
    """Three groups, in the document's voice: spotted (with its evidence), could be behind it but unchecked
    (each with how it could be checked), and ruled out (measured, and fine)."""
    edges = sorted([e for e in pack["edges"] if e["type"] == "causes" and e["to"] == cid], key=lambda e: STRENGTH.get(e["strength"], 3))
    spotted, possible, ruled = [], [], []
    for e in edges:
        k, extra = classify(e["from"])
        x = cond[e["from"]]
        if k == "seen":
            wo_ = f", work-on {rank_of[e['from']]}" if e["from"] in rank_of else ""
            spotted.append(f"{cause_name(e['from'])} — on {extra.replace(' swings', '')} swings{wo_}")
        elif k == "ruledOut":
            ruled.append(cause_name(e["from"]))
        else:
            possible.append((k, CAPACITY_GOLFER.get(e["from"]) or (cap1(x["label"].replace("Limited ", "").replace("Poor ", "")) if x["kind"] == "capacity" else cause_name(e["from"]))))
    if spotted and any(e_ in (cards.get(cid, {}) or {}) for e_ in []):
        pass
    if not edges:
        lead = "We don't know of a cause for this one yet. Worth talking through with your coach."
    elif spotted:
        lead = ("One possible cause showed up on the same swings. Because both happened on every swing, we can't prove one drives the other — "
                "so here is what we spotted, what we couldn't check, and what we've ruled out.")
    elif possible:
        lead = "We couldn't pin down what drives this one today. Here are the possible causes: the ones we couldn't check, and the ones we've ruled out."
    else:
        lead = ("We checked every cause we know of for this one, and each was fine — so it may come from something we can't yet put a name to. "
                "Worth talking through with your coach.")
    rows = []
    if spotted:  rows.append({"kind": "seen", "title": "What we spotted", "items": spotted, "more": "", "note": "it fits, but isn't proof"})
    if possible:
        # grouped by how it could be checked, the group holding the most likely cause first; six names at most
        kept = possible[:6]; order = []
        for k, _ in kept:
            if k not in order: order.append(k)
        segs = [f"<i>{HOW[k]}:</i> " + " · ".join(n for kk, n in kept if kk == k) for k in order]
        rows.append({"kind": "possible", "title": "Could be behind it — we couldn't check", "items": ["<br>".join(segs)],
                     "more": f"{len(possible) - 6} more, less likely" if len(possible) > 6 else "", "note": ""})
    if ruled:    rows.append({"kind": "ruledOut", "title": "Ruled out — measured, and fine", "items": ruled[:4],
                              "more": f"{len(ruled) - 4} more" if len(ruled) > 4 else "", "note": ""})
    ids = {"screen": [], "cantMeasure": [], "notSeen": []}
    for e in edges:
        k, _ = classify(e["from"])
        if k in ids: ids[k].append(e["from"])
    return {"lead": lead, "rows": rows, "ids": ids}

bk = {b["id"]: b for b in d["bookends"]}
def still(folder, cam, ph): return "file://" + overlay.still(folder, cam, ph)
deep = []
for i, c in enumerate(workons):
    cid = c["id"]; pc = cond[cid]; pr = PROPOSED.get(cid, {}); sp = spread(cid)
    mid = c["measureId"]; unit = sp.get("unit", ""); dp = sp.get("decimals", 0)
    ph = phase_of(c)
    pair = None; saw = ""; target = ""; fig = None
    if c.get("sessionScope") or not sp:
        # a session-level habit has no per-swing run: say what fired, over the session, in the card's own figures
        cons = c.get("consistency") or {}
        if cons.get("spreadAvailable"):
            meas_words = f"Your {cons.get('spreadSegment', 'body')}'s peak timing varied {cons['spreadValue']:.1f}× ± {cons['spreadSigma']:.1f} as widely as touring professionals' do."
        else:
            meas_words = ""
        saw = f"{cap1(pc.get('golfer') or pc['label'])} — read over the whole session, never one swing. " + meas_words
        target = PROPOSED_TARGET.get(cid, "")
    else:
        medv = sp["medianValue"]
        runs = [r for r in sp["run"] if r.get("assessable")]
        typ = min(runs, key=lambda r: abs(r["value"] - medv))["shotId"] if runs else None
        val = lambda sid: next(r for r in sp["run"] if r["shotId"] == sid)["value"]
        pic = pr.get("picture", "auto")
        cam = pr.get("camera", "Face-On"); pph = ph if ph in overlay.PH else None
        if i < 3 and pic == "change":
            pair = [{"src": "file://" + overlay.make(typ, cam, "P1", pr["line"]), "caption": f"Address, swing {ordinal[typ]}", "value": "",
                     "note": f"Your typical swing (swing {ordinal[typ]}). The line is your trail leg, hip to knee to ankle."},
                    {"src": "file://" + overlay.make(typ, cam, pph, pr["line"]), "caption": cap1(PHASE_WORDS[pph].replace("at the ", "").replace("at ", "")),
                     "value": fmt(show(val(typ), mid), unit, dp) + " lost", "note": ""}]
        elif i < 3 and pic == "ghost":
            pair = [{"src": "file://" + overlay.make(typ, cam, pph, pr["line"], ghost_address=True), "caption": f"{cap1(PHASE_WORDS[pph])}, swing {ordinal[typ]}",
                     "value": fmt(show(val(typ), mid), unit, dp) + " lost", "note": "Orange: your spine at impact. White dashes: the same swing at address. Every swing looked much like this one."}]
        elif i < 3 and pph and runs and pass_range(cid):
            best = min(runs, key=lambda r: miss(cid, ordinal[r["shotId"]] - 1))["shotId"]
            worst = max(runs, key=lambda r: miss(cid, ordinal[r["shotId"]] - 1))["shotId"]
            lo_, hi_ = pass_range(cid)
            if abs(val(worst) - val(best)) >= 0.5 * (hi_ - lo_):
                pair = [{"src": still(best, cam, pph), "caption": f"Closest, swing {ordinal[best]}", "value": fmt(show(val(best), mid), unit, dp)},
                        {"src": still(worst, cam, pph), "caption": f"Furthest, swing {ordinal[worst]}", "value": fmt(show(val(worst), mid), unit, dp)}]
            else:
                pair = [{"src": still(typ, cam, pph), "caption": f"Typical, swing {ordinal[typ]}", "value": fmt(show(val(typ), mid), unit, dp),
                         "note": "Every swing looked much like this one."}]
        pf = sp.get("pastFault", 0)
        size = "a small miss: just outside the range, never far"
        rw = range_words(cid, mid, unit)
        saw = (f"{cap1(pc.get('golfer') or pc['label'])} — on {swings_words(c['fired'], c['assessable'])}, {TREND.get(c.get('trend', 'stable'), TREND['stable'])}. "
               f"Typical {mword(mid)['golferName']}: {fmt(show(medv, mid), unit, dp)}" + (f", against a target of {rw}" if rw else "")
               + (f"; this is {size}." if pf == 0 and rw else "."))
        target = f"{cap1(mword(mid)['golferName'])}: {rw}, on most swings. Today: {fmt(show(medv, mid), unit, dp)}." if rw else ""
        fig = golfer_sign(run_figure(cid), mid)
    deep.append({
        "rank": i + 1, "id": cid, "name": pc["label"], "goal": goal_of(cid), "phase": ph, "phaseWords": PHASE_WORDS.get(ph, ""),
        "also": ", ".join(f"“{a}”" for a in (pc.get("aliases") or [])[:2]),
        "count": count_words(c),
        "saw": saw, "why": pc.get("golferWhy", ""), "cameWith": came_with(cid), "causes": causes_for(cid),
        "drills": drills_for(cid), "cue": pr.get("cue", ""), "check": pr.get("check", ""), "target": target,
        "figure": fig, "figureLabel": ("Each dot is one swing: " + mword(mid)["golferName"] + ", in " + ("degrees" if unit == "°" else (unit.replace("%", "percent of").strip() if unit.startswith("%") else (unit or "units"))) + ".") if fig else "",
        "stills": pair, "injury": pc.get("injuryNote", ""),
    })

# ── where each work-on happens in the swing: stops Address·Backswing·Top·Coming down·Impact·Finish ──
STOP = {"P1": 0, "P2": 1, "P3": 1, "P4": 2, "P5": 3, "P6": 3, "P7": 4, "P8": 5, "P9": 5, "P10": 5}
SPAN_BACK = {"m_trailKneeFlex"}            # a change measured address → top sits on the way up, not at the top
timeline, seen = [], {}
for i, c in enumerate(workons):
    if not phase_of(c): continue                     # a session-level habit has no place in the swing
    pos = STOP[phase_of(c)] - (0.4 if c["measureId"] in SPAN_BACK else 0)
    k = seen.get(pos, 0); seen[pos] = k + 1
    timeline.append({"rank": i + 1, "pos": pos + (0.3 * k if k else 0) - (0.15 if pos in [t["pos"] for t in timeline] and not k else 0)})
earliest = bool(timeline) and min(timeline, key=lambda t: t["pos"])["rank"] == 1
timeline_note = ("Your first work-on is also the earliest in the swing. Habits early in the swing tend to set up the ones after them, which is one more reason to start there."
                 if earliest else "Your first work-on is not the earliest in the swing; it leads because it missed by most. Your coach may still choose to start earlier in the swing.")

# ── worth checking: unmeasured causes that could sit behind the most work-ons ──
behind = {}
for w in deep:
    for k in ("screen", "cantMeasure"):
        for cid_ in w["causes"]["ids"][k]:
            behind.setdefault(cid_, {"kind": k, "ranks": []})["ranks"].append(w["rank"])
def wl(rs): return "work-on " + str(rs[0]) if len(rs) == 1 else "work-ons " + and_list([str(r) for r in rs])
# grouped by how each could be checked; the work-ons it could sit behind in brackets
checks = []
for kind, intro in [("screen", "A quick physical test would tell. Each could sit behind two or more of your work-ons:" ),
                    ("cantMeasure", "Your coach can look; the cameras can't see these yet:")]:
    xs = sorted([(cid_, b_) for cid_, b_ in behind.items() if b_["kind"] == kind], key=lambda kv: (-len(kv[1]["ranks"]), kv[0]))
    if kind == "screen": xs = [kv for kv in xs if len(kv[1]["ranks"]) >= 2] or xs[:3]
    if not xs: continue
    if kind == "screen" and any(len(b_["ranks"]) < 2 for _, b_ in xs): intro = "A quick physical test would tell:"
    names = [f"{CAPACITY_GOLFER.get(c_, cond[c_]['label'])} ({', '.join(str(r) for r in sorted(b_['ranks']))})" for c_, b_ in xs[:5]]
    checks.append({"title": intro, "body": " · ".join(names)})
checks_more = 0

# ── assemble ─────────────────────────────────────────────────────────────────
n_every = sum(1 for c in workons if not c.get("sessionScope") and c["fired"] == c["assessable"])
n_rep = sum(1 for c in workons if not c.get("sessionScope"))
PART = {"P1": "your set-up", "P2": "your backswing", "P3": "your backswing", "P4": "your backswing", "P5": "your downswing",
        "P6": "your downswing", "P7": "impact", "P8": "your follow-through", "P9": "your follow-through", "P10": "your finish", "": "your timing"}
ath = docs[0].get("athlete", {}); golfer = ath.get("name", "Golfer"); first = golfer.split()[0]
t0 = datetime.datetime.fromisoformat(docs[0]["clock"]["wallclock"].replace("Z", "+00:00")).astimezone()
diag = json.load(open(SESSION_DIR + "/diagnostics.json"))
clubs = list(dict.fromkeys(x.get("club", "") for x in diag["ledger"]["shots"] if x.get("club")))
club_words = ", ".join(c_.lower().replace("iron", "iron").replace("driver", "driver") for c_ in clubs) or "-"
cams = sorted({s_["alias"] for doc_ in docs for s_ in doc_["streams"] if s_["kind"] == "video"})
lm_kind = next((r.get("kind") for r in lm if r.get("kind")), "")
LM_NAME = {"gcquad": "GC Quad"}.get(lm_kind, lm_kind)
cam_words = " and ".join({"Face-On": "face-on", "DTL": "down-the-line"}.get(c_, c_) for c_ in cams)
today_n = len(today)
prepared = datetime.date.today()
def long_date(dt): return dt.strftime("%A %-d %B %Y")
def ref_words():
    b = best_i
    if not HAS_LM: return f"Swing {b + 1} came closest to your targets."
    if carry[b] == max(carry): return f"Swing {b + 1} came closest to your targets — and it was your longest, a {carry[b]} yard carry at {lm[b]['ballSpeed']:.0f} mph ball speed. Keep it in mind: it is the swing the work-ons are moving you toward."
    return f"Swing {b + 1} came closest to your targets: a {carry[b]} yard carry at {lm[b]['ballSpeed']:.0f} mph ball speed."
# what could not be checked: the gates' own clauses, counted over the session
nj = {}
for x in d.get("notJudged") or []:
    k_ = x.get("clause") or x.get("subject") or ""
    nj[k_] = max(nj.get(k_, 0), x["of"] - x["judged"])
nj_wo = [x for x in (d.get("notJudged") or []) if x["id"] in wo_ids_]
not_checked = ("Some things could not be checked on every swing: " + "; ".join(f"{k_} ({v_} of {N})" for k_, v_ in sorted(nj.items(), key=lambda kv: -kv[1])[:3]) + "."
               + (" Nothing in this report depends on them." if not nj_wo else " Where that touches one of your work-ons, its page says so.")) if nj else ""
habit_words = "habit" if len(workons) == 1 else "habits"
report = {
    "brand": {"studio": "Northfield Golf Studio", "monogram": "NG", "coach": "Alex Morgan", "coachTitle": "Head coach",
              "contact": "alex@northfieldgolf.example  ·  01234 567890", "web": "northfieldgolf.example", "accent": "#1F5A4C",
              "signoff": "Book your next session at northfieldgolf.example or reply to this email."},
    # two dates, never confused: the SESSION's (from its first swing) and when this document was PREPARED (now)
    "meta": {"golfer": golfer, "first": first, "date": long_date(t0), "dateShort": t0.strftime("%-d %b %Y"),
             "prepared": prepared.strftime("%-d %B %Y"), "preparedShort": prepared.strftime("%-d %b %Y"),
             "pdfTitle": f"Session report — {golfer} — session of {t0.strftime('%-d %b %Y')}",
             "fileName": f"{t0.strftime('%Y-%m-%d')}_{golfer.replace(' ', '-')}_session-report.pdf",
             "emailSubject": f"Your session report — {t0.strftime('%-d %B %Y')}",
             "club": club_words, "swings": N, "sessionOfDay": f"Session {this_idx + 1} of {today_n} today" if today_n > 1 else "",
             "hand": (ath.get("handedness") or "right").lower() + "-handed",
             "devices": f"{cap1(cam_words)} cameras" + (f" · {LM_NAME} launch monitor" if HAS_LM else "")},
    "headline": (f"{cap1(word(len(workons)))} {habit_words} showed on {'every' if n_every == n_rep else 'almost every'} swing. "
                 f"We start with {PART.get(phase_of(workons[0]), 'your swing')}: {goal_of(workons[0]['id']).lower()}.") if workons
                else "Nothing repeated often enough to call a habit.",
    "coachNote": args.note,
    "strengths": pick,
    "found": [{"name": cond[c["id"]]["label"], "golfer": cap1(cond[c["id"]].get("golfer") or cond[c["id"]]["label"]), "count": count_words(c)} for c in workons],
    "workOnsBrief": [{"rank": x["rank"], "goal": x["goal"], "drill": (x["drills"][0]["label"] if x["drills"] else "")} for x in deep],
    "ball": {"device": LM_NAME, "tiles": ball_tiles, "note": f"Typical value (the middle of your {N} swings) in large type; the range from lowest to highest beneath."} if HAS_LM else None,
    "grid": {"title": (f"The same {word(n_rep)}, every swing" if n_every == n_rep and n_rep > 1 else "Your habits, swing by swing"),
             "standfirst": f"Each row is one thing we measured; each column is one of your {word(N)} swings, in the order you hit them. A filled dot means it showed on that swing.",
             "sessionNote": grid_session_note,
             "swings": list(range(1, N + 1)), "rows": grid_rows, "carry": carry, "best": best_i + 1,
             "bestText": ref_words(),
             "bestStills": [still(folder_no[best_i], "DTL" if "DTL" in cams else cams[0], p) for p in ["P1", "P4", "P7", "P10"]],
             "bestLabels": ["Address", "Top", "Impact", "Finish"]},
    "today": {"sessions": [t["label"] + f" · {t['swings']} swings" for t in today], "rows": today_rows, "note": today_note} if today_n > 1 else None,
    "order": {"intro": "Your list is ordered by three rules, applied in turn. Here is each rule and what it did today.",
              "rules": rules, "items": order_items, "links": links, "notListed": not_listed,
              "timeline": timeline, "timelineNote": timeline_note},
    "workOns": deep,
    "checks": checks,
    "practice": {"title": "How to practise a change",
                 "steps": [
                     {"head": "Rehearse without a ball", "body": "Slow swings, ten at a time, feeling the change. This is where the new move is learned."},
                     {"head": "Small swings with a ball", "body": "Half swings at half speed. Keep the feel; ignore where the ball goes."},
                     {"head": "Mix it in", "body": "Full swings: one with the feel, then two normal ones. Film every fifth swing."},
                     {"head": "One thing at a time", "body": "Stay with work-on 1 until your next session. The rest of the list waits."}],
                 "note": "Studio text: the coach edits this once, in Settings, and every report carries it."},
    "numbers": {"cols": [c[0] for c in numbers_cols], "units": [""] + [c[2] for c in numbers_cols[1:]], "rows": num_rows, "median": num_med,
                "note": f"Path, face and offline: positive is right of target, negative left ({(ath.get('handedness') or 'right').lower()}-handed golfer). Habits: how many of your {word(nw)} work-ons showed on that swing."} if HAS_LM else None,
    "about": {
        "dates": f"This report covers your session on {long_date(t0)} and was prepared on {prepared.strftime('%-d %B %Y')}. If a session is re-analysed, the report is prepared again and carries the new date.",
        "measured": f"{N} swings with a {club_words}, filmed {cam_words} at high frame rate" + (f", with every ball measured by a {LM_NAME}" if HAS_LM else "") + ". Your body positions are read from the video by pose tracking" + ("; the club and ball numbers come from the launch monitor." if HAS_LM else "."),
        "ranges": "Each habit is judged against a target range. Most ranges are coaching judgement, set from what good ball-strikers do; a few come from published measurements. They are not fitted to you. A swing outside the range is not a mistake in itself — it is where the evidence points.",
        "notChecked": not_checked,
        "words": [("Habit", "something that showed on most swings — at least 3, and often enough that it is not chance"),
                  ("Target range", "where a good ball-striker's swing usually is at that point"),
                  ("Work-on", "a habit you can practise your way out of, in the order to work on it"),
                  ("P1 … P10", "the ten positions of the swing, from address (P1) through the top (P4) and impact (P7) to the finish (P10)")],
    },
}
json.dump(report, open(args.out, "w"), indent=1, ensure_ascii=False)
print("wrote report.json:", [w["name"] for w in deep], "best swing", best_i + 1, "strengths", [s["id"] for s in pick])
