# PpConfirm

The house confirmation prompt: modal, centred, dimmed, Esc cancels, display-font title, a right-aligned outlined primary followed by a neutral Cancel.

Source: `src/Gui/components/PpConfirm.qml`.

Use it for the few questions that must interrupt: closing during a live session, the Sessions screen's bulk actions, the Diagnostic Model's prompts. A soft delete that can be undone uses PpToast with UNDO instead — no modal.

The consumer supplies `title`, `body`, the two button words and the `tone`, and handles `confirmed()` / `cancelled()`.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `title` | string | `""` | display face (italic/weight per aesthetic), `min(20 px, fontSzDisplay)`, in the tone's colour |
| `body` | string | `""` | body2 (12 px) at body weight, `colorText2`, line height 1.5 |
| `confirmText` | string | "Continue" | the consequential answer |
| `cancelText` | string | "Cancel" | |
| `tone` | string | `"attention"` | `attention`: a call to act, nothing is lost · `error`: this destroys saved work |
| `dialogWidth` | real | 440 px | clamped to window − 48 px (the close prompt is 420) |
| `buttonHoverScale` | real | 1.0 | hover growth on both buttons |
| signals | | | `confirmed()`, `cancelled()` (Esc, Cancel or a programmatic close) |

## Geometry

Background PpPopoverCard `plain`: `colorSurface`, radius `Theme.radiusLg`, 1 px border in `colorAttention` / `colorError`. Padding 20 px; 12 px between title, body and a 4 px spacer; buttons 8 px apart, right-aligned.

- Confirm: PpButton `style: "tone"`, compact, 32 px tall, 24 px horizontal padding — **outlined, not filled**: transparent (hover: the tone's wash), border and label in the tone.
- Cancel: PpButton `subtle` — `colorBg2` (hover `colorBg3`), `colorBorderMid` border, `colorText2` label.

## Do / don't

- Two different events must not read alike: attention for interrupting something live, error for discarding saved work.
- Don't fill the primary — a filled button would read as the recommended answer.
- Don't close on a click outside: a click beside a prompt about saved work is not an answer. Esc = cancel.
