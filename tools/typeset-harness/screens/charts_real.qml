// The REAL Charts panel, checked: a pass-through overlay for `try.sh charts` and its self-test. It draws
// nothing and leaves the real panel (PpChartsPanel → chBody) showing, so the try-out IS the app's panel —
// and it answers the self-test's questions about what that panel shows (try_overlay.qml's selfCheck /
// selfOpen hooks):
//   selfCheck(tab)   "" when the body's register is showing that tab's view on screen (Replay has no Kinds
//                    form: a Kinds tab reads Session there), else what is wrong
//   selfOpen()       the charts analogue of the diagnostics detail: to This shot, then the first row of
//                    "Apart from the session" opened IN PLACE through the body's own openReading()
//   selfCheck("open") / selfClose() / selfCheck("closed")
//   try.sh --selftest charts tools/typeset-harness/screens/charts_real.qml
import QtQuick
Item {
    id: ov
    property bool passThrough: true
    property var body: null
    property string view
    readonly property string register: body ? String(body.register) : ""

    function walk(item, name, out) {
        if (!item) return out
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) { if (kids[i].objectName === name) out.push(kids[i]); walk(kids[i], name, out) }
        return out
    }
    function onScreen(item) {
        let i = item
        while (i && i !== body) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function shown(name) { return walk(body, name, []).filter(onScreen) }
    readonly property var viewNames: ({ shot: "chShotView", session: "chSessionView", relations: "chRelationsView", kinds: "chKindsView" })

    property string openedId: ""
    function selfCheck(what) {
        if (!body) return "no body"
        if (what === "open") {
            // the row opens in the Apart list; the same reading also opens where "Every reading against the
            // session" lists it (one openId per panel), so count the Apart list's own
            const lists = shown("chApartList")
            const rows = lists.length ? walk(lists[0], "chApartRowOpen", []).filter(onScreen) : []
            if (body.openId === "" || body.openId !== openedId) return "openId is '" + body.openId + "'"
            if (rows.length !== 1) return rows.length + " open rows in the Apart list"
            if (rows[0].readingId !== openedId) return "the open row is " + rows[0].readingId
            if (walk(rows[0], "chReadingTrace", []).length + walk(rows[0], "chRun", []).length < 1) return "the open row has no curve and no run"
            return ""
        }
        if (what === "closed") return body.openId === "" && shown("chApartRowOpen").length === 0 ? "" : "still open (" + body.openId + ")"
        const reg = body.registerItem
        if (!reg) return "no register loaded"
        const want = body.register === "Replay" && what === "kinds" ? "session" : what
        if (reg.shown !== want) return "the register shows " + reg.shown + ", not " + want
        const v = shown(viewNames[want])
        if (v.length !== 1) return viewNames[want] + " on screen: " + v.length
        if (v[0].height <= 0) return viewNames[want] + " is empty"
        return ""
    }
    function selfOpen() {
        if (!body) return ""
        body.tab = "shot"
        openLater.start()
        return "an Apart row"
    }
    Timer {
        id: openLater; interval: 1500
        onTriggered: {
            const rows = ov.shown("chApartRow")
            ov.openedId = rows.length ? rows[0].readingId : ""
            if (ov.openedId !== "") ov.body.openReading(ov.openedId)
        }
    }
    function selfClose() { if (body && openedId !== "" && body.openId === openedId) body.openReading(openedId) }
}
