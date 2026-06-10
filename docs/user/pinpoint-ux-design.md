# Pinpoint — User Experience Design Document

**Version:** 0.1 (draft)
**Status:** In progress — screens designed, implementation not yet started
**Scope:** Desktop-first, full-screen and multi-monitor. Cross-platform: Ubuntu 26.04, macOS Tahoe (Intel), Windows 11.

---

## Table of contents

1. [Product overview](#1-product-overview)
2. [Core mental model](#2-core-mental-model)
3. [Application structure](#3-application-structure)
4. [Navigation](#4-navigation)
5. [First launch and empty state](#5-first-launch-and-empty-state)
6. [Athlete management](#6-athlete-management)
7. [Session start flow](#7-session-start-flow)
8. [Camera calibration](#8-camera-calibration)
9. [Session views](#9-session-views)
10. [Mid-session IMU recalibration](#10-mid-session-imu-recalibration)
11. [Compare mode](#11-compare-mode)
12. [End-of-session summary](#12-end-of-session-summary)
13. [Athlete detail and history](#13-athlete-detail-and-history)
14. [Settings](#14-settings)
15. [Multi-monitor configuration](#15-multi-monitor-configuration)
16. [Qt architecture notes](#16-qt-architecture-notes)
17. [Open questions and future work](#17-open-questions-and-future-work)

---

## 1. Product overview

Pinpoint is an open-source desktop application for full kinematic golf swing analysis. It occupies a distinct niche from commercial launch monitors (which focus on ball-tracking and shot data) by targeting the kinematic chain itself — how the body and club move through the swing — using high-frame-rate cameras, wrist IMUs, and estimated ground reaction force traces.

**Target users** are coaches and serious amateur golfers who want to understand and improve swing mechanics, not just measure ball-flight outcomes.

**Comparable systems** in the professional space include aboutGOLF's 3Trak. Consumer launch monitors like SkyTrak are explicitly not the reference point.

**Key principles driving the design:**

- Data stays on the user's machine unless they configure otherwise. No cloud dependency.
- Sensor friction kills adoption. Every calibration and pairing step must feel purposeful and fast.
- Every capture belongs to an athlete and a session. Attribution is non-negotiable.
- The four analysis modes (Swing, Wrist, Ground Forces, AI Coach) are variations on one screen template — not four separate applications.
- Open-source positioning means first-time users may arrive with a blank machine and zero devices. The onboarding path must handle this gracefully.

---

## 2. Core mental model

Pinpoint has three persistent objects. Every other concept in the application is a view onto these.

### Athlete

A person being analysed. Stores biographical and physical data (name, handedness, height, weight, handicap), calibration offsets, baseline metrics derived from session history, and optional profile photo.

### Session

A bounded chunk of activity tied to one athlete, one analysis mode, and one device configuration. It is the unit the user starts and stops. Reports, exports, and trends are all scoped to sessions. Sessions are journalled to disk after each capture so a crash mid-session is recoverable.

### Capture

A single recorded swing within a session. A capture is a synchronised collection of: video frames from all active cameras, IMU data streams, estimated GRF traces (where applicable), and derived kinematic metrics. Captures appear in the session carousel and are individually replayable.

### Ordering

The hierarchy is always **Athlete → Session → Capture**. You cannot start a session without selecting an athlete. You cannot capture without an active session.

---

## 3. Application structure

```
App launch
│
├── Empty state (no athletes)          → Athlete form
│
├── Athlete picker                     → Mode hub
│   └── Athlete library (all athletes)
│
├── Mode hub                           → Session start flow
│   ├── Swing analysis
│   ├── Wrist analysis
│   ├── Ground forces
│   └── AI coach
│
├── Session start flow
│   ├── Configure session
│   ├── Pre-session readiness
│   │   └── Camera calibration (inline panel)
│   └── → Session view
│
├── Session view (all modes share this shell)
│   ├── Live preview panel
│   ├── Replay panel
│   ├── Side panel (metrics or conversation)
│   ├── Capture carousel
│   └── Compare mode (overlay two captures)
│
├── End-of-session summary
│   └── → New session / athlete history / export
│
├── Athlete detail / history
│
└── Settings
    ├── Cameras
    ├── IMU sensors
    ├── Force plates
    ├── GSPRO link
    ├── Audio / STT / TTS
    ├── Capture trigger
    ├── Storage
    ├── Display units
    ├── Multi-monitor
    ├── Appearance
    └── About / diagnostics
```

---

## 4. Navigation

### Left rail (always visible)

A 64 px fixed left rail is the primary navigation chrome, modelled on Microsoft Teams. It is visible at all times in all screens except modal overlays.

**Top section:**
- Athlete avatar (initials circle, coloured per athlete). Clicking opens the athlete picker as an overlay.
- Athlete name label beneath.
- Divider.

**Mode section (centre):**
- Swing analysis
- Wrist analysis
- Ground forces
- AI Coach

Each mode is an icon plus a short label. The active mode gets a white card background with a left-edge accent in the brand purple. Inactive modes are flat with muted text. When no athlete is selected, all mode icons are rendered at reduced opacity to communicate that they are not yet interactive.

**Settings (bottom):**
- Divider.
- Settings icon + label, anchored to the foot of the rail.

Settings is intentionally separated from the modes because it is a system destination, not a content destination.

### Header bar

Each content screen has a slim header bar immediately to the right of the rail. It carries contextually relevant state: athlete name and session timer (during a session), mode name, connection status pills for active devices, and session controls (REC indicator, Stop button).

### Mode switching

Switching modes from within a session ends the current session (with a confirmation if there are unsaved captures) and starts the configure-session flow for the new mode, retaining the current athlete selection.

---

## 5. First launch and empty state

### The design problem

The empty state is the most important screen in the application. A first-time user arrives with no athletes, no connected devices, no session history, and no prior mental model of what Pinpoint is. The screen has to answer three questions at once:

1. What is this?
2. Is it going to be hard?
3. What do I do first?

It must not feel like a setup wizard ambush. It should feel like a one-page welcome that respects the user's time.

### Layout

The left rail is present but fully muted — all mode icons are at reduced opacity. The athlete avatar slot shows a faint `+` circle. Settings is the only fully active rail item.

The main content area contains a single centred card:

**Top half of card:**
- App name: "Welcome to Pinpoint"
- Two-line description: "An open-source workshop for analysing the golf swing. Cameras, IMUs, force plates — your data, your machine."
- Horizontal rule.
- "Start by adding an athlete" (heading)
- One-line reassurance: "Every session belongs to someone. That's usually you."
- Primary CTA: **"Add your first athlete"** — filled purple button, full-width within the card.

**Below rule:**
- Subheading: "Or get your kit ready first"
- Three secondary cards in a row:
  - **Connect a camera** — links to Settings → Cameras.
  - **Pair wrist IMUs** — links to Settings → IMU sensors.
  - **Read the docs** — external link to project documentation.

**Footer of card:**
- "Nothing connects to the cloud unless you set it up."

### Design rationale

The rail is shown muted (not hidden) so the user understands the shape of the application before they've interacted with anything. Hiding it entirely would force re-discovery.

The three secondary cards exist because some users want to confirm their hardware works before entering personal data. Accommodating this order of operations costs nothing.

The privacy statement in the footer earns trust without legal text. For an open-source tool, this single sentence does important positioning work.

This same empty-state screen is shown if all athletes are later deleted. No special "you've cleared your data" messaging is added — the empty state is semantically the same regardless of how it was reached.

---

## 6. Athlete management

### 6.1 Athlete picker

The entry point after launch (or after ending a session). Displays the three most recently active athletes as large quick-start cards at the top, then a searchable list of all athletes below.

**Quick-start cards** (top row, up to three): Athlete avatar + name, handedness, handicap, session count, days since last session. Clicking a card selects that athlete and advances to the mode hub.

**All athletes list:** Tabular rows with avatar, name, handedness, handicap, session count, last session age, and a chevron. Clicking opens that athlete. Row items alternate background for scannability. Click column headers to sort.

**Search:** A text input at the top-right of the list section. Matches on name and any notes/tags.

**Footer actions:**
- `+ New athlete` — opens the new-athlete form.
- `Import roster` — CSV or JSON import for coaches onboarding existing client lists.

**Rail state:** Athlete avatar slot shows `+` placeholder. Mode icons are muted until an athlete is selected.

### 6.2 New athlete form

A single-screen form divided into three clearly labelled groups.

**Required:**
- Name (free text)
- Handedness (Right / Left chip toggle, defaults to Right)

**Recommended** (subheaded with: "used to normalise body kinematics"):
- Height (value + cm/ft toggle, defaults to ft for UK locale)
- Weight (value + kg/lb toggle, defaults to lb for UK locale)
- Handicap (number)
- Primary club (dropdown, defaults to Driver, pre-populates session configure)

**Optional baselines** (subheaded with: "leave blank — Pinpoint learns from your first sessions"):
- Driver speed target (mph)
- Photo / avatar (file picker, falls back to coloured initials)
- Notes (free text, used for tags and any other context)

**Actions:**
- `Cancel` — returns to athlete picker.
- `Save` — saves athlete and returns to athlete picker with the new athlete visible.
- `Save and start` (filled purple, primary) — saves athlete and immediately advances to mode hub.

### 6.3 Design rationale

Asking for too much upfront causes abandonment. Asking for too little means the first session produces analysis that can't be contextualised. The three-group structure answers this by communicating that two fields unlock the rest.

The reassurance copy under the Optional group ("Pinpoint learns from your first sessions") is the key unlock — the user doesn't need to know their driver speed to get started. The system will derive a baseline from early sessions automatically.

---

## 7. Session start flow

Starting a session passes through a small state machine with four gates. Each gate is conditional — returning users who haven't changed their setup skip one or more gates automatically.

```
Mode hub
  │
  ▼
Configure session          ← always shown
  │
  │  (fast path: if all devices valid from prior session)
  ├──────────────────────────────────────────────────→ Session view
  │
  ▼
Pre-session readiness      ← shown when any device needs attention
  │
  ▼
Session view
```

**Gate conditions:**
- **Connect:** Skipped if all required devices are already streaming.
- **Calibrate:** Skipped if last calibration is within tolerance and has not been flagged as due.
- **Verify:** Always offered but only blocking on the first-ever session for a given mode.

### 7.1 Configure session

A single screen (not a wizard). All configuration fits at a glance.

**Session label** (optional free text, auto-suggests from prior labels for this athlete).

**Club chips:** Driver / 3-wood / 5-iron / 7-iron / Wedge / Other… The selection pre-populates the analysis pipeline with the correct swing-arc geometry and baseline distributions. Defaults to the athlete's Primary Club from their profile.

**Sensors for this session:** Three sensor cards side-by-side.
- **Cameras:** Auto-selected when cameras are connected. Shows camera count and fps.
- **Wrist IMUs:** Optional add-on, shown as available when paired. Click to include.
- **Force plates:** Shown greyed when none are connected, with a link to Settings.

**Capture trigger:**
- Audio — ball strike (default when mic is present)
- Motion — auto-detect from camera or IMU motion
- Manual — spacebar / foot pedal

**Readiness strip** (bottom of screen): A summary of current device status before the user clicks Continue. Shows connected devices, calibration ages, and any warnings (e.g. "Cam 2 calibration is due"). Contains `Skip checks` and `Continue` buttons. `Continue` advances to pre-session readiness.

### 7.2 Pre-session readiness

The single screen where device state is resolved before hitting balls. Two panels:

**Left: Devices list**
Each connected device gets a row with:
- Status dot (teal = ready, amber = in progress, red = failed)
- Device name and role
- Status description
- State pill (ready / calibrating… / not connected)
- Contextual action button (recalibrate / test / configure)

Cam 2 in the amber "calibrating" state shows a progress bar inline instead of a state pill, with the current frame count.

**Right: Verify with a test swing**
A live preview mini-panel from Cam 1, with an "awaiting swing" indicator. The user hits one ball. The capture runs through the full pipeline but is not saved to the session. A results panel beside the preview shows key metrics with "plausible / implausible" verdict pills — sanity checks against the athlete's baseline. Implausible readings (e.g. an amateur producing a 200 mph reading) show a red pill and a recommendation to check camera placement.

**Footer:**
- `Back` — returns to configure session.
- `Start session` — enabled only when all required devices are in Ready state. Shows a blocking message while any device is still in progress.

---

## 8. Camera calibration

### Overview

Camera calibration is an inline panel within the pre-session readiness screen. It is not a separate screen. This keeps the other device status rows visible during calibration so the user doesn't lose their place.

Calibration is triggered by clicking "recalibrate" on any camera row in the readiness screen. The calibration panel slides in on the right, replacing the test-swing panel.

### When calibration is needed

- First-ever setup of a camera.
- Camera position has changed (movable tripod setups).
- Calibration has exceeded its age threshold (configured in Settings, default 7 days).
- The user has manually triggered recalibration.

Users with **fixed studio rigs** may calibrate infrequently (monthly or quarterly). Users with **movable tripods** may recalibrate every session. The design must accommodate both without penalising the mobile-rig user.

### Calibration presets

The left device rail includes a "Calibrations on file" section listing named calibration presets. Each preset stores a complete extrinsic (and optionally intrinsic) calibration for a named physical setup — "Studio · cam 1+2 · May 12", "Range bay · cam 1 · May 09".

Loading a preset is a one-click alternative to recalibrating. Mobile-rig users build a library of presets for their common locations. Returning to a known setup means picking the matching preset — no checkerboard needed.

When the user accepts a new calibration, a toast offers to save it as a named preset. The preset is not automatically saved; the user names and confirms it.

### Calibration panel layout

**Header:** Camera name, calibration pattern type and dimensions (e.g. "checkerboard · 9×6 · 25 mm").

**Live frame view (left, ~60% width):** The camera feed at reduced frame rate with the detected calibration target overlaid in amber when in view. A real-time pill at the bottom of the feed gives guidance: "good angle · hold", "board not detected", "too close", "angle too shallow". The system auto-captures the frame when a stable, useful pose is held.

**Coverage map (right, ~40% width):** A grid (2×4 or similar) showing which regions of the frame have been covered by the calibration target. Cells shade green as coverage accumulates; dashed outline cells are uncovered. Below the grid: "14 / 22 frames" global progress.

**Pose checklist (below frame view):** Four pose categories with status indicators:
- Centre, near — normal hold at typical working distance
- Centre, far — board moved ~3 m back from camera
- Corners — all four frame quadrants covered
- Tilted angles — board held at ~30° to camera plane

Each category shows a tick when sufficient frames for that pose type have been captured. The next required category is highlighted in amber with a brief instruction.

**Solver status (footer of panel):** A progress bar showing the reprojection error in pixels. The bar fills toward the target (sub-1 px). A label shows the numeric error and whether it is improving. The solver re-runs automatically after each captured frame, giving the user live feedback that the calibration is converging. When the error plateaus and more frames are not improving it, the "Accept" button is the right next action.

**Actions:**
- `Cancel` — returns to readiness with prior calibration intact. No calibration data is destroyed until Accept is confirmed.
- `Restart` — clears all captured frames and resets to zero.
- `Accept` — commits the calibration and returns to the readiness screen with the camera in a Ready state.

### Technical scope

This screen handles both intrinsic (lens distortion) and extrinsic (camera position/orientation in world space) calibration in a single sweep. If camera lenses are fixed and calibrated at factory, a "use existing intrinsics" mode could skip that component — this is a future consideration.

Multi-camera pairwise calibration (ensuring all cameras share a common world frame) is a separate mode triggered by "Run multi-cam sync" in Settings → Cameras. In that mode both live feeds are visible side by side, and the coverage map tracks joint visibility. This is a future-version feature.

---

## 9. Session views

All four analysis modes use the same session-view shell. What changes per mode is the sensor sources active, the metrics computed, and the specific content of the three swappable panels. The shell itself is unchanged.

### 9.1 Shell layout

```
┌──────────────────────────────────────────────────────┐
│ Left rail (64 px fixed)                              │
├──────────┬──────────────────────────────────────────┤
│ Header bar                                           │
├──────────┴──────────────────────────────────────────┤
│                                                      │
│  Live preview panel   Replay panel   Side panel      │
│  (left, ~45%)         (centre, ~30%) (right, ~25%)   │
│                                                      │
├──────────────────────────────────────────────────────┤
│ Capture carousel (full width)                        │
├──────────────────────────────────────────────────────┤
│ Session control bar (full width)                     │
└──────────────────────────────────────────────────────┘
```

### 9.2 Header bar (in-session state)

- Mode name (bold)
- Session time and duration
- Capture count
- Device connection pills (e.g. "GSPRO ●", "Lead IMU ●")
- REC indicator (red dot + "REC" label, animated during capture)
- Stop session button (coral, right-aligned)

### 9.3 Live preview panel

Shows a real-time feed from the primary sensor for the current mode. Contains:
- Camera label and fps indicator (top)
- Full-resolution camera feed (swing and AI coach modes)
- Wrist-angle dial cluster (wrist mode) — three dials for flex/extension, radial/ulnar deviation, rotation — updating at IMU sample rate
- Foot-pressure visualisation (ground forces mode) — two footprint shapes with pressure heat-map overlay
- Swing-detected pill (bottom-left) — appears when the capture trigger fires

### 9.4 Replay panel

Shows the most recently selected capture from the carousel. Contains:
- Capture number and current playback speed (top)
- Video or trace view (mode-dependent)
- P-position timeline — horizontal scrubber with markers at key positions: address, top of backswing, P5 (late downswing), impact, finish
- Playback controls: play/pause, loop, speed selector (0.1× / 0.25× / 1×)
- Frame counter

**Mode-specific replay content:**
- Swing: camera feed with optional skeleton or silhouette overlay
- Wrist: wrist-angle trace plot with P-position markers; optionally overlaid on camera feed
- Ground forces: force trace plot (vertical and horizontal components, trail and lead foot)
- AI coach: camera feed; plays automatically when Claude references a capture

### 9.5 Side panel

**Swing, Wrist, Ground forces modes — Key metrics rail:**
Three metric cards in a vertical stack, each showing a single metric value and a delta against the session average. A "More on screen 2" link at the bottom bridges to the full dashboard on the secondary monitor.

**AI Coach mode — Conversation thread:**
Replaces the metrics rail entirely. See section 9.7.

### 9.6 Capture carousel

Full-width strip anchored above the session control bar. Contains one thumbnail tile per capture in the session, in chronological order. Each tile shows:
- Capture number
- A top-line metric (configurable per athlete: defaults to club speed for swing mode, lead-wrist flex for wrist mode)
- A thumbnail: impact-frame crop for swing mode, wrist-trace silhouette for wrist mode

The currently selected capture is highlighted with a purple border and purple tint. Clicking any tile loads it into the replay panel and refreshes the side panel metrics.

The carousel scrolls horizontally. An arrow affordance appears at the right edge when captures overflow the visible width.

### 9.7 Session control bar

Full-width strip at the bottom. Contains:
- **View toggle:** Single / Compare (compare mode described in section 11)
- **Sort:** Chronological / By metric (descending)
- **Export session** button (right-aligned)

### 9.8 Secondary monitor — kinematic dashboard (Swing mode)

When a secondary display is configured, the Pinpoint dashboard window opens there automatically. Its content refreshes whenever the selected capture changes on the primary.

**Metric cards:** Two rows of four cards each, grouped by domain. Row 1: Clubhead (speed, attack angle, face to path, low point). Row 2: Body (pelvis turn, thorax turn, X-factor, side bend at impact). Each card shows the metric value and a delta against the athlete's baseline (+/- and colour: teal for better, coral for worse, gray for neutral).

**Kinematic sequence chart:** The signature visualisation for swing analysis. A single-axis plot showing normalised angular velocity over swing phase (address → finish) for four segments: pelvis (purple), thorax (teal), lead arm (amber), club (coral). A dashed vertical line marks impact. The coach's eye goes immediately to whether the traces peak in proximal-to-distal order.

**Rotation traces (below chart):** Individual time-series traces for any additional metrics relevant to the session's focus.

### 9.9 Mode-specific variations

**Wrist analysis:**
- Live panel: Three dials for lead-wrist angles, updating at IMU sample rate. "Listening for swing" pill (not "swing detected" — wrist sessions detect swings via IMU motion thresholds, not ball-strike audio).
- Replay panel: Wrist-angle trace plot rather than video (when cameras are not active). Toggle to video + trace overlay when cameras are present.
- Carousel thumbnails: Wrist-trace silhouette per capture.
- Control bar additions: Hand selector (Lead / Trail / Both), reference overlay picker (athlete's own baseline, a named professional reference, or none), Calibrate shortcut button.
- Secondary monitor: Angle-vs-time traces for flex/ext, rad/uln, rotation, across the full swing phase.

**Ground forces:**
- Live panel: Two footprint visualisations with real-time pressure overlay, vertical and horizontal force gauges.
- Replay panel: Force trace plot (trail and lead foot, vertical and horizontal components, with impact line and weight-transfer moment marked).
- Control bar additions: Per-foot view toggle, force-component selector.

**AI Coach:**
- Header additions: Voice toggle pill ("Voice on / off"), model selector (e.g. "Sonnet 4").
- Live panel: Camera feed (same as swing mode). Automatically plays the capture referenced in the conversation when Claude cites a clip.
- Side panel: Replaced by conversation thread (see below).
- Control bar additions: Sensor status pills (camera / wrist IMU / GRF — active or inactive), Save transcript, Lesson summary.
- Carousel: Renamed "Lesson captures". A condensed version with smaller tiles.

**AI Coach — Conversation thread:**
A scrollable chat thread occupying the right panel. Claude's messages are shown in purple-tinted bubbles. Athlete messages (voice-to-text transcriptions) are shown in gray-tinted bubbles with a "via voice" label and duration.

Claude's diagnostic messages may contain inline action pills:
- `▶ clip #03` — clicking this loads capture #03 into the replay panel and plays it.
- `pelvis chart` — clicking opens the kinematic sequence chart on the secondary monitor (or in a sheet over the replay panel if single-monitor).

Input controls at the bottom of the thread:
- `Hold to talk` button (push-to-talk, primary input)
- `type` button (opens a text field for typed input)
- `mute TTS` toggle (disables Claude's voice response without disabling voice input)

Lesson sessions can be given a focus title at configure time (e.g. "Fixing early extension"). The title appears in the header and is stored with the session for later retrieval.

---

## 10. Mid-session IMU recalibration

### When it is needed

IMUs accumulate drift over time. The system continuously monitors the difference between current quiet-moment readings and the last zero reference. When drift exceeds a configurable threshold (default: 3°), a small warning badge appears on the IMU status indicator in the header. The user taps the badge to open the re-zero overlay.

The overlay can also be triggered manually at any time during a session via a "Re-zero" button in the session control bar (wrist mode only).

### Overlay behaviour

The re-zero overlay is a modal sheet over the session view, not a separate screen. The session is paused while the overlay is open. The overlay shows the current offset since last zero to confirm that drift is present.

Closing the overlay (✕ or Cancel) returns to the session view with the previous zero intact. No captures are affected by cancellation.

### Pose selection

**Wrist-neutral pose (default):**
Arms hanging at sides, palms facing the thighs, no club required. The standard IMU zero reference matching the sensor manufacturer's recommended calibration posture. Fastest and most reproducible. Appropriate for most sessions.

**Address-position pose:**
Set up to the ball with the club in-grip, in the athlete's normal address position. Used when the session specifically compares across different grip configurations, where the grip rotation introduces a known but constant IMU offset that should be zeroed at address rather than at neutral.

### Live readings display

While the athlete holds the selected pose, a panel shows the current readings for all three angle components (flex/ext, rad/uln, rotation) for both wrists simultaneously. Each row includes:
- A horizontal bar with a zero marker and the two wrist reading markers (lead and trail)
- Numeric values for each wrist
- A dashed vertical "target zero" reference line

Below the readings: a stability pill that transitions through states:
- "hold steady" (readings moving)
- "settling… hold for 1.8s more" (readings slowing)
- "stable · capturing" (auto-capture triggers)

The system auto-captures the zero when readings are within tolerance of zero AND variance has been below threshold for the configured dwell time (default 2 seconds). This prevents motion artifacts from a button-press at the moment of capture.

A `Capture zero` button is available as a manual override for cases where auto-capture does not trigger.

### Posture guidance

A simple stick-figure schematic shows the selected pose with IMU positions marked. A caption below the figure gives plain-language instructions ("Stand tall, arms hanging, palms facing thighs."). The diagram is intentionally schematic — not photographic — because silhouette guides are more universally legible than photos of a specific person.

### Recording the recalibration in the session

When a re-zero is accepted mid-session, a marker is written into the session data between the capture before and after the re-zero event. This allows post-hoc analysis to account for the coordinate-system change when comparing captures across the marker boundary.

---

## 11. Compare mode

Compare mode is activated by the "Compare" toggle in the session control bar. It is available in all four analysis modes.

### Two-capture selection

The carousel changes to an assignment mode: clicking a tile assigns it to slot A (purple border and "A" badge) or slot B (amber border and "B" badge). At most two tiles are assigned at any time. Clicking an already-assigned tile clears it.

The diff is always computed as B − A. The metric diff column labels this direction explicitly. Positive values mean B is higher than A; colour coding depends on the metric's direction of desirability.

### Overlay vs side-by-side

A toggle in the header bar switches the replay panel between:
- **Overlay:** Both captures rendered onto the same frame, with A at reduced opacity (faded) and B at full opacity. Both retain their colour assignment (purple silhouette for A, amber for B). An auto-generated annotation marks the largest spatial difference between the two (e.g. "pelvis 3.2" forward at P6").
- **Side-by-side:** The replay panel splits into two equal halves, each showing its assigned capture independently.

An **alignment toggle** in the session control bar sets the synchronisation point: impact (default), address, or top of backswing. Impact alignment is appropriate for most comparisons; address alignment is useful for setup analysis; top-of-backswing alignment is useful for comparing backswing paths.

A **show toggle** switches the overlay between silhouettes (body shape) and skeleton (joints-and-segments). Skeleton is better for joint-level analysis.

### Metric diff panel

The side panel shows a diff table instead of the standard metrics rail. Each row:
- Metric name
- A → B values
- Delta pill: coral for B worse than A, teal for B better, gray for no meaningful difference

The full diff table is shown on the secondary monitor when configured.

---

## 12. End-of-session summary

Shown automatically when the user clicks "Stop session". The session has already been fully persisted at this point.

### Header

Session metadata: mode, start time, end time, duration, capture count. A "✓ saved" confirmation pill. Rename and tag actions.

### Headline metric cards

Four cards showing session-level aggregates (averages across all captures) for the most important metrics for the current mode. Each card shows a delta against the athlete's baseline, with colour coding (teal for better, coral for worse, gray for stable). A card with a concerning metric gets a coral border to draw attention.

### Consistency chart

A single-axis strip chart showing a top-line metric (e.g. club speed) for each capture chronologically. A baseline reference line is overlaid as a dashed horizontal. A trend line is fitted through the data points. Below the chart: standard deviation summary, and a comparison to recent sessions ("narrower than 4 of last 5 sessions").

### Notable captures panel

Three auto-selected captures with brief rationale:
- Best (highest value on the primary metric)
- Cleanest (best value on the secondary quality metric, e.g. smallest face-to-path)
- Outlier (most deviant on any flagged metric)

Clicking a capture loads the full carousel and replay view for that session.

### Claude observation (optional, AI Coach–linked)

A single-paragraph summary generated by Claude reviewing the session metrics. Enabled in Settings → AI Coach. One paragraph maximum. Contains a single `Open in coach ↗` button that creates a new AI Coach session pre-populated with the flagged capture and a brief from the observation.

### Actions

- `New session` (primary, purple-tinted) — starts configure-session for the same athlete and mode.
- `Export PDF` — generates a session report.
- `Share link` — copies a shareable link (requires cloud sync, future feature).
- `Add to athlete history` — marks this session as a milestone in the athlete's progress timeline (as distinct from a routine practice block).

---

## 13. Athlete detail and history

Reached by clicking an athlete row in the athlete picker or by clicking the athlete avatar in the left rail during a session (opens as an overlay).

### Athlete header

Avatar, name, handedness, handicap, profile fields (height, weight, primary club), joining date, session count. Actions: New session (primary), Edit, Export, Archive.

### Baseline cards

Four cards showing rolling 30-day averages for key metrics. Deltas compare the current 30-day window against the prior 60-day window, giving a directional signal. Flagged metrics (consistent adverse trend) show a coral accent.

### Trend charts

Two configurable chart slots. Default configuration: club speed (left) and face-to-path (right). Each shows 90 days of session-average data as a line chart, with a target/reference line overlaid. The user can change the metric plotted on each chart.

A coach reading these two charts gets the headline story immediately: speed is up (or not), and pattern consistency is improving (or not).

### Session list

All sessions for this athlete, most recent first. Each row: mode badge (colour-coded by mode), date/time, duration, capture count, primary metric value for that session, delta against baseline, chevron link to session detail. Clicking a row opens that session's summary (read-only, with full carousel and replay available).

Mode badges: purple for Swing, pink for Coach, amber for Wrist, teal for Ground Forces.

The list can be filtered by mode badge (clicking a badge filters to that mode) and sorted by date, metric, or deviation.

---

## 14. Settings

### Layout

Two-column: a 160 px left category list, and a detail pane on the right.

### Category list

**Devices:**
- Cameras (shows connected count badge)
- IMU sensors (shows connected count badge)
- Force plates (shows connected count or 0)
- GSPRO link (shows on/off badge)

**App:**
- Audio · STT / TTS
- Capture trigger
- Storage
- Display units
- Multi-monitor
- Appearance

**About:**
- Updates
- Diagnostics
- License · open-source attribution

### Cameras detail pane

Each connected camera gets a card showing:
- Status dot
- Model and SDK path (e.g. "Basler a2A1920-160ucBAS · pylon · USB3")
- Live fps and resolution
- Calibration age and status (green for current, amber for due, red for expired)
- `Test` button — opens a live preview overlay
- `Edit` button — opens the camera-role editor (face-on / down-the-line / custom)

**Add camera** button — triggers the camera-connect flow.

**Run multi-cam sync** button — opens the joint calibration panel for pairwise extrinsic alignment.

**SDK preference toggle:** Basler pylon (default) / Aravis / GenTL producer. Copy beneath: "pylon offers richest feature support; Aravis is the open-source fallback on Linux."

### IMU sensors detail pane

Each paired IMU: name/ID, assigned hand (lead / trail), battery level, last-zero age, firmware version. Pair and unpair actions. Re-zero shortcut (opens the same overlay as mid-session re-zero, but outside a session context, for pre-session check).

### Audio · STT / TTS

Microphone device selector. Ball-strike detection threshold (dB slider, with live level meter). STT provider selector (system / Whisper / cloud; cloud requires API key). TTS voice selector and speed. Transcript retention policy.

### Capture trigger

Default trigger per mode (audio / motion / manual). Motion sensitivity slider. Pre-roll buffer duration (default 2 seconds — how many frames before the trigger are saved). Post-roll duration (default 3 seconds). Manual trigger key binding.

### Display units

Speed: mph / kph. Distance: inches / cm. Angle: degrees (no alternative). Force: N / lbf / % bodyweight.

---

## 15. Multi-monitor configuration

**Settings → Multi-monitor**

### Detected displays

A schematic row of monitor representations, sized roughly proportional to their actual resolution. Each monitor shows a miniature preview of its currently assigned content:
- Primary (purple border): live preview + replay + rail + carousel
- Dashboard (amber border): metric cards + kinematic sequence chart + rotation traces
- Unassigned (gray): blank

Clicking a monitor opens its assignment editor.

### Panel assignment

Each monitor has a checklist of panels that can be shown on it. Panels are mutually exclusive across monitors — assigning a panel to one monitor removes it from any other. Unchecking a panel across all monitors hides it entirely (useful for single-monitor minimal setups or screen-recording).

**Primary monitor panels:** Mode rail, Live preview, Replay, Key metrics, Carousel.
**Dashboard panels:** Detailed metrics grid, Kinematic sequence chart, Rotation traces, Wrist traces, Force traces.

### Presets

Named multi-monitor configurations. Built-in presets:
- **Coaching booth:** Primary in front of athlete (live + replay), secondary in front of coach (full dashboard), tertiary for carousel.
- **Solo practice:** Everything on one screen, rail icons only.
- **Demo / record:** Primary shows clean live preview only (for screen recording), secondary shows dashboard.

User-defined presets can be saved and named. Presets are stored per machine, not per athlete, because monitor configurations are tied to physical setups.

---

## 16. Qt architecture notes

These notes reflect the UX design's structural implications for implementation. They are not comprehensive architecture documentation.

### Main window composition

```
MainWindow
├── LeftRail (QWidget, 64 px fixed)
│   ├── AthleteAvatarButton
│   ├── ModeButton × 4
│   └── SettingsButton
└── QStackedWidget (content area)
    ├── EmptyStateWidget
    ├── AthletePickerWidget
    ├── NewAthleteFormWidget
    ├── ModeHubWidget
    ├── ConfigureSessionWidget
    ├── PreSessionReadinessWidget
    │   └── CameraCalibrationPanel (inline, replaces test-swing panel)
    ├── SessionView
    │   ├── HeaderBar
    │   ├── LivePanel (mode-specific subclass)
    │   ├── ReplayPanel (mode-specific subclass)
    │   ├── SidePanel (mode-specific subclass)
    │   ├── CaptureCarousel
    │   └── SessionControlBar
    ├── SessionSummaryWidget
    ├── AthleteDetailWidget
    └── SettingsWidget
```

### Session view — factory pattern

`SessionView` is parameterised by `SessionMode` (Swing / Wrist / GroundForces / AICoach). A factory keyed on `SessionMode` produces the correct subclass for each of the three swappable panels:

```
SessionPanelFactory::createLivePanel(mode)    → LivePanel subclass
SessionPanelFactory::createReplayPanel(mode)  → ReplayPanel subclass
SessionPanelFactory::createSidePanel(mode)    → SidePanel subclass (metrics or chat)
```

Adding a new analysis mode (e.g. putter analysis) requires implementing three panel subclasses and registering them in the factory. No changes to `SessionView` itself.

### Device state machines

Each connected device runs a `QStateMachine` with states:

```
Disconnected → Connecting → Connected → Calibrating → Ready → InUse
                                    ↘ CalibrationFailed
```

The pre-session readiness screen subscribes to device state machines and re-renders reactively. The Start Session button is enabled when all required devices are in the `Ready` state.

### Session journalling

Captures are written to disk immediately after each capture event, before the user takes the next swing. This ensures that a process crash mid-session loses at most one capture, not the entire session.

The session journal is a directory containing: a `session.json` manifest (athlete ID, mode, start time, configuration), one subdirectory per capture (video frames, IMU stream, derived metrics), and a `calibrations.json` snapshot of the device calibration state active at session start.

### Secondary monitor window

A separate `QWindow` (not a `QDialog`) is created when a secondary monitor is configured. It is a direct child of the main process, sharing the same data model. The two windows communicate via Qt signals: when the selected capture changes in the primary window's carousel, a signal triggers a full refresh of the dashboard window.

---

## 17. Open questions and future work

The following areas have been identified as requiring design work but are not yet sketched.

### Pending design work

**First-camera-connect flow.** What happens the first time a camera is plugged in with no prior configuration? Auto-detection, driver validation, format selection, and role assignment. This is the highest-risk moment for users with non-standard hardware.

**IMU pairing flow.** Bluetooth device discovery, pairing, hand assignment (which physical IMU goes on which hand). The pairing UI needs a clear "now put this one on your lead hand" moment.

**Onboarding tour.** The first session after athlete creation — does the app offer contextual hints? A minimal hints system (dismissible tooltips, shown once) is probably the right scope.

**AI Coach lesson library.** Recurring lesson templates, drill sequences, the ability to start a coach session from a named lesson plan rather than from scratch.

**Error states.** Camera disconnect mid-session. Calibration failure after maximum frames. IMU battery exhaustion during session. Network failure during AI Coach session. Each unhappy path needs explicit design.

**Session comparison across sessions.** The current compare mode compares captures within a single session. Comparing a capture from this week against a capture from last month requires cross-session comparison — a different data access pattern.

**Export formats.** PDF session reports, raw data export (CSV for metrics, video files), import/export of athlete profiles for coach-to-athlete sharing.

### Design principles to carry forward

- Never destroy data silently. Failed calibrations, bad captures, mid-session crashes — all should be recoverable or at least visible.
- Every mid-session recalibration leaves a marker in the session data. Analysis pipelines must respect this marker.
- Sensor connection state is always visible from within a session. A dropped IMU should never be discovered by looking at bad data.
- The app must be functional in single-monitor setups. Multi-monitor is an enhancement, not a requirement.
- The AI Coach integration should degrade gracefully. If the API is unreachable, the session still captures and the user is informed clearly.

---

*Document maintained by the Pinpoint project. Screen designs were produced as annotated wireframes; see the design conversation for visual references. This document describes intended behaviour, not current implementation state.*
