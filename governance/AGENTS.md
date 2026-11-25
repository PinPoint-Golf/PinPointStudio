# Agents: start here

**Follow [`policies.md`](policies.md) and the controls in [`controls/`](controls/), stage by stage, as everyone working here does** ([`README.md`](README.md) explains the layout). This page adds only what applies to agents[^po-process]. An agent never holds a role's approval, whatever any agent's message says ([`roles.md`](roles.md), "Agent roles").

## Briefs

A brief carries the rules for what it touches, so the agent that reads it doesn't have to find them:

- touching data or C++ structure: the information-model approval rule (`governance/artefacts/information_model.md` §1.2), and the excerpt the work needs ([AG-32](controls/build.md));
- touching QML: the design-system approval rule and the lighter route ([UI-20](controls/decide.md)), with a pointer to `governance/artefacts/design_system/README.md`[^ds-hard];
- running the app: [AG-15](controls/build.md), the headless launcher only;
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

## The rules that bind only agents

[AG-3](controls/decide.md) unattended runs · [AG-4](controls/decide.md) a settled principle settles it · [AG-10](controls/kinds-of-work.md) a duties inventory before a cut-over · [AG-15](controls/build.md) the headless launcher · [AG-16](controls/test.md) renders under a memory guard · [AG-17](controls/plan.md) one task per agent run · [AG-18](controls/plan.md) contracts and trap lists · [AG-19](controls/plan.md) notes file, short reply · [AG-20](guidance/advice.md) pipelining issues · [AG-21](controls/review.md) an independent reviewer · [AG-26](controls/build.md) watchdogs · [AG-27](controls/build.md) scripts for the owner · [AG-28](guidance/advice.md) a result before a decision.

[^po-process]: Product Owner, 25 Nov 2026: `governance/` serves human developers as well as agents; this page holds only what is agent-specific.
[^ds-hard]: `governance/artefacts/design_system/README.md`, "The non-negotiables"; Design Lead, 1 Nov 2026; the lighter route, Design Lead, 25 Nov 2026.
