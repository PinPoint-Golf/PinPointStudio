# Controls: 2. Plan

Writing the plan or the brief. Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

**AG-36.** Every plan that touches a data model must say what changes on screen, view by view
(strings that change, affordances withdrawn, views that only have to keep compiling), citing the QML by file and line, with an eyeball checklist of the screens to open.
*Why:* a model change otherwise changes screens no one planned to look at.[^om-13]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-8.** A plan must contain only its chosen scope (no "later runs" sections), carry its decision rule up front (what passes, what wins, what happens if nothing passes), and run to completion without mid-way holds; judgements that would have been holds become written gates the run applies, with the evidence in the end review.
A session break is allowed only where the plan itself puts one[^om-5]. An approved list of work items runs back to back, each behind its own switch where it can be, so each can be kept or dropped at one review at the end.
*Why:* out-of-scope material blurs what is being approved, and holds stall the run; asking a role to re-decide what a gate decides wastes their attention.[^ag-8]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-9.** A plan written from an audit must place every finding: in a step, deferred with a reason the Product Owner agreed, or rejected by the Product Owner.
Report the coverage count with the plan; a deferred list holds only out-of-scope items; label unscheduled items "unscheduled", not "deferred".
*Why:* 62 of 135 findings were in no step, and a listing made them look deliberately deferred.[^ag-9]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* a statement in the plan (the coverage count)

**AG-11.** A research or modelling plan must name its deliverables up front: a document whose every figure is reproducible from a command against a named run, and the re-runnable implementation, file by file.
The definition of done counts an honestly written-up failed gate as complete and a skipped gate as incomplete.
*Why:* numbers that can't be regenerated are not a result, and without a write-up the negative results go unrecorded.[^ag-11]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AD-51.** A plan that adds or changes an object that is persisted or long-lived, and whose state changes what is allowed or what is on disk, must state that object's lifecycle (its states, its transitions and what each guarantees) in the lifecycle model before it is built. Value types, versioned packs (governed by version-gated re-analysis) and views need none.
A plan for such an object names its lifecycle section; a new state or transition is added there in the same change. Today the rule covers the application, the athlete, the session, the shot and the Rig.
The lifecycle model is [`governance/artefacts/lifecycle_model.md`](../artefacts/lifecycle_model.md) (owner: the Solution Architect). The information model's change process requires the same of a C1 or C3 change ([§1.2](../artefacts/information_model.md#12-what-counts-as-an-information-model-change)).
*Why:* a state nobody wrote down is a guarantee nobody can check: a shot that existed only inside its processor, and a Start that never switched to Capture, were both lost or broken by changes that never saw the lifecycle.[^ad-51]
*Owner:* Solution Architect · *Stage:* Plan · *Compliance:* a review of the plan

**G3.** Each feature's plan must say what debt it adds, before it is built.
*Why (proposed, for the Product Owner):* debt named before building can be weighed with the feature; debt found afterwards is already paid for.[^g3]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* a statement in the plan; an automated check is a build item

**AG-12.** Line counts must never be a target: an architectural fix is never traded for fewer lines, and the net change in production lines is reported once, at the end of a step.
*Why:* lines of code are judged only at the end.[^ag-12]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-17.** An agent run must carry one task: a second task must never be queued onto a running agent; wait for its report, show the result, then resume it with the next.
To change course, stop it and resume it with one explicit instruction.
*Why:* messages land only between an agent's tool calls and it reports once, so a queued task blended two pieces of work and hid the first result.[^ag-17]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-18.** The coordinator must fix interface contracts up front, and agents must not negotiate them; every brief must carry an explicit list of the traps found by tracing; parallel work splits by repository or by disjoint files, each implementer in its own worktree; an agent's green claim is a claim until the coordinator's own integration build and ctest agree.
*Why:* agents reading only the code miss traps found by tracing; two implementers in one tree clashed on builds.[^ag-18]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-43.** A plan asked for on a hard question (a root cause, a fragility, why something breaks) must spend its effort on that question: the mechanism, with numbers and evidence, and a design against it. Mechanical plumbing gets one line, marked out of scope.
*Why:* a plan answered a tracker-fragility question with product plumbing and left the hard question to "trace it later"; the answer that landed found two failure families in existing results without a new run.[^ag-43]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* a review of the plan

**AG-45.** Before a redesign is briefed, what the shipped screen already does that a role asked for must be listed in the brief and kept, unless that role says otherwise.
*Why:* a redesign brief asked for curves in ink only, and every design agent dropped the per-metric colours the Product Owner had asked for a week before.[^ag-45]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* a review of the brief

**AG-19.** Every agent must write its full report to a notes file in its work folder and reply in 15 lines or fewer (state, files, tests, decisions needed, the notes path).
The coordinator never re-pastes reports, reads a notes file only when a decision depends on a detail, and keeps its own context for decisions, briefs, review judgement and spot checks; a clerk agent builds, stages, commits and pushes a signed-off item, under GIT-1, GIT-2 or GIT-4. State lives in files, so a compaction loses nothing. Check any agent silent for over 30 minutes.
*Why:* reports entering the coordinator's context twice, and clerical work, used up the context needed for judgement.[^ag-19]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-48.** A coordinator session must hand over to a fresh session once about half its context is used, and must tell the Product Owner when its estimate passes that point.
It keeps the handover file current as items land, so the change loses nothing: the briefs' rules, each lane's state, what waits on a role, and where each brief and note is. It reports at once if its context has been summarised. An agent can't see its own usage, so it estimates, early rather than late; the exact figure is the session's own context command.
*Why:* a rule read hours earlier in a long session is no longer in front of the agent when it decides, so it gets missed: the overnight coordinator wrote one brief without the standing block, and a lane started a cabin run it hadn't been asked for.[^ag-48]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-46.** A run's dashboard (the file where the Product Owner reads the run's state) must be kept current: at every item's start and finish, every field of its summary is rewritten, not only a log line added; times come from the clock, never estimated; completion is a percentage weighted by effort, with an overall figure computed as done ÷ (done + left) and its range; and what blocks the run is kept apart from what does not.
The columns themselves are the programme's own files' business.
*Why:* a dashboard updated only in its log said a finished step was still running, and an estimated time misled the Product Owner after time away.[^ag-46]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**AG-20.** In an unattended run, no lane may sit idle: each lane must keep at least two briefed, unblocked items ready, drawn only from ruled work (the plan, or issues a role has scheduled), never filled with open bugs.
Start the next item the moment one lands or goes into its first review, choosing one with no files in common with any uncommitted change; one commit per issue. A signed-off item that can't commit yet is parked as a patch, so its worktree is free. When the plan's next items are blocked, bring ruled work forward (the next stage's briefs, scheduled test or lint items).
*Why:* reviews take a while, and lanes sat idle while rulings were recorded; filling them with open bugs took the steering of the work away from the Product Owner.[^ag-20]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

**BT-2.** A piece of work must need at most two or three builds in all, and the budget must cover the whole item: the implementer's builds, every fix round's and the reviewer's.
Batch related edits and build at the end of each part, not after every stage. A plan's verification section names the targets the change affects, one line each on why, and says which suites are not run and why. Reviewers run ctest on the implementer's current build and rebuild only when it is stale. A "before" build of an old commit is made only when no kept binary or dump can answer. A long machine run (a sweep, a re-run on another machine) is justified in one line before it starts, and runs once. Build-time measurements (clean, one-header, link) run on one machine of the primary platform, the same one for the before and the after, since a comparison needs one machine[^bt-2b]. Each brief sets its item's build budget, and each commit report gives the build count (proposed, for the Product Owner)[^bt-2c].
*Why:* builds are slow and memory-bound, and every build costs the owner time and energy; one step of the domain refactor ran about 200 builds.[^bt-2]
*Owner:* Product Owner · *Stage:* Plan · *Compliance:* none yet

A plan that holds data copies in the ownership section, `governance/artefacts/information_model.md` §10, and answers it.

**Also at this stage (written elsewhere):** [AG-6](decide.md) architecture: design agreed before building · [AG-10](kinds-of-work.md) a duties inventory before a cut-over (agents) · [AG-31](kinds-of-work.md) the feature freeze · [AD-16](kinds-of-work.md) a sources × metrics matrix.

**Guidance for this stage:** [AG-33](../guidance/advice.md) multi-session plans in a repo doc · [AG-39](../guidance/advice.md) a status line at each milestone.

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
[^bt-2b]: Product Owner, 24 Nov 2026. Which machine is in the local agent file (POL-7).
[^bt-2c]: The general half of the memory audit's item 1 (the Product Owner's practice since 21 Nov 2026); its machine half went to the local agent file on 26 Nov 2026. Whether this half is a control is for the Product Owner.
[^ag-43]: Product Owner, 30 Sep 2026; a control from 26 Nov 2026 (the memory audit, item 5).
[^ag-45]: Product Owner, 14 Oct 2026; a control from 26 Nov 2026 (the memory audit, item 7).
[^ag-46]: Product Owner, 14, 15 and 22 Nov 2026 (the summary and the clock) and 24 Nov 2026 (the completion figure); a control from 26 Nov 2026 (the memory audit, items 11 and 12).
[^ag-20]: Product Owner, 11 Nov 2026 (the next issue at first review) and 24 Nov 2026 (no lane idle); 25 Nov 2026 (no open bugs brought forward to fill lanes); guidance until 26 Nov 2026, then a control (the memory audit, item 8).
[^bt-2]: Product Owner, 28 Oct and 21 Nov 2026.

[^ad-51]: Product Owner and Solution Architect, 25 Nov 2026 (the lifecycle model's scope rule, from the #280, #281 and #283 losses). The lifecycle model is its own artefact, `governance/artefacts/lifecycle_model.md`, owned by the Solution Architect (option (a); DEC-204).
[^ag-48]: Product Owner, 26 Nov 2026, after the overnight brief that omitted COMMON.md.
