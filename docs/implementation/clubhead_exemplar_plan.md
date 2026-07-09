# Clubhead Detection Exemplar — Implementation Plan (Stage 2)

**Status:** H0–H2 DONE (2026-07-03/04, as-built notes per phase below);
stage-2 tools live in **`tools/shaftlab/`** (promoted 2026-07-04:
`length_model.py`, `clubhead_scan.py`, `clubhead_measure.py`,
`clubhead_annotate.py` — lab heads v0/v1/v2 renamed; byte-identical outputs
verified at the move). H3–H4 remain. Design:
[../design/clubhead_detection_design.md](../design/clubhead_detection_design.md).
Method: identical to the shaft exemplar loop — build in the lab, diagnose with
data (`--debug-frames`-style dumps), verify with eyes at full resolution,
gate numerically on 0008, sweep the corpus, freeze.

> **RETIRED to C++ (2026-07-09) — see the closing note at the end of this
> file.** The exemplar stopped here (H3 in progress, H4 never started); the
> track went direct to C++ per the standing exemplar-retirement workflow.
> `H3`'s posterior-σ tier and `H4`'s corpus gate both shipped, just not as
> Python exemplar phases.

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

`tools/shaftlab/clubhead_annotate.py` (as built, H2):

```
clubhead_annotate.py <clip.mp4> --track <stage1_track.csv> \
    [--skeleton skeleton.csv] [--fps-override F] [--out-dir out]
→  <stem>_head.csv          frame, r_h, head_x, head_y, conf_h, kind_h,
                            L_vis, L_pred, r_edge, arm_floored, s1_kind, phase
→  <stem>_head_annotated.mp4   stage-1 lines + head marker (○ meas, ◌ pred, ✕ off)
(+ render_combined.py merges stage-1 + stage-2 into one review video)
```

Plus: `score_truth.py` gains a `--head` mode (0008 hand labels carry head px);
`make_synth.py` gains a head blob + head ground truth.

## Phases

### H0 — harness + baseline (half a session) — **DONE 2026-07-03 (length-model-first reorder)**

As built (session decision: the projected-length model came first, since it
drives crop detection and the later detection CI; corpus-gating directive:
single-swing residuals are development signals only — model-form selection
needs many swings across clubs, baked into the validation docs):

- `tools/shaftlab/length_model.py` — model candidates M0 (const), M1 (single
  plane), M2 (per-phase plane, shared scale), M3 (cone), M4 (kernel);
  labeled robust fit + **per-swing censored-quantile self-fit (the production
  path, no labels)**; ω-anchored phase split from the contract track alone.
  0008 labeled 5-fold CV medians: M0 16.4 / M1 12.3 / M2 10.6 / M3 8.9 /
  M4 5.0 %. **M1 fails the 180° symmetry** (thru 34.5 %) — no single plane.
  M2's back/down planes fit near-identical ι (44.6/45.2°) with thru distinct
  (63°). Self-fit vs labels: 20.2 % median (the run-end noise floor).
  Corpus self-fit consistency (no labels): ι_back 41–59° across all 8 swings.
- `tools/shaftlab/clubhead_scan.py (lab head_v0)` — contract-v1-only reader, independent
  edge-pair+motion run-end scan, zeroth-order head, `L_pred`/`off`, head CSV +
  annotated MP4. Scan lessons that survived data contact: presence gate
  (changed-vs-scene-median OR moving — F12 radially), grip-connected run with
  25 px gap merge (F1 radially), lateral-band tension (synth needs ±12 px for
  θ-noise; blurred real footage scores 2× better at 0 — H1's wedge resolves).
- `score_truth.py --head` + `make_synth.py` (ρ profile, head blob, truth.json
  schema, contract track output). **Synth gate PASSED**: label-free self-fit
  recovers the known ρ profile at ~6 % median; head 13.9 px median.
- **0008 baseline (the H1 bar):** head meas median 29.1 px, p90 343 px,
  bad>40 px 45 %, honesty fails (placeholder conf) — recorded in
  `shaft_markup_lab/s8v2/h0/BASELINE.md`; corpus sweep + crop baseline in
  `shaft_markup_lab/H0_CORPUS.md` (off-test underfires at H0: pred-tier rays +
  under-predicted thru lengths — H1/H2 items; genuine crop validation needs
  uncropped captures).
- Two stage-2-side robustness fixes the corpus forced: re-unwrap `theta_out`
  locally (stage-1 re-inits rebase the winding), and rate-anchored phase
  splitting from meas frames only (pred bridges fake |ω| peaks).
- Surfaced for the stage-1 backlog: s9v2 f302–320 hang region measured at
  θ≈92.5°, conf 0.92 while the club visibly hangs at ~60° (H0_CORPUS.md).

### H1 — radial evidence + per-frame measurement (1 session) — **DONE 2026-07-04**

As built (`tools/shaftlab/clubhead_measure.py (lab head_v1)`; the designed peak-of-E_head formula
was tried first and REJECTED by data — channel saturation made the area
channels non-discriminative and E_end fired on mid-shaft specular breaks):

- **Head-point convention adjudicated** (0008 full-res zooms, all phases): the
  label is the **end of the club line** (on-axis, at the hosel), NOT the blade
  centroid — output stays on-axis; no lateral centroid (design §4's centroid
  refinement dropped).
- **Measurement = gap-tolerant on-axis terminus**: per-sample support =
  (thin-line OR moving) AND (changed-vs-scene-median OR moving) AND NOT
  permanent-line (scene median's own edge-pair — F12 as a channel); candidate
  segment-ends scored by tail evidence quality × length-model prior (the CI
  role of the model; σ floored at 40 px; **prior only in the output pass** —
  the fit pass is prior-free, no feedback); grip-connection + accumulated
  support-fraction gates ("40 % visible in chunks wins", radially). Key data
  lesson (f52): a 75 px specular **blowout gap** separates shaft and head at
  address — continuity must not be required.
- **Multi-width edge-pair (5/12/24 px)**: the 0009 top-of-swing bloom halo
  (~12 px) and the blade itself defeat a single 5 px width. Prior fit pools
  back+down (downswing meas coverage is sparse; kernel starvation had produced
  L_pred = 95 px and the prior dragged measurements onto its own error).
  Ambiguity-shaped conf (runner-up score ratio). `--lat-max` as in H0.
- **Gate PASSED:** 0008 meas median 24.7 px (H0: 29.1), 0009 23.2 px
  (H0: 85.6), synth 15.5 px / 2 % bad with honesty clauses passing;
  0009 address/backswing/top adjudicated correct at full res.
- **Carried to H2 (as designed):** thru-phase junk picks at conf 0.6–0.8
  (0008 thru median 280 px) — the temporal model + tiers + conf shaping own
  these; real-footage honesty clauses are H2's gate.

### H2 — temporal model + output tiers (1 session) — **DONE 2026-07-04**

As built (`tools/shaftlab/clubhead_annotate.py (lab head_v2)` on top of head_v1's measurement):

- **Arm-length plausibility floor** (user standing rule: the club is ALWAYS
  longer than the arm): floor = 1.05 × projected shoulder→grip (skeleton.csv,
  max over both shoulders), applied to candidate termini in quasi-still frames
  before the top — fixed the "impossibly short club at address" the eyeball
  review found (saturated-mat under-runs; 0008 f52/f57 now 6/13 px as pred,
  address L_pred floored). NOT applied in wrap/finish (projected club can
  legitimately be shorter than projected arm there).
- 1-D KF `[r, ṙ]` (σ_acc 4e4 px/s², 3σ gate, coast 12→4 when |ṙ|>800 px/s,
  coasted tails trimmed), segments split on stage-1 θ jumps >20°/frame,
  **per-segment RTS**. Tiers: meas = accepted measurement, conf ≥0.5, in a
  confirmed run ≥4 (single-frame holes tolerated), re-init cap 0.35 (F7/F9);
  pred = smoothed/bridged r (low-conf discarded); off = r_edge < 0.8·L̂.
- **Stage-1-pred frames never reach the meas tier** (design §2.2: the ray is a
  kinematic guess; the emitted position can't beat it — radial measurement
  still feeds the filter). Plus a per-frame 180° flip-check (opposite-ray
  support; suspect frames demoted, never corrected).
- **Gate PASSED:** 0008 meas median 19.0 px, p90 35.7, 0 % bad>40, honesty
  100 %/0 % (clauses pass with full margin); synth meas 107/120 median
  15.6 px honesty 100 %/0 %; head_step median 2.5 px. 0009 meas median
  19.4 px; its single high-conf-bad is **f80, a grip/head-SWAPPED hand label**
  (adjudicated on frame: stage-1 is right there) — excluded, honesty passes.
  f190 adjudicated the other way: genuine stage-1 confident-wrong
  (follow-through body-line lock, conf 0.90) → stage-1 backlog.
- **Carried to H3:** meas coverage is backswing-heavy (downswing/thru labels
  mostly pred-tier — honest but sparse); the hard phases (impact streak,
  wraparound occlusion, unlit finish, body gate) are H3's scope.

### H3 — hard phases (1 session, data-led) — **IN PROGRESS (2026-07-04/05)**

Done so far:
- **Hard-stratum (c1 dark studio) investigation closed** — see
  `shaft_markup_lab/c1/RESULTS.md`: median poisoning found (fix built,
  validated, deliberately reverted — honesty first), bright-on-blown exposure
  failure adjudicated (capture spec updated), stacking + terminus-contrast
  conf shaping tested and rejected by data. Conclusion: exposure-limited
  scenes are capture-side work; the meas tier SHOULD starve there.
- **Posterior-σ meas tier** (the design's actual H2 conf spec, implemented
  here): accepted + confirmed + s1-meas measurements with smoothed σ_r ≤ 10 px
  are label-grade even when instantaneous conf dips (impact blur). σ threshold
  set from data (bad frames appear from σ≈11 — f220 err 348 px at σ 11.0; not
  raised to chase f206/207 at σ 13–16 whose errors are ~33 px anyway).
  Coverage: 0008 meas 17→20 labels (0 % bad), 0009 5→7, synth 107→114, c1
  4→6 — honesty clauses pass everywhere, no new high-conf-bad anywhere.
  New additive CSV column: `sigma_r`.

Remaining (the original list):
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
- Freeze (tools already live in `tools/shaftlab/` since 2026-07-04 — the H4
  act is the corpus gate + doc pointers + a findings section for stage 2,
  fix ids continue: H-series).

## Acceptance targets (v1 freeze)

> **Corpus-gating (2026-07-03):** the 0008 rows below are *development gates*
> (relative bars for H1–H3), not validity claims. Length-model form selection
> and final accuracy acceptance require corpus labels across held-out swings
> AND clubs — protocol in `docs/validation/shaft_validation_protocol.md` and
> `docs/validation/pipeline_validation_and_tuning.md` §5.5/§3.4/§8.

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

## Closing note (2026-07-09) — exemplar retired, went direct to C++

The "port happens only after both exemplar stages are frozen together" plan
above did not hold: **H3 stopped in progress and H4 never started as Python
exemplar phases.** Per the standing workflow (new shaft-tracker features go
direct to C++, verified via `lab.py`/`swinglab_run`, not further `tools/
shaftlab/*.py` work), the remaining scope was ported and finished directly in
`src/Analysis/clubhead_track.{h,cpp}`, wired into `ShaftTracker::decideTrack`
after `reconcilePsi` — commits `cbe68cd` (port + wiring), `bd9c47e`
(corpus-gate fixes), `df76fe9` (`shaft.head.enabled` default ON).

- **H3 (posterior-σ meas tier, hard phases) shipped in C++**, not as further
  exemplar iteration: the accepted+confirmed+σ_r≤10px label-grade tier from
  the exemplar's H3 progress note is in the C++ port; the *hard-phase items
  H3 left as its "Remaining" list* — impact streak, wraparound occlusion,
  unlit finish, stage-1 pred-tier frames — were **not individually re-solved
  in C++**. They carry over as honest `pred`-tier behaviour: the backswing
  streak confidence cap (0.45, motion-blur streaks corpus-proven to
  short-lock) and the phase-ramped meas-acceptance floor (0.5·L̂ ramped to
  0.8·L̂ at takeaway/impact) are the C++ answer to the same failure modes the
  exemplar's H3 list named, arrived at by capping confidence rather than
  chasing each hard-phase channel fix.
- **H4 (corpus + freeze) was superseded, not completed as planned.** There
  was no 0002–0009 sweep + decoupling test against v5n/v6 stage-1 CSVs in the
  lab harness. Instead the C++ port was gated on a **dense 2026-07-05 studio
  corpus** (10 taped 7i swings, 40–121 dense labels/swing, captured on the
  studio PC, not the lab clips): honesty clause passes 9/10 swings (the
  allowed fail, swing_0001, is attributed to Stage-1 θ-quality, not the head
  pass), meas-tier median error 0.8–2.4 px/swing, θ bit-identical head-on vs
  head-off, all 35 analyzer unit tests pass (3 new suites). This is a
  different and arguably stronger gate than the planned H4 (real captured
  swings vs lab clips) but it means the exemplar's own acceptance-targets
  table (H4 section, `0002/0009` corpus) was never exercised — it is
  superseded, not satisfied.
- **The exemplar's self-fit length model was explicitly NOT ported** (see
  `docs/design/club_tracking_v3_design.md` §9.4's as-built note and
  `docs/implementation/shaft_tracker_impl.md`'s Phase B note): the C++ head
  pass uses the ball-measured `L_px` prior instead, which did not exist when
  this plan was written.
- Pointers: `src/Analysis/clubhead_track.h` / `.cpp` for the implementation;
  `docs/design/clubhead_detection_design.md`'s as-built section for the
  design-vs-built delta; `docs/design/clubhead_length_status.md`'s
  RESOLUTION block for the end-to-end picture.
