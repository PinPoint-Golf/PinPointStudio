# ks_overlay_chart.qml — run it

Does the "Kinematic sequence" preset draw the sequence ON the plot (a ring at every node's peak,
dimmed when unplaced; lead brackets in the combined view) and has the strip stopped drawing chips? `KSPROBE` is the grep handle.

```sh
QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
  build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
  --probe-qml tools/probes/ks_overlay_chart.qml \
  --probe-swing /mnt/swingdata/corpus/swings/2026-06-11_Mark-Liversedge_Wrist_01/swing_0002 \
  --probe-split 0 2>&1 | grep KSPROBE | awk '!seen[$0]++'
```

~15 s (5 steps × 2500 ms), then `Qt.quit()`s. Every line prints twice (stderr echo + app log);
the `awk` de-duplicates. `--probe-split 1` (default) is the split view, one facet per curve — each
facet then draws only its own segment's ring and no lead bracket (the strip's line carries the
leads); `--probe-split 0` is the combined view, where the bracket appears. See plumb_bob_chart.md for the environment notes (devBuild, no library setting
needed, no persisted writes: the probe drives its own chart with `sessionType: -1`).

Reads: `DRAWN sequencePeak:<segment> … ring@(x,y) σpx=… text='Lead arm −174 ms' placed=…`,
`DRAWN gap text`, `STRIP verdict: '<chain> · <verdict>'`, and `⛔ a chip is still drawn` if the
old strip ever comes back. Written 2026-09-18 with kinematic_sequence_design.md §8.
