# PinPoint Studio test strategy

| Document control | Value |
|---|---|
| **Owner** | Solution Architect[^owner] |
| **Home** | `governance/artefacts/test_strategy.md`, a governing artefact, hand-edited |
| **Governs** | What counts as evidence that the app does what a requirement says: the kinds of evidence, which kind can prove which class of requirement, how long each lasts, what makes a test count as proof, and how the requirements traceability matrix (RTM) reads it. It governs *what proves*, never *how to write or run a test*: that is the testing developer guide, [`docs/developer/testing_developer_guide.md`](../../docs/developer/testing_developer_guide.md), which stays |
| **Applied by** | Policy **POL-9** ([`../policies.md`](../policies.md)) and controls **BT-38** ([Plan](../controls/plan.md)), **BT-39** and **BT-40** ([Test](../controls/test.md)), **BT-41** ([Review](../controls/review.md)) and **BT-42** ([Release](../controls/release.md)). The RTM's check, `rtm_check` (once built), enforces the parts a script can see |
| **Reads with** | [`requirements.md`](requirements.md) (the needs, their status values and owners); [`uc-map.csv`](uc-map.csv), which the matrix reads beside the requirements, for each requirement's use cases[^sa-afternoon]; `governance/artefacts/rtm.md` (generated), once it lands; [`lifecycle_model.md`](lifecycle_model.md) (its evidence labels: *observed*, *seam, in a worktree*, *reading*); [`rtm_observations.csv`](rtm_observations.csv), the observations register (§7.2) |
| **Status** | v1.2, 26 Nov 2026; v1.0 approved, 26 Nov 2026[^owner]. A sentence still marked "(proposed, for the *Role*)" waits on that role's later ruling |

**How to read it.** §1 is the rule. §2 and §3 say which evidence can prove what. §4 says how long it lasts. §5 says when a test counts. §6 says where each level runs, by pointing to the controls. §7 defines the observation record. §8 is how the RTM uses all of it. §9 is the audit of today's tests against this page.

**Words used here.**
- **The default gate:** the two-pass full run on a platform, `ctest -LE qml_ui` then `ctest -L qml_ui` (BT-4), less the exclusions its runner names (on a remote test machine, the list in its committed runner).
- **Green**, for a gate: every test in it passes, apart from those in the accepted-gaps register, each listed there with its issue and its owner (a standing red such as #231's is green only while it is on that list).
- **An oracle:** the answer a test checks against, known independently of the code under test (BT-7).

---

## 1. The rule

**Nothing is called delivered, fixed or working without evidence: a passing test, or an observation recorded against a named build.**[^rule]

Anything else is "built, not verified". That is the honest word for work that compiles, reads right and has not been shown to work. It is not a failure: it is a status, and it says what is missing.

**Not evidence**, however convincing it looks:

- Reading the code or the QML. It predicts; it does not observe (AG-25). A red "found by reading the old code" is not a red run either.
- A build that compiles, or a lint that passes. They prove the code is well formed, not that it does the thing.
- An agent's report. It is a claim until the coordinator's own build and ctest agree (AG-18), and a reviewer has checked it (BT-41).
- A design document's "built" line, a closed issue, or a commit title.
- A test that cannot fail (§5.2). It passes whether or not the thing works.
- A test that was skipped, did not configure, is outside the default gate of the platform in question, or failed (§4).
- A recorded or re-recorded golden file, or a newly taken baseline, on its own. The recording is not a green run; the evidence is the difference from the last one, explained (§4).
- A failure reproduced on a clean tree. It proves the change did not cause it, not that the test is broken (BT-3).
- A run on a build other than the one the claim names (§7.1).

The same rule runs the other way for failures: a bug is filed only for a failure seen or reported (AG-24, POL-6).

## 2. The kinds of evidence

Seven kinds. The first six are the list in the brief for the first draft[^kinds]; the seventh is added because nothing in the six covers install and update. The kinds, their lifetimes (§4) and the conditions for proof (§5) are approved[^sa-approval].

| # | Kind | What it is | Where it runs | Its own conditions |
|---|---|---|---|---|
| E1 | **Unit and integration tests** | A ctest test over code, with fakes or seams for devices, stores and time | Each platform's default gate | §5; BT-3, BT-6, BT-7, BT-22 |
| E2 | **Golden traces** | A frozen record of a routine's inputs and every output it made, replayed and compared exactly (the three calibration routines' traces are the model) | As E1; an opt-in suite counts only as §4 says | §5; the record is re-made only on purpose, with the difference explained |
| E3 | **Parity and validation runs** | *Parity:* the analysis and diagnostics output of a named build over a fixed set of real swings and sessions, compared with a baseline: unchanged, or changed on purpose (`tools/parity/`). *Validation:* the same output compared with a criterion (a truth set, the launch monitor, the HackMotion) | A development machine for the session half; the primary platform's release build for anything that re-runs pose (AD-23) | AD-20 (the noise floor), BT-21 (a second backing); for validation AD-14, AD-15, AD-33, DC-7 and the release dashboard (`docs/validation/release_status.md`) |
| E4 | **Screen tests and headless probes** | *Screen tests:* the QML tests (`qml_ui`), offscreen, pressing real components. *Probes:* the real app started headless (`tools/probe/run_headless.sh`), driven by a probe script, with the fix taken out for the A side. *Review sheets* (UI-17) record what a screen shows | Each platform's gate (screen tests); macOS (probes) | §5 for screen tests; AG-15, AG-16, AG-25 (A/B) for probes; UI-17 for sheets |
| E5 | **Primary-platform runs** | The default gate on the primary platform, Windows, on a test machine of that platform through the project's committed runner for it (the machine and its runner are in the local agent file), plus anything only that machine can show: timed runs, GPU memory beside the simulator software (BT-13), sweeps (AD-23) | A test machine of the primary platform | As E1; BT-13 for performance |
| E6 | **Visit observations** | What a person at the capture rig saw the app do on a named build: real shots, hands on the sensors, the real cameras, launch monitor and phones | A capture rig, at a visit | §7; a visit holds only the steps BT-43 allows[^visit]. A BT-16 device probe (the device alone, the app closed) supports; only an observation of the app at the capture rig proves |
| E7 | **Release checks** | The checks on the packaged and published build. **Before publishing:** the bundle launches (BT-26), Gatekeeper (BT-27), the signature over the uploaded bytes (BT-28), the firewall rule on a clean machine (BT-34); these count for BT-42. **After publishing:** the update feed as published (BT-11); its observation is recorded on the release, and a failed BT-11 check reopens the release under its procedure | Each platform, at release | The release controls; recorded as an observation (§7) on the release's build |

E1 to E4's tests can be written once and re-run. E3's runs, E4's probes, and E5 to E7 are observations: each is true of the build it was made on.

## 3. Which kind can prove which class of requirement

A requirement is proved only by a kind that can see what it promises. A kind marked "supports" can show the logic is right but cannot by itself make the requirement delivered.

| Class of requirement | Typical areas (`requirements.md`) | Proved by | Supports only | Why the others can't |
|---|---|---|---|---|
| **Rules, data and logic**: a computation, a file's contents, a grading, a decision | ANA (formulas), DIAG, LIB, HON (what is stored), REL (the logic of not losing work) | E1 | E3 | A rule is checkable on its own, against an oracle: a stated invariant, a round trip, or two producers agreeing (BT-7) |
| **Behaviour kept exactly**: a refactor or a second backing changes nothing | Any, during a refactor or port | E2; BT-21's parity test | E1 | Only a frozen record of the old behaviour can show "the same" |
| **Analysis output on real swings**: unchanged, changed on purpose, or accurate | ANA, DIAG, COACH (what is graded) | E3 (parity for "unchanged"; validation for "accurate") | E1 | Fixtures stand in for real swings only until the pipeline moves (BT-8); parity proves unchanged, never correct |
| **What the app shows and does**: a screen, a flow, a word, a warning | REV, ACC, HON (what is shown), CAP and DEV flows, PRIV (what the app says) | E4 | E1 | Reading QML is not evidence (AG-25); a model test misses the panel's wiring. A screen test over a fixture proves the panel; the wiring needs a screen test over the real controller or one recorded probe[^sa-approval] |
| **The main platform**: speed, memory, the GPU, anything Windows-only | PERF, INST (Windows), the Windows half of every area | E5 | E1, E4 on macOS | A macOS run says nothing about the primary platform's GPU or file system (BT-13; #244's cases, §5.2) |
| **Hands, shots and real devices**: detection, connection, calibration, the voice, a device lost mid-session | CAP, DEV, the device halves of REL and COACH | E6 | E1, E2, E4 with fakes; a BT-16 probe | The headless start reaches no device (BT-16); a routine's golden trace proves its logic, not that it works on the real sensor. A visit observes the app working with a sensor; it never asks the owner to characterise the sensor, and a wrong reading is proved AD-11's way, by a recorded run through the real solve (E1 or E2) |
| **Install and update** | INST | E7, and E6 for a first start at the capture rig | E5 | Only the packaged or published bytes show what users receive (BT-11, BT-28) |

**A requirement can need more than one kind.** A device routine needs E1 or E2 for its logic and E6 for the real sensor; a screen that shows an analysis number needs E1 for the number and E4 for the screen. It is delivered only when every kind it needs has evidence. Which kinds a requirement needs is written on its row (§8), set when the row is approved; the class above is the default.

**A requirement is a hardware requirement** when any kind it needs is E6: its proof needs a real shot, a person's hands or a real device. The words are the visit rule's[^visit].

**A corpus swing meets E6.** Where a requirement needs E6, an E3 run over a corpus swing that shows the functionality meets it too, in every area, capture and devices included: the swing was a real shot, taken on the real devices[^sa-corpus]. In the observations register it is a line of Kind E3 whose Record reads `corpus: <session date> shot <n>, captured on <commit>`: the session date, the shot number and the build that captured the swing, never the golfer (POL-7); its Build is the build that replayed it (the line's form proposed, for the Solution Architect).

## 4. How long evidence lasts

| Evidence | Lasts | Ends when |
|---|---|---|
| A test (E1, E2's replay, E4's screen tests) in the default gate of a platform the requirement names | **Every run:** current for any build on which *that test* passed, in a run that configured every test it should (BT-22), on that platform. Another test's red does not cancel it | It fails, is skipped, is not configured, leaves the default gate, or is shown unable to fail (§5.2, BT-40) |
| A test that is opt-in, or not in the default gate of a platform the requirement names | For that platform, the recorded run's build only, like an observation | The next build |
| A golden trace's frozen record (the yardstick) | Until it is re-made | Re-making it is a change to the oracle: it lands only with the difference from the last record explained, line by line, and approved by the owner of the behaviour |
| A parity baseline (the yardstick) | Until it is retaken | A retake lands only with the diff (`parity_diff.py`) explained, every line. A baseline also lapses when its set no longer represents the library (BT-8) |
| A parity run of a named build against the baseline (E3) | That build only | The next build |
| A validation result (E3) | Until the stage it measures changes version (version-gated re-analysis), which a script can check | That stage's version changes |
| A headless probe (E4) | Its build only | Any change; to last, it becomes a screen test |
| A primary-platform run (E5) | Its build only | Any change; a test it ran is current as a test, above |
| A corpus swing that meets E6 (§3) | **No build or age limit:** it showed the requirement met, and the build that captured it is kept as a record only | Never by age or a later build. A regression shows as an open bug naming the requirement, beside the evidence, never by removing it[^sa-corpus-age] |
| A visit observation (E6) | **Its build only** | The next build. Between releases, and at a release that does not re-observe it, it stays on the requirement's row as "last observed on build X", with no time limit, except an age limit the Product Owner sets for safety-critical requirements (lost work, lost shots). Each release re-observes the smoke checklist (BT-42) |
| A release check (E7) | That release | The next release |

**The platform matters.** At 25 Nov 2026 the primary platform's default gate, on its test machine, ran 386 tests where macOS lists 405 names (*seen* in that machine's run of 25 Nov 13:54 and its configure log; the 387 of a later run is one more test that run's patch probably added, *not checked*). The 19 are explained:

| Tests | Why they don't run on Windows | Remedy |
|---|---|---|
| The phone-link conformance test and the nine phone-link interoperability tests (10) | Their conformance and simulator tools are not built on the primary platform's test machine; each configure says so | Build the tools there. BT-22's case: a suite that shrank for a missing dependency |
| Three phone-link and shot-controller tests (3) | Guarded off on Windows in their CMake files; two were never compiled with the Windows compiler | Porting work |
| The self-tests of three macOS shell tools (3) | macOS-only by design | None |
| Three tools' tests (3), registered but excluded | The primary platform's test runner excludes them because they can reach the real settings on Windows | Known; written in the runner's header |

So **the phone-link conformance and interoperability tests never run on the primary platform, where the phones pair**, and nothing they prove is proved on Windows. A test that never runs on the main platform is a line the catalogue's audit reports (§9). In the same runs, one test was skipped (reason not checked) and two screen tests were red; one of those reds is #231 (an edge case, open). The three calibration routines' golden traces sit in the opt-in setup suite, which runs nowhere by default.

## 5. What makes a test count as proof

A test proves a requirement only when all six hold. Five of them already exist as controls; this page only gathers them.

1. **It names what it proves.** One `pp_requirement(<test> …)` call per test, beside its registration, naming each requirement it proves (`REQ-ANA-8`) or each control it checks (`CTL-UI-1`, `CTL-BT-7`); for a QML test, `// REQ:` lines in its file. This is the requirements notes' phase 2 form. A `CTL-` label names a control id from `governance/controls/`; a lint test names the control that points to its rules. A test with no label proves nothing in the RTM's eyes (BT-39).
2. **It has been seen to fail.** A test that has never been red has not shown it can tell working from broken (§5.1).
3. **It checks against an independent oracle**, never a record of today's answer (BT-7).
4. **It gives the same verdict twice.** It is deterministic, or its noise floor is measured and its tolerance stated (AD-20; L-21 for intermittent failures). A test whose verdict varies is not proof until that is fixed.
5. **It is silent** (BT-6).
6. **It runs through ctest** (BT-3), in the default gate, on the platforms its requirement names (§4).

### 5.1 Seen to fail: the four forms

| The work | The red | Where it is recorded |
|---|---|---|
| **A fix** | The new or changed test, run on the code before the fix, failing on the fault the fix is for | The item's notes (the red log's name) and the fix's commit body (GIT-14) |
| **A new requirement or feature** | The test written before the behaviour, red (BT-7's "deliberately red") | As above |
| **Existing behaviour** (a refactor, a golden trace, labelling an old test) | A negative control: break the code or the fixture on purpose, see the test go red, restore it (the Witmotion routine's trace did this: one verdict edited, one input file removed, both red, both restored) | The item's notes |
| **A fault that shows only on a device, at the capture rig or in a timing race** (#280, #281) | The failure observed on a named old build (the bug's own evidence, AG-24), then an observation on the fixed build; plus a test of the logic, red first, where one can be written | The observations register (§7) and the item's notes |

A red found by reading the old code is not a red run: it is a prediction.

A test that is red only for a *different* fault proves that fault, not the one it is named for. #279's `test_23` is the case: it failed on a regression found during the work, and passes on the code before the fix, so it guards the regression and is not proof of #279's original fault (`test_20` is). It is not vacuous; it proves something else, and a reviewer checks which (BT-41).

### 5.2 A test that cannot fail

A test is **vacuous** when it stays green with the thing it proves broken. It proves nothing and must never be kept as proof: it is strengthened or retired (BT-40). Two of the examples the Product Owner named are of this kind[^vacuous]; the third, #279's `test_23`, is §5.1's case (red, but for another fault):

- **#244, the forced save failures on Windows.** The cases forced a save to fail by changing a file's or a folder's permissions, which Windows ignores. Where a case then checked that the save failed, it went red on the primary platform's test machine, which is how the fault showed. Two did not: the review found, by reading, that one case (`C15k`) passed on Windows whatever the product did, and that another (`UNR-b3`) probably did. Both now hold the file open in a way Windows honours and check that the refusal took, or read the product's own write counts. `C15k` was vacuous **on Windows only**: a break checked on macOS would have gone red and passed it.
- **`swing_doc_test`'s folder scan.** Its list of analysis folders included one that a move had renamed, and the loop did not check that each folder existed, so it silently stopped scanning it and still passed. It now fails when a listed folder is missing.

The pattern in both: the test's set-up could fail to bite without the test noticing. **A test that sets up a condition (a refused file, a missing folder, a fault) checks that the condition took.** And a break check runs on each platform the requirement names (§9).

## 6. The levels, and where each runs

This page does not restate where tests run; the controls do. In order of cost:

| Level | Where | Ruled by |
|---|---|---|
| The working loop: the tests a change affects, once, at the end | A development machine, through the one-at-a-time lock | BT-4, BT-15, BT-2 |
| Screen tests for the screens a change touches | A development machine | BT-4, BT-5 |
| Probes and renders | A development machine (macOS), headless, guarded | AG-15, AG-16, AG-25, BT-14 |
| The commit gate: the full suite in two passes when the change reaches far | A development machine | BT-4, GIT-6 |
| The primary-platform run | A test machine of the primary platform, Windows | BT-13, AD-23; [`docs/developer/testing_developer_guide.md`](../../docs/developer/testing_developer_guide.md), its section on that machine |
| Parity and validation | A development machine, and the primary platform's release build | AD-20, AD-23, AD-24 |
| A visit | A capture rig | BT-16; BT-43, the visit rule[^visit] |
| The release gate | Each platform, a fresh test tree | BT-17, BT-25, BT-29 |

## 7. The observation record

An observation (E3's runs, E4's probes, E5, E6, E7) is evidence only if it is written down against a named build, in a place the RTM can read without the network.

### 7.1 A named build

A build is named by: the commit; for an uncommitted patch on top, the patch's SHA-256; the platform and architecture; the build type (Debug, Release, installer). An installer is named by its commit, as BT-12 and BT-33 already name it. A published release is also named by its version and build number (BT-24). "Latest main" is not a name.

**When two builds are the same build.** Committing an observation makes a new commit, so a release commit can never be the commit an observation names. An observed commit therefore matches a later one when the later one descends from it and every change between them is to an evidence file (the observations register, the accepted gaps, the baseline, the issue snapshot, the generated RTM) or to release metadata (the version lines of `src/Core/version.h`, and nothing else in that file). A script checks that, from `git diff`; a person never judges it. A packaging-only release may also match: when the changes are only evidence files, release metadata, the installer scripts and the update-feed files, a committed list the Product Owner rules (proposed, for the Product Owner).

### 7.2 Its fields and its home

One record per observation, one line per requirement it covers; the lines of one observation share its id.

| Field | Holds |
|---|---|
| Id | `OBS-n`, never reused |
| Date | The day it was made |
| Requirements | The `REQ-…` ids it bears on |
| Build | §7.1 |
| Kind | E3, E4, E5, E6 or E7 |
| What was done | One plain line: the step, the device, the shot |
| What was seen | Pass or fail, and one plain line of what happened |
| Record | Where the log, sheet or summary is kept |
| Observed by | The role, or the agent role and the role who confirmed it |

Home: [`rtm_observations.csv`](rtm_observations.csv), committed beside the RTM's other inputs[^obs-home]. Its header is the nine fields above, in that order[^sa-afternoon]. **This register replaces phase 2's `verified:` entries; `requirements.md` rows carry none.** A visit's summary is written so it can be committed with it: no golfer's name or athlete id, no machine name, no share or home path (POL-7). A shot is named by its session date and shot number.

A failed observation is recorded too. It is evidence, of the opposite.

## 8. How the RTM uses all this

The RTM's design is the requirements notes' phase 2: generated from source files only, committed, checked by `rtm_check`, with an accepted-gaps register and a baseline that may only shrink. This page adds what it checks evidence against[^sa-approval]:

1. **Each requirement row names the kinds of evidence it needs** (E1–E7), defaulting to its class (§3).
2. **The evidence present** is read from committed source only: the `pp_requirement` calls and `// REQ:` lines, grepped from the source tree, for E1, E2 and E4's screen tests; `rtm_observations.csv` for the observations. A labelled test's kind is inferred: the `qml_ui` label means E4; a `golden` label (to add) means E2; anything else is E1.
3. **Which platform runs a test.** A source grep counts a test however it is guarded, so it would count the ten phone-link tests in §4 as Windows evidence. Each platform's release gate (BT-25) therefore writes the list of tests it configured to a committed file, one per platform (`governance/artefacts/rtm_platform_tests/<platform>.txt`), and BT-22's count check compares each later run on that platform with it. The RTM reads those lists to say which platforms a test is evidence on.
4. **Two modes.** Every day, `rtm_check` asks only that each piece of evidence a requirement needs *exists*: a labelled test in a platform's gate list, or an observation on some named build, shown with its build and date. At release (`--release <commit>`), the hardware rows are checked as BT-42 says; BT-11's after-publishing check is left out. So a visit does not turn the matrix red the day after it is recorded.
5. **A test counts while it passes.** The RTM cannot see pass or fail; it shows which tests prove what. Each run supplies "passing" for each test (§4); the release gate needs the whole gate green ("Words used here", at the top).
6. **The status follows the evidence.** This replaces phase 2's rule that one labelled test or one observation makes a row delivered: a row with every kind it needs present can be "delivered"; a built row missing one is "built, not verified", and names the missing kind. The status values themselves are `requirements.md`'s.
7. **What it flags.** This replaces phase 2's flag "a delivered, built or partial row with no test" with **"a delivered, built or partial row without evidence of every kind it needs"**. Two flags are added: **a test labelled for a requirement but in no platform's gate list**, and **a hardware row never observed**.
8. **Gaps.** Accepted gaps are the register's lines, each with its reason, its approving role and date; a standing red in the gate is one, with its issue and owner. Today's gaps sit in the baseline, which only shrinks. A platform whose hardware is never observed (§3) is one standing gap per platform, ruled once, not one line per requirement per release.

## 9. The catalogue's audit

The catalogue of tests (405 names on macOS at 25 Nov 2026, holding about 1,450 test cases plus the hand-rolled checks) was built before this page, and some tests may prove nothing. Each test is audited once against it[^audit].

**What is recorded for every test:**

| Field | How it is found |
|---|---|
| **What it proves** | The requirement ids or control it proves, and its evidence kind (E1, E2, E4). From reading the test, starting from the RTM seed's links by name |
| **Where it runs** | The platforms whose default gate holds it. **A test that never runs on the main platform is reported**, with why (§4's table) |
| **Whether it can fail** | A break-the-code check on a sample: break what it proves (a mutated line, an edited fixture), run it, see it go red, restore. A test that stays green is vacuous (§5.2). The break runs **on each platform the requirement names**, since a test can be vacuous on one only (`C15k`). **The sample:** every test that is the only proof of a requirement, every test whose assertions look like a recorded answer, and at least one in ten of the rest in each suite, and at least two |
| **Its cost** | Its run time, from the run logs |
| **Its failure history** | Only from repeated runs of one build. The record is thin: the primary platform's test runner keeps a run's folder only when the run fails, the kept runs cover two days (24–25 Nov) of patched worktrees over at least three base commits, and the development machine keeps no ctest history. So no failure rate can be computed and a flake can't be told from a patch's own red; this field says so where it applies |
| **What it has caught** | From the history: the commits that changed it beside a fix. No red runs are recorded before 24 Nov, so this is weak evidence, shown but not used for the verdict |

**Then one verdict:**

| Verdict | When |
|---|---|
| **Keep and tag** | It proves something, can fail, and is worth its run time: it gets its label |
| **Strengthen** | It proves something but could not fail, or its oracle is weak (BT-7): fixed, then tagged |
| **Merge** | Two tests prove the same thing with the same oracle: one's checks are folded into the other, and the emptied test goes |
| **Retire** | It proves nothing a requirement or control needs, or costs more than it could catch |

**Retirements and merges go to the Product Owner as a list, each with its reason, and no test is deleted before that approval.** A strengthened, merged or retired test's verdict, and the break that found it vacuous, are recorded in the audit's notes and in the commit that changes it.

---

## Change log

| Version | Date | Change |
|---|---|---|
| 0.1 | 25 Nov 2026 | First draft, proposed, for the Solution Architect; the catalogue's audit added at the Product Owner's request |
| 0.2 | 25 Nov 2026 | After the first review: no athlete ids; the RTM reads the source; a test's evidence stands on its own pass, and "green" allows accepted gaps; pre- and post-publish release checks; the Windows gap explained; four forms of "seen to fail"; the yardsticks split from the runs; the audit's sample, platforms and thin history |
| 1.0 | 26 Nov 2026 | Approved, and moved here from the programme's working folder; owner the Solution Architect. The marker on the register's home removed (approved); the others kept, for the Solution Architect's confirmation or a later ruling. Machines named generically (POL-7): the primary platform's test machine, a development machine or macOS, and the capture rig; no rule changed. The observations register created, empty, with §7.2's header. The visit footnote points to BT-43 |
| 1.1 | 26 Nov 2026 | The observations register's nine fields are approved, and their marker removed (§7.2). The matrix reads `uc-map.csv` beside the requirements ("Reads with"). `[^visit]`: BT-42's smoke checklist is ruled against the visit rule; §3's "hardware requirement" is still open |
| 1.2 | 26 Nov 2026 | A corpus swing that shows the functionality meets E6, in every area (§3), with no build or age limit (§4); the register line's form proposed, for the Solution Architect |

[^owner]: The Product Owner, 25 Nov 2026, asked for a test strategy artefact owned by the Solution Architect. The Product Owner accepted the strategy, 26 Nov 2026 ("The content is fine"; "I am happy"); the Product Owner and the Solution Architect approved its controls, its home here and its owner, the Solution Architect, 26 Nov 2026.
[^rule]: Product Owner, 25 Nov 2026 (the "killer rule"), after the requirements draft called 91 needs "delivered" by reading the code; with it, that a fix's test is shown failing before the fix, and the governance form (a policy, this artefact, and controls at Plan, Test, Review and Release).
[^kinds]: The brief for the first draft, 25 Nov 2026, written for the Product Owner; the strategy, with its seven kinds, accepted by the Product Owner, 26 Nov 2026; the kinds, their lifetimes and the conditions for proof were confirmed by the Solution Architect, 26 Nov 2026 (§2).
[^visit]: Product Owner, 25 Nov 2026, after visit 1: a visit holds only the steps that need a real shot, a person at the rig or a real device. Narrowed by the Product Owner, 26 Nov 2026, to steps that need a real shot: [BT-43](../controls/test.md) is the visit rule. BT-42's smoke checklist reads against it as ruled: its steps that need no shot are automated or checked at a desk before the release, and only its shot steps stay at the capture rig (the Solution Architect and the Product Owner, 26 Nov 2026: the Solution Architect's afternoon batch, item 11). How §3's "hardware requirement" reads against the narrowed rule is still open (proposed, for the Solution Architect and the Product Owner).
[^vacuous]: Product Owner, 25 Nov 2026, naming three examples (#244, #279's `test_23`, `swing_doc_test`'s folder scan). The facts: #244's item notes and review (`C15k` and `UNR-b3`, by reading; both made real); #279's review (`test_23` failed on build 4 and passes on the code before the fix); the platform refactor's B1.5 notes (`swing_doc_test`'s folder list).
[^audit]: Product Owner, 25 Nov 2026: the catalogue may need rationalising to match the strategy; every test gets what it proves, whether it can fail, its cost and what it has caught, then one verdict; retirements need the Product Owner's approval as a list with reasons.
[^obs-home]: Solution Architect, with the Product Owner, 26 Nov 2026: the evidence register's home.
[^sa-approval]: Solution Architect, 26 Nov 2026: approved §2 (the kinds of evidence, how long each lasts, and the conditions for proof), §3's wiring row, and §8's lead-in.
[^sa-afternoon]: Solution Architect, 26 Nov 2026 (the Solution Architect's afternoon batch, items 9 and 10, each as recommended): the evidence register's nine fields (§7.2); the matrix reads `uc-map.csv`.
[^sa-corpus]: Solution Architect, 26 Nov 2026, on the evidence each area needs ("if the corpus contains a swing that demonstrates the functionality, that is a pass too"), and on its reach ("of course it means capture too: where else would the data have come from?").
[^sa-corpus-age]: Solution Architect, 26 Nov 2026 ("this is evidence the requirement was met. A regression does not take that away"): a corpus line's evidence has no build or age limit; the captured-on build is a record only.
