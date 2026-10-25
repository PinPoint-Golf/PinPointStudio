#!/usr/bin/env python3
"""The whole swing's kinematic sequence curves, address to finish, for judging their SHAPE.

The app draws the sequence from the top to P8; this draws all four curves over the whole swing on one axis
(pelvis red, chest green, lead arm blue, club gold, with address / top / impact / finish lines), each with its
direction (towards the target +), so a curve can be held against a reference sequence: every segment negative
going back, crossing zero near the top, one hump coming down (kinematic_sequence_design.md §12.6; #102).

The swings must be analysed with the sequence's curve left uncut first — swinglab_run with a params file
{"sequence":{"throughP8":false}} on scratch COPIES (it writes back; never the library). Samples the analysis left
unmeasured inside the sequence's domain (transition to impact: pose lost, or near-square chest samples the gap
bridge rejected) are gaps; samples outside the domain are drawn (masked only because the sequence does not read
them). The chest's bridge, when there is one, is drawn dashed.

usage: kinematics/plot_fullswing.py <out.png> <swing dir> [<swing dir> ...]
"""
import sys, os, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from pp_swingdoc import load_swing
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

SERIES = [('pelvisAngularSpeed', 'Pelvis', '#c0392b'), ('thoraxAngularSpeed', 'Chest', '#2e7d32'),
          ('leadArmAngularSpeed', 'Lead arm', '#1f3fd1'), ('clubAngularSpeed', 'Club', '#a8902a')]
ADDRESS, TOP, TRANSITION, IMPACT, FINISH = 0, 2, 3, 5, 7


def segments(t, v, ok):
    """Split into runs of drawable samples."""
    runs, cur = [], ([], [])
    for a, b, k in zip(t, v, ok):
        if k:
            cur[0].append(a); cur[1].append(b)
        elif cur[0]:
            runs.append(cur); cur = ([], [])
    if cur[0]: runs.append(cur)
    return runs


def plot(ax, swing_dir, title):
    a = load_swing(swing_dir)['analysis']
    ks = a['kinematicSequence']
    imp = ks['impactUs']
    ph = {}
    for p in a.get('phases', []):
        if p.get('phase') not in ph: ph[p.get('phase')] = p['t_us']
    t0 = ph.get(ADDRESS)
    if t0 is None: t0 = min(p['t_us'] for p in a['phases'])
    dom = (ph.get(TRANSITION, ph.get(TOP)), imp)
    sec = lambda u: (u - t0) / 1e6
    for key, label, col in SERIES:
        ms = [m for m in a['metrics'] if m['key'] == key]
        if not ms: continue
        m = ms[0]
        valid = m.get('valid') or [1] * len(m['t_us'])
        ok = []
        for u, v, k in zip(m['t_us'], m['value'], valid):
            if k: ok.append(True)
            elif dom[0] is not None and dom[0] <= u <= dom[1]: ok.append(False)      # unmeasured in the domain
            else: ok.append(v != 0.0)                                                  # masked only by the domain
        first = True
        for xs, ys in segments([sec(u) for u in m['t_us']], m['value'], ok):
            ax.plot(xs, ys, color=col, lw=1.6, label=label if first else None)
            first = False
        if key == 'thoraxAngularSpeed':
            for n in ks.get('nodes', []):
                br = (n.get('probable') or {}).get('bridge') if n.get('segment') == 'thorax' else None
                if br and br.get('tMs'):
                    ax.plot([sec(imp + x * 1000) for x in br['tMs']], br['mean'], color=col, lw=1.6, ls=(0, (3, 2)))
    for pid, lab in [(ADDRESS, 'Adr'), (TOP, 'Top'), (IMPACT, 'Imp'), (FINISH, 'Fin')]:
        u = imp if pid == IMPACT else ph.get(pid)
        if u is None: continue
        ax.axvline(sec(u), color='#555', lw=0.9)
        ax.text(sec(u), 1.01, lab, transform=ax.get_xaxis_transform(), ha='center', va='bottom', fontsize=9, color='#333')
    ax.axhline(0, color='#888', lw=0.8)
    ax.set_ylim(-800, 2600)
    ax.grid(True, color='#e4e4e4', lw=0.6)
    ax.set_title(title, fontsize=10, pad=16, loc='left')
    ax.set_xlabel('s from address', fontsize=8)
    ax.set_ylabel('°/s', fontsize=8)
    ax.tick_params(labelsize=8)


def main():
    out, dirs = sys.argv[1], sys.argv[2:]
    n = len(dirs)
    cols = 2 if n > 1 else 1
    rows = math.ceil(n / cols)
    fig, axes = plt.subplots(rows, cols, figsize=(8 * cols, 3.8 * rows), squeeze=False)
    for i, d in enumerate(dirs):
        plot(axes[i // cols][i % cols], d, os.path.basename(d.rstrip('/')))
    for j in range(n, rows * cols): axes[j // cols][j % cols].axis('off')
    axes[0][0].legend(loc='upper left', fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    print(out)


main()
