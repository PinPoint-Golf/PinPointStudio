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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--project", choices=sorted(PRESETS), default="pinpoint")
    ap.add_argument("--projects", default=str(root.parent), help="directory holding the repos")
    ap.add_argument("--out", help="PNG to write (default build/history/<preset's name>)")
    ap.add_argument("--csv", help="also write the daily cumulative series here")
    ap.add_argument("--open", action="store_true", help="open the PNG when done (macOS)")
    ap.add_argument("--sigma", type=float, help="smoothing of the growth rate, in days (default per preset)")
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

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["date", *cats])
            for i, d in enumerate(days):
                w.writerow([d.isoformat(), *(round(cum[c][i] * 1000) for c in cats)])

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

    BLUE, ORANGE, GREEN, GOLD, RED = "#4c6fcf", "#c8754a", "#5aa27c", "#d4a72c", "#b03a48"
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(14, 10), gridspec_kw={"height_ratios": [3, 1], "hspace": 0.12}, sharex=True)
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
    ax.set_title(cfg["title"], loc="left", fontsize=15)

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
    for a in (ax, ax2):
        for sp in ("top", "right"):
            a.spines[sp].set_visible(False)
        a.grid(axis="y", color="#e4e4e4", lw=0.8)
        a.set_axisbelow(True)
    ax2.set_xlim(x[0], x[-1] + (x[-1] - x[0]) * 0.05)
    fig.text(0.01, 0.005, f"Daily net lines on {', '.join(f'{r} ({ref})' for r, ref in cfg['repos'])}, {start:%-d %b %Y} – {end:%-d %b %Y}. "
             f"Excludes {', '.join(sorted(cfg['skip'] - {'node_modules'}))}/, test data and CSV/JSON.\n"
             f"Tests = anything under {'/, '.join(sorted(cfg['test_dirs'], key=str.lower))}/. "
             f"Growth rate is a Gaussian-smoothed daily rate (σ = {sigma:g} days).",
             fontsize=8, color="#666")
    out = Path(args.out) if args.out else root / "build/history" / cfg["out"]
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150, bbox_inches="tight", facecolor="white")
    print(f"product {cum['product'][-1]:.0f}k  tests {cum['tests'][-1]:.0f}k  docs {cum['docs'][-1]:.0f}k  tools {cum['tools'][-1]:.0f}k")
    wk = int(sigma)
    print(f"last {wk} days product net: {daily['product'][-wk:].sum()/1000:.1f}k; window before: {daily['product'][-2*wk:-wk].sum()/1000:.1f}k")
    print(out)
    if args.open:
        subprocess.run(["open", str(out)])


if __name__ == "__main__":
    main()
