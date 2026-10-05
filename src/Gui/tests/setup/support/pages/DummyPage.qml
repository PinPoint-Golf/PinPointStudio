// A stand-in setup page for the Stage 4 engine tests (tst_setup_flow.qml): a real WizardPage that
// does nothing but report its lifecycle to `pageJournal` (support/PageJournal.qml) and expose the
// contract outputs the cases steer through the journal's config.
import QtQuick
import PinPointStudio

WizardPage {
    id: p

    // L3: read after the page has requested its own navigation, in the same handler.
    property string probe: "alive"

    canContinue: pageJournal.cfg(stepKey, "canContinue", true)
    canSkip:     pageJournal.cfg(stepKey, "canSkip", true)

    Component.onCompleted:   pageJournal.born(p)
    Component.onDestruction: pageJournal.died(p)
    onActiveChanged:         pageJournal.record("active", p, active ? "true" : "false")

    function enter(direction) { pageJournal.record("enter", p, direction) }
    function leave(reason)    { pageJournal.record("leave", p, reason) }

    // L3: navigate from inside the page's own handler, then keep using the page.
    function selfAdvance() {
        flow.next("done")
        return p.probe + ":" + p.stepKey + ":" + p.active
    }
}
