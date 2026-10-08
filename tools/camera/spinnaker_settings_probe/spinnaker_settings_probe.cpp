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

// spinnaker_settings_probe — drives the real FLIR cameras through role changes
// WITHOUT a power cycle, using the app's own settings writer
// (src/Video/spinnaker_settings.cpp), and measures what each connect delivers:
// frame rate from the camera's timestamps, per-frame exposure from chunk data,
// gain, picture brightness, and a read-back of every node PPS owns.
// docs/design/flir_camera_settings.md §7.
//
//   spinnaker_settings_probe [--serial N] [--seconds S] [--impact-fps F]
//                            [--impact-exposure US] [--impact-gain DB]
//                            [--impact-gamma G] [--strobe] [--no-factory]
//
// The PPS app must not be running: a camera has one owner.

#include "spinnaker_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace Spinnaker;
using namespace Spinnaker::GenApi;
namespace ps = pinpoint::spinnaker;

namespace {

struct Options {
    QString serial;
    double  seconds        = 3.0;
    double  impactFps      = 691.0;   // the cabin's saved rate; clamps to the crop's max
    double  impactExposure = 70.0;
    double  impactGain     = 12.0;
    double  impactGamma    = 0.7;
    bool    strobe         = false;
    bool    factory        = true;
};

// Every node PPS owns, read back after a connect.
std::map<std::string, std::string> readNodes(INodeMap &nm)
{
    std::map<std::string, std::string> out;
    auto rd = [&](const char *name) {
        CNodePtr n = nm.GetNode(name);
        if (!IsAvailable(n)) { out[name] = "absent"; return; }
        if (!IsReadable(n))  { out[name] = "unreadable"; return; }
        CValuePtr v(n);
        out[name] = v->ToString().c_str();
    };
    for (const char *n : { "Width", "Height", "OffsetX", "OffsetY", "PixelFormat",
                           "ExposureAuto", "ExposureTime", "AutoExposureTimeUpperLimit",
                           "AutoExposureExposureTimeUpperLimit",
                           "GainAuto", "Gain", "AutoGainUpperLimit", "AutoExposureGainUpperLimit",
                           "GammaEnable", "GammaEnabled", "Gamma",
                           "AcquisitionFrameRateAuto", "AcquisitionFrameRateEnable",
                           "AcquisitionFrameRateEnabled", "AcquisitionFrameRate" })
        rd(n);
    // Line1's mode and source are selector-dependent.
    CEnumerationPtr sel = nm.GetNode("LineSelector");
    if (IsAvailable(sel) && IsWritable(sel)) {
        CEnumEntryPtr l1 = sel->GetEntryByName("Line1");
        if (IsAvailable(l1) && IsReadable(l1)) {
            sel->SetIntValue(l1->GetValue());
            nm.InvalidateNodes();
            rd("LineMode");
            rd("LineSource");
            out["Line1.LineMode"]   = out["LineMode"];
            out["Line1.LineSource"] = out["LineSource"];
            out.erase("LineMode");
            out.erase("LineSource");
        }
    }
    CEnumerationPtr usel = nm.GetNode("UserOutputSelector");
    if (IsAvailable(usel) && IsWritable(usel)) {
        CEnumEntryPtr u1 = usel->GetEntryByName("UserOutput1");
        if (IsAvailable(u1) && IsReadable(u1)) {
            usel->SetIntValue(u1->GetValue());
            nm.InvalidateNodes();
            rd("UserOutputValue");
            out["UserOutput1.Value"] = out["UserOutputValue"];
            out.erase("UserOutputValue");
        }
    }
    return out;
}

bool setEnum(INodeMap &nm, const char *node, const char *entry)
{
    CEnumerationPtr p = nm.GetNode(node);
    if (!IsAvailable(p) || !IsWritable(p)) return false;
    CEnumEntryPtr e = p->GetEntryByName(entry);
    if (!IsAvailable(e) || !IsReadable(e)) return false;
    p->SetIntValue(e->GetValue());
    return true;
}

// The parts of VideoInputSpinnaker::start() that are not tuning: acquisition
// mode, pixel format (same priority as the app), chunk exposure, buffers.
void prepareLikeTheApp(CameraPtr cam)
{
    INodeMap &nm = cam->GetNodeMap();
    setEnum(nm, "AcquisitionMode", "Continuous");
    for (const char *f : { "BayerRG8", "BayerBG8", "BayerGR8", "BayerGB8", "BGR8", "RGB8Packed", "Mono8" })
        if (setEnum(nm, "PixelFormat", f)) break;
    CBooleanPtr chunk = nm.GetNode("ChunkModeActive");
    if (IsAvailable(chunk) && IsWritable(chunk)) {
        chunk->SetValue(true);
        if (setEnum(nm, "ChunkSelector", "ExposureTime")) {
            CBooleanPtr en = nm.GetNode("ChunkEnable");
            if (IsAvailable(en) && IsWritable(en)) en->SetValue(true);
        }
    }
    INodeMap &sm = cam->GetTLStreamNodeMap();
    setEnum(sm, "StreamBufferCountMode", "Manual");
    CIntegerPtr cnt = sm.GetNode("StreamBufferCountManual");
    if (IsAvailable(cnt) && IsWritable(cnt))
        cnt->SetValue(std::clamp<int64_t>(40, cnt->GetMin(), cnt->GetMax()));
}

struct Measure {
    int    frames = 0, incomplete = 0;
    double fps = 0, expMedUs = 0, expMinUs = 0, expMaxUs = 0;
    double mean = 0, satPct = 0, blackPct = 0;
    double gainDb = -1;
    int    width = 0, height = 0;
};

// --snapshot: the last frame of each connect as a half-resolution colour PNG
// (each 2x2 RGGB cell -> one pixel, no white balance), to compare by eye with
// a frame from a recording.
QString g_snapshotDir;
QString g_snapshotPath;
QString g_serial;
void snapshotFor(const char *step)
{
    g_snapshotPath = g_snapshotDir.isEmpty() ? QString()
                   : QStringLiteral("%1/%2_%3.png").arg(g_snapshotDir, g_serial, QLatin1String(step));
}

void saveSnapshot(ImagePtr img, const QString &path)
{
    const int w = int(img->GetWidth()) / 2, h = int(img->GetHeight()) / 2;
    const auto *p = static_cast<const uint8_t *>(img->GetData());
    const size_t stride = img->GetStride();
    QImage out(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        const uint8_t *r0 = p + size_t(2 * y) * stride, *r1 = r0 + stride;
        uchar *o = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            o[3 * x + 0] = r0[2 * x];                                   // R
            o[3 * x + 1] = uchar((int(r0[2 * x + 1]) + r1[2 * x]) / 2); // G
            o[3 * x + 2] = r1[2 * x + 1];                               // B
        }
    }
    out.save(path);
}

// Streams for `seconds`, discards the first second (auto loops settling), and
// measures the rest.
Measure stream(CameraPtr cam, double seconds)
{
    Measure m;
    INodeMap &nm = cam->GetNodeMap();
    cam->BeginAcquisition();
    std::vector<double> exps;
    std::vector<uint64_t> ts;
    double sum = 0; uint64_t n = 0, sat = 0, black = 0;
    const uint64_t settleNs = 1000000000ull;
    uint64_t t0 = 0, tEnd = uint64_t(seconds * 1e9);
    for (;;) {
        ImagePtr img;
        try { img = cam->GetNextImage(2000); } catch (Spinnaker::Exception &) { break; }
        if (img->IsIncomplete()) { ++m.incomplete; img->Release(); continue; }
        const uint64_t t = img->GetTimeStamp();
        if (!t0) t0 = t;
        const uint64_t el = t - t0;
        if (el >= settleNs) {
            ++m.frames;
            ts.push_back(t);
            try { exps.push_back(img->GetChunkData().GetExposureTime()); } catch (...) {}
            m.width = int(img->GetWidth()); m.height = int(img->GetHeight());
            // Every 4th row: plenty for a mean, cheap at 600 fps.
            const auto *p = static_cast<const uint8_t *>(img->GetData());
            const size_t stride = img->GetStride();
            for (size_t y = 0; y < img->GetHeight(); y += 4)
                for (size_t x = 0; x < img->GetWidth(); ++x) {
                    const uint8_t v = p[y * stride + x];
                    sum += v; ++n; sat += v >= 250; black += v <= 5;
                }
        }
        const bool last = el >= tEnd;
        if (last && !g_snapshotPath.isEmpty())
            saveSnapshot(img, g_snapshotPath);
        img->Release();
        if (last) break;
    }
    CFloatPtr g = nm.GetNode("Gain");
    if (IsAvailable(g) && IsReadable(g)) m.gainDb = g->GetValue();
    cam->EndAcquisition();
    if (ts.size() > 1)
        m.fps = double(ts.size() - 1) * 1e9 / double(ts.back() - ts.front());
    if (!exps.empty()) {
        std::sort(exps.begin(), exps.end());
        m.expMedUs = exps[exps.size() / 2]; m.expMinUs = exps.front(); m.expMaxUs = exps.back();
    }
    if (n) { m.mean = sum / n; m.satPct = 100.0 * sat / n; m.blackPct = 100.0 * black / n; }
    return m;
}

void printLog(const ps::SettingsLog &log)
{
    for (const QString &l : log.info) std::printf("    info: %s\n", qPrintable(l));
    for (const QString &l : log.warn) std::printf("    WARN: %s\n", qPrintable(l));
}

struct StepResult {
    std::string name;
    Measure m;
    std::map<std::string, std::string> nodes;
    QStringList warnings;   // from the settings writer; gamma's absence is expected on a Chameleon3
};

void printStep(const StepResult &r)
{
    const Measure &m = r.m;
    std::printf("  %-8s %4dx%-4d fps %7.1f  exp %8.1f us [%.1f..%.1f]  gain %5.2f dB  "
                "mean %6.1f  sat %5.2f%%  black %5.2f%%  frames %d (+%d incomplete)\n",
                r.name.c_str(), m.width, m.height, m.fps, m.expMedUs, m.expMinUs, m.expMaxUs,
                m.gainDb, m.mean, m.satPct, m.blackPct, m.frames, m.incomplete);
}

// One connect the way the app does it: prepare, ROI, every tuning node, stream.
StepResult connectAs(CameraPtr cam, const char *name, const QRectF &crop,
                     const ps::ConnectSettings &req, double seconds)
{
    StepResult r; r.name = name;
    cam->Init();
    INodeMap &nm = cam->GetNodeMap();
    prepareLikeTheApp(cam);
    ps::SettingsLog roiLog;
    ps::applyRoi(nm, crop, roiLog);
    const ps::ConnectApplied a = ps::applyConnectSettings(nm, req);
    std::printf("  [%s]\n", name);
    printLog(roiLog);
    printLog(a.log);
    r.warnings = roiLog.warn + a.log.warn;
    snapshotFor(name);
    r.m = stream(cam, seconds);
    r.nodes = readNodes(nm);
    ps::SettingsLog restore;
    ps::applyRoi(nm, QRectF(), restore);   // what stop() does
    cam->DeInit();
    printStep(r);
    return r;
}

// Reference: the camera's own Default user set, nothing of ours on top.
StepResult factory(CameraPtr cam, double seconds)
{
    StepResult r; r.name = "factory";
    cam->Init();
    INodeMap &nm = cam->GetNodeMap();
    bool loaded = false;
    if (setEnum(nm, "UserSetSelector", "Default")) {
        CCommandPtr load = nm.GetNode("UserSetLoad");
        if (IsAvailable(load) && IsWritable(load)) { load->Execute(); loaded = true; }
    }
    nm.InvalidateNodes();
    prepareLikeTheApp(cam);
    std::printf("  [factory] UserSetLoad Default: %s\n", loaded ? "done" : "NOT AVAILABLE");
    snapshotFor("factory");
    r.m = stream(cam, seconds);
    r.nodes = readNodes(nm);
    cam->DeInit();
    printStep(r);
    return r;
}

// Another application leaves the camera in a hostile state.
void dirty(CameraPtr cam)
{
    cam->Init();
    INodeMap &nm = cam->GetNodeMap();
    setEnum(nm, "ExposureAuto", "Off");
    nm.InvalidateNodes();
    CFloatPtr e = nm.GetNode("ExposureTime");
    if (IsAvailable(e) && IsWritable(e)) e->SetValue(std::clamp(70.0, e->GetMin(), e->GetMax()));
    setEnum(nm, "GainAuto", "Off");
    nm.InvalidateNodes();
    CFloatPtr g = nm.GetNode("Gain");
    if (IsAvailable(g) && IsWritable(g)) g->SetValue(g->GetMin());
    for (const char *nme : { "AutoGainUpperLimit", "AutoExposureGainUpperLimit" }) {
        CFloatPtr l = nm.GetNode(nme);
        if (IsAvailable(l) && IsWritable(l)) l->SetValue(l->GetMin());
    }
    for (const char *nme : { "AutoExposureTimeUpperLimit", "AutoExposureExposureTimeUpperLimit" }) {
        CFloatPtr l = nm.GetNode(nme);
        if (IsAvailable(l) && IsWritable(l)) l->SetValue(l->GetMin());
    }
    setEnum(nm, "AcquisitionFrameRateAuto", "Continuous");
    for (const char *nme : { "AcquisitionFrameRateEnable", "AcquisitionFrameRateEnabled" }) {
        CBooleanPtr b = nm.GetNode(nme);
        if (IsAvailable(b) && IsWritable(b)) b->SetValue(false);
    }
    if (setEnum(nm, "LineSelector", "Line1")) { nm.InvalidateNodes(); setEnum(nm, "LineSource", "ExposureActive"); }
    CIntegerPtr w = nm.GetNode("Width"), h = nm.GetNode("Height");
    if (IsAvailable(w) && IsWritable(w)) w->SetValue(std::max<int64_t>(w->GetMin(), 640));
    if (IsAvailable(h) && IsWritable(h)) h->SetValue(std::max<int64_t>(h->GetMin(), 240));
    cam->DeInit();
    std::printf("  [dirty] exposure Off 70 us, gain Off min, auto limits min, rate auto, Line1 "
                "ExposureActive, ROI 640x240\n");
}

int failures = 0;
void check(bool ok, const std::string &what)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
bool within(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Options o;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&]() { return i + 1 < args.size() ? args[++i] : QString(); };
        if      (a == "--serial")          o.serial = next();
        else if (a == "--seconds")         o.seconds = next().toDouble();
        else if (a == "--impact-fps")      o.impactFps = next().toDouble();
        else if (a == "--impact-exposure") o.impactExposure = next().toDouble();
        else if (a == "--impact-gain")     o.impactGain = next().toDouble();
        else if (a == "--impact-gamma")    o.impactGamma = next().toDouble();
        else if (a == "--strobe")          o.strobe = true;
        else if (a == "--no-factory")      o.factory = false;
        else if (a == "--snapshot")        { g_snapshotDir = next(); QDir().mkpath(g_snapshotDir); }
        else { std::fprintf(stderr, "unknown argument %s\n", qPrintable(a)); return 2; }
    }

    SystemPtr system = System::GetInstance();
    CameraList list = system->GetCameras();
    std::printf("cameras: %u\n", list.GetSize());
    for (unsigned i = 0; i < list.GetSize(); ++i) {
        CameraPtr cam = list.GetByIndex(i);
        INodeMap &tl = cam->GetTLDeviceNodeMap();
        CStringPtr sn = tl.GetNode("DeviceSerialNumber"), model = tl.GetNode("DeviceModelName");
        const QString serial = IsReadable(sn) ? QString::fromLatin1(sn->GetValue().c_str()) : QString();
        if (!o.serial.isEmpty() && serial != o.serial) continue;
        g_serial = serial;
        std::printf("\n=== %s %s ===\n", IsReadable(model) ? model->GetValue().c_str() : "?",
                    qPrintable(serial));
        try {
            ps::ConnectSettings dtl;                   // every other role: camera auto
            ps::ConnectSettings imp;                   // impact: locked
            imp.exposureUs = o.impactExposure; imp.gainDb = o.impactGain;
            imp.gamma = o.impactGamma; imp.strobe = o.strobe; imp.fps = o.impactFps;
            const QRectF impactCrop(0.25, 0.3828125, 0.5, 0.234375);   // 640x240 centred on 1280x1024

            std::vector<StepResult> steps;
            if (o.factory) steps.push_back(factory(cam, o.seconds));
            const StepResult dtl1 = connectAs(cam, "dtl1", QRectF(), dtl, o.seconds);
            const StepResult imp1 = connectAs(cam, "impact1", impactCrop, imp, o.seconds);
            const StepResult dtl2 = connectAs(cam, "dtl2", QRectF(), dtl, o.seconds);
            dirty(cam);
            const StepResult dtl3 = connectAs(cam, "dtl3", QRectF(), dtl, o.seconds);
            dirty(cam);
            const StepResult imp2 = connectAs(cam, "impact2", impactCrop, imp, o.seconds);

            std::printf("\n  -- node read-back --\n");
            std::printf("  %-36s", "node");
            for (const StepResult *s : { &dtl1, &imp1, &dtl2, &dtl3, &imp2 }) std::printf(" %-14s", s->name.c_str());
            std::printf("\n");
            for (const auto &kv : dtl1.nodes) {
                std::printf("  %-36s", kv.first.c_str());
                for (const StepResult *s : { &dtl1, &imp1, &dtl2, &dtl3, &imp2 }) {
                    auto it = s->nodes.find(kv.first);
                    std::printf(" %-14.14s", it == s->nodes.end() ? "-" : it->second.c_str());
                }
                std::printf("\n");
            }

            std::printf("\n  -- checks --\n");
            const double fullMax = std::stod(dtl1.nodes.at("AcquisitionFrameRate"));
            for (const StepResult *s : { &dtl1, &dtl2, &dtl3 }) {
                const Measure &m = s->m;
                check(m.fps > 0.98 * fullMax, s->name + ": delivers the full-frame max rate ("
                      + std::to_string(m.fps) + " of " + std::to_string(fullMax) + ")");
                check(s->nodes.at("ExposureAuto") == "Continuous", s->name + ": exposure is auto");
                check(s->nodes.at("GainAuto") == "Continuous", s->name + ": gain is auto");
                check(m.expMaxUs <= 1e6 / fullMax + 1.0, s->name + ": exposure never exceeds the frame period");
                check(s->nodes.at("Line1.LineSource") != "ExposureActive", s->name + ": Line1 is not a strobe");
                check(m.width == dtl1.m.width && m.height == dtl1.m.height, s->name + ": full frame");
            }
            for (const StepResult *s : { &dtl2, &dtl3 }) {
                // Same scene, same settings: the picture must match the first DTL
                // connect, whatever was on the camera before.
                check(within(s->m.mean, dtl1.m.mean, std::max(4.0, 0.08 * dtl1.m.mean)),
                      s->name + ": brightness matches dtl1 (" + std::to_string(s->m.mean)
                      + " vs " + std::to_string(dtl1.m.mean) + ")");
            }
            for (const StepResult *s : { &imp1, &imp2 }) {
                const Measure &m = s->m;
                check(within(m.expMedUs, o.impactExposure, 2.0), s->name + ": exposure locked at "
                      + std::to_string(o.impactExposure) + " us (" + std::to_string(m.expMedUs) + ")");
                check(within(m.gainDb, o.impactGain, 0.1), s->name + ": gain locked ("
                      + std::to_string(m.gainDb) + ")");
                const double rateNode = std::stod(s->nodes.at("AcquisitionFrameRate"));
                check(m.fps > 0.95 * rateNode, s->name + ": delivers its rate ("
                      + std::to_string(m.fps) + " of " + std::to_string(rateNode) + ")");
                check(m.width == 640 && m.height == 240, s->name + ": 640x240 crop");
                check((s->nodes.at("Line1.LineSource") == "ExposureActive") == o.strobe,
                      s->name + ": strobe " + (o.strobe ? "on" : "off"));
            }
            check(within(imp2.m.mean, imp1.m.mean, std::max(3.0, 0.1 * imp1.m.mean)),
                  "impact2 picture matches impact1");
            // Every connect must have written every node it owns: no warning
            // from the writer (bar gamma, absent in raw Bayer on a Chameleon3),
            // and the rate manual whatever the camera held before.
            for (const StepResult *s : { &dtl1, &imp1, &dtl2, &dtl3, &imp2 }) {
                QStringList unexpected;
                for (const QString &w : s->warnings)
                    if (!w.startsWith(QLatin1String("Gamma requested"))) unexpected << w;
                check(unexpected.isEmpty(), s->name + ": every node written"
                      + (unexpected.isEmpty() ? std::string() : " — " + unexpected.join("; ").toStdString()));
                const auto fa = s->nodes.find("AcquisitionFrameRateAuto");
                check(fa == s->nodes.end() || fa->second == "absent" || fa->second == "Off",
                      s->name + ": frame rate is manual (AcquisitionFrameRateAuto "
                      + (fa == s->nodes.end() ? std::string("-") : fa->second) + ")");
            }
        } catch (Spinnaker::Exception &e) {
            std::printf("  Spinnaker error: %s\n", e.what());
            ++failures;
            try { if (cam->IsInitialized()) { if (cam->IsStreaming()) cam->EndAcquisition(); cam->DeInit(); } } catch (...) {}
        }
    }
    list.Clear();
    system->ReleaseInstance();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
