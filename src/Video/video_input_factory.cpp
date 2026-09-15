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

#include "video_input_factory.h"
#include "video_input.h"
#include "camera_capabilities.h"
#include <QStringList>
#include <algorithm>
#include "../Core/device_enumerator.h"

#ifdef Q_OS_MACOS
#include "VideoInputApple.h"
#include <QCameraDevice>
#include <QMediaDevices>
#endif

#ifdef HAVE_SPINNAKER
#include "SpinnakerPlatform.h"
#undef SPINNAKER_DEPRECATED_CLASS
#define SPINNAKER_DEPRECATED_CLASS(msg) class SPINNAKER_API __declspec(deprecated(msg))
#include <Spinnaker.h>
#include "VideoInputSpinnaker.h"
#include "spinnaker_runtime.h"
#endif

#ifdef HAVE_PPCP
#include "VideoInputPpcp.h"
#endif

#ifdef HAVE_ARAVIS
#include "VideoInputAravis.h"
// Include Aravis headers last to avoid 'signals' conflict
#undef signals
#include <arv.h>
#define signals public
#endif

#include "pp_debug.h"

#ifdef Q_OS_MACOS
// Mirrors PpcpIngestPolicy::Limits::minCameraRateMhz (ppcp_ingest_policy.h)
// for the same reason and at the same number: below 120 fps the impact frame
// is too far from the true impact for this analysis to recover club-face
// measurements from. That policy rejects a PAIRED PHONE's declared camera
// rate; this is the same floor applied to what macOS lists LOCALLY, because
// nothing about a camera being local makes a 60 fps sensor more useful.
//
// The concrete trigger: macOS lists a tethered/nearby iPhone as an ordinary
// AVFoundation camera the moment Continuity Camera sees it — wired, over
// Wi-Fi, or just unlocked nearby — with no flag distinguishing it from a
// real UVC webcam. There is no reliable way to ask "is this a phone" up
// front (name/id matching is exactly the fragile, easily-renamed check this
// avoids); asking what it can DO is the same question the product already
// answers for a phone paired over PPCP, so this asks it here too. A phone
// capable of more than 60 fps belongs paired over PPCP, where its actual
// rate is declared and can be trusted, not masquerading as a webcam whose
// ceiling this host can never raise.
static constexpr double kMinUsefulCameraFps = 120.0;
#endif

void VideoInputFactory::enumerateDevices()
{
#ifdef Q_OS_MACOS
    // macOS: capabilities come from QCameraDevice — no connection needed.
    for (const QCameraDevice &dev : QMediaDevices::videoInputs()) {
        const CameraCapabilities caps = VideoInput::capabilitiesFor(dev);

        // ⚠ ONLY A CONFIRMED-LOW ceiling is filtered — `> 0.0` first.
        // `frameRate.range.max` reads 0 whenever `videoFormats()` came back
        // empty (capabilitiesFor() never sets CapabilityKind::Range in that
        // case), which is a real state for an UNPROBED camera, not a slow
        // one: format LISTING doesn't need camera permission on macOS, but a
        // camera can still legitimately report nothing the instant it
        // appears. registerDevice() dedupes by id, so a device filtered here
        // gets no second look without an app restart — treating "unknown" as
        // "low" would risk permanently hiding a real, useful camera rather
        // than a Continuity Camera nuisance, which is a worse failure than
        // showing one extra row.
        if (caps.frameRate.range.max > 0.0 && caps.frameRate.range.max < kMinUsefulCameraFps) {
            ppWarn() << "[VideoInputFactory] Filtering" << dev.description()
                     << "— max" << caps.frameRate.range.max << "fps is below the"
                     << kMinUsefulCameraFps << "fps floor this analysis needs";
            continue;
        }

        DeviceEnumerator::instance()->registerDevice(
            DeviceType::VideoInput, Backend::AppleAVFoundation,
            dev.id(), dev.description(), caps);
    }
#else
    // Qt Multimedia: VideoInput::availableDevices() now passes capabilities.
    VideoInput::availableDevices();
#endif

#ifdef HAVE_ARAVIS
    // Aravis: open each device briefly to read its GenICam parameters, then
    // release — capabilities are stored in the Device struct so CameraInstance
    // never needs to re-open a camera just for format discovery.
    arv_update_device_list();
    unsigned int nAravis = arv_get_n_devices();
    for (unsigned int i = 0; i < nAravis; ++i) {
        const char *deviceId = arv_get_device_id(i);
        const char *model    = arv_get_device_model(i);

        CameraCapabilities caps;
        caps.driverVersion = "Aravis GigE/USB3 Vision";
        GError *err = nullptr;
        ArvCamera *cam = arv_camera_new(deviceId, &err);
        if (cam) {
            caps.connectionInterface = arv_camera_is_gv_device(cam)
                ? CameraCapabilities::Interface::GigE
                : CameraCapabilities::Interface::USB3;

            ArvDevice *arvDev = arv_camera_get_device(cam);
            if (arvDev) {
                GError *snErr = nullptr;
                const char *sn = arv_device_get_string_feature_value(arvDev, "DeviceSerialNumber", &snErr);
                if (sn && !snErr) caps.serialNumber = QString::fromLocal8Bit(sn);
                g_clear_error(&snErr);
                GError *vnErr = nullptr;
                const char *vn = arv_device_get_string_feature_value(arvDev, "DeviceVendorName", &vnErr);
                if (vn && !vnErr) caps.vendorName = QString::fromLocal8Bit(vn);
                g_clear_error(&vnErr);
            }
            caps.modelName = QString::fromLocal8Bit(model);

            gint curW = 0, curH = 0;
            arv_camera_get_region(cam, nullptr, nullptr, &curW, &curH, nullptr);
            gint wMin, wMax, hMin, hMax, wInc, hInc;
            arv_camera_get_width_bounds(cam, &wMin, &wMax, nullptr);
            arv_camera_get_height_bounds(cam, &hMin, &hMax, nullptr);
            wInc = arv_camera_get_width_increment(cam, nullptr);
            hInc = arv_camera_get_height_increment(cam, nullptr);
            // Width/Height bounds are offset-dependent (max = WidthMax -
            // OffsetX) and the camera retains the last ROI across
            // connections — prefer the WidthMax/HeightMax features, which
            // always report the full (binned) sensor.
            if (arvDev) {
                GError *mErr = nullptr;
                gint64 wm = arv_device_get_integer_feature_value(arvDev, "WidthMax", &mErr);
                if (!mErr && wm > 0) wMax = (gint)wm;
                g_clear_error(&mErr);
                gint64 hm = arv_device_get_integer_feature_value(arvDev, "HeightMax", &mErr);
                if (!mErr && hm > 0) hMax = (gint)hm;
                g_clear_error(&mErr);
            }
            caps.resolution.kind     = CapabilityKind::Range;
            caps.resolution.writable = true;
            caps.resolution.widthRange  = { wMin, wMax, wInc, curW };
            caps.resolution.heightRange = { hMin, hMax, hInc, curH };
            caps.resolution.defaultResolution = { curW, curH };

            // Hardware ROI via arv_camera_set_region (applied in start()).
            caps.roi.supported    = true;
            caps.roi.widthRange   = caps.resolution.widthRange;
            caps.roi.heightRange  = caps.resolution.heightRange;
            caps.roi.offsetXRange = { 0, wMax, wInc, 0 };
            caps.roi.offsetYRange = { 0, hMax, hInc, 0 };

            // --- Frame rate at full sensor, and at the impact-camera crops ---
            // Mirrors the Spinnaker block below (impact_camera_design.md
            // §10.2): reset to the full sensor first (the camera keeps its
            // last ROI), read the full-frame bounds, then each candidate
            // crop's bounds, and leave the region at full sensor — start()
            // rewrites it. ⚠ Untested on hardware as of 2026-09-15; the
            // Spinnaker path is the measured one.
            if (wMax > 0 && hMax > 0) {
                auto rateAt = [&](gint w, gint h, double &maxOut) -> bool {
                    GError *rErr = nullptr;
                    arv_camera_set_region(cam, 0, 0, w, h, &rErr);
                    if (rErr) { g_clear_error(&rErr); return false; }
                    double fMin = 0.0, fMax = 0.0;
                    arv_camera_get_frame_rate_bounds(cam, &fMin, &fMax, &rErr);
                    if (rErr) { g_clear_error(&rErr); return false; }
                    maxOut = fMax;
                    return fMax > 0.0;
                };
                double fullMax = 0.0;
                if (rateAt(wMax, hMax, fullMax)) {
                    caps.frameRate.kind               = CapabilityKind::Range;
                    caps.frameRate.readable           = true;
                    caps.frameRate.writable           = true;
                    caps.frameRate.range.max          = fullMax;
                    caps.frameRate.range.defaultValue = arv_camera_get_frame_rate(cam, nullptr);
                }
                static const gint kProbe[][2] = {
                    {640, 240}, {640, 320}, {1280, 240}, {320, 240}, {640, 480}
                };
                QVariantList modes;
                for (const auto &m : kProbe) {
                    if (m[0] > wMax || m[1] > hMax) continue;
                    const gint w = std::max(wMin, (m[0] / std::max(1, wInc)) * std::max(1, wInc));
                    const gint h = std::max(hMin, (m[1] / std::max(1, hInc)) * std::max(1, hInc));
                    double fMax = 0.0;
                    if (!rateAt(w, h, fMax)) continue;
                    QVariantMap mode;
                    mode[QStringLiteral("w")]   = w;
                    mode[QStringLiteral("h")]   = h;
                    mode[QStringLiteral("fps")] = fMax;
                    modes.append(mode);
                }
                arv_camera_set_region(cam, 0, 0, wMax, hMax, nullptr);
                caps.extensions[QStringLiteral("impact.modes")] = modes;
            }

            guint nFormats = 0;
            const char **fmts = arv_camera_dup_available_pixel_formats_as_strings(cam, &nFormats, nullptr);
            if (fmts) {
                caps.pixelFormat.kind     = CapabilityKind::Discrete;
                caps.pixelFormat.writable = true;
                const char *curFmt = arv_camera_get_pixel_format_as_string(cam, nullptr);
                for (guint j = 0; j < nFormats; ++j) {
                    PixelFormat pf;
                    pf.nativeKey = QString::fromLocal8Bit(fmts[j]);
                    if      (pf.nativeKey == "BayerRG8")   { pf.encoding = PixelEncoding::BayerRG8;  pf.bitsPerPixel = 8; }
                    else if (pf.nativeKey == "BayerGB8")   { pf.encoding = PixelEncoding::BayerGB8;  pf.bitsPerPixel = 8; }
                    else if (pf.nativeKey == "BayerGR8")   { pf.encoding = PixelEncoding::BayerGR8;  pf.bitsPerPixel = 8; }
                    else if (pf.nativeKey == "BayerBG8")   { pf.encoding = PixelEncoding::BayerBG8;  pf.bitsPerPixel = 8; }
                    else if (pf.nativeKey == "Mono8")      { pf.encoding = PixelEncoding::Mono8;     pf.bitsPerPixel = 8; }
                    else if (pf.nativeKey == "Mono10")     { pf.encoding = PixelEncoding::Mono10;    pf.bitsPerPixel = 10; }
                    else if (pf.nativeKey == "Mono12")     { pf.encoding = PixelEncoding::Mono12;    pf.bitsPerPixel = 12; }
                    else if (pf.nativeKey == "RGB8Packed") { pf.encoding = PixelEncoding::RGB8;      pf.bitsPerPixel = 24; }
                    else if (pf.nativeKey == "BGR8")       { pf.encoding = PixelEncoding::BGR8;      pf.bitsPerPixel = 24; }
                    else                                   { pf.encoding = PixelEncoding::Unknown; }
                    caps.pixelFormat.supported.append(pf);
                    if (curFmt && pf.nativeKey == QString::fromLocal8Bit(curFmt))
                        caps.pixelFormat.defaultFormat = pf;
                }
                g_free(fmts);
            }
            g_object_unref(cam);
        }
        g_clear_error(&err);

        DeviceEnumerator::instance()->registerDevice(
            DeviceType::VideoInput, Backend::Aravis,
            QString::fromLocal8Bit(deviceId),
            QString::fromLocal8Bit(model),
            caps);
    }
#endif

#ifdef HAVE_SPINNAKER
    // Only touch the SDK when it has been discovered and made loadable at runtime;
    // the imports are delay-loaded, so calling in without the DLLs present would crash.
    if (pinpoint::spinnaker::runtimeAvailable()) try {
        Spinnaker::SystemPtr system = Spinnaker::System::GetInstance();
        Spinnaker::CameraList camList = system->GetCameras();
        for (unsigned int i = 0; i < camList.GetSize(); ++i) {
            Spinnaker::CameraPtr cam = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap &tlMap = cam->GetTLDeviceNodeMap();

            auto readTLStr = [&tlMap](const char *name) -> QString {
                Spinnaker::GenApi::CStringPtr n = tlMap.GetNode(name);
                if (Spinnaker::GenApi::IsAvailable(n) && Spinnaker::GenApi::IsReadable(n))
                    return QString::fromStdString(n->GetValue().c_str());
                return {};
            };
            QString id     = readTLStr("DeviceID");
            QString model  = readTLStr("DeviceModelName");
            QString serial = readTLStr("DeviceSerialNumber");
            QString vendor = readTLStr("DeviceVendorName");
            QString iftype = readTLStr("DeviceType");
            if (id.isEmpty())    id    = "Unknown";
            if (model.isEmpty()) model = "Spinnaker Camera";

            // Init gives full GenICam nodemap access without starting acquisition.
            CameraCapabilities caps;
            caps.driverVersion  = "Teledyne Spinnaker SDK";
            caps.serialNumber   = serial;
            caps.vendorName     = vendor;
            caps.modelName      = model;
            if (iftype.contains(QLatin1String("USB3"), Qt::CaseInsensitive))
                caps.connectionInterface = CameraCapabilities::Interface::USB3;
            else if (iftype.contains(QLatin1String("USB2"), Qt::CaseInsensitive))
                caps.connectionInterface = CameraCapabilities::Interface::USB2;
            else if (iftype.contains(QLatin1String("GigE"), Qt::CaseInsensitive))
                caps.connectionInterface = CameraCapabilities::Interface::GigE;
            try {
                cam->Init();
                Spinnaker::GenApi::INodeMap &nodeMap = cam->GetNodeMap();

                using namespace Spinnaker::GenApi;
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
                        if      (key == "BayerRG8")                    { pf.encoding = PixelEncoding::BayerRG8;  pf.bitsPerPixel = 8; }
                        else if (key == "BayerGB8")                    { pf.encoding = PixelEncoding::BayerGB8;  pf.bitsPerPixel = 8; }
                        else if (key == "BayerGR8")                    { pf.encoding = PixelEncoding::BayerGR8;  pf.bitsPerPixel = 8; }
                        else if (key == "BayerBG8")                    { pf.encoding = PixelEncoding::BayerBG8;  pf.bitsPerPixel = 8; }
                        else if (key == "BayerRG16")                   { pf.encoding = PixelEncoding::BayerRG16; pf.bitsPerPixel = 16; }
                        else if (key == "BayerGB16")                   { pf.encoding = PixelEncoding::BayerGB16; pf.bitsPerPixel = 16; }
                        else if (key == "Mono8")                       { pf.encoding = PixelEncoding::Mono8;     pf.bitsPerPixel = 8; }
                        else if (key == "Mono10")                      { pf.encoding = PixelEncoding::Mono10;    pf.bitsPerPixel = 10; }
                        else if (key == "Mono12")                      { pf.encoding = PixelEncoding::Mono12;    pf.bitsPerPixel = 12; }
                        else if (key == "Mono16")                      { pf.encoding = PixelEncoding::Mono16;    pf.bitsPerPixel = 16; }
                        else if (key == "RGB8Packed" || key == "RGB8") { pf.encoding = PixelEncoding::RGB8;      pf.bitsPerPixel = 24; }
                        else if (key == "BGR8")                        { pf.encoding = PixelEncoding::BGR8;      pf.bitsPerPixel = 24; }
                        else                                           { pf.encoding = PixelEncoding::Unknown; }
                        bool isDefault = (e->GetValue() == ptrPixelFormat->GetIntValue());
                        caps.pixelFormat.supported.append(pf);
                        if (isDefault) caps.pixelFormat.defaultFormat = pf;
                    }
                }

                CIntegerPtr ptrW = nodeMap.GetNode("Width");
                CIntegerPtr ptrH = nodeMap.GetNode("Height");
                if (IsAvailable(ptrW) && IsReadable(ptrW) &&
                    IsAvailable(ptrH) && IsReadable(ptrH)) {
                    // Range max must be the TRUE sensor dims (WidthMax/
                    // HeightMax): Width's own max is WidthMax - OffsetX, so
                    // it under-reports while a crop ROI is still programmed
                    // in the camera (ROI nodes are retained across app runs
                    // while the camera stays powered). CameraInstance sizes
                    // ring slots and the expected crop from this range max —
                    // a stale value here compounds the crop every connect.
                    CIntegerPtr ptrWMax = nodeMap.GetNode("WidthMax");
                    CIntegerPtr ptrHMax = nodeMap.GetNode("HeightMax");
                    const int sensorW = (IsAvailable(ptrWMax) && IsReadable(ptrWMax))
                                            ? (int)ptrWMax->GetValue() : (int)ptrW->GetMax();
                    const int sensorH = (IsAvailable(ptrHMax) && IsReadable(ptrHMax))
                                            ? (int)ptrHMax->GetValue() : (int)ptrH->GetMax();
                    caps.resolution.kind     = CapabilityKind::Range;
                    caps.resolution.writable = IsWritable(ptrW);
                    caps.resolution.widthRange  = { (int)ptrW->GetMin(), sensorW, (int)ptrW->GetInc(), (int)ptrW->GetValue() };
                    caps.resolution.heightRange = { (int)ptrH->GetMin(), sensorH, (int)ptrH->GetInc(), (int)ptrH->GetValue() };
                    caps.resolution.defaultResolution = { (int)ptrW->GetValue(), (int)ptrH->GetValue() };
                }

                // --- Frame rate ---
                // AcquisitionFrameRateEnable defaults to false on FLIR cameras
                // (auto rate), which causes GetMax() to return the
                // exposure-limited rate rather than the hardware maximum.
                // Enable it briefly to read the true hardware max, then restore.
                // ⚠ Two spellings: Blackfly S firmware has the SFNC
                // AcquisitionFrameRateEnable; Chameleon3 / legacy FLIR firmware
                // has AcquisitionFrameRateEnabled + AcquisitionFrameRateAuto
                // and no node of the SFNC name at all (checked on the studio
                // CM3-U3-13Y3C, fw 1.13.3.00, 2026-09-15). Touch whichever exists.
                CBooleanPtr ptrFpsEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
                if (!IsAvailable(ptrFpsEnable))
                    ptrFpsEnable = nodeMap.GetNode("AcquisitionFrameRateEnabled");
                bool restoredFpsEnable = false;
                if (IsAvailable(ptrFpsEnable) && IsWritable(ptrFpsEnable)
                        && !ptrFpsEnable->GetValue()) {
                    ptrFpsEnable->SetValue(true);
                    restoredFpsEnable = true;
                }

                // ⚠ The rate's maximum ALSO depends on the ROI, and the camera
                // keeps its ROI across app runs: after an impact session a
                // 240-row crop is still programmed and the "full-frame" max
                // would read 611.7, not 150.7. Reset to the full sensor before
                // reading, and leave it there — start() rewrites the ROI anyway.
                // AND a Width/Height write does not invalidate the rate node's
                // cached maximum, so the first read after it is one ROI stale;
                // InvalidateNodes() after the write is what refreshes it. Both
                // measured on the studio Chameleon3s, 2026-09-15
                // (impact_camera_design.md §3.1).
                CIntegerPtr ptrOX  = nodeMap.GetNode("OffsetX");
                CIntegerPtr ptrOY  = nodeMap.GetNode("OffsetY");
                const bool roiWritable = IsAvailable(ptrW) && IsWritable(ptrW)
                                      && IsAvailable(ptrH) && IsWritable(ptrH);
                const int64_t sensorW = caps.resolution.widthRange.max;
                const int64_t sensorH = caps.resolution.heightRange.max;
                auto setRegion = [&](int64_t w, int64_t h) {
                    // Offsets to minimum first so a stale offset never clamps
                    // the size; snapped down to the node increments.
                    if (IsAvailable(ptrOX) && IsWritable(ptrOX)) ptrOX->SetValue(ptrOX->GetMin());
                    if (IsAvailable(ptrOY) && IsWritable(ptrOY)) ptrOY->SetValue(ptrOY->GetMin());
                    auto snap = [](int64_t v, int64_t inc, int64_t lo, int64_t hi) {
                        inc = inc > 0 ? inc : 1;
                        v   = v < lo ? lo : (v > hi ? hi : v);
                        return lo + ((v - lo) / inc) * inc;
                    };
                    ptrW->SetValue(snap(w, ptrW->GetInc(), ptrW->GetMin(), ptrW->GetMax()));
                    ptrH->SetValue(snap(h, ptrH->GetInc(), ptrH->GetMin(), ptrH->GetMax()));
                    nodeMap.InvalidateNodes();   // the rate's max is cached across this write
                };
                if (roiWritable && sensorW > 0) {
                    try { setRegion(sensorW, sensorH); } catch (...) {}
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

                // --- Impact-camera crops (impact_camera_design.md §10.2) ---
                // A row-readout sensor gets faster as the crop gets shorter, and
                // only the camera knows by how much: 591 fps at 240 rows on a
                // Chameleon3 whose full frame is 150. Probe the candidate impact
                // crops here (nodes only, no acquisition) and publish each one's
                // advertised maximum so Settings offers modes with real numbers.
                // Recommended order first (640×240 — §10.2); CameraManager drops
                // the ones under 420 fps. The advertised figure runs 1–3.5%
                // above delivered off the firmware cap (§3.1).
                if (roiWritable && sensorW > 0 && IsAvailable(ptrFps) && IsReadable(ptrFps)) {
                    static const int kProbe[][2] = {
                        {640, 240}, {640, 320}, {1280, 240}, {320, 240}, {640, 480}
                    };
                    QVariantList modes;
                    for (const auto &m : kProbe) {
                        if (m[0] > sensorW || m[1] > sensorH) continue;
                        try {
                            setRegion(m[0], m[1]);
                            QVariantMap mode;
                            mode[QStringLiteral("w")]   = int(ptrW->GetValue());
                            mode[QStringLiteral("h")]   = int(ptrH->GetValue());
                            mode[QStringLiteral("fps")] = ptrFps->GetMax();
                            modes.append(mode);
                        } catch (...) {}
                    }
                    try { setRegion(sensorW, sensorH); } catch (...) {}
                    caps.extensions[QStringLiteral("impact.modes")] = modes;
                    QStringList summary;
                    for (const QVariant &v : modes) {
                        const QVariantMap m = v.toMap();
                        summary << QStringLiteral("%1x%2@%3")
                                       .arg(m.value(QStringLiteral("w")).toInt())
                                       .arg(m.value(QStringLiteral("h")).toInt())
                                       .arg(m.value(QStringLiteral("fps")).toDouble(), 0, 'f', 1);
                    }
                    ppInfo() << "[VideoInputFactory] Spinnaker" << model << serial
                             << "full-frame max" << caps.frameRate.range.max
                             << "fps; impact crops:" << summary.join(QLatin1String(", "));
                }

                if (restoredFpsEnable && IsAvailable(ptrFpsEnable) && IsWritable(ptrFpsEnable))
                    ptrFpsEnable->SetValue(false);

                cam->DeInit();
            } catch (...) {}

            DeviceEnumerator::instance()->registerDevice(
                DeviceType::VideoInput, Backend::Spinnaker, id, model, caps);
        }
        camList.Clear();
        system->ReleaseInstance();
    } catch (...) {}
#endif
}

VideoInputBase* VideoInputFactory::create(Backend backend, QObject *parent)
{
    // Enumerate only when the registry is empty (first use / tests). A full
    // re-enumeration briefly OPENS every Aravis device and Init()/DeInit()s
    // every Spinnaker camera — including ones live-streaming under another
    // CameraInstance in this process (the settings crop-preview creates
    // instances mid-session) — and blocks the calling thread on GenICam node
    // reads. CameraManager already enumerates at startup; rescans are
    // explicit user actions.
    bool haveVideoDevices = false;
    for (const auto &dev : DeviceEnumerator::instance()->devices())
        if (dev.type == DeviceType::VideoInput) { haveVideoDevices = true; break; }
    if (!haveVideoDevices)
        enumerateDevices();

    if (backend == Backend::Auto) {
#ifdef HAVE_SPINNAKER
        if (pinpoint::spinnaker::runtimeAvailable()) {
            for (const auto &dev : DeviceEnumerator::instance()->devices()) {
                if (dev.backend == Backend::Spinnaker) {
                    ppInfo() << "[VideoInputFactory] Spinnaker camera detected; selecting Spinnaker backend.";
                    return new VideoInputSpinnaker(parent);
                }
            }
        }
#endif

#ifdef HAVE_ARAVIS
        {
            bool hasAravis = false;
            for (const auto &dev : DeviceEnumerator::instance()->devices())
                if (dev.backend == Backend::Aravis) { hasAravis = true; break; }
            if (hasAravis) {
                ppInfo() << "[VideoInputFactory] Industrial camera(s) detected; selecting Aravis backend.";
                return new VideoInputAravis(parent);
            }
        }
#endif

#ifdef Q_OS_MACOS
        ppInfo() << "[VideoInputFactory] Selecting native Apple AVFoundation backend.";
        return new VideoInputApple(parent);
#endif

        ppInfo() << "[VideoInputFactory] Selecting Qt Multimedia backend.";
        return new VideoInput(parent);
    }

    switch (backend) {
        case Backend::QtMultimedia:
            return new VideoInput(parent);
#ifdef Q_OS_MACOS
        case Backend::AppleAVFoundation:
            return new VideoInputApple(parent);
#endif
#ifdef HAVE_ARAVIS
        case Backend::Aravis:
            return new VideoInputAravis(parent);
#endif
#ifdef HAVE_SPINNAKER
        case Backend::Spinnaker:
            if (pinpoint::spinnaker::runtimeAvailable())
                return new VideoInputSpinnaker(parent);
            ppWarn() << "[VideoInputFactory] Spinnaker backend requested but SDK not found; falling back to Qt Multimedia.";
            return new VideoInput(parent);
#endif
#ifdef HAVE_PPCP
        // ⚠ NO FALLBACK HERE, DELIBERATELY. Every other backend falls back to
        // Qt Multimedia when its SDK is missing, because they are all ways of
        // reaching a camera plugged into THIS machine. A PPCP camera is a
        // camera on another device; substituting a local one would hand the
        // caller a different camera under the same id.
        case Backend::Ppcp:
            return new VideoInputPpcp(parent);
#endif
        default:
            ppWarn() << "[VideoInputFactory] Requested backend not available on this platform; falling back to Qt Multimedia.";
            return new VideoInput(parent);
    }
}

VideoInputFactory::Backend VideoInputFactory::backendType(VideoInputBase *input)
{
    if (!input) return Backend::QtMultimedia;
#ifdef HAVE_PPCP
    if (dynamic_cast<VideoInputPpcp*>(input)) return Backend::Ppcp;
#endif
#ifdef HAVE_SPINNAKER
    if (dynamic_cast<VideoInputSpinnaker*>(input)) return Backend::Spinnaker;
#endif
#ifdef HAVE_ARAVIS
    if (dynamic_cast<VideoInputAravis*>(input)) return Backend::Aravis;
#endif
#ifdef Q_OS_MACOS
    if (dynamic_cast<VideoInputApple*>(input)) return Backend::AppleAVFoundation;
#endif
    return Backend::QtMultimedia;
}

QList<VideoInputFactory::Backend> VideoInputFactory::availableBackends()
{
    QList<Backend> list;
    list << Backend::QtMultimedia;
#ifdef HAVE_PPCP
    // Always available where the library is linked: whether a PPCP camera
    // EXISTS is a question about who is connected, not about this build.
    list << Backend::Ppcp;
#endif
#ifdef Q_OS_MACOS
    list << Backend::AppleAVFoundation;
#endif
#ifdef HAVE_ARAVIS
    list << Backend::Aravis;
#endif
#ifdef HAVE_SPINNAKER
    if (pinpoint::spinnaker::runtimeAvailable())
        list << Backend::Spinnaker;
#endif
    return list;
}

#ifdef HAVE_PPCP
int VideoInputFactory::registerPpcpPeer(const struct ppcp_peer_desc *peer)
{
    // One line, and it is a forward: VideoInputPpcp_inventory.cpp is the only
    // translation unit in src/Video that touches DeviceEnumerator on the PPCP
    // path, so that the capability mapping itself stays a pure function of a
    // declaration and testable with no registry in the link.
    return VideoInputPpcp::registerSources(peer);
}
#endif
