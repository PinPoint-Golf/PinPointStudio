# PpConnectingFrame

A travelling-light "connecting" frame overlaid on a button: a dim track around the rounded border with one bright comet of stroke chasing round it while a connection is in flight.

Hand-written from `src/Gui/components/PpConnectingFrame.qml` (static rendition — the comet is drawn at one moment).

Use it on a Connect button while `CameraInstance.isConnecting`, `ImuInstance.busy` or a manager's `anyConnecting` is true. When `running` clears (succeeded, failed or timed out) the frame fades out (`durationNormal`, 220 ms) and the button is itself again. Purely decorative: it captures no input.

The consumer overlays it with `anchors.fill: parent` on the host button, drives `running`, and matches `radius` to the button.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `running` | bool | `false` | the single input: on/off |
| `radius` | real | `Theme.radius` | match the host button |
| `lineWidth` | real | `max(2, sp(2))` = 2 px | stroke; inset by half so it sits inside the host |
| `color` | color | `colorAccent` | comet and track; pass a light hue on a filled button where the accent wouldn't read |

## Drawing

- Track: the whole outline at `color` 22 % alpha.
- Comet: one dash of 18 % of the perimeter (≥ 3 stroke units), round caps, followed by a gap longer than the perimeter so exactly one comet is ever visible; linear, one lap in `max(900 ms, perimeter-in-stroke-units × 14 ms)`.
- Reduce motion: the comet stops and a steady solid outline in `color` marks the in-flight state.
