# Controls: 1. Decide

What must be settled, and by whom, before work is built. Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

## Approvals: stop and wait

These need the named role's approval **before** the work is built. Propose, then wait; don't build ahead of the answer. Put each decision in plain English ([AG-1](decide.md)); an agent puts the matching rule in every brief that touches the area (agents).

| Change | Approved by | What counts, and how to propose |
|---|---|---|
| **Information-model change** | Solution Architect | `information_model.md` §1.2 (what counts, and how to propose).[^im-hard] |
| **Design-system change**: a new component; a new style, size or kind variant; a changed look or behaviour of an existing component; a new or changed token; retiring a component or token; any change no ruling covers | Design Lead | `governance/artefacts/design_system/README.md`, "The non-negotiables" (what counts, and how to propose). Using existing components and tokens on a screen, moving them, removing a component from a screen, or changing words, when a Design Lead ruling already covers it, takes the lighter route instead ([UI-20](decide.md)). Retiring a component or token from the design system is not light.[^ds-hard][^ui-20] |
| **An allowlist line** in `src/Gui/tests/architecture_allowlist.txt` or `design_system_allowlist.txt` | the lint's owner (Solution Architect; Design Lead) | Never add one to make a test pass: fix the code, or ask the owner[^allow] |
| **A norm becoming proven, a fault edge, coaching wording** | Coaching Lead | `roles.md`; [DC-1](decide.md), DC-2 |
| **Scope**: work into or out of a plan, a release, a change of release route | Product Owner | `roles.md`; the current plan |

In an unattended run (agents), everything else is a judgement call made, acted on and logged for review ([AG-3](decide.md)).

## Controls

**AG-1.** Every decision put to a role (a ruling, an approval, an information-model, design or scope choice) must be written in plain English, with no jargon.
For each one say what the thing is in everyday words (what it does on screen, what it is computed from), the choice, what changes for the product or the work under each answer, and the recommendation with its reason in one sentence. Code names go only in brackets after the plain words. Name deferred work by what it is and when it happens, never with words that suggest the role's own time ("session", "meeting"). Say which role a decision needs ("Design Lead: approve this sheet?"). Terse is fine elsewhere: status, and reports of work done.
*Why:* questions that leaned on coined terms and code names stalled decision walks and risked answers to the wrong question; a reader fresh to a dense list could not decode what was being asked.[^ag-1]
*Owner:* Product Owner · *Stage:* Decide · *Compliance:* none yet

**AG-3.** In an unattended run, an agent must make the call, act on it and log it, and must never block on a question; it must not use a blocking question tool, even while the owner is present, unless the Product Owner asks for a walk-through.
Log each call (what, the options, the choice, why, how to undo it) for the role's review, and carry on. Only the HARD approvals wait (an information-model change: Solution Architect; a design-system change: Design Lead), listed where the Product Owner reads what needs them, while the work around them continues; a run also stops before a live-library or corpus write, an allowlist addition, a freeze breach or a history rewrite. Never end a run message with a question.
*Why:* one blocking question stalled an orchestration for eight hours over two calls that didn't matter; replacing it with plain-text questions was the same mistake.[^ag-3]
*Owner:* Product Owner · *Stage:* Decide · *Compliance:* none yet

**AG-4.** When a stated principle or decision already settles a question, the work must build to it, correct any reference row whose wording lags the principle (citing it), and log the call; a question is asked only when nothing covers it.
*Why:* a question went to the Solution Architect mid-run that an ownership principle already answered.[^ag-4]
*Owner:* Solution Architect · *Stage:* Decide · *Compliance:* none yet

**AG-5.** A role's "doesn't matter", "could drop it" or "not worth it" must be treated as a scope cut: the related work stops, and if the cut's scope is unclear, one short question is asked before another round.
When a fix keeps needing rounds, say so before round three and offer to stop or park it.
*Why:* a narrow reading of a scope cut cost about four hours on one small component.[^ag-5]
*Owner:* Product Owner · *Stage:* Decide · *Compliance:* none yet

**AG-44.** When the work is done and a tool's stop hook objects on a technicality, an agent must say so once, in one line, and stop; it must never start sweeps, builds or commits just to satisfy the hook.
*Why:* an agent began re-running an hour-long sweep to quiet a hook after the Product Owner had said they would clear it themselves.[^ag-44]
*Owner:* Product Owner · *Stage:* Decide · *Compliance:* none yet

**AG-30.** Third-party licence questions must be weighed as personal, non-commercial use, and licence risk must not be raised as a blocker for a connector that reads a vendor's output files: the product will never be released commercially.
Still never decompile, reverse-engineer or bypass vendor software, and never touch a vendor's authentication or credential files.
*Why:* it came up over a launch-monitor vendor's licence terms for a file-reading connector.[^ag-30]
*Owner:* Product Owner · *Stage:* Decide · *Compliance:* none yet

**AG-6.** For architectural or structural work, the design must be written, read by the Solution Architect and explicitly agreed before any implementation; answers to questions asked mid-flow are not a go-ahead.
Lead a design with its principles, then apply them, and check every object: is it domain data, application state or view state? Say plainly what is running at every status update.
*Why:* implementation began before the design had been read; a design organised around audit findings read as a remediation map, not a design.[^ag-6]
*Owner:* Solution Architect · *Stage:* Decide · *Compliance:* a recorded approval (Solution Architect)

**AG-7.** A proposed information-model principle (a dependency rule, a context, a ring) must carry an include-graph check against `src/` (how many files fit, need a carve-out, or fit neither) before it goes to the Solution Architect.
*Why:* a context rule ruled without one needed three carve-outs within hours.[^ag-7]
*Owner:* Solution Architect · *Stage:* Decide · *Compliance:* none yet

**AG-2.** When decisions are walked, or before work is called done, every open item must be put: gathered from every source, the checkable ones settled, the count stated, and all of the rest walked.
Never filter by your own sense of importance; a list "for the owner" that is never shown is a decision taken by silence.
*Why:* six items were walked and thirteen left parked, and the owner found out after the commit.[^ag-2]
*Owner:* Product Owner · *Stage:* Decide · *Compliance:* none yet

**DC-1.** Every norm must stay provisional until the Coaching Lead decides otherwise with evidence.
Never change a norm's source from heuristic, nor call a norm validated or seated in text or on screen, without that decision and the evidence attached (named data, run and numbers). A rewritten citation keeps saying the norm is provisional; a re-seat goes to the Coaching Lead as a proposal with its evidence first.
*Why:* new readings grade faults straight away against heuristic norms, which is acceptable because σ and the warnings say how far to rely on them; a quiet promotion would overstate them.[^dc-1]
*Owner:* Coaching Lead · *Stage:* Decide · *Compliance:* a recorded approval (Coaching Lead)

**DC-2.** A causal edge must not be added or dropped on one's own reasoning: list the exact options and ask the Coaching Lead, also when a validator or a pack rule forces an edge out.
Where a condition is read is not when the fault happens. A fault's links follow what the fault means, not a calculation.
*Why:* causal edges are coaching judgement; an edge dropped by analogy, on a timing argument, was wrong and went back in.[^dc-2]
*Owner:* Coaching Lead · *Stage:* Decide · *Compliance:* a recorded approval (Coaching Lead)

**UI-18.** A change to a visual convention must have an explicit yes on its sheets: silence, or moving on to another request, is not approval.
*Why:* a turned-away avatar was taken as approved because the reviewer moved on.[^ui-18]
*Owner:* Design Lead · *Stage:* Decide · *Compliance:* a recorded approval (Design Lead)

**UI-20.** A UI change that only uses existing components and tokens on a screen, moves them, removes a component from a screen, or changes words, and that follows a ruling the Design Lead has already made, may take the lighter route: it must be built, shown as an annotated before-and-after sheet (UI-17), and logged for the Design Lead's review afterwards.
Still approved before it is built: a new component; a new style, size or kind variant; a changed look or behaviour of an existing component[^dl-m9]; a new or changed token; retiring a component or token from the design system; and any change no ruling covers.
*Why:* the before-approval gate is for what changes the design system, not for using it as it stands.[^ui-20]
*Owner:* Design Lead · *Stage:* Decide · *Compliance:* a review (by the Design Lead, afterwards)

**Guidance for this stage:** [AG-13](../guidance/advice.md) check a premise before applying findings · [AG-28](../guidance/advice.md) a result in front of the owner first (agents) · [AG-40](../guidance/advice.md) fix only the behaviour corrected.

[^im-hard]: `information_model.md` §1.2.
[^ds-hard]: `governance/artefacts/design_system/README.md`, "The non-negotiables"; Design Lead, 1 Nov 2026.
[^ui-20]: Design Lead, 25 Nov 2026 (the lighter design route, and its wording).
[^allow]: `roles.md`, "The documents each role owns"; `information_model.md` §1.4; `governance/artefacts/design_system/README.md`.
[^ag-1]: Product Owner, 15 Nov 2026 (define coined terms) and 14 Nov 2026 (plain names for deferred work); widened 25 Nov 2026 to every governance decision.
[^ag-3]: Product Owner, 15–16 Nov, 23 Nov and 24 Nov 2026.
[^ag-4]: Solution Architect, 15 Nov 2026.
[^ag-5]: Product Owner, 11 Nov 2026.
[^ag-30]: Product Owner, 2 Nov 2026.
[^ag-44]: Product Owner, 7 Nov 2026; a control from 26 Nov 2026 (the memory audit, item 6).
[^ag-6]: Solution Architect, 14 Nov 2026.
[^ag-7]: Solution Architect, 24 Nov 2026.
[^ag-2]: Product Owner, 7 Nov 2026.
[^dc-1]: Coaching Lead, 12 Nov 2026.
[^dc-2]: Coaching Lead, 8 Nov 2026; links follow meaning, 12–13 Nov 2026.
[^ui-18]: Design Lead, 27 Oct 2026.
[^dl-m9]: Design Lead, 25 Nov 2026 (review round 2, M9).
