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

// APP STATE (refactor step 4-a; src/App/app_state.h; information_model.md §6, R-APP-1…6, R-QML-1). Over recording
// doors — no app, no GUI — every expectation from a stated contract:
//
//   S1  THE SELECTION (R-APP-4): a pick sets the selection BEFORE the mode (a panel rebuilt on the mode switch finds
//       it); the replay loads it, playing only from capture or with "Auto-replay after select"; a re-pick of the
//       loaded shot does not restart it; a clear stops the replay; the cast follows the selection (R-APP-5, Q-07: J-a1)
//   S2  THE MODE: Capture keeps a live selection and drops a reviewed one (leaving the review); Analyse keeps Analyse
//       on a figure's pick; the post-shot replay returns to Capture at its end, a picked one does not
//   S3  SCOPED TO THE SESSION ON SCREEN (R-APP-3, E-05, E-08): the reviewed session, else the capture target, else the
//       live carousel's; a different session clears the selection (and the replay of it), the same one does not
//   S4  START AND END ARE ONE COMMAND EACH (E-12): Start opens the capture session, enters Capture (#283; S4b), then
//       shows it (empty when new, the resumed folder on extend), then the clock, then the cameras; End stops the
//       cameras, the clock, ends the session (its empty discard) and only then reloads the carousel — and not at all
//       for the close
//   S4c EVERY WAY INTO A RUNNING SESSION LANDS IN CAPTURE (#283): the toolbar's re-arm after a Stop (then the cameras,
//       nothing opened), and Home's Start while a session runs (with none running it changes nothing)
//   S5  NO ATHLETE SWITCH WHILE A SESSION RUNS (R-APP-1, the rule that replaced Stage 0.8g's check)
//   S7  BY SessionId, OVER REAL SESSIONS: another spelling of the same session folder keeps the selection, another
//       session clears it, and so does an athlete switch (the folder is not the new athlete's)
//   S8  ENDING AN EMPTY SESSION: the End discards it; the session on screen is then today's remaining session, and the
//       trashed folder is never asked for again (no Session is ever made for it: session_test S3i–k)
//   S9  A RELOAD OF THE SAME SESSION KEEPS THE SELECTION AND THE REPLAY (review B3): the live folder passing through
//       empty (a carousel reload) is not another session; End of a session with shots leaves the same one on screen;
//       an athlete switch with nothing on screen still clears it
//   S10 A SHOT IS SAVING (#281 B, #188, #280; cabin visit 1; Solution Architect, 25 Nov 2026): app.state.shotSaving is
//       the capture service's fact, its change announced queued (never inside the capture service's own state change)
//       and once. End is one immediate command; the views wait for shotSaving to fall, then call it (emulated here as
//       the End card and the close prompt do). The toolbar's End releases the devices last, the close does not.
//       Over real Sessions: waiting, the shot lands in the session that was running, which End keeps; End anyway drops
//       it and the empty session is discarded (the golfer's choice); Cancel leaves the session running and nothing
//       ends when the shot lands
//   S6  THE QML BOUNDARY (R-QML-2): app.state's members, its commands from JavaScript, and the typed values QML now
//       reads — a gadget's fields and a list of gadgets used as an array, a QList<int> as an array

#include "../app.h"
#include "../app_state.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../Export/recording_loader.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/shot/replay_source.h"

#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QQmlEngine>
#include <QJSValue>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

static int g_fail = 0;

static void check(const QString &label, bool ok)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

namespace {

struct Rig {
    pinpoint::AppState state;
    QStringList calls;                 // the doors, in the order they were called
    QString review, capture;           // two of the folders sessionOnScreen chooses between (the third: setLiveFolder)
    QString loaded;                    // the replay's folder
    bool autoSelect = false, running = false;
    bool inFlight = false;             // a shot between its impact and its document write (S10)

    Rig()
    {
        pinpoint::AppStateDoors d;
        d.sessionRunning = [this] { return running; };
        d.reviewFolder   = [this] { return review; };
        d.captureFolder  = [this] { return capture; };
        d.resumeLive     = [this] { calls << "resumeLive"; review.clear(); };
        d.replayStart    = [this](int n, const QString &dir, bool play) {
            calls << QStringLiteral("start %1 %2 %3").arg(n).arg(dir, play ? "play" : "paused");
            loaded = dir;
        };
        d.replayStop     = [this] { calls << "stop"; loaded.clear(); };
        d.replayFolder   = [this] { return loaded; };
        d.autoReplayAfterSelect = [this] { return autoSelect; };
        d.openCaptureSession  = [this](bool extend) { calls << (extend ? "open extend" : "open new"); capture = "/lib/a/S2"; };
        d.startClock          = [this] { calls << "clock"; running = true; };
        d.startCameras        = [this] { calls << "cameras"; };
        d.stopCameras         = [this] { calls << "cameras off"; };
        d.stopClock           = [this] { calls << "clock off"; running = false; };
        d.closeCaptureSession = [this] { calls << "close"; capture.clear(); };
        d.todaysFolder        = [this] { calls << "today"; return QStringLiteral("/lib/a/S1"); };
        d.shotInFlight        = [this] { return inFlight; };
        d.releaseDevices      = [this] { calls << "release"; };
        state.setDoors(d);
    }
};

const QString kShot2 = QStringLiteral("3c852070-1e72-591c-97e1-85b5df9bc128/2");
const QString kDir2  = QStringLiteral("/lib/a/S1/swing_0002");

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    std::printf("app_state_test — AppState (refactor step 4-a)\n");

    std::printf("\nS1 the selection\n");
    {
        Rig r;
        int modeAtPick = -1;
        QObject::connect(r.state.selection(), &pinpoint::ShotSelection::changed, [&] { modeAtPick = r.state.mode(); });
        r.state.enterReplay(kShot2, kDir2);
        check("S1 the selection holds the ShotRef and its folder",
              r.state.selection()->shotRef() == kShot2 && r.state.selection()->swingNumber() == 2
                  && r.state.selection()->swingDir() == kDir2);
        check("S1 …set while the mode was still Capture (before the mode switch)", modeAtPick == pinpoint::AppState::Capture);
        check("S1 …and the replay loads it, paused (Auto-replay after select off)",
              r.calls == QStringList{ "start 2 " + kDir2 + " paused" });
        r.calls.clear();
        r.state.enterReplay(kShot2, kDir2);
        check("S1 a re-pick of the loaded shot does not restart it", r.calls.isEmpty());
        check("S1 the cast follows the selection (J-a1)", r.state.castSelection()->shotRef() == kShot2);
        r.state.clearSelection();
        check("S1 a clear empties both selections and stops the replay",
              !r.state.selection()->valid() && !r.state.castSelection()->valid() && r.calls == QStringList{ "stop" });
        r.calls.clear();
        r.autoSelect = true;
        r.state.enterReplay(kShot2, kDir2);
        check("S1 with Auto-replay after select on, a pick plays", r.calls == QStringList{ "start 2 " + kDir2 + " play" });
    }

    std::printf("\nS2 the mode\n");
    {
        Rig r;
        r.state.enterReplay(kShot2, kDir2, /*fromCapture=*/true);
        check("S2 the post-shot replay plays and is marked to return", r.state.autoReturnToCapture()
              && r.calls.last().endsWith("play"));
        r.state.replayEnded();
        check("S2 …and at its end the stage is back in Capture, the live selection kept",
              r.state.mode() == pinpoint::AppState::Capture && r.state.selection()->valid());
        r.state.enterReplay(kShot2, kDir2);
        r.state.replayEnded();
        check("S2 a picked replay reaching its end stays in Replay", r.state.mode() == pinpoint::AppState::Replay);
        r.state.enterAnalyse();
        r.state.pickShot(QString(), QStringLiteral("/lib/a/S1/swing_0003"));
        check("S2 a figure's pick in Analyse keeps Analyse", r.state.mode() == pinpoint::AppState::Analyse
              && r.state.selection()->swingDir().endsWith("swing_0003"));
        r.review = QStringLiteral("/lib/a/S0");
        r.calls.clear();
        r.state.enterCapture();
        check("S2 Capture from a review drops the reviewed selection and leaves the review",
              !r.state.selection()->valid() && r.calls.contains("resumeLive"));
        r.state.enterLoadedSession();
        check("S2 a loaded session opens in Replay with nothing selected",
              r.state.mode() == pinpoint::AppState::Replay && !r.state.selection()->valid());
    }

    std::printf("\nS3 scoped to the session on screen\n");
    {
        Rig r;
        r.state.setLiveFolder(QStringLiteral("/lib/a/S1"));
        check("S3 nothing captured or reviewed: the live session", r.state.sessionOnScreenFolder() == QStringLiteral("/lib/a/S1"));
        r.state.enterReplay(kShot2, kDir2);
        r.capture = QStringLiteral("/lib/a/S1");   // an extend: the same session becomes the capture target
        r.state.refreshCaptureTarget();
        check("S3 the same session (an extend) keeps the selection", r.state.selection()->valid());
        r.calls.clear();
        r.capture = QStringLiteral("/lib/a/S2");   // a new session starts (E-05's scenario)
        r.state.refreshCaptureTarget();
        check("S3 the capture target comes before the live carousel", r.state.sessionOnScreenFolder() == r.capture);
        check("S3 a new session clears the selection, and the replay of it",
              !r.state.selection()->valid() && r.calls == QStringList{ "stop" });
        r.review = QStringLiteral("/lib/a/S0");
        r.state.refreshSessionOnScreen();
        check("S3 a review comes first", r.state.sessionOnScreenFolder() == r.review);
    }

    std::printf("\nS4 start and end, one command each\n");
    {
        Rig r;
        // The live carousel is a view of app.state's live session (refactor step 5.2): the capture target, else the live
        // folder, announced by liveSessionChanged.
        QStringList live;
        QObject::connect(&r.state, &pinpoint::AppState::liveSessionChanged, [&] { live << r.state.liveSessionFolder(); });
        r.state.startSession(false);
        check("S4 Start: the session, the clock, the cameras",
              r.calls == QStringList{ "open new", "stop", "clock", "cameras" });
        check("S4 …the live carousel shows the new session (the capture target), nothing before it",
              live == QStringList{ "/lib/a/S2" } && r.state.liveFolder() == QStringLiteral("/lib/a/S2"));
        r.calls.clear();
        live.clear();
        r.state.startSession(true);
        check("S4 Start (extend): the carousel shows the resumed session",
              r.calls == QStringList{ "open extend", "stop", "clock", "cameras" } && r.state.liveFolder() == QStringLiteral("/lib/a/S2"));
        r.calls.clear();
        r.state.endSession(true);
        check("S4 End: cameras, clock, the session's end — and only then today's session, then the devices' release",
              r.calls == QStringList{ "cameras off", "clock off", "close", "today", "release" }
                  && r.state.liveSessionFolder() == QStringLiteral("/lib/a/S1") && live == QStringList{ "/lib/a/S1" });
        r.calls.clear();
        r.capture = QStringLiteral("/lib/a/S2");
        r.state.refreshCaptureTarget();
        live.clear();
        r.state.endSession(false);
        check("S4 the close ends the same way, keeping the ended session on the carousel",
              r.calls == QStringList{ "cameras off", "clock off", "close" } && live.isEmpty()
                  && r.state.liveSessionFolder() == QStringLiteral("/lib/a/S2"));
    }

    // #283 (cabin visit 1): Start from an old session in Replay left the app on that session's Replay.
    std::printf("\nS4b a session starts in Capture\n");
    for (const bool extend : { false, true }) {
        const QString how = extend ? QStringLiteral(" (extend)") : QString();
        {
            Rig r;
            r.review = QStringLiteral("/lib/a/S0");   // an old session, opened from Home
            r.state.enterLoadedSession();
            r.state.enterReplay(QString(), QStringLiteral("/lib/a/S0/swing_0001"));
            r.calls.clear();
            r.state.startSession(extend);
            check("S4b Start from a reviewed session in Replay: Capture" + how, r.state.mode() == pinpoint::AppState::Capture);
            check("S4b …the review is left and its replay stopped" + how,
                  r.calls.contains("resumeLive") && r.calls.contains("stop") && r.loaded.isEmpty());
            check("S4b …the session on screen is the new one, nothing of the old selected" + how,
                  r.state.sessionOnScreenFolder() == r.capture && !r.state.selection()->valid());
        }
        {
            Rig r;
            r.state.setLiveFolder(QStringLiteral("/lib/a/S1"));   // the latest session, on the live carousel
            r.state.enterAnalyse();
            r.state.startSession(extend);
            check("S4b Start from Analyse with no review: Capture" + how, r.state.mode() == pinpoint::AppState::Capture
                  && !r.calls.contains("resumeLive"));
        }
    }

    // #283, the coordinator's ruling (25 Nov 2026): the two ways back into a running session land in Capture too.
    std::printf("\nS4c every way into a running session lands in Capture\n");
    {
        Rig r;
        r.state.startSession(false);   // the command's whole contract: from a review too (an old session, in Replay)
        r.review = QStringLiteral("/lib/a/S0");
        r.state.refreshSessionOnScreen();   // the composition root, as the review opens
        r.state.enterLoadedSession();
        r.state.enterReplay(QString(), QStringLiteral("/lib/a/S0/swing_0001"));
        r.calls.clear();
        r.state.rearmCapture();
        check("S4c the toolbar's re-arm from a reviewed session in Replay: Capture", r.state.mode() == pinpoint::AppState::Capture);
        check("S4c …the review is left and its replay stopped, then the cameras, and nothing opened",
              r.calls == QStringList{ "stop", "resumeLive", "cameras" } && r.loaded.isEmpty());
        r.state.refreshSessionOnScreen();   // the composition root, as the review is left (reviewActiveChanged)
        check("S4c …the session on screen is the running one, nothing of the old selected",
              r.state.sessionOnScreenFolder() == r.capture && !r.state.selection()->valid());
    }
    {
        Rig r;
        r.state.startSession(false);   // a session runs; after a Stop, a shot of it picked in Replay (no review: the
        r.state.enterReplay(QString(), QStringLiteral("/lib/a/S2/swing_0001"));   // toolbar hides Capture in a review)
        r.calls.clear();
        r.state.rearmCapture();
        check("S4c the toolbar's re-arm from the running session's Replay: Capture, the replay stopped, then the cameras",
              r.state.mode() == pinpoint::AppState::Capture && r.calls == QStringList{ "stop", "cameras" }
                  && r.loaded.isEmpty() && r.state.sessionOnScreenFolder() == r.capture);
    }
    {
        Rig r;
        r.state.startSession(false);
        r.state.enterAnalyse();        // Analyse on the running session, no review
        r.calls.clear();
        r.state.rearmCapture();
        check("S4c the toolbar's re-arm from Analyse: Capture, then the cameras",
              r.state.mode() == pinpoint::AppState::Capture && r.calls == QStringList{ "stop", "cameras" });
    }
    {
        Rig r;
        r.state.startSession(false);
        r.review = QStringLiteral("/lib/a/S0");
        r.state.refreshSessionOnScreen();   // the composition root, as the review opens
        r.state.enterLoadedSession();
        r.state.enterReplay(QString(), QStringLiteral("/lib/a/S0/swing_0001"));
        r.calls.clear();
        const bool back = r.state.backToSession();
        check("S4c Home's Start while a session runs, from a reviewed session in Replay: back to it, in Capture",
              back && r.state.mode() == pinpoint::AppState::Capture);
        check("S4c …the review is left and its replay stopped; nothing opened, no clock, no cameras",
              r.calls == QStringList{ "stop", "resumeLive" } && r.loaded.isEmpty());
        r.state.refreshSessionOnScreen();   // the composition root, as the review is left (reviewActiveChanged)
        check("S4c …the session on screen is the running one, nothing of the old selected",
              r.state.sessionOnScreenFolder() == r.capture && !r.state.selection()->valid());
    }
    {
        Rig r;
        r.review = QStringLiteral("/lib/a/S0");   // no session runs: Home's Start opens the wizard
        r.state.refreshSessionOnScreen();
        r.state.enterLoadedSession();
        r.state.enterReplay(QString(), QStringLiteral("/lib/a/S0/swing_0001"));
        r.calls.clear();
        check("S4c Home's Start with no session running: false, and nothing changes",
              !r.state.backToSession() && r.state.mode() == pinpoint::AppState::Replay && r.calls.isEmpty()
                  && r.state.sessionOnScreenFolder() == QStringLiteral("/lib/a/S0"));
    }

    std::printf("\nS5 no athlete switch while a session runs\n");
    {
        Rig r;
        check("S5 no session: a switch is allowed", r.state.mayChangeAthlete() && !r.state.athleteLocked());
        r.running = true;
        check("S5 a session running: refused", !r.state.mayChangeAthlete() && r.state.athleteLocked());
        pinpoint::AppState bare;
        check("S5 no doors (the tests' default): never locked", bare.mayChangeAthlete());
    }

    std::printf("\nS6 the QML boundary\n");
    {
        Rig r;
        QQmlEngine engine;
        QJSValue g = engine.globalObject();
        QQmlEngine::setObjectOwnership(&r.state, QQmlEngine::CppOwnership);
        g.setProperty("state", engine.newQObject(&r.state));
        const auto eval = [&engine](const char *js) { return engine.evaluate(QString::fromUtf8(js)); };
        check("S6 the mode's values", eval("state.captureMode === 0 && state.replayMode === 1 && state.analyseMode === 2").toBool());
        eval("state.enterReplay('3c852070-1e72-591c-97e1-85b5df9bc128/2', '/lib/a/S1/swing_0002')");
        check("S6 a command from JavaScript, and the selection read back",
              eval("state.mode === state.replayMode && state.selection.swingNumber === 2 && state.selection.valid").toBool());

        QList<ReplayStreamInfo> streams(2);
        streams[0].perspective = 2;
        streams[1].perspective = 4;
        streams[1].index = 1;
        g.setProperty("streams", engine.toScriptValue(streams));
        check("S6 a list of gadgets reads as an array: length, filter, a field",
              eval("streams.length === 2 && streams.filter(function (s) { return s.perspective !== 4 }).length === 1"
                   " && streams[1].index === 1").toBool());
        g.setProperty("ids", engine.toScriptValue(QList<int>{ 7, 5, 3 }));
        check("S6 a QList<int> reads as an array (indexOf, length)", eval("ids.indexOf(5) === 1 && ids.length === 3").toBool());
    }

    std::printf("\nS7 by SessionId, over real Sessions\n");
    {
        using namespace pinpoint;
        QTemporaryDir dir;
        SessionStore store;
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);   // every trash into the library's own, inside `dir`
        SwingStoreRecordingRepository recordings(store);
        auto reader = std::make_shared<SwingStoreRecordingReader>(store);
        const auto makeAthlete = [&](const char *uuid, const QString &name, const QString &token) {
            const AthleteId id = AthleteId::fromString(QString::fromLatin1(uuid));
            auto a = std::make_unique<Athlete>(nullptr, &store, id);
            AthleteRecord r;
            r.name = name;
            a->setStored(r, dir.path() + QLatin1Char('/') + token, SessionOwner{ id, name, dir.path() },
                         Athlete::Source::Library, false);
            a->setShotDoors({ &recordings, reader });
            return a;
        };
        std::unique_ptr<Athlete> golfer = makeAthlete("5a1d9c3e-2b7f-4e8a-9c10-7d2e6f3b4a21", "Test Golfer", "Test-Golfer");
        std::unique_ptr<Athlete> other  = makeAthlete("0b8e7d6c-5a4f-4e3d-8c2b-1a0f9e8d7c6b", "Other Golfer", "Other-Golfer");
        Session *s1 = golfer->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        if (s1) { s1->addShot(); s1->end(); }
        Session *s2 = golfer->startSession(QStringLiteral("wrist"), QStringLiteral("date-name-type"));
        if (s2) { s2->addShot(); s2->end(); }
        check("S7 two sessions with a swing each", dir.isValid() && s1 && s2);
        if (s1 && s2) {
            Athlete *current = golfer.get();
            App app([&current] { return current; });
            AppState &st = *app.state();
            const auto folderOf = [&store](const SessionId &id) { const auto e = store.open(id); return e ? e->folder : QString(); };
            QString live = folderOf(s1->id());
            AppStateDoors d;
            d.currentAthlete = [&current] { return current; };
            d.sessionAt      = [&app](const QString &f) { return app.sessionAt(f); };
            st.setDoors(d);
            st.setLiveFolder(live);
            check("S7 the live folder's Session is on screen (app.session too)", st.sessionOnScreen() == s1 && app.session() == s1);
            st.enterReplay(ShotRef(s1->id(), 1).toString(), live + QStringLiteral("/swing_0001"));
            live += QLatin1Char('/');
            st.setLiveFolder(live);
            check("S7 the same session spelled another way keeps the selection (by SessionId)", st.selection()->valid());
            live = folderOf(s2->id());
            st.setLiveFolder(live);
            check("S7 another session clears it", !st.selection()->valid() && st.sessionOnScreen() == s2);
            st.enterReplay(ShotRef(s2->id(), 1).toString(), live + QStringLiteral("/swing_0001"));
            current = other.get();
            st.refreshAthlete();
            check("S7 an athlete switch clears it (the folder is not the new athlete's)",
                  !st.selection()->valid() && st.sessionOnScreen() == nullptr);
        }
    }

    std::printf("\nS8 ending an empty session\n");
    {
        Rig r;
        QStringList asked;
        pinpoint::AppStateDoors d;
        d.sessionAt     = [&asked](const QString &f) { asked << f; return static_cast<pinpoint::Session *>(nullptr); };
        d.captureFolder = [&r] { return r.capture; };
        d.replayStop    = [&r] { r.calls << "stop"; };
        d.replayFolder  = [&r] { return r.loaded; };
        // The capture service's end: the empty session's folder is discarded, and then capture says it has no target
        // (activeSessionDirChanged); the carousel's reload says the live folder changed (liveSessionDirChanged).
        d.stopCameras         = [&r] { r.calls << "cameras off"; };
        d.stopClock           = [&r] { r.calls << "clock off"; };
        d.closeCaptureSession = [&r] { r.calls << "close (discarded)"; r.capture.clear(); r.state.refreshCaptureTarget(); };
        d.todaysFolder        = [] { return QStringLiteral("/lib/a/S1"); };
        r.state.setDoors(d);
        r.capture = QStringLiteral("/lib/a/S2");          // the running session, empty
        r.state.refreshCaptureTarget();
        r.state.enterReplay(QString(), QStringLiteral("/lib/a/S2/swing_0001"));   // whatever was on the stage
        asked.clear();
        r.state.endSession(true);
        check("S8 the End discards it, then shows today's remaining session",
              r.calls.mid(r.calls.indexOf("cameras off")) == QStringList{ "cameras off", "clock off", "close (discarded)" }
                  && r.state.liveSessionFolder() == QStringLiteral("/lib/a/S1"));
        check("S8 the session on screen is today's remaining one", r.state.sessionOnScreenFolder() == QStringLiteral("/lib/a/S1"));
        check("S8 the discarded folder is never asked for again", !asked.contains(QStringLiteral("/lib/a/S2")));
        check("S8 and nothing of it stays selected", !r.state.selection()->valid());
    }

    std::printf("\nS9 a reload of the same session keeps the selection and the replay\n");
    {
        Rig r;
        r.state.setLiveFolder(QStringLiteral("/lib/a/S1"));
        r.state.enterReplay(kShot2, kDir2);
        r.calls.clear();
        r.state.setLiveFolder(QString());                 // nothing on screen for a moment…
        r.state.setLiveFolder(QStringLiteral("/lib/a/S1"));   // …and the same session back
        r.state.setLiveFolder(QStringLiteral("/lib/a/S1/"));  // …and again, spelled another way (re-entering the screen)
        check("S9 a reload passing through an empty carousel keeps the selection", r.state.selection()->shotRef() == kShot2);
        check("S9 …and the replay (not stopped)", !r.calls.contains("stop") && r.loaded == kDir2);
        r.capture = QStringLiteral("/lib/a/S1");          // that session running…
        r.state.refreshCaptureTarget();
        r.capture.clear();                                // …and ended with its shots: the carousel still shows it
        r.state.refreshCaptureTarget();
        check("S9 End of a session with shots keeps both", r.state.selection()->valid() && !r.calls.contains("stop"));
        r.state.setLiveFolder(QString());
        r.state.refreshAthlete();
        check("S9 an athlete switch clears it even with nothing on screen",
              !r.state.selection()->valid() && r.calls.contains("stop"));
    }

    std::printf("\nS10 a shot is saving: the views wait, End is at once\n");
    {
        const QStringList kEnd   { "cameras off", "clock off", "close", "today", "release" };
        const QStringList kClose { "cameras off", "clock off", "close" };
        Rig r;
        int announced = 0;
        // The End card / close prompt: waiting for the shot, then End (reload: the toolbar's; not: the close's).
        bool waiting = false, waitingReload = false;
        QObject::connect(&r.state, &pinpoint::AppState::shotSavingChanged, [&] {
            ++announced;
            if (waiting && !r.state.shotSaving()) { waiting = false; r.state.endSession(waitingReload); }
        });
        const auto pressEnd = [&](bool reload) {   // End / End session & close: wait while a shot is saving
            if (r.state.shotSaving()) { waiting = true; waitingReload = reload; }
            else r.state.endSession(reload);
        };
        const auto shotStarts = [&] { r.inFlight = true; r.state.refreshShotSaving(); QCoreApplication::processEvents(); };
        const auto shotLands  = [&] { r.inFlight = false; r.state.refreshShotSaving(); };

        r.state.startSession(false);
        r.calls.clear();
        pressEnd(true);
        check("S10 no shot saving: End at once — cameras, clock, the session's end, today's session, the devices",
              r.calls == kEnd && !r.state.shotSaving());

        r.state.startSession(false);
        r.calls.clear();
        announced = 0;
        shotStarts();
        check("S10 shotSaving reads the capture service's fact, and its rise is announced once", r.state.shotSaving() && announced == 1);
        pressEnd(true);
        check("S10 End while a shot is saving: the card waits, nothing stops or ends", r.calls.isEmpty() && r.running);
        shotLands();
        check("S10 the shot lands: the fall is announced queued, not inside the capture service's state change",
              r.calls.isEmpty() && announced == 1);
        QCoreApplication::processEvents();
        check("S10 …then the card calls End: End's order, the devices released last", r.calls == kEnd && !r.running && announced == 2);
        r.calls.clear();
        r.state.refreshShotSaving();
        QCoreApplication::processEvents();
        check("S10 …announced once per change", announced == 2 && r.calls.isEmpty());

        r.state.startSession(false);
        r.calls.clear();
        shotStarts();
        r.state.endSession(true);   // End anyway
        check("S10 End anyway: at once, the devices released (which drops the shot being saved)", r.calls == kEnd);
        r.calls.clear();
        shotLands();
        QCoreApplication::processEvents();
        check("S10 …and nothing more when the shot would have landed", r.calls.isEmpty());

        r.state.startSession(false);
        r.calls.clear();
        shotStarts();
        pressEnd(false);
        check("S10 the close while a shot is saving: the prompt waits", r.calls.isEmpty() && r.running);
        shotLands();
        QCoreApplication::processEvents();
        check("S10 …and ends once the shot has landed, with no release (then the app closes)", r.calls == kClose && !r.running);

        r.state.startSession(false);
        r.calls.clear();
        shotStarts();
        r.state.endSession(false);   // Close anyway
        check("S10 Close anyway: at once, no release", r.calls == kClose);
        shotLands();
        QCoreApplication::processEvents();

        r.state.startSession(false);
        r.calls.clear();
        shotStarts();
        pressEnd(false);
        waiting = false;             // Cancel: the prompt just closes, nothing was registered
        shotLands();
        QCoreApplication::processEvents();
        check("S10 Cancel: the session runs on, and nothing ends when the shot lands", r.calls.isEmpty() && r.running);
    }
    {
        // Over real Sessions on a scratch library, with a fake capture service that allocates a shot as ShotProcessor
        // does: Session::addShot only once its analysis has landed (buildSwingExportJob), into the session still open,
        // else into one started on demand (captureSession outside a session: #188's stray folder); and the toolbar End's
        // device release joins and drops a shot still being saved (setSelected → finishNowBlocking: #281 B at the cabin).
        using namespace pinpoint;
        const auto scenario = [](bool anyway, const char *label) {
            QTemporaryDir dir;
            SessionStore store;
            store.setLockWaitForTest(200);
            store.setOsTrashForTest(false);
            SwingStoreRecordingRepository recordings(store);
            auto reader = std::make_shared<SwingStoreRecordingReader>(store);
            const AthleteId gid = AthleteId::fromString(QStringLiteral("7c3e1a2b-4d5f-4a6b-8c7d-9e0f1a2b3c4d"));
            Athlete golfer(nullptr, &store, gid);
            AthleteRecord rec;
            rec.name = QStringLiteral("Test Golfer");
            golfer.setStored(rec, dir.path() + QStringLiteral("/Test-Golfer"), SessionOwner{ gid, rec.name, dir.path() },
                             Athlete::Source::Library, false);
            golfer.setShotDoors({ &recordings, reader });

            QPointer<Session> open;
            bool inFlight = false, dropped = false, waiting = false;
            SessionId landedIn;
            AppState st;
            const auto land = [&] {   // the shot's analysis lands: its allocation, its document, the processor idle
                if (!inFlight) return;
                Session *into = open.data();
                if (!into)
                    into = golfer.startSession(QStringLiteral("wrist"), QStringLiteral("date-name-type"), nullptr,
                                               SessionStart::OnDemand);
                if (into && into->addShot())
                    landedIn = into->id();
                inFlight = false;
                st.refreshShotSaving();
            };
            AppStateDoors d;
            d.captureFolder       = [&] {
                if (!open) return QString();
                const auto e = store.open(open->id());
                return e ? e->folder : QString();
            };
            d.stopCameras         = [] {};
            d.stopClock           = [] {};
            d.closeCaptureSession = [&] { if (Session *s = open.data()) { open = nullptr; s->end(); } };
            d.shotInFlight        = [&] { return inFlight; };
            d.releaseDevices      = [&] { if (inFlight) { inFlight = false; dropped = true; } };
            st.setDoors(d);
            QObject::connect(&st, &AppState::shotSavingChanged, [&] {   // the End card
                if (waiting && !st.shotSaving()) { waiting = false; st.endSession(true); }
            });

            open = golfer.startSession(QStringLiteral("wrist"), QStringLiteral("date-name-type"));
            const SessionId running = open ? open->id() : SessionId();
            const auto entry = store.open(running);
            const QString folder = entry ? entry->folder : QString();
            check(QString::fromLatin1(label) + " a session started on a scratch library, its folder made at Start",
                  dir.isValid() && !running.isNull() && QDir(folder).exists());

            inFlight = true;                 // its only shot is still analysing…
            st.refreshShotSaving();
            QCoreApplication::processEvents();
            if (anyway) st.endSession(true); // …End anyway
            else if (st.shotSaving()) waiting = true;   // …End pressed: the card waits while a shot is saving,
            else        st.endSession(true);             // …and ends at once when none is (as the card does)
            land();                          // the analysis lands
            QCoreApplication::processEvents();
            const auto after = store.open(running);
            if (!anyway) {
                check("S10 the shot saving at End is not dropped", !dropped && !landedIn.isNull());
                check("S10 …it lands in the session that was running (not a stray one)", landedIn == running);
                check("S10 the session's folder is kept: not discarded as empty", QDir(folder).exists() && after.has_value());
                check("S10 …and it is ended, holding the shot as swing 1",
                      after && after->status == SessionStatus::Ended && store.shots(running).size() == 1
                          && store.shots(running).value(0).swingNumber() == 1);
            } else {
                check("S10 End anyway drops the shot being saved (the golfer's choice)", dropped && landedIn.isNull());
                check("S10 …and the session, empty, is discarded (DEC-93), into no stray session", !QDir(folder).exists());
            }
        };
        scenario(false, "S10 [End waits]");
        scenario(true,  "S10 [End anyway]");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
