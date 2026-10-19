# Cameras

*Directory:* `src/Gui/cameras/`  
*8 components · 4,402 lines of QML*

Camera frames/tiles, the toolbar camera panel and the analyse-view telestrator (annotation tool, layer, toolbar, icons).

**C++ in this area:** `BufferController`, `CameraInstance`, `CameraManager`, `CameraRoleProbe`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [AnnotationTool](#annotationtool) | 81 | `QtObject` | 3 | 1 | singleton |
| [PpAnnotationIcon](#ppannotationicon) | 95 | `Canvas` | 2 | 0 |  |
| [PpAnnotationLayer](#ppannotationlayer) | 344 | `Item` | 1 | 1 |  |
| [PpAnnotationToolbar](#ppannotationtoolbar) | 174 | `Item` | 1 | 5 |  |
| [PpCameraFrame](#ppcameraframe) | 2133 | `Item` | 6 | 10 | 1 local comp. |
| [PpCameraPanel](#ppcamerapanel) | 563 | `Item` | 1 | 7 | 3 local comp. |
| [PpCameraTiles](#ppcameratiles) | 778 | `Item` | 2 | 10 | 1 local comp. |
| [VideoPage](#videopage) | 234 | `Item` | 1 | 1 |  |

### AnnotationTool
`src/Gui/cameras/AnnotationTool.qml` · 81 lines · root `QtObject` · **singleton**

> Shared state for the analyse-view telestrator (draw circles / lines / hollow
> squares over replay video). ONE toolbar drives the active tool + colour here;
> each PpAnnotationLayer (one per camera tile) reads them and owns its own marks.
> Marks are EPHEMERAL — held only in this in-memory store, keyed per swing+tile,
> and wiped wholesale the moment the focused swing changes (new swing OR deselect).
> That is the "cleared when a swing is deselected" contract; nothing is persisted
> to disk. The store survives tile churn WITHIN a swing (Replay↔Analyse toggles
> recreate the layers) because it lives above the layers, here.

- **Properties:** `string tool`, `color strokeColor`, `bool paletteOpen`, `QtObject selectionOwner`, `readonly bool hasSelection`
- **Signals:** `selectionClaimed(var owner)`, `cleared()`
- **Functions:** `marksFor(key)`, `claimSelection(owner)`, `releaseSelection(owner)`, `deleteSelected()`, `recolorSelected(c)`, `clearAll()`
- **Uses:** `SessionMode`
- **Used by (3):** `PpAnnotationLayer`, `PpAnnotationToolbar`, `PpCameraTiles`

### PpAnnotationIcon
`src/Gui/cameras/PpAnnotationIcon.qml` · 95 lines · root `Canvas`

> Tiny vector glyphs for the telestrator palette, drawn on a Canvas so they read
> identically on every platform (the symbol FONT is a Linux fallback and can't be
> trusted for shape icons). kind: select | line | circle | rect | pencil | chevronLeft.

- **Properties:** `string kind`, `color iconColor`
- **Used by (2):** `PpAnnotationToolbar`, `PpCameraTiles`

### PpAnnotationLayer
`src/Gui/cameras/PpAnnotationLayer.qml` · 344 lines · root `Item` · 2 hard-coded colour(s)

> Per-tile telestrator overlay — draws/edits the focused swing's annotations on
> ONE camera tile. Hosted by PpCameraFrame (gated on annotationsEnabled), it sits
> above the pose/club replay overlay. Marks are held in the AnnotationTool store
> (keyed per swing+tile), so they survive Replay↔Analyse churn and clear on swing
> deselect; this layer is a thin imperative view over that array.
> Coordinates are stored NORMALIZED [0..1] in the video's source space and mapped
> to screen via the parent's contentRect each paint — so marks stay glued to the
> golfer through resize, letterboxing and aspect changes (same model as the pose
> skeleton and the ball ROI). Render + hit-testing follow PpCameraFrame's existing
> Canvas-paint + MouseArea idiom rather than a per-shape Item tree.

- **Properties:** `bool active`, `rect contentRect`, `string tileKey`, `var shapes`, `int selectedIndex`
- **Functions:** `deleteSelectedShape()`, `recolorSelectedShape(c)`
- **Uses:** `AnnotationTool`
- **Used by (1):** `PpCameraFrame`

### PpAnnotationToolbar
`src/Gui/cameras/PpAnnotationToolbar.qml` · 174 lines · root `Item`

> The one telestrator palette — the controls only (active tool + ink colour +
> delete/clear), driving the shared AnnotationTool state every PpAnnotationLayer
> reads. The host floats it CENTRED over the camera tiles so its position reads
> as "applies to all cameras"; the open/collapse toggle lives separately in the
> LHS gutter. Content-sized; it floats over the footage, so it wears the popover
> card's shell (stronger hairline, quiet rule) like every other floating panel.

- **Uses:** `AnnotationTool`, `PpAnnotationIcon`, `PpButton`, `PpPopoverCard`, `PpPressable`
- **Used by (1):** `PpCameraTiles`

### PpCameraFrame
`src/Gui/cameras/PpCameraFrame.qml` · 2133 lines · root `Item` · 17 hard-coded colour(s)

> Reusable camera video frame — the one video view used by every rail screen
> (Wrist, swing analysis, the Play capture tab, …). Just the aspect-locked
> frame and its in-frame overlays; NO surrounding controls. Which overlays
> are active is configured per screen via the show* flags below — the same
> frame shows skeleton-only on the Wrist screen and the full hitting-area +
> ball chrome on capture screens.
> instance is nullable: a tile can be shown for a session-enabled camera
> before it is connected (dim "Not connected" placeholder) and subscribes to
> the instance's frame feed as soon as the instance arrives.

- **Properties:** `QtObject instance`, `int replayStreamIndex`, `real viewGain`, `bool showLevels`, `string displayName`, `bool showPoseOverlay`, `bool showHittingArea`, `bool showHittingAreaHint`, `bool showBallOverlay`, `bool roiEditable`, `bool showPerspectiveBadge`, `bool showStatsOverlay`, `bool showReplayOverlay`, `bool showImpactOverlay` … (+20 more)
- **Functions:** `paintBlueprint(ctx, cr, gx, gy, gs, alphaScale, emOn, maxJoint)`
- **Local components:** `FeedState`
- **Uses:** `PpAnnotationLayer`, `PpBadge`, `PpCardNote`, `PpChip`, `PpMicro`, `PpTrustInlineFor`, `PpTypeset`, `SessionMode`, `TrustFocus`, `ViewLayout`
- **Used by (6):** `BallPage`, `CamerasPanel`, `FramingPage`, `PpCameraTiles`, `SetupCheckRow`, `VideoPage`

### PpCameraPanel
`src/Gui/cameras/PpCameraPanel.qml` · 563 lines · root `Item` · 2 hard-coded colour(s)

> Camera device panel for the session toolbar. Same shell + mode pattern as the
> IMU panel:
> "list"      — Scan / Connect / Calibrate actions, an all-cameras "Live pose"
> toggle, and the camera rows with a per-camera enable toggle
> (session-local, owned by CameraManager so the per-screen video
> tiles share it). Connect starts the full capture pipeline on
> every enabled camera — the screens' video tiles stream from it.
> Calibrate is attention-framed once a camera is connected that is
> not "fixed in place" (appSettings.cameraFixedInPlace) — i.e.
> needs stereo cal.
> "calibrate" — hosts CameraCalibrationFlow (a STUB for now) compactly in-panel.
> The heading turns colorAttention while calibrating; the toolbar turns the
> popover card's rule the same tone, so the whole card reads as work in hand.
> Each row leads with the device vocabulary's badge and says its state in words.

- **Properties:** `string mode`, `readonly bool needsCalibration`, `readonly bool havePpcp`, `readonly bool allConnected`
- **Functions:** `torchFor(peerId, rev)`, `torchOwnerRow(index)`, `startConnect()`, `disconnectAll()`
- **Local components:** `ScopedAction`, `TogglePill`, `CamRow`
- **Uses:** `CameraCalibrationFlow`, `PpBadge`, `PpCardNote`, `PpChip`, `PpConnectingFrame`, `PpMicro`, `PpPressable`
- **Used by (1):** `PpSessionToolbar`

### PpCameraTiles
`src/Gui/cameras/PpCameraTiles.qml` · 778 lines · root `Item`

> Camera stage-panel — its video source follows the session DATA SOURCE, not the
> mode. Capture is the only live-video mode: it shows one live tile per session-
> enabled camera (plus the degraded-shot ¼× auto-replay metric graph). Review AND
> Analyse both show the LOADED shot's video — one tile per stream of that swing,
> enumerated from the swing's own swing.json (shotReplay.streams), never the local
> camera rig, so a swing recorded on a different setup still plays and the playback
> follows the Review↔Analyse toggle. Hosted by PpModeStage as the "camera" panel.
> The stage's CAMERA card frames it and pads it, so the tiles run to the card body's edges and
> draw no chrome of their own: the video is the mass, and what floats over it (the impact
> picture-in-picture, the telestrator) is quiet card-family chrome.

- **Properties:** `int sessionType`, `bool showHittingArea`, `readonly string cardAside`
- **Local components:** `PipBtn`
- **Uses:** `AnnotationTool`, `PpAnnotationIcon`, `PpAnnotationToolbar`, `PpCameraFrame`, `PpCardNote`, `PpMetricChart`, `PpMicro`, `PpPressable`, `SessionMode`, `ViewLayout`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### VideoPage
`src/Gui/cameras/VideoPage.qml` · 234 lines · root `Item` · 3 hard-coded colour(s)

Camera video page: camera selector (one toggle chip per discovered camera), start/stop recording, overrun warning, and a PpCameraFrame per selected camera. Hosted by PlayPage (the 'Pose' tab).  
*(description written for this catalogue — the file has no header comment)*

- **Uses:** `PpCameraFrame`
- **Used by (1):** `PlayPage`
