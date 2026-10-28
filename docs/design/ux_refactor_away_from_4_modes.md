# UX refactor away from 4 modes

Status: **Phases 1–4 implemented** (commits `785f1c2f`, `a45ffdf0`, `2cab28f2`, `f24da90d`,
`20615b8e`). **Phase 5 is done but not yet committed**, apart from the `ScreenWrist` →
`ScreenSession` rename, which is left for its own mechanical commit after Mark's review.
Written 26 Oct 2026 against `91c6cac4`. A research
pass on 27 Oct at `ab5c4d9a` checked every claim against the code and corrected this document;
its findings are folded in below (§3 and §9 hold the evidence). Since `91c6cac4`, only
`src/Gui/setup/` has changed among the files Phase 1–2 touch (trunk-IMU commits `c154d3cf`,
`b3f7502f`). Before starting a phase, run `git log ab5c4d9a..HEAD -- <the phase's files>` and
re-check any line numbers it cites.

How to use this document with Claude Code: **one phase per fresh session.** Each phase lists its
goal, the files it touches, what it reuses, what it adds, the tests that must change, and its
acceptance checks. Phases are ordered so that each one ships on its own and leaves the app working.
In each session the orchestrator holds the contracts, runs the builds (2–3 at most) and the gates,
and reviews every diff; Opus agents write the code, one work item per agent, in parallel only on
disjoint files. Commits come at the end of the phase, after Mark has judged the results.

Visual reference (for humans): the Design canvas "PinPoint Studio — UX restructure", board
**1a · Home — Coaching tab** and **1b · Home — Your swing tab** —
https://claude.ai/code/artifact/06edc700-742b-4dd3-9f5e-01136988bced. Claude Code
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

Home today stacks an identity row, a start-session card grid (4 types), YOUR SWING (focus,
do-well, next-on-list), device readiness and pairing, and Sessions; the rail carries four obsolete
mode buttons.

## 2. Target design

### 2.1 Concepts

| Concept | Meaning | Already in code as |
|---|---|---|
| **Session** | One sitting of shots. Always assessed against the swing model. Optionally carries a **focus**. | `SessionController`, session folder, `diagnostics.json`, `work_ons.json` |
| **Focus** | "Working on X" for this session: one condition id. Changes what the session panel leads with; never changes detection or grading. | The **focus contract**, `session_diagnostics_design.md` §A6 / C2.7: `SessionDiagnosticsModel::declareFocus()`, `focusConditionId`, `focusFromShot`; persisted as `intent` in `<session>/diagnostics.json` |
| **Work-on** | A fault the golfer keeps producing, derived across sessions. | `WorkOnsController.items`, `aggregateWorkOns()`, `WorkOnStatus {Active, Easing, Cleared}` |
| **Area** | A group of conditions (setup, posture, sequence…) rated as a whole. **New view, no new detection.** | `condition.group` in `src/Resources/diagnostics/core.json`; `ConditionGroup` enum + `conditionGroupLabel()` (`Diagnostics/engine/characteristic.cpp` l.82–92) |
| **Rig capability** | What the connected kit can measure, by metric group. | `MetricCatalog.setupSummary()` → `summariseSetup()` (`Metrics/setup_capability.h`); wording in `setup/setup_capability_rows.js` |

**Session type stays, but goes internal, as the constant `Wrist` (1).** The Phase 2a audit (§9)
found no use of the type that changes what is captured, and every saved session folder (20 live,
577 corpus) is `_Wrist_`. So every session starts as `Wrist`, from one default in
`SessionController`. The UI stops showing the type. The four places where analysis still chooses
by type are tracked separately as #111 (with bug #110); they are out of scope here (§8).

### 2.2 Information architecture

```
Rail (top → bottom)            Destination (Phase 1 reuses existing screens)
───────────────────            ─────────────────────────────────────────────
Golfer avatar / switch         ScreenAthletePicker / ScreenAthleteForm (unchanged)
Home                           ScreenHome (rebuilt in Phase 4)
Session   (only while running) ScreenWrist — the one session screen
Sessions                       a second ScreenAthleteForm instance, initialTab "sessions"
Work-ons                       ScreenSwingDiagnostics (its FAULTS card is the work-ons list)
── spacer ──
Settings                       ScreenSettings (System/Developer stay beneath it, as today;
                               Reference → Swing model is the DiagnosticModel editor, slot 10)
```

- No mode buttons. No Progress page yet (out of scope; leave room in the rail).
- A **live session** is one screen (today's `ScreenWrist`). While it runs, a **Session** rail
  button appears (*decided 27 Oct*, D7); it is the way back to the session from anywhere the lock
  allows, as the active mode button is today.
- While a session runs, navigation is locked to **Session, System and Settings**. Settings stays
  open, and that is how the **Swing model** is reached mid-session: authoring a corridor against
  the live swing (`DiagnosticModel.qml` l.54–58) depends on it.
- **Swing model** is the DiagnosticModel editor's name wherever users see it (it was "Diagnostic
  Model"). It stays in Settings → Reference (slot 10), and it has **no rail button** (*decided
  28 Oct*, D14).
- **Sessions** is muted when no golfer is selected; with no athlete the form would open as a blank
  "New athlete" form.
- The Developer screen (`PlayPage`) stays unreachable, as today.

### 2.3 Home (the approved layout)

Three sections, top to bottom (wireframe in Appendix A.1):

1. **Start** — left: "Hit some shots", one-line explanation, **Start session** (opens the
   existing wizard with focus = open assessment), the club picker that exists today, and a
   "last session" line. Right: **What this rig can measure** — one row per capability group,
   live/estimated/missing, and the existing "Add iPhone as a camera" (PPCP pairing) entry, the
   phone-offer list (`PpcpOfferList`) and the device warning notice (`RmWarningNotice`). Gap rows
   **describe what is missing** ("No wrist sensor"), never instruct — the rule in
   `setup_capability_rows.js`, which stays the one place this wording lives.
2. **Two tabs: Coaching | Your swing.** One tab bar (title-sized labels, underline on the
   selected tab, the `summarySubtitle` evidence line and "All work-ons ▸" on the same baseline).
   Both tabs read the same `workOns` controller and the same evidence; they answer different
   questions, so neither replaces the other.

   **Coaching** — *what to do next*, in the golfer's words. Today's YOUR SWING content (the
   same `workOns` strings: `focusItem`, `doWellItems`, `nextItems`), **redrawn in the new visual
   language** (§2.5), not the current card style. See `03_home_coaching.png`:
   - **Your focus** card: header row (focus icon · YOUR FOCUS · title · short reason in one line ·
     **Practise**); below it two side-by-side tables, *Aim for* (tick rows) and *Right now* (fault
     rows: text · how-often meter · frequency words + trend tag · session pips); then a collapsed
     expand row **"Why it matters · how to practise"** holding the two paragraphs. The long text is
     no longer shown by default.
   - **What you do well** and **Next on your list**: two table cards side by side, same row
     grammar (mark · text with frequency words beneath · how-often meter · session pips); "Next"
     tag on the first item; trend tags (↑ growing / ↓ easing) beside the frequency words.
   - No figures, no P-positions, no claims about cause — the golfer layer's rules from
     `home_themes_design.md` still hold on this tab; only the presentation changes.

   **Your swing** — *how the whole swing stands*, by area. The rest of this item describes it.
   This tab **does** show pack labels and figures; `home_themes_design.md`'s "no figures on Home"
   now applies to the Coaching tab only.
   One row per area, collapsed by default except the
   weakest. Columns: chevron · area · rating · score · trend · biggest fault · work-ons count
   (the rating and score columns depend on D1, §7).
   Expanding a row lists that area's conditions (top 3–5 by impact) with: name · share of swings
   (`fired/assessable`) · typical value vs corridor · grade · one action:
   - an active work-on → **Practise** (opens the wizard with focus = that condition);
   - a newly listed work-on → **Add work-on** *(see decision D3)*;
   - any other Watch/Action item → **Make work-on** *(D3)*;
   - a footer link **"All of <area> →"** opens Swing diagnostics filtered to that group.

   It steers to the area of most need while showing the whole swing ("your setup is nearly
   perfect; the work is in your release").

   Default tab: **Coaching** (D6); the last tab chosen is remembered per athlete.
3. **Recent sessions** — the existing `PpSessionReportList`, unchanged in Phase 4 (D13). Its rows
   may say "Open assessment" or "Focus · <name>" from `diagnostics.json` `intent`; no session type
   is needed.

Removed from Home: the 4-type card grid (`HmTypeCard`), the "PinPoint Studio" hero, and the
device-readiness rows (folded into the capability panel). The identity row and the first-run
onboarding (no athlete yet) stay until D12 is settled. YOUR SWING stays, as the Coaching tab.
Naming: the old section heading "Your swing" now names the area tab; the summary's tab is
"Coaching".

### 2.4 Starting a session

- Every entry point — Home's Start session, every Practise button — opens the **existing
  wizard** (`ScreenSessionSetup`). The wizard has no type step today: the type is picked on Home
  (the `HmTypeCard` grid), and inside the wizard it only drives the header, a refusal for
  "coming soon" types, and the **Goals** step. So this change removes Home's grid, the wizard's
  `presets` and the Goals step.
- The wizard gains an optional **focus**, carried in from the caller and shown on its first page
  ("Focus: Early extension · change"). The picker lists open assessment, the Coaching focus
  condition, and the active work-ons (`workOns.items`: `id` = condition id, `name` = label).
  The Coaching focus has to be offered separately because it need not be a work-on: on Mark's
  library the focus is `trail_knee_straighten`, which has never been listed.
- On start: `sessionController.focusConditionId = <draft focus>` then
  `sessionController.start()` (the default type). **`SessionController` holds the focus for the
  live session.** The diagnostics panel adopts it when it activates on the live session folder.
  This is needed because the live `SessionDiagnosticsModel` belongs to its panel. That panel is
  off in every default layout and is rebuilt on every Capture↔Replay switch, and the
  second-screen window has a second model. A `declareFocus()` called once at start would be lost
  or overwritten (§3, "Focus delivery").
- Mid-session focus changes keep using the panel's `_declareFocus` path
  (`PpSessionDiagnosticsBody.qml` l.123), which also writes `sessionController.focusConditionId`.
  That way a change survives panel rebuilds and reaches the second window.
- Session **goals** (`presets[].goals`, `appSettings.sessionGoalsByType`) are retired. Nothing
  outside the wizard and its tests ever read them, and they never reached the session folder.
  "General assessment" becomes "no focus".
- The toolbar's **Capture** button (`PpSessionToolbar.qml` l.205–210) also starts sessions, with
  no wizard and no focus. It uses the same default type. Its "extend today's session" path keeps
  whatever intent that folder's `diagnostics.json` already holds.

### 2.5 Visual language (applies beyond Home)

> **This is a new aesthetic, not a restyle of one screen.** Table cards with hairline rows,
> expandable rows, title-size text tabs, the 10-segment how-often meter, trend tags, monospace
> figures and quiet section chrome replace the current card-and-pill look (compare
> `coaching_existing.png` with `03_home_coaching.png`). It will extend across the app — session,
> review, work-on and settings screens — so Home is only its first user.
>
> **Consequence: shared GUI components and theme tokens may need to change, not just be
> wrapped.** `PpCard`, `PpMeter`, `PpPips`, `PpChip`, `PpBadge`, `PpTabs`, `PpMicro`/
> `PpDisplayText` and `Theme.qml` are used well beyond Home, and the **seven** theme aesthetics
> (instrument, editorial, studio, vector, terrain, links, folio; 14 light/dark renders) must all
> carry the new look. Changing a shared component therefore changes other screens. Treat that as
> intended, but make it visible: every change to an existing shared component or token is listed
> in its PR with the screens it affects, and checked with before/after `theme_render` captures
> and the components gallery (Phase 4a) in every theme.

Its parts are built once as shared components in `src/Gui/components/` (or `src/Gui/typeset/`
for `PpTabs`, which already lives there). Both Home tabs and the area table use only these. The
research found that four of the eight pieces already exist. The "Build as" column applies the
rule below; building them as new files would fork.

| Piece | What it is | Build as | Today's users (blast radius) |
|---|---|---|---|
| Table card | Bordered card on a `colorBg2`-level ground with an optional header row of column captions; rows separated by hairlines | **Update `PpCard`** to be the table card (header captions, hairline rows, full-bleed rows, no tone rule). **Do not change `PpCardShell` defaults or `Theme.cardRule`**: they are the ground of every session stage panel. Give `PpCard` its own ground or a `PpCardShell` variant. | `PpCard`: HmSwingSummary, HmGoesTogether |
| Table row | Fixed-height row (48; 56 with a second line) on a column template shared with its card's header | **New `PpTableRow`** | — |
| Expand row | Table row with a chevron that rotates 90° and reveals a child block; keyboard-operable; `Accessible.role: Button`, expanded state exposed | **New `PpExpandRow`**, lifting HmWorkOns' drawn chevron (l.290–305). Migrate HmWorkOns onto it in the same change so a second expand look does not live on. | about 6 ad-hoc expanders; HmWorkOns migrates |
| How-often meter | 10 segments, 7×6, 2-px gap; fault or strength colour; always paired with frequency words | **Update `PpMeter`**. It already is the 10-segment how-often meter; only the geometry moves, via tokens. | HmSwingSummary, HmFocus, HmWorkOns |
| Rating bar | 6-px track with fill by score band | **Extend `PpMeter`** with `segments` (0 = continuous track), or a new `PpRatingBar` if D1 keeps a single score. Shape depends on D1. | — |
| Trend tag | Pill: "↑ growing" (warn), "↓ easing" (accent) | **Update `PpChip`'s `trend` look.** It already produces these words. Scope any font change to the trend case. The numeric "+5 / −2 / ±0" in the area table is a coloured figure drawn with `Theme.trendTone(n)`, not a component. | `PpChip` has 12 users; the trend case only HmSwingSummary, HmFocus, HmGoesTogether |
| Session pips | One dot per judged session, latest `maxPips`, hollow for a session that did not show it | **Update `PpPips`**: add `maxPips`, which replaces the duplicated `latestPips()` in HmSwingSummary/HmFocus; size and gap become tokens | HmSwingSummary, HmFocus, HmWorkOns |
| Tab bar | Title-size text tabs with a 2-px accent underline, aside text and a right-aligned link on the same baseline | **Update `src/Gui/typeset/PpTabs.qml`**. It already has the underline, link (`chip`) and aside (`note`). Add a `size: "title"` variant and an accent underline, and put the aside after the tabs. Home creates a `PpTypeset` instance for it, as `ScreenAthleteForm` does. | PpChartsRegister, PpDiagReplay, PpDiagAnalyse, ScreenAthleteForm, PpLmGraphicsBody |
| Row marks | Bare ✓; filled dot (strength); ring (fault) | **Add kinds to `PpBadge`** (`dot`, `ring`, and a no-fill flag). Do not restyle `check`/`target`, which camera, IMU and toolbar status use. | 12 files |

Section captions in the mockup ("Aim for", "How often") are sentence-case quiet labels. Draw them
with a caption role (or PpTypeset's `fLabel`) rather than restyling `PpMicro`, which has 40 users.

The decision rule, applied per piece and recorded in the Phase 4a PR:

1. **Update the existing component** when it is the same idea in the old look. Preferred: the old
   look then retires everywhere at once rather than two looks living side by side.
2. **Add a new component** only when no existing one is the same idea (`PpExpandRow`,
   `PpTableRow`).
3. **Never fork** a near-copy of an existing component for Home alone.

Theme tokens: every colour, font and helper this document names already exists (`colorRagGood/
Watch/Fault` are aliases of `colorGood`/`colorAttention`/`colorError`). **Every geometry token is
missing.** That covers row heights, row padding, item indent, hairline colour, table-card radius,
segment and pip size/gap, rating-track height, tab underline and tab title size, trend pill font,
and capability dot size. Add them through `_v(key, default)` in `Theme.qml`, so every aesthetic
is defined and a theme overrides only where it differs. Update `tst_theme_tables.qml` where an
existing value moves, and list every moved value in the PR. Whether the new components draw from
`Theme` tokens or from `PpTypeset` roles (which already define hairlines, inks and figure fonts
for the typeset system) is D11: one source, not two.

## 3. What the code has today (findings)

Line numbers are at `ab5c4d9a`.

| Area | Where | Notes |
|---|---|---|
| Screen indices | `src/Gui/shell/Main.qml` l.553–564, `screenNames` l.566–580 | Home 0, Swing 1, Wrist 2, Grf 3, Coach 4, Play (Developer) 5, NewAthlete 6, Athletes 7, System 8, Settings 9, Wizard 10, SwingDiagnostics 11. |
| Session ↔ screen coupling | `Main.qml` l.596–599, `navigation_controller.cpp` l.48–53, `SetupContext.presets[].railIndex` | The session screen is `activeSessionType + 1`, and the lock allows 8, 9 and that index. **Main.qml also uses the type for the session screen at:** l.368–369 (`sessionScreenActive` range, which gates the DETECT cluster, the cast and `ballCueEnabled`), l.211–213 (quit-dialog text), l.694 (`openSessionScreenRequested` → `navigate(i+1)`, whose only targets are the placeholders). |
| **Hidden `+1` gates** | `session/ScreenWrist.qml:48` (+ `onCurrentIndexChanged` l.89–91), `charts/PpChartsPanel.qml:88`, `review/PpDataViewer.qml:38`, `review/PpReplayCharts.qml:38`, `cameras/PpCameraTiles.qml:60`, `trust/PpTrustPanel.qml:39`, `session/ScreenSessionMode.qml:51` (dead file) | Each panel decides "my screen is showing" with `currentIndex === sessionType + 1`. Under a reordered stack, Wrist + 1 points at another screen: the panel silently stops updating, or updates while hidden. Tests `tst_charts_panel.qml` (l.129, 432, 435, 531, 534) and `tst_chart_presets.qml:148` drive this gate. |
| Rail | `shell/PpRail.qml` l.138–198 (mode buttons), `_modeMuted` l.42–44, Settings l.213–222 | Literal indices 0–4, 8, 9, 11 and **5** (Developer, l.219). Home is also highlighted on Swing diagnostics (`|| 11`). |
| NavigationController | `shell/navigation_controller.h/.cpp` | A context property only (`main.cpp` l.259, l.953). It has no QML type and is not in `PP_QML_SOURCES`, so the QML test modules cannot see an enum declared on it (§6 Phase 1). API: `navigate`, `navigateRail` (0 = Home clears history), `navigateHome`, `back`, `forward`; properties `currentIndex`, `canGoBack`, `canGoForward`, `sessionLocked`. No C++ unit test. `m_athletes` is unused. |
| Sessions host | `Main.qml` l.724–749 `athleteFormScreen` | One `ScreenAthleteForm` instance serves New/Edit athlete; Sessions needs its own (§2.2). Its Sessions tab has its own review path (`onReviewSessionRequested`, l.741–744). |
| Settings panel indices | `settings/ScreenSettings.qml` l.75–80 (`panels`), l.270, l.296 (`navIdx` 11, 12); `SettingsIndex.qml` `panelIndex: 10` ×8 (l.350–390), `panelIndex: 11` ×12 (l.452–496) | DiagnosticModel stays in slot 10 (D14), so Studio keeps 11. Four positional lists move together if a slot ever moves (see the warning at l.263–279). `MetricRoute` (Main l.770–776) has no producer today. |
| Session types | `session/session_controller.h` l.55–56 | `enum class Type { None=-1, Swing, Wrist, Grf, Coach }`, Q_ENUM. The value 1 is on disk (`capture.sessionType`), in folder names (`_Wrist_NN`) and in settings keys (`"1:<mode>:…"`). |
| Wizard | `setup/SetupContext.qml` l.92–131 (`presets`), `SetupSteps.qml` l.53–63 (`goals` step), `SetupFlow.qml` l.323–328 (comingSoon refusal), l.344 (`startRequested`), `ScreenSessionSetup.qml` l.63 (`sessionStartRequested(int sessionType, var goalKeys)`), l.245–276 (preset icon + name header) | No type step. Lint rules: **W8** (no step may read the type or preset), **W10** (the shell names no step key — so do not call the new step key `focus`, which collides with QML's `focus`/`activeFocus`; use `intent`), W3, W5. |
| Session start | `Main.qml` l.684–690 (Home → wizard), l.790–805 (wizard → session) | Saves goals and last type, navigates to the session screen **before** `start()` (afterwards the lock forbids it), `beginSessionFolder`, empties the carousel, `start(type)`, `startCapture()`. Cancel (l.780–786) clears `activeClub`. |
| SessionController | `session_controller.h` l.42–48, cpp l.44–122 | Persists nothing. `activeClub` (settable before `start()`, cleared in `endSession()` at cpp l.120) is the precedent for `focusConditionId`. Unit tests: `session_clock_test`. |
| **Focus delivery** | `session/SessionMode.qml:54`, `diagnostics/PpSessionDiagnosticsPanel.qml` l.90–201, `session_diagnostics_model.cpp` l.1240, 1430–1449, 1943–1972 | `SessionMode.sessionDiagnostics` is **null unless the panel is on**, and no default layout includes it. Each panel owns its model and rebuilds it on every Capture↔Replay switch. The cast window (`shot/PpSessionDiagnosticsWindow.qml`) has **a second model**, and either model's `persist()` can overwrite the other's intent (an existing defect). `activateSession()` wipes the focus. `declareFocus()` sets the split at the current row count, with no explicit shot. |
| Home | `home/ScreenHome.qml` (826 lines) | Sections (line ranges): §1 identity + first-run onboarding (l.167–403); §2 launcher: hero, `HmTypeCard` grid, club chips, Start (l.405–559); §3 `HmSwingSummary` (l.561–577); §4 devices: PPCP pair button, device rows, `PpcpOfferList`, `RmWarningNotice` (l.579–782); §5 Sessions (l.784–823); plus `PpTopoBackground`. Signals: `startSessionRequested(int)`, `openSessionScreenRequested(int)`, `swingDiagnosticsRequested`, `reviewSessionRequested`, `athleteSessionsRequested`, `studioSettingsRequested`, plus athlete signals. |
| YOUR SWING | `HmSwingSummary.qml`, `HmFocus.qml`; `HmGoesTogether.qml` is on Swing diagnostics | Paint-only; every string from `workOns`. Data present: `focusItem.rightNow[]` and `nextItems[]` carry `{text, share, frequency, trend, sessions}`. **Missing:** `share` on `doWellItems` (the C++ `DoWellRow.share` exists); `focusConditionId`; a short focus reason for the one-line header; a "last session" accessor. Design: `home_themes_design.md`. |
| Work-ons | `Coaching/work_ons.h`, `diagnostics/work_ons_controller.h/.cpp` | `reduceSessionWorkOns` (l.430–436) writes a `WorkOnEvidence {id, state, fired, assessable, excess}` row for **every assessed condition**, of every class. On Mark's library that is 126 ids, against 13 ever listed. `aggregateWorkOns` (l.661–666) builds summaries for **listed ids only**. Measure, typical and corridor are on listed entries (`WorkOnEntry` l.282–287) only. `kWorkOnSchemaVersion = 2` (l.75), `kWorkOnRuleVersion = 4` (l.82). |
| Work-ons publishing | `work_ons_controller.cpp` `publish()` l.378–517, `publishSummary()` l.744–810 | Two GUI-thread publish points. `publish()` runs the aggregate and emits `itemsChanged`. `publishSummary()` runs `swingSummaryView` when the swing themes land, seconds later, and emits `summaryChanged`. `FocusItem.conditionIds` exists in C++ (`swing_themes.h` l.2126–2136) but is not published. |
| Condition groups | `src/Resources/diagnostics/core.json` | `group` on every condition: setup 55, armsAndClub 29, sequence 21, ballFlight 20, posture 19, impact 15, lateral 13, release 7, finish 3 (182 conditions). Groups are a closed enum. All 20 ballFlight conditions are Outcome class. |
| Grades | `Diagnostics/norms/norm.h` l.78 | `enum class Grade { Ideal, Good, Watch, Action, NotMeasured }`. **No function grades a stored typical value.** `spreadCorridorFromRow` (`diagnostics/session_spread.h` l.144–180) rebuilds the edges from the stored band and is the nearest helper. `Theme.corridorTone()` maps Action to the fault coral (`tst_theme_bands.qml`). |
| Focus contract | `session_diagnostics_model.h` l.175–176, 341–342 | `focusConditionId`, `focusFromShot`, `declareFocus()`, `clearFocus()`: implemented, and persisted as `intent` in `diagnostics.json`. |
| Rig capability | `Metrics/setup_capability.h`, `review/metric_catalog.h` l.83–93, `SetupContext.qml` l.592–641 | `setupFacts` counts IMU roles only when calibrated **in this wizard visit** (always empty outside it), and cameras only when connected. Home needs its own "could measure" rule (D10). Wording: `setup_capability_rows.js` (six families; "describe, never instruct"; never "IMU"). |
| Swing model editor | `diagnosticmodel/DiagnosticModel.qml` | Hosted as Settings panel 10 (`ScreenSettings.qml` l.603). It is self-contained (its own `ModelBrowser`) and reads `appSettings`, `currentSwing` and `athleteController`. It exposes `showMetric`/`showCharacteristic`/`showMeasure`/`scrollToItem`. It stays there, renamed "Swing model" (D14). |
| Tab bar | `src/Gui/typeset/PpTabs.qml` | Already a tab bar with an underline, a right-hand link and an aside; used by five screens, including the athlete form's Details/Sessions. |
| Per-athlete UI state | — | **None exists.** The per-mode patterns (`viewLayoutByMode`, `sectionCollapse`) are keyed by mode or session type. |
| Sessions list | `ScreenAthleteForm` `initialTab: "sessions"` | Reached from Home's "Show all". |

## 4. Reuse, adapt, add, delete

| Component | Verdict | Change |
|---|---|---|
| `NavigationController` | **Adapt** | Named `AppScreen` enum (`src/Gui/shell/app_screen.h`) from a header-only carrier in `PP_QML_SOURCES`; lock rule = Session, System, Settings (Settings is the mid-session way to the Swing model); no `activeSessionType + 1`. |
| `Main.qml` StackLayout | **Adapt** | Order follows the enum; drop placeholders; one session screen; a second `ScreenAthleteForm` for Sessions. |
| The seven `+1` gates | **Adapt** | One shared "session screen is showing" test against `AppScreen.Session`. Each keeps its `sessionType` property, which still keys saved preferences (`"1:<mode>:…"`). |
| `PpRail.qml`, `PpRailButton.qml` | **Adapt** | Home/Session (while running)/Sessions/Work-ons; no Swing model button (D14); keep avatar, Settings, `attention`/muting behaviour. `PpRailButton` unchanged. |
| `ScreenWrist` | **Reuse** as *the* session screen | Rename (`ScreenSession`) in a separate, mechanical commit only (Phase 5). |
| `ScreenSessionMode.qml` | **Delete** (Phase 1) | Dead: listed in cmake, never instantiated. It carries a `+1` gate. |
| `ScreenSessionSetup` / `SetupContext` / `SetupSteps` / `SetupFlow` / `SetupDraft` | **Adapt** | Remove `presets` and the Goals step; add an `intent` (focus) step or first-page row; `open(request)`; `sessionStartRequested(request)`; header copy "New session". |
| `SessionController` | **Adapt** | Add `focusConditionId` (settable before `start()`, cleared in `endSession()`); one default type (`Wrist`) so the wizard and the toolbar cannot drift apart. |
| `SessionDiagnosticsModel` | **Adapt (small)** | An explicit focus start (e.g. `declareFocusFrom(id, fromShot)` or a pending focus applied at activation). Today's `declareFocus` uses the current row count, which is 0 only by accident on late adoption. |
| `PpSessionDiagnosticsPanel` | **Adapt** | On activating the live session folder, adopt `sessionController.focusConditionId`; `_declareFocus` writes it back. This also fixes the two-window overwrite. |
| `Coaching/work_ons.h` | **Adapt** | A second aggregate entry point over evidence ids (default unchanged); `WorkOnEvidence` gains measure, typical and corridor (schema 2→3). |
| `WorkOnsController` | **Adapt** | `areaItems` built in `publish()`; `focusItem.focusConditionId`; `doWellItems[].share`; short focus reason; `conditionLabel(id)` for the wizard. |
| `Coaching/swing_areas.h` (+ `swing_areas_pack.h`) | **New** | Pure, pack-agnostic `summariseAreas()` + unit tests; the pack marshalling sits in a thin `_pack.h`, as `work_ons_pack.h` does. |
| `home/HmSwingAreas.qml`, `home/HmAreaRow.qml` | **New** | Paint-only, controller injectable (same pattern as `HmSwingSummary`). |
| `home/HmRigCapability.qml` + a shared facts helper | **New** | Paints `setupSummary()` + `CapRows.rows()` for the kit Home "could measure" with (D10); the wizard keeps its draft gate. |
| `home/HmHomeTabs.qml` | **New** | `PpTabs` + `StackLayout` holding `HmSwingSummary` and `HmSwingAreas`; remembers the tab per athlete. |
| `ScreenHome.qml` | **Adapt** | §2 → single Start; §3 → `HmHomeTabs`; §4 → `HmRigCapability` + PPCP entry + offer list + warning notice; §5 unchanged. |
| `ScreenSwingDiagnostics` | **Adapt** | Becomes the Work-ons destination; add `groupFilter` so an area's "All of … →" opens it pre-filtered. |
| `DiagnosticModel` | **Adapt (rename only)** | Stays the one instance, in Settings → Reference (slot 10); shown as "Swing model" wherever users see it (D14). Reached mid-session through Settings. |
| `PpSessionReportList` | **Reuse** | Unchanged (D13). |
| `PpCard`, `PpMeter`, `PpPips`, `PpChip`, `PpBadge`, `PpTabs` | **Update** (Phase 4a) | Per the §2.5 table. |
| `PpExpandRow`, `PpTableRow` | **New** (Phase 4a) | Per the §2.5 table. |
| `PpLink`, `PpPressable`, `PpDivider`, `PpButton` | **Reuse** | Links, rows, hairlines; Practise = `PpButton { glyph: "▶" }`; Start = `PpButton { primary: true }`. |
| `HmSwingSummary`, `HmFocus` | **Adapt** — same controller bindings and states (reading / updating / nothing yet), new layout from the §2.5 components | `HmFocus` gains Practise (`practiseRequested(conditionId)`) and the collapsed why/how row. |
| `HmWorkOns` | **Adapt** | Moves onto `PpExpandRow`. |
| `HmGoesTogether` | **Keep** in Swing diagnostics | Takes the `PpCard`/`PpChip` changes. |
| AppSettings | **Adapt** | New `homeViewByAthlete` map (uuid → `{tab, expanded[], expandedSeen}`). |
| `HmTypeCard`, `app_tiles` resource | **Delete** (Phase 5) | |
| `ScreenPlaceholder` uses for Swing/GRF/Coach | **Delete** (Phase 1) | |
| `presets[].railIndex`, `presets[].goals`, `appSettings.sessionGoalsByType`, `appSettings.lastSessionType` | **Delete** (Phase 2) | `lastSessionType` is write-only today. Leave the old settings keys unread, or remove them once in the AppSettings constructor. |
| Dead session-type code (§9) | **Delete** (Phase 5) | `ShotContext::sessionType`, `MetricQuery::sessionType`, `ShotReplayController::shotContext`, the shot-marker type, `view*ByType`, `PpMarkupPanel.sessionType`, `SwingScorer`. |

## 5. The area model (Phase 3 contract)

### 5.1 Data published to QML

`WorkOnsController.areaItems : QVariantList`, built in `publish()` from the aggregate and stored as
a member. It notifies with **both** `itemsChanged` and `summaryChanged`, or with its own
`areasChanged`: `summaryChanged` alone would leave it stale until the swing themes land. It is
empty before the first publish. One map per area, in display order (D2):

```
{
  group:       "posture",            // core.json condition.group
  label:       "Posture",            // display label (§5.3)
  measurable:  true,                 // false → row shows "not measured with this rig"
  score:       46,                   // 0–100, int; -1 when !measurable   (shape depends on D1)
  trend:       +5,                   // score change vs the comparison window (§5.2); 0 when unknown
  hasTrend:    true,
  topFault:    "Early extension",    // label of the highest-impact condition, "" when none
  workOnsActive:    1,
  workOnsNew:       1,               // see D3
  items: [                           // top N (default 5) conditions in the area by impact
    {
      conditionId:  "early_extension",
      label:        "Early extension",       // pack label (coach term)
      golfer:       "…",                     // pack golfer phrase, for tooltip/second line ("" when absent)
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

**The record check is answered (27 Oct): the per-session record already carries evidence for
every assessed condition.** So:

- **No record change is needed for** `fired`, `assessable`, the score `p_c`, status, history,
  `sessionsListed` or the conclusive count. These need only a second aggregate entry point,
  either `WorkOnOptions::everyAssessed` or a sibling `aggregateConditions()`, whose id set also
  takes the evidence ids. The default must stay as it is, because it feeds the work-ons list.
- **A record change is needed for** `measureId`, `typical`, the corridor, and therefore `grade`,
  on unlisted items:
  - Extend `WorkOnEvidence` with `measureId`, `hasTypical`, `typicalValue`, `corridorLo/Hi` and
    `corridorShape`.
  - Factor the one-corridor median block (`work_ons.h` l.483–510) into a helper shared by entries
    and evidence.
  - Bump `kWorkOnSchemaVersion` 2→3. **Do not bump the rule version.** The schema bump makes the
    catch-up re-reduce each session from its `diagnostics.json` without re-detecting swings. A
    rule bump would also change the fingerprint and force the swing-themes recompute.
- **Typical value: median of fired readings or of all assessed readings?** This is a product
  call (D9). Fired-only gives no number for a quiet condition and grades Watch or Action by
  construction. All-assessed makes the grade meaningful (Ideal for a clean condition).
- **Grade.** Reconstruct it from the stored band, as the work-ons row text already does
  (`spreadCorridorFromRow`). Make it policy-aware, and colour it with `Theme.corridorTone(grade)`.
- **Unit:** `pack.measure(measureId)->unit`, as the controller does at l.472.
- **Class:** reclassify from the pack with the `sessionWorkOnsForPack` rule (`reportHold` →
  Excluded), not `isWorkOnListable`. Class is not stored on evidence.
- **`workOn`:** derive it only when `sessionsListed > 0`. A never-listed Pattern would otherwise
  read "active".

### 5.2 Rating and trend — **D1 decided: habit counts** (Mark, 28 Oct)

**The rule.** An area is rated by counting its conditions in four frequency bands. There is no
0–100 score. Per condition, `share` = recency-weighted fired / assessable (the work-ons
aggregate's `weightedFired / weightedAssessable`). The bands, as constants in `AreaOptions`:

- **habit**: share ≥ 0.5, the Coaching layer's own bar ("more than half your swings", the
  same threshold "Right now" uses);
- **occasional**: 0.15 ≤ share < 0.5;
- **clean**: share < 0.15 **and** Wilson upper bound < 0.35;
- **not yet clear**: everything else measured.

The row reads "4 habits · 12 clean" over a bar split into the four bands. Areas are ordered by
habits, then occasional, then the worst share. Trend = habits gained or cleared against the
same counts without the newest substantive session ("−1 habit"). Counted conditions: Movement
and Delivery classes. For ballFlight (all Outcome), its Outcome conditions are counted. Why:
every number traces to a named fault; one habit and four read differently (the worst-condition
rule floored both); clean conditions cannot dilute a habit (the mean rule could); and the habit
bar is Coaching's, so the two tabs cannot disagree. On Mark's library: Posture 4 habits,
Sequence 4, Lateral 2, Arms & club 2, Impact 1, Finish 1, Setup 0, Release 0.

**D9 decided: typical = median of ALL judged readings** for the evidence record, so a clean
condition shows in range and the grade means something. Work-on entries keep their fired-median.

The research numbers that led to this decision follow.

The provisional rule was `score = round(100 × (1 − max_c p_c))`, with `p_c = WorkOnSummary.score`
(the Wilson lower bound on recency-weighted fired/assessable), over Movement and Delivery
conditions. A re-implementation over Mark's 10 sessions (read-only, 27 Oct) gives:

| Area | Score | Without newest session | Worst condition (fired/assessable, times listed) |
|---|---|---|---|
| setup | 77 | 75 | `ball_too_far` 26/86, 1 |
| posture | 6 | 4 | `early_extension` 125/125, 0 |
| lateral | 7 | 6 | `trail_knee_straighten` 124/127, 0 |
| sequence | 5 | 5 | `arms_before_chest` 152/153, 4 |
| armsAndClub | 15 | 14 | `over_the_top` 118/126, 3 |
| release | 93 | 91 | `scooping` 4/10, 0 |
| impact | 16 | 14 | `hips_closed_at_impact` 117/127, 0 |
| finish | 30 | 21 | `off_balance_finish` 123/164, 5 |
| ballFlight | — | — | no Movement or Delivery conditions |

Under this rule, six of the eight scored areas read 5–16 (near-empty red bars), and trends are +0
to +9. One habit that fires on nearly every swing sets the whole area. That is honest, but it
steers weakly. Alternatives for D1:

- a grade mix with no single number;
- a prominence-weighted mean share;
- keep worst-condition and fix corridors that fire on every swing separately.

Whatever D1 picks, the implementation is a free function, so the rule can change without touching
the controller or QML:

```cpp
// Coaching/swing_areas.h — pack-agnostic; swing_areas_pack.h marshals the pack
struct AreaConditionInfo { QString group, label, golfer, unit; WorkOnClass cls; bool lmOnly; };
struct AreaItem { /* as §5.1 items */ };
struct AreaSummary { QString group; bool measurable; int score; int trend; bool hasTrend;
                     QString topFaultId; int workOnsActive, workOnsNew; std::vector<AreaItem> items; };
std::vector<AreaSummary> summariseAreas(const std::vector<SessionWorkOns> &sessions,
                                        const QHash<QString, AreaConditionInfo> &info,
                                        const AreaOptions &opt = AreaOptions());
```

Points any rule must pin (from the research):

- **Trend.** Compare the score with and without the newest **substantive** session
  (`SessionWorkOns::substantive()`), the same session for all areas. Dropping a session also
  re-ages the older ones; say so in the tooltip. `hasTrend` = at least 2 substantive sessions.
- **`measurable`** counts only Movement/Delivery conditions with `swingsAssessable > 0`.
  Excluded-class conditions do not count.
- **`topFault` needs a floor.** Otherwise `release` names `scooping` at p = 0.07.
- **Impact order** for items: `p_c` descending, ties by `sessionsListed`.
- **`workOnsNew` (D3):** prefer "listed only in the newest substantive session" over
  `sessionsListed == 1`, which marks something listed once, long ago, as new.
- **ballFlight:** its conditions are all Outcome, so no score is defined. Show it measured but
  unscored, or define an Outcome rule for this one group (part of D1).
- **Cost.** The aggregate runs on the GUI thread. `evidenceFor()` is a linear scan, so build a
  per-session `QHash` before libraries grow past tens of sessions.

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
| ballFlight | Ball flight | **only with launch-monitor evidence**: a ballFlight condition whose measures are all `lm.*` has `swingsAssessable > 0`. "Any ballFlight condition assessable" is not an LM test, because `top` and `sky` are also judged from the camera. These are Outcome-class conditions, never work-ons. |

Labels come from the existing C++ table `conditionGroupLabel()` (`characteristic.cpp` l.82–92),
which the model browser also uses. Change its "Lateral" to "Lateral motion". **Do not add a pack
`groups` table in this refactor.** Any pack edit changes the content stamp
(`session_diagnostics_model.cpp` l.812–823). That re-detects every swing in every library on the
next look and recomputes every work-ons fingerprint and the themes. Revisit it only bundled with
a pack change that forces a regrade anyway.

## 6. Phases

Paths below are repo-relative. QML tests are under `src/Gui/tests/qml/`, probes under
`src/Gui/tests/probes/`, the setup suite under `src/Gui/tests/setup/`. Run tests through ctest
only. `qml_ui` is in the default suite; `session_setup_ui` is opt-in
(`-DPP_SESSION_SETUP_UI_TEST=ON`, about 8.5 min). Probes are not in ctest: run them with
`--probe-qml` on a dev build.

### Phase 1 — Navigation without modes

Goal: the rail and stack no longer know about modes. Behaviour otherwise identical.

- **Enum carrier.** Add a header-only `AppScreen` enum (`src/Gui/shell/app_screen.h`, an uncreatable QObject;
  named so because QtQuick already registers `Screen`) to `PP_QML_SOURCES`, so `qml_ui` and `session_setup_ui`
  can load QML that names `AppScreen.Session`. Values: `Home, Session, Sessions, WorkOns,
  Athlete, Athletes, System, Settings, Developer, Wizard`. Home stays first:
  `navigateRail(Home)` clears history.
- **`navigation_controller.h/.cpp`.** `navigate()` and the other methods take a `Screen`. QML
  passes ints anyway, so the probes keep working. `blockedDuringSession()` allows Session,
  System and Settings (Settings is how the Swing model is reached mid-session). Remove the
  `activeSessionType() + 1` dependency, and the unused `m_athletes`. Add a small C++ unit test
  for the lock rule.
- **`Main.qml`:**
  - StackLayout children in enum order; delete the three `ScreenPlaceholder`s; one session screen
    (`ScreenWrist`); `screenNames` keyed by the enum.
  - `sessionScreenActive` (l.368–369) and the quit-dialog text (l.211–213) use `AppScreen.Session`.
  - `onSessionStartRequested` navigates to `AppScreen.Session` instead of `presets[type].railIndex`.
  - Every `onReviewSessionRequested` targets `AppScreen.Session`: Home, Swing diagnostics, and the
    athlete form's Sessions tab.
  - `onOpenSessionScreenRequested` (from Home's coming-soon cards) becomes a no-op until Phase 2
    removes the signal.
- **The seven `+1` gates** (§3) compare against `AppScreen.Session` through one shared helper.
  Delete `ScreenSessionMode.qml` (dead) and its cmake entry. The panels keep their `sessionType`
  property for preference keys.
- **New hosts:**
  - `AppScreen.Sessions` → a **second** `ScreenAthleteForm` instance with `initialTab "sessions"`
    and the current athlete. It has its own review wiring and header name.
  - `AppScreen.WorkOns` → `ScreenSwingDiagnostics`.
- **Settings.** Slot 10 keeps the one `DiagnosticModel`, renamed **Swing model** wherever users see
  it: its title, the Settings nav row, the `SettingsIndex` search hits and Appearance's "Swing
  model → Metrics". No screen of its own and no rail button (D14). The deep links are unchanged:
  `ScreenSettings.showMetricDetail`/`showCharacteristicDetail`/`showMeasureDetail`, the panel-10
  search hits and `MetricRoute` select slot 10 and call into the model. Update the comments in
  `DiagnosticModel.qml` l.28 and l.54–58 (it lives in Settings → Reference → Swing model,
  reachable mid-session because the lock leaves Settings open).
- **`PpRail.qml`:**
  - buttons Home, Session (visible only while `sessionLocked`), Sessions (muted without a current
    athlete), Work-ons; no Swing model button (D14);
  - `sessionLockIndex` → the bool `sessionLocked`, with Session the only enabled destination above
    the spacer while locked (Settings below it stays enabled);
  - Settings highlight unchanged (Settings/System/Developer);
  - drop Home's `|| 11` highlight.
- **Tests and probes to update:**
  - `tst_charts_panel.qml` and `tst_chart_presets.qml` + `charts/ChartsHarness.qml`: drive the
    gate with the Session index. At l.432 keep the index on Session while changing `sessionType`.
  - `tests/setup/support/SetupDriver.qml` (replica indices l.75–78) and `fakes/FakeNav.qml`
    (add `navigateHome` if Main calls it).
  - `tst_setup_nav.qml` l.290–291.
  - About 25 probes: every `navigate(2)` in `src/Gui/tests/probes/`; `theme_render.qml` l.76–87;
    `gates_page.qml`, `diagnostic_model_report_fields.qml`, `crop_editor_colour.qml` and
    `setup_mounts_settings.qml` (Settings and DiagnosticModel paths); `swing_themes_home.qml:252`.
  - Eight files under `tools/`: `tools/probes/*`, `tools/typeset-harness/*` (`--probe-nav`
    default), and `tools/session-report-mock/dump_model.qml`.
  - `tst_rail_fold.qml` needs **no** change: it tests the Diagnostic Model's own type rail.
- **Accept when:**
  - no integer screen literals remain outside the enum (grep);
  - a session can start, lock navigation, reach Settings and its Swing model, return through the
    Session button, end and unlock;
  - review-a-session from Home, Swing diagnostics and the Sessions tab lands on the session
    screen;
  - charts, camera tiles, trust and the data viewer update on the session screen and stop when
    it is hidden (probe);
  - `qml_ui` and `session_setup_ui` pass.

### Phase 2 — Start a session without a type; carry a focus

**2a (audit) is done:** §9. The wizard resolves the constant `Wrist`.

- **`ScreenHome`:** `startSessionRequested(int)` → `startSessionRequested(var request)` where
  `request = { focusConditionId: "" | id }`; drop `openSessionScreenRequested`, the type grid's
  selection logic and `comingSoonTypes`. (`HmTypeCard` itself goes in Phase 5; until Phase 4
  rebuilds Home, Home shows one Start.)
- **`SessionController`:**
  - add `focusConditionId` (Q_PROPERTY beside `activeClub`; cleared in `endSession()`);
  - add a single default type: `start()` with no argument, or a `defaultType` constant. Main and
    `PpSessionToolbar._beginSession` both use it, as do `beginSessionFolder` calls;
  - unit tests in `session_clock_test`; add the property to `fakes/FakeSessionController.qml`.
- **Wizard:**
  - Remove `presets`, the comingSoon refusal, `SetupDraft.preset/goals/goalDefs`, `GoalsPage.qml`
    and the `goals` step.
  - Add an `intent` step (or first-page row; mockup A.4) showing the focus with a change control
    over open assessment + the Coaching focus + active work-ons.
  - `open(request)` seeds the draft. `sessionStartRequested(request)` hands it back.
  - Header copy becomes "New session"; drop "We'll confirm your goals".
  - SetupContext reaches `workOns` through a guarded injectable input (the test harness has none):
    add `fakes/FakeWorkOns.qml`, and a W5 handle for it.
  - A label lookup for a focus not in `items`: `WorkOnsController.conditionLabel(id)`, or carry
    `focusLabel` in the request.
- **`Main.qml` `onSessionStartRequested`:**
  1. set `sessionController.focusConditionId` from the request;
  2. `navigate(AppScreen.Session)`;
  3. `beginSessionFolder(default, false)`;
  4. `loadSessionDir("")`;
  5. `start()`;
  6. `startCapture()`.

  Wizard Cancel and exit leave the focus empty: it is set only at Start.
- **Focus delivery:**
  - Give `SessionDiagnosticsModel` an explicit focus start (`declareFocusFrom(id, fromShot)` or a
    pending focus applied at activation).
  - `PpSessionDiagnosticsPanel._pointAtSession()` adopts `sessionController.focusConditionId`
    when the panel is live, the session is running and its folder is the active one.
  - `_declareFocus` in a live session writes `sessionController.focusConditionId`. This also
    stops the cast window overwriting the stage's focus.
  - Unit test in `session_diagnostics_model_test`; QML coverage in `tst_session_diagnostics.qml`.
- **Retire** `appSettings.sessionGoalsByType` and `appSettings.lastSessionType`. A test that
  constructs AppSettings must set `QSettings::setPath` first.
- **Tests to rewrite:**
  - The goal tests become focus tests: `tst_setup_flow` N10F/N11F/N13F, `tst_setup_nav`
    N10/N11/N13, `tst_setup_lifecycle` L03/L04, `tst_setup_smoke` test_1.
  - About 40 plan/first-step assertions that start with `"goals"`.
  - The 12 `tst_setup_ready.qml` tables whose first row is "Goals".
  - `SetupDriver`, `CalibDriver` (l.464, l.703–717), `FlowShell`.
  - The probes `setup_walk` and `setup_soak`: they find the shell by `o.presets`; change the
    predicate to `o.flow !== undefined && o.steps !== undefined`.
  - Run `session_setup_lint_test` (W3, W5, W8, W10).
- **Accept when:**
  - Home has one Start; no UI shows a session type;
  - a Practise button (a temporary one in Swing diagnostics until Phase 4) starts a session whose
    panel shows the focus from shot 1. Probe this three ways: panel on at start; panel **off** at
    start and turned on later; cast window up;
  - the Capture button still starts a session, with no focus;
  - `session_setup_ui`, `session_setup_lint_test`, `session_clock_test`,
    `session_diagnostics_model_test` and `qml_ui` pass.

### Phase 3 — The area model

**Before starting: D1 must be decided** (§5.2), and D9 (the typical-value meaning).

- `Coaching/work_ons.h`: the all-conditions aggregate entry point (default unchanged); the
  `WorkOnEvidence` extension with the shared median helper; schema 2→3, serialised in `toJson`
  and read in `sessionWorkOnsFromJson`. `work_ons_test` gains a schema round-trip check.
- New `Coaching/swing_areas.h` + `swing_areas_pack.h` implementing §5.2 under D1, with grade
  reconstruction from the stored band (policy-aware). Unit tests in a header-only
  `swing_areas_test` beside `work_ons_test` (`src/Analysis/tests/CMakeLists.txt` l.150–154), for:
  - an empty library;
  - one session;
  - a group with no assessable condition;
  - Excluded-class conditions ignored;
  - ballFlight hidden without LM-only evidence;
  - trend unknown with one substantive session;
  - stable ordering;
  - the `topFault` floor;
  - `workOn` empty for never-listed conditions.
- `WorkOnsController`:
  - `areaItems` built in `publish()` and stored;
  - `focusItem.focusConditionId` from the first `rightNow` row: add `conditionId` to
    `NeedsWorkItem`, set in `themes::needsWorkItem`. Words unchanged, no theme rule bump;
  - `doWellItems[].share`; a short focus reason;
  - a "last session" accessor (or on `sessionReportList`);
  - `work_ons_controller_test` l.354–356 gains the new keys.
- `conditionGroupLabel()`: "Lateral" → "Lateral motion".
- **Accept when:**
  - the tests pass;
  - a probe prints `areaItems` for a **copy** of the developer library under
    `/mnt/swingdata/scratch/`. The catch-up rewrites every `work_ons.json` on the schema bump, so
    never point it at the live library;
  - the numbers match a hand computation (re-run the Python from the research against the copy)
    for two areas;
  - no swing was re-detected (the content stamp is unchanged).

### Phase 4 — Home

- **4a — shared components and tokens first** (§2.5):
  - Update `PpCard`, `PpMeter`, `PpPips`, `PpChip` (trend case), `PpBadge` (new kinds) and
    `PpTabs` (title variant); add `PpExpandRow` and `PpTableRow`; migrate `HmWorkOns` onto
    `PpExpandRow`.
  - Add the geometry tokens through `_v()` for all seven aesthetics; add `Theme.trendTone()`
    (and `scoreTone()` if D1 keeps a score); update `tst_theme_tables.qml`.
  - Give each component a `tst_*.qml` that loads the real component.
  - Add a session-free gallery probe, `src/Gui/tests/probes/components_gallery.qml`, laying out
    every piece with fixed fake data under captions, over `--probe-themes 0..13`.
    `typeset_gallery` needs a session and is diagnostics-specific, so it does not fit.
  - Capture `theme_render` and the gallery **before and after** in every theme (control build
    first; settings and sessions on scratch copies). Show them as annotated before/after sheets,
    and list each affected screen in the PR: HmWorkOns and HmGoesTogether on Swing diagnostics,
    and every `PpChip`/`PpBadge` user outside Home.
  - 4a ships on its own, with Home still on its old layout, so any regression elsewhere is
    isolated from the Home rebuild.
- **`home/HmAreaRow.qml`:**
  - header row on `PpExpandRow`, item rows on `PpTableRow`, footer link;
  - signals `practiseRequested(conditionId)`, `addWorkOnRequested(conditionId)`,
    `allInAreaRequested(group)`;
  - grade word and colour from `Theme.corridorWord/corridorTone`, so Action is the fault coral,
    not alarm red;
  - typical value omitted when `hasTypical` is false.
- **`home/HmSwingAreas.qml`:**
  - header, column headers, `Repeater` over `areaItems`;
  - expands the weakest measurable area on first show;
  - remembers expansion per athlete in the new `homeViewByAthlete` setting, with an
    `expandedSeen` flag so a deliberate all-collapsed state survives. Read the map once per write;
  - reading/empty/updating states copied from `HmSwingSummary`.
- **`home/HmRigCapability.qml`:**
  - one row per `CapRows` family for the kit Home "could measure" with (D10);
  - extract the facts computation from `SetupContext.qml` l.592–641 into a shared helper with a
    mode: the wizard keeps its draft gate; Home passes none;
  - keep the PPCP pairing entry, `PpcpOfferList` and `RmWarningNotice`;
  - wording stays in `setup_capability_rows.js`;
  - run `session_setup_ui` (`tst_setup_capability`, `tst_setup_ready`).
- **`home/HmHomeTabs.qml`** (on `PpTabs`, with its own `PpTypeset`): tabs **Coaching** and
  **Your swing** (`HmSwingAreas`); default Coaching (D6); last tab per athlete.
- **Re-lay `HmSwingSummary.qml` and `HmFocus.qml`** on the §2.5 components to match
  `03_home_coaching.png`:
  - keep every binding to `workOns` and every state;
  - the PpPips slicing moves into `maxPips`;
  - `HmFocus` gains Practise → `practiseRequested(focusItem.focusConditionId)` and the collapsed
    why/how row;
  - the "Swing diagnostics →" link row is replaced by the tab bar's "All work-ons ▸".
- **`ScreenHome.qml`:**
  - §2 → Start (one `PpButton { primary: true }` + club picker + last-session line);
  - §3 → `HmHomeTabs`; §4 → `HmRigCapability`; §5 unchanged;
  - drop the hero and the device rows; identity row and onboarding per D12;
  - Home's column width per Appendix A (≈90 %) through a Home-only width rule; do not change
    `Theme.contentWidth()`, which other screens use;
  - wire `practiseRequested` → `startSessionRequested({focusConditionId})`, and
    `allInAreaRequested` → a new `swingDiagnosticsRequested(group)` →
    `ScreenSwingDiagnostics.groupFilter`.
- **Probes:**
  - `src/Gui/tests/probes/swing_themes_home.qml`: select the Coaching tab first; expand the
    why/how row before asserting `focusPractise`; scope the golfer-language check to the
    Coaching tab; follow "All work-ons ▸" instead of `diagnosticsLink`.
  - `work_ons_home.qml`: the new entry point (All work-ons ▸ or rail Work-ons).
  - Add `src/Gui/tests/qml/tst_swing_areas.qml`, driving `HmSwingAreas` with a fake controller,
    and `tst_home_tabs.qml` (default tab, switching, per-athlete memory).
- **Accept when:**
  - screenshots of Home match `03_home_coaching.png` (Coaching tab) and
    `02_home_weakest_expanded.png` (Your swing tab) in layout and proportion; compare side by
    side and list any differences in the PR;
  - A.1/A.2 are reproduced in all 14 theme renders (`theme_render`);
  - the keyboard can expand/collapse area rows and switch tabs (`PpExpandRow` and `PpTabs` only;
    app-wide keyboard access is a separate later design);
  - no figure appears without its unit and corridor;
  - the first-run (no athlete) Home still works.

### Phase 5 — Clean-up

- Delete `HmTypeCard.qml` (its cmake line), the `app_tiles` resource (root `CMakeLists.txt`
  l.1095–1103) and `src/Resources/tiles/{golfswing,grip,feet,coach}.png`. Remove any remaining
  `comingSoon` logic.
- Delete the dead session-type code listed in §9 (and in §4).
- Update `home_themes_design.md` (no-figures rule now Coaching-tab only),
  `session_wizard_refactor_design.md`, `docs/implementation/gui-catalogue/areas/05-home.md` and
  `docs/user/personas.md` to point here.
- Rename `ScreenWrist` → `ScreenSession` as a separate mechanical commit.

## 7. Decisions

Open:

- **D2 — Row order.** As approved: descending score, weakest at the bottom and expanded.
  Alternative: fixed address-to-finish order (setup, posture, lateral, sequence, arms & club,
  release, impact, finish). Make it one constant. Revisit with D1: if most scores floor,
  descending order says little.
- **D3 — Do golfers adopt work-ons?** Today work-ons are derived (`Active/Easing/Cleared`), never
  adopted. The simplest mapping needs no persistence: `new` = Active and listed only in the newest
  substantive session, labelled "New"; no Add/Make buttons. If adoption is wanted, add an
  athlete-level `<athlete>/work_on_choices.json` (`adopted` and `dismissed` sets), consulted only
  for display and ordering, never for detection. Note that the Coaching focus can be a condition
  that was never a work-on (§2.4).
- **D10 — What Home's "could measure" means.** Cameras assigned and enabled but not connected;
  sensors present and enabled but not calibrated this visit. Shown as live, or as estimated? Settle
  it at the start of Phase 4.
- **D11 — One token source for the new look.** `Theme` tokens or `PpTypeset` roles. `PpTabs`
  already draws from PpTypeset, which defines hairlines, inks and figure fonts. Settle it at the
  start of Phase 4a.
- **D12 — Home's identity row and first-run onboarding.** The mockups have neither. Keep a slim
  identity row, or rely on the rail avatar; the no-athlete onboarding must survive either way.
- **D5 — Rail width.** The current rail is an icon column with labels beneath; the approved
  mockup shows a wider labelled rail. Phase 1 keeps the current rail; widening is cosmetic and can
  follow.

Decided:

- **D1 — Area rating.** *28 Oct:* habit counts, no 0–100 score (§5.2).
- **D9 — Typical value for unlisted conditions.** *28 Oct:* median of all judged readings (§5.2).
- **D4 — "Your focus" and "What you do well".** *27 Oct:* kept, as the Coaching tab.
- **D6 — Default tab.** Coaching (what to do next) by default; Your swing (the holistic view)
  one click away; the last choice remembered per athlete.
- **D7 — Way back to a running session.** *27 Oct:* a Session rail button, visible only while a
  session runs.
- **D14 — Where the Swing model lives.** *28 Oct (Mark):* "Swing model" is the DiagnosticModel
  editor's name wherever users see it. It stays in Settings → Reference (slot 10) and is reached
  mid-session through Settings, which the lock leaves open. No rail button and no screen of its
  own: "I don't want the swing model on the main rail, that makes no sense. I just want it in the
  settings accordion."
- **D8 — Analysis gates on session type.** *27 Oct:* out of this refactor; tracked as task #111
  (with bug #110, re-analysis drops the wrist assessment).
- **D13 — Recent sessions.** Unchanged in Phase 4 (`PpSessionReportList`). A restyle onto
  `PpTableRow` is later work.
- **Wording, colour, keyboard (defaults applied in this document):**
  - capability gaps describe and never instruct ("No wrist sensor", not "Connect sensor");
  - the Action grade uses the fault coral (`Theme.corridorTone`);
  - keyboard operability is required only of `PpExpandRow` and `PpTabs`.

## 8. Out of scope

- a Progress (cross-session trends) page;
- a redesigned Sessions page;
- drills and practice plans;
- removing `sessionType` from the analysis pipeline, and the analysis gates (#111, #110);
- a pack `groups` table;
- app-wide keyboard access;
- mobile layouts.

## 9. Phase 2a audit result

27 Oct 2026, at `ab5c4d9a`, read-only. Every non-test use of the session type was classified,
across C++, QML and SwingLab, including the `_Wrist_` folder token, `capture.sessionType`,
`presets`/`railIndex`, the `*ByType` settings and the `"<type>:<mode>:…"` preference keys.

| Class | Rows | Meaning |
|---|---|---|
| Capture profile | **0** | Nothing that configures a camera, sensor, launch monitor or setup step reads the type (lint W8 forbids it in the wizard). |
| UI | 41 | Screen routing (`+1` gates, rail, lock), the wizard header and goals, per-screen preference keys, dev-monitor labels. Removed or retargeted in Phases 1–2. |
| Analysis gate | 13 rows (11 in `src/`, 2 in SwingLab) | Four distinct behaviours, below. They break the rule that analysis is session-agnostic. → #111. |
| Persisted / compat | 25 | `capture.sessionType` (written by `swing_exporter.cpp:959` and `swing_doc.cpp:1885`, read by re-analysis `swing_reanalyzer.cpp:716`); the `_Wrist_NN` folder token (`swing_paths.cpp` l.148–282, `shot_processor.cpp` l.677–686, 2005–2048), which finds today's session and is the session id for the work-ons and PPCP ledgers; `segmentRoleForSlot` for old swings with no stored role (`swing_analysis.h` l.67–74, via `swing_data_source.cpp`, which uses the *screen's* type); `SessionController::Type` value 1. |
| Dead | 14 | `ShotContext::sessionType`, `MetricQuery::sessionType` and their writers (`setup_capability.cpp:148`, `metric_catalog.cpp:68, 202`); `ShotReplayController::shotContext`; the shot-marker type (`shot_controller.cpp` l.623–635); `sessionStarted(int)`'s argument; `onShotDetected`'s third argument; `lastSessionType`; `viewPanelsByType`/`viewArrangementByType`/`viewPresetByType`; `ScreenSessionMode.qml`; `PpMarkupPanel.sessionType`; `SwingScorer`. |

**The four analysis gates** (none changes behaviour today, because the type is always 1):

1. `makeShotAnalyzer` picks the analyzer by `sessionType == 1` (`shot_analyzer.cpp:122`). It is
   used live (`shot_processor.cpp:1489`), in re-analysis (`swing_reanalyzer.cpp:1012`) and in
   SwingLab. Any other type gets a camera-only analyzer with a stub score.
2. `job.runAssessment = (m_sessionType == 1)` (`shot_processor.cpp:1164`). Re-analysis never sets
   it, which is bug #110.
3. The re-analysis refusal for a swing with no sensors and no face-on camera applies only to Wrist
   swings (`swing_reanalyzer.cpp:1005`).
4. `analysisGraphJson` picks its profile by type (`analysis_pipeline.cpp:4095`; tooling only).

**Answers.**

- **(a) Does behaviour differ by type for a session started today?** No. No session can be
  anything but Wrist: Home routes the other cards to placeholders, the wizard refuses them, and
  the toolbar exists only on `ScreenWrist`. The Wrist stage list is a strict superset of the
  camera-only one, and its extra stages already check for their data, so Wrist is never worse on
  any rig.
- **(b) What should the wizard resolve to?** The constant `SessionController.Wrist` (1). Choosing
  by rig would repeat the per-stage data checks one level up, and would itself be a session-level
  gate. Two call sites start sessions, `Main.qml` `onSessionStartRequested` and
  `PpSessionToolbar._beginSession`; both use one default in `SessionController`.
- **(c) What reads the type from saved sessions?** Re-analysis, the folder token, and the
  `SwingDataSource` compat fallback. The session list, reports, work-ons and diagnostics do not.
  All 597 saved folders (20 live, 577 corpus) are `_Wrist_`, so with the constant there is **no
  compatibility work**. Keep writing `capture.sessionType = 1`, keep the enum value 1, and keep
  the `"1:"` preference keys.

---

## Appendix A — Mockups for implementation

**Look at these before writing any QML** (open them with the Read tool):

| File | Shows |
|---|---|
| `ux_refactor_away_from_4_modes/03_home_coaching.png` | Home on the **Coaching** tab (the default): today's YOUR SWING content redrawn in the §2.5 visual language, why/how collapsed |
| `ux_refactor_away_from_4_modes/01_home_collapsed.png` | Home on the **Your swing** tab, every area collapsed |
| `ux_refactor_away_from_4_modes/02_home_weakest_expanded.png` | **Your swing** tab as first shown: the weakest area (Posture) expanded, with Practise on the active work-on and "New" on a newly listed one (D3 default) |
| `ux_refactor_away_from_4_modes/coaching_existing.png` | **Before**: today's YOUR SWING, for comparison only — do not build to it |
| `ux_refactor_away_from_4_modes/home_mockup.html` | The static page the PNGs were rendered from (1440 px wide, 2× scale) |

How to read them:

- The PNGs govern **layout, proportion, density, alignment and hierarchy**. Match them.
- The token table (A.3) governs **exact values**. The PNGs and the HTML use literal hex colours
  and a Google font; the app uses `Theme` tokens and the active theme's fonts, so colours will
  differ by theme. Never copy a hex value into QML.
- The rail in the PNGs is the **current** icon-over-label `PpRail` with the Phase 1 buttons (D5),
  not a new component. The Session button (D7) is absent because no session is running.
- All figures are illustrative. Real-library scores under the provisional rule are far lower
  (§5.2); the area rows' rating column depends on D1.
- Where the mockups conflict with this document, this document wins:
  - the capability rows' "Connect sensor" / "Connect launch monitor" / "2 × IMU" wording (use
    `setup_capability_rows.js` families and wording);
  - Action in red (use the fault coral);
  - the identity row's absence (D12).
- Home's content column is about 90 % of the area in the mockups, against
  `Theme.contentWidth()`'s 70 %. Give Home its own width rule.

Dimensions are design pixels at `Theme.sp(1) = 1`; use `Theme.sp()` and theme tokens, never hex.

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
│      │ Coaching   [Your swing]   from 180 swings over 6 sessions        All work-ons ▸         │
│      │ ─────────  ══════════                                                                   │
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
│      │ │    Ball flight    not measured with this rig                                      │ │
│      │ └───────────────────────────────────────────────────────────────────────────────────┘ │
│      │                                                                                         │
│      │ Recent sessions                                                     All sessions ▸     │
│      │ (PpSessionReportList, unchanged)                                                        │
│  ⚙   │                                                                                         │
└──────┴───────────────────────────────────────────────────────────────────────────────────────┘
```

Values are illustrative. Area names follow §5.3 (pack groups), not swing phases. The capability
rows' labels and gap wording in the wireframe are placeholders: the real rows are the
`setup_capability_rows.js` families, described, never instructed.

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
- Grade words and colours: `Theme.corridorWord(grade)` / `Theme.corridorTone(grade)`. Action is
  the fault coral, never alarm red; NotMeasured → `Theme.colorText3`. Colour is never the only
  channel: the word is always printed (as `HmSwingSummary` does today).

### A.3 Tokens and sizes

New geometry tokens go into `Theme.qml` through `_v()` (§2.5); the values below are their defaults.

| Element | Spec |
|---|---|
| Section titles | as `PpDisplayText`/`PpMicro` on Home today |
| Table card | radius: a `tableRadius` token (default 12), or `radiusLg` (6–10 by theme) — decide in 4a |
| Area row | height 48, padding 18 h; hover `Theme.colorBg2`; divider: hairline token (fainter than `colorBorder`) |
| Rating bar | 6-px track: track `Theme.colorBg3`; fill by band (D1; thresholds are constants) |
| Score | `Theme.fontData`, 15 |
| Trend | `Theme.fontData`, 12; `Theme.trendTone(n)`: + `colorGood`, − `colorWarn`, ±0 `colorText3` |
| Work-ons count | 12, semibold; active `Theme.colorAccent`, new `Theme.colorAttention` |
| Item row | height 40, indent 62, top divider |
| How-often meter | 10 segments 7×6, radius 1, gap 2; unlit colour as a token |
| Session pips | 6 px, gap 3 |
| Tab bar | title size (≈22, a new token between Heading and Display); 2-px accent underline |
| Practise | `PpButton` secondary with play glyph |
| Capability row | 8-px dot: filled `Theme.colorAccent` measured, half `Theme.colorAccentMid` estimated, outline `Theme.colorText3` unavailable; device label `Theme.fontData` 11 `Theme.colorText3` |

### A.4 Wizard, first page with focus

```
┌─ New session ───────────────────────────────────────────────┐
│ Focus   Early extension                         [change ▾]  │
│         (open assessment · Trail hip load · Early extension)│
│ Club    7 iron  (picked on Home)                            │
│ … existing setup steps (devices, mounts, calibration) …     │
│                                                [Start ▶]    │
└─────────────────────────────────────────────────────────────┘
```

The focus step's registry key is `intent`, not `focus` (lint W10). The club stays picked on Home
in this refactor; the wizard shows it.
