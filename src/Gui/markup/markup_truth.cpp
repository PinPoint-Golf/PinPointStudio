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

#include "markup_truth.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::markup {

namespace {

double roundDp(double v, int dp)
{
    const double m = std::pow(10.0, dp);
    return std::round(v * m) / m;
}

QString truthPath(const QString &swingDir) { return QDir(swingDir).filePath(QStringLiteral("truth.json")); }

QJsonObject loadObject(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

} // namespace

FaceOnInfo readFaceOn(const QString &swingDir, const QString &faceNeedle)
{
    FaceOnInfo fo;
    const QJsonObject root = loadObject(QDir(swingDir).filePath(QStringLiteral("swing.json")));
    if (root.isEmpty()) return fo;

    QVector<QJsonObject> videos;
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &v : streams) {
        const QJsonObject s = v.toObject();
        if (s.value(QStringLiteral("kind")).toString() == QLatin1String("video")) videos.push_back(s);
    }
    if (videos.isEmpty()) return fo;

    // Face-on selection: perspective == 2, else alias contains the needle, else first.
    QJsonObject pick;
    bool found = false;
    for (const QJsonObject &s : videos) {
        if (s.value(QStringLiteral("setup")).toObject().value(QStringLiteral("perspective")).toInt(-1) == 2) {
            pick = s; found = true; break;
        }
    }
    if (!found) {
        for (const QJsonObject &s : videos) {
            if (s.value(QStringLiteral("alias")).toString().contains(faceNeedle, Qt::CaseInsensitive)) {
                pick = s; found = true; break;
            }
        }
    }
    if (!found) pick = videos.first();

    fo.alias     = pick.value(QStringLiteral("alias")).toString();
    fo.videoFile = pick.value(QStringLiteral("file")).toString();
    const QJsonObject src = pick.value(QStringLiteral("source")).toObject();
    fo.srcWidth  = src.value(QStringLiteral("width")).toInt();
    fo.srcHeight = src.value(QStringLiteral("height")).toInt();

    const QJsonArray tarr = pick.value(QStringLiteral("frames")).toObject()
                                .value(QStringLiteral("t_us")).toArray();
    fo.frameTimesUs.reserve(tarr.size());
    for (const QJsonValue &t : tarr) fo.frameTimesUs.push_back(t.toInteger());

    fo.ok = !fo.frameTimesUs.isEmpty() && fo.srcWidth > 0 && fo.srcHeight > 0;
    return fo;
}

int frameIndexForUs(const FaceOnInfo &fo, qint64 us)
{
    const QVector<qint64> &v = fo.frameTimesUs;
    if (v.isEmpty()) return -1;
    auto it = std::lower_bound(v.begin(), v.end(), us);
    if (it == v.begin()) return 0;
    if (it == v.end())   return int(v.size()) - 1;
    const int hi = int(it - v.begin());
    const int lo = hi - 1;
    return (us - v[lo] <= v[hi] - us) ? lo : hi;
}

QJsonObject toJson(const TruthDoc &doc, const FaceOnInfo &fo)
{
    QJsonObject root;
    QJsonArray  shaftArr;
    const qint64 t0 = fo.frameTimesUs.isEmpty() ? 0 : fo.frameTimesUs.first();

    for (auto it = doc.shaft.constBegin(); it != doc.shaft.constEnd(); ++it) {
        const int idx = it.key();
        if (idx < 0 || idx >= fo.frameTimesUs.size()) continue;
        const ShaftLabel &L = it.value();
        const double gpx = L.gripNx * fo.srcWidth,  gpy = L.gripNy * fo.srcHeight;
        const double hpx = L.headNx * fo.srcWidth,  hpy = L.headNy * fo.srcHeight;

        QJsonObject f;
        f.insert(QStringLiteral("t_us"),  fo.frameTimesUs[idx]);
        f.insert(QStringLiteral("theta"), roundDp(std::atan2(hpy - gpy, hpx - gpx), 5));
        f.insert(QStringLiteral("grip"),  QJsonArray{ gpx, gpy });
        f.insert(QStringLiteral("head"),  QJsonArray{ hpx, hpy });
        f.insert(QStringLiteral("len"),   roundDp(std::hypot(hpx - gpx, hpy - gpy), 1));
        shaftArr.append(f);
    }

    QJsonObject ev;
    ev.insert(QStringLiteral("t0_us"), t0);
    for (auto it = doc.events.constBegin(); it != doc.events.constEnd(); ++it) {
        const int idx = it.value();
        if (idx < 0 || idx >= fo.frameTimesUs.size()) continue;
        const double s = double(fo.frameTimesUs[idx] - t0) / 1e6;
        ev.insert(it.key() + QStringLiteral("_s"), roundDp(s, 4));
    }

    root.insert(QStringLiteral("shaft"),  shaftArr);
    root.insert(QStringLiteral("events"), ev);
    return root;
}

bool writeTruth(const QString &swingDir, const TruthDoc &doc, const FaceOnInfo &fo, QString *error)
{
    const QJsonObject root = toJson(doc, fo);
    QSaveFile f(truthPath(swingDir));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

TruthDoc readTruth(const QString &swingDir, const FaceOnInfo &fo)
{
    TruthDoc doc;
    const QJsonObject root = loadObject(truthPath(swingDir));
    if (root.isEmpty()) return doc;

    const double W = fo.srcWidth  > 0 ? fo.srcWidth  : 1.0;
    const double H = fo.srcHeight > 0 ? fo.srcHeight : 1.0;

    const QJsonArray shaftArr = root.value(QStringLiteral("shaft")).toArray();
    for (const QJsonValue &v : shaftArr) {
        const QJsonObject s = v.toObject();
        const int idx = frameIndexForUs(fo, s.value(QStringLiteral("t_us")).toInteger());
        if (idx < 0) continue;
        const QJsonArray g = s.value(QStringLiteral("grip")).toArray();
        const QJsonArray h = s.value(QStringLiteral("head")).toArray();
        if (g.size() < 2 || h.size() < 2) continue;
        ShaftLabel L;
        L.gripNx = g[0].toDouble() / W; L.gripNy = g[1].toDouble() / H;
        L.headNx = h[0].toDouble() / W; L.headNy = h[1].toDouble() / H;
        doc.shaft.insert(idx, L);
    }

    const QJsonObject ev = root.value(QStringLiteral("events")).toObject();
    const qint64 t0 = ev.value(QStringLiteral("t0_us"))
                        .toInteger(fo.frameTimesUs.isEmpty() ? 0 : fo.frameTimesUs.first());
    for (const QString &name : eventNames()) {
        const QString key = name + QStringLiteral("_s");
        if (!ev.contains(key)) continue;
        const qint64 tu = t0 + qint64(std::llround(ev.value(key).toDouble() * 1e6));
        const int idx = frameIndexForUs(fo, tu);
        if (idx >= 0) doc.events.insert(name, idx);
    }
    // Legacy vocabulary (address/top/impact/finish) → P-positions, for old fixtures.
    const auto aliases = legacyEventAliases();
    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        if (doc.events.contains(it.value())) continue;          // P-key already read
        const QString key = it.key() + QStringLiteral("_s");
        if (!ev.contains(key)) continue;
        const qint64 tu = t0 + qint64(std::llround(ev.value(key).toDouble() * 1e6));
        const int idx = frameIndexForUs(fo, tu);
        if (idx >= 0) doc.events.insert(it.value(), idx);
    }
    return doc;
}

TruthSummary summarize(const QString &swingDir)
{
    TruthSummary out;
    const QJsonObject root = loadObject(truthPath(swingDir));
    if (root.isEmpty()) return out;
    out.exists     = true;
    out.shaftCount = root.value(QStringLiteral("shaft")).toArray().size();
    const QJsonObject ev = root.value(QStringLiteral("events")).toObject();
    for (const QString &k : ev.keys())
        if (k != QLatin1String("t0_us") && k.endsWith(QLatin1String("_s"))) ++out.eventCount;
    return out;
}

PoseTrack readPose2d(const QString &swingDir)
{
    PoseTrack track;
    const QJsonObject root = loadObject(QDir(swingDir).filePath(QStringLiteral("swing.json")));
    if (root.isEmpty()) return track;

    const qint64 t0 = root.value(QStringLiteral("clock")).toObject()
                          .value(QStringLiteral("t0_us")).toInteger();
    const QJsonArray frames = root.value(QStringLiteral("analysis")).toObject()
                                  .value(QStringLiteral("pose2d")).toObject()
                                  .value(QStringLiteral("frames")).toArray();
    if (frames.isEmpty()) return track;

    track.frames.reserve(frames.size());
    for (const QJsonValue &fv : frames) {
        const QJsonObject f = fv.toObject();
        PoseFrame pf;
        pf.relUs = f.value(QStringLiteral("t_us")).toInteger() - t0;   // -> window-relative
        const QJsonArray kp = f.value(QStringLiteral("kp")).toArray();
        pf.kp.reserve(kp.size());
        for (const QJsonValue &x : kp) pf.kp.push_back(float(x.toDouble()));
        const QJsonArray lead = f.value(QStringLiteral("lead")).toArray();
        if (lead.size() >= 2) { pf.leadX = lead[0].toDouble(); pf.leadY = lead[1].toDouble(); pf.hasLead = true; }
        const QJsonArray trail = f.value(QStringLiteral("trail")).toArray();
        if (trail.size() >= 2) { pf.trailX = trail[0].toDouble(); pf.trailY = trail[1].toDouble(); pf.hasTrail = true; }
        pf.handConf = float(f.value(QStringLiteral("handConf")).toDouble());
        track.frames.push_back(pf);
    }
    std::sort(track.frames.begin(), track.frames.end(),
              [](const PoseFrame &a, const PoseFrame &b) { return a.relUs < b.relUs; });
    track.ok = !track.frames.isEmpty();
    return track;
}

int poseIndexForUs(const PoseTrack &track, qint64 relUs)
{
    if (track.frames.isEmpty()) return -1;
    int best = 0;
    qint64 bestD = std::numeric_limits<qint64>::max();
    for (int i = 0; i < track.frames.size(); ++i) {
        const qint64 d = std::llabs(track.frames[i].relUs - relUs);
        if (d < bestD) { bestD = d; best = i; }
        else if (track.frames[i].relUs > relUs) break;   // ascending: past nearest
    }
    return best;
}

} // namespace pinpoint::markup
