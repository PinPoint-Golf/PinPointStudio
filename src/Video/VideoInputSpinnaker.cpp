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

#ifdef HAVE_SPINNAKER
// Include Spinnaker headers before Qt to avoid macro conflicts (like 'signals')
#include "SpinnakerPlatform.h"
#undef SPINNAKER_DEPRECATED_CLASS
#define SPINNAKER_DEPRECATED_CLASS(msg) class SPINNAKER_API __declspec(deprecated(msg))
#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#endif

#include "VideoInputSpinnaker.h"
#include "spinnaker_runtime.h"
#include "frame_crop.h"
#include "raw_video_frame.h"
#include <QVideoFrame>
#include <QDateTime>
#include "pp_debug.h"
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

#ifdef HAVE_SPINNAKER
using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;

class SpinLogHandler : public LoggingEventHandler {
public:
    void OnLogEvent(LoggingEventDataPtr eventPtr) override {
        // Suppress known-benign SDK-internal noise that fires on every
        // connect/disconnect cycle:
        //  - GCRegisterEvent / RegisterEventHandler NOT_IMPLEMENTED (-1003):
        //    the GenTL producer does not implement some optional event types,
        //    yet the SDK retries registration on every Init/BeginAcquisition.
        //  - GetNextImage abort (-1012): the normal result of EndAcquisition()
        //    interrupting a blocking GetNextImage(); the capture loop already
        //    suppresses its own copy of this error.
        const QString msg = QString::fromLocal8Bit(eventPtr->GetLogMessage());
        if (msg.contains(QLatin1String("-1003")) && msg.contains(QLatin1String("RegisterEvent")))
            return;
        if (msg.contains(QLatin1String("-1012")) && msg.contains(QLatin1String("aborted")))
            return;
        const int pri = eventPtr->GetPriority();
        if (pri <= SPINNAKER_LOG_LEVEL_ERROR)
            ppError() << "[Spinnaker]" << eventPtr->GetCategoryName()
                      << "-" << eventPtr->GetLogMessage();
        else
            ppWarn()  << "[Spinnaker]" << eventPtr->GetCategoryName()
                      << "-" << eventPtr->GetLogMessage();
    }
};

// Apply the normalized crop region (or full sensor when inactive) via the
// GenICam Width/Height/OffsetX/OffsetY nodes. Must run before
// BeginAcquisition() — the nodes are read-only while streaming. Values are
// snapped DOWN to the node increments so a delivered frame never exceeds the
// ring slot sized from the same (ceil'd) crop fraction upstream; FLIR
// increments are even on Bayer sensors, so the CFA phase is preserved.
// On failure the full frame is restored best-effort and the software crop in
// CameraInstance engages (frames arrive larger than the expected crop size).
static void applySpinnakerRoi(INodeMap &nodeMap, const QRectF &crop)
{
    try {
        CIntegerPtr w  = nodeMap.GetNode("Width");
        CIntegerPtr h  = nodeMap.GetNode("Height");
        CIntegerPtr ox = nodeMap.GetNode("OffsetX");
        CIntegerPtr oy = nodeMap.GetNode("OffsetY");
        if (!IsAvailable(w) || !IsWritable(w) || !IsAvailable(h) || !IsWritable(h)) {
            if (pp_crop::cropIsActive(crop))
                ppWarn() << "[VideoInputSpinnaker] Width/Height nodes not writable;"
                         << "software crop will engage";
            return;
        }

        auto snapDown = [](int64_t v, int64_t inc, int64_t lo, int64_t hi) {
            inc = std::max<int64_t>(1, inc);
            v   = std::clamp(v, lo, hi);
            return lo + ((v - lo) / inc) * inc;
        };

        // Offsets to minimum first so a stale ROI never clamps Width/Height max.
        if (IsAvailable(ox) && IsWritable(ox)) ox->SetValue(ox->GetMin());
        if (IsAvailable(oy) && IsWritable(oy)) oy->SetValue(oy->GetMin());

        // True sensor dims come from WidthMax/HeightMax — Width's own max is
        // WidthMax - OffsetX, so it under-reports whenever a stale offset is
        // still programmed (e.g. the offset nodes above were not writable).
        CIntegerPtr wMaxNode = nodeMap.GetNode("WidthMax");
        CIntegerPtr hMaxNode = nodeMap.GetNode("HeightMax");
        const int64_t sensorW = (IsAvailable(wMaxNode) && IsReadable(wMaxNode))
                                    ? wMaxNode->GetValue() : w->GetMax();
        const int64_t sensorH = (IsAvailable(hMaxNode) && IsReadable(hMaxNode))
                                    ? hMaxNode->GetValue() : h->GetMax();

        int64_t rw = sensorW, rh = sensorH;
        if (pp_crop::cropIsActive(crop)) {
            rw = snapDown(int64_t(std::llround(crop.width()  * sensorW)),
                          w->GetInc(), w->GetMin(), sensorW);
            rh = snapDown(int64_t(std::llround(crop.height() * sensorH)),
                          h->GetInc(), h->GetMin(), sensorH);
        }
        // GenICam ordering: shrink Width/Height FIRST, then move the offsets
        // (their GetMax() grows to sensor - size once the size is reduced).
        w->SetValue(rw);
        h->SetValue(rh);
        if (pp_crop::cropIsActive(crop)) {
            if (IsAvailable(ox) && IsWritable(ox))
                ox->SetValue(snapDown(int64_t(std::llround(crop.x() * sensorW)),
                                      ox->GetInc(), ox->GetMin(), ox->GetMax()));
            if (IsAvailable(oy) && IsWritable(oy))
                oy->SetValue(snapDown(int64_t(std::llround(crop.y() * sensorH)),
                                      oy->GetInc(), oy->GetMin(), oy->GetMax()));
            ppInfo() << "[VideoInputSpinnaker] ROI applied:"
                     << qlonglong(rw) << "x" << qlonglong(rh);
        }
    } catch (Spinnaker::Exception &e) {
        ppWarn() << "[VideoInputSpinnaker] ROI set failed:" << e.what()
                 << "- full frame; software crop will engage";
        try { // restore full frame best-effort so capture still works
            CIntegerPtr w  = nodeMap.GetNode("Width");
            CIntegerPtr h  = nodeMap.GetNode("Height");
            CIntegerPtr ox = nodeMap.GetNode("OffsetX");
            CIntegerPtr oy = nodeMap.GetNode("OffsetY");
            if (IsAvailable(ox) && IsWritable(ox)) ox->SetValue(ox->GetMin());
            if (IsAvailable(oy) && IsWritable(oy)) oy->SetValue(oy->GetMin());
            if (IsAvailable(w)  && IsWritable(w))  w->SetValue(w->GetMax());
            if (IsAvailable(h)  && IsWritable(h))  h->SetValue(h->GetMax());
        } catch (...) {}
    }
}
#endif

VideoInputSpinnaker::VideoInputSpinnaker(QObject *parent)
    : VideoInputBase(parent)
{
}

VideoInputSpinnaker::~VideoInputSpinnaker()
{
    stop();
}

bool VideoInputSpinnaker::start(const QString &deviceId)
{
    stop();
    m_abort = false;

#ifdef HAVE_SPINNAKER
    // The SDK is delay-loaded and never redistributed; bail out (without touching any
    // Spinnaker symbol) when the user has not installed it.
    if (!pinpoint::spinnaker::runtimeAvailable()) {
        emit errorOccurred(tr("Spinnaker SDK not found."));
        return false;
    }
    try {
        SystemPtr *system = new SystemPtr(System::GetInstance());
        m_system = system;

        auto *logHandler = new SpinLogHandler();
        (*system)->RegisterLoggingEventHandler(*logHandler);
        (*system)->SetLoggingEventPriorityLevel(SPINNAKER_LOG_LEVEL_WARN);
        m_logHandler = logHandler;

        CameraList camList = (*system)->GetCameras();
        unsigned int numCameras = camList.GetSize();

        if (numCameras == 0) {
            camList.Clear();
            emit errorOccurred(tr("No Spinnaker cameras found."));
            return false;
        }

        CameraPtr *camera = nullptr;
        if (deviceId.isEmpty()) {
            camera = new CameraPtr(camList.GetByIndex(0));
        } else {
            for (unsigned int i = 0; i < numCameras; i++) {
                CameraPtr cam = camList.GetByIndex(i);
                INodeMap& nodeMapTLDevice = cam->GetTLDeviceNodeMap();
                CStringPtr ptrDeviceID = nodeMapTLDevice.GetNode("DeviceID");
                if (IsAvailable(ptrDeviceID) && IsReadable(ptrDeviceID)) {
                    QString id = QString::fromStdString(ptrDeviceID->GetValue().c_str());
                    if (id == deviceId) {
                        camera = new CameraPtr(cam);
                        break;
                    }
                }
            }
        }

        camList.Clear();

        if (!camera) {
            emit errorOccurred(tr("Failed to find requested Spinnaker camera."));
            return false;
        }

        m_camera = camera;
        (*camera)->Init();

        INodeMap& nodeMap = (*camera)->GetNodeMap();

        CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
        if (IsAvailable(ptrAcquisitionMode) && IsWritable(ptrAcquisitionMode)) {
            CEnumEntryPtr ptrContinuous = ptrAcquisitionMode->GetEntryByName("Continuous");
            if (IsAvailable(ptrContinuous) && IsReadable(ptrContinuous))
                ptrAcquisitionMode->SetIntValue(ptrContinuous->GetValue());
        }

        // Prefer raw Bayer formats: lower bus bandwidth (1 byte/pixel vs 3),
        // camera free from ISP work, and host GPU handles demosaic.
        // Fall back to RGB8Packed or Mono8 if no Bayer format is available.
        struct FmtEntry { const char *name; int pattern; bool isBayer; };
        static const FmtEntry fmtPriority[] = {
            {"BayerRG8", 0, true},
            {"BayerBG8", 1, true},
            {"BayerGR8", 2, true},
            {"BayerGB8", 3, true},
            {"BGR8",     0, false},
            {"RGB8Packed", 0, false},
            {"Mono8",    0, false},
        };

        m_emitRaw = false;
        CEnumerationPtr ptrPixelFormat = nodeMap.GetNode("PixelFormat");
        if (IsAvailable(ptrPixelFormat) && IsWritable(ptrPixelFormat)) {
            for (const auto &fmt : fmtPriority) {
                CEnumEntryPtr ptrEntry = ptrPixelFormat->GetEntryByName(fmt.name);
                if (IsAvailable(ptrEntry) && IsReadable(ptrEntry)) {
                    ptrPixelFormat->SetIntValue(ptrEntry->GetValue());
                    m_bayerPattern = fmt.pattern;
                    m_emitRaw      = fmt.isBayer;
                    ppDebug() << "[VideoInputSpinnaker] Pixel format:" << fmt.name
                             << (fmt.isBayer ? "(raw Bayer, GPU demosaic)" : "(pre-decoded)");
                    break;
                }
            }
        }

        // Hardware ROI (or full sensor) — must precede BeginAcquisition().
        applySpinnakerRoi(nodeMap, m_cropRegion);

        // Increase the image buffer pool. StreamBufferCount lives in the
        // TL Stream node map (transport layer), NOT the device node map.
        // Default pool size is typically 10; raw Bayer (1 byte/pixel) fills
        // buffers 3x faster than BGR8, exhausting the pool at high frame rates.
        INodeMap &streamMap = (*camera)->GetTLStreamNodeMap();
        CEnumerationPtr ptrBufferMode = streamMap.GetNode("StreamBufferCountMode");
        CIntegerPtr ptrBufferCount = streamMap.GetNode("StreamBufferCountManual");
        if (IsAvailable(ptrBufferMode) && IsWritable(ptrBufferMode)) {
            CEnumEntryPtr ptrManual = ptrBufferMode->GetEntryByName("Manual");
            if (IsAvailable(ptrManual) && IsReadable(ptrManual))
                ptrBufferMode->SetIntValue(ptrManual->GetValue());
        }
        if (IsAvailable(ptrBufferCount) && IsWritable(ptrBufferCount)) {
            const int64_t desired = 40;
            ptrBufferCount->SetValue(
                qBound(ptrBufferCount->GetMin(), desired, ptrBufferCount->GetMax()));
            ppDebug() << "[VideoInputSpinnaker] Stream buffer count:"
                     << ptrBufferCount->GetValue();
        }

        // --- Per-frame exposure via chunk data ---
        // Enable ChunkExposureTime so captureLoop() can read the exposure that
        // was actually applied to each delivered frame (GetChunkData()), rather
        // than querying the ExposureTime node (which lags under auto-exposure).
        // Nodes are read-only once BeginAcquisition() runs, so configure here.
        m_chunkExposureEnabled = false;
        CBooleanPtr ptrChunkMode = nodeMap.GetNode("ChunkModeActive");
        if (IsAvailable(ptrChunkMode) && IsWritable(ptrChunkMode)) {
            ptrChunkMode->SetValue(true);
            CEnumerationPtr ptrChunkSel = nodeMap.GetNode("ChunkSelector");
            CEnumEntryPtr   ptrSelExp   = IsAvailable(ptrChunkSel)
                                              ? ptrChunkSel->GetEntryByName("ExposureTime")
                                              : nullptr;
            if (IsAvailable(ptrChunkSel) && IsWritable(ptrChunkSel)
                && IsAvailable(ptrSelExp) && IsReadable(ptrSelExp)) {
                ptrChunkSel->SetIntValue(ptrSelExp->GetValue());
                CBooleanPtr ptrChunkEnable = nodeMap.GetNode("ChunkEnable");
                if (IsAvailable(ptrChunkEnable) && IsWritable(ptrChunkEnable)) {
                    ptrChunkEnable->SetValue(true);
                    m_chunkExposureEnabled = true;
                }
            }
        }

        // Cache the ExposureAuto mode once (readable pre-BeginAcquisition). The
        // per-frame chunk gives the applied exposure value; this flag records
        // whether that value is a stable manual setting or auto-varying.
        m_exposureAuto = -1;
        CEnumerationPtr ptrExpAutoMode = nodeMap.GetNode("ExposureAuto");
        if (IsAvailable(ptrExpAutoMode) && IsReadable(ptrExpAutoMode)) {
            CEnumEntryPtr cur = ptrExpAutoMode->GetCurrentEntry();
            if (IsAvailable(cur) && IsReadable(cur)) {
                const std::string sym = cur->GetSymbolic().c_str();
                m_exposureAuto = (sym == "Off") ? 0 : 1;  // Continuous/Once -> auto-varying
            }
        }
        ppDebug() << "[VideoInputSpinnaker] Chunk exposure enabled:" << m_chunkExposureEnabled
                 << "ExposureAuto mode:" << m_exposureAuto;

        (*camera)->BeginAcquisition();
        m_streaming = true;
        m_state = State::Active;
        emit stateChanged(State::Active);

        // Keep the future so stop() can join the loop before deleting the
        // CameraPtr it dereferences.
        m_captureFuture = QtConcurrent::run([this]() { captureLoop(); });

        return true;
    } catch (Spinnaker::Exception &e) {
        emit errorOccurred(tr("Spinnaker error: %1").arg(e.what()));
        // Release any partially-acquired camera/system handles. A zombie
        // m_camera would DeInit the device long after another instance has
        // taken ownership of it (stop() skips DeInit while it is streaming).
        stop();
        return false;
    }
#else
    Q_UNUSED(deviceId)
    return false;
#endif
}

void VideoInputSpinnaker::stop()
{
    m_abort = true;

#ifdef HAVE_SPINNAKER
    if (m_camera) {
        CameraPtr *camera = (CameraPtr*)m_camera;
        try {
            if (m_streaming) {
                (*camera)->EndAcquisition();
                m_streaming = false;
            }
        } catch (...) {}

        // Join the pool-thread capture loop BEFORE the teardown below deletes
        // the CameraPtr it dereferences. EndAcquisition above aborts a blocked
        // GetNextImage (-1012), and the 1 s GetNextImage timeout bounds the
        // wait regardless. The loop's frame hand-off is a queued invoke, so
        // waiting here cannot deadlock.
        m_captureFuture.waitForFinished();

        try {
            // If the device is still streaming here, it belongs to ANOTHER
            // VideoInputSpinnaker instance — a stale handle's teardown raced
            // a reconnect (e.g. the settings crop-editor preview). Leave the
            // device alone: DeInit would fail with -1004, and the full-frame
            // restore below would clobber the new owner's crop ROI.
            if (!(*camera)->IsStreaming()) {
                // Leave the camera at full frame. The ROI nodes are retained
                // across connections (and across app runs while the camera
                // stays powered), and a stale crop poisons every later
                // Width/Height GetMax() read — capability queries would
                // under-report the sensor size, shrinking the next session's
                // crop sizing.
                try {
                    applySpinnakerRoi((*camera)->GetNodeMap(), QRectF());
                } catch (...) {}
                (*camera)->DeInit();
            } else {
                ppDebug() << "[VideoInputSpinnaker] device streaming under another"
                          << "handle — skipping ROI restore / DeInit";
            }
        } catch (...) {}
        delete camera;
        m_camera = nullptr;
    }

    if (m_system) {
        SystemPtr *system = (SystemPtr*)m_system;
        if (m_logHandler) {
            try {
                (*system)->UnregisterLoggingEventHandler(*(SpinLogHandler*)m_logHandler);
            } catch (...) {}
            delete (SpinLogHandler*)m_logHandler;
            m_logHandler = nullptr;
        }
        try {
            (*system)->ReleaseInstance();
        } catch (...) {}
        delete system;
        m_system = nullptr;
    }
#endif

    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void VideoInputSpinnaker::suspend()
{
    if (m_state == State::Active) {
        m_state = State::Suspended;
        emit stateChanged(State::Suspended);
    }
}

void VideoInputSpinnaker::resume()
{
    if (m_state == State::Suspended) {
        m_state = State::Active;
        emit stateChanged(State::Active);
    }
}

bool VideoInputSpinnaker::isActive() const
{
    return m_state == State::Active;
}

QVideoFrameFormat VideoInputSpinnaker::frameFormat() const
{
    return QVideoFrameFormat();
}

CameraCapabilities VideoInputSpinnaker::queryCapabilities() const
{
    CameraCapabilities caps;
    caps.queriedAt   = QDateTime::currentDateTime();
    caps.driverVersion = "Teledyne Spinnaker SDK";

#ifdef HAVE_SPINNAKER
    // Do not open a live connection before start() — return empty so that
    // callers can safely query capabilities pre-start without side-effects.
    if (!m_camera) return caps;

    auto doQuery = [&caps](Spinnaker::CameraPtr camera) {
        using namespace Spinnaker::GenApi;

        // --- Identity (TL Device nodemap) ---
        INodeMap &tlMap = camera->GetTLDeviceNodeMap();
        auto readStr = [&tlMap](const char *name) -> QString {
            CStringPtr n = tlMap.GetNode(name);
            if (IsAvailable(n) && IsReadable(n))
                return QString::fromStdString(n->GetValue().c_str());
            return {};
        };
        caps.vendorName      = readStr("DeviceVendorName");
        caps.modelName       = readStr("DeviceModelName");
        caps.serialNumber    = readStr("DeviceSerialNumber");
        caps.firmwareVersion = readStr("DeviceVersion");

        CStringPtr ptrIface = tlMap.GetNode("DeviceType");
        if (IsAvailable(ptrIface) && IsReadable(ptrIface)) {
            QString iface = QString::fromStdString(ptrIface->GetValue().c_str());
            if (iface.contains("GigEVision", Qt::CaseInsensitive))
                caps.connectionInterface = CameraCapabilities::Interface::GigE;
            else if (iface.contains("USB3Vision", Qt::CaseInsensitive))
                caps.connectionInterface = CameraCapabilities::Interface::USB3;
        }

        INodeMap &nodeMap = camera->GetNodeMap();

        // --- Pixel formats ---
        CEnumerationPtr ptrPixelFormat = nodeMap.GetNode("PixelFormat");
        if (IsAvailable(ptrPixelFormat) && IsReadable(ptrPixelFormat)) {
            caps.pixelFormat.kind     = CapabilityKind::Discrete;
            caps.pixelFormat.writable = IsWritable(ptrPixelFormat);

            NodeList_t entries;
            ptrPixelFormat->GetEntries(entries);
            for (INode *entry : entries) {
                CEnumEntryPtr e(entry);
                if (!IsAvailable(e) || !IsReadable(e)) continue;
                QString key = QString::fromStdString(e->GetSymbolic().c_str());
                PixelFormat pf;
                pf.nativeKey = key;
                if      (key == "BayerRG8")                   { pf.encoding = PixelEncoding::BayerRG8;  pf.bitsPerPixel = 8; }
                else if (key == "BayerGB8")                   { pf.encoding = PixelEncoding::BayerGB8;  pf.bitsPerPixel = 8; }
                else if (key == "BayerGR8")                   { pf.encoding = PixelEncoding::BayerGR8;  pf.bitsPerPixel = 8; }
                else if (key == "BayerBG8")                   { pf.encoding = PixelEncoding::BayerBG8;  pf.bitsPerPixel = 8; }
                else if (key == "BayerRG16")                  { pf.encoding = PixelEncoding::BayerRG16; pf.bitsPerPixel = 16; }
                else if (key == "BayerGB16")                  { pf.encoding = PixelEncoding::BayerGB16; pf.bitsPerPixel = 16; }
                else if (key == "Mono8")                      { pf.encoding = PixelEncoding::Mono8;     pf.bitsPerPixel = 8; }
                else if (key == "Mono10")                     { pf.encoding = PixelEncoding::Mono10;    pf.bitsPerPixel = 10; }
                else if (key == "Mono12")                     { pf.encoding = PixelEncoding::Mono12;    pf.bitsPerPixel = 12; }
                else if (key == "Mono16")                     { pf.encoding = PixelEncoding::Mono16;    pf.bitsPerPixel = 16; }
                else if (key == "RGB8Packed" || key == "RGB8"){ pf.encoding = PixelEncoding::RGB8;      pf.bitsPerPixel = 24; }
                else if (key == "BGR8")                       { pf.encoding = PixelEncoding::BGR8;      pf.bitsPerPixel = 24; }
                else                                          { pf.encoding = PixelEncoding::Unknown; }

                bool isDefault = (e->GetValue() == ptrPixelFormat->GetIntValue());
                caps.pixelFormat.supported.append(pf);
                if (isDefault) caps.pixelFormat.defaultFormat = pf;
            }
        }

        // --- Resolution ---
        CIntegerPtr ptrWidth  = nodeMap.GetNode("Width");
        CIntegerPtr ptrHeight = nodeMap.GetNode("Height");
        if (IsAvailable(ptrWidth) && IsReadable(ptrWidth) &&
            IsAvailable(ptrHeight) && IsReadable(ptrHeight)) {
            // Range max must be the TRUE sensor dims (WidthMax/HeightMax):
            // Width's own max is WidthMax - OffsetX, so it under-reports
            // while a crop ROI is programmed — and downstream slot/crop
            // sizing uses the range max as the sensor size.
            CIntegerPtr ptrWMax = nodeMap.GetNode("WidthMax");
            CIntegerPtr ptrHMax = nodeMap.GetNode("HeightMax");
            const int sensorW = (IsAvailable(ptrWMax) && IsReadable(ptrWMax))
                                    ? (int)ptrWMax->GetValue() : (int)ptrWidth->GetMax();
            const int sensorH = (IsAvailable(ptrHMax) && IsReadable(ptrHMax))
                                    ? (int)ptrHMax->GetValue() : (int)ptrHeight->GetMax();
            caps.resolution.kind     = CapabilityKind::Range;
            caps.resolution.writable = IsWritable(ptrWidth);
            caps.resolution.widthRange  = { (int)ptrWidth->GetMin(),  sensorW, (int)ptrWidth->GetInc(),  (int)ptrWidth->GetValue()  };
            caps.resolution.heightRange = { (int)ptrHeight->GetMin(), sensorH, (int)ptrHeight->GetInc(), (int)ptrHeight->GetValue() };
            caps.resolution.defaultResolution = { (int)ptrWidth->GetValue(), (int)ptrHeight->GetValue() };
        }

        // --- Frame rate ---
        // AcquisitionFrameRateEnable defaults to false on FLIR cameras
        // (auto rate), which causes GetMax() to return the exposure-limited
        // rate rather than the hardware maximum. Enable it briefly to read
        // the true hardware max, then restore.
        CBooleanPtr ptrFpsEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
        bool restoredFpsEnable = false;
        if (IsAvailable(ptrFpsEnable) && IsWritable(ptrFpsEnable)
                && !ptrFpsEnable->GetValue()) {
            ptrFpsEnable->SetValue(true);
            restoredFpsEnable = true;
        }
        CFloatPtr ptrFps = nodeMap.GetNode("AcquisitionFrameRate");
        if (IsAvailable(ptrFps) && IsReadable(ptrFps)) {
            caps.frameRate.kind               = CapabilityKind::Range;
            caps.frameRate.readable           = true;
            caps.frameRate.writable           = IsWritable(ptrFps);
            caps.frameRate.range.min          = ptrFps->GetMin();
            caps.frameRate.range.max          = ptrFps->GetMax();
            caps.frameRate.range.step         = 0;
            caps.frameRate.range.defaultValue = ptrFps->GetValue();
        }
        if (restoredFpsEnable && IsAvailable(ptrFpsEnable) && IsWritable(ptrFpsEnable))
            ptrFpsEnable->SetValue(false);

        // --- Exposure time ---
        CFloatPtr ptrExp = nodeMap.GetNode("ExposureTime");
        if (IsAvailable(ptrExp) && IsReadable(ptrExp)) {
            caps.exposureTime.kind               = CapabilityKind::Range;
            caps.exposureTime.readable           = true;
            caps.exposureTime.writable           = IsWritable(ptrExp);
            caps.exposureTime.range.min          = ptrExp->GetMin();
            caps.exposureTime.range.max          = ptrExp->GetMax();
            caps.exposureTime.range.step         = 0;
            caps.exposureTime.range.defaultValue = ptrExp->GetValue();
        }

        // --- Gain ---
        CFloatPtr ptrGain = nodeMap.GetNode("Gain");
        if (IsAvailable(ptrGain) && IsReadable(ptrGain)) {
            caps.gain.kind               = CapabilityKind::Range;
            caps.gain.readable           = true;
            caps.gain.writable           = IsWritable(ptrGain);
            caps.gain.range.min          = ptrGain->GetMin();
            caps.gain.range.max          = ptrGain->GetMax();
            caps.gain.range.step         = 0;
            caps.gain.range.defaultValue = ptrGain->GetValue();
        }

        // --- Exposure mode ---
        CEnumerationPtr ptrExpAuto = nodeMap.GetNode("ExposureAuto");
        if (IsAvailable(ptrExpAuto) && IsReadable(ptrExpAuto)) {
            caps.exposureMode.kind     = CapabilityKind::Discrete;
            caps.exposureMode.readable = true;
            caps.exposureMode.writable = IsWritable(ptrExpAuto);
            NodeList_t entries;
            ptrExpAuto->GetEntries(entries);
            for (INode *e : entries) {
                CEnumEntryPtr entry(e);
                if (!IsAvailable(entry) || !IsReadable(entry)) continue;
                QString key = QString::fromStdString(entry->GetSymbolic().c_str());
                bool isDefault = (entry->GetValue() == ptrExpAuto->GetIntValue());
                caps.exposureMode.options.append({ key, key, isDefault });
            }
        }

        // --- ROI ---
        CIntegerPtr ptrOffX = nodeMap.GetNode("OffsetX");
        CIntegerPtr ptrOffY = nodeMap.GetNode("OffsetY");
        if (IsAvailable(ptrOffX) && IsReadable(ptrOffX)) {
            caps.roi.supported    = true;
            caps.roi.offsetXRange = { (int)ptrOffX->GetMin(), (int)ptrOffX->GetMax(), (int)ptrOffX->GetInc(), 0 };
            caps.roi.offsetYRange = { (int)ptrOffY->GetMin(), (int)ptrOffY->GetMax(), (int)ptrOffY->GetInc(), 0 };
            caps.roi.widthRange   = caps.resolution.widthRange;
            caps.roi.heightRange  = caps.resolution.heightRange;
        }

        // --- Trigger ---
        CEnumerationPtr ptrTrigSrc = nodeMap.GetNode("TriggerSource");
        if (IsAvailable(ptrTrigSrc) && IsReadable(ptrTrigSrc)) {
            caps.trigger.supported      = true;
            caps.trigger.hasTimestamping = true;
            NodeList_t entries;
            ptrTrigSrc->GetEntries(entries);
            for (INode *e : entries) {
                CEnumEntryPtr entry(e);
                if (!IsAvailable(entry) || !IsReadable(entry)) continue;
                QString key = QString::fromStdString(entry->GetSymbolic().c_str());
                if (key.contains("Software", Qt::CaseInsensitive))
                    caps.trigger.sources.append(TriggerSource::Software);
                else if (key.contains("Line", Qt::CaseInsensitive))
                    { caps.trigger.sources.append(TriggerSource::Hardware); caps.trigger.hasHardwareInput = true; }
                else if (key.contains("Action", Qt::CaseInsensitive))
                    caps.trigger.sources.append(TriggerSource::Action);
            }
        }

        // --- Chunk data ---
        CBooleanPtr ptrChunk = nodeMap.GetNode("ChunkModeActive");
        if (IsAvailable(ptrChunk)) {
            auto chunkEnabled = [&nodeMap](const char *name) {
                CBooleanPtr n = nodeMap.GetNode(name);
                return IsAvailable(n);
            };
            caps.chunkData.frameCounter = chunkEnabled("ChunkFrameCounter");
            caps.chunkData.timestamp    = chunkEnabled("ChunkTimestamp");
            caps.chunkData.exposureTime = chunkEnabled("ChunkExposureTime");
            caps.chunkData.gain         = chunkEnabled("ChunkGain");
        }

        // --- Extensions: device temperature ---
        CFloatPtr ptrTemp = nodeMap.GetNode("DeviceTemperature");
        if (IsAvailable(ptrTemp) && IsReadable(ptrTemp))
            caps.extensions["spinnaker.DeviceTemperature"] = ptrTemp->GetValue();
    };

    if (m_camera) {
        CameraPtr *camera = (CameraPtr*)m_camera;
        doQuery(*camera);
    } else {
        try {
            SystemPtr system = System::GetInstance();
            CameraList camList = system->GetCameras();
            if (camList.GetSize() > 0) {
                CameraPtr cam = camList.GetByIndex(0);
                cam->Init();
                doQuery(cam);
                cam->DeInit();
            }
            camList.Clear();
            system->ReleaseInstance();
        } catch (Spinnaker::Exception &e) {
            caps.extensions["spinnaker.queryError"] = QString::fromLocal8Bit(e.what());
        }
    }
#endif

    return caps;
}

void VideoInputSpinnaker::captureLoop()
{
#ifdef HAVE_SPINNAKER
    // Snapshot the CameraPtr once: stop() joins this loop before deleting it,
    // so the pointer stays valid for the loop's whole lifetime.
    CameraPtr *camera = (CameraPtr*)m_camera;
    while (!m_abort && camera) {
        try {
            ImagePtr pResultImage = (*camera)->GetNextImage(1000);
            if (pResultImage->IsIncomplete()) {
                ppDebug() << "[VideoInputSpinnaker] Incomplete image, status"
                         << pResultImage->GetImageStatus();
                pResultImage->Release();
                continue;
            }

            // Read the exposure applied to THIS frame from chunk data (must
            // happen before Release()). Published for both delivery paths.
            double chunkExpUs = 0.0;
            if (m_chunkExposureEnabled) {
                try { chunkExpUs = pResultImage->GetChunkData().GetExposureTime(); } // microseconds
                catch (Spinnaker::Exception &) { chunkExpUs = 0.0; }
            }
            m_lastExposureUs.store(chunkExpUs, std::memory_order_relaxed);
            m_lastExposureAuto.store(m_exposureAuto, std::memory_order_relaxed);

            const size_t width  = pResultImage->GetWidth();
            const size_t height = pResultImage->GetHeight();
            const size_t stride = pResultImage->GetStride();
            const uchar *src    = static_cast<const uchar*>(pResultImage->GetData());

            PixelFormatEnums fmt = pResultImage->GetPixelFormat();
            bool isBayer = (fmt == PixelFormat_BayerRG8 || fmt == PixelFormat_BayerBG8 ||
                            fmt == PixelFormat_BayerGR8 || fmt == PixelFormat_BayerGB8);
            bool isRGB   = (fmt == PixelFormat_RGB8Packed);
            bool isBGR   = (fmt == PixelFormat_BGR8);

            if (isBayer && m_emitRaw) {
                // Hot path: copy raw Bayer bytes once; GPU demosaics on the display thread.
                // Pack rows (remove any stride padding) so the GPU upload path is simple.
                RawVideoFrame rawFrame;
                rawFrame.width       = static_cast<int>(width);
                rawFrame.height      = static_cast<int>(height);
                rawFrame.pattern     = static_cast<RawVideoFrame::BayerPattern>(m_bayerPattern);
                rawFrame.exposureUs  = chunkExpUs;
                rawFrame.exposureAuto = m_exposureAuto;
                rawFrame.data.resize(static_cast<qsizetype>(width * height));
                char *dst = rawFrame.data.data();
                if (stride == width) {
                    memcpy(dst, src, width * height);
                } else {
                    for (size_t y = 0; y < height; ++y)
                        memcpy(dst + y * width, src + y * stride, width);
                }
                pResultImage->Release();

                QMetaObject::invokeMethod(this, [this, rawFrame = std::move(rawFrame)]() mutable {
                    emit rawVideoFrameReady(rawFrame);
                }, Qt::QueuedConnection);

            } else {
                // Pre-decoded path (RGB8Packed, BGR8, Mono8): wrap and emit as QVideoFrame.
                QImage img;
                if (isRGB) {
                    img = QImage(src, static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3, QImage::Format_RGB888).copy();
                } else if (isBGR) {
                    img = QImage(src, static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3, QImage::Format_BGR888).copy();
                } else {
                    // Mono8: use the SDK-reported stride — Spinnaker buffers
                    // may pad rows, and QImage's implicit stride assumes
                    // 32-bit-aligned scanlines of exactly `width` bytes.
                    img = QImage(src, static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(stride), QImage::Format_Grayscale8).copy();
                }
                pResultImage->Release();

                QVideoFrame frame(img);
                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit videoFrameReady(frame);
                }, Qt::QueuedConnection);
            }

        } catch (Spinnaker::Exception &e) {
            // -1012 (SPINNAKER_ERR_ABORT) is the normal consequence of
            // EndAcquisition() being called while GetNextImage() is waiting.
            // Only log it when the abort was unexpected (m_abort is still false).
            if (!m_abort)
                ppError() << "[VideoInputSpinnaker] Capture error:" << e.what();
        }
    }
#endif
}
