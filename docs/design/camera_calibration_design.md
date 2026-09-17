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
**Revised 2026-09-15:** the impact camera (§4.8, stages 13–14, §11 items 11–16).
- **The finding behind it:** the first graded impact-camera session showed a ball-diameter ruler is not
  good enough for speeds and angles at the ball: ±10 % from the threshold on a dim ball, and ~9° of
  launch angle lost to a tilted mount.
- **What it adds:** a ChArUco card solved for pose in the measurement plane, for a face-on floor mount
  (built first) and an elevated path camera (a tripod at 1–2 m looking down from the face-on side, to validate path). The ball is kept as a relative ruler and verifier, and the launch
  monitor as a health check that is never fitted.
**Revised 2026-09-16:** the board is a real object now, and the bay is a real size. Mark's cabin is
5 × 4 m; the face-on and DTL cameras stand **1.5–2 m from the golfer**, so the ball is ~1–1.5 m from
the face-on lens and ~1.5–2 m from the DTL lens. The worked examples in §4.4 and §4.5 were written
for a camera at 3 m and are re-scaled to that. §4.1 now records what a board can and cannot do at
those distances (legibility in pixels per marker cell, not metres), **the board chosen to order**
(JD Photo Data, 200 × 300 mm Dibond, the §4.8 card exactly), and **an idealised specification for
this cabin** — two cards and a tag strip on one sheet, with the print files under `docs/design/charuco-print/`.
§4.8's card is now that board rather than an A4 print on foam, §6.1 says which cameras can and
cannot "take turns" on it, and §11 items 4, 6 and 7 are updated.
**Revised 2026-09-17:** §3a is new — the ceremony itself, step by step, which the title promised and
no section stated. It answers whether DTL, face-on and impact perspectives, or FLIR, USB and PPCP
cameras, need different ceremonies: **no** — one ceremony of seven steps, parameterised by capability
(which branch of a step runs) and by perspective (which references are legible and required), with
the two tables in §3a.3 and §3a.4 as the whole of the difference, and §3a.5 saying which steps a
change re-opens, and §3a.6 the two-camera pass (face-on + DTL as one layout, two focus walks, one
capture). §5.6 gains the back-focus finding from the face-on Chameleon3.

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

## 3a. ⭐ The ceremony — what the operator actually does

*Added 2026-09-17, because the title promises "one ceremony" and nothing below said what it was.
§4 lists the references, §5 the focus paths, §6 the order for 3-D and §9 the per-session check; this
is the walk-through they hang off, in the two slots §9.0 names. It is written per camera, because
that is how the panel and the wizard present it, and a bay is the sum of its cameras done in turn.*

### 3a.1 One ceremony, two parameters

There is one ceremony. Every camera in the bay walks the same seven steps and ends with the same
record (§3.2). Two things parameterise it, and **neither adds or removes a step — each changes what a
step says and which branch inside it runs:**

| Parameter | Comes from | Decides |
|---|---|---|
| **Capability** — what the camera can do | the backend at enumeration, expressed as `FocusCapability` (§5.1) and the intrinsics source (§3.4). ⛔ Never the backend's name | *how* step 3 is done (sweep / guide / declare), *where* `f` comes from in step 1 (typed / declared / nothing), and *how* step 5's capture is obtained (own frames / the PPCP capture leg) |
| **Perspective** — what the camera is for | `CameraInstance::Perspective` — `FaceOn`, `DownTheLine`, `Other`, `Impact` — which the wizard already asks for | *which* references are legible and offered in step 5, *which* of them are required rather than optional, and *which* tier that view's metrics gate on — so what the verdict at step 6 means |

So the answer to "does DTL need a different ceremony from face-on, or a phone from a FLIR?" is **no**.
They walk the same steps and produce the same record; §3a.3 and §3a.4 say exactly what differs at
each step, and those two tables are the whole of the difference.

### 3a.2 The seven steps, per camera

Each step names what the software does, what the operator does, what is recorded, and its fallback.
A step's fallback is always "record less, and say so" (principle 6) — never "stop" (principle 7).

**0. Attach to a bay.** Name it, or pick it. The bay lists its cameras by identity (serial, or
`peer_id + source_id`) and perspective. A camera already in the bay arrives with its stored records;
the ceremony then *shows* them and offers to redo, which is §9's check rather than a ceremony.

**1. Identify and read what the device can state.** Software: identity, model, and the I1 intrinsics
of §3.4 — pixel pitch from the model for a machine-vision camera, per-format field of view from a
phone's declare, nothing from a USB webcam. Operator: **types the lens focal length** for a camera
whose lens the device cannot see (the FLIRs). Recorded: `kind: intrinsics, method: factory |
user_measured`, per format, with its uncertainty. Fallback: I0, shown as such.

**2. Frame and light.** Operator: aims the camera at the view its perspective wants, sets exposure
and, for a mechanical lens, aperture — **before focus,** because focus is tolerant of aperture only
in one direction (§5.3) and the blur that matters at impact is exposure, not focus (§5.6). Software:
the live tile with its exposure readout; for a phone, `setExposureModeCustom` is the thing that
today is never called. Recorded: the active format, which fixes the intrinsics record bound in step 6.

**3. Focus, by capability class (§5).** The sharpness ROI is the reference the operator is about to
place, or the ball at the spot.
- *Class A (phone):* the software sweeps `lensPosition`, fits the peak, locks it, and shows the curve.
- *Class B (FLIR, any C-mount):* a live sharpness bar with peak-hold; the operator turns the ring and
  locks it; the software measures after and records the value. ⚠ **A ring at its end stop that is
  still soft is not a focus fault** — it is the mount stack (§5.6, third bullet); the flow says so
  rather than letting the operator chase the ring.
- *Class C (USB with autofocus that cannot be stopped):* detected and declared; no lock is claimed.
Recorded: `kind: focus`, the edge-spread and its anisotropy (§5.5), and the lens position as
`validFor` on the intrinsics record (§5.4). Fallback: a declared "unverifiable" focus and a G1 ceiling.

**4. Scale from the ball (G1).** Operator: a ball on the spot. Software: detects it, shows the
diameter, and the operator confirms or draws across it. Recorded: `kind: pose` at G1 — scale at the
hitting plane, ±2–3 % — **and the reference ball diameter that every later shot is checked against
(§4.3, §7.2).** This step is never skipped, because it is free and it is the verifier.

**5. Geometry from a reference, chosen by what the operator has and what the perspective can see
(§4).** Operator: places the reference and leaves it; **declares which end of the target-line stick
is the target end.** Software: **arms a calibration capture**, receives the full-resolution frames —
the FLIR's own, or the clip over the PPCP capture leg (§4.4) — and solves host-side against the bound
I1 record. Rungs, each optional above G1 and each recorded with what it consumed:
- **G2:** two sticks in a T, ball at the intersection — every wide camera.
- **G3:** + one vertical stick, a typed camera height, and gravity from a phone.
- **I2 (intrinsics, once per camera and lens):** the board held upright at 1.2–1.5 m — every camera,
  every perspective, and it supersedes the I1 record rather than replacing it.
- **G4:** the board left at the ball — only the cameras that can read it there (§6.1).
- **G5:** the wand swept through the volume — every camera at once, a joint solve.
- **Impact:** the §4.8 card, upright in the measurement plane for placement A, flat with a
  target-line stick for placement B; solved for a *pose*, never a homography.

**6. Record, bind and declare.** Software: mints the calibration `id` and digest, stamps
`validFor`, computes the `(I, G)` pair, and publishes the verdict of §9 — 🟢 / 🟠 / 🔴 with its
reason. The wizard's *Triangulation* row (§9.0) is this verdict for the bay. Recorded: everything
above, in the bay store, per `(camera identity, kind)`.

**7. Install the verifier.** Operator: a fiducial patch or one 30 mm tag fixed in view (§7.1, §4.8
item 2), and the ball left on its spot. Software: measures both and stores them as the references
§7.2 compares against every frame and every shot. This is the step that makes the other six stay
true, and the one most easily forgotten — so the flow does not declare 🟢 without it.

⭐ **What the operator sees is shorter than this.** Steps 0, 1 and 6 are automatic or one field; the
operator's ceremony is *frame, focus, ball, reference, tag* — five things, one camera at a time, with
the reference left where it is while the next camera takes its turn (§6.1, §6.3). ⭐ And for a bay
with more than one camera it is shorter still: §3a.6 re-sorts these steps by what is per-bay and what
is per-lens, and a face-on + DTL bay comes out at four operator actions.

### 3a.3 What differs by camera type — the capability axis

| Step | **FLIR (Spinnaker / Aravis)** | **USB / Qt Multimedia** | **PPCP phone** |
|---|---|---|---|
| 1 identify | serial; pixel pitch from the model; **focal length typed** → I1 | nothing stated → I0 until a board solve | `peer_id + source_id`; field of view per format from the declare → I1; a one-shot matrix at 120 fps for `cx/cy` where the platform gives one |
| 2 light | exposure **and aperture** are the operator's; global shutter, no rolling-shutter caveat | whatever the driver exposes; usually auto | exposure must be set custom and locked; rolling shutter, `readout_ns` recorded |
| 3 focus | **class B**: guide-and-verify, ring locked; the back-focus check | usually **class C**: detect the AF, declare it, no lock claimed | **class A**: deterministic sweep, `lensPosition` locked and recorded as `validFor` |
| 4 ball | same for all — the detector already runs on every backend | same | same, on the full-resolution capture, not the 640 × 360 preview |
| 5 geometry | solved here from its own full-resolution frames; I2 is worth doing once and keeping | solved here; G1 only if focus is uncontrollable (§8.3) | **solved here from a calibration capture over the capture leg** (§4.4); gravity from the `metadata` stream contributes to G3 for free; I2, if wanted, is the phone's own solve reported by `calibration_update` (§8.1) |
| 6 bind | `validFor` = "the ring as verified" | as recorded | `validFor` = the locked `lensPosition`; the Stream's `calibration_id` is fixed for its lifetime (5.9a) |
| 7 verify | fiducial + ball + a bump check | fiducial + ball + an AF-running check | fiducial + ball + `lensPosition` poll + gravity against the solved roll/pitch |

The cells are the branches; the rows are the same. ⛔ A branch is selected by the capability descriptor
and the intrinsics provenance, never by `Backend::`, so a future machine-vision camera with a motorised
lens lands in class A without touching the flow.

### 3a.4 What differs by perspective — the reference axis

| Step | **Face-on** | **Down-the-line** | **Other** (rear diagonal, overhead) | **Impact A** — face-on at the floor | **Impact B** — elevated on the face-on side |
|---|---|---|---|---|---|
| 2 frame | whole swing, ball ~1–1.5 m from the lens | whole swing along the target line, 1.5–2 m | whole swing | the 640 × 240 strip at the ball, ball ~60 % across, lens between ball height and +100 mm, level ≤ 3°, square ≤ 5° (§4.8) | a tripod at 1–2 m looking down ≥ 45°, 60° preferred |
| 3 focus ROI | the ball, or the card at the spot | the ball, or a stick end | the ball | the card | the card |
| 4 ball | required | required | required | required — and the diameter is a *relative* ruler per shot, never the scale | required |
| 5 legible references (§4.1.1) | T sticks ✓; the **perpendicular** stick's vanishing point is the strong one; board upright at the ball ✓ phone / marginal FLIR; board flat marginal; wand ✓ | T sticks ✓; the **target-line** stick's vanishing point is the strong one; **board at the ball ✗ at any size**; wand ✓ | T sticks ✓; board flat ✓ only for an overhead looking down ≥ 45°; wand ✓ | **the card upright in P** (G4, designed for it); sticks with tape marks in P (G2); ball only → withheld | **the card flat + a target-line stick** (G4); the line stick and marks (G2) |
| 5 solved | alone against the T; jointly in the wand bundle | the same | the same | alone: `solvePnP` on the card | **jointly with A**, in the bay frame — path is withheld without A (§4.8) |
| 6 what the verdict gates | overlays and plane at G2; speeds at G3; face-to-path only at G5 | the same | the same | ball speed, launch angle, clubhead speed, attack angle, low point — **none live below G4 on the card, or G2 on the marked sticks** | club path, heel/toe, launch direction |
| 7 verifier | patch + ball | patch + ball | patch + ball | the 30 mm tag + ball ratio + the dropped-ball vertical | the tag + ball |

Two things to notice. First, **the wide perspectives differ only in which references they can read and
which vanishing point is well conditioned** — the solver is the same code with the role and target-end
declaration as input (§4.4). Second, **the impact perspectives differ in the reference, not the
ceremony:** the card replaces the sticks because the metric lives in a plane the sticks do not define
precisely enough, and placement B's step 5 has a dependency (A) that no other camera has.

### 3a.5 Doing it again — which steps a change re-opens

| What changed | Re-open | Leave alone |
|---|---|---|
| a new camera joins the bay | 0–7 for that camera only | every other camera's records (§6.2) |
| a lens re-focused or the ring knocked | 3, then 6 — intrinsics are invalid at the new position (§5.4); G records built on them are flagged | 4, 5 unless the fiducial says the camera moved too |
| a tripod nudged, a mount sagged (§7.2 fires) | 5 for that camera, from the reference still on the floor; 7 | 1, 3 |
| the reference moved (a stick kicked) | 5 for every camera that used it, once it is back | 1–4 |
| the format or frame rate changed | 6 re-binds against the stored record for that format (§3.4); if none, 1 | 3 if the lens did not move |
| the light changed | 2; 3 re-verified, not re-done | the rest |
| a phone reconnects | 3 re-asserted (§10 stage 1), 6 re-bound; 5 only if gravity disagrees with the solved roll/pitch | the rest |

⛔ **A session never opens any of these.** It runs the §9 check, shows the verdict, and offers the
ceremony from the wizard's slot; the golfer decides.

### 3a.6 ⭐ The two-camera pass — face-on and DTL done once, not twice

*Added 2026-09-17 after the question "can 2 × 7 steps be combined?" They can, and the reason is that
§3a.2 walks the ceremony **per camera because the record is per camera** — but almost nothing the
operator physically does is per camera. Re-sorting the same seven steps by what is per-bay and what
is per-lens gives a pass with **four operator actions**, not fourteen steps, and it is not a special
case: it is the general ceremony organised by trip rather than by camera.*

**Which steps are really per camera.**

| Step | Per camera? | Why, and what the two-camera pass does with it |
|---|---|---|
| 0 bay | no | once |
| 1 identify + I1 | **software: per camera, automatic.** Operator: the typed focal length | both Chameleon3s share the model → one pixel-pitch lookup. The focal-length field defaults the second from the first with a *same lens?* check; two different lenses is two fields. A phone needs nothing typed |
| 2 frame + light | **frame per camera; light per bay** | the bay is lit once. Each camera's exposure is set from the same scene; a FLIR's aperture is a per-lens ring but is set in the same walk as its focus |
| 3 focus | **yes — the ring is physical** | this is the one step that cannot be merged. But it can be *concurrent*: both sharpness bars are live at once, and the same object — the sticks and the ball already on the mat — is the ROI for both |
| 4 ball → G1 | no | one ball on the spot, detected by both cameras from the same capture |
| 5 reference → G2/G3 | no | **one T on the floor, one target-end declaration, one calibration capture armed across both cameras.** Two solves come out of it, and they land in the same frame by construction because they reference the same object. §6.1's "take turns" is for the board; with sticks the turns are simultaneous |
| 6 record + verdict | software, automatic | two records; one bay verdict, the worse of the two, which is what the wizard's *Triangulation* row shows |
| 7 verifier | **tag per view; ball shared** | the DTL cannot read a flat tag at the ball (§4.1.1), so each view gets its own tag — but both are laid out in the same trip as the sticks, and the calibration capture of step 5 records their reference poses at the same time. The ball is one verifier for both |

**The pass, as the operator does it.**

1. **Lay out — one trip to the mat.** Ball on the spot; the T through it; a tag in each camera's
   view; optionally the vertical stick for G3. Declare the target end. *(steps 4, 5, 7 placed)*
2. **Light — once.** Set the bay light. The software reads both exposures against the same scene.
   *(step 2)*
3. **Focus — one walk per lens, both bars live.** Face-on ring, then DTL ring, on the sticks and
   ball already there; aperture set in the same walk. Type each camera's height while standing
   at it, unless the vertical stick is in both views, in which case it is solved. *(step 3, and G3's
   inputs)*
4. **Capture — one button.** Both cameras record the still scene; the phone's clip arrives over the
   capture leg a few seconds later and the solve waits for it. From the one capture, per camera: the
   ball diameter (G1 + reference), the T solve (G2, G3 with the vertical), the tag poses (§7's
   references). Then the records, the digests, and the bay verdict. *(steps 4–7 solved; 6 recorded)*

Four actions, two of which are the unavoidable walks to the lenses. Steps 0 and 1 are automatic
apart from one focal-length field. ⭐ **The ordering matters and is the reverse of §3a.2's:** the
references go down *first*, because they are the best focus target in the bay — a high-contrast
600 px line at exactly the plane that matters — and because it turns "focus, then place, then
capture" into "place, then focus, then capture", one trip fewer.

**What it does and does not buy.**

- **Shorter, not more accurate.** Two cameras seeing the same *flat* T give two independent solves;
  coplanar points seen from two views constrain neither camera's pose more than one view does, so
  there is no joint win at G2. The win that two cameras do offer — the face-on's weak depth axis is
  the DTL's strong lateral axis and vice versa — needs a non-coplanar reference, and it arrives at G5,
  where the wand sweep is *also* one capture for both. The pass is the same shape at every rung.
- **A free consistency verdict.** The T is one object, so the two solves must agree: the angle
  between the sticks, the stick length the DTL measures along the target line against the face-on's,
  the ball's height over the mat. Disagreement beyond the stated uncertainties is shown at step 4,
  before either record is declared 🟢. That check does not exist in a one-camera ceremony.
- **It generalises.** N cameras is one layout, N focus walks, one capture. A third camera — the rear
  diagonal, the impact strip — joins the same capture; the impact card goes down in the same trip as
  the sticks, upright at the spot, and is solved from the same frames.
- **It does not touch the record.** §3a.2 stays as the definition of what is stored per camera; the
  pass is how the flow *presents* it. ⛔ The flow must not collapse the records to match the
  presentation: a bay verdict is the worse of its cameras, and a camera re-done alone (§3a.5) still
  produces its own record without disturbing the other's.

**What cannot be combined, and why.** The rings, because they are metal. The tags, because the two
views are 90° apart and no flat target is legible to both. The focal lengths, if the lenses differ.
Everything else was only ever per camera on paper.

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
A board large enough to fill a face-on camera will not sit wholly inside a down-the-line
camera's frame, and a plain checkerboard contributes nothing unless the whole board is visible.

- **Definition**: squares across/down, square size in mm, marker size in mm, dictionary. Entered
  in-app (⛔ no file dialogs — house rule), with a couple of printable presets.
- **Plain checkerboard is still supported** for an operator who already has one; it simply requires
  the whole board in frame, and the flow says so.
- **What it is for now.** Intrinsics with distortion (I2), once per camera and lens — and ±mm pose
  at the ball (G4) for the operator who wants it. It is no longer the only route to a bay frame:
  §4.4 gets there with two sticks, and §4.6 gets *past* it for the swing volume.

#### 4.1.1 What a board can and cannot do in this cabin (2026-09-16)

⭐ **Legibility is set by pixels per marker cell at the board, not by metres.** A `DICT_4X4` marker
is six cells across including its border, so a 30 mm marker has 5 mm cells. ArUco decodes at
~3 px per cell and is reliable at ~5; a ChArUco corner then refines to ~0.2–0.3 px. The pixel scale at
the ball is fixed by the job the camera already has — framing a whole swing — and is roughly
**0.8–1.2 px/mm on a 1920 px phone and 0.5–0.8 px/mm on the 1280 px Chameleon3**, whatever lens
gets it there. A flat board is foreshortened by the sine of the grazing angle on top of that.

| Camera and board placement, 40 mm squares / 30 mm markers | phone class | Chameleon3 class | verdict |
|---|---|---|---|
| **Intrinsics:** board held upright 1.2–1.5 m from any lens | 5.5–7 px per cell | 4.5–6 | fine, every position |
| Impact camera, placement A: card upright on the mat at ~1 mm/px | 5 | 5 | fine — designed for it |
| Impact camera, placement B: card flat, 1.5 m slant, 60° down | 4.8 | 4.8 | fine |
| Face-on at 1–1.5 m from the ball, board **upright** at the ball facing it | 4–6 | 2.5–4 | phone fine, FLIR marginal |
| Face-on, board **flat** at the ball, lens ~1 m up (35–45° grazing) | 2.5–4 | 1.5–2.5 | marginal at best |
| DTL at 1.5–2 m from the ball, board upright, turned to face it | 3–5 | 2–3.5 | marginal |
| DTL, board **flat** at the ball (~30° grazing) | 1.5–2.5 | 1–2 | **no** |
| Overhead straight down from ~2.5 m, board flat | ~3.3 | ~2 | marginal / no |
| Rear diagonal or isometric at ~3 m slant, board flat | ~1.2 | <1 | **no** |

What follows from the table:
- **Intrinsics are the board's real job, and this cabin makes them matter more, not less.** Framing a
  swing from 1.5 m needs ~80° of view — a 4–5 mm C-mount on the Chameleon3, well under the ~6 mm
  below which §4.8 makes the I2 solve mandatory before a geometry record is trusted. A wide lens has
  real barrel distortion and a phone's declared field of view says nothing about it. The board fixes
  that once per lens, held at arm's length, and the position of the camera is irrelevant to it.
- **G4 pose at the ball from the wide cameras is not a board job, at any size.** Flat at the ball the
  markers are unreadable from the DTL and marginal from the face-on, and a board upright can face
  one camera at a time. Their 400 × 600 sheet with 65 mm squares reaches ~1.7 px per cell flat from
  the DTL — still no — and stops being an impact card. The sticks (§4.4) and the wand (§4.6) remain
  the geometry path for those cameras; §6.1 says which cameras can share a board.
- ⚠ The grazing angles here are **estimates from the stated distances and a guessed ~1 m lens height**;
  §11 item 6 still wants the tape measure.

#### 4.1.2 The board chosen to order (2026-09-16)

JD Photo Data, *Inkjet Printed ChArUco Target*, 3 mm aluminium composite (Dibond), **200 × 300 mm,
High Resolution** (3.7 pl: features ±0.2 mm, pitch ±0.1 mm, overall ±0.5 mm, edge roughness ±0.06 mm),
standard inspection, £101 + VAT. The pattern is free text on their order form and is ours:

- **ChArUco 7 × 5 squares, 40 mm squares, 30 mm markers, `DICT_4X4_50`, ids 0–16 in OpenCV order,
  black square top-left, landscape** — 280 × 200 mm, centred with 10 mm side margins and **flush to
  the bottom edge**, which is the edge that stands on the mat. This is the §4.8 card exactly.
- Print-ready file generated with OpenCV 4.14 and verified to decode all 17 markers and all 24
  corners when resampled to the impact camera's ~1 mm/px:
  [`charuco-print/charuco_7x5_40mm_30mm_4x4_50_300x200mm_600dpi.pdf`](charuco-print/charuco_7x5_40mm_30mm_4x4_50_300x200mm_600dpi.pdf) (and `.png`).
- **Why these choices.** 200 mm tall fits the 240-row strip upright; 5 rows (odd) sidesteps the
  OpenCV ≥ 4.6 legacy-pattern origin ambiguity that an even count reopens; `DICT_4X4` has the fewest
  bits per marker and therefore the largest cells for the size; High Resolution's ±0.06 mm edge
  roughness is a quarter of the corner σ where Standard's ±0.2 mm would equal it, for £11.40; the
  £50 measurement certificate only documents what the stated tolerance already gives spec 5.9's
  mandatory uncertainty. Dibond is flatter and stiffer than the foam board §4.8 first assumed, at
  ~230 g.
- **Ask the printer for a matt white face** — the impact strobe sits near the lens axis and a gloss
  varnish throws a hotspot into the white squares — and to print from the supplied file, so the ids
  and layout the app is told are the ids and layout on the board.
- **Two small departures from §4.8 as first written:** the pattern is flush to the base so there is
  no margin for the printed origin tick (the ball spot is the centre of the base edge, scribed), and
  the foot's slot is 3 mm, not 5.

#### 4.1.3 The idealised board for this cabin

Specified from the requirements rather than the catalogue. Three numbers define it: the marker cell
must be ≥ 5 px in the **coarsest view it must serve**; the card must fit the **smallest window it
must serve**; and it must carry enough corners for a once-per-lens intrinsics solve. For this cabin
the coarsest view is the impact strip at ~1 mm/px (5 mm cells → 30 mm markers), the smallest window
is the 240-row strip with the base on the mat (≤ 205 mm tall), and 24 corners over ~30 views is a
sound intrinsics solve. The view it deliberately does **not** serve is the DTL's flat view of the
ball spot, which would need ≥ 65 mm squares, cannot fit the strip, and is coplanar-limited anyway.

**One sheet, 300 × 450 mm, 3 mm matt white Dibond, High Resolution, cut into three:**

| Piece | Size | Pattern | Purpose |
|---|---|---|---|
| **Card A** | 300 × 205 mm | ChArUco 7 × 5, 40 mm squares, 30 mm markers, `DICT_4X4_50` **ids 0–16**, pattern 280 × 200 flush to a **5 mm base margin** carrying a printed origin tick at the centre and corner ticks at x = ±140 | the upright face-on card of §4.8 placement A; the same card, held, for every lens's intrinsics |
| **Card B** | 300 × 205 mm | identical geometry, **ids 17–33** | flat on the mat for placement B, and for any elevated camera looking down at ≥ 45° |
| **Tag strip** | 300 × 40 mm, cut into seven 40 mm tiles | seven 30 mm ArUco tags, `DICT_4X4_50` **ids 40–46**, 5 mm quiet zone each | §7.1's static fiducials: one per camera, fixed in view for the bump check |

Why this is the ideal and not just more of the same:
- **Distinct ids let both cards sit at the ball at once** — A stands on B, its base 3 mm above the mat
  and the record says so — so a camera that sees both measures the A–B relation directly, and the
  §4.8 joint solve of path (B) and attack angle (A) no longer rests on two separately placed
  origins agreeing by construction.
- **The printed origin tick and base margin** put the ball spot on the card rather than on a scribe
  line, and the 5 mm margin keeps the bottom row's corners off the cut edge.
- **The tags come from the same dictionary and the same sheet**, so the app holds one dictionary,
  one id map and one uncertainty for everything printed in the bay. 50 ids is enough: 34 used by the
  cards, 7 by the tags, 9 spare.
- **It is the same geometry as the §4.1.2 board**, so the 200 × 300 board, if that is what arrives first, is card A of
  this sheet with ids 0–16, and nothing in the app definition changes when the sheet replaces it.

Print-ready file, generated and verified (both cards decode 24 corners with their own ids, all seven
tags decode, at 1 mm/px and at 0.6 px/mm):
[`charuco-print/charuco_cabin_sheet_300x450_two_cards_plus_tags_600dpi.pdf`](charuco-print/charuco_cabin_sheet_300x450_two_cards_plus_tags_600dpi.pdf) (and `.png`).
It is the 300 × 450 option on the same order form, £129 + VAT; JD cut to size on request, and 3 mm
Dibond scores and snaps if not. ⚠ If a floor board legible to the DTL is ever wanted, that is a
**second, separate** object — 400 × 600, 6 × 9 at 65 mm, 50 mm markers — and it buys a coplanar
±15–30 mm pose the sticks already give.

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

**What it is worth — worked for this cabin (2026-09-16): a 1920 px phone framing the swing from
1.5–2 m, which puts ~0.8–1.2 px/mm at the ball.** (The first draft worked it for a camera at 3 m
with `f` ≈ 1660 px; the answer barely moves, because σ in millimetres is what matters.)

Angular error ≈ (σ_mm / L) / √N, with σ_mm = σ_px / (px per mm).

| | σ | L | N | Rotation |
|---|---|---|---|---|
| sticks | ~4 px on an endpoint ≈ 4–5 mm | 1.22 m | 4 | **~0.1°** |
| the §4.1 card | ~0.2 px on a corner ≈ 0.2 mm | 0.28 m | 24 | ~0.01° |

Fifteen times worse per point, but the sticks' four-times-larger extent claws most of it back.
0.1° is not disqualifying.

**Position is the problem, and it is the whole story.** A plane seen at grazing incidence is badly
conditioned along the view direction. In this cabin a face-on lens ~1 m up and 1–1.5 m from the ball
sees the mat at ~35–45°, the DTL at 1.5–2 m sees it at ~30°; the first draft's 3 m camera saw it at
~20°, where a flat reference localised depth at roughly **±25–50 mm**. The steeper angles here
roughly halve that, to perhaps **±15–30 mm** — an estimate until §11 item 6 is measured — and a board
held at 45° or more to the view localises it at ±1–4 mm. That ten-fold gap is **coplanarity plus
grazing angle, not point count** — which is exactly why §4.5 breaks the plane rather than adding
more sticks to it.

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
   already (§4.3). Range from apparent size is weak — at this cabin's 1–1.5 m the ball is ~35–50 px
   across, and ±0.5 px is ±1–1.5 % of range, ±15–20 mm — but it is a free consistency check on the
   plane fit, and it anchors scale at the one point that matters.
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

### 4.8 ⭐ The impact camera — a card at the ball, in the plane the metric lives in

*Added 2026-09-15.* The impact camera ([impact_camera_design.md](impact_camera_design.md)) is a
640 × 240 strip at 592 fps centred on the ball. It is the one camera in the bay whose numbers are
speeds and angles at the ball, rather than overlays and body angles, and it needs its own treatment.

**Why the ball alone is not enough — the first graded attempt, 2026-09-15.**
- **Setup:** the camera was on a tripod as low as the tripod went, looking down at the ball, with a
  launch monitor on the same swings. Mark has set that session aside as a rig to learn from, not data.
- **Absolute ruler:** the ball's diameter in pixels on a dim, top-lit ball moved **±10 % with the
  detection threshold**, and the resting ball was under-measured outright. Ball speed read **1.3–1.5 ×**
  the launch monitor on the detector's own resting radius, and 1.06–1.25 × on an edge-measured one.
  §4.2's ±2.5 % assumes ±1 px on a well-lit edge. A dim ball's edge is not one.
- **Tilt:** launch angle read **~9° low on every swing**. A camera looking down at a plane it is not
  square to turns motion out of that plane into motion within it, and no single-view correction
  recovers it.

⛔ So **the impact camera's metrics do not go live on G1.** The ball stays in the loop as a *relative*
ruler and a verifier (below), never as the scale.

**Two placements, one method.** Both are designed; the face-on floor mount is built first because it is
the only one that gives launch angle and full ball speed.

| | **A — face-on, just above the floor** (first) | **B — elevated, looking down from the face-on side** (second: a tripod at 1–2 m, to validate path) |
|---|---|---|
| Measurement plane P | the vertical plane containing the target line, through the ball centre | the horizontal plane at the mat |
| Card | stood **upright**, printed face on the target line through the ball spot | laid **flat** on the mat, origin tick on the ball spot, x arrow toward the target |
| Target-line axis from | the card's base edge on the mat (horizontal); the mat's level is §11's to check | ⭐ **an alignment stick along the target line in view**: a ~600 px line fit, ~0.1°. The card's printed arrow is only as good as the operator's eye (±1–2°), and club path needs ≤ 0.5° |
| Gives | ball speed, launch angle, clubhead speed, attack angle, low point, high/low strike | club path, heel/toe strike, launch direction, horizontal ball and clubhead speed |
| Never gives | path, face | launch angle, attack angle, low point; **full** ball speed without a launch angle from elsewhere (a 30° wedge launch reads 13 % low horizontally) |
| Lens for ~1 mm/px | f ≈ 4.8 µm × range: ~8 mm at ~1.6 m (preferred), ~4.8 mm at 1 m | ~8 mm at a ~1.5 m slant range |

**The card.** *Was* "A4 landscape on 5 mm foam board"; since 2026-09-16 it is the §4.1.2 board of
§4.1.2 — 200 × 300 mm, 3 mm Dibond, the same pattern — and ideally card A of the §4.1.3 sheet:
- **Pattern:** ChArUco 7 × 5, 40 mm squares, 30 mm markers, `DICT_4X4_50`, ids 0–16. That is
  280 × 200 mm, which fits the 240 mm strip at ~1 mm/px with its base on the mat.
- **Resolution:** at that scale a marker cell is ~5 px, still decodable, and corners localise to
  ~0.2–0.3 px. Partial views still solve (§4.1). Verified on the print file at 1 mm/px: 17 of 17
  markers, 24 of 24 corners.
- **Face-on card:** stands in a right-angle foot (a book end, or a printed bracket with a **3 mm**
  slot) so it is upright. The ball spot is the centre of the base edge: a printed tick on the
  idealised card, a scribed one on the §4.1.2 board, whose pattern is flush to the base. The solved
  frame has its origin at the ball spot on the mat and the ball centre at (0, 21.3 mm). When card B
  lies under it (§4.1.3) the base is 3 mm up and the record carries that.
- **Matt face.** The strobe sits near the lens axis; a gloss card puts a hotspot in the white squares.

⛔ **Solve a pose, not a homography.** With the bound intrinsics (§3.4), `solvePnP` on the card corners
gives the camera's full pose relative to P, not just an image→P map. That matters because much of what
is measured is *not* in P:
- **The ball** placed nearer or further than the spot sits in a parallel plane at offset d.
- **The tracked hosel** sits ~30–45 mm further from a face-on lens than the face centre (heel side,
  toward the golfer). Mapped into P that is ~4 % of speed at 1 m and ~2.5 % at 1.6 m.
- **Placement B heights:** the ball centre is at +21 mm, a teed driver ball +50 mm, the head's top line
  +40–60 mm. At B's ~1.5 m slant range that is 1.4–4 %, so each is mapped at its own height.

With a pose, each of these is a ray–plane intersection at a stated offset. With only a homography, each
is a silent scale error.

**Intrinsics.**
- **I1:** pixel pitch (the Chameleon3's PYTHON 1300 is 4.8 µm) and the typed focal length.
- **The crop:** its position shifts `cx, cy` and never `f` (§3.4: store the invariants, derive per mode).
- ⭐ **Prefer the longer lens further back:** less barrel distortion across the strip, a smaller
  perspective ramp as the ball moves in depth, and further from a shank. A lens shorter than ~6 mm gets
  the I2 board solve before its G record is trusted.

**Mounting tolerances for placement A, and what each buys:**

| | Tolerance | Because |
|---|---|---|
| Pitch (looking up/down) | ≤ 3° | The pose corrects it for points in P. For motion *out* of P (a ±5° start direction is ±9 % of the ball's speed sideways), tilt leaks that sideways motion into vertical as sin τ: ~0.2° of launch at 2°, several degrees at the 15 Sept tripod's angle |
| Yaw (axis vs square to the target line) | ≤ 5° | Leaks sideways motion into horizontal: < 1 % of speed |
| Roll | anything the framing allows | Fully corrected by the pose |
| Lens height | ball centre to ~100 mm above the mat | Keeps the ball and the bottom of the arc near the optical axis |
| Range | ≥ 1.5 m with f ≈ 8 mm | A ball starting 5° off line moves ~22 mm in depth across the track: a 1.4 % scale ramp at 1.6 m, 2.2 % at 1 m |
| Ball position in frame | ~60 % across, the larger share on the club's approach side | 6–7 head frames before impact instead of 4 |
| Guard | a low shield in front of the lens | It is in the shank zone, as the GCQuad is |

**For placement B:** a tripod at 1–2 m on the face-on side, looking down at ε ≥ 45° (≥ 60° preferred).
Its job is to validate and verify path estimates, ours and `lm.clubPath`.

⛔ **An angled view mixes the head's vertical motion into the in/out motion path is made of.** The
image's vertical velocity is v_inout·sin ε + v_vertical·cos ε.
- **Uncorrected, at 60°:** a −4° attack angle reads as ~2° of path, and a 30° launch as ~19° of launch
  direction.
- **So B is solved jointly with A, in the bay frame.** A measures the vertical motion (attack angle; the
  ball's launch angle), and B's in/out velocity is recovered with it removed. A 1° error in A costs
  ~0.6° of path at 60°, 1° at 45°, and nothing as ε → 90°.
- **Without A, B's path is withheld.** It is never corrected with `lm.attackAngle`: B is the check on
  the monitor's path, and borrowing the monitor's angle couples the errors being checked.
- **Why steeper is better:** in/out resolution scales with sin ε (−13 % at 60°), and the card on the
  mat is well conditioned at these angles.

**Per-shot corrections and checks, from the clip itself:**

1. ⭐ **The ball as a *relative* ruler.** When the card is solved, the record also stores the ball's
   detected diameter at address, under the same detector and light. Per shot, D_cal / D_shot gives the
   ball's range and so its offset d. A same-detector ratio cancels the threshold bias that sank the
   absolute ruler. Its repeatability is §11's to measure, and it is the uncertainty carried on d.
2. **A static tag.** One 30 mm ArUco tag fixed in view (the mat edge, a floor block). This is §7.1's
   fiducial with an identity. Its pose in each clip's background frame, compared with the record, is
   the bump check. A moved camera invalidates the record and says so (§7.3).
3. **Time base.**
   - Camera timestamps, measured frame rate against nominal, and gaps counted.
   - Speeds differentiate against frame index × the median period, because single timestamps jitter
     ±0.4 ms on the Chameleon3.
4. **Blur.** Ball-limb edge spread along versus across its motion (§5.5): directional blur is the
   exposure, isotropic blur is focus.
5. ⭐ **The launch monitor, where present, is a health check and never a fit.** Per shot it records the
   ball-speed ratio and launch-angle bias (A), or the launch-direction bias (B), beside the calibration
   verdict. Fitting to it would make every later grade circular.

**A dropped ball** through P verifies the vertical (~0.2° from a 30-frame track), not the scale: g over
the ~55 ms a dropped ball spends crossing a 240 mm strip is only good to ±8 %.

**Both impact cameras land in the bay frame.** Both cards use the ball spot as origin and the target
line as x, so the two impact records and the wide cameras' G2/G3 frame (§6.3) agree without
co-visibility. Placement B's path and placement A's attack angle then compose into club delivery at the
ball in 3-D.

**Tiers for the impact camera.**
- **G4, the card:** the precise path, and the one the metrics are designed around.
- **G2, no printer:** two alignment sticks with tape marks every 100 mm, one upright on the ball spot
  and one along the target line. For placement A both lie in P (~1 % scale, ~0.3°); for placement B the
  line stick lies on the mat and the marks give scale. Recorded as G2.
- **G1, ball only:** the impact metrics are withheld with the reason shown ("impact camera not
  calibrated").

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
- **A mechanical lens that will not focus at any ring position is a back-focus fault, not a focus
  fault** (*2026-09-17, the face-on Chameleon3*). The CM3-U3-13Y3C body is CS-mount and ships with a
  5 mm C-mount adapter ring; a C-mount lens needs the ring, a CS-mount lens must not have it, and the
  threaded lens holder is itself adjustable under a set screw. With the stack wrong the ring's travel
  never covers the image plane at 1–2 m, and opening the aperture — the natural response to a soft
  image — shrinks the depth of field and makes it worse. The class B guide (§5.3) therefore says,
  when the sharpness peak sits at a ring end stop: *check the mount, not the ring* — and which end
  stop it is says which way (at infinity and only near things sharp: too far from the sensor; at near
  and only far things sharp: too close). Nothing in Spinnaker can move it; there is no software focus
  on this camera.

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

⚠ **Who can actually take a turn, at this cabin's distances (§4.1.1).** A flat board at the ball is
legible to an elevated camera looking down at ≥ 45° (the §4.8 placement B tripod, an overhead on a
short pole) and, marginally, to a phone face-on at ~1 m. It is **not** legible to the DTL at
1.5–2 m, nor to a rear diagonal or an isometric camera, and no catalogue size fixes that — a
65 mm-square floor board reaches ~1.7 px per marker cell from the DTL. So "the cameras take turns"
holds among the cameras that see the board within ~45–55° of its normal; the rest land in the same
frame by the T of sticks (§6.3), which is the shared reference this bay actually uses. The board's
frame and the sticks' frame coincide by construction: origin at the ball spot, x along the target line.

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
| **13** | **Impact camera card calibration (§4.8)**, placement A (face-on floor) first. The card is detected in a calibration clip, the pose solved against the bound I1 record, the reference ball diameter at address stored, and one 30 mm tag registered. Then placement B (a tripod at 1–2 m on the face-on side, looking down; card flat plus a target-line stick; path solved jointly with A). | Needs only 2 and 3, so it can **precede 4–12**. The impact camera's speeds and angles gate on it, and nothing else in the bay does. |
| **14** | **Impact per-clip checks (§4.8):** the ball diameter ratio and its offset, the tag pose against the record, time-base gaps, blur anisotropy, and the launch-monitor residual where a monitor is present. Each is recorded on the swing beside the verdict. | The impact camera's §7. Without it a knocked tripod is invisible, which is the failure the first attempt had. |

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
4. **Board size and square pitch** for the actual distances in the bay. *Settled 2026-09-16 (§4.1.1–
   §4.1.3): 40 mm squares, 30 mm `DICT_4X4_50` markers; the board is an intrinsics and placement-B
   tool, not a floor board for the DTL.* What is still open is the marginal rows of the §4.1.1 table
   — a phone face-on reading the flat card, the Chameleon3 reading it upright — which one capture
   each will turn into a yes or a no.
5. **Ball-diameter detection repeatability** at address — it sets the uncertainty on tier G1, and
   G1 is the path most operators will be on.
6. **The actual camera heights and grazing angles in the bay.** The distances are known (face-on and
   DTL 1.5–2 m from the golfer, 2026-09-16); the lens heights are a guess of ~1 m, so the ~35–45°
   and ~30° grazing angles in §4.4 and §4.1.1 and the ±15–30 mm that follows are estimates. The
   number moves fast with angle, and a tape measure settles it in a minute.
7. **Endpoint localisation σ on a real stick at the cabin's 1.5–2 m** — the 4 px assumed in §4.4
   sets every G2 figure. Measure it on both a stick end and a clubhead, since the clubhead is the fallback.
8. **Vanishing-point `f` against the phone's declared `f`.** If they agree to a few percent the
   sanity check is worth showing; if not, it is worth knowing why before anyone trusts either.
10. **`videoFieldOfView` against the delivered matrix at 120 fps, and the 240 fps crop.** The
    §3.4 plan rests on the vendor field of view being honest per format; one run on the phone
    settles it, and tells us whether the 240 fps mode narrows the view as expected.
9. **Wand detection in motion** at 150 and 240 fps — ball-detector hit rate and centre σ at 1 m/s,
   and the actual rolling-shutter smear on the phone against its declared `readout_ns`.
11. **Impact card solve repeatability (§4.8).** Take the card out of its foot and re-seat it five times.
    The spread of the solved scale and plane is the impact camera's G4 uncertainty.
12. **Ball-diameter *ratio* repeatability at address on the impact camera,** same light:
    - twenty shots at the spot, then the ball deliberately ±25 mm from it;
    - compare with the card-solved depth.

    It sets the σ carried on every shot's depth offset.
13. **The chosen lens's distortion across the strip:** the card's straight edges laid across the full
    width. It decides whether I1 is enough or the lens needs I2 first.
14. **Mat level and the card's lean in its foot,** with a spirit level or a phone. This is the horizontal
    reference's σ, and it lands one-for-one on launch angle and attack angle.
15. **What a level floor camera sees behind the ball:** the golfer's feet and the trail heel lifting
    through impact. Does the club track survive them?
16. **The first graded impact session, as a protocol, not an ad-hoc recording:**
    - **Rig:** the calibrated face-on floor mount, the tag in view, the launch monitor on.
    - **Swings:** a dozen each with a wedge and a 7-iron, labelled correctly.
    - **Checks:** a dropped-ball vertical check before and after.
    - **Grading:** ball speed, launch angle, clubhead speed and attack angle against the monitor, reported
      with the calibration's own σ beside them.

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
