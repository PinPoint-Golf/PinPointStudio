# Trunk IMUs — the worn session (H1b, H3–H5)

**For:** Mark. **Time:** about 20 minutes, plus 6 on GOLFSIMPC.
**Design:** `docs/design/trunk_imu_sequence_grf_design.md` §16 step 2.
**Comes after:** the desk session (`hardware_desk_session.md`) and the calibration ceremony (§16 step 3), which is built using the desk results.
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
| Which way up | USB end up, label facing out | USB end up, label facing out |

Pad each sensor so it can't rattle, and close the pocket.

## The steps

| Step | You do | Time | Command | It decides |
|---|---|---|---|---|
| **H1b** | On GOLFSIMPC: run `tools\studio\trunk-hw-record.cmd`, which starts the installed app with the recorder on. Repeat the desk H1a and H1b: 3 Witmotions for 2 min, then 2 + the wG3 for 2 min. | 6 min | `trunk-hw-worn.sh rate-studio` | Whether 200 Hz holds over the studio's Bluetooth too. |
| **H3** | Belt and vest on, cameras live. Start a session with the hip and chest sensors assigned. **Calibrate** — see "Calibration" below. Then **3 hop shots**, wG3 on: stand where you address the ball, do one small hop straight up, and trigger the shot by hand. | 5 min | — | The delay between the sensors' clock and the cameras' clock, and whether it repeats (D14). It passes if each hop fits within 5 ms and the two sensors agree within 5 ms. With the wG3 on, the hop also gives the trunk-to-wrist offset, which decides whether the chest-to-arm order can be judged. |
| **H4** | In the same session, wG3 on: **at least 10 full 7-iron swings**, LM on. | 12 min | — | Whether your hips really peak after impact (the cameras say about +20 ms), or whether that is camera lag. Whether the chest's bridged peak and the camera arm's peak (+43 ms late against the wG3 on 18 Aug) hold up against the sensors, and the sequence order, all on the same swings. |
| **H5** | Nothing to capture; it is computed from H4. | — | `trunk-hw-worn.sh studio <session> [stand-tall swing]`, then `trunk-hw-worn.sh analyse <session>` | Whether the live orientation keeps the tilt at impact (under 1°), or the trunk needs re-processing after the swing (§7.2). |

`<session>` is the session folder under `/mnt/swingdata/Mark-Liversedge`, for example `2026-10-24_Mark-Liversedge_Wrist_01`.

## Calibration

**With the ceremony built** (the intended route). Run it in session setup: stand tall, turn away, turn through. It calibrates both sensors live, catches a swapped, upside-down or loose sensor on the spot, and shows the live figure. Then run `trunk-hw-worn.sh studio <session>` with no stand-tall swing. The studio passes use the calibration saved with each shot.

**Before the ceremony exists** (fallback). Start the app with `trunk-hw-worn.sh record`, which offers the hip and chest roles with no ceremony. Make **shot 1 a stand-tall shot**: toes to the stick, arms relaxed, hold still for 3 s, then trigger. Then pass that shot to the studio step: `trunk-hw-worn.sh studio <session> swing_0001`. The studio applies the stand-tall solve to every shot in the session. On this route:
- the app's live trunk readings are uncalibrated and wrong, and Ready says "not calibrated";
- nothing checks the sensors while you stand there. Check the placement by eye. The analysis flags a swap (the "hips" turning more than the "chest") and a sensor worn label-in (a **negative** forward bend at address); either means re-seat it and repeat H3–H4.

## What happens to the shots

`trunk-hw-worn.sh studio` runs four passes on GOLFSIMPC over every shot in the session (a fifth, below, is to be added in week-plan stage W1). They read the library and write only to `scratch\trunk-hw\<session>\`:
1. **dense:** every video frame posed. The hop needs it, because live pose is only every ~27 ms.
2. **bound:** the trunk sensors used, calibrated from the stand-tall shot on the fallback route.
3. **unbound:** the trunk sensors ignored, as the camera-only control.
4. **refuse:** live orientation compared against orientation re-computed from the raw sensor data, with the tilt error at address, top, impact and finish.
5. **arm unbound** (to add, W1): the wG3 ignored (`--ignore-role leadForearm,leadHand`), as the camera-arm control.

`trunk-hw-worn.sh analyse <session>` prints the verdicts for the hops (`hop`), the hip peak, sensors against cameras (`hips`), and the tilt (`tilt`). To add in W1: the hop on the wG3 lanes, and `ks`, which compares pelvis, chest and arm peak times bound against unbound per swing and draws the full-swing sequence sheets.

## Known limits

- **The hop offset includes Bluetooth queueing,** which changes when a sensor reconnects. If one drops during H3–H4, hop again.
- **The hop timing needs every frame posed.** The tool reports how good each fit is. If the residual is over 5 ms, add more hops.
- **The setup labels** for the trunk roles still read "Pelvis (belt, sacrum)" and "Thorax (vest, upper back)" until the ceremony work renames them.
