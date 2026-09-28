# 3-D swing annotations — the camera tiles' motion overlays, drawn in the 3-D swing panel

*2026-09-27. Status: **APPROVED and being built** — Mark's answers to §8 are recorded there, and
they added two things to the design: the match-the-camera presets (§5a) and the fused downswing
plane (§5b). Follows
`swing_3d_viz_design.md` (the panel, `skeleton3d`, §12 = what was built). Written after surveying
what the camera tiles draw today (`PpCameraFrame.qml`, `ViewLayout.qml`, `PpMotionPanel.qml`) and
what the 3-D panel already holds (`SwingRigDriver`, `SwingViz3DView.qml`).*

## 0. Summary

The Motion settings that dress the face-on and DTL tiles (arms, spine, shoulders, hips, legs,
shaft, grip, ball, each set to off, frame, fan or trace, plus the P-positions) are drawn in the
"3-D swing" panel as well. The **same** settings drive both, so one change to the Motion pill
dresses the video tiles and the 3-D figure together. From the panel's presets (face-on,
down-the-line, top, target side, behind) or any orbit, the traces are then seen in 3-D.

The annotations are **re-derived natively in 3-D from the fitted skeleton**. The 2-D overlays are
not lifted off the video. A pixel has no depth, so a 2-D trace pushed into the scene is only right
when seen from its own camera. The skeleton3d fit *is* those keypoints (and the shaft) solved in
3-D, so the lead wrist's trace from the fit is the lead wrist's trace from every side.

It is built in three parts:
1. **The driver** (`SwingRigDriver`) bakes each element's anchor points and bone segments into
   scene coordinates once per swing, from data it already loads.
2. **One C++ geometry item**, `SwingAnnotationGeometry` (`QQuick3DGeometry`). It builds the traces
   as tubes and the frame and fan modes as stick segments, with the fade carried in the vertex
   colours.
3. **The view** places one geometry per element and binds it to `ViewLayout`'s motion settings
   and the playhead. P-position labels are 2-D text pinned to their 3-D points.

## 1. Deliverables and definition of done

| Deliverable | What |
|---|---|
| **Document** | This file. After the build it gains a §9 "Built and graded" section with the reprojection numbers (§7.2), in the same form as `swing_3d_viz_design.md` §12 |
| **Implementation** | `src/Gui/swing3d/swing_annotation_geometry.{h,cpp}` (new); `swing_rig_driver.{h,cpp}` (anchors, P-positions, phase window); `SwingViz3DView.qml`, `SwingViz3DHost.qml`, `ScreenWrist.qml` (motion settings wiring); `src/Gui/tests/swing_annotation_geometry_test.cpp` (new), `swing_rig_driver_test.cpp` (extended); `tools/probes/swing3d_panel.qml` (extended); `tools/swinglab/swing3d_trace_reproject.py` (new, §7.2) |

**Done when** all of the following hold:
- Every element and mode in §2 appears in the panel under the same Motion setting as the tiles.
- §7.1 passes.
- §7.2's table is filled in for the 24 two-camera swings and judged by Mark. A table that shows the
  3-D traces disagree with the video still counts as done, provided it is written up honestly. A
  skipped table does not.
- The 30-minute soak is clean on the Mac (§7.3).

## 2. What is carried across, element by element

What the tiles draw is catalogued in `PpCameraFrame.qml` (layers A–K). Here is each layer's fate
in 3-D:

| Tile layer | In 3-D | Anchor / geometry | Source in `skeleton3d` |
|---|---|---|---|
| **B** frame skeleton (per element) | **Yes**, as a stick overlay on the hull (§4.2) | That element's bones as line segments, in the blueprint palette (`Theme.pose*`) | FK joint positions |
| **C** fan, last 240 ms, ≤ 24 frames, alpha 0.08 → 0.55 | **Yes**, same window, cap and fade | Sticks at past frames; arms fans the lead arm only, as on the tiles | FK at past frame times |
| **D** trace, address → playhead | **Yes** | Tube through the anchor (table below) | FK joints, `grip`, `shaftDir`, `clubLengthM` |
| **E** club shaft, frame mode | **Already drawn** (the club model) | Frame mode also adds the 10-sample head trail, as on the tiles | `grip` + `shaftDir` |
| **F** P-positions P1–P8 | **Yes** | A dot at each position's clubhead; within ±40 ms, the shaft at that instant plus a "P<n>" label | `club.positions[].t_us` → fitted shaft at that time |
| **G** shaft fan | **Yes** | Shaft segments at past frames | `grip` + `shaftDir` |
| **H** ball | **Already drawn** | Now obeys the ball element's setting (today it is always on) | `display.ball` |
| **I** predicted shaft (dev) | **No** | Dev-only on the tiles, no UI; a tracker diagnostic in image space | — |
| **J** impact-camera arc | **No** (§8 q4) | In the impact camera's image coordinates; there is no 3-D model of that camera | — |
| **K** telestrator | **No** | The user's own 2-D marks on one video | — |
| **A** live skeleton | **No** | Capture only; the panel is a replay view | — |

**Trace anchors**, mapped from `_traceAnchor()` (`PpCameraFrame.qml:169`) onto the rig
(`ybot_rig.h` joint names). "Lead" follows `ViewLayout.leadIsLeft()`, exactly as the tiles do.

| Element / target | Tile anchor (COCO kp) | 3-D anchor |
|---|---|---|
| arms / `leadWrist` | lead wrist (9/10) | lead `Hand` joint origin (the wrist) |
| spine / `neckMid` | shoulder midpoint (5, 6) | midpoint of `LeftArm` and `RightArm` (the shoulder joints) |
| shoulders / `leadShoulder` | lead shoulder (5/6) | lead `Arm` joint |
| hips / `pelvisMid` | hip midpoint (11, 12) | midpoint of `LeftUpLeg` and `RightUpLeg` |
| legs / `leadAnkle` | lead ankle (15/16) | lead `Foot` joint |
| `head` override | ear midpoint, else nose | the fit's ear-midpoint marker on `Head` (its fixed marker offset, §12.2 item 1) |
| shaft (clubhead) | `club.synth` head | `grip − 0.04·shaftDir + clubLengthM·shaftDir`, the tip of the club the panel already draws |
| shaftGrip | `club.synth` grip | `grip` |

The COCO keypoints are surface points and the rig's joints are joint centres. They are not the
same point: the fit models the shoulder and hip keypoints with a fixed width/height offset
(§12.2 item 1 there). **The 3-D anchor is the joint centre**, because the fitted marker offsets are
not persisted in `analysis.skeleton3d`. Persisting them would be a producer change with a version
bump and a library re-analysis, for a few centimetres. The offset is therefore part of what §7.2
measures, and it is reported per element rather than hidden. The head anchor is the `Head` joint
(skull base), not the ear midpoint, for the same reason.

## 3. One setting, both views

- The panel reads `ViewLayout.motionFor(mode)`, `elementMode`, `motionTraceTarget` and `leadIsLeft`
  for the screen's current session mode, the same calls `PpCameraTiles.qml:168–195` makes.
  **No new AppSettings key, no new UI, no menu.** The Motion pill already exists, and a second
  one would drift.
- `SwingViz3DHost` gains `motionOn`, `motionModes`, `motionTraceTarget` and `leadIsLeft`, and
  passes them through as it does `swingDir`/`positionUs`. `ScreenWrist.qml` binds them from
  `ViewLayout`, alongside its existing tile bindings.
- **Availability.** `PpMotionPanel._availableIn` greys an element out when the tile has no data
  for it. It is not extended: the 3-D panel is data-gated as a whole (no `skeleton3d` → its
  placeholder), and every element is derivable from any valid fit.
- **The swing window.** The tiles draw only between Address and Finish (`_inSwingWindow`). The
  driver already reads `analysis.phases` for Address and Impact, and gains Finish. Traces start at
  Address. The fit itself starts at address − 150 ms (§12.2 item 17), so that lead-in is never
  traced. When nothing is playing (`positionUs < 0`, rest at address), no trace or fan is drawn,
  and the P-position dots are.

## 4. Geometry

### 4.1 Why a C++ geometry, and why tubes

Qt Quick 3D has no polyline with a width. The candidates:

| Option | Verdict |
|---|---|
| `Repeater3D` of `#Cylinder` models per segment | 600 frames × 7 elements = thousands of Models. Rejected |
| `QQuick3DGeometry` with `PrimitiveType::Lines` | 1 px on every backend, and invisible on a high-DPI panel. Rejected |
| Camera-facing ribbon | Needs a rebuild on every orbit step, not only every playhead step. Rejected |
| **Tube**: a circle (6 sides) swept along the anchor path | View-independent: built once per swing and element, and only **trimmed** at the playhead. **Chosen** |

`SwingAnnotationGeometry : QQuick3DGeometry`, `QML_ELEMENT`, has these properties:
- `driver`
- `element` (arms…shaftGrip)
- `mode` (frame | fan | trace)
- `target`
- `leadLeft`
- `revision` (the driver's; every per-bone read already takes it, and a bare `revision` statement
  is dropped by the compiler)
- `radius` (m; trace 4 mm, sticks 3 mm)

It reads the driver's prepared arrays in C++. **No QVariantMap or JS array crosses into QML**:
every QML read of one is a deep copy (see the QmlPayload finding).

- **trace**: the full tube is built when the swing or the anchor changes. On a playhead step only
  the index count changes (the vertices up to `t` stay put). The partial last segment is
  interpolated to the playhead, so scrubbing is exact at time, like the figure. Frames the fit
  tiers `absent` for that anchor break the tube (a gap, not a bridge). `inferred` frames are
  drawn at 40 % alpha through the vertex colour, the rule the figure uses.
- **frame**: the element's bones at the playhead as thin tubes, ≤ 8 segments. This is rebuilt
  every tick, which is trivial.
- **fan**: the same sticks at each past frame in the 240 ms window (≤ 24), with alpha from the
  tiles' ramp, in one vertex buffer. The element's current frame is drawn full, as on the tiles.

Materials: `PrincipledMaterial { lighting: NoLighting; vertexColorsEnabled: true }` with alpha
blending. Colours: `_traceColor()`'s rule (accent; grip `Qt.lighter(accent, 1.7)`), and the
`Theme.pose*` palette for sticks.

### 4.2 Depth

On video every overlay sits on top of the picture. In 3-D, what makes a trace *read* as 3-D is
the body hiding the part of it that passes behind. So annotations are **depth-tested** by
default: the clubhead arc disappears behind the golfer on the far side, as it should.

Frame-mode sticks sit *inside* the hull and would vanish entirely. For them, frame mode fades the
hull to ~35 %, so the skeleton reads through it (Mark, §8 q2). How this avoids the sorting trap:
- The faded hull uses `depthDrawMode: OpaquePrePassDepthDraw`. A depth pre-pass lets only its
  front-most surface blend, so a transparent skinned mesh cannot show its own far side.
- The frame-mode sticks are **opaque** (`alphaMode: Opaque`), so they are drawn in the opaque pass
  *before* the hull. The hull then blends over them.
- Traces and fans stay transparent (`Blend`), because their fade is their meaning.

⚠ This is still the build-time spike (§6 phase 1). If it looks wrong, the fallback is sticks
drawn at a slightly larger radius *outside* the hull, never a second View3D.

### 4.3 What the driver adds

`Prepared` already holds the per-frame FK pose, `grip`, `shaftDir` and the scene transform. It
gains:
- `anchor[element][frame]` (`QVector3D`, scene frame) for the default targets plus `head`.
- `markerPos[frame][bone ends]` for the sticks, from the same FK the figure uses. The persisted
  `joint` array (mm-rounded) is **not** read, so annotations can never drift from the drawn body.
- `finishUs`, and `positions[]` = {p, t_us, source} from `analysis.club.positions`. Each P's
  shaft is evaluated from the fit at its `t_us` (slerp, as the club is). The `source == 1`
  (MilestoneFit) colour rule is kept (`colorGood`).
- `Q_INVOKABLE QVector3D positionHead(int i, int revision)` etc. for the label pins. These are
  small, typed and per-call.

The work is O(frames × elements) once per load, and it runs on the worker thread that already
parses the document.

## 5. Labels

"P4" and friends are QML `Text` items in the 2-D overlay layer, positioned by
`view.mapFrom3DScene(driver.positionHead(i, rev))` and re-evaluated with the revision and the
camera. They are hidden when the point is behind the camera (z < 0 from the mapping). There is
no 3-D text: it faces the wrong way from half the presets.

## 5a. Match the camera (Mark, §8 q3)

The fitted cameras are persisted in `skeleton3d.camera`. The face-on camera sits at the world
origin, looking along +Y, pitched by `pF` and rolled by `rF`. The DTL camera sits at `cD`, with yaw
`psiD`, pitch `pD` and roll `rD`. See `camFrame()` in `skeleton3d_fit.cpp`, which the driver mirrors
through its scene transform. Two more preset chips, **Face-on cam** and **DTL cam**, move the
view to a second `PerspectiveCamera` at that pose:
- The vertical field of view is `2·atan(H / 2f)`. The image height comes from the document:
  `club.frameHeight` for face-on and `poseDtl.height` for DTL. A chip is disabled when its view's
  height or camera is missing.
- The principal point is the image centre, as the fit assumes. The panel's aspect is not the
  video's, so the match is exact vertically and cropped or extended horizontally.
- Orbiting is off while matched, and any orbit preset returns to the orbit camera.
- A mirrored face-on source is un-mirrored in the fit. The matched view then shows the golfer
  as the camera saw them *before* mirroring, and the footnote says "mirrored" so a tile that shows
  the mirrored picture is not mistaken for a mismatch.

This is the direct, visible version of §7.2: the traces should land on the tile's traces to within
the reprojection figure.

## 5b. The fused downswing plane (Mark, §8 q5)

`club3d.planes.down` is the downswing shaft plane from the two-view fusion. It has `normal`,
`inclDeg` and `offered`, in the same world frame as `skeleton3d`. It is a plane of *directions*,
so it has no position of its own. The quad is placed through the fitted clubhead at impact:
- `e1` = the fitted impact shaft projected into the plane; `e2 = n × e1`.
- It spans from the head up the shaft to 1.8 m, and ±1.2 m along `e2`.
- It is drawn only when `offered`, in the accent at ~15 %, double-sided, with no depth write.
- A chip reads "downswing plane · club3d · <incl>°".

It is a new element, `plane`, with frame and off only (like the ball), and one new row in the Motion
panel's customise page. The tiles ignore it. It is on in the "Club track" preset and off in every
other. ⚠ The fitted club is not constrained to this plane: the 26 Sept re-grade has the fitted
downswing plane −3.0° median off club3d's, so the club can be seen leaving the quad by a few
degrees. That is the true disagreement, and it is not smoothed away.

## 6. Build order

| Phase | Deliverable | Done when |
|---|---|---|
| 1 | Driver anchors + `SwingAnnotationGeometry` trace mode; clubhead and lead-wrist traces wired to `ViewLayout`; the transparent-hull spike | Traces scrub exactly; spike answered, with pictures of a paused P4 in all five presets for Mark |
| 2 | frame + fan modes for all body elements and the shaft; ball obeys its setting | §7.1 passes |
| 3 | P-positions: dots, the ±40 ms shaft, labels | Labels track through an orbit and hide behind the camera |
| 4 | §7.2 reprojection grade on the 24 two-camera swings; the soak | §7.2 table judged by Mark; soak clean |

Builds: one app build per phase, plus the two affected test targets through ctest, once, at the
end of each phase.

## 7. Verification

### 7.1 Unit and probe
- `swing_annotation_geometry_test`: a synthetic track (the skeleton3d test generator) checks the
  following:
  - Trace vertex count vs playhead.
  - The trimmed end equals the anchor interpolated at `t`, to 1e-5 m.
  - Absent frames split the tube.
  - The fan holds ≤ 24 frames and its alpha ramp matches the tiles' constants.
  - The lead side follows `leadLeft`.
- `swing_rig_driver_test`: the anchors equal FK (+ marker offset) at every frame; the P-positions
  are read and window-relative.
- `--probe-qml tools/probes/swing3d_panel.qml` (offscreen, absolute path) checks the following:
  - Each preset from ViewLayout's catalogue instantiates the expected geometries with non-zero
    counts.
  - Motion off → none.
  - The "P4" label's text and visibility.

### 7.2 Does the 3-D trace say what the video says? (corpus)

The fitted cameras are persisted (`skeleton3d.camera`: `fF, pF, rF`; `cD, psiD, pD, rD, fD`).
Each 3-D anchor path can therefore be **projected back into each camera** and compared with the
tile's own 2-D trace of the same anchor. `tools/swinglab/swing3d_trace_reproject.py` reports this
per swing and element: median and p90 px, address → finish, face-on and DTL.

This is the number that says whether a coach can trust the 3-D trace to be the video's trace. The
fit's reprojection floor is ~7 px face-on / ~5 px DTL (§12.4 there), so for body anchors it will
read about that. For the clubhead it will be the new result, because the fit's shaft is
fused/modelled while the tile's head is the tracker's. A large clubhead residual through impact
means the 3-D arc is the fit's opinion, and the panel should say so (a tier chip on the trace)
rather than hide it. Judged per swing, by count; Mark judges the table before any commit.

### 7.3 Soak

The View3D disappearance watch still applies. The soak repeats §12.4's 30-minute scrub, preset and
reparent run with every element on trace, and then again on fan (the per-tick rebuild path).
Checked through the app log; Mac first, and GOLFSIMPC is owed as it is for the panel itself.

## 8. Questions for Mark — answered 27 Sept

1. **One setting for both (§3)**: is it right that the tiles and the 3-D panel always show the
   same annotations? The alternative is a separate 3-D motion state, which means a second pill
   and a second set of presets. Recommendation: shared. **Answer: shared.**
2. **Frame mode inside the body (§4.2)**: fade the hull to show the stick skeleton, or keep the
   hull solid and treat "frame" in 3-D as the figure itself (i.e. frame = no extra drawing)?
   Recommendation: fade, pending the spike. **Answer: fade (§4.2).**
3. **A "match the camera" preset**: the fitted cameras make it possible to put the 3-D camera
   exactly where the face-on or DTL camera stood. The 3-D annotations would then line up with the
   tile beside it, which is the most direct honesty check a user can make. It is not in scope as
   written. Add it? **Answer: yes (§5a).**
4. **Impact-camera arc (layer J)**: it stays on the impact tile. A 3-D low-point/attack-angle arc
   belongs with the attack-angle work (`low-point` thread), not here. Agreed?
5. **Things computed but drawn nowhere** (the face-on shaft plane `club.plane`, the fused
   downswing plane, head track, DTL posture lines): out of scope, since they are not tile
   annotations today. The fused plane quad is already listed as phase 5 of the panel's own
   design. Keep them out? **Answer: add the fused downswing plane (§5b); keep the rest out.**
   The impact arc (q4) stays on the impact tile.

## 9. Built and graded (27 September 2026)

### 9.1 What was built

| Piece | Where |
|---|---|
| The annotation track: 16 points per fitted frame (the lead/trail shoulder, elbow, wrist, hip, knee and ankle joint centres, the head, the grip and the club's two ends) in the scene frame, from the figure's own forward kinematics; the P-positions; the Address/Impact/Finish window; both fitted cameras; the plane quad | `SwingAnnotTrack`, `SwingRigDriver` (`src/Gui/swing3d/swing_rig_driver.{h,cpp}`) |
| The meshes: trace tubes, frame sticks, fan, head trail, plane quad. Pure arithmetic, testable without a scene | `src/Gui/swing3d/swing_annotation_mesh.{h,cpp}` |
| The Quick 3D upload, one per element | `SwingAnnotationGeometry` (`src/Gui/swing3d/swing_annotation_geometry.{h,cpp}`) |
| The view: 8 annotation models, the faded hull, P-position dots, ±40 ms shafts and labels, the plane chip, two match-camera chips | `SwingViz3DView.qml`; motion props through `SwingViz3DHost.qml`; bound in `ScreenWrist.qml` from `ViewLayout` exactly as `PpCameraTiles.qml` binds the tiles |
| The `plane` element: frame/off, "Club track" only, one Motion-panel row "Swing plane (3-D)" | `ViewLayout.qml`, `PpMotionPanel.qml` |
| Tests | `src/Gui/tests/swing_annotation_geometry_test.cpp` (new, 27 checks), `swing_rig_driver_test.cpp` (+19 checks; 34 in all); `tools/probes/swing3d_panel.qml` (§8 of its header, `--probe-annot`) |
| The corpus grade | `tools/swinglab/swing3d_trace_reproject.py` → `docs/research/data/skeleton3d/annot_reproject_20260927.{csv,md}` |

**Where the build departed from §2–§5:**
1. **The mesh is split from the geometry.** The design had one class. `swing_annotation_mesh` holds the
   arithmetic, so the tests build it without Quick 3D, and `SwingAnnotationGeometry` only uploads it.
2. **The anchors are joint centres (§2, as corrected before the build).** The trace-target anchors are
   the rig's joints, the head is the `Head` joint, and the club's ends are the drawn club's
   (`grip − 0.04·u` and that plus `clubLengthM·u`).
3. **club3d's normal is carried through the fitted face-on camera axes.** club3d's frame is the
   face-on camera's with the camera assumed level. The fit pitched and rolled that camera, so the
   normal is rotated by the fitted `pF`/`rF` before it enters the world (a few degrees).
4. **The hull fade works as §4.2 designed it (the spike passed).** The hull is at 35 % with a depth
   pre-pass, and the frame sticks are opaque and drawn first. The grabs at P4 (face-on, DTL, top,
   target side, behind, both match cameras) show the sticks through the body with no self-sorting.
5. **The image heights come from the document's streams** (`streams[].encoded`, the corpus grader's
   own lookup), with face-on falling back to `club.frameHeight`. On the 4 July swings the DTL stream
   is a 512×1024 crop, and the fit's pinhole reproduces the tile keypoints through it (§9.2
   self-check), so the crop is the frame the fit used.
6. **The ball now obeys the ball element's setting.** In the default "Clean" preset the ball is off,
   so the 3-D ball is hidden there, just as on the tiles.

### 9.2 Does the 3-D trace say what the video says? (§7.2)

The run used the 24 two-camera swings (`skeleton3d_run.sh full`, height 1.8288 m, the binary behind the 26 Sept
re-grade), graded by `swing3d_trace_reproject.py`, from Address to Finish. The table gives the median over swings of
each swing's median, with the per-swing p10–p90 in brackets, and in cm at the anchor's depth. Full
per-swing rows are in the CSV.

| Trace | Face-on | DTL | Reading |
|---|---|---|---|
| arms (lead wrist) | **0.7 cm** (0.4–0.8), 2.4 px | **1.6 cm** (0.5–4.0) | The 3-D trace is the video's trace |
| legs (lead ankle) | 4.2 cm | 2.4 cm | The ankle joint against the ankle keypoint |
| hips (pelvis mid) | 4.0 cm | 2.7 cm | Joint centres vs surface keypoints (§2) |
| spine (shoulder mid) | 4.5 cm | 6.1 cm | Joint centres vs surface keypoints |
| shoulders (lead) | 5.9 cm | 6.0 cm | Joint centres vs surface keypoints |
| head | 4.8 cm | 7.2 cm | Skull base vs the ear midpoint |
| **clubhead** | **12.4 cm** (9.8–19.6), p90 46 cm | **14.0 cm** (9.2–22.6), p90 39 cm | **Departs** |
| … address → top | 13.2 cm | 12.7 cm | |
| … top → impact | 11.5 cm | 16.5 cm | |
| … impact ± 60 ms | 9.7 cm | 11.4 cm | |
| grip | 7.9 cm | 6.4 cm | |

**Self-check.** The joint-centre keypoints (elbows, wrists, knees, ankles), reprojected by this tool,
sit at 4.8–9.7 px face-on and 3.6–7.6 px DTL across the 24 swings. The fit's own all-marker figure is
5.9–7.6 and 4.2–6.8, so the camera port, the image sizes and the time base are right.

**Reading.**
- The **body traces are the video's traces**, to the joint-centre offset. The lead wrist, which is
  the "Trace hands" anchor, is within a centimetre face-on. The shoulder, hip and head rows are the
  documented offset (§2), not error in the fit, and they are constant across swings (tight p10–p90).
- The **clubhead trace is the fit's opinion of the club**: it lies 10–16 cm from the tracker's head in
  every phase. A likely large part of that is the club's **fitted length**: median 0.853 m (4 July)
  and 0.871 m (11 June) against the 0.94 m record, ranging 0.73–1.00 m per swing (§12.2 item 7 of
  the panel design makes it a shared unknown with the record as a σ 8 cm prior). A club drawn ~9 cm
  short puts its head ~9 cm off wherever the shaft points. The grip, at 6–8 cm, says the club also sits
  a little off the hands. **Not fixed here.** It is the fit, not the annotation, and it is Mark's
  call whether the panel should draw the club at the record's length instead.

### 9.3 UI checks
- **Probe, offscreen**, 4 July swing 5. Every ViewLayout preset draws exactly its elements:
  - "Clean": the arms, spine, shoulders and hips sticks and the shaft's head trail, with the hull faded.
  - "Club track": the clubhead and grip traces plus the plane (60.6°).
  - All-fan: every element except the grip end, which has no fan of its own on the tiles either.

  Motion off gives 0 vertices. Both match cameras engage (`View3D.camera` is the match camera), and
  an orbit preset returns to the orbit camera. The P4 label reads "P4", but it cannot be placed
  offscreen (no render loop, so `mapFrom3DScene` answers 0).
- **Grabs, on screen**, paused at P4: the composite of traces, frame sticks and the plane in all 7
  presets; the fan face-on and DTL; the trace set at impact from DTL; and "Clean" face-on and from the
  face-on camera. In "Clean", the P-position dots sit at each position's clubhead and the "P4" label
  is on the clubhead. Seen from down the line, the
  plane is close to edge-on, which is what a correct swing plane looks like from behind the hands.
- **Soak**: see §9.4.

### 9.4 Soak
The soak ran 30 minutes on screen on the Mac, 27 Sept (`tools/probes/swing3d_panel.qml --probe-annot --probe-soak
30 --probe-grab`, 4 July swing 5):
- The playhead was scrubbed at 30 Hz (54 000 steps), the preset changed every 3 s, and the view moved
  to a new slot every 10 s.
- Every element was on **trace** for the first 15 minutes and on **fan** for the second, so each step
  rebuilt every mesh.
- The one view survived throughout (same object, available).
- The app log has no error line.
- All eight grabs rendered the rig, and none was blank. The fan grab at the end shows every element's
  fan; the 10-minute grab falls after Finish and correctly draws nothing.

### 9.5 Owed, and open
- **The clubhead gap (§9.2).** Mark's call: draw the club at the club record's length, or leave it as
  the fit's length and say so.
- **GOLFSIMPC soak**, as for the panel itself.

## 10. Mark's review, and the second pass (28 September 2026)

Mark on §9: "The trace matches the video, the 3-D swing is a useful diagnostic tool — the club trace is
messy and highlights how far away we are." He asked for three things: stabilise the club, whose 3-D
path is implausibly jittery while the face-on view is stable; render the traces as 3-D objects; and
make the figure transparent, up to 50 %, so the traces inside and behind it can be seen.

### 10.1 The club, stabilised for display
**What the jitter is.** The fitted clubhead has 1.8–2.8 cm of high-frequency noise, split about
evenly between the face-on image plane and depth. The face-on *tracker's* head is smooth, and the
fit's is not. Out of the shaft's own plane it wobbles ~5° rms (7 cm at the head, peaks near 20°),
on a ~60 ms scale.

**Three stabilisers were tried and rejected, each measured against both trackers** (15 library swings):
- **The face-on tracker's direction plus the fit's smoothed depth.** The face-on view matches by
  construction, but the club then stretches 0.73–1.13 m.
- **Smoothing only the out-of-plane angle** against one shaft plane. It pulls the club *off* the DTL
  video: 54 → 67 px at 60 ms, 83 px at 90 ms. The plane changes too much through the top for one
  plane to hold.
- **A speed-adaptive width.** No gain.

**Built: the fitted clubhead path, smoothed over 20 ms** (zero-phase, local quadratic, Gaussian
weights; unseen frames left out of every fit). The butt stays in the fitted hands, and the club is
re-pointed at the smoothed head at the fitted length, so the club model, its trace and the P-positions
stay one object. The length is deliberately not corrected: that is the diagnosis (§9.2). A wider
width begins to cost against the DTL video (60 px at 30 ms). The pure smoother is
`swing3d::smoothPath` (`swing_annotation_mesh.h`), applied in `SwingRigDriver::prepare`
(`kClubSmoothUs`). The tier chip says "club path smoothed 20 ms" whenever a club trace is on.

| 24 two-camera swings (`annot_reproject_20260927.md`) | raw | displayed |
|---|---|---|
| Clubhead jitter (rms residual of a local cubic over ±5 frames), median (p10–p90) | 2.69 cm (2.07–3.64) | **1.56 cm** (1.18–2.53) |
| Clubhead vs the face-on tracker, median of medians | 12.4 cm | 12.3 cm |
| Clubhead vs the DTL tracker | 14.0 cm | 14.3 cm |

The jitter falls 42 % at no cost against either video. Synthetic (`swing_annotation_geometry_test`):
a clean 35 m/s arc moves 5 mm rms (the quadratic's lag on a circle), and 2 cm of injected jitter comes
out at 0.95 cm.

### 10.2 Traces as 3-D objects
The tubes are **lit**. The mesh carries normals (40-byte vertices: position, normal, colour), and the
annotation material uses fragment lighting; the plane stays flat. The winding faces outward and the
insides are culled; the first build wound them inward, and the lit inside faces showed as dark specks
along every tube. The trace radius is 9 mm (from 7), so the shading reads.

### 10.3 The figure, see-through
Whenever any body or club element is on, the hull drops to **50 %** (`hullFadeOpacity`) and **writes
no depth**. Nothing it covers is hidden: the pelvis trace inside the hips and the club going round the
back are seen through it. Back faces stay culled, so the body does not show its own far side. This
replaces §4.2's depth pre-pass at 35 %, which hid anything inside the body that was not an opaque stick.
Frame-mode sticks are still opaque and drawn first.

**Checked:** both tests through ctest; the probe (every preset draws its elements, and the figure fades
whenever a body or club element is on); on-screen grabs at P4 in all seven presets, and the full trace
set at impact from DTL (the pelvis loop visible inside the hips).

## 11. The 3-D swing ends at P8; no fused plane ⇒ face-on only (28 September 2026)

Mark, after the DTL grabs showed the club veering off plane in the follow-through: "we are really
messy after P8 and it's not a priority right now since it doesn't reveal anything interesting… limit
the 3-D swing to stop at P8. Swings with no DTL, and therefore no fused shaft plane, should be limited
to a face-on 3-D swing view." The veer itself is root-caused in
`skeleton3d_shaft_branch_design.md`: the fit takes the wrong face-on depth mirror of the lead arm
wherever the DTL is blind. That fix is **deferred**.

**The end at P8** (`SwingRigDriver`):
- The display ends at P8 (`Phase::ShaftParallelThrough`). With no P8 on file it ends at
  impact + 100 ms; P8 sits ~95 ms after impact on the 4 July swings.
- Frames past the end are dropped, except the first one beyond it, so the pose *at* P8 interpolates
  exactly.
- Past P8 the figure holds its P8 pose and a chip says "held at P8". The traces and fans end at P8,
  and P-positions after it (P9, P10) are not offered.
- `endUs` is now the display's end.

**Face-on only** (`SwingRigDriver::faceOnOnly`): no DTL, or no offered fused downswing plane. Depth is
then the fit's guess, so:
- the panel offers only Face-on (and the face-on camera's own view where it is on file);
- orbiting is off;
- any other preset falls back to Face-on;
- a chip says "face-on only: no fused shaft plane", and the plane chip is hidden.

**The chips** are now one per note (tier, face-on only, held at P8, club smoothing, plane) in a
row that wraps upward; the old single chip ran off a narrow panel. The footnotes moved under the
preset bar.

**Checked:**
- `swing_rig_driver_test`, through ctest:
  - the display ends at P8, and past it the figure is held in its P8 pose;
  - the annotations stop at P8, and a P9 is not offered;
  - a two-view swing without a fused plane is face-on only.
- The probe:
  - swing 5 spans address − 150 ms → P8 (3.587 s), with 8 P-positions (was 9);
  - on a scratch copy with the plane withdrawn, every orbit preset resolves to Face-on and the DTL
    camera is refused.
- On-screen grabs of both.
