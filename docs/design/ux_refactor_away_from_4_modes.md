# UX refactor away from 4 modes

Status: **design + build plan, not started.** Written 26 Oct 2026 against the working tree at
`91c6cac4` (trunk-IMU work in flight). Do not start Phase 1 until that work has landed: Phase 1
touches `Main.qml`, which everything routes through.

How to use this document with Claude Code: **one phase per fresh session.** Each phase lists its
goal, the files it touches, what it reuses, what it adds, the tests that must change, and its
acceptance checks. Phases are ordered so that each one ships on its own and leaves the app working.

Visual reference (for humans): the Design canvas "PinPoint Studio — UX restructure", board
**1 · Home** — https://claude.ai/code/artifact/06edc700-742b-4dd3-9f5e-01136988bced. Claude Code
cannot open it, and it predates the decisions in this document (it shows swing phases, not pack
groups). **The authoritative visual reference is the PNGs in
`docs/design/ux_refactor_away_from_4_modes/`** (Appendix A), rendered to match this document.

---

## 1. Why

The app was designed around four session **modes** — Swing, Wrist, GRF, AI Coach — each with its
own rail button, its own start card on Home, and its own session screen. That is no longer how the
program works:

- The analysis pipeline computes whatever the rig allows. Cameras and IMUs decide which of the
  kinematic sequence, wrist, posture, plane and GRF metrics exist; the golfer does not pick a mode.
- In the code, only one mode is real: `ScreenWrist`. Swing, GRF and Coach are
  `ScreenPlaceholder`s (`Main.qml` l.719–722) and their presets are `comingSoon`
  (`SetupContext.qml` l.92–129).
- The product has become a loop: **hit shots → the diagnostic model's findings settle → work-ons
  → practise against a work-on → re-test.** Modes have no place in that loop.

Home today stacks a start-session card grid (4 types), device readiness and pairing, YOUR SWING
(focus, do-well, next-on-list), and Sessions; the rail carries four obsolete mode buttons.

## 2. Target design

### 2.1 Concepts

| Concept | Meaning | Already in code as |
|---|---|---|
| **Session** | One sitting of shots. Always assessed against the swing model. Optionally carries a **focus**. | `SessionController`, session folder, `diagnostics.json`, `work_ons.json` |
| **Focus** | "Working on X" for this session: one condition id. Changes what the session panel leads with; never changes detection or grading. | The **focus contract**, `session_diagnostics_design.md` §A6 / C2.7: `SessionDiagnosticsModel::declareFocus()`, `focusConditionId`, `focusFromShot` |
| **Work-on** | A fault the golfer keeps producing, derived across sessions. | `WorkOnsController.items`, `aggregateWorkOns()`, `WorkOnStatus {Active, Easing, Cleared}` |
| **Area** | A group of conditions (setup, posture, sequence…) rated as a whole. **New view, no new data.** | `condition.group` in `core.json` |
| **Rig capability** | What the connected kit can measure, by metric group. | `MetricCatalog.setupSummary()` → `summariseSetup()` (`Metrics/setup_capability.h`) |

**Session type stays, but goes internal.** `sessionType` is read in ~40 non-test files including
the analysis pipeline, shot analyser, swing scorer and metric catalogue. The UI stops showing it;
the wizard chooses it. Removing it from the analysis side is out of scope (§6, Phase 2a audit).

### 2.2 Information architecture

```
Rail (top → bottom)            Destination (Phase 1 reuses existing screens)
───────────────────            ─────────────────────────────────────────────
Golfer avatar / switch         ScreenAthletePicker / ScreenAthleteForm (unchanged)
Home                           ScreenHome (rebuilt in Phase 4)
Sessions                       ScreenAthleteForm, initialTab "sessions" (exists)
Work-ons                       ScreenSwingDiagnostics (its FAULTS card is the work-ons list)
Swing model                    DiagnosticModel, promoted out of Settings panel 10 into its own screen
── spacer ──
Settings                       ScreenSettings (System/Developer stay beneath it, as today)
```

- No mode buttons. No Progress page yet (out of scope; leave room in the rail).
- A **live session** is one screen (today's `ScreenWrist`). While it runs, navigation is locked
  to Session, System and Settings — the existing rule, minus the `sessionType + 1` arithmetic.

### 2.3 Home (the approved layout)

Three sections, top to bottom (wireframe in Appendix A.1):

1. **Start** — left: "Hit some shots", one-line explanation, **Start session** (opens the
   existing wizard with focus = open assessment), the club picker that exists today, and a
   "last session" line. Right: **What this rig can measure** — one row per capability group,
   live/estimated/missing, with a "connect …" link on a gap, and the existing "Add iPhone as a
   camera" (PPCP pairing) entry.
2. **Your swing by area** — the new centrepiece. One row per area, collapsed by default except the
   weakest. Columns: chevron · area · rating bar · score · trend · biggest fault · work-ons count.
   Expanding a row lists that area's conditions (top 3–5 by impact) with: name · share of swings
   (`fired/assessable`) · typical value vs corridor · grade · one action:
   - an active work-on → **Practise** (opens the wizard with focus = that condition);
   - a newly listed work-on → **Add work-on** *(see decision D3)*;
   - any other Watch/Action item → **Make work-on** *(D3)*;
   - a footer link **"All of <area> →"** opens Swing diagnostics filtered to that group.

   The table replaces YOUR SWING (`HmSwingSummary`, `HmFocus`) on Home. It steers to the area of
   most need while showing the whole swing ("your setup is nearly perfect; the work is in your
   release").
3. **Recent sessions** — the existing `PpSessionReportList`, unchanged.

Removed from Home: the 4-type card grid (`HmTypeCard`), the device-readiness block (folded into
the capability panel), YOUR SWING.

### 2.4 Starting a session

- Every entry point — Home's Start session, every Practise button — opens the **existing
  wizard** (`ScreenSessionSetup`). The type step disappears; the wizard gains an optional
  **focus** carried in from the caller and shown on its first page ("Focus: Early extension ·
  change").
- On start: `sessionController.start(resolvedType)`, then, if a focus was chosen,
  `SessionMode.sessionDiagnostics.declareFocus(conditionId)` at shot 0. Mid-session focus changes
  keep using the panel's existing `_declareFocus` path (`PpSessionDiagnosticsBody.qml` l.123).
- Session **goals** (`presets[].goals`, `appSettings.sessionGoalsByType`) are retired. "General
  assessment" becomes "no focus"; the other goals were pre-diagnostics placeholders.

## 3. What the code has today (findings)

| Area | Where | Notes |
|---|---|---|
| Screen indices | `Main.qml` l.550–580 | `screenHome 0 … screenSwingDiagnostics 11`, plus a parallel `screenNames` array. Magic ints are also used in `PpRail.qml` (0–4, 8, 9, 11) and `navigation_controller.cpp`. |
| Session ↔ screen coupling | `Main.qml` l.596–598, `navigation_controller.cpp` l.48–53, `SetupContext.presets[].railIndex` | session screen = `activeSessionType + 1`; lock allows 8, 9 and that index. |
| Mode rail buttons | `PpRail.qml` l.140–200 | Home, Swing, Wrist, GRF, Coach; `_modeMuted(idx)`; avatar at top; Settings at bottom (highlights for 5, 8, 9). |
| Session types | `session_controller.h` l.55 | `enum class Type { None=-1, Swing, Wrist, Grf, Coach }`, Q_ENUM. |
| Presets | `SetupContext.qml` l.72–129 | Per-type icon, name, railIndex, comingSoon, goals. Comment already says the preset picks only goals, rail screen and the type passed to `start()`. |
| Home | `home/ScreenHome.qml` (826 lines) | §1 identity, §2 launcher (`HmTypeCard` grid, club pick), §3 YOUR SWING (`HmSwingSummary`), §4 device readiness + PPCP pairing, §5 Sessions (`PpSessionReportList`). Signals: `startSessionRequested(int)`, `openSessionScreenRequested(int)`, `swingDiagnosticsRequested`, `reviewSessionRequested`, `athleteSessionsRequested`, `studioSettingsRequested`. |
| YOUR SWING | `HmSwingSummary.qml`, `HmFocus.qml`, `HmGoesTogether.qml` | Paint-only; every string from `workOns` (`focusItem`, `doWellItems`, `nextItems`, `togetherItems`). Design: `home_themes_design.md`. |
| Work-ons | `diagnostics/work_ons_controller.h`, `Coaching/work_ons.h` | Off-GUI-thread catch-up over `<session>/work_ons.json`; `aggregateWorkOns()` → `WorkOnSummary {id, status, score (Wilson lower bound), swingsFired, swingsAssessable, history[], latest: WorkOnEntry {measureId, typicalValue, corridorLo/Hi, corridorShape, causedBy…}}`. `WorkOnClass` excludes Outcome/Capacity/Intent/Equipment from ever being a work-on. |
| Condition groups | `Resources/diagnostics/core.json` | `group` on every condition: setup 55, armsAndClub 29, sequence 21, ballFlight 20, posture 19, impact 15, lateral 13, release 7, finish 3 (182 conditions). |
| Grades | `Diagnostics/norms/norm.h` l.78 | `enum class Grade { Ideal, Good, Watch, Action, NotMeasured }`, policy pack-wide. |
| Focus contract | `session_diagnostics_model.h` l.175–176, 341–342 | `focusConditionId`, `focusFromShot`, `declareFocus()`, `clearFocus()` — implemented. |
| Rig capability | `Metrics/setup_capability.h`, `review/metric_catalog.h` l.83–93, `SetupContext.qml` l.587–625 | `setupSummary(setup)` returns per metric group `{measured, estimated, unavailable, deviceGaps, upgrade…}`; already used by the wizard's closing page. |
| Swing model editor | `diagnosticmodel/DiagnosticModel.qml` | Hosted as Settings panel 10 (`ScreenSettings.qml` l.603). |
| Sessions list | `ScreenAthleteForm` `initialTab: "sessions"` | Reached from Home's "Show all". |

## 4. Reuse, adapt, add, delete

| Component | Verdict | Change |
|---|---|---|
| `NavigationController` | **Adapt** | Named `Screen` enum (Q_ENUM); lock rule = Session/System/Settings. |
| `Main.qml` StackLayout | **Adapt** | Order follows the enum; drop placeholders; one session screen. |
| `PpRail.qml`, `PpRailButton.qml` | **Adapt** | Replace mode buttons with Home/Sessions/Work-ons/Swing model; keep avatar, Settings, `attention`/muting behaviour. |
| `ScreenWrist` | **Reuse** as *the* session screen | Rename (`ScreenSession`) in a separate, mechanical commit only. |
| `ScreenSessionSetup` / `SetupContext` | **Adapt** | No type step; resolve type internally; accept and show a focus; retire goals. |
| `SessionController` | **Adapt** | Add `focusConditionId` (QString, settable before `start()`), cleared by `endSession()`. |
| `SessionDiagnosticsModel` | **Reuse** | Call `declareFocus()` at session start. No change. |
| `WorkOnsController` | **Adapt** | Add `areaItems` (§5), computed at publish like `swingSummaryView`. |
| `Coaching/swing_areas.h` | **New** | Pure function `summariseAreas()` + unit tests. |
| `home/HmSwingAreas.qml`, `home/HmAreaRow.qml` | **New** | Paint-only, controller injectable (same pattern as `HmSwingSummary`). |
| `home/HmRigCapability.qml` | **New** | Paints `setupSummary()` for the currently connected kit. |
| `ScreenHome.qml` | **Adapt** | §2 → single Start; §3 → `HmSwingAreas`; §4 → `HmRigCapability` + PPCP entry; §5 unchanged. |
| `ScreenSwingDiagnostics` | **Adapt** | Becomes the Work-ons destination; add `groupFilter` so an area's "All of … →" opens it pre-filtered. |
| `DiagnosticModel` | **Adapt** | Host in its own screen (keep the Settings entry as a link to it). |
| `PpSessionReportList` | **Reuse** | Unchanged. |
| `PpMeter`, `PpChip`/`PpBadge`, `PpLink`, `PpPressable`, `PpMicro`, `PpDivider` | **Reuse** | Rating bar, state chips, links, rows, captions. |
| `HmTypeCard` | **Delete** (Phase 5) | |
| `ScreenPlaceholder` uses for Swing/GRF/Coach | **Delete** (Phase 1) | |
| `HmSwingSummary`, `HmFocus` | **Retire from Home** (Phase 4); delete or re-home in Phase 5 per D4 | |
| `HmGoesTogether` | **Keep** in Swing diagnostics | |
| `presets[].railIndex`, `presets[].goals`, `appSettings.sessionGoalsByType`, `appSettings.lastSessionType` | **Delete** (Phase 2) | Migrate settings silently (ignore unknown keys). |

## 5. The area model (Phase 3 contract)

### 5.1 Data published to QML

`WorkOnsController.areaItems : QVariantList`, NOTIFY `summaryChanged`, empty before the first
summary. One map per area, in display order (D2):

```
{
  group:       "posture",            // core.json condition.group
  label:       "Posture",            // display label (§5.3)
  measurable:  true,                 // false → row shows "not measured with this rig"
  score:       46,                   // 0–100, int; -1 when !measurable
  trend:       +5,                   // score change vs the comparison window (§5.2); 0 when unknown
  hasTrend:    true,
  topFault:    "Early extension",    // label of the highest-impact condition, "" when none
  workOnsActive:    1,
  workOnsNew:       1,               // see D3
  items: [                           // top N (default 5) conditions in the area by impact
    {
      conditionId:  "early_extension",
      label:        "Early extension",       // pack label (coach term)
      golfer:       "…",                     // pack golfer phrase, for tooltip/second line
      fired:        17, assessable: 22,      // WorkOnSummary.swingsFired/Assessable
      measureId:    "…", hasTypical: true,
      typical:      6.9, corridorLo: 0, corridorHi: 3.0, corridorShape: "…", unit: "cm",
      grade:        "Action",               // Grade of the typical value against its corridor
      workOn:       "active" | "easing" | "new" | "",
      sessionsListed: 3
    }, …
  ]
}
```

Everything above is available from `WorkOnSummary` + `WorkOnEntry` + the pack, **provided the
per-session record carries evidence for every assessable condition, not only listed ones.**
First task of Phase 3: confirm what `SessionWorkOns` holds (`work_ons.h` l.300ff, its
`WorkOnEvidence` list). If it holds only listed conditions, extend the record and bump its
fingerprint — the existing catch-up will re-derive every session on next look.

### 5.2 Score and trend (provisional — D1)

Start with something that needs **no new statistics**, so the UI can ship while the modelling
question is settled in the diagnostics docs:

- Per condition in the area (Movement and Delivery classes only; `WorkOnClass::Excluded` never
  counts): `p_c = WorkOnSummary.score` (already the Wilson lower bound on recency-weighted
  fired/assessable).
- `score = round(100 × (1 − max_c p_c))` — the worst well-evidenced fault sets the area. Honest,
  explainable ("your release scores 52 because the flip shows on at least 48 % of swings"), and
  insensitive to the number of conditions in the group (setup has 55, finish 3).
- `measurable = any condition in the group has swingsAssessable > 0`.
- Trend: the same score over the records excluding the newest *conclusive* session vs including
  it. `hasTrend = false` with fewer than 2 conclusive sessions.
- Impact order for items: `p_c` descending, ties by `sessionsListed`.

Write it as a free function so D1 can replace the rule without touching the controller or QML:

```cpp
// Coaching/swing_areas.h
struct AreaItem { /* as §5.1 items */ };
struct AreaSummary { QString group; bool measurable; int score; int trend; bool hasTrend;
                     QString topFaultId; int workOnsActive, workOnsNew; std::vector<AreaItem> items; };
std::vector<AreaSummary> summariseAreas(const std::vector<WorkOnSummary> &summaries,
                                        const CharacteristicPack &pack,
                                        const AreaOptions &opt = AreaOptions());
```

### 5.3 Areas and labels

| group | Label | Shown |
|---|---|---|
| setup | Setup | always |
| posture | Posture | always |
| lateral | Lateral motion | always |
| sequence | Sequence | always |
| armsAndClub | Arms & club | always |
| release | Release | always |
| impact | Impact | always |
| finish | Finish | always (often "not measured") |
| ballFlight | Ball flight | **only with launch-monitor data**; these are Outcome-class conditions, never work-ons |

Labels belong in the pack (add a `groups` table: `{id, label, golferLabel, order}`) rather than in
QML, so a pack author can rename them. Until then a C++ fallback table.

## 6. Phases

### Phase 1 — Navigation without modes

Goal: the rail and stack no longer know about modes. Behaviour otherwise identical.

- `navigation_controller.h/.cpp`: add `enum class Screen { Home, Session, Sessions, WorkOns,
  SwingModel, Athlete, Athletes, System, Settings, Developer, Wizard }` with Q_ENUM; `navigate()`
  etc. take it (keep an `int` overload only if a probe needs it). `blockedDuringSession()` allows
  `Session`, `System`, `Settings`. Remove the `activeSessionType() + 1` dependency.
- `Main.qml`: StackLayout children in enum order; delete the three `ScreenPlaceholder`s; one
  session screen (`ScreenWrist`); `screenNames` derived from the enum (or a map keyed by it).
  `onSessionStartRequested` navigates to `Screen.Session` instead of `presets[type].railIndex`.
  `onReviewSessionRequested` handlers (Home and Swing diagnostics) target `Screen.Session`.
- New hosts: `Screen.Sessions` → `ScreenAthleteForm` with `initialTab "sessions"` (reuse the
  existing `onAthleteSessionsRequested` block); `Screen.WorkOns` → `ScreenSwingDiagnostics`;
  `Screen.SwingModel` → a thin `ScreenSwingModel.qml` hosting `DiagnosticModel` (Settings panel 10
  becomes a link to it).
- `PpRail.qml`: buttons Home, Sessions, Work-ons, Swing model; `sessionLockIndex` → a bool
  `sessionLocked` plus the Session screen being the only enabled destination; Settings unchanged.
  Respect whatever `tst_rail_fold.qml` asserts about folding.
- Tests/probes to update: `tests/qml/tst_rail_fold.qml`, `tests/probes/diag_nav_survives_mode_switch.qml`,
  `tests/setup/tst_setup_nav.qml` + `fakes/FakeNav.qml`, and every probe that navigates by raw
  index (`grep -rn "navigate(\|currentPageIndex" src/Gui/tests`).
- Accept when: no integer screen literals remain outside the enum; a session can start, lock
  navigation, end and unlock; review-a-session from Home and from Swing diagnostics lands on the
  session screen; the full QML test suite passes.

### Phase 2 — Start a session without a type; carry a focus

**2a (audit, no code change):** classify every non-test use of `sessionType` /
`activeSessionType` (≈40 files: `Analysis/*`, `Metrics/*`, `Club/shaft_track_assembly`,
`Gui/review/*`, `Gui/shot/*`, `Gui/cameras/PpCameraTiles.qml`, `Gui/charts/PpChartsPanel.qml`,
`Gui/trust/PpTrustPanel.qml`…) as *capture profile* (keep), *UI* (remove) or *dead*. Record the
result in this document under §9. Decide the type the wizard resolves to: today every startable
session is `Type::Wrist`; if the audit shows the pipeline branches on type in ways that matter, the
resolver picks from rig capability, otherwise it is a constant.

**2b (code):**
- `ScreenHome`: `startSessionRequested(int)` → `startSessionRequested(var request)` where
  `request = { focusConditionId: "" | id }`; drop `openSessionScreenRequested`.
- `ScreenSessionSetup` / `SetupContext`: remove the type step and `presets` (keep the icon/name
  nowhere); add `property string focusConditionId` set by `open(request)`; show the focus on the
  first page with a change control (a picker over active work-ons + "open assessment").
  `sessionStartRequested(type, goals)` → `sessionStartRequested(request)`.
- `SessionController`: add `focusConditionId` (settable before `start()`, cleared in
  `endSession()`).
- `Main.qml` `onSessionStartRequested`: resolve type (2a), `navigate(Screen.Session)`,
  `beginSessionFolder`, `start(type)`, `startCapture()`, then `declareFocus(focus)` on the live
  session diagnostics model when non-empty.
- Retire `appSettings.sessionGoalsByType` and `appSettings.lastSessionType`.
- Accept when: Home has one Start; Practise (temporary button in Swing diagnostics until Phase 4)
  starts a session whose panel shows the focus from shot 1; no UI shows a session type.

### Phase 3 — The area model

- First: the record check in §5.1.
- New `Coaching/swing_areas.h` (+ `.cpp` if needed) implementing §5.2; unit tests beside the
  existing work-ons tests: empty library; one session; a group with no assessable condition;
  Excluded-class conditions ignored; ballFlight hidden without LM evidence; trend unknown with one
  conclusive session; ordering stable.
- `WorkOnsController`: `Q_PROPERTY(QVariantList areaItems READ areaItems NOTIFY summaryChanged)`,
  built when the summary publishes (same place `swingSummaryView` runs), from the in-memory
  summaries — no new file, no new fingerprint.
- Optional in this phase: pack `groups` table (§5.3) with loader + validator support.
- Accept when: tests pass; a probe prints `areaItems` for the developer library and the numbers
  match hand-computed values for two areas.

### Phase 4 — Home

- `home/HmAreaRow.qml`: header row (Pressable, `aria`-equivalent `Accessible.role: Button`,
  expanded state), item rows, footer link. Signals: `practiseRequested(conditionId)`,
  `addWorkOnRequested(conditionId)`, `allInAreaRequested(group)`.
- `home/HmSwingAreas.qml`: header (title, evidence line from `summarySubtitle`, "All work-ons"
  link), column headers, `Repeater` over `areaItems`; expands the weakest measurable area on first
  show; remembers expansion per athlete in `ViewLayout`/`appSettings` (existing per-mode state
  persistence pattern). Reading/empty/updating states copied from `HmSwingSummary`.
- `home/HmRigCapability.qml`: one row per `setupSummary()` group for the currently connected kit;
  extract the "facts from current devices" part of `SetupContext.qml` (l.587–625) into a small
  shared helper so Home and the wizard compute identical facts. Keep the PPCP pairing entry.
- `ScreenHome.qml`: §2 → Start (one button + club picker + last session line); §3 →
  `HmSwingAreas`; §4 → `HmRigCapability`; §5 unchanged. Wire `practiseRequested` →
  `startSessionRequested({focusConditionId})`; `allInAreaRequested` → new
  `swingDiagnosticsRequested(group)` → `ScreenSwingDiagnostics.groupFilter`.
- Probes to update/replace: `tests/probes/work_ons_home.qml`, `tests/probes/swing_themes_home.qml`;
  add `tests/qml/tst_swing_areas.qml` driving `HmSwingAreas` with a fake controller.
- Accept when: a screenshot of Home matches `02_home_weakest_expanded.png` in layout and
  proportion (compare side by side and list any differences in the PR); Appendix A.1/A.2 are
  reproduced in every theme (`theme_render` probe); keyboard can
  expand/collapse rows and reach every action; no figure appears without its unit and corridor.

### Phase 5 — Clean-up

Delete `HmTypeCard.qml`; delete or re-home `HmSwingSummary.qml`/`HmFocus.qml` (D4); remove dead
`Theme`/asset references (`qrc:/assets/tiles/*.png` used only by the type cards); remove the
`comingSoonTypes` logic in `ScreenHome`; update `home_themes_design.md` and
`session_wizard_refactor_design.md` to point here. Rename `ScreenWrist` → `ScreenSession` as a
separate mechanical commit.

## 7. Decisions needed

- **D1 — Area score rule.** §5.2 is a placeholder (worst-condition Wilson bound). Alternatives:
  prominence-weighted mean share; grade distribution with no single number. Belongs in the
  diagnostics design docs; the UI only needs `score` 0–100 and `measurable`.
- **D2 — Row order.** As approved: descending score, weakest at the bottom and expanded.
  Alternative: fixed address-to-finish order (setup, posture, lateral, sequence, arms & club,
  release, impact, finish) — more "holistic", less "steering". Make it one constant.
- **D3 — Do golfers adopt work-ons?** Today work-ons are derived (`Active/Easing/Cleared`), never
  adopted. Simplest mapping, no persistence: `new` = Active with `sessionsListed == 1`, label
  "New"; no Add/Make buttons. If adoption is wanted, add an athlete-level
  `<athlete>/work_on_choices.json` (`adopted`, `dismissed` sets) consulted only for display and
  ordering — never for detection.
- **D4 — "Your focus" and "What you do well".** The approved Home drops both: strengths are high
  areas, the focus is the Practise button. Keep the components for a possible session-start
  suggestion, or delete in Phase 5.
- **D5 — Rail width.** The current rail is an icon column with labels beneath; the approved
  mockup shows a wider labelled rail. Phase 1 keeps the current rail; widening is cosmetic and can
  follow.

## 8. Out of scope

Progress (cross-session trends) page; a redesigned Sessions page; drills and practice plans;
removing `sessionType` from the analysis pipeline; mobile layouts.

## 9. Phase 2a audit result

*(to be filled in by Phase 2a)*

---

## Appendix A — Mockups for implementation

**Look at these before writing any QML** (open them with the Read tool):

| File | Shows |
|---|---|
| `ux_refactor_away_from_4_modes/01_home_collapsed.png` | Home, every area collapsed |
| `ux_refactor_away_from_4_modes/02_home_weakest_expanded.png` | Home as first shown: the weakest area (Posture) expanded, with Practise on the active work-on and "New" on a newly listed one (D3 default) |
| `ux_refactor_away_from_4_modes/home_mockup.html` | The static page the PNGs were rendered from (1440 px wide, 2× scale) |

How to read them:

- The PNGs govern **layout, proportion, density, alignment and hierarchy**. Match them.
- The token table (A.3) governs **exact values**. The PNGs and the HTML use literal hex colours
  and a Google font; the app uses `Theme` tokens and the active theme's fonts, so colours will
  differ by theme. Never copy a hex value into QML.
- The rail in the PNGs is the **current** icon-over-label `PpRail` with the Phase 1 buttons (D5),
  not a new component.
- All figures are illustrative.

Dimensions are design pixels at `Theme.sp(1) = 1`; use `Theme.sp()` and theme tokens, never hex.
Content max width: `Theme.contentWidth(width)` as other screens.

### A.1 Home, first view

```
┌──────┬───────────────────────────────────────────────────────────────────────────────────────┐
│ (ML) │ ┌─ START ─────────────────────────────────────┬─ WHAT THIS RIG CAN MEASURE ─ Rig ▸ ─┐ │
│      │ │ NEW SESSION                                  │ ● Body motion & sequence  FO·DTL·150│ │
│ Home │ │ Hit some shots                               │ ● Shaft & swing plane     FO·DTL    │ │
│ Sess │ │ Every shot is assessed against the swing     │ ● Pelvis & thorax         2 × IMU   │ │
│ Work │ │ model.                                       │ ● Ground force (est.)     IMU+video │ │
│ Model│ │ [▶ Start session]  Club: 7 iron ▾            │ ○ Lead wrist          Connect sensor│ │
│      │ │ Last session Mon 26 Oct · 22 shots           │ ○ Ball launch     Connect launch mon│ │
│      │ │                                              │ ▯ Add iPhone as a camera            │ │
│      │ └──────────────────────────────────────────────┴─────────────────────────────────────┘ │
│      │                                                                                         │
│      │ Your swing by area   from 180 swings over 6 sessions             All work-ons ▸         │
│      │ ┌───────────────────────────────────────────────────────────────────────────────────┐ │
│      │ │    Area           Rating                 Score  Trend  Biggest fault     Work-ons │ │
│      │ │ ▸  Setup          ██████████████████▒▒    88     +2    Ball position             │ │
│      │ │ ▸  Sequence       █████████████████▒▒▒    81     +1    —                         │ │
│      │ │ ▸  Finish         ████████████████▒▒▒▒    79     ±0    —                         │ │
│      │ │ ▸  Lateral motion ███████████████▒▒▒▒▒    74     ±0    Sway off the ball         │ │
│      │ │ ▸  Release        █████████████▒▒▒▒▒▒▒    63     −1    Flip                      │ │
│      │ │ ▸  Impact         ████████████▒▒▒▒▒▒▒▒    58     +3    Hands behind ball         │ │
│      │ │ ▸  Arms & club    ███████████▒▒▒▒▒▒▒▒▒    52     −2    Over the top     1 active │ │
│      │ │ ▾  Posture        █████████▒▒▒▒▒▒▒▒▒▒▒    46     +5    Early extension  1 active │ │
│      │ │      (expanded — A.2)                                                             │ │
│      │ │    Ball flight    not measured with this rig · Connect launch monitor             │ │
│      │ └───────────────────────────────────────────────────────────────────────────────────┘ │
│      │                                                                                         │
│      │ Recent sessions                                                     All sessions ▸     │
│      │ (PpSessionReportList, unchanged)                                                        │
│  ⚙   │                                                                                         │
└──────┴───────────────────────────────────────────────────────────────────────────────────────┘
```

Values are illustrative. Area names follow §5.3 (pack groups), not swing phases.

### A.2 An expanded area

```
│ ▾  Posture        █████████▒▒▒▒▒▒▒▒▒▒▒    46     +5    Early extension  1 active          │
│      Early extension       17/22   pelvis → ball 6.9 cm · ≤ 3.0     Action   [▶ Practise] │
│      Hip stall              9/22   pelvis rotation velocity P6       Watch    New          │
│      Loss of posture        8/22   spine angle change P4→P7 7°       Watch                 │
│      All of posture ▸                                                                      │
```

- Item columns: label (pack `label`; tooltip = `golfer` phrase) · `fired/assessable` · typical vs
  corridor (omit the number when `hasTypical` is false — mixed-club sessions) · grade word · action.
- Grade words and colours: Ideal/Good → `Theme.colorRagGood`; Watch → `Theme.colorRagWatch`;
  Action → `Theme.colorRagFault`; NotMeasured → `Theme.colorText3`. Colour is never the only
  channel: the word is always printed (as `HmSwingSummary` does today).

### A.3 Tokens and sizes

| Element | Spec |
|---|---|
| Section titles | as `PpDisplayText`/`PpMicro` on Home today |
| Area row | height 48, padding 18 h; hover `Theme.colorBg2`; divider `Theme.colorBorder` |
| Rating bar | `PpMeter` or 6-px track: track `Theme.colorBg3`; fill `Theme.colorAccent` when score ≥ 60, `Theme.colorRagWatch` 45–59, `Theme.colorRagFault` < 45 (thresholds = constants, D1) |
| Score | `Theme.fontData`, 15 |
| Trend | `Theme.fontData`, 12; + `Theme.colorGood`, − `Theme.colorWarn`, ±0 `Theme.colorText3` |
| Work-ons count | 12, semibold; active `Theme.colorAccent`, new `Theme.colorAttention` |
| Item row | height 40, indent 62, top divider |
| Practise | `PpButton` secondary with play glyph |
| Capability row | 8-px dot: filled `Theme.colorAccent` measured, half `Theme.colorAccentMid` estimated, outline `Theme.colorText3` unavailable; device label `Theme.fontData` 11 `Theme.colorText3` |

### A.4 Wizard, first page with focus

```
┌─ New session ───────────────────────────────────────────────┐
│ Focus   Early extension                         [change ▾]  │
│         (open assessment · Early extension · Over the top)  │
│ Club    7 iron ▾                                            │
│ … existing setup steps (devices, mounts, calibration) …     │
│                                                [Start ▶]    │
└─────────────────────────────────────────────────────────────┘
```
