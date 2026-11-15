# PinPoint Studio information model

| Document control | Value |
|---|---|
| **State** | **v1.8: governance-ready. Mark's answers of 15 Nov 2026 to the final re-audit's questions (MD-01…MD-12), to Q-17, Q-27 and Q-28, and to step 2's brief are applied (DEC-130…DEC-160). The questions still open are [§14](#14-open-decisions)'s Q-02, Q-05…Q-07, Q-16, Q-18…Q-20 and Q-29, each with the step whose brief brings it to Mark.** |
| **Version** | 1.8.3, 15 Nov 2026 ([§15 Change log](#15-change-log)) |
| **Status** | **Canonical** (Mark, 14 Nov 2026; DEC-40). This page is the information model. `docs/implementation/swe-audit/03-target-architecture.md` is the frozen rationale (why); it is never edited to change the model. Where the two disagree, this page wins and the disagreement is a defect in 03. |
| **Describes** | The target the refactor (`docs/implementation/swe-audit/04-remediation-plan.md`) moves the code towards. Much of it is not built yet: before relying on an element, check its row in [§11 Where the code is today](#11-where-the-code-is-today). |
| **Changed by** | Mark's approval, before it is built, for every information-model change as [§1.2](#12-what-counts-as-an-information-model-change) defines it (§1.2 is the only definition; `CLAUDE.md` points to it: Mark, 14 Nov 2026; DEC-76). Every edit, a §11 update (one of §1.2's exclusions) among them, is recorded in the change log with the ids of what changed (ratified by Mark, 14 Nov 2026; DEC-105). |

**The marks used on this page:**

| Mark | Meaning |
|---|---|
| (Mark, 14 Nov 2026; DEC-nn), (Mark, 15 Nov 2026; DEC-nn) | Mark's decision, in the register ([§13](#13-decisions-register)), with the day he made it |
| (ratified by Mark, 14 Nov 2026; DEC-nn) | A clarification written by the refactor lead (the agent that leads the refactor and wrote this page's first version), ratified by Mark |
| (proposed, for Mark) | Written by the refactor lead and not yet decided by Mark (DEC-85). A plan may rely on one only with Mark's approval. Version 1.7's "(proposed, for Mark; MD-12)" marks, the sentences the final re-audit found untraceable, were all decided on 15 Nov 2026 (DEC-140; SET-05's kind by DEC-153). The ones that stand in version 1.8 are listed in its change-log row (§15). |
| **OPEN (Q-nn, step N)** | Not decided. Described as the code is today, with the interim rule; the named step's brief brings it to Mark ([§14](#14-open-decisions)) |
| n/a: reason | The column does not apply, and why. A cell is never blank, and never a bare "—", "n/a" or "design". |
| today | What the code does now, with its evidence (a file, a commit, a finding). It describes and decides nothing. The "today" and "writer today" columns and all of §11 are of this kind. |
| no mark | **Ratified at version 1.6** (DEC-85), except a sentence marked proposed or OPEN: the agreed design (03's decisions 1–30, DEC-01…DEC-30, as this page restated them at `108e7858`), and every proposal of versions 1.0–1.4, which Mark ratified one by one (DEC-87…DEC-99, DEC-105…DEC-121, DEC-123). RAT-11 was rejected. A sentence written in v1.7 is not "ratified at 1.6": it was written in v1.7 from the decision it cites, and the DEC id is shown beside it. A sentence written in v1.8 was written from Mark's answers of 15 Nov 2026 and cites the decision (DEC-130…DEC-160) it follows. |

## Contents

1. [Purpose and how to use it](#1-purpose-and-how-to-use-it)
2. [The three contexts and the Recording contract](#2-the-three-contexts-and-the-recording-contract)
3. [The principles](#3-the-principles)
4. [The rings and the directory mapping](#4-the-rings-and-the-directory-mapping)
5. [Domain objects](#5-domain-objects)
6. [Application state](#6-application-state)
7. [Derived facts](#7-derived-facts)
8. [Persistence](#8-persistence)
9. [Concurrency and the QML boundary](#9-concurrency-and-the-qml-boundary)
10. [The ownership questions](#10-the-ownership-questions)
11. [Where the code is today](#11-where-the-code-is-today)
12. [Glossary](#12-glossary)
13. [Decisions register](#13-decisions-register)
14. [Open decisions](#14-open-decisions)
15. [Change log](#15-change-log)

---

## 1. Purpose and how to use it

This page is the source of truth for **what holds data in PinPoint Studio, who owns it, and where it lives**. It
plays the role for data that `docs/design/system/README.md` plays for the UI.

### 1.1 When to read it

| When | What to do |
|---|---|
| Before writing any plan or brief that touches data | Read this page. Copy [§10](#10-the-ownership-questions) into the plan's "Ownership" section and answer it for every new or changed thing, citing the ids of the elements it touches (ratified by Mark, 14 Nov 2026; DEC-105). |
| Before changing code that holds, derives or stores data | Find the element in [§5](#5-domain-objects)–[§8](#8-persistence) and its status in [§11](#11-where-the-code-is-today). |
| When reviewing a diff | Answer the seven ownership questions for the diff, and check that §11 still matches the code the diff touches (ratified by Mark, 14 Nov 2026; DEC-105). A "no" is CHANGES NEEDED. |
| When `architecture_lint_test` fails | Fix the code. Never add a line to its allowlist to make it pass; an allowlist entry needs Mark's approval, and each entry names the refactor step that removes it. |

### 1.2 What counts as an information-model change

**HARD: any information-model change needs Mark's approval before it is built.** Propose the need, what it affects
and the answers to the ownership questions, then wait. The definition below is the one the §10 checklist and the
reviewer use, word for word:

> An information-model change (IMC, v1.1) is any edit to code, data, a file format or a plan that adds, moves,
> renames, splits, merges or removes any of the following, or changes what this page says about one: (C1) a domain
> object, an owner of facts, or what an aggregate contains; (C2) a kind of fact (a result, a measurement
> family, a golfer input), or the kind of a fact (source, recorded input, derived, recorded product, application
> state, view state) or its level (shot, session, athlete, application);
> (C3) a persisted file, a field family in one (a new block or group of keys, or a schema-version change of an
> existing file), or a settings key that holds a domain fact; (C4) the one writer of a file, or the repository it is
> written through; (C5) an identity: a new key, a new representation, a new translation to a foreign id, or
> identifying an athlete, a session or a shot by anything but its id, or adding an id type; (C6) a dependency
> between levels: a level
> reading anything but the level below as results, or context flowing down that is not a typed input; (C7) a
> domain event, its producer, or what it recomputes; (C8) an object's lifecycle: who creates or ends it, its
> states, or the commands between them; (C9) a ring, or the ring of a directory or file; (C10) anything domain- or
> session-scoped created by a view; (C11) a principle (P1…P10) or a rule (R-*). It is not an information-model
> change to make code match this page, to add
> a metric to the catalogue within an existing measurement family, to bump a rule's or a stage's version when the
> bump only makes caches stale (the stamp already carries it), to update §11, or to edit a glossary entry's wording
> when the edit cites the decision it follows. A version bump that would rewrite
> a recorded input is a change, proposed with a dry-run count of what it would rewrite. When in doubt, it is a
> change.

Mark adopted this definition (Mark, 14 Nov 2026; DEC-68) and the rule for a version bump that would rewrite a
recorded input (Mark, 14 Nov 2026; DEC-84). `CLAUDE.md`'s rule points here and restates nothing (DEC-76). C11 (principles and rules only) and the glossary
exclusion are Mark's (Mark, 15 Nov 2026; DEC-139); with them the definition is v1.1.
"Measurement family" is defined in §12 (ratified by Mark, 14 Nov 2026; DEC-107).

C5's last clause is the stricter wording Mark ratified (ratified by Mark, 14 Nov 2026; DEC-106).

| Change | Example |
|---|---|
| C1 a new first-class object or owner | a new diagnostics owner; a new aggregate |
| C3 a new persisted file, or a new field family in one | a new `<session>/*.json`; a new block in the shot's `review` |
| C5 a new identity key | identifying an athlete, a session or a shot by anything but its id; a new id type |
| C6 a new dependency between levels | the athlete level reading shot findings; a shot reading another session |
| C7 a new event | anything beyond [§7.4](#74-events-and-what-they-recompute) |
| C10 anything created by a view | a QML file instantiating a C++ type that holds session or athlete data |

### 1.3 Ids

Every element has a stable id. An id is never reused, even after its element is removed (the change log says
what it was). Cite ids in plans, briefs, reviews, commit messages and lint messages (ratified by Mark, 14 Nov 2026; DEC-105).

| Prefix | What | Where |
|---|---|---|
| `P1`…`P9` | Principles | [§3](#3-the-principles) |
| `R-<AREA>-n` | Rules (`REC` Recording contract, `INS` instruments, `CLUB`, `CMD` commands, `HL` headless, `APP` application state, `DER` derived facts, `ANA` the analysis, `STAMP`, `PERS` persistence, `INPUT` golfer input, `CON` concurrency, `QML`) | §2–§9 |
| `OBJ-<NAME>` | Domain objects, application-state objects and value types | [§5.3](#53-the-objects), [§6](#6-application-state) |
| `ID-<NAME>` | Identities | [§5.2](#52-identities) |
| `EVT-nn` | Domain events | [§7.4](#74-events-and-what-they-recompute) |
| `FILE-<AREA>-nn` | Persisted files (`SHOT`, `SESS`, `ATH`, `LIB`, `APP`) | [§8.1](#81-files) |
| `SET-nn` | Settings keys that hold domain or rig facts | [§8.2](#82-settings-that-hold-domain-facts) |
| `C1`…`C11` | The kinds of information-model change | [§1.2](#12-what-counts-as-an-information-model-change) |
| `DEC-nn` | Decisions | [§13](#13-decisions-register) |
| `Q-nn` | Open decisions | [§14](#14-open-decisions) |
| `LC-<OBJ>` | Lifecycles | [§5.4](#54-lifecycles) |
| `CLS-<NAME>` | Class specifications | [§5.5](#55-what-each-class-is-for) |
| `RING-<NAME>` | Ring placements | [§4.1](#41-directories-mapped) |
| `EXT-nn` | Files that are not domain | [§8.3](#83-files-the-app-reads-or-writes-that-are-not-domain) |
| `GL-nn` | Glossary terms | [§12](#12-glossary) |
| `RAT-nn` | Proposals for Mark to ratify (none stands in v1.6) |
| `MD-nn` | The final re-audit's questions, all closed in v1.8 (DEC-130…DEC-140) | [§14](#14-open-decisions) |
| `TERM-n`, `LIVE-n`, `NAME-n`, `NOTDOM-n` | Rows of §3's two terms, §5.6, §4.1's names and §6.1 | where they stand | marked where they stand |
| `M-<NAME>` | Named models | [§2.4](#24-the-shots-layers) |
| `A1`…`A8` | Lint rules (`architecture_lint_test`) | [§1.4](#14-the-lint-rules) |

### 1.4 The lint rules

The lint is `src/Gui/tests/architecture_lint_test.cpp` (03 §11, agreed; held here as canonical, DEC-40). The allowlist
is tagged by the migration step that removes each entry, and adding an entry needs Mark's approval.

| Rule | Catches |
|---|---|
| A1 | an include from an inner ring into an outer one ([§4](#4-the-rings-and-the-directory-mapping)); and an include from `Export` or `Recording` into `Gui` (Mark, 15 Nov 2026; DEC-146: today's hits allowlisted, each with the step that removes it) |
| A2 | QML creating a type marked domain- or session-scoped (today: `SessionDiagnosticsModel`, `SessionSeriesModel`, `LmSessionModel`, `Charts*Presenter`, `TrustPresenter`, `SwingDataSource`) |
| A3 | folder-name parsing outside `SessionIdentity` / `SwingPaths` |
| A4 | a session or athlete file name spelled outside its owner. Today it covers FILE-SESS-02…05, FILE-ATH-01…03 and FILE-ATH-05 (eight names); the other files of §8 are not checked yet. Step 2 extends it to every FILE-SESS and FILE-ATH name, spelled only in `session_files` and `athlete_files` (Mark, 15 Nov 2026; DEC-160: IM-S2-6) |
| A5 | file I/O in a rules directory (Coaching, Diagnostics, Analysis rules) |
| A6 | a `QVariantMap`/`QVariantList` added to a presenter's or a domain type's public API |
| A7 | a new function over 300 lines in the Vocabulary and Rules rings |
| A8 | Analysis code reaching past the Recording contract into Acquisition (device classes, the ring buffer, live controllers) or into application state (the athlete controller, settings) |

A change to a lint rule's scope, or to A4's list, is an information-model change (C4, C9) (ratified by Mark, 14 Nov 2026; DEC-120).

---

## 2. The three contexts and the Recording contract

PinPoint is three contexts joined by two contracts. Dependencies run left to right; context flows back from Domain
to Analysis as typed input.

```
 ┌──────────────────────┐   Recording    ┌───────────────────────────────┐   results    ┌──────────────────────────────┐
 │ ACQUISITION          │ ─────────────▶ │ ANALYSIS                      │ ───────────▶ │ DOMAIN + APPLICATION         │
 │ transports, devices, │                │ analysis graph: inference ·   │  (shot       │ Athlete · Session · Shot ·   │
 │ ring buffer,         │                │  fusion/synthesis · segmenta- │   analysis,  │ Studio/Rig · persistence ·   │
 │ connectors,          │                │  tion · metric + joint extrac-│   shot       │ AppState · presentation      │
 │ calibration          │                │  tion · shot findings         │   findings)  │                              │
 │ procedures           │                │ cross-shot reductions:        │ ◀─────────── │ owns and stores everything;  │
 │                      │                │  session and athlete diag-    │   context    │ hands Analysis its context   │
 │                      │                │  nostics, diagnosis, coaching │  (baselines, │ (athlete facts, rig state,   │
 └──────────────────────┘                └───────────────────────────────┘   rig, usuals)│  session usuals, norms)      │
                                                                                         └──────────────────────────────┘
```

| Context | Holds | Owns |
|---|---|---|
| **Acquisition** | Transports, devices, the ring buffer, connectors, calibration *procedures*, threading inside capture. Its internals are designed in the platform refactor (capture), which follows this one; only its contract is fixed here. | Nothing persistent: it produces Recordings, which the domain owns and stores |
| **Analysis** | The analysis graph (per shot), the findings rule over the diagnostic graph, and the session's diagnosis rules (the two graphs' names: Mark, 15 Nov 2026; DEC-132) | Nothing persistent: its results are owned and stored by the domain |
| **Domain + Application** | Athlete, Session, Shot, Studio/Rig, persistence, `AppState`, presentation | Everything that is stored |

**Contexts and rings are different cuts** (ratified by Mark, 14 Nov 2026; DEC-48). A context describes how data
flows; a ring ([§4](#4-the-rings-and-the-directory-mapping)) describes which code may depend on which. So one
context spans several rings:

| Example | Context | Ring |
|---|---|---|
| A reduction *rule* (tendencies, the flight check) | Analysis | Rules |
| The findings rule, `fireFindings()` | Analysis | Rules |
| The *owner* that runs them and keeps their results (`SessionDiagnostics`, `AthleteDiagnostics`) | Domain + Application | Domain model |
| The analysis pipeline that runs the analysis graph | Analysis | Application |

### 2.1 The shapes of Analysis

**Firing findings** is the name in the target for a shot checking each of its numbers against its corridor; **diagnosis**
is reserved for the session's work over its shots' findings (Mark, 14 Nov 2026; DEC-100). The code calls the shot's part `detectShot` (a
member of `SessionDiagnosticsModel`); step 3 makes it a pure function and renames it (e.g. `fireFindings`).

| Shape | What it is | Run by |
|---|---|---|
| **The analysis graph** (per shot; DEC-132) | A pure function: Recording + effective context → the shot's analysis (the recorded product). A dependency graph of versioned stages, not a stack (`analysis_dag_design.md`). | The analysis pipeline (an application service). **It never fires findings.** (Mark, 14 Nov 2026; DEC-19, DEC-100) |
| **Firing findings** | A pure function, `fireFindings()` (step 3's name) in `src/Diagnostics`: the shot's analysis and the context its `Session` passes down (club baseline, norms, effective club) → the shot's findings (Mark, 14 Nov 2026; DEC-100), which the `Shot` posts, stamped, to its `Session`, which keeps them (Mark, 15 Nov 2026; DEC-135). | Each `Shot` fires its own findings; on a corridor or norm edit the `Session` asks each shot to re-fire, with no re-analysis (Mark, 14 Nov 2026; DEC-100) |
| **Cross-shot reductions** | The session's usuals (GL-25; Mark, 15 Nov 2026; DEC-130); the session's diagnosis from its shots' findings (tiers, patterns, links, reasons, per-club mixes, the summary) (Mark, 14 Nov 2026; DEC-100), athlete diagnosis, coaching ([§7](#7-derived-facts)) (the cross-shot "assessment" of 03 is "diagnosis": Mark, 14 Nov 2026; DEC-125). The shot's outcome, its three factors, its carry and its flight-check overlay are not reductions: they are the shot's derived data, computed for the shot with the session's usuals passed down, which feed the assessment that fires its findings (M-SHOT-LAYERS) (Mark, 14 Nov 2026; DEC-121). Incremental and stateful over many shots, recomputed by events. | `SessionDiagnostics`, `AthleteDiagnostics` |

### 2.2 The Recording contract (Mark, 14 Nov 2026; DEC-16)

A **Recording** (OBJ-RECORDING) is everything Analysis is allowed to know about a shot, and nothing else.

| Part | Contents |
|---|---|
| Kind | Which kind of shot it is: **captured** (it has streams) or **device-only** (a launch monitor alone saw it: no streams). An LM-only shot **is** a Recording (Mark, 14 Nov 2026; DEC-44). |
| Streams | Video frames, IMU and HackMotion samples, the launch monitor's readings as delivered. A device-only Recording has only the reading. |
| Timestamps | On one stated clock model: camera timestamps mark exposure end; Spinnaker frames are host-stamped (about ±0.4 ms jitter) |
| Devices | Device identity and configuration |
| Capture facts | The session type the shot was captured under (`capture.sessionType`), recorded like its analysis context; the session type itself is the `Session`'s context, kept in the session file (FILE-SESS-01). Also the shot source, the impact time and the capture settings (today: `src/Recording/recording.h`). The job builder reads the recorded type to choose the analysis stages (today). SwingLab may override it for a run (`sessionTypeOverride`); the app never does; the input stamp names it (ratified by Mark, 14 Nov 2026; DEC-88). |
| Rig | A reference to the Studio/Rig state it was captured with (cameras, positions, calibrations) |
| Integrity | Capture holes, lost frames |
| Analysis context | The athlete facts the analysis consumes (handedness, height, club set, the club and its length), **recorded at capture**, so re-analysis never reads the current athlete record. It is the only source of athlete facts for analysis (ratified by Mark, 14 Nov 2026; DEC-49). A recorded input (P1). |
| Analysis context on disk | One schema-tagged `analysisContext` block in the swing file, written once by capture through the one header writer: `athlete.handedness`, `athlete.heightM` (0 = unknown), `athlete.clubSet` (men / women / junior), the captured club (`club.name`, empty = undeclared, never the stub) with its geometry, length prior and standard length, and `bag`, a snapshot of the bag's geometry so a corrected club is resolvable without the current athlete record. `capture.club` stays, written from the same value, as provenance (Mark, 14 Nov 2026; DEC-24, DEC-32, DEC-33). The norm cohort (sex, age band) is **not** recorded in step 1 (DEC-30); whether firing findings reads it from the Recording is **OPEN (Q-05, step 3)**. |

| Id | Rule | Forbids |
|---|---|---|
| R-REC-1 | A shot's swing file is written **once by capture** (the Recording), **then only by the `Shot` itself, through the repository** (Mark, 14 Nov 2026; DEC-86, which replaces DEC-22's "then only by its `Session`"). The `Shot` makes the save (DEC-103). The only other writes are one-off migrations (R-PERS-7) that Mark approves one by one, such as the step-1 backfill of height and club set (DEC-34) (ratified by Mark, 14 Nov 2026; DEC-108). | Any other writer of a swing file |
| R-REC-2 | **Capture writes in stages**: the shot (`RecordingRepository::create`, which `Session::addShot` calls: DEC-142), then its late parts through `RecordingRepository::completeCapture`. The late parts are a closed list: (a) a late launch-monitor reading, which may carry the club the device named: follow-club (SET-08) stores that **device club** beside the captured club, as its own source fact, and never overwrites the captured club or the shot's `review`; the effective-club rule prefers the device club when follow-club is on, so the analysis comes out as it did under DEC-82 (Mark, 15 Nov 2026; DEC-133). Today follow-club overwrites the captured club, `analysisContext.club` (with `capture.club`, its provenance copy, DEC-24), and moves a seeded `review.club` along with it (`swing_doc.h`): a known interim until step 6 (DEC-133, DEC-82, DEC-67). A device club changes the effective context, so the analysis is stale by its input stamp (R-ANA-5); (b) a phone clip's stream origin and, once its bytes land, its file, frame times and rate (Mark, 15 Nov 2026; DEC-140). A new kind of late part is an IMC (C3, C4) (Mark, 14 Nov 2026; DEC-27). | A late write that is not one of these |
| R-REC-3 | **Until step 3** the later writers are today's callers, all through `RecordingRepository`: `ShotListModel`, `ReanalysisController` and the one write-back `reanalyseAndPersist`, the launch monitor controller, `PpcpClipFiler`, `SessionReviewController` (the summary sidecar), `SessionArchiver` (its stub, through the public `SwingStore::save`, today), `LibraryConverter` and the tools (`lm_repair`, `pps_convert_library`, `swinglab_run`). The `Shot` takes them over in step 3 (DEC-86, which replaces DEC-28's "the `Session`"). | Writing the swing file past the repository |
| R-REC-4 | **Analysis only ever runs on a Recording**, whether it arrived a second ago or a year ago. Live analysis and re-analysis are the same function: **same Recording + same effective context (the effective club included) → same job.** | Building an analysis job from live objects; Analysis code reaching into device classes, the ring buffer, live controllers, the athlete controller or settings (lint A8) |
| R-REC-5 | **One builder** makes the analysis job from the Recording and its effective context (the recorded analysis context, with the golfer's club correction applied), for live capture and re-analysis alike. | A second job builder; any context field set on one path only |
| R-REC-6 | **Re-analysis uses the effective club** (Mark, 14 Nov 2026; DEC-18): when the golfer has corrected the club, the builder puts the corrected club, its length and its norms into the effective context, because it is what was really hit. A corrected shot re-analyses with its corrected club. The captured club stays in the Recording for provenance. After a correction the analysis is **stale only**: it is re-analysed when the golfer asks, or by the library sweep (Mark, 14 Nov 2026; DEC-39). | Re-analysing a corrected shot with the club it was captured under; Analysis reading the shot's `review` block itself |
| R-REC-7 | **The club rule until step 6** is R-CLUB-3 (Mark, 14 Nov 2026; DEC-26, DEC-31). | Treating the popover's club, or the board's evidence suggestion, as the correction |
| R-REC-8 | **A device-only Recording** (Mark, 14 Nov 2026; DEC-44) has no streams, a launch-monitor reading, the measurement rows derived from it and one Impact phase. The analysis graph has nothing to run on; re-analysing it derives the rows again from the recorded reading (`writeAnalysis` with no analysis). The kind is the `Shot`'s own fact: the one header writer writes it when the shot is created, and a document without it is classified once, in the repository's reader (`isDeviceOnly` today) (ratified by Mark, 14 Nov 2026; DEC-87). Its key is the top-level `shotKind`, `captured` or `deviceOnly`; old files are not backfilled and are classified by today's content rule (Mark, 15 Nov 2026; DEC-147). | Treating a device-only shot as a separate store or a separate model |

**The name.** The contract's types are vocabulary shared by both sides; its store is an adapter. The types live in
`src/Recording` (DEC-29); the store moves out of `src/Export` in step 7.

**In the code** (status: partly, [§11](#11-where-the-code-is-today)):

| Element | Where | Ring |
|---|---|---|
| `Recording` (window and `t0Us`, sources by role with the one perspective mapping, capture facts, ball inputs, IMU bindings, integrity inputs; every time window-relative) | `src/Recording/recording.h` | Vocabulary |
| `AnalysisContext` (handedness, height, club set, the captured club with its geometry, length prior and standard length, the bag snapshot) | `src/Recording/analysis_context.h` | Vocabulary |
| `effectiveContext()` v1, R-CLUB-3; its inputs are the recorded context and the shot's two club edits (`ClubReview`: the board's override, the popover's club) | `src/Analysis/effective_context.*` | Rules |
| `buildAnalysisJob()`, the one builder; `ShotAnalysisJob` | `src/Analysis/analysis_job_builder.*`, `shot_analysis_job.h` | Rules |
| The one header writer and the `analysisContext` block's writer and reader | `src/Export/recording_header.*` | Adapters (the store) |
| The Recording loader (a swing folder → the Recording + the review facts) | `src/Export/recording_loader.*` | Adapters (the store) |
| `RecordingRepository`; its adapter `SwingStoreRecordingRepository` | `src/DomainModel/recording_repository.h`; `src/Export/swing_store_repository.*` | Domain model; Adapters |
| `RecordingReader` (a swing folder → the Recording + the review facts; separate from the write door, so frame decoding stays out of the review writers); its adapter `SwingStoreRecordingReader` (step 1.5, `d07ccebb`) | `src/DomainModel/recording_reader.h`; `src/Export/recording_loader.*` | Domain model; Adapters |
| `AnalysisService` (the run, and the Recording's integrity verdicts); `integrityFor()` (step 1.5, `d07ccebb`) | `src/Analysis/analysis_service.*`; `src/Analysis/recording_integrity.*` | Application; Rules |
| The capture-side snapshot (the current facts → the Recording, read once at the window freeze) (step 1.5, `d07ccebb`) | `src/Gui/shot/capture_context.*` | Adapters |

### 2.3 Studio/Rig (Mark, 14 Nov 2026; DEC-17)

**OBJ-RIG. Named; designed in the platform refactor (capture).**

| Aspect | Target |
|---|---|
| What it is | A domain entity owning the rig: which cameras and sensors, where they are, their intrinsic and extrinsic calibration, versioned over time |
| Not owned by | A device, or a shot |
| Relation to shots | Each Recording references the rig state it was captured with |
| Calibration | The *procedure* belongs to Acquisition; its *result* is a source fact owned by Studio/Rig |
| Identity, persistence, writer, `RigChanged` | Design: the platform refactor (capture). Until then a Recording keeps each stream's setup (perspective, mirrored, fixed in place) and a slot for the rig reference |
| Where rig facts live today | Settings SET-09, the studio profile FILE-APP-06, the trunk ceremony FILE-SESS-11, and assumptions in code ("fixed cameras are square", the uncalibrated DTL yaw σ). No new place may be added before the design (C3) (Mark, 15 Nov 2026; DEC-140). |

### 2.4 The shot's layers

**M-SHOT-LAYERS** (Mark, 14 Nov 2026; DEC-121): "The shot contains information that was acquired and collected, then processed and
analysed. That data is then analysed against the DAG to fire findings. The shot outcome and tendencies are part of
the shot derived data. They feed the assessment that fires findings." In the quote, "the DAG" is the diagnostic
graph (Mark, 15 Nov 2026; DEC-132, DEC-140).

| Layer | Is | Produced by | Owner | Persisted |
|---|---|---|---|---|
| 1 Acquired | What was acquired and collected: the Recording (§2.2) | capture | `Shot` | FILE-SHOT-01, 03, 04, 06 |
| 2 Processed and analysed | The analysis, the recorded product (§7.2) | the analysis pipeline | `Shot` | FILE-SHOT-01 |
| 3 Shot derived data | The shot outcome; the shot's tendencies, its three factors (strike · direction · flight); carry against the usual; the flight-check overlay (Mark, 14 Nov 2026; DEC-122, superseding DEC-06). Computed for the shot with the session's usuals (GL-25) passed down as a typed input (P6); a change of a club's usuals makes every same-club shot recompute its derived data and re-fire its findings (Mark, 15 Nov 2026; DEC-130) | the shot rules (Coaching) | computed by the `Shot` (P10; OBJ-SHOT-DERIVED), which posts it, stamped, to its `Session`; kept by the `Session` (Mark, 15 Nov 2026; DEC-135) | FILE-SESS-03, with the shot's findings (DEC-135, which moves DEC-124) |
| 4 Assessment | The analysis and the derived data checked against the diagnostic graph (the diagnostic engine, pack + norms) (Mark, 15 Nov 2026; DEC-132, DEC-140), with the club baseline, norms and effective club the `Session` passes down | `fireFindings()` (DEC-100) | `Shot` | n/a: a step, not stored |
| 5 Findings | The shot's findings, 1:1 with the shot that fired them (DEC-101) | the assessment | fired by the `Shot`, which posts them, stamped, to its `Session`; kept by the `Session` as its collection (Mark, 15 Nov 2026; DEC-135; OBJ-SHOT-RESULTS) | FILE-SESS-03 (DEC-135, superseding DEC-102) |

The outcome, the factors, the carry and the flight check are **not** findings: they feed the assessment (DEC-121). The
`Session`'s diagnostics then diagnose from its shots' findings (DEC-100): "the shot does not use the findings and the
session does" (Mark, 15 Nov 2026; DEC-135). "The shot's tendencies" (its factors) are
not the athlete level's Tendencies (§12, GL-33, GL-64). `thumb.jpg` (FILE-SHOT-05), the phase grid and
`swing_summary.json` are caches and are not placed in a layer.

---

## 3. The principles

| # | Principle | Forbids |
|---|---|---|
| P1 | **Source facts, recorded inputs and derived facts are different kinds of thing** (recorded input: Mark, 14 Nov 2026; DEC-21). A **source fact** is something only the world or the golfer can tell us (every instrument's readings and streams, the athlete record, what the golfer said, the history of what the golfer corrected); it is stored once, by its owner, and never regenerated. A **recorded input** is context written once at first need so a result is reproducible (the Recording's analysis context; a session's club baseline); it is never regenerated and never deleted with the caches. It is rewritten only when the rule that produced it has a newer version, and each rewrite is recorded with what it read (Mark, 14 Nov 2026; DEC-46); such a rewrite needs Mark's approval first, proposed with a dry-run count (Mark, 14 Nov 2026; DEC-84). The captured club is a source fact and is never overwritten: follow-club stores the device's club beside it, as its own source fact (Mark, 15 Nov 2026; DEC-133); today's overwrite is a known interim until step 6 ([§11.1](#111-status)). A **derived fact** is a pure function of source facts, recorded inputs and versioned rules; it may be cached, deleted and rebuilt at any time. | Golfer input or a recorded input stored in, or deleted with, a regenerable cache; a cache holding the only copy of anything; a derived fact stored in a source record (ratified by Mark, 14 Nov 2026; DEC-89); a higher level's verdict written into a lower level's facts (a shot's findings) |
| P2 | **Domain, application state and view state are three layers**, each referring only downwards. Domain is what is true without a screen (the same in a GOLFSIMPC sweep); application state is what the app is doing now; view state is one view's scroll, tab or expanded row. (A *layer* is one of these three; a *ring* is a dependency ring, §4: (ratified by Mark, 14 Nov 2026; DEC-118).) | Diagnostics owned by a view; the selection held in several places; domain rules in QML |
| P3 | **Aggregates have one root; each file has exactly one writer, inside its aggregate** (Mark, 14 Nov 2026; DEC-42). `Athlete` and `Session` are roots. Source facts change only through their owner's commands: an aggregate root's, or, for an entity's own facts, the entity's, reached through its root (Mark, 14 Nov 2026; DEC-104, which rewords DEC-42's and DEC-12's "the root's commands"). So inside the `Session` aggregate each `Shot` changes its own facts by its own commands (P10). The derived owners inside an aggregate (`SessionDiagnostics` in a `Session`, `AthleteDiagnostics` in an `Athlete`) write their own caches. The swing file's writers, capture, the `Shot` and approved migrations, are R-REC-1's. | Two objects writing one file; a source fact changed except by its owner's command; a service writing a session or athlete file except through its aggregate; changing the athlete under a running session; a view mutating findings |
| P4 | **Persistence lives behind repositories.** The domain model defines the repository interfaces (`RecordingRepository`, `SessionRepository`, `AthleteRepository`); adapters implement them (ratified by Mark, 14 Nov 2026; DEC-50). One repository per file family, one format definition, schema checked on read. | File I/O in domain objects or in Coaching/Diagnostics rules; a file's envelope parsed in more than one place; a file name spelled outside its repository |
| P5 | **Derivation is an explicit dependency graph, updated by events.** Each derived fact declares its inputs and rule version; only what depends on a change is recomputed; a rule's version is part of its result's stamp. | `mutable` dirty flags that rebuild everything; stamps that omit inputs; staleness found by accident |
| P6 | **One rule, one result, one owner, at the right level.** A level reads the level below as results. Context flowing down (session usuals into a shot; the athlete baseline into a session) is a typed input. Nothing recomputes another level's facts. | Recomputing another level's facts; two rules for one fact (two direction classifiers); a gloss applied per surface |
| P7 | **Identities are types, not strings.** `AthleteId`, `SessionId`, `ShotRef` are value types ([§5.2](#52-identities)); folders, display numbers and labels are properties. | Keying by name or folder; parsing a folder name outside `SessionIdentity` / `SwingPaths` (lint A3); "Shot N" meaning anything but the swing number |
| P8 | **Work runs on immutable snapshots; results come back to the owner.** Domain objects live on one owner thread: the GUI thread in the app, the main thread in a tool (ratified by Mark, 14 Nov 2026; DEC-51). Workers read immutable inputs and post results to the owning root. | A worker reading a live object; a lazy singleton first built on a worker; a new threading idiom |
| P9 | **The QML boundary is typed.** QML sees `Q_GADGET` values and `QAbstractListModel`s with declared roles. | A new `QVariantMap`/`QVariantList` in a presenter's or domain type's public API (lint A6); a domain or session-scoped object created in QML (lint A2) |
| P10 | **A `Session` is a collection of shots and their findings, plus the context they were taken in; everything specific to one shot is owned and managed by the `Shot`** (Mark, 14 Nov 2026; DEC-86, DEC-101). A shot's findings have a 1:1 relationship with the shot that fired them (DEC-101). The `Shot` is its own object, with its own commands for its own facts, and is reachable only through its `Session`. The `Session` keeps the collection (adding and trashing shots, swing numbers, order), each shot's findings and derived data, which the `Shot` fires and computes and posts to it, stamped (Mark, 15 Nov 2026; DEC-135: "the shot fires findings but the session keeps them as a collection"), the context the shots were taken in (identity, type, status, intent run, session intent, club baseline) and the session-wide results (the summary, the series). It hears every change to a shot through the shot's events, synchronously and in order (DEC-69). Each `Shot` fires its own findings from its analysis and the context its `Session` passes down (club baseline, norms, effective club); the `Session`'s diagnostics diagnose from those findings (Mark, 14 Nov 2026; DEC-100), which supersedes this principle's first wording, "grading stays run by the session's diagnostics", and DEC-19's "`SessionDiagnostics` grades every shot". | A shot-specific fact, command, file write or event owned by the `Session`, except the findings and derived data its shots post to it (DEC-135); a `Shot` reached except through its `Session` |

**Two terms side by side:**

| Id | Term | Is | Example |
|---|---|---|---|
| TERM-1 | Recorded input | Context frozen once, so a result can be reproduced | the Recording's analysis context; `club_baseline.json` |
| TERM-2 | Recorded product | A derived fact that is expensive, so it is kept with a stamp and refreshed when stale | a shot's analysis |

---

## 4. The rings and the directory mapping

Dependencies point **inward only**: a ring may use the rings inside it, never one outside (lint A1).

```
 ┌──────────────────────────────────────────────────────────────────────────────────────┐
 │ ADAPTERS  Devices: cameras · IMUs · HackMotion · launch monitors (Premier, GC Quad…)  │
 │           Persistence: the Recording store · repositories   UI: presenters · QML      │
 │  ┌────────────────────────────────────────────────────────────────────────────────┐  │
 │  │ APPLICATION  AppState · use cases (start/end session, correct, exclude, set      │  │
 │  │              intent) · services (capture, analysis pipeline, re-analysis,        │  │
 │  │              athlete catch-up, archive, report)                                  │  │
 │  │  ┌──────────────────────────────────────────────────────────────────────────┐  │  │
 │  │  │ DOMAIN MODEL  Athlete · Session · Shot · a shot's measurement set ·        │  │  │
 │  │  │               diagnostics owners · repository interfaces                   │  │  │
 │  │  │  ┌────────────────────────────────────────────────────────────────────┐  │  │  │
 │  │  │  │ RULES (pure)  measurement algorithms (raw device data → measurements,│  │  │  │
 │  │  │  │   incl. the LM's inferred reads) · instrument precedence · findings · │  │  │  │
 │  │  │  │   diagnostics engine + packs/norms · coaching rules                  │  │  │  │
 │  │  │  │  ┌──────────────────────────────────────────────────────────────┐  │  │  │  │
 │  │  │  │  │ VOCABULARY  value types (Club, Intent, Measurement, …) · ids  │  │  │  │  │
 │  │  │  │  │   · metric definitions + catalogue · club vocabulary · units  │  │  │  │  │
 │  │  │  │  │  ┌────────────────────────────────────────────────────┐      │  │  │  │  │
 │  │  │  │  │  │ PLATFORM  logging · profiling                       │      │  │  │  │  │
 │  │  │  │  │  │   · machine queries                                 │      │  │  │  │  │
 │  │  │  │  │  └────────────────────────────────────────────────────┘      │  │  │  │  │
 │  │  │  │  └──────────────────────────────────────────────────────────────┘  │  │  │  │
 │  │  │  └────────────────────────────────────────────────────────────────────┘  │  │  │
 │  │  └──────────────────────────────────────────────────────────────────────────┘  │  │
 │  └────────────────────────────────────────────────────────────────────────────────┘  │
 └──────────────────────────────────────────────────────────────────────────────────────┘
```

A **value type** (`Club`, `Intent`, one `Measurement`) is Vocabulary; the **objects that hold them** (a shot's one set
of measurements, the session's intent) are Domain model. 03 §2 draws `Intent` and `Club` in the Domain model ring;
this page is canonical (DEC-40): the types are Vocabulary.

| Ring | Holds | Never |
|---|---|---|
| **Platform** (ring 0; Mark, 14 Nov 2026; DEC-23) | Logging (`ppInfo`/`ppWarn`), profiling, machine queries. Every ring may include it. | Depends on anything outside itself; holds anything domain-shaped |
| **Vocabulary** | What things are: value types (`Club`, `Intent`, `Measurement`, …), identity types, metric definitions and the catalogue, club vocabulary, units, the Recording contract's types | Depends on anything but Platform |
| **Rules** | Pure functions: measurement algorithms (pose, club, ball, IMU fusion, kinematics, wrist, 3-D skeleton, the LM's inferred reads); the one instrument-precedence rule; firing findings; coaching (outcomes, factors, flight check, Sky, tendencies, link grading) | File I/O (lint A5); golfer-facing sentences; knowing about sessions or screens |
| **Domain model** | Entities and aggregates; the owners of derived facts; the repository interfaces (`RecordingRepository`, `SessionRepository`, `AthleteRepository`). Qt objects for signals, no QML | Persistence, device or UI code |
| **Application** | `AppState`; use cases (the commands that change the domain); services that run work: capture, the analysis pipeline, re-analysis, the athlete catch-up, the archive and the report | Holding domain data of its own; writing a file except through an aggregate |
| **Adapters** | Devices (a launch monitor is a device like any other; it simply needs no algorithm); persistence (`SwingStore`, the Recording store, implements `RecordingRepository`; the session and athlete stores implement theirs); UI (presenters, the words module, QML) | Being depended on by an inner ring |

### 4.1 Directories, mapped

This is the ring table `src/Gui/tests/architecture_lint_test.cpp` enforces: a directory's ring, with named files
placed elsewhere where a directory spans rings.

| Id | Ring | Directories (and files placed against their directory) |
|---|---|---|
| RING-PLATFORM | Platform | the logging, profiling and machine-query files of `Core` (`pp_debug`, `pp_log_stream`, `PpMessageLog`, `PpStatsLog`, `AnalysisProfileLog`, `pp_profiler`, `ort_log`, `cpu_topology`, `pp_gpu_metrics`, `pp_gpu_metrics_mac`, `pp_os_metrics`, `pp_model_path`); from step 2, `atomic_write` and `folder_lock` (Mark, 15 Nov 2026; DEC-160: IM-S2-7) |
| RING-VOCABULARY | Vocabulary | `src/Recording`, the Recording contract's types (Mark, 14 Nov 2026; DEC-29); `src/Identity`, the identity types `AthleteId`, `SessionId`, `ShotRef`, made in step 2 (Mark, 15 Nov 2026; DEC-148: a new directory, C9); `Metrics`; the rest of `Core`; the shared types of `Analysis` (`swing_analysis`, `gate_record`, `analysis_tuning`, `analysis_versions`, `uncertainty_config`, `uncertainty_json`, `segment_role`, `det_rng`, `swing_analysis_rebase`); the stream types of `Buffer` (`types`, `format_descriptor`, `source_descriptor`, `swing_window`, `swing_payload_source`, `imu_sample`, `pixel_format_names`); the reading types of `LaunchMonitor` (`launch_monitor_reading`, `lm_device`); `Export/frame_decode` |
| RING-RULES | Rules | `Pose`, `Club`, `Ball`, `Body`, `IMU`, `Kinematics`, `Wrist`, `Skeleton3d`, `Timeline`, `Diagnostics`, `Coaching`; in `Analysis`: `shot_analysis_job`, `effective_context`, `analysis_job_builder`, `reuse_gate`, `route_substitution`, `seg_resolve`, `stream_trim`, `capture_integrity_check`, `recording_integrity`, `manifest_gates`, `camera_pose_sticks`, `frame_store`; the inferred reads of `LaunchMonitor` (`lm_inferred_reads`, `lm_flight_path`, `lm_enclosing_ellipse`, `lm_club_evidence`, `lm_gates`, `lm_session_reductions`, `shot_pairing`, `standalone_gate`); `Core/kf3_smoother`; in `Gui/shot`: `shot_arbiter`, `shot_outcome` |
| RING-DOMAIN | Domain model | `src/DomainModel` (Mark, 14 Nov 2026; DEC-10, DEC-29) |
| RING-APPLICATION | Application | `Analysis` (the pipeline, re-analysis, `reanalyse_and_persist`); `src/App` for `AppState` (Mark, 14 Nov 2026; DEC-11); in `Gui/shot`: `shot_processor`, `shot_controller`, `reanalysis_controller`, `shot_source_map` (03 §2: the capture and session services are Application) |
| RING-ADAPTERS | Adapters | the rest of `Gui` (presenters, QML, the other controllers); `Export` (the Recording store); in `Analysis`: `mp4_frame_reader`, `recorded_products`; `Buffer` (the ring itself); `Video`, `Audio`, `LaunchMonitor` (connectors), `Ppcp`, `LLM`, `STT`, `TTS`, `Secrets`, `Update`; `Core/device_enumerator`, `Core/pp_settings`, `Core/studio_profile`, `Core/app_info`; the device back-ends of `IMU` (`ble_adapter_pool`, `ble_imu_transport`, `imu_base`, `imu_capabilities`, `imu_factory`, `imu_io_worker`, `imu_sample_tap`, `hm_session_worker`, `wt9011dcl`, `wt9011dcl_ble`, `wt9011dcl_base`, `wt901_config_check`); `Pose/motion_capture_probe` |
| RING-NONE | No ring (not C++) | `Resources` (shipped data: core pack, norms, flight norms, icons, fonts, keys); `Shaders` |

Today the lint places six files of `Gui/shot` in Application or Rules (above) and the rest of `Gui` in Adapters;
domain code leaves `Gui` in step 7, and the capture services' device half in the platform refactor (capture).

**Directories that span rings**, to be split:

| Directory | Parts |
|---|---|
| `Export` | The Recording contract's store (adapter) and `frame_decode` (vocabulary) |
| `Analysis` | Shared types (vocabulary), algorithm stages and the job builder (rules), orchestration (application), the MP4 reader (adapter) |
| `Core` | Platform, vocabulary, and device, settings and studio-profile infrastructure (adapters) |
| `LaunchMonitor` | Reading types (vocabulary), connectors (adapters) and inferred reads (rules) |
| `Buffer` | Stream types (vocabulary) and the ring buffer (adapter) |
| `IMU` | Fusion and orientation rules (rules) and the device back-ends (adapters) |
| `Pose` | Pose rules (rules) and the motion-capture hardware probe (adapter) |
| `Gui` | The capture and session services (application), two pure rules, and the UI (adapters) |

**Names.** "Focus" means two unrelated things and must not be reused for either:

| Id | Today's name | Kind | Target name |
|---|---|---|---|
| NAME-1 | `focusedShotId` | Application state: what is selected | `selection` |
| NAME-2 | `focusConditionId` | Domain source fact: what the golfer declared they are working on | part of `sessionIntent` |

---

## 5. Domain objects

Every first-class domain object is C++; QML holds no domain objects or rules (Mark, 14 Nov 2026; DEC-01).

### 5.1 The tree

```
Athlete  (aggregate root · AthleteId)
 │  record: name, handedness, height, sex/dob, bag, …          ← source fact, athlete.json
 │  folder: where athlete.json is found; no folder key (DEC-151)
 │  tuning log: the golfer's label corrections, in order        ← source fact, outcome_corrections.jsonl
 │  sessions: SessionIndex (SessionId → a typed entry, status included: DEC-136)   ← index only; sessions load on demand
 └─ AthleteDiagnostics      derived, from SESSION summaries

Session  (aggregate root · SessionId, a uuid minted at Start)
 │  identity: SessionIdentity (folder, start time, local day, n-of-day, type)   ← properties, never keys
 │  status: InProgress | Ended, start and end times             ← source, the session file
 │  clubBaseline: computed by the athlete level, frozen here    ← recorded input, club_baseline.json
 │  sessionIntent: declared focus (the declared work-on), declared miss, screen answers, intent runs   ← source fact
 ├─ shots: Shot*  (ShotRef = SessionId + swing number) · each Shot its own object, with its own commands,
 │               reachable only through its Session (P10)
 │    Recording (kind: captured | device-only) · analysis (recorded product)
 │    measurements: one set, each tagged with its source and its kind (reading | derived)
 │    review: reading exclusions · club correction · declared club · shotIntent · rating/note ·
 │            label corrections                                                  ← source facts
 │    effective club                                                              ← derived
│    derived data: outcome · its three factors · carry · flight check (DEC-121) ← derived: computed by the Shot, kept by its Session (DEC-135)
 │    findings: fired by the Shot itself (DEC-100)                                  ← derived: kept by its Session (DEC-135)
│    diagnostics: ShotDiagnostics (read-only view of this shot's findings and results)
 ├─ SessionDiagnostics      keeps the shots' findings and derived data (DEC-135); may hold the usuals (DEC-130);
 │                          diagnoses the session from its shots' findings; keeps the diagnosis
 └─ SessionSeries           derived, in memory only
```

### 5.2 Identities

The identity types live in `src/Identity`, in the Vocabulary ring (Mark, 15 Nov 2026; DEC-148).

| Id | Identity | Representation | Minted by, when | Persisted | Immutable | Notes |
|---|---|---|---|---|---|---|
| ID-ATHLETE | `AthleteId` | a uuid | `Athlete` creation (today `AthleteController::createAthlete`, `QUuid`) | `athlete.json` (today the settings group `athletes/<uuid>`), and in every swing header (`athlete.uuid`) | yes | The athlete's name and library folder are properties |
| ID-SESSION | `SessionId` | a uuid (Mark, 14 Nov 2026; DEC-41): random for a new session; for a session older than step 2, a name-based (v5) uuid of its athlete's id and its folder name (Mark, 15 Nov 2026; DEC-150) | `Athlete::startSession`, at Start (Mark, 14 Nov 2026; DEC-78); `Athlete::resumeSession` keeps it (DEC-77, DEC-134); an old session's is written by the step-2 migration (R-PERS-7) | the session file FILE-SESS-01, in the session's folder, with its status and start/end times | yes | The folder is a property only; its name comes from SET-04, and it never changes during the session's life: a library re-pointed mid-session ends the session and starts a new one (Mark, 15 Nov 2026; DEC-154). The v5 id: today `SessionIdentity::legacySessionId` (`src/DomainModel/session_identity.h`, step 2.1), which fixes its namespace and name string (the exact recipe: J-12, proposed, for Mark). |
| ID-SWING-NUMBER | the swing number | an integer ≥ 1, the `NNNN` of `swing_NNNN` | `Session::addShot`, before capture writes (Mark, 15 Nov 2026; DEC-142) (today `SwingPaths::allocateSwingDir`) | the folder name; the session's highest issued number in FILE-SESS-10 | yes | Unique within its session and never reissued, even after the shot is trashed; gaps stay (today's rule, kept (ratified by Mark, 14 Nov 2026; DEC-91)). "Shot N" always means swing number N. There is one allocator: a device-only shot goes into the running session; sessions already split are left as they are (Mark, 15 Nov 2026; DEC-157). |
| ID-SHOT | `ShotRef` | (`SessionId`, swing number) | with the shot | n/a: composed from the two above | yes | The only key of a shot outside its own folder. The type: today `src/Identity/identity.h` (step 2.1), with `AthleteId` and `SessionId` |
| ID-CLUB | a canonical club id | the club vocabulary's id (`club_vocabulary.h`) | n/a: vocabulary | wherever a club is named | yes | Labels come from one table (R-CLUB-5) |
| ID-RIG | the rig state | design: the platform refactor (capture) | design: the platform refactor (capture) | design: the platform refactor (capture) | design: the platform refactor (capture) | DEC-17 |
| ID-NORM-SET | a pack or norm set | its file name (today) | n/a: named by whoever writes the file | the file name | yes, while the file exists | The identity this page gave packs and norms at `108e7858`; the owner is OPEN (Q-02). EVT-12 and EVT-13 carry it. |

**Foreign ids and their translations.** Nothing outside its own adapter keys on a foreign id (ratified by Mark, 14 Nov 2026; DEC-90).

| Foreign id | Where | Translates to | Rule |
|---|---|---|---|
| The session folder name (e.g. `2026-06-05_Mark-Liversedge_Swing_01`) | the library; `DeviceOnlyMeta::sessionId`; `trashSession(sessionId)`; `restoreSession(sessionDir)` (a path) | `SessionId` through `SessionRepository` | Today the folder name **is** the key (debt, [§11](#11-where-the-code-is-today)). In the target only `SessionIdentity` and `SwingPaths` read it (P7, lint A3). |
| The swing folder name `swing_NNNN`; `RecordingHeader::Identity{id, index}`; `DeviceOnlyMeta::swingId` / `swingIndex` | the swing folder and header | the swing number of a `ShotRef` | One meaning: `id` is the folder name, `index` the swing number |
| `RecordingRepository`'s `swingDir` key | every repository call today | `ShotRef` | Step 2 re-keys the repository (debt) |
| PPCP's `peerId`, `sessionId`, `captureId`, `streamId` (`StreamOrigin`) | a phone clip's stream origin in the Recording | n/a: provenance only (today: `recording_repository.h`) | Nothing in `src/Analysis` keys on them (today: `recording_repository.h`); a PPCP "session" is a peer session, not a `Session` (§12, (ratified by Mark, 14 Nov 2026; DEC-118)) |
| PPCP annotation scope `session_id` / `shot_id` | FILE-SHOT-11 | design: the platform refactor (capture) | design: the platform refactor (capture) |
| The launch monitor's own shot number and session files | the device's reading, kept in the Recording as delivered | n/a: kept as delivered (today) | A device session is not a `Session` (§12, (ratified by Mark, 14 Nov 2026; DEC-118)) |

### 5.3 The objects

Role: aggregate root, entity, value, member (a part of the object named), index, view, or application state.
Fact kind: P1's source, recorded input, derived or recorded product.

**What each object is:**

| Id | Object | Role | Fact kind | Owner (root); level | Identity |
|---|---|---|---|---|---|
| OBJ-ATHLETE | `Athlete` | aggregate root | n/a: holds the facts below | itself; athlete | ID-ATHLETE |
| OBJ-ATHLETE-RECORD | Athlete record: name, handedness, height, weight, sex, dob, handicap, primary club, speed target, notes, the report's details (email, greeting, photo consent and its date), created at; the Wrist chart's reference swing, a `ShotRef`, one per athlete (Mark, 15 Nov 2026; DEC-152); the bag (OBJ-BAG) | member of `Athlete` | source | `Athlete`; athlete | the athlete's |
| OBJ-BAG | The bag: the athlete's clubs, each a `Club` record (canonical id, loft, lie, length, shaft, retro-band centres and band width, the taped-on date, hosel, the head patch, where the hands end, a note; today: `athlete_controller.cpp`) | member of the record | source | `Athlete`; athlete | per club: ID-CLUB |
| OBJ-TUNING-LOG | The tuning log: every label correction the golfer made, in order, including ones later overwritten (Mark, 14 Nov 2026; DEC-43) | member of `Athlete` | source | `Athlete`; athlete | n/a: one per athlete, append-only |
| OBJ-SESSION-INDEX | `SessionIndex`: `SessionId` → a typed entry, status included (Mark, 15 Nov 2026; DEC-136), with the session's summary (DEC-53); the entry's step-2 fields: step 2's brief (DEC-160: IM-S2-5) | index | derived | `Athlete`; athlete | n/a: one per athlete |
| OBJ-ATHLETE-DIAG | `AthleteDiagnostics` | derived owner | derived | `Athlete`; athlete | the athlete's |
| OBJ-SESSION | `Session` | aggregate root | n/a: holds the facts below | itself; session | ID-SESSION |
| OBJ-SESSION-IDENTITY | `SessionIdentity`: folder, start time, local day, n-of-day, type (the session type chosen at Start, which selects the analysis stage profile) | member (properties) | source | `Session`; session | n/a: properties of ID-SESSION |
| OBJ-SESSION-STATUS | `Session.status`: InProgress / Ended, start and end times | member | source | `Session`; session | n/a: an attribute |
| OBJ-CLUB-BASELINE | Club baseline: the athlete's per-club baselines as this session passes them down to its shots' findings | member | recorded input (Mark, 14 Nov 2026; DEC-21, DEC-46) | `Session`; session | n/a: one per session |
| OBJ-SESSION-INTENT | `sessionIntent`: declared focus (the declared work-on, GL-68: DEC-138), declared miss, screen answers, "from here on" intent runs | member | source | `Session`; session | n/a: one per session |
| OBJ-SHOT | `Shot` | entity: its own object (P10) | n/a: holds the facts below | `Session` holds it in its collection; the `Shot` owns and manages its own facts (P10); shot | ID-SHOT |
| OBJ-RECORDING | Recording (§2.2), with its analysis context (OBJ-ANALYSIS-CONTEXT, a recorded input) | member of `Shot` | source (the context: recorded input) | `Shot`; shot | the shot's |
| OBJ-ANALYSIS-CONTEXT | The analysis context (§2.2): handedness, height, club set, the captured club, the bag snapshot | member of the Recording | recorded input | `Shot`; shot | the shot's |
| OBJ-ANALYSIS | The analysis: stamped with its stage versions and inputs; window time | member of `Shot` | recorded product | `Shot`; shot | the shot's |
| OBJ-MEASUREMENT | Measurement `{metric, source instrument + route, kind, value, σ, gates}`, one set per shot | value, in a set the `Shot` holds | per kind (ratified by Mark, 14 Nov 2026; DEC-54): a **reading** (delivered by an instrument: LM, HackMotion) is source; a **derived measurement** (rules over the Recording's streams: camera, IMU, and the LM's inferred reads) is part of the recorded product | `Shot`; shot | n/a: (shot, metric, source) |
| OBJ-SHOT-REVIEW | The shot's review: reading exclusions, the club correction, the declared club, shot intent, rating, note, label corrections | member of `Shot` | source | `Shot`; shot | n/a: one per shot |
| OBJ-EFFECTIVE-CLUB | Effective club (R-CLUB-2, R-CLUB-3) | value | derived | `Shot`; shot | ID-CLUB |
| OBJ-SHOT-RESULTS | The shot's findings (layer 5 of M-SHOT-LAYERS) | fired by the `Shot` (Mark, 14 Nov 2026; DEC-100), 1:1 with the shot that fired them (DEC-101); a member of its `Session`'s collection (Mark, 15 Nov 2026; DEC-135) | derived | `Session`, which keeps them per shot (DEC-135, superseding DEC-102); shot | the shot's |
| OBJ-SHOT-DERIVED | The shot's derived data (layer 3 of M-SHOT-LAYERS): the shot outcome, the shot's tendencies (its three factors: strike · direction · flight), carry against the usual, and the flight-check overlay, computed for the shot with the session's usuals passed down (Mark, 14 Nov 2026; DEC-121); (Mark, 14 Nov 2026; DEC-122, superseding DEC-06) | computed by the `Shot` (P10); a member of its `Session`'s collection, with the shot's findings (Mark, 15 Nov 2026; DEC-135) | derived | `Session`, which keeps it per shot (DEC-135); shot | the shot's |
| OBJ-SHOT-DIAG | `ShotDiagnostics` | view: read-only, of OBJ-SHOT-RESULTS and OBJ-SHOT-DERIVED, exposed through the `Shot` | derived | n/a: a view owns nothing; it reads the findings and derived data its `Session` keeps for the shot (DEC-100, DEC-135); shot | the shot's |
| OBJ-SESSION-DIAG | `SessionDiagnostics`: diagnoses the session from its shots' findings (DEC-100); may compute and hold the usuals (GL-25; DEC-130); keeps the shots' findings and derived data for its `Session` (DEC-135) | derived owner | derived | `Session`; session | the session's |
| OBJ-SESSION-SUMMARY | Session summary: the one session-level product behind the session report. The report renders it, the athlete level reads it, and nothing in it is computed twice (Mark, 14 Nov 2026; DEC-81); it is `work_ons.json` (ratified by Mark, 14 Nov 2026; DEC-59). Its fields, as the starting list (ratified by Mark, 14 Nov 2026; DEC-92), which step 3 checks field by field against the client report: per shot, its `ShotRef`, effective club, outcome and three factors; per club, the usuals and carry today; the conditions with their tiers, links and reasons; the intent summaries; the session work-ons (GL-69; DEC-138) | member of `SessionDiagnostics`' results | derived | `SessionDiagnostics`; session | the session's |
| OBJ-SESSION-SERIES | `SessionSeries`: the session's measurements as series, one per session | derived owner, in memory | derived | `Session`; session | the session's |
| OBJ-PHASE-GRID | The shot's phase grid (the sampler's numbers per phase) | shot cache | derived | `Shot`, which writes it (DEC-103, superseding DEC-62); shot | the shot's |
| OBJ-MARKUP-TRUTH | The video markup's ground truth (FILE-SHOT-12) | member of `Shot` | source (Mark, 14 Nov 2026; DEC-83) | `Shot`; shot | the shot's |
| OBJ-REPORT | The session report: its PDF, its metadata and its stills, a rendering of the session summary (DEC-81) | product | derived, except its metadata's coach note and send log, which are source facts (Mark, 15 Nov 2026; DEC-140) | `Session`; session | the session's |
| OBJ-PACKS-NORMS | Packs and norms: the shipped core pack and norms; the user's pack, corridor and content edits; community sets; flight norms; the set of disabled norm sets | rule data | source (the user's edits); shipped rule data (read-only) | **OPEN (Q-02, step 3)**, its level too | ID-NORM-SET |
| OBJ-RIG | `Studio/Rig` (§2.3) | entity | source (calibration results), versioned | itself; design: the platform refactor (capture) | ID-RIG |
| OBJ-LIBRARY | The library: the root folder every repository uses; when SET-03 is empty, `<AppData>/swings` (today: `swing_paths.cpp`) | application setting | n/a: a place, not a fact | the app; application (per machine) | n/a: SET-03 |

**Lifecycle, persistence and the one writer:**

| Id | Created | Ends | Persisted in | One writer | Producer (derived only) |
|---|---|---|---|---|---|
| OBJ-ATHLETE | loaded by `AthleteRepository` when first asked for (the current athlete, or a headless service); one instance per `AthleteId` (ratified by Mark, 14 Nov 2026; DEC-52); a new one by `Athlete` creation | when its last holder releases it; `Athlete::delete` moves its whole folder to the recoverable trash (Mark, 14 Nov 2026; DEC-79); purged by housekeeping (DEC-80) ([§5.4](#54-lifecycles)) | FILE-ATH-01 | `Athlete` via `AthleteRepository` | n/a: not derived |
| OBJ-ATHLETE-RECORD | with the athlete | with the athlete | FILE-ATH-01 (settings keep only "current athlete", DEC-05) | `Athlete` | n/a: not derived |
| OBJ-BAG | with the athlete | with the athlete | FILE-ATH-01 | `Athlete` | n/a: not derived |
| OBJ-TUNING-LOG | on the first label correction | with the athlete; never deleted with the caches | FILE-ATH-04 | `Athlete`, appended on `LabelCorrected` (DEC-43) | n/a: not derived |
| OBJ-SESSION-INDEX | with the athlete | with the athlete | n/a: built from each session's summary, read through `SessionRepository` (ratified by Mark, 14 Nov 2026; DEC-53) | n/a: not persisted | `Athlete`, from `SessionRepository` |
| OBJ-ATHLETE-DIAG | with the athlete | with the athlete | FILE-ATH-02, FILE-ATH-03; the athlete-level baselines and distances: **OPEN (Q-06, step 3)** | `AthleteDiagnostics` via `AthleteRepository` | the athlete reductions over session summaries |
| OBJ-SESSION | loaded by `SessionRepository` when first asked for; `Athlete::startSession` creates a new one (DEC-78); one instance per `SessionId` | when its last holder releases it (ending changes its status, not its life); archived and restored by command; resumed by `Athlete::resumeSession` (DEC-77, DEC-134); trashed by `Athlete::trashSession` (DEC-71); purged by housekeeping (DEC-80) ([§5.4](#54-lifecycles)) | its session folder | `Session` via `SessionRepository` | n/a: not derived |
| OBJ-SESSION-IDENTITY | computed once, at load or Start | with the session | FILE-SESS-01 (the start time and the type (ratified by Mark, 14 Nov 2026; DEC-88)), the folder name, and each shot's `capture.sessionType`, the type it was captured under (DEC-88) | `SessionIdentity` (only it and `SwingPaths` read a folder name, P7) | n/a: properties |
| OBJ-SESSION-STATUS | at Start (InProgress); Ended by command; InProgress again by `Athlete::resumeSession` (DEC-77, DEC-134) | with the session | FILE-SESS-01 (Mark, 14 Nov 2026; DEC-41) | `Session` | n/a: not derived |
| OBJ-CLUB-BASELINE | frozen once at first need (never at Open) from the athlete level's computation (DEC-46) | never regenerated, never deleted with the caches; rewritten only when the baseline rule's version is newer, as a recorded event naming the sessions it read (EVT-16) | FILE-SESS-04 | `Session` via `SessionRepository` | the athlete level (`AthleteDiagnostics`) computes it; the `Session` freezes it (DEC-46) |
| OBJ-SESSION-INTENT | by command | with the session | FILE-SESS-02 | `Session` | n/a: not derived |
| OBJ-SHOT | at capture (`Session` adds it), or with its session on load | trashed by command, or with its session; purged by housekeeping (DEC-80) ([§5.4](#54-lifecycles)) | its swing folder `swing_NNNN/` | written once by capture, then only by the `Shot`, through `RecordingRepository` (R-REC-1; DEC-86) | n/a: not derived |
| OBJ-RECORDING | at capture, in stages (R-REC-2) | never regenerated; moved (not deleted) by the archive and the trash; deleted only by the housekeeping purge (DEC-80). Raw frames the archive drops when SET-14 is off are a permanent delete, shown with a warning (Mark, 15 Nov 2026; DEC-137); the OS emptying its own trash: **OPEN (Q-29, step 3)** | FILE-SHOT-01 (the context: its `analysisContext` block), FILE-SHOT-03…06 | capture, through `RecordingRepository` (R-REC-2) | n/a: not derived |
| OBJ-ANALYSIS-CONTEXT | at capture, once; for a document older than the block, built once by the repository's reader from its legacy fields | never regenerated; changed after capture only by an approved one-off migration (the step-1 backfill, DEC-34): follow-club never overwrites its captured club (Mark, 15 Nov 2026; DEC-133), and today's overwrite is a known interim until step 6 | FILE-SHOT-01, its `analysisContext` block | capture, through the one header writer (DEC-24); follow-club's device club is written beside the captured club by `Shot::completeCapture`, a late part (DEC-133, DEC-103) | n/a: a recorded input |
| OBJ-ANALYSIS | by the analysis pipeline | refreshed by re-analysis | FILE-SHOT-01 | the `Shot` (`applyAnalysis`), from the pipeline's posted result (DEC-103, superseding DEC-94's earlier placement on the `Session`); until step 3, today's callers through `RecordingRepository` (R-REC-3) | the analysis pipeline (R-ANA-3) |
| OBJ-MEASUREMENT | readings as delivered (capture); derived measurements by the measurement rules | with the shot | FILE-SHOT-01 | readings: capture (R-REC-2); derived measurements: with the analysis, as OBJ-ANALYSIS | derived measurements: the measurement rules, run by the pipeline |
| OBJ-SHOT-REVIEW | by command; capture seeds the declared club (until step 6) and the pre-filled shot intent | with the shot | FILE-SHOT-02 (label corrections and shot intent: DEC-13) | the `Shot`, by its own commands (P10; DEC-94) | n/a: not derived |
| OBJ-EFFECTIVE-CLUB | computed on use | n/a: not stored | n/a: not stored (a copy is the analysis' input stamp, R-ANA-5) | n/a: not stored | `effectiveContext()` (analysis); the one club rule (R-CLUB-2) |
| OBJ-SHOT-RESULTS | fired by the `Shot` (Mark, 14 Nov 2026; DEC-100), by the assessment of its analysis and derived data against the diagnostic graph (Mark, 14 Nov 2026; DEC-121; DEC-132), and posted, stamped, to its `Session` (DEC-135) | with the shot | FILE-SESS-03, the `Session`'s collection, as today (Mark, 15 Nov 2026; DEC-135, superseding DEC-102) | the `Session`'s `SessionDiagnostics`, FILE-SESS-03's writer, from what the `Shot` posts (DEC-135) | the assessment, `fireFindings()` (DEC-121) |
| OBJ-SHOT-DERIVED | computed by the `Shot` when its analysis lands, its club's usuals change, or its label is corrected (Mark, 15 Nov 2026; DEC-140, DEC-130, DEC-131), and posted, stamped, to its `Session` (DEC-135) | with the shot | FILE-SESS-03, with the shot's findings, kept by the `Session` (DEC-135, which moves DEC-124); recomputed when its inputs change; readers never recompose it (Mark, 14 Nov 2026; DEC-124); today recomposed per consumer (C-01, G-03), the flight-check verdict written into the rows (C-02) | as OBJ-SHOT-RESULTS (DEC-135) | the shot rules (Coaching: outcomes, factors, carry, the flight check), with the session's usuals passed down (DEC-121, DEC-122) |
| OBJ-SHOT-DIAG | with its `Shot`'s findings and derived data (DEC-100, DEC-121) | with the session | n/a: a view | n/a: a view writes nothing | n/a: a view |
| OBJ-SESSION-DIAG | with the session; always runs (every live shot fires its findings, panel or not: DEC-02, DEC-100) | with the session | FILE-SESS-03, FILE-SESS-05 | `SessionDiagnostics` via `SessionRepository` | the session's diagnosis from its shots' findings (DEC-100) |
| OBJ-SESSION-SUMMARY | by `SessionDiagnostics` | with the session | FILE-SESS-05 (ratified by Mark, 14 Nov 2026; DEC-55, DEC-59) | `SessionDiagnostics` | the session reduction |
| OBJ-SESSION-SERIES | with the session | with the session | n/a: in memory only, never persisted | n/a: not persisted | from the shots' measurements |
| OBJ-PHASE-GRID | on first need (today) | rebuilt when stale, stamped by its source document (today) | FILE-SHOT-07 | the `Shot` via `RecordingRepository` (DEC-103, superseding DEC-62) | the phase-grid sampler (`buildPhaseGrid`, `Diagnostics/measures`) |
| OBJ-MARKUP-TRUTH | by the markup tool | with the shot | FILE-SHOT-12 | the `Shot` (P10, DEC-103), through `RecordingRepository`; its command is step 3's brief | n/a: not derived |
| OBJ-REPORT | by the report command | replaced by the next report, except its metadata's coach note and send log, which are kept across remakes (Mark, 15 Nov 2026; DEC-140) | FILE-SESS-07…09 | `Session` (the report is a `Session` command, DEC-22) via `SessionRepository` | the report builder |
| OBJ-PACKS-NORMS | loaded once per change into an immutable snapshot (R-CON-4) | n/a: application lifetime | FILE-APP-01…05; SET-06 | **OPEN (Q-02, step 3)**; today the pack editor, the corridor editor and the content editors, through `atomicWrite` | n/a: source and shipped data |
| OBJ-RIG | design: the platform refactor (capture) | design: the platform refactor (capture) | design: the platform refactor (capture) | design: the platform refactor (capture) | n/a: source |
| OBJ-LIBRARY | the user sets it | n/a: per machine | SET-03 | the settings screen | n/a: a setting |

**How a view reaches it, and what it signals** (views read through `app`, [§9.2](#92-the-qml-boundary)):

| Id | View path | Emits | Consumes |
|---|---|---|---|
| OBJ-ATHLETE | `app.athlete` (= `app.state.currentAthlete`) | EVT-14, EVT-17, EVT-20, EVT-23 (DEC-134) | EVT-06 (appends the tuning log) |
| OBJ-ATHLETE-RECORD, OBJ-BAG | through `app.athlete` | EVT-14 (with `Athlete`) | n/a: changed only by command |
| OBJ-TUNING-LOG | n/a: not shown; read by the outcome tuning tools | n/a: none | EVT-06 |
| OBJ-SESSION-INDEX | through `app.athlete` | n/a: no event of its own | every event whose chain reaches the session summary (§7.4); EVT-17…20 and EVT-23 (an entry added, archived, restored, removed, resumed) |
| OBJ-ATHLETE-DIAG | through `app.athlete` | n/a: change signals only | marked stale by every event whose chain ends at the athlete level (§7.4; R-DER-4) |
| OBJ-SESSION | `app.session` (= `app.state.sessionOnScreen`) | EVT-01, 03, 08…10, 16, 18, 19 (EVT-11 removed: DEC-154; EVT-23 is the `Athlete`'s: DEC-134); it hears its shots' events (P10) | EVT-12, 13 (through `SessionDiagnostics`) |
| OBJ-SESSION-IDENTITY, OBJ-SESSION-STATUS | through `app.session` | EVT-09 (with `Session`) | n/a: changed only by command |
| OBJ-CLUB-BASELINE | through `app.session` | EVT-16 (with `Session`) | n/a: frozen; never EVT-14 |
| OBJ-SESSION-INTENT | through `app.session` | EVT-08, 10 (with `Session`) | n/a: changed only by command |
| OBJ-SHOT | `app.state.selection.shot`, `app.state.castSelection.shot` (each resolved through its session, P10) | EVT-02, 04…07, 21, 22, from its own commands (P10; DEC-103) | n/a: its own commands change it |
| OBJ-RECORDING | through the shot; played through `app.state.replay` | EVT-22 (with its `Shot`) | n/a: changes only by its late parts (R-REC-2) |
| OBJ-ANALYSIS-CONTEXT | n/a: read by the job builder only | n/a: none | n/a: never EVT-14 (a recorded input) |
| OBJ-ANALYSIS, OBJ-MEASUREMENT | through the shot; as series through `SessionSeries` presenters | EVT-02, 22 (with its `Shot`) | marked stale by every event that changes the effective context: EVT-04, EVT-21, and EVT-22 when follow-club brought a device club (R-ANA-1, R-ANA-5; DEC-133) |
| OBJ-SHOT-REVIEW | through the shot | EVT-04, 05, 06, 07, 21 (with its `Shot`) | n/a: changed only by command |
| OBJ-EFFECTIVE-CLUB | through the shot | n/a: derived | recomputed on EVT-04, EVT-21, EVT-22 |
| OBJ-SHOT-RESULTS, OBJ-SHOT-DIAG | `app.state.selection.shot.diagnostics` | change signals of its `Shot` | every event whose chain re-fires the shot's findings (§7.4) |
| OBJ-SHOT-DERIVED | `app.state.selection.shot.diagnostics` (factors, carry: §9.2) | change signals of its `Shot` | every event whose chain recomputes the shot's derived data (§7.4) |
| OBJ-SESSION-DIAG, OBJ-SESSION-SUMMARY | through `app.session` | change signals | every event whose chain reaches the session reduction (§7.4) |
| OBJ-SESSION-SERIES | presenters (LM board, Charts, report) over `app.session` | change signals | EVT-01, 02, 03, 05, 22 (ratified by Mark, 14 Nov 2026; DEC-109) |
| OBJ-PHASE-GRID | n/a: read by presenters through the session | n/a: none | rebuilt when its stamp is stale |
| OBJ-REPORT | through `app.session` | n/a: none (no consumer: DEC-70) | n/a: a product |
| OBJ-PACKS-NORMS | the pack and corridor editors | EVT-12, 13 | n/a: changed by the editors |
| OBJ-RIG | design: the platform refactor (capture) | EVT-15 | design: the platform refactor (capture) |
| OBJ-LIBRARY | settings | n/a: none; re-pointed mid-session, the session ends and a new one starts (Mark, 15 Nov 2026; DEC-154) | n/a: a setting |

### 5.4 Lifecycles

| Id | Object | States | Commands between them (event) |
|---|---|---|---|
| LC-ATHLETE | `Athlete` | active → trashed → purged | `create` (mints `AthleteId`; no event: OPEN (Q-16)); `editRecord`, `editBag` (EVT-14); `delete`, which moves the whole athlete folder, its sessions included, to the recoverable trash (Mark, 14 Nov 2026; DEC-79), and is refused while one of its sessions is InProgress (ratified by Mark, 14 Nov 2026; DEC-99) (no event: Q-16); the purge, by housekeeping (DEC-80). Today `AthleteController::createAthlete` mints the uuid and `deleteAthlete` removes only the settings group, leaving the folder and its sessions behind (today). |
| LC-SESSION | `Session` | InProgress ⇄ Ended → Archived ⇄ restored; trashed → purged | `Athlete::startSession`, which mints the `SessionId`, writes FILE-SESS-01 and first ends any session of the athlete still InProgress (Mark, 14 Nov 2026; DEC-78, DEC-73; EVT-17); `end` (EVT-09); `Athlete::resumeSession`: an Ended session goes back to InProgress with the same `SessionId`, and `end` ends it again (Mark, 14 Nov 2026; DEC-77; EVT-23); only a session started today may be resumed, and only for the same session type, as today's extend requires; any other InProgress session of the athlete is ended first (Mark, 15 Nov 2026; DEC-134; the remaining risk: a session that crashed late at night cannot be resumed the next morning, so a new one starts); `archive` (EVT-18) and `restore` (EVT-19) (DEC-22, DEC-70); `Athlete::trashSession` (EVT-20; DEC-71); the purge, by housekeeping (DEC-80). A session left InProgress by a crash stays InProgress until `end` or the athlete's next `startSession` (DEC-73). Today `SwingPaths::beginSession(…, extendExisting)` reopens today's latest session folder, and `SwingPaths::endSession` trashes a new session that is still empty (today). The empty-session discard is a rule of this lifecycle (ratified by Mark, 14 Nov 2026; DEC-93): `Session::end` on an empty session asks `Athlete::trashSession`, and EVT-20 fires (Mark, 15 Nov 2026; DEC-143). |
| LC-SHOT | `Shot` | capturing (late parts pending) → complete → archived stub → restored; trashed → purged (ratified by Mark, 14 Nov 2026; DEC-93) | added to its session's collection by `Session::addShot` (EVT-01); `Session::addShot` allocates the next swing number (FILE-SESS-10), creates the folder through `RecordingRepository::create` and returns the `Shot`, which capture then writes into: allocate → create → capture writes → late parts (Mark, 15 Nov 2026; DEC-142); its late parts by `Shot::completeCapture` (R-REC-2; EVT-22) and its re-analysis by `Shot::applyAnalysis` (EVT-02), both the `Shot`'s (DEC-103); removed from the collection by `Session::trashShot` (EVT-03), to the OS trash or FILE-LIB-01; the purge, by housekeeping (DEC-80). A trashed shot's swing number is never reissued (DEC-91). |

**Housekeeping** (Mark, 14 Nov 2026; DEC-80) is the one application service that ends things for good. It has one `purge` command; SET-14 off is documented as a permanent delete of raw frames and shown with a warning
(Mark, 15 Nov 2026; DEC-137); the OS emptying its own trash is **OPEN (Q-29, step 3)**. "Purged" is the end state of
LC-ATHLETE, LC-SESSION and LC-SHOT, and nothing else deletes a source fact permanently, except the archive's raw-frame
drop when SET-14 is off (DEC-137). Its
settings are SET-10…SET-14. It changes facts only through the roots' commands (P3; DEC-42). Today
`ArchiveController::runHousekeeping` and `emptyTrash` remove trash entries older than SET-10, and archive sessions
by age (SET-11) or when free space falls below SET-12, into SET-13, keeping raw frames according to SET-14 (today:
`archive_controller.cpp`).

The archive copies a session to FILE-LIB-02 and verifies every copy byte for byte before it thins any shot to a
stub (FILE-SHOT-10). A restore merges each shot's review three ways, so no edit the golfer made while it was
archived is lost (today: `session_archiver.*`, 108e7858; the rule is R-PERS-8).

### 5.5 What each class is for

Enough to write its header; anything an implementer would still have to choose is named as an open question. The
command names in this table and the Producer column of §7.4 are ratified as names (Mark, 14 Nov 2026; DEC-94), and
step 3 may rename them with approval. Mark split them: the shot-local commands are the `Shot`'s, the session-wide
ones the `Session`'s, the athlete's the `Athlete`'s. `applyAnalysis` and `completeCapture` are the `Shot`'s (DEC-103,
which supersedes DEC-94's earlier listing of them under the `Session`).

| Id | Class | Responsibilities | Invariants | Commands (event) | Never |
|---|---|---|---|---|---|
| CLS-ATHLETE | `Athlete` | Holds the record, the bag, the tuning log and the `SessionIndex`; owns `AthleteDiagnostics`; starts and resumes its sessions (DEC-78, DEC-134). Its step-2 form (record, bag, index, `remove()`, `recordChanged` as the interim for EVT-14) is step 2's brief's (Mark, 15 Nov 2026; DEC-160: IM-S2-5) | One instance per `AthleteId` (the identity map); its folder is where its `athlete.json` is found, with no `folder` key, kept on rename, never derived from the name (DEC-04; Mark, 15 Nov 2026; DEC-151); at most one of its sessions is InProgress, which `startSession` and `resumeSession` enforce (DEC-78, DEC-134); the record holds source facts only, so today's `lastSessionAt` and `sessionCount` are derived from the `SessionIndex` (ratified by Mark, 14 Nov 2026; DEC-89) | `create`; `editRecord`, `editBag` (EVT-14); `startSession` (EVT-17; DEC-78); `resumeSession` (EVT-23; DEC-77, DEC-134); `trashSession` (EVT-20; DEC-71); `appendTuningLog` on EVT-06 (DEC-43); `delete` (DEC-79; no event: Q-16) | Read shot findings; change a session's recorded inputs (P1); be re-pointed at another athlete (a switch replaces it, R-APP-1) |
| CLS-SESSION | `Session` | Holds its collection of shots (adding and trashing them, swing numbers, order), each shot's findings and derived data as the `Shot` posts them (DEC-135), the context they were taken in (identity, type, status, intent run, session intent, club baseline), its derived owners and its session-wide results; hears every change to a shot through the shot's events (P10); asks each shot to re-fire its findings on a corridor or norm edit (DEC-100) | Swing numbers unique, increasing, never reissued (DEC-91); its status is persisted (DEC-41), so a crash leaves it InProgress until `end` or the athlete's next `startSession` (DEC-73) | `end` (EVT-09; on an empty session it asks `Athlete::trashSession`, EVT-20: DEC-143); `addShot` (EVT-01; DEC-142); `trashShot` (EVT-03); `setIntentRun` (EVT-08); `setSessionIntent` (EVT-10); `freezeClubBaseline` (EVT-16 on a rewrite); `archive` (EVT-18), `restore` (EVT-19), `saveReport` (Mark, 15 Nov 2026; DEC-140) (the report command: DEC-22) (DEC-94). `applyAnalysis` and `completeCapture` are the `Shot`'s (DEC-103); resuming is the `Athlete`'s `resumeSession` (DEC-134); `relocate` is removed (DEC-154) | Own, write or command a shot-specific fact (P10), except keeping the findings and derived data its shots post (DEC-135); read another session's facts except a typed input (its baseline); write an athlete file; fire a shot's findings itself (DEC-100) |
| CLS-SHOT | `Shot` | Its own object (P10; DEC-95): holds its Recording, analysis, measurements and review, and manages them with its own commands; fires its own findings from its analysis and the context its `Session` passes down, and exposes them through `ShotDiagnostics` (DEC-100); it posts its findings and derived data, stamped, to its `Session`, which keeps them (Mark, 15 Nov 2026; DEC-135). It is a QObject, a child of its `Session`, exposed to QML directly and typed (P9), and reads the snapshot it fires against (club baseline, pack, norms) from its parent (Mark, 15 Nov 2026; DEC-141). How a re-fire on a worker reads that snapshot as an immutable copy and posts its results to the GUI thread (R-CON-2, R-CON-3) is step 3's design point (DEC-141) | Obtainable only from its `Session`; its kind (captured or device-only) never changes; every change is saved through the repository (one writer), evented in command order, and propagated (re-firing findings, the summary) (ratified by Mark, 14 Nov 2026; DEC-95). The `Shot` saves and emits its own changes; its `Session` hears them and propagates them (DEC-103, which supersedes DEC-95's "saved, evented … by that `Session`") | `rate`, `note` (no event: DEC-70); `correctClub` (EVT-04); `excludeReading` (EVT-05); `correctLabel` (EVT-06); `setShotIntent` (EVT-07); `setDeclaredClub` (EVT-21) (DEC-94); `fireFindings`, which its `Session` calls (DEC-100); `applyAnalysis` (EVT-02) and `completeCapture` (EVT-22) (DEC-103) | Be reached except through its `Session`; be keyed by folder; diagnose its session (that is `SessionDiagnostics`', DEC-100) |
| CLS-RECORDING | Recording | The contract (§2.2) | Written once by capture, in stages (closed list R-REC-2), then by its `Shot` (DEC-86); the analysis context never changes after capture except by an approved one-off migration (R-REC-1); follow-club stores its device club beside the captured club and never overwrites it (DEC-133; today's overwrite a known interim until step 6) | n/a: capture writes it | Carry anything Analysis may not know (§2.2) |
| CLS-SESSION-DIAG | `SessionDiagnostics` | Diagnoses the session from its shots' findings: tiers, patterns, links, reasons, per-club mixes and the summary (Mark, 14 Nov 2026; DEC-100); may compute and hold the session's usuals (GL-25; Mark, 15 Nov 2026; DEC-130); keeps, for its `Session`, each shot's findings and derived data as the shots post them, stamped, in FILE-SESS-03 (DEC-135); runs the session reduction; keeps the session summary (DEC-81); holds, as an immutable snapshot, the pack, the norms and the session's club baseline it passes down to the shots' findings (ratified by Mark, 14 Nov 2026; DEC-110). Its memory cost when every loaded session always runs in a sweep is OPEN (Q-18, step 3) | One per session; always runs (DEC-02); results stamped (R-STAMP-1) | n/a: driven by events; results posted to it (R-CON-3) | Write a shot's `review` or Recording; fire a shot's findings (the `Shot` does, DEC-100); read another session (ratified by Mark, 14 Nov 2026; DEC-110); build golfer-facing sentences (R-DER-9) |
| CLS-SHOT-DIAG | `ShotDiagnostics` | A read-only view of one shot's findings and results, reached through the `Shot` | Never outlives its session; before its findings are fired it has no value (shown "-"), never a zero (ratified by Mark, 14 Nov 2026; DEC-96) | n/a: read-only | Own or cache anything |
| CLS-ATHLETE-DIAG | `AthleteDiagnostics` | Runs the athlete reductions over session summaries: tendencies, athlete work-ons (GL-70; DEC-138), themes, baselines (including each session's club baseline before the `Session` freezes it, DEC-46), distances, consistency, intent consistency | Derived on demand: an event marks it stale and it recomputes when next read (ratified by Mark, 14 Nov 2026; DEC-60). It reads session summaries through `SessionRepository` (the `SessionIndex` holds them), never shots | n/a: driven by events; the athlete catch-up (an application service) asks it to recompute and persist its caches (04 step 3: the catch-up is cut over "via `AthleteDiagnostics`") | Append the tuning log (that is the `Athlete`'s, DEC-43); rerun a session's reduction; read shot findings |
| CLS-SESSION-SERIES | `SessionSeries` | The session's measurements as series, one per session; the presenters filter it by source. A series is metric × source × shot (ratified by Mark, 14 Nov 2026; DEC-109) | Recomputed when a shot is added, re-analysed, trashed or completed, or a reading is excluded (ratified by Mark, 14 Nov 2026; DEC-109) | n/a: driven by events | Keep per-club statistics of its own (R-INS-4) |
| CLS-REPOSITORIES | Repositories | `RecordingRepository` (a shot's files in its swing folder; at `108e7858` this page already put the phase grid and the JSON-era files under it), `SessionRepository` (a session's files; an identity map), `AthleteRepository` (an athlete's files; an identity map, ratified by Mark, 14 Nov 2026; DEC-56). Each: the one door to its files (R-PERS-1), schema checked on read (R-PERS-3), atomic writes (R-PERS-2), the advisory locks for its folders (R-PERS-6) | Keyed by `ShotRef`, `SessionId`, `AthleteId` (today `RecordingRepository` is keyed by `swingDir`: debt); a newer schema is read-only and every write refuses it; a held lock or failed write is reported to the caller and surfaced in the app, never only logged (Mark, 15 Nov 2026; DEC-140) (04 step 2's triage, Mark 14 Nov 2026, covers a failed write: DEC-126) | Load, create, enumerate an athlete's sessions, the writes each file needs (ratified by Mark, 14 Nov 2026; DEC-111); `relocate` removed (DEC-154) | Be called by a view or a rule; spell another repository's file names |
| CLS-CLUB | Club and bag | `Club` (OBJ-BAG's entries and OBJ-EFFECTIVE-CLUB): canonical id plus geometry (length, shaft type, retro-band centres, hosel from butt, shaft length, where the hands end: today's `ClubGeometry`, `src/Recording/analysis_context.h`); the bag snapshot in the Recording is that geometry for every club in the bag | The club set (men / women / junior) is computed from the record: junior under 18 on the shot's date, else women's for "female", else men's (today: `athlete_controller.cpp`); standard lengths come from the club set and height (`club_standards.h`); labels from one table | `editBag` on the `Athlete` (EVT-14) | Default an undeclared club to DRIVER (R-CLUB-4) |
| CLS-PACKS-NORMS | Packs and norms | The shipped and user rule data findings are fired against; one immutable snapshot per change (R-CON-4); a norm's standing (provisional or decided, by whom, when) is a typed field (Mark, 14 Nov 2026; DEC-127) | Every findings stamp carries the pack's and norms' versions and content, the disabled sets (SET-06) and the grade policy (SET-15) in force when the findings fired, because they may change later (R-STAMP-1; Mark, 15 Nov 2026; DEC-140) | the editors' saves (EVT-12, EVT-13); owner and repository **OPEN (Q-02, step 3)** | Be re-parsed privately; be first built on a worker |
| CLS-RIG | Studio/Rig | §2.3 | design: the platform refactor (capture) | design: the platform refactor (capture) | Add a new place for rig facts before its design (§2.3) (Mark, 15 Nov 2026; DEC-140) |

### 5.6 "Live", "open" and "on screen" are not objects (Mark, 14 Nov 2026; DEC-11)

There is one kind of object, `Session`, and three different facts about it:

| Id | Fact | Kind | Where it lives |
|---|---|---|---|
| LIVE-1 | In progress or ended | Domain attribute, persisted, true without a screen | `Session.status` (FILE-SESS-01). "The athlete's live session" is a query: their session with status InProgress. At most one InProgress session per athlete (a domain rule). |
| LIVE-2 | Loaded in memory | Repository concern | `SessionRepository` is an identity map: one instance per id while anyone holds it. The panel, a catch-up, the report and a sweep get the same `Session`. `AthleteRepository` is one too (ratified by Mark, 14 Nov 2026; DEC-56). |
| LIVE-3 | On screen; being captured into | Application state | `AppState` ([§6](#6-application-state)) |

### 5.7 Instruments (Mark, 14 Nov 2026; DEC-15)

| Id | Rule | Forbids |
|---|---|---|
| R-INS-1 | The launch monitor is one device among the others. | A separate LM model or LM-only store |
| R-INS-2 | A shot has **one** set of measurements, each tagged with its source and its kind (reading or derived). P1 applies per kind. Readings are written by capture (R-REC-2); derived measurements with the analysis. | Parallel per-instrument tables of the same metric |
| R-INS-3 | One rule decides which source wins for a metric. | Precedence stated in a presenter or per surface |
| R-INS-4 | The LM board, Charts and the report are presenters over the session's measurements (`SessionSeries`), filtered by source. | A presenter keeping its own per-club statistics |

### 5.8 Club

Five terms, one meaning each:

| Term | Is | Stored |
|---|---|---|
| **Captured club** | The club capture recorded: the session's club at the shot, with its geometry from the bag, its length prior and its standard length. Empty = undeclared. A source fact, never overwritten (Mark, 15 Nov 2026; DEC-133). | The Recording's `analysisContext.club` (and `capture.club`, provenance) |
| **Device club** | The club the launch monitor named on a late reading, which follow-club (SET-08) stores beside the captured club as its own source fact; when follow-club is on, the effective-club rule prefers it to the captured club (Mark, 15 Nov 2026; DEC-133). | beside the captured club; until step 6 follow-club overwrites `analysisContext.club` instead, a known interim (DEC-133) |
| **Declared club** | The club named on the shot's swing-edit popover, `review.club`. Until step 6 capture also seeds it (the session club, else the athlete's primary club, else the `DRIVER` stub), and the rule takes a seed as a golfer's pick (DEC-31). Follow-club moves a seeded `review.club` today: a known interim until step 6 (DEC-82). | `review.club` |
| **Club correction** | The golfer's statement of the club really hit. Until step 6, only the LM board's override (DEC-31). | `review.clubOverride` |
| **Effective club** | The club every consumer uses, derived from the four above (R-CLUB-2, R-CLUB-3). Never stored. | n/a: derived |

| Id | Rule | Detail |
|---|---|---|
| R-CLUB-1 | Source facts | The captured club (kept in the Recording, never overwritten), the device club follow-club stores beside it, the declared club and the correction (in the shot's `review`) (Mark, 15 Nov 2026; DEC-133) |
| R-CLUB-2 | The one club, from step 6 | The correction wins, else the declared club. It is the one club for findings, every presenter and re-analysis (Mark, 14 Nov 2026; DEC-18). |
| R-CLUB-3 | **Until step 6**, field by field (Mark, 14 Nov 2026; DEC-26, DEC-31; `effectiveContext()` v1) | **With a correction** (one that names another club than the captured one): name = the correction; geometry = the bag snapshot's entry for it, when present; standard length = for the correction, the recorded club set and height; length prior = none. **Without one**: name = the captured club; geometry and length prior = the captured club's; standard length = the declared club's when declared (Mark, 14 Nov 2026: the popover's club keeps setting the standard length, as today), else the captured club's. The board's evidence suggestion is never a correction. When follow-club is on, the device club takes the captured club's place in this rule (DEC-133); today it does so because follow-club overwrites the captured club, the interim. **Known interim** (Mark, 14 Nov 2026; DEC-72): when only the popover names a club, the analysis uses the captured club's name while findings and presenters use the declared club (R-CLUB-2); accepted until step 6 removes the seed. |
| R-CLUB-4 | Undeclared | An undeclared club is *undeclared* and its findings are fired as "club assumed", never a hidden DRIVER |
| R-CLUB-5 | Label | From one table |

### 5.9 Commands go through the root

| Step | Who |
|---|---|
| 1. A use case calls a command on the root, or on a `Shot` reached through its `Session` for that shot's own facts ([§5.5](#55-what-each-class-is-for); P10), e.g. `Shot::correctLabel(…)`, `Shot::excludeReading()`; the archive and the report are `Session` commands (Mark, 14 Nov 2026; DEC-22). 03's examples, `Session::correctLabel(shotRef, …)`, are replaced by P10 | Application |
| 2. The object that owns the fact validates the change | `Session` / `Athlete` / `Shot` (P10) |
| 3. It is persisted through the repository | `Session` / `Athlete`; for a shot's facts, the `Shot` (DEC-103) |
| 4. A domain event is emitted ([§7.4](#74-events-and-what-they-recompute)), and the `Session` hears its shots' events | as step 3 |
| 5. The derived owners update | `SessionDiagnostics`, `AthleteDiagnostics`, `SessionSeries` |

R-CMD-1: views never write files or mutate findings. (The command was called `excludeShot` before v1.0; it excludes
one reading, not the shot: see "reading exclusion", [§12](#12-glossary).)

### 5.10 Headless use

| Id | Case | Rule |
|---|---|---|
| R-HL-1 | Within the app | Every consumer receives the instance already loaded (the identity maps). |
| R-HL-2 | Sweeps and tools (catch-up, `regrade_ledger`, swinglab) | Get their `Session` and `Athlete` from the same repositories, headless, on the tool's main thread. A domain object is the same whether a screen shows it or a sweep runs it. |
| R-HL-3 | Separate processes (the CLI tools) | Advisory lock files (R-PERS-6). |

---

## 6. Application state

Application state is what the app is doing now. It is not domain, and it is never persisted as domain data
(remembering the last screen or selection, if wanted, is an app preference). Selection is application state, and
`AppState` (OBJ-APPSTATE) lives in `src/App` (Mark, 14 Nov 2026; DEC-11).

```
AppState  (one per app, src/App)
 ├─ currentAthlete : Athlete          replaced on switch; a switch is refused while capture runs
 ├─ captureTarget : SessionId?        owned by the capture service; exposed here read-only
 ├─ sessionOnScreen : Session         the reviewed session, else the capture target
 ├─ mode : Capture | Replay | Analyse
 ├─ selection : ShotSelection         a ShotRef (or none), scoped to sessionOnScreen
 ├─ replay : ReplayState              what is playing, of the selected shot
 └─ castSelection : ShotSelection?    while capturing, the capture target's newest shot; with nothing
                                      being captured, a shot of the session on screen (which one: Q-07)
```

| Id | Object | Is |
|---|---|---|
| OBJ-APPSTATE | `AppState` | The members above; one per app; on the owner thread (R-CON-1) |
| OBJ-SHOT-SELECTION | `ShotSelection` | A `ShotRef` or none, resolved through its session; emits a change signal; clears itself when its session closes or is replaced |
| OBJ-REPLAY-STATE | `ReplayState` | What is playing of the selected shot, not a second selection (DEC-11); its members are left to step 4's brief (Mark rejected the proposed list, RAT-11, 14 Nov 2026) |

| Id | Member | Rule | Forbids |
|---|---|---|---|
| R-APP-1 | `currentAthlete` | Replaced on switch; an edit to the record is not a switch. A switch is refused while capture runs (an application rule). | Re-pointing app-lifetime objects at a new athlete by hand |
| R-APP-2 | `captureTarget` | Owned by the capture service; `AppState` exposes it read-only (ratified by Mark, 14 Nov 2026; DEC-57). That a session is in progress is the session's own attribute; which session capture writes into is application state. | Anything but the capture service setting it; capture writing anywhere but the session it asks |
| R-APP-3 | `sessionOnScreen` | The reviewed session, else the capture target. | Each view working out "the current session" for itself |
| R-APP-4 | `selection` | Like `QItemSelectionModel` beside an item model: refers to a shot by `ShotRef`, never owns it. Several views share one selection. Resolved through the session, never by folder name. Cleared by signal when its session closes or is replaced. | A view keeping its own selected shot; a selection outliving its session; matching by folder basename |
| R-APP-5 | `castSelection` | A second selection: the wall cast follows the newest shot being captured while the stage replays another. With nothing being captured, it shows the session on screen (Mark, 14 Nov 2026; DEC-22); which shot of it, the newest or the selection's, is **OPEN (Q-07, step 4)**. Same rules as `selection`. | The cast keeping a selection of its own outside `AppState` |
| R-APP-6 | `replay` | What is playing, of the selected shot. | A second "selected" shot |

### 6.1 What is not domain

| Id | Thing | Layer |
|---|---|---|
| NOTDOM-1 | Current athlete, capture target, session on screen, mode, selections, replay | Application state (`AppState`) |
| NOTDOM-2 | The library root (SET-03), the session naming pattern (SET-04), capture and housekeeping preferences (SET-07, SET-08, SET-10…14, SET-16) | Application settings (ratified by Mark, 14 Nov 2026; DEC-117) |
| NOTDOM-3 | A view's scroll position, open tab, expanded row, chart zoom | View state (the view's own) |
| NOTDOM-4 | A view-local helper: a filter proxy, a chart's metric helper | View state; may be created in QML |
| NOTDOM-5 | Last screen, last selection, the current athlete at launch (SET-02) | App preference |

---

## 7. Derived facts

### 7.1 The three levels

| Level | Owner | Inputs (explicit, typed) | Results | Cached in |
|---|---|---|---|---|
| Shot | the `Shot`, layer by layer (M-SHOT-LAYERS; DEC-121): its derived data, then the assessment that fires its findings (DEC-100); it posts both, stamped, to its `Session`, which keeps them (DEC-135); exposed through `ShotDiagnostics` (read-only) | derived data: the analysis, with the session's usuals passed down as a typed input. Assessment: the analysis and the derived data against the diagnostic graph (DEC-132), each number against its corridor (pack + norms, less the disabled sets SET-06, at the grade policy's thresholds SET-15), with the club baseline and the effective club its `Session` passes down | derived data: outcome; the shot's tendencies, strike · direction · flight; carry against the usual; the flight-check overlay (Mark, 14 Nov 2026; DEC-122, superseding DEC-06). Findings, as fired | findings and derived data: FILE-SESS-03, kept by the `Session`, as today (Mark, 15 Nov 2026; DEC-135, superseding DEC-102 and moving DEC-124) |
| Session | `SessionDiagnostics` diagnoses (Mark, 14 Nov 2026; DEC-100) | its shots' findings and derived data; for the usuals, its shots' readings (GL-25; Mark, 15 Nov 2026; DEC-130); `sessionIntent`; the shots' intent; the session's club baseline | the usuals (GL-25; DEC-130); the ledger, the `Session`'s collection of its shots' findings and derived data (DEC-135); links/tiers, mixes, carry today, reasons, intent summaries (consistency within the session, shot against session intent) (ratified by Mark, 14 Nov 2026; DEC-58), the session summary the athlete level reads | FILE-SESS-03, FILE-SESS-05 (the session summary) |
| Athlete | `AthleteDiagnostics`, derived on demand | session summaries (intent summaries included); the athlete record | tendencies, athlete work-ons (DEC-138), themes, baselines (each session's club baseline before it is frozen, DEC-46), distances, consistency, intent consistency across sessions | FILE-ATH-02, FILE-ATH-03; baselines and distances: Q-06 |

The result names are defined in [§12](#12-glossary).

| Id | Rule | Detail |
|---|---|---|
| R-DER-1 | Direction of reading | A level reads the level below as *results*; context flowing down (session usuals into a shot, the athlete baseline into a session) is a typed input. |
| R-DER-2 | The analysis pipeline never fires findings | Findings are the `Shot`'s. On a corridor or norm edit the `Session` asks each shot to re-fire, and a club correction re-fires that shot, without re-analysis (Mark, 14 Nov 2026; DEC-19, DEC-100); a label correction re-fires that shot (Mark, 15 Nov 2026; DEC-131). |
| R-DER-3 | Athlete level | Reads session summaries, never raw shots, and never reruns a session's reduction itself. It computes the club baseline a session then freezes (DEC-46). |
| R-DER-4 | Athlete level is lazy | Derived on demand and invalidated by events: an event marks it stale, and it recomputes when next read (ratified by Mark, 14 Nov 2026; DEC-60). The tuning log is appended by the `Athlete` root, not by it (DEC-43). |
| R-DER-5 | Flight check | Shot derived data (OBJ-SHOT-DERIVED), computed for the shot with the session's usuals passed down, like the outcome; it feeds the assessment that fires the shot's findings (Mark, 14 Nov 2026; DEC-122, superseding DEC-06). |
| R-DER-6 | Direction | The pack's corridors everywhere; there is no second direction classifier (DEC-07). |
| R-DER-7 | Held conditions | One rule: Your swing follows Work-ons (DEC-09). |
| R-DER-8 | Intent | Never changes a verdict: PPS reports what it saw. |
| R-DER-9 | Words | Results are ids and numbers. Golfer-facing sentences are built in the words module, never in a reduction, and never persisted. |
| R-DER-10 | Chains end at the athlete level | Every event whose chain changes a session summary continues → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112). |

### 7.2 The analysis is a recorded product

A shot's analysis is derived from its Recording, but it is expensive and produced by versioned stages, so it is
kept in the swing document as a cache with a stamp (`k*StageVersion` and inputs, the effective club among them).

| Id | Rule | Detail |
|---|---|---|
| R-ANA-1 | Stale is known | A changed stage, or a club correction, makes the analysis known-stale, visibly. |
| R-ANA-2 | Re-analysis reproduces live | Same Recording + same effective context (the effective club included) → same job. A corrected shot re-analyses with its corrected club (Mark, 14 Nov 2026; DEC-18). |
| R-ANA-3 | Produced by | The analysis pipeline (an application service). It produces the recorded product only, and never fires findings. |
| R-ANA-4 | Bumping | A change to a stage's output bumps its `k*StageVersion`. |
| R-ANA-5 | The input stamp | `analysis.versions.inputs` records the effective context the analysis used (the effective club, its length and source, height, club set, handedness (Mark, 14 Nov 2026; DEC-75); and the session type (ratified by Mark, 14 Nov 2026; DEC-88); `kAnalysisInputsVersion`). A shot whose effective context now differs from its stamp, or that has no stamp, is stale. A change of inputs moves no `k*StageVersion` (Mark, 14 Nov 2026; DEC-25, DEC-36). |
| R-ANA-6 | One time base | `analysis.timeBase: "window"`: the analysis is window-relative on disk, rebased once in the pipeline; a document without the key is read in one place, the repository's reader (Mark, 14 Nov 2026; DEC-25). |
| R-ANA-7 | Reuse | The reuse gate reuses a stage's product only when its stamp matches: the shaft track, the ladder and the DTL club track only when the stamp's club inputs match (Mark, 14 Nov 2026, step 1's brief; DEC-128). A pose can be reused only if it was saved (SET-16; today). Pose is re-run **only** when its stage version changes, and is reused whatever pose model file the machine has (Mark, 14 Nov 2026; DEC-64). A swing with no input stamp (every swing analysed before step 1) counts as analysed with its captured club, so only a corrected club re-tracks its shaft (Mark, 15 Nov 2026; DEC-144). A handedness change re-runs the shaft track and the ladder; the reused pose and DTL pose keep their keypoints with lead and trail swapped, never re-inferred, since handedness only labels the wrists (Mark, 15 Nov 2026; DEC-145). |
| R-ANA-8 | One ball-track rule | Live and re-analysis use one rule: a valid empty-mat baseline → BallRunner (Mark, 14 Nov 2026; DEC-35). |

### 7.3 Stamps

| Id | Rule | Detail |
|---|---|---|
| R-STAMP-1 | What a stamp holds | The version of every rule that produced the result, and a fingerprint of its inputs: norms, thresholds (the grade policy, SET-15), the disabled norm sets (SET-06) and the club baseline included. A findings stamp records the norm sets and the grade policy in force when the findings fired, because they may change later (Mark, 15 Nov 2026; DEC-140) |
| R-STAMP-2 | A stale stamp | The reader recomputes. It neither trusts the cache nor throws it away silently. |
| R-STAMP-3 | Rule versions | Hand-maintained constants declared beside the rule, and checked: a golden or parity test fails when a rule's output changes and its version did not (ratified by Mark, 14 Nov 2026; DEC-61) |
| R-STAMP-4 | A recorded input's rewrite | Only when its producing rule's version is newer (e.g. `kClubBaselineSchemaVersion`, which counts rule changes); the rewrite records the rule version, when, and what it read, and is an event (EVT-16) (Mark, 14 Nov 2026; DEC-46). A rule-version bump that would rewrite a recorded input needs Mark's approval first, proposed with a dry-run count (Mark, 14 Nov 2026; DEC-84) |

### 7.4 Events and what they recompute

Events `ShotTrashed`, `SessionEnded`, `IntentRunChanged`, `SessionRelocated` and `RigChanged` are Mark's (14 Nov
2026; DEC-22), and he removed `SessionRelocated`, EVT-11 (15 Nov 2026; DEC-154); EVT-16 follows from DEC-46. Every event is emitted by the object that ran the command (the `Shot` for its own commands, P10, DEC-103; the `Session`
hears them), on the owner thread, after the change is persisted (§5.9). **Delivery** (Mark, 14 Nov 2026; DEC-69): synchronous on the owner
thread, in command order, and an event carries only the id of what changed (P7); a consumer reads anything else
from the owner. EVT-17…EVT-22 and the split of `ShotAnnotated` into EVT-04 and EVT-05 are Mark's (14 Nov 2026;
DEC-70). A rating or a note emits no event: no derived fact reads them (DEC-70).

| Id | Event | Producer (command) | Carries | Recomputes |
|---|---|---|---|---|
| EVT-01 | `ShotAdded` | `Session::addShot` (DEC-142) | `ShotRef` | the shot computes its derived data and fires its findings; that club's usuals → every same-club shot recomputes its derived data and re-fires its findings (Mark, 15 Nov 2026; DEC-130) → the session reduction → the session summary → marks the athlete level stale |
| EVT-02 | `ShotReanalysed` | `Shot::applyAnalysis` (DEC-103) | `ShotRef` | the shot recomputes its derived data (DEC-140) and fires its findings; that club's usuals → every same-club shot recomputes its derived data and re-fires its findings (proposed, for Mark: the lead's call J-3, since a re-analysis can make a shot a part swing and so change its club's pool; DEC-130's list omits EVT-02) → the session reduction → the session summary → marks the athlete level stale |
| EVT-03 | `ShotTrashed` | `Session::trashShot` | `ShotRef` | that club's usuals → every same-club shot recomputes its derived data and re-fires its findings (Mark, 15 Nov 2026; DEC-130) → the session reduction → the session summary → marks the athlete level stale |
| EVT-04 | `ShotClubCorrected` (was `ShotAnnotated`, a club correction; DEC-70) | `Shot::correctClub` (DEC-94) | `ShotRef` | marks the shot's analysis stale; **re-fires** that shot's findings with the corrected club, without re-analysis; the usuals of each club whose pool the correction changes (the old club and the new one: the lead's call J-2, proposed, for Mark) → every shot of that club recomputes its derived data and re-fires its findings (Mark, 15 Nov 2026; DEC-130) → the session reduction; then → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112) |
| EVT-05 | `ShotReadingExcluded` (was `ShotAnnotated`, a reading excluded; DEC-70) | `Shot::excludeReading` (DEC-94) | `ShotRef` | the excluded reading leaves its club's usuals → every same-club shot recomputes its derived data and re-fires its findings (Mark, 15 Nov 2026; DEC-130) → the session reduction; then → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112) |
| EVT-06 | `LabelCorrected` | `Shot::correctLabel` (DEC-94) | `ShotRef` | that shot recomputes its derived data with the corrected label and re-fires its findings (Mark, 15 Nov 2026; DEC-131, correcting this chain to DEC-121) → the session reduction; the `Athlete` appends to its tuning log (Mark, 14 Nov 2026; DEC-43); then → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112) |
| EVT-07 | `ShotIntentChanged` | `Shot::setShotIntent` (DEC-94) | `ShotRef` | no verdict: the intent shown on the shot → the session's intent summaries; then → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112) |
| EVT-08 | `IntentRunChanged` | `Session::setIntentRun` | `SessionId` | pre-fills the intent of shots taken after the change; no shot already taken changes (DEC-20) |
| EVT-09 | `SessionEnded` | `Session::end` | `SessionId` | a status change: the session summary → marks the athlete level stale; an empty session is trashed instead, by `Athlete::trashSession` (EVT-20) (Mark, 15 Nov 2026; DEC-143) |
| EVT-10 | `SessionIntentChanged` (focus, declared miss) | `Session::setSessionIntent` | `SessionId` | the session reduction only; then → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112) |
| EVT-11 | `SessionRelocated`: **removed** (Mark, 15 Nov 2026; DEC-154) | n/a: removed with `Session::relocate`; a library re-pointed mid-session ends the session and starts a new one, same type and club, the declared focus carried | n/a: removed | n/a: removed; the id is not reused (§1.3) |
| EVT-12 | `NormsChanged` | the packs-and-norms owner, publishing a new snapshot (Q-02) | the set's id | each shot first recomputes its derived data where it reads the changed set (the flight check reads the flight norms, today: `strike_from_flight.h`; P5, DEC-122); the `Session` asks each shot to re-fire its findings through the stamp (rule version and input content) (DEC-100), at once, without re-analysis, never "after a relaunch"; then → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-112) |
| EVT-13 | `PackChanged` (a corridor edit among them) | as EVT-12 | the pack's id | as EVT-12 |
| EVT-14 | `AthleteRecordChanged` | `Athlete::editRecord`, `Athlete::editBag` | `AthleteId` | athlete-level facts only. **Not** a session's club baseline or any Recording's analysis context (recorded inputs) |
| EVT-15 | `RigChanged` | Studio/Rig | design: the platform refactor (capture) | design: with Studio/Rig in the platform refactor (capture) |
| EVT-16 | `ClubBaselineRewritten` (DEC-46, DEC-70) | `Session::freezeClubBaseline`, on a rule-version change | `SessionId` (the rule version and the sessions read are recorded in FILE-SESS-04) | the session's shots re-fire their findings → the session reduction → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-113) |
| EVT-17 | `SessionStarted` | `Athlete::startSession` (DEC-78) | `SessionId` | the `SessionIndex` gains the session; the capture service may set `captureTarget` (ratified by Mark, 14 Nov 2026; DEC-113) |
| EVT-18 | `SessionArchived` | `Session::archive` | `SessionId` | the `SessionIndex` entry shows it archived; nothing re-fires (ratified by Mark, 14 Nov 2026; DEC-113): the findings stay in the session's FILE-SESS-03, which stays in the library (Mark, 15 Nov 2026; DEC-135) |
| EVT-19 | `SessionRestored` | `Session::restore` | `SessionId` | the `SessionIndex` entry shows it restored (ratified by Mark, 14 Nov 2026; DEC-113) |
| EVT-20 | `SessionTrashed` | `Athlete::trashSession` (DEC-71) | `SessionId` | the `SessionIndex` loses the session → marks the athlete level stale; a selection or `sessionOnScreen` on it clears (R-APP-4) (ratified by Mark, 14 Nov 2026; DEC-113) |
| EVT-21 | `ShotDeclaredClubChanged` | `Shot::setDeclaredClub` (DEC-94) | `ShotRef` | the declared club sets the standard length, so the analysis' input stamp no longer matches: marks the analysis stale (R-ANA-5); re-fires that shot's findings with the effective club → the session reduction → the session summary → marks the athlete level stale (ratified by Mark, 14 Nov 2026; DEC-113) |
| EVT-22 | `ShotCaptureCompleted` | `Shot::completeCapture`, on a late part (R-REC-2: a late LM reading, follow-club, a phone clip) (DEC-103) | `ShotRef` | the shot fires its findings; that club's usuals → every same-club shot recomputes its derived data and re-fires its findings (Mark, 15 Nov 2026; DEC-130) → the session reduction → the session summary → marks the athlete level stale; `SessionSeries`; when follow-club brought a device club, the effective club changes and the analysis is stale (R-ANA-5; DEC-133) (ratified by Mark, 14 Nov 2026; DEC-113) |
| EVT-23 | `SessionResumed` (ratified by Mark, 14 Nov 2026; DEC-98) | `Athlete::resumeSession` (DEC-77, DEC-134) | `SessionId` | the status is InProgress again; the `SessionIndex` entry updates; its later shots follow EVT-01 (ratified by Mark, 14 Nov 2026; DEC-113) |

An athlete created or deleted emits no domain event: **OPEN (Q-16)**.

---

## 8. Persistence

Each file has exactly one writer, inside its aggregate; source facts change only through the root's commands; the
derived owners write their own caches (P3; Mark, 14 Nov 2026; DEC-42). Every file the app writes is listed here
with its writer today and its target owner (Mark, 14 Nov 2026; DEC-45); where the design is deferred it says
"design: the platform refactor (capture)" or "design: the platform refactor". A file not in this section is not
the app's to write (C3) (ratified by Mark, 14 Nov 2026; DEC-115). Where no decision names them, the fact kinds and target owners of
the files DEC-45 added (FILE-SHOT-03…06, 09, 11; FILE-SESS-07…12; FILE-LIB-01…05; FILE-APP-03, 06) are
(ratified by Mark, 14 Nov 2026; DEC-116). Until step 3 the session file and the session input are written by today's
callers through `SessionRepository`, and the athlete record by `AthleteController` through `AthleteRepository`:
R-REC-3's interim, extended (Mark, 15 Nov 2026; DEC-160: IM-S2-4).

Paths: `<library>` is SET-03; `<athlete>` is `<library>/<athlete folder>`; `<session>` is
`<athlete>/<session folder>`; `<swing>` is `<session>/swing_NNNN`; `<archiveRoot>` is SET-13.

### 8.1 Files

**A shot's files**

| Id | File | Fact kind | Target owner | Writer today | Target writer; repository | Step |
|---|---|---|---|---|---|---|
| FILE-SHOT-01 | `<swing>/swing.ppsw`: header, streams index, the Recording's `analysisContext` block (DEC-24), the shot kind, the top-level `shotKind` (`captured` / `deviceOnly`; DEC-44, DEC-147), the analysis (`analysis.versions.inputs`, `analysis.timeBase`, DEC-25), measurements (the LM's readings among them) | source + recorded input + recorded product | `Shot` | through `RecordingRepository` (R-REC-3); the archive stub through the public `SwingStore::save` (today) | capture (`create`), then the `Shot`, its late parts by `completeCapture` (DEC-86, DEC-103); `RecordingRepository` | 2 (`ShotRef` key; the stub); 3 |
| FILE-SHOT-02 | `<swing>/swing.ppsw`, `review` block: rating, note, the declared club, the club correction, reading exclusions, shot intent, label corrections (their keys `shotIntent` and `labelCorrections`: step 2's brief; DEC-160: IM-S2-3) | source | `Shot` | `RecordingRepository::editReview` from today's callers; `create` seeds the declared club; `completeCapture` moves a seeded `review.club` with follow-club (today; a known interim, DEC-82) | the `Shot`, by its own commands (P10; DEC-94); `RecordingRepository` | 2 (golfer input moved here); 3 |
| FILE-SHOT-03 | `<swing>/<alias>.mp4`, one per camera stream | source (streams) | `Shot` (Recording) | `SwingExporter` (capture) | capture, through the Recording store; design: the platform refactor (capture) | platform refactor (capture) |
| FILE-SHOT-04 | `<swing>/<alias>.raw` raw-frame sidecars, when SET-07 `saveRawFrames` is on; they cannot be regenerated | source (streams) | `Shot` (Recording) | `SwingExporter` | as FILE-SHOT-03 | platform refactor (capture) |
| FILE-SHOT-05 | `<swing>/thumb.jpg` | derived (from a stream) | `Shot` | `SwingExporter` | as FILE-SHOT-03 | platform refactor (capture) |
| FILE-SHOT-06 | `<swing>/<clip file>`: a phone clip (`StreamArrival::fileName`) | source (a stream) | `Shot` (Recording) | `PpcpClipFiler` (the bytes; the origin through `completeCapture`) | capture; design: the platform refactor (capture) | platform refactor (capture) |
| FILE-SHOT-07 | `<swing>/swing_phasegrid.json` | derived (shot cache, stamped by its source document's size and time) | `Shot` | the phase-grid sampler in `Diagnostics/measures/measure_sample.cpp` (file I/O in a Rules directory, lint A5), for whichever presenter asks; read across sessions by the Charts delivery presenter and the report, which in the target read other sessions' results through the `SessionIndex`, never their phase grids (Mark, 14 Nov 2026; DEC-74) | the `Shot` (DEC-103, superseding DEC-62); `RecordingRepository` | 3; 5 (the cross-session reads); 7 (the I/O leaves Rules) |
| FILE-SHOT-08 | `<swing>/swing.json` | source, JSON era: read for old swings, removed on the first rewrite to `.ppsw` | `Shot` | n/a: no writer; `convert` removes it | `RecordingRepository` (read only) | n/a: retired |
| FILE-SHOT-09 | `<swing>/swing_summary.json` | derived (regenerable cache of the JSON era; left out of the archive) | `Shot` | `RecordingRepository::writeSummarySidecar`, called by `SessionReviewController` | the `Shot` (P10); `RecordingRepository`. Retiring it is an IMC for step 3's brief | 3 |
| FILE-SHOT-10 | `<swing>/swing.ppsw` as an archive stub: the summary, metric and phase rows and the review, plus an `archive` block naming where the rest went (thumb kept) | source, moved: the streams go to FILE-LIB-02 first, every copy verified | `Shot` | `SessionArchiver`, through the public `SwingStore::save` (today) | `Session::archive` (DEC-22), which has each `Shot` write its stub (P10); `RecordingRepository`. The stub needs nothing for the findings: they stay in the session's FILE-SESS-03, which stays in the library (Mark, 15 Nov 2026; DEC-135) | 2; 3 |
| FILE-SHOT-11 | `<PPCP annotation root>/<session_id>/<shot_id>/annotations` | source (a user artefact: "markup notes", §12) | design: the platform refactor (capture) | `PpcpAnnotationStore` | design: the platform refactor (capture) (it sits in the shot's folder but outside the aggregate: placement is design: the platform refactor (capture)) | platform refactor (capture) |
| FILE-SHOT-12 | `<swing>/truth.json`, `<swing>/truth_dtl.json`: the video markup's ground truth | source (Mark, 14 Nov 2026; DEC-83) | `Shot` | the markup tool (`Gui/markup/markup_truth.cpp`) | the `Shot` (P10); `RecordingRepository`; moved with the shot by the archive and the trash | 2 |
| FILE-SHOT-13 | **Retired, never built** (Mark, 15 Nov 2026; DEC-135, superseding DEC-102 and moving DEC-124). It was to be a stamped shot cache in the swing folder holding the shot's derived data and findings | n/a: retired | n/a: retired; the `Session` keeps them in FILE-SESS-03 | n/a: never existed | n/a: retired | n/a: retired |

**A session's files**

| Id | File | Fact kind | Target owner | Writer today | Target writer; repository | Step |
|---|---|---|---|---|---|---|
| FILE-SESS-01 | `<session>/session.json`, visible (Mark, 15 Nov 2026; DEC-149): `SessionId`, status, start and end times (Mark, 14 Nov 2026; DEC-41); the session type (ratified by Mark, 14 Nov 2026; DEC-88); its keys, `origin` among them: step 2's brief (DEC-160: IM-S2-1) | source | `Session` | n/a: does not exist; status is in `SessionController`'s memory, the start time parsed from the folder name | `Session`; `SessionRepository` | 2 (file, id, migration); 3 (status by command) |
| FILE-SESS-02 | `<session>/session_input.json`: declared focus (the declared work-on, DEC-138), declared miss, screen answers, intent runs (its keys: step 2's brief; DEC-160: IM-S2-3) | source | `Session` | n/a: does not exist; held in the `diagnostics.json` envelope | `Session`; `SessionRepository` | 2 |
| FILE-SESS-03 | `<session>/diagnostics.json`: the `Session`'s collection of its shots' findings and derived data, each posted stamped by its `Shot`, and the session's diagnosis (Mark, 15 Nov 2026; DEC-135, superseding DEC-102); today also golfer input (H-04) | derived (cache); today it also holds golfer input (H-04) | `SessionDiagnostics` | `SessionDiagnosticsModel`, one per view; `regrade_ledger` (a tool) also writes a seed copy directly with `QSaveFile` (today) | `SessionDiagnostics`; `SessionRepository` (tools too, R-HL-2) | 2; 3 |
| FILE-SESS-04 | `<session>/club_baseline.json`: the frozen baseline, its rule version, and each rewrite (when, what it read) | recorded input | `Session` | `SessionDiagnosticsModel`, whichever instance has authority (D-06); `WorkOnsController` names it too | `Session` freezes the athlete level's computation (DEC-46); `SessionRepository` | 3 |
| FILE-SESS-05 | `<session>/work_ons.json`: the session summary | derived (cache) | `SessionDiagnostics` (ratified by Mark, 14 Nov 2026; DEC-55) | `WorkOnsController` (athlete level); read by the report list and the report | `SessionDiagnostics`; `SessionRepository` | 3 |
| FILE-SESS-06 | `<session>/archived.json` | marker: the session is archived | `Session` | `SessionArchiver` | `Session::archive` (DEC-22); `SessionRepository` | 2; 3 |
| FILE-SESS-07 | `<session>/report/report_meta.json` | derived (the report's metadata, read by the session list), except the coach's note and the send log, which are source facts kept across remakes (Mark, 15 Nov 2026; DEC-140; `report_meta.h`) | `Session` | the report (`report_meta.h`) | the `Session`'s report command; `SessionRepository`. From step 2 its name is spelled by `SessionFiles` and the report reads it through the repository; its writes move in step 5 (Mark, 15 Nov 2026; DEC-156) | 2 (name, read); 5 |
| FILE-SESS-08 | `<session>/report/<date>_<name>_session-report.pdf` | derived (product), replaced at each remake (DEC-140) | `Session` | the report model (`pdf_document_writer`) | as FILE-SESS-07 | 2 (name); 5 |
| FILE-SESS-09 | the report stills: today `<Cache>/report-stills/<hash>/`, outside the library | derived (cache) | `Session` | `report_stills.cpp` | saved with the report through the `Session` command (04 step 5) | 5 |
| FILE-SESS-10 | `<session>/.pinpoint-last-swing`: the highest swing number issued | source (identity bookkeeping, ID-SWING-NUMBER) | `Session` | `SwingPaths::allocateSwingDir` | the `Session` (it adds shots); `SessionRepository` | 2 |
| FILE-SESS-11 | `<session>/trunk_ceremony.json`, and `trunk_ceremony_<n>.json` for the earlier ceremonies of a resumed session: trunk-sensor calibration recordings | source (a calibration) | Studio/Rig | `TrunkCeremonyRecorder`, through `atomicWrite`, the earlier one copied aside (step 2.0), on a pool thread (P8 debt; today) | design: the platform refactor (capture) (it sits in the session's folder but outside the aggregate: placement is design: the platform refactor (capture)) | platform refactor (capture) |
| FILE-SESS-12 | `<session>/diagnostics.json.<tag>`: `regrade_ledger`'s backups | derived (a backup of a cache) | `SessionDiagnostics` | `regrade_ledger`, directly with `QSaveFile` (today) | through `SessionRepository` (R-HL-2) | 2 |

**An athlete's files**

| Id | File | Fact kind | Target owner | Writer today | Target writer; repository | Step |
|---|---|---|---|---|---|---|
| FILE-ATH-01 | `<athlete>/athlete.json`: the record and the bag; the athlete's folder is the one it is found in, with no `folder` key (Mark, 15 Nov 2026; DEC-151) | source | `Athlete` | `AthleteStore` (step 2.3), for an athlete created from step 2.3 on (and when a settings athlete is deleted, into the folder that goes to the trash); called by `AthleteController` | `Athlete`; `AthleteRepository` | 2 |
| FILE-ATH-02 | `<athlete>/tendencies.json` | derived (cache) | `AthleteDiagnostics` | `WorkOnsController` | `AthleteDiagnostics`; `AthleteRepository` | 3 |
| FILE-ATH-03 | `<athlete>/swing_themes.json`, `swing_themes.<scope>.json`, `swing_themes.<scope>.<slice>.json` (per club scope and outcome slice) | derived (caches) | `AthleteDiagnostics` | `WorkOnsController` | `AthleteDiagnostics`; `AthleteRepository` | 3 |
| FILE-ATH-04 | `<athlete>/outcome_corrections.jsonl`: the tuning log, append-only | source (Mark, 14 Nov 2026; DEC-43) | `Athlete` | `SessionDiagnosticsModel`, through `AthleteFiles::appendTuningLine` under the athlete folder's lock (step 2.3) | the `Athlete` root on `LabelCorrected`, never the session and never `AthleteDiagnostics`; `AthleteRepository`, under the athlete folder's lock (the app and a CLI tool may append at once) | 2 (lock); 3 |
| FILE-ATH-05 | `<athlete>/fault_profile.json` | n/a: to be deleted (H-11, D-07) | n/a: deleted | a writer with no caller; read on every activation | deleted with its reader | 3 |

**Library-level files**

| Id | File | Fact kind | Target owner | Writer today | Target writer; repository | Step |
|---|---|---|---|---|---|---|
| FILE-LIB-01 | `<library>/.pinpoint-trash/<athlete>/<stamp>_[<session>_]<name>`, used where the volume has no OS trash | source, moved (recoverable) | the owner of what was trashed: a shot, its `Session`; a session, its `Athlete` (DEC-71); an athlete, itself (DEC-79) | `SwingPaths::trashPath`, called by `ShotListModel::trashShots`, `SessionReviewController::trashSession` and `SwingPaths::endSession` (an empty new session) (today); purged by `ArchiveController` after SET-10 days (today) | `Session::trashShot`; `Athlete::trashSession` (DEC-71); `Athlete::delete` (DEC-79); purged only by housekeeping (DEC-80) | 2 (the athlete); 3 |
| FILE-LIB-02 | `<archiveRoot>/<athlete>/<session>/`: the archive mirror (raw frames included unless the caller says otherwise) | source, moved | `Session` | `SessionArchiver` (part file, then rename), started by the user or by housekeeping's age and space rules (SET-11…14) (today) | `Session::archive` (DEC-22), which housekeeping calls (DEC-80); `SessionRepository` | 2; 3 |
| FILE-LIB-03 | `<library>/ppcp-ledger.json`: the PPCP import ledger | source (import bookkeeping) | design: the platform refactor (capture) | `PpcpImportLedger` | design: the platform refactor (capture) | platform refactor (capture) |
| FILE-LIB-04 | `<library>/PPCP Imports/<peer>/<session>/<captureId>.<ext>`: imported phone clips | source (streams) | design: the platform refactor (capture) | the PPCP import sink (`ppcp_import_sink.cpp`) (today) | design: the platform refactor (capture) | platform refactor (capture) |
| FILE-LIB-05 | `<library>/PPCP Imports/ppcp-import.json`: the legacy import ledger | source (import bookkeeping) | design: the platform refactor (capture) | the PPCP host service and import controller (`ppcp_host_service.cpp`, `ppcp_import_controller.h`) (today) | design: the platform refactor (capture) | platform refactor (capture) |

**Application-data files**

| Id | File | Fact kind | Target owner | Writer today | Target writer; repository | Step |
|---|---|---|---|---|---|---|
| FILE-APP-01 | `<AppData>/diagnostics/user.json`: the user's pack | source (the user's edits) | **OPEN (Q-02)** | the pack editor (`model_browser`), through `atomicWrite` (`file_pack_provider`) | Q-02 | 3 |
| FILE-APP-02 | `<AppData>/diagnostics/user.norms.json`: the corridor editor's norm set | source (the user's edits) | OPEN (Q-02) | the corridor editor (`file_norm_provider`), through `atomicWrite` | OPEN (Q-02) | 3 |
| FILE-APP-03 | `<AppData>/diagnostics/drills.json`, `references.json`, `gate_words.json`, `screens.json`: the user's content sets | source (the user's edits) | OPEN (Q-02) | the content editors (`drill_pack`, `reference_pack`, `gate_words_pack`, `screen_pack`), through `atomicWrite` | OPEN (Q-02) | 3 |
| FILE-APP-04 | `<AppData>/diagnostics/` community `*.json`, `*.norms.json` | rule data, read-only | OPEN (Q-02) | n/a: installed by the user | n/a: read only | 3 |
| FILE-APP-05 | `src/Resources/diagnostics/` (`core.json`, `norms.json`, `flight_norms.json`, `contexts.json`, `drills.json`, `gate_words.json`, `references.json`, `screens.json`), shipped in the build | rule data, read-only | n/a: shipped | n/a: shipped | n/a: read only | 7 (loaded by repositories) |
| FILE-APP-06 | `<AppData>/studio/`: the studio profile (`PINPOINT_STUDIO_DIR` overrides) | source (rig) | Studio/Rig | `Core/studio_profile` | design: the platform refactor (capture) | platform refactor (capture) |

### 8.2 Settings that hold domain facts

The kinds given to SET-02, 03, 04, 07, 08, 10…14 and 16 (preferences and settings, not domain facts) are
(ratified by Mark, 14 Nov 2026; DEC-117).

| Id | Key | Fact | Kind | Target owner | Writer today | Target | Step |
|---|---|---|---|---|---|---|---|
| SET-01 | `athletes/<uuid>/…`: `name`, `handedness`, `heightValue`/`heightUnit`, `weightValue`/`weightUnit`, `handicap`, `primaryClub`, `speedTarget`, `notes`, `dob`, `sex`, `reportEmail`, `reportGreeting`, `reportPhotosApproved`, `reportPhotosApprovedOn`, `createdAt`, `clubs` (the bag); `lastSessionAt`, `sessionCount` | the athlete record and bag | source; `lastSessionAt` and `sessionCount` are derived | `Athlete` | `AthleteStore` (step 2.3), for an athlete M1 has not moved: its edits written back to these keys (only the keys that changed; never by a read), called by `AthleteController`; the two derived keys are no longer written or read (the index gives them) | FILE-ATH-01 (DEC-05); the two derived keys from the `SessionIndex` (P1) | 2 |
| SET-02 | `athletes/currentAthleteUuid` | the current athlete at launch | app preference | the app | `AthleteController` | stays in settings (DEC-05) | n/a: a setting |
| SET-03 | `General/athleteLibraryPath` | the library root (OBJ-LIBRARY) | application setting, per machine | the app | `AppSettings` | stays | n/a: a setting |
| SET-04 | `storage/sessionNamingPattern` | how a new session folder is named | app preference: shapes a property only (DEC-41) | the app | `AppSettings` | stays | n/a: a setting |
| SET-05 | `analysis/clubLenPrior` | the live club-length prior, keyed by `AthleteId`, club and camera, learned under the captured club (also when the shot is later corrected) | learned rig state, per machine (Mark, 15 Nov 2026; DEC-153, which reverses MD-12's T6, "athlete-level learned state") | Studio/Rig (DEC-153) | `ShotProcessor`, through `AppSettings` | stays per machine; moves with Studio/Rig in the platform refactor (capture) (DEC-153, DEC-17) | platform refactor (capture) |
| SET-06 | `ui/diagnosticsNormSetsOff` | which norm sets findings are fired against | the user's choice; an input to findings, whose stamp records the sets in force when they fired because they may change later, not because the setting is a source fact (R-STAMP-1; Mark, 15 Nov 2026; DEC-140) | with packs and norms, OPEN (Q-02) | `AppSettings` | OPEN (Q-02) | 3 |
| SET-07 | `storage/saveRawFrames`, `storage/skipAnalysisForRawCapture` | what capture keeps, and whether it analyses | capture preferences; what was captured is recorded in the Recording | the app | `AppSettings` | stays; design: the platform refactor (capture) | n/a: a setting |
| SET-08 | `launchmonitor/premierFollowClub` | turns on follow-club (R-REC-2) | capture preference | the app | `AppSettings` | stays | n/a: a setting |
| SET-09 | the per-camera `camera/*` keys (`perspective`, `isMirrored`, `fixedInPlace`, `alias`, `ballRoi`, …), `imu/calibration`, `imu/roles`, `imu/placement`, `imu/mountOrientation`, `ppcp/studioName`, `ppcp/pairings` (the last four: Mark, 15 Nov 2026; DEC-140) | rig facts | source (rig) | Studio/Rig | device and settings code | design: the platform refactor (capture); until then each Recording records its streams' setup | platform refactor (capture) |
| SET-10 | `storage/trashRetentionDays` | how long a trash entry is kept before the purge | housekeeping preference | housekeeping (DEC-80) | `AppSettings` | stays | n/a: a setting |
| SET-11 | `storage/archiveAfterDays` | the age at which a session is archived | housekeeping preference | housekeeping (DEC-80) | `AppSettings` | stays | n/a: a setting |
| SET-12 | `storage/archiveFloorGb` | the free-space floor below which sessions are archived | housekeeping preference | housekeeping (DEC-80) | `AppSettings` | stays | n/a: a setting |
| SET-13 | `storage/archiveLocation` | `<archiveRoot>` | application setting, per machine | housekeeping (DEC-80) | `AppSettings` | stays | n/a: a setting |
| SET-14 | `storage/archiveKeepRaw` | whether the archive keeps raw frames (off: the archive drops them, a permanent delete of raw frames, documented and shown with a warning: Mark, 15 Nov 2026; DEC-137) | housekeeping preference | housekeeping (DEC-80) | `AppSettings` | stays | n/a: a setting |
| SET-15 | `ui/diagnosticsGradePolicy` | the z-thresholds findings are fired at | the user's choice; an input to findings, whose stamp records the grade policy in force when they fired because it may change later, not because the setting is a source fact (R-STAMP-1; Mark, 15 Nov 2026; DEC-140) | with packs and norms, OPEN (Q-02) | `AppSettings`; fed to `WorkOnsController` (today) | OPEN (Q-02) | 3 |
| SET-16 | `storage/savePoseKeypoints` | whether pose tracks are saved with the analysis (`PersistOptions::savePose`) | capture preference | the app | `AppSettings` | stays | n/a: a setting |
| SET-17 | `ui/wristReferenceSwingDir` | the Wrist chart's reference ★ swing, named by its folder path (P7, C5 debt); today one per machine for all athletes | moves to the athlete record (OBJ-ATHLETE-RECORD), a source fact (Mark, 15 Nov 2026; DEC-152) | `Athlete` (DEC-152) | `AppSettings` (the Charts views' carrier); since step 2.3 the record's `wristReference` too, written by `AthleteController` from it (`main.cpp`); this key is the fallback for an athlete whose record names none, until M4 | on the athlete record (FILE-ATH-01), as a `ShotRef`, one per athlete (DEC-152) | 2 |
| SET-18 | `storage/saveImuStreams` | whether IMU streams are saved | capture preference, like SET-07 (Mark, 15 Nov 2026; DEC-140) | the app | `AppSettings` | stays | n/a: a setting |
| SET-19 | `launchmonitor/standaloneShots` | whether device-only shots are created | capture preference (Mark, 15 Nov 2026; DEC-140) | the app | `AppSettings` | stays | n/a: a setting |

### 8.3 Files the app reads or writes that are not domain

Listed so that "the persistence tables are complete" can be checked. Classing them as not domain is
(ratified by Mark, 14 Nov 2026; DEC-114).

| Id | File | What | Why not domain |
|---|---|---|---|
| EXT-01 | Premier's `SessionUploadQueue.*.json`, `SessionLiveSnapshot.*.json`, `ShotReplay/history.json`; GC Quad's `LastShot.csv` | a launch monitor's own files, read by its connector | the device's; the reading lands in the Recording |
| EXT-02 | `imu_log_*.txt`, `hackmotion_log_*.txt` | device logs | diagnostics output |
| EXT-03 | `PinPointStudio_log_*`, `_stats_`, `_analysis_` (`profiler_controller.cpp`, `resource_monitor_controller.cpp`) | the app's own logs | diagnostics output |
| EXT-04 | `pinpointstudio_audio_*.wav` (Desktop) | an audio capture | a tool's output |
| EXT-05 | `<home>/<shot>-<region>.csv` | the data viewer's CSV export | an outbound export |
| EXT-06 | swing zip exports (`swing_zip_exporter`); the corridor editor's CSL export (`model_browser.cpp`) | outbound exports | outbound exports |
| EXT-07 | the Charts stills caches (`posture-picture`, `delivery-picture`; `charts_posture_presenter.cpp`, `charts_delivery_presenter.cpp`) | rendered images, in the cache location | regenerable view caches |
| EXT-08 | the tools' `--out` trees (swinglab, `regrade_ledger`, the parity tools) | run outputs | outside the library |
| EXT-09 | downloaded TTS models; update payloads | tooling | not facts about a golfer |
| EXT-10 | developer dumps behind environment switches: `imu_*.csv` (`PINPOINT_IMU_RECORD_DIR`), `*_frame.png`/`*_mask.png` (`PINPOINT_ADDRESS_MARKS_DUMP`), `mediapipe_crop_debug.jpg` | diagnostics output | accepted as not domain (Mark, 15 Nov 2026; DEC-140) |
| EXT-11 | downloaded LLM and STT models (`llm_controller.cpp`, `stt_processor.cpp`) | tooling | accepted as not domain (Mark, 15 Nov 2026; DEC-140) |
| EXT-12 | `<session>/.pinpoint-lock`, `<athlete>/.pinpoint-lock` (from step 2) | the advisory lock files (R-PERS-6) | coordination files, not facts (Mark, 15 Nov 2026; DEC-160: IM-S2-7) |

### 8.4 Rules

| Id | Rule | Forbids |
|---|---|---|
| R-PERS-1 | **One door per file.** Each file has one repository and one format definition. | A file name spelled outside its repository (lint A4); an envelope parsed in more than one place |
| R-PERS-2 | **Atomic writes, one mechanism** (`QSaveFile`, behind one helper, `atomicWrite`, in the Platform ring from step 2: DEC-160, IM-S2-7). The swing file's own writer, libppswing's `writeFileAtomic`, is the store's documented exception: it syncs the file and its folder (Mark, 15 Nov 2026; DEC-155). | Remove-then-rename; part-then-rename; a fixed `.tmp`; a second write helper |
| R-PERS-3 | **Schema checked on read.** A newer schema is read-only, and every write refuses it; an older shape is migrated in one step, in the repository. | A reader handling old shapes itself |
| R-PERS-4 | **One serialiser per analysis block**, beside its producer: part of the Recording contract. The live detail and the disk form are the same functions, in one time domain. | Hand-kept converters of the analysis block; a live form that differs from the disk form |
| R-PERS-5 | **A cache never holds the only copy** (P1). | Golfer input or a recorded input in `diagnostics.json` or any other cache |
| R-PERS-6 | **Advisory lock files** guard a session or athlete folder against a second process (the CLI tools); the tuning log among them. A lock is short, one per write; a tool refuses a session whose session file says InProgress, unless it is given `--allow-in-progress` for one a crash left InProgress (Mark, 15 Nov 2026; DEC-158). | Two processes writing one file unguarded |
| R-PERS-7 | **One-off migrations** of the library run once, with a dry-run report and backups on the share, and wait for Mark. | A silent migration of the live library |
| R-PERS-8 | **Moving source facts** (the archive, the trash) copies and verifies before anything is removed, and never drops what cannot be regenerated by default (ratified by Mark, 14 Nov 2026; DEC-97). | A move that loses a source fact |

The athlete's folder is where its `athlete.json` is found; it has no `folder` key, is kept on rename, and is never
derived from the athlete's name (DEC-04; Mark, 15 Nov 2026; DEC-151).

### 8.5 Golfer input has levels

| Id | Input | Level | Stored |
|---|---|---|---|
| R-INPUT-1 | Shot intent | Shot: can differ from the session's ("we lose focus, we react to a bad shot") | the shot's `review` block |
| R-INPUT-2 | A "from here on" intent run | Session: a session statement. It **pre-fills each shot as it is taken**, and each shot can override it. An edited run affects only shots taken after the edit (Mark, 14 Nov 2026; DEC-20). A past run is not edited after its session ends, except by resuming the session (today's only, DEC-134) (Mark, 15 Nov 2026; DEC-159). | FILE-SESS-02; the pre-filled intent on each shot |
| R-INPUT-3 | Declared focus (the declared work-on, DEC-138), declared miss, screen answers | Session | FILE-SESS-02 |
| R-INPUT-4 | Intent consistency within a session; shot intent against session intent | Session, derived | `SessionDiagnostics`' intent summaries |
| R-INPUT-5 | Intent consistency across sessions | Athlete, derived from session summaries | `AthleteDiagnostics`' caches |

Intent never excuses a finding: PPS reports what it saw.

---

## 9. Concurrency and the QML boundary

### 9.1 Concurrency

| Id | Rule | Forbids |
|---|---|---|
| R-CON-1 | Domain objects and `AppState` live on one owner thread: the GUI thread in the app, the main thread in a tool. | A domain object touched from a worker |
| R-CON-2 | Firing findings, the session's diagnosis and re-analysis run on workers over immutable inputs: a shot's analysis, a frozen baseline, a `shared_ptr<const>` pack and norms. | A worker reading a live member |
| R-CON-3 | Results are posted to the owning root, which applies them and emits change signals. Results for a session that has since closed are dropped by identity. | A worker writing a file or a model directly |
| R-CON-4 | Shared rule data (packs, norms, flight norms, the metric catalogue) is loaded once into immutable `shared_ptr<const>` snapshots. A change publishes a new snapshot and EVT-12 / EVT-13. | Lazy singletons first built on a worker; a private re-parse of a pack |
| R-CON-5 | One threading idiom: a single worker pool and one "post result to owner" helper. | A new pool, thread or idiom |

### 9.2 The QML boundary

| Id | Rule | Forbids |
|---|---|---|
| R-QML-1 | QML reaches the app through one object, `app`. `app.state` **is** the `AppState` (current athlete, capture target read-only, session on screen, mode, selection, cast selection, replay). `app.athlete` and `app.session` are shorthand for `app.state.currentAthlete` and `app.state.sessionOnScreen` (ratified by Mark, 14 Nov 2026; DEC-63). Context globals are only devices, settings, logging and `app`. | A new context property holding athlete- or session-scoped state |
| R-QML-2 | Lists are `QAbstractListModel`s with declared roles; values are `Q_GADGET`s (`ShotFactors`, `Carry`, `Club`, `Measurement`, `Intent`, …). | `QVariantMap`/`QVariantList` shapes documented only in comments (lint A6) |
| R-QML-3 | No domain or session-scoped C++ type is created in QML. View-local helpers (a filter proxy, a chart's metric helper) stay QML-created. | A view owning a session-level model (lint A2) |
| R-QML-4 | QML contains no domain rules. | "Clean vs Solid" in QML; shape by substring; counts parsed from sentences |

**Worked example: the carousel shot summary**, for LM shots:

```
app.state.selection.shot.diagnostics.factors.{strike, direction, flight}
app.state.selection.shot.diagnostics.carry
```

The selection is application state; the shot and its diagnostics are domain. The factors and the carry are the
shot's derived data, kept by its `Session` with its findings in FILE-SESS-03 (DEC-121, DEC-135), so the summary is
there whatever panels are on. It updates whenever an event's chain recomputes that shot's derived data (§7.4),
`ShotReanalysed`, `ShotAdded`, a change of its club's usuals (DEC-130) and a label correction (DEC-131) among them.

---

## 10. The ownership questions

Copy this section into every plan or brief that touches data, and fill one row per new or changed thing, citing
the ids it touches. A reviewer answers the same seven questions for the diff.

```markdown
## Ownership

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived: inputs, rule version, invalidating events | 5. Persisted where; the one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| … | source / recorded input / derived / recorded product / application state / view state | … | … | … | … | platform / vocabulary / rules / domain / application / adapter | … |

Information-model change (needs Mark's approval before it is built)? yes / no, and which of C1–C11:

> An information-model change (IMC, v1.1) is any edit to code, data, a file format or a plan that adds, moves,
> renames, splits, merges or removes any of the following, or changes what this page says about one: (C1) a domain
> object, an owner of facts, or what an aggregate contains; (C2) a kind of fact (a result, a measurement
> family, a golfer input), or the kind of a fact (source, recorded input, derived, recorded product, application
> state, view state) or its level (shot, session, athlete, application);
> (C3) a persisted file, a field family in one (a new block or group of keys, or a schema-version change of an
> existing file), or a settings key that holds a domain fact; (C4) the one writer of a file, or the repository it is
> written through; (C5) an identity: a new key, a new representation, a new translation to a foreign id, or
> identifying an athlete, a session or a shot by anything but its id, or adding an id type; (C6) a dependency
> between levels: a level
> reading anything but the level below as results, or context flowing down that is not a typed input; (C7) a
> domain event, its producer, or what it recomputes; (C8) an object's lifecycle: who creates or ends it, its
> states, or the commands between them; (C9) a ring, or the ring of a directory or file; (C10) anything domain- or
> session-scoped created by a view; (C11) a principle (P1…P10) or a rule (R-*). It is not an information-model
> change to make code match this page, to add
> a metric to the catalogue within an existing measurement family, to bump a rule's or a stage's version when the
> bump only makes caches stale (the stamp already carries it), to update §11, or to edit a glossary entry's wording
> when the edit cites the decision it follows. A version bump that would rewrite
> a recorded input is a change, proposed with a dry-run count of what it would rewrite. When in doubt, it is a
> change.

Does §11 still match the code this touches? yes / updated in the same commit
```

The template's last line is (ratified by Mark, 14 Nov 2026; DEC-105).

| # | Question | Principle |
|---|---|---|
| 1 | Is it a source fact, a recorded input, a derived fact, a recorded product, application state or view state? | P1, P2 |
| 2 | Which object owns it, and at which level? | P3, P6 |
| 3 | Who creates and ends it, and what identifies it? | P3, P7 |
| 4 | If derived: what are its inputs, its rule version, and the events that invalidate it? | P5 |
| 5 | Where is it persisted, and by which one writer (inside which aggregate)? | P1, P3, P4 |
| 6 | Which ring does each new file go in? | the rings, [§4](#4-the-rings-and-the-directory-mapping) |
| 7 | How does a view reach it? (`CLAUDE.md`, Mark's wording) | P2, P9 |

---

## 11. Where the code is today

Updated by each refactor step in the commit that changes the status; a reviewer checks it (§1.1). Step numbers
are those of `docs/implementation/swe-audit/04-remediation-plan.md`; finding ids are in `02-findings.md`. Current
at `363925e6` (step 1 items 1.0–1.6 committed), with step 1.7's status edits (the item that closes step 1),
step 2.1's row (the identity types, `d0292bba`), step 2.2's rows (`SessionRepository`, the session file, the one
allocator, `7efab713`) and step 2.3's rows (the `Athlete`, `AthleteRepository`, `athlete.json`).

### 11.1 Status

| Element (ids) | Status | Today | Delivered by |
|---|---|---|---|
| Recording contract: one builder, analysis only on a Recording (R-REC-4, R-REC-5) | done | the Recording types (`src/Recording`) and `buildAnalysisJob()`, the only job builder (`recording_contract_test`, 3586cd1b). Live capture makes the shot's Recording at the window freeze (`src/Gui/shot/capture_context.*`: the athlete, session and settings read once) and builds its job with the one builder; re-analysis and SwingLab load the Recording (`src/Export/recording_loader.*`, 186dbf51) and build theirs the same way. All three run it through `AnalysisService` (`src/Analysis/analysis_service.*`), with the Recording's integrity verdicts by one rule, `integrityFor()` (`src/Analysis/recording_integrity.*`); `ShotProcessor::buildAnalysisJob`, the synthesised analysis-only manifest and the three copies of the integrity checks are gone. For an uncorrected shot the live job equals the re-analysis job, and one Recording gets the same verdicts live and on re-analysis (`recording_contract_test` 1c, 8) (step 1.5). Re-analysis applies the golfer's club correction (step 1.6, 363925e6) | n/a: done |
| The analysis context recorded (OBJ-RECORDING; DEC-24) | partly | the one header writer writes the `analysisContext` block, and the loader reads it; a document without it is read as the legacy context, exactly as re-analysis read it before; a newer one is read-only (186dbf51). Live capture writes it on every shot, from the context read once at the freeze (`captureAnalysisContext()` in the capture snapshot, step 1.5). Follow-club rewrites the block's captured club (`withFollowedClub`, step 1.6). The backfill exists: `tools/migrate/backfill_analysis_context` writes the block into a document that has none, through `RecordingRepository::backfillAnalysisContext` (the legacy context with the athlete record's height and club set on the swing's date); a dry run by default, `--write` only with a backup folder (step 1.7). It has been run as a dry run only | the real backfill run (DEC-34: Mark reads the dry-run report first; backups on the share) |
| The shot kind (R-REC-8; DEC-44; its key `shotKind`, ratified: DEC-147) | partly | the one header writer writes `shotKind` at creation only where no later write can make it false: `captured` on the exporter's manifest with streams, `deviceOnly` on the standalone device-only shot; none on the analysis-only shot, the phone-clip placeholder and the device-only rescue of a failed camera shot's folder (a phone clip may still join it), which are read as an older document is; no later write changes it. The Recording carries it (`recording::ShotKind`, `src/Recording/recording.h`), set by the loader. A document without it is classified by one function, `readShotKind()` (`src/Export/recording_header.*`), with today's content rule, and is not written back; `RecordingRepository::isDeviceOnly` and the loader read through it (`recording_store_test` Test 7; step 1.8). Not yet the `Shot`'s, which does not exist; the shot card still derives its own "monitor only" from its row (`PpShotCard.qml`) | step 3 (the `Shot`, P10; the card's guess) |
| The input stamp (R-ANA-5; DEC-25, DEC-75: handedness included) | done | `analysis.versions.inputs` (`AnalysisInputs`, `kAnalysisInputsVersion` 1, `analysis_versions.h`) names the effective club, its length and source, height, club set, handedness and the session type; written with every analysis the one builder makes; staleness by `analysisInputsStaleness()` (`src/Analysis/reuse_gate.*`; `reuse_gate_test`) (step 1.6, 363925e6) | n/a: done |
| One time base (R-ANA-6; DEC-25) | done | every analysis written says `analysis.timeBase: "window"`; one rule (`windowTimeUs`) replaces seven copies of the guess; a document without the key is read through one function (`analysisReadOriginUs`) (d086ceaf). The pipeline rebases each analysis to window time once (`rebaseToWindowTime`, with the job's `windowT0Us`, the Recording's `t0Us`), and the live replay adds the window's start at the view boundary (77ff74a7, step 1.5); the integrity verdict's `firstHoleUs` is window time on every path (step 1.5). The posture and delivery pictures, the markup panel's recorded-pose overlay and `swinglab_run` (its re-fusion instants and `club_dtl.json`) read the analysis origin through the same function (`documentAnalysisOriginUs`) and the one rule; the Python tools that read analysis times (`consistency_extract.py`, `shaftlab/prep_swing.py`) use its port in `tools/pp_swingdoc.py` (`analysis_origin_us`, `window_time_us`). No other copy of the guess is left (step 1.7) | n/a: done |
| One serialiser per analysis block (R-PERS-4) | done | one writer and reader per block beside its producer (`*_json.h`; `swing_doc_analysis.cpp` assembles them; d086ceaf); `recorded_products.cpp` gone. One projection, `analysisDetailFor` (`swing_doc_reader.cpp`), builds the live, replayed and reloaded detail from the disk form; `toAnalysisDetail`, `DiskReplaySource`'s walk and `readSwingJson`'s projection are gone (`analysis_detail_projection_test`, 77ff74a7). The DTL overlay's translator moved beside it, into `src/Export` (step 1.7). The JSON and `QVariant` re-parsers in Charts, Diagnostics and the LM board are a different matter (one measurement set, steps 5–6) | n/a: done |
| Capture writes in stages; the interim writers through `RecordingRepository` (R-REC-2, R-REC-3; DEC-27, DEC-28) | partly | every writer of the swing file goes through `RecordingRepository` (`create`, `completeCapture`, `writeAnalysis`, `editReview`, `convert`, `writeSummarySidecar`, and `backfillAnalysisContext` for DEC-34's one-off migration, step 1.7); `SwingDocWriter`'s writes are private; re-analysis has one write-back, `reanalyse_and_persist` (108e7858). The live join writes through `create` (step 1.5): the repository is `SwingDocWriter`'s only friend. The archive stub still writes with `SwingStore::save` | step 2 (the stub); step 3 (the `Shot`, P10) |
| `RecordingRepository`, implemented by `SwingStore` (P4) | partly | `src/DomainModel/recording_repository.h`, implemented by `SwingStoreRecordingRepository`, held by the composition root (`main.cpp`, each tool's `main`); every write refuses a newer-schema document (108e7858). Reading a Recording back is a separate, small interface, `RecordingReader` (`src/DomainModel/recording_reader.h`; adapter `SwingStoreRecordingReader`, `src/Export/recording_loader.*`), so the write door does not pull frame decoding into every review writer; SwingLab reads through it (step 1.5); the app's re-analysis still calls the loader directly. Keyed by `swingDir`, not `ShotRef` (P7 debt) | step 2 (`ShotRef`); step 3 (the `Session` reads through `RecordingReader`) |
| Restoring an archived session keeps every golfer edit (R-PERS-8) | partly | the review is merged three ways on restore (108e7858); one short window per swing remains until the folder lock | step 2 (the lock) |
| The club rule until step 6 (R-CLUB-3; DEC-26, DEC-31) | partly | the rule exists, `effectiveContext()` v1. Live capture calls it with the club it seeds into `review.club` as the declared club, which is what re-analysis reads back (step 1.5). Re-analysis calls it with both club edits, the board's override applied, and the LM's low point takes the same effective club and the recorded club set (step 1.6, 363925e6) | step 6 (one club everywhere) |
| Analysis and findings name different clubs when only the popover names one (R-CLUB-3; DEC-72) | known interim | accepted until step 6 | step 6 |
| Same Recording + same effective context → same job; a corrected shot re-analyses with its corrected club (R-ANA-2) | partly | same Recording + same effective context → same job: live and re-analysis build it with the one builder, and for an uncorrected shot the two jobs are equal field by field (`recording_contract_test` 1c, step 1.5). A corrected shot re-analyses with its corrected club (step 1.6; `reuse_gate_test` 8). The board still reads its override itself (G-07) | step 6 |
| Pose reused whatever the model file; re-run only on a pose stage-version change (R-ANA-7; DEC-64) | done | a reused pose is written back unchanged and never deleted (108e7858); the one reuse gate (`src/Analysis/reuse_gate.*`) compares the stage version and scope, never the model file, and a reused pose keeps its recorded model identity (step 1.6; `reuse_gate_test` 5) | n/a: done |
| A swing file written once by capture, then only by its `Shot` (R-REC-1; DEC-86) | partly | one door (108e7858); the writers are still today's callers (E-09) | step 2 (the repository's writer contract); step 3 (the `Shot`) |
| `RecordingReader`; `AnalysisService` and `integrityFor()`; the capture-side snapshot (§2.2) | done (`d07ccebb`) | step 1.5: the read door apart from the write door; one service runs every analysis with the Recording's integrity verdicts; live capture reads the current facts once, at the window freeze | step 1.5 |
| `src/Recording` (Vocabulary) and `src/DomainModel` (DEC-29) | done | both exist and are placed in the lint's ring table; `src/DomainModel` holds `RecordingRepository` | n/a: done |
| `src/Export` renamed for the Recording store | not yet | the store and the swing document live in `src/Export` | step 7 |
| Studio/Rig (OBJ-RIG) | not yet | rig facts in SET-09, FILE-APP-06, FILE-SESS-11 and assumptions in code | the platform refactor (capture) |
| Rings enforced (`architecture_lint_test`, A1–A8); the Platform ring | done (the lint); shrinking (the allowlist) | the lint runs with today's violations allowlisted, each tagged with its step | steps 1–8 shrink the allowlist |
| A1 widened to `Export`/`Recording` → `Gui` includes (§1.4; DEC-146) | not yet | A1 checks ring against ring, so an include within the Adapters ring (`Export` → `Gui`) is not seen (step 1.6) | (proposed, for Mark) step 2, beside its other lint edits (IM-S2-6); each allowlisted hit tagged with the step that removes it (DEC-146) |
| Housekeeping as the one purge, through the roots (DEC-80); one `purge` command, SET-14 off shown with a warning (DEC-137) | not yet | `ArchiveController::runHousekeeping` and `emptyTrash` delete trash entries and archive sessions directly (today) | step 3 |
| Session resume keeps the `SessionId`, by `Athlete::resumeSession`, today's session only (DEC-77, DEC-134); `Athlete::startSession` (DEC-78) | partly | step 2.2: `SessionRepository::start` and `resume` (`src/DomainModel/session_repository.h`; adapter `src/Export/session_store.*`), called from `ShotProcessor::beginSessionFolder`: resume only for the athlete's session started today of the same type, the same `SessionId`, any other in-progress session ended first; a refused resume starts a new session. Not yet the `Athlete`'s commands | step 3 (the commands) |
| `Athlete::delete` trashes the whole folder (DEC-79) | done | step 2.3: `Athlete::remove()` (`src/DomainModel/athlete.*`) refuses while one of its sessions is InProgress (DEC-99, through the index), then `AthleteStore::trash` moves the whole folder, sessions and all, to the OS trash, else into the library's own trash in FILE-LIB-01's layout, `<library>/.pinpoint-trash/<athlete>/<stamp>_<athlete>`, which housekeeping purges as one entry (not `SwingPaths::trashPath`, which would land an athlete folder outside the library). A settings athlete's record is written into its folder as `athlete.json` first, so the trashed folder carries it; its settings group goes last (`athlete_repository_test` D1–D9) | step 2 (2.3) |
| The session summary is the report's data (DEC-81) | not yet | `SessionReportModel` computes its own figures | step 3 (the summary); step 5 (the report renders it) |
| Follow-club stores the device club beside the captured club and never overwrites it (DEC-133; DEC-82's analysis result) | known interim | `completeCapture` overwrites `analysisContext.club` (`withFollowedClub`) as well as `capture.club` (step 1.6), and also moves a seeded `review.club` | step 6 (DEC-133; the seed goes, DEC-67) |
| Markup truth files through the shot's repository (FILE-SHOT-12; DEC-83) | not yet | written by `markup_truth.cpp` directly | step 2 |
| The wrist reference shot on the athlete record, as a `ShotRef` (SET-17; DEC-152) | partly | step 2.3: the record's `wristReference` (a `ShotRef`, in `athlete.json`, or in a settings athlete's group until M1); `AthleteController` translates at its edge from and to the swing folder path the Charts views still carry (`appSettings.wristReferenceSwingDir`, wired in `main.cpp`): a ★ is recorded on the current athlete's record and a switch brings that athlete's back. An athlete whose record names none keeps today's per-machine setting as the fallback, until M4 (step 2.8) converts it | step 2 (2.3; M4 in 2.8) |
| `regrade_ledger` through `SessionRepository` (FILE-SESS-03, FILE-SESS-12) | not yet | writes `diagnostics.json` and its backups with `QSaveFile` | step 2 |
| The trunk ceremony recorder (FILE-SESS-11) | not yet | writes on a pool thread (P8 debt) | platform refactor (capture) |
| `AthleteRepository`, `SessionRepository` (identity maps) | partly | step 2.2: `SessionRepository` exists (start, resume, end, D7's `restartInLibrary`, enumerate with a typed `SessionIndexEntry` (DEC-136), allocate, shots, open / openFolder, swingFolder), one instance held by `main.cpp`; the session-input, diagnostics, baseline, work-ons, archive and trash files are not through it yet. A library re-pointed mid-session ends the session and starts a new one (DEC-154); the 0.3 stopgap is gone. Step 2.3: `AthleteRepository` (`src/DomainModel/athlete_repository.h`; adapter `src/Export/athlete_store.*`, names in `athlete_files.h`) exists — list, load (one `Athlete` per `AthleteId`, DEC-52), create, save, trash, and the picker's activity read — one instance held by `main.cpp` | step 2 (2.4–2.6) |
| `AthleteId`, `SessionId`, `ShotRef` in `src/Identity` (DEC-148), `SessionIdentity` (§5.2; DEC-41) | partly | the types exist (`src/Identity/identity.h`, `identity_test`), and `SessionIdentity` (`src/DomainModel/session_identity.*`, `session_identity_test`): a legacy folder's date, type and `_NN` under every naming pattern, D3's v5 id (DEC-150), n-of-day by start time; `SwingPaths` composes every session and swing folder name and lists a session's swing folders (step 2.1). Nothing keys on them yet: the athlete is keyed by sanitised name; the session by its folder name (`DeviceOnlyMeta::sessionId`, `trashSession`), or its path (`restoreSession`); folder names parsed in many places (E-02, E-07, F-06) | step 2 (2.2–2.7: the repositories and the callers) |
| The session file `session.json`: `SessionId`, status, start/end times (FILE-SESS-01; DEC-41, DEC-149) | partly | step 2.2: written only by `SessionStore` (keys DEC-160: IM-S2-1; the schema checked, a newer one read-only) at Start, resume and End, through `atomicWrite` under the folder lock; its names in `SessionFiles` (`src/Export/session_files.h`). A session without one is read from its folder's name (D3), never written by a read; its first write (a resume) writes the file with `origin` backfilled. Old sessions have none until M2 | step 2.8 (M2); step 3 (status by command) |
| One swing-number allocator (DEC-157) | done | step 2.2: `SessionRepository::allocateSwing`, asked by `ShotProcessor` and by `LaunchMonitorController`'s standalone shot for the running session (`ShotProcessor::captureSession`); the LM controller's own `SwingPaths` and `SwingPaths`' allocator half are deleted. Sessions already split are left as they are | n/a: done |
| `Athlete` object, one per `AthleteId`, replaced on switch | partly | step 2.3: the `Athlete`'s step-2 form (`src/DomainModel/athlete.*`; DEC-160: IM-S2-5): `id()`, `folder()`, `record()`, `index()` (DEC-136), `clubSetOn()`, `editRecord` / `editBag` / `removeClub` / `remove()`, `recordChanged()` as the interim for EVT-14. `AthleteController` is the QML adapter over the current one (its QML surface unchanged) and writes through `AthleteRepository` (IM-S2-4); `currentAthleteChanged` fires on a switch only, an edit emits `recordChanged` (R-APP-1; folds in Stage 0.7), and `AthleteSwitchGate` is deleted. A switch is still refused while a session runs (Stage 0.8g). `lastSessionAt` / `sessionCount` come from each athlete's index (DEC-89), read off the GUI thread; `scanActivity` is deleted. The `Athlete` does not start or resume sessions yet (step 3) | step 2 (2.3); step 3 |
| `athlete.json`; library folder on the record (FILE-ATH-01, SET-01) | partly | step 2.3: an athlete created from now on gets `<library>/<folder>/athlete.json` at creation (schema `pinpoint.athlete/1`, checked on read, a newer one read-only; no `folder` key: DEC-151), its folder the sanitised name chosen once, numbered when taken (DEC-04), kept on a rename (E-02). An athlete created before step 2 is read from this machine's settings and written back there — never into an `athlete.json` and never on a read — and its folder is still derived from its name, so its rename still moves the folder, until M1 (step 2.8; H-09). `athlete.json` wins when both exist | step 2 (2.3; M1 in 2.8) |
| Advisory lock files against the CLI tools (R-PERS-6) | partly | H-14. Step 2.0: `FolderLock` (`src/Core/folder_lock.*`) exists and is tested. Step 2.2: `SessionStore` takes it on the session folder for each `session.json` write and each swing allocation (a held lock refuses the write, with the holder named). Step 2.3: `AthleteStore` takes it on the athlete folder for each `athlete.json` write, and the tuning log's append takes it too; no tool takes it yet (DEC-158, DEC-160) | step 2 |
| `Session` aggregate, status, commands and events (EVT-01…23; delivery DEC-69) | not yet | a session is a folder path in several places (E-08); no domain events | step 3 (with step 2's repository) |
| `Shot` entity, its own object with its own commands and events, reached only through its `Session` (P10) | not yet | two `Shot` structs (`ShotListModel::Shot`, `PpcpClipFiler::Shot`); every shot change goes through controllers and the repository | step 3 |
| `fireFindings()` (step 3's name), a pure function in `src/Diagnostics`; each `Shot` fires its own findings (DEC-100) | not yet | `SessionDiagnosticsModel::detectShot`, a member of the view model | step 3 |
| The `Session` keeps each shot's findings and derived data, posted stamped by the `Shot`, in `diagnostics.json` (FILE-SESS-03; DEC-135) | partly | the findings are rows in each session's `diagnostics.json`, beside golfer input (H-04); `SessionDiagnosticsModel` fires them, not the `Shot`; the derived data is recomposed per consumer (C-01, G-03) | step 2 (golfer input out); step 3 (the `Shot` fires and posts) |
| Each `Shot` fires its findings; `SessionDiagnostics` diagnoses from them; `ShotDiagnostics` a read-only view; every live shot fires (DEC-02, DEC-100) | not yet | one view-created `SessionDiagnosticsModel` per panel, cast, catch-up and report (D-01…D-05) | step 3 |
| `AthleteDiagnostics`, derived on demand from session summaries | not yet | `WorkOnsController`, an app-lifetime global re-pointed by hand | step 3 |
| `work_ons.json` written by `SessionDiagnostics` (FILE-SESS-05; DEC-55) | not yet | written by the athlete-level `WorkOnsController` | step 3 |
| The tuning log appended by the `Athlete` on `LabelCorrected` (FILE-ATH-04; DEC-43) | partly | step 2.3: the append goes through the athlete repository's `AthleteFiles::appendTuningLine` (`AthleteStore::appendTuningLog`), under the athlete folder's lock for that one line (§4.9 of step 2's brief); still called by `SessionDiagnosticsModel`, which still spells the file's name | step 3 |
| Club baseline computed by the athlete level, frozen by the `Session`, rewritten only on a rule-version change as a recorded event (OBJ-CLUB-BASELINE; DEC-46) | partly | frozen at first need from up to ten earlier sessions' ledgers (`prepareClubBaseline`) and rewritten when `kClubBaselineSchemaVersion` is newer, which counts rule changes; the rewrite is not recorded; written by whichever view-model instance has authority (D-06) | step 3 |
| Archive and report as `Session` commands (DEC-22) | not yet | the archiver and the report model write the session folder directly | step 2 (the archive through `SessionRepository`), step 3 (`Session::archive()`), step 5 (the report) (DEC-65) |
| Trash through the roots (FILE-LIB-01) | not yet | `ShotListModel::trashShots` and `SessionReviewController::trashSession` call `SwingPaths::trashPath` | step 3 (shots by `Session::trashShot`, sessions by `Athlete::trashSession`, DEC-71) |
| Packs and norms: owner and repository (OBJ-PACKS-NORMS) | OPEN (Q-02) | pack, corridor and content editors write `<AppData>/diagnostics/` through `atomicWrite`; the disabled sets in SET-06 | step 3 (decision); step 7 (norms loaded by repositories) |
| Norm cohort (sex, age band) not recorded in step 1 (DEC-30) | n/a: by decision | firing findings resolves the cohort through the athlete's uuid, so it reads the live record (`DeviceOnlyMeta::athleteUuid`) | Q-05, step 3 |
| The shot's derived data computed once by the `Shot`, kept by its `Session` (OBJ-SHOT-DERIVED; DEC-121, DEC-135) | not yet | the outcome and the factors recomposed per consumer (C-01, G-03) | step 3 |
| No separate wrist model (DEC-123) | not yet | `WristDiagnosticsModel`, a QML-created model deriving its own score, findings and strengths | step 3 (the wrist-pattern result becomes part of the shot's findings) |
| Cross-session reads of shot caches (FILE-SHOT-07) | not yet | the Charts delivery presenter and the report read the day's other sessions' phase grids | step 5: through session results (DEC-74) |
| `SessionSeries`, one per session | not yet | one `SessionSeriesModel` per view (F-01) | step 5 |
| One measurement set, tagged by source and kind; the LM a device; one precedence rule | not yet | LM rows inside `analysis.metrics`; a separate LM model (A-04, A-10) | step 6 |
| The flight check as shot derived data (DEC-122) | not yet | the verdict is written into the rows (C-02) | step 6 |
| One direction rule (pack corridors) | not yet | two classifiers (F-05, G-01) | step 6 |
| An undeclared club's findings fired as "club assumed" (R-CLUB-4) | partly | Stage 0.3: fired right when the club is empty; capture still writes a DRIVER stub (the athlete's primary club, default DRIVER), so the shot reads back as a declared driver | step 6 (DEC-67) |
| Held conditions: one rule | done | Stage 0.12 | n/a: done |
| `AppState` in `src/App`; the selection and `castSelection` | not yet | the selection is held in five places (E-04, E-05) | step 4 |
| Golfer input out of the cache | partly | Stage 0.1: golfer input can no longer be lost from `diagnostics.json`; it still lives there (H-04) | step 2 |
| `session_input.json`; label corrections and shot intent on the shot's `review` | not yet | in the `diagnostics.json` envelope | step 2 |
| Atomic writes, one mechanism (R-PERS-2) | partly | Stage 0.2: the shared `atomicWrite` is on `QSaveFile`. Step 2.0: the helper is in Platform (`src/Core/atomic_write.h`, with a streaming form; DEC-160); every direct `QSaveFile` is on it except the archiver (2.6), the markup truth (2.4) and `regrade_ledger`'s seed (2.5); the trunk ceremony copies aside instead of renaming; `pack_io` keeps a forwarding call for the diagnostics callers; the swing file's own writer is the documented exception (DEC-155) | step 2 (one helper, with the repositories) (DEC-66) |
| One door per file; schema checked on read (R-PERS-1, R-PERS-3) | partly | the swing file: done (108e7858); session and athlete files: envelope parsed in about 10 places, schema tags unchecked (D-08, H-08, H-12) | step 2 |
| `fault_profile.json` deleted (FILE-ATH-05) | not yet | a reader on every activation, a writer with no caller (H-11, D-07) | step 3 |
| Stamps carry rule versions and every input (R-STAMP-1) | partly | analysis stage versions exist but gate reuse only; diagnostics stamps omit flight norms, thresholds and the disabled norm sets (A-08, C-02, G-06) | steps 3 and 6 |
| Rule versions checked by a golden or parity test (R-STAMP-3) | not yet | versions bumped by hand, unchecked (C-05) | step 3 |
| `NormsChanged`/`PackChanged` reach the app at once | partly | Stage 0.4/0.4b: a saved corridor reaches session diagnostics and the LM board without a relaunch; no event mechanism yet | step 3 |
| Shared rule data as immutable snapshots (R-CON-4) | partly | Stage 0.4: the norm set is safe to share across threads; packs still assembled independently (B-02, B-03) | step 3 |
| One threading idiom (R-CON-5) | not yet | five idioms (F-09) | step 5 |
| QML through `app`; typed models and gadgets (R-QML-1, R-QML-2) | not yet | 42 context globals; about 145 `QVariantMap`/`QVariantList` contracts (D-10) | steps 3–5 |
| No domain rules in QML (R-QML-4) | not yet | G-11, D-11 | step 5 |
| Rules without file I/O; words out of reductions | not yet | C-07, B-12, C-04; the phase-grid sampler writes its sidecar from `Diagnostics` | step 7 |

### 11.2 Today's types and their target objects

So a new struct can be checked against what already exists. A type named here keeps its job until its step; a
new type with one of these jobs is an IMC.

| Today's type (file) | Holds | Target | Step |
|---|---|---|---|
| `AthleteController` (`Gui/athlete/athlete_controller.*`) | the athlete record, the bag, the club set rule, the current athlete | `Athlete` + `AthleteRepository`; `AppState.currentAthlete` | 2; 4 |
| `SessionController` (`Gui/session/session_controller.h`) | the running session, its type and clock | `Session` (status, identity) | 3 |
| `SessionReviewController` (`Gui/review/session_review_controller.h`) | review writes, the summary sidecar, a session's trash | the `Shot`'s commands (review, sidecar: P10); `Athlete::trashSession` (DEC-71) | 3 |
| `SessionListModel`, `SessionSummary` (`Gui/review/session_summary.h`) | today's session list (a display struct) | a presenter over `SessionIndex` | 3; 5 |
| `ShotListModel::Shot` (`shot_list_model.h`), `PpcpClipFiler::Shot` (`ppcp_clip_filer.h`) | shot rows | `Shot` | 3 |
| `SessionDiagnosticsModel` | firing findings (`detectShot`), the session reduction, the baseline, the tuning log | `fireFindings()` fired by each `Shot` (DEC-100), `SessionDiagnostics`, the `Session`, the `Athlete` | 3 |
| `WorkOnsController` | the athlete reductions, `work_ons.json`, themes, tendencies | `AthleteDiagnostics`; `SessionDiagnostics` (the summary) | 3 |
| `LmSessionModel` | the LM board's model, its club handling and statistics | a presenter over `SessionSeries` | 6 |
| `SessionSeriesModel`; `ShotDiagnostics` and `SessionSnapshot` (`Gui/charts/session_series_types.h`) | per-view series; a chart's per-shot struct of the same name as the target view | `SessionSeries`; the Charts struct renamed when its view moves (ratified by Mark, 14 Nov 2026; DEC-119) | 5 |
| `SessionReportModel` | the report and its stills | the `Session`'s report command; a presenter | 5 |
| `ClubBaselineTable`, `ClubBaselineSnapshot`, `IClubBaselineProvider`; `ShotRecord` (`Coaching/diagnostic_ledger.h`); `ShotCorrection`, `ClubDistanceSession` (`Coaching/club_distances.h`) | the club baseline and its reduction inputs; a label correction | OBJ-CLUB-BASELINE; reduction inputs; OBJ-TUNING-LOG's entries | 3 |
| `ShotFactors` (`Coaching/shot_factors.h`), `ShotOutcome` | the three factors, the outcome | OBJ-SHOT-DERIVED | 3 |
| `SessionWorkOns`, `SwingThemes` | session and athlete results | OBJ-SESSION-SUMMARY; `AthleteDiagnostics` | 3 |
| `NormPack`, `CharacteristicPack`, `FlightNorms`, the pack and norm providers | rule data | OBJ-PACKS-NORMS | 3; 7 |
| `StudioProfile` (`Core/studio_profile.h`); `PpcpImportLedger::SessionRecord` | rig facts; PPCP import bookkeeping | OBJ-RIG; FILE-LIB-03 | platform refactor (capture) |
| `WristDiagnosticsModel` (`Gui/wrist/wrist_diagnostics_model.h`, a `QML_ELEMENT`) | shot-level facts derived from the analysis (score, findings, strengths) | **retires**: there is no separate wrist model. The wrist-pattern identification is an analysis of the shot, and its result is part of the shot's findings (OBJ-SHOT-RESULTS) through the one diagnostic model (Mark, 14 Nov 2026; DEC-123) | 3 |
| `SessionReportListModel` | the report list | a presenter over the `SessionIndex` (ratified by Mark, 14 Nov 2026; DEC-123) | 5 |
| `RecordedClub`, `ClubGeometry` (`src/Recording/analysis_context.h`) | the captured club and a club's geometry | the `Club` value (CLS-CLUB) (ratified by Mark, 14 Nov 2026; DEC-123) | n/a: in place |
| `RecordingHeader` (`Export/recording_header.h`) and its `Athlete` struct; `LoadedRecording` (`Export/recording_loader.h`) | the swing file's header; a loaded shot | the Recording store's forms of OBJ-RECORDING | n/a: the store's |
| `ClubReview`, `EffectiveClub`, `EffectiveContext` (`Analysis/effective_context.h`) | the club edits and the effective context | OBJ-SHOT-REVIEW's club facts; OBJ-EFFECTIVE-CLUB | n/a: in place |
| `AnalysisContext` (`Analysis/analysis_stage.h`), the stage blackboard | a stage's working state | renamed `StageState` (ratified by Mark, 14 Nov 2026; DEC-119) | 7 |
| `SessionProfile` (`Analysis/analysis_stage.h`) | the stage list for a session type | renamed `StageProfile` (ratified by Mark, 14 Nov 2026; DEC-119) | 7 |

---

## 12. Glossary

One meaning each; anything else gets another word. This glossary is (ratified by Mark, 14 Nov 2026; DEC-118); the code renames it
implies (`StageState`, `StageProfile`, the Charts `ShotDiagnostics`, `review.excluded`) are (ratified by Mark, 14 Nov 2026; DEC-119).

| Id | Term | Meaning | Not to be confused with |
|---|---|---|---|
| GL-01 | Athlete | the aggregate root keyed by `AthleteId` | `RecordingHeader::Athlete`, a header struct |
| GL-02 | Athlete record | the athlete's source facts (OBJ-ATHLETE-RECORD) | the "athlete profile" (an old name for it) |
| GL-03 | Library | the root folder the repositories use; one per machine setting (SET-03) | n/a: no other meaning |
| GL-04 | Session | the aggregate root keyed by `SessionId`: one practice | a **peer session** (PPCP's), a **device session** (a launch monitor's files), `HmSessionWorker` |
| GL-05 | Session type | the kind of practice chosen at Start; it selects the analysis stage list | `SessionProfile`, to be renamed `StageProfile` |
| GL-06 | Shot | the entity keyed by `ShotRef` | "swing", which is used only in file and folder names (`swing.ppsw`, `swing_NNNN`) |
| GL-07 | Swing number | the `NNNN` of `swing_NNNN`; "Shot N" | a rank "n of N" |
| GL-08 | Recording | the shot's captured source facts and its analysis context, written by capture | n/a: no other meaning |
| GL-09 | Analysis context | the Recording's recorded block of athlete facts | the stage blackboard `AnalysisContext` (to be renamed `StageState`); the effective context |
| GL-10 | Effective context | the analysis context with the club correction applied, for one analysis run | n/a: no other meaning |
| GL-11 | Recorded input | context frozen once for reproducibility | n/a: no other meaning |
| GL-12 | Recorded product | an expensive derived fact cached with a stamp: only the shot's analysis | n/a: no other meaning |
| GL-13 | Captured / device / declared club; club correction; effective club | §5.8 (the device club: Mark, 15 Nov 2026; DEC-133) | the primary club (the record's default), follow-club (the device naming the club), `deviceClub` / `recordedClub` (code names) |
| GL-14 | Club set | men, women or junior; selects standard lengths | the norm cohort |
| GL-15 | Norm cohort | sex and age band; selects norms | the club set |
| GL-16 | Bag | the athlete's clubs and their geometry (OBJ-BAG); the *bag snapshot* is that geometry recorded in the Recording | n/a: no other meaning |
| GL-17 | Reading exclusion | the board's "leave this reading out of the statistics" (`review.excluded` today) | trash |
| GL-18 | Trash | moving a shot or session to a recoverable place | exclusion |
| GL-19 | Archive | moving a session's streams to the archive mirror, leaving stubs | trash |
| GL-20 | Shot annotation | a golfer's judgement in the shot's `review` (an exclusion, a club correction) | **markup notes**, PPCP's annotations (FILE-SHOT-11) |
| GL-21 | Label correction | the golfer correcting a detected outcome label on one shot | a club's display label |
| GL-22 | Firing findings | the shot's assessment of its analysis and derived data against the diagnostic graph (GL-67; Mark, 15 Nov 2026; DEC-132, DEC-140), each number against its corridor, with the context its session passes down, giving its *findings* (Mark, 14 Nov 2026; DEC-100, DEC-121) | "grading", today's word, no longer used at shot level |
| GL-23 | Finding | one result of firing, one per number checked; fired by the `Shot` (Mark, 14 Nov 2026; DEC-100), kept by its `Session` (Mark, 15 Nov 2026; DEC-135) | a "row" (today's code word); a table row |
| GL-24 | Reduction | a cross-shot rule run by a diagnostics owner | n/a: no other meaning |
| GL-25 | Usuals | a session aggregate: the session's per-club typical values, computed from its shots' readings, never from findings; not part of the session's diagnosis; they flow down to the shots as an input to their derived data (M-SHOT-LAYERS layer 3) (Mark, 15 Nov 2026; DEC-130) | the diagnosis (GL-62) |
| GL-26 | Ledger | the session's diagnostic ledger: the `Session`'s collection of every shot's findings and derived data, kept in FILE-SESS-03 (`Coaching/diagnostic_ledger.h`) (Mark, 15 Nov 2026; DEC-135) | the PPCP import ledger |
| GL-27 | Tiers | a condition's standing in a session's results (`Tier`: Clean · Watching · Pattern) | n/a: no other meaning |
| GL-28 | Links | the relations between conditions that the session reduction reports; its typed definition is step 3's (the session's result types) | n/a: no other meaning |
| GL-29 | Mixes | the session reduction's mix results; typed definition: step 3 (the session's result types) | n/a: no other meaning |
| GL-30 | Reasons | the reasons the session reduction gives for its outcomes (`SessionDiagnosticsModel::reasons` today); typed definition: step 3 | n/a: no other meaning |
| GL-31 | Carry today | the session's carry per club against the usual | n/a: no other meaning |
| GL-32 | Flight-check overlay | shot derived data: the flight check computed for the shot with the session's usuals passed down (Mark, 14 Nov 2026; DEC-122, superseding DEC-06) | n/a: no other meaning |
| GL-33 | The three factors, the shot's tendencies | strike · direction · flight: the shot's derived data (Mark, 14 Nov 2026; DEC-121) | **Tendencies** (GL-64), the athlete level's |
| GL-34 | Sky | the one flight fault: for a driver, fairway wood or hybrid it replaces High when the flight says so (`Coaching/shot_factors.h`) | n/a: no other meaning |
| GL-35 | Screen answers | the golfer's answers to a session's screens (the screen pack, `screens.json`) | n/a: no other meaning |
| GL-36 | Intent run | a "from here on" intent, pre-filling each later shot | n/a: no other meaning |
| GL-37 | Work-on | never used alone: say the declared work-on (GL-68), the session work-ons (GL-69) or the athlete work-ons (GL-70) (Mark, 15 Nov 2026; DEC-138) | n/a: no other meaning |
| GL-38 | Held condition | a condition's "held" state, decided by one rule that Your swing and Work-ons share (R-DER-7) | n/a: no other meaning |
| GL-39 | Session summary | what the athlete level reads from a session (FILE-SESS-05) | `SessionSummary` (a display struct); `swing_summary.json` (a shot cache); the analysis' `summary` block |
| GL-40 | Club baseline | a session's frozen per-club baselines (OBJ-CLUB-BASELINE) | the parity baseline (a test fixture); the ball's empty-mat baseline |
| GL-41 | Catch-up | the application service that brings the athlete level's caches up to date | n/a: no other meaning |
| GL-42 | Words module | the one place golfer-facing sentences are built | n/a: no other meaning |
| GL-43 | Context | only the three contexts (§2) | the analysis context; `ShotContext`; QML context globals |
| GL-44 | Ring | a dependency ring (§4) | a layer |
| GL-45 | Layer | one of P2's three: domain, application state, view state | a ring |
| GL-46 | Stage | an analysis stage (`k*StageVersion`) | Stage 0 (the refactor's first phase, named only); the UI's *stage panel* |
| GL-47 | Profile | n/a: avoid; say athlete record, studio profile, stage profile or capture profile | n/a: no other meaning |
| GL-48 | Rig | Studio/Rig (OBJ-RIG) | the skeleton `Rig` (`Skeleton3d`), `SwingRigDriver` |
| GL-49 | The lead | the agent that leads the refactor; it wrote this page's first version | n/a: no other meaning |
| GL-50 | Live | (1) of a session: in progress; (2) of a path: capture, as against re-analysis (the live join, the live job); (3) of an object: the in-memory one other code may be changing (P8's "live object"). Say "real-time" for real time | n/a: no other meaning |
| GL-51 | Corridor | a norm's range for one number, against which a finding is fired | n/a: no other meaning |
| GL-52 | Condition | a named pattern a pack defines (a fault or a set-up), which findings and the session's diagnosis report | n/a: no other meaning |
| GL-53 | Pack | a set of conditions and the rules that relate them (the core pack, the user's pack, community packs) | n/a: no other meaning |
| GL-54 | Norm set | a file of corridors (the core norms, the user's corridor edits, community sets) | n/a: no other meaning |
| GL-55 | Content set | the user's drills, references, gate words or screens (FILE-APP-03) | n/a: no other meaning |
| GL-56 | Theme | an athlete-level swing theme from the themes reduction (FILE-ATH-03) | n/a: no other meaning |
| GL-57 | Gate | a check that decides whether a number may be trusted (`gate_record`, the gate catalogues) | n/a: no other meaning |
| GL-58 | Route | the instrument path that produced a measurement (part of its source) | n/a: no other meaning |
| GL-59 | Stub | an archived shot's swing file, thinned to its summary, rows and review (FILE-SHOT-10) | n/a: no other meaning |
| GL-60 | Window time | time relative to the start of the shot's analysed window (`analysis.timeBase: "window"`) | the capture clock |
| GL-61 | Measurement family | the catalogue `group` of a metric (`MetricDescriptor::group`, e.g. "Wrist & forearm"); a metric added within an existing group is not an IMC (§1.2) (ratified by Mark, 14 Nov 2026; DEC-107) | n/a: no other meaning |
| GL-62 | Diagnose, diagnosis | the session's work over its shots' findings: tiers, patterns, links, reasons, per-club mixes, the summary; reserved for the session (Mark, 14 Nov 2026; DEC-100). The usuals are not part of it (Mark, 15 Nov 2026; DEC-130) | firing findings (the shot's) |
| GL-63 | Shot derived data | layer 3 of M-SHOT-LAYERS: the shot outcome, the shot's tendencies (its three factors), carry against the usual and the flight-check overlay, computed for the shot with the session's usuals passed down; they feed the assessment and are not findings (Mark, 14 Nov 2026; DEC-121) | findings (GL-23) |
| GL-64 | Tendencies | the athlete level's tendencies, from session summaries (FILE-ATH-02) | the shot's tendencies (GL-33) |
| GL-65 | Assessment | layer 4 of M-SHOT-LAYERS: the shot's analysis and derived data checked against the diagnostic graph (GL-67, the diagnostic engine: Mark, 15 Nov 2026; DEC-132, DEC-140), which fires its findings (Mark, 14 Nov 2026; DEC-121) | the session's diagnosis (GL-62); 03's older cross-shot "assessment", now "diagnosis" (Mark, 14 Nov 2026; DEC-125) |
| GL-66 | Analysis graph | the per-shot graph of versioned analysis stages that makes the shot's analysis ([§2.1](#21-the-shapes-of-analysis)) (Mark, 15 Nov 2026; DEC-132) | the diagnostic graph (GL-67); bare "DAG", retired in this page |
| GL-67 | Diagnostic graph | the diagnostic engine's condition graph (pack + norms), which a shot's analysis and derived data are assessed against to fire its findings (Mark, 15 Nov 2026; DEC-132) | the analysis graph (GL-66); bare "DAG", retired in this page |
| GL-68 | Declared work-on | the condition the golfer declares they are working on: the declared focus, part of the session intent (OBJ-SESSION-INTENT) (Mark, 15 Nov 2026; DEC-138) | the session work-ons (GL-69); the athlete work-ons (GL-70) |
| GL-69 | Session work-ons | the session summary's ranked list of work-ons (OBJ-SESSION-SUMMARY) (Mark, 15 Nov 2026; DEC-138) | the declared work-on (GL-68); the athlete work-ons (GL-70) |
| GL-70 | Athlete work-ons | the athlete level's work-ons, kept by `AthleteDiagnostics` (Mark, 15 Nov 2026; DEC-138) | the declared work-on (GL-68); the session work-ons (GL-69) |

---

## 13. Decisions register

Every decision this page relies on. The ids are stable; 03's decision numbers are kept as DEC-01…DEC-30, so "03
decision 24" and DEC-24 are the same decision. 03 holds the rationale for DEC-01…DEC-30.

| Id | Decision | Source |
|---|---|---|
| DEC-01 | Every first-class domain object is C++; QML holds no domain objects or rules | 03 decision 1 |
| DEC-02 | Always grade: every live shot is graded whether or not a panel is on | 03 decision 2; under DEC-100: every live shot fires its findings |
| DEC-03 | Golfer input is stored at its level | 03 decision 3 |
| DEC-04 | The athlete library folder is stored on the record, kept on rename, migrated once | 03 decision 4; "stored on the record" reworded by DEC-151 (the folder is where `athlete.json` is found) |
| DEC-05 | `athlete.json` in the library; settings keep only "current athlete" | 03 decision 5 |
| DEC-06 | The flight check is a session-level overlay on unchanged rows | 03 decision 6; superseded by DEC-122 (shot derived data) |
| DEC-07 | Direction: the pack's corridors everywhere | 03 decision 7 |
| DEC-08 | The score pill: a design proposal for later | 03 decision 8 |
| DEC-09 | Held conditions: one rule | 03 decision 9 |
| DEC-10 | The new layer is `src/DomainModel` | 03 decision 10 |
| DEC-11 | A separate `AppState` in `src/App`; sessions have a status; repositories are identity maps | 03 decision 11 |
| DEC-12 | All domain changes go through aggregate-root commands, with domain events | 03 decision 12; "aggregate-root commands" reworded by DEC-104 (an entity's own facts by its own commands, reached through its root) |
| DEC-13 | Shot label corrections and shot intent on the shot's `review` block | 03 decision 13 |
| DEC-14 | The strangler migration of 04, the lint first | 03 decision 14 |
| DEC-15 | Instruments: the LM a device; one source-tagged measurement set; one precedence rule | 03 decision 15 |
| DEC-16 | The Recording contract | 03 decision 16 |
| DEC-17 | Studio/Rig: named now, designed in the platform refactor (capture) | 03 decision 17 |
| DEC-18 | A corrected club: re-analysis uses the effective club | 03 decision 18 |
| DEC-19 | Grading by `SessionDiagnostics`; the pipeline never grades | 03 decision 19; "`SessionDiagnostics` grades every shot" superseded by DEC-100 (the `Shot` fires its own findings; the session diagnoses) |
| DEC-20 | Intent runs pre-fill later shots; an edit affects only later shots | 03 decision 20 |
| DEC-21 | Recorded input, a third kind in P1 | 03 decision 21 (its rewrite rule refined by DEC-46) |
| DEC-22 | The five events; the cast with nothing captured; every file through its root, the archive and the report as `Session` commands, a swing file once by capture then by its `Session` | 03 decision 22 (its tuning-log writer superseded by DEC-43); "then by its `Session`" superseded by DEC-86 (then only by the `Shot`); `SessionRelocated` removed by DEC-154 |
| DEC-23 | The Platform ring | 03 decision 23 |
| DEC-24 | IM-1: the `analysisContext` block | 03 decision 24; step 1's brief §8.2 |
| DEC-25 | IM-2: the input stamp and the time base | 03 decision 25 |
| DEC-26 | IM-3: the club rule until step 6 (the effective club field by field: R-CLUB-3) | 03 decision 26 |
| DEC-27 | IM-4: capture writes in stages | 03 decision 27 |
| DEC-28 | IM-5: the interim writers until step 3 | 03 decision 28; "the `Session` takes them over" superseded by DEC-86 (the `Shot`) |
| DEC-29 | IM-6: `src/Recording` and `src/DomainModel` in step 1 | 03 decision 29 |
| DEC-30 | IM-7: no norm cohort in step 1 | 03 decision 30 |
| DEC-31 | Step 1's D1: the golfer's correction is the LM board's override only, until step 6 | step 1's brief §9 (Mark, 14 Nov 2026) |
| DEC-32 | Step 1's D2: a corrected club's length from a snapshot of the bag's geometry in the Recording | as DEC-31 |
| DEC-33 | Step 1's D3: one schema-tagged `analysisContext` block; the cohort recorded is the club set | as DEC-31 |
| DEC-34 | Step 1's D4: backfill height and club set into the old library once, dry run to Mark first, backups on the share | as DEC-31 |
| DEC-35 | Step 1's D5: one ball-track rule, today's re-analysis rule, on both paths; live cost measured | as DEC-31 |
| DEC-36 | Step 1's D6: staleness by the input stamp `kAnalysisInputsVersion`; no stage version moves | as DEC-31 |
| DEC-37 | Step 1's D7: create `src/Recording` now | as DEC-31 |
| DEC-38 | Step 1's D8: `shot_processor`'s allowlist lines shrink and are re-tagged | as DEC-31 |
| DEC-39 | Step 1's D9: after a club correction, stale only | as DEC-31 |
| DEC-40 | This page is canonical; 03 is the frozen rationale | Mark, 14 Nov 2026 (information-model audit, D1) |
| DEC-41 | `SessionId` is a uuid minted at Start, stored in a session file in the folder with the status and start/end times; the folder is a property only | as DEC-40 (D2) |
| DEC-42 | P3: each file has exactly one writer, inside its aggregate; derived owners write their own caches; source facts change only through the root's commands | as DEC-40 (D3); "the root's commands" reworded by DEC-104 |
| DEC-43 | `outcome_corrections.jsonl` is a source fact owned by `Athlete`, appended through the `Athlete` root on `LabelCorrected` | as DEC-40 (D4) |
| DEC-44 | An LM-only shot is a Recording: no streams, plus a shot-kind field | as DEC-40 (D5) |
| DEC-45 | The persistence tables list every file the app writes, with its current writer and target owner; deferred designs say so | as DEC-40 (D6) |
| DEC-46 | The club baseline: the athlete level computes it; the `Session` freezes it as a recorded input; rewritten only on a rule-version change, as a recorded event naming what it read | as DEC-40 (D7) |
| DEC-47 | The lead's 18 clarification marks are ratified as written (DEC-48…DEC-63; three marks repeat DEC-55) | as DEC-40 (D8) |
| DEC-48 | Contexts and rings are different cuts | ratified (DEC-47) |
| DEC-49 | The Recording's analysis context is the only source of athlete facts for analysis | ratified (DEC-47) |
| DEC-50 | The domain model defines the repository interfaces; adapters implement them | ratified (DEC-47) |
| DEC-51 | The owner thread: the GUI thread in the app, the main thread in a tool | ratified (DEC-47) |
| DEC-52 | `Athlete` loaded by `AthleteRepository` when first asked for; one instance per `AthleteId` | ratified (DEC-47) |
| DEC-53 | The `SessionIndex` is built from each session's summary, read through `SessionRepository` | ratified (DEC-47) |
| DEC-54 | Measurement kinds: a reading is source; a derived measurement is part of the recorded product | ratified (DEC-47) |
| DEC-55 | `work_ons.json` is written by `SessionDiagnostics` | ratified (DEC-47) |
| DEC-56 | `AthleteRepository` is an identity map too | ratified (DEC-47) |
| DEC-57 | `captureTarget` is the capture service's, read-only in `AppState` | ratified (DEC-47) |
| DEC-58 | The session level summarises intent (consistency within the session; shot intent against session intent) | ratified (DEC-47) |
| DEC-59 | `work_ons.json` is the session summary the athlete level reads | ratified (DEC-47) |
| DEC-60 | The athlete level is lazy: marked stale by events, recomputed when next read | ratified (DEC-47) |
| DEC-61 | Rule versions stay hand-kept but are checked by a golden or parity test | ratified (DEC-47) |
| DEC-62 | `swing_phasegrid.json` is written by the `Session` | ratified (DEC-47); superseded by DEC-103 (the `Shot` writes it) |
| DEC-63 | `app.state` is the `AppState`; `app.athlete` and `app.session` are shorthand | ratified (DEC-47) |
| DEC-64 | Pose re-runs only on a pose stage-version change, reused whatever model file the machine has | Mark, 14 Nov 2026 (to step 1.6) |
| DEC-65 | The archive through `SessionRepository` in step 2, `Session::archive()` in step 3, the report in step 5 | the lead's proposal; Mark agreed 14 Nov 2026 (04 steps 2, 3, 5) |
| DEC-66 | One atomic-write helper, with the repositories in step 2 | the lead's proposal; Mark agreed 14 Nov 2026 (04 step 2) |
| DEC-67 | No DRIVER stub for an undeclared club, in step 6 | the lead's proposal; Mark agreed 14 Nov 2026 (04 step 6) |
| DEC-68 | Q-01: §1.2's definition of an information-model change adopted as written; `CLAUDE.md` points at it | Mark, 14 Nov 2026 |
| DEC-69 | Q-09: events are delivered synchronously on the owner thread, in command order, carrying only the id of what changed | Mark, 14 Nov 2026 |
| DEC-70 | Q-10: add `SessionStarted`, `SessionArchived`, `SessionRestored`, `SessionTrashed`, `ShotDeclaredClubChanged`, `ShotCaptureCompleted` (EVT-17…22); split `ShotAnnotated` into `ShotClubCorrected` (EVT-04) and `ShotReadingExcluded` (EVT-05); no event for a rating or note; EVT-16 named `ClubBaselineRewritten` | Mark, 14 Nov 2026 |
| DEC-71 | Q-11: the `Athlete` trashes a session (`Athlete::trashSession`) | Mark, 14 Nov 2026 |
| DEC-72 | Q-12: until step 6, analysis and grading may name different clubs when only the popover names one: a known interim | Mark, 14 Nov 2026 |
| DEC-73 | Q-13: a session left InProgress by a crash stays so until `end` or the next `start` | Mark, 14 Nov 2026 |
| DEC-74 | Q-14: Charts and report reads of other sessions go through session results, not their phase grids (step 5) | Mark, 14 Nov 2026 |
| DEC-75 | Q-15: the input stamp includes handedness (step 1.6) | Mark, 14 Nov 2026 |
| DEC-76 | `CLAUDE.md`'s information-model rule points to §1.2 and restates nothing (CL19) | Mark, 14 Nov 2026 (round 2) |
| DEC-77 | Extending today's session is `Session::resume`: the same `SessionId`, Ended → InProgress → Ended (O1) | Mark, 14 Nov 2026 (round 2); the command is `Athlete::resumeSession`, for a session started today and of the same type only (DEC-134) |
| DEC-78 | `Athlete::startSession` starts sessions and enforces one InProgress, ending any InProgress one first (O2) | Mark, 14 Nov 2026 (round 2) |
| DEC-79 | Deleting an athlete moves the whole athlete folder to the recoverable trash (O3) | Mark, 14 Nov 2026 (round 2) |
| DEC-80 | Housekeeping: a "purged" end state in the lifecycles, owned by one housekeeping service, its age settings listed (O4) | Mark, 14 Nov 2026 (round 2) |
| DEC-81 | The session summary is the structured data behind the session report: one session-level product, defined once; the report renders it and the athlete level reads it; nothing is computed twice (O5) | Mark, 14 Nov 2026 (round 2) |
| DEC-82 | Follow-club rewrites only `analysisContext.club`; the seeded `review.club` move is a known interim until step 6 (O6) | Mark, 14 Nov 2026 (round 2); its rewrite of the captured club superseded by DEC-133 (the device club stored beside it, the same analysis result; today's overwrite a known interim until step 6) |
| DEC-83 | Markup truth files are shot-owned source facts (O7) | Mark, 14 Nov 2026 (round 2) |
| DEC-84 | A rule-version bump that would rewrite a recorded input needs Mark's approval first, proposed with a dry-run count (O8) | Mark, 14 Nov 2026 (round 2) |
| DEC-85 | Every statement not traceable to a decision of Mark's is marked "proposed"; Mark ratifies or rejects them as one list, and then the legend reads "no mark = ratified at version N" (O9) | Mark, 14 Nov 2026 (round 2); done at v1.5 |
| DEC-86 | P10: a session is a collection of shots with a context for them; everything specific to one shot is owned and managed by the `Shot`, its own object with its own commands, reachable only through its `Session`; the swing file's writer is the `Shot`, through the repository (replacing DEC-22's "then only by its `Session`"); the `Session` keeps the collection, its context and session-wide results, and hears every shot change through the shot's events (its grading line superseded by DEC-100) | Mark, 14 Nov 2026 (round 3) |
| DEC-87 | RAT-05: the shot's kind is the `Shot`'s own fact, written by the one header writer at creation; old files classified once by the repository's reader | ratified by Mark, 14 Nov 2026 |
| DEC-88 | RAT-06: the session type is session context, owned by the `Session` in the session file; each `Shot` records the type it was captured under in its Recording; SwingLab may override it for a run, the app never; it is in the input stamp | ratified by Mark, 14 Nov 2026 |
| DEC-89 | RAT-07: P1 forbids a derived fact in a source record; `lastSessionAt` and `sessionCount` are derived from the `SessionIndex` | ratified by Mark, 14 Nov 2026 |
| DEC-90 | RAT-08: nothing outside its own adapter keys on a foreign id | ratified by Mark, 14 Nov 2026 |
| DEC-91 | RAT-09: the swing number keeps today's rule (issued by the `Session`; unique, never reissued, gaps stay) | ratified by Mark, 14 Nov 2026 |
| DEC-92 | RAT-10: the session summary's fields, as the starting list; step 3 checks it field by field against the client report | ratified by Mark, 14 Nov 2026 |
| DEC-93 | RAT-12: the lifecycle states (LC-SHOT, LC-SESSION) and the empty-session discard at End | ratified by Mark, 14 Nov 2026; the discard's command and event: DEC-143 |
| DEC-94 | RAT-13: the command names, split: shot-local (`rate`, `note`, `correctClub`, `excludeReading`, `correctLabel`, `setShotIntent`, `setDeclaredClub`) on the `Shot`; session-wide (`addShot`, `trashShot`, `completeCapture`, `setIntentRun`, `setSessionIntent`, `relocate`, `restore`, `freezeClubBaseline`, `applyAnalysis`) on the `Session`; the `Athlete`'s as listed; step 3 may rename with approval | ratified by Mark, 14 Nov 2026; its placement of `applyAnalysis` and `completeCapture` on the `Session` superseded by DEC-103 (the `Shot`'s); `relocate` removed by DEC-154 |
| DEC-95 | RAT-14, changed: the `Shot` is its own object with its own commands for its own facts; obtainable only from its `Session`; every change saved (one writer), evented in command order and propagated (re-fire, summary) by that `Session`; its kind never changes | Mark, 14 Nov 2026; "saved, evented … by that `Session`" superseded by DEC-103 (the `Shot` saves and emits; the `Session` hears and propagates) |
| DEC-96 | RAT-15: `ShotDiagnostics` never outlives its session; before its findings are fired it has no value ("-"), never a zero | ratified by Mark, 14 Nov 2026 |
| DEC-97 | RAT-19: R-PERS-8, moving source facts copies and verifies first | ratified by Mark, 14 Nov 2026 |
| DEC-98 | RAT-22: session resume emits `SessionResumed` (EVT-23) | ratified by Mark, 14 Nov 2026 |
| DEC-99 | RAT-30: `Athlete::delete` is refused while one of the athlete's sessions is InProgress | ratified by Mark, 14 Nov 2026 |
| DEC-100 | "A shot fires findings whilst a session diagnoses": the `Shot` fires its own findings, a pure function of its analysis and the context its `Session` passes down (club baseline, norms, effective club); findings are the `Shot`'s facts; the `Session`'s diagnostics diagnose from them (tiers, patterns, links, reasons, usuals, per-club mixes, the summary); a corridor or norm edit → the `Session` asks each shot to re-fire, without re-analysis; "fire findings"/"finding" replace "grading"/"grade"/"row" at shot level, "diagnose"/"diagnosis" are the session's; step 3 renames `detectShot` (e.g. `fireFindings`). Supersedes DEC-19's "`SessionDiagnostics` grades every shot" and DEC-86's grading line | Mark, 14 Nov 2026 (round 3); "usuals" among what is diagnosed superseded by DEC-130 (a session aggregate from readings, not part of the diagnosis); where the findings are kept: DEC-135 (the `Shot` fires them, its `Session` keeps them) |
| DEC-101 | "The findings have a 1:1 relationship with the shot that fired them. A session is a collection of shots and findings": P10 reads "a `Session` is a collection of shots and their findings, plus the context they were taken in" | Mark, 14 Nov 2026 (round 4) |
| DEC-102 | Q-23: findings persist with the shot that fired them, a stamped shot cache in the shot's folder (FILE-SHOT-13) written by the `Shot` through `RecordingRepository`; `diagnostics.json` keeps only the session's diagnosis | Mark, 14 Nov 2026 (round 4); **superseded by DEC-135**: the `Session` keeps each shot's findings in `diagnostics.json` (FILE-SESS-03); FILE-SHOT-13 retires |
| DEC-103 | Q-22 (a)–(c), resolved by DEC-86 as the later decision: the `Shot` saves and emits its own changes (superseding DEC-95's "by that `Session`"); `applyAnalysis` and `completeCapture` are the `Shot`'s (superseding DEC-94's earlier placement); the phase grid's writer is the `Shot` (superseding DEC-62) | Mark, 14 Nov 2026 (round 4) |
| DEC-104 | Q-22 (d): P3 reads "source facts change only through their owner's commands: an aggregate root's, or, for an entity's own facts, the entity's, reached through its root" (rewording DEC-42 and DEC-12) | Mark, 14 Nov 2026 (round 4) |
| DEC-105 | RAT-01: the change log records every edit with ids; ids are stable and never reused; reviewers check §11 | ratified by Mark, 14 Nov 2026 |
| DEC-106 | RAT-02: C5's stricter wording, applied: "identifying an athlete, a session or a shot by anything but its id, or adding an id type" | ratified by Mark, 14 Nov 2026 |
| DEC-107 | RAT-03: "measurement family" is the catalogue `group` of a metric | ratified by Mark, 14 Nov 2026 |
| DEC-108 | RAT-04: besides capture and the `Shot`, the only writes of a swing file are migrations Mark approves one by one | ratified by Mark, 14 Nov 2026 |
| DEC-109 | RAT-16: a series is metric × source × shot; its invalidation list | ratified by Mark, 14 Nov 2026 |
| DEC-110 | RAT-17: `SessionDiagnostics` holds an immutable snapshot of what it passes down, and never reads another session | ratified by Mark, 14 Nov 2026 |
| DEC-111 | RAT-18: the repositories' minimum operations | ratified by Mark, 14 Nov 2026 |
| DEC-112 | RAT-20: every chain that changes a session summary ends by marking the athlete level stale | ratified by Mark, 14 Nov 2026 |
| DEC-113 | RAT-21: the recompute chains of EVT-16…EVT-23 | ratified by Mark, 14 Nov 2026; EVT-18's "the stubs keep the findings" corrected by DEC-135, EVT-23's producer by DEC-134 |
| DEC-114 | RAT-23: the files of §8.3 are not domain | ratified by Mark, 14 Nov 2026 |
| DEC-115 | RAT-24: a file not in §8 is not the app's to write | ratified by Mark, 14 Nov 2026 |
| DEC-116 | RAT-25: the fact kinds and target owners of the files DEC-45 added | ratified by Mark, 14 Nov 2026 |
| DEC-117 | RAT-26: the kinds of the preference and setting keys | ratified by Mark, 14 Nov 2026 |
| DEC-118 | RAT-27: the glossary | ratified by Mark, 14 Nov 2026 |
| DEC-119 | RAT-28: the code renames the glossary implies | ratified by Mark, 14 Nov 2026 |
| DEC-120 | RAT-31: a change to a lint rule's scope, or to A4's list, is an information-model change | ratified by Mark, 14 Nov 2026 |
| DEC-121 | RAT-32, answered by Mark (replacing the lead's reading): the shot's layers, M-SHOT-LAYERS (acquired → processed and analysed → shot derived data → assessment against the DAG → findings); the outcome and the shot's tendencies (its factors) and carry are shot derived data that feed the assessment, not findings; "the shot's tendencies" are kept apart from the athlete level's Tendencies | Mark, 14 Nov 2026 (round 5); "the DAG" is the diagnostic graph (DEC-132) |
| DEC-122 | The flight-check overlay is shot derived data, computed for the shot with the session's usuals passed down, like the outcome; it feeds the assessment that fires findings. Supersedes DEC-06's "session-level overlay" | Mark, 14 Nov 2026 (round 5) |
| DEC-123 | RAT-29, changed by Mark: `WristDiagnosticsModel` retires, with no separate wrist model; the wrist-pattern identification is an analysis of the shot, and its result is part of the shot's findings through the one diagnostic model. `SessionReportListModel` → a presenter over the `SessionIndex` (step 5) and `RecordedClub`/`ClubGeometry` → the one `Club` value: ratified | Mark, 14 Nov 2026 (round 6) |
| DEC-124 | Q-25: the shot's derived data lives with its findings, in one stamped shot cache in the shot's folder (FILE-SHOT-13), written by the `Shot` and recomputed when its inputs change; readers never recompose it | Mark, 14 Nov 2026 (round 6); **moved by DEC-135**: "together, never recomposed by readers" kept; the place is the `Session`'s FILE-SESS-03, not FILE-SHOT-13 |
| DEC-125 | Q-26: the shot layer keeps "assessment" (assessment fires findings); the older cross-shot use is "diagnosis" | Mark, 14 Nov 2026 (round 6) |
| DEC-126 | A failed `atomicWrite` is surfaced in the app, as Stage 0.4c reports a blocked `diagnostics.json` | Mark, 14 Nov 2026 (the triage; 04 step 2) |
| DEC-127 | A norm's standing is a typed field: provisional or decided, by whom and when | Mark, 14 Nov 2026 (04 step 3 item 6) |
| DEC-128 | Step 1's reuse gate: the shaft track, the ladder and the DTL club track are reused only when the stamp's club inputs match | Mark, 14 Nov 2026 (step 1's brief, approved; 04 step 1) |
| DEC-129 | `CLAUDE.md` is kept local, not tracked in git: asked "Track CLAUDE.md in git?", Mark answered "Keep it local" | Mark, 14 Nov 2026 |
| DEC-130 | MD-01: the usuals are a session aggregate computed from the shots' readings, never from findings, and are not part of the session's diagnosis; an input to shot layer 3 (M-SHOT-LAYERS). `SessionDiagnostics` may compute and hold them. Chain: EVT-01, 03, 04, 05, 22 → that club's usuals → every same-club shot recomputes its derived data and re-fires → the diagnosis → the summary → the athlete level stale. Supersedes DEC-100's "usuals" among what is diagnosed | Mark, 15 Nov 2026 (the morning walk; the lead's refinement of the auditor's recommendation, chosen by Mark) |
| DEC-131 | MD-02: a label correction re-fires the shot's findings; EVT-06's chain corrected to DEC-121 | Mark, 15 Nov 2026 (the morning walk) |
| DEC-132 | MD-03: two names, "the analysis graph" and "the diagnostic graph"; bare "DAG" retired in this page | Mark, 15 Nov 2026 (the morning walk) |
| DEC-133 | MD-04: the captured club is a source fact, never overwritten; follow-club stores the device's club beside it as its own source fact; the effective-club rule prefers it when follow-club is on. The same analysis result as DEC-82; today's overwrite stays a known interim until step 6 | Mark, 15 Nov 2026 (the morning walk) |
| DEC-134 | MD-05: `Athlete::resumeSession`, for a session started today only; it ends any other InProgress session first; resume requires the same session type, as today's extend. The lead had recommended "latest, any date"; Mark chose today's. Remaining risk: a session that crashed late at night cannot be resumed next morning, and a new session starts | Mark, 15 Nov 2026 (the morning walk; the type: step 2's brief §8.4) |
| DEC-135 | MD-06: "the shot fires findings but the session keeps them as a collection. the shot does not use the findings and the session does. so the session stores and processes all findings just as it did before." **Supersedes DEC-102 and moves DEC-124**: the `Shot` fires its findings and computes its derived data and posts both, stamped; the `Session` keeps them per shot as its collection in `diagnostics.json` (FILE-SESS-03), as today. FILE-SHOT-13 retires. DEC-124's "together, never recomposed by readers" kept; only the location changes. An archive stub needs nothing extra (the session file stays in the library) | Mark, 15 Nov 2026 (the morning walk, MD-06 and its follow-up) |
| DEC-136 | MD-07: a typed `SessionIndex` entry, status included | Mark, 15 Nov 2026 (the morning walk) |
| DEC-137 | MD-08: one purge command; SET-14 off documented as a permanent delete of raw frames, shown with a warning | Mark, 15 Nov 2026 (the morning walk) |
| DEC-138 | MD-09: three terms: "declared work-on" (session intent), "session work-ons" (the summary's ranked list), "athlete work-ons" (`AthleteDiagnostics`) | Mark, 15 Nov 2026 (the morning walk) |
| DEC-139 | MD-10: add C11 for principles and rules only; glossary wording may be edited without approval if it cites the decision it follows | Mark, 15 Nov 2026 (the morning walk) |
| DEC-140 | MD-12: all 15 sentences accepted as written (T1, T2, SET-09's four rig keys, SET-18, SET-19, T6, T3 and T4 reworded per DEC-130, DEC-131, DEC-132, T5 with the lead's fix that `report_meta.json`'s coach note and send log are source facts kept across remakes, T9, T10, EXT-10, EXT-11), with one rewording for T7 and T8: "the reason for keeping norms with the findings is not that they are a source fact per-se, it is that they are from the model at the time of the session and may change later": the stamp records the norm sets and grade policy in force when the findings fired, because they may change later; the settings themselves are the user's choice. T6 is reversed by DEC-153 | Mark, 15 Nov 2026 (the morning walk) |
| DEC-141 | Q-17: the `Shot` is a QObject child of its `Session`, exposed to QML directly; it reads the snapshot (baseline, pack, norms) from its parent. The lead had recommended a value and a passed snapshot. Remaining design point for step 3: a re-fire on a worker must read the snapshot as an immutable copy and post results to the GUI thread | Mark, 15 Nov 2026 (the morning walk) |
| DEC-142 | Q-27: `Session::addShot` allocates the next swing number, creates the folder through `RecordingRepository::create` and returns the `Shot`; capture writes into it; late parts through `completeCapture`. Order: allocate → create → capture writes → late parts | Mark, 15 Nov 2026 (the morning walk) |
| DEC-143 | Q-28: `Session::end` on an empty session asks `Athlete::trashSession`; EVT-20 fires | Mark, 15 Nov 2026 (the morning walk) |
| DEC-144 | S1-a (step 1.6): an unstamped swing counts as analysed with its captured club; only corrected clubs re-track the shaft; R-ANA-7 says so | Mark, 15 Nov 2026 (the morning walk) |
| DEC-145 | S1-b (step 1.6): keep the relabel: a handedness change re-runs the shaft and the ladder; pose keypoints are kept with lead and trail swapped. Mark first chose a pose re-run; after the lead showed that handedness only labels the wrists: "you are right, there is no need to rerun pose" | Mark, 15 Nov 2026 (the morning walk) |
| DEC-146 | S1-g: A1 widened to forbid `Export`/`Recording` → `Gui` includes; today's hits allowlisted with the step that removes each (this answer is Mark's approval of those allowlist lines) | Mark, 15 Nov 2026 (the morning walk) |
| DEC-147 | S1-k (step 1.8): the top-level `shotKind` = "captured" / "deviceOnly" ratified; no backfill; old files by the content rule | Mark, 15 Nov 2026 (the morning walk) |
| DEC-148 | Step 2's D1: a new `src/Identity/` (Vocabulary ring) for `AthleteId`, `SessionId`, `ShotRef` (the lead had recommended `src/Recording/identity.h`). A new directory is a C9 change, approved by this answer; the ring table (§4.1) and the lint's ring map gain it | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-149 | D2: the session file is `session.json`, visible | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-150 | D3: old sessions get a v5 name-based uuid (the athlete's id + the folder name); new sessions a random uuid at Start | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-151 | D4: the athlete's folder is where `athlete.json` is found; no `folder` key. IM-S2-9 approved | Mark, 15 Nov 2026 (step 2's brief §8.1, §8.2) |
| DEC-152 | D5: the Wrist chart's reference ★ swing lives on the athlete record as a `ShotRef`, one per athlete (today one per machine for all athletes, never set on the Mac). IM-S2-11 approved | Mark, 15 Nov 2026 (step 2's brief §8.1, §8.2) |
| DEC-153 | D6: the club-length prior (SET-05) is learned rig state, per machine, keyed by `AthleteId` + club + camera, learned under the captured club; owner Studio/Rig. Reverses Mark's T6 answer (DEC-140). IM-S2-10 approved | Mark, 15 Nov 2026 (step 2's brief §8.1, §8.2) |
| DEC-154 | D7: a library re-pointed mid-session ends the session and starts a new one (same type and club, declared focus carried); `Session::relocate` and EVT-11 removed; Q-03 closed. IM-S2-8 approved | Mark, 15 Nov 2026 (step 2's brief §8.1, §8.2) |
| DEC-155 | D8: libppswing's `writeFileAtomic` kept as the store's documented exception to R-PERS-2 (it syncs the file and the folder); its per-process temp name noted for libppswing, outside step 2 | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-156 | D9: the report files: `SessionFiles` names them now and the report reads its meta through the repository; the writes move in step 5 | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-157 | D10: one allocator: device-only LM shots go into the running session; the LM controller's second allocator is deleted; existing split sessions are left as they are | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-158 | D11: short locks per write; a tool refuses a session whose `session.json` says InProgress (`--allow-in-progress` for one a crash left) | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-159 | D12: no edit of a past intent run, except by resuming (today's session only, DEC-134); Q-04 closed | Mark, 15 Nov 2026 (step 2's brief §8.1) |
| DEC-160 | IM-S2-1…7 approved: the session file, its name and keys, `origin` included; `SessionId` for old sessions by v5 derivation, `ShotRef` the key of `RecordingRepository` and `RecordingReader`, the folder name a property and provenance only; golfer input out of the cache (`session_input.json`'s keys; the `review` keys `shotIntent` and `labelCorrections`); until step 3 the session file and session input written by today's callers through `SessionRepository` and the athlete record by `AthleteController` through `AthleteRepository`; the `Athlete`'s step-2 form; A4 extended to every FILE-SESS and FILE-ATH name; `atomicWrite` and `FolderLock` in the Platform ring, the lock files not domain. IM-S2-8…11 are DEC-154, DEC-151, DEC-153, DEC-152 | Mark, 15 Nov 2026 (step 2's brief §8.2) |

---

## 14. Open decisions

Each has the step whose brief brings it to Mark, the interim rule (what holds until then), and a recommendation. A closed question keeps its row and id, with the decision that closed it.

| Id | Question | Step | Interim | Recommendation |
|---|---|---|---|---|
| Q-01 | Confirm v1.0's additions to "what counts as a change" (§1.2): removal, rename, split and merge; schema-version changes; domain-fact settings keys; lifecycles (C8); the four exclusions | now | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-68) |
| Q-02 | The owner and repository of packs and norms (the user's pack, corridor and content edits; the disabled sets, SET-06) | 3 | today's editors through `atomicWrite` | 04 step 3's brief |
| Q-03 | A library re-pointed mid-session: does the session continue in the new library (today: split across two places), or end and a new one start? What `SessionRelocated` recomputes | 2 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-154): the session ends and a new one starts in the new library, same type and club, the declared focus carried; `Session::relocate` and `SessionRelocated` (EVT-11) removed |
| Q-04 | Can a past intent run be edited after its session ends? | 2 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-159): no, except by resuming the session (today's only, DEC-134) |
| Q-05 | Does firing findings read the norm cohort from the Recording? (Today it resolves it through the live record.) | 3 | as today | 04 step 3's brief |
| Q-06 | Where `AthleteDiagnostics` caches the athlete baselines and distances | 3 | club distances are cached in `tendencies.json` under `clubDistances` (today: `athlete_controller.cpp`) | 04 step 3's brief |
| Q-07 | With nothing being captured, which shot of the session on screen the cast shows: the newest, or the selection's | 4 | as today | 04 step 4's brief |
| Q-08 | Where the club-length prior (SET-05) lives, and whether it learns under the effective club | 2 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-153): learned rig state, per machine, keyed by `AthleteId`, club and camera, learned under the captured club; owner Studio/Rig |
| Q-09 | Event mechanics: payloads beyond the identity, synchronous or queued delivery on the owner thread, ordering between events of one command | 3 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-69) |
| Q-10 | Events the code needs but that are not defined: a session started, archived, restored or trashed; an athlete created or deleted; a declared-club edit; a late capture part (a late LM reading, a phone clip, follow-club); a rating or note; the name of EVT-16; and splitting `ShotAnnotated` (EVT-04, EVT-05) into two names. An athlete switch is an `AppState` signal, not a domain event; a stage-version bump is found by the stamp at load, not by an event. | 3 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-70) |
| Q-11 | Who trashes a session: the `Athlete` (it owns the index) or the `Session` | 3 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-71) |
| Q-12 | Until step 6, when only the popover names a club, the analysis uses the captured club's name (R-CLUB-3) while grading and presenters use the declared club (R-CLUB-2) | 6 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-72) |
| Q-13 | What a session left InProgress by a crash becomes | 3 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-73) |
| Q-14 | Day-level presenters (Charts delivery, the report) read other sessions' shot caches (phase grids): an athlete-level read of shot facts (C6) | 5 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-74) |
| Q-15 | Does the input stamp name handedness? The effective context carries it; DEC-25 and DEC-36 list the club, its length and source, height and club set | 1.6 | n/a: closed | **Closed**: as recommended (Mark, 14 Nov 2026; DEC-75) |
| Q-16 | An athlete created or deleted: does it need a domain event? (DEC-70 did not cover it) | 2 | none; the athlete list is the `AthleteController`'s | none: which athletes exist is read by the app from `AthleteRepository`; add one only when a derived fact depends on it |
| Q-17 | The `Shot`'s C++ form: a QObject, or a value reached through its session (QML reaches it typed either way, P9); and how a `Shot` that re-fires itself gets its `Session`'s snapshot of the baseline and norms | 3 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-141): a QObject child of its `Session`, exposed to QML directly, reading the snapshot from its parent. Left for step 3's brief: how a re-fire on a worker reads the snapshot as an immutable copy and posts its results to the GUI thread |
| Q-18 | The memory cost of `SessionDiagnostics` when every loaded session always runs (DEC-02) in a sweep over many sessions | 3 | n/a: no owner yet | measure it in step 3's parallel run; bound it by releasing sessions a sweep has finished with |
| Q-19 | The typed definitions of the session's results (links, mixes, reasons) and so of the session summary's fields (DEC-92) | 3 | today's `QVariant` shapes | a deliverable of step 3 |
| Q-20 | A machine-readable form of this page and a check that §11 matches the code | later | none | after step 2, as a tool that reads the FILE, SET and §11 rows (04 "Later") |
| Q-21 | Where the wrist view's reference shot lives (SET-17), and its key | 2 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-152): on the athlete record, as a `ShotRef`, one per athlete |
| Q-22 | Where P10 (DEC-86) and earlier decisions disagree: (a) who saves and emits a shot's changes: the `Shot`, through the repository (DEC-86), or its `Session` (DEC-95); (b) whether `applyAnalysis` and `completeCapture` are the `Shot`'s (DEC-86) or the `Session`'s (DEC-94); (c) whether the phase grid is written by the `Shot` (DEC-86) or the `Session` (DEC-62); (d) P3's "source facts change only through the root's commands" (DEC-42, DEC-12) against the `Shot`'s own commands | 3 | n/a: closed | **Closed** (Mark, 14 Nov 2026; DEC-103, DEC-104) |
| Q-23 | Where a shot's findings persist (today in `diagnostics.json`, a session file, FILE-SESS-03) now they are the `Shot`'s facts (DEC-100) | 3 | n/a: closed | **Closed** (Mark, 14 Nov 2026; DEC-102); DEC-102 superseded by DEC-135 (the `Session` keeps the findings in FILE-SESS-03) |
| Q-24 | Whose are the shot results that take the session's usuals (the flight-check overlay, outcome, the three factors, carry against the usual)? | 3 | n/a: closed | **Closed by Mark** (14 Nov 2026; DEC-121, DEC-122): they are the shot's derived data (M-SHOT-LAYERS layer 3), not findings; they feed the assessment that fires findings; the flight-check overlay is shot derived data too, superseding DEC-06 |
| Q-25 | Where the shot's derived data (OBJ-SHOT-DERIVED) persists, if at all: with the findings cache (FILE-SHOT-13), in the analysis, or recomputed on read | 3 | n/a: closed | **Closed** (Mark, 14 Nov 2026; DEC-124): the derived data lives with the findings, in one stamped shot cache (FILE-SHOT-13) the `Shot` writes and recomputes when its inputs change; readers never recompose it. Moved by DEC-135: together, in the `Session`'s FILE-SESS-03 |
| Q-26 | "Assessment" now names layer 4 of M-SHOT-LAYERS (DEC-121), but 03 §0.3 and §2.1's cross-shot row also list "assessment" among the cross-shot reductions. Which keeps the word? | now | n/a: closed | **Closed** (Mark, 14 Nov 2026; DEC-125): the shot layer keeps "assessment" (assessment fires findings); the older cross-shot use is "diagnosis" |
| MD-01 | What the session's usuals are computed from, and what a change of them recomputes. As written, findings → usuals → derived data → findings is circular; today the usuals come from readings and every same-club shot is re-judged | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-130): a session aggregate from the shots' readings, never from findings, not part of the diagnosis; EVT-01, 03, 04, 05, 22 → that club's usuals → every same-club shot recomputes its derived data and re-fires → the diagnosis → the summary → athlete stale |
| MD-02 | Does a label correction make the shot re-fire its findings? (EVT-06 says no; DEC-121 has the outcome feed the assessment) | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-131): yes, it re-fires |
| MD-03 | "DAG" means both the analysis stage graph and the diagnostic engine's condition graph | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-132): "the analysis graph" and "the diagnostic graph" (GL-66, GL-67) |
| MD-04 | Is the captured club a source fact that follow-club may rewrite, against P1's rule for a recorded input? | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-133): a source fact, never overwritten; the device club stored beside it; today's overwrite a known interim until step 6 |
| MD-05 | Who resumes a session, and which, without breaking "one InProgress per athlete"? | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-134): `Athlete::resumeSession`, for a session started today only, of the same type, ending any other InProgress one first |
| MD-06 | The session ledger of every shot's findings, and the archive stubs that keep them, against DEC-102 | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-135, superseding DEC-102 and moving DEC-124): the `Session` keeps each shot's findings and derived data in FILE-SESS-03; FILE-SHOT-13 retires; the stub needs nothing extra |
| MD-07 | What a `SessionIndex` entry holds; `startSession` and `delete` depend on its status | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-136): a typed entry, status included |
| MD-08 | Housekeeping's `purge` command; SET-14 off is a permanent delete of raw frames; the OS trash | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-137): as recommended; the OS trash is Q-29 |
| MD-09 | "Work-on" has three meanings | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-138): the three terms (GL-68…GL-70) |
| MD-10 | Does an edit to a principle, a rule (R-*) or a glossary term need approval? | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-139): C11 for principles and rules only; glossary wording editable without approval when it cites its decision |
| MD-11 | Commit the reference; track `CLAUDE.md` | before step 2's brief | n/a: closed | **Closed** (Mark, 14 Nov 2026; DEC-129): the reference committed as v1.7; `CLAUDE.md` stays local |
| MD-12 | The sentences the final re-audit found untraceable (T1–T12) and the ones added in v1.7 with the same mark (§15), all marked "(proposed, for Mark; MD-12)" | before step 2's brief | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-140): all accepted, T7 and T8 reworded; T6 reversed by DEC-153 |
| Q-27 | The shot-creation protocol: who allocates the swing number (FILE-SESS-10) before capture writes `swing_NNNN/`; whether `Session::addShot` creates the folder or registers the one capture wrote; their order | 3 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-142): `Session::addShot` allocates, creates the folder through `RecordingRepository::create` and returns the `Shot`; allocate → create → capture writes → late parts |
| Q-28 | The empty-session discard's command and event (DEC-93): does `Session::end` call `Athlete::trashSession`, and does EVT-20 fire? | 3 | n/a: closed | **Closed** (Mark, 15 Nov 2026; DEC-143): yes, and EVT-20 fires |
| Q-29 | The OS emptying its own trash: a permanent delete, outside housekeeping's one `purge`, of what the app moved there. MD-08 raised it; DEC-137 does not cover it | 3 | as today | n/a: for Mark with step 3's brief |

---

## 15. Change log

| Version | Date | Change | Approved |
|---|---|---|---|
| 0.x | to 13 Nov 2026 | Unversioned: written from 03 §0–§9 (step G); refined by step 1 (3586cd1b, 108e7858) | Mark (03 decisions 1–30) |
| 1.0 | 14 Nov 2026 | Made canonical (DEC-40). Ids for every principle, rule, object, identity, event, file, settings key, change kind, decision and open question. Applied DEC-41…DEC-47: `SessionId` and the session file; P3's wording; the tuning log a source fact of the `Athlete`; the shot kind; every written file and domain-fact settings key listed; the club baseline's producer and rewrite; the 18 clarifications ratified. Added: §1.2's definition of a change (Q-01 for its additions); §5.2 identities; the object tables split into what, lifecycle and reach; §5.4 lifecycles; §5.5 class specifications; §5.8's four club terms; EVT-16; §8.2 settings; §8.3 non-domain files; §11.2 today's types; §12–§15. Fixed: §11 brought to `108e7858`; the interim writer list; `swing_summary.json` reclassified as a cache; the effective club defined field by field; the cast's shot marked open; the lint's per-file placements in `Analysis`. Pose reuse (DEC-64) recorded. | Mark's decisions DEC-40…DEC-47, DEC-64. The rest was the lead's, not approved, and wrongly called "editorial" here until v1.2, which marks it RAT-01…RAT-31 |
| 1.1 | 14 Nov 2026 | Closed Q-01, Q-09…Q-15 as recommended (DEC-68…DEC-75): §1.2 adopted; event delivery rule (§7.4); EVT-04 renamed `ShotClubCorrected` and EVT-05 `ShotReadingExcluded` (ids kept); EVT-17…EVT-22 added; EVT-06 and EVT-16 carry only their id; `Athlete::trashSession` (§5.4, §5.5, FILE-LIB-01); crash rule for InProgress (§5.4, §5.5); R-CLUB-3's known interim; FILE-SHOT-07's cross-session reads; handedness in the input stamp (R-ANA-5); §11 rows. Added Q-16. | Mark (DEC-68…DEC-75) |
| 1.2 | 14 Nov 2026 | **Mark's decisions applied** (DEC-76…DEC-85): the header's "editorial fix" exemption removed, §1.2 the only definition (DEC-76); LC-SESSION's `Session::resume` (DEC-77; EVT-23 proposed); `Athlete::startSession` and the one-InProgress invariant moved to CLS-ATHLETE (DEC-78); `Athlete::delete` trashes the whole folder (LC-ATHLETE, FILE-LIB-01; DEC-79); housekeeping and the purged end state (§5.4, SET-10…SET-14, FILE-LIB-01/02; DEC-80); OBJ-SESSION-SUMMARY and OBJ-REPORT (DEC-81); R-REC-2(a), CLS-RECORDING, OBJ-ANALYSIS-CONTEXT, FILE-SHOT-02, §5.8 (DEC-82); FILE-SHOT-12 (DEC-83); the §1.2 definition's version-bump exclusion (both copies), P1, R-STAMP-4 (DEC-84); the legend's proposed mark and the 31 RAT marks (DEC-85). **Changes from the re-audit**: the legend's "today" row and "no mark" row; ids LC-, CLS-, RING-, EXT-, GL-; §1.4 the lint rules (from 03 §11) and A4's coverage; RING-* rows made exact (Gui/shot, IMU, Pose, Buffer files) and the IMU, Pose and Gui spanning rows; the focus-names table restored to §4.1 (dropped by mistake in v1.0); §2.2's Capture facts row (RAT-06); ID-SESSION minter; ID-NORM-SET; ID-SWING-NUMBER (RAT-09); foreign-id rows; OBJ-BAG's fields; OBJ-LIBRARY's fallback; OBJ-PACKS-NORMS's level (OPEN); OBJ-RIG's level; the reach table's Consumes column derived from §7.4; EVT-04…07, 10, 12, 13 chains extended (RAT-20); EVT-16…23 chains (RAT-21); R-DER-10; R-ANA-5 session type (RAT-06); R-ANA-7 pose saved (SET-16); R-STAMP-1 grade policy (SET-15); SET-15…SET-17; FILE-SHOT-01/10 writer today (`SwingStore::save`); FILE-SESS-01 type (RAT-06); FILE-SESS-03 `regrade_ledger`; FILE-SESS-11 resumed ceremonies and its pool thread; FILE-SESS-12; FILE-LIB-04, FILE-LIB-05; FILE-APP-05's shipped files; EXT-01…09 (markup moved to FILE-SHOT-12; logs, stills caches, CSL export, tool outputs added); bare cells filled; "live" and "swing" wording in §2.2, §3, §7.2, CLS-CLUB; GL-01…GL-61 with the new terms; §11.1 rows for DEC-77…83, SET-17, `regrade_ledger`, the ceremony thread, and the switch guard's wording; §11.2 rows for `WristDiagnosticsModel`, `SessionReportListModel`, `RecordedClub`/`ClubGeometry`; Q-06's interim corrected; Q-17…Q-21 added. Proposals RAT-01…RAT-31 marked, not decided. | Mark (DEC-76…DEC-85); RAT-01…RAT-31 await Mark |
| 1.3 | 14 Nov 2026 | **P10 and the findings refinement** (DEC-86, DEC-100): P10 added; P3 notes the conflict (Q-22); §2.1 "Firing findings" replaces "Grading" (intro, shape row, the per-shot DAG "never fires findings", the cross-shot row); the contexts table and diagram; R-REC-1 (the `Shot` writes, replacing DEC-22's "then only by its `Session`"); R-REC-3 (replacing DEC-28's "the `Session`"); §5.1 tree (shots, findings, `SessionDiagnostics`); OBJ-SHOT, OBJ-SHOT-RESULTS, OBJ-SHOT-DIAG, OBJ-SESSION-DIAG, OBJ-CLUB-BASELINE, OBJ-PHASE-GRID, OBJ-ANALYSIS, OBJ-SHOT-REVIEW (what, lifecycle, reach); the reach table's OBJ-SESSION, OBJ-SHOT, OBJ-RECORDING, OBJ-ANALYSIS, OBJ-SHOT-REVIEW, OBJ-SHOT-RESULTS rows; LC-SHOT; §5.5 intro; CLS-SESSION, CLS-SHOT, CLS-RECORDING, CLS-SESSION-DIAG, CLS-SHOT-DIAG, CLS-ATHLETE, CLS-ATHLETE-DIAG, CLS-PACKS-NORMS; R-CLUB-2…4; §5.9 steps 1–4; R-CMD-1; §7.1 shot and session rows; R-DER-2, R-DER-5; R-ANA-3; R-CON-2; §7.4 intro and the producers of EVT-02, 04…07, 21, 22, and the chains of EVT-01, 04…06, 12, 13, 16, 18, 21, 22; FILE-SHOT-01, 02, 07, 09, 10, 12; FILE-SESS-03; SET-06, SET-15; §11.1 rows (the swing-file writer, the `Shot` entity, `fireFindings()`, the findings/diagnosis row, the cohort, the undeclared club, C-02, the club interim); §11.2 `SessionReviewController`, `SessionDiagnosticsModel`; GL-22 "Firing findings", GL-23 "Finding", GL-62 "Diagnose", and GL Ledger, Overlay, Corridor, Condition; Q-05 wording. **Ratified** (DEC-87…DEC-99): RAT-05, 06, 07, 08, 09, 10, 12, 13 (split), 14 (changed), 15, 19, 22, 30, their marks replaced. **Step 1.5**: §2.2 "In the code" rows for `RecordingReader`, `AnalysisService`/`integrityFor()`, the capture snapshot; §11.1's row for them (done, in review) and its "Current at" (`77ff74a7`). **Register**: DEC-86…DEC-100; supersession notes on DEC-02, DEC-19, DEC-22, DEC-28; the conflict note on DEC-62. **Open**: Q-22 (the conflicts), Q-23 (where findings persist), Q-24 (the usual-taking shot results). | Mark (DEC-86…DEC-100) |
| 1.4 | 14 Nov 2026 | P10 reworded: a `Session` is a collection of shots and their findings, findings 1:1 with their shot (DEC-101). Q-23 closed (DEC-102): FILE-SHOT-13 added; FILE-SESS-03 keeps only the diagnosis; OBJ-SHOT-RESULTS lifecycle and §7.1's "cached in". Q-22 closed (DEC-103, DEC-104): R-REC-1 (the `Shot` saves); P3 reworded and its Forbids ("its owner's command"); OBJ-PHASE-GRID, OBJ-ANALYSIS, the reach row of OBJ-SHOT, LC-SHOT, §5.5 intro, CLS-SESSION, CLS-SHOT (invariants, commands), §5.9 step 3, §7.4 intro, EVT-02 and EVT-22 producers, FILE-SHOT-01 and FILE-SHOT-07 targets: their Q-22 marks replaced; supersession notes on DEC-12, DEC-42, DEC-62, DEC-94, DEC-95. Q-24 closed as the lead's reading, RAT-32, for Mark to confirm: §2.1's cross-shot row, the §11.1 findings row, OBJ-SHOT-RESULTS (what, lifecycle), §7.1's shot row. Register: DEC-101…DEC-104. | Mark (DEC-101…DEC-104); RAT-32 awaits Mark |
| 1.5 | 14 Nov 2026 | **Ratified** (DEC-105…DEC-120): RAT-01, 02, 03, 04, 16, 17, 18, 20, 21, 23, 24, 25, 26, 27, 28, 31; their marks replaced by DEC ids. RAT-02 applied: C5's clause in both copies of §1.2 and the C5 example row; the "proposed amendment" paragraph removed. RAT-11 rejected: OBJ-REPLAY-STATE's members left to step 4. **RAT-32 answered by Mark** (DEC-121), replacing the lead's reading: §2.4 M-SHOT-LAYERS added; §2.1's cross-shot row; OBJ-SHOT-RESULTS is the findings only (what, lifecycle); OBJ-SHOT-DERIVED added (what, lifecycle); OBJ-SHOT-DIAG views both; §7.1 shot row; §11.1 rows for derived data and the flight check; §11.2 `ShotFactors`/`ShotOutcome` → OBJ-SHOT-DERIVED; Q-24 closed by Mark. **The flight check** is shot derived data (DEC-122, superseding DEC-06): R-DER-5, GL-32. **Glossary**: GL-22 reworded; GL-33 "the shot's tendencies"; GL-63 shot derived data, GL-64 Tendencies (athlete), GL-65 Assessment. **Legend**: "no mark" = ratified at v1.5 (DEC-85); the proposed row notes that only RAT-29 stands; §1.3's `M-` prefix. **Register**: DEC-105…DEC-122; supersession notes on DEC-06 and DEC-85. **Open**: Q-25 (where the derived data persists), Q-26 ("assessment" used twice). | Mark (DEC-105…DEC-122) |
| 1.6 | 14 Nov 2026 | RAT-29 answered (DEC-123): §11.2's `WristDiagnosticsModel` retires (its wrist-pattern result becomes part of the shot's findings), a new §11.1 row; `SessionReportListModel` and `RecordedClub`/`ClubGeometry` ratified. Q-25 closed (DEC-124): FILE-SHOT-13 holds the derived data and the findings; M-SHOT-LAYERS layer 3, OBJ-SHOT-DERIVED's lifecycle and §7.1's "cached in" updated. Q-26 closed (DEC-125): the context diagram and §2.1's cross-shot row say "diagnosis"; GL-65 updated. Legend: "no mark" = ratified at v1.6; no proposal stands. Register: DEC-123…DEC-125. | Mark (DEC-123…DEC-125) |
| 1.7 | 15 Nov 2026 | Final re-audit (nothing new decided). **State line** added; legend (proposed = MD-12; no mark excludes proposed and OPEN); §1.3 `MD-`, `TERM-`, `LIVE-`, `NAME-`, `NOTDOM-` prefixes; ids on TERM-1/2, NAME-1/2, LIVE-1…3, NOTDOM-1…5. **Fixed from existing decisions**: §4.1 after `d07ccebb` (`recording_integrity`, `swing_analysis_rebase`, `pp_gpu_metrics_mac`); §2.2 and §11.1 step-1.5 rows done, "Current at" `d07ccebb`; EVT range 01…23 (§11.1, 03, 04); OBJ-SESSION/OBJ-ATHLETE emit EVT-17 (DEC-78); OBJ-SHOT-DERIVED reach row and §5.1 tree line (DEC-121); OBJ-SHOT-DIAG created (DEC-100, DEC-121; T12); OBJ-ANALYSIS-CONTEXT's later writer `Shot::completeCapture` (DEC-103); OBJ-MARKUP-TRUTH added (DEC-83); P3 points to R-REC-1's writers; M-SHOT-LAYERS layer 1 files (FILE-SHOT-05 is derived, DEC-116) and the unlayered caches; EVT-05/06 "findings" for "results"; EVT-12 chain recomputes derived data first (P5, DEC-122); §9.2 worked example (DEC-121, DEC-124); FILE-SHOT-08's Step cell; SET-02…08 blank cells; FILE-SHOT-11 and FILE-SESS-11 placement notes; DEC-126…DEC-128 registered and cited (CLS-PACKS-NORMS standing, R-ANA-7 reuse gate, the triage's failed write). **Marked proposed (MD-12)**: T1 (§2.3, CLS-RIG), T2 (R-REC-2 b), T3 (OBJ-SHOT-DERIVED trigger), T4 (layer 4, GL-22, GL-65 "the DAG"), T5 (OBJ-REPORT ends), T6 (SET-05 kind), T7 (SET-06 kind), T8 (SET-15 kind, its place in the stamps: CLS-PACKS-NORMS, R-STAMP-1), T9 (`saveReport`), T10 ("a held lock … never only logged"), and new rows marked the same way: SET-09's four rig keys, SET-18, SET-19, EXT-10, EXT-11. **Marked OPEN (MD-01…MD-10)**: MD-01 (layer 3, OBJ-SHOT-DERIVED, CLS-SESSION-DIAG, §7.1 session row, EVT-05, GL-62, §9.2, DEC-100), MD-02 (EVT-06, §9.2), MD-03 (layer 4, GL-22, GL-65, DEC-121), MD-04 (P1, R-REC-2 a, OBJ-ANALYSIS-CONTEXT, R-CLUB-1), MD-05 (LC-SESSION, CLS-ATHLETE, CLS-SESSION), MD-06 (§7.1 ledger, EVT-18, FILE-SHOT-10, GL-26), MD-07 (OBJ-SESSION-INDEX), MD-08 (§5.4 housekeeping, OBJ-RECORDING, SET-14), MD-09 (GL-37), MD-10 (§1.2). **§14**: MD-01…MD-12 with recommendations; Q-27 (the shot-creation protocol) and Q-28 (the empty-session discard's command), which no MD covers; Q-17 extended (how a `Shot` gets its session's snapshot). | Nothing approved; Mark decides MD-01…MD-12 before step 2's brief |
| 1.7 | 15 Nov 2026 | Before commit: the legend says that v1.7 sentences are written from the decision each cites, with its DEC id shown, not "ratified at 1.6"; MD-11 cites DEC-129, newly registered (Mark kept `CLAUDE.md` local on 14 Nov 2026); the 22 "O12" marks are relabelled "MD-12"; every citation of a file outside version control is removed, since §14 is the reference's own list. | Mark (DEC-129) |
| 1.7.1 | 14 Nov 2026 | Status edits only (§1.2's §11 exclusion; RAT-01), step 1.7: §4.1 RING-RULES gains `reuse_gate` (placed in the lint's ring table by step 1.6, 363925e6); §11 "Current at" `363925e6`; §11.1 rows: the Recording contract (the correction in re-analysis done), the analysis context recorded (follow-club's rewrite; the backfill tool, dry run only), the input stamp (done), one time base (done: the posture and delivery pictures, the markup pose overlay, `swinglab_run` and the two Python tools on the one rule), one serialiser per analysis block (done: the one projection, 77ff74a7), the club rule until step 6, R-ANA-2, pose reuse (done), follow-club (DEC-82), the swing-file writers (the backfill's door). No design text changed. | n/a: status edits (§1.2) |
| 1.7.2 | 14 Nov 2026 | Status edits only (§1.2's §11 exclusion; RAT-01), step 1.8: §11.1's shot-kind row (R-REC-8; DEC-44, DEC-87): partly, the field written by the one header writer where no later write can make it false (a capture with streams, a standalone device-only shot), carried by the Recording, every other document classified by `readShotKind()`. No design text changed. | n/a: status edits (§1.2) |
| 1.8 | 15 Nov 2026 | **Mark's answers of 15 Nov 2026 applied** (DEC-130…DEC-160). **MD-01** (DEC-130): the usuals a session aggregate from readings, not part of the diagnosis: §2.1 cross-shot row, M-SHOT-LAYERS layer 3, §5.1 tree, OBJ-SESSION-DIAG, CLS-SESSION-DIAG, §7.1 session row, GL-25, GL-62, the chains of EVT-01, 03, 04 (both clubs: J-2, proposed), 05, 22, EVT-02's chain (T3; its usuals step: J-3, proposed), DEC-100's note. **MD-02** (DEC-131): EVT-06 re-fires; R-DER-2; §9.2. **MD-03** (DEC-132): "the analysis graph" and "the diagnostic graph" for bare "DAG": the context diagram and table, §2.1, R-REC-8, M-SHOT-LAYERS (a note after Mark's quote, which is kept), layer 4, OBJ-SHOT-RESULTS, §7.1, GL-22, GL-65, DEC-121's note; GL-66, GL-67 added. **MD-04** (DEC-133): the captured club a source fact, the device club beside it: P1, R-REC-2 (a), OBJ-ANALYSIS-CONTEXT, CLS-RECORDING, §5.8 (a fifth term, the device club), R-CLUB-1, R-CLUB-3, the reach row of OBJ-ANALYSIS, OBJ-MEASUREMENT, EVT-22, GL-13, §11.1's follow-club row, DEC-82's note. **MD-05** (DEC-134): `Athlete::resumeSession`, today's session of the same type only: ID-SESSION, OBJ-SESSION, OBJ-SESSION-STATUS, the reach rows of OBJ-ATHLETE and OBJ-SESSION, LC-SESSION, CLS-ATHLETE, CLS-SESSION, EVT-23, §11.1, DEC-77's and DEC-113's notes. **MD-06** (DEC-135, superseding DEC-102 and moving DEC-124): the `Session` keeps each shot's findings and derived data in FILE-SESS-03; FILE-SHOT-13 retired (its row kept, marked retired): §2.1, M-SHOT-LAYERS layers 3 and 5 and the note below it, P10 (the collection; its Forbids), §5.1 tree, OBJ-SHOT-RESULTS and OBJ-SHOT-DERIVED (what and lifecycle), OBJ-SHOT-DIAG, OBJ-SESSION-DIAG, CLS-SESSION, CLS-SHOT, CLS-SESSION-DIAG, §7.1 shot and session rows, EVT-18, FILE-SHOT-10, FILE-SHOT-13, FILE-SESS-03, §9.2, GL-23, GL-26, §11.1's findings and derived-data rows, the notes on DEC-100, DEC-102, DEC-113, DEC-124, Q-23, Q-25. **MD-07** (DEC-136): OBJ-SESSION-INDEX, §5.1 tree. **MD-08** (DEC-137): §5.4 housekeeping, OBJ-RECORDING, SET-14, §11.1; the OS trash, which it leaves unanswered, is Q-29 (added). **MD-09** (DEC-138): GL-37 and GL-68…GL-70; OBJ-SESSION-INTENT, OBJ-SESSION-SUMMARY, CLS-ATHLETE-DIAG, §7.1 athlete row, FILE-SESS-02, R-INPUT-3, §5.1 tree. **MD-10** (DEC-139): C11 and the glossary exclusion, in both copies of §1.2 (the definition now v1.1); §1.2's note; §1.3's C-range and `MD-nn` row; §10's template line. **MD-11**: closed with DEC-129. **MD-12** (DEC-140): every "(proposed, for Mark; MD-12)" mark replaced: T1 (§2.3, CLS-RIG), T2 (R-REC-2 b), T3 (OBJ-SHOT-DERIVED trigger: analysis lands, its club's usuals change, or its label is corrected), T4 (layer 4, GL-22, GL-65), T5 (OBJ-REPORT, with FILE-SESS-07's coach note and send log as source facts kept across remakes; FILE-SESS-08), T7 and T8 reworded (SET-06, SET-15, CLS-PACKS-NORMS, R-STAMP-1: the stamp records what was in force when the findings fired), T9 (`saveReport`), T10 (CLS-REPOSITORIES), SET-09's four rig keys, SET-18, SET-19, EXT-10, EXT-11; T6 (SET-05) reversed by DEC-153; the legend's proposed row. **Q-17** (DEC-141): CLS-SHOT; Q-17 closed with step 3's worker design point left to its brief; OBJ-MARKUP-TRUTH's pointer. **Q-27** (DEC-142): LC-SHOT, CLS-SESSION, ID-SWING-NUMBER, R-REC-2, EVT-01. **Q-28** (DEC-143): LC-SESSION, CLS-SESSION, EVT-09. **Step 1's items**: S1-a and S1-b, R-ANA-7 (DEC-144, DEC-145); S1-g, §1.4 A1 and a §11.1 row (DEC-146); S1-k, R-REC-8, FILE-SHOT-01, §11.1's shot-kind row (DEC-147). **Step 2's brief**: D1 `src/Identity` (RING-VOCABULARY, §5.2, §11.1; DEC-148); D2 `session.json` (FILE-SESS-01, §11.1; DEC-149); D3 the old sessions' v5 ids (ID-SESSION; DEC-150); D4 the athlete's folder where `athlete.json` is found (§5.1 tree, CLS-ATHLETE, FILE-ATH-01, §8.4's note, DEC-04's note; DEC-151); D5 the Wrist reference on the record (OBJ-ATHLETE-RECORD, SET-17, §11.1; DEC-152); D6 SET-05 learned rig state, owner Studio/Rig (DEC-153); D7 `Session::relocate` and EVT-11 removed (ID-SESSION, the reach rows of OBJ-SESSION and OBJ-LIBRARY, CLS-SESSION, CLS-REPOSITORIES, §7.4's intro, EVT-11 kept as a removed row, DEC-22's and DEC-94's notes; DEC-154); D8 R-PERS-2's exception (DEC-155); D9 FILE-SESS-07, 08 (DEC-156); D10 one allocator (ID-SWING-NUMBER, a §11.1 row; DEC-157); D11 R-PERS-6 (DEC-158); D12 R-INPUT-2 (DEC-159); IM-S2-1…7 (DEC-160): FILE-SESS-01's and -02's keys, FILE-SHOT-02's keys, §8's interim writers, CLS-ATHLETE's step-2 form, OBJ-SESSION-INDEX's step-2 fields, A4's extension (§1.4), RING-PLATFORM and R-PERS-2 (`atomic_write`, `folder_lock`), EXT-12 (the lock files). **§14**: MD-01…MD-12, Q-03, Q-04, Q-08, Q-17, Q-21, Q-27, Q-28 closed with their DEC ids; Q-29 added. **Register**: DEC-130…DEC-160; notes on DEC-04, DEC-22, DEC-77, DEC-82, DEC-93, DEC-94, DEC-100, DEC-102, DEC-113, DEC-121, DEC-124. **Header**: state line, version, legend. **Left proposed**: three marks: §11.1's A1 row (which step widens A1: DEC-146 names none; J-1), EVT-02's usuals step (J-3) and EVT-04's two clubs (J-2). **Round 2** (the review): those two marks; SET-05's target cell cites DEC-17 and drops a clause no decision holds; "may compute and hold" restored (DEC-130); the usuals' definition kept in GL-25 and pointed to elsewhere. | Approved: Mark (DEC-130…DEC-160) |
| 1.8.1 | 15 Nov 2026 | Step 2.1. **Status edits** (§1.2's §11 exclusion; RAT-01): §11.1's identity-types row (partly: the types and `SessionIdentity` exist, nothing keys on them yet) and "Current at". **§5.2** (not §11, so not covered by that exclusion): "today" pointers in ID-SESSION (where the v5 id is computed; its exact recipe marked J-12, proposed, for Mark) and ID-SHOT (where the type lives). | §11: n/a, status edits (§1.2); §5.2's recipe: proposed, for Mark (J-12) |
| 1.8.2 | 15 Nov 2026 | Step 2.2. **Status edits only** (§1.2's §11 exclusion; RAT-01): §11.1 rows "Session resume keeps the `SessionId`" (partly), "The session file `session.json`" (partly), "One swing-number allocator" (done), "`AthleteRepository`, `SessionRepository`" (partly), "Advisory lock files" (the store takes the lock); §11 "Current at". | n/a, status edits (§1.2) |
| 1.8.3 | 15 Nov 2026 | Step 2.3. **Status edits only** (§1.2's §11 exclusion; RAT-01): §11.1 rows "`Athlete::delete` trashes the whole folder" (done), "The wrist reference shot on the athlete record" (partly), "`AthleteRepository`, `SessionRepository`" (partly), "`Athlete` object" (partly), "`athlete.json`" (partly), "Advisory lock files" (the athlete store takes the lock), "The tuning log" (partly); §11 "Current at"; the "Writer today" cells of FILE-ATH-01, FILE-ATH-04 (§8.1), SET-01, SET-17 (§8.2). §5.3 has no "today" column: unchanged. No design text changed: the `Athlete`'s step-2 form (IM-S2-5), its interim writer (IM-S2-4), A4's owner of `athlete.json` (IM-S2-6), D4–D6 (DEC-151…DEC-153) are as approved. | n/a, status edits (§1.2) |
