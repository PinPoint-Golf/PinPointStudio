// The ONE window-level notification surface, rendering NotificationCenter.
//
// ⛔ WHY THIS IS A MODEL AND NOT FOUR PpToasts.  Until this component the four
// shot-pipeline notices were four PpToast instances, each wired to its own
// signal and each positioned by hand relative to `saveErrorToast.y` — three of
// them computing the SAME y, so any two visible at once overlapped. Worse,
// PpToast.show() sets the text and restarts a hide timer, so ten consecutive
// failures were one toast showing the tenth message. Identity, counting,
// cause-suppression and latching all live in C++ now (see notification_center.h);
// this file only draws what the model says is live.
//
// PpToast still serves the panel-local imperative confirmations (copy-to-
// clipboard, an export result) — those are not this problem, per design §7.4 —
// and both draw the same card, PpNotificationCard.

import QtQuick
import PinPointStudio

ListView {
    id: root

    // Severity enum, mirrored from NotificationCenter::Severity.
    readonly property int sevInfo:     0
    readonly property int sevWarn:     1
    readonly property int sevError:    2
    readonly property int sevProgress: 3
    // Kind enum, mirrored from NotificationCenter::Kind.
    readonly property int kindCondition: 1

    model: notifications

    // Index 0 — the first thing raised — keeps the bottom anchor position the
    // old saveErrorToast had, and later news piles above it.
    verticalLayoutDirection: ListView.BottomToTop
    spacing: Theme.gap(10)
    interactive: false
    height: Math.min(contentHeight, parent ? parent.height * 0.6 : contentHeight)
    implicitWidth: Theme.sp(560)

    delegate: Item {
        id: row
        width: root.width
        height: card.implicitHeight

        required property string notificationId
        required property int    severity
        required property int    kind
        required property string title
        required property string detail
        required property int    count
        required property string glyph
        required property string actionLabel

        readonly property color _severityColor:
              severity === root.sevError ? Theme.colorError
            : severity === root.sevWarn  ? Theme.colorWarn
            :                              Theme.colorBorderStrong

        readonly property string _glyph:
            glyph.length > 0 ? glyph
          : severity === root.sevError    ? "⚠"
          : severity === root.sevWarn     ? "⚠"
          : severity === root.sevProgress ? "⋯"
          :                                 "◎"

        PpNotificationCard {
            id: card
            anchors.horizontalCenter: parent.horizontalCenter
            glyph:       row._glyph
            glyphColor:  row.severity === root.sevInfo ? Theme.colorText2 : row._severityColor
            tone:        row._severityColor
            // ⭐ The count is the diagnostically valuable part, and
            // the old shape lost it entirely.
            title:       row.count > 1 ? qsTr("%1 (×%2)").arg(row.title).arg(row.count)
                                       : row.title
            detail:      row.detail
            // The fix-this affordance — in-app navigation only.
            actionText:  row.actionLabel
            // A condition does not auto-hide, so it needs a way out.
            dismissible: row.kind === root.kindCondition
            onActionClicked:  notifications.invokeAction(row.notificationId)
            onDismissClicked: notifications.dismiss(row.notificationId)
        }
    }
}
