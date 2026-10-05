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

// Session setup — the SHELL (session_wizard_refactor_design.md §4.2). The replacement for
// ScreenSessionWizard.qml, built beside it in Stage 5a; Main has hosted it since Stage 5b, and the
// old file was deleted at Stage 5c. Line references "l.N" to the retired wizard are to its last
// version in git (it is in the history up to the Stage 5c commit).
//
// What it holds: the visit's SetupDraft, the hardware SetupContext, the step registry
// (SetupSteps), the SetupFlow engine, the progress indicator, ONE Loader for the current page (R2)
// and today's header and footer. What it does NOT hold is any knowledge of a step:
//
// ⚠ R1 — THIS FILE NAMES NO STEP (lint W10: no registry key appears here or in PpFlowIndicator).
// The footer is driven only by the current page's contract (canContinue, canSkip, primary, hint,
// hintTone, busy, fullBleed — WizardPage.qml) and by the flow (isFirst, isLast, noLongerNeeded,
// issues). A step-specific `if` belongs in its page or its descriptor, never here.
//
// The header, footer, rules and spacing are today's wizard's (the retired ScreenSessionWizard.qml l.590–668,
// 780–806, 2048–2240), strings and colours unchanged. The tab strip (l.669–778) is replaced by
// PpFlowIndicator (§4.9), which offers jump-back only (D5).
//
// Behaviour that differs from today's wizard, all approved (§6, §10):
//   - the header › is Continue only, disabled when Continue is; it never runs a page's primary
//     (Connect) (F5, D2);
//   - a trip to Settings keeps goals and per-session toggles: a visibility edge suspends and
//     resumes the flow, it does not re-seed (F4);
//   - open() is the only reset: it cancels the paced sensor connect and starts a new draft (N13);
//   - navigation is queued, so a double-click on Continue advances once (N19);
//   - a step that stops applying while current stays, with "No longer needed — Continue" (N14);
//   - exactly one page object is alive (L1, F7).
import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // ── Public API (Main.qml) ────────────────────────────────────────────────────────────────────
    // The presets, for Main's railIndex lookup on Start: presets[type].railIndex.
    readonly property var presets: ctxObj.presets
    // ScreenSettings sub-panel indices (the same as SetupContext's).
    readonly property int settingsPanelCameras: ctxObj.settingsPanelCameras
    readonly property int settingsPanelImus:    ctxObj.settingsPanelImus

    signal cancelled()                                       // Cancel: devices already released
    // Start: devices kept. The goal keys picked, or the preset's first (§5).
    // (The parameter is not called `goals`: W10 holds this file to no registry key.)
    signal sessionStartRequested(int sessionType, var goalKeys)
    signal cameraRecalibrateRequested()
    signal navigateToSettings(int panelIndex)                // settingsPanelCameras / settingsPanelImus
    signal exitRequested(string reason)                      // "back": ‹ on the first step; devices released

    // A new visit for a preset (SessionController.Type) — the old wizard's reset(type). Everything
    // is reset here and only here: draft, step states, goals and enablement seeded from settings,
    // the paced sensor connect cancelled.
    function open(preset) { flowObj.open(preset) }

    // The app header's ‹ / ›. ‹ on the first step exits (devices released, exitRequested("back")).
    readonly property bool canHeaderBack:    flowObj.canHeaderBack
    readonly property bool canHeaderForward: flowObj.canHeaderForward
    function headerBack()    { flowObj.back() }
    function headerForward() { flowObj.next("done") }

    // Hidden (Settings, Athletes, …) and shown again. Driven from the visibility edge below;
    // public so a host can drive it explicitly.
    function suspend() { flowObj.suspend() }
    function resume()  { flowObj.resume() }

    // HOST-level use of `visible` (R3 forbids it only inside pages and routines). A closed flow has
    // nothing to suspend: the edges that follow Start and Cancel are ignored, and an open() in
    // the same turn as the edge that shows the shell runs first (SetupFlow queues lifecycle
    // requests in order).
    onVisibleChanged: {
        if (!flowObj.isOpen) return
        if (visible) flowObj.resume()
        else         flowObj.suspend()
    }

    // The engine, for the tests and for Stage 5b's wiring.
    readonly property var flow:  flowObj
    readonly property var ctx:   ctxObj
    readonly property var draft: draftObj
    readonly property var steps: stepsObj

    readonly property int contentWidth: Theme.contentWidth(width)

    // ── The engine ───────────────────────────────────────────────────────────────────────────────
    // The app's live wrist-angle computer, handed to the context (SetupContext.liveWrist says why
    // the context cannot name it itself). Read here, where `liveWrist` is the context property.
    readonly property var _appLiveWrist: liveWrist
    SetupContext { id: ctxObj;   draft: draftObj; liveWrist: root._appLiveWrist }
    SetupDraft   { id: draftObj; ctx: ctxObj }
    SetupSteps   { id: stepsObj }

    SetupFlow {
        id: flowObj
        registry: stepsObj
        ctx:      ctxObj
        draft:    draftObj
        loader:   pageLoader

        onCancelled:                  root.cancelled()
        onStartRequested:             (preset, picked) => root.sessionStartRequested(preset, picked)
        onExitRequested:              (reason) => root.exitRequested(reason)
        onSettingsRequested:          (panelIndex) => root.navigateToSettings(panelIndex)
        onCameraRecalibrateRequested: root.cameraRecalibrateRequested()
    }

    // ── What the footer and the indicator read ──────────────────────────────────────────────────
    readonly property var  _page: flowObj.page
    readonly property string _noLongerNeeded: qsTr("No longer needed — Continue")
    // The footer hint: the flow's "no longer needed" overrides the page's own.
    // DECISION(stage5a): that hint is drawn in the good tone — it tells the golfer to go on.
    readonly property string _hint:     flowObj.noLongerNeeded ? _noLongerNeeded : (_page !== null ? _page.hint : "")
    readonly property string _hintTone: flowObj.noLongerNeeded ? "good" : (_page !== null ? _page.hintTone : "neutral")
    // Ready reads "fully ready" when no step in the plan reports an issue (today's
    // readinessIssues.length === 0, now aggregated by the flow from the descriptors).
    readonly property bool _fullyReady: flowObj.issues.length === 0

    // The indicator's model: the flow's steps, each with its hover text — the current step's
    // footer hint, any other step's first issue.
    readonly property var _indicatorModel: {
        var st = flowObj.steps, iss = flowObj.issues, out = []
        for (var i = 0; i < st.length; ++i) {
            var s = st[i], tip = ""
            if (s.current) {
                tip = root._hint
            } else {
                for (var j = 0; j < iss.length; ++j)
                    if (iss[j].key === s.key) { tip = iss[j].text; break }
            }
            out.push({ key: s.key, label: s.label, group: s.group, state: s.state, current: s.current,
                       attention: s.attention, number: s.number, tip: tip })
        }
        return out
    }

    // ── Page layout ──────────────────────────────────────────────────────────────────────────────

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ───────────────────────────────────────────────────────────

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: hdrInner.implicitHeight + Theme.sp(40)

            Item {
                id: hdrBox
                anchors.horizontalCenter: parent.horizontalCenter
                width:  root.contentWidth
                height: parent.height

                // The preset being set up, or nothing before the first open().
                readonly property var _preset: draftObj.preset >= 0 && draftObj.preset < ctxObj.presets.length
                                               ? ctxObj.presets[draftObj.preset] : null

                Column {
                    id: hdrInner
                    anchors {
                        left:           parent.left
                        right:          closeBtn.left
                        verticalCenter: parent.verticalCenter
                        rightMargin:    Theme.sp(16)
                    }
                    spacing: Theme.sp(6)

                    Row {
                        spacing: Theme.sp(8)
                        Text {
                            text:           hdrBox._preset ? hdrBox._preset.icon : ""
                            font.pixelSize: Theme.sp(20)
                            color:          Theme.colorText2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text:           hdrBox._preset ? hdrBox._preset.name : ""
                            font.family:    Theme.fontDisplay
                            font.italic:    Theme.fontDisplayItalic
                            font.weight: Theme.fontDisplayWeight
                            font.pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                            color:          Theme.colorText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        width:          parent.width
                        text:           qsTr("Before you step up, let's make sure everything is ready. We'll confirm your goals, check your cameras, and connect your sensors — it only takes a moment.")
                        font.family:    Theme.fontBody
                        font.weight:    Theme.fontBodyWeight
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText2
                        wrapMode:       Text.WordWrap
                        lineHeight:     1.65
                    }

                    Text {
                        text:               ctxObj.athleteName !== ""
                                                ? ctxObj.athleteName.toUpperCase()
                                                : qsTr("NO ATHLETE")
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color:              Theme.colorText3
                    }
                }

                // Cancel releases the devices (through the flow) and then emits cancelled().
                PpButton {
                    id: closeBtn
                    objectName: "setupCancel"
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    label:     qsTr("Cancel")
                    onClicked: flowObj.exit("cancel")
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid }

        // ── Progress indicator (replaces the tab strip) ───────────────────────

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: indicator.implicitHeight + Theme.sp(20)
            // Above the body, so a pip's hover text can overlap the page.
            z: 2

            PpFlowIndicator {
                id: indicator
                objectName: "setupIndicator"
                anchors.centerIn: parent
                width:    Math.max(0, Math.min(parent.width - Theme.sp(32), root.contentWidth))
                model:    root._indicatorModel
                groups:   stepsObj.groupLabels
                progress: flowObj.progress
                canGoTo:  function(key) { return flowObj.canGoTo(key) }
                onJumpRequested: (key) => flowObj.goTo(key)
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid }

        // ── Body — Flickable so tall pages don't clip ─────────────────────────

        Flickable {
            id: body
            Layout.fillWidth:  true
            Layout.fillHeight: true
            readonly property bool _fullBleed: root._page !== null && root._page.fullBleed
            contentWidth:  width
            // Viz pages fill the viewport exactly (no scroll padding); text-led
            // pages add a little bottom breathing room. The Math.max keeps content
            // scrollable if it ever exceeds the viewport (e.g. a very short window).
            contentHeight: Math.max(height, bodyPanel.implicitHeight + (_fullBleed ? 0 : Theme.sp(40)))
            clip:          true

            Item {
                id: bodyPanel
                anchors.horizontalCenter: parent.horizontalCenter
                // Viz pages span the full viewport; text-led pages stay in the
                // centred reading column.
                width:          body._fullBleed ? parent.width : root.contentWidth
                implicitHeight: pageLoader.height

                // THE page: SetupFlow sets its source; exactly one page is alive (R2).
                Loader {
                    id: pageLoader
                    width:  parent.width
                    height: body._fullBleed ? Math.max(Theme.sp(480), body.height)
                                            : (item !== null ? item.implicitHeight : 0)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid }

        // ── Footer ───────────────────────────────────────────────────────────

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(64)

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width:  root.contentWidth
                height: parent.height

                RowLayout {
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: Theme.sp(8)

                    // Hint text
                    Text {
                        objectName: "setupHint"
                        Layout.fillWidth: true
                        text: root._hint
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color: root._hintTone === "good" ? Theme.colorGood
                             : root._hintTone === "warn" ? Theme.colorWarn
                                                         : Theme.colorText3
                        elide: Text.ElideRight
                    }

                    // ← Back
                    PpButton {
                        objectName: "setupBack"
                        visible: flowObj.isOpen && !flowObj.isFirst
                        label:   qsTr("← Back")
                        primary: false
                        onClicked: flowObj.back()
                    }

                    // Skip → — the page offers it. Not on the last step (Start is the way on) and
                    // not on a step that no longer applies (Continue is).
                    PpButton {
                        objectName: "setupSkip"
                        visible: root._page !== null && root._page.canSkip
                                 && !flowObj.isLast && !flowObj.noLongerNeeded
                        label:   qsTr("Skip →")
                        primary: false
                        onClicked: flowObj.next("skipped")
                    }

                    // Primary action
                    Rectangle {
                        id: primaryBtn
                        objectName: "setupPrimary"
                        // The page's own primary (Connect…) replaces Continue while it offers one.
                        // Keeps the bottom-right button the single "keep progressing" control.
                        readonly property var  _offer: root._page !== null && !flowObj.noLongerNeeded
                                                       ? root._page.primary : null
                        readonly property bool _start: flowObj.isLast

                        implicitWidth:  primaryLbl.implicitWidth + Theme.sp(28)
                        implicitHeight: Theme.sp(38)
                        radius: Theme.radius
                        readonly property color _primaryBase: !_start
                                   ? Theme.colorAccent
                                   : (root._fullyReady ? Theme.colorGood : Theme.colorWarn)
                        color: primaryArea.containsMouse ? Qt.lighter(_primaryBase, 1.08) : _primaryBase
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        // Dim only when there's nothing actionable: the page's primary says it is
                        // not enabled, or Continue would not advance (a gate — Ball, Calibrate).
                        // Start is always allowed (§4.12). Uses primaryArea.pressed for press
                        // feedback — imperative opacity assignments would destroy this binding.
                        opacity: {
                            var blocked = _offer !== null ? _offer.enabled === false
                                                          : (!_start && root._page !== null && !flowObj.canContinue)
                            if (blocked) return 0.4
                            return primaryArea.pressed ? 0.8 : 1.0
                        }
                        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

                        Text {
                            id: primaryLbl
                            anchors.centerIn: parent
                            text: primaryBtn._offer !== null
                                      ? primaryBtn._offer.label
                                      : (!primaryBtn._start
                                            ? qsTr("Continue →")
                                            : (root._fullyReady ? qsTr("▶  Start session") : qsTr("▶  Start anyway")))
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color: Theme.dark ? Theme.colorBg : "#FFFFFF"
                        }

                        PpPressable {
                            id: primaryArea
                            hoverScale: 1.0   // full-width CTA — keep press-dip only
                            onClicked: {
                                // The page's primary: run it instead of advancing.
                                if (primaryBtn._offer !== null) {
                                    primaryBtn._offer.run()
                                    return
                                }
                                // The flow guards Continue (page.canContinue && the step's gate)
                                // and refuses a comingSoon preset's Start (today's defence).
                                if (primaryBtn._start) flowObj.exit("start")
                                else                   flowObj.next("done")
                            }
                        }

                        // Traveling-light frame while the page is connecting devices. Light hue
                        // to read against the filled accent/good/warn background.
                        PpConnectingFrame {
                            anchors.fill: parent
                            radius:  parent.radius
                            color:   Theme.dark ? Theme.colorBg : "#FFFFFF"
                            running: root._page !== null
                                     && (root._page.busy || (primaryBtn._offer !== null && primaryBtn._offer.busy === true))
                        }
                    }
                }
            }
        }
    }
}
