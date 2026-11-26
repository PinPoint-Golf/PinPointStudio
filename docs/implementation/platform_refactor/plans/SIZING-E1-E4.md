# Sizing E1 and E4: a quit test for every long-lived thread, and the five screen-facing globals

Read only, at `main` 126e9cf7 (26 Nov 2026). Nothing was changed, built or run for this page. The evidence is the code (file:line at 126e9cf7) and the counts below, each with its command (§3). The two rulings sized here are E1 and E4 of "Morning batch 4"[^mb4].

**In plain words.**
- **Narrowed, 26 Nov 08:50**[^e1-narrow]: E1 is E1-0, E1-cam and E1-imu only, about 7.5–12.5 h. E1-mic, E1-tts and E1-phone are dropped; their threads (Q5–Q8, S3) join §1.5's list of threads named as not pinned at SE-11b. E4 is taken as sized. The figures below are the sizing as it was put.
- **E1, about 13.5–22.5 h, in six items.** Today a headless test can make only the pose thread busy. Each device thread needs a stand-in device that runs inside the real app, behind a build option, with no real hardware.
  - Five stand-ins are sized: the camera (built on Stage D's synthetic camera, SD-2t), the Witmotion sensors with the Bluetooth scan, the microphone with speech in, speech out, and the phone link's listener.
  - A sixth item holds the parts they share.
  - Two stand-ins are left out, each with its reason: the HackMotion sensor's own stand-in (4–8 h more) and a phone actually connected (needs work in libppcp).
- **What E1 can't prove.** A stand-in tests what the app does when a device is slow at quit. It can't test how slow the real camera, sensor or speech engine is, and it can't test Windows, because the quit tests run only on the Mac and Linux. This morning's ruling replaced the hand-run quits at the studio, so that is a gap that remains. ~~The Product Owner may choose to close it with about 10 minutes at visit 3 (§1.4).~~ Withdrawn, 26 Nov: a visit now holds only steps that need clubs and a ball hit[^recut]. The gap stays named at Stage E's close (§1.4).
- **E1 costs more than the helper it replaced.** The helper lever 8 dropped was 9.5 h. The rulings pack said so when it put this option[^mr2], and the Product Owner chose it with that in view, so this page sizes it as ruled.
- **E4, about 3–4 h, one item.** The five globals move under `app`: `app.ttsController`, `app.transcriptionController`, `app.profiler`, `app.resourceMonitor` and `app.cudaRuntime`. That is 182 reads in 10 QML files and about 25 lines of C++. A new lint checks that every name still resolves. No component and no look changes. The information model's §6 `app.state` rows don't change; one §11 status row does.
- **Needs approval:**
  - Product Owner: E1's two cuts; ~~whether the studio's hand-run quit stays for the real devices~~ (withdrawn[^recut]); stand-in devices built into the Debug app.
  - Solution Architect: the sensors' transport interface; the five globals' new homes (the model files them as devices and logging today); the new lint.
  - Design Lead: nothing.

## 1. E1: every long-lived thread gets an automatic quit test while it is busy

### 1.1 Which threads, and the starting point

The threads are the ones the thread inventory lists for the threading rule (R-CON-5): Q1–Q8 and Q11, long-lived with an event loop; S1, S3 and S4, long-lived with none; and S2, already pinned[^se6a]. Five more are decided case by case:

| Thread | In E1? | Why |
|---|---|---|
| S2, the pose thread; S5, the warm-up | done | pinned by SE-1b (`warm-up`, `after-warm-up`)[^se1b] |
| Q1–Q4, the camera's capture, preprocessing, preview pose and ball threads | **in** | E1-cam |
| S1, the event buffer's merger | **in** | busy whenever any stand-in streams into the buffer; no work of its own |
| Q11, the sensors' I/O thread | **in** | E1-imu. It is **one shared thread**: the Witmotion and HackMotion objects all live on it (`imu_manager.h:361`, borrowed by `imu_instance` and `hm_instance`). A Witmotion stand-in makes it busy |
| Q10, the Bluetooth scan | **in** | E1-imu. A new thread per scan, not long-lived, but it holds the one teardown that kills a thread (D2) |
| Q5, Q6, Q7, the microphone, speech buffer and speech in | **in** | E1-mic |
| Q8, speech out | **in** | E1-tts |
| S3, the phone link's accept thread | **in, listening only** | E1-phone |
| S4, the wired phone link | out | needs a stand-in for Apple's usbmuxd. The one that exists is test code (`src/Ppcp/tests/usbmuxd_stub.h`). Not sized (proposed, for the Product Owner: with Stage F's follow-up, #272) |
| Q9, the language model | out | off unless `PINPOINT_ENABLE_LLM=1` (`llm_controller.cpp:102–107`); a golfer can't reach it; the inventory leaves it off the studio list too[^se1b] |
| J12, the industrial cameras' frame loops | out | the vendor back-ends' own code (`VideoInputSpinnaker.cpp:341–360`, `VideoInputAravis.cpp:289–304`). A stand-in replaces it; it can't test it (§1.4) |
| S6, the pose benchmark | out | finite, not long-lived. Reachable headless with one more probe switch (about 1 h), but not asked |
| J1, a shot in flight | out | not a thread. Once E1-cam and a trigger exist (E1-mic's click or E1-imu's impact), a pin quitting a shot in its post-roll or save is about 1–1.5 h more. That is Stage D's teardown item's evidence (SD-8t), not E1's |
| P1–P9, the pools; J2–J11, the pool jobs | out | pools and jobs, not threads an owner starts and stops. Morning batch 4's E3 puts the pools in the threading rule by name and on one issue[^mb4]. None needs a device: SE-1b lists them as reachable headless with a bigger fixture or a probe switch, and not pinned[^se1b] |
| The launch monitor | out | no thread of its own: the inventory's greps find none in its files, and its connector runs on the GUI thread (`git grep -nE 'std::thread\|QThread\|QtConcurrent\|moveToThread' -- 'src/Gui/launchmonitor/*' 'src/LaunchMonitor/*'`: 0) |

**Why only the pose thread can be made busy today.** Under `PINPOINT_HEADLESS_PROBE=1` every device part refuses to start and logs `[HeadlessProbe] not started: …`:
- the cameras, `camera_manager.cpp:48–51`;
- Bluetooth, `imu_manager.cpp:42–47` and `:546`;
- the scan, `device_enumerator.cpp:342–343`, which logs REFUSED, so the test fails;
- the microphone and speech, `transcription_controller.cpp:114–117`;
- speech out, `tts_controller.cpp:106–109` and `:125`;
- the phone link, `main.cpp:1958–1962`;
- the launch monitor, `launch_monitor_controller.cpp:114`.

The quit tests run inside that mode (`src/Gui/tests/quit_pins/quit_pin.py:78`, the `PINS` table).

**SD-2t's synthetic camera does not give E1 its camera test on its own.** It is built for SD-2a's capture rig, a test program that links the app's sources but not `main()`. The Stage D design chose that rig and rejected driving the built app with an injected mode, because that "puts a test-only mode in the product"[^t1]. A quit test has to run the real app's `main()` and teardown. So E1 needs a stand-in inside the app binary the quit tests launch, which is the mode the rig avoided. **This page proposes it behind the build option SD-2t already plans: on in Debug trees, off in every Release and installer build, so a shipped app never holds it** (proposed, for the Product Owner, the scope; and the Solution Architect, the build). Under the option a stand-in starts only when both switches are set: `PINPOINT_HEADLESS_PROBE=1` and a new `PINPOINT_PROBE_SYNTHETIC=<parts>` (proposed name). Every real device stays refused, and the REFUSED backstops stay.

### 1.2 What each stand-in needs, and the shared part

Each device needs five things:
1. the stand-in itself;
2. a way past its headless gate;
3. a trigger that makes its thread busy;
4. log markers for "busy" and "done";
5. a pin row in `quit_pin.py`.

Each stand-in's gate sits beside its owner's existing headless gate, and each pin's trigger comes from the pin's probe file, as SE-1b's pins do. Nothing is added to `main.cpp` except by E1-phone, whose gate is already there. That is why E1-phone waits for SE-2c, so SE-2c's count of probe switches in `main.cpp` (9 → 0) doesn't move again.

**A stand-in must also be able to be slow on purpose.** No teardown has a time limit except the scan's, and each waits for the call in progress (H5)[^se6a]. A stand-in that answers at once passes every test and proves nothing. So each one takes a "slow" setting, for example `PINPOINT_PROBE_SYNTHETIC_SLOW=<call>:<ms>` (proposed), that makes its stop, its model load or its disconnect take as long as asked. The test then shows what the owner does while it waits: whether the window closes and the app lingers (GAP-20), and whether anything runs on a destroyed object (GAP-21).

**E1-0, the shared part: 1.5–2.5 h.**
- the build option on the app target (SD-2t then puts its camera under the same option);
- one small header beside `src/Core/headless_probe.h` that reads the two switches;
- `quit_pin.py` reports Not Run (77) when the app was built without the option. Each stand-in logs one `[HeadlessProbe] synthetic: <part>` line, which the driver looks for;
- the slow setting's convention;
- the testing guide's "The quit pins" section, and the lifecycle model's quit-test rows (APP-T3, APP-T9 and APP-T11 today).

**Every pin checks the oracle SE-1b already has:**
- exit code 0;
- gone within 5 s of `QUIT-PIN: quit requested` (GU-10's limit, ruled 5 s[^gu10]);
- no fault line, and no after-shutdown line (SE-1c);
- the busy marker logged before the quit, and the done marker not;
- no line of the busy work after the quit.

Each device adds one check: its owner logs that it stopped its thread before it was destroyed. That is the threading rule's own words for these threads: "owned by its device or media owner, which stops it before it is destroyed"[^v8].

### 1.3 Device by device

| Item | Threads made busy | Seam (file:line) | Hours | What the quit test checks, beyond the oracle | Shares SD-2t? |
|---|---|---|---|---|---|
| **E1-cam** | Q1, Q2, Q3, Q4, S1 | SD-2t's back-end: `VideoInputFactory::Backend` (`video_input_factory.h:31–40`) and the enumerator's `registerDevice` (`device_enumerator.h:106`). The gate: `camera_manager.cpp:48–51`, which lists only stand-in cameras under the switch. Trigger: capture and live preview, from the pin's probe file | **2–3.5** on top of SD-2t's 3 h. The top of the range is for what reading doesn't settle: Q2–Q4 start in the instance's constructor only in a build with pose on (`camera_instance.cpp:380–493`), and it isn't known whether MoveNet's model reaches a headless run, or preview frames reach Q3 and Q4 with no window | (a) quit while capturing; (b) while previewing, with preview pose and the ball detector running; (c) (b) with the capture thread's call made slow (the blocking hop, `camera_instance.cpp:603–608`). Each camera's stop is logged before the instance is destroyed; no frame is delivered after it | **yes**: the same back-end and build option. Needs SD-2t's camera to run on a real clock. The rig already needs that, because the post-roll is a real `QTimer`[^t2] |
| **E1-imu** (Witmotion and the scan) | Q11, Q10, S1 | The transport, which both sensor kinds build: `wt9011dcl_ble.cpp:58` and `hm_session_worker.cpp:134` (`new BleImuTransport`). Below the driver, so the driver, the instance, the manager and Q11 all run unchanged. A stand-in scan on the same thread and teardown (`device_enumerator.cpp:338–364`, `:240–248`) registers two stand-in sensors through `registerImuDevice` (`device_enumerator.h:111`). The gates: `imu_manager.cpp:42–47`, `:546`, `device_enumerator.cpp:342`; the adapter, `imu_instance.cpp:496` | **4–6.5**. The top of the range is for what reading doesn't settle: whether the WT901 driver needs its configuration and battery read-backs answered before it streams | (a) quit with two sensors streaming at 200 Hz into the buffer; (b) the same with a slow disconnect, against the stop barrier (`imu_manager.cpp:203–248`); (c) with raw recording on (`PINPOINT_IMU_RECORD_DIR`): the recording's tail is on disk (`imu_instance.cpp:702–716`; a loss check); (d) quit mid-scan: the 3 s wait; (e) a stuck scan: whether `terminate()` fires (D2), and what follows | pattern only: registration through the enumerator, as SD-2t does |
| **E1-mic** | Q5, Q6, Q7 | `transcription_controller.cpp:48` builds a concrete `AudioInput` (member type `transcription_controller.h:146`). The stand-in is an `AudioInputBase` (`audio_input_base.h:37`) that plays generated speech-like bursts and a club click. The gate: `:114`, with the macOS permission requests (`:126`, `:136`) skipped for a stand-in. Speech in: `stt_processor.cpp:63`, `:149` (`STTBackendFactory::createDefault`), with a stand-in back-end (`STTBackend.h:28–54`) whose model load and transcription take as long as asked | **3–5** | (a) quit mid-utterance; (b) during the speech model's load (Q7); (c) D1: quit while a permission is still unanswered, its answer arriving after the controller has gone. No line from the answer may follow the controller's destructor. By reading this is a fault[^se6a], so (c) is likely to fail, to be filed and kept as a known failure | enum value only: microphones register as `DeviceType::AudioInput` under a `VideoInputFactory::Backend` (`audio_input.cpp:92–94`), so SD-2t's new value can name a stand-in microphone too |
| **E1-tts** | Q8 | `tts_controller.cpp:74` builds a concrete `AudioOutput` (member type `tts_controller.h:190`). The stand-in is a silent `AudioOutputBase` (`audio_output_base.h:58–82`) that opens no device. The engine: `tts_controller.cpp:94`, `:624` (`TTSEngineFactory::create`, `TTSEngineFactory.h:26`), with a stand-in engine whose voice load and synthesis take as long as asked; no voice is downloaded. The gate: `:106–109`, `:125` | **2–3.5** | (a) quit mid-sentence; (b) during the voice's load; the worker's stop logged first (`tts_controller.cpp:146–161`). **Silent by construction**: no audio device is opened, so the rule that tests make no sound holds | no |
| **E1-phone** (listening only) | S3 | The gate: `main.cpp:1958–1962`. Under the switch the host listens on a free loopback port with discovery and the wired link off. `start()` turns both on today (`ppcp_host_service.cpp:386`, `:388`). The accept thread starts at `:329` | **1–1.5** | quit while the listener runs (today it never runs headless); its join (`ppcp_host_service.cpp:397–404`) inside the limit | no |
| **E1-0** | none | §1.2 | **1.5–2.5** | the Not Run path; the "synthetic:" lines | defines the option SD-2t uses |
| **Total** | | | **13.5–22.5** | | |

**Left out, with reasons** (proposed, for the Product Owner):
- **E1-hm, HackMotion's own stand-in, 4–8 h.** Q11 is already covered by E1-imu. What HackMotion adds is its own stop barrier (`hm_instance.cpp:880–896`, a blocking `shutdown()` that closes the library's session). A stand-in below `HmInstance` has to speak the wG3 protocol back to libwrist: its start-up, keepalive and history gather. The Stage D design said the same of faking it for the rig[^t2]. libwrist's frame builders are a test-only header in another repository (`libwrist/tests/wr_wire.h`), and code does not cross repositories. So it is either a device simulator added to libwrist or a copy written here from the specification. Recommended: not now. ~~HackMotion's barrier stays in the studio's hand-run list.~~ HackMotion's barrier is a named gap at Stage E's close; a quit with the real HackMotion streaming could be part of an optional desk check on the Mac (proposed, for the Product Owner)[^recut].
- **A phone actually connected.** No stand-in phone can reach the app's listener. `ppcp-sim` has no TLS transport (`src/Ppcp/tests/run-tls-host.sh`, its banner), and the plaintext option is compiled into `ppcp_conform_host` alone, as its own banner says (`src/Ppcp/tests/CMakeLists.txt:880`). That makes it libppcp work, not sized. Recommended: with Stage F's follow-up (#272), which is where the phone link's work is parked.

**The narrowest seam, and the production edits it still needs.** Each seam is a concrete class today, so each needs a small edit to shipped code with no change in behaviour:
- **The transport.** `BleImuTransport` (`ble_imu_transport.h`, 262 lines) is concrete. The two places that build it get a small factory, and its entry points become an interface: connect, disconnect, write and state, with its signals. About 40–60 lines move; behaviour is unchanged.
  - The alternative is a second copy of `ble_imu_transport.cpp` compiled under the option. It would take real Bluetooth out of every Debug app, including the one the owner uses with real sensors. Rejected.
  - A stand-in is chosen by its device id (a `synthetic:` prefix) from the scan that registered it. There is no new `ImuBase::Transport` value and no new `Device` field.
- **The microphone:** the member's type `AudioInput *` → `AudioInputBase *`. Of its eight uses in the controller, only `setDevice` is `AudioInput`'s own (with the static `AudioInput::availableDevices`).
- **Speech out:** `AudioOutput *` → `AudioOutputBase *`. Of its eight uses, two are `AudioOutput`'s own: `setPreferredFormat` and `clearPending` (`audio_output.h:74`, `:84`). Each moves up to the base, or is called through one cast.
- **The camera:** none beyond SD-2t's ("the manager and the shot processor are not edited"[^t2]) except E1's own addition: the gate at `camera_manager.cpp:48–51` gains one branch.

### 1.4 What the stand-ins cannot cover (a risk that remains)

A stand-in replaces the lowest layer, so the quit tests cover each **owner's** teardown: the thread, its stop order, and what runs after. That is what the threading rule names. The long waits by reading (H5) are all in vendor calls:
- Spinnaker's `start()` and `stop()`;
- a real Bluetooth disconnect, and libwrist closing its session;
- whisper loading its model;
- Kokoro loading its voice.

A slow stand-in shows what the app does while one of those is slow. It cannot show how slow the real call is. Nor can the quit tests reach Windows: they run on the Mac and Linux only (`run_headless.sh`; SE-1b's notes), and the Windows quit during the warm-up is marked "NOT TESTED" (`main.cpp:180–182`). This morning's ruling replaced the hand-run quits, so the vendor layer and Windows are a gap that remains. ~~**To close it, I'd add back about 10 minutes of hand-run quits at visit 3, one with each real device busy**, as the pack's §2 (a) put it[^mr2]. It costs a few minutes on a visit already planned, and it is the only evidence for those two. The Product Owner decides whether to opt in.~~ **Withdrawn (26 Nov):** E1 is automated, and a visit now holds only steps that need clubs and a ball hit[^recut]. What is left, all proposed for the Product Owner: the real cameras' quit on Windows is seen at visit 2, where a quit while a shot is analysing already needs a ball; the vendor layer on the Mac (the HackMotion, a Witmotion, the microphone, speech mid-sentence) could be an optional desk check, about 10 min; Windows' own quit points have no home (the pins on the studio PC are unplanned, roughly 4–8 h, and need a ruling: `docs/implementation/platform_refactor/visits/RECUT-26nov.md` §3).

### 1.5 Where each item goes, and what it waits for (proposed, for the Product Owner; the ids are the docs lane's to set)

*Since 26 Nov 08:50 only E1-0, E1-cam and E1-imu stay; the E1-mic, E1-tts and E1-phone lines below are dropped[^e1-narrow].*

- **E1-0:** Stage E, lane 2, now. It waits only for SE-1b and SE-1c, both done. It goes first because every other E1 item builds on it.
- **E1-mic, then E1-tts:** Stage E, lane 2, after E1-0 and after E4, because their pins may reach the two controllers by their QML names, which E4 changes. Their files (the two media controllers, `stt_processor.cpp`, the audio bases) are in no open item. SE-7e edits only `AzureTTSEngine.cpp` and `STTBackendAzure.cpp`, and SE-8 is done.
- **E1-cam:** Stage D, lane 1, straight after SD-2t and before SD-2b, so the timing pins and the quit pins share one back-end. Waits for SD-2t (and so SD-2a, after C-8b) and E1-0.
- **E1-imu:** Stage D, lane 2, after SD-6. SD-6 moves the enumerator out of `Core` and splits its `Device` struct, and the stand-in scan lives in that file. Not beside SD-12b (the sensor manager's settings fallbacks) or SD-1e (whose inventory it would make stale). Waits for E1-0.
- **E1-phone:** Stage E, after SE-2c. The gate is in `main.cpp` today, SE-2c moves the probe switches out of it, and SE-2a takes over the phone link's statics. Or it goes with Stage F's follow-up (#272).
- **Serial, not side by side:** every E1 item adds rows to the one `PINS` table in `quit_pin.py`.
- **SE-11b's Accept** ("each listed in the model, each covered by a quit pin"[^v8]) then holds for Q1–Q8, Q10, Q11, S1, S2 and S3. It names the rest as not pinned, with their reasons: S4, Q9, J12, HackMotion's barrier and Windows (§1.1, §1.3, §1.4). The pools and pool jobs are E3's issue, not threads.

### 1.6 Approvals for E1

- **Information model: none needed on these seams** (proposed, for the Solution Architect to confirm). The reason is the one the Stage D design gave for the synthetic camera: each stand-in is a file in its device's own directory, behind a build option, registered through an existing call (`registerDevice`, `registerImuDevice`). Nothing persisted records a back-end, transport or vendor:
  - the grep below finds only `shot_processor.cpp:1028`, which reads the vendor in memory;
  - the persisted device keys are a description and a serial or id (`src/Vocabulary/Rig/device_key.h`), so a stand-in's ids reach only the test's scratch settings and scratch library.
  
  Two cases would make it the Solution Architect's call:
  - the transport interface, a new interface in `src/IMU`, the same ring as the class it abstracts ("a new interface needs a ring", C9);
  - a stand-in sensor selected by a new `Transport` value or a new `Device` field instead of its id. SD-6's split of `Device` is the natural point to settle that.
- **Product Owner:** the two items left out (§1.3); ~~the hand-run quits at visit 3 (§1.4)~~ (withdrawn[^recut]); stand-in devices built into the Debug app binary (§1.1).
- **Design Lead:** none. No screen changes.

## 2. E4: the five screen-facing globals within the rule

### 2.1 What they are, where they're registered, and their QML reads

The rule: "Context globals are only devices, settings, logging and `app`" (R-QML-1). All 18 globals are registered in `main.cpp:1766–1779`, `:1810`, `:2007` and `:2015–2016`. The members hung from `app` are handed over at `main.cpp:2019–2046` (`setMembers`, `:2045`), and declared in `src/Gui/app/gui_app.h:43–127`.

| Global | Class, file | Registered | What it owns | QML reads | Files |
|---|---|---|---|---|---|
| `ttsController` | `TtsController`, `src/Gui/media/tts_controller.h` | `main.cpp:1773` | speech out: the voice's worker thread (Q8), the engine, the speaker | **52** | `AudioPage.qml` 47, `ScreenSessionSetup.qml` 2 (`:127`), `CalibrateTrunkPage.qml` 2 (`:87`), `tests/probes/headless_start.qml` 1 (`:76`) |
| `controller` | `TranscriptionController`, `src/Gui/media/transcription_controller.h` | `:1772` | the microphone (Q5), the acoustic shot detector, speech in (Q6, Q7) | **38** | `AudioPage.qml` 32, `MicrophonesPanel.qml` 4, `PpDetectCluster.qml` 1 (`:182`), `headless_start.qml` 1 |
| `profiler` | `ProfilerController`, `src/Gui/monitor/profiler_controller.h` | `:1777` | the app's one 1 s process sampler and its 60 s dump to the log (`profiler_controller.h:36–43`) | **55** | `ScreenResourceMonitor.qml` |
| `resourceMonitor` | `ResourceMonitorController`, `src/Gui/monitor/resource_monitor_controller.h` | `:1776` | a view over the buffer, cameras, sensors, phones and launch monitor (`main.cpp:612`, `:1966`, `:1970`), and the message log's export | **30** | `ScreenResourceMonitor.qml` 25, `HmRigCapability.qml` 3, `ScreenHome.qml` 2 |
| `cudaRuntime` | `CudaRuntimeController`, `src/Update/cuda_runtime_controller.h` | `:1778` | the updater's companion that offers the CUDA runtime (`cuda_runtime_controller.h:22–45`) | **7** | `GeneralPanel.qml` |
| **Total** | | | | **182** | **10 files** (one a test probe) |

Two corrections to earlier counts:
- `ttsController` is 52 reads, not 66 (H5's figure[^h5]). `TrunkRoutine.qml:136` and the setup screen's inner object (`ScreenSessionSetup.qml:131`) each declare a **property** of the same name, and the 24 uses inside them are that property. Three more aren't reads of the global either: a binding at `CalibrateTrunkPage.qml:118`, an object key at `tst_setup_trunk.qml:893` and a string at `:1211`. None of the 27 changes.
- `controller` is 38 reads, not "at most 139". Every other QML file that says `controller` declares its own property of that name.

**Test fakes that inject one of these by name:**
- `src/Gui/tests/setup/support/SetupDriver.qml:204` hands the voice in as a context property `ttsController`. It moves into the fake `app` object at `:197`.
- `src/Gui/tests/session_setup_lint_test.cpp:712` maps the name `ttsController` to `TtsController` for its member check. The row stays, for the property, and the item confirms that the check still covers it.
- `headless_start.qml:76` checks `!!controller && !!ttsController`.

### 2.2 The right home for each (proposed, for the Solution Architect)

**The premise conflicts with the model.** The model's §11 row for R-QML-1 (step 5.8b) and `gui_app.h:32–34` already file all five as devices or logging: "the voice in and out, `controller` and `ttsController`", `cudaRuntime` and `resourceMonitor` among the devices, and `profiler` with logging[^58b]. The rulings pack said they are none of those[^mr5], and the Product Owner's ruling follows the pack. So moving them reverses a classification the Solution Architect's step 5.8b recorded. That is the Solution Architect's to confirm. Read on what each owns, I'd put all five under `app`:

- **`app.ttsController`** beside `app.llmController`, the other media engine already there. The speaker it opens is a device; the controller is the voice's engine and queue.
- **`app.transcriptionController`**, renamed: `app.controller` would say nothing, and Q17's rule against renaming is superseded for these five[^mb4]. It owns a device (the microphone) and a shot trigger (the acoustic detector) as well as speech in. If the Solution Architect reads it as a device, the other route is a device global under a device's name (for example `microphone`). That splits speech in from a microphone it can't run without, so I'd not take it.
- **`app.profiler`**, not under logging. `appLog` is a two-method bridge that every test harness fakes by name (`app_log.h:29–30`), so hanging the profiler from it would widen a faked contract.
- **`app.resourceMonitor`**: it reads devices but isn't one.
- **`app.cudaRuntime`** beside `app.updateController`, whose companion it is.

Q17's ruling ("a global leaves only when its owner changes") is superseded for these five only[^mb4]. `appSettings`, `appLog` and the device globals are unchanged.

### 2.3 The change and its size: about 3–4 h

- **C++, 0.5 h:**
  - `gui_app.h`: five `Q_PROPERTY` lines, five `Members` fields and five getters, and its header comment (`:32–34`);
  - `main.cpp`: five `setContextProperty` lines removed (`:1772`, `:1773`, `:1776–1778`) and five `m.…` lines added before `:2045`.
  - The context globals go from 18 to 13, or to 12 once SE-4 moves `studioProfile`.
- **QML, about 1 h:**
  - 182 reads in 10 files, rewritten by a script and then checked by hand, because of three forms: `target: controller` (`MicrophonesPanel.qml:69`, `PpDetectCluster.qml:182`); the `typeof ttsController !== "undefined"` guards (`ScreenSessionSetup.qml:127`, `CalibrateTrunkPage.qml:87`), which become `typeof app !== "undefined" && app.ttsController`; and the test probe.
  - No line is wrapped, so `AudioPage.qml`'s two design-system held lines stay at 141 and 301 (`design_system_allowlist.txt:52`, the Design Lead's list).
- **Tests, 1–1.5 h:** §2.4.
- **Docs, 0.5 h:** the model's §11 status row (§2.5); `gui_app.h`'s comment; the GUI catalogue's seam page (`docs/implementation/gui-catalogue/cpp-seam.md`), which names the globals; the testing guide, if the lint is new.
- **Earlier sizing, for comparison:** SE-9b was this work plus `appLog`, at 3 h, and was struck under Q17. SE-4 moves `studioProfile`'s 62 uses in 2 files in about 0.5 h. The test half is what SE-9b's 3 h did not hold.

### 2.4 The test that the QML still resolves each name (through ctest only)

The headless start (`headless_probe_test`) fails on any `ReferenceError` or `TypeError` in the log while it visits every screen and Settings panel. That isn't enough on its own:
- it skips the Developer screen, where `AudioPage.qml` (79 of the 182 reads) lives (`headless_start.qml:59`);
- a read inside an `onClicked` or a `Timer` handler is never run by it.

So the item has two halves.

1. **A runtime check, in the existing probe.** `headless_start.qml` stage 0 checks that each of the five `app.<name>` is an object and each old name is `undefined`. If the Developer screen loads headless without a fault (not settled by reading), it joins the probe's screen list too.
2. **A static check, as a new ctest lint** (a `cmake -P` script or a small test; proposed, for the Solution Architect, because it enforces R-QML-1). It checks three things:
   - the names `main.cpp` registers are the ruled list;
   - no QML file outside the tests reads any of the five old names bare. The two same-named property scopes are named in the check by file and line, so a new bare read anywhere else fails;
   - every `app.<member>` that any QML reads is a property of `GuiApp` or `App`, so a misspelt `app.ttsControler`, which QML resolves to `undefined` without a word, fails.

   It is a lint, not a `tst_*.qml`: no UI test is added unasked.

The item runs:
- `headless_probe_test`;
- the new lint;
- `session_setup_lint_test`;
- the setup tests that open `ScreenSessionSetup` and `CalibrateTrunkPage` (`ctest -L qml_ui -R tst_setup`), because `SetupDriver.qml` changes;
- both lints, as COMMON says.

### 2.5 Approvals for E4

- **Design Lead: none.** No component, variant, token or look changes; a reference is re-pointed. `PpDetectCluster.qml` is a shell file with no page in `governance/artefacts/design_system/components/`. It isn't even UI-20's lighter route, because nothing on screen changes.
- **The model's §6 `app.state` rows: unchanged.** The five go on `app`, not on `app.state`; §6's tree is `AppState`'s. R-QML-1's own wording is unchanged too, since the code comes within it.
- **The model's §11 row "QML through `app`" (R-QML-1/2): a status edit.** The global count (18 → 13) and its list of which globals are devices, settings and logging change. The list is the Solution Architect's classification from step 5.8b, so the edit waits for §2.2's confirmation. The model's walkthrough deck may name the globals; its changes collect on #286.
- **Where it goes:** Stage E, lane 2, straight after SE-4, which edits the same lines of `main.cpp` and `gui_app.h` for `studioProfile`, and before SE-2a. SE-2a then moves 13 globals into the composition root, not 18. `main.cpp` takes one lane at a time, so E4 never runs beside SE-2a–d, C-4, C-11b.1 or C-14. E1-mic and E1-tts follow it (§1.5).

## 3. The counts, and how to re-take them

From the repo root, at 126e9cf7:
- the globals: `grep -c setContextProperty src/Gui/main.cpp` → 18;
- the members on `app`: `grep -c 'Q_PROPERTY(QObject' src/Gui/app/gui_app.h` → 23;
- whether a back-end, transport or vendor is persisted: `git grep -nE 'imuTransport|imuVendor|Backend::|"backend"|"transport"|"vendor"' -- 'src/Export/*' 'src/Gui/shot/*' 'src/DomainModel/*' 'src/Vocabulary/*' 'src/Gui/app/app_settings.*' | grep -v /tests/` → one line, `shot_processor.cpp:1028`, read in memory;
- the headless gates: `git grep -n 'not started' -- 'src/*.cpp'`;
- E4's QML reads: the script below → 52, 38, 55, 30, 7; 182 in all.

Run: `sed -n '/^# E4-COUNT-BEGIN/,/^# E4-COUNT-END/p' docs/implementation/platform_refactor/plans/SIZING-E1-E4.md | python3 -I -`

```python
# E4-COUNT-BEGIN
import re, subprocess, collections
files = subprocess.run(["git", "ls-files", "*.qml", "*.js"], capture_output=True, text=True).stdout.split()
# Every other QML file that says `controller` declares a property of its own by that name.
CTRL = {"src/Gui/media/AudioPage.qml", "src/Gui/settings/MicrophonesPanel.qml",
        "src/Gui/shell/PpDetectCluster.qml", "src/Gui/tests/probes/headless_start.qml"}
STR = re.compile(r'"(?:\\.|[^"\\])*"')
def skip(n, f, i, code):
    if n == "controller" and f not in CTRL: return True
    if n == "ttsController":
        if f.endswith("calibration/TrunkRoutine.qml"): return True                      # its own property (:136)
        if f.endswith("setup/ScreenSessionSetup.qml") and 129 <= i <= 175: return True  # the inner object's (:131)
        if re.search(r"(^|[{,])\s*ttsController\s*:", code): return True                 # a binding or key of that name
    return False
total = 0
for n in ["ttsController", "controller", "profiler", "resourceMonitor", "cudaRuntime"]:
    pat = re.compile(r"(?<![A-Za-z0-9_.])" + n + r"\b")
    per = collections.Counter()
    for f in files:
        for i, line in enumerate(open(f, encoding="utf-8", errors="replace"), 1):
            code = re.sub(r"//.*$", "", STR.sub('""', line))
            if code.strip().startswith("*") or skip(n, f, i, code): continue
            per[f] += len(pat.findall(code))
    per = +per; total += sum(per.values())
    print(n, sum(per.values()), "reads in", len(per), "files:", dict(per))
print("total", total)
# E4-COUNT-END
```

## 4. For ratification (one list)

- **Product Owner:**
  - E1's six items and their places (§1.5);
  - the HackMotion stand-in and a connected phone left out (§1.3);
  - ~~whether visit 3 keeps about 10 minutes of hand-run quits for the real devices and Windows (§1.4);~~ withdrawn[^recut]; instead, whether to plan the quit pins on the studio PC (§1.4);
  - stand-in devices built into the Debug app, off in Release (§1.1);
  - E4's place, after SE-4 and before SE-2a (§2.5).
- **Solution Architect:**
  - no model change for E1's stand-ins, and the transport interface's ring (§1.6);
  - the five globals' homes, including the rename to `app.transcriptionController`, against step 5.8b's classification (§2.2);
  - the new R-QML-1 lint (§2.4);
  - the §11 status edit that follows (§2.5).
- **Design Lead:** nothing to rule (§2.5).

[^mb4]: The Product Owner and the Solution Architect, 26 Nov 2026 06:55, `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Morning batch 4": E1 is automated ("each long-lived thread gets an automatic quit test while busy, not a hand-run quit at the studio"); E4 is fixed ("brought within the rule, not named in it. This supersedes Q17's 'a global leaves only when its owner changes' for these five").
[^e1-narrow]: The coordinator's call, 26 Nov 2026 08:50 (AG-3, for the Product Owner's review; `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Coordinator's calls" (1)): E1 narrowed to the camera and the sensors; the microphone, speech and phone quits dropped, because those threads hold no golfer data and a quit that lingers a few seconds is not an issue. Undo: add the three back (about 6.5–10.5 h).
[^recut]: The Product Owner, 26 Nov 2026, midday: "please do make sure those visits need golf clubs and a ball being hit. I don't want to waste time there doing checks we can automate or do at the desk." `docs/implementation/platform_refactor/visits/RECUT-26nov.md`.
[^mr2]: `docs/implementation/platform_refactor/rulings/MORNING-RULINGS-E.md` §2: option (c), "(c) would cost more than the helper it was meant to judge"; option (a), the hand-run quits at visit 3. The helper (SE-6b, SE-6c, SE-6d) was 9.5 h (`docs/implementation/platform_refactor/plans/PLAN-E.md`, "What the rulings removed").
[^mr5]: `docs/implementation/platform_refactor/rulings/MORNING-RULINGS-E.md` §5: "five that are none of those by name".
[^se6a]: `build/platform/DEF/DUTIES-SE-6a.md` §2–§3 (the thread rows, their stop cells, flags H5, D1, D2) and `build/platform/DEF/NOTES-SE-6a.md` §3 (the threads the threading rule would list).
[^se1b]: `build/platform/E/NOTES-SE-1.md`, SE-1b §3 (the pins) and §4 (what can't be made busy headless).
[^t1]: `docs/implementation/platform_d_capture_seam.md` T1, option (e) rejected; ruled as recommended (the coordinator's call, 24 Nov 2026, PR-41, for the Product Owner's review).
[^t2]: `docs/implementation/platform_d_capture_seam.md` T2 (b) and (d), ruled as recommended (the Solution Architect and the Product Owner, 25 Nov 2026); the synthetic camera back-end is the coordinator's call. SD-2t in `docs/implementation/platform_refactor/plans/PLAN-D.md`.
[^gu10]: The Product Owner and the Solution Architect, 26 Nov 2026, `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Morning batch 1" (3): GU-10's limit is 5 s.
[^v8]: Lever 8 and Q9 (b) (the Product Owner; the Solution Architect, 25 Nov 2026): R-CON-5 names each long-lived thread's owner; "a long-lived thread that needs an event loop is owned by its device or media owner, which stops it before it is destroyed"; SE-11b's Accept "each listed in the model, each covered by a quit pin". `docs/implementation/platform_refactor/plans/PLAN-E.md` `[^v8]` and its "Ruled" paragraph.
[^h5]: `docs/implementation/platform_e_host_settings.md` H5, the globals table (its pattern `\bg\.[A-Za-z_]+` counts a same-named property's uses with the global's).
[^58b]: `governance/artefacts/information_model.md` §11.1, the row "QML through `app`; typed models and gadgets (R-QML-1, R-QML-2)", step 5.8b; `src/Gui/app/gui_app.h:32–34`.
