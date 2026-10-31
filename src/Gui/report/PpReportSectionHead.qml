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

// THE HEAD OF A REPORT SECTION — the session report's section pages (Posture, Wrist, Club delivery, each work-on):
// the kicker in the accent, the title (H1), the standfirst; then whatever the page puts inside it (a Pattern / Style
// line, the first block's heading), in order, as children of this Column. ONE pager atom that starts a fresh page
// (breakBefore) and keeps with what follows; `section` is what a continued page says ("Posture, continued").
// A page without a standfirst (a work-on: its coach's term follows the title instead) sets showStandfirst: false.
// The page sets the objectName (reportPosture, reportWrist, reportDelivery, reportWorkOn<N>). FOLIO LIGHT through `ro`.
Column {
    id: sh
    property QtObject ro: null
    property string kicker: ""
    property string title: ""
    property string standfirst: ""
    property bool showStandfirst: true
    property bool atom: true
    property bool keepNext: true
    property bool breakBefore: true
    property string section: title
    width: 600
    Text { width: parent.width; text: sh.kicker; font: sh.ro.fKicker; color: sh.ro.accent; wrapMode: Text.WordWrap }
    Item { width: 1; height: 6 }
    Text { width: parent.width; text: sh.title; font: sh.ro.fH1; color: sh.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
    Item { width: 1; height: 10; visible: sh.showStandfirst }
    Text { visible: sh.showStandfirst; width: parent.width; text: sh.standfirst; font: sh.ro.fStand; color: sh.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.2 }
}
