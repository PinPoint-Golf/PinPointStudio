# PpTextField

The app's one-line text field: a surface box, radius, a 1 px border (strong at rest, warn on error, accent on focus), body type.

Source: `src/Gui/components/PpTextField.qml`.

## When to use
Every form and settings field. It emits `editingFinished` when it loses focus (`commitOnBlur`), so a settings field commits when the user navigates away. Searches use PpSearchField.

## Props
| name | type | default | meaning |
|---|---|---|---|
| hasError | bool | false | `colorWarn` border. |
| size | string | "regular" | regular 34 px, 10 px in, 13 px at body weight · compact 24 px, 8 px in, 12 px at 400. |
| monospace | bool | false | Data face (crop numbers). |
| animateFocus | bool | false | Ease the border colour over `Theme.durationFast`. |
| commitOnBlur | bool | true | Emit editingFinished on focus loss. |
| editMenu | bool | true | Right-click Cut/Copy/Paste menu. |
| boxed | bool | true | false draws no box (PpSearchField line). |
| padHit | bool | true | false: only the text band takes a press. |
| (TextField) | | | `text`, `placeholderText`, `editingFinished`… |

## Tokens
`colorSurface` fill, radius `Theme.radius`, border `colorBorderStrong` / `colorWarn` / `colorAccent`; ink `colorText`, placeholder `colorText3`.
