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

#include <QGuiApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QQmlApplicationEngine>

#ifdef HAVE_PPCP
#include "../Ppcp/ppcp_import_controller.h"
#include "../Ppcp/ppcp_offer_controller.h"
#endif
#ifdef HAVE_PPCP_TRANSPORT
#include "../Ppcp/ppcp_host_service.h"
#endif
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <functional>
#include <memory>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <QTimer>
#include <QLocale>
#include <QQmlContext>
#include <QQuickStyle>

#include <cmath>

#include "pp_debug.h"
#include "headless_probe.h"   // PINPOINT_HEADLESS_PROBE=1: a probe's start, no device side (refactor 4-c)
#include "app_settings.h"
#include "notification_center.h"
#ifdef HAVE_PPCP_TRANSPORT
#include "shot/qml_payload.h"
#include "shot/ppcp_clip_filer.h"
#include "settings/library_converter.h"
#include "settings/archive_controller.h"
#endif
#include "app_info.h"
#ifdef HAVE_OPENCV
#  include <opencv2/core.hpp>
#  include <opencv2/core/utils/logger.hpp>
#endif
#include "SecretsManager.h"
#include "SecretsBridge.h"
#include "imu_manager.h"
#include "transcription_controller.h"
#include "tts_controller.h"
#include "camera_manager.h"
#include "camera_role_probe.h"
#include "buffer_controller.h"
#include "event_buffer.h"
#include "athlete_controller.h"
#include "navigation_controller.h"
#include "resource_monitor_controller.h"
#include "profiler_controller.h"
#include "update_controller.h"
#include "cuda_runtime_controller.h"
#include "motion_capture_probe.h"
#include "pp_os_metrics.h"
#include "clipboard_helper.h"
#include "app_log.h"
#include "llm_controller.h"
#include "session_controller.h"
#include "session_review_controller.h"
#include "work_ons_controller.h"
#include "studio_profile.h"
#include "report/session_report_model.h"
#include "report/report_stills.h"
#include "charts/charts_posture_presenter.h"
#include "charts/charts_delivery_presenter.h"
#include "charts/charts_work.h"       // ChartsLane: the charts' lanes on the one pool (refactor 5.5a)
#include "charts/session_series.h"   // SessionSeries: every loaded Session's series (refactor 5.5a)
#include "../Export/series_store.h"  // its read door
#include "report/session_report_list_model.h"
#include "report/report_mailer.h"
#include "../Diagnostics/packs/pack_provider.h"
#include "../DomainModel/rule_data.h"     // RuleData: the packs and norms, one per machine (step 3.2)
#include "../Export/rule_data_install.h" // the process's one RuleData, over its store
#include "../Metrics/metric_catalogue.h"
#include "shot_controller.h"
#include "shot_list_model.h"
#include "../Export/swing_doc.h"
#include "../Export/swing_paths.h"
#include "../Export/swing_zip_exporter.h"
#include "../Export/swing_store_repository.h"
#include "../Export/session_store.h"        // SessionRepository's adapter (step 2.2)
#include "../Export/recording_loader.h"     // SwingStoreRecordingReader (by ShotRef, step 2.4)
#include "../Export/athlete_store.h"        // AthleteRepository's adapter (step 2.3)
#include "../App/housekeeping.h"             // the one application service that ends things for good (DEC-80)
#include "../App/app.h"                      // `app` (D1) and the composition root's callables (D18 (c))
#include "../App/session_shot_source.h"      // the carousel's port onto a Session (refactor step 5.2; R-QML-5)
#include "../App/outcome_summary.h"             // the carousel's shot summary, AppState's doors (refactor 4-b)
#include "../App/athlete_catch_up.h"         // the catch-up, an application service (refactor 3.7-e)
#include "../DomainModel/summary_rules.h"    // summaryCohortFacts (3.7-e round 2)
#include "../Core/work_pool.h"                // the one pool and its receiver (D15): every session's owner (3.5)
#include "../DomainModel/session.h"
#include "../DomainModel/session_diagnostics.h"   // diagnosticsWorkTarget (3.5-ii)
#include "../DomainModel/athlete_diagnostics.h"   // athleteDiagnosticsWorkTarget: the athlete level's lane (3.7-f)
#include "../Export/athlete_cache_repository.h"   // PINPOINT_WORKONS_DIR's Athlete (3.7-f)
#include "../App/dev_athlete.h"                  // …and its rule (J-56, R2-m2)
#include "diagnostics/session_diagnostics_adapter.h"   // diagnosticsAdapters: each Session's thin adapter (3.7-a)
#include "diagnostics/session_diagnostics_model.h"   // its golfer commands' door (step 3.4b-i)
#include "launch_monitor_controller.h"
#include "shot_processor.h"
#include "shot_replay_controller.h"
#include "reanalysis_controller.h"
#include "../Pose/pose_runner.h"
#include "live_wrist_angles.h"
#include "trunk_ceremony_recorder.h"
#include "markup_controller.h"
#include "markup_image_provider.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

// True while the launch warm-up's own thread is inside PoseRunner::warmUp() (#20, #173).
std::atomic<bool> g_poseWarmUpInFlight{false};

// QUITTING DURING THE POSE WARM-UP ABANDONS IT; IT IS NEVER WAITED FOR (#20).
// The warm-up is one ORT CreateSession call — 20 s from the CoreML compiled cache, ~97 s cold —
// and nothing can cancel it part-way. It used to run on the global thread pool, and
// ~QCoreApplication waits for that pool (QThreadPool::waitForDone), so a quit in the first
// 20–97 s after launch froze the closed window until the load finished: measured 14.4 s on a
// warm cache. It now runs on its own detached thread (below), so the Qt teardown no longer waits.
// What is left is static destruction. ORT's statics and the app log (PpMessageLog, a
// function-local static) would be torn down while that thread is still inside ORT, and it logs
// when it returns. That is a crash risk inferred from those lifetimes; no such crash has been
// observed. So this guard is declared BEFORE `app` and destroyed AFTER it: every local of main(),
// QGuiApplication and the global pool have been shut down in order by then, and only if the
// warm-up is still building does it end the process there, skipping the static destructors (none
// of them persists anything: the PPCP statics are stopped on aboutToQuit). A normal quit, once the
// warm-up is done, returns from main() as before.
//
// THE POSE INFERENCE THREAD IS STOPPED HERE TOO (step 3.7; J-43; DEC-194): every cached ViTPose
// session runs on one long-lived thread PoseRunner owns (src/Pose/inference_thread.h). On a normal
// quit it is stopped and joined here, after `app` (whose destructor waits for the global pool, so
// no analysis is still asking it for work) and before any static is destroyed, so ORT and CUDA are
// never torn down under a running inference. Idle, it joins at once. While the warm-up is still
// building, that thread is inside the load, so the process ends below instead, as before.
//
// ⚠ WINDOWS: std::_Exit there still runs every DLL's DLL_PROCESS_DETACH (the CRT ends the process
// with ExitProcess, which first kills the other threads — the warm-up's among them, possibly holding
// a loader or ORT lock — and then detaches the DLLs, ORT and the CUDA runtime included). So Windows
// ends with TerminateProcess, which runs no detach at all. NOT TESTED: written on the Mac (11 Nov
// 2026); check a quit during the warm-up on the cabin PC.
struct AbandonPoseWarmUpAtExit {
    const int &exitCode;
    ~AbandonPoseWarmUpAtExit()
    {
        if (!g_poseWarmUpInFlight.load()) {
            PoseRunner::stopInferenceThread();
            return;
        }
        // The app log is still alive here (a function-local static, gone only after main()).
        ppInfo() << "[PoseRunner] warm-up still building at quit — abandoned, not waited for";
        std::fflush(stdout);
        std::fflush(stderr);
#ifdef Q_OS_WIN
        ::TerminateProcess(::GetCurrentProcess(), UINT(exitCode));
#endif
        std::_Exit(exitCode);
    }
};

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Single-instance / updater mutex. The Windows installer (Inno Setup's
    // AppMutex directive — see cmake/PinPointPackaging.cmake) checks for this
    // named mutex so it can detect a running instance and replace files in place
    // during a future (auto-)update. We only create and hold it; the app does not
    // itself reject a second instance. The handle is intentionally leaked — the OS
    // releases it on process exit. The name MUST match CPACK_INNOSETUP_SETUP_AppMutex.
    ::CreateMutexW(nullptr, FALSE, L"PinPointStudio.SingleInstance.Mutex");
#endif
    PinPointDebug::install();
#ifdef HAVE_OPENCV
    // WARN+ logger output still goes to std::cerr (no writer hook before
    // OpenCV 4.11) — captured into PpMessageLog by the cerr tee in
    // PinPointDebug::install() above.
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    cv::redirectError([](int status, const char *func, const char *msg,
                         const char * /*file*/, int /*line*/, void *) -> int {
        ppError() << "[OpenCV]" << (func ? func : "?") << "-" << (msg ? msg : "") << "(status" << status << ")";
        return 0;
    }, nullptr);
#endif

#if defined(Q_OS_LINUX)
    // Wayland/mutter does not let a client choose which monitor a window opens on
    // (see the geometry notes in Main.qml) — a secondary/kiosk post-shot display or
    // a pinned/cursor main window is placed by the compositor, not honoured. Under
    // XWayland (the xcb backend) Qt's setScreen()/geometry ARE honoured, so prefer
    // it WHEN the user actually relies on multi-display placement. Gated tightly so
    // single-display Wayland users keep native Wayland (and its better HiDPI on the
    // retina panel): only on a Wayland session with XWayland present (DISPLAY set),
    // only when QT_QPA_PLATFORM was not pinned by the user, and only when a
    // multi-display placement is configured. The platform is chosen once here, so a
    // change to these settings applies on the next launch.
    const bool onWayland = qgetenv("XDG_SESSION_TYPE") == "wayland"
                           || qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")
        && onWayland
        && qEnvironmentVariableIsSet("DISPLAY")) {   // DISPLAY set ⇒ XWayland available
        QSettings s = ppSettings();
        const QString mainMode = s.value(QStringLiteral("display/mainDisplayMode"),
                                         QStringLiteral("primary")).toString();
        const QString secMode  = s.value(QStringLiteral("display/secondaryDisplayMode"),
                                         QStringLiteral("none")).toString();
        if (mainMode != QLatin1String("primary") || secMode != QLatin1String("none"))
            qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    // Before `app`, so destroyed after it: see AbandonPoseWarmUpAtExit above.
    int exitCode = 0;
    const AbandonPoseWarmUpAtExit abandonPoseWarmUp{ exitCode };
    QGuiApplication app(argc, argv);

    // Force the Basic Qt Quick Controls style on all platforms. Without this, files
    // that import plain `QtQuick.Controls` fall back to the platform-native style; on
    // Windows that style paints an opaque ScrollBar groove (palette.window → white)
    // that ignores our dark Theme. Basic's track is transparent, matching macOS/Linux.
    // Most QML already imports QtQuick.Controls.Basic explicitly; this enforces the
    // same intent app-wide. Must be called before the engine instantiates any Control.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Register the GUI thread with the resource profiler so it shows a labelled
    // per-thread CPU row (the sampler that reads it runs on this thread too).
    pinpoint::osmetrics::registerThread("UI");

    // Must run after the app object exists: loads the Qt Multimedia FFmpeg
    // plugin (which clobbers any earlier av_log callback) and re-installs the
    // FFmpeg → PpMessageLog capture on top of it.
    PinPointDebug::installFfmpegLogCapture();

    // Load bundled fonts so Theme.qml family names resolve on all platforms. ENUMERATED, not
    // listed: the faces are cmake/PinPointFonts.cmake's one list, compiled into :/fonts, so adding
    // a face there reaches the app (and qml_ui_test, which reads the directory the same way) with
    // no second list to keep in step.
    //
    // Several families ship STATIC faces beside (or instead of) a variable file — Fraunces
    // Regular/SemiBold, Literata Regular/Medium, every Inter weight. macOS/CoreText won't
    // interpolate the variable weight axis of an application font, so a weight with no concrete
    // face registered under the family falls back to the system font (or, for Fraunces, to the
    // variable file's default instance, 9pt Black). Register every face; CoreText picks by weight.
    // Variable files first, statics after: the order the hand-written list always had, so a family
    // carrying both resolves a weight exactly as it did before the list was enumerated.
    {
        const QDir fontDir(QStringLiteral(":/fonts"));
        QStringList faces = fontDir.entryList({ QStringLiteral("*.ttf") }, QDir::Files, QDir::Name);
        std::stable_partition(faces.begin(), faces.end(), [](const QString &f) {
            return f.contains(QLatin1String("Variable"));
        });
        if (faces.isEmpty())
            ppWarn() << "[Fonts] no bundled faces under :/fonts — every Theme family falls back";
        for (const QString &face : faces) {
            if (QFontDatabase::addApplicationFont(fontDir.filePath(face)) < 0)
                ppWarn() << "[Fonts] failed to load" << face;
        }
    }

    app.setWindowIcon(QIcon(":/icons/pinpointstudio_256.png"));
    app.setDesktopFileName(QStringLiteral("pinpointstudio"));
    SecretsManager::initializeDefaults();

    // EventBuffer declared first — destroyed last (stack unwinds in reverse).
    // All controllers that hold a pointer to it must be destroyed first.
    pinpoint::EventBuffer   eventBuffer;
    eventBuffer.setLogCallback([](pinpoint::LogSeverity sev, const char *msg) {
        if (sev >= pinpoint::LogSeverity::Error)
            ppError() << msg;
        else
            ppWarn() << msg;
    });
    // Register the merger thread with the resource profiler (decoupled hook so the
    // Buffer library and its standalone tests stay free of any Core dependency).
    eventBuffer.setThreadRegisterHook([](const char *name) {
        pinpoint::osmetrics::registerThread(name);
    });
    // Merger runs for app lifetime. With no sources registered yet it
    // auto-pauses; the first registerSource() call auto-resumes it.
    eventBuffer.start();

    AppSettings              appSettings;
    // THE PACKS AND NORMS, ONE OWNER (RuleData, refactor step 3.2; D5: DEC-170). Made here, on the GUI
    // thread, before anything grades, so no reader ever builds them first on a worker (audit B-03); its
    // store is the one door to <AppData>/diagnostics/, the shipped content and SET-06 / SET-15. Installed
    // as the process's one for the readers not yet handed it (they read RuleData::current()), and as the
    // source of the shared norm slot the analysis passes read. The metric catalogue rides in its snapshot
    // (A-19). NEVER DESTROYED, as in the tools: a worker still holding a snapshot's reference when main
    // returns (the global pool drains in ~QCoreApplication, after main's locals) must not read freed memory.
    pinpoint::RuleData &ruleData = pinpoint::installProcessRuleData(
        std::make_shared<const pinpoint::analysis::MetricCatalogue>(pinpoint::analysis::makeMetricCatalogue()));
    // SET-06 and SET-15 stay where they are stored (the v1.9 reviewer's condition), and the settings page's
    // two AppSettings properties become a view of RuleData: read from its snapshot, written through its
    // commands, notified on its EVT-12 — so its repository is their only reader and writer.
    appSettings.bindRuleData(&ruleData);
    // THE ONE DOOR TO A SESSION'S OWN FILES AND ITS SWING NUMBERS (SessionRepository, step 2.2): the
    // session file, Start / resume / End, and the one swing allocator (D10) — handed to the capture
    // pipeline and the launch monitor below. GUI thread only, except swingFolder (any thread), which
    // the two swing doors below resolve every ShotRef through. Static and declared first, so it
    // outlives both doors (and the static phone-clip filer that holds one).
    static pinpoint::SessionStore sessionStore;
    // THE ONE DOOR TO THE SWING FILES (RecordingRepository, step 1.4; keyed by ShotRef since 2.4):
    // every writer below is handed this one instance. Stateless towards workers (it resolves a shot's
    // folder through sessionStore's published folders), so safe from them; static so the static
    // phone-clip filer further down never outlives it.
    static pinpoint::SwingStoreRecordingRepository recordingRepository(sessionStore);
    // THE READING DOOR (RecordingReader, step 1.5; by ShotRef since 2.4): what re-analysis reads a
    // stored shot through.
    static pinpoint::SwingStoreRecordingReader recordingReader(sessionStore);
    // A session a crash left in progress, ended by the athlete's next Start, gets its last swing's capture
    // end, marked as inferred (J-18: Mark, 15 Nov 2026): read through the swing door, which owns the format.
    sessionStore.setCaptureEndReader(&pinpoint::SwingStoreRecordingRepository::captureEndOf);
    // Storage panel: convert a JSON-era library to swing.ppsw, verified swing by swing.
    LibraryConverter         libraryConverter(&appSettings);
    // Archiving tab: move sessions out of the library and back, the trash, hours of capture left. Its "Empty now" and
    // the startup pass forward to housekeeping (below, once the athlete callables exist).
    ArchiveController        archiveController(&appSettings);
    // The session repository's worker-safe archive job (step 3.8): Session::archiveJob hands it out, housekeeping runs it.
    // ⚠ A LINT DODGE, recorded for Mark with J-9 (3.8-i NOTES, round 3): the job is the archiver's
    // (SessionArchiveRun::archiveInOwnRun), and the honest wiring is this file naming src/Export/session_archiver.h —
    // lint A5's count for main.cpp 8 → 9, an allowlist change only Mark makes. Until he decides, it comes through the
    // Archiving tab, which already includes the archiver.
    sessionStore.setArchiveJobs(ArchiveController::archiveJobs());
    // Read-only app/build/dependency info for the About box (appInfo context property).
    AppInfo                  appInfo;
    SecretsBridge            secrets;
    ImuManager              imuManager(&eventBuffer, &appSettings);
    TranscriptionController controller(&appSettings);
    TtsController           ttsController(&appSettings);
    LlmController           llmController(&appSettings);
    CameraManager           cameraManager(&eventBuffer, &appSettings);
    BufferController        bufferController(&eventBuffer);
    // EVERY LOADED SESSION'S DIAGNOSTICS RUN (refactor step 3.7-a; DEC-02; 04 step 3 item 4: "always fire"): every session
    // an Athlete loads — the live one at its Start, one a view opens — gets its one diagnostics owner, SessionDiagnostics,
    // which judges each shot as its document lands, whether or not a diagnostics panel is up, on the one pool (D15; J-3's
    // size) with the GUI thread's receiver, and writes diagnostics.json (the stage panel's model, its writer until 3.7-a, is
    // gone: the panels show this owner through their thin adapters), and — since the catch-up's cut-over (3.7-e) — the
    // session's summary, work_ons.json (WorkOnsController's private model and its write are gone: one writer of each).
    // Declared BEFORE the athlete store, so the receiver and the pool outlive every Athlete (DiagnosticsDoors).
    // THE PARALLEL RUN'S SWITCH (PINPOINT_DIAG_PARALLEL=1; refactor 3.5-ii, D14) IS GONE (refactor 3.10): since 3.7-e no
    // old model was left in the app to compare. Run P's comparison is regrade_ledger --engine=old|new.
    std::function<pinpoint::WorkTarget(const pinpoint::WorkAddress &)> diagLookup;   // set once the store exists
    // What the composition root does with each owner once made (its failed writes shown: DEC-126); set once the
    // notification centre exists, before any session is loaded.
    std::function<void(pinpoint::Session *, pinpoint::SessionDiagnostics *)> diagAttached;
    // The catch-up's ask for a live session's club baseline at Start (J-49): set once the catch-up exists (below).
    std::function<void(pinpoint::Session *)> liveBaselineAsk;
    // The catch-up's ear for a session resumed while loaded (J-49's residual, 3.7-f): set once the catch-up exists.
    std::function<void(pinpoint::Athlete *)> followResumes;
    // made before the pool, destroyed after it
    auto diagReceiver = std::make_unique<pinpoint::WorkReceiver>([&diagLookup](const pinpoint::WorkAddress &a) {
        return diagLookup ? diagLookup(a) : pinpoint::WorkTarget{};
    });
    auto diagPool = std::make_unique<pinpoint::WorkPool>();
    // THE CHARTS' LANES ON THE ONE POOL (refactor 5.5a; R-CON-5; J-74 Q6): every presenter's, still's and session series'
    // job runs here and comes back through the one receiver (charts_work.h). Uninstalled before the pool goes.
    pinpoint::charts::ChartsLane::install(diagPool.get(), diagReceiver.get());
    struct ChartsLaneUninstall { ~ChartsLaneUninstall() { pinpoint::charts::ChartsLane::uninstall(); } } chartsLaneUninstall;
    // THE ONE DOOR TO THE ATHLETE'S RECORD (AthleteRepository, step 2.3): athlete.json in each athlete's
    // folder, or this machine's settings record until migration M1. GUI thread only; declared before the
    // controller, so it outlives every Athlete the controller holds.
    pinpoint::AthleteStore  athleteStore([&appSettings] { return appSettings.athleteLibraryPath(); }, &sessionStore);
    // The doors the Athletes' sessions hand their shots (refactor step 3.4a): the one swing door and the one
    // reading door above, shared, never owned (both are static and outlive every Athlete).
    athleteStore.setShotDoors({ &recordingRepository,
                                std::shared_ptr<pinpoint::RecordingReader>(&recordingReader, [](pinpoint::RecordingReader *) {}) });
    diagLookup = [&athleteStore](const pinpoint::WorkAddress &a) {
        // A Charts lane's result (refactor 5.5a): its owner, by instance number, while it lives.
        if (pinpoint::charts::ChartsLane::owns(a))
            return pinpoint::charts::ChartsLane::target(a);
        // THE ATHLETE LEVEL'S OWN LANE (refactor 3.7-f; J-53): a Home job's result goes to that athlete's AthleteDiagnostics.
        if (pinpoint::isAthleteWorkOwner(a.owner)) {
            const pinpoint::AthleteId id = pinpoint::athleteOfWorkOwner(a.owner);
            for (pinpoint::Athlete *x : athleteStore.heldAthletes())
                if (x && x->id() == id) return pinpoint::athleteDiagnosticsWorkTarget(x, a);
            return pinpoint::athleteDiagnosticsWorkTarget(nullptr, a);   // OwnerGone
        }
        return pinpoint::diagnosticsWorkTarget(athleteStore.loadedSession(pinpoint::SessionId::fromString(a.owner)), a);
    };
    {
        // The app's doors, put together in ONE place (pinpoint::appDiagnosticsDoors, refactor 3.7-a), which
        // diag_stage_panel_test drives too; the attach told to the composition root (diagAttached, below).
        pinpoint::DiagnosticsDoors doors = pinpoint::appDiagnosticsDoors(diagPool.get(), diagReceiver.get(), &ruleData,
                                                                          sessionStore, recordingRepository);
        // EVERY SESSION'S SERIES (refactor 5.5a; J-74 Q1): attached with its diagnostics, created and ended with the
        // Session; its contents are read only when a view first asks (SessionSeries, charts/session_series.h).
        pinpoint::charts::SeriesDoors seriesDoors;
        seriesDoors.reader   = std::make_shared<pinpoint::SwingStoreSeriesReader>(sessionStore);
        seriesDoors.ruleData = &ruleData;
        doors.attach = [&diagAttached, seriesDoors](pinpoint::Session *s, const pinpoint::DiagnosticsDoors &d) {
            pinpoint::SessionDiagnostics *owner = pinpoint::SessionDiagnostics::attach(s, d);
            if (owner && diagAttached)
                diagAttached(s, owner);
            pinpoint::charts::SessionSeries::attach(s, seriesDoors);
            return owner;
        };
        athleteStore.setDiagnosticsDoors(doors);
    }
    // THE COMPOSITION ROOT'S CALLABLES (D18 (c); DEC-167): how the application services and AthleteController
    // reach the athletes, over the one athlete repository, which reads SET-03 on every call (so a library
    // re-pointed needs no new ones). AthleteController takes them (run 3.4a-ii); the catch-up in step 3.6; housekeeping
    // in step 3.8 (with its purge and size doors, below).
    const pinpoint::AthleteCallables athleteCallables{
        [&athleteStore] { return athleteStore.list(); },
        [&athleteStore](const pinpoint::AthleteId &id) { return athleteStore.load(id); },
        [&athleteStore](const pinpoint::AthleteRecord &record, pinpoint::WriteResult *result) {
            const pinpoint::AthleteCreateResult made = athleteStore.create(record);
            if (result)
                *result = made.result;
            return made.athlete;
        },
        [&athleteStore](const pinpoint::SessionOwner &owner) { return athleteStore.readActivity(owner); },   // thread-safe
        // THE UNASSIGNED OWNER (step 5.1; §5.11; DEC-198; J-67): its record written at the first need when `create`.
        [&athleteStore](bool create, pinpoint::WriteResult *result) { return athleteStore.unassigned(create, result); },
    };
    // HOUSEKEEPING (refactor step 3.8; DEC-80, DEC-137): the one application service that ends things for good — the one
    // purge, the startup pass (a session an Athlete holds archived through its Session) — and the library's size for the
    // settings page (DEC-188). Its doors are the composition root's callables (D18 (c): DEC-167): the athlete callables
    // above, and the purge and the size, carried out in Export (SwingPaths). The Archiving tab keeps a pointer (it is
    // stopped after exec, before this goes); the settings page keeps a copy of the size door.
    const pinpoint::Housekeeping housekeeping(pinpoint::HousekeepingDoors{
        athleteCallables,
        [](const QString &root, const QDateTime &cutoff) {
            const pinpoint::SwingPaths::Purged p = pinpoint::SwingPaths::purgeTrash(root, cutoff);
            return pinpoint::TrashPurge{ p.removed, p.failed };
        },
        [](const QString &root, const std::atomic_bool *abort) { return pinpoint::SwingPaths::libraryBytes(root, abort); },
    });
    appSettings.setLibrarySizer(housekeeping.librarySize());
    archiveController.setHousekeeping(&housekeeping);
    AthleteController       athleteController;
    athleteController.setCallables(athleteCallables, &sessionStore);
    // `app` (D1: DEC-164): app.sessionAt(folder), the Session of a folder a view still chooses itself, asked of
    // the current athlete. The diagnostics panel and the cast call it (refactor 3.7-a).
    pinpoint::App           appObject([&athleteController] { return athleteController.currentAthlete(); });
    // A session belongs to its athlete whoever is current (step 5.1; J-67): app.sessionAt and the door find a folder
    // through whichever athlete the repository holds — a late clip, reading or re-analysis after a switch of golfer, and
    // the Unassigned owner's sessions (a capture with no golfer chosen: §5.11).
    appObject.setAthletes([&athleteStore] { return athleteStore.heldAthletes(); });
    // THE SHOT'S AND THE SESSION'S COMMANDS (refactor step 3.4b-i): today's writers of a shot's facts forward to
    // them through `app` (ShotCommandDoor) and write nothing themselves. The diagnostics panel's model is made by
    // QML, so its door is process-wide; reset after exec (it outlives this local).
    SessionDiagnosticsModel::setShotCommands(&appObject);
    // THE SCREENS' WAY TO A SESSION'S DIAGNOSTICS (refactor 3.7-a): each Session's thin adapter, made by C++ (the stage
    // panel and the cast no longer make a model of their own). Outlives the QML engine (declared before it).
    SessionDiagnosticsAdapters diagnosticsAdapters;
    // THE QUIT SAVE (§4.4 step 8; CARRY 3.5-i §6; refactor 3.7-e: EVERY athlete's loaded sessions, not only the current
    // one's): what each loaded session's owner kept is written on aboutToQuit, before any of the app is torn down (the
    // live session's last shots inside their debounce, and its summary, which waits for End while it is live:
    // flush(final)). ~SessionDiagnostics still writes anything left, as the athlete store goes.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &athleteController, [&athleteStore] {
        for (pinpoint::Athlete *a : athleteStore.heldAthletes())
            for (pinpoint::Session *s : a->loadedSessions())
                if (pinpoint::SessionDiagnostics *d = s->diagnostics()) d->flush(/*final=*/true);
    });
    // A legacy session's owner, for the swing door's edge translation (J-39; confirmed by Mark, 15 Nov 2026): the current
    // athlete when the folder is theirs — the athlete the session list derives the same D3 id from.
    // Reset before the controller goes (the door is static).
    recordingRepository.setOwnerOf([&athleteController](const QString &athleteFolder) {
        return QDir::cleanPath(athleteController.currentFolder()) == QDir::cleanPath(athleteFolder)
                   ? pinpoint::AthleteId::fromString(athleteController.currentUuid())
                   : pinpoint::AthleteId();
    });
    // Live lead-wrist metrics (vs neutral) for the wizard "Check your sensor" overlay.
    LiveWristAngles         liveWrist(&imuManager, &appSettings, &athleteController);
    // The one sink every session- and shot-scoped notification posts to.
    // Constructed before the objects that post to it; see notification_center.h
    // for why identity (a stable id) is the whole mechanism.
    NotificationCenter      notificationCenter;
    // A SESSION'S DIAGNOSTICS THAT ARE NOT BEING SAVED (DEC-126; Stage 0.4c's notice; refactor step 3.7-a): every owner's
    // failed write (diagnostics.json, the club baseline) is said here, through the one notification centre, whether or not a
    // diagnostics panel is up — the owner now judges and writes with none (always fire) — one notice per session, taken back
    // by its next write that lands. A file it can only read (unopenable, or a newer build's) is said once, at its load. Set
    // before any session can be loaded (the doors above call it from every attach).
    diagAttached = [&notificationCenter, &liveBaselineAsk, &followResumes](pinpoint::Session *s, pinpoint::SessionDiagnostics *owner) {
        using N = NotificationCenter;
        const QString id = QStringLiteral("diagnostics.writes-blocked:") + s->id().toString();
        QString name = QObject::tr("a session");
        if (const auto e = s->entry(); e && e->startedAtUtc.isValid())
            name = QObject::tr("the %1 session").arg(e->startedAtUtc.toLocalTime().toString(QStringLiteral("ddd d MMM · HH:mm")));
        const auto post = [&notificationCenter, id, name](const QString &why) {
            N::Notification n;
            n.id       = id;
            n.severity = N::Warn;
            n.kind     = N::Condition;   // a state of the session, until a write lands
            n.title    = QObject::tr("Changes to the diagnostics of %1 are not being saved").arg(name);
            n.detail   = why;
            notificationCenter.post(n);
        };
        QObject::connect(owner, &pinpoint::SessionDiagnostics::writeFailed, &notificationCenter, post);
        QObject::connect(owner, &pinpoint::SessionDiagnostics::written, &notificationCenter, [&notificationCenter, id] {
            if (notificationCenter.isLive(id)) notificationCenter.resolve(id);
        });
        if (owner->readOnly())
            post(QObject::tr("Its diagnostics file could not be opened, or was saved by a newer version of PinPoint Studio, "
                             "so it is read and not written."));
        // A SESSION RESUMED WHILE ALREADY LOADED (J-49's residual; refactor 3.7-f): no attach then, so the catch-up hears
        // the athlete's resumes itself (AthleteCatchUp::followResumes, once per athlete) and asks the same.
        if (followResumes && s->athlete()) followResumes(s->athlete());
        // A SESSION STARTED HERE WITH NO CLUB BASELINE YET (J-49): a turn later (the Session is marked as recorded here
        // once its start returns), its predecessors are summed up first.
        QTimer::singleShot(0, owner, [owner, session = QPointer<pinpoint::Session>(s), &liveBaselineAsk] {
            if (session && session->runningHere() && session->inProgress() && owner->clubBaselineWaiting() && liveBaselineAsk)
                liveBaselineAsk(session);
        });
    };
    SessionController       sessionController(&athleteController);
    // No athlete switch while a session runs: AppState's rule (R-APP-1), which the controller asks (E-01; step 4-a).
    athleteController.setAppState(appObject.state());
    NavigationController    navController;
    // In-app updater façade: Linux AppImage engine, WinSparkle on Windows, Sparkle on
    // macOS (all behind this one QML context property). Constructed on all platforms
    // for QML uniformity; inert ("unsupported"/"devbuild") in a dev/build-tree run or
    // where no engine is compiled in. See docs/design/{linux,windows,macos}_update.md.
    UpdateController        updateController(&appSettings, &sessionController);
    // Hardware-adaptive CUDA-runtime offer (Windows): detects an NVIDIA GPU and offers
    // the separately-packaged GPU runtime when present but not installed, so users who
    // add a GPU later adapt without reinstalling. Inert off Windows. See §4.4.
    CudaRuntimeController   cudaRuntime;
    // Hardware probe behind the "Motion capture quality" setting: gates the High
    // tier on an accelerated pose backend + enough graphics memory, and measures
    // per-swing analysis time. Lazy — refresh() is kicked from the General panel,
    // and it defers while a session is active (passed sessionController for that).
    MotionCaptureProbe      motionCaptureProbe(&sessionController);
    ResourceMonitorController resourceMonitor(&eventBuffer, &cameraManager, &imuManager);
    // Resource profiler bridge — owns the single 1 s gauge sampler and the 60 s
    // stats-dump cadence. Per-session profile: reset at session start, dump at end.
    // [GpuMem] (refactor 3.7-I): one line at each start (before the reset, so its peak is the window just closed)
    // and each end; the sampler writes one every 30 s, and each shot's EVT-01 one (ShotProcessor's relay, below).
    ProfilerController        profilerController;
    QObject::connect(&sessionController, &SessionController::runningChanged,
                     &profilerController, [&sessionController, &profilerController]() {
        if (sessionController.running()) {
            ProfilerController::logGpuMem(QStringLiteral("session-start"));
            profilerController.reset();
        } else {
            ProfilerController::logGpuMem(QStringLiteral("session-end"));
            profilerController.dumpToLog();
        }
    });
    // Registers the shot-marker EventBuffer source; registering a first source
    // auto-resumes the buffer, so restore the user capture intent right after.
    ShotController            shotController(&eventBuffer, &sessionController);
    cameraManager.applyCaptureIntent();
    // THE CAROUSEL IS A VIEW (refactor step 5.2; R-QML-5): each instance shows one Session through its port, made here.
    const pinpoint::ShotSourceFor shotSourceFor = [&appObject](const QString &folder) {
        return pinpoint::shotSourceOf(appObject.sessionAt(folder));
    };
    ShotListModel             shotModel;
    shotModel.setSourceFactory(shotSourceFor);   // the session app.state names live (below)
    // Bulk "export selected shots to a zip" for the carousel ⋯ menu — derives
    // everything from the shot dirs it is handed, so it needs no dependencies.
    pinpoint::SwingZipExporter swingZipExporter;
    // The live carousel shows the current athlete's latest session from the start (app.state.showLatestSession, below,
    // once its doors are set: the Athlete's SessionIndex, not a folder scan of its own — step 5.2).
    // Session review: enumerate saved sessions + show one in its own view of that Session.
    SessionReviewController   sessionReviewController(&shotModel, &appSettings,
                                                      &athleteController);
    sessionReviewController.setRecordingRepository(&recordingRepository);   // a JSON-era swing's sidecar, at a load
    // Opening an ARCHIVED session brings it back: its stubs show at once, the restore runs on a
    // worker, and the session re-opens complete when it lands — if it is still the one on screen.
    QObject::connect(&sessionReviewController, &SessionReviewController::archivedSessionOpened,
                     &archiveController, &ArchiveController::restoreSession);
    QObject::connect(&archiveController, &ArchiveController::sessionRestored, &sessionReviewController,
                     [&sessionReviewController](const QString &dir) {
                         if (sessionReviewController.reviewActive()
                             && sessionReviewController.activeSessionId() == dir)
                             sessionReviewController.loadSession(dir);
                     });
    // WORK ONS — the faults the current athlete keeps producing, on the home screen
    // (docs/design/work_ons_design.md). It is handed the athlete's folder, the grade policy and
    // whether a session is live; it finds stale session records itself, so "end of session" is
    // nothing more than the pause lifting. The first pass is deferred so it never competes with
    // the launch.
    // THE CATCH-UP (refactor step 3.7-e, its cut-over; BRIEF §4.6; DEC-60, DEC-167, DEC-190): the application service that
    // takes each stale ended session of the current athlete in hand — its Session's SessionDiagnostics re-judges what is
    // stale, freezes its club baseline at first need, reduces and writes diagnostics.json and work_ons.json — and lets it
    // go (releasing what it loaded). WorkOnsController finds the stale sessions and asks it (its CatchUpDoor, below); its
    // write blocks go through WorkOnsController's notice route (DEC-126). Declared before workOns, so it outlives it.
    pinpoint::AthleteCatchUp catchUp(athleteCallables);
    // PINPOINT_WORKONS_DIR's own Athlete (refactor 3.7-f; made on first need by the work-ons' door, below), its caches'
    // repository: declared before workOns so they outlive it, after the session store so they go before it.
    pinpoint::AthleteCacheRepository   devCaches;
    std::unique_ptr<pinpoint::Athlete> devAthlete;
    WorkOnsController workOns;
    {
        const auto athleteDir = [&appSettings, &athleteController]() -> QString {
            // Dev seam, like PINPOINT_CORE_PACK: point the list at a folder of sessions that is
            // not the library, so a probe can exercise the catch-up without writing into it.
            const QString forced = qEnvironmentVariable("PINPOINT_WORKONS_DIR");
            if (!forced.isEmpty()) return forced;
            if (appSettings.athleteLibraryPath().isEmpty()) return QString();
            // NO ATHLETE-LEVEL FIGURES FOR THE UNASSIGNED OWNER (J-67; R-UNASSIGNED-5's principle): its sessions are
            // different golfers', and pooling them into tendencies, themes and work-ons is wrong data. Home says so.
            if (athleteController.currentIsUnassigned()) return QString();
            return athleteController.currentFolder();   // D4: the folder chosen once, kept on a rename
        };
        const auto repoint = [&workOns, athleteDir] { workOns.setAthleteDir(athleteDir()); };
        workOns.setGradePolicy(appSettings.diagnosticsGradePolicy());
        QObject::connect(&appSettings, &AppSettings::diagnosticsGradePolicyChanged, &workOns,
                         [&workOns, &appSettings] { workOns.setGradePolicy(appSettings.diagnosticsGradePolicy()); });
        QObject::connect(&athleteController, &AthleteController::currentAthleteChanged, &workOns, repoint);
        // A settings athlete's folder still follows a rename (until M1): idempotent, so every edit may ask.
        QObject::connect(&athleteController, &AthleteController::athletesChanged, &workOns, repoint);
        QObject::connect(&appSettings, &AppSettings::athleteLibraryPathChanged, &workOns, repoint);
        // Queued: both end-session paths call sessionController.endSession() and THEN
        // shotProcessor.endSessionFolder(), which may trash a folder that captured nothing.
        QObject::connect(&sessionController, &SessionController::activeSessionTypeChanged, &workOns,
                         [&workOns, &sessionController] { workOns.setPaused(sessionController.activeSessionType() >= 0); },
                         Qt::QueuedConnection);
        QTimer::singleShot(3'000, &workOns, repoint);
        // The catch-up's door: the folders WorkOnsController's scan found stale, for the athlete whose folder it reads
        // (the current one; PINPOINT_WORKONS_DIR's dev folder is no athlete's, so nothing is derived there: 3.7-e J-e7).
        // FOLDERS ARE TRANSLATED TO IDS HERE, AT THE EDGE (DEC-90; review R1-m5): the service takes the current athlete's
        // SessionIds and answers by id; a folder its index does not hold is answered at once, and WorkOnsController keeps whatever that folder's file holds.
        const auto folderToId = [&athleteController](const QString &folder) {
            pinpoint::Athlete *a = athleteController.currentAthlete();
            if (a)
                for (const pinpoint::SessionIndexEntry &e : a->index())
                    if (QDir::cleanPath(e.folder) == QDir::cleanPath(folder)) return e.id();
            return pinpoint::SessionId();
        };
        workOns.setCatchUp({
            [&catchUp, &athleteController, &workOns, folderToId](const QString &athleteDir,
                                                                const QList<WorkOnsController::CatchUpAsk> &asks) {
                pinpoint::Athlete *a = athleteController.currentAthlete();
                const bool mine = a && QDir::cleanPath(a->folder()) == QDir::cleanPath(athleteDir);
                QList<pinpoint::SessionId> ids;
                QSet<pinpoint::SessionId> baselineOnly;
                QStringList notHeld;
                for (const WorkOnsController::CatchUpAsk &k : asks) {
                    const pinpoint::SessionId id = mine ? folderToId(k.sessionDir) : pinpoint::SessionId();
                    if (id.isNull()) { notHeld << k.sessionDir; continue; }
                    ids << id;
                    if (k.baselineOnly) baselineOnly.insert(id);
                }
                for (const QString &f : std::as_const(notHeld)) workOns.sessionCaughtUp(QFileInfo(f).fileName());
                if (ids.isEmpty()) { workOns.catchUpDone(); return; }
                catchUp.runSessions(a->id(), ids, baselineOnly);
            },
            [&catchUp](bool on) { catchUp.setPaused(on); },
            [&catchUp] { catchUp.cancel(); },
            [&athleteController] {   // the cohort facts the summaries' fingerprint carries (R-STAMP-1; 3.7-e round 2)
                const pinpoint::Athlete *a = athleteController.currentAthlete();
                return a ? pinpoint::summaryCohortFacts(a->record().dob, a->record().sex)
                         : pinpoint::summaryCohortFacts(QString(), QString());
            },
        });
        QObject::connect(&catchUp, &pinpoint::AthleteCatchUp::sessionCaughtUp, &workOns,
                         [&workOns, &athleteController](const pinpoint::SessionId &id, const QString &) {
                             if (pinpoint::Athlete *a = athleteController.currentAthlete())
                                 for (const pinpoint::SessionIndexEntry &e : a->index())
                                     if (e.id() == id) { workOns.sessionCaughtUp(e.identity.folderName()); break; }   // by name (R1-m10)
                         });
        QObject::connect(&catchUp, &pinpoint::AthleteCatchUp::finished, &workOns, [&workOns] { workOns.catchUpDone(); });
        // THE LIVE SESSION'S CLUB BASELINE AT START (J-49; review R1-B1): a session just started here whose owner could
        // not freeze its baseline (earlier sessions not summed up by this build) has those summed up first, before the
        // live pause takes hold.
        liveBaselineAsk = [&catchUp](pinpoint::Session *s) {
            if (pinpoint::Athlete *a = s ? s->athlete() : nullptr) catchUp.catchUpForBaseline(a->id(), s->id());
        };
        followResumes = [&catchUp](pinpoint::Athlete *a) { catchUp.followResumes(a); };
        // THE ATHLETE'S DOOR (refactor 3.7-f): Home reads the current Athlete's AthleteDiagnostics when its folder is the
        // list's. PINPOINT_WORKONS_DIR (a dev and probe seam: swing_themes_home.qml, build/run-me/verify-swing-themes.sh)
        // names a folder that may be no listed athlete's: an Athlete is made for it here over the one session store, its
        // derived caches kept beside its sessions (a cache-only repository), its jobs inline (no app doors) — under THE
        // FOLDER'S OWN ATHLETE ID, from its athlete.json (J-56: never one made up from its name, C5). A folder with no
        // athlete record gets no Athlete: Home shows its empty state, and the app log says why.
        workOns.setAthleteDoor([&athleteController, &athleteStore, &devAthlete, &devCaches](const QString &dir) -> pinpoint::AthleteLevel * {
            pinpoint::Athlete *a = athleteController.currentAthlete();
            if (a && QDir::cleanPath(a->folder()) == QDir::cleanPath(dir)) return pinpoint::AthleteDiagnostics::of(a);
            const QString forced = qEnvironmentVariable("PINPOINT_WORKONS_DIR");
            if (forced.isEmpty() || QDir::cleanPath(forced) != QDir::cleanPath(dir)) return nullptr;
            // J-56 / R2-m2 (src/App/dev_athlete.h): the folder's own athlete id; one the store knows is the store's Athlete.
            const QString folder = QDir::cleanPath(dir);
            const pinpoint::AthleteFiles::Read r = pinpoint::AthleteFiles::read(folder);
            const pinpoint::AthleteRecord rec = r.isRead()
                ? pinpoint::AthleteRecord::fromMap(r.json.value(QStringLiteral("record")).toObject().toVariantMap()) : pinpoint::AthleteRecord();
            pinpoint::DevAthleteDoors doors;
            doors.held     = athleteStore.heldAthletes();
            doors.listed   = athleteStore.list();
            doors.load     = [&athleteStore](const pinpoint::AthleteId &id) { return athleteStore.load(id); };
            doors.caches   = &devCaches;
            doors.sessions = &sessionStore;
            QString why;
            pinpoint::Athlete *a2 = pinpoint::devAthleteOf(folder, r.isRead() ? r.id : pinpoint::AthleteId(), rec, doors, devAthlete, &why);
            static QString said;
            if (said != why) ppInfo() << "[WorkOns] PINPOINT_WORKONS_DIR" << folder << "-" << why;
            said = why;
            return a2 ? pinpoint::AthleteDiagnostics::of(a2) : nullptr;
        });
        // "Your distances" on the athlete page (refactor 3.7-f): that athlete's own athlete level.
        athleteController.setAthleteLevelDoor([](pinpoint::Athlete *a) -> pinpoint::AthleteLevel * {
            return pinpoint::AthleteDiagnostics::of(a);
        });
        QObject::connect(&catchUp, &pinpoint::AthleteCatchUp::writeBlocked, &workOns, &WorkOnsController::storageProblem);
    }

    // THE SESSION REPORT (docs/design/session_report_design.md) — the studio and its one coach, the
    // model that makes a session's report, the golfer's session list with each report's state, and the
    // hand-off to the coach's mail app. The list follows the current athlete exactly as the work-ons do
    // (and honours the same PINPOINT_WORKONS_DIR dev seam), and refreshes when a work-on record lands.
    StudioProfile          studioProfile;
    SessionReportModel     sessionReport;
    SessionReportListModel sessionReportList;
    ReportMailer           reportMailer;
    sessionReport.setStudioProfile(&studioProfile);
    sessionReport.setAthleteController(&athleteController);
    // THE SESSION'S OWN DIAGNOSIS (refactor step 3.7-d): the report reads each session's owner through that Session's thin
    // adapter for the "report" surface, reached as the stage panel reaches its own (app.sessionAt, then
    // diagnosticsAdapters.of) — it makes no model of its own. Both outlive the report (declared before it).
    // A SESSION THE MAKE LOADED IS RELEASED AFTER IT (refactor 3.7-e; CARRY 3.7-d M6, Q-18): a bulk make over N sessions
    // must not leave N Sessions and owners loaded. When the folder's Session was not loaded before the make, it is
    // released once the report's adapter is gone (its use dropped) — refused, as ever, while anything else holds it.
    sessionReport.setDiagnosisDoor([&appObject, &diagnosticsAdapters, &athleteController, &catchUp](const QString &dir) {
        pinpoint::Athlete *a = athleteController.currentAthlete();
        pinpoint::SessionId id;
        if (a)
            for (const pinpoint::SessionIndexEntry &e : a->index())
                if (QDir::cleanPath(e.folder) == QDir::cleanPath(dir)) { id = e.id(); break; }
        const bool wasLoaded = !a || id.isNull() || a->loadedSession(id);
        const pinpoint::AthleteId athleteId = a ? a->id() : pinpoint::AthleteId();
        SessionDiagnosticsModel *m = diagnosticsAdapters.of(appObject.sessionAt(dir), QStringLiteral("report"), dir);
        if (!wasLoaded && m && m->isAdapter()) {
            QPointer<pinpoint::Athlete> athlete(a);
            QObject::connect(m, &QObject::destroyed, a, [athlete, athleteId, id, &catchUp] {
                if (!athlete) return;
                QTimer::singleShot(0, athlete.data(), [athlete, athleteId, id, &catchUp] {
                    QString why;
                    if (athlete && athlete->loadedSession(id) && !athlete->releaseSession(id, &why)) {
                        // Held by another for now (the catch-up, housekeeping): released once free (review R1-m7).
                        ppInfo() << "[Report] the session a make loaded is held (" << why << "): released once free";
                        catchUp.releaseWhenFree(athleteId, id);
                    }
                });
            });
        }
        return m;
    });
    // The athlete level the report reads (refactor 3.7-f): the current athlete's, for a session in its folder.
    // The report's athlete level: THE SESSION'S OWN golfer's (REVIEW-f R2-B1), any athlete the store holds — so the
    // archive's re-stamp (S-D6) reads every golfer's left-out sessions from that golfer's owner, never the current one's.
    sessionReport.setAthleteDoor([&athleteStore](const QString &dir) -> pinpoint::AthleteLevel * {
        return pinpoint::athleteLevelOfSession(athleteStore.heldAthletes(), dir);
    });
    // A session with no golfer (the Unassigned owner's, in its folder or its old `unknown/` one: §5.11): its report reads no
    // athlete level and no same-day siblings, and says why (step 5.1 review B1; J-67 7). Path text only, nothing read.
    // The report's private carousel lists the session's shots through `app`, as every carousel does (step 5.1).
    sessionReport.setShotSourceFor(shotSourceFor);
    // THE REPORT'S RECORDS (refactor step 5.6a; FILE-SESS-07, 09, 13): read for every row through the athlete whose folder
    // holds the session, as app.sessionAt finds it (the current athlete, then the library's others), never holding a
    // Session (Athlete::readReportAt); and saved through the session's own commands (app.sessionAt, then
    // Session::recordReportMake / recordReportSend / saveReport / the stills writer).
    sessionReport.setReportReader([&athleteController, &athleteStore](const QString &dir) {
        pinpoint::Athlete *current = athleteController.currentAthlete();
        if (current)
            if (const pinpoint::ReportRead r = current->readReportAt(dir); r.any())
                return r;
        for (pinpoint::Athlete *a : athleteStore.heldAthletes())
            if (a && a != current)
                if (const pinpoint::ReportRead r = a->readReportAt(dir); r.any())
                    return r;
        return pinpoint::ReportRead{};
    });
    sessionReport.setSessionDoor([&appObject](const QString &dir) { return appObject.sessionAt(dir); });
    const auto noGolferAt = [&athleteStore](const QString &dir) {
        const QString parent = QDir::cleanPath(QFileInfo(QDir::cleanPath(dir)).absolutePath());
        for (const pinpoint::Athlete *a : athleteStore.heldAthletes()) {
            if (!a || !a->isUnassigned())
                continue;
            if (QDir::cleanPath(QFileInfo(a->folder()).absoluteFilePath()) == parent)
                return true;
            for (const QString &o : a->otherFolders())
                if (QDir::cleanPath(QFileInfo(o).absoluteFilePath()) == parent)
                    return true;
        }
        return false;
    };
    sessionReport.setNoGolferTest(noGolferAt);
    // THE GOLFER'S OTHER SESSIONS FOR THE CHARTS DELIVERY AND POSTURE SECTIONS (refactor 5.5d; DEC-74): their summaries'
    // day readings, from the session's own golfer's athlete level — the report's door — on the GUI thread; none for a
    // session with no golfer (J-68 B1: nothing pooled across golfers). A presenter hears that athlete level change (a
    // summary written: AthleteLevel::Event::Changed) and reads again.
    {
        const auto levelOf = [&athleteStore, noGolferAt](const QString &dir) -> pinpoint::AthleteLevel * {
            return dir.trimmed().isEmpty() || noGolferAt(dir) ? nullptr
                                                              : pinpoint::athleteLevelOfSession(athleteStore.heldAthletes(), dir);
        };
        pinpoint::charts::NeighboursDoor door;
        door.read = [levelOf](const QString &dir) { return pinpoint::charts::neighboursFrom(levelOf(dir), dir); };
        door.watch = [levelOf](QObject *context, const QString &dir, std::function<void()> changed) {
            pinpoint::AthleteLevel *level = levelOf(dir);
            if (!level) return QList<QMetaObject::Connection>{};
            return level->watch(context, [changed](pinpoint::AthleteLevel::Event e, const QString &, const QString &) {
                if (e == pinpoint::AthleteLevel::Event::Changed) changed();
            });
        };
        pinpoint::charts::setNeighboursDoor(std::move(door));
    }
    sessionReportList.setReportModel(&sessionReport);
    // Archive and restore re-stamp a report that was current, so it still reads current (S-D6).
    archiveController.setReportFingerprint([&sessionReport] { return sessionReport.fingerprintFunction(); });
    {
        const auto athleteDir = [&appSettings, &athleteController]() -> QString {
            const QString forced = qEnvironmentVariable("PINPOINT_WORKONS_DIR");
            if (!forced.isEmpty()) return forced;
            if (appSettings.athleteLibraryPath().isEmpty()) return QString();
            return athleteController.currentFolder();
        };
        const auto repoint = [&sessionReportList, athleteDir] { sessionReportList.setAthleteDir(athleteDir()); };
        // The library's athletes and every golfer's session count and last session (the picker's rows,
        // and its Recent order), derived from each one's SessionIndex — re-read when the library moves.
        QObject::connect(&appSettings, &AppSettings::athleteLibraryPathChanged,
                         &athleteController, &AthleteController::refreshActivity);
        QObject::connect(&athleteController, &AthleteController::currentAthleteChanged, &sessionReportList, repoint);
        QObject::connect(&athleteController, &AthleteController::athletesChanged, &sessionReportList, repoint);
        QObject::connect(&appSettings, &AppSettings::athleteLibraryPathChanged, &sessionReportList, repoint);
        // setReportModel already follows stateChanged(dir) for that one row; a work-on record landing
        // re-scans (work-on 1 and the swing count can both have moved). The live session's row offers
        // Open session only, and work-on 1 is quoted as the pack's goal, from the pack itself.
        // "Live" means a session is RUNNING, as the work-ons pause reads it — not merely that the
        // carousel holds a session's shots: at launch the carousel restores the golfer's latest
        // session, and that session must still offer its report.
        // The archiver is told the same folder: the running session is never archived under it.
        const auto followLive = [&sessionReportList, &archiveController, &sessionReviewController, &sessionController] {
            const QString live = sessionController.activeSessionType() >= 0 ? sessionReviewController.liveSessionDir()
                                                                             : QString();
            sessionReportList.setLiveSessionDir(live);
            archiveController.setLiveSessionDir(live);
        };
        QObject::connect(&sessionReviewController, &SessionReviewController::liveSessionDirChanged,
                         &sessionReportList, followLive);
        QObject::connect(&sessionController, &SessionController::activeSessionTypeChanged,
                         &sessionReportList, followLive);
        followLive();
        // The goal from the CURRENT snapshot, so a saved edit to a condition's goal reaches the list as it
        // reaches a report made after it (audit B-02: this used to freeze a private pack at first use).
        sessionReportList.setGoalResolver([&ruleData](const QString &id) {
            const std::shared_ptr<const pinpoint::RuleDataSnapshot> rules = ruleData.snapshot();
            const pinpoint::analysis::Condition *c = rules->pack->pack().condition(id);
            return c ? c->goal : QString();
        });
        QObject::connect(&workOns, &WorkOnsController::itemsChanged, &sessionReportList, &SessionReportListModel::refresh);
        QObject::connect(&workOns, &WorkOnsController::tendenciesLanded, &athleteController, &AthleteController::clubDistancesChanged);
        QTimer::singleShot(3'500, &sessionReportList, repoint);
    }

    // The startup housekeeping pass (trash retention, archive by age / below a floor), every part
    // of it off until set — housekeeping's, run on the Archiving tab's worker. Deferred so it never competes with the
    // launch itself.
    QTimer::singleShot(60'000, &archiveController, &ArchiveController::runHousekeeping);
    // Sessions and the live carousel are both athlete-scoped, but the reload above only
    // ever ran once, at startup — so switching athlete left BOTH showing the previous
    // athlete's shots. Re-point them whenever the current athlete changes.
    QObject::connect(&athleteController, &AthleteController::currentAthleteChanged,
                     &sessionReviewController, &SessionReviewController::onAthleteChanged);
    // The live carousel follows the switch through app.state (refreshAthlete: the new athlete's latest session; step 5.2).
    // THE WRIST REFERENCE ★ ON THE ATHLETE'S RECORD (D5: DEC-152). The Charts views still carry it as
    // a swing folder (appSettings.wristReferenceSwingDir); the controller translates at its edge. A ★
    // is recorded on the current athlete's record; a switch brings that athlete's ★ back. An athlete
    // whose record names none keeps today's per-machine setting (the fallback until M4 converts it).
    QObject::connect(&appSettings, &AppSettings::wristReferenceSwingDirChanged, &athleteController,
                     [&appSettings, &athleteController] {
        athleteController.setWristReferenceSwingDir(appSettings.wristReferenceSwingDir());
    });
    {
        const auto showRecordedReference = [&appSettings, &athleteController] {
            const QString recorded = athleteController.wristReferenceSwingDir();
            if (!recorded.isEmpty() && recorded != appSettings.wristReferenceSwingDir())
                appSettings.setWristReferenceSwingDir(recorded);
        };
        QObject::connect(&athleteController, &AthleteController::currentAthleteChanged, &appSettings,
                         showRecordedReference);
        showRecordedReference();
    }
    // DATA-SOURCE transition (a past session opened/closed from disk). This is the
    // ONLY axis that pauses capture: opening a loaded session stops live capture
    // (which disarms the shot trigger via the buffer state) and gates ShotController;
    // the session/clock keep running so returning to live resumes the SAME session.
    // The live-capture record state is remembered here and restored on return, so
    // pressing Capture (→ resumeLive) reinstates exactly what was running — and stays
    // idle if capture was idle. The orthogonal session MODE (Capture/Replay/Analyse)
    // is owned by the SessionMode QML singleton; Main.qml lands a loaded session in
    // Replay and a return-to-live in Capture.
    QObject::connect(&sessionReviewController, &SessionReviewController::reviewActiveChanged,
                     [&sessionReviewController, &shotController, &cameraManager,
                      wasCapturing = false]() mutable {
        const bool review = sessionReviewController.reviewActive();
        shotController.setReviewActive(review);
        if (review) {
            wasCapturing = cameraManager.captureIntent();   // remember before pausing
            cameraManager.stopCapture();
        } else if (wasCapturing) {
            wasCapturing = false;
            cameraManager.startCapture();                   // restore on return to live
        }
    });
    // Declared after cameraManager so it is destroyed FIRST: ~ShotProcessor
    // joins the shot workers and destroys the SwingWindow before
    // ~CameraManager deregisters sources and ~EventBuffer frees ring memory.
    ShotProcessor             shotProcessor(&eventBuffer, &cameraManager, &imuManager,
                                            &appSettings, &athleteController,
                                            &sessionController, &shotModel);
    shotProcessor.setRecordingRepository(&recordingRepository);   // the live join's first write (step 1.5)
    // With no golfer chosen (or a library re-pointed to one that does not list the golfer: D7, J-67), capture's session
    // is the library's Unassigned owner's, a full Session (§5.11; DEC-198); its record is written at the first Start.
    shotProcessor.setUnassignedAthlete(athleteCallables.unassigned);
    // D7 (DEC-154): a library re-pointed mid-session ends the session and starts a new one, same type and
    // club, the declared focus carried — the ShotProcessor reads it from the ending Session and hands it here as the
    // pending pick, which AppState hands to the first Session that opens, from its first shot (refactor 4-d).
    QObject::connect(&shotProcessor, &ShotProcessor::focusCarried, &sessionController,
                     [&sessionController](const QString &focus) { sessionController.setFocusConditionId(focus); },
                     Qt::DirectConnection);
    // [GpuMem] (refactor 3.7-I): one app-log line at each shot's EVT-01 (a relay, not a domain event).
    QObject::connect(&shotProcessor, &ShotProcessor::shotDocumentAnnounced, &profilerController,
                     [](const pinpoint::ShotRef &shot) {
        ProfilerController::logGpuMem(QStringLiteral("EVT-01 shot=") + shot.toString());
    });
    cameraManager.setShotProcessor(&shotProcessor);   // teardown stop-barrier
    imuManager.setShotProcessor(&shotProcessor);      // same barrier for IMU deselect

    // The trunk ceremony recorder (trunk_imu_sequence_grf_design.md §16.2 W2.1c): records the trunk
    // sensors (and a worn wG3) through session setup's calibration and writes trunk_ceremony.json
    // into the session folder when the session starts. Declared after imuManager and shotProcessor
    // so it is destroyed first.
    TrunkCeremonyRecorder     trunkCeremony(&imuManager, &athleteController);
    trunkCeremony.setSessionDirResolver([&sessionController, &shotProcessor]() {
        return sessionController.running() ? shotProcessor.activeSessionDir() : QString();
    });
    QObject::connect(&sessionController, &SessionController::sessionStarted,
                     &trunkCeremony, [&trunkCeremony](int) { trunkCeremony.onSessionStarted(); });

    // The launch monitor. Constructed unconditionally: with nothing configured it
    // holds an inert connector reporting Disabled, so QML can bind to it without
    // checking, and the toolbar dot stays dark of its own accord.
    LaunchMonitorController   launchMonitorController(&appSettings, &shotModel,
                                                      &athleteController, &sessionController,
                                                      &cameraManager);
    launchMonitorController.setRecordingRepository(&recordingRepository);
    launchMonitorController.setShotCommands(&appObject);   // a late reading is its Shot's completeCapture (3.4b-i)
    // One allocator (D10): a standalone device-only shot goes into the running session, added by the capture
    // service to the Session it captures into (Session::addShot: DEC-142); its document's landing is EVT-01's
    // moment (D19), reported back to that Session.
    launchMonitorController.setShotMaker([&shotProcessor] {
        pinpoint::SwingAllocation a;
        shotProcessor.addDeviceOnlyShot(&a);
        return LaunchMonitorController::NewShot{ a.ok() ? a.shot : pinpoint::ShotRef(), a.swingFolder, a.swingName,
                                                 a.sessionFolderName, a.result.error };
    });
    launchMonitorController.setDocumentWritten([&shotProcessor](const pinpoint::ShotRef &shot) {
        shotProcessor.shotDocumentWritten(shot);
    });
    // Disk-backed replay of saved shots (MP4 + swing.json) — independent of the
    // live SwingWindow that ShotProcessor owns for the just-captured shot.
    ShotReplayController      shotReplay(&appSettings);
    // The replay follows its shot (refactor 5.7c): the edge (DEC-90) from the replayed folder to the Shot its Session
    // holds, so a re-analysis (EVT-02) or a late part (EVT-22) is read again through that Shot with no reload.
    shotReplay.setShotResolver([&appObject](const QString &swingDir) -> pinpoint::Shot * {
        pinpoint::Session *s = appObject.sessionAt(pinpoint::sessionFolderOf(swingDir));
        return s ? s->shotAtFolder(swingDir) : nullptr;
    });
    // APP.STATE (refactor step 4-a; src/App/app_state.h): the mode, the selection, the session on screen and the
    // session's start and end, over today's objects. Each door is one call; AppState fixes the order.
    {
        pinpoint::AppState &state = *appObject.state();
        state.setReplay(&shotReplay);
        pinpoint::AppStateDoors d;
        d.currentAthlete = [&athleteController] { return athleteController.currentAthlete(); };
        d.sessionAt      = [&appObject](const QString &folder) { return appObject.sessionAt(folder); };
        d.sessionRunning = [&sessionController] { return sessionController.running(); };
        d.reviewFolder   = [&sessionReviewController] {
            return sessionReviewController.reviewActive() ? sessionReviewController.activeSessionId() : QString();
        };
        d.captureFolder  = [&shotProcessor] { return shotProcessor.activeSessionDir(); };
        d.resumeLive     = [&sessionReviewController] { sessionReviewController.resumeLive(); };
        d.replayStart    = [&shotReplay](int swingNumber, const QString &dir, bool autoPlay) {
            shotReplay.start(swingNumber, dir, 0.25, autoPlay);
        };
        d.replayStop     = [&shotReplay] { shotReplay.stop(); };
        d.replayFolder   = [&shotReplay] { return shotReplay.swingDir(); };
        d.autoReplayAfterSelect = [&appSettings] { return appSettings.autoReplayAfterSelect(); };
        d.openCaptureSession  = [&shotProcessor, &sessionController](bool extend) {
            shotProcessor.beginSessionFolder(sessionController.defaultType(), extend);
        };
        d.startClock          = [&sessionController] { sessionController.start(sessionController.defaultType()); };
        d.startCameras        = [&cameraManager] { cameraManager.startCapture(); };
        d.stopCameras         = [&cameraManager] { cameraManager.stopCapture(); };
        d.stopClock           = [&sessionController] { sessionController.endSession(); };
        d.closeCaptureSession = [&shotProcessor] { shotProcessor.endSessionFolder(); };
        d.todaysFolder        = [&shotProcessor, &sessionController] {
            return shotProcessor.todaySessionDir(sessionController.defaultType());
        };
        // The declared focus waiting for a Session (refactor 4-d): the controller holds the pick until a Session takes it.
        d.pendingFocus        = [&sessionController] { return sessionController.focusConditionId(); };
        d.dropPendingFocus    = [&sessionController] { sessionController.setFocusConditionId(QString()); };
        d.declareStartFocus   = [&shotProcessor](const QString &focus) { return shotProcessor.declareStartFocus(focus); };
        // The carousel's shot summary (refactor 4-b): worked out from the owners on each read, never kept.
        d.shotOutcome         = &pinpoint::outcomeSummaryOf;
        d.watchOutcomeSummary = &pinpoint::watchOutcomeSummary;
        state.setDoors(std::move(d));
        const auto onScreen = [&state] { state.refreshSessionOnScreen(); };
        QObject::connect(&sessionReviewController, &SessionReviewController::reviewActiveChanged, &state, onScreen);
        QObject::connect(&sessionReviewController, &SessionReviewController::liveSessionDirChanged, &state, onScreen);
        QObject::connect(&shotProcessor, &ShotProcessor::activeSessionDirChanged, &state,
                         [&state] { state.refreshCaptureTarget(); });
        // THE LIVE CAROUSEL, a view of the session app.state names live (step 5.2): the capture target, else the live
        // folder. The session it already shows is no change (End, re-entering a screen: no reset). Capture letting go
        // (End) drops the cards of shots that saved nothing and the live presentation, as End's reload did.
        QObject::connect(&state, &pinpoint::AppState::liveSessionChanged, &shotModel,
                         [&state, &shotModel] { shotModel.showSession(state.liveSessionFolder()); });
        QObject::connect(&state, &pinpoint::AppState::captureTargetChanged, &shotModel, [&state, &shotModel] {
            if (state.captureTargetFolder().isEmpty())
                shotModel.forgetCapture();
        });
        QObject::connect(&athleteController, &AthleteController::currentAthleteChanged, &state,
                         [&state] { state.refreshAthlete(); });
        QObject::connect(&sessionController, &SessionController::runningChanged, &state,
                         [&state] { state.refreshAthleteLock(); });
        QObject::connect(&shotReplay, &ShotReplayController::playbackEnded, &state, &pinpoint::AppState::replayEnded);
        state.showLatestSession();   // launch: the current athlete's latest session on the live carousel
        state.refreshSessionOnScreen();
    }
    // Any review-state transition tears down an on-screen disk replay, so the
    // previous shot's replay stage + metric graph don't linger over the newly
    // selected (or resumed-live) session. reviewActiveChanged fires on every
    // loadSession()/resumeLive() — including session→session while already in
    // review — so this covers entering review, switching sessions, and resuming
    // live. stop() is idempotent (no-ops when nothing is replaying).
    QObject::connect(&sessionReviewController, &SessionReviewController::reviewActiveChanged,
                     &shotReplay, &ShotReplayController::stop);
    ClipboardHelper           clipboardHelper;
    // QML → ppInfo/ppWarn (app_log.h). console.* is the Qt handler, not the app log.
    AppLog                    appLog;

    // Re-analyse funnel for the carousel action bar: reloads each exported swing
    // (streaming SwingDiskLoader), re-runs the analyzer on a worker, writes the
    // result back, and emits reanalysed(dir) so the carousel refreshes the row in
    // whichever model is active (live or review). Owns no live shot state.
    ReanalysisController      reanalysisController;
    // The shot is found, read through its Shot's RecordingSource and written back by Shot::applyAnalysis (EVT-02;
    // refactor step 3.4b-ii), through the Session that holds it; one no Session holds is refused (step 5.1: N1 gone).
    reanalysisController.setShotCommands(&appObject);
    // Storage → Save pose keypoints, honoured by the write-back as by the live save (asked at each
    // write, on the GUI thread).
    reanalysisController.setPersistOptions([&appSettings] {
        pinpoint::PersistOptions o;
        o.savePose = appSettings.savePoseKeypoints();
        return o;
    });

    // DELETE LETS GO OF THE SWING FIRST (#56). The replay holds the swing's videos
    // open — the focused one, and in a live session with a cast screen the newest
    // one too — and Windows will not move a folder with a file open in it. Both
    // carousel models (live, and the session under review) ask before moving one:
    // the replay stops if it is playing that folder, and a swing the re-analysis
    // queue still owes a write is refused with the reason.
    for (ShotListModel *m : { &shotModel, qobject_cast<ShotListModel *>(sessionReviewController.shots()) }) {
        if (!m) continue;
        m->setSourceFactory(shotSourceFor);   // a view of its Session; the edits go to the row's Shot (step 5.2)
        m->setFolderReleaser([&shotReplay](const QString &dir) {
            if (shotReplay.active() && QDir::cleanPath(shotReplay.swingDir()) == QDir::cleanPath(dir)) {
                ppInfo() << "[ShotListModel] stopping the replay to release" << dir;
                shotReplay.stop();
            }
        });
        m->setFolderBusyCheck([&reanalysisController](const QString &dir) {
            return reanalysisController.holds(dir)
                       ? QCoreApplication::translate("ShotListModel", "is being re-analysed — try again when it finishes")
                       : QString();
        });
    }

    // Markup Lab — in-app ground-truth labelling of recorded swings; reads the swing's
    // MP4 and overlay from disk (no buffer/SessionMode coupling); its truth.json is read
    // and written by the swing's Shot (setShotCommands, below). The image provider is
    // owned by the engine (addImageProvider, below).
    MarkupController          markupController;

    // IMU source register/deregister can change the shared EventBuffer state
    // (first-source auto-resume / last-source auto-pause). Re-apply the user
    // capture intent so cameraManager.bufferState — the QML-facing buffer
    // state — stays correct and notifies.
    QObject::connect(&imuManager, &ImuManager::bufferStateChanged,
                     &cameraManager, &CameraManager::applyCaptureIntent);

    // Shot trigger arms/disarms with the buffer state. bufferStateChanged is
    // the single always-notified buffer-state signal (IMU-caused transitions
    // are forwarded through applyCaptureIntent above).
    QObject::connect(&cameraManager, &CameraManager::bufferStateChanged,
                     &shotController, &ShotController::reevaluateArmed);

    // Detected shots drive the processor pipeline (post-roll → window capture
    // → analysis ∥ export → carousel → replay); the processor's busy state
    // disarms the trigger for the whole pipeline.
    QObject::connect(&shotController, &ShotController::shotDetected,
                     &shotProcessor,  &ShotProcessor::onShotDetected);

    // Launch monitor attribution. A shot commits, which arms the pairing for that
    // swing; the analyzer/exporter join hands over a swingDir, which flushes whatever
    // the monitor delivered in the meantime. See ShotPairing for why order alone is
    // enough to decide which swing a reading belongs to.
    //
    // The impact also goes over on the WALL clock, for a connector whose readings carry the
    // device's own contact time (Foresight Premier): the pairing checks the two agree, which is
    // what catches a shot the device silently dropped. impactUs is on EventBuffer's steady
    // clock, so it is carried across by "how long ago", taken back to back with the UTC read.
    QObject::connect(&shotController, &ShotController::shotDetected,
                     &launchMonitorController,
                     [&launchMonitorController](ShotController::Source, qint64 impactUs, int) {
        const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 agoUs    = pinpoint::EventBuffer::nowMicros() - impactUs;
        launchMonitorController.onShotDetected(impactUs > 0 ? nowUtcMs - agoUs / 1000 : 0);
    });
    // A Premier shot's club data lands with the NEXT shot — while that one is being captured.
    // It is held until the pipeline is idle, and until any re-analysis owed that folder is in.
    QObject::connect(&shotProcessor, &ShotProcessor::busyChanged,
                     &launchMonitorController, [&launchMonitorController, &shotProcessor] {
        launchMonitorController.setPipelineBusy(shotProcessor.busy());
    });
    launchMonitorController.setFolderBusyCheck([&reanalysisController](const QString &dir) {
        return reanalysisController.holds(dir);
    });
    // Following Premier's club corrected a swing analysed under the old one: analyse it again.
    launchMonitorController.setReanalyseRequest([&reanalysisController](const QString &dir) {
        reanalysisController.reanalyse(QVariantList{ dir });
    });
    QObject::connect(&reanalysisController, &ReanalysisController::reanalysed,
                     &launchMonitorController, &LaunchMonitorController::onSwingRewritten);
    QObject::connect(&shotProcessor, &ShotProcessor::shotProcessed,
                     &launchMonitorController, &LaunchMonitorController::onShotProcessed);
    // The processor allocates the swing folder before it runs anything, so a shot that
    // produced no document still has somewhere for a reading to go. analysisFailed is
    // NOT wired here: it fires while export may still succeed, and the join's shotFailed
    // is the one that means the shot produced nothing at all.
    QObject::connect(&shotProcessor, &ShotProcessor::shotFailed,
                     &launchMonitorController,
                     [&launchMonitorController, &shotProcessor](const QString &) {
        launchMonitorController.onShotFailed(shotProcessor.lastSwingDir(),
                                            shotProcessor.lastShotId());
    });
    QObject::connect(&shotProcessor,  &ShotProcessor::busyChanged,
                     &shotController, [&shotController, &shotProcessor] {
        shotController.setProcessorBusy(shotProcessor.busy());
    });
    // Hold any in-progress re-analysis batch between swings while a live shot is
    // processing — keeps two ViTPose passes from oversubscribing the CPU / OOMing.
    QObject::connect(&shotProcessor,      &ShotProcessor::busyChanged,
                     &reanalysisController, [&reanalysisController, &shotProcessor] {
        reanalysisController.setLiveBusy(shotProcessor.busy());
    });

    // ── Notifications: the act-now half of the one log ─────────────────
    //
    // These four wirings used to live as `Connections` blocks in Main.qml, each
    // driving its own hand-placed PpToast.  They are here now for the reason
    // every other cross-object join is here: the policy is C++'s, and the view
    // should only draw.  What each one carries that it did not before is a
    // STABLE ID — see notification_center.h.
    //
    // ⚠ The ids are the contract.  "swing.save.no-directory" is named as the
    // `cause` of the consequences below it, and the preflight raises that SAME
    // id before the first shot, so the shot-time occurrence coalesces into the
    // notification already on screen instead of starting a second story.
    {
        using N = NotificationCenter;

        QObject::connect(&shotProcessor, &ShotProcessor::swingSaveBlocked,
                         &notificationCenter, [&notificationCenter](const QString &detail) {
            N::Notification n;
            n.id          = QStringLiteral("swing.save.no-directory");
            n.severity    = N::Error;
            n.kind        = N::Condition;   // a state of the machine, not news
            n.title       = QObject::tr("Swings are not being saved");
            n.detail      = detail;
            // When the preflight has already said the library root is
            // unwritable, this is the same news in different words — R2 keeps it
            // in the log.  When the root IS fine and only the folder failed,
            // nothing suppresses it and it is shown on its own merits.
            n.cause       = QStringLiteral("library.unwritable");
            n.actionLabel = QObject::tr("Open Storage settings");
            n.actionId    = QStringLiteral("settings.storage");
            notificationCenter.post(n);
        });

        QObject::connect(&shotProcessor, &ShotProcessor::swingSaveFailed,
                         &notificationCenter, [&notificationCenter](const QString &error) {
            N::Notification n;
            n.id       = QStringLiteral("swing.save.failed");
            n.severity = N::Error;
            n.kind     = N::Event;          // this export failed; the next may not
            n.title    = QObject::tr("Swing save failed");
            n.detail   = error;
            n.cause    = QStringLiteral("swing.save.no-directory");
            notificationCenter.post(n);
        });

        QObject::connect(&shotProcessor, &ShotProcessor::analysisFailed,
                         &notificationCenter, [&notificationCenter](const QString &error) {
            N::Notification n;
            n.id       = QStringLiteral("shot.analysis.failed");
            n.severity = N::Warn;
            n.kind     = N::Event;
            n.title    = QObject::tr("Shot analysis failed");
            n.detail   = error;
            // A degraded analysis is a CONSEQUENCE of there being nowhere to
            // save: while that condition is on screen this stays in the log.
            n.cause    = QStringLiteral("swing.save.no-directory");
            notificationCenter.post(n);
        });

        QObject::connect(&shotController, &ShotController::shotRefused,
                         &notificationCenter,
                         [&notificationCenter](const QString &reason, const QString &id) {
            N::Notification n;
            n.id       = id;                // the controller names which fault this is
            n.severity = N::Warn;
            n.kind     = N::Event;
            n.title    = reason;            // already translated, already a sentence
            n.glyph    = QStringLiteral("⊘");
            notificationCenter.post(n);
        });

        // §7.5 R4 — the terminal statement.  Silence when everything worked:
        // the carousel row IS the feedback, and a toast per successful shot is
        // noise a golfer hitting a bucket would drown in.
        QObject::connect(&shotProcessor, &ShotProcessor::shotOutcome,
                         &notificationCenter,
                         [&notificationCenter](int ordinal, bool exportOk, bool analysisOk) {
            if (exportOk && analysisOk) return;

            N::Notification n;
            n.kind  = N::Event;
            n.glyph = QStringLiteral("◎");
            const QString shot = ordinal > 0 ? QObject::tr("Shot %1").arg(ordinal)
                                             : QObject::tr("The shot");
            if (!exportOk) {
                n.id       = QStringLiteral("shot.not-saved");
                n.severity = N::Error;
                n.title    = QObject::tr("%1 not saved").arg(shot);
                // Names the condition, so while "swings are not being saved" is
                // on screen this stays in the log instead of repeating it once
                // per shot — which is exactly what happened ten times over.
                n.cause    = QStringLiteral("swing.save.no-directory");
            } else {
                n.id       = QStringLiteral("shot.analysis.incomplete");
                n.severity = N::Warn;
                n.title    = QObject::tr("%1 saved — analysis incomplete").arg(shot);
                n.cause    = QStringLiteral("swing.save.no-directory");
            }
            notificationCenter.post(n);
        });

        // A delete the picker's prompt described wrongly (J-30): refused, or the folder kept because
        // another athlete shares it. The prompt's words stay as they are; this notice corrects them.
        QObject::connect(&athleteController, &AthleteController::deleteNotice,
                         &notificationCenter, [&notificationCenter](const QString &title, const QString &detail) {
            N::Notification n;
            n.id       = QStringLiteral("athlete.delete.notice");
            n.severity = N::Warn;
            n.kind     = N::Event;
            n.title    = title;
            n.detail   = detail;
            notificationCenter.post(n);
        });
        // The session's own file could not be written at End (DEC-126): said, not only logged. The session
        // stays in progress on disk until the athlete's next Start ends it (DEC-73).
        QObject::connect(&shotProcessor, &ShotProcessor::sessionFileFailed,
                         &notificationCenter, [&notificationCenter](const QString &error) {
            N::Notification n;
            n.id       = QStringLiteral("session.file.failed");
            n.severity = N::Warn;
            n.kind     = N::Event;
            n.title    = QObject::tr("The session's end could not be saved");
            n.detail   = error;
            notificationCenter.post(n);
        });
        // A session's work-ons record, or the athlete's swing summary or tendencies, could not be saved
        // (DEC-126; step 2.5): said, one notice per folder, so a failure that repeats is one notice.
        QObject::connect(&workOns, &WorkOnsController::storageProblem, &notificationCenter,
                         [&notificationCenter](const QString &what, const QString &where, const QString &why) {
            N::Notification n;
            n.id       = QStringLiteral("storage.workons.failed:") + where;
            n.severity = N::Warn;
            n.kind     = N::Event;
            n.title    = QObject::tr("%1 could not be saved").arg(what);
            n.detail   = why;
            notificationCenter.post(n);
        });
        // A SHOT'S EDIT, A LAUNCH MONITOR READING, OR THE TUNING LOG THAT DID NOT SAVE (DEC-126; refactor step 3.7-a):
        // logged only until now. Each is said, one notice per kind (a failure that repeats is one notice, counted): the
        // carousel's edits (the live list's and the reviewed session's), the monitor's reading and its late club data,
        // and the current athlete's tuning log (a flag or a label correction).
        const auto saveNotice = [&notificationCenter](const QString &id) {
            return [&notificationCenter, id](const QString &what, const QString &why) {
                N::Notification n;
                n.id       = id;
                n.severity = N::Warn;
                n.kind     = N::Event;
                n.title    = QObject::tr("%1 could not be saved").arg(what);
                n.detail   = why;
                notificationCenter.post(n);
            };
        };
        for (ShotListModel *m : { &shotModel, qobject_cast<ShotListModel *>(sessionReviewController.shots()) })
            if (m) QObject::connect(m, &ShotListModel::saveFailed, &notificationCenter, saveNotice(QStringLiteral("shot.edit.not-saved")));
        QObject::connect(&launchMonitorController, &LaunchMonitorController::saveFailed, &notificationCenter,
                         saveNotice(QStringLiteral("lm.reading.not-saved")));
        {
            static QMetaObject::Connection tuning;   // the current athlete's, re-made when the athlete changes
            const auto hearAthlete = [&athleteController, &notificationCenter, saveNotice] {
                QObject::disconnect(tuning);
                if (pinpoint::Athlete *a = athleteController.currentAthlete())
                    tuning = QObject::connect(a, &pinpoint::Athlete::writeFailed, &notificationCenter,
                                              saveNotice(QStringLiteral("athlete.tuning-log.not-saved")));
            };
            hearAthlete();
            QObject::connect(&athleteController, &AthleteController::currentAthleteChanged, &notificationCenter, hearAthlete);
        }
        // "Continue today's session" refused or not saved (DEC-126): a new session started instead, and
        // the golfer is told why.
        QObject::connect(&shotProcessor, &ShotProcessor::sessionResumeFailed,
                         &notificationCenter, [&notificationCenter](const QString &error) {
            N::Notification n;
            n.id       = QStringLiteral("session.resume.failed");
            n.severity = N::Warn;
            n.kind     = N::Event;
            n.title    = QObject::tr("Today's session could not be continued, so a new one started");
            n.detail   = error;
            notificationCenter.post(n);
        });

        QObject::connect(&launchMonitorController, &LaunchMonitorController::deviceOnlyShotSaved,
                         &notificationCenter, [&notificationCenter](const QString &) {
            // ⚠ REPLACES the analysis failure rather than stacking under it, as
            // the old deviceOnlyToast deliberately did: "Shot analysis failed"
            // describes the pipeline, and telling somebody their shot failed
            // when it was in fact saved is the wrong sentence in the one place
            // they look.
            notificationCenter.dismiss(QStringLiteral("shot.analysis.failed"));
            notificationCenter.dismiss(QStringLiteral("shot.analysis.incomplete"));

            N::Notification n;
            n.id       = QStringLiteral("shot.saved.device-only");
            n.severity = N::Info;
            n.kind     = N::Event;
            n.title    = QObject::tr("Launch monitor only — shot saved without video or analysis");
            n.glyph    = QStringLiteral("◎");
            notificationCenter.post(n);
        });
    }

    // ── Session-start preflight (design §7.7) ────────────────────────
    //
    // ⛔ A SESSION MUST NOT START INTO A STATE WHERE EVERY SHOT IS GUARANTEED TO
    // FAIL.  Every fault of 1 September 2026 was knowable before the golfer
    // swung: the library root is resolvable at session start and whether it is
    // writable is one mkpath of a probe directory.  Ten identical toasts
    // afterwards were the symptom; not asking beforehand was the defect.
    //
    // Hung off SessionController::sessionStarted because BOTH start paths — the
    // wizard and the toolbar's Capture button — run beginSessionFolder() and
    // then start(), so this needs no QML change and never goes near the
    // session-setup QML.
    QObject::connect(&sessionController, &SessionController::sessionStarted,
                     &notificationCenter, [&]( int) {
        using N = NotificationCenter;
        const QString root = appSettings.athleteLibraryPath().trimmed();

        // Resolved conditions first: a session starting is the moment to retire
        // last session's verdict, so a fixed path stops shouting on its own.
        for (const char *id : { "library.path-unset", "library.unwritable",
                                "library.low-space", "swing.save.no-directory" })
            notificationCenter.resolve(QLatin1String(id));

        if (root.isEmpty()) {
            // swing_paths.cpp silently redirects to AppDataLocation here, which
            // means the swings are somewhere the user did not choose and will
            // not think to look.
            N::Notification n;
            n.id          = QStringLiteral("library.path-unset");
            n.severity    = N::Warn;
            n.kind        = N::Condition;
            n.title       = QObject::tr("No athlete library folder is set");
            n.detail      = QObject::tr("Swings will be saved to the application data folder.");
            n.actionLabel = QObject::tr("Open Storage settings");
            n.actionId    = QStringLiteral("settings.storage");
            notificationCenter.post(n);
        } else {
            // §7.7's one mkpath: cheap, and it answers the only question that
            // matters — can this session write where it is about to write.
            const QString probe = root + QStringLiteral("/.pinpoint-write-probe");
            QDir          probeDir(probe);
            const bool    writable = QDir().mkpath(probe);
            if (writable) probeDir.removeRecursively();

            if (!writable) {
                N::Notification n;
                n.id          = QStringLiteral("library.unwritable");
                n.severity    = N::Error;
                n.kind        = N::Condition;
                n.title       = QObject::tr("Swings cannot be saved");
                n.detail      = QObject::tr("%1 is not writable.").arg(root);
                n.actionLabel = QObject::tr("Open Storage settings");
                n.actionId    = QStringLiteral("settings.storage");
                notificationCenter.post(n);
            }
        }

        // The folder allocation itself — the return value beginSessionFolder()
        // used to discard.  Same id the shot-time failure raises, so the two
        // coalesce into one story rather than two.
        if (!shotProcessor.sessionFolderReady()) {
            N::Notification n;
            n.id          = QStringLiteral("swing.save.no-directory");
            n.severity    = N::Error;
            n.kind        = N::Condition;
            n.title       = QObject::tr("Swings are not being saved");
            n.detail      = QObject::tr("The session folder could not be created under %1.")
                                .arg(root.isEmpty() ? QObject::tr("the library folder") : root);
            // ⚠ One fault, one sentence.  An unwritable root and a session
            // folder that could not be created are the same problem told twice,
            // and two near-identical errors on screen is the cascade §7 is
            // about.  Naming the cause means whichever was raised first is the
            // one the user reads.
            n.cause       = root.isEmpty() ? QStringLiteral("library.path-unset")
                                           : QStringLiteral("library.unwritable");
            n.actionLabel = QObject::tr("Open Storage settings");
            n.actionId    = QStringLiteral("settings.storage");
            notificationCenter.post(n);
        }

        // Headroom, from the reading the Storage panel already takes.
        const StorageInfo si = appSettings.queryStorageInfo();
        constexpr qint64 kLowSpaceBytes = 2LL * 1024 * 1024 * 1024;   // 2 GiB
        if (si.totalBytes > 0 && si.freeBytes < kLowSpaceBytes) {
            N::Notification n;
            n.id          = QStringLiteral("library.low-space");
            n.severity    = N::Warn;
            n.kind        = N::Condition;
            n.title       = QObject::tr("Low disk space");
            n.detail      = QObject::tr("%1 free on %2.")
                                .arg(QLocale().formattedDataSize(si.freeBytes), si.volumeName);
            n.actionLabel = QObject::tr("Open Storage settings");
            n.actionId    = QStringLiteral("settings.storage");
            notificationCenter.post(n);
        }
    });

    // IMU impact auto-detection (shot detection P1, re-pointed at the P3
    // arbiter): candidates funnel through reportCandidate's hold/fuse window
    // rather than committing directly. Gated behind the autoDetectSwing
    // setting. Whole chain is GUI-thread, so a direct connection; the
    // armed() gate inside reportCandidate handles capturing/busy/review.
    QObject::connect(&imuManager, &ImuManager::impactDetected, &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float conf) {
        if (appSettings.autoDetectSwing())
            shotController.reportCandidate(ShotController::Source::Imu,
                                           estImpactUs, conf);
    });
#ifdef HAVE_PPCP
    // Whether the IMU counts as a corroborating detector for a phone's Shot,
    // and it has to be BOTH switched on and actually attached.  `autoDetectSwing`
    // alone would mark it available on a machine with no IMU in the room, and
    // the rule would then refuse every phone shot for want of an instrument that
    // was never there.
    const auto applyImuAvailability = [&shotController, &imuManager, &appSettings] {
        shotController.setDetectorAvailable(
            ShotController::Source::Imu,
            appSettings.autoDetectSwing() && imuManager.imuConnected());
    };
    applyImuAvailability();
    QObject::connect(&imuManager, &ImuManager::instancesChanged,
                     &shotController, applyImuAvailability);
    QObject::connect(&appSettings, &AppSettings::autoDetectSwingChanged,
                     &shotController, applyImuAvailability);
    // ⛔ BALL-LAUNCH IS NEVER MARKED AVAILABLE, AND THAT IS DELIBERATE.  Its
    // candidates are corroboration-grade by construction — conf 0.6, below the
    // local arbiter's 0.8 lone-candidate floor — so it misses often and by
    // design.  Requiring it would refuse real shots; a detection from it still
    // counts when it fires, because `corroborated()` weighs any detector that
    // agreed, not only the ones listed as available.
#endif

    // Acoustic onset auto-detection (shot detection P2, re-pointed at the P3
    // arbiter like the IMU path). TranscriptionController::impactDetected is
    // emitted on the AUDIO thread with est_t* already computed; the
    // &shotController context makes this a queued hop onto the GUI thread.
    // Total back-date = residual device latency + mic-distance acoustic travel
    // (both physical; the old single 20 ms fudge over-corrected by 13-22 ms on
    // the truth corpus — see AppSettings::micDistanceM).
    const auto acousticBackdateUs = [&appSettings] {
        return appSettings.audioDeviceLatencyUs() + appSettings.micTravelUs();
    };
    controller.setAcousticLatencyUs(acousticBackdateUs());
    QObject::connect(&appSettings, &AppSettings::audioDeviceLatencyUsChanged,
                     &controller, [&controller, acousticBackdateUs] {
        controller.setAcousticLatencyUs(acousticBackdateUs());
    });
    QObject::connect(&appSettings, &AppSettings::micDistanceMChanged,
                     &controller, [&controller, acousticBackdateUs] {
        controller.setAcousticLatencyUs(acousticBackdateUs());
    });
    // Microphone selection + acoustic sensitivity — pushed at startup and kept
    // live. setInputDevice before the first capture so the saved device is used.
    controller.setInputDevice(appSettings.audioInputDevice());
    QObject::connect(&appSettings, &AppSettings::audioInputDeviceChanged,
                     &controller, [&controller, &appSettings] {
        controller.setInputDevice(appSettings.audioInputDevice());
    });
    // Acoustic "sensitivity" [0,1] sets the absolute amplitude gate — the
    // candidate-open threshold floor that keeps quiet ticks/ambient from firing
    // (and from masking real impacts). Mapped on a log scale across the useful
    // envelope range: s=0 → 0.30 (only loud events), s=1 → 0.01 (very sensitive),
    // s=0.5 → ~0.055. The meter draws this level so the user can sit it between
    // their ambient/keyboard noise and their club impacts.
    const auto applyAcousticSensitivity = [&controller, &appSettings] {
        const double s = appSettings.acousticSensitivity();   // already clamped [0,1]
        controller.setAcousticMinLevel(0.01 * std::pow(30.0, 1.0 - s));
    };
    applyAcousticSensitivity();
    QObject::connect(&appSettings, &AppSettings::acousticSensitivityChanged,
                     &controller, applyAcousticSensitivity);

    // Run the microphone (hence the acoustic detector + its calibration) while a
    // capturing session is live. Without this the mic only ran on the Audio page
    // / calibration view, so acoustic detection — and the calibrated gate — never
    // applied during real sessions. captureIntent is session-stable (doesn't
    // toggle per-shot). Gated by the acoustic enable + autoDetectSwing so the mic
    // stays off whenever acoustic can't contribute a candidate.
    const auto applyShotAudio = [&controller, &cameraManager, &appSettings, &shotController] {
        const bool acoustic = cameraManager.captureIntent()
                              && appSettings.acousticShotDetectionEnabled()
                              && appSettings.autoDetectSwing();
        controller.setShotDetectionActive(acoustic);
#ifdef HAVE_PPCP
        // The same predicate, told to the corroboration rule: a microphone that
        // is not listening is not evidence, and a Shot from a phone must not be
        // refused for failing to agree with a detector that was switched off.
        shotController.setDetectorAvailable(ShotController::Source::Acoustic, acoustic);
#endif
    };
    applyShotAudio();
    QObject::connect(&cameraManager, &CameraManager::captureIntentChanged,
                     &controller, applyShotAudio);
    QObject::connect(&appSettings, &AppSettings::acousticShotDetectionEnabledChanged,
                     &controller, applyShotAudio);
    QObject::connect(&appSettings, &AppSettings::autoDetectSwingChanged,
                     &controller, applyShotAudio);
    QObject::connect(&controller, &TranscriptionController::impactDetected,
                     &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float conf) {
        // Raw detector fires always (so calibration sees them); only feed the
        // arbiter when shot detection is on AND acoustic is enabled. Voice/STT
        // is independent and unaffected by acousticShotDetectionEnabled.
        if (appSettings.autoDetectSwing() && appSettings.acousticShotDetectionEnabled())
            shotController.reportCandidate(ShotController::Source::Acoustic,
                                           estImpactUs, conf);
    });
    // Vision corroboration (P3-G5): the v2 temporal ball detector's launch cliff
    // → the same funnel. CameraManager::ballLaunched already carries an absolute
    // impact time on the EventBuffer clock (stamped in CameraInstance). conf 0.6
    // is below the arbiter's 0.8 lone-candidate floor, so vision can only
    // corroborate IMU/acoustic, never commit a shot alone. (Precise frame
    // timestamps — vs today's age-from-now estimate — are a later refinement.)
    QObject::connect(&cameraManager, &CameraManager::ballLaunched, &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float conf) {
        if (appSettings.autoDetectSwing())
            shotController.reportCandidate(ShotController::Source::Ball,
                                           estImpactUs, conf);
    });

    // Voice input: completed STT transcription → coach chat (when voice input enabled).
    QObject::connect(&controller, &TranscriptionController::transcriptionReceived,
                     &llmController, [&llmController](const QString &text) {
        if (llmController.voiceInputEnabled())
            llmController.sendMessage(text);
    });

    // Voice output: completed LLM response → TTS (when voice output enabled).
    QObject::connect(&llmController, &LlmController::responseReady,
                     &ttsController, [&ttsController, &llmController](const QString &text) {
        if (llmController.voiceOutputEnabled())
            ttsController.speak(text);
    });

    // Entering/clearing an API key in Settings → Cloud Fallback re-evaluates each
    // subsystem's cloud-fallback availability and re-applies the user's preference
    // (so a force-cloud toggle that was waiting on a key engages immediately).
    QObject::connect(&secrets, &SecretsBridge::keysChanged,
                     &controller,    &TranscriptionController::refreshCloudAvailability);
    QObject::connect(&secrets, &SecretsBridge::keysChanged,
                     &ttsController,  &TtsController::refreshCloudAvailability);
    QObject::connect(&secrets, &SecretsBridge::keysChanged,
                     &llmController,  &LlmController::refreshCloudAvailability);

    QQmlApplicationEngine engine;
    // The Charts Posture and Club delivery pictures' stills (posture_chart_design.md §5.4; club_delivery_chart_design.md
    // stage 6): the report's renderer, cut at the picture's own crop, run off the GUI thread by each presenter. Returns
    // why for each it could not make.
    const auto chartsStillMaker = [](const QVariantList &stills) {
        QVector<StillRequest> rq;
        for (const QVariant &v : stills) rq << stillRequestOf(v.toMap());
        const QVector<StillResult> res = ReportStills::renderSwing(rq);
        QHash<QString, QString> errs;
        for (int i = 0; i < rq.size(); ++i)
            if (i >= res.size() || !res[i].ok)
                errs.insert(rq[i].outPath, i < res.size() && !res[i].error.isEmpty() ? res[i].error : QStringLiteral("the still was not made"));
        return errs;
    };
    pinpoint::charts::ChartsPosturePresenter::setStillMaker(chartsStillMaker);
    pinpoint::charts::ChartsDeliveryPresenter::setStillMaker(chartsStillMaker);
    // Every QML-facing swing payload (pose tiers, club/ball tracks, metric series) converts into
    // THIS engine once and is shared by reference from then on — see qml_payload.h for why a
    // QVariantMap property froze the studio for 7.8 s per shot. Must precede the first load().
    QmlPayload::setEngine(&engine);
    // Markup Lab frame source — engine takes ownership of the provider; the
    // controller keeps a non-owning pointer to push decoded frames into it.
    auto *markupProvider = new MarkupImageProvider();
    markupController.setImageProvider(markupProvider);
    markupController.setShotCommands(&appObject);   // the truth read and saved by the swing's Shot (3.4b-i, 5.7d)
    engine.addImageProvider(QStringLiteral("markup"), markupProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"),       &appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("libraryConverter"),  &libraryConverter);
    engine.rootContext()->setContextProperty(QStringLiteral("archiveController"), &archiveController);
    engine.rootContext()->setContextProperty(QStringLiteral("appInfo"),           &appInfo);
    engine.rootContext()->setContextProperty(QStringLiteral("secrets"),           &secrets);
    engine.rootContext()->setContextProperty(QStringLiteral("athleteController"), &athleteController);
    engine.rootContext()->setContextProperty(QStringLiteral("app"),               &appObject);
    // A Session's thin diagnostics adapter for a screen (refactor 3.7-a): diagnosticsAdapters.of(app.sessionAt(dir), surface).
    engine.rootContext()->setContextProperty(QStringLiteral("diagnosticsAdapters"), &diagnosticsAdapters);
    engine.rootContext()->setContextProperty(QStringLiteral("navController"),     &navController);
    engine.rootContext()->setContextProperty(QStringLiteral("imuManager"),       &imuManager);
    engine.rootContext()->setContextProperty(QStringLiteral("trunkCeremony"),    &trunkCeremony);
    engine.rootContext()->setContextProperty(QStringLiteral("launchMonitor"),   &launchMonitorController);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"),       &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("ttsController"),    &ttsController);
    engine.rootContext()->setContextProperty(QStringLiteral("llmController"),    &llmController);
    engine.rootContext()->setContextProperty(QStringLiteral("cameraManager"),    &cameraManager);
    engine.rootContext()->setContextProperty(QStringLiteral("bufferController"), &bufferController);
    engine.rootContext()->setContextProperty(QStringLiteral("resourceMonitor"),  &resourceMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("profiler"),         &profilerController);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionController"), &sessionController);
    engine.rootContext()->setContextProperty(QStringLiteral("notifications"),     &notificationCenter);
    engine.rootContext()->setContextProperty(QStringLiteral("updateController"),   &updateController);
    engine.rootContext()->setContextProperty(QStringLiteral("cudaRuntime"),        &cudaRuntime);
    engine.rootContext()->setContextProperty(QStringLiteral("appLog"),             &appLog);
    // (The PINPOINT_DEV_TRUNK_MOUNTS dev seam went with W2.2: the trunk mounts are offered because the
    // trunk calibrate step exists — ImuMounts.trunkMountsOffered.)

    // H3 — the PPCP session import engine. A context property and not a QML
    // type because libppcp is an OPTIONAL dependency (H0): a build without it
    // must still produce a working application. NOTHING IN THE UI CALLS THIS
    // YET, on purpose: the user does not import files — a connected capture
    // device offers its recorded sessions (MSG §9 session_offer/manifest) and
    // the host picks from that list. That UI is S3's (H4–H7); this controller
    // is the engine behind it. HAVE_PPCP means only "the library is linked".
#ifdef HAVE_PPCP
    static PpcpImportController ppcpImportController;
    engine.rootContext()->setContextProperty(QStringLiteral("ppcpImport"),
                                             &ppcpImportController);

    // H5 — the offer list, which IS the user-facing half of the above. A
    // connected capture device sends `session_offer` for each Session it holds
    // (MSG §9.1); each becomes a row in the DEVICES area of the home screen,
    // and accepting one sends `session_accept` with the digests our ledger
    // already holds (9.1a). The device then replays its stored frames onto the
    // live link and they arrive through the ordinary ingest path — the engine
    // above — so there is no importer and no second schema.
    //
    // ⚠ IT IS INSTALLED DETACHED, AND THAT IS THE HONEST STATE. The controller
    // needs the `ppcp_peer` of a connected device, and NOTHING IN THIS
    // APPLICATION CONSTRUCTS A PpcpHostPeer YET: H1's transport and H2's peer
    // exist and are tested, and no screen or service starts one. So the list
    // stays empty until that owner exists, and PpcpOfferList.qml hides itself
    // entirely when it is. An empty section with a heading would look like a
    // device that offered nothing, which is a different and untrue statement.
    static PpcpOfferController ppcpOfferController;
    engine.rootContext()->setContextProperty(QStringLiteral("ppcpOffers"),
                                             &ppcpOfferController);
#endif

#ifdef HAVE_PPCP_TRANSPORT
    // H-compose — THE OWNER, and the paragraph above is now out of date in one
    // respect: something DOES construct a PpcpHostPeer.  Everything about it
    // lives in PpcpHostService (its own translation unit, so ppcp-tests can
    // compile it — main.cpp cannot be syntax-checked there without reproducing
    // whisper, ONNX Runtime, OpenCV and Sparkle).  main.cpp's whole share is
    // construct, wire, expose.
    //
    // 7788 is RV §10's example port and a stable one matters: a persisted
    // pairing reconnects to an endpoint, and an ephemeral port would move under
    // it every launch.  Falling back to 0 rather than refusing to start, because
    // the pairing code carries whatever port we ended up with (4.3d).
    static PpcpHostService ppcpHost;
    ppcpHost.setOfferController(&ppcpOfferController);

    // ⭐ THE SESSION SCREEN DRIVES THE PHONES (2 Sept 2026).  Capture/Stop and
    // the wizard's start set CameraManager's capture intent; every connected
    // phone is armed or disarmed with it, so nobody taps Capture on a phone —
    // or on three of them.  A phone that connects mid-capture is armed at
    // `declare` by the service itself.
    QObject::connect(&cameraManager, &CameraManager::captureIntentChanged,
                     &ppcpHost, [&cameraManager] {
        ppcpHost.setCaptureWanted(cameraManager.captureIntent());
    });

    // ⛔ ONE LEDGER OVER ONE FILE.  The file-import path used to build its own
    // in-memory copy of the ledger the host service already has loaded, and
    // neither reloaded before saving — so an import during a live session had
    // its records overwritten by the next flush from here, and the same bundle
    // then re-imported as if it were new.  It borrows this one now.
    ppcpImportController.setSharedLedger(&ppcpHost.ledger());

    // ── CORE §8.4 / §8.5 — the swing-video leg ────────────────────────────
    //
    // ⭐ THE TWO JOINS THAT WERE MISSING, AND THEY LAND TOGETHER ON PURPOSE.
    // H-c asks the phone for the clip; H-d files the answer.  H-c without H-d
    // pulls video across the link and drops it on the floor, which is worse
    // than not asking — so if one of these connections is ever removed, remove
    // both.
    static PpcpClipFiler ppcpClipFiler;
    ppcpClipFiler.setLedger(&ppcpHost.ledger());
    ppcpClipFiler.setRecordingRepository(&recordingRepository);
    ppcpClipFiler.setShotCommands(&appObject);   // a clip's late part is its Shot's completeCapture (3.4b-i)
    // MSG 8.4a — the owed `capture_committed` goes out on the next tick, but a
    // clip that has just been flushed to disk should not wait 20 ms to be
    // acknowledged when the link is right here.
    ppcpClipFiler.setCommitPump([] { ppcpHost.flushOwedCommitsNow(); });

    // A shot was committed → ask every connected phone for its footage.
    // (`ppcpHost` and `ppcpClipFiler` are statics, so they are named directly
    // rather than captured — the same reason the aboutToQuit handler does.)
    QObject::connect(&shotController, &ShotController::captureRequested,
                     &ppcpHost, [](const QString &shotId, qint64 t0HostNs) {
        ppcpHost.requestCaptureForShot(shotId, t0HostNs);
    });
    QObject::connect(&ppcpHost, &PpcpHostService::captureAsked,
                     &ppcpClipFiler, &PpcpClipFiler::onCaptureAsked);

    // ⭐ MSG 8.5 (CR-03) — and the other answer: this host will NOT keep a Shot.
    // Every refusal that used to be silent on the wire — the corroboration rule,
    // a busy / review / stopped drop, a swing abandoned after analysis, a clip
    // nobody asked for — now tells the phone, which releases the clip under
    // 5.14g exit 5 instead of holding it for the life of the link.  The service
    // owes it durably when the link is gone (8.5i).  Beside `captureRequested`
    // on purpose: the two are the two answers to "what happened to that Shot".
    QObject::connect(&shotController, &ShotController::shotDeclined,
                     &ppcpHost, [](const QString &shotId, const QString &reason) {
        ppcpHost.declineShot(shotId, reason);
    });
    QObject::connect(&ppcpClipFiler, &PpcpClipFiler::shotDeclined,
                     &ppcpHost, [](const QString &shotId, const QString &peerId,
                                   const QString &sessionId, const QString &reason) {
        ppcpHost.declineShot(shotId, reason, peerId, sessionId);
    });

    // The swing folder exists (or never will) → file, or discard, what is owed.
    QObject::connect(&shotProcessor, &ShotProcessor::shotProcessed,
                     &ppcpClipFiler, [](int, const QString &swingDir) {
        ppcpClipFiler.onSwingReady(swingDir);
    });
    QObject::connect(&shotProcessor, &ShotProcessor::shotFailed,
                     &ppcpClipFiler, [&shotProcessor](const QString &) {
        // A shot can fail analysis and still have a folder — the clip belongs in
        // it either way.  Only a shot with NO folder has nowhere to put one.
        const QString dir = shotProcessor.lastSwingDir();
        if (dir.isEmpty()) ppcpClipFiler.onSwingFailed();
        else               ppcpClipFiler.onSwingReady(dir);
    });

    // ⚠ AND THE CLIPS THEMSELVES, CONNECTED HERE AND NOT INSIDE PpcpHostService.
    // Binding `&VideoInputPpcp::clipReady` puts a moc-generated symbol into
    // whatever translation unit does it, and ppcp_host_service_test STUBS the
    // src/Video symbols so a suite about the pairing clock does not drag in
    // Aravis, Spinnaker and Bluetooth.  main.cpp links the real class already.
    //
    // Re-run whenever the consumer set changes: a phone connecting, or
    // reconnecting, builds new instances, and UniqueConnection makes re-running
    // over the survivors free.
    const auto bindClipConsumers = [] {
        for (VideoInputPpcp *v : ppcpHost.previewConsumers())
            QObject::connect(v, &VideoInputPpcp::clipReady,
                             &ppcpClipFiler, &PpcpClipFiler::onClipReady,
                             Qt::UniqueConnection);
    };
    QObject::connect(&ppcpHost, &PpcpHostService::previewConsumersChanged,
                     &ppcpClipFiler, bindClipConsumers);
    bindClipConsumers();

    // ⭐ Publish the clip chain so ppcpStats() — and therefore an automated
    // probe — can read the whole leg from one object. Cheap, on the same 20 ms
    // tick that already drives the service.
    ppcpHost.setClipChainStatsProvider([] {
        const PpcpClipFiler::Stats &f = ppcpClipFiler.stats();
        QVariantMap m;
        m[QStringLiteral("captureRequests")] = int(f.asked);
        m[QStringLiteral("clipsAnnounced")]  = int(f.arrived);
        m[QStringLiteral("clipsConverted")]  = int(f.arrived);
        m[QStringLiteral("clipsFiled")]      = int(f.filed);
        m[QStringLiteral("clipsAbsent")]     = int(f.absent);
        m[QStringLiteral("clipsParked")]     = int(f.parked);
        m[QStringLiteral("clipsOrphaned")]   = int(f.orphaned);
        m[QStringLiteral("clipsDuplicate")]  = int(f.duplicate);
        m[QStringLiteral("clipsFailed")]     = int(f.failed);
        return m;
    });

    // A landed clip changes the swing on disk. Re-analysis is the existing
    // "this swing changed, analyse it again" path; the row updates in place and
    // the stage is never stolen from a golfer still hitting.
    QObject::connect(&ppcpClipFiler, &PpcpClipFiler::clipFiled,
                     &reanalysisController, [&reanalysisController](const QString &swingDir,
                                                                    const QString &) {
        reanalysisController.reanalyse(QVariantList{ swingDir });
    });
    // MSG 3.3 — a peer's cameras exist the moment it declares.  The registry
    // has already been told by then; CameraManager snapshots at construction
    // and merges only on enumerate(), so it is the one that has to be asked.
    QObject::connect(&ppcpHost, &PpcpHostService::sourcesChanged,
                     &cameraManager, &CameraManager::enumerate);
    // Settings -> Cameras' ROI preview is the one path wired to a live PPCP
    // peer so far (CameraInstance::ppcpAttachIfNeeded()) — real capture stays
    // exactly as unable to start a PPCP camera as it already was.
    cameraManager.setPpcpHostService(&ppcpHost);
    // A HEADLESS PROBE (PINPOINT_HEADLESS_PROBE=1, refactor 4-c) opens no phone link: no listener, no advertising, no
    // discovery, so no phone camera or microphone. The host stays, stopped, for the QML that reads it.
    if (pinpoint::headlessProbe())
        ppInfo() << "[HeadlessProbe] not started: phone link (the PPCP host's listener, discovery and advertising)";
    else if (!ppcpHost.start(7788))
        ppcpHost.start(0);
    // A paired phone is a device, so it goes in the one device list beside the
    // cameras and the IMUs.  Handed over as a plain QObject: the monitor reads
    // its `phones` property and knows nothing about PPCP (see setPhoneSource).
    resourceMonitor.setPhoneSource(&ppcpHost);
    // And a launch monitor that has dialled in is a device too — see
    // setLaunchMonitorSource. Only the GSPro link enumerates; the GCQuad is a
    // watched folder and has nothing to connect.
    resourceMonitor.setLaunchMonitorSource(&launchMonitorController);

    // ── CORE §8.2 — the shot pipeline joins the arbiter here ────────────────
    //
    // ⚠ AS SIGNALS AND NOT AS STORED POINTERS INTO main()'s STACK.
    // `ppcpHost` is a function-local static and outlives `shotController`,
    // which dies when main() returns; a callback held inside the service would
    // dangle between then and exit().  A QObject connection is severed when
    // either end goes, which is the whole reason the seam is shaped this way.
    //
    // The bridge pointer is re-read on every change rather than held: it
    // belongs to a phone, and `dropPhone()` destroys it.
    // (`ppcpHost` is a static, so it is named directly rather than captured —
    // the same reason the aboutToQuit lambda below names it.)
    const auto applyPpcpBridge = [&shotController] {
        shotController.setPpcpBridge(ppcpHost.activeShotBridge());
        // I26 / 5.12a — a Candidate names a Source THIS host declared.  The
        // microphone is the only nominator this host has: no motion Source and
        // no vision Source is declared yet, so IMU and ball candidates are not
        // nominated at all.  They still corroborate (see setDetectorAvailable),
        // but they do not reach the wire, so an issued Shot will not reference
        // them (8.2f).  Declaring a motion Source is the proper fix and is
        // deliberately a separate change.
        shotController.setPpcpSourceIds(ppcpHost.hostMicrophoneSourceId(),
                                        /*motion=*/QString(), /*vision=*/QString());
    };
    applyPpcpBridge();
    QObject::connect(&ppcpHost, &PpcpHostService::shotBridgeChanged,
                     &shotController, applyPpcpBridge);
    // 8.2h — the arbiter issued a Shot.  Subject to the corroboration rule
    // inside commitArbitratedShot(), this is where a phone's shot becomes a
    // swing.
    QObject::connect(&ppcpHost, &PpcpHostService::arbitratedShot, &shotController,
                     [&shotController](qint64 t0HostNs, const QString &shotId) {
        shotController.commitArbitratedShot(t0HostNs, shotId);
    });

    engine.rootContext()->setContextProperty(QStringLiteral("ppcpHost"), &ppcpHost);
#endif
    engine.rootContext()->setContextProperty(QStringLiteral("motionCaptureProbe"), &motionCaptureProbe);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionReviewController"), &sessionReviewController);
    engine.rootContext()->setContextProperty(QStringLiteral("workOns"),           &workOns);
    // These four live on main()'s stack. Mark them C++-owned explicitly: the QML engine otherwise came to
    // treat the report model as its own and its garbage collector deleteLater()'d it a few seconds into a
    // windowed launch ("pointer being freed was not allocated" in ~SessionReportModel, 21 Oct 2026).
    for (QObject *o : { static_cast<QObject *>(&studioProfile), static_cast<QObject *>(&sessionReport),
                        static_cast<QObject *>(&sessionReportList), static_cast<QObject *>(&reportMailer) })
        QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    engine.rootContext()->setContextProperty(QStringLiteral("studioProfile"),     &studioProfile);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionReport"),     &sessionReport);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionReportList"), &sessionReportList);
    engine.rootContext()->setContextProperty(QStringLiteral("reportMailer"),      &reportMailer);
    engine.rootContext()->setContextProperty(QStringLiteral("shotController"),    &shotController);
    engine.rootContext()->setContextProperty(QStringLiteral("shotProcessor"),     &shotProcessor);
    engine.rootContext()->setContextProperty(QStringLiteral("shotModel"),         &shotModel);
    engine.rootContext()->setContextProperty(QStringLiteral("reanalysisController"), &reanalysisController);
    engine.rootContext()->setContextProperty(QStringLiteral("swingExporter"),     &swingZipExporter);
    engine.rootContext()->setContextProperty(QStringLiteral("liveWrist"),         &liveWrist);
    engine.rootContext()->setContextProperty(QStringLiteral("markupController"),  &markupController);
    engine.rootContext()->setContextProperty(QStringLiteral("clipboard"),         &clipboardHelper);

    // Clean merger shutdown before Qt tears down its event loop. Shut the platform
    // updater (WinSparkle on Windows) down first so its helper threads stop cleanly.
    //
    // ⚠ AND THE PPCP HOST, FOR A SHARPER REASON THAN TIDINESS.  `ppcpHost` is a
    // function-local static, so its destructor does NOT run when main() returns
    // — it runs later, from `exit()`, by which time `app` (an ordinary local,
    // above) has already been destroyed.  ~PpcpHostService calls stop(), and
    // stop() joins the accept thread, tears down a QSocketNotifier and takes
    // `VideoInputPpcp`'s `ppcpLiveMutex()` on the way through `dropLink()`.
    // That last one is a FUNCTION-LOCAL STATIC mutex, first constructed when
    // the first PPCP camera appears — which is AFTER this static — so it is
    // destroyed BEFORE it, and locking it at that point returns EINVAL.
    // `std::mutex::lock()` turns EINVAL into a `std::system_error`, a
    // destructor is implicitly `noexcept`, and the process therefore
    // TERMINATES on the way out: "mutex lock failed: Invalid argument",
    // reported as a crash on exit (observed 23 Aug, PinPointStudio-…-184129).
    //
    // Stopping here fixes it at the root: the accept thread is joined and the
    // link is dropped while Qt, the event loop and every static this touches
    // are still alive.  The destructor's own stop() then finds nothing to do —
    // no link, no live code, no thread — and takes none of those paths.
    // (`ppcpHost` is a static, so it is named directly rather than captured.)
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&eventBuffer, &updateController]() {
#ifdef HAVE_PPCP_TRANSPORT
        // ⚠ AND DROP THE BORROWED LEDGER, FOR THE SAME REASON THE STOP IS HERE.
        // `ppcpImportController` is a function-local static constructed BEFORE
        // `ppcpHost`, so it is destroyed AFTER it: a reference to the host's
        // ledger held past this point outlives its owner.  Dropped while both
        // are still alive.
        ppcpImportController.setSharedLedger(nullptr);
        ppcpHost.stop();
#endif
        updateController.shutdownUpdater();
        eventBuffer.stop();
    });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("PinPointStudio", "Main");

    // PINPOINT_PROBE_REANALYSE=<swing dir>: re-analyse that swing 3 s after launch through the
    // carousel's funnel, exactly as the action bar does — the in-app crash of 7 Oct 2026
    // reproduced under lldb without a click.
    if (qEnvironmentVariableIsSet("PINPOINT_PROBE_REANALYSE")) {
        const QString dir = qEnvironmentVariable("PINPOINT_PROBE_REANALYSE");
        QTimer::singleShot(3000, &reanalysisController, [&reanalysisController, dir] {
            ppInfo() << "[Probe] re-analysing" << dir;
            reanalysisController.reanalyse(QVariantList{ dir });
        });
    }

    // PINPOINT_PROBE_CAMERA_ROLES=<report file>: move every impact-capable camera through
    // Impact → DownTheLine → Impact and back, through the calls Settings → Cameras makes, read
    // back what each camera holds after every move, write the report and quit
    // (camera_role_probe.h, flir_camera_settings.md §7). PINPOINT_PROBE_CAMERA_SERIAL limits it
    // to one camera. The operator's camera settings are restored.
    if (qEnvironmentVariableIsSet("PINPOINT_PROBE_CAMERA_ROLES")) {
        auto *roleProbe = new CameraRoleProbe(&cameraManager, &appSettings,
                                              qEnvironmentVariable("PINPOINT_PROBE_CAMERA_ROLES"),
                                              qEnvironmentVariable("PINPOINT_PROBE_CAMERA_SERIAL"), &app);
        QTimer::singleShot(3000, roleProbe, [roleProbe] { roleProbe->start(); });
    }

    // Warm the pose session off the main thread once the UI is up, so the first shot does not
    // pay the model load: on the Mac the CoreML MLProgram costs 20 s from its compiled cache and
    // 97 s the first time; on CUDA 0.3 s. The cached session is the one PoseRunner::run() keys for
    // the current quality tier (pose_inference_performance_plan.md step 1). Its OWN detached
    // thread, not the global pool: ~QCoreApplication waits for the pool, and a quit during the
    // load must not wait for it (#20) — AbandonPoseWarmUpAtExit above. Nothing joins it, and the
    // cache is never torn down (pose_runner.cpp). The load itself runs on the pose inference
    // thread, this thread waiting for it (J-43). Nothing on the GUI thread waits for it either:
    // the first run() that needs the session takes it from the cache, waiting there if the
    // warm-up is still building it.
    // A HEADLESS PROBE (refactor 4-c) loads no pose model at launch: the warm-up is the launch's one heavy load (a cold
    // CoreML compile is ~97 s and gigabytes), and a probe's scratch home has no compiled cache. A re-analysis the probe
    // asks for still loads it on first use.
    if (pinpoint::headlessProbe())
        ppInfo() << "[HeadlessProbe] not started: the pose model's launch warm-up";
    else
        QTimer::singleShot(1500, &app, [&appSettings]() {
            const QString quality = appSettings.motionCaptureQuality();
            g_poseWarmUpInFlight.store(true);
            std::thread([quality]() {
                {
                    pinpoint::osmetrics::ThreadScope _tscope("Pose.WarmUp");
                    PoseRunner::warmUp(quality);
                }
                g_poseWarmUpInFlight.store(false);
            }).detach();
        });

    exitCode = QCoreApplication::exec();
    archiveController.stopJobs();         // its housekeeping pass reaches the Athletes: stopped before they go (step 3.8)
    recordingRepository.setOwnerOf({});   // the controller it asks is a local of main (J-39 resolver)
    SessionDiagnosticsModel::setShotCommands(nullptr);   // `app` is a local of main (step 3.4b-i)
    diagLookup = {};                                     // a result still queued finds no owner (the store goes first)
    ppcpClipFiler.setShotCommands(nullptr);              // a static: it outlives `app`
    pinpoint::charts::setNeighboursDoor({});             // a static: it outlives the athlete store (refactor 5.5d)
    return exitCode;
}
