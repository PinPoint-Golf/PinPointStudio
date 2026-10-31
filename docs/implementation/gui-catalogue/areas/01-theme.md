# Theme

*Directory:* `src/Gui/theme/`  
*1 component · 939 lines of QML*

The single source of visual tokens (colours, fonts, spacing functions, radii, per-aesthetic data). Every other component reads it; nothing should hard-code a colour, font or size.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [Theme](#theme) | 939 | `QtObject` | 201 | 0 | singleton |

### Theme
`src/Gui/theme/Theme.qml` · 939 lines · root `QtObject` · **singleton**

> Single source of truth for all visual tokens.
> Set themeIndex to switch the entire UI. Never hardcode colours, fonts, or sizes in components.
> A theme is DATA: one entry in `_themes` below (design system §14). Each entry holds what its
> aesthetic shares across modes (fonts, radii, the rail) and a `light` and a `dark` block of
> colours. A token reads the entry with `_m.<name>` (colours every theme defines) or
> `_v(name, default)` (everything a theme MAY set — the default is what the six original
> aesthetics do, so a theme lists only where it differs). A new theme is one entry plus a line in
> `themeOrder`; nothing else in the app changes.
> The tables are plain JS values on a `var`, not nested QtObjects, which avoids the
> initialisation-order issues that cause "Unable to assign [undefined]" warnings.

- **Properties:** `int themeIndex`, `real fontScale`, `string density`, `bool reduceMotion`, `real overlayOpacity`, `readonly var themeOrder`, `readonly int themeCount`, `readonly string aesthetic`, `readonly bool dark`, `readonly string serifFamily`, `readonly color colorBg`, `readonly color colorBg2`, `readonly color colorBg3`, `readonly color colorSurface` … (+112 more)
- **Functions:** `cycleTheme()`, `sp(n)`, `gap(n)`, `themeInfo(i)`, `chartSeriesColor(i)`, `paletteDefault(name)`, `paletteKey(name)`, `paletteColor(name)`, `metricDefaultColorName(key)`, `metricColorName(key)` (+23)
- **Used by (201):** `AnnotationTool`, `AppearancePanel`, `ArchivingPanel`, `ArmCalibrationStatus`, `ArmVizView`, `AthleteClubsSection`, `AudioPage`, `BallPage`, `BodyVizView`, `CalibrateTrunkPage` (+191)
- **Exercised by tests/tools:** `SessionReportMock`, `SetupDriver`, `charts_delivery`, `charts_posture`, `components_gallery`, `diag_print_pdf` (+44)
