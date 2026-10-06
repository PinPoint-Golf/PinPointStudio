#!/usr/bin/env python3
"""Grade a sparser pose schedule against the full one, downstream (pose plan §1, step 4).

  schedule_grade.py <base root> <arm root> [--md out.md]

Both roots hold <id>/result.json from swinglab_run. Per swing the arm is judged on what the
consumers produced, never on the pose itself:
  metrics     keys lost / gained; for each metric present in both, the phase-sample values'
              |Δ| in units of the metric's own σ where it has one (else absolute, reported apart);
              a swing "within σ" when ≥ 95 % of its phase samples move by less than 1 σ
  phases      the ladder instants |Δ| per phase (ms)
  skeleton3d  valid → invalid; reprojection medians (px) and total cost; the grip offset norm;
              frames the fit used
  kinematics  the kinematic sequence peak order and peak times |Δ| (ms)
The DTL track and the face-on shaft are graded by their own tools (dtl_continuous_grade.py --truth,
score_truth.py); this one covers the rest.
"""
import json, math, os, sys, statistics as st, glob

def load(root):
    out = {}
    for p in glob.glob(os.path.join(root, '*', 'result.json')):
        d = json.load(open(p)); out[os.path.basename(os.path.dirname(p))] = d.get('analysis', d)
    return out

def phase_vals(m):
    return {(s.get('phase'), i): s.get('value') for i, s in enumerate(m.get('phaseSamples') or [])}

def main():
    base, arm = load(sys.argv[1]), load(sys.argv[2])
    md = sys.argv[sys.argv.index('--md') + 1] if '--md' in sys.argv else None
    rows = []; lost_all = {}; gained_all = {}
    for i in sorted(base):
        if i not in arm: continue
        A, B = base[i], arm[i]
        ma = {m['key']: m for m in A.get('metrics', [])}; mb = {m['key']: m for m in B.get('metrics', [])}
        lost = sorted(set(ma) - set(mb)); gained = sorted(set(mb) - set(ma))
        for k in lost: lost_all[k] = lost_all.get(k, 0) + 1
        for k in gained: gained_all[k] = gained_all.get(k, 0) + 1
        n = over = 0; absd = []
        for k in set(ma) & set(mb):
            pa, pb = phase_vals(ma[k]), phase_vals(mb[k]); sg = ma[k].get('sigma')
            for key in set(pa) & set(pb):
                va, vb = pa[key], pb[key]
                if va is None or vb is None: continue
                d = abs(vb - va)
                if sg and sg > 0: n += 1; over += d > sg
                else: absd.append(d)
        pha = {p['phase']: p['t_us'] for p in A.get('phases', [])}; phb = {p['phase']: p['t_us'] for p in B.get('phases', [])}
        pd = {p: abs(phb[p] - pha[p]) / 1000 for p in pha if p in phb}
        sa, sb = A.get('skeleton3d') or {}, B.get('skeleton3d') or {}
        da, db = sa.get('diagnostics') or {}, sb.get('diagnostics') or {}
        norm = lambda v: math.sqrt(sum(x * x for x in v)) if v else float('nan')
        ka, kb = A.get('kinematicSequence') or {}, B.get('kinematicSequence') or {}
        rows.append(dict(id=i[-27:], lost=len(lost), gained=len(gained), n=n, overSigma=over,
                         frac=(1 - over / n) if n else float('nan'), absMed=st.median(absd) if absd else float('nan'),
                         phaseMax=max(pd.values()) if pd else float('nan'),
                         skValid=f"{sa.get('valid')}→{sb.get('valid')}", skDtl=f"{sa.get('dtl')}→{sb.get('dtl')}",
                         reprojFo=(da.get('reprojMedPxFo'), db.get('reprojMedPxFo')), reprojDtl=(da.get('reprojMedPxDtl'), db.get('reprojMedPxDtl')),
                         cost=(da.get('costFinal'), db.get('costFinal')), frames=(len(sa.get('frames') or []), len(sb.get('frames') or [])),
                         grip=(norm((sa.get('grip') or {}).get('offset')), norm((sb.get('grip') or {}).get('offset'))),
                         ksOrder=(ka.get('peakOrder') == kb.get('peakOrder')) if ka and kb else None))
    lines = [f"| swing | metrics lost/gained | phase samples within σ | abs Δ median (no σ) | phase shift max ms | skeleton valid | DTL in fit | reproj FO px | reproj DTL px | cost | frames | grip m |", '|---' * 12 + '|']
    for r in rows:
        f = lambda t, p=2: f"{t[0]:.{p}f}→{t[1]:.{p}f}" if t[0] is not None and t[1] is not None else '-'
        lines.append(f"| {r['id']} | {r['lost']}/{r['gained']} | {r['frac']*100:.0f}% of {r['n']} | {r['absMed']:.3f} | {r['phaseMax']:.0f} | {r['skValid']} | {r['skDtl']} | {f(r['reprojFo'])} | {f(r['reprojDtl'])} | {f(r['cost'],0)} | {r['frames'][0]}→{r['frames'][1]} | {f(r['grip'],3)} |")
    fr = [r['frac'] for r in rows if r['frac'] == r['frac']]
    summ = [f"swings {len(rows)}; metrics lost on any swing: {lost_all or 'none'}; gained: {gained_all or 'none'}",
            f"phase samples within σ: median {st.median(fr)*100:.0f}% min {min(fr)*100:.0f}% (gate ≥ 95 %); swings under the gate: {sum(1 for x in fr if x < 0.95)}",
            f"phase shift max over swings: {max(r['phaseMax'] for r in rows):.0f} ms; skeleton valid changes: {sum(1 for r in rows if r['skValid'] not in ('True→True','False→False'))}; DTL dropped from the fit: {sum(1 for r in rows if r['skDtl'] == 'True→False')}, regained: {sum(1 for r in rows if r['skDtl'] == 'False→True')}",
            f"reproj FO Δ median {st.median([r['reprojFo'][1]-r['reprojFo'][0] for r in rows if None not in r['reprojFo']]):+.2f} px; DTL {st.median([r['reprojDtl'][1]-r['reprojDtl'][0] for r in rows if None not in r['reprojDtl']]):+.2f} px; grip Δ median {st.median([r['grip'][1]-r['grip'][0] for r in rows if r['grip'][0]==r['grip'][0] and r['grip'][1]==r['grip'][1]]):+.3f} m"]
    out = '\n'.join(lines + [''] + summ)
    print(out)
    if md: open(md, 'w').write(out + '\n')

if __name__ == '__main__':
    main()
