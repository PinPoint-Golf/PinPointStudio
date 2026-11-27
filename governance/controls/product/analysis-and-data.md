# Product controls: analysis and data

What the product must do with measurement, sources and devices. Who owns a piece of data, and where it persists, is the information model's (`governance/artefacts/information_model.md`). An area reference: read it when doing this kind of work ([`AGENTS.md`](../../AGENTS.md) has the index). Older text names four roles (Product Owner, Solution Architect, Design Lead, Coaching Lead); all four are the owner, so read a role's approval as "agree it with the owner first".

## Already stated elsewhere

- Reported metrics are ranked; derived metrics are estimated (`information_model.md` R-INS-3, R-INS-5).
- When a producer's output changes, bump its version constant, or re-analysis silently reuses stale tracks (`docs/developer/analysis_pipeline_developer_guide.md` §7a).
- The wrist sensor (HackMotion) is the criterion for wrist measurement: grade against it when it is present (`docs/implementation/hackmotion_integration_brief.md` §0, decision 3).
- Wired phone transport is an optimisation, never a requirement; a change of transport is a new link with a new clock estimator (`docs/design/wired_transport_design.md` §0).
- The release dashboard is regenerated whenever metric, fault-model, norm or evidence work lands (`docs/validation/release_status.md`, "How to read it").
- Parity gates run on pinned poses (`docs/developer/testing_developer_guide.md`, "Parity baseline").

## Sources and measurement

**AD-1.** Analysis must always be driven by the data: the session type must steer nothing.
A stage runs, and a metric provider answers, on the data and devices the shot actually has (a stage's `canRun()`, a provider's `availability()`), never on the session type. The stored type is read so old files load, and decides nothing.
*Why:* the session type is a leftover of the old user interface: there is no "wrist session" or "GRF session". Gating on it made a good face-on swing produce nothing because the "wrong" session was picked, and made the metric catalogue claim the golfer lacked a device they had.[^ad-1][^d7]

**AD-2.** Whatever judges or shows a quantity the launch monitor delivers must follow the one ranking rule, which puts the launch monitor first (`information_model.md` R-INS-3); that includes what a chart shows.
*Why:* our optical estimates of club and ball quantities can't beat a launch monitor's measurement; judging a swing on ours while its reading sits beside it is wrong.[^ad-2]

**AD-3.** Every launch-monitor reading must be keyed `lm.*`, and our own estimates must keep their bare keys, so the two can be compared.
A quantity keeps a bare key if it has any non-launch-monitor route; never add a launch-monitor rung to a descriptor that has another route; a bare key and its `lm.` twin use the same sign and handedness conventions. Partly stated in `docs/developer/metric_catalogue_developer_guide.md` (the source-kind table).
*Why:* the launch monitor is the reference that validates our estimates; a device rung above a camera rung would supersede the estimate and destroy the comparison.[^ad-3]

**AD-4.** Pose, ball or shaft inference must never be re-run to get launch-monitor values.
Read the stored launch-monitor reading (backfill, or compute at read time). Where the device didn't report a descent angle or apex, model it with the launch-monitor panel's flight model (`src/LaunchMonitor/inferred/lm_flight_path.h`), marked as modelled; a measured value always wins.
*Why:* those numbers live in each shot's stored reading; pose inference takes about 11 s a swing and has nothing to do with them.[^ad-4]

**AD-5.** A camera route's weakness must never be answered with "move the camera" or "wear a sensor": camera positions are fixed and many users wear no motion sensors.
The camera route is the best-effort fallback, honest about where it is unsure (a wider σ, a bound instead of a value, a low-confidence label). The options to offer are: fix it at the source, swap in a better camera route, or add an honesty guard; give each one's consequence for a camera-only user.
*Why:* the cameras serve far more than one metric, and not every user will wear sensors.[^ad-5]

**AD-6.** The target line must never be taken from the golfer's own address posture.
It comes from an external reference: the alignment stick at calibration, the wall-fixed face-on camera, or the camera-solved heading per swing. Drift since the reference goes into σ. A setup assumption that can't be avoided carries a visible warning, never hidden in σ. The heading reference for the trunk sensors is `docs/design/trunk_imu_sequence_grf_design.md` §6.6.
*Why:* a faulty setup is a common fault and one of the main things to measure; assuming a square setup absorbs exactly that fault and corrupts every reading projected onto the line.[^ad-6]

**AD-7.** A camera fixed to a wall (the face-on) must be taken as square to the target line, with no orientation σ.
Bearings that depend on the uncalibrated tripod down-the-line camera carry its yaw σ (9°) and a warning until calibration reaches them; compare such bearings between shots rather than trusting the absolute.
*Why:* nobody points a wall-mounted camera at an angle; the tripod camera's turn is real and biases bearings one way.[^ad-7]

**AD-8.** The hand keypoints of the whole-body pose must be used only as a cross-check that flags a suspect angle, never as a modelling input, a fitted parameter or a redefinition of a measured angle: they are unreliable despite high confidence scores.
The pose carries all 133 whole-body points; don't infer its layout from a downstream file.
*Why:* a confidence score on motion-blurred hands reflects the heatmap's peak, not the truth; a model built on it would inherit that error invisibly.[^ad-8][^d8]

## Devices

**AD-9.** Motion sensors must go on the thorax and pelvis only; there will never be thigh sensors.
A route that needs one is "no route", not "planned": look for the pelvis-sensor route instead.
*Why:* the catalogue reported a reading as waiting on thigh sensors, implying a dependency that will never be delivered.[^ad-9]

**AD-10.** The HackMotion and the Witmotion sensors must never be worn on the same arm at the same time, and the app must never run two wrist instruments on the arm at once, except as part of a stated test or verification process.[^ad-10x]
A protocol that co-wears them says that it is such a process, and its verification steps are controls ([AD-47](../kinds-of-work.md) to [AD-49](../kinds-of-work.md)). Otherwise the arm carries either the HackMotion or the Witmotion sensors: normal capture has one wrist instrument per arm, and the corpus mixes sensor combinations across captures. Comparing the two is done in offline harnesses outside the app. A HackMotion on the wrist with Witmotion sensors on the trunk is allowed.
*Why:* two sensors strapped close together on a wrist and hand interfere physically.[^ad-10]

**AD-11.** The motion sensor's axes must never be re-derived, and the owner must never be asked for device tests: the sensor is characterised and its runtime configuration is fixed on every connect.
When a reading looks wrong, read the existing calibration path and test it with a recorded run through the real solve; first establish how the sensors were worn or held on that run.
*Why:* a week went on characterising the device once; it was re-derived twice in a day, wrongly both times.[^ad-11]

**AD-12.** Two phones at once (down-the-line and face-on) must be treated as the core case: any "one device" assumption is a bug.
A pairing is not a phone; a link is.
*Why:* a one-phone limit that looked like the protocol's was the host's own embedding.[^ad-12]

**AD-34.** The phone clock-sync limit of 5 ms must not be raised.[^ad-34]
*Why:* it exists for fusing several devices at high frame rates and clubhead speeds.

**AD-13.** A phone's shot must be accepted only if the host corroborates it: with no host shot detector available, accept it; with one, accept it only if a host detection lands within the protocol's 50 ms coincidence window, re-checked on every host detection so arrival order doesn't decide.
*Why:* the policy is the owner's call, not the protocol's; phone shots had been dropped with no counter or log.[^ad-13]

## Cameras and instruments

**AD-31.** Timing must treat a camera frame's own stamp as the end of its exposure (the capture instant is the stamp less half the exposure).
A swing with no recorded timestamp source was stamped on arrival: don't compare its timings with a newer swing's. Partly stated in `docs/design/event_buffer_design.md` §9.
*Why:* frames stamped on arrival at the app scattered impact-clip intervals by 1.2–2.2 ms.[^ad-31]

**AD-32.** A new impact-camera recording must be judged by its measured frame rate and its levels readout, never by eye on the un-stretched tile; the shaft angle must never be fitted locally in time at contact.
*Why:* the first recordings looked like a placement problem and were a light problem; a local fit swung 15° at contact.[^ad-32] Partly stated in `docs/design/impact_camera_design.md` §10.3.

[^ad-1]: Solution Architect, undated (settled; asked that it not be put again).
[^d7]: Solution Architect, 25 Nov 2026 (#195, decision D7, stronger than recommended; DEC-202 proposed in the information model, drafted in `IM-EDITS.md`); the code's remaining use is retired by issue #273.
[^ad-2]: Product Owner, 20 Oct 2026, re-confirmed 29 Oct 2026 for what a chart shows.
[^ad-3]: Solution Architect, 3 Aug 2026; where the launch monitor is ranked first is the one precedence ladder (Solution Architect, 25 Nov 2026, #195 decision D15; DEC-203 proposed in the information model, drafted in `IM-EDITS.md`).
[^ad-4]: Product Owner, 10 Nov 2026.
[^ad-5]: Product Owner, 2 Oct 2026.
[^ad-6]: Coaching Lead, 13 Nov 2026.
[^ad-7]: Solution Architect, 12 Nov 2026 (the shoulder-aim σ decisions).
[^ad-8]: Solution Architect, undated.
[^d8]: Solution Architect, 25 Nov 2026 (#195, decision D8); `docs/design/wholebody_pose_design.md` §2.2, which proposes the hand axis as a shaft prior, is superseded on that point.
[^ad-9]: Product Owner, 12 Nov 2026.
[^ad-10]: Product Owner, 18 Aug 2026; scoped to the arm, 4 Oct 2026.
[^ad-10x]: Product Owner and Coaching Lead, 25 Nov 2026 (#195): the stated test or verification exception; the corpus mixes sensor combinations across captures.
[^ad-11]: Product Owner, 27 Oct 2026.
[^ad-12]: Product Owner, 23 Aug 2026.
[^ad-34]: Product Owner, 29 Aug 2026; kept, without its first half ("more traffic makes it worse", overtaken by the 2 Sep fix), 25 Nov 2026 (#195, decision D14).
[^ad-13]: Product Owner, 27 Aug 2026.
[^ad-31]: Incident 16 Sep 2026. Owning role (proposed): Solution Architect.
[^ad-32]: Product Owner, 15 Sep 2026.
