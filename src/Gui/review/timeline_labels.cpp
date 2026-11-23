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

#include "timeline_labels.h"

#include <QFont>
#include <QFontMetricsF>
#include <QVariantMap>
#include <QVector>
#include <algorithm>

namespace {

// Phase-name tables. Indices match Analysis Phase enum (swing_analysis.h); the array
// is the single source of truth that PpTransitTimeline (full names) and PpChartPlot
// (short tags) both read, replacing the lists that used to be duplicated in QML.
const char *const kFullNames[] = {
    "Address", "Takeaway", "Top", "Transition", "Downswing", "Impact",
    "Release", "Finish", "Mid-backswing", "Delivery", "Max speed", "Follow-through",
    "Shaft-parallel back", "Arm-parallel down", "Shaft-parallel through"
};
// The coaching P-position each phase IS, where it is one — the vocabulary a golfer and
// every coaching text already use, against which "SPB" and "APD" were private jargon this
// codebase invented for its own axis. The numbers are NOT assigned here: they are the
// mapping the Phase enum already documents against its own members (swing_analysis.h),
// and shaft_positions.h §1 is the definition of the P-system itself.
//
//   P1 Address · P2 Shaft parallel back · P3 Lead arm parallel back · P4 Top
//   P5 Lead arm parallel down · P6 Shaft parallel down · P7 Impact
//   P8 Shaft parallel through · P9 Lead arm parallel through · P10 Finish
//
// ⚠ FIVE PHASES HAVE NO P-POSITION AND ARE NOT TAGGED AT ALL. Takeaway, Transition,
// Downswing, Release and Max speed are events this pipeline detects that the coaching
// system does not name — Max speed is a peak in a signal, not a posture. They used to
// carry three-letter mnemonics (TKW, TRN, DWN, REL, SPD); those are gone by decision,
// because a chart that mixes a shared vocabulary with a private one reads as neither.
// Inventing P-numbers for them instead would have been worse: a coaching claim on the
// axis that no coaching text would recognise.
//
// ⚠ SO "" IS A REAL ANSWER FROM phaseShortTag(), NOT AN ERROR. Callers that DRAW one tag
// draw nothing and lose only a label. Callers that COMPOSE a pair ("P1→P4") must skip
// untagged phases rather than render "→P4" — hasPositionTag() is how they ask, and
// ChartMetrics::segments/nearestPhase and SwingDataSource's chips all do.
const char *const kPositionTags[] = {
    "P1",  "",    "P4",  "",    "",    "P7",  "",    "P10", "P3",  "P6",  "",    "P9",
    "P2",  "P5",  "P8"
};
constexpr int kPhaseCount = int(sizeof(kFullNames) / sizeof(kFullNames[0]));
static_assert(int(sizeof(kPositionTags) / sizeof(kPositionTags[0])) == kPhaseCount,
              "kPositionTags must cover every Phase — use \"\" for a phase with no P-position");

constexpr int kImpactPhase = 5;   // Phase::Impact — the emphasised station

// ── THE TYPED BRIDGE (refactor 5.8a, lint A6) ───────────────────────────────────────────────────
// The lists QML projected, rebuilt as the maps the map-shaped forms read; each number goes in as a QVariant(double),
// so their conversions (toLongLong, toInt, toDouble) are the ones they made of a number QML passed before.
QVariantList variantsOf(const QList<double> &l)
{
    QVariantList out;
    out.reserve(l.size());
    for (double d : l) out.append(d);
    return out;
}
QVariantList timedMapsOf(const QList<double> &times)
{
    QVariantList out;
    out.reserve(times.size());
    for (double t : times) out.append(QVariantMap{ { QStringLiteral("t_us"), t } });
    return out;
}

} // namespace

QVariantList TimelineLabels::distribute(const QVariantList &centers, const QVariantList &sizes,
                                        double gap, double lo, double hi) const
{
    const int n = centers.size();
    QVector<double> out(n);
    QVector<double> sz(n);
    for (int i = 0; i < n; ++i) {
        out[i] = centers.at(i).toDouble();
        sz[i]  = (i < sizes.size()) ? sizes.at(i).toDouble() : 0.0;
    }

    // Pass 1 — left→right: push each item right so it clears the previous one (or the
    // lower bound for the first), keeping a half-size + gap clearance.
    for (int i = 0; i < n; ++i) {
        const double h  = sz[i] / 2.0;
        const double mn = (i == 0) ? lo + h
                                   : out[i - 1] + sz[i - 1] / 2.0 + gap + h;
        if (out[i] < mn) out[i] = mn;
    }
    // Pass 2 — right→left: clamp each item left so it clears the next one (or the upper
    // bound for the last). Resolves the overflow the first pass can create at the end.
    for (int i = n - 1; i >= 0; --i) {
        const double h  = sz[i] / 2.0;
        const double mx = (i == n - 1) ? hi - h
                                       : out[i + 1] - sz[i + 1] / 2.0 - gap - h;
        if (out[i] > mx) out[i] = mx;
    }

    QVariantList res;
    res.reserve(n);
    for (int i = 0; i < n; ++i) res.append(out[i]);
    return res;
}

QVariantList TimelineLabels::stationLayout(const QVariantList &phases, qint64 startUs,
                                           qint64 endUs, bool horizontal, double mainLength,
                                           double gap, const QString &fontFamily,
                                           double fontPx) const
{
    const int n = phases.size();
    QVariantList out;
    if (n == 0) return out;

    const double span = qMax<double>(1.0, double(endUs - startUs));

    QFont font(fontFamily);
    if (fontPx > 0.0) font.setPixelSize(qRound(fontPx));
    const QFontMetricsF fm(font);
    const double lineH = fm.height();

    QVariantList centers, sizes;
    centers.reserve(n);
    sizes.reserve(n);
    QVector<double>  fracs(n);
    QVector<QString> names(n);
    QVector<int>     phaseIds(n);
    QVector<qint64>  tus(n);

    for (int i = 0; i < n; ++i) {
        const QVariantMap m = phases.at(i).toMap();
        const int    ph = m.value(QStringLiteral("phase")).toInt();
        const qint64 t  = m.value(QStringLiteral("t_us")).toLongLong();

        const double frac   = qBound(0.0, double(t - startUs) / span, 1.0);
        const double center = frac * mainLength;
        const QString name  = phaseFullName(ph);
        // Main-axis size: horizontal uses measured text width; vertical uses a uniform
        // line height (the labels stack down the right of the line).
        const double size = horizontal ? fm.horizontalAdvance(name) + 4.0 : lineH;

        fracs[i] = frac; names[i] = name; phaseIds[i] = ph; tus[i] = t;
        centers.append(center);
        sizes.append(size);
    }

    const QVariantList labelPos = distribute(centers, sizes, gap, 0.0, mainLength);

    for (int i = 0; i < n; ++i) {
        const double center = centers.at(i).toDouble();
        const double label  = labelPos.at(i).toDouble();
        QVariantMap r;
        r.insert(QStringLiteral("phase"),    phaseIds[i]);
        r.insert(QStringLiteral("tUs"),      tus[i]);
        r.insert(QStringLiteral("frac"),     fracs[i]);
        r.insert(QStringLiteral("center"),   center);
        r.insert(QStringLiteral("label"),    label);
        r.insert(QStringLiteral("name"),     names[i]);
        r.insert(QStringLiteral("isImpact"), phaseIds[i] == kImpactPhase);
        r.insert(QStringLiteral("elbow"),    qAbs(label - center) > 1.5);
        out.append(r);
    }
    return out;
}

QVariantList TimelineLabels::positionLayout(const QVariantList &positions, qint64 startUs,
                                            qint64 endUs, bool horizontal, double mainLength,
                                            double gap, const QString &fontFamily,
                                            int fontPx) const
{
    QVariantList out;
    if (positions.isEmpty()) return out;

    // Keep only valid P1..P8 + P10 (Finish) entries, sorted by time — unlike phases
    // (already time-ordered by the analyzer), milestone fits can be revised/added out
    // of order, so we re-sort defensively.
    QVector<QVariantMap> rows;
    rows.reserve(positions.size());
    for (const QVariant &pv : positions) {
        const QVariantMap m = pv.toMap();
        const int p = m.value(QStringLiteral("p")).toInt();
        if (p >= 1 && p <= 10) rows.append(m);
    }
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("t_us")).toLongLong()
             < b.value(QStringLiteral("t_us")).toLongLong();
    });

    const int n = rows.size();
    if (n == 0) return out;

    const double span = qMax<double>(1.0, double(endUs - startUs));

    QFont font(fontFamily);
    if (fontPx > 0) font.setPixelSize(fontPx);
    const QFontMetricsF fm(font);
    const double lineH = fm.height();

    QVariantList centers, sizes;
    centers.reserve(n);
    sizes.reserve(n);
    QVector<double>  fracs(n);
    QVector<QString> labels(n);
    QVector<int>     ps(n);
    QVector<qint64>  tus(n);
    QVector<int>     sources(n);
    QVector<double>  confs(n);

    for (int i = 0; i < n; ++i) {
        const QVariantMap &m = rows[i];
        const int    p = m.value(QStringLiteral("p")).toInt();
        const qint64 t = m.value(QStringLiteral("t_us")).toLongLong();

        const double  frac   = qBound(0.0, double(t - startUs) / span, 1.0);
        const double  center = frac * mainLength;
        const QString label  = QStringLiteral("P%1").arg(p);
        // Main-axis size, same rule as stationLayout: measured text width when
        // horizontal (labels run along the line), a uniform line height when vertical.
        const double size = horizontal ? fm.horizontalAdvance(label) + 4.0 : lineH;

        fracs[i] = frac; labels[i] = label; ps[i] = p; tus[i] = t;
        sources[i] = m.value(QStringLiteral("source")).toInt();
        confs[i]   = m.value(QStringLiteral("conf")).toDouble();
        centers.append(center);
        sizes.append(size);
    }

    const QVariantList solved = distribute(centers, sizes, gap, 0.0, mainLength);

    for (int i = 0; i < n; ++i) {
        const double rawCenter = centers.at(i).toDouble();
        const double center    = solved.at(i).toDouble();
        QVariantMap r;
        r.insert(QStringLiteral("p"),      ps[i]);
        r.insert(QStringLiteral("tUs"),    tus[i]);
        r.insert(QStringLiteral("frac"),   fracs[i]);
        r.insert(QStringLiteral("center"), center);
        r.insert(QStringLiteral("label"),  labels[i]);
        r.insert(QStringLiteral("source"), sources[i]);
        r.insert(QStringLiteral("conf"),   confs[i]);
        r.insert(QStringLiteral("elbow"),  qAbs(center - rawCenter) > 1.5);
        out.append(r);
    }
    return out;
}

int TimelineLabels::activeStation(const QVariantList &phases, qint64 pos) const
{
    int best = -1;
    for (int i = 0; i < phases.size(); ++i) {
        const qint64 t = phases.at(i).toMap().value(QStringLiteral("t_us")).toLongLong();
        if (t <= pos) best = i;   // greatest index that still precedes the playhead
    }
    return best;
}

double TimelineLabels::valueAtNearest(const QVariantList &tUs, const QVariantList &value,
                                      qint64 pos) const
{
    const int n = qMin(tUs.size(), value.size());
    if (n == 0) return 0.0;
    if (pos <= tUs.at(0).toLongLong())     return value.at(0).toDouble();
    if (pos >= tUs.at(n - 1).toLongLong()) return value.at(n - 1).toDouble();

    // tUs is ascending — binary search for the first sample at/after pos.
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (tUs.at(mid).toLongLong() < pos) lo = mid + 1;
        else                                hi = mid;
    }
    const qint64 tHi = tUs.at(lo).toLongLong();
    const qint64 tLo = tUs.at(lo - 1).toLongLong();
    const int idx = (pos - tLo) <= (tHi - pos) ? lo - 1 : lo;
    return value.at(idx).toDouble();
}

qint64 TimelineLabels::snap(const QVariantList &phases, qint64 us, qint64 tolUs) const
{
    qint64 bestT = us;
    qint64 bestD = tolUs;     // a candidate must fall within the tolerance
    bool   found = false;
    for (int i = 0; i < phases.size(); ++i) {
        const qint64 t = phases.at(i).toMap().value(QStringLiteral("t_us")).toLongLong();
        const qint64 d = qAbs(t - us);
        if (d <= bestD) { bestD = d; bestT = t; found = true; }   // nearest within tol wins
    }
    return found ? bestT : us;
}

QList<pinpoint::timelineui::Station> TimelineLabels::stationLayout(const QList<int> &phaseIds,
                                                                    const QList<double> &phaseTimes, qint64 startUs,
                                                                    qint64 endUs, bool horizontal, double mainLength,
                                                                    double gap, const QString &fontFamily,
                                                                    double fontPx) const
{
    QVariantList phases;
    const qsizetype n = qMin(phaseIds.size(), phaseTimes.size());
    phases.reserve(n);
    for (qsizetype i = 0; i < n; ++i)
        phases.append(QVariantMap{ { QStringLiteral("phase"), phaseIds.at(i) }, { QStringLiteral("t_us"), phaseTimes.at(i) } });
    QList<pinpoint::timelineui::Station> out;
    for (const QVariant &v : stationLayout(phases, startUs, endUs, horizontal, mainLength, gap, fontFamily, fontPx)) {
        const QVariantMap m = v.toMap();
        pinpoint::timelineui::Station st;
        st.phase    = m.value(QStringLiteral("phase")).toInt();
        st.tUs      = m.value(QStringLiteral("tUs")).toLongLong();
        st.frac     = m.value(QStringLiteral("frac")).toDouble();
        st.center   = m.value(QStringLiteral("center")).toDouble();
        st.label    = m.value(QStringLiteral("label")).toDouble();
        st.name     = m.value(QStringLiteral("name")).toString();
        st.isImpact = m.value(QStringLiteral("isImpact")).toBool();
        st.elbow    = m.value(QStringLiteral("elbow")).toBool();
        out.append(st);
    }
    return out;
}

QList<pinpoint::timelineui::PositionMark> TimelineLabels::positionLayout(const QList<int> &ps,
                                                                          const QList<double> &times,
                                                                          const QList<int> &sources,
                                                                          const QList<double> &confs, qint64 startUs,
                                                                          qint64 endUs, bool horizontal,
                                                                          double mainLength, double gap,
                                                                          const QString &fontFamily, int fontPx) const
{
    QVariantList positions;
    const qsizetype n = qMin(ps.size(), times.size());
    positions.reserve(n);
    for (qsizetype i = 0; i < n; ++i)
        positions.append(QVariantMap{ { QStringLiteral("p"),      ps.at(i) },
                                      { QStringLiteral("t_us"),   times.at(i) },
                                      { QStringLiteral("source"), i < sources.size() ? sources.at(i) : 0 },
                                      { QStringLiteral("conf"),   i < confs.size() ? confs.at(i) : 0.0 } });
    QList<pinpoint::timelineui::PositionMark> out;
    for (const QVariant &v : positionLayout(positions, startUs, endUs, horizontal, mainLength, gap, fontFamily, fontPx)) {
        const QVariantMap m = v.toMap();
        pinpoint::timelineui::PositionMark pm;
        pm.p      = m.value(QStringLiteral("p")).toInt();
        pm.tUs    = m.value(QStringLiteral("tUs")).toLongLong();
        pm.frac   = m.value(QStringLiteral("frac")).toDouble();
        pm.center = m.value(QStringLiteral("center")).toDouble();
        pm.label  = m.value(QStringLiteral("label")).toString();
        pm.source = m.value(QStringLiteral("source")).toInt();
        pm.conf   = m.value(QStringLiteral("conf")).toDouble();
        pm.elbow  = m.value(QStringLiteral("elbow")).toBool();
        out.append(pm);
    }
    return out;
}

int TimelineLabels::activeStation(const QList<double> &phaseTimes, qint64 pos) const
{
    return activeStation(timedMapsOf(phaseTimes), pos);
}

double TimelineLabels::valueAtNearest(const QList<double> &tUs, const QList<double> &value, qint64 pos) const
{
    return valueAtNearest(variantsOf(tUs), variantsOf(value), pos);
}

qint64 TimelineLabels::snap(const QList<double> &phaseTimes, qint64 us, qint64 tolUs) const
{
    return snap(timedMapsOf(phaseTimes), us, tolUs);
}

QString TimelineLabels::phaseFullName(int phase) const
{
    if (phase >= 0 && phase < kPhaseCount) return QString::fromLatin1(kFullNames[phase]);
    return QStringLiteral("P%1").arg(phase);
}

bool TimelineLabels::hasPositionTag(int phase) const
{
    return phase >= 0 && phase < kPhaseCount && kPositionTags[phase][0] != '\0';
}

QString TimelineLabels::phaseShortTag(int phase) const
{
    // Changed here rather than at the call sites on purpose: this function is the one
    // vocabulary the chart axis, the segment chips, the brush and the wrist grid all read,
    // and a P-system that reached only the axis would leave "ADR→SPB" on the chips beside
    // "P1" on the plot below them.
    if (phase >= 0 && phase < kPhaseCount)
        return QString::fromLatin1(kPositionTags[phase]);   // "" where there is no P-position
    // ⚠ Out of range reads "P99" for phase 99, which is a phase INDEX and not a
    // P-position — the one place the two numberings collide. Kept because it is
    // unreachable for any Phase the enum defines and it is what the test pins; it is a
    // diagnostic for a value that should not exist, not a label anyone is meant to read.
    return QStringLiteral("P%1").arg(phase);
}

// moc for the typed layouts' gadgets (timeline_labels_types.h has no .cpp of its own)
#include "moc_timeline_labels_types.cpp"
