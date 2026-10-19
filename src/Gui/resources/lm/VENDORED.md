# Vendored launch monitor art

Copied by `tools/lm_art/vendor.sh` from the PinPoint Golf **Resources** repository (MIT, see
`LICENSE` here), and not edited by hand. Re-run the script to update; do not touch these files.

- **Resources commit:** `b3f7558fa01817384d9602d513655343820e2f2f` — Driver: GT2-style face pattern, centred, lines to the edge
- **Vendored:** 2026-10-19
- **Files:** 98 (12292 KB in all, of which PNG 12176 KB; budget 15 MB)
- **PNG long side:** heads 1024 px, ball 768 px (sources are 2048 px). Anchor JSON is
  copied verbatim; `LmClubArt::anchors()` rescales its pixel fields to the vendored image size
  at run time, so the JSON and the PNG can never disagree.

## Why 1024 px

The contract: the Impact face must reach ≥ 3 px/mm in physical pixels at Mark's full screen on
a 2× display, at the smallest size that does, inside 15 MB.

At Mark's full screen (2560 logical wide, fontScale 1.35, so em = round(11 × 1.35) = 15 px) a
This-shot club cell is at most 18 em = 270 px tall and 1.9 × 270 = 513 px wide (the mock's
`clubCellMaxEm`, `cellAspectMax`). The Impact face then spans at most
`(513 − 2 × 6.8 em − 11) / 1.12 ≈ 266` logical px across the head's body (`faceQW`), which is

| head | body heel–toe | logical px/mm | physical px/mm (2×) |
|---|---|---|---|
| driver  | 121.5 mm | 2.19 | 4.38 |
| 3 wood  | 102.9 mm | 2.59 | 5.17 |
| hybrid  |  89.1 mm | 2.99 | 5.97 |
| 7 iron  |  97.8 mm | 2.72 | 5.44 |
| wedge   |  99.4 mm | 2.68 | 5.35 |

So the screen asks for at most ~6 physical px/mm. The face view at 1024 px gives:

| head | image | px/mm at 2048 | px/mm vendored | body mm |
|---|---|---|---|---|
| driver | 2048×1712 → 1024×856 | 10.09 | 5.04 | 121.8 |
| fairway | 2048×1742 → 1024×871 | 11.49 | 5.75 | 102.7 |
| hybrid | 2048×1780 → 1024×890 | 12.94 | 6.47 | 88.1 |
| iron | 2048×1935 → 1024×968 | 12.70 | 6.35 | 97.8 |
| wedge | 2048×1989 → 1024×994 | 11.80 | 5.90 | 98.2 |

Every head is at or above what the screen draws (≥ 3 px/mm with room, and never upsampled on
the This-shot figure). 768 px would put the hybrid (4.6) and the irons (4.8) below their screen
density; 1024 is the smallest round size that clears all five. The Session impact map can draw
the face somewhat larger than the This-shot figure and is then upsampled by up to ~1.4×.
