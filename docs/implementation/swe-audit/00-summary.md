# 00 · Summary

## In plain words

**The numbers are mostly right; the structure that carries them is not.**

What is sound:
- The analysis and diagnostics **rules** — the detection engine, the three factors, the strike-versus-flight
  check, Sky, carry pools, the frozen baseline — are pure, unit-tested functions, each written once. Reviewers
  in every area said so independently.
- The swing document's storage is good: one door, atomic, verified writes, a dated schema reference.

What is weak is everything around the rules:
- **who owns** the results;
- **how long they live**;
- **how a thing is identified**;
- **how many times the same fact is worked out.**

## The one root cause

The domain is athlete → session → shot, with diagnostics at three levels (shot, session, athlete). **None of
those things is an object in the code.**
- An athlete is a name string, and its library folder is derived from that name in six places.
- A session is a folder path, spelled two ways and re-derived in five places.
- A shot is a row in one of two lists, numbered three ways on screen.
- The selected shot is held in five places under four definitions.

The session and shot diagnostics are computed inside a 6.6k-line model that **views create**. It does not
exist unless a panel is on, up to four copies of it exist at once, and it is rebuilt with a disk re-scan on
every Capture↔Replay switch. The athlete level is an app-wide controller re-pointed by hand when the athlete
changes. The same choice — the view owns the model — was then copied on purpose into the chart series, the
launch monitor session and the data viewer.

Almost every other theme follows from that:
- **Facts computed many times.** With no owner holding a fact, each surface reruns the rule on its own copy
  of the inputs. Some copies already disagree: two direction classifiers; report figures that ignore
  exclusions; handedness missed on three surfaces.
- **Identity bugs.** With no object to hold, consumers pass strings and translate between numberings: rename
  orphans history; "Shot 7" means three things; a selection leaks into the next session.
- **Persistence faults.** With no single owner of a session's file, four writers share `diagnostics.json`,
  and golfer input stored in that cache has already been lost once.
- **⚠ comments as patches.** Many of the ⚠ comments across these files document a workaround for exactly
  this.

## What was found

About 124 distinct findings after merging duplicates (138 raw), across eight areas.

| Severity | Distinct findings |
|---|---|
| Critical | **1** |
| High | **32** |
| Medium | ~63 |
| Low | ~28 |

**Adversarial passes:** 48 Critical and High findings were tested. None was wrong; 7 were lowered and 1 was
raised. Details are in `02-findings.md` and `findings/verification.md`.

### The Critical finding

- **H-04 — golfer input lost from a cache file.** Shot intents, corrections, the declared focus and the
  declared miss live in `diagnostics.json`, the same file as the regenerable ledger. A regrade sets that file
  aside and rebuilds it without them. An unreadable file is treated as absent, and then overwritten. **One
  real loss was found on the share:** the 10 Jul Wrist session's declared focus (`excessive_shaft_lean`)
  survives only in a 7 Aug backup.

### Behaviour defects a golfer or coach can meet today

Each is confirmed in the code; none was reproduced in the app.

| Finding | Defect |
|---|---|
| D-02 | With Session diagnostics off (the default layouts), **no live shot is graded until the session ends.** This is the fault that started the audit. |
| E-01 | "＋ New athlete" or renaming the current athlete mid-session saves the next swings under another athlete, in a new folder. |
| E-02 | Renaming an athlete makes their whole history disappear from the app. |
| E-03 | Editing an athlete's club mid-session reloads the live carousel and drops the selection. |
| E-05 | A selected shot carries into the next session and is matched to a different swing there. |
| F-03 | The session report's cover and numbers include shots the golfer left out on the LM board, under the wrong club. Its appendix excludes them. |
| F-05 / G-01 | The LM card and Club delivery can say "Pull" where the three factors and Tendencies say "Straight". |
| G-05 | Every card in a camera-only or LM-only session shows a red quality "0". |
| B-07 | A shot with no club set is graded at full confidence, though the app says such shots are "trusted less". |
| B-01 | A corridor edit in the Diagnostic Model screen takes effect only after a relaunch. |
| H-02 | **Re-analysis does not reproduce the live analysis.** The athlete's height is not recorded for a normal capture, so a re-analysed swing's 3-D skeleton is fitted without it. |
| H-01 | The replay's overall score always reads 0. |

## The top ten actions

| # | Action | Answers |
|---|---|---|
| 1 | Make **Athlete** and **Session** real objects that own their children and are replaced, never re-pointed. | Theme 1 |
| 2 | **One `SessionDiagnostics` per open session**, owned by the session, the single writer of its file, existing whether or not any view is on. | D-01…D-05, H-03 |
| 3 | Move **golfer input out of the cache file**, and make the ledger reader refuse to overwrite a file it could not read. | H-04 |
| 4 | **One identity per thing**: athlete uuid with a stored folder; `SessionIdentity`; `ShotRef` = (session, swing number) shown as "Shot N" everywhere. | E-02, E-06, F-06 |
| 5 | **One selection, as application state (`AppState`)**, scoped to the session on screen by `ShotRef`. The carousel summary that started this work then becomes one binding. | E-04, E-05 |
| 6 | **Each fact computed once at its level and read as a result** by the level above: shot outcome and factors, direction, handedness, usuals, LM figures. | Theme 3 |
| 7 | **One serialiser per analysis block and one header builder**, so live, disk, replay and re-analysis agree. | H-01, H-02, A-01 |
| 8 | **Fix the Stage 0 defects** listed above. | `04-remediation-plan.md` |
| 9 | **Move domain code out of `src/Gui`, I/O out of Coaching and Diagnostics, golfer English into one words module.** | Theme 5 |
| 10 | **Enforce it:** an `architecture_lint_test` beside the design-system lint, with an allowlist that can only shrink. | §6 of `03-target-architecture.md` |

## What is done well (keep it)

- **Pure, tested rules** in Coaching and the diagnostics engine. The engine's `detect`/`explain` are
  level-agnostic, with athlete baselines injected.
- **The swing document's storage:** one door (`SwingStore`), atomic and fsynced writes, verified conversion,
  a dated schema reference.
- **Analysis discipline:**
  - one analyzer for every swing;
  - stages gated on what the capture holds;
  - the worker reads no settings;
  - stage versions dated;
  - reuse reasons recorded in each document.
- **Charts infrastructure:**
  - immutable `shared_ptr<const>` snapshots;
  - honest missing values (NaN and "-", never 0) in Charts;
  - `PresenterPool`;
  - good test seams;
  - clean QML.
- **Fingerprinted derived caches** (work-ons, tendencies, themes).

## What happens next

`03-target-architecture.md` proposes the object model. It ends with **ten decisions only Mark can make**.
`04-remediation-plan.md` stages the work:
- **Stage 0:** the defects above, about a day, no new structure.
- **Stages 1–3:** identity, the Model layer, and the selected shot with the carousel summary.
- **Stages 4–8:** charts and report, persistence, layering, enforcement and size, in that order.
