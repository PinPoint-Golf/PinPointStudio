# PpPopoverCard

A popup's background in the card family — the standard shell with a stronger hairline so it lifts off what it is drawn over; for a Popup's `background:`.

Source: `src/Gui/components/PpPopoverCard.qml`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| style | string | "" | "" menus, sheets, toolbar panels (rule in `tone`) · "plain" dialog box, `Theme.radiusLg`, no rule · "plainTight" dropdown list, `Theme.radius`, no rule |
| tone | color | `colorText3` | The rule (style ""). |
| borderColor | color | `colorBorderStrong` | The plain styles' one hairline — a confirm rings in its tone, an "add" picker in `colorAccent`. |

## Tokens & values
"" = PpCardShell floating (`colorSurface`, 1 px `colorBorderStrong`, `cardRule`). Plain = `colorSurface` + 1 px `borderColor`.
