"""Prototype of the home screen's plain-language summary for one golfer.

Two layers, kept apart on purpose:
  1. WHAT'S OFF — the faults present on most of the golfer's swings, from each session's ledger
     (diagnostics.json): how often each fired in the sessions that could judge it, and whether
     that is easing. Grouped by body area and said in golfer's words (golfer_phrases.json).
  2. WHAT GOES TOGETHER — the themes theme_pca.py found in the golfer's own swings, told only as
     firmly as their stability allows: "firm" (holds on every check), "probably", "possibly".
     A fault present on every swing at a steady size cannot form a theme, so layer 1 is not
     redundant with layer 2.

    python3 -I tools/themes/theme_pca.py --out <dir>              # writes <dir>/themes.json
    python3 -I tools/themes/theme_summary.py --themes <dir>/themes.json [session dirs]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from theme_data import load_pack  # noqa: E402

HERE = os.path.dirname(__file__)
LIBRARY = "/mnt/swingdata/Mark-Liversedge"

MIN_ASSESSED = 8        # a session must judge a fault on this many swings to count
PRESENT_SHARE = 0.5     # ...and see it on at least this share to call it present
RECENCY = 0.75          # each older session weighs this much less


# ── layer 1 ─────────────────────────────────────────────────────────────────────

def session_rates(session_dirs):
    """{condition: [(session, fired, assessed), ...]} in session order."""
    out = {}
    for d in sorted(session_dirs):
        p = os.path.join(d, "diagnostics.json")
        if not os.path.exists(p):
            continue
        with open(p) as f:
            shots = json.load(f)["ledger"]["shots"]
        name = os.path.basename(os.path.normpath(d))
        acc = {}
        for s in shots:
            for r in s["rows"]:
                f_, a = acc.get(r["conditionId"], (0, 0))
                if r["state"] == "fired":
                    acc[r["conditionId"]] = (f_ + 1, a + 1)
                elif r["state"] == "clean":
                    acc[r["conditionId"]] = (f_, a + 1)
        for cid, (f_, a) in acc.items():
            out.setdefault(cid, []).append((name, f_, a))
    return out


def whats_off(rates, pack):
    conds = {c["id"]: c for c in pack["conditions"]}
    rows = []
    for cid, per in rates.items():
        c = conds.get(cid)
        if not c or c.get("kind") not in ("fault", None):
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
            trend = "easing" if late < early - 0.15 else "growing" if late > early + 0.15 else "steady"
        rows.append({"id": cid, "group": c.get("group", ""), "label": c.get("label", cid),
                     "share": share, "sessionsSeen": sessions_seen, "sessionsJudged": len(judged),
                     "trend": trend})
    rows.sort(key=lambda r: -r["share"])
    return rows


def how_often(share):
    if share >= 0.9:
        return "on almost every swing"
    if share >= 0.7:
        return "on most swings"
    return "on more than half your swings"


# ── layer 2 ─────────────────────────────────────────────────────────────────────

def tier(st):
    """How firmly a theme may be told, from theme_pca's stability block."""
    boot_med, boot_p05 = st.get("boot_median", 0), st.get("boot_p05", 0)
    loso_min, loso_med = st.get("loso_min", 0), st.get("loso_median", 0)
    if boot_p05 >= 0.84 and loso_min >= 0.85:
        return "firm"
    if boot_med >= 0.75 and loso_med >= 0.9:
        return "probably"
    if boot_med >= 0.7 and loso_med >= 0.85:
        return "possibly"
    return None


START_WORDS = {"P1": "it starts at address", "P2": "it starts early in the backswing",
               "P3": "it starts in the backswing", "P4": "it starts in the backswing",
               "P5": "it starts on the way down", "P6": "it starts on the way down",
               "P7": "it starts around impact", "P8": "it starts after impact"}


def theme_sentence(theme, phrases, max_parts=2):
    parts = []
    for m in sorted(theme["members"], key=lambda m: -abs(m["loading"])):
        ph = phrases["measures"].get(f'{m["measure"]}:{m["rawDirection"]}')
        if ph and ph not in parts:
            parts.append(ph)
        if len(parts) == max_parts:
            break
    if not parts:
        return None
    if len(parts) == 1:
        return parts[0][0].upper() + parts[0][1:]
    # CO-MOVEMENT, NOT CAUSE: "on swings where" says what the data shows and no more.
    head, rest = parts[0], parts[1:]
    return f"On swings where {head}, " + " and ".join(rest)


def trend_words(t):
    r = (t.get("sessionTrend") or {}).get("reading", "")
    if r.startswith("better"):
        return "may be easing"
    if r.startswith("worse"):
        return "may be growing"
    return "no clear change yet"


# ── the page ────────────────────────────────────────────────────────────────────

def summary(session_dirs, themes_path, phrases_path=os.path.join(HERE, "golfer_phrases.json")):
    pack = load_pack()
    phrases = json.load(open(phrases_path))
    off = whats_off(session_rates(session_dirs), pack)
    themes = json.load(open(themes_path))["themes"] if themes_path else []

    lines = []
    n_sessions = len([d for d in session_dirs if os.path.exists(os.path.join(d, "diagnostics.json"))])
    lines.append(f"YOUR SWING — from {n_sessions} sessions\n")

    lines.append("What we see most")
    told = []
    for r in off:
        ph = phrases["conditions"].get(r["id"])
        if not ph or ph in told:
            continue
        told.append(ph)
        extra = f"; {r['trend']}" if r["trend"] in ("easing", "growing") else ""
        lines.append(f"  • {ph[0].upper() + ph[1:]} — {how_often(r['share'])}"
                     f" ({r['sessionsSeen']} of {r['sessionsJudged']} sessions{extra})")
        if len(told) == 5:
            break
    if not told:
        lines.append("  • Nothing shows up on most of your swings.")

    lines.append("\nWhat goes together in your swing")
    shown = 0
    order = {"firm": 0, "probably": 1, "possibly": 2}
    ranked = [(tier(t.get("stability") or {}), t) for t in themes]
    ranked = sorted([(k, t) for k, t in ranked if k], key=lambda kt: (order[kt[0]], -kt[1]["varianceShare"]))
    for k, t in ranked:
        s = theme_sentence(t, phrases)
        if not s:
            continue
        lead = {"firm": "", "probably": "Probably: ", "possibly": "Possibly: "}[k]
        start = START_WORDS.get((t.get("startsAt") or {}).get("phase", ""), "")
        lines.append(f"  • {lead}{s}." + (f" {start[0].upper() + start[1:]}." if start else "")
                     + f" ({trend_words(t)})")
        shown += 1
    if not shown:
        lines.append("  • Nothing yet — your faults don't rise and fall together clearly enough to say.")

    # The layer for the golfer and coach: which group each fault belongs to, by body area.
    lines.append("\nBy body area")
    by = {}
    for r in off:
        by.setdefault(phrases["groups"].get(r["group"], r["group"]), []).append(r["label"])
    for g, labels in by.items():
        lines.append(f"  {g}: " + ", ".join(labels))
    return "\n".join(lines), {"whatsOff": off, "themes": themes}


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
