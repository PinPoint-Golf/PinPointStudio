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

#include "camera_role_probe.h"

#include "camera_instance.h"
#include "camera_manager.h"
#include "camera_roi.h"
#include "app_settings.h"
#include "pp_debug.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QRectF>
#include <QTextStream>
#include <QTimer>
#include <cmath>

namespace {
// Long enough for the reconnect (factory set ~25 ms, Init, the latch), the
// auto loops to settle, and several 500 ms cameraFps samples.
constexpr int kSettleMs = 4500;

QString roleName(int r)
{
    switch (r) {
    case CameraInstance::None:        return QStringLiteral("None");
    case CameraInstance::DownTheLine: return QStringLiteral("DownTheLine");
    case CameraInstance::FaceOn:      return QStringLiteral("FaceOn");
    case CameraInstance::Other:       return QStringLiteral("Other");
    case CameraInstance::Impact:      return QStringLiteral("Impact");
    }
    return QString::number(r);
}

double num(const QVariantMap &m, const char *k)
{
    bool ok = false;
    const double v = m.value(QLatin1String(k)).toString().toDouble(&ok);
    return ok ? v : std::nan("");
}
QString str(const QVariantMap &m, const char *k) { return m.value(QLatin1String(k)).toString(); }
} // namespace

CameraRoleProbe::CameraRoleProbe(CameraManager *cameras, AppSettings *settings, QString reportPath,
                                 QString serial, QObject *parent)
    : QObject(parent), m_cameras(cameras), m_settings(settings),
      m_reportPath(std::move(reportPath)), m_serial(std::move(serial))
{
}

void CameraRoleProbe::start()
{
    m_report << QStringLiteral("CameraRoleProbe %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    findCameras(0);
}

void CameraRoleProbe::then(int delayMs, std::function<void()> step)
{
    m_queue.emplace_back(delayMs, std::move(step));
}

void CameraRoleProbe::runNext()
{
    if (m_queue.empty())
        return;
    auto [delay, step] = std::move(m_queue.front());
    m_queue.erase(m_queue.begin());
    QTimer::singleShot(delay, this, [this, step = std::move(step)]() {
        step();
        runNext();
    });
}

QVariantMap CameraRoleProbe::entryFor(const QString &key) const
{
    for (const QVariant &v : m_cameras->cameraList()) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("cameraKey")).toString() == key)
            return e;
    }
    return {};
}

CameraInstance *CameraRoleProbe::instanceFor(const QString &key) const
{
    for (const QVariant &v : m_cameras->instances()) {
        auto *ci = qobject_cast<CameraInstance *>(v.value<QObject *>());
        if (ci && ci->cameraKey() == key)
            return ci;
    }
    return nullptr;
}

// Enumeration runs asynchronously at launch: wait for the impact-capable
// (cropped-mode, i.e. GenICam) cameras to appear.
void CameraRoleProbe::findCameras(int attempt)
{
    std::vector<Target> targets;
    for (const QVariant &v : m_cameras->cameraList()) {
        const QVariantMap e = v.toMap();
        if (!e.value(QStringLiteral("impactCapable")).toBool())
            continue;
        Target t;
        t.index       = e.value(QStringLiteral("index")).toInt();
        t.key         = e.value(QStringLiteral("cameraKey")).toString();
        t.serial      = e.value(QStringLiteral("serialNumber")).toString();
        t.description = e.value(QStringLiteral("description")).toString();
        t.maxW        = e.value(QStringLiteral("maxWidth")).toDouble();
        t.maxH        = e.value(QStringLiteral("maxHeight")).toDouble();
        t.gainMaxDb   = e.value(QStringLiteral("gainMaxDb")).toDouble();
        t.impactModes = e.value(QStringLiteral("impactModes")).toList();
        t.wasSelected = e.value(QStringLiteral("selected")).toBool();
        if (t.key.isEmpty() || (!m_serial.isEmpty() && t.serial != m_serial))
            continue;
        targets.push_back(t);
    }
    if (targets.empty()) {
        if (attempt < 60) {
            QTimer::singleShot(500, this, [this, attempt]() { findCameras(attempt + 1); });
            return;
        }
        check(false, QStringLiteral("an impact-capable camera was enumerated within 30 s"));
        finish();
        return;
    }

    // The operator's state, restored at the end.
    m_perspective  = m_settings->cameraPerspective();
    m_targetFps    = m_settings->cameraTargetFps();
    m_exposureUs   = m_settings->cameraExposureUs();
    m_roi          = m_settings->cameraRoi();
    m_tuning       = m_settings->cameraTuning();
    m_wasRecording = m_cameras->isRecording();
    for (Target &t : targets)
        t.originalRole = m_perspective.value(t.key).toInt();

    // Connect every target and capture, as Connect + the live view would.
    then(0, [this, targets]() {
        for (const Target &t : targets)
            if (!t.wasSelected) { m_cameras->setSelected(t.index, true); m_deselect << t.index; }
        if (!m_cameras->isRecording()) m_cameras->startAll();
    });
    for (const Target &t : targets)
        planCamera(t);
    then(0, [this]() { finish(); });
    runNext();
}

// What the VIEW combo does before assignPerspective(Impact) on a camera with no
// impact mode yet (CamerasPanel.qml onActivated / applyImpactMode /
// setImpactExposure): default exposure, recommended mode's crop and rate.
void CameraRoleProbe::seedImpactLikeTheViewCombo(const Target &t)
{
    if (!m_settings->cameraExposureUs().contains(t.key)) {
        QVariantMap m = m_settings->cameraExposureUs();
        m[t.key] = CameraInstance::kImpactDefaultExposureUs;
        m_settings->setCameraExposureUs(m);
    }
    const QString stripKey = pp_camroi::key(t.key, CameraInstance::Impact);
    const double storedFps = m_settings->cameraTargetFps().value(t.key).toDouble();
    const bool stripUnset  = !m_settings->cameraRoi().contains(stripKey);
    if ((storedFps >= 420.0 && !stripUnset) || t.impactModes.isEmpty() || !(t.maxW > 0) || !(t.maxH > 0))
        return;
    const QVariantMap mode = t.impactModes.first().toMap();
    const double w = std::min(1.0, mode.value(QStringLiteral("w")).toDouble() / t.maxW);
    const double h = std::min(1.0, mode.value(QStringLiteral("h")).toDouble() / t.maxH);
    QVariantMap roiMap = m_settings->cameraRoi();
    const QVariantMap cur = roiMap.value(stripKey).toMap();
    const bool has = !cur.isEmpty();
    const double x = (has && cur.value(QStringLiteral("x")).toDouble() + w <= 1.0)
                         ? cur.value(QStringLiteral("x")).toDouble() : (1.0 - w) / 2.0;
    const double y = (has && cur.value(QStringLiteral("y")).toDouble() + h <= 1.0)
                         ? cur.value(QStringLiteral("y")).toDouble() : (1.0 - h) / 2.0;
    QVariantMap fps = m_settings->cameraTargetFps();
    fps[t.key] = mode.value(QStringLiteral("fps")).toDouble();
    m_settings->setCameraTargetFps(fps);
    m_cameras->setTargetFps(t.index, fps[t.key].toDouble());
    roiMap[stripKey] = QVariantMap{ { QStringLiteral("x"), x }, { QStringLiteral("y"), y },
                                 { QStringLiteral("w"), w }, { QStringLiteral("h"), h } };
    m_settings->setCameraRoi(roiMap);
}

void CameraRoleProbe::planCamera(const Target &t)
{
    // The non-Impact leg is DownTheLine: the role of the October incident.
    const int other = CameraInstance::DownTheLine;
    const int start = t.originalRole;

    then(kSettleMs, [this, t, start]() {
        m_obs.clear();
        m_report << QString() << QStringLiteral("=== %1 %2 (started as %3)")
                                     .arg(t.description, t.serial, roleName(start));
        observe(t, QStringLiteral("start"), start);
    });
    then(0, [this, t]() {
        seedImpactLikeTheViewCombo(t);
        m_cameras->assignPerspective(t.key, CameraInstance::Impact);
    });
    then(kSettleMs, [this, t]() { observe(t, QStringLiteral("impact1"), CameraInstance::Impact); });
    then(0, [this, t, other]() { m_cameras->assignPerspective(t.key, other); });
    then(kSettleMs, [this, t, other]() { observe(t, QStringLiteral("other"), other); });
    then(0, [this, t]() { m_cameras->assignPerspective(t.key, CameraInstance::Impact); });
    then(kSettleMs, [this, t]() { observe(t, QStringLiteral("impact2"), CameraInstance::Impact); });
    // Back to where it started — through the same call, so the restore is a
    // role change too — with the operator's own camera settings put back first.
    then(0, [this, t, start]() {
        m_settings->setCameraTargetFps(m_targetFps);
        m_settings->setCameraExposureUs(m_exposureUs);
        m_settings->setCameraRoi(m_roi);
        m_settings->setCameraTuning(m_tuning);
        m_cameras->assignPerspective(t.key, start);
    });
    then(kSettleMs, [this, t, start]() {
        observe(t, QStringLiteral("restored"), start);
        evaluate(t);
    });
}

void CameraRoleProbe::expectedSize(const Target &t, const QVariantMap &roiMap, int role, int *w, int *h)
{
    const QRectF r = pp_camroi::cropFor(roiMap, t.key, role);
    *w = (r.width()  > 0) ? int(std::lround(r.width()  * t.maxW)) : int(t.maxW);
    *h = (r.height() > 0) ? int(std::lround(r.height() * t.maxH)) : int(t.maxH);
}

void CameraRoleProbe::observe(const Target &t, const QString &step, int role)
{
    Observation o;
    o.step = step;
    o.role = role;
    o.reconnected = !m_obs.empty() && m_obs.back().instance.isNull();
    CameraInstance *ci = instanceFor(t.key);
    if (ci) {
        o.roleSeen  = ci->perspective();
        o.instance  = ci;
        o.cameraFps = ci->cameraFps();
        o.width     = ci->frameWidth();
        o.height    = ci->frameHeight();
        o.nodes     = ci->readBackSettings();
    }
    expectedSize(t, m_settings->cameraRoi(), role, &o.expectW, &o.expectH);
    m_obs.push_back(o);
    const QVariantMap &n = o.nodes;
    m_report << QStringLiteral("  %1 %2: %3 fps delivered, %4x%5 | ExposureAuto %6 ExposureTime %7 "
                               "GainAuto %8 Gain %9 BlackLevel %10 Rate %11 (auto %12) "
                               "Line1 %13 inv %14 | %15x%16+%17+%18")
                    .arg(step, -9).arg(roleName(role), -11)
                    .arg(o.cameraFps, 0, 'f', 1).arg(o.width).arg(o.height)
                    .arg(str(n, "ExposureAuto"), str(n, "ExposureTime"), str(n, "GainAuto"),
                         str(n, "Gain"), str(n, "BlackLevel"), str(n, "AcquisitionFrameRate"),
                         str(n, "AcquisitionFrameRateAuto"), str(n, "Line1.LineSource"),
                         str(n, "Line1.LineInverter"))
                    .arg(str(n, "Width"), str(n, "Height"), str(n, "OffsetX"), str(n, "OffsetY"));
    ppInfo() << "[CameraRoleProbe]" << m_report.last().trimmed().toUtf8().constData();
}

void CameraRoleProbe::check(bool ok, const QString &what)
{
    m_report << QStringLiteral("  %1  %2").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), what);
    if (!ok) {
        ++m_failures;
        ppWarn() << "[CameraRoleProbe] FAIL" << what.toUtf8().constData();
    }
}

void CameraRoleProbe::evaluate(const Target &t)
{
    m_report << QStringLiteral("  -- checks --");
    const auto find = [this](const QString &s) -> const Observation * {
        for (const Observation &o : m_obs) if (o.step == s) return &o;
        return nullptr;
    };
    const Observation *st = find(QStringLiteral("start")), *i1 = find(QStringLiteral("impact1")),
                      *ot = find(QStringLiteral("other")), *i2 = find(QStringLiteral("impact2")),
                      *rs = find(QStringLiteral("restored"));
    if (!st || !i1 || !ot || !i2 || !rs) { check(false, t.description + QStringLiteral(": every step observed")); return; }

    for (const Observation *o : { st, i1, ot, i2, rs })
        check(!o->nodes.isEmpty(), o->step + QStringLiteral(": the camera answered a read-back"));

    // Every move into or out of Impact rebuilt the instance (a reconnect);
    // the instance the probe found carries the new role.
    check(i1->reconnected || st->role == CameraInstance::Impact,
          QStringLiteral("start → Impact reconnected the camera"));
    check(ot->reconnected, QStringLiteral("Impact → other reconnected the camera"));
    check(i2->reconnected, QStringLiteral("other → Impact reconnected the camera"));
    // Every step delivers its OWN role's crop: the impact strip in Impact, the
    // camera's other-role crop otherwise (snapped down to the node increments).
    for (const Observation *o : { st, i1, ot, i2, rs })
        check(std::abs(o->width - o->expectW) <= 8 && std::abs(o->height - o->expectH) <= 8,
              o->step + QStringLiteral(": frames are this role's crop (%1x%2, expected %3x%4)")
                            .arg(o->width).arg(o->height).arg(o->expectW).arg(o->expectH));
    for (const Observation *o : { st, i1, ot, i2, rs })
        check(o->roleSeen == o->role, o->step + QStringLiteral(": instance role is %1 (%2)")
                                          .arg(roleName(o->role), roleName(o->roleSeen)));

    // The factory black level, as the camera holds it in a non-Impact role.
    const double factoryBl = num(ot->nodes, "BlackLevel");
    const double expUs     = m_settings->cameraExposureUs().value(t.key, CameraInstance::kImpactDefaultExposureUs).toDouble();
    const QVariantMap tuning = m_tuning.value(t.key).toMap();
    const double gainReq   = tuning.value(QStringLiteral("gainDb"), CameraInstance::kImpactDefaultGainDb).toDouble();
    const double gainWant  = t.gainMaxDb > 0 ? std::min(gainReq, t.gainMaxDb) : gainReq;
    const bool   strobe    = tuning.value(QStringLiteral("strobe"), false).toBool();

    for (const Observation *o : { i1, i2 }) {
        const QVariantMap &n = o->nodes;
        const QString s = o->step + QStringLiteral(": ");
        check(str(n, "ExposureAuto") == QLatin1String("Off") && std::fabs(num(n, "ExposureTime") - expUs) < 2.0,
              s + QStringLiteral("exposure locked at %1 us (%2 %3)").arg(expUs).arg(str(n, "ExposureAuto"), str(n, "ExposureTime")));
        check(str(n, "GainAuto") == QLatin1String("Off") && std::fabs(num(n, "Gain") - gainWant) < 0.2,
              s + QStringLiteral("gain locked at %1 dB (%2)").arg(gainWant).arg(str(n, "Gain")));
        check(std::fabs(num(n, "BlackLevel") - (factoryBl + CameraInstance::kImpactBlackLevelLiftPct)) < 0.1,
              s + QStringLiteral("black level = factory %1 + %2 (%3)").arg(factoryBl).arg(CameraInstance::kImpactBlackLevelLiftPct).arg(str(n, "BlackLevel")));
        check(str(n, "AcquisitionFrameRateAuto") != QLatin1String("Continuous"),
              s + QStringLiteral("rate manual"));
        check(num(n, "Height") < t.maxH && o->height == int(num(n, "Height")) && o->width == int(num(n, "Width")),
              s + QStringLiteral("cropped, and the frames are the crop (%1x%2)").arg(o->width).arg(o->height));
        check(o->cameraFps > 0.9 * num(n, "AcquisitionFrameRate"),
              s + QStringLiteral("delivers its rate (%1 of %2 fps)").arg(o->cameraFps, 0, 'f', 1).arg(str(n, "AcquisitionFrameRate")));
        check((str(n, "Line1.LineSource") == QLatin1String("ExposureActive")) == strobe,
              s + QStringLiteral("strobe %1 (Line1 %2)").arg(strobe ? QStringLiteral("on") : QStringLiteral("off"), str(n, "Line1.LineSource")));
    }
    for (const Observation *o : { st, ot, rs }) {
        if (o->role == CameraInstance::Impact) continue;
        const QVariantMap &n = o->nodes;
        const QString s = o->step + QStringLiteral(": ");
        check(str(n, "ExposureAuto") == QLatin1String("Continuous"), s + QStringLiteral("auto exposure (%1)").arg(str(n, "ExposureAuto")));
        check(str(n, "GainAuto") == QLatin1String("Continuous"), s + QStringLiteral("auto gain (%1)").arg(str(n, "GainAuto")));
        check(std::fabs(num(n, "BlackLevel") - factoryBl) < 0.01, s + QStringLiteral("factory black level (%1)").arg(str(n, "BlackLevel")));
        check(str(n, "AcquisitionFrameRateAuto") != QLatin1String("Continuous"), s + QStringLiteral("rate manual"));
        check(num(n, "AcquisitionFrameRate") > 140.0, s + QStringLiteral("full-height rate (%1)").arg(str(n, "AcquisitionFrameRate")));
        check(o->cameraFps > 0.9 * num(n, "AcquisitionFrameRate"),
              s + QStringLiteral("delivers its rate (%1 of %2 fps)").arg(o->cameraFps, 0, 'f', 1).arg(str(n, "AcquisitionFrameRate")));
        check(str(n, "Line1.LineSource") != QLatin1String("ExposureActive"), s + QStringLiteral("no strobe (Line1 %1)").arg(str(n, "Line1.LineSource")));
        check(num(n, "ExposureTime") <= 1e6 / num(n, "AcquisitionFrameRate") + 1.0,
              s + QStringLiteral("exposure within the frame period (%1 us)").arg(str(n, "ExposureTime")));
    }
    // Back where it started: the operator's settings and the original role.
    check(m_settings->cameraTargetFps() == m_targetFps && m_settings->cameraExposureUs() == m_exposureUs
          && m_settings->cameraRoi() == m_roi && m_settings->cameraTuning() == m_tuning,
          QStringLiteral("the operator's camera settings are restored"));
    check(m_settings->cameraPerspective().value(t.key).toInt() == t.originalRole,
          QStringLiteral("the original role is restored (%1)").arg(roleName(t.originalRole)));
}

void CameraRoleProbe::finish()
{
    // Restore the other cameras' roles too (an Impact assignment strips it
    // from every other camera), then connection and capture state.
    if (m_settings && !m_perspective.isEmpty() && m_settings->cameraPerspective() != m_perspective)
        m_settings->setCameraPerspective(m_perspective);
    if (m_cameras) {
        for (int i : m_deselect)
            m_cameras->setSelected(i, false);
        if (!m_wasRecording && m_cameras->isRecording())
            m_cameras->stopAll();
    }
    m_report << QString() << QStringLiteral("%1 (%2 failure%3)")
                                 .arg(m_failures ? QStringLiteral("FAILED") : QStringLiteral("ALL PASSED"))
                                 .arg(m_failures).arg(m_failures == 1 ? QString() : QStringLiteral("s"));
    QFile f(m_reportPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << m_report.join(QLatin1Char('\n')) << '\n';
    }
    ppInfo() << "[CameraRoleProbe]" << m_report.last().toUtf8().constData() << "- report"
             << m_reportPath.toUtf8().constData();
    QTimer::singleShot(1000, qApp, &QCoreApplication::quit);
}
