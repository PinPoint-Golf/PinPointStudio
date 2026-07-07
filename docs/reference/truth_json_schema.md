# `truth.json` schema reference

> **Date:** 2026-07-07 · **Applies to:** the per-swing `truth.json` ground-truth sidecar (SwingLab / markup lab) · **Companion of:** [`swing_json_schema.md`](swing_json_schema.md)

`truth.json` is an **optional** sidecar next to a swing's `swing.json` (`<session>/swing_<NNNN>/truth.json`). It carries the *ground truth* for that swing — hand-marked (or instrumented-club-derived) shaft endpoints and phase landmarks — used to score the analyzer during validation. It is written and read independently of `swing.json`; the analyzer never writes it.

There are **two variants** with overlapping but distinct shapes:

| Variant | Producer | `meta.source` | Blocks | Purpose |
|---|---|---|---|---|
| **Markup / human** (Tier-3) | `tools/swinglab/swinglab/label.py`, the in-app markup lab (`src/Gui/markup/`) | *absent* | `shaft`, `events`, `ball`, `meta` | Hand-labelled ground truth. The primary validation truth `score.py` reads. |
| **Instrumented / auto** | `tools/shaftlab/{club_track_v3,stripe_annotate,stripe_fusion}.py --truth-out` | `"instrumented"` | `meta`, `shaft` | Auto-generated from a **taped** (retro-band) club — only ratio-verified frames. No `events`. |

The instrumented writer **refuses to overwrite** a truth.json whose `meta.source` is not `"instrumented"` — so hand markup is never clobbered by an auto run.

## Source of truth

| Concern | File |
|---|---|
| Markup read/write (C++, byte-compatible with `label.py`) | `src/Gui/markup/markup_truth.{h,cpp}` (`TruthDoc`, `toJson`, `writeTruth`, `readTruth`) |
| Markup writer (Python, the byte oracle) | `tools/swinglab/swinglab/label.py` |
| Validation reader / scorer | `tools/swinglab/score.py` (Tier-3) |
| Instrumented writers | `tools/shaftlab/{club_track_v3,stripe_annotate,stripe_fusion}.py` (`--truth-out`) |
| Enums (Phase) + P-system mapping | `src/Analysis/swing_analysis.h`, `markup_truth.h` |

## Conventions

- **`shaft[].t_us`** — window-relative µs (same domain as `swing.json` `streams[].frames.t_us` and the replay playhead), one entry per *marked/emitted* face-on frame (not every frame).
- **`grip` / `head`** — **pixels** at the source sensor resolution (`swing.json` `streams[].source.width/height`). In memory the markup tool holds them normalized 0..1; pixels are produced only at write time.
- **`theta`** — radians, `atan2(head_y − grip_y, head_x − grip_x)` (image convention).
- **`len`** — pixels, `hypot(head − grip)`.
- **`events.<name>_s`** — event time in **seconds**, relative to `events.t0_us`: `(frame_t_us − t0_us) / 1e6`. Only *marked* events appear.
- **`events.t0_us`** — the anchor (the face-on `frames.t_us[0]`), window-relative.
- **`ball`** — `[x, y]` **pixels** at source resolution (same convention as `grip`/`head`), the stationary ball centre for the whole swing. A single point (the ball doesn't move) — marked once; **absent when unplaced**. Ground truth for the ball-detector v2 position gate.

---

## Variant 1 — markup / human truth

The shape `label.py` writes and `score.py` reads (Tier-3). Example (`2026-07-03_…_Wrist_01/swing_0002`):

```json
{
  "shaft": [
    { "t_us": 2456126, "grip": [690.59, 660.92], "head": [638.05, 943.96],
      "theta": 1.75437, "len": 287.9 },
    …
    { "t_us": 4023847, "grip": [630.12, 242.54], "head": [548.70, 361.52],
      "theta": 2.17095, "len": 144.2 }
  ],
  "events": {
    "t0_us": 4632,
    "p1_s": 2.4515, "p2_s": 2.8000, "p3_s": 2.9609, "p4_s": 3.2151, "p5_s": 3.3493,
    "p6_s": 3.4362, "p7_s": 3.4832, "p8_s": 3.5770, "p9_s": 3.5970, "p10_s": 4.0192
  },
  "ball": [655.74, 941.4],
  "meta": {
    "club": "GAP WEDGE", "shaft": "steel", "lighting": "normal",
    "scope": "full", "tempo": "normal", "contact": "ball"
  }
}
```

### `shaft[]`

| Field | Type | Notes |
|---|---|---|
| `t_us` | int µs | Window-relative; the marked face-on frame's time. |
| `grip` | float[2] | Grip endpoint, **pixels** @ source W×H. |
| `head` | float[2] | Clubhead endpoint, pixels. |
| `theta` | float rad | `atan2(hy−gy, hx−gx)`, rounded to 5 dp. |
| `len` | float px | `hypot(head−grip)`, rounded to 1 dp. |

*(No `tier`/`conf` in this variant — every entry is a human/label truth point.)*

### `events` — the P-system ladder (P1–P10)

Keys `p1_s … p10_s` (seconds from `t0_us`); only marked positions are present. `score.py` maps each to the analyzer `Phase` enum where one exists:

| Event | Golf position | Analyzer `Phase` |
|---|---|---|
| `p1` | Address | Address (0) |
| `p2` | Shaft parallel (backswing) | — (club-angle truth only) |
| `p3` | Lead arm parallel (backswing) | MidBackswing (8) |
| `p4` | Top | Top (2) |
| `p5` | Lead arm parallel (downswing) | — |
| `p6` | Shaft parallel (downswing) | Delivery (9) |
| `p7` | Impact | Impact (5) |
| `p8` | Shaft parallel (follow-through) | — |
| `p9` | Lead arm parallel (follow-through) | FollowThrough (11) |
| `p10` | Finish | Finish (7) |

`t0_us` is the window-relative anchor (face-on `frames.t_us[0]`). **Legacy** truth.json used named keys — read via aliases: `address→p1`, `top→p4`, `impact→p7`, `finish→p10`.

### `meta` — capture conditions (additive)

Free-form strings for corpus filtering/segmentation; **unset fields are omitted** (preserving the legacy `shaft`/`events`-only byte-shape).

| Field | Values | Notes |
|---|---|---|
| `club` | e.g. `"7 IRON"`, `"DRIVER"` | Club label (`club_vocabulary.h`). |
| `shaft` | `graphite` \| `steel` | |
| `lighting` | `bright` \| `normal` \| `dark` | |
| `scope` | `full` \| `pitch` \| `chip` \| `putt` | The one field validation **consumes**: full-swing-only checks are skipped when `≠ "full"`. |
| `tempo` | `slow` \| `normal` \| `fast` | |
| `contact` | `ball` \| `air` \| `mishit` | |
| `clubLeavesFrame` | bool | Head exits frame mid-swing (explains low coverage). Omitted when false. |

### `ball` — stationary ball centre (additive)

`[x, y]` in **pixels** at source resolution (same convention as `grip`/`head`). The ball is stationary until struck, so this is a **single per-swing point** — marked once in the markup lab (one click) and applied to every frame. **Omitted when unplaced** (preserving the legacy byte-shape). The ground truth for the ball-detector v2 self-location / position gate ([`ball_detection_v2.md`](../design/ball_detection_v2.md) §9.1).

| Field | Type | Notes |
|---|---|---|
| `ball` | float[2] | Ball centre, **pixels** @ source W×H. Absent until marked. |

---

## Variant 2 — instrumented / auto truth

Written by `club_track_v3.py --truth-out` (or `stripe_annotate.py` / `stripe_fusion.py`) from a **taped** club: only ratio-verified `band`/`ray` frames are emitted (`pred`/`recon` are excluded — honesty by construction). No `events` block. Example (`2026-07-05_…_Wrist_02/swing_0002`):

```json
{
  "meta": { "club": "7 IRON", "source": "instrumented", "tool": "stripe_fusion", "n": 89 },
  "shaft": [
    { "t_us": 2661877, "theta": 2.046922, "grip": [665.1, 573.5], "tier": "ray", "conf": 0.55 },
    …
  ]
}
```

### `meta`

| Field | Type | Notes |
|---|---|---|
| `club` | str | Club record name. |
| `source` | `"instrumented"` | Marks it auto-generated — the overwrite guard keys on this. |
| `tool` | str | Generating tool: `club_track_v3` / `stripe_annotate` / `stripe_fusion`. |
| `n` | int | Emitted entry count. |

### `shaft[]`

| Field | Type | Notes |
|---|---|---|
| `t_us` | int µs | Window-relative (from `clipmeta` `t_us[frame]`). |
| `theta` | float rad | Reconciled shaft direction, 6 dp. |
| `grip` | float[2] | Grip anchor, pixels, 1 dp. |
| `tier` | `band` \| `ray` | Only direct measurements are written. |
| `conf` | float | Tier confidence (band `0.75+0.05·(n−4)` capped 0.9; ray `0.55`). |
| `head` | float[2] | **Only for `band`** — measured clubhead. |
| `len` | float px | **Only for `band`** — grip→head length. |

*(A `ray` entry omits `head`/`len`; a `band` entry includes them.)*

---

## Relationship to `swing.json`

`truth.json` is independent — it references the same face-on stream by geometry, not by embedding. Read it against `swing.json`:

- Face-on stream selected by `setup.perspective == 2` (else alias containing "Face", else first video) — see `markup_truth.readFaceOn`.
- `grip`/`head` pixels are relative to `streams[].source.width/height`.
- `t_us` / `events` are in the **same window-relative domain** as `swing.json` `streams[].frames.t_us` and `capture.impactUs`.
- Frame ↔ time: `frameIndexForUs` (binary search over the face-on frame times).

`score.py` compares the analyzer's `analysis.club` / `analysis.phases` (from `swing.json`) against these truth entries by nearest timestamp.
