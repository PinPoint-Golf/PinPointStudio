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

// library_membership_lint_test (#424): each source is built once into any link.
//
// THE RULES, over every target of the tests umbrella (its manifest, written at configure from CMake's own target
// properties: cmake/PinPointTargetManifest.cmake). A "source" is a file under src/ that compiles to an object (.cpp,
// .cc, .c, .mm) or a header moc reads (Q_OBJECT, Q_GADGET, Q_NAMESPACE: its meta-object is an object too); a plain
// header listed twice compiles nothing and is not counted.
//   1. No source is in two in-tree libraries.
//   2. No executable lists a source of a library it reaches through its links (directly or through another library).
//      Under MSVC that is LNK2005; on the Mac and Linux the linker silently takes one copy.
//   5. No executable lists a source a RING library holds (kRingLibraries), whether or not it links that library: the
//      ring libraries exist so that no test or tool hand-lists their files. An executable that must compile one of
//      them itself (other flags, no Qt) is a named exception here, with its reason, never an allowlist file; an
//      exception that no longer holds fails as stale.
// The ring sources no ring library takes (kLeftInExecutables: OpenCV, the Buffer, special flags) are named with their
// reason; each must exist and stay out of the ring libraries, or its line is stale.
// The other in-tree libraries (pinpoint_buffer, pinpoint_swingstore, the device protocol libraries) are under rules 1
// and 2 only: tests still compile some Buffer files themselves, which is not this check's concern.
// The app tree runs rules 1 and 2 at configure (pp_check_library_membership), since it has no lint suite.
//
// Also printed, not checked: how many product sources the executables still hand-list, by directory (the stage's
// remaining hand-listing, P1's count).
//
// Before the scan, every rule runs on fixtures, so a rule that silently stops matching fails here.
//
//   cmake --build build/tests --target library_membership_lint_test -j 6
//   ctest --test-dir build/tests -R '^library_membership_lint_test$' --output-on-failure

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <initializer_list>

#ifndef PP_STORE_MANIFEST
#  error "PP_STORE_MANIFEST must name the manifest the tests umbrella writes"
#endif
#ifndef PP_SRC_ROOT
#  error "PP_SRC_ROOT must name the repository's src/ folder"
#endif

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

// ── The decisions ──────────────────────────────────────────────────────────────────────────────────────────────────

// The ring libraries (cmake/rings/<name>/). Rule 5 holds for these.
const QStringList &kRingLibraries()
{
    static const QStringList l{ QStringLiteral("pp_platform"), QStringLiteral("pp_vocabulary"),
                                QStringLiteral("pp_rules"),    QStringLiteral("pp_domain"),
                                QStringLiteral("pp_stores") };
    return l;
}

// An executable that compiles a ring library's source itself, and why. `source` is the path under src/.
struct Exception
{
    QString target;
    QString source;
    QString reason;
};

const QList<Exception> &kExceptions()
{
    static const QList<Exception> list{};
    return list;
}

// The ring sources left in the executables on purpose (#424): no ring library holds them, so the tests, tools and the
// app that use one list it. Each is checked to exist and to be in no ring library; one that a library takes is stale.
struct LeftOut
{
    QString source;   // path under src/
    QString reason;
};

const QList<LeftOut> &kLeftInExecutables()
{
    static const QString cv = QStringLiteral("needs OpenCV, which a ring library would force on every user");
    static const QString storesCv = QStringLiteral(
        "a store that needs OpenCV or the Buffer: in pp_stores it would make every user of the stores link OpenCV "
        "for five files (the owner, 28 Nov 2026)");
    static const QList<LeftOut> list{
        { QStringLiteral("Stores/recording_loader.cpp"), storesCv },
        { QStringLiteral("Analysis/mp4_frame_reader.cpp"), storesCv + QStringLiteral(" (the MP4 reader the loader builds)") },
        { QStringLiteral("Stores/recording_phase_grid.cpp"), storesCv + QStringLiteral(" (it calls the loader)") },
        { QStringLiteral("Stores/swing_exporter.cpp"), storesCv },
        { QStringLiteral("Stores/export_tone.cpp"), storesCv },
        { QStringLiteral("Vocabulary/Recording/frame_decode.cpp"), cv },
        { QStringLiteral("Timeline/event_refine.cpp"), cv + QStringLiteral(" (through Club/ball_anchor.h)") },
        { QStringLiteral("Analysis/frame_store.cpp"), cv },
        { QStringLiteral("Analysis/recording_integrity.cpp"),
          QStringLiteral("calls the Buffer's SwingWindow; pp_rules links no Buffer") },
        { QStringLiteral("IMU/imu_vision_fuser.cpp"), QStringLiteral("calls the Buffer's SwingWindow; pp_rules links no Buffer") },
        { QStringLiteral("Skeleton3d/skeleton3d_fit.cpp"),
          QStringLiteral("compiled optimised and assert-free by the root CMakeLists.txt's source property, and Qt-free "
                         "with its own flags in skeleton3d_test") },
        { QStringLiteral("Stores/swing_folder_reanalysis.cpp"),
          QStringLiteral("the offline tools' only, and it calls the re-analysis pipeline (Application)") },
    };
    return list;
}

// ── The manifest ───────────────────────────────────────────────────────────────────────────────────────────────────

struct Target
{
    QString     type;
    QStringList sources;   // absolute
    QStringList links;     // in-tree targets it links
};

using Manifest = QMap<QString, Target>;

Manifest parseManifest(QTextStream &in)
{
    Manifest m;
    QString  target;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.size() < 3 || line.at(1) != QLatin1Char(' ')) continue;
        const QString rest = line.mid(2);
        switch (line.at(0).toLatin1()) {
        case 'T': target = rest; m[target]; break;
        case 'K': if (!target.isEmpty()) m[target].type = rest; break;
        case 'S': if (!target.isEmpty()) m[target].sources << rest; break;
        case 'L': if (!target.isEmpty()) m[target].links << rest; break;
        default: break;
        }
    }
    return m;
}

bool isLibrary(const QString &type)
{
    return type == QLatin1String("STATIC_LIBRARY") || type == QLatin1String("OBJECT_LIBRARY")
        || type == QLatin1String("SHARED_LIBRARY") || type == QLatin1String("MODULE_LIBRARY");
}

// A file that is built into a link: an object of its own, or a header moc reads. Reads a header once.
class Counter
{
public:
    bool counts(const QString &path)
    {
        static const QRegularExpression object(QStringLiteral("\\.(c|cc|cpp|cxx|mm)$"));
        static const QRegularExpression header(QStringLiteral("\\.(h|hpp|hh)$"));
        if (object.match(path).hasMatch()) return true;
        if (!header.match(path).hasMatch()) return false;
        const auto it = m_moc.constFind(path);
        if (it != m_moc.constEnd()) return *it;
        static const QRegularExpression moc(QStringLiteral("^[ \\t]*(Q_OBJECT|Q_GADGET|Q_NAMESPACE)\\b"),
                                            QRegularExpression::MultilineOption);
        QFile      f(path);
        const bool m = f.open(QIODevice::ReadOnly | QIODevice::Text) && moc.match(QString::fromUtf8(f.readAll())).hasMatch();
        m_moc.insert(path, m);
        return m;
    }

private:
    QHash<QString, bool> m_moc;
};

struct Finding
{
    int     rule;
    QString text;
};

struct Result
{
    QList<Finding>     findings;
    int                executables = 0;
    int                libraries   = 0;
    int                ringSources = 0;
    QMap<QString, int> handListed;   // a product source an executable still compiles itself, by directory
    QStringList        ringsSeen;
};

Result scan(const Manifest &m, const QString &srcRoot, const QList<Exception> &exceptions)
{
    Result  r;
    Counter counter;
    const QString root = QDir::cleanPath(srcRoot) + QLatin1Char('/');
    auto under = [&](const QString &s) { return s.startsWith(root); };

    QHash<QString, QString> owner;   // source -> library
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        if (!isLibrary(it->type)) continue;
        ++r.libraries;
        if (kRingLibraries().contains(it.key())) r.ringsSeen << it.key();
        QSet<QString> seen;
        for (const QString &s : it->sources) {
            if (!under(s) || seen.contains(s) || !counter.counts(s)) continue;
            seen.insert(s);
            if (kRingLibraries().contains(it.key())) ++r.ringSources;
            const auto o = owner.constFind(s);
            if (o != owner.constEnd() && *o != it.key())
                r.findings << Finding{ 1, QStringLiteral("%1 is in two libraries, %2 and %3").arg(s.mid(root.size()), *o, it.key()) };
            else
                owner.insert(s, it.key());
        }
    }

    QSet<QString> used;   // exceptions that matched
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        if (it->type != QLatin1String("EXECUTABLE")) continue;
        ++r.executables;
        QSet<QString> reach;
        QStringList   todo = it->links;
        while (!todo.isEmpty()) {
            const QString l = todo.takeFirst();
            if (reach.contains(l)) continue;
            reach.insert(l);
            const auto t = m.constFind(l);
            if (t != m.constEnd()) todo << t->links;
        }
        QSet<QString> seen;
        for (const QString &s : it->sources) {
            if (!under(s) || seen.contains(s) || !counter.counts(s)) continue;
            seen.insert(s);
            const QString rel = s.mid(root.size());
            const auto    o   = owner.constFind(s);
            if (o == owner.constEnd()) {
                if (!rel.contains(QLatin1String("/tests/"))) ++r.handListed[rel.section(QLatin1Char('/'), 0, 0)];
                continue;
            }
            if (reach.contains(*o)) {
                r.findings << Finding{ 2, QStringLiteral("%1 lists %2, which %3 (a library it links) already holds: take "
                                                         "it out of the list").arg(it.key(), rel, *o) };
                continue;
            }
            if (!kRingLibraries().contains(*o)) continue;
            bool excepted = false;
            for (const Exception &e : exceptions)
                if (e.target == it.key() && e.source == rel) { excepted = true; used.insert(e.target + QLatin1Char(' ') + e.source); }
            if (!excepted)
                r.findings << Finding{ 5, QStringLiteral("%1 lists %2, a source of %3: link %3 instead (LINK %3), or name "
                                                         "it in kExceptions with the reason it must compile it itself")
                                              .arg(it.key(), rel, *o) };
        }
    }
    for (const Exception &e : exceptions)
        if (!used.contains(e.target + QLatin1Char(' ') + e.source))
            r.findings << Finding{ 5, QStringLiteral("the exception for %1 compiling %2 is stale: it no longer lists it, or "
                                                     "no ring library holds it (kExceptions, this file)").arg(e.target, e.source) };
    return r;
}

// ── The fixtures ───────────────────────────────────────────────────────────────────────────────────────────────────

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

int countRule(const Result &r, int rule)
{
    int n = 0;
    for (const Finding &f : r.findings) n += f.rule == rule;
    return n;
}

void fixtures()
{
    std::printf("the rules, on fixtures\n");
    QTemporaryDir tmp;
    check(tmp.isValid(), "a temporary directory for the fixtures");
    const QString src = tmp.path() + QStringLiteral("/src");
    writeFile(src + QStringLiteral("/A/moc.h"), "#pragma once\nclass M {\n    Q_GADGET\n};\n");
    writeFile(src + QStringLiteral("/A/plain.h"), "#pragma once\nstruct P {};\n");
    writeFile(src + QStringLiteral("/A/in_comment.h"), "#pragma once\n// not a Q_OBJECT\n");
    auto S = [&](const char *rel) { return src + QLatin1Char('/') + QLatin1String(rel); };

    auto run = [&](const QString &text, const QList<Exception> &ex = {}) {
        QString     t = text;
        QTextStream in(&t);
        return scan(parseManifest(in), src, ex);
    };
    auto lib = [&](const char *name, std::initializer_list<const char *> files, std::initializer_list<const char *> links = {}) {
        QString t = QStringLiteral("T %1\nK STATIC_LIBRARY\n").arg(QLatin1String(name));
        for (const char *f : files) t += QStringLiteral("S %1\n").arg(S(f));
        for (const char *l : links) t += QStringLiteral("L %1\n").arg(QLatin1String(l));
        return t;
    };
    auto exe = [&](const char *name, std::initializer_list<const char *> files, std::initializer_list<const char *> links = {}) {
        QString t = QStringLiteral("T %1\nK EXECUTABLE\n").arg(QLatin1String(name));
        for (const char *f : files) t += QStringLiteral("S %1\n").arg(S(f));
        for (const char *l : links) t += QStringLiteral("L %1\n").arg(QLatin1String(l));
        return t;
    };

    const QString clean = lib("pp_platform", { "A/a.cpp", "A/moc.h" }) + lib("pp_rules", { "B/b.cpp" }, { "pp_platform" })
                        + exe("t1", { "A/tests/t1.cpp", "C/c.cpp" }, { "pp_rules" });
    Result r = run(clean);
    check(r.findings.isEmpty(), "a clean tree: no finding");
    check(r.handListed.value(QStringLiteral("C")) == 1, "a test's own file is not counted as hand-listed; C/c.cpp is");

    r = run(clean + lib("other", { "B/b.cpp" }));
    check(countRule(r, 1) == 1, "rule 1: a .cpp in two libraries");
    r = run(clean + lib("other", { "A/moc.h" }));
    check(countRule(r, 1) == 1, "rule 1: a moc'd header in two libraries");
    r = run(lib("x", { "A/plain.h", "A/in_comment.h" }) + lib("y", { "A/plain.h", "A/in_comment.h" }));
    check(r.findings.isEmpty(), "rule 1: a plain header, or one naming Q_OBJECT in a comment, in two libraries is no finding");

    r = run(clean + exe("t2", { "A/a.cpp" }, { "pp_rules" }));
    check(countRule(r, 2) == 1 && countRule(r, 5) == 0, "rule 2: a test lists a source of a library it reaches through another");
    r = run(clean + exe("t3", { "A/moc.h" }, { "pp_platform" }));
    check(countRule(r, 2) == 1, "rule 2: a test lists a moc'd header of a library it links");
    r = run(clean + exe("t4", { "B/b.cpp" }));
    check(countRule(r, 5) == 1, "rule 5: a test compiles a ring library's source and links no library");
    r = run(lib("pinpoint_buffer", { "D/d.cpp" }) + exe("t5", { "D/d.cpp" }));
    check(r.findings.isEmpty(), "rule 5 is for the ring libraries: a test compiling a Buffer file itself is no finding");
    r = run(clean + exe("t6", { "B/b.cpp" }), { { QStringLiteral("t6"), QStringLiteral("B/b.cpp"), QStringLiteral("why") } });
    check(r.findings.isEmpty(), "rule 5: a named exception is honoured");
    r = run(clean, { { QStringLiteral("t6"), QStringLiteral("B/b.cpp"), QStringLiteral("why") } });
    check(countRule(r, 5) == 1, "rule 5: an exception that no longer holds is stale");
}

}   // namespace

int main()
{
    std::printf("library_membership_lint_test — each source is built once into any link (#424)\n\n");
    fixtures();

    std::printf("\nthe tests tree\n");
    QFile f(QStringLiteral(PP_STORE_MANIFEST));
    const bool ok = f.open(QIODevice::ReadOnly | QIODevice::Text);
    check(ok, QStringLiteral("the manifest %1 is there (the tests umbrella writes it at configure)").arg(QStringLiteral(PP_STORE_MANIFEST)));
    if (!ok) return 1;
    QTextStream    in(&f);
    const Manifest m = parseManifest(in);
    const Result   r = scan(m, QStringLiteral(PP_SRC_ROOT), kExceptions());
    std::printf("  %d executables, %d in-tree libraries; the ring libraries here: %s (%d sources)\n", r.executables,
                r.libraries, qPrintable(r.ringsSeen.join(QStringLiteral(", "))), r.ringSources);
    check(r.executables > 100 && r.ringsSeen.contains(QStringLiteral("pp_platform")),
          "the scan found the executables and pp_platform (a broken manifest would find none)");
    for (const Exception &e : kExceptions())
        std::printf("  exception: %s compiles %s itself — %s\n", qPrintable(e.target), qPrintable(e.source), qPrintable(e.reason));
    for (const Finding &fd : r.findings) check(false, QStringLiteral("rule %1: %2").arg(fd.rule).arg(fd.text));
    QSet<QString> ringHeld;
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        if (kRingLibraries().contains(it.key()))
            for (const QString &s : it->sources) ringHeld.insert(QDir::cleanPath(s));
    for (const LeftOut &l : kLeftInExecutables()) {
        const QString abs = QDir::cleanPath(QStringLiteral(PP_SRC_ROOT) + QLatin1Char('/') + l.source);
        std::printf("  left in the executables: %s — %s\n", qPrintable(l.source), qPrintable(l.reason));
        check(QFileInfo::exists(abs) && !ringHeld.contains(abs),
              QStringLiteral("%1 exists and no ring library holds it (else its kLeftInExecutables line is stale)").arg(l.source));
    }
    if (r.findings.isEmpty())
        check(true, "no source in two libraries, none listed beside a library holding it, no ring library's source hand-listed");
    int total = 0;
    QStringList parts;
    for (auto it = r.handListed.cbegin(); it != r.handListed.cend(); ++it) {
        total += it.value();
        parts << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    }
    std::printf("  still hand-listed by the executables (no ring library holds them): %d listings — %s\n", total,
                qPrintable(parts.join(QStringLiteral(", "))));

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
