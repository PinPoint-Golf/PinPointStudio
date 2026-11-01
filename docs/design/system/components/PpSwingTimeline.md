# PpSwingTimeline

Six stops through the swing, Address to Finish; where something first shows is marked, and the track runs on from there in the tone.

Source: `src/Gui/components/PpSwingTimeline.qml`.

Use it on a fault or coaching card to say *when* in the swing a thing begins. `stopX(i)` and `trackY` let a caller set words on the track.

The consumer supplies `start` and, if not the default, `tone`; and a width (the stops spread across it).

## Props

| name | type | default | meaning |
|---|---|---|---|
| `start` | int | 0 | 0 Address · 1 Back · 2 Top · 3 Down · 4 Impact · 5 Finish |
| `tone` | color | `gradientCool` | the marked run and stops |
| `stops` | var | read-only | the six labels via `Theme.caps()` |
| `stopX(i)` | function | | x of stop i |
| `trackY` | int | 7 px | the track's centre line |

## Geometry

- First label's left edge on the item's left, last label's right edge on its right; stops equally spaced between.
- Height = 7 + 10 + 10 (micro) + 4 = 31 px.
- Base track: 1 px `colorBorderStrong`, stop 0 → 5.
- Toned run: 2 px, round, `tone` at 70 %, from `start` → 5.
- Start stop: 16 px halo in `tone` at 22 %, then a 9 px dot in `tone`.
- After the start: 5 px dots in `tone`. Before: 5 px `colorSurface` dots with a 1 px `colorBorderStrong` ring.
- Labels: PpMicro (data face 10 px) at `trackingData`, 10 px below the track centre; the start's in `colorText`, the rest `colorText3`.
