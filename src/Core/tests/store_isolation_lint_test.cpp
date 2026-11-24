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

// store_isolation_lint_test (#187): no test suite may forget pinpoint::test::IsolatedStores.
//
// THE RULE. A suite whose target compiles a source that calls ppSettings() (the settings file, which also holds the
// phone pairings) must have a source that makes an IsolatedStores (src/Core/tests/isolated_stores.h). Otherwise it
// reads, and may write, the machine's real settings: on Windows nothing else redirects them (#187's 46 suites).
//
// HOW IT KNOWS WHAT A SUITE LINKS. The tests umbrella (tests/CMakeLists.txt) writes a manifest at the end of every
// configure, after the last target_sources: each executable target, its sources (absolute) and every test name. So
// it works for any generator (Ninja on the Mac, NMake JOM on the studio PC) and needs no Python. A target is a suite
// when a test has its name, or (the QML suite) when a test is named <target less "_test">.<file>. The sources are
// read at run time with comments and string and character literals blanked, so a ppSettings or IsolatedStores in a
// comment does not count.
//
// Why a lint and not a hash of the real store around the run: the Windows exclusions were for READS (a suite graded
// against the studio's own settings), which a hash cannot see; a hash also only says that something changed, after
// the fact, not which suite; and the studio PC's script already hashes the settings around its runs.
//
// KNOWN LIMITS: a header-only call (an inline ppSettings() in a header the target includes but does not list) is not
// seen; app_settings.h's are reached through app_settings.cpp, which every user of AppSettings compiles. A test that
// runs a TOOL through `cmake -P` (athlete_dump_ids_test, regrade_ledger_new_needs_ini_test) has no suite target, so
// it is not checked: the tools read the real settings by design. QStandardPaths users that never call ppSettings()
// (the pack readers' diagnosticsDataDir()) are not in the rule: they reach a folder named after the test binary,
// never the app's, and test mode is not required of them.
//
// NOT SEEN EITHER (review of 24 Nov 2026; #258): a ppSettings() call in a LINKED library (only each executable's own
// listed sources are scanned; no target_link_libraries is followed: clean today, unsound once Stage B Part 2 makes
// the rings libraries); an executable whose test has another name (the opt-in session_setup_ui_test); and the ORDER
// (it checks that an IsolatedStores appears, not that it comes first).
//
// Before the scan, the rule runs on fixtures, so a rule that silently stops matching fails here.
//
//   ctest --test-dir build/tests -R '^store_isolation_lint_test$' --output-on-failure

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

#ifndef PP_STORE_MANIFEST
#  error "PP_STORE_MANIFEST must name the manifest the tests umbrella writes"
#endif

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

// The code of a C++ source with comments and string / character literals blanked (newlines kept).
QString codeOf(const QString &text)
{
    QString out;
    out.reserve(text.size());
    enum { Code, Line, Block, Str, Chr } st = Code;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i), n = i + 1 < text.size() ? text.at(i + 1) : QChar();
        switch (st) {
        case Code:
            if (c == QLatin1Char('/') && n == QLatin1Char('/')) { st = Line; ++i; out += QLatin1String("  "); continue; }
            if (c == QLatin1Char('/') && n == QLatin1Char('*')) { st = Block; ++i; out += QLatin1String("  "); continue; }
            // A raw string, R"delim( ... )delim" (QML text in a suite, say): no escapes, any quote inside.
            if (c == QLatin1Char('R') && n == QLatin1Char('"')
                && (i == 0 || !(text.at(i - 1).isLetterOrNumber() || text.at(i - 1) == QLatin1Char('_'))
                    || text.at(i - 1) == QLatin1Char('8') || text.at(i - 1) == QLatin1Char('u')
                    || text.at(i - 1) == QLatin1Char('U') || text.at(i - 1) == QLatin1Char('L'))) {
                const int open = text.indexOf(QLatin1Char('('), i + 2);
                if (open > 0 && open - (i + 2) <= 16) {
                    const QString close = QLatin1Char(')') + text.mid(i + 2, open - (i + 2)) + QLatin1Char('"');
                    const int     end   = text.indexOf(close, open + 1);
                    const int     stop  = end < 0 ? int(text.size()) : end + int(close.size());
                    for (int k = i; k < stop; ++k) out += text.at(k) == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' ');
                    i = stop - 1;
                    continue;
                }
            }
            if (c == QLatin1Char('"')) { st = Str; out += QLatin1Char(' '); continue; }
            // A ' between digits is a digit separator (1'000), not a character literal.
            if (c == QLatin1Char('\'') && !(i > 0 && text.at(i - 1).isLetterOrNumber() && n.isLetterOrNumber())) {
                st = Chr; out += QLatin1Char(' '); continue;
            }
            out += c;
            break;
        case Line:
            if (c == QLatin1Char('\n')) { st = Code; out += c; } else out += QLatin1Char(' ');
            break;
        case Block:
            if (c == QLatin1Char('*') && n == QLatin1Char('/')) { st = Code; ++i; out += QLatin1String("  "); }
            else out += c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            break;
        case Str:
        case Chr:
            if (c == QLatin1Char('\\')) { ++i; out += QLatin1String("  "); continue; }
            if ((st == Str && c == QLatin1Char('"')) || (st == Chr && c == QLatin1Char('\''))) st = Code;
            out += c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            break;
        }
    }
    return out;
}

struct Manifest
{
    QMap<QString, QStringList> sources;   // target -> its sources, absolute
    QSet<QString>              tests;
};

Manifest readManifest(const QString &path, bool *ok)
{
    Manifest m;
    QFile    f(path);
    *ok = f.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!*ok) return m;
    QString     target;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.size() < 3 || line.at(1) != QLatin1Char(' ')) continue;
        const QString rest = line.mid(2);
        switch (line.at(0).toLatin1()) {
        case 'T': target = rest; m.sources[target]; break;
        case 'S': if (!target.isEmpty()) m.sources[target] << rest; break;
        case 'X': m.tests.insert(rest); break;
        default: break;
        }
    }
    return m;
}

struct Finding
{
    QString     target;
    QStringList reachedThrough;   // the sources that call ppSettings()
};

struct Result
{
    int            suites = 0, reaching = 0, isolated = 0;
    QList<Finding> missing;
};

class Scanner
{
public:
    bool callsPpSettings(const QString &path) { return code(path).contains(ppSettingsRe()); }
    bool makesIsolatedStores(const QString &path) { return code(path).contains(isolatedRe()); }

    Result scan(const Manifest &m)
    {
        Result r;
        for (auto it = m.sources.cbegin(); it != m.sources.cend(); ++it) {
            if (!isSuite(it.key(), m.tests)) continue;
            ++r.suites;
            QStringList through;
            bool        isolated = false;
            for (const QString &s : it.value()) {
                if (!isCxx(s)) continue;
                if (!s.endsWith(QLatin1String("/pp_settings.h")) && callsPpSettings(s)) through << s;
                if (makesIsolatedStores(s)) isolated = true;
            }
            if (through.isEmpty()) continue;
            ++r.reaching;
            if (isolated) ++r.isolated;
            else r.missing.push_back({ it.key(), through });
        }
        return r;
    }

private:
    static bool isSuite(const QString &target, const QSet<QString> &tests)
    {
        if (tests.contains(target)) return true;
        if (!target.endsWith(QLatin1String("_test"))) return false;
        const QString stem = target.left(target.size() - 5) + QLatin1Char('.');
        for (const QString &t : tests)
            if (t.startsWith(stem)) return true;
        return false;
    }
    static bool isCxx(const QString &p)
    {
        static const QStringList ext{ QStringLiteral(".cpp"), QStringLiteral(".cc"), QStringLiteral(".cxx"),
                                      QStringLiteral(".mm"),  QStringLiteral(".h"),  QStringLiteral(".hpp") };
        for (const QString &e : ext)
            if (p.endsWith(e)) return true;
        return false;
    }
    static const QRegularExpression &ppSettingsRe()
    {
        static const QRegularExpression re(QStringLiteral("\\bppSettings\\s*\\("));
        return re;
    }
    static const QRegularExpression &isolatedRe()
    {
        static const QRegularExpression re(QStringLiteral("\\bIsolatedStores\\b"));
        return re;
    }
    const QString &code(const QString &path)
    {
        auto it = m_code.find(path);
        if (it == m_code.end()) {
            QFile f(path);
            it = m_code.insert(path, f.open(QIODevice::ReadOnly) ? codeOf(QString::fromUtf8(f.readAll())) : QString());
        }
        return it.value();
    }
    QHash<QString, QString> m_code;
};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

void fixtures()
{
    std::printf("the rule on fixtures\n");
    QTemporaryDir tmp;
    check(tmp.isValid(), "F0 a temporary directory for the fixtures");
    const QString d = tmp.path();
    writeFile(d + QStringLiteral("/store.cpp"), "QVariant f() { return ppSettings().value(\"k\"); }\n");
    writeFile(d + QStringLiteral("/quiet.cpp"),
              "// ppSettings() in a comment\n/* ppSettings ( */ const char *s = \"ppSettings()\"; char c = '(';\n"
              "int n = 1'000;\n");
    writeFile(d + QStringLiteral("/iso_test.cpp"),
              "static const char *q = R\"QML(Text { text: \"it's \\\" ppSettings() \" }\n)QML\";\n"
              "int main() { pinpoint::test::IsolatedStores stores; }\n");
    writeFile(d + QStringLiteral("/forgot_test.cpp"), "// IsolatedStores would go here\nint main() { return 0; }\n");
    writeFile(d + QStringLiteral("/pp_settings.h"), "inline QSettings ppSettings() { return {}; }\n");

    Scanner sc;
    check(sc.callsPpSettings(d + QStringLiteral("/store.cpp")), "F1 a real ppSettings() call is seen");
    check(!sc.callsPpSettings(d + QStringLiteral("/quiet.cpp")),
          "F2 ppSettings in a comment, a string or after a character literal is not");
    check(sc.makesIsolatedStores(d + QStringLiteral("/iso_test.cpp")) && !sc.callsPpSettings(d + QStringLiteral("/iso_test.cpp")),
          "F3 an IsolatedStores after a raw string holding quotes, an apostrophe and a ppSettings() is seen, and that one is not");
    check(!sc.makesIsolatedStores(d + QStringLiteral("/forgot_test.cpp")), "F4 one in a comment is not");

    Manifest m;
    m.sources[QStringLiteral("good_test")]   = { d + QStringLiteral("/iso_test.cpp"), d + QStringLiteral("/store.cpp") };
    m.sources[QStringLiteral("bad_test")]    = { d + QStringLiteral("/forgot_test.cpp"), d + QStringLiteral("/store.cpp") };
    m.sources[QStringLiteral("calm_test")]   = { d + QStringLiteral("/forgot_test.cpp"), d + QStringLiteral("/quiet.cpp"),
                                                 d + QStringLiteral("/pp_settings.h") };
    m.sources[QStringLiteral("a_tool")]      = { d + QStringLiteral("/store.cpp") };
    m.sources[QStringLiteral("qmlish_test")] = { d + QStringLiteral("/forgot_test.cpp"), d + QStringLiteral("/store.cpp") };
    m.tests = { QStringLiteral("good_test"), QStringLiteral("bad_test"), QStringLiteral("calm_test"),
                QStringLiteral("qmlish.tst_one") };
    const Result r = sc.scan(m);
    QStringList missing;
    for (const Finding &f : r.missing) missing << f.target;
    missing.sort();
    check(r.suites == 4, QStringLiteral("F5 four suites: a tool is not one, a <stem>.<file> test makes one (%1)").arg(r.suites));
    check(r.reaching == 3 && r.isolated == 1,
          QStringLiteral("F6 three reach the settings (pp_settings.h itself does not count), one is isolated (%1, %2)")
              .arg(r.reaching).arg(r.isolated));
    check(missing == QStringList{ QStringLiteral("bad_test"), QStringLiteral("qmlish_test") },
          QStringLiteral("F7 the two that forgot are named (%1)").arg(missing.join(QStringLiteral(", "))));
}

} // namespace

int main()
{
    std::printf("store_isolation_lint_test — every suite that reaches ppSettings() makes an IsolatedStores (#187)\n\n");
    fixtures();

    std::printf("\nthe tests tree\n");
    bool           ok = false;
    const Manifest m  = readManifest(QStringLiteral(PP_STORE_MANIFEST), &ok);
    check(ok && !m.sources.isEmpty() && !m.tests.isEmpty(),
          QStringLiteral("the manifest %1 is there (the tests umbrella writes it at configure)").arg(QStringLiteral(PP_STORE_MANIFEST)));
    Scanner      sc;
    const Result r = sc.scan(m);
    std::printf("  %d suites; %d compile code that calls ppSettings(); %d of those make an IsolatedStores\n", r.suites,
                r.reaching, r.isolated);
    check(r.suites > 100 && r.reaching > 10, "the scan found the suites (a broken manifest would find none)");
    for (const Finding &f : r.missing) {
        QStringList names;
        for (const QString &s : f.reachedThrough) names << QDir(s).dirName();
        check(false, QStringLiteral("%1 compiles %2, which call ppSettings(), and makes no pinpoint::test::IsolatedStores "
                                    "(src/Core/tests/isolated_stores.h: the first line of main(), or the test class's "
                                    "first member)")
                         .arg(f.target, names.join(QStringLiteral(", "))));
    }
    if (r.missing.isEmpty()) check(true, "every suite that reaches the settings is isolated");

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
