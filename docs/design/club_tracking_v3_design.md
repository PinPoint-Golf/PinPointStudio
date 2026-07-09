# Club tracking v3 — physics-first design and research plan

**Status: design, 2026-07-05.** Successor to stripe fusion v2.0
(`stripe_fusion.py`, as-built notes in `stripe_fusion_design.md`; full
adjudication record in lab `tape_20260705/RESULTS.md`). Reviewed premise,
from Mark: v2.0 fought every counterfeit with a generic guard and bought
honesty by abstention — coverage shrank with each patch. v3 makes the golf
swing's physical structure the primary discriminator, so counterfeits are
rejected by *understanding* while real measurements are kept.

## 1. The four fundamentals, promoted to first-class constraints

**C1 — the club is held in the hands.** It is impossible for the club to
be anywhere that does not start at the hands. The full-strength form is a
*butt-termination test*: club evidence terminates within `r0 ∈ (0,
~260 mm]` behind the grip anchor. A line whose support continues behind
the butt (trouser crease, screen edge, shaft shadow) is a scene line —
vetoed regardless of how well it scores. (v2.0 used only the weak form:
"ray passes within 80 px of the anchor". Every adjudicated counterfeit
passed that gate.)

**C2 — the club overlaps the body only at address/impact and finish.**
From takeaway through top, downswing, and follow-through the club is in
free space. `skeleton.csv` (8 body joints/frame, already produced by
`prep_swing.py`) defines a per-frame body polygon (+ margin, temporally
smoothed through blur-degraded pose frames). Phase-dependent rule:

| phase | body-region evidence |
|---|---|
| takeaway → top → downswing → thru | ignored; candidate with majority support inside the body polygon is VETOED |
| address, impact zone, finish | admitted, but never sufficient alone — requires free-space support or a band lock |

Every counterfeit adjudicated in v2.0 (trouser creases, the s06 leg shadow
that stage-1's measured tier tracked, sleeve/shirt texture, foot-region mat
speckle) lives inside the body polygon during phases where the club cannot.

**C3 — the swing changes direction at the top.** Phase segmentation comes
from the hands alone (`anchors.csv`): grip speed + trajectory reversal
give still → takeaway → backswing → TOP → downswing → impact zone → thru
→ finish, with no club detection required. Within each phase the rotation
direction of θ(t) is known (chirality detected once per swing from the
hand trajectory — no handedness hardcoding), so a 180° flip becomes
*structurally impossible* rather than statistically filtered, and outage
bridging becomes a monotone bounded-rate sweep instead of naive
interpolation (v2.0's impact-zone flips cannot occur).

**C4 — club + lead arm form a dual pendulum.** With α(t) = lead-arm
direction (pose; φ is already in `anchors.csv`) and θ(t) = shaft
direction, the wrist angle ψ = θ − α is anatomically bounded and evolves
smoothly: hinging during the backswing, monotone release through the
downswing, aligned (ψ ≈ 180°) near impact. This yields a per-frame
*reachable cone* for θ — the candidate search space shrinks from 360° to
tens of degrees — plus a smoothness prior on ψ that is gentler than on θ
itself (ψ changes slowest exactly where θ changes fastest is *not* true
near release, but ψ's bounds still hold; the DP transition model encodes
both).

> **Revised, 2026-07-06 — read with §8.** The *cone* above is a **magnitude**
> bound on ψ, and the v3.0 as-built found it unrealisable as a search-space
> reducer: pose φ jumps up to ~87°/frame at the top and through the blur, so the
> cone had to be widened to 150° and switched off at address/finish/top, with C3
> (not C4) carrying the real collapse. The *stronger* structure of the double
> pendulum — that ψ is **monotone with a single reversal**, exactly C3's law but
> on the wrist — was never encoded. §8 develops it as a DP transition rail; it is
> the more important half of C4 and the current work-front.

## 2. v3.0 core: constrained global estimator

State per frame: θ (shaft direction), with (s, r0) attached when bands
lock. Evidence engines are **unchanged and already validated** — E1
band-ratio matcher (1.1–3.3° median, zero flips on corpus) and E2
polarity-aware ridge — but they are evaluated only inside the constraint
set C1∧C2∧C4, per phase C3.

Estimation is global, not per-frame: dynamic programming (Viterbi) over a
θ grid across the whole clip, transition costs from C3/C4 (phase-signed
rotation, |ω| ≤ ω_max(phase), |ω̇| bounded, ψ within anatomy), emission
costs from E1/E2. This replaces v2.0's local corroboration/interp guards,
which demote to sanity assertions. Output tiers as in v2.0 — `band`
(θ+s+head), `ray` (θ-only), plus DP-bridged `pred` (kept out of truth
files, same honesty stance). Determinism preserved (no RNG; DP is exact).

Expected effect vs v2.0, to be verified not assumed: finish and address
θ coverage return (their counterfeits are now excluded by C1/C2 instead
of by abstention), down/thru coverage at least maintained, junk stays
zero.

## 3. v3.1 variant: rotation-compensated shift-and-stack ROI

The astronomy trick, adapted: the club rotates about a (moving) hand
pivot. For a window of 2k+1 frames and a hypothesized rotation profile
ω(t) (drawn from the C3/C4-constrained DP prior — a few tens of
hypotheses, not a blind grid):

1. translate each frame so the grip anchor is registered (kills hand
   translation);
2. rotate frame t by −∫ω dt about the registered pivot;
3. stack (median or mean).

Under the correct ω the club pixels integrate **coherently** across the
window — SNR grows ~√N on top of per-frame streak energy — while the body
and background smear into arcs. The stack that maximizes coherent ridge
energy along a ray simultaneously yields: the club **region of interest**,
a refined ω estimate for the window, and a deblurred composite in which
the 2-1-3 band pattern can re-emerge at speeds where any single 6.57 ms
frame is hopeless. C2 applies before scoring: body-polygon pixels are
masked out of the coherence measure (per Mark: ROI-finding must respect
the overlap schedule), and C1's butt-termination is tested on the stacked
composite.

Pipeline placement: coarse pass on ¼-resolution frames proposes the club
corridor per window; E1/E2 (and v3.0's DP) then run full-res inside the
corridor only. This is the designed attack on the last uncovered fast
segment (impact ±10 frames, where v2.0 emits nothing) and doubles as
compute containment.

A cheap sibling for moderate speeds stays: per-frame |frame − scene
median| ROI (v2.0's motion suppression), now intersected with ¬body.

## 4. v3.2: address/hold θ truth

With C1 (butt termination), C2 (address-mode body rule), C4 (cone) and
the hold-period pixel stack (valid — the club is physically static), the
address line search is no longer "brightest thin line wins": candidates
are lines inside the cone, terminating at the hands, with the mat-crossing
property (at address the shaft is the *only* line that continues — dark —
across the blown mat toward the ball). Lateral line fitting (fit the line,
then require hand-proximity) absorbs the pose-anchor bias that force-
through-anchor sampling suffered. Output: θ-only truth for stills; (s, r0)
only if the band pattern locks (it usually cannot at this exposure —
known optics, not a tooling gap).

> **Extended by §9 (2026-07-08).** v3.2's hard case — telling the still club
> from a trailing-leg line that outscores it per-frame and crosses the mat with
> the same polarity — gains an external discriminator the cone/stack/mat gates
> cannot supply on their own: *the real shaft points at the ball.* The reliable
> v2 ball detector gives a fixed far-end landmark at address (and impact) that
> pins direction, scale, and the address/takeaway boundary. See §9.

## 5. v3.3: learned components — where training data beats hand-tuning

Physics stays the frame; learning fills perception gaps that classical
templates handle poorly. Rules of engagement: frozen versioned weights,
deterministic inference (no augmentation at test time), corpus-gated
acceptance like every other stage, ONNX Runtime deployment (already in the
app for Kokoro — the path exists).

**Heel/toe keypoint head (Mark's ask).** A small heatmap CNN (lightweight
U-Net-class, single class per keypoint: heel, toe, optionally hosel) that
localizes clubhead extremities inside the v3.1 ROI. Why learned: the head
is the one component with no ratio template — shape varies per club, blur
smears it anisotropically, and stage-2's classical terminus logic is the
current honesty bottleneck (clause fails on 7/10 swings). Why feasible
without hand-labeling thousands of frames — the **data flywheel**:

1. instrumented captures + stripe truth pin the shaft line, scale s, and
   hosel extrapolation per frame → weak labels for heel (hosel-adjacent)
   and a search ray for toe;
2. human adjudication of a stratified subset (the lab's montage workflow)
   promotes weak labels to gold;
3. blur augmentation synthesized from measured ω(t) (we know the true
   per-frame sweep from truth) manufactures hard training examples that
   match this camera's actual physics;
4. acceptance: head px error and conf-honesty clauses vs instrumented
   truth on held-out swings; the NN must beat stage-2's classical head
   measurement on the SAME corpus before it ships.

Payoff beyond the lab: a head detector trained this way does not depend on
tape — it is the bridge from instrumented lab clubs to the **passive
product path** (unmarked clubs), which is where the programme wants to
land (protocol §5.3).

**Deliberately NOT learned (for now):** shaft θ (E1/E2 + physics already
achieve ~1° with zero flips; a NN adds risk, not accuracy) and the phase
model (trivially derived from pose).

## 6. Research plan and gates (each stage corpus-gated before the next)

| stage | content | gate |
|---|---|---|
| v3.0 | phase model, C1–C4 constraint set, DP estimator wrapping validated E1/E2 | synth gate extended with phases + body polygons + counterfeit population; corpus: zero adjudicated errors AND coverage ≥ v2.0 in down/thru, > 0 at finish + address-adjacent; determinism |
| v3.0-r1 | **C4 ψ-monotonicity, as isotonic reconciliation** (fit monotone ψ, read error off the residual; reconstruction bounded to the impact blur, `RECON_PHASES=("impact",)`; §8.1) — the double pendulum's monotone structure, the substantive form of C4 replacing the wide cone | gate-0 (s01 hand markup), synth `--selftest`, s01 A/B, and 10-swing studio corpus all PASSED (2026-07-07); thru p90 5.5→3.9 (regression fixed), thru cov 649→671, down `bad>15`→0, flips 0, byte-identical determinism; fixture re-freeze pending |
| v3.1 | shift-and-stack ROI + ω measurement in the blur zone | impact ±10 frames: θ/ω emitted with visual adjudication of stacked composites; ω(t) smooth, peak within physical range for a 7-iron; no regression elsewhere |
| v3.2 | address/hold θ truth | agreement with stage-1 address meas (reliable there) + hand-label spot checks; zero flips |
| v3.3 | heel/toe NN + data flywheel | beats classical stage-2 head on held-out instrumented swings; honesty clauses pass; frozen weights versioned |
| v4 | C++ port behind `IShaftDetector` (marker mode + passive informed by truth); NN via ORT | exemplar-oracle byte-agreement for classical parts; app-side corpus replay |

Cross-cutting metrics from v3.0 on: per-phase coverage AND accuracy vs
instrumented truth (never totals alone), ω(t) profile sanity, stage-2 head
error trend, and a standing counterfeit-regression suite built from every
adjudicated failure to date (trouser crease, leg shadow, shirt texture,
arm line, mat speckle, streak-centroid impact locks — each now a named
test case).

## 7. Risks

- **Pose anchor bias/outage** at peak blur → lateral line fits + C1 test
  rather than force-through-anchor; body polygon temporally smoothed.
- **Phase mis-segmentation** on unusual swings (pumps, rehearsal waggles)
  → phase model must emit confidence; low-confidence spans fall back to
  v2.0's conservative rules.
- **C4 ψ-rail is only as clean as φ's trend** (§8) → the ψ monotonicity is
  pristine but pose φ spikes to ~87° at the top/impact; de-spike φ, key the
  rail on the *sign/trend* of Δψ not instantaneous magnitude, and relax it at
  the top (C3 covers direction there). Do **not** pin ψ's reversal to the
  hand-top — give a transition window; the lag is player-dependent and is
  itself a coaching metric.
- **Left-handed athletes** → chirality from trajectory, mirrored anatomy
  bounds; add a LH capture to the corpus before v3.0 freezes.
- **NN scope creep** → v3.3 is a keypoint head inside a physics ROI, not
  an end-to-end tracker; anything more waits for corpus evidence.
- **Compute** → ROI-first ordering keeps full-res work bounded; DP grid
  is 1-D per frame (θ), cheap.

## 8. C4 revisited — the ψ-monotonicity transition rail (2026-07-06)

**The gap.** §1's C4 promised a per-frame reachable *cone* collapsing the θ
search from 360° to tens of degrees. The v3.0 as-built (bible §6; impl §4) found
that promise unrealisable — pose φ jumps up to ~87°/frame at the top and through
the blur, so the cone was widened to 150° and switched off at address/finish/top,
and **C3, not C4, carried the search-space collapse**. C4 survived only as a wide
guardrail (an into-forearm veto plus removal of the reverse half-circle). But that
cone is a **magnitude** bound on ψ = θ − φ, and it was the *only* part of the
double-pendulum physics we encoded. The stronger structure was left out.

**The stronger structure (Mark, 2026-07-06).** ψ obeys the same one-reversal law
C3 gives θ, but on the *wrist*: from address to the top ψ cocks **monotonically**
(the interior arm–shaft angle closing ≈180°→≈90°); at transition it reverses
**once**; from transition through impact to the finish it releases
**monotonically**. Re-hinging in the backswing, or un-hinging in the downswing, is
anatomically impossible. Because θ = ψ + φ and the arm φ has its own motion,
"ψ is one-sided per phase" is *strictly stronger* than "θ is one-sided per phase"
(C3): it constrains the shaft's rotation **relative to the measured arm**.
(Convention: signed ψ = θ − φ *increasing* through the backswing is the same
physics as the coach's interior wrist angle *closing*; interior = 180° − |ψ|.)

**The recast: C4 becomes a transition term, not a cone.** Model C4 as a DP
transition cost on Δψ = Δθ − Δφ, structurally parallel to C3's cost on Δθ:

- **Sign-lock (phase-signed):** cocking sign through the backswing, the opposite
  through downswing→finish (both relative to the detected chirality); a
  transition **window** around the top where the sign is free.
- **Rate bound:** |Δψ| ≤ ψ̇_max — a wrist-hinge speed limit (WMAX-class,
  corpus-set, generous enough never to clip a fast release).
- **Smoothness:** a mild quadratic penalty on Δψ (or its curvature).

This drops into the existing Viterbi **with no state-space growth**: ψ_f = θ_f −
φ_f depends only on the DP state θ_f and the *per-frame constant* φ_f, so the
transition (θ_{f−1}, θ_f) → cost still reads only the two adjacent θ states. It
rides on top of C3; it does not replace it.

**Why this succeeds where the cone failed.**

1. **Monotonicity, not magnitude.** The standing warning is "do not tighten the
   cone — it clips real swings" (bible invariant 5). A monotonicity constraint
   never says ψ is *near* any value; it says ψ cannot reverse within a phase. It
   clips only the physically-impossible paths — exactly what we want — and it is
   far more forgiving of φ noise, needing only the *trend* of φ (Δφ over smoothed
   φ), not its instantaneous value.
2. **The arm becomes a witness for the club through the blur.** In the impact
   zone — our thinnest coverage, where the DP free-runs on θ-smoothness across an
   evidence-free gap — the arm is *more* measurable than the club (larger,
   slower, less blurred). A bounded monotone-release rail pins θ = ψ + φ using the
   well-tracked arm precisely where the club is invisible: a qualitatively better
   bridge than "flattest θ path."

**What it demands of φ.** The physics is clean; the noise is entirely in pose φ
(§8 gate-0 below). So the rail must de-spike φ robustly (median + unit-vector
Gaussian as v3.0 already does, plus outlier rejection), lean on the *sign/trend*
of Δψ rather than instantaneous ψ, and **relax at the very top**, where φ is worst
— conveniently the phase where C3 is already strongest.

**The transition is not the hand-top.** ψ's reversal *lags* the hand-top for
players who hold their cock into transition ("lag"). So the ψ-reversal must not be
pinned to C3's top: give it a window and let evidence place it. The lag magnitude
is itself a coaching metric (kinematic sequence).

**Gate-0 — hand-verified on s01 (2026-07-06).** Before any DP change, the law was
checked directly on real data. Mark hand-marked the shaft of corpus swing s01
(`2026-07-05_…_Wrist_02/swing_0001`, 121 labels of grip+head+θ, honest gaps
through the impact blur; artefact `truth.json`), and ψ = θ_markup − φ_pose was
compared against the *independent* v2 fusion truth. Where the two θ sources
overlap they agree to a **median of 0.01°** (p90 3.4°, n = 78). ψ is monotone on
**55/58 backswing steps and 53/56 downswing steps** — and every one of the six
exceptions falls on a pose-φ glitch frame, not a real wrist reversal. The θ
trajectory (which owes nothing to φ) is pristinely monotone with a single reversal
at the top; all residual ψ scatter is pose φ, which jumps a median of 1.6°/frame
but **spikes to 87° on 17 frames** clustered at the top and impact. ψ's peak sits
a few frames *after* the hand-top — the release lag. Figure 1 shows both panels.

![s01 hand markup vs v2 truth: θ single-reversal arc, and ψ = θ − φ as a monotone tent with one reversal.](../research/figures/club_track_psi_s01.png)

***Figure 1.** s01, hand markup vs v2 fusion truth. **Top:** shaft angle θ
(unwrapped) — the two independent witnesses coincide (median 0.01°), a clean
single-reversal arc (C3). The straight segment across f510–545 is the bridge over
the impact-blur gap. **Bottom:** ψ = θ − φ with robustly-smoothed pose φ (green) —
a textbook tent: cocks monotonically to the top, releases monotonically to the
finish, one reversal. Grey dots are ψ with **raw** pose φ, exposing the φ-noise the
rail must survive; red × is ψ from the v2 truth. Vertical lines mark P1–P10; the
dashed line is the hand-top P4 (ψ peaks slightly later — the release lag).*

**Status and cost.** This changes the DP transition bands → the frozen v3.0 track
re-solves → it is **v3.0-internal**, requiring the full protocol (re-gate synth →
s01 → corpus + fixture re-freeze), not an additive companion. It also
**supersedes the blocker on the deferred takeaway-mislabel fix** (impl §4): a
ψ-rail carries the near-still early takeaway (grip barely translates, but ψ is
already evolving and the arm is moving), which is exactly why that fix was parked
behind "explore C4 first."

### 8.1 As-built (2026-07-07): fit, don't penalise — and the law's domain

The transition-rail plan above was built, gated, and then **superseded** by a
cleaner form. Two findings drove the change (Mark).

**1. Monotonicity is truth, so a violation is a *measurement of error* — fit it,
don't penalise it.** A per-frame DP penalty on the sign of Δψ fires on the
pose-φ noise floor: on real data 25–62% of backswing steps show a ≈2–4° apparent
un-cock that is pure estimation noise (visible even on hand-marked θ; the
one-reversal law is a property of the *trend*, not each discrete 1° step). The
penalty had to be scoped release-only to avoid shoving θ off real backswing
ridges, which threw away the law's backswing half. The dual is exact and clean:
**treat monotone ψ as ground truth and FIT it** — per-phase weighted robust
**isotonic regression** (Pool-Adjacent-Violators + Huber-IRLS), run *after* a
pure-C3 DP. The fit's residual `ψ* − ψ_iso` is then a per-frame **φ-error /
confidence map**; a well-measured frame anchors itself (`ψ_iso ≈ ψ*` there, so
`ψ_iso + φ ≈ θ_measured`) so the fit **cannot corrupt a good measurement**; and
in the impact blur, where the shaft is lost, the arm supplies `θ = ψ_iso + φ`
(the witness, now as reconstruction not penalty). Bands are pinned (invariant 1)
and dominate the fit weights; blur-zone non-band ridges are down-weighted so the
fit *interpolates* ψ across the blur from the trusted measurements flanking it
(this is what both bridges the true blur and rejects a ψ-non-monotone
counterfeit). A blur frame whose reconstruction moves it off its own evidence by
> `RECON_TOL` is retiered **`recon`** — an honest arm-witness estimate, excluded
from truth like `pred`. Deterministic (PAVA is exact). See the exemplar bible
for the ported detail.

**2. ψ is a *double* reversal, and the law's domain is address→impact.** The
plan modelled ψ as a single tent — cock to the top, release monotonically to the
finish. The corpus (below) showed that is wrong past impact. Physically ψ cocks
to the top (reversal one), releases to ≈0 at impact (wrists fully un-hinged),
and then the wrists **re-hinge passively under centripetal load** as the arms
decelerate into the follow-through (reversal two); and *between* the two, through
and just past impact, the dominant motion is **forearm rotation about the shaft
axis** — a third rotational DOF a face-on view cannot see (the shaft is axially
symmetric; rolling it does not move the line). Imposing a monotone-release law to
the finish therefore fights a real second reversal *and* a rotation it cannot
represent. The law's clean domain is **address→impact**; the reconciliation is
bounded to the **impact blur** (`RECON_PHASES = ("impact",)` — hinge-valid ∩
shaft-lost), and past impact the shaft evidence, returned sharp, is trusted. The
**third dimension (roll) is not a face-on-shaft state** — it carries zero shaft
signal and does not move θ; it is deferred to the club IMU (full 3-D orientation,
directly), the DTL camera, and the clubhead detector, and the follow-through
ψ-residual is kept as a **roll-onset / release-complete signal**, not a θ
correction.

**Corpus A/B (studio, 10 swings; OFF = v3.0, ON = isotonic).** The first cut
reconstructed the whole release (`RECON_PHASES = ("impact","thru")`). It cut
release ψ-violations **104→35** (every swing improved), held flips 0 and
byte-identical determinism, took down `bad>15` 1→0, and left addr_back/down/
finish medians and the thru median (0.4°) unchanged — **but regressed the
follow-through**: thru p90 **3.9→5.5°** and thru coverage **678→649**, localised
entirely to frames f547–565 (24 of 29 worsened frames were `ray`, all
post-impact), exactly the re-hinge/rotation regime the single-tent law does not
model. That diagnosis motivated finding 2's narrowing to `RECON_PHASES =
("impact",)`.

**Re-gate of the narrowed form (2026-07-07): fixed.** With reconstruction bounded
to the impact blur, the whole pipeline was re-gated — synth `--selftest`, the s01
single-swing A/B, and the 10-swing studio corpus — and the regression is gone
while the impact-blur prize is kept:

| metric (studio corpus, vs v2 truth) | OFF (v3.0) | ON: impact+thru (first cut) | **ON: impact-only (shipped)** |
|---|---|---|---|
| thru p90 θ-error (°) | 3.9 | 5.5 ⚠ | **3.9** ✓ recovered to baseline |
| thru coverage (band+ray / 820) | 678 | 649 | **671** ✓ 22 of 29 back |
| down `bad>15` (impact-blur win) | 1 | 0 | **0** ✓ kept |
| release ψ-violations | 104 | 35 | **84** |
| flips (err>90) / determinism | 0 / — | 0 / identical | **0 / byte-identical** |

The ψ-violation count rising 35→84 is the mechanism working as intended, not a
loss: the first cut's 35 was bought by *flattening the real second reversal and
roll* in the follow-through — the very thing that corrupted thru accuracy. The
impact-only form flattens only the ~20 spurious impact-blur re-hinges (104→84)
and lets the physical follow-through motion stand, recording it in `psi_err` as a
roll-onset signal rather than "correcting" it. `recon` frames fell from up to
13/swing to 0–4/swing — the direct cause of the coverage recovery. On s01 the
rail alters θ on only **two frames** (the impact blur) versus pure v3.0 and adds
`psi_err` on 121 release frames; the v3.0 track is otherwise byte-identical.

## 9. The ball as the club's far-end anchor — address & impact truth from a fixed landmark (2026-07-08)

**The asymmetry this closes.** C1 anchors the *butt* end of the club to the
hands on every frame. Nothing has ever anchored the *head* end — the clubhead is
the one component the tracker infers and never observes, and stage-2's classical
terminus logic failing on 7/10 swings is named as the current honesty bottleneck
(§5.3). PinPoint now has a **reliable v2 ball detector**
([`ball_detector_design.md`](ball_detector_design.md),
[`ball_detection_v2.md`](ball_detection_v2.md)) already wired into the shot
detector. Crucially it **runs per-frame on the same face-on `CameraInstance`, in
the same full-frame-normalized coordinate space as the pose anchor and skeleton
the tracker already consumes** (`ball_detection_v2.md` §4.1a runs the ball
detector on the *same frames* as pose). So it supplies the missing head-end
anchor at zero registration cost — but only at the two instants it physically
can: **address and impact, when the clubhead is presented to the ball.** Those are
exactly the tracker's two weakest zones — the still address (v3.0 abstains, ray
tier off §10; v3.2 reconstructs θ from a scene of look-alikes) and the impact
blur (θ is bridged from the arm, §8.1). The ball converts both from *hardest to
measure* to *read off a fixed point.*

### 9.1 The geometry, and its one honest caveat

At address and at impact the shaft runs from the grip `G` (pose anchor) down to
the clubhead, and the clubhead is **at the stationary ball** `B`
(`ball_detection_v2.md` §4.1a: "the clubhead resting against the ball at
address"). So the line `G→B` is the shaft line:

```
θ_ball(f) = atan2(B_y − G_y,  B_x − G_x)
```

a **direct geometric measurement of shaft angle that needs no ridge or band
evidence at all** — available precisely where E1/E2 are weakest. Three small
offsets separate `G→B` from the exact shaft line, each foldable into an anchor σ
or a corpus-measured correction:

1. **Forward shaft lean** — the hands lead the ball (sign taken from the detected
   chirality, *never hardcoded*, consistent with C3): a few degrees at address,
   larger and more variable at impact (dynamic loft).
2. **Clubhead extent** — `B` is the ball centre; the shaft terminates at the
   hosel, offset by ~one ball radius. Small against the `G–B` baseline (≈ a full
   club length).
3. **Grip-anchor jitter** — already mitigated (hold-median anchor, F10).

**So `θ_ball` is a *soft* anchor, never a hard pin,** with σ covering the lean
until the offset `δ(handedness, club, address|impact)` is measured against
instrumented truth across the corpus and subtracted — a small constant table
(`shaftType` is already plumbed through `ShotAnalysisJob`). But even *uncorrected*
it pins two things exactly: **direction** — `G→B` points *down* to the ball, so
the 180° flip points up into the body and is structurally excluded, the same
guarantee the down-cone gives at address (§14.2) — and **scale** (§9.4).

### 9.2 Exploit 1 — address is "clubhead at the ball," not "grip is slow" (the reported failure)

> **RESOLVED at source (onset v2, 2026-07-09 — swing_span_bounding_plan.md).** The lagging
> grip proxy below is fixed inside `segmentPhases` itself: dual-threshold hysteresis
> walk-back (swing found at 8 px/f, boundary walked back to < 1.5 px/f) + a φ-witness on
> the lead-forearm direction + an impact-anchored clamp ([impact − 1.6 s, impact − 0.55 s])
> that supplies the waggle robustness the walk-back alone lacks — the clamp's impact instant
> comes from the shot trigger, ball-launch-corroborated to ±75 ms. Corpus-gated on the
> 2026-07-09 live-app corpus: onset→impact 917–1017 ms on all six swings (was ~550 ms),
> measured-tier θ elsewhere unchanged (p90 ≤ 1.3°). The ball-tk0 detector below therefore
> reverts to what it was designed to be — the *post-hoc physical refinement* of an already
> honest boundary (its search domain `[0, bs0)` now ends at the true takeaway) — rather
> than the primary fix for a mislabelled one.

The bug, precisely (impl §4 ACTION): `segment_phases` triggers the swing on
**grip** speed (`SW_SPD = 8 px/f`), but through the takeaway the club rotates
about the wrist while the grip barely translates — grip speed is a *lagging*
proxy for club motion. Every frame before the first fast grip run is dumped into
`addr`, so the early takeaway is mislabelled as address (on s01, f≈384–423, which
v2 fusion band-tracked θ 133→191°). This is the reported "it identifies the
takeaway as address."

The ball replaces the lagging proxy with the *physical event*: **address ends at
the last frame the clubhead is at the ball before the sustained takeaway;** `tk0`
is the clubhead's final departure from `B`. Two detectors, both ball-only, no
clubhead detection required:

- **Angular** — the frame at which the measured `θ(f)` first departs `θ_ball`
  beyond a threshold: the club has rotated away while the grip is still.
- **Positional** (when a head estimate exists) — `|head(f) − B| > τ`.

This is **robust to the pre-shot waggle by construction**: a waggle lifts the
clubhead and *returns it to `B`*; the swing does not. The grip-creep walk-back
proposed in impl §4 (`tk0` = walk back over grip-speed creep) can be fooled by a
waggle brushing the creep threshold; a "clubhead departs `B` for good" test cannot
be, because `B` is the fixed point the club leaves only once. **This *is* the impl
§4 takeaway-mislabel fix, grounded on a landmark instead of an `SW_SPD` tuning
constant.** Pair it with the v3.0-r1 ψ-rail, which supplies the near-still
takeaway *rate* the old `WMAX["addr"]=3.0` throttled: the ball fixes the
**labelling** of the boundary, the rail fixes the **DP transition band** past it —
the two harms impl §4 measured (lost coverage *and* a 10–18° lagged track) are
addressed by their respective owners.

### 9.3 Exploit 2 — impact θ and impact instant from ball launch

The v2 detector fires `ballLaunched(ts)` on the launch signature (at-spot response
collapses < 10 % within ~13 ms, `ball_detection_v2.md`), the **vision modality's
impact**, already fused in the shot arbiter with acoustic + IMU. The last
pre-launch frame `f_imp` still has the ball at `B` with the clubhead returning to
it, so `θ(f_imp) ≈ θ_ball(f_imp)`. This gives the §8.1 isotonic bridge a **direct
measured endpoint at its downstream end**, where today there is only
reconstruction (`ψ_iso + φ`): the impact blur is then *bracketed by two real
measurements* — the last pre-blur band lock and the ball-anchored impact — instead
of one endpoint plus a monotone assumption. It also closes a three-modality
coincidence check at a single instant (acoustic `t` / IMU `t` / ball-launch frame
all locate the same impact; the shaft θ there is pinned to `G→B`).

### 9.4 Exploit 3 — two-point pinning ⇒ measured club length, clubhead endpoints, a scale floor

The shaft is a segment `[G, head]`. At address/impact `head ≈ B`, so the segment
is **fully observed at two frames.** That yields:

- **A measured club length in pixels,** `L_px = |B − G|` at address, for *this*
  swing / club / camera / golfer — the scale prior
  [`shaft_detection_skeleton_design.md`](shaft_detection_skeleton_design.md) wanted
  but derived from whole-body *stature*; now read directly off the club at rest.
- **A hard length floor and ceiling.** Confidence → 0 for any shaft shorter than
  ~0.8·`L_px` — the exact "impossibly small shaft" failure the skeleton doc names
  ("confidence should be zero for any shaft shorter than the arm"), now killed by a
  *measured* club length rather than an arm-length surrogate. C1's butt-termination
  caps the near end; `L_px` caps the far end.
- **The clubhead sweep radius** (≈ `L_px` about `G`, image-plane foreshortening
  aside) — a strong prior for the v3.3 heel/toe keypoint head, and **two exact
  clubhead positions (= `B`)** as free weak labels / initialisation / validation
  for that NN and its data flywheel (§5): a head detector must *return to `B`* at
  impact.

> **As built (2026-07-09, Phase A + Phase B — `cbe68cd`/`bd9c47e`/`df76fe9`).**
> This section's `L_px` floor/ceiling was "designed, not built" as of the
> clubhead-length status report; both halves shipped the same session:
>
> - **`L_px` is now measured**, not just theorised — inside `decideTrack`
>   (`shaft_track_assembly.cpp`) over the **late address-hold window** (not the
>   whole pre-takeaway span the first attempt tried and found 107–178 px short
>   from teeing/setup contamination), gated by a **two-pass order-independent
>   cluster check** (component-wise median ball position, accept within 6 px —
>   the chained first-accepted gate let one warm-up lock veto every later good
>   sample) and a **≥5-accepted-sample abstention floor**. A **golf-prior
>   plausibility gate goes beyond this design's §9.6 gates**: it was
>   corpus-motivated by a real mis-lock (the 2026-07-04 session's driver-head
>   lock 130–175 px above the true ball) and rejects any median lock that isn't
>   below the ankle line and between the feet — exactly the "ball always
>   between the feet, below the ankle line" prior this doc already leans on for
>   detection (§9.6), now applied as a **length-measurement sanity check** too.
>   Rejection is honest, not silent: `ShaftDecideTrace.lPxRejected` records
>   which gate failed.
> - **`L_px` is consumed in two places**, not one: (1) as **rung 1 of the
>   4-rung projected-length ladder** (`projectedClubLenPx`) that replaced the
>   flat `0.55·frameH` fallback for every non-band-tier/non-anchored frame; and
>   (2) as the **floor/ceiling/prior for the Stage-2 measured-head pass**
>   (`clubhead_track.{h,cpp}` — annulus ceiling `1.15·L̂`, hard floor
>   `0.8·L_px` at still/impact frames, Gaussian still-frame prior). The design's
>   framing of `L_px` as *a* scale floor undersold it — it is now the top of
>   the length hierarchy, not a backstop.
> - **Exploits 3 and 4 are unchanged**; §9.2/§9.3's takeaway-boundary and
>   impact-θ anchoring stand as designed. **Anchored frames now additionally
>   carry `headPx = ball`**: `applyBallAnchor`'s address-hold and impact frames
>   set `headPx` to `B` and clear `ShaftHeadProjected` — the head *is* the
>   measurement there, not a projection, so the two-point-pinning geometry this
>   section describes is now literally what the track stores at those frames,
>   not just what it infers.
> - **§9.7's prerequisite was satisfied differently than planned.** The design
>   called for an additive `swing.json` ball block feeding the offline
>   reanalyzer. In practice the live path already had `job.ballTrack`, and an
>   offline `BallRunner` replay was added to supply the same track to
>   reanalysis — so `decideTrack`'s `const BallTrack2D* ball` is populated on
>   both paths without a persisted ball stream being a hard requirement for
>   reanalysis (live capture does still write one).

### 9.5 Exploit 4 — the ball discriminates the address look-alikes the cone and mat gates cannot

v3.2's hardest case is the still address crowded with look-alikes; on s01 the
trailing-leg line *outscores* the club per single frame (§14.1) **and** crosses
the mat with the same dark polarity (§14.3 explicitly concludes mat-crossing
*cannot* separate them — "both the club and the trailing-leg line cross the mat
with the right polarity"). The ball adds the discriminator none of the
counterfeits share: **the real shaft points at the ball; the leg line points at
the trail foot,** so `θ_leg` differs from `θ_ball` by a wide margin. Intersect the
two independent address gates — "inside the arm cone" (§14.2, `|θ−φ| < 28°`) ∩
"points at the ball" (`|θ − θ_ball| < small`) — and the admissible set is far
narrower than either alone, excluding the leg the arm-cone must admit (a leg can
sit near-parallel to the arm). **This is precisely the separation §14.3 found the
mat-crossing prior could not provide — now supplied by a landmark.** It also
corroborates the v3.2 stack `θ0` (s01: 88.65°) and provides a fallback address
truth when the stack is inconclusive — *gated on agreement*: require the stack
`θ0` and `θ_ball` to agree within tolerance; on disagreement one of them is a
counterfeit, so **trust neither and abstain** (honesty stance intact).

### 9.6 Honesty gates and failure modes (the ball never degrades θ)

- **No confident stationary ball** — practice swing, ball out of frame / off the
  stance corridor, teed shot the detector misses, or a plain detector miss. The
  anchor is **strictly additive**: fall back to the current phase-model / v3.2
  behaviour; a missing ball can never make the track worse. Same stance as v3.1 /
  v3.2 — additive companions that read the frozen v3.0 track and never rewrite it.
- **False ball** — a white distractor in the corridor (logo, mat scuff, tee cap).
  Rejected by the v2 priors before it may anchor anything (`ball_detection_v2.md`
  §4.1a: *always between the feet, always below the ankle line*, address-stable
  ≥ ~85 % of median across the hold) **and** by the `θ_ball` vs stack-`θ0`
  agreement of §9.5.
- **Unmodelled lean/offset** — soft anchor with σ until `δ` is corpus-measured;
  **wider σ at impact** (lean larger and more variable), where the anchor is only
  allowed to bridge *inside* the blur, i.e. where there is no competing shaft
  evidence to override.
- **Teed ball (driver)** — still a fixed landmark, above the mat; `δ` differs by
  club and is handled by the `shaftType` table.
- **Corpus-gated like every stage (§6):** synth gate (planted ball + planted
  in-corridor counterfeit) → s01 A/B (must fix the takeaway label *and* pin impact
  θ *without moving the good frames*) → 10-swing studio corpus → byte-identical
  determinism. Acceptance: **beat the phase model on address/takeaway boundary
  accuracy and the §8.1 bridge on impact-θ error, with zero regression to the
  moving swing.** ([[single-swing-never-judges-model-accuracy]] — s01 sizes the
  fix, the corpus judges it.)

### 9.7 Prerequisite — record the ball as a swing stream

Today the ball is **signal-only** in the app (`ballPresentChanged` has no
consumer; the offline `"ballTrajectory"` block of `ball_detector_design.md` §5 is
planned, not yet the tracker's input). For the tracker to read `B(f)`:

- **Exemplar side** — `prep_swing.py` emits a `ball.csv` (`found, x, y, r, conf`
  per frame), the direct analogue of the `anchors.csv` / `skeleton.csv` it already
  produces; `address_theta_v3` and the DP read it.
- **App side** — the face-on `CameraInstance` records a **`ball` stream** into the
  `SwingWindow` / an additive `swing.json` block, in the pose coordinate frame it
  already shares, so the live `WristAnalyzer` `ShaftTracker` reads `B(f)` beside
  the grip anchor and skeleton.

This is the deliberately **low-entropy stream** the idea started from — a constant
plus a single step at launch. Its dullness is a feature: a constant-plus-step is
trivially validated and trivially compressed, and the one interesting sample (the
launch step) is the impact anchor of §9.3. Placement mirrors v3.1 / v3.2: read the
frozen window, work only the address and impact spans, additive-merge under v3.0
precedence.

**Net:** the ball is the club's second anchor. C1 pins the butt to the hands every
frame; the ball pins the head to a fixed point at the two frames that matter most
and are hardest to measure. It does not touch the moving swing, where the club is
in free space and the ball is gone — it is an *endpoint* instrument, and the
endpoints (address labelling, impact θ, club scale) are exactly what v3 is weakest
on today.
