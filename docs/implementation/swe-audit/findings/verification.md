# Verification record

For each reviewer's report, this file records:
- what the lead checked by reading the code at `37289e33`;
- what `tools/check_citations.py` reported;
- the verdict on every Critical and High finding;
- the adversarial pass's result.

## Verdicts

| Verdict | Meaning |
|---|---|
| **Confirmed** | The cited code shows the claim. |
| **Confirmed (suspected runtime)** | The structure is shown, but the failure has not been reproduced. |
| **Revised** | True, but severity or wording changed; the reason is given. |
| **Rejected** | Not supported, with the reason. |

## H · Persistence (14 findings: 4 High, 6 Medium, 4 Low)

**Citation check:** 161 citations resolved. Of those, 27 have their quote matched and 134 have their line in range.

**Flagged by the checker and accepted:**
- **Paraphrased quotes.** Seven quotes are paraphrased: `QStringLiteral(...)` is shortened to `"..."`. All seven were read by the lead and say what they claim.
- **Shorthand line numbers.** Seven line numbers continue the wrong file in the reviewer's shorthand. In H-03, `:3084-3105` is in `session_diagnostics_model.cpp`, not `.h`. In H-04, `:3093` is in the same `.cpp`. In H-11, `:1640`, `:3195`, `:1533`, `:2992` and `:3240` are in `session_diagnostics_model.cpp`. The lead checked each against that `.cpp`.
- **One placeholder.** The checker flagged `file.cpp`: it is libppswing's `src/file.cpp`, in another repo.

| Finding | Lead's check | Verdict |
|---|---|---|
| H-01 · four `analysis` converters, drifted | `src/Gui/shot/disk_replay_source.cpp:344` reads `an["score"].toInt()`. `src/Export/swing_doc.cpp:350` writes `serializeScore(a.score)`, an object. So `overall` is always 0 on the replay path. The four converters exist at the cited lines. | Confirmed. Latent: no reader of the differing keys was found. |
| H-02 · header built twice; `athlete.heightM` only on one path | `src/Export/swing_exporter.cpp:922-926` writes name, uuid and handedness only. `src/Gui/shot/shot_processor.cpp:2097-2100` adds `heightM` on the analysis-only path only. `src/Analysis/swing_reanalyzer.cpp:788` reads it. Live uses the profile (`shot_processor.cpp:1267`). | Confirmed (suspected runtime): a re-analysed camera swing gets height 0. |
| H-03 · several writers of `diagnostics.json` | `src/Gui/shot/PpSessionDiagnosticsWindow.qml:29-36` states two models ingest independently. `session_diagnostics_model.cpp:3116-3131` merges three keys, and only under `Always` authority. `tools/diagnostics/regrade_ledger.cpp:95-99` builds a third. | Confirmed. The lost updates are suspected runtime. |
| H-04 · user input in the same file as the cache | `tools/diagnostics/regrade_ledger.cpp:87` renames the ledger aside, then `activateSession` rebuilds it. `session_diagnostics_model.cpp:1465` parses with no error check. | Confirmed. Whether input has actually been lost depends on whether `regrade_ledger` was run over library sessions that carried intents or corrections. The 10 Nov regrade of 4 Aug and 20 Oct suggests it may have; that is a question for Mark. |
| H-05 · `atomicWrite` not atomic | `src/Diagnostics/engine/pack_io.cpp:142` uses a fixed `.tmp` and no fsync; `:165-166` is remove-then-rename. | Confirmed. Medium stands. |

Medium and Low findings: the citations resolve, and the lead spot-read H-07, H-10 and H-11. They are accepted
as written, pending the adversarial pass on the High findings only.

## D · Gui/diagnostics (20 findings: 5 High, the rest Medium or Low)

**Citation check:** 136 citations resolved. Of those, 16 have their quote matched and 120 have their line in range.

**Flagged by the checker and accepted:**
- **Long quotes.** Seven are long quotes that run across several lines; the lead read each one at the cited line.
- **Shorthand line numbers.** Ten line numbers continue the wrong file in the reviewer's shorthand. For example, `:771` and `:791` after `PpChartsBody` are `PpShotCarousel.qml`. `:572` is `session_report_model.cpp`, and `:3195-3245` and `:2375-2386` are `session_diagnostics_model.cpp`. The lead checked these against the intended file.

| Finding | Lead's check | Verdict |
|---|---|---|
| D-01 · two live instances each write the whole envelope | `PpSessionDiagnosticsWindow.qml:29-36` (two models by design). `PpSessionDiagnosticsPanel.qml:323-327`: every panel ingests. The merge-before-write applies only to `Always` (`session_diagnostics_model.cpp:3116-3131`). | Confirmed (suspected runtime). Merged with H-03. |
| D-02 · diagnostics exist only while a view does | `session_diagnostics_model.h:65-71` ("A golfer who never opens the panel should not be paying"). Ingest happens only in the panel (`PpSessionDiagnosticsPanel.qml:325`), and the catch-up is paused during a session (`main.cpp:383-385`). | Confirmed. **This is the owner's original fault, still present at HEAD.** |
| D-03 · one class, four roles by flags | `src/Gui/report/session_report_model.cpp:298-311` is the fourth instance (reviewing, persist off). The authority and persist flags are as cited. | Confirmed. |
| D-04 · nullable last-wins seam | `SessionMode.qml:54`; `PpSessionDiagnosticsPanel.qml:225-230`, `:241-243`. | Confirmed. The null-seam window after a "window" cast closes is suspected runtime. |
| D-05 · all three levels in one class | Size and function lengths match `data/size_table.md`. `setSelectedShotId` → `rebuild()` (`session_diagnostics_model.cpp:767-773`). Session arithmetic is done in the class (reasons trend ±0.15, `:1925-1932`), against its header's claim. | Confirmed. |

**Corrections D made to `01-information-model.md`** (items 1, 2, 3, 4, 5, 6, 8) were checked and folded in.
The lead verified items 1, 5 and 8 against the code.

## C · Coaching (16 findings: 5 High, 9 Medium, 2 Low)

**Citation check:** 205 citations resolved. Of those, 56 have their quote matched and 134 have their line in range.

**Flagged by the checker and accepted:**
- **Multi-line quotes.** Fifteen are quotes that run across lines or join two places with "…". The lead read
  `diagnostic_ledger.h:2141`, `strike_from_flight.h:521-538` and `tendencies.h:26` and they say what is claimed.
- **Shorthand line numbers.** Five line numbers continue the wrong file in the reviewer's shorthand. For
  example, `work_ons.h:490-1191` belongs to the previous `.cpp`.

| Finding | Lead's check | Verdict |
|---|---|---|
| C-01 · no owner per level; shot outcomes recomposed per consumer | `tendencies.h:26-30`: "Every shot is RE-COMPOSED here from its ledger row … the same composer as the panel". | Confirmed. The same point as G's facts matrix, seen from the domain side. |
| C-02 · a session-level verdict persisted into shot rows | `diagnostic_ledger.h:2141-2145` ("WHAT IS PERSISTED IS THE ROWS, NOT THE REDUCTIONS … writing them out would be writing a cache"). Against it, `strike_from_flight.h:521-538`: `checkStrikesAgainstFlight` rewrites rows from the session's usuals "so diagnostics.json holds the checked rows". | Confirmed as a structural contradiction. It was a deliberate design choice by Mark (10 Nov, commit `770b9fce`), so the remedy is a design question: keep the check as a session-level overlay on unchanged rows. It does not mean the verdict is wrong. |
| C-04 · golfer English built and persisted in the domain | Not re-read by the lead beyond the cited headers; the June review had no Coaching coverage. | Accepted, pending the adversarial pass. |
| C-05 · versions bumped by hand; changelogs lost entries | `tendencies.h:71` carries 17 rule steps in one comment. | Accepted, pending the adversarial pass. |
| C-06 · five projections of condition kind/hold; swing themes ignores `reportHold` | `src/Diagnostics/packs/swing_themes_pack.h:156` has `info.fault = c.kind == ConditionKind::Fault`, and there is no `reportHold` anywhere in `swing_themes_pack.h` or `swing_themes.h`. `work_ons_pack.h:51-55` excludes held and includes Setup. `core.json` holds `chicken_wing` (kind fault) with a `reportHold`. | Confirmed in structure. The visible disagreement is suspected runtime. Whether Your swing *should* show held conditions is Mark's call. |

## F · Charts + Report (15 findings: 8 High, 5 Medium, 2 Low)

**Citation check:** 203 citations resolved. Of those, 49 have their quote matched and 143 have their line in range.

**Flagged by the checker and accepted:**
- **Quotes.** Four quotes are paraphrased: `QLatin1Char('_')` is written as `'_'`, or two places are joined with "…".
- **Shorthand line numbers.** Seven line numbers continue the wrong file in the reviewer's shorthand. For example, `:509-513` belongs to `session_report_model.cpp`, and `:1714-1948` / `:3594-3720` to the earlier-named presenter `.cpp` files.

| Finding | Lead's check | Verdict |
|---|---|---|
| F-01 · one `SessionSeriesModel` per view | Matches `data/seam_inventory.md` (5+ QML instantiations). | Confirmed. |
| F-02 · `SessionSnapshot` is a second diagnostics model | Not re-read beyond the citations. | Accepted, pending the adversarial pass. |
| F-03 · report ball figures ignore LM exclusions and overrides | `session_report_model.cpp:153-166`: `readDocs` reads `launchMonitor`, `athlete`, `clock` and `streams`, never `review`. `lmSeries` (`:857-866`) keeps every finite value. The only `excluded` read is the appendix's, from `LmSessionModel` rows (`:1075`). | Confirmed (suspected wrong data on the PDF). One PDF can show two medians for one club. |
| F-04 · the report grades links by its own rule | The cited rule matches the panel's `gradeLink` as quoted. | Accepted, pending the adversarial pass. |
| F-05 · a shot's direction is named by two rules | Cited code as quoted. Which rule should win is Mark's call. | Confirmed in structure. |
| F-06 · session order from folder names | `session_report_model.cpp:1244-1249` lists siblings by name prefix in `QDir::Name` order. `src/Export/swing_paths.cpp:178` uses date_name_type, with the type before the counter. | Confirmed (suspected wrong words on the report on a mixed-type day). |
| F-07 · three shot numberings shown side by side | `session_series_model.cpp:856` (`ordinal = snap->count() + 1`). `charts_delivery_presenter.cpp:618` ("Shot %1 of %2"). The carousel numbers by document index (`shot_list_model.cpp:132-142`). | Confirmed. After a deleted or unanalysed swing, Charts' "Shot 6" is the carousel's "Shot 7". |
| F-08 · six presenters clone one scaffold | Matches `data/cpp_duplicates.md` (headers 83–93 % alike; worker blocks cloned). | Confirmed. |

## B · Diagnostics (22 findings: 5 High, 10 Medium, 7 Low)

**Citation check:** 189 citations resolved. Of those, 67 have their quote matched and 92 have their line in range.

**Flagged by the checker and accepted:** 26 quotes are multi-line comments or "…"-joined. The lead read `norm_provider.h:258`, `live_measure_source.h:148-150` and `consistency_env.h:52-56`.

| Finding | Lead's check | Verdict |
|---|---|---|
| B-01 · a saved corridor edit never reaches the app | The only call is `resetSharedNormProvider()` at `src/Gui/diagnosticmodel/model_browser.cpp:1023`, inside `ModelBrowser::refresh()`. No QML or C++ calls `refresh()`. `save()` (`:5149`) resets screens, drills and gate words (`:5196-5213`) but not norms. | Confirmed (suspected runtime: an edit applies only after a relaunch). |
| B-02 · ≥10 independent pack assemblies | Consistent with D-02 (a fresh pack parse per model build, `session_diagnostics_model.cpp:702`) and the private providers in `WorkOnsController` and `main.cpp:442-446`. | Confirmed. |
| B-03 · unsynchronised lazy singletons touched from workers | `consistency_env.h:52-56` admits `sharedNormProvider()`'s first build "is unsafe against a first build on another thread". | Confirmed (suspected race). |
| B-05 · norm standing encoded in prose | Accepted on the reviewer's evidence. It also bears on `docs/validation/release_status.md`, which Mark relies on. | Accepted, pending the adversarial pass. |
| B-07 · "club assumed" demotion never runs | `measure_sample.cpp:1000` uses `swingDocDeclaredClub` (no stub). `context_tree.cpp:278` maps an empty club to the default context, so the context id is never empty. `session_diagnostics_model.cpp:1032-1038` hard-codes `contextInferred = false` on a premise (a DRIVER stub) that no longer holds. | **Confirmed. A behavioural defect, deterministic from the code:** a shot with no club is graded at full confidence, while the in-app Gates text says it is "trusted less". A candidate gh issue. |

## E · shot / session / selection (17 findings)

**Citation check:** 236 citations resolved. Of those, 34 have their quote matched and 192 have their line in range. Nine quotes are paraphrased; one line number is shorthand.

| Finding | Lead's check | Verdict |
|---|---|---|
| E-01 · a running session can be split across two athletes | `ScreenAthletePicker.qml:33-35`: the only guard is in QML ("this is the one guard"). The "＋ New athlete" button (`:216-218`) is not gated. `athlete_controller.cpp:385-386` auto-selects a new athlete. `swing_paths.cpp:199-201` re-allocates on a uuid or base (name) change. | Confirmed in structure. **Suspected Critical** (swings saved under the wrong athlete). A probe is needed to reproduce it; this is a candidate gh issue. |
| E-02 · library keyed by sanitised name; rename orphans history | `athlete_controller.cpp:204` says "a rename moves the folder", but no code moves one. The six derivations are as listed. | Confirmed. A rename making the history disappear follows from the code (all readers compute `<root>/<newName>`). Suspected runtime only in the sense that nobody has renamed an athlete yet. |
| E-03 · `currentAthleteChanged` on every record edit | `athlete_controller.cpp:222-228`: `reload()` always emits it. `main.cpp:461-465` reloads the live carousel without a `running` guard. | Confirmed (suspected runtime: an edit mid-session reloads the carousel and drops the selection). |
| E-04 · the selected shot held five times | The holders are as cited. | Confirmed. |
| E-05 · the selection outlives its session; resolved by basename | `session_diagnostics_model.cpp:792-811`: a basename match, then a folder-number match, with no session check. | Confirmed (suspected runtime: a different swing is shown as "This shot"). |
| E-06 · three displayed "Shot N" | Confirmed by F-07 independently. | Confirmed. Merged with F-07. |

E's corrections 1–9 were folded into `01-information-model.md`. The lead verified items 3, 5 and 9 against the code.

## A · Analysis + Metrics (20 findings: 4 High)

**Citation check:** 211 citations resolved. Of those, 28 have their quote matched and 181 have their line in range. One quote is "…"-joined; one line number is shorthand.

| Finding | Lead's check | Verdict |
|---|---|---|
| A-01 · two job builders, already disagreeing | `standardClubLengthM` is set only at `src/Analysis/swing_reanalyzer.cpp:839`. It is read at `analysis_pipeline.cpp:2959-2960` and `:3369-3370`. No assignment exists on the live path in `src/Gui/shot/shot_processor.cpp`. | Confirmed. The input difference is certain; the size of the effect on DtlPosture and the skeleton is unmeasured. Together with H-02 (height), **re-analysis is not reproducing the live analysis**, which matters for the version-gated library re-analysis. |
| A-02 · one writer, four projections | The same as H-01; confirmed there. | Confirmed. Merged with H-01 and E-10. |
| A-03 · two time domains, guessed by magnitude | `src/Gui/shot/disk_replay_source.cpp:71`: `return t >= t0 ? t - t0 : t;`. | Confirmed. The reanalyzer fallback being dead is suspected. |
| A-04 · route/instrument precedence stated four ways | Accepted on the reviewer's evidence. | Accepted, pending the adversarial pass. |

## G · Derived facts across the three levels (14 findings: 5 High, 6 Medium, 3 Low)

**Citation check:** 350 citations resolved. Of those, 31 have their quote matched and 304 have their line in range. The 14 shorthand line numbers continue the wrong file. For example, `:574` after `shot_factors.h` is `strike_from_flight.h`, and `:2146` / `:1851-2009` are `session_diagnostics_model.cpp`.

| Finding | Lead's check | Verdict |
|---|---|---|
| G-01 · LM card / Club delivery classify direction by their own rule | Agrees with F-05, found independently. | Confirmed. Merged with F-05. |
| G-02 · handedness glossed per surface; three miss it | Accepted, pending the adversarial pass. | — |
| G-03 · a shot's outcome and factors have no owner | Agrees with C-01, which reached it independently. `shotOutcome()` returns no factors (`session_diagnostics_model.cpp:2146-2222`; read by the lead earlier in this session). | Confirmed. |
| G-04 · Sky is a factor but not a label | Accepted, pending the adversarial pass. | — |
| G-05 · the per-shot "score" is the wrist estimand; 0 when unscored | `src/Gui/shot/PpShotCard.qml:236-239`: `PpQualityPill` always shown. `PpQualityPill.qml:33,44,60`: `score` defaults to 0, coloured by `qualityColor`. `Theme.qml:680`: `< 25` → `colorError`. `src/Wrist/wrist_resemblance.cpp:94`: no FE channel → 0. | Confirmed in structure. On screen: a red "0" on every card of a non-wrist session, against Mark's rule that a missing value shows "-" and a 0 is a bug. |

## Adversarial pass 1: A, B, C, H High findings (`adversarial-1-ABCH.md`)

**Result:** 18 tested. 12 stand, 3 stand at a lower severity, 2 are partly wrong, 1 stands at a higher severity, none is wrong. The lead accepts the verifier's verdicts:

| Finding | Before → after | Reason |
|---|---|---|
| A-03 · two time domains | High → **Medium** | Documents have used one time base since `717d4273` (7 Jul). The magnitude guesses only serve older files, and their comments are stale. |
| A-04 · route precedence stated four ways | High → **Medium** | No disagreement shown; the passes are tied to the ladder by `metric_catalogue_test.cpp:782/859`. |
| C-02 · session verdict persisted into rows | High → **Medium**, partly wrong | An overruled row keeps its value and the `overruled` flag, and the check undoes itself before re-judging, so nothing measured is lost. What stands: `computeContentStamp` omits the flight norms and the strike thresholds, so a change to them does not trigger a regrade. |
| C-04 · golfer English in the domain | High stays, partly wrong | The claim that a wording edit splits shot groups is false: every reduction recomposes with the current pack. The layering fault stands. |
| C-05 · versions by hand | High → **Medium** | `kThemeRuleVersion` correctly did not need a bump; the missing changelog entries are hygiene. |
| H-04 · golfer input in the cache file | High → **Critical** | **An actual loss, confirmed by the lead.** In `/mnt/swingdata/corpus/swings/2026-07-10_Mark-Liversedge_Wrist_02/`, `diagnostics.json.pre-79ffef1` (7 Aug) holds `intent.focusConditionId = "excessive_shaft_lean"`. The current `diagnostics.json` and every later backup (`pre-batch14`, `aa-sweep`, `pl-sweep`, `pre-6c990076`) have none. This was the only loss across the 100 backups searched. The backup still holds it. |

**Notes kept:**
- **A-01.** The live gap is rarely hit, because default club records carry the standard length. H-02 (height) is the broader re-analysis gap, affecting every re-analysed camera swing.
- **B-05.** Confirmed with a second example: `m_takeawayPlane` is shown as decided although its citation says provisional.
- **A-02 and H-01 are one finding** and are counted once.

## Adversarial pass 2: D, E, F, G High findings (`adversarial-2-DEFG.md`)

**Result:** 30 tested. 20 stand; 10 do not fully stand; none is wrong. The lead accepts the verifier's verdicts:

| Finding | Before → after | Reason |
|---|---|---|
| F-04 · the report grades links by its own rule | High → **Medium**, partly wrong | The report's code-0 comes from `causedBy` (`work_ons.h:linkSupported`), which runs the same Fisher test and caps as `gradeLink` (`diagnostic_ledger.h:1526-1559`), so the two cannot disagree on the same rows. What stands: a hand-written five-way mapping that lacks the panel's screen, coherence and moved-together caps. |
| F-08 · six presenters clone one scaffold | High → **Medium** | The cited drift (reversed snapshot precedence) is test-only; `setSnapshot` has no production caller. The duplication stands. |
| F-06 · session order from folder names | High stays; scenario corrected | Since 27 Oct every session is type Wrist (`session_controller.h:62,85`), so a mixed-type day exists only in older libraries. Still reachable through the `name-date-type` naming setting (`StoragePanel.qml:119`) and through standalone LM shots (`_session_` folders). |
| E-01 · a session split across two athletes | High stays; two sub-claims corrected | **Reachable:** "＋ New athlete" and the avatar edit work mid-session, the rail has no session lock, and nothing stops capture. **Not reachable:** Home's add button, which is the no-athlete empty state. The old ledger first skips the new folder's swings while their numbers collide (`session_diagnostics_model.cpp:846`), then takes later ones into the wrong session. |
| E-10 · three producers of the analysis detail | Stands; one consequence dead | `previousAnalysisDetail` has no caller. One missed live reader: the wrist presenter's off-snapshot pick reads `resemblance`, which the replay detail never carries. |
| D-04 · nullable last-wins seam | Stands; scenario corrected | In "window" mode the seam is nulled only if the cast closes before the next stage rebuild (auto-replay off), not when "the dwell outlasts the replay". |
| G-05 · the score pill shows 0 | Stands, with a carve-out | Nothing hides the pill or shows "-" for 0. The only non-wrist score source is camera pose behind `pose.wristAngles.enabled`, which is off by default. |
| E-02 · rename orphans history | Stands, with a caveat | The "a rename moves the folder" comment probably means the derived path moves, but no code moves a folder. |
| G-04 · Sky is a factor but not a label | Stands, with a caveat | Dropping Sky as a label was a dated design decision. Only the `clean` / "Good shot" result conflicts with the flight factor. |
| F-01 · one `SessionSeriesModel` per view | Stands, with a caveat | Cache eviction needs a third distinct session. |

**Confirmed again:**
- F-03: nothing upstream of `lmSeries` filters `excluded`, and the club comes from `swingDocDeclaredClub`, which never reads `review.clubOverride`.
- E-03 and E-05 stand.
- Merges: F-05 with G-01, and F-07 with E-06.
