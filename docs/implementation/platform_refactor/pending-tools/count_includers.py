#!/usr/bin/env python3
"""Planning check for Stage B Part 1 (read-only). Counts the files that include a header of a given
set, resolving a quoted include as the architecture lint does: exact path under src/, then relative to
the including file, then unique basename. Run from the repo root:
  python3 docs/implementation/platform_refactor/pending-tools/count_includers.py [--root <checkout>] vocab|vocab-after|vocab-ruling4|vocab-plan|coaching|<Dir/ or Dir/stem>...
"""
import os, re, sys, collections
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
if '--root' in sys.argv:                      # another checkout, e.g. a lane's worktree
    i = sys.argv.index('--root'); ROOT = os.path.abspath(sys.argv[i + 1]); del sys.argv[i:i + 2]
SRC = os.path.join(ROOT, 'src')
EXT = ('.h', '.hpp', '.cpp', '.mm', '.cc', '.c')
CORE_VOCAB = ['club_standards', 'club_vocabulary', 'figure_format', 'height_text', 'pp_physical_constants',
              'pp_tuned_constants', 'sidecar_policy']
SETS = {
  'vocab': ['Metrics/', 'Recording/', 'Identity/'] + ['Core/' + n for n in CORE_VOCAB]
           + ['LaunchMonitor/launch_monitor_reading', 'LaunchMonitor/lm_device', 'Export/frame_decode'],
  'vocab-ruling4': ['Metrics/', 'Recording/', 'Identity/'] + ['Core/' + n for n in CORE_VOCAB],
  'vocab-plan': ['Metrics/', 'Recording/', 'Identity/'] + ['Core/' + n for n in CORE_VOCAB + ['headless_probe', 'same_folder', 'version']],
  'coaching': ['Coaching/'],
  # B1.5's set at its new paths (ADVICE-B1.5 risk 3.1): the after-move figure must equal 'vocab' before it.
  'vocab-after': ['Vocabulary/Metrics/', 'Vocabulary/Recording/', 'Vocabulary/Identity/', 'Vocabulary/LaunchMonitor/',
                  'Vocabulary/Club/', 'Vocabulary/Common/'],
}
def walk(top):
    out = []
    for d, dn, fn in os.walk(top):
        dn[:] = [x for x in dn if x not in ('build', '.git', 'third_party', 'node_modules')]
        for f in fn:
            if f.endswith(EXT): out.append(os.path.join(d, f))
    return out
srcfiles = walk(SRC)
rel = {os.path.relpath(p, SRC).replace(os.sep, '/') for p in srcfiles}
by_name = collections.defaultdict(list)
for r in rel: by_name[os.path.basename(r)].append(r)
def resolve(frm_abs, inc):
    if inc in rel: return inc
    cand = os.path.normpath(os.path.join(os.path.dirname(frm_abs), inc))
    c = os.path.relpath(cand, SRC).replace(os.sep, '/')
    if c in rel: return c
    b = by_name.get(os.path.basename(inc), [])
    return b[0] if len(b) == 1 else None
def inset(target, keys):
    stem = target.rsplit('.', 1)[0]
    return any(target.startswith(k) if k.endswith('/') else stem == k for k in keys)
INC = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
def count(keys):
    files = collections.Counter(); n = 0; styles = collections.Counter()
    for p in srcfiles + walk(os.path.join(ROOT, 'tools')):
        try: txt = open(p, encoding='utf-8', errors='replace').read()
        except OSError: continue
        for inc in INC.findall(txt):
            t = resolve(p, inc)
            if t and inset(t, keys):
                files[p] += 1; n += 1
                styles['dir-qualified' if inc == t else ('bare' if '/' not in inc else 'relative')] += 1
    rp = lambda p: os.path.relpath(p, ROOT).replace(os.sep, '/')
    tests = [p for p in files if '/tests/' in rp(p) and rp(p).startswith('src/')]
    tools = [p for p in files if rp(p).startswith('tools/')]
    prod = [p for p in files if p not in tests and p not in tools]
    inside = [p for p in prod if inset(os.path.relpath(p, SRC).replace(os.sep, '/'), keys)]
    moved = sorted(r for r in rel if inset(r, keys) and '/tests/' not in r)
    print(f'files moved (non-test): {len(moved)}')
    print(f'includers: {len(files)} files, {n} includes; production {len(prod)} ({len(inside)} inside the moved set), '
          f'tests {len(tests)}, tools {len(tools)}; include spelling {dict(styles)}')
    return files
if __name__ == '__main__':
    for a in sys.argv[1:]:
        print('==', a); count(SETS.get(a, [a]))
