# Corpus ingest, batch running, aggregate report, run diffing, and the
# mechanical parameter sweep (Tier 0 — no model involved).

import json
import os
import random
import shutil
import subprocess
import time
from pathlib import Path

from . import Swing, default_binary, git_sha, load_json, save_json
from .score import scorecard


def doctor():
    """Self-orientation for a session on ANY host (no local context needed):
       verifies the binary, python deps, and conventions; prints fixes."""
    import platform
    from . import default_binary
    ok = True
    print(f"host: {platform.node()} ({platform.system()})")
    b = default_binary()
    if b.exists():
        print(f"binary: {b} OK")
    else:
        ok = False
        print(f"binary: {b} MISSING")
        print("  fix: cmake -S <repo> -B <build> -DPINPOINT_BUILD_TOOLS=ON && "
              "cmake --build <build> --target swinglab_run")
        print("  or:  set SWINGLAB_BIN=/path/to/swinglab_run")
    dll = os.environ.get("SWINGLAB_DLL_PATH")
    if dll:
        print(f"dll path: {dll}")
    elif os.name == "nt":
        print("dll path: SWINGLAB_DLL_PATH not set — if runs fail with exit "
              "-1073741515 (DLL not found), setx SWINGLAB_DLL_PATH to the Qt "
              "bin;OpenCV bin dirs")
    for mod in ("numpy", "cv2", "matplotlib"):
        try:
            __import__(mod)
            print(f"python: {mod} OK")
        except ImportError:
            ok = False
            print(f"python: {mod} MISSING — pip install -r tools/swinglab/requirements.txt")
    print("conventions: corpus + runs live on the shared SwingData folder "
          "(Windows C:\\Users\\developer\\Data\\PinPointStudio == Linux "
          "/mnt/swingdata) so artifacts (scorecards, contact sheets, "
          "TRIAGE/ESCALATION.md) are visible from every host.")
    print("data trust: ONLY use corpus roots containing a CORPUS.md that "
          "states calibration provenance; ingest refuses to bless without it.")
    return ok


def ingest(corpus_root):
    """Build corpus.json: every dir containing a swing.json, with quick facts."""
    root = Path(corpus_root)
    swings = []
    for sj in sorted(root.rglob("swing.json")):
        d = sj.parent
        try:
            s = Swing(d)
        except Exception as e:
            print(f"[ingest] SKIP {d}: {e}")
            continue
        vids = s.video_streams()
        has_raw = any("raw" in v for v in vids)
        imus = [x for x in s.doc.get("streams", []) if x.get("kind") == "imu"]
        cap = s.capture()
        swings.append({
            "path": str(d), "name": s.name,
            "videos": len(vids), "raw": has_raw, "imus": len(imus),
            "impact": s.impact_us() is not None,
            "bindings": len(s.bindings()),
            "truth": (d / "truth.json").exists(),
            # Markup capture conditions (scope/tempo/contact/club/shaft/lighting);
            # None when unlabelled. Filter/stratify the corpus on these (e.g. run
            # only scope=="full", or segment results by tempo).
            "conditions": s.truth_meta() or None,
            # Capture provenance (None on legacy swings lacking the fields) —
            # filter the corpus on these before tuning.
            "sessionType": cap.get("sessionType"),
            "shotSource": cap.get("shotSource"),
            "calibrated": s.calibrated(),
            "calibAgeSec": s.calib_age_sec(),
            "perspectives": [v.get("setup", {}).get("perspectiveName")
                             for v in vids] if any("setup" in v for v in vids) else None,
            # Ball-detection calibration provenance (None on legacy swings):
            # True when ANY stream captured with the environment-calibrated
            # detector; worst (lowest) margin across streams for filtering.
            "ballCalibrated": (any(v.get("setup", {}).get("ballDetection", {}).get("calibrated")
                                   for v in vids)
                               if any("ballDetection" in v.get("setup", {}) for v in vids)
                               else None),
            "ballMargin": (min((v["setup"]["ballDetection"].get("margin", 0.0)
                                for v in vids
                                if v.get("setup", {}).get("ballDetection", {}).get("calibrated")),
                               default=None)
                           if any("ballDetection" in v.get("setup", {}) for v in vids)
                           else None),
            "appVersion": cap.get("host", {}).get("version"),
        })
    notes_file = root / "CORPUS.md"
    manifest = {"root": str(root), "count": len(swings), "swings": swings,
                "notes": notes_file.read_text(encoding="utf-8") if notes_file.exists() else None,
                "blessed": notes_file.exists()}
    if not notes_file.exists():
        print("[ingest] WARNING: no CORPUS.md — corpus is UNBLESSED (write one "
              "stating recording date, calibration state, and reliability)")
    save_json(root / "corpus.json", manifest)
    print(f"[ingest] {len(swings)} swings -> {root / 'corpus.json'}")
    return manifest


def run_one(swing_dir, run_dir, params=None, trace=True, pose=None, binary=None):
    """Invoke swinglab_run for one swing; returns (ok, run_dir)."""
    swing_dir, run_dir = Path(swing_dir), Path(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(binary or default_binary()), str(swing_dir), "--out", str(run_dir)]
    if params:
        cmd += ["--params", str(params)]
    if trace:
        cmd += ["--trace"]
    pose_file = Path(pose) if pose else swing_dir / "pose.json"
    if pose_file.exists():
        cmd += ["--pose", str(pose_file)]
    # Windows: the runner's DLLs (Qt, OpenCV) are not on the service PATH —
    # SWINGLAB_DLL_PATH (set once per host via setx) is prepended for the child.
    env = os.environ.copy()
    extra = env.get("SWINGLAB_DLL_PATH")
    if extra:
        env["PATH"] = extra + os.pathsep + env.get("PATH", "")
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    (run_dir / "runner.log").write_text(r.stdout + r.stderr, encoding="utf-8")
    return r.returncode == 0, run_dir


def run_corpus(corpus_root, runs_root, run_id=None, params=None, trace=True):
    """Run + score every swing in the corpus; returns the run summary path."""
    manifest = load_json(Path(corpus_root) / "corpus.json")
    run_id = run_id or time.strftime("%Y%m%d-%H%M%S")
    out_root = Path(runs_root) / run_id
    cards = []
    for sw in manifest["swings"]:
        rd = out_root / sw["name"]
        ok, _ = run_one(sw["path"], rd, params=params, trace=trace)
        card = scorecard(rd, sw["path"]) if (rd / "runmeta.json").exists() else {
            "swing": sw["name"], "score": 0, "ok": False, "failures": ["runner_crashed"]}
        card["runner_ok"] = ok
        save_json(rd / "scorecard.json", card)
        cards.append(card)
        print(f"[run] {sw['name']}: score {card['score']} "
              f"({'ok' if ok else 'RUNNER FAIL'}) fails={card.get('failures')}")
    summary = {
        "run_id": run_id, "sha": git_sha(),
        "params": str(params) if params else None,
        "mean_score": round(sum(c["score"] for c in cards) / max(len(cards), 1), 1),
        "swings": cards,
    }
    save_json(out_root / "summary.json", summary)
    report(out_root)
    return out_root


def report(run_root):
    """Aggregate REPORT.md for one run."""
    run_root = Path(run_root)
    s = load_json(run_root / "summary.json")
    lines = [f"# SwingLab run {s['run_id']}",
             f"- sha `{s['sha']}` · params `{s['params']}` · mean score **{s['mean_score']}**",
             "",
             "| swing | score | ok | failures | warnings |",
             "|---|---|---|---|---|"]
    for c in sorted(s["swings"], key=lambda c: c["score"]):
        lines.append(f"| {c['swing']} | {c['score']} | {c.get('ok')} | "
                     f"{', '.join(c.get('failures', [])) or '—'} | "
                     f"{', '.join(c.get('warnings', [])) or '—'} |")
    (run_root / "REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[report] {run_root / 'REPORT.md'} (mean {s['mean_score']})")


def diff(run_a, run_b, regression_pts=5):
    """Per-swing regression diff between two runs. Returns count of regressions."""
    a = load_json(Path(run_a) / "summary.json")
    b = load_json(Path(run_b) / "summary.json")
    by_a = {c["swing"]: c for c in a["swings"]}
    regressions = 0
    lines = [f"# diff {a['run_id']} -> {b['run_id']}"]
    for c in b["swings"]:
        old = by_a.get(c["swing"])
        if not old:
            continue
        delta = c["score"] - old["score"]
        mark = ""
        if delta <= -regression_pts:
            mark = " ⬇ REGRESSION"
            regressions += 1
        elif delta >= regression_pts:
            mark = " ⬆ improved"
        if mark or delta:
            lines.append(f"- {c['swing']}: {old['score']} -> {c['score']}{mark}")
    out = "\n".join(lines) + f"\n\nregressions: {regressions}\n"
    (Path(run_b) / "DIFF.md").write_text(out, encoding="utf-8")
    print(out)
    return regressions


def sweep(corpus_root, runs_root, space_file, trials=20, seed=1):
    """Tier-0 random search over a parameter space:
       space.json = {"shaft.ridgeKernelPx": [5, 15, "int"], "assembly.coverageMin": [0.4, 0.8]}
       Objective = mean scorecard score. Keeps the best params + full history."""
    space = load_json(space_file)
    rng = random.Random(seed)
    history, best = [], None
    for k in range(trials):
        params = {}
        for key, spec in space.items():
            lo, hi = spec[0], spec[1]
            v = rng.uniform(lo, hi)
            if len(spec) > 2 and spec[2] == "int":
                v = int(round(v))
            params[key] = v
        pfile = Path(runs_root) / f"sweep-params-{k:03d}.json"
        save_json(pfile, params)
        out = run_corpus(corpus_root, runs_root, run_id=f"sweep-{k:03d}",
                         params=pfile, trace=False)
        mean = load_json(out / "summary.json")["mean_score"]
        history.append({"trial": k, "params": params, "mean_score": mean})
        if best is None or mean > best["mean_score"]:
            best = history[-1]
        print(f"[sweep] trial {k}: {mean} (best {best['mean_score']})")
    save_json(Path(runs_root) / "sweep-result.json",
              {"space": space, "trials": history, "best": best})
    print(f"[sweep] BEST mean={best['mean_score']} params={best['params']}")
    return best
