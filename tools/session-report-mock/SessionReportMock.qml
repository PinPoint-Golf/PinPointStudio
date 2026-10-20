import QtQuick
import PinPointStudio

// SESSION REPORT — MOCK register (design: docs/design/session_report_design.md).
// Binds ONE map, `report` (the shape the SessionReportModel will publish), and draws fixed A4 pages at
// 96 dpi logical. Contract with PpPdfPrinter: printPage (writable) / printPageCount.
// Mock only: the build lays the flowed parts (numbers table, appendix) on PpPager.
Item {
    id: root
    property var report: ({})
    property int printPage: 0
    readonly property int printPageCount: pages.length
    property string medium: "print"
    width: 794; height: 1123

    readonly property var rep: report || ({})
    readonly property var brand: rep.brand || ({})
    readonly property var meta: rep.meta || ({})
    readonly property color accent: brand.accent || "#1F5A4C"
    readonly property color ink: ro.ink
    readonly property color ink2: ro.ink2
    readonly property color ink3: ro.ink3
    readonly property color warn: ro.warn
    readonly property color hair: Qt.alpha(ro.ink, 0.16)
    readonly property color wash: Qt.alpha(accent, 0.075)
    readonly property string serif: Theme.serifFamily
    readonly property string sans: ro.r.textFamily
    readonly property string figs: ro.r.figureFamily
    readonly property real mx: 64                      // side margin
    readonly property real tw: width - 2 * mx           // text width

    // ── pages ──
    readonly property var pages: ["cover", "grid", "order"].concat((rep.workOns || []).map((w, i) => "wo" + (i + 1))).concat(["practice", "about"])
    function pageNo(k) { return pages.indexOf(k) + 1 }
    function woPage(rank) { return pageNo("wo" + rank) }

    PpTypeset { id: ro; medium: "print" }

    // ═══════════════════════ shared pieces ═══════════════════════
    component Kicker: Text { font.family: root.sans; font.pointSize: 0.75 * (10.5); font.weight: Font.DemiBold; font.letterSpacing: 0.6; color: root.accent }
    component H1: Text { font.family: root.serif; font.pointSize: 0.75 * (31); color: root.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
    component H2: Text { font.family: root.serif; font.pointSize: 0.75 * (19); color: root.ink; wrapMode: Text.WordWrap; lineHeight: 1.12 }
    component H3: Text { font.family: root.sans; font.pointSize: 0.75 * (12.5); font.weight: Font.DemiBold; color: root.ink; wrapMode: Text.WordWrap }
    component Body: Text { font.family: root.sans; font.pointSize: 0.75 * (12.5); color: root.ink; wrapMode: Text.WordWrap; lineHeight: 1.32; textFormat: Text.StyledText }
    component Small: Text { font.family: root.sans; font.pointSize: 0.75 * (10.5); color: root.ink2; wrapMode: Text.WordWrap; lineHeight: 1.3; textFormat: Text.StyledText }
    component Micro: Text { font.family: root.sans; font.pointSize: 0.75 * (9.5); color: root.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    component Stand: Text { font.family: root.serif; font.pointSize: 0.75 * (17); color: root.ink; wrapMode: Text.WordWrap; lineHeight: 1.3 }
    component Rule: Rectangle { height: 0.75; color: root.hair }
    component Num: Text { font.family: root.figs; color: root.ink }

    // a swing's mark: fired = filled warn, clean = hollow ring, not measured = a short stub
    component SwingMark: Item {
        property string state: "clean"
        property real d: 9
        width: d; height: d
        Rectangle { anchors.centerIn: parent; visible: parent.state === "fired"; width: parent.d; height: width; radius: width / 2; color: root.warn }
        Rectangle { anchors.centerIn: parent; visible: parent.state === "clean"; width: parent.d - 1.5; height: width; radius: width / 2; color: "transparent"; border.color: root.ink3; border.width: 1 }
        Rectangle { anchors.centerIn: parent; visible: parent.state !== "fired" && parent.state !== "clean"; width: parent.d * 0.6; height: 1.2; color: root.ink3 }
    }

    // the furniture every page but the cover carries
    component Page: Item {
        id: pg
        property string key: ""
        property string section: ""
        default property alias content: body.data
        width: root.width; height: root.height
        visible: root.pages[root.printPage] === key
        Rectangle { anchors.fill: parent; color: "#FFFFFF" }
        // running head
        Item {
            x: root.mx; y: 34; width: root.tw; height: 22
            Row {
                spacing: 8; anchors.verticalCenter: parent.verticalCenter
                Monogram { d: 18 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: root.brand.studio || ""; font.family: root.sans; font.pointSize: 0.75 * (10); font.weight: Font.DemiBold; color: root.ink }
            }
            Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                   text: (root.meta.golfer || "") + "  ·  session of " + (root.meta.dateShort || "") + "  ·  " + (root.meta.club || "")
                   font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink2 }
            Rectangle { anchors.top: parent.bottom; anchors.topMargin: 8; width: parent.width; height: 0.75; color: root.hair }
        }
        Item { id: body; x: root.mx; y: 84; width: root.tw; height: root.height - 84 - 70 }
        // folio
        Item {
            x: root.mx; y: root.height - 46; width: root.tw; height: 16
            Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: 8; width: parent.width; height: 0.75; color: root.hair }
            Text { text: (root.brand.coach || "") + ", " + (root.brand.studio || "") + "  ·  " + (root.brand.contact || ""); font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
            Text { anchors.right: parent.right; text: "Prepared " + (root.meta.preparedShort || "") + "  ·  " + pg.section + "   " + (root.printPage + 1) + " / " + root.pages.length; font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
        }
    }

    component Monogram: Rectangle {
        property real d: 40
        width: d; height: d; radius: d / 2; color: root.accent
        Text { anchors.centerIn: parent; text: root.brand.monogram || ""; color: "white"; font.family: root.serif; font.pointSize: 0.75 * (parent.d * 0.42); font.letterSpacing: 0.5 }
    }

    // a cropped still, captioned; no bystanders leave the studio (the crop is the golfer's box)
    component Still: Column {
        property string src: ""
        property string caption: ""
        property string value: ""
        property string note: ""
        property bool dtl: src.indexOf("_DTL_") >= 0
        property real w: 150
        spacing: 5
        Rectangle {
            width: parent.w; height: parent.w * 1.5; color: "#111"; clip: true
            Image {
                anchors.fill: parent; source: parent.parent.src; fillMode: Image.PreserveAspectCrop; asynchronous: false
                sourceClipRect: parent.parent.dtl ? Qt.rect(300, 170, 560, 840) : Qt.rect(280, 180, 440, 660)
                smooth: true; mipmap: true
            }
        }
        Row {
            spacing: 6
            Text { text: parent.parent.caption; font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink2 }
            Text { visible: parent.parent.value !== ""; text: parent.parent.value; font.family: root.figs; font.pointSize: 0.75 * (10); font.weight: Font.DemiBold; color: root.ink }
        }
        Text { visible: parent.note !== ""; width: parent.w; text: parent.note; font.family: root.sans; font.pointSize: 0.75 * (9.5); color: root.ink3; wrapMode: Text.WordWrap }
    }

    // a run of the swings against the target range: the range as a wash, the fault edge implied by the dots
    component RunChart: Item {
        id: rc
        property var fig: ({})
        property string label: ""
        property real plotH: 120
        height: plotH + 30
        readonly property var pts: fig.points || []
        readonly property var target: {
            const g = (fig.bands || []).filter(b => b.grade === "ideal" || b.grade === "good")
            return g.length ? { lo: Math.min.apply(null, g.map(b => b.lo)), hi: Math.max.apply(null, g.map(b => b.hi)) } : null
        }
        readonly property real vlo: {
            const vs = pts.filter(p => p.value !== null).map(p => p.value)
            const t = target ? [target.lo, target.hi] : []
            const all = vs.concat(t), lo = Math.min.apply(null, all), hi = Math.max.apply(null, all)
            return lo - (hi - lo) * 0.08
        }
        readonly property real vhi: {
            const vs = pts.filter(p => p.value !== null).map(p => p.value)
            const t = target ? [target.lo, target.hi] : []
            const all = vs.concat(t), lo = Math.min.apply(null, all), hi = Math.max.apply(null, all)
            return hi + (hi - lo) * 0.08
        }
        readonly property real px0: 34
        readonly property real pw: width - px0 - 70
        function fx(i) { return px0 + (i + 0.5) * pw / Math.max(1, pts.length) }
        function fy(v) { return plotH * (1 - (v - vlo) / (vhi - vlo)) }
        // target range wash + its label
        Rectangle {
            visible: !!rc.target
            x: rc.px0; width: rc.pw
            y: rc.target ? rc.fy(Math.min(rc.vhi, rc.target.hi)) : 0
            height: rc.target ? rc.fy(Math.max(rc.vlo, rc.target.lo)) - y : 0
            color: root.wash
            Rectangle { width: parent.width; height: 0.75; color: Qt.alpha(root.accent, 0.5); visible: rc.target && rc.target.hi < rc.vhi }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 0.75; color: Qt.alpha(root.accent, 0.5); visible: rc.target && rc.target.lo > rc.vlo }
        }
        Text {
            visible: !!rc.target
            x: rc.px0 + rc.pw + 8
            y: rc.target ? (rc.fy(Math.min(rc.vhi, rc.target.hi)) + rc.fy(Math.max(rc.vlo, rc.target.lo))) / 2 - height / 2 : 0
            text: "target\nrange"; font.family: root.sans; font.pointSize: 0.75 * (9.5); color: root.accent; lineHeight: 1.1
        }
        // y ticks: the target edges and the axis ends
        Repeater {
            model: rc.target ? [rc.target.lo, rc.target.hi].filter(v => v > rc.vlo && v < rc.vhi) : []
            delegate: Text {
                required property var modelData
                x: 0; width: rc.px0 - 6; horizontalAlignment: Text.AlignRight
                y: rc.fy(modelData) - height / 2
                text: Math.round(modelData) + (rc.fig.unit === "°" ? "°" : ""); font.family: root.figs; font.pointSize: 0.75 * (9); color: root.ink3
            }
        }
        Rectangle { x: rc.px0; y: rc.plotH; width: rc.pw; height: 0.75; color: root.hair }
        Repeater {
            model: rc.pts
            delegate: Item {
                required property var modelData
                required property int index
                SwingMark {
                    x: rc.fx(index) - width / 2
                    y: modelData.value === null ? rc.plotH - 6 : rc.fy(modelData.value) - height / 2
                    state: modelData.state; d: 9
                }
                Text { x: rc.fx(index) - width / 2; y: rc.plotH + 5; text: modelData.swing; font.family: root.figs; font.pointSize: 0.75 * (9); color: root.ink3 }
            }
        }
        Text { x: rc.px0; y: rc.plotH + 18; text: rc.label; font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
    }

    component DrillCard: Rectangle {
        property var drill: ({})
        property bool compact: false
        property var also: []
        property real fs: 12.5
        width: parent ? parent.width : 300
        height: dc.implicitHeight + 24
        color: Qt.alpha(root.accent, 0.045)
        border.color: Qt.alpha(root.accent, 0.25); border.width: 0.75
        radius: 3
        Column {
            id: dc
            x: 14; y: 12; width: parent.width - 28; spacing: 5
            Row {
                spacing: 8
                Text { text: "Drill"; font.family: root.sans; font.pointSize: 0.75 * (10); font.weight: Font.DemiBold; color: root.accent }
                Text { text: drill.label || ""; font.family: root.sans; font.pointSize: 0.75 * (12.5); font.weight: Font.DemiBold; color: root.ink }
                Text { visible: !!drill.equipment; text: drill.equipment ? "· you need " + drill.equipment : ""; font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink3; anchors.baseline: parent.children[1].baseline }
            }
            Body { width: parent.width; text: drill.instruction || ""; font.pointSize: 0.75 * (fs) }
            Small { width: parent.width; visible: !compact; text: "<i>What it is for.</i> " + (drill.targets || "") }
            Small { width: parent.width; visible: !compact && !!drill.note; text: "<i>If it doesn’t work.</i> " + (drill.note || ""); color: root.ink3 }
            Small { width: parent.width; visible: also.length > 0; color: root.ink
                    text: also.map(a => "<b>Also: " + a.label + ".</b> " + a.instruction).join("<br>") }
        }
    }

    // what might be behind a work-on: the lead, then every authored cause grouped by what we know about it
    component CausesBlock: Column {
        id: cb
        property var causes: ({})
        property real labelW: 190
        property real fs: 11
        width: parent ? parent.width : 300
        spacing: 7
        visible: !!causes.lead
        Body { width: cb.width; text: cb.causes.lead || ""; font.pointSize: 0.75 * (cb.fs + 1) }
        Repeater {
            model: cb.causes.rows || []
            delegate: Item {
                required property var modelData
                width: cb.width; height: Math.max(ct.height, ci.height)
                Row {
                    id: ct; spacing: 6; width: cb.labelW - 10
                    Rectangle { y: 4; width: 7; height: 7; radius: 3.5
                                color: modelData.kind === "seen" ? root.warn : (modelData.kind === "ruledOut" ? "transparent" : "transparent")
                                border.color: modelData.kind === "ruledOut" ? root.ink3 : (modelData.kind === "possible" ? root.accent : "transparent"); border.width: modelData.kind === "possible" ? 1.5 : 1 }
                    Text { width: cb.labelW - 23; text: modelData.title; font.family: root.sans; font.pointSize: 0.75 * (cb.fs); font.weight: Font.DemiBold; color: root.ink; wrapMode: Text.WordWrap }
                }
                Text {
                    id: ci; x: cb.labelW; width: cb.width - cb.labelW; wrapMode: Text.WordWrap; textFormat: Text.StyledText; lineHeight: 1.28
                    font.family: root.sans; font.pointSize: 0.75 * (cb.fs); color: root.ink
                    text: modelData.items.join(" · ") + (modelData.more ? (modelData.kind === "possible" ? "<br>" : " · ") + "<font color=\"" + root.ink3 + "\">" + modelData.more + "</font>" : "")
                          + (modelData.note ? "  <font color=\"" + root.ink3 + "\">— " + modelData.note + "</font>" : "")
                }
            }
        }
    }

    // a labelled paragraph block: label in the margin column, text in the measure
    component Para: Item {
        property string label: ""
        property string text: ""
        property real labelW: 118
        property color tone: root.ink
        property real fs: 12.5
        width: parent ? parent.width : 300
        readonly property bool stacked: labelW <= 0
        height: stacked ? pl.height + 4 + pt.height : Math.max(pl.height, pt.height)
        visible: text !== ""
        Text { id: pl; width: parent.stacked ? parent.width : parent.labelW - 12; text: parent.label; font.family: root.sans; font.pointSize: 0.75 * (10.5); font.weight: Font.DemiBold; color: root.ink; wrapMode: Text.WordWrap }
        Body { id: pt; x: parent.stacked ? 0 : parent.labelW; y: parent.stacked ? pl.height + 4 : 0; width: parent.width - x; text: parent.text; color: parent.tone; font.pointSize: 0.75 * (parent.fs) }
    }

    // ═══════════════════════ 1 · the cover: the session on one page ═══════════════════════
    Item {
        id: cover
        width: root.width; height: root.height
        visible: root.pages[root.printPage] === "cover"
        Rectangle { anchors.fill: parent; color: "#FFFFFF" }
        // masthead band
        Rectangle { x: 0; y: 0; width: parent.width; height: 6; color: root.accent }
        Row {
            x: root.mx; y: 34; spacing: 12
            Monogram { d: 38 }
            Column {
                anchors.verticalCenter: parent.verticalCenter; spacing: 1
                Text { text: root.brand.studio || ""; font.family: root.serif; font.pointSize: 0.75 * (17); color: root.ink }
                Text { text: (root.brand.coach || "") + "  ·  " + (root.brand.coachTitle || ""); font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink2 }
            }
        }
        Column {
            x: root.mx; y: 40; width: root.tw; spacing: 2
            Text { width: parent.width; horizontalAlignment: Text.AlignRight; text: "Session report"; font.family: root.sans; font.pointSize: 0.75 * (10.5); font.weight: Font.DemiBold; font.letterSpacing: 0.6; color: root.accent }
            Text { width: parent.width; horizontalAlignment: Text.AlignRight; text: "Prepared " + (root.meta.prepared || ""); font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink2 }
        }

        Column {
            x: root.mx; y: 104; width: root.tw; spacing: 0
            Kicker { text: "Session of " + (root.meta.date || "") + "  ·  " + (root.meta.club || "") + "  ·  " + (root.meta.swings || 0) + " swings  ·  " + (root.meta.sessionOfDay || "") }
            Item { width: 1; height: 8 }
            Text { text: root.meta.golfer || ""; font.family: root.serif; font.pointSize: 0.75 * (44); color: root.ink }
            Item { width: 1; height: 12 }
            Stand { width: root.tw * 0.86; text: root.rep.headline || ""; font.pointSize: 0.75 * (19) }
            Item { width: 1; height: 20 }
            // the coach's note — typed at generation, optional
            Row {
                visible: !!root.rep.coachNote
                spacing: 14
                Rectangle { width: 2; height: cn.height; color: root.accent }
                Column {
                    id: cn; width: root.tw - 16; spacing: 6
                    Text { width: parent.width; text: root.rep.coachNote || ""; font.family: root.serif; font.italic: true; font.pointSize: 0.75 * (13.5); color: root.ink; wrapMode: Text.WordWrap; lineHeight: 1.35 }
                    Text { text: "— " + (root.brand.coach || ""); font.family: root.sans; font.pointSize: 0.75 * (10.5); color: root.ink2 }
                }
            }
            Item { width: 1; height: 26 }

            // three columns: going well · what we found · what to work on
            Row {
                id: three
                width: root.tw; spacing: 22
                readonly property real cw: (width - 2 * spacing) / 3
                Column {
                    width: three.cw; spacing: 8
                    Rule { width: parent.width; color: root.accent; height: 1.5 }
                    H3 { text: "Going well" }
                    Repeater {
                        model: root.rep.strengths || []
                        delegate: Column {
                            required property var modelData
                            width: three.cw; spacing: 1
                            Body { width: parent.width; text: modelData.text; font.pointSize: 0.75 * (12) }
                            Micro { text: modelData.count }
                        }
                    }
                }
                Column {
                    width: three.cw; spacing: 8
                    Rule { width: parent.width; color: root.ink; height: 1.5 }
                    H3 { text: "What we found" }
                    Repeater {
                        model: root.rep.found || []
                        delegate: Row {
                            required property var modelData
                            spacing: 7
                            SwingMark { state: "fired"; d: 7; y: 5 }
                            Column {
                                width: three.cw - 14; spacing: 1
                                Body { width: parent.width; text: modelData.golfer; font.pointSize: 0.75 * (12) }
                                Micro { text: modelData.count }
                            }
                        }
                    }
                }
                Column {
                    width: three.cw; spacing: 8
                    Rule { width: parent.width; color: root.accent; height: 1.5 }
                    H3 { text: "What to work on" }
                    Repeater {
                        model: root.rep.workOnsBrief || []
                        delegate: Row {
                            required property var modelData
                            spacing: 8
                            Text { text: modelData.rank; font.family: root.serif; font.pointSize: 0.75 * (modelData.rank === 1 ? 22 : 15); color: modelData.rank === 1 ? root.accent : root.ink3; width: 16 }
                            Column {
                                width: three.cw - 24; spacing: 1
                                Body { width: parent.width; text: modelData.goal; font.pointSize: 0.75 * (modelData.rank === 1 ? 13 : 12); font.weight: modelData.rank === 1 ? Font.DemiBold : Font.Normal }
                                Micro { text: (modelData.drill ? "Drill: " + modelData.drill + "  ·  " : "") + "p. " + root.woPage(modelData.rank) }
                            }
                        }
                    }
                }
            }
        }

        // the ball, at the foot of the page (only with a launch monitor)
        Column {
            visible: !!root.rep.ball
            x: root.mx; y: root.height - 276; width: root.tw; spacing: 10
            Row {
                spacing: 10
                H3 { text: "The ball" }
                Small { text: "measured on every swing by " + ((root.rep.ball || {}).device || ""); anchors.baseline: parent.children[0].baseline; color: root.ink3 }
            }
            Rule { width: parent.width }
            Grid {
                columns: 4; columnSpacing: 0; rowSpacing: 14
                Repeater {
                    model: (root.rep.ball || {}).tiles || []
                    delegate: Column {
                        required property var modelData
                        width: root.tw / 4; spacing: 1
                        Micro { text: modelData.label; color: root.ink2; font.pointSize: 0.75 * (10) }
                        Row {
                            spacing: 4
                            Num { text: modelData.value; font.pointSize: 0.75 * (24) }
                            Text { text: modelData.unit; font.family: root.sans; font.pointSize: 0.75 * (10.5); color: root.ink2; anchors.baseline: parent.children[0].baseline }
                        }
                        Micro { text: modelData.range }
                    }
                }
            }
            Micro { text: (root.rep.ball || {}).note || ""; width: parent.width }
        }
        Item {
            x: root.mx; y: root.height - 46; width: root.tw; height: 16
            Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: 8; width: parent.width; height: 0.75; color: root.hair }
            Text { text: (root.brand.contact || "") ; font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
            Text { anchors.right: parent.right; text: "Inside: swing by swing p. 2  ·  why this order p. 3  ·  your work-ons p. 4  ·  practice and numbers p. " + root.pageNo("practice"); font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
        }
    }

    // ═══════════════════════ 2 · swing by swing ═══════════════════════
    Page {
        key: "grid"; section: "Swing by swing"
        Column {
            width: parent.width; spacing: 0
            Kicker { text: "Swing by swing" }
            Item { width: 1; height: 6 }
            H1 { width: parent.width; text: (root.rep.grid || {}).title || "Swing by swing" }
            Item { width: 1; height: 10 }
            Stand { width: parent.width * 0.9; font.pointSize: 0.75 * (15.5)
                    text: (root.rep.grid || {}).standfirst || "" }
            Item { width: 1; height: 22 }
            // the grid
            Item {
                id: gridBox
                width: parent.width
                readonly property real nameW: 236
                readonly property real countW: 54
                readonly property var g: root.rep.grid || ({})
                readonly property int n: (g.swings || []).length
                readonly property real pitch: (width - nameW - countW) / Math.max(1, n)
                readonly property var groups: [{ key: "habit", title: "Your habits — the five work-ons" }, { key: "seen", title: "Seen, not yet habits" }, { key: "ball", title: "What the ball did" }]
                height: gcol.height
                Column {
                    id: gcol
                    width: parent.width; spacing: 0
                    // swing numbers
                    Item {
                        width: parent.width; height: 20
                        Repeater {
                            model: gridBox.g.swings || []
                            delegate: Text {
                                required property var modelData
                                required property int index
                                x: gridBox.nameW + (index + 0.5) * gridBox.pitch - width / 2
                                text: modelData; font.family: root.figs; font.pointSize: 0.75 * (10)
                                font.weight: modelData === gridBox.g.best ? Font.Bold : Font.Normal
                                color: modelData === gridBox.g.best ? root.accent : root.ink3
                            }
                        }
                        Text { x: gridBox.width - gridBox.countW; width: gridBox.countW; horizontalAlignment: Text.AlignRight; text: "swings"; font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
                    }
                    Rectangle {   // the best swing's column
                        width: 1; height: 1; color: "transparent"
                    }
                    Repeater {
                        model: gridBox.groups
                        delegate: Column {
                            required property var modelData
                            width: gridBox.width
                            Item { width: 1; height: 10 }
                            Text { text: modelData.title; font.family: root.sans; font.pointSize: 0.75 * (10); font.weight: Font.DemiBold; color: root.ink2 }
                            Item { width: 1; height: 4 }
                            Rule { width: parent.width }
                            Repeater {
                                model: (gridBox.g.rows || []).filter(r => r.group === modelData.key)
                                delegate: Item {
                                    required property var modelData
                                    width: gridBox.width; height: (gridBox.g.rows || []).length > 9 ? 22 : 25
                                    Text { y: 6; width: gridBox.nameW - 10; elide: Text.ElideRight; text: modelData.name; font.family: root.sans; font.pointSize: 0.75 * (11.5); color: root.ink }
                                    Repeater {
                                        model: modelData.marks
                                        delegate: SwingMark {
                                            required property var modelData
                                            required property int index
                                            x: gridBox.nameW + (index + 0.5) * gridBox.pitch - width / 2; y: 9
                                            state: modelData; d: 9
                                        }
                                    }
                                    Text { x: gridBox.width - gridBox.countW; y: 6; width: gridBox.countW; horizontalAlignment: Text.AlignRight; text: modelData.count; font.family: root.figs; font.pointSize: 0.75 * (10.5); color: root.ink2 }
                                    Rule { anchors.bottom: parent.bottom; width: parent.width; color: Qt.alpha(root.ink, 0.07) }
                                }
                            }
                        }
                    }
                    // carry, as bars under the swings
                    Item { width: 1; height: 12 }
                    Item {
                        visible: (gridBox.g.carry || []).length > 0
                        width: gridBox.width; height: visible ? 64 : 0
                        readonly property var c: gridBox.g.carry || []
                        readonly property real cmax: Math.max.apply(null, c.length ? c : [1])
                        readonly property real cmin: Math.min.apply(null, c.length ? c : [0]) - 20
                        Text { y: 26; text: "Carry, yards"; font.family: root.sans; font.pointSize: 0.75 * (11.5); color: root.ink }
                        Repeater {
                            model: parent.c
                            delegate: Item {
                                required property var modelData
                                required property int index
                                readonly property real h: 46 * (modelData - parent.cmin) / (parent.cmax - parent.cmin)
                                x: gridBox.nameW + (index + 0.5) * gridBox.pitch - 7; y: 50 - h; width: 14; height: h
                                Rectangle { anchors.fill: parent; color: index + 1 === gridBox.g.best ? root.accent : Qt.alpha(root.ink, 0.22) }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.top; anchors.bottomMargin: 2; text: modelData; font.family: root.figs; font.pointSize: 0.75 * (8.5); color: root.ink3 }
                            }
                        }
                        Rule { y: 50; width: parent.width }
                    }
                }
            }
            Item { width: 1; height: 6 }
            Row {
                spacing: 16
                Row { spacing: 5; SwingMark { state: "fired"; d: 8; y: 2 } Micro { text: "showed on this swing" } }
                Row { spacing: 5; SwingMark { state: "clean"; d: 8; y: 2 } Micro { text: "did not" } }
                Row { spacing: 5; SwingMark { state: "na"; d: 8; y: 2 } Micro { text: "could not be measured" } }
            }
            Micro { visible: !!(root.rep.grid || {}).sessionNote; width: parent.width; topPadding: 4; text: (root.rep.grid || {}).sessionNote || ""; color: root.ink2 }
            Item { width: 1; height: 12 }
            // the reference swing
            Row {
                width: parent.width; spacing: 22
                Column {
                    width: 196; spacing: 8
                    Rule { width: parent.width; color: root.accent; height: 1.5 }
                    H3 { text: "Your reference swing" }
                    Body { width: parent.width; font.pointSize: 0.75 * (12); text: (root.rep.grid || {}).bestText || "" }
                }
                Row {
                    spacing: 8
                    Repeater {
                        model: ((root.rep.grid || {}).bestStills || [])
                        delegate: Still {
                            required property var modelData
                            required property int index
                            w: (root.rep.grid || {}).rows && root.rep.grid.rows.length > 9 ? 86 : 100; src: modelData; caption: ((root.rep.grid || {}).bestLabels || [])[index] || ""
                        }
                    }
                }
            }
            Item { width: 1; height: 10 }
            // earlier today
            Column {
                visible: !!root.rep.today
                width: parent.width; spacing: 6
                readonly property var t: root.rep.today || ({})
                H3 { text: "Earlier today" }
                Rule { width: parent.width }
                Row {
                    spacing: 0
                    Item { width: 236; height: 16 }
                    Repeater { model: parent.parent.t.sessions || []
                        delegate: Text { required property var modelData; width: 96; horizontalAlignment: Text.AlignHCenter; text: modelData; font.family: root.sans; font.pointSize: 0.75 * (9.5); color: root.ink3 } }
                }
                Repeater {
                    model: parent.t.rows || []
                    delegate: Row {
                        required property var modelData
                        spacing: 0
                        Text { width: 236; text: modelData.name; font.family: root.sans; font.pointSize: 0.75 * (11); color: root.ink; elide: Text.ElideRight }
                        Repeater {
                            model: modelData.marks
                            delegate: Row {
                                required property var modelData
                                width: 96; spacing: 5
                                Item { width: 26; height: 1 }
                                SwingMark { state: modelData.state === "pattern" ? "fired" : (modelData.state === "none" ? "na" : "clean"); d: 8; y: 3 }
                                Text { text: modelData.state === "pattern" ? "habit " + modelData.text : (modelData.state === "none" ? "-" : modelData.text); font.family: root.figs; font.pointSize: 0.75 * (10); color: modelData.state === "pattern" ? root.ink : root.ink3 }
                            }
                        }
                    }
                }
                Micro { width: parent.width; font.pointSize: 0.75 * (10); color: root.ink2; text: parent.t.note || "" }
            }
        }
    }

    // ═══════════════════════ 3 · why this order ═══════════════════════
    Page {
        id: orderPage
        key: "order"; section: "Why this order"
        readonly property var ord: root.rep.order || ({})
        Column {
            width: parent.width; spacing: 0
            Kicker { text: "Why this order" }
            Item { width: 1; height: 6 }
            H1 { width: parent.width; text: "One thing first, and why it is that one" }
            Item { width: 1; height: 6 }
            Stand { width: parent.width * 0.9; font.pointSize: 0.75 * (15.5); text: (orderPage.ord || {}).intro || "" }
            Item { width: 1; height: 14 }

            // the three rules, numbered, each with what it did today
            Repeater {
                model: (orderPage.ord || {}).rules || []
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: root.tw; height: rcol.height + 12
                    Text { text: index + 1; font.family: root.serif; font.pointSize: 0.75 * (22); color: root.accent; width: 34 }
                    Column {
                        id: rcol
                        x: 40; width: root.tw - 40; spacing: 4
                        H3 { text: modelData.title; font.pointSize: 0.75 * (13) }
                        Body { width: parent.width; text: modelData.body; font.pointSize: 0.75 * (11.5) }
                        Row {
                            spacing: 6; width: parent.width
                            Text { text: "Today"; font.family: root.sans; font.pointSize: 0.75 * (10.5); font.weight: Font.DemiBold; color: root.accent; y: 2 }
                            Body { width: parent.width - 50; text: modelData.today; color: root.ink2; font.pointSize: 0.75 * (11.5) }
                        }
                    }
                }
            }
            Item { width: 1; height: 6 }
            // how far each one missed: the bars that set today's order
            Rule { width: parent.width }
            Item { width: 1; height: 12 }
            Repeater {
                model: (orderPage.ord || {}).items || []
                delegate: Item {
                    required property var modelData
                    width: root.tw; height: 30
                    Text { text: modelData.rank; font.family: root.serif; font.pointSize: 0.75 * (16); color: modelData.rank === 1 ? root.accent : root.ink3; width: 22; y: 2 }
                    Text { x: 26; y: 1; width: 300; text: modelData.goal; font.family: root.sans; font.pointSize: 0.75 * (12); font.weight: modelData.rank === 1 ? Font.DemiBold : Font.Normal; color: root.ink; elide: Text.ElideRight }
                    Text { x: 26; y: 16; text: modelData.name + (modelData.phase ? "  ·  " + modelData.phase : "") + "  ·  " + modelData.count; font.family: root.sans; font.pointSize: 0.75 * (9); color: root.ink3 }
                    Rectangle { x: 340; y: 8; width: (root.tw - 340 - 60) * modelData.share; height: 9; color: modelData.rank === 1 ? root.accent : Qt.alpha(root.ink, 0.3) }
                    Text { x: root.tw - 80; y: 4; width: 80; horizontalAlignment: Text.AlignRight; text: "p. " + root.woPage(modelData.rank); font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink3 }
                }
            }
            Micro { text: "Bar: how far outside its target range each habit went, compared with the range’s own width. The longest bar goes first."; width: parent.width }
            Item { width: 1; height: 22 }

            // where each one happens in the swing
            H3 { text: "Where each one happens in your swing" }
            Item { width: 1; height: 8 }
            Item {
                id: tl
                width: root.tw; height: 92
                readonly property var stops: [{ k: "Address", p: "P1" }, { k: "Backswing", p: "P2–P3" }, { k: "Top", p: "P4" }, { k: "Coming down", p: "P5–P6" }, { k: "Impact", p: "P7" }, { k: "Finish", p: "P8–P10" }]
                readonly property var at: ({ "P4": [2], "P7": [4], "P6": [3] })
                function sx(i) { return 20 + i * (width - 40) / (stops.length - 1) }
                Rectangle { x: tl.sx(0); y: 30; width: tl.sx(tl.stops.length - 1) - tl.sx(0); height: 1.5; color: Qt.alpha(root.ink, 0.35) }
                Repeater {
                    model: tl.stops
                    delegate: Item {
                        required property var modelData
                        required property int index
                        Rectangle { x: tl.sx(index) - 4; y: 27; width: 8; height: 8; radius: 4; color: "white"; border.color: root.ink3; border.width: 1 }
                        Text { x: tl.sx(index) - width / 2; y: 40; text: modelData.k; font.family: root.sans; font.pointSize: 0.75 * (10); color: root.ink2 }
                        Text { x: tl.sx(index) - width / 2; y: 54; text: modelData.p; font.family: root.figs; font.pointSize: 0.75 * (9); color: root.ink3 }
                    }
                }
                // the work-ons, placed at the phase each is read at (report.order.timeline)
                Repeater {
                    model: ((orderPage.ord || {}).timeline || []).map(t => ({ r: t.rank, x: t.pos }))
                    delegate: Rectangle {
                        required property var modelData
                        x: 20 + modelData.x * (tl.width - 40) / 5 - 10; y: 2; width: 20; height: 20; radius: 10
                        color: modelData.r === 1 ? root.accent : "white"; border.color: modelData.r === 1 ? root.accent : root.ink2; border.width: 1
                        Text { anchors.centerIn: parent; text: modelData.r; font.family: root.serif; font.pointSize: 0.75 * (12); color: modelData.r === 1 ? "white" : root.ink }
                    }
                }
                Micro { y: 70; width: tl.width; font.pointSize: 0.75 * (10); color: root.ink2
                        text: (orderPage.ord || {}).timelineNote || "" }
            }
            Item { width: 1; height: 16 }
            // linked · not on the list · worth checking: three columns
            Row {
                id: three3
                width: root.tw; spacing: 18
                readonly property real cw: (width - 2 * spacing) / 3
                Column {
                    visible: ((orderPage.ord || {}).links || []).length > 0
                    width: three3.cw; spacing: 5
                    Rule { width: parent.width; color: root.ink; height: 1.2 }
                    H3 { text: "Linked, not proven"; font.pointSize: 0.75 * (12) }
                    Repeater { model: (orderPage.ord || {}).links || []
                        delegate: Small { required property var modelData; width: three3.cw; color: root.ink; font.pointSize: 0.75 * (10.5)
                                          text: "<b>" + modelData.from + " → " + modelData.to + ".</b> " + modelData.words } }
                }
                Column {
                    width: three3.cw; spacing: 5
                    Rule { width: parent.width; color: root.ink; height: 1.2 }
                    H3 { text: "Not on the list, and why"; font.pointSize: 0.75 * (12) }
                    Repeater { model: (orderPage.ord || {}).notListed || []
                        delegate: Small { required property var modelData; width: three3.cw; color: root.ink; font.pointSize: 0.75 * (10.5)
                                          text: "<b>" + modelData.name + ".</b> " + modelData.why } }
                }
                Column {
                    width: three3.cw; spacing: 5
                    Rule { width: parent.width; color: root.accent; height: 1.2 }
                    H3 { text: "Worth checking next time"; font.pointSize: 0.75 * (12) }
                    Repeater { model: root.rep.checks || []
                        delegate: Small { required property var modelData; width: three3.cw; color: root.ink; font.pointSize: 0.75 * (10.5)
                                          text: "<font color=\"" + root.ink3 + "\">" + modelData.title + "</font><br>" + modelData.body } }
                }
            }
        }
    }

    // ═══════════════════════ 4–6 · a work-on in depth ═══════════════════════
    component WorkOnPage: Page {
        id: wp
        property int rank: 1
        readonly property var wo: (root.rep.workOns || [])[rank - 1] || ({})
        readonly property bool dense: ((wo.causes || {}).rows || []).length >= 2
        readonly property real fs: dense ? 11.5 : 12.5
        key: "wo" + rank; section: "Work-on " + rank
        Column {
            width: parent.width; spacing: 0
            Kicker { text: "Work-on " + wp.rank + " of " + (root.rep.workOns || []).length + "  ·  " + (wp.wo.phase ? wp.wo.phase + ", " + wp.wo.phaseWords : "Over the whole session") + (wp.wo.phase ? "  ·  " + (wp.wo.count || "") : "") }
            Item { width: 1; height: 6 }
            H1 { width: parent.width; text: wp.wo.goal || "" }
            Item { width: 1; height: 5 }
            Small { visible: !!wp.wo.also || (wp.wo.name || "") !== (wp.wo.goal || ""); text: "Coaches call the habit <i>" + (wp.wo.name || "") + "</i>" + (wp.wo.also ? ", or " + wp.wo.also : "") + "."; color: root.ink3 }
            Item { width: 1; height: 18 }
            Row {
                width: parent.width; spacing: 24
                // stills: best vs furthest out, at the moment that matters
                Row {
                    id: pair
                    visible: !!wp.wo.stills
                    spacing: 10
                    Repeater {
                        model: wp.wo.stills || []
                        delegate: Still { required property var modelData; w: (wp.wo.stills || []).length > 1 ? 126 : (wp.dense ? 128 : 160); src: modelData.src; caption: modelData.caption; value: modelData.value; note: modelData.note || "" }
                    }
                }
                Column {
                    width: parent.width - (pair.visible ? pair.width + 24 : 0); spacing: 12
                    Para { label: "What we saw"; text: wp.wo.saw || ""; labelW: 0; width: parent.width; fs: wp.fs }
                    RunChart { visible: !!wp.wo.figure; width: parent.width; fig: wp.wo.figure || ({}); label: wp.wo.figureLabel || ""; plotH: wp.dense ? 80 : 92 }
                    Para { label: "Why it matters"; text: wp.wo.why || ""; labelW: 0; width: parent.width; fs: wp.fs }
                }
            }
            Item { width: 1; height: 12 }
            Rule { width: parent.width }
            Item { width: 1; height: 12 }
            Para { label: "What came with it"; text: wp.wo.cameWith || ""; width: parent.width; fs: wp.fs }
            Item { width: 1; height: wp.wo.cameWith ? (wp.dense ? 8 : 12) : 0 }
            Text { text: "What might be behind it"; font.family: root.sans; font.pointSize: 0.75 * (10.5); font.weight: Font.DemiBold; color: root.ink }
            Item { width: 1; height: 5 }
            CausesBlock { causes: wp.wo.causes || ({}); width: parent.width; fs: wp.dense ? 10.5 : 11 }
            Item { width: 1; height: 14 }
            H2 { text: "What to do" }
            Item { width: 1; height: 10 }
            // MOCK ONLY: the content gate (design §5) blocks a report whose work-on has nothing to practise
            Text { visible: !wp.wo.cue && !(wp.wo.drills || []).length && !wp.wo.check; width: parent.width; wrapMode: Text.WordWrap
                   text: "[Mock: the pack has no feel, drill or self-check for this habit yet. The content gate in design §5 would stop this report until they are written.]"
                   font.family: root.sans; font.pointSize: 0.75 * (11); font.italic: true; color: "#C0392B" }
            Para { label: "The feel"; text: wp.wo.cue ? "<i>" + wp.wo.cue + "</i>" : ""; width: parent.width; fs: wp.fs }
            Item { width: 1; height: 12 }
            Column {
                width: parent.width; spacing: 10
                DrillCard { fs: wp.fs; drill: (wp.wo.drills || [])[0] || ({}); also: (wp.wo.drills || []).slice(1, 2); width: root.tw; visible: (wp.wo.drills || []).length > 0 }
            }
            Item { width: 1; height: 14 }
            Para { label: "Check it yourself"; text: wp.wo.check || ""; width: parent.width; fs: wp.fs }
            Item { width: 1; height: 10 }
            Para { label: "Next time"; text: "<b>" + (wp.wo.target || "") + "</b>"; width: parent.width; fs: wp.fs }
        }
    }
    WorkOnPage { rank: 1 }
    WorkOnPage { rank: 2 }
    WorkOnPage { rank: 3 }

    // ═══════════════════════ 7–8 · work-ons 4 and 5, the same page ═══════════════════════
    WorkOnPage { rank: 4 }
    WorkOnPage { rank: 5 }

    // ═══════════════════════ 8 · how to practise + your numbers ═══════════════════════
    Page {
        key: "practice"; section: "Practice and numbers"
        Column {
            width: parent.width; spacing: 0
            Kicker { text: "Between now and next time" }
            Item { width: 1; height: 6 }
            H1 { width: parent.width; text: ((root.rep.practice || {}).title) || "" }
            Item { width: 1; height: 18 }
            Row {
                width: parent.width; spacing: 16
                Repeater {
                    model: (root.rep.practice || {}).steps || []
                    delegate: Column {
                        required property var modelData
                        required property int index
                        width: (root.tw - 3 * 16) / 4; spacing: 6
                        Text { text: index + 1; font.family: root.serif; font.pointSize: 0.75 * (26); color: root.accent }
                        H3 { width: parent.width; text: modelData.head }
                        Body { width: parent.width; text: modelData.body; font.pointSize: 0.75 * (11.5) }
                    }
                }
            }
            Item { width: 1; height: 34; visible: !!root.rep.numbers }
            Kicker { text: "Your numbers"; visible: !!root.rep.numbers }
            Item { width: 1; height: 6 }
            H2 { visible: !!root.rep.numbers; text: "Every swing, from the launch monitor"; font.pointSize: 0.75 * (22) }
            Item { width: 1; height: 14 }
            // the table
            Column {
                id: nt
                visible: !!root.rep.numbers
                width: root.tw
                readonly property var tab: root.rep.numbers || ({})
                readonly property var cols: tab.cols || []
                readonly property real c0: 42
                readonly property real cw: (width - c0 - 44) / Math.max(1, cols.length - 1)
                Row {
                    height: 30
                    Repeater {
                        model: nt.cols
                        delegate: Column {
                            required property var modelData
                            required property int index
                            width: index === 0 ? nt.c0 : nt.cw
                            Text { width: parent.width; horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight; text: modelData; font.family: root.sans; font.pointSize: 0.75 * (9.5); font.weight: Font.DemiBold; color: root.ink; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                            Text { width: parent.width; horizontalAlignment: Text.AlignRight; text: (nt.tab.units || [])[index] || ""; font.family: root.sans; font.pointSize: 0.75 * (8.5); color: root.ink3 }
                        }
                    }
                    Text { width: 44; horizontalAlignment: Text.AlignRight; text: "Habits"; font.family: root.sans; font.pointSize: 0.75 * (9.5); font.weight: Font.DemiBold; color: root.ink }
                }
                Rule { width: parent.width; color: root.ink; height: 1 }
                Repeater {
                    model: nt.tab.rows || []
                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: nt.width; height: 21
                        Rectangle { anchors.fill: parent; color: index % 2 ? Qt.alpha(root.ink, 0.03) : "transparent" }
                        Row {
                            y: 4
                            Repeater {
                                model: modelData.cells
                                delegate: Text {
                                    required property var modelData
                                    required property int index
                                    width: index === 0 ? nt.c0 : nt.cw
                                    horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                                    text: modelData; font.family: root.figs; font.pointSize: 0.75 * (10.5); color: root.ink
                                }
                            }
                            Text { width: 44; horizontalAlignment: Text.AlignRight; text: modelData.habits + " of 5"; font.family: root.figs; font.pointSize: 0.75 * (10.5); color: root.ink2 }
                        }
                    }
                }
                Rule { width: parent.width; color: root.ink; height: 1 }
                Row {
                    y: 4; height: 22
                    Repeater {
                        model: nt.tab.median || []
                        delegate: Text {
                            required property var modelData
                            required property int index
                            width: index === 0 ? nt.c0 : nt.cw; topPadding: 4
                            horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                            text: modelData; font.family: index === 0 ? root.sans : root.figs; font.pointSize: 0.75 * (10.5); font.weight: Font.DemiBold; color: root.ink
                        }
                    }
                }
                Item { width: 1; height: 8 }
                Micro { width: nt.width; text: (nt.tab.note || "") + " Typical: the middle value of your ten swings." }
            }
        }
    }

    // ═══════════════════════ 9 · about this report ═══════════════════════
    Page {
        key: "about"; section: "About this report"
        Column {
            width: parent.width; spacing: 0
            readonly property var ab: root.rep.about || ({})
            Kicker { text: "About this report" }
            Item { width: 1; height: 6 }
            H1 { width: parent.width; text: "How we measured, and how far to trust it" }
            Item { width: 1; height: 22 }
            Para { label: "Dates"; text: parent.ab.dates || ""; width: parent.width; labelW: 150 }
            Item { width: 1; height: 14 }
            Para { label: "How we measured"; text: parent.ab.measured || ""; width: parent.width; labelW: 150 }
            Item { width: 1; height: 14 }
            Para { label: "Target ranges"; text: parent.ab.ranges || ""; width: parent.width; labelW: 150 }
            Item { width: 1; height: 14 }
            Para { label: "What we could not check"; text: parent.ab.notChecked || ""; width: parent.width; labelW: 150 }
            Item { width: 1; height: 26 }
            H3 { text: "Words in this report" }
            Item { width: 1; height: 6 }
            Rule { width: parent.width }
            Repeater {
                model: parent.ab.words || []
                delegate: Item {
                    required property var modelData
                    width: root.tw; height: Math.max(wd.height, 22) + 8
                    Text { y: 5; width: 140; text: modelData[0]; font.family: root.sans; font.pointSize: 0.75 * (11.5); font.weight: Font.DemiBold; color: root.ink }
                    Body { id: wd; y: 5; x: 150; width: root.tw - 150; text: modelData[1]; font.pointSize: 0.75 * (11.5) }
                }
            }
            Item { width: 1; height: 40 }
            // sign-off
            Rectangle {
                width: root.tw; height: so.height + 40; color: Qt.alpha(root.accent, 0.06); radius: 3
                Row {
                    id: so
                    x: 20; y: 20; spacing: 16
                    Monogram { d: 48 }
                    Column {
                        spacing: 4; width: root.tw - 100
                        Text { text: root.brand.studio || ""; font.family: root.serif; font.pointSize: 0.75 * (18); color: root.ink }
                        Text { text: (root.brand.coach || "") + "  ·  " + (root.brand.coachTitle || ""); font.family: root.sans; font.pointSize: 0.75 * (11); color: root.ink2 }
                        Text { text: root.brand.contact || ""; font.family: root.sans; font.pointSize: 0.75 * (11); color: root.ink2 }
                        Item { width: 1; height: 4 }
                        Body { width: parent.width; text: root.brand.signoff || "" }
                    }
                }
            }
            Item { width: 1; height: 14 }
            Micro { width: parent.width; text: "Measured and prepared with PinPoint Studio. Swing images show only the golfer; video stays in the studio." }
        }
    }
}
