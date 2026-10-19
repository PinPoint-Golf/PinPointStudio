# Session screens

*Directory:* `src/Gui/session/`  
*14 components · 7,906 lines of QML*

The live/replay/analyse session screens and their furniture: persistent toolbar, stage arranger (PpModeStage), toolbar panels (club, view, motion, launch monitor, markup), session drawer; plus the SessionMode and ViewLayout singletons.

**C++ in this area:** `SessionController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpClubPanel](#ppclubpanel) | 171 | `Item` | 1 | 4 |  |
| [PpLaunchMonitorPanel](#pplaunchmonitorpanel) | 176 | `Item` | 2 | 3 |  |
| [PpLmGraphicsBody](#pplmgraphicsbody) | 2653 | `Item` | 1 | 8 | 28 local comp. |
| [PpMarkupPanel](#ppmarkuppanel) | 1135 | `Item` | 2 | 6 | 2 local comp. |
| [PpModeStage](#ppmodestage) | 497 | `Item` | 2 | 6 | 2 local comp. |
| [PpMotionPanel](#ppmotionpanel) | 534 | `Item` | 1 | 5 | 4 local comp. |
| [PpSessionDrawer](#ppsessiondrawer) | 405 | `Item` | 1 | 4 | 1 local comp. |
| [PpSessionToolbar](#ppsessiontoolbar) | 882 | `Item` | 2 | 15 | 1 local comp. |
| [PpStagePanel](#ppstagepanel) | 31 | `Item` | 1 | 1 |  |
| [PpViewPanel](#ppviewpanel) | 337 | `Item` | 1 | 5 | 3 local comp. |
| [ScreenSessionMode](#screensessionmode) | 202 | `Item` | 0 | 15 | ⚠ unreferenced |
| [ScreenWrist](#screenwrist) | 239 | `Item` | 1 | 15 |  |
| [SessionMode](#sessionmode) | 157 | `QtObject` | 24 | 0 | singleton |
| [ViewLayout](#viewlayout) | 487 | `QtObject` | 11 | 1 | singleton |

### PpClubPanel
`src/Gui/session/PpClubPanel.qml` · 171 lines · root `Item` · 1 hard-coded colour(s)

> Toolbar Club selector panel. Lists the CURRENT athlete's bag (canonical ids,
> vocabulary order) and writes the pick to SessionController.activeClub — the one
> source of truth read at shot-join time (shot_processor.cpp). The active club is
> seeded upstream (Home pick → wizard → activeClub, else the athlete's default at
> start()); this panel just surfaces and overrides it. Clubs whose record carries
> retro-band tape (non-empty bandCentersMm) show a tape marker, so a taped/
> instrumented club is visually distinct from a passive one.

- **Properties:** `readonly string currentClub`, `readonly var clubModel`
- **Signals:** `requestClose()`
- **Uses:** `ClubFormat`, `PpCardNote`, `PpMicro`, `PpPressable`
- **Used by (1):** `PpSessionToolbar`

### PpLaunchMonitorPanel
`src/Gui/session/PpLaunchMonitorPanel.qml` · 176 lines · root `Item` · no direct `Theme.*` refs

> THE STAGE FRAMES IT. PpModeStage puts the panel in its LAUNCH MONITOR card, so the panel draws no
> surface, border or title of its own.

- **Properties:** `string title`, `readonly var sessionShots`, `readonly bool reviewing`, `alias tab`, `alias interactive`, `alias medium`, `alias surface`, `alias printOrientation`, `alias printPage`, `readonly int printPageCount`, `readonly Item graphicsBody`, `readonly var shotList`, `readonly var bands`
- **Uses:** `PpCardNote`, `PpLmGraphicsBody`, `SessionMode`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`
- **Exercised by tests/tools:** `LmPanelHost`

### PpLmGraphicsBody
`src/Gui/session/PpLmGraphicsBody.qml` · 2653 lines · root `Item` · 5 hard-coded colour(s)

> THE LAUNCH MONITOR PANEL'S BODY — Foresight FSX's graphic grammar set in the typeset themes
> (docs/design/lm_panel_fsx.md; the signed-off spec is round 4c of mock A,
> build/run-me/lm-variants/qml/Panel_A_fsxpro.qml). GC Quad owners read FSX fluently: a tile with a big
> value and an AVG ± SD footer, dispersion ellipses on carry arcs, an annotated club view. Inside these
> figures FSX's conventions take precedence over §15's no-boxes and colour-only-for-out rules; themes,
> print, em size classes and fold-don't-drop still follow §15.
> Two quiet tabs. THIS SHOT: the glance tiles, then four EQUAL club figures (Path, Loft, Spin, Impact)
> in FSX's 2×2 (image 03: Path and Spin on top, Loft and Impact below; a row or a column runs Path, Loft,
> Spin, Impact), then the true-scale flight. The flight shrinks or strips first; the figures never fold
> before it. SESSION: a ball half (dispersion with enclosing ellipses on carry arcs, launch × spin, ball
> tiles) and a club half (impact map, path × face, club tiles), every tile with a strip of every shot.
> Directions are WORDS, never signs; a missing value is "-".
> THE NUMBERS COME FROM C++. This file positions and paints. Everything it draws is LmSessionModel's
> `graphics` map (g.values, g.shots, g.clubs, g.mismatch, g.device, g.leftHanded, g.flight …) or the
> board's bands, and every picture is LmClubArt's (heads, anchors, strike origin, ball, impact patch).
> No ellipse, club vote, club override or anchor table is computed here. A field the model does not
> …

- **Properties:** `var g`, `var bands`, `var shotList`, `string scopeText`, `string emptyText`, `string liveDeviceName`, `bool reviewing`, `int focusedShotId`, `bool interactive`, `string tab`, `string medium`, `string surface`, `string printOrientation`, `int printPage` … (+108 more)
- **Signals:** `shotRequested(int shotId)`, `excludeRequested(int shotId, bool excluded)`, `clubRequested(int shotId, string club)`, `undoMismatchRequested()`
- **Functions:** `pinCard(id, src, pt)`, `unpinCard()`, `setHover(id, src, pt)`, `leaveHover(src)`, `focusOn(id)`, `toggleExclude(id)`, `setClub(id, club)`, `runAction(a)`, `gateOf(key)`, `isList(x)` (+91)
- **Local components:** `FigHead`, `Tile`, `TileBlock`, `ClubKey`, `ClubSummary`, `Anno`, `ClubCell`, `ClubQuad`, `ShotFigures`, `HalfHead`, `BallHalf`, `ClubHalf`, `ScatterFig`, `FlightFigure`, `LedgerBlock`, `LedgerHead`, `LedgerRow`, `ShotTable`, `HeadImage`, `FacePath`, `FaceInk`, `BallImage`, `ShotPicker`, `StripTicks`, `Chip`, `ShotControls`, `NotesBar`, `HoverCard`
- **Uses:** `PpFolio`, `PpNotes`, `PpRunningHead`, `PpTabs`, `PpTrustInlineFor`, `PpTypeset`, `SessionMode`, `TrustFocus`
- **Used by (1):** `PpLaunchMonitorPanel`
- **Exercised by tests/tools:** `tst_lm_graphics`

### PpMarkupPanel
`src/Gui/session/PpMarkupPanel.qml` · 1135 lines · root `Item`

> Markup — a view-menu stage panel for in-app ground-truth labelling of the
> currently-focused swing (load → edit → save). Scrub frame-accurately, click
> grip then head to lay the club, press a P key (1–9, 0=P10) to tag a position;
> existing truth.json is loaded for editing and re-saved in place.
> TWO CAMERAS, SIDE BY SIDE. When the swing has a down-the-line stream the panel
> shows both frames at once, at the same displayed height and each at its own
> aspect, scrubbing together off ONE playhead. Clicks mark the pane they land in —
> face-on → truth.json, DTL → truth_dtl.json, each in its own pixel space — and the
> pane last clicked wears an accent border, because that is where a keyboard mark,
> an undo or a ball placement will go. The P-positions and the capture conditions
> are instants/properties of the SWING, so they hang off the shared playhead and
> stay in truth.json exactly as before.
> Self-contained: it decodes its own exact frames via MarkupController
> (cv::VideoCapture → image://markup), independent of the replay stage.
> MarkupController is a singleton, so only the ACTIVE host screen's panel drives
> it — `panelActive` gates loadSwing so hidden-screen copies stay inert.
> …

- **Properties:** `int sessionType`, `string targetSwingDir`, `bool panelActive`, `bool pendingGrip`, `real gripNx`, `real gripNy`, `bool ballMode`, `int pendingPane`, `readonly var pDefs`, `readonly var cocoEdges`, `readonly real kpMinConf`, `int rhsTab`, `readonly var lightingOpts`, `readonly var lightingVals` … (+9 more)
- **Functions:** `markEventByKey(k)`, `pComplete()`, `repaintPanes()`, `paneClick(pane, nx, ny)`
- **Local components:** `MlPane`, `MlButton`
- **Uses:** `ClubFormat`, `PpCardNote`, `PpComboBox`, `PpMicro`, `PpPopoverCard`, `PpSegmentedControl`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### PpModeStage
`src/Gui/session/PpModeStage.qml` · 497 lines · root `Item`

> Centre-stage arranger. Shows the enabled, host-wired stage panels (camera/charts/
> table and kin) packed per ViewLayout.arrangementFor(SessionMode.mode): tabs | split |
> stage. A panel a screen does not provide a delegate for is omitted (see `active`).
> Each arrangement's Loaders are gated on the active `arrangement` (not just the
> container's visible:), so only the selected layout instantiates its panels —
> a hidden arrangement must not spin up a second PpCameraTiles (and its camera
> frame subscriptions) behind the visible one.
> THE STAGE OWNS THE FRAME. Every panel it shows sits in a PpStageCard titled and toned from
> _defs, so the panels draw no background, border or title of their own. A panel may say one
> more thing on the card's heading row through `readonly property string cardAside`; a panel
> also used outside the stage carries `property bool framed` and is told here that it is
> framed already. In tabs the tab strip IS the card's heading, and the card takes the
> selected panel's tone.

- **Properties:** `Component cameraDelegate`, `Component chartsDelegate`, `Component sessionDiagnosticsDelegate`, `Component launchMonitorDelegate`, `Component tableDelegate`, `Component trustDelegate`, `Component markupDelegate`, `Component swing3dDelegate`, `readonly string arrangement`, `readonly var active`, `int tabIndex`
- **Local components:** `PanelCard`, `Separator`
- **Uses:** `PpCardNote`, `PpMicro`, `PpStageCard`, `PpStagePanel`, `SessionMode`, `ViewLayout`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`
- **Exercised by tests/tools:** `plumb_bob_chart`

### PpMotionPanel
`src/Gui/session/PpMotionPanel.qml` · 534 lines · root `Item` · 2 hard-coded colour(s)

> Motion panel for the session toolbar's Motion pill — replaces the old single
> "Pose overlay" toggle (formerly PpViewPanel's OVERLAYS section) with the full
> ViewLayout motion model: a master on/off switch, a named-preset picker, and a
> per-element (arms/spine/shoulders/hips/legs/shaft/ball) off|frame|fan|trace
> customiser. Edits the CURRENT session mode's motion layout only — same
> "this mode's saved layout" contract as PpViewPanel.
> Two pages that slide horizontally (PRESET is the default landing page; the
> slide honours Theme.reduceMotion via Theme.durationFast, which is already
> zeroed when reduceMotion is set):
> Page 1 — Presets: master toggle + preset list + "Customise…" disclosure.
> Page 2 — Customise: back chevron + per-element segmented controls, gated
> on data availability for the loaded swing (shotReplay.analysisDetail).
> Capture is heavily guarded: only the "Ball only" preset row is offered and
> Customise is unreachable (fan/trace are meaningless without a completed
> swing — ViewLayout.motionFor() itself hard-forces every non-ball element off
> in Capture regardless of stored state).

- **Properties:** `int page`
- **Local components:** `PresetRow`, `ElementRow`, `ElementSegmented`, `MiniToggle`
- **Uses:** `PpDivider`, `PpMicro`, `PpPressable`, `SessionMode`, `ViewLayout`
- **Used by (1):** `PpSessionToolbar`

### PpSessionDrawer
`src/Gui/session/PpSessionDrawer.qml` · 405 lines · root `Item`

> Session chooser drawer — the body of the "CHOOSE A SESSION" popup raised from
> the carousel's session chip. Lists every session on disk for the current
> athlete plus the synthesized live session (pinned first, flagged isLive), via
> sessionReviewController.sessionsModel. Selecting a row enters review
> (loadSession) or returns to live (resumeLive); the host popup closes on the
> emitted closeRequested(). Hosted as a Popup contentItem, so it reports its
> natural size through implicitHeight and the popup clamps it below the toolbar.

- **Signals:** `closeRequested()`, `sessionSelected()`, `exportRequested(string sessionId)`, `trashRequested(string sessionId)`
- **Local components:** `Stat`
- **Uses:** `PpCardNote`, `PpMicro`, `PpPopoverCard`, `PpPressable`
- **Used by (1):** `PpShotCarousel`

### PpSessionToolbar
`src/Gui/session/PpSessionToolbar.qml` · 882 lines · root `Item` · 1 hard-coded colour(s)

> Persistent session toolbar (Command-Bar direction): session clock + the one
> session-global Capture verb + two device pills, each marked with the device
> vocabulary's badge and saying its state in words. Each pill opens a Popup
> hosting the relevant device panel on a popover card. Reusable across all four
> mode screens (Wrist first). Calibration is handled entirely INSIDE the panels —
> the toolbar no longer routes a calibrate request anywhere. The bar itself stays
> flat: it is chrome, not a card.

- **Properties:** `int sessionType`, `readonly var modeNames`, `readonly bool captureLive`, `readonly int camTotal`, `readonly int camConnected`, `readonly int camEnabled`, `readonly bool camNeedsAttention`, `readonly int imuTotal`, `readonly int imuConnected`, `readonly int imuEnabled`, `readonly bool imuFailed`, `readonly bool imuNeedsAttention`, `readonly int imuLowestBattery`, `readonly bool imuBatteryLow` … (+15 more)
- **Local components:** `DevicePill`
- **Uses:** `ClubFormat`, `PpBadge`, `PpCameraPanel`, `PpClubPanel`, `PpDivider`, `PpImuPanel`, `PpMicro`, `PpMotionPanel`, `PpPopoverCard`, `PpPressable`, `PpSegmentedControl`, `PpToolPill`, `PpViewPanel`, `SessionMode`, `ViewLayout`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### PpStagePanel
`src/Gui/session/PpStagePanel.qml` · 31 lines · root `Item`

> Placeholder for a stage panel whose producer hasn't landed yet. The stage's card carries its
> title and frame; this only says so, in the card's one quiet line.

- **Uses:** `PpCardNote`
- **Used by (1):** `PpModeStage`

### PpViewPanel
`src/Gui/session/PpViewPanel.qml` · 337 lines · root `Item` · 2 hard-coded colour(s)

> View panel for the session toolbar. Two sections: an ARRANGEMENT schematic
> group (tabs/split/stage) and a PANELS toggle grid. Both edit the CURRENT
> session mode's layout via ViewLayout, persisted per SessionMode.mode.

- **Properties:** `readonly var panelMeta`
- **Local components:** `ArrangeCard`, `OrientCard`, `MiniToggle`
- **Uses:** `PpDivider`, `PpMicro`, `PpPressable`, `SessionMode`, `ViewLayout`
- **Used by (1):** `PpSessionToolbar`

### ScreenSessionMode
`src/Gui/session/ScreenSessionMode.qml` · 202 lines · root `Item` · ⚠ **no app file instantiates this**

> Reusable session-mode screen (Swing, GRF and Coach rail screens; the Wrist
> screen has its own ScreenWrist). Hosts the persistent session toolbar then a
> View-driven layout — an optional timeline rail, the centre stage (camera tiles
> today; charts/table land later), and an optional shot carousel —
> each gated by the toolbar's View control (ViewLayout, per session type).
> Replay and Analyse use the Transit timeline, in one of two orientations
> (appSettings.timelineOrientation): HORIZONTAL lives in the top rail (opens down
> from the toolbar); VERTICAL lives in a fixed-width panel to the LEFT of the
> stage. Capture has no timeline (RmTimelineChart lives only on the resource monitor).

- **Properties:** `string iconText`, `string titleText`, `int sessionType`
- **Uses:** `PpCameraTiles`, `PpChartsPanel`, `PpDataViewer`, `PpLaunchMonitorPanel`, `PpMarkupPanel`, `PpModeStage`, `PpReplayTransport`, `PpSessionDiagnosticsPanel`, `PpSessionToolbar`, `PpShotCarousel`, `PpTopoBackground`, `PpTransitTimeline`, `PpTrustPanel`, `SessionMode`, `ViewLayout`

### ScreenWrist
`src/Gui/session/ScreenWrist.qml` · 239 lines · root `Item`

> Wrist Motion screen (contentStack index 2). Hosts the persistent session
> toolbar, then a View-driven layout: an optional timeline rail, the centre
> stage (camera tiles today; charts/table land later), and an
> optional shot carousel. Which panels show, and how the stage packs them, is
> owned by the toolbar's View control (ViewLayout, per session type). The
> toolbar's device panels run calibration entirely in-panel, so this screen
> neither exposes nor routes any calibrate request.
> Replay and Analyse use the Transit timeline, in one of two orientations
> (appSettings.timelineOrientation): HORIZONTAL in the top rail, VERTICAL in a
> fixed-width panel to the LEFT of the stage. Kept in lockstep with
> ScreenSessionMode. Capture has no timeline (RmTimelineChart is resource-monitor only).

- **Uses:** `PpCameraTiles`, `PpChartsPanel`, `PpDataViewer`, `PpLaunchMonitorPanel`, `PpMarkupPanel`, `PpModeStage`, `PpReplayTransport`, `PpSessionDiagnosticsPanel`, `PpSessionToolbar`, `PpShotCarousel`, `PpTransitTimeline`, `PpTrustPanel`, `SessionMode`, `SwingViz3DHost`, `ViewLayout`
- **Used by (1):** `Main`

### SessionMode
`src/Gui/session/SessionMode.qml` · 157 lines · root `QtObject` · **singleton** · no direct `Theme.*` refs

> Session-screen MODE — the layout/activity axis (Capture | Replay | Analyse).
> Effectively global: only one session screen is visible at a time, so a single
> singleton holds the active mode and the focused swing that Replay/Analyse show.
> Mode is ORTHOGONAL to the data-source axis (Live vs Loaded, owned by
> SessionReviewController.reviewActive — kept under its legacy "review" name for
> now). Replay/Analyse work on either data-source; only Capture forces Live, so
> entering Capture returns to the live current session. The two compose — do not
> fuse them.
> Enum values are plain ints (a QML singleton can't host a Q_ENUM cleanly):
> capture = 0, replay = 1, analyse = 2. Compare with `=== SessionMode.replay`.
> shotReplay (a context property) is reachable from this singleton's root context.

- **Properties:** `readonly int capture`, `readonly int replay`, `readonly int analyse`, `int mode`, `int focusedShotId`, `string focusedSwingDir`, `var sessionDiagnostics`, `var diagnosticsNav`, `var chartsNav`, `bool autoReturnToCapture`
- **Functions:** `enterCapture()`, `enterAnalyse()`, `showReplay()`, `enterReplay(shotId, swingDir, fromCapture)`, `clearFocus()`, `enterLoadedSession()`
- **Used by (24):** `AnnotationTool`, `Main`, `PpCameraFrame`, `PpCameraTiles`, `PpChartsBody`, `PpChartsPanel`, `PpDataViewer`, `PpHeader`, `PpLaunchMonitorPanel`, `PpLmGraphicsBody` (+14)
- **Exercised by tests/tools:** `LmPanelHost`, `charts_ks_gates`, `detail_hover`, `diag_measure_tabs`, `diag_nav_survives_mode_switch`, `diag_not_judged` (+20)

### ViewLayout
`src/Gui/session/ViewLayout.qml` · 487 lines · root `QtObject` · **singleton** · no direct `Theme.*` refs

> Resolves the session-stage layout (which panels are visible + how the centre
> stage packs them) per session MODE (Capture | Replay | Analyse), persisting to
> appSettings.viewLayoutByMode. Three orthogonal concerns: VISIBILITY (panel
> on/off), ARRANGEMENT (stage packing), and MOTION (the per-element overlay
> drawn on the camera panel — pose skeleton + club shaft + ball). There are no
> named panel/arrangement presets — every panel/arrangement edit just updates
> that mode's layout in place. Motion DOES have named presets (see below).
> Panels split into two homes:
> stage panels — camera/charts/sessionDiagnostics/launchMonitor/table/swing3d/markup, arranged
> inside PpModeStage
> rail panels  — timeline/carousel, full-width rails the screen shows/hides
> A panel key is legal by being in PpModeStage's `_defs` and PpViewPanel's catalogue;
> nothing here validates one, and a stored key no host wires is simply not shown. A RETIRED key
> is the exception: it is read, and rewritten once, as its successor (_retiredKeys below). What
> defaultLayout() OMITS is therefore the real switch: "launchMonitor" is deliberately
> absent from every default, because a golfer with no monitor must never be shown an
> …

- **Functions:** `defaultLayout(mode)`, `presetCatalog()`, `enabledKeysFor(mode)`, `isPanelOn(mode, key)`, `arrangementFor(mode)`, `motionOn(mode)`, `motionPreset(mode)`, `motionFor(mode)`, `elementMode(mode, key)`, `motionTraceTarget(mode)` (+12)
- **Uses:** `SessionMode`
- **Used by (11):** `Main`, `PpCameraFrame`, `PpCameraTiles`, `PpModeStage`, `PpMotionPanel`, `PpSessionToolbar`, `PpTrustInline`, `PpViewPanel`, `ScreenSessionMode`, `ScreenWrist` (+1)
- **Exercised by tests/tools:** `charts_ks_gates`, `detail_hover`, `diag_measure_tabs`, `diag_nav_survives_mode_switch`, `diag_not_judged`, `diag_print_pdf` (+11)
