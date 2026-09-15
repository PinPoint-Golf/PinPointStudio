// chameleon3_roi_probe.cpp — does a Chameleon3 go faster with a smaller ROI, and by how much?
//
// Standalone Spinnaker probe; not part of the app build. Findings are recorded in
// docs/design/impact_camera_design.md §3.1 (2026-09-15).
//
// Build (Windows, from a vcvars64 shell; the SDK is at its default install root):
//   cl /nologo /EHsc /MD /O2 /std:c++17 /DNOMINMAX ^
//      /I"C:\Program Files\Teledyne\Spinnaker\include" chameleon3_roi_probe.cpp ^
//      /link /LIBPATH:"C:\Program Files\Teledyne\Spinnaker\lib64\vs2015" Spinnaker_v140.lib
// Run with the SDK's bin64\vs2015 on PATH and nothing else holding the cameras:
//   chameleon3_roi_probe sweep [exposure_us=70]      every camera: rows sweep, width check,
//                                                     exposure floor, binning, GPIO/trigger nodes
//   chameleon3_roi_probe dual  [W=1280] [H=240] [s=4] cameras 0 and 1 concurrently: 0 full-frame
//                                                     at its max, 1 a WxH strip at its max
//
// Method notes that matter for the numbers:
//  - ExposureTime is written BEFORE AcquisitionFrameRate. Read straight after the Width/Height
//    write, the rate node's max is one ROI stale (the previous ROI's max gets applied). An
//    exposure write in between refreshes it.
//  - Delivered fps comes from the camera's frame timestamps (ns on USB3); the host-side count
//    agrees to 0.1%. The node's max over-reports the delivered rate by 1–3.5% off the cap.
//  - Camera state (ROI, exposure, rate enable, binning) is restored on exit; Chameleon3 settings
//    are volatile anyway unless a UserSet is saved.

#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Spinnaker;
using namespace Spinnaker::GenApi;

namespace {

void setInt(INodeMap &nm, const char *name, int64_t v)
{
    CIntegerPtr p = nm.GetNode(name);
    if (!IsAvailable(p) || !IsWritable(p)) return;
    const int64_t inc = p->GetInc() > 0 ? p->GetInc() : 1;
    v = std::max(p->GetMin(), std::min(v, p->GetMax()));
    v = p->GetMin() + ((v - p->GetMin()) / inc) * inc;
    p->SetValue(v);
}
int64_t getInt(INodeMap &nm, const char *name)
{
    CIntegerPtr p = nm.GetNode(name);
    return (IsAvailable(p) && IsReadable(p)) ? p->GetValue() : -1;
}
double getFloat(INodeMap &nm, const char *name)
{
    CFloatPtr p = nm.GetNode(name);
    return (IsAvailable(p) && IsReadable(p)) ? p->GetValue() : -1;
}
void setFloat(INodeMap &nm, const char *name, double v)
{
    CFloatPtr p = nm.GetNode(name);
    if (!IsAvailable(p) || !IsWritable(p)) return;
    p->SetValue(std::max(p->GetMin(), std::min(v, p->GetMax())));
}
bool setEnum(INodeMap &nm, const char *name, const char *entry)
{
    CEnumerationPtr p = nm.GetNode(name);
    if (!IsAvailable(p) || !IsWritable(p)) return false;
    CEnumEntryPtr e = p->GetEntryByName(entry);
    if (!IsAvailable(e) || !IsReadable(e)) return false;
    p->SetIntValue(e->GetValue());
    return true;
}
std::string getEnum(INodeMap &nm, const char *name)
{
    CEnumerationPtr p = nm.GetNode(name);
    if (!IsAvailable(p) || !IsReadable(p)) return "?";
    return p->GetCurrentEntry()->GetSymbolic().c_str();
}
void listEnum(INodeMap &nm, const char *name)
{
    CEnumerationPtr p = nm.GetNode(name);
    if (!IsAvailable(p)) { printf("    %s: n/a\n", name); return; }
    NodeList_t entries; p->GetEntries(entries);
    printf("    %s (now %s):", name, getEnum(nm, name).c_str());
    for (auto &n : entries) { CEnumEntryPtr e = n; if (IsAvailable(e) && IsReadable(e)) printf(" %s", e->GetSymbolic().c_str()); }
    printf("\n");
}
void setBool(INodeMap &nm, const char *name, bool v)
{
    CBooleanPtr p = nm.GetNode(name);
    if (IsAvailable(p) && IsWritable(p)) p->SetValue(v);
}
std::string getStr(INodeMap &nm, const char *name)
{
    CStringPtr p = nm.GetNode(name);
    return (IsAvailable(p) && IsReadable(p)) ? std::string(p->GetValue().c_str()) : "?";
}

// Centred ROI. Offsets to 0 first so a stale offset never clamps Width/Height max.
void applyRoi(INodeMap &nm, int w, int h)
{
    setInt(nm, "OffsetX", 0); setInt(nm, "OffsetY", 0);
    setInt(nm, "Width", w);   setInt(nm, "Height", h);
    const int64_t sw = getInt(nm, "WidthMax"), sh = getInt(nm, "HeightMax");
    setInt(nm, "OffsetX", (sw - getInt(nm, "Width")) / 2);
    setInt(nm, "OffsetY", (sh - getInt(nm, "Height")) / 2);
}

struct Rate { double advMax = 0, camFps = 0, hostFps = 0; int frames = 0, incomplete = 0; };

// Exposure first, then the rate to its (now fresh) max, then stream `seconds`.
Rate runAtMax(CameraPtr cam, INodeMap &nm, double expUs, double seconds)
{
    Rate r;
    setFloat(nm, "ExposureTime", expUs);
    CFloatPtr fps = nm.GetNode("AcquisitionFrameRate");
    if (IsAvailable(fps) && IsWritable(fps)) { r.advMax = fps->GetMax(); fps->SetValue(r.advMax); }
    cam->BeginAcquisition();
    for (int i = 0; i < 5; ++i) { ImagePtr im = cam->GetNextImage(2000); im->Release(); }
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t ts0 = 0, ts1 = 0;
    for (;;) {
        ImagePtr im = cam->GetNextImage(2000);
        if (im->IsIncomplete()) ++r.incomplete;
        const uint64_t ts = im->GetTimeStamp();
        if (r.frames == 0) ts0 = ts;
        ts1 = ts; ++r.frames;
        im->Release();
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (dt >= seconds) { r.hostFps = (r.frames - 1) / dt; break; }
    }
    cam->EndAcquisition();
    if (ts1 > ts0 && r.frames > 1) r.camFps = (r.frames - 1) / ((ts1 - ts0) / 1e9);
    return r;
}

void commonSetup(CameraPtr cam)
{
    INodeMap &nm = cam->GetNodeMap();
    INodeMap &st = cam->GetTLStreamNodeMap();
    setEnum(nm, "AcquisitionMode", "Continuous");
    setEnum(nm, "TriggerMode", "Off");
    if (!setEnum(nm, "PixelFormat", "BayerRG8") && !setEnum(nm, "PixelFormat", "BayerBG8"))
        setEnum(nm, "PixelFormat", "Mono8");
    setEnum(nm, "ExposureAuto", "Off");
    setEnum(nm, "GainAuto", "Off");
    setBool(nm, "AcquisitionFrameRateEnable", true);
    setEnum(nm, "AcquisitionFrameRateAuto", "Off");   // older FLIR firmware spelling
    setEnum(st, "StreamBufferCountMode", "Manual");    // TL stream map, as in VideoInputSpinnaker
    setInt(st, "StreamBufferCountManual", 40);
}

struct Saved { int64_t w, h, x, y, binH, binV; double exp; bool fpsEn; std::string pix, expAuto; };
Saved save(INodeMap &nm)
{
    CBooleanPtr en = nm.GetNode("AcquisitionFrameRateEnable");
    return { getInt(nm, "Width"), getInt(nm, "Height"), getInt(nm, "OffsetX"), getInt(nm, "OffsetY"),
             getInt(nm, "BinningHorizontal"), getInt(nm, "BinningVertical"), getFloat(nm, "ExposureTime"),
             (IsAvailable(en) && IsReadable(en)) ? en->GetValue() : false,
             getEnum(nm, "PixelFormat"), getEnum(nm, "ExposureAuto") };
}
void restore(INodeMap &nm, const Saved &s)
{
    try {
        setInt(nm, "BinningVertical", s.binV > 0 ? s.binV : 1);
        setInt(nm, "BinningHorizontal", s.binH > 0 ? s.binH : 1);
        setInt(nm, "OffsetX", 0); setInt(nm, "OffsetY", 0);
        setInt(nm, "Width", s.w); setInt(nm, "Height", s.h);
        setInt(nm, "OffsetX", s.x); setInt(nm, "OffsetY", s.y);
        setEnum(nm, "PixelFormat", s.pix.c_str());
        setFloat(nm, "ExposureTime", s.exp);
        setEnum(nm, "ExposureAuto", s.expAuto.c_str());
        setBool(nm, "AcquisitionFrameRateEnable", s.fpsEn);
    } catch (Spinnaker::Exception &e) { printf("  restore: %s\n", e.what()); }
}

void printRow(const char *label, INodeMap &nm, const Rate &r)
{
    printf("  %-10s %5lld x %4lld | max %6.1f | delivered %6.1f fps (host %6.1f) | period %6.3f ms | exp %7.1f us | %d incomplete\n",
           label, (long long)getInt(nm, "Width"), (long long)getInt(nm, "Height"), r.advMax, r.camFps, r.hostFps,
           r.camFps > 0 ? 1000.0 / r.camFps : 0.0, getFloat(nm, "ExposureTime"), r.incomplete);
    fflush(stdout);
}

int sweep(SystemPtr sys, double expUs)
{
    CameraList cams = sys->GetCameras();
    printf("cameras: %u\n", cams.GetSize());
    for (unsigned ci = 0; ci < cams.GetSize(); ++ci) {
        CameraPtr cam = cams.GetByIndex(ci);
        try {
            cam->Init();
            INodeMap &nm = cam->GetNodeMap();
            INodeMap &tl = cam->GetTLDeviceNodeMap();
            printf("\n=== camera %u: %s  serial %s  fw %s  sensor %lldx%lld  link limit %lld B/s (max %lld)\n", ci,
                   getStr(tl, "DeviceModelName").c_str(), getStr(tl, "DeviceSerialNumber").c_str(),
                   getStr(nm, "DeviceFirmwareVersion").c_str(),
                   (long long)getInt(nm, "WidthMax"), (long long)getInt(nm, "HeightMax"),
                   (long long)getInt(nm, "DeviceLinkThroughputLimit"),
                   (long long)((CIntegerPtr)nm.GetNode("DeviceLinkThroughputLimit"))->GetMax());
            const Saved saved = save(nm);
            commonSetup(cam);
            printf("    PixelFormat=%s\n", getEnum(nm, "PixelFormat").c_str());

            std::vector<int> rows = { 1024, 720, 480, 400, 320, 280, 260, 240, 232, 224, 200, 120 };
            for (int pass = 0; pass < 2; ++pass) {
                printf("\n  --- rows sweep, 1280 wide, %s, exposure %.0f us ---\n", pass ? "ascending" : "descending", expUs);
                for (int r : rows) { applyRoi(nm, 1280, r); printRow("rows", nm, runAtMax(cam, nm, expUs, 2.0)); }
                std::reverse(rows.begin(), rows.end());
            }

            printf("\n  --- width check (rate should not move) ---\n");
            for (auto wh : std::vector<std::pair<int,int>>{ {1280,480}, {640,480}, {1280,240}, {640,240}, {320,240}, {160,120} }) {
                applyRoi(nm, wh.first, wh.second); printRow("wxh", nm, runAtMax(cam, nm, expUs, 2.0));
            }

            printf("\n  --- exposure floor and clamp at 1280x240 ---\n");
            applyRoi(nm, 1280, 240);
            {
                CFloatPtr ex = nm.GetNode("ExposureTime");
                setFloat(nm, "ExposureTime", expUs);
                CFloatPtr fps = nm.GetNode("AcquisitionFrameRate"); fps->SetValue(fps->GetMax());
                printf("    ExposureTime min %.1f us, max %.1f us at %.1f fps\n", ex->GetMin(), ex->GetMax(), fps->GetValue());
                for (double e : { ex->GetMin(), 50.0, 70.0, 500.0, 6570.0 })
                    printRow("exp", nm, runAtMax(cam, nm, e, 2.0));
            }

            printf("\n  --- binning, full field, %.0f us ---\n", expUs);
            for (int b : { 2, 4 }) {
                try {
                    applyRoi(nm, 4096, 4096);
                    setInt(nm, "BinningVertical", b); setInt(nm, "BinningHorizontal", b);
                    if (getInt(nm, "BinningVertical") != b) { printf("    bin%d not supported\n", b); continue; }
                    applyRoi(nm, 4096, 4096);
                    char lbl[16]; snprintf(lbl, sizeof lbl, "bin%d", b);
                    printRow(lbl, nm, runAtMax(cam, nm, expUs, 2.0));
                    printf("             pixel format now %s; BinningHorizontal=%lld\n",
                           getEnum(nm, "PixelFormat").c_str(), (long long)getInt(nm, "BinningHorizontal"));
                } catch (Spinnaker::Exception &e) { printf("    bin%d: %s\n", b, e.what()); try { cam->EndAcquisition(); } catch (...) {} }
            }
            setInt(nm, "BinningVertical", 1); setInt(nm, "BinningHorizontal", 1);

            printf("\n  --- GPIO / trigger ---\n");
            listEnum(nm, "ExposureMode");
            CEnumerationPtr ls = nm.GetNode("LineSelector");
            if (IsAvailable(ls)) {
                NodeList_t entries; ls->GetEntries(entries);
                for (auto &n : entries) {
                    CEnumEntryPtr e = n; if (!IsAvailable(e) || !IsReadable(e)) continue;
                    ls->SetIntValue(e->GetValue());
                    printf("   [%s]\n", e->GetSymbolic().c_str());
                    listEnum(nm, "LineMode"); listEnum(nm, "LineSource");
                }
            }
            listEnum(nm, "TriggerSelector"); listEnum(nm, "TriggerSource"); listEnum(nm, "TriggerActivation");

            restore(nm, saved);
            cam->DeInit();
        } catch (Spinnaker::Exception &e) {
            printf("camera %u: ERROR %s\n", ci, e.what());
            try { cam->EndAcquisition(); } catch (...) {}
            try { cam->DeInit(); } catch (...) {}
        }
        cam = nullptr;
    }
    cams.Clear();
    return 0;
}

int dual(SystemPtr sys, int w, int h, double seconds)
{
    CameraList cams = sys->GetCameras();
    if (cams.GetSize() < 2) { printf("need 2 cameras, have %u\n", cams.GetSize()); cams.Clear(); return 1; }
    CameraPtr a = cams.GetByIndex(0), b = cams.GetByIndex(1);
    a->Init(); b->Init();
    INodeMap &na = a->GetNodeMap(); INodeMap &nb = b->GetNodeMap();
    const Saved sa = save(na), sb = save(nb);
    try {
        commonSetup(a); commonSetup(b);
        applyRoi(na, 4096, 4096); applyRoi(nb, w, h);
        Rate ra, rb;
        std::thread ta([&] { try { ra = runAtMax(a, na, 70.0, seconds); } catch (Spinnaker::Exception &e) { printf("A: %s\n", e.what()); } });
        std::thread tb([&] { try { rb = runAtMax(b, nb, 70.0, seconds); } catch (Spinnaker::Exception &e) { printf("B: %s\n", e.what()); } });
        ta.join(); tb.join();
        printf("concurrent %.0f s:\n", seconds);
        printf("  A %s: ", getStr(a->GetTLDeviceNodeMap(), "DeviceSerialNumber").c_str()); printRow("full", na, ra);
        printf("  B %s: ", getStr(b->GetTLDeviceNodeMap(), "DeviceSerialNumber").c_str()); printRow("strip", nb, rb);
        printf("  link use: A %.1f MB/s, B %.1f MB/s\n",
               ra.camFps * getInt(na, "Width") * getInt(na, "Height") / 1e6,
               rb.camFps * getInt(nb, "Width") * getInt(nb, "Height") / 1e6);
    } catch (Spinnaker::Exception &e) { printf("ERROR %s\n", e.what()); }
    restore(na, sa); restore(nb, sb);
    a->DeInit(); b->DeInit(); a = nullptr; b = nullptr;
    cams.Clear();
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "sweep";
    SystemPtr sys = System::GetInstance();
    const LibraryVersion v = sys->GetLibraryVersion();
    printf("Spinnaker %d.%d.%d.%d\n", v.major, v.minor, v.type, v.build);
    int rc = 0;
    if (!strcmp(mode, "dual"))
        rc = dual(sys, argc > 2 ? atoi(argv[2]) : 1280, argc > 3 ? atoi(argv[3]) : 240, argc > 4 ? atof(argv[4]) : 4.0);
    else
        rc = sweep(sys, argc > 2 ? atof(argv[2]) : 70.0);
    sys->ReleaseInstance();
    return rc;
}
