// Fake AthleteController (src/Gui/athlete/athlete_controller.h): the wizard, the calibration
// flow and ArmVizView read currentHandedness ("Left" → left-handed, anything else right)
// and currentName.
import QtQuick

QtObject {
    property bool   hasCurrentAthlete: true
    property string currentName:       "Test Athlete"
    property string currentInitials:   "TA"
    property string currentHandedness: "Right"
    property string currentUuid:       "00000000-0000-0000-0000-000000000001"
}
