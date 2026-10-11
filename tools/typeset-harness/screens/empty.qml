// The null overlay: declares Session diagnostics' fed properties and draws nothing. Laid over a panel
// it shows the stage card's empty frame, which isolates the chrome from the panel's own content.
// To render the REAL panel, pass "none" as the overlay instead (render.sh none …).
import QtQuick
Item { property string view; property var readout; property var cards; property var source; property var header; property var detail }
