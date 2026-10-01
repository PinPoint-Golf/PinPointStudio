# shaft_lie_card.qml — run it
Does the "Club & speed" summary squeeze shaft lie onto shaft lean's card (MetricCardSpec::mergeInto),
and does shaft lie then draw no card of its own? `LIEPROBE` is the grep handle.
```sh
QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
  build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
  --probe-qml "$PWD/tools/probes/shaft_lie_card.qml" \
  --probe-swing <swing dir whose document carries shaftLie> 2>&1 | grep LIEPROBE | awk '!seen[$0]++'
```
~15 s (5 steps × 2500 ms), then `Qt.quit()`s; a watchdog quits at 40 s. ⚠ THE PROBE PATH MUST BE
ABSOLUTE (ks_overlay_chart.md says why). The swing's document must already carry `shaftLie` — a
library swing analysed before 2026-10-01 does not; re-analyse it in the app, or on a COPY with
`swinglab_run <copy> --write-back` (never on the corpus swing itself).

Reads one line per card, `card '<name>' key=<key> tiles=N: <label>=<value> | …`, and a `VERDICT`:
PASS needs one shaft lean card carrying `LIE @ ADDRESS`, `LIE @ IMPACT` and `Δ LIE`, and no card
keyed `shaftLie`. First run 2026-10-01 on 07-04 swing 3: `@ IMPACT=0 | Δ P1→P7=0 | LIE @ ADDRESS=+56
| LIE @ IMPACT=+59 | Δ LIE=+4`, four cards for five series. The lean's 0 is the σ 9.5 display step
coarsening a 5.4° reading, the card as it was — not this probe's subject.
