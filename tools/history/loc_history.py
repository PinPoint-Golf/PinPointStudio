#!/usr/bin/env python3
"""Lines of code over time across the six PinPoint repos, with feature milestones.

Walks `git log --numstat` on main in every repo, keeps a running line count per
category (product / tests / docs / tools), and draws the cumulative curves with
a smoothed weekly growth rate underneath. Release tags are marked on the axis,
so a plateau before a release (or its absence) is easy to see.

    python3 tools/history/loc_history.py                 # -> build/history/loc_history.png
    python3 tools/history/loc_history.py --open          # ... and open it
    python3 tools/history/loc_history.py --csv out.csv   # also dump the daily series

Needs numpy, scipy and matplotlib. Repos are found next to the PinPointStudio
checkout (override with --projects).

Counted: product = C/C++/ObjC++/QML/Swift/shaders/CMake; tests = anything under a
tests/ or Tests/ directory; docs = Markdown; tools = py/sh/cmd/ps1/js scripts.
Excluded: third_party/, build/, Mothballed/, CSV/JSON/images and other data.
Lines are net (added minus deleted), so a move between folders is neutral.
"""
import argparse
import csv
import datetime as dt
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter1d

REPOS = ["PinPointStudio", "PinPointCapture", "libppcp", "libwrist", "libgspro", "libppswing"]

CODE_EXT = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".mm", ".m", ".qml", ".swift",
            ".metal", ".glsl", ".frag", ".vert", ".cmake"}
SCRIPT_EXT = {".py", ".sh", ".cmd", ".bat", ".ps1", ".js"}
SKIP_DIRS = {"third_party", "build", "Mothballed", "node_modules"}

# (label, date, text y in k lines, text x shift in days). Dates are hand-picked, when the feature first worked end
# to end; subject-line matching proved unreliable. Text y is where the label sits on the
# product-code axis (a leader line runs to the curve). Add new milestones at the bottom.
MILESTONES = [
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
]


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                          check=True, errors="replace").stdout


def categorise(path):
    parts = path.split("/")
    if SKIP_DIRS & set(parts[:-1]):
        return None
    ext = os.path.splitext(path)[1].lower()
    is_test = any(p in ("tests", "Tests") for p in parts[:-1])
    if ext == ".md":
        return "docs"
    if ext in CODE_EXT or ext in SCRIPT_EXT or parts[-1] == "CMakeLists.txt":
        if is_test:
            return "tests"
        if ext in SCRIPT_EXT:
            return "tools"
        return "product"
    return None


def read_repo(repo):
    """Return (per-day net line deltas {date: {cat: n}}, [(date, subject)])."""
    out = git(repo, "log", "main", "--no-renames", "--numstat", "--reverse",
              "--format=@@%cs\t%s")
    deltas = defaultdict(lambda: defaultdict(int))
    subjects, day = [], None
    for line in out.splitlines():
        if line.startswith("@@"):
            day, subject = line[2:].split("\t", 1)
            subjects.append((day, subject))
        elif line.strip() and day:
            add, rem, path = line.split("\t", 2)
            cat = categorise(path)
            if cat and add != "-":
                deltas[day][cat] += int(add) - int(rem)
    return deltas, subjects


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--projects", default=str(root.parent), help="directory holding the repos")
    ap.add_argument("--out", default=str(root / "build/history/loc_history.png"))
    ap.add_argument("--csv", help="also write the daily cumulative series here")
    ap.add_argument("--open", action="store_true", help="open the PNG when done (macOS)")
    ap.add_argument("--sigma", type=float, default=7.0, help="smoothing of the growth rate, in days")
    args = ap.parse_args()
    projects = Path(args.projects)

    deltas, subjects = {}, {}
    for r in REPOS:
        if not (projects / r / ".git").exists():
            sys.exit(f"missing repo {projects / r}")
        deltas[r], subjects[r] = read_repo(projects / r)

    cats = ["product", "tests", "docs", "tools"]
    start = min(dt.date.fromisoformat(d) for r in REPOS for d in deltas[r])
    end = dt.date.today()
    days = [start + dt.timedelta(n) for n in range((end - start).days + 1)]
    daily = {c: np.zeros(len(days)) for c in cats}
    for r in REPOS:
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
    norm = gaussian_filter1d(np.ones(len(days)), args.sigma, mode="constant")
    weekly = gaussian_filter1d(rate, args.sigma, mode="constant") / norm * 7 / 1000

    # Release tags, from the app repo.
    tags = []
    for line in git(projects / "PinPointStudio", "for-each-ref", "refs/tags", "--format=%(refname:short)\t%(creatordate:short)").splitlines():
        name, d = line.split("\t")
        tags.append((name, dt.date.fromisoformat(d)))
    tags.sort(key=lambda t: t[1])

    resolved = [(label, dt.date.fromisoformat(d), ty, dx) for label, d, ty, dx in MILESTONES]

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    BLUE, ORANGE, GREEN, GOLD = "#4c6fcf", "#c8754a", "#5aa27c", "#d4a72c"
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(14, 10), gridspec_kw={"height_ratios": [3, 1], "hspace": 0.12}, sharex=True)
    x = mdates.date2num(days)
    for c, col, lab in [("product", BLUE, "Product code (C++, QML, C, Swift)"), ("tests", ORANGE, "Tests"),
                        ("docs", GREEN, "Docs"), ("tools", GOLD, "Tools / scripts")]:
        ax.plot(x, cum[c], color=col, lw=2.6, label=lab)
        ax.annotate(f"{lab.split(' (')[0]}\n{cum[c][-1]:.0f}k", (x[-1], cum[c][-1]), xytext=(8, 0),
                    textcoords="offset points", va="center", fontsize=10, color="#333")
    for label, d, ty, dx in resolved:
        i = (d - start).days
        ax.plot(x[i], cum["product"][i], "o", mfc="white", mec=BLUE, mew=2, ms=7, zorder=5)
        ax.annotate(label, (x[i], cum["product"][i]), xytext=(x[i] + dx, ty), ha="center",
                    va="top" if ty < cum["product"][i] else "bottom", fontsize=8.5, color="#222",
                    arrowprops=dict(arrowstyle="-", color="#888", lw=0.8, shrinkA=2, shrinkB=4))
    ax.set_ylabel("Lines (thousands)")
    ax.set_ylim(0, max(cum["product"][-1] * 1.25, 100))
    ax.legend(loc="upper left", frameon=False, fontsize=10)
    ax.set_title(f"PinPoint: lines of code over time across all six repos, with the features as they arrived", loc="left", fontsize=15)

    ax2.fill_between(x, weekly, color="#d3dff7")
    solid = len(days) - 7
    ax2.plot(x[:solid + 1], weekly[:solid + 1], color="#3a52a8", lw=2.4)
    ax2.plot(x[solid:], weekly[solid:], color="#3a52a8", lw=2.4, ls="--")
    since = days.index(dt.date(2026, 7, 25))
    avg = np.maximum(daily["product"][since:], 0).sum() / ((len(days) - since) / 7) / 1000
    ax2.axhline(avg, color="#777", ls="--", lw=1)
    ax2.text(x[0] + 2, avg + 0.8, f"average since late July: {avg:.0f}k / week", color="#777", fontsize=9)
    ax2.set_ylabel("k lines / week")
    ax2.set_ylim(0, max(weekly.max() * 1.1, 10))

    # Release tags: a diamond on the growth panel's baseline, with a faint line up through both panels.
    for name, d in tags:
        xd = mdates.date2num(d)
        for a in (ax, ax2):
            a.axvline(xd, color="#b03a48", lw=0.9, ls=":", alpha=0.45)
        ax2.plot(xd, 0, "D", color="#b03a48", ms=7, clip_on=False, zorder=6)
        if "beta" in name:
            ax2.annotate(name.replace("v0.1-", ""), (xd, 0), xytext=(0, -26), textcoords="offset points",
                         ha="center", fontsize=8.5, fontweight="bold", color="#b03a48")
    ax.plot([], [], ":", color="#b03a48", label="Release tag (alphas unlabelled, betas named)")
    ax.legend(loc="upper left", frameon=False, fontsize=10)

    ax2.xaxis.set_major_locator(mdates.WeekdayLocator(byweekday=mdates.MO, interval=2))
    ax2.xaxis.set_major_formatter(mdates.DateFormatter("%-d %b"))
    for a in (ax, ax2):
        for s in ("top", "right"):
            a.spines[s].set_visible(False)
        a.grid(axis="y", color="#e4e4e4", lw=0.8)
        a.set_axisbelow(True)
    ax2.set_xlim(x[0], x[-1] + 22)
    fig.text(0.01, 0.005, f"Daily net lines on main in {', '.join(REPOS)}, {start:%-d %b} – {end:%-d %b %Y}. "
             "Excludes third_party/, build/, Mothballed/, test data and CSV/JSON.\n"
             f"Tests = anything under tests/. Growth rate is a Gaussian-smoothed daily rate (σ = {args.sigma:g} days).",
             fontsize=8, color="#666")
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150, bbox_inches="tight", facecolor="white")
    print(f"\nproduct {cum['product'][-1]:.0f}k  tests {cum['tests'][-1]:.0f}k  docs {cum['docs'][-1]:.0f}k  tools {cum['tools'][-1]:.0f}k")
    print(f"last 4 weeks product net: {[round(daily['product'][-7*(k+1):len(days) - 7*k].sum()/1000, 1) for k in range(4)]} k/week (newest first)")
    print(out)
    if args.open:
        subprocess.run(["open", str(out)])


if __name__ == "__main__":
    main()
