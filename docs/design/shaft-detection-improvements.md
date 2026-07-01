# PinPoint — Shaft Detection & Tracking: Technical Design

**Status:** Draft for implementation
**Scope:** Detecting and tracking the golf club shaft in 2D video, anchored at the hands, robust to specular gaps and motion blur. Output is a per-frame shaft angle θ(t), angular velocity ω(t), and clubhead position, suitable for downstream swing-plane / tempo / release analysis.
**Non-goals (this phase):** full 3D shaft reconstruction, multi-camera fusion, clubface orientation.

---

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
E_motion(θ, r) = clamp( max(|I_t − B_t|, D_t)(θ, r) / τ_m , 0, 1 )
```

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
θ_meas = (θ_open + θ_close) / 2                 // mid-exposure angle
|ω_meas| = (θ_close − θ_open) / t_exposure      // DIRECT angular speed measurement
sign(ω_meas) = sign(ω̂ from tracker)            // direction from prediction / temporal order
```

5. Timestamp correction: `θ_meas` corresponds to mid-exposure, i.e. time `t_frame_start + t_exp/2`. Feed this timestamp to the filter, not the frame timestamp, if `t_exp` is a significant fraction of the frame interval.

`|ω_meas|` is the highest-value measurement in the whole pipeline: it directly observes the state's second component exactly in the phase (transition → impact) where angle-only tracking is weakest.

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
```

**Angle unwrapping is mandatory**: keep θ continuous (accumulate multiples of 2π); wrap only for the polar lookup. Innovation must be computed as the wrapped difference `wrap(θ_meas − θ̂)` into (−π, π].

### 6.2 Measurements

- Line regime: `z = θ_meas`, `H = [1 0 0]`.
- Wedge regime: `z = [θ_meas, ω_meas]ᵀ`, `H = [[1,0,0],[0,1,0]]`. Use the mid-exposure timestamp.
- Gating: Mahalanobis test at 3σ. Rejected measurement ⇒ coast (predict only) and widen the next search fan.

### 6.3 Track management

- **Coasting budget:** allow up to `k_max` consecutive misses (≈ 0.1 s worth of frames) before declaring track lost; then re-initialise (§8) over the full circle with the last θ as a weak prior.
- **Quality flags per frame:** {LINE_OK, WEDGE_OK, COASTED, REINIT, LOW_SUPPORT, FORESHORTENED} — persisted with the session for downstream analysis honesty.
- **Foreshortening detector:** if the effective usable ray length (frame-clipped or support-weighted length) drops below ~0.3 × estimated shaft length while pose confidence stays high, the shaft is pointing near the optical axis. Inflate `R` (measurement noise) sharply or coast; do not report confident angles from a stub.

### 6.4 Offline smoothing

Primary product is post-swing analysis ⇒ after capture, run a **Rauch–Tung–Striebel smoother** over the stored filter states/covariances. Occluded or blown-out frames are bridged by the smoother; per-frame detection is not required. Real-time display can show the causal filter output; analysis screens use the smoothed track.

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
