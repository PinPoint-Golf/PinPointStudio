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
        out << gates::gateItem(*e, r.value(QStringLiteral("effect")).toString(),
                               r.value(QStringLiteral("evidence")).toMap(), subject, tried, r == primary);
    }
    return out;
}

} // namespace

QString subjectLabel(const QString &subject) { return gates::subjectLabel(subject); }

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
                            { QStringLiteral("gates"), gatesOf(records, primary, subject) },
                            // what this reading kept from being judged (§4.4: the record's `blocks`, from the ledger)
                            { QStringLiteral("blocks"), shot.diagnostics.notJudgedBy.value(subject) } };
    }
    return out;
}

// ── "How far can I trust this?" (§7.4) ──────────────────────────────────────────────────────────────

namespace {

// One reading on one shot, as the summary counts it.
struct Status {
    QString subject, level;     // level: measured | estimated | bounded | notSeen | notJudged
    QString gate;               // the gate the claim shows, when a gate acted on it
    QStringList blocks, tried;  // findings it kept from being judged; routes the swing already ran for it
    bool passed() const { return level == QLatin1String("measured") || level == QLatin1String("estimated"); }
};

// "Pelvis peak" → "pelvis"; a route subject keeps its whole label.
QString shortName(const QString &subject)
{
    QString l = subjectLabel(subject);
    if (l.endsWith(QLatin1String(" peak"))) l.chop(5);
    return l.toLower();
}
QString lowerLabel(const QString &subject) { return subjectLabel(subject).toLower(); }

// Every sequence reading the shot has a node for, and every other subject a record names.
QList<Status> statusesOf(const charts::ShotSeries &shot)
{
    QList<Status> out;
    QHash<QString, QVariantMap> read;
    for (const QVariant &rv : trustReadings(shot)) read.insert(rv.toMap().value(QStringLiteral("subject")).toString(), rv.toMap());
    QHash<QString, QString> quality;
    for (const QVariant &nv : shot.kinematicSequence.value(QStringLiteral("nodes")).toList()) {
        const QVariantMap n = nv.toMap();
        quality.insert(QStringLiteral("ks.") + n.value(QStringLiteral("segment")).toString(),
                       n.value(QStringLiteral("quality")).toString() == QLatin1String("direct") ? QStringLiteral("measured")
                                                                                               : QStringLiteral("estimated"));
    }
    QStringList subjects;
    for (const QString &id : charts::sequenceSegmentIds()) subjects << QStringLiteral("ks.") + id;
    for (auto it = read.cbegin(); it != read.cend(); ++it) if (!subjects.contains(it.key())) subjects << it.key();
    for (const QString &subject : subjects) {
        const bool hasNode = quality.contains(subject);
        const auto r = read.constFind(subject);
        if (!hasNode && r == read.cend()) continue;
        Status st;
        st.subject = subject;
        st.level = r != read.cend() ? r->value(QStringLiteral("level")).toString() : quality.value(subject);
        if (r != read.cend()) {
            for (const QVariant &gv : r->value(QStringLiteral("gates")).toList()) {
                const QVariantMap g = gv.toMap();
                if (g.value(QStringLiteral("effect")).toString() == QLatin1String("masked")) continue;
                st.gate = g.value(QStringLiteral("gate")).toString();
                break;
            }
            st.blocks = r->value(QStringLiteral("blocks")).toStringList();
        }
        for (const QVariant &rv : shot.gates) {
            const QVariantMap rec = rv.toMap();
            if (rec.value(QStringLiteral("subject")).toString() != subject) continue;
            const QString route = rec.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("route")).toString();
            if (!route.isEmpty() && !st.tried.contains(route)) st.tried << route;
        }
        // a route subject that was only marked on the chart, or a gate that only fell back, says nothing here
        if (!hasNode && st.passed()) continue;
        out << st;
    }
    return out;
}

// The routes the sequence ran, in segment order, each with the readings it timed: faceOn → [ks.pelvis, ks.thorax].
QList<QPair<QString, QStringList>> routesOf(const charts::ShotSeries &shot)
{
    QList<QPair<QString, QStringList>> out;
    QHash<QString, QString> bySeg;
    for (const QVariant &nv : shot.kinematicSequence.value(QStringLiteral("nodes")).toList()) {
        const QVariantMap n = nv.toMap();
        bySeg.insert(n.value(QStringLiteral("segment")).toString(), n.value(QStringLiteral("routeId")).toString());
    }
    for (const QString &id : charts::sequenceSegmentIds()) {
        const QString r = bySeg.value(id);
        if (r.isEmpty()) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const auto &x) { return x.first == r; });
        if (it == out.end()) out.append({ r, { QStringLiteral("ks.") + id } }); else it->second << QStringLiteral("ks.") + id;
    }
    return out;
}

// "the face-on camera (pelvis, chest)"
QString routeWithReadings(const QString &theRoute, const QStringList &subjects)
{
    QStringList names;
    for (const QString &s : subjects) names << shortName(s);
    return theRoute + QStringLiteral(" (") + names.join(QStringLiteral(", ")) + QLatin1Char(')');
}

QString routeThe(const QString &route)
{
    if (route == QLatin1String("faceOn+dtl")) return QStringLiteral("the two cameras together");
    return QStringLiteral("the ") + gates::routeWords(route);
}

// A gated group's line. `on` is "" for one shot, " on 6 of 6 shots" for a session.
QString gatedLine(const gates::GateEntry *e, const QStringList &subjects, const QStringList &perSubjectOn,
                  const QString &on, const QStringList &blocks, bool everyShot, const QStringList &tried)
{
    QStringList who;
    for (int i = 0; i < subjects.size(); ++i)
        who << lowerLabel(subjects[i]) + (perSubjectOn.isEmpty() ? QString() : perSubjectOn[i]);
    QString t = W::cap1(W::list(who)) + QLatin1Char(' ') + (e ? gates::claimWords(*e) : QStringLiteral("not timed")) + on;
    // the findings it kept from being judged: named when a few, counted when many (the reading's own block,
    // "Why", and This shot's not-judged list name them all) — the summary stays a few lines
    if (!blocks.isEmpty())
        t += QStringLiteral(", so ") + (blocks.size() <= 3 ? W::list(blocks) : W::word(int(blocks.size())) + QStringLiteral(" findings"))
           + (blocks.size() == 1 ? QStringLiteral(" is") : QStringLiteral(" are"))
           + QStringLiteral(" not judged") + (everyShot ? QString() : QStringLiteral(" on those shots"));
    t += QLatin1Char('.');
    const QString lift = e ? gates::liftTextAll(*e, subjects, tried) : QString();
    if (!lift.isEmpty()) t += QLatin1Char(' ') + lift;
    return t;
}

QVariantMap line(const QString &text, const QString &kind, const QString &subject = {}, const QString &swingDir = {})
{
    return { { QStringLiteral("text"), text }, { QStringLiteral("kind"), kind },
             { QStringLiteral("subject"), subject }, { QStringLiteral("swingDir"), swingDir } };
}

QString tailOf(const QList<QPair<QString, QStringList>> &groups, bool anyPassed)
{
    QStringList parts;
    for (const auto &g : groups) {
        const gates::GateEntry *e = gates::gateEntry(g.first);
        QStringList names;
        for (const QString &s : g.second) names << shortName(s);
        parts << W::list(names) + QLatin1Char(' ') + (e ? gates::claimWords(*e) : QStringLiteral("not timed"));
    }
    if (parts.isEmpty()) return anyPassed ? QStringLiteral("every reading timed") : QString();
    return parts.join(QStringLiteral(" · "));
}

} // namespace

QVariantMap trustSummary(const charts::ShotSeries &shot)
{
    if (shot.gatesVersion <= 0)
        return { { QStringLiteral("recorded"), false }, { QStringLiteral("tail"), QStringLiteral("reasons not recorded") },
                 { QStringLiteral("lines"), QVariantList{ line(QStringLiteral("This shot was analysed before the reasons were recorded. "
                                                                              "Re-analyse it to see which gates fired."), QStringLiteral("old")) } } };
    QVariantList lines;
    const auto routes = routesOf(shot);
    if (!routes.isEmpty()) {
        QStringList w;
        for (const auto &r : routes) w << routeWithReadings(routeThe(r.first), r.second);
        lines << line(QStringLiteral("Kinematic sequence, timed from ") + W::list(w) + QLatin1Char('.'), QStringLiteral("routes"));
    }
    const QList<Status> sts = statusesOf(shot);
    QStringList measured, estimated;
    QList<QPair<QString, QStringList>> groups;   // gate → subjects, first seen first
    for (const Status &st : sts) {
        if (st.passed()) { (st.level == QLatin1String("measured") ? measured : estimated) << lowerLabel(st.subject); continue; }
        auto g = std::find_if(groups.begin(), groups.end(), [&](const auto &x) { return x.first == st.gate; });
        if (g == groups.end()) groups.append({ st.gate, { st.subject } }); else g->second << st.subject;
    }
    QStringList passed;
    if (!measured.isEmpty()) passed << W::list(measured) + QStringLiteral(" measured");
    if (!estimated.isEmpty()) passed << W::list(estimated) + QStringLiteral(" estimated");
    if (!passed.isEmpty()) lines << line(W::cap1(passed.join(QStringLiteral("; "))) + QLatin1Char('.'), QStringLiteral("passed"));
    for (const auto &g : groups) {
        QStringList blocks, tried;
        for (const Status &st : sts) {
            if (!g.second.contains(st.subject)) continue;
            for (const QString &b : st.blocks) if (!blocks.contains(b)) blocks << b;
            for (const QString &t : st.tried) if (!tried.contains(t)) tried << t;
        }
        lines << line(gatedLine(gates::gateEntry(g.first), g.second, {}, QString(), blocks, true, tried),
                      QStringLiteral("gated"), g.second.first(), shot.swingDir);
    }
    return { { QStringLiteral("recorded"), true }, { QStringLiteral("tail"), tailOf(groups, !passed.isEmpty()) },
             { QStringLiteral("lines"), lines } };
}

QVariantMap trustSummary(const charts::SessionSnapshot &session)
{
    int recorded = 0, old = 0;
    QHash<QString, QStringList> routeSubjects;
    QHash<QString, int> routeSubjectShots;      // "route|subject" → shots
    QStringList routeOrder;
    struct Passed { QString subject, level; int shots = 0; };
    QList<Passed> passed;
    struct Group { QString gate; QStringList subjects; QHash<QString, int> bySubject; QStringList blocks, tried; QString swingDir;
                   int shots = 0; };
    QList<Group> groups;
    for (const auto &sh : session.shots) {
        if (!sh) continue;
        if (sh->gatesVersion <= 0) { ++old; continue; }
        ++recorded;
        for (const auto &r : routesOf(*sh)) {
            if (!routeOrder.contains(r.first)) routeOrder << r.first;
            for (const QString &x : r.second) {
                if (!routeSubjects[r.first].contains(x)) routeSubjects[r.first] << x;
                routeSubjectShots[r.first + QLatin1Char('|') + x] += 1;
            }
        }
        QSet<QString> gatesHere;
        for (const Status &st : statusesOf(*sh)) {
            if (st.passed()) {
                auto p = std::find_if(passed.begin(), passed.end(), [&](const Passed &x) { return x.subject == st.subject && x.level == st.level; });
                if (p == passed.end()) passed.append({ st.subject, st.level, 1 }); else p->shots += 1;
                continue;
            }
            auto g = std::find_if(groups.begin(), groups.end(), [&](const Group &x) { return x.gate == st.gate; });
            if (g == groups.end()) { groups.append(Group{ st.gate, {}, {}, {}, {}, sh->swingDir, 0 }); g = groups.end() - 1; }
            if (!g->subjects.contains(st.subject)) g->subjects << st.subject;
            g->bySubject[st.subject] += 1;
            for (const QString &b : st.blocks) if (!g->blocks.contains(b)) g->blocks << b;
            for (const QString &t : st.tried) if (!g->tried.contains(t)) g->tried << t;
            if (!gatesHere.contains(st.gate)) { gatesHere.insert(st.gate); g->shots += 1; }
        }
    }
    QVariantList lines;
    if (recorded == 0) {
        if (session.count() > 0)
            lines << line(QStringLiteral("No shot in this session carries recorded reasons yet; re-analyse it to see which gates fired."),
                          QStringLiteral("old"));
        return { { QStringLiteral("recorded"), false }, { QStringLiteral("tail"), session.count() > 0 ? QStringLiteral("reasons not recorded") : QString() },
                 { QStringLiteral("lines"), lines } };
    }
    const QString of = QStringLiteral(" of ") + QString::number(recorded);
    if (!routeOrder.isEmpty()) {
        QStringList w;
        for (const QString &r : routeOrder)
        {
            QStringList subs = routeSubjects.value(r);
            std::stable_sort(subs.begin(), subs.end(), [](const QString &a, const QString &b) { return segmentOf(a) < segmentOf(b); });
            // a reading this route timed on only some shots says on how many: "(pelvis on 14 of 15, club)"
            QStringList names;
            for (const QString &x : subs) {
                const int k = routeSubjectShots.value(r + QLatin1Char('|') + x);
                names << shortName(x) + (k == recorded ? QString() : QStringLiteral(" on ") + QString::number(k) + of);
            }
            w << routeThe(r) + QStringLiteral(" (") + names.join(QStringLiteral(", ")) + QLatin1Char(')');
        }
        lines << line(QStringLiteral("Kinematic sequence, timed from ") + W::list(w) + QLatin1Char('.'), QStringLiteral("routes"));
    }
    // subjects in segment order, then by level: "lead arm peak measured on 6 of 6 shots; club peak estimated on 6 of 6"
    std::stable_sort(passed.begin(), passed.end(), [](const Passed &a, const Passed &b) { return segmentOf(a.subject) < segmentOf(b.subject); });
    QStringList pw;
    for (const Passed &p : passed)
        pw << lowerLabel(p.subject) + QLatin1Char(' ') + p.level + QStringLiteral(" on ") + QString::number(p.shots) + of
              + (pw.isEmpty() ? QStringLiteral(" shots") : QString());
    if (!pw.isEmpty()) lines << line(W::cap1(pw.join(QStringLiteral("; "))) + QLatin1Char('.'), QStringLiteral("passed"));
    std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) { return a.shots > b.shots; });
    QList<QPair<QString, QStringList>> tailGroups;
    for (Group &g : groups) {
        std::stable_sort(g.subjects.begin(), g.subjects.end(), [](const QString &a, const QString &b) {
            const int sa = segmentOf(a), sb = segmentOf(b); return (sa < 0 ? 99 : sa) < (sb < 0 ? 99 : sb); });
        bool same = true;
        for (const QString &s : g.subjects) same = same && g.bySubject.value(s) == g.bySubject.value(g.subjects.first());
        QStringList perSubject;
        if (!same)
            for (const QString &s : g.subjects) perSubject << QStringLiteral(" (") + QString::number(g.bySubject.value(s)) + of + QLatin1Char(')');
        const QString on = same ? QStringLiteral(" on ") + QString::number(g.bySubject.value(g.subjects.first())) + of + QStringLiteral(" shots")
                                : QString();
        lines << line(gatedLine(gates::gateEntry(g.gate), g.subjects, perSubject, on, g.blocks, g.shots == recorded, g.tried),
                      QStringLiteral("gated"), g.subjects.first(), g.swingDir);
        tailGroups.append({ g.gate, g.subjects });
    }
    if (old > 0)
        lines << line(W::cap1(W::word(old)) + (old == 1 ? QStringLiteral(" shot was") : QStringLiteral(" shots were"))
                          + QStringLiteral(" analysed before the reasons were recorded")
                          + (old == 1 ? QStringLiteral(" and is") : QStringLiteral(" and are"))
                          + QStringLiteral(" not counted; re-analyse to include ")
                          + (old == 1 ? QStringLiteral("it.") : QStringLiteral("them.")), QStringLiteral("old"));
    return { { QStringLiteral("recorded"), true }, { QStringLiteral("tail"), tailOf(tailGroups, !pw.isEmpty()) },
             { QStringLiteral("lines"), lines } };
}

QVariantMap trustShot(const charts::ShotSeries &shot)
{
    const QVariantList readings = trustReadings(shot);
    int bounded = 0, notSeen = 0, notJudged = 0;
    for (const QVariant &rv : readings) {
        const QString l = rv.toMap().value(QStringLiteral("level")).toString();
        if (l == QLatin1String("bounded")) ++bounded;
        else if (l == QLatin1String("notSeen")) ++notSeen;
        else if (l == QLatin1String("notJudged")) ++notJudged;
    }
    // the head IS the summary (§7.4): one statement for the panel and the fold
    const QVariantMap summary = trustSummary(shot);
    QStringList said;
    for (const QVariant &lv : summary.value(QStringLiteral("lines")).toList()) said << lv.toMap().value(QStringLiteral("text")).toString();
    const QString head = said.isEmpty() ? QStringLiteral("No gate refused, bounded or withheld a reading on this shot.") : said.join(QLatin1Char(' '));
    return { { QStringLiteral("recorded"), shot.gatesVersion > 0 },
             { QStringLiteral("readings"), readings },
             { QStringLiteral("head"), head },
             { QStringLiteral("summary"), summary },
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
    for (const Row &r : rows) {
        const gates::GateEntry *e = gates::gateEntry(r.gate);
        QVariantList subs;
        QStringList subjects = r.subjects;
        std::stable_sort(subjects.begin(), subjects.end(), [](const QString &a, const QString &b) {
            const int sa = segmentOf(a), sb = segmentOf(b);
            return (sa < 0 ? 99 : sa) < (sb < 0 ? 99 : sb); });
        for (const QString &s : subjects)
            subs << QVariantMap{ { QStringLiteral("subject"), s }, { QStringLiteral("label"), subjectLabel(s) },
                                 { QStringLiteral("shots"), r.bySubject.value(s) } };
        out << QVariantMap{ { QStringLiteral("gate"), r.gate },
                            { QStringLiteral("title"), e ? e->title : r.gate },
                            { QStringLiteral("area"), e ? e->area : QString() },
                            { QStringLiteral("acted"), r.acted },
                            { QStringLiteral("subjects"), subs },
                            { QStringLiteral("shots"), r.shots },
                            { QStringLiteral("of"), recorded },
                            { QStringLiteral("countText"), QStringLiteral("%1 of %2").arg(r.shots).arg(recorded) } };
    }
    // the head IS the summary (§7.4), as on the shot
    const QVariantMap summary = trustSummary(session);
    QStringList said;
    for (const QVariant &lv : summary.value(QStringLiteral("lines")).toList()) said << lv.toMap().value(QStringLiteral("text")).toString();
    const QString h = said.isEmpty() && recorded > 0 ? QStringLiteral("No gate refused a reading in this session.") : said.join(QLatin1Char(' '));
    return { { QStringLiteral("n"), session.count() },
             { QStringLiteral("recorded"), recorded },
             { QStringLiteral("gates"), out },
             { QStringLiteral("head"), h },
             { QStringLiteral("summary"), summary } };
}

} // namespace pinpoint::trust
