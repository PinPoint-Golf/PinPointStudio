# Two-camera capture protocol — calibrated face-on + down-the-line, with the launch monitor

**Document type:** data-collection protocol (one operator, who is also the golfer).
**Version:** 1.0 (2026-09-21) · **Owner:** Mark Liversedge.
**Why:** `docs/design/shaft_fusion_design.md` §5 and `docs/design/dtl_posture_design.md` §6 — every
number those two produced on 2026-09-21 is graded on repeatability alone, from a down-the-line
camera that was behind the ball, above hand height, turned 4–9° toward the golfer, and moved
mid-session. **Calibration method:** `docs/design/camera_calibration_design.md` (§3a.6 the
two-camera pass, §4.1 the card, §4.4 the sticks). None of that software is built; this protocol
records what it will need so the solve can be done offline, and nothing here waits for it.

> **What one session must produce:** 60–80 swings seen by two cameras that did not move, whose
> lenses, positions and pointing are written down and independently recoverable from calibration
> clips recorded the same day, with the GC Quad attached to every full swing.

---

## 0. What each part buys

| You do | It unlocks |
|---|---|
| DTL camera on the hands line, at hand height, pointing down the line | DTL plane and posture numbers that mean what a coach means by them; removes the ~20° closed-pelvis bias in the paired trunk route |
| Card clips (§4A) | focal length and principal point per lens — turns the stick's vanishing column into a yaw in degrees |
| Stick clips (§4B) + the rig sheet (§7) | where each camera points → the fused plane's HEADING → `clubPath` |
| Launch monitor on every swing | the criterion for club path, attack angle, clubhead speed (3-D speed is unbuilt because no swing has LM + DTL) |
| Taped-club sweep (§4D) | a non-coplanar reference for a later bundle adjustment — the real unlock for a metric 3-D track |
| Intent blocks (§5 C, D) | SPREAD. Thirty identical swings cannot grade a path or a thrust metric; a draw, a fade and a chair behind the hips can |
| Nothing moves, and §4 repeated at the end | the right to say one calibration covers the session. 07-04's camera moved after swing 3 and split the session in two |

---

## 1. Kit

- Both Chameleon3 cameras, tripods or mounts that lock. GC Quad, placed as usual.
- The 200 × 300 mm ChArUco card (7 × 5, 40 mm squares, `DICT_4X4_50`).
- **Four alignment sticks** (48 in / 1.219 m). A way to stand one vertical (a bucket of sand, a
  stick holder) and a **spirit level** or phone inclinometer.
- Tape measure, masking tape, a pen, **the rig sheet (§7) printed**.
- Clubs: the **taped 7-iron** (bands at 308, 362, 560, 758, 808, 854 mm; length 940, hosel 882),
  the **bare 6-iron**, a wedge, the driver.
- A chair or a fifth stick in a holder, for §5 D.
- Golf balls. If the mat is pale, a **darker hitting strip** under the ball for the DTL view (§3).

⛔ **Instruments: never HackMotion and Witmotion together.** Neither is needed for this session.
If rotation truth is wanted, do it as its own block at the very end (§5 H).

---

## 2. Rig placement — do this first, then do not touch it

Lay the geometry on the floor **before** placing cameras; the sticks are what you aim at.

1. **Target-line stick** through the ball spot, pointing at the target. Tape its ends.
2. **Cross stick** perpendicular through the ball spot (the T of design §4.4). Check square with
   the 3-4-5 rule on the tape measure; tape it.
3. **Hands-line stick**: parallel to the target line, on the line your hands hang over at
   address. Find it rather than guess it: take your 7-iron address and let a ball drop from
   under your hands, and mark where it lands — it will be somewhere between the ball and your
   toes, nearer the toes. Measure its
   offset from the target-line stick **at both ends** (the two numbers must match to ±5 mm; that
   is what makes it parallel). Tape it. This stick is the one the DTL camera is aimed down, and it
   is the second parallel line the 07-04 footage did not have.

**Down-the-line camera**
- Tripod **over the extension of the hands-line stick**, behind you, as far back as the cabin
  allows (every extra half-metre flattens perspective; write the distance down).
- **Lens at hand height** — 0.90–1.00 m for you at address. Measure lens centre to mat.
- **Level** it: no roll, no pitch. Use the spirit level on the body. Pitch is the one bias the
  forward-bend number carries; zero it rather than correct it.
- **Pan until the hands-line stick images VERTICAL and on the frame's CENTRE COLUMN** in the
  preview. That single check sets yaw ≈ 0 and lateral offset ≈ 0 together. The target-line stick
  will then lean slightly inward toward it, which is correct.
- Frame: your whole body **and the clubhead at the top and at the finish**, plus the ball, with a
  hand's width of margin. 07-03 lost ten swings to a crop centred on the screen. Swing a club
  slowly through the top while watching the preview before you lock anything.

**Face-on camera**
- On the **cross stick's extension**, facing you, 1.5–2 m or whatever the wall allows. Lens at
  **hand height** as well, level, no roll.
- Pan until the **cross stick images vertical on the centre column**. Same check, other camera.
- Frame: feet to a club-length above your head, and both ends of the swing arc.

**Launch monitor**: where it normally sits. Check in both previews that it does not hide the ball,
the clubhead at address, or your feet.

**Then:** tighten everything, **tape the tripod feet positions on the floor**, and from here on
touch no lens ring, no tripod and no camera setting until §6 is done. If anything is bumped,
redo §4B and note the swing number it happened after.

---

## 3. Camera settings — set once, write down, leave

Nothing about the lens, the region of interest, the exposure or the gain is recorded in
`swing.json` today. **The rig sheet is the only record.**

- **Frame rate** as now (≈150 fps both). Both cameras the same.
- **Exposure: as short as the light allows, fixed, auto OFF.** Aim ≤ 2 ms. The 07-04 face-on
  club zone averaged 54/255 and the impact-camera work traced its noise to a 6.5 ms exposure.
  If 2 ms is too dark, add light before adding exposure; gain last.
- **Gain fixed, auto OFF. White balance fixed.**
- **Light**: the ring light stays on the face-on camera (the band lock depends on the tape
  saturating). Add **floor-level light on the ball and the mat in front of it**.
- **The ball must not vanish into the mat in the DTL view.** On 06-11 the mat was blown white
  and only the ball's shadow was visible. In the DTL preview the ball should be clearly brighter
  than its surround and **neither should be pinned at 255** — drop the exposure or use the darker
  strip until it is not. The ball is also the DTL ruler (its measured diameter sets cm per
  pixel), so a clean, unclipped ball is worth more than a bright picture.
- **Focus**: on the sticks and ball at the hitting position, aperture a stop or two down from
  wide open for depth of field, then **lock the ring** (tape it). If the face-on lens will not
  come sharp at its end stop, that is back focus, not focus (calibration design §5.6).
- **Write down** per camera: lens focal length (mm, from the barrel), aperture, exposure, gain,
  frame rate, and the **region of interest — width, height, OffsetX, OffsetY** — plus the sensor's
  full size. The DTL stream is a 512-wide window of a 1280-wide sensor; without the offsets the
  principal point is a guess, and the yaw is only as good as that guess.

---

## 4. Calibration clips — about fifteen minutes

Record each as an ordinary capture (the manual shot trigger, or a clap). A capture is about 5 s
with ~3.5 s before the trigger, so **set the scene, hold it, then trigger**. Both cameras record
every clip. Say or note the clip's letter; the swing number goes on the rig sheet.

**A. The card — intrinsics, per lens.** 4 clips per camera.
The card is unreadable at the golfer's distance (about 2 px per marker cell at 1.75 m; it needs
~5). Hold it **0.6–0.8 m from the lens** — slightly soft is acceptable, unreadable is not; check
the preview shows distinct squares. In each 5 s clip move it slowly: centre, then each corner of
the frame, tilted 30–45° left/right/up/down, nearer and further. The corners matter most: that
is where the lens distortion lives. Do **not** touch the focus ring to make the card sharper.

**B. The sticks — where each camera points.** 2 clips, static.
The three floor sticks as laid in §2, the ball on the spot, and the **fourth stick vertical** at
the ball spot, plumbed with the level. Step out of frame. Trigger. Then move the vertical stick
to the far end of the target-line stick and trigger again. (Two parallel floor lines give the
vanishing point by intersection; the vertical gives pitch and roll; the known stick length gives
scale along the line.)

**C. The ball — the ruler.** 1 clip, static: ball on the spot, nothing else moved, you out of
frame.

**D. The taped club as a wand — the volume.** 2 clips.
Hold the taped 7-iron by the grip and sweep it **slowly** (so the bands stay sharp) through the
swing volume: low in front of the ball, up the backswing side, overhead, through to the finish
side; rotate it so it points toward and away from each camera. The six bands at known spacings
are non-coplanar points both cameras see at once.

**E. The clock.** 1 clip: from waist height, **drop a ball onto the mat** where both cameras see
it land. The bounce frame in each view checks the inter-camera clock offset to a frame.

**F. At the END of the session, repeat B and C.** If the two B's disagree, something moved.

---

## 5. Swings

Select the **actual club in the session wizard for every block** and change it when you change
club — the July corpus says "DRIVER" on a taped 7-iron. LM connected and reading for all full
swings. Keep the fifth stick and the chair out of frame except where used.

| Block | Club | Swings | Intent | What it grades |
|---|---|---|---|---|
| **A** | taped 7-iron | 10 | stock | band-lock truth in both views; the plane, posture and sequence baselines; LM speed/path/attack |
| **B** | bare 6-iron | 10 | stock | markerless beside the marked club |
| **C** | taped 7-iron | 5 + 5 | 5 × feel an exaggerated **in-to-out** (draw), 5 × exaggerated **out-to-in** (fade). Exaggerate; ugly is fine | **club path** — the LM gives a spread of several degrees either side, which is what a regression against the fused heading needs |
| **D** | taped 7-iron | 3 + 3 + 3 + 3 | (i) chair or stick touching the back of your hips at address, **stay on it** through impact; (ii) normal; (iii) ball **5 cm closer** than stock, measured; (iv) ball **5 cm further**, measured | **pelvis thrust** has a physical zero in (i); **ball reach** has a measured ±5 cm in (iii)/(iv) — the first truth either metric will have |
| **E** | taped 7-iron | 5 + 5 | 5 × three-quarter swing at ~70 %; 5 × "hips first" — feel the lower body finish early, arms late | **kinematic sequence** — a swing whose trunk peaks BEFORE impact is the only way to validate the trunk node's placement; none exists yet |
| **F** | wedge, driver | 6 + 6 | stock | the plane should steepen wedge → 7-iron → driver-flattest in order; if it does not, the geometry is wrong |
| **G** *(optional)* | any | 10 | a second golfer, stock | everything so far is one golfer |
| **H** *(optional, last)* | 7-iron | 6 | Witmotion on sacrum and sternum, **no HackMotion** | rotation truth, if an `imuIntegrity` pass can be had |

Between blocks, glance at the DTL preview: the hands-line stick (leave it down; step over it)
should still be vertical on the centre column. If it is not, stop and redo §4B.

Waggle as you like, but **hold address still for a full second** before each swing: the address plane, the posture reference and the ruler are all
read there.

---

## 6. Before you leave the bay

Open one swing from each block in the app and check:

- [ ] two videos on the swing; both play; both show you and the whole club at the top and finish
- [ ] the ball is visible at address in **both** views and not clipped white in the DTL
- [ ] the DTL tile draws a shaft at address, mid-backswing and impact (it will vanish at P2, the
      top and P6 — that is correct, the club is end-on there)
- [ ] the launch monitor reading is attached to the swing
- [ ] the club label on the swing is the club you were holding
- [ ] the end-of-session §4B/§4C clips are recorded
- [ ] the rig sheet is filled in, photographed, and the photo is in the session folder

Then leave the floor tape down. If the cameras stay where they are, the next session needs only
§4B, §4C and §4F.

---

## 7. Rig sheet

Date ________  Session folder ______________________  Mat / strip ______________

| | Face-on | Down-the-line |
|---|---|---|
| Camera serial | | |
| Lens focal length (mm) / aperture | | |
| Sensor full size (px) | | |
| ROI width × height | | |
| ROI OffsetX, OffsetY | | |
| Frame rate (fps) | | |
| Exposure (ms) / gain (dB) | | |
| Lens centre height above mat (mm) | | |
| Lens to ball, along its own axis (mm) | | |
| Lateral offset of lens from its aiming stick (mm) — should be 0 | | |
| Level: roll / pitch (°) — should be 0 / 0 | | |

Floor: target-line stick ↔ hands-line stick offset, near end ______ mm, far end ______ mm.
Cross stick square to target line (3-4-5 checked) ☐. Stick length ______ mm.
Ball spot to your toe line at stock address ______ mm.

Clips: A (face-on) swings ____–____ · A (DTL) ____–____ · B ____, ____ · C ____ · D ____, ____ ·
E ____ · F ____, ____, ____.
Blocks: A ____–____ · B ____–____ · C draw ____–____ fade ____–____ · D (i) ____ (ii) ____
(iii) ____ (iv) ____ · E ____–____ / ____–____ · F wedge ____ driver ____ · G ____ · H ____.
Anything bumped, and after which swing: ______________________________________________

---

## 8. What happens to it afterwards

1. `tools/shaftlab/dtl_yaw_probe.py` on the B clips: the vanishing point is now an
   INTERSECTION of two parallel sticks; with the focal length from A and the ROI offsets it
   becomes a yaw and a pitch in degrees, checked against the level and the rig sheet.
2. `tools/shaftlab/fuse_probe.py --yaw … --pitch …` and the app stage
   (`shaft.fusion.dtlYawDeg` / `dtlPitchDeg`): the plane heading, against the LM's club path
   across block C. **That is the test that decides whether `clubPath` leaves "planned".**
3. `tools/swinglab/dtl_posture_grade.py`: block D against its physical zero and its ±5 cm.
4. `tools/swinglab/sequence_report.py` on block E, pair route on and off.
5. Clubhead speed against the LM **less about 2 mph** (the GC Quad over-reads at 70–100 mph),
   face-on as it is and de-projected through the fused plane, to settle why the uncorrected
   number is already nearly right.
6. The card and wand clips are kept for the calibration thread's offline solve; nothing reads
   them yet.

**Owed in software, found while writing this:** `swing.json` should record each camera's lens,
ROI offsets, exposure and gain, so the rig sheet stops being the only copy.
