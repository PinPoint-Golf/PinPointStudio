# K0 — segment rates from the two-camera skeleton (2 October 2026)

Work package K0 of `docs/design/ks_skeleton3d_route_design.md` §11. It was run offline, before any
C++. The inputs are:

- the 15 swings of `2026-07-04_Mark-Liversedge_Wrist_01` in the library, each with its persisted
  `analysis.skeleton3d` (re-analysed 2 October);
- for s8 only, five fresh fits through `swinglab_run`, using the pinned `pose3` / `pose2_dtl` poses
  and the session's camera pool. Those run trees are in `/mnt/swingdata/scratch/ks-skel-k0/`.

The tool is `tools/swinglab/skeleton_rate_offline.py`. Per-swing numbers are in
`skeleton_rate_k0_20261002.csv`. The rates come from the same ±12.5 ms local quadratic and the same
`placePeak` replica as `span_pair_offline.py`. The σ_t values are nominal (0.5° per frame), because
`sigmaM` is not persisted.

## Verdict

**All three written rules say GO. But K0 found a problem the rules did not anticipate, and it
changes decision (c).** The trunk rates from the skeleton dip at impact, and a pelvis ring built on
them would place a peak on the shoulder before the dip.

*First reading, superseded by §8:* "the dip is produced by the fit's joint limits". The follow-up on
s8 showed that reading was wrong. Just after impact the down-the-line hips overlap, the fit
discounts them and coasts, and the coast reaches back before impact. The pair's pre-impact data is
clean (§8). The build was held while the cause was investigated.

| Rule (§11) | Result | Verdict |
|---|---|---|
| Pelvis usable fraction ≥ 0.9 on ≥ 12/15 | 1.00 on 15/15 (hip and shoulder lines both) | GO |
| Arm node vs the face-on arm node, median \|Δt\| ≤ 15 ms | **6.7 ms** (skeleton earlier by a median 6.7 ms, p10 −26.6, p90 0) | GO |
| Spline prior (s8): node shift ≤ 10 ms | **0.0 ms** for the pelvis (26.9 ms) and the thorax (127.2 ms) | GO |
| Pelvis/thorax vs the pair | reported below | not gated |

## 1. Continuity — the design's premise was wrong

The skeleton's hip and shoulder lines are usable on every frame of the domain, on all 15 swings.

**But so is the pair.** Its stored pelvis curve is valid on 100% of the domain samples on all 15
swings, and its thorax curve on a median 100% (p10 93%). The "43 of 271 samples" quoted in design §2
is true, but it counts the **whole recording**. The pair route only computes over Transition/Top →
Impact, and on s8 those are exactly the 43 samples. So within the sequence's domain, continuity is
**not** what the skeleton adds. §2 of the design is corrected to say so.

## 2. The trunk rates, and the dip at impact

| Median over 15 (p10–p90) | Skeleton | Today's document |
|---|---|---|
| Pelvis peak before impact | 67 ms (19–102) | pair: "did not peak before impact" (rising) on 15/15 |
| Pelvis peak value | 351 °/s (294–466) | pair at impact 233–583 °/s |
| Thorax peak before impact | 121 ms (98–138) | face-on 9 swings, pair 6 |
| Thorax peak value | 710 °/s (666–878) | — |
| Spearman of the skeleton rate against the stored curve | pelvis 0.36 (0.01–0.65); thorax 0.40 (−0.15–0.64) | |
| Peak if the search ran to impact + 60 ms (§14a, report only) | pelvis +20 ms (−74…+27), thorax +33 ms | |

The skeleton's pelvis peak is not at the domain's late edge on 13 of 15 swings. **So the skeleton rung
would place a pelvis node a median 67 ms before impact.** That would replace "did not peak before
impact" with a timing that looks like a professional's (87 ms). This is why the next section
matters.

**The dip.** On s8, the library fit's pelvis rate runs 500 °/s at −40 ms, then 24, then **−93 at
impact**, then 141, then 479 °/s at +27 ms. The signed pelvis turn sits within ±2° of square from
−20 to +13 ms. Over the 15 swings, the pelvis spends 34–54 ms within ±3° of square on 8 of them. At
the 400 °/s on either side, crossing that band takes about 15 ms.

A pelvis that stops for 30 ms at the ball and restarts at 480 °/s would need about 20,000 °/s² in
both directions. The pair, built from the same two poses, rises steadily into impact on every swing
(s8: 175 → 267 → 507 °/s).

### What causes it — s8, refitted five ways (first pass; see §8 for the correction)

All five fits used the same inputs, rate in °/s, at the frame instants relative to impact:

| Fit | −27 | −20 | −13 | −7 | 0 | +7 | +13 | +20 | +27 |
|---|---|---|---|---|---|---|---|---|---|
| full (production) | 436 | 414 | 331 | 234 | 162 | **115** | 164 | 324 | 486 |
| spline basis off | 434 | 414 | 354 | 257 | 116 | **70** | 164 | 324 | 510 |
| shaft + clubhead + grip terms off | 413 | 439 | 333 | 231 | 185 | **94** | 164 | 347 | 510 |
| acceleration prior off | 113 | 163 | 257 | 301 | 370 | 422 | 284 | 185 | 188 |
| **joint limits off** | 344 | 414 | 398 | 392 | 369 | 352 | 380 | 373 | 358 |

- **Not the spline basis, and not the club terms.** The dip is the same with either switched off.
- **The joint limits looked like the cause** (§8 corrects this). With them off, the pelvis holds 350–415 °/s through impact and
  the dip is gone. The same change takes the thorax's pre-impact trough from 361 to 386 °/s.
- **The prime suspect looked like the lead ankle** (§8: it is not). In the production fit, `lAnkle.dorsi` sits **at its +35°
  limit** from −60 to −27 ms (37, 38, 37, 36, 35°). Unconstrained it wants 46°, and 46° of
  weight-bearing dorsiflexion is not anatomical either. With the foot anchored and the ankle held,
  the lead shank, knee and hip cannot follow, so the pelvis yaw is held: `root.yaw` goes 4.9, 6.4,
  7.2, 8.0° over 20 ms. The real fault is probably upstream (the lead-leg geometry under the
  assumed DTL camera, or the foot anchor), and the limit is where it shows. This was not isolated
  further: there is no per-joint limit switch, and testing one would be a code change.
- **The acceleration prior is what fills the hole.** With it off, the pelvis curve is a different
  shape altogether. So near impact the pelvis yaw is only weakly observed, and the fit's
  constraints, not the hip keypoints, decide its shape.

**This also touches the LIVE rotation route.** `pelvisRotationSigned`, `m_pelvisRotRateP6P7`
(`hip_stall`) and the 3-D view all read this same fit. The "hip stall in the 3-D view" that started
the rotation work (07-04 **s8**, 1 October) is the swing with the deepest dip. It may be this
artefact, not a movement.

## 3. Thorax: shoulder line vs spine chain

The two observables correlate at a median 0.88 (0.80–0.93), but they differ by a median 120 °/s RMS.
Their peak instants disagree badly: shoulder line 121 ms before impact, spine chain 54 ms (0–126).
Neither can be preferred until the dip above is fixed, since both carry it.

## 4. Lead arm — clean

The skeleton's in-plane arm node sits a median 6.7 ms earlier than the face-on arm node (p10 −26.6,
p90 0; 13 of 15 within 21 ms). Its peak is 818 °/s, against the face-on route's 702 °/s and
Cheetham's 980. The arm's own downswing plane fits to 5.3° RMS off-plane. This is the cross-check
between two independent arm routes the design hoped for, and it agrees.

## 6. Club (record only)

The skeleton's shaft node sits 40 ms before impact, the fused-plane node 41 ms, within 2 ms on
13/15 swings. The exceptions are s6 (+93 ms) and s13 (−9 ms). The skeleton's peak value is about 9%
lower (1836 vs 2020 °/s). Decision (b) stands: nothing here argues for moving the club.

## 7. `sequence_order` on 07-04 today

It reads **clean on 15/15**, with P(fire) ≤ 3·10⁻⁵. The driving measure is `m_pelvisRotPeak`, whose
values are 21–47°: the largest turn **angle** between P4 and P7, which is the top of the swing. This
confirms design §10: it is assessed, and it is grading an angle, not a sequence.

## What would have happened if the build had gone ahead

Per decision (c), the pelvis ring would have been ON. 13 of 15 swings would then have gained a
placed pelvis node at a median 67 ms before impact, read off the shoulder before a joint-limit dip.
The 20 September finding (trunk still rising at impact) would have been overturned by an artefact,
and the panel would have shown a textbook sequence on a swing that has not been measured to have
one.


## 8. The follow-up on s8 — the cause is observability, not a fit bug

Mark chose "fix skeleton3d first". Every test below is on s8 alone, the fresh `pose3` / `pose2_dtl`
fit, through `swinglab_run`. Two temporary knobs were added for the tests and then **reverted**:
`skeleton3d.ankleDorsiMaxDeg` and `skeleton3d.gncStartC`. Each was checked first with a control
identical to production. Run trees are in `/mnt/swingdata/scratch/ks-skel-k0/`.

**The pelvis rate at impact, by fit (°/s, frames −7 / 0 / +7 ms):**

| Fit | −7 | 0 | +7 | Notes |
|---|---|---|---|---|
| production (C = 3, ankle ≤ 35°) | 234 | 162 | 115 | dip |
| ankle dorsiflexion ≤ 40 / 45 / 50° | 254 / 231 / 208 | 162 / 185 / 162 | 142 / 165 / 164 | dip stays. The ankle takes whatever range it gets: 24° at address becomes 30, 33, 34° |
| graduated robustness from C = 12 / 24, ending at 3 | 257 / 233 | 162 / 136 | 92 / 93 | dip stays: not a basin problem |
| robust knee C = 5 / 8 | 300 / 346 | 299 / 366 | 283 / 376 | dip fills. Foot slip 21 → 41 / 46 mm |
| robust knee C = 100 (≈ least squares) | 322 | 367 | 444 | no dip. DTL reprojection 4.4 → 8.2 px, slip 66 mm, limits held 23 → 103 |
| feet free (no contact) | 208 | 160 | 139 | dip stays |

**What the fit does with the down-the-line hip keypoints.** This compares the signed horizontal
separation of the DTL hip keypoints with the fitted hip joints reprojected through the fit's own DTL
camera (px, DTL frame 512 wide), from −20 to +33 ms around impact:

| | −20 | −7 | 0 | +7 | +13 | +20 | +27 | +33 | change |
|---|---|---|---|---|---|---|---|---|---|
| observed | 36 | 30 | 24 | 18 | 14 | 2 | −9 | −29 | **−65** |
| production fit | 21 | 16 | 14 | 13 | 12 | 10 | 6 | 2 | −19 |
| C = 100 | 26 | 20 | 18 | 15 | 11 | 7 | 2 | −5 | −31 |
| joint limits off | 25 | 19 | 16 | 13 | 10 | 6 | 4 | 1 | −24 |

The joint-centre to surface-keypoint offset accounts for about ×1.5 (the address ratio). Even
allowing for that, **every fit turns the hips through impact at about half the rate the DTL
keypoints imply**, whatever term is switched off. This is a structural conflict, not a setting.

**Why.** At square the face-on view has no sensitivity to hip yaw: its hip separation sits flat at
77–88 px from −74 to +47 ms (one frame reads 99). So the only yaw information is in the DTL. In the DTL, at square, the
hip line points almost along the camera's ray (γ = 78°), and the two hip keypoints pass in front of
each other. The lead hip's confidence falls from 0.87 to 0.57, and its separation rate jumps from
about 0.7 px/ms (−20 → +13 ms) to 2.3 px/ms (+20 → +33 ms) exactly as they cross. Whether that jump is the hips or the keypoint
detector resolving an overlap cannot be told from this video. The overlap is **after impact**, and that is where the routes differ.

**When the pair's own angle comes within 10° of square, on all 15 swings.** This is
ψ = atan2(d_dtl / r, d_fo) from the documents' poses, the quantity the pair route differentiates:

| | median | range |
|---|---|---|
| \|ψ\| first < 10°, relative to impact | +16.6 ms | **+9.5 … +23.8 ms (15/15 after impact)** |
| ψ at impact | 15.7° | 12.2 … 20.1° |
| ψ at −25 ms | 23.2° | 20.6 … 27.6° |
| DTL hip confidence first < 0.7 (from −100 ms) | +9.8 ms | after impact on 13/15 (−10 ms on s1 and s3) |

So, within the sequence's domain (top/transition → impact), the down-the-line hip keypoints are
**not** in the overlap zone. They move smoothly and accelerate into the ball (s8: 0.35 px/ms from
−73 to −20 ms, 0.6 px/ms from −20 to 0 ms).

- **The skeleton's dip is the skeleton's.** Just after impact the DTL hips overlap. The fit weighs
  those keypoints against the face-on view (blind to yaw at square), the legs and the trunk,
  discounts them, and the pelvis coasts. The motion prior and the ±12.5 ms derivative carry that
  coast back to about −13 ms. That is the dip, and it is why the skeleton cannot be the trunk rung
  through impact on this rig.
- **The pair's "still rising at impact" stands** (parent §13.4). It reads clean pre-impact
  keypoints, and its domain ends at impact. Its known limits are unchanged: an uncalibrated level
  bias, and no truth.
- *A first draft of this section said "neither route measures the trunk rate through impact" and
  proposed a 10° square blind band for both. The table above withdrew that the same day: the band
  would change nothing on any of the 15 swings, and the pair's pre-impact data is clean.*

**What would make the skeleton usable through impact:** a trunk IMU (§9 stage 2: sacrum +
sternum Witmotion, no HackMotion) to grade it, or a DTL camera that does not see the hips overlap at
square. Neither is a skeleton3d code change.

**The live rotation route inherits the coast.** `pelvisRotationSigned` near impact, and so the
P6 → P7 rate `hip_stall` reads, come from the same fit. The s8 "stall" in the 3-D view is this
coast, not evidence of a stall.

**The lead arm is unaffected.** Its 6.7 ms agreement with the face-on arm involves no square-up
geometry, and the arm rung remains clean.

## 9. Parts B and C (2 October, after Mark's decision)

Mark ruled that the cameras stay where they are and that the camera route must do its best while
staying honest. Three parts were agreed: A, the arm rung (`skeleton_arm_g3_20261002.md`); B, a
time-boxed look at what holds the pelvis back; C, an honesty guard on the rotation route.

### B — what holds the fitted pelvis back (s8 only; time box reached)

| Fit on s8 | Pelvis rate at impact (+7 ms) | Verdict |
|---|---|---|
| production | 115 °/s | dip |
| motion prior looser (`fastFactor` 30 / 80) | 69 / 70 °/s, and the dip widens to +27 ms | **deeper**: the stall comes from the observations, and the prior was partly masking it |
| feet free, club terms off, ankle limit wider, spline off, robust loss annealed | 94–165 °/s | dip stays |
| near least squares (C = 100) | 444 °/s | no dip, at the cost of the whole fit |

- **Not an attraction to face-on square.** With the prior loosened the pelvis parks at about 8–10°
  open (7–9° past face-on square), not at square.
- **The conflict is in the lower body at impact.** From the residuals of joint centres against
  keypoints:
  - DTL trail knee: 15–20 px behind from about −40 ms, where it had matched within a few px;
  - DTL lead hip: up to 43 px behind after impact;
  - face-on lead ankle: about 20 px high near impact.
- **What the model assumes.** It has a hinge knee, and all six foot markers are anchored until
  impact + 80 ms (only the lead heel's backswing lift is modelled). A real trail heel lifts and the
  trail knee kicks in before the ball. That is the likely root, but it is **not proven**, and
  changing it is a lower-body model project, not a fix.

### C — does the rotation route need an extra honesty guard? On this evidence, no

- **A data-disagreement guard is too noisy.** It compared the fit's hip bearing with the
  keypoints' own two-view bearing, mapped on Top → P6. The out-of-sample floor (address → top) is a
  median 4.6°, against 7.9° in the 40 ms before impact. After impact the comparison is dominated by
  the keypoints crossing in the DTL view and by the mapping extrapolating. Not usable as a σ
  (`tools/swinglab/rotation_coast_offline.py`).
- **The coast's own signature is real on every swing.** The fit's pelvis rate dips 22–117% within
  about 60 ms of impact on 15/15, with an implied deceleration and re-acceleration of
  4,900–19,000 °/s².
- **But the level at impact is robust.** On s8 every variant fit gives the pelvis at P7 as
  +5.9…+8.1°. The coast distorts the rate's shape, and the level after impact.
- **The stated σ already covers every live reading.** Across the variant fits:

  | Reading (live consumer) | Spread across fits | Stated σ |
  |---|---|---|
  | pelvis at P7 (`m_pelvisRotP7`) | 5.9 – 8.1° | ±3.5° |
  | pelvis rate P6 → P7 (`m_pelvisRotRateP6P7`, `hip_stall`) | 253 – 408 °/s | ≈ ±105–140 °/s |
  | thorax at the top | 98 – 108° | ±10.9° |
  | thorax at the finish (`m_thoraxRotFinish`) | 126 – 135° | ±13.2° |

  The 10% camera-scale term is what carries this.
- **No σ guard was built.** Adding one would double-count. The exposure that remains is VISUAL: the
  3-D view shows the hips pausing at the ball. Whether to mark that in the view is Mark's call.
- **Caveat.** The alternative fits exist for s8 only. The 15-swing statements above are about the
  dip, not about the σ coverage.

## 10. The lower-body follow-up, and K0 re-run (2 October, later)

Mark asked for the lower body to be fixed and K0 re-run. On s8 first, then all 15.

**The term ledger.** A debug-only dump, `PINPOINT_SKEL_TERMS=<path>`, scores every keypoint,
anchor, limit, smoothness and prior term per frame under one loss (C = 3). It compared the
production fit with the least-squares fit that follows the down-the-line hips:

- **Before impact,** following the hips makes the DTL trail knee and trail heel cheaper. The real
  trail heel lifts and the knee kicks in.
- **After impact,** following the hips makes the face-on lead toes cheaper and the lead-toe anchors
  much dearer (+441, +343). The real lead foot rolls after the ball.

**The feet were not the fix.** Contact released per marker, from the keypoints in either view
(debounced; heel lift and foot roll), released the trail small toe at −134 ms and the trail heel at
−93 ms. But the pelvis still stalled (138 / 69 / 95 °/s at 0 / +7 / +13 ms), and the lead foot was
released on keypoint noise. **Reverted.**

**The pelvis yaw had no physical bound.** In the impact window the fit loosened every joint's
acceleration σ by 12×, to about 1800 rad/s² (~100 000 °/s²). With the face-on view blind to yaw at
square and the DTL hips end-on, nothing in the data pins the pelvis yaw there, so it stopped and
restarted for free. The fit already gives pelvis **tilt** its own tight σ for the same kind of
reason. **Pelvis yaw now gets one too**: `skeleton3d.pelvisYawAccRad`, 200 rad/s² (~11 500 °/s²),
never loosened. `kSkeleton3DStageVersion` 3 → 4.

| s8, the library path (GOLFSIMPC) | Pelvis rate, −70 … +40 ms (°/s) | Cost |
|---|---|---|
| yaw σ off (as before) | −23, 45, 198, 236, 324, 276, 165, **70, 47**, 255, 357, 383 | 88 413 |
| 50 rad/s² | 155, 170, 194, 167, 211, 188, 233, 256, 234, 281, 263, 308 | 88 527 |
| 100 rad/s² | 173, 148, 171, 166, 208, 232, 186, 186, 236, 257, 263, 282 | 88 451 |
| **200 rad/s²** | 153, 143, 171, 167, 234, 209, 210, 209, 190, 277, 284, 314 | 88 415 |

**All 15 swings (a scratch copy of 07-04 re-analysed on GOLFSIMPC; the library untouched):**

| | before | after (200 rad/s²) |
|---|---|---|
| stop-restart: implied pelvis acceleration, median [range] | 11 700 [4 900–19 200] °/s² | **3 700 [1 900–5 900]** |
| dip depth, median | 80 % | 39 % |
| reprojection FO / DTL, foot slip p90, limits held | 5.6 / 5.0 px, 21.0 mm, 25 | 5.6 / 5.0 px, 20.9 mm, 23 (no swing moves more than 0.1 px / 0.3 mm) |
| pelvis at P7 · at finish · thorax at top · at finish · X-factor at top | 3.3 · 84.7 · 107.2 · 132.3 · 67.4° | 4.2 · 84.8 · 107.0 · 132.5 · 67.4° |
| **pelvis rate P6 → P7** (`hip_stall`'s measure) | **160 °/s** | **225 °/s** (+7 … +160 per swing) |

So §9's "the stated σ covers it" held for s8's Mac fits but **not** for the library's own fits: the
coast took about 30% off `hip_stall`'s rate. The bound fixes that at the source.

**K0 re-run** (`skeleton_rate_k0_yaw_20261002.csv`; the rules of design §11):

| Rule | Before | After | |
|---|---|---|---|
| pelvis usable fraction ≥ 0.9 on ≥ 12/15 | 15/15 | 15/15 | GO |
| arm vs face-on arm, median \|Δt\| | 6.7 ms | (the document's arm is now the skeleton's own) | GO |
| spline prior | 0 ms | — (unchanged) | GO |
| pelvis vs pair, Spearman (reported) | 0.36 | **0.55** | |
| pelvis rising at impact | 12/15 | 13/15 | |
| pelvis peak inside the downswing (not at the late edge) | 13/15 | **11/15**, median 60 ms before impact | |
| thorax | peak 121 ms, trough, rising at impact 15/15 | unchanged | |

**What K0 now says about a skeleton trunk rung:**

- **Pelvis.** The stop-restart is gone. The curve rises through the downswing at 150–310 °/s, but
  with ±40 °/s wiggles, and the highest wiggle sits about 60 ms before impact on 11/15 swings. The
  pair, reading clean pre-impact data, says "still rising" on the same swings. Whether the rung would
  PLACE those wiggle peaks depends on its in-app σ_t (K0's σ is nominal). A flat curve gives a wide
  σ_t, which means unplaced. That is the measurement to make before the rung is built.
- **Thorax.** Not addressed. Its trough (about 780 → 360 → 900 °/s) is in the spine twist, not the
  pelvis yaw, and its ring stays off.
- **Run-to-run variation on the library path is real.** The same swing re-analysed twice gave
  visibly different pelvis curves (s8: 162 vs 209 °/s at impact), because the DTL shaft is re-run.
  Per-swing K0 numbers carry that noise.
