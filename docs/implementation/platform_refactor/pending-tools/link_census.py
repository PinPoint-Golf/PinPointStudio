#!/usr/bin/env python3
"""link_census.py — planning helper for Stage B Part 2 (read-only). For each product source the tests tree (and,
with --app, the app tree) has an object for, lists the symbols it needs that a source in an OUTER ring defines: the
link-level form of lint A1, which reads includes only. A ring library "links only inward" when its rows here are 0.
  python3 docs/implementation/platform_refactor/pending-tools/link_census.py --root <checkout> --objs <build tree> [--objs <build tree>] [-v]
Rings are the lint's tables in <checkout> (see ring_census.py). macOS nm. Weak (inline, template) definitions are
not counted as a definer, since every includer has its own copy."""
import os, re, sys, subprocess, collections, runpy
a = sys.argv[1:]; root='.'; trees=[]; verbose='-v' in a
for i,x in enumerate(a):
    if x=='--root': root=a[i+1]
    if x=='--objs': trees.append(a[i+1])
sys.argv=['x','--root',root,'none']
g = runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)),'ring_census.py')); ring=g['ring']; ORDER=g['ORDER']
objs={}
for t in trees:
    for dp,dn,fn in os.walk(t):
        for f in fn:
            if not f.endswith(('.cpp.o','.mm.o')): continue
            p=os.path.join(dp,f); m=re.search(r'\.dir/(?:__/)*(?:src/)?(.+)\.o$',p)
            if not m: continue
            rel=m.group(1)
            if '/tests/' in rel or rel.startswith('tests/') or not os.path.exists(os.path.join(root,'src',rel)): continue
            objs.setdefault(rel,p)
defs={}; und={}
for rel,p in objs.items():
    out=subprocess.run(['nm','-gm',p],capture_output=True,text=True).stdout
    u=set()
    for l in out.splitlines():
        sym=l.rsplit(' ',1)[-1]
        if '(undefined)' in l: u.add(sym)
        elif ' weak ' not in l and ' external ' in l: defs.setdefault(sym,rel)
    und[rel]=u
rows=collections.defaultdict(list)
for rel,u in und.items():
    r=ring(rel)
    for s in u:
        d=defs.get(s)
        if d and ORDER.index(ring(d))>ORDER.index(r): rows[(rel,d)].append(s)
print('objects read:',len(objs),'| by ring:',dict(collections.Counter(ring(r) for r in objs)))
print('inner-ring source -> outer-ring definer (symbols):')
agg=collections.Counter()
for (rel,d),ss in sorted(rows.items()):
    agg[(ring(rel),ring(d))]+=1
    dem=subprocess.run(['c++filt']+ss[:3],capture_output=True,text=True).stdout.split('\n')[:3]
    print(f'  {rel} [{ring(rel)}] -> {d} [{ring(d)}] x{len(ss)}'+(': '+'; '.join(x[:90] for x in dem if x) if verbose else ''))
print('pairs by ring:',dict(agg))
