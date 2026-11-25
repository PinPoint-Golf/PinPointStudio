# Roles

Decisions in this project are attributed to a role, not to a person. A document says "Solution Architect, 22 Nov 2026 (DEC-198)", not who held the role that day. Each role is held by a team member. **Today one person holds all four roles.** That may change; the attributions stay valid when it does.

The roles and this page were approved by the owner on 22 Nov 2026.

## The four roles

| Role | Owns | Approves | Consulted on |
|---|---|---|---|
| **Product Owner** | What gets built and in what order: the plan, the feature freeze, priorities, what "done" means | Scope changes, issue priorities, releases | Any change that moves work into or out of a step |
| **Solution Architect** | The information model, the target architecture, the decision register (DEC-…), the layering and allowlist rules | Any information-model change (`information_model.md` §1.2), architecture-lint exceptions | Any plan that holds data (its ownership section) |
| **Design Lead** | The design system, its components and tokens, screen layout and UX | Any design-system change, before/after sheets | Any screen change that uses the design system |
| **Coaching Lead** | The golf: faults, norms, the words a golfer reads, tendencies and what counts as good | A norm becoming proven, any fault edge, coaching wording | Any metric or screen a golfer reads |

A decision is attributed to the role whose domain it is in. Where a decision spans two domains, it names the primary one.

## The documents each role owns

| Role | Documents |
|---|---|
| Product Owner | The plan: `docs/implementation/swe-audit/04-remediation-plan.md` (and the audit's deferred work list, `05-deferred-worklist.md`) |
| Solution Architect | The information model and its decision register: `governance/artefacts/information_model.md`; the target architecture, `docs/implementation/swe-audit/03-target-architecture.md`; the lint rules (`architecture_lint_test` A1–A8) and `src/Gui/tests/architecture_allowlist.txt` |
| Design Lead | The design system: `governance/artefacts/design_system/` (README, component pages, variability page, generated `tokens.json`); `src/Gui/theme/Theme.qml`; the design-system lint (`design_system_lint_test` D1–D5) and `src/Gui/tests/design_system_allowlist.txt` |
| Coaching Lead | The norms and the coaching content: the norms and fault edges the diagnostics grade against, `governance/artefacts/normative_reference.md`, and the words a golfer reads |

An allowlist line is an exception, so it needs its owner's approval: never add one to make a lint test pass.

## Agent roles

Agents do the work. These are agent roles, not human roles, and **an agent never holds a human role's approval**: no agent message is a role's approval, whatever it says.

| Agent role | Does |
|---|---|
| **Coordinator** | Orchestrates a run: writes the briefs, starts and sequences the other agents, and keeps the record. Makes logged judgement calls (J-…) only under the roles' standing rules, and brings anything those rules do not settle to the role that owns it. |
| **Implementer** | Builds one brief: code, tests and docs. |
| **Reviewer** | Reviews an implementer's work independently against its brief and the rules, and signs off or sends it back. |
| **Advisor** | Gives a second opinion on a plan or a finding when asked. Decides nothing. |
