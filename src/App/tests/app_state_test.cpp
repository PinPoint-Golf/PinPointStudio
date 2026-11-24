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
//   S4  START AND END ARE ONE COMMAND EACH (E-12): Start opens the capture session, then shows it (empty when new, the
//       resumed folder on extend), then the clock, then the cameras; End stops the cameras, the clock, ends the session
//       (its empty discard) and only then reloads the carousel — and not at all for the close
//   S5  NO ATHLETE SWITCH WHILE A SESSION RUNS (R-APP-1, the rule that replaced Stage 0.8g's check)
//   S7  BY SessionId, OVER REAL SESSIONS: another spelling of the same session folder keeps the selection, another
//       session clears it, and so does an athlete switch (the folder is not the new athlete's)
//   S8  ENDING AN EMPTY SESSION: the End discards it; the session on screen is then today's remaining session, and the
//       trashed folder is never asked for again (no Session is ever made for it: session_test S3i–k)
//   S9  A RELOAD OF THE SAME SESSION KEEPS THE SELECTION AND THE REPLAY (review B3): the live folder passing through
//       empty (a carousel reload) is not another session; End of a session with shots leaves the same one on screen;
//       an athlete switch with nothing on screen still clears it
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
              r.calls == QStringList{ "open new", "clock", "cameras" });
        check("S4 …the live carousel shows the new session (the capture target), nothing before it",
              live == QStringList{ "/lib/a/S2" } && r.state.liveFolder() == QStringLiteral("/lib/a/S2"));
        r.calls.clear();
        live.clear();
        r.state.startSession(true);
        check("S4 Start (extend): the carousel shows the resumed session",
              r.calls == QStringList{ "open extend", "clock", "cameras" } && r.state.liveFolder() == QStringLiteral("/lib/a/S2"));
        r.calls.clear();
        r.state.endSession(true);
        check("S4 End: cameras, clock, the session's end — and only then today's session",
              r.calls == QStringList{ "cameras off", "clock off", "close", "today" }
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

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
