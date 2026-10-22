// measure_dump_test — the dump reads what the app reads, writes nothing beside the swings, and never
// prints a missing number as a number.
//
// Two copies of the same two fixtures (live_measure_source_test's lm_7iron and sparse_noclub) are
// staged as sessions. Session A goes through measure_dump's core; session B through the APP's own
// reader (LiveMeasureSource(swingDir, pack) — the path SessionDiagnosticsModel::detectShot takes,
// which writes a phase-grid sidecar on a cache miss) and NormMeasureSource. Then:
//
//   1. every plain measure's value, grade and corridor flag agree, row for row, on both swings;
//   2. one known reading (the first that grades with a corridor) is spot-checked with its z against
//      the resolved norm's side σ, printed with both numbers;
//   3. session A's files are byte-for-byte what was staged: no sidecar, no ledger (read-only);
//   4. NaN prints empty — csvNumber, and the value cell of a measure that did not read;
//   5. a composite in the pack (if the pack carries one yet) emits its row plus one per part.
//
// No ids are pinned: the pack is being authored as this test is written.

#include "measure_dump_core.h"

#include "Diagnostics/norms/norm_measure_source.h"
#include "Diagnostics/packs/pack_provider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static bool sameNumber(double a, double b)
{
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    return a == b;   // bit-identical arithmetic: no tolerance
}

static bool stage(const QString &sessionDir, int n, const char *fixture)
{
    const QString dst = sessionDir + QStringLiteral("/swing_%1").arg(n, 4, 10, QLatin1Char('0'));
    if (!QDir().mkpath(dst)) return false;
    return QFile::copy(QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(fixture) + QStringLiteral("/swing.json")),
                       dst + QStringLiteral("/swing.json"));
}

// path -> sha1 of every file under `dir`.
static QMap<QString, QByteArray> snapshot(const QString &dir)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        QFile f(p);
        if (f.open(QIODevice::ReadOnly))
            out.insert(p, QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1));
    }
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::printf("measure_dump_test: PINPOINT_CORE_PACK unset - run through ctest\n");
        return 1;
    }

    QTemporaryDir tmp;
    check(tmp.isValid(), "temporary directory");
    const QString sessA = tmp.filePath(QStringLiteral("athlete/sessA"));
    const QString sessB = tmp.filePath(QStringLiteral("athlete/sessB"));
    const char *fixtures[] = { "lm_7iron", "sparse_noclub" };
    bool staged = true;
    for (int i = 0; i < 2; ++i)
        staged = staged && stage(sessA, i + 1, fixtures[i]) && stage(sessB, i + 1, fixtures[i]);
    check(staged, "two fixtures staged twice");
    if (!staged) return 1;

    const auto packProv = makeCharacteristicPackProvider();
    const auto norms    = sharedNormProvider();
    const CharacteristicPack &pack = packProv->pack();
    const GradePolicy policy = gradePolicyByName(QStringLiteral("standard"));

    measure_dump::Options opt;
    const QStringList ids = measure_dump::selectMeasures(pack, opt);
    check(ids.size() > 50, "the whole pack is selected by default");

    const QMap<QString, QByteArray> before = snapshot(sessA);
    const QStringList swingsA = measure_dump::swingDirsOf(sessA);
    check(swingsA.size() == 2, "both swing directories listed");

    std::printf("\n1-2. the dump agrees with the app's reader\n");
    int compared = 0, mismatched = 0, valued = 0;
    bool spotDone = false;
    for (int s = 0; s < swingsA.size(); ++s) {
        const std::vector<measure_dump::Row> rows =
            measure_dump::readSwing(sessA, swingsA.at(s), pack, norms, ids, policy);
        QMap<QString, const measure_dump::Row *> byId;
        for (const measure_dump::Row &r : rows) byId.insert(r.measure, &r);

        // The app's path, on the twin copy.
        const QString swingB = sessB + QLatin1Char('/') + QFileInfo(swingsA.at(s)).fileName();
        const LiveMeasureSource values(swingB, pack);
        const NormMeasureSource graded(values, norms, values.contextId(), policy, &pack, Cohort{});

        for (const QString &id : ids) {
            const Measure *m = pack.measure(id);
            if (!m || m->kind == MeasureKind::Composite) continue;
            const measure_dump::Row *row = byId.value(id, nullptr);
            const std::optional<MeasureReading> rd = graded.read(id);
            ++compared;
            const double want = rd ? rd->value : measure_dump::kNaN;
            const QString wantGrade = (rd && rd->hasCorridor) ? gradeName(rd->grade) : QString();
            const bool ok = row && sameNumber(row->value, want) && row->grade == wantGrade
                         && row->hasCorridor == (rd ? (rd->hasCorridor ? 1 : 0) : -1);
            if (!ok) {
                ++mismatched;
                std::printf("  MISMATCH %s %s: dump %s/%s, app %s/%s\n", qPrintable(QFileInfo(swingsA.at(s)).fileName()),
                            qPrintable(id), row ? qPrintable(measure_dump::csvNumber(row->value)) : "(no row)",
                            row ? qPrintable(row->grade) : "", qPrintable(measure_dump::csvNumber(want)),
                            qPrintable(wantGrade));
            }
            if (rd && std::isfinite(rd->value)) ++valued;

            if (!spotDone && rd && rd->hasCorridor && row) {
                const NormResolution res = norms->resolve(id, values.contextId(), Cohort{});
                if (res.found()) {
                    const double side = rd->value < res.norm->mu ? res.norm->sigmaLo : res.norm->sigmaHi;
                    const double z = (rd->value - res.norm->mu) / side;
                    std::printf("  spot: %s on %s = %.9g (app %.9g), z %.6g (from norm mu %.6g side sigma %.6g: %.6g)\n",
                                qPrintable(id), qPrintable(QFileInfo(swingsA.at(s)).fileName()), row->value,
                                rd->value, row->z, res.norm->mu, side, z);
                    check(row->value == rd->value, "spot: value equals NormMeasureSource's");
                    check(std::fabs(row->z - z) < 1e-12, "spot: z is (value - mu) / side sigma of the resolved norm");
                    spotDone = true;
                }
            }
        }

        // 5. composites, when the pack has any.
        for (const QString &id : ids) {
            const Measure *m = pack.measure(id);
            if (!m || m->kind != MeasureKind::Composite) continue;
            int compRows = 0, partRows = 0;
            for (const measure_dump::Row &r : rows) {
                if (r.measure == id) ++compRows;
                if (r.measure.startsWith(id + QLatin1Char('/')) && r.kind == QLatin1String("part")) ++partRows;
            }
            check(compRows == 1 && partRows == int(m->composite.parts.size()),
                  qPrintable(QStringLiteral("composite %1: one row and one per part on %2")
                                 .arg(id, QFileInfo(swingsA.at(s)).fileName())));
        }
    }
    std::printf("  compared %d readings, %d with a value, %d mismatched\n", compared, valued, mismatched);
    check(compared > 100 && mismatched == 0, "every plain reading matches the app's, value and grade");
    check(valued > 0, "some readings have values (the fixtures are not empty)");
    check(spotDone, "a reading with a corridor was spot-checked");

    std::printf("\n3. read-only\n");
    const QMap<QString, QByteArray> after = snapshot(sessA);
    check(before == after, "session A: no file added, removed or changed");
    const QMap<QString, QByteArray> twin = snapshot(sessB);
    std::printf("  (the app's reader left %lld files in session B, the dump %lld in A)\n",
                static_cast<long long>(twin.size()), static_cast<long long>(after.size()));

    std::printf("\n4. NaN prints empty\n");
    check(measure_dump::csvNumber(measure_dump::kNaN).isEmpty(), "csvNumber(NaN) is empty");
    check(measure_dump::csvNumber(std::numeric_limits<double>::infinity()).isEmpty(), "csvNumber(inf) is empty");
    check(measure_dump::csvNumber(0.0) == QLatin1String("0"), "csvNumber(0) is 0 - a real zero stays");
    {
        const std::vector<measure_dump::Row> rows =
            measure_dump::readSwing(sessA, swingsA.at(1), pack, norms, ids, policy);
        const int valueCol = measure_dump::csvColumns().indexOf(QStringLiteral("value"));
        const int zCol     = measure_dump::csvColumns().indexOf(QStringLiteral("z"));
        int missing = 0, badCells = 0, nanText = 0;
        for (const measure_dump::Row &r : rows) {
            const QStringList f = measure_dump::rowFields(r);
            if (f.size() != measure_dump::csvColumns().size()) ++badCells;
            for (const QString &cell : f) {
                const QString c = cell.toLower();
                if (c == QLatin1String("nan") || c == QLatin1String("-nan") || c == QLatin1String("inf")
                    || c == QLatin1String("-inf"))
                    ++nanText;
            }
            if (!std::isnan(r.value)) continue;
            ++missing;
            if (!f.at(valueCol).isEmpty() || !f.at(zCol).isEmpty() || r.refusal.isEmpty()) ++badCells;
        }
        std::printf("  sparse_noclub: %lld rows, %d with no value\n", static_cast<long long>(rows.size()), missing);
        check(missing > 0, "sparse_noclub has measures that do not read");
        check(badCells == 0, "every unread row has empty value and z cells and names its refusal");
        check(nanText == 0, "no cell reads nan or inf");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
