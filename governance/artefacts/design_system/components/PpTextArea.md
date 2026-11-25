# PpTextArea

A multi-line text field in PpTextField's look, with word wrap.

Source: `src/Gui/components/PpTextArea.qml`.

## When to use
Prose settings (Settings → Studio booking sentence, practice steps, email message) and the report sheet's note. Plain: read/write `text`. Committing (`commitOnBlur: true`): shows `value`, emits `commit(text)` on focus loss, then shows `value` again so a normalised value reads as stored.

## Props
| name | type | default | meaning |
|---|---|---|---|
| hasError | bool | false | `colorWarn` border. |
| style | string | "regular" | "lab": 10 px all round (14 left), weight 400, `colorBorderMid` edge at rest. |
| commitOnBlur | bool | false | Commit on focus loss. |
| value | string | "" | What the setting holds. |
| signal commit(text) | | | |

## Tokens
`colorSurface`, `Theme.radius`, border `colorBorderStrong` / `colorWarn` / `colorAccent`; 13 px body at `Theme.fontBodyWeight`; padding 10 px sides, 8 px top/bottom; placeholder `colorText3`.
