#!/usr/bin/env python3
"""rename_identifiers - rename C++ identifiers whole-word from a table, never inside a string literal.

    python3 -I tools/refactor/rename_identifiers.py --map <table.tsv> [--apply] [--root DIR] [PATH ...]
    python3 -I tools/refactor/rename_identifiers.py --map <table.tsv> --verify [--base REV] [--root DIR] [PATH ...]

PATHs (default `src tools`, relative to --root, default the current directory) are walked for C and C++
files only (.h .hpp .hh .c .cc .cpp .cxx .mm .m). Nothing else is ever edited: QML, JavaScript, JSON,
Python, CMake, allowlists and Markdown keep every name, because there a name can be a QML-facing name,
a persisted key or a lint's record. Their occurrences of a table name are listed ("outside the tool").

THE TABLE (TSV; `#` starts a comment line; the first non-comment line is the header):

    old <TAB> new <TAB> declared <TAB> note <TAB> reviewed

  old, new   whole C++ identifiers (`[A-Za-z_][A-Za-z0-9_]*`); `old` matches only as a whole word.
  declared   where `old` is declared (file:line), for the reader; not used.
  note       free text, for the reader; not used.
  reviewed   empty, or a reason. A name that also appears inside a string or character literal in a
             scanned file is REFUSED (not renamed at all, and listed) unless this column is filled:
             a literal may be a persisted key, a QML-facing name or a log tag. When it is filled the
             name is renamed in code and comments and every literal is still left exactly as it is.

WHAT IT REFUSES (each listed; the run exits 1 when anything is refused, and with --apply the
names that were not refused are still renamed):
  - a name found inside a string or character literal, unless `reviewed` is filled;
  - a new name that already occurs as a whole word in a scanned file (two names would merge);
  - two rows with the same old name, or two old names mapped to one new name;
  - a name that is not a C++ identifier.

WHAT IT EDITS: every whole-word occurrence in code and in comments (// and /* */), so a comment that
names a type follows it. String literals ("...", raw R"d(...)d", with any u8/u/U/L prefix), character
literals and `#include "..."` paths are never edited.

--verify (after an --apply): for every scanned C/C++ file that differs from --base (default HEAD,
read with `git show`), the ordered list of its string and character literals must be the same before
and after. Prints each file whose literals changed and the first difference; exit 1 if any did. This is
the freeze proof: a rename that would move a key, a tag or a QML-facing string shows here.

Exit 0: nothing refused (dry run or applied) / --verify found no literal changed. 1: something refused or
a literal changed. 2: a bad table or argument.

Written for platform-refactor item B1.8 (the priorities' renames, map tools/refactor/maps/B1.8-priorities.tsv);
general enough for any whole-word rename table.
"""
import argparse
import os
import re
import subprocess
import sys

CPP_EXT = {".h", ".hpp", ".hh", ".c", ".cc", ".cpp", ".cxx", ".mm", ".m"}
OTHER_EXT = {".qml", ".js", ".json", ".py", ".cmake", ".txt", ".md", ".ini", ".cmd", ".sh", ".tsv"}
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
WORD = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RAW_PREFIX = re.compile(r"(?:u8|u|U|L)?R\"([^\s()\\]{0,16})\(")


def segments(text):
    """Split C++ source into (kind, text) with kind in code / comment / literal. Concatenating the
    texts gives the input back exactly."""
    out = []
    i, n, start = 0, len(text), 0

    def flush(upto, kind="code"):
        nonlocal start
        if upto > start:
            out.append((kind, text[start:upto]))
        start = upto

    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            flush(i)
            j = text.find("\n", i)
            j = n if j < 0 else j
            # a line comment continued by a backslash-newline runs on
            while j < n and j > 0 and text[j - 1] == "\\":
                k = text.find("\n", j + 1)
                j = n if k < 0 else k
            i = j
            flush(i, "comment")
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            flush(i)
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            flush(i, "comment")
            continue
        if c == '"':
            # raw string? look back for its prefix (R, u8R, LR, uR, UR) at the token start
            m = None
            for back in (3, 2, 1):
                if i - back >= 0:
                    cand = RAW_PREFIX.match(text, i - back)
                    if cand and cand.start() == i - back and text[cand.end() - 1] == "(" \
                            and text.find('"', i - back) == i \
                            and (i - back == 0 or not (text[i - back - 1].isalnum() or text[i - back - 1] == "_")):
                        m = cand
                        break
            if m:
                lit_start = m.start()
                # the prefix letters were emitted as code so far; pull them back into the literal
                if lit_start < start:
                    raise RuntimeError("raw string prefix straddles a segment")
                flush(lit_start)
                close = ")" + m.group(1) + '"'
                j = text.find(close, m.end())
                i = n if j < 0 else j + len(close)
                flush(i, "literal")
                continue
            flush(i)
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n":
                    break  # unterminated: stop at the line end
                j += 1
            i = min(n, j + 1)
            flush(i, "literal")
            continue
        if c == "'":
            # a digit separator (1'000'000, 0xFF'FF) is not a character literal
            if i > 0 and i + 1 < n and text[i - 1] in "0123456789abcdefABCDEF" and text[i + 1] in "0123456789abcdefABCDEF":
                k = i - 1
                while k >= 0 and (text[k].isalnum() or text[k] in "_'."):
                    k -= 1
                if text[k + 1].isdigit():
                    i += 1
                    continue
            flush(i)
            j = i + 1
            while j < n and text[j] != "'":
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n":
                    break
                j += 1
            i = min(n, j + 1)
            flush(i, "literal")
            continue
        i += 1
    flush(n)
    return out


def literals(text):
    return [t for k, t in segments(text) if k == "literal"]


def read_table(path):
    rows, seen_old, seen_new, errors = [], {}, {}, []
    header = None
    with open(path, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            if header is None:
                header = cols
                if cols[:2] != ["old", "new"]:
                    errors.append(f"{path}:{ln}: the header must start 'old<TAB>new'")
                continue
            cols += [""] * (5 - len(cols))
            old, new, declared, note, reviewed = (c.strip() for c in cols[:5])
            if not IDENT.match(old) or not IDENT.match(new):
                errors.append(f"{path}:{ln}: '{old}' -> '{new}' is not a C++ identifier pair")
                continue
            if old in seen_old:
                errors.append(f"{path}:{ln}: '{old}' is already row {seen_old[old]}")
            if new in seen_new:
                errors.append(f"{path}:{ln}: '{new}' is already the new name of row {seen_new[new]}")
            seen_old[old], seen_new[new] = ln, ln
            rows.append(dict(old=old, new=new, declared=declared, note=note, reviewed=reviewed, line=ln))
    if header is None:
        errors.append(f"{path}: no header")
    return rows, errors


def walk(root, paths):
    cpp, other = [], []
    for p in paths:
        full = os.path.join(root, p)
        if os.path.isfile(full):
            (cpp if os.path.splitext(full)[1] in CPP_EXT else other).append(full)
            continue
        for d, dirs, files in os.walk(full):
            dirs[:] = sorted(x for x in dirs if not x.startswith(".") and x != "__pycache__")
            for fn in sorted(files):
                ext = os.path.splitext(fn)[1]
                fp = os.path.join(d, fn)
                if ext in CPP_EXT:
                    cpp.append(fp)
                elif ext in OTHER_EXT:
                    other.append(fp)
    return cpp, other


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--map", required=True)
    ap.add_argument("--root", default=".")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--base", default="HEAD")
    ap.add_argument("paths", nargs="*", default=["src", "tools"])
    a = ap.parse_args()
    root = os.path.abspath(a.root)

    rows, errors = read_table(a.map)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 2
    cpp, other = walk(root, a.paths)
    rel = lambda p: os.path.relpath(p, root)

    if a.verify:
        changed = 0
        for fp in cpp:
            r = rel(fp)
            try:
                before = subprocess.run(["git", "-C", root, "show", f"{a.base}:{r}"], capture_output=True,
                                        text=True, check=True).stdout
            except subprocess.CalledProcessError:
                continue  # new file
            with open(fp, encoding="utf-8", errors="surrogateescape") as f:
                after = f.read()
            if before == after:
                continue
            lb, la = literals(before), literals(after)
            if lb != la:
                changed += 1
                k = next((i for i, (x, y) in enumerate(zip(lb, la)) if x != y), min(len(lb), len(la)))
                print(f"LITERAL CHANGED {r}: #{k}: {lb[k] if k < len(lb) else '<none>'!r} -> "
                      f"{la[k] if k < len(la) else '<none>'!r}")
        files = sum(1 for fp in cpp if subprocess.run(["git", "-C", root, "diff", "--quiet", a.base, "--", rel(fp)]).returncode)
        print(f"verify: {files} changed C/C++ file(s) against {a.base}; string/char literals changed in {changed}")
        return 1 if changed else 0

    texts = {}
    for fp in cpp:
        with open(fp, encoding="utf-8", errors="surrogateescape") as f:
            texts[fp] = f.read()
    segs = {fp: segments(t) for fp, t in texts.items()}
    words_all = {}
    for fp, ss in segs.items():
        for kind, t in ss:
            for w in WORD.findall(t):
                words_all.setdefault(w, set()).add(fp)

    refused, ok = [], []
    for r in rows:
        pat = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(r["old"]) + r"(?![A-Za-z0-9_])")
        in_lit = []
        code = comment = 0
        files = set()
        for fp, ss in segs.items():
            for kind, t in ss:
                hits = len(pat.findall(t))
                if not hits:
                    continue
                if kind == "literal":
                    in_lit.append(f"{rel(fp)}: {t.strip()[:80]}")
                elif kind == "code":
                    code += hits
                    files.add(fp)
                else:
                    comment += hits
                    files.add(fp)
        r.update(code=code, comment=comment, files=files, pat=pat)
        clash = sorted(rel(p) for p in words_all.get(r["new"], ()))
        why = []
        if in_lit and not r["reviewed"]:
            why.append("inside a string literal: " + "; ".join(in_lit[:4]) + (" …" if len(in_lit) > 4 else ""))
        if clash:
            why.append(f"new name '{r['new']}' already used in " + ", ".join(clash[:4]))
        if code + comment == 0:
            why.append("no occurrence found")
        r["in_lit"] = in_lit
        (refused if why else ok).append((r, why))

    outside = {}
    skip = os.path.abspath(a.map)
    for fp in other:
        if os.path.abspath(fp) == skip:
            continue
        try:
            with open(fp, encoding="utf-8", errors="surrogateescape") as f:
                t = f.read()
        except OSError:
            continue
        for r in rows:
            n = len(r["pat"].findall(t))
            if n:
                outside.setdefault(r["old"], []).append(f"{rel(fp)} ×{n}")

    print(f"{'old':<28} {'new':<28} {'code':>5} {'cmnt':>5} {'files':>5}  literal")
    for r, why in ok + refused:
        print(f"{r['old']:<28} {r['new']:<28} {r['code']:>5} {r['comment']:>5} {len(r['files']):>5}  "
              f"{len(r['in_lit'])}{' (reviewed)' if r['in_lit'] and r['reviewed'] else ''}{'  REFUSED' if why else ''}")
    for r, why in refused:
        print(f"REFUSED {r['old']} (table line {r['line']}): " + " | ".join(why))
    for old, where in sorted(outside.items()):
        print(f"outside the tool (not edited) {old}: " + ", ".join(where))

    if a.apply and ok:
        edited = 0
        for fp, ss in segs.items():
            parts = []
            for kind, t in ss:
                if kind != "literal":
                    for r, _ in ok:
                        if r["old"] in t:
                            t = r["pat"].sub(r["new"], t)
                parts.append(t)
            new = "".join(parts)
            if new != texts[fp]:
                with open(fp, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(new)
                edited += 1
        print(f"applied: {len(ok)} name(s) renamed in {edited} file(s); {len(refused)} refused")
    else:
        print(f"dry run: {len(ok)} name(s) would be renamed; {len(refused)} refused (--apply to write)")
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main())
