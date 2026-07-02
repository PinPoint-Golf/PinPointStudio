# Shaft Markup Exemplar — Implementation & Usage (v4, frozen 2026-07-02)

The Python exemplar in `tools/markup/` is the working reference for automated
shaft markup: it must be proven here (visually + numerically) before any C++
port goes near the app. Design rationale and the F1–F9 fix table:
[../design/shaft_detection_exemplar_findings.md](../design/shaft_detection_exemplar_findings.md);
original architecture: [../design/shaft_detection_improvements.md](../design/shaft_detection_improvements.md).

## Files (`tools/markup/`)

| file | purpose |
|------|---------|
| `shaft_annotate.py` | the exemplar (v4, frozen): detection + tracking + measured/predicted output; annotated video + track CSV |
| `shaft_annotate_v5.py` | **work-in-progress checkpoint** (finish-hold F10–F14, corpus-swept) — better top-of-swing + finish, known impact-window gap vs v4; see findings doc §6.4 before promoting |
| `prep_swing.py` | exported swing dir → pose-covered Face-On clip + `anchors.csv` (grip + lead-forearm φ per frame) + `clipmeta.json`. Container written at integer fps (mpeg4 rejects some fractional rates); true fps in clipmeta |
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

- **v4 frozen** (2026-07-02) = `shaft_annotate.py`; **v5 checkpointed**
  (2026-07-03) = `shaft_annotate_v5.py` — finish-hold detection (F10–F14) +
  corpus sweep 0002–0007 (0001 has no recorded pose and cannot be processed).
  v5 results: 0009 44→65% measured (finish 4→49%), 0002 65→71% (finish 19→37%),
  0003–0007 unchanged within noise; 0008 measured-tier guard at its best
  (median 2.8°, mean 3.3°, 0% frames >30° wrong). Historical iterations and
  per-version outputs live in the scratch lab `/home/markl/shaft_markup_lab/`.
- **Capture guidance** (from corpus eyeballing): face-on framing should leave
  more room to the player's right — most post-impact loss is the club leaving
  the frame, not detection failure.
- Next, in order (details in findings doc §6.4):
  1. **Per-segment RTS smoothing** (structural — smoother must not run across
     re-init discontinuities), then re-add the speed-aware coast budget to
     recover the impact window (v5's one regression vs v4).
  2. Restore the 0009 f460 finish detection (permanence-veto threshold vs the
     median scene snapshot).
  3. Pose-derived body mask in the evidence (kills the f700 body/shoes class);
     prep must export the skeleton per frame.
  4. Finish measured-segment audit across 0002–0007; re-run the 0008 guard;
     then promote v5 → `shaft_annotate.py`.
  5. C++ re-port: the reverted first-generation port + full app wiring diff are
     preserved in `.claude/attic/auto-markup-2026-07-02/` (driver, truth.json
     schema additions, MarkupController async pattern, QML). Port the final
     algorithm onto that template; gate with the same protocol plus the
     honesty checks (bad frames low-conf; high-conf frames accurate).
  6. App policy when wired in: write only `kind=meas` frames as labels;
     optionally render `kind=pred` as a distinct visual layer, never as truth.
