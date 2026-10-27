// Fake WorkOnsController (src/Gui/diagnostics/work_ons_controller.h) — the members SetupContext
// reads for the Intent step's focus choices (phase 2 C5), as the app's "workOns" context property.
// OPT-IN, like FakeTts: SetupDriver adds it to the shell's context only when a case sets
// `d.workOns` before open(); left null, the context has NO workOns at all, as headless.
//
//   items       the work-ons list: [{ id, name, status: "active" | "easing" }]
//   focusItem   the coaching summary's focus: { focusConditionId: "" | id } ("" = none)
//   labels      id → the pack label conditionLabel() answers; an id not in it answers the id
//               itself, as the real controller's fallback does
import QtQuick

QtObject {
    property var items: []
    property var focusItem: ({ focusConditionId: "" })
    property var labels: ({})

    function conditionLabel(id) {
        return labels[id] !== undefined ? labels[id] : id
    }
}
