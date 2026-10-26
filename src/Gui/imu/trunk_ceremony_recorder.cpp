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

#include "trunk_ceremony_recorder.h"

#include "pp_debug.h"
#include "pp_version.h"
#include "athlete_controller.h"
#include "event_buffer.h"
#include "hm_instance.h"
#include "imu_instance.h"
#include "imu_manager.h"
#include "imu_sample_tap.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QThreadPool>

namespace tcf = trunk_ceremony_file;

namespace {

int64_t nowUs() { return int64_t(pinpoint::EventBuffer::nowMicros()); }

QString roleLabel(const QString &role)
{
    return role == QLatin1String("pelvis") ? QStringLiteral("hips") : QStringLiteral("chest");
}

} // namespace

TrunkCeremonyRecorder::TrunkCeremonyRecorder(ImuManager *imuManager, AthleteController *athlete, QObject *parent)
    : QObject(parent), m_imu(imuManager), m_athlete(athlete)
{
}

TrunkCeremonyRecorder::~TrunkCeremonyRecorder()
{
    stopTaps();
}

QString TrunkCeremonyRecorder::currentStep() const
{
    for (auto it = m_ceremony.steps.rbegin(); it != m_ceremony.steps.rend(); ++it)
        if (it->open()) return it->name;
    return QString();
}

tcf::Step *TrunkCeremonyRecorder::lastStepNamed(const QString &name)
{
    for (auto it = m_ceremony.steps.rbegin(); it != m_ceremony.steps.rend(); ++it)
        if (it->name == name) return &*it;
    return nullptr;
}

void TrunkCeremonyRecorder::stopTaps()
{
    for (TapRef &t : m_taps)
        if (t.tap) t.tap->stop();
}

void TrunkCeremonyRecorder::clear()
{
    stopTaps();
    m_taps.clear();
    m_ceremony = tcf::Ceremony();
    m_recording = false;
    m_pending   = false;
}

void TrunkCeremonyRecorder::drain()
{
    for (TapRef &t : m_taps) {
        tcf::Lane &L = m_ceremony.lanes[t.lane];
        L.overCap = t.tap->take(L.tUs, L.rows);
        if (L.overCap > 0 && !t.warnedOverCap) {
            t.warnedOverCap = true;
            ppWarn() << "[TrunkCeremony]" << L.role << L.serial << "— reached the cap of"
                     << qulonglong(kMaxSamplesPerLane) << "samples; later samples are dropped (counted in overCap)";
        }
    }
}

bool TrunkCeremonyRecorder::begin()
{
    if (m_recording || m_pending)
        ppInfo() << "[TrunkCeremony] begin — the previous ceremony is dropped unwritten";
    clear();
    if (!m_imu) {
        ppWarn() << "[TrunkCeremony] begin — no IMU manager; nothing recorded";
        return false;
    }

    m_ceremony.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_ceremony.appVersion = QStringLiteral(PP_APP_VERSION);
    m_ceremony.handedness = (m_athlete && m_athlete->currentHandedness().compare(QLatin1String("Left"), Qt::CaseInsensitive) == 0)
                                ? QStringLiteral("left") : QStringLiteral("right");

    QStringList summary;
    const auto addLane = [&](QObject *device, std::shared_ptr<pinpoint::ImuSampleTap> tap, tcf::Lane lane, bool live) {
        if (!tap) return;
        summary << QStringLiteral("%1 %2 (%3%4)").arg(lane.role, lane.serial, lane.instrument,
                                                      live ? QString() : QStringLiteral(", not connected"));
        TapRef ref;
        ref.tap    = std::move(tap);
        ref.device = device;
        ref.lane   = m_ceremony.lanes.size();
        m_ceremony.lanes.push_back(std::move(lane));
        m_taps.push_back(std::move(ref));
    };

    // The trunk Witmotions. deviceForRole, not instanceForRole: the peripheral is what carries the
    // tap and the A/M, and for a Witmotion the two are one object anyway.
    for (const QString &role : { QStringLiteral("pelvis"), QStringLiteral("thorax") }) {
        auto *inst = qobject_cast<ImuInstance *>(m_imu->deviceForRole(role));
        if (!inst) continue;
        tcf::Lane L;
        L.role             = role;
        L.serial           = inst->laneSerial();
        L.deviceId         = inst->deviceId();
        L.instrument       = tcf::instrumentWitmotion();
        L.configuredRateHz = inst->outputRateHz();
        L.measuredRateHz   = inst->dataRateHz();
        addLane(inst, inst->ceremonyTap(), std::move(L), inst->imuConnected());
    }
    if (m_ceremony.lanes.empty()) {
        ppWarn() << "[TrunkCeremony] begin — no live sensor holds pelvis or thorax; nothing recorded";
        return false;
    }

    // The wG3, when worn (design §6.7: the trunk sensors record through its routine, and its lanes
    // with them). Its two units are fixed by the cable: lower arm = leadForearm, palm = leadHand.
    if (auto *hm = qobject_cast<HmInstance *>(m_imu->deviceForRole(QStringLiteral("leadForearm")))) {
        const struct { wr_unit unit; const char *role; HmUnit *u; } units[] = {
            { WR_UNIT_LOWER_ARM, "leadForearm", hm->unitLowerArm() },
            { WR_UNIT_PALM,      "leadHand",    hm->unitPalm() } };
        for (const auto &u : units) {
            tcf::Lane L;
            L.role           = QString::fromLatin1(u.role);
            L.serial         = u.u ? u.u->unitId() : HmUnit::unitIdFor(hm->deviceId(), u.unit);
            L.deviceId       = hm->deviceId();
            L.instrument     = tcf::instrumentHackMotion();
            L.measuredRateHz = hm->dataRateHz();
            addLane(hm, hm->ceremonyTap(int(u.unit)), std::move(L), hm->imuConnected());
        }
    }

    for (TapRef &t : m_taps)
        t.tap->start(kMaxSamplesPerLane);
    m_ceremony.beganUs = nowUs();
    m_recording = true;
    ppInfo() << "[TrunkCeremony] recording" << m_ceremony.lanes.size() << "lanes:"
             << qPrintable(summary.join(QStringLiteral(", ")));
    emit stateChanged();
    return true;
}

void TrunkCeremonyRecorder::stepStart(const QString &name)
{
    if (!m_recording) {
        ppWarn() << "[TrunkCeremony] stepStart" << name << "— not recording (begin() first); ignored";
        return;
    }
    const int64_t t = nowUs();
    // A step restarted without its end (the routine went back) closes where the new one starts.
    if (tcf::Step *s = lastStepNamed(name); s && s->open())
        s->endUs = t;
    tcf::Step s;
    s.name    = name;
    s.startUs = t;
    m_ceremony.steps.push_back(std::move(s));
    emit stateChanged();
}

void TrunkCeremonyRecorder::stepEnd(const QString &name)
{
    tcf::Step *s = lastStepNamed(name);
    if (!m_recording || !s || !s->open()) {
        ppWarn() << "[TrunkCeremony] stepEnd" << name << "— no open step of that name; ignored";
        return;
    }
    s->endUs = nowUs();
    emit stateChanged();
}

void TrunkCeremonyRecorder::mark(const QString &name)
{
    if (!m_recording) {
        ppWarn() << "[TrunkCeremony] mark" << name << "— not recording; ignored";
        return;
    }
    tcf::Step s;
    s.name    = name;
    s.startUs = s.endUs = nowUs();
    m_ceremony.steps.push_back(std::move(s));
}

void TrunkCeremonyRecorder::attachResult(const QString &stepName, const QVariantMap &result)
{
    tcf::Step *s = (m_recording || m_pending) ? lastStepNamed(stepName) : nullptr;
    if (!s) {
        ppWarn() << "[TrunkCeremony] attachResult" << stepName << "— no such step in the ceremony; ignored";
        return;
    }
    const QString role = result.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("pelvis") || role == QLatin1String("thorax"))
        s->result.insert(role, result);
    else
        s->result = result;
}

QVariantMap TrunkCeremonyRecorder::checkTurn(const QString &direction, bool leftHanded)
{
    const auto refuse = [](const QString &why) {
        ppWarn() << "[TrunkCeremony] checkTurn —" << why;
        return QVariantMap{ { QStringLiteral("valid"), false }, { QStringLiteral("failure"), why } };
    };
    const QString d = direction.trimmed().toLower();
    imu_calibration::TrunkTurnDirection dir;
    if (d == QLatin1String("away"))
        dir = imu_calibration::TrunkTurnDirection::Away;
    else if (d == QLatin1String("through"))
        dir = imu_calibration::TrunkTurnDirection::Through;
    else
        return refuse(QStringLiteral("Unknown turn direction \"%1\" (away or through)").arg(direction));
    if (!m_recording)
        return refuse(QStringLiteral("The turn could not be checked — the ceremony is not recording"));
    const QString stepName = dir == imu_calibration::TrunkTurnDirection::Away ? tcf::stepTurnAway()
                                                                               : tcf::stepTurnThrough();
    tcf::Step *step = lastStepNamed(stepName);
    if (!step)
        return refuse(QStringLiteral("The turn could not be checked — no %1 step was started").arg(stepName));

    drain();
    const int64_t tNow = nowUs();

    // Each sensor's window: the step's start to its end (now, while open), and no later than the
    // lane's last sample — every sample up to there is in the lane, so the file replays this
    // exact window (a sample stamped before now may still be on its way from the I/O thread).
    tcf::TurnInputs in;
    in.direction  = dir;
    in.leftHanded = leftHanded;
    QVariantMap extra[2];
    const QString roles[2] = { QStringLiteral("pelvis"), QStringLiteral("thorax") };
    tcf::TurnSensorInput *inputs[2] = { &in.pelvis, &in.thorax };
    for (int k = 0; k < 2; ++k) {
        tcf::TurnSensorInput &si = *inputs[k];
        si.windowStartUs = step->startUs;
        si.windowEndUs   = step->open() ? tNow : step->endUs;
        bool trunkCal = false;
        QString deviceId;
        for (const TapRef &t : m_taps) {
            const tcf::Lane &L = m_ceremony.lanes[t.lane];
            if (L.role != roles[k] || L.instrument != tcf::instrumentWitmotion()) continue;
            si.serial = L.serial;
            deviceId  = L.deviceId;
            if (!L.tUs.empty())
                si.windowEndUs = std::min(si.windowEndUs, L.tUs.back());
            if (auto *inst = qobject_cast<ImuInstance *>(t.device.data())) {
                si.A = inst->alignA();
                si.M = inst->mountM();
                trunkCal = !inst->trunkCalibration().isEmpty();
            }
            break;
        }
        extra[k].insert(QStringLiteral("deviceId"), deviceId);
        extra[k].insert(QStringLiteral("trunkCalibrated"), trunkCal);
    }

    // Per sensor, what the window held — so a zero reading says from the log whether the
    // quaternion never moved (spread 0), was broken (non-finite), or the window missed the turn.
    for (int k = 0; k < 2; ++k) {
        const tcf::TurnSensorInput &si = *inputs[k];
        const tcf::TurnWindowStats st = tcf::turnWindowStats(m_ceremony, si);
        ppInfo() << qPrintable(QStringLiteral("[TrunkCeremony] turn %1 window %2 %3: %4 samples, first %5 us last %6 us "
                                              "(step +%7 s .. +%8 s, window to +%9 s), quaternion spread %10 deg, "
                                              "non-finite %11, A/M %12")
                                   .arg(tcf::directionName(dir), roleLabel(roles[k]), si.serial)
                                   .arg(qulonglong(st.samples)).arg(qlonglong(st.firstUs)).arg(qlonglong(st.lastUs))
                                   .arg(st.firstUs >= 0 ? double(st.firstUs - si.windowStartUs) / 1e6 : -1.0, 0, 'f', 2)
                                   .arg(st.lastUs >= 0 ? double(st.lastUs - si.windowStartUs) / 1e6 : -1.0, 0, 'f', 2)
                                   .arg(double(si.windowEndUs - si.windowStartUs) / 1e6, 0, 'f', 2)
                                   .arg(st.spreadDeg, 0, 'f', 1).arg(qulonglong(st.nonFinite))
                                   .arg(st.alignFinite ? QStringLiteral("finite") : QStringLiteral("NON-FINITE")));
    }

    const imu_calibration::TrunkTurnRecord rec = tcf::runTurn(m_ceremony, in);
    QVariantMap out = tcf::turnRecordToMap(rec, in);
    for (int k = 0; k < 2; ++k) {
        QVariantMap m = out.value(roles[k]).toMap();
        for (auto it = extra[k].cbegin(); it != extra[k].cend(); ++it)
            m.insert(it.key(), it.value());
        out.insert(roles[k], m);
    }
    step->result = out;

    const auto sensorLine = [](const QString &who, const imu_calibration::TrunkTurnSensorRecord &r) {
        return QStringLiteral("%1 range %2 deg turn %3 S-I %4 axis %5 deg (%6 samples)")
            .arg(who).arg(double(r.rangeDeg), 0, 'f', 1).arg(double(r.turnDeg), 0, 'f', 1)
            .arg(double(r.siTurnDeg), 0, 'f', 1).arg(double(r.axisFromVerticalDeg), 0, 'f', 1).arg(r.samples);
    };
    ppInfo() << qPrintable(QStringLiteral("[TrunkCeremony] turn %1 (%2): %3%4 — %5; %6; chest/hips %7")
                               .arg(tcf::directionName(dir), leftHanded ? QStringLiteral("LH") : QStringLiteral("RH"),
                                    rec.valid ? QStringLiteral("VALID") : QStringLiteral("INVALID"),
                                    rec.failure.isEmpty() ? QString() : QStringLiteral(" (") + rec.failure + QStringLiteral(")"),
                                    sensorLine(roleLabel(QStringLiteral("thorax")), rec.thorax),
                                    sensorLine(roleLabel(QStringLiteral("pelvis")), rec.pelvis))
                               .arg(double(rec.chestOverHips), 0, 'f', 2));
    return out;
}

void TrunkCeremonyRecorder::end()
{
    if (!m_recording) {
        ppWarn() << "[TrunkCeremony] end — not recording; ignored";
        return;
    }
    drain();
    stopTaps();
    drain();   // the tail pushed before the taps stopped
    m_ceremony.endedUs  = nowUs();
    m_ceremony.endedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_recording = false;
    m_pending   = true;

    QStringList counts;
    for (const tcf::Lane &L : m_ceremony.lanes)
        counts << QStringLiteral("%1 %2").arg(L.role).arg(L.tUs.size());
    ppInfo() << "[TrunkCeremony] ended after"
             << qPrintable(QString::number(double(m_ceremony.endedUs - m_ceremony.beganUs) / 1e6, 'f', 1))
             << "s," << m_ceremony.steps.size() << "steps; samples" << qPrintable(counts.join(QStringLiteral(", ")))
             << "— held for the session";
    emit stateChanged();

    // Ended while a session is running (a re-check mid-session): written now.
    const QString dir = m_sessionDir ? m_sessionDir() : QString();
    if (!dir.isEmpty())
        write(dir);
}

void TrunkCeremonyRecorder::discard()
{
    if (!m_recording && !m_pending)
        return;
    ppInfo() << "[TrunkCeremony] discarded — not written";
    clear();
    emit stateChanged();
}

// The ceremony is still the one the session's trunk sensors are calibrated from: some trunk sensor
// it solved still carries that solve (its calibratedAt). With no valid solve at all it is kept — the
// record of a failed ceremony is worth having.
bool TrunkCeremonyRecorder::solvesStillInForce() const
{
    bool anySolved = false;
    for (const tcf::Step &s : m_ceremony.steps) {
        if (s.name != tcf::stepStandTall()) continue;
        for (const QString &role : { QStringLiteral("pelvis"), QStringLiteral("thorax") }) {
            const QVariantMap r = s.result.value(role).toMap();
            if (!r.value(QStringLiteral("valid")).toBool()) continue;
            anySolved = true;
            const QString at = r.value(QStringLiteral("calibratedAt")).toString();
            for (const TapRef &t : m_taps) {
                auto *inst = qobject_cast<ImuInstance *>(t.device.data());
                if (inst && m_ceremony.lanes[t.lane].role == role
                    && inst->trunkCalibration().value(QStringLiteral("calibratedAt")).toString() == at)
                    return true;
            }
        }
    }
    return !anySolved;
}

void TrunkCeremonyRecorder::onSessionStarted()
{
    if (m_recording) {
        ppWarn() << "[TrunkCeremony] the session started with the ceremony still recording — ended here";
        end();   // writes it when the session folder is known; else it is still pending below
    }
    if (!m_pending)
        return;
    if (!solvesStillInForce()) {
        ppInfo() << "[TrunkCeremony] dropped at session start — its trunk solves are no longer the ones in force";
        clear();
        emit stateChanged();
        return;
    }
    const QString dir = m_sessionDir ? m_sessionDir() : QString();
    if (dir.isEmpty()) {
        ppWarn() << "[TrunkCeremony] the session started without a session folder — ceremony not written";
        clear();
        emit stateChanged();
        return;
    }
    write(dir);
}

void TrunkCeremonyRecorder::write(const QString &sessionDir)
{
    auto c = std::make_shared<tcf::Ceremony>(std::move(m_ceremony));
    clear();
    emit stateChanged();

    // Off the GUI thread: a ceremony with a wG3 is a few MB of JSON, and the session folder may be
    // on a share (memory note: the 28 MB document froze the GUI).
    QThreadPool::globalInstance()->start([c, sessionDir]() {
        const QDir dir(sessionDir);
        const QString path = dir.filePath(QString::fromLatin1(tcf::kFileName));
        if (QFile::exists(path)) {   // an extended session's earlier ceremony: kept, renamed
            QString aside;
            for (int n = 2;; ++n) {
                aside = dir.filePath(QStringLiteral("trunk_ceremony_%1.json").arg(n));
                if (!QFile::exists(aside)) break;
            }
            if (!QFile::rename(path, aside))
                ppWarn() << "[TrunkCeremony] could not move the earlier" << path << "aside to" << aside;
        }
        const QByteArray bytes = tcf::toJson(*c);
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
            ppWarn() << "[TrunkCeremony] could not write" << path << ":" << f.errorString();
            return;
        }
        QStringList lanes;
        for (const tcf::Lane &L : c->lanes)
            lanes << QStringLiteral("%1 %2 %3").arg(L.role, L.serial).arg(L.tUs.size());
        ppInfo() << "[TrunkCeremony] wrote" << path << "—" << c->lanes.size() << "lanes ("
                 << qPrintable(lanes.join(QStringLiteral(", "))) << ")," << c->steps.size() << "steps,"
                 << qPrintable(QString::number(double(bytes.size()) / 1e6, 'f', 2)) << "MB";
    });
}
