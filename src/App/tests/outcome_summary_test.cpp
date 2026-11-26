/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// THE CAROUSEL'S SHOT SUMMARY (refactor 4-b; build/refactor/step-4/BRIEF-b.md deliverable 3): app.state.shotOutcome
// over the REAL session store, swing door, Athlete, Session, Shot and SessionDiagnostics, the real firing reader and
// fireFindings (synchronous doors: every ask comes back inline), on a temporary library — never the share.
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT: Mark's request (14 Nov 2026: strike · direction · flight · carry,
// launch-monitor shots only), the 4-b decisions and R-APP-7 (Mark, 22 Nov 2026, DEC-196: the summary's shot is
// app.state.outcome — the selection's on a pick, move or clear; the newest shot the session on screen adds, even over an
// older selection; a re-analysis does not move it; "-" for not read and not judged yet; carry from the owner's copy of
// the reading) and DEC-96 ("-", never a zero). The words are compared with the owner's own kept factors put through
// shot_factor_words.h, so the test proves the summary is a view of the Shot's diagnostics, not a second rule.
//
//   V   the values come from the Shot's diagnostics (ShotDiagnostics → its Session's owner), carry in yards
//   RE  they update on a re-analysis (EVT-02: the owner re-fires the shot) in place, and the change is announced
//   ON  auto-replay on: the new shot is selected (Main.qml's onShotProcessed), and the summary is the selection's
//   OFF auto-replay off, nothing selected: a new shot lands, and the summary is that shot's, named
//   OLD auto-replay off, an older shot selected: a new shot lands, and the summary moves to it, named
//   CL  a click after that: the summary moves to the clicked shot
//   RN  a re-analysis of another shot does not move the summary
//   SC  the session on screen replaced: the outcome is cleared with it; a ShotAdded from the session that left
//       the screen is dropped
//   AS  an athlete switch clears the outcome (refreshAthlete)
//   NL  a shot with no launch-monitor reading: no summary, landed or selected
//   W   the words: a combined miss once, never its halves; several values " + "; "-" with no tone when not read
//   QML the typed value reads in a QQmlEngine (the QML boundary, R-QML-2)
//
//   cmake --build build/tests --target outcome_summary_test -j 6
//   ctest --test-dir build/tests -R '^outcome_summary_test$' --output-on-failure

#include "../app_state.h"
#include "../outcome_summary.h"
#include "../../Diagnostics/derived/shot_factor_words.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"
#include "../../Core/work_pool.h"
#include "../../Stores/firing_reader_store.h"
#include "../../Stores/recording_loader.h"   // SwingStoreRecordingReader
#include "../../Buffer/swing_window.h"     // a LoadedRecording owns one
#include "../../Stores/rule_data_install.h"
#include "../../Stores/session_store.h"
#include "../../Stores/swing_store_repository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QQmlEngine>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <memory>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static const AthleteId kAthlete = AthleteId::fromString(u"5a1d9c3e-2b7f-4e8a-9c10-7d2e6f3b4a21");
static const QString   kName    = QStringLiteral("Test Golfer");

static QString fixture(const char *name)
{
    return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
}

// One scratch library with the app's doors, headless (synchronous), and an AppState whose session on screen is it.
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;
    std::unique_ptr<Athlete>                       athlete;
    AppState                                       state;
    Session                                       *session = nullptr;
    QString                                        folder;
    int                                            changes = 0;   // shotOutcomeChanged heard

    Rig()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, dir.path() + QStringLiteral("/Test-Golfer"), SessionOwner{ kAthlete, kName, dir.path() },
                           Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        DiagnosticsDoors d;
        d.receiver        = receiver.get();
        d.ruleData        = RuleData::installed();
        d.reader          = std::make_shared<SwingStoreFiringReader>(store, *recordings);
        d.persist         = false;
        d.synchronous     = true;
        d.attach          = &SessionDiagnostics::attach;
        athlete->setDiagnosticsDoors(d);

        session = athlete->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        if (const auto e = session ? store.open(session->id()) : std::nullopt) folder = e->folder;
        AppStateDoors doors;
        doors.sessionAt        = [this](const QString &f) { return f == folder ? session : nullptr; };
        doors.currentAthlete   = [this] { return athlete.get(); };   // its latest session, on a switch (step 5.2)
        doors.shotOutcome         = &outcomeSummaryOf;
        doors.watchOutcomeSummary = &watchOutcomeSummary;
        state.setDoors(std::move(doors));
        state.setLiveFolder(folder);   // the live session (AppState's own since refactor step 5.2)
        QObject::connect(&state, &AppState::shotOutcomeChanged, &state, [this] { ++changes; });
    }
    ~Rig() { athlete.reset(); }

    // Capture writes the shot's document, then says so (EVT-01, D19): the owner fires it inline.
    Shot *land(const char *doc)
    {
        Shot *shot = session ? session->addShot() : nullptr;
        if (!shot) return nullptr;
        QFile::copy(fixture(doc), QDir(shot->folder()).filePath(QStringLiteral("swing.json")));
        session->shotDocumentWritten(shot->ref());
        return shot;
    }
    void pick(Shot *shot) { state.enterReplay(shot->ref().toString(), shot->folder()); }   // the carousel's click path
};

static QString carryOf(const KeptShot *k)
{
    return k && std::isfinite(k->findings.carry) ? QString::number(k->findings.carry, 'f', 1) + QStringLiteral(" yd")
                                                 : QStringLiteral("-");
}

// The summary is the owner's kept factors in shot_factor_words.h's words (V's contract).
static bool sameAsKept(const ShotOutcomeSummary &o, SessionDiagnostics *d, int swing)
{
    const KeptShot *k = d ? d->kept(swing) : nullptr;
    if (!k || !k->derived) return false;
    const analysis::ShotFactors &f = k->derived->data.factors;
    const analysis::ShotOutcomePack *pack = d->snapshot()->outcomePack.get();
    const auto s  = analysis::shotFactorWords(f.strike, analysis::shotFactorStrike(), pack);
    const auto di = analysis::shotFactorWords(f.direction, analysis::shotFactorDirection(), pack);
    const auto fl = analysis::shotFactorWords(f.flight, analysis::shotFactorFlight(), pack);
    return o.strikeText == s.text && o.strikeTone == s.tone && o.directionText == di.text && o.directionTone == di.tone
        && o.flightText == fl.text && o.flightTone == fl.tone && o.carryText == carryOf(k);
}

static void testValuesAndReanalysis()
{
    std::printf("\nV / RE — the values are the Shot's diagnostics', and follow a re-analysis\n");
    Rig rig;
    Shot *one = rig.land("lm_7iron");
    SessionDiagnostics *d = rig.session ? rig.session->diagnostics() : nullptr;
    check(QStringLiteral("V0 the shot fired inline, with its derived data"), one && d && d->kept(1) && d->kept(1)->derived);
    if (!one || !d || !d->kept(1)) return;
    check(QStringLiteral("V1 the Shot's view reads its owner: fired, a launch-monitor shot, its factors and findings"),
          one->diagnostics().fired() && one->diagnostics().hasLaunchMonitor() && one->diagnostics().factors()
          && one->diagnostics().findings() == &d->kept(1)->findings);
    rig.pick(one);
    ShotOutcomeSummary o = rig.state.shotOutcome();
    check(QStringLiteral("V2 the selected launch-monitor shot is summarised, as the selection"),
          o.shown && o.selected && o.judged && o.swingNumber == 1 && o.shotRef == one->ref().toString());
    check(QStringLiteral("V3 its words and tones are its kept factors' (%1 · %2 · %3)").arg(o.strikeText, o.directionText, o.flightText),
          sameAsKept(o, d, 1));
    check(QStringLiteral("V4 carry is the reading's, in yards: %1").arg(o.carryText), o.carryText == QStringLiteral("164.5 yd"));
    check(QStringLiteral("V5 no value is blank or a bare 0"),
          !o.strikeText.isEmpty() && !o.directionText.isEmpty() && !o.flightText.isEmpty()
          && o.strikeText != QLatin1String("0") && o.carryText != QLatin1String("0 yd"));

    // RE: the re-analysis writes a new document (here another launch-monitor shot's), then says so (EVT-02).
    const QString doc = QDir(one->folder()).filePath(QStringLiteral("swing.json"));
    QFile::remove(doc);
    QFile::copy(fixture("lm_partial"), doc);
    const int before = rig.changes;
    emit one->reanalysed(one->ref());
    o = rig.state.shotOutcome();
    check(QStringLiteral("RE1 the owner re-fired the shot, and the summary said so (%1 change(s))").arg(rig.changes - before),
          rig.changes > before);
    check(QStringLiteral("RE2 the summary now reads the re-analysed shot, in place: carry %1").arg(o.carryText),
          o.shown && o.swingNumber == 1 && o.carryText == QStringLiteral("183.9 yd") && sameAsKept(o, d, 1));
}

static void testWhichShot()
{
    std::printf("\nON / OFF / OLD / CL / RN / SC / NL — which shot the summary is (R-APP-7)\n");
    Rig rig;
    Shot *one = rig.land("lm_7iron");
    if (!one) { check(QStringLiteral("a shot landed"), false); return; }

    // ON: auto-replay after capture selects the shot just processed (Main.qml onShotProcessed → enterReplay).
    rig.pick(one);
    ShotOutcomeSummary o = rig.state.shotOutcome();
    check(QStringLiteral("ON the new shot, selected: the summary is the selection's (not named again)"),
          o.shown && o.selected && o.swingNumber == 1 && rig.state.outcome()->swingNumber() == 1);

    // OFF: nothing selected (capture, auto-replay off); a new shot lands.
    rig.state.clearSelection();
    check(QStringLiteral("OFF0 a clear clears the outcome too: no summary"), !rig.state.outcome()->valid() && !rig.state.shotOutcome().shown);
    int before = rig.changes;
    Shot *two = rig.land("lm_partial");
    o = rig.state.shotOutcome();
    check(QStringLiteral("OFF1 the landing moved the summary (%1 change(s))").arg(rig.changes - before), rig.changes > before);
    check(QStringLiteral("OFF2 the new shot's outcome, named (swing %1, carry %2)").arg(o.swingNumber).arg(o.carryText),
          two && o.shown && !o.selected && o.swingNumber == 2 && o.carryText == QStringLiteral("183.9 yd"));

    // OLD: an older shot selected; a new shot lands.
    rig.pick(one);
    check(QStringLiteral("OLD0 the pick moved the outcome to the selection"), rig.state.shotOutcome().selected && rig.state.shotOutcome().swingNumber == 1);
    Shot *three = rig.land("lm_7iron");
    o = rig.state.shotOutcome();
    check(QStringLiteral("OLD1 the new shot wins over the older selection: swing %1, named").arg(o.swingNumber),
          three && o.shown && !o.selected && o.swingNumber == 3);
    check(QStringLiteral("OLD2 …and the selection itself did not move (swing %1)").arg(rig.state.selection()->swingNumber()),
          rig.state.selection()->swingNumber() == 1);

    // CL: a click after that.
    rig.pick(two);
    o = rig.state.shotOutcome();
    check(QStringLiteral("CL the click moves the summary to the clicked shot (swing %1)").arg(o.swingNumber),
          o.shown && o.selected && o.swingNumber == 2);

    // RN: a re-analysis of another shot.
    emit three->reanalysed(three->ref());
    o = rig.state.shotOutcome();
    check(QStringLiteral("RN a re-analysis of swing 3 does not move the summary (swing %1)").arg(o.swingNumber),
          o.shown && o.selected && o.swingNumber == 2 && rig.state.outcome()->swingNumber() == 2);

    // NL: a shot with no launch-monitor reading lands, then is selected.
    Shot *four = rig.land("sparse_noclub");
    o = rig.state.shotOutcome();
    check(QStringLiteral("NL1 the newest shot has no reading: the outcome is it, and there is no summary"),
          four && rig.state.outcome()->swingNumber() == 4 && !o.shown);
    rig.pick(four);
    check(QStringLiteral("NL2 selected, it has no summary either"), !rig.state.shotOutcome().shown);
    rig.pick(three);
    check(QStringLiteral("NL3 a launch-monitor shot selected again: summarised"), rig.state.shotOutcome().shown);

    // AS: an athlete switch (the composition root's refreshAthlete) clears it, though the session on screen is the same.
    Shot *five = rig.land("lm_7iron");
    check(QStringLiteral("AS0 a shot landed: the outcome is it"), five && rig.state.outcome()->swingNumber() == 5);
    rig.state.refreshAthlete();
    check(QStringLiteral("AS an athlete switch clears the outcome: no summary"),
          !rig.state.outcome()->valid() && !rig.state.shotOutcome().shown);
    rig.pick(three);

    // SC: another session on screen (the athlete's next session).
    QPointer<Session> left = rig.session;
    const ShotRef leftShot = three->ref();
    Session *next = rig.athlete->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
    const auto e = next ? rig.store.open(next->id()) : std::nullopt;
    if (e) { rig.session = next; rig.folder = e->folder; }
    rig.state.setLiveFolder(rig.folder);
    check(QStringLiteral("SC the session on screen replaced: the outcome is cleared with it"),
          e && !rig.state.outcome()->valid() && !rig.state.shotOutcome().shown);
    // A ShotAdded from the session that left the screen (as if it were late): its connection went with the old watch,
    // and a ref of another session never resolves through the one on screen.
    if (left) emit left->shotAdded(leftShot);
    check(QStringLiteral("SC2 a ShotAdded from the session no longer on screen is dropped (it was %1)")
              .arg(left ? QStringLiteral("emitted") : QStringLiteral("released")),
          !rig.state.outcome()->valid() && !rig.state.shotOutcome().shown);
}

static void testWords()
{
    std::printf("\nW — the words\n");
    using namespace analysis;
    ShotFactor dir;
    dir.read   = true;
    dir.values = { QStringLiteral("push_slice"), QStringLiteral("slice"), QStringLiteral("push") };
    dir.faults = dir.values;
    ShotFactorWords w = shotFactorWords(dir, shotFactorDirection(), nullptr);
    check(QStringLiteral("W1 a push slice once, never its halves: '%1' (%2)").arg(w.text, w.tone),
          w.text == QStringLiteral("Push slice") && w.tone == QStringLiteral("fault"));
    ShotFactor strike;
    strike.read   = true;
    strike.values = strike.faults = { QStringLiteral("strike_toe"), QStringLiteral("smash_deficit") };
    w = shotFactorWords(strike, shotFactorStrike(), nullptr);
    check(QStringLiteral("W2 two values joined: '%1'").arg(w.text), w.text == QStringLiteral("Toe + Weak strike") && w.tone == QStringLiteral("fault"));
    ShotFactor low;
    low.read   = true;
    low.values = { QStringLiteral("low") };
    w = shotFactorWords(low, shotFactorFlight(), nullptr);
    check(QStringLiteral("W3 Low is a value to watch"), w.text == QStringLiteral("Low") && w.tone == QStringLiteral("watch"));
    ShotFactor solid;
    solid.read   = true;
    solid.values = { QStringLiteral("solid") };
    w = shotFactorWords(solid, shotFactorStrike(), nullptr);
    check(QStringLiteral("W4 Solid is good"), w.text == QStringLiteral("Solid") && w.tone == QStringLiteral("good"));
    w = shotFactorWords(ShotFactor{}, shotFactorFlight(), nullptr);
    check(QStringLiteral("W5 not read: \"-\" with no tone (DEC-96)"), w.text == QStringLiteral("-") && w.tone.isEmpty());
    const ShotOutcomeSummary none;
    check(QStringLiteral("W6 a summary not judged yet says \"-\" for every value"),
          none.strikeText == QLatin1String("-") && none.directionText == QLatin1String("-")
          && none.flightText == QLatin1String("-") && none.carryText == QLatin1String("-"));
}

static void testQml()
{
    std::printf("\nQML — the typed value through a QQmlEngine\n");
    Rig rig;
    Shot *one = rig.land("lm_7iron");
    if (!one) { check(QStringLiteral("a shot landed"), false); return; }
    rig.pick(one);
    QQmlEngine engine;
    QQmlEngine::setObjectOwnership(&rig.state, QQmlEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("state"), engine.newQObject(&rig.state));
    const QJSValue v = engine.evaluate(QStringLiteral(
        "state.shotOutcome.shown && state.shotOutcome.selected && state.shotOutcome.swingNumber === 1"
        " && state.shotOutcome.carryText === '164.5 yd' && state.shotOutcome.strikeText.length > 0"
        " && typeof state.shotOutcome.flightTone === 'string' && state.outcome.swingNumber === 1 && state.outcome.valid"));
    check(QStringLiteral("QML state.shotOutcome reads its fields (%1)").arg(v.toString()), v.toBool());
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    std::printf("outcome_summary_test\n");
    RuleData &rules = installProcessRuleData();
    check(QStringLiteral("the shipped rule data is installed"), rules.snapshot() && rules.snapshot()->pack);
    testWords();
    testValuesAndReanalysis();
    testWhichShot();
    testQml();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
