// parity_json_test — the parity dumps' canonical writer (parity_json.h), pinned on hand-built values:
//   1. keys sorted at every depth, whatever order they were inserted in (a QVariantHash too);
//   2. numbers at full precision ("%.17g"), integers as integers, NaN and infinities as null;
//   3. the run taken out: run keys (writtenAtMs, frozenAt, fingerprint …) dropped at any depth, and a
//      string holding a run folder gets its token, the longest folder first;
//   4. arrays keep the producer's order (a reordering is a difference worth seeing);
//   5. the same value written twice is byte-identical, and a JSON file read back writes the same bytes;
//   6. numbers are the C locale's whatever the process's is (a comma locale wrote "0,1…"), and match
//      printf's %.17g in the C locale, exponents included.
// Header-only, Qt Core only; no library, no fixtures.

#include "parity_json.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <limits>

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("%s  %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 1 + 2 + 4
    QVariantHash h;
    h.insert(QStringLiteral("zeta"), 1);
    h.insert(QStringLiteral("alpha"), 2);
    const QVariantMap v{
        { QStringLiteral("b"),    0.1 },
        { QStringLiteral("a"),    QVariantList{ 3, 1, 2 } },
        { QStringLiteral("nan"),  std::numeric_limits<double>::quiet_NaN() },
        { QStringLiteral("inf"),  std::numeric_limits<double>::infinity() },
        { QStringLiteral("int"),  qint64(42) },
        { QStringLiteral("flt"),  double(0.3f) },
        { QStringLiteral("hash"), h },
        { QStringLiteral("text"), QStringLiteral("a \"quote\"\n") },
    };
    const QByteArray out = parity::toCanonicalJson(v);
    check(out.indexOf("\"a\"") < out.indexOf("\"b\"") && out.indexOf("\"b\"") < out.indexOf("\"flt\"")
              && out.indexOf("\"alpha\"") < out.indexOf("\"zeta\""),
          "keys sorted at every depth, a hash's too");
    check(out.contains("\"b\": 0.10000000000000001"), "a double at %.17g");
    check(out.contains("\"flt\": 0.30000001192092896"), "a float widened, at %.17g");
    check(out.contains("\"int\": 42"), "an integer as an integer");
    check(out.contains("\"nan\": null") && out.contains("\"inf\": null"), "non-finite as null");
    check(out.contains("\"a\": [\n    3,\n    1,\n    2\n  ]"), "an array in the producer's order");
    check(out.contains("\"text\": \"a \\\"quote\\\"\\n\""), "a string escaped");

    // 3
    parity::PathTokens tokens;
    parity::addPathToken(tokens, QStringLiteral("/scratch/run/Jess"), QStringLiteral("<athlete>"));
    parity::addPathToken(tokens, QStringLiteral("/scratch/run/Jess/2026-10-20_s1"), QStringLiteral("<session>"));
    const QVariantMap run{
        { QStringLiteral("derivedFrom"), QVariantMap{ { QStringLiteral("writtenAtMs"), 1.7e12 },
                                                      { QStringLiteral("fingerprint"), QStringLiteral("abc") },
                                                      { QStringLiteral("contentStamp"), QStringLiteral("cs") } } },
        { QStringLiteral("frozenAt"), QStringLiteral("2026-11-14T10:00:00Z") },
        { QStringLiteral("dir"),      QStringLiteral("/scratch/run/Jess/2026-10-20_s1/swing_0001") },
        { QStringLiteral("up"),       QStringLiteral("/scratch/run/Jess/tendencies.json") },
        { QStringLiteral("list"),     QStringList{ QStringLiteral("/scratch/run/Jess/x") } },
    };
    const QByteArray scrubbed = parity::toCanonicalJson(parity::scrub(run, tokens));
    check(!scrubbed.contains("writtenAtMs") && !scrubbed.contains("fingerprint") && !scrubbed.contains("frozenAt")
              && scrubbed.contains("\"contentStamp\": \"cs\""),
          "run keys dropped at any depth, the rest kept");
    check(scrubbed.contains("\"<session>/swing_0001\"") && scrubbed.contains("\"<athlete>/tendencies.json\"")
              && scrubbed.contains("\"<athlete>/x\"") && !scrubbed.contains("/scratch/"),
          "run folders tokenised, the longest first, in lists too");

    // 5
    check(parity::toCanonicalJson(v) == out, "the same value writes the same bytes");
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("x.json"));
    QString why;
    check(parity::writeFile(path, parity::toCanonicalJson(parity::scrub(run, tokens)), &why), "the file is written");
    check(parity::toCanonicalJson(parity::readJsonFile(path)) == scrubbed, "a dump read back writes the same bytes");
    check(!parity::readJsonFile(tmp.filePath(QStringLiteral("none.json")), &why).isValid() && why == QStringLiteral("absent"),
          "an absent file reads as null, and says so");

    // 6. printf's own %.17g in the C locale is the reference; the writer must agree under any locale.
    const double samples[] = { 0.1, 1e20, 1e-7, 123456789.123456789, -2.5e-300, 1.0 / 3.0, 41.921593, 1e16, 3.0 };
    const auto reference = [](double d) {
        std::setlocale(LC_NUMERIC, "C");
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", d);
        return QByteArray(buf);
    };
    const auto written = [](double d) { QByteArray b; parity::appendNumber(b, d); return b; };
    bool same = true;
    for (double d : samples) same = same && written(d) == reference(d);
    check(same, "numbers are printf's %.17g in the C locale, exponents too");
    const char *commaLocales[] = { "de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8" };
    const char *comma = nullptr;
    for (const char *l : commaLocales)
        if (std::setlocale(LC_NUMERIC, l)) { comma = l; break; }
    if (!comma) {
        std::printf("SKIP  no comma locale installed (de_DE / fr_FR) — the locale case is not run\n");
    } else {
        QList<QByteArray> refs;
        for (double d : samples) refs << reference(d);       // in the C locale
        std::setlocale(LC_NUMERIC, comma);
        bool sameUnder = true;
        char probe[16];
        std::snprintf(probe, sizeof probe, "%.1f", 0.5);    // proves the locale really writes a comma
        for (qsizetype i = 0; i < refs.size(); ++i) sameUnder = sameUnder && written(samples[i]) == refs[i];
        const QByteArray doc = parity::toCanonicalJson(QVariantMap{ { QStringLiteral("b"), 0.1 } });
        check(QByteArray(probe).contains(',') && doc.contains("0.10000000000000001") && !doc.contains("0,1") && sameUnder,
              "under a comma locale numbers are still written with a point");
        std::setlocale(LC_NUMERIC, "C");
    }

    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
