# PinPoint Studio information model

**Status: target.** This page describes the information model the code is moving towards, step by step, in the
refactor (`docs/implementation/swe-audit/04-remediation-plan.md`). Much of it does not exist yet. Before relying on
any object or rule below, check [§11 Where the code is today](#11-where-the-code-is-today).

It restates `docs/implementation/swe-audit/03-target-architecture.md` (draft 2.3; decisions 1–30). Who decided what:

| Mark | Meaning |
|---|---|
| (Mark, 14 Nov 2026) | Mark's decision |
| *(clarification, for Mark to read)* | The lead's clarification of the agreed design, consistent with P1–P9. Not yet read by Mark. |
| *to decide (Mark, step N)* | Open. Described as it is today; the named step's brief brings it to Mark. |
| no mark | The agreed design (03, decisions 1–30) |

Each refactor step's brief may refine an element; when it does, this page changes in the same commit.

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

---

## 1. Purpose and how to use it

This page is the source of truth for **what holds data in PinPoint Studio, who owns it, and where it lives**. It
plays the role for data that `docs/design/system/README.md` plays for the UI.

| When | What to do |
|---|---|
| Before writing any plan or brief that touches data | Read this page. Copy the checklist in [§10](#10-the-ownership-questions) into the plan's "Ownership" section and answer it for every new or changed thing. |
| Before changing code that holds, derives or stores data | Find the object in [§5](#5-domain-objects)–[§8](#8-persistence) and its status in [§11](#11-where-the-code-is-today). |
| When reviewing a diff | Answer the six ownership questions for the diff. A "no" is CHANGES NEEDED. |
| When `architecture_lint_test` fails | Fix the code. Never add a line to its allowlist to make it pass; an allowlist entry needs Mark's approval, and each entry names the refactor step that removes it. |

**HARD: any change to the information model needs Mark's approval before it is built.** Propose the need, what it
affects and the answers to the ownership questions, then wait. That covers:

| Change | Example |
|---|---|
| A new first-class object or owner | a new diagnostics owner; a new aggregate |
| A new persisted file, or a new field family in one | a new `<session>/*.json`; a new block in the shot's `review` |
| A new identity key | keying anything by something other than `AthleteId`, `SessionId` or `ShotRef` |
| A new dependency between levels | the athlete level reading shot rows; a shot reading another session |
| A new event | anything beyond the list in [§7](#events-and-what-they-recompute) |
| Anything created by a view | a QML file instantiating a C++ type that holds session or athlete data |

---

## 2. The three contexts and the Recording contract

PinPoint is three contexts joined by two contracts. Dependencies run left to right; context flows back from Domain
to Analysis as typed input.

```
 ┌──────────────────────┐   Recording    ┌───────────────────────────────┐   results    ┌──────────────────────────────┐
 │ ACQUISITION          │ ─────────────▶ │ ANALYSIS                      │ ───────────▶ │ DOMAIN + APPLICATION         │
 │ transports, devices, │                │ per-shot DAG: inference ·     │  (shot       │ Athlete · Session · Shot ·   │
 │ ring buffer,         │                │  fusion/synthesis · segmenta- │   analysis,  │ Studio/Rig · persistence ·   │
 │ connectors,          │                │  tion · metric + joint extrac-│   shot       │ AppState · presentation      │
 │ calibration          │                │  tion · shot grading          │   grading)   │                              │
 │ procedures           │                │ cross-shot reductions:        │ ◀─────────── │ owns and stores everything;  │
 │                      │                │  session and athlete diag-    │   context    │ hands Analysis its context   │
 │                      │                │  nostics, assessment, coaching│  (baselines, │ (athlete facts, rig state,   │
 └──────────────────────┘                └───────────────────────────────┘   rig, usuals)│  session usuals, norms)      │
                                                                                         └──────────────────────────────┘
```

| Context | Holds | Owns |
|---|---|---|
| **Acquisition** | Transports, devices, the ring buffer, connectors, calibration *procedures*, threading inside capture. Its internals are designed in the platform refactor (capture), which follows this one; only its contract is fixed here. | Nothing persistent: it produces Recordings, which the domain owns and stores |
| **Analysis** | The per-shot DAG, the grading rule and the cross-shot reduction rules | Nothing persistent: its results are owned and stored by the domain |
| **Domain + Application** | Athlete, Session, Shot, Studio/Rig, persistence, `AppState`, presentation | Everything that is stored |

**Contexts and rings are different cuts** *(clarification, for Mark to read)*. A context describes how data flows; a
ring ([§4](#4-the-rings-and-the-directory-mapping)) describes which code may depend on which. So one context spans
several rings:

| Example | Context | Ring |
|---|---|---|
| A reduction *rule* (tendencies, the flight check) | Analysis | Rules |
| The grading rule, `gradeShot()` | Analysis | Rules |
| The *owner* that runs them and keeps their results (`SessionDiagnostics`, `AthleteDiagnostics`) | Domain + Application | Domain model |
| The analysis pipeline that runs the per-shot DAG | Analysis | Application |

### The shapes of Analysis

**Grading** is the name in the target for checking each of a shot's numbers against its corridor. The code calls it
`detectShot` (a member of `SessionDiagnosticsModel`); step 3 makes it a pure function and renames it `gradeShot`.

| Shape | What it is | Run by |
|---|---|---|
| **Per-shot DAG** | A pure function: Recording + effective context → the shot's analysis (the recorded product). A dependency graph of versioned stages, not a stack (`analysis_dag_design.md`). | The analysis pipeline (an application service). **It never grades.** (Mark, 14 Nov 2026) |
| **Grading** | A pure function, `gradeShot()` in `src/Diagnostics`: each of the shot's numbers against its corridor, with the session's club baseline and the effective club → the shot's rows. | `SessionDiagnostics` grades every shot of its session (Mark, 14 Nov 2026) |
| **Cross-shot reductions** | Session and athlete diagnostics, assessment, coaching, and the shot facts that take the session's usuals as input (the flight-check overlay, outcome, the three factors, carry against the usual) ([§7](#7-derived-facts)). Incremental and stateful over many shots, recomputed by events. | `SessionDiagnostics`, `AthleteDiagnostics` |

### The Recording contract (Mark, 14 Nov 2026)

A **Recording** is everything Analysis is allowed to know about a captured swing, and nothing else.

| Part | Contents |
|---|---|
| Streams | Video frames, IMU and HackMotion samples, the launch monitor's readings as delivered |
| Timestamps | On one stated clock model: camera timestamps mark exposure end; Spinnaker frames are host-stamped (about ±0.4 ms jitter) |
| Devices | Device identity and configuration |
| Rig | A reference to the Studio/Rig state it was captured with (cameras, positions, calibrations) |
| Integrity | Capture holes, lost frames |
| Analysis context | The athlete facts the analysis consumes (handedness, height, cohort, the club and its length), **recorded at capture**, so re-analysis never reads a live profile. It is the only source of athlete facts for analysis *(clarification, for Mark to read)*. |
| Analysis context on disk | One schema-tagged `analysisContext` block in the swing file, written once by capture through the one header writer: `athlete.handedness`, `athlete.heightM` (0 = unknown), `athlete.clubSet` (men / women / junior: the cohort analysis consumes), the captured club (`club.name`, empty = undeclared, never the stub) with its geometry, length prior and standard length, and `bag`, a snapshot of the bag's geometry so a corrected club is resolvable without a live profile. `capture.club` stays, written from the same value, as provenance (IM-1; Mark, 14 Nov 2026). The norm cohort (sex, age band) is **not** recorded in step 1; whether grading reads it from the Recording is step 3's (IM-7; Mark, 14 Nov 2026). |

| Rule | Forbids |
|---|---|
| A shot's swing file is written **once by capture** (the Recording), **then only by its `Session`**. That is how P3 holds for the Recording (Mark, 14 Nov 2026). | Any other writer of a swing file |
| **Capture writes in stages**: the shot, then its late parts (a late LM reading, a phone clip's stream origin) through `RecordingRepository::completeCapture`. Follow-club may rewrite the captured club, because the device named it (IM-4; Mark, 14 Nov 2026). | A late write that is not a capture fact |
| **Until step 3** the later writers are today's callers (`ShotListModel`, `ReanalysisController`, the LM controller, the archiver, the tools), all through `RecordingRepository`; the `Session` takes them over in step 3 (IM-5; Mark, 14 Nov 2026). | Writing the swing file past the repository |
| **Analysis only ever runs on a Recording**, whether it arrived a second ago or a year ago. Live analysis and re-analysis are the same function: **same Recording + same effective context (the effective club included) → same job.** | Building an analysis job from live objects; Analysis code reaching into device classes, the ring buffer, live controllers, the athlete controller or settings (lint rule A8) |
| **One builder** makes the analysis job from the Recording and its effective context (the recorded analysis context, with the golfer's club correction applied), for live capture and re-analysis alike. | A second job builder; any context field set on one path only |
| **Re-analysis uses the effective club** (Mark, 14 Nov 2026): when the golfer has corrected the club, the builder puts the corrected club, its length and its norms into the effective context, because it is what was really hit. A corrected shot re-analyses with its corrected club. The captured club stays in the Recording for provenance. | Re-analysing a corrected shot with the club it was captured under; Analysis reading the shot's `review` block itself |
| **The correction, until step 6, is the LM board's override** (`review.clubOverride`); the swing-edit popover's `review.club` is the declared club. A corrected club takes its geometry from the bag snapshot (when present) and its standard length from the club set and height; it has no length prior (IM-3; Mark, 14 Nov 2026). Without a correction the geometry and prior stay the captured club's, and the declared club, when set, gives the standard length (Mark, 14 Nov 2026: the popover's club keeps setting the standard length, as today); capture's seed in `review.club` counts as declared, as today, until step 6. | Treating the popover's club, or the board's evidence suggestion, as the correction |

**The name.** The contract's types are vocabulary shared by both sides; its store is an adapter. Both live in a
directory named for what they are (e.g. `src/Recording`), not `src/Export`.

**In the code** (status: partly — [§11](#11-where-the-code-is-today)):

| Element | Where | Ring |
|---|---|---|
| `Recording` (window and `t0Us`, sources by role with the one perspective mapping, capture facts, ball inputs, IMU bindings, integrity inputs; every time window-relative) | `src/Recording/recording.h` | Vocabulary |
| `AnalysisContext` (handedness, height, club set, the captured club with its geometry, length prior and standard length, the bag snapshot) | `src/Recording/analysis_context.h` | Vocabulary |
| `effectiveContext()` v1, the club rule above; its inputs are the recorded context and the shot's two club edits (`ClubReview`: the board's override, the popover's club), which the repository's `load` returns beside the Recording | `src/Analysis/effective_context.*` | Rules |
| `buildAnalysisJob()`, the one builder; `ShotAnalysisJob` | `src/Analysis/analysis_job_builder.*`, `shot_analysis_job.h` | Rules |

Nothing writes or reads the `analysisContext` block yet, and the app still builds its jobs the old two ways; the
store side and the callers follow in step 1's later items.

### Studio/Rig (Mark, 14 Nov 2026)

**Named; designed in the platform refactor (capture).**

| Aspect | Target |
|---|---|
| What it is | A domain entity owning the rig: which cameras and sensors, where they are, their intrinsic and extrinsic calibration, versioned over time |
| Not owned by | A device, or a shot |
| Relation to shots | Each Recording references the rig state it was captured with |
| Calibration | The *procedure* belongs to Acquisition; its *result* is a source fact owned by Studio/Rig |
| Identity, persistence, writer, `RigChanged` | Named; designed in the platform refactor (capture). Until then a Recording keeps each stream's setup (perspective, mirrored, fixed in place) and a slot for the rig reference |

---

## 3. The principles

| # | Principle | Forbids |
|---|---|---|
| P1 | **Source facts, recorded inputs and derived facts are different kinds of thing** (recorded input: Mark, 14 Nov 2026). A **source fact** is something only the world or the golfer can tell us (every instrument's readings and streams, the athlete record, what the golfer said); it is stored once, by its owner, and never regenerated. A **recorded input** is context written once at first need so a result is reproducible (the Recording's analysis context; a session's club baseline); it is never regenerated and never deleted with the caches, and it is rewritten only when its format is older than the current rule. A **derived fact** is a pure function of source facts, recorded inputs and versioned rules; it may be cached, deleted and rebuilt at any time. | Golfer input or a recorded input stored in, or deleted with, a regenerable cache; a cache holding the only copy of anything; a higher level's verdict written into a lower level's rows |
| P2 | **Domain, application state and view state are three layers**, each referring only downwards. Domain is what is true without a screen (the same in a GOLFSIMPC sweep); application state is what the app is doing now; view state is one view's scroll, tab or expanded row. | Diagnostics owned by a view; the selection held in several places; domain rules in QML |
| P3 | **Aggregates have one root, and the root is the only writer.** `Athlete` and `Session` are roots; every change goes through the root as a command. Every file is written only through its root (Mark, 14 Nov 2026). | Two objects writing one file; a service writing a session or athlete file except through the root's command; changing the athlete under a running session; a view mutating rows |
| P4 | **Persistence lives behind repositories.** The domain model defines the repository interfaces (`RecordingRepository`, `SessionRepository`, `AthleteRepository`); adapters implement them *(clarification, for Mark to read)*. One repository per file family, one format definition, schema checked on read. | File I/O in domain objects or in Coaching/Diagnostics rules; a file's envelope parsed in more than one place; a file name spelled outside its repository |
| P5 | **Derivation is an explicit dependency graph, updated by events.** Each derived fact declares its inputs and rule version; only what depends on a change is recomputed; a rule's version is part of its result's stamp. | `mutable` dirty flags that rebuild everything; stamps that omit inputs; staleness found by accident |
| P6 | **One rule, one result, one owner, at the right level.** A level reads the level below as results. Context flowing down (session usuals into a shot; the athlete baseline into a session) is a typed input. Nothing recomputes another level's facts. | Recomputing another level's facts; two rules for one fact (two direction classifiers); a gloss applied per surface |
| P7 | **Identities are types, not strings.** `AthleteId`, `SessionId`, `ShotRef` are value types; folders, display numbers and labels are properties. | Keying by name or folder; parsing a folder name outside `SessionIdentity` / `SwingPaths` (lint A3); "Shot N" meaning anything but the swing number |
| P8 | **Work runs on immutable snapshots; results come back to the owner.** Domain objects live on one owner thread: the GUI thread in the app, the main thread in a tool *(clarification, for Mark to read)*. Workers read immutable inputs and post results to the owning root. | A worker reading a live object; a lazy singleton first built on a worker; a new threading idiom |
| P9 | **The QML boundary is typed.** QML sees `Q_GADGET` values and `QAbstractListModel`s with declared roles. | A new `QVariantMap`/`QVariantList` in a presenter's or domain type's public API (lint A6); a domain or session-scoped object created in QML (lint A2) |

**Two terms side by side:**

| Term | Is | Example |
|---|---|---|
| Recorded input | Context frozen once, so a result can be reproduced | the Recording's analysis context; `club_baseline.json` |
| Recorded product | A derived fact that is expensive, so it is kept with a stamp and refreshed when stale | a swing's analysis |

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
 │  │  │ DOMAIN MODEL  Athlete · Session · Shot · measurements · diagnostics owners │  │  │
 │  │  │               · repository interfaces                                      │  │  │
 │  │  │  ┌────────────────────────────────────────────────────────────────────┐  │  │  │
 │  │  │  │ RULES (pure)  measurement algorithms (raw device data → measurements,│  │  │  │
 │  │  │  │   incl. the LM's inferred reads) · instrument precedence · grading ·  │  │  │  │
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

| Ring | Holds | Never |
|---|---|---|
| **Platform** (ring 0; Mark, 14 Nov 2026) | Logging (`ppInfo`/`ppWarn`), profiling, machine queries. Every ring may include it. | Depends on anything outside itself; holds anything domain-shaped |
| **Vocabulary** | What things are: value types (`Club`, `Intent`, `Measurement`, …), identity types, metric definitions and the catalogue, club vocabulary, units, the Recording contract's types | Depends on anything but Platform |
| **Rules** | Pure functions: measurement algorithms (pose, club, ball, IMU fusion, kinematics, wrist, 3-D skeleton, the LM's inferred reads); the one instrument-precedence rule; grading; coaching (outcomes, factors, flight check, Sky, tendencies, link grading) | File I/O (lint A5); golfer-facing sentences; knowing about sessions or screens |
| **Domain model** | Entities and aggregates; the owners of derived facts; the repository interfaces (`RecordingRepository`, `SessionRepository`, `AthleteRepository`). Qt objects for signals, no QML | Persistence, device or UI code |
| **Application** | `AppState`; use cases (the commands that change the domain); services that run work: capture, the analysis pipeline, re-analysis, the athlete catch-up, the archive and the report | Holding domain data of its own; writing a file except through a root's command |
| **Adapters** | Devices (a launch monitor is a device like any other; it simply needs no algorithm); persistence (`SwingStore`, the Recording store, implements `RecordingRepository`; the session and athlete stores implement theirs); UI (presenters, the words module, QML) | Being depended on by an inner ring |

### Directories, mapped

This is the ring table `src/Gui/tests/architecture_lint_test.cpp` enforces: a directory's ring, with named files
placed elsewhere where a directory spans rings.

| Ring | Directories (and files placed against their directory) |
|---|---|
| Platform | the logging, profiling and machine-query files of `Core` (`pp_debug`, `pp_log_stream`, `PpMessageLog`, `pp_profiler`, `cpu_topology`, `pp_gpu_metrics`, …) |
| Vocabulary | `src/Recording`, the Recording contract's types, created in step 1 (IM-6; Mark, 14 Nov 2026); `Metrics`; the rest of `Core`; the shared types of `Analysis` (`swing_analysis`, `gate_record`, `analysis_versions`, …); the stream types of `Buffer` (`types`, `swing_window`, `imu_sample`, …); the reading types of `LaunchMonitor` (`launch_monitor_reading`, `lm_device`); `Export/frame_decode` |
| Rules | `Pose`, `Club`, `Ball`, `Body`, `IMU`, `Kinematics`, `Wrist`, `Skeleton3d`, `Timeline`, `Diagnostics`, `Coaching`; the inferred reads of `LaunchMonitor` (`lm_inferred_reads`, `lm_flight_path`, `shot_pairing`, …); `Core/kf3_smoother` |
| Domain model | `src/DomainModel` (Mark, 14 Nov 2026), created in step 1 for `RecordingRepository` (IM-6; Mark, 14 Nov 2026) |
| Application | `Analysis` (the pipeline, re-analysis); `src/App` for `AppState` (Mark, 14 Nov 2026) |
| Adapters | `Gui` (presenters, QML, and today's app services); `Export` (the Recording store); `Buffer` (the ring itself); `Video`, `Audio`, `LaunchMonitor` (connectors), `Ppcp`, `LLM`, `STT`, `TTS`, `Secrets`, `Update`; `Core/device_enumerator`, `Core/pp_settings`, `Core/studio_profile`, `Core/app_info` |
| No ring (not C++) | `Resources` (shipped data: core pack, norms, flight norms, icons, fonts, keys); `Shaders` |

**Directories that span rings**, to be split:

| Directory | Parts |
|---|---|
| `Export` | The Recording contract's types (vocabulary) and its store (adapter) |
| `Analysis` | Shared types (vocabulary), algorithm stages (rules) and orchestration (application) |
| `Core` | Platform, vocabulary, and device, settings and studio-profile infrastructure (adapters) |
| `LaunchMonitor` | Reading types (vocabulary), connectors (adapters) and inferred reads (rules) |
| `Buffer` | Stream types (vocabulary) and the ring buffer (adapter) |

**Names.** "Focus" means two unrelated things and must not be reused for either:

| Today's name | Kind | Target name |
|---|---|---|
| `focusedShotId` | Application state: what is selected | `selection` |
| `focusConditionId` | Domain source fact: what the golfer declared they are working on | part of `sessionIntent` |

---

## 5. Domain objects

Every first-class domain object is C++; QML holds no domain objects or rules (Mark, 14 Nov 2026).

```
Athlete  (aggregate root · AthleteId)
 │  record: name, handedness, height, sex/dob, bag            ← source fact, athlete.json
 │  libraryFolder (stored on the record)
 │  sessions: SessionIndex (SessionId → summary)             ← index only; sessions load on demand
 └─ AthleteDiagnostics      derived, from SESSION summaries

Session  (aggregate root · SessionId)
 │  identity: SessionIdentity (folder, start time, local day, n-of-day, type)
 │  status: InProgress | Ended
 │  clubBaseline: the athlete's club baselines, written once at first need   ← recorded input, club_baseline.json
 │  sessionIntent: declared focus / work-on, declared miss, screen answers, intent runs   ← source fact
 ├─ shots: Shot*  (ShotRef)
 │    recording handle · analysis (recorded product)
 │    measurements: one set, each tagged with its source and its kind (reading | derived)
 │    annotations · club correction · shotIntent · rating/note · label corrections   ← source facts
 │    effective club                                                                  ← derived
 │    diagnostics: ShotDiagnostics (read-only view of this shot's graded rows and results)
 ├─ SessionDiagnostics      grades every shot; keeps every shot's rows and the session's results
 └─ SessionSeries           derived, in memory only
```

### The objects

| Object | Kind | Owner (root), level | Identity | Created | Destroyed | Persisted in | One writer |
|---|---|---|---|---|---|---|---|
| `Athlete` | aggregate root | itself; athlete | `AthleteId` (uuid) | loaded by `AthleteRepository` when first asked for (the current athlete, or a headless service); one instance per `AthleteId` *(clarification, for Mark to read)* | when its last holder releases it | `<athlete>/athlete.json` | `Athlete` via `AthleteRepository` |
| Athlete record (name, handedness, height, sex/dob, bag, library folder) | source | `Athlete`; athlete | the athlete's | with the athlete | — | `athlete.json` (settings keep only "current athlete") | `Athlete` |
| `SessionIndex` | index of sessions (`SessionId` → summary) | `Athlete`; athlete | — | with the athlete | with the athlete | built from each session's summary, read through `SessionRepository` *(clarification, for Mark to read)* | — |
| `AthleteDiagnostics` | derived, on demand | `Athlete`; athlete | the athlete's | with the athlete | with the athlete | `<athlete>/tendencies.json`, `swing_themes.json` (caches) | `AthleteDiagnostics` via `AthleteRepository` |
| `Session` | aggregate root | itself; session | `SessionId` | loaded by `SessionRepository` when first asked for; Start creates a new one; one instance per `SessionId` | when its last holder releases it (ending changes its status, not its life) | its session folder | `Session` via `SessionRepository` |
| `SessionIdentity` (folder, start time, local day, n-of-day, type) | properties of the session | `Session`; session | — | computed once | with the session | — | `SessionIdentity` (no other code parses a folder name) |
| `Session.status` (InProgress / Ended, start and end times) | source attribute | `Session`; session | — | at Start; Ended by command | — | today: not persisted (the running state is in `SessionController`'s memory; the start time comes from the folder name). **Where it is stored: to decide (Mark, step 3)** | `Session` |
| Club baseline (the athlete's club baselines) | recorded input (Mark, 14 Nov 2026) | `Session`; session | — | written once at first need, never at Open | never regenerated, never deleted with the caches; rewritten only when its format is older than the current rule | `<session>/club_baseline.json` | `Session` via `SessionRepository` |
| `sessionIntent` (declared focus/work-on, declared miss, screen answers, "from here on" intent runs) | source | `Session`; session | — | by command | — | `<session>/session_input.json` | `Session` |
| `Shot` | entity | `Session`; shot | `ShotRef` = (`SessionId`, swing number) | at capture (or with its session on load) | with its session | `swing_NNNN/swing.ppsw` | written once by capture, then only by its `Session` |
| Recording (its analysis context a recorded input) | source | `Shot`; shot | the shot's | at capture, in stages: the shot, then its late parts (IM-4; Mark, 14 Nov 2026) | never regenerated | `swing.ppsw` (the context: the `analysisContext` block, IM-1) | capture, through `RecordingRepository` |
| Analysis | recorded product (derived, stamped with its stage versions and its inputs; window time) | `Shot`; shot | the shot's | by the analysis pipeline | refreshed by re-analysis | `swing.ppsw` | the `Session`, from the pipeline's posted result; until step 3, today's callers through `RecordingRepository` (IM-5; Mark, 14 Nov 2026) |
| Measurement `{metric, source instrument + route, kind, value, σ, gates}` | per kind *(clarification, for Mark to read)*: a **reading** (delivered by an instrument: LM, HackMotion) is source; a **derived measurement** (rules over the Recording's streams: camera, IMU, and the LM's inferred reads) is part of the recorded product | `Shot`; shot | — | readings as delivered; derived measurements by the measurement rules | with the shot | `swing.ppsw` | `Session` |
| Annotations (a reading left out; the club it was really hit with) | source | `Shot`; shot | — | by command | — | the shot's `review` block | `Session` |
| Shot intent, rating, note, label corrections | source | `Shot`; shot | — | by command; shot intent is pre-filled from the intent run in force when the shot is taken, and the shot can override it | — | the shot's `review` block (label corrections: Mark, 14 Nov 2026) | `Session` |
| Effective club | derived: the golfer's correction if there is one, else the declared club | `Shot`; shot | `Club` value | — | — | not stored | — |
| Graded rows and the shot's results (flight-check overlay, outcome, the three factors, carry) | derived, **shot-level** facts (Mark, 14 Nov 2026) | kept by `SessionDiagnostics`; shot | the shot's | by `SessionDiagnostics`: rows by grading, the rest by its reduction | with the session | `<session>/diagnostics.json` | `SessionDiagnostics` |
| `ShotDiagnostics` | read-only view of the shot's rows and results, exposed through the `Shot` | — (a view) | the shot's | with the session's results | with the session | — | — |
| `SessionDiagnostics` | derived: grades every shot of its session and keeps the rows; owns the session's results | `Session`; session | the session's | with the session; always runs (every live shot is graded, panel or not) | with the session | `<session>/diagnostics.json`, `<session>/work_ons.json` (`work_ons.json`: *clarification, for Mark to read*) | `SessionDiagnostics` via `SessionRepository` |
| `SessionSeries` | derived: the session's measurements as series, one per session | `Session`; session | the session's | with the session | with the session | in memory only, never persisted | — |
| Packs and norms (the shipped core pack and norms; the user's pack and corridor edits; community sets; flight norms) | source (the user's edits); shipped rule data | **owner: to decide (Mark, step 3)** | the set's file name | loaded once per change | — | today: shipped sets in `src/Resources/diagnostics/` (read-only); the user's in `<AppData>/diagnostics/`: `user.json` (pack), `user.norms.json` (the corridor editor's set), other `*.json` (community sets, read-only) | today: the pack editor and the corridor editor, through `atomicWrite`. **Repository: to decide (Mark, step 3)** |
| `Studio/Rig` | source (calibration results), versioned | its own entity | named; designed in the platform refactor (capture) | — | — | named; designed in the platform refactor (capture) | named; designed in the platform refactor (capture) |

### "Live", "open" and "on screen" are not objects (Mark, 14 Nov 2026)

There is one kind of object, `Session`, and three different facts about it:

| Fact | Kind | Where it lives |
|---|---|---|
| In progress or ended | Domain attribute, persisted, true without a screen | `Session.status`. "The athlete's live session" is a query: their session with status InProgress. At most one InProgress session per athlete (a domain rule). |
| Loaded in memory | Repository concern | `SessionRepository` is an identity map: one instance per id while anyone holds it. The panel, a catch-up, the report and a sweep get the same `Session`. `AthleteRepository` is one too *(clarification, for Mark to read)*. |
| On screen; being captured into | Application state | `AppState` ([§6](#6-application-state)) |

### Instruments (Mark, 14 Nov 2026)

| Rule | Forbids |
|---|---|
| The launch monitor is one device among the others. | A separate LM model or LM-only store |
| A shot has **one** set of measurements, each tagged with its source and its kind (reading or derived). P1 applies per kind. | Parallel per-instrument tables of the same metric |
| One rule decides which source wins for a metric. | Precedence stated in a presenter or per surface |
| The LM board, Charts and the report are presenters over the session's measurements (`SessionSeries`), filtered by source. | A presenter keeping its own per-club statistics |

### Club

| Rule | Detail |
|---|---|
| Source facts | The declared club (captured, kept in the Recording for provenance) and the golfer's correction (the board's override, in the shot's `review`) |
| Until step 6 | The correction is the LM board's override (`review.clubOverride`) only; the swing-edit popover's `review.club` is the declared club, because it cannot yet be told apart from the club capture seeds into it (IM-3; Mark, 14 Nov 2026) |
| Effective club | Derived: the correction wins, else the declared club. It is the one club for grading, every presenter and re-analysis (Mark, 14 Nov 2026). |
| Undeclared | An undeclared club is *undeclared* and graded as "club assumed", never a hidden DRIVER |
| Label | From one table |

### Commands go through the root

| Step | Who |
|---|---|
| 1. A use case calls a command on the root, e.g. `Session::correctLabel(shotRef, …)`, `Session::excludeShot(shotRef)`; the archive and the report are `Session` commands too (Mark, 14 Nov 2026) | Application |
| 2. The root validates the change | `Session` / `Athlete` |
| 3. The root persists it through its repository | `Session` / `Athlete` |
| 4. The root emits a domain event ([§7](#events-and-what-they-recompute)) | `Session` / `Athlete` |
| 5. The derived owners update | `SessionDiagnostics`, `AthleteDiagnostics`, `SessionSeries` |

Views never write files or mutate rows.

### Headless use

| Case | Rule |
|---|---|
| Within the app | Every consumer receives the instance already loaded (the identity maps). |
| Sweeps and tools (catch-up, `regrade_ledger`, swinglab) | Get their `Session` and `Athlete` from the same repositories, headless, on the tool's main thread. A domain object is the same whether a screen shows it or a sweep runs it. |
| Separate processes (the CLI tools) | Advisory lock files. |

---

## 6. Application state

Application state is what the app is doing now. It is not domain, and it is never persisted as domain data
(remembering the last screen or selection, if wanted, is an app preference). Selection is application state
(Mark, 14 Nov 2026). `AppState` lives in `src/App` (Mark, 14 Nov 2026).

```
AppState  (one per app, src/App)
 ├─ currentAthlete : Athlete          replaced on switch; a switch is refused while capture runs
 ├─ captureTarget : SessionId?        owned by the capture service; exposed here read-only
 ├─ sessionOnScreen : Session         the reviewed session, else the capture target
 ├─ mode : Capture | Replay | Analyse
 ├─ selection : ShotSelection         a ShotRef (or none), scoped to sessionOnScreen
 ├─ replay : ReplayState              what is playing, of the selected shot
 └─ castSelection : ShotSelection?    the capture target's newest shot; with nothing captured, the session on screen
```

| Member | Rule | Forbids |
|---|---|---|
| `currentAthlete` | Replaced on switch; an edit to the record is not a switch. A switch is refused while capture runs (an application rule). | Re-pointing app-lifetime objects at a new athlete by hand |
| `captureTarget` | Owned by the capture service; `AppState` exposes it read-only *(clarification, for Mark to read)*. That a session is in progress is the session's own attribute; which session capture writes into is application state. | Anything but the capture service setting it; capture writing anywhere but the session it asks |
| `sessionOnScreen` | The reviewed session, else the capture target. | Each view working out "the current session" for itself |
| `selection` | Like `QItemSelectionModel` beside an item model: refers to a shot by `ShotRef`, never owns it. Several views share one selection. Resolved through the session, never by folder name. Cleared by signal when its session closes or is replaced. | A view keeping its own selected shot; a selection outliving its session; matching by folder basename |
| `castSelection` | A second selection: the wall cast follows the newest shot being captured while the stage replays another. With nothing being captured, it shows the session on screen (Mark, 14 Nov 2026). Same rules as `selection`. | The cast keeping a selection of its own outside `AppState` |
| `replay` | What is playing, of the selected shot. | A second "selected" shot |

### What is not domain

| Thing | Layer |
|---|---|
| Current athlete, capture target, session on screen, mode, selections, replay | Application state (`AppState`) |
| A view's scroll position, open tab, expanded row, chart zoom | View state (the view's own) |
| A view-local helper: a filter proxy, a chart's metric helper | View state; may be created in QML |
| Last screen, last selection | App preference, if wanted |

---

## 7. Derived facts

### The three levels

| Level | Owner | Inputs (explicit, typed) | Results | Cached in |
|---|---|---|---|---|
| Shot | `SessionDiagnostics` grades every shot of its session and keeps the rows; they are shot-level facts, exposed through the shot (`ShotDiagnostics`, read-only) (Mark, 14 Nov 2026) | grading: each of the shot's numbers (one measurement set, every instrument) against its corridor (pack + norms), with the session's club baseline and the effective club. Then the session's usuals, flowing down as a typed input | the rows as graded; the flight-check overlay; outcome; strike · direction · flight; carry against the usual | `diagnostics.json` |
| Session | `SessionDiagnostics` | its shots' results; `sessionIntent`; the shots' intent; the session's club baseline | usuals, ledger, links/tiers, mixes, carry today, reasons, intent summaries (consistency within the session, shot against session intent) *(clarification, for Mark to read)*, the session summary the athlete level reads | `diagnostics.json`, `work_ons.json` (the session summary) *(clarification, for Mark to read)* |
| Athlete | `AthleteDiagnostics`, derived on demand | session summaries (intent summaries included); the athlete record | tendencies, work-ons, themes, baselines, distances, consistency, intent consistency across sessions | `tendencies.json`, `swing_themes.json` |

| Rule | Detail |
|---|---|
| Direction of reading | A level reads the level below as *results*; context flowing down (session usuals into a shot, the athlete baseline into a session) is a typed input. |
| The analysis pipeline never grades | Grading is `SessionDiagnostics`'. A corridor edit or a club correction regrades without re-analysis (Mark, 14 Nov 2026). |
| Athlete level | Reads session summaries, never raw shots, and never reruns a session's reduction itself. One exception: it appends to its tuning log on `LabelCorrected` (Mark, 14 Nov 2026). |
| Athlete level is lazy | Derived on demand and invalidated by events: an event marks it stale, and it recomputes when next read *(clarification, for Mark to read)*. |
| Flight check | A session-level overlay on unchanged rows; the rows as graded are never rewritten. |
| Direction | The pack's corridors everywhere; there is no second direction classifier. |
| Held conditions | One rule: Your swing follows Work-ons. |
| Intent | Never changes a verdict: PPS reports what it saw. |
| Words | Results are ids and numbers. Golfer-facing sentences are built in the words module, never in a reduction, and never persisted. |

### The analysis is a recorded product

A swing's analysis is derived from its Recording, but it is expensive and produced by versioned stages, so it is
kept in the swing document as a cache with a stamp (`k*StageVersion` and inputs, the effective club among them).

| Rule | Detail |
|---|---|
| Stale is known | A changed stage, or a club correction, makes the analysis known-stale, visibly. |
| Re-analysis reproduces live | Same Recording + same effective context (the effective club included) → same job. A corrected shot re-analyses with its corrected club (Mark, 14 Nov 2026). |
| Produced by | The analysis pipeline (an application service). It produces the recorded product only, never grades. |
| Bumping | A change to a stage's output bumps its `k*StageVersion`. |
| The input stamp | `analysis.versions.inputs` records the effective context the analysis used (the effective club, its length and source, height, club set; `kAnalysisInputsVersion`). A shot whose effective context now differs from its stamp, or that has no stamp, is stale. A change of inputs moves no `k*StageVersion` (IM-2; Mark, 14 Nov 2026). |
| One time base | `analysis.timeBase: "window"`: the analysis is window-relative on disk, rebased once in the pipeline; a document without the key is guessed in one place, the repository's reader (IM-2; Mark, 14 Nov 2026). |

### Events and what they recompute

The five events `ShotTrashed`, `SessionEnded`, `IntentRunChanged`, `SessionRelocated` and `RigChanged` are Mark's
(14 Nov 2026).

| Event | Recomputes |
|---|---|
| `ShotAdded`, `ShotReanalysed` | grade that shot → the session reduction → the session summary → marks the athlete level stale |
| `ShotTrashed` | the session reduction → the session summary → marks the athlete level stale |
| `ShotAnnotated` (a club correction) | marks the shot's analysis stale; **regrades** that shot with the corrected club, without re-analysis → the session reduction |
| `ShotAnnotated` (excluded, a reading left out) | that shot's results → the session reduction (no regrade) |
| `LabelCorrected` | that shot's results → the session reduction (no regrade); the athlete level appends to its tuning log |
| `ShotIntentChanged` | no verdict: the intent shown on the shot → the session's intent summaries |
| `IntentRunChanged` | pre-fills the intent of shots taken after the change; no shot already taken changes |
| `SessionIntentChanged` (focus, declared miss) | the session reduction only |
| `SessionEnded` | a status change: the session summary → marks the athlete level stale |
| `SessionRelocated` | pending the decision in step 2's brief (a library re-pointed mid-session) |
| `NormsChanged`, `PackChanged` (a corridor edit among them) | regrade through the stamp (rule version and input content), at once, without re-analysis, never "after a relaunch" |
| `AthleteRecordChanged` | athlete-level facts only. **Not** a session's club baseline or any Recording's analysis context (recorded inputs) |
| `RigChanged` | named; designed with Studio/Rig in the platform refactor (capture) |

### Stamps

| Rule | Detail |
|---|---|
| What a stamp holds | The version of every rule that produced the result, and a fingerprint of its inputs, norms and thresholds included |
| A stale stamp | The reader recomputes. It neither trusts the cache nor throws it away silently. |
| Rule versions | Hand-maintained constants declared beside the rule, and checked: a golden or parity test fails when a rule's output changes and its version did not *(clarification, for Mark to read)* |

---

## 8. Persistence

### Files

Every file is written only through its root (Mark, 14 Nov 2026).

| File | Kind | Owner (one writer) | Repository |
|---|---|---|---|
| `swing_NNNN/swing.ppsw`: the Recording (its `analysisContext` block, IM-1); the analysis (`analysis.versions.inputs`, `analysis.timeBase`, IM-2); measurements (the LM's readings among them) | source + recorded input + recorded product | written by capture, in stages (IM-4), then only by its `Session`; until step 3 by today's callers through the repository (IM-5) | `RecordingRepository` (implemented by `SwingStore`) |
| `swing_NNNN/swing.ppsw`, `review` block: rating, note, exclusion, club correction, shot intent, label corrections | source | the `Session` | `RecordingRepository` |
| `swing_NNNN/swing_phasegrid.json` | derived (shot cache, stamped) | the `Session` *(clarification, for Mark to read)* | `RecordingRepository` |
| `swing_NNNN/swing.json`, `swing_summary.json` | JSON-era swing document and its sidecar: read for old swings, removed on the first rewrite to `.ppsw`; retired | — | `RecordingRepository` (read only) |
| `<session>/session_input.json`: declared focus/work-on, declared miss, screen answers, intent runs | source | `Session` | `SessionRepository` |
| `<session>/diagnostics.json`: the graded rows, the reduction cache | derived (cache) | `SessionDiagnostics` | `SessionRepository` |
| `<session>/club_baseline.json` | recorded input: written once at first need, never regenerated, never deleted with the caches; rewritten only when its format is older than the current rule | `Session` | `SessionRepository` |
| `<session>/work_ons.json` | derived (cache): the session summary the athlete level reads | `SessionDiagnostics` *(clarification, for Mark to read)* | `SessionRepository` |
| `<session>/archived.json` | marker: the session is archived | `Session` (the archive is a `Session` command) | `SessionRepository` |
| The session report (report documents under the session) | derived (product) | `Session` (the report is saved by a `Session` command) | `SessionRepository` |
| Session status and start/end times | source | `Session` | **to decide (Mark, step 3)**; today not persisted |
| `<athlete>/athlete.json`: the record | source | `Athlete` | `AthleteRepository` |
| `<athlete>/tendencies.json`, `<athlete>/swing_themes.json` | derived (caches) | `AthleteDiagnostics` | `AthleteRepository` |
| `<athlete>/outcome_corrections.jsonl` | derived: the tuning log, append-only, never read back as truth | `AthleteDiagnostics`, appended on `LabelCorrected`, never by the session (Mark, 14 Nov 2026) | `AthleteRepository` |
| `<athlete>/fault_profile.json` | to be deleted (step 3) | — | — |
| `<AppData>/diagnostics/user.json`, `user.norms.json`, community `*.json` | source (the user's pack and corridor edits); community sets read-only | **owner: to decide (Mark, step 3)** | **to decide (Mark, step 3)** |
| `SessionSeries` | in memory only, never persisted | — | — |

This table covers the athlete, session and shot files and the rule data they are graded against. Studio and
acquisition files (the studio profile, the PPCP import ledger, calibration and ceremony recordings) belong to
Studio/Rig and Acquisition, designed in the platform refactor (capture).

The athlete's library folder is stored on the record, kept on rename, and migrated once. It is never derived from
the athlete's name.

### Rules

| Rule | Forbids |
|---|---|
| **One door per file.** Each file has one repository and one format definition. | A file name spelled outside its repository (lint A4); an envelope parsed in more than one place |
| **Atomic writes, one mechanism** (`QSaveFile`, behind one helper). | Remove-then-rename; part-then-rename; a fixed `.tmp`; a second write helper |
| **Schema checked on read.** A newer schema is read-only; an older shape is migrated in one step, in the repository. | A reader handling old shapes itself |
| **One serialiser per analysis block**, beside its producer: part of the Recording contract. The live detail and the disk form are the same functions, in one time domain. | Hand-kept converters of the analysis block; a live form that differs from the disk form |
| **A cache never holds the only copy** (P1). | Golfer input or a recorded input in `diagnostics.json` or any other cache |
| **Advisory lock files** guard a session or athlete folder against a second process (the CLI tools). | Two processes writing one file unguarded |
| **One-off migrations** of the library run once, with a dry-run report and backups on the share, and wait for Mark. | A silent migration of the live library |

### Golfer input has levels

| Input | Level | Stored |
|---|---|---|
| Shot intent | Shot: can differ from the session's ("we lose focus, we react to a bad shot") | the shot's `review` block |
| A "from here on" intent run | Session: a session statement. It **pre-fills each shot as it is taken**, and each shot can override it. An edited run affects only shots taken after the edit (Mark, 14 Nov 2026). | `session_input.json`; the pre-filled intent on each shot |
| Declared focus/work-on, declared miss, screen answers | Session | `session_input.json` |
| Intent consistency within a session; shot intent against session intent | Session, derived | `SessionDiagnostics`' intent summaries |
| Intent consistency across sessions | Athlete, derived from session summaries | `AthleteDiagnostics`' caches |

Intent never excuses a finding: PPS reports what it saw.

---

## 9. Concurrency and the QML boundary

### Concurrency

| Rule | Forbids |
|---|---|
| Domain objects and `AppState` live on one owner thread: the GUI thread in the app, the main thread in a tool. | A domain object touched from a worker |
| Grading, reductions and re-analysis run on workers over immutable inputs: a shot's analysis, a frozen baseline, a `shared_ptr<const>` pack and norms. | A worker reading a live member |
| Results are posted to the owning root, which applies them and emits change signals. Results for a session that has since closed are dropped by identity. | A worker writing a file or a model directly |
| Shared rule data (packs, norms, flight norms, the metric catalogue) is loaded once into immutable `shared_ptr<const>` snapshots. A change publishes a new snapshot and a `NormsChanged`/`PackChanged` event. | Lazy singletons first built on a worker; a private re-parse of a pack |
| One threading idiom: a single worker pool and one "post result to owner" helper. | A new pool, thread or idiom |

### The QML boundary

| Rule | Forbids |
|---|---|
| QML reaches the app through one object, `app`. `app.state` **is** the `AppState` (current athlete, capture target read-only, session on screen, mode, selection, cast selection, replay). `app.athlete` and `app.session` are shorthand for `app.state.currentAthlete` and `app.state.sessionOnScreen` *(clarification, for Mark to read)*. Context globals are only devices, settings, logging and `app`. | A new context property holding athlete- or session-scoped state |
| Lists are `QAbstractListModel`s with declared roles; values are `Q_GADGET`s (`ShotFactors`, `Carry`, `Club`, `Measurement`, `Intent`, …). | `QVariantMap`/`QVariantList` shapes documented only in comments (lint A6) |
| No domain or session-scoped C++ type is created in QML. View-local helpers (a filter proxy, a chart's metric helper) stay QML-created. | A view owning a session-level model (lint A2) |
| QML contains no domain rules. | "Clean vs Solid" in QML; shape by substring; counts parsed from sentences |

**Worked example: the carousel shot summary**, for LM shots:

```
app.state.selection.shot.diagnostics.factors.{strike, direction, flight}
app.state.selection.shot.diagnostics.carry
```

The selection is application state; the shot and its diagnostics are domain. Because every open session has a
`Session` whose `SessionDiagnostics` always runs, the summary is there whatever panels are on. It updates when
`ShotReanalysed` or `ShotAdded` lands for the selected `ShotRef`.

---

## 10. The ownership questions

Copy this section into every plan or brief that touches data, and fill one row per new or changed thing. A
reviewer answers the same six questions for the diff.

```markdown
## Ownership

| New or changed thing | 1. Kind | 2. Owner, level | 3. Created / destroyed by; identity | 4. If derived: inputs, rule version, invalidating events | 5. Persisted where; the one writer | 6. Layer (ring) of each new file |
|---|---|---|---|---|---|---|
| … | source / recorded input / derived / application state / view state | … | … | … | … | platform / vocabulary / rules / domain / application / adapter |

Needs Mark's approval (information-model change)? yes / no — new object or owner · new persisted file or field
family · new identity key · new dependency between levels · new event · anything created by a view.
```

| # | Question | Principle |
|---|---|---|
| 1 | Is it a source fact, a recorded input, a derived fact, application state or view state? | P1, P2 |
| 2 | Which object owns it, and at which level? | P3, P6 |
| 3 | Who creates and destroys it, and what identifies it? | P3, P7 |
| 4 | If derived: what are its inputs, its rule version, and the events that invalidate it? | P5 |
| 5 | Where is it persisted, and by which one writer (through which root)? | P1, P3, P4 |
| 6 | Which layer does each new file go in? | the rings, [§4](#4-the-rings-and-the-directory-mapping) |

---

## 11. Where the code is today

Updated by each refactor step in the commit that changes the status. Step numbers are those of
`docs/implementation/swe-audit/04-remediation-plan.md`; finding ids are in `02-findings.md`.

| Element | Status | Today | Delivered by |
|---|---|---|---|
| Recording contract: one builder, analysis only on a Recording | partly | the Recording types (`src/Recording`) and the one builder `buildAnalysisJob()` exist and are tested (`recording_contract_test`); no caller yet — live capture and re-analysis still build the job separately; `athlete.heightM` not written for a normal capture (H-02, A-01, E-11) | step 1 (1.5 switches the callers) |
| Analysis context recorded in the Recording: one `analysisContext` block, club set as the cohort, the bag snapshot (IM-1; Mark, 14 Nov 2026) | partly | the `AnalysisContext` type exists (`src/Recording/analysis_context.h`); the block is not yet written or read; the club set is never written, and readers look for `athlete.sex` / `dob`, which no writer puts in a document | step 1 (1.2 the block); the old library backfilled once with height and club set (D4: dry run to Mark first, backups on the share) |
| The input stamp and one time base (IM-2; Mark, 14 Nov 2026) | not yet | stage versions only; the window-time guess `tt >= windowT0 ? tt - windowT0 : tt` in several readers (A-03) | step 1 |
| Capture writes in stages; the interim writers through `RecordingRepository` (IM-4, IM-5; Mark, 14 Nov 2026) | not yet | 27 write call sites of the swing file (step 1's brief §3.4) | step 1 (the repository); step 3 (the `Session`) |
| The club correction is the board's override until step 6 (IM-3; Mark, 14 Nov 2026) | partly | the rule exists, `effectiveContext()` v1 (`src/Analysis/effective_context.*`); re-analysis does not call it yet, so it still never reads `clubOverride` | step 1 (re-analysis); step 6 (one club everywhere) |
| Norm cohort (sex, age band) not recorded in step 1 (IM-7; Mark, 14 Nov 2026) | — | — | step 3 decides whether grading reads it from the Recording |
| Same Recording + same effective context → same job; a corrected shot re-analyses with its corrected club | not yet | the board's override is read only by the board (G-07) | step 1 (re-analysis); step 6 (one club everywhere) |
| `RecordingRepository`, implemented by `SwingStore` | not yet | `SwingStore` exists; no interface | step 1 |
| One serialiser per analysis block | partly | Stage 0.6: one stored-score reader; hand-kept converters remain (A-02, H-01, E-10, A-03) | step 1 |
| A swing file written once by capture, then only by its `Session` | not yet | `ShotProcessor` and several readers write the document (E-09) | step 1 (the rule, `RecordingRepository`) |
| `src/Recording` (Vocabulary) and `src/DomainModel` created (IM-6; Mark, 14 Nov 2026) | partly | `src/Recording` exists, placed in the lint's ring table; `src/DomainModel` has its ring row but no files until `RecordingRepository` | step 1 (1.4: `src/DomainModel`) |
| `src/Export` renamed for the Recording contract | not yet | — | step 7 (the store only; the types are born in `src/Recording` in step 1) |
| Studio/Rig | not yet | rig assumptions spread through the code | the platform refactor (capture) |
| Rings enforced (`architecture_lint_test`, A1–A8); the Platform ring | not yet | the lint and its allowlist are being written (step G) | step G (lint); steps 1–8 shrink its allowlist |
| `src/DomainModel`; repository interfaces | not yet | directory does not exist | steps 1–3 |
| `AthleteId`, `SessionId`, `ShotRef`, `SessionIdentity` | not yet | athlete keyed by sanitised name; folder names parsed in many places (E-02, E-07, F-06) | step 2 |
| `Athlete` object, one per `AthleteId`, replaced on switch | partly | `AthleteController` holds a name; a switch is refused while a session runs, and a record edit is no longer treated as a switch (Stage 0, guard only) | step 2 |
| `athlete.json`; library folder on the record | not yet | profile in per-machine settings (H-09) | step 2 |
| `AthleteRepository`, `SessionRepository` (identity maps) | not yet | — | step 2 |
| Advisory lock files against the CLI tools | not yet | H-14 | step 2 |
| `Session` aggregate, status, commands and events | not yet | a session is a folder path in several places (E-08); status only in memory | step 3 (with step 2's repository) |
| `gradeShot()`, a pure function in `src/Diagnostics` | not yet | `SessionDiagnosticsModel::detectShot`, a member of the view model | step 3 |
| `SessionDiagnostics` grades every shot and keeps the rows; `ShotDiagnostics` a read-only view; always grade | not yet | one view-created `SessionDiagnosticsModel` per panel, cast, catch-up and report (D-01…D-05) | step 3 |
| `AthleteDiagnostics`, derived on demand from session summaries | not yet | `WorkOnsController`, an app-lifetime global re-pointed by hand | step 3 |
| `work_ons.json` written by `SessionDiagnostics` *(clarification, for Mark to read)* | not yet | written by the athlete-level `WorkOnsController` | step 3 |
| `outcome_corrections.jsonl` appended by the athlete level on `LabelCorrected` | not yet | appended by `SessionDiagnosticsModel` | step 3 |
| Club baseline as a recorded input, rewritten only when its format is outdated | partly | frozen at first need and rewritten when its schema is older than the rule (`club_distances.h`); written by whichever view-model instance has authority (D-06) | step 3 |
| Archive and report as `Session` commands | not yet | the archiver and the report model write the session folder directly | step 2 (the archive through `SessionRepository`), step 3 (`Session::archive()`), step 5 (the report): the lead's proposal; Mark agreed 14 Nov 2026 |
| Packs and norms: owner and repository | to decide (Mark, step 3) | pack and corridor editors write `<AppData>/diagnostics/` through `atomicWrite` | step 3 (decision); step 7 (norms loaded by repositories) |
| Shot outcome and three factors owned once | not yet | recomposed per consumer (C-01, G-03) | step 3 |
| `SessionSeries`, one per session | not yet | one `SessionSeriesModel` per view (F-01) | step 5 |
| One measurement set, tagged by source and kind; the LM a device; one precedence rule | not yet | LM rows inside `analysis.metrics`; a separate LM model (A-04, A-10) | step 6 |
| Flight check as a session overlay | not yet | the verdict is written into the rows (C-02) | step 6 |
| One direction rule (pack corridors) | not yet | two classifiers (F-05, G-01) | step 6 |
| Undeclared club graded "club assumed" | partly | Stage 0.3: graded right when the club is empty; capture still writes a DRIVER stub (the athlete's primary club, default DRIVER), so the shot reads back as a declared driver | step 6 (one club per shot): the lead's proposal; Mark agreed 14 Nov 2026 |
| Held conditions: one rule | done | Stage 0.12 | — |
| `AppState` in `src/App`; the selection and `castSelection` | not yet | the selection is held in five places (E-04, E-05) | step 4 |
| Golfer input out of the cache | partly | Stage 0.1: golfer input can no longer be lost from `diagnostics.json`; it still lives there (H-04) | step 2 |
| `session_input.json`; label corrections and shot intent on the shot's `review` | not yet | in the `diagnostics.json` envelope | step 2 |
| Atomic writes, one mechanism | partly | Stage 0.2: the shared `atomicWrite` is on `QSaveFile`; about 15 files still use `QSaveFile` directly, and two still write a part file and rename | step 2 (one helper, with the repositories): the lead's proposal; Mark agreed 14 Nov 2026 |
| One door per file; schema checked on read | not yet | envelope parsed in about 10 places; schema tags unchecked (D-08, H-08, H-12) | step 2 |
| `fault_profile.json` deleted | not yet | a reader on every activation, a writer with no caller (H-11, D-07) | step 3 |
| Stamps carry rule versions and every input | partly | analysis stage versions exist but gate reuse only; diagnostics stamps omit flight norms and thresholds (A-08, C-02, G-06) | steps 3 and 6 |
| Rule versions checked by a golden or parity test | not yet | versions bumped by hand, unchecked (C-05) | step 3 |
| `NormsChanged`/`PackChanged` reach the app at once | partly | Stage 0.4/0.4b: a saved corridor reaches session diagnostics and the LM board without a relaunch; no event mechanism yet | step 3 |
| Shared rule data as immutable snapshots | partly | Stage 0.4: the norm set is safe to share across threads; packs still assembled independently (B-02, B-03) | step 3 |
| One threading idiom | not yet | five idioms (F-09) | step 5 |
| QML through `app`; typed models and gadgets | not yet | 42 context globals; about 145 `QVariantMap`/`QVariantList` contracts (D-10) | steps 3–5 |
| No domain rules in QML | not yet | G-11, D-11 | step 5 |
| Rules without file I/O; words out of reductions | not yet | C-07, B-12, C-04 | step 7 |
