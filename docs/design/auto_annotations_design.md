# Auto annotations: the coach's static marks, and the synthetic shaft as its own motion element

**Status:** design + build, 1 Oct 2026. Mark, on seeing the 3-D synthetic shaft's dashed line on the
down-the-line tile: "I think the plane is a new kind of annotation — it is not dynamic is it? It's the
kind of thing a coach would draw … circle round the head at address for face on, vertical lines on hips
on face on at address, vertical line at edge of butt at address on dtl and swing plane on dtl. We should
add a motion overlay toggle for Auto annotate" — and, separately, "the dynamic plane would still be an
excellent addition, but should have its own preset on the motion menu."

Two different things, and (Mark, second round) they live in two different menus: the motion
overlays are for specific kinds of analysis and belong on the Motion menu; the coach's lines are static
reference marks a swing is looked at against, available all the time, and belong on the View menu.

| what | where | what it draws | moves? | tile |
|---|---|---|---|---|
| **Synthetic shaft** — motion element `synth`, preset "Synthetic club" | Motion menu (modes off / frame) | the 3-D synthetic club (`dtl_shaft_synth3d.h`) projected into the DTL image — the dashed line that used to ride on the Shaft element | yes, with the club | DTL only |
| **Coach lines** — `ViewLayout.coachSetsFor(mode)`, stored per mode as `coachLineSets: {posture, plane, headHips}` (§1c) | View menu: a "Coach lines" heading under Replay with three toggles, Posture / Plane / Head & hips; Replay and Analyse only (Capture never draws them; the rows are hidden there) | the coach's reference marks, fixed at ADDRESS and held through the replay | no | both |

The motion element defaults off in every existing preset and the View toggle defaults off, so nothing
a user has today changes.

## 1. The marks (Coach lines)

All four are measured ONCE, on the frame nearest the Address instant (`phases` phase 0, the swing's own
address, not the replay's first frame), from the same replay payload the skeleton is drawn from, and
then drawn unchanged on every frame of the swing window. That is the point of a coach's line: the
golfer moves against it.

| mark | tile | from | rule |
|---|---|---|---|
| **Head circle** | face-on | the pose at address: ears (3, 4), eyes (1, 2), nose (0) — the smoothed series when it exists, raw detections otherwise, conf > 0.3 | centre = the mean of the visible head keypoints; radius = 0.75 × the ear-to-ear distance, else 1.1 × the eye-to-eye distance, else 4 % of the frame height; floor 2.5 % of the frame height |
| **Hip lines** | face-on | `analysis.addressMarks.faceOn` — the body's OUTER edges from the person mask (§1a) | one vertical line per side through the mask's outer edge: the outside of each hip/upper thigh, the cue for sway. Never the hip joint: a line on the joint would read as an edge and lie by half a pelvis |
| **Butt line** | DTL | `analysis.addressMarks.dtl` (§1a) | one vertical line through the mask's REAR edge at hip height — the edge of the butt, the cue for moving off the line |
| **Plane line** | DTL | the DTL ball (`dtl.ball.samples[0]`) and the DTL club's grip on the measured sample nearest address (`dtl.club.samples`, within 60 ms, flags not projected/coasted) | the classic address shaft line: through the ball and the grip, extended to the content rect's edges both ways. Without a ball: the address sample's own grip → head line extended. Without a measured address sample: no line |
| **Spine line** | DTL | the DTL pose at address: the TRAIL hip joint (12 right-handed, 11 left-handed — the trail leg's first point) and the joint neck (halfway from the shoulder midpoint, 5 and 6 both confident, to the mean of the confident ears 3 / 4, at least one — the point §1b's neck walk starts from), conf > 0.3 | from the trail hip to the neck, joining the trail leg and running through the trunk (23 Oct 2026). No tail below the hip, no extension above the neck. A hip or a neck not confident: no line (and no shoulder plane), never half a line. Needs no address marks, so a swing analysed before version 2 draws it too |
| **Shoulder plane** | DTL | the spine line above, the TRAIL shoulder joint at address (6 for a right-hander, 5 for a left-hander, `leadIsLeft`), conf > 0.3, and the drawn image's aspect | TPI's flat-shoulder-plane line: PARALLEL to the spine line, offset from the trail shoulder joint by 0.25 of the spine line's length at a right angle to it, upward (toward the back); from 0.7 of that length below the offset point to 0.5 above it. The offset is measured in image PIXELS (normalised coordinates are stretched per axis; the tile passes its content rect's width / height). No spine line, no confident trail shoulder or no aspect: no line. Dotted |
| **Hogan line** | DTL | the shaft plane's ball (`dtl.ball.samples[0]`) and the TRAIL shoulder joint at address, conf > 0.3 | from the ball through the trail shoulder to the picture's edge; with the dashed shaft plane it bounds the "V zone". No ball or no confident trail shoulder: no line. Long dashes |
| **Trail leg** | DTL | the DTL pose at address: hip → knee → ankle of the trail side (12/14/16 right-handed, 11/13/15 left-handed), conf > 0.3 | one polyline, drawn only when its three points are confident — never half a leg. The lead leg is not drawn (removed 22 Oct: behind the trail leg from down the line, a distraction) |

**Spine, shoulder plane, Hogan and trail leg (22 Oct 2026).** Mark: "it would be ace to have coaching
lines on the dtl view that show the spine and leg angles frozen at address. then we can see how the golfer
moves through the swing." Three rounds the same evening:
1. The spine through the joint midpoints (hip 11/12 → shoulder 5/6, on through the head) with both legs
   from the pose, and a shoulder plane — first at 90° to that spine (the Hogan / Watson "shoulders turn
   perpendicular to the spine" picture), then TPI's line parallel to it.
2. The spine became TPI's, "from the base of the tailbone to the back of the neck" (Mark: "spine line to
   the neck is much better") — two points on the REAR outline, so from the person mask (§1b); the lead
   leg was removed; the Hogan line was added.
3. 23 Oct: the spine became the trail hip joint → the joint neck. Mark: "the solid green spine line is
   supposed to run from the hips (joining the line that is already there from the knee to the hips) to the
   neck." The rear-outline line followed a hoodie's hood and the back, not the trunk, and did not meet the
   trail leg. The shoulder plane keeps its margin and extent as fractions of the new line's length; the
   numbers below were measured against the round-2 line (worth a look on a real frame).

TPI's check (mytpi.com, "Flat Shoulder Plane"): "draw a line from the base of the tailbone to the back of
the neck" (the spine angle line), then a second line "just above the trailing shoulder and parallel to the
spine angle line"; at the top, "if the lead shoulder is above the line they have a flat shoulder plane."
The numbers, all from Nick Henn's 20 Oct swing 1 address frame (DTL pose + the address-marks mask; one
golfer — worth a look on the corpus on the cabin PC):
- **spine tail 0.1** of its length below the tailbone, so the line's start reads past the waistband; no
  extension at the neck end. On Nick the line is 222 px (1.19 × the hip→shoulder joint length L) and
  leans 51° from vertical against the joint line's 40°: it is a chord across a rounded upper back.
- **shoulder-plane margin 0.25** of the spine line's length (0.30 L): walking from the trail shoulder
  joint at a right angle to the new spine line, the body's outline is 0.22 of it away (48 px), so 0.25 is
  just clear of the shoulder's outline — "just above" it. The joint itself is 0.10 BELOW the spine line
  (inside the chord) and the drawn line ends up 0.15 above the spine line, so the two read apart.
- **shoulder-plane extent** 0.7 below the offset point to 0.5 above it (in spine lengths), spanning the
  shoulders through the swing.
- **Hogan line to the picture's edge**: the hands at the top sit 1.25 of the ball→shoulder length up the
  line (the club beyond them) and the edge is at 1.78; a fixed length short of the edge risks ending under
  the club.
- **style**: shoulder plane in round dots, 2.5 px, 5 px apart (Qt's dash pattern is in pen widths); 1.5 px
  dots vanished over his dark shirt. Hogan line in 18 px dashes with 6 px gaps, three times the shaft
  plane's, so the two sides of the V zone read as two lines.

Mark's correction after the first cut (which drew the hip JOINTS): "on the DTL view they should just touch
the butt of the golfer — it's a cue to watch the golfer moving forward off that line; on the face-on
view the vertical lines should be on the outside of the hips left and right — a cue to watch the amount
of sway". The pose only knows joint centres, so the edges are measured from the picture.

### 1a. The edges: the person mask at address (`address_marks.h`, `AddressMarksStage`)

The app already carries a person segmenter (`src/Pose/person_segmenter.h`, u2netp, used to suppress
the background before MoveNet in the Film tab). A new analysis stage, after the DTL pose, decodes the
one frame nearest the swing's Address instant on each camera, runs the segmenter (CV_32F mask, person
≈ 1), and reads the mask along the rows at hip height:

- **Which rows.** The hip joints' row on both cameras, ±1.5 % of the frame height (31 rows on a
  1024-high frame). The wrists' row was tried first for the face-on view, because at hip height the
  arms hang outside the hips and the mask's outer edge there can be an elbow (07-04 s7: ~1 % of the
  frame outside the thigh line), and it was REJECTED on the library's s8: the segmenter drops the dark
  lead thigh beside the bright glove at that height, so the "edge" became the glove, 5 % of the frame
  INSIDE the thigh. An elbow is a small error on the outside and is the outline a coach's pen follows
  anyway; the glove is a large one on the inside. `rowSource` on the record says which rows were read.
- **How an edge is read.** Each side from ITS OWN hip joint (the left edge walking left from the left
  hip, the right from the right), the seed snapped to the nearest person pixel within 12 px, the walk
  going the whole reach — one and a half hip widths, at least a fifth of the frame width — and keeping
  the farthest person pixel it met. Not the seed's own run: at the wrists' row the hands hang in FRONT of
  the lead thigh as their own blob with a thin gap to the thigh behind (s7: the first cut's edge landed on
  the glove, 12 px inside the thigh). The reach is what keeps a background object out. Per-row edges are
  medianed; fewer than three usable rows on a side is not a measurement.
- **The butt side** (DTL) is the side away from the hands (the pose's mid-hands x); without hands no
  butt is claimed.
- **Record**: `analysis.addressMarks {faceOn, dtl: {found, t_us, rowY, rowSource, hipLeftX, hipRightX,
  leftX, rightX, buttX, side, rows, tailboneX, tailboneY, neckX, neckY, backWalks}}`, normalised;
  `versions.addressMarks` 2 (1 before 22 Oct: no spine points)
  (`kAddressMarksStageVersion`); never reused; ~1.1 s for both cameras on the Mac. The replay payload
  carries each view's object on its tile's detail (`addressMarks`, `dtl.addressMarks`).
- **Build**: the segmenter is compiled into the swinglab tool as well as the app (CMake: the offline
  targets gain `HAVE_SEGMENTER` and `person_segmenter.cpp` + `pp_gpu_metrics` when the model file is
  present at configure time); without it the stage records that it did not run and the tile draws no hip
  or butt line.
- **Checked on 07-04 s7, and the library's s4 and s8** (`scratch/dtl-precalib-20261003/m_marks`, the
  frame + mask dumps via `PINPOINT_ADDRESS_MARKS_DUMP=<dir>`): face-on hip joints 0.50–0.57 → edges
  0.47–0.60 at the hip row, both lines on the body's outer outline (the lead one on the arm-and-hip
  line); DTL joints 0.25–0.33 → rear edge 0.13–0.17, on the back of the jeans at belt height, the hands
  on the right so the rear is the left; a body clipped by the frame (07-04 s1–3) gives no butt line. Test
  `address_marks_test` (14 cases: holes crossed, the reach cap, the per-side seed, the glove in front
  of the thigh, the row choice, the butt side).

### 1b. TPI's spine angle line: the tailbone and the back of the neck (`measureBackLine`)

On the DTL view only, when the butt side is known (the hands are seen), both shoulders and both hips
are confident and at least one ear is. Each point is the REAR outline of the silhouette, read by walking
at a RIGHT ANGLE to the trunk axis (hip-joint midpoint → shoulder-joint midpoint, length L) toward the
rear (the butt line's side): 11 parallel walks with seeds spread ±0.05 L along the axis, each keeping the
farthest person pixel within the butt line's reach; a walk that meets the frame's edge, or whose seed has
no person pixel within 12 px, is dropped; the distances are medianed; fewer than three walks is not a
measurement (and then neither point is written — never half a line).
- **Why at a right angle, not along a row.** The trunk leans ~40° forward at address. A row at neck
  height reads the UPPER BACK: on Nick Henn's frame the row halfway between the shoulder joints and the
  ears met the outline at x 518, 70 px behind the collar (x 590).
- **The tailbone**: behind the hip-joint midpoint (0 of the way up the axis). On Nick it lands at the top
  of the seat at the waistband (0.426, 0.486); 0.1 and 0.2 L up climb onto the shirt above the belt.
- **The back of the neck**: behind the point halfway from the shoulder-joint midpoint to the ears'. On
  Nick 0.5 lands on the collar — the base of the neck (0.604, 0.348); 0.25 is on the upper back, 0.75 and
  1.0 climb toward the hairline and, for a more upright golfer, the back of the head.
- **Record**: `tailboneX/Y`, `neckX/Y` (normalised, null when not measured) and `backWalks` on
  `analysis.addressMarks.dtl`; `kAddressMarksStageVersion` 2. A swing analysed before has none, and its
  DTL tile shows the butt, shaft-plane, Hogan and trail-leg lines but no spine and no shoulder plane until
  it is re-analysed. Test `address_marks_test` §7 (a 45°-tilted drawn body: the points at a right angle
  to the axis, a row reading the upper back instead, the mirrored side, no rear side / no ears / a clipped
  body ⇒ nothing, a hole crossed).

### 1c. Three sets, three toggles (22 Oct 2026)

Mark, with all six DTL lines on: "lots of lines and really confusing now — the coaching lines need some
options". The single View toggle became a "Coach lines" heading under Replay with three independent
toggles. Every mark carries its set (`set` on the mark, AutoAnnotations.js); `filterSets(marks, sets)`
keeps the marks whose set is on, and the tile draws only those. Nothing else about the lines changed.

| toggle | set key | face-on | down the line |
|---|---|---|---|
| **Posture** | `posture` | — | spine (tailbone → back of the neck), shoulder plane (dotted), trail leg |
| **Plane** | `plane` | — | address shaft plane (dashed), Hogan line (long dash) — the "V zone" |
| **Head & hips** | `headHips` | head circle, hip edges | butt line |

- **Stored** per mode beside the old key: `coachLineSets: {posture, plane, headHips}` (ViewLayout
  `coachSetsFor` / `coachSetOn` / `setCoachSet`). `coachLinesOn(mode)` now means "any set on" (the tile's
  gate). `setCoachLines(mode, on)` is kept as a shim that turns all three on or off.
- **Migration**: with no `coachLineSets` stored, a stored `coachLines: true` reads as **Head & hips +
  Plane** on (what the old toggle drew), Posture off; `false` or absent reads as all off (the default).
  The first write of the mode stores the migrated sets explicitly. `coachLines` is still written, as
  "any set on", so an older build reads something sensible.
- **Capture** never has any set on, whatever is stored.
- **Tests**: `tst_coach_line_sets.qml` (the real ViewLayout over the suite's scratch settings: default
  all off; each set toggles alone and persists, per mode; a layout write keeps them; Capture none; the
  migration both ways; the shim); `tst_auto_annotations.qml` (every mark has its set; the filter).

What a mark says and does not say: every mark is a reading of the pose/mask/ball/club AT ADDRESS, in
image pixels. None of them is a metric. They are computed in the tile from what the payload carries:
the pose/ball/club marks need no re-analysis, the mask-read ones (hip edges, butt, and since version 2
the spine's two points) are whatever `analysis.addressMarks` holds — absent until the swing is analysed
with that version.
The plane line is the ADDRESS shaft line — the one coaches draw — not the fused swing plane, and its
heading on the DTL tile is whatever the camera's yaw makes it, as every DTL angle is until the
calibration (`dtl_precalibration_20261003.md` §2).

Drawn in one pen (the theme's good colour so they read as "reference", not as the club or the body):
2.6 px at alpha 0.8 — `kCoachPenPx`, `kCoachDotPx`, `kCoachAlpha` on PpCameraFrame, the one place they
are set. (Until 22 Oct 1.5 px at 0.75; Mark: "they need to be thicker but with a hint of translucency
… as not easily discerned" — ×1.75, and 0.8 still lets the swing show through while reading over both
the dark shirt and the bright mat.) Dashed for the plane line (it is an extension past where anything
was measured), solid for the circle, the verticals, the spine and the trail leg (polylines, round join
at the knee), dotted for the shoulder plane (4.4 px round dots two dot-widths apart), long dashes for
the Hogan line (18 px dash, 6 px gap). Qt's `setLineDash` counts in pen widths, so the dash patterns
are given in pixels and divided by the pen: the dashes kept their on-screen lengths when the pen
thickened, and the dots' spacing grew with the dots so they stay dots; drawn
UNDER the skeleton and club, before the frame pass. The coach lines are a View toggle, not a motion
element: the overlay canvas shows when Motion is on OR the coach lines have something to draw, and with
Motion off the paint returns after the coach-lines pass, so nothing else draws (until 22 Oct the canvas
was hidden whenever Motion was off and the toggle drew nothing then). Toggling them while the replay is
paused repaints at once.

## 2. The synthetic shaft as its own element

Today (`3109778d`, `7c59b85a`) the DTL tile draws the synthetic line whenever the Shaft element is in
Frame mode. That hides a synthesis inside a measurement's switch. Now:

- `synth` element, modes off / frame (fan and trace permanently unavailable — a synthesis is never a
  trail, the rule the Shaft element's own fan already follows for coasted frames).
- Drawn exactly as before (dim dashed under the measured line, within one and a half frame intervals
  of the playhead) but only when `_elemMode("synth") === "frame"`; the Shaft element no longer draws it.
- Available on the Customise page only when the loaded swing carries `dtl.club.synth3d` (a two-camera
  swing analysed since 1 Oct 2026); the face-on tile ignores it.
- Preset **Synthetic club**: `shaft: frame, synth: frame`, body off — the measured club with its
  synthesis under it, nothing else, so the two can be compared.

## 3. Files

- `src/Gui/session/ViewLayout.qml` — the `synth` key in `_fillModes`, every preset's explicit mode
  list ("off"), the "Synthetic club" preset; `coachSetsFor` / `coachSetOn` / `setCoachSet` and
  `coachLinesOn(mode)` (any set on) with the per-mode `coachLineSets` beside `panels / arrangement /
  motion`, migrating the old `coachLines` bool (§1c).
- `src/Gui/session/PpViewPanel.qml` — the "Coach lines" heading and its three toggles (Posture, Plane,
  Head & hips; objectNames `coachSetPosture|Plane|HeadHips`) under REPLAY, hidden in Capture.
- `src/Gui/session/PpMotionPanel.qml` — the row "Synthetic shaft" under Objects; availability
  frame-only when `dtl.club.synth3d` exists.
- `src/Analysis/address_marks.h` (the measurement, pure OpenCV), `address_marks_json.h` (the block and
  the payload objects), `AddressMarksStage` in `wrist_analyzer.cpp`, `swing_doc.cpp` (the block),
  `shot_processor.cpp` / `disk_replay_source.cpp` (the payload), `analysis_versions.h`, the CMake wiring.
- `src/Gui/cameras/AutoAnnotations.js` — pure functions, no Qt: `build(det, isDtl, addressUs, leadIsLeft,
  aspect)` returns the marks in NORMALISED coordinates (`circle {cx, cy, r}`, `vline {x, label}`, `line {x0, y0,
  x1, y1}`, `polyline {pts, dash}`); the head, the plane line, the Hogan line, the shoulder plane's
  anchor and the trail leg from the pose/ball/club in the payload; the hip and butt lines and the spine
  from `det.addressMarks`. Testable alone.
- `src/Gui/cameras/PpCameraFrame.qml` — `_annotations` (recomputed when the detail, the address
  instant or the View toggles change, not per paint; gated on `ViewLayout.coachLinesOn` and on the
  tile being a replay tile, independent of the Motion master switch, and filtered to the sets that are
  on, `filterSets(…, ViewLayout.coachSetsFor(mode))`); the paint pass; the synth gate
  moved to its own element.
- `src/Gui/tests/qml/tst_coach_line_sets.qml` — the three View toggles' storage and migration (§1c).
- `src/Gui/tests/qml/tst_auto_annotations.qml` — the JS against a synthetic payload: the circle sits on
  the head and scales with the ears; the hip lines are at the hip x's; the DTL plane line passes through
  the ball and the grip and reaches the frame edges; no ball ⇒ the grip→head line; no address pose ⇒ no
  marks; a non-DTL tile never gets the butt or plane lines; the spine runs tailbone → back of the
  neck from the address marks, ends at the neck, ignores the joints and is absent on a swing without the
  version-2 points (as is the shoulder plane); the trail leg only, never the lead, named by handedness,
  never half a leg; the shoulder plane is parallel to the spine in pixels on a 976×1024 and a 16:9 frame,
  0.25 spine lengths above the trail shoulder (the left one for a left-hander), dotted, absent without the
  spine, the trail shoulder or an aspect; the Hogan line starts at the ball, passes through the trail
  shoulder and reaches the picture's edge, long-dashed, absent without the ball or the trail shoulder;
  none of them on face-on.

## 4. Definition of done

"Synthetic club" appears in the Motion menu in Replay and its Customise row greys out when the data is
absent; on a 4 July swing the DTL tile shows the dashed synthetic line only with "Synthetic club" (or
the row on) and never with "Clean"; the View menu's "Coach lines" toggle (Replay and Analyse, not
Capture) shows the head circle and hip lines on the face-on tile and the hip and plane lines on the DTL
tile, fixed while the swing plays, with the Motion master switch on OR off; `qml_ui_test` passes.

## 5. Not built

Marks taken at any instant other than address; persistence or export of the marks;
the synthetic shaft in the 3-D panel.
