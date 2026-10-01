# The Shaft Tracker — Developer Guide

**Scope.** Everything in PinPoint Studio that turns camera pixels into a statement about the golf club's shaft. That covers:

- the face-on tracker;
- the down-the-line (DTL) tracker;
- the two-view 3-D fusion;
- the synthetic tracks (the face-on Layer C synth and the 3-D synthetic DTL line);
- the club-length and clubhead machinery hanging off the tracker;
- the metrics read from all of the above.

**State of the code.** `main` at `f434ecb2`, 1 October 2026.

**Who this is for.** Developers and domain experts who need to know what the code *actually does*, why, and where it is weak. The guide assumes no prior knowledge of the algorithms or the mathematics. §3 is a primer that every later section leans on. If you already know dynamic programming, isotonic regression, Kalman smoothing and projective geometry, skip it.

**How to read it.**

- §1–§2 explain the problem and the constraints the design was built against.
- §3 is the maths primer.
- §4 walks the face-on tracker stage by stage in execution order.
- §5 covers the DTL tracker, §6 the 3-D fusion and the swing planes, and §7 the synthetic tracks.
- §8 lists every metric the shaft feeds.
- §9 covers plumbing: pipeline, reuse, persistence, GUI, tools and tests.
- **§10 is the shortcomings section, and it does not tread lightly.**
- §11 is a glossary.

**Sources.** Every number quoted comes from a code comment, a design document or a commit message, and the source is named where it matters.

**Unit conventions used throughout.**

- **θ (theta)** is the shaft's image angle. It is the direction from the grip toward the clubhead in the image, measured with `atan2(dy, dx)` in image pixels, where **y points down**. So θ = 0° points image-right, θ = 90° points straight *down*, θ = 180° points image-left and θ = 270° points straight up.
- **φ (phi)** is the lead forearm's image angle in the same convention, measured from the elbow toward the grip.
- Angles in the code are degrees in the decide half and radians in the samples (`ShaftSample2D::thetaRad`).
- Times are microseconds (`t_us`).
- At 150 fps one frame is ~6.6–6.7 ms.

---

## Contents

1. [What the shaft tracker is for, and what it produces](#1-what-the-shaft-tracker-is-for-and-what-it-produces)
2. [The constraints: what the camera actually sees](#2-the-constraints-what-the-camera-actually-sees)
3. [A primer on the mathematics](#3-a-primer-on-the-mathematics)
4. [The face-on tracker, stage by stage](#4-the-face-on-tracker-stage-by-stage)
5. [The down-the-line tracker](#5-the-down-the-line-tracker)
6. [Two views into three dimensions: fusion and the swing planes](#6-two-views-into-three-dimensions-fusion-and-the-swing-planes)
7. [The synthetic tracks](#7-the-synthetic-tracks)
8. [The metrics the shaft feeds](#8-the-metrics-the-shaft-feeds)
9. [Plumbing: pipeline, reuse, persistence, GUI, tools, tests, switches](#9-plumbing-pipeline-reuse-persistence-gui-tools-tests-switches)
10. [Shortcomings](#10-shortcomings)
11. [Glossary](#11-glossary)

---

## 1. What the shaft tracker is for, and what it produces

### 1.1 The job

A golf swing filmed at 150 fps gives ~750 frames over 5 s. In each frame the club is somewhere between a crisp thin line (at address, at the top), a bloomed ribbon, and a translucent fan smeared over 20–30° of arc (through impact, where the clubhead travels at 70–100 mph). The tracker's job is to say, for every frame:

- **where the grip is**;
- **which way the shaft points** (θ);
- **how long it looks** (the projected length, which gives the head position);
- **how much that statement can be trusted**.

From that per-frame track it then derives:

- the swing's **P-positions** (P1 address … P8 shaft-parallel through, plus P10 finish);
- a smooth **synthetic** track between them;
- a **club length** estimate;
- inputs to a family of **metrics**: shaft lean, lie, plane, attack angle, low point, clubhead speed, lag, kinematic sequence.

The design principle that runs through every file is **honesty over coverage**:

- A frame is only called "measured" when the pixels earned it.
- Everything else is labelled for what it is: coasted, predicted, held, end-on, occluded or synthesised.
- A track whose independent witnesses contradict it is *refused*. It draws nothing, and its metrics show "–".

The project got there the hard way. The first C++ tracker (June 2026) "produced confidently-wrong markups and was reverted", because its confidence "did not correlate with error". On one swing it locked onto a shadow at the mat edge and "stayed wrong for the entire swing" (`docs/research/club_detection_from_video.md`, Phase 1).

### 1.2 The three trackers and how they relate

```
               face-on camera                              down-the-line camera
                     │                                              │
   pose (ViTPose WholeBody 133-pt) ─┐                pose (DTL) ────┤
   ball track ──────────────────────┤                               │
   club record (length, bands…) ────┤                               │
                                    ▼                               ▼
                       ┌──────────────────────┐   FaceOnWitness  ┌───────────────────┐
                       │ FACE-ON TRACKER       │ ───────────────► │ DTL TRACKER        │
                       │ ShaftTracker /        │  (one-way: time, │ DtlShaftTracker /  │
                       │ decideTrack           │   visibility,    │ dtlSolve /         │
                       │ → ShaftTrack2D        │   ball gate      │ dtlPostSolve       │
                       │   samples, positions, │   timing)        │ → DtlShaftTrack2D  │
                       │   synth, lengths,     │                  │   samples, bands   │
                       │   wedgeObs, plane     │                  └─────────┬─────────┘
                       └──────────┬───────────┘                            │
                                  │                                          │
                                  └──────────────┬───────────────────────────┘
                                                 ▼
                                   ┌──────────────────────────┐
                                   │ FUSION  fuseTracks       │
                                   │ → Track3D: 3-D direction │
                                   │   per DTL frame, back /  │
                                   │   down planes, η(t),     │
                                   │   address plane          │
                                   └──────────┬───────────────┘
                                              ▼
                       3-D synthetic DTL line (synth3d), Skeleton3D, metrics
```

- **Face-on** (`src/Analysis/shaft_tracker.*`, `shaft_tracker_math.*`, `shaft_track_assembly.*` and helpers) is the primary instrument. It runs on every swing that has a face-on camera.
- **DTL** (`src/Analysis/dtl_shaft_*`) runs only when a second camera exists. It takes *timing* and a *visibility schedule* from face-on through a read-only `FaceOnWitness`, and nothing ever flows back. That one-way rule is deliberate: "A witness that has been fitted to the thing it testifies about is not one" (`dtl_shaft_tracker.h`).
- **Fusion** (`src/Analysis/shaft_fusion.h`) reads both and feeds neither. It produces the shaft's 3-D direction and the swing planes.

### 1.3 Where it runs

The analysis pipeline is `wristProfile()` in `src/Analysis/wrist_analyzer.cpp`. The shaft stages run in this order:

1. Pose, PoseSmooth, Ball.
2. **ShaftStage**, which calls `ShaftTracker::track`, or *reuses* a persisted track (§9.2).
3. **ImpactAnchor**.
4. **ShaftLean**.
5. Event refine and the positions ladder, timeline fusion.
6. Club delivery and kinematics.
7. **ShaftPlane**.
8. DtlPose.
9. **DtlShaft**, then **ShaftFusion**, then DtlPosture, then **DtlShaftLie**.
10. KinematicSequence, then **Skeleton3D**, then **DtlSynth3D**.

Wall time: the face-on shaft stage is ~3 s on the M4 after parallelisation (10.8 → 3.1 s), and the DTL stage is ~2.2 s.

---

## 2. The constraints: what the camera actually sees

The algorithms only make sense against the physical situation they were built for. Every design decision below traces back to one of these facts.

### 2.1 Cameras and timing

**Face-on camera.**

- FLIR Chameleon3, 1280×1024, Bayer RG8, **150.713 fps**.
- Exposure ~**6.57 ms** out of a 6.70 ms frame period, a duty cycle of **98%**.
- About 3.5 mm/px at the club.
- Mark's ruling (5 July 2026): 150 fps is the camera's maximum and the exposure "cannot shorten (light budget)". The ring light at the camera and the studio downlight are the illumination. This is listed under "do not revisit" (`markerless_club_tracker_design.md` §8).

**Consequences of a 98% duty cycle.**

- The shaft is exposed for essentially the whole inter-frame interval.
- Near impact the club rotates at **15–20°/frame (2,200–3,000°/s)**, so each frame integrates a fan of shaft positions about the grip. A real club peaks around 2,500°/s.
- Image velocity grows with distance from the grip, so the shaft near the hands is sharp and the outer shaft and head smear.
- **Bare steel through the impact zone is effectively invisible**: "Impact is *unmeasurable* on a bare club at this exposure" (research report §16).

**Timestamps.** Frame timestamps come from the host (Spinnaker frames are host-stamped, ±0.4 ms jitter), and the declared container fps can lie. One corpus session declares 30 fps for a 150 fps stream. **Every tracker computes its timebase from the median inter-frame interval of the actual timestamps, never from the container fps** (`shaft_tracker.cpp`, `dtl_shaft_tracker.cpp`).

**DTL camera.**

- Present on only **34 of the 115** corpus swings (sessions 06-11, 07-03, 07-04). Nothing from 07-05 onward has one.
- 512–576 px wide, 1024 tall, ~149.3 fps.
- No ring light. The background is the lit simulator screen.
- It sits roughly on the ball–target line, "at head height or above", rather than on the hands line at hand height. Its placement was never measured. The alignment stick says yaw 4–9° and roll 2.4–6.5°, and the camera moved between 07-04 swings 3 and 4.
- DTL frames lead the face-on frames by a median 3.2 ms. No offset is applied; the witness interpolates across the phase difference.

### 2.2 The golfer and the pose

- The pose comes from **ViTPose WholeBody (133 keypoints)**. "Medium" analysis uses ViTPose-B and "High" uses ViTPose-L. Until 29 September a Windows path silently fell back to B for High swings.
- The tracker never sees the club in the pose. It sees the **hands**: the grip anchor is the mean of the two per-hand keypoint centroids. It also sees the **forearms** (elbow→grip) and an **8-joint body** (shoulders, hips, knees, ankles).
- **Hand-keypoint confidence is not trustworthy.** The model does not say when it is wrong (memory: "hands unreliable").
- The pose grip anchor sits a median **39 px off the shaft axis** face-on and 17–22 px off it down the line. The hands wrap the shaft, and their centroid is not on it. Much of the snap (§4.14), the lateral head band and the DTL lateral-origin search exists because of this offset.
- Pose runs at roughly the camera rate offline and 60 Hz live. Its frames are linearly interpolated to camera timestamps (`lerpPoseFrame`, `shaft_frame_io.h`).

### 2.3 The club

**The club record.**

Each analysis carries a club record (`ShotAnalysisJob`):

- `clubLengthM` — note the **default is 1.12 m, a driver**, when no club is recorded;
- `hoselFromButtMm`;
- `shaftLengthMm`;
- `handsEndMm`;
- `bandCentersMm`.

The lab 7-iron is 940 mm long with the hosel at 882 mm. From 4 July 2026 it carries six 25 mm glass-bead retro-reflective bands at 308, 362, 560, 758, 808 and 854 mm from the butt, a 2-1-3 pattern. A later unmarked 6-iron (955 mm, hosel 892 mm) is the markerless reference.

**What makes a shaft visible.**

- Under a ring light, steel is a bright specular line on a dark background.
- Over a blown-white mat it is a *dark* line on bright: the polarity flips.
- Bands saturate into discrete blobs when lit, but only "club-up". At address the ring-lit return barely saturates.
- Down the line, without a ring light, black tape and white paint over a mid-grey screen *cancel* any signed ridge detector. This is the **polarity trap** (§5.4).

### 2.4 The ball

The ball is the club's best far-end witness:

- at address the head sits behind it, so grip→ball is the shaft direction to within a few degrees;
- at impact the shaft passes through it.

It is found by a separate ball detector (`BallTrack2D`). Ball-dependent rules all degrade to "do nothing" when the ball is missing or untrustworthy. Several sessions break that degradation: the 15 Sept impact-camera rig hid the ball, and some sessions have two balls on the mat.

### 2.5 The data the whole thing was validated on

**One golfer (Mark), right-handed**, in one studio, with essentially:

- one taped 7-iron;
- one untaped 7-iron session in daylight;
- one unmarked 6-iron session;
- a gap wedge down the line.

The corpus is **115 swings**, with a 61-swing "pinned pose" sub-corpus used for regression gating.

Instant-level truth is thin:

- 13–14 hand-marked swings for impact timing;
- 7 unmarked-6-iron swings and ~32 corpus swings with hand-marked shaft frames;
- band-lock "truth" on taped frames, which is itself a tracker output with a 0.3° self-consistency rather than an independent instrument.

"This corpus (one club/camera/athlete) validates machinery, not accuracy claims" (`shaft_position_first_design.md` §6). Keep that sentence in mind through every number in this guide.

---

## 3. A primer on the mathematics

This section explains, from first principles, every mathematical tool the tracker uses. Each subsection says where in the code the tool appears.

### 3.1 Angles, wrapping and unwrapping

An angle is periodic: 359° and 1° are 2° apart, not 358°.

- **Wrapping** maps any angle into a canonical interval. `circWrap(a)` maps to (−180°, 180°] and is used for *differences*. `fmod(a, 360)` maps to [0°, 360°) and is used for stored θ.
- The difference of two angles must always be wrapped before it is compared. `|circWrap(θ1 − θ2)| < tol` is the idiom you will see hundreds of times.

**Unwrapping** solves the opposite problem. A swing rotates the shaft through more than 360°, but a sequence of wrapped angles jumps by ~360° every time it crosses the branch cut. Unwrapping walks the sequence and adds ±360° whenever consecutive samples differ by more than 180°. The result is a continuous angle you can differentiate, interpolate or fit a curve through. This is `np.unwrap` (`shaftshared::unwrap`).

Interpolating two angles without unwrapping can go "the long way round" the circle. The DTL witness keeps an unwrapped copy of face-on θ for exactly this reason (`dtl_face_on_witness.cpp`).

### 3.2 Robust statistics: median, percentile, Hampel

- The **median** is the middle value of a sorted sample. Unlike the mean it ignores outliers, so a few wild values do not move it.
- A **percentile** pₖ is the value below which k% of a sample falls. p50 is the median, and p97 is "nearly the top but not the single largest".
- The tracker uses percentiles as *scale* references because the maximum is fragile: one bright pixel or one blurred frame sets it.
- A **Hampel filter** compares each sample with the median of its neighbours and replaces it if it deviates by more than a threshold. `smoothPhi` does this to the forearm angle: any frame more than 20° off its 9-frame median is replaced (pose glitches spike φ by up to 87° in one frame).

### 3.3 Smoothing filters

Each of these is a reimplementation of a SciPy function so that the C++ matches the Python exemplar it was ported from.

- **Median filter** (`medianFilter1d`). Replace each sample with the median of a window around it, size 5 or 9. Removes spikes and preserves steps.
- **Gaussian filter** (`gaussianFilter1d`). Replace each sample with a weighted average of its neighbours. The weights are a bell curve exp(−k²/2σ²) truncated at 4σ, which removes high-frequency jitter.
- **Reflect boundary.** At the ends of the series the window is filled by mirroring the series (…c b a | a b c…), so the ends are not biased toward zero.

The tracker's standard smoothing is "median-5 then Gaussian σ=2". It is applied to grip speed, joints, the φ-rate and ω.

### 3.4 Cost functions and dynamic programming (the Viterbi algorithm)

The central problem: in each of ~750 frames there are 360 possible shaft directions (one per degree). The evidence in each frame is ambiguous: the arm, a leg, the mat edge and the shaft all look like lines. But the shaft *cannot jump*. Between consecutive frames it moves a bounded amount, in a direction set by the phase of the swing. We want the single *sequence* of directions that best balances "agrees with the evidence in each frame" against "moves plausibly between frames".

Write it as a **cost** to minimise. For a path θ₀, θ₁, …, θₙ₋₁:

```
Cost(path) = Σ_f  E_f(θ_f)  +  Σ_f  T(θ_{f-1} → θ_f)
```

- **E_f(θ)** is the **emission cost**: low where frame f's pixels look like a shaft at angle θ, and high where they do not or where a physical rule forbids θ.
- **T** is the **transition cost**: a smoothness penalty k·Δθ² for each move, and infinite (forbidden) outside the allowed band.

There are 360ⁿ possible paths, far too many to enumerate. **Dynamic programming** exploits the structure: the best path ending at state k in frame f must extend *some* best path ending in frame f−1. So:

```
C_f(k) = E_f(k) + min over allowed j of [ C_{f-1}(j) + T(j → k) ]
```

Compute C₀ = E₀, then C₁, C₂, … one frame at a time. At each step record *which* j achieved the minimum (the **back-pointer**). At the last frame take the cheapest k, then follow the back-pointers backwards to read off the whole path.

This is the **Viterbi algorithm**. Its cost is frames × states × allowed moves. With 750 frames × 360 states × up to 49 moves that is ~13 M operations: trivial.

It finds the **globally optimal** path. A frame where the evidence is ambiguous is resolved by what its neighbours can reach. That is why a single frame with a strong arm-line cannot pull the track off the shaft, and why the tracker's errors tend to be *whole wrong stretches* rather than single-frame glitches (see §10).

"**Banded**" means the allowed moves per frame are restricted: |Δθ| ≤ w_max(f), and optionally only increasing or only decreasing. This both enforces physics and speeds the search. The implementation is `shaftshared::viterbiBanded` in `shaft_track_assembly.cpp`. The angle grid is circular: state 359 neighbours state 0.

A **negative** emission cost (a "well") is a reward. The band lock writes −8 into a single state, which pulls the global path through that state.

### 3.5 Isotonic regression and the Pool-Adjacent-Violators algorithm

Given noisy values y₁…yₙ that should be **monotone** (never decreasing), isotonic regression finds the non-decreasing sequence x₁…xₙ closest to y in weighted least squares, Σ wᵢ(xᵢ − yᵢ)².

The **Pool-Adjacent-Violators Algorithm (PAVA)** solves it exactly in linear time:

1. Walk left to right.
2. Whenever a value is greater than its right neighbour (a "violator"), merge the two into a block whose value is their weighted mean.
3. Step back one block and check again, because the merge may have created a new violation.
4. At the end every block is constant and the blocks are non-decreasing.

That is `pava()`. For a non-increasing fit, negate, fit and negate back.

**Robust (Huber) isotonic regression.** Least squares lets one far-off value drag a whole block. *Iteratively reweighted least squares (IRLS)* with a **Huber** weight fixes that:

1. Fit.
2. Compute each residual r.
3. Give points with |r| > c (the "knee", here 8°) the reduced weight w·c/|r|.
4. Refit. Repeat 3 times.

Large residuals then count linearly rather than quadratically. That is `robustIsotonic()`.

The tracker uses this on ψ = θ − φ, the angle of the shaft relative to the forearm (the wrist hinge). Physically ψ should increase monotonically through the backswing (the wrist cocks) and decrease through the downswing (it releases). See §4.9.

### 3.6 The Kalman filter and RTS smoother

A **Kalman filter** tracks a hidden state, for example a radius r and its rate ṙ, from noisy measurements. At each step it:

- **predicts**: the state moves according to a motion model (here constant velocity plus random acceleration), and its uncertainty grows;
- **updates**: when a measurement arrives, it blends prediction and measurement weighted by their uncertainties. The more certain one counts more.

A measurement whose disagreement with the prediction exceeds a few standard deviations (the **innovation gate**, 3σ here) is rejected as an outlier.

The **Rauch–Tung–Striebel (RTS) smoother** runs a second pass backwards over the filter's stored history. Each estimate then uses future measurements as well as past ones, which removes the lag a forward-only filter has.

The clubhead's radial position along the shaft ray (§4.11) is tracked this way (`HeadKf1D`).

### 3.7 Splines: cubic Hermite and Fritsch–Carlson

To draw a smooth curve through known points (the P-anchors) where you also know the *slope* at each point, use a **cubic Hermite** segment. Between two points (t_a, p_a, slope m_a) and (t_b, p_b, m_b), with τ = (t − t_a)/(t_b − t_a) ∈ [0, 1]:

```
p(τ) = (2τ³−3τ²+1)·p_a + (τ³−2τ²+τ)·m_a·h + (−2τ³+3τ²)·p_b + (τ³−τ²)·m_b·h
```

where h = t_b − t_a. It passes exactly through both points (**C⁰**) with exactly the given slopes (**C¹**: value and first derivative continuous).

A cubic can **overshoot**: between two points it may bulge beyond both. **Fritsch–Carlson limiting** prevents that for monotone data:

- zero any endpoint slope whose sign opposes the overall change;
- if the normalised slopes (α, β) fall outside the circle α² + β² ≤ 9, scale them back inside it.

The curve is then monotone between the anchors. That is `limitMonotone` in `shaft_synthesis.h`.

### 3.8 Least squares, normal equations, Cholesky

Many fits in the code minimise a sum of squared residuals over a small set of unknowns x. Write the residuals as linear in x, ‖Ax − b‖². Setting the gradient to zero gives the **normal equations** AᵀA x = Aᵀb.

AᵀA is symmetric and positive definite when the problem is well posed, so it factors as **LLᵀ** with L lower-triangular (the **Cholesky decomposition**). The solve is then two triangular back-substitutions.

Adding a penalty such as Σ(second difference)², which says "the curve should not bend sharply", just adds more rows. The synthetic track's evidence fit (§7.1.6) and the fusion's η(t) curve (§6.4) are both penalised least squares solved this way.

### 3.9 Conics, the SVD, and why an ellipse is a plane

If something moves on a circle in a plane tilted relative to the camera, the camera sees an **ellipse**. The ratio minor/major axis equals the cosine of the tilt. So fitting an ellipse to the image path of a quantity that sweeps a circle **recovers the plane's inclination** from a single camera.

The general conic is ax² + bxy + cy² + dx + ey + f = 0. Each data point gives one equation linear in the six coefficients. The best-fitting coefficient vector v (with ‖v‖ = 1) is the **right singular vector of the smallest singular value** of the design matrix D = [x², xy, y², x, y, 1]. That is the direction in which D "squashes" the most, the closest thing to a null vector. This is what `fitConic` computes with a one-sided Jacobi **SVD** (singular value decomposition).

The conic is an ellipse iff b² − 4ac < 0. Its axes come from the eigenvalues of [[a, b/2], [b/2, c]] (§6.6).

### 3.10 Vectors, planes and cross products

In 3-D, the **cross product** u × v is perpendicular to both u and v, and its length is |u||v|·sin(angle between them).

- The normal of a plane containing two directions is their cross product.
- The intersection line of two planes is the cross product of their normals.

**The view-plane idea (§6).** A camera sees a line in the image at angle θ. The 3-D shaft could lie anywhere in the **plane** that contains the camera's viewing direction and the image line. A second camera gives a second such plane. The shaft lies on both, so its direction is the cross product of the two plane normals. Where the two planes are nearly the same plane, that cross product is tiny and its direction meaningless. Its length is the **conditioning** ("cond").

**A plane through the origin fitted to many unit directions** is the eigenvector of the scatter matrix Σ uuᵀ with the **smallest eigenvalue**: the direction in which the data has the least spread. That is `smallestEigenvector` (cyclic Jacobi rotations on a 3×3).

### 3.11 Orthographic projection

The fusion and synth3d code uses an **orthographic** camera model. A camera is three unit vectors: view direction d, image-right, image-down. A 3-D direction u projects to the image vector (u·right, u·down). Its image angle is atan2(u·down, u·right), and the projected length fraction is the length of that 2-D vector.

This ignores perspective (things nearer the lens look bigger), which is a stated approximation. At address and impact the clubhead is ~0.5 m nearer the face-on lens than the hands, and perspective makes the club read 10–17% long (`dtl_face_on_witness.cpp`).

### 3.12 Score normalisation: what "evidence 0..1" means

The ridge detector (§4.5.1) produces a raw score per direction in arbitrary units that depend on lighting. To compare directions within a frame, the tracker rescales each frame's 360 scores as:

```
norm(s) = clip( (s − p50) / (p97 − p50), 0, 1 )
```

The typical direction maps to 0 and the near-best to 1. This makes frames comparable, but it hides absolute strength: **a frame of pure noise still produces a "winner" at 1.0**. This is the single most important honesty trap in the evidence layer. `evAbsFloor` (§4.5.2) exists to close it.

---

## 4. The face-on tracker, stage by stage

### 4.0 The code map

| File | Role |
|---|---|
| `shaft_tracker.h/.cpp` | `ShaftTracker::track`: the SwingWindow layer. Derives anchors from pose, builds the frame decoder, runs the hands ladder, calls `decideTrack`, then the ball anchor. |
| `shaft_tracker_math.h/.cpp` | **Evidence engines**: E2 ridge sweep, E1 band match, E4 steel-segment lock, and the ray profile. Pure maths, core OpenCV only. |
| `shaft_track_assembly.h/.cpp` | **The deciding half**: `ShaftV3Config`, the hands-only phase model, φ smoothing, body geometry, emission, Viterbi, ψ reconcile, tiering, length ladder, head pass, placement, snap, positions, synth, self-checks. `decideTrack()` is the ~1,750-line core. |
| `shaft_track_shared.h` | The view-independent half shared with the DTL tracker: filters, unwrap, percentile/normalisation, snap search, `viterbiBanded`, the frame-cache cap. |
| `shaft_wedge.h` | Blur-wedge plateau and edge measurement. |
| `shaft_kinematics.h` | The R6 double-pendulum predictor (wrist-cock table). |
| `shaft_positions.h` | P-time location (Layer B1) and the address-hold end. |
| `shaft_position_fit.h` | The milestone fit (Layer B2) and the shared ridge line integral. |
| `shaft_synthesis.h` | Layer C synthesis and evidence fit. |
| `shaft_hand_clean.h` | Hand-track cleaning. |
| `ball_anchor.h/.cpp` | Grip→ball series, the address-hold club length, and the post-hoc ball anchor. |
| `impact_geom.h` | P7 from the club-at-ball crossing. |
| `clubhead_track.h/.cpp` | Stage-2 measured clubhead (terminus walk plus 1-D Kalman/RTS). |
| `club_length_fusion.h` | Multi-estimator length fusion and the persistent prior. |
| `hand_axis.h` | Hand centroids and the hand-axis direction. |
| `shaft_frame_io.h` | Pose interpolation, Bayer decode and the parallel frame cache. |

Parameters live in `ShaftV3Config` (`shaft_track_assembly.h`). Each field defaults to a validated constant and can be overridden by a dotted key such as `shaft.wE2` or `shaft.seg.enabled` from a tuning map, so SwingLab sweeps need no rebuild. Several onset constants live in `src/Core/pp_tuned_constants.h` (`tuned::shaft::`).

**The "dark idiom".** Almost every feature has a switch whose OFF position is *bit-identical* to the code before the feature existed. Features land dark, pass a corpus gate, and are then "frozen ON". The commit and gate that flipped each one are in `docs/developer/feature_switches_developer_guide.md`.

### 4.1 Orchestration: `ShaftTracker::track`

1. **Coverage frames.** The camera frames whose timestamps fall inside the pose track's time range. Fewer than 2 means the track is invalid.
2. **Timebase.** fps = 1 / median inter-frame interval.
3. **Hand cleaning on a copy of the pose** (`shaft_hand_clean.h`), with two rules:
   - **Pair consistency.** When both hands are still and their centroids are further apart than one lead-forearm length (or 64 px with no confident forearm) for at least 4 consecutive frames, pick the hand nearer the grip point predicted from the lead forearm. Without a forearm, pick the *lower* hand. Built for 16 Sept W02 s2 on ViTPose-B, where the lead-hand centroid sat on the *wrist*.
   - **Glitch rejection.** A one-frame jump of more than 40 px that comes straight back, from a hand resting before and after, is replaced by the mean of its neighbours. Applied only before impact.
4. **The hands ladder.** Track on the **raw** hands first. *Only* if that track comes back invalid or refused, *and* cleaning changed something, track again on the cleaned hands, and adopt the second track only if it is valid and unrefused. The reason is in the comment: applying cleaning to every swing "moved the onset and P1 of clean 09-09 swings whose poses flicker just as much (the onset heuristics were tuned on that flicker)".
5. **Per-frame anchors** (`derive`):
   - grip = mean of the two hand centroids (px);
   - φ = atan2(grip − lead elbow), valid when elbow conf > 0.30 and the forearm is longer than 8 px;
   - the same for the trail elbow;
   - the 8 body joints;
   - optionally the hand-axis direction (dark, §4.6 row 7).
   - Pose frames are linearly interpolated to each camera timestamp.
6. **Decode-once frame cache.** Gray frames decoded in parallel, up to a 1,200 MiB cap. Above the cap it falls back to serial decode.
7. **Club geometry** (`SegmentGeom`):
   - hosel = record, else length − 58 mm;
   - grip end = hosel − shaft length, else 265 mm;
   - band centres;
   - hands end.
   - The camera's recorded exposure is passed to the wedge.
8. **`attempt(pose)`.** Run `decideTrack`, then `applyBallAnchor` (§4.19), then re-sample a track-sampled P1 if the ball anchor rewrote the sample it was read from.
9. **Refusal 4.** If the pair rule fired on more than half the pose frames, "the pose has no grip to offer", and the track is refused.

### 4.2 Smoothing the forearm, chirality

- φ is gap-filled by linear interpolation (`interpFillNan`), then smoothed by `smoothPhi`: unit-vector components, Hampel rejection at 20°, median-9, then Gaussian σ=3. Working on (cos, sin) rather than the angle avoids wrap problems.
- The trail-forearm angle gets the same treatment when present.
- **Chirality** `chir` is the sign of the unwrapped φ change from takeaway to top. It is +1 or −1 depending on which way the arm rotates in the image, i.e. handedness *as seen*. Handedness is never read from a setting for this purpose.

### 4.3 The hands-only phase model (`segmentPhases`)

The tracker needs its own phase labels, because the Viterbi's allowed moves depend on them. It derives them from the **grip trajectory alone** ("C3", vision-only; the IMU segmentation passed in is ignored).

**Step 1 — motion runs.**

- Grip speed per frame = |Δgrip|, smoothed by median-5 and Gaussian-2.
- A *run* is a stretch above `swSpd` = 8 px/frame that is at least 7 frames long.

**Step 2 — run hygiene.** In order:

- **Capture-hole clip.** After impact, an inter-frame gap longer than 3 frame periods is a capture drop. Runs are clipped at it, because post-hole frames carry the stalled host's arrival times.
- **Run bridging.** Runs separated by fewer than 10 quiet frames merge (`onsetRunBridgeFrames`). A slow backswing fragments into bursts on the lerped pose.
- **Late clamp.** Runs starting more than 1 s after the supplied impact are dropped (post-finish fidgeting).
- **Early clamp.** Runs ending before impact − 1.6 s are dropped (address waggles).
- **m3gate.** A run bridged from 3 or more raw runs must have net displacement ≥ 0.2 × its path length. A pose "flapping" cluster bridges into a long run that goes nowhere (net/path 0.013 against ≥ 0.34 for every legitimate chain).

**Step 3 — top.**

- Keep the two longest runs, ordered by time. With two runs (backswing, downswing), **top** is the speed minimum in the gap between them. With one run, top is the highest grip point (minimum image y) within it.
- **bs0** is the first run's start, and **dsEnd** (finish start) is the last run's end.

**Step 4 — top-collapse repair** (ON since 10 Aug).

- With a supplied impact, a real top cannot lie within 120 ms of it. If it does, the two longest runs were downswing and follow-through, so top is re-derived: the grip apex inside [impact − 600 ms, impact − 120 ms], refined to the speed minimum within ±100 ms of it.
- If bs0 is now after the repaired top, an **onset reseed candidate** is the latest run starting before the new top. Failing that it is the end of backswing motion: walk back from top while the speed is below `swLow`.

**Step 5 — impact.** The supplied impact frame (the nearest frame to `job.impactUs`, from the acoustic or marker anchor). Without one, the first post-top frame where the grip returns to within 20 px of address height. Clamped to after top.

**Step 6 — true onset (Stage A).** bs0 is where the grip speed crossed 8 px/frame, which lags the real takeaway because the club rotates about the wrists before the hands move much. It is walked back by three rules:

- **A1.** Walk back while the smoothed speed stays ≥ `swLow` (1.5 px/frame).
- **A2 (φ witness).** Walk back while the smoothed |Δφ| stays above 0.25°/frame. The forearm rotates first. Onset = the earlier of A1 and A2.
- **No-return veto.** On real capture the lerped grip never truly rests: it keeps a 2–4 px/frame floor through fidgets, so A1/A2 run back through every waggle into the deep pre-fidget stillness, 0.5–1.5 s early. The veto finds the **last frame r that the smoothed grip later returns to** (within 7 px, at least 15 frames later, up to bs0). After that instant the hands leave "for good", which is the takeaway. The onset can only move *later*. The boundary is published as `onsetFloor` so the P1 walk-back cannot pass it.
- **A3 clamp.** With a supplied impact, the onset is clamped into [impact − 1.6 s, impact − 0.55 s]. If the walk-back from a collapsed bs0 would pin to the near edge, the reseed candidate from step 4 is used instead. `onsetRule` records which rule fired: 0 walk-back, 1 reseed, 2 near-edge pin, 3 far-edge pin.

**Step 7 — invariant backstop.** If top ≤ onset, top is re-derived after the onset.

**Step 8 — self-check.** The model is **suspect** if:

- the onset came from the A3 near-edge pin (the "manufactured-address signature", Address = impact − 0.549 s to the microsecond); or
- top − onset < 400 ms; or
- no motion run existed at all.

`segmentPhasesChecked` then retries with swSpd × 0.75, up to twice (8 → 6 → 4.5), and the first non-suspect model wins. On the 21-swing B/L set this signature marked 5 of 6 broken runs and 0 of 36 clean ones.

**Step 9 — labels.**

| Frames | Label |
|---|---|
| f < onset | Addr |
| f < top − 2 | Backswing |
| f ≤ top + 2 | Top |
| \|f − impact\| ≤ 12 | Impact |
| f < impact | Downswing |
| f ≤ fin0 | Thru |
| otherwise | Finish |

The same function, with no φ and default config, is reused by the pose runner to bound its expensive second pose pass (`estimateSwingSpanUs`).

### 4.4 Span bounding, the scene median, body geometry

- **Evidence span.** [bs0 − 100 ms, fin0 + 100 ms]. Only these frames get the expensive evidence: it took a 2.3× speed-up with no measurable accuracy change. Frames outside the span get a flat emission row, so the DP *coasts* there: it carries the angle it has at the span edge. This matters for the address hold (§4.6.2, §4.8).
- **Scene median** (`sceneMed`). The per-pixel median of every 8th frame. Anything that moves through the frame (the club) is absent from it, so |frame − sceneMed| highlights the moving club. This is the **dif channel**.
- **Body hull** (for constraint C2). The 8 smoothed joints → convex hull → half-plane form (outward normals). A point is "inside the body" if its maximum signed distance to the hull edges is ≤ 34 px. This replaced a rasterised, dilated mask (2× faster, accuracy identical); the mask survives as the `rasterC2` oracle.
- **Static runs.** Runs of ≥ 25 frames with smoothed grip speed < 0.8 px/frame.
- **rmax** = 0.62 × frame height, the longest search radius.

### 4.5 The evidence engines

#### 4.5.1 E2: the polarity-aware radial ridge sweep (`ridgeSweep`)

For each of the 360 candidate directions θ, cast a **ray** from the grip outwards. Sample it every 2 px from r = 8 px to r = 470 px. At each sample point:

1. **Lateral background.** The median of 4 pixels at ±9 and ±12 px *perpendicular* to the ray. This is the "what's beside the line" level.
2. **On-line value.** The max (bright regime) or min (dark regime) of 5 pixels at 0, ±1, ±2 px across the ray, so a line 1–2 px off the exact ray still counts.
3. **Polarity.** If the background is above 200 (blown mat), the shaft must be *dark*: evidence = bg − min − 12. Otherwise it must be *bright*: evidence = max − bg − 12. The −12 cancels the bias of taking a max/min of 5 noisy pixels over flat background. The result is clipped to [−30, +90].
4. **Accumulation.** cum(r) = Σ evidence along the ray. The ray's **score** is the maximum over r ≥ 98 px (rLo + minLenPx) of cum(r)/√(j+8), where j is the sample index. The √ normalisation rewards long consistent lines without letting length alone win. The r at the maximum is the ray's **terminus** (rEnd). Its **support** is the fraction of samples up to the terminus with evidence > 8: "was this a *continuous* line, or a few bright spots?"

Sampling is nearest-neighbour with integer clamping, deliberately, to match the NumPy exemplar bit for bit. Two sweeps run per frame:

- the **raw** channel on the gray frame (polarity-aware);
- the **dif** channel on |frame − sceneMed| (bright-only, mean of 3 on-line pixels).

#### 4.5.2 Normalisation and the absolute floor (S1)

Each channel's 360 scores are normalised by percentiles (§3.12). The frame's evidence row is the per-θ **max of the two channels**, and support is likewise the max.

Then the honesty fix (S1, ON since 10 Aug): if a channel's *raw* p97 is below **evAbsFloor = 100**, that channel is **drowned**: zero scores and zero support. Without this, a blur-flat frame with no line in it mints a full-strength winner. The floor is inert on normal frames, where raw p97 sits at 210+.

#### 4.5.3 E1: the retro-band match (`frameBandMatch`) — taped clubs only

This uses the known band pattern as a **ruler**.

1. Threshold the gray frame at 235 (saturated) and find connected blobs (area 3–2,500 px) within rmax of the grip. Keep the 20 largest.
2. For each pair of blobs, define a candidate line. Skip near-duplicate angles (within 0.03 rad). Collect the blobs within 4 px of the line. The line must pass within 80 px of the grip.
3. Project the collinear blobs onto the line, measured from the grip, in both directions (the sign is unknown).
4. **Pattern match** (`matchPattern`). For every pair of projected blobs and every pair of known band positions (mm from the butt), hypothesise a scale s = Δpx/Δmm (0.05–0.55 px/mm) and an offset r0 (butt→grip, −50…260 mm). Then:
   - predict where every band should be;
   - greedily pair the bands with blobs (tolerance max(3 px, 0.2·46·s));
   - score by the number of pairs, then RMS;
   - refit s and r0 by least squares over the pairs, requiring monotone order.
5. **Accept** if ≥ 4 bands matched with RMS ≤ 1.5 px (n = 4) or ≤ 3 px (n ≥ 5). The "dark gap" check must also pass: the bare steel between two bands of a group (< 60 mm apart) must dip below 222. That rules out a continuous bright streak masquerading as bands. With exactly 4 matches the dark gap is mandatory.
6. Output: θ, s (px/mm), r0 (mm), n.

A band lock carries a *direction*, a *scale* and an *absolute position along the club*. Because the grip end is known, it has no 180° ambiguity. It is the strongest evidence the tracker has: 0.3° median against its own band truth, and 0.26°/0.49° p50/p90 against the 1,015 band frames of the 61-swing corpus. It only fires club-up with the ring light, ~26% of span frames.

#### 4.5.4 E4: the steel-segment lock (`segmentLock`) — the markerless ruler

The bare shaft is its own ruler. Along the ray from the grip the club shows known landmarks at known millimetres from the butt:

- the **grip end** (dark rubber → bright steel);
- the exposed steel;
- the **ferrule/hosel** (a short dark gap, then chrome).

Two landmarks at known mm give the same (s, r0) a band match gives.

The algorithm, along one ray sampled every 1 px (`rayProfile` reuses E2's per-sample reduction exactly):

1. **Classify samples.** Bright (evidence ≥ 30) or dark (≤ 8).
2. **The steel run.** Bright stretches of ≥ 5 px are anchors. The run is the longest chain of anchors whose gaps are ≤ 80 px *and* whose gap background matches either side within 60 grey levels. A dropout of bare steel leaves the scene background; a clubhead's interior does not. The run must be ≥ 60 px with support ≥ 0.60.
3. **Distal landmark.** Within 25 px past the run, one of:
   - the **head** (the background shifts toward the run's own level by ≥ 50% of the contrast);
   - a **ferrule** (≥ 3 dark samples, then a bright run of ≥ 5 px: the hosel);
   - a **dark end**.
   - Otherwise there is no landmark.
   - A run ending within 25 px of the image edge has no terminus.
   - For unmarked clubs, a ≥ 2-sample dark dip in the last 25 px is taken as a swallowed ferrule.
   - The terminus refers to hosel − 12 mm (resolved ferrule) or hosel + 40 mm (ran to the hosel end).
4. **Proximal landmark.** What precedes the run's start, within 45% of rmax:
   - the hands' bloom (bright, background ≥ half the steel level) → the **hands' edge** at `handsEndMm` (180 mm default, σ 25 mm);
   - a dark matte grip followed by a step up ≥ 30 → the **grip end** at `gripEndMm`.
5. **Fit.**
   - **FULL mode** (both landmarks): s = Δr/Δmm, r0 = mm_G − r_G/s. If the club is banded, the band plateaus join as extra landmarks in a two-pass assign-and-refit.
   - **TERMINUS mode** (no proximal landmark, but a scale prior from the swing's median FULL s): the terminus alone, with s fixed.
6. **Gates.**
   - s ∈ [0.05, 0.55] px/mm;
   - r0 in range (with a −60 mm floor for a hands'-edge onset);
   - projected length s·(clubLen − r0) between 0.40 and 1.20 × the ball-measured length;
   - in FULL mode with a prior, |s − s_prior| ≤ 25%.
7. **Refine.** The probe is repeated over ±1° in 0.5° steps (±5° at address), because a 1° grid is 4.4 px lateral at 250 px, wider than the ±2 px on-line window. Ranking: locked over not, FULL over TERMINUS, then run length × support.

E4 is probed **after** the Viterbi, along the DP's own direction and the band's where E1 locked. It is never probed along raw E2 candidates, "which a crease or the lead arm wins on a third of frames". See §4.8.

#### 4.5.5 The blur wedge (R6 predictor + R8 measurement)

At delivery the shaft is a fan, not a line, and E2 (which hunts a thin ≥ 90 px straight ridge) sees little. The wedge reads the fan instead.

**R6 predictor** (`shaft_kinematics.h`).

- A double pendulum: the club direction ≈ the measured forearm direction φ plus a stereotyped **wrist-cock** angle β̂ that depends on swing progress s (0 at onset, 0.5 at top, 0.9 at impact, 1 at finish).
- β̂ comes from a hand-authored 9-knot table: 8° at address, 92° at top, 100° at s = 0.6, flipping sign through release, −95° at finish. A spread σ_β accompanies it.
- Prediction = φ + chir·β̂(s). Its frame-to-frame rate is ω̂.
- The envelope is prediction ± 3σ_β.
- Crucially this prediction **does not use the DP's own answer**, so the wedge cannot chase a wrong track.

**Trigger.** In-span frames with |ω̂| ≥ 720°/s.

**Proximal sweep.** Inside the envelope only, a second ridge sweep with a short reach (rHi = 0.35·rmax) and short minimum length (40 px). Image velocity grows with radius, so the inner shaft blurs least.

**Two measurements:**

- **Before the top: the plateau centroid** (`measureWedge`). The contiguous run of directions whose response exceeds 0.5 × evAbsFloor and spans ≥ 2°, with the most energy. Its energy-weighted centroid is the "mid-exposure" angle. It is rejected if within 12° of the forearm (the forearm smears its own fan).
- **From the top onward: the edges** (`measureWedgeEdges`, ON since 29 Sept).
  - A ridge detector answers at the fan's *two ends*, not across its body.
  - So find the two highest peaks (≥ 35% of max, ≥ 4° apart) of the [1 2 1]-smoothed response, with parabolic sub-bin refinement.
  - The **leading** peak (further along the rotation) is the shaft at the *end* of the exposure, which is the frame's timestamp.
  - The trailing peak is the shaft at exposure start. It is kept only when the separation matches |ω̂|·t_exp within max(4°, 50%).
  - The centroid had sat near the larger *trailing* peak, which was the +13° impact-shaft-lean bias. Against 123 hand-marked downswing frames: leading edge −1.2° median (|4.6|), the tracker as it was +6.4° (|8.2|), trailing +10.4°.

**Injection into the emission.**

- A Gaussian well: depth 6, σ = the edge's 4.5°, or for a centroid max(half the plateau width, |ω̂|·t_exp/2). Clamped so it never goes below −wBand: a band lock always outranks a wedge.
- **kinCone:** +4 on every state outside the envelope on triggered frames.

The exposure estimate is the recorded one when available, else 99% of the frame period. The width-derived estimate pinned at its 8 ms clamp on every corpus swing, because the plateau width is not ω·t_exp.

The v2 wrist-cock table (`kinModelV2`) is fitted to truth and indexed by *seconds before impact*. It cuts the p10–p90 residual from 74° to 21°, but stays **dark**: its flatter-then-sudden release fires the ω̂ trigger on 30% fewer frames and costs 38% of wedge stamps. The trigger and the centre need separating first.

### 4.6 The emission cost (`frameEmission` + the serial additions)

For each in-span frame, a 360-entry cost row is built in this fixed order. **The order is part of the contract.** Units are "DP cost": wE2 = 10 is the range of the evidence term.

| # | Term | Rule | Weight |
|---|---|---|---|
| 1 | Evidence | em = wE2·(1 − ev). With a band lock, ev at the band bin is raised to 1 first. | 0..10 |
| 2 | **C4 lead-arm veto** | States within 12° of φ+180 (pointing from the grip *into* the lead forearm). | +16 |
| 3 | **C4 trail-arm veto** (1 Oct) | Same for the trail forearm, only up to and including Top. After the top the trail elbow is often hidden and its φ is noise. | +16 |
| 4 | **C4 wide cone** | Not at Addr/Top/Finish. States more than 150° from φ + chir·110° (the forbidden 60° sector behind the swing). | +4 |
| 5 | **C1 reverse ray** | If the *raw* channel's evidence pointing the opposite way (θ+180) exceeds 0.45, the line is a scene line passing through the grip, not a club terminating at the hands. Excused when θ continues the forearm line. | +10 |
| 6 | **C2 body overlap** | Mid-swing only. Sample the ray at r = 45…470 step 14. If > 50% of samples fall inside the inflated body hull, the shaft would be passing through the golfer. | +13 |
| 7 | Hand-axis prior (dark) | States > 35° from the grip hand axis. | +6×conf |
| 8 | **Band well** | The band-lock bin is set to −8, **last**, overriding everything. | −8 |

Serial additions after the parallel per-frame pass:

- **Wedge well and kinCone** (§4.5.5). The band well is re-asserted after them.
- **θ_ball well and the decoy check** (§4.6.2).

#### 4.6.1 Why these constraints

Each one is a physical fact turned into a cost. In the design's own wording:

- **C1** butt-termination: the club starts at the hands.
- **C2** free space: the club is never inside the body.
- **C3** one reversal: the club rotates one way up and the other way down. Enforced in the transition (§4.7), not the emission.
- **C4** arm coupling: the club cannot point back up the forearm, and stays within a reachable cone of it.

The C4 cone was meant to be narrow. Pose φ "spikes to 87°/frame", so it was widened to 150° and switched off at address/finish/top: "Do not tighten the cone" (`club_track_v3_exemplar_explained.md`).

#### 4.6.2 The address witness: θ_ball well and decoy check (1 Oct)

**The address-hold club length (A1).** `medianGripBallLenPx` runs first:

- **Window:** the last quasi-still run around bs0 (not teeing or setup, which read 107–178 px short).
- **Cluster gate:** the component-wise median ball position, keeping samples within 6 px.
- **Golf-prior gate:** the ball must be below the ankle line and between the feet ±10% of the frame width. This catches a detector locked on the driver head.
- Output: the median grip→ball distance = the in-plane club length in px. The accepted cluster centre becomes the address ball.

**Decoy check.** A second ball on the mat, or the ball not being addressed, passes the gates (07-03: grip→ball 115–124° against a marked ~97°, all six swings). So on still in-span address frames the ridge evidence votes:

- A RAY-quality line (ev ≥ 0.45, support ≥ 0.4, not along either forearm) more than 15° from θ_ball, with less than half its evidence within 8° of θ_ball, is a **decoy vote**.
- A line at θ_ball is a **ball vote**.
- ≥ 3 decoy votes that outnumber ball votes 2:1 ⇒ the ball is dropped: no well, no A1 length, `ballSuspect`.
- The ball is **trusted** only if at least one frame confirmed it. On 07-03 (daylight, bare steel, no line anywhere) an unconfirmed well moved P1 from 5° to 24° off the marks.

**The well.** On every address-like frame (before the span, or labelled Addr and still), add 12 × min(1, |θ − θ_ball|/30°). θ_ball = atan2(ball − grip) per frame. Frames before the span have flat evidence rows, so the well alone decides them. That is the point: "the DP used to carry whatever θ it had at spanLo backwards over the whole hold (196° on 15 Sept W02 s1)".

### 4.7 The banded Viterbi (C3)

The DP of §3.4 runs over all frames, with the band from the phase label:

| Phase | w_max (deg/frame) | Direction |
|---|---|---|
| Addr | 3 | either |
| Backswing | 9 | increasing only |
| Top | 5 | either |
| Downswing | 16 | decreasing only |
| Impact | 24 | decreasing only |
| Thru | 16 | decreasing only |
| Finish | 11 | decreasing only |

- The transition cost is kSmooth·Δθ² with kSmooth = 0.03/deg², so a 10° move costs 3.
- "Increasing/decreasing" is in grid index and does not use the chirality. The sign convention therefore assumes the shaft's *image* angle increases through the backswing, which holds for a right-hander filmed face-on as in the corpus. See §10.3.
- Ties break on strict `<`, so the result is deterministic.

The result is a θ per frame, `dp.thetaDeg`, including frames outside the span, where it coasts or is set by the θ_ball well.

### 4.8 The segment-lock passes (E4, after the DP)

Run only when `shaft.seg.enabled` (ON since 10 Sept) and the club geometry is valid.

**Probe direction:**

- the DP's θ;
- or, on address-like frames with a trusted address ball, grip→ball, with ±5° refinement and the in-plane length gated ±15%;
- plus the band θ where E1 locked.

**Frames probed:** in-span frames, plus still frames outside the span (address hold, held finish), probed along the nearest in-span DP direction (`probeStill`). Address-like frames with **no** ball are *not* probed: "the DP's clamp locks a trouser crease".

**Two passes:**

- Pass 1 is prior-free.
- If ≥ 5 FULL locks exist, their median s becomes a scale prior and pass 2 re-probes unlocked frames, allowing TERMINUS locks.

**Outputs:**

- per-frame locks that become the **SEG** tier;
- a segment-length estimate for the length fusion: the median of s·(clubLen − r0) over FULL locks. It uses address locks if there are ≥ 5, else the least-foreshortened quartile, because a mid-swing median "is 40–50% short";
- `segSTyp`/`segR0Med` take rung 2 of the length ladder when no band ever locked.

A band lock *or* a segment lock is "a lock" for the RAY tier's neighbourhood test and for the ψ weights.

### 4.9 ψ-isotonic reconciliation (C4 as a monotone rail)

Define **ψ = θ − φ**, the club relative to the forearm (wrist hinge in the image). Physically:

- through the **backswing**, ψ should increase as the wrists cock;
- through **downswing + impact + thru**, it should decrease as they release.

`reconcilePsi`:

1. For each of the two blocks, take the frames in the block, *excluding a window around the top* ([top − 3, top + 12]: the transition and release lag).
2. Unwrap ψ. Fit a **robust isotonic regression** (§3.5): increasing for the backswing, decreasing for the other block. Weights by tier: band 8, segment 6 (FULL) / 3 (TERMINUS), ray 2, pred 0.3. Huber knee 8°, 3 IRLS iterations.
3. Record |ψ − ψ_iso| as the residual everywhere.
4. **Only on Impact-phase frames without a band lock** is θ actually replaced: θ := ψ_iso + φ, the arm's witness of where the shaft must be. Since 29 Sept, when the blur's leading edge was measured *and* the arm reconstruction differs from the DP by more than 6°, θ := the leading edge instead. On 33 corpus swings ψ+φ read +112° median off the marks on those frames and the leading edge +1.0°.

Why only impact? The A/B (`club_tracking_v3_design.md` §8.1):

| Measure | OFF | impact+thru | impact-only (shipped) |
|---|---|---|---|
| thru p90 θ-error | 3.9 | 5.5 | 3.9 |
| thru coverage (/820) | 678 | 649 | 671 |
| down bad > 15° | 1 | 0 | 0 |

The ψ monotonicity is valid only address → impact. After impact the forearm rolls, "a third rotational DOF a face-on view cannot see".

### 4.10 Tiers, confidence, coverage and validity

Each frame gets one tier, evaluated in this order:

1. **BAND.** E1 locked and the DP's θ is within 6° of it. conf = 0.75 + 0.05(n − 4), max 0.9.
2. **SEG.** E4 locked within 6° of the DP. conf 0.70 (FULL) or 0.62 (TERMINUS).
3. **RAY.** Not an Addr frame before the span. All of:
   - normalised evidence at the DP θ ≥ 0.45;
   - more than 1.15 × the reverse ray;
   - **verifiable**: the grip is moving, or a lock exists within ±5 frames (in the Finish phase a nearby lock is *required*);
   - absolute support ≥ 0.4.
   - conf 0.55.
   - The "static hold is a counterfeit trap" rule is why a still frame needs corroboration.
4. **WEDGE.** Only if still PRED, the wedge triggered, and the DP sits within σ + 4° of the measured edge or centroid. conf 0.45.
5. **RECON.** ψ reconciliation moved θ by more than 6°. conf 0.40. If what it moved to *is* the measured leading edge, the frame is WEDGE instead.
6. **PRED.** Everything else: the DP's coasted or predicted θ. conf 0.30.

- **Coverage** = (BAND + RAY + WEDGE + SEG frames) / frames in [onset, fin0].
- **valid** = coverage ≥ 0.60.

Note that coverage is computed *before* the follow-through demotion (§4.13) and the ball anchor (§4.19). It does not reflect those later edits.

### 4.11 Length and the clubhead

The θ path above is "corpus-validated" and **nothing in this section feeds back into it**. Head and length are a decoupled half.

#### 4.11.1 The length ladder (`projectedClubLenPx`)

The projected grip→head length used for every head that is not directly measured. The first available rung wins:

0. **Fused** (when the fusion is confident, conf ≥ 0.35): min(max(fused, arm floor), 1.1·fused).
1. **Ball-measured** A1 length.
2. **Band-corrected** (or segment-corrected): median s × (clubLen − median r0).
3. **Pose stature surrogate:** shoulder-mid→ankle-mid px / (0.83 × 1.70 m) × (clubLen − 0.13 m). This assumes the golfer is 1.70 m tall.
4. 0.45 × frame height.

All rungs are floored at 1.05 × the still shoulder→grip distance ("a club is always longer than the lead arm") and capped at 1.1 × the measurement (rung 1) or 0.62 × frame height.

#### 4.11.2 Club-length fusion (`club_length_fusion.h`)

Estimators (px), each with σ = max(sigFrac × value, 4 px):

| Estimator | Source | sigFrac |
|---|---|---|
| E-ball | A1 | 0.07 |
| E-band | | 0.30 |
| E-segment | | 0.35 |
| E-head | p95 of post-top Stage-2 measured-head radii, conf ≥ 0.5, ≥ 6 frames, excluded if pinned within 2% of its own search ceiling | 0.30 |
| E-prior | persistent EMA, joins when n ≥ 2 | its own σ |

1. **Sanity-clamp** each estimator to [max(arm floor, 0.9 × pose surrogate), min(0.62 H, 2.2 × pose surrogate)]. E-pose reads ~33% short and is a bound only, never fused.
2. **Leave-one-out outlier rejection** (≥ 3 survivors; relative deviation > 0.5 from the median of the others).
3. **Inverse-variance weighted mean.**
4. **Confidence** = exp(−(spread/0.45)²) × min(1, 0.35 + 0.25·k_instant + 0.10·min(prior n, 4)).

The fusion runs twice: a **pre-pass** before head placement (ball/band/seg/prior) and a **post-pass** with E-head. It also computes a **prior-free** variant, and only that variant (conf ≥ 0.5) updates the persistent per-athlete·club·camera EMA. The prior therefore cannot reinforce itself.

The persistent prior lives in `AppSettings` (`analysis/clubLenPrior`), keyed `athlete|club|cameraKey`. It is used only for a fixed camera, with a frame-size check, and self-heals after a camera move. It is updated only on the live path.

The σ fractions were fitted on **11 swings of one club/camera/athlete**, where E-band and E-head read 25–30% short of E-ball. The note in the code says the real fix (restricting band scale to the address hold) "is escalated, not tuned around".

#### 4.11.3 Stage-2 measured clubhead (`clubhead_track.*`)

ON since 9 July. For each in-span frame, along the *decided* θ:

**H1 terminus walk** (`measureHeadRadius`). Walk the ray from rMin = max(20, 0.06·H) to rHi. A sample is "support" if all hold:

- (thin line OR moving) AND (changed vs sceneMed OR moving) AND NOT a permanent scene line;
- multi-width edge-pair ridges (5/12/24 px for thin shaft, bloomed shaft, head blade);
- swept across a lateral band of ±30 px, because the off-axis pose anchor otherwise misses the club entirely.

Candidate termini are the ends of locally sustained segments; gaps are allowed ("a 75 px specular blowout gap must not terminate the run"). The winner maximises tail quality × a Gaussian length prior.

**Bounds** (`headBounds`). rHi = min(ray edge, 1.15 × L). The acceptance floor:

- 0.5 × L̂ universally;
- ramped up to 0.8 × L̂ across the backswing and back through the downswing;
- 0.8 × L_px + the arm floor in still and impact frames.

**Projection prior** (ON). The prior centre on every frame = in-plane length × (current shoulder→grip reach / address reach), a per-frame foreshortening proxy.

**Flip check.** The opposite ray is measured too. If it out-supports the forward ray decisively, the frame cannot be blessed "meas". It is never *corrected*.

**H2 temporal model** (`runHeadTemporal`). A constant-velocity 1-D Kalman on [r, ṙ]:

- segmented at stage-1 θ jumps > 20°/frame;
- 3σ innovation gate;
- coast budgets of 12 frames (slow) or 4 (fast, |ṙ| > 800 px/s);
- RTS smoothing per segment;
- **meas** tier only after a confirmed run of 4.

Tiers: **meas** (measured), **pred** (smoothed but unconfirmed), **off** (the head is expected outside the frame: the ray edge is less than 0.8 × L̂).

A backswing confidence cap of 0.45 reflects "systematic short-lock on motion-blur streaks in [bs0, top]".

### 4.12 Placement: building the samples

For each frame with a grip:

| Case | headPx | flags |
|---|---|---|
| **BAND** | butt-anchored: butt = grip − s·r0·u, head = butt + s·clubLen·u | Measured |
| **SEG** (only if `placeHead`, **dark**: 73 px vs hand truth against 34–48 px) | rF + s·(clubLen − terminus mm) | Measured |
| **Stage-2 meas** | grip + r·u | Measured |
| **Stage-2 off** | the ray/frame-edge point | stage-1 flag + HeadProjected + HeadOffFrame |
| **Stage-2 pred** | grip + r_smoothed·u | stage-1 flag + HeadProjected |
| Else | grip + ladder length·u | stage-1 flag + HeadProjected |

The "stage-1 flag" is Measured for RAY/SEG, Wedge for WEDGE (deliberately not Measured), and Coasted otherwise.

- **Wedge observations** (`wedgeObs`). For accepted wedge frames with edges: Lead at t, Trail at t − t_exp, Mid at t − t_exp/2, each with σ 4.5°. Used by the synth fit (§7.1.6).
- **θ̇.** A central difference over neighbouring *samples*, with no smoothing.

### 4.13 Follow-through plausibility (`demoteImplausibleFollowThrough`)

After impact the tracker can capture the lead arm, or coast with the club at rest (15 Sept pitch shots: head conf 0.84 on the forearm).

1. The swing's peak rate = max(p90 of consecutive-sample rates over the 150 ms before impact, max |θ̇| in that window).
2. Cap = max(1.0 × peak, 1,200°/s): "past impact the club only slows".
3. Every measured post-impact sample whose rate against the last good sample, *or* against its immediate predecessor, exceeds the cap is demoted: Measured/Wedge/ImuBridged are removed, Coasted|HeadProjected|Implausible added, conf ≤ 0.30.
4. The forearm-alignment test exists but is OFF. In a face-on projection the folded club and the forearm legitimately line up at the finish (21–26° apart), and the test demoted 25 good samples.

### 4.14 The snap (Layer A: line re-registration)

The DP picks a *direction from the pose grip*, and the pose grip is ~39 px off the shaft axis. The drawn line is therefore parallel to the shaft but offset, or slightly rotated about the wrong point. The snap finds the line that actually lies *on* the club.

- **Search:** perpendicular offset d ∈ ±45 px × angle change ±10°, coarse (2 px × 1°) then fine (1 px × 0.5° within ±6 px × ±2° of the best coarse cell).
- **Objective:** the mean corridor evidence along the full drawn length (`ridgeLineIntegral`), where the corridor is the mean of ±2 px across the line against a background at ±9/±12 px, with the same polarity rule.
- **Choice:** the cell nearest the centroid of the near-maximal plateau. A thick ridge scores flat across several offsets, and the centre is the on-ridge position.
- **Accept** if the support under the new line ≥ 0.25 *and* it does not point into the lead forearm. The grip moves to the foot of the perpendicular from the pose grip onto the new line; a projected head follows.

**Never snapped:**

- **BAND** frames. The snap could only move a 0.3° measurement: on the corpus it took θ-vs-band from 0.26/0.49° to 0.61/3.18°.
- **Address frames and the first 80 ms of the backswing.** It re-registers onto the *leg*: P1 4.3° → 18.7° on 0909 s0004.
- **Impact/Thru.** "The shaft is a fan there": 9.6° → 12.4° worse.

On the unmarked 6-iron the snap took direction p50 from 5.5° to 1.8°.

### 4.15 P7 from geometry (`impact_geom.h`)

The acoustic or marker impact anchor has two measured failures:

- it runs **13–22 ms early** (trigger bias);
- the nearest-frame mapping can land **234–362 ms late** across a coverage gap.

Geometry: with the trusted address ball fixed, find the first hysteresis-confirmed (±8°) crossing of θ(t) through θ_ball(t) = atan2(ball − grip(t)) within ±600 ms of the anchor. The crossing is interpolated sub-frame.

Decision: if the crossing differs from the anchor's frame by more than 100 ms *and lies earlier*, override. Both documented failures leave the truth at or before the emission; a *later* crossing is the DP coasting through the blur. Sub-frame retime of a corroborated anchor is **dark**: "it de-biases but scatters (−20..+19 ms)".

Only the P6/P7/P8 windows and the Impact *event* follow the decision. The wedge, tiers and earlier stages already ran on the segmented impact.

### 4.16 P-positions (Layer B)

The coaching P-system:

| P | Definition | Source |
|---|---|---|
| P1 | Address | end of the address hold |
| P2 | Shaft parallel (backswing) | θ crossing in (P1, P4) |
| P3 | Lead arm parallel (backswing) | φ crossing in (P2, P4) |
| P4 | Top | phase-model top |
| P5 | Lead arm parallel (downswing) | φ crossing in (P4, P6) |
| P6 | Shaft parallel (downswing, "delivery") | the **last** θ crossing in (P4, P7) |
| P7 | Impact | impact frame, possibly geometry-corrected |
| P8 | Shaft parallel (follow-through) | θ crossing after P7 |
| P10 | Finish | fin0, emitted only if last in time |

P9 is not modelled.

**"Parallel" in the image plane.** Fold both horizontal directions together with elevation(a) = atan2(sin a, |cos a|): 0 at horizontal, ±90° at vertical. A parallel event is a zero crossing of the elevation.

**Hysteresis.** A crossing counts only after the signal has gone beyond ±8° on *both* sides. The reported instant is the steepest zero-straddling pair between the arming frames, interpolated sub-frame. P6 uses the *last* crossing: with a top near horizontal (θ ≈ 20°) the shaft dips through 0° just after P4, and the true delivery parallel is later.

**P1 = the address-hold end** (`addressHoldEndFrame`). Walking back from bs0, the last frame whose trailing 10 frames have:

- net smoothed grip drift < 2 px (creep is directional, jitter nets out);
- no per-frame step ≥ 2.5 px (an oscillating waggle nets ~0 but is not still).

Preferences:

- frames where the ball is seen (BallSeen);
- if the ball track carries club-corridor activity on ≥ 50% of the hold, frames where the club is also quiet near the ball (catches a club bob about frozen hands);
- never below `onsetFloor`.

**Sampling.** Each position samples the emitted track at its time: grip and θ interpolated, conf and length from the nearer sample. **timing** = Measured only if both straddling samples are vision measurements, else Proxy.

**B2 milestone fit** (`shaft_position_fit.h`). For positions whose nearest sample is *not* already measured with conf ≥ 0.5:

1. **Shift-and-stack.** Register ±4 frames by de-rotating each by ω·Δt about the grip path, where ω is the smoothed rate. Average them. Noise falls by √N. Because only the *rate* is used, a track θ that is 180° flipped does not corrupt the stack.
2. **Joint fit** of (θ, L, grip ±6 px) on the stacked image with the same corridor ridge integral.
   - P1–P4: a narrow sector, ±7.5° about the track θ.
   - P5–P8: a wide 170° sector centred on grip→ball, else the arm, to escape flips.
   - Ball bonus at P1/P7: ×(1 + 0.6·exp(−d²/2·22²)) for heads near the ball.
3. **Accept** if support ≥ 0.35 and not into the forearm. σθ and σL come from the half-width of the ≥ 90%-of-max plateau. The drawn length is replaced by the fused club length.

### 4.17 Layer C: the synthetic track

Described in full in §7.1.

### 4.18 Self-checks and refusal

After everything, `decideTrack` compares the track with two independent witnesses:

- **P1 vs the ball.** If the trusted ball's direction differs from θ(P1) by more than 25°:
  - if P1 rests on measurement (median θ of measured samples within ±6 frames agrees with P1 to 10°), the *ball* is suspect (`ballSuspect`) and the track stands;
  - otherwise the track is **refused (reason 1)**.
- **Phase model.** Still suspect after the retries, *and* P2 or P3 missing ⇒ **refused (2)**.
- **Length.** A1 length / grip→ball distance at P1 off by more than 30% ⇒ **refused (3)**. The hold window drifted into the backswing: 407 px against a 280 px club.
- **Hands unusable** ⇒ **refused (4)** (§4.1).

A refused track has `valid=false`, keeps its samples for the lab, is persisted with `refused`, draws nothing, and every club metric shows "–" (Mark, 30 Sept). On the robustness set: 21 ball-visible swings went from 19/17 (B/L) to 21/21 sane, 15 Sept W02 from 3/13 to 13/13, and the 68 metrics of the pinned corpus were identical.

### 4.19 After `decideTrack`: the post-hoc ball anchor and the impact-anchor stage

**`applyBallAnchor`** (v3.4, "can only improve the track, never degrade it"):

- **tk0**, the first frame where θ departs θ_ball by more than max(25°, the departure at bs0), is *computed and logged only*.
- **BallSeen** is set on frames before tk0 that have a ball.
- **Address paint** (only if the ball is trusted):
  - a measured sample within 15° of θ_ball gets BallAnchored;
  - a disagreeing measured sample is left alone;
  - an *unmeasured* sample is rewritten: θ = θ_ball, head = ball, length = |B − G|, conf ≥ 0.5, HeadProjected cleared.
- **Impact:** the last frame before ball launch, if not measured, gets θ from grip → launch centre and head = ball.
- Then `ShaftTracker` re-samples P1 if its sample was rewritten.

**`ImpactAnchorStage`** (a pipeline stage after ShaftStage, `impact_anchor.h`) finds the address ball independently, *by departure*:

- diff = median of backswing frames − median of frames 150–410 ms after impact. The ball is present before and gone after.
- Search near the P1 clubhead and below the toe line, scoring a disc response at radii 5–9 px.
- Sets `shaft.ballAnchored` + `addressBallPx` and re-synthesises Layer C.

It does **not** pin the ball into the track. Shaft lean and low point read the ball directly. The hands→ball line reads "−0.8° (sd 2.1°)" against 32 marked contacts. The ball finder places "29/32 within 10 px".

---

## 5. The down-the-line tracker

Files:

- `dtl_shaft_tracker.*` — the SwingWindow layer;
- `dtl_shaft_decide.*` — the band solve;
- `dtl_shaft_post.*` — snap, length, tier ladder and HELD;
- `dtl_shaft_bands.h` — the band and edge rule;
- `dtl_shaft_config.h` — `shaft.dtl.*` keys;
- `dtl_shaft_types.h` / `dtl_shaft_track.h` — working and product types;
- `dtl_face_on_witness.*` — the witness builder;
- `dtl_shaft_json.*` — persistence.

Design: `docs/design/dtl_shaft_tracker_design.md`, `dtl_continuous_track_design_update.md`.

### 5.1 Why the face-on tracker cannot simply be pointed at the DTL camera

It was tried. The unmodified face-on tracker on DTL reported "coverage 0.769, valid", and was "152–155° from truth on 25 of 25 frames … 100% confidently wrong". Down the line:

- **The club points at the lens** three times a swing: P2, around the top, and P6. The projected length goes to zero and the image angle *flips through a pole*. No smoothness prior can carry a track through a pole.
- **The arm is collinear with the shaft** at address and impact, so face-on's C1 reverse-ray test refuses correct frames.
- **The trail leg** ("the trouser line") is a longer, higher-contrast ray out of the hands than the shaft at address.
- **No ring light**, and the background is the lit simulator screen. A taped shaft alternates black and white bands over mid-grey, and a signed bright-ridge detector *cancels* along it while a wide bright limb wins. This is the **polarity trap**.
- There is **no phase-signed rotation law** in this view: the sign of dθ_D/dt is not fixed.

So the DTL tracker reuses face-on's *engines* (ridge sweep, band match, snap, Viterbi) and none of its *decisions* (no phase model, no C2, no ψ, no bridging).

### 5.2 The face-on witness (`buildFaceOnWitness`)

What face-on hands over, read-only, interpolated to DTL times:

- θ_F, unwrapped, and θ̇.
- **ρ_F**: the face-on projected length as a fraction of the full length. It is NaN unless the face-on sample was Measured. The denominator is the p90 of measured visible lengths at near-horizontal θ (|cos θ| ≥ 0.94), because near-vertical frames read 10–17% long from perspective.
- The face-on grip's image row, the tier, the phase label, the P-ladder, impact and chirality.

`At()` returns not-ok outside the range or across a face-on gap of more than 3 frame intervals. "ok" does **not** promise a finite ρ_F.

### 5.3 The visibility schedule

If the face-on camera sees the shaft at angle θ_F with fraction ρ_F, the shaft's component along the face-on image horizontal is u_x = ρ_F cos θ_F. The DTL camera looks roughly *along* that horizontal (the target line), so the fraction of the shaft the DTL camera can see is

```
ρ̂_D = √(1 − u_x²)
```

- Where ρ_F is unknown but θ_F is known (face-on coasts at address and reconstructs at impact), ρ_F := 1 is used. This **bound** is conservative: it *minimises* ρ̂_D, so a near-horizontal face-on shaft still reads end-on.
- A frame is **sighted** if ρ̂_D ≥ `rhoSolveMin` = **0.50**. The design said 0.35, but at the top ρ̂_D only fell to 0.40–0.59 on some swings, and the solve ran a band "straight through the frames where the club is pointing at the lens".
- Maximal sighted runs of ≥ 6 frames are **bands**. Shorter runs are flicker, unless the **edge rule** admits them: ≥ 3 frames within 2 non-end-on frames of a full band, or a median ρ̂_D ≥ 0.70.
- **Each band is solved independently. Nothing connects bands and nothing is emitted in end-on gaps**, because θ_D is undefined there.

### 5.4 Anchors and the cross-view quarantine

- **Anchors:** the DTL grip (mean of the hand centroids), both elbows and wrists, and the 8 body joints. Keypoints with conf ≤ 0.30 become NaN.
- **Row fit.** Both cameras see vertical, so the DTL grip row ≈ a × face-on grip row + b. Fit a, b by least squares over P1 → impact.
- **Quarantine** a frame if any of:
  - there is no grip;
  - either wrist is unconfident;
  - |row residual| > 80 px;
  - |row residual| > 40 px more than 80 ms after impact.
- This catches "the post-impact invented hands" without trusting hand confidence. Quarantined frames are tiered OCCLUDED_WRIST or OCCLUDED_ROW, not solved.

### 5.5 The DTL ball and the club length L̂_D

**Bright cue.**

- Threshold the address-hold median image at min(230, p99.5).
- Keep compact round blobs (circularity ≥ 0.6, aspect ≤ 2, r 4–16 px) inside the DTL ball prior: below the ankle line and beyond the grip on the hips→grip side.
- **Permanence with launch:** the blob's p25 brightness over the hold must be ≥ 0.85 × its median ("a quarter of the hold may have the head over it"), and after impact it must drop below 50%.
- Exactly one survivor is required. Two or more is "ambiguous, no ball".

**Shadow cue** (for a blown-white mat, where a white ball has no edge).

- From a "club-away" window median (P2 + 40% → P5, with named fallbacks when a rung is missing): find dark compact blobs on blown mat (local median ≥ 200, ≥ 60 below it) inside the prior.
- Require the same pixels to **brighten by ≥ 40 after launch**. A scuff or tee hole stays dark.
- The clearest must beat the next by 1.5×.
- The ball centre is placed one radius above the crescent, with the radius from the scene scale.

**L̂_D (the full DTL club length).**

- If the ball was found: the median grip→ball distance over the hold / the address ρ̂_D.
- Else: face-on's full length × the row-fit scale a.

### 5.6 The DTL emission (per sighted frame)

**Three evidence channels.** Each is a ridge sweep with the absolute floor (§4.5.2). The per-θ winner sets EV and the terminus REND.

1. **Motion:** |frame − phase-aware clean plate|. The plate is the address-hold median above the grip row and the club-away median below it, invalid after impact + 100 ms when the screen animates ball flight.
2. **Contrast:** |frame − 31×31 box-blur|. It is **polarity-free** and defeats the band cancellation: 37–48 grey levels along the true shaft against 1–8 on a control line.
3. **Raw:** signed, as face-on.

**Costs:**

| Term | Rule | Weight |
|---|---|---|
| Evidence | wE2·(1 − EV) | 0..10 |
| **D1 reverse ray** | As C1. Waived where the reverse lies within 25° of grip→(either elbow or shoulder) more than 40 px away, and on ball-gated frames, because the reverse of a correct direction runs up the golfer's own arm. | +10 |
| **D2 limb veto** | Both elbows, hips, knees and ankles (not shoulders, not head), all phases. A direction within 12° of grip→joint whose ray passes within 25 px of the joint (joints < 60 px from the grip are skipped). Charged once per θ. This is the trouser-line fix. | +16 |
| **D3 over-length** | One-sided: a run longer than 1.25·ρ̂_D·L̂_D. | +8 |
| **D4 half-plane** | Where face-on measured with \|sin θ_F\| > 0.25: penalise states whose sin has the opposite sign. Up face-on is up down the line. | +6 |
| **D5 corridor** | Where face-on measured and ρ_F ≤ 0.93: two candidate centres atan2(ρ_F sin θ_F, ∓√(1 − ρ_F²)), because the depth sign is unknown. Quadratic cost outside ±25°, ceiling 5 (below wE2/2 so clean evidence can escape). The 25° is a **placeholder**. | ≤ 5 |
| **D6 ball well** | Gaussian at grip→ball at address and ±2 frames of impact. | depth 4, σ 8° |
| **D6b ball gate** | Where the club is *known* to be at the ball: the address hold, released when face-on θ moves more than 10° from P1 or after 300 ms, and ±20 ms of impact. States more than 20° from grip→ball pay this. With no ball these frames are left **unsolved**. | +30 |
| **Band well** | −8 last. | −8 |

### 5.7 The solve

One banded Viterbi per band:

- no sign restriction;
- w_max = ⌈12°/max(ρ̂_D, 0.5)⌉ per frame, because θ_D legitimately moves fast as the projection shortens;
- kSmooth as face-on.

### 5.8 Post-solve: snap, length, tiers, HELD (`dtlPostSolve`)

**Snap** on the contrast image, not the raw frame (the polarity trap again). Conditions:

- not on band-locked frames;
- only where ρ̂_D ≥ 0.6;
- objective averaged over max(rEnd, ρ̂_D·L̂_D). Using rEnd alone scored the near half of the club and landed 4° off at address.

Accept only if it beats the original line's support, does not land under a limb veto, and stays inside the ball gate.

**Run length.** Measured off the snapped line, or the best of 7 lateral origin offsets (±30 px), because the pose grip is off-axis. The longer of that and rEnd is taken, unless rEnd is the sweep's own **floor (98 px "to the digit")**, which "is not a length at all".

**Tiers:**

| Tier | Rule |
|---|---|
| **BAND** | Band lock within 6°. conf 0.75–0.90. |
| **RAY** | All of: (ev ≥ 0.45 or accepted snap support ≥ 0.36); support ≥ 0.40; beats the reverse ray by 1.15 or waived; no limb veto; run ≥ 0.35·ρ̂_D·L̂_D; run not the sweep floor; not a corridor escape after P8. conf 0.55·ev. |
| **UNSEEN** | Solved but unearned. Publishes *nothing*: θ NaN. |
| **END_ON / OCCLUDED_WRIST / OCCLUDED_ROW / OCCLUDED / UNSEEN** | Unsolved frames, with a `reason` string. END_ON is checked before quarantine since 1 Oct: 1,078 of 2,063 OCCLUDED frames were end-on. |
| **HELD** (ON by Mark's decision, gate not met) | A hole of ≤ 6 frames inside one band with a measured frame on both sides keeps the band's own Viterbi θ. Drawn dimmer, never fed to fusion or any metric. |

There is **no PRED tier**: "a frame is never measured on face-on's word". SEG is defined but never produced (deferred).

**Drawn length.** With `lenSchedule` it is ρ̂_D·L̂_D, the visibility law. Per-frame switching between length sources caused 713 head jumps over 40 px.

**Self-counts.** `publishedInEndOn` "should be 0 by construction … counted anyway, because a construction nobody measures is a belief".

### 5.9 Validation (held-out six swings, against band-template truth)

- 425 of 620 truth frames paired, **p50 0.25°, p90 0.50°, zero frames over 15°**.
- Coverage: address and impact 1.00 on all six; mid-backswing 0.71–0.88; downswing 0.72–0.93.
- After the continuity rules: drawn coverage 0.69 → 0.80 mid-backswing and 0.66 → 0.75 delivery. The ≥ 0.85 target was **not** met.

The truth is **address-region only**. "Three of the four bands … have no automatic truth", and **no DTL hand marks exist**. Of the published frames, "1,142 are RAY and 8 BAND".

---

## 6. Two views into three dimensions: fusion and the swing planes

### 6.1 The fusion idea (`shaft_fusion.h`)

**Coordinate frame.** X = face-on image right (the target for a right-hander), Z = up, Y = the face-on viewing direction.

- The face-on camera is orthographic along +Y.
- The DTL camera looks along +X, yawed and pitched (and since 2 Oct optionally rolled) by config angles. All are **assumed zero** unless a calibration is supplied.

**Per DTL frame with a measured DTL angle:**

1. Get θ_F at that instant:
   - interpolated between two *measured* face-on samples (Measured flag, not Coasted/Synthesized/Implausible/Predicted) within 12 ms → **Measured**;
   - else from the face-on synth track → **Bridged**.
2. Each camera's view plane normal: n = d × imageDir(θ).
3. u = (n_F × n_D)/|…|. The sign is decided by the camera that sees more of the shaft; if the other camera disagrees, the frame is flagged `SignDisagree`.
4. cond = |n_F × n_D|. Below 0.26 (the planes within 15° of each other) the frame is flagged `IllConditioned`.
5. Predicted projected fractions ρ_F and ρ_D are recorded. This makes the lengths a **check**, not an input.

### 6.2 Plane fits

- **Backswing window:** [Address, Top). **Downswing window:** [Top, Impact + 20 ms].
- Only Measured, unflagged frames enter, and n ≥ 8 is required.
- The plane through the origin is the smallest eigenvector of Σuuᵀ (§3.10).
- inclination = acos|n_z|, plus the out-of-plane RMS and p90.
- A plane is **offered** downstream only if its RMS ≤ 5°.
- Backswing RMS > 12° is flagged `backIncoherent`: "the backswing is not a plane (the takeaway and the lift are two)".

### 6.3 The address plane

The DTL view plane *is* the address plane when the camera looks down the line. So the address inclination is the median inclination of the DTL view-plane normals over the published DTL frames up to Address (≥ 5). It owes face-on nothing. `deliveryVsAddressDeg` = down − address; + = steeper.

### 6.4 η(t), the out-of-plane curve (ON by Mark's decision; its gate failed)

One smooth curve through the measured frames' signed out-of-plane angles:

- knots every 30 ms;
- second-difference penalty λ = 4;
- a zero prior on knots with no frame within 150 ms, so gaps relax to in-plane instead of extrapolating a slope;
- clamped to ±25°;
- solved by Cholesky;
- evaluated as Catmull-Rom.

Read only by synth3d.

### 6.5 Mirrored-band re-read (ON) and DTL anchoring (OFF)

**Reflect bands (D).** When the backswing fit is incoherent, a DTL band read mirror-imaged about the vertical (θ → π − θ, the corridor's two-centre ambiguity) is re-fused. Bands are kept greedily while the RMS falls by more than 0.5° without raising sign disagreements, and only if the refit faces the same way as the downswing plane. Result: 07-04 s2/s3 went from 20.9°/18.0° to 12.0°/4.4° RMS.

**DTL anchor (C)** is closed as degenerate: at impact cond is 0.06–0.08, because the DTL camera looks along the plane.

### 6.6 The face-on-only conic plane (`shaft_plane.h`, "transition plane")

A single-camera estimate (§3.9).

- The shaft *vector* (head − grip, never the absolute head path) sweeps a circle on the swing plane and images as an ellipse.
- Fit the conic with isotropic normalisation (one pooled scale; per-axis scaling "reported inclinations wrong by up to 40°") via SVD.
- ι = arccos(minor/major). The transition delta = ι_back − ι_down.

Rejects:

- n < 12;
- not an ellipse;
- degenerate axes;
- centre on the conic;
- **axis ratio < 0.26 (a needle, ι > 75°)**. Three of 33 reference fits were needles giving deltas of ±47–58°.

Two channels:

- **measured** (samples with headConf > 0, not synth), with a split-half repeatability;
- **synth** (the Layer C series), whose only honest quality is its anchor count and confidence. A split-half on interpolated samples "measures interpolation smoothness, not repeatability" and is structurally impossible to compute.

**Status: EXPERIMENTAL.** Absolute ι is uncalibrated (the brief bounds a 64° body-depth bias). It was removed from `over_the_top` on 23 Sept: on a golfer who comes over the top on every swing it "read −20° to +9° and changed sign".

---

## 7. The synthetic tracks

There are two, and they are different things.

### 7.1 Face-on Layer C: the synthetic shaft between the P-anchors

**Why it exists.**

- Replay at ¼× speed and the shaft "fan" need a continuous, dense shaft between frames.
- Through the impact zone the per-frame track is mostly not a measurement (bare steel is invisible there), yet the speed and path metrics need the club's path there.

The synth is a smooth curve **anchored at the located P-positions** and, since 29 Sept, **fitted to every measurement between them**.

#### 7.1.1 Grid and anchors

- Anchors = `positions` (P1…P8, P10, whichever were located), strictly ascending in time.
- Ticks on a fixed **240 Hz** grid from the first to the last anchor, *strictly between* anchors. Anchors themselves are not emitted as synth samples.
- Every tick is flagged `ShaftSynthesized` and carried in `ShaftTrack2D.synth`, never in `samples`.

#### 7.1.2 Anchor rates

- Per anchor, θ̇ = the reconciled per-frame θ's central difference, smoothed median-5 + Gaussian-2, sampled at the anchor time.
- The grip velocity is likewise the central difference of the pose grip path.

**Impact as a boundary** (ON since 6 Sept). The club loses 20–30% of its speed in two frames at contact, and a smoother spanning ±27 ms averages across it. So:

- **P7 in-rate:** a one-sided linear fit of θ over the 24 ms before P7 (widened until ≥ 3 frames; non-PRED frames preferred), floored at the P6→P7 mean rate.
- **P7 out-rate:** fitted over the 24 ms after.
- **Bracket-start anchor's out-rate:** fitted forward and capped at the mean, so the rate is monotone across the bracket.

Effect on the six 08-18 launch-monitor pairs: speed peak time moved from −17…−19 ms to −1…−9 ms, and the pre-impact SD fell from 3.3 to 1.8 mph.

#### 7.1.3 Interpolation

Per bracket [a, b], with τ ∈ (0, 1):

- **θ.** Cubic Hermite through (θ_a, out-rate_a) and (θ_b, in-rate_b), with Fritsch–Carlson limiting. θ_b is first **unwrapped toward the expected rotation** (mean rate × Δt), so a 200° bracket goes the track's way round, not the short way.
- **θ̇.** The analytic derivative of that Hermite (`curveRate`, ON). This is what clubhead speed reads.
- **Grip.** Taken from the **hand track** (linear between the nearest finite pose frames within 40 ms), *never* interpolated between anchors. A Hermite grip had swung "59 px median / 384 px maximum" off the hands. The Hermite is the fallback only where the pose has no hands.
- **Length.** Linear between the anchors' drawn lengths.
- **conf.** min(anchor confs) × (1 − 0.4·4τ(1−τ)): 1.0 at the anchors and 0.6 at the midpoint.
- **headPx** = grip + L·(cos θ, sin θ).

#### 7.1.4 Rules that stop the synth inventing a club

The anchors are split into runs, and each run is bridged separately:

- **Rule 1** (`measuredEndAfterImpact`). A bracket starting at or after P7 is bridged only when its end anchor rests on a measurement (timing ≠ Proxy). A pitch shot stops short of P10; the tracker coasts; a P10 anchor gets an angle the club never reached; and the Hermite "sweeps 165° in 80 ms".
- **Rule 3** (`maxFollowThroughRateDps` = 1,500°/s). A bracket starting at or after P8 whose anchors imply more than 1,500°/s is the tracker on the lead arm and is not bridged.

#### 7.1.5 Rule 2: the envelope clamp

After the fit below, every tick is checked against the **measured** samples within ±25 ms (brought onto the same sheet). If it is more than 10° outside their [min, max], it is clamped to the boundary, its head re-derived and its θ̇ replaced by a finite difference.

#### 7.1.6 The evidence fit (`fitSynthToEvidence`, ON since 29 Sept)

The Hermite alone is set by the anchors. A measured frame between two anchors only influenced it through the smoothed anchor rates. Now θ at the synth ticks is the solution of a penalised least-squares problem, run per *stretch* of consecutive brackets:

```
minimise  Σ_evidence ((θ(t_e) − y_e) / σ_e)²   +   Σ_nodes (θ̈_j / σ_a)² · Δt_j
```

- **Unknowns:** θ at every tick. **Fixed nodes:** the P-anchors (hard: they are the positions' definitions).
- θ(t) is linear between nodes in the data term. θ̈ is the second divided difference on the non-uniform node spacing.
- **Evidence:**
  - every Measured / IMU-bridged / Wedge sample, at σ = 3°. Wedge samples are replaced by their timed edges when edges exist;
  - each blurred frame's Trail, Mid and Lead edge observations at their own σ (4.5°);
  - optionally the hands→ball line at P7 (dark: `ballAnchorSigmaDeg` = 0).
- **σ_a = 5,000°/s²**, the "plausibility" scale, tuned against 283 hand-marked frames the fit never sees:

  | σ_a | P1–P4 | P7–P8 \|median\| | peak \|θ̈\| |
  |---|---|---|---|
  | Hermite only | 2.7° | 9.5° | 6k |
  | **5,000 (chosen)** | 2.0° | 6.5° | 18k |
  | 80,000 | 2.1° | 7.0° | 71k (chases noise) |

- **No plausibility term at a P7 node:** contact is a break.
- Solved by Cholesky on the normal equations. Ticks get θ̇ from the fitted nodes' central difference, and heads re-derived. A stretch with no evidence keeps the Hermite exactly.

#### 7.1.7 Rebuild on reuse

`resynthesizeLayerC` rebuilds the whole tier from a persisted track's samples and positions: anchor timing is re-derived from the straddling samples. It runs when a document's track is reused under the version gate, and after the ImpactAnchor stage.

#### 7.1.8 Who reads it

- The ⚠ in `shaft_synthesis.h`: **"METRICS DO READ IT."** clubheadSpeed, handSpeed and lagAngle prefer synth; attackAngle prefers it; lowPointAhead is *defined* on it; clubAngularSpeed uses it; the face-on plane has a synth channel; fusion uses it as the bridge.
- Scoring, the estimands, the plane fits in fusion, and the wrist channel filter it out by flag.
- See §10.4 for what this means.

### 7.2 The 3-D synthetic DTL line (`dtl_shaft_synth3d.h`, stage DtlSynth3D)

The DTL image angle passes through a pole at P2, P4 and P6, so "there is no smooth function of time to fit in that image". The shaft's 3-D direction *is* smooth. So:

1. Take the face-on Layer C angle θ_F at the DTL instant (within a 12 ms bracket).
2. That confines the shaft to the face-on view plane.
3. Intersect it with the swing's **phase plane**:
   - address plane up to Address;
   - backswing plane to Top;
   - downswing plane to the end of the downswing window;
   - then the downswing plane held up to 250 ms more, flagged `DownExtrapolated`.
   - With η(t), the direction is rotated η off the plane (`deprojectEta`).
4. Refuse where the two planes are within asin(0.15) of each other.
5. Project through the **assumed** DTL camera. Length = ρ_D × L̂_D. The grip anchor is the DTL tracker's own grip, or the 3-D skeleton's hands projected into DTL where the anchor was quarantined.

Status: **ON as a PREVIEW** (Mark, 1 Oct). With the camera uncalibrated, the line "is off by exactly the unknown yaw (4–9°)". It is never read by fusion, the kinematic sequence or any metric, and it is drawn dashed and dimmer ("Synthetic club" motion preset).

---

## 8. The metrics the shaft feeds

"Instant" readings snap to P-positions or ladder events. The impact instant is *not* the same everywhere; see §10.5.

| Metric (key) | View | Inputs (which samples) | What is computed | Read at | Units / σ | Status and notes |
|---|---|---|---|---|---|---|
| `impactShaftLean` | face-on | **All** `samples`, no flag filter | (θ − 90°) unwrapped, sign-flipped for left-handers; + = hands ahead (sign "PROVISIONAL"). If the address ball is found, the curve is shifted so its value at impact equals the hands→ball line's lean; the sheet is chosen so impact ∈ (−180, 180]. | Impact (`job.impactUs`), Address→Impact Δ | °, σ 9.5 | Leading-edge fix: +12.5° → 0.0° median on 32 marked P7s; ball-anchored sd 10.2 → 5.3°. σ not reduced for ball-anchored. |
| `shaftLie` | DTL | `dtlMeasured` only | Unsigned angle of the shaft line to the image horizontal (0–90°). Readings = nearest measured frame within 12.5 ms. | Address, Impact; Δ = impact − address, + = steeper | ° | Absolute value depends on DTL camera height and offset; only Δ is comparable. "–" when no measured DTL frame is near (usually: no DTL ball). |
| `shaftAngleVsHorizontal` | face-on | Measured heads: !HeadProjected, headConf ≥ 0.30 | atan2(head.y − grip.y, \|head.x − grip.x\|); + = past parallel | Top | ° | Foreshortened heads at the top fall below the 0.5·L̂ floor and become pred, leaving gaps at Top. |
| `attackAngle` | face-on | Synth arc preferred (≥ 5 ticks in ±20 ms of Impact, continuity check); else measured heads | Centred difference ±2 samples, atan2(−dy, \|dx\|); + = up | Impact | ° | Synth vs GC Quad: +0.02° bias, 3.26° spread (one session, six swings); measured heads were a median 36° off. **No characteristic reads it.** |
| `lowPointAhead` | face-on | **synth only** (no fallback), ball, ball-diameter ruler | Lowest (max-y) synth head within ±60 ms of Impact, parabola-refined; offset from ball × mm/px, + = target side | Impact | in, **σ 2.0 (Estimated / Bridged)** | 36 in × tan(3.26°). The vertex is "pinned near the P7 anchor". Read as a session tendency. Measured heads gave a value on 9 of 108 swings. |
| `clubheadSpeed` | face-on | synth (≥ 2 ticks) else samples, **no flag filter** | Composed \|v_grip + L·θ̇·n̂\|, L = fused length, scale = (clubLen − 0.13 m)/fused px | P1–P7 (masked after the P7 knot) | mph | 0.959 ± 0.022 of the launch monitor on six pairs (a lower bound). The GC Quad over-reads ~2 mph, so ≈0.98 of true. Default club length 1.12 m (driver) inflates a 7-iron ×1.22 when no club is recorded. |
| `clubheadPeakLead` | face-on | the clubheadSpeed curve | Running median ±17 ms, last sample ≥ 0.97 × peak, ms before P7 | Impact | ms | Corpus median 3.7 ms, p95 49 ms. |
| `handSpeed` | face-on | synth else samples | Grip velocity, smoothed | — | mph | Same scale as clubheadSpeed. |
| `lagAngle` | face-on | synth/samples nearest sample, **no flag filter, no max gap**; lead forearm from pose (conf ≥ 0.15) | \|wrap(θ − forearm)\| | P5, Impact (min over P5–P6) | ° | |
| `transitionPlaneDelta` (+ ι_back, ι_down) | face-on | measured channel (headConf > 0, not synth) else synth channel | §6.6 | Transition | ° | Experimental; absolute ι uncalibrated; only the `shallowing` characteristic reads it. |
| `swingPlane` | fused | face-on Measured + DTL measured; synth as bridge | Back/down plane inclination − address plane | P3 (back), P6 (down) | °, σ = plane RMS | Inclination is robust to yaw (≤ 1° over ±15°); heading is not published. |
| `clubAngularSpeed` | face-on (or IMU) | synth else samples | θ de-projected through the fused downswing plane (or the conic plane), differentiated over 25 ms | Transition → P7 | °/s | Kinematic sequence. The plane also de-projects the *lead arm*. |

Not metrics, but derived from the shaft:

- the P-positions and P-ladder events (P2, P3, P5, P6, P8 promoted by `PositionsLadderStage`);
- the P7 / Impact event (§4.15);
- the vision Address / Takeaway / Top / Impact / Finish segmentation for camera-only swings;
- the club length;
- Skeleton3D's club direction (measured face-on within ±4 ms and DTL within ±6 ms).

---

## 9. Plumbing: pipeline, reuse, persistence, GUI, tools, tests, switches

### 9.1 Inputs from the job (`ShotAnalysisJob`, `shot_analyzer.h`)

| Field | Used for |
|---|---|
| `clubLengthM` | Default **1.12** (driver). Length ladder, segment geometry, speed scale. |
| `bandCentersMm` | E1 band match. |
| `hoselFromButtMm`, `shaftLengthMm`, `handsEndMm` | Segment geometry (E4). |
| `priorClubLenPx/VarPx/N` | Persistent length prior. |
| `impactUs` | The acoustic or marker impact anchor. |
| `handedness` | Which elbow is the lead. |
| `fullWindow` | Evidence over the whole window instead of the swing span. **A trap on live-captured swings: it re-runs pose, ball, shaft and ladder.** |
| `tuningOverrides` | The `shaft.*` / `positions.*` / `synth.*` / `fusion.*` / `shaft.dtl.*` keys. |

Live, `ShotProcessor::buildAnalysisJob` fills these from AppSettings. On re-analysis, `SwingReanalyzer` fills them from `capture.club` in the document (never AppSettings).

### 9.2 Reuse under the version gate

Stage versions are in `analysis_versions.h`:

- `kShaftStageVersion` = 5
- `kDtlShaftStageVersion` = 2
- `kShaftFusionStageVersion` = 5

A re-analysis reuses the persisted face-on track (samples, positions, lengths) only when **all** hold:

- the pose and ball were reused;
- there are no tuning overrides;
- the stored shaft version matches;
- a club block exists.

On reuse, ShaftStage:

1. recomputes the forearm angles from pose;
2. re-runs `demoteImplausibleFollowThrough`;
3. re-runs `resynthesizeLayerC`;
4. builds the DTL witness from the sample flags (no trace).

DTL and fusion are never reused; they always recompute (~0.3 s). Changing any producer requires bumping its version (HARD rule), otherwise old documents keep old tracks.

### 9.3 Persistence (`swing_doc.cpp`)

**`analysis.club`** is written for valid **or refused** tracks.

- Grip and head are normalised by the frame size. Times are window-relative.
- Contents: samples (with flags), the always-empty `predicted`, synth, lengths, positions (with timing), plane, `addressBall`, `diag`, `refused`, `wedgeObs`.
- ~269 KB on a typical swing (745 samples, 321 synth ticks), against 13 MB of pose.
- **Not persisted:** `addressPhaseFrame`, `onsetFloorFrame`, `addrBallTrusted`. `diag.onsetTUs`/`topTUs` are written but not read back.

**`analysis.clubDtl`** uses schema `pinpoint.clubDtl/1`, the same bytes as SwingLab's `club_dtl.json`. It carries the config hash: a FNV-1a hash over every resolved scalar, so a results table can say which configuration produced it.

**`analysis.club3d`** uses schema `pinpoint.club3d/1`.

A two-camera document grows +27–40 MB, dominated by DTL pose.

### 9.4 GUI

The replay overlay is `PpCameraFrame.qml`. Club drawing is gated on `club.valid`, so **a refused track draws nothing**.

**Measured shaft:**

- a "blueprint" line with end ticks, a head ring (alpha × headConf) and a grip ring;
- a head trail over the last 10 measured heads.

**Projected heads** (HeadProjected, including off-frame) draw as a lone dim line. Synth ticks carry `ShaftSynthesized` but not `HeadProjected`, so they draw in the *measured* style.

**Fan mode** draws the synth plus measured samples outside the synth span. **P-markers:** MilestoneFit positions in green, TrackSample in the accent colour.

**DTL tile** (`dtl_overlay_payload`):

- RAY/SEG/BAND drawn;
- HELD drawn dimmer;
- synth3d dashed at alpha 0.55.

**Presets** (`ViewLayout.qml`): "Club track", "Club + lead arm", and "**Synthetic club**" (shaft + synth3d only, for judging the 3-D synthetic line against the measured DTL club).

**Coach lines:** the DTL "address shaft plane" line runs through the measured grip nearest Address and the ball.

**Swing3D** does not read `club.samples`. It reads Skeleton3D's grip, shaft direction and tier, the positions, and the fused downswing plane.

### 9.5 Tools

- **`tools/swinglab/src/swinglab_run.cpp`** is the lab CLI. `--trace` writes one `trace.jsonl` line per frame with every decide internal: tier, θ_dp, θ_out, ψ residual, φ, θ_ball, head radii, raw/dif p97, support at the DP, segment and band fields, wedge rows and edges. `--dtl` writes `club_dtl.json` + `trace_dtl.jsonl`.
- **Multi-swing corpus runs go on the studio PC** (memory: corpus sweeps on GOLFSIMPC, Release). Run trees go on the share, not local disk.
- **`tools/shaftlab/`** is the Python exemplar the C++ was ported from (`club_track_v3.py`, `stripe_fusion.py`, `clubhead_*.py`, `fusion_geom.py`, `plane_probe.py`, graders and montages). It is **retired as a development surface** (Mark, 8 Sept). Several parity pins remain; its README's file table predates v3.

### 9.6 Tests

`src/Analysis/tests/`:

- shaft: `shaft_decide_test`, `shaft_evidence_test`, `shaft_segment_test`, `shaft_wedge_test`, `shaft_onset_test`, `shaft_positions_test`, `shaft_position_fit_test`, `shaft_synthesis_test`, `shaft_resynth_test`, `shaft_kinematics_test`, `shaft_plane_test`, `shaft_plane_corpus_test`, `shaft_fusion_test`, `shaft_hand_axis_prior_test`;
- DTL: `dtl_shaft_decide_test`, `dtl_shaft_post_test`, `dtl_shaft_bands_test`, `dtl_shaft_lie_test`, `dtl_shaft_synth3d_test`;
- related: `ball_anchor_test`, `impact_geom_test`, `club_length_fusion_test`, `clubhead_measure_test`, `clubhead_temporal_test`, `hand_axis_test`, `club_delivery_test`, `positions_ladder_test`;
- `src/Export/tests/swing_doc_test.cpp` round-trips the flags.

Run them through `ctest`; bare binaries lack the norms environment and fake-fail.

### 9.7 Switches

Every `shaft.*` key with its status (LIVE/DARK), the commit that set it, and the gate evidence is in `docs/developer/feature_switches_developer_guide.md` §3.7–§3.14. The deliberately dark ones and why:

| Switch | Why it is dark |
|---|---|
| `seg.placeHead` | Worse head: 73 px vs 34–48. |
| `wedge.kinModelV2` | Starves the trigger. |
| `handAxisPrior` | Gate never run. |
| `impactGeom.retime` | Scatters ±20 ms. |
| `followThrough.minShaftForearmDeg` | Demotes real finishes. |
| `synth.ballAnchorSigmaDeg` | Moves P7 speed ~9 mph with no truth to say which way is right. |
| `fusion.dtlAnchor` | Degenerate. |
| `rasterC2`, `spanBound=false`, `psiRail=false` | Python parity oracles. |

---

## 10. Shortcomings

What follows is everything I found that is wrong, weak, unproven, inconsistent or fragile, grouped by kind. Where a limitation is a deliberate design decision, that is said. Being deliberate does not stop it being a limitation.

### 10.1 The evidence base: one golfer, one studio, thin truth

1. **Every constant was tuned on one right-handed golfer** in one studio, essentially one taped 7-iron, one untaped 7-iron session in daylight, one unmarked 6-iron session and one DTL gap wedge.
   - The tracker has dozens of measured thresholds: onset box 7 px / gap 15, swSpd 8, rhoSolveMin 0.50, the head-floor ramps, the wrist-cock table, σ fractions in the length fusion, σ_a = 5,000°/s².
   - Each is a statement about *this* golfer's tempo, *this* camera's scale (~3.5 mm/px) and *this* lighting.
   - There is no left-handed swing, no second golfer, no driver (the "DRIVER" sessions were the taped 7-iron), no graphite shaft, and no second studio in the corpus.
   - The project's own docs say so: "validates machinery, not accuracy claims".

2. **Truth is thin, and partly self-referential.**
   - **Band-lock "truth" is not independent.** It is the E1 engine, and against hand marks "the marked club was never 0.3 degrees" (commit e319d148).
   - **Hand marks are biased toward frames a human can see**, which are the frames the tracker also finds easy.
   - **Impact-zone truth on a bare club "has no truth of the right kind"** (research §19). Every claim about P6–P8 on the markerless stack is unverified.
   - **DTL truth is address-region only.** No DTL hand marks exist, three of four DTL bands have no automatic truth, and the DTL ablation rows and a deterministic re-run were "NOT RUN — owed".
   - **Instant timing truth:** 13–14 swings.

3. **Pixel-scale constants are absolute pixels.**
   - Search radii (rHi 470 px, rmax 0.62·H), lateral offsets (±9/±12 px), snap ranges (±45 px), hand-cleaning tolerances (40/64 px), still thresholds (2 px, 2.5 px/frame), onset box (7 px), body margin (34 px) and the 98 px ridge floor are all tuned at ~3.5 mm/px and 1280×1024.
   - A camera further away, a different resolution or a crop changes their meaning. Only a few constants scale with the frame (rmax, the head floors, the DTL shadow area).
   - The 720 px-wide 06-11 face-on session and the 512–576 px DTL frames already run on the same absolute constants.

4. **Speed constants are absolute frames.**
   - w_max per phase (deg/frame), `impHalf` 12 frames, `stillMin` 25 frames, `onsetRunBridgeFrames` 10 and the run length ≥ 7 assume ~150 fps.
   - Some are converted from µs (collars, A3 clamp, top repair); many are not.
   - At 120 or 240 fps the per-frame bands and window lengths mean different physical things.

5. **Determinism is per machine.** Cross-platform bit-equality is untested (an MSVC/GCC atan2 LSB difference has been seen). Pose itself is non-deterministic run to run on the studio PC. Byte-identity gates hold only on one machine with pinned pose.

### 10.2 Physics the camera cannot see

6. **Bare steel through impact is invisible at a 6.6 ms exposure.**
   - Everything the tracker publishes for P6→P8 on an unmarked club is constructed: the DP's smoothness, ψ reconstruction, the wedge's edge reading, or the synth.
   - "The 9.4° 'agreement' on those frames is two constructions coinciding."
   - The wedge edge is a real measurement of *the blur*, but its σ (4.5°) is the median hand-mark residual on 32 swings, not a calibrated per-frame uncertainty.

7. **The head goes dark around impact.** The Stage-2 measured head is absent from roughly −45 ms to +40 ms around impact. Low point, attack angle and the speed therefore read the synth arc. That is an **interpolation between P6, P7 and P8**, and "its vertex is pinned near the P7 anchor".

8. **Single-view face-on loses depth.**
   - θ is an image angle; "parallel" is image-plane parallel.
   - The clubhead is ~0.5 m nearer the lens at address and impact (perspective makes the club 10–17% long).
   - Shaft lean, the face-on plane and the face-on speeds all ignore the depth component.
   - The fused 3-D path exists only on the 34 two-camera swings.

9. **The forearm roll after impact is a third rotational degree of freedom** face-on cannot see. ψ monotonicity, and anything built on it, is meaningful only from address to impact.

10. **The DTL view has structural blind spots.**
    - P2, around the top, and P6 are end-on gaps by physics.
    - OCCLUDED at the top is "the hands, not the club".
    - No 2-D curve can bridge a pole.
    - Fusion is ill-conditioned at impact (cond 0.06–0.08), because the DTL camera looks along the plane's node.

### 10.3 Algorithmic weaknesses

11. **The pose is the single point of failure, and it is not trustworthy.**
    - Grip, φ, the body hull, the phase model, the ray origin, the ball geometry and the length floors all come from hand and body keypoints.
    - "A pose difference of median 0.6% of the frame put the entire backswing on the wrong structure" (16 Sept W02 s2, ViTPose-B vs -L).
    - The defences are heuristic: hand cleaning, the hands ladder, the trail-arm veto, refusal.
    - The grip anchor is ~39 px off-axis by construction, and much of the downstream machinery compensates for it rather than fixing it.
    - **The hand-axis prior** (the WholeBody hand direction as a θ prior) was built and **never gated**.

12. **The phase model is a stack of patches over one fragile signal (grip speed).**
    - Two-longest-runs ranking, bridging, two candidacy clamps, m3gate, top repair, onset reseed, A1/A2/veto walk-back, A3 clamp, invariant backstop, self-check, retry ladder.
    - Each fixed a named swing. Together they are hard to reason about: interactions are found by corpus A/B, not by analysis.
    - It fails on unusual swings (pumps, waggles, pitch shots, very slow takeaways).
    - **The DP's allowed directions are hard-wired to this model.** A wrong phase label forbids the correct path: a frame labelled Downswing cannot rotate backwards. This is why phase-model collapses produced "the DP walks the backswing the wrong way under downswing constraints (150–160° at P2)".

13. **The C3 rotation sign ignores chirality.**
    - `phaseSign()` forces θ-index *increasing* in the backswing and *decreasing* from the downswing on, whatever `chir` says. The wide cone and the wrist-cock predictor do use `chir`.
    - For the corpus geometry (right-hander, face-on, target image-right) the shaft's image angle does increase through the backswing.
    - **For a left-hander filmed the same way the image rotation reverses**, and the banded Viterbi would forbid the true path in every swing phase. The design's own risk list says "add a LH capture to the corpus before v3.0 freezes"; there is none.
    - The same applies to a mirrored camera.
    - (Shaft lean flips its sign for `handedness==2`, but the tracker underneath does not.)

14. **Percentile normalisation hides absolute strength.**
    - Every frame produces a full-strength "winner". `evAbsFloor` (100) and `raySupportMin` (0.4) are the only absolute statements.
    - The floor is set to be "inert on the blessed corpus", so it protects only against total blackness, not against a weak but structured wrong line.

15. **Evidence ties are broken by priors that encode this golfer.**
    - Where the shaft and a limb or trouser line both reach normalised evidence ≈ 1, the decision falls to: the arm vetoes, the C2 hull, the ball well, the wedge kinCone (via the stereotyped wrist-cock table), and the DP's smoothness.
    - The wrist-cock table is hand-authored and indexed by swing progress. Against truth its median residual is −12° and its p10–p90 spread 78°.
    - The fitted v2 table is better (27°) but dark because it starves the wedge trigger.

16. **Address depends on the ball.**
    - With no trusted ball, address frames are not segment-probed, the θ_ball well is absent, and the DP coasts whatever θ it had at the span edge backwards over the hold.
    - The 15 Sept impact-camera rig hid the ball: 11 of 13 broken swings that session had no address ball.
    - The decoy check needs at least one supported line at θ_ball. On a daylight bare-steel session there is none, so the ball is not trusted even when it is right.
    - The INV §5a session-level "face-on unusable when the ball is hidden" warning is specified but not built.

17. **The snap has a known tail it cannot fix.**
    - On the daylight session it re-registers onto "the lead arm at the top and the leg at address", costing 15 of 60 marks.
    - The design says the fix "needs the elbow keypoint inside the tracker", and the snap's only arm test is the lead-forearm veto direction.
    - It is disabled at address, the first 80 ms of the takeaway, and through impact, precisely where it hurt.

18. **The Stage-2 head stops at the last lit steel** more often than at the clubhead.
    - Head p90 is ~100 px, and the backswing has a "systematic short-lock" (confidence capped at 0.45).
    - `seg.placeHead` would place the head from the segment terminus but measured *worse*.
    - The flip check refuses blessing but never corrects.

19. **The length fusion's σ fractions** come from 11 swings of one club, camera and athlete.
    - E-band and E-head read 25–30% short of E-ball. That is a foreshortening bias absorbed into σ rather than removed.
    - The pose stature rung assumes a 1.70 m golfer.
    - The default club length is a 1.12 m driver; a missing club record silently mis-scales every speed (a 7-iron reads ~22% fast).

20. **The DTL tracker's priors are placeholders.**
    - Corridor half-width 25° "PLACEHOLDER — Stage 0 measures it", never measured.
    - The depth-sign split was never established (60/40).
    - The D1 reverse-ray test is waived on 82–85% of published frames, so it is effectively off. The lateral-proximity replacement is unbuilt.
    - The D2 limb veto fired on zero frames on its measurement set ("unearned").
    - `rhoSolveMin` 0.50 "does not transfer across rigs".
    - A confirmed forearm lock was published at P2 on 06-11 (θ 225°).
    - The shadow ball cue "is a scene assumption in disguise".
    - The two L̂_D estimates disagree by −15% to +11%.
    - The 06-11 address angle sits 4–6° above its own ball line, unexplained.

21. **Fusion and synth3d assume an uncalibrated, orthographic camera.**
    - Yaw, pitch and roll are assumed zero, while the stick shows yaw 4–9°, roll 2.4–6.5°, and a camera that moved mid-session.
    - Plane *heading* moves one-for-one with yaw and is not published. Inclination is claimed robust (≤ 1° over ±15° yaw), but that was measured on one session.
    - Perspective is ignored.
    - The backswing is "not a plane" (`backIncoherent`); the backswing self-plane is refused on 18 of 24 swings.
    - η(t), HELD and synth3d are **ON by Mark's decision despite failing their gates**, to be judged in the app. That is a legitimate call, but they are unvalidated.

22. **The face-on conic plane is experimental and uncalibrated.**
    - A body-depth bias up to 64° is bounded but not removed.
    - Split-half "cannot see validity" (an ι 89° needle scored 0.00°).
    - It changed sign on a golfer who is over the top every swing.

23. **Impact timing is still layered heuristics.** The acoustic anchor is 13–22 ms early; the geometry override is one-sided; the sub-frame retime is dark (±15 ms scatter). The P7 used by different metrics differs (§10.5).

24. **Synth coasting and the follow-through.**
    - "The coasting model's undamped rotation remains open."
    - The follow-through rules (rate caps, measured-end) are guards against known failure shapes, not a model.
    - The forearm test is off because the face-on projection makes a real finish look like an arm lock.

### 10.4 Honesty contract versus what the metrics actually read

The tracker's tiers are carefully honest. Several metrics then read across them:

25. **The synthetic tier feeds metrics, while the flag comment says it does not.**
    - `ShaftSynthesized` is documented in `swing_analysis.h` as "EXCLUDED from metrics/scoring/estimands", and `SynthConfig::enabled`'s comment says "metrics never read synth".
    - In fact clubheadSpeed, handSpeed, lagAngle, clubheadPeakLead, attackAngle, lowPointAhead, clubAngularSpeed, the conic plane's fallback channel and the fusion bridge all read it.
    - Since `fitEvidence` the synth *is* a fit to the measurements, which justifies much of that use. But it is a fit **anchored hard to P-positions that are themselves often Proxy-timed**, and its σ_a was chosen against 283 marks.
    - Turning `synth.enabled` off now changes speeds and removes low point. It is no longer a display switch.

26. **Several metrics read every sample regardless of tier.**
    - `impactShaftLean` reads **all** samples, coasted and implausible included, and differentiates nothing, but takes θ at impact from whatever tier is there (often RECON or WEDGE).
    - `lagAngle` takes the *nearest* sample with no tier filter and no maximum time gap.
    - The speeds and `clubAngularSpeed` read synth-or-samples with no filter.
    - The conic plane's "measured" channel admits any sample with headConf > 0, including HeadProjected / Coasted / Implausible. That is inconsistent with club_delivery's stricter `headMeasured` (≥ 0.30, projected excluded).
    - Each choice has a reason in its own comment, but the result is that the metric layer does **not** inherit the tracker's honesty by construction; each metric re-decides.

27. **σ is not always propagated.**
    - Shaft lean ships a constant σ 9.5° even when ball-anchored (sd 5.3°).
    - Low point's σ 2.0 in is from one session of six swings.
    - Most metrics carry no σ at all; a coasted-tier value and a band-tier value look the same on the card.

28. **`attackAngle` is computed and charted but read by no characteristic**, because the camera fallback "flipped shallow ↔ steep on the same swings". It still appears in the UI.

### 10.5 Engineering and code-health issues

29. **Three different impact instants.**
    - Shaft lean uses `job.impactUs` (the raw acoustic anchor).
    - Club delivery, the plane, the lie and fusion use the ladder Impact (possibly geometry-corrected).
    - The speed mask, peak lead and kinematic sequence use the P7 knot.
    - The buildShaftLeanSeries comment says "the hands at the P7 instant"; the code uses `job.impactUs`.
    - Within one swing these can differ by the 13–22 ms anchor bias or more.

30. **Dead flags and dead fields.**
    - `ShaftImuBridged` has no producer since the v3 port (7 July). Nothing writes `ShaftKinematicPredicted` or the `predicted[]` series (always persisted empty). `imuVisionCorr` is always 0.
    - The tracker header still advertises IMU streams and segmentation as inputs "accepted for call-site compatibility but unused".
    - Consumers still test these bits, and the synth envelope "admits IMU-bridged samples".
    - **Consequence:** on a *reused* track, `resynthesizeLayerC` derives `isPred` from the dead `ShaftKinematicPredicted` flag, so it is always false. The impact-boundary fit's "prefer measured frames" filter therefore never removes anything on reuse, whereas live it uses the real PRED tier. **A reused track's synth is not the synth the live run produced.**

31. **Wedge frames are not "Measured".** `ShaftWedge` deliberately omits `ShaftMeasured`. Consumers that test only `ShaftMeasured` (fusion's measured face-on test, Skeleton3D's face-on lookup, `buildFaceOnWitness`'s ρ_F) treat delivery-zone wedge frames as unmeasured. Some of that is intended, because a wedge carries no length, but it is not stated at those sites.

32. **θ wrap convention violated.** `ShaftSample2D::thetaRad` is documented as [0, 2π), but `applyBallAnchor` writes raw `atan2` values, which can be negative. Most consumers wrap or unwrap, but the documentation is wrong, and any consumer that trusts it is exposed.

33. **Coverage and validity are computed before later edits.** `coverage`/`valid` are frozen before the follow-through demotion and the ball anchor. A track with many demoted follow-through frames keeps its pre-demotion coverage.

34. **Round-trip gaps.** `addressPhaseFrame`, `onsetFloorFrame` and `addrBallTrusted` are not persisted; `diag.onsetTUs`/`topTUs` are written but not read back. A re-written reused document carries −1 for onset and top. The reuse path cannot reproduce the trust decision for the address ball; it re-derives what it can from flags.

35. **Stale and contradictory text.** The code comments this guide originally listed here were corrected in the commit after it:
    - the `ShaftSynthesized` / `ShaftImuBridged` / `ShaftKinematicPredicted` / `ShaftWedge` / `thetaRad` / `ShaftPosition::timing` / `imuVisionCorr` / `predicted` notes in `swing_analysis.h`;
    - the synth readers in `shaft_synthesis.h` and `club_delivery.h/.cpp`;
    - the "dark at merge" config comments in `shaft_track_assembly.h/.cpp`;
    - the ClubDelivery, ImpactAnchor, Kinematics, ShaftPlane, shaft-lean and fusion address-plane stage comments in `wrist_analyzer.cpp`;
    - `kinematic_series.h`, `shaft_plane.h`, `shaft_positions.h`, `analysis_versions.h` (reuse note and the missing version-history entries), `swing_doc.cpp`, `dtl_shaft_types.h` (p90, not p95);
    - the `swinglab_run` header and a status note on the shaftlab README.

    **Still stale, because they are user-visible catalogue text rather than comments** (`metric_catalogue_manifest.cpp`, and pinned by the catalogue tests):
    - `attackAngle.howToRead` ("needs a MEASURED clubhead");
    - the `clubheadSpeed` / `handSpeed` routes ("scaled by the ball-diameter ruler" — they use club length);
    - `swingPlane.howToRead` ("one number per swing").

    The behaviours the corrected comments now describe (dead flags, the reuse `isPred` divergence, the three impact instants) are unchanged and remain shortcomings in their own right (items 25–34).

36. **The deciding core is one ~1,750-line function.** `decideTrack` interleaves evidence, emission edits, DP, segment passes, reconciliation, length ladder, two fusions, the head pass, placement, demotion, snap, impact geometry, positions, the milestone fit, synthesis, self-checks and trace filling.
    - Its correctness rests on a documented but fragile **evaluation order**: the band well last, re-asserted after the wedge and ball well; tiers before placement; demotion before snap and anchors.
    - Many behaviours are guarded only by "byte-identical when dark" corpus gates rather than unit tests of the interaction.
    - The `ShaftDecideTrace` struct has grown to ~60 fields.

37. **Cost.** Evidence is 2 sweeps × 360 rays × ~230 samples per in-span frame, plus wedge sweeps, segment probes (±1° × several directions × 2 passes), the snap grid (~580 line integrals per sample), the head pass (forward + flip ray with a lateral band) and the milestone fit's stacks.
    - The frame cache holds up to 1.2 GB of gray frames.
    - It is fine offline on the M4 (~3 s), but it is not a live-path algorithm, and over-cap spans fall back to serial decode.

38. **The exemplar is retired but still cited as the oracle.** Parity tests pin the C++ to Python behaviours (nearest-neighbour sampling, median-of-4, the scipy reflect boundaries) that no longer need to be kept, and the Python has stopped evolving. The oracles survive as switches (`rasterC2`, `spanBound=false`, `psiRail=false`) in production config.

### 10.6 What would most change the picture

These are listed as consequences of the shortcomings, not as a plan:

- **A second golfer and a left-handed capture**, before any accuracy claim leaves the building. Item 13 says the left-handed case is likely broken outright.
- **Independent truth through impact** (a high-speed reference camera, or hand marks on the delivery frames of the markerless club). That would grade the synth, the wedge edges, lean, attack angle and low point.
- **A calibrated DTL camera** (the protocol session's stick clips): heading, the synth3d line and the fusion conditioning all wait on it.
- **Moving the metric layer onto the tier contract**, with one impact instant and propagated σ, so honesty is inherited rather than re-decided per metric.
- **Removing the dead IMU/predicted plumbing** and the stale comments, so the next reader sees the code that runs.

---

## 11. Glossary

| Term | Meaning |
|---|---|
| **θ** | Shaft image angle, grip→head, atan2 with y down. 90° = straight down. |
| **φ** | Lead-forearm image angle, elbow→grip. |
| **ψ** | θ − φ: the club relative to the forearm (image wrist hinge). |
| **chir** | Chirality: sign of the forearm's rotation takeaway→top. |
| **bs0 / onset / top / impact / fin0** | Phase-model landmarks: takeaway start, top, impact frame, finish start. |
| **span** | [bs0 − 100 ms, fin0 + 100 ms], the frames that get evidence. |
| **E1 / E2 / E4** | Band match / ridge sweep / steel-segment lock. |
| **C1–C4** | Butt-termination / body free space / one reversal / arm coupling. |
| **Tier** | What earned a frame's angle. Face-on: BAND, SEG, RAY, WEDGE, RECON, PRED. DTL: BAND, RAY, HELD, END_ON, OCCLUDED_*, UNSEEN. |
| **Measured** | A tier earned from pixels (face-on BAND/SEG/RAY, plus WEDGE as "evidenced"; DTL BAND/RAY). |
| **Coasted / projected** | θ or head carried by the model, not seen. |
| **Synth** | Layer C 240 Hz series between P-anchors (face-on), or the 3-D synthetic DTL line. |
| **Wedge** | The blur fan of a fast-rotating shaft; its leading edge is the shaft at frame time. |
| **ρ_F, ρ̂_D** | Face-on projected length fraction; the predicted DTL visible fraction. |
| **Band (DTL)** | A maximal run of sighted DTL frames, solved independently. Not the same as a retro-band on the club. |
| **cond** | Conditioning of a two-plane intersection, \|n₁ × n₂\|. |
| **ι (iota)** | Conic-plane inclination, arccos(minor/major). |
| **η (eta)** | Fused out-of-plane angle curve. |
| **A1 / L_px** | The address-hold grip→ball club length. |
| **Refused** | A track contradicted by its witnesses: valid = false, draws nothing, metrics "–". |
| **Dark / frozen ON** | Switch OFF and bit-identical to before / switched ON after a corpus gate. |
