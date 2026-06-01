# Wrist Motion — Sensor Calibration Guide

This guide explains how to attach the motion sensors (IMUs) and run the short
calibration that a **Wrist Motion** session needs. Calibration takes under a
minute and only has to be done once per session, when you put the sensors on.

It teaches PinPoint exactly how each sensor is sitting on your arm, so the on-screen
3D model moves the way your arm actually moves — and it double-checks that every
sensor is strapped on correctly before you start.

---

## What you need

| Sensor | Where it goes | Required? | Colour in the app |
|---|---|---|---|
| **Forearm** | Just behind the wrist, on the forearm | **Yes** | 🔴 Red |
| **Hand** | Back of the hand | **Yes** | 🟡 Yellow |
| **Upper arm** | Outside of the upper arm | Optional (adds arm rotation) | 🟢 Green |

A **face-on camera is optional** for a Wrist Motion session and is **not** required
for calibration — the calibration uses the sensors alone.

> **Which arm?** PinPoint calibrates your **lead arm** — for a right-handed golfer
> that's the **left** arm (set your handedness on your athlete profile). Put the
> sensors on that arm.

---

## How to mount the sensors

Each sensor straps on **like a watch**, with a consistent orientation. With your arm
hanging by your side:

- The **face of the sensor points away from your thigh** (outward, to the side).
- The **USB/charging connector points forward** (the way you're facing).

Strap each sensor firmly so it can't rotate or slide on the limb during the swing.
The same orientation applies to all three sensors (forearm, hand, upper arm).

> A coloured marker on each segment of the on-screen model shows where its sensor
> should sit (🔴 forearm, 🟡 hand, 🟢 upper arm) — a handy reference if you're unsure.

---

## Running the calibration

Start a **Wrist Motion** session from the Home screen and follow the wizard. The
button in the **bottom-right corner is always the one that moves you forward.**

### 1 · Motion Sensors

- Each detected sensor appears as a chip with an **Enable** toggle. Turn off any
  sensor you're not using this session (e.g. if you're going without the upper arm).
  Disabled sensors are greyed out and won't be connected.
- Press the bottom-right button — it reads **Connect** — to pair the enabled sensors.
  It shows **Connecting…** while it works.
- Once every enabled sensor is connected, the button changes to **Continue →**.
  Press it to move on. (If you change a toggle, it switches back to **Connect**
  until everything's connected again.)

### 2 · Calibrate

An avatar demonstrates the two poses first. Then you hold each pose in turn while
PinPoint captures it — **a progress bar fills while you hold still, and resets if
you move**, so just relax and keep steady.

1. **Arm down** — let your lead arm hang relaxed at your side, palm toward your
   thigh. Hold still until it captures.
2. **Arm out to the side** — raise your arm out to the side toward shoulder height
   (keep it straight). Hold still until it captures.

That's it — the step shows **Complete** and you continue.

### 3 · Confirm Tracking

Move your arm around slowly and check the 3D model follows you: out to the side,
forward, and a forearm twist should all track. If anything looks wrong, use the
**Recalibrate** link to run the two poses again. When you're happy, continue to
**Ready** and start your session.

---

## If a sensor is mounted incorrectly

The calibration is also a safety check. If a sensor is strapped on the wrong way —
upside-down, or rotated around the limb — the Calibrate step will **fail** and tell
you **which** sensor is wrong (e.g. *"Sensor mounted incorrectly (hand) — re-seat per
the strap guide and tap Recalibrate."*).

When this happens:

1. Re-seat the named sensor following **[How to mount the sensors](#how-to-mount-the-sensors)**
   (face away from the thigh, USB forward, strapped firmly).
2. Tap **Recalibrate** and hold the two poses again.

This check runs on every connected sensor, so a problem with any one of them is
caught before you start.

---

## Tips

- **Hold still for the capture.** The bar fills only while you're steady; small
  movements just reset it — there's no penalty, take your time.
- **Strap firmly.** If a sensor shifts on the limb after calibration, the model will
  drift — re-seat and recalibrate.
- **Recalibrate any time** you take the sensors off and on again, or if tracking
  starts to look off.
- The calibration is **per-session** and isn't saved — every fresh start begins
  clean, so you always calibrate against how the sensors are actually sitting today.
