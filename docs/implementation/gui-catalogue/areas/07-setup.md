# Session setup

*Directory:* `src/Gui/setup/` (+ `pages/`, `parts/`)  
*21 components · 4,308 lines of QML*

The session-setup flow (replaced the old wizard; docs/design/session_wizard_refactor_design.md): a non-visual flow engine, step descriptors as data, a hardware-context object, a WizardPage contract, nine step pages and a few shared parts.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [ScreenSessionSetup](#screensessionsetup) | 419 | `Item` | 1 | 8 |  |
| [SetupContext](#setupcontext) | 661 | `QtObject` | 1 | 1 |  |
| [SetupDraft](#setupdraft) | 137 | `QtObject` | 1 | 0 |  |
| [SetupFlow](#setupflow) | 391 | `QtObject` | 1 | 1 |  |
| [SetupSteps](#setupsteps) | 292 | `QtObject` | 1 | 10 |  |
| [StepDescriptor](#stepdescriptor) | 66 | `QtObject` | 1 | 0 |  |
| [WizardPage](#wizardpage) | 71 | `Item` | 10 | 0 |  |
| [BallPage](#ballpage) | 168 | `WizardPage` | 1 | 4 |  |
| [CalibrateArmPage](#calibratearmpage) | 96 | `WizardPage` | 1 | 2 |  |
| [CamerasPage](#cameraspage) | 204 | `WizardPage` | 1 | 3 |  |
| [CheckArmPage](#checkarmpage) | 189 | `WizardPage` | 1 | 3 |  |
| [FramingPage](#framingpage) | 233 | `WizardPage` | 1 | 4 |  |
| [GoalsPage](#goalspage) | 136 | `WizardPage` | 1 | 3 |  |
| [ImusPage](#imuspage) | 412 | `WizardPage` | 1 | 5 |  |
| [ReadyPage](#readypage) | 346 | `WizardPage` | 1 | 2 | 1 local comp. |
| [TriangulatePage](#triangulatepage) | 98 | `WizardPage` | 1 | 4 |  |
| [SetupCheckRow](#setupcheckrow) | 169 | `Item` | 4 | 4 |  |
| [SetupStatusCircle](#setupstatuscircle) | 44 | `Rectangle` | 1 | 0 |  |
| [SetupStepIntro](#setupstepintro) | 62 | `Column` | 6 | 1 |  |
| [SetupSummaryRow](#setupsummaryrow) | 62 | `Item` | 1 | 0 |  |
| [SetupTogglePill](#setuptogglepill) | 52 | `Rectangle` | 1 | 0 |  |

## `setup/`

### ScreenSessionSetup
`src/Gui/setup/ScreenSessionSetup.qml` · 419 lines · root `Item` · 3 hard-coded colour(s)

> Session setup — the SHELL (session_wizard_refactor_design.md §4.2). The replacement for
> ScreenSessionWizard.qml, built beside it in Stage 5a; Main has hosted it since Stage 5b, and the
> old file was deleted at Stage 5c. Line references "l.N" to the retired wizard are to its last
> version in git (it is in the history up to the Stage 5c commit).
> What it holds: the visit's SetupDraft, the hardware SetupContext, the step registry
> (SetupSteps), the SetupFlow engine, the progress indicator, ONE Loader for the current page (R2)
> and today's header and footer. What it does NOT hold is any knowledge of a step:
> ⚠ R1 — THIS FILE NAMES NO STEP (lint W10: no registry key appears here or in PpFlowIndicator).
> The footer is driven only by the current page's contract (canContinue, canSkip, primary, hint,
> hintTone, busy, fullBleed — WizardPage.qml) and by the flow (isFirst, isLast, noLongerNeeded,
> issues). A step-specific `if` belongs in its page or its descriptor, never here.
> The header, footer, rules and spacing are today's wizard's (the retired ScreenSessionWizard.qml l.590–668,
> 780–806, 2048–2240), strings and colours unchanged. The tab strip (l.669–778) is replaced by
> PpFlowIndicator (§4.9), which offers jump-back only (D5).
> Behaviour that differs from today's wizard, all approved (§6, §10):
> - the header › is Continue only, disabled when Continue is; it never runs a page's primary
> …

- **Properties:** `readonly var presets`, `readonly int settingsPanelCameras`, `readonly int settingsPanelImus`, `readonly bool canHeaderBack`, `readonly bool canHeaderForward`, `readonly var flow`, `readonly var ctx`, `readonly var draft`, `readonly var steps`, `readonly int contentWidth`
- **Signals:** `cancelled()`, `sessionStartRequested(int sessionType, var goalKeys)`, `cameraRecalibrateRequested()`, `navigateToSettings(int panelIndex)`, `exitRequested(string reason)`
- **Functions:** `open(preset)`, `headerBack()`, `headerForward()`, `suspend()`, `resume()`
- **Uses:** `PpButton`, `PpConnectingFrame`, `PpFlowIndicator`, `PpPressable`, `SetupContext`, `SetupDraft`, `SetupFlow`, `SetupSteps`
- **Used by (1):** `Main`
- **Exercised by tests/tools:** `CalibDriver`, `SetupDriver`, `setup_walk`, `tst_setup_smoke`

### SetupContext
`src/Gui/setup/SetupContext.qml` · 661 lines · root `QtObject`

> The hardware, as session setup sees it (session_wizard_refactor_design.md §4.8, R6). The ONE
> object in the flow that touches the managers: descriptors, the flow and the pages read the
> derivations below and call the actions below, and never name cameraManager or imuManager
> (lint W4 for pages). This fixes F12: the slot → device → unit lookup was re-derived about ten
> times in the retired ScreenSessionWizard.qml, each copy with its own hand-placed dependency reads, and one
> missing read made a binding go stale.
> ⚠ REACTIVE DEPENDENCIES ARE WRITTEN ONCE, HERE. The role resolvers (deviceIdForRole, roleForDevice,
> instanceForRole …) are Q_INVOKABLEs and not reactive on their own, so every binding that calls
> them first ASSIGNS from the properties that change their answer: imuDeviceList (enumeration and
> session enablement), instances (connection — the real manager re-emits instancesChanged on every
> imuConnected change, imu_manager.cpp createInstance) and appSettings.imuRoles (placement).
> They are assigned to a local, never written as bare statements: a bare `a.b` line is dropped
> by the QML compiler and the dependency silently goes with it (memory note
> qml-dead-statement-bindings).
> Inputs default to the app's context properties and are injectable, so the tests build this over
> fakes without a child context if they want to.
> …

- **Properties:** `var cameraMgr`, `var imuMgr`, `var settings`, `var athlete`, `var launchMon`, `var draft`, `var liveWrist`, `readonly int settingsPanelCameras`, `readonly int settingsPanelImus`, `readonly var presets`, `readonly string athleteName`, `readonly bool leftHanded`, `readonly var cameraList`, `readonly var faceOn` … (+39 more)
- **Signals:** `mountRefused(string deviceId, string role, string holderId)`
- **Functions:** `cameraInstanceFor(cameraKey)`, `cameraPoseEnabled(inst)`, `setCameraPoseEnabled(inst, on)`, `connectCameras()`, `setCameraEnabled(cameraKey, on)`, `ensureBallRoi()`, `relearnBallBaseline()`, `connectSensors()`, `cancelPacedConnect()`, `setSensorEnabled(deviceId, on)` (+8)
- **Uses:** `ImuMounts`
- **Used by (1):** `ScreenSessionSetup`
- **Exercised by tests/tools:** `FlowShell`

### SetupDraft
`src/Gui/setup/SetupDraft.qml` · 137 lines · root `QtObject` · no direct `Theme.*` refs

> One visit to session setup (session_wizard_refactor_design.md §4.6): what outlives the pages,
> which exist only while current (R2). Reset by SetupFlow.open() and nowhere else.
> ⚠ reset() IS THE ONLY PLACE SAVED SETTINGS ARE SEEDED (F4). Today's wizard re-seeded the goals
> and the per-session camera/sensor enablement on every `visible` edge (wizard l.571–588), so a
> trip to Settings and back threw away the choices made in this visit. A suspend/resume does not
> touch this object.
> Every write REPLACES `states` / `outcomes` with a new object: bindings over a var property see
> an assignment, never an in-place mutation.

- **Properties:** `var ctx`, `int preset`, `var goals`, `bool goalsInteracted`, `var states`, `var outcomes`, `readonly var goalDefs`, `readonly var defaultGoal`
- **Functions:** `state(key)`, `setState(key, s)`, `setStates(map)`, `toggleGoal(key)`, `goalsForStart()`, `recordOutcome(group, routine, devices)`, `clearOutcome(group)`, `outcome(group)`, `reset(presetIndex)`
- **Used by (1):** `ScreenSessionSetup`
- **Exercised by tests/tools:** `FlowShell`

### SetupFlow
`src/Gui/setup/SetupFlow.qml` · 391 lines · root `QtObject`

> The session-setup flow engine (session_wizard_refactor_design.md §4.5). Non-visual. It owns the
> plan, the current step, the navigation queue and the page lifecycle; the decisions themselves are
> the pure functions in setup_flow.js, and the visit's state (goals, step states, outcomes) lives
> in the SetupDraft.
> ── Wiring (the host — the setup shell, or a test's FlowShell) ─────────────────────────────────
> SetupContext { id: ctx; draft: draft }
> SetupDraft   { id: draft; ctx: ctx }
> SetupSteps   { id: steps }
> Loader       { id: pageLoader; … geometry … }
> SetupFlow    { registry: steps; ctx: ctx; draft: draft; loader: pageLoader }
> DECISION(stage4): the HOST owns the Loader and hands it in; the flow drives it with setSource().
> The Loader is a visual item that the shell lays out (full-bleed or reading column), so it belongs
> in the shell's tree; a flow-owned Loader would have to be re-parented, which is more code and
> one more thing to get wrong. The flow never touches its geometry.
> ── Rules it enforces ─────────────────────────────────────────────────────────────────────────────
> R2  Exactly one page object is alive. A transition leaves the old page, unloads it, WAITS until it
> …

- **Properties:** `var registry`, `var ctx`, `var draft`, `Loader loader`, `bool isOpen`, `bool suspended`, `string current`, `WizardPage page`, `readonly bool wired`, `readonly int preset`, `readonly var descriptors`, `readonly var plan`, `readonly var numbering`, `readonly var states` … (+15 more)
- **Signals:** `cancelled()`, `startRequested(int preset, var goals)`, `exitRequested(string reason)`, `settingsRequested(int panelIndex)`, `cameraRecalibrateRequested()`
- **Functions:** `stepLabelFor(key)`, `open(presetIndex)`, `next(mark)`, `back()`, `goTo(key, resetStates)`, `suspend()`, `resume()`, `exit(reason)`, `canGoTo(key)`, `openSettings(panelIndex)` (+2)
- **Uses:** `WizardPage`
- **Used by (1):** `ScreenSessionSetup`
- **Exercised by tests/tools:** `FlowShell`, `tst_setup_flow`

### SetupSteps
`src/Gui/setup/SetupSteps.qml` · 292 lines · root `QtObject`

> THE step registry (session_wizard_refactor_design.md §4.4, R1). A step exists here, as one
> StepDescriptor, and in one page file under pages/ — nowhere else. Order here is the order of the
> flow; nothing reorders it.
> ⚠ R6 / §4.12 / lint W8: no `applies` and no `gate` below may mention the session type or the
> preset. Steps come from the hardware in the session: with no arm sensor there is no arm
> Calibrate or Check step, whatever the session was opened as.
> The issue and summary texts are today's (wizard l.489–565, 1947–2015) wherever today had one;
> the sensor issues are new because the per-type slot table they came from is gone (§4.12). Each
> new string is marked DECISION(stage4), and Stage 6 re-baselines Ready's golden tables on purpose.
> Adding a step (§4.10): one descriptor here, one page file. The trunk work adds the two lines
> marked TRUNK below. A step can also be added from outside through `extensions` (test N17).

- **Properties:** `var pageOverride`, `readonly var groupLabels`, `list<StepDescriptor> extensions`, `readonly list<StepDescriptor> builtIn`, `readonly var descriptors`, `readonly var keys`, `readonly var instrumentGroups`
- **Functions:** `descriptor(key)`, `pageUrl(key)`
- **Uses:** `BallPage`, `CalibrateArmPage`, `CamerasPage`, `CheckArmPage`, `FramingPage`, `GoalsPage`, `ImusPage`, `ReadyPage`, `StepDescriptor`, `TriangulatePage`
- **Used by (1):** `ScreenSessionSetup`
- **Exercised by tests/tools:** `FlowShell`

### StepDescriptor
`src/Gui/setup/StepDescriptor.qml` · 66 lines · root `QtObject`

> One step of session setup, as DATA (session_wizard_refactor_design.md §4.4, R1). A step is this
> descriptor plus one page file and nothing else: the flow, the indicator, the numbering, the
> closing page's issues and summary all read it from here.
> ⚠ applies / gate / issues / summary are evaluated WITHOUT the page alive — by the flow's plan
> binding, by Ready, and by the indicator's "needs attention" ring on a step that is not current.
> So they read the context (`ctx`, SetupContext) and the draft (`draft`, SetupDraft) and nothing
> else: no page property, no manager. Bindings that call them pick up their reactive
> dependencies from the reads they make, so a predicate must READ what it depends on (a bare
> `ctx.foo` statement is dropped by the compiler — memory note qml-dead-statement-bindings).
> ⚠ No applies or gate may read the session type or the preset (§4.12, lint W8): hardware
> decides which steps run, the preset only picks goals and where Start lands.

- **Properties:** `string key`, `string title`, `string eyebrow`, `string group`, `string instrumentGroup`, `string page`, `string after`, `var applies`, `var gate`, `var issues`, `var summary`
- **Used by (1):** `SetupSteps`
- **Exercised by tests/tools:** `setup_soak`, `tst_setup_flow`, `tst_setup_nav`

### WizardPage
`src/Gui/setup/WizardPage.qml` · 71 lines · root `Item`

> The page contract (session_wizard_refactor_design.md §4.3). Every page under setup/pages/ has a
> WizardPage as its root (lint W2). A page exists only while it is the current step (R2): the
> flow creates it through the shell's Loader and destroys it when the step is left, so any state
> that must outlive it lives in the draft or in a manager.
> Lifecycle, driven by SetupFlow and never by the page itself:
> created → active = true → enter(direction) … leave(reason) → active = false → destroyed
> `direction`: "forward" | "back" | "jump" | "resume".
> `reason`:    "forward" | "skip" | "back" | "jump" | "suspend" | "exit".
> A suspend (the shell hidden for Settings) is leave("suspend") + active = false with the page
> kept; the resume is active = true + enter("resume").
> ⚠ R3: every Timer `running`, every Connections `enabled` and every animation in a page (or in
> a routine it hosts) is gated on `active` — never on `visible`, never on a step index (lint W1).
> ⚠ R5: a page REQUESTS navigation (flow.next("done"), flow.back(), …). The flow applies it on a
> later event-loop turn, so code after the call in the same handler still runs on a live page.
> ⚠ Hardware goes through `ctx` only; a page never names imuManager or cameraManager (lint W4).

- **Properties:** `bool active`, `var flow`, `var ctx`, `var draft`, `string stepLabel`, `string stepKey`, `bool canContinue`, `bool canSkip`, `var primary`, `string hint`, `string hintTone`, `bool fullBleed`, `bool busy`
- **Functions:** `enter(direction)`, `leave(reason)`
- **Used by (10):** `BallPage`, `CalibrateArmPage`, `CamerasPage`, `CheckArmPage`, `FramingPage`, `GoalsPage`, `ImusPage`, `ReadyPage`, `SetupFlow`, `TriangulatePage`
- **Exercised by tests/tools:** `DummyPage`, `ExtraPage`

## `setup/pages/`

### BallPage
`src/Gui/setup/pages/BallPage.qml` · 168 lines · root `WizardPage`

> Session setup — Ball Detection, v2 temporal detector (the retired ScreenSessionWizard.qml Panel 3,
> l.1155–1287, with its footer hint, Skip and dimming, l.2090–2092, 2112, 2132, 2174), ported
> with every string, colour and size unchanged.
> Always in the flow. The user frames the hitting area, presses Learn to seed the empty-mat
> baseline (relearnBallBaseline → Option A), then places a ball; the step stays live (ballPresent,
> with a ting) and GATES Continue on a detected ball (also the registry's `gate`). Skip is an
> escape hatch. No calibration profile — the temporal detector self-calibrates at runtime.

- **Properties:** `bool learning`
- **Functions:** `enter(direction)`, `leave(reason)`
- **Uses:** `PpButton`, `PpCameraFrame`, `SetupStepIntro`, `WizardPage`
- **Used by (1):** `SetupSteps`

### CalibrateArmPage
`src/Gui/setup/pages/CalibrateArmPage.qml` · 96 lines · root `WizardPage` · no direct `Theme.*` refs

> Session setup — Calibrate (the arm group's calibrate step; the retired ScreenSessionWizard.qml Panel 5,
> l.1640–1660, with its footer hint and Skip, l.2098–2100, 2134). Design §4.7 "Hosts".
> ONE guide view and ONE routine: the page hosts ImuCalibrationFlow in its full layout, which
> holds exactly one BodyVizView and loads exactly one routine — the leadForearm holder's vendor,
> the same answer as ctx.groups.arm.routine. The page exists only while Calibrate is current
> (R2), so neither does the routine (R7, fixes F3 for the wizard host).
> Lifecycle (D1, R8):
> enter  — a recorded arm outcome that is still VALID (draft.outcome: the devices' live
> validity) shows complete with no re-run (showCompleted, fixes F1 for both vendors);
> otherwise a fresh run (begin).
> leave  — nothing to do here: the flow sets `active` false after leave(), and the calibration
> flow stops its routine on that edge (a HackMotion aborts, once).
> completed — the outcome goes into the draft, which outlives this page (Check, Ready, the
> indicator read it there).
> Recalibrate (the status column's button) begins a fresh run; the outcome is dropped from the
> draft as the run's done goes false (below), so Continue closes at once.

- **Functions:** `enter(direction)`
- **Uses:** `ImuCalibrationFlow`, `WizardPage`
- **Used by (1):** `SetupSteps`
- **Exercised by tests/tools:** `setup_soak`

### CamerasPage
`src/Gui/setup/pages/CamerasPage.qml` · 204 lines · root `WizardPage`

> Session setup — Cameras (the retired ScreenSessionWizard.qml Panel 1, l.917–1092, with its footer hint,
> Skip, Connect and traveling-light frame, l.2075–2084, 2130, 2154–2158, 2200–2204, 2232),
> ported with every string, colour and size unchanged. Hardware through `ctx` only (lint W4).
> Connect is the footer's `primary` until nothing enabled is left to connect; while it is, the
> page does not offer Continue, so the header › (Continue only) cannot mark the step done without
> connecting (F5, D2).
> DECISION(stage5a): the two-camera branches of today's panel are not carried: the "Down-the-line
> camera" placeholder row and the "Down-the-line camera not assigned" hint (l.964–965, 2081–2082)
> and the two-view intro text (l.982) applied only when the session type required two cameras —
> the three comingSoon types, which Home never opens. Nothing is "required" any more (§4.12), and
> SetupContext.camsAllConnected already dropped the same clause in Stage 4. For every session
> that can be opened, the page is today's.

- **Functions:** `camDetail(d)`
- **Uses:** `SetupCheckRow`, `SetupStepIntro`, `WizardPage`
- **Used by (1):** `SetupSteps`

### CheckArmPage
`src/Gui/setup/pages/CheckArmPage.qml` · 189 lines · root `WizardPage`

> Session setup — Confirm tracking (the arm group's check step; the retired ScreenSessionWizard.qml Panel 6,
> l.1662–1822), strings, colours and layout unchanged.
> The ArmVizView is THIS page's: it exists only while Check is current (R2 — CK2, and with the
> Calibrate page's guide view, at most one View3D under the shell: L2). The live wrist computer
> runs only while the page is active, switched in enter() and leave() — NOT by a permanent
> Binding, which forced it false on every step change and so overrode anyone else who had turned
> it on (F6, CK1).
> Recalibrate drops the arm outcome and jumps back to Calibrate with Calibrate and Check pending
> (N15); the Calibrate page then begins a fresh run, because nothing valid is recorded.

- **Properties:** `readonly var liveWrist`
- **Functions:** `enter(direction)`, `leave(reason)`, `recalibrate()`
- **Uses:** `ArmVizView`, `PpDisplayText`, `WizardPage`
- **Used by (1):** `SetupSteps`
- **Exercised by tests/tools:** `setup_soak`

### FramingPage
`src/Gui/setup/pages/FramingPage.qml` · 233 lines · root `WizardPage`

> Session setup — Framing. One live frame per connected camera with the pose skeleton over it, and
> a verdict per camera on the headroom above the hands at the top of the backswing.
> Why: on 5 Oct 2026 the down-the-line frame put the hands at the top only 44–113 px below the top
> edge of a 988 px frame (4–11 %); the club left the picture from just after P3 to about P5 on all
> 31 swings and the 3-D club was unmeasured there. The rule (two_camera_capture_protocol.md §2,
> "headroom check"): at the top the wrists sit at least one club length below the top edge —
> roughly the lower two-thirds of the frame — and the clubhead is in view at the top and the
> finish, in every camera. MoveNet sees wrists, not the clubhead, so the page checks the wrists
> and the intro asks the golfer to look for the clubhead.
> Never gates Continue: a camera that cannot be moved still records, and the golfer decides.
> Pose is turned on per instance for the visit and put back on leave (the Capture view's Motion
> overlay owns it otherwise). Hardware through `ctx` only (lint W4).

- **Properties:** `readonly real minScore`, `readonly real minHeadroom`, `readonly int windowMs`, `var verdicts`
- **Functions:** `verdictOf(key)`, `verdictText(key)`, `cameraLabel(c)`, `enter(direction)`, `leave(reason)`
- **Uses:** `PpCameraFrame`, `SetupCheckRow`, `SetupStepIntro`, `WizardPage`
- **Used by (1):** `SetupSteps`

### GoalsPage
`src/Gui/setup/pages/GoalsPage.qml` · 136 lines · root `WizardPage` · 1 hard-coded colour(s)

> Session setup — Goals (the retired ScreenSessionWizard.qml Panel 0, l.807–915, and its footer hint,
> l.2070–2074), ported with every string, colour and size unchanged. The picks live in the draft
> (SetupDraft.goals), not here: this page exists only while current (R2), and a trip to Settings
> no longer re-seeds them (F4).

- **Uses:** `PpPressable`, `SetupStepIntro`, `WizardPage`
- **Used by (1):** `SetupSteps`

### ImusPage
`src/Gui/setup/pages/ImusPage.qml` · 412 lines · root `WizardPage` · 1 hard-coded colour(s)

> Session setup — Sensors (the retired ScreenSessionWizard.qml Panel 4, l.1289–1638, with its footer hint,
> Skip, Connect and traveling-light frame, l.2093–2097, 2133, 2150–2152, 2175, 2187, 2206–2208,
> 2233–2234). Hardware through `ctx` only (lint W4).
> What changed from today's panel (design §4.12, "found is not wanted"):
> - one row per ENUMERATED sensor, not one per slot of a per-session-type table (that table is
> gone). A wG3 is one row: one peripheral, one enable switch, one mount ("Lead forearm + hand");
> - each row shows its MOUNT by name. A remembered mount is confirmed, not asked again (mounts
> are settings and stick); a sensor that is in the session with no mount gets the question
> "Where is this sensor worn?" in the row, and "change" reopens it for a mounted one. The
> choices are the Settings ▸ IMUs combo's (ImuMounts, the manager's one-claim rule): a mount
> another sensor holds is greyed and names its holder; a refusal leaves the mount as it was
> and says who holds it, in the page;
> - a mount whose sensor the scan cannot see is its own row, "<Mount> — <name> (not found)",
> with today's "power it on and Scan" (D6, reworded from slot to mount);
> - Connect is the paced connect in ImuManager (one call; a wG3 connects once, D4). The primary
> reads "Connecting…" for as long as the queue or any sensor of the session is still
> …

- **Functions:** `chooseMount(deviceId, role)`
- **Uses:** `ImuMounts`, `PpPressable`, `SetupCheckRow`, `SetupStepIntro`, `WizardPage`
- **Used by (1):** `SetupSteps`

### ReadyPage
`src/Gui/setup/pages/ReadyPage.qml` · 346 lines · root `WizardPage`

> Session setup — Ready, the closing page: "What this session will record" (design §4.14;
> the retired ScreenSessionWizard.qml Panel 7, l.1824–2043, for the heading, the issue list, the summary
> table and the notice, strings and colours unchanged).
> Mark, 5 Oct: close with a page describing what is mounted and how that will impact the
> recording — no trunk sensors and rotation is degraded, no arm/wrist sensors and no wrist
> metrics, no launch monitor and no ball metrics. Top to bottom:
> the heading and today's copy ("good to go" when no step reports an issue);
> To fix before you start — the flow's issues, each with its settings link;
> What is set up — the flow's summary rows (one per step that has one) and the launch monitor;
> What this session will record — one row per family, from ctx.capability (the catalogue's
> resolver over the live setup, worded by setup_capability_rows.js — this page only draws it);
> the notice.
> Start is the footer's and is always allowed (§4.12): this page describes, it gates nothing.

- **Local components:** `SectionLabel`
- **Uses:** `SetupSummaryRow`, `WizardPage`
- **Used by (1):** `SetupSteps`
- **Exercised by tests/tools:** `ready_forecast`

### TriangulatePage
`src/Gui/setup/pages/TriangulatePage.qml` · 98 lines · root `WizardPage`

> Session setup — Triangulate (the retired ScreenSessionWizard.qml Panel 2, l.1094–1153, with its footer hint
> and Skip, l.2085–2089, 2109–2111, 2131), ported with every string, colour and size unchanged.
> Only in the plan when both camera perspectives are connected (the registry's `applies`).
> Hosts the same stereo-calibration STUB PpCameraPanel's calibrate mode uses; the real ChArUco
> capture flow drops in here when the pipeline lands (TODO). The stub's validity flags are
> SetupContext's, still always false — kept as today (D7, F11).
> If the DTL camera is deselected while this page is current, the step stays current and the
> shell's footer reads "No longer needed — Continue" (SetupFlow.noLongerNeeded, N14); nothing
> here has to know.

- **Uses:** `CameraCalibrationFlow`, `SetupCheckRow`, `SetupStepIntro`, `WizardPage`
- **Used by (1):** `SetupSteps`

## `setup/parts/`

### SetupCheckRow
`src/Gui/setup/parts/SetupCheckRow.qml` · 169 lines · root `Item`

> One device or check row: status circle, label + sub-text, optional live camera thumbnail, enable
> toggle, Recalibrate button and status chip. Ported verbatim from the retired ScreenSessionWizard.qml's inline
> `component CheckRow` (l.2330–2471) — the camera rows (with their thumbnail Loader), the
> Triangulate checks and, in Stage 5b, the sensor rows all use it.

- **Properties:** `bool ok`, `bool warn`, `bool optional`, `string label`, `string subOk`, `string subWarn`, `string subFail`, `bool showRecal`, `bool recalEnabled`, `bool disabled`, `string subDisabled`, `bool showToggle`, `bool toggleChecked`, `string chipText` … (+3 more)
- **Signals:** `toggled(bool value)`, `recalibrate()`
- **Uses:** `PpButton`, `PpCameraFrame`, `SetupStatusCircle`, `SetupTogglePill`
- **Used by (4):** `CamerasPage`, `FramingPage`, `ImusPage`, `TriangulatePage`

### SetupStatusCircle
`src/Gui/setup/parts/SetupStatusCircle.qml` · 44 lines · root `Rectangle`

> The status circle at the left of a SetupCheckRow. Ported verbatim from the retired ScreenSessionWizard.qml's
> inline `component StatusCircle` (l.2309–2328).

- **Properties:** `bool ok`, `bool warn`, `bool error`
- **Used by (1):** `SetupCheckRow`

### SetupStepIntro
`src/Gui/setup/parts/SetupStepIntro.qml` · 62 lines · root `Column`

> A setup page's eyebrow, heading and body, over a hairline. Ported verbatim from the wizard's
> inline `component StepIntro` (the retired ScreenSessionWizard.qml l.2244–2277). The pages bind `eyebrow`
> to WizardPage.stepLabel, which the flow keeps in step with the indicator's numbering.
> Named Setup… because several other files still declare an inline StepIntro / CheckRow /
> TogglePill of their own; a module type of the same name would sit beside each of them.

- **Properties:** `string eyebrow`, `string heading`, `string body`
- **Uses:** `PpDisplayText`
- **Used by (6):** `BallPage`, `CamerasPage`, `FramingPage`, `GoalsPage`, `ImusPage`, `TriangulatePage`

### SetupSummaryRow
`src/Gui/setup/parts/SetupSummaryRow.qml` · 62 lines · root `Item`

> One row of the closing page's summary table: dot, label, value. Ported verbatim from
> the retired ScreenSessionWizard.qml's inline `component SummaryRow` (l.2473–2503), ready for the Stage 5b
> closing page, which renders SetupFlow.summaryRows through it.

- **Properties:** `string rowLabel`, `string rowValue`, `bool good`, `string tone`
- **Used by (1):** `ReadyPage`

### SetupTogglePill
`src/Gui/setup/parts/SetupTogglePill.qml` · 52 lines · root `Rectangle`

> Small on/off toggle — mirrors the TogglePill in ImusPanel.qml so the
> wizard's per-IMU enable switch looks and behaves identically to Settings.
> (Ported verbatim from the retired ScreenSessionWizard.qml's inline `component TogglePill`, l.2279–2307.)

- **Properties:** `bool checked`
- **Signals:** `toggled(bool value)`
- **Used by (1):** `SetupCheckRow`
