#!/usr/bin/env python3
"""Check every code citation in the audit's markdown against the pinned commit.
   check_citations.py <repo-root> <commit> <file.md> [more.md ...]

A citation is `path:line`, `path:a-b`, or `path:a, b` — path either repo-relative (src/…, tools/…, docs/…) or a bare
file name (`swing_doc.cpp:440`), resolved by unique basename, else by the last full path with that basename on the
same line, else by the file the finding's previous citation named. `:N` alone continues the previous file.
When a backticked quote follows a citation (`path:line` `code`, path:line — `code`), the quote must appear,
whitespace-insensitively, within ±4 lines of the cited range in `git show <commit>:<path>`.
Exit 1 if any citation fails. Prints counts and every failure.
"""
import re, sys, subprocess, collections, os

root, commit, files = sys.argv[1], sys.argv[2], sys.argv[3:]
tree = subprocess.run(["git", "-C", root, "ls-tree", "-r", "--name-only", commit], capture_output=True, text=True).stdout.split()
by_base = collections.defaultdict(list)
for p in tree: by_base[os.path.basename(p)].append(p)
EXT = r"(?:cpp|h|hpp|qml|js|py|json|md|txt|cmake)"
CIT = re.compile(r"((?:\.\./)?(?:[\w.+-]+/)*[\w.+-]+\." + EXT + r")?(?<![\w/])?:(\d+)(?:\s*[-–]\s*(\d+))?((?:\s*,\s*\d+(?:\s*[-–]\s*\d+)?)*)`?(?:\s*(?:[—:-]+\s*)?`([^`]{6,})`)?")
cache = {}
def blob(path):
    if path not in cache:
        r = subprocess.run(["git", "-C", root, "show", f"{commit}:{path}"], capture_output=True, text=True, errors="replace")
        cache[path] = r.stdout.splitlines() if r.returncode == 0 else None
    return cache[path]
squash = lambda s: re.sub(r"\s+", "", s)
stats = collections.Counter(); problems = []
def resolve(name, line, prev):
    if name is None: return prev
    if name.startswith("../"): return "EXTERNAL"
    if name in tree: return name
    base = os.path.basename(name)
    cands = [p for p in by_base.get(base, []) if p.endswith(name)]
    if len(cands) == 1: return cands[0]
    for m in re.finditer(r"((?:src|tools|docs|cmake|tests)/[\w./+-]+)", line):
        if m.group(1).endswith("/" + base) or m.group(1) == name: return m.group(1)
    if prev and os.path.basename(prev) == base: return prev
    return ("AMBIG", name, cands)
for md in files:
    prev = None
    for ln, line in enumerate(open(md, encoding="utf-8"), 1):
        if line.startswith("### "): prev = None
        for m in CIT.finditer(line):
            name = m.group(1)
            if name is None and (prev is None or not re.search(r"[`(\s,]$|^$", line[max(0, m.start() - 1):m.start()] or " ")):
                continue
            path = resolve(name, line, prev)
            if path is None: continue
            if path == "EXTERNAL": stats["external_repo"] += 1; continue
            if isinstance(path, tuple):
                stats["ambiguous"] += 1; problems.append(f"{md}:{ln}: ambiguous file name {path[1]} ({len(path[2])} matches)"); continue
            prev = path
            lines = blob(path)
            if lines is None:
                stats["missing_file"] += 1; problems.append(f"{md}:{ln}: no such file at {commit}: {path}"); continue
            ranges = [(int(m.group(2)), int(m.group(3) or m.group(2)))]
            for extra in re.findall(r"(\d+)(?:\s*[-–]\s*(\d+))?", m.group(4) or ""):
                ranges.append((int(extra[0]), int(extra[1] or extra[0])))
            bad = [(a, b) for a, b in ranges if a < 1 or b > len(lines) or b < a]
            if bad:
                stats["bad_line"] += 1; problems.append(f"{md}:{ln}: {path}:{bad[0][0]}-{bad[0][1]} outside 1..{len(lines)}"); continue
            quote = m.group(5)
            if quote:
                a, b = ranges[0]
                window = squash("".join(lines[max(0, a - 5):min(len(lines), b + 4)]))
                parts = [p for p in re.split(r"…|\.\.\.", quote) if len(squash(p)) >= 6] or [quote]
                if not all(squash(p) in window for p in parts):
                    stats["quote_not_found"] += 1; problems.append(f"{md}:{ln}: quote not at {path}:{a}: `{quote[:90]}`"); continue
                stats["ok_quoted"] += 1
            else:
                stats["ok_line"] += 1
print(dict(stats))
for p in problems: print(p)
sys.exit(1 if problems else 0)
