# Controls: kinds of work

On top of the stages, each kind of work adds its own controls. A control written here lives here; the others are listed by id. Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

## Feature

**Extras:** [G3](plan.md) say what debt the feature adds · [UI-20](decide.md) the lighter design route · [UI-8](product/ui-behaviour.md) every register · [UI-9](product/ui-behaviour.md) This shot and Session views · [DC-6](product/diagnostics-and-coaching.md) new readings grade at once, no blanket warning · [AG-36](plan.md) what changes on screen, in every model plan · [BT-38](plan.md) requirements and their evidence in the plan.

**Guidance:** [AG-34](../guidance/advice.md) batch small UI follow-ups.

## Bug fix

**Extras:** [AG-22](review.md) fix only blocking findings; file the rest · [AG-24](review.md) bugs need evidence; how to file · [AD-22](kinds-of-work.md) one swing for a hypothesis · [AG-38](review.md) a backtrace before diagnosing a crash · [BT-41](review.md) the proof, red before the fix · [GIT-14](commit.md) the evidence in the commit.

**Guidance, lessons that bite here:** [L-21](../guidance/lessons.md) sample intermittent failures · [L-36](../guidance/lessons.md) read the truth first.

## Refactor

**AG-31.** During any refactor plan, a feature freeze must hold: no new functionality unless the plan names it, even when it is asked for.
Bug fixes are not features. Before adding behaviour, check it is a line of the plan; if it isn't, name the freeze and offer an issue for after the refactor.
*Why:* features landing in the old structure mid-refactor are how a refactor never finishes.[^d6]
*Owner:* Product Owner · *Stage:* Refactor · *Compliance:* none yet

**AG-10.** Before a cut-over (code being replaced and deleted) is coded, the implementer must write a duties inventory and the coordinator must check it: every side effect, caller, writer and test of the code removed, with where each duty goes; and the coordinator must write the item's deliverables as one numbered list.
Tests are re-pointed at the new owner, never deleted. Keep items small.
*Why:* hidden duties and deliverables scattered over six documents made every cut-over take two or three review rounds.[^ag-10]
*Owner:* Product Owner · *Stage:* Refactor · *Compliance:* none yet

The steps for a cut-over are [`procedures/cut-over.md`](../procedures/cut-over.md).

**Extras:** [GIT-2](commit.md) a refactor's pre-approval · [AG-6](decide.md) architecture: design agreed before building · [AG-7](decide.md) an include check before a dependency rule · [AG-9](plan.md) every audit finding placed · [AG-12](plan.md) line counts are never a target · [BT-21](test.md) parity for a second backing.

## Data and analysis work

**Extras:** [AG-37](build.md) read the analysis or diagnostics guide first.

Runs over swings: sweeps, re-analysis, the corpus and the library, truth and grading, and capturing the data. The steps for a sweep are [`procedures/sweep.md`](../procedures/sweep.md), and for a capture session or a corpus the capture and collection protocols listed in [`procedures/README.md`](../procedures/README.md); the measurement controls are in [`analysis-and-data.md`](product/analysis-and-data.md).

**AD-14.** Before more work on a vision estimate, ground truth for it must be obtained, the measure of success agreed up front, and the baseline shown before code changes.
For the arm, that is a two-camera session with the lead-forearm sensor (one instrument only, AD-10); for the club at impact, the launch monitor.
*Why:* a day of arm and clubhead-speed vision changes made no measurable difference, because every change was judged without a criterion.[^ad-14]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-15.** Every markerless-tracker result must be reported beside the marked-club band-lock figure on the same frames ("band X / segment Y", never Y alone).
*Why:* a bare figure has no scale; the band lock is the validated reference.[^ad-15]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-16.** Producer work must start from a matrix of every delivered source (each camera, the 3-D fit, each sensor, the launch monitor) against every catalogue metric it can measure, flagging every missing route, not just the planned ones already written down; lead and trail metrics together; the matrix shown before the plan.
*Why:* an audit of only the written-down routes left real routes out, and the work had to be reopened with its tests and sweeps.[^ad-16]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-17.** Corpus swings must be classified by inspection (for example by band-tier count), never by their recorded club label, which does not say which club was hit; a split by label (taped or bare, steel or graphite) is wrong.
*Why:* sessions labelled driver were a taped 7-iron, and two labelled 7-iron were wedges.[^ad-17]
*Owner:* Solution Architect (proposed) · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-18.** On the corpus's down-the-line clips, detection and tracking (found, continuous, pixel precision) must be graded, not metric accuracy.
They sit off the hand line, too high, with no truth; if down-the-line metrics matter, capture a new session with the camera on the ball-to-hands line at hand height.
*Why:* listing those swings as two-camera made the data look more ready than it is.[^ad-18]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-19.** On hand-marked bare-club swings, the seen P1–P6 marks and the inferred P7–P10 marks must be scored separately, and through-swing figures labelled "agreement with an inferred line".
*Why:* near impact the detector also builds its line from the hands, so agreement there is two constructions coinciding, not accuracy.[^ad-19]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-20.** Every parity run must measure the pose noise floor (the old binary twice), or compare with a stated tolerance: pose inference is not reliably deterministic.
A gate for a change at the pose level is "no worse against truth, and look at each swing that moved", not "unchanged".
*Why:* two runs on one swing once differed in about twenty metrics; batching alone moved one swing's finish by about a second.[^ad-20]
*Owner:* Solution Architect (proposed) · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-21.** Re-analysis must follow the information model's reuse rule (`information_model.md` R-ANA-7: pose re-runs only when its stage version changes); beyond it, re-analysis must never delete keypoints it isn't about to re-infer, a metrics-only re-analysis must never re-track, and the shaft must be anchored on the hands.
Anything else that re-runs pose is a defect to report.
*Why:* pose is the most expensive stage and moves the numbers; a re-tracked dim clip replaced good live tracks with coasting ones.[^ad-21]
*Owner:* Solution Architect · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-22.** A hypothesis must be tested on the one known swing that shows the problem, and widened to the affected set only once it holds.
Batches are for scope and regression checks after the cause is known.
*Why:* an 18-swing batch was run twice to test one hypothesis.[^ad-22]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-23.** Any multi-swing run (a sweep, a bisect of three or more swings, anything that re-runs pose, or a string of one-swing runs) must run on the primary platform's release build, not a development build. A one-swing comparison with pinned poses may run locally.
Keep a sweep's before and after on one platform. A build on a remote build machine goes only through the project's committed script for that machine, never through ad-hoc commands[^ad-23b]; uncommitted work goes there as a patch, never as a commit[^ad-23c]. Which machine, which script and how it is driven are in the local agent file ([POL-7](../policies.md)).
*Why:* the release build there is many times faster; platforms decode video slightly differently.[^ad-23]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-24.** Before a write-back sweep, the changed rule must be checked over every swing (corpus and library) read-only first, and then swept once.
Back up first. Give a time estimate up front, and say before starting if a sweep must be repeated. Judge a pass by the per-swing before-and-after diff against the backup, not a total, and read the first swing's reuse line before letting it run on.
*Why:* five write-back sweeps each found one more flaw that a read-only check would have found in minutes; a total hid a swap.[^ad-24]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-25.** A sweep after a change to diagnostics content must run in two halves, in order: re-analysis refreshes the swing metrics, then the ledger regrade re-reduces each session's diagnostics.
Check the sweep binary's build date against the last producer commit first.
*Why:* run the other way round, the corpus still showed a fault on most shots from retired keys.[^ad-25]
*Owner:* Product Owner (proposed) · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-26.** Library and corpus backups must never live in a local build folder: they go to the shared scratch area and are deleted once the owner has accepted the result.
*Why:* local build folders are not where durable data is kept.[^ad-26]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-27.** Corpus run outputs (per-swing results of sweeps and audits) must go to the shared scratch area, never the development machine's disk; only small derived files stay local.
Check free space before a multi-variant sweep. Delete each run tree as soon as its derived CSVs and gate tables are in the repo, and say what was deleted and how much; keep a parity gate's run folder until the note citing it is written. Don't delete other sessions' scratch unasked.
*Why:* one plan filled the machine's disk and every tool failed; another left 24 GB of dumps behind.[^ad-27]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-28.** "The library" must mean the athlete folders the app reads; the corpus is a separate, curated copy for validation.
When the owner wants a change visible in the app, re-analyse the athlete folders (the corpus too only when validation needs it), and read a swing from the folder the app reads before saying the change has reached what the owner sees.
*Why:* a whole day's passes went to the corpus only; the app still showed the old result, and the review sheets were drawn from the wrong data.[^ad-28]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-29.** A golfer whose swings break processing must stay in the library and the corpus; they are left out of pooled norm statistics and reported separately, and their failures are open processing work.
*Why:* a golfer whose swings break the pipeline is its test case, not noise to delete.[^ad-29]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-30.** What a corpus run reads must live in the corpus: pinned poses, truth sets and calibrations go in the corpus, listed in its README, never in `build/`.
Corpus session folder names are never renamed (they are the join key for pinned runs, pose caches and recorded CSVs), and the corpus manifest sits inside the corpus root. The frozen pose cache of the original population is never regenerated; new gates use the newer pinned population, and gate populations select swings explicitly (the manifest is an inventory).
*Why:* `build/` is transient; moving the manifest broke the parity runner; a re-extracted pose would break comparability with every published result.[^ad-30]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-33.** A criterion's known bias must be corrected before grading against it, and the write-up must say so; a bias on the criterion is not a licence to tune our number.
(The launch monitor's clubhead-speed over-read: [`two_camera_capture_protocol.md`](../procedures/two_camera_capture_protocol.md) §8.)
*Why:* most of the camera's apparent shortfall against the launch monitor was the launch monitor's own bias.[^ad-33]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**DC-7.** For posture work, new readings must be graded against the Coaching Lead's coach-eye labels, which are the truth: read a label's words literally, and claim a contradiction only where a label covers it.
*Why:* an agent twice over-reached, misreading a label and calling a setup fault wrong where the labels said nothing.[^dc-7]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-35.** In a capture session, once the rig is placed, no camera, lens ring, tripod or camera setting may be touched until the session's closing checks are done; a camera that is bumped or moved is recalibrated (its pointing clips recorded again) and the swing it happened after is written down.
On a calibrated rig, a moved camera's calibration is redone before its next swing counts.
*Why:* a down-the-line camera that moved after the third swing split a session's calibration in two, and every number from it was graded on repeatability alone.[^ad-35]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-36.** Each camera of a capture session must be framed so that, with the club held at the top, the wrists sit at least one club length (as the club appears at address) below the top edge, and the clubhead is visible at the top and at the finish in both views.
Checked with a number, not by eye: setup's Framing step reads the live wrist dots; the clubhead is looked for in the frame.
*Why:* on 5 Oct 2026 the down-the-line frame lost the club from just after P3 to about P5 on all 31 swings, and on five the tracker drew the lead arm instead; the July framing had the same fault.[^ad-36]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-37.** A capture session's record must be complete: the launch-monitor reading attached to every full swing, the club selected in the app the club actually in hand (changed with every change of club), and the rig sheet filled in and photographed into the session folder.
Classifying corpus swings by club is [AD-17](kinds-of-work.md).
*Why:* the rig sheet is the only record of lenses, offsets, exposure and gain; the July corpus labelled a taped 7-iron "driver"; the launch monitor is the criterion for club path, attack angle and clubhead speed.[^ad-37]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-38.** A corpus's acceptance thresholds must be registered before its collection, and a threshold must never be moved to make a result pass.
*Why (proposed, for the Product Owner):* a threshold moved after the data is seen no longer tests anything.[^ad-38]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-39.** Each corpus swing must be assigned to Tune, Validation or Held-out when the corpus is blessed, before any analysis, and the assignment recorded; Held-out is touched once, at freeze.
A tuning sweep searches Tune, selects on Validation and touches Held-out once (`--freeze`).
*Why (proposed, for the Product Owner):* a value selected on the swings it is scored on overstates how well it generalises.[^ad-39]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-40.** A swing that fails any of a protocol's must gates must be recaptured, never tuned around: it is a data failure.
*Why:* tuning around a bad capture fits the analysis to a fault in the data.[^ad-40]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-41.** Worn sensors must be calibrated at every donning, re-checked (Confirm Tracking) every 15 swings or 20 minutes, whichever comes first, and recalibrated after any re-seat or slip, any break over 10 minutes, a battery swap, any calibration-gate failure, and every change of golfer; no scored swing has calibration older than 30 minutes.
The swings a failed gate affects are recaptured ([AD-40](kinds-of-work.md)).
*Why:* calibration discipline is the single biggest determinant of the data's quality.[^ad-41]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-42.** A labeller or coach marking up a swing must work from its video only and must not see the analyzer's output for it (track, phase events, metrics) before or while marking it; an annotation made unblinded is excluded.
*Why:* a label made after seeing the estimate anchors the "truth" to the thing it is meant to judge.[^ad-42]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-43.** A study's golfers must give informed consent for each instrument and use (video, sensors, a third-party device and its export, the launch monitor, a coach's review), keep the right to withdraw, and be stored under an athlete id, never a name; a study of several golfers has its ethics approval before anyone is recruited.
*Why:* a study's data are about people, and a golfer can withdraw.[^ad-43]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-44.** Lab tools must not write into a corpus swing folder except its markup (`truth.json`) and the lab's own `pose.json`.
Which component writes each of a swing's files, and who owns it, is the information model's file table (`governance/artefacts/information_model.md`, the `FILE-SHOT-*` rows); this control adds nothing to it for the app and covers only the lab tools.
*Why:* the swing folder is the captured record that every later run is compared against.[^ad-44]
*Owner:* Solution Architect · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-45.** In a study of several golfers, the golfers, not the swings, are the sample size for any population claim, and swings must never be treated as independent of their golfer: the analysis accounts for the clustering (mixed models or cluster-robust intervals).
*Why:* repeated swings by one golfer are correlated; pooling them as independent overstates precision by the design effect.[^ad-45]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-46.** A taped club's band record must be re-measured, and its taping date updated, whenever its tape is replaced; the club's id in markup must match its record exactly.
*Why:* adhesive drift is real, and without a matching record the tape is only "easier to see" rather than truth.[^ad-46]
*Owner:* Product Owner · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-47.** A capture that co-wears the HackMotion and the PinPoint wrist sensors under [AD-10](product/analysis-and-data.md)'s exception must re-run Confirm Tracking once both are donned (PinPoint first, then the HackMotion); if the tracking is worse than before the HackMotion went on, re-seat and recalibrate. A swing captured with a perturbed mount is invalid.
*Why:* a perturbed mount would attribute a mounting artifact to a disagreement between the devices.[^ad-47]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-48.** In such a capture, the defined sync action (for example three sharp wrist snaps) must be performed at the start of every capture block, and recorded.
*Why:* the two devices keep independent clocks (at 45 m/s, 1 ms is 45 mm); the sync action gives both a sharp, shared feature to cross-correlate, more robust than impact on partial swings.[^ad-48]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**AD-49.** A co-worn swing counts as paired only if both devices recorded the same shot and the export is matched to its swing folder, the sync action is present and the lag resolved, the co-mount check ([AD-47](kinds-of-work.md)) passed, the HackMotion was zeroed this session (and re-zeroed whenever PinPoint was recalibrated or a sensor re-donned), and the reconciliation transform is stable (a sign flip only); otherwise it is recaptured or moved to the robustness set.
*Why:* a swing where one device dropped out, or whose devices are not reconciled, measures nothing about their agreement.[^ad-49]
*Owner:* Coaching Lead · *Stage:* Data and analysis work · *Compliance:* none yet

**Guidance, lessons that bite here:** [L-29](../guidance/lessons.md) a score doesn't prove stages ran · [L-30](../guidance/lessons.md) the re-analysis path decides · [L-31](../guidance/lessons.md) trace files come from a second run · [L-32](../guidance/lessons.md) capture holes · [L-33](../guidance/lessons.md) verdicts written once · [L-34](../guidance/lessons.md) heuristics tuned to flicker · [L-36](../guidance/lessons.md) read the truth first · [L-37](../guidance/lessons.md) re-analysis isn't idempotent · [L-39](../guidance/lessons.md) tools outside their build folder · [L-41](../guidance/lessons.md) re-filing a session · [L-58](../guidance/lessons.md) OpenCV decodes differ across hosts · [L-59](../guidance/lessons.md) sensor timing and buffers · [L-60](../guidance/lessons.md) stalled gates and circular calibration.

[^d6]: Product Owner, 25 Nov 2026 (#195, decision D6); the freeze first ruled for the domain refactor, 14 Nov 2026.
[^ag-10]: Product Owner, 21–22 Nov 2026.
[^ad-14]: Product Owner, 19 Oct 2026.
[^ad-15]: Product Owner, 8 Sep 2026.
[^ad-16]: Product Owner, 13 Nov 2026.
[^ad-17]: Incident 8 Sep 2026. Owning role (proposed): Solution Architect.
[^ad-18]: Product Owner, 20 Sep 2026.
[^ad-19]: Coaching Lead, 9 Sep 2026 (the hand markup of an unmarked 6-iron session).
[^ad-20]: Incidents 6 Sep, 6 Oct and 24 Nov 2026. Owning role (proposed): Solution Architect.
[^ad-21]: Solution Architect, 14 Nov 2026 (pose only on a version change); Coaching Lead, 17 Sep 2026 (the shaft anchored on the hands); incident 18 Sep 2026 (a full-window write-back re-tracked four swings).
[^ad-22]: Product Owner, 29 Sep 2026.
[^ad-23b]: Product Owner, 7 Oct 2026 (one script, after hours lost to ad-hoc scripts).
[^ad-23c]: Product Owner, 22 Oct 2026 (the patch route).
[^ad-23]: Product Owner, 29 Sep and 19 Oct 2026.
[^ad-24]: Product Owner, 7 Nov 2026; incidents 4 Sep and 18 Sep 2026.
[^ad-25]: Incident 14 Sep 2026. Owning role (proposed): Product Owner.
[^ad-26]: Product Owner, 29 Sep 2026.
[^ad-27]: Product Owner, 6 Sep and 28 Sep 2026; the parity-gate exception, 20 Sep 2026.
[^ad-28]: Product Owner, 19 Oct 2026.
[^ad-29]: Product Owner, 13 Nov 2026.
[^ad-30]: Product Owner, 28 Sep 2026 (inputs in the corpus) and 9 Sep 2026 (names, manifest, the frozen population).
[^ad-33]: Coaching Lead, 21 Sep 2026.
[^dc-7]: Coaching Lead, 22 Oct 2026.
[^ad-35]: Moved from `two_camera_capture_protocol.md` §2 and `corpus3_collection_protocol.md` §5, §6.1 and §11, 25 Nov 2026 (#195), on the Product Owner's instruction that the protocols' mandatory rules are controls. The incident: the 4 Jul 2026 session.
[^ad-36]: Moved from `two_camera_capture_protocol.md` §2, 25 Nov 2026 (#195). The incident: 5 Oct 2026.
[^ad-37]: Moved from `two_camera_capture_protocol.md` §3, §5 and §6, 25 Nov 2026 (#195).
[^ad-38]: Moved from `corpus1_collection_protocol.md` §2 (and the pre-registered tables of the Corpus 2 and Corpus 3 protocols), 25 Nov 2026 (#195).
[^ad-39]: Moved from `corpus1_collection_protocol.md` §10 and `shaft_validation_protocol.md` §5, 25 Nov 2026 (#195).
[^ad-40]: Moved from `corpus1_collection_protocol.md` §11 (inherited by the Corpus 2 and Corpus 3 protocols), 25 Nov 2026 (#195). Owner Coaching Lead, confirmed by the Product Owner and the Coaching Lead, 25 Nov 2026.
[^ad-41]: Moved from `corpus1_collection_protocol.md` §6.2 (marked mandatory) and `corpus3_collection_protocol.md` §6.3, 25 Nov 2026 (#195). Owner Coaching Lead, confirmed by the Product Owner and the Coaching Lead, 25 Nov 2026.
[^ad-42]: Moved from `corpus1_collection_protocol.md` §9.4 (marked mandatory), `corpus3_collection_protocol.md` §3, §9.3 and §14, and `shaft_validation_protocol.md` §3, 25 Nov 2026 (#195). Owner Coaching Lead, confirmed by the Product Owner and the Coaching Lead, 25 Nov 2026.
[^ad-43]: Moved from `corpus1_collection_protocol.md` §4, `corpus2_collection_protocol.md` §4 and `corpus3_collection_protocol.md` §4, 25 Nov 2026 (#195). Owner Coaching Lead, confirmed by the Product Owner and the Coaching Lead, 25 Nov 2026.
[^ad-44]: Moved from `corpus1_collection_protocol.md` §10 and `shaft_validation_protocol.md` §3, 25 Nov 2026 (#195). Owner Solution Architect, confirmed by the Product Owner and the Coaching Lead, 25 Nov 2026.
[^ad-45]: Moved from `corpus3_collection_protocol.md` §3 and §12, 25 Nov 2026 (#195).
[^ad-46]: Moved from `instrumented_club_protocol.md` §3, 25 Nov 2026 (#195).
[^ad-47]: Moved from `corpus2_collection_protocol.md` §6.1, §6.4 and §11, 25 Nov 2026 (#195). Product Owner and Coaching Lead, 25 Nov 2026: Corpus 2's verification steps are controls under AD-10's exception.
[^ad-48]: Moved from `corpus2_collection_protocol.md` §6.5 and §11, 25 Nov 2026 (#195). Product Owner and Coaching Lead, 25 Nov 2026, as AD-47.
[^ad-49]: Moved from `corpus2_collection_protocol.md` §6.2, §7.1 and §11, 25 Nov 2026 (#195). Product Owner and Coaching Lead, 25 Nov 2026, as AD-47.
