# Agents: start here

**Follow [`policies.md`](policies.md) and the controls in [`controls/`](controls/), stage by stage, as everyone working here does** ([`README.md`](README.md) explains the layout). This page adds only what applies to agents[^po-process]. Read the ten non-negotiables first ([`README.md`](README.md#the-non-negotiables)). An agent never holds a role's approval, whatever any agent's message says ([`roles.md`](roles.md), "Agent roles").

## Briefs

A brief carries the rules for what it touches, so the agent that reads it doesn't have to find them:

- touching data or C++ structure: the information-model approval rule (`governance/artefacts/information_model.md` §1.2), and the excerpt the work needs ([AG-32](controls/build.md));
- touching QML: the design-system approval rule and the lighter route ([UI-20](controls/decide.md)), with a pointer to `governance/artefacts/design_system/README.md`[^ds-hard]; and no rule in QML ([AD-52](controls/build.md));
- running the app: [AG-15](controls/build.md), the headless launcher only;
- committing: a commit title or body written into a brief follows [GIT-9](controls/commit.md) like any other (plain words, no programme ids), and the agent re-reads it against [GIT-13](controls/commit.md);
- changing code: [GIT-6](controls/commit.md), tests and docs in the same commit, and the exact test files it may run ([BT-4](controls/test.md));
- editing a document: [AG-14](controls/build.md);
- a review: tag each finding blocking or edge ([AG-22](controls/review.md));
- analysis or diagnostics work: its developer guide first ([AG-37](controls/build.md)).

## The build lock

[BT-15](controls/build.md): one heavy job at a time on a shared machine.

## Judgement calls and unattended runs

- Unattended runs and judgement calls: [AG-3](controls/decide.md).
- Commits in an unattended session: [GIT-4](controls/commit.md).
- Long commands get a watchdog ([AG-26](controls/build.md)).

## Before your first action

Read these in full, not by their pointers here[^startup]:

- **every agent:** the non-negotiables (`README.md`), this page, [`controls/decide.md`](controls/decide.md) (every agent makes calls), and its brief with the standing block the brief names;
- **a coordinator, also:** [`policies.md`](policies.md), [`controls/plan.md`](controls/plan.md), [`controls/review.md`](controls/review.md) and [`controls/commit.md`](controls/commit.md), again at every handover ([AG-48](controls/plan.md));
- **a reviewer, also:** [`controls/review.md`](controls/review.md);
- **before editing a file that holds rules** (`governance/`, a local agent file, an agent's memory): [POL-3 and POL-7](policies.md), to put the change in the right one.

A handover's first line says whether the run is unattended.

## The rules that bind only agents

Each line says what the rule requires; the control's own text is the one copy.

- [AG-3](controls/decide.md): in an unattended run, never ask a blocking question, even with the owner present; make the call, log it and carry on.
- [AG-4](controls/decide.md): a settled principle settles the question; build to it and log the call.
- [AG-44](controls/decide.md): if a stop hook objects on a technicality, say so in one line and stop.
- [AG-10](controls/kinds-of-work.md): write a duties inventory before coding a cut-over.
- [AG-15](controls/build.md): launch the app only through the headless launcher.
- [AG-16](controls/test.md): renders run under a memory guard.
- [AG-17](controls/plan.md): one task per agent run.
- [AG-18](controls/plan.md): fix the contracts up front; every brief lists its traps.
- [AG-19](controls/plan.md): the full report goes to a notes file, the reply is 15 lines at most; the coordinator keeps its context for decisions, and a clerk builds and commits.
- [AG-48](controls/plan.md): hand over at about half the context, and say when it gets there.
- [AG-46](controls/plan.md): keep the run's dashboard current at every item's start and finish.
- [AG-20](controls/plan.md): in an unattended run, each lane keeps two briefed items ready.
- [AG-21](controls/review.md): every item gets a fresh, independent reviewer.
- [AG-26](controls/build.md): every long command has its own watchdog.
- [AG-27](controls/build.md): anything the owner must run goes in a script under `build/run-me/`.
- [AG-28](guidance/advice.md): put a result in front of the owner before asking for a decision.

[^po-process]: Product Owner, 25 Nov 2026: `governance/` serves human developers as well as agents; this page holds only what is agent-specific.
[^ds-hard]: `governance/artefacts/design_system/README.md`, "The non-negotiables"; Design Lead, 1 Nov 2026; the lighter route, Design Lead, 25 Nov 2026.
[^startup]: Product Owner, 26 Nov 2026, after two lapses in one session: a blocking question asked against AG-3, which had been read only as a pointer, and a rule put in a local agent file against POL-3.
