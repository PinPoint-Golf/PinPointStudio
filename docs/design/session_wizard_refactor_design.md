# Session setup wizard — audit and refactor design

**Status:** design, not started. Written 5 October 2026. This is the preparation for the trunk IMU work (`trunk_imu_design.md` §5.6–5.7, §6.7), which needs new wizard pages.
**Owner:** Mark.
**Protected files:** `src/Gui/session/ScreenSessionWizard.qml` and `src/Gui/calibration/ImuCalibrationFlow.qml` (memory note `do-not-touch-the-session-wizard`). The refactor session is the approval context for changing them. Each stage in §8 is shown with its test run before the next stage starts.
**Builds on:**
- `trunk_imu_design.md`: the pages this refactor has to make easy to add.
- `pinpoint_qml_design_system.md`: the palette and components the progress indicator uses.
- Memory notes `view3d-disappearance-watch`, `verify-ui-by-probing`, `qml-dead-statement-bindings`, `analysis-is-session-agnostic`.

---

## 0. Summary

**The wizard is brittle for one structural reason.** Each step's knowledge is spread over about a dozen places in a 2,504-line file: the index constants, two hand-written state arrays, the tab-label array, `visibleSteps`, the footer hint text, the hint colour, the Skip rule, the primary-button label, its opacity, `goNext`'s gates, `readinessIssues` and the summary rows. Adding a step means editing all of them, and missing one fails silently at runtime.

**The calibration chain is fragile for a second reason: nothing tells it that the user has left.**
- Its timers, the guide animation and its device signal handlers are gated on `visible`, on a gate flag that is set once and never cleared, or on nothing at all.
- Both calibration hosts stay instantiated for the life of the app: the wizard sits in `Main.qml`'s StackLayout, and the toolbar IMU panel sits in a Popup.
- So the chain keeps running after Back, Skip, a trip to Settings or Start. Every device phase change also reaches every hidden copy of the flow.

**Five findings look like real defects** (§3). The two worst:
- **F1.** Going Back from "Check your sensor" to "Calibrate" wipes a good calibration. The "already calibrated" test reads a property that is never true, so every entry re-runs the routine. The re-run starts by clearing both calibrations on every segment.
- **F3.** A HackMotion routine is probably driven by two flows at once: the wizard's and the hidden toolbar panel's. Each sends its own position markers to the device.

These come from reading the code. Stage 0 confirms them by running it before anything is changed.

**The design** (§4–§6):
- **A step registry.** Each step is one descriptor plus one page file.
- **A small flow engine.** It owns the order, the states, navigation and the page lifecycle.
- **Pages that exist only while they are current.** A page that does not exist cannot fire a timer.
- **An explicit enter/leave contract.** Every timer and handler is gated on one `active` flag.
- **Calibration routines split from their 3-D guide.** The guide sits behind an interface, so the chains can be tested with a fake guide and fake sensors.
- **A grouped progress indicator** replaces today's tab strip.
- **Applicability comes from the assigned hardware**, not the session type. This is what the trunk pages need.

**The tests** (§7):
- **About 80 cases in a new `wizard_ui_test` binary**, with fake cameras, sensors, HackMotion device and guide. They run offscreen in seconds, or windowed for the cases that need the real 3-D view.
- **Render and soak probes.**
- **A static lint test.**
- **A hardware checklist for Mark.**

The same suite runs before and after every change. Cases that encode a known defect are marked as expected failures against today's code and flip to passes when the stage that fixes them lands.

---

## 1. Deliverables and definition of done

| Deliverable | Done when |
|---|---|
| This document | Mark has read it and answered §10's decisions. |
| `wizard_ui_test` + fakes + lint (Stage 0) | It builds and runs against **today's** wizard. The baseline pass/expected-fail table is recorded in §11. F1–F5 are confirmed or refuted by a test, not by reading. |
| Refactored wizard (Stages 1–6) | Every test that passed in the baseline still passes, and every expected failure for a defect Mark chose to fix now passes. There are no QML warnings in any test. A dummy step added in a test needs only a registry entry and a page file (test N17). |
| Render and hardware verification (Stage 7) | H2 probes pass: guide frame grabs, and a 6-minute soak with no blank View3D. Mark runs the H3 checklist on Witmotion ×3, wG3, and no-IMU setups, and every row matches its expected outcome and log lines. |
| Commit | Only at the end, after Mark has tested in the app (memory note `feedback_commit_only_at_the_end`). |

A stage whose tests fail is **not** worked around. It stops, the failure is written up in §11, and Mark decides.

---

## 2. The audit: what the wizard is today

### 2.1 Files and hosts

| File | Lines | Role |
|---|---|---|
| `src/Gui/session/ScreenSessionWizard.qml` | 2,504 | The whole wizard: state, navigation, eight panels, footer, five inline components. |
| `src/Gui/calibration/ImuCalibrationFlow.qml` | 1,849 | Two calibration state machines (Witmotion, HackMotion), 11 timers, 3 Connections, two layouts (full, compact), 11 status sub-components. |
| `src/Gui/viz/BodyVizView.qml` | 879 | The 3-D guide avatar. Its `FrameAnimation` slerp emits `leadArmAnimFinished()`, which drives both calibration chains. `fullyLoaded` is the start gate. |
| `src/Gui/viz/ArmVizView.qml` | 504 | The live arm on "Check your sensor". It has its own View3D. |
| `src/Gui/shell/Main.qml` | — | Hosts the wizard at `screenWizard` (10), l.744–781. The header ‹/› drive the wizard's steps (l.625–651). The rail is locked except Settings and System (l.577–596). |
| `src/Gui/imu/PpImuPanel.qml` | — | The **second host** of `ImuCalibrationFlow` (compact), l.244–252, inside the toolbar's IMU Popup (`PpSessionToolbar.qml:717`). It also carries a copy of the paced IMU connect queue (l.99). |
| `src/Gui/home/ScreenHome.qml` | — | Entry: `startSessionRequested(type)` → `Main` calls `sessionWizard.reset(type)` and navigates. |

**Context objects the wizard reads directly:** `cameraManager`, `imuManager`, `appSettings`, `athleteController`, `liveWrist`, `sessionController`. It also uses the enums `CameraInstance.*` and `SessionController.Wrist`. None of these is injectable today, which is why nothing tests the wizard.

### 2.2 Steps

| # | Step | Shown when | Primary button | Continue gate (`goNext`) | Skip shown when |
|---|---|---|---|---|---|
| 0 | Goals | always | Continue | none | never |
| 1 | Cameras | always | **Connect** while any enabled camera is unselected, else Continue | none | not `camsAllConnected` |
| 2 | Triangulate | a face-on **and** a DTL camera are *selected* (`hasTriangulateStep`, l.88) | Continue | none (stubbed; `_todo_*` flags are always false) | not triangulated (always) |
| 3 | Ball | always | Continue (dimmed until ball) | `ballReady`: the face-on instance's `ballPresent` (l.157) | no ball |
| 4 | IMUs | always | **Connect / Connecting…** until all required connected and nothing left to connect, else Continue | none | not `imusAllConnected` |
| 5 | Calibrate | `sessionType === Wrist` (`hasCalibrateStep`, l.82) | Continue (dimmed until done) | `calibFlow.calibrationDone` (l.156) | not done |
| 6 | Check ("Confirm") | as Calibrate | Continue | none | never |
| 7 | Ready | always | Start session / Start anyway | — | never |

Numbering ("STEP x OF N") and the tab strip come from `visibleSteps` (l.119), which is recomputed live. Triangulate appears or disappears the moment a camera is selected or deselected, including from the toolbar.

### 2.3 Pathways

The paths that matter are set by hardware, by the session type and by the slot-A vendor:

| Path | Cameras | IMUs | Steps | Calibration routine |
|---|---|---|---|---|
| W-WT | 1 face-on | 2–3 Witmotion (A, B, opt. C) | 0 1 3 4 5 6 7 | Witmotion: arm-down → T-pose, mount gate |
| W-WT2 | face-on + DTL | 2–3 Witmotion | 0 1 2 3 4 5 6 7 | Witmotion |
| W-HM | 1 face-on | wG3 in A+B (one device) | 0 1 3 4 5 6 7 | HackMotion device routine |
| W-none | none / skipped | none / skipped | 0 1 3 4 5 6 7 | Calibrate shows `NoImuWarning`; only Skip gets past it |
| Other types | — | — | Swing/GRF/Coach are `comingSoon`; Home never opens the wizard for them (`ScreenHome.qml:30`) | — |

**Entry and exit routes:**
- **Entry:** Home → `reset(type)` → navigate. Coming back from Settings, the wizard becomes visible again and `onVisibleChanged` runs (see F4).
- **Exits:**
  - Cancel → `releaseDevices()` + home.
  - ‹ on the first step → `releaseDevices()` + `navController.back()`.
  - Start → navigate to the rail screen, `beginSessionFolder`, `sessionController.start`, `startCapture`.
  - Rail Settings/System, and the "→ Open … settings" deep links on Cameras, IMUs and Ready → the wizard is hidden, not destroyed.

### 2.4 The dependency chain: timers, signals, the 3-D guide

**Witmotion routine** (`ImuCalibrationFlow.qml`). `G` = `flow._autoStartGate`. `V` = `flow.visible`.

```
wizard onCurrentStepChanged(→Calibrate)                         ScreenSessionWizard.qml:217
  └─ calibFlow.begin()  = d._reset() [clears BOTH calibrations on A/B/C] ; G = true      :94
introStartTimer   3000 ms one-shot   running: !isHM && phase==0 && V && G && bvv.fullyLoaded   :975
  └─ bvv.resetArmAnimation(down); dur 3000; _animStage="introUp"; _leadArmTarget = tPose
BodyVizView FrameAnimation (stall-clamped slerp) ── leadArmAnimFinished()     BodyVizView.qml:336
Connections{target: bvv; enabled: G}                                            :994
  "introUp"   → reset(tPose); dur 3000; "introDown"; target = down  → finished
  "introDown" → introReadyTimer 2000 one-shot                                    :1031
                 └─ phase = 1; phase1MinHoldTimer 2000 one-shot → _phase1MinHoldDone  :1046
phase1HoldTimer   100 ms repeat   running: V && G && phase==1 && minHoldDone && !armDownCaptured && leadImu   :1061
  still (<15°/s) for 2000 ms → average → setNominalCalibration on A, B, C → _armDownCaptured
  └─ captureTransitionTimer 800 one-shot → reset(down); dur 1500; "raise"; target = tPose   :1109
       "raise" finished → raiseReadyTimer 2000 one-shot → phase = 2                   :1123
stabilityHoldTimer 100 ms repeat  running: V && G && phase==2 && !done && !mountFailed && leadImu  :901
  still for 2000 ms → refineMountAboutLongAxis per segment → gate (dev ≤15°, grav ≤25°)
  └─ calibrationDone = true → ting + completed()      |   or mountFailed + message
Connections{target: leadImu; enabled: phase>0 && !done} onImuConnectedChanged → calibrationFailed   :861
```

**HackMotion routine.** The device's phase is the authority. Every call into the library is queued and returns nothing.

```
hmStartTimer 1500 one-shot  running: isHM && hmStep==0 && V && G && bvv.fullyLoaded      :1145
  └─ _hmBegin(): checks connected+streaming; poses guide; dev.beginCalibration()
onHmPhaseChanged  (a binding on dev.calibrationPhase — NOT gated by V or G)               :398
  AWAIT_HORIZONTAL → hmStep 1; hmHorizontalSettleTimer 2000 → dev.confirmHorizontal()
  OBSERVING_RAISE  → hmStep 2; anchor travel quat; guide forearm anim 3000 ("hmRaise")
       finished → hmRaiseConfirmTimer 500 → travel ≥15°? dev.confirmRaise() : abort + warn
  APPLYING         → hmStep 3
  VERIFYING        → hmStep 4; guide return 1500 ("hmReturn") → hmRefSettleTimer 1500
                        → dev.confirmReferencePose(); hmPresenceWaitTimer 6000
  COMPLETE         → _hmEvaluate()  (verdict from calibrationState, never phase)
  ABORTED          → _hmStop(reason)
Connections{target: isHM ? leadDevice : null}  (NOT gated)                                 :875
  calibrationCallRefused → _hmRefused ; calibrationInvalidated → _hmInvalidated
  calibrationStateChanged → _hmEvaluate ; imuConnectedChanged(false) → _hmInvalidated
```

**What gates what.** Only the four binding-driven timers stop when the page is hidden: `introStartTimer`, `phase1HoldTimer`, `stabilityHoldTimer` and `hmStartTimer`. The rest keep running:
- the seven one-shot timers, once started;
- the guide animation;
- the `leadArmAnimFinished` chain;
- every HackMotion phase and state handler.

**Other timers in the wizard:**
- `imuConnectTimer` (2 s, the paced BLE queue, l.1371). It lives inside the IMUs panel's Column. `Main` reaches into it through `releaseDevices()`.
- `learnTimer` (2 s, Ball step "Learning…", l.1248).

**Other cross-object couplings:**
- `liveWrist.active` is driven by an unconditional `Binding` in the Check panel (l.1777).
- Ball ROI defaults are written when the Ball Loader loads (l.1203).
- Camera and IMU session enablement is re-seeded in `onVisibleChanged` (l.571).

### 2.5 What happens to the chain on each exit route

| Route | Wizard | `calibFlow` | Device / guide |
|---|---|---|---|
| Continue (done) from Calibrate | → Check | `G` stays true; no `reset` | none in flight (done) |
| **Skip** from Calibrate mid-run | → Check | `G` true; one-shots and the animation chain continue; repeaters pause (`V` false) | HM: phase handlers keep sending markers; the routine may complete off-page |
| **Back** from Calibrate mid-run | → IMUs | as Skip | as Skip |
| Back from Check → Calibrate | → Calibrate | `imu.calibrated` is false (F1) → `begin()` → `_reset()` clears A/B/C | Witmotion calibration wiped; HM routine aborted and re-run |
| Recalibrate link on Check | sets `currentStep` directly; forces `begin()` | fresh run | intended |
| Rail → Settings mid-run | hidden | as Skip; on return, `onVisibleChanged` re-seeds goals and enablement (F4) | as Skip |
| Header › on Cameras/IMUs | `goNext("done")` without connecting (F5) | — | — |
| Start session | rail screen | `G` stays true for the life of the app | the hidden flow still reacts to every HM phase change (F3) |
| Toolbar IMU panel → Calibrate (during a session) | — | the panel's flow runs; the **wizard's flow also reacts** to the device's phase changes (F3) | duplicate markers suspected |

---

## 3. Findings

Each finding was established by reading, not running. Stage 0 turns each one into a test (the ID in the last column). Severity: **H**: wrong data or a lost calibration. **M**: wrong state the user can see. **L**: maintenance hazard.

| ID | Finding | Evidence | Sev | Test |
|---|---|---|---|---|
| **F1** | **Re-entry restore is dead, so going back to Calibrate wipes a good calibration.** The wizard restores only when `leadImu.calibrated`. For a Witmotion that property is set only by `ImuInstance::setCalibration()`, which nothing calls. The flow uses `setNominalCalibration` / `refineMountAboutLongAxis`, which set `anatCalibrated`. For a HackMotion, `HmUnit::calibrated()` returns a constant `false`. So every entry runs `begin()`, and `_reset()` calls `clearCalibration()` + `clearFunctionalCalibration()` on every segment. The fix in `060d5723` (skip + back falsely complete) closed one hole by making the restore unreachable. | `ScreenSessionWizard.qml:237–241`; `ImuCalibrationFlow.qml:112–116, 790–823`; `imu_instance.cpp:517–550`; `hm_instance.h:177` | H | CW7, CH10 |
| **F2** | **There is no leave contract.** Nothing stops the chain when the step is left. `_autoStartGate` is set by `begin()` and cleared only by `reset()`, which the wizard never calls. One-shot timers, the animation chain and the HM handlers outlive the step (§2.5). | `ImuCalibrationFlow.qml:88–95, 994–1028, 1031–1131, 1155–1234` | H | CW6, CW9, CH8 |
| **F3** | **Hidden flows drive the device (suspected).** `onHmPhaseChanged → _hmOnPhase()` and the device `Connections` are not gated by visibility or by the gate. Both instances react to every phase change, and each starts its own settle timer that ends in `confirmHorizontal()` / `confirmRaise()`: the wizard's flow (alive for the app's life) and the toolbar panel's (instantiated with its Popup). The library's answer to a duplicate marker is unknown (refusal → both flows `_hmStop`, or ignored). | `ImuCalibrationFlow.qml:396–398, 579–648, 875–898`; `PpSessionToolbar.qml:717`; `PpImuPanel.qml:244` | H | CH9 |
| **F4** | **Settings round trip loses choices.** `onVisibleChanged` re-seeds `selectedGoals` from saved settings and resets per-session camera/IMU enablement from the global exclusions every time the wizard becomes visible, including the return from Settings. Goal edits and the per-session toggles made in this visit are lost. | `ScreenSessionWizard.qml:571–588` | M | N11, N12 |
| **F5** | **The header › and the footer disagree.** › calls `goNext("done")`. That is gated only for Calibrate and Ball, so on Cameras or IMUs it marks the step done without connecting. The footer would have offered Connect. | `Main.qml:631–650`; `ScreenSessionWizard.qml:154–164, 2150–2209` | M | N8 |
| F6 | **`liveWrist.active` is forced false app-wide.** The Check panel's `Binding` has no `when`, and the wizard never dies, so `liveWrist.active` is false whenever the wizard is not on Check. There is no other user today. The trunk check view or the toolbar would hit it. | `ScreenSessionWizard.qml:1777–1781` | M | CK1 |
| F7 | **Every panel is alive all the time.** BodyVizView (19 GLBs + View3D) and ArmVizView (View3D) exist from app start, whatever the session uses, alongside PpImuPanel's own BodyVizView. The `fullyLoaded` start gate exists because of the load stall. Memory note `view3d-disappearance-watch` asks for fewer live View3Ds. | `ScreenSessionWizard.qml:1651, 1711`; `ImuCalibrationFlow.qml:1251, 1394` | M | L1, L2 |
| F8 | **Step knowledge lives in about 12 places.** The two literal state arrays disagree: `reopenAtTriangulate` writes 7 entries for an 8-step wizard. | l.44, 57–69, 119, 154–175, 489–565, 683, 2066–2116, 2129–2137, 2150–2190, 1947–2015 | L | N17, lint W3 |
| F9 | **Calibrate is gated on the session type** (`hasCalibrateStep`). The trunk pages need it gated on the assigned sensors (memory note `analysis-is-session-agnostic`). | l.82 | L | N1, N18 |
| F10 | **The paced IMU connect queue lives in a page**, and is copied in `PpImuPanel`. `Main` reaches into it to cancel. | l.1300–1389; `PpImuPanel.qml:99`; `Main.qml:643` | L | D3, D4 |
| F11 | **Dead and stub code:** `_prevStep` (written, never read), `imuDetail` (unused), `reopenAtTriangulate` (never called, wrong length), and the `_todo_*` stubs, which put every non-fixed two-camera setup on "Not quite ready". | l.42, 215, 342, 347 | L | R1 |
| F12 | **The slot → device → unit lookup is re-derived about 10 times**, each with its own hand-placed reactive-dependency reads. One missing read makes a binding go stale (memory note `qml-dead-statement-bindings`). | l.446–481, 1312–1344, 1498–1537; `ImuCalibrationFlow.qml:188–235` | L | lint W4 |
| F13 | **`_bvv` is assigned in `Component.onCompleted` and never cleared.** A layout switch leaves the timers pointing at a destroyed view. | `ImuCalibrationFlow.qml:91, 1256, 1401` | L | CW11 |
| F14 | **The per-session-type tables live inside the view** (`sessionTypes`, `imuRequirements`, `goalDefsByType`). `Main` reaches into the wizard for `sessionTypes[type].railIndex`. | l.247–335; `Main.qml:761` | L | R1 |

Four memory regressions this design has to make structurally impossible (memory note `do-not-touch-the-session-wizard`):
- a renamed entry point with stale call sites (`begin()` never fired);
- a `Q_INVOKABLE` in a `private:` section;
- a *unit* asked for `imuConnected`;
- toggling `visible` on the `BodyVizView` that drives the chain.

They map to lint rules W1/W5, test L4 (no QML warnings) and rule R3.

---

## 4. Design

### 4.1 Rules

These are the contract. Each one has a test or a lint rule.

- **R1. One registry.** A step exists in one descriptor and one page file. Nothing else names it.
- **R2. A page exists only while it is current.** Pages are Loader-created. State that must outlive a page lives in the session draft (§4.6) or in a C++ manager.
- **R3. One activity flag.** Every Timer `running`, every `Connections.enabled` and every animation in a page or routine is gated on `active`. Never on `visible`, never on `currentStep`.
- **R4. Leave is explicit.** The flow calls `page.leave(reason)` before it destroys the page. Routines stop their timers and abort device work there.
- **R5. Navigation is queued.** Pages *request* navigation. The flow applies it on the next event-loop turn, so a page is never destroyed inside its own handler.
- **R6. Hardware decides applicability.** A step applies because of what is assigned and connected. The session type picks goals and requirements only.
- **R7. Only an active routine may call a device.** Device signal handlers live in the routine, and the routine exists only while its page is active (R2), or while the toolbar panel is in calibrate mode.
- **R8. Re-entry reads, never re-runs.** A calibrate page restores from the recorded outcome, checked against the device's live validity. It re-runs only on an explicit Recalibrate.
- **R9. The guide is an interface.** Routines talk to `CalibrationGuide`, never to `BodyVizView` internals.
- **R10. Every transition is one app-log line.** Format: `[Setup] enter calibrateArm dir=forward`, `[Calib] witmotion stage=phase1 captured`, and so on (memory note `diagnostics-go-to-the-app-log`).
- **R11. A QML warning is a test failure.**

### 4.2 Architecture

```
Main.qml ── ScreenSessionWizard.qml  (the SHELL; the file name is kept so Main changes little)
              ├─ SetupFlow            non-visual: plan, current, states, queue, lifecycle, trace
              │    └─ setup_flow.mjs  pure functions: plan(), next(), back(), goTo(), issues()
              ├─ SetupSteps           the registry: [StepDescriptor …]
              ├─ SetupContext         hardware view: cameras, slots → devices/units, roles, ball
              ├─ SetupDraft           this visit: type, goals, enable overrides, step states, outcomes
              ├─ PpFlowIndicator      the progress indicator (§4.9)
              ├─ Loader { source: flow.current.page }   ← exactly one page alive
              └─ footer               driven by the current page's contract
pages/   GoalsPage  CamerasPage  TriangulatePage  BallPage  ImusPage
         CalibrateArmPage  CheckArmPage  [CalibrateTrunkPage  CheckTrunkPage]  ReadyPage
calibration/
         WitmotionArmRoutine.qml   non-visual: the Witmotion state machine + its 6 timers
         HackMotionArmRoutine.qml  non-visual: the HackMotion state machine + its 5 timers + device Connections
         CalibrationGuide.qml      interface + BodyVizView adapter
         ArmCalibrationStatus.qml  the status column (full | compact), presentational only
         ImuCalibrationFlow.qml    thin composition, SAME public API, for PpImuPanel
```

**Language choice: QML/JS, not C++, for the flow engine.**
- The predicates read live QML-side hardware bindings.
- The pages are QML.
- The past failures were runtime-resolution failures that a C++ engine would not have prevented, because QML still calls into it by name.

The decisions sit in a pure `.mjs` module, which tests call directly. C++ changes are limited to three seams (§4.11).

### 4.3 The page contract — `WizardPage.qml`

Every page's root is a `WizardPage`.

```qml
// Inputs (set by the shell)
property bool   active        // R3: true from enter() until leave()
property var    flow          // to request navigation (queued, R5)
property var    ctx           // SetupContext
property var    draft         // SetupDraft
property string stepLabel     // "STEP 4 OF 8 · MOTION SENSORS", from the flow

// Outputs (bound by the page)
property bool   canContinue: true          // footer Continue enabled; header › enabled
property bool   canSkip:     false         // footer Skip shown
property var    primary:     null          // { label, run(), busy } overrides Continue (Connect…)
property string hint:        ""            // footer hint
property string hintTone:    "neutral"     // "good" | "warn" | "neutral"
property bool   fullBleed:   false         // viz pages use the full viewport width

// Lifecycle (called by the flow, never by the page itself)
function enter(direction) {}   // "forward" | "back" | "jump" | "resume"
function leave(reason)    {}   // "forward" | "skip" | "back" | "jump" | "suspend" | "exit"
```

`active` is set before `enter()` and cleared before `leave()` returns. Timers bound to it stop in the same turn.

### 4.4 The step descriptor and the registry

```qml
StepDescriptor {
    key:      "calibrateArm"
    title:    qsTr("Calibrate")            // indicator label
    group:    "sensors"                    // indicator group
    page:     "pages/CalibrateArmPage.qml"
    applies:  function(ctx, draft) { return ctx.armRoutine !== "" }     // R6
    gate:     function(ctx, draft) { return draft.outcome("arm").done } // Continue allowed
    issues:   function(ctx, draft) { … }   // [{ text, panel }] for Ready; [] when fine
    summary:  function(ctx, draft) { … }   // { label, value, good } for the Ready table
}
```

`gate`, `issues` and `summary` are evaluated **without the page alive**. Ready aggregates them over the plan, and the indicator can show "needs attention" on a step that is not current. The page's own `canContinue` is ANDed with `gate` for the steps that have live in-page conditions (Ball).

**The registry today, and with the trunk pages:**

| key | group | applies | gate |
|---|---|---|---|
| `goals` | session | always | — |
| `cameras` | cameras | always | — (Connect is the primary until nothing is left to connect) |
| `triangulate` | cameras | a selected face-on and a selected DTL | — |
| `ball` | cameras | always | `ctx.ballPresent` |
| `imus` | sensors | always | — |
| `calibrateArm` | sensors | an arm routine is resolvable from slot A (`ctx.armRoutine` = `"witmotion"` / `"hackmotion"`) **or** the session's requirements list an arm slot | `draft.outcome("arm").done` |
| `checkArm` | sensors | as `calibrateArm` | — |
| `calibrateTrunk` *(new)* | sensors | a pelvis or thorax role is assigned | `draft.outcome("trunk").done` |
| `checkTrunk` *(new)* | sensors | as `calibrateTrunk` | — |
| `ready` | ready | always | — |

`calibrateArm` keeps today's W-none behaviour: a Wrist session with no sensor still shows the step with the "no sensor assigned" warning. The `or` clause preserves that. Mark can drop it (D6).

### 4.5 The flow engine — `SetupFlow` + `setup_flow.mjs`

**State:** `plan` (the ordered applicable keys), `current` (a key), `states` (key → `pending | done | skipped`), and a pending-navigation slot.

**Plan.** `plan = registry.filter(applies)`, recomputed reactively. **The current step is pinned.** If it stops applying while current, it stays current, shows "No longer needed — Continue" as its hint, and is dropped from the plan when left. Steps never reorder. A newly applicable step is inserted in registry order.

**Operations.** All of them are queued (R5):

| Operation | Guard | Effect |
|---|---|---|
| `next("done")` | `page.canContinue && gate` | state[current] = done; leave("forward"); current = next in plan; enter("forward") |
| `next("skipped")` | `page.canSkip` | state = skipped; leave("skip"); … enter("forward") |
| `back()` | current ≠ first | state[current] = pending; leave("back"); current = previous; enter("back") |
| `goTo(key)` | key earlier in plan **and** state[key] ∈ {done, skipped} | leave("jump"); current = key; enter("jump"). States in between are kept. |
| `suspend()` / `resume()` | the wizard is hidden or shown (Settings) | leave("suspend") / enter("resume"). The draft is untouched (fixes F4). |
| `open(type)` | from Home | a new draft; states pending; current = first. **The only place settings are seeded.** |
| `exit("cancel" \| "start")` | — | leave("exit"); cancel → `releaseDevices()` (camera stop/disconnect, `imuManager.cancelPacedConnect()` + `disconnectAll()`); start → keep devices |

**The header ‹ and ›** call `back()` and `next("done")`. › is enabled exactly when the footer's Continue would advance. It never runs a page's `primary` (Connect), so it can never mark a step done without connecting (fixes F5, D2).

**Trace (R10).** Every operation logs one line through `appLog` (§4.11): the operation, from, to, the reason and the plan.

### 4.6 The session draft — `SetupDraft`

The state of one wizard visit, created by `open(type)` and dropped at exit:
- `sessionType`, `goals`, `goalsInteracted`;
- `cameraEnabled` / `imuEnabled` overrides (written through to the managers as today, but **seeded once**);
- `states`;
- `outcomes`: `{ arm: { done, routine, at, devices }, trunk: {…} }`.

**An outcome counts as valid only while its devices' live validity holds:**
- Witmotion: `fullyCalibrated` on each recorded segment.
- HackMotion: `calibrationState === CALIBRATED`.

A link drop clears it. This is what R8's restore reads, replacing the dead `calibrated` test (fixes F1).

### 4.7 Calibration: routines, guide and hosts

**Split `ImuCalibrationFlow.qml` along the line it already draws for itself.** It has two state machines branched at the top. Each becomes a non-visual routine with the same inputs and outputs:

```qml
// WitmotionArmRoutine / HackMotionArmRoutine (and later TrunkRoutine)
property bool   active                 // R3: ANDed into every Timer.running and Connections.enabled
property var    guide                  // CalibrationGuide (R9)
property var    segments               // { a, b, c } VIZ units; { device } for HM — injected, not looked up
property var    pace                   // durations; pace.scale = 1 in the app, ~0.05 in tests
readonly property string stage         // "idle" | "intro" | "phase1" | … | "done" | "failed"
readonly property bool   done, failed
readonly property string message, messageKind
function begin()                       // fresh run (clears, as today)
function stop(reason)                  // timers off; HM: abort if a routine is live (wording as today)
function restore()                     // show complete from live device state; never re-runs
signal completed()
```

What changes inside them is mechanical:
- `flow.visible && flow._autoStartGate` becomes `active`.
- `flow._bvv.*` becomes `guide.*`.
- `imuManager.instanceForSlot(...)` becomes the injected `segments`.
- The one-shot timers get a `running: … && active` guard or are stopped in `stop()`.

**The quaternion maths, the mount gate, the HackMotion verdict logic and every message string move verbatim.** The tests in §7 are what show nothing else moved.

**`CalibrationGuide` (R9):**

```qml
readonly property bool ready              // the BodyVizView adapter: bvv.fullyLoaded
function pose(armQ, foreQ)                // = resetArmAnimation(armQ, foreQ) without animating
function animate(armQ, foreQ, ms)         // starts the slerp; emits finished() at the end
function cancel()                         // stops any animation; no finished()
signal finished()
// HackMotion pose constants, read off the avatar today:
readonly property quaternion hmUpperArm, hmForePose0, hmForePose1
readonly property real hmRaiseDeg
property bool useGuideCamera
```

The real adapter wraps one `BodyVizView` and owns `_animStage`-free plumbing: the routine keeps its own stage, and the guide only reports "finished". `FakeGuide` (tests) is ready immediately and finishes when the test says so, or after `ms × scale`.

**Hosts:**
- **Wizard:** `CalibrateArmPage` creates the guide view and one routine. It picks the routine by `ctx.armRoutine` in a Loader, so only one routine exists (fixes F3 by R7 + R2). On `enter`: `restore()` if `draft.outcome("arm")` is valid, else `begin()`. On `leave`: `stop(reason)`, and the outcome is recorded in the draft on `completed()`.
- **Toolbar:** `ImuCalibrationFlow.qml` keeps its public API (`layoutMode`, `calibrationDone`, `begin()`, `reset()`, `showCompleted()`, `completed`, `cancelled`) so `PpImuPanel` needs one change: bind `active: root.mode === "calibrate"` in place of the `onVisibleChanged` auto-start. Inside, it holds the routine in a Loader that is active only while `active`, so the hidden toolbar copy no longer reacts to the device (F3).

`CheckArmPage` owns the `ArmVizView`. It sets `liveWrist.active = true` on enter and `false` on leave, with no permanent `Binding` (fixes F6).

### 4.8 The hardware context — `SetupContext`

One object resolves everything the pages ask about hardware. Its reactive dependencies are written **once** (fixes F12):
- `faceOn[]`, `dtl[]`, `others[]`; `camsAllConnected`; `ballInstance`; `ballPresent`;
- `slot(s)` → `{ id, device, unit, present, enabled, connected, vendor, unitLabel }` for `A`, `B`, `C` today, then **roles** when `trunk_imu_design.md` §6.7 lands (`pelvis`, `thorax`, `leadForearm`, …);
- `armRoutine` (`"witmotion"` | `"hackmotion"` | `""`), `trunkRoles[]`;
- `requirements` (the per-type table moved out of the view: `sessionTypes`, `imuRequirements`, `goalDefsByType`, F14).

**In tests the context is built over fakes. In the app it is built over the real managers.** This is the single seam that makes the wizard testable.

### 4.9 The progress indicator — `PpFlowIndicator`

It replaces the tab strip (`ScreenSessionWizard.qml:669–778`). It is generic: the model is `[{ key, label, group, state, current, attention }]`, so the toolbar calibration (and the trunk ceremony's S1–S3) can reuse it at a smaller size.

```
  SESSION      CAMERAS                      SENSORS                              READY
  ●───────────●──────────●──────────◉━━━━━━━○──────────○──────────○──────────○
  Goals ✓     Cameras ✓  Ball ⚠      IMUs       Calibrate   Check       Ready
                                     ▲ step 4 of 7
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━░░░░░░░░░░░░░░░░░░░░░░░░░░  3 of 7 done
```

- **States:**
  - done: ✓, colorGood;
  - skipped: ⚠, colorWarn;
  - current: filled accent;
  - pending: outline;
  - **attention**: an amber ring when a visited step's `issues()` is non-empty, for example a camera that disconnected after Cameras was done.
- **Groups** label their run of steps. A thin bar under the strip shows the fraction of the plan complete.
- **Steps that appear or disappear** (Triangulate, the trunk pages) fade and grow in place. Steps never reorder. The numbering always matches the page eyebrow, because both come from `flow.plan`.
- **Click a done or skipped step** to jump back (`flow.goTo`). Future steps are not clickable (D5). Hovering shows the step's hint or first issue.
- **Narrow widths:** below a threshold, groups other than the current one collapse to a single pip with a count ("Cameras 3/3").
- **Accessibility:** each pip is `Accessible.name: "<label>, <state>"`.

### 4.10 Adding a page: the recipe (trunk as the example)

1. Write `pages/CalibrateTrunkPage.qml`: a `WizardPage` hosting `TrunkRoutine` + guide. Write `pages/CheckTrunkPage.qml` the same way.
2. Add two `StepDescriptor`s to `SetupSteps` (`applies: ctx.trunkRoles.length > 0`, `gate`, `issues`, `summary`).
3. Add their test cases to `tst_wizard_trunk.qml` using the fakes.

Nothing else changes: no shell, footer, Ready, indicator or Main edits. Test N17 proves this with a dummy step.

### 4.11 C++ seams (outside the protected files)

| Seam | Why |
|---|---|
| `ImuManager::connectPaced(QStringList ids, int gapMs)`, `cancelPacedConnect()`, `Q_PROPERTY bool pacedConnectActive` | Moves the BLE queue out of a page (R2, F10). `PpImuPanel` adopts it later. |
| `ImuInstance` `Q_PROPERTY bool fullyCalibrated NOTIFY anatCalibratedChanged` (already a method) | The R8 validity check (F1). A matching read on `HmInstance` is its `calibrationState`. |
| `appLog`: a context object with `Q_INVOKABLE void info(QString tag, QString text)` / `warn(...)` → `ppInfo` / `ppWarn` | R10. QML's `console.*` goes to the Qt handler, which is not the app log (memory note `how-logging-works-in-pps`). Check first that no equivalent exists. |

Every new `Q_INVOKABLE` goes in a `public:` section, and lint rule W5 checks it.

---

## 5. What the refactor keeps exactly

These behaviours must survive byte-for-byte or test-for-test:
- Every user-visible string, colour and layout on every page, except the tab strip (replaced) and the defect fixes Mark approves.
- The Witmotion maths: stillness 15 °/s, 2,000 ms holds, slerp averaging, `setNominalCalibration` on A/B/C at arm-down, the φ refinement, and the mount gate (dev ≤ 15°, grav ≤ 25°).
- The HackMotion choreography and every ⚠ comment on it: no wall-clock fallback on the raise; the travel gate (15°) before `a2 01`; the verdict only from `calibrationState` at hmStep ≥ 4; the stale-verdict guard at steps 0–2; the presence-wait bound; the abort wording at VERIFYING.
- The guide pacing durations (3000 / 3000 / 2000 / 2000 / 800 / 1500 / 2000; HM 1500 / 2000 / 3000 / 500 / 1500 / 1500 / 6000).
- The `fullyLoaded` start gate.
- The ball step's ROI default and Learn behaviour.
- The camera and IMU enablement semantics (manager-owned, shared with the toolbar).
- `sessionStartRequested(type, goals)` with the first goal as the default, and `releaseDevices()` on Cancel / ‹-exit.

---

## 6. Behaviour changes proposed (each needs Mark's yes — §10)

| Change | Fixes |
|---|---|
| Leaving Calibrate mid-run stops the routine (HM: aborts). Returning restores if complete, else starts fresh. | F2, F3 |
| Back from Check to Calibrate shows "Calibration complete" with Recalibrate, instead of wiping and re-running. | F1 |
| A Settings round trip keeps goals and per-session toggles. | F4 |
| The header › is Continue, never Connect. It is disabled when Continue is. | F5 |
| A progress indicator with jump-back replaces the tab strip. | request |
| Calibrate/Check applicability comes from the sensors. | F9, trunk |

---

## 7. Tests

### 7.1 Harnesses

| ID | Harness | Runs | What it is for |
|---|---|---|---|
| **H1** | `wizard_ui_test`: a QuickTest binary declaring the app's QML module (as `qml_ui_test` does) plus test-only C++ enum stand-ins and QML fakes | offscreen (CI, both platforms) **and** windowed (`--platform cocoa` / `windows`) | flow, gating, lifecycle, routines, readiness, indicator. Cases tagged `needsRender` skip offscreen **with a printed reason**, never a silent pass. |
| **H2** | `--probe-qml` scripts under `src/Gui/tests/probes/wizard_*.qml`, on the real app, windowed, on a visible Space | Mac (and the studio PC) | the real `BodyVizView` guide renders and completes the chain; a 6-minute soak; View3D count |
| **H3** | hardware checklist (§7.6) | Mark, with the sensors | the real devices, the real BLE timing, the real HackMotion library |
| **H4** | `wizard_lint_test` (C++, reads source, like `qml_reactivity_test`) | CI | rules that are cheaper to check statically |

**Why a separate binary from `qml_ui_test`:**
- the fakes have to be injected under the context-property names the wizard reads (`cameraManager`, `imuManager`, `athleteController`, `liveWrist`, `sessionController`);
- the app-only enums (`CameraInstance`, `SessionController`) have to be provided.

Neither should leak into the other suites.

**Injection:**
- Before the refactor, the setup object exposes `Q_INVOKABLE QObject *createWithContext(url, QVariantMap props)`. It creates a child `QQmlContext` with the fakes as context properties and instantiates the component in it. The current wizard resolves its unqualified names through that context unchanged.
- After the refactor, pages take `ctx` and `draft` explicitly, and the same helper still serves the shell-level tests.

**Enum stand-ins.** Test-only headers declare `QML_NAMED_ELEMENT(CameraInstance)` / `(SessionController)` with the same enum names and values. Lint W6 checks the values against the real headers, so the stand-ins cannot drift.

### 7.2 Fakes

| Fake | Controls the test needs |
|---|---|
| `FakeCameraManager` | `cameraList` entries (key, perspective, selected, sessionEnabled, alias), `instances` (with `ballPresent`, `roi`), `anyConnecting`, `isRecording`, `anySelected`. It records calls to `setSelected`, `startAll`, `stopCapture`, `disconnectAll`, `setSessionCameraEnabled`, `relearnBallBaseline` and `setBallRoi`. |
| `FakeImuManager` | `imuDeviceList`, `instances`, placement → `deviceIdForSlot` / `deviceForSlot` / `instanceForSlot` / `unitLabelForSlot` / `isHackMotionDevice`, `imuScanActive`, `anyConnecting`, `connectPaced` (after Stage 1). It records `setSelected` with timestamps, plus `disconnectAll`, `rescanImu` and `setSessionImuEnabled`. |
| `FakeImuInstance` (Witmotion) | `quatW/X/Y/Z`, `angularVelocityDps`, `imuConnected`, `anatQuat`, `anatCalibrated`, `mountDeviationDeg`, `mountGravityErrorDeg`, `stateLabel`. It records `clearCalibration`, `clearFunctionalCalibration`, `setNominalCalibration(q)` and `refineMountAboutLongAxis(ref, φ)`. A script sets the mount result. |
| `FakeHmDevice` + 2 `FakeHmUnit` | `calibrationPhase`, `calibrationState`, `calibrationAbortReason`, `presence*`, `poseSpreadMaxDeg`, `relativeAngleDeg`, `streaming`, `imuConnected`, `calibrationActive`; signals `calibrationCallRefused`, `calibrationInvalidated`, `calibrationStateChanged`. It records `beginCalibration`, `confirmHorizontal`, `confirmRaise`, `confirmReferencePose` and `abortCalibration`. An **auto-script mode** steps the phases the way the library does when it receives each call. A **strict mode** refuses a duplicate marker with `INVALID_STATE`, which is how CH9 detects F3. |
| `FakeGuide` | `ready` (settable), `animate()` records the target and emits `finished()` on `guide.finish()` or after `ms × scale`; `stall = true` never finishes. |
| `FakeAthlete`, `FakeLiveWrist`, `FakeSessionController` | handedness; `active` with a write log; `activeClub`. |

**Every test also installs a message handler. Any QML warning or error that the test did not explicitly expect fails the test (R11, L4).** This one rule catches the class of failure that has cost the most: a binding failing at runtime with no build error.

### 7.3 The catalogue

Columns:
- **Base**: what the case is expected to do against **today's** code. `P` = pass. `X` = expected failure, with the finding it encodes. `—` = not applicable before the refactor (new API).
- **After**: what it must do after the refactor. `P` everywhere unless Mark declines the behaviour change.

The "Base" column is the before-test. Stage 0 records the actual results in §11.

**Navigation and flow (N)**

| ID | Scenario | Expected | Base |
|---|---|---|---|
| N1 | Open Wrist, 1 face-on, Witmotion A+B | Plan Goals, Cameras, Ball, IMUs, Calibrate, Check, Ready. Current Goals. All pending. Eyebrow "STEP 1 OF 7". | P |
| N2 | Select a DTL camera while on Cameras | Triangulate inserted after Cameras. Numbering 1..8 everywhere. Deselect → removed. | P |
| N3 | Continue through every ungated step | Each is marked done and the next applicable step is entered. | P |
| N4 | Back from each step | The current step becomes pending and the previous applicable step is entered. Back on the first step → exit with `releaseDevices` (Main). | P |
| N5 | Skip on Cameras, Triangulate, Ball, IMUs, Calibrate | Marked skipped (⚠). Ready lists the matching consequence. | P |
| N6 | Ball gate | Continue does nothing until `ballPresent`. Skip works. | P |
| N7 | Calibrate gate | Continue does nothing until the routine is done. Skip works. | P |
| N8 | Header › on Cameras with an unconnected enabled camera | Disabled, or no advance. Cameras not marked done. | **X F5** |
| N9 | Cancel at each step | `stopCapture`, `disconnectAll` (cams + IMUs), queue cancelled, `activeClub` cleared, home. | P |
| N10 | Start on Ready, no goals chosen | `sessionStartRequested(type, [firstGoalKey])`. With goals: those goals. | P |
| N11 | Pick goals → rail Settings → back | Goals unchanged. | **X F4** |
| N12 | Disable a camera and an IMU in the wizard → Settings → back | Both still disabled for this session. | **X F4** |
| N13 | `open(type)` after a part-finished visit | Everything reset: states, goals, outcomes, queue, ball learning, recalibrate flag. | P (partly; record) |
| N14 | On Triangulate, deselect DTL | Triangulate stays current with the "no longer needed" hint. Continue → Ball. Triangulate gone from the plan. | X (record actual) |
| N15 | "Recalibrate" link on Check | → Calibrate. Calibrate and Check pending. A fresh run begins. | P |
| N16 | Click a done step in the indicator | Jumps there with leave("jump"). Future pips are inert. | — |
| N17 | **Extensibility:** a test registers a dummy descriptor + page | It appears in the plan, the indicator, the numbering and Ready's issues. Navigation in and out works. No other file is edited. | — |
| N18 | Wrist session with only trunk roles assigned (after §6.7) | Arm steps absent, trunk steps present. | — |
| N19 | Navigation requested twice in one turn (double-click Continue) | Exactly one advance. | X (record) |

**Devices (D)**

| ID | Scenario | Expected | Base |
|---|---|---|---|
| D1 | Cameras: 2 enabled, unselected | Primary reads "Connect". Click → `setSelected` for each enabled one, then `startAll` once. Primary becomes "Continue →". | P |
| D2 | Toggle a camera off | Row dimmed "DISABLED — WON'T CONNECT". Not selected by Connect. `setSessionCameraEnabled(key,false)`. | P |
| D3 | IMUs: A and B on two Witmotions, C optional on a third | `setSelected` at t ≈ 0, 2, 4 s (± one tick). "Connecting…" during the run. The traveling-light frame runs. | P |
| D4 | wG3 in A+B | Exactly **one** `setSelected` for the device. Both rows read the same device with "Lower arm" / "Palm". | P |
| D5 | Back from IMUs mid-queue | The queue still completes (after: in `ImuManager`). Cancel mid-queue stops it. | P |
| D6 | Slot assigned, sensor not in the device list | Row fail text. Ready: "assigned but the sensor was not found. Power it on and Scan." | P |
| D7 | Scan button | Mirrors `imuScanActive`. Click → `rescanImu()`. | P |

**Calibration — Witmotion (CW)** (FakeGuide, 3 FakeImuInstance, `pace.scale` 0.05 after Stage 2; real time before)

| ID | Scenario | Expected | Base |
|---|---|---|---|
| CW1 | Happy path | Nothing starts until `guide.ready`. Guide targets in order: tPose, down, tPose. Phase 1 → 2 s still → `setNominalCalibration` on A, B, C with the averaged quats → raise → phase 2 → 2 s still → `refineMountAboutLongAxis` per segment → done. `completed` once, ting once. | P (`needsRender` before) |
| CW2 | Motion > 15 °/s during each hold | The hold resets. No capture until 2 s of stillness. | P |
| CW3 | Mount fails on the hand | `mountFailed`. The message names "hand". Continue blocked. Recalibrate → `clearCalibration` **and** `clearFunctionalCalibration` on A, B, C, then a fresh run. | P |
| CW4 | Lead disconnects in phase 1 or 2 | `calibrationFailed`. Badge "Failed". | P |
| CW5 | `guide.stall` during each animation | The chain does not advance. No wall-clock bypass. | P |
| CW6 | **Back** at each stage (intro, phase-1 hold, raise animation, phase 2) | After leave: no timer fires, no device method is called, `guide.cancel()` is called, the stage is "idle". | **X F2** |
| CW7 | Complete → Check → **Back** to Calibrate | Shows complete. No `clear*` calls. Continue enabled. | **X F1** |
| CW8 | Skip Calibrate → Check → Back | A fresh run begins (nothing recorded). | P |
| CW9 | **Skip** mid-run | As CW6. | **X F2** |
| CW10 | Rail → Settings mid-run → back | The routine stopped on suspend and restarts on resume. No stray capture. | **X F2** |
| CW11 | The toolbar host switches `layoutMode` | The guide reference follows the new view. No calls on a destroyed view. | X F13 (record) |
| CW12 | A sensor reassigned so slot A changes vendor mid-run | Routine swapped. No HackMotion "done" survives into Witmotion, or the reverse. | P |

**Calibration — HackMotion (CH)** (FakeHmDevice in auto-script mode unless stated)

| ID | Scenario | Expected | Base |
|---|---|---|---|
| CH1 | Happy path | Calls in order: `beginCalibration`, `confirmHorizontal`, `confirmRaise`, `confirmReferencePose`, each exactly once, at the paced offsets. The guide plays the raise (3000) and the return (1500). Done only on `calibrationState === CALIBRATED`. | P |
| CH2 | Forearm travel 8° | `abortCalibration`. A warn naming 8° and 30°. **No** `confirmRaise`. | P |
| CH3 | Each refusal (NO_STREAM, BUSY, LINK_DOWN, INVALID_STATE, other) | The matching message and kind. Stopped at hmStep 9. | P |
| CH4 | Each abort reason | The matching text. RAISE_TOO_SLOW is a warn, the others errors. | P |
| CH5 | `presenceNotMeasured`; and nothing ever arrives | The "too few readings" error; and the timeout error after 6 s. | P |
| CH6 | Link drop at each step and after done | Invalidated, `done` false, message. The outcome is cleared from the draft. | P |
| CH7 | The previous attempt's COMPLETE state lingers when a new run begins | No verdict read before hmStep 3. | P |
| CH8 | Back or Skip at each step | `abortCalibration` exactly once if active. No further device calls. The VERIFYING wording says "declined", not "undone". | **X F2** |
| CH9 | **Two hosts alive:** the wizard on Calibrate plus a toolbar `PpImuPanel` instantiated, strict device | Each marker is sent exactly once. No refusal. | **X F3** (suspected) |
| CH10 | Complete → Check → Back | Shows complete from `calibrationState`. No `beginCalibration`. | **X F1** |
| CH11 | Not streaming / not connected at begin | The matching error. No `beginCalibration`. | P |

**Check page (CK)**

| ID | Scenario | Expected | Base |
|---|---|---|---|
| CK1 | Enter, then leave Check | `liveWrist.active` is true while on Check and false after. Setting it true from elsewhere while the wizard is on another step is **not** overridden. | **X F6** |
| CK2 | Not on Check | No `ArmVizView` exists. | **X F7** |

**Ready (R)**

| ID | Scenario | Expected | Base |
|---|---|---|---|
| R1 | Golden table: 12 hardware/skip combinations → the exact `readinessIssues` list (texts + settings links) | Equal to the baseline captured in Stage 0 (characterisation). | P |
| R2 | Fully ready vs not | "▶ Start session" good-green vs "▶ Start anyway" warn. The heading and notice copy match. | P |
| R3 | Summary rows per combination | Equal to the baseline. | P |

**Lifecycle and resources (L)**

| ID | Scenario | Expected | Base |
|---|---|---|---|
| L1 | Walk the whole flow | Exactly one page object alive at any time. | **X F7** |
| L2 | Walk the whole flow | At most one View3D alive in the wizard (count `View3D` instances under the wizard). | **X F7** |
| L3 | A page calls `flow.next()` and then reads its own property in the same handler | No TypeError: the navigation was queued. | — |
| L4 | Every case above | No unexpected QML warning or error. | record |
| L5 | Start a session, then trigger HM phase changes on the device | The wizard's routine does nothing (it does not exist). | **X F3** |

**Progress indicator (P)** (a component test, in `qml_ui_test`, plus wiring cases in H1)

| ID | Scenario | Expected |
|---|---|---|
| P1 | A model of 7–10 steps in 4 groups | One pip per step in order. Group labels over their runs. Colours per state. |
| P2 | A step inserted or removed | Animated in place. No reorder. Numbering equals the page eyebrow. |
| P3 | Click a done pip / a future pip | `goTo(key)` / nothing. |
| P4 | Window at minimum width | No pip or label escapes the strip (layout containment, as `tst_lm_graphics` does). Non-current groups collapse. |
| P5 | A visited step's issues become non-empty (a camera dropped) | Attention ring on that pip. Hover shows the issue. |

### 7.4 Render probes (H2)

| ID | Probe | Pass |
|---|---|---|
| RS1 | `wizard_guide_witmotion.qml`: the real `BodyVizView` + `WitmotionArmRoutine` with fake sensors held still. Grab frames at each stage boundary. | The chain completes. Every grab has pixel std > a blank control (memory note `verify-ui-by-probing`). The arm is at the expected pose (the grab is compared by eye once, then hashed). |
| RS2 | `wizard_guide_hackmotion.qml`: the same for the HM raise and return | As RS1. The forearm moves, the upper arm does not (the shipped-once defect). |
| RS3 | `wizard_soak.qml`: walk Calibrate ↔ Check every 30 s for 6 minutes, windowed, on a visible Space | Every grab is rendered. View3D count ≤ 1. No blank view (memory note `view3d-disappearance-watch`). |
| RS4 | First entry on the studio PC (Windows load stall) | The chain does not start before `guide.ready`. The intro is seen from its start. |

### 7.5 Lint (H4) — `wizard_lint_test`

| ID | Rule |
|---|---|
| W1 | Under `src/Gui/setup/` and `src/Gui/calibration/`: no `Timer` `running:` expression and no `Connections` `enabled:` mentions `visible` or `currentStep`. Each must mention `active`. |
| W2 | Every `.qml` under `setup/pages/` has `WizardPage` as its root type. |
| W3 | Every descriptor `key` has a page file and appears in at least one `tst_wizard_*.qml`. No page file is unreferenced. |
| W4 | No page reads `imuManager.` or `cameraManager.` directly. Hardware goes through `ctx` (F12). |
| W5 | Every method name the setup and calibration QML calls on `imuManager`, a device, a unit or `appLog` exists as a **public** `Q_INVOKABLE` (or a property) in the corresponding header. This is the private-section trap. |
| W6 | The test enum stand-ins match the real `CameraInstance::Perspective` and `SessionController::Type` enumerators and values. |
| W7 | `pace.scale` is assigned only under `tests/`. |

### 7.6 Hardware checklist (H3) — for Mark

Run with `PINPOINT_LOG_STDERR=1` (memory note `getting-the-pps-app-log`). Every row lists the `[Setup]` / `[Calib]` lines that must appear.

| # | Setup | Do | Expect |
|---|---|---|---|
| 1 | 3 Witmotion, 1 cam | Walk the whole flow, calibrate, check, Start | Done in one pass. The check arm follows. The session starts. One `[Calib] witmotion done`. |
| 2 | same | Calibrate → done → Check → **Back** | "Calibration complete" shown. No re-run. No `clear` line. |
| 3 | same | **Back** during the raise animation; then forward again | `[Calib] stop reason=back`. Fresh run on return. No stray capture line while away. |
| 4 | same | Mid phase 2, rail → Settings, wait 10 s, return | `suspend` / `resume` lines. Restarted. Goals and toggles intact. |
| 5 | same | Strap the hand sensor rotated 90° | Mount fail names "hand". Recalibrate after re-seating passes. |
| 6 | same | Switch off the lead sensor mid-phase 1 | "Failed". Recalibrate after reconnecting works. |
| 7 | wG3 | Full flow | One of each marker in the log. CALIBRATED. Check view OK. |
| 8 | wG3 | Don't raise the forearm | Travel warn. No `confirmRaise` in the log. |
| 9 | wG3 | Start a session, then toolbar IMU → Calibrate | Exactly one of each marker (F3 gone). |
| 10 | wG3 | Calibrate → Check → Back | Complete shown. No `beginCalibration`. |
| 11 | none | Wrist with no sensors and no cameras | Skips work. Ready lists the issues. "Start anyway" starts. |
| 12 | 2 cams | Connect the DTL on Cameras | Triangulate appears in the indicator and the numbering. |
| 13 | any | Cancel at Ball | Cameras released (LEDs off), IMUs disconnected, microphone released. |
| 14 | any | Header ‹ / › through the whole flow | › never connects and never passes a gate. ‹ on Goals exits and releases. |

---

## 8. The refactor plan — stages

Every stage ends with the H1 suite run (offscreen; windowed for the `needsRender` cases) and its result written in §11. A red case outside the stage's intended flips stops the stage. Builds: the test binary only, until Stage 4. The app is built at the end of Stages 2, 4 and 7 (memory note `build-and-test-economy`).

| Stage | Change | Protected files touched | Gate |
|---|---|---|---|
| **0** | `wizard_ui_test` target, fakes, injection helper, enum stand-ins, lint W5/W6, the full catalogue written against **today's** API (an adapter maps the catalogue's `flow.*` calls onto today's `currentStep` / `goNext` / `goBack`). Run it. Confirm or refute F1–F7. | none | The baseline recorded in §11. Every `X` either fails for the stated reason or is re-labelled with what actually happened. Mark sees the table. |
| 1 | C++ seams: `connectPaced`, `fullyCalibrated` property, `appLog`. Unit tests for the first two. | none | Existing suites green. |
| 2 | Split `ImuCalibrationFlow` into `WitmotionArmRoutine`, `HackMotionArmRoutine`, `CalibrationGuide`, `ArmCalibrationStatus`, keeping the public API. Add `active`. `PpImuPanel` binds `active`. | `ImuCalibrationFlow.qml` | CW/CH all at baseline or better. CH9/L5 now pass. RS1/RS2 grabs. The app build runs the toolbar calibration on the probe. |
| 3 | `SetupFlow`, `setup_flow.mjs`, `SetupSteps`, `SetupContext`, `SetupDraft`, `WizardPage`, as new files, tested on a dummy shell (N1–N5, N16, N17, N19, L3). | none | Green. |
| 4 | Move the panels into pages one at a time (Goals → Ready, Calibrate/Check last). `ScreenSessionWizard.qml` becomes the shell. `Main` → `flow.*`. | `ScreenSessionWizard.qml` | After **each** page: the full H1 suite at baseline or better. |
| 5 | `PpFlowIndicator` replaces the tab strip. | `ScreenSessionWizard.qml` | P1–P5. N1/N2 numbering. |
| 6 | The approved behaviour changes (§6, §10). | both | The chosen `X` cases flip to `P`. Nothing else changes. |
| 7 | H2 probes and soak on the Mac, RS4 on the studio PC. Mark runs H3. | — | All rows match. Then commit (Mark's approval), push on his word. |

**Rollback:** each stage is one local, uncommitted set of changes. Stage 0's suite runs against the stash of the previous stage, so any regression can be bisected by stage.

---

## 9. Risks

- **The current wizard may not load headless.** View3D, `RuntimeLoader` and `BayerVideoItem` may misbehave offscreen. Mitigations, in order:
  - fakes keep `instances` empty unless a test needs a frame;
  - `needsRender` cases run windowed;
  - if the module will not instantiate at all offscreen, Stage 0 runs H1 windowed only and says so.

  This is the first thing Stage 0 finds out.
- **Fakes drift from the real managers.** Lint W5 checks the names the QML calls. The H3 checklist is the backstop for behaviour. The fakes model only what the wizard reads.
- **Loader-per-page reloads the 19-GLB avatar on every entry to Calibrate.** On Windows that is a visible stall; the `fullyLoaded` gate already hides it from the chain. If it is too slow, the calibrate and check pages share one guide view held by the shell while either is current (one View3D still). Decide on RS4's numbers.
- **Characterisation encodes today's behaviour, including accidents.** Every `X` is a deliberate decision in §10, not a silent change.
- **The protected-file rule.** Approval is per stage, with the stage's test table shown first. Nothing is edited that a test does not run.

---

## 10. Decisions for Mark

| # | Question | Recommendation |
|---|---|---|
| D1 | Leaving Calibrate mid-run (Back, Skip, Settings): **stop and restart** on return, or pause and resume? | Stop and restart. HackMotion cannot pause (the device times the raise). One rule for both is simpler and testable. |
| D2 | Header ›: **Continue only** (disabled when Continue is), or also performs Connect? | Continue only. Connect stays a deliberate footer press. |
| D3 | Back into a completed Calibrate: **show complete + Recalibrate**, or re-run as now? | Show complete. Today's re-run is the accident behind F1. |
| D4 | Trunk: **separate Calibrate-trunk / Check-trunk steps**, or one Calibrate page sequencing routines? | Separate steps. Each is a routine with its own gate. The combined arm+trunk ceremony (`trunk_imu_design.md` D8) can later be one routine whose descriptor replaces both. |
| D5 | Indicator: **click to jump back**? Forward jumps? | Back yes, forward no in v1. |
| D6 | A Wrist session with no arm sensor: keep showing Calibrate with "no sensor" (today), or drop the step? | Drop it once trunk roles exist. Until then keep today's behaviour. |
| D7 | Triangulate's stub readiness issues (F11): keep as today, or stop reporting an unbuildable check? | Keep as today in this refactor (out of scope), then fix with the camera calibration work. |

---

## 11. Results

*Filled in during the refactor session: the Stage 0 baseline table (case → base result → notes), then one row per stage with the suite result and anything that went red.*
