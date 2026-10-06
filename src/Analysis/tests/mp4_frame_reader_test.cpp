// Mp4FrameDecoder (src/Analysis/mp4_frame_reader.h) — the re-analysis MP4 reader's
// random access, on a synthetic clip written here with cv::VideoWriter: 60 frames,
// each with its own index painted in as 8 big black/white blocks (robust to lossy
// coding). Two clips, because what OpenCV's writer produces is not the exporter's
// stream (GOP 10, no B-frames — ffmpeg_video_encoder.cpp) and the writer ignores the
// key-interval request on this build: MPEG-4 part 2 comes out with a keyframe every
// 12 frames (the seek path), and H.264 with ONE keyframe (the GOP-is-the-clip
// fallback, as on clips written before the exporter set its GOP).
//
// Proves, OFF and with decode.seek on: every frame asked for — forward, back, jumps,
// the two-pass pattern (coarse stride to the end, then a span) — is the frame
// returned; and counts the frames decoded, so the step-5 claim (a back-seek costs a
// GOP plus OpenCV's 16-frame backoff, not a decode from frame 0) is a number here.
//
// Run via CTest (src/Analysis/tests/CMakeLists.txt).

#include "../mp4_frame_reader.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using pinpoint::analysis::Mp4FrameDecoder;

static int g_fail = 0;
static void check(bool c, const char* label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static constexpr int kW = 320, kH = 240, kFrames = 60, kBlock = 40;

static cv::Mat paint(int idx)
{
    cv::Mat m(kH, kW, CV_8UC3, cv::Scalar(64, 64, 64));
    for (int b = 0; b < 8; ++b) {
        const bool on = (idx >> b) & 1;
        cv::rectangle(m, cv::Rect(b * kBlock, 100, kBlock, kBlock),
                      on ? cv::Scalar(255, 255, 255) : cv::Scalar(0, 0, 0), cv::FILLED);
    }
    return m;
}

static int unpaint(const cv::Mat& m)
{
    if (m.empty() || m.cols != kW || m.rows != kH)
        return -1;
    int idx = 0;
    for (int b = 0; b < 8; ++b) {
        const cv::Vec3b px = m.at<cv::Vec3b>(100 + kBlock / 2, b * kBlock + kBlock / 2);
        if ((px[0] + px[1] + px[2]) / 3 > 128)
            idx |= (1 << b);
    }
    return idx;
}

static bool writeClip(const std::string& path, const char* cc)
{
    cv::VideoWriter w;
    const std::vector<int> params = { cv::VIDEOWRITER_PROP_KEY_INTERVAL, 10 };
    if (!w.open(path, cv::CAP_FFMPEG, cv::VideoWriter::fourcc(cc[0], cc[1], cc[2], cc[3]),
                30.0, cv::Size(kW, kH), params))
        return false;
    for (int i = 0; i < kFrames; ++i)
        w.write(paint(i));
    w.release();
    return true;
}

static bool readAll(Mp4FrameDecoder& d, const std::vector<int>& order, const char* label)
{
    bool ok = true;
    for (int want : order) {
        cv::Mat f;
        const bool r = d.read(want, f);
        const int got = r ? unpaint(f) : -2;
        if (got != want) {
            std::printf("    asked %d, got %d\n", want, got);
            ok = false;
        }
    }
    check(ok, label);
    return ok;
}

int main()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pp_mp4_frame_reader_test";
    fs::create_directories(dir);
    const std::string clip = (dir / "clip_mp4v.mp4").string();
    const std::string clip264 = (dir / "clip_h264.mp4").string();

    std::printf("=== synthetic clips ===\n");
    if (!writeClip(clip, "mp4v")) {
        std::printf("  no MPEG-4 encoder in this OpenCV build — SKIP\n");
        fs::remove_all(dir);
        return 0;
    }
    const bool have264 = writeClip(clip264, "avc1");
    std::printf("  mp4v written; h264 %s\n", have264 ? "written" : "not available");

    // The two-pass pose pattern: coarse stride 12 over the whole clip, then a dense
    // span [30, 45] — the second pass starts behind where the first ended.
    std::vector<int> twoPass;
    for (int i = 0; i < kFrames; i += 12) twoPass.push_back(i);
    for (int i = 30; i <= 45; ++i) twoPass.push_back(i);

    std::printf("=== OFF (rewind to 0 on a back-seek) ===\n");
    int64_t offDecoded = 0;
    {
        Mp4FrameDecoder d(clip, {});
        readAll(d, { 0, 1, 2, 10, 25, 59 }, "forward reads land on the asked frame");
        readAll(d, { 5, 40, 3, 57, 57, 0 }, "back-seeks (rewind) land on the asked frame");
        check(d.stats().rewinds >= 3, "a back-seek is a rewind");
        Mp4FrameDecoder d2(clip, {});
        readAll(d2, twoPass, "two-pass pattern, OFF");
        offDecoded = d2.stats().decoded + d2.stats().seekDecodedEst;
        std::printf("  OFF two-pass: %s\n", d2.summary().c_str());
        check(offDecoded == 49 + 46, "OFF decodes 0..48, then 0..45 again (95 frames)");
        check(!d2.info().backend.empty(), "the backend OpenCV chose is reported");
    }

    std::printf("=== decode.seek on ===\n");
    {
        Mp4FrameDecoder::Config c;
        c.seek = true;
        Mp4FrameDecoder d(clip, c);
        readAll(d, { 0, 1, 2, 10, 25, 59 }, "forward reads land on the asked frame");
        readAll(d, { 5, 40, 3, 57, 57, 0, 33, 20, 21, 50, 11 },
                "back-seeks and jumps land on the asked frame");
        std::printf("  %s\n", d.summary().c_str());
        check(d.info().keyframes.size() >= 2, "keyframes probed from the packets");
        check(!d.info().keyframes.empty() && d.info().keyframes.front() == 0, "frame 0 is a keyframe");
        check(d.info().maxGop > 0 && d.info().maxGop <= 12, "GOP 12 as written");
        check(d.stats().seekFailures == 0, "no seek landed on the wrong frame");
        check(d.stats().seeks >= 1, "a back-seek past the backoff seeks");

        Mp4FrameDecoder d2(clip, c);
        readAll(d2, twoPass, "two-pass pattern, seek on");
        const int64_t onDecoded = d2.stats().decoded + d2.stats().seekDecodedEst;
        std::printf("  seek two-pass: %s\n", d2.summary().c_str());
        std::printf("  two-pass frames decoded: OFF %lld, seek %lld\n",
                    static_cast<long long>(offDecoded), static_cast<long long>(onDecoded));
        check(d2.stats().rewinds == 0 && d2.stats().seeks == 1, "pass 2 is one keyframe seek, no rewind");
        check(onDecoded < offDecoded, "seek decodes fewer frames than the rewind");

        // Every frame, in reverse — the worst order for a forward decoder.
        std::vector<int> rev;
        for (int i = kFrames - 1; i >= 0; --i) rev.push_back(i);
        Mp4FrameDecoder d3(clip, c);
        readAll(d3, rev, "every frame in reverse lands");
        check(d3.stats().seekFailures == 0, "reverse: no mislanded seek");
    }

    if (have264) {
        std::printf("=== H.264, one keyframe: seek on falls back to the rewind ===\n");
        Mp4FrameDecoder::Config c;
        c.seek = true;
        Mp4FrameDecoder d(clip264, c);
        std::vector<int> rev;
        for (int i = kFrames - 1; i >= 0; i -= 3) rev.push_back(i);
        readAll(d, twoPass, "h264 two-pass pattern lands");
        readAll(d, rev, "h264 reverse lands");
        std::printf("  %s\n", d.summary().c_str());
        check(d.info().keyframes.size() >= 1, "h264 keyframes probed");
        check(d.info().seekUseless == (d.info().keyframes.size() == 1),
              "one keyframe ⇒ seek reported useless");
        check(d.stats().seekFailures == 0, "h264: no mislanded seek");
    }

    std::printf("=== decode.threads 4, decode.hwAccel any (fail-soft) ===\n");
    {
        Mp4FrameDecoder::Config c;
        c.threads = 4;
        c.hwAccel = true;
        c.seek    = true;
        Mp4FrameDecoder d(have264 ? clip264 : clip, c);
        readAll(d, twoPass, "threads + hw + seek: two-pass pattern lands");
        std::printf("  %s\n", d.summary().c_str());
        check(d.info().opened, "opens (hardware or software)");
    }

    std::printf("=== missing file ===\n");
    {
        Mp4FrameDecoder d((dir / "nope.mp4").string(), {});
        cv::Mat f;
        check(!d.read(0, f), "read fails cleanly");
    }

    fs::remove_all(dir);
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
