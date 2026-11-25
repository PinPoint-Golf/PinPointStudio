# PpDeviceFilterBar

"RECORDED WITH" — the device filter over a list of sessions: one filter chip per device any saved session used, multi-select with AND semantics.

Source: `src/Gui/components/PpDeviceFilterBar.qml`.

Use it above a session list (the carousel's session drawer; the golfer's sessions on Home and the golfer page). Two chips on = sessions that had both. Each chip's count is what adding it would leave; a chip that would leave nothing is quietened but still tappable. Clear drops them all. The bar hides when no saved session says what recorded it.

The consumer supplies a `DeviceFilterModel` (`availableDevices [{id,label,count}]`, `deviceFilter`, `toggleDevice(id)`, `clearDeviceFilter()`, `shownSavedCount`, `savedCount`).

## Props

| name | type | default | meaning |
|---|---|---|---|
| `model` | DeviceFilterModel | null | |
| `showCount` | bool | `false` | "3 of 12" after Clear while the filter is on — for a list with no header to say it |
| `active` | bool | read-only | a filter is on |

## Parts (a Flow, 6 px spacing, 22 px rows)

- Head: PpMicro `Theme.caps("Recorded with")` — data face 10 px, `trackingMicro`, `colorText3`.
- Chips: PpChoiceChip `filter` — 22 px capsule, 16 px horizontal padding, label 11 px body face; rest transparent with `colorBorderMid`, hover → `colorBg2` and `colorAccent` border; selected `colorAccentLight` fill, `colorAccent` border and ink; unselected ink `colorText2`; count in the micro data face `colorText3`, 5 px after the label; dimmed 0.45 opacity.
- Clear: PpLink — body2 12 px in `colorAccent`.
- Count: data face 11 px, `colorText3`.
