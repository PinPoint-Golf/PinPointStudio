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

// store_isolation_lint_test (#187, #258): no test executable may forget pinpoint::test::IsolatedStores, or make it late.
//
// THE RULE. Every executable of the tests umbrella that compiles, or links a library that compiles, a source calling
// ppSettings() (the settings file, which also holds the phone pairings) must make an IsolatedStores
// (src/Core/tests/isolated_stores.h) FIRST, in one of its own sources:
//   - the first statement of main();
//   - the first member of a class (Q_OBJECT and access labels may come before it): the QTest class, or the set-up
//     class of QUICK_TEST_MAIN_WITH_SETUP;
//   - a namespace-scope object (a file-scope static), for a gtest_main suite.
// Anywhere else (a later statement, a later member, inside a test function or a nested block) fails as "late". A
// helper in a linked library does not count: the executable's own code must make it. Otherwise the executable reads,
// and may write, the machine's real settings: on Windows nothing else redirects them (#187's 46 suites).
//
// EVERY EXECUTABLE, NOT ONLY THE ONES A TEST IS NAMED AFTER. The rule used to check a target only when a test had its
// name; an opt-in suite with another test name (session_setup_ui_test) and every tool passed unseen. Now every
// executable is checked, and the ones that must read the real settings are a named decision in this file
// (kExemptions, each with its reason), never an allowlist:
//   - a TOOL (a developer's tool, run by hand on a scratch library) may skip the helper. The tests that run it are
//     declared with it and checked against the build tree's CTestTestfile.cmake files, so a new test that runs a tool
//     fails here until someone decides; an exemption that no longer reaches the settings, or whose declared test no
//     longer runs it, fails as stale;
//   - LATE (isolated_stores_test alone) may make the helper after other statements, because measuring the real
//     locations first is what it is for. It must still make one; one made first fails as stale.
//
// HOW IT KNOWS WHAT AN EXECUTABLE LINKS. The tests umbrella (tests/CMakeLists.txt) writes a manifest at the end of
// every configure, after the last target_sources: every executable and in-tree library target, its type, its sources
// (absolute), the in-tree targets it links (LINK_LIBRARIES, INTERFACE_LINK_LIBRARIES, $<TARGET_OBJECTS:..>, ALIASes
// resolved), and every test name. This file follows the links transitively and folds the libraries' sources in. So
// it works for any generator (Ninja on the Mac, NMake JOM on the studio PC) and needs no Python. The sources are read
// at run time with comments and string and character literals blanked, so a ppSettings or IsolatedStores in a
// comment does not count.
//
// Why a lint and not a hash of the real store around the run: the Windows exclusions were for READS (a suite graded
// against the studio's own settings), which a hash cannot see; a hash also only says that something changed, after
// the fact, not which suite; and the studio PC's script already hashes the settings around its runs.
//
// KNOWN LIMITS: a header-only call (an inline ppSettings() in a header the target includes but does not list) is not
// seen; app_settings.h's are reached through app_settings.cpp, which every user of AppSettings compiles. "First" is
// read from the code's shape, not proven: an earlier file-scope static that touches a store, in the same or another
// file, is not seen (the review of #187 found none). QStandardPaths users that never call ppSettings() (the pack
// readers' diagnosticsDataDir()) are not in the rule: they reach a folder named after the test binary, never the
// app's, and test mode is not required of them. "The first member" is of any class: a helper first in a gtest fixture
// passes, though a fixture is made per test. A test that runs a tool by a path its add_test command line does not show
// (a script that finds the tool itself, or the path in the test's ENVIRONMENT) is not seen.
//
// Before the scan, the rule runs on fixtures, so a rule that silently stops matching fails here.
//
//   ctest --test-dir build/tests -R '^store_isolation_lint_test$' --output-on-failure

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <initializer_list>
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

// ── The decisions: the executables that may read the real settings, or make the helper late ───────────────────────

struct Exemption
{
    enum Kind { Tool, Late };
    QString     target;
    Kind        kind;
    QStringList runners;   // Tool: exactly the tests that run it (checked against the CTestTestfile.cmake files)
    QString     reason;
};

const QList<Exemption> &kExemptions()
{
    // What each tool reads from the real settings, and why. RuleData reads two keys on every load (SET-06
    // ui/diagnosticsNormSetsOff, SET-15 ui/diagnosticsGradePolicy); AthleteStore's scan reads the [athletes] records.
    // No tool calls a writer of either, and no test runs the seven by-hand ones (checked below).
    static const QString rules = QStringLiteral("ui/diagnosticsNormSetsOff and ui/diagnosticsGradePolicy (RuleData)");
    static const QString athletes = QStringLiteral(" and the [athletes] records (AthleteStore)");
    static const QString testMode = QStringLiteral(
        "a by-hand tool on a scratch copy; it turns test mode on to keep the user's data out, but test mode does not "
        "move the settings file, so it still reads ");
    static const QString tail = QStringLiteral(" from the real one (reads only); no test runs it");
    static const QList<Exemption> list{
        { QStringLiteral("athlete_dump"), Exemption::Tool, { QStringLiteral("athlete_dump_ids_test") },
          QStringLiteral("the athlete-level parity dump; athlete_dump_ids_test drives it through cmake -P with no "
                         "main() of its own to isolate, so the studio PC excludes that test (studio-build.cmd TEXCL); "
                         "an opt-in settings path for the tool is #258's E2") },
        { QStringLiteral("regrade_ledger"), Exemption::Tool, { QStringLiteral("regrade_ledger_new_needs_ini_test") },
          QStringLiteral("re-reduces a session's ledger under the current pack; regrade_ledger_new_needs_ini_test "
                         "drives it through cmake -P, so the studio PC excludes that test (studio-build.cmd TEXCL); "
                         "an opt-in settings path for the tool is #258's E2") },
        { QStringLiteral("charts_series_dump"), Exemption::Tool, {}, testMode + rules + athletes + tail },
        { QStringLiteral("day_readings_dump"), Exemption::Tool, {}, testMode + rules + tail },
        { QStringLiteral("diag_rank_diff"), Exemption::Tool, {},
          QStringLiteral("a by-hand tool comparing two packs over sessions; incidentally, it grades through this "
                         "process's RuleData, which reads ") + rules + QStringLiteral(", and the session code it "
                         "compiles brings AthleteStore, which reads the [athletes] records (reads only); no test runs it") },
        { QStringLiteral("diag_uncertainty_report"), Exemption::Tool, {},
          QStringLiteral("a by-hand tool applying the uncertainty gates to sessions; incidentally, it grades through "
                         "this process's RuleData, which reads ") + rules + QStringLiteral(", and the session code it "
                         "compiles brings AthleteStore, which reads the [athletes] records (reads only); no test runs it") },
        { QStringLiteral("measure_dump"), Exemption::Tool, {},
          QStringLiteral("a by-hand tool writing every pack measure's reading as a CSV; incidentally, it installs this "
                         "process's RuleData, which reads ") + rules + QStringLiteral(" (reads only; it grades with "
                         "its own --policy, not the setting); no test runs it (measure_dump_test is its own suite, "
                         "with the helper)") },
        { QStringLiteral("post_shot_bench"), Exemption::Tool, {}, testMode + rules + athletes + tail },
        { QStringLiteral("variability_calibrate"), Exemption::Tool, {},
          QStringLiteral("a by-hand tool calibrating the variability grader over a read-only extract; incidentally, it "
                         "installs this process's RuleData, which reads ") + rules + QStringLiteral(" (reads only); no "
                         "test runs it") },
        { QStringLiteral("isolated_stores_test"), Exemption::Late, {},
          QStringLiteral("the helper's own proof: it reads the real locations first (test mode off, no QSettings "
                         "made, no store opened), then makes the helper and shows they moved") },
    };
    return list;
}

// ── Reading the sources ─────────────────────────────────────────────────────────────────────────────────────────

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

// Where a source makes its IsolatedStores: None (it makes none), Late (only somewhere the rule refuses), or First.
struct Placement
{
    enum Kind { None, Late, First };
    Kind    kind = None;
    QString where;   // First: "main()", "class X", "file scope"; Late: "line N, <why>"
};

// The text from the statement boundary before `open` (a '{') up to it: what the brace opens.
QString headOf(const QString &code, int open)
{
    int j = open - 1;
    while (j >= 0 && code.at(j) != QLatin1Char(';') && code.at(j) != QLatin1Char('{') && code.at(j) != QLatin1Char('}')) --j;
    return code.mid(j + 1, open - j - 1);
}

bool opensNamespace(const QString &head)
{
    static const QRegularExpression ns(QStringLiteral("\\bnamespace\\b")), ext(QStringLiteral("^\\s*extern\\s*$"));
    return head.contains(ns) || ext.match(head).hasMatch();
}

Placement placementOf(const QString &code)
{
    static const QRegularExpression decl(QStringLiteral(
        "(?:(?:::\\s*)?pinpoint\\s*::\\s*)?(?:test\\s*::\\s*)?\\bIsolatedStores\\s+[A-Za-z_]\\w*\\s*[;{(=]"));
    // What may come before the declaration in its scope and still leave it first.
    static const QRegularExpression prefix(QStringLiteral(
        "^(?:\\[\\[\\s*\\w+\\s*\\]\\]|\\bstatic\\b|\\bconst\\b|\\binline\\b|\\s)*$"));
    static const QRegularExpression access(QStringLiteral(
        "\\bQ_OBJECT\\b|\\bQ_GADGET\\b|\\b(?:public|protected|private)\\b\\s*(?:\\b(?:Q_SLOTS|slots)\\b\\s*)?:(?!:)"));
    static const QRegularExpression classHead(QStringLiteral("\\b(?:class|struct)\\s+(\\w+)"));
    static const QRegularExpression mainHead(QStringLiteral("\\bmain\\s*\\([^)]*\\)\\s*$"));

    Placement best;
    for (auto it = decl.globalMatch(code); it.hasNext();) {
        const int   pos = int(it.next().capturedStart());
        QList<int>  stack;   // the open braces enclosing pos
        for (int i = 0; i < pos; ++i) {
            if (code.at(i) == QLatin1Char('{')) stack.push_back(i);
            else if (code.at(i) == QLatin1Char('}') && !stack.isEmpty()) stack.pop_back();
        }
        const int line = int(code.left(pos).count(QLatin1Char('\n'))) + 1;
        Placement p;
        p.kind = Placement::Late;
        if (std::all_of(stack.cbegin(), stack.cend(), [&](int o) { return opensNamespace(headOf(code, o)); })) {
            p = { Placement::First, QStringLiteral("file scope") };
        } else {
            const int     open    = stack.back();
            const QString head    = headOf(code, open);
            QString       between = code.mid(open + 1, pos - open - 1);
            const auto    cls     = classHead.match(head);
            if (cls.hasMatch() && !head.contains(QLatin1Char('('))) {
                between.replace(access, QStringLiteral(" "));
                p = prefix.match(between).hasMatch()
                        ? Placement{ Placement::First, QStringLiteral("class ") + cls.captured(1) }
                        : Placement{ Placement::Late,
                                     QStringLiteral("line %1, not the first member of class %2").arg(line).arg(cls.captured(1)) };
            } else if (mainHead.match(head).hasMatch()) {
                p = prefix.match(between).hasMatch()
                        ? Placement{ Placement::First, QStringLiteral("main()") }
                        : Placement{ Placement::Late, QStringLiteral("line %1, not the first statement of main()").arg(line) };
            } else {
                p.where = QStringLiteral("line %1, inside a function or block other than main() (%2)")
                              .arg(line).arg(head.simplified().right(60));
            }
        }
        if (p.kind > best.kind) best = p;
        if (best.kind == Placement::First) break;
    }
    return best;
}

// ── The manifest and the build tree's tests ────────────────────────────────────────────────────────────────────

struct Target
{
    QString     type;
    QStringList sources;   // absolute
    QStringList links;     // in-tree targets it links
};

struct Manifest
{
    QMap<QString, Target> targets;
    QSet<QString>         tests;
    QStringList           unread;   // generator expressions the umbrella could not read a path or target from
    int                   edges = 0;
};

Manifest parseManifest(QTextStream &in)
{
    Manifest m;
    QString  target;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.size() < 3 || line.at(1) != QLatin1Char(' ')) continue;
        const QString rest = line.mid(2);
        switch (line.at(0).toLatin1()) {
        case 'T': target = rest; m.targets[target]; break;
        case 'K': if (!target.isEmpty()) m.targets[target].type = rest; break;
        case 'S': if (!target.isEmpty()) m.targets[target].sources << rest; break;
        case 'L': if (!target.isEmpty()) { m.targets[target].links << rest; ++m.edges; } break;
        case 'G': m.unread << target + QStringLiteral(": ") + rest; break;
        case 'X': m.tests.insert(rest); break;
        default: break;
        }
    }
    return m;
}

Manifest readManifest(const QString &path, bool *ok)
{
    QFile f(path);
    *ok = f.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!*ok) return {};
    QTextStream in(&f);
    return parseManifest(in);
}

// For each name, the tests whose command line (in a CTestTestfile.cmake under `buildDir`) runs a binary of that name.
QMap<QString, QStringList> testsRunning(const QString &buildDir, const QStringList &names, int *files)
{
    static const QRegularExpression addTest(QStringLiteral("^add_test\\(\\s*(?:\\[(=*)\\[(.*?)\\]\\1\\]|\"([^\"]*)\"|(\\S+))(.*)$"));
    QList<QRegularExpression> runs;
    for (const QString &n : names)
        runs << QRegularExpression(QStringLiteral("[/\\\\]") + QRegularExpression::escape(n) + QStringLiteral("(?:\\.exe)?[\"\\s;)]"));
    QMap<QString, QStringList> out;
    *files = 0;
    QDirIterator it(buildDir, { QStringLiteral("CTestTestfile.cmake") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        ++*files;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const auto m = addTest.match(in.readLine());
            if (!m.hasMatch()) continue;
            const QString test = !m.captured(2).isEmpty() ? m.captured(2) : !m.captured(3).isEmpty() ? m.captured(3) : m.captured(4);
            const QString cmd  = m.captured(5);
            for (int i = 0; i < names.size(); ++i)
                if (cmd.contains(runs.at(i))) out[names.at(i)] << test;
        }
    }
    return out;
}

// ── The scan ───────────────────────────────────────────────────────────────────────────────────────────────────

struct Finding
{
    enum Kind { Missing, Late, Stale, UndeclaredRunner };
    QString target;
    Kind    kind;
    QString detail;
};

struct Result
{
    int            executables = 0, namedByTest = 0, reaching = 0, first = 0, exempt = 0;
    QList<Finding> findings;
    QStringList    names(Finding::Kind k) const
    {
        QStringList out;
        for (const Finding &f : findings)
            if (f.kind == k) out << f.target;
        out.sort();
        return out;
    }
};

class Scanner
{
public:
    bool      callsPpSettings(const QString &path) { return code(path).contains(ppSettingsRe()); }
    bool      makesIsolatedStores(const QString &path) { return code(path).contains(isolatedRe()); }
    Placement placement(const QString &path) { return placementOf(code(path)); }

    Result scan(const Manifest &m, const QList<Exemption> &exemptions, const QMap<QString, QStringList> &runners)
    {
        QHash<QString, const Exemption *> exempt;
        for (const Exemption &e : exemptions) exempt.insert(e.target, &e);

        Result r;
        for (auto it = m.targets.cbegin(); it != m.targets.cend(); ++it) {
            if (it.value().type != QLatin1String("EXECUTABLE")) continue;
            const QString &name = it.key();
            ++r.executables;
            if (namedByTest(name, m.tests)) ++r.namedByTest;

            // What it reaches: its own sources, then every library it links, transitively.
            QStringList through;
            for (const QString &s : it.value().sources)
                if (reaches(s)) through << QFileInfo(s).fileName();
            QSet<QString> seen{ name };
            QStringList   queue = it.value().links;
            while (!queue.isEmpty()) {
                const QString lib = queue.takeFirst();
                if (seen.contains(lib) || !m.targets.contains(lib)) continue;
                seen.insert(lib);
                const Target &t = m.targets[lib];
                for (const QString &s : t.sources)
                    if (reaches(s)) through << QFileInfo(s).fileName() + QStringLiteral(" (library ") + lib + QLatin1Char(')');
                queue << t.links;
            }

            // Where its own code makes the helper.
            Placement best;
            for (const QString &s : it.value().sources) {
                if (!isCxx(s)) continue;
                Placement p = placement(s);
                if (p.kind > best.kind) best = { p.kind, QFileInfo(s).fileName() + QStringLiteral(": ") + p.where };
                if (best.kind == Placement::First) break;
            }

            const Exemption *e = exempt.value(name);
            if (through.isEmpty()) {
                if (e) r.findings.push_back({ name, Finding::Stale, QStringLiteral("it no longer reaches ppSettings()") });
                continue;
            }
            ++r.reaching;
            const QString via = through.join(QStringLiteral(", "));
            if (e && e->kind == Exemption::Tool) {
                ++r.exempt;
                QStringList actual = runners.value(name);
                actual.removeDuplicates();
                actual.sort();
                QStringList declared = e->runners;
                declared.sort();
                for (const QString &t : actual)
                    if (!declared.contains(t))
                        r.findings.push_back({ name, Finding::UndeclaredRunner,
                                               QStringLiteral("test %1 runs it, and it reads the real settings (%2)")
                                                   .arg(t, via) });
                for (const QString &t : declared)
                    if (!actual.contains(t))
                        r.findings.push_back({ name, Finding::Stale, QStringLiteral("test %1 no longer runs it").arg(t) });
                if (best.kind == Placement::First)
                    r.findings.push_back({ name, Finding::Stale, QStringLiteral("it makes the helper first now") });
                continue;
            }
            if (e && e->kind == Exemption::Late) {
                ++r.exempt;
                if (best.kind == Placement::None)
                    r.findings.push_back({ name, Finding::Missing, QStringLiteral("reaches %1").arg(via) });
                else if (best.kind == Placement::First)
                    r.findings.push_back({ name, Finding::Stale, QStringLiteral("it makes the helper first now") });
                continue;
            }
            if (best.kind == Placement::First) ++r.first;
            else if (best.kind == Placement::Late)
                r.findings.push_back({ name, Finding::Late, QStringLiteral("reaches %1; its helper is at %2").arg(via, best.where) });
            else
                r.findings.push_back({ name, Finding::Missing, QStringLiteral("reaches %1").arg(via) });
        }
        for (const Exemption &e : exemptions)
            if (!m.targets.contains(e.target))
                std::printf("  note: the exemption for %s names no target in this tree\n", qPrintable(e.target));
        return r;
    }

private:
    bool reaches(const QString &s)
    {
        return isCxx(s) && !s.endsWith(QLatin1String("/pp_settings.h")) && callsPpSettings(s);
    }
    // The old rule's suites: a test has its name, or (the QML suite) a test is named <target less "_test">.<file>.
    static bool namedByTest(const QString &target, const QSet<QString> &tests)
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

// ── Fixtures ───────────────────────────────────────────────────────────────────────────────────────────────────

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
    const auto    w = [&](const char *name, const QByteArray &text) { writeFile(d + QLatin1Char('/') + QLatin1String(name), text); };
    w("store.cpp", "QVariant f() { return ppSettings().value(\"k\"); }\n");
    w("quiet.cpp", "// ppSettings() in a comment\n/* ppSettings ( */ const char *s = \"ppSettings()\"; char c = '(';\n"
                   "int n = 1'000;\n");
    w("iso_test.cpp", "static const char *q = R\"QML(Text { text: \"it's \\\" ppSettings() \" }\n)QML\";\n"
                      "int main() { pinpoint::test::IsolatedStores stores; }\n");
    w("forgot_test.cpp", "// IsolatedStores would go here\nint main() { return 0; }\n");
    w("pp_settings.h", "inline QSettings ppSettings() { return {}; }\n");
    // Placement.
    w("main_first.cpp", "namespace { int helper() { return 1; } }\nint main(int argc, char *argv[])\n{\n"
                        "    // why it is first\n    pinpoint::test::IsolatedStores stores;\n"
                        "    QCoreApplication app(argc, argv);\n    return helper();\n}\n");
    w("main_second.cpp", "int main(int argc, char **argv)\n{\n    QCoreApplication app(argc, argv);\n"
                         "    pinpoint::test::IsolatedStores stores;\n    return 0;\n}\n");
    w("member_first.cpp", "class T : public QObject\n{\n    Q_OBJECT\n    pinpoint::test::IsolatedStores m_stores;\n"
                          "public:\n    int x = 0;\nprivate slots:\n    void a() {}\n};\nQTEST_GUILESS_MAIN(T)\n");
    w("member_after_label.cpp", "class S : public QObject\n{\n    Q_OBJECT\npublic slots:\n"
                                "    pinpoint::test::IsolatedStores m_stores;\n};\n");
    w("member_second.cpp", "class T : public QObject\n{\n    Q_OBJECT\n    QTemporaryDir m_dir;\n"
                           "    pinpoint::test::IsolatedStores m_stores;\n};\n");
    w("file_static.cpp", "namespace { int helper() { return 1; } }\nstatic pinpoint::test::IsolatedStores g_stores;\n"
                         "TEST(A, B) { EXPECT_EQ(helper(), 1); }\n");
    w("in_namespace.cpp", "namespace pinpoint { namespace {\nconst test::IsolatedStores g_stores;\n} }\n");
    w("in_function.cpp", "void T::initTestCase()\n{\n    pinpoint::test::IsolatedStores stores;\n}\n"
                         "int main() { return 0; }\n");
    w("in_block.cpp", "int main()\n{\n    {\n        pinpoint::test::IsolatedStores stores;\n    }\n}\n");
    w("pointer.cpp", "static pinpoint::test::IsolatedStores *g_stores = nullptr;\nint main() { return 0; }\n");

    Scanner sc;
    const auto at = [&](const char *name) { return d + QLatin1Char('/') + QLatin1String(name); };
    check(sc.callsPpSettings(at("store.cpp")), "F1 a real ppSettings() call is seen");
    check(!sc.callsPpSettings(at("quiet.cpp")), "F2 ppSettings in a comment, a string or after a character literal is not");
    check(sc.makesIsolatedStores(at("iso_test.cpp")) && !sc.callsPpSettings(at("iso_test.cpp")),
          "F3 an IsolatedStores after a raw string holding quotes, an apostrophe and a ppSettings() is seen, and that one is not");
    check(!sc.makesIsolatedStores(at("forgot_test.cpp")), "F4 one in a comment is not");

    const auto kindOf = [&](const char *name) { return sc.placement(at(name)).kind; };
    check(kindOf("main_first.cpp") == Placement::First && kindOf("iso_test.cpp") == Placement::First,
          "F8 the first statement of main() is first (a comment before it, a namespace before main)");
    check(kindOf("main_second.cpp") == Placement::Late, "F9 a helper that comes second in main() is late");
    check(kindOf("member_first.cpp") == Placement::First && kindOf("member_after_label.cpp") == Placement::First,
          "F10 a class's first member is first (after Q_OBJECT, and after an access label)");
    check(kindOf("member_second.cpp") == Placement::Late, "F11 a class's second member is late");
    check(kindOf("file_static.cpp") == Placement::First && kindOf("in_namespace.cpp") == Placement::First,
          "F12 a file-scope static is first, in a namespace too");
    check(kindOf("in_function.cpp") == Placement::Late && kindOf("in_block.cpp") == Placement::Late,
          "F13 one made in a test function, or in a nested block of main(), is late");
    check(kindOf("pointer.cpp") == Placement::None && kindOf("forgot_test.cpp") == Placement::None,
          "F14 a pointer to one, or one in a comment, makes none");

    // The manifest, as the umbrella writes it, through the same reader.
    QByteArray text;
    const auto exe = [&](const char *t, std::initializer_list<const char *> srcs, std::initializer_list<const char *> links,
                         const char *type = "EXECUTABLE") {
        text += QByteArray("T ") + t + "\nK " + type + "\n";
        for (const char *s : srcs) text += "S " + at(s).toUtf8() + "\n";
        for (const char *l : links) text += QByteArray("L ") + l + "\n";
    };
    exe("good_test", { "iso_test.cpp", "store.cpp" }, {});
    exe("bad_test", { "forgot_test.cpp", "store.cpp" }, {});
    exe("calm_test", { "forgot_test.cpp", "quiet.cpp", "pp_settings.h" }, {});
    exe("qmlish_test", { "forgot_test.cpp", "store.cpp" }, {});
    exe("late_test", { "main_second.cpp", "store.cpp" }, {});
    exe("lib_user_test", { "forgot_test.cpp" }, { "mid_lib" });
    exe("lib_iso_test", { "main_first.cpp" }, { "mid_lib" });
    exe("mid_lib", { "quiet.cpp" }, { "store_lib", "mid_lib" }, "STATIC_LIBRARY");
    exe("store_lib", { "store.cpp" }, {}, "OBJECT_LIBRARY");
    exe("helper_lib_test", { "forgot_test.cpp", "store.cpp" }, { "iso_lib" });
    exe("iso_lib", { "iso_test.cpp" }, {}, "STATIC_LIBRARY");
    exe("unnamed", { "forgot_test.cpp", "store.cpp" }, {});
    exe("exempt_tool", { "store.cpp" }, {});
    exe("stale_tool", { "quiet.cpp" }, {});
    exe("sneaky_tool", { "store.cpp" }, { "store_lib" });
    exe("late_ok_test", { "main_second.cpp", "store.cpp" }, {});
    text += "G unnamed: $<FOO:bar>\n";
    for (const char *t : { "good_test", "bad_test", "calm_test", "qmlish.tst_one", "late_test", "lib_user_test",
                           "lib_iso_test", "helper_lib_test", "tool_driver_test", "late_ok_test" })
        text += QByteArray("X ") + t + "\n";
    QTextStream    in(&text);
    const Manifest m = parseManifest(in);
    check(m.targets.size() == 16 && m.edges == 6 && m.tests.size() == 10 && m.unread.size() == 1
              && m.targets.value(QStringLiteral("store_lib")).type == QLatin1String("OBJECT_LIBRARY"),
          QStringLiteral("F15 the reader keeps targets, types, links, tests and the unread expressions (%1, %2, %3)")
              .arg(m.targets.size()).arg(m.edges).arg(m.tests.size()));

    // The tests a build tree runs, from its CTestTestfile.cmake files.
    QDir(d).mkpath(QStringLiteral("tree/sub"));
    w("tree/CTestTestfile.cmake", "subdirs(\"sub\")\n");
    w("tree/sub/CTestTestfile.cmake",
      "add_test([=[tool_driver_test]=] \"/usr/bin/cmake\" \"-DTOOL=/b/sub/exempt_tool\" \"-P\" \"/s/check.cmake\")\n"
      "add_test([=[sneaky_test]=] \"C:/b/sub/sneaky_tool.exe\" \"--real\")\n"
      "add_test([=[exempt_tool_test]=] \"/b/sub/exempt_tool_test\")\n");
    int        files   = 0;
    const auto runners = testsRunning(d + QStringLiteral("/tree"),
                                      { QStringLiteral("exempt_tool"), QStringLiteral("sneaky_tool"), QStringLiteral("stale_tool") },
                                      &files);
    check(files == 2 && runners.value(QStringLiteral("exempt_tool")) == QStringList{ QStringLiteral("tool_driver_test") }
              && runners.value(QStringLiteral("sneaky_tool")) == QStringList{ QStringLiteral("sneaky_test") }
              && !runners.contains(QStringLiteral("stale_tool")),
          "F16 the tests that run a binary are read from the CTestTestfile.cmake files (a -DTOOL= path, a .exe; "
          "not a binary whose name only starts the same)");

    const QList<Exemption> ex{
        { QStringLiteral("exempt_tool"), Exemption::Tool, { QStringLiteral("tool_driver_test") }, QStringLiteral("fixture") },
        { QStringLiteral("stale_tool"), Exemption::Tool, {}, QStringLiteral("fixture") },
        { QStringLiteral("sneaky_tool"), Exemption::Tool, {}, QStringLiteral("fixture") },
        { QStringLiteral("late_ok_test"), Exemption::Late, {}, QStringLiteral("fixture") },
    };
    const Result r = sc.scan(m, ex, runners);
    check(r.executables == 13 && r.namedByTest == 9,
          QStringLiteral("F5 every executable is checked, libraries are not (13), the old rule's nine among them (%1, %2)")
              .arg(r.executables).arg(r.namedByTest));
    check(r.reaching == 11 && r.first == 2 && r.exempt == 3,
          QStringLiteral("F6 eleven reach the settings (pp_settings.h itself does not count; a library two links away "
                         "does), two make the helper first, three are exempt (%1, %2, %3)")
              .arg(r.reaching).arg(r.first).arg(r.exempt));
    const QStringList missing = r.names(Finding::Missing);
    check(missing == QStringList{ QStringLiteral("bad_test"), QStringLiteral("helper_lib_test"), QStringLiteral("lib_user_test"),
                                  QStringLiteral("qmlish_test"), QStringLiteral("unnamed") },
          QStringLiteral("F7 the ones that forgot are named: through a linked library too, an executable no test is "
                         "named after too, and a helper in a linked library does not count (%1)")
              .arg(missing.join(QStringLiteral(", "))));
    check(r.names(Finding::Late) == QStringList{ QStringLiteral("late_test") },
          QStringLiteral("F17 a suite that makes the helper second fails (%1)").arg(r.names(Finding::Late).join(QStringLiteral(", "))));
    check(r.names(Finding::Stale) == QStringList{ QStringLiteral("stale_tool") }
              && r.names(Finding::UndeclaredRunner) == QStringList{ QStringLiteral("sneaky_tool") },
          QStringLiteral("F18 an exemption that no longer reaches the settings is stale, and a test that runs an exempt "
                         "tool undeclared fails (%1; %2)")
              .arg(r.names(Finding::Stale).join(QStringLiteral(", ")), r.names(Finding::UndeclaredRunner).join(QStringLiteral(", "))));
}

} // namespace

int main()
{
    std::printf("store_isolation_lint_test — every executable that reaches ppSettings() makes an IsolatedStores first "
                "(#187, #258)\n\n");
    fixtures();

    std::printf("\nthe tests tree\n");
    bool           ok = false;
    const Manifest m  = readManifest(QStringLiteral(PP_STORE_MANIFEST), &ok);
    check(ok && !m.targets.isEmpty() && !m.tests.isEmpty(),
          QStringLiteral("the manifest %1 is there (the tests umbrella writes it at configure)").arg(QStringLiteral(PP_STORE_MANIFEST)));
    check(m.edges > 0, QStringLiteral("the manifest lists the in-tree libraries the executables link (%1 links)").arg(m.edges));
    for (const QString &u : m.unread) std::printf("  note: a generator expression not read: %s\n", qPrintable(u));
    QStringList unknown;   // a linked in-tree target the walk did not list would be followed nowhere
    for (auto it = m.targets.cbegin(); it != m.targets.cend(); ++it)
        for (const QString &l : it.value().links)
            if (!m.targets.contains(l)) unknown << it.key() + QStringLiteral(" -> ") + l;
    check(unknown.isEmpty(), QStringLiteral("every linked in-tree target is in the manifest, so its sources are scanned (%1)")
                                 .arg(unknown.isEmpty() ? QStringLiteral("all") : unknown.join(QStringLiteral(", "))));

    QStringList tools;
    for (const Exemption &e : kExemptions())
        if (e.kind == Exemption::Tool) tools << e.target;
    int        files   = 0;
    const auto runners = testsRunning(QFileInfo(QStringLiteral(PP_STORE_MANIFEST)).absolutePath(), tools, &files);
    check(files > 5, QStringLiteral("the build tree's CTestTestfile.cmake files are read (%1)").arg(files));

    Scanner      sc;
    const Result r = sc.scan(m, kExemptions(), runners);
    std::printf("  %d executables (%d of them named by a test, the old rule's suites); %d reach ppSettings(), in their "
                "own sources or a linked library; %d make an IsolatedStores first; %d are exempt by name\n",
                r.executables, r.namedByTest, r.reaching, r.first, r.exempt);
    for (const Exemption &e : kExemptions())
        std::printf("  exempt (%s): %s — %s\n", e.kind == Exemption::Tool ? "tool" : "late", qPrintable(e.target),
                    qPrintable(e.reason));
    check(r.executables > 100 && r.reaching > 10, "the scan found the executables (a broken manifest would find none)");
    for (const Finding &f : r.findings) {
        switch (f.kind) {
        case Finding::Missing:
            check(false, QStringLiteral("%1 %2, and makes no pinpoint::test::IsolatedStores (src/Core/tests/isolated_stores.h: "
                                        "the first statement of main(), the test class's first member, or a file-scope "
                                        "static)").arg(f.target, f.detail));
            break;
        case Finding::Late:
            check(false, QStringLiteral("%1 %2: make it the first statement of main(), the test class's first member, "
                                        "or a file-scope static").arg(f.target, f.detail));
            break;
        case Finding::Stale:
            check(false, QStringLiteral("%1's exemption is stale: %2 (kExemptions, this file)").arg(f.target, f.detail));
            break;
        case Finding::UndeclaredRunner:
            check(false, QStringLiteral("%1: %2. A test must not reach the real settings: give the tool a settings path "
                                        "the test can point at a scratch folder, or decide it in kExemptions")
                             .arg(f.target, f.detail));
            break;
        }
    }
    if (r.findings.isEmpty()) check(true, "every executable that reaches the settings makes the helper first, or is exempt by name");

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
