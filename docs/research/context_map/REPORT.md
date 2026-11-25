# Is a context map the right shape for R-CTX-1? Side-track report, 24 Nov 2026

**Status (25 Nov 2026): remembered for later, not adopted.** The Product Owner and the Solution Architect agreed with this report's verdict, and an outside review (another vendor's assistant) agreed too[^ctxr]. It comes back when Coaching has code of its own (#277). This side track was run in `build/` and moved here so it isn't lost; the brief beside it keeps its original paths. Re-run the count with `python3 docs/research/context_map/check_includes.py` (`includes.csv`, 194 KB, is not kept; the script regenerates it with `--csv`).

**Verdict: remember for later.** On today's code both rules reject the same real problems. The map removes two of the three carve-outs by renaming them, and pays off only when Coaching has code.

## 1. What was tested
The information model (IM, `governance/artefacts/information_model.md` v1.10.1) orders three contexts in rule R-CTX-1 (§2), Acquisition → Diagnostics → Coaching: a later one may include an earlier one, never the reverse. The day it was ruled it gained three carve-outs: Domain + Application as a **hub** reached through its contracts (Q26); a **Shared** tag (Q27); and **deferral tags** (capture code in `Gui` stays tagged hub until Stage D; the drill pack stays tagged Diagnostics). Lint A9 will check it from Stage B (the platform refactor's folder moves; Stage D later splits capture out of `Gui`). The hypothesis: a **context map** ("only through a named contract or the shared kernel") is the better shape.

`check_includes.py` (beside this file; run at commit `4ae8ac9c`) tags the 1,041 C++ files of `src/` (tests excluded) by §4.1's Context column and resolves quoted includes as the lint does: 2,966 resolve inside `src/`, 28 are third-party or generated, none is ambiguous. It judges the 963 that cross a context under both rules, on two taggings: **ruled** (§4.1 as written) and **true** (the deferrals undone). Every row is in `includes.csv`.

## 2. The map
| # | From → to | Named contract | IM ids |
|---|---|---|---|
| 1 | any → Platform | the base | §4 RING-PLATFORM, R-CON-5 |
| 2 | any → Shared | the shared kernel (`Identity`, `Core`'s vocabulary); it includes only Platform | RING-VOCABULARY, Q27 |
| 3 | Diagnostics → Acquisition | **the Recording**: Acquisition's Vocabulary-ring files | §2.2, OBJ-RECORDING, R-REC-4, lint A8 |
| 4 | Coaching → Diagnostics | **the results**: result value types only | §2, OBJ-SHOT-DERIVED, OBJ-SHOT-RESULTS, OBJ-SESSION-DIAG, -SUMMARY, -SERIES, OBJ-ATHLETE-DIAG |
| 5 | a context → hub | **the hub's contracts**: `DomainModel`'s interfaces and value types | R-CTX-1, Q26 |
| 6 | hub → a context | composition, unbounded | R-CTX-1, DEC-48 |
| – | any other pair | none | R-CTX-1's "Forbids" |

Rows 2, 5 and 6 are Q27 and Q26 as first-class rows. Row 6 is not a contract.

## 3. The counts
| Class | Includes |
|---|---|
| (a) fits a named relationship | **886**: hub composes 403, Platform 208, shared kernel 149, one context once deferrals are undone 74, Recording 46, hub contract 6 |
| (b) passes today only by a carve-out; the map rejects | **68**: 58 by a deferral tag, 10 where the rules differ |
| (c) fits neither rule | **9** |
| the map allows, today rejects | 0 |

Rejections: today's rule 9 on ruled tags and 67 on true tags; the map 15 and 77. **The deferral is not a property of the chain: either rule needs it, for the same 58 includes.** Carve-outs at work on ruled tags: Shared 149, hub contract 3, hub composes 479. I count the hub and Shared includes as (a), because the map names them.

**Calls made** (all in the script's tables; each moves a count):
- "To place in Stage B": `Body`, `Wrist`, `Skeleton3d` and `LaunchMonitor`'s inferred reads → Diagnostics. `LaunchMonitor`'s connectors, `Audio` input, `IMU`'s back-ends and `device_enumerator` → Acquisition. `kf3_smoother` → Shared. The rest (`LLM`, `STT`, `TTS`, `Secrets`, `Update`, audio output, `pp_settings`, `studio_profile`, `app_info`, `motion_capture_probe`, `mp4_frame_reader`, `recorded_products`) → hub.
- **L2's list is short.** §4.1 names three IMU files as Acquisition's signal maths. Eleven more (five `hm_*` wire and device types, the three orientation filters, `impact_detector`, the two trunk ceremony files) read as device or signal-level, and the device back-ends include them. Tagging them Acquisition is my call; `hm_frame` (the HackMotion-to-anatomical frame map) I left in Diagnostics. Read literally (`--literal-l2`), (c) is 18: nine more includes from `IMU/hm_session_worker`, `imu_base` and `imu_io_worker`.
- True capture set: `Gui/cameras`, `Gui/imu`, and `Gui/shot`'s `shot_processor`, `capture_context`, `shot_controller`, `shot_arbiter`, `shot_source_map`, `ppcp_clip_filer`.
- Hub contract set: IM has not named it. Three headers are hit (`recording_source.h`, `shot_command_values.h`, `recording_repository.h`); my other eleven are guessed from file names.

### The (c) list: rejected by both rules
| Include | Problem | Status |
|---|---|---|
| `src/Recording/recording.h:45` → `Vocabulary/Analysis/swing_analysis.h` | The Recording contract depends on Diagnostics' vocabulary (`BallTrack2D`, `BallBaselineRef`, `ImuSegmentBinding`) | **new** |
| `src/Video/video_overlay_pose.h:24` → `Pose/pose_estimator_base.h` | A device directory includes Diagnostics; the ruled amendment (AM6) says none does | **new** |
| `src/IMU/trunk_stand_tall.h:45` → `Vocabulary/Analysis/trunk_calib_provenance.h` | Acquisition's signal maths includes Diagnostics' vocabulary | in `build/platform/D/REVIEW-D1.md` |
| `src/Diagnostics/measures/measure_sample.cpp:28` → `Export/swing_store.h` | Diagnostics reads the store | known; the platform plan's item P-3 removes it |
| `src/IMU/hm_session_worker.cpp:28` → `IMU/hm_frame.h` | A device back-end includes Diagnostics' frame map | gone if `hm_frame` is placed Acquisition |
| `src/Ppcp/ppcp_host_inventory.cpp:30`, `ppcp_host_service.cpp:47`, `ppcp_pairing_store.cpp:73` → `Core/pp_settings.h` | Acquisition reads application settings (lint A8 calls the file application state) | gone if `pp_settings` is placed Platform |
| `src/Analysis/swing_reanalyzer.cpp:20` → `Analysis/recorded_products.h` | Diagnostics includes the store's serialiser | gone if it is placed Diagnostics |

The first four need none of my placements: **A9 cannot land "with no allowlist line" until they are fixed, under either rule.**

### The (b) list (with targets: `b_and_c_grouped.txt`)
- **Deferral, capture → Diagnostics (19):** `Gui/cameras/camera_instance.cpp:51,55`; `camera_instance.h:58,59`; `Gui/imu/hm_instance.cpp:26`; `imu_instance.h:36`; `imu_role_map.h:27`; `Gui/shot/capture_context.cpp:38`; `capture_context.h:61–63`; `shot_processor.cpp:35–39`; `shot_processor.h:42–44`.
- **Deferral, capture → hub past its contracts (35):** `Gui/cameras/camera_instance.cpp:25,34,73`; `camera_instance.h:41`; `camera_manager.cpp:26`; `camera_manager.h:28`; `camera_role_probe.cpp:24`; `Gui/imu/imu_manager.h:29`; `trunk_ceremony_recorder.cpp:22,25`; `Gui/shot/capture_context.cpp:21,22,27,32,34–37`; `capture_context.h:64,66`; `ppcp_clip_filer.cpp:23,24,26`; `shot_controller.cpp:25`; `shot_processor.cpp:21,33,34,43–46`; `shot_processor.h:21,45,46,49`.
- **Deferral, Diagnostics → the drill pack (4):** `Diagnostics/engine/diagnostics_health.cpp:21`; `diagnostics_health.h:22`; `Diagnostics/packs/file_pack_provider.cpp:24`; `swing_themes_pack.h:59`.
- **The rules differ: the chain allows, the map rejects (10). These break the hypothesis.**
  - Diagnostics reusing Acquisition's signal maths, which is not the Recording (6): `Body/body_rotation.cpp:21`; `IMU/hm_binding_recon.h:29`; `imu_refusion_check.h:30`; `imu_vision_fuser.cpp:27,32`; `orientation_refuser.h:27`. Two of the six hit files IM itself tags Acquisition. Legitimate reuse. The chain's "contracts and rules" covers it; the map needs a seventh row, or the maths moved to Shared.
  - The drill pack, once Coaching, including Diagnostics' pack machinery (4): `Diagnostics/packs/drill_pack.h:21,22`; `drill_pack.cpp:20,22`. Not the results contract. The map forces a decision on where pack machinery lives; the chain passes it unseen.

## 4. The two rules compared
- **Real problems rejected:** the same; the (c) list is identical.
- **Legitimate cases rejected:** the map rejects ten more, until it gains a signal-rules row and a home for pack machinery.
- **Carve-outs:** the chain has three. The map has one (deferral) and six rows, one unbounded. The true part of the hypothesis: **v1.10.1 is already a context map with a chain sentence on top.**
- **Unguarded by both:** hub → context is 403 of 963 crossings (327 into Diagnostics; 46 are `Gui/charts` including its rules and engine). Bounding it means naming each context's published headers: new design work.
- **For A9:** both need the same tags and hub contract set. The chain adds an order comparison; the map adds two header sets derivable from the tags, absorbs lint A8, and closes a hole (A9 does not say what Shared may include; no hits today). Neither is clearly simpler.

## 5. The coaching track (`docs/design/coaching_layer.md`)
| Need | Today's chain, as A9 checks it | The map |
|---|---|---|
| Golfer model, playbook: read results | Coaching may include any Diagnostics header, the engine too; "never re-derives" is unchecked | Row 4 checks it, once the header set is named |
| Practice log: rehearsal swings, shot intervals (PRA-R8) | Coaching may include Acquisition's ring and devices; "never reads raw streams" is unchecked | Rejected: the hub's events (§7.4), or a new row |
| Play source (WMP-R2), an Acquisition adapter | Passes by order, unexamined | A new row (Acquisition → Coaching), or the hub's typed input |
| External uploads (ASY-R1) | The Recording; no exception | The same |

The chain needs no new exception because it permits everything from a later context; its limits are words A9 cannot check. The map needs one or two new rows and can check them. One fault under either shape: R-CTX-1 has Coaching read results "through the owners", and forbids every context to include the owner classes.

## 6. Recommendation: remember for later
- **Not "apply now":** it changes no verdict on a real problem, keeps the deferral, and reopens a rule ruled today while Stage B is in flight. A9's data is the same under both, so switching later is one function in the lint.
- **Not "drop":** once Coaching has code, three of its rules would rest on unchecked words.
- **Trigger:** the coaching track's first information-model change, or Stage D lifting the capture deferral.
- **Applying would change:** R-CTX-1 (reworded as §2's table, the order kept as a consequence), §1.4's A9 with A8 folded in, §2's opening and diagram caption, §4.1's Context column, GL-43, and new ids for the rows: a C9 and C11 change for the Solution Architect. Cost: half a day of IM text, about 30 lines of lint, no code moves, and decisions on the signal maths, the pack machinery and the play and practice rows.
- **Do in Stage B under either shape:** fix or rule the first four (c) includes; widen L2's list; name the hub contract set; have A9 reject a Shared file that includes a context.

[^ctxr]: The Product Owner, 25 Nov 2026, in the platform refactor's coordination session.
