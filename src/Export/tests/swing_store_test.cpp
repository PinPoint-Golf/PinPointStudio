// swing_store_test — the swing-document store (src/Export/swing_store.h) and its Qt adapter.
//
// What is guarded here:
//   · the adapter round trip keeps VALUES and TYPES: an integer reads back as an integer and a double
//     as a double, exactly as Qt held them — the distinction every reader keys on (toInt vs toDouble);
//   · both formats read; a rewrite of a JSON-era swing leaves only swing.ppsw (migrate on rewrite);
//   · the verified conversion: converts, is idempotent, and on ANY failure leaves the JSON exactly
//     as it was and no .ppsw behind;
//   · the libppswing corpus fixtures (a face-on + IMU swing and a two-camera DTL swing) survive
//     Qt → .ppsw → Qt unchanged.
//
// Timing (the Phase 2 encode-time gate): set PP_STORE_TIMING_DIR to a real swing directory and the
// test also times load() and save() on a copy of its document. Informational, never a failure.

#include "../swing_store.h"
#include "../ppsw_qt.h"

#include <ppswing/ppswing.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariant>

#include <cmath>
#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

// Deep equality that ALSO requires matching number kinds (integer vs double) and the sign of zero.
static bool sameTyped(const QJsonValue &a, const QJsonValue &b, QString *where, const QString &path = {})
{
    if (a.type() != b.type()) { *where = path + " (type)"; return false; }
    switch (a.type()) {
        case QJsonValue::Double: {
            const bool ai = a.toVariant().typeId() == QMetaType::LongLong;
            const bool bi = b.toVariant().typeId() == QMetaType::LongLong;
            if (ai != bi) { *where = path + " (int vs double)"; return false; }
            if (ai) {
                if (a.toInteger() != b.toInteger()) { *where = path; return false; }
            } else {
                const double x = a.toDouble(), y = b.toDouble();
                if (!(x == y) || std::signbit(x) != std::signbit(y)) { *where = path; return false; }
            }
            return true;
        }
        case QJsonValue::Array: {
            const QJsonArray x = a.toArray(), y = b.toArray();
            if (x.size() != y.size()) { *where = path + " (length)"; return false; }
            for (qsizetype i = 0; i < x.size(); ++i)
                if (!sameTyped(x.at(i), y.at(i), where, path + "/" + QString::number(i))) return false;
            return true;
        }
        case QJsonValue::Object: {
            const QJsonObject x = a.toObject(), y = b.toObject();
            if (x.keys() != y.keys()) { *where = path + " (keys)"; return false; }
            for (auto it = x.begin(); it != x.end(); ++it)
                if (!sameTyped(it.value(), y.value(it.key()), where, path + "/" + it.key())) return false;
            return true;
        }
        default:
            if (a != b) { *where = path; return false; }
            return true;
    }
}

static bool writeText(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

static QByteArray readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A tree with every shape the writer treats specially: integer / double / integral-double / -0.0
// scalars, a rectangular numeric array (→ ndarray, delta-coded ints), one with nulls, an array of
// objects with a missing key (→ table with a presence bitmap), nesting, and non-ASCII strings.
static QJsonObject awkwardTree()
{
    QJsonArray tUs, kp, withNull, frames;
    for (int i = 0; i < 300; ++i) {
        tUs.append(qint64(1'000'000) + i * 6667);
        kp.append(QJsonArray{ 101.25 + i, 202.5 - i, 0.875 });
        withNull.append(i % 7 == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(i * 0.1));
        QJsonObject fr{ { "t_us", qint64(i) * 6667 }, { "found", i % 3 != 0 } };
        if (i % 5) fr.insert("x", 12.5 + i);   // absent on every fifth row
        frames.append(fr);
    }
    return QJsonObject{
        { "schema",   "pinpoint.swing/2" },
        { "int",      37 },
        { "double",   0.1 },
        { "integral", 37.0 },                 // whole: Qt holds it as an integer (see the check below)
        { "negzero",  -0.0 },
        { "big",      qint64(636'000'000'000LL) },
        { "text",     QString::fromUtf8("Mark Liversedge — 7 iron · ✓") },
        { "empty",    QJsonObject{} },
        { "nothing",  QJsonValue(QJsonValue::Null) },
        { "streams",  QJsonArray{ QJsonObject{ { "alias", "Face-On" }, { "frames", QJsonObject{ { "t_us", tUs } } } } } },
        { "analysis", QJsonObject{ { "kp", kp }, { "valid", withNull }, { "frames", frames } } },
    };
}

// A swing-shaped document for loadAnalysis(): ~30 metric curves (one /analysis/metrics chunk), a
// pose track and a club track big enough to be chunks of their own, a small inline `ball` track,
// and the analysis block's small fields. `bigInline` adds two inline blocks that are each under
// splitBytes but together push `analysis` itself over it, so the writer moves the whole block to
// an "/analysis" chunk — the shape 2 of the 50 7 Oct swings have.
static QJsonObject analysisDoc(bool bigInline)
{
    const qint64 t0 = 97982181793;
    QJsonArray metrics;
    for (int k = 0; k < 30; ++k) {
        QJsonArray ts, vs, valid;
        for (int i = 0; i < 400; ++i) {
            ts.append(qint64(t0 + 4000 * i));
            vs.append(0.5 + 0.001 * i + k);
            valid.append(i % 7 ? 1 : 0);
        }
        metrics.append(QJsonObject{
            { "key", QStringLiteral("metric%1").arg(k) }, { "label", "A metric" }, { "unit", "°" },
            { "t_us", ts }, { "value", vs }, { "valid", valid }, { "sigma", 1.25 }, { "sigmaKind", 2 },
            { "phaseSamples", QJsonArray{ QJsonObject{ { "phase", 5 }, { "t_us", qint64(t0 + 120000) },
                                                       { "value", 3.5 }, { "band", "" },
                                                       { "sigma", 0.5 }, { "sigmaKind", 2 } } } } });
    }
    QJsonArray frames, clubSamples;
    for (int i = 0; i < 4000; ++i) {
        frames.append(QJsonObject{ { "t_us", qint64(t0 + 1000 * i) },
                                   { "kp", QJsonArray{ 0.1 + i, 0.2 + i, 0.3 + i, 0.4 + i } } });
        clubSamples.append(QJsonObject{ { "t_us", qint64(t0 + 1000 * i) }, { "theta", 0.01 * i } });
    }
    QJsonObject analysis{
        { "metrics", metrics },
        { "phases", QJsonArray{ QJsonObject{ { "phase", 0 }, { "t_us", qint64(t0 + 50000) }, { "conf", 1.0 } },
                                QJsonObject{ { "phase", 5 }, { "t_us", qint64(t0 + 120000) }, { "conf", 0.75 } } } },
        { "kinematicSequence", QJsonObject{ { "impactUs", qint64(120000) }, { "verdict", "other" },
                                            { "nodes", QJsonArray{ QJsonObject{ { "segment", "pelvis" },
                                                                                { "tPeakUs", qint64(125000) } } } } } },
        { "pose2d", QJsonObject{ { "camera", 2 }, { "frames", frames } } },
        { "club", QJsonObject{ { "samples", clubSamples } } },
        { "ball", QJsonObject{ { "samples", QJsonArray{ QJsonObject{ { "t_us", qint64(t0) }, { "x", 0.5 } } } } } },
        { "score", 80 }, { "tier", 2 } };
    if (bigInline) {
        QJsonArray a, b;
        for (int i = 0; i < 5000; ++i) { a.append(0.25 + 0.001 * i); b.append(0.75 + 0.001 * i); }
        analysis.insert("segmentation", QJsonObject{ { "x", a } });
        analysis.insert("timings", QJsonObject{ { "y", b } });
    }
    return QJsonObject{
        { "clock", QJsonObject{ { "t0_us", t0 }, { "unit", "us" } } },
        { "review", QJsonObject{ { "club", "7 IRON" }, { "rating", 0 } } },
        { "capture", QJsonObject{ { "club", QJsonObject{ { "name", "7 IRON" } } } } },
        { "analysis", analysis } };
}

// loadAnalysis(dir) against load(dir): every top-level block but `analysis` identical, every
// analysis field identical except the per-frame tracks, which must be absent.
static void checkAnalysisLoad(const QString &dir, const char *label)
{
    QString err, where;
    const QJsonObject full = SwingStore::load(dir, &err);
    const QJsonObject lite = SwingStore::loadAnalysis(dir, &err);
    const QStringList tracks{ "pose2d", "poseDtl", "skeleton3d", "club", "clubDtl", "club3d", "ball", "impact" };
    bool same = !full.isEmpty() && !lite.isEmpty() && full.keys() == lite.keys();
    for (const QString &k : full.keys())
        if (same && k != "analysis" && !sameTyped(full.value(k), lite.value(k), &where, "/" + k)) same = false;
    const QJsonObject fa = full.value("analysis").toObject(), la = lite.value("analysis").toObject();
    bool noTracks = true;
    for (const QString &k : tracks) noTracks = noTracks && !la.contains(k);
    for (const QString &k : fa.keys()) {
        if (tracks.contains(k)) continue;
        if (!la.contains(k)) { same = false; where = "/analysis/" + k + " (missing)"; break; }
        if (!sameTyped(fa.value(k), la.value(k), &where, "/analysis/" + k)) { same = false; break; }
    }
    std::printf("  %s:\n", label);
    check(same && la.value("metrics").toArray().size() == 30,
          "  every analysis field but the tracks equals the full load (metrics, phases, sequence, clock, club)");
    if (!where.isEmpty()) std::printf("    first difference: %s\n", where.toUtf8().constData());
    check(fa.contains("pose2d") && fa.contains("club") && fa.contains("ball") && noTracks,
          "  ...and the pose, club and ball tracks are absent (inline or chunked)");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::printf("no temp dir\n"); return 1; }

    std::printf("=== adapter: values and number kinds survive .ppsw ===\n");
    {
        const QString dir = tmp.filePath("awkward");
        QDir().mkpath(dir);
        const QJsonObject in = awkwardTree();
        QString err, where;
        check(SwingStore::save(dir, in, &err), "save");
        const QJsonObject back = SwingStore::load(dir, &err);
        check(sameTyped(in, back, &where), "round trip is typed-equal");
        if (!where.isEmpty()) std::printf("    first difference: %s\n", where.toUtf8().constData());
        check(back.value("int").toVariant().typeId() == QMetaType::LongLong, "an integer stays an integer");
        check(back.value("double").toVariant().typeId() == QMetaType::Double, "a double stays a double");
        // Qt 6 itself holds an integral double (37.0) and -0.0 as INTEGERS inside QJsonValue, and has
        // always written them "37" / "0" to swing.json. The typed round trip above proves the adapter
        // keeps exactly what Qt holds; these two record that Qt's model is the ceiling, so nobody
        // mistakes it for an adapter loss.
        check(in.value("integral").toVariant().typeId() == back.value("integral").toVariant().typeId(),
              "37.0: whatever kind Qt holds, it reads back as the same kind");
        check(back.value("negzero").toDouble() == 0.0, "-0.0: reads back as zero");
    }

    std::printf("\n=== both formats: JSON read, .ppsw preferred, rewrite migrates ===\n");
    {
        const QString dir = tmp.filePath("legacy");
        QDir().mkpath(dir);
        const QJsonObject doc = awkwardTree();
        check(!SwingStore::hasDocument(dir), "an empty dir has no document");
        check(writeText(SwingStore::jsonPath(dir), QJsonDocument(doc).toJson()), "legacy swing.json written");
        writeText(SwingStore::legacySummaryPath(dir), "{}");
        check(SwingStore::info(dir).format == SwingStore::Format::Json, "info: JSON");
        check(SwingStore::load(dir) == doc, "a JSON-era document loads");
        check(SwingStore::save(dir, doc), "rewrite");
        check(SwingStore::info(dir).format == SwingStore::Format::Ppsw, "info: .ppsw after the rewrite");
        check(!QFile::exists(SwingStore::jsonPath(dir)), "…swing.json retired");
        check(!QFile::exists(SwingStore::legacySummaryPath(dir)), "…and its sidecar");
        check(SwingStore::load(dir) == doc, "…and it reads back the same");

        // A pair (an interrupted conversion) reads the .ppsw.
        writeText(SwingStore::jsonPath(dir), R"({"schema":"stale"})");
        check(SwingStore::info(dir).format == SwingStore::Format::Ppsw, "a pair: .ppsw wins");
    }

    std::printf("\n=== conversion: verified, idempotent, and fail-closed ===\n");
    {
        const auto summaryFn = [](const QJsonObject &root, const QString &) {
            return QJsonObject{ { "schema", "test.summary/1" },
                                { "ordinal", root.value("int").toInt() } };
        };
        const QString dir = tmp.filePath("convert");
        QDir().mkpath(dir);
        const QByteArray text = QJsonDocument(awkwardTree()).toJson(QJsonDocument::Indented);
        writeText(SwingStore::jsonPath(dir), text);

        SwingStore::ConvertResult r = SwingStore::convertDir(dir, summaryFn, /*deleteJson=*/false);
        check(r.outcome == SwingStore::ConvertResult::Outcome::Converted, "converted (keep JSON)");
        check(QFile::exists(SwingStore::jsonPath(dir)) && QFile::exists(SwingStore::ppswPath(dir)),
              "…both files present with deleteJson off");
        check(r.jsonBytes == text.size() && r.ppswBytes > 0 && r.ppswBytes < r.jsonBytes,
              "…sizes reported, and the .ppsw is smaller");

        r = SwingStore::convertDir(dir, summaryFn, /*deleteJson=*/true);
        check(r.outcome == SwingStore::ConvertResult::Outcome::Converted, "re-converted from the JSON");
        check(!QFile::exists(SwingStore::jsonPath(dir)), "…and the JSON deleted once proven");
        QJsonObject back = SwingStore::load(dir);
        check(back.value("summary").toObject().value("ordinal").toInt() == 37, "summary block added");
        back.remove("summary");
        QString where;
        check(sameTyped(QJsonDocument::fromJson(text).object(), back, &where), "…and nothing else changed");
        check(SwingStore::loadSummaryBlock(dir).value("schema").toString() == "test.summary/1",
              "the summary reads from the root chunk alone");

        r = SwingStore::convertDir(dir, summaryFn, true);
        check(r.outcome == SwingStore::ConvertResult::Outcome::AlreadyPpsw, "idempotent: already .ppsw");
        const QString none = tmp.filePath("none");
        QDir().mkpath(none);
        check(SwingStore::convertDir(none, summaryFn, true).outcome
                  == SwingStore::ConvertResult::Outcome::NoDocument, "no document → nothing done");

        // Fail closed: a document that already carries a top-level "summary" is refused…
        const QString clash = tmp.filePath("clash");
        QDir().mkpath(clash);
        const QByteArray clashText = R"({"summary":{"mine":1},"int":3})";
        writeText(SwingStore::jsonPath(clash), clashText);
        r = SwingStore::convertDir(clash, summaryFn, true);
        check(r.outcome == SwingStore::ConvertResult::Outcome::Failed && !r.error.isEmpty(),
              "a document that already has a summary is refused");
        check(readText(SwingStore::jsonPath(clash)) == clashText && !QFile::exists(SwingStore::ppswPath(clash)),
              "…its JSON untouched, no .ppsw left behind");

        // …and so is malformed JSON, including a stale .ppsw from an interrupted attempt.
        const QString bad = tmp.filePath("bad");
        QDir().mkpath(bad);
        writeText(SwingStore::jsonPath(bad), "{ this is not json");
        writeText(SwingStore::ppswPath(bad), "stale");
        r = SwingStore::convertDir(bad, summaryFn, true);
        check(r.outcome == SwingStore::ConvertResult::Outcome::Failed, "malformed JSON fails");
        check(readText(SwingStore::jsonPath(bad)) == "{ this is not json", "…its JSON untouched");
        check(!QFile::exists(SwingStore::ppswPath(bad)), "…and the stale .ppsw removed");

        const QStringList dirs = SwingStore::swingDirsUnder(tmp.path());
        check(dirs.contains(dir) && dirs.contains(clash) && !dirs.contains(none),
              "swingDirsUnder finds document dirs only");
        QDir().mkpath(tmp.filePath(".pinpoint-trash/x"));
        writeText(tmp.filePath(".pinpoint-trash/x/swing.json"), "{}");
        check(!SwingStore::swingDirsUnder(tmp.path()).contains(tmp.filePath(".pinpoint-trash/x")),
              "…and never enters a dot-directory");
    }

    std::printf("\n=== loadAnalysis: the analysis without the per-frame tracks ===\n");
    {
        const auto chunks = [](const QString &dir) {
            QStringList names;
            const ppsw::Reader r = ppsw::Reader::open(SwingStore::ppswPath(dir).toStdString());
            for (const ppsw::TocEntry &e : r.toc())
                names << QString::fromStdString(e.name);
            return names;
        };
        const QString normal = tmp.filePath("analysis_normal");
        QDir().mkpath(normal);
        check(SwingStore::save(normal, analysisDoc(false)), "a swing-shaped document saved");
        const QStringList c1 = chunks(normal);
        check(c1.contains("/analysis/metrics") && !c1.contains("/analysis")
                  && c1.filter("/analysis/pose2d").size() > 0,
              "...as the app writes them: metrics and pose in chunks of their own");
        checkAnalysisLoad(normal, "root + /analysis/metrics");

        const QString whole = tmp.filePath("analysis_whole");
        QDir().mkpath(whole);
        check(SwingStore::save(whole, analysisDoc(true)), "a document whose analysis block outgrows splitBytes");
        const QStringList c2 = chunks(whole);
        check(c2.contains("/analysis") && c2.contains("/analysis/metrics"),
              "...is written with `analysis` as one chunk (\"/analysis\"), metrics inside it");
        checkAnalysisLoad(whole, "\"/analysis\" chunk, descended");

        const QString legacy = tmp.filePath("analysis_legacy");
        QDir().mkpath(legacy);
        writeText(SwingStore::jsonPath(legacy), QJsonDocument(analysisDoc(true)).toJson(QJsonDocument::Compact));
        check(SwingStore::info(legacy).format == SwingStore::Format::Json, "a legacy swing.json");
        checkAnalysisLoad(legacy, "swing.json, parsed whole");

        const QString none = tmp.filePath("analysis_none");
        QDir().mkpath(none);
        QString err;
        check(SwingStore::loadAnalysis(none, &err).isEmpty() && !err.isEmpty(), "no document: empty, with a reason");
    }

#ifdef PP_LIBPPSWING_FIXTURES
    std::printf("\n=== the libppswing corpus fixtures: Qt → .ppsw → Qt ===\n");
    for (const char *name : { "swing_imu_faceon.json", "swing_dtl.json" }) {
        const QByteArray text = readText(QStringLiteral(PP_LIBPPSWING_FIXTURES "/") + QLatin1String(name));
        const QJsonObject in = QJsonDocument::fromJson(text).object();
        const QString dir = tmp.filePath(QLatin1String(name));
        QDir().mkpath(dir);
        QString where;
        const bool ok = !in.isEmpty() && SwingStore::save(dir, in)
                        && sameTyped(in, SwingStore::load(dir), &where);
        check(ok, name);
        if (!where.isEmpty()) std::printf("    first difference: %s\n", where.toUtf8().constData());
    }
#endif

    if (const QByteArray timing = qgetenv("PP_STORE_TIMING_DIR"); !timing.isEmpty()) {
        std::printf("\n=== timing: %s ===\n", timing.constData());
        const QString src = QString::fromLocal8Bit(timing);
        QElapsedTimer t;
        t.start();
        const QJsonObject doc = SwingStore::load(src);
        const qint64 loadMs = t.elapsed();
        const QString dir = tmp.filePath("timing");
        QDir().mkpath(dir);
        t.restart();
        const ppsw::Value tree = ppswqt::fromQt(doc);   // the adapter half of save(), alone
        const qint64 adaptMs = t.elapsed();
        t.restart();
        const bool saved = SwingStore::save(dir, doc);
        const qint64 saveMs = t.elapsed();
        std::printf("  adapter (Qt → ppsw) %lld ms of the save below\n", (long long)adaptMs);
        std::printf("  %s: load %lld ms, save %lld ms (%lld bytes)\n",
                    SwingStore::info(src).format == SwingStore::Format::Ppsw ? ".ppsw" : "json",
                    (long long)loadMs, (long long)saveMs, (long long)QFileInfo(SwingStore::ppswPath(dir)).size());
        t.restart();
        const QJsonObject again = SwingStore::load(dir);
        std::printf("  .ppsw: load %lld ms\n", (long long)t.elapsed());
        t.restart();
        const QJsonObject blk = SwingStore::loadSummaryBlock(dir);
        std::printf("  .ppsw: summary block alone %lld ms\n", (long long)t.elapsed());
        check(saved && !doc.isEmpty() && again == doc, "timing document loaded, saved and read back equal");
    }

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
