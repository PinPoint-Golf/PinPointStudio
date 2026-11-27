#!/usr/bin/env python3
"""Lines of code over time across a project's repos, with feature milestones.

Walks `git log --numstat` in every repo of a preset, keeps a running line count per
category (product / tests / docs / tools), and draws the cumulative curves with a
smoothed weekly growth rate underneath. Release tags are marked on the axis, so a
plateau before a release (or its absence) is easy to see.

    python3 tools/history/loc_history.py                           # PinPoint -> build/history/loc_history.png
    python3 tools/history/loc_history.py --project goldencheetah   # ../GoldenCheetah -> loc_history_goldencheetah.png
    python3 tools/history/loc_history.py --open                    # ... and open it
    python3 tools/history/loc_history.py --csv out.csv             # also dump the daily series

Needs numpy, scipy and matplotlib. Repos are found next to the PinPointStudio
checkout (override with --projects). A preset is a dict in PRESETS: add one to chart
another project.

Counted: product = C/C++/ObjC++/QML/Swift/shaders/CMake (qmake .pro/.pri and Qt .ui
for GoldenCheetah); tests = anything under a tests/ or Tests/ directory; docs =
Markdown; tools = py/sh/cmd/ps1/js scripts. Excluded: vendored code (third_party/,
qwt/, contrib/ ...), build/, Mothballed/, CSV/JSON/images and other data.
Lines are net (added minus deleted), so a move between folders is neutral.

Bugs: when a preset names GitHub repos in `issues`, a panel between the lines and the
growth rate carries the number of issues labelled `bug` open on each day, read with `gh`
from each issue's opened and closed dates. A reopened bug counts from its first opening
to its last close. The same panel carries the running total of those bugs closed, so its
slope shows the effort going into fixing them; opened = open + closed on every day. The
panel has its own time axis, starting on the first day there is a bug on record.
--no-issues leaves them off.

Release readiness: for PinPoint, a panel at the top shows the requirements
(governance/artefacts/requirements.md) as a heatmap of area by release and MoSCoW
(v1 Must, v1 Should, v1 Could, v2): in each cell, done out of all with partial, planned
and proposed beneath, and how many rows an open bug blocks; the shade is the share
ready (done with no open bug). Under the bugs panel, the open bugs by their area label. It reads
the page and the live issues through tools/method/requirements_table.py, the same reader
as the traceability report (tools/method/make_rtm.py). --no-readiness leaves it off.
"""
import argparse
import csv
import datetime as dt
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter1d

CODE_EXT = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".mm", ".m", ".qml", ".swift",
            ".metal", ".glsl", ".frag", ".vert", ".cmake"}
SCRIPT_EXT = {".py", ".sh", ".cmd", ".bat", ".ps1", ".js"}

# Each preset: repos as (directory next to this checkout, ref); skip = vendored/build
# directories (any path component); test_dirs; milestones as (label, date, text y in k lines,
# text x shift in days) with hand-picked dates, when the feature first worked end to end;
# release = regex for the tags that are releases (named, drawn as a diamond + line), other
# tags are alphas/RCs, drawn as faint lines (minor="line") or small ticks (minor="tick").
PRESETS = {
    "pinpoint": dict(
        title="PinPoint: lines of code over time across all six repos, with the features as they arrived",
        repos=[("PinPointStudio", "main"), ("PinPointCapture", "main"), ("libppcp", "main"),
               ("libwrist", "main"), ("libgspro", "main"), ("libppswing", "main")],
        tag_repo="PinPointStudio", release=r"beta", minor="line",
        skip={"third_party", "build", "Mothballed", "node_modules"},
        test_dirs={"tests", "Tests"}, extra_code=set(),
        sigma=7.0, since="2026-07-25", since_label="late July", xtick="2w",
        out="loc_history.png",
        # PinPointCapture's issues are its v1-v3 roadmap (epics, decisions), not defects;
        # the app's bug count is the stability signal.
        issues=["PinPoint-Golf/PinPointStudio"],
        readiness=True,
        milestones=[
            ("Capture, IMU, audio, STT/TTS", "2026-04-29", 52, 0),
            ("Pose", "2026-05-08", 120, 0),
            ("Club tracker", "2026-05-20", 80, 0),
            ("Analysis engine", "2026-06-07", 185, 0),
            ("GUI rebuilt\n(review, shot, settings)", "2026-06-14", 135, 0),
            ("Updater", "2026-06-17", 32, 0),
            ("Metrics", "2026-07-22", 245, -2),
            ("Diagnostics +\ndiagnostic model", "2026-07-25", 38, 0),
            ("Launch monitor", "2026-08-04", 205, 2),
            ("HackMotion wrist\n(libwrist)", "2026-08-16", 290, 0),
            ("Phones: PPCP +\niPhone capture app", "2026-08-21", 185, 0),
            ("Markerless shaft", "2026-09-08", 375, 0),
            ("GSPro connector", "2026-09-09", 205, 0),
            (".ppsw swing format", "2026-09-23", 462, -14),
            ("3-D swing\n(skeleton3d)", "2026-09-27", 420, -3),
            ("Work-ons, themes,\nsession setup", "2026-10-04", 495, 0),
            ("Folio theme,\ntypeset panels", "2026-10-10", 300, -14),
            ("Charts panel,\ntrust + gates", "2026-10-14", 250, -2),
            ("Session report PDF", "2026-10-20", 345, 0),
            ("Posture composites", "2026-10-22", 535, -5),
            ("Trunk IMUs\n(hip + chest sensors)", "2026-10-26", 280, 4),
            ("UX refactor: one rail,\nno session modes", "2026-10-28", 585, -8),
            ("Foresight Premier\nconnector", "2026-11-02", 462, 0),
            ("Variability, Consistency\n& Tendencies", "2026-11-11", 635, -3),
            ("Domain model\nrefactor", "2026-11-24", 495, -2),
        ]),
    "goldencheetah": dict(
        title="GoldenCheetah: lines of code over time, with the features as they arrived",
        repos=[("GoldenCheetah", "origin/master")],
        tag_repo="GoldenCheetah", release=r"^[vV]\d+\.\d+$", minor="tick",
        skip={"third_party", "build", "qwt", "contrib", "vendor", "levmar", "qtsolutions", "qxt"},
        test_dirs={"test", "tests", "unittests", "Tests"}, extra_code={".ui", ".pro", ".pri"},
        sigma=30.0, since=None, since_label="", xtick="1y",
        out="loc_history_goldencheetah.png",
        milestones=[
            ("Initial import", "2006-09-06", 45, 200),
            ("ANT+ sport", "2009-03-18", 60, 0),
            ("Train view", "2011-09-01", 150, -300),
            ("Strava upload,\nErgDB", "2011-11-26", 40, 0),
            ("Measures\n(Withings)", "2012-07-08", 175, -150),
            ("R embedding,\nBluetooth LE", "2016-04-25", 120, -200),
            ("Cloud services\nrefactor", "2017-03-25", 190, 0),
            ("Python embedding", "2017-11-22", 130, 350),
            ("User charts", "2020-03-08", 240, -250),
            ("ChartSpace overview", "2020-05-31", 270, 300),
        ]),
}


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                          check=True, errors="replace").stdout


def categorise(path, cfg):
    parts = path.split("/")
    if cfg["skip"] & set(parts[:-1]):
        return None
    ext = os.path.splitext(path)[1].lower()
    is_test = any(p in cfg["test_dirs"] for p in parts[:-1])
    if ext == ".md":
        return "docs"
    if ext in CODE_EXT or ext in cfg["extra_code"] or ext in SCRIPT_EXT or parts[-1] == "CMakeLists.txt":
        if is_test:
            return "tests"
        if ext in SCRIPT_EXT:
            return "tools"
        return "product"
    return None


def read_repo(repo, ref, cfg):
    """Return per-day net line deltas {date: {cat: n}}."""
    out = git(repo, "log", ref, "--no-renames", "--numstat", "--reverse", "--format=@@%cs")
    deltas = defaultdict(lambda: defaultdict(int))
    day = None
    for line in out.splitlines():
        if line.startswith("@@"):
            day = line[2:]
        elif line.strip() and day:
            add, rem, path = line.split("\t", 2)
            cat = categorise(path, cfg)
            if cat and add != "-":
                deltas[day][cat] += int(add) - int(rem)
    return deltas


def bug_counts(repos, days):
    """(open, closed so far) at the end of each day across `repos`, or (None, None) if gh can't say."""
    rows = []
    for repo in repos:
        try:
            out = subprocess.run(["gh", "issue", "list", "-R", repo, "--state", "all", "--limit", "5000",
                                  "--json", "createdAt,closedAt,labels"],
                                 capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError) as e:
            print(f"open bugs left off: gh issue list -R {repo} failed ({getattr(e, 'stderr', '') or e})".strip(), file=sys.stderr)
            return None, None
        import json
        for it in json.loads(out):
            opened = dt.date.fromisoformat(it["createdAt"][:10])
            closed = dt.date.fromisoformat(it["closedAt"][:10]) if it.get("closedAt") else None
            if any(l["name"] == "bug" for l in it["labels"]):
                rows.append((opened, closed))
    if not rows:
        return None, None
    bugs = np.full(len(days), np.nan)
    closed_so_far = np.full(len(days), np.nan)
    first = min(o for o, _ in rows)
    for i, d in enumerate(days):
        if d >= first:
            bugs[i] = sum(1 for o, c in rows if o <= d and (c is None or c > d))
            closed_so_far[i] = sum(1 for _, c in rows if c is not None and c <= d)
    return bugs, closed_so_far


META_LABELS = {"bug", "enhancement", "edge case", "finding", "follow-up", "engineering", "documentation",
               "question", "platform refactor"}


def readiness(root, want_issues):
    """(rt, rows, have_issues, open bugs by area label) for the readiness panel."""
    sys.path.insert(0, str(root / "tools/method"))
    import requirements_table as rt
    rows = rt.read_requirements(root)
    issues = rt.read_issues() if want_issues else None
    if want_issues and issues is None:
        print("readiness: issues left off (gh issue list failed)", file=sys.stderr)
    rt.link_issues(rows, issues)
    by_label = None
    if issues is not None:
        by_label = defaultdict(int)
        for it in issues.values():
            if it["state"] == "OPEN" and it["bug"]:
                for lab in it["labels"]:
                    if lab not in META_LABELS and not lab.startswith("priority"):
                        by_label[lab] += 1
    return rt, rows, issues is not None, by_label


def draw_readiness(axg, rt, rows, have_issues, by_label):
    """The release-readiness heatmap."""
    from matplotlib.colors import to_rgb
    from matplotlib.patches import FancyBboxPatch
    RED, INK, MUTED = "#b03a48", "#222", "#666"
    lo, hi = np.array(to_rgb("#eef6f1")), np.array(to_rgb("#2e7a52"))   # one green ramp, light to dark
    cols = ["v1 Must", "v1 Should", "v1 Could", "v2"]
    areas = list(dict.fromkeys(r["area"] for r in rows))
    live = [r for r in rows if r["status"] != "won't have"]
    LABEL_W = 1.05                                   # the area names sit inside the axes, left of the cells
    axg.set_xlim(-LABEL_W, len(cols))
    axg.set_ylim(len(areas), -0.95)
    axg.axis("off")
    for j, col in enumerate(cols):
        cr = [r for r in live if rt.column(r) == col]
        done = sum(r["status"] == "done" for r in cr)
        ready = sum(r["status"] == "done" and not r.get("open_bugs") for r in cr)
        axg.text(j + 0.5, -0.62, col, ha="center", va="center", fontsize=11, fontweight="bold", color=INK)
        axg.text(j + 0.5, -0.22, f"{100 * ready / max(len(cr), 1):.0f}% ready ({done}/{len(cr)} done)",
                 ha="center", va="center", fontsize=8.5, color=MUTED)
    for i, area in enumerate(areas):
        axg.text(-0.04, i + 0.5, area, ha="right", va="center", fontsize=9.5, color=INK)
        for j, col in enumerate(cols):
            cr = [r for r in live if r["area"] == area and rt.column(r) == col]
            x0, y0, w, h = j + 0.025, i + 0.07, 0.95, 0.86
            if not cr:
                axg.add_patch(FancyBboxPatch((x0, y0), w, h, boxstyle="round,pad=0,rounding_size=0.04",
                                             fc="#f5f5f5", ec="none"))
                axg.text(j + 0.5, i + 0.5, "-", ha="center", va="center", fontsize=9, color="#aaa")
                continue
            n = {s: sum(r["status"] == s for r in cr) for s in ("done", "partial", "planned", "proposed")}
            blocked = sum(bool(r.get("open_bugs")) for r in cr)
            frac = sum(r["status"] == "done" and not r.get("open_bugs") for r in cr) / len(cr)
            fc = lo + (hi - lo) * frac
            axg.add_patch(FancyBboxPatch((x0, y0), w, h, boxstyle="round,pad=0,rounding_size=0.04", fc=fc, ec="none"))
            ink = "white" if frac > 0.55 else INK
            sub = " · ".join(f"{v} {k[:4] + '.' if k == 'proposed' else k}" for k, v in n.items() if v and k != "done")
            dy = 0.17 if blocked else 0.0
            axg.text(j + 0.5, i + 0.36 - dy / 2, f"{n['done']} / {len(cr)} done", ha="center", va="center",
                     fontsize=9, fontweight="bold", color=ink)
            axg.text(j + 0.5, i + 0.66 - dy / 2, sub or " ", ha="center", va="center", fontsize=7,
                     color=ink if frac > 0.55 else MUTED)
            if blocked:
                axg.text(j + 0.5, i + 0.86, f"\u26a0 {blocked} blocked by {'a bug' if blocked == 1 else 'bugs'}", ha="center", va="center",
                         fontsize=7.5, fontweight="bold", color="white" if frac > 0.55 else RED)
    axg.set_title("Release readiness: requirements by area, release and MoSCoW "
                  "(done = shipped in a beta, used, no open bug)", loc="left", fontsize=11, pad=4)



def draw_bugs_by_label(axl, have_issues, by_label):
    """Open bugs by area label: one bar per label, the labels along the x axis, most first."""
    INK, MUTED = "#222", "#666"
    axl.set_title("Open bugs by area label", loc="left", fontsize=11)
    for sp in ("top", "right"):
        axl.spines[sp].set_visible(False)
    if not by_label:
        axl.axis("off")
        axl.text(0, 0.5, "issues not read" if not have_issues else "no open bugs", fontsize=9, color=MUTED,
                 transform=axl.transAxes)
        return
    items = sorted(by_label.items(), key=lambda kv: (-kv[1], kv[0]))
    xs = np.arange(len(items))
    axl.bar(xs, [v for _, v in items], width=0.62, color="#7a3e8f")
    for x, (_, v) in zip(xs, items):
        axl.text(x, v, f"{v}", ha="center", va="bottom", fontsize=9, color=INK)
    axl.set_xticks(xs, [lab.replace(" ", "\n", 1) for lab, _ in items], fontsize=9)   # two-word labels wrap
    from matplotlib.ticker import MaxNLocator
    axl.yaxis.set_major_locator(MaxNLocator(integer=True))
    axl.set_xlim(-0.6, len(items) - 0.4)
    axl.set_ylim(0, max(v for _, v in items) * 1.22)
    axl.set_ylabel("open bugs")
    axl.tick_params(axis="x", length=0)
    axl.tick_params(axis="y", labelsize=9)
    axl.grid(axis="y", color="#e4e4e4", lw=0.8)
    axl.set_axisbelow(True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--project", choices=sorted(PRESETS), default="pinpoint")
    ap.add_argument("--projects", default=str(root.parent), help="directory holding the repos")
    ap.add_argument("--out", help="PNG to write (default build/history/<preset's name>)")
    ap.add_argument("--csv", help="also write the daily cumulative series here")
    ap.add_argument("--open", action="store_true", help="open the PNG when done (macOS)")
    ap.add_argument("--sigma", type=float, help="smoothing of the growth rate, in days (default per preset)")
    ap.add_argument("--no-issues", action="store_true", help="leave the bugs panel off")
    ap.add_argument("--no-readiness", action="store_true", help="leave the release-readiness panel off")
    args = ap.parse_args()
    cfg = PRESETS[args.project]
    sigma = args.sigma or cfg["sigma"]
    projects = Path(args.projects)
    repos = [r for r, _ in cfg["repos"]]

    deltas = {}
    for r, ref in cfg["repos"]:
        if not (projects / r / ".git").exists():
            sys.exit(f"missing repo {projects / r}")
        deltas[r] = read_repo(projects / r, ref, cfg)

    cats = ["product", "tests", "docs", "tools"]
    start = min(dt.date.fromisoformat(d) for r in repos for d in deltas[r])
    last = max(dt.date.fromisoformat(d) for r in repos for d in deltas[r])
    end = max(last, dt.date.today()) if args.project == "pinpoint" else last
    days = [start + dt.timedelta(n) for n in range((end - start).days + 1)]
    daily = {c: np.zeros(len(days)) for c in cats}
    for r in repos:
        for d, per in deltas[r].items():
            i = (dt.date.fromisoformat(d) - start).days
            for c, n in per.items():
                daily[c][i] += n
    cum = {c: np.cumsum(daily[c]) / 1000 for c in cats}
    bugs, closed_bugs = (None, None) if args.no_issues or not cfg.get("issues") else bug_counts(cfg["issues"], days)

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["date", *cats, *(["open_bugs", "closed_bugs"] if bugs is not None else [])])
            for i, d in enumerate(days):
                extra = [] if bugs is None else ["" if np.isnan(v[i]) else int(v[i]) for v in (bugs, closed_bugs)]
                w.writerow([d.isoformat(), *(round(cum[c][i] * 1000) for c in cats), *extra])

    # Growth rate: Gaussian-smoothed daily net product lines, normalised at the
    # edges so the last days are not pulled toward zero.
    rate = np.maximum(daily["product"], 0)
    norm = gaussian_filter1d(np.ones(len(days)), sigma, mode="constant")
    weekly = gaussian_filter1d(rate, sigma, mode="constant") / norm * 7 / 1000

    # Release tags.
    tags = []
    for line in git(projects / cfg["tag_repo"], "for-each-ref", "refs/tags",
                    "--format=%(refname:short)\t%(creatordate:short)").splitlines():
        name, d = line.split("\t")
        d = dt.date.fromisoformat(d)
        if start <= d <= end:
            tags.append((name, d))
    tags.sort(key=lambda t: t[1])

    resolved = [(label, dt.date.fromisoformat(d), ty, dx) for label, d, ty, dx in cfg["milestones"]]

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    ready = None
    if cfg.get("readiness") and not args.no_readiness:
        ready = readiness(root, not args.no_issues)

    BLUE, ORANGE, GREEN, GOLD, RED = "#4c6fcf", "#c8754a", "#5aa27c", "#d4a72c", "#b03a48"
    ratios = [3, 1] if bugs is None else [5.45, 1.75, 1.43]   # main panel stays 5.45 in tall either way
    top = 0 if ready is None else 1
    if ready is not None:
        ratios = [len({r["area"] for r in ready[1]}) * 0.5 + 0.8] + ratios
        if bugs is not None:
            ratios.insert(3, 1.45)                    # open bugs by area label, under the bugs panel
    fig = plt.figure(figsize=(14, (10 if bugs is None else 13)
                              + (0 if ready is None else ratios[0] * 1.15 + (1.9 if bugs is not None else 0))))
    gs = fig.add_gridspec(len(ratios), 1, hspace=(0.14 if bugs is None else 0.24) * (1 if ready is None else 0.8),
                          height_ratios=ratios, **({} if ready is None else dict(top=0.965, bottom=0.06)))
    if ready is not None:
        axg = fig.add_subplot(gs[0])
    ax = fig.add_subplot(gs[top])
    ax2 = fig.add_subplot(gs[-1], sharex=ax)
    ax.tick_params(labelbottom=False)
    axb = None if bugs is None else fig.add_subplot(gs[top + 1])
    axl = fig.add_subplot(gs[top + 2]) if ready is not None and bugs is not None else None
    x = mdates.date2num(days)
    span = x[-1] - x[0]
    ends = []
    for c, col, lab in [("product", BLUE, "Product code (C++, QML, C, Swift)" if args.project == "pinpoint" else "Product code (C++, Qt)"),
                        ("tests", ORANGE, "Tests"), ("docs", GREEN, "Docs"), ("tools", GOLD, "Tools / scripts")]:
        ax.plot(x, cum[c], color=col, lw=2.6, label=lab)
        ends.append((cum[c][-1], f"{lab.split(' (')[0]} {cum[c][-1]:.0f}k"))
    ytop = max(cum["product"][-1] * 1.25, 100)
    placed = -ytop * 0.02
    for y, text in sorted(ends):                 # keep the end labels from stacking on each other
        placed = max(y, placed + ytop * 0.05)
        ax.annotate(text, (x[-1], y), xytext=(x[-1] + span * 0.01, placed), textcoords="data", va="center",
                    fontsize=10, color="#333", annotation_clip=False)
    for label, d, ty, dx in resolved:
        i = (d - start).days
        ax.plot(x[i], cum["product"][i], "o", mfc="white", mec=BLUE, mew=2, ms=7, zorder=5)
        ax.annotate(label, (x[i], cum["product"][i]), xytext=(x[i] + dx, ty), ha="center",
                    va="top" if ty < cum["product"][i] else "bottom", fontsize=8.5, color="#222",
                    arrowprops=dict(arrowstyle="-", color="#888", lw=0.8, shrinkA=2, shrinkB=4))
    ax.set_ylabel("Lines (thousands)")
    ax.set_ylim(0, max(cum["product"][-1] * 1.25, 100))
    ax.set_title(cfg["title"], loc="left", fontsize=15 if ready is None else 12)
    if ready is not None:
        fig.suptitle(cfg["title"].split(":")[0] + ": release readiness, growth and bugs", x=0.125, ha="left",
                     fontsize=15, y=0.99)
        draw_readiness(axg, *ready[:4])
        if axl is not None:
            draw_bugs_by_label(axl, ready[2], ready[3])

    ax2.fill_between(x, weekly, color="#d3dff7")
    solid = len(days) - int(sigma)
    ax2.plot(x[:solid + 1], weekly[:solid + 1], color="#3a52a8", lw=2.4)
    ax2.plot(x[solid:], weekly[solid:], color="#3a52a8", lw=2.4, ls="--")
    if cfg["since"]:
        since = days.index(dt.date.fromisoformat(cfg["since"]))
        avg = np.maximum(daily["product"][since:], 0).sum() / ((len(days) - since) / 7) / 1000
        ax2.axhline(avg, color="#777", ls="--", lw=1)
        ax2.text(x[0] + 2, avg + 0.8, f"average since {cfg['since_label']}: {avg:.0f}k / week", color="#777", fontsize=9)
    ax2.set_ylabel("k lines / week")
    ax2.set_ylim(0, weekly.max() * 1.1)
    ax2.set_title("Product growth", loc="left", fontsize=11)

    # Bugs: open and the running total closed, on their own time axis because the record is
    # only weeks long. Each line is named at its last value.
    if axb is not None:
        PURPLE, TEAL = "#7a3e8f", "#1f8a70"
        first_bug = x[np.flatnonzero(~np.isnan(bugs))[0]]
        for series, col, word in [(closed_bugs, TEAL, "closed"), (bugs, PURPLE, "open")]:
            axb.step(x, series, where="post", color=col, lw=2.6)
            axb.annotate(f"{series[-1]:.0f} {word}", (x[-1], series[-1]), xytext=(8, 0), textcoords="offset points",
                         ha="left", va="center", fontsize=10, color=col, fontweight="bold", annotation_clip=False)
        axb.set_xlim(first_bug, x[-1] + 0.25)
        axb.set_ylim(0, max(np.nanmax(bugs), np.nanmax(closed_bugs)) * 1.15)
        axb.set_ylabel("bugs")
        axb.set_title(f"Bugs, {', '.join(r.split('/')[-1] for r in cfg['issues'])}: open, and closed so far", loc="left", fontsize=11)
        axb.xaxis.set_major_locator(mdates.DayLocator(interval=2))
        axb.xaxis.set_major_formatter(mdates.DateFormatter("%-d %b"))

    # Release tags. Releases: diamond on the growth panel's baseline, a faint line up through
    # both panels and a name below. Other tags (alphas, RCs): a faint line or a small tick.
    rel = re.compile(cfg["release"])
    last_label_x, drop = -1e9, 0
    for name, d in tags:
        xd = mdates.date2num(d)
        if rel.search(name):
            drop = 14 if xd - last_label_x < span * 0.04 and not drop else 0
            last_label_x = xd
            for a in (ax, ax2):
                a.axvline(xd, color=RED, lw=0.9, ls=":", alpha=0.45)
            ax2.plot(xd, 0, "D", color=RED, ms=7, clip_on=False, zorder=6)
            ax2.annotate(re.sub(r"^v0\.1-", "", name.lower()), (xd, 0), xytext=(0, -26 - drop), textcoords="offset points",
                         ha="center", fontsize=8.5, fontweight="bold", color=RED)
            if axb is not None and xd >= first_bug:
                axb.axvline(xd, color=RED, lw=0.9, ls=":", alpha=0.45)
                axb.annotate(re.sub(r"^v0\.1-", "", name.lower()), (xd, 0.97), xycoords=("data", "axes fraction"), xytext=(4, 0),
                             textcoords="offset points", va="top", fontsize=8.5, fontweight="bold", color=RED)
        elif cfg["minor"] == "line":
            for a in (ax, ax2):
                a.axvline(xd, color=RED, lw=0.9, ls=":", alpha=0.45)
            ax2.plot(xd, 0, "D", color=RED, ms=7, clip_on=False, zorder=6)
        else:
            ax2.plot(xd, 0, "|", color=RED, ms=7, mew=1, clip_on=False, zorder=6)
    ax.plot([], [], ":", color=RED, label="Release (named); alphas / RCs ticked")
    ax.legend(loc="upper left", frameon=False, fontsize=10)

    if cfg["xtick"] == "1y":
        ax2.xaxis.set_major_locator(mdates.YearLocator())
        ax2.xaxis.set_major_formatter(mdates.DateFormatter("%Y"))
    else:
        ax2.xaxis.set_major_locator(mdates.WeekdayLocator(byweekday=mdates.MO, interval=2))
        ax2.xaxis.set_major_formatter(mdates.DateFormatter("%-d %b"))
    for a in (ax, ax2) if axb is None else (ax, axb, ax2):
        for sp in ("top", "right"):
            a.spines[sp].set_visible(False)
        a.grid(axis="y", color="#e4e4e4", lw=0.8)
        a.set_axisbelow(True)
    ax2.set_xlim(x[0], x[-1] + (x[-1] - x[0]) * 0.05)
    fig.text(0.01, 0.005, f"Daily net lines on {', '.join(f'{r} ({ref})' for r, ref in cfg['repos'])}, {start:%-d %b %Y} – {end:%-d %b %Y}. "
             f"Excludes {', '.join(sorted(cfg['skip'] - {'node_modules'}))}/, test data and CSV/JSON.\n"
             f"Tests = anything under {'/, '.join(sorted(cfg['test_dirs'], key=str.lower))}/. "
             f"Growth rate is a Gaussian-smoothed daily rate (σ = {sigma:g} days)."
             + (f" Bugs: issues labelled bug on GitHub ({', '.join(cfg['issues'])}), counted at the end of each day." if bugs is not None else "")
             + ("\nReadiness: governance/artefacts/requirements.md with the live issues; a row is blocked when an open bug cites it "
                "(in its status or evidence, or by a Requirement: line). Detail: python3 tools/method/make_rtm.py." if ready is not None else ""),
             fontsize=8, color="#666")
    out = Path(args.out) if args.out else root / "build/history" / cfg["out"]
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150, bbox_inches="tight", facecolor="white")
    print(f"product {cum['product'][-1]:.0f}k  tests {cum['tests'][-1]:.0f}k  docs {cum['docs'][-1]:.0f}k  tools {cum['tools'][-1]:.0f}k")
    wk = int(sigma)
    print(f"last {wk} days product net: {daily['product'][-wk:].sum()/1000:.1f}k; window before: {daily['product'][-2*wk:-wk].sum()/1000:.1f}k")
    if ready is not None:
        rt, rows = ready[0], ready[1]
        for col in ("v1 Must", "v1 Should", "v1 Could", "v2"):
            cr = [r for r in rows if rt.column(r) == col and r["status"] != "won't have"]
            ok = sum(r["status"] == "done" and not r.get("open_bugs") for r in cr)
            print(f"readiness {col}: {ok}/{len(cr)} ready ({100 * ok / max(len(cr), 1):.0f}%)")
    if bugs is not None:
        print("open bugs, last 8 days: " + "  ".join(f"{days[i]:%-d %b} {bugs[i]:.0f}" for i in range(-8, 0)))
        print("closed bugs, last 8 days: " + "  ".join(f"{days[i]:%-d %b} {closed_bugs[i]:.0f}" for i in range(-8, 0)))
    print(out)
    if args.open:
        subprocess.run(["open", str(out)])


if __name__ == "__main__":
    main()
