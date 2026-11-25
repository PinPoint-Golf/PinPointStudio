# Controls: 5. Review

Reviewing the change, and putting it before the role that approves it. Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

**AG-21.** Each fix in a batch reviewed later must get a fresh, independent reviewer agent (not a fork of the coordinator) that checks every definition-of-done bullet and the diff, and returns SIGNED OFF or CHANGES NEEDED before the fix reaches the role's list.
Reviewers read, and run ctest on the implementer's build; they never edit, and rebuild only when the build is stale.
*Why:* what lands on the owner's list should already have survived an independent check, so the review time goes on judgement.[^ag-21]
*Owner:* Product Owner · *Stage:* Review · *Compliance:* a review (this control is the review)

**AG-22.** Only blocking findings may be fixed in an item, and the rest must be filed as issues, everywhere, interactive or unattended. A finding is blocking only when it is wrong data or lost golfer input in the normal flow, a defect the golfer sees in the normal flow, a breach of a HARD rule, a broken build or test, or the item's own deliverable not met. Blocking findings are fixed in the item.
Anything that needs an unusual condition is an edge case: a GitHub issue labelled `edge case`, with an area label, a priority and the evidence, and parked. Reviewers tag each finding BLOCKING or EDGE. A fix round fixes blockers, plus minors that are wording, comments or tests; a minor that changes behaviour goes on the carry list unless the golfer would see it. Each item gets one review plus one check of the fixes; a defect an item's own testing finds becomes a new small item, never part of the one that found it.
*Why:* reviews kept finding real but rare cases, each costing a round and its builds.[^ag-22][^d4]
*Owner:* Product Owner · *Stage:* Review · *Compliance:* a review

**AG-23.** Retired.
 It said a bug found along the way and within reach is fixed in the same session; AG-22 replaces it.[^d4]
*Owner:* Product Owner · *Stage:* Review · *Compliance:* retired

**AG-24.** A bug must be filed only for an observed failure (seen, or reported by a user); a predicted failure goes to the role as a finding, and an agent's "likely bug" is a prediction until checked.
An observed, unexplained result that could change users' data is filed at once, without being asked, saying the cause is unknown. A bug found and not fixed by the end of a session is filed as a GitHub issue before the session closes, never written into an agent's memory: in plain English, with what goes wrong, how to recreate it and its definition of done, the `bug` label, an area label, a priority label and the milestone; close it with the fixing commit. Deferred work goes into its matching task issue.
*Why:* predicted failures filed as bugs read as known defects; a measured, serious shift went unfiled; one tracker that others can read is the record.[^ag-24]
*Owner:* Product Owner · *Stage:* Review · *Compliance:* none yet

**UI-17.** Every UI change must be shown as annotated before-and-after review sheets, never raw full-page renders: one sheet per change, a title and two or three caption lines (before, after, what to check), the BEFORE and AFTER panels side by side cropped to the region, boxes on exactly what changed, and a control panel where the claim is that a normal case is untouched.
Check each sheet yourself before showing it.
*Why:* fourteen near-identical full-page renders were unreviewable; the sheets made review fast.[^ui-17]
*Owner:* Design Lead · *Stage:* Review · *Compliance:* a review

**AG-38.** On a crash report, a native debugger backtrace must be got before any diagnosis or code change.
*Why:* a guessed crash site was wrong; the backtrace named a different cause.[^om-13]
*Owner:* Product Owner · *Stage:* Review · *Compliance:* none yet

**Product controls** (what the product must do) a review checks the change against: [`analysis-and-data.md`](product/analysis-and-data.md) ([AD-1](product/analysis-and-data.md), [AD-2](product/analysis-and-data.md), [AD-3](product/analysis-and-data.md), [AD-4](product/analysis-and-data.md), [AD-5](product/analysis-and-data.md), [AD-6](product/analysis-and-data.md), [AD-7](product/analysis-and-data.md), [AD-8](product/analysis-and-data.md), [AD-9](product/analysis-and-data.md), [AD-10](product/analysis-and-data.md), [AD-11](product/analysis-and-data.md), [AD-12](product/analysis-and-data.md), [AD-13](product/analysis-and-data.md), [AD-31](product/analysis-and-data.md), [AD-32](product/analysis-and-data.md), [AD-34](product/analysis-and-data.md)), [`diagnostics-and-coaching.md`](product/diagnostics-and-coaching.md) ([DC-3](product/diagnostics-and-coaching.md), [DC-4](product/diagnostics-and-coaching.md), [DC-5](product/diagnostics-and-coaching.md), [DC-6](product/diagnostics-and-coaching.md), [DC-8](product/diagnostics-and-coaching.md), [DC-9](product/diagnostics-and-coaching.md), [DC-10](product/diagnostics-and-coaching.md), [DC-11](product/diagnostics-and-coaching.md), [DC-12](product/diagnostics-and-coaching.md), [DC-13](product/diagnostics-and-coaching.md), [DC-14](product/diagnostics-and-coaching.md), [DC-15](product/diagnostics-and-coaching.md)), [`ui-behaviour.md`](product/ui-behaviour.md) ([UI-1](product/ui-behaviour.md), [UI-2](product/ui-behaviour.md), [UI-3](product/ui-behaviour.md), [UI-4](product/ui-behaviour.md), [UI-5](product/ui-behaviour.md), [UI-6](product/ui-behaviour.md), [UI-7](product/ui-behaviour.md), [UI-8](product/ui-behaviour.md), [UI-9](product/ui-behaviour.md), [UI-10](product/ui-behaviour.md), [UI-11](product/ui-behaviour.md), [UI-12](product/ui-behaviour.md), [UI-13](product/ui-behaviour.md), [UI-14](product/ui-behaviour.md), [UI-15](product/ui-behaviour.md), [UI-16](product/ui-behaviour.md), [UI-19](product/ui-behaviour.md), [UI-21](product/ui-behaviour.md), [UI-22](product/ui-behaviour.md)).

**Also at this stage (written elsewhere):** [UI-18](decide.md) an explicit yes on a visual convention · [UI-20](decide.md) the lighter design route · [AG-2](decide.md) put every open item · [AG-1](decide.md) decisions in plain English.

**Guidance for this stage:** [AG-28](../guidance/advice.md) a result in front of the owner first (agents).

[^ag-21]: Product Owner, 30 Oct 2026.
[^ag-22]: Product Owner, 21–22 Nov 2026.
[^d4]: Product Owner, 25 Nov 2026 (#195, decision D4): fix only blocking bugs and file the rest, everywhere; this replaces "fix it if it's within reach" (Product Owner, 11 Nov 2026).
[^ag-24]: Product Owner, 6 Nov 2026 (evidence) and 18 Oct 2026 (bugs live in GitHub issues; priority labels 24 Oct, task issues 21 Oct).
[^ui-17]: Design Lead, 17 Oct 2026.
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
