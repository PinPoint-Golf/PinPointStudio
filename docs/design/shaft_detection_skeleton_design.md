# Shaft Detection — Skeleton-Aware Scaling & ROI (current vs. revised)

> **Status:** **PROPOSAL / design** · **Date:** 2026-06-19 · **Grounded against:** `main` @ `c71c73c`
> **Author:** design review (shaft detection ⨯ pose skeleton)
>
> **Scope.** This document is *narrow*: it covers only how the club-shaft detector
> derives its **search scale**, its **angular search region (ROI)**, and its
> **physical-plausibility / error gating** from the human pose skeleton — what it does
> today, where it under-exploits the skeleton, and a revised approach. It does **not**
> re-specify the detection math (anchored radial transform,
> motion-blur wedge, Viterbi/KF assembly) — that lives in
> [`docs/design/shot_analyzer_design.md`](shot_analyzer_design.md) addendum **B.1–B.10**
> and the as-built [`docs/implementation/shaft_tracker_impl.md`](../implementation/shaft_tracker_impl.md).
> Read those first for the algorithm; read this for the skeleton-coupling changes.

---

## 0. The observation that triggered this

> *"The shaft detection makes no attempt to use the skeleton — a golf club is longer than
> an arm (a scaling clue), and no attempt is made to align the shaft with the arms. Arms
> hanging down ⇒ the club hangs down; arms out to the side ⇒ club out to the side. The ROI
> for the shaft should be derived from the arm positions."*
>
> *"Too often it gets obsessed on a region and 'detects' a shaft that is impossibly small.
> Confidence should be zero for any shaft shorter than the length of the arm."*

The instinct is **right about the lever, wrong about the absolute**. The detector is *not*
skeleton-blind — it is anchored on the hands, scaled from the body silhouette, and masks the
forearms (§1). But it exploits the skeleton **only globally and only as a mask**: it scales
from whole-body *stature*, not from the *arm*; and its single positive directional prior
(inter-hand direction) is short-baseline and frequently unavailable. The **arm geometry
sitting right at the grip — the most informative, most reliable skeletal cue for both scale
and club direction — is currently used only to *subtract* the forearm, never to *predict*
the shaft.** That is the gap this document closes.

---

## 1. What the skeleton IS used for today (correcting the premise)

The detector is the offline `ShaftTracker` (S1 per-frame detection in
`src/Analysis/shaft_tracker_math.cpp`; S2 orchestration in `src/Analysis/shaft_tracker.cpp`).
Its sole input pose is `PoseTrack2D` (17 COCO keypoints + hand centroids per frame,
`swing_analysis.h:199`). Three skeleton-derived quantities feed detection today:

| # | Skeletal cue | What it drives | Where | Always available? |
|---|--------------|----------------|-------|-------------------|
| A | **Grip anchor** `g` = midpoint(leadHand, trailHand) | The 1-DOF pivot — the entire transform is *rays from `g`* | `shaft_tracker.cpp:74-77` (`anchorAt`) | **Yes** — wrists fall back when hand centroids are low-conf |
| B | **Body silhouette height** → `pxPerM` → search radius `R_max` | The radial extent of the search disc | `shaft_tracker.cpp:104-125` (`medianPoseHeight`), `:175-180` | Only when ≥10 keypoints span the body |
| C-1 | **Inter-hand direction** `normalize(trail − lead)` | *Soft* von-Mises bump on `S(θ)` | `shaft_tracker.cpp:82-88` → `shaft_tracker_math.cpp:178-179` | **No** — disabled when `handConf == 0` (wrist-fallback) |
| C-2 | **Elbow directions** `g → elbow` (both arms) | *Hard* ±12° clutter mask (zeroes `S(θ)`) — removes the forearm false positive | `shaft_tracker.cpp:89-99` → `shaft_tracker_math.cpp:167-176` | When elbow conf ≥ 0.30 |

So: **anchor (A) and scale (B) are global; the only positive directional prior (C-1) is
short-baseline and often missing; the arm is otherwise used only to mask (C-2).**

### 1.1 The scale path in detail (cue B)

```cpp
// shaft_tracker.cpp:175-180
const double bodyFrac = medianPoseHeight(pose);                         // normalized silhouette height
const double pxPerM   = bodyFrac > 0.05 ? bodyFrac * h / kAssumedStatureM : 0.0;  // kAssumedStatureM = 1.70
const double radius   = pxPerM > 0.0 ? 1.25 * job.clubLengthM * pxPerM
                                     : 0.5 * std::min(w, h);            // fallback: half-frame
dcfg.maxRadiusPx = float(std::clamp(radius, 80.0, double(std::min(w, h))));
```

`R_max` is `1.25 × L_club × pxPerM`. `L_club` is known in metres (`job.clubLengthM`, default
1.12 m driver). The only unknown is **`pxPerM`**, and today it is recovered from the
*full-body bounding-box height* against an *assumed 1.70 m stature*. The original design
(B.2, B.3 step 1) explicitly flagged this as "prior-quality scale… it only sizes a search
region" — acceptable, but the weakest link.

### 1.2 The directional path in detail (cues C-1, C-2)

`buildThetaWeights` (`shaft_tracker_math.cpp:155-185`) builds the per-θ multiplicative weight:

- **Hard mask:** weight `= 0` within ±`clutterMaskDeg` (12°) of each `g→elbow` direction.
- **Soft bump:** multiply by a von-Mises bump (σ = 25°, floor 0.3) toward `interHandDirRad`
  **iff `prior.hasInterHandDir`**.
- A second soft bump toward `predictedThetaRad` exists **iff `prior.hasPredictedTheta`** —
  **but `shaft_tracker.cpp:215-221` never sets `hasPredictedTheta`.** The machinery is wired;
  no per-frame producer feeds it. (IMU coupling happens later, in S2 assembly.)

**Net:** on a common wrist-fallback frame (no WholeBody hand decode, `handConf == 0`), the
angular search is a **full unweighted 360° scan** with only the two forearm wedges masked
out. Nothing tells the detector that the club hangs *down and away from the shoulders*.

---

## 2. The gaps (precisely)

**G1 — Scale is global, metric-assuming, and framing-fragile.**
`pxPerM` needs the *whole body* in frame and an *assumed stature*. Golf framing routinely
crops the lower body (tight DTL, face-on chest-up) → `bodyFrac` collapses → fallback to a
**half-frame** `R_max`, which is enormous: it admits far-field clutter (alignment sticks,
mat edges, the shaft's ground shadow at long ρ) and inflates the scan. Stature also varies
±15% across athletes, biasing `pxPerM` even when the body *is* fully framed.

**G2 — No arm-direction prior; the strongest, always-present directional cue is unused.**
The club is mechanically tied to the arms: at address and through impact the shaft is very
nearly the **extension of the lead-arm line beyond the grip**. The skeleton hands us that
line for free (shoulder→grip is a long, high-confidence baseline). Today it is used only to
*mask* the forearm (C-2), never to *predict* the shaft. The one positive prior (inter-hand,
C-1) is a **short** baseline (hands are centimetres apart → a few noisy pixels) and **dies on
wrist-fallback frames**.

**G3 — The arm⇄club kinematic coupling is not modeled at all.**
"Arms hang ⇒ club hangs; arms out ⇒ club out" is a real, exploitable constraint, strongest in
the quasi-static phases (Address, Impact, Finish) and bounded — never absent — through the
dynamic phases by the wrist-cock angle. The detector encodes none of it, so it cannot down-
weight the large fraction of the 360° circle the club essentially never occupies (e.g. up
through the torso toward the head).

**G4 — No physical-length plausibility gate; short dense ridges become high-score "shafts."**
The only length floor today is **absolute**: `minVisibleLenPx = 30 px`
(`shaft_tracker_math.h:75`, gate at `shaft_tracker_math.cpp:307-309`). A club shaft is
*physically* ≈ 2× an arm long, yet a 30–50 px ridge — a glove seam, a watch/logo, a bright
forearm highlight, a wrinkle, the grip cap — clears that floor easily, and because
`runScore` rewards **mean ridge strength** (`shaft_tracker_math.cpp:138-140`) a *short but
dense* ridge can outscore the real, partly-low-contrast shaft. Worse, this is *sticky*: once
a frame's winner is a tiny bright blob near the grip, the S2 Viterbi transition cost
(`shaft_track_assembly.cpp`) rewards temporal continuity and **locks onto the same wrong
region across frames** — the "gets obsessed on a region" failure. The detector has every
number it needs to reject this (it knows the arm length under R1) but never applies the
constraint: **a detection shorter than one arm length is physically impossible for a shaft and
must score zero, not merely score low.**

**G5 — No kinematic *angle* envelope; the detector can pick a club direction the swing cannot
produce.** There is no guardrail on the club *angle*. The only angular shaping is the soft
inter-hand prior (C-1, often absent) and the hard forearm mask (C-2) — neither is derived from
the **lead-arm angle**, and neither encodes the single strongest 2-D constraint in a golf
swing: the club is the **second link of a double pendulum** hinged at the wrists, so its angle
is tied to the lead-arm angle by the wrist-cock angle, which follows a stereotyped curve
through the swing. With phase known (we have the segmentation ladder) the club angle is
predictable from the lead-arm angle to within **~±20–30°**; even without phase it is bounded to
a ~90–110° arc on a known side. The detector exploits none of this, so a strong off-axis ridge
(an alignment stick, a trouser crease, the trail arm) can win even when it points somewhere the
club physically *cannot* be for the current arm pose and swing phase. This is the angular twin
of G4's missing length gate — and the lever the user is pointing at.

**G6 — Blur in the delivery→impact zone is handled *reactively*, and R5 would actively fight
it.** The original detector *does* have motion-blur machinery (addendum B.4, "read the wedge,
don't fight it"): a fast shaft images as a **fan/wedge** about the grip — a *plateau* in `S(θ)`,
whose centroid is the mid-exposure angle and whose width measures `ω·t_exp` — handled by the
`refineTheta`/twin-horn code, with the insight that the **proximal shaft barely blurs**
(image velocity ∝ ρ). So the *spirit* the user asks for is present. But three things are not:
> 1. **It is reactive, not predictive.** The pipeline runs the same thin-ridge detector
>    everywhere and reads a plateau *if one happens to be there*; when even the wedge drowns it
>    emits nothing and coasts on IMU. After the lead arm passes parallel the shaft is often a
>    *faint, wide, semi-transparent* smear with no crisp ridge **anywhere** — the ridge response
>    (9 px top-hat/black-hat) never fires, so there is no plateau to read and vision silently
>    gives up exactly where the user wants it to "work with the blur."
> 2. **R5 (my addition) would kill the blurred shaft.** Only the innermost ≈ `shaft_width /
>    (ω·t_exp)` px stays crisp — at `ω≈30 rad/s`, `t_exp≈2 ms`, ~4 px shaft ⇒ only ~65 px — which
>    is **shorter than one arm**. R5's "visible length ≥ one arm" gate, as written, drops the
>    real shaft in precisely this phase. The length gate must become blur-aware.
> 3. **The detector still *looks for a straight object*.** The TLS line refit and single-ray run
>    length assume a line; in the high-ω phase the success criterion should switch to "is there
>    swept energy consistent with the *predicted fan*?" — which R6's `ω` prediction now makes
>    possible (we know where the fan is and how wide it should be *before* looking).

---

## 3. Revised approach

The changes are surgical: R1–R3 + R5 touch only `anchorAt()`/`AnchorState` in
`shaft_tracker.cpp`, the `AnchorPrior`/`ShaftDetectConfig` structs, and
`buildThetaWeights()`/`pickPeaks()` in `shaft_tracker_math.cpp`. R6 adds one new pure header
(`shaft_kinematics.h`, table + predictor, standalone-testable). R7 is additive output +
opt-in overlay instrumentation (a parallel `predicted` series); its only schema change is
additive. R8 (blur-first mode) is the one larger change — T1 reuses existing response maps,
T2 (temporal differencing) is deferred. No live-path change, no new dependency.

### R1 — Scale `R_max` from the **arm**, not the silhouette

Recover `pxPerM` from the **visible arm-segment pixel length** instead of the whole-body
bounding box. The arm is (a) *always* visible — we needed it to locate the grip; (b) *local*
to the grip, so its foreshortening matches the club's; (c) far less stature-sensitive than
whole-body height; (d) independent of lower-body framing.

```
armPx   = ‖shoulder − wrist‖           (lead arm preferred; see §8 "Lead-arm identification")
pxPerM  = armPx / L_arm_nominal         (L_arm_nominal ≈ 0.52 m acromion→wrist; scale by
                                         athlete height when the profile has it)
R_max   = clamp( 1.25 · L_club · pxPerM , 80 , min(w,h) )
```

This keeps `L_club` (so different clubs still scale correctly) and only replaces the *source*
of `pxPerM`. Equivalent metric-free form, useful as a sanity bound:

```
R_max ≈ 1.25 · (L_club / L_arm_nominal) · armPx ≈ 2.6 · armPx   (driver; ratio ≈ 2.1×)
```

i.e. *the club images about 2–2.6× the lead-arm length* — exactly the user's "a club is
longer than an arm" scaling clue, made quantitative (driver ≈ 2.1×, 7-iron ≈ 1.8×, wedge ≈
1.7× of full-arm length).

**Scale ladder (most → least trustworthy):**
1. Full arm `shoulder→wrist` (both conf ≥ 0.30) — preferred.
2. Forearm `elbow→wrist` × ~1.9 (upper-arm occluded but elbow visible).
3. Current silhouette + stature path (no arm keypoints) — *retained as fallback*.
4. Half-frame (no usable pose) — *retained as last resort*.

### R2 — An **arm-hang directional prior** on `S(θ)` (the headline change)

Add a soft, always-available prior centred on the direction the club hangs from the arms:

```
armHangDir = atan2( g.y − shoulderRef.y , g.x − shoulderRef.x )     // image coords, y down
```

`shoulderRef` = lead shoulder (preferred), else shoulder midpoint. This is the
**shoulder→grip** ray — long baseline, shoulders are among the highest-confidence COCO
keypoints, and it is computable on *every* frame including wrist-fallback frames where the
inter-hand prior (C-1) is dead. It literally encodes "arms hang down ⇒ club hangs down; arms
out ⇒ club out": the shaft prior points wherever the arms point the hands.

Feed it as a von-Mises bump exactly like the existing soft priors (multiplicative, floored —
**never a hard gate**, per the standing design rule that a prior must not kill a measurement):

- **Quickest implementation:** populate the *already-wired-but-unfed*
  `prior.hasPredictedTheta / predictedThetaRad / predictedSigmaRad` at S1 with
  `armHangDir` and a phase-dependent σ — `buildThetaWeights` already consumes them
  (`shaft_tracker_math.cpp:180-181`). ~20 lines in `anchorAt()`/`track()`.
- **Cleaner implementation (recommended):** add dedicated
  `bool hasArmAxisDir; float armAxisDirRad, armAxisSigmaRad;` to `AnchorPrior` so the
  semantics ("vision arm prior" vs "IMU-predicted θ") stay distinct from the S2 IMU channel.

**Phase-aware width** (σ), because the coupling tightens and loosens through the swing:

| Phase (from `segmentation.eventFor`) | Shaft vs. arm-hang line | σ (suggested) |
|--------------------------------------|--------------------------|---------------|
| Address, Impact, Finish (quasi-static) | shaft ≈ arm extension, ±20° | **25°** (tight) |
| Takeaway / early backswing | club lags, wrist cocking | 45° |
| Top / transition / mid-downswing | up to ±90° lead/lag (wrist cock) | **70–80°** (loose) |

Even at the loosest setting the prior is informative: with floor 0.3 it down-weights (never
removes) the hemisphere the club cannot reach. When IMU is bound, the S2 `ŝ_hand` channel
already supplies a *tighter* per-frame θ prediction through blur — R2 is the **vision-only**
prior that improves *candidate generation at S1* so S2 has better candidates to associate
(and it is the only directional prior on IMU-less shots).

> **R2 is deliberately the *naive* version** — it centres the prior on the *arm* direction.
> That is correct at address and impact but **~90° wrong at the top of the swing**, where the
> club stands perpendicular to the lead arm. **R6 replaces the mean with the kinematically
> predicted club angle** (`armHangDir + wrist-cock`) and is the real fix; read R2 as the
> degenerate `wrist-cock ≡ 0` case of R6.

**Consistency check (no conflict with the existing mask):** `armHangDir` (shoulder→grip,
pointing *down/away*) is ≈180° from `g→elbow` (pointing *up toward the shoulder*, the C-2
mask direction). The new soft bump and the existing hard forearm mask point in opposite
hemispheres — they reinforce, they do not fight.

### R3 — Arm-derived angular **sector** (the user's "ROI from arm positions") — *soft, optional*

The user asked for the ROI itself to be arm-derived. R2 already narrows the *effective*
search (low-weight bins rarely win). A harder **angular sector** — skip bins outside
`armHangDir ± sectorHalfWidth` — would cut compute and false positives further. **We
recommend implementing this as a *soft floor*, not a hard cut**, for one concrete failure
reason: at the top of the backswing the club can swing nearly 90–120° off the arm-hang line,
and a hard sector risks dropping the true club there. Since a full 360° scan already costs
only ~2 ms/frame (impl doc S1 timing), the compute saving does not justify the miss risk.
Net: realise "ROI from arm positions" as the **R2 weighting**, widened by phase; expose a
`shaft.sectorHalfWidthDeg` override for experiments but default it to "soft/none."

> **R6 lifts this caution.** The reason a hard sector was unsafe is that R2's mean is wrong at
> the top. Once R6 centres the sector on the *kinematically predicted* club angle, a
> moderately tight sector is defensible **everywhere** — the ROI becomes a true annular sector
> (radius from R1, angle from R6). See R6.

### R4 — Seed S2 from the arm line at Address (synthesis)

`armHangDir` at the Address frame is a near-perfect seed for two S2 steps that currently
bootstrap from vision alone:
- the first Viterbi node's expected θ (today the first transition just adds ~20° slack —
  `transNoRateExtraRad`), and
- the `ŝ_hand` calibration's initial guess (today a blind 800-point spherical lattice).

Seeding both from the arm line tightens early association and speeds calibration convergence.
Low priority relative to R1/R2 but nearly free once R2 computes `armHangDir`.

### R5 — Physical-length plausibility gate (kill the "impossibly small shaft")

**Rule (the user's requirement, verbatim intent): a candidate whose visible length is shorter
than one arm length scores zero — it is dropped, never merely down-weighted.** A shaft cannot
image shorter than the club, and the club is ≈ 2× an arm; even allowing for heavy occlusion,
*half* the club (≈ one arm) is the most generous floor that is still physically defensible.

```
armPx        = ‖shoulder − wrist‖                      // already computed for R1
minShaftLenPx = max( minVisibleLenPx ,                 // keep the absolute 30 px floor
                     minLenFracOfArm · armPx )          // NEW: relative floor, default 1.0
```

Pass `minShaftLenPx` into `ShaftDetectConfig` and **apply it where the absolute floor already
lives** — the candidate quality gate in `pickPeaks()` (`shaft_tracker_math.cpp:307-309`),
which today reads:

```cpp
if (p.w >= minW && visLen >= cfg.minVisibleLenPx)   // ← replace cfg.minVisibleLenPx with cfg.minShaftLenPx
    out.push_back(p);
```

Because the gate runs **at candidate selection (S1)**, a sub-arm ridge never becomes a
candidate, so it never enters S2 Viterbi — which is what breaks the "obsession": the bad
attractor is removed *before* the temporal cost can lock onto it. If a frame's *only*
above-noise ridge is sub-arm, that frame yields **no vision measurement** (empty candidate
list) and S2 fills it by IMU bridge / coast — exactly the desired outcome: **no measurement
beats a confidently-wrong tiny one.**

**Two supporting checks (defence in depth):**

- **S2 safety net.** In `ShaftTrackAssembly`, when the selected per-frame observation's
  `visibleLenPx < minShaftLenPx` (e.g. a candidate that squeaked through on a frame with a
  bad `armPx`), force `conf = 0` and clear the `ShaftMeasured` flag for that sample. Literal
  realisation of "confidence should be zero," at the output boundary.
- **Length-continuity outlier.** Visible shaft length changes *smoothly* (foreshortening is
  gradual). A frame whose `visibleLenPx` collapses to a small fraction (< ~0.4×) of the local
  running median while its neighbours are full-length is almost certainly a misdetection;
  down-weight or reject it. This catches sticky wrong-region locks that happen to exceed the
  static arm floor. (Complements, does not replace, R5's absolute floor.)

**Occlusion caveat (why the floor is the *arm*, not the *club*).** Visible length is
legitimately reduced by torso crossings (bridged by `runMaxGapPx`, so both sides still count),
body occlusion, frame edges, and — in DTL, not the supported face-on view — by foreshortening
when the shaft points toward/away from camera. The arm-length floor (≈ half the club) leaves
≈ 50%+ occlusion headroom, so it rejects the impossible without rejecting the merely-occluded.
`minLenFracOfArm` is a `shaft.*` tuning override (default 1.0); relax toward ~0.8 only if the
top-of-swing over-rejects on real footage (S2 IMU-bridging covers the few affected frames
anyway).

> **Blur interaction (critical — see G6/R8).** This arm-length floor is correct for *sharp*
> frames but **must not apply in the high-ω (wedge) phase**, where only the proximal ~65 px of
> the shaft stays crisp and the floor would reject the real blurred shaft. When a candidate is a
> wedge (`wedge == true`) or the phase is delivery→impact, R5 switches from "single-ray run
> length ≥ one arm" to a **fan criterion** (swept energy across the predicted angular extent,
> R8) — the length plausibility is evaluated on the blur, not against a straight object.

### R6 — Lead-arm → club-angle correlation (the double-pendulum wrist-cock model)

This is **R2 done right** and the answer to "do we have an angle guardrail?" (today: no). Model
the swing as a **double pendulum**: link 1 = the lead arm (shoulder→hands, image angle
`φ_arm`, measured directly from the skeleton), link 2 = the club (hands→head, image angle
`φ_club`). The wrist hinge couples them by the **wrist-cock angle** `β = φ_club − φ_arm`, whose
magnitude follows a stereotyped curve — ≈ 0° at address → ≈ 90° at the top (the "L") → held
(lag) through transition → released back to ≈ 0° at impact → recocked to ≈ 90° at the finish —
with the club on the **trail side** of the lead arm from takeaway to impact and crossing to the
**lead side** through the follow-through. The prediction is then:

```
φ_club_pred(t) = φ_arm(t) + sign(branch) · β̂(s)        // s = swing progress, from phase events
prior width / envelope half-width = σ_β(s)
```

**Correlation table** — seed values from golf biomechanics, refined empirically (below). This
is the "very strong correlation table" the user described; it is the entire model's payload:

| Swing point (phase event)        |  s   | wrist-cock \|β̂\| | club side of arm | σ_β |
|----------------------------------|------|------------------|------------------|-----|
| Address                          | 0.00 | 0–10° (≈ collinear, slight forward lean) | lead (lean) | 8°  |
| Takeaway (shaft ∥ ground)        | 0.15 | 20–35°           | trail            | 15° |
| Lead arm ∥ ground (backswing)    | 0.35 | 60–80°           | trail            | 20° |
| Top of backswing                 | 0.50 | 85–100° ("L")    | trail            | 22° |
| Transition / early downswing     | 0.60 | 90–110° (lag)    | trail            | 30° |
| Delivery (shaft ∥ ground, down)  | 0.80 | 35–60° (releasing)| trail           | 25° |
| Impact                           | 0.90 | 0–15° (released, hands ahead) | ≈ collinear | 10° |
| Extension (post-impact)          | 0.95 | 15–40° (**sign flips**) | lead       | 20° |
| Finish                           | 1.00 | 80–110° (recocked, wrapped) | lead    | 30° |

**Three uses, in increasing assertiveness:**

1. **Sharpen the R2 prior mean** *(candidate generation)*. Centre the soft bump on
   `φ_club_pred` instead of `armHangDir`; width `σ_β(s)`. At the top this corrects the prior by
   ~90° — the single biggest accuracy gain for S1.
2. **Plausibility envelope** *(the guardrail the user asked for)*. The kinematic arc
   `φ_club_pred ± k·σ_β` (k ≈ 3) bounds where the club can be. **Soft-penalise** candidates
   inside a margin; **hard-reject only on gross violation** (e.g. > 4σ_β *and* on the wrong
   side of the arm) so a genuinely unusual move is never silently dropped. This is the angular
   twin of R5's length gate.
3. **Flagged fallback predictor** *(coverage)*. When vision yields no candidate and IMU is
   absent/uncalibrated, emit `φ_club_pred` as a low-confidence sample flagged
   `KinematicPredicted` (analogous to `ImuBridged`) to bridge blur/occlusion gaps on IMU-less
   shots. Never overrides a real measurement; a distinct flag so consumers can tell.

**Resolving the double value (branch).** The same `φ_arm` occurs twice — once on the backswing,
once on the downswing/through — with *different* β̂, so the branch must be known. It is: the
segmentation ladder gives pre-Top / post-Top / post-Impact directly; absent it, `sign(φ̇_arm)`
disambiguates everywhere except the velocity-zero turn points (Top, transition), where we
simply widen `σ_β` and fall back to the soft prior. No hard rejection near turn points.

**Seed now, learn later — and calibrate from the IMU per shot.** The table is a *defensible
seed* from double-pendulum understanding; because it lives in **image space** (the face-on
projection of the 3-D wrist cock), it should be **refined empirically from the SwingLab
corpus** rather than hand-tuned. Best of all: on IMU-equipped shots the lead-hand sensor
measures the *actual* wrist cock every frame — so **IMU shots train and verify the table that
vision-only shots consume.** Ground-truth where we have it, model where we don't.

**Honest bound on "predict purely from one metric."** `φ_arm` *alone* (no phase) pins the club
to a ~90–110° arc on a known side — already killing ~¾ of the circle, a strong guardrail but
not pointwise-accurate. `φ_arm` **+ phase + branch** (all of which we have for free) tightens it
to ±20–30°, competitive with the IMU prior. So the user's claim holds *with* the near-free
phase/branch disambiguation, and degrades gracefully (to the wide arc) without it.

**Reconciliation.** R6 subsumes R2's mean and lifts R3's "stay soft" caution (the ROI sector
can now centre on `φ_club_pred`, valid even at the top). The ROI becomes a true **annular
sector**: radius ∈ [ρ_min, R_max] from R1, angle ∈ `φ_club_pred ± k·σ_β` from R6 — a 2-D region
derived almost entirely from the lead arm plus phase.

### R7 — Dual output: *predicted* shaft vs. *actual* shaft

The detector should emit **two parallel series**, not one:

- **Actual** — `ShaftTrack2D::samples` (the existing series): the shaft the detector *inferred
  from the pixels* (vision measurement, fused/smoothed in S2). "What we found."
- **Predicted** — `ShaftTrack2D::predicted` (**new**): the shaft the **R6 kinematic model**
  expects from the lead-arm angle + wrist-cock table *alone*, computed for **every** frame
  independent of what vision found. "What the model says."

**Data-model delta (additive, reuses the existing sample type):**

```cpp
struct ShaftTrack2D {
    ...
    std::vector<ShaftSample2D> samples;     // ACTUAL  — detector-inferred (unchanged)
    std::vector<ShaftSample2D> predicted;   // NEW     — pure R6 model, flags = KinematicPredicted
    float modelVisionResidualDeg = -1.f;    // NEW     — RMS|actual − predicted| over confident, prior-free vision frames
};
```

`predicted[i]`: `thetaRad = φ_club_pred`, same `gripPx` anchor, `headPx = grip + clubLenPx ·
(cos φ, sin φ)` (full predicted length from R1's scale), `visibleLenPx = clubLenPx`, `conf` from
`σ_β(s)`, `flags = KinematicPredicted`. **It is nearly free** — R6 already computes `φ_club_pred`
for the prior; R7 merely *also stores* it as a series instead of discarding it. Critically,
**emitting `predicted` is independent of `useKinematicPrior`**: the model series is always
produced; the toggle only controls whether the model *biases the actual detection* (see below).

**Why two series — the payoff.** Overlaying both makes model and detector mutually checkable,
frame by frame:

| predicted vs. actual | vision conf | reading |
|----------------------|-------------|---------|
| coincide | high | model **and** detector agree — validated |
| diverge | high | **detector error** (the "obsession" / off-axis lock) *or* a genuinely unusual swing — inspect |
| diverge | low / none | model **extrapolating** into a vision gap (blur/occlusion) — expected |
| coincide | none | this `actual` frame *is* model-filled (R6 fallback, use #3) — the overlay reveals it |

**Overlay (S4, `PpCameraFrame.qml`) — a developer/test visualization, default-off.** Draw
`predicted` in a distinct **ghost** style (dashed, semi-transparent, different hue) behind the
solid `actual` line, gated by a diagnostic toggle — it is *not* end-user replay chrome, it is
"shown when testing code based on the model," per the request. Optionally render the **±k·σ_β
envelope** as a faint cone from the grip, so the R6 guardrail is *visible* and you watch
candidates fall inside/outside it. (Honour the subtle-chrome convention: muted, behind the
media, off until enabled.)

**Methodological guardrail — keep the comparison honest.** To validate the *model*, the `actual`
series it is compared against must be **prior-free vision** — run with `useKinematicPrior =
false` so R6's bump did not pull the measurement toward its own prediction (otherwise the
comparison is **circular**: the prior would be grading its own homework). In production both stay
on (the prior helps detection); in the **model-validation A/B** the prior is detached so `actual`
is an *independent witness*. `modelVisionResidualDeg` is therefore accumulated only over
prior-free `ShaftMeasured` frames.

**Self-diagnosis & the R6 learning loop.** `modelVisionResidualDeg` joins `imuVisionCorr` as a
per-shot health metric and QA gate. On the corpus it is *exactly* the signal that fits/sharpens
the β̂ table (R6's "seed now, learn later") — the residual **is** the table error. On IMU shots
the loop closes three ways on one timeline: IMU-measured wrist cock, vision-measured shaft, and
model-predicted shaft, all directly comparable.

**Persistence.** Add `predicted` additively to the `swing.json` club/shaft block (and the
normalized replay export at `shot_processor.cpp:148-171`), behind a flag so normal exports are
not bloated; SwingLab turns it on to compute residuals offline.

### R8 — Blur-first detection in the high-ω zone (predict the fan; stop hunting a line)

The delivery→impact phase needs a **mode switch**, not just graceful failure. The original B.4
wedge handling is the right *representation* (a fan with `θ_centroid` and `σ = ω·t_exp/2`); R8
makes it **predictive** by spending the two things R1/R6/IMU now give us — a known angle and a
known angular rate — so vision keeps contributing through the blur instead of coasting.

**Trigger & Calibration.** Enter blur mode when the predicted rate is high — `ω̂ = φ̇_arm·(1 + dβ̂/ds·ṡ)` from
R6, or the IMU `θ̇` when bound, exceeds a threshold (≈ the rate at which `ω·t_exp` exceeds the
ridge kernel) — *or* simply when phase ∈ {delivery, impact, early follow-through}. Shutter exposure
`t_exp` is calibrated dynamically using a two-pass pre-pass: we fit `t_exp = median(observedWedgeWidthRad / ω̂)`
over moderate-speed wedge frames where $\omega \ge 5.0\text{ rad/s}$ (bootstrapping with a default of 3 ms).

**Predict the fan, then verify it (matched filter, not peak-hunt).** We know the fan *before
looking*: centre `φ_club_pred` (R6), angular extent `≈ ω̂·t_exp`. So:
1. **Stop requiring a straight ridge.** Skip the TLS line refit and the single-ray run-length
   test; the success criterion becomes "is there swept energy consistent with the predicted
   fan?" (the user's "don't keep looking for a straight object").
2. **Integrate, don't threshold-per-pixel.** Within the narrow predicted θ-window, *sum* the
   (signed) response across the fan extent and along ρ. A semi-transparent smear is faint
   per-pixel (each pixel is occluded only `~shaft_width/(ω·ρ·t_exp)` of the exposure) but
   **coherent over the predicted region** — integration recovers it where a per-pixel ridge
   threshold cannot. Because the R6 envelope is tight, we can **lower the threshold inside it**
   without inviting false positives. The threshold scale `blurThreshScale` and envelope width
   are scaled dynamically based on the prior-free tracking residual calculated in the pre-pass:
   larger residuals (e.g. from atypical stances) expand the prior bounds and scale the threshold
   toward 1.0 to avoid false positive accumulation.
3. **Proximal anchoring (B.4's insight, now phase-gated).** Weight toward small ρ where the
   shaft stays crisp; the proximal tangent is the reliable `θ`, the distal fan sets `σ`.
4. **Relax R5 here.** Length plausibility uses the fan's proximal-crisp extent + swept area, not
   "≥ one arm" along one ray (see the R5 blur note).

**Output.** A wedge candidate: `θ` = fan centroid (refined toward `φ_club_pred`), `σ_θ = ω̂·t_exp/2`,
low-but-nonzero `conf`, `flags |= Wedge`. It already flows through B.5's KF (which takes wedge
width into `R`) and through R7 (the **envelope cone is literally the fan we expect** — the
test overlay shows the predicted wedge, and `actual` either lands in it or the model bridges).

**Implementation of T1 & T2 Tiers:**
- **T1 — surgical, most of the value (Implemented):** Reuses the existing response maps: phase/ω-gated
  proximal weighting + lowered threshold + matched-filter integration *inside the R6 envelope* +
  R5 relaxation.
- **T2 — Background Subtraction Smear Pass (Implemented):** Decodes a median background luma map
  from 15 evenly-spaced window frames, computes absolute difference maps, dynamically masks out the moving
  golfer's body using skeleton joint coordinates, and passes this motion-only difference matrix to the
  swept-energy integrator inside the R6 kinematic prior envelope. This isolates the faint club sweep from static background clutter.

If even the background-difference pass drowns (very fast + low light + low contrast): emit no vision measurement
and let R6/IMU bridge — the layer-4 "never fabricate" rule stands. R8 *widens the band* where vision
still contributes; it does not pretend to see a shaft that is not there.

---

## 4. Why the arm is the right cue (numbers)

- **Anthropometry.** Acromion→wrist (full arm) ≈ 0.50–0.55 m for adults; forearm
  (elbow→wrist) ≈ 0.26–0.28 m. Driver ≈ 1.12–1.17 m, 7-iron ≈ 0.95 m, wedge ≈ 0.89 m. So
  club/arm ≈ 1.7–2.2× — a stable image-space ratio that needs **no full-body framing**.
- **Confidence.** Shoulders and elbows are consistently the highest-scoring COCO keypoints
  in a golf setup (torso faces or sides the camera, rarely self-occluded at address); the
  shoulder→grip baseline is ~0.5 m → hundreds of pixels at typical framing, so its *direction*
  is far more stable than the ~few-pixel inter-hand baseline.
- **Mechanics.** A flat lead wrist puts the shaft on the lead-arm line at address and impact;
  through the swing the deviation is the wrist-cock angle (bounded, and itself measured by the
  lead-hand IMU when bound). The arm line is therefore a *bounded* predictor everywhere, not
  just a static one.
- **Double pendulum (R6).** The swing's textbook model is two links hinged at the wrists —
  lead arm then club. The hinge angle (wrist cock) is *not* free: it loads to ~90° by the top,
  lags through transition, and releases to ~0° at impact. That stereotyped curve is precisely
  what makes `φ_club` predictable from `φ_arm` + phase, and what bounds the plausibility
  envelope. It is the most-studied invariant in swing biomechanics — a solid foundation for a
  seed table that the corpus (and the IMU) then sharpen.

---

## 5. Degradation ladder & failure modes

| Condition | Behaviour |
|-----------|-----------|
| Both arm segments confident | R1 arm scale + R2 arm-hang prior (tight by phase) |
| Only forearm confident | R1 forearm×1.9 scale + R2 prior from elbow→grip |
| Shoulders low-conf, grip known | R2 falls back to inter-hand (C-1) if available, else no positive prior (today's behaviour); R1 → silhouette |
| No usable pose | unchanged: half-frame `R_max`, unweighted 360° (current fallback) |
| Top-of-swing wide club | **R6 centres the prior on `φ_club_pred` (club ⟂ arm there)** — the case R2 alone gets wrong; envelope widened by `σ_β`, never hard-rejected near the turn point |
| DTL / non-face-on view | arm foreshortens *with* the club (same plane) — R1 stays consistent; R2/R6 σ loosened, β̂ table is view-specific (note for the DTL extension, addendum B.10) |
| Sub-arm "shaft" near the grip | **R5 drops it at S1** ⇒ no candidate ⇒ no Viterbi lock; frame becomes IMU-bridged/coasted, not confidently-wrong |
| Heavily occluded but real shaft | R5 floor is the *arm* (≈ half club) ⇒ ≈ 50%+ occlusion headroom; `runMaxGapPx` bridges torso crossings so both sides still count toward visible length |
| Off-axis clutter (alignment stick, trail arm, crease) | **R6 envelope** penalises/rejects it as kinematically impossible for the current arm pose + phase |
| Branch ambiguous (Top / transition, φ̇_arm ≈ 0) | R6 widens `σ_β` and falls back to the soft prior — no hard rejection where the branch can't be resolved |
| No phase available (segmentation failed) | R6 degrades to the wide ~90–110° arc on the velocity-resolved side — still kills ~¾ of the circle |
| Delivery→impact, shaft is a wedge | **R8 blur mode:** predict the fan from `ω̂·t_exp` at `φ_club_pred`, integrate inside the R6 envelope, **R5 relaxed**; output a wedge (`σ = ω̂·t_exp/2`), not a forced line |
| Peak blur, faint semi-transparent smear | R8-T2 temporal-difference pass; if it still drowns, emit nothing → R6/IMU bridge (never fabricate) |

**Guard rails kept intact:** *priors* stay multiplicative with floor 0.3 (no hard gate on a
measurement). The two **physical-impossibility** gates are the deliberate exceptions where a
hard zero is correct: **R5** (length < one arm) and **R6's gross-violation** clause (angle
> 4σ_β *and* wrong side of the arm). Both are conservative by construction. The forearm clutter
mask (C-2) is unchanged; the detector still returns empty rather than fabricate when nothing
plausible is found.

---

## 6. Implementation surface (estimated)

| Change | File | Approx. |
|--------|------|---------|
| Compute `armPx`, arm-scale ladder | `shaft_tracker.cpp` (`track`, near `:175`) | ~25 lines |
| Compute `armHangDir`, phase σ; populate prior | `shaft_tracker.cpp` (`anchorAt` + `track`) | ~30 lines |
| `AnchorState` arm fields | `shaft_tracker.cpp:48-54` | ~4 lines |
| `AnchorPrior` arm-axis fields (cleaner path) | `shaft_tracker_math.h:101-106` | ~3 lines |
| Consume arm-axis bump | `shaft_tracker_math.cpp:155-185` (`buildThetaWeights`) | ~5 lines |
| Compute `minShaftLenPx` (R5), pass into config | `shaft_tracker.cpp` (`track`, near `:175`) | ~4 lines |
| Apply R5 floor at candidate gate | `shaft_tracker_math.cpp:307-309` (`pickPeaks`) | ~1 line |
| R5 S2 safety net + length-continuity outlier | `shaft_track_assembly.cpp` (smooth/output) | ~15 lines |
| **R6 kinematics model** — seed table + `predictClubAngle(s, φ_arm, branch, handed) → {mean, σ}` and `clubAngleEnvelope(...)`; pure, standalone-tested | **new** `src/Analysis/shaft_kinematics.h` + `tests/shaft_kinematics_test.cpp` | ~120 lines |
| R6 wire-in: resolve phase/branch, feed `φ_club_pred` as the R2 prior mean, apply envelope at the candidate gate | `shaft_tracker.cpp` (`anchorAt`/`track`), `shaft_tracker_math.cpp` (`buildThetaWeights`/`pickPeaks`) | ~30 lines |
| R6 fallback sample + `KinematicPredicted` flag | `swing_analysis.h` (`ShaftSampleFlags`), `shaft_track_assembly.cpp` | ~10 lines |
| R6 IMU-calibration loop (per-shot β̂ correction from lead-hand sensor) — *optional, phase 2* | `shaft_tracker.cpp` (uses existing `qHand` per-frame) | ~25 lines |
| New `ShaftDetectConfig` knobs (`armScaleRatio`, `armAxisSigma*Deg`, `sectorHalfWidthDeg`, `minShaftLenPx`, `minLenFracOfArm`, `envelopeKSigma`, `useKinematicPrior`, `kinematicFallback`) + `shaft.*` tuning overrides | `shaft_tracker_math.h`, `shaft_tracker.cpp:155-180` | ~18 lines |
| **R7 dual output** — `predicted` series + `modelVisionResidualDeg` (predicted already computed for the prior) | `swing_analysis.h` (`ShaftTrack2D`), `shaft_tracker.cpp` (`track`) | ~20 lines |
| R7 overlay — ghost `predicted` line + optional ±k·σ_β envelope cone, behind a diagnostic toggle | `PpCameraFrame.qml` | ~40 lines |
| R7 persistence — additive `predicted` block (flagged) | `swing_doc.cpp`, `shot_processor.cpp:148-171` | ~25 lines |
| **R8-T1 blur mode** — ω/phase trigger, proximal weighting + lowered threshold + matched-filter integration inside the R6 envelope, R5 relaxed in wedge mode | `shaft_tracker_math.cpp` (scan/score/gate), `shaft_tracker.cpp` (pass ω̂, t_exp) | ~50 lines |
| R8-T2 temporal-difference / median-background pass — *deferred follow-on* | `shaft_tracker.cpp` (uses S0's frame set) + new helper | ~80 lines |
| R8 knobs (`blurOmegaTrigger`, `expExposureUs`, `blurThresholdScale`, `wedgeEnvelopeKSigma`) | `shaft_tracker_math.h` | ~6 lines |

R1/R2/R5-S1/R6 change only detection inputs, the candidate gate, and a new pure header; the R5
S2 safety net and R6 fallback touch assembly but write only `conf`/flags (and one additive flag
bit). **R7 adds** an additive `predicted` field to `ShaftTrack2D`, an additive `swing.json`
block, and an opt-in overlay — **existing fields, consumers, the replay path, and the live
60 Hz pose path are unchanged.**

---

## 7. Validation plan

- **Unit (synthetic).** Extend `src/Analysis/tests/shaft_tracker_test.cpp`: render a shaft at
  a known angle with a synthetic shoulder/elbow/wrist set, assert (a) R1 `R_max` tracks club
  length within tolerance across framings that crop the lower body, and (b) the R2 prior
  raises the true-θ score and lowers a planted off-axis clutter line, without ever zeroing the
  true bin (floor-0.3 invariant).
- **Unit (R5 plausibility gate).** Plant a *short, bright, dense* ridge near the grip (≈ 0.5×
  arm length) alongside a longer, lower-contrast true shaft; assert the short ridge produces
  **no candidate** (dropped by the arm-length floor) while the true shaft is still detected.
  Add a multi-frame case where the short ridge persists across frames to confirm S2 does **not**
  lock onto it (no "obsession"), and a fully-occluded-to-half case to confirm the real shaft at
  ≈ 0.6× club (> 1 arm) survives.
- **Unit (R6 kinematics).** Standalone `shaft_kinematics_test.cpp`: assert `predictClubAngle`
  reproduces the seed table at the canonical phases and interpolates monotonically between;
  assert the branch flip at impact; assert `clubAngleEnvelope` brackets a synthetic on-model
  shaft and rejects an off-axis planted line (alignment stick) at every phase **except** the
  velocity-zero turn points, where it must widen rather than reject.
- **Corpus calibration of the table (the real win).** On **IMU-equipped** SwingLab clips,
  extract the measured wrist cock per phase from the lead-hand `qHand` and fit β̂(s)/σ_β(s);
  compare against the seed table and replace it with the empirical (image-space) fit. This is
  how the "very strong correlation table" stops being a guess — report the residual spread per
  phase as the honest σ_β.
- **Unit (R8 blur mode).** Render a synthetic *wedge* (uniform sweep over `ω·t_exp`, faint and
  semi-transparent) at a known mid-exposure angle with **no crisp ridge anywhere**; assert (a)
  the thin-ridge path returns nothing, (b) R8 with the predicted angle + ω returns a `Wedge`
  candidate whose `θ` ≈ mid-exposure angle (tol ~2°) and `σ ≈ ω·t_exp/2`, and (c) **R5 does not
  reject it** despite sub-arm crisp length. Add a delivery→impact synthetic sequence and assert
  vision coverage through the blur improves vs. the reactive baseline (and never fabricates when
  the wedge is rendered below the noise floor).
- **A/B on real swings (SwingLab).** Gate every new behaviour behind `shaft.*` tuning
  overrides (default OFF) and sweep via the `swinglab` harness (lab.py + `swinglab_run`):
  compare coverage, `imuVisionCorr`, and false-positive rate with/without R1+R2+R5+R6+R8 on the
  corpus. Headline metrics: **coverage on lower-body-cropped clips** (G1), **directional
  accuracy on wrist-fallback clips** (G2), **false-positive / tiny-detection rate** (G4 — count
  samples whose `visibleLenPx < armPx`; target zero post-R5), **off-axis false-positive rate**
  (G5 — candidates outside the R6 envelope that won pre-R6), and **vision coverage through the
  delivery→impact blur window** (G6/R8 — fraction of `ShaftMeasured` frames in the high-ω span,
  vs. the reactive baseline that coasts there).
- **Unit (R7 dual output).** Assert `predicted` is emitted for **every** frame (including ones
  where vision returned no candidate); assert `predicted[i].thetaRad == φ_club_pred`; assert
  `modelVisionResidualDeg` accumulates only over prior-free `ShaftMeasured` frames; assert
  toggling `useKinematicPrior` changes `samples` but **not** `predicted`.
- **Overlay smoke (headless).** Under the offscreen QML harness, enable the diagnostic toggle
  and confirm the ghost `predicted` line + envelope cone render without disturbing the existing
  `actual` overlay or the replay scrub.
- **Acceptance.** No regression in coverage/`imuVisionCorr` on currently-passing clips; net
  gain on the headline subsets above; **zero sub-arm detections** and **zero kinematically
  impossible (wrong-side, >4σ_β) detections** survive to the output track; `predicted` present
  on every analysed frame.

---

## 8. Deferred / open questions

- **Per-subject arm length.** `L_arm_nominal` could come from the athlete profile (height →
  arm via a regression) instead of a fixed 0.52 m — folds into the athlete/calibration work.
- **Lead-arm identification.** R1/R2 prefer the lead arm; selection needs handedness +
  `mirroredSource`. Until wired, use the *straighter* arm (smaller shoulder-elbow-wrist bend)
  or the shoulder-midpoint→grip ray — both handedness-free. *(This sub-section to be expanded
  when handedness metadata is plumbed.)*
- **DTL extension.** The arm-hang prior generalises to down-the-line, but σ and the lead-arm
  choice differ; tracked under addendum B.10's DTL deferral.
- **Relationship to the IMU `ŝ_hand` channel.** R2/R6 are deliberately vision-only at S1;
  confirm they do not double-count once S2's IMU prior is present (they should not — different
  stages, and S2 fuses, it does not re-detect).
- **Learned correlation table (R6).** Start with the seed table; the calibration loop above
  replaces it with a corpus/IMU-fit β̂(s). Open question: one global table vs. per-club
  (driver vs. wedge wrist-cock timing differs) vs. per-skill-band. Default to one global table;
  revisit if the residual σ_β stays high in transition.
- **3-D projection of β̂ (R6).** The table is face-on image-space and absorbs the swing-plane
  projection empirically. A genuine swing-plane angle (needs C1 intrinsics + plane estimate)
  would make β̂ view-independent and feed the DTL extension — deferred with B.10.
- **Putting / chipping.** The full-swing β̂ curve does not apply; R6 should be gated to the
  Swing session type (and disabled or re-tabled for partial swings).
- **Exposure-time source (R8).** `t_exp` drives the predicted fan width. B.4 recovers a
  per-camera estimate from observed wedge widths; bootstrap from camera metadata when available
  (Aravis/Spinnaker expose it; Qt Multimedia often does not). Open question: feed back the
  R8-estimated `t_exp` into the camera-capabilities block for next-shot priors.
- **R8-T2 temporal differencing.** The median-background / frame-difference faint-smear detector
  is the deferred follow-on; it needs care where the *body* also moves fast (lead arm, hands) so
  the difference image is not pure club. Mask by the R6 envelope before differencing.
