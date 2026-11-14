# G · Derived facts — who computes each one, who reads it, and where it should live

Reviewer G, cross-cutting. Commit `37289e33`. Read-only. Every claim cites the line at that commit. A runtime
defect I have not reproduced is marked **suspected**, with the scenario that would show it.

## 1. Summary

The rules themselves are in good shape. Strike, direction, flight, Sky, the strike-versus-flight check, carry
pools and the frozen baseline are pure header functions in `src/Coaching`, each written once and unit-tested.
Callers share the function, not a copy of it.

The problem is the facts those functions produce. None is owned at its level, stored, and read by everyone.
Each surface calls the rule again on its own copy of the inputs:

1. **A shot's outcome and three factors have no shot-level owner.** Up to four `SessionDiagnosticsModel`
   instances compose them. The athlete level composes them again for every shot of every session, from
   `diagnostics.json` and `work_ons.json`, not from what the session level computed (G-03). This shot never
   receives the factors at all.
2. **Two shot-shape classifiers disagree today.** The launch monitor card and Club delivery use
   `lmShapeClass` (pull at 1°, fade at 2° of axis). The ledger's direction rows use the pack (pull at about
   4°, fade from 4°). One shot can read "Pull–fade" on the card and "Straight" in the three factors (G-01).
3. **Handedness is glossed surface by surface, and three surfaces miss it** (all suspected for a
   left-hander). The pattern cards and chains, the Home work-ons, and Club delivery, whose left-hand flag is
   never set (G-02).
4. **Sky is a factor but not a label.** The same shot can be "Good shot" and "clean" in This shot and the
   shot mix, but a Sky fault in the club's flight bar and in Tendencies (G-04, suspected).
5. **The per-shot "score" pill is the wrist estimand.** It is 0 when there is no lead-wrist series, and it is
   drawn as a red "0" on every card in other sessions (G-05, suspected).

The check (brief question 2) is applied in one function and in one class. But it runs inside each model
instance, in memory, and is written to disk only when something else forces a write. Readers that go to disk
(Charts, Tendencies, work-ons, baseline freezing) read the last persisted verdict, not the one the panel
shows (G-06).

## 2. How the area is put together

- **Rules (pure, `pinpoint::analysis`, header-only):**
  - `shot_outcomes.h::composeShotOutcome`: labels, phrase, story, driver, carry waterfall.
  - `shot_factors.h::shotFactors`: strike, direction and flight.
  - `strike_from_flight.h`: flight usuals, `classifyFlight`, the Sky rule, `checkStrikesAgainstFlight`.
  - `club_distances.h`: pools, frozen baseline, "Your distances".
  - `tendencies.h` and `outcome_scope.h`: the athlete reductions.
  - `lm_inferred_reads.h`: LM shape and strike reads.
  - `lm_flight_path.h`: the flight model.
- **Session and shot facts are computed together, in memory, per model instance.** The instance is
  `SessionDiagnosticsModel`. Its `ensureOutcomes()` (`session_diagnostics_model.cpp:1789-2086`) composes every
  shot, then tallies the reasons, shot log, shot mix, carry today and club mix. The instances are:
  - the panel (one per panel, including the cast);
  - the work-ons catch-up (`work_ons_controller.cpp:351`);
  - the report (`session_report_model.cpp:301`).
- **Persisted:**
  - the checked rows, corrections and intents, in `<session>/diagnostics.json`;
  - the session's link and tier evidence, in `work_ons.json` `outcomeEvidence` (`work_ons_controller.cpp:623`);
  - the frozen usual numbers, in `club_baseline.json`.
- **Not persisted:** outcomes, factors, flight usuals, carry today, club mix.
- **Athlete facts are reduced off the thread by `WorkOnsController`** from the persisted files.
  `reduceTendencies` is reached three ways:
  - `maybeTendencies` (Home);
  - `runThemes` (the outcome filter, via `judgedShotFactors`);
  - `tendenciesAsOf` (the report).
  Each path re-composes every shot: `tendencies.h:551-560`, `outcome_scope.h:147-156`.
- **The launch monitor is a parallel world.** `LmSessionModel` (the panel; a second one is built inside the
  report, `session_report_model.cpp:1925`) has its own:
  - per-shot club (override, evidence or recorded);
  - carry statistics;
  - tile grades;
  - shape and strike reads.
  None of them pass through the ledger.

## 3. The matrix

"Once" means one function is called. "Copies" means the fact is recomputed from raw inputs at more than one
call site, or by a second rule.

| # | Fact | Level | Producers (file:line) | Consumers (file:line) | Computed | Drifted? | Should be owned by |
|---|---|---|---|---|---|---|---|
| 1a | Three factors (strike · direction · flight) | Shot | Rule: `shot_factors.h:279`. Calls: `session_diagnostics_model.cpp:2060` (session club mix), `tendencies.h:559` via `:793` (Home, report), `outcome_scope.h:147` via `work_ons_controller.cpp:1681` (Home filter) | Session Outcomes bars `PpDiagSessionOutcomes.qml:44,180`; Tendencies/flow and filter counts (`work_ons_controller.cpp:2174`); report `report_tendencies.h:51`. **Not** This shot: `shotOutcome()` returns no factors (`session_diagnostics_model.cpp:2203-2218`) | Copies: 1 session path + 3 athlete paths, each re-composing | Inputs differ (G-03). Copies use the same rule | A per-shot record, computed once by the session owner after the check, stored, read by Home and the report |
| 1b | Flight band (Low/Mid/High) | Shot | `strike_from_flight.h:387` `classifyFlight`. Called from `shot_factors.h:345` and `strike_from_flight.h:506` (`flightSaysSky`) | as 1a; Sky check `:574`; usual-carry mishits `club_distances.h:201` | Copies (re-integrates the flight model each call, `:371-383`) | Norms: session model uses `flightNorms()` (test override, `session_diagnostics_model.h:1014-1016`); athlete uses `shippedFlightNorms()` (`work_ons_controller.cpp:227`). Same in the app | Stored with 1a |
| 2 | Strike-vs-flight check (overruled rows) | Session (rewrites shot rows) | `strike_from_flight.h:554`. Called only from `session_diagnostics_model.cpp:1773` via `checkRowsAgainstFlight` at `:879`, `:1338`, `:1513`, `:1767` | In memory: composer `shot_outcomes.h:569-585`, mishits `club_distances.h:173`. From disk (`diagnostic_ledger.h:2269`): Tendencies `work_ons_controller.cpp:196`, themes/filter `:1670`, Charts `session_series_model.cpp:796`, baseline freezing `session_diagnostics_model.cpp:2560-2570`. **Bypassed** by the LM card's strike read `lm_session_model.cpp:1358` | Once per instance per change; persisted only on an owed write | Suspected after a `flight_norms.json` change (G-06). LM card never checked (G-01) | One session owner; persisted whenever flags move |
| 3 | Session flight usuals; Sky (`flightSkied`, `flightSaysSky`) | Session (usuals) → shot (Sky) | Usuals `strike_from_flight.h:96`. Called from `:564`, `session_diagnostics_model.cpp:1779` (every `ensureOutcomes` and `correctOutcome` `:2408`), `club_distances.h:254` (each pool: carry today, every predecessor at freeze, every session in distances), `tendencies.h:537` (every session in each of 3 reductions). Sky `:483`/`:503`, from `shot_factors.h:352`, `:574`, `club_distances.h:201` | Strike inference `shot_outcomes.h:616`; factors; check; mishits | Copies (≥ 6 call paths) | Same function and inputs, so no drift today (G-10) | Stored on the session record |
| 4 | Composed outcome (labels, phrase, story, driver, carry waterfall) | Shot | `shot_outcomes.h:557`. Called from `session_diagnostics_model.cpp:1830`, `:2408` ("bare", uncorrected) and `tendencies.h:558` | This shot `PpShotOutcomeHead.qml:43-60` via `shotOutcome` (`:2146`); Analyse `PpDiagAnalyse.qml:111`; Replay `PpDiagReplay.qml:125`; print `PpDiagPrint.qml:86` (the report's own instance); shot log, reasons, mix (`:1851-2009`); Tendencies driver words (`tendencies.h:805-806`) | Copies | Driver: session uses live links/tiers (`:1820-1825`); athlete uses `work_ons.json` evidence, "absent on a record not re-derived yet" (`work_ons_controller.cpp:168-173`) | As 1a |
| 5a | Carry today (per club, this session) | Session | `session_diagnostics_model.cpp:2012-2046` (qualifying shots via `poolSessionByClub`) | `PpDiagSessionOutcomes.qml:230-232`, Replay `:131` | Once | **Second number**: LM board and report LM figures, all non-excluded shots under the board's club (`lm_session_model.cpp:1085-1092,1137`; `session_report_model.cpp:1129-1136`) (G-08) | Session owner; the LM board reads it or labels its own |
| 5b | Usual carry (frozen baseline) | Athlete → session snapshot | `club_distances.h:433` from `session_diagnostics_model.cpp:2577` | `usualFor` at `:1831`, `:1867`, `:2027`, `:2153`; tendencies `tendencyUsualEntry` `tendencies.h:541` | Once, frozen | Gated `n ≥ 10` in carry waterfall and carry today (`shot_outcomes.h:437`, `session_diagnostics_model.cpp:2028`), **ungated** in `shotCarryLoss` (`tendencies.h:202`) for reason cost and tendency loss | Fine as a snapshot; one gate |
| 5c | "Your distances" (usual, current) | Athlete | `club_distances.h:675` from `work_ons_controller.cpp:234` | Home Tendencies rows | Once | Named "usual" like 5b; different window (G-08) | Athlete owner (is) |
| 6 | LM reading and LM-derived rows | Shot | Emitter `swing_doc.cpp:1082` and `:1053`. Writers: live `:1981`, re-analysis carry `:1640-1700`, `refreshLaunchMonitorRows` `:2037` (from `reanalysis_controller.cpp:77`, `swinglab_run.cpp:741`, `lm_repair.cpp:212`), device-only `:2157` | Diagnostics readings `session_diagnostics_model.cpp:1235-1238`; LM board `lm_session_model.cpp:1019`; report raw block `session_report_model.cpp:155`; flight model `strike_from_flight.h:381`, `lm_session_model.cpp:1286` | Once (shared emitter); strip-and-replace loop ×3 | Re-analysis carries `lm.` rows verbatim while refresh rebuilds them from raw (G-14) | Export (is) |
| 6b | Inferred shape and strike reads | Shot | `lm_inferred_reads.h:223` `lmShapeClass` (LM card `:298`, Club delivery `session_delivery.cpp:709-711`, compound miss); `lmStrikeQuality` `:484` | LM card; Club delivery and report Delivery page | Once in LM land | **Disagrees** with the ledger's direction and strike rows (G-01) | One per-shot direction and strike verdict |
| 7 | Club identity | Shot | Declared `club_vocabulary.h:117` (diagnostics `measure_sample.cpp:1000`, Charts `session_series_model.cpp:238`). Stubbed "DRIVER" `:128` (carousel `swing_doc.cpp:1148`, `shot_processor.cpp:2389`). LM effective club `lm_session_model.cpp:354-364` (override in `review.clubOverride`, `shot_list_model.cpp:411`; evidence relabel). Canonical `club_vocabulary.h:74` | Every per-club reduction; LM board scope and grades `lm_session_model.cpp:1064,1146` | **Three identities** | Board grades an undeclared swing as a driver (suspected, G-07) | One resolved club per shot |
| 7b | Club display label | Presentation | `ClubFormat.qml:36-40`, `session_summary.h:119`, `club_distances.h:104`, `PpLmGraphicsBody.qml:289`, `session_delivery.cpp:158`, `report_words.h:192`, `charts_posture_presenter.cpp:1003` | — | 7 copies | Spellings differ ("7 iron" vs "7 IRON" kept; trim or no trim) (G-12) | One C++ helper and `ClubFormat` |
| 8 | Handedness gloss | Shot (display) | `shot_outcomes.h:194`. Applied in composer `:643`, `:783`; factors `shot_factors.h:131`; SDM reasons `:1944`, `:1955`, detail `:2307`, correction `:2395`; tendencies `:853`, `:1009`. LM card `lm_inferred_reads.h:233` with `leftHanded` from `PpLaunchMonitorPanel.qml:77` | Not applied: cards `session_diagnostics_model.cpp:5063,5103` (`conditionName` `:3267`); work-ons `work_ons_controller.cpp:213`; Club delivery `session_delivery.h:169` (never set) | Per surface | Missed on three surfaces (G-02) | Once, on the per-shot and per-condition display record |
| 9 | Grade and corridor of a metric on a shot | Shot | Engine `norm.h:506` (`grade`) for rows; LM board tiles `lm_session_model.cpp:871`; `gradeOnCorridor` `work_ons_controller.cpp:102`; `deviationEdges` `norm.h:533`; `spreadBands` `session_spread.h:312` | Rows, cards, LM tiles, Home area items | Rule written 3× ("Mirrors grade() term for term", `norm.h:532`) | Board ignores the #176 status rule (`metric_corridor.h:96-123` vs `characteristic_pack.h:522`). Latent: every LM measure is live today (G-13) | `norm.h` (is) + one edge function |
| 9b | Variability grade per club and reading | Session | Shared grader; inputs built in SDM `:3654-3661`, `:4579`, LM board `:883-913`, Charts `charts_traces_presenter.cpp:2491,2550`, posture `:1052`, wrist `:1352`, consistency `consistency_grades.cpp:90` | each panel | One grader, 7 input builders | Only the LM board applies live hysteresis (`lm_session_model.cpp:912`); board groups by its own club (G-13) | — |
| 10 | Per-club factor mix and tendencies | Session (mix), athlete (tendencies) | Session mix `session_diagnostics_model.cpp:2048-2085`; tendency bar `tendencies.h:971-980`; club tuples `tendencies.h:794`; filter counts `work_ons_controller.cpp:1681-1700`; report `work_ons_controller.cpp:274` | Session Outcomes; Home Tendencies/Flow/filter; report Tendencies page | Copies with **different counting** | Same club and session, different % (G-09) | Session mix at session level; Tendencies reads it |
| 11 | Shot "score" | Shot | `wrist_resemblance.cpp:84-114`, `analysis_pipeline.cpp:2060,2103` | Card `PpShotCard.qml:236-239`, action bar `PpShotActionBar.qml:185-188`, filter `PpShotFilter.qml:287-306` | Once | Wrist-only number shown as a general quality score; 0 when absent (G-05) | — |
| 12 | Intent per shot | Shot | `session_diagnostics_model.cpp:1747-1752` `intentFor`; `tendencies.h:190` `resolvedShotIntents` | composer | 2 implementations | Equivalent today (G-14) | One function |
| 13 | Filter key `strike:…\|direction:…\|flight:…` | Contract | `outcome_scope.h:74-78`; `HmShotFilter.qml:86-97` | Home | 2 | Same today (G-11) | C++ only |

## 4. Findings

### G-01 · The launch monitor card and Club delivery name a shot's shape and strike with their own rules, which disagree with the ledger
- **Category:** 4 One source per derived fact; 5 Duplication. **Severity:** High
- **Evidence:**
  - LM thresholds: `lm_inferred_reads.h:63-66`, `double startT = 1.0;`, `double curveT = 2.0;`, `double severeAxisT = 10.0;`. The classifier is "the one every shape-naming surface shares" (`:296-298`). It is used by the LM card (`lm_session_model.cpp:1323`) and by Club delivery (`session_delivery.cpp:709-711`, "so this section and the launch monitor card name a shot alike").
  - The ledger's direction rows use the pack instead:
    - `core.json:5418-5425` `"sig_fadeBeyondStraight" … "threshold": 4.0`;
    - `core.json:5233` `sig_pull` is `outsideCorridor` on `m_lmLaunchDirection`, with `norms.json:2096-2099` `"mu": 0.0, "sigmaLo": 2.0`. It fires at Watch, beyond 2σ under Standard (`norm.h:88`), so beyond about 4°.
  - The compound misses are the exception. They come from `lmCompoundMiss`, which "asks lmFlightShape" (`lm_inferred_reads.h:387-390`). So push_slice uses the LM thresholds and push uses the pack's.
  - Strike: `lmStrikeQuality` names "Thin" on `av > t.severeVMm` (`lm_inferred_reads.h:505-506`), from the raw strike height, with no flight check. The ledger overrules that same thin when the flight disagrees (`strike_from_flight.h:570-572`).
- **What goes wrong.** Take a 7 iron with start −2.5° and spin axis +3°.
  - The LM card and Club delivery say "Pull–fade" / "Pull, slight fade".
  - The ledger fires neither pull (needs < −4°) nor fade (needs > 4°). The direction factor says **Straight**, and Tendencies counts it as straight.
  - On the 4 Aug sticker-offset session (cited at `strike_from_flight.h:71-74`), the LM card says "Thin" on shots that This shot reports as "Low on the face, flight normal".
  - The header claims all shape surfaces share one classifier. That is true for the LM surfaces only. Drift is already present by construction.
- **Fix.** Pick one per-shot direction and strike verdict, owned at shot level.
  - Either express `lmShapeClass`'s thresholds as the pack's signals, or have the pack read `lmShapeClass`'s output, as `compoundMiss` already does.
  - The LM card then shows the shot's checked strike and direction values (G-03's per-shot record) and keeps its own words only as "inferred" evidence.
  - The thresholds are coach config. Changing them needs Mark.
- **Effort:** M. **Related:** G-03, G-06.

### G-02 · Handedness is glossed per surface, three surfaces miss it, and the hand comes from three places
- **Category:** 4 One source; 2 Identity. **Severity:** High (suspected runtime defect; the structure is shown by the code)
- **Evidence:**
  - Rows are physical, so the gloss is needed. `shot_outcomes.h:191-192`: "Rows stay target-relative; a left-hander's ball going right is a hook".
  - It is applied in the composer (`:643`, `:783`), the factors (`shot_factors.h:131`), the session reasons (`session_diagnostics_model.cpp:1944`, `:1955`) and the tendencies (`tendencies.h:853`).
  - Pattern cards, chains and watching are named unglossed:
    - `session_diagnostics_model.cpp:5063` and `:5103` `c[QStringLiteral("name")] = conditionName(id);`;
    - `conditionName` is the raw pack label (`:3267-3272`).
  - Home work-ons are unglossed: `work_ons_controller.cpp:213` `e.name = packLabel(pack, e.id);`.
  - Club delivery is never mirrored. `session_delivery.h:169` reads `bool leftHanded = false; // … the snapshot carries none today`. Nothing in `src/` assigns it (grep `leftHanded =` in `src/Gui/charts`, `src/Gui/report`: only the declaration). Yet `session_delivery.cpp:718` says "the words mirror for a left-handed golfer", and the QML doc says so too (`PpChShotShapeGrid.qml:24`).
  - The three sources:
    - `ShotRecord.handedness` from each swing's summary (`session_diagnostics_model.cpp:1239`);
    - the *current* athlete setting for the LM card (`PpLaunchMonitorPanel.qml:77` `athleteController.currentHandedness === "Left"`);
    - the first document's `hand` for the report (`session_report_model.cpp:1207`).
- **What goes wrong (suspected).** For a left-hander whose ball curves right (spin axis +):
  - This shot says **Hook**;
  - the LM card says Hook;
  - the session's pattern card for the same row says **Slice**;
  - Club delivery says "slight fade".
  
  An in-to-out work-on is listed on Home as "Out-to-in path". Reviewing an old session after the athlete's hand setting changed would also flip the LM card but not This shot.
- **Fix.** Gloss once, on the display record each owner publishes:
  - the per-condition display id and name, beside the row id, on the ledger summary;
  - the per-shot record (G-03).
  
  Take the hand from one place (the session's shots). The presenters stop calling `glossedConditionId` themselves. `DeliveryShotInput` takes the shot's hand.
- **Effort:** M. **Related:** G-03.

### G-03 · A shot's outcome and factors have no owner: computed per model instance and re-composed by the athlete level from different inputs
- **Category:** 1 Ownership; 4 One source (wrong level). **Severity:** High
- **Evidence:**
  - Session level: `session_diagnostics_model.cpp:1829-1831` `m_composed.push_back(composeShotOutcome(s, m_corrections.value(s.shotId), intentFor(s.shotId), usualFor(s.club), m_outcomePack, facts));`. The factors exist only inside the club-mix tally (`:2060`). `shotOutcome()` returns no factors (`:2203-2218`).
  - The athlete level re-composes every shot:
    - `tendencies.h:557-559` `o = composeShotOutcome(…); return shotFactors(…)`;
    - `outcome_scope.h:147-156` `judgedShotFactors`;
    - three entry points: `work_ons_controller.cpp:227` (Home), `:1681` (filter), `:274` + `session_report_model.cpp:141` (report).
  - The design is deliberate: `work_ons_controller.cpp:622` "what the tendency reduction needs to re-compose every shot itself".
  - Inputs differ by level:
    - links and tiers: live `m_outcomeEvidence` (`:1820-1825`) versus `work_ons.json` (`work_ons_controller.cpp:168-173`, "Absent on a record not re-derived yet: the words then say less");
    - rows: in memory (`m_shots`) versus disk (`fromJson`, `work_ons_controller.cpp:196`);
    - norms: `flightNorms()` versus `shippedFlightNorms()`.
- **What goes wrong.**
  - The driver ("because …") a Tendencies row quotes for a shot can differ from This shot's driver. This happens whenever the catch-up has not re-derived the session since its links moved, e.g. a session reviewed right after a correction.
  - The flight factor can differ wherever the persisted checked rows lag the panel's (G-06).
  - This shot cannot show the shot's own Flight value, though the session bar and Tendencies count that shot under it.
  - Each of four in-app instances recomputes the same facts. Each athlete reduction recomposes the whole library: three reductions, each over every session.
- **Fix.** A `ShotDiagnostics` value per shot, computed once by the session owner after the check and persisted with the ledger (a `shots[].outcome` block keyed by the content stamp). It holds:
  - checked rows' verdict;
  - labels and display ids;
  - the three factors with method and raw value;
  - the carry block;
  - the driver.
  
  This shot, the shot log, the club mix, Tendencies, the filter and the report read it. The athlete level reduces these records and stops recomposing from rows and evidence.
- **Effort:** L. **Related:** G-04, G-06, G-09, G-10; information-model §3.

### G-04 · Sky is a flight factor but not a label, so a skied shot can be "Good shot" and "clean" while the bars count it as a Sky fault
- **Category:** 4 One source. **Severity:** High (suspected)
- **Evidence:**
  - The composer drops Sky: `shot_outcomes.h:597` `fired.removeAll(QStringLiteral("sky"));`.
  - Clean is decided without it: `:913` `out.clean = hasLm && directionAssessed && !anyMiss;`, with "Good shot" at `:918-922`.
  - The factor adds it: `shot_factors.h:352` `if (value == QLatin1String("high") && flightSkied(shot, usuals)) value = QStringLiteral("sky");`.
  - Both meet in one panel: the session mix counts clean (`session_diagnostics_model.cpp:2000` `composeSessionMix(m_composed)`, `shot_outcomes.h:962` `if (s.clean) { ++clean; continue; }`), while the club flight bar counts the fault (`session_diagnostics_model.cpp:2069`).
- **What goes wrong (suspected).** A driver flies High, 3° steeper than the session's usual attack, at 80% of the usual carry. With no frozen baseline yet, `carry_deficit` cannot fire, so the shot has no miss label.
  - This shot: "Good shot".
  - Shot log cell: clean.
  - Session mix: "n clean".
  - Same panel's Driver flight bar: one **Sky**.
  - Tendencies: a Sky fault.
- **Fix.** The composed record carries the factors (G-03), and `clean` / "Good shot" means no fault in any factor. Or Sky stays a label for display while being counted once as a factor. Which is intended is Mark's call.
- **Effort:** S once G-03 exists. **Related:** G-03.

### G-05 · The per-shot "score" is the wrist estimand, shown on every card as a quality score, and is 0 when nothing scored it
- **Category:** 11 Missing values; 14 Leftover. **Severity:** High (suspected; Critical if confirmed on screen)
- **Evidence:**
  - The only producers are the wrist stages:
    - `analysis_pipeline.cpp:2015` `ctx.detail->score = WristResemblanceScorer::score(…)`;
    - `:2060` and `:2103` `ctx.detail->score.overall = ar.score.total;`.
  - No lead-wrist series gives 0: `wrist_resemblance.cpp:94-96` `if (!fe) { // no FE channel ⇒ resemblance is undefined  sb.overall = 0;`.
  - Device-only shots: `swing_doc.cpp:2151` "No score: nothing scored it". Read back as 0 at `:1180-1182`.
  - The card always draws the pill: `PpShotCard.qml:236-239` `PpQualityPill { … score: card.score  mark: true }`, with no visibility condition. `Theme.qml:680` `if (score < 25) return colorError`.
  - The model field is stale: `shot_list_model.h:256` `int score = 0; // 0–100 quality (placeholder for now)`.
- **What goes wrong (suspected).** In a camera-only or launch-monitor-only session, every carousel card and the action bar show a red "0" with a target mark. The quality filter offers 0 as a band. This contradicts the rule that a missing value shows "-". The number also measures wrist resemblance, not the shot, while the diagnostics carry their own per-shot verdict (factors, fired counts). So two per-shot "quality" claims can disagree.
- **Fix.**
  - Make "not scored" a state (NaN or a `has` flag) carried from `ScoreBreakdown` through the summary and the model, and have the pill show "-".
  - Name the number for what it is (the wrist score), or decide what a per-shot quality number should be.
  
  The pill's look is a design-system matter, so Mark approves before anything visual changes.
- **Effort:** S–M. **Related:** June review Theme I (placeholder surface).

### G-06 · The strike-versus-flight check is applied in memory per instance, persisted only when something else is written, and its Sky half is missing from every staleness stamp
- **Category:** 4 One source; 9 Persistence. **Severity:** Medium (suspected)
- **Evidence:**
  - The file claims the rule is applied once: `strike_from_flight.h:521-524` "This is the ONE place … so diagnostics.json holds the checked rows and every reader … counts the same verdict".
  - On load the check runs (`session_diagnostics_model.cpp:1511-1513`), but activation only persists through `settleBatch()`, which returns unless a write is owed (`:1378` `if (!m_owedRebuild) return;`).
  - Sky overrules depend on the flight table (`strike_from_flight.h:574` `flightSaysSky(s, usuals, norms)`). Yet the table is in no stamp:
    - the session content stamp has pack, norms, contexts and schema, but no flight norms (`session_diagnostics_model.cpp:929-942`);
    - the work-on fingerprint is rule version + content stamp + documents (`work_ons_controller.cpp:478-487`);
    - only the athlete tendency fingerprint has it (`:239-249`).
  - These readers trust the stored flags:
    - Charts (`session_series_model.cpp:796`);
    - themes and filter (`work_ons_controller.cpp:1670`);
    - baseline freezing (`session_diagnostics_model.cpp:2566`);
    - posture presenter (`charts_posture_presenter.cpp:709`).
- **What goes wrong (suspected).** A band in `flight_norms.json` is tuned so a driver that read High now reads Mid.
  - The panel, on its next activation, overrules that shot's fired sky row in memory and shows it.
  - `diagnostics.json` keeps it fired until some swing regrades.
  - Charts' fired overlay, the work-on counts and the next baseline freeze still count the sky.
  - Tendencies (re-fingerprinted) recompute the factor fresh. So Home and the panel agree with each other and disagree with Charts.
  
  The check runs only inside `SessionDiagnosticsModel`, so the LM card never sees it (G-01).
- **Fix.**
  - Put the flight table's stamp into `computeContentStamp`.
  - Persist after `checkRowsAgainstFlight` whenever any `overruled` flag changed.
  - Ultimately, persist the verdict once at the session owner (G-03), which then is the "one place" the comment promises.
- **Effort:** S. **Related:** G-01, G-03.

### G-07 · A shot has three club identities, and the launch monitor board grades an undeclared swing as a driver
- **Category:** 2 Identity; 4 One source. **Severity:** Medium (the grading is suspected)
- **Evidence:**
  - Diagnostics and Charts use the declared club, empty when none: `measure_sample.cpp:1000` `grid.club = swingDocDeclaredClub(root);` and `session_series_model.cpp:238`.
  - The carousel and summary use the stub: `swing_doc.cpp:1148` `s.club = swingDocClub(root);`. `club_vocabulary.h:69` `clubStub() { return QStringLiteral("DRIVER"); }`.
  - The same header explains why the stub is wrong for grading, `club_vocabulary.h:110-112`: "For a PICKER row the stub is a kindness … for a CORRIDOR it is a lie".
  - The LM board takes `recorded` from that stubbed role (`lm_session_model.cpp:1027`). It then lets an override or an evidence relabel win (`:354-364`) and grades in `contextIdForClub(club)` (`:1146`).
  - The override is persisted separately as `review.clubOverride` ("Nothing else in the app interprets them", `shot_list_model.h:76-78`).
- **What goes wrong.**
  - A camera swing recorded before `capture.club.name` existed has no club in diagnostics. The carousel shows "Driver", and the LM board scopes it with the driver shots and colours its tiles against driver corridors (suspected).
  - A shot the board relabels to pitching wedge (by evidence or by the golfer) stays a 7 iron in carry today, the usual carry, the flight usuals, the three factors and Tendencies.
  - `strike_from_flight.h:25-27` notes that "a wedge filed as a 7 iron" is common, but the session's own correction to it never reaches the diagnostics.
- **Fix.**
  - One resolved club per shot (declared → golfer's board override → nothing), carried on the shot record and read by every surface.
  - The stub becomes a display-only fallback word.
  - Whether a board override should re-club the shot for diagnostics is Mark's decision.
- **Effort:** M. **Related:** June review Export item "club hardcoded DRIVER on reload" (partly fixed: see §6).

### G-08 · "Carry this session" and "your usual carry" each exist twice with different rules, and the short-of-usual loss ignores the usual's own minimum
- **Category:** 4 One source; 10 Configuration. **Severity:** Medium
- **Evidence:**
  - Diagnostics' carry today uses qualifying shots only (part swings and strike mishits out, `club_distances.h:265-277`), grouped by declared club (`session_diagnostics_model.cpp:2018-2023`).
  - The LM board and the report's LM figures use every non-excluded shot with readings, grouped by the board's club (`lm_session_model.cpp:1085-1092`, `:1137`; `session_report_model.cpp:1129-1136`).
  - "Usual" means two things:
    - the frozen baseline (strictly earlier days) in This shot and carry today;
    - `reduceClubDistances` over all sessions, including this one (`club_distances.h:675-679`), as Home's usual distance.
  - The gate on the usual differs:
    - the carry waterfall gates `usual->n < kClubBaselineMinShots` (`shot_outcomes.h:437`);
    - carry today gates `usual->n >= kClubBaselineMinShots` (`session_diagnostics_model.cpp:2028`);
    - `shotCarryLoss` has no gate (`tendencies.h:202-209`). It feeds the reasons' "cost" (`session_diagnostics_model.cpp:1867`) and the tendency loss.
- **What goes wrong.**
  - In one session the 7 iron can read "152 ± 6" in the Outcomes block and a lower mean on the LM board, because the board includes the two fats.
  - The usual on This shot and the usual on Home differ as days pass, both called "usual".
  - With 3 earlier 7-iron shots, a reason reports "costs 14 yd" against a usual the waterfall refuses to show.
- **Fix.**
  - Carry today is a session fact: one owner, with the qualifying rule stated. The LM board either reads it or labels its figure "all shots".
  - One `earnedUsual(entry)` predicate used by all three.
  - Distinct words for the frozen usual and the current usual.
- **Effort:** S–M. **Related:** G-07.

### G-09 · The per-club factor mix is counted two ways under one bar
- **Category:** 4 One source; 5 Duplication. **Severity:** Medium
- **Evidence:**
  - Session level: "Every value a shot shows is counted … a factor's parts can add up to more than its shots" (`session_diagnostics_model.cpp:2046-2050`, tally at `:2062-2066`).
  - Tendencies: "The bar: each shot once, under whichever of its values is commonest with this key" (`tendencies.h:970-980`).
  - Both feed `composeFactorMixBar` (`shot_factors.h:381`), which renders either way.
- **What goes wrong.** For a session of 10 driver shots, 4 of them push slices:
  - the session Outcomes bar shows Push slice 40%, Slice 40%, Push 40% (parts > 100%);
  - Home Tendencies scoped to that same session shows Push slice 40% once.
  
  Same club, same shots, different percentages in two places, both drawn by one component.
- **Fix.** Choose one counting rule for the bar, or name them differently. The session mix should be the session owner's and Tendencies should reduce from it (G-03).
- **Effort:** S. **Related:** G-03.

### G-10 · Session flight usuals (a session fact) are recomputed from raw rows at least six ways and never stored
- **Category:** 4 One source (wrong level); 6 Cost. **Severity:** Medium
- **Evidence:** `sessionFlightUsuals(m_shots)` is called:
  - in every `ensureOutcomes()` and every `correctOutcome()` (`session_diagnostics_model.cpp:1779`, `:2408`);
  - inside each check (`strike_from_flight.h:564`);
  - inside every pool (`club_distances.h:254`), which runs:
    - for carry today;
    - for each of up to ten predecessors when a baseline freezes (`session_diagnostics_model.cpp:2577`);
    - for every session in "Your distances";
  - per session in every tendency reduction (`tendencies.h:537`), three of which exist (Home, filter, report).
- **What goes wrong.** No drift today, because it is one function on the same rows. But the athlete level recomputes session facts itself from raw rows, and `flightSaysSky` re-integrates a flight model for each candidate (`strike_from_flight.h:505-507`, `:371-383`). The session-to-shot context flow the owner describes is invisible: no object holds "this session's usuals".
- **Fix.** Store the usuals on the session record that the session owner persists (beside the ledger), and pass them to every shot-level judgement.
- **Effort:** S once a session record exists. **Related:** G-03.

### G-11 · Domain rules and a contract written in QML
- **Category:** 3 Layering; 7 Contracts. **Severity:** Medium
- **Evidence:**
  - `PpShotOutcomeHead.qml:80` `function _noneWord(family) { return family === "strike" ? "Clean" : … }`. The factor table calls the same value "Solid" (`shot_factors.h:171`).
  - `:81-86` `_meantShapes` decides which shape a golfer meant from substrings of label ids (`indexOf("slice")`).
  - `:52-55` derives the session-level fact "no reading on any shot" from `shotLog`.
  - `HmShotFilter.qml:86-97` re-implements the filter key format of `outcome_scope.h:74-78`.
- **What goes wrong.**
  - The correction row says "Clean" where the factor bar beside it says "Solid".
  - A new direction id (e.g. a "push_fade") would be mapped by a string match in a view.
  - The key format has two writers that must change together.
- **Fix.** The model supplies the none-word, the meant shapes and `sessionUnread`. QML takes the key as an opaque string from C++, which already holds the parser.
- **Effort:** S. **Related:** G-03.

### G-12 · Club labels and club groupings exist as several copies
- **Category:** 5 Duplication; 10 Configuration. **Severity:** Low
- **Evidence:**
  - Display, seven copies:
    - `ClubFormat.qml:38-39`;
    - `session_summary.h:119-125` (trims);
    - `club_distances.h:104-107` (does not);
    - `PpLmGraphicsBody.qml:289` ("Unknown club");
    - `session_delivery.cpp:158-163` (lower-case);
    - `report_words.h:192-202`;
    - `charts_posture_presenter.cpp:1003-1007`.
  - Canonicalisation outside `canonicalClubId`: `session_delivery.cpp:153-154`, `tendencies.h:229`.
  - Groupings: `flight_norms.json` categories (data); `tendencyIronGroup` (`tendencies.h:227-236`, code: long 1-4, mid 5-7, short 8-PW); `isSkyClub` (`strike_from_flight.h:474-478`, substring); `tendencyClubGroup` through the context tree (`work_ons_controller.cpp:139-151`).
- **What goes wrong.** The groupings agree today. A club added to the vocabulary or re-banded in `flight_norms.json` has to be added in three other places, and labels can already differ by whitespace.
- **Fix.** One C++ `clubLabel()` behind `ClubFormat`. One club-category table in data, read by flight, tendencies and Sky.
- **Effort:** S.

### G-13 · The grade rule is written three times, and the board's tiles skip the measure-status rule
- **Category:** 5 Duplication; 4 One source. **Severity:** Low
- **Evidence:**
  - The rule is written three times:
    - `norm.h:506-523` `grade`;
    - `norm.h:532` "Mirrors grade() term for term; a change to one must change the other" (`deviationEdges`);
    - `work_ons_controller.cpp:97-112` `gradeOnCorridor` ("Boundaries as grade() has them").
  - The board grades `res.grade(st.latest, …)` (`lm_session_model.cpp:871`) via `corridorForMetricAtPhase` (`metric_corridor.h:96-123`), which never calls `packMeasureGrades` (`characteristic_pack.h:522`, "may a verdict rest on this measure's reading?").
  - Live hysteresis, which "Live surfaces pass … through" (`variability_grader.h:120-121`), is applied only by the board (`lm_session_model.cpp:912`).
- **What goes wrong.** Latent. Every LM measure is `live` in `core.json` today, so no tile is wrongly coloured now. Demote one and the board still colours it while the rows refuse it. A change to grade edges must be made in three functions.
- **Fix.** One `gradeOnEdges(edges, value)` used by `grade` and Home. Status check inside `corridorForMetricAtPhase`. Decide whether every live panel applies hysteresis.
- **Effort:** S.

### G-14 · Small duplicate and leftover derivations
- **Category:** 14 Dead code; 5 Duplication. **Severity:** Low
- **Evidence:**
  - `composeShotMixBar` and `ShotMixTally` (`shot_outcomes.h:1001-1053`) have no production caller; only `tendencies_test.cpp:246-250` uses them.
  - Intent resolution has two implementations: `session_diagnostics_model.cpp:1747-1752` and `tendencies.h:190-198`.
  - The `lm.`-row strip loop is written three times (`swing_doc.cpp:1972-1979`, `:2051-2057`, plus the carry at `:1670-1686`).
  - Re-analysis carries `lm.` rows verbatim (`:1680-1686`) while `refreshLaunchMonitorRows` rebuilds them from raw (`:2062`). So a `fieldDefs` change reaches a swing only through lm_repair, not through re-analysis.
- **What goes wrong.** Two rules must change together. A dead mix composer reads as live.
- **Fix.** Delete the dead composer. One `resolvedIntent(shotId, …)`. One `replaceLmRows(an, reading, root)` used by all four writers.
- **Effort:** S.

## 5. Done well

- **The rules are pure, header-only and tested.** Examples: `shot_factors.h:48`, `strike_from_flight.h`, `club_distances.h:42` ("Unit-tested standalone"). Every duplicate *fact* in this report calls the same *function*. Nobody has copied a rule body. Fixing G-03 is a move of call sites, not a reconciliation of copies.
- **The flight table is data with a schema** (`strike_from_flight.h:228-233`), read once (`:464-468`), and stamped into the tendency fingerprint (`work_ons_controller.cpp:239-249`). The Sky rule, the strike inference and the check live beside it, so "one Sky" holds wherever the rule is called.
- **`gradedReadings` (#176)** is applied at the top of every verdict function (`strike_from_flight.h:102`, `:140`, `:182`, `:390`, `:486`; `tendencies.h:206`), so held readings never judge.
- **LM-derived rows have a single emitter** (`swing_doc.cpp:1053-1094`). It is shared by the live pairing, re-analysis, `refreshLaunchMonitorRows` and device-only writes. The CLIs (`swinglab_run.cpp:741`, `lm_repair.cpp:212`, `regrade_ledger.cpp:95`) call the app's code rather than re-implementing it.
- **The LM surfaces share one shape classifier.** `lmShapeClass` replaced two drifting copies (`lm_inferred_reads.h:296-298`), and `compoundMiss` is constructed from it with a sweep test (`:387-390`).
- **One club resolver for diagnostics and Charts** (`club_vocabulary.h:95-125`), with the reason documented.
- **Grade policy presets are one table** (`norm.h:92-111`). QML only maps a grade token to a tone (`Theme.qml:733-746`) and does not grade.

## 6. June review status (`code_quality_review.md`)

The June review predates `src/Coaching`, so none of the derived-fact code existed. One item touches this area:

- **Export: "`club` hardcoded "DRIVER" on reload — silent data fabrication (`swing_doc.cpp:254`)". Partly fixed.**
  - Diagnostics, Charts and the phase grid now use `swingDocDeclaredClub` (`club_vocabulary.h:117`), which returns "" when no club was declared.
  - The stub remains for the carousel and summary (`swing_doc.cpp:1148`) and the live row (`shot_processor.cpp:2389`). From there it reaches the LM board's club scope and grading (G-07).
- **Theme K (magic numbers): open, but disciplined.** Coaching thresholds are named `constexpr`s with their reason (`strike_from_flight.h:61-76`, `:470-471`, `club_distances.h:63-69`). They are code, not data. `flight_norms.json` is the exception that went to data. `LmInferenceTuning` is called "COACH-SET CONFIG, not view literals" (`lm_inferred_reads.h:57-61`), but it is a struct of defaults in code.

## 7. Corrections to 01-information-model.md

1. **Four in-app `SessionDiagnosticsModel` instances, not three.** `SessionReportModel::model()` creates one per report (`session_report_model.cpp:301`), reviewing, with persistence off (`:305-308`) and `BaselineAuthority::Never` (`:505`). It composes This shot for the printed report (`PpDiagPrint.qml:86`). The tools `regrade_ledger`, `diag_rank_diff` and `diag_uncertainty_report` create more. There are also two `LmSessionModel`s: the panel (`PpLaunchMonitorPanel.qml:63`) and a local one in the report build (`session_report_model.cpp:1925`).
2. **"Outcomes and factors: not persisted (recomputed)" is right, with two additions.**
   - The session's outcome evidence (links and tiers) *is* persisted, in `work_ons.json` `outcomeEvidence` (`work_ons_controller.cpp:623`), so that the athlete level can recompose every shot.
   - The athlete level therefore recomputes the **shot** level too, not only the session level: `tendencies.h:551-560` from `reduceTendencies`, the outcome filter (`work_ons_controller.cpp:1681`) and the report (`:274`).
3. **The three factors are not part of the shot surface.** §3 lists "factors" under the shot level, computed by `shotOutcome`. In fact `shotOutcome()` returns no factors (`session_diagnostics_model.cpp:2203-2218`). The factors are computed only inside the session club-mix tally (`:2060`) and the athlete reductions.
4. **"Athlete baselines feed session grading (`usualFor`, :2153)" needs one qualification.** `usualFor` reads the session's own frozen sidecar `club_baseline.json` (`:2491-2495`). The first model with authority writes it, from its predecessors' `diagnostics.json` (`prepareClubBaseline`, `:2511-2585`). It is a session-owned snapshot of athlete data, not a read of the athlete level.
5. **The check (`checkStrikesAgainstFlight`) runs on every row change *and on every load*** (`:879`, `:1338`, `:1513`, `:1767`), in each instance. A load-time change is not persisted unless another write is owed (`:1378`). See G-06.
6. **The launch monitor board holds a fourth per-shot identity and its own per-club statistics.**
   - Its effective club (`lm_session_model.cpp:354-364`) is persisted as `review.clubOverride`, separately from `review.club`.
   - Its carry and tile grades never pass through the ledger.
   
   §2's identity table should list the club alongside the numberings (G-07).

## 8. Questions I could not settle from the code

1. Should the launch monitor card's shape ("Pull–fade") and the ledger's direction factor agree on every shot? Or is the card's an "inferred" read that is allowed to differ? The `LmInferenceTuning` comment says its thresholds still await the coach (`lm_inferred_reads.h:60-61`).
2. Should This shot show the shot's Flight value (Low, Mid, High or Sky)? And should a Sky shot ever read "Good shot" (G-04)?
3. Are the session club mix's "every value counts" bars and Tendencies' "each shot once" bars meant to show different percentages for the same shots (G-09)?
4. Should a club the golfer picks on the launch monitor board re-club the shot for diagnostics, carry and Tendencies, or stay a board-only view (G-07)?
5. What should the per-shot score pill show outside wrist sessions (G-05)?
6. `session_report_model.cpp:509-513` says the report's `activateSession` reconcile "persists, as it does for the panel". But the model was built with `setPersistEnabled(false)` (`:307`). I could not settle from the code which one is true; it matters for G-06.
