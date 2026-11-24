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
Options: --root <checkout> (default: this file's checkout); --allow-prose <file> (with --verify: prose edits in that
doc, e.g. the IM rows a brief names, are listed instead of failing); --allow-code-path <file>:<lines> (with --verify:
on exactly those lines of that file, a path in a C++ string changed to the map's image is listed instead of failing,
for a hand edit a brief names, e.g. B1.6's gate_catalogue_rules.h:38-42); --quiet (totals only).

What it rewrites (MOVES.md items 1-9): the moves (git mv); every `#include "…"` / `#import "…"` of a moved file in src,
tests and tools, by how it resolves (the lint's A1 resolver: relative to the file, then under src/, then under src/Gui/,
then a unique basename): relative -> the path from the file's new home; under src/ or src/Gui/ -> the same style;
through an include directory (a bare name, or any spelling that resolved only by basename) -> the relative path; the
outward includes inside moved files; path mentions in build files (with their set() variables evaluated), the lint's
tables, the allowlist, tools/refactor/context_check.py's tables, tools/, packaging/, .github/, docs/developer/,
docs/design/, BUILDING.md, README.md and C++ comments. A mention is rewritten only when it is anchored: `src/<old>…`,
`${VAR}/…`, `../…`, or `<old dir>/<a real moved file or folder>`; a bare old folder name (`Metrics/`) is rewritten
only in the tables (lint, allowlist key, context_check.py), and reported anywhere else.

What it never does: add an include directory or an allowlist line; delete a table entry or change a ring or a context
(those are hand edits the brief lists; the report shows each moved file's ring and context before and after); guess an
ambiguous basename (an error: the run refuses); touch a C++ string literal outside the lint (reported: a path in code
is behaviour); touch docs/implementation/ (dated snapshots), QML, or anything under build/ or third_party/. An include
directory that names a moved folder is re-pathed (never added) and reported, so the brief can remove it once nothing
needs it.

Exit codes: 0 done / report clean; 1 errors (nothing changed) or --verify found other lines; 2 usage; 77 self-test
skipped (no git).
"""
import ast
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
SCAN_DIRS = ('src/', 'tests/', 'tools/', 'cmake/', 'packaging/', '.github/', 'docs/developer/', 'docs/design/')
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
CMAKE_BUILTIN = ('CMAKE_SOURCE_DIR', 'PROJECT_SOURCE_DIR', 'PP_REPO_ROOT', 'CMAKE_CURRENT_SOURCE_DIR',
                 'CMAKE_CURRENT_LIST_DIR')


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
        if not line:
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


class Plan:
    """The map expanded against a tree. moves: old file -> new file (every move the map makes, done or pending)."""

    def __init__(self, entries, files):
        self.entries = entries
        self.files = set(files)
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
    if path.startswith('docs/') or path.endswith('.md'):
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
    if path.startswith('docs/'):
        return b.endswith(('.md', '.txt'))
    return b.endswith(TEXT_EXT) or b == 'CMakeLists.txt'


# ── CMake variables and include-directory contexts ──────────────────────────────────────────────
SET_RE = re.compile(r'(?mi)^[ \t]*(?:set\s*\(\s*(\w+)\s+("?)([^\s")]+)\2(?:\s+CACHE\b[^)]*)?'
                    r'|get_filename_component\s*\(\s*(\w+)\s+("?)([^\s")]+)\5\s+(?:ABSOLUTE|REALPATH))\s*\)')


def cmake_base(path):
    """CMAKE_CURRENT_SOURCE_DIR for a build file (a .cmake under a cmake/ folder is included from its parent)."""
    d = posixpath.dirname(path)
    if posixpath.basename(path) != 'CMakeLists.txt' and posixpath.basename(d) == 'cmake':
        return posixpath.dirname(d)
    return d


def eval_cmake_value(val, vars_, path):
    def sub(m):
        name = m.group(1)
        if name in ('CMAKE_SOURCE_DIR', 'PROJECT_SOURCE_DIR', 'PP_REPO_ROOT'):
            return '@ROOT@'
        if name == 'CMAKE_CURRENT_SOURCE_DIR':
            return '@ROOT@/' + cmake_base(path)
        if name == 'CMAKE_CURRENT_LIST_DIR':
            return '@ROOT@/' + posixpath.dirname(path)
        if name in vars_ and vars_[name] is not None:
            return '@ROOT@/' + vars_[name]
        return '\0'
    s = re.sub(r'\$\{(\w+)\}', sub, val)
    if '\0' in s or not s.startswith('@ROOT@'):
        return None
    return norm(s[len('@ROOT@'):].lstrip('/'))


class CMakeVars:
    def __init__(self, texts):
        """texts: {path: text} of every build file. Global = names set to one value everywhere."""
        self.per_file, glob, clash = {}, {}, set()
        for _ in range(2):                                         # twice: a global may be set from another
            for path in sorted(texts):
                local = dict((k, v) for k, v in glob.items() if k not in clash)
                local.update(self.per_file.get(path, {}))
                mine = {}
                for m in SET_RE.finditer(texts[path]):
                    name, val = (m.group(1), m.group(3)) if m.group(1) else (m.group(4), m.group(6))
                    mine[name] = eval_cmake_value(val, dict(local, **mine), path)
                self.per_file[path] = mine
            glob, clash = {}, set()
            for path, mine in self.per_file.items():
                for k, v in mine.items():
                    if k in glob and glob[k] != v:
                        clash.add(k)
                    glob[k] = v
        self.glob = {k: v for k, v in glob.items() if k not in clash and v is not None}

    def scope(self, path):
        s = dict(self.glob)
        d = posixpath.dirname(path)
        sib = posixpath.join(d, 'CMakeLists.txt') if posixpath.basename(path) != 'CMakeLists.txt' else None
        if sib and sib in self.per_file:
            s.update({k: v for k, v in self.per_file[sib].items()})
        s.update(self.per_file.get(path, {}))
        s['CMAKE_SOURCE_DIR'] = s['PROJECT_SOURCE_DIR'] = s['PP_REPO_ROOT'] = ''
        s['CMAKE_CURRENT_SOURCE_DIR'] = cmake_base(path)
        s['CMAKE_CURRENT_LIST_DIR'] = posixpath.dirname(path)
        return {k: v for k, v in s.items() if v is not None}


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
                best = None
                for k, v in scope.items():
                    fv = self.plan.fwd(v)
                    if k in CMAKE_BUILTIN[:3] and k != 'PP_REPO_ROOT':
                        continue
                    if (Pn == fv or Pn.startswith(fv + '/')) and fv and (best is None or len(fv) > len(best[1])):
                        best = (k, fv)
                if best is None:
                    return None
                rest = relpath(Pn, best[1]) if Pn != best[1] else ''
                new = '${%s}' % best[0] + ('/' + rest if rest else '')
        if core.endswith('/') and not new.endswith('/'):
            new += '/'
        return new + trail

    def rewrite_span(self, seg, path, ln, mode, scope, kind, incdir_mask=None, offset=0):
        out, last = [], 0
        for m in TOKEN_RE.finditer(seg):
            tok = m.group(0)
            r = self.interpret(tok, path, mode, scope)
            if r is None:
                self.residue(tok, path, ln, kind)
                continue
            P, style = r
            new = self.respell(tok, P, style, path, scope)
            if new is None:
                self.rep.unplaced.append((path, ln, tok, 'no spelling for %s from here' % self.plan.fwd(P)))
                continue
            if new == tok:
                continue
            check = self.interpret(new, self.final_path(path), mode, scope) if style[0] != 'var' else None
            out.append(seg[last:m.start()]); out.append(new); last = m.end()
            isdir = incdir_mask is not None and any(a <= offset + m.start() < b for a, b in incdir_mask)
            if isdir:
                self.rep.incdirs.append((path, ln, tok, new))
            self.rep.mentions[kind].append((path, ln, tok, new))
        out.append(seg[last:])
        return ''.join(out)

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
        kind = file_kind(path)
        lines = text.splitlines(keepends=True)
        scope = self.cvars.scope(path) if kind == 'build' else {}
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
                                            incdir if c != 'm' else None, start + i)
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
        entries = parse_map(f.read())
    files = list_files(root)
    return Plan(entries, files), files


def compute(root, plan, files):
    texts = {}
    for p in sorted(files):
        if in_scope(p):
            t = read_text(os.path.join(root, p))
            if t is not None:
                texts[p] = t
    rw = Rewriter(root, plan, texts)
    rw.rep.errors.extend(plan.errors)
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
    say('== tables: rings and contexts that change: %d' % len(r.tables))
    for x in r.tables:
        say('  ' + x)
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
    with the rewriter's own pass over the result, so nothing the map still asks for passes on an unchanged line."""
    allow_code = allow_code or {}
    with open(map_path, encoding='utf-8') as f:
        entries = parse_map(f.read())
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

    build_old = {p: base_text(p) for p in base_files if file_kind(p) == 'build' and in_scope(p)}
    build_new = {p: work_text(p) for p in work if file_kind(p) == 'build' and in_scope(p)}
    vars_old = CMakeVars({k: v for k, v in build_old.items() if v is not None})
    vars_new = CMakeVars({k: v for k, v in build_new.items() if v is not None})
    ip_old = Interp(plan, old_tree, vars_old)
    ip_new = Interp(plan, new_tree, vars_new)

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
        sm = difflib.SequenceMatcher(None, ol, nl, autojunk=False)
        for op, i1, i2, j1, j2 in sm.get_opcodes():
            if op == 'equal':
                continue
            pairs = list(zip(range(i1, i2), range(j1, j2))) if (i2 - i1) == (j2 - j1) else []
            for i, j in pairs:
                why = check_pair(ol[i], nl[j], b, w, kind, ip_old, ip_new, plan, j + 1 in code_ok)
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
                for i in range(i1, i2):
                    if i + 1 in tbl:
                        listed.append('%s:%d (base): a %s table line deleted: %s' % (b, i + 1, kind, ol[i].strip()))
                    elif kind == 'build' and only_dirs(ol[i], b, ip_old):
                        listed.append('%s:%d (base): an include directory line deleted: %s' % (b, i + 1, ol[i].strip()))
                    else:
                        bad.append('%s:%d (base): line deleted: %s' % (b, i + 1, ol[i].rstrip('\r\n')))
                for j in range(j1, j2):
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
            if h1 in (None, 'ambiguous') or 'src/' + t1 != want:
                bad.append('%s:%d: "%s" resolves to %s, not %s' % (w, n1, c, t1, want))
            elif h1 == 'base' and plan.moved('src/' + t0):
                bad.append('%s:%d: "%s" reaches a moved file only through an include directory' % (w, n1, c))
            elif h0 == 'rel' and h1 != 'rel':
                bad.append('%s:%d: "%s" no longer resolves from the file\'s own folder (%s)' % (w, n1, c, h1))
    # the rewriter's own pass over the result: a file still to move, or an include or mention still to rewrite, fails
    # (a stale spelling left on a line the diff never touched); a pending re-key on a table line is a listed hand edit
    plan_now = Plan(entries, work)
    rw, _ = compute(root, plan_now, work)
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


class Interp:
    """A path token's meaning in one tree (verify's own reading; it shares only the token grammar)."""

    def __init__(self, plan, tree, cvars):
        self.plan, self.tree, self.cvars = plan, tree, cvars

    def meaning(self, tok, path, kind):
        core, trail = split_trailing(tok)
        scope = self.cvars.scope(path) if kind == 'build' else {}
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


def check_pair(old, new, b, w, kind, ip_old, ip_new, plan, code_ok=False):
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
        ma, mc = ip_old.meaning(a, b, kind), ip_new.meaning(c, w, kind)
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
        print('\n'.join('    | ' + l for l in out.splitlines()))
        check(rc == 0 and 'VERIFY PASSED' in out, '--verify passes on the tool\'s own move')

        def mutate(what, rel, fn, expect_rc=1, needle=None, extra=()):
            before = get(d, rel)
            put(d, rel, fn(before))
            rc, out = tool(d, mp, '--verify', base, *extra)
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
        rc, out = tool(d, mp, '--verify', base)
        with open(png, 'wb') as f:
            f.write(pb)
        check(rc == 1 and 'a binary file changed' in out, 'verify fails: a binary file changed')
        if os.name != 'nt':
            xp = os.path.join(d, 'tools/x.py')
            os.chmod(xp, 0o755)
            rc, out = tool(d, mp, '--verify', base)
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
        rc, out = tool(d, mp, '--verify', base)
        sh(d, 'mv', 'src/Vocabulary/log.h', 'src/Core/log.h')
        check(rc == 1 and 'not a move of the map' in out, 'verify fails: a file moved that the map does not move')
        rc, out = tool(d, mp, '--verify', base)
        check(rc == 0, 'verify passes again once each mutation is undone')
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
        if a == '--allow-prose':
            opts['allow'].add(argv[i + 1]); i += 2; continue
        if a == '--allow-code-path':                     # <file>:<line>[-<line>][,...]: a named file's named lines only
            f, _, spec = argv[i + 1].rpartition(':')
            lines = set()
            for part in spec.split(','):
                lo, _, hi = part.partition('-')
                lines.update(range(int(lo), int(hi or lo) + 1))
            opts.setdefault('code', {}).setdefault(f, set()).update(lines); i += 2; continue
        if a in ('--dry-run', '--over-work', '--quiet'):
            opts[a] = True; i += 1; continue
        if a.startswith('-'):
            print(__doc__); return 2
        args.append(a); i += 1
    if len(args) != 1:
        print(__doc__); return 2
    mp = os.path.abspath(args[0]) if os.path.exists(args[0]) else os.path.join(root, args[0])
    try:
        if 'verify' in opts:
            return verify(root, mp, opts['verify'], opts['allow'], opts.get('--quiet'), opts.get('code'))
        return run_move(root, mp, opts.get('--dry-run', False), opts.get('--over-work', False), opts.get('--quiet'))
    except Fail as e:
        print('move_files.py: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
