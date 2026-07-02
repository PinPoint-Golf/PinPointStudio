# Shaft Detection Exemplar — Findings & Fixes (v4 frozen; v5 checkpoint)

**Status:** v4 (`tools/markup/shaft_annotate.py`) is the last frozen, gate-passing
exemplar. **v5 (`tools/markup/shaft_annotate_v5.py`) is a work-in-progress
checkpoint** — finish-hold detection + corpus sweep, documented in §6 below; it
improves the top of the swing and the finish substantially but has a known
impact-window gap vs v4 and two open defects. Do not port v5 until §6.4 is closed. Companion: [shaft_detection_improvements.md](shaft_detection_improvements.md)
(the original architecture this iterates on) and
[../implementation/shaft_markup_exemplar_impl.md](../implementation/shaft_markup_exemplar_impl.md) (how to run).

This documents what visual failure analysis on real swings found wrong with the
original reference implementation (v1), the fixes, and the methodological
learnings. Every fix below was motivated by a specific frame someone could look
at, and verified the same way.

## 1. Why v1 failed in practice (and why nobody noticed)

Run on `swing_0009`, v1 locks onto the **club's shadow / mat edge at address**
(43° vs true ~98°) and — because detection is confined to a gated fan around the
prediction with no escape mechanism — **stays wrong for the entire swing**, while
reporting LINE_OK. It looked good on `swing_0008` purely by luck of a correct
initialisation: one right init and the fan follows the club; one wrong init and
every subsequent frame confirms the distractor.

A C++ port of v1 (production `ShaftTracker` shares the weakness differently) was
briefly wired into the Markup panel and produced visibly bad markups — reverted.
Rule reaffirmed: **prove the algorithm on the exemplar with eyes on frames before
porting anything into the app.**

## 2. The fixes (F1–F9)

| id | fix | failure it kills | evidence |
|----|-----|------------------|----------|
| F1 | **Run-start gate** — the supported evidence run must *begin* within 55% of the ray ("the club is attached to the hands") | shadow / mat-edge lines, which collect far-radius evidence but never touch the hands | 0009 f0: init 43°→98° |
| F2 | **Edge-pair width prior** (design §4.2 — specified but never implemented in v1): sample `∇I·n` at θ±w/(2r); antiparallel pair required | single edges (mat border), soft wide structures (shadows, limbs) | 0009 address/backswing |
| F3 | **Forearm plausibility sector** (±120° of lead elbow→grip, from pose) — applied in the **tracking fan only**; it must be OFF during full-circle (re)init because the finish wrap breaks the forearm-continuation assumption | arm locks mid-swing | 0009 f186 arm ray correctly suppressed |
| F4 | **Wrong-lock escape** — coarse full-circle rescan every 7 frames; a distant peak that decisively beats the track twice forces re-init | permanent confident-wrong locks (v1's fatal flaw) | 0009 recovery after overhead phase |
| F5 | **ω sanity clamp** (>3000°/s implausible; real clubs peak ~2500°/s) | absurd wedge ω (observed 62,000°/s) destabilising the filter and faking confidence | 0009 f145+ |
| F6 | **180°-flip test at init** — evaluate the peak *and* its opposite ray, crediting a bright distal clubhead blob at the supported run's end (design §7's disambiguation) | opposite-ray inits at the finish | 0009 f373 (was c=1.00 wrong) |
| F7 | **Re-init confirmation** — conf capped at 0.35 until 3 accepted measurements follow a (re)init | semi-confident junk re-inits | 0009 f455 (0.80→0.35) |
| F8 | **Either-path acceptance** — global support fraction (v1 gate) **or** dense-run (extent ≥10% of ray, in-run density ≥0.45) | v1's global-F rejecting foreshortened/cropped clubs at the finish (evidence diluted over a too-long ray); a run-only gate conversely rejects 0008's thin blurred shaft | 0008 measured coverage 21→39 of 51 |
| F9 | **Measured/predicted output model** — conf ≥0.5 in runs ≥4 frames = MEASURED; all other detections are **discarded** and replaced by PREDICTION: the RTS smoothed track through clean gaps (the smoother *is* the optimal bridge), a cubic-hermite bridge (decayed-ω, 360°-aligned) across REINIT/LOST/runaway breaks, decaying-ω extrapolation (τ=0.12 s) for tails | emitting low-confidence junk as labels; nothing at all post-impact where the club is cropped but kinematically predictable | 0009 predictions land on the club (f186/f290/f310) |

## 3. Results at freeze

- **swing_0009** (visual eval): address→impact→wraparound measured correctly;
  post-impact/finish covered by predictions that mostly lie on the club;
  **zero confident-wrong frames** (finish all conf ≤0.35, kind=pred).
- **swing_0008** (51 hand-labelled frames): measured tier **n=39, median 2.9°,
  mean 10.2°, p90 7.1°, 5% >30°**; predicted tier n=12, median 50° (concentrated
  in the genuinely-broken follow-through — declared as prediction).
  v1 all-frames reference: mean 18.8° with no honesty signal at all.

## 4. Methodological learnings (apply to all future detector work)

1. **Median error lies.** v1-era validation reported "median 7.4°" while 24% of
   frames were >30° wrong *with confidence 0.93–0.96*. Report mean / p90 / %bad
   and split by output kind; gate confidence honesty explicitly (bad frames must
   be low-conf, high-conf frames must be accurate).
2. **Confidence must correlate with error or the review workflow is fiction.**
   The whole point of auto-markup is human review of flagged frames; a detector
   that is confidently wrong defeats it silently.
3. **Look at frames, full-resolution.** Two frames (0009 f269, f476) were
   misdiagnosed as failures from small montage tiles and turned out to be
   correct at full-res. Diagnose from zooms; use montages only for triage.
4. **Diagnose with data before tuning.** A per-frame gate dump
   (`--debug-frames`) showed the finish club failed *only* the global-F gate —
   pointing at the exact fix (F8) instead of threshold-fiddling.
5. **One good swing ≠ a working detector.** v1 passed on 0008 and collapsed on
   0009. Evaluate on multiple swings (corpus sweep still pending) before
   trusting, and before porting.

## 5. Known remaining weaknesses

- Finish-hold detection: the static over-shoulder club (0009 f331–f450) is
  mostly undetected (dark-on-dark, sparse run density) — covered honestly by
  prediction, but real measurements there would anchor the predicted tier.
- 0008's follow-through predicted tier is weak (long hermite bridge across a
  broken gap); any mid-follow-through measured segment would fix it.
- Corpus sweep `swing_0001–0007` not yet run.
- Grip anchor accuracy at the finish is suspect (pose hands vs visible glove
  offset) — worth quantifying before blaming evidence.

## 6. v5 iteration — finish-hold detection + corpus sweep (2026-07-03, checkpoint)

Goal: detect the static held club at the finish (v4's biggest gap) and validate
across the corpus (swing_0002–0007; 0001 has no recorded pose). Iterated in the
lab with per-frame landmark adjudication; checkpointed as
`tools/markup/shaft_annotate_v5.py` (= lab v5n).

### 6.1 Capture guidance (user finding)

Eyeballing the annotated corpus showed the post-impact losses are **largely
cropping**: the club exits the frame. **Face-on framing should leave more room to
the player's right** (trail side). Post-impact is kinematically smooth and less
critical for analysis, so cropped frames belong to the predicted tier — but
better framing converts them to measurements for free.

### 6.2 New fixes (F10–F14)

| id | fix | rationale / evidence |
|----|-----|----------------------|
| F10 | **Still-hold stacked re-acquisition**: when the grip ROI is still and the track is lost, average the last 9 frames (noise ~/√K) with a median-stabilised anchor (pose hands wander >100px during a "static" finish) and scan full-circle; accepted detections feed the KF as NORMAL measurements (they must earn confirmation) | single-frame finish evidence is marginal (club S=0.15/F=0.15 vs gates); stacked it clears them decisively (S=0.36/F=0.50). Hold-vs-track divergence >45° forces re-init (the KF gate would reject the jump) |
| F11 | Windowed stillness (≥6/9 frames), stillness blocks single-frame inits (only stacked evidence may acquire in a static scene), still-period consistency (a static club has ONE angle — measured outliers >25° from the conf-weighted circular mean are demoted) | acquire-die-reacquire cycles and instantaneous stillness both starved the stacked path; lostness resets only on CONFIRMED ω-sane measurements |
| F12 | **Scene-permanence veto** for re-acquisitions: a candidate whose edge-pair run already exists (radially overlapping) in a pre-swing scene snapshot is permanent structure (neon strips), not the club. Snapshot = **pixel-median of 11 frames across the clip** — NOT frame 0 | kills the neon-parallel confident-wrong locks. Frame-0 snapshot contains the ADDRESS CLUB, which vetoed impact-zone re-acquisition (the club returns to its address angle at impact) and silently killed the whole downswing — caught by user eyeball, root-caused, fixed with the club-free median |
| F13 | Post-swing quasi-static re-inits additionally require a clubhead blob at the run's end: bright (raw luma) AND changed vs the pre-swing scene | brightness alone is satisfied by the mat; change alone by the golfer's own moved body. The AND still fails against bright+moved objects (see 6.4 f700) |
| F14 | **Quasi-static gating of all hardened gates** (grip-ROI motion < 3 grey levels): F12/F13 apply only in near-still scenes — fast motion keeps v4's permissive acquisition | every distractor lock (neon, collar, body-mat) occurred at stillness; the hardened gates strangled mid-downswing recovery when applied globally |

Also: hold candidates = top-4 NMS peaks + 180° opposites (a dominant, correctly
blob-rejected body line must not mask the true club elsewhere in the circle).

### 6.3 Results at checkpoint (v5n vs v4)

| swing | measured % | finish-region % |
|---|---|---|
| 0009 | 44 → 65% | 4 → **49%** |
| 0002 | 65 → 71% | 19 → **37%** |
| 0003–0007 | ±2–7pts | ~unchanged |
| 0008 guard (at v5l) | measured tier: median 2.8°, mean 3.3°, p90 6.9°, **0% >30° wrong** (n=37/51) | |

Landmarks verified full-res on 0009: hanging club f310/f331 and post-impact
recovery f210 measured correctly; neon lock f435 and body line f450 dead.

### 6.4 Open items (close before promoting v5 → the exemplar)

1. **Per-segment RTS smoothing (structural, do first).** The smoother currently
   runs over one history containing re-init discontinuities (init appends
   pseudo-predictions); long junk-coast chains poison it — smoothed ω reaches
   1e18+, and an attempted speed-aware coast budget (more init boundaries) drove
   it to 1e49 and had to be reverted. Split the RTS at re-init boundaries; then
   re-add the speed-aware coast budget (12 coast frames at 2000°/s ≈ 160° of
   blind drift — fast phases need ~4), which should recover the impact window
   (f165–205 currently rides the predicted tier; v4 measured it).
2. **f460 regression**: the median snapshot partially contains the finish-pose
   body, and the permanence veto now rejects the true over-shoulder club on 0009
   — tune the veto threshold or exclude finish-weighted frames from the median.
3. **f700-class (s2)**: a body-seam ray terminating at the golfer's white shoes —
   bright AND moved, defeating both blob channels. Needs a pose-derived body
   mask in the evidence (the C++ tracker's clutter mask; prep must export the
   skeleton per frame).
4. Finish measured-segment audit across 0002–0007 (zoom adjudication), then
   re-run the 0008 numeric guard on the final variant.

### 6.5 Methodology lessons (additions to §4)

6. **Aggregate stats mask phase-local regressions.** v5's overall measured% dip
   looked like "junk removal" while the entire downswing had silently moved to
   the predicted tier — a user eyeball caught it. Track per-phase coverage
   (address/backswing/downswing/impact/finish), not just totals.
7. **Harden gates only in the regime whose failures they fix.** Every
   anti-distractor gate added for the static finish also fired during fast
   motion, where the permissive design was already correct.
8. **A veto's reference data must not contain the thing being detected** (the
   address club in the frame-0 snapshot). Prefer references that structurally
   exclude the target (temporal median).
