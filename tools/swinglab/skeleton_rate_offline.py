#!/usr/bin/env python3
"""Segment angular speeds from the TWO-CAMERA SKELETON, offline — K0 of ks_skeleton3d_route_design.md.

WHAT. The kinematic sequence reads its pelvis and thorax rates off the face-on + DTL separation pair
and its lead arm off the face-on image; the rotation route (body_rotation_estimation.md §7) already
reads skeleton3d's hip and shoulder lines. This measures what a skeleton rung WOULD give, before a
line of C++ is written, against what each swing's document says today:

  1  continuity   the usable fraction of skeleton frames over the sequence domain (Transition, else
                  Top → Impact) for the hip and shoulder lines — both joints tier ≥ Constrained and
                  the line ≥ 8 cm long in plan — beside the stored pair curve's valid fraction
  2  trunk rates  leadSign·dψ/dt off the line bearings, through the SAME ±12.5 ms local quadratic and
                  the SAME placePeak replica span_pair_offline.py uses; against the stored curve:
                  correlation, peak instant and value (Cheetham 477 / 727 °/s), rising at impact,
                  and — report only, §14a — the peak instant if the search ran to impact + 60 ms
  3  thorax       the shoulder line against the spine-chain yaw (root.yaw + the three spine twists)
  4  lead arm     the shoulder→wrist unit vector's angle in its OWN fitted downswing plane, |dα/dt|,
                  node against the document's face-on arm node
  6  club         the fitted shaft direction the same way, against the document's club node (record)
  7  sequence_order  what the session ledger says on each shot (diagnostics.json)

Item 5 (the spline prior) needs a re-fit and is driven separately: pass --alt <dir> holding
<id>/result.json from a swinglab_run with skeleton3d.splineBasis=false and the node times are compared.

NOT persisted: skeleton3d's per-joint σ (sigmaM). The rate σ here is a NOMINAL per-frame angle σ
(--angle-sigma-deg, 0.5°) through the local fit, so σ_t is indicative only; the real σ is first seen
in the app (G3). Nothing here is truth: no corpus swing has a trunk IMU.

    python3 tools/swinglab/skeleton_rate_offline.py --session /mnt/swingdata/Mark-Liversedge/2026-07-04_Mark-Liversedge_Wrist_01 \
        [--swings 8] [--alt /mnt/swingdata/scratch/ks-skel-k0/nospline] \
        --csv docs/research/data/kinematic_sequence/skeleton_rate_k0_20261002.csv
"""
import argparse, csv, json, math, os, sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
from pp_swingdoc import load_swing                                  # noqa: E402
from span_pair_offline import local_fits, place_peak, DERIV_WINDOW_MS  # noqa: E402  the shipped placement, replicated

J = {n: i for i, n in enumerate(['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'HeadTop_End',
                                 'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftHand', 'LeftHandMiddle1',
                                 'RightShoulder', 'RightArm', 'RightForeArm', 'RightHand', 'RightHandMiddle1',
                                 'LeftUpLeg', 'LeftLeg', 'LeftFoot', 'LeftToeBase', 'LeftToe_End',
                                 'RightUpLeg', 'RightLeg', 'RightFoot', 'RightToeBase', 'RightToe_End'])}
TIER_CONSTRAINED = 2
MIN_BASELINE_M = 0.08                    # tuned::bodyRotation::kTriMinBaselineM
PH_ADDRESS, PH_TOP, PH_TRANSITION, PH_IMPACT = 0, 2, 3, 5
POST_IMPACT_US = 60000                   # skeleton3d's fast window ends at impact + 60 ms
CHEETHAM = {'pelvis': 477.0, 'thorax': 727.0, 'leadArm': 980.0, 'club': 2250.0}


def load_skeleton(doc):
    sk = doc['analysis'].get('skeleton3d') or {}
    if not (sk.get('valid') and sk.get('dtl')):
        return None
    names = sk['jointNames']
    if names[:len(J)] != list(J.keys()):
        raise SystemExit('joint layout changed — update J')
    fr = sk['frames']
    t = np.array([f['t'] for f in fr], dtype=float)
    p = np.array([np.array(f['p'], dtype=float).reshape(-1, 3) / 1000.0 for f in fr])   # m
    tier = np.array([[int(c) for c in f['tier']] for f in fr])
    dof = np.array([f['d'] for f in fr], dtype=float)
    u = np.array([f.get('u') or [np.nan] * 3 for f in fr], dtype=float)
    g = np.array([f.get('g') or [np.nan] * 3 for f in fr], dtype=float)
    s = np.array([f.get('s', 0) for f in fr])
    return dict(t=t, p=p, tier=tier, dof=dof, dofNames=sk['dofNames'], u=u, g=g, s=s,
                calibrated=sk.get('camera', {}).get('calibrated', False))


def line_bearing(sk, a, b):
    pa, pb = sk['p'][:, J[a]], sk['p'][:, J[b]]
    ok = (sk['tier'][:, J[a]] >= TIER_CONSTRAINED) & (sk['tier'][:, J[b]] >= TIER_CONSTRAINED)
    dx, dy = pb[:, 0] - pa[:, 0], pb[:, 1] - pa[:, 1]
    ok &= np.hypot(dx, dy) >= MIN_BASELINE_M
    psi = np.where(ok, np.arctan2(dy, dx), np.nan)
    return psi, ok


def unwrap_valid(a):
    out = a.copy(); m = np.isfinite(a)
    out[m] = np.unwrap(a[m])
    return out


def rate_of(t_us, ang_rad, sign, magnitude, angle_sigma_deg):
    """dα/dt in °/s on the frames' own clock: the ±12.5 ms local quadratic (angular_rate.h)."""
    ts = t_us / 1e6
    _, c1, _, ss = local_fits(ts, ang_rad, DERIV_WINDOW_MS / 2e3)
    r = sign * c1 * 180.0 / math.pi
    if magnitude:
        r = np.abs(r)
    sig = np.where(ss > 0, angle_sigma_deg / np.sqrt(np.maximum(ss, 1e-12)), np.nan)
    return r, sig


def plane_angle(vecs, dom):
    """Angle of unit vectors in their own best-fit plane over `dom` (SVD; normal = least singular)."""
    ok = dom & np.all(np.isfinite(vecs), axis=1)
    if ok.sum() < 5:
        return None, None
    V = vecs[ok] / np.linalg.norm(vecs[ok], axis=1, keepdims=True)
    _, _, vt = np.linalg.svd(V - 0 * V.mean(0), full_matrices=False)   # through the origin: directions
    e1, e2, n = vt[0], vt[1], vt[2]
    allv = vecs / np.linalg.norm(vecs, axis=1, keepdims=True)
    a = np.arctan2(allv @ e2, allv @ e1)
    a[~np.all(np.isfinite(vecs), axis=1)] = np.nan
    off = np.degrees(np.arcsin(np.clip(np.abs(V @ n), 0, 1)))
    return unwrap_valid(a), float(np.sqrt(np.mean(off ** 2)))


def peak(t_us, r, sig, valid, lo, hi):
    pk = place_peak(t_us / 1e6, r, sig, valid, lo / 1e6, hi / 1e6)
    if pk is None:
        return None
    pk['t_us'] = pk['t'] * 1e6
    return pk


def doc_series(doc, key):
    for m in doc['analysis'].get('metrics', []):
        if m['key'] == key:
            return m
    return None


def doc_node(doc, seg):
    for n in (doc['analysis'].get('kinematicSequence') or {}).get('nodes', []):
        if n['segment'] == seg:
            return n
    return None


def corr(a, b):
    m = np.isfinite(a) & np.isfinite(b)
    if m.sum() < 5:
        return float('nan'), float('nan')
    pr = float(np.corrcoef(a[m], b[m])[0, 1])
    ra = np.argsort(np.argsort(a[m])); rb = np.argsort(np.argsort(b[m]))
    sr = float(np.corrcoef(ra, rb)[0, 1])
    return pr, sr


def sequence_order_findings(session_dir):
    p = os.path.join(session_dir, 'diagnostics.json')
    if not os.path.exists(p):
        return {}
    D = json.load(open(p, encoding='utf-8'))
    out = {}
    def walk(o, shot=None):
        if isinstance(o, dict):
            sh = o.get('shot', o.get('swing', o.get('shotIndex', shot)))
            if o.get('id') == 'sequence_order' or o.get('characteristic') == 'sequence_order':
                out.setdefault(str(sh), []).append({k: v for k, v in o.items()
                                                    if k in ('state', 'verdict', 'fired', 'pFire', 'assessable',
                                                             'status', 'reason', 'value', 'values')})
            for v in o.values():
                walk(v, sh)
        elif isinstance(o, list):
            for v in o:
                walk(v, shot)
    walk(D)
    return out


def analyse(swing_dir, alt_dir, angle_sigma_deg, lead_is_left):
    doc = load_swing(swing_dir)
    sk = load_skeleton(doc)
    name = os.path.basename(swing_dir)
    row = dict(swing=name)
    if sk is None:
        row['skip'] = 'no two-camera skeleton'
        return row, None
    ph = {p['phase']: p['t_us'] for p in doc['analysis']['phases'] if p.get('segment', 0) == 0}
    imp = ph.get(PH_IMPACT)
    lo = ph.get(PH_TRANSITION, ph.get(PH_TOP))
    if imp is None or lo is None:
        row['skip'] = 'no domain'
        return row, None
    t = sk['t']
    dom = (t >= lo) & (t <= imp)
    dom_post = (t >= lo) & (t <= imp + POST_IMPACT_US)
    lead = 1.0 if lead_is_left else -1.0
    row.update(domain_ms=(imp - lo) / 1e3, n_dom=int(dom.sum()), calibrated=sk['calibrated'])
    series = {}

    # Which side is the lead: the grip point is the LEAD hand's (FitResult::grip), so it sits nearer
    # LeftHand for a right-hander if the rig's Left is anatomical left.
    dl = np.nanmedian(np.linalg.norm(sk['g'] - sk['p'][:, J['LeftHand']], axis=1))
    dr = np.nanmedian(np.linalg.norm(sk['g'] - sk['p'][:, J['RightHand']], axis=1))
    row['grip_to_LeftHand_mm'] = dl * 1e3; row['grip_to_RightHand_mm'] = dr * 1e3

    # ── 1–2  trunk ────────────────────────────────────────────────────────────────────────────
    for seg, a, b, key in (('pelvis', 'LeftUpLeg', 'RightUpLeg', 'pelvisAngularSpeed'),
                           ('thorax', 'LeftArm', 'RightArm', 'thoraxAngularSpeed')):
        psi, ok = line_bearing(sk, a, b)
        row[f'{seg}_usable'] = float(ok[dom].mean()) if dom.any() else float('nan')
        r, sg = rate_of(t, unwrap_valid(psi), lead, False, angle_sigma_deg)
        valid = ok & np.isfinite(r)
        pk = peak(t, r, sg, valid, lo, imp)
        pk2 = peak(t, r, sg, valid, lo, imp + POST_IMPACT_US)
        node = doc_node(doc, seg)
        row[f'{seg}_doc_route'] = node['routeId'] if node else ''
        # The stored curve: its valid fraction over the domain, and agreement on the skeleton's frames.
        m = doc_series(doc, key)
        if m:
            st = np.array(m['t_us'], float); sv = np.array(m['value'], float)
            svalid = np.array(m.get('valid', [1] * len(st)), bool)
            sd = (st >= lo) & (st <= imp)
            row[f'{seg}_doc_valid'] = float(svalid[sd].mean()) if sd.any() else float('nan')
            good = svalid & np.isfinite(sv)
            on = np.interp(t, st[good], sv[good], left=np.nan, right=np.nan) if good.sum() > 1 else np.full_like(t, np.nan)
            # never interpolate across the stored curve's holes: drop skeleton frames nearer a hole than a sample
            if good.sum() > 1:
                gi = np.searchsorted(st, t)
                for k in range(len(t)):
                    j0, j1 = max(gi[k] - 1, 0), min(gi[k], len(st) - 1)
                    if not (good[j0] and good[j1]):
                        on[k] = np.nan
            row[f'{seg}_r_pearson'], row[f'{seg}_r_spearman'] = corr(np.where(dom & valid, r, np.nan), on)
            series[f'{seg}_doc'] = (st, np.where(good, sv, np.nan))
        if pk:
            row[f'{seg}_peak_before_ms'] = (imp - pk['t_us']) / 1e3
            row[f'{seg}_peak_dps'] = pk['peak']
            row[f'{seg}_at_late_edge'] = pk['at_late']
            row[f'{seg}_tsig_ms_nominal'] = pk['t_sigma_ms']
        # rising at impact: the local slope at the last valid domain frame
        dv = np.where(dom & valid)[0]
        if len(dv):
            k = dv[-1]
            row[f'{seg}_rate_at_impact'] = float(r[k])
            row[f'{seg}_rising_at_impact'] = bool(len(dv) > 2 and r[dv[-1]] > r[dv[-3]])
        if pk2:
            row[f'{seg}_peak_post_ms'] = (pk2['t_us'] - imp) / 1e3   # + = after impact (report only, §14a)
            row[f'{seg}_peak_post_dps'] = pk2['peak']
        series[seg] = (t, np.where(valid, r, np.nan))

    # ── 3  thorax by the spine chain ──────────────────────────────────────────────────────────
    dn = sk['dofNames']
    yaw = sk['dof'][:, dn.index('root.yaw')] + sum(sk['dof'][:, dn.index(f'{s}.twist')] for s in ('spine', 'spine1', 'spine2'))
    if np.nanmax(np.abs(yaw)) > 2 * math.pi + 0.5:
        yaw = np.radians(yaw)
    # Sign: whichever makes the chain rate agree with the shoulder line in sign over the domain.
    rc, sgc = rate_of(t, unwrap_valid(yaw), 1.0, False, angle_sigma_deg)
    rs = series['thorax'][1]
    both = dom & np.isfinite(rc) & np.isfinite(rs)
    flip = -1.0 if both.sum() > 3 and np.nansum(rc[both] * rs[both]) < 0 else 1.0
    rc = flip * rc
    row['thoraxChain_r_vs_shoulder'] = corr(np.where(both, rc, np.nan), np.where(both, rs, np.nan))[0]
    pkc = peak(t, rc, sgc, np.isfinite(rc), lo, imp)
    if pkc:
        row['thoraxChain_peak_before_ms'] = (imp - pkc['t_us']) / 1e3
        row['thoraxChain_peak_dps'] = pkc['peak']
    if both.sum():
        row['thorax_shoulder_minus_chain_rms_dps'] = float(np.sqrt(np.mean((rs[both] - rc[both]) ** 2)))
    series['thoraxChain'] = (t, rc)

    # ── 4  lead arm ────────────────────────────────────────────────────────────────────────────
    sh, wr = ('LeftArm', 'LeftHand') if lead_is_left else ('RightArm', 'RightHand')
    ok = (sk['tier'][:, J[sh]] >= TIER_CONSTRAINED) & (sk['tier'][:, J[wr]] >= TIER_CONSTRAINED)
    vec = sk['p'][:, J[wr]] - sk['p'][:, J[sh]]
    vec[~ok] = np.nan
    row['leadArm_usable'] = float(ok[dom].mean())
    alpha, off = plane_angle(vec, dom)
    if alpha is not None:
        row['leadArm_offplane_rms_deg'] = off
        r, sg = rate_of(t, alpha, 1.0, True, angle_sigma_deg)
        valid = ok & np.isfinite(r)
        pk = peak(t, r, sg, valid, lo, imp)
        node = doc_node(doc, 'leadArm')
        if pk:
            row['leadArm_peak_before_ms'] = (imp - pk['t_us']) / 1e3
            row['leadArm_peak_dps'] = pk['peak']
            row['leadArm_tsig_ms_nominal'] = pk['t_sigma_ms']
        if node:
            row['leadArm_doc_route'] = node['routeId']
            row['leadArm_doc_before_ms'] = node['beforeImpactMs']
            row['leadArm_doc_peak_dps'] = node['peakDps']
            row['leadArm_doc_placed'] = node['placed']
            if pk:
                row['leadArm_dt_ms'] = row['leadArm_doc_before_ms'] - row['leadArm_peak_before_ms']
        m = doc_series(doc, 'leadArmAngularSpeed')
        if m:
            series['leadArm_doc'] = (np.array(m['t_us'], float), np.array(m['value'], float))
        series['leadArm'] = (t, np.where(valid, r, np.nan))

    # ── 6  club (record only) ──────────────────────────────────────────────────────────────────
    cu = sk['u'].copy(); cu[sk['s'] < 2] = np.nan        # shaftTier: 2 one view, 3 both
    row['club_usable_2plus'] = float((sk['s'][dom] >= 2).mean())
    alpha, off = plane_angle(cu, dom)
    if alpha is not None:
        r, sg = rate_of(t, alpha, 1.0, True, angle_sigma_deg)
        valid = np.isfinite(r)
        pk = peak(t, r, sg, valid, lo, imp)
        node = doc_node(doc, 'club')
        if pk:
            row['club_peak_before_ms'] = (imp - pk['t_us']) / 1e3
            row['club_peak_dps'] = pk['peak']
        if node:
            row['club_doc_route'] = node['routeId']
            row['club_doc_before_ms'] = node['beforeImpactMs']
            row['club_doc_peak_dps'] = node['peakDps']

    # ── 5  the spline prior (an alternative fit of the same swing) ─────────────────────────────
    if alt_dir:
        cand = [os.path.join(alt_dir, d) for d in os.listdir(alt_dir) if d.endswith(name)]
        rp = os.path.join(cand[0], 'result.json') if cand else None
        if rp and os.path.exists(rp):
            A = json.load(open(rp, encoding='utf-8'))
            sk2 = load_skeleton(A if 'analysis' in A else {'analysis': A})
            if sk2:
                for seg, a, b in (('pelvis', 'LeftUpLeg', 'RightUpLeg'), ('thorax', 'LeftArm', 'RightArm')):
                    psi, ok2 = line_bearing(sk2, a, b)
                    r2, sg2 = rate_of(sk2['t'], unwrap_valid(psi), lead, False, angle_sigma_deg)
                    for tag, hi in (('', imp), ('_post', imp + POST_IMPACT_US)):
                        pk = peak(sk2['t'], r2, sg2, ok2 & np.isfinite(r2), lo, hi)
                        if pk:
                            row[f'{seg}_alt_peak_before_ms{tag}'] = (imp - pk['t_us']) / 1e3
                            row[f'{seg}_alt_peak_dps{tag}'] = pk['peak']
                    series[f'{seg}_alt'] = (sk2['t'], np.where(ok2, r2, np.nan))
    return row, dict(series=series, lo=lo, imp=imp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--session', required=True)
    ap.add_argument('--swings', default='', help='comma list of swing numbers; default all')
    ap.add_argument('--alt', default='', help='run tree of an alternative skeleton fit (<id>/result.json)')
    ap.add_argument('--angle-sigma-deg', type=float, default=0.5)
    ap.add_argument('--lead-right', action='store_true', help='a left-handed golfer (lead side = right)')
    ap.add_argument('--csv')
    ap.add_argument('--dump', help='write the per-frame rates of each swing to this .json')
    a = ap.parse_args()
    want = {int(x) for x in a.swings.split(',') if x}
    dirs = sorted(d for d in os.listdir(a.session) if d.startswith('swing_'))
    if want:
        dirs = [d for d in dirs if int(d.split('_')[1]) in want]
    rows, dumps = [], {}
    for d in dirs:
        row, extra = analyse(os.path.join(a.session, d), a.alt, a.angle_sigma_deg, not a.lead_right)
        rows.append(row)
        if extra and a.dump:
            dumps[d] = {k: [list(map(float, v[0])), [None if not np.isfinite(x) else float(x) for x in v[1]]]
                        for k, v in extra['series'].items()} | {'lo': extra['lo'], 'imp': extra['imp']}
    so = sequence_order_findings(a.session)
    if so:
        print('sequence_order entries in diagnostics.json:', json.dumps(so)[:3000])
    keys = []
    for r in rows:
        for k in r:
            if k not in keys:
                keys.append(k)
    for r in rows:
        print(json.dumps({k: (round(v, 3) if isinstance(v, float) else v) for k, v in r.items()}))
    if a.csv:
        with open(a.csv, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=keys); w.writeheader()
            for r in rows:
                w.writerow({k: (f'{v:.3f}' if isinstance(v, float) else v) for k, v in r.items()})
    if a.dump:
        json.dump(dumps, open(a.dump, 'w'))


if __name__ == '__main__':
    main()
