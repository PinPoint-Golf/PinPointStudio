# Shaft Markup Exemplar — Implementation & Usage (stage 1 v6 + stage 2 H2)

The Python exemplar in **`tools/shaftlab/`** (renamed from `tools/markup/`,
2026-07-04) is the working reference for automated club markup: it must be
proven here (visually + numerically) before any C++ port goes near the app.
Design rationale and the F1–F17 fix table:
[../design/shaft_detection_exemplar_findings.md](../design/shaft_detection_exemplar_findings.md);
original architecture: [../design/shaft_detection_improvements.md](../design/shaft_detection_improvements.md);
stage 2 (clubhead): [../design/clubhead_detection_design.md](../design/clubhead_detection_design.md)
+ [clubhead_exemplar_plan.md](clubhead_exemplar_plan.md) (H0–H2 as-built).
The folder's own `README.md` carries the same map + workflow.

## Files (`tools/shaftlab/`)

| file | purpose |
|------|---------|
| `shaft_annotate.py` | **stage 1** (v6 working prototype): detection + tracking, per-segment KF/RTS, still-hold re-acquisition, body gating, measured/predicted output; annotated video + track CSV |
| `length_model.py` | **stage 2**: projected club-length model M0–M4 (labeled fit + per-swing censored self-fit = production path); theta re-unwrap + ω-anchored phase split |
| `clubhead_scan.py` | **stage 2** scan primitives (scene median, ray edge, run-end) + H0 zeroth-order baseline tool (lab head_v0) |
| `clubhead_measure.py` | **stage 2** per-frame head measurement: gap-tolerant on-axis terminus, multi-width edge-pair, permanence veto, length-prior candidate scoring (lab head_v1) |
| `clubhead_annotate.py` | **stage 2 main tool**: arm-length plausibility floor, segmented 1-D KF + per-segment RTS, meas/pred/off tiers, 180° flip check (lab head_v2) |
| `render_combined.py` | stage-1 line + stage-2 head marker in one review video |
| `prep_swing.py` | exported swing dir → pose-covered Face-On clip + `anchors.csv` (grip + lead-forearm φ) + `skeleton.csv` (8 body joints px; body gate + arm floor) + `clipmeta.json`. Container written at integer fps (mpeg4 rejects some fractional rates); true fps in clipmeta |
| `montage.py` | tile annotated frames for visual review (uniform 24, or explicit frame list) |
| `score_truth.py` | numeric eval vs a swing's hand-labelled `truth.json`, split by output kind; `--head <csv>` adds head-px/length error + conf-honesty clauses |
| `make_synth.py` | synthetic swing generator: theta + foreshortening profile + head blob/streak; writes truth.json + a contract-format track so stage 2 runs standalone |

Environment: Python ≥3.10 venv with `numpy`, `opencv-python-headless`;
stage 2 additionally needs `scipy` (`/home/markl/venv/pinpoint`).

## Workflow

```bash
python tools/shaftlab/prep_swing.py /mnt/swingdata/.../swing_0009 /tmp/s9
# prints clip frame count + fps and writes faceon_swing.mp4, anchors.csv,
# skeleton.csv, clipmeta.json

python tools/shaftlab/shaft_annotate.py /tmp/s9/faceon_swing.mp4 \
    --anchors /tmp/s9/anchors.csv --fps-override <fps from prep output> \
    --out-dir /tmp/s9/out [--debug-frames 186,310]

python tools/shaftlab/clubhead_annotate.py /tmp/s9/faceon_swing.mp4 \
    --track /tmp/s9/out/faceon_swing_track.csv --fps-override <fps> \
    --out-dir /tmp/s9/head        # skeleton.csv auto-found next to the clip

python tools/shaftlab/montage.py /tmp/s9/out/faceon_swing_annotated.mp4 /tmp/s9/out/mont
python tools/shaftlab/score_truth.py /tmp/s9/out/faceon_swing_track.csv /tmp/s9/clipmeta.json \
    --head /tmp/s9/head/faceon_swing_head.csv
python tools/shaftlab/render_combined.py /tmp/s9/faceon_swing.mp4 \
    /tmp/s9/out/faceon_swing_track.csv /tmp/s9/head/faceon_swing_head.csv /tmp/s9/combined.mp4
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
- **Stage 2 (clubhead) H0–H2 built + gated 2026-07-03/04** — length model,
  gap-tolerant terminus measurement, segmented KF/RTS + meas/pred/off tiers,
  arm-length plausibility floor. 0008 head meas median 19.0 px with the
  honesty clauses passing; per-phase numbers + known limits in the plan doc's
  as-built sections and `shaft_markup_lab/{s8v2/h0/BASELINE.md, H0_CORPUS.md}`.
  H3 (hard phases: impact streak, wraparound occlusion, body gate, unlit
  finish — down/thru meas coverage) and H4 (corpus freeze + decoupling test)
  remain.
- **Capture guidance** (from corpus eyeballing): face-on framing should leave
  more room to the player's right — most post-impact loss is the club leaving
  the frame, not detection failure.
- **Stage-1 backlog surfaced by stage 2** (adjudicated frames, s9v2 clip
  indices): f190 follow-through body-line lock at conf 0.90; f302–320 hang
  region θ≈92.5° conf 0.92 vs visible ~60°; plus the known 0008 f220–227
  fast-follow-through cluster (findings §7).
- Next, in order (details in findings doc §7 + clubhead plan):
  1. **Capture uncropped swings** (face-on framing with more room to the
     player's right) — the prototype's post-impact ceiling is the crop, not the
     detector.
  2. Stage-2 H3 (hard phases) → H4 (corpus + freeze).
  3. Stage-1: conf shaping for short fast-motion re-init segments (0008
     f220–227) + the s9v2 backlog items above; finish-segment zoom audit
     across 0003–0007.
  4. C++ re-port (both stages, same CSV-shaped contract between them): the
     reverted first-generation port + full app wiring diff are preserved in
     `.claude/attic/auto-markup-2026-07-02/` (driver, truth.json schema
     additions, MarkupController async pattern, QML). Port the exemplar onto
     that template; gate with the same protocol plus the honesty checks. The
     stage-2 modules carry C++ PORT NOTES in their docstrings (written while
     the design decisions were fresh — read them first).
  5. App policy when wired in: write only `kind=meas` frames as labels;
     optionally render `kind=pred` as a distinct visual layer, never as truth.
