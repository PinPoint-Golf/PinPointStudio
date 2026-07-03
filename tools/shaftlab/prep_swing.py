#!/usr/bin/env python3
"""Prep a swing for the shaft exemplar: clip + extended anchors (frame,gx,gy,phi_deg,phi_ok) where phi is the
lead-forearm extension direction (elbow->grip, image deg). Usage: prep2.py <swingDir> <outDir>"""
import json, sys, os, csv, math
import numpy as np, cv2

sw, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
d = json.load(open(f"{sw}/swing.json"))
t0 = d['clock']['t0_us']
face = [s for s in d['streams'] if s.get('kind') == 'video'
        and (s.get('setup', {}).get('perspective') == 2 or 'Face' in s.get('alias', ''))][0]
vt = np.array(face['frames']['t_us'], float)
W, H = face['encoded']['width'], face['encoded']['height']
pose = d['analysis']['pose2d']['frames']
prel = np.array([p['t_us'] - t0 for p in pose], float)
lead = np.array([p['lead'] for p in pose], float)
trail = np.array([p['trail'] for p in pose], float)
grip = 0.5 * (lead + trail)
# lead forearm: handedness Right -> lead arm is COCO left (elbow 7); Left -> right (8)
rh = d.get('athlete', {}).get('handedness', 'Right').lower().startswith('r')
ei = 7 if rh else 8
elb = np.array([[p['kp'][3*ei], p['kp'][3*ei+1]] for p in pose], float)
econf = np.array([p['kp'][3*ei+2] for p in pose], float)
# body joints for the clutter mask: shoulders, hips, knees, ankles (COCO 5,6,11..16)
BODY_J = [5, 6, 11, 12, 13, 14, 15, 16]
bj = {j: (np.array([[p['kp'][3*j], p['kp'][3*j+1]] for p in pose], float),
          np.array([p['kp'][3*j+2] for p in pose], float)) for j in BODY_J}

covered = np.where((vt >= prel[0]) & (vt <= prel[-1]))[0]
n0, n1 = int(covered[0]), int(covered[-1])
sub = np.arange(n0, n1 + 1)
fps = (len(sub) - 1) / ((vt[n1] - vt[n0]) / 1e6)
gx = np.interp(vt[sub], prel, grip[:, 0]) * W
gy = np.interp(vt[sub], prel, grip[:, 1]) * H
ex = np.interp(vt[sub], prel, elb[:, 0]) * W
ey = np.interp(vt[sub], prel, elb[:, 1]) * H
ec = np.interp(vt[sub], prel, econf)

cap = cv2.VideoCapture(f"{sw}/{face['file']}")
# Integer container fps: mpeg4 rejects some fractional rates as an invalid
# timebase (EINVAL, swing-dependent). Analysis always uses clipmeta's true fps
# via --fps-override; the container rate only affects casual playback speed.
vw = cv2.VideoWriter(f"{out}/faceon_swing.mp4", cv2.VideoWriter_fourcc(*"mp4v"),
                     int(round(fps)), (W, H))
rows, i, o = [], 0, 0
while True:
    ok, frame = cap.read()
    if not ok: break
    if n0 <= i <= n1:
        dx, dy = gx[o] - ex[o], gy[o] - ey[o]
        plen = math.hypot(dx, dy)
        pok = 1 if (ec[o] > 0.3 and plen > 8) else 0
        phi = math.degrees(math.atan2(dy, dx)) if pok else 0.0
        rows.append([o, float(gx[o]), float(gy[o]), round(phi, 2), pok])
        vw.write(frame)
        o += 1
    i += 1
cap.release(); vw.release()
with open(f"{out}/anchors.csv", "w", newline="") as f:
    csv.writer(f).writerows(rows)
# skeleton.csv: frame, then x,y,conf (px) per body joint in BODY_J order —
# consumed by shaft_annotate's body-collinearity gate (a candidate ray whose
# evidence run tracks the golfer's own torso/legs is not the club)
srows = []
for o2 in range(len(sub)):
    t = vt[sub][o2]
    row = [o2]
    for j in BODY_J:
        pts, cf = bj[j]
        row += [round(float(np.interp(t, prel, pts[:, 0]) * W), 1),
                round(float(np.interp(t, prel, pts[:, 1]) * H), 1),
                round(float(np.interp(t, prel, cf)), 2)]
    srows.append(row)
with open(f"{out}/skeleton.csv", "w", newline="") as f:
    csv.writer(f).writerows(srows)
json.dump({"swingDir": sw, "frame0": n0, "fps": fps, "W": W, "H": H,
           "t_us": [int(t) for t in vt[sub]]}, open(f"{out}/clipmeta.json", "w"))
print(f"clip: {o} frames @ {fps:.2f} fps, ext anchors (phi_ok on {sum(r[4] for r in rows)}/{o})")
