# Shaft Detection Exemplar — Findings & Fixes (v4, 2026-07-02)

**Status:** Frozen at v4. `tools/markup/shaft_annotate.py` is the current exemplar
and the oracle for any C++ port. Companion: [shaft_detection_improvements.md](shaft_detection_improvements.md)
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
