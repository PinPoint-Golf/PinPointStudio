# Shaft Markup Exemplar — Implementation & Usage (v6 working prototype)

The Python exemplar in `tools/markup/` is the working reference for automated
shaft markup: it must be proven here (visually + numerically) before any C++
port goes near the app. Design rationale and the F1–F9 fix table:
[../design/shaft_detection_exemplar_findings.md](../design/shaft_detection_exemplar_findings.md);
original architecture: [../design/shaft_detection_improvements.md](../design/shaft_detection_improvements.md).

## Files (`tools/markup/`)

| file | purpose |
|------|---------|
| `shaft_annotate.py` | the exemplar (v6 working prototype): detection + tracking, per-segment KF/RTS, still-hold re-acquisition, body gating, measured/predicted output; annotated video + track CSV |
| `prep_swing.py` | exported swing dir → pose-covered Face-On clip + `anchors.csv` (grip + lead-forearm φ) + `skeleton.csv` (8 body joints px, for the body-collinearity gate) + `clipmeta.json`. Container written at integer fps (mpeg4 rejects some fractional rates); true fps in clipmeta |
| `montage.py` | tile annotated frames for visual review (uniform 24, or explicit frame list) |
| `score_truth.py` | numeric eval vs a swing's hand-labelled `truth.json`, split by output kind (meas/pred) |
| `make_synth.py` | synthetic swing generator (design §12.1) |

Environment: any Python ≥3.10 venv with `numpy` and `opencv-python-headless`.

## Workflow

```bash
python tools/markup/prep_swing.py /mnt/swingdata/.../swing_0009 /tmp/s9
# prints clip frame count + fps and writes faceon_swing.mp4, anchors.csv, clipmeta.json

python tools/markup/shaft_annotate.py /tmp/s9/faceon_swing.mp4 \
    --anchors /tmp/s9/anchors.csv --fps-override <fps from prep output> \
    --out-dir /tmp/s9/out [--debug-frames 186,310]

python tools/markup/montage.py /tmp/s9/out/faceon_swing_annotated.mp4 /tmp/s9/out/mont
python tools/markup/score_truth.py /tmp/s9/out/faceon_swing_track.csv /tmp/s9/clipmeta.json
```

Key facts:
- **Container fps is fake** (30fps tag); real timing is `streams[].frames.t_us`
  (~149 fps). `prep_swing.py` computes the true rate — always pass it via
  `--fps-override`.
- The clip starts at **pose coverage**, not video start; clip frame `f` ↔
  window-relative `clipmeta.t_us[f]`. `truth.json` `t_us` is in the same
  window-relative domain (`score_truth.py` handles the mapping).
- Anchors come from `analysis.pose2d` (lead/trail hand midpoint, interpolated to
  every frame); φ is the lead elbow→grip image angle (handedness-aware).

## Output contract (`*_track.csv`)

Per frame: `theta_filt, omega_filt, theta_smooth, omega_smooth, conf, S_peak,
support, near, band_deg, flag, grip_x, grip_y, kind, theta_out`.

Consumers use **`theta_out` + `kind`**:
- `kind=meas` — confident detection (conf ≥0.5 in a run of ≥4 frames). These are
  label-grade.
- `kind=pred` — kinematic prediction (smoother through clean gaps, hermite/decay
  across track breaks). Clearly-marked second tier; low-confidence detections
  are **discarded**, never emitted as measurements.

Overlay video: red = measured, cyan = predicted, orange = wedge blur edges,
blue dot = grip anchor; HUD shows θ/ω/conf/kind/flag.

## Verification protocol (used at the freeze; repeat after any change)

1. **Numeric** (`swing_0008`, 51 hand labels): `score_truth.py` — measured tier
   must hold ~median ≤3°, mean ≤11°, %>30° ≤5–10%; watch the meas/pred split
   sizes. Freeze values: meas n=39 median 2.9° mean 10.2° p90 7.1° bad 5%.
2. **Visual** (`swing_0009` — the designated hard example): montage triage, then
   **full-res zooms of any suspect frame** before concluding (small tiles have
   caused misdiagnoses). Requirement: zero confident-wrong frames; predictions
   through post-impact should lie plausibly on/near the club.
3. `--debug-frames <ids>` prints top-6 peaks with every gate value per frame —
   diagnose gate failures with data before tuning constants.
4. **Check per-phase coverage, not just totals** (address / backswing /
   downswing / impact / finish): aggregate measured% once masked the loss of the
   entire downswing while the finish improved.

## Status & next steps

- **v6 working prototype** (2026-07-03) = `shaft_annotate.py` — per-segment
  RTS + speed-aware coast (impact window recovered), still-hold re-acquisition,
  body-collinearity gating with clear-candidate preference (findings §6–§7).
  Corpus (meas%/finish%): 0002 71/38, 0003 81/49, 0004 75/27, 0005 73/32,
  0006 75/41, 0007 66/52, 0009 71/63 (0001 has no recorded pose). 0008 guard:
  median 2.7°, with a known 3-frame confident-wrong cluster in the fast
  follow-through (findings §7). v4/v5 in git history; per-version outputs in
  the scratch lab `/home/markl/shaft_markup_lab/`.
- **Capture guidance** (from corpus eyeballing): face-on framing should leave
  more room to the player's right — most post-impact loss is the club leaving
  the frame, not detection failure.
- Next, in order (details in findings doc §7):
  1. **Capture uncropped swings** (face-on framing with more room to the
     player's right) — the prototype's post-impact ceiling is the crop, not the
     detector.
  2. Conf shaping for short fast-motion re-init segments (the 0008 f220–227
     confident-wrong cluster) without killing correct short impact re-locks.
  3. Full finish-segment zoom audit across 0003–0007.
  4. C++ re-port: the reverted first-generation port + full app wiring diff are
     preserved in `.claude/attic/auto-markup-2026-07-02/` (driver, truth.json
     schema additions, MarkupController async pattern, QML). Port the final
     algorithm onto that template; gate with the same protocol plus the
     honesty checks (bad frames low-conf; high-conf frames accurate).
  5. App policy when wired in: write only `kind=meas` frames as labels;
     optionally render `kind=pred` as a distinct visual layer, never as truth.
