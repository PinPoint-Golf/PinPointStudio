// END-TO-END: THE HOME SCREEN'S "YOUR SWING" AND THE SWING DIAGNOSTICS SCREEN UNDER IT.
//
// swing_themes_test proves the reduction, swing_themes_golden_test holds it to the Python
// reference, and work_ons_controller_test proves the catch-up writes and reads swing_themes.json
// and publishes the focus; none of them can see whether the home screen draws it. This drives the
// real app:
//
//   wait for the work-ons catch-up and the summary → log the focus, "what you do well", "next on
//   your list" and "what goes together" →
//   HOME: the focus card is drawn with its title, at least one aim, at least one "right now"
//   fault, and how to practise when it has a drill → "do well" has 1..3 items, each drawn →
//   "next" is drawn with as many delegates as it has items, the first (only) wearing NEXT → no
//   drawn text in YOUR SWING is technical (no P-position, no degrees, no fault's technical name)
//   or causal (because / cause / due to / leads to) → the work-ons are no longer on the home
//   screen →
//   click "Swing diagnostics →": the screen is drawn, the home is not, its FAULTS card draws a row
//   per work-on (five at most until "more"), and "what goes together" draws an item each with its
//   first half and where it starts, or its note → no causal words there either →
//   click "← Home": the home is back.
//
// Point the app at a folder of sessions that is NOT the library (PINPOINT_WORKONS_DIR), so the
// catch-up writes its work_ons.json and swing_themes.json files there:
//   PINPOINT_WORKONS_DIR=<athlete dir> QT_QPA_PLATFORM=offscreen PinPointStudio \
//       --probe-qml <abs path to this file> [--probe-png <abs path>] [--probe-png-section <abs path>]
//       [--probe-png-diagnostics <abs path>]
// --probe-png grabs the whole home column (YOUR SWING above DEVICES, as the golfer sees it);
// --probe-png-section the summary alone; --probe-png-diagnostics the Swing diagnostics column.
// Each on the theme's own background. Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.
// build/run-me/verify-swing-themes.sh wraps it.
//
// The Mac app is a Debug build: the reduction takes ~15 s there, so this waits up to 4 minutes.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string png:            arg("--probe-png", "")
    readonly property string pngSection:     arg("--probe-png-section", "")
    readonly property string pngDiagnostics: arg("--probe-png-diagnostics", "")

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function find(item, name) { const out = []; walk(item, name, out); return out.length ? out[0] : null }
    function all(item, name) { const out = []; walk(item, name, out); return out }
    function rootItem() { let t = probe; while (t.parent) t = t.parent; return t }
    function drawn(item) {
        for (let t = item; t; t = t.parent) if (!t.visible) return false
        return item.width > 0 && item.height > 0
    }
    // Every piece of text drawn under an item: each visible Text's own words.
    function drawnTexts(item, out) {
        if (!item || !item.visible) return out
        if (typeof item.text === "string" && item.text !== "" && item.width > 0 && item.height > 0
                && item.font !== undefined)
            out.push(item.text)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) drawnTexts(kids[i], out)
        return out
    }
    function saysCause(s) { return /because|cause|due to|leads to/i.test(s) }
    // A golfer reads the home screen: no P-position, no angle in degrees, no fault's technical name.
    function technical(s, names) {
        if (/\bP(10|[1-9])\b/.test(s)) return "a P-position"
        if (/\d\s*°/.test(s)) return "degrees"
        for (const n of names)
            if (n !== "" && new RegExp("\\b" + n.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + "\\b").test(s))
                return "the fault name '" + n + "'"
        return ""
    }

    property int waited: 0
    property double startedMs: Date.now()
    Timer {
        interval: 250; running: true; repeat: true
        onTriggered: {
            probe.waited += 1
            const done = workOns.athleteDir !== "" && !workOns.updating && workOns.sessionsFound > 0
                         && workOns.summaryReady && !workOns.summaryUpdating
            if (!done && probe.waited < 960) return      // 4 minutes
            running = false
            probe.inspectHome()
        }
    }

    // ── The home screen ──────────────────────────────────────────────────────────────────
    property var summary: null
    function inspectHome() {
        log("athleteDir " + workOns.athleteDir + " · settled after " + (Date.now() - startedMs) + " ms")
        check(!workOns.updating && workOns.sessionCount === workOns.sessionsFound && workOns.sessionsFound > 0,
              "the work-ons catch-up finished (" + workOns.sessionCount + " of " + workOns.sessionsFound + " sessions)")
        check(workOns.summaryReady && !workOns.summaryUpdating, "the swing summary is ready")
        log("reduced from " + workOns.summarySwings + " swings over " + workOns.summarySessions + " sessions")

        const focus = workOns.focusItem || {}
        const well = workOns.doWellItems, next = workOns.nextItems
        const together = workOns.togetherItems, note = workOns.togetherNote
        const pips = (a) => (a || []).map(b => b ? "●" : "○").join("")
        const fault = (f) => f.text + " — " + f.frequency + " (" + f.share.toFixed(3)
                             + (f.trend > 0 ? ", growing" : f.trend < 0 ? ", easing" : "") + ")  " + pips(f.sessions)
        log(workOns.summarySubtitle)
        if (focus.present) {
            log("Your focus: " + focus.title)
            for (const a of focus.aimFor || []) log("  aim for   ✓ " + a)
            for (const f of focus.rightNow || []) log("  right now ◎ " + fault(f))
            if (focus.why !== "") log("  why: " + focus.why)
            if (focus.practise !== "") log("  practise (" + focus.practiseLabel + "): " + focus.practise)
            log("  " + focus.reason)
        } else {
            log("Your focus: none")
        }
        log("What you do well")
        for (let i = 0; i < well.length; ++i)
            log("  ✓ " + well[i].text + " — " + well[i].caption + "  " + pips(well[i].sessions))
        log("Next on your list")
        for (let i = 0; i < next.length; ++i) log("  ◎ " + fault(next[i]))
        log("What goes together")
        for (let i = 0; i < together.length; ++i) {
            const t = together[i]
            log("  [" + t.tier + "] " + t.first + (t.second !== "" ? " / " + t.second : "")
                + (t.startWords !== "" ? " — " + t.startWords + " (stop " + t.startStop + ")" : " — unplaced")
                + (t.trend > 0 ? ", may be growing" : t.trend < 0 ? ", may be easing" : ""))
        }
        if (note !== "") log("  note: " + note)

        summary = find(rootItem(), "swingSummary")
        check(summary !== null && drawn(summary), "the home screen draws the summary")
        if (!summary) { finish(); return }

        // ── The focus ──
        check(focus.present === true, "there is a focus (Mark's data has faults)")
        const card = find(summary, "focusCard")
        check(card !== null && drawn(card), "the focus card is drawn")
        const title = card ? find(card, "focusTitle") : null
        check(title !== null && drawn(title) && title.text !== "" && title.text === focus.title,
              "the focus is titled: " + (title ? title.text : "(none)"))
        const aims = card ? all(card, "focusAim") : [], nows = card ? all(card, "focusNow") : []
        check(aims.length >= 1 && aims.length === (focus.aimFor || []).length && aims.every(drawn),
              "every 'aim for' line is drawn, at least one (" + aims.length + " of " + (focus.aimFor || []).length + ")")
        check(nows.length >= 1 && nows.length === (focus.rightNow || []).length && nows.every(drawn),
              "every 'right now' fault is drawn, at least one (" + nows.length + " of " + (focus.rightNow || []).length + ")")
        const practise = card ? find(card, "focusPractise") : null
        if (focus.practise !== "")
            check(practise !== null && drawn(practise) && practise.text === focus.practise,
                  "the focus has a drill, and how to practise it is drawn")
        else
            check(practise === null || !drawn(practise), "the focus has no drill, and no 'how to practise' is drawn")

        // ── What you do well | next on your list ──
        const wellItems = all(summary, "doWellItem"), nextItems = all(summary, "nextItem")
        check(well.length >= 1 && well.length <= 3, "what you do well has 1 to 3 items (" + well.length + ")")
        check(wellItems.length === well.length && wellItems.every(drawn),
              "every 'do well' item is drawn (" + wellItems.length + " of " + well.length + ")")
        check(next.length <= 4 && nextItems.length === next.length && nextItems.every(drawn),
              "every 'next on your list' item is drawn, four at most (" + nextItems.length + " of " + next.length + ")")
        if (nextItems.length > 0) {
            const chips = nextItems.map(it => { const c = find(it, "nextChip"); return c !== null && drawn(c) })
            check(chips[0] && chips.slice(1).every(c => !c), "the first on the list, and only it, wears NEXT")
        }

        // ── A golfer's words ──
        const names = (workOns.items || []).map(it => it.name || "")
        const texts = drawnTexts(summary, [])
        const tech = texts.map(t => [t, technical(t, names)]).filter(p => p[1] !== "")
        check(texts.length > 0 && tech.length === 0, "no drawn text in YOUR SWING is technical (" + texts.length + " texts)"
              + (tech.length ? (": " + tech.map(p => "'" + p[0] + "' has " + p[1]).join(" | ")) : ""))
        const causal = texts.filter(saysCause)
        check(causal.length === 0, "no drawn text in YOUR SWING says because / cause / due to / leads to"
              + (causal.length ? (": " + causal.join(" | ")) : ""))

        // ── The faults moved off the home screen ──
        const column = summary.parent
        check(column !== null && all(column, "workOnsEmpty").length === 0 && all(column, "workOnRow").length === 0,
              "the home column no longer holds the work-ons")
        const link = find(summary, "diagnosticsLink")
        check(link !== null && drawn(link), "the 'Swing diagnostics →' link is drawn")

        const shots = []
        if (png !== "" && column) shots.push({ item: column, path: png })
        if (pngSection !== "") shots.push({ item: summary, path: pngSection })
        grabNext(shots, () => {
            if (!link) { finish(); return }
            link.clicked(null)
            after(600, inspectDiagnostics)
        })
    }

    // ── Swing diagnostics ────────────────────────────────────────────────────────────────
    function inspectDiagnostics() {
        const screen = find(rootItem(), "swingDiagnosticsScreen")
        check(screen !== null && drawn(screen), "the link opens the Swing diagnostics screen")
        check(!drawn(summary), "…in place of the home screen")
        if (!screen) { finish(); return }

        check(screen.faults && screen.faults.title === "FAULTS", "its faults card is headed FAULTS")
        const items = workOns.items || []
        const rows = all(screen, "workOnRow")
        check(rows.length >= 1 && rows.length === Math.min(items.length, 5) && rows.every(drawn),
              "FAULTS draws a row per work-on, five at most until 'more' (" + rows.length + " of " + items.length + ")")

        const together = workOns.togetherItems
        const card = find(screen, "goesTogether")
        check(card !== null && drawn(card), "the 'what goes together' card is drawn")
        const togetherItems = card ? all(card, "togetherItem") : [], noteItems = card ? all(card, "togetherNote") : []
        check(togetherItems.length === together.length && togetherItems.every(drawn),
              "every 'goes together' item is drawn (" + togetherItems.length + " of " + together.length + ")")
        const noteDrawn = noteItems.length === 1 && drawn(noteItems[0])
        check(noteItems.length === 1 && noteDrawn === (together.length === 0),
              together.length === 0 ? "the note is drawn" : "no note is drawn beside the items")
        const whole = togetherItems.filter(t => t.firstText !== "" && t.startWordsText !== "")
        check(togetherItems.length === 0 ? noteDrawn : whole.length === togetherItems.length,
              "every 'goes together' item has its first half and where it starts, or the note is drawn ("
              + whole.length + " of " + togetherItems.length + ")")
        const causal = drawnTexts(card, []).filter(saysCause)
        check(causal.length === 0, "nothing in 'what goes together' says because / cause / due to / leads to"
              + (causal.length ? (": " + causal.join(" | ")) : ""))

        const back = find(screen, "diagnosticsBack")
        check(back !== null && drawn(back), "'← Home' is drawn")
        const shots = []
        if (pngDiagnostics !== "" && card) shots.push({ item: card.parent, path: pngDiagnostics })
        grabNext(shots, () => {
            if (!back) { finish(); return }
            back.clicked(null)
            after(600, () => {
                check(drawn(summary) && !drawn(screen) && navController.currentIndex === 0,
                      "'← Home' returns to the home screen")
                finish()
            })
        })
    }

    // Runs `fn` once, `ms` from now.
    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms
        t.triggered.connect(() => { t.destroy(); fn() })
        t.start()
    }

    // The item is grabbed on its own (grabToImage renders the whole subtree, so the clipped
    // Flickable it sits in and the window's height do not cut it), then that image is laid over the
    // theme's background, off to the side of the window, and grabbed again: on its own the column
    // is transparent — text on nothing. (A ShaderEffectSource over the live item smeared the small
    // rounded shapes — the meter, the pips, the badges.) `then` runs once every shot is saved.
    property var grabResult: null       // kept alive while its url is shown
    function grabNext(shots, then) {
        if (shots.length === 0) { then(); return }
        const s = shots.shift()
        s.item.grabToImage(function (shot) {
            probe.grabResult = shot
            const pad = Theme.sp(24)
            const frame = Qt.createQmlObject(
                'import QtQuick; Rectangle { property alias source: img.source; '
                + 'readonly property bool ready: img.status === Image.Ready; '
                + 'Image { id: img; x: ' + pad + '; y: ' + pad + '; cache: false } }', probe)
            frame.color  = Theme.colorBg
            frame.width  = s.item.width + 2 * pad
            frame.height = s.item.height + 2 * pad
            frame.x      = -frame.width - 100
            frame.source = shot.url
            settle.frame = frame
            settle.shot  = s
            settle.rest  = shots
            settle.then  = then
            settle.tries = 0
            settle.start()
        })
    }
    Timer {
        id: settle
        property var frame
        property var shot
        property var rest
        property var then
        property int tries: 0
        interval: 200
        onTriggered: {
            const f = frame, s = shot, r = rest, k = then
            if (!f.ready && ++tries < 25) { start(); return }
            f.grabToImage(function (img) {
                log("image " + (img.saveToFile(s.path) ? s.path : "NOT SAVED")
                    + " (" + Math.round(f.width) + "×" + Math.round(f.height) + ")")
                f.destroy()
                probe.grabResult = null
                probe.grabNext(r, k)
            })
        }
    }
}
