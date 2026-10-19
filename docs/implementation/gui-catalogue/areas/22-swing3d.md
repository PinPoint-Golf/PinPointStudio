# Swing 3-D

*Directory:* `src/Gui/swing3d/`  
*2 components · 699 lines of QML*

The skeleton3d swing visualisation view and its keep-alive host.

**C++ in this area:** `SwingAnnotationGeometry`, `SwingHullGeometry`, `SwingRigDriver`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [SwingViz3DHost](#swingviz3dhost) | 73 | `Item` | 1 | 1 |  |
| [SwingViz3DView](#swingviz3dview) | 626 | `Item` | 1 | 7 | 3 local comp. |

### SwingViz3DHost
`src/Gui/swing3d/SwingViz3DHost.qml` · 73 lines · root `Item` · no direct `Theme.*` refs

> SwingViz3DHost — keeps ONE SwingViz3DView alive for a screen and lends it to whichever panel
> slot is showing (docs/design/swing_3d_viz_design.md §6).
> ⚠ WHY A HOST. PpModeStage rebuilds its delegates on every mode, tab and arrangement switch,
> and destroying Quick 3D View3D instances has corrupted the render thread's heap in this app
> before (CapturePage.qml keeps its three views alive for exactly that reason). So the view is
> created the first time a slot asks for it — never before, so a user who never opens the panel
> pays nothing — and is then only REPARENTED: into the slot on attach, back here on detach.
> The Loader is the view's QObject owner, so a slot's destruction never takes the view with it.

- **Properties:** `string swingDir`, `real positionUs`, `bool motionOn`, `var motionModes`, `string motionTraceTarget`, `bool leadIsLeft`, `Item slot`, `readonly Item view`
- **Functions:** `attach(s)`, `detach(s)`
- **Uses:** `SwingViz3DView`
- **Used by (1):** `ScreenWrist`
- **Exercised by tests/tools:** `swing3d_panel`

### SwingViz3DView
`src/Gui/swing3d/SwingViz3DView.qml` · 626 lines · root `Item` · 6 hard-coded colour(s)

> SwingViz3DView — the swing as the fitted skeleton, the club and the ball, from any side
> (docs/design/swing_3d_viz_design.md §6). SwingRigDriver does the arithmetic; this file only
> places what it answers. The figure is a smooth, simplified MANNEQUIN on the Y-bot's skeleton
> (qrc:/assets/swing3d/man_*.glb, tools/generate_swing3d_mannequin.py) — the Y-bot's own shells
> read as a robot at swing speed. NOTHING here is shared with the calibration views.
> Scene frame (the driver's): metres, +Y up, the floor at y = 0, the ball at address at the
> origin, +X along the stance toward the lead heel, +Z toward the face-on camera's side.
> Presets orbit the golfer's mid-hip at address; the two "cam" presets stand where the fitted
> face-on / DTL camera stood (docs/design/swing_3d_annotations_design.md §5a).
> MOTION ANNOTATIONS (swing_3d_annotations_design.md): the camera tiles' Motion setting, drawn in
> 3-D — the same ViewLayout object the tiles read, passed in by the host. Each element is one
> SwingAnnotationGeometry (trace tube, frame sticks, fan); the P-positions ride the club; the fused
> downswing plane is the "plane" element (3-D only — the tiles ignore it).
> ⚠ Lives inside SwingViz3DHost, which is created once and REPARENTED between panel slots —
> never destroyed. Destroying View3D instances corrupted the Quick 3D render thread before
> (CapturePage.qml), and the session stage rebuilds its delegates on every mode/tab switch.

- **Properties:** `string swingDir`, `real positionUs`, `string preset`, `bool motionOn`, `var motionModes`, `string motionTraceTarget`, `bool leadIsLeft`, `readonly real hullFadeOpacity`, `readonly bool hullFaded`, `readonly bool clubTraced`, `readonly int matchView`, `readonly bool matched`, `readonly var annotations`, `readonly bool usingMatchCamera` … (+10 more)
- **Functions:** `elemMode(key)`, `presetAllowed(p)`, `mapPosition(i)`, `presetRotation(p)`, `applyPreset(p)`, `mapToView(p, tick, camNow)`
- **Local components:** `Bone`, `Annot`, `Chip`
- **Uses:** `PpCardNote`, `PpChip`, `PpMicro`, `PpSegmentedControl`, `PpTrustInlineFor`, `PpTypeset`, `TrustFocus`
- **Used by (1):** `SwingViz3DHost`
