# Down-the-line shaft tracker — design

**Status: design, 2026-09-20. Nothing built. One feasibility run made while writing
it (§2), on one swing, judged by eye — it sizes the problem, it does not grade
anything.** Successor context: the corpus audit is
`dtl_shaft_tracking_corpus_assessment.md` (21 named swings, what they can and cannot
grade); the face-on tracker is `club_tracking_v3_design.md` and
`markerless_club_tracker_design.md`; the history of what worked and what did not is
`docs/research/club_detection_from_video.md`. This document assumes all three and
repeats only what it changes.

Premise, from Mark: the first DTL tracker may **depend on the face-on tracker** —
knowing the shaft's orientation from face-on should shrink the DTL search — and a
standalone DTL tracker comes later. The design takes that literally and then asks
what, exactly, face-on knows that DTL can use. The answer turned out to be larger
than "a narrower angle search", and different in kind.

Deliverable of this session: this document. Deliverable of the work it describes: a
DTL shaft track, dark in the app, produced by `swinglab_run` on the 12 taped 07-04
swings and graded against the DTL band lock on the same frames (§6, §7). No DTL
*metric* is in scope — the assessment's §4 stands.

---

## 0. Summary

Five findings drive the design.

1. **The face-on tracker does not transfer, and fails in the way the programme
   exists to prevent.** Pointed at the DTL stream unmodified it reports coverage
   0.77 and VALID, publishes a measured-tier line along the *lead forearm* for the
   whole top of the backswing and along the *trail forearm* through impact — while
   the real shaft is sharp and in plain view — and builds a P-ladder that is wrong
   at P1 and P7 by about 160°. Confidently wrong, Phase 1's disease. §2.

2. **The reason is structural, not tuning: the one-reversal law is a face-on law.**
   Face-on looks along the swing plane's normal, so θ(t) is a clean single-reversal
   arc and that arc carries the search-space collapse (research §8, "C3, not C4").
   DTL looks *along* the plane. The club passes end-on to the camera three times
   before impact — at P2, at the top, at P6 — and at each the projected shaft
   shrinks to a stub, θ is undefined, and whatever long line is near the hands (a
   forearm) wins. There is no monotone θ law to lean on in this view.

3. **What face-on can tell DTL robustly is not the angle. It is the *schedule*.**
   With target-line component `u_x = ρ_F·cos θ_F` from the face-on track, the DTL
   projected length is `ρ_D = √(1 − u_x²)`. On the feasibility swing that one line
   predicts ρ_D = 0.97 / 0.16 / 1.00 / 0.23 / 0.98 / 0.00 / 0.98 at P1…P7, and the
   frames agree at every one: long, stub, long, stub, long, stub, long. Face-on
   knows *when the club is visible down the line and how long it should look*. A
   forearm lock at the top is then not out-scored, it is impossible: the club
   cannot be 300 px long when it is pointing at the lens.

4. **The angle prior is real but has a hole exactly where it is least needed.**
   In the mid-backswing, the face-on (θ, length) predicts the DTL direction to
   5.6° p50 / 10.8° p90 of where the DTL line was actually found (n = 27, one
   swing, reference is the unmodified tracker's on-shaft frames — a sanity check,
   not a grade). At address and impact it fails outright — it predicts vertical
   where the shaft is at 52–55° — because perspective magnification cancels the
   foreshortening it depends on. But at address and impact DTL has its own ball,
   and grip→ball is a direct DTL measurement. The two priors are complementary.

5. **DTL is sighted where face-on is blind.** At impact the clubhead's velocity is
   along the DTL optical axis, so the shaft is a sharp line in the DTL frame at the
   instant it is a 15–20° fan face-on. The research record calls bare-club impact
   "unmeasurable"; from this view it is one of the four best-seen moments of the
   swing. That is the prize that justifies the work, and the reason the coupling
   must be one-directional (§5.10).

The design therefore is: **inherit time from face-on** (phases, impact, the
P-ladder — never re-derived in DTL); **inherit the visibility schedule** (solve only
inside sighted bands, never bridge an end-on gap); **constrain direction softly** by
a face-on corridor whose width is measured, not assumed, with the DTL ball as the
address/impact anchor; **keep the evidence engines and the post-solve segment
probe as built**; and **rewrite the body constraints for this view**, with both
elbows inside the tracker from day one. Band-lock truth in DTL is generated
*without* any face-on input, so the instrument that grades the coupling does not
depend on it.

---

## 1. What the DTL view looks like

All from `2026-07-04_…_Wrist_01/swing_0006`, DTL 512×1024 BayerRG8, 150.7 fps,
frames extracted at the face-on P-ladder instants on the shared clock. Figure 1
pairs each with its face-on frame.

![DTL (left of each pair) and face-on (right) at P1–P10, 07-04 swing_0006.](../research/figures/dtl_faceon_pairs_0704_s0006.jpg)

***Figure 1.** Matched frames. Read the DTL column as a visibility pattern: long
shaft at P1, stub at P2, long at P3, nothing at P4, long at P5, a thin
foreshortened line across the torso at P6, long and sharp at P7, hidden at P8,
visible again above the head at P10.*

### 1.1 Visibility is banded

| Position | DTL appearance | Face-on at the same instant | ρ_D predicted from face-on |
|---|---|---|---|
| P1 address | full length, bands resolved, hands → ball, ~52° below horizontal | long, near vertical | 0.97 |
| P2 shaft parallel | **stub** below the hands, ~40 px | full length, horizontal | 0.16 |
| P3 lead arm parallel | full length, up-left, **head at the top-left corner of the frame** | long, vertical | 1.00 |
| P4 top | **not visible** — end-on, behind the hands and head | full length, horizontal | 0.23 |
| P5 | full length, up-left, moderate blur, bands resolved | long, near vertical | 0.98 |
| P6 shaft parallel | thin foreshortened line **across the torso** | full length, horizontal, blurred | 0.00 |
| P7 impact | full length, **sharp**, bands resolved, head at the ball, ~55° | fan | 0.98 |
| P8 | hands and club **hidden behind the body** | long | 0.63 |
| P10 finish | short section above the head, thin | short, foreshortened | 0.91 |

Four sighted bands before and through impact — address→pre-P2, P2½→P3½, P4½→P5½,
P6½→just past P7 — then an occlusion the geometry does not predict (the golfer's
back turns to the camera and the body covers the hands), then the finish. The
end-on gaps are physics: the shaft is parallel to the target line, which is the
optical axis. The sighted bands are exactly the positions a coach reads from this
view (shaft plane at address, P3, P5, and the return at impact); the gaps are the
positions a coach reads face-on. Nothing is lost that anyone looks at.

### 1.2 The background is the simulator screen

Face-on has a dark room behind the golfer. DTL has the lit screen from roughly
row 280 to row 650 across the whole width: mid-grey, textured, with a HUD panel of
text on the left, a vertical white flag line near x ≈ 405, and — after impact — a
ball-flight animation that redraws the whole thing (Figure 1, P8 and P10 differ
from P1–P7). Below it the mat is lit and carries a white alignment stick lying
along the target line, which images as a **bright, thin, straight, near-vertical
line** at x ≈ 350, rows 740–900, a few tens of pixels from where the clubhead sits
at address and impact. The club's shadow on the mat is a moving dark blob that
tracks the club (Figure 1, P2 and P5).

Consequences, each taken up in §5:

- Over the screen the shaft is mid-grey on mid-grey — the regime the markerless
  work already names: "bare steel over a lit but unclipped mat has no contrast."
  Bands still show; bare steel may not. The motion channel
  (`|frame − sceneMedian|`) is the primary instrument there, not the raw ridge.
- A whole-clip scene median **contains the address club** — the golfer stands at
  address for half the clip — and the club returns to within 3° of that line at
  impact. This is the research record's permanence-snapshot error (§17.2: "contained
  the address club and therefore silently vetoed the whole downswing") waiting to be
  made again. §5.4.
- After impact the median is invalid over the screen.
- The alignment stick, the flag line and the HUD text are static; the shadow is
  not.

### 1.3 Blur is lower where it matters

The assessment inferred it; the frames show it. At P7 the DTL shaft is as sharp as
at address, because the club's speed is along the optical axis. The fastest
*apparent* motion in DTL is through P5½→P6½, where the projected shaft is
collapsing anyway. The blur wedge engine (E3) is expected to be idle in this view;
that is a measurement for Stage 0, not an assumption.

### 1.4 Framing

07-04 DTL is 512 px wide and the club reaches the top-left corner at P3. The
segment probe's image-edge guard (a run ending within 25 px of the edge has no
terminus) and `ShaftHeadOffFrame` both apply as built; direction is unaffected.

---

## 2. The transfer experiment

The assessment's open question 3 — "does the face-on tracker transfer at all?" —
was costed as cheap once stream selection landed. It is cheaper than that: the
tracker keys off `pose.camera` (`shaft_tracker.cpp:118-134`), and
`swinglab_run --face-on <alias substring>` already lets the tool name any stream as
the face-on one. So:

```
swinglab_run <07-04 swing_0006> --out <run> --face-on DTL --trace     # 13.6 s wall
```

runs the entire production pipeline on the DTL stream as though it were face-on.
Binary of 18 Sept; no code changed.

| | face-on (recorded) | same pipeline on DTL |
|---|---|---|
| pose | — | 215 frames posed, **209 with both wrists > 0.3** |
| measured samples / 745 | 419 (+13 wedge) | **118** (+2 wedge) |
| coasted | 312 | **624** |
| `coverage`, `valid` | 0.995, true | **0.769, true** |
| address ball length (`ballPx`) | 260 px | none (−1) |
| P1 θ | 104° | **215°** (the shaft is at ~52°) |
| P7 θ | 103° | **214°** (the shaft is at ~55°) |
| swing-plane "delta" | 0.1° | 29° |

![The unmodified face-on tracker on the DTL stream.](../research/figures/dtl_transfer_0704_s0006.jpg)

***Figure 2.** 14 DTL frames, address to finish. Yellow dot = pose grip anchor;
green = a sample the tracker flags **measured**, grey = coasted; the two thin lines
are the face-on prediction of §4 under each sign of the unknown depth component,
drawn at the predicted projected length. Top row, frames 3–4: measured and on the
shaft. Top row, frames 6–7 (top of the backswing): **measured, and lying along the
lead forearm**, while the prediction lines are short. Bottom row, frames 3–4
(impact): **measured, pointing up-left across the trail forearm and hip**, with
the real shaft sharp and unclaimed below the hands.*

What the run says, by eye on one swing:

1. **The pose anchor transfers.** The grip dot sits on the hands in every frame
   from address through impact. It is lost only from about P8, where the hands are
   behind the body and the estimator invents them near the torso's centre.
   The wholebody model's hand confidence is known not to be trustworthy, and it
   will not say so.
2. **The evidence engines transfer.** Where the club is long and the body is not
   competing (mid-backswing), the line is found and sits on the shaft; E1 locked
   (`bandPx` 185).
3. **The decision layer does not.** Two of the four pre-finish sighted bands are
   confidently wrong. The arm at the top is the FO daylight-session counterfeit
   ("a parallel bright ridge … with the same line confidence") promoted from a
   tail to the main event, because here the true shaft is *absent* and the arm is
   the only long line from the hands. At impact the solve, holding downswing
   kinematics that do not exist in this projection, arrives pointing the wrong way
   and the measured tier follows it.
4. **The phase model does not.** The hands-only model is tuned on the face-on hand
   path; on the DTL path it produced a ladder ~330 ms late at P2 with P3 and P5
   missing. The DTL tracker should not own a phase model at all while a face-on
   one exists on the same clock.
5. **The ball prior does not.** "Between the feet, below the ankle line" is a
   face-on sentence. In DTL the ball is a stance-width to the *right* of both feet.

Nothing here is a surprise after §1; the value of running it is that each failure
is now a named regression case with a frame number, which is how every guard in the
face-on tracker earned its place.

---

## 3. What carries over, and what does not

The research record's most durable lessons, and their disposition in this view.

| Learned face-on | Disposition for DTL |
|---|---|
| **Honesty by discrimination, not abstention** (§17.1) | Kept, and it is why the visibility schedule matters: "the club cannot be long when face-on says it is end-on" discriminates the forearm from the shaft *inside* the frames where both are plausible lines. The end-on gaps themselves are honest absence by geometry — a hole, not an abstention. |
| **One global solve over per-frame picks** (Phase 5) | Kept, but **per sighted band**, not across the clip. A global path across an end-on gap is the "flatter wrong branch across an evidence-free gap" failure by construction; in DTL θ is *undefined* in the gap, so there is nothing to bridge. |
| **C1 attachment** | Kept unchanged. The club starts at the hands in every view. |
| **C2 free space** | **Rewritten.** In DTL the shaft legitimately crosses the torso (P6) and the thighs (address, impact). The face-on schedule must not be ported; §5.6. |
| **C3 one reversal** | **Does not exist on θ_D.** It is inherited *through* the face-on coupling: θ_F obeys it, and θ_D is constrained relative to θ_F. §5.7. |
| **C4 arm coupling / ψ rail** | **Dropped.** ψ = θ − φ is the wrist angle only in the plane face-on sees. In DTL the forearm–shaft image angle mixes cock, plane and roll. The into-forearm veto survives (it is geometry, not anatomy) and is extended to the trail forearm. |
| **The cone failed because pose φ is noisy** (research §8) | Heeded: the face-on corridor is conditioned on the *face-on track's tier*, not on pose; it is wide; and it is a cost, not a gate. |
| **Strong evidence must anchor the solve** (band well) | Kept for DTL band locks — DTL's own, never face-on's. |
| **"A prior that had quietly become a pin"** (Phase 12) | The face-on corridor is never a well and never publishes a frame. A DTL sample is measured only on DTL evidence; §5.9. |
| **Locate the segment *after* the solve, along its direction** (Phase 13) | Kept as built. No pre-solve pins from 1-D profiles. |
| **The pose anchor is 39 px off the shaft axis; re-register the line** | Kept (`shaft.snap.*`), and the offset re-measured for this view in Stage 0. |
| **The snap's one tail needs the elbow in the tracker** | Not deferred a second time. Both elbows are passed to the DTL decision layer from the first build. |
| **Probe address toward the ball, not along a clamp** | Kept, with a DTL ball (§5.5). |
| **Band frames are left alone by the snap** | Kept. |
| **Measure before designing** (P0 probe); **the band lock is the yardstick, hand marks cannot see 3°** | Stage 0 is a probe; every result is reported beside the DTL band lock on the same frames. |
| **Read a tail per mark before calling it a regression** | Grading reports per-band, per-frame diffs against the previous config. |
| **A diagnostic that re-executes the pipeline is not observing it** | The DTL trace reuses the run's own pose and the run's own face-on track. One inference, one solve. |
| **Corpus club labels lie; declared fps lies** | Classify by evidence; time from `t_us`. |
| **Pose is non-deterministic across hosts; pin it for A/B** | DTL pose is cached per stream (assessment §5.2) and pinned for every comparison. |

---

## 4. Two-view geometry — what face-on knows

### 4.1 Frame and equations

World axes: **X** along the target line toward the target, **Z** up, **Y** from the
ball toward the golfer (away from the face-on camera). Shaft unit vector
`u = (u_x, u_y, u_z)`, butt to head. Image angles are atan2 in pixel coordinates,
y down, as the tracker stores them.

Idealise both cameras as level, orthographic and orthogonal (face-on looks along
+Y, DTL along +X; for a right-handed golfer image-right is +X face-on and −Y in
DTL). Then with ρ the projected length as a fraction of the true length:

```
face-on:   ρ_F (cos θ_F, sin θ_F) = ( u_x, −u_z)
DTL:       ρ_D (cos θ_D, sin θ_D) = (−u_y, −u_z)

u_x = ρ_F cos θ_F          u_z = −ρ_F sin θ_F          |u_y| = √(1 − ρ_F²)

(a)  ρ_D  = √(1 − u_x²)                       projected DTL length   — from θ_F, ρ_F
(b)  sign(sin θ_D) = sign(sin θ_F)            up or down is shared   — from θ_F alone
(c)  θ_D  = atan2(ρ_F sin θ_F, ∓√(1 − ρ_F²))  direction, two-valued  — needs ρ_F well
(d)  ρ_F² + ρ_D² = 1 + u_z²                   a consistency identity — testable on bands
```

Handedness flips the sign of X in the face-on image; take it from the chirality the
face-on tracker already detects, never from a setting.

### 4.2 How well-conditioned each one is

**(a) is robust.** It depends on `u_x = ρ_F cos θ_F`. Where it matters — deciding
between "full length" and "stub" — the face-on shaft is near horizontal and long,
cos θ_F ≈ ±1, and a 10% error in ρ_F moves ρ_D from 0.0 to 0.44 at worst: still a
stub. Feasibility swing: 0.97 / 0.16 / 1.00 / 0.23 / 0.98 / 0.00 / 0.98 / 0.63 /
0.91 at P1–P8, P10, against Figure 1's long / stub / long / gone / long / thin line
/ long / (occluded) / short. Seven for seven before the occlusion.

**(b) is robust** wherever face-on is measured. It halves the circle and makes the
180° flip structurally impossible in DTL for the same reason the one-reversal law
does face-on — by inheritance.

**(c) is conditional.** `|u_y| = √(1 − ρ_F²)` is ill-conditioned as ρ_F → 1, and
face-on ρ_F is the weakest thing the face-on tracker publishes (head p50 21–26 px
on ~290). Worse, the rig is not orthographic: at address the clubhead is ~0.5 m
nearer the face-on camera than the hands, perspective magnifies it, and the
recorded face-on length at address (299 px) is *longer* than at P2 (292 px) where
the shaft truly is in the image plane. (c) then returns |u_y| = 0 and predicts a
vertical DTL shaft; the real one is at 52°. Measured on the feasibility swing:

| band | (c) vs where the DTL line was found |
|---|---|
| mid-backswing, 2.83–3.00 s, 27 frames | **5.6° p50, 10.8° p90**, max 24.5° (sign +, club behind the hands) |
| address, impact | **~35–40° wrong** — predicts ~90°, shaft at 52–55° |
| top, P6 | not applicable — ρ_D < 0.25, θ_D undefined |

By eye on four gridded frames (±3°): P1 61° predicted with a true ρ_F of 0.88
substituted vs 52° seen; P3 −122° vs −113°; P5 −118° vs −120°; P7 61° vs 55.5°.
So with an honest ρ_F the idealised model is good to ~10°; the residual is the
camera's pitch and its offset from the hand line (assessment §2.2), and it is
systematic, which means a fitted camera model can remove most of it (§4.4).

**The sign in (c)** is the side of the face-on image plane the head is on. Golf
fixes it by phase — toward the ball at address and impact, behind the hands
through the backswing and downswing — and the face-on plane fit
(`shaft_plane.h`: "a plane fixes which side of the node line every direction is
on") states the same thing as a model. The prototype does not hardcode the
schedule: it offers both signs and records which the evidence takes, per phase,
over the dev set. If golf is right the table will be boring, and then it can be
hardcoded.

### 4.3 The second anchor: DTL's own ball

At address and impact the head is at the ball in every view. `θ_ball,D =
atan2(B_y − G_y, B_x − G_x)` in DTL pixels is a direct measurement needing no
face-on input at all — the v3 §9 far-end anchor, re-seated in this view, with the
same soft-anchor stance and the same ~3° lean caveat. It covers precisely the two
bands where (c) fails. The golf prior that validates the ball changes: in DTL the
ball is **beyond the toe line on the side the golfer faces, below the ankle line,
and address-stable**; it is not between the feet.

### 4.4 Self-calibrating the second camera (optional, measured first)

Replace the idealised DTL camera with a weak-perspective one: three rotation
parameters and a scale ratio, per rig sub-group. On the taped club both views
produce a band lock with its own `s` on hundreds of frames per swing, so each such
frame gives four numbers (two image vectors) for two unknowns (u on the sphere);
the four camera parameters are overdetermined by a small bundle adjustment, and
identity (d) is the residual to watch. This is *not* the camera calibration thread
(`camera_calibration_design.md`) and produces nothing metric; it produces a better
corridor centre. It is built only if Stage 0 shows the idealised corridor is too
wide to be useful (§7).

### 4.5 Time

The two streams share the host clock (`t_us` in one window timebase) but are not
frame-synchronous: DTL frames lead the nearest face-on frame by a median 3.2 ms
(half a frame). At 2,000–3,000°/s face-on that is 6–10° of θ_F. The face-on track
is therefore **interpolated to each DTL frame's `t_us`** (unwrapped θ, using the
recorded `thetaDot`), never looked up by index. These July streams are
host-stamped (the PC clock at handling, ±0.4 ms jitter), so a constant inter-camera
latency bias is possible; Stage 0 measures it by cross-correlating the grip's
image row in the two views, which both cameras see (§5.2), and the offset becomes
a per-session constant.

---

## 5. Design

### 5.1 Shape

```
face-on pipeline (unchanged)  ──►  FaceOnWitness{ t → θ_F, ρ_F, tier, phase, P-ladder, impact, chirality }
                                          │   read-only, interpolated to DTL t_us
DTL stream ─► PoseRunner(DTL) ─► anchors  ▼
          ─► clean plate (phase-aware) ─► E2 raw + motion, E1 bands      (as built)
          ─► visibility schedule ρ̂_D(t) ─► sighted bands
          ─► per band: emission (E2/E1 + DTL constraints + corridor cost) ─► Viterbi
          ─► post-solve: segment probe, snap (band frames excluded)        (as built)
          ─► tiering ─► DtlShaftTrack2D   (own product; never read by face-on)
```

A new orchestrator, `DtlShaftTracker`, beside `ShaftTracker`, sharing
`shaft_tracker_math.*` (E1, E2, `rayProfile`, `segmentLock`) and the Viterbi and
snap from `shaft_track_assembly.*` through the seams they already have. It does
**not** call `decideTrack()`: that function *is* the face-on decision layer — phase
model, C2 schedule, ψ reconcile, the `verifiable` clause — and §2 is what it does
here. `ShaftTracker` is not touched; face-on output must stay byte-identical with
the DTL tracker on or off, and that is a gate.

### 5.2 Anchors, and a cross-view check on them

DTL pose (same ViTPose wholebody, DTL stream, cached under a stream key) supplies
the grip anchor, **both forearms (elbow→wrist, lead and trail)**, and the body
joints. One cross-view witness is free: both cameras see vertical, so the grip's
image row in DTL is an affine function of its row face-on, `y_D ≈ a·y_F + b`,
fitted per swing over the address-to-impact frames. A DTL anchor whose row
disagrees by more than the fit's p95 residual is quarantined — marked unanchored,
its frame unsolved. This is how the post-impact invented hands are caught without
trusting hand confidence. Hands remain a cross-check, never a measurement.

### 5.3 Time is inherited

Phases, the P-ladder, the impact instant and chirality come from the face-on
analysis of the same swing. The DTL tracker has no phase model. Reasons: the
face-on ladder is the one the research programme spent Phases 12 and 13 making
honest; the hands-only model demonstrably mis-segments on the DTL hand path (§2);
and a second, independent ladder would give every downstream consumer two answers
to "when was P6". 06-11 has no `capture.impactUs`, but it has a face-on analysis
with an impact; the DTL side needs nothing more.

### 5.4 The clean plate is phase-aware

Build the DTL background per region from frames in which the club is known — from
the face-on ladder — to be elsewhere:

| region | built from | valid |
|---|---|---|
| hands-to-ball corridor (rows ≳ 500, the address/impact zone) | median over P2½→P5½ (club above the waist) | address band, impact band |
| upper frame (rows ≲ 500) | median over the address hold | backswing and downswing bands |
| screen rows, after impact + 100 ms | none — raw channel only | finish |

The alignment stick, flag line and HUD then vanish from the motion channel and the
address club does not. This is the one place the face-on coupling changes the
*evidence* rather than the decision, and it is only a choice of which frames enter
a median.

### 5.5 Evidence engines

As built, with three differences.

- **E2** runs on raw and motion channels as today. Over the screen rows the motion
  channel is expected to carry the bare shaft; Stage 0 measures steel evidence by
  background regime (dark ceiling / screen / lit mat / trousers) the way the P0
  probe did face-on.
- **E1** runs unchanged with the club record's band centres. `s` bounds are
  re-derived for the DTL scale (Stage 0: ~1.07× the face-on px/mm on 07-04).
- **The DTL ball.** Prototype: a static bright blob inside the DTL ball prior
  (§4.3), median over the late address hold, with the as-built cluster check and
  ≥ 5-sample abstention floor. The production ball detector's DTL calibration is
  out of scope; `setup.ballDetection.calibrated` is false on every corpus stream.
- **E3 wedge** off until Stage 0 shows a frame that needs it.

### 5.6 Constraints for this view

| | Rule |
|---|---|
| **D1 attachment** | C1 unchanged: evidence must terminate within `r0 ≤ 260 mm` behind the anchor; reverse-ray test as built. |
| **D2 forearm vetoes** | A candidate within 12° of *either* forearm's direction **and** whose ray passes within `armLatPx` of that elbow is vetoed. Distance-to-elbow is the test the research record found the counterfeit fails and the shaft does not. Both arms, all phases. |
| **D3 length consistency** | The visibility law as a per-frame discriminator: a candidate whose evidenced run `rEnd` exceeds `1.25 × ρ̂_D × L̂_D` costs `wLen`; in a frame with ρ̂_D < `rhoSolveMin` (0.35) nothing is solved at all. `L̂_D` is the DTL full length: address grip→ball over ρ̂_D(address), then band/segment `s`, the as-built ladder. One-sided — a short run is always allowed (occlusion, dim steel), the markerless work's length-gate lesson. |
| **D4 half-plane** | (b): candidates on the wrong side of horizontal cost `wHalf` while the face-on tier is measured and `|sin θ_F| > 0.25`. |
| **D5 corridor** | §5.7. |
| **D6 ball anchor** | soft θ anchor at address hold and the impact frame, σ as face-on; additive, absent ball ⇒ no-op. |
| body overlap | **no C2.** Body evidence is admitted in every phase. The counterfeits C2 caught face-on (creases, leg lines) are caught here by D3 and D6: at address and impact the true shaft points at the ball and has the predicted length; the trouser line does neither. If Stage 3 shows a body counterfeit surviving, it gets adjudicated and a rule, in that order. |

### 5.7 The face-on corridor

Per DTL frame `t`, from the interpolated face-on witness:

```
centre(s)   θ̂_D± from (c), both signs, unless the sign table has settled the phase
half-width  w(t) = w0(band)  ⊕  ∂θ_D/∂ρ_F · σ_ρ  ⊕  ∂θ_D/∂θ_F · σ_θF(tier)
cost        0 inside; quadratic to wCorr (≈ wE2/2) outside; saturating
gate        OFF when face-on tier ∈ {PRED, RECON, Coasted, Implausible}
            OFF when ρ_F > rhoFMax (0.93) — (c) is degenerate; D6 or nothing
            OFF in the finish
```

`w0` per band is **set from Stage 0's measured residual** (p99 of corridor-centre
vs DTL band-lock θ on the dev swings), not chosen here. Expectation from §4.2:
about ±25° in the two mid bands, i.e. a 7× reduction of the circle on top of D4's
2×. The corridor's cost ceiling is deliberately below E2's weight: clean evidence
outside the corridor wins and is *logged* (`trace->corridorEscape`), because an
escape is either an off-plane swing — the thing a coach wants to see — or a
face-on error, and both are worth a frame number. A corridor that silently wins
every argument is a pin.

### 5.8 The solve

State: θ_D on the 1° grid. One Viterbi per sighted band (maximal run of frames
with ρ̂_D ≥ `rhoSolveMin`, a quarantine-free anchor, and inside the inherited
swing span). Emission = `wE2·(1 − ev)` + D1–D6, DTL band well last, as built.
Transition: quadratic smoothness with a rate bound `ω_max(t) = ω_base / ρ̂_D(t)`
— θ_D legitimately moves fast as the projection shortens toward a gap, so a fixed
per-phase rate (the face-on kernel) would either clip the band edges or be loose
everywhere else. No phase-signed direction term: the sign of dθ_D/dt is not a law
in this view. No ψ reconcile. Bands are solved independently; nothing connects
them, and no sample is emitted in a gap.

**Considered and deferred:** solving for the out-of-plane angle η (with
`u_y = sin η`) instead of θ_D. η is the physically smooth coordinate and is what a
standalone or fused tracker should eventually estimate, but it bakes the camera
model into the state and makes the published DTL angle a function of face-on's.
For the prototype the published quantity stays a DTL image measurement; η is
derived afterwards for inspection (§9).

### 5.9 Tiering and honesty

| tier | condition | published |
|---|---|---|
| BAND | DTL E1 lock within `bandTol` of the solve | θ, s, head |
| SEG | post-solve segment lock along the solve direction | θ, s, terminus |
| RAY | E2 `EV ≥ 0.45`, beats reverse ray 1.15×, `SUP ≥ 0.4`, passes D2 and D3 | θ |
| END-ON | ρ̂_D < `rhoSolveMin` | nothing — absent by geometry, reason recorded |
| OCCLUDED | anchor quarantined (§5.2) | nothing, reason recorded |
| UNSEEN | in a sighted band, no tier met | nothing |

No PRED tier in the prototype: there is no kinematic model of θ_D to predict with,
and inventing one is how §2's impact frames happened. **A frame is never measured
on face-on's word**: the corridor shapes the search, the tier is earned from DTL
pixels alone. `coverage` is reported over sighted-band frames only, and beside it
the fraction of the swing span that is sighted at all — two numbers, so a 0.95
cannot hide that 40% of the swing was end-on.

### 5.10 One direction only

Face-on conditions the DTL *search*. The DTL track never feeds the face-on
tracker, the face-on plane fit, or any face-on metric in this work. This is the
research record's IMU firewall applied to a second camera, for the same reason:
DTL's long-term value is as the witness that owes nothing to face-on — sighted at
impact where face-on is a fan, and the only view that sees the depth component
face-on's foreshortening guesses at. A witness that has been fitted to the thing it
testifies about is not one. Fusion is a later document.

### 5.11 Output and persistence

`DtlShaftTrack2D` — same sample shape as `ShaftTrack2D` plus per-sample `tierD`,
`rhoPred`, `corridorCentre/Width`, `corridorEscape`. Written to
`analysis.clubDtl` by `swinglab_run` only. Nothing in the app reads it; no UI, no
wizard, no metrics. Stage version `kDtlShaftStageVersion = 1` in
`analysis_versions.h` from the first write, so the first change re-analyses.

---

## 6. Truth, and how results are reported

**The instrument is the DTL band lock, generated without face-on.** E1 on the DTL
stream of the 12 taped swings, unconditioned — no corridor, no schedule, no
inherited phases — accepted per frame under E1's own gates, then adjudicated by
montage at full resolution before it is trusted (research rule 4; and §17.2, "truth
generators must be validated adversarially"). It will exist only where bands
resolve, which by §1.1 is the sighted bands. That is sufficient: those are the only
frames the tracker claims.

**Second referee: Mark's hand marks**, sparse, on the dev six — grip and head at
the face-on ladder's P1, P3, P5, P7 and P10 instants, where DTL is sighted. They
are too coarse to see a 3° defect and are not asked to; they are there to catch
the instrument being wrong in kind.

Every result is a table with these columns, per sighted band, never pooled:

| column | why |
|---|---|
| frames in band / frames published, by tier | coverage, honestly denominated |
| θ vs DTL band lock, p50 / p90 / % > 15° | the 0.3° yardstick |
| **confidently wrong**: published frames > 15° from band lock or mark | the one unacceptable state; target 0 |
| corridor residual (centre vs band lock), p50 / p99 | sizes `w0`; tests §4 |
| corridor escapes, count and adjudication | is the prior a pin? |
| same run with the corridor **off** (schedule and D3 kept) | what the angle prior buys |
| same run with the schedule **off** too | what face-on buys in total |
| published frames in END-ON/OCCLUDED spans | must be 0 |

The two ablation rows are the design's own test. If the corridor-off row matches
the corridor-on row, the angle prior is dead weight and should be removed before
it costs anything; §0 finding 3 predicts most of the gain is in the schedule.

**Gates** for "the prototype works", on the held-out six (07-04 s0010–s0015), run
once: zero confidently-wrong published frames; θ vs band lock p50 ≤ 1.5°, p90 ≤ 5°
in each of the four pre-finish bands; sighted-band coverage ≥ 0.80 in the address,
mid-backswing and impact bands, ≥ 0.60 in the downswing band; zero published
frames in END-ON spans; face-on `result.json` byte-identical with the DTL tracker
on and off; deterministic re-run. **Transfer** (06-11, nine bare-shaft swings, run
once, last): no band lock exists, so the report is tier mix and sighted coverage
beside 07-04's, plus hand marks on three swings — a finding, not a gate.

---

## 7. Plan

Build economy applies: one build per stage, affected test targets only, once.
GOLFSIMPC is not needed until a full 12-swing sweep; a swing runs in ~14 s on the
Mac.

| Stage | Deliverable | Done when |
|---|---|---|
| **0 — measure** | `tools/shaftlab/dtl_probe.py`, no C++ change. Inputs: recorded face-on `analysis.club` + a `--face-on DTL --trace` run per swing for DTL pose anchors and E1/E2 evidence. Outputs to `docs/research/data/dtl/`: (i) inter-camera clock offset per session from grip-row cross-correlation; (ii) DTL pose anchor continuity and its offset from the band-locked shaft axis; (iii) ρ̂_D schedule vs observed evidenced run length; (iv) corridor residual vs unconditioned DTL band lock, idealised model, both signs → the sign table and `w0`; (v) steel evidence by background regime; (vi) identity (d) residual | a results note with the six tables on the dev six; **decision recorded**: idealised corridor, fitted camera (§4.4), or no corridor |
| **1 — plumbing** | assessment §5 items 1–2: `CameraPlacement::DownTheLine` populated (perspective name, alias fallback for 06-11), the never-populated invariant in `analysis_stage_test.cpp` retired, stream-keyed pose cache; `swinglab_run --dtl` runs face-on as today, then poses the DTL stream | face-on output byte-identical on the 61 pinned swings; DTL pose cached for the 21 |
| **2 — tracker v0** | `DtlShaftTracker`: witness interpolation, anchor quarantine, phase-aware clean plate, schedule, per-band solve with D1–D4 + D6, tiering, trace; corridor behind `shaft.dtl.corridor.enabled` (default from Stage 0); unit tests on synthetic two-view swings with a planted forearm at the top and a planted alignment stick | dev-six table of §6 produced; every §2 failure frame now END-ON or correct |
| **3 — adjudicate and iterate** | montage review of every confidently-wrong and every corridor-escape frame on the dev six; rules added only against adjudicated counterfeits | dev six meets the §6 gates |
| **4 — held-out** | 07-04 s0010–s0015, run once | gates met or the misses written up; results to Mark **before** any commit of tuned defaults |
| **5 — transfer** | 06-11 bare wedge, run once | finding recorded; research paper gains the DTL phase |
| **6 — standalone** | §9 | separate design |

Stage 0 and Stage 1 are independent and can run in parallel.

---

## 8. Risks and open questions

- **One golfer, one rig, twelve swings, and a camera in the wrong place.** Every
  width and threshold fitted here is a property of this rig. The design's
  *structure* (schedule, bands, inherited time) does not depend on placement; its
  numbers do. A correctly placed DTL camera changes the sighted bands' edges and
  should narrow the corridor residual. Nothing here should be frozen as a default
  until the validation capture exists.
- **Face-on errors propagate.** A face-on phase-model collapse (research §16,
  "What was under it") would hand DTL a wrong schedule. Mitigation: the
  schedule is cross-checked against DTL's own evidence — if E2 finds a strong,
  attached, long run in a span the schedule calls END-ON, the swing is flagged
  `scheduleConflict` and the DTL track is withheld. That check is also the seed
  of the standalone tracker.
- **The ρ̂_D schedule near band edges.** Between ρ̂_D 0.35 and 0.6 the projected
  shaft is short, the forearms are long, and D3's one-sidedness admits a short
  true run and a short piece of forearm alike. D2 is what separates them; if it
  does not, raise `rhoSolveMin` and accept narrower bands. Coverage is the thing
  the programme trades.
- **Bare steel over the screen.** May simply have no contrast in the raw channel;
  the motion channel depends on the clean plate being right. If 06-11 shows a hole
  over the screen rows it is a lighting/background finding, not an algorithm one.
- **The occlusion after impact is not predicted by geometry.** It is caught only
  by the anchor quarantine. If the quarantine leaks, the first symptom will be
  published frames between P8 and P9; the report has a row for it.
- **Inter-camera latency** may not be constant on host-stamped streams. If Stage 0
  finds it drifting within a session, the corridor widens by `ω_F · jitter` and
  impact-band claims weaken accordingly.
- **Open — for Mark:** should the sparse DTL hand marks be made before Stage 2
  (so the instrument is cross-checked before anything is tuned against it) or
  after the first montage shows where the band lock is doubtful? The design
  assumes before, on the dev six only.

---

## 9. The path to standalone

Each thing face-on supplies has a DTL-native replacement, and the coupled tracker
is what generates the data to build them:

| Supplied by face-on now | Standalone replacement | What the coupled runs leave behind for it |
|---|---|---|
| phases, impact, ladder | a DTL hands-path phase model (the hands' *vertical* trajectory is common to both views) plus DTL ball launch | per-swing DTL hand paths with a trusted ladder attached |
| visibility schedule ρ̂_D | DTL's own evidence: evidenced run length from the hands collapsing and recovering — the `scheduleConflict` check of §8 turned round | the measured ρ_D(t) profile per swing, which is nearly the same curve for every swing of one golfer |
| corridor | a per-golfer DTL shape model θ_D(phase) learned from coupled sessions — the DTL analogue of the corpus shape model | the published θ_D tracks themselves |
| half-plane | hands above/below the shoulders, from DTL pose | — |
| — | the η-state solve of §5.8, once a fitted camera exists | identity (d) residuals |

Standalone is therefore not a rewrite: it is the same tracker with
`FaceOnWitness` filled from DTL-side estimators, gated against the coupled
tracker's output on the same swings.

---

## 10. Deliberately not done

- **DTL metrics** — plane, path, anything in degrees a golfer would read. The
  camera is off the hand line and above hand height (assessment §4).
- **Stereo lift and metric 3-D.** §4.4 fits a projection to improve a prior; it
  does not calibrate anything.
- **Feeding DTL back into face-on**, including the tempting one: DTL's sharp
  impact-frame θ as the face-on bridge's endpoint. That is fusion, it breaks the
  firewall, and it waits for a camera in the right place.
- **A learned detector.** Same ruling as face-on.
- **App surface.** No capture, wizard, UI or settings change. The tracker is
  reachable from `swinglab_run` only.
- **07-03.** The golfer is out of frame; no algorithm recovers that.

---

## Appendix — provenance

All on 2026-09-20, macOS, `build/tools-parity-ninja/swinglab_run` built 18 Sept,
corpus at `/mnt/swingdata/corpus/swings`, swing
`2026-07-04_Mark-Liversedge_Wrist_01/swing_0006`.

- **Frames** — ffmpeg `select=eq(n,i)`, `i` = nearest `streams[].frames.t_us` to
  each `analysis.club.positions[].t_us` of the recorded face-on analysis.
- **Transfer run** — `swinglab_run <swing> --out <scratch> --face-on DTL --trace`;
  table values from its `result.json` `analysis.club` and the run log. Sample
  tiers from `flags & ShaftMeasured`.
- **ρ̂_D and the corridor centre** — §4.1 (a) and (c) evaluated on the recorded
  face-on `samples` (nearest sample, **not** interpolated — a Stage 0 refinement),
  with `L_full` = 295 px taken as the face-on length at P2/P6. The 27-frame
  residual is against the transfer run's measured-flag samples in 2.82–3.00 s,
  which Figure 2 shows on the shaft in two frames; it is not truth.
- **By-eye angles** — read off a 64 px grid on gamma-lifted native-resolution DTL
  frames; ±3° at best.
- **Frame offset** — DTL `t_us` minus nearest face-on `t_us`, median −3,223 µs,
  frame interval 6,702 µs.

One swing. Every number above is a reason to build Stage 0, and none is a result.
