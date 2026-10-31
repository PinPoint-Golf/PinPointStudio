# Trunk IMUs — the worn session (H1b, H3–H5)

**For:** Mark. **Time:** about 20 minutes, plus 6 on GOLFSIMPC.
**Design:** `docs/design/trunk_imu_sequence_grf_design.md` §16 step 2.
**Comes after:** the desk session (`hardware_desk_session.md`, run 26 Oct: 200 Hz holds) and the calibration routine (design §16.2, W2), built on the desk results.
**The wG3 is worn too** (design §3.7, D28; week plan §16.1 W3). The trunk sensors and the wG3 together give the pelvis, chest and arm curves of the kinematic sequence from sensors, and grade all three camera routes on the same swings.
**Before you start:** the code is committed and pushed, and GOLFSIMPC is built from it (`tools\studio\studio-build.cmd both`). The studio passes run from the studio's own checkout.

Everything runs from one script:

```
build/run-me/trunk-hw-worn.sh <step>
```

## What you need

- The 2 trunk Witmotions, charged, with the rate the desk session settled on.
- The wG3 on the lead wrist, calibrated with its own routine (the trunk sensors on and recording while you do it).
- A firm belt with a pouch, and a snug GPS-style vest with a back pocket.
- The alignment stick, both cameras, and the LM.

## Where the sensors go (design §4.1)

| | Hip sensor | Chest sensor |
|---|---|---|
| Where | Middle of the lower back, on the flat of the sacrum, **below** the waistband | Vest back pocket, between the shoulder blades |
| Which way up | USB end down, label facing out (its printing reads the right way up) | USB end down, label facing out (its printing reads the right way up) |

Pad each sensor so it can't rattle, and close the pocket.

## The steps

| Step | You do | Time | Command | It decides |
|---|---|---|---|---|
| **H1b** | On GOLFSIMPC: run `tools\studio\trunk-hw-record.cmd`, which starts the installed app with the recorder on. Repeat the desk H1a and H1b: 3 Witmotions for 2 min, then 2 + the wG3 for 2 min. | 6 min | `trunk-hw-worn.sh rate-studio` | Whether 200 Hz holds over the studio's Bluetooth too. |
| **H3** | Belt and vest on, cameras live. Start a session with the hip and chest sensors assigned. **Calibrate** — see "Calibration" below. Then **3 hop shots**, wG3 on: stand where you address the ball, do one small hop straight up, land, stand still, and **trigger the shot by hand within one second of landing** (not before you land, and not after two seconds). The wG3 is read back from its own memory for 2 s either side of the trigger, and records fast only while the wrist moves, so the landing jolt has to sit in that stretch for the trunk-to-wrist clock check to be read. Leave about 5 s between hop shots, so one pull does not land on the next. | 5 min | — | The delay between the sensors' clock and the cameras' clock, and whether it repeats (D14). It passes if each hop fits within 5 ms and the two sensors agree within 5 ms. With the wG3 on, the hop also gives the trunk-to-wrist offset, which decides whether the chest-to-arm order can be judged. |
| **H4** | In the same session, wG3 on: **at least 10 full 7-iron swings**, LM on. | 12 min | — | Whether your hips really peak after impact (the cameras say about +20 ms), or whether that is camera lag. Whether the chest's bridged peak and the camera arm's peak (+43 ms late against the wG3 on 18 Aug) hold up against the sensors, and the sequence order, all on the same swings. |
| **H5** | Nothing to capture; it is computed from H4. | — | `trunk-hw-worn.sh studio <session> [stand-tall swing]`, then `trunk-hw-worn.sh analyse <session>` | Whether the live orientation keeps the tilt at impact (under 1°), or the trunk needs re-processing after the swing (§7.2). |

`<session>` is the session folder under `/mnt/swingdata/Mark-Liversedge`, for example `2026-10-24_Mark-Liversedge_Wrist_01`.

## Calibration

**The routine** (the route). Assign the hip and chest sensors on the Sensors page, then the setup flow runs it. With the wG3 on, its own calibration comes first and the trunk sensors record through it. Then: stand tall (the hold bar fills), turn away twice, turn through twice, each with its 10-second countdown. It calibrates both sensors live and names the sensor and the cause on a failure: tilted or upside down at stand tall; backwards, swapped, too little turn or a tilted turn in the turns. Backwards and swapped send you back to stand tall, because the sensor has to be re-seated. The Check page then shows the live figure and "hips · chest · X"; turn away and the figure must turn away. The whole routine is saved with the session as `trunk_ceremony.json`. Then run `trunk-hw-worn.sh studio <session>` with no stand-tall swing: the studio passes use the calibration saved with each shot.

**A sensor worn label-in, or on the front of the body** passes the routine (stand tall and an upright turn cannot see it, design §6.3; a front-worn sensor with its label out reads exactly like a back-worn one facing in). Every swing checks its own address: a backward lean there puts a ⚠ on that sensor's turn readings, saying it is probably on the front or worn label-in. If you see it, wear the sensor on your back (hip sensor in the middle of the lower back, chest sensor in the vest's back pocket), label out, and recalibrate.

**Fallback, if the routine will not pass.** Make **shot 1 a stand-tall shot**: toes to the stick, arms relaxed, hold still for 3 s, then trigger. Pass it to the studio step: `trunk-hw-worn.sh studio <session> swing_0001`. The studio applies the stand-tall solve to every shot. On this route nothing checks the sensors while you stand there; the analysis still flags a label-in sensor per swing.

## What happens to the shots

`trunk-hw-worn.sh studio` runs six passes on GOLFSIMPC over every shot in the session. They read the library and write only to `scratch\trunk-hw\<session>\`:
1. **dense:** every video frame posed. The hop needs it, because live pose is only every ~27 ms.
2. **bound:** the trunk sensors and the wG3 used, with the trunk calibrated by the routine (or from the stand-tall shot on the fallback route).
3. **unbound:** the trunk sensors ignored (`--ignore-role pelvis,thorax`), as the camera hips and chest control.
4. **arm unbound:** the wG3 ignored (`--ignore-role leadForearm,leadHand`), as the camera-arm control.
5. **fullswing:** the bound pass with the sequence curves left whole, address to finish. The other passes draw them only from the top to P8, and the signed-chart check needs the turn into the downswing, which for the hips comes before the top.
6. **refuse:** live orientation compared against orientation re-computed from the raw sensor data, with the tilt error at address, top, impact and finish.

`trunk-hw-worn.sh analyse <session>` prints the verdicts for the hops (`hop`), the hip peak, sensors against cameras (`hips`), the sequence (`ks`), and the tilt (`tilt`).
- **The hop** reads all four units: the two trunk sensors, and the wG3's palm and lower arm from each shot's swing.json (the raw recorder takes Witmotions only). Each unit gets its offset to the video and its residual, and the hop verdict adds the trunk-to-wrist (Witmotion ↔ wG3) offset per hop and its spread across hops, which passes within 5 ms. A wG3 landing sampled coarser than every 12.5 ms (the wG3's live rate, outside its high-rate window) is shown with a ⚠ and left out of that verdict.
- **`ks`** compares, per swing, each sensor's peak time with the camera's: hips and chest from the unbound pass, arm from the arm-unbound pass, with the club beside them and the sequence order in each pass. An unplaced camera chest is read from its bridged or indicated peak, and says which. The summary gives each segment's median difference with a 95 % confidence interval, and whether the camera's own ± covers the sensor. It also checks, on the fullswing pass, that the hips, chest and arm all turn into the downswing the same way round and within 80 ms of each other near the top. A curve upside down against the others would read as a sequence fault. It draws one sheet per swing, one panel per pass, under `verdicts/ks/sheets/`.

## Known limits

- **The hop offset includes Bluetooth queueing,** which changes when a sensor reconnects. If one drops during H3–H4, hop again.
- **The hop timing needs every frame posed.** The tool reports how good each fit is. If the residual is over 5 ms, add more hops.
