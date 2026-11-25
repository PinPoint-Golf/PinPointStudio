# PpNotificationHost

The window's notification centre: a bottom-up stack of PpNotificationCards, one per live notice in the `NotificationCenter` model.

Source: `src/Gui/components/PpNotificationHost.qml`.

It only draws what the model says is live; raising, coalescing, auto-hiding and actions live in C++ (`notifications`). Panel-local imperative confirmations (copy, an export result) stay with PpToast — both draw the same card.

The consumer places it (it is 560 px wide by default, at most 60 % of its parent's height) and provides the global `notifications` model with roles `notificationId, severity, kind, title, detail, count, glyph, actionLabel`.

## Props / model

| name | type | default | meaning |
|---|---|---|---|
| `model` | model | `notifications` | NotificationCenter |
| `sevInfo/sevWarn/sevError/sevProgress` | int | 0/1/2/3 | mirrored severity enum |
| `kindCondition` | int | 1 | a condition never auto-hides, so it gets a ✕ |
| `implicitWidth` | real | 560 px | |
| `spacing` | real | 10 px | between cards |

## Mapping

| severity | hairline + glyph colour | default glyph |
|---|---|---|
| error | `colorError` | ⚠ |
| warn | `colorWarn` | ⚠ |
| progress | `colorBorderStrong` / glyph `colorText2` | ⋯ |
| info | `colorBorderStrong` / glyph `colorText2` | ◎ |

- A repeated notice shows its count: "Title (×3)" — the count is the diagnostically valuable part.
- `actionLabel` is the fix-this affordance, in-app navigation only.
- Index 0 (the first raised) sits at the bottom; later news piles above it. Not interactive (no scrolling).
