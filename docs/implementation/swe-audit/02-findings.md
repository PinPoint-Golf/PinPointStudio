# 02 · Findings

These are the eight reviewers' findings after verification. Each finding below was:
1. checked by the lead against the code at `37289e33`;
2. citation-checked by `tools/check_citations.py`;
3. for Critical and High findings, put through an adversarial pass that tried to disprove it.

That record is in `findings/verification.md`. Full write-ups, with evidence, failure scenario and fix, are in
the reviewers' reports (`findings/A-…H-…md`). This page groups the findings by theme and gives each its final
severity. Where two reviewers found the same thing independently, it is listed once with both ids.

**Totals:** 138 raw findings, about 124 distinct after merging.

| Severity | Distinct findings |
|---|---|
| **Critical** | 1 |
| **High** | 32 |
| Medium | ~63 |
| Low | ~28 |

**Adversarial passes:** none of the 48 Critical and High findings tested was wrong. Seven were lowered to
Medium, and one was raised to Critical.

Severity key:

| Severity | Meaning |
|---|---|
| Critical | Wrong data shown or persisted, or data lost, at HEAD |
| High | A structural violation every change has to work around, or copies that already disagree |
| Medium | Raises the cost or risk of change |
| Low | Hygiene |

A runtime defect is **suspected** unless it was reproduced. The audit ran nothing.

---

## Theme 1 · The information model has no objects: athlete, session, shot and selection are strings, and models belong to views

Domain objects missing, or owned by the wrong thing, and what that costs. See `01-information-model.md`.

| Id | Finding | Sev |
|---|---|---|
| **D-02** | Session and shot diagnostics exist only while a view does. With the panel off, no live shot is graded until the session ends; with it on, every Capture↔Replay switch rebuilds the model (pack parse, SHA-1, reconcile, sibling ledgers). **This is the fault that started the audit, still present at HEAD.** | High |
| **D-01 / H-03** | Two live models (stage panel and cast) each write the whole `diagnostics.json` from memory, as do the catch-up and `regrade_ledger`. Only one of them merges, and only three keys. Golfer input on one surface is overwritten by another (suspected). | High |
| **D-03** | One class plays four roles chosen by flags (panel, cast, catch-up, report). Three can hold the same session at once. | High |
| **D-04** | The nullable "last panel wins" `SessionMode.sessionDiagnostics`. Five consumers do nothing while it is null, without saying so. | High |
| **E-01** | A running session can be split across two athletes. "＋ New athlete" and the avatar edit are reachable mid-session and auto-select. `SwingPaths` re-allocates the session folder silently (suspected Critical, if reproduced). | High |
| **E-03** | `currentAthleteChanged` fires on every athlete-record edit. A club-length edit mid-session reloads the live carousel, drops the selection and ends a review (suspected). | High |
| **E-04** | "The selected shot" is held five times under four definitions; every consumer fetches its own copy. | High |
| **E-05** | The selection outlives its session, and is resolved into a new session's shot by folder basename. Two different swings are presented as "the" shot (suspected). | High |
| **E-08** | "The current session" is re-derived from strings in five places with different rules. | High |
| **F-01** | Session chart data has no owner: one `SessionSeriesModel` per view (5+). Trust views go stale. | High |
| **F-02** | `SessionSnapshot` is a second session-diagnostics model. The report reads both. | High |
| **B-02** | At least 10 independent pack assemblies with unrelated lifetimes. They re-parse 704 KB each, and can disagree after an edit. | High |
| D-06 | Athlete-level facts (the club baseline freeze) are computed by the session view-model, by whichever instance has authority. | Med |
| D-15 | `WorkOnsController`'s in-flight derivation is neither cancelled nor paused on an athlete switch; the catch-up overlaps open sessions. | Med |
| E-12 | The session lifecycle is a four-call QML choreography repeated in four places. | Med |
| E-13 | App-lifetime globals hold athlete-scoped state and are re-pointed by hand. | Med |
| E-14 | Panels own session-level models, so state has to be parked on `SessionMode` to survive a rebuild. | Med |
| D-13 | `rebuild()` both reduces and presents: a selection click re-reduces the whole session. | Med |
| D-14 | Each ⚠ comment in the diagnostics panel, mapped to the structural fault it patches. | Med |
| A-19 | The metric catalogue is "not a singleton" but is rebuilt six or more times and installs a hidden global. | Low |

## Theme 2 · Identity: one thing, several keys

| Id | Finding | Sev |
|---|---|---|
| **E-02** | The library is keyed by the athlete's sanitised name, while a uuid exists. Renaming an athlete orphans their whole history, look-alike names share a folder, and the folder is derived six ways. | High |
| **E-06 / F-07** | "Shot N" has three on-screen meanings (carousel swing number, ledger position, Charts' rank). They disagree after a trashed or unanalysed swing. | High |
| **F-06** | Session order and "n of the day" come from folder names; the Sessions list uses start time. Reachable through a naming setting and LM-only sessions. | High |
| E-07 | Two machine numberings; six folder-number parsers; `ingestShot(shotId, …)` ignores its id. | Med |
| G-07 | A shot has three club identities. The LM board probably grades an undeclared club as a driver. | Med |
| C-11 | One fact under several names; keys built from words or folder names. | Med |
| A-13 | Domain identities (camera roles) are defined in the GUI and travel down as magic ints. | Med |
| D-16 | A stale three-numberings comment; `ingestShot` ignores its id. | Low |

## Theme 3 · One fact, computed more than once, sometimes by different rules

| Id | Finding | Sev |
|---|---|---|
| **C-01 / G-03** | A shot's outcome and three factors have no owner. They are composed per model instance, then recomposed by each athlete-level reduction (Home, outcome filter, report). `shotOutcome()` returns no factors at all. | High |
| **F-05 / G-01** | Two direction classifiers that already disagree. The LM card and Club delivery use `lmShapeClass` (1°/2°); the factors and Tendencies use pack corridors (~4°). A 7 iron at −2.5° start reads "Pull–fade" on the card and "Straight" in Tendencies. | High |
| **F-03** | The report's cover, numbers table, carry row and reference swing ignore the golfer's LM exclusions and club overrides; the appendix of the same PDF honours them (suspected wrong data on the PDF). | High |
| **G-02** | Handedness is glossed per surface, and three surfaces miss it: pattern cards and chains, Home work-ons, Club delivery (suspected). | High |
| **G-04** | Sky is a flight factor but not a label, so a skied shot can be "Good shot" and clean in This shot while the flight bar counts it as a Sky fault. | High |
| **G-05** | The per-shot "score" is the wrist resemblance and 0 when unscored. Every card in a camera-only or LM-only session shows a red "0". | High |
| **C-06** | The pack's condition kind and hold rule are projected five ways. Swing themes ignores `reportHold` and Setup, so Your swing and Work-ons can disagree (`chicken_wing`). | High |
| **A-01** | Live and re-analysis build the analysis job separately; only re-analysis sets the standard club length. Rarely hit live. | High |
| F-04 | The report maps link grades by hand without the panel's caps. (Partly wrong: the test itself is shared.) | Med |
| G-06 | The strike-vs-flight check runs in memory per instance and is persisted only when another write is owed. `flight_norms.json` is in no stamp. | Med |
| G-08 | "Carry this session" and "your usual carry" each exist twice with different rules. | Med |
| G-09 | The per-club factor mix is counted two ways under one bar. | Med |
| G-10 | Session flight usuals are recomputed from raw rows in six or more places and never stored. | Med |
| G-11 / D-11 | Domain rules in QML ("Clean" vs "Solid", shape-by-substring, fault-line distances from band words; the pattern C21 retired). | Med |
| C-02 | The flight check rewrites persisted rows (by design, 10 Nov). Nothing measured is lost, but `computeContentStamp` omits the flight norms and thresholds. | Med |
| C-10, D-17, B-17, B-22, G-12, G-13, G-14, A-12 | The same rule, list, mapping or label in several places. B-17's copies already disagree on two enumerators. | Med/Low |

## Theme 4 · Persistence and contracts

| Id | Finding | Sev |
|---|---|---|
| **H-04** | **Critical: golfer input shares a file with a cache that is rebuilt by deleting it. One loss confirmed.** A declared focus (`excessive_shaft_lean`) in the 10 Jul Wrist session exists only in the 7 Aug backup. The mechanisms (regrade sets the file aside; an unreadable file is treated as absent, then overwritten) are all still present. | **Critical** |
| **A-02 / H-01 / E-10** | One writer and four hand-kept converters of the analysis block, already disagreeing. The replay's `overall` is always 0; σ, timing and fusion keys are on disk but missing live; three "mirrors swing_doc.cpp" comments are false. | High |
| **H-02 / E-11** | The swing header is built twice and has drifted. `athlete.heightM` is never written for a normal capture, so **re-analysis does not reproduce the live analysis** (height prior 0 for the 3-D skeleton), and a female athlete's low point uses the men's club length. | High |
| H-05 | `atomicWrite` (all session and athlete caches) is remove-then-rename with a fixed `.tmp`: not atomic. | Med |
| H-06 | Whole-document read-modify-writes and fat parses still run on the GUI thread (every star, LM pairing, re-analysis write-back). | Med |
| H-07 | Each carousel row carries a full analysis detail that no QML reads; the documented fallback is dead code. | Med |
| H-08, C-03, D-08, B-20, F-13 | Schema tags written but never checked; each reader handles older shapes itself; the envelope parsed in about 10 places; version gates bypassed on read. | Med |
| H-09 | The athlete profile lives in per-machine settings, so GOLFSIMPC re-analysis can use a different profile. | Med |
| H-10 | Archive restore drops "Leave out" and club overrides made while archived. | Med |
| A-03 | Two time domains, guessed by magnitude (older files only since 7 Jul; stale comments). | Med |
| A-08, A-09, A-10 | Stage versions gate reuse only, so library staleness is invisible. Write-back is two hand copies. LM rows live inside `analysis.metrics` with three writers. | Med |
| C-05 | Rule and schema versions bumped by hand; changelog gaps. | Med |
| D-10, F-12, A-14 | 43+ QVariant shapes defined only in comments; payload contracts in untracked `build/` files; untyped tuning overrides. | Med |
| H-11 / D-07 | `fault_profile.json`: a reader on every activation, a writer with no caller. | Low |
| H-12, H-13, H-14 | File names spelled per class; a process-wide one-entry document cache; no lock against the CLI tools. | Low |

## Theme 5 · Layering: domain, presentation and I/O in the wrong places

| Id | Finding | Sev |
|---|---|---|
| **C-04** | Golfer-facing English is built inside domain reductions and persisted (tendencies, outcomes, distances, themes); the "no prose here" rule is applied in two files only. | High |
| **B-05** | A norm's standing ("provisional" / "Mark decided") exists only as prose, parsed by substring. The release dashboard already misreads two norms (`m_takeawayPlane`, `m_clubSlotTop`). | High |
| F-11 / D-09 / C-08 | About 5,000 lines of domain rules under `Gui/charts` and `Gui/diagnostics`. Coaching and Metrics include them upward, which makes one include cycle (`data/include_graph.md`). | Med |
| C-07 / B-12 / B-06 | Domain headers do file I/O, and a hidden flight-norm singleton fails silently. Pure functions read process state. Flight norms are a second norm registry. | Med |
| B-08, B-09, A-11 | Wrong-way dependencies (a Coaching adapter in Diagnostics; Wrist ↔ norms; Analysis ↔ every producer). The engine/packs/norms/measures directories are not layers. | Med |
| B-10, A-15 | Pixel layout and UI strings in the engine; coach wording in Metrics, part of it contradicting the code. | Med |
| B-14 | Diagnostics is not a library target. | Med |
| C-14 / B-15 / A-20 | `pinpoint::analysis` spans eleven directories with generic names; it has already caused one silent crash (ODR). | Med |
| A-05 | `analysis_pipeline.cpp` holds about 50 stages of domain logic and is built into no default ctest target. | Med |

## Theme 6 · Size, cohesion and copy-paste

| Id | Finding | Sev |
|---|---|---|
| **D-05** | `SessionDiagnosticsModel`: all three diagnostic levels, view state and every view-model, in 6.6k lines. Nine functions over 150 lines. | High |
| **E-09** | `ShotProcessor` mixes capture, job building, serialisation, document write, carousel rows, a replay player and session-folder ownership. Untested. | High |
| F-08 | Six chart presenters clone one scaffold (83–93 % alike). | Med |
| F-10, C-09, A-16, B-11, B-18 | God functions: `SessionReportModel::build` 1,011 lines; `composeShotOutcome` 390; `reduceTendencies` 438; `SwingDiskLoader::load` 575; `characteristic_pack.cpp` eight jobs. | Med |
| A-06 | The metric catalogue is 5,460 lines of data written as code; three tools regex-scrape it (one builds the release dashboard). | Med |
| A-07, B-13, C-12, B-16 | Metric keys as repeated literals; flat registries copying the loader; thresholds copied as literals. | Med/Low |
| A-04 | Route and instrument precedence stated four ways (no disagreement shown). | Med |

## Theme 7 · Concurrency and state

| Id | Finding | Sev |
|---|---|---|
| **B-03** | Registry caches are unsynchronised lazy singletons, first touched from worker threads. The code's own comments record GUI-thread workarounds. | High |
| D-12 | The detection worker reads members the GUI thread can write (`m_policyName`), against its header's claim (suspected race). | Med |
| F-09 | Five threading idioms. `~SessionSeriesModel` still blocks on its job; two pools are never joined at quit. | Med |
| C-16 | `gradedReadings` returns a reference into a scratch copy of the whole shot. | Low |

## Theme 8 · Docs and comments that say the opposite of the code; dead code

| Id | Finding | Sev |
|---|---|---|
| **B-07** | **Behaviour defect.** "Club assumed" demotion never runs: a shot with no club is graded at full confidence, while the in-app Gates text says it is "trusted less". It rests on a DRIVER stub that no longer exists. | High |
| **B-01** | **Behaviour defect.** A saved corridor edit never reaches the app until a relaunch: the norm cache's only reset has no caller. | High |
| A-18 | The comments and developer guide prescribe full-window re-analysis, which the code no longer does and the shaft guide calls a trap. | Med |
| B-04 | The diagnostics developer guide describes an earlier system ("execution layer not wired"). | Med |
| D-18, D-19, E-15, F-14, C-15, A-17, H-11 | Comments contradicting the code; dead or test-only code (`closeSession`, `previousAnalysisDetail`, `WristDiagnosticsModel`, `wristArchetype`). | Low |
| C-13, B-19 | Mixed "missing" conventions: "—" vs "-"; four conventions for "no value" in one result family. | Med/Low |

## Design-system items (Mark's approval needed before any change)

| Id | Finding | Sev |
|---|---|---|
| D-20 | The cast window hand-draws its Pin button. | Low |
| E-16 | `PpModeStage` hand-draws its tab strip while `PpTabs` exists. | Low |
| G-05 | (UI half) What the score pill shows when nothing scored the shot. | — |

## The June review, re-checked

| Item | Status |
|---|---|
| Header built twice | **Worse**: the copies have drifted (H-02). |
| Schema string literal | **Open**: now ×4. |
| Atomic-write scaffolding | **Worse**: three mechanisms, one not atomic. |
| π / `wrapPi` | **Worse**: π literal in 33 files; four `wrapPi` definitions. |
| Settings-fallback idiom | **Worse**: 48 uses (was 32). |
| `metric_keys.h`, `nearestIndex`/`phaseValue` duplicates, device key | **Open** |
| Club `"DRIVER"` on reload | **Fixed** |
| Inline thresholds | **Mostly fixed** (`pp_tuned_constants.h`). |
| `reference_bands` literals | **Fixed** (moved to `norms.json`). |

The June review did not cover Coaching, Diagnostics or Gui/diagnostics, which are the areas where most of
this audit's High findings sit.
