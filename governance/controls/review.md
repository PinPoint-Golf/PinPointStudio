# Controls: 5. Review

Reviewing the change, and putting it before the role that approves it. An area reference: read it when doing this kind of work ([`AGENTS.md`](../AGENTS.md) has the index). Older text names four roles (Product Owner, Solution Architect, Design Lead, Coaching Lead); all four are the owner, so read a role's approval as "agree it with the owner first".

**AG-21.** An independent reviewer is used only for a genuinely risky stored-data migration, a destructive file operation, or tricky capture concurrency, and it is told to flag real bugs and data-loss risks only.
Routine work is implemented, tested and reported in one pass, with no reviewer.
*Why:* a reviewer on every step multiplied fix rounds, notes and edge-case issues without finding proportionate defects.

**AG-22.** Open a GitHub issue only for a real bug, a real data-loss risk, or concrete follow-up work worth doing; never for a contrived or theoretical edge case. Fix what is in scope; anything else worth knowing goes in the report.
*Why:* filing every review finding as an issue produced about 70 open edge-case and finding issues, most of them noise.

**AG-24.** A bug must be filed only for an observed failure (seen, or reported by a user); a predicted failure is never filed as a `bug`: an agent's "likely bug" is a prediction until checked. A prediction is filed at all only when it is a real data-loss risk or concrete follow-up worth doing ([AG-22](review.md)), labelled `finding` and marked "reproduce first"[^finding].
An observed, unexplained result that could change users' data is filed at once, without being asked, saying the cause is unknown. A bug found and not fixed by the end of a session is filed as a GitHub issue before the session closes, never written into an agent's memory: in plain English, with what goes wrong, how to recreate it and its definition of done, the `bug` label, an area label, a priority label and the milestone; close it with the fixing commit, which names its evidence ([GIT-14](commit.md))[^ag-24e]. Deferred work goes into its matching task issue.
*Why:* predicted failures filed as bugs read as known defects; a measured, serious shift went unfiled; one tracker that others can read is the record.[^ag-24]

**UI-17.** Every UI change must be shown as annotated before-and-after review sheets, never raw full-page renders: one sheet per change, a title and two or three caption lines (before, after, what to check), the BEFORE and AFTER panels side by side cropped to the region, boxes on exactly what changed, and a control panel where the claim is that a normal case is untouched.
Check each sheet yourself before showing it.
*Why:* fourteen near-identical full-page renders were unreviewable; the sheets made review fast.[^ui-17]

**GIT-15.** When a change is ready for the Product Owner to test, it must come with a short, targeted list of what to try: the parts at risk, not everything.
*Why:* the Product Owner tests before approving a commit ([GIT-1](commit.md)); on one change the useful list was four things that read a renumbered index, because the rest was pure deletion.[^git-15]

**AG-38.** On a crash report, a native debugger backtrace must be got before any diagnosis or code change.
*Why:* a guessed crash site was wrong; the backtrace named a different cause.[^om-13]

**AG-42.** A fix offered for a breach of a rule must remove the breach. A change that meets a rule's letter and not its purpose (moving part of the logic, or moving a read where the lint doesn't look) is not offered as a fix: the choice is the whole change, or an honest deferral with the breach named on its issue.
*Why:* a proposed "focused" fix moved only the calibration thresholds out of QML, which is not in the spirit of the rule; another would have moved a forbidden read out of the lint's sight.[^ag-42]

**Product controls** (what the product must do) a review checks the change against: [`analysis-and-data.md`](product/analysis-and-data.md) ([AD-1](product/analysis-and-data.md), [AD-2](product/analysis-and-data.md), [AD-3](product/analysis-and-data.md), [AD-4](product/analysis-and-data.md), [AD-5](product/analysis-and-data.md), [AD-6](product/analysis-and-data.md), [AD-7](product/analysis-and-data.md), [AD-8](product/analysis-and-data.md), [AD-9](product/analysis-and-data.md), [AD-10](product/analysis-and-data.md), [AD-11](product/analysis-and-data.md), [AD-12](product/analysis-and-data.md), [AD-13](product/analysis-and-data.md), [AD-31](product/analysis-and-data.md), [AD-32](product/analysis-and-data.md), [AD-34](product/analysis-and-data.md)), [`diagnostics-and-coaching.md`](product/diagnostics-and-coaching.md) ([DC-3](product/diagnostics-and-coaching.md), [DC-4](product/diagnostics-and-coaching.md), [DC-5](product/diagnostics-and-coaching.md), [DC-6](product/diagnostics-and-coaching.md), [DC-8](product/diagnostics-and-coaching.md), [DC-9](product/diagnostics-and-coaching.md), [DC-10](product/diagnostics-and-coaching.md), [DC-11](product/diagnostics-and-coaching.md), [DC-12](product/diagnostics-and-coaching.md), [DC-13](product/diagnostics-and-coaching.md), [DC-14](product/diagnostics-and-coaching.md), [DC-15](product/diagnostics-and-coaching.md), [DC-16](product/diagnostics-and-coaching.md)), [`ui-behaviour.md`](product/ui-behaviour.md) ([UI-1](product/ui-behaviour.md), [UI-2](product/ui-behaviour.md), [UI-3](product/ui-behaviour.md), [UI-4](product/ui-behaviour.md), [UI-5](product/ui-behaviour.md), [UI-6](product/ui-behaviour.md), [UI-7](product/ui-behaviour.md), [UI-8](product/ui-behaviour.md), [UI-9](product/ui-behaviour.md), [UI-10](product/ui-behaviour.md), [UI-11](product/ui-behaviour.md), [UI-12](product/ui-behaviour.md), [UI-13](product/ui-behaviour.md), [UI-14](product/ui-behaviour.md), [UI-15](product/ui-behaviour.md), [UI-16](product/ui-behaviour.md), [UI-19](product/ui-behaviour.md), [UI-21](product/ui-behaviour.md), [UI-22](product/ui-behaviour.md)), [`capture-and-session.md`](product/capture-and-session.md) ([AD-50](product/capture-and-session.md)).

**Also at this stage (written elsewhere):** [BT-39](test.md) a test names what it proves · [BT-40](test.md) a test that can't fail is no proof · [UI-18](decide.md) an explicit yes on a visual convention · [UI-20](decide.md) the lighter design route · [AG-2](decide.md) put every open item · [AG-1](decide.md) decisions in plain English.

**Guidance for this stage:** [AG-28](../guidance/advice.md) a result in front of the owner first (agents).

[^ag-21]: Product Owner, 30 Oct 2026.
[^ag-21d]: Product Owner, 26 Nov 2026 (the programme runs attended, one step at a time): one fresh review is kept for the risky steps, and a fix from a review is not reviewed again; the wording approved the same day. It replaces the same day's earlier rulings that every item, then every chunk, is reviewed (the memory audit, item 14) and that a code fix gets a fresh reviewer.
[^finding]: Product Owner, 26 Nov 2026 (the memory audit, contradiction 18): a finding nobody has seen is filed as a `finding` issue, "reproduce first", never as a `bug`; this settles AG-22 against AG-24.
[^git-15]: Product Owner, 31 Jul 2026; a control from 26 Nov 2026 (the memory audit, item 15).
[^ag-22]: Product Owner, 21–22 Nov 2026.
[^d4]: Product Owner, 25 Nov 2026 (#195, decision D4): fix only blocking bugs and file the rest, everywhere; this replaces "fix it if it's within reach" (Product Owner, 11 Nov 2026).
[^ag-24]: Product Owner, 6 Nov 2026 (evidence) and 18 Oct 2026 (bugs live in GitHub issues; priority labels 24 Oct, task issues 21 Oct).
[^ui-17]: Design Lead, 17 Oct 2026.
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^ag-42]: Product Owner, 25 Nov 2026 ("the qml calibration is all or nothing; the option you proposed is not really in the spirit of the rule").
[^bt-41]: Product Owner, 25 Nov 2026 ([POL-9](../AGENTS.md): a fix's test is shown failing before the fix). #279's review for `test_23`. The control and its wording approved by the Product Owner and the Solution Architect, 26 Nov 2026.
[^bt-41b]: Product Owner, 26 Nov 2026, with the steps that get no fresh review ([AG-21](review.md)).
[^ag-24e]: Closing an issue is a "fixed" claim ([POL-9](../AGENTS.md)): approved by the Product Owner and the Solution Architect, 26 Nov 2026.
