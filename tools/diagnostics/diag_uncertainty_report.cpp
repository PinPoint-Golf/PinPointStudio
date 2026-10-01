// diag_uncertainty_report — the §A8 gates, applied to real sessions, READ-ONLY on the library.
//
// session_diagnostics_design.md §A8.8. For every session directory given (laid out by
// diag_uncertainty_prep.py from a scratch sweep — never the library itself), the panel's own model
// is run six times, once per combination of the measurement-uncertainty switches:
//
//   M0  σ off, soft tier off, posterior off   — the pre-§A8 panel
//   M1  σ on                                   — G1: every shot's verdict identical to M0
//   M2  σ on, soft tier                        — G3: a tier that moves must rest on a borderline shot
//   M3  σ on, soft tier, posterior ranking     — G4: the root order, REPORTED, never gated
//   S0  σ off, soft tier                       — G2: no σ ⇒ the tiers and roots of M0
//   P0  σ off, soft tier, posterior            — G2: the posterior's own reordering, reported
//
// plus, on M3: the re-rank cost (one explain() and its stability draws), the WORD SENSITIVITY of
// the top root (every Causes edge's strength and every condition's prominence moved one rung each
// way; a single word that flips the top root is named), and the screen calibration table (empty
// until screens are entered in the library's sessions).
//
//   export PINPOINT_CORE_PACK=src/Resources/diagnostics/core.json   (and NORMS, CONTEXTS, SCREENS,
//                                                                    DRILLS, REFERENCES likewise)
//   ./build/tests/Analysis/diag_uncertainty_report --out <dir> [--library <lib>] <session-dir>...
//
// Writes <out>/diag_uncertainty_report.md and .json. The session dirs get a diagnostics.json and
// phase-grid sidecars as the model always writes; they are scratch and are left for inspection.
// NOT A TEST, not registered with ctest — precedent: rank_shift_report, regrade_ledger.

#include "Gui/diagnostics/session_diagnostics_model.h"
#include "Diagnostics/pack_provider.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <map>

using namespace pinpoint::analysis;

namespace {

struct Mode { const char *name; bool sigma, soft, post; };
const Mode kModes[] = {
    { "M0", false, false, false }, { "M1", true, false, false }, { "M2", true, true, false },
    { "M3", true, true, true },    { "S0", false, true, false }, { "P0", false, true, true },
};

struct RunOut {
    std::map<std::pair<int, QString>, ConditionRow> rows;   // (shot, condition) → row
    std::map<QString, ConditionLedger>              ledgers;
    std::vector<RankedCause>                        roots;
    std::vector<ShotRecord>                         shots;
};

RunOut runMode(const QString &dir, const Mode &m)
{
    QFile::remove(QDir(dir).filePath(QStringLiteral("diagnostics.json")));
    SessionDiagnosticsModel model;
    model.setSynchronous(true);
    model.setCadence(QStringLiteral("everyShot"));
    model.setUncertaintyModes(m.sigma, m.soft, m.post);
    model.activateSession(dir);
    RunOut o;
    o.shots = model.shotRecords();
    for (const ShotRecord &s : o.shots)
        for (const ConditionRow &r : s.rows) o.rows[{ s.shotId, r.conditionId }] = r;
    for (const ConditionLedger &l : model.ledgerRows()) o.ledgers[l.id] = l;
    o.roots = model.explanation().roots;
    QFile::remove(QDir(dir).filePath(QStringLiteral("diagnostics.json")));
    return o;
}

QStringList rootIds(const std::vector<RankedCause> &rs)
{
    QStringList out;
    for (const RankedCause &r : rs) out << r.conditionId;
    return out;
}

const char *tierName(Tier t) { return t == Tier::Pattern ? "Pattern" : t == Tier::Watching ? "Watching" : "Clean"; }

// The session's soft evidence, exactly as SessionDiagnosticsModel::refreshExplanation builds it.
DetectionResult sessionEvidence(const std::map<QString, ConditionLedger> &ledgers)
{
    DetectionResult det;
    for (const auto &[id, l] : ledgers) {
        if (l.tier != Tier::Pattern && l.assessable < 1) continue;
        Finding f;
        f.conditionId = id;
        f.state       = l.tier == Tier::Pattern ? FindingState::Fired : FindingState::NotFired;
        f.confidence  = 1.0f;
        f.pFire       = float(l.pPattern);
        det.findings.push_back(std::move(f));
    }
    return det;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString     outDir, library;
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--out") && i + 1 < argc)     { outDir  = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--library") && i + 1 < argc) { library = QString::fromLocal8Bit(argv[++i]); continue; }
        dirs << a;
    }
    if (dirs.isEmpty() || outDir.isEmpty()) {
        std::fprintf(stderr, "usage: diag_uncertainty_report --out <dir> [--library <lib>] <session-dir>...\n");
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::fprintf(stderr, "diag_uncertainty_report: export PINPOINT_CORE_{PACK,NORMS,CONTEXTS,SCREENS,DRILLS,"
                             "REFERENCES} at src/Resources/diagnostics/*.json first.\n");
        return 2;
    }
    QDir().mkpath(outDir);
    const auto packProv = makeCharacteristicPackProvider();
    const CharacteristicPack &pack = packProv->pack();

    QStringList md;
    QJsonArray  sessionsJson;
    int g1Rows = 0, g1Bad = 0, g2TierBad = 0, g2RootBad = 0, g3Moves = 0, g3Bad = 0, g4Changed = 0;
    int rowsAll = 0, rowsQuant = 0, rowsBorder = 0;
    std::map<QString, int> flipEdges, flipConds;
    double costMax = 0.0;
    int screensSeen = 0;

    for (const QString &dir : dirs) {
        const QString name = QDir(dir).dirName();   // the session dirs arrive with a trailing slash
        std::map<QString, RunOut> R;
        for (const Mode &m : kModes) R[m.name] = runMode(dir, m);
        QJsonObject sj;
        sj[QStringLiteral("session")] = name;
        md << QStringLiteral("## %1 — %2 shots").arg(name).arg(R["M0"].shots.size()) << QString();

        // What the panel concludes under the old logic (M0) and the new (M3): patterns and roots.
        for (const char *mn : { "M0", "M3" }) {
            QStringList pats;
            QJsonArray  patJ;
            for (const auto &[id, l] : R[mn].ledgers)
                if (l.tier == Tier::Pattern) {
                    pats << QStringLiteral("%1 (%2/%3%4)").arg(id).arg(l.fired).arg(l.assessable)
                                .arg(QString::fromLatin1(mn) == QLatin1String("M3")
                                         ? QStringLiteral(", P %1").arg(l.pPattern, 0, 'f', 2) : QString());
                    patJ.append(id);
                }
            QJsonArray rootJ;
            for (const RankedCause &rc : R[mn].roots) rootJ.append(rc.conditionId);
            md << QStringLiteral("%1 patterns: %2").arg(QString::fromLatin1(mn), pats.isEmpty() ? QStringLiteral("none") : pats.join(QStringLiteral(", ")));
            md << QStringLiteral("%1 roots: %2").arg(QString::fromLatin1(mn),
                     R[mn].roots.empty() ? QStringLiteral("none") : rootIds(R[mn].roots).join(QStringLiteral(", ")));
            sj[QStringLiteral("patterns") + QString::fromLatin1(mn)] = patJ;
            sj[QStringLiteral("roots") + QString::fromLatin1(mn)]    = rootJ;
        }
        md << QString();

        // G1 — verdicts identical with σ on.
        int bad = 0;
        for (const auto &[k, r0] : R["M0"].rows) {
            ++g1Rows;
            const auto it = R["M1"].rows.find(k);
            if (it == R["M1"].rows.end() || it->second.state != r0.state) ++bad;
        }
        g1Bad += bad;
        for (const auto &[k, r] : R["M1"].rows) {
            if (r.state == ShotState::NotAssessable) continue;
            ++rowsAll;
            if (r.quantified) ++rowsQuant;
            if ((r.pFire > float(pinpoint::tuned::diagUncertainty::kBorderLo) && r.pFire < float(pinpoint::tuned::diagUncertainty::kBorderHi))
                || r.grossRisk > float(pinpoint::tuned::diagUncertainty::kBorderGross)) ++rowsBorder;
        }
        md << QStringLiteral("G1: %1 verdict mismatches with σ on.").arg(bad);

        // G2 — no σ ⇒ today's tiers and roots under the soft tier; the posterior's reorder reported.
        int tierBad = 0;
        for (const auto &[id, l] : R["M0"].ledgers)
            if (R["S0"].ledgers.count(id) == 0 || R["S0"].ledgers[id].tier != l.tier) ++tierBad;
        const bool rootsSame = rootIds(R["M0"].roots) == rootIds(R["S0"].roots);
        g2TierBad += tierBad; g2RootBad += rootsSame ? 0 : 1;
        md << QStringLiteral("G2: %1 tier changes and %2 with σ withheld under the soft tier. "
                             "Posterior with σ withheld: roots %3 → %4.")
                  .arg(tierBad).arg(rootsSame ? QStringLiteral("the same roots") : QStringLiteral("DIFFERENT roots"))
                  .arg(rootIds(R["M0"].roots).join(QStringLiteral(", ")), rootIds(R["P0"].roots).join(QStringLiteral(", ")));

        // G3 — every tier move under the soft tier rests on a borderline shot.
        QStringList moves;
        for (const auto &[id, l2] : R["M2"].ledgers) {
            const auto it = R["M1"].ledgers.find(id);
            if (it == R["M1"].ledgers.end() || it->second.tier == l2.tier) continue;
            ++g3Moves;
            if (!l2.borderline) ++g3Bad;
            moves << QStringLiteral("- %1: %2 → %3 (%4 of %5 shots fired; P(Pattern) %6)%7")
                         .arg(id, tierName(it->second.tier), tierName(l2.tier))
                         .arg(l2.fired).arg(l2.assessable).arg(l2.pPattern, 0, 'f', 2)
                         .arg(l2.borderline ? QString() : QStringLiteral(" **NOT BORDERLINE — G3 FAIL**"));
        }
        md << QStringLiteral("G3: %1 tier moves under the soft tier.").arg(moves.size());
        md << moves;

        // G4 — the root order, reported.
        const QStringList before = rootIds(R["M2"].roots), after = rootIds(R["M3"].roots);
        if (before != after) ++g4Changed;
        md << QStringLiteral("G4: roots, sum score → posterior: %1 → %2")
                  .arg(before.join(QStringLiteral(", ")), after.join(QStringLiteral(", ")));
        for (const RankedCause &rc : R["M3"].roots)
            md << QStringLiteral("  - %1: score %6, P(present) %2, old score %3, explains %4, %5")
                      .arg(rc.conditionId).arg(rc.posterior, 0, 'f', 3).arg(rc.legacyScore, 0, 'f', 3)
                      .arg(rc.coverage).arg(rc.stabilityWord.isEmpty() ? QStringLiteral("(unstamped)") : rc.stabilityWord)
                      .arg(rc.score, 0, 'f', 3);

        // Cost of one re-rank on M3: explain() once plus one per stability draw.
        {
            LedgerOptions opt;
            opt.softTier = true;
            const DetectionResult det = sessionEvidence(R["M3"].ledgers);
            QElapsedTimer t; t.start();
            ExplainOptions eo; eo.posteriorRank = true;
            Explanation ex = explain(pack, det, {}, eo);
            std::vector<Explanation> drawn;
            QHash<QString, int>      seen;   // the model's own de-duplication, mirrored
            QElapsedTimer td; td.start();
            const std::vector<QSet<QString>> pats = patternDraws(R["M3"].shots, opt);
            const double drawMs = double(td.nsecsElapsed()) / 1e6;
            for (const QSet<QString> &pat : pats) {
                QStringList key(pat.begin(), pat.end());
                key.sort();
                const QString k = key.join(QLatin1Char('|'));
                if (const auto it = seen.constFind(k); it != seen.constEnd()) {
                    drawn.push_back(drawn[size_t(it.value())]);
                    continue;
                }
                seen.insert(k, int(drawn.size()));
                DetectionResult dd;
                for (const auto &[id, l] : R["M3"].ledgers) {
                    if (l.assessable < 1) continue;
                    Finding f; f.conditionId = id; f.confidence = 1.0f;
                    f.state = pat.contains(id) ? FindingState::Fired : FindingState::NotFired;
                    dd.findings.push_back(std::move(f));
                }
                drawn.push_back(explain(pack, dd, {}, eo));
            }
            stampStability(ex, drawn);
            const double ms = double(t.nsecsElapsed()) / 1e6;
            costMax = std::max(costMax, ms);
            md << QStringLiteral("Re-rank cost: %1 ms (explain + %2 stability draws: drawing %3 ms, %4 distinct Pattern sets explained).")
                      .arg(ms, 0, 'f', 1).arg(drawn.size()).arg(drawMs, 0, 'f', 1).arg(seen.size());

            // Word sensitivity: one rung either way on each word; name any single word that flips
            // the top root.
            if (!ex.roots.empty()) {
                const QString top = ex.roots.front().conditionId;
                QStringList flips;
                for (size_t i = 0; i < pack.edges.size(); ++i) {
                    if (pack.edges[i].type != EdgeType::Causes) continue;
                    for (int dlt : { -1, 1 }) {
                        const int s = int(pack.edges[i].strength) + dlt;
                        if (s < int(Strength::VeryWeak) || s > int(Strength::VeryStrong)) continue;
                        CharacteristicPack p2 = pack;
                        p2.edges[i].strength = Strength(s);
                        const Explanation e2 = explain(p2, det, {}, eo);
                        const QString t2 = e2.roots.empty() ? QString() : e2.roots.front().conditionId;
                        if (t2 != top) {
                            const QString key = QStringLiteral("%1 → %2").arg(pack.edges[i].from, pack.edges[i].to);
                            ++flipEdges[key];
                            flips << QStringLiteral("edge %1 %2 → %3 puts %4 first")
                                         .arg(key, strengthLabel(pack.edges[i].strength), strengthLabel(Strength(s)),
                                              t2.isEmpty() ? QStringLiteral("no root") : t2);
                        }
                    }
                }
                for (size_t i = 0; i < pack.conditions.size(); ++i) {
                    for (int dlt : { -1, 1 }) {
                        const int pr = int(pack.conditions[i].prominence) + dlt;
                        if (pr < int(Prominence::Rare) || pr > int(Prominence::Ubiquitous)) continue;
                        CharacteristicPack p2 = pack;
                        p2.conditions[i].prominence = Prominence(pr);
                        const Explanation e2 = explain(p2, det, {}, eo);
                        const QString t2 = e2.roots.empty() ? QString() : e2.roots.front().conditionId;
                        if (t2 != top) {
                            ++flipConds[pack.conditions[i].id];
                            flips << QStringLiteral("prominence of %1 %2 → %3 puts %4 first")
                                         .arg(pack.conditions[i].id, prominenceLabel(pack.conditions[i].prominence),
                                              prominenceLabel(Prominence(pr)), t2.isEmpty() ? QStringLiteral("no root") : t2);
                        }
                    }
                }
                md << QStringLiteral("Word sensitivity: %1 single-word moves change the top root (%2).")
                          .arg(flips.size()).arg(top);
                for (const QString &f : flips) md << QStringLiteral("  - ") + f;
            }
        }

        // Screens entered in the LIBRARY's session (the scratch copy has none of its own).
        if (!library.isEmpty()) {
            QFile f(QDir(library).filePath(name + QStringLiteral("/diagnostics.json")));
            if (f.open(QIODevice::ReadOnly))
                screensSeen += QJsonDocument::fromJson(f.readAll()).object()
                                   .value(QStringLiteral("screens")).toObject().size();
        }
        md << QString();
        sj[QStringLiteral("rootsBefore")] = QJsonArray::fromStringList(before);
        sj[QStringLiteral("rootsAfter")]  = QJsonArray::fromStringList(after);
        sj[QStringLiteral("tierMoves")]   = int(moves.size());
        sessionsJson.append(sj);
    }

    QStringList head;
    head << QStringLiteral("# Diagnostics uncertainty — gate report (session_diagnostics_design.md §A8.8)") << QString()
         << QStringLiteral("%1 sessions. Assessed rows with σ on: %2, of which %3 quantified and %4 borderline.")
                .arg(dirs.size()).arg(rowsAll).arg(rowsQuant).arg(rowsBorder) << QString()
         << QStringLiteral("| Gate | Result | Detail |") << QStringLiteral("|---|---|---|")
         << QStringLiteral("| G1 verdicts identical with σ on | %1 | %2 mismatches over %3 rows |")
                .arg(g1Bad == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")).arg(g1Bad).arg(g1Rows)
         << QStringLiteral("| G2 no σ ⇒ today's tiers and roots (soft tier) | %1 | %2 tier changes, %3 sessions with different roots |")
                .arg(g2TierBad == 0 && g2RootBad == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")).arg(g2TierBad).arg(g2RootBad)
         << QStringLiteral("| G3 soft-tier moves rest on borderline shots | %1 | %2 moves, %3 not borderline |")
                .arg(g3Bad == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")).arg(g3Moves).arg(g3Bad)
         << QStringLiteral("| G4 posterior ranking (reported, not gated) | — | root order changed in %1 of %2 sessions |")
                .arg(g4Changed).arg(dirs.size())
         << QStringLiteral("| Cost < 50 ms per re-rank | %1 | worst %2 ms |")
                .arg(costMax < 50.0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")).arg(costMax, 0, 'f', 1)
         << QString()
         << QStringLiteral("Screen calibration (§A8.7): %1 screen results entered across these sessions%2")
                .arg(screensSeen).arg(screensSeen == 0 ? QStringLiteral(" — the table is empty until screens are entered.")
                                                       : QStringLiteral("."))
         << QString();
    if (!flipEdges.empty() || !flipConds.empty()) {
        head << QStringLiteral("Words that alone decide a top root somewhere (sessions affected):");
        for (const auto &[k, n] : flipEdges) head << QStringLiteral("- edge %1 (%2)").arg(k).arg(n);
        for (const auto &[k, n] : flipConds) head << QStringLiteral("- prominence of %1 (%2)").arg(k).arg(n);
        head << QString();
    }

    QFile mf(QDir(outDir).filePath(QStringLiteral("diag_uncertainty_report.md")));
    if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        mf.write((head + md).join(QLatin1Char('\n')).toUtf8());
    QFile jf(QDir(outDir).filePath(QStringLiteral("diag_uncertainty_report.json")));
    if (jf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        jf.write(QJsonDocument(QJsonObject{ { QStringLiteral("sessions"), sessionsJson } }).toJson());
    std::printf("%s\n", qPrintable(head.join(QLatin1Char('\n'))));
    return 0;
}
