#!/usr/bin/env python3
"""parity_diff - compare two parity baseline runs and list every difference.

    python3 -I tools/parity/parity_diff.py <labelA> <labelB> [--rtol 1e-9] [--atol 1e-12]
                                           [--out differences.md] [--max-per-group 40]
    python3 -I tools/parity/parity_diff.py --self-test

A label names <scratch>/<label>/out (scratch default /mnt/swingdata/scratch/parity); a value with
a path separator is taken as the run folder (or its out/ folder) itself. The three content files
are compared - analysis.json, diagnostics.json, athlete.json; a file in one run and not the other
is a difference, a file in neither is noted and skipped - but when no file is compared at all
(both runs empty, or a file unreadable or empty) the tool stops with exit 2, so a run that never
wrote its outputs cannot pass. run_*.json (commit, timings) is shown in the header and never compared.

How values are judged (build/refactor/step-G/BRIEF.md section 2):
  whole numbers    exact when both are whole (a JSON integer, or a float with no fraction): a
                   count, an id, an epoch-ms or -us timestamp; 1 and 1.0 are equal
  other numbers    equal when |a-b| <= max(rtol * max(|a|,|b|), atol); rtol 1e-9, atol 1e-12
  inf, nan         never within a tolerance: inf equals only the same-signed inf, NaN only NaN
  strings, bools,  exact (a state, a label, a display string); a type change (null to a number,
  null, types      say) is a difference
  objects          a key in one run only is reported as missing or extra
  arrays           of objects that carry an identity (athlete, session, swing, shotId,
                   conditionId, id, key, ...; unique in both runs) are matched by it, so one
                   inserted shot does not shift every row after it; an order change of the same
                   ids is reported once. Other arrays are compared by position, and a length
                   change is reported.

Output: a Markdown "differences" list - first by field (the path with identities as [*]: how
many, the largest relative change, one example), then by swing or session - each with an empty
"explained" box for the reader. Printed, and written to --out when given. Exit 0 when the runs
agree, 1 when there is any difference, 2 on a usage error or when nothing was compared.
--self-test checks these rules on built-in cases (exit 0 when all hold).
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import parity_common as pc  # noqa: E402

# Identity keys, most specific first; composite keys for lists no single key identifies.
ID_KEYS = ["athlete", "session", "swing", "shotId", "conditionId", "id", "key", "measureId",
           "gate", "clubId", "club", "phase", "p"]
ID_COMBOS = [("gate", "subject"), ("measureId", "signalId"), ("phase", "segment")]
UNIT_KEYS = ("athlete", "session", "swing")   # what a "swing / session" group is made of


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def whole(v):
    return isinstance(v, int) or (isinstance(v, float) and math.isfinite(v) and v.is_integer())


def numbers_equal(a, b, rtol, atol):
    """(equal, relative change) under the rules in the module header."""
    if a == b:                                   # also 1 == 1.0, and inf == inf of one sign
        return True, None
    if not (math.isfinite(a) and math.isfinite(b)):
        both_nan = isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b)
        return both_nan, None if both_nan else math.inf
    d, scale = abs(a - b), max(abs(a), abs(b))
    rel = d / scale if scale else math.inf
    if whole(a) and whole(b):                    # counts, ids, timestamps: exact
        return False, rel
    return d <= max(rtol * scale, atol), rel


def scalar(v):
    return v is None or isinstance(v, (str, int, float, bool))


def identity_of(items):
    """The key (or key tuple) that identifies every object of a list uniquely, else None."""
    if not items or not all(isinstance(x, dict) for x in items):
        return None
    for k in ID_KEYS:
        vals = [x.get(k) for x in items]
        if all(k in x and scalar(x[k]) and x[k] is not None for x in items) and len(set(vals)) == len(vals):
            return (k,)
    for combo in ID_COMBOS:
        if all(all(c in x and scalar(x[c]) for c in combo) for x in items):
            vals = [tuple(x[c] for c in combo) for x in items]
            if len(set(vals)) == len(vals):
                return combo
    return None


class Diff:
    def __init__(self, rtol, atol):
        self.rtol, self.atol = rtol, atol
        self.items = []        # (file, unit, field, where, kind, a, b, rel)

    def add(self, ctx, kind, a, b, rel=None):
        self.items.append((ctx["file"], " / ".join(ctx["unit"]) or "(whole file)",
                           "".join(ctx["field"]) or "(root)", "".join(ctx["where"]) or "(root)",
                           kind, a, b, rel))

    def walk(self, a, b, ctx):
        if is_num(a) and is_num(b):
            eq, rel = numbers_equal(a, b, self.rtol, self.atol)
            if not eq:
                self.add(ctx, "number", a, b, rel)
            return
        if type(a) is not type(b) and not (is_num(a) and is_num(b)):
            self.add(ctx, "type", a, b)
            return
        if isinstance(a, dict):
            for k in sorted(set(a) | set(b)):
                sub = child(ctx, "." + k if ctx["field"] else k)
                if k not in b:
                    self.add(sub, "missing in B", a[k], None)
                elif k not in a:
                    self.add(sub, "extra in B", None, b[k])
                else:
                    self.walk(a[k], b[k], sub)
            return
        if isinstance(a, list):
            idk = identity_of(a) if identity_of(b) == identity_of(a) else None
            if idk:
                ka = [tuple(x[k] for k in idk) for x in a]
                kb = [tuple(x[k] for k in idk) for x in b]
                ia, ib = dict(zip(ka, a)), dict(zip(kb, b))
                common = [k for k in ka if k in ib]
                if common != [k for k in kb if k in ia]:
                    self.add(ctx, "order", None, None)
                for k in ka:
                    sub = child(ctx, "[*]", label_of(idk, k), unit_part(idk, k))
                    if k not in ib:
                        self.add(sub, "missing in B", ia[k], None)
                    else:
                        self.walk(ia[k], ib[k], sub)
                for k in kb:
                    if k not in ia:
                        self.add(child(ctx, "[*]", label_of(idk, k), unit_part(idk, k)), "extra in B", None, ib[k])
                return
            for i in range(min(len(a), len(b))):
                self.walk(a[i], b[i], child(ctx, "[]", "[%d]" % i))
            if len(a) != len(b):
                self.add(ctx, "length", len(a), len(b))
            return
        if a != b:
            self.add(ctx, "value", a, b)


def label_of(idk, k):
    return "[" + ",".join("%s=%s" % (n, v) for n, v in zip(idk, k)) + "]"


def unit_part(idk, k):
    return [str(v) for n, v in zip(idk, k) if n in UNIT_KEYS]


def child(ctx, field, where=None, unit=None):
    return {"file": ctx["file"], "field": ctx["field"] + [field], "where": ctx["where"] + [where or field],
            "unit": ctx["unit"] + (unit or [])}


def short(v, n=70):
    if v is None:
        return "null"
    s = repr(v) if not isinstance(v, (dict, list)) else ("{...%d keys}" % len(v) if isinstance(v, dict)
                                                         else "[...%d items]" % len(v))
    return s if len(s) <= n else s[: n - 3] + "..."


def change(kind, a, b, rel):
    if kind in ("missing in B", "extra in B"):
        return "%s: %s" % (kind, short(a if kind == "missing in B" else b))
    if kind == "order":
        return "same items, different order"
    if kind == "length":
        return "length %s -> %s" % (a, b)
    if kind == "number":
        return "%s -> %s (rel %.3g)" % (short(a), short(b), rel)
    return "%s -> %s" % (short(a), short(b))


def out_dir(label, scratch):
    d = pc.label_dir(label, scratch)
    return d if os.path.basename(os.path.normpath(d)) == "out" else os.path.join(d, "out")


def header_of(d):
    bits = []
    for f in ("run_mac.json", "run_analysis.json"):
        p = os.path.join(d, f)
        if os.path.exists(p):
            m = pc.read_json(p)
            bits.append("%s: commit %s (%s changed paths)" % (f, m.get("commit"), m.get("changedPaths")))
    return "; ".join(bits) or "no run_*.json"


def report(diff, la, lb, da, db, notes, max_per_group):
    L = ["# Parity differences: `%s` vs `%s`" % (la, lb), "",
         "- A: `%s` - %s" % (da, header_of(da)), "- B: `%s` - %s" % (db, header_of(db)),
         "- Tolerance: rtol %g, atol %g; states and labels exact." % (diff.rtol, diff.atol)]
    for n in notes:
        L.append("- %s" % n)
    L += ["", "**%d difference(s).**" % len(diff.items)]
    if not diff.items:
        return "\n".join(L) + "\n"
    groups = {}
    for it in diff.items:
        groups.setdefault((it[0], it[2], it[4]), []).append(it)
    L += ["", "## By field", "",
          "Tick each one explained (and say by what), or leave it as not explained.", ""]
    for (f, field, kind), its in sorted(groups.items()):
        rels = [i[7] for i in its if i[7] is not None]
        ex = its[0]
        L.append("- [ ] `%s` `%s` - %s, %d place(s)%s. e.g. %s `%s`: %s" % (
            f, field, kind, len(its), (", largest rel %.3g" % max(rels)) if rels else "",
            ex[1], ex[3], change(ex[4], ex[5], ex[6], ex[7])))
    units = {}
    for it in diff.items:
        units.setdefault((it[0], it[1]), []).append(it)
    L += ["", "## By swing / session", ""]
    for (f, unit), its in sorted(units.items()):
        L.append("### %s - %s (%d)" % (f, unit, len(its)))
        L.append("")
        for it in its[:max_per_group]:
            L.append("- [ ] `%s`: %s" % (it[3], change(it[4], it[5], it[6], it[7])))
        if len(its) > max_per_group:
            L.append("- ... and %d more (raise --max-per-group to list them)" % (len(its) - max_per_group))
        L.append("")
    return "\n".join(L) + "\n"


def load_content(path):
    """A content file's JSON, or None when it is empty or not JSON (the caller stops: exit 2)."""
    try:
        if os.path.getsize(path) == 0:
            return None
        return pc.read_json(path)
    except (OSError, ValueError):
        return None


def self_test():
    """The rules of the module header on built-in cases. Prints each; exit 0 when all hold."""
    inf, nan = float("inf"), float("nan")
    cases = [  # (name, a, b, differences expected)
        ("equal floats", 1.5, 1.5, 0),
        ("float noise under rtol", 3.9999999999999294, 4.000000000000005, 0),
        ("float change over rtol", 39.904, 39.904 * (1 + 1e-6), 1),
        ("near zero under atol", 0.0, 1e-13, 0),
        ("near zero over atol", 0.0, 1e-11, 1),
        ("int vs same float", 1, 1.0, 0),
        ("epoch-ms integer +1", 1792843859477, 1792843859478, 1),
        ("epoch-us integer +1", 1792843859477000, 1792843859477001, 1),
        ("whole float +1", 1792843859477.0, 1792843859478.0, 1),
        ("count +1", 9, 10, 1),
        ("inf vs number", inf, 1.0, 1),
        ("inf vs huge", inf, 1e300, 1),
        ("inf vs -inf", inf, -inf, 1),
        ("inf vs inf", inf, inf, 0),
        ("nan vs nan", nan, nan, 0),
        ("nan vs number", nan, 1.0, 1),
        ("bool vs int", True, 1, 1),
        ("null vs 0", None, 0, 1),
        ("state change", "fired", "clean", 1),
        ("missing key", {"a": 1, "b": 2}, {"a": 1}, 1),
        ("extra key", {"a": 1}, {"a": 1, "b": 2}, 1),
        ("key order ignored", {"a": 1, "b": 2}, {"b": 2, "a": 1}, 0),
        ("inserted shot, no cascade", {"shots": [{"shotId": 1, "v": 1}, {"shotId": 2, "v": 2}]},
         {"shots": [{"shotId": 1, "v": 1}, {"shotId": 9, "v": 0}, {"shotId": 2, "v": 2}]}, 1),
        ("reordered ids, once", {"s": [{"id": "x"}, {"id": "y"}]}, {"s": [{"id": "y"}, {"id": "x"}]}, 1),
        ("scalar list length", [1, 2], [1, 2, 3], 1),
    ]
    bad = 0
    for name, a, b, want in cases:
        d = Diff(1e-9, 1e-12)
        d.walk(a, b, {"file": "t", "field": [], "where": [], "unit": []})
        ok = len(d.items) == want
        bad += not ok
        print("%-4s %-28s expected %d, got %d" % ("ok" if ok else "FAIL", name, want, len(d.items)))
    # Nothing compared is a stop, not a pass: two empty runs, and an empty content file.
    import tempfile
    with tempfile.TemporaryDirectory() as t:
        for r in ("A", "B"):
            os.makedirs(os.path.join(t, r, "out"))
        rc_empty = run([os.path.join(t, "A"), os.path.join(t, "B")], quiet=True)
        for r in ("A", "B"):
            open(os.path.join(t, r, "out", "athlete.json"), "w").close()
        rc_zero = run([os.path.join(t, "A"), os.path.join(t, "B")], quiet=True)
    for name, rc in (("two empty runs", rc_empty), ("empty content file", rc_zero)):
        ok = rc == 2
        bad += not ok
        print("%-4s %-28s expected exit 2, got %d" % ("ok" if ok else "FAIL", name, rc))
    print("self-test: %s" % ("all %d cases hold" % (len(cases) + 2) if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    return run(sys.argv[1:])


def run(argv, quiet=False):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("labelA")
    ap.add_argument("labelB")
    ap.add_argument("--scratch", default=pc.SCRATCH_ROOT, help="scratch root (default %(default)s)")
    ap.add_argument("--rtol", type=float, default=1e-9)
    ap.add_argument("--atol", type=float, default=1e-12)
    ap.add_argument("--out", help="also write the Markdown here")
    ap.add_argument("--max-per-group", type=int, default=40, help="lines per swing/session group")
    ap.add_argument("--self-test", action="store_true", help="check the comparison rules and exit")
    a = ap.parse_args(argv)

    da, db = out_dir(a.labelA, a.scratch), out_dir(a.labelB, a.scratch)
    for d in (da, db):
        if not os.path.isdir(d):
            print("parity_diff: no run at %s" % d, file=sys.stderr)
            return 2
    diff = Diff(a.rtol, a.atol)
    notes = []
    compared = 0
    for f in pc.CONTENT_FILES:
        pa, pb = os.path.join(da, f), os.path.join(db, f)
        ctx = {"file": f, "field": [], "where": [], "unit": []}
        if not os.path.exists(pa) and not os.path.exists(pb):
            notes.append("`%s`: in neither run, not compared." % f)
            continue
        if not os.path.exists(pb):
            diff.add(ctx, "missing in B", "(file)", None)
            continue
        if not os.path.exists(pa):
            diff.add(ctx, "extra in B", None, "(file)")
            continue
        ja, jb = load_content(pa), load_content(pb)
        if ja is None or jb is None:
            print("parity_diff: %s is empty or not JSON - nothing to compare"
                  % (pa if ja is None else pb), file=sys.stderr)
            return 2
        diff.walk(ja, jb, ctx)
        compared += 1
        notes.append("`%s`: compared." % f)
    if compared == 0:
        print("parity_diff: no content file (%s) was compared - the runs hold no outputs"
              % ", ".join(pc.CONTENT_FILES), file=sys.stderr)
        return 2
    text = report(diff, a.labelA, a.labelB, da, db, notes, a.max_per_group)
    if not quiet:
        sys.stdout.write(text)
    if a.out:
        with open(a.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return 1 if diff.items else 0


if __name__ == "__main__":
    sys.exit(main())
