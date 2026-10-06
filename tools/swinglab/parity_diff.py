#!/usr/bin/env python3
"""parity_diff.py -- byte-identical soak gate for analyzer refactors.

Pairs every `result.json` found (recursively, any depth) under two swinglab
run roots by their path relative to the root, and diffs each pair. Excluded --
BY DESIGN, because they are wall-clock readings that legitimately differ
between two runs of the SAME deterministic pipeline, not because they are
expected to match:

  * analysis.timings, wherever an "analysis" object holds one (the top-level
    document's, and any nested analysis block): per-stage milliseconds, and
    since 6 Oct 2026 analysis.timings.stages -- the per-stage start/end/thread
    timeline (analysis_dag_design.md step A), which a parallel run
    (analysis.parallel) changes by construction;
  * analysis.skeleton3d.diagnostics.ms and
    analysis.skeleton3d.diagnostics.plane.branchMs: the solver's own wall time
    and its plane-branch wall time (skeleton3d_json.h). They are the only other
    timer-sourced fields in the document (every other "...Ms" is a swing-clock
    duration and stays compared).

Everything else -- the whole result.json document, not just "analysis" -- must
compare byte-identical.

Usage:
    parity_diff.py RUN_ROOT_A RUN_ROOT_B [--file result.json]

Exit 0 iff every pair compared equal and nothing was unpaired.
"""

import argparse
import json
import sys
from pathlib import Path


def find_files(root, name):
    """Paths of every `name` file under root, relative to root, any depth."""
    root = Path(root)
    return {p.relative_to(root) for p in root.rglob(name)}


# Wall-clock fields inside analysis.skeleton3d, as key paths below it.
SKELETON3D_WALLCLOCK = (("diagnostics", "ms"), ("diagnostics", "plane", "branchMs"))


def _strip_path(obj, path):
    for k in path[:-1]:
        obj = obj.get(k) if isinstance(obj, dict) else None
        if obj is None:
            return
    if isinstance(obj, dict):
        obj.pop(path[-1], None)


def strip_timings(doc):
    """Delete, in place, every wall-clock field (see the module docstring) --
    nothing else -- and return doc: `timings` under any "analysis" object at
    any depth, and skeleton3d's solver ms / branchMs under it."""
    def walk(node):
        if isinstance(node, dict):
            analysis = node.get("analysis")
            if isinstance(analysis, dict):
                analysis.pop("timings", None)
                sk = analysis.get("skeleton3d")
                if isinstance(sk, dict):
                    for path in SKELETON3D_WALLCLOCK:
                        _strip_path(sk, path)
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(doc)
    return doc


def first_diff(a, b, path=""):
    """Path to the first differing value between two JSON-like structures
    ("" means equal), e.g. "analysis.series[3].samples[12].value"."""
    if type(a) is not type(b):
        return f"{path or '<root>'} (type {type(a).__name__} vs {type(b).__name__})"
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            sub = f"{path}.{k}" if path else k
            if k not in a:
                return f"{sub} (missing in A)"
            if k not in b:
                return f"{sub} (missing in B)"
            d = first_diff(a[k], b[k], sub)
            if d:
                return d
        return ""
    if isinstance(a, list):
        if len(a) != len(b):
            return f"{path or '<root>'}[] size {len(a)} vs {len(b)}"
        for i, (av, bv) in enumerate(zip(a, b)):
            d = first_diff(av, bv, f"{path}[{i}]")
            if d:
                return d
        return ""
    if a != b:
        return f"{path or '<root>'}: {a!r} != {b!r}"
    return ""


def compare_pair(path_a, path_b):
    """(equal, detail) for one result.json pair -- detail is the first-diff
    path when unequal, else empty."""
    with open(path_a, encoding="utf-8") as f:
        doc_a = strip_timings(json.load(f))
    with open(path_b, encoding="utf-8") as f:
        doc_b = strip_timings(json.load(f))
    if json.dumps(doc_a, sort_keys=True) == json.dumps(doc_b, sort_keys=True):
        return True, ""
    return False, first_diff(doc_a, doc_b) or "differs (no leaf path found)"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_root_a")
    ap.add_argument("run_root_b")
    ap.add_argument("--file", default="result.json",
                    help="filename to pair and compare (default: result.json)")
    args = ap.parse_args()

    root_a, root_b = Path(args.run_root_a), Path(args.run_root_b)
    files_a = find_files(root_a, args.file)
    files_b = find_files(root_b, args.file)

    n_pass = n_diff = n_unpaired = 0
    for rel in sorted(files_a | files_b):
        if rel not in files_a or rel not in files_b:
            print(f"UNPAIRED {rel}")
            n_unpaired += 1
            continue
        equal, detail = compare_pair(root_a / rel, root_b / rel)
        if equal:
            print(f"PASS {rel}")
            n_pass += 1
        else:
            print(f"DIFF {rel}: {detail}")
            n_diff += 1

    n_compared = n_pass + n_diff
    print(f"\n{n_compared} compared, {n_pass} pass, {n_diff} diff, {n_unpaired} unpaired")
    sys.exit(0 if (n_diff == 0 and n_unpaired == 0) else 1)


if __name__ == "__main__":
    main()
