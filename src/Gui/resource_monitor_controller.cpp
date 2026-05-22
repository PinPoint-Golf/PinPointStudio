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

#include "resource_monitor_controller.h"
#include "camera_manager.h"
#include "imu_manager.h"
#include "imu_instance.h"
#include "pp_debug.h"
#include "video_controller.h"

#include <cmath>

ResourceMonitorController::ResourceMonitorController(
    pinpoint::EventBuffer *buffer,
    CameraManager         *cameras,
    ImuManager            *imu,
    QObject               *parent)
    : QObject(parent)
    , m_buffer(buffer)
    , m_cameras(cameras)
    , m_imu(imu)
{
    m_ageTimer.start();
}

static QString bufferStateString(pinpoint::BufferState s)
{
    switch (s) {
    case pinpoint::BufferState::Idle:      return QStringLiteral("idle");
    case pinpoint::BufferState::Capturing: return QStringLiteral("capturing");
    case pinpoint::BufferState::Paused:    return QStringLiteral("paused");
    case pinpoint::BufferState::Stopping:  return QStringLiteral("stopping");
    }
    return QStringLiteral("idle");
}

static QString fmtCount(quint64 n)
{
    if (n >= 1000000) return QString::number(n / 1000000.0, 'f', 2) + QStringLiteral(" M");
    if (n >= 1000)    return QString::number(n / 1000.0,    'f', 1) + QStringLiteral(" K");
    return QString::number(n);
}

static QString fmtBytes(quint64 n)
{
    if (n >= 1073741824ULL) return QString::number(n / 1073741824.0, 'f', 2) + QStringLiteral(" GB");
    if (n >= 1048576ULL)    return QString::number(n / 1048576.0,    'f', 1) + QStringLiteral(" MB");
    if (n >= 1024ULL)       return QString::number(n / 1024.0,       'f', 0) + QStringLiteral(" KB");
    return QString::number(n) + QStringLiteral(" B");
}

static QString fmtInterArrival(int64_t us)
{
    if (us <= 0)         return QStringLiteral("—");
    if (us >= 1000000)   return QString::number(us / 1000000.0, 'f', 1) + QStringLiteral(" s");
    if (us >= 1000)      return QString::number(us / 1000.0,    'f', 1) + QStringLiteral(" ms");
    return QString::number(us) + QStringLiteral(" µs");
}

void ResourceMonitorController::refresh()
{
    if (!m_buffer) return;

    m_snapshot = m_buffer->diagnostics();
    const bool capturing = (m_snapshot.state == pinpoint::BufferState::Capturing);

    // ── Sources ──────────────────────────────────────────────────────────────
    m_sources.clear();
    for (const auto &src : m_snapshot.sources) {
        double fill = src.slot_count > 0
            ? std::min(1.0, double(src.events_written) / double(src.slot_count))
            : 0.0;
        quint64 overwritten = quint64(src.events_overwritten);
        int64_t maxIa = src.max_inter_arrival_us;
        QVariantMap m;
        m[QStringLiteral("name")]               = QString::fromStdString(src.name);
        m[QStringLiteral("eventsWritten")]       = quint64(src.events_written);
        m[QStringLiteral("eventsOverwritten")]   = overwritten;
        m[QStringLiteral("bytesWritten")]        = quint64(src.bytes_written_total);
        m[QStringLiteral("slotCount")]           = quint64(src.slot_count);
        m[QStringLiteral("stalled")]             = src.stalled;
        m[QStringLiteral("maxInterArrivalUs")]   = qint64(maxIa);
        m[QStringLiteral("boundsViolations")]    = quint64(src.bounds_violations);
        m[QStringLiteral("monoViolations")]      = quint64(src.monotonicity_violations);
        m[QStringLiteral("ringFillFraction")]    = fill;
        // Pre-formatted strings for QML (no JS formatting needed)
        m[QStringLiteral("eventsWrittenStr")]    = fmtCount(quint64(src.events_written));
        m[QStringLiteral("eventsOverwrittenStr")]= overwritten > 0 ? QString::number(overwritten) : QStringLiteral("—");
        m[QStringLiteral("bytesWrittenStr")]     = fmtBytes(quint64(src.bytes_written_total));
        m[QStringLiteral("maxInterArrivalStr")]  = fmtInterArrival(maxIa);
        m_sources.append(m);
    }

    // ── Devices ──────────────────────────────────────────────────────────────
    m_devices.clear();

    auto findSourceById = [this](pinpoint::SourceId sid)
        -> const pinpoint::EventBuffer::DiagnosticsSnapshot::SourceInfo*
    {
        for (const auto &src : m_snapshot.sources)
            if (src.id == sid) return &src;
        return nullptr;
    };

    // Camera entries — one per enumerated device, mirrors the IMU pattern.
    // A live VideoController is present only when the device is selected.
    {
        const QList<Device> camDevices =
            DeviceEnumerator::instance()->devices(DeviceType::VideoInput);

        for (const Device &camDev : camDevices) {
            VideoController *ctrl = m_cameras->controllerFor(camDev.id);

            const pinpoint::SourceId sid = ctrl
                ? ctrl->sourceId() : pinpoint::kInvalidSourceId;
            const auto *srcInfo = findSourceById(sid);

            QString backendStr;
            switch (camDev.backend) {
            case VideoInputFactory::Backend::Aravis:    backendStr = QStringLiteral("Aravis");         break;
            case VideoInputFactory::Backend::Spinnaker: backendStr = QStringLiteral("Spinnaker");      break;
            default:                                    backendStr = QStringLiteral("Qt Multimedia");  break;
            }

            const QString sn = camDev.capabilities.serialNumber;

            // Live frame rate and resolution from the controller when available;
            // fall back to enumerated capabilities before any frame has arrived.
            double rate = ctrl ? ctrl->cameraFps() : 0.0;
            int    fw   = ctrl ? ctrl->frameWidth()  : 0;
            int    fh   = ctrl ? ctrl->frameHeight() : 0;
            if (fw == 0 || fh == 0) {
                fw = camDev.capabilities.resolution.defaultResolution.width;
                fh = camDev.capabilities.resolution.defaultResolution.height;
            }

            QString camStatus;
            if (!ctrl)               camStatus = QStringLiteral("not selected");
            else if (ctrl->isRecording()) camStatus = QStringLiteral("streaming");
            else                     camStatus = QStringLiteral("stopped");

            quint64 evW  = srcInfo ? quint64(srcInfo->events_written)      : 0;
            quint64 byW  = srcInfo ? quint64(srcInfo->bytes_written_total)  : 0;
            quint64 evOW = srcInfo ? quint64(srcInfo->events_overwritten)   : 0;
            double  fill = srcInfo && srcInfo->slot_count > 0
                ? std::min(1.0, double(srcInfo->events_written) / double(srcInfo->slot_count))
                : 0.0;
            quint64 ringBytes = (sid != pinpoint::kInvalidSourceId)
                ? quint64(m_buffer->getSlotCapacity(sid)) * quint64(m_buffer->getSlotCount(sid))
                : 0;
            QString srcName = srcInfo ? QString::fromStdString(srcInfo->name) : QString();

            QVariantMap dev;
            dev[QStringLiteral("kind")]         = QStringLiteral("Camera");
            dev[QStringLiteral("name")]         = sn.isEmpty()
                ? camDev.description
                : camDev.description + QStringLiteral(" (") + sn + QStringLiteral(")");
            dev[QStringLiteral("model")]        = camDev.description;
            dev[QStringLiteral("serialNumber")] = sn;
            dev[QStringLiteral("backend")]      = backendStr;
            dev[QStringLiteral("status")]       = camStatus;
            dev[QStringLiteral("dataRateHz")]   = rate;
            dev[QStringLiteral("batteryPct")]   = -1;
            dev[QStringLiteral("sourceName")]   = srcName;
            dev[QStringLiteral("ringFill")]     = fill;
            dev[QStringLiteral("hasWarning")]   = capturing && srcInfo && srcInfo->stalled;
            dev[QStringLiteral("eventsWritten")]     = evW;
            dev[QStringLiteral("bytesWritten")]      = byW;
            dev[QStringLiteral("eventsOverwritten")] = evOW;
            dev[QStringLiteral("dataRateStr")]   = rate > 0
                ? QString::number(rate, 'f', 1) + QStringLiteral(" fps")
                : QStringLiteral("—");
            dev[QStringLiteral("eventsWrittenStr")]    = fmtCount(evW);
            dev[QStringLiteral("bytesWrittenStr")]     = fmtBytes(byW);
            dev[QStringLiteral("eventsOverwrittenStr")] = evOW > 0
                ? QString::number(evOW) : QStringLiteral("0");
            dev[QStringLiteral("ringCapacityStr")] = ringBytes > 0
                ? fmtBytes(ringBytes) : QStringLiteral("—");
            dev[QStringLiteral("resolutionStr")] = (fw > 0 && fh > 0)
                ? QString::number(fw) + QStringLiteral(" × ") + QString::number(fh)
                : QStringLiteral("—");
            m_devices.append(dev);
        }
    }

    // IMU entries — one per enumerated device (mirrors cameraList pattern).
    // Devices appear regardless of connection state; the live ImuInstance (if
    // selected) supplies data rates, battery, and buffer source info.
    {
        const QList<Device> imuDevices =
            DeviceEnumerator::instance()->devices(DeviceType::Imu);

        for (const Device &imuDev : imuDevices) {
            // Look up a live instance for this device (null when not selected).
            ImuInstance *inst = qobject_cast<ImuInstance *>(m_imu->instanceFor(imuDev.id));

            const pinpoint::SourceId sid = inst
                ? inst->sourceId() : pinpoint::kInvalidSourceId;
            const auto *imuSrc = findSourceById(sid);

            const QString backend = imuDev.imuTransport == ImuBase::Transport::Ble
                ? QStringLiteral("Bluetooth LE")
                : QStringLiteral("Serial");
            const QString model = imuDev.imuCapabilities.modelName.isEmpty()
                ? imuDev.description
                : imuDev.imuCapabilities.modelName;

            QString imuStatus;
            if (inst && inst->imuConnected()) imuStatus = QStringLiteral("connected");
            else if (inst && inst->busy())    imuStatus = QStringLiteral("connecting");
            else                              imuStatus = QStringLiteral("disconnected");

            double  imuRate      = inst ? inst->dataRateHz()     : 0.0;
            int     batPct       = inst ? inst->batteryPercent()  : -1;
            quint64 imuEW        = imuSrc ? quint64(imuSrc->events_written)      : 0;
            quint64 imuBW        = imuSrc ? quint64(imuSrc->bytes_written_total)  : 0;
            quint64 imuOW        = imuSrc ? quint64(imuSrc->events_overwritten)   : 0;
            quint64 imuRingBytes = imuSrc
                ? quint64(m_buffer->getSlotCapacity(imuSrc->id)) * quint64(imuSrc->slot_count)
                : 0;
            double fill = imuSrc && imuSrc->slot_count > 0
                ? std::min(1.0, double(imuSrc->events_written) / double(imuSrc->slot_count))
                : 0.0;
            QString imuSrcName = imuSrc
                ? QString::fromStdString(imuSrc->name) : QString();

            const QString imuId = imuDev.imuCapabilities.serialNumber.isEmpty()
                ? imuDev.id
                : imuDev.imuCapabilities.serialNumber;

            QVariantMap dev;
            dev[QStringLiteral("kind")]               = QStringLiteral("IMU");
            dev[QStringLiteral("name")]               = imuId.isEmpty()
                ? imuDev.description
                : imuDev.description + QStringLiteral(" (") + imuId + QStringLiteral(")");
            dev[QStringLiteral("model")]              = model;
            dev[QStringLiteral("backend")]            = backend;
            dev[QStringLiteral("identifier")]         = imuDev.id;
            dev[QStringLiteral("status")]             = imuStatus;
            dev[QStringLiteral("dataRateHz")]         = imuRate;
            dev[QStringLiteral("batteryPct")]         = batPct;
            dev[QStringLiteral("sourceName")]         = imuSrcName;
            dev[QStringLiteral("ringFill")]           = fill;
            dev[QStringLiteral("hasWarning")]         = capturing && imuSrc && imuSrc->stalled;
            dev[QStringLiteral("eventsWritten")]      = imuEW;
            dev[QStringLiteral("bytesWritten")]       = imuBW;
            dev[QStringLiteral("eventsOverwritten")]  = imuOW;
            dev[QStringLiteral("dataRateStr")]        = imuRate > 0
                ? QString::number(imuRate, 'f', 1) + QStringLiteral(" Hz")
                : QStringLiteral("—");
            dev[QStringLiteral("eventsWrittenStr")]   = fmtCount(imuEW);
            dev[QStringLiteral("bytesWrittenStr")]    = fmtBytes(imuBW);
            dev[QStringLiteral("eventsOverwrittenStr")] = imuOW > 0
                ? QString::number(imuOW) : QStringLiteral("0");
            dev[QStringLiteral("batteryStr")]         = batPct >= 0
                ? QString::number(batPct) + QStringLiteral(" %")
                : QStringLiteral("—");
            dev[QStringLiteral("ringCapacityStr")]    = imuRingBytes > 0
                ? fmtBytes(imuRingBytes) : QStringLiteral("—");
            m_devices.append(dev);
        }
    }

    // ── Warnings — only generated while actively capturing ───────────────────
    // Edge-trigger stall/anomaly conditions through ppWarn() so they flow into
    // PpMessageLog automatically.  m_activeWarnings prevents repeated firing.
    m_warnings.clear();
    if (capturing) for (const auto &src : m_snapshot.sources) {
        QString name = QString::fromStdString(src.name);
        if (src.stalled) {
            int64_t stalledMs = (m_snapshot.snapshot_timestamp_us - src.last_write_timestamp_us) / 1000;
            int fillPct = int(std::round(
                src.slot_count > 0
                    ? double(src.events_written) / double(src.slot_count) * 100.0
                    : 0.0));
            QString msg = QStringLiteral("%1 stalled — no events for %2 ms, ring %3% full")
                              .arg(name).arg(stalledMs).arg(fillPct);
            m_warnings.append(msg);
            if (!m_activeWarnings.contains(msg))
                ppWarn() << qPrintable(msg);
        }
        if (src.bounds_violations > 0 || src.monotonicity_violations > 0) {
            QString msg = QStringLiteral("%1: %2 timestamp anomalies (bounds: %3, monotonicity: %4)")
                              .arg(name)
                              .arg(src.bounds_violations + src.monotonicity_violations)
                              .arg(src.bounds_violations)
                              .arg(src.monotonicity_violations);
            m_warnings.append(msg);
            if (!m_activeWarnings.contains(msg))
                ppWarn() << qPrintable(msg);
        }
    }
    m_activeWarnings = QSet<QString>(m_warnings.begin(), m_warnings.end());

    // ── Pull new entries from the global PpMessageLog into the display list ──
    // Entries arrive oldest-first from fetchSince(); prepend each so the list
    // stays newest-first for the QML view.
    {
        const auto newEntries = PpMessageLog::instance()->fetchSince(m_logSeq);
        for (auto it = newEntries.crbegin(); it != newEntries.crend(); ++it) {
            QVariantMap row;
            row[QStringLiteral("timestamp")] = it->timestamp;
            row[QStringLiteral("severity")]  = it->severity;
            row[QStringLiteral("message")]   = it->message;
            m_messageLog.prepend(row);
        }
        while (m_messageLog.size() > kMaxLogEntries)
            m_messageLog.removeLast();
    }

    // ── Timeline history (delta per tick) ────────────────────────────────────
    quint64 curr = m_snapshot.timeline_entries;
    quint64 delta = (curr >= m_prevTimelineEntries) ? (curr - m_prevTimelineEntries) : 0;
    m_prevTimelineEntries = curr;
    m_timelineHistory.append(delta);
    while (m_timelineHistory.size() > kHistoryCount)
        m_timelineHistory.removeFirst();

    m_ageTimer.restart();
    emit snapshotChanged();
}

QString ResourceMonitorController::bufferState() const
{
    return bufferStateString(m_snapshot.state);
}

quint64 ResourceMonitorController::totalEvents() const
{
    quint64 total = 0;
    for (const auto &src : m_snapshot.sources)
        total += src.events_written;
    return total;
}

quint64 ResourceMonitorController::timelineEntries() const
{
    return m_snapshot.timeline_entries;
}

int ResourceMonitorController::sourceCount() const
{
    return int(m_snapshot.sources.size());
}

QVariantList ResourceMonitorController::sources() const { return m_sources; }
QVariantList ResourceMonitorController::devices() const { return m_devices; }
QStringList  ResourceMonitorController::warnings() const { return m_warnings; }

QString ResourceMonitorController::totalEventsStr() const     { return fmtCount(totalEvents()); }
QString ResourceMonitorController::timelineEntriesStr() const { return fmtCount(m_snapshot.timeline_entries); }

int ResourceMonitorController::snapshotAgeMs() const
{
    return int(m_ageTimer.elapsed());
}

QVariantList ResourceMonitorController::timelineHistory() const
{
    QVariantList result;
    for (quint64 v : m_timelineHistory)
        result.append(QVariant::fromValue(v));
    return result;
}

QVariantList ResourceMonitorController::messageLog() const { return m_messageLog; }

void ResourceMonitorController::clearLog()
{
    m_messageLog.clear();
    m_activeWarnings.clear();
    m_logSeq = PpMessageLog::instance()->currentSeq();
    emit snapshotChanged();
}
