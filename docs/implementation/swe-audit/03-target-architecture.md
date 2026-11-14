# 03 · Target architecture — draft 2.1 **agreed by Mark, 14 Nov 2026**; draft 2.2 adds his 14 Nov decisions and the lead's clarifications for him to read (to be refined in each step's brief)

**Status:**
- Draft 2.2, 14 Nov 2026: clarifications from the information-model review (`docs/developer/information_model.md`).
  Decisions 1–23 (§13) are Mark's; 19–23 record his 14 Nov decisions below. Each change says who decided it.
  - **Mark, 14 Nov:**
    - **Grading** (the code's `detectShot`; step 3 renames it `gradeShot`): `SessionDiagnostics` grades every shot
      of its session, each number against its corridor, with the session's club baseline and the effective club,
      and keeps the rows (`diagnostics.json`, one writer). The rows are shot-level facts, exposed through the shot
      (`ShotDiagnostics`, read-only). The analysis pipeline never grades. A corridor edit or a club correction
      regrades without re-analysis. P6 is unchanged (§0.3, §3, §5).
    - A "from here on" intent run pre-fills each shot as it is taken; each shot can override it; an edited run
      affects only shots taken after the edit (§7).
    - P1 gains a third kind, the **recorded input**: written once at first need, never regenerated, never deleted
      with the caches. The club baseline is one; it is rewritten only when its format is older than the current
      rule, as today (§1, §3, §7).
    - Five events: `ShotTrashed`, `SessionEnded`, `IntentRunChanged`, `SessionRelocated`, `RigChanged` (§5).
    - With nothing being captured, the cast shows the session on screen (§4).
    - Every file is written only through its root: the archive and the report are `Session` commands; a shot's
      swing file is written once by capture, then only by its `Session`, which is how P3 holds for the Recording;
      `outcome_corrections.jsonl` is appended by the athlete level on `LabelCorrected`, never by the session (§7).
    - A corrected club: re-analysis uses the effective club, so the golfer's correction wins over the declared
      club; same Recording + same effective context → same job (§0.1, §5, §6; decision 18).
    - `AppState` lives in `src/App` (decision 11).
    - The **Platform** ring is added as ring 0, inside Vocabulary: logging (`ppInfo`/`ppWarn`), profiling and
      machine queries; every ring may include it, and nothing domain-shaped goes in it (§2).
  - **The lead's clarifications, consistent with P1–P9, for Mark to read:**
    - the Recording's analysis context is the only source of athlete facts for analysis; `Session.athleteSnapshot`
      is removed (§0.1, §3);
    - measurements carry a kind: a reading (source) or derived (the recorded product) (§1, §3);
    - the domain defines the repository interfaces, `RecordingRepository` among them, which `SwingStore`
      implements; `AthleteRepository` is an identity map too (§1, §2, §3);
    - the session level summarises intent, and the athlete level reads those summaries; `ShotIntentChanged`
      changes no verdict; the athlete level is derived on demand, marked stale by events (§5, §7);
    - `captureTarget` is the capture service's, read-only in `AppState`; `app.state` is the `AppState` (§4, §9);
    - "the owner thread" replaces "the GUI thread" for headless tools (§1, §8);
    - rule versions stay hand-kept but are checked by a golden or parity test (§5);
    - the persistence table's added rows and their kinds (§7);
    - contexts and rings are different cuts (§0);
    - §10 is numbered as the steps of `04-remediation-plan.md`.
  - **Open, to decide (Mark, step 3):** the owner and repository of packs and norms (the user's pack and corridor
    edits); where session status and start/end times are stored.
- Draft 2.1, 14 Nov 2026. It adds §0, the context map: three contexts, the Recording contract and Studio/Rig.
- Draft 2. It is rewritten around design principles. Draft 1 was organised around the findings,
  and it put the selection in the domain model, which Mark rightly challenged.
- Mark has agreed draft 2.1 and decisions 1–23. Nothing in a structural step of `04-remediation-plan.md` is built
  until Mark has approved that step's brief.
- Stage 0, local defect fixes that need no new structure, is committed.

## 0. The context map: three contexts and the contracts between them

PinPoint is three contexts, joined by two contracts (Mark and the lead, draft 2 review). This audit and this design
cover **Analysis** and **Domain + Application** in full. **Acquisition** is reviewed in a later session. Its
internals are out of scope here, but its contract with Analysis, the Recording, is decided now.

```
 ┌──────────────────────┐   Recording    ┌───────────────────────────────┐   results    ┌──────────────────────────────┐
 │ ACQUISITION          │ ─────────────▶ │ ANALYSIS                       │ ───────────▶ │ DOMAIN + APPLICATION          │
 │ transports, devices, │  (the layering │ per-shot DAG:  inference ·     │  (shot       │ Athlete · Session · Shot ·    │
 │ ring buffer,         │   contract)    │  fusion/synthesis · segmenta-  │   analysis,  │ Studio/Rig · persistence ·    │
 │ connectors,          │                │  tion · metric + joint extrac- │   shot       │ AppState · presentation       │
 │ calibration          │                │  tion · shot grading           │   grading)    │                              │
 │ procedures           │                │ cross-shot reductions: session │ ◀─────────── │ owns and stores everything;   │
 │                      │                │  and athlete diagnostics,      │   context    │ hands Analysis its context    │
 │                      │                │  assessment, coaching          │  (baselines, │ (athlete facts, rig state,    │
 └──────────────────────┘                └───────────────────────────────┘   rig, usuals)│  session usuals, norms)       │
                                                                                          └──────────────────────────────┘
```

**Contexts and rings are different cuts** (the lead's clarification). A context describes how data flows; a ring
(§2) describes which code may depend on which. So the grading and reduction *rules* belong to the Analysis context and
live in the Rules ring, while the *owners* that run them and keep their results (`SessionDiagnostics`,
`AthleteDiagnostics`) live in the Domain model ring. Acquisition and Analysis own nothing persistent: their products are owned and stored by the domain.

### 0.1 The Recording: the layering contract between Acquisition and Analysis

A Recording is everything Analysis is allowed to know about a captured swing, and nothing else:
- the streams: video frames, IMU and HackMotion samples, and the launch monitor's readings as delivered;
- their timestamps, on one stated clock model (camera timestamps mark exposure end; Spinnaker frames are
  host-stamped with about ±0.4 ms jitter);
- device identity and configuration;
- a reference to the **rig state** it was captured with (cameras, positions, calibrations);
- capture integrity (holes, lost frames);
- the **analysis context** it was captured under: the athlete facts the analysis consumes (handedness, height,
  cohort, the club and its length). These are recorded, so re-analysis never reaches for a live profile. It is the
  only source of athlete facts for analysis.

A shot's swing file is written **once by capture** (the Recording), **then only by its `Session`** (Mark, 14 Nov).
That is how P3 holds for the Recording.

**A corrected club (Mark, 14 Nov):** re-analysis uses the **effective club**, its length and norms, because it is what
was really hit. The one job builder applies the golfer's correction to make the **effective context**; Analysis itself
never reads the shot's `review`. The captured club stays in the Recording for provenance, and a correction is a
`ShotAnnotated` event that marks the shot's analysis stale. Parity is: **same Recording + same effective context
(the effective club included) → same job**, so a corrected shot re-analyses with its corrected club.

**The rule:** capture produces a Recording, and **Analysis only ever runs on a Recording**, one that arrived a second
ago or a year ago. Live analysis and re-analysis are then the same function by construction. That removes the root
of the parity drift:
- the job built from live objects on one path and from the document on the other;
- height lost on re-analysis;
- the standard club length set on one path only (H-02, A-01, E-11).

It also dissolves `ShotProcessor`'s mix of capture and analysis (E-09) into two services either side of the
contract.

**The name matters.** Today the contract's types, its reader and writer and its store (`SwingStore`, `swing_doc`,
the exporter) live in a directory called `src/Export`. That name hides what the code is. In the target it is **the
Recording contract**:
- its types are vocabulary, shared by both sides (§2);
- its store is an adapter;
- it moves, in the migration, to a directory named for what it is (e.g. `src/Recording`).

### 0.2 Studio / Rig: a domain entity the model was missing

A calibration *procedure* belongs to Acquisition. Its *result* is a source fact that Analysis consumes:
- which cameras and sensors;
- where they are;
- their intrinsic and extrinsic calibration;
- versioned over time.

That result belongs to the **Studio/Rig**, a domain entity, not to a device and not to a shot. Each Recording
references the rig state it was captured with.

Today assumptions such as "fixed cameras are square" and the uncalibrated DTL yaw σ are spread through the code,
because nothing owns the rig. Studio/Rig is named here; its design goes with the Acquisition session.

### 0.3 The shapes of Analysis

**Grading** is the name in the target for checking each of a shot's numbers against its corridor. The code calls it
`detectShot` (a member of `SessionDiagnosticsModel`); step 3 makes it a pure function and renames it `gradeShot`.

| Shape | What it is | Owner |
|---|---|---|
| **Per-shot DAG** | A pure function: Recording + effective context → the shot's analysis (the recorded product). A dependency graph of versioned stages, not a stack: segmentation feeds fusion and the metrics; impact timing feeds back (`analysis_dag_design.md`). | Run by the analysis pipeline (an application service); the results are stored on the `Shot`. **It never grades** (Mark, 14 Nov). |
| **Grading** | A pure function, `gradeShot()` in `src/Diagnostics`: each of the shot's numbers against its corridor, with the session's club baseline and the effective club → the shot's rows (shot-level facts). | `SessionDiagnostics` grades every shot of its session and keeps the rows (Mark, 14 Nov) |
| **Cross-shot reductions** | Session and athlete diagnostics, assessment, coaching, and the shot facts that take the session's usuals as input (flight-check overlay, composed outcome, the three factors, carry against the usual). Incremental and stateful, over many shots, recomputed by events (§5). | `SessionDiagnostics`, `AthleteDiagnostics` |

### 0.4 Decided now, and deferred

| Decided in this design | Deferred to the Acquisition session |
|---|---|
| The three contexts and their direction | Transports, devices, the ring buffer, connectors, threading inside capture |
| The Recording contract: its contents, the rule "analysis only on a Recording", its name | The capture pipeline that produces it |
| Studio/Rig as a domain entity, referenced by each Recording | Its full model, and the calibration procedures |
| The analysis context recorded in the Recording | How devices deliver their data into it |

## 1. The principles the design follows

Everything below follows from nine principles. They are the part to agree first. The rest is their
application to PinPoint.

| # | Principle | What it means here | What breaks it today |
|---|---|---|---|
| P1 | **Source facts, recorded inputs and derived facts are different kinds of thing.** | A *source fact* is something only the world or the golfer can tell us: the recordings and the readings every instrument delivers (cameras, IMUs, the launch monitor alike), the athlete record, what the golfer said. It is stored once, by its owner, and never regenerated. A *recorded input* (Mark, 14 Nov) is context written once at first need so a result is reproducible (the Recording's analysis context; a session's club baseline, `club_baseline.json`): never regenerated, never deleted with the caches, and rewritten only when its format is older than the current rule (today's `clubBaselineIsOutdated`, `src/Coaching/club_distances.h`). A *derived fact* (every diagnostic, at every level; tendencies; the athlete's baselines) is a pure function of source facts, recorded inputs and versioned rules. It may be cached, and may be deleted and rebuilt at any time. A cache never holds the only copy of anything. A measurement is a source fact when it is a **reading** delivered by an instrument (LM, HackMotion), and part of the recorded product when it is **derived** by rules from the Recording's streams (camera, IMU). | Golfer input stored inside the regenerable ledger (H-04, the Critical); a session verdict written into shot rows (C-02). |
| P2 | **Domain, application state and view state are three layers.** | *Domain*: what is true without a screen, the same in a GOLFSIMPC sweep. *Application state*: what the app is doing now (current athlete, the session on screen, the mode, what is selected, what is replaying). *View state*: one view's scroll, open tab, expanded row. Each refers only downwards. | Diagnostics owned by a view (D-02). Selection held in five places under four definitions (E-04). Domain rules in QML (G-11). |
| P3 | **Aggregates have one root, and the root is the only writer.** | An `Athlete` is a root: its record and its sessions' index. A `Session` is a root: its shots, the golfer's session input, and the session's derived facts. Every change to a session goes through the session (a command), so one object enforces its rules and writes its files. Every file is written only through its root (Mark, 14 Nov): the archive and the report are `Session` commands; a swing file is written once by capture, then only by its `Session`. | Four instances writing one `diagnostics.json` (H-03, D-01). The athlete changed under a running session (E-01). |
| P4 | **Persistence lives behind repositories, not in domain objects or rules.** | The domain model defines the repository interfaces (`RecordingRepository`, `SessionRepository`, `AthleteRepository`); adapters implement them, `SwingStore` (already good) implementing `RecordingRepository`. Domain objects and the Coaching/Diagnostics rules never touch a file. One repository per file family, one format definition, schema checked on read. | File I/O in Coaching headers (C-07); `diagnostics.json` parsed in about 10 places (D-08); file names spelled per class (H-12). |
| P5 | **Derivation is an explicit dependency graph, updated by events.** | Each derived fact declares its inputs and its rule version. A change is an event (shot added, re-analysed, annotated, corrected; intent changed; norms changed; athlete record changed). Only what depends on the change is recomputed. A rule's version is part of its result's stamp, so stale results are known, not guessed. | `mutable` dirty flags that rebuild everything (D-13); stamps that omit their inputs (C-02, G-06); hand-bumped versions (C-05); stale library analyses that are invisible (A-08). |
| P6 | **One rule, one result, one owner, at the right level.** | A level reads the level below as *results*. Context flowing down (session usuals into a shot; the athlete baseline into a session) is a typed input. Nothing recomputes another level's facts. | Shot outcomes recomposed by four consumers (G-03, C-01); two direction rules (F-05/G-01); handedness glossed per surface (G-02). |
| P7 | **Identities are types, not strings.** | `AthleteId` (uuid), `SessionId`, `ShotRef` (session + swing number) are value types. Folders, display numbers and labels are *properties*, never keys. | Athlete keyed by name (E-02); "Shot N" meaning three things (E-06); a selection matched by folder basename (E-05). |
| P8 | **Work runs on immutable snapshots; results come back to the owner.** | Domain objects live on one owner thread: the GUI thread in the app, the main thread in a tool. Grading and reductions run on workers over immutable inputs and post results to the owning root, which publishes them. No worker reads a live object. | The worker reading GUI-thread members (D-12); lazy singletons first touched on workers (B-03); five threading idioms (F-09). |
| P9 | **The QML boundary is typed.** | QML sees `Q_GADGET` value types and `QAbstractListModel`s with declared roles. There are no `QVariantMap` shapes documented only in comments, and no domain object is created in QML. | 145 `QVariantMap`/`QVariantList` contracts (D-10); 20 C++ types created by views (`data/seam_inventory.md`). |

## 2. Layers: rings, by kind of code

Dependencies point **inward only**: a ring may use the rings inside it, never one outside. Draft 1 drew a chain
of today's folders, which mixed kinds of code (persistence inside the rule stack; metric definitions after the
analysis). Mark, draft 2 review: the rings are drawn by kind of code instead, and today's folders are mapped onto
them.

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
 │  │  │               · Intent · Club · repository interfaces                      │  │  │
 │  │  │  ┌────────────────────────────────────────────────────────────────────┐  │  │  │
 │  │  │  │ RULES (pure)  measurement algorithms (raw device data → measurements,│  │  │  │
 │  │  │  │   incl. the LM's inferred reads) · instrument precedence · grading ·  │  │  │  │
 │  │  │  │   diagnostics engine + packs/norms · coaching rules                  │  │  │  │
 │  │  │  │  ┌──────────────────────────────────────────────────────────────┐  │  │  │  │
 │  │  │  │  │ VOCABULARY  value types · ids · metric definitions + catalogue│  │  │  │  │
 │  │  │  │  │             · club vocabulary · units                          │  │  │  │  │
 │  │  │  │  └──────────────────────────────────────────────────────────────┘  │  │  │  │
 │  │  │  └────────────────────────────────────────────────────────────────────┘  │  │  │
 │  │  └──────────────────────────────────────────────────────────────────────────┘  │  │
 │  └────────────────────────────────────────────────────────────────────────────────┘  │
 └──────────────────────────────────────────────────────────────────────────────────────┘
```

| Ring | Holds | Never |
|---|---|---|
| **Platform** (ring 0; Mark, 14 Nov) | Logging (`ppInfo`/`ppWarn`), profiling, machine queries. Every ring may include it. | Depends on anything outside itself; holds anything domain-shaped. |
| **Vocabulary** | What things *are*: value types, identity types, metric definitions and the catalogue, club vocabulary, units. | Depends on anything but Platform. |
| **Rules** | Pure functions: measurement algorithms (pose, club, ball, IMU fusion, kinematics, wrist, 3-D skeleton, the LM's inferred reads); which source wins for a metric (one rule, A-04); grading; coaching (outcomes, factors, flight check, Sky, tendencies, link grading). | File I/O; golfer-facing sentences; knowing about sessions or screens (F-11, C-04, C-07, B-10). |
| **Domain model** | Entities and aggregates (§3); the owners of derived facts (§5). Qt objects, for signals; no QML. Persistence through repository *interfaces* (`RecordingRepository`, `SessionRepository`, `AthleteRepository`) that the adapters implement. | Including persistence, device or UI code. |
| **Application** | `AppState` (§4); use cases (the commands that change the domain); services that run work: capture, the analysis pipeline (orchestrating the measurement rules for a swing), re-analysis, the athlete catch-up. | Holding domain data of its own. |
| **Adapters** | **Devices** deliver raw data, or measurements: a launch monitor is a device like any other; it simply needs no algorithm. **Persistence** implements the repositories (`SwingStore` implements `RecordingRepository`; the session and athlete files). **UI**: presenters, the words module, QML. | Being depended on by an inner ring. |

**Today's directories, mapped** (the ring table `src/Gui/tests/architecture_lint_test.cpp` enforces):

| Ring | Directories (and files placed against their directory) |
|---|---|
| Platform | the logging, profiling and machine-query files of `Core` (`pp_debug`, `pp_log_stream`, `pp_profiler`, `cpu_topology`, …) |
| Vocabulary | `Metrics` (definitions, catalogue); the rest of `Core`; the shared types of `Analysis`; the stream types of `Buffer`; the reading types of `LaunchMonitor`; `Export/frame_decode` (**the Recording contract's types**, §0.1) |
| Rules | `Pose`, `Club`, `Ball`, `Body`, `IMU`, `Kinematics`, `Wrist`, `Skeleton3d`, `Timeline`, `Diagnostics`, `Coaching`; the inferred reads in `LaunchMonitor`; `Core/kf3_smoother` |
| Domain model | `src/DomainModel` (new) |
| Application | `Analysis` (the pipeline, re-analysis); capture and session services now in `src/Gui/shot`; `AppState` in `src/App` |
| Adapters | `Gui`; the **Recording store** (today in `src/Export`: `SwingStore`, `swing_doc`, the exporter) and the repositories; `Buffer` (the ring itself); `Video`, `Audio`, `LaunchMonitor` (connectors), `Ppcp`, `LLM`, `STT`, `TTS`, `Secrets`, `Update`; the device, settings and studio-profile files of `Core` |
| No ring (not C++) | `Resources` (shipped data); `Shaders` |

Five directories split across rings today, which the migration resolves:
- `Export`: the Recording contract's types (vocabulary) and its store (an adapter), under a name that hides both (§0.1);
- `Analysis`: shared types, algorithm stages and orchestration (A-05, A-11);
- `Core`: platform, vocabulary, and device, settings and studio-profile infrastructure;
- `LaunchMonitor`: reading types, connectors (adapters) and inferred reads (rules);
- `Buffer`: stream types (vocabulary) and the ring buffer (an adapter).

**Note on names:** the code uses "focus" for two unrelated things, which is a symptom of P2.
- `focusedShotId` is application state: what is selected.
- `focusConditionId` is a domain source fact: what the golfer declared they are working on.

The target renames the first to `selection`.

## 3. The domain model

```
Athlete  (aggregate root · id: AthleteId)
 │  record: name, handedness, height, sex/dob, bag            ← source fact, athlete.json
 │  libraryFolder (stored on the record, not derived from the name)
 │  sessions: SessionIndex (SessionId → summary)             ← index only; sessions load on demand
 │  (no "live" or "open" slots: see the note below the diagram)
 ├─ AthleteDiagnostics      derived: tendencies, work-ons, themes, club baselines + distances,
 │                           consistency, intent consistency — from SESSION results, never from raw shots
 │
Session  (aggregate root · id: SessionId)
 │  identity: folder, start time, local day, n-of-day, type   (SessionIdentity, computed once)
 │  status: InProgress | Ended   (start/end times persisted, where: to decide, Mark, step 3; at most one
 │                                InProgress per athlete — a domain rule)
 │  clubBaseline: club baselines from the athlete level, written once at first need, never at Open; rewritten
 │                only when its format is older than the current rule   ← recorded input, club_baseline.json
 │  sessionIntent: declared focus / work-on, declared miss, screen answers, intent runs   ← source fact
 ├─ shots: Shot*  (entities · id: ShotRef)
 │    recording handle (SwingStore) · analysis (recorded product, versioned — §5)
 │    measurements: one set, each {metric, source instrument + route, kind, value, σ, gates}
 │      — from every instrument alike: a reading (LM, HackMotion) is a source fact; a derived measurement
 │        (camera/IMU via the measurement rules) is part of the recorded product
 │    annotations: per-source judgements (this LM reading left out; the club it was really hit with)
 │    shotIntent (pre-filled from the intent run in force when taken; the shot can override) · rating/note ·
 │    label corrections   ← source facts
 │    effective club (the correction, else the declared club)   ← derived
 │    diagnostics: ShotDiagnostics (read-only view of this shot's rows and results — shot-level facts kept by
 │                 SessionDiagnostics: rows, overlay, outcome, strike · direction · flight, carry)
 ├─ SessionDiagnostics      derived: grades every shot and keeps the rows; usuals, ledger, links/tiers, mixes,
 │                           carry today, reasons, summary
 └─ SessionSeries           derived, in memory only: the session's measurements as series (one per session);
                             the LM board, Charts and the report are presenters over it, filtered by source
```

**"Live", "open" and "on screen" are not objects** (Mark, draft 2 review). There is one kind of object,
`Session`, and three different facts about it:
- **In progress or ended** is a domain *attribute* (`Session.status`), persisted and true without a screen.
  "The athlete's live session" is a query: their session with status InProgress.
- **Loaded in memory** is a repository concern. `SessionRepository` is an identity map: one instance per
  `SessionId` while anyone holds it. `AthleteRepository` is one too: one `Athlete` per `AthleteId`, shared by the
  current-athlete slot and any headless service. The panel, a catch-up, the report and a sweep all get the same object, so
  two copies of one session cannot exist.
- **On screen** and **being captured into** are application state (§4).

**Lifetimes:**

| Object | Created | Destroyed |
|---|---|---|
| `Athlete` | loaded by `AthleteRepository` when first asked for (the athlete becomes current, or a headless service); one instance per `AthleteId` | when its last holder releases it |
| `Session` | loaded by `SessionRepository` when first asked for (Start creates a new one); one instance per `SessionId` | when its last holder releases it. Ending a session changes its status; it does not destroy the object |
| `Shot` | with its session; a new shot at capture | with its session |

**How the domain is used:**
- A domain object is the same whether a screen shows it or a sweep runs it. The catch-up, `regrade_ledger`
  and swinglab get their `Session` from the same repository, headless.
- Within the app they always receive the instance already loaded (the identity map). Separate processes
  (the CLI tools) are a cross-process question, H-14: advisory lock files.
- **Commands go through the root.** For example `Session::correctLabel(shotRef, …)` and
  `Session::excludeShot(shotRef)`; the archive and the report are `Session` commands too (Mark, 14 Nov). The root validates the change, persists it through its repository, emits a
  domain event, and the derived owners update. Views never write files or mutate rows.

## 4. Application state (not domain)

```
AppState  (one per app, in src/App)
 ├─ currentAthlete : Athlete           replaced on switch (refused while capture is running — an application rule)
 ├─ captureTarget : SessionId?         the session capture is writing into (owned by the capture service; read-only here)
 ├─ sessionOnScreen : Session          the reviewed session, else the capture target
 ├─ mode : Capture | Replay | Analyse
 ├─ selection : ShotSelection          a ShotRef (or none), scoped to sessionOnScreen
 ├─ replay : ReplayState               what is playing — of the selected shot, not a second "selected"
 └─ castSelection : ShotSelection?     the wall cast follows the newest shot captured while the stage replays
                                       another; with nothing being captured, it shows the session on screen
                                       (Mark, 14 Nov)
```

- **Selections are application state.** Like Qt's `QItemSelectionModel` beside an item model, they refer to
  domain objects by identity (`ShotRef`) and never own them. Several views share one selection, and more than
  one selection can exist (the stage and the cast).
- **Lifetime follows the domain, by signal, not by ownership.** A selection listens to its session's
  lifetime and clears itself when the session closes or is replaced (E-05). It resolves its `ShotRef` through
  the session, never by folder name.
- **`AppState` is not persisted as domain data.** Remembering the last screen or the last selection, if
  wanted, is an app preference.
- **The current athlete, the capture target and the session on screen are application state.** That a
  session is in progress is a domain attribute of the session (§3). Which session the screen shows, or capture
  writes into, is not.

## 5. Derived facts: levels, dependencies, versions

| Level | Owner | Inputs (explicit, typed) | Results | Cached in |
|---|---|---|---|---|
| Shot | `SessionDiagnostics` grades every shot of its session and keeps the rows; they are shot-level facts, exposed through the shot (`ShotDiagnostics`, read-only) (Mark, 14 Nov) | grading: each of the shot's numbers (every instrument, one set) against its corridor (pack + norms), with the session's club baseline and the effective club; then the session's usuals, flowing down as a typed input | rows as graded; the flight-check overlay; outcome; strike · direction · flight; carry against the usual | `diagnostics.json` (cache) |
| Session | `SessionDiagnostics` | its shots' results; `sessionIntent`; the shots' intent; the session's club baseline | usuals, ledger, links/tiers, mixes, carry today, reasons, intent summaries (consistency within the session; shot against session intent), the session summary the athlete level reads | `diagnostics.json`, `work_ons.json` (the session summary) |
| Athlete | `AthleteDiagnostics`, derived on demand: an event marks it stale and it recomputes when next read | session summaries (intent summaries included); the athlete record | tendencies, work-ons, themes, baselines, distances, consistency, intent consistency across sessions | `tendencies.json`, `swing_themes.json` |

The analysis pipeline never grades. A corridor edit or a club correction regrades without re-analysis (Mark, 14 Nov).

**The analysis is a "recorded product".** A swing's analysis is derived from the recording, but it is
expensive and is produced by versioned stages. It is treated as a cache with a stamp (`k*StageVersion` and
inputs, the effective club among them), kept in the swing document. A changed stage, or a club correction, makes
it known-stale; re-analysis refreshes it.
- Re-analysis must reproduce live: the same job builder; same Recording + same effective context (the effective
  club included) → same job (H-02, A-01).
- The analysis pipeline produces the recorded product only; it never grades.
- Staleness is visible, not discovered by accident (A-08).

**Events and invalidation (P5).** `ShotTrashed`, `SessionEnded`, `IntentRunChanged`, `SessionRelocated` and
`RigChanged` are Mark's (14 Nov).

| Event | Recomputes |
|---|---|
| `ShotAdded`, `ShotReanalysed` | grade that shot → the session reduction → the session summary → marks the athlete level stale |
| `ShotTrashed` | the session reduction → the session summary → marks the athlete level stale |
| `ShotAnnotated` (club correction) | marks the shot's analysis stale; **regrades** that shot with the corrected club, without re-analysis → the session reduction |
| `ShotAnnotated` (excluded, a reading left out) | that shot's results → the session reduction (no regrade) |
| `LabelCorrected` | that shot's results → the session reduction (no regrade); the athlete level appends to its tuning log (Mark, 14 Nov) |
| `ShotIntentChanged` | no verdict: the intent shown on the shot → the session's intent summaries |
| `IntentRunChanged` | pre-fills the intent of shots taken after the change; no shot already taken changes |
| `SessionIntentChanged` (focus, declared miss) | the session reduction only |
| `SessionEnded` | a status change: the session summary → marks the athlete level stale |
| `SessionRelocated` | pending the decision in step 2's brief (a library re-pointed mid-session) |
| `NormsChanged`, `PackChanged` (a corridor edit among them) | regrade through the stamp (the rule version and the content of the inputs), without re-analysis — never "until relaunch" (B-01) |
| `AthleteRecordChanged` | athlete-level facts. **Not** a session's club baseline or any Recording's analysis context: recorded inputs, frozen by design (E-03) |
| `RigChanged` | named; designed with Studio/Rig in the Acquisition session |

**Stamps:** every cached derived result carries the versions of its rules and a fingerprint of its inputs,
norms and thresholds included (C-02, G-06). A reader that finds a stale stamp recomputes. It does not trust the
cache and does not throw it away silently. Rule versions stay hand-maintained constants declared beside the rule,
but are checked: a golden or parity test fails when a rule's output changes and its version did not (C-05;
step 3).

## 6. Identity

| Thing | Identity (type) | Properties, never keys |
|---|---|---|
| Athlete | `AthleteId` (uuid) | name; library folder (stored on the record, migrated once) |
| Session | `SessionId` | folder; start time; local day; n-of-day; type. Computed once by `SessionIdentity`; **no other code parses a folder name** |
| Shot | `ShotRef` = (`SessionId`, swing number) | folder; "Shot N" (always the swing number); a rank "n of N" only shown beside it |
| Club | the effective club per shot, derived: the golfer's correction (the board's override), else the declared club | label (one table). An undeclared club is *undeclared* and graded as "club assumed", never a hidden DRIVER |

## 7. Persistence and repositories

| File | Kind (P1) | Owner (one writer) | Repository |
|---|---|---|---|
| `swing.ppsw` — recordings, analysis, instrument readings (the LM's among them), shot `review` (rating, note, exclusion, club override, **shot intent**, **label corrections**) | source + recorded product | written once by capture (the Recording), then only by its `Session` (Mark, 14 Nov) | `RecordingRepository` (`SwingStore`, exists) |
| `swing_NNNN/swing_phasegrid.json` | derived (shot cache, stamped) | the `Session` | `RecordingRepository` |
| `swing_NNNN/swing.json`, `swing_summary.json` | the JSON-era document and its sidecar: read for old swings, removed on the first rewrite to `.ppsw`; retired | — | `RecordingRepository` (read only) |
| `<session>/session_input.json` — declared focus/work-on, declared miss, screen answers, intent runs | source | `Session` | `SessionRepository` |
| `<session>/diagnostics.json` — rows, reduction cache | derived (cache) | `SessionDiagnostics` | `SessionRepository` |
| `<session>/club_baseline.json` | recorded input (P1): written once at first need, never regenerated, never deleted with the caches; rewritten only when its format is older than the current rule | `Session` | `SessionRepository` |
| `<session>/work_ons.json` | derived (cache): the session summary the athlete level reads | `SessionDiagnostics` | `SessionRepository` |
| `<session>/archived.json` | marker | `Session` (the archive is a `Session` command; Mark, 14 Nov) | `SessionRepository` |
| the session report (report documents under the session) | derived (product) | `Session` (saved by a `Session` command; Mark, 14 Nov) | `SessionRepository` |
| session status and start/end times | source | `Session` | to decide (Mark, step 3); today not persisted |
| `<athlete>/athlete.json` — the record (Mark, 14 Nov) | source | `Athlete` | `AthleteRepository` |
| `<athlete>/tendencies.json`, `swing_themes.json` | derived (cache) | `AthleteDiagnostics` | `AthleteRepository` |
| `<athlete>/outcome_corrections.jsonl` | derived: the tuning log, append-only | the athlete level (`AthleteDiagnostics`), on `LabelCorrected`, never the session (Mark, 14 Nov) | `AthleteRepository` |
| `<athlete>/fault_profile.json` | to be deleted (H-11, step 3) | — | — |
| `<AppData>/diagnostics/user.json`, `user.norms.json`, community `*.json` (packs and norms; the corridor editor writes `user.norms.json`) | source (the user's edits); community sets read-only | owner: to decide (Mark, step 3) | to decide (Mark, step 3) |
| `SessionSeries` | in memory only, not persisted | — | — |

The table covers the athlete, session and shot files and the rule data they are graded against. Studio and
acquisition files (the studio profile, the PPCP import ledger, calibration and ceremony recordings) go with Studio/Rig
and the Acquisition session.

**Rules:**
- **One door per file.** Each file has one repository and one format definition (no envelope parsed in 10
  places, no file name spelled in 5).
- **Atomic writes, one mechanism (`QSaveFile`, behind one helper)** — Stage 0.2 has done this for the shared
  `atomicWrite`; about 15 files still use `QSaveFile` directly, and two still write a part file and rename.
- **Schema checked on read.**
  - A newer schema is read-only.
  - An older shape is migrated in one step, in the repository.
- **One serialiser per analysis block, beside its producer.** The live detail and the disk form are the same
  functions in one time domain (H-01, A-03).
- **Golfer input has levels** (Mark):
  - Shot intent is the shot's, and can differ from the session's ("we lose focus, we react to a bad shot").
  - A "from here on" run is a session statement that pre-fills each shot as it is taken, and each shot can override
    it. An edited run affects only shots taken after the edit (Mark, 14 Nov).
  - The session level summarises intent (consistency within the session, shot intent against session intent); the
    athlete level reads those summaries for consistency across sessions, never raw shots.
  - Intent never excuses a finding; PPS reports what it saw.

## 8. Concurrency (P8)

- **Domain objects and `AppState` live on one owner thread**: the GUI thread in the app, the main thread in a tool.
- **Grading, reductions and re-analysis run on workers**, over immutable inputs: a shot's analysis, a
  frozen baseline, a `shared_ptr<const>` pack and norms.
- **Results are posted to the owning root**, which applies them and emits change signals. Results for a
  session that has since closed are dropped by identity.
- **Shared rule data** (packs, norms, flight norms, the metric catalogue) is loaded once into immutable
  `shared_ptr<const>` snapshots. A change publishes a new snapshot and a `NormsChanged`/`PackChanged` event.
  There are no lazy singletons first built on a worker (B-02, B-03).
- **One threading idiom** — a single worker pool and "post result to owner" helper — replaces the five
  (F-09).

## 9. The QML boundary (P9)

- **QML reaches the app through one object, `app`:**
  - `app.state`, which **is** the `AppState` (the current athlete, the capture target read-only, the session on
    screen, mode, selection, cast selection, replay);
  - `app.athlete` and `app.session`, shorthand for `app.state.currentAthlete` and `app.state.sessionOnScreen`;
  - their typed children.

  Today's 42 context globals shrink to devices, settings, logging and `app`.
- **Lists are `QAbstractListModel`s with declared roles.** Values are `Q_GADGET`s (`ShotFactors`, `Carry`,
  `Club`, `Measurement`, `Intent`, …).
- **No domain or session-scoped C++ type is created in QML.** View-local helpers (a filter proxy, a chart's
  metric helper) stay QML-created.
- **QML contains no domain rules:** no "Clean vs Solid", no shape-by-substring, no counts parsed from
  sentences (G-11, D-11).

## 10. How we get there (migration strategy)

The change is made **incrementally, strangler-style.** The app keeps working after every step, and each step
is testable on its own.

The steps are numbered as in `04-remediation-plan.md` §3.

- **Step G. Guardrails first:** the parity baseline, and the lint (§11) with today's violations allowlisted, so
  every step can only shrink the list.
1. **The Recording contract.** One builder makes the Recording, and its effective analysis context, for live capture
   and for re-analysis alike, so the analysis service has one input. `RecordingRepository` arrives here. Stage 0.10
   is its first piece.
2. **Identity types and repositories** (`AthleteId`, `SessionId`, `ShotRef`, `SessionIdentity`;
   `AthleteRepository`, `SessionRepository`). Existing code adopts them file by file.
3. **`Session` and `SessionDiagnostics` arrive as owners**, and the existing `SessionDiagnosticsModel` is split
   behind them; grading becomes the pure `gradeShot()`.
   - The panel, the cast, the report and the catch-up are re-pointed at the owner one at a time.
   - Each re-pointing deletes a private instance.
   - A thin adapter keeps today's QML API until its last view moves.
4. **`AppState` and the selection** replace `SessionMode`'s focus fields, `CurrentSwing` and the carousel's
   copy.
5. **Charts and the report onto the session; presenters move to typed results**, one view at a time. The
   `QVariantMap` shapes go as each view moves.
6. **Measurements unified, the flight-check overlay**: one source-tagged set per shot; the LM a device.
7. **Rules move down and lose their I/O** (domain code out of `src/Gui`; norms loaded by repositories).

## 11. Enforcement: `architecture_lint_test`

| Rule | Catches |
|---|---|
| A1 | an include from a lower layer into a higher one (§2) |
| A2 | QML creating a type marked domain- or session-scoped |
| A3 | folder-name parsing outside `SessionIdentity` / `SwingPaths` |
| A4 | a session or athlete file name outside its repository |
| A5 | file I/O in a rules directory (Coaching, Diagnostics, Analysis rules) |
| A6 | a `QVariantMap`/`QVariantList` added to a presenter's or a domain type's public API |
| A7 | a new function over 300 lines in rings 1–2 |
| A8 | Analysis code reaching past the Recording contract into Acquisition (device classes, the ring buffer, live controllers) or into application state (the athlete controller, settings) |

The allowlist is tagged by the migration step that removes each entry. Adding an entry needs Mark's approval.

## 12. Where the carousel shot summary lands

For LM shots only:

```
app.state.selection.shot.diagnostics.factors.{strike, direction, flight}
app.state.selection.shot.diagnostics.carry
```

The selection is application state; the shot and its diagnostics are domain. Because a `Session` exists for
every open session and its `SessionDiagnostics` always runs, the summary is there whatever panels are on. It
updates when `ShotReanalysed` or `ShotAdded` lands for the selected `ShotRef`.

## 13. Decisions

**Agreed by Mark, 14 Nov 2026:**

| # | Decision | Answer |
|---|---|---|
| 1 | The object model | Approved. **Every first-class domain object is C++**: Athlete, Session, Shot, the three diagnostic owners, measurements, club, intent. QML holds no domain objects or rules. *(Draft 2: the selection is application state, §4, not domain.)* |
| 2 | Cost | Always grade: every live shot is graded whether or not a panel is on. |
| 3 | Golfer input | Stored at its level: shot intent on the shot, session intent in the session; aggregates derived (§7). |
| 4 | Athlete library folder | Stored on the record; kept on rename; migrated once. |
| 5 | Athlete profile | `athlete.json` in the library; settings keep only "current athlete". |
| 6 | Flight check | A session-level overlay on unchanged rows. |
| 7 | Direction | The pack's corridors everywhere; the LM classifier goes. |
| 8 | Score pill | Rethink the per-shot number: a design proposal for later; the red "0" filed as an issue. |
| 9 | Held conditions | One rule: Your swing follows Work-ons. |
| 10 | The new layer | `src/DomainModel`. |

**New in drafts 2 to 2.2, decided by Mark on 14 Nov:**

| # | Decision | Proposal |
|---|---|---|
| 11 | Application state | A separate `AppState` (current athlete, capture target, session on screen, mode, selections, replay), not part of the domain, in `src/App` (Mark, 14 Nov). Sessions have a status (in progress/ended), not live/open slots, and the repository is an identity map. |
| 12 | Commands | All domain changes go through aggregate-root commands, with domain events (§3, §5). |
| 13 | Shot label corrections | Move them from the session file to the shot's `review` block, with shot intent. That puts every shot-level source fact on the shot (§7). |
| 14 | Migration | The strangler order in §10 (the steps of `04-remediation-plan.md`), with the lint first. |
| 15 | Instruments | The launch monitor is one device among the others. A shot has one set of source-tagged measurements; one rule decides which source wins for a metric; the LM board, Charts and the report are presenters over the session's measurements. No separate LM model. (Mark, draft 2 review.) |
| 16 | The Recording contract | The layering contract between Acquisition and Analysis (§0.1): analysis only ever runs on a Recording, which carries its analysis context. Today's `src/Export` is renamed for what it is (e.g. `src/Recording`). |
| 17 | Studio/Rig | A domain entity owning the rig and its calibrations, versioned and referenced by each Recording. Named now; designed in the Acquisition session. |
| 18 | A corrected club | Re-analysis uses the effective (corrected) club, its length and norms; the captured club stays in the Recording; a correction marks the shot's analysis stale. Same Recording + same effective context → same job (§0.1). |
| 19 | Grading | `SessionDiagnostics` grades every shot of its session and keeps the rows, shot-level facts exposed through the shot; the analysis pipeline never grades; a corridor edit or club correction regrades without re-analysis (§0.3, §5). |
| 20 | Intent runs | A "from here on" run pre-fills each shot as it is taken; each shot can override it; an edited run affects only later shots (§7). |
| 21 | Recorded input | A third kind in P1; the club baseline is one, rewritten only when its format is outdated (§1). |
| 22 | Events, cast, writers | The five new events (§5); the cast shows the session on screen when nothing is captured (§4); every file is written only through its root, the archive and the report as `Session` commands, a swing file once by capture then by its `Session`; the tuning log appended by the athlete level on `LabelCorrected` (§7). |
| 23 | Platform ring | Ring 0 holds logging, profiling and machine queries; every ring may include it; nothing domain-shaped goes in it (§2). |

## 14. Keeping it this way

The audit found two ways the structure decayed:
1. **Nobody had to decide where a thing belonged.** A panel made its own model, and that was copied on purpose.
2. **Nothing checked afterwards.**

The fix has the same two halves that work for the design system.

### 14a. Design phase: the information-model rule (CLAUDE.md, beside the design-system rule)

- **`docs/developer/information_model.md`, written from §1–§9 once agreed, is the source of truth.** It
  plays the role `docs/design/system/README.md` plays for the UI.
- **Every plan that touches data has an "Ownership" section.** It answers, for each new or changed thing:
  1. **Is it a source fact, a recorded input, a derived fact, application state or view state?** (P1, P2)
  2. Which object owns it, and at which level?
  3. Who creates and destroys it, and what identifies it?
  4. If derived: what are its inputs, its rule version, and the events that invalidate it?
  5. Where is it persisted, and by which one writer?
  6. Which layer does each new file go in?
- **HARD, as for the design system:** any change to the information model needs Mark's approval before it is
  built. That covers:
  - a new first-class object or owner;
  - a new persisted file or field family;
  - a new identity key;
  - a new dependency between levels;
  - a new event;
  - anything created by a view.

### 14b. Review and commit phase: the enforcer

| Gate | Checks |
|---|---|
| `architecture_lint_test` (§11) | Mechanical rules, with an allowlist that can only shrink. |
| Reviewer architecture check | Every reviewer agent and `/code-review` answers the six questions for the diff. A "no" is CHANGES NEEDED. |
| Pre-commit hook (optional) | Runs both lints before `git commit`. |
| Per-release re-run of the audit tools | Diffs the include graph, the seam, identities, sizes and duplicates against the last release. |

**Decisions on §14:**
1. Adopt 14a as a HARD rule?
2. Build the lint first, before structural step 1?
3. The pre-commit hook: on or off?
