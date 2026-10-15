# Wrist motion → Charts

**Status:** design chosen 15 Oct 2026: B (the Wrist tab), with the one-vocabulary alignment of §7
(Mark: "your proposal is correct, let's add that in"). Three designs were mocked over the real panels
with real data. Nothing built.

Mark, 15 Oct 2026: "look at the wrist motion chart and see how it can be deprecated and the
functionality incorporated into the chart panel. Ideally we would not retire any functionality and
the wrist chart would gain features we added to the chart panel."

**Where the work is** (local; `build/` is not in git):
- Review PDF: `build/run-me/wrist-charts/wrist-charts-designs.pdf`.
- Mocks and notes: `build/run-me/wrist-charts/qml/variant_{a,b,c}/` (NOTES.md, SIGNATURE.md, QML) and
  the brief `BRIEF.md`.
- Renders: `/mnt/swingdata/scratch/typeset-harness/out/w_{a,b,c}*`.

Read first: `docs/design/charts_panel_traces_relations.md` (the Charts design this extends),
`docs/design/pinpoint_qml_design_system.md` §15 and the Wrist motion entry (§2) of
`docs/design/stage_audit_20261011.md`.

---

## 1. The two panels today

**Wrist motion** (`src/Gui/diagnostics/WristDiagnostics.qml`, stage key `wristMotion`, Wrist screen
only) is a **checkpoint scorecard for one swing**. `WristDiagnosticsModel` runs the wrist assessment
engine (`src/Wrist/`) on the focused swing and shows each wrist degree of freedom as Δ from address at
P1–P8 against an archetype band, graded per cell, with a 0–100 score, findings and strengths. It
reads nothing across the session except a previous or reference swing as a ghost.

**Charts** (`src/Gui/charts/`, shipped 14 Oct) is **continuous curves across the session**: This shot ·
Session · Relations · Kinds, P-aligned, with Against, the group fit, Apart from the session, Replay and
Analyse registers. It already has a **"Wrist & forearm" preset** and already carries the wrist readings
(`hm.leadWristFlexExt`, `hm.leadWristRadUln`, `hm.forearmRotation`, `trailWristFlexExt`) in its shot
profile, Relations and Kinds (dump of 18 Aug Wrist_02, 15 Oct). What it does not have is the
**assessment**: bands, grades, the score, findings, the band model, the cross-session reference.

So the merge is not "draw wrist curves in Charts", which already happens. It is "give Charts the wrist
assessment without losing any of it".

### 1.1 The data, honestly

Only two sessions in the library carry lead-wrist data: 18 Aug Wrist_01 and Wrist_02 (HackMotion wG3 +
launch monitor, 5 and 6 shots). Every Wrist session since (4 Jul, 9 Sept, 5/7/8/13 Oct) is pose-only:
the trail wrist is the only DOF, from pose. No swing in the library has `wristScore` or the
`leadWristFlexExt` (Witmotion) series. The designs were mocked on 18 Aug Wrist_02 shot 3, with one
pose-only render (4 Jul) each. Six shots is a small session for Session, Relations and Kinds, and the
renders say so.

## 2. Parity: what Wrist motion does (nothing may be lost)

| # | Function | Today |
|---|---|---|
| W1 | Checkpoint profile per DOF: Δ from address at P1–P8 (lead wrist hinge, lead wrist bow/cup, lead forearm roll, trail wrist extension; lead elbow grid-only) | DofTrajectoryStrip |
| W2 | Archetype band per checkpoint; a grade per cell (in range / watch / fault / no data) | RAG glyphs (an audit defect: traffic lights) |
| W3 | Band model: Auto (→ Neutral / Bowed / Cupped) or a manual override; re-bands and re-grades | segmented control |
| W4 | Archetype similarity (Neutral / Bowed / Cupped %), which is effective | chips |
| W5 | Wrist score 0–100, band word, breakdown ("100 − 12 = 88", penalty per finding) | WristScorePill |
| W6 | Findings: name, positions, magnitude, confidence, explanation, coaching, linked-to, corroborated-by, ball flight, points lost; click seeks to the checkpoint | FindingsList / FindingCard |
| W7 | Working well, the same fields plus "protect" | StrengthsList |
| W8 | Compare ghost: none / previous swing / reference swing | segmented control |
| W9 | Set as reference ★: a benchmark swing kept in app settings, across sessions | `appSettings.wristReferenceSwingDir` |
| W10 | The checkpoint follows the playhead; "At P4"; position name and note | card aside + header |
| W11 | Position × DOF grid (every DOF incl. elbow, every cell) | PositionAngleGrid |
| W12 | Consequence poles per DOF ("stuck"/"cast", "closed"/"open", "over-roll"/"block", "held"/"scoop") | strip axis words |
| W13 | Source and confidence per DOF ("IMU", "IMU + pose", "pose (no trail IMU)", ●●○○) | strip footer |
| W14 | Empty states: no wrist metrics; no swing selected | PpCardNote |

## 3. What the wrist read gains from Charts

- **Curves, not eight points:** the whole bow/cup and hinge trace in its metric colour, the windowed
  mean with raw dots, peaks with σ, click or drag to seek.
- **Session:** every shot's wrist curves P-aligned, the envelope, where they part, facets by a fault,
  the group fit. The question "how repeatable is my wrist" has no answer in Wrist motion today.
- **Against:** session median, most / least typical shot, previous / next, shots like this one, and the
  difference strip ("when this swing left your normal one").
- **Apart from the session:** each wrist reading beside the session's box, this shot ringed.
- **Relations:** wrist readings against everything else the swing produced, including the launch
  monitor (face, path, spin) and the shaft; drift across the session.
- **Kinds:** wrist style as kinds of swing.
- **Registers:** Replay ("now" at the playhead), Analyse, the cast; persisted reader choices; print
  when D8 lands.

And Charts gains from the wrist panel, for every preset: a **cross-session Reference ★** in Against,
**bands drawn at P-positions** wherever a metric has them, and **findings with seek** as a pattern.

## 4. The three designs

All three cover W1–W14 (each NOTES.md has the full table) and all three drop the traffic lights:
a checkpoint outside its band gets Charts' own warn ring plus a word ("fault · 19° open", "scoop 7°").
They differ in **where the wrist read lives**.

### A · Preset: wrist dissolves into Charts

No wrist view. The Wrist screen opens Charts on the "Wrist & forearm" preset, and Charts gains three
general features that carry the wrist panel:
- **Corridor ticks at the P-positions** on any trace whose metric has bands, graded with Charts' P marks
  (filled = inside, ring = watch, warn ring = outside) and the pole word on the worst exit. A quiet
  **Values: as measured · from address** choice and **Bands: Auto → Cupped · Neutral · Bowed · Cupped ·
  off** (W1–W3, W12).
- **★ Reference** as the first Against option, for every metric, across sessions (W8, W9).
- **An assessment fold** in the This shot answer column whenever the preset's domain has an assessor:
  score with its sum, findings that open in place and seek, working well, resemblance (W4–W7). An
  **At the P-positions** fold is the old grid (W11). In Session, a rail gives the score by shot, the Auto
  band per shot and each finding as a strip across the shots ("5 of 6").

Strongest: the least new surface, and every other preset gains corridors, ★ Reference and an
assessment fold. Weakest: the This shot head is Charts' peak-led head ("bow/cup peaks 170 ms after
impact"), the wrong question for a joint angle, and at half width the answer column is tall and the
P grid falls below the fold.

### B · Wrist tab: a domain view inside Charts

Charts gains a fifth tab, **Wrist**, shown only when a wrist reading exists (gated on data). It has
This shot and Session scopes; This shot · Session · Relations · Kinds carry the wrist curves as usual.
- **This shot:** a standfirst at the playhead's P ("At P4, top: hinge −40°, bow/cup −19° outside its
  corridor, open …"); the score figure and its sum; the **checkpoint profile**: P1–P8 per joint on a
  discrete axis in the metric's colour, the band as a wash, the playhead's P as a picked column
  through every row, the numbers under each column (the grid), pole words at the right, source and
  confidence on the label, elbow as a numbers-only row. Findings and working well in a right-hand
  column at wide.
- **Gains in the tab:** curves behind the checkpoints (on/off), the session median as a fourth Against,
  each wrist reading beside the session's box, and "what moves with the lead wrist at impact" from
  Relations, launch monitor included, with the chance line (lag r −.96, clubhead speed −.88; launch
  angle .78 inside chance at n = 6).
- **Session:** a box per checkpoint across the shots (this one ringed, "median / N° wide"), the score
  shot by shot, the style per shot against the resemblance index, and which findings keep firing (a
  mark per shot that picks that swing).

Strongest: one panel holds the figure and the verdict, and the wrist gets its own head. Weakest: a
fifth tab, and at half width three control lines push the profile to the fold (fold them to a summary
line at compact).

### C · Split: curves to Charts, verdicts to Session diagnostics

Each question goes to the panel that already answers it for the rest of the body. The Wrist screen
opens on Charts + Session diagnostics.
- **Charts** gains a third **Draw** form, *checkpoints* (beside small multiples and overlaid), and a
  **Bands** choice carrying the similarity %. Checkpoints draws Δ at P1–P8 against the band **and the
  session's box at every checkpoint**: how repeatable the wrist is at each P.
- **Session diagnostics** takes the score (masthead, "84 of 100 · 100 − 7 − 9", a run chart in Session)
  and the wrist engine's findings as ordinary ledger rows with strips, recurrence and an in-place card
  ("Go to P5 in the replay"). As conditions, wrist findings become Charts facets, Relations splits and
  Kinds labels for free.

Strongest: C exposed that **the app already has two judges of the wrist** (§4.1). Weakest: the figure
and the verdict are never in one panel; on a one-panel layout half the wrist read is gone, and Charts
can only point to it.

### 4.1 Found in the data (every design hit these)

| Finding | Effect | Where |
|---|---|---|
| Two judges of the wrist: the wrist engine and Session diagnostics' wrist conditions | 18 Aug: cupped at the top, both fire on 4 shots and each alone on 1; Scooping fires on 3 shots, the engine finds no flip; the engine's cast fires on 5, the Casting condition reads clean | `src/Wrist/` vs diagnostics norms |
| P2 is sampled at Takeaway (phase 1), which the ladder puts on Address | P2 reads 0° on every joint and grades "watch"; Charts' P2 is shaft-parallel back. Changing it changes grades | `wristCheckpoints()`, see the comment in `WristDiagnosticsModel::positions()` |
| Auto bands disagree with the resemblance index | Auto grades shots 2–6 against Cupped while the index says Neutral is closest (84 vs 50); the three % do not sum to 100 | assessment engine / `wrist_resemblance` |
| The Cupped band at P8 is −14 to +2° | all six shots reach about −50°: the band or the P8 instant is wrong for this golfer | `reference_bands` |
| Scores contradict marks | shot 3 scores 84 "good" with bow/cup out at 4 checkpoints; 4 Jul shot 8 scores 100 with P3 out (the audit's "red square on a 100/100") | scoring vs grading |
| `shotReplay.analysisDetail` carries no `resemblance` | today's panel shows no similarity on these swings | analysis detail |

### 4.2 Recommendation

**B as the base, with A's general features.** B is the only design that keeps the figure and the
verdict in one panel and gives the wrist its own head (A's head asks about peaks; C splits the read
across two panels). Take from A the three things that make every other preset better: corridor ticks
at P-positions, ★ Reference in Against for every metric, and the findings-with-seek block as a shared
block rather than a wrist one. Take from C the session box at every checkpoint and, above all, its
finding: **settle the two judges before building**, so the Wrist tab and Session diagnostics show one
verdict.

## 5. Migration (all designs)

- Stage key `wristMotion` leaves `PpModeStage` / `PpViewPanel`; saved layouts that name it are
  migrated to `charts` (with the wrist entry selected) on load, once, so nobody opens to a hole.
- `appSettings.wristReferenceSwingDir` becomes the Charts reference (generalised, same value, so a
  golfer's existing benchmark survives).
- Retired QML: `WristDiagnostics`, `DofTrajectoryStrip`, `PositionAngleGrid`, `FindingsList`,
  `FindingCard`, `StrengthsList`, `WristScorePill` (none is used elsewhere; checked 15 Oct).
- `WristDiagnosticsModel`'s work moves into C++ that Charts calls (per shot, and per session in the
  session-series model); the engine in `src/Wrist/` is untouched.
- Tests that name the wrist panel move with it (`tst_session_diagnostics` probes, stage-separator and
  theme probes list `wristMotion`).

Design-specific: A and B map a saved `wristMotion` card to `charts` (B with `tab = "wrist"`); C maps
it to `sessionDiagnostics`. The model requests (exact shapes) are in each design's NOTES.md; common to
all: a per-shot wrist assessment in the session-series model (off the GUI thread, no analysis blob
held), `{ dof, metricKey }` on the wrist strips so nothing joins by display name, and the checkpoint's
own `t_us` for seeking.

## 6. Decisions for Mark

| | Decision | Proposal |
|---|---|---|
| W-D1 | Design | **DECIDED:** B (Wrist tab) with A's corridor ticks, ★ Reference and findings block made general, and C's checkpoint boxes |
| W-D2 | Two judges of the wrist | **DECIDED (§7.3):** the diagnostics conditions are the one judge; each engine rule becomes a condition or a second measure on one; the wrist score is a domain score over those conditions |
| W-D3 | P2 instant | **DECIDED (§7.3):** sample P2 at shaft-parallel back (phase 12), the P2 every other panel uses; regrade, and say so in the release notes |
| W-D4 | Auto bands | Follow the resemblance index, or explain why not; today they disagree on 4 of 6 shots |
| W-D5 | ★ Reference | One swing for every metric, kept across sessions (today's `wristReferenceSwingDir` value carried over) |
| W-D6 | Exits that fire no finding | Mark them, and say on the head why they are not a finding ("no finding fired; 2 checkpoints sat outside a band") |
| W-D7 | The Wrist screen default | Charts on the Wrist tab, This shot |
| W-D8 | Fourth comparison line | Session median, beside previous and ★ reference |
| W-D9 | Bands and corridors | **DECIDED (§7.3):** the diagnostics norms are the one table; the Wrist tab draws the resolved context's corridor as its band |
| W-D10 | Forearm roll (takeaway, top, impact) | **DECIDED (§7.3):** the roll measures read `forearmRotation` / `hm.forearmRotation` with their own norm rows; `early_face_roll` = roll from address to shaft-parallel back; starts wide and monitored; sign checked on a directed capture before it grades anyone |

**Chosen 15 Oct (Mark): B is the base.** He asked next for the wrist chart, the reference bands and
the diagnostics measures and corridors to be aligned: §7.

The data in §4.1 is a separate question from the panel: the P8 band and the score/mark contradictions
want a look at the wrist engine with more than one golfer's swings (tuning bands to one golfer is
the trap the per-swing wrist work hit). Lead-wrist data in the library is 11 swings from one day.

## 7. One wrist vocabulary: chart, bands, measures, corridors

Mark, 15 Oct 2026: "do we have metrics for positions outside of those bands at the top and impact.
also do we have a characteristic for rolling the forearm from P0–P2 … a really common fault of
opening the clubface at the takeaway. Would be good to align the wrist chart, the reference bands and
the metrics and measures corridors."

### 7.1 What exists today

- **One table already.** The wrist engine's bands are not its own: `NormBandProvider`
  (`src/Wrist/reference_bands.cpp`) reads the diagnostics norms (`src/Resources/diagnostics/norms.json`),
  rows `m_leadWristFlexExt_p1…p8`, `m_leadWristRadUln_p1…p8`, `m_leadForearmRot_p1…p8`,
  `m_trailWristFlexExt_p1…p5`, with Bowed and Cupped contexts for bow/cup. The band the Wrist tab draws
  and the corridor Session diagnostics judges are the same numbers.
- **Measures at every position.** `core.json` has a measure per joint per P-position (change from
  address), plus `m_leadWristAtTop`, `m_leadWristAtImpact` (absolute) and `m_lagAngleDown`.
- **Conditions at the top and impact** (each fires when its measure leaves the corridor):

| Where | Condition | Measure |
|---|---|---|
| Top | Lead wrist cupped at the top / bowed at the top | bow/cup at P4, low / high |
| Top | Face open at the top / shut at the top | forearm roll at P4, high / low |
| Top | Insufficient wrist set / over-set | hinge at P4 |
| Impact | Scooping / lead wrist bows too far into impact | bow/cup at P7, low / high |
| Impact | Flipping / face held open through impact | forearm roll at P7, low / high |
| Downswing | Casting / holding the angle too long | lag angle, lowest P5–P6 |
| Takeaway | **Face rolled open early** / face held shut in the takeaway | forearm roll at P2, high / low |

### 7.2 Where they do not line up

1. **The takeaway roll can never fire.** `early_face_roll` and `face_held_shut_takeaway` read
   `forearmPronation`, the ISB radioulnar angle, which needs an upper-arm sensor: no swing in the
   library has it (the norm rows say "SIGN UNVERIFIED … no swing carries a forearm-rotation reading").
   The reading that does exist, `hm.forearmRotation` (the wG3 lower-arm unit's twist from address; its
   card already shows the takeaway peak, Mark 1 Oct: "watching it between P1 and P2 is quite
   informative"), feeds no measure. The same is true of the face at the top and at impact (P4, P7 roll).
2. **Two P2s.** Diagnostics reads P2 at shaft-parallel back; the engine reads the SAME norm row at
   Takeaway, which lands on Address, so its P2 is 0° on every joint.
3. **Two judges.** The engine's rules (`src/Wrist/assessment_rules.cpp`) and the diagnostics conditions
   judge the same joints separately and disagree on 18 Aug (§4.1). Two of them share a name for
   different things: the engine's "Open face at top" is a CUP reading at P4; diagnostics' "Face open at
   the top" is forearm ROLL at P4 (its own consequence text says the cup half is `cupped_at_top`).
   The engine's "cast" reads hinge P5–P6; diagnostics' Casting reads lag angle P5–P6.
4. **Two scores.** The engine's 0–100 comes from its own rule penalties, not from the conditions.

### 7.3 Decided (Mark, 15 Oct 2026)

- **The norms are the one table** (already true); the Wrist tab draws `monitorLo…monitorHi` of the
  resolved context as the band, so chart, band and corridor are one number by construction.
- **Diagnostics' P vocabulary everywhere:** the engine's P2 moves to shaft-parallel back (regrades).
- **Diagnostics conditions are the one judge.** Each engine rule becomes a condition or a second
  measure on one: open/closed face at top → `cupped_at_top` / `bowed_at_top`; flip → `scooping`;
  cast → `casting` with hinge P5–P6 as a second measure beside lag angle; insufficient set → the same
  id; over-rotation / holding off → `flipping` / `face_held_open_impact`; chicken wing → the same id.
  The engine's strengths (set loaded, face rotation on schedule, lead arm width) become the conditions'
  "working well" states. The wrist score becomes a domain score over the wrist conditions, so the
  number and the findings can never disagree (the "84 good with four points out of band" problem).
  The Wrist tab shows these conditions; Session diagnostics shows the same ones.
- **Wire the forearm roll to the reading that exists:** the forearm-roll measures (P2, P4, P7) take
  `forearmRotation` / `hm.forearmRotation` (single-segment twist from address) as their source, as
  bow/cup does with `preferKeys`. They are a different quantity from the radioulnar angle, so they
  get their own norm rows: `early_face_roll` = roll from address to shaft-parallel back past the
  corridor. Its corridor and sign need data: 11 HackMotion swings from one day are not enough to set
  one, so it starts as a monitored, wide corridor (as `m_transitionPlaneDelta` did), and the sign is
  checked on a directed capture (roll open on purpose, then hold square).

### 7.4 Build order this adds

The alignment comes FIRST, under the Wrist tab, because the tab shows the conditions' verdict:
1. **Engine P2 at shaft-parallel back** (`wristCheckpoints()`), with a stage-version bump so the library
   regrades.
2. **Forearm roll wired:** measures `m_leadForearmRot_p2/p4/p7` take `forearmRotation` /
   `hm.forearmRotation`; new norm rows for single-segment rotation (wide, monitored); the directed
   capture to check the sign before the rows grade.
3. **One judge:** the engine's rules folded into conditions (the map in §7.3), cast's hinge reading as a
   second measure on Casting, strengths as working-well states, and the name clash resolved (the cup
   reading is "cupped at the top"; "face open at the top" stays the roll reading). The wrist score
   computed over the wrist conditions.
4. Then the Wrist tab (§4, B) reads conditions, corridors and the score from one place.

Regrading changes results the golfer has seen, so steps 1–3 are judged on the 18 Aug swings and the
library before commit.
