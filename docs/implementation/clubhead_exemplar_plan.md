# Clubhead Detection Exemplar — Implementation Plan (Stage 2)

**Status:** Plan (2026-07-03), ready to execute. Design:
[../design/clubhead_detection_design.md](../design/clubhead_detection_design.md).
Method: identical to the shaft exemplar loop — build in the lab, diagnose with
data (`--debug-frames`-style dumps), verify with eyes at full resolution,
gate numerically on 0008, sweep the corpus, freeze, then promote to
`tools/markup/`.

## Ground rules (inherited, non-negotiable)

- **Decoupling**: stage 2 reads ONLY the shaft-track contract v1 columns
  (`frame, grip_x, grip_y, theta_out, kind, conf`) + video + clipmeta.
  Develop against **frozen** stage-1 CSVs (`lab/s*/v6/faceon_swing_track.csv`).
- **Honesty**: measured/predicted/off tiers; low-confidence detections are
  discarded, never emitted; confidence must correlate with error.
- **Verification**: mean/p90/%bad + per-phase coverage (never medians or
  totals alone); full-res zooms before concluding anything from montage tiles.
- **Per-segment smoothing** from day one; segments split on stage-1 θ jumps.

## Deliverable

`tools/markup/clubhead_annotate.py`:

```
clubhead_annotate.py <clip.mp4> --track <stage1_track.csv> \
    --clipmeta <clipmeta.json> [--out-dir out] [--debug-frames …]
→  <stem>_head.csv          frame, r_h, head_x, head_y, conf_h, kind_h, channels
→  <stem>_head_annotated.mp4   stage-1 lines + head marker (○ meas, ◌ pred, ✕ off)
```

Plus: `score_truth.py` gains a `--head` mode (0008 hand labels carry head px);
`make_synth.py` gains a head blob + head ground truth.

## Phases

### H0 — harness + baseline (half a session)
- Skeleton `clubhead_annotate.py`: contract reader, band sampler, overlay,
  CSV writer. No detection yet: emit the **zeroth-order head** =
  `grip + L_vis·dir(θ)` from a *recomputed* run-end (stage 2's own edge-pair
  termination scan — NOT stage-1's internal columns).
- Extend `score_truth.py` for head-px error vs 0008 hand labels; record the
  baseline table (this is the number every later phase must beat).
- Extend `make_synth.py` with a head blob + truth; baseline it too.
- **Gate:** harness runs end-to-end on 0008 + 0009; baseline numbers recorded.

### H1 — radial evidence + per-frame measurement (1 session)
- Implement the four channels (`E_chg`, `E_mot`, `E_end`, `E_wide`), the
  combination, peak + parabolic refinement, lateral centroid, plausibility
  annulus, off-frame declaration.
- `--debug-frames` dump: per-radius channel profiles for named frames.
- **Adjudicate the head-point convention** early: full-res zooms of ~6 hand-
  labelled 0008 frames — does the label sit at hosel, centroid, or visual tip?
  Match it (this decides the lateral-centroid weighting).
- **Gate:** visual — address, backswing, top on 0009 correct at full-res;
  numeric — beats H0 baseline on 0008 measured-tier median.

### H2 — temporal model + output tiers (1 session)
- 1-D KF `[r_h, ṙ_h]` + 3σ gate + speed-aware coast + per-segment RTS
  (segments split on stage-1 re-inits); measured/predicted/off tiers;
  conf from posterior σ_r with the stage-1 conf-shaping playbook
  (unconfirmed re-acquisitions capped, ω-consistency cross-check).
- **Gate:** 0008 head error meas-tier median < 25 px (score.py's bar) with
  honesty clauses (≥⅔ of bad frames low-conf; ≤5% of high-conf frames bad);
  `track.head_step` smoothness passes.

### H3 — hard phases (1 session, data-led)
Work the failure-mode table with the debug dump before touching thresholds:
- impact streak (radial peak stays valid through the smear?),
- occlusion during wraparound (coast + honest pred),
- ball suppression at address (change channel — verify, don't assume),
- unlit finish head (0009 f373–f460 region — the case that killed luma gates),
- stage-1 pred-tier frames (wedge widening + prominence).
- **Gate:** zero confident-wrong head positions across the adjudicated
  landmark set (build it as we go, like the shaft landmark list).

### H4 — corpus + freeze (half a session)
- Sweep 0002–0007 + 0009; per-phase coverage table; full-res spot audit.
- **Decoupling test**: rerun stage 2 against two different frozen stage-1
  CSVs of the same swing (v5n vs v6) — stage-2 code paths must not care;
  document the delta as stage-1-attributable.
- Freeze, promote to `tools/markup/`, update the three shaft docs' pointers +
  a findings section for stage 2 (fix ids continue: H-series).

## Acceptance targets (v1 freeze)

| metric | target |
|---|---|
| 0008 head error, meas tier | median < 25 px (score.py gate), p90 < 60 px |
| honesty | ≥⅔ of >40 px frames low-conf; ≤5% of high-conf frames >40 px |
| synthetic | r_h RMSE < 4 px @ ≤40% dropout; zero false heads on clean clips |
| corpus | no confident-wrong landmarks on 0002/0009; per-phase coverage reported |
| decoupling | byte-identical stage-2 output for identical contract input; graceful degradation across v5n→v6 stage-1 swap |

## Open questions for the working session

1. **Head-point convention** — hosel vs centroid vs visual tip; adjudicate on
   0008 zooms first (H1). The hand labels are the contract.
2. **Impact-streak semantics** — at 150 fps the head moves 20–30 cm/frame near
   impact: is the "position" the mid-exposure point (streak centroid) or the
   trailing edge? Mid-exposure matches the shaft θ convention — confirm on
   zooms.
3. **Should `off` frames emit a clamped ray-edge point** for the overlay only
   (visual continuity) while the CSV stays empty? Leaning yes (overlay-only).
4. **skeleton.csv as optional occlusion prior** — use only if H3 shows body
   occlusion produces false positives; keep it out otherwise (fewer inputs =
   stronger decoupling).
5. Exposure metadata (unlocks stage-1 direct-ω too) — chase in the capture
   pipeline this round or next?

## What this is NOT (v1)

No face orientation, no ball tracking, no 3-D, no feedback of L(t) into
stage 1 (§2.4 of the design), no app C++ work — the port happens only after
both exemplar stages are frozen together.
