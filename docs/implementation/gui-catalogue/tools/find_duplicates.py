#!/usr/bin/env python3
"""Duplicate / near-duplicate detection across the QML layer (app files only).

    find_duplicates.py <repo-root> <inventory.json> <out.json>

Three signals:
  1. file pairs  -- Jaccard similarity of normalised 4-token shingles
  2. block clones -- runs of >= MINRUN identical normalised lines shared by >= 2 files
  3. function bodies -- identical (normalised) JS function bodies in different files
"""
import json, os, re, sys, collections, itertools

root, inv, out = sys.argv[1:4]
d = json.load(open(inv))
app = [c for c in d["components"] if c["category"] == "app"]

def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"(?m)(?<!:)//.*$", "", s)

def norm_line(l):
    l = l.strip()
    l = re.sub(r'"(?:[^"\\]|\\.)*"', '"S"', l)
    l = re.sub(r"'(?:[^'\\]|\\.)*'", "'S'", l)
    l = re.sub(r"\b\d+(?:\.\d+)?\b", "N", l)
    l = re.sub(r"\s+", " ", l)
    return l

docs = {}
for c in app:
    src = strip_comments(open(os.path.join(root, c["path"]), encoding="utf-8", errors="replace").read())
    lines = [norm_line(l) for l in src.splitlines()]
    lines = [l for l in lines if l and not l.startswith(("import ", "pragma "))]
    docs[c["path"]] = lines

# ---- 1. whole-file similarity -------------------------------------------------------------
def shingles(lines, k=4):
    toks = re.findall(r"[A-Za-z_]\w*|[{}()\[\]:;,.=+\-*/<>!?&|]", " ".join(lines))
    return set(zip(*[toks[i:] for i in range(k)])) if len(toks) >= k else set()

sh = {p: shingles(l) for p, l in docs.items()}
pairs = []
paths = [p for p in docs if len(sh[p]) >= 40]
inv_idx = collections.defaultdict(list)
for p in paths:
    for s in sh[p]: inv_idx[s].append(p)
cand = collections.Counter()
for s, ps in inv_idx.items():
    if len(ps) > 12: continue          # ubiquitous boilerplate
    for a, b in itertools.combinations(sorted(ps), 2): cand[(a, b)] += 1
for (a, b), n in cand.items():
    inter = len(sh[a] & sh[b]); uni = len(sh[a] | sh[b])
    j = inter / uni
    contain = inter / min(len(sh[a]), len(sh[b]))
    if j >= 0.30 or contain >= 0.60:
        pairs.append(dict(a=a, b=b, jaccard=round(j, 3), containment=round(contain, 3),
                          size_a=len(docs[a]), size_b=len(docs[b])))
pairs.sort(key=lambda x: -x["jaccard"])

# ---- 2. block clones ------------------------------------------------------------------------
MINRUN = 7
TRIVIAL = re.compile(r"^[{}\[\]()]+,?$|^anchors\.|^width: |^height: |^id: |^color: |^radius: |^visible: |^text: |^font\.")
def key(run): return "\n".join(run)
blocks = collections.defaultdict(set)
for p, L in docs.items():
    for i in range(len(L) - MINRUN + 1):
        run = L[i:i + MINRUN]
        nontriv = sum(1 for l in run if not TRIVIAL.match(l))
        if nontriv < MINRUN - 2: continue
        blocks[key(run)].add((p, i))
# merge overlapping windows into maximal clones
by_pair = collections.defaultdict(list)       # (frozenset of files) -> [(start per file)]
for k, occ in blocks.items():
    files = sorted(set(p for p, _ in occ))
    if len(files) < 2: continue
    by_pair[tuple(files)].append(sorted(occ))
clones = []
for files, windows in by_pair.items():
    # collapse consecutive windows: group by (file0, start) adjacency
    first = files[0]
    starts = sorted(set(i for w in windows for (p, i) in w if p == first))
    runs, cur = [], [starts[0]]
    for s in starts[1:]:
        if s == cur[-1] + 1: cur.append(s)
        else: runs.append(cur); cur = [s]
    runs.append(cur)
    for r in runs:
        length = len(r) + MINRUN - 1
        sample = docs[first][r[0]:r[0] + min(length, 14)]
        # locations in every file
        locs = {}
        for w in windows:
            for (p, i) in w:
                if p == first and i != r[0]: continue
        for p in files:
            ss = sorted(set(i for w in windows for (q, i) in w if q == p))
            locs[p] = ss[0] if ss else None
        clones.append(dict(files=list(files), length=length, sample=sample, first_line_index=r[0]))
clones.sort(key=lambda c: (-len(c["files"]) * c["length"], -c["length"]))

# ---- 3. identical functions -------------------------------------------------------------------
fn_bodies = collections.defaultdict(list)
for c in app:
    src = strip_comments(open(os.path.join(root, c["path"]), encoding="utf-8", errors="replace").read())
    for m in re.finditer(r"(?m)^[ \t]*function\s+(\w+)\s*\(([^)]*)\)\s*\{", src):
        i = m.end(); depth = 1
        while i < len(src) and depth:
            depth += {"{": 1, "}": -1}.get(src[i], 0); i += 1
        body = re.sub(r"\s+", " ", src[m.end():i - 1]).strip()
        if len(body) < 40: continue
        fn_bodies[body].append((c["path"], m.group(1)))
dup_fns = [dict(body=b[:400], where=[dict(file=f, fn=n) for f, n in sorted(set(w))])
           for b, w in fn_bodies.items() if len(set(f for f, _ in w)) > 1]
dup_fns.sort(key=lambda x: -len(x["where"]))

# same function NAME defined in several files (likely helper duplication even if bodies differ)
names = collections.defaultdict(set)
for c in app:
    for fn in c["functions"]: names[fn].add(c["path"])
same_name = {n: sorted(ps) for n, ps in names.items() if len(ps) > 1}

json.dump(dict(file_pairs=pairs, clones=clones[:400], dup_function_bodies=dup_fns, same_function_name=same_name),
          open(out, "w"), indent=1)
print("pairs", len(pairs), "clones", len(clones), "dupfn", len(dup_fns), "samename", len(same_name))
