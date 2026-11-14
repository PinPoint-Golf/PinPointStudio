"""Shared helpers for the SWE audit tools (docs/implementation/swe-audit).

Every tool takes the repo root as argv[1] and writes into docs/implementation/swe-audit/data/.
Scope is fixed here so every count in the audit is over the same files.
"""
import os, re

RING1 = ["src/Analysis", "src/Metrics", "src/Diagnostics", "src/Coaching", "src/Gui/diagnostics"]
RING2 = ["src/Gui/shot", "src/Gui/review", "src/Gui/session", "src/Gui/home", "src/Gui/charts", "src/Gui/report"]
BOUNDARY = ["src/Core", "src/LaunchMonitor", "src/Export"]
SCOPE = RING1 + RING2

CPP_EXT = (".cpp", ".h", ".hpp", ".cc")
QML_EXT = (".qml", ".js")

def ring_of(path):
    for d in RING1:
        if path.startswith(d + "/"): return "ring1"
    for d in RING2:
        if path.startswith(d + "/"): return "ring2"
    for d in BOUNDARY:
        if path.startswith(d + "/"): return "boundary"
    return "other"

def is_excluded(rel):
    parts = rel.split("/")
    return "tests" in parts or "build" in parts or "Testing" in parts or "third_party" in parts

def walk(root, dirs, exts):
    out = []
    for d in dirs:
        base = os.path.join(root, d)
        for dp, dn, fn in os.walk(base):
            dn[:] = sorted(x for x in dn if x not in ("tests", "build", "Testing"))
            for f in sorted(fn):
                if f.endswith(exts):
                    rel = os.path.relpath(os.path.join(dp, f), root)
                    if not is_excluded(rel): out.append(rel)
    return out

def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()

def strip_comments(src):
    """Blank out comments and string literals but keep line structure (so line numbers survive)."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i); j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in src[i:j])); i = j
        elif c == '"' and not src.startswith('R"', i - 1):
            j = i + 1
            while j < n and src[j] != '"' and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append('"' + " " * max(0, j - i - 1) + ('"' if j < n else "")); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)

def top_dir(rel):
    """src/Gui/diagnostics/x.cpp -> Gui/diagnostics ; src/Coaching/x.h -> Coaching"""
    p = rel.split("/")
    if len(p) >= 3 and p[1] == "Gui" and len(p) >= 4: return "Gui/" + p[2]
    if len(p) >= 3 and p[1] == "Diagnostics" and len(p) >= 4: return "Diagnostics/" + p[2]
    return p[1] if len(p) >= 2 else rel

def out_path(root, name):
    return os.path.join(root, "docs/implementation/swe-audit/data", name)
