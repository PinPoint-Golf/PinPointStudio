# Controls: 3. Build

Writing the code, the screens and the documents. An area reference: read it when doing this kind of work ([`AGENTS.md`](../AGENTS.md) has the index). Older text names four roles (Product Owner, Solution Architect, Design Lead, Coaching Lead); all four are the owner, so read a role's approval as "agree it with the owner first".

## What to read before you touch it

- **Any QML:** `governance/artefacts/design_system/README.md` (the rule book), then the page in `governance/artefacts/design_system/components/` for each `Pp*` component you use.
- **Anything that holds data** (a model, a file, a cache, an id, a controller, a QML property that carries domain facts): `governance/artefacts/information_model.md` §1.1, §1.2, §10 and the sections for the ids the work touches. Find them with grep, or print them with `python3 tools/method/im_excerpt.py ID …` (agents: [AG-32](build.md)).
- **C++ structure** (a new file, a directory, an include): `information_model.md` §4 (the rings) and §1.4 (the lint rules)[^im-rings].
- **Norms:** `governance/artefacts/normative_reference.md`.

## Already stated elsewhere

- How the app log works, and which calls reach it (`docs/developer/resource_profiler_developer_guide.md`, "The application log").
- A target that needs `ppWarn` links the real log stream, never a stub that discards lines and never a second channel (`docs/developer/testing_developer_guide.md`, "Conventions and gotchas").
- Swing documents are read and written only through the swing store (`SwingStore` in C++, `tools/pp_swingdoc.py` in Python) (`docs/implementation/swing_storage_impl.md`).
- A test that constructs settings redirects the per-user stores first (`docs/developer/testing_developer_guide.md`, "Per-user stores"). *Check:* `store_isolation_lint_test`.

## Controls

**AG-47.** Every new design document must carry a table of where it meets today's product rules: each rule it meets, what changes, a way through, and which role decides.
*Why:* a design note met five HARD rules; set out in the document itself, each clash went to the role that decides it instead of surfacing during the build.[^ag-47]

**AG-32.** An agent must never read the information model whole (about 100k tokens): it must read the excerpt its work needs, printed with `python3 tools/method/im_excerpt.py ID …`.
*Why:* reading it whole spent the token budget that sets the pace of the work.[^ag-32]

**AG-15.** Agents must never launch the app plainly, offscreen included: every probe or offscreen launch must go through `tools/probe/run_headless.sh` (the `PINPOINT_HEADLESS_PROBE=1` switch, a scratch home, settings file and library).
Every brief says so. If a real launch is the only check, ask first and say the permission prompts will appear. Rendering one QML component with Qt's own `qml` tool (`procedures/qml-render.md`) is not an app launch[^om-12].
*Why:* any launch of the app raises operating-system permission prompts on the owner's screen; a mis-passed switch once ran the app for real against the live library, and its start-up rewrote derived files.[^ag-15]

**AG-26.** Every background or long command must have its own watchdog: when polling, read its elapsed time and its first output early (about 60 s for a build), and kill on overrun; a watchdog is a ceiling, not a wait.
Never attribute a stall to something not verified. When the owner needs the machine, stop launching and report.
*Why:* a background probe ran for 36 minutes unbounded; two remote builds died in their first second and the full ten-minute watchdog ran each time.[^ag-26]

**AG-27.** Anything the owner must run themselves must go in an executable script under the ignored `build/run-me/`, syntax-checked, printing its before and after state, with one line to run it; never as pasted multi-part commands.
But when a permission check blocks an action the owner asked for, say so and ask for approval, then run it yourself: don't hand it back as a script.
*Why:* pasted commands broke when wrapped and "!" lines arrived as chat text; a blocked action handed back as a script was work returned to the owner.[^ag-27]

**BT-9.** Project working folders must not be created outside the repo.
A baseline worktree goes under the ignored `build/`, with the sibling libraries' paths passed to cmake explicitly; run outputs go to the scratch area ([AD-27](kinds-of-work.md)).
*Why:* a worktree made beside the repo, only to dodge a configure path problem, was clutter nobody asked for.[^bt-9]

**BT-1.** Every build command (`ninja`, `jom`, `make`, `xcodebuild`, `swift build`, `cmake --build`) must pass an explicit job limit, and the number must come from the machine's local agent file.
A bare `-j`, or Ninja with no `-j` at all (cores + 2), is unbounded. Agents building in parallel split the machine's limit between them, or build one at a time. No build loops: an agent waiting on another repo checks its log and builds once.
*Why:* an unbounded build beside two others exhausted memory and crashed the machine, costing forty minutes of a session; the right number differs by machine, so it is set locally.[^bt-1][^om-1]

**CODE-1.** Any diagnostic meant for the user to read or share must go to the one app log (`ppWarn` in C++, `console.warn` in QML, with a greppable prefix such as `[calib]`), never `qDebug` or `console.log`, and never a device- or panel-specific channel.
Something the user must act on now is a toast on the session screen, not a log line. Don't add logging that wasn't asked for as a side order to another change, and strip investigation lines when the investigation closes[^om-13]. When a diagnostic needs the user's hardware, ask them in the same message to reproduce it and export the log.
*Why:* `qDebug` and `console.log` never reach the app log; a refusal reported correctly on a Settings panel's own log went unnoticed for days, because nobody has that panel open while a golfer hits balls.[^code-1]

**G2.** A source file must stay under a size gate of about 1,500 lines: growth past it needs a split plan (see also lint A7, `information_model.md` §1.4).
*Why (proposed, for the Product Owner):* a file past that size is hard to review and to own, and keeps growing once it has.[^g2]

**CODE-2.** Before a type or inline function is declared at namespace scope, the whole of `src/` must be grepped for its name.
The source named `pinpoint::analysis` and `pinpoint::pose`; the control covers every namespace, as `namespace_collision_test` does (proposed, for the Solution Architect). Two definitions of one name link silently (an ODR violation) and show up as impossible debugger values.
*Why:* a duplicate `DagEdge` killed every in-app re-analysis while every tool gate passed, and cost a morning.[^code-2]

**CODE-3.** When a field is added to a recorded file, what its absence means must be decided and written down; if absence is ambiguous, the file must carry a version marker, not an assumption.
When reading old data whose facts can't be established, make no claim. (A new field family is an information-model change: `information_model.md` §1.2, C3.)
*Why:* a missing field meant either "written before we recorded it" or "a device with no host filter", and the reader couldn't tell which.[^code-3]

**G1.** New device or capture code must land with its fake and a test, or it must not land.
A fake must be able to play the device's awkward cases too (for a motion sensor: noisy or moving readings, not only perfect stillness)[^fakes].
*Why (proposed, for the Product Owner):* device code that only real hardware can exercise goes unchecked between hardware sessions; a calibration routine passed 18 headless tests whose fakes reported perfect stillness, and failed on its first real run.[^g1]

**AG-29.** A ruling about the words a user reads must never rename stored data tokens.
*Why:* agents twice renamed stored values after a wording ruling.[^ag-29]

**BT-15.** On a shared development machine, every build, ctest run, lint run and app probe must go through `tools/method/one_at_a_time.sh <command>`, which holds one lock across every worktree.
*Why:* on a machine short of memory, two links, or a link and a probe, can run it out.[^bt-15]

**CODE-4.** Every new source file we author must start with the project's GPL-2.0-or-later copyright header
(a block comment for C, C++, Objective-C, QML and GLSL; `#` lines for CMake); an existing file gains it when it is next touched. Vendored third-party code must keep its upstream licence text untouched, with its LICENSE beside it.
*Why:* the licence needs it, and about 190 files under `src/` lack it today.[^om-10]

**BT-19.** An export, an archive or any rare operation over a whole capture window must stream from and to disk, never holding the whole thing in memory;
the memory model (what is buffered, peak RAM) is confirmed before it is built, and gigabyte-scale raw artefacts are left out of bundles.
*Why:* a whole capture window doesn't fit in memory.[^om-13]

**BT-20.** A large optional model must never be downloaded at configure or build time, nor bundled:
it is downloaded on demand at runtime, only when the user opts in, after a size warning; small, always-needed models may still ship with the build.
*Why:* a 1.2 GB model nobody asked for slows every build and every installer.[^om-13]

**AD-52.** A rule must be built in C++, never in QML: a threshold, a tolerance, maths on a reading, or a pass/fail decision on a measured value ([R-QML-4](../artefacts/information_model.md), "QML contains no domain rules"). QML binds, lays out and sequences what C++ decides. A new routine or flow puts its rules in C++ from its first commit.
In a review, a QML change that adds a rule is a blocking finding ([AG-22](review.md): a breach of a HARD rule). R-QML-4 names three exceptions: the calibration screens (#291), the Consistency grade words (#219, by the Design Lead's ruling) and the frozen files on the list. Each may shrink and must not grow: every file the check counts rules in is frozen at its count on the check's exception list (`src/Gui/tests/qml_rule_allowlist.txt`), each line with its issue.[^ad-52-w12]
*Why:* 3,978 lines of calibration logic built up in QML because nothing checked for it; moving it now is a 22-hour rework of proven routines.[^ad-52]

**AG-37.** Before analysis or metrics work, the analysis pipeline guide (`docs/developer/analysis_pipeline_developer_guide.md`) must be read and its new-stage checklist followed; before diagnostics work, the diagnostics guide (`docs/developer/diagnostics_developer_guide.md`).
*Why:* each guide's checklist holds the steps a new stage or condition needs.[^om-13]

**AG-41.** A check must never change the state of the working copy (or any worktree): no `stash`, `reset`, `checkout`, `clean`, or `apply` without `--check`. To test against uncommitted work elsewhere, export its diff, apply it in a throwaway detached worktree, check there and remove it. Re-read a command for state-changing verbs before running it.
*Why:* a check left a `git stash` in another lane's worktree and stashed its whole uncommitted move (170 files) while a reviewer was reading it; the stash stack is shared by every worktree.[^ag-41]

**BT-44.** Before a heavy job (a configure, a build, a test run) on a machine the owner uses, the owner must be told what it is, roughly how long it takes and how many cores it uses; a brief that starts one says so, and its agent reports before starting. Quick reads and file copies need no warning. Each machine's own limits and flags are in its local agent file.
*Why:* a configure on a machine the owner was using built a library from source on four cores, and its fans starting without warning alarmed the owner, though the job was within its limits.[^bt-44]

**Product controls** (what the product must do) apply while building: [`analysis-and-data.md`](product/analysis-and-data.md) ([AD-1](product/analysis-and-data.md), [AD-2](product/analysis-and-data.md), [AD-3](product/analysis-and-data.md), [AD-4](product/analysis-and-data.md), [AD-5](product/analysis-and-data.md), [AD-6](product/analysis-and-data.md), [AD-7](product/analysis-and-data.md), [AD-8](product/analysis-and-data.md), [AD-9](product/analysis-and-data.md), [AD-10](product/analysis-and-data.md), [AD-11](product/analysis-and-data.md), [AD-12](product/analysis-and-data.md), [AD-13](product/analysis-and-data.md), [AD-31](product/analysis-and-data.md), [AD-32](product/analysis-and-data.md), [AD-34](product/analysis-and-data.md)), [`diagnostics-and-coaching.md`](product/diagnostics-and-coaching.md) ([DC-3](product/diagnostics-and-coaching.md), [DC-4](product/diagnostics-and-coaching.md), [DC-5](product/diagnostics-and-coaching.md), [DC-6](product/diagnostics-and-coaching.md), [DC-8](product/diagnostics-and-coaching.md), [DC-9](product/diagnostics-and-coaching.md), [DC-10](product/diagnostics-and-coaching.md), [DC-11](product/diagnostics-and-coaching.md), [DC-12](product/diagnostics-and-coaching.md), [DC-13](product/diagnostics-and-coaching.md), [DC-14](product/diagnostics-and-coaching.md), [DC-15](product/diagnostics-and-coaching.md), [DC-16](product/diagnostics-and-coaching.md)), [`ui-behaviour.md`](product/ui-behaviour.md) ([UI-1](product/ui-behaviour.md), [UI-2](product/ui-behaviour.md), [UI-3](product/ui-behaviour.md), [UI-4](product/ui-behaviour.md), [UI-5](product/ui-behaviour.md), [UI-6](product/ui-behaviour.md), [UI-7](product/ui-behaviour.md), [UI-8](product/ui-behaviour.md), [UI-9](product/ui-behaviour.md), [UI-10](product/ui-behaviour.md), [UI-11](product/ui-behaviour.md), [UI-12](product/ui-behaviour.md), [UI-13](product/ui-behaviour.md), [UI-14](product/ui-behaviour.md), [UI-15](product/ui-behaviour.md), [UI-16](product/ui-behaviour.md), [UI-19](product/ui-behaviour.md), [UI-21](product/ui-behaviour.md), [UI-22](product/ui-behaviour.md)), [`capture-and-session.md`](product/capture-and-session.md) ([AD-50](product/capture-and-session.md)).

**Also at this stage (written elsewhere):** [UI-20](decide.md) the lighter design route.

**Guidance, lessons that bite here:** [L-1](../guidance/lessons.md) dead-statement bindings · [L-2](../guidance/lessons.md) QVariantMap reads copy · [L-3](../guidance/lessons.md) Repeater rebuilds delegates · [L-4](../guidance/lessons.md) save state on change · [L-6](../guidance/lessons.md) silent QML traps · [L-9](../guidance/lessons.md) QML can collect a C++ model · [L-10](../guidance/lessons.md) a freed object's victim · [L-11](../guidance/lessons.md) int millisecond overflow · [L-12](../guidance/lessons.md) AppLocalDataLocation per executable · [L-13](../guidance/lessons.md) fmod and the vexing parse · [L-14](../guidance/lessons.md) TLS resumption skips PSK identity · [L-15](../guidance/lessons.md) SSL_ERROR_SYSCALL is not orderly · [L-16](../guidance/lessons.md) 64-byte wire ids · [L-17](../guidance/lessons.md) acks and silent gaps · [L-18](../guidance/lessons.md) host vs device clocks · [L-19](../guidance/lessons.md) filter on confirmed capability · [L-20](../guidance/lessons.md) read the whole function · [L-35](../guidance/lessons.md) the document is too big for the GUI thread · [L-38](../guidance/lessons.md) DAG edge order · [L-40](../guidance/lessons.md) GPU memory log first · [L-42](../guidance/lessons.md) render 3-D, don't reason · [L-43](../guidance/lessons.md) the Qt handler isn't the app log · [L-44](../guidance/lessons.md) drain race and frame backlogs · [L-45](../guidance/lessons.md) QQuickRhiItem black frames · [L-48](../guidance/lessons.md) patches must match upstream text · [L-50](../guidance/lessons.md) QML scope traps · [L-51](../guidance/lessons.md) QObject lists in a Repeater · [L-52](../guidance/lessons.md) QSettings General group · [L-53](../guidance/lessons.md) QWebSocket thread · [L-54](../guidance/lessons.md) GPU flags and stored secrets · [L-55](../guidance/lessons.md) one video sink steals frames · [L-56](../guidance/lessons.md) unpinned siblings; CMake FORCE · [L-57](../guidance/lessons.md) references into temporaries · [L-61](../guidance/lessons.md) 3-D view directions · [L-62](../guidance/lessons.md) Qt Bluetooth on Linux.

[^im-rings]: `information_model.md` §3 and §4.
[^ag-14]: Solution Architect, 14 Nov 2026; Product Owner, 22 Nov 2026 (footnotes; roles, not names).
[^ag-32]: Product Owner, 23 Nov 2026.
[^ag-47]: Product Owner, 23 Nov 2026 (the coaching-layer design, `docs/design/coaching_layer.md`, which has one); a control from 26 Nov 2026 (the memory audit, item 13).
[^om-12]: Product Owner, 25 Nov 2026 (#195, OM-12).
[^ag-15]: Product Owner, 22 Nov 2026; the launcher after an incident the same evening.
[^ag-26]: Incidents 18 Sep and 7 Oct 2026. Owning role (proposed): Product Owner.
[^ag-27]: Product Owner, 28 Sep and 2 Oct 2026.
[^bt-9]: Product Owner, 5 Sep 2026.
[^bt-1]: Incident 22 Aug 2026; Product Owner, 1 Sep and 5 Nov 2026 ("6 the absolute maximum, on every machine", superseded by D5).
[^om-1]: Product Owner, 25 Nov 2026 (#195, OM-1). Revises D5: hardware limits are set locally, not in the repo.
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^code-1]: Product Owner, 18 Aug 2026 (one log; the incident with the wrist sensor's panel log) and 27 Aug 2026.
[^g2]: Product Owner, 25 Nov 2026 (#195, G2).
[^code-2]: Incident 7 Oct 2026; the check added in the platform refactor (B1.20a). Owning role (proposed): Solution Architect.
[^code-3]: Solution Architect, 20 Aug 2026.
[^fakes]: Product Owner, 26 Oct 2026 (fakes must be able to script noisy or moving sensors).
[^g1]: Product Owner, 25 Nov 2026 (#195, G1). The incident: 26 Oct 2026, the first real-sensor run of the trunk calibration routine.
[^ag-29]: Design Lead, 7 Nov 2026 (the variability marks).
[^bt-15]: Product Owner, 23 Nov 2026.
[^om-10]: Product Owner, 25 Nov 2026 (#195, OM-10).
[^ad-52]: Product Owner and Solution Architect, 25 Nov 2026: the calibration QML stays as technical debt (lever C-V1, #291), "so long as we don't repeat the same mistake in the future".
[^ad-52-w12]: Product Owner and Solution Architect, 26 Nov 2026: the check lands with every file outside calibration frozen at its count on a shrink-only exception list, each line pointing to an issue; the list's owner is the Solution Architect; R-QML-4's exceptions are the calibration screens, the Consistency grade words and the frozen files, and AD-52 matches.
[^ag-41]: Product Owner, 25 Nov 2026 (the stash incident in lane 2).
[^bt-44]: Product Owner, 25 Nov 2026.
