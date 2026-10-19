#!/usr/bin/env python3
"""Render data/duplicate-evidence.md (mechanical evidence) from inventory.json + duplicates.json.

    build_evidence.py <catalogue-dir>
"""
import json, os, sys, collections

D = os.path.abspath(sys.argv[1])
inv = json.load(open(os.path.join(D, "inventory.json")))
dup = json.load(open(os.path.join(D, "duplicates.json")))
app = [c for c in inv["components"] if c["category"] == "app"]
short = lambda p: p.replace("src/Gui/", "")

o = ["# Duplicate evidence — mechanical", "",
     "Output of `tools/find_duplicates.py` and the inventory. These are *signals*, not verdicts: every row says only that text or names coincide. The verdicts, with a reading of the code, are in [duplicates.md](../duplicates.md).", ""]

# ---- local components defined in more than one file, or shadowing a shared one ----------
shared = {c["name"] for c in app if not c["dir"].endswith(("components", "typeset"))} | {c["name"] for c in app}
local = collections.defaultdict(list)
for c in app:
    for ic in c["inline_components"]:
        local[ic["name"]].append((c["path"], ic["base"]))
o += ["## 1. Local (`component X:`) names declared in several files", "",
      "A local component is private to its file. The same name in several files is almost always the same widget re-implemented per file.", "",
      "| Local component | Files | Declared in |", "|---|---:|---|"]
for n, locs in sorted(local.items(), key=lambda kv: -len(kv[1])):
    if len(locs) > 1:
        o.append(f"| `{n}` | {len(locs)} | " + ", ".join(f"`{short(p)}`" for p, _ in locs) + " |")
o += ["", "### Local components that shadow a module-level component of the same name", "",
      "| Local name | Declared in | Module component |", "|---|---|---|"]
names = {c["name"]: c for c in app}
for n, locs in sorted(local.items()):
    if n in names:
        o.append(f"| `{n}` | " + ", ".join(f"`{short(p)}`" for p, _ in locs) + f" | `{short(names[n]['path'])}` |")
o += ["", "### Local components named like a shared primitive (not identical names)", ""]
prims = ["Toggle", "Pill", "Chip", "Badge", "Card", "Section", "Heading", "Row", "Button", "Btn", "Picker", "Flick", "Foot", "Notice"]
o += ["| Family | Local components |", "|---|---|"]
fam = collections.defaultdict(list)
for n, locs in local.items():
    for p in prims:
        if p.lower() in n.lower():
            fam[p].extend((n, path) for path, _ in locs)
for p in prims:
    if fam[p]:
        o.append(f"| {p} | " + ", ".join(f"`{n}` ({short(path).split('/')[-1][:-4]})" for n, path in sorted(set(fam[p]))) + " |")

# ---- identical function bodies --------------------------------------------------------------
o += ["", "## 2. Identical function bodies in different files", "",
      "| Function(s) | Files | Body (start) |", "|---|---:|---|"]
for f in dup["dup_function_bodies"]:
    fn = ", ".join(sorted(set(w["fn"] for w in f["where"])))
    o.append(f"| `{fn}` | {len(f['where'])}: " + ", ".join(f"`{short(w['file']).split('/')[-1][:-4]}`" for w in f["where"]) + f" | `{f['body'][:90].replace('|','/')}…` |")

# ---- same function name --------------------------------------------------------------------
o += ["", "## 3. Same function name in 3+ files", "",
      "| Function | Files | In |", "|---|---:|---|"]
for n, ps in sorted(dup["same_function_name"].items(), key=lambda kv: -len(kv[1])):
    if len(ps) >= 3 and not n.startswith("on"):
        o.append(f"| `{n}` | {len(ps)} | " + ", ".join(f"`{short(p).split('/')[-1][:-4]}`" for p in ps[:8]) + (" …" if len(ps) > 8 else "") + " |")

# ---- similar file pairs -----------------------------------------------------------------------
o += ["", "## 4. Whole-file similarity", "",
      "Jaccard similarity of 4-token shingles after stripping comments/strings/numbers; *containment* = share of the smaller file found in the other. A tiny file fully contained in a large one (e.g. `SetupTogglePill`) is a *copy of a primitive*, not a copy of the large file.", "",
      "| File A | File B | Jaccard | Containment | Lines A / B |", "|---|---|---:|---:|---|"]
seen = set()
for p in dup["file_pairs"][:30]:
    o.append(f"| `{short(p['a'])}` | `{short(p['b'])}` | {p['jaccard']} | {p['containment']} | {p['size_a']} / {p['size_b']} |")

# ---- clone blocks, collapsed by file-set ------------------------------------------------------
groups = collections.defaultdict(lambda: [0, 0, None])
for c in dup["clones"]:
    k = tuple(c["files"])
    g = groups[k]
    g[0] += 1
    if c["length"] > g[1]: g[1] = c["length"]; g[2] = c["sample"]
o += ["", "## 5. Shared code blocks (≥ 7 normalised lines)", "",
      "Runs of identical code (after normalising strings/numbers/whitespace) shared by the same set of files. `Runs` = distinct runs found for that file set; `Longest` = the longest run in lines.", "",
      "| Files | Runs | Longest | Sample (first lines of longest) |", "|---|---:|---:|---|"]
for k, (n, ln, sample) in sorted(groups.items(), key=lambda kv: -(len(kv[0]) * kv[1][1]))[:30]:
    files = ", ".join(f"`{short(p).split('/')[-1][:-4]}`" for p in k[:6]) + (f" (+{len(k)-6})" if len(k) > 6 else "")
    smp = " ⏎ ".join(s[:60] for s in (sample or [])[:3]).replace("|", "/")
    o.append(f"| {files} | {n} | {ln} | `{smp}` |")

open(os.path.join(D, "data", "duplicate-evidence.md"), "w").write("\n".join(o) + "\n")
print("ok")
