# Proposal: Coupling the Kinematic Sequence to the Swing Plane

**A plane-referenced reformulation of segmental sequencing, and a route from body kinematics to club path**

*Draft 2 — July 2026*

---

## 0. The one-page version

Today PinPoint measures two things separately: the **kinematic sequence** (when each body segment reaches peak rotation speed) and the **swing plane** (the tilted disc the club travels around). This proposal argues they are not really two things, and that joining them yields something neither can produce alone — a diagnostic account of **club path**.

The argument runs in three steps.

**Step one — the sequence and the plane are already entangled.** Every segment's rotation splits, via a single dot product against the plane normal, into a part that spins the club *around* the plane and a part that *tilts the plane itself*. Current software reports the sum of the two and cannot tell them apart. Separating them gives an **alignment index** per segment, and the trigonometry of that index predicts something non-obvious: small misalignments cost almost nothing in distance but leak heavily into plane instability. That's a candidate explanation for the golfer with a perfect sequence chart who still can't find a fairway.

**Step two — this leads directly to club path, and path is the goal.** Path at impact is the tangent to the clubhead's arc at the ball. It depends on the plane's orientation, on the shape of the arc, and on where the ball sits along that arc. A launch monitor sees only the resulting tangent and cannot separate those causes. **We can.** Two golfers with identical out-to-in path can have entirely different problems — one aims the plane left, the other catches the ball too late in the arc — and the fixes are opposite. Decomposing path into its causes, and tracing each cause back to the kinematic sequence, is the primary deliverable of this proposal.

**Step three — face-to-path comes along for the ride, but only as an inference.** The clubface is a further degree of freedom that neither the plane nor the sequence currently sees. It is driven distally and moves fast enough that predicting it from body kinematics is, I think, not honestly achievable. But the geometry we build for path gives us one half of the classical **D-plane**, and if face-to-path proves to be more stable than face angle alone — which is cheaply testable — we can offer a *suggested* face-to-path tendency derived from the geometry, clearly labelled as inference rather than measurement.

**Primary goal: club path, decomposed and attributed. Secondary goal: face-to-path as a geometric suggestion.**

---

## 1. Glossary — everything decoded before we start

This field is thick with acronyms and terms of art. Here is every one used in this document, in plain English. Skip ahead if you know them; come back if a section stops making sense.

### Body and sequencing

| Term | What it actually means |
|---|---|
| **Kinematic sequence (KS)** | The order and timing in which body segments reach maximum rotation speed during the downswing. The "textbook" order is pelvis → thorax (ribcage) → lead arm → club. Usually drawn as four coloured humps on a speed-vs-time graph. |
| **Proximal-to-distal sequencing (PDS)** | The same idea as a principle: big segments near the body's centre (proximal) go first, small segments at the ends (distal) go last. What coaches call the "kinetic chain" or the "whip effect". |
| **Angular velocity (ω)** | How fast something is rotating. Crucially a **vector** — it has direction as well as magnitude. See §3. Units: degrees per second (°/s) or radians per second (rad/s). |
| **Hub / hub path** | The point the club swings around, roughly the mid-hands. Its trajectory through space is the "hub path". |
| **ISA (instantaneous screw axis)** | The single axis a body segment is *actually* rotating about at a given instant, allowed to move and re-orient frame by frame. More honest than assuming rotation about a fixed anatomical axis. |

### Planes and geometry

| Term | What it actually means |
|---|---|
| **Swing plane** | The flat, tilted disc that best approximates the path of the clubhead (or shaft, or hands, depending who is defining it). |
| **Functional swing plane (FSP)** | Kwon's specific version: the plane fitted to the clubhead trajectory over a defined window around impact. The most standardised definition available. |
| **Delivery plane** | The plane fitted to a short window immediately before impact only. More directly relevant to path than a whole-downswing plane. |
| **Motion plane (MP)** | The equivalent plane fitted to a body point rather than the clubhead — e.g. the plane the lead wrist travels in. |
| **Plane normal (n̂)** | The direction perpendicular to a plane, sticking out of it like a flagpole from a hillside. The "hat" means unit vector — length exactly 1, so direction only, no magnitude. The single most important object in this document. |
| **Inclination / slope angle** | How steeply the plane is tilted relative to the ground. |
| **Direction angle** | Which way the plane is aimed relative to the target line. |
| **PCA / SVD / OLS** | Principal Component Analysis, Singular Value Decomposition, Orthogonal Least Squares. Three related ways of finding the best-fit plane through a cloud of 3D points. They give effectively the same answer; SVD is the most numerically stable and is what I'd use. |
| **Eccentricity** | How squashed an ellipse is. Zero means a perfect circle; higher values mean more elongated. |

### Impact and ball flight

| Term | What it actually means |
|---|---|
| **Club path** | The horizontal direction the clubhead is actually travelling at impact, relative to the target line. Negative (leftward, for a right-hander) is "out-to-in"; positive is "in-to-out". |
| **Angle of attack (AoA)** | The vertical equivalent — whether the clubhead is descending or ascending at impact. |
| **Low point** | The bottom of the clubhead's arc. Where it sits relative to the ball is a major determinant of both path and angle of attack. |
| **Face angle** | Where the clubface is pointing at impact, relative to the target line. |
| **Face-to-path** | Face angle minus club path. The primary driver of how much the ball curves. Zero means a straight shot on whatever line the path dictates. |
| **Closure rate** | How fast the face is rotating from open to closed through impact. Also called club handle twist velocity in the literature. |
| **Spin axis** | The tilt of the ball's axis of rotation. Tilted axis means curved flight. |
| **D-plane** | Jorgensen's "descriptive plane": the plane containing the clubhead's velocity vector and the clubface normal at impact. The ball launches within it and the spin axis is perpendicular to it. See §8.3 — this is the object that unifies everything. |
| **Dynamic loft** | The effective loft actually delivered at impact, after shaft lean and shaft bending, as opposed to the loft stamped on the sole. |
| **Gear effect** | Off-centre strikes make the clubhead twist, imparting sidespin that partly counteracts the resulting start direction. Why a toe hit can curve left with an open face. |

### Statistics and maths

| Term | What it actually means |
|---|---|
| **Dot product (a·b)** | An operation on two vectors returning a single number: how much of one vector points along the other. The mathematical engine of this entire proposal. |
| **Cross product (a × b)** | An operation on two vectors returning a third vector, perpendicular to both. |
| **RMS / RMSE** | Root mean square (error). A way of averaging errors that punishes big ones more than small ones. |
| **SPM1D** | Statistical Parametric Mapping for one-dimensional data. Compares entire *curves* statistically rather than single points on them. |
| **fPCA** | Functional Principal Component Analysis. Compresses a whole curve into two or three numbers capturing most of its shape. |

---

## 2. The problem with the current approach

### 2.1 What we do now

PinPoint currently produces a kinematic sequence chart: four curves, one per segment, each showing rotation speed rising and falling through the downswing. The coach reads off two things — the *order* of the peaks and their *magnitudes*.

Separately, PinPoint produces swing plane information, currently as static angles and shortly as a time series of inclination and direction.

These are presented as unrelated diagnostics on different screens. And critically, **neither of them says anything about where the ball goes.**

### 2.2 The kinematic sequence already hides a plane assumption

Here is the finding that should change how we think about this. Marsan, Bourgain, Thoreux et al. (2019) tested **seven different ways** of computing the kinematic sequence from the same motion capture data on the same thirteen golfers. The seven differed only in which piece of the angular velocity vector they used: its total magnitude, or its component perpendicular to the sagittal, frontal, transverse or *swing* plane, or along the segment's own long axis, or a mixture.

The choice of method produced **almost as many different kinematic sequences as there were methods**, for every golfer. Change the projection, change the verdict on whether the golfer sequences correctly.

Sit with that. The kinematic sequence is not an objective property of the swing. It is a property of the swing *plus* an arbitrary reference direction that most software chooses silently. And one candidate reference direction has a far stronger physical claim than the others, because the club is genuinely trying to travel in a plane.

An analogy. Suppose you are timing four runners but your only instrument measures how fast each is approaching *you*, standing at some arbitrary point on the infield. Move to a different spot and you get different times and possibly a different finishing order — not because the runners changed, but because your measurement axis did. The current kinematic sequence has exactly this problem. The fix is not a better stopwatch; it is to stand somewhere principled.

**We are not proposing to link two independent measurements. We are proposing to make an existing dependency explicit and then exploit it.**

### 2.3 The swing plane has the mirror-image gap

Coleman and Rankin (2005) showed the plane is not static: the lead-arm plane steepened from about 133° to about 102° during the downswing, and rotated from about −9° to +5° relative to the target line. Roughly 30° of tilt change and 14° of aim change in about a quarter of a second.

Thirty degrees is a lot, and something is driving it. But the plane time-series alone tells you *that* the plane moved, never *what moved it*. It is a symptom with no aetiology. You can tell the golfer their plane steepened but not what to do about it.

### 2.4 And neither reaches the ball

The deeper problem is that both metrics stop short of the thing golfers care about. A coach does not want to know that the thorax peaked 40 ms late; they want to know why the ball went left. The bridge between body kinematics and ball flight is **club path**, and nothing in the current feature set attempts to cross it.

The two existing metrics have complementary blind spots and a shared one. That is usually a sign they want to be one thing.

---

## 3. The central idea

### 3.1 Rotation is a direction, not just a speed

This is the conceptual hurdle, and everything follows from it, so I'll labour it.

When we say "the pelvis is rotating at 480 degrees per second", we have said only half of what's true. Rotation happens *about an axis*, and that axis points somewhere. The full description is a vector: point your right thumb along the axis of rotation and your curled fingers show which way the thing is spinning. The vector's length is the speed; its direction is the axle.

Picture a bicycle wheel. The angular velocity vector doesn't lie in the wheel — it sticks out of the hub along the spindle, perpendicular to the wheel. Spin the wheel faster and the vector lengthens. Tilt the wheel and the vector tilts with it.

Current kinematic sequence software mostly reports the **length** of that vector and discards its **direction**. That discarded direction is what we want back.

### 3.2 The plane normal is the ideal axle

The swing plane is a tilted disc. Its normal **n̂** sticks straight out of it — the spindle of the disc.

If the golfer were a perfect machine, every segment would rotate about exactly this axle: pelvis, ribcage, arm and club spinning like coaxial gears on a single shaft. Everything each segment did would go straight into moving the club around the disc, and nothing would be wasted.

Real golfers aren't machines. Each segment's angular velocity points *near* the plane normal but not along it. The mismatch is what nobody measures.

### 3.3 Splitting the vector in two

Any vector splits into a piece along a chosen direction and a piece perpendicular to it. Think of a torch shining at an angle onto a stick: the shadow along the ground is one piece, the height it stands off the ground is the other. Together they reconstruct the stick.

For each segment's angular velocity **ω** and the plane normal **n̂**:

```
ω∥ = (ω · n̂) n̂        ← the part spinning about the plane's axle
ω⊥ = ω − ω∥            ← everything left over
```

The dot product `ω · n̂` is simply "how much of ω points along n̂" — a single number, the length of the shadow.

The two pieces have completely different jobs:

- **ω∥ — in-plane spin.** Carries the club around the disc. Becomes clubhead speed. What you actually want to sequence.
- **ω⊥ — off-plane spin.** Doesn't drive the club around the circle; it tips the circle over. Becomes plane instability, and hence — as §7 will show — becomes club path error.

Both were previously buried inside a single magnitude, indistinguishable.

### 3.4 The alignment index

For each segment, at each frame:

```
a(t) = (ω · n̂) / |ω|
```

This is the cosine of the angle between the segment's rotation axis and the plane's axle. It runs from +1 (perfectly aligned, everything going into the swing) through 0 (rotating entirely off-axis, contributing nothing) to −1 (rotating backwards relative to the plane).

It is dimensionless — a pure efficiency ratio, independent of speed. A slow golfer and a fast golfer can have identical alignment; a Tour player and a beginner can have identical peak speeds with wildly different alignment.

**This is the number the kinematic sequence has never shown.**

---

## 4. Why misalignment ruins accuracy but not distance

This falls straight out of trigonometry and I think it is the most useful single result in the proposal.

If a segment's rotation axis is off the plane normal by angle θ, then the fraction going into **club speed** is cos θ and the fraction going into **tipping the plane** is sin θ. Look at how those behave for small errors:

| Misalignment θ | Speed retained (cos θ) | Speed lost | Off-plane leak (sin θ) | Leak-to-loss ratio |
|---|---|---|---|---|
| 5° | 99.6% | 0.4% | 8.7% | 23 : 1 |
| 10° | 98.5% | 1.5% | 17.4% | 11.6 : 1 |
| 15° | 96.6% | 3.4% | 25.9% | 7.6 : 1 |
| 20° | 94.0% | 6.0% | 34.2% | 5.7 : 1 |
| 25° | 90.6% | 9.4% | 42.3% | 4.5 : 1 |

Read the last column. **At small misalignments the off-plane leak is an order of magnitude larger than the speed penalty.**

The reason is that cosine is *flat* near zero — its derivative there is zero, so small errors cost almost nothing. Sine is *steep* near zero — its derivative is 1, so small errors produce proportionally large effects. For small θ the ratio is approximately 2/θ (θ in radians).

This gives a testable, mechanistic prediction:

> **Misalignment is a first-order effect on plane stability and only a second-order effect on clubhead speed.**

A golfer whose thorax rotates 15° off the swing plane loses about 3% of that segment's speed contribution — undetectable, well inside shot-to-shot noise — but dumps a quarter of that segment's rotation into tipping the plane. And tipping the plane, per §7, moves the club path.

This explains something the current metrics cannot: why a golfer can show a textbook sequence — correct order, rising peaks, beautiful graph — hit the ball a perfectly respectable distance, and still spray it. Their sequence is fine. Their *alignment* is not, and there is no column for it.

It also predicts the reverse, which is a good falsification test: alignment scores should correlate strongly with shot dispersion and weakly with carry distance. If alignment turns out to correlate mainly with distance, the model is wrong and I would want to know that early.

### 4.1 A worked example

Marsan et al. report these typical peak segment speeds:

| Segment | Peak \|ω\| |
|---|---|
| Pelvis | 480 ± 82 °/s |
| Thorax | 605 ± 87 °/s |
| Lead arm | 1310 ± 236 °/s |
| Lead forearm | 1490 ± 203 °/s |
| Lead hand | 1650 ± 211 °/s |

Take two hypothetical golfers, both with textbook sequences and identical peak magnitudes. Golfer A's thorax rotates 5° off the plane normal; Golfer B's rotates 20° off.

- **Golfer A:** in-plane = 605 × cos 5° = **603 °/s**; off-plane = 605 × sin 5° = **53 °/s**
- **Golfer B:** in-plane = 605 × cos 20° = **569 °/s**; off-plane = 605 × sin 20° = **207 °/s**

Their kinematic sequence charts are indistinguishable. Their in-plane contributions differ by 6%. Their off-plane contributions differ by a factor of **four**.

Golfer B is fighting his own thorax for control of the plane, and therefore for control of his path. Nothing on the current chart says so.

### 4.2 A sanity check on scale

Is 207 °/s of off-plane rotation enough to actually move the plane? Compare against Coleman and Rankin's measured plane motion: roughly 30° over a downswing of about 250 ms, an average reorientation rate of order **120 °/s**.

So a single segment misaligned by 20° generates an off-plane component of the same order as — indeed larger than — the total observed plane motion. Two conclusions:

1. The numbers are in the right ballpark; the mechanism is plausible.
2. Observed plane motion must be the **residual of largely-cancelling off-plane components**. Several segments each push the plane around, mostly in opposition, and we see what doesn't cancel.

The second point is important and slightly humbling. The relationship between individual misalignment and net plane motion is not a simple sum but a partial cancellation, so small changes in one segment can produce disproportionate changes in the residual. That argues for reporting alignment *per segment* rather than rolling it into one "efficiency score" that hides the cancellation.

---

## 5. Closing the loop: what moved the plane?

The alignment index tells you a segment is leaking. The coach's next question is: *is that leak what's moving my plane?*

### 5.1 The plane has its own angular velocity

The plane normal **n̂**(t) is a unit vector that swivels over time, and a swivelling unit vector has its own angular velocity:

```
Ω = n̂ × (dn̂/dt)
```

**Ω** is the plane's rotation rate — how fast, and about what axis, the disc is tipping and turning. Stable plane, |Ω| near zero. Hard steepening through the downswing, |Ω| spikes.

### 5.2 Cross-correlation: who leads whom

We now have on one time axis four leak signals (|ω⊥| per segment) and one plane-motion signal (**Ω**).

**Cross-correlation** asks: if I slide one curve forwards or backwards in time, at what offset does it best match the other? Two transparencies with wiggly lines, slid over each other until the wiggles align; the distance slid is the lag.

Run it for each segment against **Ω**. The segment whose leak *leads* the plane motion at the shortest positive lag with the highest correlation is the prime suspect. That is attribution, not just symptom:

> *"Your plane steepened 18° through the downswing. Your thorax off-plane rotation peaks 40 ms before the steepening and tracks it at r = 0.81. Your pelvis shows no such relationship."*

### 5.3 A necessary caution about "cause"

Lead-lag is suggestive of causation but is not proof, and I would resist reaching for Granger causality or transfer entropy even though they're sitting right there.

The reason is mechanical coupling. Pelvis, thorax, arm and club are bolted together through the kinetic chain; when one moves the others are obliged to. Temporal precedence in a rigidly coupled system tells you the *propagation order of a disturbance*, not which link originated it.

The analogy: two dancers in a close waltz. One consistently leads the other by a fraction of a beat, and cross-correlation shows this beautifully. It does not establish that the leading dancer is choosing the steps — they may both be following the music, or the follower may be steering with subtle pressure while appearing to lag.

The only rigorous route to causal claims is forward-dynamics simulation: build a model, change one input, observe the output. MacKenzie and Sprigings did exactly this and validated their model's lead-arm plane against Coleman and Rankin's data. That's a research programme, not a v1 feature.

**Recommendation:** present output as *mechanistic decomposition plus temporal association*. "Your thorax off-plane rotation is closely associated with your plane steepening" is honest. "Your thorax is causing your plane to steepen" is not, and a good coach will eventually catch us out.

---

## 6. Clubhead speed

Before path, the simpler outcome. The clubhead's velocity is the hub velocity plus the velocity contributed by the club rotating about the hub:

```
v_clubhead = v_hub + (ω_club × r)
```

where **r** runs from hub to clubhead. This splits clubhead speed into two attributable sources:

1. **Hub translation** — the hands physically moving through space, generated mainly by arms and torso.
2. **Club rotation about the hub** — the club whipping around the hands, generated mainly by the wrists releasing.

At impact these are wildly unequal, with the rotational term dominating. *Calibrate the actual proportions against our own capture data rather than taking figures from the literature.*

The value of the split is that it maps onto Nesbit and Serrano's linear-versus-angular work decomposition and is immediately coachable: "arms-driven speed generator" versus "release-driven speed generator" is a distinction real coaches make.

**The number that justifies the whole plane-referenced approach:** in the *International Journal of Golf Science* study of amateur drivers, angular impulse due to total torque applied **in the plane of the swing** predicted **91% of the variability in clubhead speed**. That is unusually strong for biomechanics, and it says plainly that the plane-referenced frame is where the signal lives.

---

## 7. Club path — the primary diagnostic

This is the deliverable that justifies the rest of the work.

### 7.1 Path is not the same thing as plane direction

A very common conflation, and getting past it is the whole trick.

The swing plane is a *racetrack*. Club path is *which way the car is pointing as it passes the grandstand*. Same track, different point on the lap, different direction. You cannot read the second off the first without knowing where on the track you are.

Concretely: the clubhead travels on an arc within the plane. Path at impact is the **tangent to that arc at the ball's position on it**, projected onto the horizontal. Move the ball along the arc and the tangent rotates, even though the plane hasn't moved a degree.

### 7.2 The three ingredients

```
club path = f(plane orientation, arc geometry, ball position on the arc)
```

1. **Plane orientation** — the inclination and direction time series from the earlier proposal, evaluated in a short window before impact (the *delivery plane* rather than the whole-downswing plane).
2. **Arc geometry** — the size and shape of the arc. Not a circle: Morrison et al. fitted an **ellipse**, and found eccentricity was *greater* in high-skilled golfers. That matters, because a more eccentric arc means the tangent direction changes faster with position, so better players are in a geometrically more sensitive regime.
3. **Ball position relative to low point** — where along the arc contact occurs.

### 7.3 How much does ball position actually matter? A derivation

Worth doing properly, because the answer sets our accuracy requirements.

Model the arc as a circle of radius R lying in a plane tilted at angle α from vertical. Parameterise position by φ, the angle from low point. Working through the geometry, the tangent's horizontal projection gives a path angle of:

```
path ≈ −arctan( tan φ · sin α )
```

and for small φ this linearises to:

```
path ≈ −φ · sin α
```

Three things fall straight out.

**The sign convention confirms coaching orthodoxy.** At low point (φ = 0), path is zero — the club is momentarily travelling parallel to the target line. Past low point (φ > 0, club rising) the path goes negative: **out-to-in**. Before low point, **in-to-out**. This is exactly why moving the ball back in the stance promotes a rightward path and moving it forward promotes a leftward one, and why teeing a driver forward to hit up on it carries a built-in leftward path bias that good drivers offset by shifting their swing centre away from the target.

**We can put a number on the sensitivity.** With arc length s = Rφ, one inch of ball position is φ = 0.0254/R radians. For a driver with R ≈ 1.6 m that's 0.91°. With a plane tilted around 40–45° from vertical, sin α ≈ 0.7. So:

> **Roughly 0.6–0.7° of club path per inch of ball position along the arc.**

*R and α should be measured per golfer and per club rather than assumed — but the order of magnitude is what matters here.*

**This sets our measurement requirement.** To resolve path to 1° we need to locate the ball relative to low point to within about 1.5 inches (4 cm). That is demanding but not unreasonable, and we already have ball detection. The harder half is low point itself, which for a driver sits behind and below the ball where the club never goes — it has to be *inferred* from the arc fit rather than observed. That's precisely what ellipse fitting does.

### 7.4 The literature has validated this route

Morrison, McGrath & Wallace (2018), *Journal of Sports Sciences* 36(3):303–310, is the key reference and it is remarkably encouraging. Fifty-two male golfers, 27 high-skilled and 25 intermediate, hitting 40 drives each. They fitted the clubhead trajectory near impact to an ellipse, achieving a fit **RMSE of 1.2 mm**. Club path and angle of attack derived from that fitted trajectory came out at **2% and 3% normalised bias-corrected RMSE** respectively. They also generated a set of "rule of thumb" values relating club path, angle of attack and delivery plane angle for coaches.

So the geometric route from plane and arc to path is a solved problem at roughly 2% accuracy. That is our validation target, it is published, and it is achievable.

### 7.5 The decomposition — and why it's the actual product

Here is where we do something a launch monitor structurally cannot.

A launch monitor measures the tangent. It sees the *sum* of the three ingredients and has no way to separate them. So when it reports −4° path, it reports a symptom with no cause attached.

We can report:

> *"Your path is −4°. About −1.5° of that comes from your delivery plane being aimed left of target. The other −2.5° comes from contacting the ball roughly 4 cm past your low point."*

**Two golfers with identical path readings can have opposite problems.** Golfer A aims the plane left and contacts at low point. Golfer B has a perfectly aimed plane but is catching the ball late in the arc. Tell both of them to "swing more from the inside" and you help the first and wreck the second. Tell Golfer B to move the ball back and shift their low point forward and you fix them without touching a plane that was never broken.

I don't know of another product that separates these. It is a genuine differentiator and it falls out of geometry we're building anyway.

### 7.6 Tracing each term back to the body

This is where the kinematic sequence rejoins, and each of the three ingredients has a different biomechanical origin — which is itself useful, because it tells a coach *which part of the swing to work on*.

| Path ingredient | Traces back to | Sequencing signature |
|---|---|---|
| **Delivery plane direction** | Off-plane leaks, ω⊥, from §3–5 | Attributable per segment via the cross-correlation in §5.2. A misaligned thorax shows up here. |
| **Arc geometry (radius, eccentricity)** | Hub path shape; lead arm extension; wrist release radius | Related to Miura's parametric acceleration — pulling the hub in shortens the radius, which changes both speed and arc shape. |
| **Ball position on the arc** | Low point location, driven by lateral centre-of-mass shift and hub path translation toward the target | A *sequencing* quantity: low point moves target-ward when the pelvis leads properly. Late pelvis, low point behind the ball. |

That last row is the satisfying one. **Low point location is a direct consequence of sequencing.** The pelvis leading the downswing shifts the swing centre toward the target, which moves low point forward, which moves the ball earlier on the arc, which moves path rightward. There is a clean mechanical chain from "your pelvis is late" to "your path is 3° left", and we can trace every link of it.

That chain — sequencing → low point → arc position → path — is, I think, the single most valuable thing in this proposal. It is the bridge from body to ball that neither existing metric crosses.

---

## 8. Face-to-path — a suggested inference, not a measurement

Everything above concerns where the club is *going*. Face angle concerns where the club is *pointing*, and it is a genuinely different animal. I want to scope it honestly: **we should present this as geometric suggestion, clearly labelled, not as a measured value.**

### 8.1 The face is a degree of freedom nothing currently sees

The plane framework captures two rotational degrees of freedom — in-plane spin and off-plane tilt. Face angle is a **third**: rotation about the shaft's own long axis. It is very nearly orthogonal to everything discussed so far. You can hold plane, path and speed perfectly constant and rotate the face through 40°.

### 8.2 The club body frame

The natural extension is to stop resolving the club's angular velocity against the plane normal alone and instead resolve it in a frame fixed to the clubhead:

- **ŝ** — along the shaft
- **f̂** — out of the face (the face normal)
- **t̂** = ŝ × f̂ — roughly along the leading edge

Then ω_club resolves into three physically distinct channels:

| Component | Physical meaning | Determines |
|---|---|---|
| ω·f̂ | swinging the club through the arc | club speed, path |
| **ω·ŝ** | **face rotation — opening and closing** | **face angle** |
| ω·t̂ | toe-up / toe-down | dynamic lie, gear effect exposure |

The middle channel already has a name in the literature — *club handle twist velocity* — and there is published work relating it to biomechanical characteristics of the drive. It is the closure rate coaches talk about, made rigorous.

Physiologically it traces to the distal end of the chain: lead wrist flexion (bowing) closes the face, extension (cupping) opens it, and lead forearm supination through the downswing rotates the face back toward square. There is a large-DOF study finding that 16 degrees of freedom explain 97–99% of endpoint velocity, with the largest contributions from pelvis and torso twist proximally and elbow pronation/supination plus wrist flexion/extension in the lead arm — note that those distal degrees of freedom appear in *both* the speed story and the face story, which is not a coincidence and which §8.5 returns to.

### 8.3 The D-plane: where path and face rejoin

This is the unification, and it is older than any of the modern work.

Jorgensen (*The Physics of Golf*, 1994) defined the **D-plane** — "D" for descriptive — as the plane containing the clubhead's velocity vector **V** and the clubface normal **N** just before collision. The ball launches within that plane, and for a centred strike the ball's spin axis is perpendicular to it. Tuxen built TrackMan around measuring it.

Look at what that means for us:

- **Swing plane + arc geometry gives us V.** (That's §7.)
- **Club body frame gives us N.** (That's §8.2.)
- **V and N together are the D-plane.**

We would not be inferring two loosely-related numbers. We would be reconstructing the single geometric object that determines ball flight, from two halves we can each obtain. Face-to-path is simply the angle between V and N; curvature follows from the D-plane's tilt.

The transfer functions onward to ball flight are published: Wood et al. (2018) found average horizontal launch angle sits 61–76% of the way from club path toward face angle, and vertical launch angle 72–83% of the way from angle of attack toward dynamic loft, across golfers and a robot using driver, 7-iron and 58° wedge.

### 8.4 Why face is much harder than path

Now the uncomfortable part, and the reason face is scoped as inference rather than measurement.

**Path is proximally driven and slow. Face is distally driven and fast.**

Vaughan found the swing plane is essentially established by about 0.1 s before impact. It is set by large segments moving at 480–600 °/s and it changes at order 100 °/s. Face closure, by contrast, happens at the very end of the chain at rates that can exceed 1000 °/s near impact.

Work through the consequence. At 1000 °/s the face rotates **1° per millisecond**. A 2 ms variation in release timing is 2° of face — several yards offline immediately, more once curvature develops. No model built on body kinematics will predict that reliably, because the required timing precision exceeds both what we can resolve and what the golfer can repeat.

I would rather state this plainly now than have us build toward a number we cannot deliver.

### 8.5 But face-to-path may behave better than face alone

Here is the escape hatch, and I think it is the most promising speculative line in this document.

Face and path share upstream causes. The classic pattern — late release gives steep, out-to-in *and* open; early release gives shallow, in-to-out *and* closed — says the two errors move together. If they do, their *difference* has lower variance than either component. Which would be very convenient, since face-to-path is precisely what determines curvature, and curvature is what golfers actually complain about.

**This is testable for essentially nothing.** Take any launch monitor dataset — ours or a public one — and compute, within golfer, σ(face), σ(path) and σ(face − path). If σ(face − path) is meaningfully smaller than both, the coupling is real and face-to-path is the right target. If it isn't, face and path are independently controlled and we should scope face out entirely.

That is a one-afternoon experiment that determines the shape of a multi-month feature. It should happen early.

There is a stronger version worth testing at the same time: that a single latent variable — **release timing relative to the plane** — simultaneously sets club speed (via parametric acceleration), arc position at impact (hence path), and closure progress (hence face). If true, fPCA over the alignment and closure curves should recover it as the first principal component, and we would have one number predicting three outcomes. That is the version that would be transformative. It is also the version most likely to be too neat to survive contact with data, so it is a hypothesis, not a plan.

### 8.6 How we present it

Given §8.4, the face output must be framed carefully. My recommendation:

- **Never present a face angle in degrees as though measured.** We aren't measuring it.
- **Present a face-to-path *tendency*** — a directional suggestion with an explicit uncertainty band, derived from the geometry and the closure-rate channel.
- **Anchor it to the observed shot** where possible. If we know the ball curved left, we know the sign of face-to-path; the geometry then tells us *why*, which is the useful part.
- **Label it visually as inference.** Different treatment from measured quantities — dashed, tinted, whatever the design language supports. A coach who understands we are suggesting rather than measuring will trust the rest of the product more, not less.

The honest pitch is: *"the geometry suggests your face-to-path is running open, and the closure channel says it's because the face is still rotating quickly at impact rather than having settled"* — which is a real, actionable observation that doesn't overclaim precision we don't have.

---

## 9. What we would ship

In build order:

| # | Metric | Definition | What it tells the golfer |
|---|---|---|---|
| 1 | **In-plane kinematic sequence** | \|ω∥\| per segment over time | The speed-generating sequence with off-plane noise stripped out. The honest version of the chart we already show. |
| 2 | **Alignment index** | a(t) = (ω·n̂)/\|ω\| per segment | How much of each segment's effort goes into the swing versus fighting the plane. |
| 3 | **Plane reorientation rate** | \|Ω\| where Ω = n̂ × dn̂/dt | How much the plane is moving, moment by moment. |
| 4 | **Club path** | Tangent to the fitted arc at ball position | **Primary deliverable.** Where the club is actually going. |
| 5 | **Path decomposition** | Path split into plane-direction and arc-position contributions | **The differentiator.** Not what the path is, but *why*. |
| 6 | **Attribution** | Lagged cross-correlation of \|ω⊥\| and low-point shift against path error | Which part of the swing is responsible. |
| 7 | **Face-to-path tendency** | Angle between fitted V and inferred N, with uncertainty band | *Suggested*, clearly labelled as inference. |

Plus the clubhead speed decomposition (§6) as a simpler standalone.

### 9.1 Visualisation

Two views, not seven charts.

**The sequence view.** Keep the familiar four-hump kinematic sequence, but shade each curve by its alignment index — green where the segment is on-axis, warming to red where it leaks. Underneath, on the same time axis, the plane reorientation rate. A coach reads in one glance: *the sequence is in order, but the thorax hump goes orange right where the plane rate spikes.* No new mental model required — it's the chart they know with the missing dimension painted on.

**The path view.** A down-the-line schematic of the fitted arc with the ball on it, the low point marked, and the tangent drawn. Alongside, the decomposition as a simple stacked bar: total path, split into plane contribution and arc-position contribution. This is the screen that answers "why did it go left", and I would expect it to become the most-used view in the product.

---

## 10. The statistical layer

### 10.1 Don't reduce to peaks

The instinct is to summarise each curve by peak value and peak timing, then run ordinary statistics on those. **Resist it.** Reducing a curve to two numbers throws away exactly the coupling structure we've gone to the trouble of exposing. The interesting question is not "when did the thorax peak" but "what shape did the thorax alignment curve have, and did that shape co-occur with the plane movement".

### 10.2 SPM1D for curve comparison

**Statistical Parametric Mapping for one-dimensional data** compares whole curves. Conceptually: run a statistical test at every time point, then correct for having run hundreds of correlated tests. The output isn't "p = 0.03" but a shaded region on the time axis: *"this golfer differs significantly from the reference cohort between 45% and 62% of the downswing."*

Far better for coaching than a single p-value, because it localises the problem in time. Free implementations exist in Python (`spm1d`) and MATLAB.

### 10.3 fPCA for compression

**Functional Principal Component Analysis** takes a family of curves and finds the two or three "shape modes" explaining most of the variation. Each golfer's curve becomes two or three scores, feedable into ordinary regression against path error or dispersion.

The analogy: rather than describing a face pixel by pixel, describe it as "70% along the round-to-narrow axis, 20% along the young-to-old axis". Most of the information, a fraction of the numbers.

This is how I would build the eventual predictive model: fPCA scores of the alignment curves as predictors, path error as response.

### 10.4 Precedent for a nuanced verdict

Lee, Ehrlich and Cain (2026) studied fourteen elite golfers — PGA Tour, Korn Ferry, NCAA and PGA of America — regressing sequencing quality across backswing, transition and downswing onto clubhead speed. Their most useful finding for us: **slight variations from the "optimal" sequence yielded comparable clubhead speed**.

A warning against a binary pass/fail verdict. There is not one correct sequence but a family of workable ones. Whatever we ship should present a band or distribution, not a tick or a cross. Coaches burned by dogmatic sequence software will notice the difference.

---

## 11. Risks and failure modes

### 11.1 The alignment index is a ratio of two noisy quantities

a(t) = (ω·n̂)/|ω| divides one estimated vector by the magnitude of another. When |ω| is small the denominator approaches zero and the index becomes wildly unstable.

Where is |ω| small? At transition, where segments reverse — precisely where coaches most want to look.

**Mitigation:** gate a(t) on a magnitude threshold. Below it, grey the curve or render it as a dashed uncertainty band. Never show a confident-looking alignment value derived from near-zero rotation. Set the threshold from our own measured noise floor, not from a round number.

### 11.2 Pose-derived pelvis and thorax rotation is the weak link

ViTPose gives joint positions; segment angular velocity requires differentiating noisy positions and fitting a rotation. Pelvis and thorax are worst because they're defined by few keypoints over a short baseline, so small keypoint errors become large angular errors.

IMU-instrumented segments will be far better, creating an awkward asymmetry: the segments we measure most reliably are not the ones coaches most want to discuss.

**Mitigation:** carry per-segment confidence to the UI. Consider whether pelvis and thorax alignment ships in v1 at all, or waits for better pose smoothing.

### 11.3 The plane normal inherits our shaft-detection error

Everything is referenced to n̂. Our shaft accuracy is around 0.49° RMSE face-on and 2.44° down-the-line, so the normal carries at least that. Given that §4's interesting effects live at 5–20° of misalignment, a 2° systematic error is not negligible relative to signal.

**Mitigation:** validate on synthetic swings first. Generate a trajectory with a known plane and known segment axes, run it through the full chain, measure what emerges. If we can't recover a synthetic 10° misalignment to within a couple of degrees, we shouldn't ship the metric.

### 11.4 Low point is inferred, not observed

For a driver the low point sits behind and below the ball, where the club never travels. It must be extrapolated from the arc fit. Extrapolation beyond the data is where fits go wrong, and per §7.3 a 4 cm error in low point is about 1° of path.

**Mitigation:** fit the arc over as wide a window as the data supports, quantify the extrapolation uncertainty explicitly, and propagate it into the path confidence interval. Morrison et al.'s 1.2 mm ellipse RMSE suggests this is tractable, but they had optical motion capture and we do not.

### 11.5 Shaft deflection breaks the wrist-to-face chain

The shaft is bent and twisted at impact. Face orientation at the clubhead is not what grip orientation implies — toe droop and shaft twist add several degrees, and the amount is club-specific. This is exactly why HackMotion *correlates* with face angle without determining it. Any wrist-to-face model needs a shaft compliance term.

### 11.6 Strike location is an invisible confounder

Gear effect on off-centre hits breaks the clean D-plane relationship: a toe strike can curve left with an open face. Strike location is invisible to body kinematics. It is irreducible, and it puts a floor under face-related prediction error that better kinematics cannot lift. Another reason face is scoped as inference.

### 11.7 We need face orientation and our shaft tracker doesn't provide it

Detecting the shaft gives ŝ but not roll about ŝ. That's a genuine hardware gap: either a clubhead-mounted IMU, vision good enough to resolve face or hosel orientation, or a crown marker.

**Worth stressing: the path half needs nothing we don't already have.** Only the face half is blocked on hardware. This is a strong argument for the phasing in §12.

### 11.8 We will disagree with every other product on the market

TPI, GEARS, K-Vest and the rest compute the kinematic sequence on resultant magnitude or the segment's longitudinal component. If we default to a plane-normal basis our numbers won't match anything users have seen — and per Marsan et al. they *will* differ, potentially in reported order, not just magnitude.

**Mitigation, non-negotiable:** compute both. Default the chart to the conventional basis so our numbers reconcile with the industry. Offer the plane-referenced view as clearly-labelled additional analysis with a short explanation. Otherwise the first support ticket is "your kinematic sequence is wrong", and we'll spend more time defending the maths than selling the insight.

### 11.9 Novelty risk

Marsan et al. found the swing-plane projection was *not* the best at recovering the textbook sequence order — their mixed method did better. Genuine caution: the plane-normal basis may not reproduce what coaches expect.

But this cuts both ways. Marsan et al. asked "which projection best recovers the conventional sequence?", treating that sequence as ground truth. We are asking "which projection best predicts *outcomes* — path error, dispersion?" Those may have different answers, and the second is the one that matters commercially. We should be explicit that we're departing from their framing deliberately.

---

## 12. Phasing

Restructured to get path in front of users as early as possible, since it needs no new hardware.

**Phase 1 — Foundations.** Swing plane as a time series. Segment angular velocity vectors from existing pose and IMU pipelines. Delivery-plane fitting over a short pre-impact window. No new UI. Validate everything against synthetic data.

**Phase 2 — Arc fitting and path.** Ellipse fit to the clubhead trajectory near impact, low point extrapolation with explicit uncertainty, path and angle of attack derivation. **Validate against a launch monitor.** Target: match Morrison et al.'s 2% on path. This is the first real go/no-go gate — if we can't hit path accurately, the decomposition is worthless.

**Phase 3 — Path decomposition and first UI.** Split path into plane-direction and arc-position contributions. Ship the path view (§9.1). This is the first thing that delivers standalone user value, and it can ship before any of the alignment work is exposed.

**Phase 4 — Alignment and attribution.** Compute ω∥, ω⊥, a(t) and Ω. Validate the §4 prediction: does alignment correlate with dispersion more strongly than with distance? Second go/no-go gate. Then ship the alignment-shaded sequence view and the cross-correlation attribution linking sequencing to path error.

**Phase 5 — Face-to-path, conditional.** Run the σ(face − path) variance test *early*, ideally during Phase 2 since it needs only launch monitor data. Only proceed if it passes and the hardware gap in §11.7 is resolved. Ship as clearly-labelled inference per §8.6.

**Phase 6 — Statistics.** SPM1D against a reference cohort, fPCA-based outcome modelling, the release-timing latent variable hypothesis.

The critical discipline is at the two gates. It would be easy to skip to the pretty charts, because the charts are the fun bit. But the value proposition rests on path being accurate and alignment actually predicting something, and we should find both out before building UI around them.

---

## 13. Open questions

1. **Which plane for which purpose?** The delivery plane (short pre-impact window) is right for path. The functional swing plane or a whole-downswing fit may be right for alignment. They differ, and using two planes in one product needs careful labelling so coaches don't think we're contradicting ourselves.

2. **Windowed or instantaneous n̂?** A sliding-window fit is stable but lags; a truly instantaneous plane is responsive but noisy. Since Ω involves differentiating n̂, this matters a great deal.

3. **How wide a window for the arc fit?** Wider gives a better-conditioned fit and better low-point extrapolation; narrower better reflects the actual delivery geometry. Empirical question.

4. **Which segments?** Four (pelvis, thorax, arm, club) matches convention. Marsan et al. use five. More segments means finer attribution, more noise, a busier chart.

5. **Backswing too?** Everything above assumes the downswing. Plane fitting quality is known to vary across phases and the backswing plane is less well-defined. Probably v2.

6. **What's the reference cohort?** SPM1D needs something to compare against. Build our own, licence one, or ship without normative comparison initially?

7. **Do we need a launch monitor in the loop for validation, and whose?** Phase 2 is blocked without one. Worth resolving early — this may be a procurement question rather than an engineering one.

---

## 14. Key references

| Reference | Why it matters |
|---|---|
| **Morrison A, McGrath D, Wallace ES (2018).** *Journal of Sports Sciences* 36(3):303–310. | **The keystone for path.** Ellipse fitting to clubhead trajectory near impact (1.2 mm RMSE), path and angle of attack derived at 2% and 3% normalised RMSE, plus coach-facing rules of thumb relating path, angle of attack and delivery plane angle. |
| **Marsan T, Bourgain M, Thoreux P, et al. (2019).** *Acta of Bioengineering and Biomechanics* 21(2):115–120. | **The keystone for sequencing.** Shows the kinematic sequence depends critically on which angular velocity component is used, including the swing-plane projection. The paper that makes this proposal necessary. |
| **Jorgensen T (1994).** *The Physics of Golf.* | Origin of the D-plane: the plane containing clubhead velocity and face normal at impact, within which the ball launches and perpendicular to which the spin axis lies. |
| Coleman SGS & Rankin AJ (2005). *J Sports Sci* 23(3):227–234. | First systematic quantification of instantaneous plane inclination and direction through the downswing. Source of the 133°→102° steepening figure. |
| Kwon YH, Como CS, Singhal K, Lee S, Han KH (2012). *Sports Biomechanics* 11(2):127–148. | Defines the Functional Swing Plane and the standard slope/direction angle conventions. |
| Morrison A, McGrath D, Wallace ES (2014). *Procedia Engineering* 72:144–149. | Changes in clubhead trajectory and planarity across swing phases; introduces the delivery plane. |
| Cheng KJ, Jump IP, Klubertanz MR, Oliver GD (2026). *Applied Sciences* 16(5):2327. | Open-access review of plane-fitting methods (PCA, SVD, OLS, FSP) and planarity metrics. Best single entry point. |
| Lee J, Ehrlich J, Cain C (2026). *Int J Sports Science & Coaching*, DOI 10.1177/17479541261429479. | Kinematic sequence → clubhead speed in fourteen elite golfers. Source of the "slight variations give comparable results" caution. |
| Wood et al. (2018). | Transfer functions from path/face and attack angle/dynamic loft to launch direction: 61–76% and 72–83% respectively. |
| Nesbit SM (2005). *J Sports Sci Med* 4:499–519; Nesbit SM & Serrano M (2005). | Work and power decomposition. Basis for the linear/angular attribution in §6. |
| Nesbit SM & McGinnis R. Hub path analysis (PMC3761476). | Hub path and its role in golfer/club kinetic transfer. |
| *International Journal of Golf Science* — "How Amateur Golfers Deliver Energy to the Driver". | In-plane angular impulse predicts 91% of clubhead speed variance. |
| Sports Engineering (2011), DOI 10.1007/s12283-010-0059-7, Part 2. | Kinematic sequence relative to each segment's instantaneous screw axis. Conceptual precursor to the alignment index. |
| Carson HJ et al. (2018/2019). Grip type, wrist kinematics and clubhead characteristics. *J Sports Sci* / PMID 30110244. | Wrist flexion/extension and internal/external rotation versus clubface angle at impact. Foundation for the face channel. |
| Tinmark F, Hellström J, Halvorsen K, Thorstensson A (2010). | Elite golfers' kinematic sequence, full and partial swings. Good normative magnitudes. |
| MacKenzie SJ & Sprigings EJ (2009). *Sports Biomechanics*. | Forward-dynamics model whose lead-arm plane validates against Coleman & Rankin. The route to genuine causal claims. |
| Betzler NF, Monk SA, Wallace ES, Otto SR (2012). *J Sports Sci* 30:439–448. | Variability in clubhead presentation and impact location. Useful for setting realistic error expectations. |

---

## 15. Summary

The kinematic sequence and the swing plane are not two measurements. Every segment's rotation splits, via a single dot product against the plane normal, into a part that generates clubhead speed and a part that destabilises the plane. Current software reports the sum and can't tell them apart. Separating them yields an alignment index whose trigonometry predicts something non-obvious and useful: small misalignments cost almost nothing in distance but leak heavily into plane instability.

**That plane instability is not an abstraction — it is club path.** Path at impact is the tangent to the clubhead's arc at the ball, and it depends on plane orientation, arc geometry, and where the ball sits along the arc. A launch monitor sees only the sum of those three. Decomposing them lets us tell a golfer not just that their path is 4° left, but how much of that comes from an aimed-left plane versus contacting late in the arc — two problems with opposite fixes that currently produce identical numbers. And each ingredient traces back to the body, with low point location in particular being a direct consequence of pelvis sequencing. That chain — sequencing → low point → arc position → path — is the bridge from body to ball that neither existing metric crosses, and it is the primary goal of this work.

Face-to-path rides along on the same geometry, because the fitted arc gives us the velocity vector that forms one leg of Jorgensen's D-plane. But face is driven distally and rotates at roughly a degree per millisecond near impact, which puts honest prediction beyond reach. We should offer it as a clearly-labelled geometric *suggestion* with an explicit uncertainty band, and only after a cheap variance test confirms that face-to-path is more stable than face angle alone.

The risks are real: noise in the alignment ratio at low rotation speeds, weak pose-derived pelvis and thorax data, low point being extrapolated rather than observed, and inevitable divergence from every competitor's sequence numbers. All are manageable with confidence gating, synthetic validation, and shipping both bases side by side.

The things to establish before building UI are the two gates in §12: can we derive path to within about 2%, and does alignment actually predict dispersion? If both hold, we have something no other product has. If either fails, we've learned it cheaply.
