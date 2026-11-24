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

// Namespace collision test — platform refactor B1.20a (build/platform/B1/BRIEF-B1.20a.md).
//
// Two types of one name in one namespace, defined in two files, are an ODR violation the linker accepts: on 7 Oct
// 2026 the DAG executor's `struct DagEdge` and the diagnostics layout's `pinpoint::analysis::DagEdge` were both
// linked into the app, the linker kept one inline constructor, and every in-app re-analysis died with locals the
// source cannot produce. This test stops that coming back. Like architecture_lint_test it reads the SOURCE of
// everything under src/ at run time (C++ only: .h .hpp .cpp .cc .mm; tests/, Testing/, third_party/ and build/ are
// skipped) and fails when one (namespace, name) is DEFINED in two different files.
//
//   - Declarations read: `class X`, `struct X`, `enum X` / `enum class X` / `enum struct X`, and `using X = …`, at
//     namespace scope (every enclosing brace a namespace, or an `extern "C" {` block, which is transparent).
//     A type nested in a class or local to a function is not at namespace scope and is not read.
//   - A definition is a name followed by `{`, a base clause or an enum's underlying type and then `{`, or `final`;
//     an alias `using X = …;` is a definition. A forward declaration is `class X;` (or `enum class X : int;`).
//     Any number of forward declarations beside one definition pass; two definitions in two files fail, as do two
//     identical aliases (the brief: only a forward declaration and its definition pass).
//   - Namespaces nest: `namespace a { namespace b {` and `namespace a::b {` are one namespace, a::b; an `inline
//     namespace` is a part of the name like any other. The global namespace counts (printed `::Name`).
//   - Not read: an anonymous namespace (per file).
//   - A `detail` namespace (any part named `detail`) is NOT per file in C++: its types have external linkage.
//     A repeat with a site in a HEADER fails like any other (PR-42: `pinpoint::analysis::detail`, the 7 Oct
//     namespace, is opened in several headers). A repeat whose every site is a source file (.cpp, .cc, .mm) is
//     printed as a `note:`, not a failure (PR-42 keeps the brief's exemption there). C++ would still link two
//     such types as one, so a note is worth reading.
//   - Not declarations: template parameters (`template <class T>`), specialisations (`struct hash<Foo>`),
//     qualified names (`struct A::B {`, an out-of-line nested definition), anonymous `enum {`, `using namespace`,
//     `using a::b;`, an elaborated use (`struct stat st;`, `struct tm *f();`), Objective-C's `@class` and
//     `@interface`. A class macro is read past: `class PP_EXPORT Name final : Base {` defines Name.
//   - Comments and string literals are blanked first (lint_source_text.h, shared with architecture_lint_test), and
//     preprocessor lines (with their `\` continuations) are blanked too, so a `#define` that spells a type is not
//     read. A file whose braces or parentheses do not balance with its directives blanked (an `#if` whose
//     branches open different scopes) FAILS, because its scopes could not be read.
//
// A finding is printed `namespace::Name: file1:line, file2:line` (every defining site). There is no allow-list:
// a finding is a real ODR risk, renamed or moved into its own namespace; which, is the Solution Architect's call.
//
// Before the scan, the rules run on fixtures (a planted duplicate fails; a forward declaration and its
// definition pass; one name in two namespaces passes; …), so a rule that silently stops matching fails here
// rather than passing everything.
//
// KNOWN LIMITS (checked against the tree when they were written down; re-check before relying on one):
//   - `typedef` and `union` are not read (the brief names class, struct, enum and using).
//   - Two files that are never linked together (a macOS and a Windows variant of one source) are reported all
//     the same: the test reads text, not the build. #if 0 blocks are read too.
//   - A namespace opened by a macro (QT_BEGIN_NAMESPACE, …) is read as whatever the macro's text is: the global
//     namespace, since the macro has no braces of its own.
//   - A type declared by a macro (Q_DECLARE_FLAGS, …) is not read.
//
//   cmake --build build/tests --target namespace_collision_test -j 3
//   ctest --test-dir build/tests -R '^namespace_collision_test$' --output-on-failure

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdio>

#include "lint_source_text.h"

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

// One type declared at namespace scope.
struct Decl {
    QString ns;              // "a::b", or "" for the global namespace
    QString name;
    QString kind;            // class, struct, enum, using
    bool    definition = false;
    bool    detail     = false;  // inside a namespace part named `detail` (a repeat only in source files is a note)
    QString file;
    int     line = 0;
};

// The code copy with preprocessor directives (and their continuation lines) blanked; newlines kept.
QString withoutDirectives(const QString &code)
{
    QStringList lines = code.split(QLatin1Char('\n'));
    bool        cont  = false;
    for (QString &l : lines) {
        const bool directive = cont || l.trimmed().startsWith(QLatin1Char('#'));
        if (!directive) continue;
        cont = l.endsWith(QLatin1Char('\\'));
        l.fill(QLatin1Char(' '));
    }
    return lines.join(QLatin1Char('\n'));
}

struct Token {
    QString text;
    int     offset = 0;
    bool    ident  = false;
};

QVector<Token> tokens(const QString &code)
{
    QVector<Token> out;
    const int      n = code.size();
    for (int i = 0; i < n;) {
        const QChar c = code.at(i);
        if (c.isSpace()) { ++i; continue; }
        if (pp_lint::isIdent(c)) {
            int j = i;
            while (j < n && pp_lint::isIdent(code.at(j))) ++j;
            out.append({ code.mid(i, j - i), i, !c.isDigit() });
            i = j;
            continue;
        }
        if (c == QLatin1Char(':') && i + 1 < n && code.at(i + 1) == QLatin1Char(':')) {
            out.append({ QStringLiteral("::"), i, false });
            i += 2;
            continue;
        }
        out.append({ QString(c), i, false });
        ++i;
    }
    return out;
}

// A specifier with arguments between `class` and the name: alignas(…), __declspec(…), __attribute__(…) or an
// all-capitals macro. Anything else followed by '(' is a function (`struct tm *f()`, `struct X make()`).
bool isSpecifierMacro(const QString &id)
{
    if (id == QLatin1String("alignas") || id == QLatin1String("__declspec") || id == QLatin1String("__attribute__"))
        return true;
    bool letter = false;
    for (const QChar c : id) {
        if (c.isLower()) return false;
        letter = letter || c.isLetter();
    }
    return letter;
}

enum class Scope { Namespace, Transparent, Other };
struct Frame {
    Scope       scope = Scope::Other;
    QStringList parts;          // a namespace's name parts ("a::b" → a, b); empty = anonymous
};

// Every class / struct / enum / using alias at namespace scope in one file. `problems` collects what made the
// file unreadable (unbalanced braces or parentheses).
QVector<Decl> declarations(const QString &rel, const QString &rawText, QStringList *problems)
{
    const pp_lint::Source src  = pp_lint::makeSource(rel, rawText);
    const QString         code = withoutDirectives(src.code);
    const QVector<Token>  t    = tokens(code);
    QVector<int>          lineOf(code.size() + 1);
    {
        int ln = 1;
        for (int k = 0; k < code.size(); ++k) { lineOf[k] = ln; if (code.at(k) == QLatin1Char('\n')) ++ln; }
        lineOf[code.size()] = ln;
    }
    QVector<Decl>  out;
    QVector<Frame> stack;
    int            paren = 0, bracket = 0;
    bool           broken = false;
    const int      nt = t.size();
    auto is = [&](int k, const char *s) { return k >= 0 && k < nt && t.at(k).text == QLatin1String(s); };

    // At namespace scope: every frame a namespace or transparent, no parenthesis or bracket open.
    auto nsScope = [&](bool *recordable, bool *detail, QString *ns) {
        QStringList parts;
        *recordable = true;
        *detail     = false;
        for (const Frame &f : stack) {
            if (f.scope == Scope::Other) return false;
            if (f.scope == Scope::Namespace) {
                if (f.parts.isEmpty()) *recordable = false;                   // anonymous: per file
                if (f.parts.contains(QLatin1String("detail"))) *detail = true;
                parts += f.parts;
            }
        }
        *ns = parts.join(QLatin1String("::"));
        return paren == 0 && bracket == 0;
    };

    // Skip a balanced <…> starting at k (t[k] == "<"); returns the index after the closing '>'.
    auto skipAngles = [&](int k) {
        int depth = 0, p = 0;
        for (; k < nt; ++k) {
            const QString &x = t.at(k).text;
            if (x == QLatin1String("(")) ++p;
            else if (x == QLatin1String(")")) --p;
            else if (p == 0 && x == QLatin1String("<")) ++depth;
            else if (p == 0 && x == QLatin1String(">") && --depth == 0) return k + 1;
            else if (p == 0 && (x == QLatin1String(";") || x == QLatin1String("{"))) return k;   // not a template list
        }
        return k;
    };
    // Skip a balanced (…) or [[…]] starting at k; returns the index after its close.
    auto skipGroup = [&](int k, const char *open, const char *close) {
        int depth = 0;
        for (; k < nt; ++k) {
            if (t.at(k).text == QLatin1String(open)) ++depth;
            else if (t.at(k).text == QLatin1String(close) && --depth == 0) return k + 1;
        }
        return k;
    };

    for (int i = 0; i < nt; ++i) {
        const QString &x = t.at(i).text;
        if (x == QLatin1String("(")) { ++paren; continue; }
        if (x == QLatin1String(")")) { if (--paren < 0) broken = true; continue; }
        if (x == QLatin1String("[")) { ++bracket; continue; }
        if (x == QLatin1String("]")) { if (--bracket < 0) broken = true; continue; }
        if (x == QLatin1String("{")) {
            Frame f;
            if (paren == 0 && bracket == 0) {
                // back over identifiers and '::' to the start of the run: `[inline] namespace a::b {`
                int k = i - 1;
                while (k >= 0 && (t.at(k).ident || t.at(k).text == QLatin1String("::"))) --k;
                int ns = -1;
                for (int j = k + 1; j < i; ++j)
                    if (t.at(j).text == QLatin1String("namespace")) { ns = j; break; }
                if (ns >= 0) {
                    f.scope = Scope::Namespace;
                    for (int j = ns + 1; j < i; ++j)
                        if (t.at(j).ident) f.parts << t.at(j).text;
                } else if (is(i - 1, "\"") && is(i - 2, "\"") && is(i - 3, "extern")) {
                    f.scope = Scope::Transparent;                              // extern "C" { … }
                }
            }
            stack.append(f);
            continue;
        }
        if (x == QLatin1String("}")) {
            if (stack.isEmpty()) broken = true;
            else stack.removeLast();
            continue;
        }
        if (!t.at(i).ident) continue;

        bool    recordable = false, detail = false;
        QString ns;
        if (x == QLatin1String("template")) {
            if (is(i + 1, "<") && nsScope(&recordable, &detail, &ns)) i = skipAngles(i + 1) - 1;   // its parameters
            continue;
        }
        const bool typeKw = x == QLatin1String("class") || x == QLatin1String("struct") || x == QLatin1String("enum");
        const bool usingKw = x == QLatin1String("using");
        if (!typeKw && !usingKw) continue;
        if (is(i - 1, "@") || is(i - 1, "friend") || is(i - 1, "enum")) continue;    // ObjC @class; `enum class` read at enum
        if (!nsScope(&recordable, &detail, &ns) || !recordable) continue;
        const int declLine = lineOf[t.at(i).offset];

        if (usingKw) {
            // using X = …;   (not `using namespace a;`, not `using a::b;`)
            if (i + 2 < nt && t.at(i + 1).ident && t.at(i + 1).text != QLatin1String("namespace") && is(i + 2, "="))
                out.append({ ns, t.at(i + 1).text, QStringLiteral("using"), true, detail, rel, declLine });
            continue;
        }
        QString kind = x;
        int     k    = i + 1;
        if (kind == QLatin1String("enum") && (is(k, "class") || is(k, "struct"))) ++k;
        // the head: identifiers (class macros, the name, `final`), attributes and macro arguments skipped
        QStringList idents;
        for (; k < nt;) {
            if (is(k, "[") && is(k + 1, "[")) { k = skipGroup(k, "[", "]"); continue; }
            if (is(k, "(") && !idents.isEmpty() && isSpecifierMacro(idents.last())) {          // alignas(…), MACRO(…)
                k = skipGroup(k, "(", ")");
                idents.removeLast();
                continue;
            }
            if (!t.at(k).ident) break;
            idents << t.at(k).text;
            ++k;
        }
        while (!idents.isEmpty() && (idents.last() == QLatin1String("final") || idents.last() == QLatin1String("sealed")))
            idents.removeLast();
        if (idents.isEmpty() || k >= nt) continue;                           // anonymous, or the file ended
        const QString &term = t.at(k).text;
        bool definition = false;
        if (term == QLatin1String("{")) {
            definition = true;
        } else if (term == QLatin1String(":")) {
            // a base clause, or an enum's underlying type: the declaration's next '{' or ';' decides
            int j = k + 1, p = 0;
            for (; j < nt; ++j) {
                const QString &y = t.at(j).text;
                if (y == QLatin1String("(")) ++p;
                else if (y == QLatin1String(")")) --p;
                else if (p == 0 && (y == QLatin1String("{") || y == QLatin1String(";"))) break;
            }
            if (j >= nt) continue;
            definition = t.at(j).text == QLatin1String("{");
            if (!definition && idents.size() != 1) continue;
        } else if (term == QLatin1String(";")) {
            if (idents.size() != 1) continue;                                // `struct stat st;`: a use, not a declaration
        } else {
            continue;                                                        // `::` qualified, `<` specialised, `*`, `(`, …
        }
        out.append({ ns, idents.last(), kind, definition, detail, rel, declLine });
    }
    if (broken || !stack.isEmpty() || paren != 0 || bracket != 0)
        problems->append(QStringLiteral("%1: its braces/parentheses do not balance with the preprocessor lines blanked "
                                        "(%2 scope%3 left open, parentheses %4, brackets %5)")
                             .arg(rel).arg(stack.size()).arg(stack.size() == 1 ? "" : "s").arg(paren).arg(bracket));
    return out;
}

QString keyOf(const Decl &d) { return (d.ns.isEmpty() ? QString() : d.ns) + QStringLiteral("::") + d.name; }

struct Collision {
    QString     key;
    QStringList sites;       // file:line, sorted
    bool        note = false;    // a `detail` repeat with no site in a header: printed, not counted (PR-42)
};

bool isHeader(const QString &file)
{
    const QString ext = QFileInfo(file).suffix();
    return ext == QLatin1String("h") || ext == QLatin1String("hpp") || ext == QLatin1String("hh") || ext == QLatin1String("hxx");
}

// (namespace, name) pairs defined in two or more different files.
QVector<Collision> collisions(const QVector<Decl> &decls)
{
    QMap<QString, QVector<Decl>> defs;
    for (const Decl &d : decls)
        if (d.definition) defs[keyOf(d)].append(d);
    QVector<Collision> out;
    for (auto it = defs.cbegin(); it != defs.cend(); ++it) {
        QSet<QString> files;
        for (const Decl &d : it.value()) files.insert(d.file);
        if (files.size() < 2) continue;
        Collision c;
        c.key = it.key();
        bool detail = false, header = false;
        for (const Decl &d : it.value()) {
            c.sites << QStringLiteral("%1:%2").arg(d.file).arg(d.line);
            detail = detail || d.detail;
            header = header || isHeader(d.file);
        }
        c.note = detail && !header;
        std::sort(c.sites.begin(), c.sites.end());
        out.append(c);
    }
    return out;
}

// ── Fixtures: every rule must still bite ───────────────────────────────────────────────────────
QVector<Decl> scanAll(const QVector<QPair<QString, QString>> &files, QStringList *problems)
{
    QVector<Decl> all;
    for (const auto &f : files) all += declarations(f.first, f.second, problems);
    return all;
}

int failing(const QVector<Collision> &cs)
{
    return int(std::count_if(cs.cbegin(), cs.cend(), [](const Collision &c) { return !c.note; }));
}

QStringList namesOf(const QVector<Decl> &ds)
{
    QStringList out;
    for (const Decl &d : ds) out << QStringLiteral("%1%2").arg(keyOf(d), d.definition ? QString() : QStringLiteral(";"));
    return out;
}

void selfTest()
{
    std::printf("=== the rules still catch what they are for ===\n");
    QStringList p;
    auto two = [&](const char *a, const char *b) {
        return collisions(scanAll({ { QStringLiteral("A/a.h"), QString::fromUtf8(a) }, { QStringLiteral("B/b.h"), QString::fromUtf8(b) } }, &p));
    };

    const auto planted = two("namespace pinpoint::analysis {\nstruct DagEdge { int a; };\n}\n",
                             "namespace pinpoint {\nnamespace analysis {\n\nstruct DagEdge {\n  QString s;\n};\n}\n}\n");
    check(failing(planted) == 1 && planted.first().key == QLatin1String("pinpoint::analysis::DagEdge")
              && planted.first().sites == QStringList{ QStringLiteral("A/a.h:2"), QStringLiteral("B/b.h:4") },
          QStringLiteral("a planted duplicate fails, the two namespace spellings read as one, both sites named (got: %1)")
              .arg(planted.isEmpty() ? QStringLiteral("none") : planted.first().key + QLatin1String(": ") + planted.first().sites.join(QLatin1String(", "))));
    check(failing(two("namespace p { class X; struct Y; enum class E : int; }\n",
                      "namespace p {\nclass X { };\nstruct Y : Base { };\nenum class E : int { a, b };\n}\n")) == 0,
          QStringLiteral("a forward declaration (class, struct, opaque enum) and its definition pass"));
    check(failing(two("namespace p { struct Y { }; }\n", "namespace q { struct Y { }; }\nnamespace p::r { struct Y { }; }\n")) == 0,
          QStringLiteral("one name in two namespaces passes"));
    check(failing(two("struct G { };\n", "struct G { int x; };\n")) == 1,
          QStringLiteral("the global namespace counts"));
    check(failing(two("extern \"C\" {\nstruct C1 { };\n}\n", "struct C1 { };\n")) == 1,
          QStringLiteral("extern \"C\" { } is transparent"));
    check(failing(two("namespace p { using T = int; }\n", "namespace p { using T = int; }\n")) == 1,
          QStringLiteral("an alias defined in two files fails, even an identical one"));
    const auto anon = two("namespace { struct Z { }; }\nnamespace p { namespace { struct Z2 { }; } }\nnamespace p::detail { struct D { }; }\n",
                          "namespace { struct Z { }; }\nnamespace p { namespace { struct Z2 { }; } }\nnamespace p { namespace detail { struct D { }; } }\n");
    check(failing(anon) == 1 && anon.size() == 1 && anon.first().key == QLatin1String("p::detail::D"),
          QStringLiteral("anonymous namespaces are not read; a detail repeat in two headers fails (PR-42)"));
    const auto detailCpp = collisions(scanAll({ { QStringLiteral("A/a.cpp"), QStringLiteral("namespace p::detail { struct D { }; }\n") },
                                                { QStringLiteral("B/b.mm"), QStringLiteral("namespace p { namespace detail { struct D { int x; }; } }\n") } }, &p));
    const auto detailMixed = collisions(scanAll({ { QStringLiteral("A/a.cpp"), QStringLiteral("namespace p::detail { struct D { }; }\n") },
                                                  { QStringLiteral("B/b.h"), QStringLiteral("namespace p::detail { struct D { int x; }; }\n") } }, &p));
    check(failing(detailCpp) == 0 && detailCpp.size() == 1 && detailCpp.first().note && failing(detailMixed) == 1,
          QStringLiteral("a detail repeat only in source files is a note; one with a header site fails (PR-42)"));
    check(failing(two("namespace p {\nstruct A { struct Inner { }; enum Kind { k }; };\ninline void f() { struct Local { }; }\n"
                      "auto g = [] { struct L2 { }; };\n}\n",
                      "namespace p { struct Inner { }; struct Local { }; struct L2 { }; enum Kind { j }; }\n")) == 0,
          QStringLiteral("nested, function-local and lambda-local types are not at namespace scope"));
    check(failing(two("namespace p { struct S { }; }\n",
                      "namespace p {\n// struct S { };\n/* struct S { }; */\nconst char *t = \"struct S { };\";\n"
                      "#define DECL struct S {\n#define LONG \\\n  struct S { };\n}\n")) == 0,
          QStringLiteral("comments, strings and preprocessor lines (with continuations) are blanked"));

    QStringList none;
    const auto read = namesOf(declarations(QStringLiteral("C/c.h"), QStringLiteral(
        "namespace p {\n"
        "template <class T, typename U = std::vector<int>, template <class> class C> struct W { };\n"
        "struct stat st;\nstruct tm *localNow();\nstruct Tm makeTm() { return {}; }\nvoid take(class Q *q, enum R r);\nusing namespace std;\nusing a::b;\n"
        "template <> struct hash<Foo> { };\nstruct A::B { };\ntemplate class Box<int>;\nenum { kOne = 1 };\nenum : int { kTwo };\n"
        "class PP_EXPORT M final : public QObject { };\nstruct alignas(16) [[nodiscard]] V { };\nenum E2 : uint8_t { x };\n"
        "QVector<struct H> hs;\nQ_DECLARE_METATYPE(p::M)\nenum class F;\n"
        "}\n@class NSFoo;\n@interface PPView : NSView {\n  int ivar;\n}\n@end\n"), &none));
    const QStringList want{ QStringLiteral("p::W"), QStringLiteral("p::M"), QStringLiteral("p::V"), QStringLiteral("p::E2"),
                            QStringLiteral("p::F;") };
    check(read == want && none.isEmpty(),
          QStringLiteral("reads a template, a class macro + final, attributes, an enum's type and an opaque enum; not template "
                         "parameters, uses (a return type too), using-directives, specialisations, qualified names, anonymous enums, ObjC (got: %1)")
              .arg(read.join(QLatin1String(", "))));

    QStringList bad;
    declarations(QStringLiteral("D/d.h"), QStringLiteral("#ifdef A\nnamespace x {\n#else\nnamespace y {\n#endif\n}\n"), &bad);
    check(bad.size() == 1 && bad.first().startsWith(QLatin1String("D/d.h:")),
          QStringLiteral("a file whose scopes do not balance is reported, not read silently"));
    check(p.isEmpty(), QStringLiteral("the fixtures themselves balance (%1)").arg(p.join(QLatin1String("; "))));
}

} // namespace

int main()
{
    selfTest();

    const QString root = QStringLiteral(PP_SRC_ROOT);
    if (!QDir(root).exists()) {
        check(false, QStringLiteral("the source tree is where the build says it is (%1)").arg(root));
        return 1;
    }
    std::printf("\n=== no two types of one name in one namespace (B1.20a) ===\n");

    static const QStringList skipParts{ QStringLiteral("tests"), QStringLiteral("Testing"),
                                        QStringLiteral("third_party"), QStringLiteral("build") };
    static const QStringList cppExt{ QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("h"),
                                     QStringLiteral("hpp"), QStringLiteral("mm") };
    QVector<Decl> all;
    QStringList   problems;
    int           files = 0;
    QDirIterator  it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel  = QDir(root).relativeFilePath(path);
        bool          skip = false;
        for (const QString &part : rel.split(QLatin1Char('/'))) skip = skip || skipParts.contains(part);
        if (skip || !cppExt.contains(QFileInfo(rel).suffix())) continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        ++files;
        all += declarations(rel, QString::fromUtf8(f.readAll()), &problems);
    }
    int defs = 0;
    for (const Decl &d : all) defs += d.definition ? 1 : 0;
    check(files > 500, QStringLiteral("the C++ sources were found and read (%1 files)").arg(files));
    check(defs > 1000, QStringLiteral("the namespace-scope types were read (%1 declarations, %2 definitions)").arg(all.size()).arg(defs));
    const bool anchor = std::any_of(all.cbegin(), all.cend(), [](const Decl &d) {
        return d.definition && keyOf(d) == QLatin1String("pinpoint::analysis::DagEdge")
            && d.file == QLatin1String("Diagnostics/engine/dag_layout.h");
    });
    check(anchor, QStringLiteral("the scan sees pinpoint::analysis::DagEdge defined in Diagnostics/engine/dag_layout.h"));

    std::sort(problems.begin(), problems.end());
    for (const QString &pr : problems) std::printf("      %s\n", qPrintable(pr));
    check(problems.isEmpty(), QStringLiteral("every file's scopes could be read (%1 could not)").arg(problems.size()));

    const auto cs = collisions(all);
    int        n  = 0;
    for (const Collision &c : cs) {
        if (c.note) continue;
        ++n;
        std::printf("      %s: %s\n", qPrintable(c.key), qPrintable(c.sites.join(QLatin1String(", "))));
    }
    check(n == 0, QStringLiteral("no (namespace, name) is defined in two files (%1 %2)").arg(n).arg(n == 1 ? "is" : "are"));
    for (const Collision &c : cs)
        if (c.note)
            std::printf("      note: %s: %s (a detail namespace in source files only: not counted, but not per file in C++)\n", qPrintable(c.key),
                        qPrintable(c.sites.join(QLatin1String(", "))));

    if (g_fail)
        std::printf("\n  Two types of one name in one namespace are an ODR violation the linker accepts (7 Oct 2026, DagEdge).\n"
                    "  Rename one, or move it into its own namespace; this test has no allow-list.\n");
    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
