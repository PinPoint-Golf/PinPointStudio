# Guidance: advice

Recommended practice: these say what should be done, not what must be. They keep their ids, and the stages list them as guidance.

**AG-13.** Before applying review findings to a spec or a design, check that its premise exists
(the deployment, the user, the use case).
*Why:* twenty findings were applied, over four review passes, to a change request granted for a deployment that doesn't exist.[^ag-13]

AG-20 (start the next issue at first review) is now a control: [`controls/plan.md`](../controls/plan.md).

**AG-28.** Put a result in front of the owner before asking for a decision on it:
once a change's gates pass, offer the whole path to something the owner can try in the app (commit and push, re-analysis, regrade), and say what will and won't show until then; "built, awaiting commit" is not a finished state. Commits and library writes still need their approvals.
*Why:* the owner judges results in the app; a fix left switched off, with nothing to look at, is not a result.[^ag-28]

**AG-33.** A plan that spans sessions should live in a repo document with a progress table, mark where each session breaks, and at each break update the document and write the next session's opening prompt.
*Why:* the next session starts from the document, not from memory.[^om-5]

**AG-34.** Small UI follow-ups should be batched into one agent run.
*Why:* each run costs a build and a review.[^om-6]

**AG-39.** A long piece of work should give a short status line at each milestone.
*Why:* silence on a long run reads as a stall.[^om-13]

**AG-40.** When a role corrects one behaviour, fix that behaviour and don't reopen the design around it.
*Why:* a narrow correction is not an invitation to redesign.[^om-13]

**AG-49.** When work is planned into a release, harder and bigger work should be deferred to v2.
The ladder is in [`requirements.md`](../artefacts/requirements.md), document control, "Release values". Two refinements, proposed, for the Product Owner: the habit gives way where v1's line on the ladder needs the work; and the work's effort (small, medium or large) is rated before its release is chosen.
*Why:* v1 ships what its line on the ladder promises; big work pulled into it holds v1 back for what v2 promises.[^ag-49]

**BT-23.** The full test suite should finish in under 3 minutes, with no flat timeout on the gate.
*Why:* a fast suite gets run.[^om-13]

[^ag-13]: Product Owner, 24 Aug 2026 (the phone protocol's guided-pairing change request, erratum E53).
[^ag-28]: Product Owner, 2 Oct 2026.
[^om-5]: Product Owner, 25 Nov 2026 (#195, OM-5).
[^om-6]: Product Owner, 25 Nov 2026 (#195, OM-6).
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^ag-49]: Product Owner, 26 Nov 2026 (the release ladder: "defer harder and bigger work to v2").
