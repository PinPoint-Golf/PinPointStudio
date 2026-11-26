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

// THE REDUCTIONS' RULE VERSIONS ARE CHECKED (R-STAMP-3; audit C-05's other half; JUDGEMENT-CALLS J-13; refactor
// step 3.6). 3.3's rule_version_golden_test pins fireFindings(); this pins the session and athlete reductions that
// read its records — each with the version constant(s) its output answers to:
//
//   sessionWorkOns   sessionPrioritiesForPack per session (work_ons_pack.h)          kPrioritySchemaVersion, kPriorityRuleVersion
//   athleteWorkOns   aggregatePriorities / aggregateConditions (work_ons.h)            kPrioritySchemaVersion, kPriorityRuleVersion
//   tendencies       reduceTendencies (tendencies.h)                                kTendencyRuleVersion, kTendencySchemaVersion
//   outcomeScope     judgedShotFactors + outcomeContrasts (outcome_scope.h)         kOutcomeScopeRuleVersion
//   distances        reduceClubDistances (club_distances.h; in tendencies.json)    kTendencyRuleVersion, kClubBaselineSchemaVersion
//   clubBaseline     clubBaselineAsOf (club_distances.h)                            kClubBaselineSchemaVersion
//   themes           reduceSwingThemes, layer 1, do well and the focus (swing_themes.h)  kThemeSchemaVersion, kThemeRuleVersion
//   praise           applyPraiseRule on the themes' do-well rows (swing_themes.h)  kPraiseRuleVersion
//   board            annotatedShot (board_annotations.h)                           kBoardAnnotationRuleVersion
//
// THE FIXTURE: the four live_swings fixtures fired by fireFindings over the FROZEN content copy 3.3 froze
// (data/rule_version_golden/core.json, norms.json, contexts.json — never src/Resources), arranged as three sessions on
// three days (each record a shot of its session, renumbered), the flight table empty (frozen: no shipped table is
// read). Because the inputs are frozen, a digest moves only when a reduction's own rule moves — or fireFindings'
// (which 3.3's golden then catches first).
//
//   • the versions match and a digest moved → FAIL: a reduction's output changed and no version did. Bump the version
//     the change belongs to, with its dated history line, then re-record;
//   • a version moved → FAIL until the golden is re-recorded for it;
//   • the frozen copy was edited → FAIL (frozenContent): re-record, no version owed.
// Re-record: PP_RECORD_RULE_GOLDEN=1 ctest --test-dir build/tests -R '^reduction_rule_golden_test$'
// (writes data/rule_version_golden/reductions.json into the source tree; commit it with the change that moved it).
// Numbers are snapped and rounded as 3.3's test does (|x| < 1e-9 → 0; 9 significant digits).

#include "../engine/fire_findings.h"
#include "../derived/board_annotations.h"
#include "../derived/club_distances.h"
#include "../derived/outcome_scope.h"
#include "../derived/shot_outcomes_pack.h"
#include "../derived/swing_themes.h"
#include "../derived/tendencies.h"
#include "../derived/work_ons_pack.h"
#include "../../DomainModel/rule_data.h"
#include "../../Stores/rule_data_install.h"
#include "../../Stores/rule_data_store.h"   // the norm factories by path (refactor step 7.1)
#include "../../Stores/phase_grid_store.h"   // the phase grid's file door (P-3)
#include "../engine/pack_io.h"
#include "../norms/context_tree.h"
#include "../norms/norm_pack.h"
#include "../packs/characteristic_pack.h"
#include "../packs/drill_pack.h"
#include "../packs/swing_themes_pack.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif
#ifndef PP_RULE_VERSION_GOLDEN_DIR
#  define PP_RULE_VERSION_GOLDEN_DIR "rule_version_golden"
#endif

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

static QString stageFixture(const QTemporaryDir &tmp, const QString &name)
{
    const QString dst = QDir(tmp.path()).filePath(QLatin1String("live_swings/") + name);
    if (!QDir().mkpath(dst)) return QString();
    const QString src = QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(name + QLatin1String("/swing.json"));
    return QFile::copy(src, QDir(dst).filePath(QStringLiteral("swing.json"))) ? dst : QString();
}

static QJsonValue rounded(const QJsonValue &v)
{
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (!std::isfinite(d)) return v;
        if (std::fabs(d) < 1e-9) return QJsonValue(0.0);
        return QJsonValue(QString::number(d, 'g', 9).toDouble());
    }
    if (v.isArray()) {
        QJsonArray out;
        for (const QJsonValue &e : v.toArray()) out.append(rounded(e));
        return out;
    }
    if (v.isObject()) {
        QJsonObject out;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), rounded(it.value()));
        return out;
    }
    return v;
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QString contentKey(const CharacteristicPack &pack, const INormProvider &norms)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QJsonDocument(savePack(pack)).toJson(QJsonDocument::Compact));
    h.addData(QJsonDocument(saveNormPack(norms.norms())).toJson(QJsonDocument::Compact));
    h.addData(QJsonDocument(saveContextTree(norms.contexts())).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(h.result().toHex().left(16));
}

// One reduction's entry: its versions and the digest of its canonical output.
struct Entry {
    QJsonObject versions;
    QByteArray  bytes;
};

static QJsonObject digestOf(const Entry &e)
{
    return QJsonObject{ { QStringLiteral("versions"), e.versions },
                        { QStringLiteral("sha1"), QString::fromLatin1(QCryptographicHash::hash(e.bytes, QCryptographicHash::Sha1).toHex()) } };
}

static QByteArray canonical(const QJsonValue &v)
{
    const QJsonValue r = rounded(v);
    return r.isArray() ? QJsonDocument(r.toArray()).toJson(QJsonDocument::Indented)
                       : QJsonDocument(r.toObject()).toJson(QJsonDocument::Indented);
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    pinpoint::installProcessRuleData();
    std::printf("reduction_rule_golden_test\n");

    // ── The frozen content (3.3's copy) and the fired fixtures ──
    const QDir frozen(QString::fromLocal8Bit(PP_RULE_VERSION_GOLDEN_DIR));
    const QString packPath = frozen.filePath(QStringLiteral("core.json"));
    FiringInputs inputs;
    inputs.pack  = std::shared_ptr<const ICharacteristicPackProvider>(
        makeResourcePackProvider(RuleFile::read(packPath, readFile(packPath)), nullptr).release());
    inputs.norms = std::shared_ptr<const INormProvider>(
        makeResourceNormProvider(frozen.filePath(QStringLiteral("norms.json")),
                                 frozen.filePath(QStringLiteral("contexts.json"))).release());
    inputs.gradePolicy = QStringLiteral("standard");
    inputs.withSigma   = true;
    check(inputs.pack && !inputs.pack->pack().measures.empty() && !inputs.norms->norms().norms.empty(),
          QStringLiteral("the frozen pack, norms and contexts are read (") + frozen.path() + QLatin1Char(')'));
    if (g_fail) return 1;
    const CharacteristicPack &pack = inputs.pack->pack();

    QTemporaryDir tmp;
    QHash<QString, ShotRecord> fired;
    for (const QString &name : { QStringLiteral("lm_7iron"), QStringLiteral("lm_partial"), QStringLiteral("rich_7iron"),
                                 QStringLiteral("sparse_noclub") }) {
        FiringReadings readings;
        readings.grid       = pinpoint::PhaseGridStore::readForMeasures(stageFixture(tmp, name), (pack).measures, pinpoint::SidecarPolicy::ParseAndWrite);
        readings.handedness = QStringLiteral("Right");
        const FiredShot f = fireFindings(std::move(readings), inputs);
        check(f.ok && !f.record.rows.empty(), name + QStringLiteral(": fires"));
        fired.insert(name, f.record);
    }
    if (g_fail) return 1;

    // Three sessions on three days; each record a shot of its session, renumbered, a minute apart.
    const QVector<QStringList> plan{ { QStringLiteral("lm_7iron"), QStringLiteral("rich_7iron"), QStringLiteral("lm_7iron") },
                                     { QStringLiteral("lm_partial"), QStringLiteral("sparse_noclub"), QStringLiteral("lm_7iron"),
                                       QStringLiteral("rich_7iron") },
                                     { QStringLiteral("rich_7iron"), QStringLiteral("lm_7iron"), QStringLiteral("lm_7iron") } };
    const QDate day0(2026, 9, 1);
    const FlightNorms flight;   // frozen: empty (no shipped table)
    std::vector<ClubDistanceSession> sessions;
    for (int si = 0; si < plan.size(); ++si) {
        ClubDistanceSession s;
        s.folder = QStringLiteral("2026-09-0%1_Golden_Swing_01").arg(si + 1);
        s.day    = day0.addDays(si);
        s.nOfDay = 1;
        for (int k = 0; k < plan[si].size(); ++k) {
            ShotRecord r = fired.value(plan[si][k]);
            r.shotId      = k + 1;
            r.timestampMs = QDateTime(s.day, QTime(10, k)).toMSecsSinceEpoch();
            s.shots.push_back(std::move(r));
        }
        sessions.push_back(std::move(s));
    }
    sessions[1].corrections.insert(3, { ShotCorrection{ QStringLiteral("strike"), QStringLiteral("solid"), QStringLiteral("thin") } });

    QHash<QString, Entry> out;
    const LedgerOptions ledgerOpt;

    // sessionWorkOns, athleteWorkOns
    {
        QJsonArray perSession;
        std::vector<SessionPriorities> records;
        for (const ClubDistanceSession &s : sessions) {
            SessionPriorities w = sessionPrioritiesForPack(&pack, s.shots, conditionLedgers(s.shots, ledgerOpt));
            w.sessionId = s.folder;
            perSession.append(toJson(w));
            records.push_back(std::move(w));
        }
        const QJsonObject v{ { QStringLiteral("workOnSchema"), kPrioritySchemaVersion }, { QStringLiteral("workOnRule"), kPriorityRuleVersion } };
        out.insert(QStringLiteral("sessionWorkOns"), Entry{ v, canonical(perSession) });
        QJsonArray agg;
        for (const std::vector<PrioritySummary> &list : { aggregatePriorities(records), aggregateConditions(records) })
            for (const PrioritySummary &w : list)
                agg.append(QJsonObject{ { QStringLiteral("id"), w.id }, { QStringLiteral("status"), priorityStatusToString(w.status) },
                                        { QStringLiteral("score"), w.score }, { QStringLiteral("listed"), w.sessionsListed },
                                        { QStringLiteral("pattern"), w.sessionsPattern }, { QStringLiteral("fired"), w.swingsFired },
                                        { QStringLiteral("assessable"), w.swingsAssessable } });
        out.insert(QStringLiteral("athleteWorkOns"), Entry{ v, canonical(agg) });
    }

    // tendencies, outcomeScope, distances
    const ShotOutcomePack outcomePack = shotOutcomePackFor(&pack);
    {
        std::vector<TendencySessionInput> in;
        for (const ClubDistanceSession &s : sessions) {
            TendencySessionInput t;
            t.session = s;
            in.push_back(std::move(t));
        }
        const auto group = [](const QString &club) { return club.trimmed().isEmpty() ? QString() : tendencyIronGroup(club); };
        const TendencySet set = reduceTendencies(in, outcomePack, group, flight);
        QJsonObject t = tendencySetToJson(set);
        t.remove(QStringLiteral("clubDistances"));
        out.insert(QStringLiteral("tendencies"),
                   Entry{ QJsonObject{ { QStringLiteral("tendencyRule"), kTendencyRuleVersion },
                                       { QStringLiteral("tendencySchema"), kTendencySchemaVersion } }, canonical(t) });

        QJsonObject scope;
        ShotFilter filter;
        filter.strike = QStringLiteral("solid");
        QSet<QString> faults;
        for (const Condition &c : pack.conditions) faults.insert(c.id);
        for (const TendencySessionInput &s : in) {
            const QHash<int, ShotFactors> judged = judgedShotFactors(s, outcomePack, flight);
            QJsonObject cs;
            const QHash<QString, OutcomeContrast> contrasts = outcomeContrasts(s.session.shots, judged, filter, faults);
            for (auto it = contrasts.constBegin(); it != contrasts.constEnd(); ++it)
                if (it->n1 || it->n0)
                    cs.insert(it.key(), QJsonArray{ it->a, it->n1, it->b, it->n0 });
            QJsonArray inFilter;
            for (const ShotRecord &r : shotsInShotFilter(s.session.shots, judged, filter)) inFilter.append(r.shotId);
            scope.insert(s.session.folder, QJsonObject{ { QStringLiteral("contrasts"), cs }, { QStringLiteral("inFilter"), inFilter } });
        }
        out.insert(QStringLiteral("outcomeScope"),
                   Entry{ QJsonObject{ { QStringLiteral("outcomeScopeRule"), kOutcomeScopeRuleVersion } }, canonical(scope) });

        QJsonArray dist;
        for (const ClubDistanceItem &it : reduceClubDistances(sessions, {}, flight)) dist.append(clubDistanceItemToJson(it));
        out.insert(QStringLiteral("distances"),
                   Entry{ QJsonObject{ { QStringLiteral("tendencyRule"), kTendencyRuleVersion },
                                       { QStringLiteral("clubBaselineSchema"), kClubBaselineSchemaVersion } }, canonical(dist) });

        ClubBaselineSnapshot snap;
        snap.frozenAt = QStringLiteral("fixed");
        snap.clubs    = clubBaselineAsOf(sessions, day0.addDays(2), flight);
        out.insert(QStringLiteral("clubBaseline"),
                   Entry{ QJsonObject{ { QStringLiteral("clubBaselineSchema"), kClubBaselineSchemaVersion } },
                          canonical(clubBaselineToJson(snap)) });
    }

    // themes, praise
    {
        std::vector<ThemeSessionInput> in;
        for (const ClubDistanceSession &s : sessions) in.push_back(ThemeSessionInput{ s.folder, s.shots });
        const SwingThemes themes = reduceSwingThemes(in, themeMeasureInfo(pack), themeConditionInfo(pack, DrillSet{}));
        QJsonObject t = toJson(themes);
        out.insert(QStringLiteral("themes"),
                   Entry{ QJsonObject{ { QStringLiteral("themeSchema"), kThemeSchemaVersion }, { QStringLiteral("themeRule"), kThemeRuleVersion } },
                          canonical(t) });

        // Every grade the rule distinguishes, cycled over the themes' do-well rows (and over three made-up rows, so the
        // rule is exercised whatever the fixture praises).
        std::vector<DoWellRow> rows = themes.doWell;
        for (const char *id : { "golden.a", "golden.b", "golden.c", "golden.d", "golden.e", "golden.f" }) {
            DoWellRow r;
            r.conditionId = QLatin1String(id);
            rows.push_back(r);
        }
        const std::vector<PraiseGrade> grades{ { true, 5, false }, { true, 4, false }, { true, 3, false },
                                               { false, 0, false }, { true, 0, false }, { true, 5, true } };
        QHash<QString, PraiseGrade> gradeOf;
        for (size_t i = 0; i < rows.size(); ++i) gradeOf.insert(rows[i].conditionId, grades[i % grades.size()]);
        applyPraiseRule(rows, [&gradeOf](const QString &id) { return gradeOf.value(id); });
        QJsonArray kept;
        for (const DoWellRow &r : rows) kept.append(r.conditionId);
        out.insert(QStringLiteral("praise"), Entry{ QJsonObject{ { QStringLiteral("praiseRule"), kPraiseRuleVersion } }, canonical(kept) });
    }

    // board
    {
        const QSet<QString> lm = launchMonitorMeasures(pack);
        QJsonArray shots;
        for (const BoardAnnotation &a : { BoardAnnotation{ true, QString() }, BoardAnnotation{ false, QStringLiteral("8 IRON") },
                                          BoardAnnotation{ true, QStringLiteral("6 IRON") } })
            shots.append(toJson(std::vector<ShotRecord>{ annotatedShot(fired.value(QStringLiteral("rich_7iron")), a, lm) }));
        QStringList ids(lm.begin(), lm.end());
        ids.sort();
        shots.append(QJsonArray::fromStringList(ids));
        out.insert(QStringLiteral("board"), Entry{ QJsonObject{ { QStringLiteral("boardRule"), kBoardAnnotationRuleVersion } }, canonical(shots) });
    }

    QJsonObject digests;
    for (auto it = out.constBegin(); it != out.constEnd(); ++it) digests.insert(it.key(), digestOf(it.value()));
    const QJsonObject now{
        { QStringLiteral("about"), QStringLiteral("the session and athlete reductions on the fired live_swings fixtures (reduction_rule_golden_test.cpp). "
                                                  "Re-record with PP_RECORD_RULE_GOLDEN=1 only after moving the version the change belongs to.") },
        { QStringLiteral("frozenContent"), contentKey(pack, *inputs.norms) },
        { QStringLiteral("reductions"), digests },
    };

    const QString goldenPath = frozen.filePath(QStringLiteral("reductions.json"));
    if (qEnvironmentVariableIntValue("PP_RECORD_RULE_GOLDEN") == 1) {
        QFile f(goldenPath);
        const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate)
                     && f.write(QJsonDocument(now).toJson(QJsonDocument::Indented)) > 0;
        check(ok, QStringLiteral("recorded the golden at ") + goldenPath);
        return g_fail ? 1 : 0;
    }
    QFile in(goldenPath);
    const QJsonObject golden = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
    check(!golden.isEmpty(), QStringLiteral("the golden is read (") + goldenPath + QLatin1Char(')'));
    if (golden.isEmpty()) return 1;
    check(golden.value(QStringLiteral("frozenContent")).toString() == now.value(QStringLiteral("frozenContent")).toString(),
          QStringLiteral("frozenContent: the frozen copy is the one the golden was recorded on (edited: re-record; no version owed)"));
    const QJsonObject want = golden.value(QStringLiteral("reductions")).toObject();
    for (auto it = out.constBegin(); it != out.constEnd(); ++it) {
        const QJsonObject w = want.value(it.key()).toObject();
        const QJsonObject g = digestOf(it.value());
        const bool sameVersions = w.value(QStringLiteral("versions")).toObject() == g.value(QStringLiteral("versions")).toObject();
        check(sameVersions, it.key() + QStringLiteral(": its rule versions are the golden's (a version moved on purpose: re-record)"));
        if (!sameVersions) continue;
        const bool same = w.value(QStringLiteral("sha1")).toString() == g.value(QStringLiteral("sha1")).toString();
        if (!same) {
            const QString dump = QDir::current().filePath(QStringLiteral("reduction_rule_golden_%1.json").arg(it.key()));
            QFile d(dump);
            if (d.open(QIODevice::WriteOnly | QIODevice::Truncate)) d.write(it.value().bytes);
            std::printf("        the output is at %s\n", qPrintable(dump));
        }
        check(same, it.key() + QStringLiteral(": the output is the golden's — if it moved on purpose, bump the version it belongs to "
                                              "(above) with a dated history line, then re-record"));
    }
    check(want.size() == out.size(), QStringLiteral("every reduction the golden pins is checked"));

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "passed", g_fail);
    return g_fail ? 1 : 0;
}
