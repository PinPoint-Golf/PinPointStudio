# PpTopoBackground

A theme-reactive, slowly drifting topographic-contour background: contour lines of a noise terrain, coloured along the brand sweep, with hover lift, tap ripples and an occasional light sweep.

Hand-written from `src/Gui/components/PpTopoBackground.qml` and `src/Shaders/topo.frag` (static rendition: one frame of the shader, ported to a canvas; no drift, hover, ripple or sparkle).

Use it behind a hero screen (Home) as a calm overlay under content. It paints behind everything, so it cannot receive pointer events itself: drive `hoverPoint` and `ripple()` from passive HoverHandler/TapHandler at the screen root.

The consumer binds the three colour stops to Theme tokens — `colorLow: gradientWarm`, `colorMid: gradientWarmLit`, `colorHigh: gradientCool` (and `accentColor: colorAccent`) — so a theme change repaints it, and gates `animated` on reduce-motion (`animated: !appSettings.reduceMotion`).

## Props

| name | type | default | meaning |
|---|---|---|---|
| `colorLow` / `colorMid` / `colorHigh` | color | `#E6AC54` / `#5FA8A0` / `#4A7BA6` | ramp stops by elevation — **bind to `gradientWarm` / `gradientWarmLit` / `gradientCool`** |
| `backgroundColor` | color | transparent | used only with `fillBackground` |
| `fillBackground` | bool | `false` | false = overlay only |
| `levels` | real | 9 | contour bands |
| `lineWidth` | real | 1.6 | line thickness, screen px, anti-aliased |
| `noiseScale` | real | 3.2 | terrain frequency |
| `intensity` | real | 0.2 | line opacity — kept low, calm |
| `speed` | real | 0.85 | drift rate (time = elapsed × speed × 0.07) |
| `animated` | bool | `true` | master switch; pauses when hidden or the app is backgrounded |
| `hoverLift` / `hoverRadius` | real | 0.28 / 0.18 | lift under the cursor |
| `rippleRadius` / `rippleWidth` / `rippleAmp` / `rippleDuration` | real | 1.05 / 0.06 / 0.36 / 1.6 s | tap ripple; up to 3 at once |
| `accentColor`, `rippleTint` | color, real | `colorHigh`, 1.0 | accent tint on the ripple ring |
| `sparkle`, `flashColor`, `flashWidth`, `flashStrength`, `flashDuration`, `flashInterval`, `flashJitter` | | true, warm near-white, 0.12, 1.0, 2.2 s, 7 s, 6 s | the occasional light sweep |
| `hoverPoint` | point | (−1, −1) | normalised cursor; (−1,−1) = none |
| `ripple(nx, ny)` | function | | spawn a ripple |

## Drawing

Height = 4-octave value-noise fbm of (aspect-corrected uv × noiseScale, time). Line where `fract(h × levels)` crosses 0, width measured in screen space (`fwidth`). Line colour = ramp(h): low→mid over 0–0.5, mid→high over 0.5–1; alpha = `intensity`.

## Don't

- Don't raise `intensity` to make it a feature — it is a ground, not a picture.
- Don't run it with reduce-motion on.
