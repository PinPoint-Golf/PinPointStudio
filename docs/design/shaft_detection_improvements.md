# PinPoint — Shaft Detection & Tracking: Technical Design

**Status:** Realized — and substantially extended — in the **exemplar**
(`tools/markup/shaft_annotate.py`, v6 working prototype, 2026-07-03). The
**app C++** (`src/Analysis/shaft_tracker*`, the production ShaftTracker) is a
different algorithm that predates/diverges from this design and does **not**
implement it — see *Implementation status* below. Findings from realizing this
design live in [shaft_detection_exemplar_findings.md](shaft_detection_exemplar_findings.md) (fix ids F1–F17 referenced throughout).
**Scope:** Detecting and tracking the golf club shaft in 2D video, anchored at the hands, robust to specular gaps and motion blur. Output is a per-frame shaft angle θ(t), angular velocity ω(t), and clubhead position, suitable for downstream swing-plane / tempo / release analysis.
**Non-goals (this phase):** full 3D shaft reconstruction, multi-camera fusion, clubface orientation.

---

## Implementation status (2026-07-03)

Two implementations exist and MUST NOT be conflated:

- **Exemplar** — `tools/markup/shaft_annotate.py` (Python, offline markup +
  oracle for any future port). Faithful to §3–§8 of this design, then extended
  by seventeen real-footage fixes (F1–F17). This is the validated algorithm.
- **App C++** — `src/Analysis/shaft_tracker{,_math,_assembly}` (production).
  Shares the anchored-ray concept but uses different evidence (morphological
  ridge, no edge-pair, no motion-min), Viterbi association instead of gated-fan
  tracking, and its own KF/RTS assembly. It was wired into Auto Markup once,
  produced confidently-wrong markups, and was reverted — the §A.1 oracle
  contract ("C++ reproduces the exemplar CSV") is currently **unmet** and will
  be honored by a fresh port of the exemplar (template preserved in
  `.claude/attic/auto-markup-2026-07-02/`), not by the production tracker.

| Design item | Exemplar (v6) | App C++ (ShaftTracker) |
|---|---|---|
| §1/§3 anchored 1-DOF ray, polar sampling, r_min hand-skip | ✅ | ✅ concept (per-frame radial scan) |
| §3.2 gated-fan tracking | ✅ | ✖ full-sector candidates + Viterbi instead |
| §3.3 anchor jitter mitigation | ✅ pose-interp + hold-median anchor (F10) | ◐ pose-interp + ±3px rescore grid |
| §4.1 motion evidence `min(\|I−B\|, D)` | ✅ | ✖ (median bg-sub exists behind a default-off flag) |
| §4.2 edge-pair width prior | ✅ (F2 — was missing in v1; the single biggest fix) | ✖ morphological ridge instead |
| §4.3 robust accumulation S(θ)/F(θ) | ✅ + run-start/extent/density gates (F1/F8) | ◐ own scoring, run-length floor |
| §5 line/wedge regimes, online w₀, ω sign-guard | ✅ (direct-ω fusion implemented but needs exposure metadata — see §5.3 note) | ◐ wedge detection behind default-off blur-mode flag |
| §6.1–6.2 constant-jerk KF, wrapped innovation, 3σ gate | ✅ | ✖ different model (2-channel wrap-aware KF in assembly) |
| §6.3 coast budget | ✅ **corrected**: speed-aware, 4 frames when fast (F15 — see note) | n/a (association-based) |
| §6.3 per-frame quality flags | ✅ (FORESHORTENED not yet — face-on only) | ◐ own flag set |
| §6.4 RTS smoothing | ✅ **corrected**: per-segment only (see §6.4 note) | ◐ has RTS; the same continuity caveat is UNVERIFIED there |
| §7 clubhead confirmation, L estimate, head as 2nd track | ◐ distal-blob credit + 180° test only (F6/F16) | ✖ headPx projected, not measured |
| §8.1 address initialisation | ✅ + blob-credit 180° disambiguation (F6) | ◐ own chirality auto-detect |
| §8.2 scale estimation (L₀, w_butt) | ✖ fixed r_max=0.45H, constant w=5px | ◐ arm-derived radius scale (flagged) |
| §8.3 distractor suppression | ✅ **far beyond the doc** — F1, F4, F12, F16, F17 (see corrected §8.3) | ◐ clutter mask + skeleton priors (mostly default-off) |
| §9 DTL / foreshortening | ✖ face-on only | ✖ face-on only |
| §10 fallback tiers | ✅ not built (as directed) | ✅ not built |
| §12 validation | ✅ synthetic + real corpus w/ truth.json scoring; full §12.2 matrix awaits uncropped captures | ◐ SwingLab harness |
| Output honesty (beyond this doc) | ✅ measured/predicted tiers, low-conf discarded (F9) | ✖ conf did not correlate with error (the reversion cause) |

## 1. Problem statement and core reframe

Inputs per frame:

- Grayscale (or RGB) frame `I_t`, resolution ≥ 720p, frame rate 60–240 fps, ideally short exposure.
- Hand/wrist keypoints from the pose model (MoveNet Thunder real-time / ViTPose-B offline), giving a grip anchor point `P_g = (x_g, y_g)` with confidence `c_g`.
- Static camera assumption (studio / tripod). If violated, background-subtraction evidence degrades gracefully; gradient evidence still works.

Because the shaft always passes through the grip, **the shaft is not a free 2D line (2 DOF) but a ray from a known point (1 DOF: angle θ)**. All detection is therefore performed as a search over θ ∈ [0, 2π), with radius r as the integration variable. This single constraint is what makes the specular-gap problem tractable: we never require local continuity, only accumulated evidence along a candidate ray.

Failure modes we must survive:

1. **Specular gaps** — steel shaft alternates bright flash / dark / background-matched along its length. No continuous ridge exists.
2. **Motion blur wedge** — near transition and impact, ω·t_exposure can exceed 10–30°, smearing the shaft into a sector.
3. **Occlusion** — shaft crosses the body, arms, or legs (especially down-the-line view at address and follow-through).
4. **Foreshortening** — shaft points toward/away from the camera (down-the-line view mid-backswing), projecting to a short stub.
5. **Distractors** — alignment sticks, mat edges, club lying on ground, limb edges, netting.
6. **Rolling shutter** — at high ω the shaft images as a curve; no straight-line model fits.

---

## 2. Architecture overview

```
Pose (hands) ──► Grip anchor P_g
                     │
Frame I_t ──► Preprocess ──► Polar unwrap around P_g (§3)
                     │
                     ├──► Motion evidence  E_motion(θ, r)   (§4.1)
                     ├──► Gradient evidence E_grad(θ, r)    (§4.2)
                     │
                     ▼
             Angular evidence function S(θ) (§4.3)
                     │
        Kalman prediction gates search fan (§6)
                     │
                     ▼
     Regime switch: line fit vs wedge fit (§5)
                     │
                     ▼
   Measurement (θ_meas, ω_meas?) ──► EKF update ──► RTS smoother (offline)
                     │
                     ▼
        Clubhead confirmation at distal end (§7)
```

All components are classical CV (OpenCV + Eigen); no training data required. A learned fallback is specified in §10 but should not be built first.

Follow the existing PinPoint pattern: define an abstract `IShaftDetector` base class with a factory, so the classical detector, a future learned detector, and a retroreflective-marker detector are interchangeable backends.

---

## 3. Polar unwrap

### 3.1 Definition

Given anchor `P_g` and maximum radius `R_max`, define the polar image:

```
U(i, j) = I( x_g + r_j · cos(θ_i),  y_g + r_j · sin(θ_i) )

θ_i = i · Δθ,          i = 0 … N_θ − 1,   Δθ ≈ 0.25°–0.5°  (N_θ = 720–1440)
r_j = r_min + j · Δr,  j = 0 … N_r − 1,   Δr = 1 px
```

- `r_min` ≈ 0.5 × hand-width in pixels (skip the hands themselves; pose keypoints and glove pixels are noise here).
- `R_max` = estimated shaft length in pixels + margin (see §8.2 for length estimation at address). Clamp to frame bounds per ray.
- Bilinear sampling (`cv::remap` with precomputed maps when `P_g` is quantised to a grid, or direct sampling otherwise).

In `U`, a straight shaft through `P_g` is a **vertical line**; a blur wedge is a **vertical band**. The 2D detection problem is now a 1D peak-finding problem over columns.

### 3.2 Gated unwrap

Do **not** unwrap all 360° every frame. Given the tracker's predicted angle θ̂ and innovation covariance, unwrap only the fan `θ̂ ± max(3σ_θ, 10°)` plus the predicted blur width. Full 360° sweep is used only for (re-)initialisation (§8).

Complexity per frame: `O(N_θ_fan × N_r)` samples — for a ±15° fan at 0.25° and 600 px radius, ≈ 72k samples. Negligible.

### 3.3 Sub-pixel anchor jitter

Pose keypoints jitter by several pixels. Mitigate:

- Anchor at the midpoint of left/right wrist keypoints; temporally smooth `P_g` with a light constant-velocity Kalman filter (or reuse the existing wrist Kalman filtering already planned for PinPoint).
- Anchor error of ε pixels causes angular error ≈ ε / r; evidence integrated at larger radii is intrinsically more accurate. The radius weighting in §4.3 exploits this.

---

## 4. Evidence functions

Three per-sample cues are computed on the unwrapped grid, then robustly accumulated per column.

### 4.1 Motion evidence

Maintain a running background model `B_t` (running median over ~2 s, or `cv::BackgroundSubtractorMOG2` with slow learning rate; a plain exponential running average is acceptable for a static studio camera). Also compute the 2-frame absolute difference `D_t = |I_t − I_{t−1}|`.

```
E_motion(θ, r) = clamp( min(|I_t − B_t|, D_t)(θ, r) / τ_m , 0, 1 )     // min, NOT max
```

> **Validated finding (reference impl):** the combination MUST be `min`, not `max`.
> With `max`, the shaft burns into the background during the address hold and its
> *ghost* (|I−B| at the vacated position) sustains a confident false lock inside the
> gated fan while the real shaft leaves the fan entirely. A ghost has ≈zero frame
> difference; a genuinely moving shaft has both channels high — `min` kills the ghost
> and keeps the shaft. A briefly-static shaft (top of backswing) then has zero motion
> evidence, which is fine: the gradient channel carries it.

- `τ_m` ≈ 15–25 grey levels (auto-tune from background noise: 4× MAD of `D_t` over a background region).
- Motion evidence is **immune to specularity**: it does not care whether a shaft segment is blown out, dark, or mid-grey — only that it differs from background.
- Caveat: the moving arms also generate motion. This is handled by (a) the search fan gating, (b) the width prior in gradient evidence (arms are far wider than a shaft), and (c) starting integration at `r_min` beyond the hands.

### 4.2 Gradient orientation coherence + width prior

Compute Sobel gradients on the original image, sample `(g_x, g_y)` into the unwrap. For a ray at angle θ, the ray direction is `d = (cosθ, sinθ)` and its normal is `n = (−sinθ, cosθ)`.

Per-sample orientation-coherent gradient magnitude:

```
G(θ, r) = |∇I| · cos²(∠∇I − ∠n)         (gradient should be ⟂ to shaft axis)
```

Implementation without atan2: `G = (∇I · n)² / (|∇I| + ε)`.

**Edge-pair width prior.** A shaft of projected width `w(r)` produces two antiparallel edge responses separated by `w(r)` across the ray. In the unwrap, examine the small angular neighbourhood of each column: for each `r`, look for a positive `(∇I · n)` response at `θ − δ` and negative at `θ + δ` (or vice versa) where `δ(r) ≈ w(r) / (2r)` radians.

```
E_grad(θ, r) = sqrt( max(0, +(∇I·n)(θ−δ, r)) · max(0, −(∇I·n)(θ+δ, r)) ) / τ_g
             clamped to [0, 1]
```

- Shaft width model: shafts are conical, ~15 mm at butt to ~9 mm at tip. In pixels: `w(r) = w_butt − (w_butt − w_tip) · r / L`, with `w_butt`, `L` (shaft length in px) estimated at address (§8.2). If unavailable, a constant `w = 3–6 px` prior works at 720p–1080p.
- The paired-edge form strongly suppresses single edges (limb silhouettes, mat lines) and wide structures (forearms).

### 4.3 Robust angular accumulation

**Do not average.** Averaging lets long gaps drag down a true shaft; robust counting does not.

Per-column combined sample score:

```
s(θ, r) = max( E_motion(θ, r),  E_grad(θ, r) )        // OR-combination
```

(OR, not AND: during the blur wedge, gradient evidence exists only at the wedge edges while motion evidence fills the interior; at address, motion evidence is zero and gradient evidence carries everything.)

Column score with radius weighting and clamped accumulation:

```
S(θ) = Σ_r  w_r(r) · min(s(θ, r), s_cap)      with s_cap ≈ 0.8
       ────────────────────────────────
              Σ_r  w_r(r)

w_r(r) = r / R_max     // linear radius weight: far samples are angularly more precise
                       // and less contaminated by hand/arm pixels
```

Also track the **support fraction**:

```
F(θ) = (# samples with s(θ,r) > 0.3) / N_r
```

A detection is accepted only if `F(θ*) > F_min` (≈ 0.25). This is the formal statement of "40% visible in disconnected chunks still wins": gaps reduce `F` but a shaft visible over a quarter of its length in any distribution of chunks passes, while an isolated bright distractor spanning 5% of the ray does not.

Smooth `S(θ)` with a small Gaussian (σ ≈ 0.5°) before peak analysis.

---

## 5. Line vs wedge measurement models

### 5.1 Regime selection

Predicted blur width from the tracker: `β̂ = |ω̂| · t_exposure` (radians). Exposure time from camera metadata; if unavailable, estimate once from an observed wedge (§5.3) or assume `t_exp = 1/(2·fps)`.

- `β̂ < β_switch` (≈ 2°): **line regime**.
- `β̂ ≥ β_switch`: **wedge regime**.

Always verify against the observed profile: measure the width of the region `{θ : S(θ) > 0.5·S(θ*)}`; if it disagrees with the regime by >2×, trust the observation.

### 5.2 Line regime

- θ* = argmax S(θ) within the gated fan.
- Sub-degree refinement: parabolic interpolation on `S` around the peak, **or** (better) a weighted total-least-squares line fit through `P_g` on the raw supporting sample coordinates:

```
θ_meas = atan2( Σ w_k · (y_k − y_g)·sign_k ,  Σ w_k · (x_k − x_g)·sign_k )
```

where the sum runs over samples with `s > 0.3` in the peak's angular neighbourhood, `w_k = s_k · r_k`. (In practice the 1D parabolic refinement on S(θ) is sufficient; implement it first.)

- Measurement noise: `σ_θ ≈ Δθ / sqrt(F(θ*) · N_r)` scaled empirically; also inflate by anchor jitter term `σ_anchor / r̄`.

### 5.3 Wedge regime — blur as signal

During exposure the shaft sweeps from `θ_open` to `θ_close`. The unwrapped evidence is a **band** whose edges are sharp (the shaft's instantaneous positions at shutter open/close), interior partially filled (each interior angle was occupied for only a fraction of the exposure, so it is dimmer).

Procedure:

1. Compute `S(θ)` over the gated fan (widened by predicted `β̂ + 5°` on the leading side).
2. Band segmentation: threshold at `0.4 · max S`; take the connected angular interval containing the predicted θ̂.
3. Edge refinement: compute `dS/dθ`; `θ_open` and `θ_close` are the extremal derivative locations at the band boundaries, refined by parabolic interpolation.
4. Measurements:

```
θ_meas = (θ_open + θ_close) / 2                        // mid-exposure angle
band_blur = max(0, (θ_close − θ_open) − w₀)            // subtract intrinsic width!
|ω_meas| = band_blur / t_exposure                       // DIRECT angular speed measurement
sign(ω_meas) = sign(ω̂ from tracker)                    // only when |ω̂| is well above zero
```

> **Validated findings (reference impl):**
> 1. **The raw band width is biased.** It includes the shaft's intrinsic angular
>    width in the S(θ) profile (a ~5 px shaft at r=100 px subtends ~3°) plus the
>    smoothing kernel. Uncorrected, ω is overestimated ~2× and destabilises the
>    filter (overshoot → gate rejections → oscillation). Calibrate w₀ online: measure
>    the band width at the same threshold during line-regime frames with |ω̂|·t_exp
>    < 0.5° (EMA). Use ω_meas only when band_blur > ~1.5°.
> 2. **Sign guard at reversal.** Near the top of the swing ω̂ ≈ 0 and the
>    sign-from-prediction rule is unreliable; one wrong-signed ω measurement there can
>    run the filter off by a full revolution (it then re-locks mod 360° with a broken
>    unwrap). Suppress the ω component of the measurement when |ω̂| < ~200°/s; use the
>    θ component only.

5. Timestamp correction: `θ_meas` corresponds to mid-exposure, i.e. time `t_frame_start + t_exp/2`. Feed this timestamp to the filter, not the frame timestamp, if `t_exp` is a significant fraction of the frame interval.

`|ω_meas|` is the highest-value measurement in the whole pipeline: it directly observes the state's second component exactly in the phase (transition → impact) where angle-only tracking is weakest.

> **[2026-07-03 status]** Implemented in the exemplar but **inactive**: ω-fusion
> is gated on knowing the exposure time and the capture pipeline does not record
> it. Recording exposure in swing.json capture metadata is a cheap, high-value
> action for the next capture round.

### 5.4 Rolling shutter caveat

If the sensor is rolling-shutter and ω is high, rows of the frame sample the shaft at different times → the shaft images as a curve and both models mis-fit. Detection: line-regime residuals grow systematically with row index. Mitigations, in order of preference: (a) require/recommend global shutter or very short exposure in capture guidance; (b) inflate measurement noise when high ω + rolling shutter is known; (c) (later) per-row time model — out of scope for v1. Log a per-frame quality flag.

---

## 6. Temporal tracking

### 6.1 State and process model

Extended (actually linear) Kalman filter on:

```
x = [ θ, ω, α ]ᵀ        // angle (unwrapped, continuous), angular velocity, angular accel

F(dt) = [ 1  dt  dt²/2 ]
        [ 0  1   dt    ]
        [ 0  0   1     ]

Q: white-noise jerk model, q_jerk tuned so the filter tolerates the downswing
   (peak α for a driver swing ~ 2000–4000 °/s² is NOT rare; ω peaks ~ 2000–2500 °/s
   ≈ 35–45 rad/s at impact for fast swings — size Q from these, not from gentle motion).
   Validated: σ_jerk = 2.5e5 °/s³ tracked a 2160 °/s-peak synthetic swing at 120 fps
   with zero coasts; 5e4 was too tight (coasted through the downswing).
```

**Reference implementation:** `tools/shaft_annotate.py` (Python/OpenCV) implements
§3–§8 end-to-end and is the oracle for the C++ port: on the synthetic harness it
achieves θ RMSE 0.49° (smoothed), ω RMSE ~2% of peak, 120/120 frames tracked at 35%
specular dropout. The C++ implementation should reproduce its CSV output on the same
inputs to within measurement noise.

**Angle unwrapping is mandatory**: keep θ continuous (accumulate multiples of 2π); wrap only for the polar lookup. Innovation must be computed as the wrapped difference `wrap(θ_meas − θ̂)` into (−π, π].

### 6.2 Measurements

- Line regime: `z = θ_meas`, `H = [1 0 0]`.
- Wedge regime: `z = [θ_meas, ω_meas]ᵀ`, `H = [[1,0,0],[0,1,0]]`. Use the mid-exposure timestamp.
- Gating: Mahalanobis test at 3σ. Rejected measurement ⇒ coast (predict only) and widen the next search fan.

### 6.3 Track management

- **Coasting budget:** allow up to `k_max` consecutive misses (≈ 0.1 s worth of frames) before declaring track lost; then re-initialise (§8) over the full circle with the last θ as a weak prior.

  > **[2026-07-03 correction — exemplar F15]** 0.1 s is far too long at speed:
  > 15 coast frames at 2000°/s is ~200° of blind drift. The budget must be
  > speed-aware (exemplar: 12 frames when \|ω̂\| < 800°/s, else 4), plus a
  > runaway-kill for accepted-but-insane states (wedge-band ω inflation).
- **Quality flags per frame:** {LINE_OK, WEDGE_OK, COASTED, REINIT, LOW_SUPPORT, FORESHORTENED} — persisted with the session for downstream analysis honesty.
- **Foreshortening detector:** if the effective usable ray length (frame-clipped or support-weighted length) drops below ~0.3 × estimated shaft length while pose confidence stays high, the shaft is pointing near the optical axis. Inflate `R` (measurement noise) sharply or coast; do not report confident angles from a stub.

### 6.4 Offline smoothing

Primary product is post-swing analysis ⇒ after capture, run a **Rauch–Tung–Striebel smoother** over the stored filter states/covariances. Occluded or blown-out frames are bridged by the smoother; per-frame detection is not required. Real-time display can show the causal filter output; analysis screens use the smoothed track.

> **[2026-07-03 CRITICAL correction — exemplar]** The smoother must run
> **per continuous track segment, never across a re-initialisation**: an init
> frame's pseudo-prediction (P_pred = P₀) is not a real prediction, and the
> backward recursion through it detonates numerically (observed smoothed ω of
> 1e18–1e49 through junk-coast chains). The exemplar tags every frame
> init/tracked/free and smooths only within contiguous init→tracked runs.
> The app C++ assembly has its own RTS — whether it shares this defect is
> **unverified** and must be checked before that code is trusted or ported.

---

## 7. Clubhead confirmation

At the distal end of the accepted ray:

1. Search an annulus `r ∈ [0.85·L, 1.1·L]` along θ* ± 3° in the motion mask.
2. Find the largest connected blob; centroid = clubhead measurement `P_h`.
3. Uses:
   - **Length update:** running estimate of shaft pixel length `L` (slow EMA; L is constant in 3D but its projection varies — track projected length only for gating, and record max observed as the full-length estimate).
   - **180° disambiguation:** at (re)initialisation, the true direction is the one with a clubhead blob at its distal end; the butt end has none.
   - **Second tracked point:** feed `P_h` into its own 2D constant-acceleration KF — this is the clubhead-path/speed measurement the analysis screens ultimately want, and it cross-checks ω (|P_h − P_g| · ω ≈ clubhead tangential speed).

---

## 8. Initialisation at address

### 8.1 Detection

Trigger when pose is stable (wrist velocity < threshold for ~0.5 s). Run the full-circle unwrap with gradient evidence only (no motion at address). At address the shaft is static and sharp — this is the easy case. Take the global peak of `S(θ)` subject to `F > F_min` and plausibility prior (shaft points down-ish: dot product of ray with image-down > 0, generous threshold to handle camera roll).

### 8.2 Scale estimation

- Locate the clubhead blob (or grass/ball contact region) along the detected ray → shaft pixel length `L₀`, butt width `w_butt` from the edge-pair separation near `r_min`.
- These seed the width prior `w(r)` (§4.2) and the clubhead annulus (§7).
- Optional cross-check against pose skeleton scale (e.g. hip–shoulder pixel distance vs anthropometric ratios) for sanity.

### 8.3 Distractor suppression at init

Alignment sticks and clubs on the ground also pass through no anchor — they generally do **not** intersect `P_g`, so the anchored formulation already rejects most of them. Residual risk: a stick coincidentally aligned through the hands. Mitigate with the clubhead-blob check and, once motion starts, the fact that static distractors have zero motion evidence.

> **[2026-07-03 correction — this claim was too optimistic.]** Real studio
> footage produced FIVE confident-wrong distractor classes that all pass the
> anchored formulation: the club's own shadow on the lit mat (killed by the
> run-start gate F1), permanent bright strips aligned with a grip ray — neon
> door frame (scene-permanence veto F12), the golfer's own torso/leg lines and
> lit trouser seams (body-collinearity gate F16), the vacated position of a
> previously-hanging club, fed by the seam behind it (clear-candidate
> preference F17), and permanently wrong locks sustained by the gated fan
> itself (escape rescan F4). None are hypothetical; every one was observed and
> adjudicated frame-by-frame. See shaft_detection_exemplar_findings.md §2/§6/§7.

---

## 9. Per-view considerations

- **Face-on (caddy view):** best case. Shaft stays roughly in the image plane through most of the swing; foreshortening only briefly at the top.
- **Down-the-line:** shaft points near the optical axis through mid-backswing and mid-downswing. Expect FORESHORTENED flags there; the track is naturally strong at address, top (shaft ~parallel to image plane again for many players), and through impact. The RTS smoother bridges the foreshortened windows; downstream metrics for DTL should be defined only on well-observed phases (e.g. shaft plane at address vs impact).
- The 2D θ(t) is a **projected** angle. Document this clearly in the analysis layer; do not present projected ω as true 3D angular velocity without labelling. (Future work: fuse two views or IMU for 3D.)

---

## 10. Fallback tiers (do not build first)

1. **Classical pipeline above** — build and evaluate this fully first.
2. **Learned segmentation assist:** small U-Net (e.g. 256×256 crop around hands) predicting a shaft-probability mask, used as a third evidence channel `E_learn` in §4.3 — *not* as a replacement for the geometric machinery. Training data: synthetic renders (trivial to generate: line + cone + motion blur + specular noise composited over studio frames) + a few hundred hand-labelled frames. Keep the ONNX-runtime pathway consistent with the existing ViTPose integration.
3. **Retroreflective marker mode:** two tape bands at known fractional positions on the shaft + ring light ⇒ blob-pair detection, sub-pixel, near-unbreakable. Offer as an optional "high accuracy mode"; shares the same `IShaftDetector` interface and downstream tracker. (Avoid IR illumination if a GCQuad is present — visible-band ring light only.)

---

## 11. Implementation notes

- **Language/libs:** C++17/20, OpenCV (remap, Sobel, MOG2), Eigen (KF). No Qt dependency in the detector core — pure C++ library consumed by the QML/C++ app, unit-testable headlessly. Factory-registered behind `IShaftDetector`.

  > **[2026-07-03 status]** Not yet done to this spec. The existing production
  > `ShaftTracker` is a different algorithm (see *Implementation status*) and
  > must not be mistaken for the port of this design. A first-generation port +
  > full app wiring (driver, truth.json conf schema, async controller, QML) is
  > preserved in `.claude/attic/auto-markup-2026-07-02/` as the template; the
  > port target is exemplar v6, gated on the findings doc's verification
  > protocol including confidence-honesty checks.
- **Threading:** detector runs on the existing capture/processing worker thread; per-frame budget target < 2 ms at 1080p for the gated path (trivially achievable — the unwrap fan is ~10⁵ samples).
- **Precompute:** cos/sin tables per θ bin; regenerate remap maps only when `P_g` moves > 1 px.
- **Determinism:** given identical input video + pose, output must be bit-identical (no wall-clock dependence) for regression testing.
- **Data recording:** persist per frame: θ_filtered, θ_smoothed, ω, quality flag, F(θ*), band width, clubhead point. This is the contract with the analysis screens.

### 11.1 Suggested module layout

```
shaftdetect/
  IShaftDetector.h            // ABC + factory (PinPoint pattern)
  ClassicalShaftDetector.{h,cpp}
  PolarUnwrapper.{h,cpp}      // §3  — remap maps, gated fan
  EvidenceComputer.{h,cpp}    // §4  — motion, gradient, S(θ), F(θ)
  WedgeFitter.{h,cpp}         // §5  — regime switch, line/wedge fits
  ShaftTracker.{h,cpp}        // §6  — KF, RTS smoother, track management
  ClubheadDetector.{h,cpp}    // §7
  AddressInitializer.{h,cpp}  // §8
  types.h                     // ShaftMeasurement, ShaftState, QualityFlag
tests/
  synthetic/                  // §12.1 generator + golden tests
```

---

## 12. Validation strategy

### 12.1 Synthetic harness (build this first)

Render synthetic swings: parametric θ(t) (e.g. scaled real kinematic-sequence profile), draw a conical anti-aliased line from a moving anchor, apply: exposure-integrated motion blur (accumulate N sub-exposure renders), specular dropout (random along-shaft masking, 20–70% removed in chunks), sensor noise, moving "arm" occluders, static distractor lines. Ground truth θ, ω known exactly.

Metrics: angle RMSE (deg) vs blur level and dropout fraction; ω RMSE in wedge regime; track-loss rate; re-init latency.

Acceptance targets (initial): θ RMSE < 1° (smoothed) at ≤ 40% dropout and blur ≤ 15°; ω from wedge within 10% of truth; zero track losses on clean synthetic swings.

### 12.2 Real footage

- Record a matrix: {face-on, DTL} × {60, 120, 240 fps} × {auto exposure, 1/1000 s} × {steel, graphite} in the studio.
- Manual annotation of ~20 frames/swing (address, takeaway, top, mid-down, impact, follow-through) for spot RMSE.
- Regression suite: golden outputs on committed clips; determinism (§11) makes this exact.

### 12.3 Known-hard cases to include from day one

Shaft-behind-body frames (DTL), full specular blowout frames, alignment stick through the frame, club change mid-session (length re-estimation), left-handed player (no handedness assumptions anywhere), anchor dropout (pose confidence dip).

---

## 13. Symbol table

| Symbol | Meaning |
|---|---|
| `P_g` | grip anchor (wrist midpoint), pixels |
| `θ, ω, α` | shaft projected angle, angular velocity, acceleration |
| `U(i,j)` | polar unwrap, angle × radius |
| `E_motion, E_grad` | per-sample evidence channels ∈ [0,1] |
| `S(θ)` | robust column score; `F(θ)` support fraction |
| `w(r)` | expected projected shaft width at radius r |
| `t_exp` | exposure time; `β = ω·t_exp` blur band width |
| `θ_open, θ_close` | wedge edge angles (shutter open/close) |
| `L` | shaft length, pixels (projected; max observed ≈ full) |
| `R` , `Q` | KF measurement / process noise covariances |

---

## Appendix A — Reference implementation: `shaft_annotate.py`

> **[2026-07-03: this appendix describes v1 and is SUPERSEDED.]** The current
> exemplar is v6 (same file, evolved through F1–F17). The v1 figures below are
> synthetic-harness results; on real footage v1 locked onto the club's shadow at
> address and stayed wrong for entire swings ("zero false locks" did not
> survive contact with reality). Current behaviour, results and limits:
> shaft_detection_exemplar_findings.md §2–§7 and
> ../implementation/shaft_markup_exemplar_impl.md.

This appendix documents the Python reference implementation for developers — primarily as a guide for the C++ port, and for anyone tuning or extending the tool. It implements §3–§8 of this document in ~400 lines (Python 3, OpenCV, NumPy; no other dependencies). It is single-file and single-pass-plus-smoother by design: readability over performance.

### A.1 Purpose and role

1. **Markup generator** — produces per-frame shaft annotations (video overlay + CSV) so validation footage does not need hand labelling; human review is directed only at flagged frames via the contact sheet.
2. **Oracle for the C++ port** — given the same video and anchor input, the C++ implementation should reproduce the CSV columns `theta_smooth` / `omega_smooth` to within measurement noise. The Python is deliberately written as a close structural match to the intended C++ module layout (§11.1).

Validated on the synthetic harness (`make_synth.py`): θ RMSE 0.49° smoothed, ω RMSE ≈ 2% of a 2160°/s peak, 120/120 frames tracked, 35% specular dropout, zero false locks.

### A.2 Data flow

```
video ──► per-frame loop ──────────────────────────────┐
  │   AnchorSource.get()          → grip (x,y)          │  pass 1 (causal)
  │   evidence_scan()             → S(θ), F(θ)          │
  │   fit_peak() / fit_wedge()    → θ_meas [, band]     │
  │   KF.step()                   → x=[θ,ω,α], flag     │
  │   background update (EMA)                           │
  └────────────────────────────────────────────────────┘
      KF.rts()                    → smoothed track          pass 2 (batch)
      overlay render + CSV + review contact sheet           pass 3 (presentation)
```

Frames are retained in memory between passes (`frames_raw`) so the overlay can be drawn with the *smoothed* track. For long clips this is the first thing to change (see A.9).

### A.3 Configuration constants (top of file)

| Constant | Doc ref | Meaning / validated value |
|---|---|---|
| `DTHETA = 0.25` | §3.1 | angular bin (deg) |
| `R_MIN_FRAC = 0.06` | §3.1 | r_min as fraction of frame height (skips hands) |
| `F_MIN = 0.22`, `S_ACCEPT = 0.12` | §4.3 | detection acceptance: support fraction and column score |
| `TAU_G, TAU_B, TAU_M` | §4 | evidence normalisation scales (gradient / brightness / motion, grey levels) |
| `S_CAP = 0.8` | §4.3 | per-sample clamp in the robust accumulation |
| `BAND_THR = 0.4` | §5.3 | wedge band segmentation threshold (fraction of peak) |
| `BETA_SWITCH = 2.0` | §5.1 | predicted blur (deg) above which the wedge model is used |
| `COAST_MAX = 12` | §6.3 | consecutive misses before track loss / re-init |
| `GATE_SIGMA = 3.0` | §6.2 | Mahalanobis gate |
| `FAN_MIN = 10.0` | §3.2 | minimum half-fan (deg) |
| `R_THETA = 1.5²` | §5.2 | base θ measurement variance (deg²) |
| `SIGMA_JERK = 2.5e5` | §6.1 | process noise (deg/s³) — validated; 5e4 coasts through the downswing |

`TAU_M` (motion) is the constant most likely to need adjustment on new footage; raise it if sensor noise or flicker produces spurious motion evidence.

### A.4 `class KF` — tracker and smoother (§6)

Constant-jerk linear Kalman filter on `x = [θ, ω, α]` in **degrees**, θ kept **unwrapped** (continuous across ±180°).

- `F` and `Q` are built once from `dt`; `Q` is the standard white-jerk discretisation (the 3×3 polynomial-in-dt matrix scaled by `SIGMA_JERK²`).
- `init(theta)` — seeds the state with generous `P` (ω, α unknown).
- `step(z, R, H)` — one predict/update. Accepts `z = None` for coasting. The θ innovation is wrapped via `wrap180()` **before** gating — this is the single most bug-prone spot in any port; get it wrong and the filter unwinds by whole revolutions. Gating is a χ² test at `GATE_SIGMA² · dim(z)`; a gated-out measurement coasts.
- Every call appends `(x, P, x_pred, P_pred)` to `self.hist` — the exact quantities the RTS pass needs.
- `rts()` — standard Rauch–Tung–Striebel backward pass over `hist`. Note the smoother gain uses the *stored* predicted covariance `Pp1`, and the state correction difference also passes through `wrap180` on the θ component.

C++ port note: this maps directly onto `ShaftTracker` (§11.1) with Eigen; keep degrees or switch to radians consistently — the constants above are in degrees.

### A.5 `class AnchorSource` — grip anchor (§3.3)

Three modes, resolved in this priority:

1. **`csv`** (`--anchors file.csv`, rows `frame,x,y`) — anchors exported from the pose pipeline. Missing frames hold the last position and set `ok = False`. This is the intended production mode.
2. **`lk`** (default with `--grip X Y`) — pyramidal Lucas–Kanade tracking of the single grip point (31×31 window, 3 levels). Requires `uint8` images (a float frame will assert inside OpenCV — hence the internal `gray8` conversion). Tracking failure (status 0 or error > 40) holds the last position rather than jumping.
3. **`constant`** — fixed anchor; adequate for tripod footage with quiet hands, and for the synthetic harness.

Known limitation: LK degrades when the hands themselves motion-blur; expect drift through the downswing on real footage. Prefer CSV anchors for anything quantitative. The anchor is *not* currently smoothed inside this tool (the design doc §3.3 suggests a light KF on the anchor — do this in the C++ version, or pre-smooth the CSV).

### A.6 `evidence_scan()` — §3 + §4 in one function

Per call (i.e. per frame), over a caller-supplied angle set `thetas_deg`:

1. Sobel gradients and magnitude on the full frame (candidate optimisation: restrict to the fan's bounding box).
2. **Motion image** — the validated anti-ghost form: `min(|I − B|, |I − I_prev|)` (§4.1). First frame falls back to `|I − B|`.
3. Polar sampling: builds `X, Y` sample grids for all (θ, r) pairs and uses `cv2.remap` with bilinear interpolation — one remap per field (gx, gy, |∇|, motion). `valid` masks off-frame samples; rays are effectively clipped per-ray at the frame edge.
4. Evidence channels: orientation-coherent gradient `E_grad = clamp(((∇I·n)²/|∇I|)/τ_g)` and motion `E_mot`; combined with **max** (the OR-combination of §4.3 — do not confuse with the **min** inside the motion channel itself).
5. Robust accumulation with radius weights `w = r`, per-sample clamp `S_CAP`, plus support fraction `F(θ)`; `S` is then smoothed with a 9-tap Gaussian (σ = 2 bins).

Deviation from the doc: the **edge-pair width prior** (§4.2) is *not* implemented here — plain orientation coherence proved sufficient on synthetic + studio stills. Implement the pair prior in C++ if real footage shows locks onto single-edge structures (forearm silhouettes, mat edges).

Also note the still-image experiments used a brightness channel (`I − blur(I)`) in place of motion; in the video tool motion replaces it. If you need single-frame operation, reinstate `E_bright` behind a flag.

### A.7 `fit_peak()` / `fit_wedge()` — §5

- `fit_peak`: argmax of smoothed `S` with 3-point parabolic sub-bin refinement (guarded against a degenerate second difference).
- `fit_wedge`: walks outward from the peak until `S` falls below `BAND_THR · S_peak`; returns `(θ_open, θ_close)`. Deliberately simple; the doc's derivative-extremum refinement (§5.3 step 3) is a straightforward upgrade if band-edge noise matters.

### A.8 Main loop — regime logic, flags, calibration

Per frame, after anchor lookup:

- **Fan selection** (§3.2): if initialised, predict and scan `θ̂ ± (max(3σ_θ, FAN_MIN) + β̂/2 + 2°)`; else full 360° (or ±45° around `--seed-deg` on frame 0).
- **Acceptance**: `good = S_peak > S_ACCEPT ∧ F > F_MIN`.
- **Initialisation**: first `good` frame seeds the KF → `REINIT`.
- **Regime switch** (§5.1): `wedge = |ω̂|·t_exp > BETA_SWITCH`. `t_exp` is `--exposure` if given, else assumed `dt/2` (regime switching still works; direct ω measurement does not — see next point).
- **Wedge branch** (§5.3, with the two validated corrections):
  - `band_blur = max(0, band − w0)` — subtracts the calibrated intrinsic profile width;
  - ω measurement used only when `--exposure` was given **and** `band_blur > 1.5°` **and** `|ω̂| > 200 °/s` (sign guard at reversal). Otherwise θ-only update with doubled `R_THETA`.
- **Line branch** (§5.2): θ-only update. When effectively static (`|ω̂|·t_exp < 0.5°`) the same band fit is run to **calibrate `w0`** (EMA, 0.9/0.1) — the static width later subtracted in the wedge branch.
- **Measurement unwrap**: every measurement is re-expressed near the prediction as `z = θ̂ + wrap180(θ_meas − θ̂)` before entering the filter.
- **Flags**: `LINE_OK` / `WEDGE_OK` (accepted), `COASTED` (gated out or regime fell through), `LOW_SUPPORT` (evidence too weak), `REINIT`, `LOST` (after `COAST_MAX` misses; triggers full-sweep re-initialisation).
- **Background update**: `cv2.accumulateWeighted(gray, bg, 0.02)` — slow EMA, no fancy model. The anti-ghost `min` in the evidence is what makes this safe.

### A.9 Outputs and CSV schema

| Column | Meaning |
|---|---|
| `frame`, `t_s` | frame index, timestamp (frame start) |
| `theta_filt`, `omega_filt` | causal KF estimate (deg, deg/s) |
| `theta_smooth`, `omega_smooth` | RTS-smoothed — **use these for analysis/labels** |
| `S_peak`, `support` | evidence quality at the accepted peak (§4.3) |
| `band_deg` | raw wedge band width (NaN in line regime) — *not* de-biased; subtract w0 yourself if consuming it |
| `flag` | quality flag (above) |
| `grip_x`, `grip_y` | anchor used for this frame |

Overlay video: red ray = smoothed θ (yellow when the frame's flag is not OK — i.e. the smoothed value there is interpolation, not measurement); orange rays = wedge edges; blue dot = anchor; HUD text = frame/θ/ω/flag. Review sheet: up to 24 flagged frames tiled 6-wide.

Caveats for a developer extending this:
- θ_smooth is reported per frame-start timestamp; the doc's mid-exposure timestamp correction (§5.3 step 5) is **not** applied. At `t_exp = dt/2` this is a ω·dt/4 phase bias — negligible for markup, worth doing properly in C++.
- All frames are held in memory (three-pass structure). For multi-minute clips either stream pass 1 to disk or accept causal-only overlay.
- `mp4v` fourcc is used for portability; swap for `avc1` if your OpenCV build has H.264.

### A.10 Porting checklist (Python → C++ modules of §11.1)

| Python | C++ module | Watch out for |
|---|---|---|
| constants block | config struct | keep degrees vs radians consistent |
| `AnchorSource` | pose adapter / `IAnchorSource` | anchor smoothing (add), uint8 for LK |
| `evidence_scan` | `PolarUnwrapper` + `EvidenceComputer` | remap map reuse when anchor moves < 1 px; **min** in motion channel, **max** across channels |
| `fit_peak`/`fit_wedge` | `WedgeFitter` | parabolic guard; band threshold relative to peak |
| `KF` (+`rts`) | `ShaftTracker` | `wrap180` on every θ innovation and RTS correction; store predicted P for RTS |
| main-loop regime logic | `ShaftTracker` policy layer | w0 calibration, sign guard, `t_exp` provenance |
| overlay/CSV | app layer | smoothed-vs-causal distinction in UI |

Determinism requirement (§11) applies to the port: same video + anchors ⇒ bit-identical CSV. The Python tool satisfies this (no wall-clock or RNG in the pipeline; the only RNG is in `make_synth.py`, which is seeded).
