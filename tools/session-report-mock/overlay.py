# overlay.py — a still at a phase, cropped later by the register, with the measured body line drawn on it:
# this frame's line solid (warn); optionally the same swing's ADDRESS line dashed white, for a change-from-address measure.
# Keypoints: analysis.poseDtl / pose2d .smoothed[...] (133 × x,y,conf, normalised), matched to the frame by t_us.
import sys, os, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))   # tools/
import pp_swingdoc as p
from PIL import Image, ImageDraw
S = None       # set by assemble.py: the stills folder beside report.json
BASE = None    # set by assemble.py: the session folder
PH = {'P1': 0, 'P2': 12, 'P3': 8, 'P4': 2, 'P5': 13, 'P6': 9, 'P7': 5, 'P8': 14, 'P9': 11, 'P10': 7}
WARN = (196, 82, 40, 255); GHOST = (255, 255, 255, 235)
_docs = {}
def doc(sw):
    if sw not in _docs: _docs[sw] = p.load_swing(f'{BASE}/swing_{sw:04d}/swing.ppsw')
    return _docs[sw]
def frame_index(d, cam, phase):
    st = [s for s in d['streams'] if s['kind'] == 'video' and s['alias'] == cam][0]
    ts = st['frames']['t_us']; t = {x['phase']: x['t_us'] for x in d['analysis']['phases']}[PH[phase]]
    return min(range(len(ts)), key=lambda k: abs(ts[k] - t)), ts
def still(sw, cam, phase):
    out = f'{S}/s{sw:04d}_{cam}_{phase}.jpg'
    if not os.path.exists(out):
        i, _ = frame_index(doc(sw), cam, phase)
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', f'{BASE}/swing_{sw:04d}/{cam}.mp4', '-vf', f'select=eq(n\\,{i})', '-vframes', '1', '-q:v', '2', out], check=True)
    return out
def kp_at(d, cam, phase):
    pose = d['analysis']['poseDtl' if cam == 'DTL' else 'pose2d']
    i, ts = frame_index(d, cam, phase); sm = pose['smoothed']
    j = min(range(len(sm)), key=lambda k: abs(sm[k]['t_us'] - ts[i]))
    return sm[j]['kp']
def _pts(kp, ids, W, H): return [(kp[3*j] * W, kp[3*j+1] * H) for j in ids]
def _mid(a, b): return ((a[0]+b[0])/2, (a[1]+b[1])/2)
def _line(g, ps, col, w, dash=False):
    for (x0, y0), (x1, y1) in zip(ps, ps[1:]):
        if not dash: g.line([(x0, y0), (x1, y1)], fill=col, width=w); continue
        L = ((x1-x0)**2 + (y1-y0)**2) ** 0.5; n = max(1, int(L // 14))
        for k in range(n):
            if k % 2: continue
            a, b = k / n, min(1, (k + 1) / n)
            g.line([(x0 + (x1-x0)*a, y0 + (y1-y0)*a), (x0 + (x1-x0)*b, y0 + (y1-y0)*b)], fill=col, width=w)
    for x, y in ps: g.ellipse([x-w, y-w, x+w, y+w], fill=col)
SEG = {'knee': lambda kp, W, H: _pts(kp, [12, 14, 16], W, H),                          # trail hip – knee – ankle (right-handed)
       'spine': lambda kp, W, H: [_mid(*_pts(kp, [11, 12], W, H)), _mid(*_pts(kp, [5, 6], W, H))]}
def make(sw, cam, phase, kind, ghost_address=False):
    src = still(sw, cam, phase)
    im = Image.open(src).convert('RGB'); W, H = im.size; g = ImageDraw.Draw(im, 'RGBA')
    d = doc(sw)
    _line(g, SEG[kind](kp_at(d, cam, phase), W, H), WARN, 7)
    if ghost_address: _line(g, SEG[kind](kp_at(d, cam, 'P1'), W, H), GHOST, 5, dash=True)
    out = f'{S}/o{sw:04d}_{cam}_{phase}_{kind}{"_g" if ghost_address else ""}.jpg'
    im.save(out, quality=92)
    return out
