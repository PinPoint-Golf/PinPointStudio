# Shaft Markup Exemplar — Implementation & Usage (v4, frozen 2026-07-02)

The Python exemplar in `tools/markup/` is the working reference for automated
shaft markup: it must be proven here (visually + numerically) before any C++
port goes near the app. Design rationale and the F1–F9 fix table:
[../design/shaft_detection_exemplar_findings.md](../design/shaft_detection_exemplar_findings.md);
original architecture: [../design/shaft_detection_improvements.md](../design/shaft_detection_improvements.md).

## Files (`tools/markup/`)

| file | purpose |
|------|---------|
| `shaft_annotate.py` | the exemplar (v4): detection + tracking + measured/predicted output; annotated video + track CSV |
| `prep_swing.py` | exported swing dir → pose-covered Face-On clip + `anchors.csv` (grip + lead-forearm φ per frame) + `clipmeta.json` |
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

## Status & next steps

- **Frozen at v4** (2026-07-02). Historical iterations (v1 baseline → v2 → v3 →
  v4 with per-version outputs on both swings) live in the scratch lab
  `/home/markl/shaft_markup_lab/` (not part of the repo).
- Next, in order:
  1. Finish-hold detection (static over-shoulder club — see findings doc §5).
  2. Corpus sweep `swing_0001–0007` with the verification protocol above.
  3. C++ re-port: the reverted first-generation port + full app wiring diff are
     preserved in `.claude/attic/auto-markup-2026-07-02/` (driver, truth.json
     schema additions, MarkupController async pattern, QML). Port the v2→v4
     algorithm deltas onto that template; gate with the same protocol plus the
     honesty checks (bad frames low-conf; high-conf frames accurate).
  4. App policy when wired in: write only `kind=meas` frames as labels;
     optionally render `kind=pred` as a distinct visual layer, never as truth.
