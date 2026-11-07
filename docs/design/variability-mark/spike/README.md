# Variability marks: rendering spike (Session 2)

**7 Nov 2026.** The spike asked one question before anything was built on top: does Qt Quick Shapes with `Shape.CurveRenderer` (spec §11) draw the marks well, keep them clipped inside a scrolled panel, and scroll smoothly in a 500-row table?

**Verdict: yes on macOS (Metal), so CurveRenderer stays as specced.** Windows D3D11 was not measured; see "Open".

## How it was run

- **Probe:** `src/Gui/tests/probes/variability_gallery.qml`. It is loaded with `--probe-qml` over the app and has three modes: `grid`, `clip` and `table`. `--probe-renderer curve|geometry` picks the renderer.
- **Windowed run:** `build/run-me/variability-spike.sh`, on 7 Nov, in a real window on Mark's Mac: cocoa, Metal, dpr 2.
- **First cut:** the first cut of `PpVariabilityMark` drew Dispersion and Overlay in both forms, with geometry in pixels from the §4 tables and strokes at constant pixel widths.
- **Offscreen runs can't answer this question.** Offscreen, Qt uses the software scene graph, so every Shape draws through QPainter whatever renderer it asks for (`rendererType` 3). Forcing RHI offscreen gives the right renderers but never presents a frame. The offscreen files below are labelled as such.

## Results

| Check | CurveRenderer (Metal, dpr 2) | GeometryRenderer + antialiasing (Metal, dpr 2) |
|---|---|---|
| **Clip:** a 360×240 clipped Flickable scrolled by (90, 120) and ringed by a solid band; pixels of a mark inside the band, more than 3 px from the clip edge | 0 | 0 |
| **Table:** 500 rows, `reuseItems`, 10 s scroll down and back; frame time p50 / p95 / p99 / max | 16.66 / 17.35 / 18.43 / 18.77 ms | 16.66 / 17.53 / 18.52 / 18.68 ms |
| Frames dropped (swap interval > 20 ms) | 0 of 601 | 0 of 601 |
| Delegates created / reused during the scroll | 0 / 954 | 0 / 954 |
| Delegate creation, a row with a text cell, two Cell marks and two words | 0.525 ms | 0.52 ms |

- **Clip.** The 11 Oct note in `src/Gui/typeset/PpMark.qml`, that CurveRenderer marks ignored an ancestor's clip once it scrolled, does not reproduce in a window. It does reproduce offscreen, where all Shapes ignore the clip, scrolled or not, whichever renderer they ask for. That makes it likely the 11 Oct observation came from an offscreen render.
- **Small sizes.** In the windowed grid (`windowed_grid_instrument_{light,dark}_metal_dpr2.png`) both renderers look the same. Every grade separates at 12 px, and the 1 px ring and baseline read in Instrument Dark.
- **dpr 1.** At dpr 1 (offscreen, software: `offscreen_zoom_*_dpr1.png`), Dispersion grades 1 and 2 are hard to tell apart at 12 px, the Overlay icon's grades 1–3 merge into a hatched band, and the dark-theme baseline is faint. At 16 px everything separates. The spec's recommended default is 20 px (16 in dense lists), so this matters only for marks drawn at 12 px on 100%-scaled screens.

## Notes for later items

- **Hosting tables need a real model.** With an integer model (`model: 500`), Qt's row context object answered unqualified names. `Math`, `qsTr` and `probe` came out undefined inside the mark in about 20 rows. A ListModel or C++ model is fine.
- **Tone fade on delegate reuse.** With `reuseItems`, a reused row's new grade fades from the previous row's tone over `durationSlow`, as §4.2 asks. In a scrolling table this shows as colour lag. This needs Mark's ruling.
- **No pixel snapping.** Snapping isn't asked for; at dpr 2 it isn't needed.

## Open

- **Windows D3D11 not measured** (Mark, 7 Nov). The cabin PC has only a shipping build, with the probe hook compiled out, and an ssh session has no GPU desktop. Check it when Session 5 puts the marks on real screens.
- **The clip counter** in `build/run-me/variability-spike/clip_check.py` read the band in the wrong coordinates at dpr 2, so it flagged even the plain-Rectangle control as leaking. The figures above were counted again at the right scale.
