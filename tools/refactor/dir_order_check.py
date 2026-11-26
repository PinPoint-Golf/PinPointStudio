#!/usr/bin/env python3
"""dir_order_check.py: the folder order inside one source directory, checked include by include.

Platform refactor B1.19 (Diagnostics re-cut by layer; R8 in docs/implementation/platform_b1_placements.md; the folders
and their order ruled by the Solution Architect, 26 Nov 2026, option (B)): the folders of src/Diagnostics are ordered,
lowest first, `model` -> `content` -> `derived` -> `engine`, and a file may include only its own folder and the
folders before it. This script lists every quoted #include that breaks the order: from a file in a lower folder to a
file in a higher one. Read-only.

    python3 tools/refactor/dir_order_check.py --order model,content,derived,engine --cycles   # ctest's check
    python3 tools/refactor/dir_order_check.py --order ... --map tools/refactor/maps/B1.19-diagnostics.map
    python3 tools/refactor/dir_order_check.py --cycles                                       # folder cycles only
    python3 tools/refactor/dir_order_check.py --self-test --order ... --cycles               # fixtures, then the tree

--root <checkout>   the tree to read (default: this file's checkout).
--base <dir>        the directory whose first-level folders are ordered (default: src/Diagnostics).
--order a,b,c       the folders, lowest first. Every file under --base (tests/ aside) must sit in one of them: a file in
                    an unlisted folder, or directly in --base, is an error (exit 2), never skipped, so a new folder
                    cannot pass by not being named.
--map <map>         the PROPOSED layout: a tools/refactor/move_files.py map (`old -> new`; a directory, a stem or one
                    file; `#`, `keep:` and `hand:` lines ignored). Each file is placed by its new path; includes are
                    read and resolved where the files are today. A map line that matches no file is an error.
--cycles            also print the folder graph's cycles (strongly connected sets of two or more folders), with the
                    file-level includes that make each edge. Needs no --order.
--self-test         first prove the check on fixture trees it writes in a temporary directory (never this tree): a
                    clean four-folder tree passes; one planted upward include turns it red (exit 1, the include
                    listed, and with --cycles the cycle it makes), and removing it turns it green again; a file in an
                    unlisted folder and an unresolved include are each exit 2; a test's include across folders and
                    into a missing file is skipped. A fixture that gives the wrong answer is exit 1 ("SELF-TEST
                    FAILED") and the tree is not read. With --order or --cycles, the tree is then checked as usual.

What is read: every .h/.hpp/.cpp/.cc/.mm under --base, except any path with a `tests` folder (the tests include
across folders and into src/Gui by design: B1.18's review, N1). A folder's sub-folders belong to it
(`derived/series/` is `derived`). Includes are resolved as lint A1 resolves them: relative to the file, then under
src/, then under src/Gui/, then a unique basename under --base (a bare name through a global include directory). An
include that resolves outside --base is not part of the order and is not listed. A quoted include that resolves
nowhere is an error: a check that can't see an edge must not pass it.

Exit codes: 0 no upward include (and, with --cycles, no folder cycle); 1 an upward include or a cycle, each listed, or
a failed self-test; 2 usage or input error (unlisted folder, unresolved include, a map line that matches nothing).
ctest: diagnostics_dir_order_check (src/Gui/tests/CMakeLists.txt), with --self-test, the ruled order and --cycles.
"""

import argparse
import contextlib
import io
import os
import posixpath
import re
import shutil
import sys
import tempfile

SRC_EXT = ('.h', '.hpp', '.cpp', '.cc', '.mm')
STEM_EXT = ('.h', '.cpp', '.mm')
INC_RE = re.compile(r'^\s*#\s*(?:include|import)\s*"([^"]+)"')


class InputError(Exception):
    pass


def is_test(rel):
    return 'tests' in rel.split('/')


def list_files(root, base):
    out = []
    top = os.path.join(root, base)
    if not os.path.isdir(top):
        raise InputError(f'no such directory: {base}')
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames[:] = sorted(d for d in dirnames if d != '__pycache__')
        for name in sorted(filenames):
            if name.endswith(SRC_EXT):
                rel = posixpath.relpath(os.path.join(dirpath, name).replace(os.sep, '/'), root.replace(os.sep, '/'))
                out.append(rel)
    return out


def read_map(root, path, files):
    """old path -> new path, for every file a map line names."""
    fileset = set(files)
    moved = {}
    with open(path, encoding='utf-8') as f:
        for n, raw in enumerate(f, 1):
            line = raw.split('#', 1)[0].strip()
            if not line or line.startswith(('keep:', 'hand:')):
                continue
            parts = re.split(r'\s*(?:->|→)\s*', line)
            if len(parts) != 2:
                raise InputError(f'{path}:{n}: not `old -> new`: {raw.strip()}')
            old, new = parts
            hits = []
            if old.endswith('/'):
                hits = [(p, posixpath.join(new.rstrip('/'), p[len(old):])) for p in files if p.startswith(old)]
            elif posixpath.splitext(old)[1]:
                if old in fileset:
                    hits = [(old, posixpath.join(new, posixpath.basename(old)) if new.endswith('/') else new)]
            else:
                for ext in STEM_EXT:
                    if old + ext in fileset:
                        dest = (posixpath.join(new, posixpath.basename(old) + ext) if new.endswith('/')
                                else new + ext)
                        hits.append((old + ext, dest))
            if not hits:
                raise InputError(f'{path}:{n}: matches no file under the checked directory: {old}')
            for o, d in hits:
                if o in moved:
                    raise InputError(f'{path}:{n}: {o} is mapped twice')
                moved[o] = d
    return moved


def folder_of(rel, base):
    sub = rel[len(base) + 1:]
    return sub.split('/', 1)[0] if '/' in sub else ''


def resolve(root, base, src, spelling, by_name):
    cands = [posixpath.normpath(posixpath.join(posixpath.dirname(src), spelling)),
             posixpath.normpath(posixpath.join('src', spelling)),
             posixpath.normpath(posixpath.join('src/Gui', spelling))]
    for c in cands:
        if not c.startswith('..') and os.path.isfile(os.path.join(root, c)):
            return c
    hits = by_name.get(posixpath.basename(spelling), [])
    if len(hits) == 1:
        return hits[0]
    if len(hits) > 1:
        raise InputError(f'{src}: "{spelling}" is ambiguous: {", ".join(hits)}')
    return None


def edges(root, base, files):
    """(file, line, spelling, target) for every quoted include of a file into another file under base."""
    by_name = {}
    for p in files:
        by_name.setdefault(posixpath.basename(p), []).append(p)
    unresolved = []
    out = []
    for src in files:
        with open(os.path.join(root, src), encoding='utf-8', errors='surrogateescape') as f:
            for n, line in enumerate(f, 1):
                m = INC_RE.match(line)
                if not m:
                    continue
                tgt = resolve(root, base, src, m.group(1), by_name)
                if tgt is None:
                    if posixpath.basename(m.group(1)).startswith(('moc_', 'ui_')) or m.group(1).endswith('.moc'):
                        continue   # generated by the build
                    unresolved.append(f'{src}:{n}: "{m.group(1)}"')
                    continue
                if tgt.startswith(base + '/') and not is_test(tgt):
                    out.append((src, n, m.group(1), tgt))
    if unresolved:
        raise InputError('unresolved includes:\n  ' + '\n  '.join(unresolved))
    return out


def sccs(nodes, adj):
    index, low, stack, on, out, counter = {}, {}, [], set(), [], [0]

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in sorted(adj.get(v, ())):
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            out.append(sorted(comp))

    for v in sorted(nodes):
        if v not in index:
            visit(v)
    return [c for c in out if len(c) > 1]


def self_test():
    """The fixtures (--self-test): each case writes its tree, runs the check on it, and compares the exit code and the
    lines it must print. Returns the number of failed cases."""
    order = ['--order', 'model,content,derived,engine']
    clean = {
        'src/Diagnostics/model/a.h': '#pragma once\n',
        'src/Diagnostics/content/b.h': '#include "../model/a.h"\n',
        'src/Diagnostics/derived/c.h': '#include "../content/b.h"\n#include "../model/a.h"\n',
        'src/Diagnostics/derived/series/s.h': '#include "../c.h"\n',
        'src/Diagnostics/engine/d.h': '#include "../derived/series/s.h"\n#include <vector>\n',
        'src/Diagnostics/engine/d.cpp': '#include "d.h"\n#include "Core/x.h"\n',
        'src/Core/x.h': '#pragma once\n',
        # a test includes across folders and into src/Gui by design; it is skipped, even an include that resolves nowhere
        'src/Diagnostics/tests/t.cpp': '#include "../engine/d.h"\n#include "../../Gui/missing.h"\n',
    }
    plant = dict(clean)
    plant['src/Diagnostics/model/a.h'] = '#pragma once\n#include "../engine/d.h"\n'
    cases = [
        ('a clean tree passes', clean, order + ['--cycles'], 0, ['Upward includes (order model -> content -> derived -> '
                                                                  'engine): 0', 'Folder cycles: 0']),
        ('a planted upward include is red', plant, order, 1,
         ['Upward includes (order model -> content -> derived -> engine): 1',
          'src/Diagnostics/model/a.h:2  [model -> engine]  "../engine/d.h"']),
        ('the planted include makes a cycle', plant, ['--cycles'], 1, ['Folder cycles: 1']),
        ('removing it is green again', clean, order + ['--cycles'], 0, ['Upward includes (order model -> content -> '
                                                                         'derived -> engine): 0']),
        ('a file in an unlisted folder is an error', dict(clean, **{'src/Diagnostics/views/v.h': '#pragma once\n'}),
         order, 2, ['src/Diagnostics/views/v.h']),
        ('an unresolved include is an error', dict(clean, **{'src/Diagnostics/model/e.h': '#include "gone.h"\n'}),
         order, 2, ['src/Diagnostics/model/e.h:1: "gone.h"']),
    ]
    failed = 0
    for name, tree, args, want, lines in cases:
        tmp = tempfile.mkdtemp(prefix='dir_order_selftest_')
        try:
            for rel, text in tree.items():
                path = os.path.join(tmp, *rel.split('/'))
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                got = main(['--root', tmp] + args)
            text = out.getvalue()
            missing = [l for l in lines if l not in text]
            ok = got == want and not missing
            print(f'  {"ok  " if ok else "FAIL"} {name}: exit {got} (want {want})'
                  + (f'; missing {missing}' if missing else ''))
            if not ok:
                failed += 1
                print('    ' + text.replace('\n', '\n    '))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return failed


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument('--base', default='src/Diagnostics')
    ap.add_argument('--order', default='')
    ap.add_argument('--map')
    ap.add_argument('--cycles', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    a = ap.parse_args(argv)
    if a.self_test:
        print('dir_order_check --self-test: the fixtures')
        if self_test():
            print('SELF-TEST FAILED')
            return 1
        print('self-test passed')
        if not a.order and not a.cycles:
            return 0
    base = a.base.strip('/')
    order = [x for x in a.order.split(',') if x]
    if not order and not a.cycles:
        ap.error('give --order, --cycles, or both')
    try:
        files = [p for p in list_files(a.root, base) if not is_test(p)]
        moved = read_map(a.root, a.map, files) if a.map else {}
        place = {p: folder_of(moved.get(p, p), base) for p in files}
        bad = sorted(p for p in files if not place[p] or (order and place[p] not in order))
        if bad:
            raise InputError('files in no listed folder (' + (','.join(order) or 'none at --base') + '):\n  '
                             + '\n  '.join(f'{p} -> {moved.get(p, p)}' if p in moved else p for p in bad))
        inc = edges(a.root, base, files)
    except InputError as e:
        print(f'dir_order_check: {e}', file=sys.stderr)
        return 2

    where = 'proposed by ' + a.map if a.map else 'as on disk'
    counts = {}
    for p in files:
        counts[place[p]] = counts.get(place[p], 0) + 1
    print(f'{base}, {where}: {len(files)} files (tests skipped), {len(inc)} includes inside it')
    print('  files by folder: ' + ', '.join(f'{k} {counts[k]}' for k in (order or sorted(counts)) if k in counts))
    fail = False
    if order:
        rank = {k: i for i, k in enumerate(order)}
        up = [(s, n, sp, t) for s, n, sp, t in inc if rank[place[t]] > rank[place[s]]]
        print(f'Upward includes (order {" -> ".join(order)}): {len(up)}')
        for s, n, sp, t in up:
            print(f'  {s}:{n}  [{place[s]} -> {place[t]}]  "{sp}"' + (f'  (to be {moved[s]})' if s in moved else ''))
        fail |= bool(up)
    if a.cycles:
        adj, why = {}, {}
        for s, n, sp, t in inc:
            fs, ft = place[s], place[t]
            if fs != ft:
                adj.setdefault(fs, set()).add(ft)
                why.setdefault((fs, ft), []).append(f'{s}:{n} -> {t}')
        cyc = sccs(set(place.values()), adj)
        print(f'Folder cycles: {len(cyc)}')
        for c in cyc:
            es = [(x, y) for x in c for y in c if y in adj.get(x, ())]
            print(f'  {{{", ".join(c)}}}: {len(es)} folder edges')
            for x, y in es:
                print(f'    {x} -> {y}: {len(why[(x, y)])} includes, e.g. {why[(x, y)][0]}')
        fail |= bool(cyc)
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
