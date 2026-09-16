# Impact camera — a 420 fps window on the ball, and what it can honestly measure

**Status:** **DESIGN, not built.** Written 2026-09-14 from a conversation about adding a
high-speed (420 fps, 320×240) camera aimed at the ball. No hardware chosen, nothing wired.
**Scope:** what a small, fast, ball-centred camera can deliver — as a visual and as club data —
where it goes, what it needs to be true of the sensor and the light, how each quantity is read
off the frames, and how it is validated. The stated motive is a **low-cost route to club data
for users whose launch monitor reports ball data only**.
**Not in scope:** capture-stack integration (which backend, which ring), the analysis stage that
consumes the clip, the UI. Those are named where they matter (§8) and designed later.
**Update 2026-09-15:** the ROI frame-rate probe §3 asked for has been run on both studio
Chameleon3s — §3.1. The 420 fps the design was written around is now a measured 591 fps at a
240-row ROI (612 fps at ≤ 224 rows), at any width up to the full 1280; §1, §2, §10 and §11 carry
the measured figures alongside the original ones.
**Update 2026-09-15 (evening) — placement decided, calibration made a precondition.** Both placements
are designed: **the face-on floor mount (§4) is built first**, because it is the only one that gives
launch angle and full ball speed. The elevated path camera (§5) follows: a tripod at 1–2 m looking
down from the face-on side, to validate and verify path estimates, solved jointly with the floor
camera.
- **Why calibration comes first:** a first graded session, a tripod as low as it went looking down,
  with the launch monitor alongside, showed the ball-diameter ruler this document leaned on (§4
  "Scale") is not good enough. It read ball speed 1.3–1.5 × the monitor, and launch angle ~9° low.
- **What changes:** every speed and angle here now needs the card calibration of
  [camera_calibration_design.md](camera_calibration_design.md) §4.8. The ball stays as a relative
  ruler and a verifier. The session itself was set aside as a rig lesson, not data.

---

## 0. The conclusions, up front

1. **Frame rate is the easy part. Exposure and placement decide whether this yields club
   numbers or just a clip.** The pixel scale of 320 px over a 400 mm field is about what the
   1080p face-on camera already has over the whole body. The new camera does not win on
   resolution. It wins on 2.8× more head samples near the ball and — only if the exposure is
   two orders of magnitude shorter than today's — on blur. Our present low-point noise traces
   to a 6.5 ms exposure ([low_point_metric_design.md](low_point_metric_design.md), 2026-08-21
   update; [camera_calibration_design.md](camera_calibration_design.md) §7). This camera
   reproduces that problem exactly unless §3 is honoured.
2. **No single placement gives all three asked-for quantities.** A ground-level **face-on zoom**
   gives attack angle, low point and high/low strike. A camera **looking down on the ball** (§5: a
   tripod at 1–2 m on the face-on side, to validate path) gives club path and
   heel/toe strike. Face angle should come from *neither* camera: with path measured, it falls
   out of the launch monitor's launch direction on a well-conditioned equation (§6).
3. **420 fps is the right rate for the metrics, and the right rate for the visual only because
   the visualisation carries it.** The raw frames are a five-frame flipbook, not a video, and
   there is never a frame with the ball on the face. The product is the frames *plus* the fitted
   arc, the ghosted head positions and the marked impact instant — which are the metric
   pipeline's own outputs, so there is no separate visual layer to build (§7).
4. **Build the face-on zoom first, the elevated path camera second.** The face-on is a ground mount beside
   hardware we already have, it is the clip people want to look at, and every one of its
   outputs has a criterion we already own (§9). In the studio that means the tripod Chameleon3
   becomes the impact camera and an iPhone takes over DTL — **after** an overlap session shows
   the phone's rolling shutter does not cost more than its frame rate buys (§10).
5. **The Chameleon3 clears the frame-rate gate with room to spare (measured, §3.1).** Both
   studio cameras deliver 591 fps at 240 rows and 612 fps at ≤ 224 rows, at 70 µs exposure,
   at any width up to 1280 — the rate depends on rows only — with no incomplete frames while
   the other camera streams full-frame beside it. Exposure goes down to 6.4 µs. The chosen
   mode is **640×240 at 591 fps, 50–70 µs, ~1 mm/px** (§10.2). What remains is what §1 said
   remains: light, and a lens for the mount distance.

---

## 1. The physics that sets the design

Everything below follows from a few numbers. 7-iron at 80 mph (36 m/s); driver in brackets.

| Quantity | Value at the 420 fps design point | Chameleon3 at 591 fps (measured, §3.1) |
|---|---|---|
| Head travel per frame | 86 mm (107 mm) | 61 mm (76 mm) |
| Ball–face contact time | ~0.45 ms — **never** a frame during contact at this rate | 0.45 ms — 27% odds of one; still never relied on |
| Blur per 100 µs of exposure | 3.6 mm (4.5 mm) | same — exposure, not rate |
| Exposure for ≤ 2 px blur at ~1 mm/px | **≤ 70 µs** (≤ 50 µs) | same; the camera's floor is 6.4 µs |
| Head positions in a 400 mm field | 4–5 before impact, 2–3 after | 6–7 before, 3–4 after |
| Ball travel per frame after impact | ~110 mm (~145 mm) — 1–2 frames in the field | ~78 mm (~103 mm) — 2–3 frames |
| Raw data rate, mono 8-bit | ~32 MB/s — any USB 2 link | Bayer 8-bit: 45 MB/s at 320×240, 181 MB/s at 1280×240 |

Two consequences:

- **Exposure, not fps, is the constraint.** A 70 µs exposure at 420 fps is a 3% duty cycle, so
  the sensor has time to spare. What it does not have is light: relative to a 4 ms indoor
  exposure that is ~60× less. Because the field is tiny the light can be too: two ~100 W LED
  floods at half a metre on the ball patch are in the right range for a global-shutter mono
  sensor at f/2. A strobed LED driven off the camera's strobe output is the better version and
  the one every commercial unit uses (usually at 850 nm so the golfer does not see it). Start
  continuous, measure the blur, then decide.
- **The arc is curved on the scale of one frame.** Near the bottom the head's tangent turns by
  several degrees per 90 mm of travel, both in the vertical plane (attack angle) and in the
  ground plane (path). **A straight-line fit through the visible positions is biased** unless
  they happen to straddle the ball symmetrically. Every direction in this document is the
  tangent of a *curved* fit evaluated at the ball, never a line through the points.

## 2. Why 420 fps is the design point, and more is the wrong purchase

The directions come from a curved fit through the head positions inside the lit field. That
needs ≥ 4–5 pre-impact positions to constrain a quadratic (or a circle whose radius the wide
cameras already know). Beyond that, the fitted direction improves as √n with frame count and
linearly with pixel scale — doubling fps buys ~1.4×, doubling resolution buys 2×. So:

> **Pick the mode that gives at least 4 pre-impact head positions at about 1 mm per pixel, then
> spend everything else on exposure and light.**

By that rule 320×240 @ 420 fps over a 400 mm field and 640×480 @ 210 fps over an 800 mm field
are equivalent for the numbers. The 420 version needs a quarter of the lit area and has more
frames close to the ball, so it is the easier one to make work.

| Rate | Head positions in 400 mm (7i) | P(frame during contact) | For metrics | For the eye |
|---|---|---|---|---|
| 240 (phone) | 2 | 11% | no — a tangent cannot be fitted, only a biased mean direction | what users already have |
| **420–500** | **4–5** | 19% | **yes, with exposure ≤ 70 µs** | a 5-frame flipbook — see §7 |
| **591** (Chameleon3, 240-row ROI, measured §3.1) | **6–7** | 27% | **yes** — the exposure is available; the light is the question | 6–7 frames; the same flipbook, one step finer |
| 1000 | 11 | 45% | marginally better, usually at a resolution cost | 11 frames; still stepped viewing |
| 2200+ | 25+ | 100% | no further gain | watchable compression video; different hardware class and cost |

The head crosses the field in ~11 ms. At 420 fps that is five frames — a sixth of a second at
normal playback. Even 1000 fps is a third of a second and even odds of catching the ball on the
face. Watchable impact video starts around 2000 fps. Nothing at this cost point *plays* well;
§7 is how it is presented instead.

## 3. What must be true of the camera

- **Global shutter.** Non-negotiable. A rolling shutter reads 240 lines over most of a 2.4 ms
  frame; the top and bottom of the head are then imaged ~2 ms apart, a skew of tens of
  millimetres. Candidate sensor families: OV9281/OV7251, IMX296/IMX287, AR0234.
- **Mono, not colour.** ~3× the sensitivity and no Bayer interpolation on the edges we fit.
- **Exposure ≤ 70 µs, manually locked**, with the exposure value recorded per clip exactly as
  `CameraInstance::refreshExposure` already records it for the wide cameras. The blur budget is
  the calibration's precondition, as focus is for the wide cameras.
- **Verify the mode is a true 420 fps** before believing anything: film a dropped ball and check
  free fall against the frame spacing. The same drop gives the pixel scale. Duplicated or
  dropped frames in cheap USB 420 modes are common and invisible in playback.
- **Which 320×240 is it?** A sensor *crop* and a *binned/scaled* mode have very different fields
  of view for the same lens, so focal length follows from the mount distance *after* that is
  known — the same trap [camera_calibration_design.md](camera_calibration_design.md) §3.4
  records for the phone's 240 fps crop.
- **Field of view ~400 mm** along the direction of head travel (the frame's long axis), ~300 mm
  across. Tighter buys pixel scale but loses positions; §2's rule is the arbiter.

**A cropped Chameleon3 is the candidate, and it has now been measured (§3.1).** The studio's
CM3-U3-13Y3C (PYTHON 1300, global shutter, 1280×1024 at 149 fps delivered) reads at 6.39 µs per
row plus 158 µs per frame — 1024 rows is where its 150 fps comes from — and its firmware floors
the frame period at 1.635 ms (611.7 fps). A 240-row ROI delivers 591 fps; ≤ 224 rows delivers
the 611.7 cap. Width has no effect on the rate at all. The exposure node goes down to 6.4 µs, and
70 µs streams at the full rate. Two costs stand: it is a Bayer sensor (sensitivity and edge
sharpness below a mono part), and a 320-px crop behind the present lens at the present distance is
~2 mm/px over ~620 mm — it needs a longer lens or a closer mount, and it then stops being a body
camera. A third Chameleon3, not a re-tasked one.

### 3.1 Measured: the studio Chameleon3s under a cropped ROI (2026-09-15)

Both studio cameras — CM3-U3-13Y3C, serials 18277032 and 17453937, firmware 1.13.3.00, Spinnaker
4.3.0.190 on the Windows PC — were probed with `tools/probes/chameleon3_roi_probe.cpp` (build and
run notes at the top of the file). Method: BayerRG8, the pixel format the capture path uses;
ExposureAuto off; AcquisitionFrameRateEnable on; ExposureTime written, then AcquisitionFrameRate
written to its own maximum; 40 stream buffers as in `VideoInputSpinnaker`; each ROI streamed for
2 s. The delivered rate comes from the camera's own frame timestamps and agrees with the host-side
frame count to 0.1%; incomplete frames were counted. The two cameras agree to 0.1 fps on every
row, so one table serves both:

| Rows (1280 wide, 70 µs exposure) | Node's max (fps) | Delivered (fps) | Frame period |
|---|---|---|---|
| 1024 (full frame) | 150.7 | 149.3 | 6.70 ms |
| 720 | 213.1 | 210.2 | 4.76 ms |
| 480 | 316.4 | 310.1 | 3.22 ms |
| 400 | 377.3 | 368.5 | 2.71 ms |
| 320 | 467.4 | 454.0 | 2.20 ms |
| 280 | 530.8 | 513.6 | 1.95 ms |
| 260 | 569.4 | 549.6 | 1.82 ms |
| **240** | 611.7 | **591.1** | 1.69 ms |
| 232 | 611.7 | 609.5 | 1.64 ms |
| ≤ 224 | 611.7 | **611.7** | 1.635 ms — the firmware floor |

- **Rows are the whole story; width is free.** 1280×240, 640×240 and 320×240 all deliver
  591.1 fps; 1280×480 and 640×480 both deliver 310.1. Delivered period ≈ 158 µs + 6.39 µs × rows,
  floored at 1.635 ms, so the 611.7 fps cap is reached at about 230 rows and below. The 320×240 in
  this document's title is therefore not a frame-rate choice — 1280×240 runs at the same rate — it
  is a field-of-view and light choice (§1: the lit area scales with the field).
- **The node over-reports.** AcquisitionFrameRate's maximum sits 1–3.5% above what is delivered
  off the cap (611.7 reported at 240 rows, 591.1 delivered) and is exact on it. The Cameras panel,
  which reads that node, will say 611.7 for a 240-row ROI. Plan on the delivered column; the camera
  timestamps are the truth, and they are what the capture path records.
- **Exposure.** Floor 6.4 µs. 50 µs and 70 µs both stream at the full 591 fps with zero incomplete
  frames, so the ≤ 70 µs budget of §1 is available and the light question is exactly as §1 states
  it. (At the 6.4 µs floor a 240-row ROI reaches the 611.7 cap; from 50 µs to the ceiling it is
  591.1 — the exposure costs one small step near the cap and nothing elsewhere.) The ceiling is
  the node's frame period minus 63 µs (1571 µs at the cap). Writing a rate the
  current exposure cannot fit clamps the exposure down (6570 µs became 1571 µs at 240 rows); by
  the same rule, a 6.57 ms exposure holds any ROI to ~150 fps. The fast rate needs the short
  exposure *and* rate control enabled with the rate written — a cropped ROI alone does nothing.
- **A note for the capture path.** When AcquisitionFrameRate's maximum was read straight after the
  Width/Height write, it was one ROI stale: the rate applied was the previous ROI's maximum and the
  delivered rate followed it, in every row of a sweep. The cause is the GenApi node cache — a
  Width/Height write does not invalidate the rate node's cached maximum — and the fix is
  `InvalidateNodes()` on the node map after the write, which reads fresh every time, under auto
  or locked exposure alike (a same-value ExposureTime write, first thought to refresh it, does
  not; the rate write of the previous sweep step had). Two further firmware facts, found when the
  first build of the Settings placement offered nothing: this camera has **no**
  AcquisitionFrameRateEnable node — it has the legacy AcquisitionFrameRateEnabled and
  AcquisitionFrameRateAuto, so the app's guarded write to the SFNC name had always been a silent
  no-op — and ExposureTime is read-only until ExposureAuto is Off. `VideoInputSpinnaker::start()`
  and the enumerate-time probe in `video_input_factory.cpp` now do all three.
- **Binning is the other 320×240.** BinningVertical = 2 sets both axes (BinningHorizontal is
  read-only and follows it): 640×512 over the full field at 470–475 fps delivered, pixel format still BayerRG8 (the
  colour correctness of a binned Bayer frame was not checked by eye). So on this camera both
  answers to "which 320×240 is it?" exist and are told apart by the Width/Height/Binning nodes: a
  crop keeps the pixel scale and shrinks the field; a bin keeps the field and halves the scale.
  Binned-and-cropped was not measured.
- **Link budget.** DeviceLinkThroughputLimit is 198.1 MB/s and already at its maximum. Full frame
  at 149 fps is 195 MB/s; 1280×240 at 591 fps is 181 MB/s; 320×240 at 591 fps is 45 MB/s. **Both
  cameras streaming at once** on the Windows PC — one full-frame at 149 fps, the other a strip at
  its maximum — ran 4 s with zero incomplete frames on either, for strips of 1280×240, 1280×480,
  640×240 and 320×240. Not checked: whether the two cameras share a host controller, and behaviour
  over a session rather than 4 s.
- **Strobe and trigger.** Line1 is a dedicated output with sources ExposureActive,
  ExternalTriggerActive and UserOutput1; Line2 and Line3 switch between input and output; Line0 is
  input-only. TriggerSource offers Software, Line0, Line2 and Line3; ExposureMode offers Timed and
  TriggerWidth. The strobe of §11 step 4 can be driven from Line1 with no camera-side hardware.

**What changes in the design.** Nothing in the argument; several numbers. The per-frame
quantities at 591 fps are the third column of the §1 table: 61 mm of head travel, 6–7 pre-impact
positions in 400 mm, a 27% chance of a frame during contact. §2's rule — at least 4–5 positions at
~1 mm/px, then spend on exposure and light — is met with margin, and because width is free the
field can grow along the head's travel at no rate cost if the light can follow: 1280 px at
~1 mm/px is about 21 head positions per swing, but four times the lit area of a 320-px field and
181 MB/s on the link. The frame-rate gate in §11 step 2 is passed. Frame integrity — no drops, no
duplicates — is established on the bench, so the dropped-ball test's remaining job is the pixel
scale and the physical blur.

## 4. Placement A — face-on zoom, at ground level beside the face-on camera

The camera looks horizontally, perpendicular to the target line, from the golfer's front
(same side as the existing face-on camera), centred on the ball. It sees the vertical plane
containing the target line: the arc's vertical profile, the ball, the mat surface.

**What it gives, and how each is read:**

| Quantity | Read | Note |
|---|---|---|
| **Attack angle** | tangent at the ball's x of a quadratic (or known-radius circle) through the 4–5 pre-impact head positions | same sign as `attackAngle` in `club_delivery.h`: + is a more UPWARD strike |
| **Low point ahead** | vertex of the same fit | becomes a *measured* vertex for irons; for a driver (+AoA) the vertex sits before the ball, still inside a 400 mm field. Replaces the P7-pinned interpolation vertex that `lowPointAhead` reads today, with its frozen σ = 2.0 in |
| **Impact instant** | first frame with the ball displaced, minus displacement ÷ the LM's ball speed | sub-frame, ~0.1 ms; a clip-local anchor that needs no cross-camera sync |
| **High/low strike** | fitted head (leading edge / face centre) evaluated at that instant, vertically against the ball centre | same axis as `lm.strikeHeight`; always an extrapolation, never observed (§7) |
| **Clubhead speed at the ball** | head velocity at the contact instant from a one-sided fit over the pre-contact frames, in plane millimetres, the hosel's depth offset applied | the camera-available rung on `clubheadSpeed`, graded against `lm.clubheadSpeed` as the composed speed was |
| **Ball speed** | robust line (Theil–Sen) through the in-flight ball centres against frame index × median period, in plane millimetres | graded against `lm.ballSpeed`. A ±5° start direction costs 0.4 %; per-frame pairs are useless (timestamps jitter ±0.4 ms) |
| **Launch angle** | the same line's direction in P | graded against `lm.launchAngle`. Only this placement gives it (§5 cannot) |
| **Shaft lean at impact** | shaft line in the last pre-impact frame | bonus; foreshortening is small in this view |

**What it cannot give:** path. In/out motion is along the optical axis. (At very short range
the head's apparent size changes with depth, and a close-mounted camera *could* read a
±1–2° path from scale — the GCQuad geometry — but that is a stretch, not a plan.)

**Scale comes from the card calibration, not from the ball** (corrected 2026-09-15). This section used
to say the ball-diameter ruler was good to a couple of percent without a board.

*What the first graded session showed.* On a dim, top-lit ball the detected diameter moves ±10 % with
the threshold. Ball speed read 1.3–1.5 × the launch monitor on it. What replaces it,
[camera_calibration_design.md](camera_calibration_design.md) §4.8:
- **The card.** A ChArUco card stood upright in P, the vertical plane through the ball along the target
  line, is solved for the camera's pose. Every measurement is then made in millimetres in P, or in a
  plane parallel to it at a stated offset.
- **The ball, per shot.** Its diameter *relative to its reading at calibration time* gives its depth
  offset from P. The same detector under the same light cancels the threshold bias.
- **The hosel.** It is ~30–45 mm further from the lens than the face centre, so it is mapped at that
  offset.
- **A fixed tag** in view catches a bumped camera.

**Mounting** (tolerances in §4.8):
- **Position:** level, within 3° of pitch, and square to the target line within 5°. Lens between ball
  height and ~100 mm above the mat.
- **Lens and framing:** ≥ 1.5 m back with an ~8 mm lens, ball ~60 % across.
- **Guard:** a low guard in front of the lens.

⚠ **Two things a level floor camera sees that the tilted tripod did not.** Both are for the first
calibrated session to measure ([camera_calibration_design.md](camera_calibration_design.md) §11,
items 15–16):
- **The golfer's feet**, 300–500 mm behind the ball and inside the 240 mm strip, with the trail heel
  lifting through impact. The background model is built at address, so the heel's rise is foreground
  near the arc bottom.
- **The room beyond the golfer.**

## 5. Placement B — elevated, looking down from the face-on side (a tripod at 1–2 m)

A tripod on the face-on side, the camera 1–2 m up and looking down at the ball at ≥ 45° (≥ 60°
preferred), the frame's long axis along the target line.
- **Its job (Mark, 2026-09-15):** to validate and verify path estimates, ours and `lm.clubPath`. It
  is a check, not a production source.
- **Why a tripod:** the original idea was a ceiling mount looking straight down. A tripod is what the
  studio will actually use, and the angle it looks down at has a price, set out below.
- **What stays out of view:** from above and in front of the ball, the golfer's hands and body at
  impact (50+ cm inside the ball) stay out of the strip. At 60° the strip covers only ~±140 mm of
  ground either side of the ball, so the feet are out too.
- **What it sees of the head:** the toe-and-crown side, nearest the lens.

| Quantity | Read | Note |
|---|---|---|
| **Club path** | tangent at the ball's position of a curved fit through the ground-plane head positions, with the head's vertical motion (from placement A, in the bay frame) removed first — see the mount notes below | `clubPath` sign per [pinpoint_sign_conventions.md](pinpoint_sign_conventions.md): + is the head travelling right of the target line |
| **Heel/toe strike** | fitted head at the impact instant (§4's method — the ball is visible from above too) against the ball centre, along the face's heel–toe axis | same axis as `lm.strikeLocation`, toe +; perspective between head height and ground plane is a few percent — calibrate on lines drawn on the mat, accept ~3 mm |
| **Launch direction** | ball track over its 1–2 frames in the field | a cross-check on `lm.launchDirection`, marginal at this field size |
| **Clubhead speed** | head velocity in the ground plane at the contact instant | attack angle costs < 0.5 % at −5°; the head's top line is mapped at its height (+40–60 mm) |
| **Ball speed (horizontal only)** | the ball line's ground-plane velocity | ⛔ **not ball speed.** It is v·cos(launch): 13 % low at a 30° wedge launch. It needs a launch angle from placement A or a monitor before it is a speed. The ball also rises toward the lens (~120 mm over the track; ~7 % of range at a 1.5 m slant range looking down at 60°), mapped at a height from that same launch angle |

**Cannot give:** launch angle, attack angle, low point, and on its own a full ball speed. The ball's
apparent growth as it rises is ~2 px over the track, not a measurement.

**Calibration and mounting** ([camera_calibration_design.md](camera_calibration_design.md) §4.8):
- **The card, for scale and tilt:** a ChArUco card flat on the mat, origin tick on the ball spot. It is
  solved for pose.
- **An alignment stick for direction:** the stick along the target line in view gives the target-line
  direction to ~0.1°. The card's printed arrow is only as good as the operator's eye (±1–2°), and club
  path needs ≤ 0.5°.
- **The mount:** a tripod on the face-on side, 1–2 m up, looking down at ≥ 45° (≥ 60° preferred).
  An ~8 mm lens gives ~1 mm/px at a ~1.5 m slant range. Heights (ball centre, a teed ball, the head's
  top line) are mapped as offset planes, not squeezed into the mat's.
- ⛔ **Path is solved jointly with placement A.** Looking down at an angle ε, the image's vertical mixes
  in/out motion (× sin ε) with vertical motion (× cos ε).
  - **The size of it at 60°:** a −4° attack angle alone reads as ~2° of path, and a 30° launch as ~19°
    of launch direction.
  - **The fix:** placement A measures that vertical motion in the same bay frame, and B's in/out
    velocity is recovered with it removed. A 1° error in A's angle costs ~0.6° of path at 60°, 1° at
    45°, and nothing as ε → 90°.
  - **Without A:** B's path is withheld. It is never corrected with `lm.attackAngle`, because B is the
    check on the monitor's path, and borrowing its attack angle would couple the two instruments'
    errors.
  - **Resolution:** in/out resolution scales with sin ε (−13 % at 60°), another reason to look down
    steeply.

**Do not take face angle from the top line seen from above.** The heel–toe edge's projection onto
the ground is *not* perpendicular to the face normal's projection unless the toe and heel are at
the same height. With 30° of loft and 5° of toe-up the edge is displaced ~3°; and an 80 mm iron
top line at ~1 mm/px is under a degree per pixel of angular resolution anyway. The number would
look plausible and be wrong by more than the thing it is meant to diagnose.

## 6. Face and face-to-path come from the D-plane, not from a camera

For a centred strike the horizontal launch direction is mostly face with a loft-dependent
fraction of path:

    launchDirection ≈ (1 − k)·face + k·path        k ≈ 0.15 (driver) … 0.25 (wedge)

A ball-only launch monitor has two equations in two unknowns — this one, and the spin axis as a
function of (face − path) through a gear-effect coefficient that depends on strike location —
and the pair is ill-conditioned. That is why such devices' face/path estimates are poor.

With **path measured by the overhead**, face falls out of launch direction alone:

    face = (launchDirection − k·path) / (1 − k)

With `lm.launchDirection` good to ~0.5° and path to ~0.3°, face is good to ~0.7°, and
face-to-path is the difference. The spin axis then stops being an input and becomes a
**consistency check**: a residual between the spin axis and the (face − path) prediction is
gear effect, i.e. an off-centre strike — which the overhead's heel/toe measurement confirms
independently. Two instruments, one closed loop, and the quantity the ball inversion is worst at
is the one the camera measures directly.

## 7. The visualisation is the metric pipeline's own evidence

The frames alone would disappoint (§2). Presented as a stepped, annotated sequence they are more
informative than any clip under 2000 fps:

- **Ghosted head outlines** from each frame composited onto one still, so the sampling is
  visible as sampling.
- **The fitted arc**, drawn through them, with the **low point marked** (face-on) or the
  **path tangent at the ball** (overhead).
- **The extrapolated head at the impact instant**, drawn *as an extrapolation* — a distinct
  style from observed frames — against the ball. The visual must never imply a contact frame
  exists, because at this rate one never does.
- **Frame stepping**, not playback. A user stepping through can see when the fit is trustworthy
  and when it is not, which is exactly the honesty `metric_presentation_honesty.md` asks of a
  number.

The arc fit, the vertex, the sub-frame impact time and the extrapolated head position are the
things worth drawing *and* the things the metrics are computed from. One object; no separate
visual layer.

**Context comes from the cameras we already have.** The 150 fps face-on covers the whole arc at
a similar pixel scale; the impact zoom is a magnified inset on the same timeline, related by
`capture.impactUs` — which the clip itself now anchors better than the acoustic/IMU anchor does
(§4, impact instant).

## 8. What this reuses, and what it does not need

- **No pose model, no shaft tracker.** The field is static apart from club and ball. Background
  subtraction, blob and sub-pixel edge fits are the whole detector — classical CV on a tiny
  frame.
- **No cross-camera sync for the club numbers.** Impact lies inside the clip; every quantity in
  §4–§5 is clip-local. Sync matters only to place the inset on the wide timeline, and a few ms
  is fine for that.
- **Trigger:** a ring on the existing `source_ring` / `swing_window` shape, armed off the
  acoustic shot detector or the wide cameras' shot detection; the clip is short (±100 ms is
  ~85 frames) and cheap to keep.
- **Route ladder:** each optical quantity keeps its bare key (`attackAngle`, `clubPath`,
  `lowPointAhead`, `strikeLocation`, `strikeHeight`) and sits *beside* its `lm.` twin, never
  above it — `launch_monitor_reading.h` says why. The impact camera is a new rung on the bare
  key with `RouteQuality::Direct` where the read is a measurement (attack angle, path, low point
  vertex) and `Estimated` where it is an extrapolation (both strike coordinates).
- **Version gating:** a new stage version in `analysis_versions.h` so a re-analysis knows the
  clip was consumed; the existing ball/shaft stages are untouched.

## 9. Validation — what the criterion is for each quantity

The GCQuad in the studio reports club path, attack angle, face, strike location and strike
height. It is the criterion for everything here except low point, which it does not report.

| Quantity | Criterion | Method |
|---|---|---|
| Ball speed, launch angle | `lm.ballSpeed`, `lm.launchAngle` | ratio and bias ± SD per club, reported beside the card calibration's own σ. The launch monitor is a criterion and a per-shot health check, **never fitted to** (calibration §4.8) |
| Clubhead speed | `lm.clubheadSpeed` | ratio ± SD, the way the composed speed earned its 0.959 ± 0.022 |
| Attack angle | `lm.attackAngle` | limits of agreement across sessions and clubs, as for `clubheadSpeed` |
| Club path | `lm.clubPath` | same |
| Face, face-to-path (derived, §6) | `lm.faceAngle`, `lm.faceToPath` | same; a bias here is a bias in *k*, and *k* is per-loft |
| Strike heel/toe, high/low | `lm.strikeLocation`, `lm.strikeHeight`, **and impact tape** | tape is the cheap direct truth and works for a user without the device |
| Low point | a scored line on the mat / divot start | physical, per [low_point_metric_design.md](low_point_metric_design.md) §7 |

Ship the numbers to ball-only-LM users only after the studio comparison is done, and ship them
with the σ the comparison produced rather than a frozen constant.

## 10. The studio layout — two Chameleon3s and a phone

The studio has two Chameleon3s (one wall-mounted face-on, one on a tripod that has been the DTL
camera) and iPhones that pair over PPCP. The intended layout:

| Position | Device | Role | Why this device |
|---|---|---|---|
| **Face-on** | wall-mounted Chameleon3, 1280×1024 @ 150 fps | unchanged: pose, shaft, ball ruler, `lowPointAhead` | already mounted; global shutter; the pipeline was tuned on its frames |
| **Impact** | tripod Chameleon3, **640×240 crop @ 591 fps, 50–70 µs, ~1 mm/px** (§10.2; 612 fps at ≤ 224 rows, any width up to 1280) | §4 face-on zoom: attack angle, low point, high/low strike | global shutter; exposure floor 6.4 µs; the ROI rate is measured, not assumed (§3.1); the ROI path already exists (§3); on the **same bearing** as the face-on camera, so the impact inset overlays the wide arc with no transform |
| **DTL** | iPhone via PPCP, portrait 1080×1920 @ 240 fps | pose, shaft plane, the rotation and over-the-top family | portrait suits the DTL frame; ~2× the vertical pixel density of the Chameleon3 over the same field; 4.2 ms exposure is shorter than the Chameleon3's current 6.6 ms; pairing, host-driven capture, the clip leg and sync convergence all work as of September 2026 |

**Impact camera mount** (revised 2026-09-15; tolerances in
[camera_calibration_design.md](camera_calibration_design.md) §4.8).
- **Where:** a floor mount (a plate or a clamp arm on a floor block), not a tripod at its lowest
  setting. Face-on side, the ball about 60 % of the way across the frame so most of the width is
  before impact.
- **Level and square:** lens between ball height and ~100 mm above the mat, level within 3°, square to
  the target line within 5°.
- **What went wrong on 15 Sept:** a tripod as low as it would go, looking down at the ball. That lost
  ~9° of launch angle to the tilt, and the session was set aside.
- **Calibration:** the ChArUco card is solved in place (§4) before any metric is read.

A 640-px crop is half the present width, so the
existing lens at about **two thirds of the face-on camera's distance** gives ~1 mm/px and a
~640 × 240 mm field (§10.2). The rate does not care about width (§3.1), so the crop can be as
wide as the light allows: 1280×240 at the same distance is the full present width over a 240-row
band, still 591 fps, at 181 MB/s on the link. A camera on the face-on bearing under two metres
from the ball is where a sharp shank goes; the GCQuad lives in the same place and takes the
occasional hit, so a low mount and a small guard are the answer, not a different bearing.

**What this layout still does not give:** path, and therefore face-to-path (§5–§6). A DTL
phone cannot supply it either. The overhead remains the second step.

### 10.1 The DTL swap is conditional on an overlap session

Two things about the phone are not properties of the Chameleon3 it replaces, and neither is
something to assume:

1. **Rolling shutter.** Every iPhone camera is one. At delivery the head moves fast enough that
   a readout spread of a few milliseconds across the frame time-shifts the head relative to the
   grip — on a shaft spanning a third of the frame, on the order of a couple of degrees of shaft
   angle near impact. The tape-free shaft figures and the corridor/sign review were validated on
   global-shutter frames only.
2. **The 240 fps mode is a sensor crop with no per-frame intrinsics.** Narrower field than the
   120 fps mode on the same lens, so the phone stands further back than expected; intrinsics
   come from the one-shot 120 fps matrix, per
   [camera_calibration_design.md](camera_calibration_design.md) §3.4.

**The session:** keep the tripod Chameleon3 on DTL, add the phone on DTL beside it on the same
bearing, face-on as usual, a dozen swings with the lab 7-iron. Judge the phone's DTL against the
Chameleon3's **on the same swings** by (a) metric count — the only honest yardstick for a
pipeline run — and (b) the shaft band figures and P-anchor timings, both of which have corpus
baselines. A delivery-window shaft-angle offset that grows with clubhead speed is the rolling
shutter's signature. If the phone holds, retask the tripod camera; if it does not, both cameras
are still where they were and nothing has been lost.

### 10.2 The chosen mode — 640×240 at 591 fps, and why not 320×240 at 420

The design was written around 320×240 at 420 fps. With §3.1 measured, the chosen mode is:

| Setting | Chosen | Fallback |
|---|---|---|
| ROI | 640×240 crop, ball at ~60% across | 640×320 @ 454 fps if vertical framing is tight |
| Rate | 591 fps (the rows' maximum) | 454 fps |
| Exposure | 50–70 µs, locked | same |
| Scale | ~1 mm/px via lens and distance | same |
| Binning | off | off |

- **591 fps, not 420.** The rate is set by the row count alone, so any crop of ≤ 240 rows runs
  at 591 fps whether asked for or not. At a fixed 70 µs exposure a higher rate costs no light,
  and the clip is short (±100 ms ≈ 120 frames × 154 KB ≈ 18 MB), so the extra frames are free.
  The 420 estimate was right about what the fit needs — 4–5 head positions before the ball. At
  591 fps over a 400 mm run there are 6–7, the tangent fit tightens by roughly √(n), and the
  ball gets 2–3 frames in the field instead of 1–2, which sharpens the sub-frame impact instant
  (§4). The only reason to choose a lower rate deliberately is to buy rows.
- **240 rows, not fewer.** 224 rows buys 3.5% more rate for 16 mm of vertical field; not worth
  it. At 1 mm/px, 240 mm has to hold the mat, a teed driver ball, a driver head ~110 mm tall,
  and the arc's rise over the field (~35 mm over 640 mm at a 1.5 m radius). That fits without
  much spare. If a driver on a high tee will not frame, go to 320 rows at 454 fps — still above
  the design point, and the right trade. Never go below 240 rows for rate.
- **640 wide, not 320.** Width is free in rate, and 640 mm along the travel roughly doubles the
  pre-impact positions (from ~4 to ~7–8 at 61 mm/frame with the ball placed off-centre), so the
  arc's curvature comes from the data rather than an assumed radius. The costs are light — the
  extra area must be lit, or it is dark pixels that cost nothing — and data: 91 MB/s instead of
  45, a 5 s pre-allocated ring of ~450 MB instead of ~230 MB. Not 1280 wide: 181 MB/s and a
  ~900 MB ring for a strip the floods will not cover, and a 1.28 m field rises ~137 mm over its
  length, which eats the rows.
- **No binning.** Bin 2 gives the whole sensor at ~475 fps but halves the scale to ~2 mm/px. The
  fit gains linearly from scale and only as √(n) from rate (§2), so binning is the wrong side of
  that trade.
- **50–70 µs.** ≤ 2 px of blur at 1 mm/px (§1). The rate does not move anywhere in that range
  (§3.1). The 6.4 µs floor is irrelevant; there is no light for it.

What this does not settle is the light: at 70 µs the floods or strobe must bring a 640 × 240 mm
patch to a usable level on a Bayer sensor, which is the blur-and-level measurement §11 step 2
still asks for before any fitting is written.

**In Settings → Cameras (built 2026-09-15).** The VIEW selector gains **Club/Ball Impact**. It is
offered only to a camera that reaches 420 fps at some crop, which the backend establishes at
enumeration by probing the candidate crops (640×240, 640×320, 1280×240, 320×240, 640×480) on
the camera's own rate node — nodes only, no acquisition — and publishing each one's advertised
maximum. Exactly one camera holds the placement: assigning it clears it from any other. On first
assignment the row seeds the recommended mode and a 70 µs exposure; the row then shows IMPACT
MODE chips (crop × rate, ★ on the recommended one) in place of the frame-rate chips, and an
EXPOSURE chip row (30/50/70/100 µs). A mode sets the crop *size* and the rate; the crop's
*position* is placed in the ordinary crop editor. All three apply at the next connect, in the
order ROI → exposure → rate (§3.1's stale-max and exposure-clamp rules), and the ring is sized
for the mode's rate rather than the 200 fps GenICam default. Pose estimation is off for the
impact camera. The rate the chips show is the camera's advertised maximum; the delivered rate
(§3.1's table) is what the tile's live counter reports.

**What is kept, and how it replays (built 2026-09-16).** The impact camera's frames go into the
ring and the 4 s swing window like any other camera's, and the window needs no change: a lane
may cover any sub-range of the window. The trim is at **export only**, the one exception to the
"exports are never trimmed" rule: `ShotProcessor::buildSwingExportJob` gives an Impact-perspective
camera a keep band of **impact − 200 ms .. + 100 ms** on the arbiter's instant (the physics needs
~40 ms before and ~30 ms after; the rest is for the anchor, which runs 13–22 ms early and, rarely,
hundreds late — `impact_geom.h`), widening to ± 500 ms if the band catches nothing and keeping the
whole lane if even that is empty. `SwingExporter` drops entries outside the band and writes the band
as the stream's `clip` object in swing.json. ~180 frames at 591 fps: a 6 s file at the 30 fps
container rate instead of 79 s, ~21 MB of raw sidecar instead of ~360. On replay the clip is never
the master and never widens the span; it **loops** at the same capture-time speed as the other
tiles (quarter speed ⇒ one loop per ~1.2 s), re-phased every tick so that its impact frame is on
screen at the moment the playhead crosses impact and the loop runs around it otherwise. The live
post-shot replay does the same with its in-window track. Frame stepping steps the master; per-tile
stepping at the impact camera's own rate (§7) is still to do.

**An iPhone over PPCP is not a candidate,** and the selector does not offer it one. Two of §3's
requirements fail on the device, not on the transport: every iPhone camera is a rolling shutter
(§10.1), and the fastest declared profile is 240 fps at 1080p, which §2 puts at two head
positions in the field — a mean direction, not a tangent. The shortest exposure it locks is
1/8000 s (125 µs), above the 70 µs budget besides. A phone stays the DTL camera.

### 10.3 Tuning to the room — what the first recordings showed, and what was built (2026-09-15)

**The first session.** Thirteen swings on the tripod Chameleon3 in the §10.2 mode, eight at
70 µs and five at 102 µs, under a ring light aimed at the ball plus the room's floods. The ball
was plain in every clip and the club was not — until the frames were stretched 12×, at which
point the shaft, the head and the ball's dimples were all there, sharp, at 592 fps. The levels
were the whole story:

| Thing in the frame | Level, of 255 |
|---|---|
| The mat | 5–8 |
| Club head and shaft body | 10–30 |
| Specular glint on the shaft | 100–250, some frames only |
| The ball | 100–250 |

So the camera was two orders of magnitude short of light, exactly as §1 warned, and the
ring light could never close it: full scale at 70 µs and f/2 needs direct-sunlight illuminance
on the patch (over 100 000 lux); a ring light spread over the mat from a metre away is around
1 000. The ball only shows because it is matte white and returns the on-axis light from any
angle, where polished steel returns it only in the mirror direction. Two things compounded it:
the app never set the camera's **gain** (the driver enumerated the node and nothing wrote it),
and the clip's encoder ran at the library's default quality, which quantised a frame this dark
into 16-px macroblocks at ~137 kbps — nothing in post could get that back. Two of the thirteen
were not swings at all (the view blocked, and the ring light sitting in front of the lens), and
in every measurable swing the ball left the frame 3–6 frames (5–10 ms) *before* the acoustic
anchor; that timing question is open and separate.

**What the impact row in Settings → Cameras now carries.** The mode chips (crop size, rate,
resolution) and Set crop stay on the row; everything below sits behind a **TUNING** disclosure,
closed by default — it is for the operator tuning a room, not for choosing a camera, and shown
always it overwhelmed the row (Mark, 2026-09-15). All persisted per camera and, for
the ones the camera holds, written the moment they are clicked while it streams (`applyLiveTuning`
on the backend's thread — ExposureTime, Gain and Gamma are all writable during acquisition) and
primed again at the next connect:

- **Gain** (dB; auto-gain off). Applied before the ADC, so it lifts a club body at 25 above the
  8-bit floor instead of stretching a floor that is already there. Chips run to the camera's own
  maximum (`gainMaxDb` from the capability query). The row also shows what the camera *holds*,
  read back after the write, because the node clamps.
- **Gamma** (in-camera). Applied on the sensor's full bit depth before the 8-bit output, so a
  value below 1 lifts the shadows the club lives in while the ball stays where it is — a better
  "software gain" than any multiply on 8-bit data.
- **Exposure** as a free value beside the 30/50/70/100 chips: once gain is in play the right
  exposure is whatever the blur budget allows for the club in hand.
- **View gain** (×1–×16). A display stretch on the tile and on replay (`viewgain.frag`, a
  multiply clipped at white over the video item), never in the recorded pixels. What lets a
  70 µs tile be aimed at all. Stamped into the clip so replay shows what live showed.
- **Levels.** Median (the mat), 99.9th percentile (the brightest thing that is not a hot pixel)
  and the clipped fraction of the *raw* frame, a few times a second, on the tile's stats pill and
  on the row. Without a number, tuning the light is guessing; the target is the mat under 40,
  the peak near 250 with almost nothing clipped, the club body then around 100.
- **Strobe.** Line1 as an output carrying ExposureActive (`LineSelector`/`LineMode`/
  `LineSource`), for the LED strobe §11 step 4 names. Next connect.
- **Note.** Free text — lens, aperture, light — stamped into every clip, since a clip's date
  says when it was made and nothing else about the room.

**Defaults, from the table above:** gain **12 dB** (4×: the club body from ~25 to ~100 at
70 µs), gamma **0.7**, view gain **×1** (the two above already put the club where the eye can
see it), strobe off. The ball clips at any gain that makes the club visible under a ring light;
that is the ring light's geometry, not a reason to lower the gain — floods change the ratio.

**Provenance.** The stream's `capture` object gains `gainDb` + `gainSource` (`applied` when
read back from the camera, `requested` otherwise), `gamma`, `strobe`, `viewGain`, `note`, and
`measuredFps` — the median inter-frame interval of `frames.t_us`, beside the nominal
`fps_num/den`. The nominal is now the rate the camera was *asked* for rather than what its
frame-rate node read at start: the node reported 30 for five of the session's clips while the
frames arrived at 592, and the stamped rate said 30. The impact clip also encodes at its own
quality (CRF 12, or lossless when the library is): ~180 frames of 640×240, a few MB.

**Replay.** The picture-in-picture has its own transport along its bottom edge: play/pause
holds the loop on the impact frame, ◂ ▸ step one clip frame, and a slider scrubs the loop band
by hand. The clip loops at a FIXED 1/50 of real time over the band its track marks (the club
and ball in view, four frames of context), not at the window's speed: the interesting part is
~60 ms of capture time, and at the window's ×¼ or ×1 it was a flicker.

**The second session, same day, at 12 dB / 0.7 / 102 µs (wedge pitches, 40–50 yards):** the club is legible in the
raw frames — shaft, head shape, the ball's dimples — and the CRF 12 clip has no macroblocks. The
levels moved the wrong way for a different reason: the ring light was off the ball, so the mat sat
at 6–8, the ball's peak at ~58 and nothing clipped; the frame uses the bottom quarter of its
range. The camera's own answer is the rest of its gain (the chips run to the node's maximum) and,
for a full driver swing, a *shorter* exposure than 102 µs (§1: ≤ 50 µs for 2 px at driver speed) — both of
which need the light §1 describes before they cost nothing.

**The track (built 2026-09-15, `ImpactRunner`, analysis stage "Impact").** From the clip alone:
a per-pixel median background over the opening frames (the resting ball is part of it) with a
spread mask for anything that flickers at the frame edge; the resting ball as the bright round
blob in that background, whose 42.67 mm diameter scales the frame; foreground as frame −
background over a noise-scaled threshold; the club as the largest non-ball component, its
principal axis the shaft and the shaft's low end the **hosel** — the tracked point, because it is
on every frame the shaft is, whereas the head body is dark under a 100 µs exposure and shows only
as an edge that comes and goes (kept as a separate marker when it does); departure as the first
frame the resting ball's spot goes dark; the ball afterwards as the round blob nearest its
predicted position — and then the path is **synthesised**, not fitted through detections
(Mark, 2026-09-15: detection is too error-prone for the club to be highlighted frame by frame;
the space the head travels through wants a smooth curve and a feedback loop). Robust quadratics
in time for the hosel and the shaft angle over the run (30 frames before departure to 3 after,
edge frames predicted but never fitted, hosels more than max(8 px, 3 MAD) off the fit dropped —
the club touching the ball shortens the shaft and the "hosel" jumps up it); the head as a rigid
offset from the hosel in the club's own frame, the shaft tracker's trick, self-calibrated by a
feedback loop: predict the head disc on every frame from the smoothed hosel and angle, gather all
WEAK foreground inside it (sole and crown glints never seed a component on their own), take the
centroid, re-estimate the offset as the median over frames, twice; a generic 30/35 mm prior when
fewer than three frames show anything. On the 2026-09-15 session — 40–50 yard pitches with a wedge, not the driver the capture's club
label claims (corpus club labels are unreliable; Mark confirmed the club. The 45–55 mph ball speed
the track gave was read on the uncalibrated ball ruler and settles nothing — see the evening note at
the end of this section)
— the loop finds ~37 mm along the shaft and a few mm across on every swing, which is a wedge blade
from its hosel; the agreement between swings is the evidence. Written as `analysis.impact` and drawn on the
impact tile at the clip's **own** playhead (it loops, and can be held). **v0.1 (Mark, end of 2026-09-15): the central arc alone, translucent,
this frame's synthesised head as a ring with a cross riding it, and the ball's outline — at half
opacity, so the footage reads first.** The band of head half-width, its edges, the ghosted heads,
the raw shaft/hosel and the hull are all in the data (`analysis.impact`) and the lab, not on the
tile. And the model is an ARC IN SPACE, not a fit in time: the hosel's y as one robust quadratic
in x over the whole run (1.4–3.3 px rms on all thirteen studio swings — the proof it is an arc),
the shaft angle as one robust line in x, and time only placing the club along the arc per frame
(a local fit of x(t)). The first version fitted the angle locally in time and it swung 15° over
the contact frames, which at 35 px of head offset put a 9 px wobble in the head — the "jittery
noise" of the first screenshots. The head BODY is
at mat level under a 100 µs exposure and never appears in the difference image (measured):
nothing is drawn as a head silhouette until the head is lit. All of this was proven first in
`tools/impactlab/impact_review.py` (contact sheets + a metrics line per swing; `--from-json`
checks the C++ stage against the lab), the shaftlab rule. On the first thirteen swings: the resting ball on all thirteen
(1.12–1.15 mm/px), departure seen on all, and the departure ran 5–10 ms before `capture.impactUs`
on every one — the acoustic anchor question is now measurable per swing. The loop itself now runs three times slower than
the window's capture-time speed — the six head positions before the ball are 40 ms of capture
time, unreadable at the old rate. What this does not yet do: attack angle, low point and
high/low strike from the arc's tangent at the ball (§4), and the ball's own launch direction and speed
from the post-departure samples. **These wait for the card calibration** (evening note below).

**The first graded attempt, evening of 2026-09-15 — set aside, and what it taught.** Seven full
pitching-wedge swings with the GCQuad, camera on a tripod as low as it went, looking down. Mark set
the session aside as a rig mistake, not data.

What it established, all of which the design now carries:
1. **The resting-ball scale is not a scale.**
   - The detected resting diameter was well under the ball's real image, and ball speed read 1.3–1.5 ×
     the monitor.
   - Even a careful edge measure moves ±10 % with the threshold on a dim, top-lit ball.
   - → the card calibration (§4, calibration §4.8); the ball kept as a relative ruler.
2. **A tilted camera loses the angle.** Launch read ~9° low on every swing. → the level floor mount
   and its tolerances.
3. **Pairwise ball speeds are useless** on these clips. Per-frame timestamps jitter ±0.4 ms (1.2–2.2 ms
   intervals against a true 1.69 ms) while the ball moves an even 38–39 px per frame, and centroids
   alternate between blob and predicted-disc detections. → a robust line against frame index × median
   period (§4 table).
   - **Root cause, found 2026-09-16 and fixed at source:** the jitter was not the camera. Frames were
     stamped when they reached the app, after the transfer, the driver and a queued hop; the camera's own
     timestamps are steady to 1–2 µs. Clips recorded from now on carry the camera's clock
     (`event_buffer_design.md` §9, `capture.timestampSource`), so the frame-index workaround is belt and
     braces rather than a necessity — and ball speed from these older clips still needs it.
4. **Framing decides the club.** With the ball ~40 % across, the club showed on only 4–6 frames (one
   swing none). → the ball ~60 % across.
5. **Club speed from `path.points` sags into departure.** The loess x(t) smooths across the contact
   step. → a one-sided pre-contact fit (§4 table).
6. **Attack angle must be the head's direction, not the hosel arc's.** θ turns with x (−2.5° hosel
   against ≈ −3.7° head on one 15 Sept pitch).

**Still ahead:** the light itself, which no setting supplies: §1's two 100 W floods at half a metre concentrated on the
patch, a large diffuse source near the camera axis rather than a small one so the club's mirror
reflection lands in the lens from more head orientations, or a lit white board behind the ball
for a silhouette that does not depend on the club's finish at all.

## 11. Order of work

1. **Overlap session** (§10.1) — decides whether the tripod camera is free.
2. **Impact camera** — a floor-mounted Chameleon3, level and square (§4; not a tripod at its lowest),
   as the §4 face-on zoom, in the §10.2 mode (640×240 at 591 fps, 50–70 µs).
   - **Passed:** the ROI frame-rate gate (§3.1: 591 fps at 240 rows, no dropped or duplicated frames
     on the bench).
   - **Still to gate on:** a measured blur ≤ 2 px.
   - **Superseded 2026-09-15:** the dropped ball is no longer the scale check. It verifies the vertical;
     the scale comes from step 2a.
   2a. **Card calibration** ([camera_calibration_design.md](camera_calibration_design.md) §4.8, stages
       13–14). The ChArUco card is solved for pose in the flight plane, the ball's reference diameter
       stored, and the tag registered. **No speed or angle is read before it.**
   2b. **The first graded session** (calibration §11 item 16). Wedge and 7-iron, launch monitor on,
       labels correct. Then the estimators of §4's table, in plane millimetres, graded against `lm.*`.
3. **Elevated path camera (§5)** — a tripod 1–2 m up on the face-on side, looking down. It gives path
   and heel/toe, solved jointly with the face-on floor camera, **to validate and verify path
   estimates** (ours and `lm.clubPath`), and unlocks §6. A third camera, not a re-tasked one.
4. **Strobe** — only if continuous light cannot reach the exposure budget, or the golfer objects
   to the floods. Line1 on the Chameleon3 is a dedicated output that carries ExposureActive
   (§3.1), so the camera side needs no extra hardware.

Nothing in 1–4 touches the session wizard or the wide-camera pipeline.
