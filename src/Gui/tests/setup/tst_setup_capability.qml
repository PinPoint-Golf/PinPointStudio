// Stage 5b — the closing page's "What this session will record" wording, as a unit:
// src/Gui/setup/setup_capability_rows.js over the REAL catalogue (MetricCatalog.setupSummary), for
// the seven setups of src/Metrics/tests/setup_capability_test.cpp (design §4.14).
//
// What is pinned here:
//   - every catalogue group lands in exactly one family, and every family names only groups the
//     catalogue has (a new group cannot vanish from the page, a renamed one cannot linger);
//   - the state rule and the reason vocabulary, case by case;
//   - per setup, the state and the reason of every family — read and checked by hand when they
//     were written (Stage 5b), so a change in the catalogue that moves one shows up here;
//   - no string the golfer can read carries an engineer's word: no role name, no "IMU", no
//     "move" (the page describes, it never instructs — cameras are fixed), no slot letter.
import QtQuick
import QtTest
import PinPointStudio
import "../../setup/setup_capability_rows.js" as CapRows

TestCase {
    id: tc
    name: "SetupCapability"

    MetricCatalog { id: catalog }

    function init() {
        testLog.reset()
        if (Qt.platform.pluginName === "offscreen")
            testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
    }
    function cleanup() {
        var w = testLog.takeWarnings()
        compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
    }

    // The seven setups (setup_capability_test.cpp setups()), as the setup flow would hand them to
    // the catalogue (SetupContext.setupFacts), with the context the reasons read.
    readonly property var setups: [
        { tag: "1 face-on only",
          facts: { faceOn: true, dtl: false, imuRoles: [], hackMotion: false, launchMonitor: false },
          ctx:   { armInSession: false, armCalibrated: false, upperArmHeld: false, cameraCount: 1,
                   faceOn: "connected", dtl: "absent" } },
        { tag: "2 face-on + wG3",
          facts: { faceOn: true, dtl: false, imuRoles: ["leadForearm", "leadHand"], hackMotion: true, launchMonitor: false },
          ctx:   { armInSession: true, armCalibrated: true, upperArmHeld: false, cameraCount: 1,
                   faceOn: "connected", dtl: "absent" } },
        { tag: "3 face-on + two Witmotions",
          facts: { faceOn: true, dtl: false, imuRoles: ["leadForearm", "leadHand"], hackMotion: false, launchMonitor: false },
          ctx:   { armInSession: true, armCalibrated: true, upperArmHeld: false, cameraCount: 1,
                   faceOn: "connected", dtl: "absent" } },
        { tag: "4 two cameras + trunk",
          facts: { faceOn: true, dtl: true, imuRoles: ["pelvis", "thorax"], hackMotion: false, launchMonitor: false },
          ctx:   { armInSession: false, armCalibrated: false, upperArmHeld: false, cameraCount: 2,
                   faceOn: "connected", dtl: "connected" } },
        { tag: "5 everything + launch monitor",
          facts: { faceOn: true, dtl: true, imuRoles: ["pelvis", "thorax", "leadForearm", "leadHand"], hackMotion: true,
                   launchMonitor: true },
          ctx:   { armInSession: true, armCalibrated: true, upperArmHeld: false, cameraCount: 2,
                   faceOn: "connected", dtl: "connected" } },
        { tag: "6 nothing",
          facts: { faceOn: false, dtl: false, imuRoles: [], hackMotion: false, launchMonitor: false },
          ctx:   { armInSession: false, armCalibrated: false, upperArmHeld: false, cameraCount: 0,
                   faceOn: "absent", dtl: "absent" } },
        { tag: "7 two cameras, no sensors",
          facts: { faceOn: true, dtl: true, imuRoles: [], hackMotion: false, launchMonitor: false },
          ctx:   { armInSession: false, armCalibrated: false, upperArmHeld: false, cameraCount: 2,
                   faceOn: "connected", dtl: "connected" } },
        // Not one of setup_capability_test's: the face-on camera assigned but not connected (or
        // Cameras skipped) — the reasons say "not connected", and nothing sees the swing.
        { tag: "8 face-on assigned, not connected",
          facts: { faceOn: false, dtl: false, imuRoles: [], hackMotion: false, launchMonitor: false },
          ctx:   { armInSession: false, armCalibrated: false, upperArmHeld: false, cameraCount: 0,
                   faceOn: "unconnected", dtl: "absent" } }
    ]

    // Per setup, per family: the row as one line (CapRows.rows(...).text). Written by running
    // the seven setups at Stage 5b and reading every row; see the report for the review.
    readonly property var expected: ({
        "1 face-on only": [
            "Wrist — Partly, 1 of 11 · no wrist sensor",
            "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
            "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
            "Sequence and tempo — Recorded, 7 of 9 estimated · no wrist sensor · no trunk sensors",
            "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
            "Ball and strike — Not recorded · no launch monitor"
        ],
        "2 face-on + wG3": [
            "Wrist — Partly, 9 of 11 · no upper-arm sensor",
            "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
            "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
            "Sequence and tempo — Recorded, 6 of 9 estimated · no trunk sensors · no down-the-line camera",
            "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
            "Ball and strike — Not recorded · no launch monitor"
        ],
        "3 face-on + two Witmotions": [
            "Wrist — Partly, 6 of 11 · no upper-arm sensor",
            "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
            "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
            "Sequence and tempo — Recorded, 6 of 9 estimated · no trunk sensors · no down-the-line camera",
            "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
            "Ball and strike — Not recorded · no launch monitor"
        ],
        "4 two cameras + trunk": [
            "Wrist — Partly, 1 of 11 · no wrist sensor",
            "Body turn and posture — Recorded, 1 of 15 estimated",
            "Setup and stance — Measured",
            "Sequence and tempo — Recorded, 3 of 9 estimated · no wrist sensor",
            "Club — Partly, 12 of 21 · no launch monitor · estimated from the cameras",
            "Ball and strike — Not recorded · no launch monitor"
        ],
        "5 everything + launch monitor": [
            "Wrist — Partly, 9 of 11 · no upper-arm sensor",
            "Body turn and posture — Recorded, 1 of 15 estimated",
            "Setup and stance — Measured",
            "Sequence and tempo — Recorded, 1 of 9 estimated",
            "Club — Recorded, 1 of 21 estimated",
            "Ball and strike — Measured by the launch monitor"
        ],
        "6 nothing": [
            "Wrist — Not recorded · no wrist sensor · no face-on camera",
            "Body turn and posture — Not recorded · no trunk sensors · no face-on camera",
            "Setup and stance — Not recorded · no face-on camera · no down-the-line camera",
            "Sequence and tempo — Not recorded · no face-on camera · no wrist sensor",
            "Club — Not recorded · no launch monitor · no face-on camera",
            "Ball and strike — Not recorded · no launch monitor"
        ],
        "7 two cameras, no sensors": [
            "Wrist — Partly, 1 of 11 · no wrist sensor",
            "Body turn and posture — Recorded, 5 of 15 estimated · no trunk sensors",
            "Setup and stance — Measured",
            "Sequence and tempo — Recorded, 7 of 9 estimated · no wrist sensor · no trunk sensors",
            "Club — Partly, 12 of 21 · no launch monitor · estimated from the cameras",
            "Ball and strike — Not recorded · no launch monitor"
        ],
        "8 face-on assigned, not connected": [
            "Wrist — Not recorded · no wrist sensor · face-on camera not connected",
            "Body turn and posture — Not recorded · no trunk sensors · face-on camera not connected",
            "Setup and stance — Not recorded · face-on camera not connected · no down-the-line camera",
            "Sequence and tempo — Not recorded · face-on camera not connected · no wrist sensor",
            "Club — Not recorded · no launch monitor · face-on camera not connected",
            "Ball and strike — Not recorded · no launch monitor"
        ]
    })

    // As SetupContext calls it: with a launch monitor configured, the same setup without it too.
    function rowsFor(s) {
        var noLm = null
        if (s.facts.launchMonitor) {
            var f = {}
            for (var k in s.facts) f[k] = s.facts[k]
            f.launchMonitor = false
            noLm = catalog.setupSummary(f)
        }
        return CapRows.rows(catalog.setupSummary(s.facts), s.ctx, noLm)
    }

    // ── Families ────────────────────────────────────────────────────────────
    function test_familiesCoverTheCatalogueOnce() {
        var groups = catalog.groups
        verify(groups.length >= 10, "the catalogue's groups were not read: " + JSON.stringify(groups))
        for (var i = 0; i < groups.length; ++i)
            compare(CapRows.familiesNaming(groups[i]), 1, "group '" + groups[i] + "' is in "
                    + CapRows.familiesNaming(groups[i]) + " families")
        var fams = CapRows.families
        compare(fams.length, 6)
        for (var f = 0; f < fams.length; ++f)
            for (var g = 0; g < fams[f].groups.length; ++g)
                verify(groups.indexOf(fams[f].groups[g]) >= 0,
                       "family " + fams[f].key + " names '" + fams[f].groups[g] + "', which the catalogue does not have")
        compare(fams.map(function(x) { return x.label }),
                ["Wrist", "Body turn and posture", "Setup and stance", "Sequence and tempo", "Club", "Ball and strike"])
    }

    // ── The state rule ──────────────────────────────────────────────────────
    function test_stateRule_data() {
        return [
            { tag: "nothing",           m: 0, e: 0, u: 5, state: "none",      text: "Not recorded", tone: "muted" },
            { tag: "all planned",       m: 0, e: 0, u: 0, state: "none",      text: "Not recorded", tone: "muted" },
            { tag: "all measured",      m: 4, e: 0, u: 0, state: "measured",  text: "Measured",     tone: "good" },
            { tag: "all estimated",     m: 0, e: 3, u: 0, state: "estimated", text: "Estimated",    tone: "warn" },
            { tag: "all recorded, some estimated", m: 8, e: 7, u: 0, state: "recorded",
              text: "Recorded, 7 of 15 estimated", tone: "good" },
            { tag: "estimated or none", m: 0, e: 3, u: 2, state: "partly",    text: "Partly, 3 of 5", tone: "warn" },
            { tag: "partly",            m: 2, e: 1, u: 4, state: "partly",    text: "Partly, 3 of 7", tone: "warn" },
            { tag: "measured or none",  m: 2, e: 0, u: 4, state: "partly",    text: "Partly, 2 of 6", tone: "warn" }
        ]
    }
    function test_stateRule(row) {
        var st = CapRows.stateOf(row.m, row.e, row.u)
        compare(st, row.state)
        compare(CapRows.stateText(st, row.m, row.e, row.u), row.text)
        compare(CapRows.toneOf(st), row.tone)
    }

    // ── The reason vocabulary ───────────────────────────────────────────────
    function test_reasons_data() {
        var none = { armInSession: false }
        var uncal = { armInSession: true, armCalibrated: false }
        var noUpper = { armInSession: true, armCalibrated: true, upperArmHeld: false }
        var full = { armInSession: true, armCalibrated: true, upperArmHeld: true }
        return [
            { tag: "trunk",            gaps: ["bodyImus"],               arm: none,    want: ["no trunk sensors"] },
            { tag: "wrist none",       gaps: ["wristImus", "hackMotion"], arm: none,   want: ["no wrist sensor"] },
            { tag: "wrist uncal",      gaps: ["hackMotion"],             arm: uncal,   want: ["wrist sensor not calibrated"] },
            { tag: "wrist no upper",   gaps: ["wristImus"],              arm: noUpper, want: ["no upper-arm sensor"] },
            { tag: "wrist all held",   gaps: ["wristImus"],              arm: full,    want: ["no wrist sensor"] },
            { tag: "dtl",              gaps: ["dtl"],                    arm: none,    want: ["no down-the-line camera"] },
            { tag: "dtl unconnected",  gaps: ["dtl"],                    arm: { dtl: "unconnected" },
              want: ["down-the-line camera not connected"] },
            { tag: "face-on",          gaps: ["faceOn"],                 arm: none,    want: ["no face-on camera"] },
            { tag: "face-on unconnected", gaps: ["faceOn"],              arm: { faceOn: "unconnected" },
              want: ["face-on camera not connected"] },
            { tag: "launch monitor",   gaps: ["launchMonitor"],          arm: none,    want: ["no launch monitor"] },
            { tag: "club sensor",      gaps: ["clubSensor"],             arm: none,    want: [] },
            { tag: "camera products",  gaps: ["clubTrack", "ballTrack"], arm: none,    want: [] },
            { tag: "at most two, in order", gaps: ["dtl", "faceOn", "launchMonitor", "bodyImus", "wristImus"],
              arm: none, want: ["no wrist sensor", "no trunk sensors"] },
            { tag: "cameras last",     gaps: ["dtl", "faceOn"],          arm: none,
              want: ["no face-on camera", "no down-the-line camera"] }
        ]
    }
    function test_reasons(row) {
        compare(CapRows.reasonsFor(row.gaps, row.arm), row.want)
    }
    function test_estimatedNote() {
        compare(CapRows.estimatedNote(2), "estimated from the cameras")
        compare(CapRows.estimatedNote(1), "estimated from the camera")
        compare(CapRows.estimatedNote(0), "estimated")
        // A measured family says nothing more.
        var rows = CapRows.rows([{ group: "Strike", measured: 3, estimated: 0, unavailable: 0, derived: 0,
                                   deviceGapIds: [] }], { cameraCount: 1, faceOn: "connected" })
        compare(rows[5].text, "Ball and strike — Measured")
        compare(rows[5].reason, "")
        // "Recorded, E of T estimated" carries no estimate note: the state already says it.
        rows = CapRows.rows([{ group: "Body rotation", measured: 2, estimated: 3, unavailable: 0, derived: 0,
                               deviceGapIds: ["bodyImus"] }], { cameraCount: 2, faceOn: "connected" })
        compare(rows[1].text, "Body turn and posture — Recorded, 3 of 5 estimated · no trunk sensors")
        // Derived metrics count as measured only in Sequence and tempo, and only when something
        // sees the swing (a connected face-on camera or an arm sensor group) — else unavailable.
        var summary = [{ group: "Tempo & sequence", measured: 0, estimated: 0, unavailable: 3, derived: 2,
                         deviceGapIds: ["bodyImus"] },
                       { group: "Head", measured: 0, estimated: 0, unavailable: 3, derived: 2,
                         deviceGapIds: ["dtl"] }]
        rows = CapRows.rows(summary, { cameraCount: 1, faceOn: "connected" })
        compare(rows[3].text, "Sequence and tempo — Partly, 2 of 5 · no trunk sensors")
        compare(rows[2].text, "Setup and stance — Not recorded · no down-the-line camera")
        rows = CapRows.rows(summary, { cameraCount: 0, faceOn: "absent", armInSession: true, armCalibrated: true })
        compare(rows[3].text, "Sequence and tempo — Partly, 2 of 5 · no trunk sensors", "a calibrated arm group sees the swing")
        // …but only once calibrated: an uncalibrated sensor yields no usable lanes.
        rows = CapRows.rows(summary, { cameraCount: 0, faceOn: "absent", armInSession: true, armCalibrated: false })
        compare(rows[3].text, "Sequence and tempo — Not recorded · no face-on camera · no trunk sensors",
                "an uncalibrated arm group sees nothing")
        rows = CapRows.rows(summary, { cameraCount: 0, faceOn: "unconnected" })
        compare(rows[3].text, "Sequence and tempo — Not recorded · face-on camera not connected · no trunk sensors")
    }

    // ── The seven setups ────────────────────────────────────────────────────
    function test_setups_data() { return setups.map(function(s) { return { tag: s.tag, setup: s } }) }
    function test_setups(row) {
        var rows = rowsFor(row.setup)
        var got = rows.map(function(r) { return r.text })
        console.info("[CAP] " + row.tag + " " + JSON.stringify(got))
        var want = expected[row.tag]
        verify(want !== undefined, "no expected rows for " + row.tag)
        compare(got, want, row.tag)
    }

    // ── Whose word it is (Stage 5d) ─────────────────────────────────────────
    // With a launch monitor merely configured, a family the launch monitor ALONE measures says so,
    // with the device's name when the connector gives one; a family it only adds to (Club) does not.
    function test_measuredByTheLaunchMonitor() {
        var s = { facts: { faceOn: true, dtl: true, imuRoles: ["pelvis", "thorax", "leadForearm", "leadHand"],
                           hackMotion: true, launchMonitor: true },
                  ctx: { armInSession: true, armCalibrated: true, upperArmHeld: false, cameraCount: 2,
                         faceOn: "connected", dtl: "connected", lmName: "GSPro" } }
        var rows = rowsFor(s)
        compare(rows[5].text, "Ball and strike — Measured by the launch monitor (GSPro)")
        compare(rows[5].tone, "good")
        verify(rows[4].stateText.indexOf("launch monitor") < 0, "Club claims the launch monitor: " + rows[4].text)
        s.ctx.lmName = ""
        compare(rowsFor(s)[5].text, "Ball and strike — Measured by the launch monitor")
        // No "without" summary (none configured): plain words.
        compare(CapRows.rows(catalog.setupSummary(s.facts), s.ctx)[5].text, "Ball and strike — Measured")
    }

    // ── Nothing an engineer would write ─────────────────────────────────────
    function test_noEngineerWords() {
        var texts = []
        for (var i = 0; i < setups.length; ++i) {
            var rows = rowsFor(setups[i])
            for (var j = 0; j < rows.length; ++j) texts.push(rows[j].label, rows[j].stateText, rows[j].reason, rows[j].text)
        }
        for (var k in CapRows.reasons) texts.push(CapRows.reasons[k])
        texts.push(CapRows.estimatedNote(0), CapRows.estimatedNote(1), CapRows.estimatedNote(2))
        var roleRe = /(lead|trail)\s*(forearm|hand|upper\s*arm)|LeadForearm|LeadHand|LeadUpperArm|pelvis|thorax|Pelvis|Thorax/
        for (var t = 0; t < texts.length; ++t) {
            var s = texts[t]
            verify(!roleRe.test(s), "a role name in '" + s + "'")
            verify(s.indexOf("IMU") < 0, "'IMU' in '" + s + "'")
            verify(!/\bmove\b/i.test(s), "'move' in '" + s + "'")
            verify(!/\b[A-D]\b/.test(s), "a slot letter in '" + s + "'")
        }
    }
}
