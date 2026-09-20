# Pairing the face-on and down-the-line spans: what it fixes, and what it then says

**Written 2026-09-20.** Work package K0 behind `kinematic_sequence_design.md` §5.2, §11 item 6 and
§12.4 item 6. Offline, Python only, no C++ touched and nothing re-analysed.

**Rig**: `tools/swinglab/span_pair_offline.py`. Per-swing rows in
`pair_span_turn_20260920.csv` (21 swings × 216 columns). Figures in `figures/07_ks_pair_*.png`.

**Data**: the 21 swings that have a down-the-line pose cache — 12 from 2026-07-04 (512×1024 DTL,
1280×1024 face-on, 150 fps) and 9 from 2026-06-11 (576×1024 DTL, 720×1024 face-on). Face-on pose
is the pinned `pose2` cache; phase ladder and impact from each swing's `swing_phasegrid.json`; the
lead-arm and club nodes are read unaltered from `analysis.kinematicSequence` of the fresh runs in
`build/dtl/{fo_b,heldout,transfer}`. Frame sizes come from `swing.json streams[].encoded`.

**The face-on leg is the 18 Sept rig's own arithmetic** (`span_turn_offline.load_spans`,
`resample`, `anchors`): the span is the **2-D keypoint distance** `hypot(Δx·W, Δy·H)` in pixels —
not `|Δx|` — gated at confidence 0.30, resampled to a 4 ms grid with an 8 ms gaussian, reference
`W` read off the span maximum over the downswing. Placement is the shipped C++ path re-implemented
sample for sample from `angular_rate.h` and `segment_rates.cpp finishChannel`: local quadratic over
a 25 ms window fixed in TIME, `reduceExtremum`'s centred 40 ms windowed mean, `σ_t = sqrt(2σ_r/|r̈|)`,
placement threshold 40 ms.

---

## 1. The headline

**The pairing works as geometry and does not unblock the node, because on this golfer the trunk
peaks after impact.**

Sequence domain as designed (transition → impact), 21 swings:

| Segment | route | produced | placed | bounded | placed peak before impact, ms | σt ms | peak °/s | Cheetham 2008 pros |
|---|---|---|---|---|---|---|---|---|
| Pelvis | face-on only | 21 | **0** | 18 | — | 38 | — | 87 ± 19 · 477 ± 53 |
| Pelvis | face-on + DTL | 21 | **0** | 21 | — | — | — | |
| Thorax | face-on only | 21 | **0** | 14 | — | 18 | — | 68 ± 14 · 727 ± 61 |
| Thorax | face-on + DTL | 21 | **3** ⚠ | 18 | 138 [117, 141] | 16 | 383 | |
| Lead arm | face-on (run result) | 21 | 21 | 0 | 114 [87, 154] | 18 | 684 | 65 ± 8 · 980 ± 68 |
| Club | face-on (run result) | 21 | 19 | 0 | 53 [29, 61] | 10 | 1835 | — · 2254 ± 68 |

⚠ All three pair-placed thorax nodes fail the shipped reversal-spike gate
(`sequence.minAfterReversalMs`, 60 ms). Applying it, the pair places **0 pelvis and 0 thorax** nodes
inside the sequence domain, against face-on's 0 and 0 on these 21 swings (0 and 2 on the full 61).

Thirty-nine of the 42 pair trunk nodes (and all 42 once the spike gate is applied) are bounded at
the **late** edge of the domain: the rate is still climbing where the downswing stops. Under the
pair the bound is `peakNoEarlierThanMs` ≈ 2 ms [1, 3] — "it had not peaked by impact" — where
face-on's bound was 84 ms [57, 89] for the pelvis and 44 ms [40, 72] for the thorax.

Run the same peak finder on a domain extended 150 ms past impact and the peaks are all there:

| Segment | produced | placed | peak AFTER impact, ms | σt ms | peak °/s, ellipse ref | peak °/s, face-on ref | Cheetham |
|---|---|---|---|---|---|---|---|
| Pelvis | 21 | 20 | 39 [28, 44] (20 … 49) | 12.5 | 841 [814, 941] | 639 [598, 691] | 87 ms before · 477 |
| Thorax | 21 | 19 | 29 [24, 36] (20 … 45) | 24.7 | 726 [681, 769] | 671 [620, 678] | 68 ms before · 727 |

Not one of those peaks sits at the extended domain's edge and not one is a reversal spike. So this
is not "the estimator ran out of window": the peaks are interior, and they are 90 to 130 ms later
than the benchmark, on the wrong side of the ball.

By session:

| | pelvis peak after impact, ms | thorax peak after impact, ms | pelvis °/s (face-on ref) | thorax °/s |
|---|---|---|---|---|
| 2026-07-04 (n=12) | 29 [27, 31] | 25 [23, 27] | 652 | 676 |
| 2026-06-11 (n=9) | 43 [40, 46] | 37 [35, 41] | 633 | 650 |

The two rigs differ by ~13 ms in where they put the peak and agree on its size to 3 %.

---

## 2. The geometry, measured

### 2.1 The two views do see one line turning

The decisive test does not go through the ellipse at all. Take face-on's own angle, from its own
reference (`θ_fo = acos(w/W_peak)`, sign from its span maximum), and ask the down-the-line
separation to be `w' = W'·cos(θ_fo − γ)` for **some** scale and **some** inter-view angle. That is
linear in `(W'cos γ, W'sin γ)`, so the R² is a verdict on whether the pairing is possible at all.

| | R² | rms residual, px | γ, ° |
|---|---|---|---|
| Pelvis, 07-04 | 0.950 [0.932, 0.965] | 9.4 | 81.9 [77.8, 83.3] |
| Pelvis, 06-11 | 0.899 [0.841, 0.909] | 12.7 | 75.2 [71.9, 76.7] |
| Thorax, 07-04 | 0.963 [0.954, 0.970] | 11.4 | 82.0 [80.2, 82.3] |
| Thorax, 06-11 | 0.961 [0.949, 0.967] | 12.3 | 84.1 [81.5, 85.0] |

So: yes. The down-the-line hip and shoulder separations are the complement of the face-on spans to
within 9–13 px on spans of 85–145 px, over 21 swings and two different camera rigs.

### 2.2 The effective inter-view angle is 75–84°, not 90°

Three estimators, two of which agree:

| estimator | pelvis 07-04 | pelvis 06-11 | thorax 07-04 | thorax 06-11 |
|---|---|---|---|---|
| consistency fit (§2.1) | 82° | 75° | 82° | 84° |
| from Δt of the square-up instants | 86° | 71° | 73° | 77° |
| general conic cross term `cos γ = −B/2√(AC)` | 54° | 43° | 63° | 67° |

**The conic estimator is not usable and is reported only to say so.** It works on squared
coordinates, where the cloud's departure from an ellipse (§2.4) enters the cross term directly; its
tilt on the normalised cloud reads −53° for the pelvis and −45° for the thorax, which is what
½·atan2(B, A−C) returns whenever A ≈ C whatever B is. The two estimators that use the *signed*
observable land between 71° and 86°; the Δt estimator is the noisier of the two because it inherits
face-on's own scatter in locating the square-up (§2.3), so the consistency fit is the one to take.

Read physically: the down-the-line camera is about 6–15° off the axis the hip and shoulder lines
turn about — consistent with it sitting behind the ball rather than behind the hands. The pelvis
offset is larger on the 06-11 rig (71–75°) than on 07-04 (82–86°). **This is a per-session,
per-rig constant that the pair measures for free**, and it is the one number a `faceOn+dtl` rung
would need to carry.

### 2.3 The square-up instant becomes a measurement

Face-on infers the square-up from the maximum of a flat curve. The pair reads it off a zero
crossing of a steep one. Both, in ms before impact (negative = after impact):

| | face-on span maximum | DTL zero crossing | spread of the face-on estimate within a session |
|---|---|---|---|
| Pelvis 07-04 | −19 [−30, +23] | −23 [−25, −20] | range 87 ms |
| Pelvis 06-11 | +3 [−5, +12] | −39 [−41, −37] | range 52 ms |
| Thorax 07-04 | +1 [−3, +2] | −23 [−26, −22] | range 20 ms |
| Thorax 06-11 | −21 [−29, −20] | −41 [−43, −39] | range 16 ms |

The DTL estimate's within-session interquartile range is 3–5 ms; face-on's is 5–53 ms and its
pelvis estimate on 07-04 scatters over 87 ms across twelve swings of one golfer on one rig. The
median DTL−face-on difference is 36 ms (pelvis) and 21 ms (thorax); almost all of that is face-on's
own scatter, not a bias, and it is exactly the "flat maximum" the design predicted.

The other half of that: **face-on's reference width is systematically too narrow**. The ellipse fit
puts `W` at 1.11× [1.09, 1.12] the face-on downswing maximum for the pelvis and 1.09× for the
thorax on 07-04 (1.02× on 06-11). Face-on never sees these lines square; its own maximum is the
closest it got, and the pair says that was 8–26 % short. Consequently face-on reads less turn than
the pair over the whole downswing — 7° less for the pelvis, and 12° less over the last 100 ms
before impact.

### 2.4 The closure residual: where the ellipse does and does not close

`|(w/W)² + (w'/W')² − 1|`, median [and p90] per phase, over 21 swings:

| phase | pelvis p50 | pelvis p90 | thorax p50 | thorax p90 |
|---|---|---|---|---|
| address (250 ms hold) | 0.049 | 0.110 | 0.107 | 0.185 |
| backswing | 0.075 | 0.148 | 0.110 | 0.172 |
| top ±50 ms | 0.067 | 0.135 | 0.110 | 0.180 |
| **downswing** | **0.020** | 0.076 | **0.055** | 0.148 |
| impact ±50 ms | 0.135 | 0.233 | 0.068 | 0.191 |
| follow-through | 0.321 | 0.401 | 0.118 | 0.220 |

The ellipse closes best exactly where the sequence needs it — 2 % (pelvis) and 5.5 % (thorax) over
the downswing — and fails in the follow-through (32 % for the pelvis), where the arms cross the
body in the down-the-line view and the simulator screen redraws behind the golfer.

**The closure residual is a necessary check, not a sufficient one, and the note says so before
anyone leans on it.** With `x = w/W`, `y = w'/W'`, `dθ = (x·dy − y·dx)/(x²+y²)` while
`d(x²+y²) = 2x²εx + 2y²εy`. The closure sees the SUM of the two relative errors; the angle sees
their DIFFERENCE. A cloud sitting perfectly on the circle can still have both scales wrong in the
same direction, and `θ` would be right anyway. Closure of 0.13 at impact means the two views
disagree about a combined 13 % of radius there; it does not translate into a degree figure.

The pelvis's 0.135 at impact is worth naming: it is the perspective confound of design §12.4 item 2
showing itself. The hips move toward the face-on camera through impact, which magnifies `w` while
the down-the-line view — where that motion is lateral — reads no such change. The cloud leaves the
circle on the inside, and both the scale and the residual say the face-on leg is the one moving.

---

## 3. What looks wrong

### 3.1 The first cut of this tool was confidently wrong, in the design's own words

Design §5.2 reads `w_dtl = w₀ sin θ` and §5.3's sign unfold flips at the span extremum. Taken
literally — an unsigned span, unfolded at its minimum — the tool produced, on every one of the 21
swings, a pelvis "peak" of 806 °/s [778, 903] sitting **exactly 4 ms (one grid step) after the
down-the-line span minimum**, with σ_t of 10–15 ms. That is not a peak. An unsigned span has a V at
square; flipping its sign at the vertex inserts a step of `2·w'_min` into θ at one sample — 13° in
4 ms on swing 0004 — and a 25 ms derivative reads a step as an enormous, sharply curved rate, which
`σ_t = sqrt(2σ_r/|r̈|)` then reports as ±12 ms. This is the §12.1 spike and the §12.4 item 2
bootstrap σ for the third time, arriving by a third route.

The fix is not a filter. The down-the-line observable has to be the **signed horizontal separation**
`(kp_a.x − kp_b.x)·W_frame`, which passes through zero and out the other side with no fold, and
whose zero crossing is the square-up instant. Two consequences worth carrying into the C++:

* the signed separation reaches 2.1 % of `W'` (pelvis) and 1.1 % (thorax) of its scale at the
  crossing; the unsigned 2-D distance floors at 11.6 % and 14.4 % — the vertical offset a
  head-height camera sees between two shoulders never goes away, and it is a 8°–14° floor on the
  angle;
* the face-on leg does **not** need it. Face-on's span never crosses zero in a golf swing and the
  rig's 2-D distance is fine there. Recomputing the face-on leg as `|Δx|` moves the **pelvis** peak
  by 0 ms on 20 of 21 swings (one grid step on the other) and leaves the closure residuals within
  0.001. The **shoulders** are a different matter: `|Δx|` moves the thorax peak on 18 of 21 swings, by
  a median of 4 ms and by 84 ms on one, and it trades closure — slightly better at the top (0.101
  against 0.110) for distinctly worse at impact (0.101 against 0.068). The face-on shoulders carry
  a real vertical separation that the 2-D distance absorbs and `|Δx|` discards. Reported as a
  variant, not adopted; the asymmetry is the point, and it is a thing to settle before coding:
  the two legs do not want the same projection.

### 3.2 The pelvis peak magnitude is not credible; the thorax's is

The pelvis peaks at 841 °/s [814, 941] with the ellipse reference and 639 [598, 691] with face-on's
own. Cheetham's professionals peak the pelvis at 477 ± 53. The lower of the two is 3 σ above that
and the higher is nearly double it. The thorax, by contrast, reads 726 [681, 769] and 671
[620, 678] against a benchmark of 727 ± 61 — inside it on both references.

Two readings, and this pass cannot separate them. Either the pelvis genuinely whips through square
after impact on this golfer (which is consistent with everything else here: he arrives at impact
still ~18° short of square and squares up 20–49 ms later), or the DTL hip keypoints are being
dragged by the trunk and arms through the follow-through and the angle they report is not the
pelvis's. The second is the reason for gate G4 below.

### 3.3 The order the pair produces is fully reversed, and not resolvable

On the extended domain, with the arm and club nodes from the run results:

| order | n |
|---|---|
| leadArm > club > thorax > pelvis | 12 |
| leadArm > club > pelvis > thorax | 4 |
| leadArm > thorax > pelvis (no club node) | 2 |
| leadArm > club > pelvis (thorax σ) | 1 |
| club > leadArm > thorax > pelvis | 1 |
| leadArm > club (no trunk) | 1 |

The lead arm peaks first on 20 of 21; both trunk segments peak after the club node on 17 of 21.
That is the exact reverse of proximal-to-distal, and it is the signature of the thing the §12.4
bounds were already pointing at.

**But the order does not resolve.** `orderResolved` (every adjacent gap > √(σ²+σ²), k = 1) holds on
**2 of 21** on the extended domain, against 18 of 21 for the shipped face-on route, which only ever
had two nodes to order. The reason is specific: the pelvis and thorax peaks sit **4 ms apart**
[4, 8] with σ_t of 12.5 and 24.7 ms. The pair cannot separate the two trunk segments from each
other at all. It can say "both trunk segments peaked after the club"; it cannot say which came
first, and the verdict machinery correctly refuses to.

### 3.4 The down-the-line keypoints, checked rather than assumed

The design warned that the far hip is occluded at address in this view.

| | face-on conf p10 | DTL conf p10, top→impact | DTL conf p10, impact→+100 ms |
|---|---|---|---|
| Hips | 0.75 | 0.75 [0.74, 0.77] | 0.63 [0.60, 0.63] |
| Shoulders | **0.52** | 0.72 [0.70, 0.75] | 0.70 [0.68, 0.71] |

No frame of any of the 21 swings drops a hip or shoulder keypoint below the 0.30 gate: across all
21 swings from address to impact + 150 ms the lowest single-frame confidence is 0.57 for a hip and
0.45 for a shoulder. The down-the-line **shoulders are more confident than the face-on ones**
(0.72 against 0.52), which is not what the design expected: it warned about the far hip at address,
and the far hip holds up. Hip confidence does fall by 0.12 in the 100 ms after impact — which is
exactly where this method now puts the trunk peak, and is a reason to distrust §3.2's magnitude.

Confidence lies, so two behavioural checks as well:

* the down-the-line separation correlates with face-on's own `sqrt(1 − (w/W)²)` prediction at
  r = 0.80 (hips) and 0.89 (shoulders) over takeaway→impact. It is tracking the turn, not a prior;
* at the instant face-on reports its maximum — face-on's own claim of "square" — the down-the-line
  separation is at 32 % of `W'` (hips) and 25 % (shoulders), i.e. 19° and 14° of turn still to come.
  The two views disagree about *when* square is, and §2.3 says which of them is the loose one.

### 3.5 Where the σ comes from, and what it is not

Per-sample σ_θ is propagated from each view's address-hold jitter through the atan2 Jacobian, which
— unlike the face-on acos — has no singularity: `σ_θ = √((y·σ_w/W)² + (x·σ_w'/W')²)/(x²+y²)`, and
it reads 2.5° at address, 2.3° at the top and 2.5° at impact for the pelvis. **There is no blind
band in the pair, in principle or in the numbers.** That is the design's claim and it holds.

The thorax is worse: 6.5° at address, 2.4° at the top, 5.6° at impact, because the down-the-line
shoulder separation's address-window standard deviation is 14.8 px against the face-on shoulders'
2.3 px. Inspecting it, the separation drifts 30 → 84 px over the 250 ms address window: the golfer
is not still there, so this σ_w is an upper bound that has real motion baked in. It is the direct
cause of the thorax's 24.7 ms σ_t against the pelvis's 12.5 ms, and it is fixable by measuring the
jitter on a genuinely still window rather than the phase ladder's address anchor.

σ_t is still curvature-derived and still says nothing about truth. It is fit stability with a
better-conditioned angle behind it. §9's truth capture remains the only arbiter.

### 3.6 The peak time does not care about the reference widths — the magnitude does

Moving `W` and `W'` by ±3 % independently (four combinations per swing per segment):

| | peak time shift, max over the four | peak magnitude shift |
|---|---|---|
| Pelvis | 0 ms on 20 of 21, one grid step (4 ms) on 1 | 5.7 % |
| Thorax | 0 ms on 21 of 21 | 5.9 % |

At ±10 % the time still moves by at most one 4 ms grid step. And the whole-reference swap — from
the uncalibrated ellipse fit to face-on's own `W_peak`, an 11 % change in `W` — leaves the pelvis
peak time unmoved on 17 of 21 swings and the thorax's on 21 of 21, never by more than one grid
step, while moving the pelvis magnitude from 841 to 639 °/s.

This is the algebra behind it, and it is the strongest argument for the rung: an error in the two
scales reaches `θ` only through their DIFFERENCE, as `x·y·(εW − εW')/(x²+y²)` — a smooth, slowly
varying term that shifts a rate curve's level without moving its argmax. A common-mode error does
not reach the angle at all. **The pair route's timing claim survives an
uncalibrated reference; its magnitude claim does not.** Since a sequence node is a time, that is the
right way round.

---

## 4. Verdict

**Not yet. The geometry is sound and the arithmetic is now right; what it measures is a swing whose
trunk peaks outside the metric's domain, on one golfer, with no truth attached.**

What was established:

1. the two views are consistent with one rigid line turning (R² 0.90–0.96, rms 9–13 px);
2. the pair fixes both pixel scales with no calibration and measures the effective inter-view angle
   (75–84°, per rig) as a by-product;
3. there is no blind band — σ_θ is 2.3–2.5° for the pelvis everywhere, including at square;
4. the square-up instant becomes a 3–5 ms measurement instead of a 20–90 ms inference;
5. the peak time is invariant to ±10 % reference error;
6. face-on's reference width is 8–26 % too narrow for the hips (0–15 % for the shoulders), and
   face-on under-reads the pelvis's turn by 7° over the downswing and 12° in the last 100 ms.

What stops it becoming the `faceOn+dtl` rung today:

* it places **no more nodes in the sequence domain than face-on does** (0 and 0), because the trunk
  peaks 20–49 ms after impact on all 21 swings;
* the pelvis magnitude, 639–841 °/s, is not credible against a 477 ± 53 benchmark, and the hip
  confidence dips exactly where the peak is claimed;
* the two trunk nodes land 4 ms apart and cannot be ordered against each other;
* n = 21, one golfer, two rigs, both his; no truth.

If the rung is built, these are the gates, stated before more data:

* **G1 — signed observable.** The down-the-line leg is the signed horizontal separation. Any
  implementation that takes an unsigned span and unfolds at its minimum reproduces §3.1's 800 °/s
  artefact, and `segment_rates_test` should have a case that fails if someone does.
* **G2 — closure as a producer gate.** Emit nothing where the closure residual over the domain
  exceeds ~0.15 p90. That rejects the follow-through outright and would have caught the 06-11
  pelvis swings with R² 0.84.
* **G3 — the inter-view angle is carried, not assumed.** Fit γ per swing per segment from the
  consistency form (§2.1) and refuse a pairing whose γ sits outside, say, 60–90°, or whose R² is
  below 0.85. A γ near 90 is the assumption; 75° is the measurement, and the difference is 15° of
  angle at the top.
* **G4 — the trunk magnitude is not published until an IMU has seen it.** §9 stage 2, one unit on
  the sacrum and one on the sternum, is the arbiter for the 639–841 °/s reading and for whether the
  post-impact peak is the golfer or the keypoints. Until then a pair node is a *time* with a σ and
  the level series stays deleted, exactly as §2 requires.
* **G5 — the domain question is a design decision, not a fix.** A sequence defined on
  [transition, impact] will report this golfer's trunk as "did not peak in the downswing". That is
  the truthful output and the bound the pair gives (`peakNoEarlierThanMs` ≈ 2 ms) is far tighter and
  better founded than face-on's 84 ms. Whether the metric should also look past impact is a
  question for the chart, and it should be asked before, not after, someone reads 12 out-of-sequence
  swings as a fault.

One thing to check that this pass could not: the down-the-line pose is asserted to be on the
face-on clock. Every result here that depends on when something happened would move with a stream
timestamp offset, and a 30 ms offset is the size of the whole finding. The face-on and DTL
square-up estimates agree to 2 ms on some swings and 40 ms on others, and §2.3 attributes all of
that to face-on's flat maximum — which is the right reading but is not a *proof* that the clocks
are aligned. A hard sync check against a common event (the ball leaving, the club crossing a known
plane in both views) is cheap and should come before the rung is built.
