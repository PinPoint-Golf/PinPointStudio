# Reviewer C: src/Coaching and its seams

Pinned at `37289e33`. The area is 21 headers (14,093 lines, 371 inline functions, no `.cpp`, no build target). It meets
`src/Diagnostics`, `src/LaunchMonitor`, `src/Analysis` and `src/Gui` (SessionDiagnosticsModel and WorkOnsController).
I only read code. Nothing was built or run. Defects I have not reproduced are marked **suspected**.

## 1. Summary

The numbers are carefully done. The software structure around them is not. The statistical core
(`diagnostic_ledger.h` tiers, Wilson bounds, link grading, chain extraction) is pure, injectable, deterministic and
well tested. So are most of the athlete-level reductions. What matters most:

1. **Coaching has no level owners.** It is a library of free functions over value structs. Each file mixes two or
   three diagnostic levels. All the derived state (shot outcomes, session evidence, tendencies, themes) is held by two
   Gui controllers. Shot-level facts (composed outcome, three factors) are never stored. They are recomposed in at
   least three places, and each place builds its session context with its own helper (C-01).
2. **A session-level verdict is written into the persisted shot rows.** `checkStrikesAgainstFlight` changes a shot's
   ledger rows based on the other shots in its session, and diagnostics.json keeps the result. That breaks the
   ledger's own rule ("what is persisted is the rows, not the reductions"). The six readers of the file mostly don't
   check its version (C-02, C-03).
3. **Golfer-facing English is built and persisted in the domain.** Two Coaching files state "data only, no prose".
   Most of the rest (tendencies, shot outcomes, club distances, swing themes) build sentences, and tendencies.json and
   diagnostics.json store them. So a wording change is a rule-version bump plus a cache rebuild (C-04).
4. **Version discipline is manual and has drifted.** One rule change on 10 Nov needed five hand bumps across five
   constants. Changelog entries are missing, and "schema" and "rule" mean opposite things in different files (C-05).
5. **Copies of one rule already disagree.** "Which conditions can headline" is projected from the pack five separate
   ways. Swing themes ignores `reportHold` and `Setup`, which the work-ons rule applies, and chicken_wing is held in
   the shipped pack (C-06).

The rest is Medium or Low: I/O and a silent hidden singleton (`shippedFlightNorms`), an upward include into Gui,
god functions (composeShotOutcome is 390 lines, reduceTendencies 438), duplicated id lists, three spellings for each
launch-monitor reading, two "missing" glyphs, and dead code that only its own tests keep alive.

## 2. How the area is put together

**Objects.** There are no classes with state. Everything is an `inline` free function over plain structs:
`ShotRecord`/`ConditionRow` (ledger rows), `ConditionLedger`, `LinkEvidence`, `ShotOutcome`, `ShotFactors`,
`SessionWorkOns`, `TendencySet`, `SwingThemes` and `ClubBaselineSnapshot`. Each `*_pack.h` maps the
`CharacteristicPack` onto small structs, so no pack type reaches the arithmetic. Everything lives in
`pinpoint::analysis`, except `session_report.h` (`::report`) and `consistency_home.h` (`pinpoint::charts`).

**Who owns the results.** `SessionDiagnosticsModel` (Gui, 6,575 lines) holds the session: `m_shots`, `m_ledgers`,
`m_outcomeEvidence`, and `mutable std::vector<ShotOutcome> m_composed` at `session_diagnostics_model.h:1007`. It is
the only writer of diagnostics.json. `WorkOnsController` (Gui, 2,325 lines) holds the athlete: `m_themes`
(`work_ons_controller.h:653`) and `m_tendencySet` (`:686`). It also owns its own `SessionDiagnosticsModel` for the
catch-up (`:647`). Nothing owns the shot level. Its facts are recomposed on demand.

**Data in.** Ledger rows come from the engine via `detectShot` (Gui). The pack facts come through the marshallers. The
flight table comes from `shippedFlightNorms()` (a Qt resource or an environment variable). The athlete baseline is
`club_baseline.json`, frozen per session. Session evidence is `work_ons.json` `outcomeEvidence`.

**Data out.** Five JSON files are written by Gui through Coaching codecs: `diagnostics.json` (rows, corrections,
intents), `work_ons.json`, `club_baseline.json`, `tendencies.json` and `swing_themes.json`. QVariantMaps go out
through `tendency_flow_map.h` and `sequence_checks.h`.

**Levels by file** (S = shot, Se = session, A = athlete, V = view/presentation, IO = file I/O):

| file | levels | notes |
|---|---|---|
| diagnostic_ledger.h | S rows + Se reductions + persistence | also carries a copy of the LM readings per shot (schema 11–13) |
| strike_from_flight.h | S rules, Se usuals, Se→S row mutation, IO | flight_norms.json loader and singleton |
| shot_outcomes.h | S composer (takes Se facts and the A baseline), Se mix, club mix bar, V words | |
| shot_factors.h | S factors, mix bar, S miss helpers named `tendency*` | |
| club_distances.h | A baseline and distances, S mishit and swing length, Se pools, V formatting, IO | |
| tendencies.h | A reduction; also S corrections/intent JSON, the Se evidence JSON, the club-distance row JSON, V words | |
| work_ons.h | Se list + A aggregate | cleanly split by function |
| swing_themes.h | A reduction + linear-algebra library + V (`swingSummaryView`) + persistence | |
| session_report.h | Se | includes a Gui header |
| outcome_scope.h, club_scope.h, swing_areas.h | A filters and areas | |
| sequence_checks.h | S checks + Se consistency + QVariantMap | |
| tendency_flow.h / _map.h | A view layout + QVariantMap words | |
| consistency_home.h | V (namespace `pinpoint::charts`) | |
| dashboard_reductions.h | V, dead | |

**Is header-only a deliberate seam?** Yes, and it says so. `sequence_checks.h:28-29` reads: "Header-only and
Qt-Core-only … so the session model, the Charts series model and the tests all include it without a link-line change."
The tests do compile each header standalone (`src/Analysis/tests/*_test.cpp`).

The cost is not compile time. The worst header is included by at most 10 app translation units (my transitive count).
The cost is structural:

- There is no library boundary. Coaching's tests sit under `src/Analysis/tests`.
- Engine types are copied to keep headers standalone (`CompositePartRow`, `diagnostic_ledger.h:515-518`).
- Configuration arrives through default arguments to hidden singletons (C-07).
- Everything shares one namespace with eleven other directories (C-14).
- Without objects there is no place for the level owners. That responsibility has moved up into Gui (C-01).

The house rule in `analysis_pipeline_developer_guide.md` §6.2 is "header + cpp". Coaching does not follow it.

## 3. Findings

### C-01 · The three diagnostic levels have no owner in Coaching; shot-level facts are unowned and recomposed per consumer
- Category: 1 Ownership and lifetime, 4 One source per derived fact      - Severity: High
- Evidence:
  - `src/Coaching/tendencies.h:26-28` — `Every shot is RE-COMPOSED here from its ledger row, the session's stored evidence block and the pack's adjacency (composeShotOutcome, the same composer as the panel)`
  - `src/Gui/diagnostics/session_diagnostics_model.h:1007` — `mutable std::vector<pinpoint::analysis::ShotOutcome> m_composed;     // aligned with m_shots`
  - Two separate builders of the same session context:
    - `session_diagnostics_model.cpp:1779` — `return ShotOutcomeSessionFacts{ m_outcomeEvidence.links, m_outcomeEvidence.tiers, sessionFlightUsuals(m_shots) };`
    - `src/Coaching/tendencies.h:537` — `return ShotOutcomeSessionFacts{ s.evidence.links, s.evidence.tiers, sessionFlightUsuals(s.session.shots) };`
  - A third consumer recomposes too:
    - `src/Coaching/outcome_scope.h:158-166` — `judgedShotFactors(...)` calls `judgeTendencyShot` for every shot
    - `work_ons_controller.cpp:1681` calls `judgedShotFactors`
  - `sessionFlightUsuals` is recomputed at four call sites: `strike_from_flight.h:564`, `club_distances.h:254`, `tendencies.h:537` and `session_diagnostics_model.cpp:1779`.
  - The hazard shows in a Gui comment: `work_ons_controller.cpp:1673-1674` — `off the whole session (its flight usuals are the session's), so it takes the ledger before the club filter.`
  - Athlete-level state lives in a Gui controller: `work_ons_controller.h:653` `SwingThemes m_themes;`, `:686` `TendencySet m_tendencySet;`
- What goes wrong: "one composer" is guaranteed only by sharing a function. A consumer gets the same answer only if it
  builds the same inputs: rows, corrections, intent, frozen usual, links and tiers, and session usuals over the
  *whole* session. The panel uses the live `m_outcomeEvidence`. Tendencies and the shot filter use the copy stored in
  `work_ons.json`.
  - If the copy on disk is older than the live session (the catch-up has not rewritten it yet), Home's tendency and
    This Shot can describe the same shot differently.
  - A caller that slices the shots before computing usuals gets a different Sky and a different inferred strike. The
    `work_ons_controller.cpp:1674` comment shows one caller already had to code around this.
  - Because no shot-level object stores the outcome, each new surface has to recompose it and repeat the whole recipe.
- Fix:
  - Give each level a value object owned at its level:
    - `ShotDiagnosis {rows, outcome, factors, carry}`, built once per (shot, session evidence) and stored on the session.
    - `SessionDiagnosis {shots, ledgers, links, usuals, mixes}`, owned by a session-scoped service rather than a QML model.
    - `AthleteDiagnosis {baselines, tendencies, work-ons, themes}`, owned per selected athlete.
  - Coaching keeps the pure functions that build these, and views only read them.
  - Compute the session usuals once, as a field of `SessionDiagnosis`, and never from whatever shot vector a caller
    happens to hold.
- Effort: L        - Related: C-02, C-04; Gui reviewers' findings on SessionDiagnosticsModel/WorkOnsController

### C-02 · A session-level verdict is written into persisted shot rows, against the ledger's own persistence rule
- Category: 3 Layering, 9 Persistence, 4 One source      - Severity: High
- Evidence:
  - The ledger's rule: `src/Coaching/diagnostic_ledger.h:2141-2143` — `WHAT IS PERSISTED IS THE ROWS, NOT THE REDUCTIONS. … writing them out would be writing a cache that can disagree with the code that produced it.`
  - `src/Coaching/strike_from_flight.h:521-523` — `applied by SessionDiagnosticsModel each time the session's rows change … so diagnostics.json holds the checked rows and every reader … counts the same verdict`
  - `strike_from_flight.h:538-540` — `the first three 7 irons' thins stand, and the fourth gives the club a usual that can overrule all four.`
  - `strike_from_flight.h:577-579` — `r.state = ShotState::NotAssessable; r.notAssessableReason = *why; r.overruled = true;`
  - These readers rely on the persisted check without the model:
    - `work_ons_controller.cpp:196` and `:1396`, both `fromJson(root.value(QStringLiteral("ledger")).toObject())`
    - `session_series_model.cpp:796`
    - `charts_posture_presenter.cpp:714`
- What goes wrong:
  - A shot's persisted row state is no longer that shot's reading. It is a session reduction, using session usuals
    and the flight table, frozen at the last write.
  - If a threshold in `strike_from_flight.h` (`kFlightThinLaunchShare`, `kSkyCarryShare` …) or flight_norms.json
    changes, every ledger on disk carries the old verdict until a model reopens and rewrites it. The readers listed
    above count the stale verdict meanwhile.
  - Nothing in the file records which flight rule produced the verdict. The only stamp is `kDiagSchemaVersion`, which
    is defined as the ROW shape (`:178-179`).
  - The 10 Nov change had to bump the schema (14) to force a regrade. That is a rule version disguised as a schema.
- Fix:
  - Keep the measured row as measured. Make "overruled" a derived session fact, computed in `SessionDiagnosis`
    (C-01) from rows, usuals and norms.
  - If it must be persisted for outside readers, persist it beside the rows as a stamped reduction: rule version plus
    the norms hash, as the tendency fingerprint already does for the table bytes (`work_ons_controller.cpp:241`).
    Readers should then reject a stale stamp.
- Effort: M        - Related: C-03, C-05

### C-03 · The ledger file's reader does not enforce its own contract: no version check; stored gates vs today's gates is ambiguous
- Category: 7 Contracts, 9 Persistence      - Severity: Medium
- Evidence:
  - `src/Coaching/diagnostic_ledger.h:2396-2420`: `fromJson` reads `options` and `shots` and never reads `schemaVersion`.
  - Only one reader checks the version: `session_diagnostics_model.cpp:2564` `if (ledgerObj.value(QStringLiteral("schemaVersion")).toInt() != kDiagSchemaVersion) allCurrent = false;`
  - These readers do not check it: `work_ons_controller.cpp:196`, `:1396`, `:1670`, `session_series_model.cpp:796`, `charts_posture_presenter.cpp:714`.
  - `diagnostic_ledger.h:2144-2147` — `a gate that moves in a later build re-grades an old session honestly instead of quoting a stale verdict. The options travel alongside so a session records the gates it was read under`
  - But the readers reuse the stored gates:
    - `session_diagnostics_model.cpp:1471` `m_shots = fromJson(ledgerObj, &m_opt);`
    - `session_series_model.cpp:797-800` `by the ledger's own reduction over the options the file was written with`
  - `LedgerOptions::softTier/mcDraws/mcSeed` (`:261-263`) are not written (`:2373-2385`), so those three always take today's values.
- What goes wrong:
  - **Suspected.** Take a session whose diagnostics.json was last written at schema ≤13 and not yet regraded after
    the upgrade. Tendencies, consistency marks and Charts read its rows with sticker-offset thins still `Fired`, even
    though tendency rule 14 says an overruled thin is no miss.
  - Whether the catch-up always rewrites every stale ledger before those jobs read it is not visible in Coaching; see
    the Questions.
  - Separately, a moved default in `LedgerOptions` does *not* reach an old session, which contradicts the comment.
    Two of the "gates" (softTier, mcDraws/mcSeed) do reach it.
- Fix:
  - `fromJson` should return the version (or `std::optional`, or `ok`), as `sessionWorkOnsFromJson` and
    `tendencySetFromJson` already do. Put the envelope (`"ledger"` key, corrections, intents) behind one
    `readDiagnosticsFile()`.
  - Decide in one sentence whether old sessions use stored or current gates, persist all of LedgerOptions or none,
    and correct the comment.
- Effort: S        - Related: C-02, C-05

### C-04 · Golfer-facing English is built inside domain reductions and persisted; the area's own "no prose" rule is applied in two files only
- Category: 3 Layering (presentation in domain), 9 Persistence      - Severity: High
- Evidence:
  - The rule is stated twice:
    - `diagnostic_ledger.h:1409-1411` — `DATA ONLY — no prose. … a reduction that emits English is a reduction that has to be re-translated.`
    - `session_report.h:38` — `WORDS ARE NOT BUILT HERE.`
  - Sentences built in the domain:
    - `tendencies.h:690-697` (e.g. `"%1 and %2 came together on %3 of %4 shots."`)
    - `:1072-1073` `QStringLiteral("↑ growing") … QStringLiteral("↓ easing") : QStringLiteral("→ steady")`
    - `:1150` `"%1 %2 of your last %3 bad %4 shots."`
    - `:1181` `"On the ball — your %1 with %2%3."`
    - `shot_outcomes.h:919-924` (`"Good flight · strike not measured"`)
  - Persisted English, athlete level:
    - `tendencies.h:1209-1231`: tendencies.json stores `label`, `frequency`, `seenWith`, `trendWords`, `chain[].text`, `alsoSeen`, `earlyNote`, `playTo`
    - `:465-482`: club rows store `mishitsText`, `fewShotsText`, `gapsText` and `shortBelowText` (`"Carry under %1 — your average less 1.5 × your spread…"`)
    - `shot_outcomes.h:1062`: `ShotMixBar.line`
  - Persisted English, session/shot level:
    - `strike_from_flight.h:544` `"the strike height disagrees with the ball flight"`, written into `ConditionRow::notAssessableReason`
    - Compare `CompositeUnreadRow::reason`, which persists tokens (`diagnostic_ledger.h:535` `notMeasured · noCorridor · …`), and Gui words them at display (`session_diagnostics_model.cpp:187-192`).
  - English used as identity: `shot_outcomes.h:169` `QString mixKey; // "fat slice": the phrase without its quantity, lower case`. It is the grouping key at `:963-965` and `tendencies.h:1126-1128`.
  - Presentation changes forced rule bumps: `tendencies.h:71` rule 16 is `a factor's bar names a value to watch (Low) as kind "watch"`, and rule 2 is `a left-hander's chain is glossed`.
- What goes wrong:
  - Every wording change needs a `kTendencyRuleVersion` bump and a full athlete re-reduction. Otherwise tendencies.json
    keeps the old words, which happened for rules 2 and 16.
  - The left-hander gloss is fixed at reduction time.
  - QML cannot re-style or localise anything.
  - A pack wording edit (`golferShort`) changes `mixKey` and so splits one shot combination into two groups.
  - Within one file format (diagnostics.json), some reasons are tokens and some are sentences.
- Fix:
  - Reductions return ids, counts, enums and numbers. Put one `coaching_words.h` (or Gui `report_words.h`, which
    already exists) at the presentation boundary to turn them into sentences.
  - Persist tokens such as `overruled: "strikeHeight"` and never sentences.
  - Key mixes by the sorted label-id tuple, not by phrase.
- Effort: L        - Related: C-05, C-13

### C-05 · Rule and schema versions are maintained by hand, inconsistently, and their changelogs have lost entries
- Category: 9 Persistence and versioning, 13 Comments as a symptom      - Severity: High
- Evidence:
  - One upstream change (the 10 Nov flight check, commit 770b9fce) bumped five constants by hand:
    - `diagnostic_ledger.h:224` (schema 14)
    - `work_ons.h:90` (rule 6)
    - `tendencies.h:71` (rule 14)
    - `outcome_scope.h:41` (rule 5)
    - `club_distances.h:67` (`kClubBaselineSchemaVersion` 4)
  - `kThemeRuleVersion` was not bumped (`swing_themes.h:129`, still 3). Themes are re-derived only because the themes
    fingerprint hashes the work-on fingerprints, which hash `kWorkOnRuleVersion` (`work_ons_controller.cpp:482`, `:1490-1493`).
  - `tendencies.h:71` is one 1,250-character line holding the changelog `2: … 3: … 4: … 9: … 10: …`. Entries 5–8 are missing.
  - `work_ons.h:84-93` lists 2, 3, 4 and 6. Rule 5 was bumped in 398a6c48 with no entry.
  - "Schema" and "rule" mean opposite things in different files:
    - `work_ons.h:75-76` — `Two numbers because they change for different reasons: a new field is a new schema, a new ordering is a new rule.`
    - `club_distances.h:67` — `kClubBaselineSchemaVersion = 4; … 2: strikes read from the flight leave the pool; 3: every shot counts; 4: misses off the flight-checked rows`. These are all rule changes.
    - `tendencies.h:72` `kTendencySchemaVersion = 1`, although rules 12, 15 and 17 changed the stored shape (three factors, `together`, `flow`).
- What goes wrong:
  - Whether a cache is stale depends on someone remembering every downstream constant. The themes cache stays correct
    only through an indirect hash dependency nobody declared. A change that only bumps `kThemeRuleVersion`, or a
    future cache that does not hash the work-on fingerprint, will silently serve stale output.
  - The missing entries mean the comment can no longer be trusted as the history. The git log is the real record.
- Fix:
  - Derive cache keys from declared dependencies: each reduction declares its rule version and the versions of what it
    reads (ledger rule, flight rule, norms hash), and the key is their hash.
  - Use one convention for schema versus rule.
  - Move the changelogs into a `docs/…/versions.md` table or the commit history, so the code keeps one line per
    current meaning.
- Effort: M        - Related: C-02, C-03

### C-06 · Five separate projections of the pack's condition kind and hold rule; swing themes disagrees with work-ons on held and Setup conditions
- Category: 5 Duplication (parallel tables/enums), 4 One source      - Severity: High
- Evidence:
  - Five mappings of `ConditionKind`:
    - `work_ons_pack.h:51-55` — `if (!c.reportHold.isEmpty()) return WorkOnClass::Excluded; … case ConditionKind::Setup: return WorkOnClass::Movement;`
    - `session_report.h:154-165` (`FactKind`, `r.held = !c.reportHold.isEmpty()`)
    - `swing_areas_pack.h:68-69` (`workOnClassFor`, `!c.reportHold.isEmpty()`)
    - `shot_outcomes_pack.h:34-36` (`ShotConditionLayer`; `Setup` falls to `Root`)
    - `src/Diagnostics/packs/swing_themes_pack.h:156` — `info.fault = c.kind == ConditionKind::Fault;`. This one has no `reportHold` test and excludes Setup.
  - `src/Resources/diagnostics/core.json`: `chicken_wing` is kind `fault` with a `reportHold` (lead arm after impact not measured reliably).
- What goes wrong: **suspected, not reproduced.** Take a golfer whose lead arm fails on most swings. chicken_wing
  stays off the Work-ons list (held). But swing themes counts it as a needs-work fault, so it can appear among Home →
  Your swing "needs work" and "next on your list" cards. Meanwhile a Setup habit (ball position, stance) can be a
  Work-on but never Your focus or "what you do well". Two athlete-level surfaces then disagree about what to work on.
  Every new consumer must also remember the hold rule; two already have not.
- Fix: one marshalled `ConditionRole {listable, headlinable, held, family, layer}`, computed once from the pack
  (beside `workOnClassFor`) and read by all five. Swing themes should take `listable && !held`, unless the owner
  decides otherwise (see Questions).
- Effort: S        - Related: C-01

### C-07 · Domain headers do file I/O, and a hidden singleton reaches reductions through default arguments and fails silently
- Category: 3 Layering (I/O in domain), 11 Errors, 12 Testability, 8 State      - Severity: Medium
- Evidence:
  - `strike_from_flight.h:441` `qgetenv("PINPOINT_CORE_FLIGHT_NORMS")`
  - `:448-449` `QFile f(path); if (!f.open(QIODevice::ReadOnly)) {`
  - `:464-467` — `inline const FlightNorms &shippedFlightNorms() { static const FlightNorms k = loadFlightNorms(flightNormsPath());`. No error is captured.
  - The singleton as default arguments: `club_distances.h:250` `const FlightNorms &norms = shippedFlightNorms()`, and likewise `:357`, `:435`, `:677`.
  - `club_distances.h:485-487`: `clubBaselineIsOutdated` opens and parses a file.
  - An empty table overrules every fired sky row:
    - `strike_from_flight.h:574`: `flightSaysSky` returns false
    - `:393`: `if (r.category.isEmpty()) return std::nullopt;`
  - The Gui works around this by warming the singleton: `work_ons_controller.cpp:1563` and `:1996` `(void)shippedFlightNorms();`
  - `src/Analysis/tests/club_distances_test.cpp:116` calls `poolSessionByClub(s)` (the default norms). That test target is not given `pp_norm_env` (`src/Analysis/tests/CMakeLists.txt:159`) and has no Qt resources.
  - The brief's premise needs one correction: `shot_factors.h:54-58` includes `<QFile>`, `<QJsonDocument>`, `<QJsonArray>` and `<QJsonObject>` but uses none of them. They are leftovers from when the flight table lived there (`:43`).
- What goes wrong:
  - If the resource is missing or the override path is wrong, the app does not say so. Every flight is "not read",
    no Sky is ever called, and every measured sky row is turned NotAssessable with "Sky is judged from the ball flight".
  - The empty table is cached for the life of the process.
  - From reading the build, club_distances_test runs against an empty table, so its Sky path is never exercised. I
    did not run it.
  - A domain function's result depends on the environment and on whatever the first caller triggered.
- Fix:
  - Load the flight table at the composition root (where the pack and norms are loaded), and fail loudly on an empty
    or invalid table.
  - Pass `const FlightNorms&` explicitly. Remove the default arguments and `flightNormsPath()` from Coaching.
  - Move the baseline file probe into the Gui store layer.
  - Remove the dead includes.
- Effort: S        - Related: C-15

### C-08 · Upward and sideways dependencies: Coaching → Gui, Diagnostics → Coaching, and view code living in Coaching
- Category: 3 Layering      - Severity: Medium
- Evidence:
  - `session_report.h:61` `#include "../Gui/diagnostics/session_spread.h"`. That header includes `../typeset/box_stats.h` (`session_spread.h:23`).
  - `session_spread.h:391-396`. The formatter has already moved to Core and survives here as a shim: `it is the app's one formatter (Core/figure_format.h) … Kept under these names for Coaching/session_report.h.`
  - `src/Diagnostics/packs/swing_themes_pack.h` includes `src/Coaching/swing_themes.h` (`include_graph.md` line 11). The other three marshallers live in Coaching.
  - `consistency_home.h:35` `namespace pinpoint::charts {`: the Charts consistency table's columns live in Coaching.
  - Chart layout and QML contracts in Coaching:
    - `tendency_flow_map.h:33-34` `inline QVariantMap tendencyFlowMap(... double plotHeight, double gap)`
    - `sequence_checks.h:340` `inline QVariantMap sequenceChecksToVariant`
    - `swing_themes.h:2193-2437` `SwingSummaryView`
- What goes wrong:
  - Coaching cannot be built or reasoned about without Gui headers.
  - The directory cycle Coaching ↔ Diagnostics/packs ↔ Gui (include_graph "Directory cycles") follows from these
    edges.
  - QVariantMap shapes are documented only in comments inside the domain (`sequence_checks.h:336-337`), so QML-facing
    contracts change whenever domain code changes.
- Fix:
  - Move `SpreadCorridor`/`passBandOf` arithmetic into `Diagnostics/norms` (they are norm arithmetic) and call
    `pinpoint::figures::fig` directly. Delete the shim names.
  - Move `swing_themes_pack.h` into Coaching beside its siblings.
  - Move `consistency_home.h`, `tendency_flow_map.h`, the `*ToVariant` builders and `swingSummaryView` into Gui
    presenters.
- Effort: S–M        - Related: C-04

### C-09 · God functions and multi-responsibility files
- Category: 6 Size and cohesion      - Severity: Medium
- Evidence:
  - `shot_outcomes.h:557-946` `composeShotOutcome` is **390 lines**. It does excludes, corrections, intent, inferred
    strike, the implied walk, absorption, driver scoring, link words, carry, phrase, story and the log cell.
  - `tendencies.h:763-1200` `reduceTendencies` is **438 lines**. It does judging, the club tally, per-key factor
    usuals, the building floor, trend, chain, together, flow, on-ball tallies and sentences.
  - `swing_themes.h:1676-1907` `reduceLayers` is 232 lines.
  - `work_ons.h:485` `reduceSessionWorkOns` is 177 lines.
  - `swing_themes.h` (2,681 lines) holds a Householder/QL eigensolver, Jacobi SVD, Hungarian matching and EM
    imputation (`:490-1191`), the athlete reduction, a view model (`:2193`) and persistence (`:2475`).
  - `diagnostic_ledger.h` (2,550 lines) holds statistics primitives, the row model, session reductions, chain
    extraction, anti-churn ranking and persistence.
- What goes wrong:
  - Every rule change in the composer is made inside one 390-line function, and the comments mark its steps
    ("Step 1", "Steps 3–5").
  - The linear-algebra kernel is in a coaching header, so a numerics fix and a wording fix land in the same file and
    invalidate the same TUs.
- Fix:
  - Split `composeShotOutcome` along its own step comments: `resolveLabels`, `impliedSet`, `chooseDriver`, `linkWords`,
    `phrase`.
  - Split `reduceTendencies` into `judgeShots`, `factorUsual(key, factor)`, `onBall` and a words pass (C-04).
  - Move `themes::` numerics into `src/Analysis/linalg_small.h`.
  - Split the ledger into `ledger_stats.h` / `ledger_rows.h` (+ JSON) / `ledger_reduce.h` / `ledger_chains.h`.
- Effort: M

### C-10 · The same rule or id list in several places; one clone is already test-only
- Category: 5 Duplication      - Severity: Medium
- Evidence:
  - The combined-miss halves rule appears four times:
    - `shot_factors.h:83-84` `(other.id == QLatin1String("pull_hook") && (l.id == QLatin1String("pull") || …`
    - `shot_factors.h:98-99` (`tendencyMissHalves`)
    - `shot_outcomes.h:692-699`
    - `shot_outcomes.h:713-714`
  - Lists of strike-miss ids:
    - `shot_outcomes.h:587-588` (`thin`/`top`/`chunk`)
    - `strike_from_flight.h:570-571` (same three)
    - `club_distances.h:159-164` `isClubDistanceMishit` (adds `sky`, `shank`)
    - `club_distances.h:212-222` `ClubMishits::add`
    - `shot_factors.h:171-178` (the factor table)
    - `tendencies.h:451-464` (JSON keys and words)
  - Largest-remainder percentages, cloned:
    - `shot_factors.h:414-424`
    - `shot_outcomes.h:1035-1045` (`composeShotMixBar`). Its only callers are tests: `grep composeShotMixBar` finds `src/Analysis/tests/tendencies_test.cpp` only.
  - Four medians:
    - `diagnostic_ledger.h:292` `medianOf` (nth_element)
    - `strike_from_flight.h:86` `flightMedian` (sort, with a minimum count)
    - `swing_themes.h:412` `nanMedian`
    - `swing_themes.h:422` `medianPropagating`
- What goes wrong:
  - Adding a combined miss (say a "pull slice"), or moving a strike id between factors (Sky moved out of strike on
    10 Nov, `shot_outcomes.h:615`), means finding every list by hand.
  - `isClubDistanceMishit` still treats `sky` as a strike mishit. That may be deliberate (it keeps the usual carry
    clean), but nothing ties it to the factor table.
- Fix:
  - Put `halves` and `family` on the pack facts (`ShotConditionFacts`) or on `shotFactorValueTable`, and derive every
    list from that table.
  - Delete `composeShotMixBar`/`ShotMixTally` and keep one `largestRemainderPercents(counts, total)`.
  - Use one median helper with an explicit NaN policy.
- Effort: S        - Related: C-15

### C-11 · Identity: one fact under several names, and keys built from words or folder names
- Category: 2 Identity      - Severity: Medium
- Evidence:
  - Three spellings for each launch-monitor reading:
    - the `ShotRecord` field (`diagnostic_ledger.h:749` `carry`)
    - the measure id (`:783` `{ "m_carryDistance", &ShotRecord::carry }`)
    - the LM stats key (`club_distances.h:283` `shot.lm.insert(QStringLiteral("lm.carryDistance"), s.carry);`)
  - The ledger also carries its own copy of the LM readings already in swing.json (`diagnostic_ledger.h:745-762`, schema 11–13).
  - Session identity and order come from the folder name: `club_distances.h:94-100` `sessionDayOfFolder` (regex on `QFileInfo(...).fileName()`); `tendencies.h:1340-1346` orders same-day sessions by `folder < asOfFolder`.
  - Two ids per condition:
    - the row id and the left-hander `displayId` (`shot_outcomes.h:107-108`)
    - stored filter keys use displayIds (`outcome_scope.h:13-14`)
    - tendencies un-gloss with the newest shot's handedness (`tendencies.h:913-914`, `:1009`)
  - Club taxonomies, at least five:
    - `Core/club_vocabulary.h`
    - `contextIdForClub` (word match)
    - `tendencyIronGroup` (`tendencies.h:227-237`, by number)
    - flight_norms.json categories (`fairway_hybrid` vs tendencies' `woods`)
    - `isSkyClub` (`strike_from_flight.h:474-478`, substring)
- What goes wrong:
  - A renamed measure or stats key breaks one path silently.
  - The ledger's LM copy can disagree with swing.json until a regrade.
  - Renaming a session folder changes its day and its order in the golfer's history.
  - A pooled key with mixed handedness is un-glossed with one hand.
  - A club added to the vocabulary must be added to several classifiers.
- Fix:
  - Use one `LmField` enum with a table that gives the field pointer, the measure id and the stats key, and derive
    the other two names from it.
  - Store a session's day as data in the session document, falling back to the folder name.
  - Carry the row id everywhere and gloss at display only.
  - Use one club-category table (flight_norms.json already has one), read by all classifiers.
- Effort: M        - Related: C-04

### C-12 · Thresholds copied as literals, or reused for an unrelated meaning
- Category: 10 Configuration vs code      - Severity: Medium
- Evidence:
  - Literal copies:
    - `tendencies.h:212-213` — `The bands from half up use themeFrequency's thresholds and its words.` Then it hard-codes `0.995`, `0.9`, `0.7`, `0.5` (`:216-219`), while swing themes injects these (`swing_themes.h:211-213` `everySwing`, `almostEvery`, `mostSwings`).
    - `swing_areas.h:126` `kAreaHabitShare = 0.5; // "more than half your swings" (Coaching's presentShare)`, a literal copy of `ThemeOptions::presentShare` (`swing_themes.h:192`), described at `:28-29` as `so the two tabs cannot disagree`.
  - A threshold reused for another meaning: `outcome_scope.h:216` `if (c.n0 < opt.minSessionShots) return counts + " — too few other shots to compare"`. `TendencyOptions::minSessionShots` is "sessions with ≥ 8 shots" (`tendencies.h:77`).
- What goes wrong:
  - If a tuning sweep moves `ThemeOptions::mostSwings` or `presentShare`, Tendencies and Your swing keep the old
    band, and the "cannot disagree" claim becomes false without any code being touched.
  - Tuning the tendency session floor also changes the shot-filter contrast floor.
- Fix: put each shared band in one place (a `CoachingBands` struct or `tuned::` namespace) and reference it everywhere.
  Give the contrast floor its own named option.
- Effort: S        - Related: C-10

### C-13 · Mixed "missing" conventions, in persisted data and in golfer text
- Category: 11 Errors and missing values      - Severity: Medium
- Evidence:
  - Two glyphs for "nothing":
    - em dash: `club_distances.h:115` `if (!std::isfinite(mean)) return QStringLiteral("—");`, also `:123`, `lm_session_reductions.h:224` and `sequence_checks.h:192`
    - hyphen: `tendencies.h:928` `tf.label = QStringLiteral("-");`, and `session_report.h:499` (`"-" for none`, via `figure_format.h`)
  - Two app-wide formatters: `lmFormat` (club distances, the carry waterfall at `shot_outcomes.h:464`) and `pinpoint::figures::fig`, which `session_spread.h:392` calls "the app's one formatter".
  - Sentinels in the row model:
    - `pFire = -1.f` "not formed" (`diagnostic_ledger.h:625`)
    - NaN for LM readings
    - `firedSignalCount 0` means both "unknown" and "did not fire" (`:638-640`)
  - The writer guards some numbers and not others:
    - guarded: `:2167-2168` `JSON has no spelling for NaN, and a null read back as 0 would put a reading in the ledger nobody measured`
    - unguarded: `:2254-2257` `ro["value"] = r.value; … ro["z"] = r.z;`
    - read back with `toDouble()` (default 0) at `:2438-2441`
- What goes wrong:
  - The same "no number" shows as "—" in Your distances and as "-" in Tendencies and the report, against the owner's
    rule ("-" is NA).
  - A NaN `z` or `value` on an assessable row would come back as 0 after a save and reload. Live and review would
    then differ in `zRun` and the trend. The engine currently sets z = 0 rather than NaN for no scale
    (`characteristic_engine.h:286-288`), so this is latent, not observed.
- Fix:
  - Route every golfer-facing number through `figures::fig` (one glyph).
  - Write every double through the same `putFinite` helper and read it back through `num()`.
  - Make `pFire` a `std::optional<float>` in memory.
- Effort: S

### C-14 · Coaching shares `pinpoint::analysis` with eleven directories and uses generic names, the condition behind the 7 Oct ODR incident
- Category: 15 Names and namespaces      - Severity: Medium
- Evidence:
  - `namespace pinpoint::analysis` is declared in headers under Club (38), Analysis (21), Coaching (20), Wrist (17),
    Body (14), Metrics (13), Diagnostics (35), Timeline (6) and Pose (6). This is my count of `^namespace
    pinpoint::analysis` per directory.
  - Coaching uses generic names in it:
    - `toJson` (three overloads: `diagnostic_ledger.h:2241`, `work_ons.h:919`, `swing_themes.h:2475`)
    - `fromJson` (`diagnostic_ledger.h:2396`)
    - `enum class Tier`, `Stage`, `Trend`
    - `struct Chain`, `Coverage`, `Bookends`, `NodeSpec`, `EdgeSpec`
    - `rowFor`, `ledgerFor`, `medianOf`
  - Shot-level helpers named after an athlete-level consumer: `shot_factors.h:71` `(moved here from tendencies.h, unchanged)`. They kept the names `tendencyMissLabels`, `tendencyMissHalves`, `tendencySameMiss`, `tendencyShotMisses`.
- What goes wrong:
  - Tests compile Coaching headers in subsets, without the app's other pinpoint::analysis headers. A same-named
    inline struct or function with a different body links silently, and only in the app. That is how the
    `DagEdge` incident happened (owner's memory note, 7 Oct).
  - I found no collision at HEAD. Struct names were checked against all of `src`; local structs such as `Fault` and
    `Group` are function-scoped.
- Fix: put Coaching in `pinpoint::coaching` (sub-namespaces `ledger`, `outcomes`, `athlete`), and rename the
  `tendency*` shot helpers to `shotMiss*`.
- Effort: M (mechanical, wide)

### C-15 · Dead code kept alive only by its tests
- Category: 14 Dead and leftover code      - Severity: Low
- Evidence:
  - `dashboard_reductions.h:28` — `No QML façade calls it today (the corridor bar and ChartMetrics::barDomain were removed).` It has no app includer, only `dashboard_reductions_test.cpp`.
  - `composeShotMixBar`/`ShotMixTally` (`shot_outcomes.h:1001-1053`): tests only.
  - `tendencyMissLabels`, `tendencySameMiss` (`shot_factors.h:76`, `:104`): tests only.
  - `patternSetStableShots` (`diagnostic_ledger.h:2053`): tests only, superseded by `patternSetStableAtLeast`.
  - `swingSummaryLines` (`swing_themes.h:2060`, `the older form; kept for the probe`): only the tests and `tools/themes/theme_summary.py` refer to it.
  - `consistency_home.h:24-25` — `the old per-session <session>/consistency.json files stay in the libraries, unread.`
- What goes wrong: about 250 lines of reductions that look live. A maintainer can change them, or worse, keep the
  copies in C-10 in step with them, for no user.
- Fix: delete them with their tests. If `swingSummaryLines` is the Python-parity oracle, move it into the golden test.
- Effort: S        - Related: C-10

### C-16 · `gradedReadings` returns a reference into a caller-supplied scratch copy of the whole shot
- Category: 7 Contracts, 12 Testability      - Severity: Low
- Evidence:
  - `diagnostic_ledger.h:806-813` — `inline const ShotRecord &gradedReadings(const ShotRecord &s, ShotRecord &scratch) { if (s.ungradedReadings.isEmpty()) return s; scratch = s; …`
  - `shot_factors.h:290-291` — `ShotRecord scratch; bool strikeAssessed = std::isfinite(gradedReadings(shot, scratch).smash)`
  - It is called at least nine times per shot across `strike_from_flight.h` (`:102`, `:140`, `:182`, `:390`, `:486`), `tendencies.h:206`, `club_distances.h:179` and `shot_outcomes.h:455`.
- What goes wrong: when a reading is held, each call deep-copies every row of the shot to hide up to 14 doubles. The
  copy happens several times per shot per surface. The API also makes it easy to return a dangling reference if
  someone stores the result past the scratch's scope.
- Fix: return a small value type (`GradedReadings {double carry, …}` built from `shotReadingFields()`). The judging
  functions then take that instead of a `ShotRecord`.
- Effort: S

## 4. Done well

- **The ledger's statistical core.**
  - `diagnostic_ledger.h:97-264`: every gate is a named constant fed in through `LedgerOptions`. The gates carry
    `PP_GATE` tags (`:979-980`, `:1241`, `:1323`).
  - Rule 1 (NotAssessable is never clean) holds in every denominator (`:1120`, `:1239`, `:1537`).
  - The draws are deterministic Monte Carlo, seeded by `DetRng`, so live and replay agree (`:1140-1142`).
  - Ledgers keep first-seen order and never iterate a QHash (`:1209-1210`).
  - The functions are pure and tested without Qt GUI. Keep this as the model for the rest of the layer.
- **Schema handling in the ledger reader.** Each schema bump is explained field by field (`:178-238`). Absent numbers
  read back as NaN, never 0 (`:2499-2503`), and absent enums read back as their honest default (`:447`, `:487`).
- **The pack marshallers.** `work_ons.h:145-146`, `shot_outcomes.h:29-30`, `swing_areas.h:48-49` and the `*_pack.h`
  files keep the pack format out of the arithmetic, so each reduction can be tested on hand-built facts. The C-06
  problem is that the projections are not shared, not that the pattern is wrong.
- **Context flows down explicitly.** `ShotOutcomeSessionFacts` (`shot_outcomes.h:94-99`) passes session context into
  the shot composer one way, as a value, and states the invariant (`:25-27`: "Nothing in this file can raise a word
  above what the session has earned").
- **One reading table for producer and hider.** `shotReadingFields()` (`diagnostic_ledger.h:774-799`) is shared by
  `detectShot` and `gradedReadings`, so they cannot name different measures.
- **Versioned readers refuse foreign files.** `sessionWorkOnsFromJson` (`work_ons.h:998-1000`), `tendencySetFromJson`
  (`tendencies.h:1365-1367`), `clubBaselineFromJson` (`club_distances.h:495-498`) and `swingThemesFromJson` all reject
  another version rather than misreading it. The ledger reader is the one exception (C-03).
- **Swing themes reproducibility.** Replicates are seeded per index, cancellation tokens are passed in, and a golden
  test holds it to the Python reference (`swing_themes.h:76-80`, `:100-101`).
- **Compile cost is modest.** My transitive count gives no Coaching header more than 10 app translation units, so
  header-only is not a build-time problem.

## 5. June review status

`docs/implementation/code_quality_review.md` (20 Jun, ~56k LOC) has no `src/Coaching` section and does not name any
Coaching file. The layer did not exist in its present form then. So there is no item to mark fixed, open or worse. On
the June themes as they now apply here:

- **Theme D (duplicated numeric helpers): present in Coaching.** There are four medians in Coaching alone (C-10), and
  more under the same namespace elsewhere (`Club/shaft_direction.h:239`, `Body/trunk_sensor_series.h:346`, and
  anonymous copies in `Body/lower_body_metrics.cpp:51` and `Body/foot_metrics.cpp:63`).
- **Theme I (dead surface presented as live): present** (C-15).
- **Theme K (magic numbers): largely avoided.** The ledger and work-ons inject everything. The remaining problem is
  literal copies of injected values (C-12).
- **The June conclusion does not hold for this area.** June said the debt was "mechanical, not architectural". Here
  the main faults are architectural: level ownership (C-01), persisted reductions (C-02) and presentation in the
  domain (C-04).

## 6. Questions I could not settle from the code

1. **Setup and held conditions on Home.** Is it intended that Home → Your swing (swing themes) includes `reportHold`
   conditions such as chicken_wing, and excludes `Setup` conditions, while Work-ons does the opposite (C-06)? The
   owner's rule "only reportHold keeps a condition out of reports" covers reports. Does it cover Your swing?
2. **Which gates do old sessions use?** Should an old session be re-reduced with today's `LedgerOptions` or with the
   ones stored in its file? The comment says both (C-03).
3. **Does the catch-up always finish first?** Does the work-ons catch-up rewrite every diagnostics.json below
   `kDiagSchemaVersion` before the tendencies, consistency-marks and Charts jobs read it? If it does, C-03's scenario
   is only a window. If it does not, Home can count unchecked strikes. The guarantee lives in WorkOnsController, not
   in Coaching.
4. **Should tendencies.json hold words?** Is it meant as a rendered cache (sentences included) or as data? The report
   reads it (`tendency_flow_map.h:20`). If the report must reproduce exactly what Home showed, the words may be there
   on purpose. If so, that should be stated and stamped separately from the rule (C-04, C-05).
5. **Is isClubDistanceMishit's `sky` deliberate?** Should `sky` stay a strike mishit for the usual carry now that Sky
   is a flight fault in the three factors (C-10)?
