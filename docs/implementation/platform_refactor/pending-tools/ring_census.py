#!/usr/bin/env python3
"""ring_census.py — planning helper for Stage B Part 2 (read-only; nothing is built or changed).
  python3 docs/implementation/platform_refactor/pending-tools/ring_census.py [--root <checkout>] [--manifest <store_isolation_manifest.txt>] [section ...]
Sections: rings deps traps manifest defines globals (default: all).
A file's ring is read from the lint's own tables (dirRings(), fileRings() in src/Gui/tests/architecture_lint_test.cpp),
longest prefix, so the figures are the lint's as of the checkout given. Test folders (*/tests/) are left out of rings."""
import os, re, sys, collections
args = sys.argv[1:]; root = '.'; man = None
if '--root' in args: i = args.index('--root'); root = args[i+1]; del args[i:i+2]
if '--manifest' in args: i = args.index('--manifest'); man = args[i+1]; del args[i:i+2]
want = set(args) or {'rings','deps','traps','manifest','defines','globals'}
SRC = os.path.join(root, 'src')
lint = open(os.path.join(SRC, 'Gui/tests/architecture_lint_test.cpp'), encoding='utf-8').read()
a = lint.index('&dirRings()'); b = lint.index('&fileRings()'); c = lint.index('QString stemOf')
pair = re.compile(r'QStringLiteral\("([^"]+)"\),\s*(\w+)\s*\}')
DIRS = sorted(pair.findall(lint[a:b]), key=lambda p: -len(p[0])); FILES = dict(pair.findall(lint[b:c]))
ORDER = ['Platform','Vocabulary','Rules','DomainModel','Application','Adapters']
def ring(rel):
    stem = os.path.splitext(rel)[0]
    if stem in FILES: return FILES[stem]
    for d, r in DIRS:
        if rel.startswith(d): return r
    return 'none'
files = []
for dp, dn, fn in os.walk(SRC):
    if '/tests' in dp or dp.endswith('/Resources') or '/Resources/' in dp: continue
    for f in fn:
        if f.endswith(('.h','.hpp','.cpp','.mm')): files.append(os.path.relpath(os.path.join(dp,f), SRC))
txt = {}
for f in files:
    try: txt[f] = open(os.path.join(SRC,f), encoding='utf-8', errors='replace').read()
    except OSError: txt[f] = ''
R = {f: ring(f) for f in files}
def table(title, rows, pats):
    print('\n## ' + title); print('ring | ' + ' | '.join(n for n,_ in pats))
    for r in ORDER + ['none']:
        fs = [f for f in files if R[f]==r and rows(f)]
        print(r, '|', ' | '.join(str(sum(1 for f in fs if re.search(p, txt[f], re.M))) for _,p in pats))
if 'rings' in want:
    print('## Files by ring (lint tables; tests left out)'); print('ring | .cpp/.mm | headers | lines | directories')
    for r in ORDER + ['none']:
        fs = [f for f in files if R[f]==r]
        cpp = [f for f in fs if not f.endswith(('.h','.hpp'))]
        dirs = collections.Counter(f.split('/')[0] for f in cpp)
        print(r, '|', len(cpp), '|', len(fs)-len(cpp), '|', sum(txt[f].count('\n') for f in fs), '|', ' '.join(f'{d}:{n}' for d,n in dirs.most_common()))
if 'deps' in want:
    table('Third-party and Qt-module includes, files per ring (any file)', lambda f: True, [
        ('opencv', r'#include\s*<opencv2/'), ('onnxruntime', r'#include\s*[<"]onnxruntime'), ('Eigen', r'#include\s*<Eigen/'),
        ('ffmpeg', r'#include\s*<lib(av|sw)'), ('whisper/ggml', r'#include\s*[<"](whisper|ggml)'), ('ppswing/ppsw', r'#include\s*[<"]ppsw'),
        ('QtQml/Quick', r'#include\s*<(QQml|QQuick|QJSValue|QtQml|QtQuick|qqmlregistration)'), ('Multimedia', r'#include\s*<(QMedia|QAudio|QVideo|QCamera|QtMultimedia)'),
        ('Bluetooth', r'#include\s*<(QBluetooth|QLowEnergy|QtBluetooth)'), ('Network', r'#include\s*<(QTcp|QUdp|QNetwork|QSsl|QHostAddress|QtNetwork|QLocal)'),
        ('QtGui types', r'#include\s*<(QImage|QColor|QPainter|QFont|QPixmap|QGuiApplication|QVector3D|QMatrix4x4|QQuaternion|QPointF|QRectF|QPolygonF)')])
if 'traps' in want:
    table('MSVC static-library traps, files per ring', lambda f: True, [
        ('Q_OBJECT', r'^\s*Q_OBJECT\b'), ('Q_GADGET/NAMESPACE', r'^\s*Q_(GADGET|NAMESPACE)\b'), ('QML_ELEMENT etc', r'^\s*QML_(ELEMENT|SINGLETON|NAMED_ELEMENT|UNCREATABLE|ANONYMOUS)'),
        ('.moc include', r'#include\s*"[^"]+\.moc"'), ('Q_GLOBAL_STATIC', r'Q_GLOBAL_STATIC'), ('startup/ctor fn', r'Q_COREAPP_STARTUP_FUNCTION|Q_CONSTRUCTOR_FUNCTION|__attribute__\(\(constructor'),
        ('qRegisterMetaType', r'qRegisterMetaType|Q_DECLARE_METATYPE'), ('Q_INIT_RESOURCE', r'Q_INIT_RESOURCE'), ('":/" resource path', r'":/|"qrc:/'),
        ('ppSettings(', r'\bppSettings\s*\('), ('QSettings', r'\bQSettings\b'), ('file-scope registrar', r'^static\s+(const\s+)?(bool|int|auto)\s+\w*(reg|Reg|init|Init)\w*\s*=\s*\w+\(')])
    print('\n(file-scope registrar hits, listed)')
    for f in files:
        for m in re.finditer(r'^static\s+(const\s+)?(bool|int|auto)\s+\w*(reg|Reg|init|Init)\w*\s*=\s*\w+\(.*$', txt[f], re.M): print(' ', R[f], f, m.group(0)[:90])
if 'manifest' in want and man:
    T = collections.OrderedDict(); cur = None; tests = []
    for l in open(man, encoding='utf-8'):
        k, v = l[0], l[2:].strip()
        if k == 'T': cur = v; T[cur] = []
        elif k == 'S': T[cur].append(v)
        elif k == 'X': tests.append(v)
    def prod(s):
        i = s.find('/src/'); rel = s[i+5:] if i >= 0 else None
        return rel if rel and '/tests/' not in rel and rel.endswith(('.cpp','.mm')) else None
    uses = collections.Counter(); per = {}
    for t, ss in T.items():
        p = [prod(s) for s in ss]; p = [x for x in p if x]; per[t] = p; uses.update(p)
    hl = [t for t in T if per[t]]
    print('\n## The tests umbrella (manifest)')
    print('executables', len(T), '| ctest tests', len(tests), '| executables that hand-list a product source', len(hl), '| product-source compiles', sum(uses.values()), '| distinct product sources', len(uses))
    print('compiles by ring (each product source x the executables that list it); distinct sources by ring:')
    for r in ORDER + ['none']:
        print(' ', r, sum(n for s,n in uses.items() if ring(s)==r), '|', sum(1 for s in uses if ring(s)==r))
    print('compiles by directory:', end=' ')
    bd = collections.Counter()
    for s,n in uses.items(): bd[s.split('/')[0]] += n
    print(' '.join(f'{d}:{n}' for d,n in bd.most_common()))
    print('most-compiled 15:', ', '.join(f'{s} x{n}' for s,n in uses.most_common(15)))
    print('executables by number of product sources listed: 1-5:', sum(1 for t in hl if len(per[t])<=5), '6-20:', sum(1 for t in hl if 5<len(per[t])<=20), '21-50:', sum(1 for t in hl if 20<len(per[t])<=50), '>50:', sum(1 for t in hl if len(per[t])>50), '| largest:', max((len(per[t]),t) for t in hl))
    full = lambda r: sum(1 for f in files if R[f]==r and not f.endswith(('.h','.hpp')))
    print('share of each ring\'s sources the tests tree compiles at all:', ' '.join(f"{r}:{sum(1 for s in uses if ring(s)==r)}/{full(r)}" for r in ORDER))
    byd = collections.Counter()
    for t in hl:
        s0 = [s for s in T[t] if '/tests/' in s]; d = s0[0].split('/src/')[1].split('/')[0] if s0 and '/src/' in s0[0] else '?'; byd[d] += 1
    print('hand-listing executables by suite folder:', dict(byd))
if 'defines' in want:
    print('\n## Per-test DEFINES that a product source reads (one library build cannot serve two values)')
    names = collections.Counter()
    for dp, dn, fn in os.walk(root):
        if '/build' in dp or '/.git' in dp or '/_deps' in dp: continue
        for f in fn:
            if f == 'CMakeLists.txt' and '/tests' in dp + '/':
                t = open(os.path.join(dp,f), encoding='utf-8', errors='replace').read()
                for m in re.finditer(r'DEFINES\s+((?:[A-Z_0-9]+(?:="?[^\s)]*)?\s*)+)', t):
                    for d in m.group(1).split(): names[d.split('=')[0]] += 1
    for n, k in names.most_common():
        hits = [f for f in files if re.search(r'\b' + re.escape(n) + r'\b', txt[f])]
        if hits: print(f'  {n}: set by {k} test target(s); read in {len(hits)} product file(s):', ', '.join(f'{h}[{R[h]}]' for h in hits[:6]), '...' if len(hits) > 6 else '')
if 'globals' in want:
    print('\n## Quoted includes that resolve only through a global include directory, by the includer\'s ring')
    by = {}
    for f in files:
        by.setdefault(os.path.basename(f), []).append(f)
    tot = collections.Counter(); amb = 0; pathd = collections.Counter()
    for f in files:
        d = os.path.dirname(f)
        for m in re.finditer(r'^\s*#\s*(?:include|import)\s*"([^"]+)"', txt[f], re.M):
            inc = m.group(1)
            if inc.endswith('.moc') or inc.startswith('moc_'): continue
            if os.path.exists(os.path.join(SRC, d, inc)): continue                      # beside the includer
            if os.path.exists(os.path.join(SRC, inc)): pathd[R[f]] += 1; continue      # spelled from src/
            c2 = by.get(os.path.basename(inc), [])
            hit = [c for c in c2 if c.endswith(inc)]
            if hit: tot[R[f]] += 1; amb += len(hit) > 1
    print('ring | only through a global directory | spelled from src/')
    for r in ORDER: print(r, '|', tot[r], '|', pathd[r])
    print('total', sum(tot.values()), '| of them with two candidate files:', amb)
