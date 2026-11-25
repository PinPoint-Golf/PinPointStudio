# PpComboBox

The shared dropdown selector: a thin styling layer over the Basic ComboBox so every drop-down shares one look and one height.

Source: `src/Gui/components/PpComboBox.qml`.

## When to use
Picking one of a list too long for a segmented control (frame rates, placements). Drive it like a normal ComboBox — `model`, `currentIndex`, `onActivated` — and keep sync logic on the instance.

## Props
| name | type | default | meaning |
|---|---|---|---|
| displaySuffix | string | "" | Appended to every label (e.g. " Hz"). |
| itemEnabledFn | function(index)→bool | null | Disable individual rows (muted, inert). |
| displayFn | function(value)→string | null | Format labels for display only. |
| (ComboBox) | | | `model`, `currentIndex`, `activated`, `enabled`… |

## Tokens / states
Field 34 px, `colorSurface`, `Theme.radius`, 1 px border `colorBorderStrong` (focus `colorAccent`, disabled `colorBorderMid`); text 12 px `Theme.fontBodyWeight` `colorText` (disabled `colorText3`), 10 px in, 26 px right clearance; ⌄ 13 px `colorText3`, 10 px from the right. Popup 2 px below: PpPopoverCard "plainTight" (surface, `Theme.radius`, `colorBorderStrong`), max 280 px; rows 34 px, hovered/highlighted `colorAccentLight`, disabled rows `colorText3`.
