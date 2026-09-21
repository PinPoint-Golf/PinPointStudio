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

#pragma once

// The down-the-line shaft track as JSON — schema `pinpoint.clubDtl/1`
// (dtl_shaft_tracker_design.md §5.11). ONE builder for both writers: SwingLab's
// club_dtl.json and swing.json's `analysis.clubDtl`, so the file a corpus run
// grades and the block the app persists cannot drift apart.
//
// DETERMINISTIC by construction: nothing time-measured goes in, so one track
// always produces the same bytes (SwingLab's G2 gate). NaN is written as JSON
// null, never as a NaN/inf token: those are not JSON, half the readers accept
// them silently, and "absent" has to survive the round trip as absent.
//
// Times follow serializeAnalysis's rule: a value ≥ t0Us is made relative to it,
// an already-relative one passes through; t0Us = 0 keeps absolute times.
//
// The track builder is INLINE and this header is OpenCV-free on purpose: the
// swing.json writer (swing_doc.cpp) is linked into targets that carry neither
// OpenCV nor the tracker, and it holds only the track. The config echo is the one
// part that needs DtlShaftConfig, so it is computed where the track is produced
// (dtlShaftConfigJson → DtlShaftTrack2D::configJson / configHash) and the
// DtlShaftConfig overload lives in dtl_shaft_json.cpp.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <cstdint>
#include <vector>

#include "analysis_versions.h"   // kDtlShaftStageVersion
#include "dtl_shaft_track.h"     // DtlShaftTrack2D (OpenCV-free)

namespace pinpoint::analysis {

struct DtlShaftConfig;   // dtl_shaft_config.h

// The resolved scalars echoed into the `config` block.
QJsonObject dtlShaftConfigJson(const DtlShaftConfig& cfg);

// THE CONTRACT: `cfg` is echoed into `config` and hashed into summary.configHash —
// pass the config the track was produced with. `streamAlias` / `streamFile` name the
// DTL stream the track is OF, as swing.json's streams[] names it. Equivalent to the
// overload below with dtlShaftConfigJson(cfg) and dtlConfigHash(cfg).
QJsonObject dtlShaftTrackToJson(const DtlShaftTrack2D& track, int64_t t0Us,
                                const DtlShaftConfig& cfg,
                                const QString& streamAlias, const QString& streamFile);

// The same bytes from a config block and hash already in hand (a track's own
// configJson / configHash). Lifted verbatim from swinglab_run's --dtl block
// (2026-09-21), where club_dtl.json was first written; SwingLab's G2 gate is that
// its file did not move by a byte.
inline QJsonObject dtlShaftTrackToJson(const DtlShaftTrack2D& track, int64_t t0Us,
                                       const QJsonObject& config, const QString& configHash,
                                       const QString& streamAlias, const QString& streamFile)
{
    // Same domain rule as SwingDocWriter's rel(): the analysis times go out
    // window-relative, and an already-relative value passes through.
    const qint64 t0 = qint64(t0Us);
    const auto rel = [t0](int64_t t) -> qint64 {
        const qint64 tt = qint64(t);
        return tt >= t0 ? tt - t0 : tt;
    };
    // Local, not M_PI: that macro needs _USE_MATH_DEFINES on MSVC.
    constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
    const auto jnum = [](double v) -> QJsonValue {
        return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
    };
    const auto jpt = [&](const QPointF &p, double iw, double ih) -> QJsonValue {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) return QJsonValue(QJsonValue::Null);
        return QJsonValue(QJsonArray{ p.x() * iw, p.y() * ih });
    };
    const double iw = track.frameWidth  > 0 ? 1.0 / track.frameWidth  : 0.0;
    const double ih = track.frameHeight > 0 ? 1.0 / track.frameHeight : 0.0;

    QJsonArray frames;
    std::vector<int> bandN(track.bands.size(), 0), bandPub(track.bands.size(), 0);
    for (const DtlSample &s : track.samples) {
        if (s.band >= 0 && s.band < int(bandN.size())) {
            ++bandN[size_t(s.band)];
            if (s.tier >= DtlTier::Ray) ++bandPub[size_t(s.band)];
        }
        QJsonValue corridor = QJsonValue(QJsonValue::Null);
        if (s.corridorOn || std::isfinite(s.corrCentreDeg[0]))
            corridor = QJsonObject{
                { "centres", QJsonArray{ jnum(s.corrCentreDeg[0] * kDeg2Rad),
                                         jnum(s.corrCentreDeg[1] * kDeg2Rad) } },
                { "half",    jnum(s.corrHalfDeg * kDeg2Rad) },
                { "on",      s.corridorOn } };
        frames.append(QJsonObject{
            { "t_us",    rel(s.t_us) },
            { "tier",    QString::fromLatin1(dtlTierName(s.tier)) },
            { "grip",    jpt(s.gripPx, iw, ih) },
            { "head",    jpt(s.headPx, iw, ih) },
            { "theta",   jnum(s.thetaRad) },
            { "lenPx",   jnum(s.lenPx) },
            // "snapLine" | "latBand" | "rend" — WHERE the run was
            // measured. "rend" is the DP's own ridge-score argmax,
            // which sits at its own 98 px floor whenever the ray
            // from the pose grip leaves the shaft early; the other
            // two measured off the re-registered line. A length
            // with no stated provenance is a number.
            { "lenSrc",  QString::fromLatin1(dtlLenSrcName(s.lenSrc)) },
            // "ev" | "lineConf" — WHICH gate let this frame
            // publish. EV is read along a ray from the POSE grip,
            // which sits tens of px off the shaft axis; lineConf
            // is the support under the RE-REGISTERED line the
            // frame actually publishes. A published frame that
            // came through the second is a different claim from
            // one that came through the first.
            { "evSrc",   QString::fromLatin1(dtlEvSrcName(s.evSrc)) },
            { "conf",    double(s.conf) },
            { "rhoPred", jnum(s.rhoPred) },
            // "measured" | "bound" | "none" — a ρ̂_D built on the
            // ρ_F := 1 bound is a different claim from one built
            // on a measured face-on length, and address and impact
            // are ALL bound. A report that cannot tell them apart
            // cannot say what the schedule is standing on.
            { "rhoSrc",  QString::fromLatin1(dtlRhoSrcName(s.rhoSrc)) },
            { "corridor", corridor },
            { "escape",  s.corridorEscape },
            { "band",    s.band },
            { "reason",  s.reason } });
    }
    QJsonArray bands;
    QJsonObject coverageByBand;
    for (size_t b = 0; b < track.bands.size(); ++b) {
        const DtlBand &d = track.bands[b];
        bands.append(QJsonObject{ { "lo_us", rel(d.loUs) },
                                  { "hi_us", rel(d.hiUs) },
                                  { "name",  d.name } });
        coverageByBand[d.name] = bandN[b] ? double(bandPub[b]) / double(bandN[b]) : 0.0;
    }
    return QJsonObject{
        { "schema",        "pinpoint.clubDtl/1" },
        // The producer's version (analysis_versions.h), echoed so the
        // first CHANGE to the producer can be told from the first run of
        // it in a file that travels without its swing.json.
        { "stageVersion",  kDtlShaftStageVersion },
        { "stream",        QJsonObject{ { "alias", streamAlias }, { "file", streamFile } } },
        { "frameWidth",    track.frameWidth },
        { "frameHeight",   track.frameHeight },
        { "clockOffsetUs", double(track.clockOffsetUs) },
        { "config",        config },
        { "frames",        frames },
        { "bands",         bands },
        // The DTL band-lock truth is generated WITHOUT face-on
        // (§6) and is a separate instrument; empty here on purpose
        // rather than filled from this run's own band locks, which
        // would be the tracker grading itself.
        { "truth",         QJsonArray{} },
        { "summary",       QJsonObject{
            { "sightedFrac",      track.sightedFrac },
            { "coverageByBand",   coverageByBand },
            { "publishedInEndOn", track.publishedInEndOn },
            // Additive: `source` says WHICH cue answered, and the
            // shadow cue's own numbers are echoed whether or not
            // it supplied the verdict, so a scene that carries
            // both can be read off one file.
            { "ball", QJsonObject{ { "found",  track.ball.found },
                                   { "x",      jnum(track.ball.x) },
                                   { "y",      jnum(track.ball.y) },
                                   { "reason", track.ball.reason },
                                   { "source", track.ball.source },
                                   { "shadowX",     jnum(track.ball.shadowX) },
                                   { "shadowY",     jnum(track.ball.shadowY) },
                                   { "launchRise",  jnum(track.ball.launchRise) },
                                   { "radiusPx",    jnum(track.ball.radiusPx) },
                                   { "nCandidates", track.ball.nCandidates } } },
            { "lFullPx",     jnum(track.lFullPx) },
            { "lFullSource", track.lFullSource },
            { "rowFitA",     jnum(track.rowFitA) },
            { "rowFitB",     jnum(track.rowFitB) },
            // Which rule placed the club-away window the clean
            // plate's low region and the shadow ball cue are both
            // built from. On a swing whose ladder is short it is a
            // FALLBACK, and "the scene changed" and "the ladder
            // was short" are different findings.
            { "clubAwayWindow", track.clubAwayWindow },
            // Which configuration produced these numbers. "The
            // numbers moved" and "the config moved" are different
            // findings, and a run whose settings live only in a
            // shell history cannot tell them apart.
            { "configHash",  configHash } } } };
}

// The DTL stream's alias and file as swing.json's streams[] names them, matched
// on the recorded serial (DtlShaftTrack2D::streamSerial / the window's
// device_serial). An empty serial takes the first video stream — SwingLab's rule
// since the first club_dtl.json, kept so its bytes do not move. Both out-params
// are left untouched when nothing matches.
inline void dtlStreamName(const QJsonObject& manifestRoot, const QString& serial,
                          QString* alias, QString* file)
{
    for (const QJsonValue &sv : manifestRoot[QStringLiteral("streams")].toArray()) {
        const QJsonObject s = sv.toObject();
        if (s[QStringLiteral("kind")].toString() != QLatin1String("video")) continue;
        const QString rec = s[QStringLiteral("source")].toObject()[QStringLiteral("serial")].toString();
        if (!serial.isEmpty() && rec != serial) continue;
        if (alias) *alias = s[QStringLiteral("alias")].toString();
        if (file)  *file  = s[QStringLiteral("file")].toString();
        return;
    }
}

} // namespace pinpoint::analysis
