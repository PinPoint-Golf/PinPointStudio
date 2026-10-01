#!/usr/bin/env python3
"""Run swinglab_run over a swing set, in parallel, one fresh --out dir per swing.

Used by the shaft-uncertainty calibration (docs/design/shaft_uncertainty_propagation_design.md).
Runs ON THE STUDIO PC (memory: corpus sweeps on GOLFSIMPC Release), with the venv python.

Usage:
  sweep.py --bin EXE --set SET.json --out ROOT [--params P.json] [--jobs 3]

SET.json is a list of {"id": "<session>__<swing>", "dir": swing dir, "pose": pinned pose json or null,
"dtlPose": pinned DTL pose json or null, "args": [extra swinglab_run args]}. Every swing is run with
--trace; a swing with dtlPose also gets --dtl --dtl-pose. The run dir is wiped first, because
swinglab_run's rename into an existing dir fails silently (memory: swinglab_run output files).
Writes ROOT/sweep_log.json with per-swing exit code and wall time.
"""
import argparse, json, os, shutil, subprocess, time
from concurrent.futures import ThreadPoolExecutor


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--set", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--params")
    ap.add_argument("--jobs", type=int, default=3)
    a = ap.parse_args()
    swings = json.load(open(a.set, encoding="utf-8"))
    os.makedirs(a.out, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = r"C:\Qt\6.11.0\msvc2022_64\bin;" + os.path.dirname(a.bin) + ";" + env.get("PATH", "")

    def one(s):
        rd = os.path.join(a.out, s["id"])
        shutil.rmtree(rd, ignore_errors=True)
        cmd = [a.bin, s["dir"], "--out", rd, "--trace"]
        if s.get("pose"): cmd += ["--pose", s["pose"]]
        if s.get("dtlPose"): cmd += ["--dtl", "--dtl-pose", s["dtlPose"]]
        if a.params: cmd += ["--params", a.params]
        cmd += s.get("args", [])
        t0 = time.time()
        p = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
        dt = time.time() - t0
        with open(rd + ".log", "w", encoding="utf-8") as f:
            f.write(p.stdout[-20000:] + "\n--- stderr ---\n" + p.stderr[-200000:])
        return {"id": s["id"], "rc": p.returncode, "wall": dt}

    t0 = time.time()
    with ThreadPoolExecutor(a.jobs) as ex:
        log = list(ex.map(one, swings))
    json.dump({"bin": a.bin, "params": a.params, "wall": time.time() - t0, "runs": log},
              open(os.path.join(a.out, "sweep_log.json"), "w"), indent=1)
    bad = [r for r in log if r["rc"] != 0]
    print(f"{len(log)} swings, {len(bad)} failed, {time.time()-t0:.0f} s")
    for r in bad: print("  FAIL", r["id"], r["rc"])


if __name__ == "__main__":
    main()
