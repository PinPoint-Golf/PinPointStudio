# Controls: 2. Plan

Writing the plan or the brief. An area reference: read it when doing this kind of work ([`AGENTS.md`](../AGENTS.md) has the index). Older text names four roles (Product Owner, Solution Architect, Design Lead, Coaching Lead); all four are the owner, so read a role's approval as "agree it with the owner first".

**AG-36.** Every plan that touches a data model must say what changes on screen, view by view
(strings that change, affordances withdrawn, views that only have to keep compiling), citing the QML by file and line, with an eyeball checklist of the screens to open.
*Why:* a model change otherwise changes screens no one planned to look at.[^om-13]

**AG-11.** A research or modelling plan must name its deliverables up front: a document whose every figure is reproducible from a command against a named run, and the re-runnable implementation, file by file.
The definition of done counts an honestly written-up failed gate as complete and a skipped gate as incomplete.
*Why:* numbers that can't be regenerated are not a result, and without a write-up the negative results go unrecorded.[^ag-11]

**AD-51.** A plan that adds or changes an object that is persisted or long-lived, and whose state changes what is allowed or what is on disk, must state that object's lifecycle (its states, its transitions and what each guarantees) in the lifecycle model before it is built. Value types, versioned packs (governed by version-gated re-analysis) and views need none.
A plan for such an object names its lifecycle section; a new state or transition is added there in the same change. Today the rule covers the application, the athlete, the session, the shot and the Rig.
The lifecycle model is [`governance/artefacts/lifecycle_model.md`](../artefacts/lifecycle_model.md) (owner: the Solution Architect). The information model's change process requires the same of a C1 or C3 change ([§1.2](../artefacts/information_model.md#12-what-counts-as-an-information-model-change)).
*Why:* a state nobody wrote down is a guarantee nobody can check: a shot that existed only inside its processor, and a Start that never switched to Capture, were both lost or broken by changes that never saw the lifecycle.[^ad-51]

**G3.** Each feature's plan must say what debt it adds, before it is built.
*Why (proposed, for the Product Owner):* debt named before building can be weighed with the feature; debt found afterwards is already paid for.[^g3]

**AG-12.** Line counts must never be a target: an architectural fix is never traded for fewer lines, and the net change in production lines is reported once, at the end of a step.
*Why:* lines of code are judged only at the end.[^ag-12]

**AG-43.** A plan asked for on a hard question (a root cause, a fragility, why something breaks) must spend its effort on that question: the mechanism, with numbers and evidence, and a design against it. Mechanical plumbing gets one line, marked out of scope.
*Why:* a plan answered a tracker-fragility question with product plumbing and left the hard question to "trace it later"; the answer that landed found two failure families in existing results without a new run.[^ag-43]

**AG-45.** Before a redesign is briefed, what the shipped screen already does that a role asked for must be listed in the brief and kept, unless that role says otherwise.
*Why:* a redesign brief asked for curves in ink only, and every design agent dropped the per-metric colours the Product Owner had asked for a week before.[^ag-45]

**BT-2.** Builds are targeted: a test build passes `--target` for only the test executables and lints being run (`tools/method/one_at_a_time.sh cmake --build build/tests --target <suite> … architecture_lint_test design_system_lint_test -j 6`), and the app tree (`PinPointStudio`) is built once, at the end of a change, before the headless probe and the commit.
A full `build/tests` build is for a release or a change to CMake or the build system. The app tree is built mid-work only for app-only code with no unit-test target. Debug and test builds compile through ccache (`cmake/PinPointCcache.cmake`; a 4 GB cache; never a release build, whose build date must be fresh), so a fix round that reverts an edit, or a reconfigure, does not recompile what is cached. Build-time measurements (clean, one-header, link) run on one machine of the primary platform, the same one for the before and the after.
*Why:* full test-tree builds of about 2,000 targets after a shared-header edit, repeated three or four times a step, cost 10–12 minutes each; one step of the domain refactor ran about 200 builds.

A plan that holds data copies in the ownership section, `governance/artefacts/information_model.md` §10, and answers it.

**Also at this stage (written elsewhere):** [AG-6](decide.md) architecture: design agreed before building · [AG-10](kinds-of-work.md) a duties inventory before a cut-over (agents)· [AD-16](kinds-of-work.md) a sources × metrics matrix.

**Guidance for this stage:** [AG-33](../guidance/advice.md) multi-session plans in a repo doc · [AG-39](../guidance/advice.md) a status line at each milestone · [AG-49](../guidance/advice.md) defer harder and bigger work to v2.

[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^om-5]: Product Owner, 25 Nov 2026 (#195, OM-5).
[^ag-8]: Product Owner, 5 Sep, 2 Oct and 14 Oct 2026.
[^ag-9]: Product Owner, 14 Nov 2026.
[^ag-11]: Product Owner, undated.
[^g3]: Product Owner, 25 Nov 2026 (#195, G3).
[^ag-12]: Product Owner, 22 Nov 2026 (the step-3 retrospective).
[^ag-17]: Product Owner, 12 Oct 2026.
[^ag-18]: Product Owner, 29 Aug 2026 (multi-repo work) and 15 Nov 2026 (own worktrees).
[^ag-19]: Product Owner, 14 Nov 2026.
[^ag-19b]: Product Owner, 26 Nov 2026: the programme runs attended, one step at a time, and the coordinator commits; the wording approved the same day.
[^bt-2b]: Product Owner, 24 Nov 2026. Which machine is in the local agent file (POL-7).
[^bt-2c]: The general half of the memory audit's item 1 (the Product Owner's practice since 21 Nov 2026); its machine half went to the local agent file on 26 Nov 2026. Whether this half is a control is for the Product Owner.
[^ag-43]: Product Owner, 30 Sep 2026; a control from 26 Nov 2026 (the memory audit, item 5).
[^ag-45]: Product Owner, 14 Oct 2026; a control from 26 Nov 2026 (the memory audit, item 7).
[^ag-46]: Product Owner, 14, 15 and 22 Nov 2026 (the summary and the clock) and 24 Nov 2026 (the completion figure); a control from 26 Nov 2026 (the memory audit, items 11 and 12).
[^ag-46b]: Product Owner, 26 Nov 2026 (the programme runs attended, one step at a time; the wording approved the same day), in place of a rewrite at every item's start and finish.
[^ag-20r]: Product Owner, 26 Nov 2026: one lane, attended (with [AG-50](plan.md)).
[^ag-20]: Product Owner, 11 Nov 2026 (the next issue at first review) and 24 Nov 2026 (no lane idle); 25 Nov 2026 (no open bugs brought forward to fill lanes); guidance until 26 Nov 2026, then a control (the memory audit, item 8).
[^bt-2]: Product Owner, 28 Oct and 21 Nov 2026.
[^bt-2d]: Product Owner, 26 Nov 2026: the full suite at a coarse cadence, not for every step; the wording approved the same day; then "stop doing builds everywhere": no extra run for a step that changes the Windows build. Then, 27 Nov 2026: no run without a reason ("doing it without a reason is daft"); the cadence of about every third step is withdrawn. Which machine runs it is in the local agent file (POL-7).

[^ad-51]: Product Owner and Solution Architect, 25 Nov 2026 (the lifecycle model's scope rule, from the #280, #281 and #283 losses). The lifecycle model is its own artefact, `governance/artefacts/lifecycle_model.md`, owned by the Solution Architect (option (a); DEC-204).
[^ag-50b]: Product Owner, 26 Nov 2026, the same evening: the plan's steps go into GitHub issues with a `platform refactor` label, worked through gradually; "the brief needs to be recorded in the issue as a comment. We can't rely upon transient files in a build folder."
[^ag-50c]: Product Owner, 27 Nov 2026: "that should be the full issue per session", replacing one step per session. The issues' "finish once started" marker is then redundant, and its sentence here is dropped (proposed, for the Product Owner).
[^ag-50]: Product Owner, 26 Nov 2026: first chunks of about 6–10 h ("we definitely need to work at a less granular level; it's bonkers right now"); then, the same day, attended, one lane, the plan as numbered steps run one at a time, no review cycle for every item and the studio PC used far less. The wording approved the same day.
[^ag-48]: Product Owner, 26 Nov 2026, after the overnight brief that omitted COMMON.md; the re-read at half, the handover at about 65% and the one-page state, Product Owner, 26 Nov 2026 ("just do it"), because every handover costs the next coordinator its re-reading.
[^bt-38]: Product Owner, 25 Nov 2026 (the evidence rule, [POL-9](../AGENTS.md)); the control and its wording approved by the Product Owner and the Solution Architect, 26 Nov 2026.
