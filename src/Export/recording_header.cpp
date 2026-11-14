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

#include "recording_header.h"

#include "swing_exporter.h"
#include "../Core/club_standards.h"
#include "../Core/club_vocabulary.h"

namespace pinpoint {

namespace rec = pinpoint::recording;

namespace {

// "pinpoint.<kind>/<n>" → n; 0 when the tag is absent or is not of that kind.
int schemaVersionOf(const QJsonValue &tag, QLatin1String kind)
{
    const QString s = tag.toString();
    const QString prefix = QStringLiteral("pinpoint.") + kind + QLatin1Char('/');
    if (!s.startsWith(prefix))
        return 0;
    bool ok = false;
    const int v = s.mid(prefix.size()).toInt(&ok);
    return ok && v > 0 ? v : 0;
}

QString handednessName(rec::Handedness h)
{
    switch (h) {
    case rec::Handedness::Right: return QStringLiteral("Right");
    case rec::Handedness::Left:  return QStringLiteral("Left");
    default:                     return QStringLiteral("unknown");
    }
}

QString clubSetName(ClubSet s)
{
    switch (s) {
    case ClubSet::Women:  return QStringLiteral("women");
    case ClubSet::Junior: return QStringLiteral("junior");
    default:              return QStringLiteral("men");
    }
}

ClubSet clubSetFromName(const QString &s)
{
    if (s == QLatin1String("women"))  return ClubSet::Women;
    if (s == QLatin1String("junior")) return ClubSet::Junior;
    return ClubSet::Men;
}

QJsonArray doubles(const std::vector<double> &v)
{
    QJsonArray a;
    for (double d : v) a.append(d);
    return a;
}

std::vector<double> doublesFrom(const QJsonArray &a)
{
    std::vector<double> v;
    v.reserve(size_t(a.size()));
    for (const QJsonValue &x : a) v.push_back(x.toDouble());
    return v;
}

QJsonObject geometryJson(const rec::ClubGeometry &g)
{
    return QJsonObject{
        { QStringLiteral("lengthMm"),        g.lengthMm },
        { QStringLiteral("lengthKnown"),     g.lengthKnown },
        { QStringLiteral("shaftType"),       g.shaftType },
        { QStringLiteral("bandCentersMm"),   doubles(g.bandCentersMm) },
        { QStringLiteral("hoselFromButtMm"), g.hoselFromButtMm },
        { QStringLiteral("shaftLengthMm"),   g.shaftLengthMm },
        { QStringLiteral("handsEndMm"),      g.handsEndMm },
    };
}

rec::ClubGeometry geometryFromJson(const QJsonObject &o)
{
    rec::ClubGeometry g;
    g.lengthMm        = o.value(QStringLiteral("lengthMm")).toDouble(0.0);
    g.lengthKnown     = o.value(QStringLiteral("lengthKnown")).toBool(g.lengthMm > 0.0);
    g.shaftType       = o.value(QStringLiteral("shaftType")).toString();
    g.bandCentersMm   = doublesFrom(o.value(QStringLiteral("bandCentersMm")).toArray());
    g.hoselFromButtMm = o.value(QStringLiteral("hoselFromButtMm")).toDouble(0.0);
    g.shaftLengthMm   = o.value(QStringLiteral("shaftLengthMm")).toDouble(0.0);
    g.handsEndMm      = o.value(QStringLiteral("handsEndMm")).toDouble(0.0);
    return g;
}

// The standard length of `clubName` AS STORED (trimmed, compared case-insensitively — today's
// re-analysis rule), for the set and height; 0 when it names no standard club.
double standardLengthOf(const QString &clubName, ClubSet set, double heightM)
{
    const ClubStandard st = standardClub(clubName.trimmed(), set, heightM);
    return st.valid ? double(st.lengthMm) : 0.0;
}

} // namespace

int swingDocSchemaVersionOf(const QJsonObject &root)
{
    return schemaVersionOf(root.value(QStringLiteral("schema")), QLatin1String("swing"));
}

// ── The one header writer ─────────────────────────────────────────────────────────────────────

QJsonObject recordingHeaderJson(const RecordingHeader &h, const QJsonArray &streams)
{
    QJsonObject root;
    root[QStringLiteral("schema")] = QString::fromLatin1(kSwingDocSchema);
    if (h.swing)
        root[QStringLiteral("swing")] = QJsonObject{
            { QStringLiteral("index"), h.swing->index },
            { QStringLiteral("id"),    h.swing->id },
        };
    if (h.athlete) {
        QJsonObject a{
            { QStringLiteral("name"), h.athlete->name },
            { QStringLiteral("uuid"), h.athlete->uuid },
        };
        if (h.athlete->handedness)
            a[QStringLiteral("handedness")] = *h.athlete->handedness;
        // Older readers' copy of the height (the analysis reads analysisContext). Absent when unknown.
        if (h.athlete->heightM > 0.0)
            a[QStringLiteral("heightM")] = h.athlete->heightM;
        root[QStringLiteral("athlete")] = a;
    }
    if (h.sessionDir)
        root[QStringLiteral("session")] = QJsonObject{ { QStringLiteral("dir"), *h.sessionDir } };
    if (h.clock)
        root[QStringLiteral("clock")] = QJsonObject{
            { QStringLiteral("t0_us"),     static_cast<qint64>(h.clock->t0Us) },
            { QStringLiteral("unit"),      QStringLiteral("us") },
            { QStringLiteral("wallclock"), h.clock->wallclock.toString(Qt::ISODateWithMs) },
        };
    if (h.window)
        root[QStringLiteral("window")] = QJsonObject{
            { QStringLiteral("start_us"), static_cast<qint64>(h.window->startUs) },
            { QStringLiteral("end_us"),   static_cast<qint64>(h.window->endUs) },
        };
    if (h.thumbnail)
        root[QStringLiteral("thumbnail")] = QJsonObject{
            { QStringLiteral("file"), h.thumbnail->file },
            { QStringLiteral("t_us"), static_cast<qint64>(h.thumbnail->tUs) },
        };
    if (!h.capture.isEmpty())
        root[QStringLiteral("capture")] = h.capture;
    if (h.analysisContext)
        root[QStringLiteral("analysisContext")] = analysisContextJson(*h.analysisContext);
    root[QStringLiteral("streams")] = streams;
    return root;
}

RecordingHeader recordingHeaderFor(const SwingExportJob &job, int64_t t0Us, int64_t durationUs)
{
    RecordingHeader h;
    h.swing   = RecordingHeader::Identity{ job.swingId, job.swingIndex };
    h.athlete = RecordingHeader::Athlete{ job.athleteName, job.athleteUuid, job.handedness, job.athleteHeightM };
    h.sessionDir = job.sessionId;
    // Wallclock: the anchor was snapshotted when the window was captured, i.e. at monotonic
    // ~endTimestampUs(); subtracting the window duration gives an honest (ms-accurate) wallclock
    // for t0.
    h.clock  = RecordingHeader::Clock{ t0Us, job.wallclockAnchorUtc.addMSecs(-durationUs / 1000) };
    h.window = RecordingHeader::Window{ 0, durationUs };
    h.capture = captureBlockJson(job);
    h.analysisContext = job.analysisContext;
    return h;
}

QJsonObject captureBlockJson(const SwingExportJob &job)
{
    QJsonObject cap{
        {QStringLiteral("sessionType"), job.sessionType},
        {QStringLiteral("shotSource"),  job.shotSource},
        // Window-relative impact (-1 = unknown) — the re-analysis impact reference,
        // present even for analysis-skipped corpus swings (no analysis.phases block).
        {QStringLiteral("impactUs"),    static_cast<qint64>(job.impactUs)},
        {QStringLiteral("swingDetectionSensitivity"), job.swingDetectionSensitivity},
        {QStringLiteral("motionCaptureQuality"),      job.motionCaptureQuality},
        {QStringLiteral("latencyUs"), QJsonObject{
            {QStringLiteral("imuBle"),      static_cast<qint64>(job.imuBleLatencyUs)},
            {QStringLiteral("audioDevice"), job.audioDeviceLatencyUs},
            // Presence marks a travel-corrected anchor (see SwingExportJob).
            {QStringLiteral("micTravel"),   static_cast<qint64>(job.micTravelUs)},
        }},
        {QStringLiteral("host"), QJsonObject{
            {QStringLiteral("app"),         QStringLiteral("PinPointStudio")},
            {QStringLiteral("version"),     job.host.appVersion},
            {QStringLiteral("gitSha"),      job.host.gitSha},
            {QStringLiteral("hostname"),    job.host.hostname},
            {QStringLiteral("platform"),    job.host.platform},
            {QStringLiteral("poseBackend"), job.host.poseBackend},
        }},
    };
    // Club geometry (shaft-tracker E1 band matcher) — persisted so re-analysis of a document
    // older than analysisContext recovers it, and kept for provenance and older readers (the
    // analysis reads analysisContext). Omitted when unresolved (untaped/no active club). clubName
    // alone is enough to write the block: an untaped club with no recorded geometry still carries
    // the length-prior identity (club_length_fusion.h).
    if (job.clubLengthM > 0.0 || !job.bandCentersMm.empty() || !job.clubName.isEmpty()) {
        QJsonArray bands;
        for (double b : job.bandCentersMm) bands.append(b);
        QJsonObject club{
            {QStringLiteral("lengthMm"),        job.clubLengthM * 1000.0},
            {QStringLiteral("shaftType"),       job.shaftType},
            {QStringLiteral("bandCentersMm"),   bands},
            // Additive (Phase A5): hosel offset from the butt, mm. 0 = unknown —
            // absent on swings captured before this field existed.
            {QStringLiteral("hoselFromButtMm"), job.hoselFromButtMm},
            // Additive (markerless P4): exposed shaft length and the hands' end on
            // the grip, mm. 0 = unknown — absent on swings captured before.
            {QStringLiteral("shaftLengthMm"),   job.shaftLengthMm},
            {QStringLiteral("handsEndMm"),      job.handsEndMm},
            // Additive (club-length fusion): canonical club-vocabulary id — half
            // the persistent prior key (athleteUuid|clubName|cameraKey).
            {QStringLiteral("name"),            job.clubName},
        };
        // The prior the live analysis fuse ACTUALLY used for this shot (state
        // BEFORE this shot's update) — re-analysis replays it for a byte-exact
        // fuse instead of reading AppSettings (deterministic, cross-host).
        // Omitted when no prior joined (first shot / cold key / camera unfixed).
        if (job.priorClubLenN > 0 && job.priorClubLenPx > 0.0)
            club[QStringLiteral("lengthPrior")] = QJsonObject{
                {QStringLiteral("px"),    job.priorClubLenPx},
                {QStringLiteral("varPx"), job.priorClubLenVarPx},
                {QStringLiteral("n"),     job.priorClubLenN},
            };
        cap[QStringLiteral("club")] = club;
    }
    return cap;
}

// ── The analysis context: capture side ────────────────────────────────────────────────────────

rec::ClubGeometry clubGeometryFromRecord(const QVariantMap &r)
{
    rec::ClubGeometry g;
    g.lengthMm    = r.value(QStringLiteral("lengthMm")).toDouble();
    if (!(g.lengthMm > 0.0)) g.lengthMm = 0.0;
    g.lengthKnown = g.lengthMm > 0.0;
    g.shaftType   = r.value(QStringLiteral("shaftType")).toString();
    for (const QVariant &b : r.value(QStringLiteral("bandCentersMm")).toList())
        g.bandCentersMm.push_back(b.toDouble());
    const double hosel = r.value(QStringLiteral("hoselFromButtMm")).toDouble();
    g.hoselFromButtMm = hosel > 0.0 ? hosel : 0.0;
    g.shaftLengthMm   = r.value(QStringLiteral("shaftLengthMm")).toDouble();
    g.handsEndMm      = r.value(QStringLiteral("handsEndMm")).toDouble();
    return g;
}

rec::AnalysisContext captureAnalysisContext(const SwingExportJob &job, const QString &sex,
                                            const QString &dobIso, const QDate &on,
                                            const QVariantMap &bagRecords)
{
    rec::AnalysisContext c;
    c.handedness = rec::handednessFromName(job.handedness);
    c.heightM    = job.athleteHeightM > 0.0 ? job.athleteHeightM : 0.0;
    c.clubSet    = clubSetOn(sex, QDate::fromString(dobIso, Qt::ISODate), on);

    // The captured club: what the session was on, with the geometry and prior the export job
    // carries — the same values capture.club records. Empty = undeclared, never the stub.
    c.club.name = job.clubName.trimmed();
    rec::ClubGeometry &g = c.club.geometry;
    g.lengthMm        = job.clubLengthM > 0.0 ? job.clubLengthM * 1000.0 : 0.0;
    g.lengthKnown     = g.lengthMm > 0.0;
    g.shaftType       = job.shaftType;
    g.bandCentersMm   = job.bandCentersMm;
    g.hoselFromButtMm = job.hoselFromButtMm;
    g.shaftLengthMm   = job.shaftLengthMm;
    g.handsEndMm      = job.handsEndMm;
    if (job.priorClubLenN > 0 && job.priorClubLenPx > 0.0) {
        c.club.lengthPrior.px    = job.priorClubLenPx;
        c.club.lengthPrior.varPx = job.priorClubLenVarPx;
        c.club.lengthPrior.n     = job.priorClubLenN;
    }
    c.club.standardLengthMm = standardLengthOf(c.club.name, c.clubSet, c.heightM);

    for (auto it = bagRecords.cbegin(); it != bagRecords.cend(); ++it) {
        const QString id = canonicalClubId(it.key());
        if (!id.isEmpty())
            c.bag.insert(id, clubGeometryFromRecord(it.value().toMap()));
    }
    return c;
}

// ── The analysis context: on disk ─────────────────────────────────────────────────────────────

QJsonObject analysisContextJson(const rec::AnalysisContext &c)
{
    QJsonObject club = geometryJson(c.club.geometry);
    club[QStringLiteral("name")]             = c.club.name;
    club[QStringLiteral("standardLengthMm")] = c.club.standardLengthMm;
    if (c.club.lengthPrior.present())
        club[QStringLiteral("lengthPrior")] = QJsonObject{
            { QStringLiteral("px"),    c.club.lengthPrior.px },
            { QStringLiteral("varPx"), c.club.lengthPrior.varPx },
            { QStringLiteral("n"),     c.club.lengthPrior.n },
        };
    QJsonObject bag;
    for (auto it = c.bag.cbegin(); it != c.bag.cend(); ++it)
        bag[it.key()] = geometryJson(it.value());
    return QJsonObject{
        { QStringLiteral("schema"),  QString::fromLatin1(kAnalysisContextSchema) },
        { QStringLiteral("athlete"), QJsonObject{
              { QStringLiteral("handedness"), handednessName(c.handedness) },
              { QStringLiteral("heightM"),    c.heightM },
              { QStringLiteral("clubSet"),    clubSetName(c.clubSet) },
          } },
        { QStringLiteral("club"), club },
        { QStringLiteral("bag"),  bag },
    };
}

rec::AnalysisContext analysisContextFromJson(const QJsonObject &block)
{
    rec::AnalysisContext c;
    const QJsonObject a = block.value(QStringLiteral("athlete")).toObject();
    c.handedness = rec::handednessFromName(a.value(QStringLiteral("handedness")).toString());
    c.heightM    = a.value(QStringLiteral("heightM")).toDouble(0.0);
    c.clubSet    = clubSetFromName(a.value(QStringLiteral("clubSet")).toString());

    const QJsonObject club = block.value(QStringLiteral("club")).toObject();
    c.club.name             = club.value(QStringLiteral("name")).toString();
    c.club.geometry         = geometryFromJson(club);
    c.club.standardLengthMm = club.value(QStringLiteral("standardLengthMm")).toDouble(0.0);
    const QJsonObject prior = club.value(QStringLiteral("lengthPrior")).toObject();
    if (!prior.isEmpty()) {
        c.club.lengthPrior.px    = prior.value(QStringLiteral("px")).toDouble(-1.0);
        c.club.lengthPrior.varPx = prior.value(QStringLiteral("varPx")).toDouble(-1.0);
        c.club.lengthPrior.n     = prior.value(QStringLiteral("n")).toInt(0);
    }
    const QJsonObject bag = block.value(QStringLiteral("bag")).toObject();
    for (auto it = bag.constBegin(); it != bag.constEnd(); ++it)
        c.bag.insert(it.key(), geometryFromJson(it.value().toObject()));
    return c;
}

rec::AnalysisContext legacyAnalysisContext(const QJsonObject &root)
{
    rec::AnalysisContext c;
    const QJsonObject ath = root.value(QStringLiteral("athlete")).toObject();
    c.handedness = rec::handednessFromName(ath.value(QStringLiteral("handedness")).toString());
    c.heightM    = ath.value(QStringLiteral("heightM")).toDouble(0.0);
    // The club set as today's re-analysis derived it: athlete.sex and athlete.dob on the session's
    // date (the first 10 characters of session.dir). No writer has ever put sex or dob in a
    // document, so in practice this is the men's set — "unknown → men's, as today".
    // The session's date is read from its folder name, as today's re-analysis did (A3, step 2's
    // SessionIdentity is the one place).
    const QString sessionDir = root.value(QStringLiteral("session")).toObject().value(QStringLiteral("dir")).toString();
    const QDate on = QDate::fromString(sessionDir.left(10), Qt::ISODate);
    c.clubSet = clubSetOn(ath.value(QStringLiteral("sex")).toString(),
                          QDate::fromString(ath.value(QStringLiteral("dob")).toString(), Qt::ISODate), on);

    const QJsonObject capture = root.value(QStringLiteral("capture")).toObject();
    const QJsonObject clubIn  = capture.value(QStringLiteral("club")).toObject();
    // The captured club name, TRIMMED and otherwise as stored (the standard length below is read
    // from exactly this spelling, as today's swingDocDeclaredClub does).
    c.club.name = clubIn.value(QStringLiteral("name")).toString().trimmed();
    if (!clubIn.isEmpty()) {
        rec::ClubGeometry &g = c.club.geometry;
        const double lmm = clubIn.value(QStringLiteral("lengthMm")).toDouble(0.0);
        g.lengthMm        = lmm > 0.0 ? lmm : 0.0;
        g.lengthKnown     = lmm > 0.0;
        g.shaftType       = clubIn.value(QStringLiteral("shaftType")).toString();
        g.hoselFromButtMm = clubIn.value(QStringLiteral("hoselFromButtMm")).toDouble(0.0);
        g.shaftLengthMm   = clubIn.value(QStringLiteral("shaftLengthMm")).toDouble(0.0);
        g.handsEndMm      = clubIn.value(QStringLiteral("handsEndMm")).toDouble(0.0);
        g.bandCentersMm   = doublesFrom(clubIn.value(QStringLiteral("bandCentersMm")).toArray());

        // The RECORDED prior — the state the live fuse actually used for THIS shot. Absent block
        // (or no updates folded in) ⇒ the shot ran prior-free.
        const QJsonObject priorIn = clubIn.value(QStringLiteral("lengthPrior")).toObject();
        const double px = priorIn.value(QStringLiteral("px")).toDouble(-1.0);
        const int    n  = priorIn.value(QStringLiteral("n")).toInt(0);
        if (px > 0.0 && n > 0) {
            c.club.lengthPrior.px    = px;
            c.club.lengthPrior.varPx = priorIn.value(QStringLiteral("varPx")).toDouble(-1.0);
            c.club.lengthPrior.n     = n;
        }
    }
    // The recorded standard length: the trimmed captured name's, only when it names a standard club
    // (HANDOFF, 1.1 review: the empty-popover case then matches today exactly).
    c.club.standardLengthMm = standardLengthOf(c.club.name, c.clubSet, c.heightM);
    return c;
}

AnalysisContextRead readAnalysisContext(const QJsonObject &root)
{
    AnalysisContextRead out;
    const int docVersion = swingDocSchemaVersionOf(root);
    if (docVersion > kSwingDocSchemaVersion) {
        out.readOnly = true;
        out.readOnlyReason = QStringLiteral("this shot was saved by a newer PinPoint Studio "
                                            "(swing schema %1, this build reads up to %2)")
                                 .arg(docVersion).arg(kSwingDocSchemaVersion);
    }

    const QJsonValue blockValue = root.value(QStringLiteral("analysisContext"));
    if (!blockValue.isObject()) {
        out.context = legacyAnalysisContext(root);
        return out;
    }
    const QJsonObject block = blockValue.toObject();
    const int v = schemaVersionOf(block.value(QStringLiteral("schema")), QLatin1String("analysisContext"));
    if (v == 0) {
        // A block this build cannot place: not trusted as the recorded context, and the document is
        // not written back over it.
        out.context  = legacyAnalysisContext(root);
        out.readOnly = true;
        if (out.readOnlyReason.isEmpty())
            out.readOnlyReason = QStringLiteral("this shot's analysis context has no schema this build reads");
        return out;
    }
    out.context  = analysisContextFromJson(block);
    out.recorded = true;
    if (v > kAnalysisContextSchemaVersion) {
        out.readOnly = true;
        if (out.readOnlyReason.isEmpty())
            out.readOnlyReason = QStringLiteral("this shot's analysis context was saved by a newer PinPoint "
                                                "Studio (analysisContext schema %1, this build reads up to %2)")
                                     .arg(v).arg(kAnalysisContextSchemaVersion);
    }
    return out;
}

analysis::ClubReview clubReviewOf(const QJsonObject &root)
{
    const QJsonObject review = root.value(QStringLiteral("review")).toObject();
    analysis::ClubReview r;
    r.correction = review.value(QStringLiteral("clubOverride")).toString();
    r.declared   = review.value(QStringLiteral("club")).toString();
    return r;
}

} // namespace pinpoint
