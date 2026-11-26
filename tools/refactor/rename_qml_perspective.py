#!/usr/bin/env python3
"""rename_qml_perspective - QML names the one perspective enum: `CameraInstance.<value>` -> `Perspective.<value>`.

    python3 -I tools/refactor/rename_qml_perspective.py [--root DIR] [--apply]
    python3 -I tools/refactor/rename_qml_perspective.py [--root DIR] --verify

Walks `src/` under --root (default the current directory) for .qml, .js and .mjs files, tests, fakes and probes
included, and rewrites each whole `CameraInstance.None|DownTheLine|FaceOn|Other|Impact` to `Perspective.<same value>`,
in code and in comments alike (a comment that names a value follows it). Every other `CameraInstance.` member
(`isConnecting`, `kImpactDefault*`, ...) is left exactly as it is, and so is every string literal: a name inside
quotes is refused (listed, nothing changed in that file), since a string may be a key or a log tag.

Without --apply it is a dry run: it prints each file and its count by value, the total, and the other
`CameraInstance.` members it leaves, and changes nothing. --apply makes the edits. --verify fails (exit 1) if any
`CameraInstance.<value>` is left; it is the brief's accept count (0).

`Perspective` is the Adapters ring's QML name for `recording::camera_perspective::Perspective`
(src/Gui/shell/perspective_qml.h); the codes (None 0, DownTheLine 1, FaceOn 2, Other 3, Impact 4) are the same, so
no value a binding reads changes.

Written for platform-refactor item C-2b (docs/design/rig_design.md §4.7).
Exit 0: done (dry run, applied, or verified clean). 1: something refused, or --verify found a value left.
"""
import argparse
import os
import re
import sys

VALUES = ("None", "DownTheLine", "FaceOn", "Other", "Impact")
NAME = re.compile(r"(?<![\w$.])CameraInstance\.(%s)\b" % "|".join(VALUES))
OTHER = re.compile(r"(?<![\w$.])CameraInstance\.([A-Za-z_]\w*)")
EXTS = (".qml", ".js", ".mjs")


def in_string(line, pos):
    """True when `pos` on this one line falls inside a '...', "..." or `...` literal (comments end the scan)."""
    quote = None
    i = 0
    while i < pos:
        c = line[i]
        if quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = None
        else:
            if line.startswith("//", i):
                return False
            if c in "'\"`":
                quote = c
        i += 1
    return quote is not None


def files(root):
    for d, dirs, names in os.walk(os.path.join(root, "src")):
        dirs.sort()
        for n in sorted(names):
            if n.endswith(EXTS):
                yield os.path.join(d, n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()

    total, nfiles, refused, others = 0, 0, [], {}
    for path in files(a.root):
        rel = os.path.relpath(path, a.root)
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        counts, bad = {}, []
        for ln, line in enumerate(text.split("\n"), 1):
            for m in NAME.finditer(line):
                if in_string(line, m.start()):
                    bad.append("%s:%d" % (rel, ln))
                else:
                    counts[m.group(1)] = counts.get(m.group(1), 0) + 1
            for m in OTHER.finditer(line):
                if m.group(1) not in VALUES:
                    others.setdefault(m.group(1), []).append("%s:%d" % (rel, ln))
        if bad:
            refused += bad
            continue
        n = sum(counts.values())
        if not n:
            continue
        total += n
        nfiles += 1
        print("%-60s %3d  %s" % (rel, n, " ".join("%s %d" % (v, counts[v]) for v in VALUES if v in counts)))
        if a.apply:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(NAME.sub(lambda m: "Perspective." + m.group(1), text))

    print("total: %d uses in %d files%s" % (total, nfiles, " (applied)" if a.apply else
                                              "" if a.verify else " (dry run: nothing changed)"))
    for name, where in sorted(others.items()):
        print("left as it is: CameraInstance.%s (%s)" % (name, ", ".join(where)))
    for r in refused:
        print("REFUSED (inside a string literal): %s" % r)
    if a.verify and total:
        print("VERIFY FAILED: %d uses of CameraInstance.<perspective value> remain" % total)
        return 1
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main())
