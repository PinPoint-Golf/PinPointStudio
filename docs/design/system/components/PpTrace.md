# PpTrace

A declarative polyline trace drawn from normalised (0..1) points — the shot card's IMU-only fallback and the review panel's chart line.

Source: `src/Gui/components/PpTrace.qml`.

**Use** for a small, axis-less curve that fills its item. **Don't** use for a chart with axes, bands or several series (the Charts panel).

The consumer supplies the size and `points` (`[{x, y}]`, 0..1, y down), typically from the model's `tracePoints` role.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `points` | var | `[]` | normalised points, scaled to the item |
| `strokeColor` | color | `colorAccent` | the line |

Stroke `sp(1)` = 1 px, no fill, round joins and caps. A metric's trace passes `Theme.metricColor(key)` (e.g. `metricCornflower`).
