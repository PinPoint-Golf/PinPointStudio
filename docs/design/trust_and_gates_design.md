# Trust and gates: saying why a reading was refused

**Status:** **closed 17 October 2026.** Stages 0–7 built and pushed (stage 3 with the Trust panel of §7.0; stage 4 as §4.4; stage 5 as §7.4; stage 6 as `trust_stage6_plan.md`; stage 7 as §7.5). Three styling and policy questions are left open on purpose (§11). Written 16 October 2026.
**Owner:** Mark.
**Audience:** developers working on the analysis producers, the diagnostics engine, the Charts and
diagnostics panels, and the Setup page.
**Builds on:**
- `metric_presentation_honesty.md`: validity masks, phase domains, σ-governed display (the
  *per-sample* half of honesty; this document is the *per-reading and per-finding* half);
- `uncertainty_census_design.md`: σ for every metric (the *how much* question; this document
  is the *whether, and why not* question). The two censuses are separate tables over the same
  metrics and are meant to be read together;
- `kinematic_sequence_design.md` §12: the sighted band and the bounds a refused node keeps;
- the Setup page's capability summary (`src/Metrics/setup_capability*`,
  `src/Gui/setup/setup_capability_rows.js`): measured / estimated / unavailable / derived, the
  *before-capture* answer to the same question.

---

## Contents

1. [What prompted this](#1-what-prompted-this)
2. [The problem](#2-the-problem)
3. [Principles](#3-principles)
4. [The model: gates, gate records, trust levels](#4-the-model-gates-gate-records-trust-levels)
5. [The gate catalogue](#5-the-gate-catalogue)
6. [Gate records in the swing document](#6-gate-records-in-the-swing-document)
7. [How it is shown](#7-how-it-is-shown)
8. [The census of gates in the code today](#8-the-census-of-gates-in-the-code-today)
9. [Build order](#9-build-order)
10. [Definition of done](#10-definition-of-done)
11. [Decisions taken, and the ones still open](#11-decisions-taken-and-the-ones-still-open)

---

## 1. What prompted this

Shot 4 of `2026-08-18_Mark-Liversedge_Wrist_02` (a 7 iron, face-on camera, lead-arm IMUs), Charts →
Kinematic sequence, Analyse. The strip reads:

> Lead arm then club, in order so far; pelvis and chest not placed
> Pelvis not placed (σ 47 ms). Chest not placed (σ 64 ms).

The curves underneath show an obvious pelvis peak at +45 ms and a chest peak at +32 ms, both drawn
with rings. Mark's question, reasonably: *the peaks are plain on the chart, why are they not placed?*

The analysis had answered that question carefully, and the screen threw the answer away. From the
swing document:

| Node | tPeak | tσ | Bound the producer kept | Why it was refused |
|---|---|---|---|---|
| pelvis | +45 ms | 47 ms | peak **no earlier than 129 ms** before impact | the face-on view lost sight of the pelvis turn (within 20° of square) at −129 ms while its rate was still rising; the ring is the first sample after sight returned |
| thorax | +32 ms | 64 ms | peak no earlier than 55 ms before impact | the same, from −55 ms |

So the truthful sentence is "the pelvis peaked somewhere after −129 ms, where this camera cannot
see the turn", and the screen said "σ 47 ms", a reason that was not the reason. The bound was in the
swing document all along, and the older review chart prints it ("peaked after −129 ms, out of this
camera's sight", `chart_metrics.cpp:1074`); the Charts panel, which replaced it, says "still speeding up
at impact" when that was the gate and otherwise prints the node's σ, whatever refused it
(`charts_traces_presenter.cpp:1032`). Inside the blind
stretch the face-on pelvis rate dips to −400 °/s at −55 ms (a pelvis turning backwards
mid-downswing), which the chart draws as a solid curve: the very samples the gate distrusts are the
ones that make the refused peak look obvious. Five of the session's six shots put the pelvis
"peak" 18–51 ms after impact; all six were refused for the same gate.

Mark knew the rule existed and still had to ask. That is the bug this document is about: **the app
has many careful gates and no way of saying which ones fired, on what evidence, and what they
blocked.**

## 2. The problem

Every producer in the pipeline guards its output. A non-exhaustive list (the full census is §8):
the kinematic sequence's sighted band, σ ceiling, reversal spike guard, rising-at-the-end refusal
and credible-club floor; the two-camera trunk route's consistency floor; the shaft tracker's tier
ladder and gross-error gates; the impact camera's estimate gates; the IMU integrity check; the
capture-hole clip; pose keypoint confidence floors; the geometric span-ratio gates and phase
domains of `metric_presentation_honesty.md`; the diagnostics' minimum-shot and `withheld` rules;
and the route quality every catalogue route carries (Direct / Estimated, Inertial / Projected /
Triangulated).

Each gate makes a good decision and almost all of them **collapse it to one bit** on the way out:
`placed = false`, `valid = 0`, a missing key, a `-` on the card. What is lost:

1. **Which gate fired.** A node refused for being out of sight and a node refused for σ look the
   same, and the panel picks a σ to print because σ is the field it has.
2. **The evidence.** The numbers the gate compared ("|turn| 14° < 20°", "rate rising 129 ms
   before impact").
3. **What the gate could still say.** Bounds, ranges, "drawn but not judged". Some of this is
   stored (the KS bounds) and simply not read; most is not stored at all.
4. **What it blocked downstream.** A refused pelvis node means no sequence order, which means
   `late_pelvis_rotation` cannot be judged on that shot, which means the session's pattern count
   for it is out of n−1, not n. Nobody can see that chain today.
5. **What would lift it.** For this gate: a second camera view (the two-camera route places these
   nodes on 4 July) or a pelvis IMU. For a σ ceiling: nothing the golfer can do. The difference
   matters to the reader and to Mark planning sessions.

The consequence is not only confusion. A refused reading that says nothing invites the reader to
trust the curve over the gate, which is exactly backwards in the cases the gate exists for.

## 3. Principles

1. **A refusal is a finding.** Every `-` has a reason the reader can open, in plain words, with
   the numbers. "Not placed" with no reason is a defect.
2. **Say what is still known.** A gate that blocks the exact value often leaves a bound, a range or
   an order. Show it ("after −129 ms") rather than nothing.
3. **The reason shown is the reason that fired.** Never substitute a field that happens to be
   available (the σ in §1).
4. **Gates are data, explanations are written once.** One catalogue entry per gate, read by every
   surface, so the words do not drift between the chart, the diagnostics and the docs.
5. **Gate on data, never on session type** (`analysis-is-session-agnostic`). A gate fires because
   of what was captured on this swing, and its record says so in those terms.
6. **What would lift it is stated as a route, not an instruction.** "Measured on swings with the
   down-the-line camera or a pelvis IMU" is information; "move the camera" is not something the
   app says (camera fallback is best effort and honest).
7. **Trust is a property of a reading, not of a metric.** The same metric is Measured on one shot
   and Bounded on the next; the catalogue's route quality is the ceiling, the gates decide where
   under it this reading landed.
8. **No new confidence without evidence.** This design labels and explains; it does not loosen any
   gate. A gate is changed only by its own design, on graded results.

## 4. The model: gates, gate records, trust levels

### 4.1 Gate

A named check in a producer that can block, bound, withhold or downgrade an output. Identified by a
stable id, `area.gateName` (e.g. `sequence.sightedBand`, `sequence.maxPlaceSigma`,
`shaft.tierSynth`, `imu.integrity`). Where a tuned constant already exists, the id matches its
tuning key (`sequence.sightedTurnDeg` → gate `sequence.sightedBand`), so the catalogue, the tuning
overrides and the code agree.

### 4.2 Gate record

What a gate leaves on a swing when it fires. One record per firing:

| Field | Content |
|---|---|
| `gate` | the gate id |
| `subject` | what it acted on: a metric key, a metric key + phase, a sequence node (`ks.pelvis`), a characteristic id, a whole route (`route.faceOn+dtl`) |
| `effect` | `refused` (no value) · `bounded` (a range instead of a value) · `withheld` (value shown, not judged) · `downgraded` (value kept at a lower trust level, e.g. route fallback) · `masked` (samples invalidated, the reading survives) |
| `evidence` | the numbers the gate compared, as a small map with units (`{ "sightLostMs": -129, "rateAtEdge": "rising" }`); never prose |
| `bound` | when `effect = bounded`: `{ "noEarlierThanMs": 129 }` or `{ lo, hi, unit }` |
| `blocks` | filled downstream (§4.4): the characteristics, orders and verdicts this record prevented |

Records are evidence, not sentences: the words come from the catalogue (§5) filled from `evidence`,
so a record written in October reads correctly with words revised in December.

### 4.3 Trust level of a reading

One of five, derived from the reading's route and the records against it, never authored:

| Level | Meaning | Example |
|---|---|---|
| **Measured** | a Direct route, every gate passed | lead arm peak from the arm IMUs |
| **Estimated** | an Estimated / Projected route, every gate passed | club peak from the face-on club track |
| **Bounded** | the value is refused, a range survives | pelvis peak "after −129 ms" |
| **Drawn, not judged** | shown, kept out of every finding (`withheld`) | a node whose route may not carry a verdict |
| **Not seen** | refused, nothing survives | thorax spike at the transition |

The levels are ordered and a reading takes the lowest that applies. Measured vs Estimated already
exists in the catalogue (`via(..., Direct | Estimated)`); the other three are what gate records add.

### 4.4 Propagation to findings

The diagnostics engine already knows which readings each measure and characteristic reads. When a
characteristic cannot be judged on a shot because an input reading is Bounded or Not seen, the
engine adds the characteristic to that record's `blocks` and records "not judged" against the
characteristic, with the record id as its cause. A session pattern then reads "late pelvis: 0 of 0
judged (6 not judged: pelvis peak out of sight)" instead of a silent "0 of 6".

**As built (stage 4, 16 October 2026).** The diagnostics' per-swing grid carries the swing's records
(`swing_phasegrid.json` schema 8). When a condition or a sequence check cannot be assessed because a
measure is missing or withheld, and that measure's metric stands on a sequence node a gate refused
(`gates::subjectForMetricKey`, `gates::primaryRecord`), the ledger row records the gate and the reading
(`notJudgedGate`, `notJudgedSubject`, ledger schema 8) and its reason reads the gate's clause ("pelvis
peak out of sight"). `blocks` is derived from the ledger rather than written back into the swing
document: the Trust panel lists, under each reading, the findings that name it. Session diagnostics
groups a shot's not-judged findings by that reading, each opening its reason (Trust panel, else in
place), keeps a condition judged on no shot visible when a gate is the reason, and states per group
"pelvis peak out of sight on 6 of 6 shots, so not judged there: …", with each finding's own judged
count where it varies. No verdict is drawn from a bound yet (open decision 2).

A Bounded reading may still decide a characteristic when the bound alone settles it (a pelvis peak
"no earlier than 30 ms after impact" is late whatever its exact time). That is the characteristic's
call, made explicitly in its measure; the default is "not judged".

## 5. The gate catalogue

A manifest beside the metric catalogue (`src/Metrics/gate_catalogue_manifest.cpp`), one entry per
gate:

| Field | Content |
|---|---|
| `id` | `sequence.sightedBand` |
| `area` | Kinematic sequence |
| `title` | "Out of the camera's sight" |
| `explain` | one paragraph for the reader: "A single face-on camera sees the hips and shoulders turn only while they are turned away from it. Within 20° of square it cannot tell how fast they are turning, so a peak there cannot be timed." |
| `evidenceText` | the sentence filled from a record: "Out of sight from {sightLostMs} ms; the rate was still {rateAtEdge}." |
| `effects` | which of refused / bounded / withheld / downgraded / masked it can produce |
| `tuning` | the tuned constants it reads (`sequence.sightedTurnDeg = 20°`), shown with their values |
| `lift` | routes that do not have this gate, as data: `["faceOn+dtl", "pelvisImu"]`; the words are generated ("measured on swings with …") |
| `design` | the design doc section that justifies it |
| `since` | the stage version that introduced it |

The catalogue is the reminder Mark asked for: a **Gates page in the Metric Library** lists every
gate by area with its explanation and current thresholds, and each metric's library entry lists the
gates that can act on it. A unit test asserts every gate id a producer emits is in the catalogue,
and every catalogue entry is emitted by some producer (no dead entries).

## 6. Gate records in the swing document

- **Where:** `analysis.gates`, an array of records (§4.2). One array per swing, not per metric, so a
  record about a route or a characteristic has a home.
- **Who writes them:** the producer that owns the gate, through one helper
  (`GateLog::fire(gate, subject, effect, evidence, bound)`) passed down the pipeline the way the
  analysis context is today. No producer formats words.
- **Versioning:** the metrics, segment rates, kinematic sequence and diagnostics have no stage
  version and are recomputed on every analysis (`analysis_versions.h`), so their records appear on
  the next re-analysis with no bump. A *reused* stage (pose, ball, shaft) that starts emitting records
  must bump its `k*StageVersion` (`version-gated-reanalysis`), or a reused result would carry none.
  `analysis.gates` gets its own `gatesVersion` so a reader can tell "no gate fired" from "analysed
  before records existed"; the latter reads "reason not recorded (analysed before records existed)"
  wherever a reason would show, and falls back to today's behaviour.
- **Size:** records are per firing, not per sample. A face-on swing fires perhaps 10–30; a sample
  mask (`masked`) is one record per run, not per sample.
- **Existing fields stay.** `KsNode.placed`, the bounds, `MetricSeries::withheld` and `valid` remain
  the machine-readable outputs consumers already use; the record is the explanation beside them, and
  for one release both are written. A later pass may derive the old fields from the records.

## 7. How it is shown

### 7.0 Where a reason lives (decided 16 October 2026)

**The reason sits where the claim is made, at the claim's weight, and opens on demand.** A claim
carries one quiet mark: the bound or the gate's short words ("after −129 ms", "out of sight",
"not judged"), set in the link colour where it can be opened. The reasons themselves never sit above
the figure they explain.

- **The home is the Trust panel** ("Readings and their limits", `src/Gui/trust/PpTrustPanel.qml`), a
  stage panel beside Charts and Session diagnostics, turned on in View, following the picked shot. It
  holds the shot's head sentence (§7.4, the shot half), one foldable section per reading a gate touched
  (`PpTrustReading`: the reading, its trust level, and per gate what it compared, with why the gate
  exists, its threshold and which setups do not have it folded underneath), and the session's count of
  each gate by reading.
- **A mark anywhere opens its reading there** (`TrustFocus.open(subject, swingDir)`): a key-figure
  foot, the sequence strip's "Why", an out-of-sight bar, a dashed ring; later a finding not judged in
  Session diagnostics, a "-" on the launch monitor board.
- **With the panel off, the same reading expands in place** under the claim (`PpTrustInline`), full
  width, until closed, with "Show in the Trust panel" on its head line. A reason is never more than one
  click away and never behind a setting.
- **Popovers are not used.** They are transient, cannot hold folds and render as small as their anchor.
- **One statement for every surface:** `src/Gui/trust/trust_readings.h` builds the readings from the
  records and the catalogue; the panel, the in-place block and the marks all read it.

### 7.1 On every reading (the question "can I trust this?")

- A refused or bounded value never shows a bare `-`. It shows the bound when there is one ("after
  −129 ms") or the gate's title ("out of sight"), in the muted style, and opens the record on click.
- A small trust mark beside every value: none for Measured, `est.` for Estimated, `≥`/`≤` for
  Bounded, `not judged` for withheld. Hover gives the level's one-line meaning.
- Opening it shows the record: the gate's explanation, the evidence sentence, the tuning value, what
  it blocked (§4.4) and which routes would not have this gate.

### 7.2 On charts

- A gate that masks or blinds part of a curve marks that stretch with a **bar along the foot of the
  plot** in the curve's colour, spanning the stretch and ending where the curve ends, labelled with the
  gate's short words (Mark, 16 Oct: a bar, not a wash over the canvas). For the kinematic sequence: the sighted band per segment, so the −400 °/s
  pelvis dip is visibly inside "out of sight".
- A ring the producer did not place is drawn hollow and dashed (today it is a hollow ring that reads
  as "placed but faint").

### 7.3 On findings

- A characteristic not judged on a shot lists the gate behind it. A session pattern states its
  denominator honestly: "judged on 2 of 6 shots".
- The diagnostics' fired-ledger and the Work-ons screen never rank a pattern judged on fewer than the
  existing minimum without saying so.

### 7.4 Per shot and per session: "How far can I trust this?"

A short trust summary, generated from the records and the catalogue, at the head of Analyse (a fold,
collapsed by default once read). *As built (stage 5, 16 Oct 2026): see "Stage 5 as built" below; the
fold is closed by default, always (Mark), and the example's wording is superseded.*

> **Face-on camera and lead-arm sensors.** Club and lead arm measured on all 6 shots. Pelvis and chest
> timing out of the camera's sight on 6 of 6 shots, so the sequence order and late-pelvis findings
> are not judged; these are measured on swings with the down-the-line camera or a pelvis sensor.

The same vocabulary as the Setup page's capability rows (measured / estimated / unavailable), so the
page that predicts what a setup can measure and the page that reports what it did measure agree.
Setup can then add a line per family: "with this setup, pelvis timing is usually out of sight".

**Stage 5 as built (16 October 2026).**

- *One statement.* `trustSummary(shot)` / `trustSummary(session)` in `src/Gui/trust/trust_readings.{h,cpp}`.
  The Trust panel's head reads it (its few words as the standfirst, its lines under them), and so does the
  fold "How far can I trust this?" (`src/Gui/trust/PpTrustSummaryFold.qml`) under the This shot and Session
  heads of Session diagnostics in every register (Analyse and Replay), and set open in the printed report. The fold is closed by default and shows only its few
  words ("pelvis and chest out of sight · club drawn, not judged"); nothing is added to the chart area. A
  gated line's "Why ›" opens the reading (§7.0). The Session view's separate not-judged line is folded in.
- *Lines.* The routes that timed the sequence, each with its readings ("the face-on camera (pelvis, chest),
  the arm sensors (lead arm)…"); what passed, in the Setup page's words (measured / estimated, from the
  node's route quality); per gate, the readings it acted on in its own words, what it kept from being judged
  (named when three or fewer, else counted), and its lift. Session lines carry "on n of m shots" over the
  shots that carry records; older shots are named and not counted.
- *Vocabulary.* "Unavailable" is the resolver's word for "no route on this setup" and reads "Not recorded"
  on the Setup page. A gated reading is not unavailable (a route ran and a gate refused it), so it takes the
  gate's words, never "not recorded".
- *Scope.* Only the kinematic sequence records gates today, so the summary speaks for its four readings and
  says nothing of other families (no "everything else measured"). Stage 6 widens it area by area.
- *Devices.* The Charts snapshot carries no device list; the routes that ran (`node.routeId`, in the
  catalogue's `routeWords`) stand in for the opening "Face-on camera and lead-arm sensors".
- *The Setup line.* A gate entry may name the readings it binds because of where the capture stands
  (`GateEntry::setupBound`; only `sequence.sightedBand`, on pelvis and chest). `summariseSetup` notes it on
  the group of a reading whose winning route on the planned setup is not one of the gate's lifts, and the
  Ready page says it under the family row: "pelvis and chest peaks often out of sight with this setup". No
  other gate is forecast: the rest depend on the swing, and "usually" would need the library counts of §7.5.

### 7.5 The Gates page

In the Metric Library: every gate, by area, with its explanation, thresholds, effects and lifting
routes; and per gate, how often it fired on the current library (a count, from the swept records).
That count is also the developer's view: a gate firing on 100% of a setup's swings is either right
about that setup or wrong about everything.

*As built (stage 7, 16 Oct 2026):* Settings → Diagnostic Model → Gates. Every catalogue entry (per-swing
gates and rules), filterable by area, kind and effect. The counts come from the swings' phase-grid sidecars,
read in the background, and are broken down by session. The page is read-only. A gate firing on every swing
is not coloured (colour means outside the corridor); its count says so.

## 8. The census of gates in the code today

The full inventory, gate by gate with file and line, is
`docs/research/data/gates/census_20261016.md`. About 125 gates across eleven areas. What it shows:

### 8.1 By area

| Area | Gates | Recorded with a reason today | Reaches the reader today |
|---|---|---|---|
| Kinematic sequence | ~40 | the bounds only (`peakNoEarlierThanMs` / `peakNoLaterThanMs`); `placed` with no reason; pair-route refusals in the log | the older review chart prints the bound; the **Charts panel prints σ for every unplaced node** except one still rising at impact (`charts_traces_presenter.cpp:1032`) |
| Body rotation, trunk, posture | ~18 | sample masks (`valid`) | dashed curves; refused metrics simply absent |
| Club and shaft | ~25 | whole-track refusals (`refusedReason` 1–4), per-sample tier and gross risk | ⚠ on the carousel for a refused track; per-sample gates not at all |
| Impact camera | 4 | `headAssumed` | no |
| Pose | 8 | per-keypoint tier and σ | no |
| IMU | 11 | `imuIntegrity`, binding calibration, wrist `lowConfidence` | ⚠ and the IMU panel |
| Launch monitor | 8 | nothing (fields dropped) | "-" |
| Diagnostics | ~22 | the ledger's `SignalRefusal` reasons, `notAssessableReason`, `dataWarning` | yes, the best-served area |
| Charts presenters | ~11 | not recorded (computed on read) | as sentences |
| Phases | ~9 | event confidence, `TimingClass`, fusion decisions | a faded tick |
| Other (capture holes, skeleton3d, ball, reducers) | ~8 | `captureIntegrity`, `skeleton3d.reason` | ⚠, 3-D view |

### 8.2 What it means for the design

1. **The diagnostics already do most of this for findings.** `SignalRefusal` and the ledger's
   reasons are a gate record in all but name. The new record format should be shaped so the ledger's
   reasons map onto it, not alongside it.
2. **The producers are where the reasons die.** Kinematic sequence, body metrics, club, pose and the
   launch monitor decide well and record a bit. That is where stage 2 onwards does its work.
3. **Some surfaces ignore reasons that ARE recorded.** The bound in §1 is in the document; the Charts
   panel prints σ instead. Fixing that needs no new data and can land before everything else (§9 stage 0).
4. **Several gates are in the log only.** The two-camera route's refusals (`diag.refusal`) explain
   exactly why a swing fell back to face-on, and nobody reading the app can see them.
5. **Dead or warn-only entries exist.** `kTriMinBaselineM` has no consumer; the uncalibrated-binding
   check in the re-analyser warns and does not gate. The catalogue pass should remove or reclassify
   them rather than catalogue them.
6. **Reading confidence is constant in the diagnostics.** `live_measure_source.cpp:124` sets it to 1.0,
   and phase confidence never reaches a finding. The trust level (§4.3) gives that field something
   true to carry.

## 9. Build order

Each stage is one session, graded on screen before commit.

0. **Use what is already recorded** (no new data). The Charts panel's sequence strip reads the node's
   bounds and rising-at-the-end flag the way the review chart already does (`chart_metrics.cpp:1068`),
   and stops printing σ as the reason for an unplaced node unless σ is the gate that fired. This is
   the shot in §1, fixed in an afternoon, before the rest of the design lands.
1. **Catalogue and record format.** `GateCatalogue`, the manifest with the census's entries (§8),
   `GateLog`, `analysis.gates` serialisation, the catalogue tests. No producer emits yet; nothing
   changes on screen.
2. **Kinematic sequence emits** (the case in §1): sighted band (with its bounds), σ ceiling, spike
   guard, rising-at-the-end, credible-club floor, pair-route consistency. Stage version bump; the
   18 August and 4 July sessions re-analysed as the test set.
3. **Charts read it for the sequence:** the strip's "not placed" replaced by the record (bound, title,
   open-to-explain), the sighted band shaded on the curves, unplaced rings dashed. This is the first
   point Mark sees a change.
4. **Propagation to findings** (§4.4) for the characteristics that read the sequence; "judged on n of
   m" in Session diagnostics.
5. **The trust summary** (§7.4) and the Setup page line. *Built 16 Oct 2026; see §7.4 "Stage 5 as built".*
6. **Remaining areas, one per session, ordered by how often their gates fire on the library:** club
   and shaft tiers; impact camera; pose confidence and the span-ratio gates; IMU integrity and capture
   holes; launch monitor corroboration; the diagnostics' own minimum-shot rules. *Built 16–17 Oct 2026
   in two sessions instead of one per area (record everything, then show it everywhere); see
   `trust_stage6_plan.md`.*
7. **The Gates page** in the Metric Library, with library firing counts. *Built 16 Oct 2026; see §7.5.*

Stages 1–3 are the minimum that answers the question that prompted this.

## 10. Definition of done

1. Every gate in the §8 census is in the catalogue, or removed from the code, or listed here with the
   reason it is not a gate.
2. Every refused, bounded or withheld reading on screen opens to the record that refused it. No bare
   `-` where a gate fired.
3. No surface shows a reason other than the one recorded (§3.3).
4. Every characteristic not judged on a shot names its cause; every session pattern states the number
   of shots it was judged on.
5. The trust summary exists for every analysed session, and agrees with the Setup page's vocabulary.
6. A unit test ties emitted gate ids to the catalogue both ways.

**Where each is met (17 October 2026).** 1: the catalogue holds the per-swing gates and the on-read rules
(`gate_catalogue_*.h`); what the census listed and is not a gate is below. 2: every surface reads the record
through `TrustWords` / `PpGateNote` (Charts, Session diagnostics in Analyse, Replay and Print, the 3-D view,
shot card, edit panel, camera frames, data viewer, launch monitor board). The camera chips, the Table's
"Not shown" line and the launch monitor field words could not be rendered offscreen; they are checked
in the app.
3: words come only from the catalogue entry of the recorded gate. 4: stage 4 (§4.4) and the diagnostics'
`rule.diag.*` refusals. 5: stage 5 (§7.4). 6: `gate_catalogue_test`.

**Census entries that are not gates, and why** (not recorded): capture-time and device checks with no swing
to attach to (phone-shot corroboration, the sensor impact detector, filter seeding, the calibration axis
test, launch monitor pairing and standalone refusals); provenance labels (sample tier, timing class); the
phase-domain mask and the clubhead speed past the club's impact position (a metric's defined span, not a
refusal); σ-only filters (the pose smoother's coasting, an assumed club length); the phase retiming and
fusion abstentions (silent on reuse, no change for the reader); the impact anchor (reused, not stored);
bridged detector gaps (a hold, not a refusal).

## 11. Decisions taken, and the ones still open

**Taken (16 October 2026):**
- Gates become data: a catalogue written once plus per-swing records (Mark).
- This design explains and labels; it changes no gate threshold.
- The kinematic sequence is the first producer, because it prompted this.
- Record retention (was open 4): the library is re-analysed on the studio after each stage lands, with a
  backup on the share first (Mark, 16 Oct).
- The design closes with open 1–3 still open (Mark, 17 Oct). They are styling and per-finding policy
  calls for later sessions, not missing pieces of the model.

**Open:**
1. **Trust mark styling.** Text marks (`est.`, `≥`) or a glyph; decided in the typeset playbook with
   the stage audit, not here.
2. **Bounded readings in findings.** Which characteristics may decide on a bound alone (§4.4) is a
   per-characteristic call, to be made with the diagnostics norms, not by default.
3. **Showing the best guess.** Whether a Bounded reading also shows the refused value ("+45 ms,
   estimated, out of sight") greyed. The 16 October discussion argued against it for the sequence:
   on that shot the guess is very likely wrong and reads as "pelvis late".
*(4, record retention: taken, see above.)*
