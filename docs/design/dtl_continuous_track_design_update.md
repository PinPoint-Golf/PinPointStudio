# The down-the-line club track: why it is sporadic, why it cannot synthesise itself, and what makes it continuous

**Status:** design update, 30 Sept 2026; §3.2a added 1 Oct 2026 after §3.1 shipped. Seeds the two-camera protocol session
(`docs/validation/two_camera_capture_protocol.md`). Supersedes nothing yet: the DTL tracker's
"never bridge an end-on gap" rule (`dtl_shaft_tracker_design.md` §5.8–5.9) stays for what the
tracker *measures*; this note is about what the product *draws and computes* in the gaps, and what
the clean session must give us for that to be honest.

## 1. What Mark sees, in numbers

From today's corpus runs (`/mnt/swingdata/scratch/robust-20261001/corpus_v11`, the persisted
`clubDtl` block), on the two usable two-camera sessions:

| swing | frames | RAY (published) | END_ON | OCCLUDED | UNSEEN | sighted fraction | fused 3-D frames |
|---|---|---|---|---|---|---|---|
| 07-04 s7 (taped 7-iron) | 528 | 180 | 74 | 179 | 95 | 0.45 | 90 |
| 06-11 s1 (bare wedge) | 465 | 197 | 28 | 138 | 102 | 0.63 | 103 |

Laid out in time on 07-04 s7 (100 ms buckets from the DTL window start): RAY from the address
hold through P3, **END_ON at the top (3.18–3.28 s)**, RAY through P5–P6, UNSEEN at impact,
**END_ON just after impact (3.58–3.78 s)**, RAY briefly, then **OCCLUDED from 3.98 s to the end**.
Coverage inside the bands the tracker does open: address 1.00, P2.3→P3.7 0.68, P4.7→P5.7 0.65,
P6.4→P7.8 1.00. Nothing is published between bands (`publishedInEndOn` = 0 on every corpus
swing). The fused 3-D shaft exists on 90 of 528 frames because it is built only where the DTL
published. The block records the DTL camera as `calibrated: false, dtlYawDeg: 0, dtlPitchDeg: 0`.

So three different things are happening, and they need three different answers:

1. **The gaps at P2, the top and P6 are physics.** From a camera on the target line the shaft is
   parallel to the optical axis at those instants: it projects to a dot. There is no angle in the
   image to measure, and none to interpolate.
2. **The holes inside the sighted bands are engineering.** Every frame must pass the RAY gate on
   its own; a miss is a hole; sighted runs shorter than six frames are thrown away; the drawn
   length jumps between three sources; and the tile hides the shaft once the nearest sample is
   40 ms old. That is the "sporadic".
3. **The long OCCLUDED tail is the hands, not the club.** OCCLUDED is assigned when the grip
   anchor is quarantined (no grip, an unconfident wrist, or a row residual over 80 px), and it
   outranks END_ON. On 07-04 s7 the reasons read "anchor quarantined: row residual 55–87 px". The
   DTL pose loses the hands behind the body at the top and through the finish; the club may be
   perfectly visible.

## 2. Why "the synthetic track from this view" cannot be resolved

The face-on synthetic track (Layer C, `shaft_synthesis.h`) is a cubic Hermite in the face-on
angle θ_F between the P-position anchors, and it works because θ_F is a smooth, monotone-by-phase
function of time (the one-reversal law) and the anchors are real measurements. Neither holds in
the DTL view:

- **θ_D is undefined exactly at the anchors that would bracket the gaps.** The DTL angle swings
  through a singularity at P2, P4 and P6: the projected length ρ_D goes to zero and the angle flips
  sign as the shaft passes through the camera's axis. Interpolating θ_D across that is
  interpolating through a pole. The tracker's design was right to refuse it.
- **The smooth variable is not in this image.** What varies smoothly through the top is the shaft's
  3-D direction: its angle within the swing plane (which the face-on camera sees, that is θ_F
  de-projected) and its out-of-plane angle η (which the DTL view carries in ρ_D and θ_D away from
  end-on, and the design deferred solving for, §5.8). A synthetic DTL line is the *projection of a
  3-D synthetic shaft*, never a curve fitted in DTL pixels.
- **Projecting needs the DTL camera.** The fusion today is orthographic, level and uncalibrated:
  yaw and pitch are assumed zero, and the alignment stick says the real rig was yawed 4–9° toward
  the golfer, rolled 2.4–6.5°, with a 75–84° inter-camera angle, and it moved between s3 and s4.
  A heading through the fused plane moves one-for-one with the camera's yaw. Without the camera's
  pose, a projected line would be drawn in the wrong place by exactly the unknown yaw.
- **The backswing plane is not one plane on this data.** Fusion fits a backswing and a downswing
  plane; the downswing fits (60.3° ± 0.7° on the 7-iron), the backswing is flagged `backIncoherent`
  and the 07-04 s1–3 band is mirrored. Part of that is the moved rig; part may be a real hinge.
  Only the downswing plane is safe to de-project through today.

So the answer to "why can't it be resolved" is: the information to resolve it exists, in the
face-on angle and the swing plane, but the two things that turn it into a DTL line, the camera's
pose and a trustworthy backswing plane, are exactly what the old sessions did not record.

## 3. What we can do, in three layers

### 3.1 Now, on today's data: the sporadic holes inside the bands

These do not need the clean session and can be gated on the existing 21 DTL swings plus the
held-out band truth (`corpus/dtl_heldout_truth`). None of them bridges an end-on gap.

- **A bounded HOLD inside a sighted band.** Between two RAY frames of the same band, a frame that
  failed the RAY gate keeps the band's own Viterbi θ_D with a `Held` tier and the neighbours'
  confidence, for at most N frames (N ≈ 6, ~40 ms). Different from the PRED tier the design
  rejected: that invented measurements at impact across a gap; this coasts a solved path between
  two measurements of the same band, and is flagged.
- **Quarantine after end-on, not before.** Classify END_ON first (ρ̂_D < `rhoSolveMin`), then
  quarantine; and separate "wrist unconfident" from "row residual" in the tier name, so the tile
  and the metrics know whether the club was hidden or the hands were.
- **Shorter minimum run.** `minBandFrames` 6 → 3 near band edges; the schedule already says
  where the club is coming into view.
- **One length per band.** L̂_D from the ball at address, held per band with the projected
  ρ̂_D scaling, instead of switching between `rend`, `snapLine` and `latBand` frame by frame
  (that switching is the flicker in the drawn head).
- **Do not publish escapes.** Post-P8 RAY frames with `escape: true`, θ = 2π and the head at the
  frame edge still publish (skeleton3d branch note); they are noise the tile draws.
- **Tile staleness tied to the tier**, not a fixed 40 ms: a Held frame is current.

Expected effect: the RAY/Held coverage inside bands rises from ~0.65–0.7 to ~0.9 on the mid
backswing and delivery bands; the gaps at P2, the top and P6 remain, as they should until 3.2.

### 3.2 With the clean session: a continuous DTL line as the projection of a 3-D synthetic shaft

The product to build is a 3-D synthetic shaft, drawn into *both* tiles:

- **Direction in the plane** from the face-on Layer C synth (continuous, already built, C¹).
- **Plane** per phase from fusion: address plane (from DTL alone, as `swingPlane` does now),
  backswing plane, downswing plane. The clean session decides whether the backswing is one plane,
  a hinge at a known phase, or needs the out-of-plane angle η as a fitted curve.
- **Out-of-plane angle η(t)** solved from the DTL's own ρ_D and θ_D on the sighted frames, as a
  smooth spline through the end-on instants (η is finite and smooth there; it is θ_D that is not).
  This is the deferred §5.8 item and it is the right place for the DTL's information to enter.
- **Projection** into the DTL image with the calibrated camera (yaw, pitch, roll, position relative
  to the face-on camera) and the DTL grip anchor; where the grip is quarantined, the anchor is the
  projected 3-D hands from skeleton3d.
- **Honesty rules** carried over from the face-on synth: the projected line is flagged
  `Synthesized`, the metrics never read it, the tile can show it dimmer, and a DTL measurement is
  never claimed where there was none. The tracker's measured frames stay the evidence.

Skeleton3d already fits a continuous 3-D club with a plane prior and has a "DTL cam" preset in
the 3-D view; the shortest path to a first drawn line is to project *that* onto the tile, as a
preview, before the fusion-plane product replaces it.

### 3.2a Before the calibration: four improvements the two views already carry (added 1 Oct 2026)

*Written after §3.1 shipped (`dtl_continuous_20261002.md`) and after a first reading of the
re-analysed 4 July session. Mark's question: "are there any improvements we can put into the
synthetic and fused tracks before we have calibration?" There are four, and they are ordered by
what each buys. None needs a stick clip; each is gated on data that exists; each ships dark
until its gate passes. What none of them reaches is stated at the end, so the calibration is not
mistaken for optional.*

**Where things stand.** The fused track (`club3d`) exists only on frames the DTL tracker
MEASURED, in bands, bridged on the face-on side by the Layer C synth where face-on coasted. The
continuous 3-D club that the kinematic sequence and the 3-D panel use is not the fused track: it
is the face-on synth de-projected through the fused *downswing* plane (the sequence), and the
skeleton fit's own club held to the plane where DTL is blind (the panel). §3.1's new DTL synth is
that same de-projection, in-plane, projected back into the DTL tile. The in-plane assumption is
the thing all three share, and it is the thing the DTL view can correct.

**(A) The out-of-plane angle η(t) from the DTL view's own frames** — the deferred §5.8 item of
the tracker design, and the piece §3.2 names as "the right place for the DTL's information to
enter".

- *Signal.* Every fused frame already carries `oopDeg`: the signed angle of its direction off
  its phase's fitted plane. Unlike θ_D, η is finite and smooth through P2, the top and P6, so it
  can be fitted across the end-on gaps where θ_D cannot.
- *Fit.* One smooth curve per swing over address → P8 (a cubic smoothing spline or a Hermite
  through per-band medians; knots every ~30 ms), fitted on frames that are face-on-MEASURED,
  unflagged (`signDisagree`, `illConditioned`, `offPlane` all clear) and not `bridged`; pulled
  to zero with a weak prior where no band is within ~150 ms, so a gap coasts to in-plane
  rather than extrapolating a slope.
- *Use.* De-project the face-on synth angle through the plane **rotated by η(t) about the
  face-on view line of that frame** — equivalently, choose the direction in the face-on view
  plane that sits η(t) off the phase plane, on the side the sign says. That direction is the
  continuous 3-D shaft; project it into the DTL tile as §3.1's synth does now.
- *Why it survives the unknown yaw.* η is a residual within one camera model, like the
  inclination, which moved ≤ 1° over ±15° of yaw. The heading still moves 1:1 with yaw; the
  shape does not.
- *Gate.* Leave-one-band-out on the 21 DTL swings: fit η without a band, predict θ_D on that
  band's measured frames through the de-projection, and compare with what the tracker measured
  (p50/p90 in degrees, per band family). Pass = the prediction with η beats the in-plane
  prediction on the mid-backswing and delivery bands and is no worse anywhere; the fused
  planes and every metric unchanged (the curve feeds the synth and the tile, nothing else, until
  a separate decision moves the sequence onto it).
- *Files.* `shaft_fusion.h` (the fit, pure std, beside `fitPlane`); `dtl_shaft_synth3d.h` (an
  η input to `deproject`); `club3d` JSON gains `eta: {knots, values, n, rms}`; `synth3d` samples
  gain `etaDeg`. Test: a synthetic swing with a known sinusoidal η recovered through the gaps.

**(B) The camera from the skeleton fit** — a self-calibration from the golfer's body rather
than from a stick.

- *Signal.* `skeleton3d` fits both cameras (`fitCameras`, `fitDtlRoll`): on 07-04 s7 it reads
  the two views' ground-plane rays 77.8° apart, i.e. a DTL yaw of about 12° from square, plus a
  pitch and a roll. The session pool (`skeleton3d_pool.h`) medians the shared values per
  session. The fusion and the DTL synth assume 0, 0, 0.
- *Cross-check first, then use.* The alignment-stick probe (`dtl_yaw_probe.py`) read 4–9° on the
  same rig, depending on the assumed distance. Before either number is trusted: put the skeleton's
  per-swing yaw, its session median and the stick probe's range side by side for 07-04 s4–15
  and 06-11 s1–9. Agreement within ~3° makes the skeleton's camera the fusion's seed (it fills
  `ShotAnalysisJob::dtlCameraCalib` with `source: "skeleton3d pool"`, `calibrated` still false,
  a new `estimated: true`); disagreement is a finding about one of the two and stops here.
- *What it changes.* The fused planes' heading and the DTL synth's drawn heading — on s7 the
  synth read 61° at P1 against a measured 55°, which is this yaw. Inclination barely.
- *Gate.* With the skeleton camera in, the DTL synth's θ_D on MEASURED frames (where it can be
  compared) must land nearer the tracker than with the zero camera, per band, on the 21 swings;
  the corpus's `deliveryVsAddressDeg` spread across a session must not widen; the sequence's club
  node timing (`clubheadPeakLead` sd) must not worsen. Ships behind `shaft.fusion.cameraFromSkeleton`.
- *Caveat.* The skeleton's camera model is pinhole with an assumed principal point and no lens
  distortion, and its lengths are frozen to the height for that reason (`fitLengths` note). Its
  yaw is a measurement with an unstated uncertainty; the cross-check is what states it.

**(C) Anchor the fusion on the DTL view at impact** — the mirror of what the DTL synth does
with face-on, at the one moment the roles reverse.

- *Why.* Face-on coasts through impact (the club is a fan); DTL sees impact sharply, in plain
  view, at ρ̂_D ≈ 0.95. Today the fused frames there are `bridged`: the DTL measurement is
  intersected with the face-on *synth*, so the impact direction rests on the weaker witness.
- *Rule.* On a frame where the DTL tracker MEASURED and face-on did not (the bracket is not
  measured on both sides), take the direction as the DTL view plane's intersection with the
  phase's fitted plane — the same de-projection as the address plane and as (A), from the
  other camera. Flag it `dtlAnchored`; keep it out of the plane fits like `bridged`; publish it
  in `club3d.frames` beside the bridged value so the two can be compared on every such frame.
  With (A) in, the rotation by η(t) applies here too.
- *Where it matters.* P6.5 → P7.5: the direction attack angle, lag and the impact lean read.
  It gives the impact instant a shaft that owes nothing to the face-on bridge.
- *Gate.* On the 21 swings, the `dtlAnchored` direction against the `bridged` one on the same
  frames: the angle between them, per swing, and which lies nearer the neighbouring
  fully-measured fused frames (the last before and first after the coast). Pass = nearer on
  the median swing and never worse by more than the fusion's own rms. Then, separately and by
  decision: whether the sequence's club channel reads it through impact.

**(D) Repair the mirrored backswing band inside the fusion** — the fusion's own finding
(`shaft_fusion_design.md` §0 item 1, §5 owed item 1).

- *Why here and not in the tracker.* The tracker design's one-direction rule (§5.10) stands:
  face-on and the plane must not steer the DTL tracker. The fusion reads both and feeds
  neither, so a decision it makes about how to *read* a DTL band is its own.
- *Rule.* When the backswing plane fit is incoherent (`backIncoherent`, rms > 12°), refit with
  each DTL band's θ_D reflected about the band's corridor centre line (θ → 2·centre − θ, the
  other depth sign of §4.1 (c)); keep the reflection for a band only if the refit's rms drops
  below the coherence threshold AND the reflected band's `signDisagree` count does not rise.
  Record `reflectedBands` in `club3d.summary`.
- *Gate.* 07-04 s1–3 and the wedge session, where the fusion first saw it: the backswing plane
  becomes coherent (rms 17–23° → under 12°) and its inclination lands within a few degrees of
  s4–15's; no swing that was coherent changes. The DTL track itself is untouched, byte for byte.

**Order.** A, then B, then C, then D. A is the honest continuous track and its gate needs no new
data; B changes only headings and needs its cross-check; C slots into A's de-projection; D is
small and independent. Each behind its own switch, each with a bit-identical OFF, each judged on
the 21 DTL swings by the grader that exists (`dtl_continuous_grade.py` extended with the per-item
columns above) before its default moves.

**What none of this reaches.** The club's heading as a number that could be called club path,
and any depth of the hands or the grip. The first needs the camera's yaw to a degree; the second
needs a perspective model with the camera's position. Those are what the protocol's stick and
card clips give (`camera_pose_sticks.h` is waiting for them) and nothing above substitutes for
them. Everything here improves the shape and the continuity of the 3-D club; it makes the
calibration worth more, not less.

### 3.3 Later: a DTL tracker that knows its own geometry

With a calibrated camera the tracker can predict its own end-on schedule from the 3-D shaft rather
than from the face-on angle with an assumed on-axis camera (the `scheduleConflict` check the
design never built), and can grade itself against the projection. The per-golfer θ_D(phase) shape
model of design §9 becomes a prior on η, not on θ_D.

## 4. What the clean session must give us (additions to protocol v1.0)

The protocol's placement rules stand: lens on the hands-line stick at hand height (0.90–1.00 m),
levelled, panned so the stick is vertical on the centre column, framing that keeps the clubhead
at the top and the finish, ball unclipped, nothing moved, calibration clips A–F at the start and
the stick and ball clips again at the end, launch monitor on every swing, rig sheet. For this
design the session must also provide, in this order of importance:

1. **The DTL camera's pose relative to the face-on camera**, measured, not inferred: yaw, pitch,
   roll and position, from the card and stick clips seen by *both* cameras, stored in the swing
   document as `club3d.camera.{calibrated: true, yawDeg, pitchDeg, rollDeg, offset}`. Everything
   in §3.2 hangs on this number. Yaw to ≤ 1°.
2. **DTL truth beyond address.** Today there is none. Hand marks on the DTL frames at P1, P3, P5,
   P7 (the instants where the shaft is a line) for at least ten swings, plus the clubhead at the
   top in the DTL frame (a dot, but its position is the projection of the 3-D head and is exactly
   what §3.2 predicts).
3. **Both clubs.** The taped 7-iron (band locks in DTL need light: a ring light on the DTL camera,
   which the corpus never had, gave 0–6 locks per swing) and the bare 6-iron, ten swings each,
   same rig.
4. **Camera timestamps from both cameras** (already the rule for face-on), and the ball-drop clock
   clip, so the two views can be registered to a millisecond; fusion's inter-camera latency is an
   open risk in the tracker design.
5. **A backswing-plane test:** five swings with a deliberately flat and five with a deliberately
   steep backswing, so the hinge question in §3.2 is answered by data rather than assumed.

What the session does *not* need: a third camera, or any new markers on the golfer.

## 5. Order of work

1. §3.1 on today's data, gated on the 21 DTL swings and the held-out band truth; a small,
   contained change to `dtl_shaft_post` and the tile. **Done 1 Oct 2026 — `dtl_continuous_20261002.md`.**
1a. §3.2a A–D, in that order, each behind its own switch and gated on the same 21 swings (+ 07-04 s1–3
   for D). **Done 3 Oct 2026 — `dtl_precalibration_20261003.md`:** A built, gate not met (η is not
   continuous across an end-on gap on this golfer), dark; B stopped at its cross-check (skeleton yaw
   12.6° vs the stick 9.0–9.9°, ranges disjoint); C degenerate — down the line the DTL view plane at
   impact IS the swing plane, kept as a diagnostic, off; D on (s2/s3 coherent again, headings within 1°
   of the downswing plane's).
2. Record the protocol session (§4).
3. Calibration: the card/stick solve for the DTL pose; store it; re-run fusion with the real
   camera and re-measure the plane fits and the heading.
4. η(t) and the 3-D synthetic shaft; project into both tiles; grade against the new DTL marks.
5. Only then decide whether the tracker itself should solve for η (§3.3).

## 6. Sources

`dtl_shaft_tracker_design.md` (§0, §1.1, §5.8–5.9, §5A, §8–10), `dtl_shaft_tracking_corpus_assessment.md`,
`shaft_fusion_design.md` (§2, §4.1(c), §5), `kinematic_sequence_design.md` §13–14,
`dtl_posture_design.md` §4, `two_camera_capture_protocol.md`, `skeleton3d_shaft_branch_design.md`;
code: `src/Analysis/dtl_shaft_decide.cpp` (sighted frames 874–950, quarantine 698–741, ρ̂ 634–666),
`dtl_shaft_post.cpp` (tiers 390–405, 522–547), `shaft_fusion.h` (camera 86–112, fuse loop 314–331),
`src/Gui/shot/dtl_overlay_payload.cpp`, `src/Gui/cameras/PpCameraFrame.qml` (1178–1203, 1420–1434).
Numbers: `corpus_v11` runs of 07-04 s7 and 06-11 s1, 30 Sept 2026.
