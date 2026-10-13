/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "lm_club_art.h"

#include "../../Analysis/lm_club_evidence.h"
#include "../../Core/pp_debug.h"

#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

const QString kRoot = QStringLiteral(":/lm/");

bool knownHead(const QString &head)
{
    return head == QLatin1String("driver") || head == QLatin1String("3wood")
        || head == QLatin1String("hybrid") || head == QLatin1String("iron")
        || head == QLatin1String("wedge");
}

QJsonObject readJson(const QString &qrcPath)
{
    QFile f(qrcPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// Scale every number in a (possibly nested) JSON array of numbers — a point, a bbox, a
// 2×4 matrix, a polyline of points. `keepTail` leaves the last N entries of each innermost
// array alone, for the parting line's [x, y, visible] rows.
QJsonValue scaled(const QJsonValue &v, double k, int keepTail = 0)
{
    if (v.isDouble())
        return v.toDouble() * k;
    if (!v.isArray())
        return v;
    const QJsonArray a = v.toArray();
    const bool innermost = !a.isEmpty() && !a.first().isArray();
    QJsonArray out;
    for (int i = 0; i < a.size(); ++i) {
        if (innermost && keepTail > 0 && i >= a.size() - keepTail)
            out.append(a.at(i));
        else
            out.append(scaled(a.at(i), k, keepTail));
    }
    return out;
}

} // namespace

LmClubArt::LmClubArt(QObject *parent)
    : QObject(parent)
{
}

QString LmClubArt::dirFor(const QString &head)
{
    return head == QLatin1String("3wood") ? QStringLiteral("fairway") : head;
}

QString LmClubArt::headFor(const QString &club) const
{
    return pinpoint::analysis::lmClubHead(club);
}

QUrl LmClubArt::image(const QString &head, const QString &view, const QString &look) const
{
    if (!knownHead(head))
        return {};
    const QString d = dirFor(head);
    const QString rel = QStringLiteral("clubheads/%1/%1_%2_%3.png").arg(d, view, look);
    if (!QFile::exists(kRoot + rel))
        return {};                       // no back view for a wood; an unknown look
    return QUrl(QStringLiteral("qrc:/lm/") + rel);
}

QSize LmClubArt::vendoredSize(const QString &qrcPath) const
{
    // The header only — QImageReader::size() does not decode the pixels.
    QImageReader r(qrcPath);
    return r.size();
}

QVariantMap LmClubArt::anchors(const QString &head, const QString &view) const
{
    if (!knownHead(head))
        return {};
    const QString key = head + QLatin1Char('/') + view;
    if (const auto it = m_anchorCache.constFind(key); it != m_anchorCache.constEnd())
        return *it;

    const QString d = dirFor(head);
    QJsonObject a = readJson(kRoot + QStringLiteral("clubheads/%1/%1_%2.json").arg(d, view));
    if (a.isEmpty()) {
        m_anchorCache.insert(key, {});
        return {};
    }

    // The scale from the anchors' image to the one vendored, measured from the PNG itself.
    const int srcW = a.value(QStringLiteral("image")).toObject().value(QStringLiteral("width")).toInt();
    const QSize vend = vendoredSize(kRoot + QStringLiteral("clubheads/%1/%1_%2_light.png").arg(d, view));
    double k = 1.0;
    if (srcW > 0 && vend.isValid() && vend.width() > 0)
        k = double(vend.width()) / double(srcW);
    else
        ppWarn() << "[LmClubArt] cannot size the vendored render for" << key << "— anchors left at source scale";

    if (k != 1.0) {
        a[QStringLiteral("image")] = QJsonObject{ { QStringLiteral("width"),  vend.width() },
                                                  { QStringLiteral("height"), vend.height() } };
        a[QStringLiteral("mm_per_px")] = a.value(QStringLiteral("mm_per_px")).toDouble() / k;
        // Every field that is a pixel coordinate or a pixel length.
        static const char *kPixelFields[] = {
            "world_to_px", "face_centre", "face_plane", "shaft_axis", "ground_y",
            "sole_contact", "head_bbox", "body_bbox", "alpha_bbox", "pivot",
        };
        for (const char *f : kPixelFields) {
            const QString fk = QString::fromLatin1(f);
            if (a.contains(fk) && !a.value(fk).isNull())
                a[fk] = scaled(a.value(fk), k);
        }
        QJsonObject le = a.value(QStringLiteral("leading_edge")).toObject();
        for (const QString &p : le.keys())
            le[p] = scaled(le.value(p), k);
        if (!le.isEmpty())
            a[QStringLiteral("leading_edge")] = le;
        QJsonObject ff = a.value(QStringLiteral("face_frame")).toObject();
        if (!ff.isEmpty()) {
            for (const char *f : { "origin", "x_px_per_mm", "y_px_per_mm" })
                ff[QString::fromLatin1(f)] = scaled(ff.value(QString::fromLatin1(f)), k);
            a[QStringLiteral("face_frame")] = ff;
        }
    }
    a.remove(QStringLiteral("render_seconds"));   // the renderer's own bookkeeping
    a[QStringLiteral("head")] = head;

    const QVariantMap out = a.toVariantMap();
    m_anchorCache.insert(key, out);
    return out;
}

// FORESIGHT'S STRIKE ORIGIN, measured up the face plane from our render's face_centre.
//
// A GC Quad reports strike height from Foresight's own face centre, which for an iron is the
// point midway between the 6th and 7th score lines counted up from the sole. Our render's
// `face_centre` is the modelled nominal impact point, which sits elsewhere, so a strike has to
// be shifted by the difference before it is drawn through face_frame (whose +y runs up the
// face plane). From ~/Projects/Resources/clubheads/src/clubs.py, CLUBS (Resources b6c3457):
//
//   7 iron  grooves v0 = 8.3 mm, pitch 3.56 mm (v measured up the face plane from the sole):
//           lines 6 and 7 at 8.3 + 5 × 3.56 = 26.10 and 29.66 → midway 27.88 mm.
//           fc = (−3.0, 18.0): 18.0 mm VERTICAL, i.e. 18.0 / cos 34° = 21.71 mm up the face.
//           27.88 − 21.71 = +6.17 mm.
//   wedge   grooves v0 = 4.6, pitch 2.95: lines 6–7 midway at 4.6 + 5.5 × 2.95 = 20.83 mm.
//           fc = (−5.0, 12.0): 12.0 / cos 56° = 21.46 mm. 20.83 − 21.46 = −0.63 mm.
//   woods   the middle of the score-line field: driver rows −10 / 0 / +10 → 0; the 3 wood's
//           lines (z 8.13–28.01) and the hybrid's (z 8.17–23.84) come out at −0.14 and +0.01,
//           which the contract rounds to 0 — a seventh of a millimetre is below anything a
//           strike reading or a 5 mm marker can show.
double LmClubArt::strikeOriginMm(const QString &head) const
{
    if (head == QLatin1String("iron"))  return 6.17;
    if (head == QLatin1String("wedge")) return -0.63;
    return 0.0;
}

// FORESIGHT'S CLUB MARKERS, per head, in face_frame mm from our face_centre.
//
// THE LAYOUT is Foresight Sports' club-fiducial (club marker) guide for its club-data units: the
// four-dot devices (GC Quad, QuadMAX, Falcon, GCHawk) take two dots on the horizontal centre
// line of the face, one in from each end of the scoring area (toe, heel), and two more on the
// toe side at the top and bottom of the strike zone (hi, lo); the single-dot devices (GC3, an
// HMT on its own) take one dot at the top of the strike zone above face centre. Every dot is a
// 5 mm sticker, placed about Foresight's face centre — so every y below carries
// strikeOriginMm(head), the shift from our face_centre to theirs.
//
// THE NUMBERS are the guide's placements measured on OUR heads, from Resources' clubs.py
// (b6c3457) groove and score-line geometry — the table signed off in the mock and carried in
// PpLmGraphicsBody.qml's faceMarks, moved here so the panel draws markers from one source:
//   toe / heel  one 5 mm marker in from the ends of the score line nearest the centre. Score-line
//               ends (face_frame x from fc): 7 iron grooves uc −3.1 ± 24.9 → +25.0 / −24.8;
//               wedge uc −4.45 ± 24.45 → +23.9 / −25.0; driver row ends |fu| = 24.1 + 17 = 41.1;
//               3 wood line ends +29.6 / −28.9; hybrid +29.1 / −30.0. Markers (signed off):
//               iron +20.0 / −19.8, wedge +18.9 / −20.0, driver ±36.1, 3 wood +24.6 / −23.9,
//               hybrid +24.1 / −25.0.
//   lo / hi     the bottom and top of the strike zone about the Foresight centre: iron and wedge
//               the groove field half a marker in, ±17.1 / ±13.7; woods the outer score rows,
//               driver ±12.5, 3 wood ±10.3, hybrid ±8.45.
QVariantList LmClubArt::markers(const QString &head) const
{
    struct Marks { double toe, heel, lo, hi; };
    Marks m{ 20.0, -19.8, -17.1, 17.1 };                       // iron (and anything unknown)
    if (head == QLatin1String("driver"))      m = { 36.1, -36.1, -12.5, 12.5 };
    else if (head == QLatin1String("3wood"))  m = { 24.6, -23.9, -10.3, 10.3 };
    else if (head == QLatin1String("hybrid")) m = { 24.1, -25.0, -8.45, 8.45 };
    else if (head == QLatin1String("wedge"))  m = { 18.9, -20.0, -13.7, 13.7 };
    const double sc = strikeOriginMm(head);
    auto dot = [](const char *name, double x, double y) {
        return QVariantMap{ { QStringLiteral("name"), QString::fromLatin1(name) },
                            { QStringLiteral("xMm"),  x },
                            { QStringLiteral("yMm"),  y } };
    };
    return QVariantList{
        dot("toe",  m.toe,  sc),
        dot("heel", m.heel, sc),
        dot("hi",   m.toe,  sc + m.hi),
        dot("lo",   m.toe,  sc + m.lo),
        dot("one",  0.0,    sc + m.hi),
    };
}

QUrl LmClubArt::ballImage(const QString &look) const
{
    const QString rel = QStringLiteral("ball/ball_side_%1.png").arg(look);
    if (!QFile::exists(kRoot + rel))
        return {};
    return QUrl(QStringLiteral("qrc:/lm/") + rel);
}

QVariantMap LmClubArt::ballAnchors() const
{
    static const QString key = QStringLiteral("ball/side");
    if (const auto it = m_anchorCache.constFind(key); it != m_anchorCache.constEnd())
        return *it;
    QJsonObject a = readJson(kRoot + QStringLiteral("ball/ball_side.json"));
    if (a.isEmpty())
        return {};
    const int srcW = a.value(QStringLiteral("image")).toObject().value(QStringLiteral("width")).toInt();
    const QSize vend = vendoredSize(kRoot + QStringLiteral("ball/ball_side_light.png"));
    if (srcW > 0 && vend.isValid() && vend.width() != srcW) {
        const double k = double(vend.width()) / double(srcW);
        a[QStringLiteral("image")] = QJsonObject{ { QStringLiteral("width"),  vend.width() },
                                                  { QStringLiteral("height"), vend.height() } };
        a[QStringLiteral("mm_per_px")] = a.value(QStringLiteral("mm_per_px")).toDouble() / k;
        a[QStringLiteral("centre")]    = scaled(a.value(QStringLiteral("centre")), k);
        a[QStringLiteral("radius_px")] = a.value(QStringLiteral("radius_px")).toDouble() * k;
        QJsonObject pl = a.value(QStringLiteral("parting_line")).toObject();
        if (pl.contains(QStringLiteral("polyline"))) {
            pl[QStringLiteral("polyline")] = scaled(pl.value(QStringLiteral("polyline")), k, /*keepTail*/ 1);
            a[QStringLiteral("parting_line")] = pl;
        }
    }
    const QVariantMap out = a.toVariantMap();
    m_anchorCache.insert(key, out);
    return out;
}

QUrl LmClubArt::patchImage(int mm) const
{
    if (mm != 20 && mm != 25 && mm != 30)
        return {};
    return QUrl(QStringLiteral("qrc:/lm/ball/patch_%1mm.svg").arg(mm));
}

QString LmClubArt::patchPath(int mm) const
{
    if (mm != 20 && mm != 25 && mm != 30)
        return {};
    if (const auto it = m_patchPathCache.constFind(mm); it != m_patchPathCache.constEnd())
        return *it;
    QFile f(kRoot + QStringLiteral("ball/patch_%1mm.svg").arg(mm));
    QString d;
    if (f.open(QIODevice::ReadOnly)) {
        // The file is one <path> with one d="…" (Resources ball/README.md, "Using the impact
        // patch"), so the first d attribute is the patch.
        static const QRegularExpression re(QStringLiteral("\\sd=\"([^\"]+)\""));
        const QRegularExpressionMatch m = re.match(QString::fromUtf8(f.readAll()));
        if (m.hasMatch())
            d = m.captured(1);
    }
    m_patchPathCache.insert(mm, d);
    return d;
}

int LmClubArt::patchMm(const QString &head) const
{
    return (head == QLatin1String("iron") || head == QLatin1String("wedge")) ? 20 : 25;
}
