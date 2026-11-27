/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// capture_rig_test: the real ShotProcessor, driven by synthetic streams, with no device and a scratch library
// (#394, step 80; docs/implementation/platform_d_capture_seam.md, T1). Built only in a Debug app tree with
// PP_CAPTURE_RIG=ON (cmake/PinPointCaptureRig.cmake), because it compiles the app's own sources.
//
// The composition is main.cpp's, cut to what a shot passes through: the event buffer, the settings, the session and
// swing stores, the athlete store and its callables, the Rig, the two device managers under the headless switch (no
// camera is enumerated and no Bluetooth scan runs), the carousel model and the shot processor, wired as main.cpp wires
// them. Every store is the scratch one IsolatedStores makes; the library is its library root.
//
// The cases are named in docs/implementation/platform_refactor/duties/DUTIES-shot_processor.md:
//   R1  one camera-less synthetic swing: the trigger, the post-roll, the freeze, the analysis (which has nothing to
//       read), the allocation, the join, the carousel card and the return to capturing;
//   R2  two runs of the same swing come to the same thing, in words that name no time and no absolute path.
//
// Run it: cmake -DPP_CAPTURE_RIG=ON <Debug app tree>; cmake --build <tree> --target capture_rig_test;
//         ctest --test-dir <tree> -R capture_rig -V   (PINPOINT_LOG_STDERR=1 adds the app's log)

#include "../../../Core/tests/isolated_stores.h"

#include "app_settings.h"
#include "athlete_controller.h"
#include "camera_manager.h"
#include "event_buffer.h"
#include "imu_manager.h"
#include "imu_sample.h"
#include "PpMessageLog.h"
#include "session_controller.h"
#include "shot_controller.h"
#include "shot_list_model.h"
#include "shot_processor.h"
#include "../../../App/app.h"                      // AthleteCallables
#include "../../../DomainModel/athlete.h"
#include "../../../DomainModel/rig.h"
#include "../../../DomainModel/session.h"
#include "../../../Stores/athlete_store.h"
#include "../../../Stores/rig_store.h"
#include "../../../Stores/rule_data_install.h"
#include "../../../Stores/session_store.h"
#include "../../../Stores/swing_store_repository.h"
#include "../../../Vocabulary/Metrics/metric_catalogue.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

int g_fail = 0;
void check(bool ok, const QString &label)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// Runs the event loop until `done` holds or `ms` pass. True when it held.
template <typename Pred> bool waitFor(Pred done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// A synthetic stream: a 200 Hz inertial lane on the buffer, written from its own thread while the buffer captures
// (a write slot is refused while it is paused, as a real producer's is). No sensor object stands behind it, so no
// analysis binding reads it: it is the window's content, not a swing.
class SyntheticImuLane
{
public:
    explicit SyntheticImuLane(pinpoint::EventBuffer &buffer) : m_buffer(buffer)
    {
        using namespace std::chrono_literals;
        pinpoint::SourceDescriptor d;
        d.name                     = "rig-imu";
        d.window_duration          = 5000ms;
        d.expected_interarrival_us = std::chrono::microseconds(5000);
        pinpoint::ImuFormat f;
        f.device         = pinpoint::DeviceKind::IMU_WitMotion;
        f.sample_rate_hz = 200;
        f.packet_bytes   = sizeof(pinpoint::ImuSample);
        d.format.device  = pinpoint::DeviceKind::IMU_WitMotion;
        d.format.format  = f;
        m_id = m_buffer.registerSource(d);
        m_thread = std::thread([this] {
            uint64_t n = 0;
            while (m_running.load(std::memory_order_relaxed)) {
                auto slot = m_buffer.acquireWriteSlot(m_id);
                if (slot.valid && slot.capacity >= sizeof(pinpoint::ImuSample)) {
                    pinpoint::ImuSample s{};
                    s.accel_z = 1.0f;
                    s.gyro_z  = float(std::sin(double(n) * 0.01));
                    s.quat_w  = 1.0f;
                    std::memcpy(slot.data, &s, sizeof(s));
                    *slot.bytes_written = sizeof(s);
                    *slot.timestamp_us  = pinpoint::EventBuffer::nowMicros();
                    m_buffer.publish(m_id, slot.sequence);
                    ++n;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(5000));
            }
        });
    }
    ~SyntheticImuLane()
    {
        m_running.store(false);
        if (m_thread.joinable()) m_thread.join();
    }
    pinpoint::SourceId id() const { return m_id; }

private:
    pinpoint::EventBuffer &m_buffer;
    pinpoint::SourceId     m_id = pinpoint::kInvalidSourceId;
    std::atomic_bool       m_running{true};
    std::thread            m_thread;
};

// main.cpp's composition, cut to the shot's path. Members are declared in main.cpp's order, so they are destroyed in
// its reverse: the processor first (it joins its workers), the buffer last.
struct Rig {
    // `run`: this run's own folder inside the scratch stores, holding its library and its rig, so two runs share
    // nothing on disk.
    Rig(const pinpoint::test::IsolatedStores &iso, const QString &run)
        : library(iso.libraryRoot() + QLatin1Char('/') + run)
        , athleteStore([this] { return appSettings.athleteLibraryPath(); }, &sessionStore)
        , rigStore(iso.rigDir() + QLatin1Char('/') + run,
                   { RigStore::Group::Cameras, RigStore::Group::ClubLengthPrior, RigStore::Group::Sensors })
        , rig(rigStore)
    {
        // ⚠ NEVER THE SYSTEM TRASH. A session ended with nothing in it is discarded, and the stores move a discard to
        // the system trash where the volume has one: the scratch library is on the boot volume, so without these the
        // rig would put its sessions in the machine's Trash. Off, they go to the library's own .pinpoint-trash.
        sessionStore.setOsTrashForTest(false);
        athleteStore.setOsTrashForTest(false);
        if (!iso.contains(library) || !QDir().mkpath(library) || !QDir().mkpath(rigStore.folder()))
            qFatal("capture_rig_test: the run's folders are not in the scratch stores");
        eventBuffer.start();
        appSettings.setAthleteLibraryPath(library);
        sessionStore.setCaptureEndReader(&pinpoint::SwingStoreRecordingRepository::captureEndOf);
        athleteStore.setShotDoors({ &recordingRepository, {} });
        callables = pinpoint::AthleteCallables{
            [this] { return athleteStore.list(); },
            [this](const pinpoint::AthleteId &id) { return athleteStore.load(id); },
            [this](const pinpoint::AthleteRecord &record, pinpoint::WriteResult *result) {
                const pinpoint::AthleteCreateResult made = athleteStore.create(record);
                if (result) *result = made.result;
                return made.athlete;
            },
            [this](const pinpoint::SessionOwner &owner) { return athleteStore.readActivity(owner); },
            [this](bool create, pinpoint::WriteResult *result) { return athleteStore.unassigned(create, result); },
        };
        rig.load();
        athleteController.setCallables(callables, &sessionStore);

        imuManager    = std::make_unique<ImuManager>(&eventBuffer, &appSettings);
        imuManager->attachRig(&rig, &rig, rig.parts());
        cameraManager = std::make_unique<CameraManager>(&eventBuffer, &appSettings);
        cameraManager->attachRig(&rig, rig.parts());
        sessionController = std::make_unique<SessionController>(&athleteController);
        // The shot marker's source, as main.cpp registers it; then the capture intent, as main.cpp applies it.
        shotController = std::make_unique<ShotController>(&eventBuffer, sessionController.get());
        lane           = std::make_unique<SyntheticImuLane>(eventBuffer);
        cameraManager->applyCaptureIntent();

        processor = std::make_unique<ShotProcessor>(&eventBuffer, cameraManager.get(), imuManager.get(), &appSettings,
                                                    &athleteController, sessionController.get(), &shotModel);
        processor->setRecordingRepository(&recordingRepository);
        processor->attachRig(&rig, &rig.parts());
        processor->setUnassignedAthlete(callables.unassigned);
        cameraManager->setShotProcessor(processor.get());
        imuManager->setShotProcessor(processor.get());

        QObject::connect(processor.get(), &ShotProcessor::shotProcessed, [this](int id, const QString &dir) {
            ++processed; lastShotId = id; lastSwingDir = dir;
        });
        QObject::connect(processor.get(), &ShotProcessor::shotFailed, [this](const QString &e) { ++failed; lastError = e; });
        QObject::connect(processor.get(), &ShotProcessor::shotOutcome, [this](int, bool exportOk, bool analysisOk) {
            ++outcomes; lastExportOk = exportOk; lastAnalysisOk = analysisOk;
        });
        // Each state the processor passes through, with the buffer's state as it enters it.
        QObject::connect(processor.get(), &ShotProcessor::stateChanged, [this] {
            states << processor->stateName();
            bufferAtState << (eventBuffer.state() == pinpoint::BufferState::Paused ? QStringLiteral("paused")
                                                                                     : QStringLiteral("capturing"));
        });
        QObject::connect(processor.get(), &ShotProcessor::shotDocumentAnnounced,
                         [this](const pinpoint::ShotRef &) { ++announced; });
    }
    ~Rig()
    {
        if (processor) processor->finishNowBlocking();
        processor.reset();
        lane.reset();
        shotController.reset();
    }

    // The golfer's Start, as app.state's doors make it (main.cpp): the capture session's folder, then the cameras'
    // capture intent on, which resumes the buffer.
    void start(bool extend = false)
    {
        processor->beginSessionFolder(/*sessionType=*/0, extend);
        cameraManager->startCapture();
    }

    // A swing NOW, by the given source: its impact `backMs` before the trigger (a manual press is made after the
    // swing). Waits until the processor is idle again. True when it was.
    bool swing(ShotController::Source source = ShotController::Source::Manual, int backMs = 300, int timeoutMs = 60000)
    {
        const qint64 impact = pinpoint::EventBuffer::nowMicros() - qint64(backMs) * 1000;
        processor->onShotDetected(source, impact, /*sessionType=*/0);
        if (!processor->busy()) return false;
        return waitFor([this] { return !processor->busy(); }, timeoutMs);
    }

    QString                                  library;
    pinpoint::EventBuffer                    eventBuffer;
    AppSettings                              appSettings;
    pinpoint::SessionStore                   sessionStore;
    pinpoint::SwingStoreRecordingRepository  recordingRepository{ sessionStore };
    pinpoint::AthleteStore                   athleteStore;
    pinpoint::AthleteCallables               callables;
    AthleteController                        athleteController;
    RigStore                                 rigStore;
    pinpoint::Rig                            rig;
    std::unique_ptr<ImuManager>              imuManager;
    std::unique_ptr<CameraManager>           cameraManager;
    std::unique_ptr<SessionController>       sessionController;
    std::unique_ptr<ShotController>          shotController;
    std::unique_ptr<SyntheticImuLane>        lane;
    ShotListModel                            shotModel;
    std::unique_ptr<ShotProcessor>           processor;

    int     processed = 0, failed = 0, outcomes = 0, announced = 0;
    int     lastShotId = -1;
    bool    lastExportOk = false, lastAnalysisOk = false;
    QString lastSwingDir, lastError;
    QStringList states, bufferAtState;
};

// Every swing document under `root` (a run's library), relative to it, sorted.
QStringList swingDocuments(const QString &root)
{
    QStringList out;
    QDirIterator it(root, { QStringLiteral("swing.json"), QStringLiteral("swing.ppsw") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

// What one camera-less swing came to, in the words a second run can be compared in: no time, no absolute path.
struct SwingRun {
    bool        idleAgain = false, capturingAgain = false;
    int         processed = 0, failed = 0, outcomes = 0, announced = 0, cards = 0;
    bool        exportOk = false, analysisOk = false;
    QString     error;
    QString     sessionDir;   // the session begun at Start, relative to the library
    QString     swingDir;     // the allocation, relative to the library
    bool        swingDirMade = false;
    QStringList documents;    // relative to the library
    QStringList states, bufferAtState;   // the processor's states, and the buffer's as each was entered
    double      progress = 0.0;          // the analysis progress at the end

    bool operator==(const SwingRun &o) const
    {
        return idleAgain == o.idleAgain && capturingAgain == o.capturingAgain && processed == o.processed
               && failed == o.failed && outcomes == o.outcomes && announced == o.announced && cards == o.cards
               && exportOk == o.exportOk && analysisOk == o.analysisOk && error == o.error && sessionDir == o.sessionDir && swingDir == o.swingDir
               && swingDirMade == o.swingDirMade && documents == o.documents && states == o.states
               && bufferAtState == o.bufferAtState && progress == o.progress;
    }
    QString describe() const
    {
        return QStringLiteral("processed %1, failed %2 (\"%3\"), outcomes %4 (export %5, analysis %6), announced %7, "
                              "cards %8, allocated \"%9\" (made %10), documents [%11], states [%12], buffer [%13], progress %14")
            .arg(processed).arg(failed).arg(error).arg(outcomes).arg(exportOk).arg(analysisOk).arg(announced)
            .arg(cards).arg(swingDir).arg(swingDirMade).arg(documents.join(QStringLiteral(", ")))
            .arg(states.join(QLatin1Char(' '))).arg(bufferAtState.join(QLatin1Char(' '))).arg(progress);
    }
};

// One Start, one manual swing, one End, in a fresh rig on its own folders.
SwingRun oneCameralessSwing(const pinpoint::test::IsolatedStores &iso, const QString &runName)
{
    Rig r(iso, runName);
    r.start();
    check(r.eventBuffer.state() == pinpoint::BufferState::Capturing, runName + ": the buffer captures after the Start");
    check(r.processor->sessionFolderReady() && iso.contains(r.processor->activeSessionDir()),
          runName + ": the session's folder is made at Start, in the scratch library");

    SwingRun out;
    out.sessionDir     = QDir(r.library).relativeFilePath(r.processor->activeSessionDir());
    out.idleAgain      = r.swing();
    out.capturingAgain = r.eventBuffer.state() == pinpoint::BufferState::Capturing;
    out.processed = r.processed; out.failed = r.failed; out.outcomes = r.outcomes; out.announced = r.announced;
    out.exportOk  = r.lastExportOk; out.analysisOk = r.lastAnalysisOk; out.error = r.lastError;
    out.cards     = r.shotModel.rowCount();
    const QString dir = r.processor->lastSwingDir();
    out.swingDir      = dir.isEmpty() ? QString() : QDir(r.library).relativeFilePath(dir);
    out.swingDirMade  = !dir.isEmpty() && QFileInfo(dir).isDir();
    out.documents     = swingDocuments(r.library);
    out.states        = r.states;
    out.bufferAtState = r.bufferAtState;
    out.progress      = r.processor->analysisProgress();
    r.processor->endSessionFolder();
    return out;
}

// R1 (DUTIES-shot_processor.md rows 7, 11, 21, 22, 27, 34, 36, 37, 40). With no camera and no bound sensor the
// analysis has nothing to read, so today's code writes no document: what it does instead is pinned here. The swing
// that is saved needs the stand-in camera (step 81).
void caseOneSwing(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R1 one camera-less synthetic swing\n");
    const SwingRun run = oneCameralessSwing(iso, QStringLiteral("r1"));
    std::printf("    %s\n", qPrintable(run.describe()));
    check(run.idleAgain, "the shot runs to its end: the processor is idle again");
    check(run.capturingAgain, "the buffer captures again after the shot");
    check(run.outcomes == 1, "one terminal statement for the shot");
    check(run.states == QStringList({ "postroll", "processing", "idle" }),
          "post-roll, then processing (no gather: nothing deferred), then idle: " + run.states.join(QLatin1Char(' ')));
    check(run.bufferAtState.value(1) == QStringLiteral("paused"), "the buffer is paused while the window is processed");
    check(run.progress == 1.0, "the analysis progress ends full");
    check(run.processed == 0 && run.failed == 1, "it is announced as failed, never as processed");
    check(run.error == QStringLiteral("no IMU and no pose data in window"),
          "the analysis fails for having nothing to read: \"" + run.error + "\"");
    check(!run.analysisOk && run.exportOk, "the outcome: analysis faulted, the export not (skipped: no camera)");
    check(run.swingDir == run.sessionDir + QStringLiteral("/swing_0001"),
          "the swing is allocated in the session begun, as its first: " + run.swingDir);
    check(run.documents.isEmpty() && run.announced == 0, "no document is written, and no EVT-01 announced");
    check(run.cards == 1, "the card lands all the same (a card with no document)");
}

// R2: two runs of the same swing come to the same thing. (With the stand-in camera, step 81, it compares the documents.)
void caseTwoRunsEqual(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R2 two runs equal\n");
    const SwingRun a = oneCameralessSwing(iso, QStringLiteral("r2a"));
    const SwingRun b = oneCameralessSwing(iso, QStringLiteral("r2b"));
    check(a == b, "the two runs come to the same: " + a.describe() + (a == b ? QString() : " | " + b.describe()));
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores iso;   // first, before any store (isolated_stores.h)
    qputenv("PINPOINT_HEADLESS_PROBE", "1");   // no camera enumerated, no Bluetooth scan (headless_probe.h)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // The app's log, on stderr when asked, as the app does it (PinPointDebug::install): ctest -V shows it.
    if (!qEnvironmentVariableIsEmpty("PINPOINT_LOG_STDERR"))
        PpMessageLog::instance()->setEchoToStderr(true);
    QCoreApplication::setOrganizationName(QStringLiteral("PinPointStudio"));
    QCoreApplication::setApplicationName(QStringLiteral("PinPointStudio"));
    pinpoint::installProcessRuleData(
        std::make_shared<const pinpoint::analysis::MetricCatalogue>(pinpoint::analysis::makeMetricCatalogue()));

    caseOneSwing(iso);
    caseTwoRunsEqual(iso);

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
