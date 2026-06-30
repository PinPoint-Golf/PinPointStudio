# Shaft detection — lead-arm plausibility (Phase 1, done) + blur-line detection (Phase 2, corpus-gated)

**Status:** Phase 1 implemented behind `shaft.useArmPlausibility` (default OFF, byte-identical when off).
Phase 2 (blur-line wedge detector) **designed, not built — pick up with a corpus.**
**Origin:** the 2026-06-30 `swing_0008` investigation (a real face-on swing with 51 hand-labelled shaft
frames). **Harness:** `/swinglab` + `docs/validation/shaft_validation_protocol.md`. **Tracker:**
`src/Analysis/shaft_tracker*.{h,cpp}`, `shaft_tracker_math.cpp`.

---

## 1. Why — the two failure modes we found

Running the (vision-only) shaft tracker on `swing_0008` and scoring against the hand labels exposed
**two distinct failures**, with very different fixes:

1. **Address / slow phases — wrong *selection*.** At P1 the recovered shaft pointed **up the lead arm**
   (−129°) instead of down to the ball (truth +102°). The true club was either not in the candidate set
   or out-ranked by the bright forearm ridges. This is a **search-space** problem → **Phase 1**.
2. **Downswing through impact (dt ≈ ±130 ms) — failed *detection*.** The fast club is a **thick, bright,
   motion-blurred streak**; the 9 px thin-ridge top-hat/black-hat doesn't fire on thick blur, so the true
   club is simply **absent from the candidate set** (43 % of labelled frames overall; almost all of the
   fast phase). This is a **detection** problem → **Phase 2**.

Aggregate on `swing_0008` (gate off): `truth.theta_rms ≈ 66°`, `head_median ≈ 191 px`, coverage 0.76 —
dominated by the downswing runaway (the track ran to ~800° while truth dipped past vertical).

**Preprocessing is *not* the primary culprit.** A faithful reproduction showed the club ridge survives
top-hat/black-hat thresholding (~85 %); the MAD threshold degenerates to its floor in the high-dynamic-range
studio (near-black surround + blown-out spotlit mat), so floor clutter *buries* the club rather than the
morphology erasing it. The slow-frame cleanup comes from the Phase-1 wedge mask, not a threshold change.

---

## 2. Phase 1 — lead-arm plausibility sector (DONE)

**Idea.** The club is a wrist-cocked continuation of the lead forearm; the wrist cannot fold it back up the
arm. So **regardless of phase or chirality**, the club lies within ~±120° of the lead-forearm extension.
Restrict the angular search to that sector — everything else is zeroed.

**Why phase- & chirality-independent (the key advantage over the existing priors).** It needs no
swing-progress `s` (which the `useKinematicPrior`/`useEnvelope` paths require and which is absent without
IMU/clean segmentation) and the sector is symmetric, so no chirality estimate is needed. This is exactly
why it works where those priors didn't.

**Implementation** (all behind `shaft.useArmPlausibility`, default OFF):
- `shaft_tracker.cpp::anchorAt()` — computes the **lead-forearm extension** (lead elbow→grip continued;
  handedness picks the lead side; falls back to shoulder→grip with a widened sector; nothing if pose is
  unconfident). Stored on `AnchorState` (`hasArmAxis`, `armAxisRad`, `armAxisIsForearm`).
- `AnchorPrior` (`shaft_tracker_math.h`) carries `armAxisRad` + `armPlausMaxRad` (0 ⇒ disabled).
- `shaft_tracker.cpp` per-frame loop populates them when enabled (forearm: ±`armPlausibilityMaxDeg`=120°;
  shoulder→grip fallback: +30°, capped 175°).
- `shaft_tracker_math.cpp::buildThetaWeights()` — **zeroes every θ bin outside the sector**. This is a
  *scan* restriction, not a post-filter: it both forbids the arm-as-club pick **and** frees the top-K so
  the (often weaker) true club ridge is no longer crowded out.
- Tunables: `shaft.useArmPlausibility` (bool), `shaft.armPlausibilityMaxDeg` (default 120).

**Validated on `swing_0008`** (gate on vs off):
- P1 pick flips from −129° (up the arm) to **97°** (down the club; truth 102° → ~5° error).
- **Detection 57 %→63 %, correct-selection 24 %→39 %**, head_median 191→177 px, **no net loss of legit
  detections** (didn't clip any fully-cocked club at top/follow-through on this swing).
- Aggregate `theta_rms` barely moved (66→71°) — *expected*: it's dominated by the **undetected** downswing,
  which Phase 1 cannot fix (a restricted search can't conjure a candidate detection never produced).

**Flip decision (deferred):** keep OFF until a corpus regression-gate confirms it never clips a legitimate
fully-cocked club across golfers/clubs (one swing is not enough). Then flip the default in
`shaft_tracker.cpp` and freeze `armPlausibilityMaxDeg`.

---

## 3. Phase 2 — blur-line (line-integral) detection in the wedge (TO BUILD)

**The problem to solve.** In the fast phase the club is a thick, faint, possibly **gappy** blur. The
current detector is an **anchored continuous run-scan**: each θ-ray accumulates a supra-threshold run that
must *start near the grip* and stay continuous (bridging only `runMaxGapPx`). Two things break it:
- thin-ridge morphology under-responds to thick blur;
- the usable signal is **fragmented** — on `swing_0008` dt−80 the frame-to-frame diff along the true club
  was strong at the grip (~235) and clubhead (~248) but weak in the middle (~30–40), so the run breaks and
  **zero candidates** form (confirmed: a temporal-diff channel fed through the run-scan changed nothing).

**The fix — a Radon-style line-integral scan over the plausible wedge.** Instead of requiring a continuous
anchored run, for each candidate angle through the grip **sum** the response along the whole line and take
the angle of maximum integral:

```
for θ in plausible-wedge (Phase-1 sector, cheap — small range):
    I(θ) = Σ_ρ  response(grip + ρ·dir(θ))      for ρ in [rhoMin, maxRadius]
candidate = argmax_θ I(θ)  (+ parabolic sub-bin refine; σ from the peak width)
```

- **Robust to gaps** (it integrates, doesn't require continuity) and **to thickness** (a thick streak has
  high integral along its axis; adjacent bins merge by NMS/wedge).
- **Response = top-hat ∪ black-hat ∪ frame-to-frame temporal diff.** The temporal diff is the high-value
  channel for the fast phase (the club is what moved) — it must NOT be body-masked (the club crosses the
  body), and is wedge-confined by Phase 1. (Median-background subtraction, body-masked, is the wrong tool
  here — it masks the club.)
- **Engage in the fast phase only.** Gate on the predicted angular rate ω̂ (already computed from the
  per-frame prediction) exceeding a blur threshold, or fall through to the run-scan; in slow phases the
  run-scan is better (sharp thin ridge) and the diff is near-zero. This is the natural evolution of the
  existing `useBlurMode`/`BlurAcc` path (`shaft_tracker_math.cpp`) — which today does not win — toward a
  line-integral rather than a run accumulator.
- **Clubhead end-point** from the far extent of the supra-integral mass along the chosen line (not a run
  terminus).

**Integration points.**
- New response builder in `detectShaft()` (`shaft_tracker_math.cpp`): a `lineIntegralScan(resp..., prior)`
  alongside the existing `scanAllAnchors`, selected when a `blurMode`/`useLineIntegral` flag is set for the
  frame. Reuse `AnchorPrior.armAxisRad/armPlausMaxRad` to bound θ.
- Temporal-diff channel in `shaft_tracker.cpp`: maintain `prevLuma`/`prevLumaT`, `absdiff(luma, prevLuma)`
  un-body-masked, pass as `diffImage` (the param already exists) when in the fast phase. (A reverted
  scaffold of this exists in git history from 2026-06-30; the run-scan made it inert — re-add it feeding
  the line-integral, not the run-scan.)
- New tunables: `shaft.useLineIntegral` (bool), `shaft.lineIntegralOmegaDps` (fast-phase trigger),
  `shaft.useTemporalDiff` (bool) — all default OFF, dotted-key, swept by SwingLab.

**Acceptance / validation (the corpus is the prerequisite).** Validate via
`docs/validation/shaft_validation_protocol.md`, scored by `score.py`:
- **Downswing detection rate** (fraction of labelled fast-phase frames with a true-direction candidate) ↑
  materially from the ~42 % baseline, and **correct-selection** ↑.
- `truth.theta_rms < 3°` / `head_median < 25 px` on the labelled set, coverage ≥ 0.6 on ≥ 90 % clean.
- **Regression-gate:** 0 per-swing regressions vs the Phase-1 baseline (`lab.py diff`), partitioned
  Tune/Validation/Held-out.
- A/B each lever independently (line-integral; temporal-diff; together) with the plausibility sector on.

**Capture caveat — read before over-fitting a detector.** `swing_0008` is 60 fps MP4 in harsh studio
lighting; per the validation programme, **fast-phase pixel-θ on MP4 is "advisory"** — heavy blur + low
contrast are near the recoverable limit. Validate Phase 2 on a **raw high-speed subset** (`saveRawFrames`
ON, higher shutter, even lighting, non-reflective hitting area), not this single legacy clip. Capture
quality may do more for the downswing than any detector change.

---

## 4. Pickup checklist (when a corpus exists)

1. Capture/label per `docs/validation/shaft_validation_protocol.md` (Wrist session, face-on `perspective=2`,
   **raw subset** for the fast phase, mark shaft line + P7 impact, blinded).
2. Baseline with **Phase 1 on**: `lab.py run … --params '{"shaft.useArmPlausibility":true}' --id p1base`.
3. Build the line-integral detector + temporal-diff channel (§3); A/B each lever; regression-gate.
4. If it clears the gates: flip `useArmPlausibility` (Phase 1) and the Phase-2 flags' defaults; freeze the
   sector width and the new thresholds in `src/Core/pp_tuned_constants.h`; add `tuning_overrides_test` rows.
5. Update `shaft_validation_protocol.md` §5/§7 and this doc's status banner.

**Related:** `shaft_validation_protocol.md` (V&V&T), `shaft_detection_skeleton_impl.md` (K0–K6, the
`useBlurMode`/`BlurAcc` path Phase 2 evolves), `tunable_parameters_reference.md` §2.2 (`shaft.*`).
