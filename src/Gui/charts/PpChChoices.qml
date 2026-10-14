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
import PinPointStudio

// PPCHCHOICES — a view's controls as quiet lines of text ("Metrics  Kinematic sequence ▾   Window  P4→P8 ·
// P1→P10 · Full"). The current choice is ink, the others links; nothing is boxed and nothing pops up: a long
// list opens IN PLACE under the line (the register owns the PpInlinePicker). Wraps, never truncates. A set
// may be a toggle row (`multi`: several options on at once, e.g. a preset's metrics) or an opener (`opens`:
// one option that toggles a picker, drawn with ▾ / ▴). Screen only.
//   sets: [{ key, label, current, multi, opens, open, options: [{ key, label, on, enabled, swatch }] }]
//   picked(setKey, optionKey) — the register routes it to the body.
// objectNames: chChoice:<set key> on each group, chOption (+ `key`, `setKey`) on each option.
Flow {
    id: cc
    property QtObject roles: null
    property bool interactive: true
    property var sets: []
    readonly property var keyKinds: []
    property bool atom: true
    signal picked(string set, string option)
    spacing: roles ? roles.u(4) : 4
    Repeater {
        model: cc.roles ? cc.sets : []
        delegate: Row {
            id: grp
            required property var modelData
            objectName: "chChoice:" + modelData.key
            spacing: cc.roles.u(8)
            rightPadding: cc.roles.u(18)
            readonly property var opts: modelData.options || []
            Text { id: lab; text: grp.modelData.label; font: cc.roles.fLabel; color: cc.roles.ink3; topPadding: cc.roles.u(1.5) }
            Flow {
                width: Math.min(meas.implicitWidth + 4, cc.width - lab.implicitWidth - grp.spacing - grp.rightPadding)
                spacing: cc.roles.u(7)
                // the options' natural width (one line), so the group wraps inside itself only when it must
                Row {
                    id: meas; visible: false; spacing: cc.roles.u(7)
                    Repeater { model: grp.opts
                               delegate: Text { required property var modelData; text: modelData.label + "  ·"; font: cc.roles.fSmallM } }
                }
                Repeater {
                    model: grp.opts
                    delegate: Row {
                        id: opt
                        required property var modelData
                        required property int index
                        objectName: "chOption"
                        readonly property string key: modelData.key
                        readonly property string setKey: grp.modelData.key
                        readonly property bool on: grp.modelData.multi ? modelData.on === true
                                                   : grp.modelData.opens ? grp.modelData.open === true
                                                   : modelData.key === grp.modelData.current
                        readonly property bool live: cc.interactive && modelData.enabled !== false
                                                     && (grp.modelData.multi || grp.modelData.opens || !on)
                        spacing: cc.roles.u(7)
                        // a metric's swatch: its own hue (identity), faint when it is switched off
                        Rectangle {
                            visible: !!opt.modelData.swatch
                            width: visible ? cc.roles.u(cc.roles.ch.swatch) : 0; height: width
                            anchors.verticalCenter: parent.verticalCenter
                            radius: cc.roles.square ? 0 : width / 2
                            color: opt.modelData.swatch ? opt.modelData.swatch : "transparent"
                            opacity: opt.on ? 1 : 0.35
                        }
                        Text {
                            text: opt.modelData.label + (grp.modelData.opens ? (grp.modelData.open ? "  ▴" : "  ▾") : "")
                            textFormat: Text.PlainText
                            font: opt.on && !grp.modelData.opens ? cc.roles.fSmallM : (opt.live ? cc.roles.fSmallL : cc.roles.fSmall)
                            color: opt.modelData.enabled === false ? cc.roles.ink3
                                 : grp.modelData.multi ? (opt.on ? cc.roles.ink : cc.roles.ink3)
                                 : opt.on && !grp.modelData.opens ? cc.roles.ink : (opt.live ? cc.roles.link : cc.roles.ink3)
                            // a switched-off metric stays in the line, dimmed (never silently dropped)
                            MouseArea { enabled: opt.live; anchors.fill: parent; anchors.margins: -cc.roles.u(2)
                                        cursorShape: Qt.PointingHandCursor; onClicked: cc.picked(opt.setKey, opt.key) }
                        }
                        Text { visible: opt.index < grp.opts.length - 1; text: "·"; font: cc.roles.fSmall; color: cc.roles.ink3 }
                    }
                }
            }
        }
    }
}
