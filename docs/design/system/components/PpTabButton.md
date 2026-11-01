# PpTabButton

One tab of a stock TabBar drawn the app's way: body-face label, `colorText` when checked and `colorText3` otherwise.

Source: `src/Gui/components/PpTabButton.qml`.

## When to use
Inside a `TabBar`. Two looks, the two tab bars the app has: `segment` (IMU lab tabs) and `underline` (the lab pages' top bar). For title-size page tabs use PpTabs.

## Props
| name | type | default | meaning |
|---|---|---|---|
| text | string | | The label (TabButton). |
| style | string | "segment" | "segment" \| "underline" |
| checked | bool | | TabButton's checked state. |

## Tokens / states
Label `Theme.fontBody` 13 px, 400. **segment**: checked on `colorBg3`, radius `Theme.radius` − 1; unchecked transparent. **underline**: square; unchecked `colorBg2`, checked `colorBg` with a 2 px `colorAccent` underline.
