#!/usr/bin/env python3
"""analysis_dag.py -- draw the analysis stage graph and a run's timeline.

docs/design/analysis_dag_design.md step A. Input is one of:

  * a dag.json from `swinglab_run --dag <file>` (pinpoint.analysisDag/1): the
    declared graph -- every stage's reads/writes, the edges they imply, the
    serial groups -- plus that run's stage timeline;
  * a result.json / swing.json (analysis.timings.stages) or a swinglab
    runmeta.json (stages): a timeline only, so only the Gantt is drawn.

Output by extension:

  * .svg  -- the graph (graphviz `dot` when it is on PATH, else a layered
    layout by topological rank) and, when there is a timeline, its Gantt
    beside it as <stem>_gantt.svg;
  * .html -- one self-contained page with both, inline SVG.

The graph: one box per stage, width and colour by the stage's wall ms in the
timeline (unit weight without one), edges from the transitive reduction (an
edge implied by a longer path is not drawn), the serial group boxed, and the
CRITICAL PATH -- the heaviest chain through the graph by stage ms, i.e. the
floor no amount of parallelism gets under -- in red. The Gantt: one row per
pool thread, a bar per stage that ran, the same critical path in red, and the
run's wall time.

python3 + stdlib only.

Usage:
    analysis_dag.py dag.json --out build/run-me/dag/graph.svg
    analysis_dag.py result.json --out gantt.svg
    analysis_dag.py dag.json --out dag.html [--title "07-04 s5, parallel"]
"""

import argparse
import html
import json
import math
import os
import shutil
import subprocess
import sys

# ── palette ──────────────────────────────────────────────────────────────────
# Sequential light→dark blue for "how long", one accent for the critical path.
SEQ = ["#eef3fb", "#d3e1f5", "#a9c6ec", "#76a3de", "#4a80c9", "#2b5fa8", "#1b437d"]
CRIT = "#d6402b"
INK = "#1f2430"
MUTED = "#6b7280"
GRID = "#e5e7eb"


def load(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    graph = doc if doc.get("schema", "").startswith("pinpoint.analysisDag") else None
    timeline = None
    if graph is not None:
        timeline = graph.get("timeline")
    elif isinstance(doc.get("analysis"), dict):
        timeline = doc["analysis"].get("timings", {}).get("stages")
    elif "stages" in doc:
        timeline = doc["stages"]
    if graph is None and not timeline:
        sys.exit(f"{path}: neither a pinpoint.analysisDag graph nor a stage timeline")
    return graph, timeline


def stage_ms(graph, timeline):
    """name -> ms (0 for a skipped stage); None without a timeline."""
    if not timeline:
        return None
    return {s["name"]: (float(s.get("ms", 0.0)) if s.get("ran") else 0.0) for s in timeline}


def critical_path(graph, ms):
    """Heaviest chain through the graph by stage ms (unit weight with no
    timeline). Edges point forward in authored order, so ids are a
    topological order."""
    nodes = graph["nodes"]
    n = len(nodes)
    w = [(ms.get(nd["name"], 0.0) if ms is not None else 1.0) for nd in nodes]
    preds = [[] for _ in range(n)]
    for e in graph["edges"]:
        preds[e["to"]].append(e["from"])
    best = [0.0] * n
    back = [-1] * n
    for j in range(n):
        b, bi = 0.0, -1
        for p in preds[j]:
            if best[p] > b:
                b, bi = best[p], p
        best[j] = b + w[j]
        back[j] = bi
    end = max(range(n), key=lambda i: best[i]) if n else -1
    path = []
    while end >= 0:
        path.append(end)
        end = back[end]
    path.reverse()
    total = sum(w[i] for i in path)
    return path, total


def colour_for(ms, ms_max):
    if ms is None or ms_max <= 0:
        return SEQ[1]
    t = math.log1p(ms) / math.log1p(ms_max)
    return SEQ[min(len(SEQ) - 1, int(t * (len(SEQ) - 1) + 0.5))]


def text_colour(fill):
    return "#ffffff" if fill in SEQ[4:] else INK


def esc(s):
    return html.escape(str(s), quote=True)


# ── graph: own layered layout ────────────────────────────────────────────────
def graph_svg(graph, ms, crit, title):
    nodes = graph["nodes"]
    n = len(nodes)
    edges = [e for e in graph["edges"] if not e.get("reduced")]
    rank = [nd.get("rank", 0) for nd in nodes]
    ms_max = max(ms.values()) if ms else 0.0
    crit_set = set(crit)
    crit_edges = {(crit[i], crit[i + 1]) for i in range(len(crit) - 1)}

    def width(i):
        if ms is None:
            return 150
        m = ms.get(nodes[i]["name"], 0.0)
        return 120 + min(160, 18 * math.sqrt(m / 100.0) * 4)

    by_rank = {}
    for i in range(n):
        by_rank.setdefault(rank[i], []).append(i)
    col_gap, row_h, box_h, pad = 70, 46, 32, 30
    col_w = {r: max(width(i) for i in ids) for r, ids in by_rank.items()}
    xs, x = {}, pad
    for r in sorted(by_rank):
        xs[r] = x
        x += col_w[r] + col_gap
    total_w = x - col_gap + pad
    max_rows = max(len(v) for v in by_rank.values())
    total_h = pad * 2 + 40 + max_rows * row_h + 40
    pos = {}
    for r, ids in by_rank.items():
        top = pad + 40 + (max_rows - len(ids)) * row_h / 2.0
        for k, i in enumerate(ids):
            pos[i] = (xs[r], top + k * row_h)

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {total_w:.0f} {total_h:.0f}" '
           f'width="{total_w:.0f}" height="{total_h:.0f}" font-family="Helvetica, Arial, sans-serif">',
           f'<rect width="100%" height="100%" fill="#ffffff"/>',
           f'<text x="{pad}" y="{pad + 8}" font-size="16" font-weight="600" fill="{INK}">{esc(title)}</text>']
    # serial groups
    names = [nd["name"] for nd in nodes]
    for grp in graph.get("serialGroups", []):
        ids = [names.index(g) for g in grp if g in names]
        if not ids:
            continue
        x0 = min(pos[i][0] for i in ids) - 8
        y0 = min(pos[i][1] for i in ids) - 8
        x1 = max(pos[i][0] + width(i) for i in ids) + 8
        y1 = max(pos[i][1] + box_h for i in ids) + 8
        out.append(f'<rect x="{x0:.0f}" y="{y0:.0f}" width="{x1 - x0:.0f}" height="{y1 - y0:.0f}" rx="8" '
                   f'fill="none" stroke="{MUTED}" stroke-dasharray="4 3"/>')
        out.append(f'<text x="{x0 + 4:.0f}" y="{y0 - 4:.0f}" font-size="10" fill="{MUTED}">serial group</text>')
    # edges
    for e in edges:
        a, b = e["from"], e["to"]
        (ax, ay), (bx, by) = pos[a], pos[b]
        x1, y1 = ax + width(a), ay + box_h / 2
        x2, y2 = bx, by + box_h / 2
        hot = (a, b) in crit_edges
        dx = max(30, (x2 - x1) / 2)
        tip = ", ".join(e.get("kinds", [])) + (": " + ", ".join(e.get("via", [])) if e.get("via") else "")
        out.append(f'<path d="M{x1:.1f},{y1:.1f} C{x1 + dx:.1f},{y1:.1f} {x2 - dx:.1f},{y2:.1f} {x2:.1f},{y2:.1f}" '
                   f'fill="none" stroke="{CRIT if hot else "#9aa3b2"}" stroke-width="{2.4 if hot else 1}" '
                   f'opacity="{1 if hot else 0.7}"><title>{esc(names[a])} → {esc(names[b])} ({esc(tip)})</title></path>')
    # nodes
    for i in range(n):
        x0, y0 = pos[i]
        m = ms.get(names[i], 0.0) if ms is not None else None
        fill = colour_for(m, ms_max)
        ran = ms is None or m > 0
        stroke = CRIT if i in crit_set else ("#c3c8d2" if ran else "#d1d5db")
        sw = 2.6 if i in crit_set else 1
        label = names[i] + (f"  {m / 1000:.2f} s" if m and m >= 100 else (f"  {m:.0f} ms" if m else ""))
        nd = nodes[i]
        tip = (f"{names[i]}\nreads: {', '.join(nd.get('reads', []))}\nwrites: {', '.join(nd.get('writes', []))}"
               f"\nappends: {', '.join(nd.get('appends', []))}\nseries: {nd.get('series')}")
        dash = "" if ran else ' stroke-dasharray="3 2"'
        out.append(f'<g><title>{esc(tip)}</title>'
                   f'<rect x="{x0:.0f}" y="{y0:.0f}" width="{width(i):.0f}" height="{box_h}" rx="5" '
                   f'fill="{fill if ran else "#f9fafb"}" stroke="{stroke}" stroke-width="{sw}"{dash}/>'
                   f'<text x="{x0 + 8:.0f}" y="{y0 + 20:.0f}" font-size="11" '
                   f'fill="{text_colour(fill) if ran else MUTED}">{esc(label)}</text></g>')
    out.append('</svg>')
    return "\n".join(out)


def graph_svg_dot(graph, ms, crit, title):
    """The same picture through graphviz `dot`; None if dot fails."""
    nodes = graph["nodes"]
    names = [nd["name"] for nd in nodes]
    ms_max = max(ms.values()) if ms else 0.0
    crit_set = set(crit)
    crit_edges = {(crit[i], crit[i + 1]) for i in range(len(crit) - 1)}
    lines = ["digraph G {", 'rankdir=LR; nodesep=0.15; ranksep=0.5;',
             f'label="{title}"; labelloc=t; fontname="Helvetica";',
             'node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=10];',
             'edge [color="#9aa3b2", arrowsize=0.6];']
    for k, grp in enumerate(graph.get("serialGroups", [])):
        lines.append(f'subgraph cluster_{k} {{ style=dashed; color="{MUTED}"; label="serial group"; fontsize=9;')
        lines.extend(f'  n{names.index(g)};' for g in grp if g in names)
        lines.append('}')
    for i, nm in enumerate(names):
        m = ms.get(nm, 0.0) if ms is not None else None
        fill = colour_for(m, ms_max)
        w = 1.2 + (min(2.2, 0.25 * math.sqrt(m / 100.0) * 4 / 4) if m else 0)
        label = nm + (f"\\n{m / 1000:.2f} s" if m and m >= 100 else (f"\\n{m:.0f} ms" if m else ""))
        extra = f', color="{CRIT}", penwidth=2.6' if i in crit_set else ''
        lines.append(f'n{i} [label="{label}", fillcolor="{fill}", fontcolor="{text_colour(fill)}", width={w:.2f}{extra}];')
    for e in graph["edges"]:
        if e.get("reduced"):
            continue
        hot = (e["from"], e["to"]) in crit_edges
        lines.append(f'n{e["from"]} -> n{e["to"]}' + (f' [color="{CRIT}", penwidth=2.4]' if hot else '') + ';')
    lines.append("}")
    try:
        r = subprocess.run(["dot", "-Tsvg"], input="\n".join(lines).encode(), capture_output=True, timeout=60)
        return r.stdout.decode() if r.returncode == 0 else None
    except Exception:
        return None


# ── Gantt ────────────────────────────────────────────────────────────────────
def gantt_svg(timeline, crit_names, title):
    ran = [s for s in timeline if s.get("ran")]
    if not ran:
        return '<svg xmlns="http://www.w3.org/2000/svg" width="300" height="40"><text x="10" y="25">no stage ran</text></svg>'
    t_end = max(float(s["endMs"]) for s in ran)
    t0 = min(float(s["startMs"]) for s in ran)
    threads = sorted({int(s.get("thread", 0)) for s in ran})
    left, right, top, row_h, bar_h = 110, 30, 60, 34, 24
    plot_w = 1100
    scale = plot_w / max(1.0, t_end)
    height = top + len(threads) * row_h + 50
    width = left + plot_w + right
    busy = sum(float(s["ms"]) for s in ran)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" '
           f'height="{height}" font-family="Helvetica, Arial, sans-serif">',
           '<rect width="100%" height="100%" fill="#ffffff"/>',
           f'<text x="{left}" y="24" font-size="16" font-weight="600" fill="{INK}">{esc(title)}</text>',
           f'<text x="{left}" y="44" font-size="12" fill="{MUTED}">wall {t_end / 1000:.2f} s · '
           f'stage time {busy / 1000:.2f} s on {len(threads)} thread{"s" if len(threads) != 1 else ""} · '
           f'critical path in red</text>']
    step = 500 if t_end <= 6000 else 1000 if t_end <= 20000 else 5000
    t = 0
    while t <= t_end + 1:
        x = left + t * scale
        out.append(f'<line x1="{x:.1f}" y1="{top - 6}" x2="{x:.1f}" y2="{top + len(threads) * row_h}" stroke="{GRID}"/>')
        out.append(f'<text x="{x:.1f}" y="{top + len(threads) * row_h + 16}" font-size="10" fill="{MUTED}" '
                   f'text-anchor="middle">{t / 1000:g} s</text>')
        t += step
    ms_max = max(float(s["ms"]) for s in ran)
    for r, th in enumerate(threads):
        y = top + r * row_h
        out.append(f'<text x="{left - 10}" y="{y + bar_h / 2 + 4:.0f}" font-size="11" fill="{INK}" '
                   f'text-anchor="end">thread {th}</text>')
        for s in ran:
            if int(s.get("thread", 0)) != th:
                continue
            x0 = left + float(s["startMs"]) * scale
            w = max(1.0, float(s["ms"]) * scale)
            hot = s["name"] in crit_names
            fill = colour_for(float(s["ms"]), ms_max)
            out.append(f'<g><title>{esc(s["name"])}: {float(s["ms"]):.0f} ms, '
                       f'{float(s["startMs"]):.0f} → {float(s["endMs"]):.0f} ms</title>'
                       f'<rect x="{x0:.1f}" y="{y}" width="{w:.1f}" height="{bar_h}" rx="3" fill="{fill}" '
                       f'stroke="{CRIT if hot else "#ffffff"}" stroke-width="{2 if hot else 0.6}"/>')
            label = s["name"]
            if w > 7 * len(label):
                out.append(f'<text x="{x0 + 4:.1f}" y="{y + bar_h / 2 + 4:.0f}" font-size="10" '
                           f'fill="{text_colour(fill)}">{esc(label)}</text>')
            out.append('</g>')
    out.append('</svg>')
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="dag.json (swinglab_run --dag), result.json, swing.json or runmeta.json")
    ap.add_argument("--out", required=True, help="output .svg or .html")
    ap.add_argument("--title", default=None)
    ap.add_argument("--no-dot", action="store_true", help="never use graphviz, always the built-in layout")
    args = ap.parse_args()

    graph, timeline = load(args.input)
    ms = stage_ms(graph, timeline)
    title = args.title or os.path.basename(os.path.dirname(os.path.abspath(args.input))) or args.input

    crit, crit_ms = ([], 0.0)
    if graph is not None:
        crit, crit_ms = critical_path(graph, ms)
        names = [nd["name"] for nd in graph["nodes"]]
        crit_names = {names[i] for i in crit}
        unit = "ms" if ms is not None else "stages"
        print(f"critical path ({crit_ms:.0f} {unit}): " + " → ".join(names[i] for i in crit))
    else:
        # No graph: the chain the run actually waited on, walking back from the last stage
        # to end through whichever stage ended last before it started.
        ran = sorted([s for s in timeline if s.get("ran")], key=lambda s: float(s["endMs"]))
        chain = []
        cur = ran[-1] if ran else None
        while cur is not None:
            chain.append(cur["name"])
            # Strictly earlier-ending, so two zero-length stages at one instant cannot cycle.
            prior = [s for s in ran if float(s["endMs"]) <= float(cur["startMs"]) + 0.5
                     and float(s["endMs"]) < float(cur["endMs"])]
            cur = max(prior, key=lambda s: float(s["endMs"])) if prior else None
        chain.reverse()
        crit_names = set(chain)
        print("waited-on chain: " + " → ".join(chain))

    if timeline:
        ran = [s for s in timeline if s.get("ran")]
        wall = max(float(s["endMs"]) for s in ran) if ran else 0.0
        print(f"wall {wall:.0f} ms, stage time {sum(float(s['ms']) for s in ran):.0f} ms, "
              f"threads {len({int(s.get('thread', 0)) for s in ran})}")

    out = args.out
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    gsvg = None
    if graph is not None:
        gtitle = f"{graph.get('profile', '')} stage graph — {title}"
        if not args.no_dot and shutil.which("dot") and out.endswith(".svg"):
            gsvg = graph_svg_dot(graph, ms, crit, gtitle)
        if gsvg is None:
            gsvg = graph_svg(graph, ms, crit, gtitle)
    tsvg = gantt_svg(timeline, crit_names, f"Run timeline — {title}") if timeline else None

    if out.endswith(".html"):
        parts = [p for p in (gsvg, tsvg) if p]
        page = ("<!doctype html><html><head><meta charset='utf-8'><title>Analysis DAG</title>"
                "<style>body{margin:16px;font-family:Helvetica,Arial,sans-serif;background:#fff;color:#1f2430}"
                "svg{max-width:100%;height:auto;display:block;margin-bottom:24px}</style></head><body>"
                + "\n".join(parts) + "</body></html>")
        with open(out, "w", encoding="utf-8") as f:
            f.write(page)
        print(f"wrote {out}")
    else:
        stem = out[:-4] if out.endswith(".svg") else out
        if gsvg:
            with open(out, "w", encoding="utf-8") as f:
                f.write(gsvg)
            print(f"wrote {out}")
        if tsvg:
            gpath = stem + "_gantt.svg" if gsvg else out
            with open(gpath, "w", encoding="utf-8") as f:
                f.write(tsvg)
            print(f"wrote {gpath}")


if __name__ == "__main__":
    main()
