# Platform refactor, Stage E: the app host, settings, threading and secrets. Six design questions, five more that the items wait on, and two scope levers, classified

**Status: proposed, 24 Nov 2026. Nothing here is ruled.** Each row ends with an empty **Ruled** line: the
coordinator writes the ruling, the role and the date there, and the briefs that wait read it. Until a row is ruled,
today's behaviour stays and the items that wait on it are not built[^hard]. Every recommendation is **(proposed)**
for the role its **Class** line names. Each row also says what happens **if nothing is ruled**.

**Why this page exists.** Stage E of the platform plan gives `main.cpp`'s objects one owner, takes the settings
class out of its header, tidies the long-lived threads and moves the API keys out of the settings file[^plan].
Its item plan (34 items, about 83 hours) is provisional: six questions decide a third of it, and none is
ruled[^plane]. Five more questions gate single items. Two of the forecast's scope levers, 7 and 8, are put here as
proposals with their counts, as the forecast asked[^fore].

**The secrets move stays.** Moving the API keys out of the plain settings file is a security gain, and this page
proposes no cut to it (K1).

**How each row is classified.** By the information model's definition of a change, §1.2 (C1–C11), read through
`tools/method/im_excerpt.py`[^imc]. A row that only makes code match what the information model already says is not
a change (§1.2's own exclusion). A row that adds a file needing a ring, changes a rule or a lint rule's scope, or
changes a persisted store or its one writer, is. Where the model is silent, §1.2's last sentence applies: "When in
doubt, it is a change."

| Class | Meaning | Count | Rows |
|---|---|---|---|
| **HARD: the Solution Architect** | An information-model change, or a question the plan keeps for that role. Nothing that depends on it is built before the ruling | **6** | H1, H2, H3, H4, K1, K5 |
| **HARD: the Design Lead** | A change to what the golfer or coach sees | **1** | K2 |
| **Coordinator's call** | A build-structure choice with no C1–C11 change | **0** | none |
| **Product Owner** | Scope and order | **6** | H5, H6, K3, K4, V7, V8 |

Thirteen rows: the six questions the item plan names (H1–H6), five more that its SE-0 line lists or that the
counts turned up (K1–K5), and the two levers (V7, V8). K5 asks for no change, and nothing waits on it.

**Six places where this page differs from the item plan**, each explained in its row:
1. **There is one design-system row, not none.** The settings page fills each API-key field from the stored key
   and has a Show button. The item plan's own ownership row says a view sees "present or absent, never the key".
   That changes what the coach sees (K2).
2. **Both answers to Q9 need the Solution Architect.** Naming the thread owners as exceptions edits R-CON-5, as the
   pose thread's exception did. The Product Owner's part of lever 8 is only whether to spend the 9.5 hours (H3, V8).
3. **The settings groups have no counted consumer.** 22 product files include `app_settings.h`, not 91; 91 is the
   number of files that name the class. SE-5d's Accept count is already met by SE-5c (V7).
4. **`studioProfile` can leave the globals without a ruling on Q17**: the information model already says it is
   reached through `app` (H5). And `studio_profile.h`'s four A6 findings may stay in scope under P9's wording,
   because the model calls the studio profile an entity that owns source facts (H4).
5. **The Gemini key's move to a request header (SE-7c) does not need the new store.** The key is in a request's
   address at one line of one file, which is handed the key and does not read the store (K1).
6. **Ten places declare a pool of their own or put work on Qt's global pool, and this stage's plan holds none of
   them** (H3).

Two smaller corrections, for the briefs: the updater reads two settings, not one (`checkForUpdates` and
`skippedUpdateVersion`: `grep -n 'm_settings->' src/Update/*.cpp src/Update/*.mm`); and 8 files call
`moveToThread` in code. The three more that the item plan counts (`ble_imu_transport`, `hm_session_worker`,
`STTWorker`) name it only in comments: they are workers that an owner moves, not owners.

**How to read the counts.** Taken on main at `f1885b7c`, each with its command, run from the repository root in
`bash` (the same commands in `zsh` return zeros where a pattern holds a `*`). "Product files" leaves out `tests/`
directories. The briefs re-take every figure at run time.

**Include check, as the method asks before a dependency rule is proposed**[^inc]. H1 proposes one: only the
composition root builds the stores. `main.cpp` has 109 `#include` lines (`grep -c '^#include' src/Gui/main.cpp`),
7 of them a store or repository header in `src/Export`
(`grep -nE '^#include .*(store|repository)' src/Gui/main.cpp`). It is in `src/Gui`, the Adapters ring, which is
the outermost ring, so every one of its includes points inward and lint A1 passes today.

**What can start with no ruling.** The inventories (SE-1a, SE-5a, SE-6a); the quit pins (SE-1b) and the settings
pins (SE-5b); the settings bodies out of the header (SE-5c); the updater's injection (SE-3); the studio profile's
repository (SE-4's first half, after B1.9; its directory is ruled[^q11]; its typed API waits for H4 and its
global's move for H5); the probe's job onto the pool (SE-6e, after SD-7); the one cloud-fallback piece (SE-8); and
the Gemini key in a header (SE-7c). Everything else waits for a row below.

**Built (SE-3).** The updater no longer includes a `Gui` header. It is handed one object, `UpdateHost`
(`src/Update/update_host.h`), that answers whether a session is running and holds its two settings: automatic
checks, read at start-up and followed, and the skipped version, which it only writes. The app's answer is
`AppUpdateHost` (`src/Gui/app/app_update_host.h`), built in `main.cpp` over the one `AppSettings` and
`SessionController`. Lint A1 on the updater's four files: 7 → 0, the four lines removed; no settings key, default or
behaviour changes[^se3-built].

## The six questions

### H1 · The composition root's ring, and where its file goes
- **Today.** `main.cpp` is 2,139 lines (`wc -l src/Gui/main.cpp`), with 100 `connect(` calls and 18 QML globals
  (`grep -c 'connect(' src/Gui/main.cpp`; `grep -n setContextProperty src/Gui/main.cpp src/Gui/app/*.cpp | wc -l`).
  It holds 9 function-local statics, 7 of them services: the session store, the two recording doors and four
  phone-link objects (`grep -nE '^\s+static [A-Za-z_:<>]+ [a-zA-Z_]+' src/Gui/main.cpp`). Six teardown lines
  follow `exec()`, each there because a static or a callback outlives a local of `main()`
  (`sed -n '2131,2139p' src/Gui/main.cpp`). The Windows quit path during the pose warm-up is marked "NOT TESTED"
  (`grep -n 'NOT TESTED' src/Gui/main.cpp`: line 183). The lint allows the file 8 A5 findings, tagged
  `# kept (composition root)` (`grep -E '^Gui/main\.cpp' src/Gui/tests/architecture_allowlist.txt`). That line
  is per file: a new file beside `main.cpp` does not inherit it.
- **Options.**
  (a) One owning object in a new file beside `main.cpp` in `src/Gui`, in the Adapters ring by its directory. Its
  A5 findings move with it, so the allowlist gains a line for the new file and `main.cpp`'s count falls.
  (b) The same file, with the lint taught what a composition root is: a named file that may include the
  repositories and build the stores. A5's repository-include half then passes for it by rule, and only real file
  access is left as findings.
  (c) `src/App`. That is the Application ring, and a composition root includes adapters, so lint A1 fails.
  (d) A seventh ring.
- **Recommendation: (b).** Building the stores is the composition root's whole job, and the model already uses the
  term that way: the studio profile is created "by the composition root at launch" (OBJ-STUDIO-PROFILE), and the
  application services get their objects "through callables the composition root hands them" (R-HL-2)[^imc]. The
  term is on 18 lines of the model (`grep -c 'composition root' governance/artefacts/information_model.md`). A `# kept` allowlist line treats that as debt that is never paid. Under (b)
  what is left on the allowlist is real: the bundled-font listing (`main.cpp:291`) and the folder probe
  (`:1522`), which SE-2c moves or SE-1a places. (a) is the fallback: same file, one more allowlist line. (d) adds
  a ring for one file.
- **What the ruling must also say.** Each tool's `main` is a composition root too; the
  lint does not read the tools today, so (b) names the app's file only. The root's members are declared in the
  order they must be torn down, and it owns `GuiApp` (the `app` global's object) and the phone link's four statics,
  which Stage F builds on.
- **Class: HARD: the Solution Architect.** (a) adds an allowlist line, which needs that role's approval. (b)
  changes lint A5's scope, an information-model change (C4, C9)[^imc]. (d) is C9.
- **If nothing is ruled:** SE-2a is not built, so SE-2b to SE-2d wait, and so does Stage F's ownership of the
  phone link. SE-1a and SE-1b still run.
- **Waits:** SE-2a, then SE-2b, SE-2c, SE-2d.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### H2 · The shape of the settings split
- **Today.** `AppSettings` is 1,958 lines of header and 163 of source, with 123 properties (110 after Stage C) and
  228 direct `ppSettings()` calls, all in the header (`wc -l src/Gui/app/app_settings.*`;
  `grep -c Q_PROPERTY src/Gui/app/app_settings.h`; `grep -c 'ppSettings()' src/Gui/app/app_settings.h`). Its keys
  sit under 13 prefixes (`grep -oE 'QStringLiteral\("[A-Za-z]+/' src/Gui/app/app_settings.h | sort | uniq -c`:
  `ui/` 50, `General/` 37, `camera/` 28, `storage/` 26, `imu/` 22, `display/` 22, `launchmonitor/` 20, `view/` 15,
  five more with 2 each).
  22 product files include the header: 18 in `Gui` and 4 in `Update`
  (`grep -rlE '#include ["<].*app_settings\.h' src --include='*.h' --include='*.cpp' --include='*.mm' | grep -v /tests/ | wc -l`).
  91 product files name the class (the same command with `'\bAppSettings\b'`). QML uses 113 of its names 550
  times in 70 files; 420 of those uses, in 48 files, are outside `tests`
  (`grep -rnoE '\bappSettings\.[A-Za-z_]+' src --include='*.qml' | wc -l`), and 197 are writes
  (`grep -rnE '\bappSettings\.[A-Za-z_]+ *= ' src --include='*.qml' | wc -l`).
  Five entries of IM §8.2 are domain facts that `AppSettings` carries: SET-05 and SET-09 (rig facts, which Stage C moves to
  the Rig), SET-06 and SET-15 (already a view over `RuleData`), and SET-17 (moving to the athlete record).
- **Options.**
  (a) Groups as C++ types behind today's `AppSettings`, backed by one injected settings object; C++ consumers take a
  group; QML is untouched (SE-5d, SE-5e.1, SE-5e.2).
  (b) One class, its bodies in the `.cpp`, no groups (lever 7: V7).
  (c) Groups all the way to QML (adds SE-5h; see H5).
- **Recommendation: (b), through V7.** If the Product Owner keeps the groups, then (a), with two points for this
  role. First, a group that a file outside `Gui` takes (the updater's four files today) needs a home an inner ring
  may include, or the A1 findings only change their header's name: that home is a ring placement (C9). Second, no
  group may become the writer of a domain-fact key: SET-06 and SET-15 keep `RuleData` as their writer, and SET-05,
  SET-09 and SET-17 leave settings by the plans that already hold them.
- **Class: HARD: the Solution Architect**, for (a) and (c): a new header's ring (C9), and C3 or C4 if a
  domain-fact key changes its writer. **(b) is not a change**: no file moves ring, no key moves, no writer changes.
- **If nothing is ruled:** (b) is what gets built, because SE-5c needs no ruling and the group items wait.
- **Waits:** SE-5d, SE-5e.1, SE-5e.2. Not SE-5a, SE-5b or SE-5c.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### H3 · Q9: R-CON-5 and the long-lived threads
- **Today.** R-CON-5 reads "One threading idiom: a single worker pool", with one exception, the pose inference
  thread. It forbids "a new pool, thread or idiom"[^imc]. The code holds more than that:
  - **Long-lived threads.** 8 product files call `moveToThread` in code, 29 times
    (`grep -rn moveToThread src --include='*.h' --include='*.cpp' --include='*.mm' | grep -v /tests/ | grep -vE '//.*moveToThread'`).
    `new QThread` is written at 12 places in 6 files, for 10 threads (the camera's capture thread and the speech
    worker's thread are each made on two code paths), and `ImuManager` holds an eleventh as a member
    (`grep -rnE 'new QThread(\(|;)' src --include='*.cpp' | grep -v /tests/`). So: the camera 4, transcription 2,
    and one each for speech out, the language model, speech in, the sensor scan and the sensors' I/O.
    `ImuInstance` and `HmInstance` borrow the manager's thread and stop none. 7 files hold a hand-written
    teardown, 34 `quit`, `wait` or interrupt lines in all
    (`grep -rnE '[tT]hread[A-Za-z_]*(\.|->)(quit|wait|requestInterruption|terminate)\(' src --include='*.cpp' | grep -v /tests/ | cut -d: -f1 | sort | uniq -c`).
    The plan counts 8 owners; by teardown it is 7 files. SE-6a settles the list.
  - **Other `std::thread`s.** 27 product lines name `std::thread`
    (`grep -rn 'std::thread' src --include='*.h' --include='*.cpp' --include='*.mm' | grep -v /tests/ | wc -l`).
    They are not classed here as long-lived or finite; SE-6a classes them. The eleven above are `QThread`s only.
  - **Work off the one pool.** Six private pools, each used, and four jobs on Qt's global pool
    (`grep -rnE 'new QThreadPool|QThreadPool +[a-zA-Z_]+;|QThreadPool::globalInstance\(\)' src --include='*.h' --include='*.cpp' | grep -v /tests/ | grep -vE ':\s*//'`):
    pools in `session_diagnostics_model.h` (2), `work_ons_controller.h`, `session_list_presenter.h`,
    `session_diagnostics_files.h` and `report_stills.cpp`; jobs in `swing_rig_driver.cpp` (2),
    `athlete_diagnostics.cpp` and `trunk_ceremony_recorder.cpp`. Stage C's C-14 holds the last. This stage's plan
    holds none of the ten; its one finite job is the motion-capture probe's `std::thread` (SE-6e). Not checked
    here: which plan, if any, holds the other nine. That is for the coordinator to place before this stage closes.
  - **No observed fault.** No open issue's title names a thread teardown. The exit crash of August is fixed. No
    test quits the app at any point today; SE-1b writes the first.
- **Options.**
  (a) Amend R-CON-5 to one rule with two tested forms: the pure C++ serial executor (the pose thread's shape,
  promoted to Platform when a second caller needs it) and a Qt event-loop owner for the hardware and media threads.
  Build the second and move every owner onto it (SE-6b, SE-6c, SE-6d).
  (b) Amend R-CON-5 to name a class of exception: a long-lived thread that needs an event loop is owned by its
  device or media owner, which stops it before it is destroyed; the model lists them from SE-6a's inventory,
  which also says whether any long-lived `std::thread` belongs on the list. No helper is built.
  (c) As (a), for the media owners only (SE-6b, SE-6c); the device owners are named as in (b).
  (d) Leave R-CON-5 as it is. The rule then keeps saying "one idiom" over eleven threads it does not describe.
- **Recommendation: (b), with V8 taken.** The rule should describe the code: these threads pre-date it and need an
  event loop the pool cannot give. Whether a shared helper is worth building is a separate question, and the
  counts do not make its case yet: no fault is observed, no lint line or issue closes on it, and its device half
  rewrites the teardown of the camera's four threads, which can only be proved at the cabin. The quit pins are the
  evidence that is missing. If a pin or the cabin's quit run shows a teardown fault, (c) or (a) comes back as the
  fix, with the fault as its test. (c) is the fallback if this role wants the helper to exist before the rule
  names it: its five threads can be tested on the Mac.
- **Class: HARD: the Solution Architect** (C11: R-CON-5 is amended under (a), (b) and (c) alike). SE-6e and the
  pools above make code match R-CON-5 as it stands and are not a change.
- **If nothing is ruled:** (d). SE-6b to SE-6d are not built; the Accept row's "hand-written teardowns 8 → 0" is
  not met; the audit's F-09 remainder stays open and must be recorded as an agreed deferral[^sched].
- **Waits:** SE-6b, SE-6c, SE-6d. Not SE-6a, SE-6e or SE-6f.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### H4 · Q5: lint A6 back to P9's wording
- **Today.** The same question as the Stage D pack's row V2, which is already before the Solution Architect[^sd0].
  One ruling answers both. Stage E's share of the 31 A6 allowlist lines
  (`grep -E '\| A6 \|' src/Gui/tests/architecture_allowlist.txt`):
  - the monitoring and media headers, 15 findings in 5: `profiler_controller.h` 8, `resource_monitor_controller.h`
    4, `llm_controller.h` 1, `transcription_controller.h` 1, `LlmWorker.h` 1 (SE-9a);
  - 42 more in 4 headers that no item holds: `app_settings.h` 26, `markup_controller.h` 11, `studio_profile.h` 4,
    `archive_controller.h` 1.
- **Options.** (a) Narrow A6 to P9's wording, "a presenter's or a domain type's public API". (b) Keep it on every
  header.
- **Recommendation: (a)**, as in V2. Under it SE-9a is dropped and the 42 never become items.
- **One line for the ruling to settle: `studio_profile.h`.** The plan lists it with the settings and tool objects
  that fall out of scope. But the model defines the studio profile as an entity that owns source facts (the coach's
  entries; OBJ-STUDIO-PROFILE). That reads as a domain type, so its 4 findings (the practice steps, the missing
  details, and `fill`'s map: `grep -n 'QVariant\(Map\|List\)' src/Core/studio_profile.h`, 9 lines, which the
  lint counts as 4) would stay in scope and
  be typed with its repository in SE-4. Proposed: they stay in scope. The cost is not measured; about 1.5 hours is
  a guess.
- **Class: HARD: the Solution Architect.** A change to a lint rule's scope is an information-model change (C4,
  C9)[^imc].
- **If nothing is ruled:** A6 stays wide. SE-9a is built (3 h), and the 42 findings need three more items, about
  9 hours, which the 83 hours do not hold.
- **Waits:** SE-9a; the three items that (b) would add; the scope of SE-4.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].
- **Built (SE-4):** `studio_profile.h`'s 4 findings are typed: the practice steps and the missing details are lists
  of two gadgets (`StudioPracticeStep`, `StudioMissingDetail`), QML sets the steps as two lists of text, and the
  template fill, which no QML calls, is C++ only. Its A6 line is gone[^se4-built].

### H5 · Q17: no QML global is renamed for the count
- **Today.** The same question as the Stage D pack's row V1, before the Product Owner[^sd0]. Stage E's globals
  (for each name `g`: `grep -rnoE '\bg\.[A-Za-z_]+' src --include='*.qml' | wc -l`, and `-l` for files):

  | Global | Uses | QML files | Item that would move it |
  |---|---|---|---|
  | `appSettings` | 550 (420 outside `tests`) | 70 (48) | SE-5h |
  | `studioProfile` | 62 | 2 | SE-5h |
  | `secrets` | 10 | 1 | SE-5h (its API changes under K2 in any case) |
  | `ttsController` | 66 | 4 | SE-9b |
  | `profiler` | 55 | 1 | SE-9b |
  | `resourceMonitor` | 30 | 3 | SE-9b |
  | `appLog` | 16 | 9 | SE-9b |
  | `cudaRuntime` | 7 | 1 | SE-9b |
  | `controller` (transcription) | at most 139 | at most 17 | SE-9b; the name is generic, so the pattern over-counts |

  60 `tst_*.qml` files exist (`find src -name 'tst_*.qml' | wc -l`); 22 QML files under `tests` use `appSettings`.
- **Proposal.** A global leaves only when its object gets a new owner, never to lower the count. `appSettings`
  stays one object for QML whatever H2 and V7 decide. SE-5h and SE-9b are dropped. **One global does leave:
  `studioProfile`**, so the Accept count is 18 → 17.
- **Why that one.** The model already says the studio profile is reached "through `app`", and names today's
  `studioProfile` global as the interim (OBJ-STUDIO-PROFILE)[^imc]. SE-4 gives the profile its repository and
  its ruled directory, so the 62 uses in 2 files move to `app` with it, by hand, with their `tst_*.qml` run. That
  makes code match the page, so it is no information-model change; the count is the Product Owner's, with the
  rest of this row.
- **Reason.** As V1: a renamed global moves a name, not an owner, and wrapping them would itself be an
  information-model change (C1) with no gain. The settings object alone would be 550 scripted edits.
- **Hours.** Stage E: **6.5** (SE-5h 3.5, SE-9b 3), less about half an hour for the studio profile's move.
- **Design system.** No row here changes a look: a moved global re-points a name. If the Product Owner keeps
  SE-5h and the split then reorders or regroups a Settings page, that is a change to what the coach sees and stops
  for the Design Lead[^ds].
- **Class: Product Owner** (an Accept count and two items' scope). One point stays with the Solution Architect and
  nothing waits on it: R-QML-1 allows "devices, settings, logging and `app`" as globals, and the two media
  controllers and the three monitoring objects are none of those by name.
- **If nothing is ruled:** SE-5h and SE-9b are not built, the studio profile's global stays, and the count
  stays at 18.
- **Waits:** SE-5h, SE-9b.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].
- **Built (SE-4):** the studio profile has its repository (`src/App/studio_profile_repository.h`, over the one store
  `src/Stores/studio_profile_store.*`) and QML reaches it as `app.studioProfile`: the globals are 17. The 62 uses
  were 60 in `StudioPanel.qml` and 2 in a comment in `PpTextArea.qml`; the count's pattern missed the two bare
  reads, `StudioPanel.qml`'s guard and `PpSessionReportList.qml`'s default, which moved too[^se4-built].

### H6 · Q6: #193, the app's own trash always
- **Today.** A discard goes to the operating system's trash first, and to the library's own trash only where the
  volume has none. Two code lines make that choice (`grep -rn 'QFile::moveToTrash(' src --include='*.cpp' | grep -v /tests/`:
  `swing_paths.cpp:201`, `athlete_store.cpp:461`). The model records it as today's behaviour, kept on purpose, with
  the change "to be filed as a post-refactor issue" (FILE-LIB-01, DEC-175)[^imc]. #193 is that issue. Windows can
  empty a full Recycle Bin by itself, which is a permanent delete outside the app. Two open test defects touch
  the same choice: #203 (probes send scratch sessions to the real macOS Trash) and #250.
- **Options.** (a) Out of this stage: #193 stays its own issue, taken after the refactor. (b) In this stage, as
  SE-10 (2.5 h).
- **Recommendation: (a)** (the forecast's lever 5). It is new behaviour, the refactor's freeze holds new
  behaviour, and no other item of this stage depends on it. It is worth doing soon after: it removes a silent
  permanent delete, and it would close #203's cause.
- **Class: Product Owner**, for when. **If it is built, in this stage or later, it is also HARD twice over:** for
  the Solution Architect, because it reverses DEC-175 and changes what FILE-LIB-01 says (C3); and for the Design
  Lead, because the settings page must then say that the app's trash is the one place to restore from, and
  Windows' own Restore no longer applies[^ds].
- **If nothing is ruled:** (a). Today's behaviour stays.
- **Waits:** SE-10.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

## Five more questions the items wait on

### K1 · The API keys' store, and its home
- **The secrets move is a security gain, and it stays in the plan.** The forecast lists it as lever 12 and says
  not to take it[^fore]. This page agrees, for three reasons that can each be checked today:
  1. The Azure and Gemini keys sit unencrypted in one plain-text settings file on every platform. The code says so
     itself (`sed -n '24,38p' src/Secrets/SecretsManager.h`).
  2. The Gemini key is sent in the request's address, where proxies and logs record it
     (`grep -n 'key=' src/LLM/GeminiLlmEngine.cpp`: line 68). The other two services already use a header.
  3. A key can be baked into a build at compile time and is then written to the settings file on first run
     (`grep -n ASSEMBLYAI_API_KEY CMakeLists.txt`; `SecretsManager.cpp:60–71`).
- **Today.** `src/Secrets` is 229 lines in four files (`wc -l src/Secrets/*`). Keys are read at 14 places in 4
  product files (`grep -rnE 'SecretsManager::read' src --include='*.cpp' | grep -v /tests/ | grep -v src/Secrets/`),
  some on every request. An environment variable overrides the stored key, and at start-up three environment
  values are copied into the settings file. No test names the class
  (`grep -rlE 'SecretsManager|SecretsBridge' src --include='*.cpp' --include='*.h' | grep /tests/ | wc -l`: 0).
  The coach's mail password is already in the system's store: 467 lines in five files, one per platform, with an
  in-memory mode for tests (`wc -l src/Gui/report/mail_credentials*`). On Linux that "store" is the settings
  file, and the file says so. The information model names neither the keychain nor the keys
  (`grep -c keychain governance/artefacts/information_model.md`: 0).
- **Options.**
  (a) One credential store in `src/Secrets/` (Adapters, as that directory is today), generalised from
  `mail_credentials_*`: a service and an account in, a secret out. The mail password becomes its first client and
  the API keys its second.
  (b) Leave the mail files where they are and write a second copy for the API keys.
  (c) Keep the keys in the settings file (lever 12).
- **Recommendation: (a).** One store, one test mode, one place where the three platforms differ. (b) doubles the
  platform code that is hardest to test. (c) gives up the gain.
- **What the ruling must also say.**
  - **The environment override.** Proposed: a developer's environment variable is still read first, but nothing is
    copied from the environment or from the build into any store. The compile-time default goes.
  - **The copy.** Each key is copied once, read back, and only then removed from the settings file (SE-7b). A
    machine put back on an older build after that finds no key, and the coach types it again.
  - **Linux.** It keeps today's settings-file fallback, as the mail password does, and the settings page says so.
  - **Reads.** The store reads each key once and holds it in memory, so a request never waits on the keychain.
- **SE-7c need not wait for the store.** The only `key=` in a request's address is line 68 of
  `GeminiLlmEngine.cpp` (`grep -rn 'key=' src --include='*.cpp' | grep -v /tests/`; the two other hits are
  comments in the phone link). That file is handed the key by its caller and does not read the store, so moving
  the key to a header closes reason 2 with no ruling. The item's other half, checking the model id, is not sized
  here.
- **Class: HARD: the Solution Architect.** The model is silent, so "when in doubt" applies: a new persisted store
  with one writer (C3, C4), an owner of application state the model does not list (C1), and five files moving from
  `Gui/report` to `src/Secrets` (the ring is unchanged, Adapters, but the model names both directories).
- **If nothing is ruled:** SE-7a and SE-7b are not built and the keys stay in the settings file. SE-7c is still
  done.
- **Waits:** SE-7a, then SE-7b. The guard for both: no test or agent run touches the real keychain; the in-memory
  mode is in place first.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### K2 · The API-key field on the settings page
- **Today.** Settings → General has two key fields, for Azure and Gemini
  (`grep -n 'secretName:' src/Gui/settings/GeneralPanel.qml`). Each is filled from the stored key, masked, with a
  Show button that reveals it (`sed -n '82,121p' src/Gui/settings/GeneralPanel.qml`: `secrets.read()` at line 108).
  So the key crosses into QML whenever the page opens. The mail password does the opposite: once saved it is never
  shown again, and the row reads "Saved in Keychain" with a "Change" link
  (`sed -n '688,745p' src/Gui/settings/StudioPanel.qml`).
- **Options.**
  (a) The field stays as it is and reads the key through the new store. Nothing the coach sees changes; the key
  still reaches QML.
  (b) The mail password's pattern: "Saved in <store>" and "Change", and the key is never shown again. The Show
  button goes. It is built from parts the page already uses (`PpTextField`, `PpLink`, the page's own row and
  title), so no new component, but the row's behaviour changes. A "Remove" action is needed too, which the mail
  row has no equivalent of on the same line.
  (c) As (b), with the pattern made one shared component, because two panels would then compose it by hand.
- **Recommendation: (b).** A secret that is stored safely and then handed back to the page on request is only half
  moved. (b) also makes the two kinds of secret on the settings pages behave alike. (c) is the tidier end, but it
  is a new component and belongs in a design-system proposal of its own.
- **Class: HARD: the Design Lead.** (b) and (c) change what the coach sees and how a control behaves; (c) adds a
  component[^ds]. The proposal that goes to that role needs a before and after picture of the row in its three
  states (empty, saved, changing) and on Linux, where the wording must say the key is in the settings file. That
  picture is not drawn here.
- **If nothing is ruled:** (a). SE-7b moves the keys and leaves the field's look alone.
- **Waits:** the QML half of SE-7b only. SE-7a and the copy do not wait.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### K3 · AssemblyAI: delete it, or move its key
- **Today.** `STTBackendAssemblyAI` is 296 lines in two files (`wc -l src/STT/*AssemblyAI*`). Nothing uses it:
  the name appears in its own two files, in a comment in `STTBackend.h`, and in the build file, which calls it
  "kept for reference, not used as fallback" (`grep -rl AssemblyAI src CMakeLists.txt`;
  `grep -n -i assemblyai CMakeLists.txt`). Its key is the only one with a compile-time default. It has no field on
  the settings page.
- **Options.** (a) Delete the back-end, its build lines and its key. (b) Keep it and move its key with the others.
- **Recommendation: (a).** It is unused, git keeps it, and deleting it removes the last reason for a key to be
  baked into a build.
- **Class: Product Owner** (whether a feature kept "for reference" stays). No C1–C11: the key is not in the model.
- **If nothing is ruled:** (b): one more key in SE-7b. The compile-time default goes in either case (K1).
- **Waits:** SE-7d.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### K4 · Q24: #234 in this stage's first part
- **Today.** The report list and the Archiving tab are told which session is live by `main.cpp`, from the review
  controller, in one block of four lines (`grep -n 'liveSessionDir\|setLiveSessionDir' src/Gui/main.cpp`:
  lines 920 to 925; a fifth hit, line 1133, tells `AppState` itself when the review controller's live session
  changes, and SE-1a's inventory says whether it goes too). The model gives that fact to `AppState`. The block's own comment says "live" here means a
  session is running, not merely that one is on screen.
- **Options.** (a) In SE-2d, as the composition root rewrites those lines anyway. (b) A small item of its own,
  later.
- **Recommendation: (a).** The brief keeps the meaning "running", and checks first that `AppState` holds that
  fact and not only the session on screen.
- **Class: Product Owner** (which stage a carried-in issue goes to). The edit makes code match the model and is
  not a change.
- **If nothing is ruled:** SE-2d is not built and #234 stays open on its issue.
- **Waits:** SE-2d (and H1, through SE-2a).
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### K5 · Q7: the phone link's pairing key stays in settings
- **Today.** The pairing key is in the settings file on purpose. The reasons are written where it is stored
  (`sed -n '19,70p' src/Ppcp/ppcp_pairing_store.cpp`): in a keychain, Windows and Linux could keep no pairing at
  all, and macOS asked for permission on every relink. The cost, a key readable by anything running as the user,
  was weighed and accepted there. The model lists `ppcp/pairings` with the rig facts in settings (SET-09).
- **Options.** (a) Leave it. (b) Move it to K1's store.
- **Recommendation: (a)**, as the plan recommends[^plan]. K1's store does not remove either of the two reasons.
- **Class: the Solution Architect's question; HARD only if it moves** (C3: SET-09's key). As recommended, nothing
  changes and nothing is built.
- **If nothing is ruled:** (a).
- **Waits:** nothing.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

## The two levers, as proposals

### V7 · Settings: one class, its bodies out of the header, no groups
- **Today.** The counts are in H2. Four more decide this row.
  1. **SE-5d's Accept count is met without it.** "Direct `ppSettings()` calls in the header 228 → 0" is what
     SE-5c does by moving the bodies to the `.cpp`.
  2. **SE-5e.2's Accept count measures the wrong thing.** "Includers 91 → the façade's": 91 is the files that name
     the class; 22 include the header.
  3. **Tests already keep off the real store** without an injected object: 10 test files point `QSettings` at a
     scratch folder, 4 of which name `AppSettings`
     (`grep -rlE 'QSettings::setPath' src --include='*.cpp' | grep /tests/ | wc -l`).
  4. **The 24 local fallbacks are Stage D's**, and go whatever is decided here[^plane].
- **Proposal.** Take the lever. Build the inventory (SE-5a), the pins (SE-5b) and the bodies out of the header
  (SE-5c). Drop SE-5d, SE-5e.1 and SE-5e.2. SE-5f (the Rig's copied keys leave) and SE-5g (the Archiving tab's
  three reads) are re-based to follow SE-5c. SE-5h goes with H5.
- **Reason.** No Accept count, lint line or issue names groups. The costs the plan gave for the split are the
  header's size and the whole-app rebuild on every edit. SE-5c removes the first and most of the second: a
  changed body no longer rebuilds the app, though a new property still rebuilds the 22 includers. What is left of
  the case is narrower dependencies for C++ consumers, with no consumer waiting for one. The split is also the item of this
  stage most able to lose a setting. Nothing is closed off: groups can be cut behind the same class later, and
  the inventory and the pins are what that would need first.
- **What is given up.** `AppSettings` stays one object of 110 properties, and any file that holds it can read any
  of them. Lint A8 already keeps settings out of the analysis code.
- **Hours.** **9.5** (SE-5d 3.5, SE-5e.1 3, SE-5e.2 3). It does not add to H5's 6.5 one for one: with no groups,
  SE-5h has nothing to show QML and is moot.
- **Class: Product Owner** (three items' scope; no count in the Accept row changes). Taking it removes H2's
  question; not taking it leaves H2 for the Solution Architect.
- **If nothing is ruled:** the same as taking it, for as long as H2 is unruled.
- **Waits:** SE-5d, SE-5e.1, SE-5e.2; the order of SE-5f and SE-5g.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

### V8 · Threads: name the owners, build no helper yet
- **Today.** The counts are in H3: eleven long-lived threads, a hand-written teardown in 7 files, 34 lines.
- **Proposal.** Take the lever, paired with H3's option (b). Build the inventory (SE-6a), the probe's job on the
  pool (SE-6e) and #196's pool size (SE-6f). Drop SE-6b, SE-6c and SE-6d. The quit pins (SE-1b) and the cabin's
  quit run then test every teardown as it stands. A fault they find brings the helper back as its fix, for the
  owner at fault first.
- **Reason.** In H3: no observed fault, nothing closes on it, and the device half is the riskiest edit in the
  stage and can be proved only at the cabin. It also removes one of this stage's three parked items (SE-6d), so
  the second hands-on quit run follows SE-2b alone, and SE-6d no longer waits on Stage D's camera tidying, which
  the forecast's lever 9 may itself drop.
- **What is given up.** The Accept row's "hand-written teardowns 8 → 0" is not met and should be re-set to "each
  listed in the model, each covered by a quit pin". The audit's F-09 remainder stays open; it is recorded as an
  agreed deferral with its trigger (a failed quit pin), not left unplaced[^sched].
- **Hours.** **9.5** (SE-6b 3, SE-6c 3, SE-6d 3.5). The middle course, H3's (c), saves 3.5.
- **Class: Product Owner**, for the hours and the Accept count. **The words that name the owners in R-CON-5 are
  the Solution Architect's (H3, C11)**: the lever cannot be taken on this row alone.
- **If nothing is ruled:** the three items wait on H3 in any case.
- **Waits:** SE-6b, SE-6c, SE-6d.
- **Ruled:** as recommended (the Product Owner and the Solution Architect, 25 Nov 2026)[^e-ruled-25nov].

## What the design must also answer

**The guards.** No test or agent run reads or writes the real settings store or the real keychain: the scratch
settings helper and the credential store's in-memory mode are in place before SE-5b and SE-7a. An item that moves
a QML name runs the `tst_*.qml` files that cover it[^plan]. No item launches the app unattended except through the
headless probe.

**The quit runs.** The "before" run on the cabin PC is by hand, six steps, at visit 1; the Windows path has never
been run[^plane]. Under V8 the "after" run follows SE-2b only.

**The design system.** One row, K2. H6 becomes a second if #193 is ever built. Every other QML edit in this stage
re-points a name with no change of look. An item that finds it needs a new component, variant or state, or that
would reorder a Settings page, stops and asks the Design Lead[^hard].

**Already ruled, so not asked here.** The studio profile's directory is `src/App/`[^q11]. `pp_settings` is
Platform[^r3]. The 24 settings fallbacks are deleted in Stage D[^plane]. The Rig's 13 copied settings keys leave
in this stage, after Stage C's close, as the Rig design sets out[^rig].

**The walkthrough deck.** If H1, H3 or K1 is ruled as recommended, check
`docs/developer/information_model_walkthrough.pptx` when the items land: it may no longer show the composition
root, the threading rule or where the API keys are kept as built. The Product Owner regenerates it.

## Ownership (IM §10), as proposed

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived | 5. Persisted where; the one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| The composition root (H1) | none: it holds no fact; it owns the application's objects | itself; application | `main()`; ends after `exec()` returns, members in reverse order | n/a | none | Adapters, beside `main.cpp`; named to the lint (H1) | never: it owns `app` |
| The settings (SET-03, SET-04, SET-07, SET-08, SET-10 to SET-14, SET-16, SET-18, SET-19) (H2, V7) | application state | `AppSettings`; application | the root | n/a | the settings file; `AppSettings` | no new file under V7 | `appSettings` (unchanged, H5) |
| The settings that are domain facts (SET-05, SET-06, SET-09, SET-15, SET-17) | as IM §8.2 gives each | as IM §8.2 gives each; unchanged here | unchanged | n/a | unchanged here; SET-05 and SET-09 leave in Stage C, then their copied keys in SE-5f | none | unchanged |
| The API keys (K1) | application state (a secret) | the credential store; application | the coach, on the settings page; identified by service and account | n/a | the system's secure store (the settings file on Linux); the store | Adapters (`src/Secrets`) | `secrets`: present or absent (K2) |
| The mail password (K1) | application state (a secret) | the same store; application | the coach | n/a | the system's secure store; the store, for the report's mailer | Adapters (moved from `Gui/report`) | `app.reportMailer`, as today |
| The studio profile (OBJ-STUDIO-PROFILE, SET-20, FILE-APP-06) | source (the coach's entries) | `StudioProfile`; application | the root; one per machine | n/a | its keys and its folder; its repository (SE-4) | Application; the repository Adapters | through `app` (H5) |
| A long-lived thread (R-CON-5, DEC-194) (H3) | none: machinery | its device or media owner | the owner; stopped before the owner is destroyed | n/a | none | no new file under V8 | never |
| The library's trash (FILE-LIB-01, DEC-175) (H6) | unchanged as recommended | unchanged | unchanged | n/a | unchanged | none | unchanged |

Information-model change? **Yes**, if ruled as recommended: C4 and C9 (H1, H4), C11 (H3), C1, C3 and C4 (K1).
None under H2 with V7 taken, H5, H6, K3, K4 or K5. §11 is updated by SE-11b.

## What waits on what

| Row | Class | Items that wait |
|---|---|---|
| H1 | **HARD: the Solution Architect** | SE-2a, then SE-2b, SE-2c, SE-2d; Stage F's ownership of the phone link |
| H2 | **HARD: the Solution Architect** (not asked if V7 is taken) | SE-5d, SE-5e.1, SE-5e.2 |
| H3 | **HARD: the Solution Architect** | SE-6b, SE-6c, SE-6d |
| H4 | **HARD: the Solution Architect** | SE-9a; three items if A6 stays wide; SE-4's scope |
| H5 | Product Owner | SE-5h, SE-9b; the studio profile's global, in SE-4 |
| H6 | Product Owner (and both HARD roles, if it is built) | SE-10 |
| K1 | **HARD: the Solution Architect** | SE-7a, SE-7b |
| K2 | **HARD: the Design Lead** | the QML half of SE-7b |
| K3 | Product Owner | SE-7d |
| K4 | Product Owner | SE-2d |
| K5 | the Solution Architect's question; no change proposed | nothing |
| V7 | Product Owner | SE-5d, SE-5e.1, SE-5e.2 |
| V8 | Product Owner (with H3) | SE-6b, SE-6c, SE-6d |

Not waiting on any row: SE-1a, SE-1b, SE-3, SE-4's repository, SE-5a, SE-5b, SE-5c, SE-5f, SE-5g, SE-6a, SE-6e, SE-6f, SE-7c,
SE-8, SE-11a, SE-11b. Each still waits for the items the plan puts before it.

**Effect on the item plan, if every row is ruled as recommended.** Ten items dropped: SE-5d, SE-5e.1, SE-5e.2
(V7, 9.5 h); SE-6b, SE-6c, SE-6d (V8, 9.5 h); SE-9a (H4, 3 h); SE-5h and SE-9b (H5, 6.5 h); SE-10 (H6, 2.5 h).
About 83 h becomes about **52 h**, for 24 items, the figure the item plan gives for all of its levers together.
Two small additions are not in that figure: the studio profile's API typed and its global moved, in SE-4 (H4, H5:
about 2 h, a guess). Parked items fall from three to two (SE-2b and SE-7b). The larger effect is on risk: the two
chains dropped are the ones that edit the most files for no count that anything else depends on.

[^hard]: `CLAUDE.md`: any information-model change needs the Solution Architect's approval, and any design-system change the Design Lead's, before it is built.
[^plan]: `docs/implementation/platform_refactor_assessment.md`, "Stage E · App host, settings, threading and secrets": its bullets marked "(proposed, for the Solution Architect)" and "(for the Product Owner)", its Closes, Test, Accept and Guards rows, §4 (the June items) and §9 (Q5, Q6, Q7, Q9, Q17, Q24).
[^plane]: `build/platform/DEF/PLAN-E.md`, 24 Nov 2026: the items SE-0 to SE-11b, their order and hours, its six corrections, and notes N4 (the by-hand quit run) and N6 (Linux).
[^fore]: `build/platform/DEF/FORECAST.md`, 24 Nov 2026: levers 1, 2, 5, 7, 8 and 12, and "Decide levers 7–9 when SE-0 and SD-0 are put to you, with their counts".
[^imc]: `governance/artefacts/information_model.md` §1.2 (C1–C11), §1.4 (the lint rules; its last line makes a change of a rule's scope an information-model change: DEC-120), §8.2 (SET-01 to SET-20), §9.1 (R-CON-5), P9, P11, R-QML-1, R-HL-2, DEC-175, DEC-193, DEC-194, FILE-LIB-01, FILE-APP-06 and OBJ-STUDIO-PROFILE, read as excerpts.
[^inc]: The method's rule: check the include graph before proposing a dependency rule.
[^sd0]: `docs/implementation/platform_d_capture_seam.md`: V1 (Q17) and V2 (Q5).
[^q11]: The plan's "Rulings of 24 Nov": the studio profile goes to `src/App/`, and Stage E gives it its repository (Q11; the Solution Architect).
[^r3]: `docs/implementation/platform_b1_placements.md`, R3: `pp_settings` is Platform, with lint A8's name check widened (the Solution Architect, 24 Nov 2026).
[^rig]: `docs/design/rig_design.md`: `AppSettings` loses 13 properties in Stage C, and their copied keys are removed by Stage E's settings work.
[^ds]: `governance/artefacts/design_system/README.md`, "The non-negotiables": any change to the design system needs the Design Lead's approval before it is built. This page also sends to that role any row that changes what the golfer or coach sees.
[^sched]: The method's rule: a plan places every audit finding, as a step, an agreed deferral or a rejection. F-09 is `docs/implementation/swe-audit/02-findings.md`: "Five threading idioms".

[^se3-built]: Built in SE-3, 26 Nov 2026 (`build/platform/E/NOTES-SE-3.md`): the rule is lint A1's widening (the Solution Architect, 15 Nov 2026: DEC-146), and SE-3 needed no ruling (above). The information model's §11 row for A1's widening is updated (a status edit).
[^e-ruled-25nov]: Ruled in the platform programme's coordination session, 25 Nov 2026: every open question of this pack as recommended, levers 7 (settings: one class, bodies out of the header, no groups) and 8 (threads: name the owners, build no helper yet) taken. **Condition on lever 8 (Product Owner):** Stage E's first items include quit tests aimed at the lifecycle model's GAP-20 (a quit that waits with no window) and GAP-21 (a quit that touches destroyed objects or kills a thread), quitting while each long-lived thread is busy; a fault they find brings the helper back as its fix, for the owner at fault first. H4 and H5 were ruled with Stage D (Q5, Q17). K2's new behaviour (the key never shown again; Change and Remove) is the Design Lead's approval.
[^se4-built]: Built in SE-4, 26 Nov 2026 (`build/platform/E/NOTES-SE-4.md`): lint A5 on `studio_profile.cpp` 9 → 0 and A6 on `studio_profile.h` 4 → 0 (both allowlist lines removed); no settings key, file or format changes (`report_studio_test`, `persistedBytesUnchanged`). The information model's §11 rows are updated (version 1.12.19).
