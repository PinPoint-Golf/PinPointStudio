#!/usr/bin/env python3
"""parity_baseline - take one parity baseline run under a label.

    python3 -I tools/parity/parity_baseline.py <label> [--half mac|analysis] [options]

Every run writes under <root>/<label>/, the parity scratch root being /mnt/swingdata/scratch/parity
on the Mac and C:\\PinPointStudio\\scratch\\parity on the cabin PC (fixed: any destination whose real
path, symlinks resolved, is not inside it is refused). The content goes in <label>/out/, which
parity_diff.py compares. A label is a name such as stageG-<sha>; a value with a path separator is
taken as the run folder itself, and must still be inside the root.
Design: build/refactor/step-G/BRIEF.md section 2.

--half mac (default; the Mac)
  1. Copies every session of the parity set (parity_set.json, from parity_select.py) from the
     library to <label>/lib/<Athlete>/<session> (a copy; the library is only read).
  2. regrade_ledger --tag parity --dump out/diagnostics.json <every session copy>
  3. athlete_dump --fresh --out out/athlete.json <every athlete folder of the copy>
     (that order: the catch-up trusts ledgers stamped current, so step 2 makes them this build's).
  4. Writes out/run_mac.json (commit, binaries, exit codes, seconds per step) and deletes the
     copies unless --keep-copies.
  Needs build/tests/Analysis/{regrade_ledger,athlete_dump} built at the commit under test, and
  sets the eight PINPOINT_CORE_* to the checkout's src/Resources/diagnostics/*.json.

--half analysis (the cabin PC, through run_analysis_golfsimpc.cmd; never a Mac sweep)
  Runs `swinglab_run <corpus>/swings/<s>/<w> --out <label>/analysis/<id> --doc-blocks
  --pose <pinned pose> [--dtl-pose <pinned DTL pose>] [--impact-us N]` over the set's 61 swings,
  so no pose is re-inferred (a swinglab_run older than --doc-blocks refuses the option), then
  writes out/analysis.json (the digest below) and out/run_analysis.json, and
  deletes the result trees unless --keep-results. --digest-only re-digests a kept tree.
  The digest per swing is result.json with the bulky or run-dependent parts taken out:
  the wall times (analysis.timings, skeleton3d.diagnostics.ms and .plane.branchMs) are dropped;
  analysis.pose2d/poseDtl keep their small keys and each per-frame array (frames, smoothed,
  synth) becomes {count, sha1}, compared exactly (the pose is the pinned input, so a changed
  hash means it was altered on the way through); paths become <corpus>/<swing>/<run>; and runmeta's verdict
  (ok, error, score, frames, impactUs, bindings, sessionType, calibrated) is kept as "run".
  --doc-blocks adds the swing document's `summary` block and this pass's `imuIntegrity` /
  `captureIntegrity` verdicts, as a write-back would store them; they are compared like the rest.
  A swing FAILS (and the run exits 1) when it has no result.json, a non-zero exit, or a result.json
  with no `summary` (the --doc-blocks write failed); its runner log is kept as
  out/logs/<id>.runner.log. `--self-test` checks that rule on built-in cases.

Exit 0 when every tool exited 0; 1 when one did not (the outputs are still written); 2 on a
refusal (a used label, a destination outside the parity scratch root, a missing binary).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import parity_common as pc  # noqa: E402

CORE = ["PACK:core", "NORMS:norms", "FLIGHT_NORMS:flight_norms", "CONTEXTS:contexts", "SCREENS:screens",
        "DRILLS:drills", "GATE_WORDS:gate_words", "REFERENCES:references"]
# Wall-clock fields inside the analysis block (they differ run to run on the same input; found by
# two runs of one swing, 14 Nov 2026): the stage timings, and skeleton3d's solve and branch times
# (std::chrono in src/Skeleton3d, written by skeleton3d_json.h).
WALL_TIMES = (("timings",), ("skeleton3d", "diagnostics", "ms"), ("skeleton3d", "diagnostics", "plane", "branchMs"))
RUNMETA_KEEP = ("ok", "error", "score", "frames", "impactUs", "bindings", "sessionType", "calibrated")


def log(msg):
    print(msg, flush=True)


def run_tool(cmd, logfile, env=None, timeout=3600):
    """Run one tool with a watchdog; its output goes to logfile. Returns (exit code, seconds)."""
    t0 = time.time()
    with open(logfile, "w", encoding="utf-8", errors="replace") as lf:
        lf.write("$ " + " ".join(cmd) + "\n")
        lf.flush()
        try:
            rc = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT, env=env, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            lf.write("\nparity: killed after %d s\n" % timeout)
            rc = -9
    return rc, round(time.time() - t0, 1)


def binary_info(path):
    st = os.stat(path)
    return {"path": os.path.relpath(path, pc.REPO) if path.startswith(pc.REPO) else path,
            "mtime": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(st.st_mtime)),
            "sha1": pc.sha1_file(path)}


def provenance():
    sha, dirty = pc.git_head()
    return {"commit": sha, "changedPaths": dirty, "host": os.uname().nodename if hasattr(os, "uname")
            else os.environ.get("COMPUTERNAME"), "paritySetSha1": None}


# ---------------------------------------------------------------- the Mac half

def mac_half(a, pset, run_dir):
    out = os.path.join(run_dir, "out")
    lib = os.path.join(run_dir, "lib")
    for f in ("diagnostics.json", "athlete.json", "run_mac.json"):
        if os.path.exists(os.path.join(out, f)):
            log("refusing: %s already holds a Mac half (%s); use a new label" % (run_dir, f))
            return 2
    if os.path.exists(lib):
        log("refusing: %s exists (a half-finished run?); delete it or use a new label" % lib)
        return 2
    bins = {n: os.path.join(a.bin_dir, n) for n in ("regrade_ledger", "athlete_dump")}
    for n, p in bins.items():
        if not os.path.isfile(p):
            log("refusing: %s is not built (%s)" % (n, p))
            return 2
    os.makedirs(os.path.join(out, "logs"), exist_ok=True)

    meta = provenance()
    meta["paritySetSha1"] = pc.sha1_file(a.set)
    meta["binaries"] = {n: binary_info(p) for n, p in bins.items()}
    meta["seconds"] = {}

    # 1. copy
    t0 = time.time()
    sessions, athletes, copy_s = [], [], {}
    for s in pset["sessions"]:
        src = os.path.join(a.library, *s["path"].split("/"))
        dst = os.path.join(lib, *s["path"].split("/"))
        ts = time.time()
        shutil.copytree(src, dst, symlinks=True)
        copy_s[s["path"]] = round(time.time() - ts, 1)
        sessions.append(dst)
        ad = os.path.dirname(dst)
        if ad not in athletes:
            athletes.append(ad)
        log("copied  %-55s %5.1f s" % (s["path"], copy_s[s["path"]]))
    meta["seconds"]["copy"] = round(time.time() - t0, 1)
    meta["seconds"]["copyPerSession"] = copy_s

    env = dict(os.environ)
    res = os.path.join(pc.REPO, "src", "Resources", "diagnostics")
    for kv in CORE:
        k, f = kv.split(":")
        env["PINPOINT_CORE_" + k] = os.path.join(res, f + ".json")

    # 2. the session level
    rc1, s1 = run_tool([bins["regrade_ledger"], "--tag", "parity", "--dump",
                        os.path.join(out, "diagnostics.json")] + sessions,
                       os.path.join(out, "logs", "regrade_ledger.log"), env, a.timeout)
    log("regrade_ledger --dump   exit %d  %6.1f s" % (rc1, s1))
    # 3. the athlete level
    rc2, s2 = run_tool([bins["athlete_dump"], "--fresh", "--out", os.path.join(out, "athlete.json")]
                       + athletes, os.path.join(out, "logs", "athlete_dump.log"), env, a.timeout)
    log("athlete_dump --fresh    exit %d  %6.1f s" % (rc2, s2))
    meta["seconds"]["regradeLedgerDump"] = s1
    meta["seconds"]["athleteDump"] = s2
    meta["exit"] = {"regrade_ledger": rc1, "athlete_dump": rc2}
    meta["sessions"] = [s["path"] for s in pset["sessions"]]

    # 4. tidy
    if not a.keep_copies:
        shutil.rmtree(lib)
        log("deleted the session copies (%s)" % lib)
    meta["copiesKept"] = bool(a.keep_copies)
    pc.write_canonical(os.path.join(out, "run_mac.json"), meta)
    log("wrote %s" % out)
    return 0 if rc1 == 0 and rc2 == 0 else 1


# ---------------------------------------------------------------- the analysis half

def _hash_json(obj):
    return hashlib.sha1(json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def _tokenise(obj, subs):
    if isinstance(obj, dict):
        return {k: _tokenise(v, subs) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_tokenise(v, subs) for v in obj]
    if isinstance(obj, str):
        for raw, tok in subs:
            if raw and raw in obj:
                obj = obj.replace(raw, tok)
        return obj
    return obj


def digest_result(result, runmeta, swing_dir, run_dir, corpus):
    """One swing's analysis digest (see the module header)."""
    d = {}
    for k, v in result.items():
        if k in ("source",):        # the swing folder path and raw/mp4 - "frames" is kept from runmeta
            continue
        d[k] = v
    an = d.get("analysis")
    if isinstance(an, dict):
        an = json.loads(json.dumps(an))            # a private deep copy
        for path in WALL_TIMES:
            node = an
            for k in path[:-1]:
                node = node.get(k) if isinstance(node, dict) else None
            if isinstance(node, dict):
                node.pop(path[-1], None)
        for pk in ("pose2d", "poseDtl"):
            p = an.get(pk)
            if isinstance(p, dict):
                for k, v in list(p.items()):
                    if isinstance(v, (list, dict)):
                        p[k] = {"count": len(v), "sha1": _hash_json(v)}
        d["analysis"] = an
    d["run"] = {k: runmeta.get(k) for k in RUNMETA_KEEP} if runmeta is not None else None
    subs = []
    for raw, tok in ((run_dir, "<run>"), (swing_dir, "<swing>"), (corpus, "<corpus>")):
        for form in {raw, raw.replace("\\", "/"), raw.replace("/", "\\")}:
            subs.append((form, tok))
    subs.sort(key=lambda s: -len(s[0]))
    return _tokenise(d, subs)


def swing_failure(entry):
    """Why one swing's analysis entry counts as FAILED, or None when it is good. --doc-blocks is
    always passed, and result.json exists only when the analysis produced a detail, which the
    production writer always gives a summary: so a result with no `summary` means the doc-blocks
    write failed (swinglab_run only warns on stderr and exits 0), and must not pass silently in
    the baseline and every step run alike."""
    if entry.get("result") is None:
        return entry.get("why") or "no result.json"
    if entry.get("exitCode") not in (0, None):
        return "swinglab_run exited %s" % entry["exitCode"]
    if "summary" not in entry["result"]:
        return "result.json has no summary: the --doc-blocks document write failed (see the runner log)"
    return None


def self_test():
    """swing_failure on built-in cases; exit 0 when all hold."""
    good = {"summary": {}, "analysis": {}}
    cases = [
        ("good swing", {"result": good, "exitCode": 0}, False),
        ("good, re-digested (no exit code)", {"result": good, "exitCode": None}, False),
        ("no result.json", {"result": None, "exitCode": 1, "why": "no result.json"}, True),
        ("non-zero exit", {"result": good, "exitCode": 1}, True),
        ("killed by the watchdog", {"result": good, "exitCode": -9}, True),
        ("result without summary", {"result": {"analysis": {}}, "exitCode": 0}, True),
    ]
    bad = 0
    for name, entry, want in cases:
        got = swing_failure(entry) is not None
        bad += got != want
        print("%-4s %-34s expected %s, got %s" % ("ok" if got == want else "FAIL", name,
                                                   "FAILED" if want else "ok", "FAILED" if got else "ok"))
    print("self-test: %s" % ("all %d cases hold" % len(cases) if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


def analysis_half(a, pset, run_dir):
    out = os.path.join(run_dir, "out")
    trees = os.path.join(run_dir, "analysis")
    swings = pset["swings"]
    if a.only:
        swings = [w for w in swings if w["id"] in set(a.only)]
        if len(swings) != len(set(a.only)):
            log("refusing: --only names a swing not in the set")
            return 2
    if not a.digest_only:
        if os.path.exists(os.path.join(out, "analysis.json")) or os.path.exists(trees):
            log("refusing: %s already holds an analysis half; use a new label" % run_dir)
            return 2
        if not a.swinglab or not os.path.isfile(a.swinglab):
            log("refusing: --swinglab must name the swinglab_run binary (got %r)" % a.swinglab)
            return 2
    os.makedirs(out, exist_ok=True)
    meta_path = os.path.join(out, "run_analysis.json")
    if a.digest_only:
        if not os.path.isdir(trees):
            log("refusing: --digest-only needs a kept result tree (%s)" % trees)
            return 2
        # Re-digesting keeps the run's own record (commit, binary, exit codes, seconds).
        meta = pc.read_json(meta_path) if os.path.exists(meta_path) else {"exit": {}}
    else:
        meta = provenance()
        meta["paritySetSha1"] = pc.sha1_file(a.set)
        meta["binaries"] = {"swinglab_run": binary_info(a.swinglab)}
        meta["perSwingSeconds"] = {}
        meta["exit"] = {}
    t_all = time.time()
    for i, w in enumerate(swings):
        sd = os.path.join(a.corpus, *w["swing"].split("/"))
        rd = os.path.join(trees, w["id"])
        if a.digest_only:
            continue
        os.makedirs(rd)
        cmd = [a.swinglab, sd, "--out", rd, "--doc-blocks",
               "--pose", os.path.join(a.corpus, *w["pose"].split("/"))]
        if w.get("dtlPose"):
            cmd += ["--dtl-pose", os.path.join(a.corpus, *w["dtlPose"].split("/"))]
        if w.get("impactUs") is not None:
            cmd += ["--impact-us", str(int(w["impactUs"]))]
        rc, secs = run_tool(cmd, os.path.join(rd, "runner.log"), None, a.timeout)
        meta["perSwingSeconds"][w["id"]] = secs
        meta["exit"][w["id"]] = rc
        log("[%2d/%d] %-4s %-48s %5.1f s" % (i + 1, len(swings), "ok" if rc == 0 else "FAIL", w["id"], secs))
    if not a.digest_only:
        meta["seconds"] = round(time.time() - t_all, 1)

    digests = []
    for w in swings:
        sd = os.path.join(a.corpus, *w["swing"].split("/"))
        rd = os.path.join(trees, w["id"])
        rp, mp = os.path.join(rd, "result.json"), os.path.join(rd, "runmeta.json")
        entry = {"swing": w["id"], "exitCode": meta["exit"].get(w["id"])}
        if os.path.exists(rp):
            entry["result"] = digest_result(pc.read_json(rp), pc.read_json(mp) if os.path.exists(mp) else None,
                                            sd, rd, a.corpus)
        else:
            entry["result"] = None
            entry["why"] = "no result.json (see %s)" % os.path.join(rd, "runner.log")
        digests.append(entry)
    digests.sort(key=lambda e: e["swing"])
    failed = []
    for e in digests:
        why = swing_failure(e)
        if why is None:
            continue
        failed.append(e["swing"])
        e["failed"] = why
        # Keep the failed swing's runner log: the result trees are deleted below.
        src = os.path.join(trees, e["swing"], "runner.log")
        if os.path.exists(src):
            os.makedirs(os.path.join(out, "logs"), exist_ok=True)
            shutil.copyfile(src, os.path.join(out, "logs", e["swing"] + ".runner.log"))
        log("FAILED %s: %s" % (e["swing"], why))
    pc.write_canonical(os.path.join(out, "analysis.json"),
                       {"tool": "parity_baseline --half analysis", "digestVersion": 1, "swings": digests})
    pc.write_canonical(meta_path, meta)
    log("analysis: %d swings, %d failed, %.0f s; wrote %s" % (len(digests), len(failed), meta.get("seconds", 0), out))
    if not a.keep_results and not a.digest_only:
        shutil.rmtree(trees)
        log("deleted the result trees (%s)" % trees)
    return 1 if failed else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("label", help="run name (e.g. stageG-<sha>) or a run folder path")
    ap.add_argument("--half", choices=("mac", "analysis"), default="mac")
    ap.add_argument("--set", default=pc.DEFAULT_SET, help="the parity set (default %(default)s)")
    ap.add_argument("--library", default=pc.LIBRARY, help="library root, read only (default %(default)s)")
    ap.add_argument("--corpus", default=pc.CORPUS, help="corpus root, read only (default %(default)s)")
    ap.add_argument("--bin-dir", default=os.path.join(pc.REPO, "build", "tests", "Analysis"),
                    help="mac: where regrade_ledger and athlete_dump are (default %(default)s)")
    ap.add_argument("--keep-copies", action="store_true", help="mac: keep <label>/lib after the dumps")
    ap.add_argument("--swinglab", help="analysis: the swinglab_run binary")
    ap.add_argument("--only", nargs="+", metavar="ID", help="analysis: just these swing ids (a smoke test)")
    ap.add_argument("--keep-results", action="store_true", help="analysis: keep <label>/analysis/")
    ap.add_argument("--digest-only", action="store_true", help="analysis: re-digest a kept tree")
    ap.add_argument("--self-test", action="store_true", help="check the swing-failure rule and exit")
    ap.add_argument("--timeout", type=int, default=1800, help="seconds one tool run may take (default %(default)s)")
    a = ap.parse_args()

    run_dir = pc.require_scratch(pc.label_dir(a.label))
    pset = pc.read_json(a.set)
    os.makedirs(run_dir, exist_ok=True)
    return mac_half(a, pset, run_dir) if a.half == "mac" else analysis_half(a, pset, run_dir)


if __name__ == "__main__":
    sys.exit(main())
