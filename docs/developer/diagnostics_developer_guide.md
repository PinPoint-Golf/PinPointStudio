# Pinpoint Diagnostics — Developer Guide

**Audience**: developers working on the diagnostics model — characteristics, measures, norms, contexts, the causal graph, or the screens over them
**Location**: `src/Diagnostics/` (the model, in four folders by layer, lowest first: `model/`, `content/`, `derived/`, `engine/`; §14), `src/DomainModel/` (the owners that run it: `Session` → `SessionDiagnostics`, `Athlete` → `AthleteDiagnostics`, `RuleData`), `src/App/athlete_catch_up.*` (the catch-up), `src/Core/work_pool.*` (the one pool), `src/Gui/diagnostics/` and `src/Gui/diagnosticmodel/` (the thin adapters, the `ModelBrowser` façade and the panel views), `src/Resources/diagnostics/` (the content)
**Language**: C++17 value types and rules, Qt-only, no Qt-GUI below the façade layer
**Status**: the CONTENT layer is production and shipped. The EXECUTION layer runs (refactor step 3's build items, closed by 3.10 on 22 Nov 2026; step 3.0 and the re-fire are still to do, 04 step 3's status): every shot of every loaded session is fired by `fireFindings()` and the session diagnosed by its own `SessionDiagnostics`, panel or no panel; the athlete level is `AthleteDiagnostics`, over the session summaries. **§6.0 says who runs what.** Section 8 is the breakdown of what is live and what is dormant; read it before assuming any one piece runs.

---

## Contents

1. [What this subsystem is](#1-what-this-subsystem-is)
2. [Where it fits](#2-where-it-fits)
3. [The data model](#3-the-data-model)
4. [The two joins, and how resolution works](#4-the-two-joins-and-how-resolution-works)
5. [Grading](#5-grading)
6. [Detection and explanation](#6-detection-and-explanation) — **§6.0, who runs it: the owners**
7. [Providers, layering and persistence](#7-providers-layering-and-persistence)
8. [**Live, dormant, planned — the full breakdown**](#8-live-dormant-planned--the-full-breakdown)
9. [Validation and health](#9-validation-and-health)
10. [The GUI façade — `ModelBrowser`](#10-the-gui-façade--modelbrowser)
11. [Adding things](#11-adding-things)
12. [Testing](#12-testing)
13. [Traps that have already cost time](#13-traps-that-have-already-cost-time)
14. [File map](#14-file-map)

---

## 1. What this subsystem is

Five registries and the rules that join them.

| Registry | Owns | Lives in |
|---|---|---|
| **Metric catalogue** | what can be measured, and what it means | `src/Vocabulary/Metrics/` — see its own guide |
| **Characteristic pack** | measures, signals, characteristics, causal edges | `src/Diagnostics/` + `core.json` |
| **Norm set** | what normal looks like, per measure per context | `src/Diagnostics/` + `norms.json`, `contexts.json` |
| **Screen set** | the physical tests a `screenRef` names — protocol and pass criterion | `src/Diagnostics/screen_pack.*` + `screens.json` |
| **Drill set** | what a golfer does about a characteristic | `src/Diagnostics/drill_pack.*` + `drills.json` |

The last two are flat reference lists rather than provider hierarchies, and deliberately: the
pack/norm polymorphism exists so a COMMUNITY pack can be namespaced against core, and there is no
community story for a screen. They layer a user file over the shipped one and stop there.

The design rule that shapes everything: **numbers are content, not code.** No corridor is compiled in. Before stage 9 of the norms work there was a table in `reference_bands.cpp`; it was migrated behind a byte-for-byte parity gate and then deleted along with the gate, because a parity test that outlives what it compares against pins the shipped content to a frozen table and blocks the first legitimate re-seat.

The second rule: **a metric describes itself and does not judge itself.** `MetricDescriptor` carries identity, units, meaning and requirements. It carries no bands. Judgement belongs to a norm, which is keyed on a *measure* — and a measure is a metric plus the decision of which reading is meant.

---

## 2. Where it fits

```
 CONTENT (reviewable JSON, shipped in Qt resources)
   core.json          measures · signals · conditions · edges
   norms.json         norms  (measure, context) -> mu/sigma/monitor + provenance
   contexts.json      the shot-type tree
   screens.json       the physical tests a screenRef names — protocol + pass criterion
   drills.json        what a golfer does about a characteristic
        │
        │  resource_pack_provider / resource_norm_provider  (+ file_* for the user's own,
        │  merged_* to layer them)
        ▼
 MODEL  (C++ value types + rules, no Qt-GUI)
   characteristic.h        Measure · Signal · Condition · Edge  + their enums
   characteristic_pack.h   the pack, the metric->measure join, validatePack()
   norm.h                  Norm · GradePolicy · grade() · bandEdgesOf() · ragOf()
   context_tree.h          the tree, chain(), resolveContextBinding()
   metric_corridor.h       (metric, phase) -> measure -> norm -> four band edges
   characteristic_engine.h detect() -> findings          ◄ live: fireFindings → detectForSwing (§6.0)
   relation_resolver.h     findings -> ranked causes     ◄ live: the session adapter's explanation (§6.0)
   fire_findings.h         a shot's findings + derived data, pure (§6.0)
   session_reduction.h     a session's diagnosis from its shots' findings, pure (§6.0)
   measure_sample.h        read a measure off a stored swing (phase grid + sidecar)
   diagnostics_health.h    assembled-library checks
        │
        ├──────────────► Wrist/reference_bands.h  NormBandProvider
        │                   projects a Norm into a Band for the WRIST GRID  (live)
        │
        ├──────────────► Gui/review/metric_catalog.cpp
        │                   corridors for MetricDetail, PpBandRail, 3 dashboard zones  (live)
        │
        ▼
 FAÇADE  (QML_ELEMENT, marshalling only — no rules)
   ModelBrowser        one connected content graph, read AND edited (§10)
   layout/             dag_layout.h (the causal graph) · corridor_plot.h (the corridor picture):
                       pixel layout in plain C++, in the UI ring beside the façade
        │
        ▼
 VIEWS   Settings → Diagnostic Model (navIdx 9) · Settings → Metrics · the wrist grid
```

And the EXECUTION side, which runs the model on the golfer's shots (§6.0):

```
 RuleData (src/DomainModel)       one immutable snapshot of packs, norms, flight table, grade policy
        │ (EVT-12/13 on an edit: every loaded session re-fires)
        ▼
 Athlete ──owns──► Session ──owns──► Shot            (src/DomainModel; reached only through their owner)
    │                 │
    │                 └─► SessionDiagnostics          fires each shot (fireFindings) and diagnoses the session
    │                       (reduceSession) as JOBS OF VALUES on its own serial lane of the one pool
    │                       (src/Core/work_pool.h); keeps and writes diagnostics.json + work_ons.json
    └─► AthleteDiagnostics                            tendencies, themes, marks, distances, club baseline —
          from the session SUMMARIES only; its own lane     from work_ons.json only
 AthleteCatchUp (src/App)                            sums up stale ended sessions, oldest first, one at a time
        │
        ▼
 THIN ADAPTERS (src/Gui/diagnostics)                  SessionDiagnosticsModel per surface, WorkOnsController for
                                                      Home: words and view state only; made by C++, never by QML
```

**All logic stays in C++.** QML renders shapes and holds no rules: it does not walk the context tree, does not decide what "inherited" means, does not compute a band edge from a policy, and does not lay out the causal graph. Every one of those is a statement about correctness, and a statement about correctness written in a delegate is a statement nothing can test.

---

## 3. The data model

### Measure — a metric plus which reading

```cpp
struct Measure {
    QString       id;              // stable, never reused
    MeasureKind   kind;            // Provided (a metric key) | Composed (facet-built)
    Series        series;          // Composed only
    Reducer       reducer;         // WHERE the phase lives. At / Delta / Rate / Extremum
    QString       metricKey;       // Provided only — links into MetricCatalogue
    QString       label;
    QStringList   aliases;
    QString       unit;
    ViewNeeded    viewNeeded;      // FaceOn | DownTheLine | Any
    MeasureStatus status;          // Live | Planned | NoProducer | NotCapturable | ExternalDevice | Held
    QString       gapReason;       // NotCapturable: why, in one line
    QString       highMeans;       // what a HIGH value means, in the measure's own words
};
```

**`ExternalDevice` is the third answer to "why is there no value", and not a shade of either
neighbour.** A launch monitor resolves face angle, spin and strike location; integrating one is work
we intend to do, so `NotCapturable` would be false. But the work is an INTEGRATION, not a producer
written from our own pixels, and a roadmap row reading "build a spin-axis producer" sends somebody
the wrong way — so it is roadmap-eligible and **sectioned apart** in `roadmap()` and
`roadmapMarkdown()`, while `captureGaps()` stays `NotCapturable`-only. The per-shot half lives on
the requirement (`MetricRequirement::launchMonitor`): a golfer with no launch monitor gets "needs a
launch monitor" through the same path a missing face-on camera takes, and the day a connector sets
`ShotContext::hasLaunchMonitor` the same metric resolves Measured with no content change. A norm on
one is legitimate — it is what the reading will be graded against — which is why `normNotCapturable`
stays scoped to `NotCapturable`.

**Only `Live` grades (#176).** `measureStatusGrades()` (`measure_vocabulary.h`) is one definition,
applied on every path a verdict rests on (`packMeasureGrades()` asks it of a pack). In the engine's read
(`characteristic_engine.cpp` `evaluate`), a signal reading a measure of any other status — `held` and `externalDevice` included — is refused as
`SignalRefusal::NotLive` (`rule.diag.notLive`) before the source is asked, on condition signals,
`evaluateSignal`, order-chain fallback links and `requiresBeforeImpact` companions, for per-swing and
session-scope sources alike. Outside the engine, `packMeasureGrades()` gates the session model's reads a
verdict rests on (timing-spread peak times, swing length), and every flight verdict reads a shot's
launch-monitor readings through `GradedReadings` (`ShotRecord::ungradedReadings`, schema 15) — the
strike and Sky checks, the session's usual flight, the flight band, the strike factor, the carry lost,
which shots leave the club distances (`shotMishitKind`, "far short" included) and the carry block's
breakdown of a short carry. Display is the named exception: club distances' reductions, the consistency
and delivery charts, the numbers quoted beside an outcome label and the carry block's headline ("short by
X", shown only when `carry_deficit` fired — off a live carry measure, or off the live `m_carryShortfall` when the carry itself is held) read the readings plainly, so a
held one is still shown. Status is per MEASURE, not per data key: holding `m_carryDistance` does not hold
`m_carryShortfall`, which reads the same carry. The ledger row records the
status (`notLiveStatus`, schema 15) and reads "held back until it is trusted to grade" for `held`.
Composite parts and the DAG's availability call the same predicate. The launch-monitor measures grade
because they are `live`; an `externalDevice` measure grades the day it is flipped.

**Every measure must state its status.** `Measure`'s default is `NoProducer`, so a pack measure with no
`status` key loads as no-producer and nothing grades on it; the loader warns (`measureNoStatus`). The
same goes for a test fixture built in code: its measures must say `Live`.

Three fields carry more weight than their size suggests:

- **`reducer` is where the phase lives.** There is no phase field. `At{p7}` is impact; `Delta{p1→p4}` is the change to the top; `Extremum{p5..p6, Min}` is a trough across a stretch. Two measures over one metric with different reducers are *related, not duplicates* — that is the whole point of the split, and `m_leadWristAtImpact` (absolute) versus `m_leadWristFlexExt_p7` (Δ from address) is the case that proves it.
- **`windowMs` reads a span no phase bounds**[^ps-d5]. `"windowMs": [startMs, endMs]` is a span in whole milliseconds after the reducer's `anchor`: `[t(anchor) + startMs, t(anchor) + endMs]`, where `t(anchor)` is the instant of the metric's own value at that phase. It exists because some readings sit between phases that are unreliable: `m_leadElbowFoldAfterImpact` reads the 100 ms after impact because P8 is often mistimed.
  - `extremum` with `windowMs`: the windowed-mean extreme over the span **less the value at the anchor**, the same signed deviation an anchored phase-window extremum reports (`m_spineBendDive`). So `{ "kind": "extremum", "anchor": "p7", "windowMs": [0, 100], "sense": "max" }` is "the most it reached in the 100 ms after impact, beyond its value at impact".
  - `delta` with `windowMs`: `value(t(anchor) + endMs) − value(t(anchor) + startMs)`, each a ±15 ms median; at `startMs` 0 the start is the anchor's own value.
  - The validator (`badReducer`) refuses a negative or reversed or empty span, a span on `at` or `rate`, a span with no `anchor`, and a fraction of a millisecond. The loader refuses `windowMs` written beside a phase `window` (the struct cannot hold both, so the loader is the one place that sees it). The domain check reads only the anchor: a metric masked past its domain has no valid sample there, so the span reads nothing rather than reading outside it.
  - A missing anchor phase leaves the measure not assessable, as an unsegmented phase-window end does; a span with nothing valid in it likewise.
  - **Coverage (D5 round 3).** A span reads only when its valid samples cover it: no stretch longer than `reduce.msSpanMaxGapUs` (30 ms) unread, its two ends included (`PhaseGridMsSpan::maxGapUs`, applied when read through `PhaseGridConfig::msSpanMaxGapUs`). Otherwise the measure is not assessable with `MissingKind::WindowNotCovered` ("too little of the moment it is read over could be seen"), never a value built from what is left. This is a gap rule, not a percentage, because a share of frames can be met by the frames before a hidden peak.
  - σ: an extremum's is the span's σ (the larger of the series σ and the anchor value's) ⊕ the anchor's; a delta's is the start's (the anchor's at `startMs` 0, else the series σ) ⊕ the end's (the series σ). An instant between phases has no labelled σ of its own.
  - **The phase grid has to be asked for the span.** A phase window is exact by aggregation over the cached adjacent-phase spans; a millisecond span is not, so `buildPhaseGrid` computes the spans named in `PhaseGridConfig::msWindows` and stores them per metric (`msSpans`, and `source.ms_windows` in the sidecar). `LiveMeasureSource(dir, pack)` and the model browser add the pack's spans themselves (`msWindowsOf`). The sidecar guard is a **subset** test: a reader naming no span accepts any grid, a reader naming a span the sidecar lacks rebuilds it with the **union**, so two packs' readers cannot keep overwriting each other; a reader that may not fat-parse (`writeSidecar` false, i.e. `SidecarPolicy::CachedOnly`) is served the held grid without that span, so only the span's measures go missing, never the swing. (Refactor 3.7-0 split that one bool: `PhaseGridStore::read` (`src/Stores/phase_grid_store.h`) also takes a `SidecarPolicy` (`src/Vocabulary/Common/sidecar_policy.h`), whose `ParseNoWrite` parses a miss or a stale sidecar — rebuilding with the union, as `ParseAndWrite` does — and writes no sidecar: the parallel run's firing reader, in memory only. The bool form keeps its meaning: false is `CachedOnly`, true `ParseAndWrite`.) Every sidecar written before the field lacks the shipped span, so the first pack read of each swing re-parses it once (as a re-analysis does anyway). No schema bump: a sidecar from before the field holds no spans, which every reader that names none accepts. A grid built without the span answers nothing for it.
  - A `windowMs` measure is placed at its anchor (`condition_reading.h`), is never matched as a reading AT a phase (below), and is named "peak P7 +0 to 100 ms from P7" (`canonicalMeasureLabel`) and "max P7 +0 to 100 ms" in the model browser's reducer column.
- **A phase the swing does not have is explained by a record on the phase**[^ps-d6]. A measure whose reducer needs a phase (its `anchor`, or either end of its phase `window`) with no value there is not assessable with `MissingKind::PhaseNotSegmented`. Where the swing's records refuse that phase itself — today only `positions.p8Refused` on `position.p8`, fired when FA's bounded P8 was refused (`Club/p8_refusal_gate.h`) — `LiveMeasureSource::gateCause` names that record, before any record on the measure's keys, so the cell reads "P8 (the shaft level after impact) not timed" rather than "not segmented". The record covers no metric key on purpose: the keys a P8 measure reads also read at other positions. `gates::phaseSubject` maps a phase to its record subject.
- **`highMeans` is a correctness mechanism, not decoration.** It is the sentence an author reads *instead of* picking "High" or "Low" when attaching a signal. Three signals shipped inverted because someone picked a direction against a sign convention that was unstated or the opposite of what they assumed — and an inverted signal fires happily, on the wrong swings, with correct-sounding consequence text attached. The measure picker refuses to mint a measure without one.

### Signal — the rule that watches

```cpp
enum class SignalTest { OutsideCorridor, Threshold, Order, Ratio };

struct Signal {
    QString                  id;
    SignalTest               test;
    QStringList              measures;   // 1 for corridor/threshold, 2 for order/ratio
    std::optional<Direction> direction;  // High | Low — WHICH TAIL
    std::optional<double>    threshold;  // Threshold only
};
```

`OutsideCorridor` is the preferred form: it authors no numbers and inherits the measure's norm, so re-seating a corridor from a corpus updates every signal on it. `Threshold` authors a number and needs a citation to be more than an opinion.

`direction` is not optional in spirit. It names which tail the signal watches, and an axis has two conditions on one norm — without the side check both tails would fire on any deviation in either direction.

### Condition — a characteristic (or a latent cause)

```cpp
struct Condition {
    QString                     id, label, axis;
    QStringList                 aliases;       // coach phrasing that resolves here — the glossary
    ConditionGroup              group;         // Setup | Posture | Lateral | Rotation | Arms | Wrists |
                                               //   PlanePath | Delivery | BallFlight | Golfer — WHAT MOVES
    ConditionKind               kind;          // Fault | Setup | Delivery | Outcome | Capacity |
                                               //   Intent | Equipment — WHAT SORT OF THING
    Prominence                  prominence;    // Rare … Ubiquitous — how common, in this population
    Observability               observability; // Observable | Latent | Both
    ConfirmedBy                 confirmedBy;   // Measured | Screened | Asserted
    ConditionState              state;
    QStringList                 detectedBy;    // signal ids
    LocalizedText               consequence;   // why it matters — always present
    LocalizedText               injuryNote;
    QString                     screenRef;     // Screened: which physical screen
    Provenance                  provenance;    // tier + citation + author
    std::vector<ContextBinding> bindings;      // per-context applicable/material
};
```

Four distinctions the UI must never blur:

- **`ConditionKind`** — what sort of thing it *is*. **Intrinsic, and it never moves when a producer lands.**
- **`Observability`** — can it be *seen* in the swing? A `Latent` condition has no signals by definition; it is resolved by the explanation pass from what it explains, never detected. Also intrinsic — it is a property of the movement, not of our cameras.
- **`ConfirmedBy`** — how it is established **today**. `Measured` (a signal fired, the app knows), `Screened` (a physical test would settle it), `Asserted` (knowable only by asking). **This is the one that is allowed to move**, and it moves when a producer or a launch-monitor connector lands — exactly as `MeasureStatus` does.
- **`ProvenanceTier`** — is it cited, or proposed? A `Proposed` condition must be badged as such.

**`group` is the area: what moves, and nothing else.** The phase is a column in the Consistency table, and Setup is the one
exception, because it is static and has its own chart. A group only classifies. It is not a swing order, and neither
detection, grading nor link grading reads it: a link's direction is the graph's authored direction. Home, Consistency and the
report take areas from `areaGroupOrder()`, which leaves out `Golfer`: the latent causes are filed there for the model
browser and editor and are never an area. The rule, the membership and the old-token alias
(`currentConditionGroupToken`, `characteristic_pack.cpp`) are in `docs/design/swing_areas_regroup.md`.

**That division of labour is the whole of why `kind` exists, and it was learned the hard way.** For a
year the pack had only the last three, and with no field for *what sort of thing this is* the first
two silently took the job on. `over_the_top` shipped `Latent` + `Asserted` — the headers define those
as "cannot be seen in the swing" and "intent, habit, perception", and it is neither; it is the most
visible fault in amateur golf. It was tagged that way because nobody had written it a measure. So the
two epistemic fields had become a record of **producer coverage**, and nothing could report it,
because every row still held a legal value. The same pressure filled `ConditionGroup::Setup` with
fourteen physical screens, nine beliefs and a set of clubs. See trap 15.

**`Delivery` is the kind most easily confused with `Fault`**, and the line is worth stating: path,
face-to-path, attack angle, low point and shaft lean at impact are very nearly a *closed determinant
set* — given them, the ball flight follows. They are the scoreboard of the downswing, not the
movement error, and a coach does not give a lesson on a number. Body and hand positions at impact
(axis tilt, pelvis rotation, a bowed lead wrist) *cause* delivery and are Faults.

**`Prominence` is the base rate, and it is a prior.** No peer-reviewed source counts named swing
faults across a population, so every value is this library's editorial judgement at `practice` tier —
never a citation. It is re-seatable: once the swing library has volume, prevalence is measurable, and
the seat will be biased in a way that must be reported because it can only count what can fire.
`prominenceWeight()` reads 0.05 / 0.10 / 0.20 / 0.35 / 0.60, and **the spread is the design, not the
values** — 12× against `strengthWeight`'s 9.5×, so that prevalence and evidence stay commensurable.
The default is the **middle** rung, deliberately breaking this file's first-value convention: it is
the only default that is harmless when wrong, since this one will multiply into a rank.

**`Asserted` causes are offered, never concluded.** They are surfaced, visually distinct, phrased as a question, and they never count as resolving a finding. Dropping them instead would leave characteristics whose only cause is habit with an empty explanation panel, when the truthful answer is "this may simply be how they set up".

### Edge — the causal graph

```cpp
struct Edge { QString from, to; EdgeType type; Strength strength; Provenance provenance; };
// EdgeType: Causes | Corroborates | Excludes
// Strength: VeryWeak | Weak | Moderate | Strong | VeryStrong
// An edge carries the SAME Provenance as a condition — tier, citation, searchedOn, searchTerms.
// (A bare `QString citation` is the pre-pass-1 shape; the loader still migrates it, promoting a
//  legacy top-level citation to Supported. Do not author that form.)
```

`Causes` must form a DAG — `validatePack()` refuses a cycle, because the assembled library is re-validated after every merge and one circular edge would fail every characteristic, not just the two involved. `Corroborates` is refused between a pair that already has a causal path either way: the pair would double-count when the explanation is ranked.

**Strength is authored in words and read in words.** The stored names are a magnitude ladder; the labels a reader sees are a frequency one — `veryWeak` reads "rarely", `strong` reads "usually" — and the two have never matched. That is deliberate: the outer rungs were added in the naming idiom rather than the label's so `weak`, `moderate` and `strong` keep the spelling every shipped and user pack already stores, and no pack migrates. Nothing renders a strength as a percentage.

`strengthWeight()` (`characteristic.h`) is the one weight the ladder carries: **P(effect | cause)**, 0.10 / 0.30 / 0.60 / 0.80 / 0.95. It is the quantity the labels have always described — how often this cause produces this effect — and it stays strictly inside 0…1. Neither bound is available: `edgeWeight()` returns 0 for "no such edge" and an immaterial finding is weighted 0, so a rung at 0 would be indistinguishable from the claim's absence; and a probability of exactly 1 is absorbing under Bayes, which no authored coaching claim earns.

Two things it is not, both of which it resembles:

- **Not P(cause | effect)**, which is what ranking causes from an observed effect actually wants. Bayes needs a base rate for how common each cause is. **`Condition::prominence` is now that base rate**, and it changes what this paragraph used to say: for a year no `Condition` carried one, so a caller treating this as a posterior was ranking by out-degree — most outgoing edges wins — while looking rigorous. The half that was missing exists; **it is not yet consumed** (see below).
- **`RankedCause::score` is not a probability either.** It sums one weight per covered finding, so a cause explaining four of them scores past 1 and is meant to. The sum is ordinal and only ever reaches a comparison; normalising it would break the greedy set cover it feeds.

The values were re-cut once, from an ordinal ladder running to 1.5, after measuring that the rescale preserved 98% of cause pairings on the shipped pack and that every pair it reordered was a near-tie. Anything that moves them owes the same measurement.

**`prominenceWeight()` is now consumed**, and the score it feeds is:

```
score(cause) = P(cause) × Σ over covered findings of P(effect | cause)
```

The factor enters at `rankWeight()` (`relation_resolver.cpp`) and nowhere else. That lambda is the
single chokepoint both accumulation sites go through — the candidate pass and the greedy re-score —
so one edit covers both; applying it at the call sites would be two edits that must agree forever.
`better()` gained a `1e-9` epsilon, because a sum of *products* can land on one value by arithmetic
differing in the last bit where a sum of five discrete weights could not. **That epsilon is sized for
representation error and must never be widened to restore the tie-breaks** — prominence separating
two previously-tied causes is the change working.

**The measurement needed no API flag, and that is the part worth reusing.** Run `explain()` twice
over the *same* code: once on the shipped pack, once on a copy with every prominence forced to one
rung. Uniform prominence makes `score(c) = k · Σ w(c,e)`, a positive scalar multiple of the old
score, so that run reproduces the pre-change ordering exactly and **is** the control. Nothing
reimplements the greedy pass, so what is measured is what ships. `rank_shift_report`
(`src/Diagnostics/tests/`) is that harness — **deliberately not a ctest gate**, because an assertion
on the preserved-pairing percentage would pin the shipped prominence values and the first coach-led
re-rank would fail it. Its result on the shipped pack:

| | |
|---|---|
| cause pairings compared | 1328, over 409 ranked synthetic finding sets |
| preserved | 1048 (**78.9%**) |
| reordered **by score** | 94 (**7.1%** of all pairings) |
| reordered by the cover picking differently | 186 |

**Read the split before reading the headline.** Only the score-driven number says prominence decided
a pairing; the rest are downstream of the greedy cover choosing a different first cause, and counting
them as "prominence reordered this" overstates the change. And note the two figures are not
comparable to the strength re-cut's 98% — that was a *rescale of an existing term*, this is a *new
term*, and a new term that reordered nothing would not have been worth adding. What matters is
whether the reorderings are defensible: the widest evidence margin prominence overturns is 2.7×,
against a 3× prevalence ratio, so the ladders are behaving commensurably and no large evidence
advantage is being overwhelmed by a small prevalence edge. The report names every reordered pair with
both scores, so that is checkable rather than assertable.

### Norm — what normal looks like

```cpp
struct Norm {
    QString measureId, contextId;      // the key…
    Cohort  cohort;                    // …and its optional third term. NEVER an athlete id
    double  mu, sigmaLo, sigmaHi;      // asymmetric BY DESIGN, not as an option
    std::optional<double> monitorLo, monitorHi;   // absolute Watch bounds — migrated content only
    std::optional<double> plausibleLo, plausibleHi;   // outside these the reading is NOT BELIEVED
    int        n;  NormSource source;  QString unit, author, citation;  QDate setOn;
    std::optional<NormBasis> basedOn;  // what an override was made against (user rows only)

    double claimLo() const;            // mu - sigmaLo — the norm's OWN claim, policy-free
    double claimHi() const;            // mu + sigmaHi
};
```

`claimLo()/claimHi()` are **not** the Ideal band. The Ideal band is `mu ± idealMaxZ × sigma` and comes from `bandEdgesOf()`; it moves with the grade policy and the claim does not. See §5.

**`plausibleLo/Hi` answer a different question from the corridor.** The corridor asks whether the swing was good; these ask whether the reading is real. Outside them the grade is `NotMeasured` **plus a distinct `implausible` flag**, and plausibility outranks even the monitor band — a driver smash of 1.62 is a mis-tracked ball, and grading it in either direction would launder a capture fault into a confident diagnosis. Three states, never merged: a capture GAP (nothing arrived), a swing FINDING (something arrived and was poor), a capture FAULT (something arrived that this instrument cannot produce). On the ROW rather than the measure — the opposite of `Shape` — because the physical cap is context-dependent: smash is bounded by loft.

**Shape lives on the MEASURE, not here** (`characteristic.h`). One-sidedness is a property of the quantity and cannot differ between a driver row and an iron row, so norm rows carry only numbers and `validateNormsAgainst` joins the two — exactly as it already does for `unit`. On a floor, `sigmaHi` must equal `sigmaLo` and only `monitorLo` is legal; both are named load errors.

**All norms are population norms**, and the inviolable rule is that a norm is **never keyed by athlete id**. It used to read "one population for everyone", which the optional `Cohort` retires: a cohort is a *segmented* population — men 55–64 — not a person. A norm that differs per player is a different feature with different storage and a different UI. Seating from a chosen set of swings records `n` and the scope in provenance; the result still applies to everyone in that cohort.

**Cohort shifts the corridor and NEVER inverts valence** — the same rule context obeys — and band-edge cliffs are accepted: a grade can shift on a 55th birthday, and there is no interpolation, because interpolating implies a continuity the banded literature does not support.

`monitorLo/Hi` exist ONLY to reproduce migrated content exactly. The old table added a fixed number of degrees either side of green, which is not a fixed multiple of the green half-width and so cannot be reproduced by any global z policy. Anything authored in the corridor editor omits them and derives its Watch edge from the `GradePolicy`.

`basedOn` is the base of a three-way comparison. Without it, "the shipped corridor has been revised since you overrode it" is undecidable — *your row differs from the shipped row* is also just what an override IS, so a two-way comparison would report every edit forever.

### Context — the shot-type tree

```cpp
struct ContextNode { QString id, label, parentId; };   // one root: `any`
```

Shipped shape, and the thing to know about it:

```
any ── full_swing ── {driver, fairway_wood, iron, wedge, archetype_bowed, archetype_cupped}
    ├─ partial ── {pitch, chip}
    ├─ bunker
    └─ specialty
```

**The general case lives at `any`, not at `full_swing`.** A full swing is a shot *type*, with no more claim to being the general case than a pitch has. All 44 general norm rows sit at the root. Putting them at `full_swing` is a live bug shape, not a style preference: `partial`, `bunker` and `specialty` are *siblings* of `full_swing`, so a pitch or bunker shot could not see them, and every reading on such a shot came back `NotMeasured`. `diagnostics_health_test` pins `ungradedContext` at zero to stop it recurring.

**Shot type informs a diagnosis; it never gates one.** `ContextBinding::applicable` remains as the general exclusion mechanism — it is simply always true while shot type is its only key. `material` is how context is allowed to speak: an immaterial finding is still produced, still listed, still counted in coverage, and contributes **zero** to the ranking score.

---

## 4. The two joins, and how resolution works

### Metric → measure (`characteristic_pack.h`)

```cpp
std::vector<const Measure *> measuresForMetricAtPhase(pack, metricKey, phase);   // ordered
const Measure              *measureForMetricAtPhase (pack, metricKey, phase);    // the first
```

A measure matches a phase **through its reducer**, because that is where the phase lives:

| Reducer | Matches |
|---|---|
| `At` | its anchor phase |
| `Delta` / `Rate` | the window's END phase — the measure IS the change observed there |
| `Extremum` | **never**. "The lowest lag angle between P5 and P6" is not a reading at P5 or at P6 |
| any reducer with `windowMs` | **never**. A span after a phase is not a reading at that phase |

Preference order is **absolute before change**, because a corridor keyed on a phase means the absolute reading there. Both `m_leadWristAtTop` (at P4) and the Δ cell name P4 on one metric; the absolute one wins.

A measure matches a key on **any rung of its ladder** (`measureKeyLadder`: `preferKeys`, then `metricKey`), and within each of the two groups a measure whose **own** key is the one asked for comes first; pack order decides the rest. `lm.clubPath` is `m_lmClubPath`'s own key and a rung of `m_clubPathAtImpact`'s, so the launch monitor's board resolves its path corridor on `m_lmClubPath` and an edit to that row moves the board's band; `lm.launchAngle` and `lm.launchDirection` likewise. A rung still answers when the reading's own measure has no norm: `lm.clubheadSpeed` grades against `m_clubheadSpeedImpact`'s floor rows, because `m_lmClubheadSpeed` carries none.[^own-key-first]

[^own-key-first]: GitHub #190.

The list form exists because the winner can carry no norm. `m_leadWristAtTop` is preferred at P4 and has no norm at all — a caller taking only the winner draws nothing at the top of the swing while the Δ measure beside it has had a corridor since v1.

### Measure → norm (`norm_provider.h`)

`INormProvider::resolve(measureId, contextId, athlete = {})` walks **up** the chain from the requested context; the nearest row wins. It is **non-virtual on purpose** — every provider must resolve identically, or a grade would depend on which layer a norm happened to be stored in.

**The walk is CONTEXT-MAJOR.** At each node of the context chain the cohort keys are probed most-specific-first — sex+band, sex+adult, band, adult, sex, unqualified (`cohortProbeOrder`, `norm.h`) — and only when none is present does it move up the tree. Two intended consequences: an unqualified `driver` row beats a senior row at `any` for a driver shot, and a senior row at `any` answers wherever no club row exists. Inverting the loops would make every cohort row shadow every club row beneath it.

`athlete` is the GOLFER's cohort, derived from their date of birth **at the swing date** (`ageBandFor`) and never stored — an athlete ages across their own history. An axis with no answer is skipped, never guessed: unknown date of birth, unknown sex or a declined answer means only rows unqualified on that axis match, and the reading **still grades** against the universal corridor. An athlete we know nothing about yields exactly one probe, so resolution costs what it did before cohorts existed.

Two fallbacks that look similar and are not:

| Requested | Behaviour |
|---|---|
| **empty** | resolves at the default context (`full_swing`); `NormMeasureSource` marks each reading *inferred* and the engine demotes its confidence ×0.7 (`kInferredContextConfidence`, rule `rule.diag.inferredContext`, "club assumed") |
| **unknown to the tree** | resolves to **nothing**. Grading it against full-swing norms would be a wrong answer wearing a right answer's clothes |

**How a live shot reaches the empty row.** A swing's club is the *declared* one (`swingDocDeclaredClub`: `review.club`, else `capture.club.name`), never the picker's "DRIVER" stub, so it is empty for a swing that declared none. `LiveMeasureSource::contextInferred()` says so, and `detectForSwing()` then hands `NormMeasureSource` no context (it resolves at the default node) while the condition bindings and `LiveDetection::contextId` still read the default node; `LiveDetection::contextInferred` is set. `fireFindings()` (`src/Diagnostics/engine/fire_findings.h`, which `SessionDiagnosticsModel::detectShot` calls; refactor step 3.3) copies it onto every row of the shot (`ConditionRow::contextInferred`), and the ranking demotes those rows again (−0.05, `rankScores`, `work_ons.h`). A context named through `detectForSwing`'s override is a declared one and is never inferred. The rule is versioned in the ledger's content stamp (`kRowGradingVersion`, declared beside the rule in `fire_findings.h` and checked by `rule_version_golden_test`), so a ledger graded before it regrades once on activation.

### The pair, composed (`metric_corridor.h`)

```cpp
std::optional<MetricCorridor> corridorForMetricAtPhase(pack, norms, metricKey, phase,
                                                      contextId, policy = {});
```

Header-only, free-standing, and deliberately **not** inside the QML façade that consumes it — a rule that exists only inside a façade is a rule nothing can test. It walks the candidate measures in preference order and takes the first one a norm resolves for, reporting which one answered (`measureId`, `deltaFromAddress`) because the candidates are different quantities.

Returns `nullopt` — never a zeroed corridor — when no measure reads that metric at that phase, or none has a norm. "The corridor is 0 to 0" is a different claim.

---

## 5. Grading

`norm.h`, header-only, no Qt-GUI.

```cpp
enum class Grade { Ideal, Good, Watch, Action, NotMeasured };
struct GradePolicy { double idealMaxZ = 1.0, goodMaxZ = 2.0, watchMaxZ = 3.0; };
double normZ(value, norm);                 // signed, PER SIDE
Grade  grade(value, norm, policy = {});
```

`normZ` computes distance in tolerances **per side**, so an asymmetric norm grades asymmetrically. A zero tolerance on the relevant side yields infinity rather than dividing by zero: a norm admitting only its own centre is degenerate but well-defined, and the validator warns about it separately.

**Precedence in `grade()`**, when the norm carries explicit monitor bounds:

```
outside [monitorLo, monitorHi]   -> Action
otherwise                        -> the z-derived band, CAPPED AT WATCH
```

The cap is what makes migrated content exact: across all 39 migrated rows the monitor bounds are strictly tighter than 3σ, so a value inside them can never legitimately reach Action by z alone — and a value outside them was RED under the old classifier however few tolerances out it was. Both halves are needed to reproduce that.

### One projection, three consumers

```cpp
struct NormBandEdges { double idealLo, idealHi, watchLo, watchHi; };
NormBandEdges bandEdgesOf(norm, policy = {}, marginOverride = -1.0);
```

**Every edge here is policy-dependent, Ideal included.** `bandEdgesOf()` is the single definition of that precedence, consumed by `NormBandProvider` (the wrist grid), `NormModel::normAt` (the measures view) and `metric_corridor.h` (metric detail + dashboard). It replaced three copies, one of which — the corridor editor's — was drawing the wrong Watch edge for all 56 migrated rows.

**The norm's own claim is a different object and does not live here.** `Norm::claimLo()/claimHi()` is `mu ± sigma` — what the norm asserts about the population — and it never moves when a user changes the grade policy. Use it for **authoring and diff** (the corridor editor's handles, a comparison against a shipped or parent row, the validator); use `bandEdgesOf()` for anything that **renders or grades**. The two are not interchangeable, and every call site is a deliberate choice between them.

They were interchangeable until 2026-07-28, wrongly. `bandEdgesOf()` set `idealLo = mu − sigmaLo` while `grade()` applied `policy.idealMaxZ`; under `standard` (idealMaxZ = 1.0) those coincide, which is why nothing caught it for nine stages. Under `strict` (0.75) a value at 0.9σ was **drawn inside the green band and graded `Good`** — and `ragOf(Good)` is Amber, so one number produced a green band and an amber chip. Under `lenient` (1.5) a value at 1.3σ was drawn outside green and graded Ideal. This is exactly the disagreement `withinBand`'s own comment exists to eliminate, at whole-band scale rather than float-epsilon scale.

`norm_test` now sweeps both Ideal edges under every preset, and asserts `gradePolicyIsOrdered()` — `idealMaxZ > 0`, `goodMaxZ ≥ idealMaxZ`, `watchMaxZ ≥ goodMaxZ`. The engine silently depends on that ordering: `onTail` compares against the Ideal edge while `deviated` comes from `grade()`, so a preset with `goodMaxZ < idealMaxZ` would admit a reading that is a deviation on neither tail — a signal that can never fire, for a reason nothing reports. It held only because three hand-written presets happened to be ordered.

`marginOverride` is the SwingLab `bands.*` sweep. It exists because half the norms do not store their Watch edge as a margin at all, so sweeping "the margin" without it would silently do nothing on those.

### `ragOf()` and the 3-band collapse

The wrist grid renders the legacy `PpRag` (Green/Amber/Red). `ragOf(Grade)` is the single collapse, and **it is not 2:1:1**: for migrated rows Ideal is exactly the old green band, so Good AND Watch both sit inside the old amber. A surface showing a grade word and a RAG chip together will look self-contradictory — show one or the other. `reference_bands_test` sweeps every shipped norm asserting `ragOf(grade(v)) == classifyDelta(v)`, over both branches of the precedence rule.

### A signal fires on a DEVIATION

Watch or Action — **not** merely on leaving Ideal. Ideal is the middle of normal, so firing at `|z| > 1` would trip roughly a third of a normal population on every characteristic.

---

## 6. Detection and explanation

Both run in the app. `detect()` runs inside `fireFindings()` for every shot (through `detectForSwing`,
`engine/live_measure_source.cpp`); `explain()` runs in the session adapter's explanation
(`SessionDiagnosticsModel::refreshExplanation`). §6.0 says who calls them, on which thread, and who keeps the result.

### 6.0 Who runs it: the owners (refactor step 3)

Since refactor step 3 the diagnostics have owners at three levels, and views only read them
(`governance/artefacts/information_model.md` §10; CLS-SESSION-DIAG, CLS-ATHLETE-DIAG)[^dec-owners].
Nothing here is a view's: no QML creates a diagnostics model, and a panel that is closed changes nothing that is
judged (always fire)[^dec02].

| Level | Owner (one writer) | What it holds | Its files |
|---|---|---|---|
| Shot | the `Shot` (`DomainModel/shot.*`), through its `Session` | its findings and derived data, fired by `fireFindings()`, kept by the session[^dec135] | none of its own (FILE-SHOT-13 retired) |
| Session | `SessionDiagnostics` (`DomainModel/session_diagnostics.*`), the `Session`'s child | each shot's findings and derived data, stamped; the usuals; the diagnosis (ledgers, stage, patterns, reasons, work-ons, per-club mixes, carry); the typed summary | `diagnostics.json` (FILE-SESS-03), `work_ons.json` (FILE-SESS-05) |
| Athlete | `AthleteDiagnostics` (`DomainModel/athlete_diagnostics.*`), the `Athlete`'s child | the athlete work-ons, tendencies, themes and "what you do well", the consistency marks, distances, a session's club baseline before it is frozen | `tendencies.json`, `swing_themes*.json` (FILE-ATH-02/03), caches |
| Rules | `RuleData` (`DomainModel/rule_data.*`, store `Stores/rule_data_store.*`) | one immutable snapshot of the packs, norms, flight table, catalogue and grade policy | the user's pack and norm files (FILE-APP-01…05) |

**The shot: `fireFindings()` and `deriveShotData()`** (`Diagnostics/engine/fire_findings.h`). Pure functions: values in,
values out, no QObject, no file. `fireFindings()` assesses the shot's readings (its phase grid and a few document facts,
read by the firing reader `Stores/firing_reader_store.*`) against the pack and norms at the grade policy, for the
athlete's norm cohort on the shot's date[^dec171], with the club baseline: one row per condition, the sequence checks,
the placed peaks, and since 3.8-ii the wrist checkpoint profile (`WristDiagnosticsModel` is gone)[^dec123]. Layer 3,
`deriveShotData()`, is what the shot came to: its outcome and three factors (strike, direction, flight) against the
session's usuals. `FiringInputs` names every input; `firingStamp()` fingerprints them (R-STAMP-1), and
`rule_version_golden_test` / `reduction_rule_golden_test` fail when a rule's output moves without its version (C-05).
The analysis pipeline never fires findings.

**The session: `SessionDiagnostics`.** One per loaded `Session`, made with it, so every loaded session is judged.
- *The snapshot.* It holds one `FiringSnapshot` (`shared_ptr<const>`): the `RuleData` snapshot's pack and norms, the grade
  policy (never a live read: D-12), the flight table, the frozen club baseline, the usuals, the cohort facts and the
  golfer's input. A change builds a new one; a job keeps the one it was asked with.
- *Asks and the lane.* Each shot's fire is an ask: it takes the next number of the one process-wide counter, and its job
  (values only) runs on THIS SESSION INSTANCE'S serial lane of the one pool, at low priority, the live session's lane in
  front (§4.4 of step 3's brief; D15, D24). The result is posted to the composition root's `WorkReceiver`, which resolves
  the session by id and checks the instance, the shot and the ask before handing it over: a superseded, trashed or
  released result is dropped and still settles its batch.
- *What re-fires.* A new shot, a re-analysis (EVT-02), a club correction (EVT-04, both clubs' shots), an exclusion, a
  late launch-monitor reading (EVT-22), an edit of the rule data (EVT-12/13). A usual that changes recomputes that
  club's shots' DERIVED DATA only, never their findings[^dec191]. A failed read comes back unread: nothing kept, the
  shot marked unread (a persisted key), re-asked at the next load.
- *The diagnosis.* When a batch settles it posts `reduceSession()` (`Diagnostics/engine/session_reduction.h`, pure) to the
  same lane; the latest reduction is kept. It reads each shot as the golfer annotated it on the launch-monitor board
  (`Diagnostics/derived/board_annotations.h`; Stage 0.5's follow-up). The session's status is read from the `Session`, its one writer.
- *Its files.* It writes what it kept, debounced after a batch, at once on a release, and on quit, through
  `SessionRepository`'s id-keyed diagnostics methods: `diagnostics.json` (each shot's findings, its derived data under
  `derived`, the unread marks) and the summary `work_ons.json`, which carries the session's gate counts (`gates`,
  `Diagnostics/derived/session_gates.h`: the swings counted, those with nothing recorded, per `gatesVersion`, per gate the swings it
  fired on), made off the same grid read as the day readings; the model browser's Gates view sums them over every
  athlete's `SessionIndex` (`Gui/diagnosticmodel/gate_counts.h`) and reads only a session not summarised yet swing by
  swing. A live session's summary waits for its End. A newer
  build's file is read and never written. A failed write is said (`writeFailed`; the composition root posts the notice)[^dec126]. At load it re-asks exactly the shots that have no result, are unread, or whose stamp is stale.
- *The club baseline* is a recorded input[^dec46]: read if current, else frozen at first need from the athlete level's
  computation over the predecessors' summaries (`AthleteDiagnostics::clubBaselineFor`, D7)[^dec172], through
  `Session::freezeClubBaseline`; not frozen while it is not computable, and the live panel says so[^j49].
- *Headless.* `DiagnosticsDoors::synchronous` runs the same jobs on the calling thread (R-HL-2): `regrade_ledger`, the
  tools and the tests.

**The athlete: `AthleteDiagnostics`.** It reads the session summaries only (R-DER-3)[^dec92], never a ledger or a
document, and works out Home's figures on its own lane of the one pool[^j53]: the athlete work-ons, the tendencies, the
themes and "what you do well" (praised only when in range AND Repeatable or Grooved: the praise rule, 04 step 3 item 5),
the consistency marks, the distances. Its one exception is the scoped views (a club scope, a shot filter)[^dec195], which
take each session's judged rows from its own `SessionDiagnostics`. A session whose summary is missing or of an older rule
is counted and shown with a warning, never silently left out[^j51].

**The catch-up: `AthleteCatchUp`** (`src/App/athlete_catch_up.*`, an application service with no data of its own). For an
athlete, every ended session whose summary is stale, oldest first, one at a time: it loads the `Session` (which runs its
diagnostics), waits for them to settle, asks them to write, and releases the session (Q-18), then asks
`AthleteDiagnostics` to persist. It pauses while a session is live and cancels on an athlete switch (D-15); its write
blocks are reported, not only logged. `WorkOnsController`'s scan finds the stale sessions and asks for them.

**The one pool** (`src/Core/work_pool.h`): a fixed set of workers running SERIAL LANES, one per owner instance, with one
process-wide ask counter and one receiver on the GUI thread. A job holds values only and never writes a file or touches
a model; the owner applies its result on its own thread. (Known interim: the firing reader may write the phase-grid and
summary sidecar caches it rebuilds, under the swing's lock, never during an archive: CARRY C5, R1-m6.)

**The thin adapters** (`src/Gui/diagnostics/`). What QML reads, words and view state only (D3; typed in step 5)[^dec165]:
- `SessionDiagnosticsModel` in adapter mode, one per `Session` and surface (`panel`, `cast`, `cards`, `report`, `tool`), the
  `Session`'s child, reached by `diagnosticsAdapters.of(app.sessionAt(folder), surface)` (`SessionDiagnosticsAdapters`). It
  mirrors the owner's diagnosis and adds the selection, cadence, the open detail, the explanation (`explain()`) and the same
  golfer's other sessions (`historyOf`). `QML_UNCREATABLE`. It holds a use of its `Session` only while a view is bound.
- `WorkOnsController` for Home: it reads `AthleteDiagnostics` (through the `AthleteLevel` interface) and words it.
- `SessionReportModel` reads the session's `report` adapter and `AthleteDiagnostics::tendenciesAsOf`.
- No golfer chosen: the session is the Unassigned owner's full `Session` (information_model.md §5.11; step 5.1), judged
  like any other with no norm cohort, and it freezes no club baseline (`AthleteDiagnostics::clubBaselineFor`); Home
  shows it no athlete-level figures. Only a folder no athlete of the library holds has no diagnostics, and every place
  its findings would appear says so (show with a warning).

**The old engine is gone.** The engine half of `SessionDiagnosticsModel` (its per-shot read, its own reduction, stores,
two of its pools and its baseline authorities) and `regrade_ledger --engine old` were deleted in refactor 5.4b[^r54b].
`SessionDiagnosticsModel` is only the Session's thin adapter now. A folder no `Session` holds, including one whose file
names another athlete id[^ja3], gets the empty adapter and is judged by nothing; the panel and the carousel say so. The
golfer's edits through an adapter go to the Session's and the Shot's commands; when no Session is reached, nothing is
written and the panel's notice says so. `regrade_ledger`, `diag_rank_diff`, `diag_uncertainty_report` and
`post_shot_bench` run the session's owner; `measure_dump` reads each shot as the owner's firing reader does and builds its
readings as `fireFindings` does, with no cohort; the session report mock dumps the owner's thin adapter[^r53-tools].

**The tools.** `regrade_ledger` re-judges each session through its own `SessionDiagnostics` (`--engine new`, the only
engine; `--engine old` is refused with a sentence)[^r53-tools][^r54b]. The new engine **must know each
session's athlete**, because it freezes each session's club baseline from its own athlete's earlier sessions: without a record, every
session of one folder (the corpus's `swings`) would count as one golfer, and one golfer's usuals would be frozen into
another's sessions for good. Since refactor 4-c[^j61] the athlete is read from the library, the athlete folder's
`athlete.json` through `AthleteRepository`. A folder with no `athlete.json` is read from `athletes.json` beside the session
(`pinpoint.athletes/1`, the corpus's `corpus/swings/athletes.json`: the entry whose name is in the session folder's name).
`--athletes-ini` (a copy of the app's settings) is only the last fallback, for a library not yet migrated. A session no
source names is refused, untouched (3.10 review B1; `regrade_ledger_new_needs_ini_test`). In a folder of several golfers'
sessions each golfer's Athlete lists no earlier session, so a session read from `athletes.json` freezes no club baseline:
it is graded with none (no usual numbers, as under the old engine), an existing `club_baseline.json` is read as it is, and
the tool says "club baseline not frozen: corpus run". It `--dump`s
the parallel run's form P (`Gui/diagnostics/diag_parallel_run.h`, ids and numbers only); `tools/parity/parity_baseline.py`
runs `--engine new` for the session half over session copies that carry their athlete folders' `athlete.json` (a copy of
the settings as `--athletes-ini` only for a library not yet migrated: refactor 4-c), and `athlete_dump` for the athlete half.
`diag_rank_diff` and `diag_uncertainty_report` read a session's owner and its `tool` adapter (`tools/diagnostics/owner_harness.h`).

### `detect()` — `characteristic_engine.h`

```cpp
DetectionResult detect(const CharacteristicPack &pack, const IMeasureSource &source,
                       const ContextTree *contexts = nullptr, const QString &contextId = {});
```

Per condition: skip `Latent` ones (no signals by definition), skip `Retired` and `Superseded` ones,
resolve the context binding, evaluate every signal, emit a `Finding`.

**`ConditionState` gates detection on those two values and no others.** They are the states that
mean *withdrawn*, and withdrawn content must not diagnose — until this was added the engine read all
six identically, so retiring a characteristic changed a badge and nothing else. The other four are
editorial confidence rather than withdrawal, and **62 of the 112 shipped conditions are `Draft`** —
reading "not finished" as "not in use" would dark more than half the library while every census
still said it was there. A `Superseded` condition must name its successor (the validator refuses
otherwise), so the replacement is already in the pack and evaluates in its place.

Three rules that must not be softened downstream:

1. **An inapplicable condition is OMITTED** — not `NotFired` (which claims it was assessed and found absent), not `Unavailable` (which claims the app tried and could not). Both would be wrong in a way a coach could read. Nothing may backfill the gap.
2. **No corridor ⇒ `Unavailable`, never a pass.** This is the single most important branch in the engine: most of the pack has no corridor yet, and "we could not assess this" must never render as "this is fine".
3. **`material` is unweighted, never softened.** An immaterial finding is listed and counted in coverage and adds **zero** to the ranking score — zero rather than a fraction, because a fraction would be a number nobody could defend.

The measure source seam:

```cpp
class IMeasureSource {                // what detect() reads
    virtual std::optional<MeasureReading> read(const QString &measureId) const = 0;
};
class IMeasureValueSource {           // raw values, no grading
    virtual std::optional<Value> value(const QString &measureId) const = 0;
};
class NormMeasureSource final : public IMeasureSource;   // joins values + norms -> graded readings
```

### Measurement uncertainty: a probability on every verdict (`session_diagnostics_design.md` §A8)

Since October 2026, a finding carries `pFire`, `grossRisk` and `quantified` beside its unchanged
verdict.

- **Where σ comes from.** The phase grid (sidecar schema 5) carries each value's measurement σ and
  where it came from (`SigmaSource`: reading, series, noise, none). `reduceOverGridReading()`
  propagates it through every reducer. `LiveMeasureSource` hands it to `MeasureReading::measSigma`
  behind `diagnostics.uncertainty.enabled`.
- **How a signal uses it.** Each signal computes P(fire) = Φ(±(v − e)/σ) against the tail's
  deviation edge, `deviationEdges()`. That helper mirrors `grade()` term for term; change both or
  neither.
- **How signals combine.** ANY combines as 1 − Π(1 − p) and ALL as Π p.
- **No σ.** P(fire) restates the verdict as 1 or 0, and `quantified` is false. A σ is never invented.
- **The verdict is never changed by any of this.** The report tool's gate G1 checks it on every row.

The measurement σ (how wrong this reading might be) is not the norm's σ (how golfers vary). Only
the first makes a verdict a probability.

In the session layer:

- **Soft counts.** `LedgerOptions::softTier` feeds expected counts into the Wilson bound.
  `patternDraws()` gives each condition's P(Pattern) from 200 deterministic draws.
- **Exact ends.** `wilsonLowerBound` returns exactly 0 when nothing fired, and `wilsonUpperBound`
  (`work_ons.h`) exactly 1 when every shot fired. The formula only cancels to them, and the residue
  sorts: the bound is a sort key, and the order must not depend on the compiler.[^wilson-ends]
- **Hard captions.** Recurrence captions and the integer counts stay hard.

### `relation_resolver.h` — the explanation pass

Fired conditions → ranked root causes + test recommendations. Two rules do the work:

1. **A characteristic with an in-pack cause is never a root.** The graph is not two clean layers — early extension causes loss of posture, casting causes scooping. Presenting a leaf as a root hands the coach a symptom and calls it the diagnosis.
2. **An `Asserted` cause is offered, never concluded.**

**The ranking score** (`ExplainOptions::posteriorRank`, §A8.6) is

    P(c | evidence) × Σ q_e · s(c → e)

over the findings c would explain. P(c | evidence) has two sources:

- **A cause nobody measured** gets its noisy-OR posterior: prominence as the prior, and each
  assessed child's likelihood ratio under soft evidence q_e. A child assessed and absent counts
  against the cause.
- **A cause that was itself assessed** keeps its own P(present), un-updated. An inference through
  authored words never overrules a measurement of the thing itself.

Every root also carries a stability word, from `stampStability()` over the session's
measurement-uncertainty draws: firm, likely or fragile. Neither the score nor the posterior is
ever shown as a percentage. `legacyScore` keeps the pre-§A8 score for rank-shift reports.

`TestRecommendation` ("screen this — it would explain four of your findings") is the highest-value output of the model and costs no capture hardware: the dominant causes in the pack are screen-backed, so a handful of physical tests explain most of what was detected. The `screenRef` it carries now resolves to a protocol and a pass criterion in `screens.json`, so the recommendation names a test somebody can actually run.

### The two non-causal edge types

Both are consumed by `explain()`, and the asymmetry between them is deliberate.

- **`Excludes` changes the output.** Two findings that cannot both describe one swing are resolved
  BEFORE ranking: the more confident stands, ties break on the id, and the dropped one is recorded
  in `Explanation::suppressed` with a reason rather than vanishing. Leaving both in would put a
  contradiction in front of a coach and let one cause be credited twice for two versions of one
  event. A suppressed finding is never also listed as `unexplained`.
- **`Corroborates` is reported, and breaks ties.** It never scales a score. A multiplier would mean
  inventing a number nobody could defend when asked why one cause outranked another — the same
  reason `material` contributes zero rather than a fraction. `characteristic_engine_test` asserts
  the scores are identical with and without the edge.

Both are symmetric in meaning, so `relatedBy()` reads them from either end and an author may write
the edge whichever way round they think of it.

---

## 7. Providers, layering and persistence

Both registries follow the same shape: an abstract provider, a resource-backed core, a file-backed user layer, and a merger.

| | Pack | Norms |
|---|---|---|
| Interface | `ICharacteristicPackProvider` | `INormProvider` |
| Shipped | `resource_pack_provider.cpp` (`:/diagnostics/core.json`) | `resource_norm_provider.cpp` |
| User's own | `file_pack_provider.cpp` | `file_norm_provider.cpp` |
| Layered | `merged_pack_provider.cpp` | `merged_norm_provider.cpp` |
| Assembled by | `RuleData`'s one snapshot (refactor 3.2); `makeCharacteristicPackProvider()` is a view of it | `RuleData`'s one snapshot; `sharedNormProvider()` a view of it |

`PINPOINT_CORE_PACK` / `PINPOINT_CORE_NORMS` / `PINPOINT_CORE_CONTEXTS` override the paths, because the Qt resource exists only inside the app binary and without that seam no standalone test or offline tool could reach shipped content.

**Rules that have already caused bugs:**

- **Context specificity BEATS layer precedence.** A user adjusting the general norm must not silently override every club-specific shipped norm beneath it. `norm_pack_test` pins this.
- **A merging caller keys off `parsed`, not `loaded`.** An overlay routinely fails *standalone* referential validation — its edges point at core conditions it does not itself contain. Keying off `loaded` made the characteristic editor start from an EMPTY user pack, and `save()` then erased every override the user had ever made.
- **A LocalUser pack REPLACES the whole incoming edge set of any condition it names as an effect.** So code that writes one edge must write the condition's whole incoming set — which is why `linkCause()` goes through `beginEdit`/`addCause`/`save` rather than appending.
- **`save()` reloads the provider**, destroying the pack any `const Condition *` points into. Copy labels out first.
- **`sharedNormProvider()` is cached process-wide, as an immutable snapshot.** Since refactor step 3.2 its source is `RuleData`'s snapshot, and every write of a user norm set is `RuleData::saveUserNorms`, which publishes the new snapshot and resets the slot itself (EVT-12). The pack and corridor editor (`ModelBrowser`) reads `RuleData`'s snapshot and writes through its commands, with no reset of its own (step 3.9); a code path that writes a norm file some other way (a test) calls `resetSharedNormProvider()` after it, or every later take returns the set from before the write until the next launch. A reader keeps the `shared_ptr<const INormProvider>` it took, so a reset never frees or changes a set somebody is reading: one pass grades against one norm set. Taking, first building and resetting are all safe from any thread (a lock guards the slot and the build runs under it). Each reset moves `sharedNormProviderGeneration()`; a holder that derives something from the norms compares the generation it built at (`sharedNormProvider(&gen)`) to know it is stale.
  - **Who sees a saved corridor, and when.** Anything that takes the provider per pass sees it on its next pass: work-ons jobs, the report's pages, the relations and wrist rows, variability lookups. The process-wide derived envs follow the generation: `postureSharedEnv()` (a snapshot; the posture presenter's worker empties its caches when it changes), the wrist checkpoint profile (since step 3.8-ii part of each shot's findings, `Wrist/wrist_profile_assess.cpp` over the firing inputs' norms: a saved corridor reaches it through the re-fire EVT-12 starts, as it reaches the conditions), and the consistency env's norm half (`consistencyEnvFromPacks()`). The consistency, relations and traces presenters re-take the env (and their scale provider; the consistency and relations presenters also drop their caches) at their **next job** when the generation has moved. A save schedules no job, so a view already open keeps its old grades until its next job runs (a new snapshot, a pick, a setting change). The wrist cells are the findings' (3.8-ii), so the wrist grid follows the re-fire. Since refactor 5.5a the Charts pack holders follow a pack save too: the session's series (`SessionSeries`) republishes on EVT-13, and `postureSharedEnv()`, the consistency env's pack half and the Wrist presenter's conditions are made again over `RuleData::current()` (`consistencyEnvGeneration()` moves). Since refactor step 3 the findings follow an edit at once: `RuleData` publishes a new snapshot (EVT-12/13) and every loaded session's `SessionDiagnostics` re-fires its shots against it (each job keeps the snapshot it was asked with, so nothing races a worker); a session not loaded is re-judged when it is next loaded, because its findings stamp no longer matches. The thin adapters re-read their owner's diagnosis; they hold no norm set of their own. (Only the old engine, for J-a3's folders and `regrade_ledger --engine old`, still re-takes the set at activation: `followNormSave()`.) `LmSessionModel` (the LM board) grades on the GUI thread and re-takes at the top of its grade pass (`rebuild()`), so the board shows the old grades until its next rebuild (a shot, a pick, a setting change). Pack edits (`makeCharacteristicPackProvider()` copies) are a separate story, audit B-02.
  - **The flat registries are snapshots too** (refactor 3.2; B-03, B-13). `sharedScreenSet()`, `sharedDrillSet()`, `sharedGateWordSet()` and `sharedReferenceSet()` read `RuleData::current()` (`DomainModel/rule_data.h`'s interim accessors, for the readers not yet handed the snapshot; steps 5 and 7 move them), and a reference is into a snapshot `RuleData` keeps for the process's life, so no reset deletes what a reader holds. The two Charts statics (`postureSharedEnv`, `ConsistencyPackFacts`) keep the pack they first took until relaunch (3.2's J9; step 5).

---

## 8. Live, dormant, planned — the full breakdown

The section to read before assuming anything here runs. "Dormant" means written and tested but with no caller in the app; it is not the same as broken, and not the same as planned.

### 8.1 Code — live

| What | Consumed by |
|---|---|
| `characteristic.{h,cpp}`, `characteristic_pack.{h,cpp}` | everything; the pack loads at app start |
| `norm.h`, `norm_pack.{h,cpp}` | grading, projection, every façade |
| `context_tree.{h,cpp}` | norm resolution, the context list in the editors |
| `metric_corridor.h` | `metric_catalog.cpp` → MetricDetail, PpBandRail, dashboard Motion/Verdict/Setup zones |
| `NormBandProvider` (`Wrist/reference_bands.cpp`) | the wrist grid (the Charts wrist presenter) and offline (`analysis_pipeline`) |
| **`fireFindings()`, `deriveShotData()`** (`engine/fire_findings`) | every shot of every loaded session, through `SessionDiagnostics` (§6.0) |
| **`detect()`** (`engine/characteristic_engine`), **`NormMeasureSource`**, `LiveMeasureSource` (`IMeasureValueSource`'s real implementation) | `fireFindings()` through `detectForSwing` (`engine/live_measure_source.cpp`) |
| **`reduceSession()`** (`engine/session_reduction`) | `SessionDiagnostics`' diagnosis, on its lane |
| **`explain()`** (`engine/relation_resolver`) | the session adapter's explanation (`SessionDiagnosticsModel::refreshExplanation`) |
| `SessionDiagnostics`, `AthleteDiagnostics`, `AthleteCatchUp`, `RuleData`, `WorkPool` | §6.0 |
| `measure_sample.{h,cpp}` | the corridor editor's histogram, the corpus-share health scan |
| `dag_layout.{h,cpp}` | `ModelGraph.qml` via `ModelBrowser::graph()` (which lays a condition out through `dag()`) |
| `diagnostics_health.{h,cpp}` | `ModelBrowser` (the validation strip) |
| `measure_facets.{h,cpp}`, `anatomy_vocabulary.{h,cpp}` | Composed-measure vocabulary, the measure picker |
| all six providers + the two mergers | pack and norm assembly |
| `ModelBrowser` in `Gui/diagnosticmodel/` | the Diagnostics settings panel |

### 8.2 Code — DORMANT (written, tested, no caller)

| What | State | What it needs |
|---|---|---|
| ~~**`detect()`**, **`NormMeasureSource`**, **`IMeasureValueSource`**~~ | **no longer dormant** — `fireFindings()` calls `detect()` over `LiveMeasureSource` + `NormMeasureSource` for every shot (§6.0, §8.1) | — |
| ~~**`relation_resolver`**~~ | **no longer dormant** — `explain()` runs in the session adapter (§8.1) | — |
| **`ContextBinding::applicable` / `material`** | resolved by `resolveContextBinding()`, honoured by `detect()`, editable in the UI | content: **no shipped condition carries a binding row** (0 of 146), so both are inert |
| **`Condition::kind`** | authored on all 146, validated, marshalled, faceted, sorted, editable | a consumer with an opinion. Nothing in `detect()` or `explain()` branches on it; `outcomeReachOf()` is the one rule that reads it |
| ~~**`Condition::prominence` / `prominenceWeight()`**~~ | **no longer dormant** — it weights `RankedCause::score` via `rankWeight()`, measured by `rank_shift_report` (§3) | a caller of `explain()` |
| **`GradePolicy` reaching grading** | reaches three façades and the corpus scan | the engine (it takes a policy in its constructor already) |
| **`SignalTest::Threshold`** | implemented in `evaluate()` | content — no shipped signal uses it |
| **`SignalTest::Ratio`** | implemented in `evaluate()` | content — no shipped signal uses it |
| **`Norm::basedOn`** | written at save, read by `overrideCoreChanged` | time. No existing user row has one, so the check is correctly silent until an override is saved |
| ~~**`EdgeType::Corroborates` / `Excludes`**~~ | **no longer dormant** — 9 and 5 shipped, and `relation_resolver` consumes both | a caller of `explain()` |

**The engine is wired** (the session diagnostics panel, the carousel's pips, the cast, the report and Home read it; §6.0). What stays dormant above is content or a consumer with no opinion yet, not the engine.

### 8.3 Content — the shipped numbers

> **Counted, not remembered — recount before you trust this.** Every figure below is a `git grep`
> away from being wrong, and this table has been wrong before: it sat at "112 conditions, 178 edges"
> while the pack held 146 and 306, which is a census understating the causal graph by more than half.
> Nothing warns, because a stale number in prose is not a compile error. Regenerate with:
>
> ```sh
> python3 -c "
> import json,collections
> d=json.load(open('src/Resources/diagnostics/core.json'))
> n=json.load(open('src/Resources/diagnostics/norms.json'))['norms']
> for k in ('measures','signals','conditions','edges'): print(k, len(d[k]))
> for f in ('kind','prominence','group','observability','confirmedBy'):
>     print(f, dict(collections.Counter(c[f] for c in d['conditions'])))
> print('edges', dict(collections.Counter(e.get('type','causes') for e in d['edges'])))
> print('norms', len(n))"
> ```

**Pack** (`core.json`) — 110 measures, 119 signals, 146 conditions, 306 edges:

| Measures | | Signals | | Conditions | |
|---|---|---|---|---|---|
| Live | **83** | `outsideCorridor` | 118 | Observable | 122 |
| Planned | 16 | `order` | 1 | Latent | 24 |
| ExternalDevice | 9 | with a direction | 118 | `Measured` | 115 |
| NoProducer | 2 | | | `Asserted` | 17 |
| Provided | 101 | | | `Screened` | 14 |
| Composed | 9 | | | with an axis | 99 |
| with `highMeans` | 75 | | | with aliases | 119 |
| with a norm | 99 | | | with drills | 23 |
| | | | | with binding rows | **0** |

Reducers: 55 `at`, 43 `delta`, 10 `extremum`, 2 `rate`.
Edges: 283 `Causes`, 18 `Corroborates`, 5 `Excludes` — 103 strong, 184 moderate, 19 weak.
(`veryWeak` and `veryStrong` are authored nowhere yet; both rungs exist for authors, not for the
shipped set.)

**Kinds** — the axis added 2026-08-03:

| kind | | | kind | |
|---|---|---|---|---|
| `fault` | **69** | | `capacity` | 14 |
| `setup` | 20 | | `delivery` | 13 |
| `outcome` | 20 | | `intent` | 9 |
| | | | `equipment` | 1 |

**Prominence** — 58 `occasional`, 53 `common`, 25 `uncommon`, 7 `ubiquitous`, 3 `rare`. All editorial
judgement; no prevalence study exists (see §3). The seven at `ubiquitous` are `over_the_top`,
`early_extension`, `casting`, `out_to_in_path`, `open_face_to_path`, `slice` and
`trying_to_lift_the_ball`. `coming_out_of_it` (was `loss_of_posture`) left that rung when it became a
conjunction: a conjunction cannot be commoner than its rarer part.

**The two axes answer different questions.** `group` says what moves; `kind` says what sort of thing it
is. They agree on three groups (`setup` is all Setup, `ballFlight` all Outcome,
and every `delivery` row is Delivery) but not on the rest: `planePath` splits 13 Fault / 7 Delivery (+`arms_swing_around`, +`arms_lift`)[^ps-d2], so the Delivery
kind spans two groups, and `golfer` holds three kinds (17 Capacity, 10 Intent, 1 Equipment), which include
every physical screen and every intent node in the library.

Provenance, after the pass of 2026-07-27 — **no `proposed` remains anywhere, and every condition
records the date its search was made**:

| tier | conditions | edges | |
|---|---|---|---|
| `supported` | 5 | 6 | the defining variable (conditions) or the named pair (edges) is directly measured |
| `indirect` | 23 | 26 | a source measures the mechanism underneath; the named claim is our reading |
| `practice` | 118 | 274 | searched, coaching consensus found, no peer-reviewed test of the named claim |
| `proposed` | **0** | **0** | nobody has looked — the tier a completed search pass empties |

`core_pack_test` no longer asserts `proposed > 0`; that was a mid-search proxy. It asserts
`practice > cited` plus an all-searched check instead. **Do not restore the old assertion** — adding
a proposed row back to satisfy it is the laundering it was written to prevent.
Groups (recounted 8 Nov 2026, with the two side-bend conditions: 188 conditions, later than the figures above): setup 25,
posture 16, lateral 20, rotation 29, arms 8, wrists 14, planePath 18, delivery 10, ballFlight 20, golfer 28.
Since then (13 Nov 2026, plane stack): D1 retired two planePath rows and added two; D2 added `arms_swing_around` and
`arms_lift` to planePath, which now holds 20 (192 conditions).

**Norms** (`norms.json`) — 156 rows: 99 at `any`, 8 + 8 at the two archetypes, and 13 / 2 / 13 / 13 at driver / fairway wood / iron / wedge. **Every row is `source: heuristic` with `n = 0`**; 56 carry explicit monitor bounds (the migrated wrist grid + tempo), 50 carry a citation. Those citations are **notes, not identifiers** — `citation` is documented as "DOI/PMID, or the note explaining a provisional figure", and many of them name the paper the figure should be re-seated FROM. Nothing moved to `source: literature` in the 2026-07-27 pass, deliberately: that tier asserts the mu and sigma came off a results table, and nobody read one. See `docs/implementation/provenance_log.md` §6.

**Contexts** (`contexts.json`) — 13 nodes, one root. 7 have norms at or beneath them; the other 5 (`partial`, `pitch`, `chip`, `bunker`, `specialty`) carry none of their own and inherit from `any`, which the health list reports as harmless.

**Screens** (`screens.json`) — 13, all referenced by at least one condition. Nine carry a numeric pass floor with its unit; four are qualitative on purpose, and `reference_sets_test` asserts that so the absence of a number reads as intent rather than as unfinished content.

**Drills** (`drills.json`) — 45, attached to 138 conditions. Every condition the work-on rule can list carries at least one that resolves, because its work-on page in the session report prints them; `core_pack_test`'s report gate (`reportContentGaps`, §9) holds the shipped pack to that.

### 8.4 Content — what can actually fire

> **A gap here is the design working, not a backlog.** The model is authored **ahead of the
> producers on purpose**: a complete library is a reference a coach can review and validate, and it
> is what then *sets* the priority order for building producers. A model that only described what
> the pipeline could already measure would be a mirror of the pipeline and useless for deciding what
> the pipeline should do next. **So do not add a check, a warning or a test that reports a modelled
> item as deficient for lacking a producer** — that restates the purpose of the exercise, and it is
> `observableNoSignal`'s mistake in a new costume (trap 3c). The roadmap surface already enumerates
> producer gaps; nothing else should. For the same reason, an analysis over the pack should sweep
> **every** condition, not the subset that can currently fire.

**75 signals can fire, covering 71 conditions.** Everything else sits on a measure that is `planned`,
`externalDevice` or `noProducer` — **zero** are live with no norm, which `core_pack_test` asserts.
So the dark ones are waiting on *producers*, not on corridors.

**Prominence and detectability run inversely at the top**, which is the sharpest priority statement
the library can now make and was invisible before there was a base rate to sort on:

| rung | fires | dark | |
|---|---:|---:|---|
| Ubiquitous | 1 | 3 | **25%** |
| Common | 19 | 3 | 86% |
| Occasional | 18 | 6 | 75% |
| Uncommon | 14 | 2 | 87% |
| Rare | 3 | 0 | 100% |

Every rare fault in the library is detectable and three of the four commonest are not. It is not a
coincidence: the frequent faults are whole-body events needing depth — pelvis thrust, spine-angle
change, the transition plane — while the rare ones tend to be single-joint readings a face-on camera
already resolves. Read as a producer roadmap, not as a defect list.

**That roadmap is written out, ranked, in `metric_catalogue_developer_guide.md` Appendix A0.** Join
each unbuilt metric to the characteristics authored against it and take their top rung, and the
question "which producer next" answers itself — `swingPlane` leads with six characteristics
including over-the-top, because one metric read at three phases carries the whole plane story.

The last jump was 25 → 35 and came from one analysis stage over COCO body keypoints 11–16, which
turned `pelvisSway`, `pelvisLift` and two new frontal-plane keys live in a single change. The
governing rule there is worth carrying: **a face-on camera resolves the frontal plane and nothing in
depth**, so `pelvisThrust` (depth), the knee flexion angles (sagittal) and `pelvisRotation` stay
planned on purpose rather than by omission.

That is double the eight signals of the previous content package, and the doubling came entirely
from a gap nobody had noticed: eleven producer keys were live and carried **no measure at all** in
the pack — head sway, head lift, head tilt, lead heel lift, the two foot flares, toe line, impact
shaft lean, hand speed, clubhead speed and backswing tempo. Putting measures and corridors on them
cost no pipeline work whatsoever.

The single `order` signal (`sig_sequenceOrder`) still cannot fire: both of its measures
(`m_pelvisRotPeak`, `m_thoraxRotPeak`) are `planned`.

**Every condition that can fire can also be EXPLAINED**, and that was not true when they were first
counted. Ten of them had no cause at all and four had neither cause nor effect — so the least
explicable part of the library was precisely the part a golfer would meet first. The gap formed at
the intersection of two healthy-looking states: the causal work went in per GROUP, while the firing
set is decided per PRODUCER, and the eleven live-but-unclaimed producer keys arrived after their
group had been wired. Nothing reported it, because `noCause` is a warning that fires 25 more times
on content nobody can capture yet. `core_pack_test` now gates it directly — scoped to what can fire,
because a `noCause` on a producer-less condition is a backlog and a condition that fires TODAY with
nothing behind it is a defect in what ships.

> **~~A constraint that caps all of this~~ — REMOVED, 2026-08-02.** Most pose metrics used to be
> session-gated to Wrist Motion (`wristSessionOk`, sessionType 1 or −1), so the head, foot and
> shaft-lean signals read Unavailable in a Swing session. That gate is **gone**, on both sides: the
> body-metric stages are now listed once in `appendBodyMetricStages()` and run in every profile, and
> no provider reads `sessionType`. **Analysis is agnostic of session type** — a session type is what
> the operator meant to capture, and only the sensors are evidence about what was captured. Ledger
> `X1` in the content-extension plan is closed by this.

**0 of the conditions that can fire lack a cause** — six did, briefly, the moment their producers
landed, and `core_pack_test` caught it. Six `causes` edges were authored to close that; see trap 3b.

The 9 `Composed` measures (facet-built rather than metric-backed) are no longer all planned: five of
them — `m_shoulderPlane`, `m_thoraxDrift`, `m_trailElbowRise`, `m_leadArmToTorso`, `m_leadHandWidth`
— went live with `upper_body_metrics.cpp`, which was built to the geometry their `series` facets
already named. The Composed path is production-live for the first time.

Condition fields in use: `consequence` 146/146, `provenance` 146/146, `kind` 146/146,
`prominence` 146/146, `aliases` 119, `axis` 99, `drills` 23, `screenRef` 14, `injuryNote` 11,
`bindings` **0**.

**References** (`references.json`) — 21, joined to the pack by `Provenance::citation` against
`doi` **or** `pmid` (exact string match, either field). One record carries a PMID and no DOI because
its journal issues none, and opens at PubMed rather than doi.org. Five are cited by nothing and are
kept on purpose: two contradict claims the pack makes, one governs the wording of every injury note,
one is background, and one is an arXiv preprint held for attribution only.

### 8.5 The honest summary

- **Everything that stores, resolves, projects, renders and edits the content is live**, and two real surfaces depend on it today: the wrist grid and the metric/dashboard corridors.
- **Everything that turns a shot into findings is dormant** — one adapter and one design decision away.
- **The numbers are all starting figures.** Nothing is seated on data; `n = 0` on every norm, and every prominence is editorial judgement. Treat a shipped corridor and a shipped rung alike as a hypothesis, and do not build a gate that pins one.
- **The content leads the code, deliberately.** More is modelled than can be captured, and that is the product: a complete library is what a coach can review and what then decides which producer gets built next. The gap is the roadmap, not a defect — see the note opening §8.4.

---

## 9. Validation and health

Four validators, plus one set of checks that spans them.

| Function | Sees | Runs where |
|---|---|---|
| `validatePack(pack)` | one characteristic pack | every load, both layers |
| `validateNormPack(norms)` | one norm set, standalone | every load; the corridor editor before a save |
| `validateContextTree(tree)` | one context tree | every load — duplicate ids, unknown parents, cycles |
| `validateNormsAgainst(norms, pack, tree)` | the ASSEMBLED library | `diagnosticsHealth()` — and nothing else, ever, until stage 10 |
| `diagnosticsHealth(pack, norms, catalogue)` | all three registries at once | `ModelBrowser` (the validation strip) |

`validateNormsAgainst()` had owned `normUnitMismatch`, `unknownNormMeasure`, `unknownNormContext` and `normNotCapturable` since stage 1 with exactly one caller: its own test. **A check that never runs is indistinguishable from a check that passes.** If you add a referential check, add the call site in the same change.

**And a check that cannot stay silent is worse than none.** `observableNoSignal` fired on all seven
signal-less strike outcomes — `chunk`, `thin`, `top`, `sky`, `shank`, `pull_hook`, `push_slice` —
which are `Observable` (a golfer can plainly see a thin shot) and `Asserted` (we cannot measure it
from our pixels) *by design*. Seven rows describing the design is a health list people learn to
scroll past. It is now scoped by `ConfirmedBy`, exactly as `inconsistentReach` one line below it
always was. `bothTailsOneCondition`, `inconsistentReach`, `needsRevalidation` and `duplicateMeasure`
had no test in either direction, which is how that survived; all four are now gated both ways.

`health()` merges three sources — the pack validator's warnings, the norm set's own (which `norm_provider.h` had promised were "part of the health list" while they were not), and `diagnosticsHealth()`. Codes, with what each means an author should DO, are documented at the top of `diagnostics_health.h`.

**The kind rules are all gated on whether the pack has an opinion.** `faultNotObservable`,
`kindReachMismatch`, `outcomeNotBallFlight` and `outcomeHasEffect` (all warnings, all opening at zero
over shipped content) run only when some condition holds a non-default kind. A pack authored before
the field existed loads with every condition at `Fault`, and an ungated `faultNotObservable` would
accuse every `Latent` row in it of an omission that is really the field's own age. The stated cost: a
pack whose conditions genuinely are all Faults escapes the four — acceptable, and arguably correct,
since there is then nothing for a kind rule to compare.

Note what is NOT there. **"This condition has no prominence" cannot be written at all**, and the
reason is worth understanding before somebody tries: `Prominence` has five legitimate values and no
sentinel, so by the time the validator sees a row, one nobody authored is byte-identical to one
somebody authored at the default rung — and 58 shipped conditions sit at that rung on purpose. Any
predicate over the loaded pack either accuses those 58 or reports nothing. An eighth "unset" value
would fix the predicate and break what the enum is for. `core_pack_test` asks the question at the
only layer where it is answerable, by reading the shipped JSON as raw text and checking the key is
present on every condition.

Two scoping rules there are load-bearing:

- **`personalNormNoSample` is scoped to the personal layer** via `INormProvider::isOverridden` — tracked at merge time, never derived by comparing values (a user row holding the shipped numbers is still the user's). Unscoped, it opens with 39 rows of noise about migrated content that was fine yesterday.
- **`signalNoNorm` is scoped to LIVE measures.** A producer-less measure is already the roadmap's row, and reporting it here would rank it as though authoring a corridor would fix it.

`oneBandCorpus` is different in kind: it needs a pass over the swing library, so it is opt-in (`startCorpusCheck()`), runs on a worker, is capped at 2000 swings **with the cap reported**, and `corpusEverScanned` exists so "nothing found" and "nothing checked" cannot read the same.

**Do not add a check that pins shipped values.** Health checks look for wrong *shapes* — no norm, a unit mismatch, everything graded into one band — never for specific numbers. That is the same mistake the deleted parity gates made.

**The report's words have one source for each message.** `reportFieldIssues(condition)` and
`reportFieldIssues(measure)` (`characteristic_pack.h`) are the checks on one row's report text —
`golferPhraseWording` (a goal over 60 characters or with a number, "don't" or "never" in it, a cue
that promises a fix, …), `golferPhraseMissing`, the picture's `unknownPicture*` and
`pictureIncomplete`, `reportHoldNoReason`, `badGolferSign` — each tagged with the `field` it is
about. `validatePack()` raises them by calling those two for every row, the loader keeps only the
faults the in-memory row cannot hold (a `picture` object with nothing in it, a hold not written as
`{"reason": …}`), and the Diagnostic Model's inspector calls the same two while the user types, so
the line under a field and the line in Health are one string. Do not write a second copy of a
wording rule in the inspector; add it to `reportFieldIssues()`. Whether a listable habit HAS its
words is a different question, `reportContentGaps(pack, drillIds)`: presence only, it needs the
pack and the drill registry, and `core_pack_test` holds the shipped pack to zero gaps.

---

## 10. The GUI façade — `ModelBrowser`

> **This replaced four façades, and the old ones are gone.** `CharacteristicLibraryModel`, `CharacteristicEditorModel`, `NormModel`, `NormEditorModel` and the fourteen QML views under `src/Gui/characteristics/` were **deleted on 2026-07-31**. The Diagnostic Model panel had already answered every deep link into them and depended on none of it, so it was a removal rather than an untangling. If you find a reference to any of those names, it predates that.

One `QML_ELEMENT` marshaller — `ModelBrowser` (`src/Gui/diagnosticmodel/model_browser.{h,cpp}`) — is the whole panel's model: **one connected content graph, read and edited through one object**. The panel is `DiagnosticModel.qml` (Settings navIdx 9), with the `Model*.qml` views and `RingMenu.qml` hanging off it.

### Reuse; do not reimplement

Every **rule** in `ModelBrowser` is a call into the layer that owns it — `causesOf` / `effectsOf` / `coverageOf` / `hasCausalPath` and `tailsOfAxis` (`characteristic_pack.h`), `validatePack`, `diagnosticsHealth`, `layoutDag`, `measureDisplayLabel`, the shared screen/drill/reference registries, the enum label tables. What is reimplemented here is **marshalling, and only marshalling**.

### The layering — why there are four packs, not one

The earlier façade read the pack **as saved**. That is correct for a read-only panel and fatal for an editing one: every surface must show the library *as it would be if you saved now* — the table, the inspector, the graph, and above all the validation strip, which is worthless if it grades the file rather than the draft.

| Layer | What it is |
|---|---|
| `m_core` | the shipped pack, read-only — the **only** way to answer "does this ship?" |
| `m_savedUser` | the user pack as it is on disk — the baseline every dirty mark is measured from |
| `m_working` | the user pack as edited. Unsaved. |
| `m_assembled` | `core + memory(m_working)`, rebuilt after every mutation. **Everything reads this.** |

The norm set is layered identically (`m_savedNorms` / `m_workingNorms` / `m_norms`) because corridors are edited here too, and **one undo history spans both registries**. A command therefore snapshots both packs, and one Save writes both files. Two histories was the alternative and was rejected: an author who edits a characteristic, then its corridor, then hits ⌘Z twice has one mental model of what should happen, not two.

**Copy-on-write falls out of the layering rather than being coded.** Editing a field of a *shipped* object copies that object into `m_working`; undoing that first edit removes the copy — which is exactly the reset, with no separate "take theirs" path to keep in step.

### QML holds no rules

Sorting, filtering, faceting, search, the legality of a proposed link, which cell may be edited and with what control, and which of two values is "yours" — all decided in C++. QML is handed `columns(type)` and `rows(type, query)` and renders them; it does not know what a Measure is. **A rule written in a delegate is a rule nothing can test.**

### The QML API is typed (refactor step 3.9-ii; P9, R-QML-2)

What `ModelBrowser` hands QML is a `Q_GADGET` value type declared in `model_browser.h`, every field named, read in QML by the names the old maps carried: `ModelEditResult` (`ok`, `message`, and `type`/`id`/`edgeId`/`backups`/`cascaded`/`path` from the writes that have them), `ModelCandidate` (every picker), `ModelOption` (closed vocabularies, `current` on the ring collar), `ModelTypeEntry` (the rail), `ModelEditEntry` (the Edits list), `ModelLegendEntry`, `ModelGradePolicy`, `ModelNormSet`, `ModelObjectRef` (`graphFocus()`), `ModelLinkRefusals` (`refuses(id)` / `refusalOf(id)`), `ModelCorridorContext`, `ModelFacetSeed`, `ModelMeasurePreview`. A list is a `QList` of the gadget, which QML reads as a sequence (`length`, `[i]`, `modelData`). The mint takes its four facets as arguments (`previewMeasure(what, quantity, reference, anchor)`).

Since 3.9-ii's second run the table, inspector, graph and corridor plot are typed too, and `model_browser.h` has no A6 line left: `ModelColumn` and `ModelRow` / `ModelCell` (`columns()`, `rows()`, `searchAll()`; a row's `sortKeys` and `searchText` are C++-only members rows() sorts and searches on), `ModelFacet` / `ModelFacetOption` (`facets()`), `ModelInspection` / `ModelInspectorSection` / `ModelInspectorRow` (`inspect()`; a report field carries `wordingChecked` and its `issues`), `ModelFieldIssue` (`fieldIssues()`), `ModelGraph` / `ModelGraphNode` / `ModelGraphEdge` / `ModelGraphMeasure` / `ModelGraphReference` / `ModelGraphHeading` (`graph()`, and the C++-only `dag()`), and `ModelCorridorPlot` / `ModelPlotBand` / `ModelPlotPoint` (`corridorPlot()`).

**What QML passes in** is a `QML_STRUCTURED_VALUE` gadget, so QML still writes a plain object: `rows(type, { search, sort, descending, facets: [{ key, values }], ids })` (`ModelRowQuery`, its picks `ModelFacetPick` — the panel holds its picks as `{ key: [values] }` and passes `Object.keys(…).map(…)`), `graph(type, id, { nodeH, gapX, …, depth, expanded, includeMeasures, … })` (`ModelGraphOptions`) and `corridorPlot(measure, context, { width, height, windowMin, windowMax, previewMu, … })` (`ModelCorridorPlotOptions`). A key left out keeps the gadget's default: NaN (a metric or a preview) or -1 (a bound) means "not given", so the layout's own default stands; the graph's switches default to `DagLayoutOptions`' (model_browser_test holds them together).

**One trap:** a field a map left out is a default on a gadget (`""`, `false`, `0`, `[]`), so QML tests the default, never `=== undefined` (the mint's exact match is `hasExactMatch`; a graph edge with no link row is `!e.rowId`; a field checked live is `wordingChecked`; a claim with a strength to set has `options.length > 0`). A `QVariant` field (a cell's or a field row's stored `value`) is the exception: invalid where the map had no key, so it still reads `undefined`. Calls no view makes (`dag`, `linkLegality`, `bindingsOf`, `linksCitingReference`, `roadmap`, `captureGaps`, `causeCoverage`, `glossary`; `validation()`) are C++ only. The C++ tests read the gadgets by property name, as QML does, through `src/Diagnostics/tests/model_browser_test_support.h`.

### The marshaller is where a feature can be complete on both sides and still absent

Stage 8 finished the DAG's headings, edge labels and arrowheads in C++ and they reached nothing, because `dag()` did not copy them into the `QVariantMap`. Nothing warned — QML read `undefined` and rendered nothing. The graph is a typed `ModelGraph` now (3.9-ii), so a field QML reads must be declared on the gadget, but the copy from the layout is still by hand in `marshalLayout()`: **when you add a field to a layout type, add it to its gadget and copy it in the marshaller.**

A second instance of the same class of bug, found while porting the tests off the old façades: the row comparator tested `typeId() == Int` and nothing else, so every count taken from a container's `size()` — a `qsizetype`, not an `int` — fell through to the string branch and sorted lexicographically. The bibliography read `7 6 6 4 3 2 2 12 1`, with the paper holding up twelve claims buried between the twos and the ones. Corridor `mu` is a `double` and sorted the same wrong way.

Grade policy and library paths are **bound from `AppSettings` by the hosting QML**, never read from settings inside the façade — the object stays testable standalone. `ModelBrowser` and `MetricCatalog` each expose a `gradePolicy` property for that reason.

---

## 11. Adding things

### A norm

Author in `norms.json` (or seat it in the corridor editor). Key it on the **measure**, at the narrowest context where the claim is true. The general case goes at `any`. Unit must match the measure's — the loader refuses a mismatch naming both sides.

### A measure

Add to `core.json` or mint it in the measure picker. `Provided` needs a `metricKey`; `Composed` needs facets that pass the validity table. **Give it a `highMeans` sentence** — the picker will refuse without one, and the sentence is what a later author reads when choosing which tail a signal watches.

The reducer must name the phase the **producer** actually labels. `m_tempoRatio` asked P4 while `tempo_metrics` emits at P7, so it resolved unavailable on every swing for two stages *and* the corridor silently disappeared from two live surfaces.

### A characteristic

Needs a `consequence` (why it matters), a `ConditionKind`, a `Prominence`, a `ConfirmedBy`, an `Observability`, and either `detectedBy` signals or a reason it is latent. Causal edges are `Causes` from cause to effect; the validator refuses cycles, self-edges, and `Corroborates` over an existing causal path.

**Pick the kind before anything else** — it is the one field that constrains the others, and the
validator will tell you when they disagree. Ask what sort of thing it is, not where in the swing it
happens: a movement error is a `Fault`, an impact number is `Delivery`, a range of motion is a
`Capacity`. The trap is `Fault` versus `Delivery`; see §3.

**Prominence is a judgement, and it ships as one.** Set the rung against how often you would expect to
see it in the golfers this library is for. It stays at `practice` tier — **never attach a citation to
a rung**, because no prevalence study exists to attach. If you find yourself wanting to write a note
explaining where the intuition came from, the answer is "this library's editorial judgement" and
nothing more; `core_pack_test` greps the raw content bytes for vendor names, and a provenance note is
where one would get in.

An axis-partner tail is where the kind field earns its keep. Authoring the opposite end of a corridor
does not make that end a fault — `shallowing` is the low tail of `over_the_top`'s axis and is a move
good players make deliberately, so it is `Delivery` with a wide corridor and no injury note. Before
the kind field there was nowhere to say that, and the pattern would have minted a fault out of a
technique.

### A screen or a drill

Author it in `screens.json` / `drills.json`, in the `screen.` / `drill.` namespace — the join with
`Condition::screenRef` and `Condition::drills` is an exact string match, so an id outside the
namespace matches nothing and fails **silently**, which the validator refuses at load. A screen needs
a protocol and a pass criterion; a numeric pass floor needs its unit beside it. A drill needs an
instruction and a target, and the target is written as INTENT — nothing here has measured an effect
on anybody.

**Describe a screen generically, from the clinical range-of-motion literature.** No branded
screening system may be named, cited or alluded to. The movements are common property; the packaging
of them into a named system is somebody's product, and `reference_sets_test` greps the raw bytes.

### Gate words

"What we could not check", on the report's back page, in the golfer's words: one clause per
catalogued gate and engine rule, and the words for each gate subject (`ks.pelvis`, `track.club`,
`shot`, …), in `src/Resources/diagnostics/gate_words.json`, read through `gate_words_pack.h`
(`sharedGateWordSet()`, a user layer overriding by id) and edited on the Diagnostic Model's Gates
page. A gate added to `gate_catalogue*.h` needs its clause here in the same change — `core_pack_test`
holds the shipped file to the catalogue in both directions. Write the clause WITHOUT ITS SUBJECT, to
follow the reading's name after a colon ("when your hips turned fastest: the face-on camera cannot
time a turn…"), lower case and with no digits (`gateWordsProblem()`). A metric subject has no entry:
the report uses that measure's `golferName`. A gate with no clause is left out of the report, never
printed in the analyst's words.

### A new signal test

Add the enum, implement it in `evaluate()`, extend `validatePack()`'s arity/threshold checks, and add a case to `characteristic_engine_test`. `Threshold` and `Ratio` are already there as worked examples of the shape.

### A new health check

Put it in `diagnosticsHealth()`, document the code and its "what to DO" in the header's table, add a label in `HealthView.qml`'s `_codeLabel`, and gate it in `diagnostics_health_test` in **both** directions — the check firing when it should, and staying silent when it should not. Half the value of that test is the negative cases.

### A new file

Three targets: the app (`CMakeLists.txt`), the offline stack (`_pinpoint_offline_sources`, if `swinglab_run` or re-analyse needs it), and the test (`src/Analysis/tests/CMakeLists.txt`). A `.cpp` missing from the app link fails loudly; missing from the offline stack fails at `swinglab_run` link time.

---

## 12. Testing

All in the analyzer suite: `cmake -S src/Analysis/tests -B build/analyzer-tests` then `ctest --test-dir build/analyzer-tests`. Build with `--parallel 4` — the box OOMs above that.

| Suite | Gates |
|---|---|
| `characteristic_pack_test` | load, save, validation, every error and warning code |
| `core_pack_test` | the SHIPPED pack: ids, referential integrity, that every live corridor signal can fire, and that **everything which can fire can also be explained** — a condition detectable today with no cause is a defect, not a backlog item. Also the kind census, that `Outcome` and the `BallFlight` group are coextensive **in both directions** (which is what makes `outcomeReachOf()`'s move a no-op rather than a hope), and — read off the raw JSON, because the loader cannot answer it — that every condition declares a kind and a prominence. The **report gate**: every habit the work-on rule can list has its report words, a picture and a drill that resolves, and every measure it reads has a `golferName` (`reportContentGaps`), with `reportFieldIssues()` naming the right field for each case. The **gate words**: every catalogued gate and rule has a clause and every clause is a catalogued id, the subjects are exactly the gate subjects, every entry passes `gateWordsProblem()`, and a user layer overrides by id through a save and a reload |
| `axis_direction_test` | every corridor signal points the way its own condition claims. The fixture carries the catalogue quote that decides each row, so a new signal with no fixture row **fails** |
| `norm_test` | the grade rule: per-side z, monitor bounds dominating in both directions, the drawn Ideal edge agreeing with the graded one **under every preset**, and the preset ordering invariant |
| `norm_pack_test` | persistence, validation, layering — that context specificity beats layer precedence, the cohort probe order exhaustively, and the band a date of birth derives at a given date |
| `shared_norm_provider_test` | the process-wide norm set: a saved user set plus the reset is what the next take returns, a snapshot taken before keeps its band, the generation moves once per reset, racing first callers get one assembly, and resets interleaved with readers on other threads never hand out an empty, half-built or changing set |
| `seed_conversion_test` | the SHIPPED one-sided content: which band a value lands in either side of the smash-factor conversion, that shape ships on exactly one measure, and that the plausibility caps fall with loft. Pins no `mu` and no `sigma` — those are heuristics and are meant to move |
| `context_tree_test` | upward resolution, the unknown-context rule, the shipped tree |
| `measure_facets_test`, `anatomy_vocabulary_test` | the Composed vocabulary and its validity table |
| `measure_sample_test` | reading a measure off a stored swing; an unsegmented phase yields NOTHING, never a nearest-sample guess |
| `norm_measure_source_test` | values + norms → a graded reading, and the three silent failures (firing on ordinary variation, grading an unknown context against the default, an absent norm reading as a pass) |
| `characteristic_engine_test` | `detect()` and the explanation pass — including that prominence orders two causes alike in coverage and strength, that swapping the rungs swaps the order (so it is the rung deciding and not the id), that it never changes what a cause explains, and that a one-rung pack reproduces the pre-change ordering. Shapes only; no rung or weight is pinned |
| `rank_shift_report` | **not a test.** The measurement `characteristic.h` demands of anything moving the ranking weights. Run it by hand and put its output in the commit message; never gate on it |
| `dag_layout_test` | no overlaps, determinism, depth bound, waypoint routing — samples every curve and fails on any box it crosses |
| `diagnostics_health_test` | every check, in both directions, plus the shipped library |
| `manifest_migration_test` | the (metric, phase) → measure → norm join for every metric that ever had a corridor |
| `reference_bands_test` | the Norm→Band projection, the archetype mechanism, and `ragOf(grade(v)) == classifyDelta(v)` over the whole shipped set |
| `wrist_norm_render_test` | the engine renders a real grid from the shipped norms — and an empty norm source greys everything, so the guard can fail |
| `reference_sets_test` | the screen and drill registries: every reference resolves both ways, each validator fires AND stays silent, and no branded screening system reached the content |
| `model_browser_test`, `inspector_refresh_test`, `corridor_plot_test`, `diagnostics_catalogue_integrity_test` | the façade: every rule it hides is asserted here or nowhere — the layering and undo across both registries, the row comparator's type handling, the roadmap ranking, free-text search, and that screens, drills and the glossary reach QML, which is the "complete on both sides and reaching nothing" trap |
| `axis_direction_test` | every signal watches the tail its `highMeans` sentence says it does — see trap 1 |
| `fire_findings_test`, `rule_version_golden_test` | `fireFindings()` over one swing with every input in the stamp; a rule's output that moves without its version bump fails (C-05) |
| `reduction_rule_golden_test` | the session and athlete reductions (work-ons, tendencies, outcome scope, distances, club baseline, themes, praise, the board) pinned to their versions |
| `session_diagnostics_test` | the session's owner: the re-fire (a club correction re-fires both clubs; a usual's change re-derives only), superseded and trashed results dropped and settled, a release mid-run (T1–T4), unread marks, the load's re-asks, the writes |
| `session_gates_test` | the summary's gate counts: the writer's block for a session with a swing analysed before records existed and two `gatesVersion`s, the same block from results loaded from the file, the index summary's read (a summary without the block is not summarised yet), and the Gates view's sum over two athletes' indexes equal to the library walk, with none, one and every session read swing by swing |
| `athlete_diagnostics_test` | the athlete level: reads summaries only, stale on an event and recomputed when read, the praise rule, the catch-up (oldest first, one at a time, paused and cancelled, write blocks reported) |
| `session_test`, `shot_commands_test` | the `Session` and `Shot` commands and their events, in command order |
| `rule_data_test`, `work_pool_test` | one snapshot shared, an edit publishes a new one; a lane keeps order, two lanes do not block each other, the receiver drops what it cannot resolve |
| `diag_stage_panel_test` | the thin adapters over the owner: the panel, cast, cards and report surfaces, no golfer, a folder no `Session` holds |
| `diag_parallel_live_test` | a live owner against a stored owner of the same live-shaped session: form P equal (D22: a live shot read with no sidecar reads as the stored session does) |
| `session_diagnostics_model_test`, `golfer_input_test`, `shot_number_test` | the thin adapter's view half over the owners (`src/Diagnostics/tests/owner_rig.h`): cards, chains, detail, spread and history, shot outcomes, sequence and composite words, variability marks, the golfer's edits and their events |
| `diag_footer_test` | a finished session's driver footer is its final diagnosis's; live it moves only with the pattern set (§A5) |

The `norm_model_test` / `norm_editor_model_test` / `characteristic_editor_test` targets no longer exist; their assertions were ported onto `ModelBrowser` when the old façades were deleted (§10). One was *not* ported and is called out rather than quietly dropped: the old model returned the roadmap markdown as a string and the test checked its headings. The new panel exports to a file, and a test that wrote into the developer's Documents folder to read it back would be a test with a side effect on the product.

Note where these live: the sources are in **`src/Diagnostics/tests/`** but they are compiled by the **Analysis suite** (`src/Analysis/tests/CMakeLists.txt`) — the same arrangement as `swing_doc_test`. There is no Diagnostics test project. Standalone targets reach shipped content through `pp_norm_env(<target>)` and `-DPP_CORE_PACK_PATH`.

---

## 13. Traps that have already cost time

Each of these is a bug that shipped or nearly shipped. They are here because none of them looks like a bug while you are writing it.

1. **An inverted signal is invisible.** It fires confidently, on the wrong swings, with correct-sounding consequence text. `highMeans` + `axis_direction_test` exist solely to prevent it. Three signals shipped inverted.
2. **A field can be complete on both sides and reach nothing.** Add to the marshaller in the same change, and grep it when you add to a value type.
3. **A rule can exist, be tested, and never run.** `validateNormsAgainst` for nine stages; `detect()` until the session diagnostics wired it. Add the call site in the same change, or write down that you did not.
3b. **Detection and explanation are authored on different axes, so the gap between them is invisible.** Causal edges go in per condition GROUP; what can actually fire is decided per PRODUCER. Add a live producer and the condition over it joins the firing set without ever passing the group sweep that would have given it a cause — which is how ten of the sixteen detectable conditions came to have no explanation while every census looked healthy. **When you make something newly detectable, check what explains it in the same change.**
3c. **A warning that fires on the design is worse than no warning.** `observableNoSignal` accused seven conditions of an omission that was the content telling the truth. Scope a check to the state it actually means, and gate it in BOTH directions — the negative case is what catches this, and four checks had neither.
4. **A corridor keyed on the wrong phase disappears silently.** The reducer must name the phase the producer labels.
5. **Do not pin shipped numbers in a test.** Two parity gates had to be deleted for exactly this. Gate *shapes* and *relationships*, not values.
6. **Unit strings can match while conventions do not.** `normUnitMismatch` compares strings and is structurally incapable of catching stance width reading ~2× its corridor in a field both sides spell `% shoulder width`.
6b. **And it is blind to the PRODUCER, which is the third party to that comparison.** It checks the norm against the MEASURE, and the loader refuses a mismatch there — so both ends of the content agree by construction while the producer emits something else entirely. Three live measures shipped declaring `cm` over producers emitting `×frame` or `mm`: `sig_excessiveHeelLift` could not fire on any swing ever recorded, and `headSway` read Action on all of them. Two silent failures in opposite directions from one root. `measureUnitMismatch` now compares `Measure::unit` against `MetricDescriptor::unit` (`Rate` reducers exempt) and is gated at zero over the shipped library. **A producer must emit ONE unit for a key, for all time** — where that depends on a ruler, the metric is absent when the ruler fails rather than present in the fallback scale, because a metric whose unit changes per swing cannot carry a norm at all.
7. **Never derive "edited" by comparing values.** A user row holding the shipped numbers is still the user's. Track it at merge time.
8. **`NotMeasured` is never a pass**, and an omitted condition is not a `NotFired` one. Three distinct states; nothing may collapse them.
9. **Inside a Repeater delegate, only the component root id resolves** — and a handler on a composite type that declares its own `id: root` cannot see even that. It throws only on click, so no binding, test or screenshot will show it. At file scope, `root.x` inside a composite is fine.
10. **A `const Condition *` dies when `save()` reloads the provider.** Copy what you need out first.
11. **Text links are `Theme.colorAccent` at rest**, body font, trailing arrow, cursor change only (`ScreenHome.qml`'s `switchLink`). Muted-until-hover is for secondary chrome, not for a way out of the page.
12. **A default setting can hide a whole-band defect, and the tests will agree with it.** The Ideal band was drawn at `mu ± sigma` and graded at `mu ± idealMaxZ × sigma` for nine stages. Under `standard` those are the same number, so nothing failed — and three separate tests had written the defect down as a requirement ("a policy change does not move the IDEAL band"). **When a value is a default, sweep the non-defaults**: `norm_test` and the façade's corridor assertions now run under every shipped preset, not only the shipped one.
13. **An epistemic field will absorb an ontological job if there is nowhere else to put it.** `Observability` and `ConfirmedBy` mean "can it be seen" and "how is it established". With no field recording *what sort of thing this is*, they quietly became a record of producer coverage instead — `over_the_top` shipped `Latent` + `Asserted` for a year, meaning "invisible" and "a belief", because nobody had written it a measure. The same pressure turned `ConditionGroup::Setup` into the bucket holding fourteen physical screens, nine beliefs and a set of clubs. **Nothing reported either, because every row still held a legal value in every field** — which is what makes this class of drift different from a bug. When a field's values start correlating with what the pipeline can do rather than with what the thing is, the taxonomy is missing an axis.
14. **A census in prose rots silently.** §8.3 read "112 conditions, 178 edges" while the pack held 146 and 306 — understating the causal graph by more than half, in the document people consult to find out how big the model is. A wrong number in a comment is not a compile error and no test looks at it. Recount before quoting, with the one-liner at the head of §8.3.
15. **Positional cell arrays fail in a way count parity cannot see.** `conditionRow()` builds cells in two branches against one `columns()`; transpose two in one branch and the count is unchanged, nothing throws, and the facet rail silently reads the wrong column — with the *filter* agreeing, because it resolves the value the same wrong way. Both halves wrong in the same direction is invisible to any check that counts. `model_browser_test` asserts each enum column holds only that enum's labels.

---

## 14. File map

```
                                Four folders by layer, lowest first: model → content → derived → engine. A file
                                includes only its own folder and those before it (ctest diagnostics_dir_order_check,
                                tools/refactor/dir_order_check.py; R8, ruled 26 Nov 2026). Tests in tests/ are exempt.
src/Diagnostics/model/          the vocabulary and the types:
  characteristic.{h,cpp}        Measure · Signal · Condition · Edge, enums, label tables
  pack_io.{h,cpp}               the pack's file shape, validatePack / loadPack
                                norm · context_tree · measure_vocabulary.h · measurement_precedence.h ·
                                anatomy_vocabulary · measure_facets · measure_sample (builds and reduces the phase grid) ·
                                delivery_grid_numbers.h · excludes_rule.h · club_baseline_provider.h · enum_table_p.h
src/Diagnostics/content/        the loaders, validators and registries: characteristic_pack · screen_pack · drill_pack ·
                                gate_words_pack · reference_pack · norm_pack · layered_set.h · area_regroup_table.h ·
                                pack_provider.h · norm_provider.h · {resource,file,merged,memory}_{pack,norm}_provider ·
                                provider_leaf_p.h · condition_reading.h
src/Diagnostics/derived/        the derived facts (outcomes, factors, tendencies, the ledger, the session's results) and
                                their packs (swing_themes_pack.h among them); series/ the Charts' session maths
src/Diagnostics/engine/
  characteristic_engine.{h,cpp} detect(), evaluate(), Finding
  relation_resolver.{h,cpp}     explain(): ranked causes + screen recommendations
  fire_findings.{h,cpp}         fireFindings(), deriveShotData(), FiringInputs, firingStamp()   (the shot)
  session_reduction.{h,cpp}     reduceSession(): the session's diagnosis, pure               (the session)
  diagnostics_health.{h,cpp}    assembled-library checks
                                the measure sources: live_measure_source (detectForSwing) · norm_measure_source.h ·
                                session_measure_source.h · composite_reading.h · metric_corridor.h ·
                                shot_direction_bounds.h · wrist_domain_score.h
src/Vocabulary/Analysis/        phase_grid.h (the phase grid's value types) · sigma_source.h (P-3)
src/Stores/phase_grid_store.*   swing_phasegrid.json's file door (FILE-SHOT-07); a shot's grid through
                                RecordingReader::phaseGrid (P-3)

src/DomainModel/                the owners: session · shot · session_diagnostics · athlete · athlete_diagnostics ·
                                rule_data (+ rule_data_repository.h; the store Stores/rule_data_store)
src/App/athlete_catch_up.*      the catch-up (an application service)
src/Core/work_pool.*            the one pool, its lanes, the ask counter, the receiver
src/Stores/firing_reader_store.* the firing reader (a shot's grid and summary from its Recording)
src/Gui/diagnostics/            the thin adapters: session_diagnostics_model (its view half; the old engine went in 5.4b),
                                session_diagnostics_adapter (diagnosticsAdapters), work_ons_controller,
                                diag_parallel_run (form P, the parity dump's, and the in-memory doors)
src/Gui/diagnosticmodel/        ModelBrowser + DiagnosticModel.qml + the Model*.qml views
src/Gui/diagnosticmodel/layout/ dag_layout.{h,cpp} (the causal graph) · corridor_plot.{h,cpp} (the corridor picture):
                                pixel layout, plain C++, the UI ring (tests in src/Diagnostics/tests)
src/Resources/diagnostics/      core.json · norms.json · contexts.json · screens.json · drills.json · gate_words.json
src/Wrist/reference_bands.{h,cpp}      NormBandProvider — Norm -> Band for the wrist grid
src/Gui/review/metric_catalog.cpp      corridors for the metric + dashboard surfaces
```

**Read next:** `docs/design/swing_fault_ontology.md` (why `kind` and `prominence` exist, and the coaching review that produced them), `docs/design/diagnostics_norms.md` (the brief), `docs/implementation/diagnostics_norms_impl_plan.md` (how it was built, its ledger, and the handoff to *Diagnosis execution, V&V*), `docs/user/pinpoint-diagnostics-guide.md` (the same model in non-technical language — useful for naming things the way users will), `governance/artefacts/sign_conventions.md` (before touching any direction), and `docs/developer/metric_catalogue_developer_guide.md` (the other registry).

## Notes

[^ps-d5]: Plane stack D5, 13 Nov 2026.
[^ps-d6]: Plane stack D6, 13 Nov 2026.
[^dec-owners]: DEC-100, DEC-135, DEC-190–192.
[^dec02]: DEC-02.
[^dec135]: DEC-135.
[^dec171]: DEC-171.
[^dec123]: DEC-123.
[^dec191]: DEC-191.
[^dec126]: DEC-126.
[^dec46]: DEC-46.
[^dec172]: D7: DEC-172.
[^j49]: J-49.
[^dec92]: DEC-92.
[^j53]: J-53.
[^dec195]: DEC-195.
[^j51]: J-51.
[^dec165]: D3: DEC-165.
[^j59]: J-59 (`build/refactor/step-3/JUDGEMENT-CALLS.md`).
[^ja3]: J-a3.
[^j61]: J-61.
[^ps-d2]: Plane stack D2, 13 Nov 2026.
[^r53-tools]: Refactor item 5.3, 22 Nov 2026 (`build/refactor/step-5/BRIEF-5.3.md`); the corpus golfer file is the Product Owner's answer 1 to step 5's questions, 22 Nov 2026.
[^r54b]: Refactor 5.4b, 22 Nov 2026: the old diagnostics engine deleted once migration M1 left no session of the library naming another athlete id (build/refactor/step-5/DUTIES-5.4.md). Coordinator judgement call (J-72), 22 Nov 2026.
[^wilson-ends]: #259, ruled by the Solution Architect, 25 Nov 2026: arm64's fused multiply-subtract left about 1e-18 at the lower end, which reordered never-fired conditions on the Mac only. Work-on rule 7.
