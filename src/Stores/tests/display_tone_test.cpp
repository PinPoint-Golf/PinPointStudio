/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// The display tone curve (#183; src/Video/display_tone.h, src/Stores/export_tone.h).
//
//  1. The table: its ends, monotone, never darker, values worked out by hand for γ 0.80, and every one of the
//     256 entries equal to what the shader computes on an 8-bit value (round(255·(v/255)^exponent)).
//  2. One source: the exponent QML hands the shader (displayToneFor of a Bayer stream) is the constant the
//     table is built from, and viewgain.frag takes its exponent as data, never as a number of its own.
//  3. The stream rule: every Bayer format curves, every other format (a webcam's, a phone clip's) doesn't, and
//     the swing document's spelling gives the same answer as the format.
//  4. The export: which of a swing's files a zip curves; a synthetic clip re-toned frame by frame comes out with
//     every frame, each pixel the formula of the stored frame's pixel, and the stored file byte-identical (the
//     analysis decodes the same frames after an export as before); thumb.jpg likewise (ruled: the Product Owner,
//     26 Nov 2026, #183 batch 3 (4)).
//  5. A still picture (display_tone_image.h: the report's stills, the Markup Lab, thumb.jpg): every channel
//     through the one table, alpha dropped; a stream at 1.0 left exactly as it is. And Settings → Cameras' sensor
//     preview runs the shader's pass with the camera's own C++ value (#183 batch 3 (5)).
//  6. The analysis path never sees it: only the display and export files include the headers, and the recording
//     loader only for #183's analysis A/B (a tool's switch, off by default).
//
// Run via CTest (src/Analysis/tests/CMakeLists.txt).

#include "../../Video/display_tone.h"
#include "../../Video/display_tone_image.h"
#include "../export_tone.h"
#include "../../Analysis/mp4_frame_reader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTemporaryDir>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// The curve worked out here, not taken from the code under test.
static int curved(int v) { return int(std::lround(255.0 * std::pow(v / 255.0, 0.80))); }

// ── 1–3 ─────────────────────────────────────────────────────────────────────────────────────────

static void tableAndRule()
{
    std::printf("=== the table ===\n");
    const auto &t = displayToneLut();
    check(t[0] == 0 && t[255] == 255, "black and white stay where they are");
    bool mono = true, lifts = true;
    for (int v = 1; v < 256; ++v) mono  = mono && t[v] >= t[v - 1];
    for (int v = 0; v < 256; ++v) lifts = lifts && t[v] >= v;
    check(mono, "monotone");
    check(lifts, "never darker than the stored value");
    // By hand, 255·(v/255)^0.8: 1 → 3.03, 16 → 27.84, 32 → 48.46, 64 → 84.38, 128 → 146.92, 200 → 209.96, 254 → 254.20.
    check(t[1] == 3 && t[16] == 28 && t[32] == 48 && t[64] == 84 && t[128] == 147 && t[200] == 210 && t[254] == 254,
          "the hand-worked values at γ 0.80");
    bool shaderModel = true;
    for (int v = 0; v < 256; ++v)
        shaderModel = shaderModel && t[v] == curved(v);
    check(shaderModel, "all 256 entries equal the shader's pow on an 8-bit value, rounded");

    std::printf("=== one source ===\n");
    check(kDisplayToneExponent == 0.80, "the ruled exponent is 0.80 (Design Lead, 25 Nov 2026)");
    check(displayToneFor(PixelFormat::BayerRG8) == kDisplayToneExponent,
          "the exponent handed to the shader is the constant");
    check(displayToneLut() == displayToneLutFor(displayToneFor(PixelFormat::BayerRG8)),
          "the export's table is built from the exponent the shader is handed");
    QFile frag(QStringLiteral(PP_SRC_DIR "/Shaders/viewgain.frag"));
    QFile qml(QStringLiteral(PP_SRC_DIR "/Gui/cameras/PpCameraFrame.qml"));
    const bool opened = frag.open(QIODevice::ReadOnly) && qml.open(QIODevice::ReadOnly);
    const QString fragText = QString::fromUtf8(frag.readAll());
    const QString qmlText  = QString::fromUtf8(qml.readAll());
    check(opened && fragText.contains(QStringLiteral("float toneExponent;"))
                 && fragText.contains(QStringLiteral("vec3(toneExponent)"))
                 && !fragText.contains(QStringLiteral("0.8")),
          "viewgain.frag takes the exponent as a uniform and holds no number of its own");
    check(qmlText.contains(QStringLiteral("toneExponent: root._tone"))
              && qmlText.contains(QStringLiteral(".displayTone : 1.0"))
              && qmlText.contains(QStringLiteral("instance.displayTone")),
          "PpCameraFrame passes the C++ value (live and replay) to the shader");
    QFile panel(QStringLiteral(PP_SRC_DIR "/Gui/settings/CamerasPanel.qml"));
    const QString panelText = panel.open(QIODevice::ReadOnly) ? QString::fromUtf8(panel.readAll()) : QString();
    check(panelText.contains(QStringLiteral("camRow.localPreviewInstance.displayTone) || 1.0"))
              && panelText.contains(QStringLiteral("sourceItem: settingsBayerView.visible ? settingsBayerView : null"))
              && panelText.count(QStringLiteral("viewgain.frag.qsb")) == 1,
          "Settings > Cameras' sensor preview passes the preview camera's C++ value to the same shader");

    std::printf("=== the stream rule ===\n");
    bool bayer = true;
    for (PixelFormat f : { PixelFormat::BayerRG8, PixelFormat::BayerRG12, PixelFormat::BayerRG16, PixelFormat::BayerBG8,
                           PixelFormat::BayerGR8, PixelFormat::BayerGB8, PixelFormat::BayerGB16 })
        bayer = bayer && displayToneFor(f) == 0.80;
    check(bayer, "every Bayer format: 0.80");
    bool other = true;
    for (PixelFormat f : { PixelFormat::NV12, PixelFormat::YUYV, PixelFormat::UYVY, PixelFormat::YUV422,
                           PixelFormat::YUV420P, PixelFormat::BGR24, PixelFormat::RGB24, PixelFormat::BGRA32,
                           PixelFormat::RGBA32, PixelFormat::MJPEG, PixelFormat::H264_NAL, PixelFormat::Mono8,
                           PixelFormat::Unknown })
        other = other && displayToneFor(f) == 1.0;
    check(other, "webcam, phone, Mono8 and unknown formats: 1.0 (already tone-mapped, or not ours to curve)");
    check(displayToneFor(std::string_view("")) == 1.0 && displayToneFor(std::string_view("BayerXY8")) == 1.0,
          "an empty name (a phone clip) and an unknown name: 1.0");
    bool names = true;
    for (int i = 0; i <= int(PixelFormat::H264_NAL); ++i) {
        const auto f = static_cast<PixelFormat>(i);
        names = names && displayToneFor(std::string_view(pixelFormatName(f))) == displayToneFor(f);
    }
    check(names, "the document's spelling and the format agree for every format");
}

// ── 4: the export ───────────────────────────────────────────────────────────────────────────────

static QJsonObject videoStream(const QString &file, const QJsonObject &source, int perspective)
{
    QJsonObject s{ { QStringLiteral("kind"), QStringLiteral("video") }, { QStringLiteral("file"), file },
                   { QStringLiteral("source"), source },
                   { QStringLiteral("playback"), QJsonObject{ { QStringLiteral("fps"), 30 } } } };
    if (perspective >= 0)
        s[QStringLiteral("setup")] = QJsonObject{ { QStringLiteral("perspective"), perspective } };
    return s;
}
static QJsonObject fmt(const char *f) { return { { QStringLiteral("serial"), QStringLiteral("1") },
                                                 { QStringLiteral("pixelFormat"), QString::fromLatin1(f) } }; }

static void members()
{
    std::printf("=== which files a zip curves ===\n");
    const QJsonObject phone{ { QStringLiteral("serial"), QStringLiteral("phone-1") } };
    const QJsonObject rig{ { QStringLiteral("streams"), QJsonArray{
        videoStream(QStringLiteral("dtl.mp4"),    fmt("BayerRG8"), 1),
        videoStream(QStringLiteral("face.mp4"),   fmt("BayerRG8"), 2),
        videoStream(QStringLiteral("impact.mp4"), fmt("BayerRG8"), 4),
        videoStream(QStringLiteral("cam.mp4"),    fmt("NV12"),     3),
        videoStream(QStringLiteral("phone.mov"),  phone,           -1),
        QJsonObject{ { QStringLiteral("kind"), QStringLiteral("imu") } } } } };
    const ExportToneMembers m = exportToneMembers(rig);
    check(m.clips.size() == 3 && m.clips.contains(QStringLiteral("dtl.mp4")) && m.clips.contains(QStringLiteral("face.mp4"))
              && m.clips.contains(QStringLiteral("impact.mp4")),
          "the three studio-camera clips, and not the webcam's or the phone's");
    check(m.clips.value(QStringLiteral("impact.mp4")).impact && !m.clips.value(QStringLiteral("face.mp4")).impact
              && m.clips.value(QStringLiteral("face.mp4")).playbackFps == 30,
          "the impact clip is known as one; the playback rate is the stream's");
    check(m.thumbnail, "thumb.jpg, from the face-on studio camera, is curved");

    const QJsonObject webcamFaceOn{ { QStringLiteral("streams"), QJsonArray{
        videoStream(QStringLiteral("dtl.mp4"),  fmt("BayerRG8"), 1),
        videoStream(QStringLiteral("face.mp4"), fmt("NV12"),     2) } } };
    const ExportToneMembers w = exportToneMembers(webcamFaceOn);
    check(!w.thumbnail && w.clips.size() == 1, "a webcam face-on: its thumbnail is not curved, the Bayer DTL clip is");

    const QJsonObject noSetup{ { QStringLiteral("streams"), QJsonArray{
        videoStream(QStringLiteral("phone.mov"), phone,           -1),
        videoStream(QStringLiteral("a.mp4"),     fmt("BayerBG8"), -1) } } };
    check(exportToneMembers(noSetup).thumbnail, "no setup block: the first camera stream made the thumbnail");
    check(exportToneMembers(QJsonObject{}).clips.isEmpty() && !exportToneMembers(QJsonObject{}).thumbnail,
          "no document: nothing curved");
}

// Hands back what it is given: the frames the export would encode.
class CapturingEncoder final : public IVideoEncoder {
public:
    bool open(const VideoEncoderConfig &c) override { cfg = c; opened = true; return true; }
    bool writeBgr(const cv::Mat &bgr) override { frames.push_back(bgr.clone()); return true; }
    bool finish() override { finished = true; return true; }
    VideoEncoderConfig   cfg;
    std::vector<cv::Mat> frames;
    bool opened = false, finished = false;
};

static constexpr int kW = 320, kH = 240, kFrames = 24;

static cv::Mat paint(int idx)
{
    // A dark scene, as a studio camera records it: a ramp over the low codes, a mid patch and a white one.
    cv::Mat m(kH, kW, CV_8UC3);
    for (int x = 0; x < kW; ++x)
        m.col(x).setTo(cv::Scalar((x * 90) / kW, (x * 70) / kW + 5, (x * 60) / kW + idx));
    cv::rectangle(m, cv::Rect(20, 20, 60, 60), cv::Scalar(128, 128, 128), cv::FILLED);
    cv::rectangle(m, cv::Rect(240, 20, 60, 60), cv::Scalar(255, 255, 255), cv::FILLED);
    return m;
}

static QByteArray sha(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

static void clip()
{
    std::printf("=== a clip re-toned for the zip ===\n");
    QTemporaryDir dir;
    const QString stored = dir.filePath(QStringLiteral("face.mp4"));
    cv::VideoWriter w;
    if (!w.open(stored.toStdString(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0,
                cv::Size(kW, kH))) {
        // As mp4_frame_reader_test: an OpenCV without its FFmpeg writer (the studio PC's, where the
        // opencv_videoio_ffmpeg plugin is absent) can't make the synthetic clip. The app's export doesn't need
        // it (it encodes through libav), so this section is skipped there and proved where the writer exists.
        std::printf("  no MPEG-4 writer in this OpenCV build: the clip checks SKIP\n");
        return;
    }
    for (int i = 0; i < kFrames; ++i) w.write(paint(i));
    w.release();
    const QByteArray before = sha(stored);

    // What the analysis decodes from the stored clip, before any export.
    std::vector<cv::Mat> analysed;
    {
        analysis::Mp4FrameDecoder d(stored.toStdString(), {});
        cv::Mat f;
        for (int64_t i = 0; d.read(i, f); ++i) analysed.push_back(f.clone());
    }
    check(int(analysed.size()) == kFrames, "the stored clip decodes to every frame written");

    CapturingEncoder enc;
    VideoEncoderConfig cfg;
    cfg.out_fps = 30;
    cfg.path    = dir.filePath(QStringLiteral("out.mp4")).toStdString();
    const RetoneResult r = retoneClip(stored.toStdString(), enc, cfg, displayToneLut());
    check(r.ok && enc.opened && enc.finished, "re-toned, the encoder opened and finished");
    check(r.frames == kFrames && int(enc.frames.size()) == kFrames,
          "every frame, in order: the document's frame times still match");
    check(enc.cfg.width == kW && enc.cfg.height == kH && enc.cfg.out_fps == 30, "the clip's own size and rate");

    bool exact = enc.frames.size() == analysed.size();
    for (size_t i = 0; exact && i < analysed.size(); ++i) {
        const cv::Mat &a = analysed[i], &b = enc.frames[i];
        exact = a.size() == b.size() && a.type() == CV_8UC3 && b.type() == CV_8UC3;
        for (int y = 0; exact && y < a.rows; ++y)
            for (int x = 0; exact && x < a.cols; ++x)
                for (int c = 0; c < 3; ++c)
                    if (b.at<cv::Vec3b>(y, x)[c] != curved(a.at<cv::Vec3b>(y, x)[c])) { exact = false; break; }
    }
    check(exact, "each exported pixel is the curve of the stored frame's pixel, every channel");

    check(sha(stored) == before, "the stored clip is byte-identical after the export");
    std::vector<cv::Mat> after;
    {
        analysis::Mp4FrameDecoder d(stored.toStdString(), {});
        cv::Mat f;
        for (int64_t i = 0; d.read(i, f); ++i) after.push_back(f.clone());
    }
    bool same = after.size() == analysed.size();
    for (size_t i = 0; same && i < after.size(); ++i)
        same = cv::norm(after[i], analysed[i], cv::NORM_INF) == 0.0;
    check(same, "the analysis decodes the same, uncurved frames after the export");

    CapturingEncoder none;
    const RetoneResult missing = retoneClip(dir.filePath(QStringLiteral("nope.mp4")).toStdString(), none, cfg, displayToneLut());
    check(!missing.ok && !none.opened, "a clip that won't open fails without opening the encoder (the stored one ships)");
}

static void thumbnail()
{
    std::printf("=== thumb.jpg re-toned for the zip (ruled: #183 batch 3 (4)) ===\n");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("thumb.jpg"));
    QImage img(64, 48, QImage::Format_RGB888);
    img.fill(QColor(40, 64, 128));
    img.save(path, "JPG", 100);
    const QByteArray before = sha(path);
    const QImage stored(path);
    const QByteArray out = retoneJpeg(path, displayToneLut());
    QImage got;
    got.loadFromData(out, "JPG");
    bool close = !got.isNull() && got.size() == stored.size();
    for (int y = 0; close && y < got.height(); y += 7)
        for (int x = 0; close && x < got.width(); x += 7) {
            const QRgb a = stored.pixel(x, y), b = got.pixel(x, y);
            close = std::abs(qRed(b) - curved(qRed(a))) <= 3 && std::abs(qGreen(b) - curved(qGreen(a))) <= 3
                 && std::abs(qBlue(b) - curved(qBlue(a))) <= 3;
        }
    check(close, "each pixel is the curve of the stored one (within 3 codes: JPEG)");
    check(sha(path) == before, "the stored thumb.jpg is untouched");
    check(retoneJpeg(dir.filePath(QStringLiteral("none.jpg")), displayToneLut()).isEmpty(),
          "no thumbnail: nothing (the stored file ships as it is)");
}

// ── 5: a still picture ──────────────────────────────────────────────────────────────────────────

static void picture()
{
    std::printf("=== a still picture ===\n");
    const auto &t = displayToneLut();
    QImage rgb(16, 8, QImage::Format_RGB888);
    for (int y = 0; y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x)
            rgb.setPixelColor(x, y, QColor(x * 16, y * 32, 255 - x * 16));
    const QImage curvedRgb = withDisplayTone(rgb, kDisplayToneExponent);
    bool exact = curvedRgb.format() == QImage::Format_RGB888 && curvedRgb.size() == rgb.size();
    for (int y = 0; exact && y < rgb.height(); ++y)
        for (int x = 0; exact && x < rgb.width(); ++x) {
            const QColor a = rgb.pixelColor(x, y), b = curvedRgb.pixelColor(x, y);
            exact = b.red() == t[a.red()] && b.green() == t[a.green()] && b.blue() == t[a.blue()];
        }
    check(exact, "every channel of every pixel through the one table");

    // A decoded video frame comes as 32-bit premultiplied; opaque, so the curve is the same, alpha dropped.
    QImage argb(4, 4, QImage::Format_ARGB32_Premultiplied);
    argb.fill(QColor(32, 64, 128));
    const QImage fromArgb = withDisplayTone(argb, kDisplayToneExponent);
    const QColor c = fromArgb.pixelColor(1, 1);
    check(fromArgb.format() == QImage::Format_RGB888 && c.red() == t[32] && c.green() == t[64] && c.blue() == t[128],
          "a 32-bit video frame: curved the same, as RGB888");

    const QImage asIs = withDisplayTone(argb, displayToneFor(PixelFormat::NV12));
    check(asIs.format() == argb.format() && asIs == argb, "a webcam's or phone's picture (1.0): exactly as it is");
    check(withDisplayTone(QImage(), kDisplayToneExponent).isNull(), "no picture: none");
    check(withToneTable(rgb, t) == curvedRgb, "the table and the stream's exponent give the same picture");
}

// ── 6: the analysis path never sees it ──────────────────────────────────────────────────────────

static void analysisNeverSeesIt()
{
    std::printf("=== who includes the curve ===\n");
    // The only files that may: the curve's own module, the screen (live and replay), the export zip, this test.
    const QSet<QString> allowed{
        QStringLiteral("Stores/export_tone.h"), QStringLiteral("Stores/export_tone.cpp"),
        QStringLiteral("Stores/swing_zip_exporter.cpp"),
        QStringLiteral("Gui/cameras/camera_instance.cpp"), QStringLiteral("Gui/shot/disk_replay_source.cpp"),
        QStringLiteral("Stores/tests/display_tone_test.cpp"),
        // A still picture through the table (#183 batch 3): the helper, the report's stills (and the Charts
        // pictures, which share the renderer) and its test, and the Markup Lab.
        QStringLiteral("Video/display_tone_image.h"), QStringLiteral("Gui/report/report_stills.cpp"),
        QStringLiteral("Gui/tests/report_stills_test.cpp"), QStringLiteral("Gui/markup/markup_controller.cpp"),
        // #183's analysis A/B (a tool's switch, off unless decode.toneExponent is set: display_tone.h) and its test.
        QStringLiteral("Stores/recording_loader.cpp"), QStringLiteral("Analysis/tests/swing_window_parity_test.cpp") };
    const QDir rootDir(QDir(QStringLiteral(PP_SRC_DIR)).canonicalPath());
    const QString root = rootDir.path();
    QStringList offenders;
    int scanned = 0;
    QDirIterator it(root, { QStringLiteral("*.h"), QStringLiteral("*.cpp"), QStringLiteral("*.mm") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel  = rootDir.relativeFilePath(path);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        ++scanned;
        const QString text = QString::fromUtf8(f.readAll());
        const bool includes = text.contains(QStringLiteral("display_tone.h\""))
                           || text.contains(QStringLiteral("display_tone_image.h\""))
                           || text.contains(QStringLiteral("export_tone.h\""));
        if (includes && !allowed.contains(rel))
            offenders << rel;
    }
    for (const QString &o : offenders) std::printf("    includes the curve: %s\n", qPrintable(o));
    check(scanned > 500, "the source tree was scanned");
    check(offenders.isEmpty(), "nothing else includes the curve: not the analysis, the recorder, the decoder or the encoder");
    QFile loader(rootDir.filePath(QStringLiteral("Stores/recording_loader.cpp")));
    const QString loaderText = loader.open(QIODevice::ReadOnly) ? QString::fromUtf8(loader.readAll()) : QString();
    check(loaderText.contains(QStringLiteral("toneExponent")),
          "the recording loader uses it only behind the A/B tool's decode.toneExponent key");
}

int main()
{
    tableAndRule();
    members();
    clip();
    thumbnail();
    picture();
    analysisNeverSeesIt();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
