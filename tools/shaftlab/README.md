# shaftlab — shaft + clubhead detection exemplar (Python)

The **validated reference implementation** for automated club markup, built
lab-first with frame-by-frame visual adjudication. It must be proven here
(visually + numerically) before any C++ port goes near the app — the one
C++ port attempted without that gate produced confidently-wrong markups and
was reverted (template preserved in `.claude/attic/auto-markup-2026-07-02/`).

Two stages, decoupled by the **shaft-track contract v1** (the only stage-1
output stage 2 may read: `frame, grip_x, grip_y, theta_out, kind, conf`):

```
video ─► prep_swing.py ─► clip + anchors.csv + skeleton.csv + clipmeta.json
             │
   STAGE 1   ▼
  shaft_annotate.py        theta(t): detection + KF/RTS + meas/pred tiers
             │  *_track.csv (contract v1)
   STAGE 2   ▼
  clubhead_annotate.py     r_h(t): head along the ray + length model +
             │             KF/RTS + meas/pred/off tiers
             ▼
  render_combined.py       shaft + head overlay video for eyeballing
```

## Files

| file | role |
|---|---|
| `prep_swing.py` | exported swing dir → pose-covered face-on clip + grip/forearm anchors + 8-joint skeleton + clipmeta (true fps — container fps is fake). Captures with a raw Bayer sidecar decode from `.raw` (EA demosaic, app-matching) into an FFV1-lossless clip; `--mp4` / `--bilinear` override for A/B. Embedded exposure lands in clipmeta `exposure_s` |
| `shaft_annotate.py` | **stage 1** (v6 working prototype): shaft angle detection/tracking, F1–F17 fix set |
| `length_model.py` | projected club-length model M0–M4; labeled fit + per-swing censored self-fit (production path); phase split; the geometry primer for the port lives in its docstring |
| `clubhead_scan.py` | stage-2 scan primitives (scene median, ray edge, run-end) + the H0 zeroth-order baseline tool |
| `clubhead_measure.py` | stage-2 per-frame head measurement: gap-tolerant on-axis terminus, multi-width edge-pair, permanence veto, length-prior candidate scoring |
| `clubhead_annotate.py` | **stage 2, the main tool**: arm-length plausibility floor, segmented 1-D KF + per-segment RTS, meas/pred/off tiers, flip check |
| `score_truth.py` | numeric eval vs hand labels (`truth.json`): theta by kind; `--head` adds head px / length error + conf-honesty clauses |
| `make_synth.py` | synthetic swing (theta + foreshortening profile + head blob + streak) with truth.json + contract-track output — the machinery accuracy gate |
| `montage.py` | tile annotated frames for visual triage (zoom full-res before concluding anything) |
| `render_combined.py` | stage-1 line + stage-2 head marker in one review video |

## Workflow

```bash
P=/home/markl/venv/pinpoint/bin/python3        # numpy+opencv+scipy venv
$P prep_swing.py /mnt/swingdata/.../swing_0009 /tmp/s9      # prints true fps
$P shaft_annotate.py /tmp/s9/faceon_swing.mp4 --anchors /tmp/s9/anchors.csv \
    --fps-override <fps> --out-dir /tmp/s9/out
$P clubhead_annotate.py /tmp/s9/faceon_swing.mp4 \
    --track /tmp/s9/out/faceon_swing_track.csv --fps-override <fps> \
    --out-dir /tmp/s9/head            # skeleton.csv auto-found next to clip
$P score_truth.py /tmp/s9/out/faceon_swing_track.csv /tmp/s9/clipmeta.json \
    --head /tmp/s9/head/faceon_swing_head.csv
$P render_combined.py /tmp/s9/faceon_swing.mp4 \
    /tmp/s9/out/faceon_swing_track.csv /tmp/s9/head/faceon_swing_head.csv \
    /tmp/s9/combined.mp4
```

Synthetic self-check (no captures needed; hard accuracy gate):

```bash
$P make_synth.py --out-dir /tmp/synth
$P clubhead_annotate.py /tmp/synth/swing_synth.mp4 --track /tmp/synth/synth_track.csv \
    --fps-override 120 --lat-max 12 --out-dir /tmp/synth/head
$P score_truth.py /tmp/synth/synth_track.csv /tmp/synth/clipmeta.json \
    --head /tmp/synth/head/swing_synth_head.csv
```

## Ground rules (non-negotiable, learned the hard way)

- **Decoupling**: stage 2 reads contract v1 + video + clipmeta + skeleton.csv
  (an independent input) — never stage-1 internals. Develop against frozen
  stage-1 CSVs.
- **Honesty**: measured/predicted/off tiers; low-confidence detections are
  discarded, never emitted; confidence must correlate with error (clauses:
  ≥⅔ of bad frames low-conf, ≤5% of high-conf frames bad).
- **Verification**: mean/p90/%bad + per-phase coverage, never medians or
  totals alone; full-res zooms before concluding anything from montage tiles;
  single-swing residuals never select a model form (corpus-gated —
  `docs/validation/pipeline_validation_and_tuning.md` §5.5).
- **Determinism**: same inputs → byte-identical CSVs (the regression and
  C++-port oracle contract).

## Docs

Design: `docs/design/shaft_detection_improvements.md` (stage 1),
`docs/design/clubhead_detection_design.md` (stage 2),
`docs/design/shaft_detection_exemplar_findings.md` (F1–F17 + methodology).
Implementation/usage: `docs/implementation/shaft_markup_exemplar_impl.md`,
`docs/implementation/clubhead_exemplar_plan.md` (H0–H4, as-built notes).
Iteration history + per-swing outputs: `/home/markl/shaft_markup_lab/`
(scratch; H0/H2 baselines in `s8v2/h0/BASELINE.md`, `H0_CORPUS.md`).
