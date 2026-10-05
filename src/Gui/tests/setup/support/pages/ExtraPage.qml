// N17 / N18: the page file of a step the TEST registers (an extension descriptor). Distinct from
// DummyPage so a case can tell which file the flow loaded for the extra step.
import QtQuick
import PinPointStudio

WizardPage {
    id: p
    objectName: "extraPage"

    canSkip: true

    Component.onCompleted:   pageJournal.born(p)
    Component.onDestruction: pageJournal.died(p)
    onActiveChanged:         pageJournal.record("active", p, active ? "true" : "false")

    function enter(direction) { pageJournal.record("enter", p, direction) }
    function leave(reason)    { pageJournal.record("leave", p, reason) }
}
