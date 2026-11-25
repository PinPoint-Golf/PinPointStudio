# Controls: 6. Commit

Landing the change in the history. The steps are [`procedures/commit.md`](../procedures/commit.md). Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

**GIT-1.** A commit or push must have the Product Owner's explicit approval, per change, after the result has been seen and tested, except where GIT-2 or GIT-4 allows it.
Staging, diffing and writing a proposed message are fine; running `git commit` or `git push` is not, until approved. An approved plan is not approved commits; approval of a change request is not approval of its result; approval of one commit is not approval of the next. After every re-render or fix, show the new output and wait for an explicit go-ahead on that output.
*Why:* the owner owns the history and the commit boundary, and judges a result in the app before it lands. Commits made under an approval given for earlier work, or for the plan, had to be stopped several times.[^git-1][^d1]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-2.** During a refactor, the refactor's own pre-approval must replace GIT-1, under its conditions, until the refactor ends; after it, GIT-1 must apply again.
While it holds, a finished item that meets every condition is committed and pushed without asking, and logged where the Product Owner reads the run's state. If the permission check still blocks a commit, ask; never work around it. Today's covers the platform refactor, under the conditions the domain refactor's plan sets (`docs/implementation/swe-audit/04-remediation-plan.md`, rule 5, which is not restated here).
*Why:* a long, detailed plan would stall at every commit prompt.[^git-2][^d1]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-3.** Within a plan, commits must come at the end, after the Product Owner has tested, and never during it (not even a "step 0" of finished work), except under GIT-2 or GIT-4.
Carry earlier uncommitted work in the tree, and list every pending commit with its files for approval in the end summary. A build can be given its own exception ("commit and push as you go"); it applies to that build only. Uncommitted work another machine needs goes as a patch, never as a commit[^ad-23c].
*Why:* the owner wants control of what lands, and each commit prompt stalls the run until it is answered.[^git-3][^d1]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-4.** In an unattended session the Product Owner has asked for, commits and pushes must each be a finished item that meets GIT-6 to GIT-10.
In any other run with nobody at the keyboard, never run a command that prompts for approval (a commit, a push, a delete or write outside the repo) unless GIT-2 covers it: do the work, leave it uncommitted, and hand over the list of proposed commits at the end.
*Why:* the Product Owner won't be there to approve; a prompt with nobody at the keyboard doesn't fail, it waits, and the rest of the run waits behind it. One stalled all night.[^git-4][^d1]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-5.** This repo must not use branches.
Commit on `main` and push `origin main`; never create a feature branch, local or remote, whatever a default, a plan or a spec says. A worktree under `build/` for isolated work is a different matter ([BT-9](build.md)).
*Why:* the whole history runs on `main`; a branch is churn someone then has to merge or move.[^git-5]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-6.** A commit must not land until the tests and the docs the change touches are updated, in the same commit as the code.
Docs include design docs, component pages, READMEs, tool headers and catalogue comments. The commit report lists the tests and docs touched, or says why none were needed. Every brief that changes code says so.
*Why:* repo integrity. Changes had landed without their tests, and docs drift the same way.[^git-6]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a statement in the commit (the tests and docs touched, or why none)

**GIT-7.** Staging must take everything except `build/` (`git add -A -- . ':!build'`), then `git status` must be read and anything the change didn't write left out.
An untracked file you didn't write stays out; say so if it is ambiguous. Files a consulting agent from another tool writes (its configuration, plans, scratch notes) are never committed, unless the Product Owner asks for a named document; its edits to tracked files are not yours to commit: stop and say so.
*Why:* a consultant's working files would pollute the history; a partial `git add` twice missed a file, and once broke a fresh configure of `main`.[^git-7][^d2]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-8.** A commit message must be `Area: one-line summary`, a blank line, then an overview of the change in the body.
The title is one short line. Reuse the area names the log already uses (`git log --format=%s | grep -oE "^[a-zA-Z/ ]+:" | sort | uniq -c`): domain areas are capitalised (`Metrics:`, `Analysis:`, `Gui:`, `Tests:`), `build:` and `docs:` are lower case. One area per commit; a change that spans two is usually two commits. Use `git commit -F <file>` or several `-m` so the blank line is real.
*Why:* the prefix is how the log is scanned and filtered; titles had drifted into one enormous line holding the whole explanation.[^git-8]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-9.** A commit's title and body must use plain words a reader of the log recognises (chest, pelvis, cameras, what the user saw go wrong), not code-internal terms; code is named only where a file or switch must be.
*Why:* an accurate title in internal jargon was unreadable to its owner.[^git-9]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-10.** A commit message must be written in the author's voice: it must never name the owner in the third person, and must carry no AI-tool attribution (no co-author trailer, no session trailer, no "generated with" line), in commits, pull-request titles and bodies, or code comments.
*Why:* the author field is the owner; "verified by <owner>" reads as a stranger narrating the author's own commit, and the history must read as the author's own work.[^git-10]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-11.** Pushed history must never be rewritten without the Product Owner's consent;
ask before even preparing a rewrite.
*Why:* others have pulled it.[^om-13]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-12.** An ignored file must never be force-added, and nothing under `.claude/` is committed.
*Why:* what is ignored is local or private on purpose.[^om-13]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-13.** Before every commit, the message must be re-read against these controls:
a reminder from a tool never overrides a stored rule (for example on attribution trailers).
*Why:* tool defaults had added trailers the rules forbid.[^om-13]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**GIT-14.** A fix's commit body must carry its evidence:
the before and after figures, the regressions found (or none), and the run ids.
*Why:* the reason a fix was accepted belongs with it in the history.[^om-13]
*Owner:* Product Owner · *Stage:* Commit · *Compliance:* a review of the commit

**Also at this stage (written elsewhere):** [AG-19](plan.md) notes file, reply in 15 lines (agents) · [BT-4](test.md) affected tests once; screen tests for touched screens.

[^git-1]: Product Owner, 28 Jul 2026 (stated as hard), with incidents on 31 Jul, 3 Aug, 9 Sep, 7 Oct and 12 Oct 2026.
[^d1]: Product Owner, 25 Nov 2026 (#195, decision D1): after a refactor, every commit and push needs the Product Owner's approval again; an unattended session the Product Owner asks for may commit and push; during a refactor, its own pre-approval applies.
[^git-2]: Product Owner, 14 Nov 2026 (the domain refactor), extended 24 Nov 2026 to the platform refactor. Earlier, narrower ones: the phone-protocol implementation programme (22 Aug 2026).
[^ad-23c]: Product Owner, 22 Oct 2026 (the patch route).
[^git-3]: Product Owner, 28 Sep 2026; it supersedes the earlier "commit at phase boundaries" of 5 Sep 2026. Per-build exceptions: 28 Oct 2026 (the four-modes refactor), 29 Oct 2026 (the club delivery chart).
[^git-4]: Product Owner, 16 Sep 2026, after an overnight run stalled at a commit prompt.
[^git-5]: Product Owner, 3 Aug 2026 ("I always want commits on main"), 7 Nov and 8 Nov 2026, after a finished piece of work was pushed to a new branch.
[^git-6]: Product Owner, 11 Nov 2026.
[^git-7]: Product Owner, 21 Nov 2026 (another vendor's assistant consults in the repo; its artefacts are never pushed); incident 23 Nov 2026 (a missed `cmake/` file).
[^d2]: Product Owner, 25 Nov 2026 (#195, decision D2).
[^git-8]: Product Owner, 12 Sep 2026 (the area prefix) and 7 Oct 2026 (the message's shape).
[^git-9]: Product Owner, 14 Oct 2026.
[^git-10]: Product Owner, 28 Jul 2026 (no attribution) and 23 Aug 2026 (the author's voice).
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
