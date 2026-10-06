#!/usr/bin/env python3
"""Thin a pinned pose track by swing phase, to grade a sparser pose schedule downstream
(docs/design/pose_inference_performance_plan.md step 4) with NO code change: the thinned file is
injected through `swinglab_run --pose` / `--dtl-pose` and the shaft trackers, skeleton3d and the
metrics run as they are.

  thin_pose.py <pose.json> <swing dir> <policy> <out.json>

The pose file is {"frames":[{t_us, kp, ...}]} in the corpus pinned shape; phases come from the swing
document (analysis.phases, window-relative µs, the same clock as the pose). A policy is a list of
(zone, stride) pairs applied in order, the first zone that contains a frame's t_us deciding; frames
outside every zone keep stride `rest`. Zones are named by phase ids with an offset in ms:
    "P3-50:P8+50=1, P1-100:P3-50=2, rest=4"
P-names: P1 address, P2, P3, P4 top, P5, P6, P7 impact, P8, fin. A frame is kept when it is the
stride-th since the zone's first frame, counted inside the zone, so zone edges are always kept.
Prints the kept/total count and the largest gap per zone.
"""
import json, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from pp_swingdoc import load_swing

PH = {'P1': 0, 'tkw': 1, 'P2': 12, 'P3': 8, 'P4': 2, 'P5': 13, 'P6': 9, 'P7': 5, 'P8': 14, 'fin': 7}

def phase_times(swing_dir):
    a = load_swing(swing_dir)['analysis']
    byid = {p['phase']: p['t_us'] for p in a.get('phases', [])}
    return {name: byid[i] for name, i in PH.items() if i in byid}

def parse_policy(text, ph):
    zones, rest = [], 1
    for part in [p.strip() for p in text.split(',') if p.strip()]:
        if part.startswith('rest'):
            rest = int(part.split('=')[1]); continue
        span, stride = part.split('=')
        lo, hi = span.split(':')
        def t(s):
            m = re.fullmatch(r'(P\d|fin|tkw)([+-]\d+)?', s)
            if not m or m.group(1) not in ph: return None
            return ph[m.group(1)] + int(m.group(2) or 0) * 1000
        a, b = t(lo), t(hi)
        if a is None or b is None: continue           # a phase this swing never found: zone dropped
        zones.append((a, b, int(stride)))
    return zones, rest

def main():
    src, swing_dir, policy, out = sys.argv[1:5]
    frames = json.load(open(src))['frames']
    ph = phase_times(swing_dir)
    zones, rest = parse_policy(policy, ph)
    kept, counters, gaps, last = [], {}, {}, {}
    for f in frames:
        t = f['t_us']; key, stride = 'rest', rest
        for i, (a, b, s) in enumerate(zones):
            if a <= t <= b: key, stride = i, s; break
        n = counters.get(key, 0); counters[key] = n + 1
        if n % stride == 0:
            kept.append(f)
            if key in last: gaps[key] = max(gaps.get(key, 0), (t - last[key]) / 1000.0)
            last[key] = t
    json.dump({'frames': kept}, open(out, 'w'))
    desc = {('rest' if k == 'rest' else f'zone{k}'): f'{counters[k]} -> max gap {gaps.get(k, 0):.0f} ms' for k in counters}
    print(f'{os.path.basename(out)}: kept {len(kept)} of {len(frames)} {desc}')

if __name__ == '__main__':
    main()
