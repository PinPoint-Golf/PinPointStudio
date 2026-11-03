/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "launch_monitor_controller.h"

#include "app_settings.h"
#include "athlete_controller.h"
#include "camera_manager.h"
#include "launch_monitor_factory.h"
#include "pp_debug.h"
#include "session_controller.h"
#include "standalone_gate.h"
#include "shot_list_model.h"
#include "../../Export/swing_doc.h"

#include "gspro_monitor.h"
#include "premier_club.h"
#include "premier_monitor.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>

using namespace pinpoint::lm;

LaunchMonitorController::LaunchMonitorController(AppSettings *settings, ShotListModel *shotModel,
                                                 AthleteController *athleteController,
                                                 SessionController *sessionController,
                                                 CameraManager *cameraManager,
                                                 QObject *parent)
    : QObject(parent), m_settings(settings), m_shotModel(shotModel),
      m_athletes(athleteController), m_session(sessionController), m_cameras(cameraManager)
{
    reconfigure();

    if (m_settings) {
        connect(m_settings, &AppSettings::launchMonitorEnabledChanged,
                this, &LaunchMonitorController::reconfigure);
        connect(m_settings, &AppSettings::launchMonitorKindChanged,
                this, &LaunchMonitorController::reconfigure);
        connect(m_settings, &AppSettings::launchMonitorPathChanged,
                this, &LaunchMonitorController::reconfigure);
        connect(m_settings, &AppSettings::launchMonitorPremierPathChanged,
                this, &LaunchMonitorController::reconfigure);
        connect(m_settings, &AppSettings::launchMonitorPollMsChanged, this, [this]() {
            if (m_monitor) m_monitor->setPollIntervalMs(m_settings->launchMonitorPollMs());
        });
        // The GSPro link's address. Both go through reconfigure() rather than a
        // setSourcePath() poke, because changing either has to rebind the listener
        // and the connector's own start/stop is what knows how.
        connect(m_settings, &AppSettings::gsProPortChanged,
                this, &LaunchMonitorController::reconfigure);
        connect(m_settings, &AppSettings::gsProInterfaceChanged,
                this, &LaunchMonitorController::reconfigure);
    }

    // End Session: Premier's carry lives in a file of its own (issue #148), looked up as each
    // reading goes final. One last sweep catches any it missed; what it finds goes through
    // onReadingUpdated like late club data.
    if (m_session) {
        connect(m_session, &SessionController::activeSessionTypeChanged, this, [this]() {
            if (m_session->activeSessionType() != static_cast<int>(SessionController::Type::None))
                return;
            if (auto *pm = qobject_cast<ForesightPremierMonitor *>(m_monitor))
                pm->sweepCarry();
        });
    }
}

LaunchMonitorController::~LaunchMonitorController()
{
    if (m_monitor)
        m_monitor->stop();
}

void LaunchMonitorController::reconfigure()
{
    // ⚠ DORMANT IS BUILT OUT OF THE PARTS THAT ALREADY EXIST, not out of a fourth
    // state. "Switched off" and "nothing configured" behave identically — no
    // listener, no dot, no chime, no port held — so disabling resolves to
    // Kind::None and the existing InertLaunchMonitor does the rest. The SETTINGS
    // are untouched, which is the whole point: the port, the interface and the
    // GCQuad's folder are all still there when it comes back on.
    //
    // ⚠ AND IT RELEASES PORT 921. That is why this connector needs the switch more
    // than the GCQuad does: a user who wants GSPro itself to run has to hand the
    // port back, and reconfiguring the connector to do it would lose the setup.
    const bool enabled = !m_settings || m_settings->launchMonitorEnabled();
    const Kind kind = (m_settings && enabled) ? kindFromKey(m_settings->launchMonitorKind())
                                              : Kind::None;

    // Rebuild only when the kind actually changes; a path edit must not tear down and
    // recreate the connector, because that would re-prime the watermark and could lose
    // a reading in flight.
    if (!m_monitor || m_monitor->kind() != kind) {
        if (m_monitor) {
            m_monitor->stop();
            m_monitor->deleteLater();
        }
        m_monitor = makeLaunchMonitor(kind, this);
        connect(m_monitor, &LaunchMonitorBase::readingAvailable,
                this, &LaunchMonitorController::onReadingAvailable);
        connect(m_monitor, &LaunchMonitorBase::stateChanged,
                this, &LaunchMonitorController::stateChanged);
        // ⚠ Only one connector enumerates, so only one has this signal. The cast
        // is what keeps the base class out of it: a folder does not connect, and
        // giving LaunchMonitorBase a virtual for a question one connector can
        // answer would put an empty implementation in every future one.
        // The same reasoning for the one connector whose readings arrive in two halves:
        // Premier writes a shot's club data with the NEXT shot (premier_monitor.h).
        if (auto *pm = qobject_cast<ForesightPremierMonitor *>(m_monitor))
            connect(pm, &ForesightPremierMonitor::readingUpdated,
                    this, &LaunchMonitorController::onReadingUpdated);
        // A new connector's shot ids mean nothing to the old one's folders.
        m_shotDirs.clear();
        m_shotDirOrder.clear();
        m_heldUpdates.clear();
        if (auto *gs = qobject_cast<GsProMonitor *>(m_monitor)) {
            connect(gs, &GsProMonitor::clientsChanged,
                    this, &LaunchMonitorController::devicesChanged);
            // ⚠ AND the status line, because stateLabel() reads deviceCount(): the
            // moment a device connects the label stops saying "no launch monitor
            // yet", and that happens before the library changes state.
            connect(gs, &GsProMonitor::clientsChanged,
                    this, &LaunchMonitorController::stateChanged);
        }
    }

    if (m_settings) {
        m_monitor->setPollIntervalMs(m_settings->launchMonitorPollMs());
        // ⚠ THE SOURCE IS A FOLDER FOR ONE CONNECTOR AND AN ADDRESS FOR THE OTHER,
        // and the base class anticipated exactly that ("either a watched path or a
        // polled endpoint"). Composed here rather than stored composed, so the two
        // settings stay independently editable and a port change cannot corrupt a
        // path.
        if (kind == Kind::GsPro) {
            m_monitor->setSourcePath(gsProSourcePath(m_settings->gsProPort(),
                                                     m_settings->gsProInterface()));
        } else if (kind == Kind::Premier) {
            // Its own folder setting, so choosing Premier never overwrites FSX2020's. Empty
            // means Premier's own data folder for this account, which is nearly always right.
            const QString p = m_settings->launchMonitorPremierPath();
            m_monitor->setSourcePath(p.isEmpty() ? ForesightPremierMonitor::defaultFolder() : p);
        } else {
            m_monitor->setSourcePath(m_settings->launchMonitorPath());
        }
    }
    m_monitor->start();
    // What PinPoint already knows, told again: a connector rebuilt by a settings
    // change must not wait for the next club selection to learn the club.
    if (!m_club.isEmpty() || m_sessionActive) {
        setPlayerClub(m_club, m_leftHanded);
        setSessionActive(m_sessionActive);
    }
    emit stateChanged();
    emit devicesChanged();
}

QVariantList LaunchMonitorController::devices() const
{
    QVariantList out;
    auto *gs = qobject_cast<GsProMonitor *>(m_monitor);
    if (!gs)
        return out;                 // a folder does not enumerate

    for (const auto &c : gs->clients()) {
        QVariantMap m;
        m[QStringLiteral("deviceId")]   = c.deviceId;
        m[QStringLiteral("peer")]       = c.peer;
        m[QStringLiteral("shots")]      = c.shots;
        m[QStringLiteral("messages")]   = c.messages;
        m[QStringLiteral("identified")] = c.identified;
        m[QStringLiteral("connectedAtMs")]   = c.connectedAtMs;
        m[QStringLiteral("lastMessageAtMs")] = c.lastMessageAtMs;
        // ⚠ WHAT TO SHOW, decided here rather than in QML. A client is an
        // anonymous socket until its first message — the protocol has no
        // handshake and a device may say nothing for minutes — and "connecting…"
        // is the honest label for that, not a blank row.
        const QString label = c.identified && !c.deviceId.isEmpty()
                                  ? c.deviceId
                                  : tr("connecting…");
        m[QStringLiteral("label")] = label;

        // ── The same row, in the resource monitor's vocabulary ──────────────
        // ⚠ ONE MAP WITH BOTH SETS OF KEYS, not two models. A paired phone does
        // exactly this — PpcpHostService::phones() emits rows the monitor
        // appends VERBATIM — and the alternative is two lists of the same thing
        // that can disagree about how many launch monitors are connected.
        m[QStringLiteral("kind")]       = QStringLiteral("LaunchMonitor");
        m[QStringLiteral("name")]       = label;
        m[QStringLiteral("identifier")] = c.peer;
        // "connected" is what the home screen's dot reads for green. An
        // unidentified client is on the wire but has said nothing, so it is not
        // green yet — the same distinction the settings panel draws in amber.
        m[QStringLiteral("status")]     = c.identified ? QStringLiteral("connected")
                                                       : QStringLiteral("connecting");
        // ⚠ A LAUNCH MONITOR HAS NO RATE, and the phone row already carries the
        // scar from claiming one: "0 Hz" is a number nobody asked for. Zero here,
        // and the row's right-hand cell shows the shot count instead.
        m[QStringLiteral("dataRateHz")] = 0.0;
        m[QStringLiteral("hasWarning")] = false;
        m[QStringLiteral("model")]      = deviceName();
        m[QStringLiteral("backend")]    = QStringLiteral("GSPro Open Connect");
        out.append(m);
    }
    return out;
}

bool LaunchMonitorController::enumerates() const
{
    return qobject_cast<GsProMonitor *>(m_monitor) != nullptr;
}

void LaunchMonitorController::setPlayerClub(const QString &club, bool leftHanded)
{
    m_club       = club;
    m_leftHanded = leftHanded;
    // A no-op on a connector that cannot send, which is why the caller does not
    // have to ask which one is configured.
    if (auto *gs = qobject_cast<GsProMonitor *>(m_monitor))
        gs->setPlayerClub(club, leftHanded);
}

void LaunchMonitorController::setSessionActive(bool active)
{
    m_sessionActive = active;
    if (auto *gs = qobject_cast<GsProMonitor *>(m_monitor))
        gs->setSessionActive(active);
}

bool LaunchMonitorController::configured() const
{
    return m_monitor && m_monitor->kind() != Kind::None;
}

QString LaunchMonitorController::stateName() const
{
    if (!m_monitor)
        return QStringLiteral("disabled");
    switch (m_monitor->state()) {
    case State::Disabled: return QStringLiteral("disabled");
    case State::Waiting:  return QStringLiteral("waiting");
    case State::Ready:    return QStringLiteral("ready");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("disabled");
}

QString LaunchMonitorController::stateLabel() const
{
    // ⚠ "SWITCHED OFF" AND "NOT CONFIGURED" ARE THE SAME STATE AND DIFFERENT
    // FACTS. Dormant is deliberately built out of Kind::None (see reconfigure),
    // which is right for behaviour and wrong for the label: a user who has just
    // set a port and flicked the switch off must not be told their connector does
    // not exist. The settings know which it is; the connector cannot.
    const bool chosen = m_settings
                        && kindFromKey(m_settings->launchMonitorKind()) != Kind::None;
    const bool off    = chosen && m_settings && !m_settings->launchMonitorEnabled();

    if (!m_monitor)
        return off ? tr("Switched off") : tr("Not configured");
    switch (m_monitor->state()) {
    case State::Disabled: return off ? tr("Switched off") : tr("Not configured");
    case State::Waiting:  return waitingLabel();
    case State::Ready:    return tr("Connected");
    case State::Error:
        // ⚠ A LISTENER HAS NO FOLDER. This string was written when there was one
        // connector and it read a file; for the GSPro link the error is a bind
        // failure, and telling somebody to check a folder would send them looking
        // for something that does not exist.
        if (m_monitor->kind() == Kind::GsPro)
            return tr("Cannot listen");
        // Premier updates itself, and its files are internal, not an export: a changed
        // format is the error to expect, and "check the folder" would be the wrong advice.
        if (m_monitor->kind() == Kind::Premier
            && m_monitor->errorText().contains(QLatin1String("format")))
            return tr("Premier's files have changed");
        return tr("Cannot read the folder");
    }
    return tr("Not configured");
}

QString LaunchMonitorController::waitingLabel() const
{
    // The GCQuad waits for a SHOT — it is watching a folder that is already there.
    // The GSPro link waits for a DEVICE first, and only then for a shot, and the
    // difference is the whole of what a user needs to know while nothing happens.
    if (m_monitor && m_monitor->kind() == Kind::GsPro)
        return deviceCount() > 0 ? tr("Waiting for a shot")
                                 : tr("Listening — no launch monitor yet");
    // Premier writes shots only while a RANGE session is open — never in a training drill —
    // so "waiting" there means "no range session", and saying so is what stops it looking
    // broken in a drill.
    if (m_monitor && m_monitor->kind() == Kind::Premier)
        return tr("Waiting for a range session — training drills send no shots");
    return tr("Waiting for a shot");
}

QString LaunchMonitorController::errorText() const
{
    return m_monitor ? m_monitor->errorText() : QString();
}

QString LaunchMonitorController::sourceText() const
{
    return m_monitor ? m_monitor->sourceDescription() : QString();
}

QString LaunchMonitorController::premierDefaultFolder() const
{
    return ForesightPremierMonitor::defaultFolder();
}

QString LaunchMonitorController::deviceName() const
{
    return m_monitor ? kindShortLabel(m_monitor->kind()) : QString();
}

void LaunchMonitorController::onShotDetected(qint64 impactUtcMs)
{
    m_destinationHasDoc = true;   // assume the pipeline will produce one, until it says otherwise
    m_destinationShotId = -1;
    m_pairing.noteShotDetected(impactUtcMs > 0 ? std::optional<qint64>(impactUtcMs) : std::nullopt);
}

void LaunchMonitorController::onShotProcessed(int shotId, const QString &swingDir)
{
    Q_UNUSED(shotId)
    if (swingDir.isEmpty())
        return;
    m_destinationHasDoc = true;
    if (const auto parked = m_pairing.noteSwingDir(swingDir))
        applyToSwing(swingDir, *parked);
}

void LaunchMonitorController::onShotFailed(const QString &swingDir, int shotId)
{
    // NOT the end of the shot. The processor allocates the swing folder BEFORE it runs
    // analysis or export, so a shot with no cameras and no IMUs — the whole point of
    // owning a launch monitor without them — reaches here with a real, empty folder and
    // a reading that describes what happened. Filling it is the honest outcome; leaving
    // it empty and saying "analysis failed" describes the pipeline rather than the shot.
    //
    // No setting gates this. The app DETECTED a shot and the monitor MEASURED it; there
    // is nothing to be cautious about, which is what separates it from a standalone shot
    // arriving with nothing else going on at all.
    if (swingDir.isEmpty()) {
        if (m_pairing.hasParked())
            ppWarn() << "LaunchMonitor: shot failed with no folder allocated; reading discarded";
        m_pairing.noteShotFailed();
        return;
    }

    m_destinationHasDoc = false;
    m_destinationShotId = shotId;
    if (const auto parked = m_pairing.noteSwingDir(swingDir))
        writeDeviceOnly(swingDir, *parked, QString(), 0, QString(), m_destinationShotId);
}

void LaunchMonitorController::onReadingAvailable(const LaunchMonitorReading &reading)
{
    // First: the club, so whatever the reading is attributed to below is recorded with it.
    followDeviceClub(reading);

    const ShotPairing::Offer offer = m_pairing.offerReading(reading);
    // Every timed decision is logged with its offset: the window is a starting value, and
    // these lines are what size it (docs/design/foresight_premier_connector.md §4.3).
    if (offer.contactDeltaMs) {
        const char *what = offer.timeCheck == ShotPairing::TimeCheck::Matched      ? "matches our impact"
                         : offer.timeCheck == ShotPairing::TimeCheck::BeforeImpact ? "is from before our impact — an earlier shot we did not see; still waiting"
                                                                                    : "is too late for our impact — the device dropped our shot; this swing gets no reading";
        ppInfo() << "LaunchMonitor: reading" << reading.deviceShotId << "contact Δ"
                 << *offer.contactDeltaMs << "ms (window ±" << ShotPairing::kContactWindowMs
                 << "ms)" << what;
    }
    switch (offer.disposition) {
    case ShotPairing::Disposition::Discarded:
        // Nothing was waiting for it. Normally that is the end of the matter — a ball
        // struck while we were not capturing, or a second write with no shot between.
        // With standalone shots switched on it becomes a shot of its own instead.
        if (createStandaloneShot(reading))
            return;
        ppInfo() << "LaunchMonitor: reading" << reading.deviceShotId
                 << "arrived with no shot waiting; discarded";
        return;
    case ShotPairing::Disposition::Parked:
        // The usual case — analysis is still running, so there is nowhere to put it
        // yet. onShotProcessed() will flush it.
        return;
    case ShotPairing::Disposition::Claimed:
        // The destination is known. Whether it already holds a document decides which
        // way the reading goes in — the shot may have failed before this arrived.
        if (m_destinationHasDoc)
            applyToSwing(offer.swingDir, reading);
        else
            writeDeviceOnly(offer.swingDir, reading, QString(), 0, QString(), m_destinationShotId);
        return;
    }
}

bool LaunchMonitorController::writeDeviceOnly(const QString &swingDir,
                                              const LaunchMonitorReading &r,
                                              const QString &swingId, int swingIndex,
                                              const QString &sessionId, int existingShotId)
{
    pinpoint::SwingDocWriter::DeviceOnlyMeta meta;
    // The rescue path is handed a folder somebody else allocated, so it reads the
    // identity back off the path rather than being told: <session>/<swing_NNNN>.
    const QDir dir(swingDir);
    meta.swingId    = swingId.isEmpty() ? dir.dirName() : swingId;
    meta.sessionId  = sessionId.isEmpty() ? QDir(dir).dirName() : sessionId;
    if (sessionId.isEmpty()) {
        QDir parent(swingDir);
        if (parent.cdUp()) meta.sessionId = parent.dirName();
    }
    meta.swingIndex = swingIndex;
    if (swingIndex == 0) {
        // "swing_0007" → 7. Falls back to 0, which is only an ordering hint.
        const QString digits = meta.swingId.section(QLatin1Char('_'), -1);
        meta.swingIndex = digits.toInt();
    }
    if (m_athletes) {
        meta.athleteName = m_athletes->currentName();
        meta.athleteUuid = m_athletes->currentUuid();
    }
    if (m_session) {
        meta.club        = m_session->activeClub();
        meta.sessionType = m_session->activeSessionType();
    }
    meta.wallclockMs = r.readAtMs > 0 ? r.readAtMs : QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!pinpoint::SwingDocWriter::writeDeviceOnlySwing(swingDir, r, meta, &error)) {
        ppWarn() << "LaunchMonitor: cannot write a device-only shot to" << swingDir << ":" << error;
        return false;
    }

    if (m_shotModel) {
        if (existingShotId >= 0) {
            // The processor already made a row for this shot — an in-memory one with no
            // folder, because nothing was written. Point it at the folder we just filled.
            m_shotModel->attachSwingDir(existingShotId, swingDir);
        } else {
            const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(swingDir);
            m_shotModel->addShot(swingDir, ps.timestampLabel, ps.club,
                                 /*hasVideo*/ false, QUrl(), /*tracePoints*/ {},
                                 /*score*/ 0, ps.metrics, ps.analysisDetail);
        }
    }

    ppInfo() << "LaunchMonitor: device-only shot" << r.deviceShotId << "written to" << swingDir;
    rememberShotDir(r.deviceShotId, swingDir);

    m_lastReading = summaryOf(r) + QStringLiteral(" · ") + tr("monitor only");
    emit lastReadingChanged();

    emit deviceOnlyShotSaved(swingDir);
    emit readingApplied(swingDir);
    return true;
}

bool LaunchMonitorController::createStandaloneShot(const LaunchMonitorReading &reading)
{
    // The decision is decideStandalone()'s, not this function's. Everything here is
    // fact-GATHERING — see standalone_gate.h for why the two are separated.
    StandaloneFacts facts;
    facts.connectorConfigured = configured();
    facts.standaloneEnabled   = m_settings && m_settings->launchMonitorStandalone();
    facts.libraryConfigured   = m_settings && !m_settings->athleteLibraryPath().isEmpty();
    facts.athleteSelected     = m_athletes && m_athletes->hasCurrentAthlete();
    facts.sessionRunning      = m_session  && m_session->running();
    // Capture-active is the user's own statement, and no buffer state can stand in for
    // it: with no cameras and no IMUs there are no sources, so the buffer stays paused
    // however loudly they have said they are hitting balls.
    facts.captureActive       = m_cameras  && m_cameras->captureIntent();

    const StandaloneVerdict verdict = decideStandalone(facts);
    if (verdict != StandaloneVerdict::Record) {
        ppInfo() << "LaunchMonitor: reading" << reading.deviceShotId
                 << "not recorded as a shot of its own —"
                 << standaloneVerdictReason(verdict);
        return false;
    }

    const auto alloc = m_paths.allocateSwingDir(m_settings->athleteLibraryPath(),
                                                m_athletes->currentName(),
                                                m_athletes->currentUuid(),
                                                m_settings->sessionNamingPattern(),
                                                QString());
    if (alloc.swingDir.isEmpty()) {
        ppWarn() << "LaunchMonitor: standalone shot skipped — could not create the swing folder";
        return false;
    }

    return writeDeviceOnly(alloc.swingDir, reading, alloc.swingId, alloc.swingIndex,
                           alloc.sessionId, /*existingShotId*/ -1);
}

bool LaunchMonitorController::applyToSwing(const QString &swingDir, const LaunchMonitorReading &r)
{
    // Following Premier's club: followDeviceClub() set it as the reading arrived, which is
    // normally before the shot's analysis began (the reading lands ~0.7 s after contact, the
    // analysis ~1.25 s after detection). When the analysis won that race the swing was recorded
    // under the old club: correct it in this same write and re-analyse with the right one.
    const QString club = followedClub(r);
    const QJsonObject captureClub = club.isEmpty() ? QJsonObject() : captureClubFor(club);
    bool clubReplaced = false;
    QString error;
    if (!pinpoint::SwingDocWriter::updateLaunchMonitor(swingDir, r, &error, captureClub, &clubReplaced)) {
        ppWarn() << "LaunchMonitor: cannot write reading to" << swingDir << ":" << error;
        return false;
    }
    if (clubReplaced) {
        ppInfo() << "LaunchMonitor: swing" << swingDir << "was analysed before Premier named its club;"
                 << "recorded as" << club << "and re-analysed";
        if (m_reanalyse)
            m_reanalyse(swingDir);
    }

    // Re-read the carousel row in place so the shot picks up its new metrics without
    // losing selection or ordering. The phase-grid sidecar is guarded on swing.json's
    // size and mtime, which the rewrite just moved, so the measures table regenerates
    // on its next read rather than serving a grid with no launch monitor rows in it.
    if (m_shotModel)
        m_shotModel->refreshShot(swingDir);
    rememberShotDir(r.deviceShotId, swingDir);

    m_lastReading = summaryOf(r);
    emit lastReadingChanged();

    emit readingApplied(swingDir);
    return true;
}

QString LaunchMonitorController::summaryOf(const LaunchMonitorReading &r) const
{
    QStringList bits;
    // Premier's ids are GUIDs; the first block is enough to tell shots apart on one line.
    if (!r.deviceShotId.isEmpty())
        bits << (r.deviceShotId.size() > 12 ? r.deviceShotId.left(8) : r.deviceShotId);
    if (!r.deviceClub.isEmpty())   bits << r.deviceClub;
    if (r.clubheadSpeed)           bits << tr("%1 mph").arg(*r.clubheadSpeed, 0, 'f', 1);
    else if (r.ballSpeed)          bits << tr("ball %1 mph").arg(*r.ballSpeed, 0, 'f', 1);
    if (r.carryDistance)           bits << tr("%1 yd").arg(*r.carryDistance, 0, 'f', 0);
    return bits.join(QStringLiteral(" · "));
}

QString LaunchMonitorController::followedClub(const LaunchMonitorReading &r)
{
    if (!m_settings || !m_settings->launchMonitorFollowClub()
        || r.deviceKind != kindKey(Kind::Premier) || r.deviceClub.isEmpty())
        return {};
    const QString club = premierClubToVocabulary(r.deviceClub);
    if (club.isEmpty() && r.deviceClub != m_unknownClubCode) {
        m_unknownClubCode = r.deviceClub;
        ppInfo() << "LaunchMonitor: Premier's club" << r.deviceClub
                 << "is not one PinPoint names; the session's club is left as it is";
    }
    return club;
}

void LaunchMonitorController::followDeviceClub(const LaunchMonitorReading &r)
{
    // Only into a running session: outside one the active club is the Home pick waiting for the
    // next session, and a ball hit on the simulator in between is not a choice of club.
    if (!m_session || !m_session->running())
        return;
    const QString club = followedClub(r);
    if (club.isEmpty() || club == m_session->activeClub())
        return;
    ppInfo() << "LaunchMonitor: Premier's club is" << r.deviceClub << "— the session's club is now"
             << club << "(was" << m_session->activeClub() << ")";
    m_session->setActiveClub(club);
}

QJsonObject LaunchMonitorController::captureClubFor(const QString &club) const
{
    // The same fields, from the same bag record, as the capture path (ShotProcessor's export job →
    // swing_exporter.cpp). No length prior: that is the live fuse's state for the club the swing
    // was analysed with, and re-analysis without one is the supported "cold key" case. A club
    // missing from the bag writes its name alone, and re-analysis takes its standard length.
    QVariantMap rec;
    if (m_athletes)
        rec = m_athletes->clubsFor(m_athletes->currentUuid()).value(club).toMap();
    QJsonArray bands;
    for (const QVariant &b : rec.value(QStringLiteral("bandCentersMm")).toList())
        bands.append(b.toDouble());
    return QJsonObject{
        { QStringLiteral("lengthMm"),        double(qMax(0, rec.value(QStringLiteral("lengthMm")).toInt())) },
        { QStringLiteral("shaftType"),       rec.value(QStringLiteral("shaftType")).toString() },
        { QStringLiteral("bandCentersMm"),   bands },
        { QStringLiteral("hoselFromButtMm"), qMax(0.0, rec.value(QStringLiteral("hoselFromButtMm")).toDouble()) },
        { QStringLiteral("shaftLengthMm"),   rec.value(QStringLiteral("shaftLengthMm")).toDouble() },
        { QStringLiteral("handsEndMm"),      rec.value(QStringLiteral("handsEndMm")).toDouble() },
        { QStringLiteral("name"),            club },
    };
}

void LaunchMonitorController::rememberShotDir(const QString &deviceShotId, const QString &swingDir)
{
    if (deviceShotId.isEmpty() || swingDir.isEmpty())
        return;
    if (!m_shotDirs.contains(deviceShotId))
        m_shotDirOrder.append(deviceShotId);
    m_shotDirs.insert(deviceShotId, swingDir);
    while (m_shotDirOrder.size() > 32)
        m_shotDirs.remove(m_shotDirOrder.takeFirst());
}

void LaunchMonitorController::onReadingUpdated(const LaunchMonitorReading &reading)
{
    // Still parked (the swing has not reached disk): the fuller reading simply takes the
    // parked one's place, and goes in whole when the swing does.
    if (m_pairing.replaceParked(reading)) {
        ppInfo() << "LaunchMonitor: a fuller reading (club data or carry) for" << reading.deviceShotId
                 << "arrived before its swing was saved; it goes in with the ball data";
        return;
    }
    const QString dir = m_shotDirs.value(reading.deviceShotId);
    if (dir.isEmpty()) {
        // A shot no swing claimed — dropped by the pairing, or never ours. Nothing to amend.
        ppInfo() << "LaunchMonitor: a fuller reading (club data or carry) for" << reading.deviceShotId
                 << "belongs to no swing here; discarded";
        return;
    }
    m_heldUpdates.insert(dir, reading);
    flushUpdates();
}

void LaunchMonitorController::setPipelineBusy(bool busy)
{
    m_pipelineBusy = busy;
    if (!busy)
        flushUpdates();
}

void LaunchMonitorController::onSwingRewritten(const QString &swingDir)
{
    if (m_heldUpdates.contains(swingDir))
        flushUpdates();
}

void LaunchMonitorController::flushUpdates()
{
    if (m_pipelineBusy || m_heldUpdates.isEmpty())
        return;
    // Keys first: the writes below emit signals, and nothing should be iterating the hash then.
    const QStringList dirs = m_heldUpdates.keys();
    for (const QString &dir : dirs) {
        if (m_folderHeld && m_folderHeld(dir))
            continue;                   // the re-analysis write goes first; onSwingRewritten retries
        const LaunchMonitorReading r = m_heldUpdates.take(dir);

        QString error;
        if (!pinpoint::SwingDocWriter::updateLaunchMonitor(dir, r, &error)) {
            ppWarn() << "LaunchMonitor: cannot write the club data for" << r.deviceShotId
                     << "to" << dir << ":" << error;
            continue;
        }
        ppInfo() << "LaunchMonitor: club data for" << r.deviceShotId << "written to" << dir;
        if (m_shotModel)
            m_shotModel->refreshShot(dir);
        m_lastReading = summaryOf(r);
        emit lastReadingChanged();
        emit readingUpdatedForSwing(dir);
    }
}
