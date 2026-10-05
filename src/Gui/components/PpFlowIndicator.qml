/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// PpFlowIndicator — the progress indicator of a multi-step flow (session_wizard_refactor_design.md
// §4.9). It replaces the session wizard's tab strip, and is generic: it knows nothing of session
// setup, so the toolbar calibration and the trunk ceremony can reuse it (`compact`).
//
//   SESSION      CAMERAS                      SENSORS                              READY
//   ●───────────●──────────●──────────◉━━━━━━━○──────────○──────────○──────────○
//   Goals ✓     Cameras ✓  Ball ⚠      IMUs       Calibrate   Check       Ready
//   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━░░░░░░░░░░░░░░░░░░░░░░░░░░  3 of 7 done
//
// ── Model ─────────────────────────────────────────────────────────────────────────────────────
//   model    [{ key, label, group, state, current, attention, number, tip }] in flow order —
//            state "pending" | "done" | "skipped"; `current` marks the step on screen; `attention`
//            an amber ring (a visited step that needs attention); `number` the 1-based display
//            number the page eyebrow also shows; `tip` the hover text ("" for none).
//   groups   group → label ("Cameras"); drawn capitalised over each group's run.
//   progress { passed, total } — a skipped step counts as passed ("3 of 7 done").
//   canGoTo  function(key) → bool, or null; a done or skipped pip is clickable when it says yes.
//   jumpRequested(key)  emitted on such a click. Future and current pips are inert (D5).
//
// ── Behaviour ─────────────────────────────────────────────────────────────────────────────────
// - Steps that appear or disappear grow or shrink IN PLACE and never reorder: the pips are a
//   ListModel kept in step with `model` by key (insert at its place, mark leaving, remove once
//   shrunk), so a pip that stays is the same object throughout (P2).
// - Narrow widths: once the row no longer fits at full cell width, every group other than the
//   current one collapses to a single pip with a count ("Cameras 3/3"); and the cells shrink to
//   fit whatever is left, labels eliding — nothing escapes the strip (P4).
// - Accessibility: each pip is `Accessible.name: "<label>, <state>"`.
// Palette and type are the design system's tokens; the pip colours are the old tab strip's.
import QtQuick
import PinPointStudio

Item {
    id: root

    // ── Inputs ───────────────────────────────────────────────────────────────────────────────────
    property var  model:    []
    property var  groups:   ({})
    property var  progress: ({ passed: 0, total: 0 })
    property var  canGoTo:  null
    property bool compact:  false

    signal jumpRequested(string key)

    // ── Geometry ─────────────────────────────────────────────────────────────────────────────────
    readonly property real cellWidth:   compact ? Theme.sp(56) : Theme.sp(84)
    readonly property real pipSize:     compact ? Theme.sp(12) : Theme.sp(16)
    readonly property real groupHeight: compact ? 0 : Theme.sp(14)
    readonly property real labelHeight: Theme.sp(14)
    readonly property real barHeight:   Theme.sp(14)
    readonly property real _gap:        Theme.sp(4)
    readonly property real _pipTop:     groupHeight > 0 ? groupHeight + _gap : 0
    readonly property real _rowHeight:  _pipTop + pipSize + _gap + labelHeight

    implicitWidth:  model.length * cellWidth
    implicitHeight: _rowHeight + Theme.sp(8) + barHeight

    // Too narrow for every pip at full width: collapse the groups that are not current.
    readonly property bool collapsed: width > 0 && width < model.length * cellWidth

    // ── Derived from the model ───────────────────────────────────────────────────────────────────
    readonly property var _byKey: {
        var out = {}, m = root.model
        for (var i = 0; i < m.length; ++i) out[m[i].key] = m[i]
        return out
    }
    readonly property string _currentGroup: {
        var m = root.model
        for (var i = 0; i < m.length; ++i) if (m[i].current) return m[i].group
        return ""
    }
    // group → { head, count, passed, skipped, attention, lastVisited, firstPending, keys }
    readonly property var _groupStats: {
        var out = {}, m = root.model
        for (var i = 0; i < m.length; ++i) {
            var e = m[i], g = out[e.group]
            if (g === undefined) {
                g = { head: e.key, count: 0, passed: 0, skipped: 0, attention: false,
                      firstPending: -1, keys: [] }
                out[e.group] = g
            }
            g.count++
            g.keys.push(e.key)
            if (e.state === "done" || e.state === "skipped") g.passed++
            if (e.state === "skipped") g.skipped++
            if (e.attention) g.attention = true
            if (g.firstPending < 0 && e.state !== "done" && e.state !== "skipped") g.firstPending = e.number
        }
        return out
    }
    // How many cells are on show once collapsing has been applied (the target, not the animation).
    readonly property int _shownTarget: {
        if (!root.collapsed) return root.model.length
        var n = 0, gs = root._groupStats
        for (var g in gs) n += (g === root._currentGroup) ? gs[g].count : 1
        return n
    }
    readonly property real _cellW: Math.max(1, Math.min(cellWidth, width / Math.max(1, _shownTarget)))
    readonly property string _firstKey: model.length > 0 ? model[0].key : ""

    // A collapsed group's pip jumps to the latest visited step of the group that may be jumped to.
    // DECISION(stage5a): the latest, so a click goes back as little as possible.
    function _groupJumpKey(group) {
        var g = _groupStats[group]
        if (g === undefined) return ""
        for (var i = g.keys.length - 1; i >= 0; --i) {
            var e = _byKey[g.keys[i]]
            if ((e.state === "done" || e.state === "skipped") && _allowed(e.key)) return e.key
        }
        return ""
    }
    function _allowed(key) { return canGoTo ? canGoTo(key) === true : true }

    function _stateName(e) {
        if (!e) return "pending"
        return e.current ? "current" : (e.state === "done" || e.state === "skipped" ? e.state : "pending")
    }
    function _stateText(name) {
        if (name === "done")    return qsTr("done")
        if (name === "skipped") return qsTr("skipped")
        if (name === "current") return qsTr("current")
        return qsTr("pending")
    }

    // ── The pip list, kept in step with `model` by key ──────────────────────────────────────────
    ListModel { id: cells }

    onModelChanged: _sync()
    Component.onCompleted: _sync()

    function _sync() {
        var keys = [], set = {}, m = root.model
        for (var i = 0; i < m.length; ++i) { keys.push(m[i].key); set[m[i].key] = true }
        // Leaving: marked, shrunk by the delegate, removed by _purge() once at zero.
        for (var c = 0; c < cells.count; ++c)
            if (cells.get(c).present && !set[cells.get(c).key]) cells.setProperty(c, "present", false)
        // Arriving: inserted right after the previous key of the model, so nothing moves.
        var j = 0
        for (var n = 0; n < keys.length; ++n) {
            var at = -1
            for (var k = 0; k < cells.count; ++k) if (cells.get(k).key === keys[n]) { at = k; break }
            if (at >= 0) {
                if (!cells.get(at).present) cells.setProperty(at, "present", true)   // came back mid-exit
                j = at + 1
                continue
            }
            cells.insert(j, { key: keys[n], present: true })
            ++j
        }
    }
    function _purge() {
        for (var i = cells.count - 1; i >= 0; --i) {
            if (cells.get(i).present) continue
            var it = pipRepeater.itemAt(i)
            if (it === null || it.grow <= 0) cells.remove(i)
        }
    }

    // ── The strip ────────────────────────────────────────────────────────────────────────────────
    Row {
        id: pipRow
        x: Math.max(0, Math.round((root.width - width) / 2))
        height: root._rowHeight
        spacing: 0

        Repeater {
            id: pipRepeater
            model: cells

            delegate: Item {
                id: cell
                required property string key
                required property bool   present
                required property int    index

                objectName: "flowPip"
                readonly property string stepKey: key
                readonly property var  _entry: root._byKey[key] !== undefined ? root._byKey[key] : null
                // A leaving pip keeps drawing what it last was while it shrinks.
                property var _last: null
                on_EntryChanged: if (_entry !== null) _last = _entry
                readonly property var  step: _entry !== null ? _entry : _last

                readonly property string group:  step ? step.group : ""
                readonly property var    _gs:    root._groupStats[group]
                readonly property bool   _groupHead: _gs !== undefined && _gs.head === key
                // Collapsed: this cell stands for its whole group.
                readonly property bool   asGroup: root.collapsed && group !== root._currentGroup && _groupHead
                readonly property bool   _collapsedAway: root.collapsed && group !== root._currentGroup && !_groupHead
                readonly property bool   shown: present && _entry !== null && !_collapsedAway

                readonly property string stateName: {
                    if (!asGroup) return root._stateName(step)
                    if (_gs.passed < _gs.count) return "pending"
                    return _gs.skipped > 0 ? "skipped" : "done"
                }
                readonly property string glyph: {
                    if (stateName === "done")    return "✓"
                    if (stateName === "skipped") return "⚠"
                    if (asGroup) return _gs.firstPending > 0 ? String(_gs.firstPending) : ""
                    return step ? String(step.number) : ""
                }
                readonly property string labelText: asGroup
                    ? qsTr("%1 %2/%3").arg(root.groups[group] !== undefined ? root.groups[group] : group)
                                      .arg(_gs.passed).arg(_gs.count)
                    : (step ? step.label : "")
                readonly property bool attention: asGroup ? (_gs !== undefined && _gs.attention)
                                                          : (step !== null && step.attention === true)
                readonly property string tipText: asGroup ? "" : (step && step.tip ? step.tip : "")
                readonly property string jumpKey: asGroup ? root._groupJumpKey(group)
                                                          : ((stateName === "done" || stateName === "skipped") && root._allowed(key) ? key : "")
                readonly property bool clickable: shown && jumpKey !== ""
                readonly property bool hovered: hit.containsMouse

                // Appear/disappear in place: the cell's width and opacity follow `grow`.
                property bool _born: false
                Component.onCompleted: { _last = _entry; _born = true }
                property real grow: shown && _born ? 1 : 0
                Behavior on grow { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
                onGrowChanged: if (grow <= 0 && !present) Qt.callLater(root._purge)

                width:   root._cellW * grow
                height:  root._rowHeight
                opacity: grow
                visible: grow > 0
                z:       hovered ? 2 : 1

                // Group label over its run (the run's width, left-aligned on the first pip).
                Text {
                    objectName: "flowGroupLabel"
                    visible: root.groupHeight > 0 && cell._groupHead && !cell.asGroup
                    x: Math.round((cell.width - root.pipSize) / 2)
                    // Collapsed, only the current group keeps its label, and its run is whole.
                    width: Math.max(0, (cell._gs !== undefined ? cell._gs.count : 1) * root._cellW - x)
                    height: root.groupHeight
                    text: root.groups[cell.group] !== undefined ? root.groups[cell.group] : cell.group
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    font.capitalization: Font.AllUppercase
                    color: Theme.colorText3
                    elide: Text.ElideRight
                }

                // Connector halves: left = the segment from the previous pip, right = to the next.
                // A segment is green once the earlier step of the pair is done (the old strip's rule).
                Rectangle {
                    readonly property var _prev: cell.step && cell.step.number > 1
                                                 ? root.model[cell.step.number - 2] : undefined
                    visible: cell.key !== root._firstKey
                    x: 0
                    width: Math.max(0, (cell.width - root.pipSize) / 2)
                    y: root._pipTop + root.pipSize / 2
                    height: 1
                    color: _prev !== undefined && _prev.state === "done" ? Theme.colorGood : Theme.colorBorderMid
                    Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
                }
                Rectangle {
                    readonly property bool _last: cell.step !== null && cell.step.number >= root.model.length
                    visible: !_last
                    x: Math.round((cell.width + root.pipSize) / 2)
                    width: Math.max(0, cell.width - x)
                    y: root._pipTop + root.pipSize / 2
                    height: 1
                    color: cell.step && cell.step.state === "done" ? Theme.colorGood : Theme.colorBorderMid
                    Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
                }

                // Attention: an amber ring around the pip.
                Rectangle {
                    objectName: "flowPipAttention"
                    visible: cell.attention
                    width:  root.pipSize + Theme.sp(6)
                    height: width
                    radius: width / 2
                    x: Math.round((cell.width - width) / 2)
                    y: root._pipTop - Theme.sp(3)
                    color: "transparent"
                    border.width: Math.max(1, Theme.sp(1.5))
                    border.color: Theme.colorWarn
                }

                Rectangle {
                    id: pip
                    objectName: "flowPipDot"
                    width: root.pipSize; height: root.pipSize
                    radius: root.pipSize / 2
                    x: Math.round((cell.width - width) / 2)
                    y: root._pipTop
                    color: {
                        var s = cell.stateName
                        if (s === "current") return Theme.colorAccent
                        if (s === "done")    return Theme.colorGoodLight
                        if (s === "skipped") return Theme.colorWarnLight
                        return Theme.colorBg3
                    }
                    border.width: 1
                    border.color: {
                        var s = cell.stateName
                        if (s === "current") return Theme.colorAccent
                        if (s === "done")    return Theme.colorGood
                        if (s === "skipped") return Theme.colorWarn
                        return Theme.colorBorderMid
                    }
                    Behavior on color        { ColorAnimation { duration: Theme.durationNormal } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationNormal } }

                    Text {
                        objectName: "flowPipGlyph"
                        anchors.centerIn: parent
                        text: cell.glyph
                        font.family:    Theme.fontData
                        font.pixelSize: root.compact ? Theme.sp(8) : Theme.fontSzMicro
                        color: {
                            var s = cell.stateName
                            if (s === "current") return Theme.dark ? Theme.colorBg : "#FFFFFF"
                            if (s === "done")    return Theme.colorGood
                            if (s === "skipped") return Theme.colorWarn
                            return Theme.colorText3
                        }
                    }
                }

                Text {
                    objectName: "flowPipLabel"
                    x: 0
                    y: root._pipTop + root.pipSize + root._gap
                    width: cell.width
                    height: root.labelHeight
                    horizontalAlignment: Text.AlignHCenter
                    text: cell.labelText
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzMicro
                    elide: Text.ElideRight
                    color: {
                        var s = cell.stateName
                        if (s === "current") return Theme.colorAccent
                        if (s === "done")    return Theme.colorGood
                        if (s === "skipped") return Theme.colorWarn
                        return Theme.colorText3
                    }
                }

                MouseArea {
                    id: hit
                    x: 0
                    y: root._pipTop - Theme.sp(3)
                    width: cell.width
                    height: root._rowHeight - y
                    hoverEnabled: true
                    enabled: cell.shown
                    cursorShape: cell.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (cell.clickable) root.jumpRequested(cell.jumpKey)

                    Accessible.role: Accessible.Button
                    Accessible.name: cell.labelText + ", " + root._stateText(cell.stateName)
                    Accessible.description: cell.tipText
                    Accessible.onPressAction: if (cell.clickable) root.jumpRequested(cell.jumpKey)
                }

                // Hover: the step's hint or first issue, below the pip, kept inside the strip's width.
                Rectangle {
                    id: tip
                    objectName: "flowPipTip"
                    readonly property real _w: Math.min(tipText.implicitWidth + Theme.sp(16), Theme.sp(320),
                                                        Math.max(Theme.sp(40), root.width))
                    readonly property real _cellX: pipRow.x + cell.x
                    visible: cell.hovered && cell.tipText !== "" && cell.shown
                    width: _w
                    height: tipText.implicitHeight + Theme.sp(10)
                    x: Math.max(-_cellX, Math.min((cell.width - _w) / 2, root.width - _cellX - _w))
                    y: root._rowHeight + Theme.sp(2)
                    radius: Theme.radius
                    color: Theme.colorBg2
                    border.width: 1
                    border.color: cell.attention ? Theme.colorWarn : Theme.colorBorderMid

                    Text {
                        id: tipText
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                  leftMargin: Theme.sp(8); rightMargin: Theme.sp(8) }
                        text: cell.tipText
                        wrapMode: Text.WordWrap
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzMicro
                        color: Theme.colorText2
                    }
                }
            }
        }
    }

    // ── Progress: a thin bar and "N of M done" ──────────────────────────────────────────────────
    Item {
        y: root._rowHeight + Theme.sp(8)
        width: root.width
        height: root.barHeight

        Text {
            id: doneText
            objectName: "flowProgressText"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: qsTr("%1 of %2 done").arg(root.progress.passed !== undefined ? root.progress.passed : 0)
                                       .arg(root.progress.total !== undefined ? root.progress.total : 0)
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color: Theme.colorText3
        }
        Rectangle {
            id: track
            anchors { left: parent.left; right: doneText.left; rightMargin: Theme.sp(10); verticalCenter: parent.verticalCenter }
            height: Math.max(2, Theme.sp(2))
            radius: height / 2
            color: Theme.colorBorderMid
            Rectangle {
                objectName: "flowProgressFill"
                height: parent.height
                radius: parent.radius
                width: root.progress.total > 0 ? parent.width * Math.min(1, root.progress.passed / root.progress.total) : 0
                color: Theme.colorAccent
                Behavior on width { NumberAnimation { duration: Theme.durationNormal } }
            }
        }
    }
}
