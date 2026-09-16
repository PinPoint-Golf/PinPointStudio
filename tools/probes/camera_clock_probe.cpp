// camera_clock_probe.cpp — can a Chameleon3's own clock stamp our frames, and how does it line up with ours?
//
// Standalone Spinnaker probe; not part of the app build. It answers the questions the camera-timestamp
// work (event_buffer_design.md §9, 2026-09-15) gates on, before any app code relies on them:
//   1. which clock nodes this firmware has (TimestampLatch + its value node, chunk Timestamp / FrameCounter);
//   2. GetTimeStamp()'s units, and the camera crystal's drift against steady_clock (ppm);
//   3. the latch: its value node, its units and epoch against the FRAME timestamps, and how well a
//      TimestampLatch bracketed by two host reads pins the offset;
//   4. the envelope offset: host arrival minus frame timestamp at the fastest frame — absolute, so two
//      runs can be compared;
//   5. what the frame timestamp marks: at a fixed rate, exposure 1 ms vs 6 ms — if the envelope offset
//      grows by ~5 ms the stamp is the exposure START, if not it is the end/readout;
//   6. whether the counter resets at AcquisitionStart / camera re-Init.
//
// Results (2026-09-15, both studio CM3-U3-13Y3C, fw 1.13.3.00, Spinnaker 4.3.0.190, Windows):
//  • Nodes: TimestampLatch (command) + `Timestamp` (integer) — no TimestampLatchValue; the latched value
//    is read from `Timestamp`. Chunk Timestamp and FrameCounter (not FrameID) exist; the chunk timestamp
//    equals GetTimeStamp().
//  • Units and epoch: frame timestamps and the latched value are both ns on ONE camera clock (latch vs
//    frames 1.000000 units/ns over 30 s). Drift against steady_clock ~0.5–1.4 ppm. The counter does not
//    reset at AcquisitionStart or DeInit/Init.
//  • Jitter: camera-timestamp interval SD 1–2 µs; host-arrival interval SD 45 µs (strip, idle host) up to
//    1.9 ms (full frame beside a second camera); arrival lag p99 up to 75 ms and max 154 ms at full frame.
//  • The latch pins the MINIMUM transfer latency: 1.7 ms for 640×240, 6.8 ms for 1280×1024. The min-RTT
//    bracket is 210–290 µs (±~120 µs); the fastest-quarter spread is ~10 µs beside a strip stream and
//    ~450 µs beside a full-frame one.
//  • ⚠ A 200-command latch burst WHILE STREAMING disturbed the camera's own frame spacing (a ~20 ms gap,
//    interval SD 1 µs → 165–425 µs). Latch before BeginAcquisition, briefly.
//  • What the frame timestamp marks: the envelope offset moved +0.12 ms for 6 ms vs 1 ms exposure and
//    +0.03 ms for 3 ms — the stamp is the exposure END (readout). Mid-exposure = ts − exposure / 2.
//
// Build (Windows, from a vcvars64 shell; the SDK is at its default install root):
//   cl /nologo /EHsc /MD /O2 /std:c++17 /DNOMINMAX ^
//      /I"C:\Program Files\Teledyne\Spinnaker\include" camera_clock_probe.cpp ^
//      /link /LIBPATH:"C:\Program Files\Teledyne\Spinnaker\lib64\vs2015" Spinnaker_v140.lib
// Run with the SDK's bin64\vs2015 on PATH and NOTHING else holding the cameras (close PinPointStudio):
//   camera_clock_probe inventory                          every camera: clock + chunk nodes
//   camera_clock_probe clock <cam> <W> <H> <exp_us> <s>   one camera streaming at its max rate
//   camera_clock_probe exposure <cam>                     full frame at ~150 fps, 1000 µs vs 6000 µs
//   camera_clock_probe dual <exp0_us> <W1> <H1> <exp1_us> <s>
//                                                          cam 0 full frame + cam 1 strip, concurrently
//   camera_clock_probe reset <cam>                        first timestamp across restarts and re-Init
//
// Host clock is std::chrono::steady_clock (QueryPerformanceCounter on Windows) — the same clock
// EventBuffer::nowMicros() stamps with. Camera state is restored on exit.

#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Spinnaker;
using namespace Spinnaker::GenApi;

namespace {

int64_t hostNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

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

// What kind of node, if any, a name resolves to — for the inventory.
std::string describeNode(INodeMap &nm, const char *name)
{
    CNodePtr n = nm.GetNode(name);
    if (!n) return "absent";
    if (!IsAvailable(n)) return "present, not available";
    std::string kind;
    switch (n->GetPrincipalInterfaceType()) {
    case intfICommand:     kind = "command"; break;
    case intfIInteger:     kind = "integer"; break;
    case intfIFloat:       kind = "float"; break;
    case intfIBoolean:     kind = "boolean"; break;
    case intfIEnumeration: kind = "enum"; break;
    case intfIString:      kind = "string"; break;
    default:               kind = "other"; break;
    }
    std::string access = std::string(IsReadable(n) ? "R" : "") + (IsWritable(n) ? "W" : "");
    std::string value;
    if (n->GetPrincipalInterfaceType() == intfIInteger && IsReadable(n))
        value = " = " + std::to_string(((CIntegerPtr)n)->GetValue());
    if (n->GetPrincipalInterfaceType() == intfIFloat && IsReadable(n))
        value = " = " + std::to_string(((CFloatPtr)n)->GetValue());
    return kind + " (" + access + ")" + value;
}

void listEnumEntries(INodeMap &nm, const char *name)
{
    CEnumerationPtr p = nm.GetNode(name);
    if (!IsAvailable(p)) { printf("    %s: n/a\n", name); return; }
    NodeList_t entries; p->GetEntries(entries);
    printf("    %s (now %s):", name, getEnum(nm, name).c_str());
    for (auto &n : entries) { CEnumEntryPtr e = n; if (IsAvailable(e) && IsReadable(e)) printf(" %s", e->GetSymbolic().c_str()); }
    printf("\n");
}

// Centred ROI. Offsets to 0 first so a stale offset never clamps Width/Height max.
void applyRoi(INodeMap &nm, int w, int h)
{
    setInt(nm, "OffsetX", 0); setInt(nm, "OffsetY", 0);
    setInt(nm, "Width", w);   setInt(nm, "Height", h);
    const int64_t sw = getInt(nm, "WidthMax"), sh = getInt(nm, "HeightMax");
    setInt(nm, "OffsetX", (sw - getInt(nm, "Width")) / 2);
    setInt(nm, "OffsetY", (sh - getInt(nm, "Height")) / 2);
    nm.InvalidateNodes();
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
    setBool(nm, "AcquisitionFrameRateEnabled", true);
    setEnum(st, "StreamBufferCountMode", "Manual");    // TL stream map, as in VideoInputSpinnaker
    setInt(st, "StreamBufferCountManual", 40);
}

// Enable the timestamp and frame-id chunks where the firmware has them. Returns what took.
struct Chunks { bool timestamp = false; bool frameId = false; std::string frameIdName; };
Chunks enableChunks(INodeMap &nm)
{
    Chunks c;
    setBool(nm, "ChunkModeActive", true);
    auto enable = [&](const char *entry) {
        if (!setEnum(nm, "ChunkSelector", entry)) return false;
        CBooleanPtr en = nm.GetNode("ChunkEnable");
        if (!IsAvailable(en) || !IsWritable(en)) return false;
        en->SetValue(true);
        return en->GetValue();
    };
    c.timestamp = enable("Timestamp");
    if (enable("FrameID"))           { c.frameId = true; c.frameIdName = "FrameID"; }
    else if (enable("FrameCounter")) { c.frameId = true; c.frameIdName = "FrameCounter"; }
    return c;
}
void disableChunks(INodeMap &nm)
{
    for (const char *entry : { "Timestamp", "FrameID", "FrameCounter" })
        if (setEnum(nm, "ChunkSelector", entry)) setBool(nm, "ChunkEnable", false);
    setBool(nm, "ChunkModeActive", false);
}

struct Saved { int64_t w, h, x, y; double exp; std::string pix, expAuto; };
Saved save(INodeMap &nm)
{
    return { getInt(nm, "Width"), getInt(nm, "Height"), getInt(nm, "OffsetX"), getInt(nm, "OffsetY"),
             getFloat(nm, "ExposureTime"), getEnum(nm, "PixelFormat"), getEnum(nm, "ExposureAuto") };
}
void restore(INodeMap &nm, const Saved &s)
{
    try {
        disableChunks(nm);
        setInt(nm, "OffsetX", 0); setInt(nm, "OffsetY", 0);
        setInt(nm, "Width", s.w); setInt(nm, "Height", s.h);
        setInt(nm, "OffsetX", s.x); setInt(nm, "OffsetY", s.y);
        setEnum(nm, "PixelFormat", s.pix.c_str());
        setFloat(nm, "ExposureTime", s.exp);
        setEnum(nm, "ExposureAuto", s.expAuto.c_str());
    } catch (Spinnaker::Exception &e) { printf("  restore: %s\n", e.what()); }
}

double pct(std::vector<double> v, double p)
{
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    const double idx = p * (v.size() - 1);
    const size_t lo = size_t(std::floor(idx)), hi = size_t(std::ceil(idx));
    return v[lo] + (v[hi] - v[lo]) * (idx - lo);
}
double stddev(const std::vector<double> &v)
{
    if (v.size() < 2) return NAN;
    double m = 0; for (double x : v) m += x; m /= v.size();
    double s = 0; for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / (v.size() - 1));
}

// One latch bracket: host read, TimestampLatch, the latched value, host read. This firmware has no
// TimestampLatchValue; the latched value is read from `Timestamp` (and we record which).
struct LatchSample { int64_t h0, h1, v; };
const char *latchValueNode(INodeMap &nm)
{
    CIntegerPtr a = nm.GetNode("TimestampLatchValue");
    if (IsAvailable(a) && IsReadable(a)) return "TimestampLatchValue";
    CIntegerPtr b = nm.GetNode("Timestamp");
    if (IsAvailable(b) && IsReadable(b)) return "Timestamp";
    return nullptr;
}
bool latchOnce(INodeMap &nm, LatchSample &out)
{
    CCommandPtr latch = nm.GetNode("TimestampLatch");
    const char *vn = latchValueNode(nm);
    if (!IsAvailable(latch) || !IsWritable(latch) || !vn) return false;
    CIntegerPtr value = nm.GetNode(vn);
    out.h0 = hostNs();
    latch->Execute();
    out.v = value->GetValue();
    out.h1 = hostNs();
    return true;
}

// N brackets; the min-RTT one gives (hostMid, value). Prints the spread.
struct LatchResult {
    bool ok = false; double hostMidNs = 0; double value = 0;
    double rttMinUs = 0, rttP50Us = 0, rttP99Us = 0; double offsetSpreadUs = 0; std::string node;
};
LatchResult latchBracket(INodeMap &nm, int n, const char *label)
{
    LatchResult r;
    std::vector<LatchSample> s;
    for (int i = 0; i < n; ++i) { LatchSample x; if (!latchOnce(nm, x)) break; s.push_back(x); }
    if (s.empty()) { printf("  latch %-12s: not available\n", label); return r; }
    std::vector<double> rtt, off;
    size_t best = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        rtt.push_back((s[i].h1 - s[i].h0) / 1e3);
        off.push_back(((s[i].h0 + s[i].h1) / 2.0 - double(s[i].v)) / 1e3);
        if (s[i].h1 - s[i].h0 < s[best].h1 - s[best].h0) best = i;
    }
    std::vector<size_t> order(s.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rtt[a] < rtt[b]; });
    std::vector<double> fastOff;
    for (size_t k = 0; k < std::max<size_t>(1, order.size() / 4); ++k) fastOff.push_back(off[order[k]]);
    r.ok = true;
    r.node = latchValueNode(nm);
    r.hostMidNs = (s[best].h0 + s[best].h1) / 2.0;
    r.value = double(s[best].v);
    r.rttMinUs = pct(rtt, 0.0); r.rttP50Us = pct(rtt, 0.5); r.rttP99Us = pct(rtt, 0.99);
    r.offsetSpreadUs = fastOff.size() > 1 ? (pct(fastOff, 1.0) - pct(fastOff, 0.0)) : 0.0;
    // Consecutive latched values against host time: the latch value's own units per host ns.
    double unitsPerNs = NAN;
    if (s.size() >= 2 && s.back().h0 > s.front().h0)
        unitsPerNs = double(s.back().v - s.front().v) / double((s.back().h0 + s.back().h1) / 2 - (s.front().h0 + s.front().h1) / 2);
    printf("  latch %-12s: node %s n=%zu  rtt min %.1f  p50 %.1f  p99 %.1f us | value %.0f | units/host-ns over the burst %.6f | fastest-quarter offset spread %.1f us\n",
           label, r.node.c_str(), s.size(), r.rttMinUs, r.rttP50Us, r.rttP99Us, r.value, unitsPerNs, r.offsetSpreadUs);
    return r;
}

struct Frame { int64_t arrivalNs; uint64_t ts; int64_t chunkTs; int64_t frameId; int64_t chunkFrameId; bool incomplete; };

struct StreamStats {
    std::string label;
    int frames = 0, incomplete = 0, idGaps = 0;
    double camFps = 0, unitsRatio = 0, driftPpmEnvelope = NAN;
    double envOffsetMs = NAN;          // host arrival − frame ts at the fastest frame, absolute
    double lagP50Ms = NAN, lagP99Ms = NAN, lagMaxMs = NAN;   // arrival − (ts + envelope offset)
    double hostIntervalSdUs = NAN, camIntervalSdUs = NAN;
    bool chunkTsAgrees = false, haveChunkTs = false;
    // The latch against the frames.
    bool latched = false;
    double latchUnitsPerHostNs = NAN;  // latch value units per host ns, start → end of the run
    double latchVsFrameTsMs = NAN;     // (host − latch value) − (host − frame ts at the fastest frame), if ns
};

// Stream `seconds` at the node's max rate for the current ROI and exposure, with latch brackets before
// and after, and report the clock numbers.
StreamStats streamClock(CameraPtr cam, double expUs, double seconds, const char *label, bool verbose = true)
{
    StreamStats st; st.label = label;
    INodeMap &nm = cam->GetNodeMap();
    setFloat(nm, "ExposureTime", expUs);
    nm.InvalidateNodes();
    CFloatPtr fps = nm.GetNode("AcquisitionFrameRate");
    if (IsAvailable(fps) && IsWritable(fps)) fps->SetValue(fps->GetMax());
    const Chunks ch = enableChunks(nm);

    cam->BeginAcquisition();
    for (int i = 0; i < 10; ++i) { ImagePtr im = cam->GetNextImage(2000); im->Release(); }
    const LatchResult l0 = latchBracket(nm, 200, "start");

    std::vector<Frame> fr;
    fr.reserve(size_t(seconds * 700));
    const int64_t tEnd = hostNs() + int64_t(seconds * 1e9);
    while (hostNs() < tEnd) {
        ImagePtr im = cam->GetNextImage(2000);
        Frame f;
        f.arrivalNs = hostNs();                       // first statement after the frame is in hand
        f.incomplete = im->IsIncomplete();
        f.ts = im->GetTimeStamp();
        f.frameId = int64_t(im->GetFrameID());
        f.chunkTs = -1; f.chunkFrameId = -1;
        if (!f.incomplete) {
            try { if (ch.timestamp) f.chunkTs = im->GetChunkData().GetTimestamp(); } catch (...) {}
            try { if (ch.frameId)   f.chunkFrameId = im->GetChunkData().GetFrameID(); } catch (...) {}
        }
        im->Release();
        fr.push_back(f);
    }
    const LatchResult l1 = latchBracket(nm, 200, "end (live)");
    cam->EndAcquisition();

    st.frames = int(fr.size());
    if (fr.size() < 10) { printf("  %s: too few frames\n", label); return st; }
    for (auto &f : fr) st.incomplete += f.incomplete ? 1 : 0;
    for (size_t i = 1; i < fr.size(); ++i) {
        const int64_t d = (fr[i].chunkFrameId >= 0 && fr[i-1].chunkFrameId >= 0)
                              ? fr[i].chunkFrameId - fr[i-1].chunkFrameId : fr[i].frameId - fr[i-1].frameId;
        if (d != 1) ++st.idGaps;
    }
    const double camSpanNs  = double(fr.back().ts - fr.front().ts);
    const double hostSpanNs = double(fr.back().arrivalNs - fr.front().arrivalNs);
    st.camFps = (fr.size() - 1) / (camSpanNs / 1e9);
    st.unitsRatio = camSpanNs / hostSpanNs;           // ≈1.0 ⇒ GetTimeStamp is ns

    std::vector<double> hostIv, camIv;
    for (size_t i = 1; i < fr.size(); ++i) {
        hostIv.push_back((fr[i].arrivalNs - fr[i-1].arrivalNs) / 1e3);
        camIv.push_back(double(fr[i].ts - fr[i-1].ts) / 1e3);
    }
    st.hostIntervalSdUs = stddev(hostIv);
    st.camIntervalSdUs  = stddev(camIv);

    int agree = 0, have = 0;
    for (auto &f : fr) if (f.chunkTs >= 0) { ++have; if (std::llabs(f.chunkTs - int64_t(f.ts)) <= 1000) ++agree; }
    st.haveChunkTs = have > 0; st.chunkTsAgrees = have > 0 && agree == have;

    // Envelope drift from the minima of the first and last tenths; positive ppm ⇒ the camera runs slow.
    const size_t tenth = std::max<size_t>(1, fr.size() / 10);
    double minHead = 1e300, minTail = 1e300; double tHead = 0, tTail = 0;
    for (size_t i = 0; i < tenth; ++i) {
        const double d = double(fr[i].arrivalNs) - double(fr[i].ts);
        if (d < minHead) { minHead = d; tHead = double(fr[i].ts); }
    }
    for (size_t i = fr.size() - tenth; i < fr.size(); ++i) {
        const double d = double(fr[i].arrivalNs) - double(fr[i].ts);
        if (d < minTail) { minTail = d; tTail = double(fr[i].ts); }
    }
    if (tTail > tHead) st.driftPpmEnvelope = (minTail - minHead) / (tTail - tHead) * 1e6;

    // Absolute envelope offset and the lag distribution against it.
    std::vector<double> d;
    for (auto &f : fr) d.push_back(double(f.arrivalNs) - double(f.ts));
    const double envNs = pct(d, 0.0);
    st.envOffsetMs = envNs / 1e6;
    for (double &x : d) x = (x - envNs) / 1e6;
    st.lagP50Ms = pct(d, 0.5); st.lagP99Ms = pct(d, 0.99); st.lagMaxMs = pct(d, 1.0);

    // The latch against the frames: its units (start→end of the run, a long baseline) and, if those are
    // ns, how its epoch sits against the frame timestamps. latchVsFrameTsMs ≈ the minimum transfer latency
    // (positive, a few ms) if the latch and the frame stamps share an epoch.
    if (l0.ok) {
        st.latched = true;
        if (l1.ok && l1.hostMidNs > l0.hostMidNs)
            st.latchUnitsPerHostNs = (l1.value - l0.value) / (l1.hostMidNs - l0.hostMidNs);
        const double latchOffsetNs = l0.hostMidNs - l0.value;     // host − latch, if ns
        st.latchVsFrameTsMs = (envNs - latchOffsetNs) / 1e6;
    }

    if (verbose) {
        printf("  %s: %lldx%lld exp %.0f us | %d frames, %d incomplete, %d frame-id gaps | cam %.2f fps | ts units ratio %.6f (1.0 = ns)\n",
               label, (long long)getInt(nm, "Width"), (long long)getInt(nm, "Height"), getFloat(nm, "ExposureTime"),
               st.frames, st.incomplete, st.idGaps, st.camFps, st.unitsRatio);
        printf("    interval SD: host arrival %.1f us  vs  camera timestamp %.1f us\n", st.hostIntervalSdUs, st.camIntervalSdUs);
        printf("    chunks: Timestamp %s, %s %s | chunk ts %s GetTimeStamp\n",
               ch.timestamp ? "on" : "unavailable", ch.frameIdName.empty() ? "FrameID" : ch.frameIdName.c_str(),
               ch.frameId ? "on" : "unavailable", !st.haveChunkTs ? "n/a vs" : (st.chunkTsAgrees ? "==" : "!="));
        printf("    envelope offset (host arrival − frame ts, fastest frame): %.3f ms absolute\n", st.envOffsetMs);
        printf("    arrival lag above the envelope: p50 %.3f  p99 %.3f  max %.3f ms\n", st.lagP50Ms, st.lagP99Ms, st.lagMaxMs);
        printf("    drift: envelope %.2f ppm\n", st.driftPpmEnvelope);
        if (st.latched)
            printf("    latch vs frames: latch units per host ns %.6f | (host−latch) vs (host−frame ts at envelope): %.3f ms\n",
                   st.latchUnitsPerHostNs, st.latchVsFrameTsMs);
        fflush(stdout);
    }
    return st;
}

int inventory(SystemPtr sys)
{
    CameraList cams = sys->GetCameras();
    printf("cameras: %u\n", cams.GetSize());
    for (unsigned ci = 0; ci < cams.GetSize(); ++ci) {
        CameraPtr cam = cams.GetByIndex(ci);
        try {
            cam->Init();
            INodeMap &nm = cam->GetNodeMap();
            INodeMap &tl = cam->GetTLDeviceNodeMap();
            printf("\n=== camera %u: %s  serial %s  fw %s\n", ci, getStr(tl, "DeviceModelName").c_str(),
                   getStr(tl, "DeviceSerialNumber").c_str(), getStr(nm, "DeviceFirmwareVersion").c_str());
            for (const char *n : { "TimestampLatch", "TimestampLatchValue", "TimestampReset", "TimestampIncrement",
                                   "Timestamp", "GevTimestampTickFrequency", "GevTimestampControlLatch",
                                   "GevTimestampValue", "DeviceClockFrequency", "ChunkModeActive", "ChunkEnable",
                                   "ChunkTimestamp", "ChunkFrameID", "ChunkFrameCounter" })
                printf("    %-26s %s\n", n, describeNode(nm, n).c_str());
            listEnumEntries(nm, "ChunkSelector");
            cam->DeInit();
        } catch (Spinnaker::Exception &e) {
            printf("camera %u: ERROR %s\n", ci, e.what());
            try { cam->DeInit(); } catch (...) {}
        }
        cam = nullptr;
    }
    cams.Clear();
    return 0;
}

int clockOne(SystemPtr sys, unsigned ci, int w, int h, double expUs, double seconds)
{
    CameraList cams = sys->GetCameras();
    if (ci >= cams.GetSize()) { printf("no camera %u\n", ci); cams.Clear(); return 1; }
    CameraPtr cam = cams.GetByIndex(ci);
    cam->Init();
    INodeMap &nm = cam->GetNodeMap();
    const Saved saved = save(nm);
    try {
        printf("camera %u serial %s\n", ci, getStr(cam->GetTLDeviceNodeMap(), "DeviceSerialNumber").c_str());
        commonSetup(cam);
        applyRoi(nm, w, h);
        streamClock(cam, expUs, seconds, "clock");
    } catch (Spinnaker::Exception &e) { printf("ERROR %s\n", e.what()); try { cam->EndAcquisition(); } catch (...) {} }
    restore(nm, saved);
    cam->DeInit(); cam = nullptr; cams.Clear();
    return 0;
}

// What the frame timestamp marks. Full frame at its max rate (both exposures fit at ~149 fps), 1000 µs
// then 6000 µs then 1000 µs again — the repeat shows a difference is not drift. The envelope offset is
// absolute, so a ~5 ms rise at 6 ms means the stamp is the exposure START.
int exposureTest(SystemPtr sys, unsigned ci)
{
    CameraList cams = sys->GetCameras();
    if (ci >= cams.GetSize()) { printf("no camera %u\n", ci); cams.Clear(); return 1; }
    CameraPtr cam = cams.GetByIndex(ci);
    cam->Init();
    INodeMap &nm = cam->GetNodeMap();
    const Saved saved = save(nm);
    try {
        commonSetup(cam);
        applyRoi(nm, 4096, 4096);
        std::vector<StreamStats> runs;
        for (double e : { 1000.0, 6000.0, 1000.0, 3000.0 }) {
            char lbl[32]; snprintf(lbl, sizeof lbl, "exp %.0f", e);
            runs.push_back(streamClock(cam, e, 8.0, lbl));
        }
        printf("\n  envelope offset (absolute, ms): 1 ms %.3f | 6 ms %.3f | 1 ms %.3f | 3 ms %.3f\n",
               runs[0].envOffsetMs, runs[1].envOffsetMs, runs[2].envOffsetMs, runs[3].envOffsetMs);
        printf("  6 ms − mean(1 ms) = %+.3f ms ; 3 ms − mean(1 ms) = %+.3f ms\n",
               runs[1].envOffsetMs - (runs[0].envOffsetMs + runs[2].envOffsetMs) / 2,
               runs[3].envOffsetMs - (runs[0].envOffsetMs + runs[2].envOffsetMs) / 2);
        printf("  → +5 / +2 ms means the timestamp marks exposure START (mid-exposure = ts + exposure/2);\n"
               "    ~0 means END/readout (mid-exposure = ts − exposure/2).\n");
    } catch (Spinnaker::Exception &e) { printf("ERROR %s\n", e.what()); try { cam->EndAcquisition(); } catch (...) {} }
    restore(nm, saved);
    cam->DeInit(); cam = nullptr; cams.Clear();
    return 0;
}

int dual(SystemPtr sys, double exp0, int w1, int h1, double exp1, double seconds)
{
    CameraList cams = sys->GetCameras();
    if (cams.GetSize() < 2) { printf("need 2 cameras, have %u\n", cams.GetSize()); cams.Clear(); return 1; }
    CameraPtr a = cams.GetByIndex(0), b = cams.GetByIndex(1);
    a->Init(); b->Init();
    INodeMap &na = a->GetNodeMap(); INodeMap &nb = b->GetNodeMap();
    const Saved sa = save(na), sb = save(nb);
    try {
        commonSetup(a); commonSetup(b);
        applyRoi(na, 4096, 4096); applyRoi(nb, w1, h1);
        StreamStats ra, rb;
        std::thread ta([&] { try { ra = streamClock(a, exp0, seconds, "A full", false); } catch (Spinnaker::Exception &e) { printf("A: %s\n", e.what()); } });
        std::thread tb([&] { try { rb = streamClock(b, exp1, seconds, "B strip", false); } catch (Spinnaker::Exception &e) { printf("B: %s\n", e.what()); } });
        ta.join(); tb.join();
        for (auto *s : { &ra, &rb })
            printf("  %-8s %d frames, %d incomplete, %d id gaps, %.2f fps | interval SD host %.1f / cam %.1f us | env offset %.3f ms | lag p50 %.3f p99 %.3f max %.3f ms | drift %.2f ppm | latch units/ns %.6f, latch vs frames %.3f ms\n",
                   s->label.c_str(), s->frames, s->incomplete, s->idGaps, s->camFps, s->hostIntervalSdUs, s->camIntervalSdUs,
                   s->envOffsetMs, s->lagP50Ms, s->lagP99Ms, s->lagMaxMs, s->driftPpmEnvelope, s->latchUnitsPerHostNs, s->latchVsFrameTsMs);
    } catch (Spinnaker::Exception &e) { printf("ERROR %s\n", e.what()); }
    restore(na, sa); restore(nb, sb);
    a->DeInit(); b->DeInit(); a = nullptr; b = nullptr;
    cams.Clear();
    return 0;
}

// Does the counter reset at AcquisitionStart, or at camera re-Init? First timestamps of short runs.
int resetTest(SystemPtr sys, unsigned ci)
{
    CameraList cams = sys->GetCameras();
    if (ci >= cams.GetSize()) { printf("no camera %u\n", ci); cams.Clear(); return 1; }
    CameraPtr cam = cams.GetByIndex(ci);
    auto firstTs = [&](const char *label) {
        cam->BeginAcquisition();
        ImagePtr im = cam->GetNextImage(2000);
        const uint64_t ts = im->GetTimeStamp(); const int64_t h = hostNs();
        im->Release();
        cam->EndAcquisition();
        printf("  %-22s first ts %.6f s   host %.6f s   host-ts %.6f s\n", label, ts / 1e9, h / 1e9, (h - double(ts)) / 1e9);
    };
    try {
        cam->Init(); commonSetup(cam);
        firstTs("run 1");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        firstTs("run 2 (+0.5 s)");
        cam->DeInit();
        cam->Init(); commonSetup(cam);
        firstTs("after DeInit/Init");
        cam->DeInit();
    } catch (Spinnaker::Exception &e) { printf("ERROR %s\n", e.what()); try { cam->EndAcquisition(); } catch (...) {} try { cam->DeInit(); } catch (...) {} }
    cam = nullptr; cams.Clear();
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "inventory";
    SystemPtr sys = System::GetInstance();
    const LibraryVersion v = sys->GetLibraryVersion();
    printf("Spinnaker %d.%d.%d.%d | host clock steady_clock\n", v.major, v.minor, v.type, v.build);
    int rc = 0;
    auto arg = [&](int i, double def) { return argc > i ? atof(argv[i]) : def; };
    if (!strcmp(mode, "clock"))
        rc = clockOne(sys, unsigned(arg(2, 0)), int(arg(3, 4096)), int(arg(4, 4096)), arg(5, 1000), arg(6, 60));
    else if (!strcmp(mode, "exposure"))
        rc = exposureTest(sys, unsigned(arg(2, 0)));
    else if (!strcmp(mode, "dual"))
        rc = dual(sys, arg(2, 1000), int(arg(3, 640)), int(arg(4, 240)), arg(5, 100), arg(6, 20));
    else if (!strcmp(mode, "reset"))
        rc = resetTest(sys, unsigned(arg(2, 0)));
    else
        rc = inventory(sys);
    sys->ReleaseInstance();
    return rc;
}
