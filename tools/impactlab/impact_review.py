#!/usr/bin/env python3
"""impactlab — review and refine the impact camera's track without the app.

Reads a swing's `analysis.impact` (ImpactRunner's MEASURED per-frame ball,
hosel, shaft and head-glint samples — impact_camera_design.md §7, §10.3),
decodes the impact clip, synthesises the clubhead path from the measurements,
draws what the impact tile draws, and writes one contact sheet + one metrics
line per swing. Iterate here; port what survives.

    python3 tools/impactlab/impact_review.py <swing_dir>... [--out DIR] [--every N]

Synthesis (the lab-first version of what the C++ stage will do):
  1. the run: club frames from 30 before the ball leaves to 3 after, edge frames
     predicted but never fitted;
  2. robust quadratics in time for the hosel x(t), y(t) and the shaft angle
     theta(t) (IRLS, Huber), residual outliers dropped, refit — the de-jitter;
  3. the head as a RIGID OFFSET from the hosel in the club's own frame,
     self-calibrated from the glint frames against the SMOOTHED hosel/angle
     (median over frames), with a feedback pass that rejects glints far from
     the prediction and re-estimates; a club-type default when no glints;
  4. the synthesised head on every frame of the run = smoothed hosel +
     R(theta) · d, and the swept space as a ribbon of head half-width along it.
"""
import argparse, json, math, os, sys
import numpy as np, cv2
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing as load_swing_doc  # noqa: E402

BALL_MM = 42.67
ACCENT = (0, 160, 255)     # BGR ≈ the theme accent (amber)
ACCENT_DIM = (0, 100, 160)


def load_swing(swing_dir):
    d = load_swing_doc(swing_dir)
    im = d.get('analysis', {}).get('impact')
    st = [s for s in d['streams'] if s.get('setup', {}).get('perspective') == 4]
    if not im or not st:
        return None, None, None
    cap = cv2.VideoCapture(os.path.join(swing_dir, st[0]['file']))
    frames = []
    while True:
        ok, f = cap.read()
        if not ok: break
        frames.append(cv2.cvtColor(f, cv2.COLOR_BGR2GRAY))
    return d, im, frames


def robust_polyfit(t, y, deg=2, huber=3.0, iters=4):
    """IRLS quadratic; returns coefficients and the final weights."""
    t = np.asarray(t, float); y = np.asarray(y, float)
    w = np.ones_like(y)
    coef = np.polyfit(t, y, deg)
    for _ in range(iters):
        r = y - np.polyval(coef, t)
        s = 1.4826 * np.median(np.abs(r - np.median(r))) + 1e-6
        a = np.abs(r) / (huber * s)
        w = np.where(a <= 1, 1.0, 1.0 / a)
        coef = np.polyfit(t, y, deg, w=np.sqrt(w))
    return coef, w


def foreground_weak(frames, k_bg=30):
    """Background + weak foreground threshold + never-mask, as ImpactRunner builds them."""
    stack = np.stack(frames[:k_bg]).astype(np.float32)
    bg = np.median(stack, 0); mad = np.median(np.abs(stack - bg), 0)
    noise = max(1.0, float(np.median(mad)))
    strong = max(10.0, 6 * noise); weak = max(4.0, 2.5 * noise)
    never = (mad > max(strong, 4 * noise)).astype(np.uint8)
    cnt = np.zeros_like(bg)
    for f in frames: cnt += (f.astype(np.float32) - bg > weak)
    persist = (cnt > 0.33 * len(frames)).astype(np.uint8)
    never = np.maximum(never, cv2.dilate(persist, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))))
    never = cv2.dilate(never, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
    return bg, weak, never


def loess(t, y, keep, tq, half=6.0):
    """Robust LOCAL quadratic at tq: tricube weights over ±half frames on the kept
    samples (a global quadratic cannot follow a velocity that changes at impact)."""
    t = np.asarray(t, float); y = np.asarray(y, float); k = np.asarray(keep, bool)
    w = np.clip(1 - (np.abs(t - tq) / half) ** 3, 0, 1) ** 3 * k
    if (w > 0).sum() < 4:                      # thin end: widen until four points
        for h in (half * 1.5, half * 2.5, half * 4):
            w = np.clip(1 - (np.abs(t - tq) / h) ** 3, 0, 1) ** 3 * k
            if (w > 0).sum() >= 4: break
    if (w > 0).sum() < 3:
        return float(np.polyval(np.polyfit(t[k], y[k], 1), tq))
    deg = 2 if (w > 0).sum() >= 5 else 1
    c = np.polyfit(t, y, deg, w=np.sqrt(w))
    return float(np.polyval(c, tq))


def synthesise(im, frames=None, before=30, after=60, prior_mm=(30.0, 35.0)):
    """From measured samples (+ the frames, for the feedback pass) to a smooth
    head path. prior_mm: the head centre's offset from the hosel in the club's
    frame (along the shaft, across toward the toe) when no evidence says
    otherwise — a generic head, since the capture's club label is unreliable."""
    W, H = im['width'], im['height']
    smp = im['samples']
    leave = im.get('ballLeaveTUs')
    if leave is None:
        return None
    t_all = np.array([s['t_us'] for s in smp], float)
    period = (t_all[-1] - t_all[0]) / max(1, len(smp) - 1)
    dep = int(np.argmin(np.abs(t_all - leave)))
    mmpx = im.get('mmPerPx') or 1.1
    ball_px = BALL_MM / mmpx

    rows = []
    for i in range(max(0, dep - before), min(len(smp), dep + after + 1)):
        s = smp[i]
        if 'hx' not in s: continue
        hx, hy = s['sbx'] * W, s['sby'] * H                      # hosel (px)
        ax, ay = s['sbx'] * W - s['sax'] * W, s['sby'] * H - s['say'] * H
        n = math.hypot(ax, ay) or 1.0
        rows.append(dict(i=i, t=(t_all[i] - leave) / period, hx=hx, hy=hy,
                         th=math.atan2(ay / n, ax / n),
                         gx=s['hx'] * W if s.get('hs') else None, gy=s['hy'] * H if s.get('hs') else None,
                         edge=bool(s.get('edge'))))
    fit = [r for r in rows if not r['edge']]
    if len(fit) < 4:
        return None
    t = np.array([r['t'] for r in fit])
    hx_a = np.array([r['hx'] for r in fit]); hy_a = np.array([r['hy'] for r in fit]); th_a = np.array([r['th'] for r in fit])
    # Hard outlier drop: a hosel more than max(8 px, 3 MAD) off the robust fit is
    # a broken measurement (the club touching the ball shortens the shaft and
    # the "hosel" jumps up it), not jitter to average. Two rounds.
    keep = np.ones(len(fit), bool)
    for _ in range(2):
        cx, _ = robust_polyfit(t[keep], hx_a[keep]); cy, _ = robust_polyfit(t[keep], hy_a[keep])
        res = np.hypot(hx_a - np.polyval(cx, t), hy_a - np.polyval(cy, t))
        mad = 1.4826 * np.median(np.abs(res[keep] - np.median(res[keep]))) + 1e-6
        newkeep = res <= max(8.0, 3 * mad)
        if newkeep.sum() < 4 or np.array_equal(newkeep, keep): break
        keep = newkeep
    # The path is an ARC in space, whatever the timing does: the head slows at
    # impact but the shape it sweeps stays a smooth curve. So: the hosel's
    # y as a robust quadratic in x over the whole run (one curve, no local
    # windows to wobble), the shaft angle as a robust line in x, and TIME only
    # says where along the arc the club is on each frame (x(t), local fit).
    # The outlier drop above (global quadratic in time) already found the
    # broken contact-frame hosels; re-check once against the arc.
    def arc_fit(k):
        cyx, _ = robust_polyfit(hx_a[k], hy_a[k], deg=2)
        cthx, _ = robust_polyfit(hx_a[k], th_a[k], deg=1)
        return cyx, cthx
    cyx, cthx = arc_fit(keep)
    res = np.abs(hy_a - np.polyval(cyx, hx_a))
    mad = 1.4826 * np.median(np.abs(res[keep] - np.median(res[keep]))) + 1e-6
    nk = res <= max(6.0, 3 * mad)
    if nk.sum() >= 4:
        keep = nk; cyx, cthx = arc_fit(keep)
    def smooth(tq):
        x = loess(t, hx_a, keep, tq)
        return (x, float(np.polyval(cyx, x)), float(np.polyval(cthx, x)))
    res_h = np.abs(hy_a - np.polyval(cyx, hx_a))[keep]
    dropped = int((~keep).sum())
    half_w = 0.7 * ball_px          # ~30 mm: a driver head's half-height face-on
    # The head offset: a generic prior, then the feedback loop — predict the
    # head disc on every run frame from the smoothed hosel/angle + current
    # offset, gather ALL weak foreground inside that disc (sole and crown
    # glints are weak and never seed a component on their own), take the
    # centroid where there is enough of it, and re-estimate the offset as the
    # median over frames. Two passes. Neck glints (within 12 px of the hosel
    # along the shaft) never count: they are the hosel, not the head.
    d = np.array([prior_mm[0] / mmpx, prior_mm[1] / mmpx])
    assumed = True; evid = 0
    fg = foreground_weak(frames) if frames else None
    def gather(dcur):
        pts = []
        for r in rows:
            if r['edge'] or fg is None: continue
            bg, weak, never = fg
            hx, hy, th = smooth(r['t'])
            u = np.array([math.cos(th), math.sin(th)]); v = np.array([-u[1], u[0]])
            pred = np.array([hx, hy]) + dcur[0] * u + dcur[1] * v
            m = np.zeros(bg.shape, np.uint8)
            cv2.circle(m, (int(pred[0]), int(pred[1])), int(half_w), 1, -1)
            diff = frames[r['i']].astype(np.float32) - bg
            cand = (diff > weak) & (m > 0) & (never == 0)
            # not the ball, not the neck
            smp_i = im['samples'][r['i']]
            if 'bx' in smp_i:
                bm = np.zeros(bg.shape, np.uint8); cv2.circle(bm, (int(smp_i['bx'] * W), int(smp_i['by'] * H)), int(smp_i['br'] * W * 1.15) + 2, 1, -1)
                cand &= bm == 0
            nm = np.zeros(bg.shape, np.uint8); cv2.circle(nm, (int(hx), int(hy)), 12, 1, -1)
            cand &= nm == 0
            ys, xs = np.nonzero(cand)
            if len(xs) >= 15:
                e = np.array([xs.mean(), ys.mean()]) - np.array([hx, hy])
                pts.append([e @ u, e @ v, len(xs), r['i']])
        return np.array(pts) if pts else np.zeros((0, 4))
    for _ in range(2):
        ev = gather(d)
        if len(ev) >= 3:
            d = np.median(ev[:, :2], 0); assumed = False; evid = len(ev)
    glints = [r for r in rows if r['gx'] is not None and not r['edge']]
    glints_used = evid
    pts = []
    for r in rows:
        hx, hy, th = smooth(r['t'])
        u = np.array([math.cos(th), math.sin(th)]); v = np.array([-u[1], u[0]])
        head = np.array([hx, hy]) + d[0] * u + d[1] * v
        pts.append(dict(i=r['i'], t=r['t'], x=float(head[0]), y=float(head[1]),
                        hx=float(hx), hy=float(hy), th=float(th)))
    xs = np.linspace(min(p['hx'] for p in pts), max(p['hx'] for p in pts), 64)
    ribbon = []
    for x in xs:
        y = float(np.polyval(cyx, x)); th = float(np.polyval(cthx, x))
        u = np.array([math.cos(th), math.sin(th)]); v = np.array([-u[1], u[0]])
        h = np.array([x, y]) + d[0] * u + d[1] * v
        ribbon.append(dict(x=float(h[0]), y=float(h[1])))
    return dict(pts=pts, ribbon=ribbon, arc=cyx.tolist(), theta=cthx.tolist(), d=d.tolist(), assumed=assumed, half_w=half_w, glints=len(glints),
                glints_used=glints_used, fit_frames=int(keep.sum()), dropped=dropped,
                res_rms=float(np.sqrt(np.mean(res_h ** 2))), res_max=float(res_h.max()),
                dep=dep, period=period, W=W, H=H, rows=rows)


def draw_frame(gray, s, syn, im, gain=3.0):
    f = cv2.cvtColor(np.clip(gray.astype(np.float32) * gain, 0, 255).astype(np.uint8), cv2.COLOR_GRAY2BGR)
    W, H = im['width'], im['height']
    if syn:
        # the swept space: a LIGHT band of head half-width along the arc, its
        # edges drawn thin and the centre line thin — the picture shows through
        pts = np.array([[p['x'], p['y']] for p in syn['ribbon']], np.float64)
        if len(pts) >= 2:
            over = f.copy()
            cv2.polylines(over, [pts.astype(np.int32)], False, ACCENT_DIM, int(2 * syn['half_w']), cv2.LINE_AA)
            f = cv2.addWeighted(over, 0.10, f, 0.90, 0)
            d = np.gradient(pts, axis=0); n = d / (np.linalg.norm(d, axis=1, keepdims=True) + 1e-9)
            nrm = np.stack([-n[:, 1], n[:, 0]], 1) * syn['half_w']
            for sgn in (1, -1):
                cv2.polylines(f, [(pts + sgn * nrm).astype(np.int32)], False, ACCENT_DIM, 1, cv2.LINE_AA)
            cv2.polylines(f, [pts.astype(np.int32)], False, ACCENT, 1, cv2.LINE_AA)
    # this frame: ball, shaft, hosel, synthesised head, measured glint
    if 'bx' in s:
        cv2.circle(f, (int(s['bx'] * W), int(s['by'] * H)), int(s['br'] * W) + 2, ACCENT, 1, cv2.LINE_AA)
    if 'hx' in s:   # the raw measurement, faint — evidence, not the product
        cv2.circle(f, (int(s['sbx'] * W), int(s['sby'] * H)), 2, ACCENT_DIM, 1, cv2.LINE_AA)
    return f


def sheet(frames, im, syn, lo, hi, every):
    rows = []
    smp = im['samples']
    cur = {p['i']: p for p in (syn['pts'] if syn else [])}
    for i in range(lo, hi, every):
        f = draw_frame(frames[i], smp[i], syn, im)
        if i in cur:
            c = cur[i]
            # the smoothed shaft: from the synthesised hosel up the smoothed angle
            L = 1.6 * syn['half_w'] / 0.7        # ≈ the visible shaft (a ball diameter and a half)
            gx, gy = c['hx'] - L * math.cos(c['th']), c['hy'] - L * math.sin(c['th'])
            cv2.line(f, (int(gx), int(gy)), (int(c['hx']), int(c['hy'])), ACCENT, 1, cv2.LINE_AA)
            cv2.circle(f, (int(c['hx']), int(c['hy'])), 2, ACCENT, -1, cv2.LINE_AA)
            cv2.circle(f, (int(c['x']), int(c['y'])), int(syn['half_w']), ACCENT, 1, cv2.LINE_AA)
            cv2.drawMarker(f, (int(c['x']), int(c['y'])), ACCENT, cv2.MARKER_CROSS, 8, 1, cv2.LINE_AA)
        cv2.putText(f, f'f{i}', (4, 12), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (200, 200, 200), 1)
        rows.append(f)
    return np.concatenate(rows, 0) if rows else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('swings', nargs='+')
    ap.add_argument('--out', default='build/impactlab')
    ap.add_argument('--every', type=int, default=2)
    ap.add_argument('--span', type=int, nargs=2, default=(-14, 18), help='frames around departure on the sheet')
    ap.add_argument('--from-json', action='store_true', help="draw analysis.impact.path (the C++ stage's synthesis) instead of the lab's")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    print('swing        mm/px  leave-anchor  hosel-frames  fit(dropped)  head-evidence-frames  offset(px along,across)  hosel-res rms/max px')
    for sd in a.swings:
        d, im, frames = load_swing(sd)
        name = os.path.basename(sd.rstrip('/'))
        if not im:
            print(f'{name}  NO IMPACT TRACK'); continue
        syn = synthesise(im, frames)
        if a.from_json and im.get('path', {}).get('kind') == 'synth' and syn is not None:
            P = im['path']; W, H = im['width'], im['height']
            t_all = [x['t_us'] for x in im['samples']]
            lab_pts = {p['i']: (p['x'], p['y']) for p in syn['pts']}
            syn['pts'] = [dict(i=min(range(len(t_all)), key=lambda k: abs(t_all[k] - q['t_us'])), t=0,
                               x=q['x'] * W, y=q['y'] * H, hx=q['hx'] * W, hy=q['hy'] * H, th=q['th']) for q in P['points']]
            syn['half_w'] = P['halfWidth'] * W
            if 'ribbon' in P:
                syn['ribbon'] = [dict(x=q['x'] * W, y=q['y'] * H) for q in P['ribbon']]
            else:
                syn['ribbon'] = [dict(x=q['x'], y=q['y']) for q in syn['pts']]
            syn['d'] = [P['headAlongPx'], P['headAcrossPx']]; syn['assumed'] = P['headAssumed']
            syn['glints_used'] = P['headEvidence']; syn['fit_frames'] = P['hoselFit']; syn['dropped'] = P['hoselDropped']
            syn['res_rms'] = P['hoselResRmsPx']; syn['res_max'] = float('nan')
            # parity: the stage's head vs the lab's head on the same frames
            diffs = [math.hypot(q['x'] - lab_pts[q['i']][0], q['y'] - lab_pts[q['i']][1]) for q in syn['pts'] if q['i'] in lab_pts]
            syn['parity_px'] = (sum(diffs) / len(diffs)) if diffs else float('nan')
        leave = im.get('ballLeaveTUs'); imp = d['capture']['impactUs']
        if syn is None:
            print(f'{name}  synthesis: too few hosel frames'); continue
        print(f"{name}  {im['mmPerPx']:.2f}   {(leave - imp) / 1000:+6.1f} ms     "
              f"{len([r for r in syn['rows']]):3d}       {syn['fit_frames']:3d}({syn['dropped']})        {syn['glints_used']:2d}          "
              f"({syn['d'][0]:+6.1f},{syn['d'][1]:+6.1f}){' assumed' if syn['assumed'] else ''}     "
              f"{syn['res_rms']:.2f}/{syn['res_max']:.2f}"
              + (f"   stage-vs-lab head {syn['parity_px']:.1f} px" if 'parity_px' in syn else ''))
        dep = syn['dep']
        img = sheet(frames, im, syn, max(0, dep + a.span[0]), min(len(frames), dep + a.span[1]), a.every)
        if img is not None:
            cv2.imwrite(os.path.join(a.out, f'{name}.png'), img)


if __name__ == '__main__':
    main()
