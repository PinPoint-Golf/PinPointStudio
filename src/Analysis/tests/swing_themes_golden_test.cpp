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

// swing_themes.h against the Python reference, on real ledgers: the fixture's five sessions of
// Mark's swings (src/Analysis/tests/data/swing_themes/ledgers) and the shipped core.json, marshalled
// through swing_themes_pack.h exactly as the app does, reduced with the default options on threads,
// and compared field by field with expected.json (tools/themes/make_golden.py) at the tolerances
// the shared contract sets: the prepared matrix to 1e-9, eigenvalues and the parallel-analysis
// threshold to 1e-9, loadings to 1e-4, variance shares to 1e-5, the congruence summaries to 1e-3,
// and tiers, members, directions, positions, trends, layer 1, "what you do well" (every row rule
// v1 keeps after the needs-work exclusion, shares to 1e-12), each condition's drill and swing
// position, the focus groups (focus rule v1), every line and the whole VIEW the home screen draws
// (where the shown themes' exclusion is applied, with the focus and "next on your list") exactly.
// The drills are the shipped registry (drills.json beside core.json), read through
// sharedDrillSet() as the app reads it — with Qt's test-mode data location, so no user drill
// layer on this machine can change the answer.
//
// Each field prints one PASS/FAIL line; a failing field prints its first few differences. The wall
// time of the reduction is printed too — it is the cost the app pays on a catch-up.

#include "../swing_themes.h"
#include "swing_themes_pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// One field's comparison: counts the differences and prints the first few.
struct Diff {
    const char *field;
    int n = 0;
    explicit Diff(const char *f) : field(f) {}
    void note(const QString &where, const QString &got, const QString &want)
    {
        if (++n <= 4)
            std::printf("        %s%s: got %s, expected %s\n", field, qPrintable(where), qPrintable(got), qPrintable(want));
    }
    void num(const QString &where, double got, const QJsonValue &want, double tol)
    {
        const bool wantNull = want.isNull() || want.isUndefined();
        const bool same = wantNull ? !std::isfinite(got)
                                   : std::isfinite(got) && std::fabs(got - want.toDouble()) <= tol;
        if (!same)
            note(where, QString::number(got, 'g', 17),
                 wantNull ? QStringLiteral("null") : QString::number(want.toDouble(), 'g', 17));
    }
    void exact(const QString &where, const QString &got, const QString &want)
    {
        if (got != want) note(where, got, want);
    }
    void exact(const QString &where, long long got, long long want)
    {
        if (got != want) note(where, QString::number(got), QString::number(want));
    }
    void done(const char *label)
    {
        if (n > 4) std::printf("        %s: … %d differences in all\n", field, n);
        check(n == 0, label);
    }
};

static QString at(int i) { return QStringLiteral("[%1]").arg(i); }

// Pips as a string of 1s and 0s, from either side.
static QString pips(const std::vector<bool> &v)
{
    QString s;
    for (bool b : v) s += b ? QLatin1Char('1') : QLatin1Char('0');
    return s;
}
static QString pips(const QJsonValue &v)
{
    QString s;
    for (const QJsonValue &b : v.toArray()) s += b.toBool() ? QLatin1Char('1') : QLatin1Char('0');
    return s;
}
static QString at(int i, int j) { return QStringLiteral("[%1][%2]").arg(i).arg(j); }

static ThemeParallelFor threads()
{
    const int workers = std::max(1u, std::thread::hardware_concurrency());
    return [workers](int n, const std::function<void(int)> &body) {
        std::atomic<int> next{ 0 };
        std::vector<std::thread> pool;
        for (int w = 0; w < std::min(workers, n); ++w)
            pool.emplace_back([&]() { for (int i = next++; i < n; i = next++) body(i); });
        for (std::thread &t : pool) t.join();
    };
}

int main()
{
    std::printf("swing_themes_golden_test\n");
    const QString dir = QStringLiteral(PP_SWING_THEMES_FIXTURE_DIR);

    QFile ef(dir + QStringLiteral("/expected.json"));
    if (!ef.open(QIODevice::ReadOnly)) {
        std::printf("  [FAIL] cannot open %s/expected.json\n", PP_SWING_THEMES_FIXTURE_DIR);
        return 1;
    }
    const QJsonObject exp = QJsonDocument::fromJson(ef.readAll()).object();

    QFile pf(QStringLiteral(PP_CORE_PACK_PATH));
    if (!pf.open(QIODevice::ReadOnly)) {
        std::printf("  [FAIL] cannot open %s\n", PP_CORE_PACK_PATH);
        return 1;
    }
    const PackLoadResult pack = loadPack(pf.readAll(), QStringLiteral("core.json"));
    check(pack.parsed, "core.json parses");

    // The shipped drill registry, beside core.json, through the app's own loader.
    QStandardPaths::setTestModeEnabled(true);
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_DRILLS"))
        qputenv("PINPOINT_CORE_DRILLS", QFileInfo(QStringLiteral(PP_CORE_PACK_PATH)).dir().filePath(QStringLiteral("drills.json")).toLocal8Bit());
    check(!sharedDrillSet().drills.empty() && loadUserDrillSet().drills.empty(),
          "the shipped drills load, with no user drill layer");

    std::vector<ThemeSessionInput> sessions;
    const QDir ledgers(dir + QStringLiteral("/ledgers"));
    for (const QString &name : ledgers.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile f(ledgers.filePath(name + QStringLiteral("/diagnostics.json")));
        if (!f.open(QIODevice::ReadOnly)) continue;
        ThemeSessionInput s;
        s.name  = name;
        s.shots = fromJson(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("ledger")).toObject());
        sessions.push_back(std::move(s));
    }
    check(sessions.size() == size_t(exp.value(QStringLiteral("sessions")).toArray().size()), "every fixture session loads");

    const QHash<QString, ThemeMeasureInfo>   info       = themeMeasureInfo(pack.pack);
    const QHash<QString, ThemeConditionInfo> conditions = themeConditionInfo(pack.pack);
    const ThemePhrases                       phrase     = themePhrases(pack.pack);

    // ── The table, orientation and the prepared matrix ───────────────────────────────────────────
    const auto sorted = themes::sortedSessions(sessions);
    const themes::Table raw = themes::buildTable(sorted);
    {
        const QJsonObject mi = exp.value(QStringLiteral("measureInfo")).toObject();
        Diff d("measureInfo");
        d.exact(QStringLiteral(".columns"), raw.measures.join(QLatin1Char(',')), mi.keys().join(QLatin1Char(',')));
        for (const QString &id : raw.measures) {
            const ThemeMeasureInfo m = info.value(id);
            const QJsonObject e = mi.value(id).toObject();
            d.exact(QStringLiteral("[%1].sign").arg(id), (long long)m.sign, e.value(QStringLiteral("sign")).toInt());
            d.exact(QStringLiteral("[%1].twoSided").arg(id), m.twoSided, e.value(QStringLiteral("twoSided")).toBool());
            d.exact(QStringLiteral("[%1].when").arg(id), m.when, e.value(QStringLiteral("when")).toInt());
            d.exact(QStringLiteral("[%1].metricKey").arg(id), m.metricKey, e.value(QStringLiteral("metricKey")).toString());
        }
        d.done("orientation, swing position and metricKey of every column, from the pack");
    }
    const themes::Table kept = themes::dropSparseSwings(raw, ThemeOptions{});
    const themes::Prepared prep = themes::prepare(kept, info, ThemeOptions{});
    {
        Diff d("Z");
        const QJsonArray Z = exp.value(QStringLiteral("Z")).toArray();
        d.exact(QStringLiteral(".rows"), prep.Z.rows, Z.size());
        for (int i = 0; i < std::min(prep.Z.rows, int(Z.size())); ++i) {
            const QJsonArray r = Z.at(i).toArray();
            d.exact(at(i) + QStringLiteral(".cols"), prep.Z.cols, r.size());
            for (int j = 0; j < std::min(prep.Z.cols, int(r.size())); ++j) d.num(at(i, j), prep.Z.at(i, j), r.at(j), 1e-9);
        }
        d.done("the prepared matrix (oriented, session-centred, scaled, winsorised) to 1e-9");
    }

    // ── The reduction ────────────────────────────────────────────────────────────────────────────
    const auto t0 = std::chrono::steady_clock::now();
    const SwingThemes st = reduceSwingThemes(sessions, info, conditions, ThemeOptions{}, threads());
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  reduction: %.2f s on %u threads (%d swings × %d measures, k = %d)\n", wall,
                std::max(1u, std::thread::hardware_concurrency()), st.swings, int(st.measures.size()), st.k);

    {
        Diff d("data");
        QStringList names;
        for (const QJsonValue &v : exp.value(QStringLiteral("sessions")).toArray()) names.append(v.toString());
        d.exact(QStringLiteral(".sessions"), st.sessionNames.join(QLatin1Char(',')), names.join(QLatin1Char(',')));
        d.exact(QStringLiteral(".swingsKept"), st.swings, exp.value(QStringLiteral("swingsKept")).toInt());
        d.exact(QStringLiteral(".sessionsKept"), st.sessions, exp.value(QStringLiteral("sessionsKept")).toInt());
        d.exact(QStringLiteral(".enough"), st.enough, exp.value(QStringLiteral("enough")).toBool());
        QStringList dropped, want;
        for (const auto &x : st.dropped) dropped.append(QStringLiteral("%1#%2").arg(x.first).arg(x.second));
        for (const QJsonValue &v : exp.value(QStringLiteral("dropped")).toArray())
            want.append(QStringLiteral("%1#%2").arg(v.toArray().at(0).toString()).arg(v.toArray().at(1).toInt()));
        d.exact(QStringLiteral(".dropped"), dropped.join(QLatin1Char(',')), want.join(QLatin1Char(',')));
        QStringList measures;
        for (const QJsonValue &v : exp.value(QStringLiteral("measures")).toArray()) measures.append(v.toString());
        d.exact(QStringLiteral(".measures"), st.measures.join(QLatin1Char(',')), measures.join(QLatin1Char(',')));
        d.done("sessions, kept swings, the dropped swings and the kept measures");
    }
    {
        Diff d("k");
        d.exact(QString(), st.k, exp.value(QStringLiteral("k")).toInt());
        d.done("parallel analysis keeps the same number of themes");
    }
    {
        Diff d("eig");
        const QJsonArray e = exp.value(QStringLiteral("eig")).toArray(), t = exp.value(QStringLiteral("paThreshold")).toArray();
        d.exact(QStringLiteral(".size"), (long long)st.eig.size(), e.size());
        for (int i = 0; i < std::min(int(st.eig.size()), int(e.size())); ++i) d.num(at(i), st.eig[size_t(i)], e.at(i), 1e-9);
        for (int i = 0; i < std::min(int(st.paThreshold.size()), int(t.size())); ++i)
            d.num(QStringLiteral(".threshold") + at(i), st.paThreshold[size_t(i)], t.at(i), 1e-9);
        d.done("observed eigenvalues and the shuffled 95th percentile to 1e-9");
    }
    {
        Diff d("loadings");
        const QJsonArray L = exp.value(QStringLiteral("loadings")).toArray();
        for (int i = 0; i < std::min(int(st.measures.size()), int(L.size())); ++i) {
            const QJsonArray r = L.at(i).toArray();
            for (int j = 0; j < std::min(st.k, int(r.size())); ++j)
                d.num(at(i, j), st.loadings[size_t(i) * size_t(st.k) + size_t(j)], r.at(j), 1e-4);
        }
        const QJsonArray v = exp.value(QStringLiteral("varianceShare")).toArray();
        for (int j = 0; j < std::min(int(st.varianceShare.size()), int(v.size())); ++j)
            d.num(QStringLiteral(".varianceShare") + at(j), st.varianceShare[size_t(j)], v.at(j), 1e-5);
        d.done("rotated loadings to 1e-4, variance shares to 1e-5");
    }
    {
        Diff d("clusterLabels");
        const QJsonArray c = exp.value(QStringLiteral("clusterLabels")).toArray();
        d.exact(QStringLiteral(".size"), (long long)st.clusterLabels.size(), c.size());
        for (int i = 0; i < std::min(int(st.clusterLabels.size()), int(c.size())); ++i)
            d.exact(at(i), st.clusterLabels[size_t(i)], c.at(i).toInt());
        d.done("the clustering cross-check labels");
    }

    // ── Themes ───────────────────────────────────────────────────────────────────────────────────
    {
        const QJsonArray et = exp.value(QStringLiteral("themes")).toArray();
        Diff shape("themes"), members("members"), stab("stability"), trend("trend");
        shape.exact(QStringLiteral(".size"), (long long)st.themes.size(), et.size());
        for (int j = 0; j < std::min(int(st.themes.size()), int(et.size())); ++j) {
            const SwingTheme &t = st.themes[size_t(j)];
            const QJsonObject e = et.at(j).toObject();
            const QString w = QStringLiteral("[T%1]").arg(j + 1);
            shape.exact(w + QStringLiteral(".tier"), themeTierToString(t.tier), e.value(QStringLiteral("tier")).toString());
            shape.exact(w + QStringLiteral(".startsAt"), t.startsAt, e.value(QStringLiteral("startsAt")).toInt());
            shape.num(w + QStringLiteral(".varianceShare"), t.varianceShare, e.value(QStringLiteral("varianceShare")), 1e-5);

            const QJsonArray em = e.value(QStringLiteral("members")).toArray();
            QStringList got, want;
            for (const ThemeMember &m : t.members) got.append(m.measureId);
            for (const QJsonValue &v : em) want.append(v.toObject().value(QStringLiteral("measure")).toString());
            QStringList gs = got, ws = want;
            gs.sort();
            ws.sort();
            members.exact(w + QStringLiteral(".set"), gs.join(QLatin1Char(',')), ws.join(QLatin1Char(',')));
            for (const QJsonValue &v : em) {
                const QJsonObject o = v.toObject();
                const QString id = o.value(QStringLiteral("measure")).toString();
                for (const ThemeMember &m : t.members) {
                    if (m.measureId != id) continue;
                    members.exact(w + QStringLiteral("[%1].rawHigh").arg(id), m.rawHigh, o.value(QStringLiteral("rawHigh")).toBool());
                    members.exact(w + QStringLiteral("[%1].when").arg(id), m.when, o.value(QStringLiteral("when")).toInt());
                    members.num(w + QStringLiteral("[%1].loading").arg(id), m.loading, o.value(QStringLiteral("loading")), 1e-4);
                }
            }

            stab.num(w + QStringLiteral(".bootMedian"), t.bootMedian, e.value(QStringLiteral("bootMedian")), 1e-3);
            stab.num(w + QStringLiteral(".bootP05"), t.bootP05, e.value(QStringLiteral("bootP05")), 1e-3);
            stab.num(w + QStringLiteral(".losoMin"), t.losoMin, e.value(QStringLiteral("losoMin")), 1e-3);
            stab.num(w + QStringLiteral(".losoMedian"), t.losoMedian, e.value(QStringLiteral("losoMedian")), 1e-3);
            stab.num(w + QStringLiteral(".emVsZeroFill"), t.emVsZeroFill, e.value(QStringLiteral("emVsZeroFill")), 1e-3);
            stab.num(w + QStringLiteral(".clusterJaccard"), t.clusterJaccard, e.value(QStringLiteral("clusterJaccard")), 1e-3);
            const QJsonArray lp = e.value(QStringLiteral("losoPerSession")).toArray();
            stab.exact(w + QStringLiteral(".losoPerSession.size"), (long long)t.losoPerSession.size(), lp.size());
            for (int s = 0; s < std::min(int(t.losoPerSession.size()), int(lp.size())); ++s)
                stab.num(w + QStringLiteral(".losoPerSession") + at(s), t.losoPerSession[size_t(s)], lp.at(s), 1e-3);

            const QJsonArray sc = e.value(QStringLiteral("sessionScores")).toArray();
            trend.exact(w + QStringLiteral(".sessionScores.size"), (long long)t.sessionScores.size(), sc.size());
            for (int s = 0; s < std::min(int(t.sessionScores.size()), int(sc.size())); ++s)
                trend.num(w + QStringLiteral(".sessionScores") + at(s), t.sessionScores[size_t(s)], sc.at(s), 1e-3);
            trend.num(w + QStringLiteral(".slope"), t.slope, e.value(QStringLiteral("slope")), 1e-3);
            trend.exact(w + QStringLiteral(".trend"), t.trend, e.value(QStringLiteral("trend")).toInt());
        }
        shape.done("each theme's tier, start and variance share");
        members.done("each theme's members, their direction and position, loadings to 1e-4");
        stab.done("bootstrap, leave-one-session-out, zero-fill and cluster congruence to 1e-3");
        trend.done("session scores and slope to 1e-3, and the trend reading");
    }

    // ── Layer 1 and the words ────────────────────────────────────────────────────────────────────
    {
        Diff d("seenMost");
        const QJsonArray sm = exp.value(QStringLiteral("seenMost")).toArray();
        d.exact(QStringLiteral(".size"), (long long)st.seenMost.size(), sm.size());
        for (int i = 0; i < std::min(int(st.seenMost.size()), int(sm.size())); ++i) {
            const SeenMostRow &r = st.seenMost[size_t(i)];
            const QJsonObject e = sm.at(i).toObject();
            d.exact(at(i) + QStringLiteral(".id"), r.conditionId, e.value(QStringLiteral("id")).toString());
            d.num(at(i) + QStringLiteral(".share"), r.share, e.value(QStringLiteral("share")), 1e-12);
            d.exact(at(i) + QStringLiteral(".sessionsSeen"), r.sessionsSeen, e.value(QStringLiteral("sessionsSeen")).toInt());
            d.exact(at(i) + QStringLiteral(".sessionsJudged"), r.sessionsJudged, e.value(QStringLiteral("sessionsJudged")).toInt());
            d.exact(at(i) + QStringLiteral(".trend"), r.trend, e.value(QStringLiteral("trend")).toString());
            d.exact(at(i) + QStringLiteral(".pips"), pips(r.pips), pips(e.value(QStringLiteral("pips"))));
        }
        d.done("what we see most: the same faults, shares, sessions, trends and pips, in the same order");
    }
    {
        Diff d("doWellCandidates");
        const QJsonArray dw = exp.value(QStringLiteral("doWellCandidates")).toArray();
        d.exact(QStringLiteral(".size"), (long long)st.doWell.size(), dw.size());
        for (int i = 0; i < std::min(int(st.doWell.size()), int(dw.size())); ++i) {
            const DoWellRow &r = st.doWell[size_t(i)];
            const QJsonObject e = dw.at(i).toObject();
            d.exact(at(i) + QStringLiteral(".id"), r.conditionId, e.value(QStringLiteral("id")).toString());
            d.num(at(i) + QStringLiteral(".share"), r.share, e.value(QStringLiteral("share")), 1e-12);
            d.exact(at(i) + QStringLiteral(".prominence"), r.prominence, e.value(QStringLiteral("prominence")).toInt());
            d.exact(at(i) + QStringLiteral(".sessionsJudged"), r.sessionsJudged, e.value(QStringLiteral("sessionsJudged")).toInt());
            d.exact(at(i) + QStringLiteral(".swingsJudged"), r.swingsJudged, e.value(QStringLiteral("swingsJudged")).toInt());
            d.exact(at(i) + QStringLiteral(".pips"), pips(r.pips), pips(e.value(QStringLiteral("pips"))));
            QStringList fams;
            for (const QJsonValue &f : e.value(QStringLiteral("families")).toArray()) fams.append(f.toString());
            d.exact(at(i) + QStringLiteral(".families"), r.families.join(QLatin1Char(',')), fams.join(QLatin1Char(',')));
        }
        d.done("what you do well (after the needs-work exclusion): the same rows, shares, sessions, swings, pips and "
               "families (metricKey roots), in the same order");
    }
    {
        const SwingSummaryLines lines = swingSummaryLines(st, phrase);
        const QJsonObject el = exp.value(QStringLiteral("lines")).toObject();
        for (const char *key : { "seenMost", "together" }) {
            const QStringList got = QLatin1String(key) == QLatin1String("seenMost") ? lines.seenMost : lines.together;
            const QJsonArray want = el.value(QLatin1String(key)).toArray();
            Diff d(key);
            d.exact(QStringLiteral(".size"), got.size(), want.size());
            for (int i = 0; i < std::min(int(got.size()), int(want.size())); ++i) d.exact(at(i), got[i], want.at(i).toString());
            d.done(QLatin1String(key) == QLatin1String("seenMost") ? "the \"what we see most\" lines, word for word"
                                                                   : "the \"what goes together\" lines, word for word");
        }
    }

    // ── Your focus ───────────────────────────────────────────────────────────────────────────────
    {
        Diff d("conditionFocus");
        const QJsonObject cf = exp.value(QStringLiteral("conditionFocus")).toObject();
        d.exact(QStringLiteral(".size"), (long long)conditions.size(), cf.size());
        for (const QString &id : cf.keys()) {
            const QJsonObject e = cf.value(id).toObject();
            d.exact(QStringLiteral("[%1].known").arg(id), conditions.contains(id), true);
            d.exact(QStringLiteral("[%1].drill").arg(id), conditions.value(id).drill, e.value(QStringLiteral("drill")).toString());
            d.exact(QStringLiteral("[%1].when").arg(id), conditions.value(id).when, e.value(QStringLiteral("when")).toInt());
        }
        d.done("every condition's drill (first in the registry) and earliest swing position, from the pack");
    }
    {
        Diff d("focusOrder");
        const QJsonArray fo = exp.value(QStringLiteral("focusOrder")).toArray();
        d.exact(QStringLiteral(".size"), (long long)st.focusOrder.size(), fo.size());
        for (int i = 0; i < std::min(int(st.focusOrder.size()), int(fo.size())); ++i) {
            const FocusGroup &g = st.focusOrder[size_t(i)];
            const QJsonObject e = fo.at(i).toObject();
            QStringList ids;
            for (const QJsonValue &v : e.value(QStringLiteral("conditionIds")).toArray()) ids.append(v.toString());
            d.exact(at(i) + QStringLiteral(".key"), g.key, e.value(QStringLiteral("key")).toString());
            d.exact(at(i) + QStringLiteral(".drill"), g.drill, e.value(QStringLiteral("drill")).toString());
            d.exact(at(i) + QStringLiteral(".when"), g.when, e.value(QStringLiteral("when")).toInt());
            d.exact(at(i) + QStringLiteral(".conditionIds"), g.conditionIds.join(QLatin1Char(',')), ids.join(QLatin1Char(',')));
        }
        d.done("the focus groups: the same keys, drills, positions and faults, in the same order");
    }

    // ── The view ─────────────────────────────────────────────────────────────────────────────────
    {
        const SwingSummaryView v = swingSummaryView(st, phrase);
        const QJsonObject ev = exp.value(QStringLiteral("view")).toObject();
        Diff d("view");
        d.exact(QStringLiteral(".subtitle"), v.subtitle, ev.value(QStringLiteral("subtitle")).toString());
        d.exact(QStringLiteral(".note"), v.note, ev.value(QStringLiteral("note")).toString());

        const QJsonArray ew = ev.value(QStringLiteral("doWell")).toArray();
        d.exact(QStringLiteral(".doWell.size"), (long long)v.doWell.size(), ew.size());
        for (int i = 0; i < std::min(int(v.doWell.size()), int(ew.size())); ++i) {
            const QJsonObject e = ew.at(i).toObject();
            const QString w = QStringLiteral(".doWell") + at(i);
            d.exact(w + QStringLiteral(".text"), v.doWell[size_t(i)].text, e.value(QStringLiteral("text")).toString());
            d.exact(w + QStringLiteral(".caption"), v.doWell[size_t(i)].caption, e.value(QStringLiteral("caption")).toString());
            d.exact(w + QStringLiteral(".pips"), pips(v.doWell[size_t(i)].pips), pips(e.value(QStringLiteral("pips"))));
        }

        const QJsonArray en = ev.value(QStringLiteral("needsWork")).toArray();
        d.exact(QStringLiteral(".needsWork.size"), (long long)v.needsWork.size(), en.size());
        for (int i = 0; i < std::min(int(v.needsWork.size()), int(en.size())); ++i) {
            const NeedsWorkItem &n = v.needsWork[size_t(i)];
            const QJsonObject e = en.at(i).toObject();
            const QString w = QStringLiteral(".needsWork") + at(i);
            d.exact(w + QStringLiteral(".text"), n.text, e.value(QStringLiteral("text")).toString());
            d.num(w + QStringLiteral(".share"), n.share, e.value(QStringLiteral("share")), 1e-12);
            d.exact(w + QStringLiteral(".frequency"), n.frequency, e.value(QStringLiteral("frequency")).toString());
            d.exact(w + QStringLiteral(".trend"), n.trend, e.value(QStringLiteral("trend")).toInt());
            d.exact(w + QStringLiteral(".pips"), pips(n.pips), pips(e.value(QStringLiteral("pips"))));
        }

        const QJsonArray et = ev.value(QStringLiteral("together")).toArray();
        d.exact(QStringLiteral(".together.size"), (long long)v.together.size(), et.size());
        for (int i = 0; i < std::min(int(v.together.size()), int(et.size())); ++i) {
            const TogetherItem &t = v.together[size_t(i)];
            const QJsonObject e = et.at(i).toObject();
            const QString w = QStringLiteral(".together") + at(i);
            d.exact(w + QStringLiteral(".tier"), t.tier, e.value(QStringLiteral("tier")).toString());
            d.exact(w + QStringLiteral(".first"), t.first, e.value(QStringLiteral("first")).toString());
            d.exact(w + QStringLiteral(".second"), t.second, e.value(QStringLiteral("second")).toString());
            d.exact(w + QStringLiteral(".startStop"), t.startStop, e.value(QStringLiteral("startStop")).toInt());
            d.exact(w + QStringLiteral(".startWords"), t.startWords, e.value(QStringLiteral("startWords")).toString());
            d.exact(w + QStringLiteral(".trend"), t.trend, e.value(QStringLiteral("trend")).toInt());
        }
        d.done("the view: subtitle, do well, needs work, goes together and the note, word for word");

        // The focus and "next on your list".
        Diff f("view.focus");
        const QJsonObject ef = ev.value(QStringLiteral("focus")).toObject();
        const auto strings = [](const QJsonValue &v) {
            QStringList l;
            for (const QJsonValue &x : v.toArray()) l.append(x.toString());
            return l;
        };
        const auto items = [&](Diff &dd, const QString &where, const std::vector<NeedsWorkItem> &got, const QJsonValue &want) {
            const QJsonArray w = want.toArray();
            dd.exact(where + QStringLiteral(".size"), (long long)got.size(), w.size());
            for (int i = 0; i < std::min(int(got.size()), int(w.size())); ++i) {
                const NeedsWorkItem &n = got[size_t(i)];
                const QJsonObject e = w.at(i).toObject();
                const QString x = where + at(i);
                dd.exact(x + QStringLiteral(".text"), n.text, e.value(QStringLiteral("text")).toString());
                dd.num(x + QStringLiteral(".share"), n.share, e.value(QStringLiteral("share")), 1e-12);
                dd.exact(x + QStringLiteral(".frequency"), n.frequency, e.value(QStringLiteral("frequency")).toString());
                dd.exact(x + QStringLiteral(".trend"), n.trend, e.value(QStringLiteral("trend")).toInt());
                dd.exact(x + QStringLiteral(".pips"), pips(n.pips), pips(e.value(QStringLiteral("pips"))));
            }
        };
        f.exact(QStringLiteral(".present"), v.focus.present, ef.value(QStringLiteral("present")).toBool());
        f.exact(QStringLiteral(".title"), v.focus.title, ef.value(QStringLiteral("title")).toString());
        f.exact(QStringLiteral(".aimFor"), v.focus.aimFor.join(QLatin1Char('|')), strings(ef.value(QStringLiteral("aimFor"))).join(QLatin1Char('|')));
        items(f, QStringLiteral(".rightNow"), v.focus.rightNow, ef.value(QStringLiteral("rightNow")));
        f.exact(QStringLiteral(".why"), v.focus.why, ef.value(QStringLiteral("why")).toString());
        f.exact(QStringLiteral(".practiseLabel"), v.focus.practiseLabel, ef.value(QStringLiteral("practiseLabel")).toString());
        f.exact(QStringLiteral(".practise"), v.focus.practise, ef.value(QStringLiteral("practise")).toString());
        f.exact(QStringLiteral(".reason"), v.focus.reason, ef.value(QStringLiteral("reason")).toString());
        f.exact(QStringLiteral(".conditionIds"), v.focus.conditionIds.join(QLatin1Char(',')),
                strings(ef.value(QStringLiteral("conditionIds"))).join(QLatin1Char(',')));
        f.done("the focus: title, aim for, right now, why, the drill and the reason, word for word");
        std::printf("        focus: %s (%s)\n", qPrintable(v.focus.title), qPrintable(v.focus.conditionIds.join(QStringLiteral(", "))));

        Diff n("view.next");
        items(n, QString(), v.next, ev.value(QStringLiteral("next")));
        n.done("next on your list: the same faults, words, shares, trends and pips, in the same order");
        for (const NeedsWorkItem &x : v.next) std::printf("        next: %s\n", qPrintable(x.text));
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
