#!/usr/bin/env python3
"""move_files.py: the platform refactor's one move tool (Stage B; build/platform/B1/MOVES.md is its specification).

A move is a map file, tools/refactor/maps/<item>.map: one `old -> new` per line (`→` also works), paths from the repo
root, `#` starts a comment. `src/Metrics/ -> src/Vocabulary/Metrics/` moves a directory (listed from the tree at run
time, with its tests/ folder). `src/Core/club_standards -> src/Vocabulary/Club/` moves a stem: its .h, .cpp and .mm,
whichever exist. A path with an extension moves that one file. A destination ending in `/` keeps the file name.

    python3 tools/refactor/move_files.py <map> --dry-run       # the report; changes nothing
    python3 tools/refactor/move_files.py <map>                 # git mv + every rewrite; refuses a dirty tree
    python3 tools/refactor/move_files.py <map> --over-work     # the same over uncommitted work (another lane's replay)
    python3 tools/refactor/move_files.py <map> --verify <base> # every changed line since <base> is a move's: exit 1 if not
    python3 tools/refactor/move_files.py --self-test           # proves each rule on a fixture tree (ctest: move_files_selftest)
A map line `keep: <file>:<lines>` (docs only; the map is the only source) leaves those lines exactly as they are (a dated
record); --verify passes the old spelling there only, and fails if a kept line changed. A keep that matches nothing is an
error (B1.2c): on a doc the tool does not scan, a stale line number, or a line the map would not rewrite.
A map line `hand: <new file> <- <source file>:<lines>[,<lines>...]` declares a file a brief splits out of another by
hand, after the move (B1.7's capture_time_types.h out of swing_analysis.h); line numbers are the source's before the
move, and each range is one block. The move makes no split. --verify then accepts exactly the split's lines: each block
byte for byte in the new file, in order, whole and in the same namespace; the new file's other lines scaffold only
(blank, comments, #pragma once, includes, an include guard, namespace lines); the blocks gone from the source, which may
gain one include of the new file; an include of the source re-pointed at the new file (listed, anywhere); a build-file
line naming only the new file. Anything else still fails, and a hand: line that matches nothing (no such new file, a
source not in the base, a range past its end, blocks the new file does not hold) is refused.
Options: --root <checkout> (default: this file's checkout); --allow-prose <file> (with --verify: prose edits in that
doc, e.g. the IM rows a brief names, are listed instead of failing); --allow-code-path <file>:<lines> (with --verify:
on exactly those lines of that file, a path in a C++ string changed to the map's image is listed instead of failing,
for a hand edit a brief names, e.g. B1.6's gate_catalogue_rules.h:38-42); --configure <scratch dir> (with --verify:
then cmake -S <root> -B <dir>/app and -S <root>/tests -B <dir>/tests, exit 1 if either fails to configure; run it
through build/refactor/one_at_a_time.sh; without it --verify prints NOT CONFIGURED and exits 3, B1.2b);
--no-configure-ok (the self-test only: --verify without --configure may pass); --seed <build dir> (its app/ and tests/
caches give the configure its toolchain, fetched sources and downloads; default <root>/build); --cmake-arg <arg>
(passed on whatever it starts with, -D... included, after the seed's: it wins); --quiet (totals only).

What it rewrites (MOVES.md items 1-9): the moves (git mv); every `#include "…"` / `#import "…"` of a moved file in src,
tests and tools, by how it resolves (the lint's A1 resolver: relative to the file, then under src/, then under src/Gui/,
then a unique basename): relative -> the path from the file's new home; under src/ or src/Gui/ -> the same style;
through an include directory (a bare name, or any spelling that resolved only by basename) -> the relative path; the
outward includes inside moved files; path mentions in build files (a ${VAR} only where CMake's scopes give it to that
line: CMakeVars walks the configure, function and macro bodies where they are called; a variable set in a sibling
folder, or later in the file, is never used; one the walk can't decide is listed as can't place, never guessed), the lint's
tables, the allowlist, tools/refactor/context_check.py's tables, tools/, packaging/, .github/, docs/developer/,
docs/design/, governance/ (its .md and .txt: the governed artefacts and procedures; B1.2c), BUILDING.md, README.md and
C++ comments. A mention is rewritten only when it is anchored: `src/<old>…`,
`${VAR}/…`, `../…`, or `<old dir>/<a real moved file or folder>`; a bare old folder name (`Metrics/`) is rewritten
only in the tables (lint, allowlist key, context_check.py), and reported anywhere else.

What it never does: add an include directory or an allowlist line; delete a table entry or change a ring or a context
(those are hand edits the brief lists; the report shows each moved file's ring and context before and after); guess an
ambiguous basename (an error: the run refuses); touch a C++ string literal outside the lint (reported: a path in code
is behaviour); touch docs/implementation/ (dated snapshots), QML, or anything under build/ or third_party/. An include
directory that names a moved folder is re-pathed (never added) and reported, so the brief can remove it once nothing
needs it.

Exit codes: 0 done / report clean; 1 errors (nothing changed), --verify found other lines or --configure failed; 2
usage; 3 --verify passed but nothing was configured (not a pass); 77 self-test skipped (no git).
"""
import ast
import bisect
import collections
import difflib
import os
import posixpath
import re
import shutil
import stat
import subprocess
import sys
import tempfile

CPP_EXT = ('.h', '.hpp', '.cpp', '.cc', '.c', '.mm', '.m')
STEM_EXT = ('.h', '.cpp', '.mm')
LINT = 'src/Gui/tests/architecture_lint_test.cpp'
ALLOWLIST = 'src/Gui/tests/architecture_allowlist.txt'
CONTEXT = 'tools/refactor/context_check.py'
SCAN_DIRS = ('src/', 'tests/', 'tools/', 'cmake/', 'packaging/', '.github/', 'docs/developer/', 'docs/design/',
             'governance/')
SCAN_ROOT_FILES = ('CMakeLists.txt', 'BUILDING.md', 'README.md')
NOT_SCANNED = ('tools/refactor/maps/', 'tools/refactor/move_files.py', 'tools/refactor/README.md')
SKIP_PARTS = {'build', 'third_party', '.git', 'node_modules'}
TEXT_EXT = CPP_EXT + ('.txt', '.cmake', '.in', '.py', '.sh', '.zsh', '.bash', '.cmd', '.bat', '.ps1', '.md', '.json',
                      '.yml', '.yaml', '.toml', '.cfg', '.ini', '.iss', '.nsi', '.plist', '.html', '.csv', '.map')
LINT_TABLE_FUNCS = ('dirRings', 'fileRings', 'ownedFiles', 'isRulesIoDir', 'countedStoreDoor', 'a5Exempt',
                    'isRepositoryHeader', 'isAcquisitionOrAppState', 'a3Allowed', 'selfTest')   # selfTest: the fixtures
INC_RE = re.compile(r'^(\s*#\s*(?:include|import)\s*")([^"]+)(".*)$', re.S)
ANGLE_RE = re.compile(r'^\s*#\s*(?:include|import)\s*<([^>]+)>')
# QML/JS: comments are rewritten like C++ comments; their strings (imports, resources) never are.
TOKEN_RE = re.compile(r'(?:\$\{\w+\}|[\w.\-/+])+')


class Fail(Exception):
    pass


def norm(p):
    """A repo-relative posix path, normalised; '' for the root; None if it leaves the checkout."""
    if p in ('', '.'):
        return ''
    q = posixpath.normpath(p)
    if q == '.':
        return ''
    if q == '..' or q.startswith('../') or q.startswith('/'):
        return None
    return q


def relpath(target, base):
    r = posixpath.relpath(target or '.', base or '.')
    return r


def read_text(path):
    with open(path, 'rb') as f:
        data = f.read()
    if b'\0' in data[:8192]:
        return None
    return data.decode('utf-8', 'surrogateescape')


def write_text(path, text):
    with open(path, 'wb') as f:
        f.write(text.encode('utf-8', 'surrogateescape'))


def git(root, *args, check=True, binary=False):
    r = subprocess.run(['git', '-C', root] + list(args), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and r.returncode != 0:
        raise Fail('git %s: %s' % (' '.join(args), r.stderr.decode('utf-8', 'replace').strip()))
    return r.stdout if binary else r.stdout.decode('utf-8', 'surrogateescape')


def list_files(root):
    out = git(root, 'ls-files', '-z', '-co', '--exclude-standard')
    files = set()
    for p in out.split('\0'):
        if p and os.path.isfile(os.path.join(root, p)):
            files.add(p)
    return files


# ── the map ─────────────────────────────────────────────────────────────────────────────────────
def parse_map(text):
    entries = []
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line or line.startswith(('keep:', 'hand:')):
            continue
        parts = re.split(r'\s*(?:->|→)\s*', line)
        if len(parts) != 2 or not parts[0] or not parts[1]:
            raise Fail('map line %d: want "old -> new": %s' % (n, raw))
        old, new = parts
        for p in (old, new):
            if not p.startswith('src/') or norm(p.rstrip('/')) is None or ' ' in p:
                raise Fail('map line %d: a path under src/ from the repo root: %s' % (n, p))
        if old.endswith('/') and not new.endswith('/'):
            raise Fail('map line %d: a directory moves to a directory (end both with /): %s' % (n, raw))
        if old.endswith('/') and (new.startswith(old) or old.startswith(new)):
            raise Fail('map line %d: a directory cannot move into itself or its parent: %s' % (n, raw))
        entries.append((old, new, n))
    if not entries:
        raise Fail('the map has no moves')
    return entries


def parse_lines_spec(arg):
    """'<file>:<line>[-<line>][,...]' -> (file, {lines})."""
    f, _, spec = arg.rpartition(':')
    if not f or not spec:
        raise Fail('want <file>:<lines>: %s' % arg)
    lines = set()
    for part in spec.split(','):
        lo, _, hi = part.partition('-')
        lines.update(range(int(lo), int(hi or lo) + 1))
    return f, lines


def parse_keeps(text):
    """The lines a move leaves exactly as they are: the map's `keep: <file>:<lines>` stanzas, and nothing else, so the
    committed map is the only place a verify can be told to accept an old spelling (a dated record, e.g. a decision or
    change-log row that must say what was true then; prose only: keep_errors). {file: {line numbers}} in the tree
    before the move."""
    keep = collections.defaultdict(set)
    for raw in text.splitlines():
        line = raw.split('#', 1)[0].strip()
        if line.startswith('keep:'):
            f, ls = parse_lines_spec(line[len('keep:'):].strip())
            keep[f] |= ls
    return dict(keep)


def keep_errors(keep):
    """A keep on anything but a doc is refused (B1.2b's review): a kept build, C++ or tool line would hide a stale path
    that no later check sees (an include directory, a code string). A keep on a doc the tool does not scan matches
    nothing, so it is refused too (B1.2c): it would leave its line as it is by default and say nothing (B1.5's IM keeps,
    aimed at governance/ before the tool scanned it)."""
    errs = []
    for f in sorted(keep):
        if file_kind(f) != 'doc':
            errs.append('keep: %s is a %s file: keep: is for dated lines in docs only' % (f, file_kind(f)))
        elif not in_scope(f):
            errs.append('keep: %s matches nothing: the tool does not scan that file (docs: docs/developer/, '
                        'docs/design/, governance/, BUILDING.md, README.md; never docs/implementation/)' % f)
    return errs


# ── hand splits (`hand:`): a new file cut by hand out of another, declared in the map ───────────────────────────
HAND_RE = re.compile(r'^hand:\s*(\S+)\s*(?:<-|←)\s*(\S+)$')


def parse_hands(text):
    """The map's `hand: <new file> <- <source file>:<lines>[,<lines>...]` lines: a header a brief splits out of another
    by hand (B1.7's capture_time_types.h out of swing_analysis.h). Each range is one block, taken whole and unedited;
    line numbers are the source's in the tree before the move, as keep:'s are. -> [(new, source, [(lo, hi)], map line)]."""
    hands = []
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line.startswith('hand:'):
            continue
        m = HAND_RE.match(line)
        f, _, spec = (m.group(2) if m else '').rpartition(':')
        if not m or not f or not re.match(r'^\d+(-\d+)?(,\d+(-\d+)?)*$', spec):
            raise Fail('map line %d: want "hand: <new file> <- <source file>:<lines>": %s' % (n, raw))
        ranges = []
        for part in spec.split(','):
            lo, _, hi = part.partition('-')
            lo, hi = int(lo), int(hi or lo)
            if lo < 1 or hi < lo:
                raise Fail('map line %d: a line range runs from 1 upwards, low to high: %s' % (n, part))
            ranges.append((lo, hi))
        hands.append((m.group(1), f, ranges, n))
    return hands


def hand_errors(hands, plan, files, text_of=None):
    """What a hand: line is refused for before any split is checked (the move and --verify alike): a file that is not
    C++ under src/; a source that is not in the tree (it matches nothing); a new file the map moves or moves to; a line
    two hand: lines both take. With text_of (--verify: the source's text in its base), a range past the source's end."""
    errs, claimed = [], {}
    dests = set(plan.moves.values())
    for new, src, ranges, n in hands:
        for p in (new, src):
            if not p.startswith('src/') or not p.endswith(CPP_EXT) or norm(p) != p:
                errs.append('hand: (map line %d) %s: a hand split is a C++ file under src/, from the repo root' % (n, p))
        if new == src:
            errs.append('hand: (map line %d) %s is split out of itself' % (n, new))
        if new in plan.moves or new in dests:
            errs.append('hand: (map line %d) %s is a move of the map, not a hand split' % (n, new))
        if src not in files:
            errs.append('hand: (map line %d) %s matches nothing: the source is not in the tree before the move' % (n, src))
            continue
        t = text_of(src) if text_of else None
        if text_of and t is None:
            errs.append('hand: (map line %d) %s is not a text file' % (n, src)); continue
        count = len(t.splitlines()) if t is not None else None
        for lo, hi in ranges:
            if count is not None and hi > count:
                errs.append('hand: (map line %d) %s:%d-%d matches nothing: the source has %d lines' % (n, src, lo, hi, count))
            for i in range(lo, hi + 1):
                if (src, i) in claimed:
                    errs.append('hand: (map line %d) %s:%d is taken by map line %d too' % (n, src, i, claimed[(src, i)]))
                    break
                claimed[(src, i)] = n
    return errs


def brace_context(text):
    """For each line (index 0 = line 1, plus one entry past the end), the C++ scope open at its start: a tuple of the
    open namespaces' names (one entry per `::` part) and '{' for any other brace. Comments, strings and preprocessor
    lines are skipped. A close with nothing open is recorded as '?'."""
    cls = spans_cpp(text)
    code = ''.join(ch if c == 'c' or ch == '\n' else ' ' for ch, c in zip(text, cls))
    code = '\n'.join(' ' * len(l) if l.lstrip().startswith('#') else l for l in code.split('\n'))
    starts = [0] + [m.end() for m in re.finditer('\n', code)]
    if code.endswith('\n'):
        starts.pop()
    events = [(m.start(), m) for m in re.finditer(r'\bnamespace\s+([A-Za-z_][\w:]*)?\s*\{|\{|\}', code)]
    stack, out, k = [], [], 0
    for s in starts + [len(code) + 1]:
        while k < len(events) and events[k][0] < s:
            m = events[k][1]
            if m.group(0) == '}':
                if stack:
                    stack.pop()
                else:
                    stack.append('?')
            elif m.group(0) == '{':
                stack.append(('{',))
            else:
                stack.append(tuple('ns:' + x for x in (m.group(1) or '(anonymous)').split('::')))
            k += 1
        out.append(tuple(x for e in stack for x in (e if isinstance(e, tuple) else (e,))))
    return out


SCAFFOLD_RES = (re.compile(r'^#\s*pragma\s+once$'), re.compile(r'^#\s*(?:include|import)\s*(?:"[^"]+"|<[^>]+>)$'),
                re.compile(r'^#\s*endif$'),
                re.compile(r'^namespace\s+[A-Za-z_][\w:]*\s*\{$'), re.compile(r'^\}$'))


def check_hand_file(new, blocks, base_text, nt, old_tree, new_tree, plan):
    """--verify on a declared hand split: each block of its source (in its base) is in the new file byte for byte, in
    the declared order, whole (its braces balance) and in the same scope (namespaces, nesting); every other line is
    scaffold (scaffold_lines); the braces balance; every include resolves, never through an include directory, and a
    taken one names the map's image of what it named in the source. -> (bad lines, the reviewer's line or None)."""
    bad = []
    if nt is None:
        return ['hand: %s is not a text file' % new], None
    nl = nt.splitlines(keepends=True)
    ctx_n = brace_context(nt)
    used, cur, ctx_cache, srcs = {}, 0, {}, []
    for src, (lo, hi), n in blocks:
        st = base_text(src)
        if src not in ctx_cache:
            ctx_cache[src] = brace_context(st)
        ctx_s, block = ctx_cache[src], st.splitlines(keepends=True)[lo - 1:hi]
        if ctx_s[lo - 1] != ctx_s[hi]:
            bad.append('hand: (map line %d) %s:%d-%d is not whole: its braces do not balance' % (n, src, lo, hi))
        k = next((k for k in range(cur, len(nl) - len(block) + 1) if nl[k:k + len(block)] == block), None)
        if k is None:
            bad.append('hand: (map line %d) %s matches nothing: it does not hold %s:%d-%d byte for byte%s' % (
                n, new, src, lo, hi, ' after the blocks declared before it' if cur else ''))
            continue
        if ctx_n[k] != ctx_s[lo - 1]:
            bad.append('hand: (map line %d) %s:%d: %s:%d-%d sits in %s there, in %s in its source' % (
                n, new, k + 1, src, lo, hi, '::'.join(ctx_n[k]) or 'the global scope', '::'.join(ctx_s[lo - 1]) or
                'the global scope'))
        for x in range(hi - lo + 1):
            used[k + x] = (src, lo + x)
        cur = k + len(block)
        srcs.append('%s:%d-%d' % (src, lo, hi))
    scaffold = scaffold_lines(nt)
    for i, line in enumerate(nl):
        if i not in used and i + 1 not in scaffold:
            bad.append('%s:%d: in a hand split, a line that is neither taken from its source nor scaffold (blank, a '
                       'comment, #pragma once, an include, a guard, a namespace line): %s' % (new, i + 1, line.rstrip('\r\n')))
        m = INC_RE.match(line.rstrip('\r\n'))
        if not m:
            continue
        h1, t1 = new_tree.resolve(new, m.group(2))
        if h1 in (None, 'ambiguous', 'base'):
            bad.append('%s:%d: "%s" %s' % (new, i + 1, m.group(2), 'resolves only through an include directory'
                                           if h1 == 'base' else 'does not resolve'))
        elif i in used:
            src, sl = used[i]
            h0, t0 = old_tree.resolve(src, m.group(2))
            if h0 not in (None, 'ambiguous') and plan.fwd('src/' + t0) != 'src/' + t1:
                bad.append('%s:%d: "%s" resolves to %s, not %s (what it named in %s:%d)' % (
                    new, i + 1, m.group(2), 'src/' + t1, plan.fwd('src/' + t0), src, sl))
    if ctx_n[-1]:
        bad.append('%s: its braces do not balance at the end (%s still open)' % (new, '::'.join(ctx_n[-1])))
    if bad:
        return bad, None
    return bad, 'hand split %s: %d lines taken byte for byte from %s; %d other lines, all scaffold: read by the reviewer' % (
        new, len(used), ', '.join(srcs) or 'nothing', len(nl) - len(used))


def scaffold_lines(text):
    """Line numbers (1-based) of a C++ file's scaffold: blank, a comment, #pragma once, an #include or #import, an
    include guard (#ifndef G, then #define G with no value, #endif), a namespace opened or closed; a trailing comment
    allowed. Any other #define is not scaffold: it could switch code on in every file that includes the new one."""
    cls = spans_cpp(text)
    out, off, guard = set(), 0, None
    for n, line in enumerate(text.splitlines(keepends=True), 1):
        body = ''.join(ch if c != 'm' else ' ' for ch, c in zip(line, cls[off:off + len(line)])).strip()
        off += len(line)
        g = re.match(r'^#\s*ifndef\s+(\w+)$', body)
        d = re.match(r'^#\s*define\s+(\w+)$', body)
        if not body or g or (d and d.group(1) == guard) or any(r.match(body) for r in SCAFFOLD_RES):
            out.add(n)
        if body:
            guard = g.group(1) if g else None                      # a guard's #define comes right after its #ifndef
    return out


class Plan:
    """The map expanded against a tree. moves: old file -> new file (every move the map makes, done or pending)."""

    def __init__(self, entries, files):
        self.entries = entries
        self.files = set(files)
        self.keep = {}                                             # {file: {lines}} left as they are (--keep, keep:)
        self.moves, self.stems, self.dirs = {}, {}, []
        errors = []
        # file and stem lines first, so a folder line never claims a file another line put in its destination
        for old, new, n in sorted(entries, key=lambda e: e[0].endswith('/')):
            if old.endswith('/'):
                self.dirs.append((old, new))
                at_old = [f for f in files if f.startswith(old)]
                at_new = [f for f in files if f.startswith(new)]
                for f in at_old:
                    self.moves[f] = new + f[len(old):]
                claimed = set(self.moves.values())
                for f in at_new:
                    o = old + f[len(new):]
                    if f not in claimed:
                        self.moves.setdefault(o, f)
                if not at_old and not at_new:
                    errors.append('map line %d: nothing at %s or %s' % (n, old, new))
                continue
            base, ext = posixpath.splitext(old)
            cands = [old] if ext in CPP_EXT or ext in TEXT_EXT else [old + e for e in STEM_EXT]
            stem_old = base if ext else old
            if new.endswith('/'):
                dest = lambda c: new + posixpath.basename(c)
                stem_new = new + posixpath.basename(stem_old)
            else:
                nb, ne = posixpath.splitext(new)
                dest = (lambda c: new) if ext else (lambda c: new + posixpath.splitext(c)[1])
                stem_new = nb if ext else new
            hit = False
            for c in cands:
                d = dest(c)
                if c in files or d in files:
                    self.moves[c] = d
                    hit = True
            if not hit:
                errors.append('map line %d: no file at %s (or its destination)' % (n, old))
            elif not ext:
                self.stems[stem_old] = stem_new
        seen = {}
        for o, d in sorted(self.moves.items()):
            if o in files and d in files:
                errors.append('both %s and its destination %s exist' % (o, d))
            if d in seen:
                errors.append('%s and %s both move to %s' % (seen[d], o, d))
            seen[d] = o
        self.errors = errors
        self.pending = {o: d for o, d in self.moves.items() if o in files}
        self.done = {o: d for o, d in self.moves.items() if o not in files and d in files}
        self.dirs.sort(key=lambda x: -len(x[0]))
        self.back = {d: o for o, d in self.moves.items()}
        # the tree as it will be, and the union of the old and new homes (stale spellings still resolve)
        self.final = (self.files - set(self.pending)) | set(self.pending.values())
        self.virtual_old = (self.files - set(self.done.values())) | set(self.done)
        self.union = self.files | self.virtual_old

    def fwd(self, p):
        """Where a path (file, stem or folder, repo-relative) is after the move."""
        if p is None:
            return None
        if p in self.moves:
            return self.moves[p]
        if p in self.stems:
            return self.stems[p]
        for old, new in self.dirs:
            o = old.rstrip('/')
            if p == o or p.startswith(old):
                return new.rstrip('/') + p[len(o):]
        return p

    def moved(self, p):
        return p is not None and self.fwd(p) != p

    def is_move_root(self, p):
        """A moved folder itself (a bare `Metrics/` mention is not anchored)."""
        return any(p == old.rstrip('/') for old, _ in self.dirs)


class Tree:
    """Resolution over a set of repo files: the A1 resolver of architecture_lint_test.cpp."""

    def __init__(self, files):
        self.files = set(files)
        self.src = {f[4:] for f in files if f.startswith('src/') and f.endswith(CPP_EXT)}
        self.by_name = collections.defaultdict(list)
        for r in self.src:
            self.by_name[posixpath.basename(r)].append(r)
        self.dirs = set()
        for f in files:
            d = posixpath.dirname(f)
            while d and d not in self.dirs:
                self.dirs.add(d)
                d = posixpath.dirname(d)

    def resolve(self, frm, inc):
        """-> (how, src-relative target or candidates). how: rel | src | gui | base | ambiguous | None."""
        c = norm(posixpath.join(posixpath.dirname(frm), inc))
        if c and c.startswith('src/') and c[4:] in self.src:
            return 'rel', c[4:]
        c = norm(inc)
        if c and c in self.src:
            return 'src', c
        c = norm('Gui/' + inc)
        if c and c in self.src:
            return 'gui', c
        same = self.by_name.get(posixpath.basename(inc), [])
        if len(same) == 1:
            return 'base', same[0]
        if len(same) > 1:
            return 'ambiguous', sorted(same)
        return None, None

    def exists(self, p):
        """A file, a stem (with any C++ extension) or a folder."""
        if p is None:
            return None
        if p == '':
            return 'dir'
        if p in self.files:
            return 'file'
        if p in self.dirs:
            return 'dir'
        if any(p + e in self.files for e in CPP_EXT):
            return 'stem'
        return None


# ── text spans ──────────────────────────────────────────────────────────────────────────────────
def spans_cpp(text):
    """Per-character class: c code, m comment, s string/char literal content."""
    n, i, out = len(text), 0, ['c'] * len(text)
    while i < n:
        ch = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out[i:j] = ['m'] * (j - i); i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if text[k] != '\n':
                    out[k] = 'm'
            i = j
        elif ch == '"' and i > 0 and text[i - 1] == 'R' and (i < 2 or not (text[i - 2].isalnum() or text[i - 2] == '_')):
            m = re.match(r'"([^()\\\s]{0,16})\(', text[i:])
            if not m:
                i += 1; continue
            close = ')' + m.group(1) + '"'
            j = text.find(close, i + m.end())
            j = n if j < 0 else j + len(close)
            for k in range(i + 1, j - 1):
                if text[k] != '\n':
                    out[k] = 's'
            i = j
        elif ch in '"\'':
            if ch == "'" and i > 0 and (text[i - 1].isalnum()) and i + 1 < n and text[i + 1].isalnum():
                i += 1; continue                                  # a digit separator 1'000
            j = i + 1
            while j < n and text[j] != ch and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                out[k] = 's'
            i = j + 1
        else:
            i += 1
    return out


def spans_py(text):
    n, i, out = len(text), 0, ['c'] * len(text)
    while i < n:
        ch = text[i]
        if ch == '#':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out[i:j] = ['m'] * (j - i); i = j
        elif ch in '"\'':
            q = text[i:i + 3] if text[i:i + 3] in ('"""', "'''") else ch
            j = i + len(q)
            while j < n and not text.startswith(q, j) and (len(q) == 3 or text[j] != '\n'):
                j += 2 if text[j] == '\\' else 1
            for k in range(i + len(q), min(j, n)):
                if text[k] != '\n':
                    out[k] = 's'
            i = j + len(q)
        else:
            i += 1
    return out


def spans_cmake(text):
    n, i, out = len(text), 0, ['c'] * len(text)
    while i < n:
        ch = text[i]
        if ch == '#':
            if text.startswith('#[[', i) or text.startswith('#[=', i):
                m = re.match(r'#\[(=*)\[', text[i:])
                close = ']' + (m.group(1) if m else '') + ']'
                j = text.find(close, i)
                j = n if j < 0 else j + len(close)
            else:
                j = text.find('\n', i)
                j = n if j < 0 else j
            for k in range(i, j):
                if text[k] != '\n':
                    out[k] = 'm'
            i = j
        elif ch == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                if text[k] != '\n':
                    out[k] = 's'
            i = j + 1
        else:
            i += 1
    return out


def file_kind(path):
    b = posixpath.basename(path)
    if path == LINT:
        return 'lint'
    if path == ALLOWLIST:
        return 'allowlist'
    if path == CONTEXT:
        return 'context'
    if b == 'CMakeLists.txt' or b.endswith('.cmake') or b.endswith('.cmake.in'):
        return 'build'
    if path.endswith(CPP_EXT):
        return 'cpp'
    if path.endswith(('.qml', '.js')):
        return 'script'                                            # QML/JS: comments only
    if path.endswith('.py'):
        return 'py'
    if path.startswith(('docs/', 'governance/')) or path.endswith('.md'):
        return 'doc'
    return 'text'


def in_scope(path):
    if any(path.startswith(p) for p in NOT_SCANNED) or set(path.split('/')[:-1]) & SKIP_PARTS:
        return False
    if path.startswith('docs/') and not path.startswith(('docs/developer/', 'docs/design/')):
        return False
    if not (path in SCAN_ROOT_FILES or path.startswith(SCAN_DIRS)):
        return False
    b = posixpath.basename(path)
    if path.startswith(('src/', 'tests/')):
        return path.endswith(CPP_EXT) or file_kind(path) in ('build', 'lint', 'allowlist') \
            or b.endswith(('.py', '.sh', '.qml', '.js'))
    if path.startswith(('docs/', 'governance/')):                  # prose only: tokens.json is generated
        return b.endswith(('.md', '.txt'))
    return b.endswith(TEXT_EXT) or b == 'CMakeLists.txt'


# ── CMake variables and include-directory contexts ──────────────────────────────────────────────
ROOTM = '@ROOT@'
CANT_PLACE = 'CMake scope: '                                       # the prefix of a reason the CMake model gives (B1.2b)
CMD_RE = re.compile(r'([A-Za-z_]\w*)[ \t]*\(')
WORD_RE = re.compile(r'"((?:\\.|[^"\\])*)"|([^\s()"]+)')
LIST_OUT_LAST = ('GET', 'LENGTH', 'FIND', 'JOIN', 'SUBLIST')


def cmake_base(path):
    """CMAKE_CURRENT_SOURCE_DIR for a build file (a .cmake under a cmake/ folder is included from its parent): the
    folder a relative path in it is read from. Variables come from CMakeVars' walk, not from here."""
    d = posixpath.dirname(path)
    if posixpath.basename(path) != 'CMakeLists.txt' and posixpath.basename(d) == 'cmake':
        return posixpath.dirname(d)
    return d


def _p(d):
    return ROOTM + ('/' + d if d else '')


def _repo(v):
    """A value that names a place in the checkout -> its repo path ('' for the root); anything else -> None."""
    if not isinstance(v, str) or ';' in v or not (v == ROOTM or v.startswith(ROOTM + '/')):
        return None
    return norm(v[len(ROOTM):].lstrip('/'))


def _canon(v):
    if isinstance(v, str) and (v == ROOTM or v.startswith(ROOTM + '/')) and ';' not in v:
        r = _repo(v)
        return None if r is None else _p(r)
    return v


def cmake_commands(text):
    """[(name, words, first line, last line)]: each command outside comments and strings, in order, its arguments
    unquoted (a comment inside the arguments dropped)."""
    cls = spans_cmake(text)
    starts = [0] + [k + 1 for k, ch in enumerate(text) if ch == '\n']
    out, i, n = [], 0, len(text)
    while True:
        m = CMD_RE.search(text, i)
        if not m:
            break
        s = m.start()
        if cls[s] != 'c' or (s and (text[s - 1].isalnum() or text[s - 1] in '_${')):
            i = m.end(); continue
        j, depth = m.end(), 1
        while j < n and depth:
            if cls[j] == 'c':
                depth += {'(': 1, ')': -1}.get(text[j], 0)
            j += 1
        args = ''.join(ch if cls[k] != 'm' else ' ' for k, ch in enumerate(text[m.end():j - 1], m.end()))
        words = [q.replace('\\"', '"') if q or not a else a for q, a in WORD_RE.findall(args)]
        out.append((m.group(1).lower(), words, bisect.bisect_right(starts, s), bisect.bisect_right(starts, j - 1)))
        i = j
    return out


ABSENT = object()                                                  # a variable a scope does not hold


class Doubt(collections.namedtuple('Doubt', 'cands why')):
    """A value the walk can't decide (B1.2b): cands, the values it may have (None for empty or unreadable), and why."""
    __slots__ = ()


def _cands(v):
    if isinstance(v, Doubt):
        return set(v.cands)
    return {v} if isinstance(v, str) else {None}


class _Frame:
    """One reading of one build file, or of one function or macro body in it (span: its first and last line): the
    variables it starts with and each change in its scope, by line."""
    __slots__ = ('path', 'entry', 'changes', 'line', 'span')         # changes: (line, name, value, pass)

    def __init__(self, path, entry, span=None):
        self.path, self.entry, self.changes, self.line, self.span = path, entry, [], 0, span


class _State:
    """One variable scope (a directory's, or a function call's): its variables, the scope PARENT_SCOPE writes to,
    and the frames reading in it (a file and the files it include()s)."""

    def __init__(self, vars_, parent=None):
        self.vars, self.parent, self.frames = vars_, parent, []


class _Return(Exception):
    pass


class LineInfo:
    """What one build-file line knows: known {variable: repo path}; doubts {variable: (repo paths it may name, None
    for empty or unreadable; why)}; body: None, or 'called' / 'uncalled' for a line in a function or macro body."""
    __slots__ = ('known', 'doubts', 'body')

    def __init__(self, known, doubts, body):
        self.known, self.doubts, self.body = known, doubts, body


class CMakeVars:
    """Which variables a build file knows, line by line, as CMake's own scopes give them (B1.2a, B1.2b).

    It walks the tree as a configure does, from every CMakeLists that calls project() (the app's root, tests/, each
    suite that also runs standalone, src/Buffer), in statement order: add_subdirectory() gives the child a copy of the
    scope at that line; include() reads the file in the includer's scope (a module name through CMAKE_MODULE_PATH);
    foreach() with literal items is expanded; a CACHE INTERNAL/FORCE set is seen from then on, a cache default is not.
    A function() or macro() body is read where it is CALLED, in the caller's scope (a function in a copy of it, with
    its arguments; a macro in the caller's own scope, so its sets change the caller's variables); a body line knows
    only what every call agrees on, and a body no call reaches knows nothing. `if(NOT COMMAND x)` and
    `if(PROJECT_IS_TOP_LEVEL)` are decided; any other condition reads each branch from the state before the if, and
    after endif() a variable the branches (or the branch not taken) leave different is undecided. unset() unsets;
    set(X v PARENT_SCOPE) writes the parent's X after the add_subdirectory() or the call; return() ends the file or
    function, and one that runs only on some paths leaves what follows it undecided for the includer or caller. A
    file reached more than once knows a variable only where every reading agrees on its value. An undecided value is
    a Doubt with a reason, so a line that uses it is reported "can't place", never guessed. A build file nothing
    reaches (a `cmake -P` script) knows only its own sets and CMAKE_CURRENT_LIST_DIR; `unreached` lists them."""

    def __init__(self, texts):
        self.texts = texts
        self.cmds = {p: cmake_commands(t) for p, t in texts.items()}
        self.all_funcs = {w[0].lower() for cs in self.cmds.values() for c, w, _, _ in cs if c in ('function', 'macro') and w}
        self.spans = {}                                            # file -> every function/macro body's (first, last)
        for p, cs in self.cmds.items():
            sp = []
            for k, (c, w, first, _) in enumerate(cs):
                if c in ('function', 'macro'):
                    e = self._end(cs, k, ('function', 'macro'), ('endfunction', 'endmacro'))
                    sp.append((first, cs[min(e, len(cs) - 1)][3]))
            self.spans[p] = sp
        self.setters = collections.defaultdict(set)
        for p, cs in self.cmds.items():
            for c, w, _, _ in cs:
                if c in ('set', 'get_filename_component') and w and '$' not in w[0]:
                    self.setters[w[0]].add(p)
        self.runs = collections.defaultdict(list)
        self._bp = {}
        roots = [p for p in sorted(texts) if posixpath.basename(p) == 'CMakeLists.txt'
                 and any(c == 'project' for c, _, _, _ in self.cmds[p])]
        for p in roots:
            self._root(p, posixpath.dirname(p))
        self.unreached = []
        for p in sorted(texts):
            if not any(fr.span is None for fr in self.runs.get(p, ())):
                self.unreached.append(p)
                self._root(p, None)

    # ── the walk
    def _root(self, path, top):
        self.cache, self.funcs, self.defs, self.active, self._base, self._rets = {}, set(), {}, [], [], []
        self._tag = self._ntag = 0                                 # a foreach pass: one line, two passes, two values
        v = {'CMAKE_CURRENT_LIST_DIR': _p(posixpath.dirname(path))}
        if posixpath.basename(path) == 'CMakeLists.txt':          # always read as its own directory
            v['CMAKE_CURRENT_SOURCE_DIR'] = _p(posixpath.dirname(path))
        if top is not None:
            v['CMAKE_SOURCE_DIR'] = _p(top)
        self._run(path, self.cmds[path], _State(v), (), 0)

    def _get(self, st, name):
        return st.vars[name] if name in st.vars else self.cache.get(name)

    def _sub(self, s, st):
        """A word with its variables read: a string, None (unknown), or a Doubt (it uses an undecided variable)."""
        if '$ENV{' in s or '$<' in s or '${${' in s:
            return None
        bad, doubt = [], {}

        def one(m):
            v = self._get(st, m.group(1))
            if isinstance(v, Doubt):
                doubt.setdefault(m.group(1), v)
                return ''
            if v is None:
                bad.append(m.group(1))
                return ''
            return v
        r = re.sub(r'\$\{(\w+)\}', one, s)
        if doubt:
            name, d = next(iter(doubt.items()))
            cands = {None}
            if not bad and len(doubt) == 1:
                cands = set()
                for c in d.cands:
                    cands.add(None if c is None else re.sub(r'\$\{(\w+)\}', lambda m: c if m.group(1) == name
                                                             else self._get(st, m.group(1)), s))
            return Doubt(frozenset(cands), d.why)
        return None if bad or '${' in r else r

    def _rel(self, r, st):
        if r is None:
            return None
        if r == ROOTM or r.startswith(ROOTM + '/'):
            return _repo(r)
        base = _repo(self._get(st, 'CMAKE_CURRENT_SOURCE_DIR'))
        if base is None or r.startswith('/') or re.match(r'[A-Za-z]:', r) or ROOTM in r:
            return None
        return norm(posixpath.join(base, r))

    def _path(self, s, st):
        """A word as a repo path: a variable's place, or relative to CMAKE_CURRENT_SOURCE_DIR; None if unknown; a
        Doubt of repo paths if it uses an undecided variable."""
        r = self._sub(s, st)
        if isinstance(r, Doubt):
            return Doubt(frozenset(self._rel(c, st) for c in r.cands), r.why)
        return self._rel(r, st)

    def _record(self, frames, name, val):
        for fr in frames:
            fr.changes.append((fr.line, name, val, self._tag))

    def _assign(self, st, name, val, cache=False):
        val = _canon(val)
        st.vars[name] = val
        if cache:
            self.cache[name] = val
        self._record(self.active if cache else st.frames, name, val)

    def _unset(self, st, name, where):
        st.vars.pop(name, None)
        c = self.cache.get(name)
        val = c if c is not None else Doubt(frozenset([None]), 'unset() at %s:%d' % where)
        if c is None:
            st.vars[name] = val                                    # read as empty: never a path
        self._record(st.frames, name, val)

    # an undecided if: each branch from the state before it, merged at endif
    def _snap(self, st):
        return dict(st.vars), dict(self.cache)

    def _set_to(self, st, vars_, cache):
        for name in set(st.vars) | set(vars_):
            a, b = st.vars.get(name, ABSENT), vars_.get(name, ABSENT)
            if a != b:
                if b is ABSENT:
                    del st.vars[name]
                else:
                    st.vars[name] = b
                v = st.vars.get(name, self.cache.get(name))
                self._record(st.frames, name, v)
        for name in set(self.cache) | set(cache):
            a, b = self.cache.get(name, ABSENT), cache.get(name, ABSENT)
            if a != b:
                if b is ABSENT:
                    del self.cache[name]
                else:
                    self.cache[name] = b
                self._record(self.active, name, st.vars.get(name, self.cache.get(name)))

    @staticmethod
    def _merged(dicts, why):
        out = {}
        for name in set().union(*dicts):
            vals = [d.get(name, ABSENT) for d in dicts]
            if all(v == vals[0] for v in vals[1:]):
                if vals[0] is not ABSENT:
                    out[name] = vals[0]
                continue
            cands = set()
            for v in vals:
                cands |= _cands(None if v is ABSENT else v)
            out[name] = Doubt(frozenset(cands), why)
        return out

    def _merge(self, st, snaps, why):
        self._set_to(st, self._merged([s[0] for s in snaps], why), self._merged([s[1] for s in snaps], why))

    def _run(self, path, cmds, st, stack, unk, span=None, key=None):
        """Read `cmds` (a whole file, or a function body: span) in the scope `st`."""
        key = key or path
        if key in stack or len(stack) > 60:
            return
        fr = _Frame(path, dict(self.cache, **st.vars), span)
        self.runs[path].append(fr)
        st.frames.append(fr); self.active.append(fr); self._base.append(unk); self._rets.append([])
        try:
            try:
                self._block(path, cmds, st, stack + (key,), unk)
            except _Return:
                pass
            rets = self._rets[-1]
            if rets:
                self._merge(st, [self._snap(st)] + [s for s, _ in rets],
                            'return() at %s:%d ends %s on some paths only' % (path, rets[0][1], posixpath.basename(path)))
        finally:
            st.frames.pop(); self.active.pop(); self._base.pop(); self._rets.pop()

    def _cond(self, w, st):
        neg, ws = False, list(w)
        while ws and ws[0].upper() == 'NOT':
            neg, ws = not neg, ws[1:]
        v = None
        if len(ws) == 2 and ws[0].upper() == 'COMMAND':
            name = ws[1].lower()
            v = True if name in self.funcs else (False if name in self.all_funcs else None)
        elif ws == ['PROJECT_IS_TOP_LEVEL']:
            v = {'ON': True, 'OFF': False}.get(self._get(st, 'PROJECT_IS_TOP_LEVEL'))
        return None if v is None else v != neg

    @staticmethod
    def _end(cmds, k, opens, closes):
        depth = 0
        for e in range(k, len(cmds)):
            if cmds[e][0] in opens:
                depth += 1
            elif cmds[e][0] in closes:
                depth -= 1
                if not depth:
                    return e
        return len(cmds)

    def _block(self, path, cmds, st, stack, unk):
        """Read commands in order; True when the rest is never reached (a return() on every path that got here)."""
        fr = st.frames[-1]
        ifs = []                                                   # one dict per open if: see _if_*
        k = 0
        while k < len(cmds):
            c, w, first, last = cmds[k]
            fr.line = last
            off = any(not x['read'] for x in ifs)
            outer_off = any(not x['read'] for x in ifs[:-1])
            if c in ('function', 'macro'):
                e = self._end(cmds, k, ('function', 'macro'), ('endfunction', 'endmacro'))
                if not off and w:
                    name = w[0].lower()
                    self.funcs.add(name)
                    self.defs[name] = (c, path, cmds[k + 1:e], w[1:], (first, cmds[min(e, len(cmds) - 1)][3]))
                k = e + 1
                continue
            dead = False
            if c == 'if':
                if off:
                    ifs.append(dict(read=False, taken=True, snap=None, ends=[], els=False, fell=False, dead=False))
                else:
                    v = self._cond(w, st)
                    ifs.append(dict(read=v is not False, taken=v, snap=self._snap(st) if v is None else None, ends=[],
                                    els=False, fell=False, dead=False, line=first))
            elif c in ('elseif', 'else') and ifs:
                e = ifs[-1]
                if not outer_off:
                    self._if_close(e, st)
                if c == 'elseif':
                    if outer_off or e['taken'] is True:
                        e['read'] = False
                    else:
                        v = self._cond(w, st)
                        if v is None and e['snap'] is None:
                            e['snap'], e['line'] = self._snap(st), first
                        e['read'] = v is not False
                        t = e['taken']
                        e['taken'] = True if (v is True and t is False) else (None if (v is None or t is None) else False)
                else:
                    e['read'] = not outer_off and e['taken'] is not True
                    e['els'] = e['read']
            elif c == 'endif' and ifs:
                e = ifs.pop()
                if not any(not x['read'] for x in ifs):
                    self._if_close(e, st, False)
                    implicit = not (e['taken'] is True or e['els'])
                    if e['snap'] is not None:
                        paths = e['ends'] + ([e['snap']] if implicit else [])
                        if paths:
                            self._merge(st, paths, 'set under the undecided if at %s:%d' % (path, e['line']))
                    dead = not (e['fell'] or implicit)
            elif off:
                pass
            else:
                u = unk + sum(1 for x in ifs if x['taken'] is None)
                if c == 'foreach' and w:
                    e = self._end(cmds, k, ('foreach',), ('endforeach',))
                    dead = self._foreach(path, cmds[k + 1:e], w, st, stack, u, cmds[min(e, len(cmds) - 1)][3])
                    k = e
                elif c == 'return':
                    if u == self._base[-1]:
                        raise _Return()                            # on every path here: the rest is never read
                    self._rets[-1].append((self._snap(st), first))
                    dead = True
                else:
                    dead = self._command(c, w, st, stack, u, (path, first))
            if dead:
                if not ifs:
                    return True
                ifs[-1]['dead'], ifs[-1]['read'] = True, False
            k += 1
        return False

    def _if_close(self, e, st, restore=True):
        """The end of a branch being read: keep its state for the merge, then go back to the state before the if (at
        endif the merge sets it instead)."""
        if e['snap'] is None:
            e['fell'] = e['fell'] or (e['read'] and not e['dead'])
            e['dead'] = False
            return
        if e['read'] and not e['dead']:
            e['fell'] = True
            e['ends'].append(self._snap(st))
        e['dead'] = False
        if restore:
            self._set_to(st, *e['snap'])

    def _foreach(self, path, body, w, st, stack, unk, end_line):
        var, items = w[0], None
        if len(w) > 2 and w[1] == 'IN' and w[2] == 'ITEMS':
            words = w[3:]
        elif len(w) > 1 and w[1] in ('IN', 'RANGE'):
            words = None
        else:
            words = w[1:]
        if words is not None:
            subs = [self._sub(x, st) for x in words]
            items = None if any(not isinstance(x, str) for x in subs) else [y for x in subs for y in x.split(';') if y]
        prev = st.vars.get(var, ABSENT)
        self._assign(st, var, items[0] if items and len(items) == 1 else None)
        dead, tag = False, self._tag
        try:
            for it in (items if items else [None]):
                st.vars[var] = it
                self._ntag += 1
                self._tag = self._ntag
                if self._block(path, body, st, stack, unk):
                    dead = True
                    break
        finally:
            self._tag = tag
            st.frames[-1].line = end_line
            if prev is ABSENT:
                st.vars.pop(var, None)
                self._record(st.frames, var, self.cache.get(var))
            else:
                self._assign(st, var, prev)
        return dead

    def _call(self, name, w, st, stack, unk):
        """A function or macro call: its body read here, in the caller's scope. True if a macro's return() ends the
        caller on every path."""
        kind, dpath, body, params, span = self.defs[name]
        key = ('call', name)
        if key in stack or len(stack) > 60:
            return False
        args = [_canon(self._sub(x, st)) for x in w]
        joined = lambda xs: ';'.join(xs) if all(isinstance(x, str) for x in xs) else None
        bind = {'ARGC': str(len(args)), 'ARGV': joined(args), 'ARGN': joined(args[len(params):])}
        for i, a in enumerate(args):
            bind['ARGV%d' % i] = a
        for i, p in enumerate(params):
            bind[p] = args[i] if i < len(args) else None
        if kind == 'function':
            v = dict(st.vars)
            v.update(bind)
            v['CMAKE_CURRENT_FUNCTION_LIST_DIR'] = _p(posixpath.dirname(dpath))
            self._run(dpath, body, _State(v, st), stack, unk, span, key)
            return False
        saved = {k: st.vars.get(k, ABSENT) for k in bind}         # a macro's arguments are text, not the caller's
        st.vars.update(bind)                                       # variables: bound unrecorded, then put back
        fr = _Frame(dpath, dict(self.cache, **st.vars), span)
        self.runs[dpath].append(fr)
        st.frames.append(fr); self.active.append(fr)
        try:
            return self._block(dpath, body, st, stack + (key,), unk)
        finally:
            st.frames.pop(); self.active.pop()
            for k, v in saved.items():
                if v is ABSENT:
                    st.vars.pop(k, None)
                else:
                    st.vars[k] = v

    def _command(self, c, w, st, stack, unk, where):
        if c in self.defs:
            return self._call(c, w, st, stack, unk)
        if c == 'set' and w and '$' not in w[0]:
            vals = w[1:]
            if 'PARENT_SCOPE' in vals:
                par = st.parent
                if par is not None:
                    subs = [self._sub(x, st) for x in vals[:vals.index('PARENT_SCOPE')]]
                    val = self._joined(subs)
                    if unk or self._rets[-1]:                      # only on some paths: the parent's value is undecided
                        why = 'set(%s … PARENT_SCOPE) at %s:%d runs on some paths only' % ((w[0],) + where)
                        val = Doubt(frozenset(_cands(self._get(par, w[0])) | _cands(val)), why)
                    if val is None and not vals[:vals.index('PARENT_SCOPE')]:
                        self._unset(par, w[0], where)
                    else:
                        self._assign(par, w[0], val)
                return False
            cache = 'CACHE' in vals
            if cache:
                force = 'FORCE' in vals or 'INTERNAL' in vals
                vals = vals[:vals.index('CACHE')]
                if w[0] in self.cache and not force:
                    return False
            if not vals and not cache:
                self._unset(st, w[0], where)                       # set(X): unsets X
                return False
            val = self._joined([self._sub(x, st) for x in vals]) if vals else None
            if cache and not force:
                val = None                                         # a cache default: a -D on the command line wins
            self._assign(st, w[0], val, cache)
        elif c == 'unset' and w and '$' not in w[0]:
            flags = [x.upper() for x in w[1:]]
            if 'CACHE' in flags:
                self.cache.pop(w[0], None)
                self._record(self.active, w[0], st.vars.get(w[0]))
            elif 'PARENT_SCOPE' in flags:
                if st.parent is not None:
                    self._unset(st.parent, w[0], where)
            else:
                self._unset(st, w[0], where)
        elif c == 'get_filename_component' and len(w) >= 3 and '$' not in w[0]:
            mode = w[2].upper()
            p = self._path(w[1], st) if mode in ('ABSOLUTE', 'REALPATH', 'DIRECTORY', 'PATH') else None
            dirn = mode in ('DIRECTORY', 'PATH')
            if isinstance(p, Doubt):
                val = Doubt(frozenset(None if x is None else _p(posixpath.dirname(x) if dirn else x) for x in p.cands), p.why)
            else:
                if p is not None and dirn:
                    p = posixpath.dirname(p)
                val = None if p is None else _p(p)
            self._assign(st, w[0], val, 'CACHE' in w[3:])
        elif c == 'list' and len(w) >= 2:
            op = w[0].upper()
            if w[1] == 'CMAKE_MODULE_PATH' and op in ('APPEND', 'PREPEND'):
                mp = self._get(st, 'CMAKE_MODULE_PATH')
                cur = [x for x in mp.split(';') if x] if isinstance(mp, str) else ([] if mp is None else ['?'])
                add = [_p(p) if isinstance(p, str) else '?' for p in (self._path(x, st) for x in w[2:])]
                self._assign(st, 'CMAKE_MODULE_PATH', ';'.join(cur + add if op == 'APPEND' else add + cur))
            elif '$' not in w[-1] and '$' not in w[1]:
                self._assign(st, w[-1] if op in LIST_OUT_LAST else w[1], None)
        elif c == 'project':
            here = self._get(st, 'CMAKE_CURRENT_SOURCE_DIR')      # what project() sets does not hang on a condition
            self._assign(st, 'PROJECT_SOURCE_DIR', here)
            if w and '$' not in w[0]:
                self._assign(st, w[0] + '_SOURCE_DIR', here)
            top = here is not None and here == self._get(st, 'CMAKE_SOURCE_DIR')
            self._assign(st, 'PROJECT_IS_TOP_LEVEL', 'ON' if top else 'OFF')
        elif c == 'include' and w:
            t = self._include_target(w[0], st)
            if t is not None:
                prev = st.vars.get('CMAKE_CURRENT_LIST_DIR')
                st.vars['CMAKE_CURRENT_LIST_DIR'] = _p(posixpath.dirname(t))
                self._run(t, self.cmds[t], st, stack, unk)
                st.vars['CMAKE_CURRENT_LIST_DIR'] = prev
        elif c == 'add_subdirectory' and w:
            d = self._path(w[0], st)
            d = d if isinstance(d, str) else None
            t = None if d is None else posixpath.join(d, 'CMakeLists.txt') if d else 'CMakeLists.txt'
            if t in self.texts:
                v = dict(st.vars)
                v['CMAKE_CURRENT_SOURCE_DIR'] = v['CMAKE_CURRENT_LIST_DIR'] = _p(d)
                self._run(t, self.cmds[t], _State(v, st), stack, unk)
        return False

    @staticmethod
    def _joined(subs):
        if not subs:
            return None
        ds = [x for x in subs if isinstance(x, Doubt)]
        if ds:
            if len(subs) == 1:
                return ds[0]
            return Doubt(frozenset([None]), ds[0].why)
        return None if any(x is None for x in subs) else ';'.join(subs)

    def _include_target(self, word, st):
        r = self._sub(word, st)
        if not isinstance(r, str):
            return None
        if '/' in r or r.endswith('.cmake'):
            t = self._path(word, st)
            return t if t in self.texts else None
        mp = self._get(st, 'CMAKE_MODULE_PATH')
        for m in (mp if isinstance(mp, str) else '').split(';'):
            d = _repo(m)
            if d is not None and posixpath.join(d, r + '.cmake') in self.texts:
                return posixpath.join(d, r + '.cmake')
        return None

    # ── what a line knows
    def _span(self, path, line):
        """The innermost function or macro body holding `line`, or None."""
        best = None
        for a, b in self.spans.get(path, ()):
            if a <= line <= b and (best is None or a >= best[0]):
                best = (a, b)
        return best

    def _prepare(self, path):
        groups = collections.defaultdict(list)
        for fr in self.runs.get(path, ()):
            groups[fr.span].append(fr)
        if None not in groups:
            groups[None] = [_Frame(path, {'CMAKE_CURRENT_LIST_DIR': _p(posixpath.dirname(path))})]
        out = {}
        for span, frames in groups.items():
            uniq = {}
            for fr in frames:                                      # a function called many times the same way: once
                k = (tuple(sorted(fr.entry.items(), key=lambda x: x[0])), tuple(fr.changes))
                uniq.setdefault(k, fr)
            runs = list(uniq.values())
            what = 'calls' if span else 'routes'
            lines = [0] + sorted({ln for fr in runs for ln, _, _, _ in fr.changes})
            scopes = []
            for b in lines:
                states = []
                for fr in runs:
                    s, at = dict(fr.entry), {}
                    for ln, name, val, tag in fr.changes:
                        if ln > b:
                            continue
                        if name in at and at[name][0] == ln and at[name][2] != tag and at[name][1] != val:
                            val = None                             # one line, two passes, two values (a loop body)
                        at[name] = (ln, val, tag)
                        s[name] = val
                    states.append(s)
                known, doubts = {}, {}
                for name in set().union(*states):
                    if name == 'CMAKE_MODULE_PATH':
                        continue
                    vals = [s.get(name) for s in states]
                    v0 = vals[0]
                    if all(v == v0 for v in vals[1:]) and not isinstance(v0, Doubt):
                        r = _repo(v0)
                        if r is not None:
                            known[name] = r
                        continue
                    cands, whys = set(), []
                    for v in vals:
                        if isinstance(v, Doubt):
                            whys.append(v.why)
                        cands |= {_repo(x) for x in _cands(v)}
                    if whys or any(x is not None for x in cands):
                        doubts[name] = (frozenset(cands), whys[0] if whys else
                                        'the %d %s that reach this line give it different values' % (len(states), what))
                scopes.append((known, doubts))
            out[span] = (lines, scopes)
        self._bp[path] = out

    def info(self, path, line=None):
        """LineInfo for the build file `path` at `line` (its end when None)."""
        if path not in self._bp:
            self._prepare(path)
        groups = self._bp[path]
        span = self._span(path, line) if line is not None else None
        if span is not None and span not in groups:
            return LineInfo({}, {}, 'uncalled')
        lines, scopes = groups[span]
        known, doubts = scopes[-1] if line is None else scopes[max(0, bisect.bisect_left(lines, line) - 1)]
        return LineInfo(known, doubts, None if span is None else 'called')

    def scope(self, path, line=None):
        """{variable: repo path} that the build file `path` knows at `line` (its end when None): set on every route
        that reaches it, to one place."""
        return self.info(path, line).known

    def where_set(self, name):
        return sorted(self.setters.get(name, ()))


def include_dir_ranges(text):
    """Character ranges of words that are include directories: target_include_directories(...), include_directories(...),
    an INCLUDE/INCLUDES/INCLUDE_DIRS keyword's words, and set() of a variable used in one of those."""
    kinds = spans_cmake(text)
    code = ''.join(ch if k != 'm' else ' ' for ch, k in zip(text, kinds))
    ranges, used = [], set()

    def calls():
        for m in re.finditer(r'(\w+)\s*\(', code):
            depth, j = 1, m.end()
            while j < len(code) and depth:
                if code[j] == '(' and kinds[j] == 'c':
                    depth += 1
                elif code[j] == ')' and kinds[j] == 'c':
                    depth -= 1
                j += 1
            yield m.group(1).lower(), m.end(), j - 1

    for name, a, b in calls():
        words = [(w.group(0), a + w.start(), a + w.end()) for w in re.finditer(r'[^\s()]+', code[a:b])]
        if name in ('target_include_directories', 'include_directories'):
            sel = [w for w in words[1 if name.startswith('target') else 0:]
                   if w[0].strip('"') not in ('PRIVATE', 'PUBLIC', 'INTERFACE', 'BEFORE', 'AFTER', 'SYSTEM')]
        else:
            sel, on = [], False
            for w in words:
                bare = w[0].strip('"')
                if re.match(r'^[A-Z][A-Z_]+$', bare):
                    on = bare in ('INCLUDE', 'INCLUDES', 'INCLUDE_DIRS', 'INCLUDE_DIRECTORIES')
                    continue
                if on:
                    sel.append(w)
        for w in sel:
            ranges.append((w[1], w[2]))
            used.update(re.findall(r'\$\{(\w+)\}', w[0]))
    for name, a, b in calls():
        if name == 'set':
            words = [(w.group(0), a + w.start(), a + w.end()) for w in re.finditer(r'[^\s()]+', code[a:b])]
            if words and words[0][0] in used:
                ranges.extend((w[1], w[2]) for w in words[1:] if w[0] != 'CACHE')
    return ranges


# ── tables: rings (lint) and contexts (context_check.py) ────────────────────────────────────────
def func_body(text, name):
    m = re.search(r'\b' + name + r'\s*\([^)]*\)\s*(?:const\s*)?\{', text)
    if not m:
        return None
    depth, j = 1, m.end()
    while j < len(text) and depth:
        depth += {'{': 1, '}': -1}.get(text[j], 0)
        j += 1
    return m.start(), j


def lint_tables(text):
    """(dirRings, fileRings) as lists of (key, ring name)."""
    out = []
    for fn in ('dirRings', 'fileRings'):
        r = func_body(text, fn)
        body = text[r[0]:r[1]] if r else ''
        out.append(re.findall(r'\{\s*QStringLiteral\("([^"]+)"\)\s*,\s*(\w+)\s*\}', body))
    return out


def ring_of(tables, rel):
    dirs, files = tables
    st = posixpath.splitext(rel)[0] if '.' in posixpath.basename(rel) else rel
    for k, v in files:
        if k == st:
            return v
    best, ring = -1, None
    for k, v in dirs:
        if rel.startswith(k) and len(k) > best:
            best, ring = len(k), v
    return ring


def context_tables(text):
    """DIRS and FILES of context_check.py, literal keys only; a key built at run time is reported, not read."""
    dirs, files, gap, built = {}, {}, {}, []
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return None
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) \
                and node.targets[0].id in ('DIRS', 'FILES', 'L2_GAP') and isinstance(node.value, ast.Dict):
            t = dirs if node.targets[0].id == 'DIRS' else (files if node.targets[0].id == 'FILES' else gap)
            for k, v in zip(node.value.keys, node.value.values):
                if isinstance(k, ast.Constant) and isinstance(k.value, str):
                    t[k.value] = v.id if isinstance(v, ast.Name) else ast.dump(v)
                else:
                    built.append(ast.dump(k) if k is not None else '**')
        elif isinstance(node, (ast.Assign, ast.AugAssign, ast.Expr, ast.For)):
            src = ast.dump(node)
            if "id='DIRS'" in src or "id='FILES'" in src:
                if not (isinstance(node, ast.Assign) and isinstance(node.value, ast.Dict)):
                    built.append(src[:80])
    files.update(gap)                     # tag(): the L2-gap placements win over FILES (unless --literal-l2)
    return dirs, files, built


def context_of(tables, rel):
    dirs, files = tables[0], tables[1]
    st = posixpath.splitext(rel)[0] if '.' in posixpath.basename(rel) else rel
    if st in files:
        return files[st]
    best, tag = -1, None
    for k, v in dirs.items():
        if rel.startswith(k) and len(k) > best:
            best, tag = len(k), v
    return tag


# ── the rewrite ─────────────────────────────────────────────────────────────────────────────────
class Report:
    def __init__(self):
        self.moves = []
        self.includes = []           # (file, line, old, new, how, outward)
        self.mentions = collections.defaultdict(list)   # kind -> (file, line, old, new)
        self.incdirs = []            # (file, line, old, new)
        self.unplaced = []           # (file, line, text, why)
        self.kept = []               # (file, line, the line, what the move would have made it)
        self.errors = []
        self.tables = []             # text lines
        self.scope = collections.Counter()
        self.spelling = collections.Counter()


def split_trailing(tok):
    core = tok
    while core and core[-1] in '.,:;' and not core.endswith('..'):
        core = core[:-1]
    return core, tok[len(core):]


class Rewriter:
    def __init__(self, root, plan, texts):
        self.root, self.plan, self.texts = root, plan, texts
        self.cur = Tree(plan.files)
        self.old = Tree(plan.virtual_old)
        self.final = Tree(plan.final)
        self.union = Tree(plan.union)
        self.cvars = CMakeVars({p: t for p, t in texts.items() if file_kind(p) == 'build'})
        self.rep = Report()
        self.moved_names = {posixpath.basename(f) for f in plan.moves}
        self.moved_tails = set()                                   # every trailing part of a moved path (B1.2b)
        for m in list(plan.moves) + list(plan.stems) + [o.rstrip('/') for o, _ in plan.dirs]:
            parts = m.split('/')
            self.moved_tails.update('/'.join(parts[i:]) for i in range(len(parts)))

    # where the file at `path` (current location) ends up
    def final_path(self, path):
        return self.plan.pending.get(path, path)

    def resolve_now(self, path, inc):
        how, t = self.cur.resolve(path, inc)
        if how in ('rel', 'src', 'gui'):
            return how, t
        o = self.plan.back.get(path, path)                       # stale spelling from the old home
        how2, t2 = self.old.resolve(o, inc)
        if how2 in ('rel', 'src', 'gui'):
            p = self.plan.fwd('src/' + t2)
            if p in self.plan.files:
                return how2, p[4:]
        return how, t

    def rewrite_include(self, path, ln, line):
        m = INC_RE.match(line)
        if not m:
            a = ANGLE_RE.match(line)
            if a:
                c = norm(a.group(1))
                if c and (c in self.cur.src or c in self.old.src) and self.plan.moved('src/' + c):
                    self.rep.errors.append('%s:%d: <%s> names a moved header in <…>: rewrite it by hand' % (path, ln, a.group(1)))
            return line
        inc = m.group(2)
        how, t = self.resolve_now(path, inc)
        fp = self.final_path(path)
        if how == 'ambiguous':
            if any(self.plan.moved('src/' + c) or ('src/' + c) in self.plan.moves.values() for c in t) \
                    or posixpath.basename(inc) in self.moved_names:
                self.rep.errors.append('%s:%d: "%s" is ambiguous (%s): never a pick' % (path, ln, inc, ', '.join(t)))
            return line
        if how is None:
            if posixpath.basename(inc) in self.moved_names and path.startswith(('src/', 'tests/', 'tools/')):
                self.rep.unplaced.append((path, ln, inc, 'does not resolve; its name is a moved file\'s'))
            return line
        T = 'src/' + t
        Tn = self.plan.fwd(T) if T in self.plan.pending else T
        moved_target = T in self.plan.pending or T in self.plan.done.values()
        if path.startswith(('src/', 'tools/')) and moved_target:
            self.rep.scope[path] += 1                              # count_includers.py's scope and spelling names
            self.rep.spelling['dir-qualified' if inc == t else ('bare' if '/' not in inc else 'relative')] += 1
        if Tn == T and fp == path:
            return line
        # keep the spelling if it already resolves right from the new home
        hn, tn = self.final.resolve(fp, inc)
        if hn not in (None, 'ambiguous') and 'src/' + tn == Tn and (hn == how or hn == 'rel') \
                and (hn != 'base' or Tn == T):
            return line
        if how == 'src' and Tn != T:
            new = Tn[4:]
        elif how == 'gui' and Tn != T and Tn.startswith('src/Gui/'):
            new = Tn[len('src/Gui/'):]
        elif how in ('src', 'gui') and Tn == T:
            new = inc
        elif how == 'base' and Tn == T:
            return line                                            # an include directory that stays
        else:
            new = relpath(Tn, posixpath.dirname(fp))
        hn, tn = self.final.resolve(fp, new)
        if hn in (None, 'ambiguous') or 'src/' + tn != Tn:
            self.rep.errors.append('%s:%d: "%s" -> "%s" would not resolve to %s from %s' % (path, ln, inc, new, Tn, fp))
            return line
        if new == inc:
            return line
        outward = fp != path and not moved_target
        self.rep.includes.append((path, ln, inc, new, how, outward))
        return m.group(1) + new + m.group(3)

    def interpret(self, tok, path, mode, scope):
        """-> (P, style) where P is a repo path in the union tree, or None. mode: table | anchored | build."""
        core, _ = split_trailing(tok)
        if not core or '/' not in core and mode != 'build':
            return None
        m = re.match(r'^\$\{(\w+)\}(/.*)?$', core)
        if m:
            if m.group(1) not in scope:
                return None
            base = scope[m.group(1)]
            P = norm(posixpath.join(base, (m.group(2) or '/')[1:])) if (m.group(2) or '/')[1:] else base
            return (P, ('var', m.group(1))) if P is not None and self.union.exists(P) else None
        if '$' in core or '{' in core or '}' in core:
            return None
        if core.startswith(('./', '../')):
            P = norm(posixpath.join(posixpath.dirname(path) if mode != 'build' else cmake_base(path), core))
            return (P, ('rel', mode == 'build')) if P is not None and self.union.exists(P) else None
        if core == 'src' or core.startswith('src/'):
            P = norm(core)
            return (P, ('repo',)) if P is not None and self.union.exists(P) else None
        if mode == 'build':
            P = norm(posixpath.join(cmake_base(path), core))
            if P is not None and self.union.exists(P):
                return P, ('rel', True)
            if '/' not in core:
                return None
        if not re.match(r'\w', core):
            return None                                            # "/", "-x/y", "+=": not a path under src/
        P = norm('src/' + core)
        if P is None or P == 'src':
            return None
        if not self.union.exists(P):
            if mode == 'table' and self.plan.moved(P) and not self.plan.is_move_root(P):
                return P, ('srcroot',)                             # a fixture's made-up file in a moved folder
            return None
        if mode != 'table' and self.plan.is_move_root(P):
            return None                                            # a bare old folder name: report, don't guess
        return P, ('srcroot',)

    def respell(self, tok, P, style, path, scope):
        core, trail = split_trailing(tok)
        Pn = self.plan.fwd(P)
        if Pn == P and (self.final_path(path) == path or style[0] != 'rel'):
            return tok                                             # only a relative spelling depends on where it is
        if style[0] == 'repo':
            new = Pn
        elif style[0] == 'srcroot':
            if not Pn.startswith('src/'):
                return None
            new = Pn[4:]
        elif style[0] == 'rel':
            base_now = cmake_base(path) if style[1] else posixpath.dirname(path)
            fp = self.final_path(path)
            base_new = cmake_base(fp) if style[1] else posixpath.dirname(fp)
            if base_now == base_new and Pn == P:
                return tok
            new = relpath(Pn, base_new)
            if not core.startswith(('./', '../')) and new.startswith('..') and style[1]:
                new = new                                          # a build file's relative path: it may climb
            elif core.startswith('./') and not new.startswith('.'):
                new = './' + new
        else:
            name = style[1]
            vnew = self.plan.fwd(scope[name])
            if Pn == vnew or Pn.startswith(vnew + '/') or vnew == '':
                rest = relpath(Pn, vnew) if Pn != vnew else ''
                new = '${%s}' % name + ('/' + rest if rest else '')
            else:
                # only a variable this line knows (CMakeVars' scopes); the deepest that holds the path, then the one
                # the file already spells most, then the name; else a repo-root variable it knows (B1.2a)
                text = self.texts.get(path, '')
                used = lambda k: text.count('${%s}' % k)
                cands = [(len(fv), used(k), k, fv) for k, v in scope.items() for fv in [self.plan.fwd(v)]
                         if fv and (Pn == fv or Pn.startswith(fv + '/'))
                         and k not in ('CMAKE_SOURCE_DIR', 'PROJECT_SOURCE_DIR')]
                if not cands:
                    cands = [(0, used(k) + (k == 'CMAKE_SOURCE_DIR'), k, '') for k, v in scope.items()
                             if self.plan.fwd(v) == '' and k not in ('CMAKE_CURRENT_SOURCE_DIR', 'CMAKE_CURRENT_LIST_DIR')]
                if not cands:
                    return None
                best = max(cands, key=lambda x: (x[0], x[1], [-ord(ch) for ch in x[2]]))[2:]
                rest = relpath(Pn, best[1]) if Pn != best[1] else ''
                new = '${%s}' % best[0] + ('/' + rest if rest else '')
        if core.endswith('/') and not new.endswith('/'):
            new += '/'
        return new + trail

    def rewrite_span(self, seg, path, ln, mode, scope, kind, incdir_mask=None, offset=0, info=None):
        out, last = [], 0
        for m in TOKEN_RE.finditer(seg):
            tok = m.group(0)
            r = self.interpret(tok, path, mode, scope)
            if r is None:
                why = self.cmake_cant_place(tok, info) if info is not None and mode == 'build' else None
                if why:
                    self.rep.unplaced.append((path, ln, tok, why))
                else:
                    self.residue(tok, path, ln, kind)
                continue
            P, style = r
            new = self.respell(tok, P, style, path, scope)
            if new is None:
                self.rep.unplaced.append((path, ln, tok, 'no spelling for %s from here' % self.plan.fwd(P)))
                continue
            if new == tok:
                continue
            if info is not None and mode == 'build' and info.body and style[0] in ('rel', 'repo'):
                base, want = info.known.get('CMAKE_CURRENT_SOURCE_DIR'), cmake_base(path) if style[0] == 'rel' else ''
                if info.body == 'uncalled' or base != want:
                    self.rep.unplaced.append((path, ln, tok, CANT_PLACE + 'a path in a function or macro body is read '
                                              'from the caller\'s folder (%s), not %s' % (
                                                  'no call reaches it' if info.body == 'uncalled' else
                                                  'the calls disagree' if base is None else base or 'the root',
                                                  want or 'the root')))
                    continue
            check = self.interpret(new, self.final_path(path), mode, scope) if style[0] != 'var' else None
            out.append(seg[last:m.start()]); out.append(new); last = m.end()
            isdir = incdir_mask is not None and any(a <= offset + m.start() < b for a, b in incdir_mask)
            if isdir:
                self.rep.incdirs.append((path, ln, tok, new))
            self.rep.mentions[kind].append((path, ln, tok, new))
        out.append(seg[last:])
        return ''.join(out)

    def tail_moves(self, tail):
        t = tail
        while t.startswith(('../', './')):
            t = t.split('/', 1)[1]
        t = t.rstrip('/')
        return bool(t) and t not in ('.', '..') and t in self.moved_tails

    def cmake_cant_place(self, tok, info):
        """Why a `${VAR}/…` word the line can't read is listed, not skipped (B1.2b): VAR is undecided here and one of
        its values makes the word name a moved path, or VAR is unknown and the word's tail names one."""
        core, _ = split_trailing(tok)
        m = re.match(r'^\$\{(\w+)\}/(.+)$', core)
        if not m or m.group(1) in info.known or m.group(1).endswith('_BINARY_DIR'):
            return None
        name, tail = m.groups()
        d = info.doubts.get(name)
        if d:
            cands, why = d
            for c in sorted(x for x in cands if x is not None):
                P = norm(posixpath.join(c, tail))
                if P is not None and self.union.exists(P) and self.plan.moved(P):
                    return CANT_PLACE + '${%s} is undecided here (%s); as %s it names %s, which moves' % (
                        name, why, c or 'the root', P)
            if None in cands and self.tail_moves(tail):
                return CANT_PLACE + '${%s} is undecided here (%s); its tail names a moved path' % (name, why)
            return None
        if not self.tail_moves(tail):
            return None
        if info.body == 'uncalled':
            return CANT_PLACE + '${%s} is in a function or macro body no call reaches; its tail names a moved path' % name
        return CANT_PLACE + '${%s} has no value the tool can read at this line; its tail names a moved path' % name

    def residue(self, tok, path, ln, kind):
        """A mention the tool did not place: an old folder or moved file named in a form it does not anchor."""
        core, _ = split_trailing(tok)
        if '/' not in core:
            return
        for old, new in self.plan.dirs:
            o = old[4:].rstrip('/')
            for m in re.finditer(r'(?:^|(?<=/))' + re.escape(o) + r'(?=/|$)', core):
                head = core[:m.end()]
                if head.endswith(new[4:].rstrip('/')):
                    continue
                full = core[:m.start()]
                if full and not full.endswith(('src/', '}/')) and not full.startswith(('.', '$')) and full not in ('/',):
                    continue                                       # another tree's folder of the same name
                self.rep.unplaced.append((path, ln, tok, 'names the moved folder %s; rewrite by hand if it means it' % old))
                return
        for so, sn in self.plan.stems.items():
            o = so[4:]
            if re.search(r'(?:^|/)' + re.escape(o) + r'(?:\.|$)', core) and not core.endswith(sn[4:]):
                self.rep.unplaced.append((path, ln, tok, 'names the moved file %s' % so))
                return

    def rewrite_file(self, path, text):
        """The file rewritten, except the lines --keep / keep: name, which stay as they are (each listed)."""
        new = self._rewrite_file(path, text)
        if file_kind(path) == 'build':
            new = self.whole_lines(path, text, new)
        kept = self.plan.keep.get(self.plan.back.get(path, path), set()) | self.plan.keep.get(path, set())
        if not kept:
            return new
        ol, nl = text.splitlines(keepends=True), new.splitlines(keepends=True)
        r = self.rep
        drop = lambda xs: [x for x in xs if not (x[0] == path and x[1] in kept)]
        r.includes, r.incdirs, r.unplaced = drop(r.includes), drop(r.incdirs), drop(r.unplaced)
        for k in list(r.mentions):
            r.mentions[k] = drop(r.mentions[k])
        for ln in sorted(kept):
            if ln > len(ol):
                r.errors.append('keep: %s:%d is past the end of the file' % (path, ln))
                continue
            a, b = ol[ln - 1].rstrip('\r\n'), nl[ln - 1].rstrip('\r\n')
            r.kept.append((path, ln, a, b))
            if a == b:
                r.errors.append('keep: %s:%d has nothing the map rewrites (a stale line number?)' % (path, ln))
            nl[ln - 1] = ol[ln - 1]
        return ''.join(nl)

    def whole_lines(self, path, text, new):
        """A build line with a CMake-scope can't-place word is left whole (B1.2b's review): no word on it is rewritten,
        and each word the move would have changed is listed as left with it."""
        r = self.rep
        stop = {ln for p, ln, _, why in r.unplaced if p == path and why.startswith(CANT_PLACE)}
        if not stop:
            return new
        ol, nl = text.splitlines(keepends=True), new.splitlines(keepends=True)
        for kind in list(r.mentions):
            keep_, left = [], []
            for x in r.mentions[kind]:
                (left if x[0] == path and x[1] in stop else keep_).append(x)
            r.mentions[kind] = keep_
            for p, ln, a, b in left:
                r.unplaced.append((p, ln, a, CANT_PLACE + 'left with its line, which holds a word that can\'t be '
                                   'placed (the move would have made it %s)' % b))
        r.incdirs = [x for x in r.incdirs if not (x[0] == path and x[1] in stop)]
        for ln in stop:
            if 0 < ln <= len(nl):
                nl[ln - 1] = ol[ln - 1]
        return ''.join(nl)

    def _rewrite_file(self, path, text):
        kind = file_kind(path)
        lines = text.splitlines(keepends=True)
        scope = {}
        if kind in ('cpp', 'lint', 'script'):
            cls = spans_cpp(text)
        elif kind in ('py', 'context'):
            cls = spans_py(text)
        elif kind == 'build':
            cls = spans_cmake(text)
        else:
            cls = None
        incdir = include_dir_ranges(text) if kind == 'build' else None
        out, pos = [], 0
        for ln, line in enumerate(lines, 1):
            body = line.rstrip('\r\n'); eol = line[len(body):]
            start = pos; pos += len(line)
            info = None
            if kind == 'build':
                info = self.cvars.info(path, ln)                   # what this line knows (B1.2a, B1.2b)
                scope = info.known
            if kind in ('py', 'context', 'build', 'text') or path.endswith(('.sh', '.zsh', '.cmd', '.ps1')):
                self.split_paths(body, path, ln)
            if kind in ('cpp', 'lint') and INC_RE.match(body) or ANGLE_RE.match(body) and kind in ('cpp', 'lint'):
                if path.startswith(('src/', 'tests/', 'tools/')):
                    body = self.rewrite_include(path, ln, body)
                out.append(body + eol); continue
            if kind == 'allowlist':
                if body.lstrip().startswith('#') or '|' not in body:
                    out.append(self.rewrite_span(body, path, ln, 'anchored', scope, 'allowlist') + eol); continue
                key, rest = body.split('|', 1)
                out.append(self.rewrite_span(key, path, ln, 'table', scope, 'allowlist')
                           + '|' + self.rewrite_span(rest, path, ln, 'anchored', scope, 'allowlist') + eol)
                continue
            if cls is None:
                out.append(self.rewrite_span(body, path, ln, 'anchored', scope, kind) + eol); continue
            # walk the line's spans
            res, i = [], 0
            while i < len(body):
                c = cls[start + i]; j = i
                while j < len(body) and cls[start + j] == c:
                    j += 1
                seg = body[i:j]
                if kind in ('cpp', 'script'):
                    if c == 'm':
                        seg = self.rewrite_span(seg, path, ln, 'anchored', scope, 'comment')
                    elif c == 's':
                        self.code_string(seg, path, ln)
                elif kind == 'lint':
                    if c == 's':
                        seg = self.rewrite_span(seg, path, ln, 'table', scope, 'lint')
                    elif c == 'm':
                        seg = self.rewrite_span(seg, path, ln, 'anchored', scope, 'lint')
                elif kind == 'context':
                    if c == 's':
                        seg = self.rewrite_span(seg, path, ln, 'table', scope, 'context')
                    elif c == 'm':
                        seg = self.rewrite_span(seg, path, ln, 'anchored', scope, 'context')
                elif kind == 'py':
                    if c in 'sm':
                        seg = self.rewrite_span(seg, path, ln, 'anchored', scope, 'tool')
                elif kind == 'build':
                    seg = self.rewrite_span(seg, path, ln, 'anchored' if c == 'm' else 'build', scope, 'build',
                                            incdir if c != 'm' else None, start + i, info)
                res.append(seg); i = j
            out.append(''.join(res) + eol)
        return ''.join(out)

    SPLIT_RE = re.compile(r'(["\'])([\w.\-]+)\1(?:\s*,\s*(["\'])[\w.\-]+\3)+')
    PART_RE = re.compile(r'(["\'])([\w.\-/]+/)\1\s*\+')

    def split_paths(self, body, path, ln):
        """A path the tool can't rewrite because it is split across a join ("src", "Metrics", "x.h") or built in parts
        ('Metrics/' + name): reported as can't place, never skipped."""
        for m in self.SPLIT_RE.finditer(body):
            words = re.findall(r'["\']([\w.\-]+)["\']', m.group(0))
            src_var = re.search(r'\b\w*SRC\w*\s*,\s*$', body[:m.start()], re.I)
            starts = [k for k, w in enumerate(words) if w == 'src'] or ([None] if src_var else [])
            hit = None
            for k in starts:
                parts = ['src'] + words[(k + 1) if k is not None else 0:]
                for e in range(len(parts), 1, -1):                  # the whole join, then each shorter prefix
                    c = '/'.join(parts[:e])
                    if self.union.exists(c) and self.plan.moved(c):
                        hit = c; break
                if hit:
                    break
            if hit:
                self.rep.unplaced.append((path, ln, m.group(0), 'a path split across a join names %s: write it as '
                                          'one "src/..." string so a move rewrites it' % hit))
        for m in self.PART_RE.finditer(body):
            w = m.group(2).rstrip('/')
            for c in ([w] if w.startswith('src/') else ['src/' + w]):
                if self.union.exists(c) and (self.plan.moved(c) or any(s.startswith(c + '/') for s in self.plan.stems)):
                    self.rep.unplaced.append((path, ln, m.group(0), 'a path built in parts from %s/: write each path '
                                              'whole so a move rewrites it' % c))

    def code_string(self, seg, path, ln):
        for m in TOKEN_RE.finditer(seg):
            r = self.interpret(m.group(0), path, 'anchored', {})
            if r and self.plan.moved(r[0]):
                self.rep.unplaced.append((path, ln, m.group(0), 'a path in a C++ string literal is behaviour: a hand edit'))
            elif r is None:
                self.residue(m.group(0), path, ln, 'code')

    def tables_report(self, new_texts):
        """Each moved file's ring (lint) and context (context_check.py), before and after."""
        lt, ct = self.texts.get(LINT), self.texts.get(CONTEXT)
        lt2, ct2 = new_texts.get(LINT, lt), new_texts.get(CONTEXT, ct)
        rows = []
        if lt is not None:
            a, b = lint_tables(lt), lint_tables(lt2)
            for o, d in sorted(self.plan.pending.items()):
                if '/tests/' in o or not o.endswith(CPP_EXT):
                    continue
                r0, r1 = ring_of(a, o[4:]), ring_of(b, d[4:])
                if r0 != r1:
                    rows.append('ring changes: %s %s -> %s %s (a hand edit if the brief rules it)' % (o, r0, d, r1))
        if ct is not None:
            a, b = context_tables(ct), context_tables(ct2)
            if a is None or b is None:
                rows.append('context_check.py does not parse')
            else:
                for x in a[2]:
                    self.rep.errors.append('context_check.py builds a table key at run time (%s): make it a literal' % x)
                lost = collections.defaultdict(list)
                for o, d in sorted(self.plan.pending.items()):
                    if '/tests/' in o or not o.endswith(CPP_EXT):
                        continue
                    t0, t1 = context_of(a, o[4:]), context_of(b, d[4:])
                    if t0 != t1:
                        lost[(posixpath.dirname(d), t0, t1)].append(posixpath.basename(d))
                for (dd, t0, t1), names in sorted(lost.items(), key=lambda x: (x[0][0], str(x[0][1]), str(x[0][2]))):
                    what = 'NO TAG (context_check.py errors on it): add a DIRS or FILES entry by hand' if t1 is None \
                        else 'a hand edit if the brief rules it'
                    rows.append('context changes: %s/ (%d files) %s -> %s; %s' % (dd, len(names), t0, t1, what))
        self.rep.tables = rows


def build_plan(root, map_path):
    with open(map_path, encoding='utf-8') as f:
        text = f.read()
    entries = parse_map(text)
    files = list_files(root)
    plan = Plan(entries, files)
    plan.keep = parse_keeps(text)
    plan.errors.extend(keep_errors(plan.keep))
    for k in sorted(plan.keep):
        if k not in files and plan.moves.get(k) not in files:
            plan.errors.append('keep: %s is not in the tree' % k)
    # hand: the move makes no split (the brief does, by hand, after it), and an --over-work replay may run after the
    # split; so it refuses only what holds either way: the source in the tree, a new file the map moves. --verify
    # checks the rest against its base.
    plan.hands = parse_hands(text)
    plan.errors.extend(hand_errors(plan.hands, plan, files | set(plan.moves)))
    return plan, files


def compute(root, plan, files):
    texts = {}
    for p in sorted(files):
        if in_scope(p):
            t = read_text(os.path.join(root, p))
            if t is not None:
                texts[p] = t
    rw = Rewriter(root, plan, texts)
    rw.rep.errors.extend(plan.errors)
    read = set(texts) | {plan.back.get(p, p) for p in texts}
    for k in sorted(plan.keep):                                    # a keep the rewriter never reads matches nothing
        if k not in read and in_scope(k) and (k in files or plan.moves.get(k) in files):
            rw.rep.errors.append('keep: %s matches nothing: the tool could not read it as text' % k)
    new_texts = {}
    for p, t in texts.items():
        n = rw.rewrite_file(p, t)
        if n != t:
            new_texts[p] = n
    rw.tables_report(new_texts)
    for o, d in sorted(plan.pending.items()):
        rw.rep.moves.append((o, d))
    tests_cmake = sorted({posixpath.dirname(o) for o in plan.pending if o.endswith('/tests/CMakeLists.txt')})
    for t in tests_cmake:
        rw.rep.unplaced.append((t, 0, t, 'a moved suite folder: tests/CMakeLists.txt\'s suite list may name it'))
    return rw, new_texts


def print_report(rw, plan, quiet, dry):
    r = rw.rep
    say = print
    say('== files to move: %d (%d already at their new path)' % (len(plan.pending), len(plan.done)))
    if not quiet:
        for o, d in r.moves:
            say('  %s -> %s' % (o, d))
    hows = collections.Counter(x[4] for x in r.includes)
    files_inc = {x[0] for x in r.includes}
    say('== include lines to rewrite: %d in %d files; by how they resolved: %s; outward includes inside moved files: %d'
        % (len(r.includes), len(files_inc), dict(sorted(hows.items())), sum(1 for x in r.includes if x[5])))
    if not quiet:
        for p, ln, a, b, how, outw in r.includes:
            say('  %s:%d  "%s" -> "%s"  (%s%s)' % (p, ln, a, b, how, ', outward' if outw else ''))
    say('== includers of the moved set in src and tools (count_includers.py\'s scope): %d files, %d includes; '
        'include spelling %s' % (len(r.scope), sum(r.scope.values()), dict(sorted(r.spelling.items()))))
    for kind in ('build', 'lint', 'allowlist', 'context', 'tool', 'text', 'doc', 'comment'):
        xs = r.mentions.get(kind, [])
        say('== path mentions, %s: %d lines, %d mentions' % (kind, len({(p, ln) for p, ln, _, _ in xs}), len(xs)))
        if not quiet:
            for p, ln, a, b in xs:
                say('  %s:%d  %s -> %s' % (p, ln, a, b))
    say('== include directories re-pathed (never added; no include of a moved file relies on one after the run; '
        'remove each by hand when the brief says): %d' % len(r.incdirs))
    for p, ln, a, b in r.incdirs:
        say('  %s:%d  %s -> %s' % (p, ln, a, b))
    un = [p for p in rw.cvars.unreached if not p.endswith('.in')]
    say('== build files the CMake walk does not reach (a cmake -P script, or reached only from inside a function; '
        'read with their own sets only): %d' % len(un))
    if not quiet:
        for p in un:
            say('  ' + p)
    say('== tables: rings and contexts that change: %d' % len(r.tables))
    for x in r.tables:
        say('  ' + x)
    say('== lines kept as they are (--keep / keep:; dated records): %d' % len(r.kept))
    for p, ln, a, b in r.kept:
        say('  %s:%d  %s' % (p, ln, 'kept; the move would have made it: ' + b.strip() if a != b else 'kept (nothing to rewrite there)'))
    hands = getattr(plan, 'hands', [])
    say('== hand splits declared (hand:; the brief makes each after the move, --verify checks it): %d' % len(hands))
    for new, src, ranges, n in hands:
        say('  %s <- %s:%s' % (new, src, ','.join('%d-%d' % r if r[0] != r[1] else '%d' % r[0] for r in ranges)))
    say("== can't place (not rewritten; read each): %d" % len(r.unplaced))
    if not quiet:
        for p, ln, t, why in r.unplaced:
            say('  %s:%d  %s: %s' % (p, ln, t, why))
    say('== errors: %d' % len(r.errors))
    for e in r.errors:
        say('  ERROR ' + e)
    say('TOTALS: move %d files; rewrite %d include lines in %d files; %d path mentions; %d include dirs re-pathed; '
        "%d can't place; %d errors%s" % (len(plan.pending), len(r.includes), len(files_inc),
                                         sum(len(v) for v in r.mentions.values()), len(r.incdirs), len(r.unplaced),
                                         len(r.errors), ' (dry run: nothing changed)' if dry else ''))


def run_move(root, map_path, dry, over_work, quiet):
    plan, files = build_plan(root, map_path)
    rw, new_texts = compute(root, plan, files)
    print('move_files.py %s on %s%s' % (os.path.relpath(map_path, root) if map_path.startswith(root) else map_path,
                                         root, ' (dry run)' if dry else ''))
    print_report(rw, plan, quiet, dry)
    if dry:
        return 1 if rw.rep.errors else 0
    if rw.rep.errors:
        print('refused: %d errors; nothing changed' % len(rw.rep.errors))
        return 1
    if not over_work:
        st = git(root, 'status', '--porcelain', '--untracked-files=all')
        if st.strip():
            print('refused: the tree is not clean (git status); --over-work replays a move over uncommitted work')
            return 1
    tracked = set(p for p in git(root, 'ls-files', '-z').split('\0') if p)
    for p, t in new_texts.items():
        write_text(os.path.join(root, p), t)
    for o, d in sorted(plan.pending.items()):
        os.makedirs(os.path.join(root, posixpath.dirname(d)), exist_ok=True)
        if o in tracked:
            git(root, 'mv', o, d)
            if os.path.exists(os.path.join(root, o)):
                raise Fail('git mv %s %s did not move it' % (o, d))
        else:
            os.rename(os.path.join(root, o), os.path.join(root, d))
            print('  moved an untracked file without git: %s -> %s' % (o, d))
    for o in sorted({posixpath.dirname(o) for o in plan.pending}, key=lambda x: -len(x)):
        d = os.path.join(root, o)
        while o.startswith('src/') and os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
            o = posixpath.dirname(o); d = os.path.join(root, o)
    print('done: %d files moved, %d files rewritten' % (len(plan.pending), len(new_texts)))
    return 0


# ── --verify ────────────────────────────────────────────────────────────────────────────────────
def verify(root, map_path, base, allow_prose, quiet, allow_code=None):
    """Pairs files by the map, then checks each changed line by what it points at (its own reading of the diff); ends
    with the rewriter's own pass over the result, so nothing the map still asks for passes on an unchanged line. A kept
    line (--keep, keep:) must be byte-identical to the base, and its old spelling passes there only."""
    allow_code = allow_code or {}
    with open(map_path, encoding='utf-8') as f:
        map_text = f.read()
    entries = parse_map(map_text)
    keep = parse_keeps(map_text)
    bad_keep = keep_errors(keep)
    hands = parse_hands(map_text)
    base_modes = {}
    for rec in git(root, 'ls-tree', '-r', '-z', base).split('\0'):
        if rec:
            meta, path = rec.split('\t', 1)
            base_modes[path] = meta.split()[0]
    base_files = set(base_modes)
    plan = Plan(entries, base_files)
    bad, listed, ok_renames = [], [], 0
    if plan.errors:
        bad.extend('map: ' + e for e in plan.errors)
    work = list_files(root)
    expect = {plan.moves.get(p, p): p for p in base_files}       # new path -> base path
    maps_dir = 'tools/refactor/'
    for w in sorted(work - set(expect)):
        if w in {h[0] for h in hands}:
            continue                                               # a hand split: checked below, line by line
        (listed if w.startswith(maps_dir) else bad).append('added file %s%s' % (
            w, ' (the map or the tool: read by the reviewer)' if w.startswith(maps_dir) else ': not a move of the map'))
    for w, b in sorted(expect.items()):
        if w not in work:
            bad.append('missing %s (from %s)' % (w, b) if w != b else 'deleted %s' % b)
    changed = set(p for p in git(root, 'diff', '--no-renames', '-z', '--name-only', base, '--').split('\0') if p)
    old_tree, new_tree = Tree(base_files), Tree(work)
    base_texts = {}

    def base_text(b):
        if b not in base_texts:
            data = git(root, 'show', '%s:%s' % (base, b), binary=True)
            base_texts[b] = None if b'\0' in data[:8192] else data.decode('utf-8', 'surrogateescape')
        return base_texts[b]

    work_texts = {}

    def work_text(w):
        if w not in work_texts:
            work_texts[w] = read_text(os.path.join(root, w))
        return work_texts[w]

    # hand splits (hand:): the new file holds the declared lines byte for byte, plus scaffold only; the source loses
    # exactly those lines and may gain an include of the new file; an includer of the source may point at the new file
    # instead; a build file may gain a line naming it. Everything else in those files is checked as before.
    hand_new, taken, splits_of = collections.OrderedDict(), collections.defaultdict(set), collections.defaultdict(set)
    hand_bad = hand_errors(hands, plan, base_files, base_text)
    bad.extend(hand_bad)
    for new, src, ranges, n in ([] if hand_bad else hands):
        if new in base_files:
            bad.append('hand: (map line %d) %s is in the base: a hand split is a new file' % (n, new)); continue
        if new not in work:
            bad.append('hand: (map line %d) %s matches nothing: no such file in the tree' % (n, new)); continue
        for r in ranges:
            hand_new.setdefault(new, []).append((src, r, n))
            taken[src].update(range(r[0], r[1] + 1))
        splits_of[src].add(new)
    for new in hand_new:
        bad_n, ln = check_hand_file(new, hand_new[new], base_text, work_text(new), old_tree, new_tree, plan)
        bad.extend(bad_n)
        if ln:
            listed.append(ln)

    def hand_include(b, w, line, old=None):
        """The new file an include line of w names, when the hand: rules allow it: an include of a split added in its
        source (old None), or one that named the source in the base now naming its split (old: the base line)."""
        mn = INC_RE.match(line.rstrip('\r\n'))
        if not mn:
            return None
        h1, t1 = new_tree.resolve(w, mn.group(2))
        if h1 in (None, 'ambiguous', 'base'):
            return None
        if old is None:
            return 'src/' + t1 if 'src/' + t1 in splits_of.get(b, ()) else None
        mo = INC_RE.match(old.rstrip('\r\n'))
        if not mo or old[len(old.rstrip('\r\n')):] != line[len(line.rstrip('\r\n')):] or mo.group(1) != mn.group(1) \
                or re.sub(r'\s+', ' ', mo.group(3)[1:]).strip() != re.sub(r'\s+', ' ', mn.group(3)[1:]).strip():
            return None                                            # only the path changed (and the comment's spacing)
        h0, t0 = old_tree.resolve(b, mo.group(2))
        if h0 in (None, 'ambiguous') or (h0 == 'rel' and h1 != 'rel'):
            return None
        return 'src/' + t1 if 'src/' + t1 in splits_of.get('src/' + t0, ()) else None

    build_old = {p: base_text(p) for p in base_files if file_kind(p) == 'build' and in_scope(p)}
    build_new = {p: work_text(p) for p in work if file_kind(p) == 'build' and in_scope(p)}
    vars_old = CMakeVars({k: v for k, v in build_old.items() if v is not None})
    vars_new = CMakeVars({k: v for k, v in build_new.items() if v is not None})
    ip_old = Interp(plan, old_tree, vars_old)
    ip_new = Interp(plan, new_tree, vars_new)

    def hand_build_line(w, line, ln):
        """A build-file line that names only hand-split files (a source list's new entry), or None."""
        cls = spans_cmake(line)
        body = ''.join(ch if c != 'm' else ' ' for ch, c in zip(line, cls))
        toks, seps = tokens_and_seps(body.rstrip('\r\n'))
        if not hand_new or not toks or any(x.strip() for x in seps):
            return None
        names = []
        for t in toks:
            m = ip_new.meaning(t, w, 'build', ln)
            if m is None or m[1] or m[0] not in hand_new:
                return None
            names.append(m[0])
        return names
    # kept lines (--keep / keep:): each must sit unchanged in an equal block of its file's diff, whatever else changed
    # around it (an --allow-prose file included); the final pass reads it at its line in the work tree
    work_keep = collections.defaultdict(set)
    bad.extend(bad_keep)
    for bf, ls in sorted(keep.items()):
        if file_kind(bf) != 'doc':
            continue
        wf = plan.moves.get(bf, bf)
        if bf not in base_files:
            bad.append('keep: %s is not in the base' % bf); continue
        if wf not in work:
            continue
        ot, nt = base_text(bf), work_text(wf)
        if ot is None or nt is None:
            bad.append('keep: %s is not a text file' % bf); continue
        ol, nl = ot.splitlines(keepends=True), nt.splitlines(keepends=True)
        eq = [(i1, i2, j1) for op, i1, i2, j1, _ in difflib.SequenceMatcher(None, ol, nl, autojunk=False).get_opcodes()
              if op == 'equal']
        for i in sorted(ls):
            hit = [j1 + (i - 1 - i1) for i1, i2, j1 in eq if i1 <= i - 1 < i2]
            if i > len(ol):
                bad.append('keep: %s:%d is past the end of the base file' % (bf, i))
            elif not hit:
                bad.append('%s:%d (base): a kept line (--keep / keep:) changed or was removed: %s' % (
                    bf, i, ol[i - 1].rstrip('\r\n')))
            else:
                work_keep[wf].add(hit[0] + 1)

    for w, b in sorted(expect.items()):
        if w not in work:
            continue
        if b != w:
            ok_renames += 1
        if os.name != 'nt' and base_modes.get(b) in ('100644', '100755'):
            ex = os.stat(os.path.join(root, w)).st_mode & 0o111
            if (base_modes[b] == '100755') != bool(ex):
                bad.append('%s: the file mode changed (%s in the base)' % (w, base_modes[b]))
        if b == w and w not in changed:
            continue
        ot, nt = base_text(b), work_text(w)
        if ot is None or nt is None:
            with open(os.path.join(root, w), 'rb') as f:
                same = f.read() == git(root, 'show', '%s:%s' % (base, b), binary=True)
            if not same:
                bad.append('%s: a binary file changed' % w)
            continue
        if ot == nt:
            continue
        kind = file_kind(w)
        if w.startswith(maps_dir) and w != CONTEXT:
            listed.append('%s: the tool or a map changed (%d lines differ): read by the reviewer' % (
                w, sum(1 for l in difflib.ndiff(ot.splitlines(), nt.splitlines()) if l[:1] in '+-')))
            continue
        if w in allow_prose:
            listed.append('%s: prose edits allowed by --allow-prose' % w)
            continue
        if kind == 'allowlist':
            bad.extend(check_allowlist(ot, nt, plan, w))
        ol, nl = ot.splitlines(keepends=True), nt.splitlines(keepends=True)
        tbl = lint_table_lines(ot) if kind == 'lint' else context_table_lines(ot) if kind == 'context' else set()
        tbl_new = lint_table_lines(nt) if kind == 'lint' else context_table_lines(nt) if kind == 'context' else set()
        code_ok = allow_code.get(w, set()) | allow_code.get(b, set())
        # a hand split's lines leave both sides before the diff: the source's taken lines (checked in the new file) and
        # the work's lines that only name the new file (an include of it in its source; a build list's entry)
        oi = [i for i in range(len(ol)) if i + 1 not in taken.get(b, ())]
        nj = []
        for j in range(len(nl)):
            if kind == 'cpp' and hand_include(b, w, nl[j]):
                continue                                           # listed by the include pass below
            names = hand_build_line(w, nl[j], j + 1) if kind == 'build' else None
            if names:
                listed.append('%s:%d: a line naming the hand split %s added: %s' % (w, j + 1, ', '.join(names), nl[j].strip()))
                continue
            nj.append(j)
        sm = difflib.SequenceMatcher(None, [ol[i] for i in oi], [nl[j] for j in nj], autojunk=False)
        for op, a1, a2, b1, b2 in sm.get_opcodes():
            if op == 'equal':
                continue
            olds_, news_ = oi[a1:a2], nj[b1:b2]                    # the real line indices
            pairs = list(zip(olds_, news_)) if len(olds_) == len(news_) else []
            for i, j in pairs:
                if kind == 'cpp' and hand_include(b, w, nl[j], ol[i]):
                    continue                                       # re-pointed at a hand split: listed below
                why = check_pair(ol[i], nl[j], b, w, kind, ip_old, ip_new, plan, j + 1 in code_ok, (i + 1, j + 1))
                if why is None:
                    continue
                if isinstance(why, tuple):
                    listed.append('%s:%d: %s: %s' % (w, j + 1, why[1], nl[j].strip()))
                    continue
                if i + 1 in tbl or j + 1 in tbl_new:
                    listed.append('%s:%d: hand edit on a %s table line: %s' % (w, j + 1, kind, nl[j].strip()))
                else:
                    bad.append('%s:%d: %s\n      - %s\n      + %s' % (w, j + 1, why, ol[i].rstrip('\r\n'), nl[j].rstrip('\r\n')))
            if not pairs:
                for i in olds_:
                    if i + 1 in tbl:
                        listed.append('%s:%d (base): a %s table line deleted: %s' % (b, i + 1, kind, ol[i].strip()))
                    elif kind == 'build' and only_dirs(ol[i], b, ip_old):
                        listed.append('%s:%d (base): an include directory line deleted: %s' % (b, i + 1, ol[i].strip()))
                    else:
                        bad.append('%s:%d (base): line deleted: %s' % (b, i + 1, ol[i].rstrip('\r\n')))
                for j in news_:
                    if j + 1 in tbl_new:
                        listed.append('%s:%d: a %s table line added: %s' % (w, j + 1, kind, nl[j].strip()))
                    else:
                        bad.append('%s:%d: line added: %s' % (w, j + 1, nl[j].rstrip('\r\n')))
    # every include that resolved in the base resolves to the map's image of it now, and not through an include dir
    for w in sorted(work):
        if not w.endswith(CPP_EXT) or not w.startswith(('src/', 'tests/', 'tools/')) or w not in expect:
            continue
        b = expect[w]
        nt = work_text(w)
        ot = nt if b == w and w not in changed else base_text(b)   # an unchanged file: its base text is its text
        if ot is None or nt is None:
            continue
        olds = [(n, m.group(2)) for n, l in enumerate(ot.splitlines(), 1) for m in [INC_RE.match(l)] if m]
        news = [(n, m.group(2)) for n, l in enumerate(nt.splitlines(), 1) for m in [INC_RE.match(l)] if m]
        if b in splits_of:                                         # a hand split's source: its include of the split
            nls = nt.splitlines()
            for n1, c in list(news):
                h = hand_include(b, w, nls[n1 - 1])
                if h:
                    news.remove((n1, c))
                    listed.append('%s:%d: an include of its hand split %s added: %s' % (w, n1, h, nls[n1 - 1].strip()))
        if len(olds) != len(news):
            bad.append('%s: %d includes, the base had %d' % (w, len(news), len(olds))); continue
        for (n0, a), (n1, c) in zip(olds, news):
            h0, t0 = old_tree.resolve(b, a)
            if h0 in (None, 'ambiguous'):
                if a != c:
                    bad.append('%s:%d: "%s" did not resolve in the base, now "%s"' % (w, n1, a, c))
                continue
            h1, t1 = new_tree.resolve(w, c)
            want = plan.fwd('src/' + t0)
            if 'src/' + t0 in splits_of and h1 not in (None, 'ambiguous', 'base') and 'src/' + t1 in splits_of['src/' + t0] \
                    and not (h0 == 'rel' and h1 != 'rel'):
                listed.append('%s:%d: "%s" re-pointed from %s to its hand split %s' % (w, n1, c, 'src/' + t0, 'src/' + t1))
                continue
            if h1 in (None, 'ambiguous') or 'src/' + t1 != want:
                bad.append('%s:%d: "%s" resolves to %s, not %s' % (w, n1, c, t1, want))
            elif h1 == 'base' and plan.moved('src/' + t0):
                bad.append('%s:%d: "%s" reaches a moved file only through an include directory' % (w, n1, c))
            elif h0 == 'rel' and h1 != 'rel':
                bad.append('%s:%d: "%s" no longer resolves from the file\'s own folder (%s)' % (w, n1, c, h1))
    # the rewriter's own pass over the result: a file still to move, or an include or mention still to rewrite, fails
    # (a stale spelling left on a line the diff never touched); a pending re-key on a table line is a listed hand edit
    plan_now = Plan(entries, work)
    plan_now.keep = dict(work_keep)
    rw, _ = compute(root, plan_now, work)
    for p, ln, a, c in rw.rep.kept:
        if a != c:                                                 # a == c: the rewriter's pass errs (stale number)
            listed.append('%s:%d: kept as it was (--keep / keep:); the map would make it %s: %s' % (
                p, ln, c.strip(), a.strip()))
    for o, d in sorted(plan_now.pending.items()):
        bad.append('still to move: %s -> %s' % (o, d))
    for p, ln, a, c, how, _ in rw.rep.includes:
        bad.append('%s:%d: the map still rewrites "%s" -> "%s"' % (p, ln, a, c))
    tables = {}
    for kind, xs in sorted(rw.rep.mentions.items()):
        for p, ln, a, c in xs:
            if p in (LINT, CONTEXT):
                if p not in tables:
                    t = work_text(p) or ''
                    tables[p] = lint_table_lines(t) if p == LINT else context_table_lines(t)
                if ln in tables[p]:
                    listed.append('%s:%d: a table key the map would still re-key (%s -> %s): a hand edit?' % (p, ln, a, c))
                    continue
            bad.append('%s:%d: the map still rewrites %s -> %s' % (p, ln, a, c))
    for e in rw.rep.errors:
        bad.append('the rewriter\'s pass: ' + e)
    # can't-place lines: the base reading's (as the move saw them) and the result's, each once; the configure decides
    rw0 = Rewriter(root, plan, {p: t for p, t in build_old.items() if t is not None})
    seen = set()
    for label, rr, texts_ in (('base', rw0, build_old), ('now', rw, None)):
        if texts_ is not None:
            for p in sorted(texts_):
                if texts_[p] is not None:
                    rr.rewrite_file(p, texts_[p])
        for p, ln, t, why in rr.rep.unplaced:
            if not why.startswith(CANT_PLACE):
                continue
            w = plan.moves.get(p, p) if label == 'base' else p
            if (w, t) in seen:
                continue
            seen.add((w, t))
            listed.append('%s:%d%s: can\'t place %s: %s (read it; --configure is the proof)' % (
                p, ln, ' (base)' if label == 'base' else '', t, why[len(CANT_PLACE):]))
    print('move_files.py --verify %s against %s' % (os.path.relpath(map_path, root) if map_path.startswith(root) else map_path, base))
    print('renames checked: %d; files with changed lines: %d' % (ok_renames, len([1 for w, b in expect.items()
                                                                                  if w in work and (b != w or w in changed)])))
    print('listed for the reviewer: %d' % len(listed))
    for x in listed:
        print('  ' + x)
    print('other lines: %d' % len(bad))
    for x in bad[:400]:
        print('  ' + x)
    print('VERIFY %s' % ('FAILED' if bad else 'PASSED'))
    return 1 if bad else 0


# ── --configure: the real proof of a move's build files ──────────────────────────────────────────
SEED_KEYS = ('CMAKE_PREFIX_PATH', 'CMAKE_MAKE_PROGRAM', 'CMAKE_C_COMPILER_LAUNCHER', 'CMAKE_CXX_COMPILER_LAUNCHER',
             'CMAKE_BUILD_TYPE', 'PP_OPENCV_ROOT', 'PP_EIGEN_DIR', 'PP_LIBPPCP_DIR', 'PP_LIBWRIST_DIR', 'PP_LIBGSPRO_DIR',
             'PP_LIBPPSWING_DIR', 'PINPOINT_BUILD_TOOLS', 'PINPOINT_BUILD_LM_REPAIR')   # --cmake-arg comes last: it wins
FETCH_PREFIX = 'FETCHCONTENT_SOURCE_DIR_'


def read_cache(build_dir):
    out = {}
    try:
        with open(os.path.join(build_dir, 'CMakeCache.txt'), encoding='utf-8', errors='replace') as f:
            for line in f:
                m = re.match(r'^([A-Za-z_][\w.\-]*):(\w+)=(.*)$', line.rstrip('\r\n'))
                if m:
                    out[m.group(1)] = m.group(3)
    except OSError:
        pass
    return out


def seed_for(seed, tree):
    """-G and -D arguments mirroring a configured tree (<seed>/<tree>), so a scratch configure fetches nothing the seed
    already has: its toolchain keys, every FETCHCONTENT_SOURCE_DIR_* (set, or a <name>-src the seed fetched, from
    either tree), and the downloads it keeps in _deps/ (models, tools) to copy in."""
    if not seed:
        return [], []
    cache = read_cache(os.path.join(seed, tree))
    args = ['-G', cache['CMAKE_GENERATOR']] if cache.get('CMAKE_GENERATOR') else []
    args += ['-D%s=%s' % (k, cache[k]) for k in SEED_KEYS if cache.get(k)]
    srcs, keep = {}, []
    other_t = 'tests' if tree == 'app' else 'app'
    for t, c in ((other_t, read_cache(os.path.join(seed, other_t))), (tree, cache)):   # this tree's own win
        deps = os.path.join(seed, t, '_deps')
        for n in sorted(os.listdir(deps)) if os.path.isdir(deps) else []:
            if n.endswith('-src'):
                srcs[n[:-4].upper()] = os.path.join(deps, n)
            elif t == tree and not n.endswith(('-build', '-subbuild')):
                keep.append(os.path.join(deps, n))
        srcs.update({k[len(FETCH_PREFIX):]: v for k, v in c.items() if k.startswith(FETCH_PREFIX) and v})
    args += ['-D%s%s=%s' % (FETCH_PREFIX, k, v) for k, v in sorted(srcs.items())]
    return args, keep


def copy_tree(src, dst):
    if sys.platform == 'darwin' and subprocess.run(['cp', '-cR', src, dst], stdout=subprocess.DEVNULL,
                                                   stderr=subprocess.DEVNULL).returncode == 0:
        return                                                     # an APFS clone: no bytes copied
    shutil.copytree(src, dst, symlinks=True)


def configure(root, out_dir, seed, extra):
    """cmake -S <root> -B <out>/app and -S <root>/tests -B <out>/tests: 0 if both configure, else 1 (the log names
    each error). Run it through build/refactor/one_at_a_time.sh: it is a heavy job."""
    cmake = shutil.which('cmake')
    if cmake is None:
        print('configure: FAILED: no cmake on the PATH')
        return 1
    if seed is None and os.path.isfile(os.path.join(root, 'build', 'app', 'CMakeCache.txt')):
        seed = os.path.join(root, 'build')
    rc = 0
    for tree, src in (('app', root), ('tests', os.path.join(root, 'tests'))):
        if not os.path.isfile(os.path.join(src, 'CMakeLists.txt')):
            print('configure %s: FAILED: no %s/CMakeLists.txt (a tree not configured is never a pass)' % (
                tree, os.path.relpath(src, root)))
            rc = 1; continue
        b = os.path.join(out_dir, tree)
        if os.path.exists(b) and os.listdir(b):
            print('configure %s: FAILED: %s is not empty (a scratch folder only; never a real build tree)' % (tree, b))
            rc = 1; continue
        args, keep = seed_for(seed, tree)
        os.makedirs(os.path.join(b, '_deps'), exist_ok=True)
        for k in keep:
            copy_tree(k, os.path.join(b, '_deps', os.path.basename(k)))
        log = os.path.join(out_dir, tree + '-configure.log')
        with open(log, 'wb') as f:
            r = subprocess.run([cmake, '-S', src, '-B', b] + args + list(extra), stdout=f, stderr=subprocess.STDOUT)
        with open(log, encoding='utf-8', errors='replace') as f:
            lines = f.read().splitlines()
        errs = [k for k, l in enumerate(lines) if l.startswith('CMake Error')]
        print('configure %s: %s (cmake exit %d, %d CMake errors; log %s; seed %s)' % (
            tree, 'PASSED' if r.returncode == 0 and not errs else 'FAILED', r.returncode, len(errs), log,
            os.path.join(seed, tree) if seed else 'none'))
        for k in errs[:20]:
            for l in lines[k:k + 4]:
                print('    ' + l)
        if r.returncode or errs:
            rc = 1
    print('CONFIGURE %s' % ('FAILED' if rc else 'PASSED'))
    return rc


class Interp:
    """A path token's meaning in one tree (verify's own reading; it shares only the token grammar)."""

    def __init__(self, plan, tree, cvars):
        self.plan, self.tree, self.cvars = plan, tree, cvars

    def meaning(self, tok, path, kind, line=None):
        core, trail = split_trailing(tok)
        scope = self.cvars.scope(path, line) if kind == 'build' else {}
        m = re.match(r'^\$\{(\w+)\}(/.*)?$', core)
        cands = []
        if m and m.group(1) in scope:
            rest = (m.group(2) or '/')[1:]
            cands.append(norm(posixpath.join(scope[m.group(1)], rest)) if rest else scope[m.group(1)])
        elif core.startswith(('./', '../')):
            cands.append(norm(posixpath.join(cmake_base(path) if kind == 'build' else posixpath.dirname(path), core)))
        elif core == 'src' or core.startswith('src/'):
            cands.append(norm(core))
        else:
            if kind == 'build':
                cands.append(norm(posixpath.join(cmake_base(path), core)))
            cands.append(norm('src/' + core))
        for c in cands:
            if c is not None and self.tree.exists(c):
                return c, trail, core.endswith('/')
        c = cands[-1]
        if kind in ('lint', 'context', 'allowlist') and c and c.startswith('src/') and c != 'src':
            if any(c.startswith(o) for o, n in self.plan.dirs) or any(c.startswith(n) for o, n in self.plan.dirs):
                return c, trail, core.endswith('/')               # a fixture's made-up file in a moved folder
        return None


def tokens_and_seps(line):
    toks, seps, last = [], [], 0
    for m in TOKEN_RE.finditer(line):
        seps.append(line[last:m.start()]); toks.append(m.group(0)); last = m.end()
    seps.append(line[last:])
    return toks, seps


def check_pair(old, new, b, w, kind, ip_old, ip_new, plan, code_ok=False, lines=(None, None)):
    """None if the changed line is a move's; else why not."""
    ob, oe = old.rstrip('\r\n'), old[len(old.rstrip('\r\n')):]
    nb, ne = new.rstrip('\r\n'), new[len(new.rstrip('\r\n')):]
    if oe != ne:
        return 'the line ending changed'
    mo, mn = INC_RE.match(ob), INC_RE.match(nb)
    if mo or mn:
        if not (mo and mn) or mo.group(1) != mn.group(1) or mo.group(3) != mn.group(3):
            return 'an include line changed beyond its path'
        h0, t0 = ip_old.tree.resolve(b, mo.group(2))
        h1, t1 = ip_new.tree.resolve(w, mn.group(2))
        if h0 in (None, 'ambiguous'):
            return 'the base include did not resolve'
        if h1 in (None, 'ambiguous') or 'src/' + t1 != plan.fwd('src/' + t0):
            return 'the include now points at %s, not %s' % (t1, plan.fwd('src/' + t0))
        return None
    to, so = tokens_and_seps(ob)
    tn, sn = tokens_and_seps(nb)
    if len(to) != len(tn):
        if kind == 'build' and len(tn) < len(to):
            why = dropped_dirs(to, so, tn, sn, b, ip_old)
            return ('list', 'an include directory removed') if why is None else why
        return 'words were added or removed'
    if so != sn:
        return 'text other than a path changed'
    if kind in ('cpp', 'script'):
        oc, nc = comment_mask(ob), comment_mask(nb)
    for k, (a, c) in enumerate(zip(to, tn)):
        if a == c:
            continue
        if kind in ('cpp', 'script'):
            pa, pc = position(so, to, k), position(sn, tn, k)
            if not (oc[pa] and nc[pc]) and not code_ok:
                return 'a path changed outside a comment or include (behaviour)'
        if kind == 'build':                                        # a variable the line can't see (B1.18's F1)
            inf = ip_new.cvars.info(w, lines[1])
            known = inf.known
            for name in re.findall(r'\$\{(\w+)\}', c):
                if name in known:
                    continue
                if name in inf.doubts:                             # B1.2b: undecided, never a pass
                    return '"%s" -> "%s": can\'t place: ${%s} is undecided at this line (%s)' % (
                        a, c, name, inf.doubts[name][1])
                if inf.body == 'uncalled':
                    return '"%s" -> "%s": can\'t place: a function or macro body no call reaches' % (a, c)
                if name not in re.findall(r'\$\{(\w+)\}', a):
                    where = [x for x in ip_new.cvars.where_set(name) if x != w]
                    return '"%s" -> "%s": ${%s} is not set in this file\'s scope at this line%s' % (
                        a, c, name, ' (set only in %s)' % ', '.join(where[:4]) if where else '')
            if inf.body and '${' not in c:                          # a body reads a relative path from its caller
                base, want = known.get('CMAKE_CURRENT_SOURCE_DIR'), '' if c.startswith('src/') else cmake_base(w)
                if inf.body == 'uncalled' or base != want:
                    return '"%s" -> "%s": can\'t place: in a function or macro body a path is read from the caller\'s ' \
                           'folder (%s), not %s' % (a, c, 'no call reaches it' if inf.body == 'uncalled' else
                                                    'the calls disagree' if base is None else base or 'the root',
                                                    want or 'the root')
        ma, mc = ip_old.meaning(a, b, kind, lines[0]), ip_new.meaning(c, w, kind, lines[1])
        if ma is None:
            return '"%s" named nothing in the base' % a
        if mc is None:
            return '"%s" names nothing now' % c
        if ma[1] != mc[1]:
            return 'trailing text changed'
        if plan.fwd(ma[0]) != mc[0]:
            return '"%s" -> "%s": %s is not the map\'s image of %s' % (a, c, mc[0], ma[0])
        if not plan.moved(ma[0]) and b == w:
            return '"%s" -> "%s": the same place respelled in a file that did not move' % (a, c)
    if code_ok and kind in ('cpp', 'script') and to != tn:
        return ('list', 'a path in code changed to the map\'s image, allowed by --allow-code-path')
    return None


def position(seps, toks, k):
    return sum(len(seps[i]) + len(toks[i]) for i in range(k)) + len(seps[k])


def comment_mask(line):
    cls = spans_cpp(line)
    stripped = line.lstrip()
    whole = stripped.startswith(('*', '/*', '//'))
    return [whole or c == 'm' for c in cls] + [whole]


def dropped_dirs(to, so, tn, sn, b, ip_old):
    """A build line that lost words: allowed only if each lost word was an include directory (a folder) and nothing
    else changed (listed as a removal by the summary of --verify's caller)."""
    i = j = 0
    while i < len(to):
        if j < len(tn) and to[i] == tn[j]:
            i += 1; j += 1; continue
        m = ip_old.meaning(to[i], b, 'build')
        if m is None or ip_old.tree.exists(m[0]) != 'dir':
            return '"%s" was removed and is not a folder' % to[i]
        i += 1
    if j != len(tn):
        return 'words were added or changed'
    return None


def only_dirs(line, b, ip_old):
    body = line.split('#', 1)[0]
    toks, seps = tokens_and_seps(body)
    if not toks or any(s.strip() for s in seps):
        return False
    return all((ip_old.meaning(t, b, 'build') or (None,))[0] is not None
               and ip_old.tree.exists(ip_old.meaning(t, b, 'build')[0]) == 'dir' for t in toks)


def lint_table_lines(text):
    """1-based numbers of the lint file's table lines: inside a table function, an entry or a comment; anywhere, a line
    whose string literal is a path key."""
    lines = set()
    starts = [0]
    for i, ch in enumerate(text):
        if ch == '\n':
            starts.append(i + 1)

    def line_of(off):
        lo, hi = 0, len(starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if starts[mid] <= off:
                lo = mid
            else:
                hi = mid - 1
        return lo + 1
    for fn in LINT_TABLE_FUNCS:
        r = func_body(text, fn)
        if r:
            for n in range(line_of(r[0]) + 1, line_of(r[1])):
                l = text[starts[n - 1]:starts[n] if n < len(starts) else len(text)].strip()
                if l.startswith('//') or 'QStringLiteral("' in l or 'QLatin1String("' in l or re.search(r'"\w[\w/]*"', l):
                    lines.add(n)
    for n, l in enumerate(text.splitlines(), 1):
        if re.search(r'(?:QStringLiteral|QLatin1String)\("[A-Z][\w]*/[\w/]*"\)', l):
            lines.add(n)
    return lines


def context_table_lines(text):
    """1-based numbers of context_check.py's table lines: its DIRS, FILES and L2_GAP literals."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return set()
    lines = set()
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) \
                and node.targets[0].id in ('DIRS', 'FILES', 'L2_GAP'):
            lines.update(range(node.lineno, getattr(node, 'end_lineno', node.lineno) + 1))
    return lines


def check_allowlist(ot, nt, plan, w):
    def entries(t):
        out = []
        for l in t.splitlines():
            s = l.strip()
            if s and not s.startswith('#') and '|' in s:
                f = [x.strip() for x in s.split('|')]
                out.append((f[0], f[1] if len(f) > 1 else '', f[2] if len(f) > 2 else ''))
        return out
    a, b = entries(ot), entries(nt)
    bad = []
    if len(a) != len(b):
        bad.append('%s: %d entries, the base had %d (a line is never added or removed by a move)' % (w, len(b), len(a)))
        return bad
    for x, y in zip(a, b):
        if x[1:] != y[1:]:
            bad.append('%s: %s: rule or count changed (%s -> %s)' % (w, x[0], '|'.join(x[1:]), '|'.join(y[1:])))
        if plan.fwd('src/' + x[0]) != 'src/' + y[0]:
            bad.append('%s: key %s -> %s is not the map\'s image' % (w, x[0], y[0]))
    return bad


# ── --self-test ─────────────────────────────────────────────────────────────────────────────────
def constructs_selftest(tmp, sh, tool, put, get, check):
    """B1.2b's fixture: the CMake constructs whose value the walk decides, or can't (then the line is listed as can't
    place and left as it was, never guessed). Pure Python: it runs on every platform. Each case names what the old
    reading (B1.2a: bodies at their definition, a first set under an if kept, unset/return/PARENT_SCOPE ignored) did."""
    d3 = os.path.join(tmp, 'constructs')
    os.makedirs(d3)
    sh(d3, 'init', '-q')
    H = {
        'CMakeLists.txt': (
            'cmake_minimum_required(VERSION 3.16)\n'                                       # 1
            'project(app NONE)\n'                                                           # 2
            'set(D2 ${CMAKE_SOURCE_DIR}/src/Diagnostics)\n'                                 # 3
            'function(two_calls t)\n'                                                       # 4
            '    add_custom_target(${t} SOURCES ${D2}/engine/e.cpp)\n'                      # 5  calls disagree
            'endfunction()\n'                                                               # 6
            'two_calls(f1)\n'                                                               # 7
            'set(D2 ${CMAKE_SOURCE_DIR}/src/Lab)\n'                                         # 8
            'two_calls(f2)\n'                                                               # 9
            'if(WIN32)\n'                                                                   # 10
            '    set(WD ${CMAKE_SOURCE_DIR}/src/Diagnostics)\n'                             # 11
            '    add_custom_target(i1 SOURCES ${WD}/engine/e.cpp)\n'                        # 12 known in its branch
            '    include(${CMAKE_SOURCE_DIR}/cmake/win.cmake)\n'                            # 13
            'endif()\n'                                                                     # 14
            'add_custom_target(i2 SOURCES ${WD}/engine/e.cpp ${IV}/engine/e.cpp)\n'         # 15 undecided if
            'macro(pick d)\n'                                                               # 16
            '    set(MV ${d})\n'                                                            # 17
            'endmacro()\n'                                                                  # 18
            'set(MV ${CMAKE_SOURCE_DIR}/src/Lab)\n'                                         # 19
            'pick(${CMAKE_SOURCE_DIR}/src/Diagnostics)\n'                                   # 20
            'add_custom_target(m SOURCES ${MV}/engine/e.cpp)\n'                             # 21 the macro's set
            'set(UV ${CMAKE_SOURCE_DIR}/src/Diagnostics)\n'                                 # 22
            'unset(UV)\n'                                                                   # 23
            'add_custom_target(u SOURCES ${UV}/engine/e.cpp)\n'                             # 24 unset
            'include(${CMAKE_SOURCE_DIR}/cmake/ret.cmake)\n'                                # 25
            'add_custom_target(r SOURCES ${RV}/engine/e.cpp)\n'                             # 26 return()
            'set(PV ${CMAKE_SOURCE_DIR}/src/Lab)\n'                                         # 27
            'set(PW ${CMAKE_SOURCE_DIR}/src/Diagnostics)\n'                                 # 28
            'add_subdirectory(src/Sub)\n'                                                   # 29
            'add_custom_target(p SOURCES ${PW}/engine/e.cpp)\n'                             # 30 PARENT_SCOPE
            'function(never_called)\n'                                                      # 31
            '    add_custom_target(nc SOURCES ${D2}/engine/e.cpp)\n'                        # 32 no call
            'endfunction()\n'                                                               # 33
            'add_subdirectory(src/Diagnostics)\n'                                           # 34
            'diag_t(dt2)\n'                                                                 # 35
            'add_custom_target(w SOURCES ${UV}/engine/e.cpp src/Diagnostics/engine/e.cpp)\n'  # 36 left whole
            'add_custom_target(rb SOURCES ${RB}/engine/e.cpp)\n'                            # 37 return() on every path
            'add_custom_target(pv SOURCES ${PV}/engine/e.cpp)\n'),                          # 38 PARENT_SCOPE, every path
        'cmake/win.cmake': 'set(IV ${CMAKE_SOURCE_DIR}/src/Diagnostics)\n',
        'cmake/ret.cmake': ('set(RB ${CMAKE_SOURCE_DIR}/src/Diagnostics)\nif(WIN32)\n    return()\nendif()\n'
                            'set(RV ${CMAKE_SOURCE_DIR}/src/Diagnostics)\nreturn()\nset(RB ${CMAKE_SOURCE_DIR}/src/Lab)\n'),
        'src/Sub/CMakeLists.txt': ('set(PV ${CMAKE_SOURCE_DIR}/src/Diagnostics PARENT_SCOPE)\nif(WIN32)\n'
                                   '    set(PW ${CMAKE_SOURCE_DIR}/src/Lab PARENT_SCOPE)\nendif()\n'),
        'src/Diagnostics/CMakeLists.txt': ('function(diag_t t)\n    add_custom_target(${t} SOURCES engine/e.cpp)\n'
                                           'endfunction()\ndiag_t(dt1)\n'),
        'src/Diagnostics/engine/e.cpp': '// e\n',
        'src/Lab/engine/e.cpp': '// another tree\'s engine/\n',
        'docs/developer/h.md': ('| 1.0 | (dated) moved to `src/Diagnostics/engine/e.cpp` |\n'
                                'Today it is `src/Diagnostics/engine/e.cpp`.\nNo path on this line.\n'),
        'governance/artefacts/g.md': ('| DEC-1 | (dated) made `src/Diagnostics/engine/e.cpp` |\n'   # B1.2c: the IM's
                                      'It lives in `src/Diagnostics/engine/e.cpp`.\n'),               # home today
        'governance/artefacts/tokens.json': '{"usage": "src/Diagnostics/engine/e.cpp"}\n',    # generated: not prose
        'docs/implementation/old.md': 'A dated snapshot: `src/Diagnostics/engine/e.cpp`.\n',  # never scanned
    }
    for k, v in H.items():
        put(d3, k, v)
    sh(d3, 'add', '-A'); sh(d3, 'commit', '-q', '-m', 'base')
    base3 = sh(d3, 'rev-parse', 'HEAD').strip()
    mp3 = os.path.join(tmp, 'c.map')
    put(tmp, 'c.map', 'src/Diagnostics/engine/ -> src/Diagnostics/model/\nkeep: docs/developer/h.md:1   # a dated row\n'
                      'keep: governance/artefacts/g.md:1   # a dated row in a governed artefact\n')
    for args in (('--dry-run',), ('--verify', base3, '--no-configure-ok')):
        rc, out = tool(d3, mp3, *(args + ('--keep', 'docs/developer/h.md:2')))
        check(rc == 2 and '--keep is gone: a kept line goes in the map' in out,
              'keep: --keep on the command line is refused (%s): the map is the only source, so a verify can\'t be '
              'loosened by hand' % args[0])
    for bad_keep in ('CMakeLists.txt:12', 'src/Diagnostics/engine/e.cpp:1'):
        put(tmp, 'k.map', 'src/Diagnostics/engine/ -> src/Diagnostics/model/\nkeep: %s\n' % bad_keep)
        rc, out = tool(d3, os.path.join(tmp, 'k.map'), '--dry-run')
        rc2, out2 = tool(d3, os.path.join(tmp, 'k.map'), '--verify', base3, '--no-configure-ok')
        f = bad_keep.split(':')[0]
        check(rc == 1 and rc2 == 1 and ('keep: %s is a ' % f) in out and ('keep: %s is a ' % f) in out2
              and 'docs only' in out,
              'keep: a keep on a %s line is refused, in the dry run and in --verify' % ('build' if f.endswith('.txt') else 'C++'))
    for unscanned in ('docs/implementation/old.md', 'tools/refactor/README.md'):
        if unscanned not in H:
            put(d3, unscanned, 'Names `src/Diagnostics/engine/e.cpp`.\n')      # untracked: the tool lists it all the same
        put(tmp, 'k.map', 'src/Diagnostics/engine/ -> src/Diagnostics/model/\nkeep: %s:1\n' % unscanned)
        rc, out = tool(d3, os.path.join(tmp, 'k.map'), '--dry-run')
        rc2, out2 = tool(d3, os.path.join(tmp, 'k.map'), '--verify', base3, '--no-configure-ok')
        if unscanned not in H:
            os.remove(os.path.join(d3, unscanned))
        needle = 'keep: %s matches nothing: the tool does not scan that file' % unscanned
        check(rc == 1 and rc2 == 1 and needle in out and needle in out2,
              'keep: a keep on a doc the tool does not scan (%s) matches nothing and is refused, in the dry run and in '
              '--verify (B1.2c: it was dropped without a word)' % unscanned)
    with open(os.path.join(d3, 'governance', 'bin.md'), 'wb') as f:      # untracked, and not text
        f.write(b'`src/Diagnostics/engine/e.cpp`\0\n')
    put(tmp, 'k.map', 'src/Diagnostics/engine/ -> src/Diagnostics/model/\nkeep: governance/bin.md:1\n')
    rc, out = tool(d3, os.path.join(tmp, 'k.map'), '--dry-run')
    os.remove(os.path.join(d3, 'governance', 'bin.md'))
    check(rc == 1 and 'keep: governance/bin.md matches nothing: the tool could not read it as text' in out,
          'keep: a keep on a scanned doc the tool can\'t read as text matches nothing and is refused (B1.2c)')
    put(tmp, 'k.map', 'src/Diagnostics/engine/ -> src/Diagnostics/model/\nkeep: docs/developer/h.md:3\n')
    rc, out = tool(d3, os.path.join(tmp, 'k.map'), '--dry-run')
    check(rc == 1 and 'keep: docs/developer/h.md:3 has nothing the map rewrites' in out,
          'keep: a kept line with nothing to rewrite is an error (a stale line number)')
    rc, out = tool(d3, mp3)
    print('\n'.join('    | ' + l for l in out.splitlines()))
    out_move = out
    check(rc == 0 and 'done: 1 files moved' in out, 'constructs: the move runs')
    hl = get(d3, 'docs/developer/h.md').splitlines()
    check(hl == ['| 1.0 | (dated) moved to `src/Diagnostics/engine/e.cpp` |', 'Today it is `src/Diagnostics/model/e.cpp`.',
                 'No path on this line.']
          and 'docs/developer/h.md:1  kept; the move would have made it:' in out,
          'keep: a dated line is left exactly as it is and named in the report; the next line is rewritten')
    gl = get(d3, 'governance/artefacts/g.md').splitlines()
    check(gl == ['| DEC-1 | (dated) made `src/Diagnostics/engine/e.cpp` |', 'It lives in `src/Diagnostics/model/e.cpp`.']
          and 'governance/artefacts/g.md:1  kept; the move would have made it:' in out
          and 'governance/artefacts/g.md:2  ' in out,
          'governance/: a governed artefact\'s path is rewritten as a doc\'s, and its keep holds (B1.2c)')
    check(get(d3, 'governance/artefacts/tokens.json') == H['governance/artefacts/tokens.json']
          and get(d3, 'docs/implementation/old.md') == H['docs/implementation/old.md'],
          'governance/: only its .md/.txt are read (tokens.json is generated); docs/implementation/ is never touched')
    now = get(d3, 'CMakeLists.txt').splitlines()

    def case(ln, line, needle, what):
        check(now[ln - 1] == line and (needle is None or ('CMakeLists.txt:%d  ' % ln) in out and needle in out), what)

    case(5, '    add_custom_target(${t} SOURCES ${D2}/engine/e.cpp)',
         '${D2} is undecided here (the 2 calls that reach this line give it different values); as src/Diagnostics',
         'function body: two calls give ${D2} two places; the line is left and listed as can\'t place (the old '
         'reading used the definition site\'s src/Diagnostics and wrote ${D2}/model/, wrong for the second call)')
    case(32, '    add_custom_target(nc SOURCES ${D2}/engine/e.cpp)', 'in a function or macro body no call reaches',
         'function body: a body no call reaches knows no variable; listed as can\'t place')
    check(get(d3, 'src/Diagnostics/CMakeLists.txt').splitlines()[1] == '    add_custom_target(${t} SOURCES engine/e.cpp)'
          and "src/Diagnostics/CMakeLists.txt:2  engine/e.cpp: CMake scope: a path in a function or macro body is read " \
              "from the caller's folder (the calls disagree)" in out,
          'function body: a relative path, called from two folders, is left and listed (CMake reads it from each caller)')
    case(12, '    add_custom_target(i1 SOURCES ${WD}/model/e.cpp)', None,
         'undecided if: a first set is known inside its own branch (src/Gui\'s PP_ROOT)')
    case(15, 'add_custom_target(i2 SOURCES ${WD}/engine/e.cpp ${IV}/engine/e.cpp)',
         '${WD} is undecided here (set under the undecided if at CMakeLists.txt:10)',
         'undecided if: after endif() a set and an include() under it are can\'t place (the old reading kept both)')
    check('${IV} is undecided here (set under the undecided if at CMakeLists.txt:10)' in out,
          'undecided if: the include()d file\'s set is named too')
    case(21, 'add_custom_target(m SOURCES ${MV}/model/e.cpp)', None,
         'macro: its set changes the caller\'s ${MV} (the old reading skipped the body and kept src/Lab, a stale line)')
    case(24, 'add_custom_target(u SOURCES ${UV}/engine/e.cpp)', '${UV} is undecided here (unset() at CMakeLists.txt:23)',
         'unset(): the variable is gone; listed as can\'t place (the old reading kept it and rewrote the line)')
    check(now[36] == 'add_custom_target(rb SOURCES ${RB}/model/e.cpp)' and now[37] == 'add_custom_target(pv SOURCES ${PV}/model/e.cpp)',
          'return() on every path and a PARENT_SCOPE set on every path are modelled: those lines are rewritten')
    case(26, 'add_custom_target(r SOURCES ${RV}/engine/e.cpp)',
         '${RV} is undecided here (return() at cmake/ret.cmake:3 ends ret.cmake on some paths only)',
         'return(): a set after one that runs on some paths is can\'t place; one on every path stops the file, so '
         '${RB} keeps the value set before it (the old reading took the set after it)')
    case(30, 'add_custom_target(p SOURCES ${PW}/engine/e.cpp)',
         '${PW} is undecided here (set(PW … PARENT_SCOPE) at src/Sub/CMakeLists.txt:3 runs on some paths only)',
         'PARENT_SCOPE: a child\'s set writes the parent\'s ${PV} after add_subdirectory(); one under an undecided if '
         'leaves ${PW} can\'t place (the old reading ignored both)')
    rc, out = tool(d3, mp3, '--verify', base3, '--no-configure-ok')
    check(rc == 0 and 'VERIFY PASSED' in out and "CMakeLists.txt:15 (base): can't place ${WD}/engine/e.cpp" in out,
          'constructs: --verify passes the move and lists each can\'t-place line for the reviewer')
    check("CMakeLists.txt:5 (base): can't place ${D2}/engine/e.cpp" in out,
          'constructs: --verify lists the base reading\'s can\'t-place lines, so the two-calls line (whose values '
          'name no moved path once moved) does not drop out of the list')
    check(now[35] == 'add_custom_target(w SOURCES ${UV}/engine/e.cpp src/Diagnostics/engine/e.cpp)'
          and "CMakeLists.txt:36  src/Diagnostics/engine/e.cpp: CMake scope: left with its line" in out_move
          and "CMakeLists.txt:36 (base): can't place src/Diagnostics/engine/e.cpp: left with its line" in out,
          'a can\'t-place line is left WHOLE: its rewritable neighbour is not rewritten, and is listed as left with it')

    def mutate3(what, fn, needle):
        before = get(d3, 'CMakeLists.txt')
        put(d3, 'CMakeLists.txt', fn(before))
        rc, out = tool(d3, mp3, '--verify', base3, '--no-configure-ok')
        put(d3, 'CMakeLists.txt', before)
        check(rc == 1 and needle in out, what)
        if not (rc == 1 and needle in out):
            print('\n'.join('    | ' + l for l in out.splitlines()))

    rc, out = tool(d3, mp3, '--verify', base3, '--no-configure-ok')
    check(rc == 0 and 'docs/developer/h.md:1: kept as it was (--keep / keep:)' in out
          and 'governance/artefacts/g.md:1: kept as it was (--keep / keep:)' in out,
          'keep: --verify accepts the old spelling on the kept line and names it (in docs/ and in governance/)')

    def mutate_doc(what, fn, needle, extra=(), want=1, rel='docs/developer/h.md'):
        before = get(d3, rel)
        put(d3, rel, fn(before))
        rc, out = tool(d3, mp3, '--verify', base3, '--no-configure-ok', *extra)
        put(d3, rel, before)
        check(rc == want and needle in out, what)
        if not (rc == want and needle in out):
            print('\n'.join('    | ' + l for l in out.splitlines()))

    mutate_doc('keep: verify fails the old spelling on a line that is not kept',
               lambda s: s.replace('Today it is `src/Diagnostics/model/', 'Today it is `src/Diagnostics/engine/'),
               'docs/developer/h.md:2: the map still rewrites')
    mutate_doc('keep: verify fails when a kept line is changed (even to the new spelling)',
               lambda s: s.replace('(dated) moved to `src/Diagnostics/engine/', '(dated) moved to `src/Diagnostics/model/'),
               'docs/developer/h.md:1 (base): a kept line (--keep / keep:) changed')
    mutate_doc('governance/: verify\'s last pass fails the old spelling left in a governed artefact (B1.2c)',
               lambda s: s.replace('lives in `src/Diagnostics/model/', 'lives in `src/Diagnostics/engine/'),
               'governance/artefacts/g.md:2: the map still rewrites', rel='governance/artefacts/g.md')
    prose = ('--allow-prose', 'docs/developer/h.md')               # the IM's case: its rows are edited by hand too
    mutate_doc('keep: under --allow-prose, a line added above the kept one: verify finds it at its new line and passes',
               lambda s: '| 1.1 | a row added by hand |\n' + s, 'docs/developer/h.md:2: kept as it was', prose, 0)
    mutate_doc('keep: under --allow-prose, a kept line edited still fails',
               lambda s: s.replace('(dated) moved', '(dated, edited) moved'),
               'docs/developer/h.md:1 (base): a kept line (--keep / keep:) changed', prose)
    mutate3("verify fails: the old reading's function-body spelling (${D2}/model/)",
            lambda s: s.replace('SOURCES ${D2}/engine/e.cpp)\nendfunction()\ntwo_calls', 'SOURCES ${D2}/model/e.cpp)\nendfunction()\ntwo_calls'),
            "can't place: ${D2} is undecided at this line")
    mutate3("verify fails: a rewrite through a variable set under an undecided if",
            lambda s: s.replace('i2 SOURCES ${WD}/engine/', 'i2 SOURCES ${WD}/model/'),
            "can't place: ${WD} is undecided at this line (set under the undecided if at CMakeLists.txt:10)")


def hand_selftest(tmp, sh, tool, put, get, check):
    """The hand split (hand:, after B1.7): a header cut by hand out of another after the move, as B1.7 cut
    capture_time_types.h out of swing_analysis.h. Declared in the map, --verify accepts exactly its lines (the taken
    blocks, the new file's scaffold, the source's include of it, an includer re-pointed at it, a build list's entry);
    undeclared, or with anything else changed, it fails. Pure Python: it runs on every platform."""
    d4 = os.path.join(tmp, 'hand')
    os.makedirs(d4)
    sh(d4, 'init', '-q')
    big = ('#pragma once\n'                                        # 1
           '#include "../Core/log.h"\n'                            # 2
           '#include "seg.h"\n'                                    # 3  the map moves seg.h
           '\n'                                                    # 4
           'namespace pp::analysis {\n'                            # 5
           '\n'                                                    # 6
           '// A binding.\n'                                       # 7  taken
           'struct Binding {\n'                                    # 8
           '    int role = 0;\n'                                   # 9
           '};\n'                                                  # 10
           '\n'                                                    # 11 taken
           'inline int f() { return 1; }\n'                        # 12 stays
           '\n'                                                    # 13
           '// A track.\n'                                         # 14 taken
           'struct Track {\n'                                      # 15
           '    int n = 0;\n'                                      # 16
           '};\n'                                                  # 17
           '\n'                                                    # 18 taken
           '} // namespace pp::analysis\n')                        # 19
    H = {
        'src/Analysis/big.h': big,
        'src/Analysis/seg.h': '#pragma once\nstruct Seg {};\n',
        'src/Core/log.h': '#pragma once\n',
        'src/Recording/rec.h': '#pragma once\n#include "../Analysis/big.h"    // Binding\nstruct Rec {};\n',
        'src/Gui/user.cpp': '#include "../Analysis/big.h"\n#include "../Core/log.h"\nint u() { return pp::analysis::f(); }\n',
        'CMakeLists.txt': 'add_executable(app\n    src/Analysis/big.h\n    src/Analysis/seg.h\n    src/Recording/rec.h\n'
                          '    src/Gui/user.cpp)\n',
    }
    for k, v in H.items():
        put(d4, k, v)
    sh(d4, 'add', '-A'); sh(d4, 'commit', '-q', '-m', 'base')
    base4 = sh(d4, 'rev-parse', 'HEAD').strip()
    move = 'src/Analysis/seg.h -> src/Common/\n'
    hand = 'hand: src/Recording/types.h <- src/Analysis/big.h:7-11,14-18   # the brief\'s split\n'
    put(tmp, 'h.map', move + hand)
    put(tmp, 'h0.map', move)
    mp4, mp0 = os.path.join(tmp, 'h.map'), os.path.join(tmp, 'h0.map')
    rc, out = tool(d4, mp4, '--dry-run')
    check(rc == 0, 'hand: a declared split passes the dry run before the split exists (the move makes no split)')
    rc, out = tool(d4, mp4)
    check(rc == 0 and 'done: 1 files moved' in out, 'hand: the move runs with a hand: line in its map')
    # the brief's hand split, after the move: two blocks cut into a new header, the source includes it, one includer
    # and the build list point at it
    put(d4, 'src/Recording/types.h', '/*\n * The capture types (licence block).\n */\n#pragma once\n\n#include "../Core/log.h"\n\n'
        'namespace pp::analysis {\n\n' + ''.join(big.splitlines(True)[6:11]) + ''.join(big.splitlines(True)[13:18]) +
        '} // namespace pp::analysis\n')
    b = get(d4, 'src/Analysis/big.h').splitlines(True)
    put(d4, 'src/Analysis/big.h', ''.join(b[:3] + ['#include "../Recording/types.h" // Binding, Track\n'] + b[3:6] + b[11:13]
                                          + b[18:]))
    put(d4, 'src/Recording/rec.h', get(d4, 'src/Recording/rec.h').replace('"../Analysis/big.h"    //', '"types.h"    //'))
    put(d4, 'CMakeLists.txt', get(d4, 'CMakeLists.txt').replace('    src/Recording/rec.h\n',
                                                                '    src/Recording/rec.h\n    src/Recording/types.h\n'))
    rc, out = tool(d4, mp4, '--verify', base4, '--no-configure-ok')
    ok = rc == 0 and 'VERIFY PASSED' in out and 'hand split src/Recording/types.h: 10 lines taken byte for byte' in out \
        and 'src/Analysis/big.h:4: an include of its hand split src/Recording/types.h added' in out \
        and 'src/Recording/rec.h:2: "types.h" re-pointed from src/Analysis/big.h to its hand split' in out \
        and 'CMakeLists.txt:5: a line naming the hand split src/Recording/types.h added' in out
    check(ok, 'hand: a declared hand split passes --verify, each of its lines listed for the reviewer')
    if not ok:
        print('\n'.join('    | ' + l for l in out.splitlines()))
    rc, out = tool(d4, mp0, '--verify', base4, '--no-configure-ok')
    ok = rc == 1 and 'added file src/Recording/types.h: not a move of the map' in out \
        and '(base): line deleted: struct Binding {' in out and 'rec.h:2: the include now points at Recording/types.h' in out
    check(ok, 'hand: the same split undeclared fails --verify (the new file, the taken lines, the re-pointed include)')
    if not ok:
        print('\n'.join('    | ' + l for l in out.splitlines()))

    def hand_case(what, hand_line, needle, extra_rel=None, fn=None, run=('--verify', base4, '--no-configure-ok')):
        put(tmp, 'hx.map', move + hand_line)
        before = get(d4, extra_rel) if extra_rel else None
        if extra_rel:
            put(d4, extra_rel, fn(before))
        rc, out = tool(d4, os.path.join(tmp, 'hx.map'), *run)
        if extra_rel:
            put(d4, extra_rel, before)
        check(rc == 1 and needle in out, what)
        if not (rc == 1 and needle in out):
            print('\n'.join('    | ' + l for l in out.splitlines()))

    hand_case('hand: refused when it matches nothing: no such new file in the tree',
              'hand: src/Recording/nothere.h <- src/Analysis/big.h:7-11\n', 'nothere.h matches nothing: no such file')
    hand_case('hand: refused when it matches nothing: the new file does not hold the declared lines',
              hand.replace('7-11,14-18', '7-11,12,14-18'), 'matches nothing: it does not hold src/Analysis/big.h:12')
    hand_case('hand: refused when it matches nothing: a range past the source\'s end',
              hand.replace('14-18', '14-40'), 'big.h:14-40 matches nothing: the source has 19 lines')
    hand_case('hand: refused when it matches nothing: a source not in the tree (the move refuses it too)',
              'hand: src/Recording/types.h <- src/Analysis/gone.h:1-2\n', 'gone.h matches nothing: the source is not in the tree',
              run=('--dry-run',))
    hand_case('hand: a malformed hand: line is refused', 'hand: src/Recording/types.h src/Analysis/big.h:7\n',
              'want "hand: <new file> <- <source file>:<lines>"')
    hand_case('hand: a declared split plus an unrelated bad include still fails', hand,
              '"../Core/log.h" resolves to', 'src/Gui/user.cpp',
              lambda s: s.replace('#include "../Analysis/big.h"', '#include "../Core/log.h"'))
    hand_case('hand: a taken line edited in the new file fails (taken byte for byte)', hand,
              'matches nothing: it does not hold src/Analysis/big.h:7-11', 'src/Recording/types.h',
              lambda s: s.replace('int role = 0;', 'int role = 1;'))
    hand_case('hand: a code line added to the new file fails (only scaffold besides the taken lines)', hand,
              'neither taken from its source nor scaffold', 'src/Recording/types.h',
              lambda s: s.replace('} // namespace', 'inline int g() { return 2; }\n} // namespace'))
    tb = get(d4, 'src/Recording/types.h')
    put(d4, 'src/Recording/types.h', tb.replace('#pragma once\n', '#ifndef TYPES_H\n#define TYPES_H\n', 1) + '#endif // TYPES_H\n')
    rc, out = tool(d4, mp4, '--verify', base4, '--no-configure-ok')
    put(d4, 'src/Recording/types.h', tb)
    check(rc == 0 and 'VERIFY PASSED' in out, 'hand: an include guard (#ifndef, #define, #endif) in the new file is scaffold')
    hand_case('hand: a #define added to the new file fails (an include guard only)', hand,
              'neither taken from its source nor scaffold (blank, a comment, #pragma once, an include, a guard, a '
              'namespace line): #define PP_SWITCH', 'src/Recording/types.h',
              lambda s: s.replace('#pragma once\n', '#pragma once\n#ifndef TYPES_H\n#define TYPES_H\n#define PP_SWITCH\n', 1)
              .replace('} // namespace pp::analysis\n', '} // namespace pp::analysis\n#endif\n'))
    hand_case('hand: the taken blocks in another namespace fail', hand, 'sits in ns:pp::ns:other there, in ns:pp::ns:analysis',
              'src/Recording/types.h', lambda s: s.replace('namespace pp::analysis {', 'namespace pp::other {'))
    hand_case('hand: a taken line left in the source too fails (a split moves the lines)', hand,
              'src/Analysis/big.h:9: line added: struct Binding {', 'src/Analysis/big.h',
              lambda s: s.replace('inline int f()', '// A binding.\nstruct Binding {\n    int role = 0;\n};\n\ninline int f()'))
    hand_case('hand: an include of the split added outside its source fails', hand,
              'src/Gui/user.cpp: 3 includes, the base had 2', 'src/Gui/user.cpp',
              lambda s: '#include "../Recording/types.h"\n' + s)
    hand_case('hand: an includer of the source re-pointed at another file still fails', hand,
              'src/Recording/rec.h:2: "../Core/log.h" resolves to', 'src/Recording/rec.h',
              lambda s: s.replace('"types.h"', '"../Core/log.h"'))
    hand_case('hand: a build line naming another new file still fails', hand,
              'CMakeLists.txt:6: line added:     src/Recording/other.h', 'CMakeLists.txt',
              lambda s: s.replace('    src/Recording/types.h\n', '    src/Recording/types.h\n    src/Recording/other.h\n'))
    rc, out = tool(d4, mp4, '--verify', base4, '--no-configure-ok')
    check(rc == 0, 'hand: verify passes again once each case is undone')


def self_test():
    if shutil.which('git') is None:
        print('move_files_selftest: SKIPPED: no git on the PATH')
        return 77
    env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
    env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1', GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t',
               GIT_COMMITTER_NAME='t', GIT_COMMITTER_EMAIL='t@t', GIT_TERMINAL_PROMPT='0')
    me = os.path.abspath(__file__)
    fails = []

    def check(cond, what):
        print(('  ok    ' if cond else '  FAIL  ') + what)
        if not cond:
            fails.append(what)

    def sh(d, *args):
        r = subprocess.run(['git', '-C', d, '-c', 'commit.gpgsign=false', '-c', 'core.autocrlf=false'] + list(args),
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        if r.returncode:
            raise Fail('git %s: %s' % (args, r.stdout.decode('utf-8', 'replace')))
        return r.stdout.decode('utf-8', 'replace')

    def tool(d, *args):
        r = subprocess.run([sys.executable, '-I', me, '--root', d] + list(args), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, env=env)
        return r.returncode, r.stdout.decode('utf-8', 'replace')

    def put(d, rel, text):
        p = os.path.join(d, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(text.encode('utf-8'))

    def get(d, rel):
        with open(os.path.join(d, rel), 'rb') as f:
            return f.read().decode('utf-8')

    def snapshot(d):
        out = {}
        for top, dirs, names in os.walk(d):
            dirs[:] = [x for x in dirs if x != '.git']
            for n in names:
                p = os.path.join(top, n)
                with open(p, 'rb') as f:
                    out[os.path.relpath(p, d)] = f.read()
        return out

    def onerror(func, path, _):
        os.chmod(path, stat.S_IWRITE)
        func(path)

    tmp = tempfile.mkdtemp(prefix='move_files_selftest.')
    try:
        d = os.path.join(tmp, 'repo')
        os.makedirs(d)
        sh(d, 'init', '-q')
        F = {
            'src/Metrics/m.h': '#pragma once\n#include "../Core/log.h"\n#include "variability/v.h"\nint m();\n',
            'src/Metrics/m.cpp': '#include "m.h"\n#include "Core/log.h"\n// see src/Core/log.h and Metrics/m.h\nint m() { return 1; }\n',
            'src/Metrics/variability/v.h': '#pragma once\n#include "../m.h"\n',
            'src/Metrics/tests/m_test.cpp': '#include "../m.h"\n',
            'src/Metrics/dup.h': '#pragma once\n',
            'src/Video/dup.h': '#pragma once\n',
            'src/Core/log.h': '#pragma once\n',
            'src/Core/club.h': '#pragma once\n#include "log.h"\n',
            'src/Core/club.cpp': '#include "club.h"\n',
            'src/Gui/g.cpp': '#include "m.h"\n#include "club.h"\n#include "../Metrics/variability/v.h"\n'
                             '#include "variability/v.h"\nconst char *p = "Metrics/m.h";\n',
            'src/Gui/dupuser.cpp': '#include "dup.h"\n',
            'src/Analysis/a.cpp': '#include "../Metrics/m.h"\n#include "Metrics/m.h"\n#include "../Core/club.h"\n',
            'src/Video/cam.mm': '#import "../Metrics/m.h"\n#include <Metrics/notthere.h>\n',
            'src/Analysis/tests/CMakeLists.txt':
                'set(ANALYSIS ${CMAKE_CURRENT_SOURCE_DIR}/..)\nset(SRC ${ANALYSIS}/..)\n'
                'pp_add_test(t SOURCES t.cpp ${SRC}/Metrics/m.cpp ${SRC}/Core/club.cpp\n    INCLUDE ${SRC}/Metrics ${SRC}/Core)\n',
            'src/Analysis/tests/t.cpp': '#include "m.h"\n#include "club.h"\n',
            'CMakeLists.txt': 'add_executable(app src/Metrics/m.cpp src/Core/club.cpp src/Gui/g.cpp)\n'
                              'target_include_directories(app PRIVATE src/Gui src/Core src/Metrics)\n'
                              '# the Metrics/ folder; src/Metrics/variability/ too\n',
            LINT: '// lint\nconst X &dirRings()\n{\n    static const X t{\n        { QStringLiteral("Metrics/"), Vocabulary },\n'
                  '        { QStringLiteral("Core/"), Vocabulary },\n        { QStringLiteral("Gui/"), Adapters },\n    };\n    return t;\n}\n'
                  'const X &fileRings()\n{\n    static const X t{\n        { QStringLiteral("Core/club"), Vocabulary },\n    };\n    return t;\n}\n'
                  'bool r(const QString &rel) { return rel.startsWith(QLatin1String("Metrics/")); }\n',
            ALLOWLIST: '# allowlist\nMetrics/m.cpp | A5 | 1 | reads src/Metrics/m.h  # kept\nGui/g.cpp | A1 | 2 | why  # kept\n',
            CONTEXT: "DIRS = {\n    'Metrics/': DIA,\n    'Core/': S,\n    'Gui/': HUB,\n}\nFILES = {\n    'Gui/g': HUB,\n}\n",
            'tools/x.py': "SRC = 'src/Metrics/m.h'\n",
            'tools/y.py': 'import os\nROOT = "."\np = os.path.join(ROOT, "src", "Metrics", "m.h")\nq = "Metrics/" + "m.h"\n'
                          't = ("Metrics", "Recording")\n',
            'docs/developer/guide.md': 'Read `src/Metrics/m.h`, [link](../../src/Core/club.h), and the `Metrics/` folder.\n',
            'docs/implementation/old.md': 'src/Metrics/m.h (a dated snapshot)\n',
        }
        for k, v in F.items():
            put(d, k, v)
        put(d, '.gitignore', 'build/\n')
        with open(os.path.join(d, 'src/Gui/b.png'), 'wb') as f:
            f.write(b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR')
        sh(d, 'add', '-A'); sh(d, 'commit', '-q', '-m', 'base')
        base = sh(d, 'rev-parse', 'HEAD').strip()
        mp = os.path.join(tmp, 'm.map')
        put(tmp, 'm.map', '# a test map\nsrc/Metrics/ -> src/Vocabulary/Metrics/\nsrc/Core/club → src/Vocabulary/Club/\n')
        print('fixture: %s' % d)

        snap = snapshot(d)
        rc, out = tool(d, mp, '--dry-run')
        check(snapshot(d) == snap, 'a dry run changes nothing (every byte the same)')
        check(rc == 1 and 'ERROR' in out and '"dup.h" is ambiguous' in out, 'an ambiguous basename is an error, never a pick')
        rc, out = tool(d, mp)
        check(rc == 1 and 'refused' in out and snapshot(d) == snap, 'the real run refuses while there are errors, changing nothing')
        os.remove(os.path.join(d, 'src/Video/dup.h')); sh(d, 'commit', '-qam', 'no dup')
        base = sh(d, 'rev-parse', 'HEAD').strip()
        snap = snapshot(d)
        rc, out = tool(d, mp, '--dry-run')
        print('\n'.join('    | ' + l for l in out.splitlines()))
        check(rc == 0 and snapshot(d) == snap, 'the dry run is clean once the basename is unique')
        check('files to move: 7' in out, 'a directory (with tests/ and a subfolder) and a stem (.h, .cpp) expand from the tree')
        check("names the moved folder src/Metrics/" in out and 'docs/developer/guide.md' in out,
              "a bare old folder name in a doc is reported, not rewritten")
        check('a path in a C++ string literal is behaviour' in out, 'a path in a C++ string literal is reported, not rewritten')
        check('tools/y.py:3' in out and 'split across a join names src/Metrics/m.h' in out,
              'a path split across os.path.join is reported as can\'t place, never skipped')
        check('tools/y.py:4' in out and 'built in parts from src/Metrics/' in out and 'tools/y.py:5' not in out,
              'a path built in parts ("Metrics/" + name) is reported; a tuple of folder names is not')
        check('include directories re-pathed' in out and 'src/Metrics -> src/Vocabulary/Metrics' in out
              and '${SRC}/Metrics -> ${SRC}/Vocabulary/Metrics' in out, 'include directories are re-pathed and listed')
        check('NO TAG' in out and 'src/Vocabulary/Club/' in out, "a stem that loses its context tag is reported")
        check('ring changes' not in out, 'rings: the re-keyed lint tables keep every moved file in its ring')

        put(d, 'src/Gui/wip.cpp', '#include "club.h"\n')
        rc, out = tool(d, mp)
        check(rc == 1 and 'not clean' in out, 'a dirty tree is refused')
        os.remove(os.path.join(d, 'src/Gui/wip.cpp'))
        put(d, 'src/Gui/g.cpp', F['src/Gui/g.cpp'] + '// wip\n')
        rc, out = tool(d, mp, '--over-work')
        check(rc == 0 and 'done: 7 files moved' in out, '--over-work replays the move over uncommitted work')
        put(d, 'src/Gui/g.cpp', get(d, 'src/Gui/g.cpp').replace('// wip\n', ''))
        g = get(d, 'src/Gui/g.cpp')
        check(g.startswith('#include "../Vocabulary/Metrics/m.h"\n#include "../Vocabulary/Club/club.h"\n'
                           '#include "../Vocabulary/Metrics/variability/v.h"\n#include "../Vocabulary/Metrics/variability/v.h"\n'),
              'bare and include-directory includes become the relative path; a ../ path is recomputed')
        check('"Metrics/m.h";' in g, 'the C++ string literal is untouched')
        a = get(d, 'src/Analysis/a.cpp')
        check(a == '#include "../Vocabulary/Metrics/m.h"\n#include "Vocabulary/Metrics/m.h"\n#include "../Vocabulary/Club/club.h"\n',
              'a path under src/ keeps its style; a relative one is recomputed')
        mh = get(d, 'src/Vocabulary/Metrics/m.h')
        check('#include "../../Core/log.h"' in mh and '#include "variability/v.h"' in mh,
              'an outward include inside a moved file gains a ../; one inside the moved folder is kept')
        mc = get(d, 'src/Vocabulary/Metrics/m.cpp')
        check('// see src/Core/log.h and Vocabulary/Metrics/m.h' in mc and '#include "Core/log.h"' in mc,
              'a comment path is re-pathed; an include under src/ of a file that stays is kept')
        check(get(d, 'src/Vocabulary/Club/club.h').endswith('#include "../../Core/log.h"\n'),
              'a stem-moved file including a sibling that stayed gets the relative path')
        check(get(d, 'src/Video/cam.mm').startswith('#import "../Vocabulary/Metrics/m.h"\n'), 'an #import line is rewritten')
        t = get(d, 'src/Analysis/tests/t.cpp')
        check(t == '#include "../../Vocabulary/Metrics/m.h"\n#include "../../Vocabulary/Club/club.h"\n',
              "bare includes that relied on the test's own INCLUDE ${SRC}/Metrics become relative")
        cm = get(d, 'src/Analysis/tests/CMakeLists.txt')
        check('${SRC}/Vocabulary/Metrics/m.cpp ${SRC}/Vocabulary/Club/club.cpp' in cm
              and 'INCLUDE ${SRC}/Vocabulary/Metrics ${SRC}/Core)' in cm, 'a test list and its INCLUDE are re-pathed, none added')
        root = get(d, 'CMakeLists.txt')
        check('src/Vocabulary/Metrics/m.cpp src/Vocabulary/Club/club.cpp' in root
              and 'PRIVATE src/Gui src/Core src/Vocabulary/Metrics)' in root and 'src/Vocabulary/Metrics/variability/' in root
              and 'the Metrics/ folder' in root, 'the root list is re-pathed; a bare folder name in a comment is not')
        lt = get(d, LINT)
        check('QStringLiteral("Vocabulary/Metrics/"), Vocabulary' in lt and 'QStringLiteral("Vocabulary/Club/club"), Vocabulary' in lt
              and 'QLatin1String("Vocabulary/Metrics/")' in lt and 'QStringLiteral("Core/"), Vocabulary' in lt,
              "the lint's tables and rules are re-keyed")
        al = get(d, ALLOWLIST)
        check(al.count('\n') == F[ALLOWLIST].count('\n') and 'Vocabulary/Metrics/m.cpp | A5 | 1 | reads src/Vocabulary/Metrics/m.h' in al,
              'the allowlist line is re-keyed, none added')
        cx = get(d, CONTEXT)
        check("'Vocabulary/Metrics/': DIA" in cx and "'Gui/g': HUB" in cx, "context_check.py's tables are re-keyed")
        check(get(d, 'tools/x.py') == "SRC = 'src/Vocabulary/Metrics/m.h'\n", 'a tool path is re-pathed')
        check(get(d, 'docs/developer/guide.md') == 'Read `src/Vocabulary/Metrics/m.h`, [link](../../src/Vocabulary/Club/club.h), '
              'and the `Metrics/` folder.\n', 'a doc path and link are re-pathed; the bare folder name is not')
        check(get(d, 'docs/implementation/old.md') == F['docs/implementation/old.md'], 'a dated snapshot is not touched')
        check(not os.path.exists(os.path.join(d, 'src/Metrics')), 'the emptied folder is gone')

        snap = snapshot(d)
        rc, out = tool(d, mp, '--over-work')
        check(rc == 0 and snapshot(d) == snap and 'files to move: 0' in out, 'idempotent: a second run changes nothing')

        rc, out = tool(d, mp, '--verify', base)
        check(rc == 3 and 'VERIFY PASSED' in out and 'NOT CONFIGURED: this is not a pass' in out,
              '--verify without --configure says NOT CONFIGURED and exits 3: never a silent pass (B1.2b)')
        rc, out = tool(d, mp, '--verify', base, '--no-configure-ok')
        print('\n'.join('    | ' + l for l in out.splitlines()))
        check(rc == 0 and 'VERIFY PASSED' in out, '--verify passes on the tool\'s own move (--no-configure-ok)')

        def mutate(what, rel, fn, expect_rc=1, needle=None, extra=()):
            before = get(d, rel)
            put(d, rel, fn(before))
            rc, out = tool(d, mp, '--verify', base, '--no-configure-ok', *extra)
            put(d, rel, before)
            ok = rc == expect_rc and (needle is None or needle in out)
            check(ok, what)
            if not ok:
                print('\n'.join('    | ' + l for l in out.splitlines()))

        mutate('verify fails: an include retargeted to another header', 'src/Gui/g.cpp',
               lambda s: s.replace('"../Vocabulary/Club/club.h"', '"../Core/log.h"', 1))
        mutate('verify fails: an include deleted', 'src/Analysis/a.cpp', lambda s: s.split('\n', 1)[1])
        mutate('verify fails: a code line changed in a renamed file', 'src/Vocabulary/Metrics/m.cpp',
               lambda s: s.replace('return 1', 'return 2'))
        mutate('verify fails: an allowlist line added', ALLOWLIST, lambda s: s + 'Core/log.h | A1 | 1 | new  # kept\n')
        mutate('verify fails: an include directory added', 'CMakeLists.txt',
               lambda s: s.replace('src/Vocabulary/Metrics)', 'src/Vocabulary/Metrics src/Vocabulary/Club)'))
        mutate('verify fails: a line ending changed only', 'tools/x.py', lambda s: s.replace('\n', '\r\n'))
        mutate('verify lists, and passes, a hand edit of context_check.py\'s tables (a tag added)', CONTEXT,
               lambda s: s.replace("    'Core/': S,\n", "    'Core/': S,\n    'Vocabulary/Club/': S,\n"), 0,
               'a context table line added')
        mutate('verify fails: a line deleted from a build file that is not an include directory', 'CMakeLists.txt',
               lambda s: s.replace('# the Metrics/ folder; src/Vocabulary/Metrics/variability/ too\n', ''))
        mutate('verify fails: a path respelled in C++ code (a string literal)', 'src/Gui/g.cpp',
               lambda s: s.replace('"Metrics/m.h";', '"Vocabulary/Metrics/m.h";'))
        mutate('verify lists, and passes, that respelling on the one line --allow-code-path names', 'src/Gui/g.cpp',
               lambda s: s.replace('"Metrics/m.h";', '"Vocabulary/Metrics/m.h";'), 0, 'allowed by --allow-code-path',
               ('--allow-code-path', 'src/Gui/g.cpp:5'))
        mutate('verify fails: --allow-code-path names another line', 'src/Gui/g.cpp',
               lambda s: s.replace('"Metrics/m.h";', '"Vocabulary/Metrics/m.h";'), 1, None,
               ('--allow-code-path', 'src/Gui/g.cpp:4'))
        mutate('verify fails: a stale spelling put back on a line the diff never touched (the rewriter\'s final pass)',
               'src/Analysis/tests/CMakeLists.txt', lambda s: s.replace('${SRC}/Vocabulary/Metrics/m.cpp', '${SRC}/Metrics/m.cpp'),
               1, 'the map still rewrites')
        png = os.path.join(d, 'src/Gui/b.png')
        with open(png, 'rb') as f:
            pb = f.read()
        with open(png, 'ab') as f:
            f.write(b'\0')
        rc, out = tool(d, mp, '--verify', base, '--no-configure-ok')
        with open(png, 'wb') as f:
            f.write(pb)
        check(rc == 1 and 'a binary file changed' in out, 'verify fails: a binary file changed')
        if os.name != 'nt':
            xp = os.path.join(d, 'tools/x.py')
            os.chmod(xp, 0o755)
            rc, out = tool(d, mp, '--verify', base, '--no-configure-ok')
            os.chmod(xp, 0o644)
            check(rc == 1 and 'the file mode changed' in out, 'verify fails: a file mode changed only')
        mutate('verify lists, and passes, a lint table line deleted by hand', LINT,
               lambda s: s.replace('        { QStringLiteral("Vocabulary/Club/club"), Vocabulary },\n', ''), 0,
               'a lint table line deleted')
        mutate('verify passes an include directory removed by hand', 'CMakeLists.txt',
               lambda s: s.replace(' src/Vocabulary/Metrics)', ')'), 0)
        mutate('verify fails: an include put back to its base spelling (only the resolution pass sees it)', 'src/Gui/g.cpp',
               lambda s: s.replace('#include "../Vocabulary/Metrics/m.h"\n', '#include "m.h"\n', 1), 1,
               'only through an include directory')
        sh(d, 'mv', 'src/Core/log.h', 'src/Vocabulary/log.h')
        rc, out = tool(d, mp, '--verify', base, '--no-configure-ok')
        sh(d, 'mv', 'src/Vocabulary/log.h', 'src/Core/log.h')
        check(rc == 1 and 'not a move of the map' in out, 'verify fails: a file moved that the map does not move')
        rc, out = tool(d, mp, '--verify', base, '--no-configure-ok')
        check(rc == 0, 'verify passes again once each mutation is undone')
        # ── B1.2a: a variable is used only where CMake's scopes put it (B1.18's F1) ──────────────────────────────
        d2 = os.path.join(tmp, 'scopes')
        os.makedirs(d2)
        sh(d2, 'init', '-q')
        G = {
            'CMakeLists.txt': 'cmake_minimum_required(VERSION 3.16)\nproject(app NONE)\n'
                              'add_custom_target(app SOURCES src/Diagnostics/engine/layout.cpp)\n'
                              'set(X ${CMAKE_SOURCE_DIR}/src/Diagnostics)\nadd_custom_target(app2 SOURCES ${X}/engine/layout.cpp)\n'
                              'function(app_layout t)\n    add_custom_target(${t} SOURCES ${LAYOUT}/engine/layout.cpp)\n'
                              'endfunction()\nset(LAYOUT ${CMAKE_SOURCE_DIR}/src/Diagnostics)\napp_layout(app3)\n',
            'tests/CMakeLists.txt': 'cmake_minimum_required(VERSION 3.16)\nproject(umbrella NONE)\n'
                                    'set(PP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)\nforeach(suite Analysis Gui)\n'
                                    '    add_subdirectory(${PP_SRC}/${suite}/tests ${CMAKE_BINARY_DIR}/${suite})\nendforeach()\n',
            'src/Gui/tests/CMakeLists.txt': 'include(${CMAKE_CURRENT_LIST_DIR}/paths.cmake)\n'
                                            'add_custom_target(gui_t SOURCES ${DIAG}/engine/layout.cpp)\nadd_subdirectory(sub)\n',
            'src/Gui/tests/paths.cmake': 'set(GUI ${CMAKE_CURRENT_SOURCE_DIR}/..)\nset(SRC ${GUI}/..)\nset(DIAG ${SRC}/Diagnostics)\n',
            'src/Gui/tests/sub/CMakeLists.txt': 'add_custom_target(gui_sub SOURCES ${DIAG}/engine/layout.cpp)\n'
                                                'set(GDIAG ${GUI}/diag)\n',
            'src/Analysis/tests/CMakeLists.txt': 'set(ANALYSIS ${CMAKE_CURRENT_SOURCE_DIR}/..)\nset(SRC ${ANALYSIS}/..)\n'
                                                 'set(DIAG ${SRC}/Diagnostics)\nset(GDIAG ${ANALYSIS}/diag)\n'
                                                 'add_custom_target(analysis_t SOURCES ${DIAG}/engine/layout.cpp ${SRC}/Analysis/a.cpp)\n',
            'src/Diagnostics/engine/layout.cpp': '// layout\n',
            'src/Analysis/a.cpp': '// a\n',
        }
        for k, v in G.items():
            put(d2, k, v)
        put(d2, '.gitignore', 'build/\n')
        sh(d2, 'add', '-A'); sh(d2, 'commit', '-q', '-m', 'base')
        base2 = sh(d2, 'rev-parse', 'HEAD').strip()
        mp2 = os.path.join(tmp, 's.map')
        put(tmp, 's.map', 'src/Diagnostics/engine/layout.cpp -> src/Gui/diag/\n')
        rc, out = tool(d2, mp2)
        print('\n'.join('    | ' + l for l in out.splitlines()))
        check(rc == 0 and 'done: 1 files moved' in out, 'scopes: the move runs')
        an = get(d2, 'src/Analysis/tests/CMakeLists.txt')
        print('    | src/Analysis/tests/CMakeLists.txt:5 now: ' + an.splitlines()[4])
        check('${SRC}/Gui/diag/layout.cpp ${SRC}/Analysis/a.cpp' in an and '${GUI}' not in an,
              "B1.18's F1: ${GUI}, set only in a sibling (src/Gui/tests), is never written into src/Analysis/tests; "
              "it gets the file's own ${SRC}")
        check('add_custom_target(gui_t SOURCES ${GUI}/diag/layout.cpp)' in get(d2, 'src/Gui/tests/CMakeLists.txt'),
              'scopes: a variable set in an include()d file is used in the includer, after the include')
        check(get(d2, 'src/Gui/tests/sub/CMakeLists.txt').startswith('add_custom_target(gui_sub SOURCES ${GUI}/diag/layout.cpp)'),
              'scopes: a variable set in the parent before add_subdirectory() is used in the child; one the child '
              'sets on a later line (GDIAG) is not')
        root2 = get(d2, 'CMakeLists.txt')
        check('add_custom_target(app SOURCES src/Gui/diag/layout.cpp)' in root2
              and 'add_custom_target(app2 SOURCES ${CMAKE_SOURCE_DIR}/src/Gui/diag/layout.cpp)' in root2,
              'scopes: the root list is re-pathed; a path no variable there holds falls back to ${CMAKE_SOURCE_DIR}, '
              'the repo root on the only route that reaches it')
        check('add_custom_target(${t} SOURCES ${CMAKE_SOURCE_DIR}/src/Gui/diag/layout.cpp)' in root2,
              'B1.2b: a function body is read where it is CALLED: ${LAYOUT}, set after the definition and before the '
              'call, is read there (the definition site does not know it, so the old reading left the line stale)')
        cfg = shutil.which('cmake') is not None and os.name != 'nt'      # cmake: the Mac and Linux only
        rc, out = tool(d2, mp2, '--verify', base2, '--no-configure-ok')  # every platform (Windows has no cmake run)
        check(rc == 0 and 'VERIFY PASSED' in out and 'NOT CONFIGURED (--no-configure-ok' in out,
              '--verify passes on the move; --no-configure-ok says the configure was not run')
        if cfg:
            rc, out = tool(d2, mp2, '--verify', base2, '--configure', os.path.join(tmp, 'cfg1'))
            print('\n'.join('    | ' + l for l in out.splitlines()))
            check(rc == 0 and 'VERIFY PASSED' in out and 'CONFIGURE PASSED' in out,
                  '--verify passes on the move and --configure configures both trees')

        def mutate2(what, rel, fn, needle, extra=('--no-configure-ok',)):
            before = get(d2, rel)
            put(d2, rel, fn(before))
            rc, out = tool(d2, mp2, '--verify', base2, *extra)
            put(d2, rel, before)
            ok = rc == 1 and needle in out
            check(ok, what)
            if not ok:
                print('\n'.join('    | ' + l for l in out.splitlines()))

        mutate2("verify fails: B1.18's spelling, ${GUI}/... written into src/Analysis/tests (set only in a sibling)",
                'src/Analysis/tests/CMakeLists.txt', lambda s: s.replace('${SRC}/Gui/diag/', '${GUI}/diag/'),
                "${GUI} is not set in this file's scope")
        mutate2('verify fails: a variable used on a line before the line that sets it',
                'src/Gui/tests/sub/CMakeLists.txt', lambda s: s.replace('${GUI}/diag/layout.cpp', '${GDIAG}/layout.cpp'),
                "${GDIAG} is not set in this file's scope at this line")
        if cfg:
            mutate2('--configure fails when the test tree does not configure (the independent proof)',
                    'src/Analysis/tests/CMakeLists.txt', lambda s: s.replace('${SRC}/Gui/diag/', '${GUI}/diag/'),
                    'configure tests: FAILED', ('--configure', os.path.join(tmp, 'cfg2')))
            rc, out = tool(d2, mp2, '--verify', base2, '--configure', os.path.join(tmp, 'cfg1'))
            check(rc == 1 and 'is not empty' in out, '--configure refuses a folder that is not empty')
            mutate2("--configure fails on the old reading's stale function-body line (CMake reads the body at the call)",
                    'CMakeLists.txt', lambda s: s.replace('${CMAKE_SOURCE_DIR}/src/Gui/diag/layout.cpp)\nendfunction',
                                                          '${LAYOUT}/engine/layout.cpp)\nendfunction'),
                    'configure app: FAILED', ('--configure', os.path.join(tmp, 'cfg3')))
            os.rename(os.path.join(d2, 'tests', 'CMakeLists.txt'), os.path.join(tmp, 'tests.txt'))
            rc, out = tool(d2, mp2, '--verify', base2, '--configure', os.path.join(tmp, 'cfg4'), '--no-configure-ok')
            os.rename(os.path.join(tmp, 'tests.txt'), os.path.join(d2, 'tests', 'CMakeLists.txt'))
            check(rc == 1 and 'configure tests: FAILED: no tests/CMakeLists.txt' in out and 'CONFIGURE FAILED' in out,
                  '--configure fails a tree it could not configure (no tests/CMakeLists.txt): never "skipped"')
            seed = os.path.join(tmp, 'seed')                     # a seed whose caches turn the tools OFF
            for t in ('app', 'tests'):
                put(seed, t + '/CMakeCache.txt', 'PINPOINT_BUILD_TOOLS:BOOL=OFF\nPINPOINT_BUILD_LM_REPAIR:BOOL=ON\n')
            rc, out = tool(d2, mp2, '--verify', base2, '--configure', os.path.join(tmp, 'cfg5'), '--seed', seed,
                           '--cmake-arg', '-DPINPOINT_BUILD_TOOLS=ON')
            caches = [get(tmp, 'cfg5/%s/CMakeCache.txt' % t) if os.path.isfile(os.path.join(tmp, 'cfg5', t, 'CMakeCache.txt'))
                      else '' for t in ('app', 'tests')]
            check(rc == 0 and all('PINPOINT_BUILD_TOOLS:UNINITIALIZED=ON' in c and 'PINPOINT_BUILD_LM_REPAIR:UNINITIALIZED=ON' in c
                                  for c in caches),
                  '--cmake-arg passes a -D token through and wins over the seed; the seed carries PINPOINT_BUILD_LM_REPAIR')
        else:
            print('  skip  --configure checks: Windows, or no cmake on the PATH')
        constructs_selftest(tmp, sh, tool, put, get, check)
        hand_selftest(tmp, sh, tool, put, get, check)
    finally:
        shutil.rmtree(tmp, onerror=onerror)
    print('move_files_selftest: %s (%d failures)' % ('FAILED' if fails else 'PASSED', len(fails)))
    return 1 if fails else 0


def main(argv):
    try:                                       # -I ignores PYTHONUTF8: never fail on a source line's bytes (Windows)
        sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    except (AttributeError, ValueError):
        pass
    if '--self-test' in argv:
        return self_test()
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    args, opts = [], {'allow': set()}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--root':
            root = os.path.abspath(argv[i + 1]); i += 2; continue
        if a == '--verify':
            opts['verify'] = argv[i + 1]; i += 2; continue
        if a in ('--configure', '--seed'):
            opts[a] = os.path.abspath(argv[i + 1]); i += 2; continue
        if a == '--cmake-arg':
            opts.setdefault('cmake_args', []).append(argv[i + 1]); i += 2; continue
        if a == '--allow-prose':
            opts['allow'].add(argv[i + 1]); i += 2; continue
        if a == '--keep':
            print('--keep is gone: a kept line goes in the map, as `keep: <file>:<lines>` (docs only), so a verify '
                  'reads the same keeps as the move'); return 2
        if a == '--allow-code-path':                     # <file>:<line>[-<line>][,...]: a named file's named lines only
            f, lines = parse_lines_spec(argv[i + 1])
            opts.setdefault('code', {}).setdefault(f, set()).update(lines)
            i += 2; continue
        if a in ('--dry-run', '--over-work', '--quiet', '--no-configure-ok'):
            opts[a] = True; i += 1; continue
        if a.startswith('-'):
            print(__doc__); return 2
        args.append(a); i += 1
    if len(args) != 1:
        print(__doc__); return 2
    mp = os.path.abspath(args[0]) if os.path.exists(args[0]) else os.path.join(root, args[0])
    try:
        if 'verify' in opts:
            rc = verify(root, mp, opts['verify'], opts['allow'], opts.get('--quiet'), opts.get('code'))
            if '--configure' in opts:
                rc = configure(root, opts['--configure'], opts.get('--seed'), opts.get('cmake_args', ())) or rc
            elif opts.get('--no-configure-ok'):
                print('NOT CONFIGURED (--no-configure-ok: for the self-test only)')
            else:                                          # B1.2b: a skipped configure is never a silent pass
                print('NOT CONFIGURED: this is not a pass (add --configure <scratch dir>; see MOVES.md)')
                rc = rc or 3
            return rc
        if '--configure' in opts or opts.get('--no-configure-ok'):
            print('--configure and --no-configure-ok go with --verify'); return 2
        return run_move(root, mp, opts.get('--dry-run', False), opts.get('--over-work', False), opts.get('--quiet'))
    except Fail as e:
        print('move_files.py: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
