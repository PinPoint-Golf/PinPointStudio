// Fake AppLog (src/Gui/app/app_log.h) — the "appLog" context property. Records every line
// as "[<tag>] <text>" in `lines` (and `entries` with the level), so a case can read what the
// app log would have said. ⚠ Injected by every driver that builds a context map: production
// QML calls appLog unguarded, so a missing fake is a ReferenceError, not a silent skip.
import QtQuick

QtObject {
    property var lines: []
    property var entries: []
    // Echo to the test output (console.info) — off by default to keep runs quiet.
    property bool echo: false

    function _add(level, tag, text) {
        var line = "[" + tag + "] " + text
        lines.push(line)
        // `t`: wall clock (Date.now()), so a failing case can print the log as a timeline.
        entries.push({ level: level, tag: tag, text: text, line: line, t: Date.now() })
        if (echo) console.info("appLog " + level + " " + line)
    }
    function info(tag, text) { _add("info", tag, text) }
    function warn(tag, text) { _add("warn", tag, text) }

    // Lines whose text contains `needle` (and, optionally, carry `tag`).
    function matching(needle, tag) {
        var out = []
        for (var i = 0; i < entries.length; ++i)
            if ((!tag || entries[i].tag === tag) && entries[i].text.indexOf(needle) >= 0)
                out.push(entries[i].line)
        return out
    }
    function clear() { lines = []; entries = [] }
}
