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

#include "spinnaker_settings.h"

#ifdef HAVE_SPINNAKER
#include <algorithm>
#include <cmath>

using namespace Spinnaker;
using namespace Spinnaker::GenApi;

namespace pinpoint::spinnaker {

// The caller passes an empty rect for "no crop" (VideoInputSpinnaker maps
// pp_crop::cropIsActive onto that), so the module needs no frame_crop.h.
static bool cropActive(const QRectF &crop) { return !crop.isEmpty(); }

// Apply the normalized crop region (or full sensor when inactive) via the
// GenICam Width/Height/OffsetX/OffsetY nodes. Must run before
// BeginAcquisition() — the nodes are read-only while streaming. Values are
// snapped DOWN to the node increments so a delivered frame never exceeds the
// ring slot sized from the same (ceil'd) crop fraction upstream; FLIR
// increments are even on Bayer sensors, so the CFA phase is preserved.
// On failure the full frame is restored best-effort and the software crop in
// CameraInstance engages (frames arrive larger than the expected crop size).
void applyRoi(INodeMap &nodeMap, const QRectF &crop, SettingsLog &out)
{
    try {
        CIntegerPtr w  = nodeMap.GetNode("Width");
        CIntegerPtr h  = nodeMap.GetNode("Height");
        CIntegerPtr ox = nodeMap.GetNode("OffsetX");
        CIntegerPtr oy = nodeMap.GetNode("OffsetY");
        if (!IsAvailable(w) || !IsWritable(w) || !IsAvailable(h) || !IsWritable(h)) {
            if (cropActive(crop))
                out.warn << QStringLiteral("Width/Height nodes not writable; software crop will engage");
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
        if (cropActive(crop)) {
            rw = snapDown(int64_t(std::llround(crop.width()  * sensorW)),
                          w->GetInc(), w->GetMin(), sensorW);
            rh = snapDown(int64_t(std::llround(crop.height() * sensorH)),
                          h->GetInc(), h->GetMin(), sensorH);
        }
        // GenICam ordering: shrink Width/Height FIRST, then move the offsets
        // (their GetMax() grows to sensor - size once the size is reduced).
        w->SetValue(rw);
        h->SetValue(rh);
        if (cropActive(crop)) {
            if (IsAvailable(ox) && IsWritable(ox))
                ox->SetValue(snapDown(int64_t(std::llround(crop.x() * sensorW)),
                                      ox->GetInc(), ox->GetMin(), ox->GetMax()));
            if (IsAvailable(oy) && IsWritable(oy))
                oy->SetValue(snapDown(int64_t(std::llround(crop.y() * sensorH)),
                                      oy->GetInc(), oy->GetMin(), oy->GetMax()));
            out.info << QStringLiteral("ROI applied: %1 x %2").arg(qlonglong(rw)).arg(qlonglong(rh));
        }
    } catch (Spinnaker::Exception &e) {
        out.warn << QStringLiteral("ROI set failed: %1 - full frame; software crop will engage")
                        .arg(QString::fromLocal8Bit(e.what()));
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

// Guarded single-node writes. Each is a silent no-op (returns false) when the
// node or entry is missing, as on a firmware that spells it differently
// (flir_camera_settings.md §4).
static bool setEnumEntry(INodeMap &nodeMap, const char *node, const char *entry)
{
    CEnumerationPtr ptr = nodeMap.GetNode(node);
    if (!IsAvailable(ptr) || !IsWritable(ptr))
        return false;
    CEnumEntryPtr e = ptr->GetEntryByName(entry);
    if (!IsAvailable(e) || !IsReadable(e))
        return false;
    ptr->SetIntValue(e->GetValue());
    return true;
}

static bool setBoolNode(INodeMap &nodeMap, const char *node, bool value)
{
    CBooleanPtr ptr = nodeMap.GetNode(node);
    if (!IsAvailable(ptr) || !IsWritable(ptr))
        return false;
    ptr->SetValue(value);
    return true;
}

static bool setFloatNode(INodeMap &nodeMap, const char *node, double value, QStringList &log,
                         const char *unit)
{
    CFloatPtr ptr = nodeMap.GetNode(node);
    if (!IsAvailable(ptr) || !IsWritable(ptr))
        return false;
    ptr->SetValue(std::clamp(value, ptr->GetMin(), ptr->GetMax()));
    log << QStringLiteral("%1=%2%3").arg(QLatin1String(node)).arg(ptr->GetValue(), 0, 'f', 1)
                                     .arg(QLatin1String(unit));
    return true;
}

// Writes EVERY tuning node PPS owns, on every connect, from the request alone:
// a FLIR camera keeps node values while it has power — across DeInit, a PPS
// restart, another application — so a node not written here is a node the
// last owner chose (flir_camera_settings.md §1, §5).
//
// ⚠ ORDER: ROI (applyRoi) → exposure → gain → gamma → Line1 → rate → auto
// exposure limit, with InvalidateNodes() between. The rate's maximum depends
// on the ROI but a Width/Height write does not invalidate its cache;
// ExposureTime is read-only until ExposureAuto is Off and its access mode is
// cached the same way; a rate the exposure cannot fit clamps a locked
// exposure down silently, so the exposure goes first; and the auto-exposure
// limit is the frame period, so it goes after the rate. Measured on both
// studio Chameleon3s, 2026-09-15 (impact_camera_design.md §3.1).
//
// ⚠ The rate is never auto. On the Chameleon3 an auto rate lets auto exposure
// stretch the frame period: 19 fps on 2026-10-08, where the camera runs 150.7.
ConnectApplied applyConnectSettings(INodeMap &nodeMap, const ConnectSettings &s)
{
    ConnectApplied applied;
    SettingsLog &out = applied.log;
    QStringList log;
    const bool lockedExposure = s.exposureUs > 0.0;
    const bool lockedGain     = s.gainDb >= 0.0;

    // Exposure: locked value, or auto (its limit is set after the rate).
    setEnumEntry(nodeMap, "ExposureAuto", lockedExposure ? "Off" : "Continuous");
    nodeMap.InvalidateNodes();
    if (lockedExposure) {
        if (!setFloatNode(nodeMap, "ExposureTime", s.exposureUs, log, "us"))
            out.warn << QStringLiteral("ExposureTime not writable; exposure not locked");
    } else {
        log << QStringLiteral("ExposureAuto=Continuous");
    }

    // Gain: locked value, or auto over the sensor's whole range, so a dim
    // scene gets gain rather than a longer exposure.
    setEnumEntry(nodeMap, "GainAuto", lockedGain ? "Off" : "Continuous");
    nodeMap.InvalidateNodes();
    if (lockedGain) {
        CFloatPtr ptrGain = nodeMap.GetNode("Gain");
        if (IsAvailable(ptrGain) && IsWritable(ptrGain)) {
            ptrGain->SetValue(std::clamp(s.gainDb, ptrGain->GetMin(), ptrGain->GetMax()));
            applied.gainDb = ptrGain->GetValue();
            log << QStringLiteral("Gain=%1dB").arg(applied.gainDb, 0, 'f', 1);
        } else {
            out.warn << QStringLiteral("Gain not writable; gain not locked");
        }
    } else {
        log << QStringLiteral("GainAuto=Continuous");
        for (const char *name : { "AutoGainUpperLimit", "AutoExposureGainUpperLimit" }) {
            CFloatPtr lim = nodeMap.GetNode(name);
            if (IsAvailable(lim) && IsWritable(lim))
                setFloatNode(nodeMap, name, lim->GetMax(), log, "dB");
        }
    }

    // Gamma: on at the value, or off; under either enable spelling. Absent
    // altogether on the studio Chameleon3s in raw Bayer (2026-10-08 log).
    {
        bool any = false;
        for (const char *name : { "GammaEnable", "GammaEnabled" })
            any |= setBoolNode(nodeMap, name, s.gamma > 0.0);
        nodeMap.InvalidateNodes();
        CFloatPtr ptrGamma = nodeMap.GetNode("Gamma");
        if (IsAvailable(ptrGamma) && IsWritable(ptrGamma)) {
            // With no writable enable, a linear curve is "off".
            ptrGamma->SetValue(std::clamp(s.gamma > 0.0 ? s.gamma : 1.0,
                                          ptrGamma->GetMin(), ptrGamma->GetMax()));
            if (s.gamma > 0.0)
                applied.gamma = ptrGamma->GetValue();
            any = true;
        }
        log << (!any ? QStringLiteral("Gamma=absent")
                     : s.gamma > 0.0 ? QStringLiteral("Gamma=%1").arg(applied.gamma, 0, 'f', 2)
                                     : QStringLiteral("Gamma=off"));
        if (!any && s.gamma > 0.0)
            out.warn << QStringLiteral("Gamma requested but this camera has no gamma node in this pixel format");
    }

    // Line1: ExposureActive for an LED strobe driver, or released. Line1 is
    // output-only on the Chameleon3, whose LineSource has no Off (entries
    // ExposureActive, ExternalTriggerActive, UserOutput1 — 2026-10-08 log), so
    // there "released" is a user output held low.
    if (setEnumEntry(nodeMap, "LineSelector", "Line1")) {
        nodeMap.InvalidateNodes();
        if (s.strobe) {
            setEnumEntry(nodeMap, "LineMode", "Output");   // output-only lines have no mode to set
            nodeMap.InvalidateNodes();
            if (setEnumEntry(nodeMap, "LineSource", "ExposureActive"))
                log << QStringLiteral("Line1=strobe");
            else
                out.warn << QStringLiteral("Strobe requested but LineSource could not be set to "
                                           "ExposureActive; no strobe output");
        } else if (setEnumEntry(nodeMap, "LineMode", "Input")) {
            log << QStringLiteral("Line1=Input");
        } else if (setEnumEntry(nodeMap, "LineSource", "Off")) {
            log << QStringLiteral("Line1=Off");
        } else if (setEnumEntry(nodeMap, "LineSource", "UserOutput1")) {
            nodeMap.InvalidateNodes();
            if (setEnumEntry(nodeMap, "UserOutputSelector", "UserOutput1"))
                setBoolNode(nodeMap, "UserOutputValue", false);
            log << QStringLiteral("Line1=UserOutput1(low)");
        } else {
            out.warn << QStringLiteral("Line1 could not be released");
        }
    } else if (s.strobe) {
        out.warn << QStringLiteral("Strobe requested but this camera has no Line1");
    }

    // Rate: manual, under either firmware's spelling — SFNC
    // AcquisitionFrameRateEnable (Blackfly S), or legacy
    // AcquisitionFrameRateAuto=Off + AcquisitionFrameRateEnabled (Chameleon3,
    // which has no node of the SFNC name) — at the request or this ROI's max.
    // ⚠ Enable FIRST: on the Chameleon3 AcquisitionFrameRateAuto is read-only
    // while AcquisitionFrameRateEnabled is false, so a camera another
    // application left on auto rate (Enabled false, Auto Continuous) kept its
    // auto rate when Auto was written first (spinnaker_settings_probe,
    // 2026-10-08, "dirty" step).
    for (const char *name : { "AcquisitionFrameRateEnable", "AcquisitionFrameRateEnabled" })
        setBoolNode(nodeMap, name, true);
    nodeMap.InvalidateNodes();
    setEnumEntry(nodeMap, "AcquisitionFrameRateAuto", "Off");
    nodeMap.InvalidateNodes();
    double fps = 0.0;
    CFloatPtr ptrFps = nodeMap.GetNode("AcquisitionFrameRate");
    if (IsAvailable(ptrFps) && IsWritable(ptrFps)) {
        const double maxFps = ptrFps->GetMax();
        ptrFps->SetValue(s.fps > 0.0 ? std::clamp(s.fps, ptrFps->GetMin(), maxFps) : maxFps);
        fps = ptrFps->GetValue();
        log << QStringLiteral("FrameRate=%1fps(%2, max %3)")
                   .arg(fps, 0, 'f', 1)
                   .arg(s.fps > 0.0 ? QStringLiteral("requested %1").arg(s.fps, 0, 'f', 1)
                                    : QStringLiteral("max"))
                   .arg(maxFps, 0, 'f', 1);
    } else {
        out.warn << QStringLiteral("AcquisitionFrameRate not writable; rate stays at the camera's own");
    }

    // Auto exposure may use the whole frame period and no more.
    if (!lockedExposure && fps > 0.0) {
        for (const char *name : { "AutoExposureTimeUpperLimit", "AutoExposureExposureTimeUpperLimit" })
            setFloatNode(nodeMap, name, 1e6 / fps, log, "us");
    }
    nodeMap.InvalidateNodes();

    out.info << QStringLiteral("settings: %1 %2")
                    .arg(lockedExposure ? QStringLiteral("impact locked") : QStringLiteral("camera auto"),
                         log.join(QLatin1Char(' ')));
    return applied;
}

} // namespace pinpoint::spinnaker
#endif // HAVE_SPINNAKER
