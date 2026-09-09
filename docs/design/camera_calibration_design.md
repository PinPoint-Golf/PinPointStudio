# Camera calibration — one ceremony, every camera, and the focus that makes it mean anything

**Status:** **DESIGN, not built.** Written 2026-08-28 after a day spent finding out that a phone's
preview had never once shown a frame, and that nothing anywhere measured whether a camera was in
focus. The slot it drops into already exists — `src/Gui/calibration/CameraCalibrationFlow.qml` is
an explicit stub for "stereo / ChArUco calibration", shaped like `ImuCalibrationFlow` and **already
instantiated by both `PpCameraPanel` and the session wizard** (§9.0) — so this needs no new screen
and no edit to either host.
**Scope:** how PinPointStudio establishes, records and *continuously verifies* what it knows about
each camera's geometry and focus — across Aravis, Spinnaker, USB and a PPCP phone — and how that
fits the flow of starting a session. ⚠ It deliberately covers the operator who has a printed
ChArUco board, the one who has a golf ball and a tape measure, **and the one who has two alignment
sticks and no intention of printing anything** (§4.4–§4.7).
**Not in scope:** clock synchronisation (PPCP owns it, §6.3), colour, and the analysis-side question
of which metrics may be computed at which tier — named here, decided elsewhere.
**Revised 2026-09-09:** a board-free geometry path. The first draft ran intrinsics and pose up one
ladder with the board at the top of both. That was wrong in a way that matters: intrinsics and
geometry are separate problems with separate sources, a board is a poor way to put three cameras in
one frame, and the operator with two alignment sticks can get most of what the board gives — and
can get *more* than the board gives for 3-D fusion, with a ball wand. §3.3 is now two ladders,
§3.4 says how intrinsics are fetched, stored, bound and used — and corrects the assumption that a
phone delivers them per frame, which is false at 240 fps — §4.4–§4.7 are new, §6.3–§6.4 are new,
and §10–§12 are revised to match.

---

## 1. Why this exists, and what it is actually for

Calibration is not one thing. It answers four separate questions, and a given operator may need
only some of them:

| | Question | Needed for |
|---|---|---|
| **Scale** | how many millimetres is a pixel, at the hitting plane? | clubhead speed, low point, ball speed |
| **Distortion** | is a straight line straight? | shaft angle, especially away from frame centre |
| **Pose** | where is this camera, relative to the ball and to the other cameras? | anything 3-D, and relating a camera to the hitting plane |
| **Focus** | is the image sharp enough to measure at all? | **all three of the above** |

⛔ **Focus is not a fourth item on the list — it is the precondition for the other three.** A
solve computed from soft corners returns numbers with small residuals and large errors, and
nothing about the arithmetic will tell you. This is why focus belongs *inside* calibration rather
than beside it in a camera-settings panel.

### 1a. What today actually does

Nothing. There is no calibration anywhere in PinPointStudio, and
`ppcp_source_declaration.cpp:457` declines to offer one for exactly the right reason:

> *"No `calibration` either, until the rig exists — 5.9's uncertainty is mandatory, so there is no
> way to offer a calibration without one."*

On the phone side, `AVFoundationCaptureDevice` locks focus after an **800 ms** bounded wait and, on
timeout, `waitForConvergence` simply `return`s — so `lockControls` locks wherever the lens happened
to be **mid-hunt**, silently. `focusPointOfInterest` is never set, `lensPosition` is never read or
recorded, `setFocusModeLockedWithLensPosition` is never called, and nothing re-asserts the lock
after an interruption. A camera can therefore be soft for an entire session with no signal at
either end.

---

## 2. Principles

1. **The protocol already has the model. Use it for every backend.** `CORE` §5.9 defines a
   Calibration as `{id, source_id, kind, parameters, uncertainty, method, observed_at}` with
   `uncertainty` **mandatory** and `method ∈ {factory, per_frame, solved, user_measured,
   estimated_online}`. That is the right shape for a FLIR camera as much as for a phone, and
   adopting it internally means the PPCP half is a serialisation rather than a translation.
2. **Uncertainty is not optional and not decoration.** A calibration without an error bar cannot be
   verified, cannot be compared to a later one, and cannot tell a metric whether to trust it. The
   protocol makes this impossible to skip; we should be glad.
3. **Gate on capability, never on backend or session type.** What a camera can do about focus is a
   property of the camera, not of its driver's name or of what kind of session is running. (The
   same rule the analysis pipeline had to learn.)
4. **PinPointStudio owns the ceremony and the record. The device owns the measurement.** Where the
   full-resolution pixels are is where the solve happens.
5. **Establish rarely, verify constantly.** A calibration is a bay-level asset with a long life. A
   session *checks* it; a session never *does* it.
6. **Degrade explicitly.** An operator with no board still gets scale and focus. What they do not
   get is recorded as not-got, per swing, and analysis reads that rather than guessing.
7. **Never block a golfer.** A red calibration is declared, recorded and shown — it does not refuse
   the session. Someone hitting balls beats a perfect rig.

---

## 3. The model

### 3.1 Bay

A **Bay** is a persistent, named physical setup: the cameras in it (by role and identity), their
calibrations, the reference geometry, and the board definition in use. A session *attaches to* a
bay.

⚠ **This is the single decision that makes the session flow work.** Calibration belongs to the bay,
because that is the thing that is actually stable: the phone on its mount, the FLIR on its tripod,
the mat, the ball position. Sessions come and go against it.

### 3.2 Calibration record

One record per `(bay, camera identity, kind)`, shaped on 5.9:

```
Calibration {
    id            stable, ours
    sourceId      the camera it describes
    kind          intrinsics | extrinsics | focus        (5.9's registry is open)
    parameters    kind-specific map
    uncertainty   MANDATORY, kind-specific
    method        factory | per_frame | solved | user_measured | estimated_online
    observedAt    when it was measured
    validFor      ⚠ see §5.4 — a lens position, for a camera whose focus can move
}
```

**Camera identity** is the serial number for Aravis/Spinnaker/USB, and `peer_id + source_id` for a
PPCP camera — 8.5c scopes ids by the minting peer, so a phone's `src:camera:wide` is only unique
alongside its peer id.

### 3.3 Two ladders, not one — what an operator can get with what they have

⚠ *Revised 2026-09-09.* **Intrinsics are a property of the lens; geometry is a property of the bay.**
The first is established once per camera and lens position and lives for years. The second is
re-established whenever a tripod moves, and must put every camera into the *same* frame. Nothing
lying on the floor can ever supply the first (§4.4), and a board is the worst available way to
supply the second to three cameras at once (§6.3). So there are two ladders, and **every swing
records its rung on both.**

**Intrinsics — per camera, per lens position, long-lived:**

| Tier | Method | Source | Gives | Does **not** give |
|---|---|---|---|---|
| **I0** | — | nothing | pixel measurements only | scale, distortion, pose |
| **I1** | `factory` / `per_frame` | what the device states about itself at enumeration: a phone's per-format field of view, a machine-vision camera's pixel pitch **plus a typed lens focal length** (§3.4) | `f`, `cx/cy`, per format | distortion, an error bar better than the vendor's |
| **I2** | `solved` | a printed ChArUco or checkerboard, **once** per camera + lens | `f`, `cx/cy`, distortion, reprojection RMS | — |

⭐ A phone is born at I1 and never needs the board unless a metric demands distortion. A FLIR with a
fixed C-mount lens reaches I1 the moment the operator types the lens's focal length, and is solved
to I2 once and cached forever — **per-session intrinsics are pointless for a lens that cannot
move.** Where both exist, they cross-check (§8.1). §3.4 says exactly when each is fetched, where it
is stored and how it is used.

### 3.4 ⭐ Intrinsics — fetched at enumeration, bound on connect, verified at session start

*Added 2026-09-09, after the question "fetch at enumeration, or put them in the settings panel?"
The answer is both, and neither is a ceremony: enumeration is the **time**, the Cameras panel is
the **home**, connect is where the record is **bound**, and the session **verifies** it.*

⚠ **First, a correction the whole section rests on.** The August draft assumed a phone's intrinsics
"arrive free, per frame". PinPointCapture measured on 24 August (iPhone 16,
`Tests/DeviceSessionTests.swift`, `intrinsicsAvailabilityByFormat`) that intrinsic matrix delivery
is available at 1080p 30/60/120 fps and **absent at 1080p 240 fps** — the ranked best mode, and the
one this product wants. So per-frame delivery is a bonus where the platform offers it, never the
plan, and **the plan is to read intrinsics once, at enumeration, from what the device can state
about itself.**

**Fetched — at enumeration, per format, no open device.** Intrinsics are a device property, and
enumeration is when device properties are read. What each backend can state:

| Backend | Read at enumeration | Cannot be known by the device |
|---|---|---|
| PPCP phone | `f` per format from `AVCaptureDeviceFormat.videoFieldOfView` — static, every format including 240 fps, no capture session; a one-shot matrix at 120 fps on the same lens for `cx/cy`, computed once on the phone and cached | nothing |
| Spinnaker / Aravis | `DeviceModelName`, already read from the transport nodemap *before* the camera is opened — a per-model pixel-pitch lookup fills `CameraCapabilities::sensorWidthMm`, which exists today and which nothing populates | **the lens.** A C-mount lens is invisible to the camera; its focal length is typed in the Cameras panel |
| USB / Qt Multimedia | nothing | everything; it is I0 and says so |

⭐ For a phone, enumeration *is* connect: the declare arrives when the link comes up, and 5.6's
`Source.calibration` (0..1) is the slot — `kind: intrinsics`, `method: factory`, one entry per
profile, uncertainty stated. PinPointCapture does not populate it and PinPointStudio does not consume
it; both are small changes and neither is a protocol change.

⛔ **Fetch only on connect and two things break:** the Cameras panel cannot show or accept a focal
length for a camera that is enumerated but not selected, and the wizard cannot say which tier a
camera *would* give before it is chosen.

**Stored — one record per `(camera identity, format)`, in the bay store, shown in the panel.**

- The record is the §3.2 shape with `kind: intrinsics`. `parameters` holds `fx, fy, cx, cy` in
  pixels **for that format**, plus the provenance of each: `f` from vendor field-of-view, from
  pitch × typed focal length, or from a solve; `cx, cy` assumed at the image centre until a solve
  says otherwise.
- ⛔ **Never one matrix per camera.** A 240 fps mode commonly uses a sensor crop, so its field of
  view is narrower than the 120 fps mode on the same lens; a binned mode halves `f` in pixels. For a
  machine-vision camera store the invariants — pixel pitch and lens focal length — and derive `f`
  per mode from the binning the format declares. For a phone store what each format declares.
- `validFor` carries the lens position where the camera has one (§5.4). A FLIR's is "the ring as
  verified"; a phone's is the locked `lensPosition`.
- `uncertainty` is **mandatory** (5.9) and a vendor value earns a real one: a field of view rounded
  to a tenth of a degree and a principal point assumed at centre is ±2–3 % on `f` and ±1 % of width
  on `cx, cy`, stated as such. A typed focal length is `user_measured` and inherits whatever the
  lens's label is worth — nominal focal lengths on cheap C-mount glass are off by up to 5 %.
- **The Cameras panel is the home.** Its capability strip already shows sensor, resolution, pixel
  format and bit depth; intrinsics are a sixth cell, showing the value in force, its provenance and
  its tier, with the focal-length field beside the existing *Fixed in place* card for a camera
  whose lens it cannot see. An operator can override any device-stated value; the override is
  recorded as `user_measured` and the device value is kept for the cross-check.
- An I2 solve (§4.1) **supersedes** the I1 record for the same key rather than replacing it: the
  factory value stays as the independent estimate §8.1 cross-checks against.

**Bound — on connect, or more precisely at stream open.** The value in force cannot exist until
the camera is configured, because it depends on the active format and, for a phone, the lens
position. So connect is where PinPointStudio:

1. resolves the catalogue entry for the chosen mode and applies its binning or crop;
2. stamps the lens position as `validFor`, once focus is locked (§5.2, §5.3);
3. mints the calibration `id` and digest the stream and every swing carry (§9) — which is exactly
   PPCP's rule that a Stream's `calibration_id` is fixed for its lifetime and a change closes it
   (5.9a, 5.11a).

**Verified — at session start and continuously,** as §9 and §7 already describe: identity matches,
the bound record's format is the active format, the lens position is the calibrated one, and for
a phone a per-frame matrix, where the platform happens to deliver one, agrees with the bound record
to within its uncertainty. Disagreement is reported, never averaged away.

**Used — by two consumers, and only through the bound record.**

- **Every geometry solve (§4.4–§4.6) takes `f` and `cx, cy` from the bound record.** A
  vanishing-point rotation needs `f`; a bundle adjustment needs the full matrix as its starting
  point and may refine it. The solve records which intrinsics `id` it consumed, so a later I2 solve
  invalidates the G record built on the I1 one — and says so, rather than silently mixing them.
- **Every metric reads the `(I, G)` pair** on the swing (§3.3). Distortion-sensitive measures — a
  shaft angle far from frame centre, anything triangulated at the edge of the field — read I ≥ 2
  and are withheld with a reason at I1.

**Geometry — per bay, re-done when something moves:**

| Tier | Method | Reference | Gives | Does **not** give |
|---|---|---|---|---|
| **G0** | — | nothing | — | scale, pose |
| **G1** | `user_measured` | any known-size object at the hitting plane (§4.2) | scale at that plane, ±2–3 % | pose |
| **G2** | `solved` | **two alignment sticks in a T on the ground**, plus a role declaration (§4.4) | a bay frame with its origin at the ball and axes on the target line and the ground; rotation ~0.15°; scale ~1 %; **position ±25–50 mm** | the swing volume; anything that needs sub-degree 3-D |
| **G3** | `solved` | G2 **+ one vertical stick**, a measured camera height, and gravity on a phone (§4.5) | as G2 with position ±5–10 mm | sub-mm 3-D |
| **G4** | `solved` | a ChArUco board left at the ball, seen by each camera in turn (§6.1) | position ±1–4 mm *at the board*, degrading with distance from it | the volume; a joint solve |
| **G5** | `solved` | G2 **+ a ball wand** swept through the hitting volume (§4.6) | a joint bundle over every camera, < 0.3 px RMS, sub-mm through the volume the swing actually occupies | — |

⛔ **Every swing records `(I, G)`.** A metric that needs scale reads G ≥ 1 and refuses otherwise; a
metric that needs the volume reads G ≥ 3; a metric that needs sub-degree 3-D — club path,
face-to-path — reads G = 5 and is otherwise **withheld with its reason shown**, never produced in
fictional millimetres. This is the same discipline as `5.8f`'s "a value must not be used to mean
unknown", and the same "no producer yet" state the diagnostics model already treats as normal.

---

## 4. The reference object

The ceremony is the same shape whatever the operator owns; only the reference differs. The flow
asks, once: **what have you got?** — and presents the answer as two setups rather than a parts list:

- **Quick setup** — two alignment sticks, a typed camera height, and gravity from a phone. Unlocks
  overlays, the plane, sequencing and approximate speeds. Metrics needing sub-degree geometry stay
  greyed out **with the reason shown.**
- **Precise setup** — a printed ChArUco, or the wand for 3-D. ⚠ A board is not expensive: an A3
  print glued to card. Its cost is *faff*, not money, and the choice should be framed to the
  operator that way, so they upgrade when a diagnostic they want demands it — not before.

### 4.1 ChArUco board — the precise path (I2 intrinsics; G4 geometry)

A checkerboard with ArUco markers in the white squares. ⛔ **Preferred over a plain checkerboard for
one decisive reason:** every corner has a unique identity, so a *partial* view still contributes.
A board large enough to fill a face-on camera at 3 m will not sit wholly inside a down-the-line
camera's frame, and a plain checkerboard contributes nothing unless the whole board is visible.

- **Definition**: squares across/down, square size in mm, marker size in mm, dictionary. Entered
  in-app (⛔ no file dialogs — house rule), with a couple of printable presets.
- **Plain checkerboard is still supported** for an operator who already has one; it simply requires
  the whole board in frame, and the flow says so.
- **What it is for now.** Intrinsics with distortion (I2), once per camera and lens — and ±mm pose
  at the ball (G4) for the operator who wants it. It is no longer the only route to a bay frame:
  §4.4 gets there with two sticks, and §4.6 gets *past* it for the swing volume.

### 4.2 Known-size object — scale for everyone (G1)

The operator nominates something of known size at the hitting plane and draws across it:

- ⭐ **a golf ball — 42.67 mm, and the best reference in the building.** It is spherical, so its
  projected diameter is independent of orientation; it is high-contrast against a mat; it is at
  exactly the plane that matters; and **it is present in every single shot.**
- an alignment stick (1.2 m), an A4 sheet (210 × 297 mm), a club of measured length, or a tape
  measure laid on the mat.

Uncertainty comes from the detection: a ball ~40 px across measured to ±1 px is ±2.5 % scale. That
is a real number and it must be carried, not rounded away.

### 4.3 ⭐ The ball is also a permanent, free verifier

Because a ball is in frame at address for every swing, and PinPointStudio already detects it with a
radius (`CameraInstance::ballRadius`), **every shot carries its own scale check at no cost**. If the
ball's diameter in pixels at address drifts while the bay is nominally unchanged, either the camera
moved or the ball is not where it was. That is a calibration alarm no ceremony could provide, and it
runs forever.

### 4.4 ⭐ Alignment sticks — the geometry path that needs no printer (G2)

Two alignment sticks laid on the ground in a **T**: one on the target line through the ball, one
perpendicular to it through the ball, **ball at the intersection.** Every camera in the bay can see
the floor, so every camera sees both — the face-on, the down-the-line, the rear diagonal, and the
elevated DTL that a board can never satisfy at the same time as the others.

**Sticks, not clubs.** An alignment stick is a consistent 48 in / 1.219 m, a uniform-diameter
cylinder with no head. A club's length is ambiguous — measured along the shaft to the sole? to the
heel? — and varies 34–45 in, so asking for it asks the user for a number they will get wrong. A club
is an acceptable fallback *if the user types a length*, and its uncertainty says so.

⛔ **What sticks can never give is intrinsics.** Everything on the ground is coplanar, and one view of
a plane does not constrain focal length. `f` must come from the intrinsics ladder — the phone's
declared field of view, a typed lens focal length or a one-off board on a FLIR (§3.4). That is not a weakness of the path; it is the
split that makes the path work.

**Design to the primitives, not to the endpoints.** The T offers four geometric primitives of
wildly different quality, and the solve must be built around the good ones:

| Primitive | Localisation | Used for |
|---|---|---|
| line direction of each stick | **excellent** — a line fit over ~600 px of straight, high-contrast shaft | two orthogonal vanishing points |
| intersection of the two lines | **excellent**, ~0.5 px | the origin: the ball |
| two orthogonal vanishing points, with `f` known | **good**, closed form | **full rotation** |
| the four endpoints | **poor**, 5–15 px — a rounded stick end, or a clubhead that is a 3-D blob | **scale only** |

So: rotation from the vanishing points, origin from the intersection, endpoints for scale and
nothing else. ⛔ Never fit a homography to four sloppy endpoints and call it a pose.

⭐ **The bay's asymmetry helps.** The DTL camera looks *along* the target-line stick, so that
stick's vanishing point sits near the frame and is well conditioned. The face-on camera has the
perpendicular stick pointing at it, so *its* vanishing point is the good one. Each camera gets one
strong vanishing point either way. (With no intrinsics at all, two orthogonal vanishing points also
yield a rough `f` from `f² = −(v₁−c)·(v₂−c)`. It is noisy and degenerates as a vanishing point runs
to infinity: a sanity check against I1, never a calibration.)

**Ask the operator two cheap things.** Which camera is face-on, DTL or rear (the wizard already
knows the *view*), and **which end of the target-line stick is the target end.** Together they
resolve the plane's two-fold pose ambiguity, label which stick is which, and cost nothing. Ask.

**What it is worth — worked for 1920 px, ~60° HFOV (`f` ≈ 1660 px), a camera at 3 m:**

Angular error ≈ (σ / f) · (d / L) / √N.

| | σ | L | N | Rotation |
|---|---|---|---|---|
| sticks | ~4 px on an endpoint | 1.22 m | 4 | **~0.15°** |
| A3 ChArUco | ~0.2 px on a corner | 0.4 m | 24 | ~0.01° |

Fifteen times worse per point, but the sticks' three-times-larger extent claws most of it back.
0.15° is not disqualifying.

**Position is the problem, and it is the whole story.** A plane seen at grazing incidence is badly
conditioned along the view direction. Cameras 1–1.5 m up at 3 m see the floor at ~20°, and a flat
reference at that angle localises depth at roughly **±25–50 mm**. A board held at 45° or more
localises it at ±1–4 mm. That ten-to-thirty-fold gap is **coplanarity plus grazing angle, not point
count** — which is exactly why §4.5 breaks the plane rather than adding more sticks to it.

⚠ **The honest caveat.** Our face-on shaft detection sits at 0.49° RMSE. A 0.15° rotation error is
comfortably below that; a 40 mm position error becomes the dominant term in anything that
triangulates a clubhead — club path and face-to-path above all, the two things a golfer most wants
3-D for. **G2 is fine for overlays, plane visualisation, phase segmentation and ±5 % speeds. It is
not fine for face-to-path,** and the tier gate of §3.3 is what makes that a shown reason rather
than a wrong number.

**For a phone the solve runs on a capture, host-side.** PinPointStudio never sees more than a
640 × 360 preview live, and a vanishing-point solve from that is worthless. But the capture leg
delivers full-resolution frames, and the bound I1 record (§3.4) supplies `f`. So the sticks
ceremony for a phone is: **arm a calibration capture with the sticks in place, receive the clip,
solve on it here.** No on-device solver, no protocol change, and the FLIRs are solved from their own
full-resolution frames the same way.

### 4.5 Breaking the plane — what else helps, ranked by what it buys

The additional information the operator can give, ordered by value per unit of faff:

1. ⭐ **Gravity, from a phone's IMU.** Free — PPCP already carries continuous attitude and gravity
   on a `metadata` stream (`REQ-CLIP-1`, 5.11), and PinPointStudio currently discards it. At
   ~0.3–0.5° it pins roll and pitch against the horizontal directly, and gravity **+ one strong
   vanishing point + camera height + one known length is a full pose with no ill-conditioned step
   in it.** Nothing to add to the protocol; something to start consuming.
2. ⭐ **Camera height off the ground.** The single most valuable number a user can type. A tape
   measure to the lens centre is good to ±10 mm — better than the plane fit can do — and it fixes
   precisely the axis the plane fit is weakest on.
3. ⭐ **One vertical stick of known length** — pushed upright into the mat or turf, or held in a
   printed foot or a sand bucket at a declared position. This one addition breaks coplanarity and
   collapses the depth error from ~40 mm to ~5–8 mm. **If the operator will do exactly one thing
   beyond the T, it is this, not a third stick on the floor.**
4. **The ball.** 42.67 mm, spherical so its apparent diameter is orientation-free, and detected
   already (§4.3). Range from apparent size is weak — at 3 m and ~27 px across, ±0.5 px is ±2 % of
   range, ±60 mm — but it is a free consistency check on the plane fit, and it anchors scale at the
   one point that matters.
5. **The role and target-end declaration** of §4.4. Low metric value, high robustness value, zero
   cost. Required rather than optional.

Together, 1–3 are tier **G3**: sticks, a vertical, a tape measure, a phone's gravity if there is a
phone. Everything in it is already in a golf bag or a kitchen drawer.

**Other readily-available objects, for completeness.** Two separate needs hide in "what else
helps": **scale** is one scalar, and the whole system needs exactly one trustworthy length —
it propagates linearly into speeds (1 % → ~1 mph at 100 mph) and not at all into angles, so spend
the scale budget on one good object rather than five mediocre ones. **Volume** is the 3-D problem
of §4.6 and no ground object addresses it at all.

| Object | Known dimension | Verdict |
|---|---|---|
| alignment stick, **vertical** | 1.219 m, exact | the best improvised object there is: a thin high-contrast cylinder with a subpixel line fit and a defined tip — needs a base |
| A4 / Letter sheet | 297 × 210 mm, ±0.5 mm | dimensionally certain, dead flat, free, high contrast on turf; print a marker on it and it is a proper target |
| golf ball | 42.67 mm | scale anywhere it appears, detector exists; weak alone |
| hitting-mat edges | usually a known rectangle | good for the ground frame right under the ball; useless for volume |
| the golfer | their stated stature | spans 0.9–1.8 m and is in every frame, but joint centres are soft (±20–30 mm) — a scale prior and a drift check, never primary |
| room corners / net frame (a fixed cabin) | surveyed once | three orthogonal directions → three vanishing points → rotation **and** `f`; strong for a permanent bay |
| golf hole | 108 mm | outdoors on a green only |
| credit card | 85.60 × 53.98 mm (ISO 7810) | too small at 3 m; phone-close setups only |
| **golf bag** | 86–105 cm depending on type | ⚠ a **poor object**: soft rounded top, leans, compresses into the mat, dark and textureless, clubs sticking out; top localises to ±20–30 mm and its axis is not reliably vertical — worse than the sticks. But it is a rigid-ish, self-supporting, metre-tall thing already standing in the swing volume and visible to every camera: **an excellent mount.** Tape an A4 marker sheet to it and a bad object becomes a good one, placed exactly where §4.6 wants one. |

### 4.6 ⭐ The ball wand — for 3-D fusion of pose and shaft (G5)

**Ground references calibrate the ground.** The club at the top of the backswing is ~2 m up and
~1.5 m behind the ball; pose keypoints span 0.9–1.8 m. Extrapolating a ground-plane fit into that
volume amplifies its error non-linearly, and no number of floor sticks changes that. A calibration
object for fusion must **occupy the space the swing occupies.**

Commercial optical mocap solved this decades ago, and the tool is cheap: two or three golf balls
(or ping-pong balls) fixed on an alignment stick at a known separation — say 600 mm — held rigid by
**3-D-printed collars**. Wave it slowly through the hitting volume for 20–30 s while every camera
records.

Why it is the right tool for exactly this problem:

- it fills the volume with correspondences instead of calibrating one plane — which is what pose +
  shaft fusion needs and nothing static provides;
- a **bundle adjustment** over a few thousand wand observations typically lands under 0.3 px
  reprojection RMS and at sub-millimetre 3-D residuals in a 3 m volume — an order of magnitude past
  any static reference, and better than our 0.49° shaft detection deserves, which is the right way
  round;
- detection is already solved: white spheres, the existing detector;
- **one known length fixes scale for the whole rig**, and it is the one scalar §4.5 said to spend
  the budget on;
- it is a *joint* solve, so the per-camera pose errors that §6.3 warns add up in triangulation are
  instead solved together.

**Then the standard two-step.** Wand → relative extrinsics and scale. Ground sticks → world origin
and axes. The sticks are not wasted; they move to the job they are good at — defining the target
line and the ground plane — and stop being asked for metric precision.

**Caveats to design around.** The wand needs temporal sync across cameras: the FLIRs have a
trigger, and PPCP now converges to the 5 ms gate in seconds — at 150 fps with a ~1 ms residual and
a wand at 1 m/s, that is 1 mm, acceptable. **Rolling shutter on a phone is the real constraint**:
instruct slow, smooth motion when a phone is in the rig, reject frames above a wand-velocity
threshold, and note that `readout_ns` is declared and `rowInstantNs` already applied, so the smear is
correctable in principle.

### 4.7 The shaft and the golfer refine it for free

The club has a fixed length and sweeps the entire volume on every shot. A **rigid-length constraint
on the shaft endpoints across frames** is a legitimate term in a post-hoc bundle adjustment, and so
are limb lengths from pose. ⛔ Circular if used alone — a solve that only ever sees the thing it is
measuring can be consistently wrong — but **as a refinement on top of a wand or board solve it is
free accuracy,** and it self-corrects drift when a camera is nudged mid-session.

Its second role matters more than its first: the per-shot shaft-length and limb-length residuals
are a **calibration-health indicator** (§7.2). The failure mode of improvised calibration is not
inaccuracy; it is *undetected* inaccuracy, and this is what detects it.

---

## 5. Focus

### 5.1 Three capability classes, derived from the camera and not its backend

| Class | Cameras | What we can do |
|---|---|---|
| **A — programmable** | PPCP phone | read, set and lock `lensPosition`; sweep it ourselves |
| **B — mechanical** | Spinnaker, Aravis, most C-mount | nothing by API. A human turns a ring |
| **C — uncontrollable** | many USB webcams | AF may be running and cannot be stopped |

A `FocusCapability` descriptor — `canReadPosition`, `canSetPosition`, `canLock`, `isMechanical` —
selects the path. ⛔ Never branch on `Backend::`.

### 5.2 Class A: sweep, do not autofocus

Replace autofocus with a **deterministic sweep**: step `lensPosition` across a bracketed range,
capture a frame at each, compute a sharpness metric over the reference ROI, fit a parabola about
the maximum, and lock the sub-step peak with `setFocusModeLockedWithLensPosition`.

⚠ **The curve is the evidence, and this is the whole point.** Autofocus yields a number; a sweep
yields a number *and* a reason to believe it:

- a sharp unimodal peak → real focus, and its width is the uncertainty;
- a flat curve → the ROI had no texture; the result is untrustworthy and says so;
- two peaks → glass, a reflection, or a mirror in the bay.

### 5.3 Class B: guide the human, then verify

The software cannot turn the ring, so it does the two things it can. **Guide**: a live sharpness
bar with peak-hold — "turn until it peaks, then lock the ring". **Verify**: measure after, record
the value, and alarm if it changes. Mechanical focus does not drift on its own, so once verified it
stays verified until something is knocked — which is exactly what §7 watches for.

⚠ Also tell the operator the thing they can act on: **stopping the aperture down buys depth of
field**, and depth of field is what makes focus robust to a golfer moving.

### 5.4 ⛔ Focus and intrinsics are not independent

Changing focus **changes the intrinsic matrix**. That is why `REQ-OPT-2` locks focus for the
session, and why 5.11m makes a preview profile declare `intrinsics: none` — a decimated, rescaled
view has a different matrix and must not pretend otherwise.

**Therefore a Calibration is only valid at the lens position it was solved at.** The record carries
that position (`validFor`), verification checks it, and re-focusing **invalidates the intrinsics** —
which is what 5.11a1's `calibration_changed` close reason exists to say.

### 5.5 Measuring sharpness honestly

Sharpness metrics are content-dependent, so an absolute threshold is meaningless — a blank mat
scores zero however sharp it is. Two measurements, used together:

1. **Edge-spread on a step edge** — the 10–90 % rise distance, in pixels, across the ball's limb or
   a board square. This estimates the blur circle in physical units and is self-normalising,
   because a step edge is a step edge whatever the scene.
2. ⭐ **Its anisotropy, measured at several orientations around the edge.** Defocus blur is
   isotropic (a disc); motion blur is directional (a line). One measurement therefore says *which
   blur you have* — and separates the two failure modes that are otherwise endlessly confused.

⚠ **Both must be measured at address**, where the subject is nearly still, or motion blur
contaminates the reading. Pose or ball-presence detection is the right tool for spotting that
moment — used to decide *when* to measure, never *what* to measure.

### 5.6 ⚠ What "soft" often actually is

Two things masquerade as focus and must be ruled out before anyone re-focuses anything:

- **The preview cannot show focus.** A PPCP preview is 640×360 JPEG at quality 0.6, downscaled 3×
  from 1080p. That pipeline destroys precisely the high-frequency content focus consists of. It will
  look soft when the capture is pin-sharp. The tile says `Preview` for this reason.
- **Exposure, not focus, dominates blur at impact.** `setExposureModeCustom` is never called, so
  exposure sits wherever auto landed — indoors at 240 fps, at or near the full 4.2 ms frame
  duration. A clubhead at 40 m/s travels **~17 cm** during one exposure. No focus setting fixes
  that; more light and a shorter exposure do.

---

## 6. Multiple cameras

### 6.1 ⭐ The board stays put; the cameras take turns

An earlier version of this argument said extrinsics need the board seen by two cameras
simultaneously, which is awkward in a real bay. **It does not.**

Lay the board flat at the ball position and **leave it there**. Each camera observes it whenever it
likes and solves its own pose relative to the board. Because they all reference the same stationary
object, they all land in one frame — with no simultaneous view, no shared trigger, and no clock
alignment required.

Better still, the frame is **physically meaningful**: put the board's origin at the ball and one
axis along the target line, and the bay frame is the frame the golf metrics already want.

The two conditions are checkable, and §7 checks them: the board must not move between observations,
and no camera may move afterwards.

### 6.2 What each camera contributes

Intrinsics are per-camera and independent — each can be solved alone, in any order, at any time.
Only pose needs the shared board. So a bay can be brought up incrementally: a new camera added next
week is solved on its own and placed in the existing frame without redoing anything.

### 6.3 ⭐ Sticks put every camera in the same frame — and this is where they genuinely win

§6.1's argument holds even more strongly for the T on the floor. A shared *ground* reference gives
every camera a pose in one world frame with **no co-visibility requirement and no synchronised
board-waving at all.** Getting a board legible to a face-on, a DTL 90° away and a rear diagonal
simultaneously is awkward; raise the DTL on a pole and it is worse. Two sticks on the floor are
trivially visible to all of them, and the ball sits at the origin where the metrics want it.

The cost is the one §4.6 exists to pay: each camera's pose is estimated **independently**, so errors
do not cancel the way a joint bundle's do. A triangulated point inherits the *sum* of two independent
pose errors. At G2/G3 that is a known, recorded uncertainty; at G5 the wand's joint solve removes it.

### 6.4 The order of operations, for a bay that wants 3-D

1. **Intrinsics once** — read at enumeration (I1, §3.4), a one-off board where distortion matters
   (I2). Never per session.
2. **Wand, if the operator will wave it for thirty seconds** — relative extrinsics and scale (G5).
   Fall back to sticks + one vertical + camera height (G3).
3. **World frame from the T** — origin at the ball, one axis down the target line.
4. **Continuous validation** — shaft-length and limb-length residuals per shot (§4.7, §7.2). If the
   residuals drift, prompt a re-do rather than silently reporting bad numbers.

---

## 7. Verification and invalidation

Establishment is a ceremony. **Verification is continuous, cheap, and is where the value actually
is.**

### 7.1 A static fiducial patch

Put a small high-frequency target (a Siemens star or a few checker squares) permanently in frame at
roughly the hitting plane — the edge of the mat, or the bay wall. Because it never moves, its
sharpness is a **pure** focus signal: no motion, no content change, no inference needed. One small
ROI per frame.

⭐ It catches two faults for the price of one: focus moving, and **the camera being bumped** — which
is at least as dangerous and currently invisible.

### 7.2 The checks, and what each costs

| Check | Cost | Catches |
|---|---|---|
| fiducial sharpness | one small ROI/frame | focus drift, camera bump |
| ball diameter at address | already computed | scale drift, camera moved, ball moved |
| `lensPosition` / `focusMode` poll | one property read/s | a lock silently lost |
| board glimpse (opportunistic) | only if the board is in frame | pose drift |
| ⭐ shaft-length residual, per shot (§4.7) | endpoints already tracked; needs G ≥ 2 and two cameras | pose or scale drift **in the volume**, a nudged camera |
| limb-length residuals from pose, per shot | keypoints already computed | the same, cross-checked against a second rigid body |
| gravity vs solved roll/pitch (phone) | one continuous stream, already sent | a phone mount that has sagged |

### 7.3 Declaring it

A violated calibration is **reported, not silently repaired**. It is recorded on the swing, shown
on the camera tile, and — for a PPCP camera — is what `calibration_changed` (5.11a1) exists to say.
Re-focusing or re-solving is an explicit, operator-visible act that supersedes the record.

---

## 8. Per-backend notes

### 8.1 PPCP phone

- **Intrinsics are declared, not delivered.** Per-frame matrix delivery (`REQ-OPT-7`,
  `kCMSampleBufferAttachmentKey_CameraIntrinsicMatrix`) is **absent at 1080p 240 fps** on an
  iPhone 16 (measured 24 August, §3.4), so the phone's native tier is I1 by way of a per-format
  field of view carried in `Source.calibration` at declare, `method: factory`. Where a mode does
  deliver per frame, that is a free verifier of the declared value (§3.4), never a substitute for
  it. Our board does not *replace* either — it **cross-checks** them. Two independent estimates with
  uncertainties is a stronger position than either alone.
- ⛔ **The solve must happen on the phone.** PinPointStudio receives only 640×360 preview; solving
  intrinsics from it is meaningless, because the downscale changes the very matrix being measured
  and destroys sub-pixel corner precision. The phone detects corners, solves, and reports.
- ⚠ **There is no "please calibrate" message in PPCP, and we should not invent one yet.** The phone
  offers the action in its own UI and reports the result unprompted via `calibration_update` — which
  already exists, needs no CR, and matches the ownership logic we accepted for `stream_open`.
- PinPointStudio must start consuming `PPCP_EVENT_CALIBRATION_UPDATE`; it is unhandled today.
- Focus is class A, and §5.4 applies with full force: the phone's intrinsics move with focus, so its
  calibration is keyed to a lens position.
- ⭐ **Attitude and gravity already arrive continuously** on the phone's `metadata` stream
  (`REQ-CLIP-1`, 5.11 — the stream-anchored Capture that revision 5 added for exactly this).
  PinPointStudio discards it today. It is the cheapest pose constraint in the building (§4.5) and
  must be consumed, keyed to the same `peer_id + source_id` as the camera it rides with.
- **Geometry for a phone is solved here, on a capture** (§4.4): the on-device solve of the bullet
  above is for *intrinsics*, where the board must be seen at full resolution live. Sticks and the
  wand are solved from received full-resolution clips using the bound I1 record (§3.4), so the G
  ladder needs nothing from the phone that declare and the capture leg do not already deliver.

### 8.2 Spinnaker / Aravis

- Fixed C-mount optics: focus and aperture are **mechanical**, class B. Guide-and-verify (§5.3).
- Intrinsics are stable and worth solving properly once — I2 is genuinely achievable and stays
  achieved.
- Global-shutter parts remove the rolling-shutter caveat that a phone carries (`readout_ns`, 6.2,
  and `VideoInputPpcp::rowInstantNs`). Worth recording which it is.

### 8.3 USB / Qt Multimedia

- Often class C: autofocus that cannot be stopped. ⛔ **Detect it and declare it.** A camera whose
  focus we cannot hold is a G1 camera at best — scale, never pose — and its swings say so. Never present an
  uncontrollable camera as locked.

---

## 9. ⭐ How this fits the flow of a new session

**The session verifies. The session never calibrates.**

### 9.0 ⭐ Both hosts already exist, and so does half the concept

This does not need a new screen, and — importantly — **it needs no edit to
`ScreenSessionWizard.qml`**, which is under a no-touch-without-approval rule. The stub is already
instantiated twice:

- `PpCameraPanel.qml:178` — `layoutMode: "compact"`, the Settings path, with `onCompleted` /
  `onCancelled` returning to the camera list;
- `ScreenSessionWizard.qml:1144` — `layoutMode: "full"`, already a step in the new-session flow.

So **implementing the flow lights up both slots and touches neither host.**

⚠ And the wizard has already reasoned about this further than expected. Beside that step sits a
`CheckRow` labelled *Triangulation* bound to `root._todo_triangulationValid` — a placeholder waiting
for exactly the extrinsics validity of §6 — and it is marked:

```qml
optional: root.anyFixedCamera
subFail:  root.anyFixedCamera ? qsTr("OPTIONAL — CAMERAS ARE FIXED IN PLACE")
                              : qsTr("NOT CONFIRMED")
```

⛔ **That is the Bay concept, already encoded.** The wizard has always known that cameras fixed in
place do not need re-calibrating every session. §3.1 gives that intuition a home and a record;
`_todo_triangulationValid` is the signal it has been waiting for.

**Bay setup — rare, deliberate.** The full ceremony, in the two slots above. A golfer walking the
wizard every session sees a *check*, not a ceremony: the wizard step's job is to show the verdict
and offer re-calibration, not to demand one.

**Starting a session — automatic, seconds, no interaction.** The wizard chooses cameras and views
as it does now. On session open, a **calibration health check** runs per camera:

1. restore the stored calibration for this bay and camera identity;
2. confirm the identity still matches (serial, or peer + source);
3. for class A, confirm `lensPosition` equals the calibrated one — re-assert if not;
4. measure the fiducial, and the ball if it is there, against their references;
5. publish a verdict.

| Verdict | Meaning | What happens |
|---|---|---|
| 🟢 **verified** | calibration restored and confirmed | nothing; proceed |
| 🟠 **degraded** | usable, with a named reason (lower tier, stale, unverifiable) | proceed, recorded on every swing |
| 🔴 **uncalibrated** | nothing valid for this camera | proceed, metrics needing scale withheld |

⛔ **None of them stops the session.** A golfer hitting balls beats a perfect rig; what matters is
that the record is honest about what was known at the time.

**During the session:** the §7 checks run continuously; a violation raises a toast on the session
screen (that is the established channel for something the operator must act on *now*) and is
recorded on subsequent swings.

**On every swing:** the calibration `id` **and a digest of it**, plus the `(I, G)` tiers and the verification
result. ⚠ By reference *and* digest, so a re-analysis six months later knows exactly which geometry
was in force and cannot silently pick up a newer one — the same concern already raised about
`swing.json` provenance.

---

## 10. Implementation staging

Each stage is independently useful, and nothing later is needed for something earlier to pay off.

| # | Stage | Notes |
|---|---|---|
| **1** | **Make the current focus failure visible.** Report `waitForConvergence` timeout instead of returning silently; read and record `lensPosition` at lock; re-assert the lock after `interruptionEnded`. | An afternoon on the phone. Tells us whether a bad lock is the real problem *before* anything is built on the assumption that it is. |
| **2** | **Calibration record + per-bay store**, shaped on 5.9, **and the I1 fetch of §3.4**: pixel-pitch lookup at enumeration, per-format field of view from the phone's declare, a focal-length field and an intrinsics cell in the Cameras panel. | Where focus lives, and the first thing that gives a camera a tier. No ceremony. |
| **3** | **Link `opencv_calib3d` and `opencv_objdetect`.** | ⚠ PinPointStudio deliberately links only core/imgproc/imgcodecs to keep VTK out of the bundle — but VTK comes from `opencv_viz`, not these. Say so in the comment or someone will "fix" it back. |
| **4** | **G1 known-size flow** — nominate an object, draw across it, store scale + uncertainty. | Smallest thing that gives a real answer, and it serves the operator with no board. |
| **5** | **Focus: sweep for class A, guide-and-verify for class B**, stored as `kind: focus`. | Delivers the original ask. ⚠ Do stage 1 first — it may show the fix is smaller than this. |
| **6** | **Fiducial + ball continuous verification**, and the session health check of §9. | The part that keeps it true. |
| **7** | **ChArUco intrinsics solve** for local cameras. | I2. Once per camera + lens, cached. |
| **8** | **PPCP on-device solve** and `calibration_update` consumption. | Largest; do it once the shape is proven locally. |
| **9** | **Ground-stick geometry (G2)** — detect two sticks in a T, vanishing-point rotation, intersection origin, endpoint scale, role + target-end declaration; runs on a full-resolution capture for every backend. | ⭐ **The first thing that supplies `ScreenSessionWizard`'s `_todo_triangulationValid`** and puts every camera in one frame — with no board. Needs `f` from the bound I1 record of §3.4 — a phone's declared field of view, or a FLIR's typed focal length — so it can precede 7 and 8. |
| **10** | **Break the plane (G3)** — a vertical stick, a typed camera height, and gravity consumed from the phone's metadata stream. | Small increments on 9; the vertical stick is the one that pays. |
| **11** | **Board extrinsics (G4)** — the §6.1 ceremony. | Now a modest increment on 7 + 9, for the operator who owns a board and wants ±mm at the ball. |
| **12** | **Wand + bundle adjustment (G5)**, shaft/limb-length refinement, per-shot residual health. | The real unlock for 3-D fusion of pose and shaft. Needs the printed collars and a sync check (§4.6). |

---

## 11. What must be measured before committing

⚠ Written down because today's lesson was that reasoning ahead of evidence is expensive.

1. **The real depth of field at bay distances.** Estimates put the iPhone wide's hyperfocal near
   3.5–4 m, which would make everything from ~1.7 m to infinity acceptably sharp and would mean a
   golfer moving is *not* a focus problem at all. Ten minutes with a tape measure and a chart
   settles it, and it decides how much of §5 is worth building.
2. **Whether a full-resolution frame at address is actually soft.** If it is not, this is a preview
   artefact plus an exposure problem, and §5.6 is the whole answer.
3. **Whether the phone can detect ChArUco corners at capture resolution without disturbing
   capture.** Decides whether stage 8 is a background task or a separate mode.
4. **Board size and square pitch** for the actual distances in the bay.
5. **Ball-diameter detection repeatability** at address — it sets the uncertainty on tier G1, and
   G1 is the path most operators will be on.
6. **The actual camera heights and grazing angles in the bay.** The ±25–50 mm of §4.4 assumes ~20°;
   the number moves fast with angle, and a tape measure settles it in a minute.
7. **Endpoint localisation σ on a real stick at 3 m** — the 4 px assumed in §4.4 sets every G2
   figure. Measure it on both a stick end and a clubhead, since the clubhead is the fallback.
8. **Vanishing-point `f` against the phone's declared `f`.** If they agree to a few percent the
   sanity check is worth showing; if not, it is worth knowing why before anyone trusts either.
10. **`videoFieldOfView` against the delivered matrix at 120 fps, and the 240 fps crop.** The
    §3.4 plan rests on the vendor field of view being honest per format; one run on the phone
    settles it, and tells us whether the 240 fps mode narrows the view as expected.
9. **Wand detection in motion** at 150 and 240 fps — ball-detector hit rate and centre σ at 1 m/s,
   and the actual rolling-shutter smear on the phone against its declared `readout_ns`.

---

## 12. Open questions

- **Requesting a calibration over PPCP.** §8.1 proposes the phone offers it locally and reports
  unprompted, needing no CR. If a host-driven trigger is wanted later, that is a genuine protocol
  gap and a candidate CR — but it should be argued from a real need, not designed in advance.
- **A `calibration` kind registry.** 5.9's registry now lists `intrinsics | position |
  bias_alignment | pose`, so what this document calls `extrinsics` should be carried as **`pose`**;
  `focus` is still ours to propose. The registry is open, so this costs nothing, but the names should
  be agreed with the protocol team before they appear in a bundle. The `(I, G)` tier pair of §3.3 is
  a PinPointStudio record, not a protocol field.
- **Whether G2/G3 geometry belongs in the phone's bundle at all.** It is solved host-side from a
  capture (§4.4), so the phone has nothing to report. If a future phone-side consumer wants the pose
  — an AR overlay, say — that is a `calibration_update` in the other direction, and a real gap.
- **Which metrics may be computed at which tier.** Named in §3.3, decided in the analysis model.
- **Rolling shutter.** `readout_ns` is already declared and `rowInstantNs` already applies it;
  whether calibration should *verify* the declared value is a separate question worth asking.
