# Position-first shaft measurement — implementation plan

Companion to [`docs/design/shaft_position_first_design.md`](../design/shaft_position_first_design.md).
Status: PROPOSED (2026-07-11). Phases are commit-sized; every phase carries its
own gate and its own rollback (a config flag). Nothing ships dark for long:
each layer has an explicit flag-flip commit once its corpus gate passes.

Conventions: build `--parallel 4` locally; corpus runs serialized on the
studio; per-phase soak gates use the P6 pattern (reference binary vs
flag-off run, byte-compare modulo volatile keys). Ground truth = markup-lab
`truth.json` labels. Never push without approval (standing rule).

---

## Phase A — line re-registration («snap») + lineConf

### A1 — the snap pass (dark, `shaft.snap.enabled = false`)

**Code**
- `src/Analysis/shaft_track_assembly.h`: `SnapConfig {enabled=false, maxOffsetPx=15,
  maxDeltaDeg=3.0, minLineConf=0.25, corridorHalfPx=2}` nested in `ShaftV3Config`
  as `snap`; `fromOverrides` gains `shaft.snap.*` keys.
- `src/Analysis/shaft_track_assembly.cpp`: new pass after PASS 2 placement
  (~:1280 block end), before the length-fusion post-pass: for each emitted
  sample with a vision tier (Measured|Wedge — never Coasted/pred), local search
  over (⊥ offset, Δθ) maximizing the ridge line integral (reuse the E2
  ridge/Sobel field already computed per frame; cache per-frame fields only for
  span frames — memory unchanged, they exist already). Accept iff
  lineConf ≥ minLineConf AND the arm-plausibility sector admits the snapped
  line; else keep the original and record its lineConf anyway.
- `src/Analysis/swing_analysis.h`: `ShaftSample2D.lineConf = -1.f`.
- Serialization (both parity writers, additive): `samples[].lineConf` in
  `src/Export/swing_doc.cpp` and `src/Gui/shot/shot_processor.cpp`
  (`toAnalysisDetail`).
- `ShaftDecideTrace`: `snapAppliedN`, `medianSnapOffsetPx`, `medianLineConf`
  (SwingLab triage).

**Tests / gates**
- New synth fixture in `src/Analysis/tests/shaft_decide_test.cpp`: render the
  shaft deliberately offset from the injected grip anchor (+12 px ⊥); assert
  snap recovers the line within 2 px and lineConf > 0.7; assert snap-off leaves
  samples byte-identical.
- Full-suite green both hosts; app builds.

**Blast radius:** flag off ⇒ byte-identical (soak-gated in A2). Flag on ⇒ only
`gripPx/headPx/thetaRad/lineConf` of vision-tier samples move, bounded by
maxOffsetPx/maxDeltaDeg. No tier, DP, ψ, segmentation, fusion, or length
changes. ~350 LoC.

### A2 — corpus gate + flag flip

- New estimand in swinglab scoring (`tools/swinglab/score.py`): median ⊥
  distance from the drawn line to the labelled shaft segment over labelled
  frames (`truth.json` shaft endpoints exist from markup lab).
- Studio runs: (a) snap-off soak vs an A1-parent reference binary —
  byte-identical required; (b) snap-on vs snap-off on the labelled corpus —
  gate: ⊥ distance improves on EVERY labelled swing; θ-RMS vs markup does not
  regress on any; `lab.py diff` 0 regressions.
- Flip `snap.enabled` default → true in a dedicated commit citing the numbers.

**Blast radius:** the deliberate visual change lands here, reversible by flag.

---

## Phase B — P-anchors + milestone fits

### B1 — P-time extraction + report-only positions[] (dark, `positions.enabled=false`)

**Code**
- `src/Analysis/swing_analysis.h`: `Phase::ShaftParallelBack = 12 (P2)`,
  `ArmParallelDown = 13 (P5)`, `ShaftParallelThrough = 14 (P8)`;
  `struct ShaftPosition {int p; int64_t t_us; QPointF gripPx, headPx;
  double thetaRad, lenPx; float conf, sigmaThetaDeg, sigmaLenPx; int stackN;
  uint8_t source;}` and `ShaftTrack2D.positions`.
- New header-only `src/Analysis/shaft_positions.h` (+
  `src/Analysis/tests/shaft_positions_test.cpp`): pure functions that take the
  smoothed θ(t), φ(t), and segmentation events and return the P1–P8 times
  (crossing detection with per-P phase windows, hysteresis on the parallel
  crossings). Header-only + standalone-tested per the detector-math convention.
- `shaft_track_assembly.cpp` (or `wrist_analyzer.cpp` — whichever owns the
  post-track view of both θ and segmentation; decide at implementation):
  populate `positions[]` at the located times **from the existing (snapped)
  track** — no new measurement yet — with `source = TrackSample`; emit the new
  PhaseEvents (P2/P5/P8; upgrade P3/P6 provenance to Club).
- Serialization: `analysis.club.positions[]` in both parity writers; phase
  label map (`swing_data_source.cpp phaseLabel`, timeline labels) gains the
  three names; data viewer Analysis group gains a "Positions n/8 · min conf"
  row. Schema doc + changelog.

**Gate:** P-time accuracy vs labelled P-frames on the markup corpus (median ≤ 1
frame, no outlier > 3 frames); flag-off soak byte-identical. This phase also
produces the **baseline per-P error table** — the "how bad is each position
today" numbers that Layer B2 must beat.

**Blast radius:** report-only — positions mirror existing samples; UI additions
are new rows/labels. ~450 LoC.

### B2 — milestone fitter (`positions.fitEnabled`, dark)

**Code**
- New `src/Analysis/shaft_position_fit.{h,cpp}` (+ tests): the ±k-frame
  milestone fit — shift-and-stack registration along locally predicted motion
  (from smoothed ω(t)), then joint (grip, θ, L) refinement on the stacked image
  against ridge + band evidence + arm sector (+ ball at P1/P7, reusing
  BallTrack2D). Consumes the FrameSource the tracker already threads through;
  runs after decideTrack inside the same analyzer stage (compute budget: 8
  fits × small windows ≪ the pose pass).
- Populates `positions[]` with `source = MilestoneFit`, honest σ from the fit
  residuals; falls back to B1's track sample when a fit rejects (stack too
  blurred, off-frame, lineConf floor).
- Config namespace `positions.*` (k, stack strides, accept floors) via
  fromOverrides.

**Tests:** synth fixtures per P archetype (still P1, reversal P4, blur-gap P7,
off-frame P8) asserting fit beats the injected per-frame noise; determinism.

**Gate (studio):** per-P |Δθ|, |ΔL|, grip ⊥ error vs markup — must beat the B1
baseline table at every labelled P with margin, conf correlating with error;
fit-off soak byte-identical.

**Blast radius:** new module + `positions[]` content upgrade. `samples[]`
untouched — metrics/scoring unaffected. ~700 LoC, the heavy phase.

### B3 — re-point length fusion + band-scale fix (absorbs the 2026-07-10 escalation)

- E-ball/E-band candidates sourced from the P1 milestone fit (in-plane, still,
  stacked); E-head from P4–P8 fits' head terminus. The fusion math, prior,
  persistence, serialization stay exactly as shipped (d325268) — only
  `instantLengthCandidates`/E-head extraction inputs change, behind
  `positions.fitEnabled`.
- Re-run the P6 convergence chain on the studio; expect E-band to land within
  ~5% of E-ball (vs −28% today) ⇒ re-tighten `fusion.sigFracBand` (and
  possibly sigFracHead) from the new measured deviations; close
  `runs/ESCALATION.md` with the numbers.

**Gate:** convergence chain spread ≤ the current ±2.6% with tighter conf; 0
regressions on the lab run.

### B4 — flag-flip: `positions.enabled` + `positions.fitEnabled` default ON

Dedicated commit citing B1/B2/B3 gate numbers, after Mark's review of the per-P
table.

---

## Phase C — synthesis between anchors

### C1 — synthesized tier (`synth.enabled`, dark)

- `swing_analysis.h`: `flags` widens `uint8_t → uint16_t`;
  `ShaftSynthesized = 0x100`. Mechanical sweep of flags consumers (C++
  bitmasks, QML `flags &` uses, schema doc flag table) — the ONLY non-additive
  type change in the whole plan; JSON/QML already treat flags as int.
- New `src/Analysis/shaft_synthesis.h` (header-only + tests): boundary-value
  fit of the existing kinematic model (R7 machinery) through consecutive P
  states, C¹ at anchors; emits per-frame synthesized samples between anchors.
- Emission choice (decide in code review): populate the existing `predicted[]`
  channel with the constrained model (it is empty in v3 and already excluded
  from metrics), OR a third array. Either is additive in swing.json.
- Consumers filter by flag: metrics/scoring/SwingLab estimands EXCLUDE
  synthesized samples (assert via a unit test that the estimand helpers skip
  the flag).

### C2 — overlay + viewer honesty

- Replay overlay: anchored/measured frames bright, synthesized dim (extends
  the existing measured-vs-projected split); data viewer Club lanes show
  synthesized samples at conf 0 styling.
- Any new visibility toggle goes in the View menu via ViewLayout (standing
  rule), deferred to a UX pass with Mark — C2 only extends existing styling.

**Gate:** continuity at anchors (< ε discontinuity), visual review by Mark,
synth-off soak byte-identical. ~400 LoC across C1+C2.

---

## Phase V — freeze

- Full corpus battery at final defaults: soak (all flags off ⇒ pre-v3.5
  byte-identical), labelled-corpus per-P table, convergence chain, `lab.py
  diff` 0 regressions, both hosts' unit suites, app builds.
- Docs as-built pass (design doc §s marked IMPLEMENTED, schema doc, tunables
  reference gains the three namespaces), memory update.
- Standing caveat recorded: single-corpus validation; multi-club corpus
  re-validation before coach-facing accuracy claims.

---

## Blast-radius summary (the "what do we risk losing" table)

| Existing asset | Effect |
|---|---|
| v3 DP tracker + ψ rail + Stage-2 head pass | untouched; consumed as backbone |
| Club-length fusion + prior + serialization (this week's work) | untouched through B2; B3 changes candidate sourcing only, behind a flag, re-gated by the same convergence chain |
| Band-bias escalation | resolved by B3 (by construction, not tuning) |
| Untaped-club investigation | B2's shift-and-stack IS its planned mechanism — shared implementation, one corpus later |
| Wrist metrics / scoring / segmentation / ball / IMU / capture | untouched (metrics keep reading `samples[]`; synthesized excluded by flag) |
| swing.json | additive throughout; one C++ type widening (flags) with no schema effect |
| Rollback at any point | flip the layer's flag default; every layer soak-gated to byte-identical when off |

## Sizing

| Phase | Est. LoC | Studio time |
|---|---|---|
| A1+A2 | ~400 + score.py estimand | 2 corpus runs |
| B1 | ~450 | 1 run |
| B2 | ~700 | 2–3 runs |
| B3 | ~100 | convergence chain ×1–2 |
| C1+C2 | ~400 | 1 run + visual |

Suggested execution: A and B1 in one working session (A2 gate overnight-able);
B2 is its own session; B3/B4 short; C after Mark reviews the per-P numbers —
the per-P table from B1 may itself reshape priorities (e.g. if P4/P7 are the
only weak spots, B2 can start there and defer P2/P8 fits).
