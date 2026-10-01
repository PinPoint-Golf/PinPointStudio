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
| **Coach lines** — `ViewLayout.coachLinesOn(mode)`, stored per mode as `coachLines: bool` | View menu toggle, Replay and Analyse only (Capture never draws them; the row is hidden there) | the coach's reference marks, fixed at ADDRESS and held through the replay | no | both |

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
  leftX, rightX, buttX, side, rows}}`, normalised; `versions.addressMarks` 1
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

What a mark says and does not say: every mark is a reading of the pose/mask/ball/club AT ADDRESS, in
image pixels. None of them is a metric; nothing persists; the payload is unchanged (they are computed in the
tile from what the payload already carries, so no re-analysis is needed and old swings get them too).
The plane line is the ADDRESS shaft line — the one coaches draw — not the fused swing plane, and its
heading on the DTL tile is whatever the camera's yaw makes it, as every DTL angle is until the
calibration (`dtl_precalibration_20261003.md` §2).

Drawn in one thin pen (1.5 px, 0.75 alpha, the theme's good colour so they read as "reference", not as
the club or the body), dashed for the plane line (it is an extension past where anything was measured),
solid for the circle and the verticals; drawn UNDER the skeleton and club, before the frame pass.

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
  list ("off"), the "Synthetic club" preset; `coachLinesOn(mode)` / `setCoachLines(mode, on)` with the
  per-mode `coachLines` bool beside `panels / arrangement / motion` (absent ⇒ off).
- `src/Gui/session/PpViewPanel.qml` — the "Coach lines" toggle under REPLAY, hidden in Capture.
- `src/Gui/session/PpMotionPanel.qml` — the row "Synthetic shaft" under Objects; availability
  frame-only when `dtl.club.synth3d` exists.
- `src/Analysis/address_marks.h` (the measurement, pure OpenCV), `address_marks_json.h` (the block and
  the payload objects), `AddressMarksStage` in `wrist_analyzer.cpp`, `swing_doc.cpp` (the block),
  `shot_processor.cpp` / `disk_replay_source.cpp` (the payload), `analysis_versions.h`, the CMake wiring.
- `src/Gui/cameras/AutoAnnotations.js` — pure functions, no Qt: `build(det, isDtl, addressUs)` returns
  the marks in NORMALISED coordinates (`circle {cx, cy, r}`, `vline {x, label}`, `line {x0, y0, x1,
  y1}`); the head and the plane line from the pose/ball/club in the payload, the hip and butt lines from
  `det.addressMarks`. Testable alone.
- `src/Gui/cameras/PpCameraFrame.qml` — `_annotations` (recomputed when the detail, the address
  instant or the View toggle changes, not per paint; gated on `ViewLayout.coachLinesOn` and on the
  tile being a replay tile, independent of the Motion master switch); the paint pass; the synth gate
  moved to its own element.
- `src/Gui/tests/qml/tst_auto_annotations.qml` — the JS against a synthetic payload: the circle sits on
  the head and scales with the ears; the hip lines are at the hip x's; the DTL plane line passes through
  the ball and the grip and reaches the frame edges; no ball ⇒ the grip→head line; no address pose ⇒ no
  marks; a non-DTL tile never gets the butt or plane lines.

## 4. Definition of done

"Synthetic club" appears in the Motion menu in Replay and its Customise row greys out when the data is
absent; on a 4 July swing the DTL tile shows the dashed synthetic line only with "Synthetic club" (or
the row on) and never with "Clean"; the View menu's "Coach lines" toggle (Replay and Analyse, not
Capture) shows the head circle and hip lines on the face-on tile and the hip and plane lines on the DTL
tile, fixed while the swing plays, with the Motion master switch on OR off; `qml_ui_test` passes.

## 5. Not built

A shoulder plane line (ball → trail shoulder, the second line of the
"two-plane" drawing); marks taken at any instant other than address; persistence or export of the marks;
the synthetic shaft in the 3-D panel.
