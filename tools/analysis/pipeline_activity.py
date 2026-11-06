#!/usr/bin/env python3
"""pipeline_activity.py -- the analysis pipeline as a UML activity diagram, in a PDF.

The stage graph (analysis_dag.py) says what MAY run in what order. This says what DOES run, and
why: every decision that picks a path through a swing's analysis is a diamond with its guards, so
"reload the pose or infer it" is a branch on the page and not something hidden inside a box. Each
diamond says whether the swing document keeps its answer.

The flow below is written by hand, because the branches live inside stages and no export sees them.
So the tool refuses to draw a diagram that has drifted from the code. It fails when:

  * a stage in the executor's export is missing from the flow, or the flow names one that isn't there;
  * a real ordering edge is drawn backwards, or across two parallel branches;
  * a decision's source line can't be found in the source (it cites file:line, found by text).

Refresh the export after changing a stage's declaration:
    swinglab_run <swing> --out <dir> --dag docs/developer/analysis_dag.json

Usage:
    pipeline_activity.py                    write docs/developer/analysis_pipeline_activity.pdf
    pipeline_activity.py --check            checks only, write nothing
    pipeline_activity.py --out other.pdf    write elsewhere

python3 + stdlib only. Page sizes follow their content, so a viewer's fit-to-width reads best;
bookmarks and the contents page jump between sections. A join means "all of this is finished before
the next thing is drawn". The executor may overlap more than that; it never overlaps less.
"""

import argparse
import json
import os
import sys
import zlib

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DAG = os.path.join(REPO, "docs", "developer", "analysis_dag.json")
OUT = os.path.join(REPO, "docs", "developer", "analysis_pipeline_activity.pdf")

P = "src/Analysis/analysis_pipeline.cpp"
RA = "src/Analysis/swing_reanalyzer.cpp"
PR = "src/Pose/pose_runner.cpp"
ST = "src/Club/shaft_tracker.cpp"
SR = "src/Analysis/seg_resolve.h"

# ── The decisions ────────────────────────────────────────────────────────────
# kept: none = nowhere, log = app log only, part = partly in the document, doc = in the document.
DECISIONS = {
    "force": dict(q="Forced re-run?", kept="none", src=(RA, "if (!opts.forceRerun)"),
        where="Nowhere. The reuse log line sits inside this branch, so a forced run doesn't even log it.",
        why="swinglab only; the app never sets it. Throws away every recorded track."),
    "over": dict(q="Tuning overrides?", kept="none", src=(RA, "bool poseOverride = false;"),
        where="Nowhere in the document. swinglab prints one line to stderr.",
        why="A pose., ball. or address key re-runs pose. Any key at all re-runs the shaft."),
    "raw": dict(q=".raw on disk?", kept="log", src=(RA, "if (QFile::exists(rawPath)) {"),
        where="Log only: an Mp4Reader line appears when the MP4 was read.",
        why="Raw frames or the compressed MP4. Everything downstream sees different pixels."),
    "pose": dict(q="Reload face-on pose?", kept="log", src=(RA, "const bool poseMatch = !poseOverride"),
        where="Log only, one [Reanalysis] line. The document says ran: true either way.",
        why="Pose version, model file and scope all match, and no pose override."),
    "ball": dict(q="Reload ball?", kept="log", src=(RA, "const bool ballMatch = "),
        where="Log only. ran: true either way.",
        why="Needs a reloaded pose and a matching ball version."),
    "shaft": dict(q="Reload shaft track?", kept="log", src=(RA, "const bool shaftMatch = "),
        where="Log only. ran: true either way.",
        why="Reloaded ball, matching shaft version, a recorded track, and NO tuning override of any kind."),
    "ladder": dict(q="Ladder recorded?", kept="part", src=(RA, "ls.job.ladderPreloaded = segmentationFromAnalysisJson"),
        where="Partly: EventRefine and TimelineFusion show ran: false, with no reason.",
        why="A track without its ladder cancels shaft reuse. With one, the ladder stages stand down."),
    "dtlpose": dict(q="Reload DTL pose?", kept="log", src=(RA, "const bool poseDtlMatch = "),
        where="Log only. ran: true either way.",
        why="Independent of the face-on decision, so a swing can hold a new face-on pose beside an old DTL one."),
    "scan": dict(q="Which pose path?", kept="part", src=(P, "if (segImu.conf > 0.f && !ctx.job.fullWindow) {"),
        where='Partly: scope says "span" for both bounded and two-pass. Only the log separates them.',
        why="The branch #154 was hiding behind."),
    "span": dict(q="Coarse pass found a span?", kept="log", src=(PR, "two-pass: no swing span from the coarse pass"),
        where="Log only, one warning.", why="If not, pass 2 fills the whole window."),
    "crop": dict(q="Crop accepted?", kept="doc", src=(PR, "auto makeCrop = [&]"),
        where="In the document: pose2d.cropRect, absent when full-frame.",
        why="A crop of 90 % of the frame or more, or too few bbox frames, falls back to full-frame."),
    "bound": dict(q="Tracker span-bounded?", kept="none", src=(ST, "if (job.fullWindow)"),
        where="Nowhere, and not logged.", why="--full-window turns it off. Series lengths change; positions don't."),
    "demote": dict(q="Implausible samples?", kept="part", src=(P, "const int windowDemoted = demoteImplausibleImpactWindow"),
        where="Partly: demoted samples carry a flag, not that a reuse pass set it.",
        why="The checks run again on every reuse, and can demote one more sample than the fresh run did."),
    "witness": dict(q="DTL witness source?", kept="log", src=(P, "buildFaceOnWitness(ctx.detail->shaft, nullptr, ctx.job.impactUs)"),
        where='Log only: "witness traced" or "from reused flags".',
        why="Shifted 2 of 410 published DTL frames on one swing."),
    "seg": dict(q="Which ladder wins?", kept="part", src=(SR, "if (ladderPreloaded) return *ladderPreloaded;"),
        where="Partly: phases carry a role and the ladder a confidence. Nothing states which won.",
        why="Reused, then adopted sensors, then vision, then the sensor clamp."),
    "ballp1": dict(q="Ball found at address?", kept="part", src=(ST, "if (!ns || !(ns->flags & ShaftBallAnchored)) continue;"),
        where="Partly: club.addressBall is absent when no ball anchored.",
        why="With no ball, a fresh run keeps a synth built before P1's angle was rewritten; a reuse rebuilds it."),
    "fusion": dict(q="refine.fusion on?", kept="doc", src=(P, "&& !TimelineFusionConfig::fromOverrides(ctx.job.tuningOverrides).enabled"),
        where="In the document: segmentation.fusion.", why="Picks one of the two ladder builders. On by default."),
    "dtlcam": dict(q="DTL camera present?", kept="doc", src=(P, "if (ctx.job.dtlSource == pinpoint::kInvalidSourceId) return false;"),
        where="In the document: the streams list, and stages show ran: false.",
        why="Without it the DTL branch stands down, and Skeleton3D starts and refuses."),
    "imu": dict(q="Motion sensors present?", kept="doc", src=(P, "return !ctx.caps.imus.empty();"),
        where="In the document: the streams list.", why="Decides the pose path and which ladder wins."),
}
KEPT = {"none": "NOT KEPT", "log": "LOG ONLY", "part": "PARTLY", "doc": "IN DOCUMENT"}

GATE = {
    "ShaftPlane": "The stage is on, the track is valid, Takeaway, Top and Impact exist.",
    "BodyRotationTriangulated": "The skeleton used the DTL and a rotation series is missing.",
    "ClubDelivery": "A valid shaft track with samples.",
    "KinematicSequence": "The stage is on, Impact exists, at least one input.",
    "AddressMarks": "Segmenter built in, Address exists, either pose exists.",
    "DtlSynth3D": "The DTL track, the synth and the 3-D shaft exist.",
    "ShaftFusion": "Both club tracks valid, Top and Impact exist.",
    "DtlPosture": "DTL pose and Address exist.",
    "DtlShaftLie": "The DTL track has samples and Address exists.",
    "Impact": "The job names an impact camera stream.",
}

# ── Colours (RGB 0..1). Every state also has its own outline, so print in grey still reads. ──
def rgb(h): return tuple(int(h[i:i + 2], 16) / 255 for i in (1, 3, 5))
INK, MUT, LINE = rgb("#17211f"), rgb("#566461"), rgb("#7f8d89")
RUNF, RUNS = rgb("#d8ece6"), rgb("#0b6b5c")
LOADF, LOADS = rgb("#f8e8c4"), rgb("#94650a")
SILF, SILS = rgb("#e7def5"), rgb("#6241a5")
NOTEF, NOTES = rgb("#f1f4f3"), rgb("#9aa6a2")
BAD, BADF = rgb("#b3261e"), rgb("#fbe4e1")
WARNF, GOODF = rgb("#f8e8c4"), rgb("#d8ece6")
WHITE = (1, 1, 1)
TAGC = {"none": (BADF, BAD), "log": (BADF, BAD), "part": (WARNF, LOADS), "doc": (GOODF, RUNS)}

# ── Text metrics: the standard Helvetica widths, per 1000 ─────────────────────
_W = [278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278]+[556]*10+[278,278,584,584,584,556,1015,
      667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,
      278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,
      556,500,722,500,500,500,334,260,334,584]
def tw(s, size, font="F1"):
    if font == "F3": return len(s) * size * 0.6
    k = 1.06 if font == "F2" else 1.0
    return sum(_W[ord(c) - 32] if 32 <= ord(c) < 127 else 556 for c in s) * size * k / 1000
def wrap(s, size, width, font="F1"):
    out, cur = [], ""
    for w in s.split():
        t = (cur + " " + w).strip()
        if tw(t, size, font) <= width or not cur: cur = t
        else: out.append(cur); cur = w
    if cur: out.append(cur)
    return out

# ── A small PDF: vector shapes, three base fonts, links, bookmarks ───────────
class Page:
    def __init__(s, w, h, title):
        s.w, s.h, s.title, s.ops, s.links = w, h, title, [], []
    def _y(s, y): return s.h - y
    def poly(s, pts, fill=None, stroke=None, lw=1.0, dash=None, close=True):
        o = ["q", f"{lw:.2f} w", "[%s] 0 d" % (" ".join(str(d) for d in dash) if dash else "")]
        if fill: o.append("%.3f %.3f %.3f rg" % fill)
        if stroke: o.append("%.3f %.3f %.3f RG" % stroke)
        o.append(f"{pts[0][0]:.2f} {s._y(pts[0][1]):.2f} m")
        o += [f"{x:.2f} {s._y(y):.2f} l" for x, y in pts[1:]]
        if close: o.append("h")
        o.append({(True, True): "B", (True, False): "f", (False, True): "S"}[(bool(fill), bool(stroke))])
        o.append("Q"); s.ops.append(" ".join(o))
    def rect(s, x, y, w, h, **k):
        s.poly([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], **k)
    def rrect(s, x, y, w, h, r=6, **k):
        pts = []
        import math
        for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
            for i in range(5):
                a = math.radians(a0 + i * 22.5); pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        s.poly(pts, **k)
    def circle(s, cx, cy, r, **k):
        import math
        s.poly([(cx + r * math.cos(i * math.pi / 12), cy + r * math.sin(i * math.pi / 12)) for i in range(24)], **k)
    def line(s, pts, arrow=True, color=LINE, lw=0.9, dash=None):
        s.poly(pts, stroke=color, lw=lw, dash=dash, close=False)
        if arrow:
            (x0, y0), (x1, y1) = pts[-2], pts[-1]
            if abs(y1 - y0) >= abs(x1 - x0):
                d = 1 if y1 > y0 else -1; tri = [(x1, y1), (x1 - 3, y1 - 6 * d), (x1 + 3, y1 - 6 * d)]
            else:
                d = 1 if x1 > x0 else -1; tri = [(x1, y1), (x1 - 6 * d, y1 - 3), (x1 - 6 * d, y1 + 3)]
            s.poly(tri, fill=color)
    def text(s, x, y, t, size=9, font="F1", color=INK, align="l"):
        if align == "c": x -= tw(t, size, font) / 2
        elif align == "r": x -= tw(t, size, font)
        b = t.encode("cp1252", "replace").replace(b"\\", b"\\\\").replace(b"(", b"\\(").replace(b")", b"\\)")
        s.ops.append("BT %.3f %.3f %.3f rg /%s %.2f Tf %.2f %.2f Td (%s) Tj ET" % (*color, font, size, x, s._y(y), b.decode("latin-1")))
    def link(s, x, y, w, h, page):
        s.links.append((x, s.h - y - h, x + w, s.h - y, page))

def write_pdf(path, pages, title):
    objs = {}
    n = 3 + 3                                   # 1 catalog, 2 pages, 3 outlines, 4-6 fonts
    ids = []
    for _ in pages:
        ids.append((n + 1, n + 2)); n += 2      # page, contents
    objs[2] = "<< /Type /Pages /Count %d /Kids [%s] >>" % (len(pages), " ".join(f"{a} 0 R" for a, _ in ids))
    objs[4] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    objs[5] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>"
    objs[6] = "<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>"
    streams = {}
    for i, p in enumerate(pages):
        a, c = ids[i]
        annots = []
        for x0, y0, x1, y1, tgt in p.links:
            n += 1
            objs[n] = "<< /Type /Annot /Subtype /Link /Rect [%.1f %.1f %.1f %.1f] /Border [0 0 0] /Dest [%d 0 R /Fit] >>" % (x0, y0, x1, y1, ids[tgt][0])
            annots.append(f"{n} 0 R")
        objs[a] = ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.0f %.0f] /Contents %d 0 R "
                   "/Resources << /Font << /F1 4 0 R /F2 5 0 R /F3 6 0 R >> >> /Annots [%s] >>") % (p.w, p.h, c, " ".join(annots))
        streams[c] = zlib.compress("\n".join(p.ops).encode("latin-1"))
    first = n + 1
    esc = lambda t: t.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
    for i, p in enumerate(pages):
        n += 1
        prev = f"/Prev {n - 1} 0 R " if i else ""
        nxt = f"/Next {n + 1} 0 R " if i < len(pages) - 1 else ""
        objs[n] = "<< /Title (%s) /Parent 3 0 R %s%s/Dest [%d 0 R /Fit] >>" % (esc(p.title), prev, nxt, ids[i][0])
    objs[3] = "<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count %d >>" % (first, n, len(pages))
    objs[1] = "<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R /PageMode /UseOutlines >>"
    n += 1
    objs[n] = "<< /Title (%s) /Producer (tools/analysis/pipeline_activity.py) >>" % esc(title)
    info = n
    out = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n"); offs = {}
    for k in sorted(set(objs) | set(streams)):
        offs[k] = len(out)
        if k in streams:
            out += b"%d 0 obj\n<< /Length %d /Filter /FlateDecode >>\nstream\n" % (k, len(streams[k])) + streams[k] + b"\nendstream\nendobj\n"
        else:
            out += ("%d 0 obj\n%s\nendobj\n" % (k, objs[k])).encode("latin-1")
    xref = len(out); top = max(offs) + 1
    out += b"xref\n0 %d\n0000000000 65535 f \n" % top
    for k in range(1, top):
        out += b"%010d 00000 n \n" % offs[k] if k in offs else b"0000000000 65535 f \n"
    out += b"trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (top, info, xref)
    with open(path, "wb") as f: f.write(out)

# ── The diagram's blocks. Each measures itself, then draws at (x, y): in at (x+cx, y), out at (x+cx, y+h). ──
GAP, AW = 15, 214
class Block:
    ends = False
class Act(Block):
    """An activity. stage= ties it to the executor's stage; several branches may carry one stage."""
    def __init__(s, title, say="", stage=None, kind="run", when=None, w=AW):
        s.title, s.say, s.stage, s.kind, s.when, s.aw = title, say, stage, kind, when, w
    def measure(s, info):
        s.lines = wrap(s.say, 7.6, s.aw - 14) if s.say else []
        s.cond = wrap("when: " + s.when, 6.8, s.aw - 14) if s.when else []
        s.io = []
        if s.stage and s.stage in info and s.kind == "run":
            n = info[s.stage]
            r = [x for x in n["reads"] if x != "halted"]
            if r: s.io += wrap("in  " + " ".join(r), 5.6, s.aw - 14, "F3")
            w = n["writes"] + n["appends"]
            if w: s.io += wrap("out " + " ".join(w), 5.6, s.aw - 14, "F3")
        s.w, s.cx = s.aw, s.aw / 2
        s.h = 19 + len(s.lines) * 9.2 + len(s.cond) * 8.2 + len(s.io) * 6.8 + 3
    def draw(s, p, x, y, ctx):
        f, st, dash = {"run": (RUNF, RUNS, None), "load": (LOADF, LOADS, [4, 3]), "silent": (SILF, SILS, [1, 2]),
                       "note": (NOTEF, NOTES, None)}[s.kind]
        p.rrect(x, y, s.w, s.h, fill=f, stroke=st, lw=1.1, dash=dash)
        yy = y + 13
        p.text(x + 7, yy, s.title, 9, "F2")
        yy += 3
        for l in s.lines: yy += 9.2; p.text(x + 7, yy, l, 7.6)
        for l in s.cond: yy += 8.2; p.text(x + 7, yy, l, 6.8, color=MUT)
        for l in s.io: yy += 6.8; p.text(x + 7, yy, l, 5.6, "F3", MUT)
class Seq(Block):
    def __init__(s, *items): s.items = list(items)
    def measure(s, info):
        for i in s.items: i.measure(info)
        l = max((i.cx for i in s.items), default=0)
        r = max((i.w - i.cx for i in s.items), default=0)
        s.w, s.cx = l + r, l
        s.h = sum(i.h for i in s.items) + GAP * max(0, len(s.items) - 1)
        s.ends = bool(s.items) and s.items[-1].ends
    def draw(s, p, x, y, ctx):
        yy = y
        for k, i in enumerate(s.items):
            i.draw(p, x + s.cx - i.cx, yy, ctx)
            yy += i.h
            if k < len(s.items) - 1:
                p.line([(x + s.cx, yy), (x + s.cx, yy + GAP)])
                yy += GAP
class Empty(Block):
    w, h, cx = 44, 0, 22
    def measure(s, info): pass
    def draw(s, p, x, y, ctx): pass
class If(Block):
    """A decision. dec = a key of DECISIONS (kept or not, cited), or a plain question for a capability gate."""
    DW, DH = 170, 54
    def __init__(s, dec, yes, no, gy="yes", gn="no"):
        s.dec = DECISIONS.get(dec); s.key = dec if s.dec else None
        s.q = s.dec["q"] if s.dec else dec
        s.a, s.b = yes or Empty(), no or Empty()
        s.gy, s.gn = gy, gn
    def measure(s, info):
        s.a.measure(info); s.b.measure(info)
        col = 22
        ax = s.a.cx
        bx = s.a.w + col + s.b.cx
        need = s.DW + 40
        if bx - ax < need: col += need - (bx - ax); bx = s.a.w + col + s.b.cx
        s.bleft = s.a.w + col
        s.ax, s.bx = ax, bx
        c = (ax + bx) / 2
        cap = 150 if s.dec else 0
        lpad = max(0, s.DW / 2 + 4 - c)
        s.ax += lpad; s.bx += lpad; s.bleft += lpad; s.aleft = lpad
        s.cx = c + lpad
        s.w = max(s.bleft + s.b.w, s.cx + max(s.DW / 2, cap + 8))
        s.top = 25 if s.dec else 4
        s.h = s.top + s.DH + 16 + max(s.a.h, s.b.h) + 22
        s.ends = s.a.ends and s.b.ends
        if s.ends: s.h -= 16
    def draw(s, p, x, y, ctx):
        cx = x + s.cx
        red = s.dec and s.dec["kept"] in ("none", "log")
        p.line([(cx, y), (cx, y + s.top)], arrow=False)
        if s.dec:
            f, st = TAGC[s.dec["kept"]]
            lab = KEPT[s.dec["kept"]]
            tw0 = tw(lab, 6.4, "F2") + 8
            p.rect(cx + 7, y + 1, tw0, 10, fill=f, stroke=st, lw=0.7, dash=[2, 1.5] if s.dec["kept"] == "log" else None)
            p.text(cx + 11, y + 8.6, lab, 6.4, "F2", st)
            p.text(cx + 7, y + 19, ctx["src"][s.key], 6, "F3", MUT)
        dy = y + s.top
        d = [(cx, dy), (cx + s.DW / 2, dy + s.DH / 2), (cx, dy + s.DH), (cx - s.DW / 2, dy + s.DH / 2)]
        p.poly(d, fill=WHITE, stroke=BAD if red else INK, lw=1.7 if red else 1.0)
        ls = wrap(s.q, 8.4, s.DW * 0.56, "F2")
        ty = dy + s.DH / 2 + 3 - (len(ls) - 1) * 5
        for l in ls: p.text(cx, ty, l, 8.4, "F2", align="c"); ty += 10
        my, by = dy + s.DH / 2, dy + s.DH + 16
        ax, bx = x + s.ax, x + s.bx
        p.line([(cx - s.DW / 2, my), (ax, my), (ax, by)], arrow=not isinstance(s.a, Empty)); p.text(cx - s.DW / 2 - 4, my - 3, "[" + s.gy + "]", 7.2, "F2", MUT, "r")
        p.line([(cx + s.DW / 2, my), (bx, my), (bx, by)], arrow=not isinstance(s.b, Empty)); p.text(cx + s.DW / 2 + 4, my - 3, "[" + s.gn + "]", 7.2, "F2", MUT)
        s.a.draw(p, x + s.aleft, by, ctx); s.b.draw(p, x + s.bleft, by, ctx)
        if s.ends: return
        mh = max(s.a.h, s.b.h)
        my2 = by + mh + 12
        for bl, bxx, side in ((s.a, ax, -1), (s.b, bx, 1)):
            if not bl.ends: p.line([(bxx, by + bl.h), (bxx, my2), (cx + side * 8, my2)])
        p.poly([(cx, my2 - 6), (cx + 8, my2), (cx, my2 + 6), (cx - 8, my2)], fill=WHITE, stroke=INK, lw=0.9)
        p.line([(cx, my2 + 6), (cx, y + s.h)], arrow=False)
class Fork(Block):
    def __init__(s, *branches, label=None): s.br, s.label = list(branches), label
    def measure(s, info):
        for b in s.br: b.measure(info)
        s.w = sum(b.w for b in s.br) + 20 * (len(s.br) - 1)
        c = [sum(b.w + 20 for b in s.br[:i]) + s.br[i].cx for i in range(len(s.br))]
        s.cs = c
        s.cx = (c[0] + c[-1]) / 2
        s.h = 3 + 14 + max(b.h for b in s.br) + 14 + 3
    def draw(s, p, x, y, ctx):
        c = [x + v for v in s.cs]
        p.rect(c[0] - 12, y, c[-1] - c[0] + 24, 3, fill=INK)
        jy = y + s.h - 3
        p.rect(c[0] - 12, jy, c[-1] - c[0] + 24, 3, fill=INK)
        xx = x
        for b, cc in zip(s.br, c):
            p.line([(cc, y + 3), (cc, y + 17)])
            b.draw(p, xx, y + 17, ctx)
            p.line([(cc, y + 17 + b.h), (cc, jy)])
            xx += b.w + 20
class Par(Block):
    """Stages with no ordering between them: a fork/join around a grid. Any order, any overlap."""
    def __init__(s, *acts, cols=2, label="any order, may overlap"): s.acts, s.cols, s.label = list(acts), cols, label
    def measure(s, info):
        for a in s.acts: a.aw = 196; a.measure(info)
        s.rows = [s.acts[i:i + s.cols] for i in range(0, len(s.acts), s.cols)]
        s.rh = [max(a.h for a in r) for r in s.rows]
        s.iw = s.cols * 196 + (s.cols - 1) * 10
        s.ih = sum(s.rh) + 10 * (len(s.rows) - 1)
        s.w, s.cx = s.iw + 20, (s.iw + 20) / 2
        s.h = 3 + 10 + 12 + s.ih + 12 + 10 + 3
    def draw(s, p, x, y, ctx):
        cx = x + s.cx
        p.rect(cx - 90, y, 180, 3, fill=INK)
        p.line([(cx, y + 3), (cx, y + 13)], arrow=False)
        p.rrect(x, y + 13, s.w, s.ih + 24, r=8, stroke=NOTES, lw=0.8, dash=[3, 3])
        p.text(x + 10, y + 21, s.label, 6.6, color=MUT)
        yy = y + 25
        for r, h in zip(s.rows, s.rh):
            xx = x + 10
            for a in r:
                a.h = h; a.draw(p, xx, yy, ctx); xx += 206
            yy += h + 10
        p.line([(cx, y + s.h - 13), (cx, y + s.h - 3)], arrow=False)
        p.rect(cx - 90, y + s.h - 3, 180, 3, fill=INK)
class Start(Block):
    w, cx, h = 20, 10, 18
    def measure(s, info): pass
    def draw(s, p, x, y, ctx): p.circle(x + 10, y + 9, 8, fill=INK)
class Stop(Block):
    ends = True
    def __init__(s, say): s.say = say
    def measure(s, info):
        s.w = max(26, tw(s.say, 7.6) + 8); s.cx = s.w / 2; s.h = 34
    def draw(s, p, x, y, ctx):
        c = x + s.cx
        p.circle(c, y + 11, 10, fill=WHITE, stroke=INK, lw=1); p.circle(c, y + 11, 6, fill=INK)
        p.text(c, y + 31, s.say, 7.6, color=MUT, align="c")
class Jump(Block):
    """An on-page connector: click it to follow the flow to another section."""
    def __init__(s, key, into=False): s.key, s.into = key, into
    def measure(s, info):
        s.w, s.cx, s.h = 200, 100, 24
    def draw(s, p, x, y, ctx):
        c = x + s.cx
        p.circle(c, y + 12, 11, fill=WHITE, stroke=RUNS, lw=1.2)
        n = ctx["pageno"][s.key]
        p.text(c, y + 15, ctx["title"][s.key].split()[0], 9, "F2", RUNS, "c")
        p.text(c + (15 if not s.into else -15), y + 15, ("continues: " if not s.into else "from: ") + " ".join(ctx["title"][s.key].split()[1:]),
               7.4, color=MUT, align="l" if not s.into else "r")
        p.link(x, y, s.w, s.h, n - 1)

def A(stage, say="", **k): return Act(stage, say, stage=stage, **k)
def flow():
    return [
    ("entry", "1  How the run starts", "Before any stage runs: what can be reloaded from the swing document. Everything later reads these answers.", Seq(
        Start(),
        If("Re-analysis of a saved swing?",
            Seq(Act("reanalyzeSwingDir", "Reads the swing document and opens each camera's frames."),
                If("raw", Act("Read raw frames", "Bit-faithful, as captured.", kind="run"), Act("Decode the MP4", "Compressed. Different pixels from the raw frames.", kind="run"), "yes", "no"),
                If("force", Act("Reload nothing", "Every producer runs again.", kind="note"), Seq(
                    If("over", Act("Reuse refused", "Any key: no shaft reuse. A pose., ball. or address key: no pose reuse either.", kind="note"), None, "any", "none"),
                    If("pose", Seq(Act("Load recorded pose", "Sets posePreloaded and its scope.", kind="load"),
                        If("ball", Seq(Act("Load recorded ball", "Sets ballPreloaded.", kind="load"),
                            If("shaft", If("ladder", Act("Load track and ladder", "Sets shaftPreloaded and ladderPreloaded.", kind="load"), Act("Drop the track", "A track needs its ladder.", kind="note")), None)), None)), None),
                    If("dtlpose", Act("Load recorded DTL pose", "Sets poseDtlPreloaded.", kind="load"), None)), "yes", "no")),
            Act("Live capture", "Frames come from the capture ring. Nothing is preloaded.", kind="note"), "yes", "live"),
        Jump("inputs"))),
    ("inputs", "2  Frames and sensors", "Decoded once per camera. The sensors decide the pose path on the next page.", Seq(
        Jump("entry", True),
        Fork(A("FrameDecodeFaceOn", "One decode of the window for every consumer.", when="frame store on, camera has frames"),
             A("FrameDecodeDtl", "The same for the down-the-line camera.", when="a DTL camera has frames"),
             Seq(A("ImuResample", "Motion sensor streams onto one clock.", when="at least one sensor"),
                 A("ImuSegmentation", "The sensor ladder.", when="sensor streams exist"),
                 A("WristMetrics", "Wrist series.", when="a sensor ladder exists")),
             A("Impact", "Impact camera frames around the strike.", when=GATE["Impact"])),
        Jump("faceon"))),
    ("faceon", "3  Face-on pose, ball and shaft", "The heart of #154: each producer either loads or computes, and the document says ran: true both ways.", Seq(
        Jump("inputs", True),
        If("posePreloaded?",
            Act("Pose: load", "Copy the recorded track. 0 ms, recorded as ran: true.", stage="Pose", kind="load"),
            Seq(If("scan",
                    Act("Pose: one bounded pass", "Sensor span plus pad, address hold reached back sparsely.", stage="Pose"),
                    If("--full-window?",
                        Act("Pose: whole window", "One pass over the whole window, one crop.", stage="Pose"),
                        Seq(Act("Pose: two-pass", "Eight-frame crop scan, then a coarse pass through that crop.", stage="Pose"),
                            If("span", Act("Fill the span", "Dense around impact.", kind="note"), Act("Fill the window", "Optimisation lost, result kept.", kind="note"))),
                        "yes", "no"),
                    "sensor span", "none"),
                If("crop", None, Act("Pose full-frame", "No crop.", kind="note"))), "yes", "no"),
        A("PoseSmooth", "Motion overlay smoother, on loaded and computed tracks alike.", when="pose has frames"),
        If("ballPreloaded?", Act("Ball: load", "0 ms, recorded as ran: true.", stage="Ball", kind="load"), Act("Ball: replay", "Tracks the ball through the window.", stage="Ball"), "yes", "no"),
        If("shaftPreloaded?",
            Seq(Act("Shaft: load", "3 ms, recorded as ran: true.", stage="Shaft", kind="load"),
                If("demote", Act("Demote them", "The plausibility checks run again on a reloaded track.", kind="note"), None),
                Act("Rebuild the synth", "From the recorded samples and P1.", kind="note")),
            If("bound", Act("Track the swing span", "The default.", stage="Shaft"), Act("Track the whole window", "--full-window only.", stage="Shaft")), "yes", "no"),
        If("witness", Act("Witness from sample flags", "Rebuilt from what was stored.", kind="note"), Act("Witness from the decide trace", "What the tracker actually saw.", kind="note"), "reused track", "fresh track"),
        Jump("ladder"))),
    ("ladder", "4  The ladder", "One ladder of phases and positions. A reused ladder stands the refiners down.", Seq(
        Jump("faceon", True),
        Fork(Seq(A("SegResolve", "Picks the ladder for the rest of the run."),
                 If("seg", Act("Reused, else adopted sensors", "A reused ladder outranks everything.", kind="note"), Act("Vision, else the sensor clamp", "", kind="note"), "reused or sensors", "neither")),
             Seq(A("ImpactAnchor", "Finds the address ball by its departure.", when="valid track, P1, P7 and pose exist"),
                 If("ballp1", Act("Record the ball, rebuild the synth", "", kind="note"), None))),
        A("ShaftLean", "Reads the samples and the ball line.", when="valid track"),
        If("Any product at all?", A("RequireProducts", "Metrics or pose exist."), Stop("halted: video only"), "yes", "no"),
        If("Reused ladder, or sensors adopted?", None,
           Seq(A("EventRefine", "Fine-tunes Takeaway and Address. A refined Address drags P1.", when="face-on camera, valid track, vision conf above 0"),
               If("P1 moved?", Act("Rebuild the synth", "Since the #154 fix: fresh and reused agree.", kind="note"), None)), "yes", "no"),
        If("fusion", If("Reused ladder?", None, A("TimelineFusion", "Arbitrates each position between sources."), "yes", "no"),
           A("PositionsLadder", "Back-fills empty slots. Never runs with fusion on.", kind="note"), "on", "off"),
        A("BindDetail", "Binds the ladder and series onto the result."),
        Jump("metrics"))),
    ("metrics", "5  Metrics", "These all append to the series, spliced back in authored order, so they may overlap.", Seq(
        Jump("ladder", True),
        Par(A("HeadTrack", "", when="face-on pose"), A("FootMetrics", "", when="face-on pose"),
            A("LowerBodyMetrics", "", when="face-on pose"), A("UpperBodyMetrics", "", when="face-on pose"),
            A("BodyRotation", "", when="pose, or a trunk sensor"), A("Tempo", "", when="stage on"),
            A("ClubDelivery", "", when=GATE["ClubDelivery"]), A("Kinematics", "", when="stage on")),
        A("ShaftPlane", "Mutates the shaft, so it waits for the readers before it.", when=GATE["ShaftPlane"]),
        Jump("dtl"))),
    ("dtl", "6  Down the line", "Everything that needs the second camera. Its pose decision is separate from the face-on one.", Seq(
        Jump("metrics", True),
        If("dtlcam", Seq(
            If("poseDtlPreloaded?", Act("DtlPose: load", "Keeps a pose scheduled around the ladder it was made with.", stage="DtlPose", kind="load"),
               Act("DtlPose: infer", "Schedule B, off this run's ladder.", stage="DtlPose"), "yes", "no"),
            A("DtlShaft", "The DTL club track, always recomputed.", when="valid shaft, witness exists"),
            Fork(A("ShaftFusion", "The fused 3-D shaft.", when=GATE["ShaftFusion"]),
                 A("DtlPosture", "Posture series and the ruler.", when=GATE["DtlPosture"]),
                 A("DtlShaftLie", "", when=GATE["DtlShaftLie"]))),
            Act("All DTL stages skip", "ran: false.", kind="note"), "yes", "no"),
        A("AddressMarks", "Runs on one camera too.", when=GATE["AddressMarks"]),
        Jump("skel"))),
    ("skel", "7  Skeleton and sequence", "The 3-D fit needs both cameras, but it starts either way.", Seq(
        Jump("dtl", True),
        If("Two cameras?", A("Skeleton3D", "Rigid skeleton fit to both poses, the shaft and any sensors."),
           Act("Skeleton3D: refuses", "Starts, stops at 1 ms. Recorded as ran: true.", stage="Skeleton3D", kind="silent"), "yes", "no"),
        Fork(Seq(A("BodyRotationTriangulated", "", when=GATE["BodyRotationTriangulated"]),
                 A("KinematicSequence", "", when=GATE["KinematicSequence"])),
             A("DtlSynth3D", "", when=GATE["DtlSynth3D"])),
        Jump("score"))),
    ("score", "8  Scoring and write-back", "The last stages, then what reaches the document.", Seq(
        Jump("skel", True),
        Fork(A("Bindings", "Which sensor sat where."), A("Resemblance", "The score against the reference.")),
        If("imu", A("Assessment", "Scored from the sensor lanes."), A("PoseAssessment", "Off by default, so it never runs.", kind="note"), "yes", "no"),
        Act("Write the document", "versions and timings.stages: name, ran, ms, thread, sometimes a skip reason. No record of the decisions on these pages beyond their Kept tags.", kind="note"),
        Stop("done"))),
    ]

# ── Checks: the drawing may not disagree with the code ───────────────────────
def walk(b, path, out):
    if isinstance(b, Seq):
        for i, x in enumerate(b.items): walk(x, path + [("s", id(b), i)], out)
    elif isinstance(b, If):
        walk(b.a, path + [("x", id(b), 0)], out); walk(b.b, path + [("x", id(b), 1)], out)
    elif isinstance(b, Fork):
        for i, x in enumerate(b.br): walk(x, path + [("p", id(b), i)], out)
    elif isinstance(b, Par):
        for i, x in enumerate(b.acts): walk(x, path + [("p", id(b), i)], out)
    elif isinstance(b, Act) and b.stage:
        out.setdefault(b.stage, []).append(path)

def order(p, q):
    """<0 p before q, >0 p after q, 0 parallel, None exclusive branches of one decision."""
    for a, b in zip(p, q):
        if a == b: continue
        if a[0] == "s": return a[2] - b[2]
        if a[0] == "x": return None
        return 0
    return -1

def check(dag, sections):
    errs, src = [], {}
    pos = {}
    for k, (key, _, _, block) in enumerate(sections):
        walk(block, [("s", 0, k)], pos)
    names = [n["name"] for n in dag["nodes"]]
    for n in names:
        if n not in pos: errs.append(f"stage {n} is in the export but not in the flow")
    for n in pos:
        if n not in names: errs.append(f"the flow names {n}, which is not in the export")
    for e in dag["edges"]:
        if "halted" in e["kinds"] and len(e["kinds"]) == 1: continue
        u, v = names[e["from"]], names[e["to"]]
        for p in pos.get(u, []):
            for q in pos.get(v, []):
                o = order(p, q)
                if o is None: continue
                if o == 0: errs.append(f"{u} -> {v} is a real edge {e['via']} but they are drawn in parallel")
                elif o > 0: errs.append(f"{u} -> {v} is a real edge {e['via']} but {v} is drawn first")
    for k, d in DECISIONS.items():
        path, needle = d["src"]
        hit = None
        with open(os.path.join(REPO, path), encoding="utf-8") as f:
            for i, l in enumerate(f, 1):
                if needle in l: hit = i; break
        if hit is None: errs.append(f"decision {k}: {needle!r} is not in {path}")
        else: src[k] = f"{os.path.basename(path)}:{hit}"
    return errs, src

def used(b, out):
    if isinstance(b, Seq):
        for x in b.items: used(x, out)
    elif isinstance(b, If):
        if b.key: out.add(b.key)
        used(b.a, out); used(b.b, out)
    elif isinstance(b, Fork):
        for x in b.br: used(x, out)

# ── The pages ────────────────────────────────────────────────────────────────
def contents(sections, ctx, dag):
    W, M = 760, 36
    rows = sorted(DECISIONS.items(), key=lambda kv: ["none", "log", "part", "doc"].index(kv[1]["kept"]))
    meas = [(wrap(d["where"], 7.6, 270), wrap(d["why"], 7.6, 200)) for _, d in rows]
    H = 330 + sum(max(len(a), len(b), 2) * 9.4 + 5 for a, b in meas) + 30
    p = Page(W, H, "Contents and decisions")
    p.text(M, 46, "Analysis pipeline: what runs, and why", 21, "F2")
    p.text(M, 63, "Every branch in one swing's analysis, as a UML activity diagram. Generated by tools/analysis/pipeline_activity.py and checked against the code.", 8.6, color=MUT)
    y = 88
    p.text(M, y, "Sections", 11, "F2"); y += 15
    for k, (key, title, say, _) in enumerate(sections):
        p.text(M, y, title, 9, "F2", RUNS); p.text(M + 170, y, say, 7.4, color=MUT)
        p.link(M - 2, y - 9, W - 2 * M, 12, k + 1); y += 13
    y += 12
    p.text(M, y, "Reading a page", 11, "F2"); y += 10
    x = M
    for kind, lab in (("run", "computes"), ("load", "loads from the document"), ("silent", "ran: true, did nothing"), ("note", "a step inside a stage")):
        f, st, d = {"run": (RUNF, RUNS, None), "load": (LOADF, LOADS, [4, 3]), "silent": (SILF, SILS, [1, 2]), "note": (NOTEF, NOTES, None)}[kind]
        p.rrect(x, y, 22, 12, r=3, fill=f, stroke=st, lw=1, dash=d); p.text(x + 27, y + 9, lab, 7.8); x += 34 + tw(lab, 7.8) + 12
    y += 28
    p.poly([(M + 12, y - 8), (M + 24, y), (M + 12, y + 8), (M, y)], fill=WHITE, stroke=BAD, lw=1.7)
    p.text(M + 30, y + 3, "red diamond: a decision the document does not keep. A black one is kept, or partly kept.", 7.8)
    y += 16
    p.rect(M, y - 1, 24, 3, fill=INK)
    p.text(M + 30, y + 3, "bar: fork or join. A join means all of it is finished before what is drawn next; the executor may overlap more, never less.", 7.8)
    y += 12
    nk = sum(1 for d in DECISIONS.values() if d["kept"] in ("none", "log"))
    p.text(M, y + 12, f"{nk} of {len(DECISIONS)} decisions leave no record in the document. Italic-free fine print inside a box lists what it reads (in) and writes (out), from the executor's export.".replace("Italic-free fine print inside a box lists", "The fine print inside a box lists"), 7.8, color=MUT)
    y += 30
    p.text(M, y, "The decisions", 11, "F2"); y += 13
    cols = (M, M + 112, M + 172, M + 450, M + 662)
    for x, h in zip(cols[:4], ("Decision", "Kept?", "Where the answer goes", "Why it matters")):
        p.text(x, y, h, 7.4, "F2", MUT)
    y += 5
    p.line([(M, y), (W - M, y)], arrow=False, color=NOTES, lw=0.6); y += 11
    for (k, d), (a, b) in zip(rows, meas):
        p.text(cols[0], y, d["q"], 8, "F2")
        p.text(cols[0], y + 8.6, ctx["src"][k], 5.4, "F3", MUT)
        f, st = TAGC[d["kept"]]; lab = KEPT[d["kept"]]
        p.rect(cols[1], y - 7.5, tw(lab, 6.2, "F2") + 7, 10, fill=f, stroke=st, lw=0.6, dash=[2, 1.5] if d["kept"] == "log" else None)
        p.text(cols[1] + 3.5, y, lab, 6.2, "F2", st)
        for i, l in enumerate(a): p.text(cols[2], y + i * 9.4, l, 7.6)
        for i, l in enumerate(b): p.text(cols[3], y + i * 9.4, l, 7.6)
        h = max(len(a), len(b), 2) * 9.4 + 5
        p.line([(M, y + h - 8), (W - M, y + h - 8)], arrow=False, color=NOTES, lw=0.3)
        y += h
    return p

def section_page(idx, sec, ctx, info):
    key, title, say, block = sec
    block.measure(info)
    W = max(620, block.w + 72)
    H = block.h + 112
    p = Page(W, H, title)
    p.text(36, 40, title, 17, "F2")
    for i, l in enumerate(wrap(say, 8.8, W - 72)): p.text(36, 56 + i * 11, l, 8.8, color=MUT)
    p.text(W - 36, 40, "Contents", 8.6, "F2", RUNS, "r"); p.link(W - 86, 30, 52, 14, 0)
    block.draw(p, (W - block.w) / 2, 80, ctx)
    return p

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dag", default=DAG); ap.add_argument("--out", default=OUT)
    ap.add_argument("--check", action="store_true", help="run the checks and write nothing")
    a = ap.parse_args()
    with open(a.dag, encoding="utf-8") as f: dag = json.load(f)
    secs = flow()
    errs, src = check(dag, secs)
    seen = set()
    for s in secs: used(s[3], seen)
    for k in DECISIONS:
        if k not in seen: errs.append(f"decision {k} is never drawn")
    if errs:
        print("pipeline_activity: the flow disagrees with the code:", file=sys.stderr)
        for e in errs: print("  " + e, file=sys.stderr)
        return 1
    print(f"ok: {len(dag['nodes'])} stages, {len(dag['edges'])} edges, {len(DECISIONS)} decisions match the code")
    if a.check: return 0
    ctx = dict(src=src, pageno={s[0]: i + 2 for i, s in enumerate(secs)}, title={s[0]: s[1] for s in secs})
    info = {n["name"]: n for n in dag["nodes"]}
    pages = [contents(secs, ctx, dag)] + [section_page(i, s, ctx, info) for i, s in enumerate(secs)]
    write_pdf(a.out, pages, "PinPoint analysis pipeline")
    print(f"wrote {a.out}: {len(pages)} pages, " + ", ".join(f"{p.w:.0f}x{p.h:.0f}" for p in pages))
    return 0

if __name__ == "__main__":
    sys.exit(main())
