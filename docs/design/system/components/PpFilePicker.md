# PpFilePicker

THE in-app file picker: a modal sheet in the app's card style for choosing one file — the app opens no native dialogs.

Source: `src/Gui/components/PpFilePicker.qml`.

## When to use
Choosing a file from disk (the studio's logo, the coach's photo). Set `title`, `nameFilters` and optionally `startFolder`; listen for `chosen(path)`. Press a folder to open it; press a file to select it (a second press, or Choose, picks it).

## Props
| name | type | default | meaning |
|---|---|---|---|
| title | string | "Choose a file" | Micro caps header. |
| nameFilters | var | ["*.png","*.jpg","*.jpeg","*.svg"] | Files shown (folders always). |
| hint | string | "" | One line under the list (micro, `colorText3`). |
| previewImages | bool | true | Show the selected file as an image on the right. |
| startFolder | string | "" | Opening folder; else last visited, Pictures, Desktop, Home. |
| selectedPath | string | "" | Selected file. |
| currentFolder | string (ro) | | |
| signals | | | `chosen(path)`, `cancelled()` |
| functions | | | `goTo(path)`, `up()`, `select(path)`, `choose()` |

## Tokens / layout
Up to 760 × 540, 18 px padding, PpPopoverCard (card shell, `colorBorderStrong` hairline, rule in `colorText3`); modal dim black at 0.25 light / 0.45 dark. Root links (PpLink, 18 px apart; current one demi-bold ink) and "↑ Up one level"; folder path in micro data `colorText3`. List (3/5 width): `colorSurface`, `Theme.radius`, `colorBorderMid`; rows 30 px, 10 px in, ▸ for folders, name 12 px, size micro data; selected `colorAccentLight` with accent name, hover `colorBg2`. Preview (2/5): `colorBg2`, `Theme.radius`. Foot: Cancel (outline) and Choose (primary, disabled until a file is selected).
