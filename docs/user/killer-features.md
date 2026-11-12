# Killer Features — Swing Mechanics & Kinematics App

**Guiding principle:** close the loop from body → club → ball. Kinematics only matter insofar as they change impact.

## 1. Swing-by-swing fusion with launch-monitor data
- Every capture automatically tied to club and ball data (path, face, attack angle, low point, dispersion).
- Queryable across sessions, e.g. "all swings where face-to-path was more than +3° → compare pelvis motion".
- Removes the video and numbers silos.

## 2. Validated 3D kinematics with honest error bars
- Markerless pose estimation.
- Kinematic sequence: peak angular velocity order and timing for pelvis, thorax, arm and club.
- Pelvis and thorax rotation, sway, thrust, lift and side bend.
- Measurement uncertainty stated, not hidden; benchmarked against marker-based systems such as GEARS.

## 3. Ground reaction forces
- Force and pressure-plate integration (force, torque, centre-of-pressure traces).
- Force shows the cause; kinematics show the effect.

## 4. Phase-normalised comparison
- Overlays and side-by-side views synced by swing position (P1–P10), not by time.
- Compare against the player's own best swings by default, not a tour model.

## 5. Variability and trends
- Spread of key parameters within a session.
- Change over weeks and months.
- Stability under pressure: range versus course or competition.

## 6. Real-time biofeedback for practice
- Audio or haptic cues when a chosen parameter is in or out of range.
- Fading feedback frequency to support motor learning and transfer.

## 7. Frictionless capture
- Synchronised multi-angle high-frame-rate video.
- Automatic trimming on impact; zero fiddling.
- Must not break the rhythm of a lesson.

## 8. Coach workflow
- Annotation and voice-over.
- A library for each student, with session notes.
- Asynchronous remote review.

## 9. Open data
- Raw export of time series and derived metrics.
- No lock-in, which matters for researchers and analytically minded coaches.

## Anti-feature
A dashboard of 80 metrics. Help the coach find the two or three variables that drive *this* player's ball flight, then train those.

---

# Where PinPoint Studio stands (10 Nov 2026)

Assessed against the code at `770b9fce` (main, 10 Nov 2026). Every "built" claim below was checked in code, not taken from a design doc. Each feature has four parts:

- **Built**: in the app today.
- **Gaps**: what the feature asks for that the app doesn't do.
- **Can build now**: needs no new hardware and no unbuilt dependency.
- **Blocked by**: what has to exist first.

Items marked **DS** add or change a screen, so they need Mark's design-system approval before they're built. Items marked **Mark's call** need a ruling first.

## At a glance

| # | Feature | How far met | Main blocker for the rest |
|---|---|---|---|
| 1 | Launch-monitor fusion | **Mostly.** Every shot carries the LM reading; Home filters by outcome. You can't query by a number, and nothing compares body measures. | None. The rest can be built now. |
| 2 | Validated 3-D kinematics | **Partly.** Two-camera rotations and the sequence come with σ. Sway, lift and side bend are 2-D. 42 of 58 metrics have no per-reading σ. Nothing is benchmarked against markers. | Camera calibration (#74), the uncertainty census (#88), the worn trunk session (#109), a marker lab |
| 3 | Ground reaction forces | **Not met.** There are two 2-D balance proxies only. | Force plates / Wii boards, the worn trunk session, calibration |
| 4 | Phase-normalised comparison | **Partly.** Chart traces line up by P-position, with an Against picker. There's no video side-by-side and no "best swings" pool. | None for the basics. P-position reliability (#3, #4, #24, #30, #70) limits quality. |
| 5 | Variability and trends | **Mostly, within a session.** The Consistency tab, variability marks and trend arrows are built. There's no dated trend chart on Home and no practice-vs-pressure context. | None. On-course capture is out of reach. |
| 6 | Real-time biofeedback | **Not met.** Results arrive about 15 s after impact. Tones exist but carry no metric. | Mark's call on the audio rule; the census for honest in-range tests; live paths |
| 7 | Frictionless capture | **Mostly.** Multi-camera capture, automatic shot detection, LM pairing and auto-replay all work. Capture pauses until the previous shot finishes, and setup has up to 11 steps. | None for the main items. Hardware sync needs trigger cabling. |
| 8 | Coach workflow | **Partly.** There's a telestrator (not saved), automatic coach lines, per-shot notes and stars, an athlete library, and a report PDF sent by email. No voice-over, no golfer-side viewer. | Hosting for remote review; a recorder for voice-over |
| 9 | Open data | **Mostly.** Open binary format (MIT library + Python reader), raw lanes, zip and per-swing CSV export. No per-shot metrics table, and the schema doc is stale. | None |
| ✗ | Anti-feature | **Respected.** Work-ons list 3–5 faults, and Tendencies uses three factors. Charts and Readings still expose the full metric set for those who want it. | — |

---

## 1. Launch-monitor fusion

**Built**
- Three connectors: GC Quad / FSX CSV, GSPro and Foresight Premier (`src/LaunchMonitor/launch_monitor_factory.cpp:20`).
- The LM reading is paired to the detected swing by time (`shot_pairing.h`). A reading that no other detector saw becomes a shot of its own.
- Every swing stores up to 26 `lm.*` fields (`launch_monitor_reading.cpp:34`), plus a list of the fields the import rejected:
  - club speed, attack angle, path, face, face-to-path, dynamic loft, spin loft, lie, closure rate
  - ball speed, launch angle, launch direction
  - smash, strike location
  - spin rate, spin axis
  - carry, total, offline, peak height, descent
- Launch-monitor figures win over the camera's when both exist (`lm.*` preferred keys).
- Face-to-path is a diagnosed condition (`open_face_to_path` / `closed_face_to_path`), so its value lands in every session's `diagnostics.json` ledger.
- Shot outcomes give each shot three factors: strike, direction and flight. Strike verdicts are checked against the ball flight.
- Home filters by Club · Strike · Direction · Flight across all sessions, and says how often each fault fires in the filtered shots against the others (`outcome_scope.h:186`).
- Tendencies, the Matchups tab, and the per-club distances and offline spread (`club_distances.h`) are all built.
- Charts › Relations scatters any metric (LM included) against any other within one session, with a "Moves with" ranking.
- The report has a launch-monitor appendix.

**Gaps**
- **No query by a number.** Home's filter takes outcome categories only. "Face-to-path > +3°" can't be asked.
- **The contrast compares how often faults fire, not body values.** Nothing says "pelvis sway on these shots vs the rest".
- **Relations works within one session.** No library-wide "what moves with your carry / face-to-path".
- **Low point:** no connector fills `lm.lowPointAhead`, because none of the three LMs reports it. The camera's `lowPointAhead` exists (#67).
- The ledger's per-shot record holds carry, offline, smash, launch and spin, but not club path, face or face-to-path as fields. Face-to-path is reachable only through its condition row.

**Can build now**
- **1a. Numeric shot filter on Home** (DS: a new picker), e.g. `lm.faceToPath > 3`, `lm.offline < −10`.
  - It reads the ledgers only. Home must not open swing documents, which freeze the GUI.
  - Needs club path, face and face-to-path added to the ledger's per-shot record. That is a schema bump, so the library re-grades.
- **1b. Value contrast:** next to "fired 8 of 11 vs 12 of 31", show a measure's median and spread in the two groups. This covers measures that some condition reads. Anything else would need a stored per-session summary of readings.
- **1c. "What drives your ball flight" across sessions:** rank body readings by how strongly they move with each LM figure over the library. This is the direct answer to the anti-feature. It needs the same per-session summary as 1b, and any ranked "driver" must be worded as "moves with", not cause.

**Blocked by**
- Low point from the LM needs a launch monitor that reports it. The camera route is #67.

## 2. Validated 3-D kinematics with honest error bars

**Built**
- 133-point WholeBody pose on both cameras. Pose runs about 3 s for two cameras on the studio PC (134eac67).
- The two views are fused into a 3-D skeleton (`src/Skeleton3d`), with a synthesised "usual swing" for the session.
- Pelvis and thorax rotation, signed rotation and X-factor come from the two-camera fit, or from the trunk IMUs when worn (`metric_catalogue_manifest.cpp:626–882`).
- **Kinematic sequence** (`src/Kinematics/segment_rates.cpp`):
  - Each segment's peak carries its time before impact, a σ on that time and a σ on the peak value.
  - The order is refused when peaks sit within their σ.
  - Faults are graded relative to impact and are sharp findings.
- σ on 16 of 58 metrics. It propagates into firing probability and the diagnostic graph, and shows on cards.
- **Validation against truth:**
  - Shaft angle: σ coverage checked on 996 hand-marked frames.
  - Clubhead speed: checked against the GC Quad.
  - Arm peak: checked against the HackMotion wG3 (camera +43 ms late).
  - Trunk IMU code: built and calibrated in setup (W2).

**Gaps**
- **Sway, lift and side bend are 2-D**, face-on only, and sway and lift stop at impact. Thrust is down-the-line 2-D and needs a ruler. Nothing exists for thorax sway, thrust or lift. `thoraxSideBend` and `pelvisObliquity` have no producer.
- **Every two-camera route is "Estimated".** skeleton3d solves its own cameras from an assumed 2.2 m placement, and `camerasCalibrated` is hard-wired false (`analysis_pipeline.cpp:3026`).
- **Uncertainty is uneven:**
  - 22 metrics carry frame noise only, and 20 carry no σ at all.
  - The IMU routes have no σ.
  - The planned "uncertainty not characterised" label isn't built.
- **No marker-based benchmark.** Rotation and the sequence have only internal checks. The trunk sensors have never been worn in a swing session.
- The `thoraxRotation` help text still describes a shoulder-span route that no longer exists (`metric_catalogue_manifest.cpp:774`).

**Can build now**
- **2a.** Fix the stale `thoraxRotation` text.
- **2b. The uncertainty census itself** (#88). It is Mark's hard gate before validation and full release, and needs no hardware.
- **2c.** The "uncertainty not characterised" label on cards. Probably DS, as a new label state.
- **2d. 3-D sway, lift and side bend** from skeleton3d joints, as Estimated routes beside the 2-D ones.
  - Sway and lift as a percentage of stance width are scale-free, so they're honest before calibration. Centimetres are not.
  - Low value until calibration lands, so do this only if wanted ahead of it.
- **2e.** The session sequence measures (`m_ksOrderShare`, `m_ksTimingSpread`), which have no producer.

**Blocked by**
- **Camera calibration (#74):** designed, nothing built. Every 3-D route stays Estimated until it lands, and calibrated σ can't exist without it.
  - Stages 1–4 and 9 need no new kit: focus visibility, the per-bay record, linking OpenCV calibration, known-size scale, the alignment-stick geometry. The ChArUco board was ordered in September.
- **The worn trunk session, W3/W4 (#109):** the sensors are in hand; it needs Mark's time. It is the truth for camera rotation and for the pelvis/thorax timing in the sequence, including whether hips really peak after impact.
- **A marker-based reference** (GEARS or Vicon lab access) for the benchmark. External.
- **Validation** waits on the census, by Mark's rule.

## 3. Ground reaction forces

**Built**
- Nothing measures force.
- Two 2-D geometry proxies exist: `comOverLeadFoot` and `balanceHeelToe`. Both say they are proxies.

**Gaps**
- Everything. There's no device support, no centre-of-pressure trace and no force estimate.

**Can build now**
- Nothing of real value.
  - The design's Phase 0 (net force from body motion, `trunk_imu_sequence_grf_design.md` §12) needs the centre of mass in metres, so it needs calibration. The design also flags the double-differentiation risk at 150 fps.
  - A `ForcePlatformDevice` interface with a synthetic device would be groundwork only.

**Blocked by**
- **Hardware:** two Wii balance boards (Bluetooth Classic HID through hidapi, per foot vertical force and centre of pressure) are design Phase 1, the cheapest real step. A DIY plate comes later.
- **The worn trunk session:** Part B's lead-side load and drive are estimated from the trunk IMUs.
- **Camera calibration:** centre of mass in metres for Phase 0 and the force split.
- **A reference session on borrowed plates** to validate any of it. Until then everything stays experimental.

## 4. Phase-normalised comparison

**Built**
- Charts › Session overlays traces lined up by P-positions (the default) or by impact, as small multiples or overlaid (`session_warp.h`).
- Charts › This shot has an **Against** picker, always P-aligned, with a difference strip. Options: session median, typical, least typical, previous, next, "shots like this", or a ★ reference swing from any session.
- The Wrist and Posture views have ghosts (previous, ★ reference, session median).
- The 3-D panel shows the session's synthesised usual swing.
- Session diagnostics show bookend shots for each condition.
- Comparisons default to the player's own session, not a model.

**Gaps**
- **No video side-by-side or overlay** of two swings, by time or by P-position.
- **No "best swings" pool.** ★ is one swing picked by hand, and nothing picks the player's best by outcome.
- Settings › Swing model corridors are population norms, by design (`norms.json`). Comparing against the player's own swings happens only through Against.
- No 3-D overlay of two figures.

**Can build now**
- **4a. Video side-by-side synced by P-position** (DS). The playheads map through each swing's stored P-times, using the same warp the charts use. Any swing can go against any other, and against ★.
- **4b. A "your best swings" Against option**: the P-aligned median of a pool chosen by outcome. **Mark's call** on how "best" is chosen (e.g. centred strike, offline within the club's band, carry near the club's usual). It must not model intent, and must give no miss family priority.
- **4c.** A 3-D ghost of ★ or the best pool beside the current figure. Low value until calibration: Mark judged the 3-D usual swing "better, still not great".

**Blocked by**
- Nothing hard. P-position errors (#3, #4, #24, #30, and timeline fusion #70 not built) make any P-sync wrong on the swings they hit. The worst is P8 landing hundreds of ms late.

## 5. Variability and trends

**Built**
- **Within a session:** the Consistency tab (area × span: steady / some / loose), variability marks and typicality badges, run-plus-box readings, and the LM ellipses.
- **Across sessions:**
  - Trend arrows on Home's swing areas, faults and Tendencies, using Kendall/Theil-Sen (`diagnostic_ledger.h:303`).
  - The Consistency column on Home, from the last 20 readings.
  - A per-session box-plot history strip for a condition's measure, in Session diagnostics › Analyse (`PpHistoryStrip.qml`).
  - Per-club carry and offline spread.

**Gaps**
- **No trend chart over weeks and months on Home.** The history strip sits inside one session's panel, ordered by session, with no date axis.
- **No trend for LM figures** (carry, offline, club speed) or for any chosen reading across sessions.
- **No session context.** The setup Focus step picks a condition to work on, but nothing records practice vs on-course vs competition.

**Can build now**
- **5a. Trends on Home** (DS): a dated chart per fault, per work-on and per LM figure, with the Theil-Sen slope. Everything it needs is in the ledgers.
- **5b. A session context tag** (DS, **Mark's call** on the choices; e.g. Practice · Lesson · Test), stored with the session and usable as a filter or split.
  - "Pressure" in a studio could be a test session: one ball per club, nothing to retry.
  - The tag records what kind of session it was, not what the golfer meant to do.

**Blocked by**
- True range-vs-course stability needs capture away from the studio (a portable phone rig). Not on any plan.

## 6. Real-time biofeedback

**Built**
- No metric drives any cue.
- Audio exists:
  - `TingPlayer` tones: shot detected, shot refused, LM reading arrived.
  - Spoken instructions in the trunk calibration routine, behind an experimental setting.
- `LiveWristAngles` shows live wrist angles during setup only, and doesn't feed analysis.
- Results appear about 15 s after impact on the studio PC: 1.25 s post-roll plus analysis.
- No haptics.

**Gaps**
- **No cue at all.** There's no in-range test, no chosen parameter and no fading schedule.
- **No live path into analysis.** Camera results are post-shot only.
- **One rule breach:** the developer-only LLM coach speaks its replies through TTS. Those can be verdicts or readings, which the spoken-output rule forbids (instructions only).

**Can build now**
- **6a. After-shot range tone with fading** (**Mark's call**).
  - Pick one reading and its band. A tone sounds when the shot is analysed: one pitch for in, another for out.
  - Feedback can fade (every shot → every 2nd → every 3rd, or only when asked).
  - This is "knowledge of results" a few seconds after the shot, which is the setting the fading research is about.
  - A tone is not speech, but it does carry a verdict, so it needs Mark's ruling against the spoken-output rule. Tests must use a silent fake.
- **6b. Real-time wrist cue from the wG3.** Its live lane runs 25–100 Hz in the downswing, with 20–25 ms of Bluetooth jitter. That's enough for a cue at the top (e.g. lead wrist flexion at P4), the way HackMotion does it. It needs P4 found live from the arm sensor. **Mark's call**, same rule question.
- **6c.** Silence the spoken LLM replies, or limit them to instructions.

**Blocked by**
- **The census**, for an honest "in range" (a band means little without σ). The IMU routes have none.
- **The worn trunk session**, before any live trunk-turn cue can be trusted.
- **Live pose**, for any camera-based real-time cue. Not planned.
- **Haptics** need a device. The wG3 buzzes on link-up, but there's no command for it in our code.

## 7. Frictionless capture

**Built**
- **Cameras:** FLIR (Spinnaker), any GigE / USB3 Vision camera (Aravis), macOS webcams and PPCP phones, in face-on, down-the-line and impact roles.
- **Sync** is in software: each camera's clock is mapped to the host, phones are clock-synced over PPCP, and frames are matched by time.
- **Shot detection** is automatic and fused from acoustic, IMU, pose, ball and phone detectors (two agreeing, or one strong), with a manual button as fallback.
- The LM reading attaches to the swing by time.
- After each shot a ¼-speed replay plays automatically; ESC skips it.
- The impact camera's clip is trimmed around impact, and playback is trimmed to the swing.

**Gaps**
- **The camera ring stays paused from detection until the replay ends** (`shot_processor.h:54`; `armed()` needs the processor idle). A swing in that window, about 15 s plus the replay, isn't captured. This is the biggest risk to a lesson's rhythm.
- **The face-on and down-the-line files keep the whole 4 s window.** The export is not trimmed (`shot_processor.cpp:1120`).
- **Setup has up to 11 steps.** The Ball step blocks until a ball is seen, the Triangulate step's checks are always false, and there's no "same as last time".
- **Sync is software only:** free-running cameras sit about 3 ms out of phase. No hardware trigger.
- The microphone detector picks up the TV (seen 2 Nov).

**Can build now**
- **7a. Let the next swing be captured sooner.** Resume the ring as soon as analysis and export have copied what they need, and play the replay from the exported clip. This is a medium-sized change to who owns the frozen window.
- **7b. Trim the face-on and down-the-line export** to the analysed span plus a margin. This saves disk and loads faster. Mark's call on the margin; the replay already trims.
- **7c. "Same setup as last time"** (DS): when cameras, sensors and bay haven't changed, skip straight to Ready, with a check rather than the full walk-through.

**Blocked by**
- **Hardware sync** needs trigger cabling to the FLIR GPIO lines, or PTP-capable cameras.
- **Camera calibration** will add a ceremony. The design keeps it to one, done once per bay.

## 8. Coach workflow

**Built**
- **Telestrator** on Analyse: line, ellipse and square, with colours. Marks are held in memory only and wiped when the swing changes.
- **Automatic coach lines** at address: head, hips, plane, spine, shoulder plane, Hogan line.
- **Per-shot record:** stars, a note and the club, saved in the swing.
- **Athlete library:** a profile with notes, a sessions list, and archive and restore.
- **Session report PDF** with a coach's note and a Tendencies page. It is emailed directly over SMTP (password in the system keychain) or handed to the mail app.

**Gaps**
- Drawn marks aren't saved, and there's no angle tool.
- No voice-over and no screen or replay recording.
- No session note outside the report.
- No remote viewer for the golfer, and no share link.
- The LLM coach is a developer screen with no swing data in its context.

**Can build now**
- **8a. Save the telestrator marks** with the swing and show them in report stills (DS).
- **8b. An angle tool** (DS).
- **8c. A session notes field** on the Sessions screen (DS).
- **8d. Export a clip with the marks and coach lines drawn in**, using the FFmpeg encoder already in `src/Export`. The clip goes with the report email. That is the cheapest form of asynchronous review.

**Blocked by**
- **Voice-over** needs a recorder that muxes the microphone with the rendered view. FFmpeg is there; the recorder isn't.
- **A golfer-side viewer or share link** needs hosting and sign-in. **Mark's call**: the project is never commercial, so who would host it?
- **A useful LLM coach** is Mark's call on whether it's wanted at all.

## 9. Open data

**Built**
- **Swings are stored as `swing.ppsw`.** libppswing is MIT licensed and has a spec, a C++ CLI and a Python reader.
- Each swing holds:
  - the raw IMU lanes
  - 133-point pose per frame, raw and smoothed, with σ
  - the club, ball and impact tracks
  - the launch-monitor block
  - phases, metrics, gates and timings
  - the camera MP4s
- **Export:**
  - Shots or a whole session as a zip of MP4s plus `swing.json`.
  - A per-swing CSV from the data viewer.
- **Import:** phone (PPCP) session bundles and launch-monitor files.
- The app is GPL v2 or later.

**Gaps**
- **No per-shot metrics table** (one row per shot) for a session or the library. This is the export a researcher or a spreadsheet coach wants first.
- **The schema doc is stale** (`docs/reference/swing_json_schema.md`). It doesn't mention `.ppsw` and doesn't describe the `dtl` or `skeleton3d` blocks.
- No 3-D motion export (C3D, TRC, BVH).
- No import of other apps' swings.

**Can build now**
- **9a. Metrics CSV for a session or the library**, as a bulk job beside the zip export on the Sessions list. It holds every per-shot reading, the LM fields, outcomes and σ.
- **9b. Bring the schema doc up to date** and point it at the libppswing Python reader.
- **9c. TRC export of the 3-D skeleton.** TRC is plain text, so no library is needed. C3D would need ezc3d. Note that the skeleton is uncalibrated, so the export must say so.

**Blocked by**
- Nothing critical.

---

## Dependencies that gate the rest

| Dependency | State | Unlocks | What it needs |
|---|---|---|---|
| Camera calibration (#74) | Designed, nothing built | 2 (measured 3-D, calibrated σ, 3-D side bend), 3 (centre of mass in metres), 4c, 6, the triangulate step | Code for stages 1–4 and 9 needs no new kit; the ChArUco board for lens models; bench measurements (design §11) |
| Uncertainty census (#88) | Brief written, not started | 2 (validation), 6 (honest in-range) | No hardware. Mark's hard gate before validation and full release |
| Worn trunk session W3/W4 (#109) | Sensors and calibration routine ready | 2 (rotation and sequence truth), 3 Part B, 6 trunk cue | Mark wearing the sensors and the wG3 |
| Force plates (#72 Part C) | Designed, nothing built | 3 | Two Wii balance boards first; borrowed plates for a reference session |
| Marker-based reference | None | 2 (benchmark against GEARS or Vicon) | Access to a lab |
| P-position reliability (#3, #4, #24, #30, #70) | Known bugs; timeline fusion designed, not built | 4, 7 | Code only |
| Analysis latency (about 15 s) | Pose already 2–3× faster | 6 (anything real-time from cameras), 7 | Mostly the frozen-window change (7a), not more speed |
| Hosting | None | 8 (golfer viewer, share link) | Mark's call |

## Candidates to plan now

Ranked by value for the effort. All need no new hardware.

| Rank | Item | Feature | Size | Needs first |
|---|---|---|---|---|
| 1 | Numeric shot filter + value contrast across sessions (1a, 1b) | 1, anti-feature | Medium; a ledger schema bump and re-grade | DS approval |
| 2 | Metrics CSV for a session / the library (9a) + schema doc (9b) | 9 | Small | — |
| 3 | Dated trends on Home for faults and LM figures (5a) | 5 | Medium | DS approval |
| 4 | Capture the next swing sooner (7a) + trimmed export (7b) | 7 | Medium | Mark's call on the margin |
| 5 | Video side-by-side by P-position (4a) + best-swings pool (4b) | 4 | Medium–large | DS approval; Mark's call on "best" |
| 6 | Saved marks, angle tool, session notes, clip with marks drawn in (8a–8d) | 8 | Medium | DS approval |
| 7 | Honesty fixes: stale text (2a), "not characterised" label (2c) | 2 | Small | DS approval for 2c |
| 8 | Uncertainty census (2b) | 2 | Large; analysis work | — |
| 9 | After-shot range tone with fading (6a) | 6 | Small–medium | Mark's ruling on the audio rule |
| 10 | "Same setup as last time" (7c); session context tag (5b) | 7, 5 | Small each | DS approval; Mark's call on the tag's choices |
| 11 | "What drives your ball flight" across sessions (1c) | 1, anti-feature | Medium–large; needs a stored per-session readings summary | Builds on 1b |

Calibration stages 1–4 and 9 are also buildable now, but they belong to the calibration programme (#74), not to this list.

---

## Market reference (Nov 2026)

| # | Killer feature | Leading apps | PinPoint Studio today |
|---|---|---|---|
| 1 | Launch-monitor fusion | Swing Catalyst, Trackman, Foresight FSX Pro (plus Sportsbox integration) | Every shot fused; outcome filter across sessions; no numeric query |
| 2 | Validated 3D kinematics | GEARS (marker-based reference), AMM (electromagnetic 6-DOF, TPI 3D tour database), K-Motion (IMU vest), Sportsbox AI, Theia3D | Two-camera 3-D skeleton fitted with the club; trunk IMUs on the Direct routes; partial σ; uncalibrated; not benchmarked |
| 3 | Ground reaction forces | Swing Catalyst (1000 Hz dual force plates, $12,500; force vectors drawn on the video); BodiTrak is pressure-only (vertical force + CoP, no shear/torque) | None |
| 4 | Phase-normalised comparison | Sportsbox AI, GEARS, V1 (manual sync only) | P-aligned chart overlays; no video side-by-side |
| 5 | Variability and trends | Trackman (Combine/Test Center), HackMotion — **market gap** | Strong within a session; trend arrows across sessions; no dated chart |
| 6 | Real-time biofeedback | K-Motion (140+ drills with visual and audio cues; "train to your best swing"), HackMotion | None |
| 7 | Frictionless capture | Swing Catalyst, Trackman, Swing Profile (phone) | Automatic detection and pairing; capture pauses while a shot is processed |
| 8 | Coach workflow | V1, OnForm, CoachNow | Report PDF by email; unsaved telestrator; no remote viewer |
| 9 | Open data | GEARS, Theia3D, Kinovea — **market gap** | Open format and raw lanes; no metrics table yet |
| ✗ | Anti-pattern (metric overload) | Main risk with the data-rich suites (GEARS, Trackman's full parameter set, 3D dashboards) | Work-ons and three factors keep the top level small |

Notes:
- **Swing Catalyst** comes closest to covering everything: up to four synced high-speed cameras, force and pressure plates, launch-monitor auto-record, and now markerless motion capture.
- **Sportsbox AI** is the markerless leader (PGA of America official 3D software; acquired by a DeChambeau-led group, Apr 2026). It lifts single-camera 2D video to 3D, so accuracy claims need scrutiny.
- **HackMotion** is the best wrist biofeedback example: adaptive guided practice plus ball-flight tagging after each shot.
- **Rows 5 and 9 are open opportunities** that fit PPS's open-source positioning. PPS is closest on both: items 5a and 9a would close most of the remaining gap.
- Theia3D is included from general knowledge and has not been freshly verified. GEARS, K-Motion, Swing Catalyst and AMM were checked on 12 Nov 2026 (next section). The GEARS and AMM detail comes mostly from launch coverage, so newer versions may exist.

---

## Against the 3-D biomechanics systems (12 Nov 2026)

GEARS, K-Motion, Swing Catalyst and AMM are the systems a coach would set PPS against on body measurement. This is the same question as features 1–9, asked from the competitor side. Item numbers (2d, 6a …) and issue numbers refer to the sections and tables above.

### What each one is

| System | Technology | What it measures | Distinctive strength | Price (where published) |
|---|---|---|---|---|
| **GEARS** | Eight high-speed cameras; 26 body markers and 6 club markers (OptiTrack platform) | Body angles and lines, kinematic sequence, club in 3-D, ball for impact location | Full 3-D club delivery at sub-millimetre resolution — the lab reference | About $39,500 at launch |
| **K-Motion** (K-Vest, K-Coach, K-Player) | IMU vest, belt and glove | 3-D trunk and arm motion, kinematic sequence, setup by club ("My Bag") | The training loop: capture the best swing, then train it with 140+ biofeedback activities; cloud link from coach to player | Not published |
| **Swing Catalyst** | Up to four synced high-speed cameras; pressure and force plates; launch-monitor integration | Video, ground reaction force, CoP heat map, markerless joint angles and centre of mass | Measured force drawn as vectors on the video; mature coach video tools and shareable lessons | Dual Force Plates $12,500, plus a Pro+ subscription |
| **AMM** | Electromagnetic 6-DOF sensors; TPI 3D reports | 200+ parameters, including sway, thrust, lift, pelvis turn, forward and side bend | True segment translations; a tour-pro reference database behind every number | Walkabout 6D under $10,000 (2012) |

AMM's electromagnetic field is distorted by metal, so its sensors cannot go on a steel club.

### Parity, capability by capability

| Capability | Leader | PPS today | Gap | Closed by |
|---|---|---|---|---|
| 3-D segment rotations | GEARS, AMM, K-Motion | skeleton3d two-camera fit (Estimated); trunk IMUs (Direct) | **Architecture at parity; accuracy unproven** | #74, #109 |
| Translations (sway, thrust, lift) | AMM, K-Motion | 2-D: sway and lift face-on, to impact; thrust down-the-line with a ruler; nothing for the thorax | **Large** | 2d now (as % of stance); measured after #74 |
| Kinematic sequence | All four | Peaks with σ; gyro peaks for pelvis, chest and arm when worn | At parity on worn swings; the camera routes are weaker (arm +43 ms) | #109, 2f |
| Club delivery | GEARS | Impact: LM covers it. Through the swing: shaft plane and path fused in 3-D, in the same model as the body | Small. Face through the swing is derivable when the wG3 is worn (palm roll + camera shaft, checked against LM face and closure rate); shaft deflection is not measured | 2g |
| Measured GRF / CoP | Swing Catalyst | Two 2-D proxies | **Large** | #72 (Wii boards); a reference on borrowed plates |
| Synced multi-camera video | Swing Catalyst | Face-on, down-the-line and impact roles; software sync | Parity; no hardware sync | Trigger cabling |
| Coach video tools | Swing Catalyst | Telestrator (unsaved); P-aligned chart overlays | Medium | 4a, 8a–8d |
| LM integration | Swing Catalyst | Three connectors; GSPro OpenConnect coming | Near parity | — |
| Biofeedback training | K-Motion | None | **Large** | 6a, 6b |
| Normative reference | AMM (TPI 3D), GEARS | Literature corridors; comparisons against the player's own swings | A different basis, and a defensible one | — (no tour corpus planned) |
| Coach–player platform | K-Motion, Swing Catalyst | Report PDF by email | Medium | Hosting (Mark's call) |
| Fidelity against markers | GEARS | About 7 px reprojection (~2 cm at the golfer) | Stays | A marker-lab benchmark |

### Where PPS leads

None of the four has any of these:
- **The causal diagnostic network.** The competitors give numbers, some with tour comparisons. None models signals → conditions → consequences, explains root causes, or lets coaches maintain the rules.
- **Shot-outcome explanation and Tendencies.** "What happened there?" is answered for each shot and each session, with outcomes classified automatically from the LM. The competitors show LM numbers next to video.
- **Session statistics.** Chains are confirmed over a session, not read from one swing.
- **Body, club and ball in one fitted 3-D model.** Only GEARS does this, and it needs eight cameras and a marker suit.
- **IMUs as truth for the camera routes.** The worn sensors improve the results for camera-only users, not just for the wearer.
- **Open source, an open format and commodity hardware**, against $10k–40k+ closed systems.

### Actions to close the gaps

Ranked by how much each changes the comparison.

| Rank | Action | Gap it closes | State | Ref |
|---|---|---|---|---|
| 1 | **Camera calibration**, stages 1–4 and 9, then the lens models | 3-D accuracy, metres, a real heading and target line, measured translations. Removes the 7 px residual and the knee and spine biases (skeleton3d §12.5) | Designed; nothing built; board in hand | #74 |
| 2 | **The worn reference session**: both cameras, trunk IMUs, wG3 and LM, giving the IMU-vs-camera table | Proves rotations and the sequence; settles the thorax disagreement (corr 0.37); gives σ and bias for every camera route; the evidence to cite at launch | Sensors and routine ready | #109 |
| 3 | **Witmotion clock fit** from the hop spike (2f, new) | Sequence order is decided by tens of ms, and Witmotion samples carry host-arrival times from a bursty link | In the trunk design §7.1; not built | 2f |
| 4 | **σ on the IMU routes**, then the census | The Direct routes claim more than they show (`body_rotation.cpp:112`) | Not started | #88 |
| 5 | **Trunk data hygiene**: `imuIntegrity` parity on a Witmotion swing; an IMU-only job run end to end | Trunk numbers believable at all | Open | — |
| 6 | **3-D sway, lift and thrust**, thorax included, from skeleton3d | Translations | Can build now, as % of stance before calibration | 2d |
| 7 | **Coach video tools** | Swing Catalyst's strength | Can build now | 4a, 8a–8d |
| 8 | **A training loop**: after-shot tone with fading; wG3 cue at the top; work-on drills measured on the measures they target | K-Motion's proposition, and the "help the golfer improve" layer | Mark's call | 6a, 6b, work-ons |
| 9 | **Force**: Wii boards, then a reference session on borrowed plates | GRF | Hardware | #72 |
| 10 | **A marker-lab benchmark** | GEARS's fidelity claim | External | — |

New items for section 2, **Can build now**:
- **2f. Witmotion clock fit.** Fit each trunk sensor's clock to the host from the landing spike of the calibration hop, which shows in both trunk accelerometers, both wG3 units and the video (trunk design §7.1). Until then every arm-versus-trunk gap carries both clocks' σ.
- **2g. Face angle through the swing (wG3 worn).**
  - The face follows the lead palm's full orientation (forearm rotation plus wrist), not wrist angles alone.
  - The cameras' fused shaft fixes two of the club's three rotations. The palm unit supplies the third, roll about the shaft, which skeleton3d already accepts as its HackMotion term (`hmSigmaDeg`).
  - Set a constant grip offset with the face square at address. Validate against the LM's face angle and closure rate at impact, which gives the series a measured σ.
  - The wG3 samples adaptively up to 800 Hz through impact. Check that analysis uses the full-rate recording, not the 25–100 Hz live lane.
  - Resolve the quaternion anomaly first (trunk design §3.7: lower-arm quaternions turning ~50° through a downswing the gyro integrates to ~246°), or build from the gyro anchored at address.
  - Hand–club compliance near impact shows up as error; the LM check measures it. Camera-only swings don't get this.

### Sources (12 Nov 2026)

- GEARS: https://m.morgancreekgolf.com/teaching-academy/gears-golf · https://golfnewsstoriesonline.com/news/all/golf_news/naturalpoint_introduces_gears_3d_golf_swing_analysis_system.html · https://thesandtrap.com/forums/topic/76878-gears-golf-swing-analysis-system/
- K-Motion: https://www.k-motion.com/?p=1358 · https://thegolfwire.com/323950-2 · https://thegolfwire.com/328979-2 · https://www.hogwire.biz/?p=3908
- Swing Catalyst: https://shop.swingcatalyst.com · https://foresightsports.eu/complementary-technology/swing-catalyst · https://support.swingcatalyst.com/hc/en-us/articles/6091620159644 · https://swingcatalyst.com/products/dual-force-plates
- AMM: https://www.newswire.com/news/advanced-motion-measurement-releases-walkabout-6d-golf-system-83126 · https://golfmonthly.com/gear/gear-news/st-andrews-links-academy-adds-new-analysis-equipment-9093 · https://www.mytpi.com/articles/technology/capture-methods-overview
