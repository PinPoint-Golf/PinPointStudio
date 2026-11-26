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

// THE RULE VERSIONS ARE CHECKED (R-STAMP-3; audit C-05; refactor step 3.3). fireFindings() (fire_findings.h)
// on a fixed fixture — the four live_swings fixtures, a FROZEN copy of the pack, norms and contexts
// (data/rule_version_golden/core.json, norms.json, contexts.json: the shipped files as of bb729ca7, never read
// from src/Resources), fixed inputs — gives a record whose digest is pinned in data/rule_version_golden/golden.json,
// KEYED BY the versions the output answers to: kDiagSchemaVersion (the row shape), kRowWordsVersion (what a row
// says), kRowGradingVersion (how a row is graded), kPhaseGridSchemaVersion (the numbers it is graded on).
// Because the content is frozen, an edit to the shipped pack or norms never moves this test, and so can never
// hide a rule change made in the same commit.
//
//   • the versions match and a digest moved → FAIL: a rule's output changed and no version did. Bump the version
//     the change belongs to, with its dated history line, then re-record;
//   • a version moved                        → FAIL until the golden is re-recorded for it, so the golden always
//     pins the output of the versions in force;
//   • the frozen copy itself was edited      → FAIL (its own check, "frozenContent"): re-record, no version owed;
//   • everything matches                     → PASS.
// Re-record: PP_RECORD_RULE_GOLDEN=1 ctest --test-dir build/tests -R '^rule_version_golden_test$'
// (writes the golden into the source tree; commit it with the change that moved it).
//
// The digest is of the ledger's own JSON of the record (toJson), each number first SNAPPED — a magnitude below
// kZeroEpsilon (1e-9) is 0, and −0 is 0 — then rounded to 9 significant digits. The rounding absorbs the relative
// last-bit drift of floating-point contraction (FMA on arm64 clang, none on MSVC x64); the snap absorbs the
// absolute noise a difference of two near-equal numbers leaves around zero (a −1e-17 where the other compiler
// gives 0), which no relative rounding can. 1e-9 because every reading, corridor, z and share a row carries is
// many orders above it, while that noise is ~1e-12 or less for operands up to a few thousand. The one exception is
// a firing probability's far tail (`pFire` on a clean row, e.g. 1.5e-11 … 3e-37 on rich_7iron): those values are
// real, but no grade or word can depend on them, so they are snapped on purpose. On a mismatch the record is written beside the build
// (rule_version_golden_<fixture>.json) for diffing.

#include "../engine/fire_findings.h"
#include "../../DomainModel/rule_data.h"
#include "../../Stores/rule_data_install.h"
#include "../../Stores/rule_data_store.h"   // the norm factories by path (refactor step 7.1)
#include "../../Stores/phase_grid_store.h"   // the phase grid's file door (P-3)
#include "../engine/pack_io.h"                 // RuleFile: the frozen pack's bytes
#include "../norms/context_tree.h"
#include "../norms/norm_pack.h"
#include "../packs/characteristic_pack.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

constexpr double kZeroEpsilon = 1e-9;

// Every number snapped (|x| < kZeroEpsilon and −0 → 0) and rounded to 9 significant digits, recursively.
static QJsonValue rounded(const QJsonValue &v)
{
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (!std::isfinite(d)) return v;
        if (std::fabs(d) < kZeroEpsilon) return QJsonValue(0.0);   // −0 included
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

static QJsonObject versions()
{
    return QJsonObject{ { QStringLiteral("diagSchema"), kDiagSchemaVersion },
                        { QStringLiteral("rowWords"), kRowWordsVersion },
                        { QStringLiteral("rowGrading"), kRowGradingVersion },
                        { QStringLiteral("phaseGrid"), kPhaseGridSchemaVersion } };
}

static QString contentKey(const CharacteristicPack &pack, const INormProvider &norms)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QJsonDocument(savePack(pack)).toJson(QJsonDocument::Compact));
    h.addData(QJsonDocument(saveNormPack(norms.norms())).toJson(QJsonDocument::Compact));
    h.addData(QJsonDocument(saveContextTree(norms.contexts())).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(h.result().toHex().left(16));
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    pinpoint::installProcessRuleData();
    std::printf("rule_version_golden_test\n");

    // THE FROZEN CONTENT, never the shipped files (no bibliography: the loader's book gate is off, a fixed choice).
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
    check(inputs.pack && !inputs.pack->pack().measures.empty() && !inputs.norms->norms().norms.empty()
              && !inputs.norms->contexts().isEmpty(),
          QStringLiteral("the frozen pack, norms and contexts are read (") + frozen.path() + QLatin1Char(')'));
    if (g_fail) return 1;

    // A frozen 7-iron baseline, so the two history measures read on the monitor's shots.
    ClubBaselineEntry seven;
    seven.club = pinpoint::canonicalClubId(QStringLiteral("7 IRON"));
    seven.carryMean = 172.0;  seven.carrySd = 7.0;
    seven.ballSpeedMean = 118.0;  seven.ballSpeedSd = 3.0;
    seven.speedMean = 84.0;  seven.smashMean = 1.39;
    seven.n = 24;
    inputs.clubBaseline = QHash<QString, ClubBaselineEntry>{ { seven.club, seven } };

    QTemporaryDir tmp;
    const QStringList fixtures{ QStringLiteral("lm_7iron"), QStringLiteral("lm_partial"), QStringLiteral("rich_7iron"),
                                QStringLiteral("sparse_noclub") };
    QJsonObject digests;
    QHash<QString, QByteArray> canonical;
    for (const QString &name : fixtures) {
        const QString dir = stageFixture(tmp, name);
        FiringReadings readings;
        readings.grid       = pinpoint::PhaseGridStore::readForMeasures(dir, (inputs.pack->pack()).measures, pinpoint::SidecarPolicy::ParseAndWrite);
        readings.handedness = QStringLiteral("Right");
        const FiredShot fired = fireFindings(std::move(readings), inputs);
        int fired_ = 0, na = 0;
        for (const ConditionRow &r : fired.record.rows) {
            if (r.state == ShotState::Fired) ++fired_;
            if (r.state == ShotState::NotAssessable) ++na;
        }
        QJsonObject doc = toJson(std::vector<ShotRecord>{ fired.record });
        doc.insert(QStringLiteral("hasLaunchMonitor"), fired.hasLaunchMonitor);
        const QByteArray bytes = QJsonDocument(rounded(doc).toObject()).toJson(QJsonDocument::Indented);
        canonical.insert(name, bytes);
        digests.insert(name, QJsonObject{
            { QStringLiteral("sha1"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex()) },
            { QStringLiteral("rows"), int(fired.record.rows.size()) },
            { QStringLiteral("fired"), fired_ },
            { QStringLiteral("notAssessable"), na },
            { QStringLiteral("checks"), int(fired.record.signalRows.size()) },
        });
        check(fired.ok && !fired.record.rows.empty(), name + QStringLiteral(": fires"));
    }

    const QJsonObject now{
        { QStringLiteral("about"), QStringLiteral("fireFindings() on the live_swings fixtures (rule_version_golden_test.cpp). "
                                                  "Re-record with PP_RECORD_RULE_GOLDEN=1 only after moving the version the change belongs to.") },
        { QStringLiteral("versions"), versions() },
        { QStringLiteral("frozenContent"), contentKey(inputs.pack->pack(), *inputs.norms) },
        { QStringLiteral("fixtures"), digests },
    };

    const QString goldenPath = frozen.filePath(QStringLiteral("golden.json"));
    if (qEnvironmentVariableIntValue("PP_RECORD_RULE_GOLDEN") == 1) {
        QFile out(goldenPath);
        const bool ok = out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                     && out.write(QJsonDocument(now).toJson(QJsonDocument::Indented)) > 0;
        check(ok, QStringLiteral("recorded the golden at ") + goldenPath);
        return g_fail ? 1 : 0;
    }

    QFile in(goldenPath);
    const QJsonObject golden = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
    check(!golden.isEmpty(), QStringLiteral("the golden is read (") + goldenPath + QLatin1Char(')'));
    if (golden.isEmpty()) return 1;

    const bool sameVersions = golden.value(QStringLiteral("versions")).toObject() == versions();
    const bool sameContent  = golden.value(QStringLiteral("frozenContent")).toString()
                           == now.value(QStringLiteral("frozenContent")).toString();
    check(sameVersions, QStringLiteral("the rule versions are the golden's (if a version moved on purpose, re-record: "
                                       "PP_RECORD_RULE_GOLDEN=1)"));
    check(sameContent, QStringLiteral("frozenContent: the frozen copy under data/rule_version_golden/ is the one the "
                                      "golden was recorded on (it was edited: re-record; no version is owed)"));
    if (sameVersions && sameContent) {
        const QJsonObject want = golden.value(QStringLiteral("fixtures")).toObject();
        for (const QString &name : fixtures) {
            const QString got = digests.value(name).toObject().value(QStringLiteral("sha1")).toString();
            const bool same = want.value(name).toObject().value(QStringLiteral("sha1")).toString() == got;
            if (!same) {
                const QString dump = QDir::current().filePath(QStringLiteral("rule_version_golden_%1.json").arg(name));
                QFile d(dump);
                if (d.open(QIODevice::WriteOnly | QIODevice::Truncate)) d.write(canonical.value(name));
                std::printf("        the record is at %s\n", qPrintable(dump));
            }
            check(same, name + QStringLiteral(": fireFindings' output is the golden's — if it moved on purpose, bump the "
                                              "version it belongs to (fire_findings.h kRowGradingVersion for a grade, "
                                              "kRowWordsVersion for a word; diagnostic_ledger.h kDiagSchemaVersion for a "
                                              "field) with a dated history line, then re-record"));
        }
    }

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "passed", g_fail);
    return g_fail ? 1 : 0;
}
