// diag_rank_diff — two packs over the same sessions, READ-ONLY on the library.
//
// trunk_imu_sequence_grf_design.md §10.8 (gates) and §10.9 P5. rank_shift_report is synthetic and
// cannot compare two packs on real swings; this runs the Session diagnostics panel's own model
// (SessionDiagnosticsModel — detectShot()'s detectForSwing with the sequence-check signals, the
// ledger reduction, explain() over the pattern set) once under pack A and once under pack B, and
// writes what moved:
//
//   roots_by_session.csv   session, rank, root_a, score_a, root_b, score_b (top 10 each)
//   firing_counts.csv      condition, fired_a, fired_b, assessable_a, assessable_b (all swings)
//   firing_changes.csv     one row per swing × condition whose fired state differs, with B's
//                          driving measure and value
//   sequence_checks.csv    pack B's F1–F7, F9 per swing (✓ ✗ ? —, pFire), n of m, Σ(1 − pFire),
//                          clubhead speed and smash — the §10.6 count's validation input
//
// plus a summary on stdout.
//
// NOTHING IS WRITTEN INTO A SESSION. The model writes diagnostics.json after every batch and the
// swing_phasegrid.json / swing_summary.json sidecars beside each swing, so it never sees the
// library: each session is mirrored into <out>/.work/<athlete>/<session>/ first — the swing
// document (swing.ppsw or swing.json) and its sidecars, copied with their modification times so
// the sidecar guards still match and the cheap read is taken exactly as the app takes it — and
// the model is pointed at the mirror. The mirror's diagnostics.json carries only the original's
// intent (focus, declared miss), entered screens and ledger options, so every shot is detected
// fresh under each pack and both packs see the same intent. The mirror is deleted afterwards
// (--keep-work keeps it). The library is only ever opened for reading.
//
//   ./build/tests/Analysis/diag_rank_diff --pack-a <dir> --pack-b <dir> --out <dir> <session>...
//
// Each pack dir holds core.json, norms.json, contexts.json, screens.json, drills.json and
// references.json — the files the PINPOINT_CORE_* variables name. A file missing from a dir falls
// back to that variable as it was set when the tool started, else to the checkout's
// src/Resources/diagnostics; which was used is printed. Drills are resolved for completeness but
// the model never reads them.
//
// NOT A TEST, not registered with ctest — precedent: rank_shift_report, diag_uncertainty_report.

#include "Gui/diagnostics/session_diagnostics_model.h"
#include "Coaching/sequence_checks.h"
#include "Diagnostics/measures/live_measure_source.h"
#include "Diagnostics/norms/norm_provider.h"
#include "Diagnostics/packs/pack_provider.h"
#include "Diagnostics/packs/reference_pack.h"
#include "Diagnostics/packs/screen_pack.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

using namespace pinpoint::analysis;

#ifndef PP_DIAG_RES_DIR
#define PP_DIAG_RES_DIR ""
#endif

namespace {

// ── Packs ───────────────────────────────────────────────────────────────────────────────────────

struct PackFile { const char *stem; const char *env; };
const PackFile kPackFiles[] = {
    { "core",       "PINPOINT_CORE_PACK" },     { "norms",  "PINPOINT_CORE_NORMS" },
    { "contexts",   "PINPOINT_CORE_CONTEXTS" }, { "screens", "PINPOINT_CORE_SCREENS" },
    { "drills",     "PINPOINT_CORE_DRILLS" },   { "references", "PINPOINT_CORE_REFERENCES" },
};

struct Pack {
    QString label;
    std::map<QString, QString> path;   // env var → file
};

std::map<QString, QString> g_startEnv;   // the PINPOINT_CORE_* variables as the tool found them

bool resolvePack(const QString &label, const QString &dir, Pack &out)
{
    out.label = label;
    bool ok = true;
    for (const PackFile &pf : kPackFiles) {
        const QString file = QString::fromLatin1(pf.stem) + QStringLiteral(".json");
        QString path, from;
        if (!dir.isEmpty() && QFile::exists(QDir(dir).filePath(file))) {
            path = QDir(dir).absoluteFilePath(file);
            from = QStringLiteral("pack dir");
        } else if (const QString env = g_startEnv[QLatin1String(pf.env)]; !env.isEmpty()) {
            path = env;
            from = QStringLiteral("$") + QLatin1String(pf.env);
        } else {
            path = QDir::cleanPath(QDir(QStringLiteral(PP_DIAG_RES_DIR)).filePath(file));
            from = QStringLiteral("checkout");
        }
        if (!QFile::exists(path)) { from += QStringLiteral(" — MISSING"); ok = false; }
        std::printf("pack %s: %-16s <- %s (%s)\n", qPrintable(label), qPrintable(file),
                    qPrintable(path), qPrintable(from));
        out.path[QLatin1String(pf.env)] = path;
    }
    return ok;
}

// Point every content reader at this pack, and drop the process-wide caches that would otherwise
// keep serving the previous one. The characteristic pack has no cache: each model builds its own.
void applyPack(const Pack &p)
{
    for (const auto &[env, path] : p.path) qputenv(env.toLatin1().constData(), path.toLocal8Bit());
    resetSharedNormProvider();
    resetSharedScreenSet();
    resetSharedReferenceSet();
}

// ── The mirror ──────────────────────────────────────────────────────────────────────────────────

// Copy one file and give the copy the source's modification time: the phase-grid and summary
// sidecars are guarded on the document's size AND mtime, so a copy with a fresh mtime would miss
// both caches and read differently from the app (slower, and through the fallback paths).
bool copyPreserving(const QString &src, const QString &dst)
{
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) return false;
    QFile f(dst);
    if (!f.open(QIODevice::ReadWrite)) return false;
    const QDateTime mt = QFileInfo(src).lastModified();
    const bool ok = f.setFileTime(mt, QFileDevice::FileModificationTime);
    f.close();
    return ok;
}

const char *kSwingFiles[] = { "swing.ppsw", "swing.json", "swing_phasegrid.json", "swing_summary.json" };

int folderShotId(const QString &name)
{
    bool ok = false;
    const int id = name.mid(name.lastIndexOf(QLatin1Char('_')) + 1).toInt(&ok);
    return ok && id >= 0 ? id : -1;
}

// Mirror a session's swing documents under `workRoot`. Returns the mirror's session dir, fills
// shotId → swing folder name, and the original's intent/screens/options for the mirror's ledger.
QString mirrorSession(const QString &sessionDir, const QString &workRoot,
                      std::map<int, QString> &swingNames, QByteArray &intentDoc, int *copyFailures)
{
    const QFileInfo si(sessionDir);
    const QString athlete = QFileInfo(si.absolutePath()).fileName();
    const QString work = QDir(workRoot).filePath(athlete + QLatin1Char('/') + si.fileName());
    QDir(work).removeRecursively();
    QDir().mkpath(work);

    const QDir src(sessionDir);
    for (const QString &name : src.entryList(QStringList{ QStringLiteral("swing_*") },
                                             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const int id = folderShotId(name);
        if (id < 0) continue;
        swingNames[id] = name;
        const QString dstDir = QDir(work).filePath(name);
        QDir().mkpath(dstDir);
        for (const char *f : kSwingFiles) {
            const QString s = src.filePath(name + QLatin1Char('/') + QLatin1String(f));
            if (!QFile::exists(s)) continue;
            if (!copyPreserving(s, QDir(dstDir).filePath(QLatin1String(f)))) ++*copyFailures;
        }
    }

    // What the panel would carry over from the session's own ledger and that is NOT evidence: the
    // declared focus and miss, the screens entered at the range, the ledger's gates. The rows are
    // left behind so that every shot is detected under the pack being compared.
    QJsonObject root;
    QFile f(src.filePath(QStringLiteral("diagnostics.json")));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject orig = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        for (const char *k : { "schemaVersion", "intent", "screens" })
            if (orig.contains(QLatin1String(k))) root[QLatin1String(k)] = orig.value(QLatin1String(k));
        const QJsonObject ledger = orig.value(QStringLiteral("ledger")).toObject();
        if (ledger.contains(QStringLiteral("options")))
            root[QStringLiteral("ledger")] = QJsonObject{ { QStringLiteral("options"), ledger.value(QStringLiteral("options")) } };
    }
    intentDoc = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return work;
}

// ── One pack over one session ───────────────────────────────────────────────────────────────────

struct RunOut {
    std::vector<ShotRecord>  shots;
    std::vector<RankedCause> roots;
    double                   ms = 0.0;
};

RunOut runPack(const Pack &p, const QString &work, const QByteArray &intentDoc)
{
    applyPack(p);
    QFile lf(QDir(work).filePath(QStringLiteral("diagnostics.json")));
    if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) { lf.write(intentDoc); lf.close(); }

    RunOut o;
    QElapsedTimer t;
    t.start();
    {
        SessionDiagnosticsModel m;
        m.setSynchronous(true);
        m.setReviewing(true);            // a recorded session, opened as the panel opens one
        m.activateSession(work);
        o.shots = m.shotRecords();
        o.roots = m.explanation().roots;
    }
    o.ms = double(t.nsecsElapsed()) / 1e6;
    return o;
}

// ── Output helpers ──────────────────────────────────────────────────────────────────────────────

QString csvField(const QString &s)
{
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n')))
        return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

struct Csv {
    QFile f;
    explicit Csv(const QString &path, const QStringList &header) : f(path)
    {
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            std::fprintf(stderr, "diag_rank_diff: cannot write %s\n", qPrintable(path));
        row(header);
    }
    void row(const QStringList &cells)
    {
        QStringList c;
        for (const QString &s : cells) c << csvField(s);
        f.write((c.join(QLatin1Char(',')) + QLatin1Char('\n')).toUtf8());
    }
};

QString num(double v, int dp = 3) { return std::isfinite(v) ? QString::number(v, 'f', dp) : QString(); }
QString pf(float p)               { return p >= 0.f ? QString::number(double(p), 'f', 3) : QString(); }

QString stateName(const ConditionRow *r)
{
    if (!r) return QStringLiteral("absent");
    switch (r->state) {
    case ShotState::Fired:         return QStringLiteral("fired");
    case ShotState::Clean:         return QStringLiteral("clean");
    case ShotState::NotAssessable: break;
    }
    return QStringLiteral("na");
}

bool fired(const ConditionRow *r) { return r && r->state == ShotState::Fired; }

QStringList topIds(const std::vector<RankedCause> &rs, int n)
{
    QStringList out;
    for (int i = 0; i < int(rs.size()) && i < n; ++i) out << rs[size_t(i)].conditionId;
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QString     packADir, packBDir, outDir;
    bool        keepWork = false;
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--pack-a") && i + 1 < argc) { packADir = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--pack-b") && i + 1 < argc) { packBDir = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--out") && i + 1 < argc)    { outDir   = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--keep-work"))               { keepWork = true; continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "diag_rank_diff: unknown option %s\n", argv[i]);
            return 2;
        }
        dirs << a;
    }
    if (dirs.isEmpty() || outDir.isEmpty() || packADir.isEmpty() || packBDir.isEmpty()) {
        std::fprintf(stderr, "usage: diag_rank_diff --pack-a <dir> --pack-b <dir> --out <dir> [--keep-work] "
                             "<session-dir>...\n");
        return 2;
    }
    for (const PackFile &p : kPackFiles)
        g_startEnv[QLatin1String(p.env)] = qEnvironmentVariable(p.env);

    Pack A, B;
    const bool okA = resolvePack(QStringLiteral("A"), packADir, A);
    const bool okB = resolvePack(QStringLiteral("B"), packBDir, B);
    if (!okA || !okB) {
        std::fprintf(stderr, "diag_rank_diff: a pack file is missing (above)\n");
        return 2;
    }
    for (const Pack *p : { &A, &B }) {
        applyPack(*p);
        const auto prov = makeCharacteristicPackProvider();
        int errors = 0;
        for (const ValidationIssue &i : prov->report().issues) if (i.severity == IssueSeverity::Error) ++errors;
        std::printf("pack %s: %d conditions, %d validation errors\n",
                    qPrintable(p->label), int(prov->pack().conditions.size()), errors);
        if (prov->pack().conditions.empty()) {
            std::fprintf(stderr, "diag_rank_diff: pack %s has no conditions\n", qPrintable(p->label));
            return 2;
        }
    }

    QDir().mkpath(outDir);
    const QString workRoot = QDir(outDir).absoluteFilePath(QStringLiteral(".work"));
    const QDir od(outDir);

    Csv roots(od.filePath(QStringLiteral("roots_by_session.csv")),
              { "session", "rank", "root_a", "score_a", "root_b", "score_b" });
    Csv changes(od.filePath(QStringLiteral("firing_changes.csv")),
                { "session", "swing", "condition", "state_a", "state_b", "pFire_a", "pFire_b",
                  "driving_measure_b", "driving_value_b" });
    QStringList seqHead{ "session", "swing" };
    for (const SequenceCheckDef &d : sequenceCheckDefs())
        seqHead << QLatin1String(d.facet) << QLatin1String(d.facet) + QStringLiteral("_pFire");
    seqHead << "n" << "m" << "expected_passed" << "clubhead_speed" << "smash";
    Csv seq(od.filePath(QStringLiteral("sequence_checks.csv")), seqHead);

    struct Count { int firedA = 0, firedB = 0, assA = 0, assB = 0; };
    std::map<QString, Count> counts;
    int swingsA = 0, swingsB = 0, nChanges = 0, copyFailures = 0;
    double msA = 0.0, msB = 0.0;
    QStringList summary;

    for (const QString &raw : dirs) {
        const QString dir = QDir::cleanPath(QDir(raw).absolutePath());
        const QString name = QFileInfo(dir).fileName();
        if (!QDir(dir).exists()) {
            std::printf("%s: SKIP, no such directory\n", qPrintable(name));
            continue;
        }
        std::map<int, QString> swingNames;
        QByteArray intentDoc;
        const QString work = mirrorSession(dir, workRoot, swingNames, intentDoc, &copyFailures);

        const RunOut ra = runPack(A, work, intentDoc);
        const RunOut rb = runPack(B, work, intentDoc);
        swingsA += int(ra.shots.size());
        swingsB += int(rb.shots.size());
        msA += ra.ms;
        msB += rb.ms;

        // 1. Roots.
        for (int k = 0; k < 10; ++k) {
            const RankedCause *a = k < int(ra.roots.size()) ? &ra.roots[size_t(k)] : nullptr;
            const RankedCause *b = k < int(rb.roots.size()) ? &rb.roots[size_t(k)] : nullptr;
            if (!a && !b) break;
            roots.row({ name, QString::number(k + 1),
                        a ? a->conditionId : QString(), a ? num(a->score) : QString(),
                        b ? b->conditionId : QString(), b ? num(b->score) : QString() });
        }

        // 2 + 3. Per swing × condition.
        std::map<int, const ShotRecord *> byA, byB;
        for (const ShotRecord &s : ra.shots) byA[s.shotId] = &s;
        for (const ShotRecord &s : rb.shots) byB[s.shotId] = &s;
        std::set<int> ids;
        for (const auto &kv : byA) ids.insert(kv.first);
        for (const auto &kv : byB) ids.insert(kv.first);
        for (int id : ids) {
            const ShotRecord *sa = byA.count(id) ? byA[id] : nullptr;
            const ShotRecord *sb = byB.count(id) ? byB[id] : nullptr;
            const QString swing = swingNames.count(id) ? swingNames[id] : QString::number(id);
            std::set<QString> conds;
            if (sa) for (const ConditionRow &r : sa->rows) conds.insert(r.conditionId);
            if (sb) for (const ConditionRow &r : sb->rows) conds.insert(r.conditionId);
            for (const QString &c : conds) {
                const ConditionRow *a = sa ? rowFor(*sa, c) : nullptr;
                const ConditionRow *b = sb ? rowFor(*sb, c) : nullptr;
                Count &n = counts[c];
                if (fired(a)) ++n.firedA;
                if (fired(b)) ++n.firedB;
                if (a && a->state != ShotState::NotAssessable) ++n.assA;
                if (b && b->state != ShotState::NotAssessable) ++n.assB;
                if (fired(a) == fired(b)) continue;
                ++nChanges;
                const bool ev = b && !b->drivingMeasureId.isEmpty();
                changes.row({ name, swing, c, stateName(a), stateName(b),
                              a ? pf(a->pFire) : QString(), b ? pf(b->pFire) : QString(),
                              ev ? b->drivingMeasureId : QString(), ev ? num(b->value, 4) : QString() });
            }
        }

        // 4. Pack B's sequence checks, with the outcome readings §10.6 validates the count against.
        {
            applyPack(B);
            const auto provB = makeCharacteristicPackProvider();
            for (const ShotRecord &s : rb.shots) {
                const QString swing = swingNames.count(s.shotId) ? swingNames[s.shotId] : QString::number(s.shotId);
                const SequenceChecks sc = sequenceChecksOf(s);
                QStringList row{ name, swing };
                for (size_t i = 0; i < sequenceCheckDefs().size(); ++i) {
                    if (sc.recorded && i < sc.checks.size()) {
                        const SequenceCheck &c = sc.checks[i];
                        row << sequenceCheckMark(c.state)
                            << (c.state == SequenceCheckState::NotAssessable ? QString() : pf(c.pFire));
                    } else {
                        row << QStringLiteral("—") << QString();
                    }
                }
                double smash = std::numeric_limits<double>::quiet_NaN();
                {
                    // The mirror's sidecar is fresh after the run, so this is the cheap read.
                    LiveMeasureSource src(QDir(work).filePath(swing), provB->pack());
                    if (const auto v = src.value(QStringLiteral("m_smashFactor")); v && std::isfinite(v->value))
                        smash = v->value;
                }
                row << QString::number(sc.passed) << QString::number(sc.assessable)
                    << num(sc.expectedPassed) << num(s.clubheadSpeed, 1) << num(smash, 3);
                seq.row(row);
            }
        }

        const QStringList ta = topIds(ra.roots, 3), tb = topIds(rb.roots, 3);
        summary << QStringLiteral("%1 (%2 swings): %3  ->  %4%5")
                       .arg(name).arg(rb.shots.size())
                       .arg(ta.isEmpty() ? QStringLiteral("(no roots)") : ta.join(QStringLiteral(", ")),
                            tb.isEmpty() ? QStringLiteral("(no roots)") : tb.join(QStringLiteral(", ")),
                            ta == tb ? QStringLiteral("  [same]") : QString());

        if (!keepWork) QDir(work).removeRecursively();
    }
    if (!keepWork) QDir(workRoot).removeRecursively();

    Csv fc(od.filePath(QStringLiteral("firing_counts.csv")),
           { "condition", "fired_a", "fired_b", "assessable_a", "assessable_b" });
    QStringList moved;
    for (const auto &[c, n] : counts) {
        fc.row({ c, QString::number(n.firedA), QString::number(n.firedB),
                 QString::number(n.assA), QString::number(n.assB) });
        if (n.firedA != n.firedB)
            moved << QStringLiteral("  %1: %2 -> %3 fired (assessable %4 -> %5)")
                         .arg(c).arg(n.firedA).arg(n.firedB).arg(n.assA).arg(n.assB);
    }

    std::printf("\nswings read: %d under A, %d under B, over %d sessions%s\n", swingsA, swingsB,
                int(dirs.size()), swingsA == swingsB ? "" : "  — COUNTS DIFFER");
    if (swingsB > 0)
        std::printf("time per swing: A %.0f ms, B %.0f ms (mirror reads, detection, reduction, explain)\n",
                    msA / std::max(1, swingsA), msB / std::max(1, swingsB));
    if (copyFailures > 0)
        std::printf("WARNING: %d swing files could not be mirrored with their mtime\n", copyFailures);
    std::printf("\ntop-3 roots per session, A -> B:\n");
    for (const QString &s : summary) std::printf("  %s\n", qPrintable(s));
    std::printf("\nconditions whose firing count changed (%d swing x condition flips):\n", nChanges);
    if (moved.isEmpty()) std::printf("  none\n");
    for (const QString &s : moved) std::printf("%s\n", qPrintable(s));
    std::printf("\nwrote %s/{roots_by_session,firing_counts,firing_changes,sequence_checks}.csv\n",
                qPrintable(QDir(outDir).absolutePath()));
    return 0;
}
