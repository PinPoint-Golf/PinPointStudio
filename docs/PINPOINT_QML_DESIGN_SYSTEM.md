# PINPOINT — QML DESIGN SYSTEM INSTRUCTIONS

This document instructs Claude Code on how to implement the Pinpoint visual design
system in QML/Qt. Read this file before writing any QML. Every UI component must
conform to the token system, typography rules, and component patterns defined here.

Three aesthetic directions have been designed and prototyped (Instrument, Editorial,
Studio). The active aesthetic is configured via a single property on the Theme singleton.
All QML must use Theme tokens — never hardcode colours, font sizes, or spacing values.

---

## 1. FILE STRUCTURE

```
src/
  ui/
    theme/
      Theme.qml            ← singleton, exposes all tokens
      ThemeInstrument.qml  ← Instrument palette + typography
      ThemeEditorial.qml   ← Editorial palette + typography
      ThemeStudio.qml      ← Studio palette + typography
    components/
      PpRail.qml           ← left navigation rail
      PpHeader.qml         ← top header bar
      PpVideoPanel.qml     ← camera / replay panel
      PpMetricCard.qml     ← single metric display
      PpMetricRail.qml     ← right-side metrics column
      PpCarousel.qml       ← capture carousel
      PpCarouselThumb.qml  ← individual carousel tile
      PpStatusBar.qml      ← bottom status strip
      PpPill.qml           ← status pill (connection, REC, etc.)
      PpButton.qml         ← standard button
      PpDivider.qml        ← hairline rule
      PpTimeline.qml       ← P-position scrubber
    screens/
      ScreenEmpty.qml
      ScreenAthletePicker.qml
      ScreenSessionConfigure.qml
      ScreenReadiness.qml
      ScreenSwingAnalysis.qml
      ScreenWristAnalysis.qml
      ScreenGroundForces.qml
      ScreenAiCoach.qml
      ScreenSummary.qml
      ScreenAthleteDetail.qml
      ScreenSettings.qml
    MainWindow.qml
```

---

## 2. THEME SINGLETON

Declare `Theme.qml` as a QML singleton using `pragma Singleton`. It exposes the
active palette and typography scale. All other QML files import it and reference
`Theme.colorBg` etc. — never hardcoded values.

```qml
// Theme.qml
pragma Singleton
import QtQuick

QtObject {
    id: root

    // Active aesthetic: "instrument" | "editorial" | "studio"
    // Set from C++ via QQmlContext or from Settings at startup.
    property string aesthetic: "studio"

    // Dark mode toggle
    property bool dark: false

    // Delegate to the active sub-theme
    readonly property var _t: {
        if (aesthetic === "instrument") return dark ? InstrumentDark : InstrumentLight
        if (aesthetic === "editorial")  return dark ? EditorialDark  : EditorialLight
        return dark ? StudioDark : StudioLight
    }

    // ── Colour tokens (all components use these names) ──────────────────────
    readonly property color colorBg:           _t.bg
    readonly property color colorBg2:          _t.bg2
    readonly property color colorBg3:          _t.bg3
    readonly property color colorSurface:      _t.surface
    readonly property color colorBorder:       _t.border
    readonly property color colorBorderMid:    _t.borderMid
    readonly property color colorBorderStrong: _t.borderStrong
    readonly property color colorText:         _t.text
    readonly property color colorText2:        _t.text2
    readonly property color colorText3:        _t.text3
    readonly property color colorAccent:       _t.accent
    readonly property color colorAccentLight:  _t.accentLight
    readonly property color colorAccentMid:    _t.accentMid
    readonly property color colorGood:         _t.good
    readonly property color colorGoodLight:    _t.goodLight
    readonly property color colorWarn:         _t.warn
    readonly property color colorWarnLight:    _t.warnLight

    // ── Typography tokens ────────────────────────────────────────────────────
    // fontBody: primary UI font (menus, labels, body copy)
    // fontData: monospaced font for all numeric values, timestamps, status
    // fontDisplay: serif display font for headings/session titles
    //              (Instrument and Editorial only; Studio uses fontBody at larger size)
    readonly property string fontBody:    _t.fontBody
    readonly property string fontData:    _t.fontData
    readonly property string fontDisplay: _t.fontDisplay   // may equal fontBody in Studio

    // ── Spacing & geometry tokens ────────────────────────────────────────────
    readonly property int railWidth:     _t.railWidth      // px
    readonly property int sidenavWidth:  _t.sidenavWidth   // all secondary side panels (settings, athletes, etc.)
    readonly property int headerHeight:  40                 // fixed across all aesthetics
    readonly property int carouselHeight: _t.carouselHeight
    readonly property int statusBarHeight: _t.statusBarHeight
    readonly property int radius:        _t.radius          // standard corner radius
    readonly property int radiusLg:      _t.radiusLg        // card-level radius

    // ── Animation durations (ms) ─────────────────────────────────────────────
    readonly property int durationFast:   120
    readonly property int durationNormal: 220
    readonly property int durationSlow:   350

    // ── Border widths ────────────────────────────────────────────────────────
    // Qt does not support sub-pixel borders natively.
    // Use 1px borders at opacity 0.5–0.6 to simulate hairline.
    readonly property real borderWidth: 1
    readonly property real borderOpacityNormal: 0.5
    readonly property real borderOpacityStrong: 0.75
}
```

---

## 3. PALETTE DEFINITIONS

### 3.1 Instrument

```qml
// ThemeInstrument.qml — light values
QtObject {
    property color bg:           "#F5F2ED"
    property color bg2:          "#EDE9E2"
    property color bg3:          "#E4DFD6"
    property color surface:      "#FDFAF6"
    property color border:       Qt.rgba(80/255, 70/255, 55/255, 0.12)
    property color borderMid:    Qt.rgba(80/255, 70/255, 55/255, 0.17)
    property color borderStrong: Qt.rgba(80/255, 70/255, 55/255, 0.22)
    property color text:         "#1C1810"
    property color text2:        "#5C5448"
    property color text3:        "#9A9087"
    property color accent:       "#2B4A3F"
    property color accentLight:  Qt.rgba(43/255, 74/255, 63/255, 0.08)
    property color accentMid:    Qt.rgba(43/255, 74/255, 63/255, 0.18)
    property color good:         "#1E4D3A"
    property color goodLight:    Qt.rgba(30/255, 77/255, 58/255, 0.09)
    property color warn:         "#7A3B1E"
    property color warnLight:    Qt.rgba(122/255, 59/255, 30/255, 0.08)
    property string fontBody:    "DM Sans"
    property string fontData:    "DM Mono"
    property string fontDisplay: "DM Serif Display"
    property int railWidth:      56
    property int carouselHeight: 120
    property int statusBarHeight: 36
    property int radius:         6
    property int radiusLg:       10
}

// Dark variant — swap these values:
// bg "#161412"  bg2 "#1E1C19"  bg3 "#252220"  surface "#1A1816"
// border Qt.rgba(255/255,248/255,235/255,0.08)
// borderMid Qt.rgba(255/255,248/255,235/255,0.11)
// borderStrong Qt.rgba(255/255,248/255,235/255,0.14)
// text "#F0EBE1"  text2 "#A09880"  text3 "#665E52"
// accent "#7EBFAA"  accentLight Qt.rgba(126/255,191/255,170/255,0.09)
// accentMid Qt.rgba(126/255,191/255,170/255,0.16)
// good "#7EBFAA"  goodLight Qt.rgba(126/255,191/255,170/255,0.09)
// warn "#D4896A"  warnLight Qt.rgba(212/255,137/255,106/255,0.09)
```

### 3.2 Editorial

```qml
// Light:
// bg "#FAFAF8"  bg2 "#F2F1ED"  bg3 "#E8E7E2"  surface "#FFFFFF"
// border Qt.rgba(0,0,0,0.07)  borderMid Qt.rgba(0,0,0,0.10)  borderStrong Qt.rgba(0,0,0,0.13)
// text "#111110"  text2 "#4A4A47"  text3 "#9B9B97"
// accent "#1A3A5C"  accentLight Qt.rgba(26/255,58/255,92/255,0.06)
// accentMid Qt.rgba(26/255,58/255,92/255,0.14)
// good "#1A4A2E"  goodLight Qt.rgba(26/255,74/255,46/255,0.07)
// warn "#8B2500"  warnLight Qt.rgba(139/255,37/255,0,0.06)
// fontBody "Instrument Sans"  fontData "JetBrains Mono"  fontDisplay "Playfair Display"
// railWidth 58  radius 3  radiusLg 6

// Dark variant:
// bg "#141412"  bg2 "#1C1C1A"  bg3 "#242422"  surface "#181816"
// border Qt.rgba(255/255,255/255,245/255,0.07)
// text "#F0EFE8"  text2 "#9A9A92"  text3 "#5A5A55"
// accent "#A8C4E0"  accentLight Qt.rgba(168/255,196/255,224/255,0.07)
// accentMid Qt.rgba(168/255,196/255,224/255,0.14)
// good "#8ABFA0"  goodLight Qt.rgba(138/255,191/255,160/255,0.07)
// warn "#D4896A"  warnLight Qt.rgba(212/255,137/255,106/255,0.07)
```

### 3.3 Studio

```qml
// Light:
// bg "#F6F6F5"  bg2 "#EEEEED"  bg3 "#E4E4E3"  surface "#FAFAF9"
// border Qt.rgba(0,0,0,0.06)  borderMid Qt.rgba(0,0,0,0.10)  borderStrong Qt.rgba(0,0,0,0.15)
// text "#0A0A09"  text2 "#525251"  text3 "#ABABAA"
// accent "#0066FF"  accentLight Qt.rgba(0,102/255,255/255,0.05)
// accentMid Qt.rgba(0,102/255,255/255,0.12)
// good "#0A7A4A"  goodLight Qt.rgba(10/255,122/255,74/255,0.06)
// warn "#D94A00"  warnLight Qt.rgba(217/255,74/255,0,0.06)
// fontBody "Geist"  fontData "Geist Mono"  fontDisplay "Geist"  (no separate display font)
// railWidth 52  radius 5  radiusLg 8

// Dark variant:
// bg "#111110"  bg2 "#191918"  bg3 "#212120"  surface "#161615"
// border Qt.rgba(255/255,255/255,255/255,0.055)
// borderMid Qt.rgba(255/255,255/255,255/255,0.09)
// borderStrong Qt.rgba(255/255,255/255,255/255,0.13)
// text "#EDEDEC"  text2 "#878786"  text3 "#4A4A48"
// accent "#4D90FF"  accentLight Qt.rgba(77/255,144/255,255/255,0.07)
// accentMid Qt.rgba(77/255,144/255,255/255,0.15)
// good "#30C983"  goodLight Qt.rgba(48/255,201/255,131/255,0.07)
// warn "#FF6B35"  warnLight Qt.rgba(255/255,107/255,53/255,0.08)
```

---

## 4. FONT LOADING

Load fonts from the `assets/fonts/` directory using `FontLoader` in `main.qml`
before any UI is instantiated. Qt bundles fonts via the Qt resource system (`.qrc`).

```qml
// main.qml — load all fonts before creating the window
FontLoader { id: flDmSans;           source: "qrc:/fonts/DMSans-VariableFont.ttf" }
FontLoader { id: flDmMono;           source: "qrc:/fonts/DMMono-Regular.ttf" }
FontLoader { id: flDmMonoLight;      source: "qrc:/fonts/DMMono-Light.ttf" }
FontLoader { id: flDmSerif;          source: "qrc:/fonts/DMSerifDisplay-Regular.ttf" }
FontLoader { id: flDmSerifItalic;    source: "qrc:/fonts/DMSerifDisplay-Italic.ttf" }
FontLoader { id: flInstrumentSans;   source: "qrc:/fonts/InstrumentSans-VariableFont.ttf" }
FontLoader { id: flJetBrainsMono;    source: "qrc:/fonts/JetBrainsMono-Regular.ttf" }
FontLoader { id: flJetBrainsMonoLt;  source: "qrc:/fonts/JetBrainsMono-Light.ttf" }
FontLoader { id: flPlayfair;         source: "qrc:/fonts/PlayfairDisplay-VariableFont.ttf" }
FontLoader { id: flPlayfairItalic;   source: "qrc:/fonts/PlayfairDisplay-Italic-VariableFont.ttf" }
FontLoader { id: flGeist;            source: "qrc:/fonts/Geist-VariableFont.ttf" }
FontLoader { id: flGeistMono;        source: "qrc:/fonts/GeistMono-VariableFont.ttf" }
```

**Font weight mapping (QML `font.weight` values):**

| CSS weight | QML constant         | Value |
|------------|----------------------|-------|
| 200        | `Font.Thin`          | 100   |
| 300        | `Font.Light`         | 300   |
| 400        | `Font.Normal`        | 400   |
| 500        | `Font.Medium`        | 500   |
| 600        | `Font.DemiBold`      | 600   |

Use `Font.Light` and `Font.Normal` only. Never use `Font.DemiBold` or `Font.Bold`
in Pinpoint UI — it reads as too heavy against the ambient chrome.

---

## 5. TYPOGRAPHY SCALE

All text in Pinpoint uses one of these roles. Reference the role, not a pixel size,
so the aesthetic switch can rescale without touching components.

| Role | Aesthetic | Size (px) | Weight | Font |
|------|-----------|-----------|--------|------|
| `display` | Instrument | 26 | Normal | DM Serif Display italic |
| `display` | Editorial  | 34 | Normal | Playfair Display italic |
| `display` | Studio     | 22 | Light  | Geist |
| `heading` | all        | 16 | Normal | fontBody |
| `body`    | all        | 13 | Normal | fontBody |
| `body2`   | all        | 12 | Light  | fontBody |
| `label`   | all        | 11 | Light  | fontBody, uppercase, tracking +0.06em |
| `data`    | all        | 18–22 | Light | fontData |
| `dataSm`  | all        | 13 | Light  | fontData |
| `micro`   | all        | 10 | Normal | fontData, uppercase, tracking +0.07em |

**Implement as helper properties on Theme:**

```qml
// Add to Theme.qml

// Display — session titles, welcome heading
readonly property int fontSzDisplay: _t.fontSzDisplay   // 22–34 depending on aesthetic
readonly property bool fontDisplayItalic: _t.fontDisplayItalic  // true for Instrument + Editorial

// Data values — metric readouts, fps, timestamps
readonly property int fontSzData:    20    // large metric values
readonly property int fontSzDataSm:  13    // small data / status
readonly property int fontSzMicro:   10    // uppercase micro labels

// Body
readonly property int fontSzBody:    13
readonly property int fontSzBody2:   12
readonly property int fontSzLabel:   11

// Letter spacing — QML uses em fractions (divide CSS em by font size if needed)
// Use font.letterSpacing property in pixels: 11px * 0.06em = 0.66px ≈ 0.7
readonly property real trackingMicro:  0.8   // uppercase micro labels
readonly property real trackingLabel:  0.6   // uppercase section labels
readonly property real trackingData:   0.3   // data values
readonly property real trackingNormal: 0.0
```

**Text component usage pattern:**

```qml
// ✅ Correct — always use Theme tokens
Text {
    text: "98.4"
    font.family: Theme.fontData
    font.pixelSize: Theme.fontSzData
    font.weight: Font.Light
    color: Theme.colorText
    font.letterSpacing: Theme.trackingData
}

// ❌ Wrong — never hardcode
Text {
    text: "98.4"
    font.family: "Geist Mono"
    font.pixelSize: 20
    color: "#0A0A09"
}
```

---

## 6. BORDER RENDERING

Qt/QML does not render sub-pixel borders. The HTML prototypes use `0.5px` borders
throughout. In QML, simulate this with 1px `Rectangle` borders at reduced opacity,
or use thin `Rectangle` elements as separators.

```qml
// Hairline border on a container — use a Rectangle with border
Rectangle {
    color: Theme.colorSurface
    border.width: 1
    border.color: Qt.rgba(
        Theme.colorBorder.r,
        Theme.colorBorder.g,
        Theme.colorBorder.b,
        Theme.colorBorder.a * 0.6   // reduce to simulate 0.5px
    )
    radius: Theme.radius
}

// Hairline divider (horizontal rule)
Rectangle {
    width: parent.width
    height: 1
    color: Theme.colorBorder
    opacity: 0.6
}

// Hairline divider (vertical separator in header)
Rectangle {
    width: 1
    height: 16
    color: Theme.colorBorderStrong
    opacity: 0.7
}
```

---

## 7. CORE COMPONENT PATTERNS

### 7.1 Left navigation rail — `PpRail.qml`

```qml
Rectangle {
    id: rail
    width: Theme.railWidth
    color: {
        // Instrument uses bg2 for rail; Editorial and Studio use bg
        if (Theme.aesthetic === "instrument") return Theme.colorBg2
        return Theme.colorBg
    }

    // Right-edge hairline border
    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: Theme.colorBorderMid
        opacity: 0.6
    }

    Column {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
        topPadding: 14
        spacing: 4

        // Athlete avatar (initials circle)
        PpAthleteAvatar { }

        // Divider
        PpDivider { orientation: Qt.Horizontal; width: 24 }

        // Mode buttons
        Repeater {
            model: modeModel
            PpRailButton { }
        }

        Item { Layout.fillHeight: true }   // spacer

        PpDivider { orientation: Qt.Horizontal; width: 24 }

        PpRailButton { mode: "settings" }
    }
}
```

**Rail button active state — varies by aesthetic:**

```qml
// PpRailButton.qml
Rectangle {
    property bool isActive: false
    property string aesthetic: Theme.aesthetic

    color: isActive ? _activeBg : "transparent"
    radius: Theme.radius

    // Editorial: left-border accent, no background radius
    // Instrument + Studio: filled background rect with radius
    readonly property color _activeBg: {
        if (aesthetic === "editorial") return Theme.colorAccentLight
        if (aesthetic === "instrument") return Theme.colorSurface
        return Theme.colorSurface   // Studio
    }

    // Editorial uses a 2px left accent bar instead of a full background
    Rectangle {
        visible: Theme.aesthetic === "editorial" && isActive
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 2
        color: Theme.colorAccent
        radius: 0
    }

    // Studio + Instrument: hairline border when active
    border.width: isActive && aesthetic !== "editorial" ? 1 : 0
    border.color: Theme.colorBorderMid
}
```

### 7.2 Header bar — `PpHeader.qml`

```qml
Rectangle {
    height: Theme.headerHeight
    color: {
        if (Theme.aesthetic === "instrument") return Theme.colorSurface
        return Theme.colorBg
    }

    // Bottom hairline
    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 1
        color: Theme.colorBorderMid
        opacity: 0.6
    }

    Row {
        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
        spacing: 12

        // Editorial: "Pinpoint" italic serif wordmark
        // Studio: plain weight text
        // Instrument: plain weight text
        Text {
            text: "Pinpoint"
            font.family: Theme.aesthetic === "editorial" ? Theme.fontDisplay : Theme.fontBody
            font.italic: Theme.aesthetic === "editorial"
            font.pixelSize: Theme.aesthetic === "editorial" ? 16 : 13
            font.weight: Font.Normal
            color: Theme.colorText
        }

        PpHeaderSeparator { }

        Text {
            text: currentModeName    // e.g. "Swing analysis"
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzLabel
            font.weight: Font.Light
            color: Theme.colorText3
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.trackingLabel
        }
    }
}
```

### 7.3 Video panel — `PpVideoPanel.qml`

```qml
Rectangle {
    color: {
        // Video area background — dark regardless of light/dark mode
        // in production this is replaced by the camera texture
        return Theme.dark ? "#0C0C0B" : Theme.colorBg3
    }

    // Panel label bar (Instrument: overlaid text; Editorial/Studio: dedicated bar)
    PpPanelBar {
        visible: Theme.aesthetic !== "instrument"
        anchors { top: parent.top; left: parent.left; right: parent.right }
        labelText: panelLabel      // e.g. "Live · cam 1 · face-on"
        metaText: panelMeta        // e.g. "158 fps"
    }

    // Instrument: floating labels directly over video
    Text {
        visible: Theme.aesthetic === "instrument"
        anchors { top: parent.top; left: parent.left; topMargin: 10; leftMargin: 12 }
        text: panelLabel
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzMicro
        font.weight: Font.Normal
        color: Theme.colorText3
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Theme.trackingMicro
    }

    // Timeline strip at bottom
    PpTimeline {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
    }
}
```

### 7.4 Metric value display — `PpMetricCard.qml`

This is the component used in the key metrics rail and in the session summary grid.
The metric value uses `fontData`; the label uses `fontBody` (or `fontData` for Studio
and Instrument, `fontBody` light for Editorial).

```qml
Column {
    spacing: 2

    // Label — uppercase micro
    Text {
        text: metricLabel.toUpperCase()
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        font.weight: Font.Light
        color: Theme.colorText3
        font.letterSpacing: Theme.trackingMicro
    }

    // Value — the dominant element
    Text {
        text: metricValue
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzData
        font.weight: Font.Light
        color: Theme.colorText
        font.letterSpacing: -0.2   // slight tightening for large mono numerals
    }

    // Unit — micro, below value
    Text {
        visible: metricUnit.length > 0
        text: metricUnit
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzMicro
        font.weight: Font.Normal
        color: Theme.colorText3
        font.letterSpacing: Theme.trackingMicro
    }

    // Delta vs baseline
    Text {
        visible: metricDelta.length > 0
        text: metricDelta
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzDataSm
        font.weight: Font.Normal
        color: deltaPositive ? Theme.colorGood :
               deltaNegative ? Theme.colorWarn :
               Theme.colorText3
        font.letterSpacing: Theme.trackingData
    }
}
```

### 7.5 Metric value in Editorial — display serif override

In the Editorial aesthetic, large metric values use `fontDisplay` (Playfair Display)
rather than `fontData`. Apply this override at the `PpMetricCard` level:

```qml
Text {
    text: metricValue
    font.family: Theme.aesthetic === "editorial" ? Theme.fontDisplay : Theme.fontData
    font.italic: Theme.aesthetic === "editorial"
    font.pixelSize: Theme.fontSzData
    font.weight: Font.Normal
    color: Theme.colorText
}
```

### 7.6 Capture carousel — `PpCarousel.qml`

```qml
Rectangle {
    height: Theme.carouselHeight
    color: Theme.colorSurface

    // Top hairline
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 1
        color: Theme.colorBorderMid
        opacity: 0.6
    }

    Row {
        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
        spacing: 10

        Text {
            text: "Session"
            font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.weight: Font.Normal
            color: Theme.colorText3
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.trackingMicro
        }

        // Thumbnails
        ListView {
            orientation: ListView.Horizontal
            model: captureModel
            delegate: PpCarouselThumb { }
            spacing: 8
        }
    }
}
```

### 7.7 Carousel thumbnail — `PpCarouselThumb.qml`

```qml
Rectangle {
    width: 70
    height: Theme.carouselHeight - 24   // vertical padding
    radius: Theme.radius
    color: isSelected ? Theme.colorAccentLight : Theme.colorBg2

    border.width: 1
    border.color: isSelected ? Theme.colorAccent : Theme.colorBorder
    // Simulate 0.5px selected border: use full width but bump opacity
    // Simulate 0.5px unselected border: full width at 0.5 opacity in colorBorder

    // Capture number
    Text {
        anchors { top: parent.top; left: parent.left; topMargin: 6; leftMargin: 7 }
        text: "#" + captureIndex.toString().padStart(2, "0")
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzMicro - 1   // 9px
        font.weight: Font.Normal
        color: isSelected ? Theme.colorAccent : Theme.colorText3
        font.letterSpacing: Theme.trackingData
    }

    // Primary metric value
    Text {
        anchors { bottom: parent.bottom; left: parent.left; bottomMargin: 6; leftMargin: 7 }
        text: primaryMetric
        font.family: Theme.aesthetic === "editorial" ? Theme.fontDisplay : Theme.fontData
        font.pixelSize: 13
        font.weight: Font.Normal
        color: isSelected ? Theme.colorAccent : Theme.colorText2
    }

    // Hover/press states
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onContainsMouseChanged: parent.border.color =
            containsMouse ? Theme.colorBorderStrong : (isSelected ? Theme.colorAccent : Theme.colorBorder)
        onClicked: captureSelected(index)
    }
}
```

### 7.8 Status pill — `PpPill.qml`

```qml
Rectangle {
    property string pillType: "neutral"  // "neutral" | "live" | "rec" | "warn" | "good"
    property string pillText: ""
    property bool showDot: false

    height: 22
    radius: height / 2   // always a capsule
    color: _bg
    border.width: 1
    border.color: _border

    readonly property color _bg: {
        if (pillType === "live") return Theme.colorGoodLight
        if (pillType === "rec")  return Theme.colorWarnLight
        if (pillType === "good") return Theme.colorGoodLight
        return "transparent"
    }
    readonly property color _border: {
        if (pillType === "live") return Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.25)
        if (pillType === "rec")  return Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.25)
        if (pillType === "good") return Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.25)
        return Theme.colorBorder
    }

    Row {
        anchors.centerIn: parent
        spacing: 5
        leftPadding: 9
        rightPadding: 9

        Rectangle {
            visible: showDot
            width: 5; height: 5
            radius: 2.5
            color: pillType === "rec" ? Theme.colorWarn : Theme.colorGood
            anchors.verticalCenter: parent.verticalCenter

            // Blinking animation for REC pill only
            SequentialAnimation on opacity {
                running: pillType === "rec"
                loops: Animation.Infinite
                NumberAnimation { to: 0.25; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0;  duration: 600; easing.type: Easing.InOutSine }
            }
        }

        Text {
            text: pillText
            font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.weight: Font.Normal
            color: {
                if (pillType === "live" || pillType === "good") return Theme.colorGood
                if (pillType === "rec")  return Theme.colorWarn
                return Theme.colorText3
            }
            font.letterSpacing: Theme.trackingData
        }
    }
}
```

### 7.9 Primary button — `PpButton.qml`

```qml
Rectangle {
    property string label: ""
    property bool primary: false
    property bool destructive: false

    height: 34
    radius: Theme.radius
    color: _bg
    border.width: 1
    border.color: _border

    readonly property color _bg: {
        if (primary) return Theme.colorAccent
        if (destructive) return Theme.colorWarnLight
        return "transparent"
    }
    readonly property color _border: {
        if (primary) return Theme.colorAccent
        if (destructive) return Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.5)
        return Theme.colorBorderStrong
    }

    Text {
        anchors.centerIn: parent
        text: parent.label
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight: primary ? Font.Normal : Font.Light
        color: {
            if (primary) return Theme.dark ? Theme.colorBg : "#FFFFFF"
            if (destructive) return Theme.colorWarn
            return Theme.colorText2
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onContainsMouseChanged: parent.opacity = containsMouse ? 0.85 : 1.0
        onPressed: parent.scale = 0.97
        onReleased: parent.scale = 1.0
        onClicked: parent.clicked()
    }

    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    Behavior on scale   { NumberAnimation { duration: Theme.durationFast } }

    signal clicked()
}
```

---

## 8. SCREEN-LEVEL LAYOUT

All screens follow the same shell structure. Build it once in `MainWindow.qml`
and swap only the content area.

```qml
// MainWindow.qml
Rectangle {
    color: Theme.colorBg

    Row {
        anchors.fill: parent

        // Left navigation rail — always visible
        PpRail {
            id: rail
            height: parent.height
        }

        // Main content column
        Column {
            width: parent.width - rail.width
            height: parent.height

            // Top header bar — always visible
            PpHeader {
                width: parent.width
            }

            // Content area — swapped by screen
            Loader {
                id: screenLoader
                width: parent.width
                height: parent.height - header.height
                source: currentScreenSource
            }
        }
    }
}
```

### Session view grid

The swing analysis screen uses a three-column, three-row grid. Use a `GridLayout`
or explicit anchoring — do not use `ColumnLayout`/`RowLayout` for this layout as
the video panels must fill their cells completely.

```qml
// ScreenSwingAnalysis.qml
Item {
    anchors.fill: parent

    // Row 1: video panels + metrics rail (fills all remaining height)
    // Row 2: capture carousel (fixed height)
    // Row 3: status bar (fixed height)

    PpVideoPanel {
        id: livePanel
        anchors { top: parent.top; left: parent.left; bottom: carousel.top }
        width: parent.width * 0.47   // ~45% of content area
    }

    PpVideoPanel {
        id: replayPanel
        anchors { top: parent.top; left: livePanel.right; bottom: carousel.top }
        width: parent.width * 0.31   // ~30% of content area
    }

    PpMetricRail {
        anchors { top: parent.top; left: replayPanel.right; right: parent.right; bottom: carousel.top }
    }

    PpCarousel {
        id: carousel
        anchors { bottom: statusBar.top; left: parent.left; right: parent.right }
        height: Theme.carouselHeight
    }

    PpStatusBar {
        id: statusBar
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: Theme.statusBarHeight
    }
}
```

---

## 9. ANIMATION AND TRANSITIONS

Keep animations subtle. The design language is quiet; transitions should reinforce
that, not draw attention.

```qml
// Screen transitions — cross-fade only, no slides
Loader {
    id: screenLoader

    // Fade out old, fade in new
    onSourceChanged: {
        fadeOut.start()
    }

    NumberAnimation {
        id: fadeOut
        target: screenLoader
        property: "opacity"
        to: 0
        duration: Theme.durationNormal
        onFinished: { screenLoader.source = pendingSource; fadeIn.start() }
    }
    NumberAnimation {
        id: fadeIn
        target: screenLoader
        property: "opacity"
        to: 1
        duration: Theme.durationNormal
    }
}

// Theme switch — animate colour changes
// Bind all colors through Theme and add Behavior to the root Rectangle
Behavior on color { ColorAnimation { duration: Theme.durationSlow } }

// Hover states — opacity only, never scale for data panels
// Scale (0.97) is permitted only on PpButton
Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
```

---

## 10. WHAT TO NEVER DO

These rules are absolute. Do not break them regardless of expediency.

- **Never hardcode a colour** — always use `Theme.color*`
- **Never hardcode a font family string** — always use `Theme.font*`
- **Never hardcode a font pixel size** — always use `Theme.fontSz*`
- **Never use `font.bold: true`** — use `font.weight: Font.Medium` at most
- **Never use `font.weight: Font.Bold` or `Font.DemiBold`** — too heavy
- **Never use gradients, drop shadows, or blur effects** — the design is flat
- **Never use `Image` decoratively** — all visual elements are geometric primitives
- **Never use `border.width` greater than 1** in standard components
  (exception: the active-mode selection indicator in the left rail is 2px in Editorial)
- **Never use opaque coloured backgrounds on the rail or header** that compete
  with the content area — the structural chrome must always recede
- **Never animate `x`, `y`, `width`, or `height`** on the main layout elements —
  layout shifts are jarring; only opacity transitions are used between screens
- **Never use `Qt.rgba(r, g, b, 1.0)` when `Theme.color*` already provides the value**
- **Never set `font.capitalization: Font.AllUppercase`** except on `label` and `micro`
  role text — body copy and data values are always sentence case
- **Never hardcode a secondary side-panel width** — always use `Theme.sidenavWidth`
  (275 sp). Every secondary navigation panel (settings, athletes, or any future screen
  with a side rail) must bind to this token so all sidebars remain the same width.

---

## 11. MULTI-MONITOR SUPPORT

The secondary monitor dashboard is a separate `QQuickWindow` instantiated from C++.
It shares the same `Theme` singleton (registered as a QML singleton via C++).
All component imports resolve from the same QML module path.

```cpp
// C++ — creating the dashboard window
auto dashWin = new QQuickWindow();
dashWin->setSource(QUrl("qrc:/ui/screens/ScreenDashboard.qml"));
dashWin->setScreen(QGuiApplication::screens().value(1));
dashWin->show();
```

The dashboard window does not have a rail or header. It fills its screen with the
metrics grid and kinematic sequence chart. It observes the same `SessionModel` as
the primary window via the C++ data layer.

---

## 12. THEME SWITCHING AT RUNTIME

Expose theme switching to QML via C++ properties on the root context:

```cpp
// main.cpp
engine.rootContext()->setContextProperty("AppSettings", &settings);
// AppSettings exposes: aesthetic (string), darkMode (bool)
```

```qml
// Theme.qml — bind to C++ settings
aesthetic: AppSettings.aesthetic
dark: AppSettings.darkMode
```

When `aesthetic` or `dark` changes, all `Theme.color*` bindings update automatically.
Because QML bindings are reactive, every component in the scene re-evaluates its
colour properties without any explicit repaint calls. Add `Behavior on color` to
the root application `Rectangle` to animate the transition.

---

*This document is the single source of truth for Pinpoint visual implementation.
When in doubt, match the HTML prototypes in `pinpoint-aesthetic-*.html`.
All palette values in this document were extracted directly from those prototypes.*
