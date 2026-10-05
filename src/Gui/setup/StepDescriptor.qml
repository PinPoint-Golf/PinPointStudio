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

// One step of session setup, as DATA (session_wizard_refactor_design.md §4.4, R1). A step is this
// descriptor plus one page file and nothing else: the flow, the indicator, the numbering, the
// closing page's issues and summary all read it from here.
//
// ⚠ applies / gate / issues / summary are evaluated WITHOUT the page alive — by the flow's plan
// binding, by Ready, and by the indicator's "needs attention" ring on a step that is not current.
// So they read the context (`ctx`, SetupContext) and the draft (`draft`, SetupDraft) and nothing
// else: no page property, no manager. Bindings that call them pick up their reactive
// dependencies from the reads they make, so a predicate must READ what it depends on (a bare
// `ctx.foo` statement is dropped by the compiler — memory note qml-dead-statement-bindings).
//
// ⚠ No applies or gate may read the session type or the preset (§4.12, lint W8): hardware
// decides which steps run, the preset only picks goals and where Start lands.
import QtQuick

QtObject {
    // Stable identity: states, outcomes, the log and the tests use it.
    property string key: ""
    // The indicator's label ("Calibrate").
    property string title: ""
    // The page eyebrow's tail: "STEP 6 OF 8 · CALIBRATE". Today's eyebrows are not the tab labels
    // ("CONFIRM TRACKING" vs "Confirm"), so the two are separate.
    property string eyebrow: ""
    // Indicator group: "session" | "cameras" | "sensors" | "ready".
    property string group: ""
    // The instrument group whose calibration this step belongs to ("arm", "trunk"), or "".
    // SetupSteps.instrumentGroups is derived from it: a mount is offered only for a group with a
    // calibrate step in the registry (§4.12), which is what ImuMounts.trunkMountsOffered stands
    // in for until Stage 5 wires the two together.
    property string instrumentGroup: ""
    // Page file, relative to src/Gui/setup/ ("pages/CalibrateArmPage.qml"), or an absolute URL
    // (an extension registered from elsewhere, e.g. a test).
    property string page: ""
    // For an EXTENSION (SetupSteps.extensions): the key it is inserted after; "" = at the end.
    // Built-in steps are in registry order and leave it empty.
    property string after: ""

    // Does this step run in this session (R6: from the hardware)?
    property var applies: function(ctx, draft) { return true }
    // May Continue mark it done? ANDed with the page's own canContinue (design §4.5).
    property var gate:    function(ctx, draft) { return true }
    // What the closing page lists for this step: [{ text, panel }], panel = a Settings sub-panel
    // index for a "→ Open … settings" link, or -1.
    property var issues:  function(ctx, draft) { return [] }
    // The closing page's row: { label, value, good }, or null for a step with no row.
    property var summary: function(ctx, draft) { return null }
}
