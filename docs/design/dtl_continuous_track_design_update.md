# The down-the-line club track: why it is sporadic, why it cannot synthesise itself, and what makes it continuous

**Status:** design update, 30 Sept 2026. Seeds the two-camera protocol session
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
   contained change to `dtl_shaft_post` and the tile.
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
