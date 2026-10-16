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

#include "trust_readings.h"

#include "../charts/charts_words.h"
#include "../charts/session_sequence.h"
#include "../../Metrics/gate_catalogue.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace pinpoint::trust {

namespace {

namespace W = charts::words;

// §4.3: ordered, a reading takes the LOWEST that applies.
enum Level { NotSeen = 0, NotJudged = 1, Bounded = 2, Estimated = 3, Measured = 4 };

QString levelKey(int l)
{
    switch (l) {
    case NotSeen:   return QStringLiteral("notSeen");
    case NotJudged: return QStringLiteral("notJudged");
    case Bounded:   return QStringLiteral("bounded");
    case Estimated: return QStringLiteral("estimated");
    default:        return QStringLiteral("measured");
    }
}
QString levelWord(int l)
{
    switch (l) {
    case NotSeen:   return QStringLiteral("Not seen");
    case NotJudged: return QStringLiteral("Drawn, not judged");
    case Bounded:   return QStringLiteral("Bounded");
    case Estimated: return QStringLiteral("Estimated");
    default:        return QStringLiteral("Measured");
    }
}
QString levelTip(int l)
{
    switch (l) {
    case NotSeen:   return QStringLiteral("Refused, and nothing about it survives.");
    case NotJudged: return QStringLiteral("Shown, but kept out of every finding.");
    case Bounded:   return QStringLiteral("The exact value is refused; a range survives.");
    case Estimated: return QStringLiteral("An estimated route; every gate passed.");
    default:        return QStringLiteral("A direct route; every gate passed.");
    }
}
QString effectWord(const QString &e)
{
    if (e == QLatin1String("bounded"))    return QStringLiteral("bounded");
    if (e == QLatin1String("refused"))    return QStringLiteral("refused");
    if (e == QLatin1String("withheld"))   return QStringLiteral("not judged");
    if (e == QLatin1String("downgraded")) return QStringLiteral("fell back");
    if (e == QLatin1String("masked"))     return QStringLiteral("marked on the chart");
    return e;
}

int segmentOf(const QString &subject)
{
    if (!subject.startsWith(QLatin1String("ks."))) return -1;
    const auto &ids = charts::sequenceSegmentIds();
    const auto it = std::find(ids.begin(), ids.end(), subject.mid(3));
    return it == ids.end() ? -1 : int(it - ids.begin());
}

// The gates behind one reading, the record the claim shows first.
QVariantList gatesOf(const QVariantList &records, const QVariantMap &primary, const QString &subject)
{
    QStringList tried;   // routes this swing already ran: not offered as a lift
    for (const QVariant &r : records) {
        const QString route = r.toMap().value(QStringLiteral("evidence")).toMap().value(QStringLiteral("route")).toString();
        if (!route.isEmpty() && !tried.contains(route)) tried << route;
    }
    QVariantList ordered;
    if (!primary.isEmpty()) ordered << primary;
    for (const QVariant &r : records) if (r.toMap() != primary) ordered << r;
    QVariantList out;
    for (const QVariant &rv : ordered) {
        const QVariantMap r = rv.toMap();
        const gates::GateEntry *e = gates::gateEntry(r.value(QStringLiteral("gate")).toString());
        if (!e) continue;
        const QString eff = r.value(QStringLiteral("effect")).toString();
        QStringList tun;
        for (const gates::GateTuning &t : e->tuning) {
            if (t.unit == QLatin1String("switch")) continue;
            const QString v = W::fig(t.value, std::fabs(t.value - std::round(t.value)) < 1e-9 ? 0 : 2);
            tun << (t.unit == QStringLiteral("°") ? v + t.unit : t.unit.isEmpty() ? v : v + QChar(0x00A0) + t.unit);
        }
        out << QVariantMap{ { QStringLiteral("gate"), e->id },
                            { QStringLiteral("effect"), eff },
                            { QStringLiteral("effectWord"), effectWord(eff) },
                            { QStringLiteral("title"), e->title },
                            { QStringLiteral("evidence"), gates::evidenceText(*e, r.value(QStringLiteral("evidence")).toMap()) },
                            { QStringLiteral("explain"), e->explain },
                            { QStringLiteral("tuning"), tun.isEmpty() ? QString() : QStringLiteral("Threshold ") + tun.join(QStringLiteral(", ")) },
                            { QStringLiteral("lift"), gates::liftText(*e, subject, tried) },
                            { QStringLiteral("primary"), r == primary } };
    }
    return out;
}

} // namespace

QString subjectLabel(const QString &subject)
{
    static const QStringList seg{ QStringLiteral("Pelvis peak"), QStringLiteral("Chest peak"),
                                  QStringLiteral("Lead arm peak"), QStringLiteral("Club peak") };
    const int s = segmentOf(subject);
    if (s >= 0) return seg[s];
    if (subject.startsWith(QLatin1String("route."))) return QStringLiteral("The ") + gates::routeWords(subject.mid(6)) + QStringLiteral(" route");
    return subject;
}

QVariantList trustReadings(const charts::ShotSeries &shot)
{
    QVariantList out;
    if (shot.gatesVersion <= 0) return out;
    const charts::ShotSequence q = charts::shotSequence(shot);
    // the node's route quality, the ceiling the gates decide under (§3.7)
    QHash<QString, bool> direct;
    for (const QVariant &nv : shot.kinematicSequence.value(QStringLiteral("nodes")).toList()) {
        const QVariantMap n = nv.toMap();
        direct.insert(QStringLiteral("ks.") + n.value(QStringLiteral("segment")).toString(),
                      n.value(QStringLiteral("quality")).toString() == QLatin1String("direct"));
    }
    // subjects in record order, sequence nodes first in segment order
    QStringList subjects;
    for (const QString &id : charts::sequenceSegmentIds()) subjects << QStringLiteral("ks.") + id;
    for (const QVariant &rv : shot.gates) {
        const QString s = rv.toMap().value(QStringLiteral("subject")).toString();
        if (!subjects.contains(s)) subjects << s;
    }
    for (const QString &subject : subjects) {
        QVariantList records;
        for (const QVariant &rv : shot.gates)
            if (rv.toMap().value(QStringLiteral("subject")).toString() == subject) records << rv;
        if (records.isEmpty()) continue;
        const int seg = segmentOf(subject);
        QVariantMap primary;
        QString figure = QStringLiteral("-"), shortText;
        int level = direct.value(subject, false) ? Measured : Estimated;
        if (seg >= 0) {
            const charts::SegmentPeak &p = q.segments[size_t(seg)];
            primary = p.refusal;
            if (p.placed) {
                figure = W::signedMs(p.shotMs);
            } else if (p.present) {
                const charts::UnplacedWhy why = charts::unplacedWhy(p);
                figure = why.unit.isEmpty() ? why.figure : why.figure + QChar(0x00A0) + why.unit;
                shortText = why.shortText;
                level = p.refusal.value(QStringLiteral("effect")).toString() == QLatin1String("bounded") ? Bounded : NotSeen;
            }
        }
        for (const QVariant &rv : records) {
            const QVariantMap r = rv.toMap();
            const QString eff = r.value(QStringLiteral("effect")).toString();
            if (eff == QLatin1String("withheld")) {
                level = std::min(level, int(NotJudged));
                if (primary.isEmpty()) primary = r;
                if (shortText.isEmpty()) shortText = QStringLiteral("not judged");
            }
            // a non-sequence subject: the records alone decide
            if (seg < 0 && eff == QLatin1String("refused")) level = std::min(level, int(NotSeen));
            if (seg < 0 && eff == QLatin1String("bounded")) level = std::min(level, int(Bounded));
        }
        out << QVariantMap{ { QStringLiteral("subject"), subject },
                            { QStringLiteral("area"), seg >= 0 || subject.startsWith(QLatin1String("route.")) ? QStringLiteral("Kinematic sequence") : QString() },
                            { QStringLiteral("label"), subjectLabel(subject) },
                            { QStringLiteral("key"), seg >= 0 ? charts::sequenceSeriesKey(seg) : QString() },
                            { QStringLiteral("level"), levelKey(level) },
                            { QStringLiteral("levelWord"), levelWord(level) },
                            { QStringLiteral("levelTip"), levelTip(level) },
                            { QStringLiteral("figure"), figure },
                            { QStringLiteral("shortText"), shortText },
                            { QStringLiteral("gates"), gatesOf(records, primary, subject) } };
    }
    return out;
}

QVariantMap trustShot(const charts::ShotSeries &shot)
{
    const QVariantList readings = trustReadings(shot);
    int bounded = 0, notSeen = 0, notJudged = 0;
    QStringList notTimed, notJudgedL;
    for (const QVariant &rv : readings) {
        const QVariantMap r = rv.toMap();
        const QString l = r.value(QStringLiteral("level")).toString();
        const QString lab = r.value(QStringLiteral("label")).toString().toLower();
        const QString why = r.value(QStringLiteral("shortText")).toString();
        if (l == QLatin1String("bounded") || l == QLatin1String("notSeen")) {
            (l == QLatin1String("bounded") ? bounded : notSeen) += 1;
            notTimed << lab + (why.isEmpty() ? QString() : QStringLiteral(" (") + why + QStringLiteral(")"));
        } else if (l == QLatin1String("notJudged")) {
            ++notJudged;
            notJudgedL << lab;
        }
    }
    QString head;
    if (shot.gatesVersion <= 0)
        head = QStringLiteral("This shot was analysed before the reasons were recorded. Re-analyse it to see which gates fired.");
    else if (notTimed.isEmpty() && notJudgedL.isEmpty())
        head = readings.isEmpty() ? QStringLiteral("No gate refused, bounded or withheld a reading on this shot.")
                                  : QStringLiteral("Every reading was timed; some curves have stretches the camera could not see.");
    else {
        if (!notTimed.isEmpty()) head = QStringLiteral("Not timed: ") + W::list(notTimed) + QStringLiteral(".");
        if (!notJudgedL.isEmpty())
            head += (head.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("Drawn, not judged: ")
                  + W::list(notJudgedL) + QStringLiteral(".");
    }
    return { { QStringLiteral("recorded"), shot.gatesVersion > 0 },
             { QStringLiteral("readings"), readings },
             { QStringLiteral("head"), head },
             { QStringLiteral("counts"), QVariantMap{ { QStringLiteral("bounded"), bounded },
                                                      { QStringLiteral("notSeen"), notSeen },
                                                      { QStringLiteral("notJudged"), notJudged } } } };
}

QVariantMap trustSession(const charts::SessionSnapshot &session)
{
    struct Row { QString gate; QHash<QString, int> bySubject; QStringList subjects; int shots = 0; bool acted = false; };
    QList<Row> rows;
    int recorded = 0;
    for (const auto &sh : session.shots) {
        if (!sh || sh->gatesVersion <= 0) continue;
        ++recorded;
        QHash<QString, QSet<QString>> seen;   // gate → subjects on this shot
        QHash<QString, bool> acted;
        for (const QVariant &rv : sh->gates) {
            const QVariantMap r = rv.toMap();
            const QString g = r.value(QStringLiteral("gate")).toString();
            seen[g].insert(r.value(QStringLiteral("subject")).toString());
            if (r.value(QStringLiteral("effect")).toString() != QLatin1String("masked")) acted[g] = true;
        }
        for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
            auto row = std::find_if(rows.begin(), rows.end(), [&](const Row &x) { return x.gate == it.key(); });
            if (row == rows.end()) { rows.append(Row{ it.key(), {}, {}, 0, false }); row = rows.end() - 1; }
            row->shots += 1;
            row->acted = row->acted || acted.value(it.key());
            for (const QString &s : it.value()) {
                if (!row->subjects.contains(s)) row->subjects << s;
                row->bySubject[s] += 1;
            }
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.shots > b.shots; });
    QVariantList out;
    QStringList head;
    for (const Row &r : rows) {
        const gates::GateEntry *e = gates::gateEntry(r.gate);
        QVariantList subs;
        QStringList subjects = r.subjects;
        std::stable_sort(subjects.begin(), subjects.end(), [](const QString &a, const QString &b) {
            const int sa = segmentOf(a), sb = segmentOf(b);
            return (sa < 0 ? 99 : sa) < (sb < 0 ? 99 : sb); });
        QStringList words;
        for (const QString &s : subjects) {
            subs << QVariantMap{ { QStringLiteral("subject"), s }, { QStringLiteral("label"), subjectLabel(s) },
                                 { QStringLiteral("shots"), r.bySubject.value(s) } };
            words << subjectLabel(s).toLower();
        }
        out << QVariantMap{ { QStringLiteral("gate"), r.gate },
                            { QStringLiteral("title"), e ? e->title : r.gate },
                            { QStringLiteral("area"), e ? e->area : QString() },
                            { QStringLiteral("acted"), r.acted },
                            { QStringLiteral("subjects"), subs },
                            { QStringLiteral("shots"), r.shots },
                            { QStringLiteral("of"), recorded },
                            { QStringLiteral("countText"), QStringLiteral("%1 of %2").arg(r.shots).arg(recorded) } };
        if (r.acted && head.size() < 2 && e)
            head << W::list(words) + QStringLiteral(": ") + e->title.left(1).toLower() + e->title.mid(1)
                  + QStringLiteral(" on ") + W::word(r.shots) + QStringLiteral(" of ") + W::word(recorded) + QStringLiteral(" shots");
    }
    QString h;
    if (recorded == 0) h = session.count() > 0 ? QStringLiteral("No shot in this session carries recorded reasons yet; re-analyse it to see which gates fired.") : QString();
    else if (head.isEmpty()) h = QStringLiteral("No gate refused a reading in this session.");
    else h = W::cap1(head.join(QStringLiteral("; "))) + QStringLiteral(".");
    return { { QStringLiteral("n"), session.count() },
             { QStringLiteral("recorded"), recorded },
             { QStringLiteral("gates"), out },
             { QStringLiteral("head"), h } };
}

} // namespace pinpoint::trust
