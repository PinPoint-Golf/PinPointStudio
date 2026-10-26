# Trunk IMUs — the desk session (H0–H2)

**For:** Mark. **Time:** about 15 minutes, at a desk. Nothing is worn, and no cameras are needed.
**Design:** `docs/design/trunk_imu_sequence_grf_design.md` §16 step 2 (*"Hardware measurements before the estimator … Each decides whether a planned piece is needed at all"*).
**Then:** the calibration ceremony is built (§16 step 3) using these numbers, and after that comes the worn session (`hardware_worn_session.md`).

It runs on the Mac Debug app as it is now. No commit, push or studio is needed. Everything runs from one script:

```
build/run-me/trunk-hw-desk.sh <step>
```

Each recording step opens the app with the raw recorder on. The recorder writes one file per connected Witmotion, with every sample's arrival time and its raw accelerometer and gyro values. Quit the app to close the files. Recordings land in `/mnt/swingdata/scratch/trunk-hw/desk/`.

## What you need

- 3 Witmotions (WT9011DCL), charged.
- The wG3.

## The steps

| Step | You do | Time | Command | It decides |
|---|---|---|---|---|
| **H0** | Read the model printed on each unit. In Settings ▸ IMUs, confirm the gyro range is ±2000 °/s and the accelerometer ±16 g, and set **200 Hz** on all three. Note any you can't set. | 2 min | — | Whether the sensors are what the frame maths assumes: +Y runs along the long axis away from the USB end (corrected 26 Oct 2026; the original wording said the USB end is +Y) and the label face is +Z (design R6). |
| **H1a** | Connect **all 3 Witmotions**, place them anywhere, and leave them streaming for **2 min**. Then quit the app. | 3 min | `trunk-hw-desk.sh h1-three` | Whether Bluetooth keeps up at 200 samples a second with three sensors. |
| **H1b** | Connect **2 Witmotions and the wG3**, and leave them streaming for **2 min**. Then quit. | 3 min | `trunk-hw-desk.sh h1-wg3` | The same, with the wG3 sharing the radio (the arm-and-trunk session, D1). |
| **H2** | Connect **2 Witmotions** and put them **flat on the desk, untouched**: no typing on that desk, no knocks. Leave them for **5 min**. Then quit. | 6 min | `trunk-hw-desk.sh h2-still` | How far each gyro drifts when still, and whether the accelerometer reads 1 g. |

Then run `trunk-hw-desk.sh verdicts`. It analyses the newest recording of each step and prints the verdict lines.

## What the verdicts mean, and what they change

- **Rate (H1a, H1b).** "200 Hz sustained" needs a mean of at least 194 samples a second, with under 1 % of samples missing.
  - If it holds: the kinematic sequence's timing uses 200 Hz, about ±1 ms per peak.
  - If not: it stays at 100 Hz, about ±2–3 ms, and the timing uncertainty says so.
  - Either way, the ceremony's "enough samples" check is set from the rate you actually get.
- **Bias (H2).** If each gyro axis reads under 0.5 °/s at rest, a swing (about 1.5 s) drifts well under 1°, and no bias correction is needed. The tool prints the drift in degrees over 1.5 s. The ceremony's "hold still" threshold and its 1 g check are then set from these real numbers instead of the design's guesses.

## Notes

- The recorder takes Witmotions only. The wG3 in H1b is there to share the radio, not to be measured.
- If a sensor drops out and reconnects, the recorder starts a new file. The verdict reads each file on its own, so a dropout shows as a short file.
- Delete `/mnt/swingdata/scratch/trunk-hw/desk/` once the verdicts are written into the design doc's results.
