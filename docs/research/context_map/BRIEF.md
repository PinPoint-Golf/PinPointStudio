# Side track · Is a context map the right shape for R-CTX-1?

**A test, not a change.** The Product Owner's ruling (24 Nov): work this as a side track "to test and work it without impacting the refactor that has just happened or is in flight". It ends in an informed decision: drop it, remember it for later, or apply it now. Gemini reviews the result before that decision.

**Write only inside `build/context-map/`.** Never edit `src/`, `docs/`, `tools/`, `build/platform/`, the worktrees, or any issue. No builds, no commits, no app launches.

## The question
IM §2's R-CTX-1 (v1.10.1) orders the contexts Acquisition → Diagnostics → Coaching, with Domain + Application a hub reached through its contracts (Q26), and a Shared tag for Vocabulary that two contexts use (Q27). Within one morning it needed three carve-outs:
- the hub (Q26);
- the Shared tag (Q27);
- code tagged by deferral: the capture code in `Gui` stays Domain + Application until Stage D, and the drill pack stays Diagnostics until no Diagnostics file includes it.

The coordinator's hypothesis: the flaw is that a data-flow chain (DEC-48: "contexts and rings are different cuts") was promoted into a dependency rule. The better shape may be a **context map**, as in bounded contexts: contexts depend on each other only through a **named contract** or the **shared kernel**, and "left to right" follows from each contract's direction. **Test the hypothesis; don't assume it.** Find the cases that break it as hard as the ones that fit.

## Read
- `docs/developer/information_model.md`, but never whole (about 100k tokens): §2 (to §2.4's end), §4 and §4.1, §1.4's lint table, and R-CTX-1 (grep).
- `git show 898b4892:docs/design/four_contexts.md`: the ruled amendment, in full.
- `docs/design/coaching_layer.md`: skim, for what Coaching will need from the others.
- `src/Gui/tests/architecture_lint_test.cpp`'s `fileRings()`, for how the lint maps files today.

## Do
1. **Draft the context map**: a table of every allowed dependency between contexts (Acquisition, Diagnostics, Coaching, Domain + Application, Shared, Platform). Give each its direction, the named contract it goes through (the Recording; the results contract; the hub's contracts; the shared kernel), and the IM ids that already define it.
2. **Check the code against it.** Using §4.1's Context column, extended with your own placements for its "to place in Stage B" rows (state each one), classify every `#include` that crosses between contexts in `src/`, not tests, by grep:
   - (a) fits a named relationship;
   - (b) fits only by a carve-out of today's R-CTX-1;
   - (c) fits neither, which is a real design problem.

   Give counts, and list every (b) and (c) with file:line. Script the count, and keep the script in `build/context-map/` so it can be re-run.
3. **Compare the two rules on the same includes:** today's R-CTX-1 with its carve-outs, against "only through a named contract or the shared kernel". Which rejects more real problems and fewer legitimate cases? Which is simpler for lint A9 to check?
4. **Look ahead to the coaching track:** the golfer model, the playbook, the practice log, the play source and external uploads. Which rule handles their dependencies with fewer exceptions?
5. **Recommend** one of: **drop it** (today's rule is enough), **remember for later** (worth it once Coaching has code), or **apply now** (before Stage B builds A9). Say what applying would change in IM (the ids), and what it would cost.

## Deliver
- `build/context-map/REPORT.md`: at most two pages, plain British English, written for an outside reviewer who has only this file. Facts with sources; the map; the counts; the (c) list; the comparison; the recommendation.
- `build/context-map/check_includes.py`, the re-runnable script.

Reply in 6 lines or fewer.
