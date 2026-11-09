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

import QtQuick

// SHOTOUTCOMEHEAD — the head of This shot on the Replay glance (shot_outcomes_design.md §6.1): the labels in the
// golfer's words, the story, up to two quiet lines, and quiet links to say "not right". Labels are typeset words
// (no pills), corrections are links, nothing is coloured: every string is the model's, positioned here.
// A clean shot is one calm line. On every shot the correction row stays folded until "Not right?" opens it, and a
// correction folds it again. It is drawn only where a press can land (interactive), so the auto-closing cast shows the words alone. Nothing is drawn while
// `ready` is false: the labels and story land together, after analysis.
// outcome: SessionDiagnosticsModel.shotOutcome(shotId), or {}. Left-handers: labels and corrections carry the
// golfer's ids (`id`, never `rowId`), glossed by the model, so nothing here mirrors anything.
Item {
    id: hd
    objectName: "sdOutcomeHead"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property var source: null                        // correctOutcome / setShotIntent / setIntentFrom / missCandidates
    property var outcome: ({})
    property int shotId: -1
    property int shotNo: 0
    property bool interactive: true                  // false on the auto-closing cast: no correction row at all
    property bool whyEnabled: true                   // off on every cast
    signal whyRequested()

    readonly property bool ready: !!outcome && outcome.ready === true
    readonly property var labels: (outcome && outcome.labels) || []
    readonly property bool clean: !!outcome && outcome.clean === true
    readonly property var driver: (outcome && outcome.driver) || ({})
    readonly property bool hasDriver: driver.id !== undefined && driver.id !== ""
    readonly property string carryLine: (outcome && outcome.carry && outcome.carry.line) || ""
    readonly property string noReasonText: (outcome && outcome.noReasonText) || ""

    readonly property string producedLine: (outcome && outcome.producedLine) || ""
    // a session with no launch monitor at all says so once, in the session block, not on every shot
    readonly property bool sessionUnread: {
        const log = source && source.shotLog
        return !!log && log.length > 0 && log.every(s => !!s.noReading)
    }
    readonly property string labelWords: labels.filter(l => !l.absorbed)
                                               .map(l => l.short + (l.corrected ? " (you said)" : l.inferred ? " (from the flight)" : ""))
                                               .join(" · ")
    readonly property string headText: clean || labels.length === 0 ? ((outcome && outcome.phrase) || "") : labelWords

    readonly property var candidates: hd.ready && hd.interactive && hd.source && typeof hd.source.missCandidates === "function"
                                      ? (hd.source.missCandidates() || []) : []
    readonly property var families: {
        const seen = {}, out = []
        const add = f => { if (f && !seen[f]) { seen[f] = true; out.push(f) } }
        labels.forEach(l => add(l.family))
        candidates.forEach(m => add(m.family))
        return out
    }
    // one shot's folds: the correction row opened, the intent just chosen, "from here on" pressed
    property int _openShot: -1
    property int _chosenShot: -1
    property string _chosenShape: ""
    property int _fromSetShot: -1
    readonly property bool correctOpen: _openShot === shotId
    readonly property bool showCorrections: interactive && families.length > 0 && correctOpen
    readonly property string _intendedLabel: { const l = labels.find(x => x.intended); return l ? String(l.id) : "" }
    readonly property string intentNow: _chosenShot === shotId ? _chosenShape : _intendedLabel

    function _noneWord(family) { return family === "strike" ? "Clean" : family === "direction" ? "Straight" : "None" }
    function _meantShapes(detected) {
        const has = w => detected.some(l => String(l.id).indexOf(w) >= 0)
        const out = []
        if (has("slice") || has("fade")) out.push("fade")
        if (has("hook") || has("draw")) out.push("draw")
        return out.length ? out : ["fade", "draw"]
    }
    function chipsFor(family) {
        const detected = labels.filter(l => l.family === family)
        const out = detected.map(l => ({ key: l.id, label: l.short, on: true }))
        for (const m of candidates)
            if (m.family === family && !out.some(c => c.key === m.id)) out.push({ key: m.id, label: m.name || m.id, on: false })
        out.push({ key: "", label: _noneWord(family), on: detected.length === 0 })
        if (family === "direction" && detected.length > 0)
            for (const shape of _meantShapes(detected)) out.push({ key: "meant:" + shape, label: "Meant to " + shape, on: intentNow === shape })
        return out
    }
    function pickChip(family, key) {
        if (!interactive || !source) return
        if (key.indexOf("meant:") === 0) {
            const shape = key.slice(6), next = intentNow === shape ? "" : shape
            if (typeof source.setShotIntent === "function") source.setShotIntent(shotId, next)
            _chosenShot = shotId; _chosenShape = next
            _openShot = -1
            return
        }
        if (labels.some(l => l.family === family && l.id === key)) return    // already what the shot says
        if (typeof source.correctOutcome === "function") source.correctOutcome(shotId, family, key)
        _openShot = -1
    }
    function setFromHere() {
        if (!interactive || !source || intentNow === "" || typeof source.setIntentFrom !== "function") return
        source.setIntentFrom(shotId, intentNow)
        _fromSetShot = shotId
    }

    visible: ready && !sessionUnread
    implicitHeight: visible ? col.height : 0
    height: implicitHeight

    // labels are words in the head, so tests read them here: one marker per label, as PpLedgerTable's fresh marker
    Repeater {
        model: hd.roles ? hd.labels : []
        delegate: Item {
            required property var modelData
            objectName: "sdOutcomeChip:" + modelData.id
            readonly property string text: modelData.short
            readonly property string labelFamily: modelData.family
            readonly property bool corrected: !!modelData.corrected
            readonly property bool intended: !!modelData.intended
            width: 0; height: 0
        }
    }

    Column {
        id: col
        width: hd.width
        spacing: hd.roles.g(5)
        PpStandfirst {
            width: parent.width; roles: hd.roles; brush: hd.brush; interactive: false
            kicker: "What happened · shot " + hd.shotNo
            head: hd.headText
            standfirst: hd.clean || !hd.outcome || !hd.outcome.story ? "" : hd.roles.esc(hd.outcome.story)
        }
        Text {
            objectName: "sdOutcomeProduced"
            visible: hd.producedLine !== ""
            width: parent.width; wrapMode: Text.WordWrap
            text: hd.producedLine; font: hd.roles.fSmall; color: hd.roles.ink2
        }
        Item {
            objectName: "sdOutcomeSeenWith"
            visible: hd.hasDriver && !hd.clean
            width: parent.width; height: visible ? Math.max(seen.height, strength.height) : 0
            Text {
                id: seen
                width: parent.width - (strength.visible ? strength.width + hd.roles.g(14) : 0)
                wrapMode: Text.WordWrap
                text: hd.roles.seenWith(hd.driver)
                font: hd.roles.fSmall; color: hd.roles.ink2
            }
            Text {
                id: strength
                visible: String(hd.driver.strength || "") !== ""
                anchors.right: parent.right
                y: seen.y + seen.baselineOffset - baselineOffset
                text: hd.roles.cap1(hd.driver.strength || ""); font: hd.roles.fLabel; color: hd.roles.ink3
            }
        }
        Text {
            objectName: "sdOutcomeCarryLine"
            visible: hd.carryLine !== "" && !hd.clean
            width: parent.width; wrapMode: Text.WordWrap
            text: hd.carryLine; font: hd.roles.fSmall; color: hd.roles.ink3
        }
        Text {
            objectName: "sdOutcomeNoReason"
            visible: hd.noReasonText !== ""
            width: parent.width; wrapMode: Text.WordWrap
            text: hd.noReasonText; font: hd.roles.fSmall; color: hd.roles.ink2
        }
        Column {
            objectName: "sdOutcomeCorrectRow"
            visible: hd.showCorrections
            width: parent.width
            spacing: hd.roles.g(3)
            Text { text: "Not right?"; font: hd.roles.fLabel; color: hd.roles.ink3 }
            Repeater {
                model: hd.showCorrections ? hd.families : []
                delegate: PpStandfirst {
                    required property var modelData
                    readonly property string family: modelData
                    objectName: "sdOutcomeCorrect:" + family
                    width: col.width; roles: hd.roles; brush: hd.brush; interactive: hd.interactive
                    spacing: hd.roles.g(2)
                    kicker: hd.roles.cap1(family)
                    chips: hd.chipsFor(family)
                    onChipActivated: key => hd.pickChip(family, key)
                }
            }
        }
        Flow {
            width: parent.width
            spacing: hd.roles.g(14)
            // the door to the correction row, on every shot; the same link folds it again unchanged
            Text {
                objectName: "sdOutcomeNotRight"
                visible: hd.interactive && hd.families.length > 0
                text: hd.correctOpen ? "Close" : "Not right?"; font: hd.roles.fSmallL; color: hd.roles.link
                MouseArea { anchors.fill: parent; anchors.margins: -hd.roles.u(3); cursorShape: Qt.PointingHandCursor
                            onClicked: hd._openShot = hd.correctOpen ? -1 : hd.shotId }
            }
            Text {
                objectName: "sdOutcomeFromHere"
                visible: hd.interactive && hd.intentNow !== ""
                text: hd._fromSetShot === hd.shotId ? "From here on · set" : "From here on"
                font: hd._fromSetShot === hd.shotId ? hd.roles.fSmall : hd.roles.fSmallL
                color: hd._fromSetShot === hd.shotId ? hd.roles.ink3 : hd.roles.link
                MouseArea { enabled: hd._fromSetShot !== hd.shotId; anchors.fill: parent; anchors.margins: -hd.roles.u(3)
                            cursorShape: Qt.PointingHandCursor; onClicked: hd.setFromHere() }
            }
            Text {
                objectName: "sdOutcomeWhy"
                visible: hd.whyEnabled
                text: "Why →"; font: hd.roles.fSmallL; color: hd.roles.link
                MouseArea { anchors.fill: parent; anchors.margins: -hd.roles.u(3); cursorShape: Qt.PointingHandCursor
                            onClicked: hd.whyRequested() }
            }
        }
    }
}
