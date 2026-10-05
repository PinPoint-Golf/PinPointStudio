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

// The page contract (session_wizard_refactor_design.md §4.3). Every page under setup/pages/ has a
// WizardPage as its root (lint W2). A page exists only while it is the current step (R2): the
// flow creates it through the shell's Loader and destroys it when the step is left, so any state
// that must outlive it lives in the draft or in a manager.
//
// Lifecycle, driven by SetupFlow and never by the page itself:
//   created → active = true → enter(direction) … leave(reason) → active = false → destroyed
// `direction`: "forward" | "back" | "jump" | "resume".
// `reason`:    "forward" | "skip" | "back" | "jump" | "suspend" | "exit".
// A suspend (the shell hidden for Settings) is leave("suspend") + active = false with the page
// kept; the resume is active = true + enter("resume").
//
// ⚠ R3: every Timer `running`, every Connections `enabled` and every animation in a page (or in
// a routine it hosts) is gated on `active` — never on `visible`, never on a step index (lint W1).
// ⚠ R5: a page REQUESTS navigation (flow.next("done"), flow.back(), …). The flow applies it on a
// later event-loop turn, so code after the call in the same handler still runs on a live page.
// ⚠ Hardware goes through `ctx` only; a page never names imuManager or cameraManager (lint W4).
import QtQuick

Item {
    id: page

    // ── Inputs (set by the flow) ─────────────────────────────────────────────────────────────────
    property bool   active:    false    // R3: true from just before enter() until just after leave()
    property var    flow:      null     // SetupFlow — to request navigation
    property var    ctx:       null     // SetupContext — every hardware read and device action
    property var    draft:     null     // SetupDraft — this visit's goals, states and outcomes
    property string stepLabel: ""       // "STEP 4 OF 8 · MOTION SENSORS", live from the plan
    // The descriptor key this page was created for. Not in the design's §4.3 list: added so a page
    // (and the log) can name itself without a lookup; nothing requires a page to read it.
    property string stepKey:   ""

    // ── Outputs (bound by the page) ──────────────────────────────────────────────────────────────
    property bool   canContinue: true          // footer Continue enabled; header › enabled
    property bool   canSkip:     false         // footer Skip shown
    // { label, run(), busy, enabled } replaces Continue (Connect…). `busy` runs the traveling-light
    // frame; `enabled: false` dims it (opacity 0.4) — omitted means enabled. While a page offers a
    // primary it should report canContinue false: the header › is Continue only and must not pass
    // a step whose footer is asking to connect (F5, D2).
    property var    primary:     null
    property string hint:        ""            // footer hint
    property string hintTone:    "neutral"     // "good" | "warn" | "neutral"
    property bool   fullBleed:   false         // viz pages span the viewport, not the reading column
    // Devices are connecting on this page's behalf: the footer primary's traveling-light frame runs
    // whatever the primary currently reads (today's footer ran it on Cameras whenever
    // cameraManager.anyConnecting, Connect or not). Added in Stage 5a.
    property bool   busy:        false

    // ── Lifecycle (called by the flow; a page overrides them) ───────────────────────────────────
    function enter(direction) {}
    function leave(reason)    {}
}
