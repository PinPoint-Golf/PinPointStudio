# Corpus v1 — Verification, Validation & Tuning Plan

**Status**: Operational runbook (updated 2026-06-26). Awaiting corpus v1 capture (50 real golf swings, native video format).
**Owner**: Mark Liversedge
**Harness**: SwingLab (`tools/swinglab/`, `swinglab_run` + `lab.py`) — see `docs/developer/swinglab_developer_guide.md`.
**Scope**: Use the 50-swing corpus to verify, validate, and tune the offline analysis pipeline:
IMU fusion + calibration → phase segmentation → shaft tracking (incl. skeleton-aware) → shot detection → wrist metrics + scoring.

> **This is now the Corpus-1 operational slice of a three-corpus programme.** The methodological backbone — the
> validity ladder, the three-corpus progression, sample-size/power per statistic, and the per-stage V&V&T — lives in
> [`docs/validation/pipeline_validation_and_tuning.md`](../validation/pipeline_validation_and_tuning.md) (the *reference*),
> with the full tunable-parameter catalog + injection/sweep machinery in
> [`docs/validation/tunable_parameters_reference.md`](../validation/tunable_parameters_reference.md). The reference
> **supersedes the "external reference: none" decision in §5/§10 below** (HackMotion is now the Corpus-2 concurrent
> criterion, piloted in Corpus 1) and adds the `score.* / sampler.* / rules.* / bands.* / filter.*` tuning surfaces that
> this runbook predates. Where this doc and the reference disagree, the reference wins — the inline notes below mark each
> superseded point.

---

## 0. Grounding findings (confirmed 2026-06-23)

These facts were established by inspecting the existing studio recordings and running `lab.py doctor`. They define the
plan's constraints — read them first.

1. **"Native video format" = MP4 + inline-IMU `swing.json`.** A recorded swing dir is:
   `Face-On.mp4` (perspective 2) + `Down-the-Line.mp4` (perspective 1) + `swing.json` (schema `pinpoint.swing/2`,
   IMU samples inline) + `thumb.jpg`. **No `.raw` sidecars; no separate `imu_*.csv/.bin`.**
2. **The existing 15 recordings are unreliable in specific, diagnosable ways** — they define the capture bar corpus v1 must clear:
   - 06-10 swings: 0–1 IMU streams, empty analysis.
   - 06-11 session (the best): 2–3 IMU streams, analysis ran (6–8 phases incl. Impact, 2–5 metrics) **but `bindings: 0`
     on every swing** and scores are wildly inconsistent (1, 3, 32, 71, 100).
   - 06-19 (newest): regressed to **0 IMU**, empty analysis.
   - `ballDetection.calibrated = false` and raw frames absent on all 15.
3. **Zero `analysis.bindings[]` is fatal for IMU validation.** The runner deliberately never fabricates identity A/M
   (re-fusing without the session calibration is fiction). With no bindings, SwingLab cannot re-fuse the IMU → **no wrist
   metrics, no IMU↔vision correlation, no IMU-bridged shaft coverage**. The Wrist analyzer is the only *real* analyzer, so
   missing bindings nullifies most of the pipeline. **This is the #1 capture must-fix.**
4. **Only the Wrist analyzer (sessionType 1) is real.** Swing (0), GRF (2), Coach (3) are deterministic stubs
   (`shot_analyzer.cpp:61–93`) — they emit a placeholder score and no metrics. Corpus v1 should be **Wrist lessons**.
5. **Harness is operational on this host**: `swinglab_run` built (2026-06-20), venv + numpy/cv2/matplotlib OK, `doctor` green.

**Consequence**: the highest-leverage work is a *capture acceptance gate* (Phase C0) that guarantees every swing carries
bound+calibrated IMU, a real impact instant, and (ideally) ball calibration — before any tuning conclusion is drawn.

---

## 1. Guiding principles

- **SwingLab is the substrate.** Every validation re-runs the *production* C++ pipeline on the saved swing dir
  (`makeShotAnalyzer → ImuVisionFuser → PhaseSegmenter → PoseRunner → ShaftTracker → metrics`), unmodified. We never
  reimplement analysis math in Python — the scorecard *judges* outputs, it never recomputes them.
- **Physics invariants carry the load.** The Tier-1/2 scorecard checks need *no labels* and run on all 50 swings — that is
  the soak backbone. Hand-labelling (Tier-3) is reserved for a small subset.
- **Tune params, not code, first.** All analysis knobs — `seg.* / shaft.* / assembly.*` (track/segmentation) **plus
  `score.* / sampler.* / rules.* / bands.* / filter.*`** (scoring, wrist-assessment, orientation re-fusion) — inject from a
  params JSON via `tuningOverrides`, so sweeps iterate at binary speed. Frozen defaults are consolidated in
  `src/Core/pp_tuned_constants.h` (the single freeze edit-point); the full catalog + injection contract is in
  `docs/validation/tunable_parameters_reference.md`. Only a few surfaces stay rebuild-only **code** (live-thread `seed.*`,
  runtime Madgwick-vs-ESKF choice, wrist-axis sign conventions); changing those is an escalation, gated by a reproduced failure.
- **The diff gate is law.** A params change lands only if mean score improves **and** `lab.py diff` reports
  `regressions: 0` (a +3 mean can hide a −20 on two swings). Same-host diffs only (CPU vs CUDA pose differs).
- **Bless before you conclude.** A corpus root needs `CORPUS.md` (date + calibration provenance) or `ingest` won't bless
  it; `calibrated: false` swings are *data* failures, excluded from tuning baselines, never tuned around.

---

## 2. The validation pyramid

```
 Tier 3  truth metrics      labelled subset (10–15 swings) + 1 external-reference swing   ── highest cost, absolute accuracy
 Tier 2  cross-modal        IMU↔vision θ̇ corr, segmentation sanity, repeatability         ── no labels, all 50
 Tier 1  physics invariants coverage, monotonic-t, sweep envelope, peak-rate-near-impact  ── no labels, all 50  (SOAK BACKBONE)
 Tier 0  capture gate       provenance present, calibrated, bindings>0, impact present    ── pre-flight, every swing  ◄ NEW
```

Tier 0 is added by this plan because the existing data fails *capture* gates that the scorecard assumes already passed.

---

## 3. Phase plan (sequence)

| Phase | Goal | Gate to exit |
|---|---|---|
| **C0** | Capture & bless corpus v1 | All 50 swings pass the Tier-0 pre-flight; `CORPUS.md` written; `ingest` marks `blessed: true` |
| **C1** | Baseline run + triage | `run --id baseline`; `REPORT.md` read; every failure classified parametric/data/algorithmic in `TRIAGE.md` |
| **C2** | Self-validation (Tier 1/2) | Tier-1/2 pass-rate measured per subsystem; data-failures pruned from the tuning set |
| **C3** | Ground-truth labelling | `truth.json` on the labelled subset; 1 external-reference swing measured |
| **C4** | Tuning + flag-flip A/B | Each `seg.*/shaft.*/assembly.*/score.*/sampler.*/rules.*/bands.*/filter.*` sweep + each dark flag (K1–K4, R5/R6/R8) decided via the per-swing regression gate on the Tune/Validation partition (§6) |
| **C5** | Acceptance & freeze | Per-subsystem acceptance targets met; defaults updated; scorecard re-baselined; findings written up |

---

## 4. Per-subsystem V&V matrices

Notation: **Self** = no labels needed; **Label** = needs `truth.json`; **Ext** = needs external reference (goniometer/HackMotion/high-speed).

### A. IMU fusion & calibration  (`src/IMU`, `src/Analysis/imu_vision_fuser.*`, `imu_calibration.h`)

| Claim | Check / metric | GT | Knobs (file) | Acceptance target |
|---|---|---|---|---|
| Bindings re-fuse identically live vs offline | A·q·M golden: same (A,M,q_raw) → same q_anat at `imu_instance.cpp:213` and `imu_vision_fuser.cpp:72` | Self | — (golden test) | bit-identical (existing unit test extended) |
| Host-fused quaternion is stable through swing | quaternion unit-norm; gravity-up stays up; no gimbal snap | Self | Madgwick `m_beta=0.05` (`orientation_filter.h:48`); ESKF noise | norm 1±1e-4; `gimbalDropCount` flat |
| Filter seeds correctly from address stillness | convergence < 500 ms from a still seed | Self | `kInitAccelTolG=0.15`, `kInitGyroMaxRadps=0.5`, `kInitMaxSeedAttempts=200` (`imu_base.h:62`) | converged before Takeaway |
| IMU↔vision angular-rate agreement | `xmodal.imu_vision_corr` (Pearson θ̇) | Self | (fusion + shaft assembly) | **≥ 0.9** on bound+calibrated swings |
| Calibration mount gates hold | `axisAngleDeg ∈ [60°,120°]`; composite `calibrated` (anat valid ∧ mount ≤15° ∧ gravity ≤25°) | Self | gate constants (`imu_calibration.h:138`) | all corpus bindings `calibrated: true` |
| Madgwick vs ESKF choice | side-by-side corr + jitter on same swings | Self | `orientationFilter` per stream | pick the higher-corr / lower-jitter default |
| Offline re-fusion reproduces the live quat | re-fuse from recorded raw accel+gyro, warm-started from the stored quat (nominal `dt = 1/outputRateHz`, °/s→rad/s) → geodesic disagreement vs stored | Self | — (parity gate) | `maxDeg < 0.5°`, `warmStarted: true` — the **pre-collection E1 gate** (`corpus1_collection_protocol.md` §0a, tool BUILT: `swinglab_run --refuse-orientation`) |
| Phase-adaptive gain + impact handling | per-phase `imu_vision_corr`; impact-continuity across `impactUs`; bounded net drift at the still finish | Self (+ Ext at C2 vs HackMotion) | `filter.*` (`refuse/adaptive/betaStatic/betaDynamic/accelErrGateG/gyroGateDps/impactBlankPreMs/impactBlankPostMs/accelSatG`, `orientation_refuser.h`) | reference §5.3.1 — `filter.refuse` feeds re-fused orientation into `ImuVisionFuser` so the schedule moves the wrist metric; observable via `xmodal.imu_vision_corr` + `diag.*` + provisional `filter.impact_continuity` |

*Corpus dependency*: every swing must carry **bound, calibrated** lead-forearm + lead-hand IMU (optionally lead-upper-arm).
Without bindings this entire matrix is unrunnable. **The `filter.*` re-fusion schedule is the deepest IMU-path tuning surface
(reference §5.3.1); its *values* await real swings (the synth models no impact shock), but the machinery is built and the E1
parity gate must pass on ~3 pilot swings before collection.**

### B. Phase segmentation  (`src/Analysis/phase_segmenter.*`, `SegmentationConfig`, prefix `seg.*`)

| Claim | Check | GT | Knobs | Target |
|---|---|---|---|---|
| Events monotonic in time | `seg.monotone` | Self | — | 100% of swings |
| Tempo plausible | `seg.tempo_ratio ∈ [1.2, 6.0]` | Self | `backswing*Us`, `top*BeforeImpactUs` | warn-only; distribution sane |
| Top timing accurate | `truth.event_top_s ≤ 0.03 s` | Label | `topMin/MaxBeforeImpactUs`, `transBeforeTopUs` | ≤ 30 ms on labelled subset |
| Takeaway / Finish timing | `truth.event_takeaway_s ≤ 0.08`, `event_finish_s ≤ 0.12` | Label | `takeawayFracOfPeak`, `finish*Us` | within tolerance |
| Address/Finish stillness gates | event presence + confidence | Self | `stillGyroDps=15`, `stillAccelTolG=0.08`, `addressStillMinUs` | events found on all clean swings |

*21 `seg.*` fields, ~14 sweep-worthy* (`phase_segmenter.h:42–85`). Primary sweep targets: `fcEnvelopeHz`, the `top*`/`takeaway*`
gates, `voteAgreeUs`, `finish*` gates.

### C. Shaft tracking + skeleton-aware  (`src/Analysis/shaft_tracker*.{h,cpp}`, `ShaftDetectConfig` `shaft.*`, `AssemblyConfig` `assembly.*`)

| Claim | Check | GT | Target |
|---|---|---|---|
| Track covers the swing | `club.coverage ≥ 0.6` (Measured\|ImuBridged) | Self | ≥ 0.6 on all clean face-on swings |
| Per-frame angle continuity | `track.theta_step < 25°/frame`; `track.len_step`, `track.head_step` | Self | 0 violations on measured neighbours |
| Downswing envelope | `track.downswing_sweep ∈ [86°, 458°]` | Self | within band |
| Peak rate near impact | `track.peak_rate_near_impact` within 120 ms | Self | within window |
| ŝ_hand fit engages when IMU bound | trace last line: `ok/sign/residualRad/framesUsed` | Self | fit `ok`, residual < `calibAcceptRad≈7°` |
| Angle accuracy vs labels | `truth.theta_rms_deg < 3°`; `truth.head_median_px < 25 px` | Label | within tolerance on labelled subset |
| No sub-arm / impossible detections | R5 sub-arm count = 0; R6 envelope-violation count = 0 (proposed Tier-2 checks) | Self | 0 (once flags on) |

*ShaftDetectConfig: 26 knobs (~8 sweep-worthy: `ridgeKernelPx`, `noiseSigmaK`, `thresholdFloor`, `nmsSeparationDeg`,
`clutterMaskDeg`, `minScoreFrac`, `runMaxGapPx`, `interHandSigmaDeg`). AssemblyConfig: 15 knobs (~12 sweep-worthy incl.
`coverageMin`, `jerkPsd`, `transSigma*`, `visionSigmaFloorRad`, calib gates). See `shaft_tracker_math.h:61`,
`shaft_track_assembly.h:101`.*

**Skeleton-aware flag-flip program (K5)** — all default OFF, shipped byte-identical; corpus A/B flips each only on its gate:

| Flag (dotted key) | What it does | Flip gate (measured on corpus) |
|---|---|---|
| `shaft.useArmScale` (K1) | arm-length search radius from pose | coverage ↑ on cropped/low-framed swings, no regressions |
| `shaft.minLenFracOfArm` (R5) | reject sub-arm ridges | **0 legitimate samples** lost; sub-arm FP eliminated |
| `shaft.useKinematicPrior` (K2) | arm-hang directional prior | directional accuracy ↑ on wrist-fallback swings |
| `shaft.useEnvelope` (R6) | hard-reject impossible detections | **0 false rejections** on measured frames |
| `shaft.useBlurMode` (R8) | blur-first in delivery→impact | delivery→impact coverage ↑, off-axis FP flat |
| `shaft.emitPredicted` (R7) | dual measured/predicted output | already ON (additive); residual metric used to bound R8 |

**Pixel-level caveat**: shaft tuning ideally wants **raw frames** (scorecards flag `frames: mp4`). On MP4-only corpus,
shaft timing/coverage/correlation conclusions are valid; sub-pixel θ-accuracy conclusions are *advisory* — see §10.

### D. Shot detection + ball  (`src/IMU/impact_detector.h`, `src/Audio/onset_detector.h`, `src/Gui/shot/shot_arbiter.h`, `src/Pose/ball_*`)

| Claim | Check | GT | Target |
|---|---|---|---|
| IMU impact timing | committed vs true impact, back-dated `kImuBleLatencyUs=30 ms` | Ext (true impact) | bias < 1 frame after calibration |
| Acoustic onset timing | committed vs true, back-dated `audioDeviceLatencyUs=20 ms` | Ext | bias < 1 frame |
| Per-modality precision/recall | TP/FP/FN vs labelled impacts + FP traps | Label | recall ≥ 0.95 real strikes; FP ≈ 0 on mat-taps/waggles |
| Arbiter fusion | ≥2 modalities within 40 ms **or** lone conf ≥0.8; 1.5 s refractory; priority Acoustic>Imu>Ball | Self/Label | matches `arbiter_test` truth table on corpus events |
| Latency auto-calibration (P4) | IMU accel-peak ↔ acoustic-envelope cross-correlation | Self | derive per-source latency offset; compare to fixed constants |
| Ball calibration margin | `ballMargin ≥ 0.15`; `driftAtCapture` low | Self | margin ≥ 0.35 recommended on calibrated streams |

*Blocked*: vision modality (`Source::Ball` via `ballLaunched`) is **not implemented** — vision cannot be a trigger candidate
yet. Validate the IMU+acoustic dyad now; hold vision for the Kalman-track producer.
*Corpus dependency*: needs an **audio stream** and a **true-impact instant** (acoustic onset on clean strikes or a high-speed
contact frame). Sensitivity mapping Low/Med/High → 1.5/1.0/0.7 (`imu_manager.cpp:548`) is the main tuning surface.

### E. Wrist metrics & scoring (M1)  (`src/Analysis/wrist_analyzer.cpp`, `metric_extractor`, `wrist_angle_sampler.h`, `swing_scorer.cpp`)

| Metric (real) | Phases | Validation | GT | Target |
|---|---|---|---|---|
| `leadWristFlexExt` | Address/Top/Impact | plausibility (impact 15–30° more flexed than address); repeatability | Self + Ext | between-swing RMSE < 5° (repeats); < 5° vs reference |
| `leadWristRadUln` | Address/Top/Impact | plausibility (±40° ulnar / ±17° radial clinical); weakest axis | Self + Ext | between-swing RMSE < 8°; < 8° vs reference |
| `forearmPronation` | Top→Impact | smooth trajectory; needs upper-arm IMU | Self | no step at impact; suppressed if slot C absent |
| `leadArmFlexion` | Top/Impact | IMU vs 2D-camera elbow cross-check (~165–180° at impact) | Self | IMU↔2D agreement within tol |
| `score` | — | weights sum 1.0; sub-scores ∈ [0,100]; geometric mean | Self | stable & monotonic w.r.t. metric quality |

*Gimbal handling*: pitch-proxy ≥ `gimbalThresholdDeg=75°` → `Indeterminate` (`wrist_angle_sampler.h`). Expect < 2% of samples.
*Knobs (now dotted-key — no escalation needed)*: sampler `sampler.windowHalfUs` (15 ms) / `sampler.gimbalThresholdDeg` (75) /
`sampler.minValidSamples` (1); scoring bands `score.<metric>.mu/.sigma/.weight/.oneSidedDir` + deadbands
`score.zIn/.zOut/.p` (`swing_scorer.cpp`; frozen in `pp_tuned_constants.h::scoring`, *provisional* — **re-seated at Corpus 2**
against the observed + HackMotion tour-range distribution). Two caveats for the operator: (1) `score.*` moves `r.score` but
has **no Tier-1 scorecard check** — its objective is the HackMotion criterion, so it is swept but validated against HackMotion,
not the lab pass-rate; (2) `sampler.gimbalThresholdDeg` is injectable but currently **inert offline** — the analysis adapter
sets `pitchProxyDeg = 0`, so the Indeterminate gate never fires (open follow-up A.1 #1, reference Appendix A).
*Corpus dependency*: lead-forearm + lead-hand IMU mandatory; lead-upper-arm optional (enables pronation + IMU-elbow).
Repeatability needs **repeated swings of the same motion** by one golfer.

---

## 5. Ground-truth labelling plan (Phase C3)

Tier-3 is the only way to validate *absolute* track/phase accuracy. Budget it deliberately — it is the most expensive evidence.

- **Labelling tool — in-app Markup panel (NOT `lab.py label`).** Per the 2026-06-23 decision, ground-truth labelling is an
  in-app **View-menu stage panel** (`src/Gui/session/PpMarkupPanel.qml`, toggle "Markup" in the session toolbar's View pill):
  select a shot, scrub frame-accurately, click grip→head to lay the club, tag P1–P10, save. It **loads existing truth.json for
  editing** (adjustable: load→edit→save) and writes a sibling **byte-compatible with SwingLab `score.py`** (verified by
  round-trip: writer reproduces the synth ground truth at `theta_rms 0.2°` / `head 2.6px`, score 100). `lab.py label` remains
  a headless fallback.
- **Labelling model — P-positions, not dense frames (decided 2026-06-23).** For each swing, label the golf **P-system
  P1–P10** and place the **club (grip→head) at each P** — ~10 points/swing instead of ~30. This concentrates the expensive
  absolute-truth labels at the biomechanically meaningful, sparse positions; the **dense per-frame track sanity is already
  free via the Tier-1 invariants** (theta_step / coverage / sweep / peak-rate run on all frames, no labels). Net: ~3 min/swing,
  so **many more swings labellable** — which is what the repeatability/distribution-driven plan (no external reference) needs.
  The analyzer is already P-aware (`swing_analysis.h:108`: P3=MidBackswing, P6=Delivery, P9=FollowThrough + Address/Top/
  Impact/Finish), so P-labels map straight onto the segmenter's events:
    - **Shaft @ each P** → `truth.theta_rms_deg` / `truth.head_median_px` (8–10 samples spanning the arc).
    - **P times** → `truth.event_p*_s` (`score.py` maps p1→0, p3→8, p4→2, p6→9, p7→5, p9→11, p10→7; p2/p5/p8 carry shaft-θ
      only + a `truth.parallel_p{2,6,8}_deg` "shaft ~horizontal" label-consistency check).
- **Insurance: a 2–3 swing dense-labelled subset** (every ~10th frame) kept as a high-resolution θ(t) reference to confirm the
  P-sampling doesn't miss between-point errors. The lab supports both workflows (dense placement via stride stepping).
- **Budget: 10–15 P-labelled swings minimum**, and because each is cheap, scale up opportunistically toward the full 50.
- **No concurrent wrist criterion *within Corpus 1 itself*.** Corpus-1 wrist-metric validation is **repeatability +
  plausibility bounds only** — no absolute-accuracy claim — and shot-detection latency constants (30 ms IMU / 20 ms audio)
  stay fixed estimates (P4 cross-correlation can *estimate* an offset from the corpus, but without a true-impact reference it
  can't be validated to ±1 frame). **⚠ Superseded for the wider programme:** HackMotion is now the **Corpus-2 concurrent
  criterion** for FE/RUD/PS, with a **single-session pilot run during Corpus 1** to de-risk the protocol and *size* Corpus 2 —
  see reference §6 and `corpus1_collection_protocol.md` §0b. The "Decided: none" in §10.3 refers to Corpus 1 only.
- **Known-group labels drive the diagnosis knobs.** Scripted-fault and clean/control swings get a `truth.json`
  `meta.knownGroup` tag; this is what makes the new `rules.*` / `bands.*` knobs observable, via `score.py`'s `diag.recall` /
  `diag.clean_no_fault` checks (the offline assessment pass, `runAssessment`). Without the `knownGroup` tag those knobs are
  injected but unmeasured (tunable-params reference §4.2).
- **Self-validating evidence covers the rest**: IMU↔vision correlation, coverage, monotonicity, and repeatability across the
  repeated-swing sets — these run on all 50 with zero labelling and carry the bulk of the validation.

---

## 6. Tuning workflow & discipline (Phase C4)

The loop (binding for operator sessions, encoded in the `/swinglab` skill):

1. `ingest <corpus>` → `run --id baseline` → read `REPORT.md` (mean + worst-first table).
2. **Triage** worst swings from `scorecard.json` (named checks) + `contact_sheet.png` + `trace.jsonl`. Classify each:
   **parametric** (fixable by params), **data** (recapture/exclude — do not tune around), **algorithmic** (escalate).
3. **Parametric fix** → params JSON (dotted keys, any of the eight namespaces) → `run --id candidate --params p.json` →
   **always** `diff baseline candidate`. Keep only if mean ↑ and `regressions: 0`. Prefer `sweep` over >3 hand-iterations —
   the sweep loop now enforces the **per-swing regression gate in-loop** (`--baseline`) and the **Tune/Validation/Held-out
   partition** (`--partition` / `--freeze`): sweep on Tune, select the winner on Validation (nested CV in practice), touch
   Held-out exactly once at freeze. Optimiser: **coordinate descent** (`--method coordinate` — the recommended default for the
   largely-separable corridor/scoring knobs; pass it explicitly, the CLI defaults to `random`); escalate to Bayesian (TPE/GP)
   or CMA-ES (the continuous `filter.*` schedule) only on plateau (reference §7.1).
4. **Record** in `<runs>/TRIAGE.md`; **escalate** via `<runs>/ESCALATION.md` on the mechanical triggers (sweep plateau ×2;
   one invariant failing >30% of corpus; ≥2 identical algorithmic failures; any C++ change needed).

Tier rules: operator runs the loop & edits params/labels only (cap 10 candidate runs/session); engineer engages from an
ESCALATION.md, must reproduce in a test/synth first, rebuild **both** `swinglab_run` and the app, and attach a 0-regression
diff before committing; design-class for algorithm/architecture changes (with design-doc addenda). Never push without approval.

---

## 7. Acceptance criteria (Phase C5 exit gates)

Per subsystem, corpus v1 "passes" when:

- **IMU/calibration**: all bindings `calibrated: true`; `imu_vision_corr ≥ 0.9` on ≥ 90% of bound swings; A·q·M golden bit-identical.
- **Segmentation**: `seg.monotone` 100%; Top error ≤ 30 ms on labelled subset; tempo distribution within [1.2, 6.0].
- **Shaft**: `club.coverage ≥ 0.6` on ≥ 90% of clean face-on swings; `imu_vision_corr ≥ 0.9`; on labelled subset
  `theta_rms < 3°`, `head_median < 25 px`; each K-flag decided (flipped-on with 0 regressions, or documented as deferred).
- **Shot detection**: recall ≥ 0.95 on real strikes; ≈0 FP on the trap set; arbiter matches truth table; latency constants
  confirmed or re-derived within ±1 frame.
- **Wrist metrics**: between-swing repeatability RMSE < 5° (FE) / < 8° (RUD); < 2% Indeterminate; (if reference recorded)
  absolute bias < 5°/8°; scoring bands (`score.*`) reviewed against the observed distribution (re-seated at Corpus 2 vs HackMotion).
- **Orientation re-fusion (`filter.*`)**: re-fusion parity `maxDeg < 0.5°` (E1 gate); per-phase `imu_vision_corr` not
  regressed by the schedule; `filter.impact_continuity` within its (provisional) bound. Schedule *values* are Corpus-1
  self-validated and confirmed against HackMotion *just after impact* at Corpus 2 (reference §5.3.1).

Outputs: locked values edited into `src/Core/pp_tuned_constants.h` (the single freeze edit-point; `tuned_constants_parity_test`
guards byte-identity), a re-baselined scorecard, `TRIAGE.md`/`ESCALATION.md`
archived on SwingData, and per-subsystem sign-off notes appended to the relevant `*_impl.md` docs.

---

## 8. Corpus capture checklist (Phase C0 — the linchpin)

Every swing must clear this **before** it counts toward the corpus. A swing failing any **must** gate is recaptured.

**Must (hard gates — without these the swing is unanalyzable):**
- [ ] Session type = **Wrist (1)**.
- [ ] IMU bound: lead-forearm (slot A) + lead-hand (slot B) connected, streaming, **inline samples present** in swing.json.
- [ ] **`analysis.bindings[]` populated** and `calibrated: true` for each binding (anat valid ∧ mount ≤15° ∧ gravity ≤25°).
- [ ] `calibAgeSec` small (recalibrate per session; reject stale).
- [ ] Analysis populated: `phases[]` includes **Impact (phase 5)**; metrics non-empty; score sane.
- [ ] Face-On camera perspective = 2; (optional) DTL = 1; `athlete.handedness` set.

**Should (raises validation ceiling):**
- [ ] Lead-upper-arm IMU (slot C) → unlocks pronation + IMU-elbow metrics.
- [ ] Ball detection calibrated (`ballDetection.calibrated: true`, `margin ≥ 0.35`) on the face-on stream.
- [ ] Audio stream captured (for shot-detection timing & arbiter validation).
- [ ] IMU output rate 200 Hz (±5 ms impact resolution vs ±10 ms at 100 Hz).

**Stratification of the 50 (recommended):**
- ~20 **repeatability** swings: one golfer, same club/target, in repeated sets of 3–5 (drives repeatability + tuning baseline).
- ~20 **diversity** swings: varied tempo, clubs, and (if available) golfers/handedness.
- ~10 **stress** swings: clutter backgrounds (mat lines, alignment sticks), very fast/slow tempo, extreme wrist ROM.
- **Raw-frame tuning subset (if recordable)**: ~12 swings with `saveRawFrames=ON` for pixel-level shaft tuning (§10).
- **Labelled subset**: 10–15 swings (overlapping the above) for Tier-3.

**Blessing**: write `CORPUS.md` at the corpus root (recording date, calibration provenance, IMU config, raw-subset list,
known deviations). `lab.py ingest` refuses to bless without it.

**Pre-flight validator** (small, high-value): a `lab.py`-side check (or one-off script) run after capture that, per
swing, asserts *imu-stream present ∧ bindings>0 ∧ calibrated:true ∧ impact-phase present*, and prints a red/green table — so a
bad swing is caught at the studio, not three weeks later in a baseline run. `ingest`'s `corpus.json` already surfaces
`calibrated / calibAgeSec / perspectives / ballCalibrated / ballMargin / frames`; this gate adds the bindings/impact asserts.

**Re-fusion parity gate (E1 — BUILT).** Beyond the provenance asserts, run the offline re-fusion parity tool on ~3 pilot
swings *before* committing the studio to the full 50: `swinglab_run <pilot_swing> --out <run> --refuse-orientation` re-runs
Madgwick from each IMU's recorded raw accel+gyro (warm-started from the stored quat) and writes `refusion.json` — pass =
`warmStarted: true` ∧ `maxDeg < 0.5°` per source. This proves the captured data is genuinely post-hoc tunable (the `filter.*`
schedule re-fuses from exactly this raw data); a re-fusion gap discovered after capture wastes the corpus. See
`corpus1_collection_protocol.md` §0a.

---

## 9. Sequencing & effort

| Phase | Effort | Depends on |
|---|---|---|
| C0 capture + bless | studio time (50 swings + calibration discipline) | hardware: 2–3 IMUs, 2 cameras, mic |
| C1 baseline + triage | ~1–2 h (mostly machine time on studio PC, CUDA pose) | C0 blessed |
| C2 self-validation | ~1 day (read scorecards, prune data-failures) | C1 |
| C3 labelling | ~1.5 h labelling + ~1 reference swing | display session |
| C4 tuning + flag-flips | ~1–2 weeks (sweeps, diff gates, flag A/Bs) | C2 + C3 |
| C5 acceptance + freeze | ~1 day (write-ups, default merges, re-baseline) | C4 |

Run batch/sweeps on the Windows studio PC (RTX 5080, CUDA pose, corpus on local NVMe); the Linux box reaches the same data via
`/mnt/swingdata`. **Never diff across hosts.**

---

## 10. Risks & decisions (resolved 2026-06-23)

1. **Native-video = MP4 limits pixel-level shaft tuning.** ✅ *Decided: record a ~12-swing raw subset (`saveRawFrames=ON`).*
   Pixel-level shaft tuning + the K-flag pixel decisions (R5/R6/R8) are in scope on that subset; the rest stay
   coverage/timing/correlation only (scorecard `frames: mp4`).
2. **IMU sensor count.** ✅ *Decided: mixed 2- and 3-sensor.* The 3-sensor swings validate pronation + IMU-elbow; the
   2-sensor swings additionally exercise the **slot-C-absent degradation path** (pronation suppressed, elbow→2D fallback) —
   add a check that this degradation is graceful, not a failure.
3. **External reference.** ✅ *Decided: none **for Corpus 1 itself***. Corpus-1 wrist metrics validate on **repeatability +
   plausibility only**. **⚠ Superseded for the programme:** HackMotion is the **Corpus-2 concurrent criterion** for FE/RUD/PS,
   piloted in a single Corpus-1 session to de-risk + size it (reference §6, `corpus1_collection_protocol.md` §0b). Latency
   constants stay fixed estimates until impact-truth markup (reference §8).
4. **Labelling.** ✅ *Decided: 10–15 swings, labelled via the new in-app Markup Lab* (`src/Gui/markup/`), not `lab.py label`.
5. **Stub analyzers.** Swing/GRF/Coach are stubs; corpus v1 validates Wrist only. Tuning those is future work once they're real.
6. **`ballLaunched` not implemented.** Vision shot-trigger validation is deferred until the Kalman-track producer lands.
7. **Scoring bands are provisional — and now sweepable.** `kWristBands` μ/σ are empirical, exposed as **`score.*` dotted keys**
   (frozen default in `pp_tuned_constants.h::scoring`) and **re-seated at Corpus 2** against the observed + HackMotion
   tour-range distribution. No longer a C++ escalation — but note `score.*` moves `r.score` with **no Tier-1 scorecard check**,
   so it is swept yet validated against the HackMotion criterion, not the lab pass-rate (reference §6.6).

---

## 11. File map

```
tools/swinglab/lab.py                          # ingest / run / one / diff / sweep / label
tools/swinglab/src/swinglab_run.cpp            # offline runner (rebuild after any analysis C++ change; --refuse-orientation)
docs/validation/pipeline_validation_and_tuning.md   # REFERENCE / backbone (3-corpus, sample size, per-stage V&V&T)
docs/validation/tunable_parameters_reference.md     # parameter catalog + injection/sweep developer guide
docs/validation/corpus1_collection_protocol.md      # Corpus-1 capture protocol (E1 re-fusion parity gate, HackMotion pilot)
docs/developer/swinglab_developer_guide.md     # harness reference
docs/developer/swing_export_developer_guide.md # swing-dir / swing.json schema
src/Core/pp_tuned_constants.h                  # FROZEN defaults — single freeze edit-point (seed/scoring/sampler/rules/mount)
src/Analysis/phase_segmenter.h                 # SegmentationConfig  (seg.*)
src/Analysis/shaft_tracker_math.h              # ShaftDetectConfig   (shaft.*)
src/Analysis/shaft_track_assembly.h            # AssemblyConfig      (assembly.*)
src/Analysis/wrist_angle_sampler.h             # PpWristSamplingConfig (sampler.*)
src/Analysis/swing_scorer.cpp                  # kWristBands scoring (score.*)
src/Analysis/assessment_rule.h                 # RuleTuning (rules.*);  reference_bands.h → BandTuning (bands.*)
src/Analysis/wrist_analyzer.cpp                # M1 real Wrist analyzer (segConfigFor / scoreBandsFor / refuseConfigFromTuning)
src/IMU/orientation_refuser.h                  # offline phase-adaptive re-fusion (filter.*)
src/IMU/impact_detector.h  src/Audio/onset_detector.h  src/Gui/shot/shot_arbiter.h
<SwingData>/corpus-v1/CORPUS.md                # blessing (required) + corpus tier + Tune/Validation/Held-out partition
<SwingData>/runs/                              # scorecards, contact sheets, REPORT/TRIAGE/ESCALATION
```
