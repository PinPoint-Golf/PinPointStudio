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

#include "report_stills.h"

#include "../../Analysis/mp4_frame_reader.h"   // Mp4FrameDecoder — the re-analysis reader, OpenCV BGR
#include "../../Core/pp_debug.h"               // ppInfo / ppWarn — the app log
#include "../../Core/pp_tuned_constants.h"     // the per-keypoint pose gate
#include "../../Export/swing_store.h"          // the swing document, whichever format it is in
#include "../markup/markup_truth.h"            // faceOnStreamIndex / streamLooksDownTheLine / frameIndexForUs

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QThreadPool>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>


// ─────────────────────────────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────────────────────────────
namespace {

using namespace pinpoint::report::stills;

// The line colour: Theme.colorWarn in the Folio LIGHT palette (src/Gui/theme/Theme.qml, `folio.light`),
// the report's only palette. Hard-coded because the stills are made in C++ off the GUI thread; change
// both together.
const QColor kWarn(0xBA, 0x56, 0x2D);

// The per-keypoint gate: the codebase-wide 0.30 the body modules apply (pp_tuned_constants.h).
double gateFor(const Segment &s)
{
    return s.lowerBody ? pinpoint::tuned::lowerBody::kConfMin : pinpoint::tuned::upperBody::kConfMin;
}

// Pose sample matching: the pose runs on (nearly) every frame through the swing and the analyzer's
// instants ARE frame times, so a sample normally sits exactly on the frame. One frame interval at the
// studio's 150 fps is 6.7 ms; 20 ms accepts the nearest sample within three. Outside the swing the
// schedule is sparse (gaps to 80 ms at address); a bracket within 60 ms either side is blended there.
constexpr qint64 kPoseNearestUs = 20000;
constexpr qint64 kPoseBracketUs = 60000;

enum class Cam { FaceOn, Dtl, Unknown };
Cam camOf(const QString &c)
{
    const QString s = c.trimmed().toLower().remove(QLatin1Char('-')).remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
    if (s == QLatin1String("faceon") || s == QLatin1String("face")) return Cam::FaceOn;
    if (s == QLatin1String("dtl") || s == QLatin1String("downtheline")) return Cam::Dtl;
    return Cam::Unknown;
}

using StreamPick = StillStream;   // report_stills_pure.h stillStreamOf
StreamPick pickStream(const QJsonObject &root, Cam cam) { return stillStreamOf(root, cam == Cam::Dtl); }

PoseTrackLite readPose(const QJsonObject &analysis, Cam cam, qint64 t0) { return readPoseTrack(analysis, cam == Cam::Dtl, t0); }

// One swing's document and decoders, shared by every request for it.
struct SwingContext {
    QString     dir;
    QJsonObject root, analysis;
    qint64      t0 = 0;
    bool        leadIsLeft = true;
    std::map<int, PoseTrackLite>                     pose;      // by Cam
    std::map<int, std::optional<QRectF>>             band;      // by Cam
    std::map<int, StreamPick>                        streams;   // by Cam
    std::map<int, QVector<ShaftSample>>              shafts;    // by Cam
    std::map<int, std::unique_ptr<pinpoint::analysis::Mp4FrameDecoder>> decoders;
};

const PoseTrackLite &poseFor(SwingContext &ctx, Cam cam)
{
    auto it = ctx.pose.find(int(cam));
    if (it == ctx.pose.end()) it = ctx.pose.emplace(int(cam), readPose(ctx.analysis, cam, ctx.t0)).first;
    return it->second;
}

const std::optional<QRectF> &bandFor(SwingContext &ctx, Cam cam)
{
    auto it = ctx.band.find(int(cam));
    if (it != ctx.band.end()) return it->second;
    return ctx.band.emplace(int(cam), swingBodyBand(poseFor(ctx, cam), ctx.analysis, ctx.t0)).first->second;
}

// The dashed address line: dark outline under white, drawn as explicit dashes so the two strokes'
// dashes coincide (a QPen dash pattern is in units of ITS width, so two widths would drift apart).
void drawDashed(QPainter &p, const QVector<QPointF> &pts, double w)
{
    const double dash = 4.0 * w, gap = 3.0 * w;
    QPainterPath path;
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const QPointF a = pts[i], b = pts[i + 1];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len <= 0) continue;
        for (double s = 0; s < len; s += dash + gap) {
            const double e = std::min(len, s + dash);
            path.moveTo(a + (b - a) * (s / len));
            path.lineTo(a + (b - a) * (e / len));
        }
    }
    p.setPen(QPen(QColor(0, 0, 0, 170), w + 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    for (const QPointF &q : pts) p.drawEllipse(q, w * 1.2 + 1.0, w * 1.2 + 1.0);
    p.setPen(QPen(QColor(255, 255, 255, 240), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 240));
    for (const QPointF &q : pts) p.drawEllipse(q, w * 1.2, w * 1.2);
    p.setBrush(Qt::NoBrush);
}

void drawSolid(QPainter &p, const QVector<QPointF> &pts, double w)
{
    QPainterPath path;
    path.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i) path.lineTo(pts[i]);
    p.setPen(QPen(kWarn, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(kWarn);
    for (const QPointF &q : pts) p.drawEllipse(q, w * 1.3, w * 1.3);
    p.setBrush(Qt::NoBrush);
}

void addNote(StillResult &r, const QString &n)
{
    if (n.isEmpty()) return;
    r.note = r.note.isEmpty() ? n : r.note + QLatin1Char(' ') + n;
}

StillResult renderInContext(SwingContext &ctx, const StillRequest &rq)
{
    StillResult r;
    const Cam cam = camOf(rq.camera);
    if (cam == Cam::Unknown) { r.error = QStringLiteral("unknown camera \"%1\"").arg(rq.camera); return r; }
    if (rq.outPath.isEmpty()) { r.error = QStringLiteral("no output path"); return r; }
    const QString camWords = cam == Cam::Dtl ? QStringLiteral("down-the-line") : QStringLiteral("face-on");

    auto sit = ctx.streams.find(int(cam));
    if (sit == ctx.streams.end()) sit = ctx.streams.emplace(int(cam), pickStream(ctx.root, cam)).first;
    const StreamPick &stream = sit->second;
    if (!stream.ok) { r.error = QStringLiteral("this swing has no %1 video").arg(camWords); return r; }
    const QString videoPath = QDir(ctx.dir).filePath(stream.file);
    if (!QFileInfo::exists(videoPath)) { r.error = QStringLiteral("the %1 video is missing (%2)").arg(camWords, stream.file); return r; }

    const std::optional<qint64> at = positionInstantUs(ctx.analysis, rq.phase, ctx.t0);
    if (!at) { r.error = QStringLiteral("this swing has no %1 position").arg(rq.phase); return r; }
    const int idx = nearestFrame(stream.tUs, *at);
    if (idx < 0) { r.error = QStringLiteral("the %1 video has no frames").arg(camWords); return r; }
    const qint64 frameUs = stream.tUs[idx];

    // ── The frame ─────────────────────────────────────────────────────────────────────────────────────
    // FFmpeg backend with the keyframe seek: the seek is verified against each landed frame's own
    // timestamp and falls back to rewind-and-decode for good the first time it lands wrong
    // (mp4_frame_reader.h), so it cannot return a neighbouring frame; it only saves decoding a whole
    // clip for a still near its end. Where FFmpeg cannot load (a Windows install without the plugin)
    // the open fails soft to OpenCV's default and the seek is simply not used.
    auto &dec = ctx.decoders[int(cam)];
    if (!dec) {
        pinpoint::analysis::Mp4FrameDecoder::Config cfg;
        cfg.ffmpeg = true;
        cfg.seek   = true;
        dec = std::make_unique<pinpoint::analysis::Mp4FrameDecoder>(videoPath.toStdString(), cfg);
    }
    cv::Mat bgr;
    if (!dec->read(idx, bgr) || bgr.empty() || bgr.type() != CV_8UC3) {
        r.error = QStringLiteral("could not decode frame %1 of %2").arg(idx).arg(stream.file);
        return r;
    }
    const int W = bgr.cols, H = bgr.rows;

    // ── The pose at that frame ────────────────────────────────────────────────────────────────────────
    const PoseTrackLite &pose = poseFor(ctx, cam);
    QVector<QPointF> linePts, addrPts;
    if (!rq.line.isEmpty()) {
        const Segment seg = segmentFor(rq.line, ctx.leadIsLeft);
        // The line at an instant, from the pose or (for `shaft`) the camera's shaft track; empty + *why.
        auto lineAt = [&](qint64 us, bool atAddress, QString *why) -> QVector<QPointF> {
            const QString when = atAddress ? QStringLiteral(" at address") : QStringLiteral(" at this moment");
            if (seg.club) {
                auto it = ctx.shafts.find(int(cam));
                if (it == ctx.shafts.end()) it = ctx.shafts.emplace(int(cam), shaftTrack(ctx.analysis, cam == Cam::Dtl, ctx.t0)).first;
                if (it->second.isEmpty()) { *why = QStringLiteral("this swing has no club track for this camera."); return {}; }
                // The shaft track is per frame on both cameras; 20 ms finds this frame's own sample.
                const QVector<QPointF> pts = shaftAt(it->second, us, kPoseNearestUs);
                if (pts.isEmpty()) *why = QStringLiteral("the club was not measured%1 from this camera.").arg(when);
                return pts;
            }
            if (!pose.present) { *why = QStringLiteral("this swing's body positions were not saved."); return {}; }
            const QVector<float> kp = poseAt(pose.samples, us, kPoseNearestUs, kPoseBracketUs);
            if (kp.isEmpty()) { *why = QStringLiteral("the body was not tracked%1.").arg(when); return {}; }
            QString weak;
            const QVector<QPointF> pts = segmentPoints(seg, kp, gateFor(seg), &weak);
            if (pts.isEmpty()) *why = QStringLiteral("the %1 could not be seen clearly%2.").arg(weak, atAddress ? when : QStringLiteral(" here"));
            return pts;
        };
        const QString what = seg.club ? QStringLiteral("club line") : QStringLiteral("body line");
        if (!seg.known()) {
            ppWarn() << "[ReportStills] unknown line" << rq.line << "— drawn without one";
            addNote(r, QStringLiteral("No body line: \"%1\" is not a line the report draws.").arg(rq.line));
        } else {
            QString why;
            linePts = lineAt(frameUs, false, &why);
            if (linePts.isEmpty()) addNote(r, QStringLiteral("No %1: %2").arg(what, why));
            // An address line with no line of this moment beside it would be a comparison with one side
            // missing; it is left off and the note above already says why. The address line is the SAME
            // camera's reading at the address instant, drawn on this frame: the camera does not move, so the
            // two lines share one picture's coordinates.
            if (rq.addressLine && !linePts.isEmpty()) {
                const std::optional<qint64> a = positionInstantUs(ctx.analysis, QStringLiteral("P1"), ctx.t0);
                QString awhy = QStringLiteral("this swing has no address position.");
                if (a) addrPts = lineAt(stream.tUs[std::max(0, nearestFrame(stream.tUs, *a))], true, &awhy);
                if (addrPts.isEmpty()) addNote(r, QStringLiteral("No address line: %1").arg(awhy));
            }
        }
    }

    // ── The crop ──────────────────────────────────────────────────────────────────────────────────────
    // A request that names its rect (StillRequest::cropPx: the posture picture drew its lines in it) is cut
    // there; else the golfer's crop, fitted to the aspect asked.
    double padX = 0, padY = 0;
    QRectF srcN;
    QRect srcPx;
    if (rq.cropPx.isValid()) {
        srcPx = rq.cropPx.intersected(QRect(0, 0, W, H));
        srcN = QRectF(double(srcPx.x()) / W, double(srcPx.y()) / H, double(srcPx.width()) / W, double(srcPx.height()) / H);
    } else {
        QString cropNote;
        const QRectF cropN = stillGolferCrop(pose, bandFor(ctx, cam), &cropNote);
        addNote(r, cropNote);
        srcN = fitAspect(cropN, rq.aspect, double(W) / double(H), &padX, &padY);
        srcPx = stillCropPixels(srcN, W, H);
    }
    if (srcPx.width() < 8 || srcPx.height() < 8) { r.error = QStringLiteral("the golfer's crop is empty"); return r; }

    QImage crop = QImage(bgr.data, W, H, int(bgr.step), QImage::Format_BGR888).copy(srcPx)
                      .convertToFormat(QImage::Format_RGB888);

    // ── Exposure, on the crop alone ───────────────────────────────────────────────────────────────────
    std::array<quint32, 256> hist{};
    for (int y = 0; y < crop.height(); ++y) {
        const uchar *row = crop.constScanLine(y);
        for (int x = 0; x < crop.width(); ++x) {
            const uchar *px = row + 3 * x;
            ++hist[(299 * px[0] + 587 * px[1] + 114 * px[2] + 500) / 1000];
        }
    }
    const std::array<uint8_t, 256> lut = exposureCurve(hist);
    for (int y = 0; y < crop.height(); ++y) {
        uchar *row = crop.scanLine(y);
        for (int x = 0; x < 3 * crop.width(); ++x) row[x] = lut[row[x]];
    }

    // ── Size and padding ──────────────────────────────────────────────────────────────────────────────
    const double outWsrc = srcPx.width() * (1.0 + padX), outHsrc = srcPx.height() * (1.0 + padY);
    const int maxPx = rq.maxPx > 16 ? rq.maxPx : 1400;
    const double s = double(maxPx) / std::max(outWsrc, outHsrc);
    const int outW = std::max(1, int(std::lround(outWsrc * s))), outH = std::max(1, int(std::lround(outHsrc * s)));
    const int imgW = std::max(1, int(std::lround(srcPx.width() * s))), imgH = std::max(1, int(std::lround(srcPx.height() * s)));
    const int offX = (outW - imgW) / 2, offY = (outH - imgH) / 2;

    QImage out(outW, outH, QImage::Format_RGB32);
    {
        // The pad is a pillarbox in the crop's own dark tone (its 5th-percentile luminance after the lift):
        // the studio's dark bay continued, with the picture's edges feathered into it over 4% of its width.
        // Tried and dropped (review sheets, 20 Oct): the mean colour of the crop's edges, which read as a grey
        // slab beside a lit mat, and a row-by-row continuation of the edge colours, which read as a smear.
        // Only the crop's own pixels feed it, so nothing outside the privacy bound can appear.
        quint64 acc = 0, tot = 0; int dark = 0;
        for (int v = 0; v < 256; ++v) tot += hist[v];
        for (int v = 0; v < 256; ++v) { acc += hist[v]; if (acc * 20 >= tot) { dark = lut[v]; break; } }
        const QColor pad(dark, dark, dark);
        out.fill(pad);
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect(offX, offY, imgW, imgH),
                    crop.scaled(imgW, imgH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        const int feather = std::max(2, int(std::lround(0.04 * std::min(imgW, imgH))));
        auto fade = [&](QRect r, Qt::Orientation o, bool rising) {
            QLinearGradient g(o == Qt::Horizontal ? QPointF(r.left(), 0) : QPointF(0, r.top()),
                              o == Qt::Horizontal ? QPointF(r.right() + 1, 0) : QPointF(0, r.bottom() + 1));
            QColor a = pad; a.setAlpha(rising ? 0 : 255);
            QColor b = pad; b.setAlpha(rising ? 255 : 0);
            g.setColorAt(0, a); g.setColorAt(1, b);
            p.fillRect(r, g);
        };
        if (offX > 0) {
            fade(QRect(offX, offY, feather, imgH), Qt::Horizontal, false);
            fade(QRect(offX + imgW - feather, offY, feather, imgH), Qt::Horizontal, true);
        }
        if (offY > 0) {
            fade(QRect(offX, offY, imgW, feather), Qt::Vertical, false);
            fade(QRect(offX, offY + imgH - feather, imgW, feather), Qt::Vertical, true);
        }

        // ── The lines ─────────────────────────────────────────────────────────────────────────────────
        // The address line is drawn a little finer than this one.
        // Sized for PRINT: a still sits about 4 cm wide on the A4 page, so the line is ~1% of the
        // still's short edge (9 px on a 935 px still, ~0.5 mm printed). 4 px at a 1400 px long edge
        // read as a hairline on paper (20 Oct review), however right it looked on screen.
        const double lw = std::max(3.0, 0.0095 * double(std::min(outW, outH)));
        auto toOut = [&](const QPointF &n) {
            return QPointF(offX + (n.x() * W - srcPx.left()) * s, offY + (n.y() * H - srcPx.top()) * s);
        };
        auto mapped = [&](const QVector<QPointF> &pts) { QVector<QPointF> o; for (const QPointF &q : pts) o << toOut(q); return o; };
        p.setRenderHint(QPainter::Antialiasing);
        // Lines stop at the picture: a clubhead outside the golfer's crop (the shaft at P2, a long club at
        // the top) must not run on across the pad as if the picture went on.
        p.setClipRect(QRect(offX, offY, imgW, imgH));
        if (addrPts.size() >= 2) drawDashed(p, mapped(addrPts), lw * 0.85);
        if (linePts.size() >= 2) drawSolid(p, mapped(linePts), lw);
    }

    // ── Write ─────────────────────────────────────────────────────────────────────────────────────────
    QDir().mkpath(QFileInfo(rq.outPath).absolutePath());
    QSaveFile f(rq.outPath);
    if (!f.open(QIODevice::WriteOnly) || !out.save(&f, "JPG", 88) || !f.commit()) {
        r.error = QStringLiteral("could not write %1").arg(rq.outPath);
        return r;
    }
    r.ok = true;
    r.path = rq.outPath;
    r.frameIndex = idx;
    r.crop = srcN;
    r.lineDrawn = linePts.size() >= 2;
    r.addressDrawn = addrPts.size() >= 2;
    return r;
}

} // namespace

ReportStills::ReportStills(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<StillResult>();
    qRegisterMetaType<QVector<StillResult>>();
}

QVector<StillResult> ReportStills::renderSwing(const QVector<StillRequest> &requests)
{
    QVector<StillResult> results(requests.size());
    if (requests.isEmpty()) return results;

    // Requests are grouped by swing (renderAll does that); a mixed list is still served, one swing at a time.
    QHash<QString, QVector<int>> bySwing;
    QStringList order;
    for (int i = 0; i < requests.size(); ++i) {
        const QString d = QDir::cleanPath(requests[i].swingDir);
        if (!bySwing.contains(d)) order << d;
        bySwing[d] << i;
    }
    for (const QString &dir : order) {
        SwingContext ctx;
        ctx.dir = dir;
        QString err;
        ctx.root = pinpoint::SwingStore::load(dir, &err);
        if (ctx.root.isEmpty()) {
            for (int i : bySwing[dir])
                results[i].error = QStringLiteral("could not read the swing document%1").arg(err.isEmpty() ? QString() : QStringLiteral(": ") + err);
            continue;
        }
        ctx.analysis = ctx.root.value(QStringLiteral("analysis")).toObject();
        ctx.t0 = qint64(ctx.root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
        ctx.leadIsLeft = leadIsLeftFor(ctx.root.value(QStringLiteral("athlete")).toObject()
                                           .value(QStringLiteral("handedness")).toString());
        // Each camera decoded forward: the requests sorted by their instant, so P1 → P4 → P7 is one pass.
        QVector<int> idx = bySwing[dir];
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            const auto ta = positionInstantUs(ctx.analysis, requests[a].phase, ctx.t0);
            const auto tb = positionInstantUs(ctx.analysis, requests[b].phase, ctx.t0);
            return ta.value_or(0) < tb.value_or(0);
        });
        for (int i : idx) {
            results[i] = renderInContext(ctx, requests[i]);
            if (!results[i].ok)
                ppWarn() << "[ReportStills]" << dir << requests[i].camera << requests[i].phase << "—" << results[i].error;
        }
    }
    return results;
}

StillResult ReportStills::renderOne(const StillRequest &request)
{
    return renderSwing({ request }).value(0);
}

void ReportStills::renderAll(const QVector<StillRequest> &requests)
{
    // One task per swing (its document read once, each camera decoded forward), on a small private pool:
    // each FFmpeg decoder already runs its own threads, so more than three swings at once only contends.
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 3, 1, 3));
        return p;
    }();

    QHash<QString, QVector<int>> bySwing;
    QStringList order;
    for (int i = 0; i < requests.size(); ++i) {
        const QString d = QDir::cleanPath(requests[i].swingDir);
        if (!bySwing.contains(d)) order << d;
        bySwing[d] << i;
    }
    struct Job { QVector<int> idx; QVector<StillRequest> reqs; };
    QVector<Job> jobs;
    for (const QString &d : order) {
        Job j;
        j.idx = bySwing[d];
        for (int i : j.idx) j.reqs << requests[i];
        jobs << j;
    }
    const int n = requests.size();
    if (n == 0) {
        QMetaObject::invokeMethod(this, [this] { emit finished({}); }, Qt::QueuedConnection);
        return;
    }
    auto *watcher = new QFutureWatcher<QVector<StillResult>>(this);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    connect(watcher, &QFutureWatcher<QVector<StillResult>>::finished, this, [this, watcher, jobs, n, timer] {
        QVector<StillResult> all(n);
        const QList<QVector<StillResult>> parts = watcher->future().results();
        int ok = 0;
        for (int j = 0; j < parts.size() && j < jobs.size(); ++j)
            for (int k = 0; k < jobs[j].idx.size() && k < parts[j].size(); ++k) {
                all[jobs[j].idx[k]] = parts[j][k];
                ok += parts[j][k].ok ? 1 : 0;
            }
        ppInfo() << "[ReportStills]" << ok << "of" << n << "stills made from" << jobs.size() << "swings in"
                 << timer->elapsed() << "ms";
        watcher->deleteLater();
        emit finished(all);
    });
    watcher->setFuture(QtConcurrent::mapped(pool, jobs, [](const Job &j) { return ReportStills::renderSwing(j.reqs); }));
}
