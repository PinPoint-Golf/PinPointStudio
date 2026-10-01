# instant_cards.qml — run it
Which metrics a preset DRAWS and which it only CARDS (MetricCardSpec::drawsCurve, 2026-10-01): an
instant-only metric (shaft lean, shaft lie, x-factor at the top …) must have no legend chip and no
curve but a summary card; a series with no curve and a phase sample (attack angle, hand path loop,
every `lm.` number) must have a card. `ICPROBE` is the grep handle.
```sh
QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
  build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
  --probe-qml "$PWD/tools/probes/instant_cards.qml" \
  --probe-swing /mnt/swingdata/Mark-Liversedge/2026-07-04_Mark-Liversedge_Wrist_01/swing_0003 \
  --probe-presets "Club & speed;Club delivery;Arms;All" \
  [--probe-inspect shaftLie,clubheadSpeed] 2>&1 | grep ICPROBE | awk '!seen[$0]++'
```
~20 s (3 load steps + one per preset × 2500 ms), then `Qt.quit()`s; a watchdog quits at 50 s.
⚠ THE PROBE PATH MUST BE ABSOLUTE (ks_overlay_chart.md). The private chart has `sessionType: -1`,
so nothing is persisted. `--probe-inspect` prints, per key, the resolved domain, the P7 instant,
the nearest sample and its validity — the trail that found the snapped-domain refusal (the DTL
lie's P7 frame sits 2.9 ms before the face-on impact instant; measuredAt now judges the domain on
the nearest sample, chart_metrics.cpp).

Reads, per preset: `legend:` (the chips), `visible curves:` + `plots=N`, `card series:`, one
`card '<name>' key=… curve=<bool> tiles=N: …` line per card, and a `VERDICT`. First run on 07-04
swing 3: Club & speed legend = clubheadSpeed, handSpeed, lagAngle; cards add Peak lead and Shaft
lean (five tiles with the lie). Club delivery legend = (none), plots=0, six cards: Plane, Plane Δ,
Hand loop, Attack, Low pt, Past parallel.
