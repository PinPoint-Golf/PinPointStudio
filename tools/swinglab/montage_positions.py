#!/usr/bin/env python3
"""montage_positions.py -- a montage of shaft P1-P8 coaching positions,
plus measured-vs-synthesized strobe tiles, for a swing corpus
(shaft_position_first design Sec 2 Layer B).

Two artefacts per swing, written under --out:
  <swing>_pstrip.png    one row of 8 tiles, one per coaching position P1..P8:
                         the video frame nearest the position's t_us, cropped
                         around the drawn shaft, with the shaft line drawn
                         grip->head -- BRIGHT GREEN when source=MilestoneFit
                         (the shift-and-stack fit), BRIGHT ORANGE when
                         source=TrackSample (sampled straight off the track).
                         A thin white reference line is added when truth.json
                         has a labelled shaft frame within 40 ms. Missing P's
                         render as a grey placeholder tile.
  <swing>_strobe.png    ONE background frame (the P4 frame) with every P
                         position drawn bright (as above) plus, when synth[]
                         exists (or --strobe-synth forces the fallback), a
                         low-alpha stride-sampled overlay of the interpolated
                         states in between -- so measured-vs-synthesized
                         reads at a glance. When synth[] is absent AND
                         --strobe-synth is passed, the dim layer falls back
                         to samples[]'s measured-tier (flags & ShaftMeasured)
                         states instead, labelled "track" in the header.

Plus two corpus-wide grids (all requested swings stacked into one PNG each,
width capped to ~2400px): corpus_pstrips.png, corpus_strobes.png.

  montage_positions.py <run_dir> <corpus_dir> --out <dir>
      [--swings s01,s03,...] [--strobe-synth] [--synth-stride N]
  montage_positions.py --selftest <tmpdir>

<run_dir> holds one subdir per swing name with result.json + runmeta.json
(the `lab.py run` / `run_corpus` output layout -- swinglab.RunResult).
<corpus_dir> holds one subdir per swing name with swing.json (+ truth.json,
+ the face-on video) -- the swinglab.Swing layout.

positions[] (P1-P8) and synth[] are read straight off result.json's
analysis.club block (RunResult.club); see src/Analysis/swing_analysis.h
(ShaftPosition) and src/Export/swing_doc.cpp for the on-disk schema this
mirrors. synth[] may not exist yet on older runs -- treated as optional
throughout.

t_us -> video frame mapping: swing.json's face-on video stream carries
frames.t_us, one timestamp per encoded frame, in the SAME window-relative
base as result.json's samples[]/positions[]/synth[] t_us and truth.json's
shaft[].t_us (score.py and swinglab.plots.contact_sheet already assume this
and compare them directly with no offset). A frame is located by
np.searchsorted(frame_ts, t_us) clamped to range, then a hard seek via
cv2.CAP_PROP_POS_FRAMES -- the exact technique swinglab.plots._frame_at uses.
"""
import argparse
import math
import shutil
import sys
from pathlib import Path

import numpy as np
import cv2

# Windows consoles default to cp1252 -- reconfigure so nothing in these
# prints can crash the tooling on the studio box (mirrors lab.py).
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, str(Path(__file__).parent))
from swinglab import Swing, RunResult  # noqa: E402  (same-dir package, see lab.py)

# ---------------------------------------------------------------- constants
TILE = 260                     # square P-strip tile side, px
STROBE_MAX_W = 900             # per-swing strobe image cap, px
GRID_MAX_W = 2400              # corpus montage width cap, px
CROP_PAD_FRAC = 0.55           # crop half-pad as a fraction of shaft length
CROP_MIN_SIDE = 180            # never crop tighter than this (px, pre-resize)
TRUTH_TOL_US = 40_000          # "within 40 ms" truth-match window

SRC_TRACKSAMPLE, SRC_MILESTONEFIT = 0, 1
SHAFT_MEASURED = 0x01          # ShaftSample2D flag bit (swing_analysis.h)

COL_MILESTONE = (0, 255, 0)      # bright green  (BGR) -- source=MilestoneFit
COL_TRACK     = (0, 140, 255)    # bright orange (BGR) -- source=TrackSample
COL_TRUTH     = (255, 255, 255)  # white -- markup reference line
COL_DIM       = (255, 200, 120)  # dim cyan-ish -- synth / track fallback
COL_MISSING_BG = (40, 40, 40)
COL_HEADER_BG  = (18, 18, 18)
COL_HEADER_FG  = (225, 225, 225)

LEGEND_PSTRIP = ("green=MilestoneFit(fit)  orange=TrackSample(raw)  "
                 "white=truth(<=40ms)  grey=P missing")
LEGEND_STROBE = ("bright=P1..P8 measured  thin dim=synth[] interpolation "
                 "(or samples[] 'track' fallback)")


# ------------------------------------------------------------------ drawing
def resize_smart(img, size):
    h, w = img.shape[:2]
    interp = cv2.INTER_AREA if (w > size[0] or h > size[1]) else cv2.INTER_LINEAR
    return cv2.resize(img, size, interpolation=interp)


def draw_line_alpha(img, p0, p1, color, thickness, alpha):
    p0 = (int(round(p0[0])), int(round(p0[1])))
    p1 = (int(round(p1[0])), int(round(p1[1])))
    if alpha >= 0.999:
        cv2.line(img, p0, p1, color, thickness, cv2.LINE_AA)
        return
    overlay = img.copy()
    cv2.line(overlay, p0, p1, color, thickness, cv2.LINE_AA)
    cv2.addWeighted(overlay, alpha, img, 1.0 - alpha, 0.0, dst=img)


def put_text(img, text, org, scale=0.42, color=(240, 240, 240), thickness=1,
            bg=(0, 0, 0), bg_alpha=0.55):
    font = cv2.FONT_HERSHEY_SIMPLEX
    (tw, th), base = cv2.getTextSize(text, font, scale, thickness)
    x, y = int(org[0]), int(org[1])
    if bg is not None:
        overlay = img.copy()
        cv2.rectangle(overlay, (x - 2, y - th - 3), (x + tw + 2, y + base + 2), bg, -1)
        cv2.addWeighted(overlay, bg_alpha, img, 1.0 - bg_alpha, 0.0, dst=img)
    cv2.putText(img, text, (x, y), font, scale, color, thickness, cv2.LINE_AA)


def make_header(width, lines, line_h=22, pad=8):
    """Baked legend/title banner. Auto-shrinks a line's font scale (down to a
    floor) so long legend text never silently clips on a narrow frame -- real
    face-on video is wide enough that this only bites in tiny test fixtures."""
    font = cv2.FONT_HERSHEY_SIMPLEX
    h = pad * 2 + line_h * len(lines)
    img = np.full((h, max(width, 1), 3), COL_HEADER_BG, dtype=np.uint8)
    avail = max(width - 20, 10)
    for i, line in enumerate(lines):
        scale = 0.46
        while scale > 0.24:
            (tw, _), _ = cv2.getTextSize(line, font, scale, 1)
            if tw <= avail:
                break
            scale -= 0.03
        y = pad + line_h * i + int(line_h * 0.72)
        cv2.putText(img, line, (10, y), font, scale, COL_HEADER_FG, 1, cv2.LINE_AA)
    return img


# --------------------------------------------------------------- geometry
def to_px(norm_xy, W, H):
    return (float(norm_xy[0]) * W, float(norm_xy[1]) * H)


def pos_line_px(entry, W, H):
    return to_px(entry["grip"], W, H), to_px(entry["head"], W, H)


def source_color(entry):
    return COL_MILESTONE if int(entry.get("source", 0)) == SRC_MILESTONEFIT else COL_TRACK


def crop_box(W, H, p0, p1, pad_frac=CROP_PAD_FRAC, min_side=CROP_MIN_SIDE):
    """Square crop box around a drawn grip->head line, clamped to the frame.
    None ⇒ crop would be degenerate or basically the whole frame anyway --
    caller falls back to the full (downscaled) frame."""
    length = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    if length < 4.0:
        return None
    cx, cy = (p0[0] + p1[0]) / 2.0, (p0[1] + p1[1]) / 2.0
    side = max(length * (1.0 + 2 * pad_frac), min_side)
    if side >= 0.92 * min(W, H):
        return None
    half = side / 2.0
    x0, y0, x1, y1 = cx - half, cy - half, cx + half, cy + half
    if x0 < 0: x1 -= x0; x0 = 0.0
    if y0 < 0: y1 -= y0; y0 = 0.0
    if x1 > W: x0 -= (x1 - W); x1 = float(W)
    if y1 > H: y0 -= (y1 - H); y1 = float(H)
    x0, y0 = max(0.0, x0), max(0.0, y0)
    x1, y1 = min(float(W), x1), min(float(H), y1)
    if x1 - x0 < 8 or y1 - y0 < 8:
        return None
    return int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))


# ------------------------------------------------------------- video / t_us
def frame_at(cap, frame_ts, t_us):
    """Nearest-frame lookup by t_us -- same technique as
    swinglab.plots._frame_at (searchsorted + hard seek, no interpolation)."""
    idx = int(np.clip(np.searchsorted(frame_ts, t_us), 0, len(frame_ts) - 1))
    cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
    ok, img = cap.read()
    return (img if ok else None), idx


# ------------------------------------------------------------------- truth
def truth_shaft(swing):
    t = swing.truth()
    return (t or {}).get("shaft") or None


def nearest_truth_entry(shaft_list, t_us, tol_us=TRUTH_TOL_US):
    if not shaft_list:
        return None
    best = min(shaft_list, key=lambda f: abs(int(f["t_us"]) - int(t_us)))
    return best if abs(int(best["t_us"]) - int(t_us)) <= tol_us else None


# --------------------------------------------------------------- load data
def load_swing_data(corpus_dir, run_dir, name):
    swing_dir = corpus_dir / name
    try:
        swing = Swing(swing_dir)
    except Exception as e:
        return None, f"{name}: cannot read swing.json ({e})"
    video = swing.face_on()
    if video is None:
        return None, f"{name}: no face-on video stream in swing.json"
    vfile = video.get("file")
    if not vfile:
        return None, f"{name}: video stream has no file name"
    vpath = swing_dir / vfile
    if not vpath.exists():
        return None, f"{name}: video file missing: {vpath}"
    frame_ts = np.array(video.get("frames", {}).get("t_us", []), dtype=np.int64)
    if frame_ts.size == 0:
        return None, f"{name}: video stream has no frame timestamps"

    run_swing_dir = run_dir / name
    if not (run_swing_dir / "result.json").exists():
        return None, f"{name}: no result.json under {run_swing_dir}"
    try:
        run = RunResult(run_swing_dir)
    except Exception as e:
        return None, f"{name}: cannot read run dir ({e})"
    club = run.club
    if not club:
        return None, f"{name}: result.json has no analysis.club block"

    W = club.get("frameWidth") or video.get("encoded", {}).get("width")
    H = club.get("frameHeight") or video.get("encoded", {}).get("height")
    if not W or not H:
        return None, f"{name}: cannot resolve frame dimensions"

    return dict(name=name, swing=swing, vpath=vpath, frame_ts=frame_ts,
                W=int(W), H=int(H),
                positions=club.get("positions", []) or [],
                samples=club.get("samples", []) or [],
                synth=club.get("synth", []) or []), None


def positions_by_p(positions):
    d = {}
    for pos in positions:
        p = pos.get("p")
        if p is not None:
            d[int(p)] = pos
    return d


# --------------------------------------------------------------- P-strip
def missing_tile(p):
    img = np.full((TILE, TILE, 3), COL_MISSING_BG, dtype=np.uint8)
    cv2.rectangle(img, (2, 2), (TILE - 3, TILE - 3), (70, 70, 70), 1)
    put_text(img, f"P{p}", (10, 30), scale=0.7, color=(150, 150, 150), bg=None)
    put_text(img, "missing", (10, TILE - 14), scale=0.42, color=(120, 120, 120), bg=None)
    return img


def position_tile(frame_full, W, H, entry, truth_entry):
    img = frame_full.copy()
    try:
        g, h = pos_line_px(entry, W, H)
    except Exception:
        return missing_tile(entry.get("p", "?"))
    if truth_entry is not None:
        tg, th_ = tuple(truth_entry["grip"]), tuple(truth_entry["head"])
        draw_line_alpha(img, tg, th_, COL_TRUTH, 2, 1.0)
    col = source_color(entry)
    draw_line_alpha(img, g, h, col, 3, 1.0)
    cv2.circle(img, (int(round(h[0])), int(round(h[1]))), 4, col, -1, cv2.LINE_AA)

    box = crop_box(W, H, g, h)
    crop = img[box[1]:box[3], box[0]:box[2]] if box is not None else img
    tile = resize_smart(crop, (TILE, TILE))

    p = entry.get("p")
    conf = float(entry.get("conf", 0.0))
    sigma = entry.get("sigmaThetaDeg", -1.0)
    src_label = "fit" if int(entry.get("source", 0)) == SRC_MILESTONEFIT else "raw"
    put_text(tile, f"P{p}  {src_label}  conf {conf:.2f}", (6, TILE - 24), scale=0.4)
    sigma_txt = f"+/-{sigma:.1f}deg" if (sigma is not None and sigma >= 0) else "(no fit sigma)"
    put_text(tile, sigma_txt, (6, TILE - 8), scale=0.36, color=(200, 200, 200))
    if truth_entry is not None:
        put_text(tile, "truth", (TILE - 52, 16), scale=0.36, color=(255, 255, 255))
    return tile


def build_pstrip(data):
    name = data["name"]
    pos_by_p = positions_by_p(data["positions"])
    truth_list = truth_shaft(data["swing"])
    cap = cv2.VideoCapture(str(data["vpath"]))
    tiles = []
    n_truth_hits = 0
    for p in range(1, 9):
        entry = pos_by_p.get(p)
        if entry is None:
            tiles.append(missing_tile(p))
            continue
        frame, _ = frame_at(cap, data["frame_ts"], int(entry["t_us"]))
        if frame is None:
            tiles.append(missing_tile(p))
            continue
        te = nearest_truth_entry(truth_list, int(entry["t_us"])) if truth_list else None
        if te is not None:
            n_truth_hits += 1
        tiles.append(position_tile(frame, data["W"], data["H"], entry, te))
    cap.release()

    row = np.concatenate(tiles, axis=1)
    header = make_header(row.shape[1], [f"{name} -- P1..P8 coaching positions", LEGEND_PSTRIP])
    img = np.concatenate([header, row], axis=0)
    return img, {"n_present": len(pos_by_p), "n_truth_hits": n_truth_hits}


# ---------------------------------------------------------------- strobe
def synth_or_fallback_series(data, strobe_synth_flag):
    synth = data["synth"]
    if synth:
        return synth, "synth"
    if strobe_synth_flag:
        meas = [s for s in data["samples"] if (int(s.get("flags", 0)) & SHAFT_MEASURED)]
        return meas, "track"
    return [], None


def build_strobe(data, strobe_synth_flag, synth_stride):
    name = data["name"]
    W, H = data["W"], data["H"]
    pos_by_p = positions_by_p(data["positions"])
    cap = cv2.VideoCapture(str(data["vpath"]))

    if 4 in pos_by_p:
        bg_t = int(pos_by_p[4]["t_us"])
    elif pos_by_p:
        mid = sorted(pos_by_p)[len(pos_by_p) // 2]
        bg_t = int(pos_by_p[mid]["t_us"])
    else:
        bg_t = int(data["frame_ts"][len(data["frame_ts"]) // 2])
    frame, _ = frame_at(cap, data["frame_ts"], bg_t)
    cap.release()
    img = frame.copy() if frame is not None else np.zeros((H, W, 3), dtype=np.uint8)

    dim_series, dim_kind = synth_or_fallback_series(data, strobe_synth_flag)
    stride = max(1, int(synth_stride))
    for i, s in enumerate(dim_series):
        if i % stride != 0:
            continue
        try:
            g, h = pos_line_px(s, W, H)
        except Exception:
            continue
        draw_line_alpha(img, g, h, COL_DIM, 1, 0.35)

    for p in sorted(pos_by_p):
        entry = pos_by_p[p]
        try:
            g, h = pos_line_px(entry, W, H)
        except Exception:
            continue
        col = source_color(entry)
        draw_line_alpha(img, g, h, col, 3, 1.0)
        put_text(img, f"P{p}", (int(h[0]) + 4, int(h[1]) - 4), scale=0.42, color=col)

    if img.shape[1] > STROBE_MAX_W:
        scale = STROBE_MAX_W / img.shape[1]
        img = resize_smart(img, (STROBE_MAX_W, int(round(img.shape[0] * scale))))

    header_lines = [f"{name} -- strobe (bg P4)", LEGEND_STROBE]
    if dim_kind == "track":
        header_lines.append("(synth[] absent -- dim layer = samples[] measured-tier fallback, "
                            "labelled 'track')")
    elif dim_kind is None:
        header_lines.append("(no synth[] -- pass --strobe-synth to draw the samples[] fallback)")
    header = make_header(img.shape[1], header_lines)
    out = np.concatenate([header, img], axis=0)
    return out, {"n_positions": len(pos_by_p), "dim_kind": dim_kind, "n_dim": len(dim_series)}


# ------------------------------------------------------------- per-swing
def process_swing(name, corpus_dir, run_dir, out_dir, strobe_synth_flag, synth_stride):
    data, err = load_swing_data(corpus_dir, run_dir, name)
    if err:
        print(f"[montage_positions] SKIP {err}")
        return None

    pstrip, pstats = build_pstrip(data)
    strobe, sstats = build_strobe(data, strobe_synth_flag, synth_stride)

    out_dir.mkdir(parents=True, exist_ok=True)
    p_path = out_dir / f"{name}_pstrip.png"
    s_path = out_dir / f"{name}_strobe.png"
    cv2.imwrite(str(p_path), pstrip)
    cv2.imwrite(str(s_path), strobe)
    print(f"[montage_positions] {name}: P {pstats['n_present']}/8 present, "
          f"{pstats['n_truth_hits']} truth hits -> {p_path.name}")
    print(f"[montage_positions] {name}: strobe {sstats['n_positions']} positions, "
          f"dim={sstats['dim_kind']} n={sstats['n_dim']} -> {s_path.name}")

    return dict(name=name, pstrip=pstrip, strobe=strobe, p_path=p_path, s_path=s_path,
                pstats=pstats, sstats=sstats)


# ------------------------------------------------------------- corpus grid
def pad_to_width(img, width, bg=(15, 15, 15)):
    if img.shape[1] == width:
        return img
    canvas = np.full((img.shape[0], width, 3), bg, dtype=np.uint8)
    canvas[:, :img.shape[1]] = img
    return canvas


def stack_rows_capped(rows, max_w=GRID_MAX_W):
    if not rows:
        return None
    width = max(r.shape[1] for r in rows)
    canvas = np.concatenate([pad_to_width(r, width) for r in rows], axis=0)
    if canvas.shape[1] > max_w:
        scale = max_w / canvas.shape[1]
        canvas = resize_smart(canvas, (max_w, int(round(canvas.shape[0] * scale))))
    return canvas


def grid_capped(tiles, max_w=GRID_MAX_W, bg=(12, 12, 12)):
    if not tiles:
        return None
    tw = max(t.shape[1] for t in tiles)
    th = max(t.shape[0] for t in tiles)
    cols = max(1, min(len(tiles), max_w // max(tw, 1)))
    rows_n = math.ceil(len(tiles) / cols)
    canvas = np.full((rows_n * th, cols * tw, 3), bg, dtype=np.uint8)
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        canvas[r * th:r * th + t.shape[0], c * tw:c * tw + t.shape[1]] = t
    if canvas.shape[1] > max_w:
        scale = max_w / canvas.shape[1]
        canvas = resize_smart(canvas, (max_w, int(round(canvas.shape[0] * scale))))
    return canvas


def discover_swings(corpus_dir):
    return sorted(d.name for d in corpus_dir.iterdir()
                 if d.is_dir() and (d / "swing.json").exists())


def run_montage(run_dir, corpus_dir, out_dir, swings=None, strobe_synth=False, synth_stride=6):
    run_dir, corpus_dir, out_dir = Path(run_dir), Path(corpus_dir), Path(out_dir)
    names = swings if swings else discover_swings(corpus_dir)
    if not names:
        print(f"[montage_positions] no swings found under {corpus_dir}")
        return []

    results = []
    for name in names:
        r = process_swing(name, corpus_dir, run_dir, out_dir, strobe_synth, synth_stride)
        if r:
            results.append(r)
    if not results:
        print("[montage_positions] no swings produced output -- nothing to montage")
        return []

    out_dir.mkdir(parents=True, exist_ok=True)

    row_width = results[0]["pstrip"].shape[1]
    p_header = make_header(row_width, [
        f"PinPoint Studio -- P-position corpus montage ({len(results)} swings)", LEGEND_PSTRIP])
    grid_p = stack_rows_capped([p_header] + [r["pstrip"] for r in results])
    cp_path = out_dir / "corpus_pstrips.png"
    cv2.imwrite(str(cp_path), grid_p)
    print(f"[montage_positions] wrote {cp_path}")

    grid_s = grid_capped([r["strobe"] for r in results])
    if grid_s is not None:
        s_header = make_header(grid_s.shape[1], [
            f"PinPoint Studio -- strobe corpus montage ({len(results)} swings)", LEGEND_STROBE])
        grid_s = np.concatenate([s_header, grid_s], axis=0)
    cs_path = out_dir / "corpus_strobes.png"
    cv2.imwrite(str(cs_path), grid_s)
    print(f"[montage_positions] wrote {cs_path}")

    return results


# =================================================================== selftest
def _swing_state(frac):
    """theta (rad, image atan2 convention), grip/head normalized [0,1]."""
    theta = math.radians(-130.0 + 260.0 * frac)
    grip = (0.55 - 0.05 * frac, 0.78 - 0.06 * frac)
    Lnorm = 0.30
    head = (grip[0] + Lnorm * math.cos(theta), grip[1] + Lnorm * math.sin(theta))
    return theta, grip, head


def _make_video(path, W, H, n_frames, fps):
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    vw = cv2.VideoWriter(str(path), fourcc, fps, (W, H))
    if not vw.isOpened():
        path = path.with_suffix(".avi")
        vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"XVID"), fps, (W, H))
    dt_us = int(round(1_000_000 / fps))
    frame_ts = []
    for i in range(n_frames):
        t_us = i * dt_us
        frame_ts.append(t_us)
        img = np.full((H, W, 3), 30, dtype=np.uint8)
        cv2.putText(img, f"f{i}", (8, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (90, 90, 90), 1, cv2.LINE_AA)
        frac = i / max(1, n_frames - 1)
        _, grip, head = _swing_state(frac)
        gpx, hpx = (grip[0] * W, grip[1] * H), (head[0] * W, head[1] * H)
        cv2.line(img, (int(gpx[0]), int(gpx[1])), (int(hpx[0]), int(hpx[1])), (255, 255, 255), 2, cv2.LINE_AA)
        vw.write(img)
    vw.release()
    return path, frame_ts, dt_us


def fabricate_swing(root, name, W=480, H=270, fps=30, n_frames=40,
                    with_synth=True, with_truth=True, p_list=range(1, 9)):
    import json
    swing_dir = root / "corpus" / name
    run_dir = root / "run" / name
    swing_dir.mkdir(parents=True, exist_ok=True)
    run_dir.mkdir(parents=True, exist_ok=True)

    vpath, frame_ts, dt_us = _make_video(swing_dir / "swing.mp4", W, H, n_frames, fps)
    span_us = (n_frames - 1) * dt_us

    samples = []
    for i in range(n_frames):
        frac = i / max(1, n_frames - 1)
        theta, grip, head = _swing_state(frac)
        flags = SHAFT_MEASURED if (i % 5 != 0) else 0x04  # mostly measured, a few "coasted"
        samples.append({"t_us": i * dt_us, "grip": list(grip), "head": list(head),
                        "theta": theta, "conf": 0.7 + 0.2 * math.sin(frac * 3.1), "flags": flags})

    p_fracs = {1: 0.00, 2: 0.10, 3: 0.28, 4: 0.42, 5: 0.50, 6: 0.58, 7: 0.72, 8: 0.92}
    positions = []
    for p in p_list:
        frac = p_fracs[p]
        theta, grip, head = _swing_state(frac)
        t_us = int(round(frac * span_us))
        is_fit = p in (3, 4, 5, 6)
        positions.append({
            "p": p, "t_us": t_us, "grip": list(grip), "head": list(head), "theta": theta,
            "lenPx": 0.30 * H, "conf": 0.65 + 0.03 * p,
            "sigmaThetaDeg": (1.5 + 0.2 * p) if is_fit else -1.0,
            "sigmaLenPx": (3.0 + p) if is_fit else -1.0,
            "stackN": (5 + p) if is_fit else 0,
            "source": SRC_MILESTONEFIT if is_fit else SRC_TRACKSAMPLE,
        })

    club = {"frameWidth": W, "frameHeight": H, "samples": samples, "positions": positions}
    if with_synth:
        n_synth = n_frames * 3
        synth = []
        for i in range(n_synth):
            frac = i / max(1, n_synth - 1)
            theta, grip, head = _swing_state(frac)
            synth.append({"t_us": int(round(frac * span_us)), "grip": list(grip),
                         "head": list(head), "theta": theta, "conf": 0.5, "flags": 0})
        club["synth"] = synth

    swing_json = {
        "streams": [{
            "kind": "video", "alias": "FaceOn", "file": vpath.name,
            "setup": {"perspective": 2},
            "encoded": {"width": W, "height": H},
            "frames": {"t_us": frame_ts},
        }],
        "capture": {"sessionType": 1},
        "analysis": {},
    }
    (swing_dir / "swing.json").write_text(json.dumps(swing_json, indent=1), encoding="utf-8")

    if with_truth:
        truth_frames = []
        for p, frac in p_fracs.items():
            if p not in (2, 4, 6, 8) or p not in p_list:
                continue
            theta, grip, head = _swing_state(frac)
            t_us = int(round(frac * span_us)) + 4000   # 4ms jitter, within the 40ms tolerance
            truth_frames.append({"t_us": t_us, "theta": theta,
                                 "grip": [grip[0] * W, grip[1] * H],
                                 "head": [head[0] * W, head[1] * H]})
        (swing_dir / "truth.json").write_text(
            json.dumps({"shaft": truth_frames, "events": {}}, indent=1), encoding="utf-8")

    (run_dir / "result.json").write_text(
        json.dumps({"analysis": {"club": club}}, indent=1), encoding="utf-8")
    (run_dir / "runmeta.json").write_text(
        json.dumps({"ok": True, "frames": "mp4"}, indent=1), encoding="utf-8")
    return swing_dir, run_dir


def selftest(tmpdir):
    tmpdir = Path(tmpdir)
    if tmpdir.exists():
        shutil.rmtree(tmpdir)
    tmpdir.mkdir(parents=True)
    print(f"[selftest] fabricating fixtures under {tmpdir}")

    # synth_swing: full P1..P8, synth[] present, truth.json present (partial coverage).
    fabricate_swing(tmpdir, "synth_swing", with_synth=True, with_truth=True, p_list=range(1, 9))
    # track_swing: only P1..P6 (P7/P8 missing), no synth[], no truth.json -- exercises the
    # missing-P tile, the samples[] fallback dim layer, and the no-truth path.
    fabricate_swing(tmpdir, "track_swing", with_synth=False, with_truth=False, p_list=range(1, 7))

    out_dir = tmpdir / "out"
    results = run_montage(tmpdir / "run", tmpdir / "corpus", out_dir,
                          strobe_synth=True, synth_stride=5)

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(f"[selftest] {'PASS' if cond else 'FAIL'}: {msg}")
        if not cond:
            ok = False

    check(len(results) == 2, f"both swings produced results (got {len(results)})")
    by_name = {r["name"]: r for r in results}

    for nm in ("synth_swing", "track_swing"):
        r = by_name.get(nm)
        check(r is not None, f"{nm}: present in results")
        if not r:
            continue
        for key in ("p_path", "s_path"):
            p = r[key]
            exists = p.exists()
            check(exists, f"{nm}: {p.name} exists")
            sz = p.stat().st_size if exists else 0
            check(sz > 20_000, f"{nm}: {p.name} size {sz}B > 20KB")
        check(r["pstrip"].shape[1] == 8 * TILE,
              f"{nm}: pstrip width {r['pstrip'].shape[1]} == {8 * TILE}")
        check(r["pstrip"].shape[0] > TILE, f"{nm}: pstrip height {r['pstrip'].shape[0]} > tile size")

    sw = by_name.get("synth_swing")
    if sw:
        check(sw["pstats"]["n_present"] == 8, "synth_swing: all 8 P present")
        check(sw["pstats"]["n_truth_hits"] > 0, "synth_swing: truth overlay matched >=1 position")
        check(sw["sstats"]["dim_kind"] == "synth", "synth_swing: strobe used the synth[] dim layer")
        check(sw["sstats"]["n_positions"] == 8, "synth_swing: strobe drew 8 bright positions")

    tw = by_name.get("track_swing")
    if tw:
        check(tw["pstats"]["n_present"] == 6, "track_swing: 6/8 P present (P7/P8 missing)")
        check(tw["pstats"]["n_truth_hits"] == 0, "track_swing: no truth.json -> 0 truth hits")
        check(tw["sstats"]["dim_kind"] == "track",
              "track_swing: strobe fell back to the samples[] 'track' dim layer")

    for fname in ("corpus_pstrips.png", "corpus_strobes.png"):
        p = out_dir / fname
        exists = p.exists()
        check(exists, f"corpus: {fname} exists")
        sz = p.stat().st_size if exists else 0
        check(sz > 20_000, f"corpus: {fname} size {sz}B > 20KB")

    print(f"[selftest] {'ALL PASS' if ok else 'SOME CHECKS FAILED'}")
    return ok


# --------------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(prog="montage_positions.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", nargs="?", help="dir with one subdir per swing (result.json + runmeta.json)")
    ap.add_argument("corpus_dir", nargs="?", help="dir with one subdir per swing (swing.json [+truth.json])")
    ap.add_argument("--out", default=None, help="output dir for the PNGs")
    ap.add_argument("--swings", default=None, help="comma-separated swing names (default: every swing in corpus_dir)")
    ap.add_argument("--strobe-synth", action="store_true",
                    help="when synth[] is absent, fall back to drawing samples[] measured-tier "
                         "states dim in the strobe tile (labelled 'track')")
    ap.add_argument("--synth-stride", type=int, default=6,
                    help="draw every Nth synth/track sample in the strobe dim layer (default 6)")
    ap.add_argument("--selftest", default=None, metavar="TMPDIR",
                    help="fabricate a synthetic corpus+run under TMPDIR, run the full pipeline, "
                         "assert the outputs, and exit (ignores run_dir/corpus_dir/--out)")
    args = ap.parse_args()

    if args.selftest:
        sys.exit(0 if selftest(args.selftest) else 1)

    if not args.run_dir or not args.corpus_dir or not args.out:
        ap.error("run_dir, corpus_dir, and --out are required (unless --selftest TMPDIR is given)")

    swings = [s.strip() for s in args.swings.split(",") if s.strip()] if args.swings else None
    results = run_montage(args.run_dir, args.corpus_dir, args.out, swings=swings,
                          strobe_synth=args.strobe_synth, synth_stride=args.synth_stride)
    sys.exit(0 if results else 1)


if __name__ == "__main__":
    main()
