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

// THE REPORT'S POSTURE PAGE — the map's `posture` block (docs/design/posture_chart_design.md §5.7, stage 3b;
// build/report-build/REPORT_MAP.md `posture`), cut from the Charts Posture presenter's SESSION payload
// (charts/charts_posture_presenter.h postureSessionReportPayload: the same numbers and words the screen draws,
// nothing picked, every sentence counting "swings" — the report's word). Pure and header-only, so report_model_test proves its shape, caps and absence without a
// session behind it; SessionReportModel feeds it the payload it read for the session and the report's own
// swing numbers.
//
// WHAT IT DOES, AND ONLY THIS:
//   · ABSENT (null) when the payload is not available or carries no profile row (§9.1 rule 6: no page, no head);
//   · keeps the presenter's sentences and figures VERBATIM (kicker, head, pattern line, the profile's sub and
//     caption, every row and cell, the strip's line and legend, each departure's line and detail, the
//     repeatability words, the trust lines) — the page is the screen's reading, not a second one;
//   · renumbers the strip's marks to the REPORT's swing numbers (the presenter's ordinals count the shots it
//     read; the report numbers the swings the diagnostics read, in folder order) by swing folder;
//   · caps the lists (rule 4) and words the "N more"; drops what has nothing to say ("What moved" without a
//     line, an empty repeatability line), so the register draws only what is there;
//   · adds the ONE sentence the screen sets in QML and the report may not (rule 7): the departures' sub.
//
//   · THE PICTURE (stage 5, §5.4): the presenter's picture of the session's typical swing, its caption numbered as the
//     report numbers that swing ("A typical swing: swing 4"), each frame's coach lines cut to the picture's default sets
//     (paper has no Coach lines toggle: the posture and head-and-hips lines), the rest verbatim. The report model makes
//     the stills (each frame's `still`) and fills `src`; null when no frame shows, with `pictureWhy` saying why.
//
//  posture: {
//    kicker ("10 swings  ·  7 iron  ·  Posture"), head (the session's head line), patternLine ("" none),
//    profile: { positions: [{ tag, name, available }] (8), rows: [ROW, the presenter's, unchanged] (≤ 6), sub, caption },
//    moved: [{ tag, when ("At the top"), line }] (≤ 2: the top, then impact; only those with a line),
//    strip: { marks: [{ swing, ok, differs, between, word }] (every shot read, ≤ kPostureStripCap), line, legend,
//             more ("" | "N more swings not shown") } | null (no mark),
//    departures: [{ id, name, detail, line, shots }] (≤ kPostureDepartureCap), departuresMore ("" | "N more"),
//    departuresSub,
//    address: { title ("At address"), sub, legend, rows: [ADDRESS, the presenter's, unchanged] (≤ kPostureAddressCap) } | null
//             (no reading the capture can produce: no block) — under the profile, above What moved; its trust lines are
//             among `trust`,
//    repeat ("" none), trust: [string] (≤ kPostureTrustCap), trustMore ("" | "N more"),
//    picture: { typical ("A typical swing: swing 4"), typicalWhy, legend, why: [string], swingDir,
//               groups: [{ camera, label, frames: [{ key, camera, tag, when, caption, note, width, height, src, imageWhy,
//                                                    still, marks: [MARK, the default sets only] }] }] } | null,
//    pictureWhy: [string] (why there is no picture; empty with one) }
#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace pinpoint::report {

constexpr int kPostureDepartureCap = 6;     // the core names at most seven departures (D1–D7)
constexpr int kPostureTrustCap = 16;        // cameras, rows not measured, the ⚠ reasons, sources, datum, and the address
                                            // block's (rig rule, set-up key per camera, its bands' source, what is not read)
constexpr int kPostureAddressCap = 8;       // the pack's rig-sensitive address readings (six today)
constexpr int kPostureStripCap = 120;       // marks; the page sets them 30 to a line (four lines)
constexpr int kPostureRowCap = 6;           // head, hips, bend, lead knee, trail knee, pelvis height

inline QString postureMoreWords(int more, const char *noun = nullptr)
{
    if (more <= 0) return QString();
    if (!noun) return QStringLiteral("%1 more").arg(more);
    return QStringLiteral("%1 more %2 not shown").arg(more).arg(QString::fromLatin1(noun) + (more == 1 ? QString() : QStringLiteral("s")));
}

// `swingNoOfDir`: QDir::cleanPath(swing folder) → the report's swing number (1-based). A shot the report does not
// number keeps the presenter's ordinal.
inline QVariant postureReportBlock(const QVariantMap &payload, const QHash<QString, int> &swingNoOfDir)
{
    const QVariant none = QVariant::fromValue(nullptr);
    if (!payload.value(QStringLiteral("available")).toBool()) return none;
    const QVariantMap prof = payload.value(QStringLiteral("profile")).toMap();
    QVariantList rows;
    for (const QVariant &r : prof.value(QStringLiteral("rows")).toList()) {
        if (rows.size() >= kPostureRowCap) break;
        const QVariantMap rm = r.toMap();
        if (rm.contains(QStringLiteral("present")) && !rm.value(QStringLiteral("present")).toBool()) continue;
        rows.append(rm);
    }
    if (rows.isEmpty()) return none;

    QVariantMap out;
    out.insert(QStringLiteral("kicker"), payload.value(QStringLiteral("kicker")).toString());
    out.insert(QStringLiteral("head"), payload.value(QStringLiteral("headText")).toString());
    out.insert(QStringLiteral("patternLine"), payload.value(QStringLiteral("patternLine")).toString());

    QVariantList positions;
    for (const QVariant &p : prof.value(QStringLiteral("positions")).toList()) {
        const QVariantMap pm = p.toMap();
        positions.append(QVariantMap{ { QStringLiteral("tag"), pm.value(QStringLiteral("tag")) },
                                      { QStringLiteral("name"), pm.value(QStringLiteral("name")) },
                                      { QStringLiteral("available"), pm.value(QStringLiteral("available")) } });
    }
    out.insert(QStringLiteral("profile"), QVariantMap{ { QStringLiteral("positions"), positions }, { QStringLiteral("rows"), rows },
                                                       { QStringLiteral("sub"), prof.value(QStringLiteral("sub")).toString() },
                                                       { QStringLiteral("caption"), prof.value(QStringLiteral("caption")).toString() } });

    // the "At address" block (§7.4.2): the presenter's rows verbatim, in the report's noun already
    {
        const QVariantMap ad = payload.value(QStringLiteral("address")).toMap();
        QVariantList arows;
        for (const QVariant &r : ad.value(QStringLiteral("rows")).toList()) {
            if (arows.size() >= kPostureAddressCap) break;
            arows.append(r);
        }
        out.insert(QStringLiteral("address"), arows.isEmpty() ? none
            : QVariant(QVariantMap{ { QStringLiteral("title"), ad.value(QStringLiteral("title")).toString() },
                                    { QStringLiteral("sub"), ad.value(QStringLiteral("sub")).toString() },
                                    { QStringLiteral("legend"), ad.value(QStringLiteral("legend")).toString() },
                                    { QStringLiteral("rows"), arows } }));
    }

    // what moved: the top, then impact, each only with a line
    QVariantList moved;
    const QVariantMap wm = payload.value(QStringLiteral("whatMoved")).toMap();
    for (const char *k : { "top", "impact" }) {
        const QVariantMap m = wm.value(QString::fromLatin1(k)).toMap();
        const QString line = m.value(QStringLiteral("line")).toString();
        if (line.trimmed().isEmpty()) continue;
        moved.append(QVariantMap{ { QStringLiteral("tag"), m.value(QStringLiteral("tag")) }, { QStringLiteral("when"), m.value(QStringLiteral("when")).toString() },
                                  { QStringLiteral("line"), line } });
    }
    out.insert(QStringLiteral("moved"), moved);

    // the pattern by shot, numbered as the report numbers its swings
    const QVariantMap st = payload.value(QStringLiteral("strip")).toMap();
    const QVariantList allMarks = st.value(QStringLiteral("marks")).toList();
    QVariantList marks;
    for (const QVariant &v : allMarks) {
        if (marks.size() >= kPostureStripCap) break;
        const QVariantMap m = v.toMap();
        const QString dir = QDir::cleanPath(m.value(QStringLiteral("swingDir")).toString());
        const int swing = swingNoOfDir.value(dir, m.value(QStringLiteral("ordinal")).toInt());
        marks.append(QVariantMap{ { QStringLiteral("swing"), swing }, { QStringLiteral("ok"), m.value(QStringLiteral("ok")).toBool() },
                                  { QStringLiteral("differs"), m.value(QStringLiteral("differs")).toBool() },
                                  { QStringLiteral("between"), m.value(QStringLiteral("between")).toBool() },
                                  { QStringLiteral("word"), m.value(QStringLiteral("word")).toString() } });
    }
    out.insert(QStringLiteral("strip"), marks.isEmpty() ? none
        : QVariant(QVariantMap{ { QStringLiteral("marks"), marks }, { QStringLiteral("line"), st.value(QStringLiteral("line")).toString() },
                                { QStringLiteral("legend"), st.value(QStringLiteral("legend")).toString() },
                                { QStringLiteral("more"), postureMoreWords(int(allMarks.size() - marks.size()), "swing") } }));

    // the departures, as counts across the shots
    const QVariantList allDeps = payload.value(QStringLiteral("departures")).toList();
    QVariantList deps;
    for (const QVariant &v : allDeps) {
        if (deps.size() >= kPostureDepartureCap) break;
        const QVariantMap d = v.toMap();
        deps.append(QVariantMap{ { QStringLiteral("id"), d.value(QStringLiteral("id")) }, { QStringLiteral("name"), d.value(QStringLiteral("name")).toString() },
                                 { QStringLiteral("detail"), d.value(QStringLiteral("detail")).toString() },
                                 { QStringLiteral("line"), d.value(QStringLiteral("line")).toString() },
                                 { QStringLiteral("shots"), d.value(QStringLiteral("shots")).toInt() } });
    }
    out.insert(QStringLiteral("departures"), deps);
    out.insert(QStringLiteral("departuresMore"), postureMoreWords(int(allDeps.size() - deps.size())));
    out.insert(QStringLiteral("departuresSub"), deps.isEmpty() ? QString()
               : QStringLiteral("Each fault that took a reading out of the band: on how many swings, and the position it starts from."));

    out.insert(QStringLiteral("repeat"), payload.value(QStringLiteral("repeat")).toMap().value(QStringLiteral("words")).toString());

    const QStringList allTrust = payload.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
    QStringList trust;
    int trustMore = 0;
    for (const QString &l : allTrust) {
        if (l.trimmed().isEmpty()) continue;
        if (trust.size() >= kPostureTrustCap) ++trustMore;
        else trust << l;
    }
    out.insert(QStringLiteral("trust"), trust);
    out.insert(QStringLiteral("trustMore"), postureMoreWords(trustMore));

    // the picture strip of the session's typical swing
    const QVariantMap pic = payload.value(QStringLiteral("picture")).toMap();
    const QStringList picWhy = pic.value(QStringLiteral("why")).toStringList();
    if (pic.value(QStringLiteral("available")).toBool()) {
        const QString dir = QDir::cleanPath(pic.value(QStringLiteral("swingDir")).toString());
        const int swing = swingNoOfDir.value(dir, pic.value(QStringLiteral("ordinal")).toInt());
        const QVariantMap sets = pic.value(QStringLiteral("defaultSets")).toMap();
        QVariantList groups;
        for (const QVariant &gv : pic.value(QStringLiteral("groups")).toList()) {
            const QVariantMap g = gv.toMap();
            QVariantList frames;
            for (const QVariant &fv : g.value(QStringLiteral("frames")).toList()) {
                const QVariantMap f = fv.toMap();
                QVariantList marks;
                for (const QVariant &mv : f.value(QStringLiteral("marks")).toList()) {
                    const QVariantMap m = mv.toMap();
                    if (m.value(QStringLiteral("role")).toString() == QLatin1String("coach")
                        && !sets.value(m.value(QStringLiteral("set")).toString()).toBool()) continue;
                    marks.append(m);
                }
                QVariantMap o;
                for (const char *k : { "key", "camera", "tag", "when", "caption", "note", "width", "height", "src", "imageWhy", "still" })
                    o.insert(QString::fromLatin1(k), f.value(QString::fromLatin1(k)));
                o.insert(QStringLiteral("marks"), marks);
                frames.append(o);
            }
            groups.append(QVariantMap{ { QStringLiteral("camera"), g.value(QStringLiteral("camera")) },
                                       { QStringLiteral("label"), g.value(QStringLiteral("label")) }, { QStringLiteral("frames"), frames } });
        }
        out.insert(QStringLiteral("picture"), QVariantMap{
            { QStringLiteral("typical"), QStringLiteral("A typical swing: swing %1").arg(swing) },
            { QStringLiteral("typicalWhy"), pic.value(QStringLiteral("typicalWhy")).toString() },
            { QStringLiteral("legend"), pic.value(QStringLiteral("legend")).toString() },
            { QStringLiteral("why"), picWhy }, { QStringLiteral("swingDir"), pic.value(QStringLiteral("swingDir")).toString() },
            { QStringLiteral("groups"), groups } });
        out.insert(QStringLiteral("pictureWhy"), QStringList{});
    } else {
        out.insert(QStringLiteral("picture"), none);
        out.insert(QStringLiteral("pictureWhy"), picWhy);
    }
    return out;
}

} // namespace pinpoint::report
