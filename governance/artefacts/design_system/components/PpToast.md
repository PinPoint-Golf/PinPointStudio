# PpToast

A transient snackbar — PpNotificationCard in its `toast` style — that places itself, centred, and hides itself on a timer.

Source: `src/Gui/components/PpToast.qml`.

Use it for a panel-local, imperative confirmation: a soft delete with UNDO (reversible, so it replaces a modal confirm), a copy-to-clipboard result, an export result. Window-wide news that must persist or be counted goes through PpNotificationHost instead.

The consumer calls `show(msg)`, wires `undoClicked()` (restoring via the model invokable), and optionally sets `copyText`, `severity`, `placement` and `clearOf`.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `message` | string | `""` | the one line (set by `show(msg)`) |
| `showUndo` | bool | `true` | `false` hides the UNDO action for purely informational notices |
| `sticky` | bool | `false` | suppresses auto-hide; caller re-shows with sticky=false when its operation ends |
| `severity` | string | `"info"` | `info` (neutral, `colorBorderStrong` hairline, `colorText2` glyph) · `warn` · `error` — tints hairline and glyph only; the fill stays `colorBg3` |
| `placement` | string | `"bottom"` | `bottom`: `margin` above the parent's bottom (or above `clearOf` while shown) · `above`: outside the parent's top |
| `margin` | real | 24 px | distance from the edge |
| `clearOf` | Item | null | a bar the toast must not cover |
| inherited | | | `copyText`, glyph `🗑` by default |
| signal | | | `undoClicked()` |
| function | | | `show(msg)` |

## Behaviour

Auto-hides after `durationSlow × 21` = 7350 ms; a copy restarts the timer. The action reads `Theme.caps("Undo")`.

## Tokens

As PpNotificationCard; severity via `Theme.toneColor(severity, colorBorderStrong)` → `colorWarn` / `colorError`.
