# Policies

The short principles that every control, procedure and piece of work here follows. Each policy names the controls that carry it out; a policy is changed as the README describes, by the role named in its footnote.

**POL-1. Decisions are put in plain English.** Every decision put to a role says what the thing is in everyday words, the choice, what changes under each answer, and a recommendation with its reason. *Controls:* [AG-1](controls/decide.md), [AG-2](controls/decide.md).[^pol-1]

**POL-2. Decisions belong to roles, not to people, and every governing sentence is traced.** Documents, briefs and commits name the role that decided; provenance goes in footnotes, never in running text; a sentence no decision covers is marked proposed. *Controls:* [AG-14](controls/build.md), [GIT-10](controls/commit.md); [`roles.md`](roles.md).[^pol-2]

**POL-3. One copy of each rule.** A rule is written once, in one place; everywhere else points to it. A rule already stated in a governing document (the information model, the design system) is pointed to, never restated.[^pol-3]

**POL-4. Approval comes before building what needs it.** An information-model change, a design-system change, an exception to a lint, a norm becoming proven, a fault edge, coaching wording and scope are approved by their role before the work is built. *Controls:* the approvals in [`controls/decide.md`](controls/decide.md).[^pol-4]

**POL-5. Honest numbers.** The product shows every reading it has, with its warning, and never hides one for being unreliable; it never claims more than it measured. *Controls:* [UI-1](controls/product/ui-behaviour.md), [UI-2](controls/product/ui-behaviour.md), [AD-5](controls/product/analysis-and-data.md), [DC-6](controls/product/diagnostics-and-coaching.md).[^pol-5]

**POL-6. Honest reporting.** Work is reported as it was observed: a failure is filed as a bug only when seen (one predicted by reading is filed as a finding, to reproduce first), and a result is shown before a decision is asked for. A claim that something works is POL-9's. *Controls:* [AG-22](controls/review.md), [AG-24](controls/review.md); guidance [AG-28](guidance/advice.md).[^pol-6]

**POL-7. Privacy.** Nothing private enters the repository: no golfer's name or athlete id, no machine name, login, share path or home path, and no one's personal working style. Rules here apply to any developer; how one developer uses their own machines and shares is written in their local agent file, never here. That file is local and untracked.[^pol-7][^pol-7w]

**POL-8. A personal, non-commercial product.** PinPoint Studio will never be released commercially; licence questions are weighed accordingly, and vendor software is never decompiled, bypassed or reached into. *Controls:* [AG-30](controls/decide.md).[^pol-8]

**POL-9. No claim without evidence.** Nothing is called delivered, fixed or working without evidence: a passing test, or an observation recorded against a named build. Anything else is "built, not verified". What counts as evidence, and which kind proves what, is the test strategy ([`artefacts/test_strategy.md`](artefacts/test_strategy.md)). *Controls:* [BT-38](controls/plan.md), [BT-39](controls/test.md), [BT-40](controls/test.md), [BT-41](controls/review.md), [BT-42](controls/release.md); [AG-25](controls/test.md), [GIT-14](controls/commit.md).[^pol-9]

[^pol-1]: Product Owner, 15 Nov 2026, widened 25 Nov 2026.
[^pol-2]: Product Owner, 22 Nov 2026 (roles, not names; footnotes); Solution Architect, 14 Nov 2026 (trace every sentence).
[^pol-3]: Product Owner, 21 Nov 2026 (#195, "One source"); Solution Architect, 14 Nov 2026 (never paraphrase a governing definition).
[^pol-4]: `roles.md`; `governance/artefacts/information_model.md` §1.2; `governance/artefacts/design_system/README.md`, "The non-negotiables".
[^pol-5]: Product Owner, 22 Oct 2026 ("a big philosophical principle for the entire product").
[^pol-6]: Product Owner, 6 Nov 2026 (evidence for bugs) and 2 Oct 2026 (a result before a decision); 26 Nov 2026 (a finding nobody has seen is filed as a finding, never a bug). Stated as a policy: proposed, for the Product Owner. The claim that something works moved to POL-9, with AG-25: the Product Owner and the Solution Architect, 26 Nov 2026.
[^pol-7]: Product Owner, 21 Nov 2026 (#195: "Nothing private is copied"; personal and machine matter "never goes into the repo").
[^pol-7w]: Product Owner, 26 Nov 2026: governance holds nothing about one developer's own machines, shares or how and when they are used; that is one developer's way of working, and it would leak a private set-up into a public repository.
[^pol-8]: Product Owner, 2 Nov 2026.
[^pol-9]: Product Owner, 25 Nov 2026 (the "killer rule"), after the requirements draft called 91 needs "delivered" by reading the code; as a policy, with its controls and the test strategy, approved by the Product Owner and the Solution Architect, 26 Nov 2026.
