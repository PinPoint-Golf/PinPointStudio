#!/opt/homebrew/bin/python3
"""assemble_review.py — build a review PDF from typeset-harness renders (render.sh / render_stage.sh).

  assemble_review.py --src <render dir> --dest <dir> --title "Charts — round 1" --tags t1,t2 [options]

Pages, in order (a page whose renders are all missing is left out):
  1. cover            --cover COVER.md (a text page)
  2. comparison       per theme, a grid of WINDOW grabs: one row per view, one column per tag, at
                      --compare-layout (default half). Tags are the candidates (or registers) side by side.
  3. widths           per tag × view × theme, the PANEL grabs at true scale: full on the first row,
                      the other layouts (stage, stagecam, half, third, cast) on the second.
  4. states           per tag × theme, one row per view, one column per demo state, at --states-layout.
  5. print            per --print tag × orientation × theme, the A4 pages small, plus each set saved as an
                      A4 PDF in <dest>/print/.
  6. notes            --notes a.md,b.md (text pages)
Writes <dest>/<name>.pdf and <dest>/pages/NN.png.

File names it reads (what the probes write):
  <tag>_<layout>_<aesthetic>_<light|dark>_<view>[_<state>]_{window,panel}.png
  <tag>_print-<orient>_<aesthetic>_<light|dark>_p<N>.png
Views, layouts, themes and states default to what is found for the given tags. render_stage.sh output
reads as tag <tag>_<mode>, view <panel>.

Pillow only. Run with /opt/homebrew/bin/python3 (no -I: Pillow is a site package).
"""
import argparse, glob, os, re, textwrap
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "..", "..", "src", "Resources", "fonts")
LAYOUT_ORDER = ["full", "stage", "stagecam", "half", "third", "cast"]
LAYOUT_LABEL = {"full": "Full", "stage": "Stage", "stagecam": "Beside the camera", "half": "Half",
                "third": "Third", "cast": "Cast (second screen)"}

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--src", required=True, help="the render directory ($TYPESET_OUT)")
ap.add_argument("--dest", required=True, help="where the PDF and pages go")
ap.add_argument("--title", default="Typeset review")
ap.add_argument("--name", default="review", help="PDF file name, without .pdf")
ap.add_argument("--tags", required=True, help="comma list; each is one candidate / register")
ap.add_argument("--labels", default="", help="comma list, one per tag (default: the tags)")
ap.add_argument("--views", default="", help="comma list (default: found)")
ap.add_argument("--view-labels", default="", help="comma list, one per view")
ap.add_argument("--layouts", default="", help="comma list (default: found)")
ap.add_argument("--themes", default="", help="e.g. folio_light,vector_dark (default: found)")
ap.add_argument("--states", default="", help="comma list (default: found)")
ap.add_argument("--compare-layout", default="half")
ap.add_argument("--states-layout", default="half")
ap.add_argument("--print", dest="print_tags", default="", help="comma list of print tags")
ap.add_argument("--cover", default="", help="a Markdown file for the cover page")
ap.add_argument("--notes", default="", help="comma list of Markdown files for the appendix")
A = ap.parse_args()
csv = lambda s: [x for x in s.split(",") if x]

def font(n, s): return ImageFont.truetype(os.path.join(FONTS, n), s)
F_TITLE, F_HEAD = font("InterDisplay-Regular.ttf", 64), font("InterDisplay-Regular.ttf", 40)
F_SUB, F_BODY, F_CAP = font("Inter-SemiBold.ttf", 26), font("Inter-Regular.ttf", 24), font("Inter-Regular.ttf", 22)
PAPER, INK, INK2, RULE = (250, 250, 247), (40, 40, 38), (110, 108, 104), (215, 213, 206)

# ── discovery ────────────────────────────────────────────────────────────────────────────────────
tags = csv(A.tags)
tag_label = dict(zip(tags, csv(A.labels) or tags))
want_views = csv(A.views)
FOUND = {}   # (tag, layout, theme, view, state, kind) -> path
pat = re.compile(r"^(?P<layout>" + "|".join(LAYOUT_ORDER) + r")_(?P<theme>[a-z0-9]+_(?:light|dark))_(?P<rest>.+)_(?P<kind>window|panel)\.png$")
for t in tags:
    for f in glob.glob(os.path.join(A.src, glob.escape(t) + "_*.png")):
        m = pat.match(os.path.basename(f)[len(t) + 1:])
        if not m: continue
        rest = m["rest"]
        if want_views:
            v = next((v for v in sorted(want_views, key=len, reverse=True) if rest == v or rest.startswith(v + "_")), None)
            if v is None: continue
            st = rest[len(v) + 1:]
        else:
            v, _, st = rest.partition("_")
        FOUND[(t, m["layout"], m["theme"], v, st, m["kind"])] = f
def found(i): return sorted({k[i] for k in FOUND}, key=lambda x: (LAYOUT_ORDER.index(x) if x in LAYOUT_ORDER else 99, x))
views = want_views or found(3)
view_label = dict(zip(views, csv(A.view_labels) or views))
layouts = csv(A.layouts) or found(1)
themes = csv(A.themes) or sorted(found(2), key=lambda x: (not x.startswith("folio"), x))
def states_for(t):
    return csv(A.states) or sorted({k[4] for k in FOUND if k[0] == t and k[4]})
theme_label = lambda th: th.replace("_", " ").capitalize()

def load(p): return Image.open(p).convert("RGB") if p and os.path.exists(p) else None
def img(tag, layout, theme, view, state="", kind="panel"): return load(FOUND.get((tag, layout, theme, view, state, kind)))

# ── page primitives ──────────────────────────────────────────────────────────────────────────────
def page(w, h): im = Image.new("RGB", (w, h), PAPER); return im, ImageDraw.Draw(im)
def head(d, W, m, title, sub):
    d.text((m, 70), title, font=F_HEAD, fill=INK)
    if sub: d.text((m, 135), sub, font=F_BODY, fill=INK2)
    d.line([m, 190, W - m, 190], fill=RULE, width=2)
def put(pg, d, im, x, y, cap, scale=1.0):
    d.text((x, y), cap, font=F_CAP, fill=INK2); y += 34
    if im is None:
        d.text((x, y), "missing", font=F_CAP, fill=INK2); return y + 40
    if scale != 1.0: im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
    pg.paste(im, (x, y)); d.rectangle([x - 1, y - 1, x + im.width, y + im.height], outline=RULE)
    return y + im.height

def row_page(title, sub, rows, gap=60, m=80):
    """rows: lists of (image, caption), pasted 1:1 — true scale. Rows with no image are dropped."""
    rows = [r for r in rows if any(im for im, _ in r)]
    if not rows: return None
    widths = [sum((im.width if im else 500) for im, _ in r) + gap * (len(r) - 1) for r in rows]
    heights = [max((im.height if im else 400) for im, _ in r) + 34 for r in rows]
    W = 2 * m + max(widths); H = 220 + sum(heights) + gap * (len(rows) - 1) + m
    pg, d = page(W, H); head(d, W, m, title, sub)
    y = 220
    for r, h in zip(rows, heights):
        x = m
        for im, cap in r:
            put(pg, d, im, x, y, cap); x += (im.width if im else 500) + gap
        y += h + gap
    return pg

def grid_page(title, sub, cells, cols, cw=1260):
    """cells: (image, caption), each scaled to width cw — for whole-window comparisons."""
    if not any(im for im, _ in cells): return None
    m, gap = 80, 60
    first = next(im for im, _ in cells if im)
    ch = int(cw * first.height / first.width) + 34
    rows = (len(cells) + cols - 1) // cols
    W = 2 * m + cols * cw + (cols - 1) * gap
    pg, d = page(W, 220 + rows * (ch + gap) + m); head(d, W, m, title, sub)
    for i, (im, cap) in enumerate(cells):
        put(pg, d, im, m + (i % cols) * (cw + gap), 220 + (i // cols) * (ch + gap), cap, cw / im.width if im else 1)
    return pg

def print_pages(tag, orient, theme):
    files = sorted(glob.glob(os.path.join(A.src, f"{glob.escape(tag)}_print-{orient}_{theme}_p*.png")),
                   key=lambda f: int(re.search(r"_p(\d+)\.png$", f).group(1)))
    if not files: return None, []
    ims = [load(f) for f in files]
    per, sc = (4 if orient == "portrait" else 3), 0.42
    pw, ph = int(ims[0].width * sc), int(ims[0].height * sc)
    m, gap = 80, 50
    rows = (len(ims) + per - 1) // per
    W = 2 * m + per * pw + (per - 1) * gap
    pg, d = page(W, 220 + rows * (ph + 34 + gap) + m)
    head(d, W, m, f"Print — A4 {orient}, {theme_label(theme)}", f"{tag} · {len(ims)} pages · shown small; the pages are in print/ as A4 PDFs")
    for i, im in enumerate(ims):
        put(pg, d, im, m + (i % per) * (pw + gap), 220 + (i // per) * (ph + 34 + gap), f"page {i+1}", sc)
    return pg, ims

def text_page(title, paras, W=3248):
    clean = lambda t: re.sub(r"\*\*?([^*]+)\*\*?", r"\1", t).replace("`", "")
    m, colw, gap, top = 120, 1440, 128, 230
    lines = []
    for kind, txt in paras:
        if kind == "h": lines += [("gap", ""), ("h", clean(txt)), ("hgap", "")]
        else:
            for ln in textwrap.wrap(clean(txt), 112): lines.append((kind, ln))
            lines.append(("pgap", ""))
    step = {"gap": 20, "h": 46, "hgap": 0, "pgap": 12}
    hs = [step.get(k, 36) for k, _ in lines]
    total = sum(hs); two = total > 1500; per = (total + 1) // 2 if two else total
    pg, d = page(W, top + per + 260)
    d.text((m, 100), title, font=F_TITLE, fill=INK)
    x, y, used = m, top, 0
    for (kind, txt), h in zip(lines, hs):
        if two and x == m and used >= per and kind in ("gap", "h"): x, y = m + colw + gap, top
        if kind == "h": d.text((x, y), txt, font=F_SUB, fill=INK)
        elif kind in ("p", "s"): d.text((x, y), txt, font=F_BODY, fill=INK2 if kind == "s" else INK)
        y += h; used += h
    return pg

def md(path):
    if not os.path.exists(path): return []
    out = []
    for ln in open(path).read().splitlines():
        s = ln.strip()
        if not s or s.startswith("```") or re.match(r"^\|?\s*:?-{3,}", s): continue
        if s.startswith("#"): out.append(("h", s.lstrip("# ")))
        else: out.append(("p", s.replace("|", "  ").strip()))
    return out

# ── the review ───────────────────────────────────────────────────────────────────────────────────
pages = []
if A.cover: pages.append(text_page(A.title, md(A.cover)))

cl = A.compare_layout
for th in themes:
    cells = [(img(t, cl, th, v, kind="window"), f"{view_label[v]} — {tag_label[t]}") for v in views for t in tags]
    pages.append(grid_page(f"{A.title} — side by side", f"{theme_label(th)} · {LAYOUT_LABEL.get(cl, cl)} width, in the real window",
                           cells, cols=min(len(tags), 3) if len(tags) > 1 else min(len(views), 2)))

for t in tags:
    for v in views:
        for th in themes:
            first = [(img(t, "full", th, v), "Full")] if "full" in layouts else []
            rest = [(img(t, l, th, v), LAYOUT_LABEL.get(l, l)) for l in layouts if l != "full"]
            pages.append(row_page(f"{tag_label[t]} · {view_label[v]}",
                                  f"{theme_label(th)} · {', '.join(LAYOUT_LABEL.get(l, l).lower() for l in layouts)} at true scale",
                                  [r for r in (first, rest) if r]))

sl = A.states_layout
for t in tags:
    states = states_for(t)
    if states:
        for th in themes:
            pages.append(row_page(f"{tag_label[t]} · interaction states",
                                  f"{theme_label(th)} · {LAYOUT_LABEL.get(sl, sl).lower()} · {', '.join(states)}",
                                  [[(img(t, sl, th, v, s), f"{view_label[v]} · {s}") for s in states] for v in views]))

pdir = os.path.join(A.dest, "print")
for pt in csv(A.print_tags):
    for orient in ("portrait", "landscape"):
        pthemes = sorted({re.search(rf"_print-{orient}_([a-z0-9]+_(?:light|dark))_p\d+\.png$", f).group(1)
                          for f in glob.glob(os.path.join(A.src, f"{glob.escape(pt)}_print-{orient}_*_p*.png"))})
        for th in pthemes:
            pg, ims = print_pages(pt, orient, th)
            if pg: pages.append(pg)
            if ims:
                os.makedirs(pdir, exist_ok=True)
                ims[0].save(os.path.join(pdir, f"{pt}_{orient}_{th}.pdf"), save_all=True, append_images=ims[1:], resolution=192)

for n in csv(A.notes):
    paras = md(n)
    if paras: pages.append(text_page("Notes · " + os.path.splitext(os.path.basename(n))[0], paras))

pages = [p for p in pages if p is not None]
if not pages: raise SystemExit(f"no renders found in {A.src} for tags {tags}")
os.makedirs(A.dest, exist_ok=True)
pd = os.path.join(A.dest, "pages"); os.makedirs(pd, exist_ok=True)
for f in glob.glob(pd + "/*.png"): os.remove(f)
for i, p in enumerate(pages): p.save(os.path.join(pd, f"{i+1:02d}.png"))
pages[0].save(os.path.join(A.dest, A.name + ".pdf"), save_all=True, append_images=pages[1:], resolution=200)
print(len(pages), "pages ->", os.path.join(A.dest, A.name + ".pdf"))
