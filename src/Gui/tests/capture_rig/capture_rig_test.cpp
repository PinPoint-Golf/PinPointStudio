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
//   R2  two runs of the same swing come to the same thing, in words that name no time and no absolute path;
//   R3  one synthetic swing saved, with the stand-in camera: exported, its document written and read back (the
//       degraded path: the frames hold no golfer, so the analysis fails and the shot is announced as failed);
//   R4  two saved runs record the same things;
//   R5  a processed shot: a skip-analysis capture, exported and written, announced as processed;
//   D1  a stand-in deferred source is reserved, polled and collected (the gather is reachable);
//   T1  the post-roll per source; T2 the window's bounds frozen at the pause, and the resume guard during the gather;
//   T3  reserve at detection, collect at the gather; T4 the gather's own deadline, and its order against the library's;
//   W1  the first write (R-REC-1), W2 the staged writes; W3 the Recording read once at the freeze; W4 a session folder
//       that cannot be written (blocked, a card with no document);
//   READY1 the trigger arms only once every device's buffer is active again; ONE1 one shot at a time (#281, by design);
//   --xfail <issue>: the known bugs, each written to the right behaviour and expected to fail: #188 (End with a shot in
//       flight, and in its post-roll), #53 (a shot whose impact lies before ready is taken).
// The stand-ins (step 81): the camera, VideoInputStandIn, registered with the device enumerator and created only in
// this build; the deferred source, StandInDeferredSource below, handed to the processor beside the sensor manager's.
//
// Run it: cmake -DPP_CAPTURE_RIG=ON <Debug app tree>; cmake --build <tree> --target capture_rig_test;
//         ctest --test-dir <tree> -R capture_rig -V   (PINPOINT_LOG_STDERR=1 adds the app's log)

#include "../../../Core/tests/isolated_stores.h"

#include "app_settings.h"
#include "athlete_controller.h"
#include "camera_instance.h"
#include "camera_manager.h"
#include "event_buffer.h"
#include "imu_manager.h"
#include "imu_sample.h"
#include "PpMessageLog.h"
#include "session_controller.h"
#include "shot_controller.h"
#include "shot_list_model.h"
#include "shot_processor.h"
#include "deferred_source.h"
#include "device_enumerator.h"
#include "video_input_standin.h"
#include "../../../Stores/swing_doc.h"
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
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        d.identifier               = "rig-imu";   // keys its session-lifetime count, as a real device's is
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

// A STAND-IN DEFERRED SOURCE (DeferredSource; the HackMotion's part in a shot): it records when it is reserved, polled
// and collected, and stays pending for `holdMs` after its reservation (forever when negative), so a test can hold the
// gather open. It returns no block, so nothing is stitched: the window is the ring's, as with a refused pull.
class StandInDeferredSource : public pinpoint::DeferredSource
{
public:
    explicit StandInDeferredSource(int holdMs) : m_holdMs(holdMs) {}

    QString deviceId() const override { return QStringLiteral("standin-deferred-0"); }
    void reserveHistory(qint64 impactUs, qint64 deadlineUs) override
    {
        ++reserves;
        reservedImpactUs   = impactUs;
        reservedDeadlineUs = deadlineUs;
        reservedAtUs       = pinpoint::EventBuffer::nowMicros();
        m_pending          = true;
    }
    bool historyPending() const override
    {
        ++polls;
        if (!m_pending) return false;
        return m_holdMs < 0 || pinpoint::EventBuffer::nowMicros() - reservedAtUs < qint64(m_holdMs) * 1000;
    }
    pinpoint::hm::HistoryResult takeHistoryResult() override
    {
        ++takes;
        takenAtUs = pinpoint::EventBuffer::nowMicros();
        m_pending = false;
        return {};
    }
    pinpoint::SourceId deferredLane(int) const override { return pinpoint::kInvalidSourceId; }

    int            reserves = 0, takes = 0;
    mutable int    polls = 0;
    qint64         reservedImpactUs = -1, reservedDeadlineUs = -1, reservedAtUs = -1, takenAtUs = -1;

private:
    int  m_holdMs;
    bool m_pending = false;
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
        // As main.cpp hands the sensor manager's, with the test's stand-ins after them.
        processor->setDeferredSources([this] {
            QVector<pinpoint::DeferredSource *> v = imuManager->deferredSources();
            for (StandInDeferredSource *d : standInSources) v.push_back(d);
            return v;
        });
        cameraManager->setShotProcessor(processor.get());
        imuManager->setShotProcessor(processor.get());
        // The shot trigger's wiring, as main.cpp makes it: a sensor's buffer change re-applies the capture intent; every
        // buffer change re-evaluates the trigger; the processor's busy state disarms it; a detected shot drives the
        // processor. (A case may still call the processor's onShotDetected itself, past the trigger's gate.)
        QObject::connect(imuManager.get(), &ImuManager::bufferStateChanged, cameraManager.get(),
                         &CameraManager::applyCaptureIntent);
        QObject::connect(cameraManager.get(), &CameraManager::bufferStateChanged, shotController.get(),
                         &ShotController::reevaluateArmed);
        QObject::connect(shotController.get(), &ShotController::shotDetected, processor.get(),
                         &ShotProcessor::onShotDetected);
        QObject::connect(processor.get(), &ShotProcessor::busyChanged, shotController.get(),
                         [this] { shotController->setProcessorBusy(processor->busy()); });

        QObject::connect(processor.get(), &ShotProcessor::shotProcessed, [this](int id, const QString &dir) {
            ++processed; lastShotId = id; lastSwingDir = dir;
        });
        QObject::connect(processor.get(), &ShotProcessor::shotFailed, [this](const QString &e) { ++failed; lastError = e; });
        QObject::connect(processor.get(), &ShotProcessor::shotOutcome, [this](int, bool exportOk, bool analysisOk) {
            ++outcomes; lastExportOk = exportOk; lastAnalysisOk = analysisOk;
        });
        // Each state the processor passes through, with the buffer's state as it enters it.
        QObject::connect(processor.get(), &ShotProcessor::stateChanged, [this] {
            stateAtUs << pinpoint::EventBuffer::nowMicros();
            stateAtWallMs << QDateTime::currentMSecsSinceEpoch();
            states << processor->stateName();
            bufferAtState << (eventBuffer.state() == pinpoint::BufferState::Paused ? QStringLiteral("paused")
                                                                                     : QStringLiteral("capturing"));
        });
        QObject::connect(processor.get(), &ShotProcessor::shotDocumentAnnounced,
                         [this](const pinpoint::ShotRef &shot) {
                             ++announced; announcedShots << shot; events << QStringLiteral("announced");
                         });
        // The order the shot's stages are announced in.
        QObject::connect(processor.get(), &ShotProcessor::analysisFailed, [this] { events << QStringLiteral("analysisFailed"); });
        QObject::connect(processor.get(), &ShotProcessor::swingSaved, [this] { events << QStringLiteral("mediaSaved"); });
        QObject::connect(processor.get(), &ShotProcessor::shotProcessed, [this] { events << QStringLiteral("processed"); });
        QObject::connect(processor.get(), &ShotProcessor::shotFailed, [this] { events << QStringLiteral("failed"); });
        QObject::connect(processor.get(), &ShotProcessor::shotOutcome, [this] { events << QStringLiteral("outcome"); });
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

    // The stand-in camera, connected and recording, as a golfer's Connect and the session's start make it (the
    // camera manager's setSelected and startAll). main() registered it with the device enumerator, so the manager
    // listed it; it is the only camera listed. Waits until its frames reach the buffer.
    bool connectStandInCamera(int warmUpMs = 1500)
    {
        cameraManager->setSelected(0, true);
        cameraManager->startAll();
        const std::vector<CameraInstance *> live = cameraManager->liveCameraInstances();
        if (live.size() != 1) return false;
        CameraInstance *cam = live.front();
        // ⚠ THE CAMERA'S OWN FRAMES, by its source id: the synthetic inertial lane fills the buffer on its own.
        const bool flowing = waitFor([&] {
            const pinpoint::SourceId id = cam->sourceId();
            if (id == pinpoint::kInvalidSourceId || !eventBuffer.isCapturing()) return false;
            const auto recent = eventBuffer.snapshot(pinpoint::EventBuffer::nowMicros() - 500'000,
                                                     pinpoint::EventBuffer::nowMicros());
            return std::any_of(recent.begin(), recent.end(),
                               [id](const pinpoint::IndexEntry &e) { return e.source_id == id; });
        }, 10000);
        if (!flowing) return false;
        waitFor([] { return false; }, warmUpMs);   // a pre-roll of frames before the swing
        return true;
    }

    // A swing NOW, by the given source: its impact `backMs` before the trigger (a manual press is made after the
    // swing). Waits until the processor is idle again. True when it was.
    bool swing(ShotController::Source source = ShotController::Source::Manual, int backMs = 300, int timeoutMs = 60000)
    {
        const qint64 impact = pinpoint::EventBuffer::nowMicros() - qint64(backMs) * 1000;
        lastImpactUs  = impact;
        lastTriggerUs = pinpoint::EventBuffer::nowMicros();
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
    QVector<StandInDeferredSource *>         standInSources;   // the test's, handed to the processor (not owned)
    qint64                                   lastImpactUs = -1, lastTriggerUs = -1;   // the last swing()'s

    int     processed = 0, failed = 0, outcomes = 0, announced = 0;
    int     lastShotId = -1;
    bool    lastExportOk = false, lastAnalysisOk = false;
    QString lastSwingDir, lastError;
    QStringList states, bufferAtState, events;
    QList<pinpoint::ShotRef> announcedShots;   // each shot whose first document was announced (EVT-01)
    QVector<qint64> stateAtUs, stateAtWallMs;   // when each state was entered: the buffer's clock, the wall clock

    // When the state `name` was first entered (the buffer's clock), or -1.
    qint64 enteredUs(const QString &name) const
    {
        const int i = states.indexOf(name);
        return i < 0 ? -1 : stateAtUs.value(i, -1);
    }
    qint64 enteredWallMs(const QString &name) const
    {
        const int i = states.indexOf(name);
        return i < 0 ? -1 : stateAtWallMs.value(i, -1);
    }
    void clearStates() { states.clear(); bufferAtState.clear(); stateAtUs.clear(); stateAtWallMs.clear(); events.clear(); }
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
    bool        exportOk = false, analysisOk = false;   // shotOutcome's flags: the stage did NOT fault
    QString     error;
    QString     sessionDir;   // the session begun at Start, relative to the library
    QString     swingDir;     // the allocation, relative to the library
    bool        swingDirMade = false;
    QStringList documents;    // relative to the library
    QStringList states, bufferAtState;   // the processor's states, and the buffer's as each was entered
    double      progress = 0.0;          // the analysis progress at the end
    // The saved swing, as the swing door reads it back: what it records, less its time and paths.
    bool        docOk = false, docHasVideo = false;
    QStringList docDevices, swingFiles;  // the devices it names; the swing folder's file names

    bool operator==(const SwingRun &o) const
    {
        return idleAgain == o.idleAgain && capturingAgain == o.capturingAgain && processed == o.processed
               && failed == o.failed && outcomes == o.outcomes && announced == o.announced && cards == o.cards
               && exportOk == o.exportOk && analysisOk == o.analysisOk && error == o.error && sessionDir == o.sessionDir && swingDir == o.swingDir
               && swingDirMade == o.swingDirMade && documents == o.documents && states == o.states
               && bufferAtState == o.bufferAtState && progress == o.progress && docOk == o.docOk
               && docHasVideo == o.docHasVideo && docDevices == o.docDevices && swingFiles == o.swingFiles;
    }
    QString describe() const
    {
        return QStringLiteral("processed %1, failed %2 (\"%3\"), outcomes %4 (export %5, analysis %6), announced %7, "
                              "cards %8, allocated \"%9\" (made %10), documents [%11], states [%12], buffer [%13], progress %14")
            .arg(processed).arg(failed).arg(error).arg(outcomes).arg(exportOk).arg(analysisOk).arg(announced)
            .arg(cards).arg(swingDir).arg(swingDirMade).arg(documents.join(QStringLiteral(", ")))
            .arg(states.join(QLatin1Char(' '))).arg(bufferAtState.join(QLatin1Char(' '))).arg(progress)
            + QStringLiteral(", document read back %1 (video %2, devices [%3]), files [%4]")
                  .arg(docOk).arg(docHasVideo).arg(docDevices.join(QStringLiteral(", ")))
                  .arg(swingFiles.join(QStringLiteral(", ")));
    }
};

// How a run is set up: the stand-in camera connected or not; the in-window replay after a shot not reviewable on disk
// (the View menu's auto-replay; off saves a case the quarter-speed replay); a corpus capture's skip-analysis export.
struct SwingOptions {
    bool withCamera   = false;
    bool autoReplay   = true;
    bool skipAnalysis = false;
};

// One Start, one manual swing, one End, in a fresh rig on its own folders.
SwingRun oneSwing(const pinpoint::test::IsolatedStores &iso, const QString &runName, SwingOptions opt = {})
{
    const bool withCamera = opt.withCamera;
    Rig r(iso, runName);
    r.appSettings.setAutoReplayAfterCapture(opt.autoReplay);
    r.appSettings.setSaveRawFrames(opt.skipAnalysis);
    r.appSettings.setSkipAnalysisForRawCapture(opt.skipAnalysis);
    r.start();
    if (withCamera)
        check(r.connectStandInCamera(), runName + ": the stand-in camera is connected and its frames reach the buffer");
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
    if (out.swingDirMade) {
        const pinpoint::PersistedShot shot = pinpoint::SwingDocReader::readSwingJson(dir);
        out.docOk       = shot.ok;
        out.docHasVideo = shot.hasVideo;
        out.docDevices  = shot.devices;
        out.swingFiles  = QDir(dir).entryList(QDir::Files, QDir::Name);
    }
    r.processor->endSessionFolder();
    return out;
}

// R1 (DUTIES-shot_processor.md rows 7, 11, 21, 22, 27, 34, 36, 37, 40). With no camera and no bound sensor the
// analysis has nothing to read, so today's code writes no document: what it does instead is pinned here. The swing
// that is saved needs the stand-in camera (step 81).
void caseOneSwing(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R1 one camera-less synthetic swing\n");
    const SwingRun run = oneSwing(iso, QStringLiteral("r1"));
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
    check(!run.analysisOk && run.exportOk, "the outcome (shotOutcome's not-faulted flags): the analysis faulted, the export not (skipped: no camera)");
    check(run.swingDir == run.sessionDir + QStringLiteral("/swing_0001"),
          "the swing is allocated in the session begun, as its first: " + run.swingDir);
    check(run.documents.isEmpty() && run.announced == 0, "no document is written, and no EVT-01 announced");
    check(run.cards == 1, "the card lands all the same (a card with no document)");
}

// R2: two runs of the same swing come to the same thing. (With the stand-in camera, step 81, it compares the documents.)
void caseTwoRunsEqual(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R2 two runs equal\n");
    const SwingRun a = oneSwing(iso, QStringLiteral("r2a"));
    const SwingRun b = oneSwing(iso, QStringLiteral("r2b"));
    check(a == b, "the two runs come to the same: " + a.describe() + (a == b ? QString() : " | " + b.describe()));
}

// R3 (step 81): the saved swing, with the stand-in camera. Its window holds the camera's frames, the export encodes
// them, and the join writes the document through the swing door, which reads it back. ⚠ THE DEGRADED PATH: the stand-in's
// frames hold no golfer, so the analysis still has nothing to read and fails; the document is the media's, with no
// analysis, and the shot is announced as failed. Not being reviewable on disk, it gets the in-window replay.
void caseSavedSwing(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R3 one synthetic swing saved, with the stand-in camera\n");
    const SwingRun run = oneSwing(iso, QStringLiteral("r3"), { .withCamera = true });
    std::printf("    %s\n", qPrintable(run.describe()));
    check(run.idleAgain && run.capturingAgain, "the shot runs to its end, and the buffer captures again");
    check(run.outcomes == 1, "one terminal statement for the shot");
    check(run.processed == 0 && run.failed == 1 && run.error == QStringLiteral("no IMU and no pose data in window"),
          "the analysis fails (no golfer in the frames), so the shot is announced as failed: \"" + run.error + "\"");
    check(run.states == QStringList({ "postroll", "processing", "replaying", "idle" }),
          "not reviewable on disk, it gets the in-window replay: " + run.states.join(QLatin1Char(' ')));
    check(run.exportOk, "the camera's frames are exported");
    check(run.documents.size() == 1 && run.documents.value(0).startsWith(run.swingDir + QLatin1Char('/')),
          "one document is written, in the swing's folder: " + run.documents.join(QStringLiteral(", ")));
    check(run.docOk && run.docHasVideo, "the swing door reads it back, with its video");
    check(run.announced == 1, "its first document is announced once (EVT-01)");
    check(run.cards == 1, "one card");
}

// R4 (step 81): two saved swings, in two runs, record the same things (their time, paths and frames aside).
void caseTwoSavedRunsEqual(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R4 two saved runs equal\n");
    const SwingRun a = oneSwing(iso, QStringLiteral("r4a"), { .withCamera = true, .autoReplay = false });
    const SwingRun b = oneSwing(iso, QStringLiteral("r4b"), { .withCamera = true, .autoReplay = false });
    check(a.docOk && a == b, "the two saved runs come to the same: " + a.describe() + (a == b ? QString() : " | " + b.describe()));
}

// R5 (step 81): a processed shot. A corpus capture that skips the analysis (saveRawFrames and skipAnalysisForRawCapture)
// exports the stand-in camera's frames, raw too, and writes the document: no stage faulted and something was kept, so
// the shot is announced as processed, after its write (DUTIES rows 19, 34).
void caseProcessedSwing(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("R5 a processed shot: a skip-analysis capture with the stand-in camera\n");
    const SwingRun run = oneSwing(iso, QStringLiteral("r5"), { .withCamera = true, .autoReplay = false, .skipAnalysis = true });
    std::printf("    %s\n", qPrintable(run.describe()));
    check(run.idleAgain && run.capturingAgain, "the shot runs to its end, and the buffer captures again");
    check(run.processed == 1 && run.failed == 0 && run.outcomes == 1, "announced once, as processed");
    check(run.exportOk && run.analysisOk, "neither stage is called faulted: the analysis was skipped on request (shotOutcome)");
    check(run.states == QStringList({ "postroll", "processing", "idle" }), "no replay: " + run.states.join(QLatin1Char(' ')));
    check(run.documents.size() == 1 && run.docOk && run.docHasVideo && run.announced == 1,
          "one document, read back with its video, announced once");
}

// D1 (step 81): a deferred source is reachable. A stand-in that holds the gather open for 1.5 s is reserved at the
// trigger and collected after the pause; the processor passes through its gather.
void caseDeferredReachable(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("D1 a stand-in deferred source is reserved, polled and collected\n");
    Rig r(iso, QStringLiteral("d1"));
    StandInDeferredSource late(1500);
    r.standInSources << &late;
    r.start();
    const bool idle = r.swing();
    check(idle, "the shot runs to its end");
    check(r.states.contains(QStringLiteral("gathering")), "the processor gathers: " + r.states.join(QLatin1Char(' ')));
    check(late.reserves == 1 && late.takes == 1 && late.polls >= 2, QStringLiteral("reserved once, polled %1 times, collected once")
                                                                      .arg(late.polls));
    r.processor->endSessionFolder();
}

// ── Timing pins (#394, step 83; DUTIES-shot_processor.md rows 1-4, 8-12) ──────────────────────────────────────

// T1: the post-roll per source. The freeze (the pause, as the processor leaves its post-roll) lands the source's
// post-roll after the trigger, within the timer's 5 % (checked at 10 %, for a loaded machine): Manual and Ppcp 500 ms,
// the four detectors 1250 ms.
// Camera-less, one session.
void caseTimingPostRoll(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("T1 the post-roll per source\n");
    using S = ShotController::Source;
    Rig r(iso, QStringLiteral("t1"));
    r.start();
    const struct { S source; const char *name; int ms; } rows[] = {
        { S::Manual, "Manual", 500 }, { S::Imu, "Imu", 1250 }, { S::Pose, "Pose", 1250 },
        { S::Ball, "Ball", 1250 },    { S::Acoustic, "Acoustic", 1250 }, { S::Ppcp, "Ppcp", 500 },
    };
    for (const auto &row : rows) {
        r.clearStates();
        const bool idle = r.swing(row.source, /*backMs=*/0);
        const qint64 frozeUs = r.enteredUs(QStringLiteral("processing"));
        const qint64 tookMs  = frozeUs < 0 ? -1 : (frozeUs - r.lastTriggerUs) / 1000;
        // ⚠ ABOUT the post-roll, not at least it: the post-roll is a default (coarse) QTimer, which Qt fires within 5 %
        // of its interval, early or late. Measured on the Mac: 1229-1233 ms for 1250, 476-514 ms for 500.
        check(idle && tookMs >= row.ms * 90 / 100 && tookMs < row.ms + 300,
              QStringLiteral("%1: the window is frozen %2 ms after the trigger (post-roll %3 ms)")
                  .arg(QLatin1String(row.name)).arg(tookMs).arg(row.ms));
    }
    r.processor->endSessionFolder();
}

// T2 and T3: a late sensor holding the gather open for about 1.75 s (3 s from its reservation), on a skip-analysis capture with the stand-in camera (a
// document to read the window back from). The window's bounds are frozen at the pause, not after the gather: the
// document's clock (its window's start on the wall clock) is the pause less the 4 s window. A resume asked for during
// the gather is refused (the guard is up from the pause). The source is reserved at the trigger with the swing's impact
// and impact + 7 s, and collected once the gather ends, before the window is processed.
void caseTimingGather(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("T2 the window's bounds frozen at the pause; T3 reserve at detection, collect at the gather\n");
    Rig r(iso, QStringLiteral("t2"));
    StandInDeferredSource late(3000);
    r.standInSources << &late;
    r.appSettings.setAutoReplayAfterCapture(false);
    r.appSettings.setSaveRawFrames(true);
    r.appSettings.setSkipAnalysisForRawCapture(true);
    r.start();
    check(r.connectStandInCamera(), "the stand-in camera is connected");
    bool resumeRefused = false, resumeTried = false;
    QObject::connect(r.processor.get(), &ShotProcessor::stateChanged, r.processor.get(), [&] {
        if (r.processor->stateName() != QStringLiteral("gathering") || resumeTried) return;
        resumeTried = true;
        r.cameraManager->resumeBuffer();   // as any resume path does: QML, a capture intent
        resumeRefused = r.eventBuffer.state() == pinpoint::BufferState::Paused;
    });
    const bool idle = r.swing(ShotController::Source::Imu, /*backMs=*/0);
    const qint64 gatherUs = r.enteredUs(QStringLiteral("gathering")), processUs = r.enteredUs(QStringLiteral("processing"));
    const qint64 pauseWallMs = r.enteredWallMs(QStringLiteral("gathering"));
    check(idle && r.states == QStringList({ "postroll", "gathering", "processing", "idle" }),
          "post-roll, gathering, processing, idle: " + r.states.join(QLatin1Char(' ')));
    check(processUs - gatherUs >= 1'400'000, QStringLiteral("the gather is held open: %1 ms").arg((processUs - gatherUs) / 1000));
    check(resumeTried && resumeRefused, "a resume asked for during the gather is refused: the buffer stays paused");

    const QString dir = r.processor->lastSwingDir();
    const pinpoint::PersistedShot shot = pinpoint::SwingDocReader::readSwingJson(dir);
    const qint64 windowEndWallMs = shot.wallclockMs + 4000;   // clock.wallclock is the window's start; it spans 4 s
    check(shot.ok && std::llabs(windowEndWallMs - pauseWallMs) < 150,
          QStringLiteral("the window ends at the pause, not after the gather: %1 ms from the pause, %2 ms before the "
                         "gather ended").arg(windowEndWallMs - pauseWallMs).arg((processUs - gatherUs) / 1000));

    check(late.reserves == 1 && late.reservedImpactUs == r.lastImpactUs
              && late.reservedDeadlineUs == r.lastImpactUs + 7'000'000,
          "T3 reserved once, with the swing's impact and impact + 7 s");
    check(late.reservedAtUs >= r.lastTriggerUs && late.reservedAtUs - r.lastTriggerUs < 50'000 && late.reservedAtUs < gatherUs,
          QStringLiteral("T3 reserved at the trigger (%1 ms after it), before the pause").arg((late.reservedAtUs - r.lastTriggerUs) / 1000));
    check(late.takes == 1 && late.takenAtUs >= gatherUs + 1'400'000 && late.takenAtUs <= processUs,
          "T3 collected once, as the gather ends, before the window is processed");
    r.processor->endSessionFolder();
}

// T4: the gather's own deadline. A late sensor that never reports: the gather gives up 6 s after the pause and the
// shot goes on to its end (not an error, not a cancel). For a detector source (post-roll 1250 ms) the gather's deadline
// lies past the library's (the reservation's impact + 7 s), as the two are ordered on purpose.
// ⚠ AND FOR A MANUAL TRIGGER: its impact is the press itself (ShotController::commitShot: -1 is now), not back-dated, so
// the gather gives up at about impact + 6.5 s, before the library's deadline. Measured and printed here; not pinned
// either way, and reported.
void caseTimingDeadline(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("T4 the gather's deadline, 6 s past the pause\n");
    for (const auto source : { ShotController::Source::Imu, ShotController::Source::Manual }) {
        const bool detector = source == ShotController::Source::Imu;
        Rig r(iso, detector ? QStringLiteral("t4imu") : QStringLiteral("t4manual"));
        StandInDeferredSource never(-1);
        r.standInSources << &never;
        r.start();
        const bool idle = r.swing(source, /*backMs=*/0);
        const qint64 gatherUs = r.enteredUs(QStringLiteral("gathering")), processUs = r.enteredUs(QStringLiteral("processing"));
        const qint64 waitedMs = (processUs - gatherUs) / 1000;
        const qint64 oursPastImpactMs   = (processUs - never.reservedImpactUs) / 1000;
        const qint64 libraryPastImpactMs = (never.reservedDeadlineUs - never.reservedImpactUs) / 1000;
        if (detector) {
            check(idle && r.outcomes == 1, "Imu: the shot goes on to its end, with one terminal statement");
            check(waitedMs >= 6000 && waitedMs < 6300, QStringLiteral("Imu: the gather gives up %1 ms after the pause").arg(waitedMs));
            check(oursPastImpactMs > libraryPastImpactMs,
                  QStringLiteral("Imu: our deadline (%1 ms past impact) lies past the library's (%2 ms)")
                      .arg(oursPastImpactMs).arg(libraryPastImpactMs));
        } else {
            check(idle && waitedMs >= 6000 && waitedMs < 6300,
                  QStringLiteral("Manual: the gather gives up %1 ms after the pause").arg(waitedMs));
            std::printf("    NOTE Manual: our deadline lies %lld ms past impact, the library's %lld ms: %s\n",
                        (long long)oursPastImpactMs, (long long)libraryPastImpactMs,
                        oursPastImpactMs > libraryPastImpactMs ? "ordered" : "NOT ordered (ours first)");
        }
        r.processor->endSessionFolder();
    }
}

// ── Write pins and the scratch cases (#394, step 84; DUTIES-shot_processor.md rows 18, 25, 31, 34-37) ─────────

// The bytes of a file, hashed: a document that is rewritten changes them.
QByteArray hashOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

// The swing documents under one session's folder.
QStringList documentsIn(const QString &folder)
{
    QStringList out;
    QDirIterator it(folder, { QStringLiteral("swing.json"), QStringLiteral("swing.ppsw") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    out.sort();
    return out;
}

// A rig with the stand-in camera, the in-window replay off, and a full window of frames before its first swing.
std::unique_ptr<Rig> cameraRig(const pinpoint::test::IsolatedStores &iso, const QString &run, bool skipAnalysis = false)
{
    auto r = std::make_unique<Rig>(iso, run);
    r->appSettings.setAutoReplayAfterCapture(false);
    r->appSettings.setSaveRawFrames(skipAnalysis);
    r->appSettings.setSkipAnalysisForRawCapture(skipAnalysis);
    r->start();
    if (!r->connectStandInCamera(/*warmUpMs=*/4500)) {
        check(false, run + ": the stand-in camera is connected");
        return {};
    }
    return r;
}

// W1 the first write (R-REC-1) and W2 the staged writes, on the ordinary path (the analysis runs, and fails on the
// stand-in's frames). The swing is allocated only after its analysis, at the export; its media are written before its
// document; the document is written once, at the join, and announced (EVT-01) before the terminal statement; and End
// does not rewrite it.
void caseWrites(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("W1 the first write; W2 the staged writes\n");
    auto r = cameraRig(iso, QStringLiteral("w1"));
    if (!r) return;
    const QString session = r->processor->activeSessionDir();
    const pinpoint::SessionId running = r->processor->openSession() ? r->processor->openSession()->id() : pinpoint::SessionId();
    bool allocatedAtFreeze = true, documentAtMedia = true, mediaAtMedia = false;
    QObject::connect(r->processor.get(), &ShotProcessor::stateChanged, r->processor.get(), [&] {
        if (r->processor->stateName() == QStringLiteral("processing"))
            allocatedAtFreeze = !r->processor->lastSwingDir().isEmpty() || !QDir(session).entryList(
                                    { QStringLiteral("swing_*") }, QDir::Dirs).isEmpty();
    });
    QObject::connect(r->processor.get(), &ShotProcessor::swingSaved, r->processor.get(), [&](const QString &dir) {
        const QStringList files = QDir(dir).entryList(QDir::Files);
        mediaAtMedia    = files.contains(QStringLiteral("Stand-in-camera.mp4")) && files.contains(QStringLiteral("thumb.jpg"));
        documentAtMedia = !documentsIn(dir).isEmpty();
    });
    const bool idle = r->swing();
    check(idle, "the shot runs to its end");
    check(!allocatedAtFreeze, "W2 nothing is allocated at the freeze: no swing folder until the export");
    check(r->events.indexOf(QStringLiteral("analysisFailed")) >= 0
              && r->events.indexOf(QStringLiteral("analysisFailed")) < r->events.indexOf(QStringLiteral("mediaSaved")),
          "W2 the export (and with it the allocation) comes after the analysis: " + r->events.join(QLatin1Char(' ')));
    check(mediaAtMedia && !documentAtMedia, "W2 the media are written first, with no document yet");
    const QStringList docs = documentsIn(session);
    check(docs.size() == 1 && r->announcedShots.size() == 1 && r->announcedShots.front().session() == running
              && r->announcedShots.front().swingNumber() == 1,
          "W1 one document, the first shot of the session begun (by its id)");
    check(r->events.indexOf(QStringLiteral("announced")) > r->events.indexOf(QStringLiteral("mediaSaved"))
              && r->events.indexOf(QStringLiteral("announced")) < r->events.indexOf(QStringLiteral("outcome"))
              && r->events.count(QStringLiteral("announced")) == 1,
          "W1 the document is announced once (EVT-01), after the media and before the terminal statement");
    const QByteArray before = docs.isEmpty() ? QByteArray() : hashOf(docs.front());
    r->processor->endSessionFolder();
    check(!before.isEmpty() && hashOf(docs.value(0)) == before, "W1 End does not rewrite it: written once, by capture");
}

// W3 the Recording is read once, at the freeze (rows 18, 28): the session's club changed while the shot is processed
// does not reach its document, which records the club at the freeze.
void caseFrozenRecording(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("W3 the Recording, read once at the freeze\n");
    auto r = cameraRig(iso, QStringLiteral("w3"));
    if (!r) return;
    r->sessionController->setActiveClub(QStringLiteral("7i"));
    bool changed = false;
    QObject::connect(r->processor.get(), &ShotProcessor::stateChanged, r->processor.get(), [&] {
        if (r->processor->stateName() == QStringLiteral("processing") && !changed) {
            changed = true;
            QTimer::singleShot(0, r->processor.get(), [&] { r->sessionController->setActiveClub(QStringLiteral("PW")); });
        }
    });
    const bool idle = r->swing();
    const pinpoint::PersistedShot shot = pinpoint::SwingDocReader::readSwingJson(r->processor->lastSwingDir());
    check(idle && changed && shot.ok && shot.club == QStringLiteral("7i"),
          "the document records the club at the freeze (7i), not the one chosen while it was processed (PW): \"" + shot.club + "\"");
    r->processor->endSessionFolder();
}

// W4 a library that cannot be written (rows 26, 36, 43): the swing's folder cannot be made, so the shot says it is
// blocked (swingSaveBlocked), writes nothing, still lands a card (a card with no document, #235 (1)'s case), and makes
// one terminal statement.
void caseUnwritable(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("W4 a session folder that cannot be written\n");
    auto r = cameraRig(iso, QStringLiteral("w4"));
    if (!r) return;
    const QString session = r->processor->activeSessionDir();
    int blocked = 0;
    QObject::connect(r->processor.get(), &ShotProcessor::swingSaveBlocked, r->processor.get(), [&] { ++blocked; });
    const QFileDevice::Permissions was = QFile::permissions(session);
    QFile::setPermissions(session, QFileDevice::ReadOwner | QFileDevice::ExeOwner);   // read-only: no swing folder
    const bool idle = r->swing();
    QFile::setPermissions(session, was);
    check(idle && blocked == 1, QStringLiteral("the shot says it is blocked, once (%1)").arg(blocked));
    check(swingDocuments(r->library).isEmpty() && r->announced == 0, "nothing is written, nothing announced");
    check(r->shotModel.rowCount() == 1 && r->outcomes == 1, "a card lands all the same, with one terminal statement");
    r->processor->endSessionFolder();
}

// ── The ready rule (#394, step 85; the owner, 28 Nov 2026: one shot at a time) ────────────────────────────────

// The device sources (every one but the shot marker's) that have written this session, and whether each has written
// since the buffer's last resume: "active". The buffer's per-source count is zeroed at each resume (the rings are
// cleared); its session-lifetime count is not.
struct DeviceActivity {
    int devices = 0, active = 0;
    QStringList waiting;   // the names of those not yet active
    bool allActive() const { return devices > 0 && active == devices; }
};
DeviceActivity deviceActivity(pinpoint::EventBuffer &buffer)
{
    DeviceActivity a;
    for (const auto &src : buffer.diagnostics().sources) {
        if (src.identifier == "shot_controller" || src.lifetime_events_written == 0) continue;
        ++a.devices;
        if (src.events_written > 0) ++a.active;
        else a.waiting << QString::fromStdString(src.name);
    }
    return a;
}

// READY1: after a shot, the trigger arms again only once every device's buffer is active again: each camera's and each
// sensor's source has written since the resume. Taken through the shot trigger, as a golfer's shot is.
void caseReady(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("READY1 the trigger arms only once every device's buffer is active again\n");
    auto r = cameraRig(iso, QStringLiteral("ready1"));
    if (!r) return;
    check(waitFor([&] { return r->shotController->armed(); }, 5000), "armed after the Start, the camera connected");
    int rearms = 0;
    DeviceActivity atArm;
    bool sawShot = false;
    QObject::connect(r->shotController.get(), &ShotController::armedChanged, r->shotController.get(), [&] {
        if (!r->shotController->armed()) { sawShot = true; return; }
        if (!sawShot) return;
        ++rearms;
        atArm = deviceActivity(r->eventBuffer);
    });
    r->clearStates();
    r->shotController->triggerShot(ShotController::Source::Manual);
    check(r->processor->busy(), "the trigger's shot is taken");
    check(waitFor([&] { return !r->processor->busy(); }, 30000), "it runs to its end");
    check(waitFor([&] { return r->shotController->armed(); }, 5000), "and the trigger arms again");
    std::printf("    at the re-arm: %d of %d devices active (waiting: %s)\n", atArm.active, atArm.devices,
                qPrintable(atArm.waiting.join(QStringLiteral(", "))));
    check(rearms == 1 && atArm.devices >= 2 && atArm.allActive(),
          QStringLiteral("at the moment it armed, every device's buffer was active again (%1 of %2)")
              .arg(atArm.active).arg(atArm.devices));
    r->processor->endSessionFolder();
}

// ONE1 one shot at a time (the owner, 28 Nov 2026; #281, closed as by design). Through the shot trigger: from the
// shot's trigger until the trigger arms again (its analysis, its save, its in-window replay, the wait for every device)
// the trigger is disarmed and the header's "not ready" holds; a swing in that time is refused and nothing of it is
// taken; once armed, "not ready" falls. The shot here is not reviewable on disk (its frames hold no golfer), so it gets
// the in-window replay, which refuses the next swing too.
void caseOneAtATime(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("ONE1 one shot at a time\n");
    auto r = std::make_unique<Rig>(iso, QStringLiteral("one1"));
    r->appSettings.setAutoReplayAfterCapture(true);
    r->start();
    check(r->connectStandInCamera(4500), "the stand-in camera is connected");
    check(waitFor([&] { return r->shotController->armed() && !r->shotController->notReadyAfterShot(); }, 5000),
          "armed and ready before the shot");
    bool disarmedThroughout = true, notReadyThroughout = true, refusedDuringProcessing = false, refusedDuringReplay = false;
    int detected = 0;   // shots the trigger let through
    QObject::connect(r->shotController.get(), &ShotController::shotDetected, r->shotController.get(), [&] { ++detected; });
    QObject::connect(r->processor.get(), &ShotProcessor::stateChanged, r->processor.get(), [&] {
        const QString st = r->processor->stateName();
        if (st == QStringLiteral("idle")) return;
        // Asked a turn later: the processor announces its state before its busy flag, which the trigger hears next.
        QTimer::singleShot(0, r->processor.get(), [&] {
            if (!r->processor->busy()) return;
            disarmedThroughout = disarmedThroughout && !r->shotController->armed();
            notReadyThroughout = notReadyThroughout && r->shotController->notReadyAfterShot();
        });
        if (st == QStringLiteral("processing") || st == QStringLiteral("replaying")) {
            const bool replay = st == QStringLiteral("replaying");
            QTimer::singleShot(0, r->processor.get(), [&, replay] {
                const int before = detected;
                r->shotController->triggerShot(ShotController::Source::Manual);   // a swing now
                (replay ? refusedDuringReplay : refusedDuringProcessing) = detected == before && !r->shotController->armed();
            });
        }
    });
    r->clearStates();
    r->shotController->triggerShot(ShotController::Source::Manual);
    check(waitFor([&] { return !r->processor->busy(); }, 30000), "the shot runs to its end");
    check(r->states == QStringList({ "postroll", "processing", "replaying", "idle" }),
          "one shot, with its in-window replay: " + r->states.join(QLatin1Char(' ')));
    check(disarmedThroughout && notReadyThroughout, "the trigger is disarmed, and \"not ready\" holds, at every step of it");
    check(refusedDuringProcessing, "a swing while it is analysed and saved is refused");
    check(refusedDuringReplay, "a swing during its in-window replay is refused");
    check(waitFor([&] { return r->shotController->armed() && !r->shotController->notReadyAfterShot(); }, 5000),
          "then the trigger arms, and \"not ready\" falls");
    check(swingDocuments(r->library).size() == 1 && r->processor->lastSwingDir().endsWith(QStringLiteral("swing_0001")),
          "one document: nothing of the refused swings was taken");
    r->processor->endSessionFolder();
    check(!r->shotController->notReadyAfterShot(), "after End, nothing is waited for");
}

// ── The known bugs: each written to the right behaviour, and expected to fail on today's code ──────────────────
// Run one with --xfail <issue>; ctest runs each as its own test (cmake/PinPointCaptureRig.cmake), which passes only
// while the right behaviour is NOT seen (an expected failure), fails as soon as it is (the bug is gone: make the case
// an ordinary pin), and fails on a crash.

// Whether the run's library trash holds anything: a session discarded as empty goes there.
bool libraryTrashEmpty(const Rig &r)
{
    const QString trash = r.library + QStringLiteral("/.pinpoint-trash");
    return !QDir(trash).exists() || QDirIterator(trash, QDir::AllEntries | QDir::NoDotAndDotDot,
                                                 QDirIterator::Subdirectories).hasNext() == false;
}

// #188: a shot still being processed when End is pressed is saved into a stray new session. RIGHT: it lands in the
// session that was running when it was hit (by its id), and that session is not discarded as empty. Ended while its analysis runs, at the processor (the app's End now waits
// for a shot in flight unless the golfer says End anyway; that dialog is AppState's, not in the rig).
bool xfailEndInFlight(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("X188 End with a shot in flight\n");
    auto r = cameraRig(iso, QStringLiteral("x188"));
    if (!r) return false;
    const QString session = r->processor->activeSessionDir();
    const pinpoint::SessionId running = r->processor->openSession() ? r->processor->openSession()->id() : pinpoint::SessionId();
    bool ended = false;
    QObject::connect(r->processor.get(), &ShotProcessor::stateChanged, r->processor.get(), [&] {
        if (r->processor->stateName() == QStringLiteral("processing") && !ended) {
            ended = true;
            QTimer::singleShot(0, r->processor.get(), [&] { r->processor->endSessionFolder(); });   // the golfer's End
        }
    });
    const bool idle = r->swing();
    const QStringList all = swingDocuments(r->library);
    const pinpoint::SessionId landed = r->announcedShots.isEmpty() ? pinpoint::SessionId() : r->announcedShots.front().session();
    std::printf("    session running: %s (%s)\n    the shot's session: %s\n    documents: %s\n    the library's trash: %s\n",
                qPrintable(running.toString()), qPrintable(QDir(r->library).relativeFilePath(session)),
                qPrintable(landed.toString()), qPrintable(all.join(QStringLiteral(", "))),
                libraryTrashEmpty(*r) ? "empty" : "holds a discarded session");
    // What the case relies on, checked: the shot ran, was ended in flight, and was written and announced once. Only then
    // is the named behaviour asked.
    check(idle && ended && !running.isNull() && all.size() == 1 && r->announcedShots.size() == 1,
          "X188 the shot ran to its end, was ended in flight, and one document was written and announced");
    // ⚠ BY THE SESSION'S ID, NOT ITS FOLDER: today the running session is discarded as empty and a new one, started on
    // demand for the shot, takes the same folder name.
    return landed == running && libraryTrashEmpty(*r);
}

// #188, just before End: End pressed in the swing's post-roll. RIGHT: as above, it lands in its own session.
bool xfailEndInPostRoll(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("X188b End in the swing's post-roll\n");
    auto r = cameraRig(iso, QStringLiteral("x188b"));
    if (!r) return false;
    const QString session = r->processor->activeSessionDir();
    const pinpoint::SessionId running = r->processor->openSession() ? r->processor->openSession()->id() : pinpoint::SessionId();
    QTimer::singleShot(100, r->processor.get(), [&] { r->processor->endSessionFolder(); });   // 100 ms after the trigger
    const bool idle = r->swing();
    const QStringList all = swingDocuments(r->library);
    const pinpoint::SessionId landed = r->announcedShots.isEmpty() ? pinpoint::SessionId() : r->announcedShots.front().session();
    std::printf("    session running: %s (%s)\n    the shot's session: %s\n    documents: %s\n    the library's trash: %s\n",
                qPrintable(running.toString()), qPrintable(QDir(r->library).relativeFilePath(session)),
                qPrintable(landed.toString()), qPrintable(all.join(QStringLiteral(", "))),
                libraryTrashEmpty(*r) ? "empty" : "holds a discarded session");
    check(idle && !running.isNull() && all.size() == 1 && r->announcedShots.size() == 1,
          "X188b the shot ran to its end, and one document was written and announced");
    return landed == running && libraryTrashEmpty(*r);
}

// #53, what is left of its capture half under the ruling (the owner, 28 Nov 2026: one shot at a time; ready once every
// device captures again). A shot whose impact lies BEFORE ready is taken: a detector's, or a phone's, back-dated impact
// can name an instant when the trigger was still disarmed and the rings were still being cleared, as on 20 Oct (that
// swing's impact sits 264 ms before its first frame). RIGHT: a shot whose impact precedes the moment the trigger armed
// is refused, as the swing was hit while not ready. Today it is taken, and saved with no frame before its impact.
bool xfailImpactBeforeReady(const pinpoint::test::IsolatedStores &iso)
{
    std::printf("X53 a shot whose impact lies before ready\n");
    auto r = cameraRig(iso, QStringLiteral("x53"), /*skipAnalysis=*/true);
    if (!r) return false;
    r->shotController->triggerShot(ShotController::Source::Manual);
    const bool firstTaken = r->processor->busy();
    const bool firstDone  = waitFor([&] { return !r->processor->busy(); }, 30000);
    const bool ready      = waitFor([&] { return r->shotController->armed(); }, 5000);
    const qint64 readyUs  = pinpoint::EventBuffer::nowMicros();
    const QString firstDir = r->processor->lastSwingDir();
    // An acoustic detection committed now, its impact back-dated 400 ms, to before the trigger armed.
    r->shotController->triggerShot(ShotController::Source::Acoustic, readyUs - 400'000);
    const bool secondTaken = r->processor->busy();
    waitFor([&] { return !r->processor->busy(); }, 30000);
    if (secondTaken) {
        const pinpoint::PersistedShot two = pinpoint::SwingDocReader::readSwingJson(r->processor->lastSwingDir());
        std::printf("    taken; its capture warning: %s\n",
                    qPrintable(QJsonDocument(QJsonObject::fromVariantMap(two.dataWarningDetail)).toJson(QJsonDocument::Compact)));
    }
    r->processor->endSessionFolder();
    check(firstTaken && firstDone && ready && pinpoint::SwingDocReader::readSwingJson(firstDir).ok,
          "X53 the first shot was taken and saved, and the trigger armed again");
    return !secondTaken;
}

struct KnownBug { const char *issue; bool (*run)(const pinpoint::test::IsolatedStores &); const char *right; };
const KnownBug kKnownBugs[] = {
    { "188",  xfailEndInFlight,   "a shot ended while being processed lands in its own session" },
    { "188b", xfailEndInPostRoll, "a shot ended in its post-roll lands in its own session" },
    { "53",   xfailImpactBeforeReady, "a shot whose impact lies before ready is refused" },
};

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
    const auto registerStandInCamera = [] {
        DeviceEnumerator::instance()->registerDevice(DeviceType::VideoInput, VideoInputFactory::Backend::StandIn,
                                                     QStringLiteral("standin-camera-0"), QStringLiteral("Stand-in camera"),
                                                     VideoInputStandIn::capabilities());
    };

    // --xfail <issue>: one known bug, expected to fail (above).
    const QStringList args = QCoreApplication::arguments();
    if (const int at = args.indexOf(QStringLiteral("--xfail")); at >= 0) {
        const QString issue = args.value(at + 1);
        for (const KnownBug &bug : kKnownBugs) {
            if (issue != QLatin1String(bug.issue)) continue;
            registerStandInCamera();
            const bool right = bug.run(iso);
            if (g_fail) {
                std::printf("FAILED: a check this case relies on failed (%d)\n", g_fail);
                return 1;
            }
            std::printf(right ? "XPASS (#%s): %s, now. The bug is gone: make this an ordinary pin\n"
                              : "XFAIL (#%s, expected): today's code does not do it: %s\n",
                        bug.issue, bug.right);
            return right ? 1 : 0;
        }
        std::printf("no known bug %s\n", qPrintable(issue));
        return 2;
    }

    caseOneSwing(iso);
    caseTwoRunsEqual(iso);
    // The stand-in camera, listed from here on (a rig that does not connect it is camera-less, as above).
    registerStandInCamera();
    caseSavedSwing(iso);
    caseTwoSavedRunsEqual(iso);
    caseProcessedSwing(iso);
    caseDeferredReachable(iso);
    caseTimingPostRoll(iso);
    caseTimingGather(iso);
    caseTimingDeadline(iso);
    caseWrites(iso);
    caseFrozenRecording(iso);
    caseUnwritable(iso);
    caseReady(iso);
    caseOneAtATime(iso);

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
