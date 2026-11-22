import QtQuick
import QtQuick.Controls
import PinPointStudio

// Dumps the diagnostics panel's surface for one session as "DUMP:<key>:<json>" lines (session report mock;
// tools/session-report-mock/README.md). dump-session.sh turns the log into the JSON assemble.py reads. The panel's
// model is the session owner's thin adapter (SessionDiagnosticsModel::adapterFor, refactor 3.7-a) when the session is
// held by its Athlete, which dump-session.sh arranges; a model that is not one (the old engine, a folder no Session
// holds) is refused, so a dump is always the owner's (refactor 5.3).
Item {
    id: probe
    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string sessionDir: arg("--probe-session", "")
    function log(s) { console.warn("PROBE: " + s) }
    function dump(k, v) { console.warn("DUMP:" + k + ":" + JSON.stringify(v)) }
    function walk(item, name, out) { if (!item) return; if (item.objectName === name) out.push(item); const kids = item.children || []; for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out) }
    function after(ms, fn) { const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe); t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start() }
    Instantiator { id: shots; model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir } }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            const seen = new Set()
            function mute(o) { if (!o || seen.has(o)) return; seen.add(o); if (String(o).indexOf("TingPlayer") === 0) { o.volume = 0; o.frequency = 1 } const ds = o.data || []; for (let i = 0; i < ds.length; ++i) mute(ds[i]) }
            mute(probe.Window.window.contentItem)
            for (const k of ["camera", "swing3d", "launchMonitor", "charts", "table", "markup", "trust"]) ViewLayout.setPanel(app.state.analyseMode, k, false)
            ViewLayout.setPanel(app.state.analyseMode, "sessionDiagnostics", true)
            ViewLayout.setArrangement(app.state.analyseMode, "tabs")
            navController.navigate(AppScreen.Session)
            app.state.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            probe.after(5000, () => {
                const o = shots.objectAt(0)
                app.state.enterReplay(sessionReviewController.activeShots.shotRefOf(o.shotId), o.swingDir)
                probe.after(3000, () => {
                    app.state.enterAnalyse()
                    probe.after(6000, () => {
                        const bs = []; probe.walk(probe.Window.window.contentItem, "sdBody", bs)
                        if (!bs.length) { probe.log("no sdBody"); Qt.exit(1); return }
                        const m = bs[0].source
                        if (!m || !m.adapter) { probe.log("the panel's model is not the session owner's adapter (is the session held?)"); Qt.exit(2); return }
                        probe.log("the panel's model is the session owner's adapter")
                        const shotList = []
                        for (let i = 0; i < shots.count; ++i) { const s = shots.objectAt(i); shotList.push({ shotId: s.shotId, ordinal: s.ordinal, swingDir: s.swingDir }) }
                        probe.dump("shots", shotList)
                        for (const k of ["stage", "shotCount", "patternCount", "headerInfo", "cards", "watching", "chains", "unchained", "unchainedLine", "driver", "coverageLine", "notJudged", "notJudgedLine", "bookends", "expectations", "declaredMiss", "focusConditionId"]) probe.dump(k, m[k])
                        probe.dump("sequenceConsistency", m.sequenceConsistency())
                        probe.dump("sequenceRanking", m.sequenceRanking())
                        const ids = (m.cards || []).map(c => c.id).concat((m.watching || []).map(c => c.id))
                        for (const id of ids) { probe.dump("detail." + id, m.conditionDetail(id)); probe.dump("spread." + id, m.spreadOf(id)) }
                        for (const id of (m.cards || []).map(c => c.id)) m.historyOf(id)
                        probe.after(6000, () => {
                            for (const id of (m.cards || []).map(c => c.id)) probe.dump("history." + id, m.historyOf(id))
                            const sh = []; for (let i = 0; i < shots.count; ++i) { const s = shots.objectAt(i); const sid = m.shotIdForSwingDir(s.swingDir); sh.push({ swingDir: s.swingDir, readout: m.shotReadout(sid), seq: m.sequenceChecks(sid) }) }
                            probe.dump("readouts", sh)
                            probe.log("done"); Qt.exit(0)
                        })
                    })
                })
            })
        }
    }
}
