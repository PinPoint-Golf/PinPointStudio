# Proposal: Coupling the Kinematic Sequence to the Swing Plane

**A plane-referenced reformulation of segmental sequencing for PinPoint Studio**

*Draft 1 — July 2026*

---

## 0. The one-paragraph version

Today PinPoint measures two things separately: the **kinematic sequence** (when each body segment reaches peak rotation speed) and the **swing plane** (the tilted disc the club travels around). This proposal argues they are not really two things. Every segment's rotation can be split into a part that spins the club *around* the plane and a part that *tilts the plane itself*. Splitting them gives four new measurements that the current kinematic sequence throws away — and one of them, the **alignment index**, turns out to explain a coaching phenomenon that neither metric can explain alone: why a golfer with a textbook sequence graph can still hit the ball sideways.

The mathematics is a single dot product. The insight is in what the dot product reveals.

---

## 1. Glossary — everything decoded before we start

The literature in this area is thick with acronyms and terms of art. Here is every one used in this document, in plain English. Skip ahead if you already know them; come back if a section stops making sense.

| Term | What it actually means |
|---|---|
| **Kinematic sequence (KS)** | The order and timing in which body segments reach their maximum rotation speed during the downswing. The "textbook" order is pelvis → thorax (ribcage) → lead arm → club. Usually shown as four coloured humps on a speed-vs-time graph. |
| **Proximal-to-distal sequencing (PDS)** | The same idea, stated as a principle: big segments near the body's centre (proximal) go first, small segments at the ends (distal) go last. Same thing coaches call the "kinetic chain" or the "whip effect". |
| **Angular velocity (ω)** | How fast something is rotating. Crucially, it is a **vector**, not just a number — it has a direction as well as a magnitude. See §3. Measured in degrees per second (°/s) or radians per second (rad/s). |
| **Swing plane** | The flat, tilted disc that best approximates the path of the clubhead (or club shaft, or hands, depending who is defining it). |
| **Functional swing plane (FSP)** | Kwon's specific version: the plane fitted to the clubhead trajectory over a defined window around impact. The most standardised definition in the literature. |
| **Motion plane (MP)** | The equivalent plane fitted to a body point rather than the clubhead — e.g. the plane the lead wrist travels in. |
| **Plane normal (n̂)** | The direction perpendicular to a plane, pointing straight out of it like a flagpole from a hillside. The "hat" means it's a unit vector — length exactly 1, so it carries direction only, no magnitude. This is the single most important object in this whole proposal. |
| **Inclination / slope angle** | How steeply the plane is tilted relative to the ground. |
| **Direction angle** | Which way the plane is aimed relative to the target line. |
| **PCA / SVD / OLS** | Principal Component Analysis, Singular Value Decomposition, Orthogonal Least Squares. Three mathematically related ways of finding the best-fit plane through a cloud of 3D points. For our purposes they give effectively the same answer; SVD is the most numerically stable and is what I'd use. |
| **ISA (instantaneous screw axis)** | The single axis about which a body segment is *actually* rotating at a given instant, allowed to move and re-orient frame by frame. A more honest description than assuming a segment rotates about a fixed anatomical axis. |
| **CHS** | Clubhead speed. |
| **Hub / hub path** | The point the club swings around — roughly the mid-hands. Its trajectory through space is the "hub path". |
| **RMS** | Root mean square. A way of averaging errors that punishes big ones more than small ones. |
| **SPM1D** | Statistical Parametric Mapping for one-dimensional data. A method for statistically comparing entire *curves* against each other rather than comparing single points on them. |
| **fPCA** | Functional Principal Component Analysis. A method that compresses a whole curve down to two or three numbers that capture most of its shape. |
| **Dot product (a·b)** | An operation on two vectors that returns a single number: how much of one vector points along the other. See §3.2 — this is the mathematical engine of the entire proposal. |

---

## 2. The problem with the current approach

### 2.1 What we do now

PinPoint currently produces a kinematic sequence chart: four curves, one per segment, each showing rotation speed rising and falling through the downswing. The coach reads off two things: the *order* of the peaks and the *magnitudes* of the peaks. A good sequence peaks in order, and each successive peak is higher than the last.

Separately, PinPoint produces swing plane information — currently as static angles, and (per the previous proposal) shortly as a time series of inclination and direction.

These are presented as unrelated diagnostics on different screens.

### 2.2 Why that is a problem

Here is the finding that should change how we think about this. Marsan, Bourgain, Thoreux et al. (2019) tested **seven different ways** of computing the kinematic sequence from the same motion capture data on the same thirteen golfers. The seven methods differed only in which piece of the angular velocity vector they used: its total magnitude, or its component perpendicular to the sagittal plane, or the frontal plane, or the transverse plane, or *the swing plane*, or along the segment's own long axis, or a mixture.

They found that the choice of method produced **almost as many different kinematic sequences as there were methods**, for every single golfer. Change the projection, change the answer about whether the golfer sequences correctly.

Sit with that for a moment. It means the kinematic sequence is not an objective property of the swing. It is a property of the swing *plus* an arbitrary choice of reference direction that most software makes silently. And one of the candidate reference directions — the one that arguably has the strongest physical claim, because the club is genuinely trying to travel in a plane — is the swing plane itself.

**So the kinematic sequence already contains a hidden swing-plane assumption. We are not proposing to link two independent measurements. We are proposing to make an existing dependency explicit and then exploit it.**

An analogy. Suppose you are timing four runners on a track but you only have a stopwatch that measures how fast they are approaching *you*, standing at some arbitrary point on the infield. Move to a different spot on the infield and you get different times, and possibly a different finishing order — not because the runners changed, but because your measurement axis did. The current kinematic sequence has exactly this problem. The fix is not a better stopwatch. The fix is to stand somewhere principled.

### 2.3 What the swing plane can't tell us either

The plane time-series has the mirror-image gap. Coleman and Rankin (2005) showed the plane is not static: the lead-arm plane steepened from about 133° to about 102° during the downswing, and rotated from about −9° to +5° relative to the target line. That is roughly 30° of tilt change and 14° of aim change in about a quarter of a second.

Thirty degrees is a lot. Something is driving it. But the plane time-series alone tells you *that* the plane moved, never *what moved it*. It is a symptom with no aetiology. For a coaching product this is close to useless — you can tell the golfer their plane steepened but not what to do about it.

The two metrics have complementary blind spots. That is usually a sign they want to be one metric.

---

## 3. The central idea

### 3.1 Rotation is a direction, not just a speed

This is the conceptual hurdle, and everything else follows from it, so I'll labour it.

When we say "the pelvis is rotating at 480 degrees per second", we've said only half of what's true. Rotation happens *about an axis*, and that axis points somewhere. The full description is a vector: point your right thumb along the axis of rotation, and your curled fingers show which way the thing is spinning. The length of the vector is the speed; the direction of the vector is the axle.

Picture a bicycle wheel. The angular velocity vector doesn't lie in the wheel — it sticks out of the hub, along the spindle, perpendicular to the wheel. Spin the wheel faster and the vector gets longer. Tilt the wheel and the vector tilts with it.

Current kinematic sequence software mostly reports the **length** of that vector and discards its **direction**. That's the discarded information we want back.

### 3.2 The plane normal is the ideal axle

Now think about the swing plane. It is a tilted disc. Its normal vector **n̂** sticks straight out of it — the spindle of the disc.

If the golfer were a perfect machine, every segment would be rotating about exactly this axle. Pelvis, ribcage, arm, club: all spinning like coaxial gears on a single shaft. Everything each segment does would go directly into moving the club around the disc. Nothing would be wasted.

Real golfers aren't machines. Each segment's angular velocity vector points somewhere *near* the plane normal but not exactly along it. And the mismatch is the thing nobody is measuring.

### 3.3 Splitting the vector in two

Any vector can be split into a piece pointing along a chosen direction and a piece perpendicular to it. Think of a torch shining at an angle onto a stick: the shadow the stick casts along the ground is one piece, and the height it stands off the ground is the other. Together they reconstruct the stick.

For each segment's angular velocity **ω** and the plane normal **n̂**:

```
ω∥ = (ω · n̂) n̂        ← the part spinning about the plane's axle
ω⊥ = ω − ω∥            ← everything left over
```

The dot product `ω · n̂` is just "how much of ω points along n̂" — it returns a single number, the length of the shadow.

These two pieces have completely different physical jobs:

- **ω∥ — in-plane spin.** This is the component that carries the club around the disc. This is the part that becomes clubhead speed. This is what you actually want to sequence.

- **ω⊥ — off-plane spin.** This is the component that *torques the plane*. It doesn't drive the club around the circle; it tries to tip the circle over. This is the part that becomes plane instability.

Both were previously buried inside a single magnitude on the kinematic sequence chart, indistinguishable from each other.

### 3.4 The alignment index

Define, for each segment, at each frame:

```
a(t) = (ω · n̂) / |ω|
```

This is the cosine of the angle between the segment's rotation axis and the plane's axle. It ranges from +1 (perfectly aligned, everything going into the swing) through 0 (rotating entirely off-axis, contributing nothing to club speed) to −1 (rotating backwards relative to the plane).

It is dimensionless — a pure efficiency ratio, independent of how fast the segment is going. A slow golfer and a fast golfer can have identical alignment; a Tour player and a beginner can have identical peak speeds with wildly different alignment.

**This is the number the kinematic sequence has never shown.**

---

## 4. The gold: why misalignment ruins your accuracy but not your distance

This is the part I think is genuinely novel and genuinely useful, and it falls straight out of trigonometry.

Suppose a segment's rotation axis is off the plane normal by an angle θ. Then:

- The fraction going into **club speed** is cos θ
- The fraction going into **tipping the plane** is sin θ

Now look at how those two behave for small errors:

| Misalignment θ | Speed retained (cos θ) | Speed lost | Off-plane leak (sin θ) | Leak-to-loss ratio |
|---|---|---|---|---|
| 5° | 99.6% | 0.4% | 8.7% | 23 : 1 |
| 10° | 98.5% | 1.5% | 17.4% | 11.6 : 1 |
| 15° | 96.6% | 3.4% | 25.9% | 7.6 : 1 |
| 20° | 94.0% | 6.0% | 34.2% | 5.7 : 1 |
| 25° | 90.6% | 9.4% | 42.3% | 4.5 : 1 |

Read the last column. **At small misalignments, the off-plane leak is an order of magnitude larger than the speed penalty.**

The reason is that cosine is *flat* near zero — its derivative is zero there, so small errors cost almost nothing. Sine is *steep* near zero — its derivative is 1, so small errors produce proportionally large effects. Mathematically, for small θ the ratio is approximately 2/θ (in radians).

This gives us a testable, mechanistic prediction:

> **Misalignment is a first-order effect on plane stability and only a second-order effect on clubhead speed.**

Which is to say: a golfer whose thorax is rotating 15° off the swing plane loses about 3% of that segment's contribution to speed — barely detectable, well inside shot-to-shot noise — but dumps a full quarter of that segment's rotation into tipping the plane.

**This explains something the current metrics cannot.** It explains why a golfer can show a textbook kinematic sequence — correct order, rising peaks, beautiful graph — hit the ball a perfectly respectable distance, and still spray it. Their sequence is fine. Their *alignment* is not. And the current chart has no column for that.

It also predicts the reverse, which is a good falsification test: if we're right, alignment scores should correlate strongly with shot dispersion and weakly with carry distance. If alignment turns out to correlate mainly with distance, the model is wrong and I'd want to know that early.

### 4.1 A worked example

Marsan et al. report these typical peak segment speeds for their thirteen golfers:

| Segment | Peak |ω| |
|---|---|
| Pelvis | 480 ± 82 °/s |
| Thorax | 605 ± 87 °/s |
| Lead arm | 1310 ± 236 °/s |
| Lead forearm | 1490 ± 203 °/s |
| Lead hand | 1650 ± 211 °/s |

Take two hypothetical golfers, both with a textbook sequence and identical peak magnitudes. Golfer A's thorax rotates 5° off the plane normal; Golfer B's rotates 20° off.

- **Golfer A:** in-plane component = 605 × cos 5° = **603 °/s**. Off-plane component = 605 × sin 5° = **53 °/s**.
- **Golfer B:** in-plane component = 605 × cos 20° = **569 °/s**. Off-plane component = 605 × sin 20° = **207 °/s**.

Their kinematic sequence charts are indistinguishable — both show a thorax peak at 605 °/s, in the right place, in the right order. Their in-plane contributions differ by 6%, which is real but modest. Their off-plane contributions differ by a factor of **four**.

Golfer B is fighting his own thorax for control of the plane. Nothing on the current chart says so.

### 4.2 A sanity check on the scale

Is 207 °/s of off-plane rotation actually enough to move the plane? Let's check against Coleman and Rankin's measured plane motion: roughly 30° of inclination change over a downswing of about 250 ms, i.e. an average plane reorientation rate of order **120 °/s**.

So a single segment misaligned by 20° generates an off-plane component of the same order of magnitude as — indeed larger than — the total observed plane motion. This tells us two things:

1. The numbers are in the right ballpark. The mechanism is plausible.
2. The observed plane motion must be the **residual of largely-cancelling off-plane components**. Several segments are each pushing the plane around, mostly in opposition, and what we see is what doesn't cancel.

Point 2 is important and slightly humbling. It means the relationship between individual segment misalignment and net plane motion is not a simple sum — it's a partial cancellation, and small changes in one segment can produce disproportionate changes in the residual. That argues for measuring alignment per segment and *reporting it per segment*, rather than rolling it into a single "efficiency score" that hides the cancellation.

---

## 5. Closing the loop: what moved the plane?

The alignment index tells you a segment is leaking. The next question a coach asks is: *is that leak what's moving my plane?* We can answer this.

### 5.1 The plane has its own angular velocity

The plane normal **n̂**(t) is a unit vector that swivels around over time. A swivelling unit vector has its own angular velocity:

```
Ω = n̂ × (dn̂/dt)
```

(That's a cross product; it returns a vector perpendicular to both inputs, which is exactly the axis the normal is swivelling about.)

**Ω** is the plane's own rotation rate — how fast, and about what axis, the disc is tipping and turning. If the plane is stable, |Ω| is near zero. If the golfer is steepening hard through the downswing, |Ω| spikes.

### 5.2 Cross-correlation: who leads whom

Now we have, on the same time axis:

- Four off-plane leak signals, one per segment: |ω⊥| for pelvis, thorax, arm, club
- One plane-motion signal: **Ω**

**Cross-correlation** asks: if I slide one curve forwards or backwards in time, at what offset does it best match the other? The analogy is two transparencies with wiggly lines on them — you slide one over the other until the wiggles line up, and the distance you slid is the lag.

Do this for each segment against **Ω**. The segment whose leak signal *leads* the plane motion by the shortest positive lag, with the highest correlation, is your prime suspect. That gives you an attribution, not just a symptom:

> *"Your plane steepened 18° through the downswing. Your thorax off-plane rotation peaks 40 ms before the steepening and tracks it at r = 0.81. Your pelvis shows no such relationship."*

That is a coaching instruction. The current chart produces nothing of the kind.

### 5.3 A necessary caution about "cause"

Cross-correlation with a lead-lag relationship is *suggestive* of causation but is not proof of it, and I would resist the temptation to reach for Granger causality or transfer entropy here even though they're sitting right there.

The reason is mechanical coupling. The pelvis, thorax, arm and club are physically bolted together through the kinetic chain. When one moves, the others are obliged to move. Temporal precedence in a rigidly coupled system tells you about the *propagation order of a disturbance*, not about which link is the origin.

The analogy: two dancers in a close waltz. One consistently leads the other by a fraction of a beat. Cross-correlation will show this beautifully. It does not establish that the leading dancer is choosing the steps — they may both be following the music, or the follower may be steering with subtle pressure while appearing to lag.

The only rigorous route to causal claims is forward-dynamics simulation — build a model, change one input, observe the output. MacKenzie and Sprigings did exactly this and validated their model's lead-arm plane against Coleman and Rankin's experimental data. That's a substantial research programme and well beyond a v1 feature.

**Recommendation:** present the output as *mechanistic decomposition plus temporal association*, in language that doesn't overclaim. "Your thorax off-plane rotation is closely associated with your plane steepening" is honest. "Your thorax is causing your plane to steepen" is not, and a good coach will eventually catch us out on it.

---

## 6. The club speed half of the question

You also asked how the kinematic sequence feeds clubhead speed. There is a clean decomposition for this too.

The clubhead's velocity is the velocity of the hub (roughly mid-hands) plus the velocity contributed by the club rotating about that hub:

```
v_clubhead = v_hub + (ω_club × r)
```

where **r** is the vector from the hub to the clubhead.

This splits clubhead speed into two attributable sources:

1. **Hub translation** — the hands physically moving through space. Generated primarily by the arms and torso.
2. **Club rotation about the hub** — the club whipping around the hands. Generated primarily by the wrists releasing.

At impact these are wildly unequal. Rough figures for a driver: the hub is travelling at perhaps 4–6 m/s while the clubhead is at 50 m/s, so the great majority of clubhead speed comes from the rotational term. *These numbers should be calibrated against our own capture data rather than taken from me — treat them as an order-of-magnitude sanity check only.*

The value of the split is that it maps directly onto Nesbit and Serrano's linear-versus-angular work decomposition, which is well established in the literature, and it is immediately coachable: "you're an arms-driven speed generator" versus "you're a release-driven speed generator" is a real distinction that real coaches make.

### 6.1 The supporting evidence

The number that justifies making the plane the reference frame at all: in the *International Journal of Golf Science* study of amateur drivers, the angular impulse due to total torque applied **in the plane of the swing** during the downswing predicted **91% of the variability in clubhead speed**.

Ninety-one percent, from a single in-plane quantity. That is an unusually strong result in biomechanics, and it says plainly that the plane-referenced frame is not an aesthetic preference — it is where the signal lives.

---

## 7. What we'd actually ship: four new metrics

Recapping what falls out of the above, in the order I'd build them:

| # | Metric | Definition | What it tells the golfer |
|---|---|---|---|
| 1 | **In-plane kinematic sequence** | \|ω∥\| per segment, over time | The speed-generating sequence, with off-plane noise stripped out. This is the honest version of the chart we already show. |
| 2 | **Alignment index** | a(t) = (ω·n̂)/\|ω\| per segment | How much of each segment's effort is going into the swing versus fighting the plane. **The new one.** |
| 3 | **Plane reorientation rate** | \|Ω\| where Ω = n̂ × dn̂/dt | How much the plane is moving, moment by moment. |
| 4 | **Attribution** | lagged cross-correlation of \|ω⊥\| against Ω | Which segment's leak is associated with the plane movement, and when. |

Plus the clubhead speed decomposition (§6) as a separate, simpler deliverable.

### 7.1 How I'd visualise it

The elegant presentation is a **single chart, not two**. Keep the familiar four-hump kinematic sequence, but shade or colour each curve by its alignment index at that instant — green where the segment is on-axis, warming towards red where it's leaking. Underneath, on the same time axis, plot plane reorientation rate.

A coach then reads, in one glance: *the sequence is in order, but the thorax hump goes orange right where the plane rate spikes.* That's the whole diagnosis in one picture, and it requires no new mental model — it's the chart they already know with the missing dimension painted onto it.

---

## 8. The statistical layer

Two questions we'll need to answer that require more than a chart: *is this golfer's pattern unusual?* and *does this pattern predict outcomes?*

### 8.1 Don't reduce to peaks

The instinct is to summarise each curve by its peak value and peak timing, then run ordinary statistics on those numbers. **Resist this.** Reducing a curve to two numbers throws away exactly the coupling structure we've just gone to the trouble of exposing. The interesting question is not "when did the thorax peak" but "what shape did the thorax alignment curve have, and did that shape co-occur with the plane rate curve".

### 8.2 SPM1D for curve comparison

**Statistical Parametric Mapping for one-dimensional data (SPM1D)** is the established tool for comparing whole curves. Conceptually: run a statistical test at every single time point, then apply a correction for the fact that you've run hundreds of tests on correlated data. The output is not "p = 0.03" but a shaded region on the time axis saying *"this golfer differs significantly from the reference cohort between 45% and 62% of the downswing."*

That is a far better fit for coaching than a single p-value, because it localises the problem in time. Free implementations exist in Python (`spm1d`) and MATLAB.

### 8.3 fPCA for compression

**Functional Principal Component Analysis (fPCA)** takes a family of curves and finds the two or three "shape modes" that explain most of the variation between them. Each golfer's curve then becomes two or three scores, which can be fed into ordinary regression against clubhead speed or dispersion.

The analogy: rather than describing a face pixel by pixel, you describe it as "70% along the round-to-narrow axis, 20% along the young-to-old axis." Most of the information, a tiny fraction of the numbers.

This is how I'd build the eventual predictive model: fPCA scores of the alignment curves as predictors, shot outcome as response.

### 8.4 Precedent for a nuanced verdict

Lee, Ehrlich and Cain (2026, *International Journal of Sports Science & Coaching*) studied fourteen elite golfers — PGA Tour, Korn Ferry, NCAA and PGA of America — regressing sequencing quality across backswing, transition and downswing onto clubhead speed. Their most useful finding for us is that **slight variations from the "optimal" sequence yielded comparable clubhead speed**.

This is a warning against a binary pass/fail verdict in the UI. There is not one correct sequence; there is a family of workable ones. Whatever we ship should present a *distribution* or a *band*, not a tick or a cross. Coaches who have been burned by dogmatic sequence software will notice and appreciate the difference.

---

## 9. Risks and failure modes

I want these on the record before any code gets written.

### 9.1 The alignment index is a ratio of two noisy quantities

a(t) = (ω·n̂)/|ω| divides one estimated vector by the magnitude of another. When |ω| is small, the denominator approaches zero and the index becomes wildly unstable — a tiny absolute error produces a huge swing in the ratio.

Where is |ω| small? At transition, the top of the backswing, exactly where segments are reversing direction. Which is precisely where coaches most want to look.

**Mitigation:** gate a(t) on a magnitude threshold. Below the threshold, grey the curve out or render it as a dashed uncertainty band rather than a solid line. Never show a confident-looking alignment value derived from near-zero rotation. Choose the threshold empirically from our own noise floor, not from a round number.

### 9.2 Pose-derived pelvis and thorax rotation will be the weak link

ViTPose keypoints give us joint positions; segment angular velocity has to be derived from them, which means differentiating noisy position estimates and then fitting a rotation. Pelvis and thorax are the worst cases because they're defined by relatively few keypoints spanning a short baseline — small keypoint errors produce large angular errors.

The IMU-instrumented segments will be far better. This creates an awkward asymmetry: the segments we can measure most reliably are not necessarily the ones the coach most wants to discuss.

**Mitigation:** carry per-segment confidence through to the UI, and be honest about it — the same confidence-tier approach already planned for plane inclination (robust) versus off-plane deviation (fragile). Consider whether pelvis/thorax alignment should ship at all in v1 or wait for better pose smoothing.

### 9.3 The plane normal inherits our shaft-detection error

Everything here is referenced to n̂. Our published shaft angle accuracy is around 0.49° RMSE face-on and 2.44° down-the-line, so the normal carries at least that. A 2° error in n̂ propagates directly into every alignment index — and given §4's finding that the interesting effects live at 5–20° of misalignment, a 2° systematic error is not negligible relative to the signal.

**Mitigation:** validate the whole pipeline on synthetic swings first. Generate a trajectory with a *known* plane and *known* segment axes, run it through the full chain, and measure what comes out. If we can't recover a synthetic 10° misalignment to within a couple of degrees, we shouldn't ship the metric.

### 9.4 We will disagree with every other product on the market

TPI, GEARS, K-Vest, Swing Catalyst and the rest compute the kinematic sequence on resultant magnitude or on the segment's longitudinal component. If we default to a plane-normal basis, our numbers will not match anything our users have seen elsewhere — and per Marsan et al., they *will* differ, potentially in the reported order and not just the magnitudes.

**Mitigation, and I'd treat this as non-negotiable:** compute both. Default the chart to the conventional basis so our numbers reconcile with the rest of the industry. Offer the plane-referenced view as a clearly-labelled additional analysis with a short explanation of why it differs. If we don't do this, the first support ticket is "your kinematic sequence is wrong", and we will spend more time defending the maths than selling the insight.

### 9.5 The novelty risk

Marsan et al. found the swing-plane projection method was *not* the best at recovering the textbook pelvis→thorax→arm→forearm→hand order — their mixed method (longitudinal for pelvis/thorax, plane-normal for arm/forearm/hand) did better. This is a genuine caution: the plane-normal basis may not reproduce the sequence coaches expect, and we should not assume it will.

But note that this cuts both ways. Marsan et al. were asking "which projection best recovers the conventional sequence?" — treating the conventional sequence as ground truth. We are asking a different question: "which projection best predicts *outcomes*?" Those may well have different answers, and the second is the one that matters commercially. Worth being explicit that we're departing from their framing deliberately, not accidentally.

---

## 10. Suggested phasing

**Phase 1 — Foundations.** Swing plane as a time series (previous proposal). Segment angular velocity vectors from existing pose and IMU pipelines. No new UI. Validate both against synthetic data.

**Phase 2 — Decomposition.** Compute ω∥, ω⊥ and a(t) per segment. Compute Ω. Log everything, expose nothing. Build up a corpus across a range of players.

**Phase 3 — Validation.** Test the §4 prediction on real data: does alignment correlate with dispersion more strongly than with distance? This is the go/no-go gate. If the prediction fails, stop and rethink before building UI.

**Phase 4 — Visualisation.** The alignment-shaded kinematic sequence chart (§7.1), with confidence gating.

**Phase 5 — Attribution and statistics.** Cross-correlation attribution, SPM1D against a reference cohort, fPCA-based outcome modelling.

The critical discipline is Phase 3. It would be easy to skip straight to the pretty chart, because the chart is the fun bit. But the entire value proposition rests on alignment actually predicting something, and we should find that out before we've built a UI around it.

---

## 11. Open questions

1. **Which plane?** The clubhead plane (Kwon's FSP), the shaft plane, or the hand/hub plane? They are measurably different, and the alignment index will differ depending on which n̂ we use. My instinct is the clubhead plane for coaching interpretability, but this deserves an empirical test.

2. **Windowed or instantaneous n̂?** A sliding-window plane fit is stable but lags; a truly instantaneous plane (from three consecutive points) is responsive but noisy. Given that Ω involves differentiating n̂, this choice matters a great deal.

3. **Which segments?** Four (pelvis, thorax, arm, club) matches convention. Marsan et al. use five (splitting arm into arm, forearm, hand). More segments means finer attribution but more noise and a busier chart.

4. **Backswing too?** Everything above assumes the downswing. The plane in the backswing is measurably less well-defined — plane fitting quality is known to vary across swing phases. Probably a v2 question.

5. **What's the reference cohort?** SPM1D needs something to compare against. Do we build our own, licence one, or ship without normative comparison initially?

---

## 12. Key references

| Reference | Why it matters |
|---|---|
| Marsan T, Bourgain M, Thoreux P, et al. (2019). *Acta of Bioengineering and Biomechanics* 21(2):115–120. | **The keystone.** Shows the kinematic sequence depends critically on which angular velocity component is used, including the swing-plane projection. This is the paper that makes the whole proposal necessary. |
| Coleman SGS & Rankin AJ (2005). *Journal of Sports Sciences* 23(3):227–234. | First systematic quantification of instantaneous plane inclination and direction through the downswing. Source of the 133°→102° steepening figure. |
| Kwon YH, Como CS, Singhal K, Lee S, Han KH (2012). *Sports Biomechanics* 11(2):127–148. | Defines the Functional Swing Plane and the standard slope/direction angle conventions. |
| Cheng KJ, Jump IP, Klubertanz MR, Oliver GD (2026). *Applied Sciences* 16(5):2327. | Open-access review of plane-fitting methods (PCA, SVD, OLS, FSP) and planarity metrics. Best single entry point. |
| Lee J, Ehrlich J, Cain C (2026). *Int J Sports Science & Coaching*, DOI 10.1177/17479541261429479. | Kinematic sequence → clubhead speed in fourteen elite golfers. Source of the "slight variations give comparable results" caution. |
| Nesbit SM (2005). *Journal of Sports Science and Medicine* 4:499–519; Nesbit SM & Serrano M (2005). | Work and power decomposition of the swing. Basis for the linear/angular attribution in §6. |
| Nesbit SM & McGinnis R. Hub path analysis (PMC3761476). | Hub path and its role in golfer/club kinetic transfer. |
| *International Journal of Golf Science* — "How Amateur Golfers Deliver Energy to the Driver". | Source of the finding that in-plane angular impulse predicts 91% of clubhead speed variance. |
| Sports Engineering (2011), DOI 10.1007/s12283-010-0059-7, Part 2. | Kinematic sequence relative to each segment's instantaneous screw axis. Conceptual precursor to the alignment index. |
| Tinmark F, Hellström J, Halvorsen K, Thorstensson A (2010). | Elite golfers' kinematic sequence, full and partial swings. Good normative magnitudes. |
| MacKenzie SJ & Sprigings EJ (2009). *Sports Biomechanics*. | Forward-dynamics model whose lead-arm plane validates against Coleman & Rankin. The route to genuine causal claims. |

---

## 13. Summary

The kinematic sequence and the swing plane are not two measurements. Every segment's rotation splits, via a single dot product against the plane normal, into a part that generates clubhead speed and a part that destabilises the plane. Current software reports the sum of the two and can't tell them apart.

Separating them yields an **alignment index** per segment, and the trigonometry of that index predicts something useful and non-obvious: small misalignments cost almost nothing in distance but leak heavily into plane instability, by roughly an order of magnitude at typical error angles. That is a candidate explanation for the golfer with a perfect sequence chart who still can't find a fairway.

The risks are real — noise in the ratio at low rotation speeds, weak pose-derived pelvis and thorax data, and inevitable divergence from every competitor's numbers. All are manageable with confidence gating, synthetic validation, and shipping both bases side by side.

The thing to do before building any of it is Phase 3: check whether alignment actually predicts dispersion. If it does, we have something no other product has. If it doesn't, we've learned that cheaply.
