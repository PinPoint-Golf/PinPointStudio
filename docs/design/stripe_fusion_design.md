# Stripe fusion — full-swing instrumented truth from a fixed environment

**Status: design, 2026-07-05.** Successor to `stripe_annotate.py` v1 (blob →
ratio match), which is validated but scoped to the club-up phases. Companion:
`docs/validation/instrumented_club_protocol.md` (band geometry, session
plan), `tools/shaftlab/README.md` (exemplar rules), lab
`tape_20260705/RESULTS.md` (the adjudication record this design rests on).

## 1. The environment is fixed — the fusion has to happen in software

Hardware ceiling (per Mark, 2026-07-05; do not revisit): 150 fps is the
camera's max; ~6.57 ms exposure is effectively full-frame and cannot
shorten (light budget); the blown hitting-area highlights ARE the studio
downlight doing its job; a ring light is mounted at the camera.

What a human sees in these captures — and the detector must reproduce: the
club is visible in EVERY frame, but through different physics per regime:

| regime | dominant signal | background | polarity |
|---|---|---|---|
| address/backswing over cloth | steel specular + retro bands (bands only marginally over saturation, bloom near grip) | dark trousers | shaft BRIGHT |
| address, tip section | bare steel | blown mat (255) | shaft DARK; bands white-on-white = INVISIBLE |
| downswing/thru | retro bands blaze as discrete saturated dashes; steel smeared | mixed | bands bright; the validated v1 regime |
| finish holds | mixed; bands often blazing | dark wall / shirt | mostly bright |

Adjudicated failure modes that kill single-signal detectors here (all in
`tape_20260705/RESULTS.md`): saturated-blob thresholding sees nothing at
address (bands fragment to 1–25 px² among 150 saturated junk components);
loosening blob gates floods the ratio matcher with junk affine fits (n=4
median error 112°); a lateral-MAX profile sampler is structurally blind to
the dark-on-mat regime (reads the blown mat's 255, never the dark shaft).

## 2. Design: three evidence terms, one temporal estimator

Per-frame state: `(theta, s, r0)` — ray direction from the grip anchor,
projected scale px/mm, butt→anchor offset mm. Inputs: raw-decoded frames,
`anchors.csv` (pose grip — independent of stage 1), `clubs.json` geometry.
Stage-1 outputs are never read: this is the truth generator that scores
stage 1.

### E1 — band-pattern evidence (keep v1, it is validated)

The existing blob → collinear RANSAC → order-preserving affine ratio match,
with the flip test and dark-gap verification. 1.1° median, zero flips where
it fires. Extended by sub-saturation band peaks taken from the E2 profile
(local prominence, no absolute threshold) so the ratio matcher also gets
candidates where bands peak at 240 instead of blooming.

### E2 — polarity-agnostic ridge evidence (new; the "steel line")

For a candidate ray, sample `m(r)` = on-ray mean (±1 px) and `b(r)` =
lateral background (median at ±8–12 px). Evidence is `|m − b|` with a
**piecewise-coherent sign**: the shaft is brighter than cloth AND darker
than blown mat in the same frame, so the sign of `(m − b)` must be constant
within background segments and may flip only at background transitions
(segment `b(r)` by level). Incoherent sign flips get no credit — that is
what separates a real thin line from noise. This term sees the steel
everywhere the human does, including dark-dashes-on-mat.

### E3 — temporal evidence (new; the human's trump card)

- **Still runs** (grip-region stillness, same detection concept stage 1
  uses for F11): median-stack the E2 *profiles* across the run — aligned
  per frame by that frame's own anchor — √N noise suppression, then run E1
  peak extraction + ratio fit ONCE per run and broadcast to member frames.
  Profile-space stacking, never pixel-space (anchor wobble smears pixels;
  adjudicated in the 07-05 probes).
- **Motion phases**: evidence sampled on the scene-median-subtracted frame
  (primitives already in `clubhead_scan.py`). Static clutter — mat sparkle,
  ceiling lights, club bag, screen — vanishes; the moving shaft and its
  blazing bands survive. This removes the junk field that killed the
  loosened blob matcher.

### Estimator

Coarse-to-fine theta sweep (±12° around the temporal prediction; full
sweep on reinit), (s, r0) grid only where E1 candidates exist. Forward pass
+ RTS-style backward smoothing (same shape as stage 1's tracker), with the
usual segment splits at confident jumps.

### Honesty tiers (truth writer)

- `meas`: E1 locked (≥4 bands, or ≥3 flip-unique within a still run) →
  full truth: theta + s + head extrapolation.
- `ray`: E2/E3 locked, bands not → **theta-only truth entry** (no head, no
  len). `score_truth.py` already scores theta independently of head, so
  ray-tier entries immediately widen stage-1 scoring coverage.
- absent otherwise. No pred tier in truth output, ever. Confidence must
  satisfy the standard honesty clauses against hand-label spot checks.

## 3. Validation gates (corpus discipline, in order)

1. **Synthetic**: extend `make_synth.py` with a blown-background region
   (inverted contrast), sub-saturation bands, and speckle junk; require
   theta mean ≤2°, zero flips, and correct tier assignment.
2. **s01 adjudication**: overlay + montage full-res eyeball; cross-check
   vs stage-1 meas frames (expect ≤3° median agreement; zero >30° in meas
   AND ray tiers); confirm the address dark-on-mat section is followed.
3. **Corpus**: all 10 swings of tape_20260705; per-phase coverage table
   vs the v1 baseline (v1: ~0% address, 54–79% club-up); honesty clauses;
   byte-identical rerun (determinism).
4. Truth v2 rewrite (`--truth-out`) + passive stage-1/stage-2 rescoring;
   only then is the corpus fit to gate the deferred passive decisions
   (length-model form, F11 fix, arm-floor confidence).

## 4. Non-goals

- No change to passive stage 1/2 in this work (they are consumers of the
  truth, and the C++ port stays frozen behind the exemplar gate).
- No learning/training; deterministic classical pipeline only (regression
  oracle + port contract).
- No new hardware asks.
