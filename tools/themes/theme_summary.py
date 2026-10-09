"""Prototype of the home screen's plain-language summary for one golfer.

Two layers, kept apart on purpose:
  1. WHAT WE SEE MOST — the faults present on most of the golfer's swings, from each session's
     ledger (diagnostics.json): how often each fired in the sessions that could judge it, and
     whether that is easing. Said in the golfer's words: the condition's `golfer` phrase in the
     diagnostics pack (core.json).
  2. WHAT GOES TOGETHER — the themes theme_pca.py found in the golfer's own swings, told only as
     firmly as their stability allows: "firm" (holds on every check), "probably", "possibly".
     Each is said from its leading members' `golferHigh` / `golferLow` phrases in the pack.
     A fault present on every swing at a steady size cannot form a theme, so layer 1 is not
     redundant with layer 2.

Beside them, WHAT YOU DO WELL (rule v1) — the faults this golfer is reliably clear of, judged by
value against each reading's own corridor (never by the fired flag), said in the condition's
`golferWell` phrase. Kept away from anything the other two layers talk about: a condition whose
quantity (metricKey family root) is a needs-work fault's (the reduction) or a member's of a theme
the page shows (the view) is not praised.

YOUR FOCUS (rule v1) — one thing to work on, chosen from the needs-work faults: faults sharing a
drill (the first of the condition's `drills` the drill registry holds) are one group, a fault with
none is a group of its own; the group that starts EARLIEST in the swing comes first, then one with
a drill, then the one covering most faults, then the most frequent. The focus is said as the
drill, what to aim for (`golferWell`), what happens now (`golfer`), why it matters (`golferWhy`)
and the drill to practise; every other needs-work fault is "next on your list", in group order.

The page itself (subtitle, the focus, the cards, the "goes together" pairs and the note) is
summary_view(), the reference the app's swingSummaryView() (src/Analysis/swing_themes.h) is tested
against through make_golden.py; swingSummaryLines() is the older flat form of the same words.
Change a word here and the golden fixture with it.
Co-movement, never cause: no line may say because / cause / due to / leads to.

    python3 -I tools/themes/theme_pca.py --out <dir>              # writes <dir>/themes.json
    python3 -I tools/themes/theme_summary.py --themes <dir>/themes.json [session dirs]
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from theme_data import REPO, load_pack, measure_meta  # noqa: E402
from theme_pca import theme_tier, when_of  # noqa: E402

LIBRARY = "/mnt/swingdata/Mark-Liversedge"
DRILLS = os.path.join(REPO, "src", "Resources", "diagnostics", "drills.json")

MIN_ASSESSED = 8        # a session must judge a fault on this many swings to count
PRESENT_SHARE = 0.5     # ...and see it on at least this share to call it present
RECENCY = 0.75          # each older session weighs this much less
TREND_DELTA = 0.15      # late vs early rate change that reads as easing / growing
MAX_SEEN_MOST = 5
MAX_TOGETHER = 4
MAX_PARTS = 2
MIN_SWINGS = 60         # layer 2 needs this many swings (after the sparse drop) ...
MIN_SESSIONS = 3        # ... over this many sessions

# What you do well (rule v1). Sessions are judged with MIN_ASSESSED and weighted with RECENCY, as
# layer 1 is.
PROMINENCE = {"rare": 0, "uncommon": 1, "occasional": 2, "common": 3, "ubiquitous": 4}
DETECTION = {"any": 0, "all": 1, "first": 2}
DETECTION_ALL = 1
WELL_MIN_PROMINENCE = 2     # Occasional and up: praising the absence of a rare fault says nothing
WELL_MIN_SESSIONS = 2       # judged sessions ...
WELL_MIN_SWINGS = 30        # ... and judged swings over them
WELL_SHARE = 0.85           # recency-weighted in-band share
WELL_SESSION = 0.8          # the latest judged session's in-band share; also each session's pip
MAX_DO_WELL = 3
MAX_NEEDS_WORK = MAX_SEEN_MOST
MAX_NEXT = 4                # "next on your list", after the focus
UNPLACED = 11               # a focus group with no known swing position ranks after P10
EVERY_SWING = 0.995         # caption boundaries
ALMOST_EVERY = 0.9
MOST_SWINGS = 0.7
IN_BAND_SHAPES = ("floor", "ceiling", "twoSided")   # the ledger's spellings; a pack Target is twoSided

NOTHING_SEEN = "Nothing shows up on most of your swings."
NOTHING_TOGETHER = "Nothing yet — your faults don't rise and fall together clearly enough to say."


def not_yet(swings, sessions):
    return ("Not yet — it takes about 60 swings over 3 sessions to see what goes together "
            f"(so far: {swings} swings over {sessions} sessions).")


def upper_first(s):
    return s[:1].upper() + s[1:]


# ── phrases (pack content) ───────────────────────────────────────────────────────

def condition_phrases(pack):
    """condition id -> the golfer's phrase (`golfer`), empty when the pack has none."""
    return {c["id"]: c.get("golfer", "") or "" for c in pack["conditions"]}


def condition_well_phrases(pack):
    """condition id -> the golfer's phrase for the fault's ABSENCE (`golferWell`), empty when none."""
    return {c["id"]: c.get("golferWell", "") or "" for c in pack["conditions"]}


def condition_why_phrases(pack):
    """condition id -> what the fault costs the golfer's shots (`golferWhy`), empty when none."""
    return {c["id"]: c.get("golferWhy", "") or "" for c in pack["conditions"]}


def load_drills(path=DRILLS):
    """drill id -> {"label", "instruction"}, from the shipped drill registry (drills.json)."""
    with open(path) as f:
        doc = json.load(f)
    return {d["id"]: {"label": d.get("label", "") or "", "instruction": d.get("instruction", "") or ""}
            for d in doc.get("drills", [])}


def condition_info(pack, drills=None):
    """condition id -> what "what you do well" and the focus read off the pack: fault (kind),
    detection (0 any, 1 all, 2 first), prominence (Rare 0 .. Ubiquitous 4), the condition's
    FAMILIES — the metricKey (or the measure id when it has none) of the FIRST measure of each
    detectedBy signal, in detectedBy order, each once — its DRILL (the first of its `drills` the
    registry holds, else "") and WHEN, the earliest swing position (1..10) any of those first
    measures is read at (0s ignored; 0 when none is placed)."""
    signals = {s["id"]: s for s in pack["signals"]}
    meas = {m["id"]: m for m in pack["measures"]}
    meta = measure_meta(pack)
    drills = drills or {}
    out = {}
    for c in pack["conditions"]:
        fams, whens = [], []
        for sid in c.get("detectedBy", []):
            ms = (signals.get(sid) or {}).get("measures", [])
            if not ms:
                continue
            fam = measure_family(meas, ms[0])
            if fam not in fams:
                fams.append(fam)
            w = when_of(meta[ms[0]]) if ms[0] in meta else 0
            if w > 0:
                whens.append(w)
        out[c["id"]] = {"fault": c.get("kind") == "fault",
                        "detection": DETECTION[c.get("detection", "any") or "any"],
                        "prominence": PROMINENCE[c.get("prominence", "occasional") or "occasional"],
                        "families": fams,
                        "drill": next((d for d in (c.get("drills") or []) if d in drills), ""),
                        "when": min(whens, default=0)}
    return out


SIGNED_SUFFIX = "Signed"


def family_root(key):
    """A metricKey's FAMILY ROOT: the key with a trailing "Signed" removed. The metric catalogue
    (src/Metrics/metric_catalogue_manifest.cpp) says pelvisRotationSigned is "the same physical
    quantity as Pelvis rotation", carrying only the sign the magnitude throws away — so a fault
    read on one and praise read on the other ("your hips keep turning through impact" beside
    "your hips haven't turned enough by impact") are about one thing, and must share a family."""
    return key[:-len(SIGNED_SUFFIX)] if key.endswith(SIGNED_SUFFIX) else key


def measure_family(meas, mid):
    """A measure's family: its metricKey's root, or its own id when it has none (or is not in the
    pack)."""
    return family_root((meas.get(mid) or {}).get("metricKey", "") or "") or mid


def measure_phrases(pack):
    """measure id -> (golferHigh, golferLow, family), each phrase empty when the pack has none.
    The family is the measure's metricKey root (family_root): one quantity read at several
    moments, signed or not. Empty when the measure has no metricKey."""
    return {m["id"]: (m.get("golferHigh", "") or "", m.get("golferLow", "") or "",
                      family_root(m.get("metricKey", "") or ""))
            for m in pack["measures"]}


# ── layer 1 ─────────────────────────────────────────────────────────────────────

def session_rates_from_ledgers(ledgers):
    """[(session name, ledger dict)] in session order -> {condition: [(session, fired, assessed)]}.
    A session appears for a condition only if it assessed it at least once."""
    out = {}
    for name, led in ledgers:
        acc = {}
        for s in led["shots"]:
            for r in s["rows"]:
                f_, a = acc.get(r["conditionId"], (0, 0))
                if r["state"] == "fired":
                    acc[r["conditionId"]] = (f_ + 1, a + 1)
                elif r["state"] == "clean":
                    acc[r["conditionId"]] = (f_, a + 1)
        for cid, (f_, a) in acc.items():
            out.setdefault(cid, []).append((name, f_, a))
    return out


def read_ledgers(session_dirs):
    out = []
    for d in sorted(session_dirs):
        p = os.path.join(d, "diagnostics.json")
        if not os.path.exists(p):
            continue
        with open(p) as f:
            out.append((os.path.basename(os.path.normpath(d)), json.load(f)["ledger"]))
    return out


def session_rates(session_dirs):
    return session_rates_from_ledgers(read_ledgers(session_dirs))


def whats_off(rates, pack):
    """Faults (pack kind == 'fault' only) present in the latest session that judged them, by
    recency-weighted share, highest first (stable)."""
    conds = {c["id"]: c for c in pack["conditions"]}
    rows = []
    for cid, per in rates.items():
        c = conds.get(cid)
        if not c or c.get("kind") != "fault":
            continue
        judged = [(s, f, a) for s, f, a in per if a >= MIN_ASSESSED]
        if not judged:
            continue
        latest = judged[-1]
        if latest[1] / latest[2] < PRESENT_SHARE:
            continue
        w = [RECENCY ** (len(judged) - 1 - i) for i in range(len(judged))]
        share = sum(wi * f / a for wi, (_, f, a) in zip(w, judged)) / sum(w)
        sessions_seen = sum(1 for _, f, a in judged if f / a >= PRESENT_SHARE)
        trend = ""
        if len(judged) >= 3:
            early = sum(f / a for _, f, a in judged[:-2]) / len(judged[:-2])
            late = sum(f / a for _, f, a in judged[-2:]) / 2
            trend = ("easing" if late < early - TREND_DELTA
                     else "growing" if late > early + TREND_DELTA else "steady")
        rows.append({"id": cid, "group": c.get("group", ""), "label": c.get("label", cid),
                     "share": share, "sessionsSeen": sessions_seen, "sessionsJudged": len(judged),
                     "trend": trend, "pips": [f / a >= PRESENT_SHARE for _, f, a in judged]})
    rows.sort(key=lambda r: -r["share"])
    return rows


def how_often(share):
    if share >= 0.9:
        return "on almost every swing"
    if share >= 0.7:
        return "on most swings"
    return "on more than half your swings"


def seen_most_lines(off, cond_phrase):
    """At most five lines, one per distinct phrase; conditions with no phrase are skipped."""
    lines, told = [], []
    for r in off:
        ph = cond_phrase.get(r["id"], "")
        if not ph or ph in told:
            continue
        told.append(ph)
        extra = f"; {r['trend']}" if r["trend"] in ("easing", "growing") else ""
        lines.append(f"{upper_first(ph)} — {how_often(r['share'])}"
                     f" ({r['sessionsSeen']} of {r['sessionsJudged']} sessions{extra})")
        if len(lines) == MAX_SEEN_MOST:
            break
    return lines or [NOTHING_SEEN]


# ── what you do well (rule v1) ──────────────────────────────────────────────────

def row_in_band(row):
    """True when every reading behind the row (its `readings`, else its own driving fields) is
    inside its own corridor BY VALUE — floor v >= lo, ceiling v <= hi, twoSided lo <= v <= hi —
    False when one is outside, None when the row is not counted at all: not assessable, no
    readings, or a reading whose corridor shape is anything else ("none", "unknown")."""
    if row.get("state") == "notAssessable":
        return None
    rs = row.get("readings") or ([row] if row.get("drivingMeasureId") else [])
    if not rs:
        return None
    inside = True
    for r in rs:
        shape, v = r.get("corridorShape", "unknown"), r.get("value", 0.0)
        lo, hi = r.get("corridorLo", 0.0), r.get("corridorHi", 0.0)
        if shape not in IN_BAND_SHAPES or v is None or not math.isfinite(v):
            return None
        if shape == "floor":
            inside = inside and v >= lo
        elif shape == "ceiling":
            inside = inside and v <= hi
        else:
            inside = inside and lo <= v <= hi
    return inside


def well_counts_from_ledgers(ledgers):
    """[(session name, ledger dict)] in session order -> {condition: [(session, good, counted)]},
    a session appearing for a condition only when it counted one of its rows."""
    out = {}
    for name, led in ledgers:
        acc = {}
        for s in led["shots"]:
            for r in s["rows"]:
                ib = row_in_band(r)
                if ib is None:
                    continue
                g, a = acc.get(r["conditionId"], (0, 0))
                acc[r["conditionId"]] = (g + (1 if ib else 0), a + 1)
        for cid, (g, a) in acc.items():
            out.setdefault(cid, []).append((name, g, a))
    return out


def needs_work_families(info, off):
    """Every needs-work condition's families (ALL of them, not just the five shown)."""
    fams = set()
    for r in off:
        fams.update((info.get(r["id"]) or {}).get("families", []))
    return fams


def do_well(counts, info, off):
    """Rule v1. Candidates: fault, detection any / first (all is a conjunction: no one reading is
    the condition), prominence Occasional and up. Judged sessions counted >= MIN_ASSESSED rows;
    at least WELL_MIN_SESSIONS of them and WELL_MIN_SWINGS rows over them. Kept when the
    recency-weighted in-band share is >= WELL_SHARE and the latest judged session's is >=
    WELL_SESSION, and its families touch no needs-work condition's. Ranked prominence, share,
    judged swings (each descending), then id. Uncapped, and still carrying each row's families:
    the view drops the rows that touch a theme it SHOWS, then takes the first it can say."""
    banned = needs_work_families(info, off)
    rows = []
    for cid, per in counts.items():
        ci = info.get(cid)
        if (not ci or not ci["fault"] or ci["detection"] == DETECTION_ALL
                or ci["prominence"] < WELL_MIN_PROMINENCE):
            continue
        judged = [(s, g, a) for s, g, a in per if a >= MIN_ASSESSED]
        swings = sum(a for _, _, a in judged)
        if len(judged) < WELL_MIN_SESSIONS or swings < WELL_MIN_SWINGS:
            continue
        w = [RECENCY ** (len(judged) - 1 - i) for i in range(len(judged))]
        share = sum(wi * g / a for wi, (_, g, a) in zip(w, judged)) / sum(w)
        latest = judged[-1]
        if share < WELL_SHARE or latest[1] / latest[2] < WELL_SESSION:
            continue
        if banned.intersection(ci["families"]):
            continue
        rows.append({"id": cid, "share": share, "prominence": ci["prominence"],
                     "sessionsJudged": len(judged), "swingsJudged": swings,
                     "pips": [g / a >= WELL_SESSION for _, g, a in judged],
                     "families": list(ci["families"])})
    rows.sort(key=lambda r: (-r["prominence"], -r["share"], -r["swingsJudged"], r["id"]))
    return rows


# ── your focus (rule v1) ────────────────────────────────────────────────────────

def focus_order(off, info):
    """Every needs-work fault (all of `off`, uncapped) in groups: a drill's faults together (key =
    the drill id), a fault with no drill alone (key "solo:" + id). A group's `when` is the earliest
    swing position of its faults (0 = none placed, ranked as UNPLACED, after P10). Groups ranked:
    earliest first, a drill before none, more faults, the highest share, then key; within a group
    by share (descending), then id."""
    groups = {}
    for r in off:
        ci = info.get(r["id"]) or {}
        drill = ci.get("drill", "")
        key = drill if drill else "solo:" + r["id"]
        g = groups.setdefault(key, {"key": key, "drill": drill, "faults": []})
        g["faults"].append((r["id"], r["share"], ci.get("when", 0)))
    out = []
    for g in groups.values():
        faults = sorted(g["faults"], key=lambda f: (-f[1], f[0]))
        out.append({"key": g["key"], "drill": g["drill"],
                    "when": min((w for _, _, w in faults if w > 0), default=0),
                    "conditionIds": [f[0] for f in faults], "maxShare": max(f[1] for f in faults)})
    out.sort(key=lambda g: (g["when"] or UNPLACED, 0 if g["drill"] else 1, -len(g["conditionIds"]),
                            -g["maxShare"], g["key"]))
    return [{k: g[k] for k in ("key", "drill", "when", "conditionIds")} for g in out]


# ── layer 2 ─────────────────────────────────────────────────────────────────────

START_WORDS = {1: "It starts at address", 2: "It starts early in the backswing",
               3: "It starts in the backswing", 4: "It starts in the backswing",
               5: "It starts on the way down", 6: "It starts on the way down",
               7: "It starts around impact", 8: "It starts after impact",
               9: "It starts in the follow-through", 10: "It shows in the finish"}
TREND_WORDS = {-1: "may be easing", 1: "may be growing", 0: "no clear change yet"}
LEAD = {"firm": "", "probably": "Probably: ", "possibly": "Possibly: "}
TIER_ORDER = {"firm": 0, "probably": 1, "possibly": 2}


def theme_parts(theme, meas_phrase, max_parts=MAX_PARTS):
    """The phrases a theme is said with: members by -|loading|, each its golferHigh or golferLow
    as the theme pushes its raw value; empty and repeated phrases skipped, and so is a member of a
    family already said. At most max_parts."""
    parts, families = [], set()
    for m in sorted(theme["members"], key=lambda m: -abs(m["loading"])):
        hi, lo, fam = meas_phrase.get(m["measure"], ("", "", ""))
        ph = hi if m["rawHigh"] else lo
        if ph and ph not in parts and not (fam and fam in families):
            parts.append(ph)
            if fam:
                families.add(fam)
        if len(parts) == max_parts:
            break
    return parts


def theme_sentence(theme, meas_phrase, max_parts=MAX_PARTS):
    """Members by -|loading|; each says its golferHigh or golferLow phrase as the theme pushes its
    raw value; empty and repeated phrases skipped, and so is a second member of a family already
    said (the same quantity at another moment — "your hips slide …, your hips move …" says one
    thing twice). CO-MOVEMENT, NOT CAUSE: "on swings where" says what the data shows and no more."""
    parts = theme_parts(theme, meas_phrase, max_parts)
    if not parts:
        return None
    if len(parts) == 1:
        return upper_first(parts[0])
    return f"On swings where {parts[0]}, {parts[1]}"


def together_lines(themes, meas_phrase, enough=True, swings=0, sessions=0):
    """themes: [{tier, varianceShare, members: [{measure, loading, rawHigh}], startsAt, trend}]."""
    if not enough:
        return [not_yet(swings, sessions)]
    ranked = sorted([t for t in themes if t.get("tier")],
                    key=lambda t: (TIER_ORDER[t["tier"]], -t["varianceShare"]))
    lines = []
    for t in ranked:
        s = theme_sentence(t, meas_phrase)
        if not s:
            continue
        start = START_WORDS.get(t.get("startsAt") or 0, "")
        lines.append(f"{LEAD[t['tier']]}{s}." + (f" {start}." if start else "")
                     + f" ({TREND_WORDS[t.get('trend', 0)]})")
        if len(lines) == MAX_TOGETHER:
            break
    return lines or [NOTHING_TOGETHER]


def themes_from_pca_json(doc):
    """theme_pca.py's themes.json -> the theme form together_lines() reads."""
    out = []
    for t in doc.get("themes", []):
        st = t.get("stability") or {}
        tier = st.get("tier", theme_tier(st.get("boot_median", 0), st.get("boot_p05", 0),
                                         st.get("loso_min", 0), st.get("loso_median", 0)))
        first = (t.get("startsAt") or {}).get("phase", "")
        out.append({"tier": tier, "varianceShare": t["varianceShare"],
                    "members": [{"measure": m["measure"], "loading": m["loading"],
                                 "rawHigh": m["rawDirection"] == "high"} for m in t["members"]],
                    "startsAt": int(first[1:]) if first else 0,
                    "trend": (t.get("sessionTrend") or {}).get("trend", 0)})
    return out


# ── the view (what the home screen draws) ──────────────────────────────────────

START_STOP_WORDS = ["starts at address", "starts in the backswing", "starts at the top",
                    "starts coming down", "starts around impact", "starts in the finish"]


def start_stop(starts_at):
    """Swing position -> the timeline's six stops (address, back, top, down, impact, finish);
    -1 when the theme has no known position."""
    p = starts_at or 0
    if p <= 0:
        return -1
    return 0 if p <= 1 else 1 if p <= 3 else 2 if p == 4 else 3 if p <= 6 else 4 if p == 7 else 5


def well_caption(share):
    if share >= EVERY_SWING:
        return "In the ideal range on every swing"
    if share >= ALMOST_EVERY:
        return "In the ideal range on almost every swing"
    return "In the ideal range on most swings"


def frequency(share):
    if share >= EVERY_SWING:
        return "every swing"
    if share >= ALMOST_EVERY:
        return "almost every swing"
    if share >= MOST_SWINGS:
        return "most swings"
    return "more than half your swings"


def counted(n, one, many):
    return f"{n} {one if n == 1 else many}"


NUMBER_WORDS = {2: "two", 3: "three", 4: "four", 5: "five", 6: "six", 7: "seven", 8: "eight",
                9: "nine"}
REASON_EARLIEST = "Picked first: it comes earliest in your swing"
REASON_MOST = "Picked first: it shows up on the most of your swings"


def needs_work_item(r, phrase):
    return {"text": upper_first(phrase), "share": r["share"], "frequency": frequency(r["share"]),
            "trend": 1 if r["trend"] == "growing" else -1 if r["trend"] == "easing" else 0,
            "pips": list(r["pips"])}


def focus_view(focus, off, cond_phrase, well_phrase, why_phrase, drills):
    """(focus, next): the focus is the first group of `focus` (focus_order) whose faults say at
    least one `golfer` phrase — its drill's label as the title (a solo fault's capitalised
    `golferWell`), what to aim for (each fault's `golferWell`), what happens now (each fault as a
    needs-work item), why it matters (the `golferWhy` sentences), the drill and the reason it was
    chosen. next = every other needs-work fault in group order, phrases told once (the focus's
    included), at most MAX_NEXT."""
    rows = {r["id"]: r for r in off}
    item = {"present": False, "title": "", "aimFor": [], "rightNow": [], "why": "",
            "practiseLabel": "", "practise": "", "reason": "", "conditionIds": []}
    chosen, told = None, []
    for g in focus:
        said, right = [], []
        for cid in g["conditionIds"]:
            ph = cond_phrase.get(cid, "")
            if not ph or ph in said:
                continue
            said.append(ph)
            right.append(needs_work_item(rows[cid], ph))
        if right:
            chosen, told = g, said
            item["rightNow"] = right
            break
    if chosen:
        ids = chosen["conditionIds"]
        drill = drills.get(chosen["drill"], {"label": "", "instruction": ""}) if chosen["drill"] else None
        aim = []
        for cid in ids:
            ph = well_phrase.get(cid, "")
            if ph and ph not in aim:
                aim.append(ph)
        whys = []
        for cid in ids:
            w = why_phrase.get(cid, "")
            if w and w not in whys:
                whys.append(w)
        earliest = min(g["when"] or UNPLACED for g in focus)
        reason = REASON_EARLIEST if (chosen["when"] or UNPLACED) == earliest else REASON_MOST
        n = len(item["rightNow"])
        if n >= 2:
            reason += f" and covers {NUMBER_WORDS.get(n, str(n))} of the things on your list"
        item.update({"present": True,
                     "title": drill["label"] if drill is not None
                     else upper_first(well_phrase.get(ids[0], "")),
                     "aimFor": [upper_first(a) for a in aim], "why": " ".join(whys),
                     "practiseLabel": drill["label"] if drill is not None else "",
                     "practise": drill["instruction"] if drill is not None else "",
                     "reason": reason + ".", "conditionIds": list(ids)})
    nxt = []
    for g in focus:
        if chosen and g["key"] == chosen["key"]:
            continue
        for cid in g["conditionIds"]:
            ph = cond_phrase.get(cid, "")
            if len(nxt) == MAX_NEXT or not ph or ph in told:
                continue
            told.append(ph)
            nxt.append(needs_work_item(rows[cid], ph))
    return item, nxt


def summary_view(all_swings, all_sessions, well, off, themes, cond_phrase, well_phrase,
                 meas_phrase, enough, swings, sessions, focus=(), why_phrase=None, drills=None):
    """The page: subtitle; the focus and "next on your list" (focus_view, from focus_order's
    groups); "what you do well" (less any row whose families touch a member of a theme the page
    shows; at most three) and "what needs work" (one item per distinct phrase, at most five, with
    one pip per judged session — kept for compatibility, the home no longer shows it); "what goes
    together" (tier, then variance; two phrased parts as "When A" / "B", one as A alone; its
    timeline stop); the note shown when nothing goes together. all_swings / all_sessions count
    every shot in the ledgers and every session holding one; swings / sessions are layer 2's
    (after the sparse drop)."""
    view = {"subtitle": f"From {counted(all_swings, 'swing', 'swings')} over "
                        f"{counted(all_sessions, 'session', 'sessions')}",
            "doWell": [], "needsWork": [], "together": [], "note": ""}
    view["focus"], view["next"] = focus_view(list(focus), off, cond_phrase, well_phrase,
                                             why_phrase or {}, drills or {})
    shown = []
    if enough:
        ranked = sorted([t for t in themes if t.get("tier")],
                        key=lambda t: (TIER_ORDER[t["tier"]], -t["varianceShare"]))
        for t in ranked:
            parts = theme_parts(t, meas_phrase)
            if not parts:
                continue
            shown.append(t)
            stop = start_stop(t.get("startsAt"))
            view["together"].append({
                "tier": t["tier"],
                "first": "When " + parts[0] if len(parts) > 1 else upper_first(parts[0]),
                "second": parts[1] if len(parts) > 1 else "",
                "startStop": stop, "startWords": START_STOP_WORDS[stop] if stop >= 0 else "",
                "trend": int(t.get("trend", 0))})
            if len(view["together"]) == MAX_TOGETHER:
                break
    # A quantity a SHOWN theme moves is not praised: every member of it, not only the two named.
    # A measure's family here is its metricKey root, or its own id when it has none.
    banned = {meas_phrase.get(m["measure"], ("", "", ""))[2] or m["measure"]
              for t in shown for m in t["members"]}
    told = []
    for r in well:
        if banned.intersection(r["families"]):
            continue
        ph = well_phrase.get(r["id"], "")
        if not ph or ph in told:
            continue
        told.append(ph)
        view["doWell"].append({"text": upper_first(ph), "caption": well_caption(r["share"]),
                               "pips": list(r["pips"])})
        if len(view["doWell"]) == MAX_DO_WELL:
            break
    told = []
    for r in off:
        ph = cond_phrase.get(r["id"], "")
        if not ph or ph in told:
            continue
        told.append(ph)
        view["needsWork"].append({"text": upper_first(ph), "share": r["share"],
                                  "frequency": frequency(r["share"]),
                                  "trend": 1 if r["trend"] == "growing" else -1 if r["trend"] == "easing" else 0,
                                  "pips": list(r["pips"])})
        if len(view["needsWork"]) == MAX_NEEDS_WORK:
            break
    if not view["together"]:
        view["note"] = NOTHING_TOGETHER if enough else not_yet(swings, sessions)
    return view


# ── the page ────────────────────────────────────────────────────────────────────

def summary(session_dirs, themes_path):
    pack = load_pack()
    ledgers = read_ledgers(session_dirs)
    off = whats_off(session_rates_from_ledgers(ledgers), pack)
    doc = json.load(open(themes_path)) if themes_path else {}
    themes = themes_from_pca_json(doc)
    lib = doc.get("library") or {}
    swings, sessions = lib.get("swings", 0), len(lib.get("sessions", []))
    enough = bool(doc) and swings >= MIN_SWINGS and sessions >= MIN_SESSIONS

    drills = load_drills()
    info = condition_info(pack, drills)
    well = do_well(well_counts_from_ledgers(ledgers), info, off)
    well_phrase = condition_well_phrases(pack)
    focus, nxt = focus_view(focus_order(off, info), off, condition_phrases(pack), well_phrase,
                            condition_why_phrases(pack), drills)

    lines = [f"YOUR SWING — from {len(ledgers)} sessions\n"]
    if focus["present"]:
        lines += [f"Your focus: {focus['title']}", f"  ({focus['reason']})"]
        lines += [f"  aim for   {x}" for x in focus["aimFor"]]
        lines += [f"  right now {x['text']} — {x['frequency']}" for x in focus["rightNow"]]
        if focus["why"]:
            lines.append(f"  why       {focus['why']}")
        if focus["practise"]:
            lines.append(f"  practise  {focus['practiseLabel']}: {focus['practise']}")
    lines.append("\nNext on your list")
    lines += [f"  • {x['text']} — {x['frequency']}" for x in nxt]
    lines.append("\nWhat you do well")
    lines += [f"  • {upper_first(well_phrase.get(r['id'], '') or r['id'])} — "
              f"{well_caption(r['share'])} ({r['share']:.3f})" for r in well]
    lines.append("\nWhat we see most")
    lines += [f"  • {s}" for s in seen_most_lines(off, condition_phrases(pack))]
    lines.append("\nWhat goes together in your swing")
    lines += [f"  • {s}" for s in together_lines(themes, measure_phrases(pack), enough, swings,
                                                 sessions)]
    return "\n".join(lines), {"whatsOff": off, "doWell": well, "themes": themes}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sessions", nargs="*")
    ap.add_argument("--themes", help="themes.json from theme_pca.py")
    ap.add_argument("--json", help="also write the structured summary here")
    a = ap.parse_args()
    dirs = a.sessions or sorted(glob.glob(os.path.join(LIBRARY, "*/")))
    text, data = summary(dirs, a.themes)
    print(text)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(data, f, indent=1, default=str)


if __name__ == "__main__":
    main()
