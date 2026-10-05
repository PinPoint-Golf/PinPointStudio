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

// Session-setup lint — docs/design/session_wizard_refactor_design.md §7.5.
//
// Static rules over session setup (src/Gui/setup/), the calibration flows and their hosts.
// Like qml_reactivity_test, this reads the SOURCE: none of these defects shows up when the QML is
// loaded through a fake, because the fake is exactly what drifts.
//
//   W1  Timers and Connections under calibration/ and setup/ are gated on `active`, never on
//       `visible` / `currentStep`. Enforced since Stage 2 (kEnforceW1).
//   W5  Every member the QML uses on a context object is reachable from QML: a call needs a PUBLIC
//       Q_INVOKABLE or slot, a read needs a Q_PROPERTY, a signal or a public invokable. And no
//       Q_INVOKABLE sits in a non-public section anywhere in the device headers — moc registers it,
//       QML cannot call it, and nothing says so at runtime but a silent `undefined`.
//   W6  The test enum stand-ins under tests/setup/ match CameraInstance::Perspective and
//       SessionController::Type, and the real enums still hold the values the stand-ins were cut
//       from.
//
//   W7  The routines' clock (`pace`, and its `scale`) is overridden only under tests/: no
//       production QML assigns `pace.scale`, every production `pace:` binding is a
//       declaration or a forward of the host's `flow.pace`, and every literal `scale:` under
//       calibration/ and setup/ is 1.
//   W2  Every .qml under setup/pages/ has WizardPage as its root type.
//   W3  Every registry key has its page file, every page file is named by the registry, and every
//       key appears (as a string) in at least one tests/setup/tst_setup_*.qml.
//   W4  No page under setup/pages/ reads imuManager. or cameraManager. — hardware goes through
//       `ctx` (F12).
//   W10 The shell names no step (R1): no registry key appears in setup/ScreenSessionSetup.qml or
//       components/PpFlowIndicator.qml, as an identifier or as a string literal that IS the key.
//   W8  No `applies` / `gate` in setup/SetupSteps.qml mentions `sessionType` or `preset`: hardware
//       decides which steps run (§4.12).
//   W9  No QML outside tests/ uses the slot-letter API (the …ForSlot shims,
//       setPlacementForDevice, appSettings.imuPlacement), except the site in kW9Allowed
//       (PpDataViewer.qml:96, which labels old swings). Enforced (kEnforceW9); the shims were
//       deleted from ImuManager at Stage 5c.
// Each rule is one function in kRules.
//
//   cmake --build build/gui-tests --target session_setup_lint_test
//   ctest --test-dir build/gui-tests -R session_setup_lint --output-on-failure

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdio>

namespace {

// ── Switches ──────────────────────────────────────────────────────────────────────────────────

// W1 was a report until the routines grew an `active` property (design stage 2). Enforced since.
constexpr bool kEnforceW1 = true;

// W5 must have found real uses, or an empty walk (a moved file, a renamed object) would pass.
constexpr int kW5MinUses = 40;

// W9 is ENFORCED for QML since Stage 5b. The one site allowed: the review screen's old-swing
// labelling, which reads the derived, read-only imuPlacement view on purpose to label swings that
// carry no role (swing_data_source.cpp) and stays. The …ForSlot shims and setPlacementForDevice
// were deleted from ImuManager at Stage 5c, so W5 also fails any QML that still calls one. A
// whole-file entry is a path; a single site is path:line.
constexpr bool kEnforceW9 = true;
const char *const kW9Allowed[] = {
    "review/PpDataViewer.qml:96",
};

// appLog has no C++ class yet (design stage 1). The first of these that exists is used; while none
// does, appLog uses are listed as skipped, not checked.
const char *const kAppLogHeaderCandidates[] = {
    "app/app_log.h",
    "shell/app_log.h",
    "app/app_log_bridge.h",
};

// ── Reporting ─────────────────────────────────────────────────────────────────────────────────

int g_fail = 0;

void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}
void skip(const QString &label) { std::printf("  [SKIP] %s\n", qPrintable(label)); }
void info(const QString &label) { std::printf("  [INFO] %s\n", qPrintable(label)); }
void detail(const QString &line) { std::printf("      %s\n", qPrintable(line)); }

// ── Paths ─────────────────────────────────────────────────────────────────────────────────────

QString guiRoot() { return QDir::cleanPath(QStringLiteral(PP_QML_ROOT)); }
QString absPath(const QString &rel) { return guiRoot() + QLatin1Char('/') + rel; }
QString relPath(const QString &path) { return QDir(guiRoot()).relativeFilePath(path); }

bool readText(const QString &path, QString *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    *out = QString::fromUtf8(f.readAll());
    return true;
}

// Files under `relDir` matching `patterns`; empty when the directory does not exist.
QStringList filesUnder(const QString &relDir, const QStringList &patterns, bool recursive)
{
    QStringList out;
    const QString dir = absPath(relDir);
    if (!QFileInfo(dir).isDir()) return out;
    QDirIterator it(dir, patterns, QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) out << it.next();
    out.sort();
    return out;
}

// Offset → 1-based line number.
class LineIndex
{
public:
    explicit LineIndex(const QString &text)
    {
        m_starts.push_back(0);
        for (int i = 0; i < text.size(); ++i)
            if (text.at(i) == QLatin1Char('\n')) m_starts.push_back(i + 1);
    }
    int line(int offset) const
    {
        const auto it = std::upper_bound(m_starts.begin(), m_starts.end(), offset);
        return int(it - m_starts.begin());
    }

private:
    QVector<int> m_starts;
};

// ── Lexical stripping ─────────────────────────────────────────────────────────────────────────
//
// Comments and string literals are replaced by spaces, character for character, newlines kept —
// so offsets and line numbers in the stripped text are those of the file.

enum class Lang { Cpp, Qml };

bool isIdentChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'); }

// keepStrings: string literals are left as written (comments are still blanked) — for rules about
// what a literal says (W3's registry keys, W10's step names written as strings).
QString strip(const QString &in, Lang lang, bool keepStrings = false)
{
    QString   s = in;
    const int n = s.size();
    auto      blank = [&s](int i) {
        if (s.at(i) != QLatin1Char('\n')) s[i] = QLatin1Char(' ');
    };

    int i = 0;
    while (i < n) {
        const QChar c  = s.at(i);
        const QChar nx = i + 1 < n ? s.at(i + 1) : QChar();

        if (c == QLatin1Char('/') && nx == QLatin1Char('/')) {
            while (i < n && s.at(i) != QLatin1Char('\n')) blank(i++);
            continue;
        }
        if (c == QLatin1Char('/') && nx == QLatin1Char('*')) {
            blank(i);
            blank(i + 1);
            i += 2;
            while (i < n && !(s.at(i) == QLatin1Char('*') && i + 1 < n && s.at(i + 1) == QLatin1Char('/')))
                blank(i++);
            if (i < n) {
                blank(i);
                blank(i + 1);
                i += 2;
            }
            continue;
        }

        // C++ raw string: R"delim( … )delim"
        if (lang == Lang::Cpp && c == QLatin1Char('"') && i > 0 && s.at(i - 1) == QLatin1Char('R')
            && (i < 2 || !isIdentChar(s.at(i - 2))
                || QStringLiteral("8LuU").contains(s.at(i - 2)))) {
            const int open = s.indexOf(QLatin1Char('('), i + 1);
            if (open > 0) {
                const QString marker = QString(QLatin1Char(')')) + s.mid(i + 1, open - i - 1) + QString(QLatin1Char('"'));
                const int     close  = s.indexOf(marker, open + 1);
                const int     end    = close < 0 ? n : close + marker.size();
                while (i < end) blank(i++);
                continue;
            }
        }

        bool quote = c == QLatin1Char('"');
        if (c == QLatin1Char('\'')) {
            quote = true;
            if (lang == Lang::Cpp && i > 0 && s.at(i - 1).isLetterOrNumber()) {
                // 1'000'000 — a digit separator if the token it sits in starts with a digit.
                int b = i - 1;
                while (b > 0 && s.at(b - 1).isLetterOrNumber()) --b;
                if (s.at(b).isDigit()) quote = false;
            }
        }
        if (lang == Lang::Qml && c == QLatin1Char('`')) quote = true;   // template literal (${…} lost)

        if (quote && keepStrings) {
            const QChar q = c;
            ++i;
            while (i < n && s.at(i) != q) {
                if (q != QLatin1Char('`') && s.at(i) == QLatin1Char('\n')) break;
                if (s.at(i) == QLatin1Char('\\') && i + 1 < n) ++i;
                ++i;
            }
            if (i < n && s.at(i) == q) ++i;
            continue;
        }
        if (quote) {
            const QChar q = c;
            blank(i++);
            while (i < n && s.at(i) != q) {
                if (q != QLatin1Char('`') && s.at(i) == QLatin1Char('\n')) break;   // unterminated
                if (s.at(i) == QLatin1Char('\\') && i + 1 < n) blank(i++);
                blank(i++);
            }
            if (i < n && s.at(i) == q) blank(i++);
            continue;
        }
        ++i;
    }

    if (lang == Lang::Cpp) {
        // Preprocessor lines, with their backslash continuations.
        int  pos  = 0;
        bool cont = false;
        while (pos < n) {
            int eol = s.indexOf(QLatin1Char('\n'), pos);
            if (eol < 0) eol = n;
            int first = pos;
            while (first < eol && s.at(first).isSpace()) ++first;
            const bool directive = cont || (first < eol && s.at(first) == QLatin1Char('#'));
            if (directive) {
                int last = eol - 1;
                while (last >= pos && s.at(last).isSpace()) --last;
                cont = last >= pos && s.at(last) == QLatin1Char('\\');
                for (int k = pos; k < eol; ++k) s[k] = QLatin1Char(' ');
            } else {
                cont = false;
            }
            pos = eol + 1;
        }
    }
    return s;
}

// Replace every match of `re` in `s` by spaces (offsets kept).
void blankMatches(QString &s, const QRegularExpression &re)
{
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        for (int k = m.capturedStart(); k < m.capturedEnd(); ++k)
            if (s.at(k) != QLatin1Char('\n')) s[k] = QLatin1Char(' ');
    }
}

// ── C++ header model ──────────────────────────────────────────────────────────────────────────

enum class Access { Public, Protected, Private };
enum class Kind { Property, Signal, Slot, Invokable, Method };
enum class Section { Normal, Signals, Slots };

struct Member
{
    QString name;
    Kind    kind;
    Access  access;
    int     line;
};

struct ClassInfo
{
    QString         name;
    QString         file;   // relative to src/Gui
    QVector<Member> members;
};

QString accessName(Access a)
{
    switch (a) {
    case Access::Public:    return QStringLiteral("public");
    case Access::Protected: return QStringLiteral("protected");
    case Access::Private:   return QStringLiteral("private");
    }
    return QStringLiteral("?");
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Property:  return QStringLiteral("Q_PROPERTY");
    case Kind::Signal:    return QStringLiteral("signal");
    case Kind::Slot:      return QStringLiteral("slot");
    case Kind::Invokable: return QStringLiteral("Q_INVOKABLE");
    case Kind::Method:    return QStringLiteral("plain method (not invokable)");
    }
    return QStringLiteral("?");
}

// A deliberately small reader: it tracks braces, class bodies and access sections, and classifies
// each member declaration of a class body by its macros and its section. Function bodies, enum
// bodies and initialisers are skipped as opaque blocks.
class HeaderParser
{
public:
    QVector<ClassInfo> classes;

    bool parse(const QString &relFile)
    {
        QString raw;
        if (!readText(absPath(relFile), &raw)) return false;
        m_file = relFile;
        m_s    = strip(raw, Lang::Cpp);
        LineIndex lines(m_s);
        m_lines = &lines;

        struct Scope
        {
            enum T { Decl, Class, Block } t;
            int     cls;
            Access  access;
            Section section;
        };
        QVector<Scope> stack;
        stack.push_back({ Scope::Decl, -1, Access::Public, Section::Normal });

        static const QRegularExpression accessRe(QStringLiteral(
            R"((?:^|[^\w])(public|private|protected|signals|Q_SIGNALS)(?:\s+(slots|Q_SLOTS))?\s*$)"));
        static const QRegularExpression classHeadRe(QStringLiteral(
            R"(^\s*(?:typedef\s+)?(?:template\s*<[^{]*>\s*)?(class|struct|union)\b([^{]*)$)"));
        static const QRegularExpression scopeHeadRe(QStringLiteral(R"(^\s*(?:inline\s+)?(namespace|extern)\b)"));
        static const QRegularExpression macroRe(macroPattern());

        const int n         = m_s.size();
        int       declStart = 0;
        for (int i = 0; i < n; ++i) {
            const QChar c   = m_s.at(i);
            const Scope top = stack.last();

            if (top.t == Scope::Block) {
                if (c == QLatin1Char('{')) {
                    stack.push_back({ Scope::Block, -1, Access::Public, Section::Normal });
                } else if (c == QLatin1Char('}')) {
                    stack.pop_back();
                    if (stack.isEmpty()) stack.push_back({ Scope::Decl, -1, Access::Public, Section::Normal });
                    if (stack.last().t != Scope::Block) declStart = i + 1;
                }
                continue;
            }

            if (c == QLatin1Char(';')) {
                if (top.t == Scope::Class) classify(top.cls, top.access, top.section, declStart, i);
                declStart = i + 1;
                continue;
            }

            if (c == QLatin1Char(':') && top.t == Scope::Class) {
                const bool dbl = (i + 1 < n && m_s.at(i + 1) == QLatin1Char(':'))
                              || (i > 0 && m_s.at(i - 1) == QLatin1Char(':'));
                if (!dbl) {
                    const QString acc = m_s.mid(declStart, i - declStart);
                    const auto    m   = accessRe.match(acc);
                    if (m.hasMatch()) {
                        // Whatever precedes the keyword (a run of Q_PROPERTYs) belongs to the old section.
                        classify(top.cls, top.access, top.section, declStart, declStart + m.capturedStart(1));
                        const QString kw = m.captured(1);
                        Scope        &cur = stack.last();
                        if (kw == QLatin1String("signals") || kw == QLatin1String("Q_SIGNALS")) {
                            cur.access  = Access::Public;
                            cur.section = Section::Signals;
                        } else {
                            cur.access  = kw == QLatin1String("public")    ? Access::Public
                                        : kw == QLatin1String("protected") ? Access::Protected
                                                                           : Access::Private;
                            cur.section = m.captured(2).isEmpty() ? Section::Normal : Section::Slots;
                        }
                        declStart = i + 1;
                    }
                }
                continue;
            }

            if (c == QLatin1Char('{')) {
                QString head = m_s.mid(declStart, i - declStart);
                blankMatches(head, macroRe);
                const auto cm = classHeadRe.match(head);
                if (cm.hasMatch()) {
                    ClassInfo ci;
                    ci.name = className(cm.captured(2));
                    if (top.t == Scope::Class && top.cls >= 0)
                        ci.name = classes.at(top.cls).name + QStringLiteral("::") + ci.name;
                    ci.file = m_file;
                    classes.push_back(ci);
                    const bool isClass = cm.captured(1) == QLatin1String("class");
                    stack.push_back({ Scope::Class, int(classes.size()) - 1,
                                      isClass ? Access::Private : Access::Public, Section::Normal });
                } else if (top.t != Scope::Class && scopeHeadRe.match(head).hasMatch()) {
                    stack.push_back({ Scope::Decl, -1, Access::Public, Section::Normal });
                } else {
                    // An inline function body (or a default argument / initialiser): the part before
                    // the brace is still a declaration worth recording.
                    if (top.t == Scope::Class && head.contains(QLatin1Char('(')))
                        classify(top.cls, top.access, top.section, declStart, i);
                    stack.push_back({ Scope::Block, -1, Access::Public, Section::Normal });
                }
                declStart = i + 1;
                continue;
            }

            if (c == QLatin1Char('}')) {
                stack.pop_back();
                if (stack.isEmpty()) stack.push_back({ Scope::Decl, -1, Access::Public, Section::Normal });
                declStart = i + 1;
                continue;
            }
        }
        m_lines = nullptr;
        return true;
    }

private:
    QString          m_file;
    QString          m_s;
    const LineIndex *m_lines = nullptr;

    // Qt / QML macros (Q_*, QML_*, QT_*), with a parenthesised argument list when they have one.
    // Q_INVOKABLE never has one, so `Q_INVOKABLE QObject *f(` loses only the macro.
    static QString macroPattern()
    {
        return QStringLiteral(R"(\b(?:Q|QML|QT)_[A-Z0-9_]+\b(?:\s*\([^()]*\))?)");
    }

    static QString className(const QString &afterKeyword)
    {
        static const QRegularExpression singleColon(QStringLiteral(R"((?<!:):(?!:))"));
        QString     h   = afterKeyword;
        const auto  m   = singleColon.match(h);
        if (m.hasMatch()) h = h.left(m.capturedStart());
        QStringList tok = h.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
        tok.removeAll(QStringLiteral("final"));
        if (tok.isEmpty()) return QStringLiteral("<anonymous>");
        QString name = tok.last();
        name.remove(QRegularExpression(QStringLiteral(R"([^\w:])")));
        return name.isEmpty() ? QStringLiteral("<anonymous>") : name;
    }

    void classify(int cls, Access access, Section section, int start, int end)
    {
        if (cls < 0 || end <= start) return;
        static const QRegularExpression propRe(QStringLiteral(R"(\bQ_PROPERTY\s*\(([^()]*)\))"));
        static const QRegularExpression propNameRe(
            QStringLiteral(R"(([A-Za-z_]\w*)\s+(?:READ|MEMBER|BINDABLE)\b)"));
        static const QRegularExpression invRe(QStringLiteral(R"(\bQ_INVOKABLE\b)"));
        static const QRegularExpression slotRe(QStringLiteral(R"(\bQ_SLOT\b)"));
        static const QRegularExpression signalRe(QStringLiteral(R"(\bQ_SIGNAL\b)"));
        static const QRegularExpression macroRe(macroPattern());
        static const QRegularExpression templRe(QStringLiteral(R"(\btemplate\s*<[^;{]*?>)"));
        static const QRegularExpression skipRe(
            QStringLiteral(R"(^\s*(?:friend|using|typedef|enum|static_assert)\b)"));

        QVector<Member> &out = classes[cls].members;
        QString          d   = m_s.mid(start, end - start);

        auto pit = propRe.globalMatch(d);
        while (pit.hasNext()) {
            const auto pm = pit.next();
            const auto nm = propNameRe.match(pm.captured(1));
            if (nm.hasMatch())
                out.push_back({ nm.captured(1), Kind::Property, Access::Public,
                                m_lines->line(start + pm.capturedStart()) });
        }
        blankMatches(d, propRe);

        const auto im        = invRe.match(d);
        const bool invokable = im.hasMatch();
        const bool qslot     = slotRe.match(d).hasMatch();
        const bool qsignal   = signalRe.match(d).hasMatch();
        blankMatches(d, macroRe);
        blankMatches(d, templRe);

        if (d.trimmed().isEmpty() || skipRe.match(d).hasMatch()) return;

        const int paren = d.indexOf(QLatin1Char('('));
        if (paren < 0) return;                                      // a data member
        const int eq = d.indexOf(QLatin1Char('='));
        if (eq >= 0 && eq < paren) return;                          // `T x = f(…)`

        int e = paren - 1;
        while (e >= 0 && d.at(e).isSpace()) --e;
        int b = e;
        while (b >= 0 && (d.at(b).isLetterOrNumber() || d.at(b) == QLatin1Char('_') || d.at(b) == QLatin1Char('~')))
            --b;
        const QString name = d.mid(b + 1, e - b);
        if (name.isEmpty() || name == QLatin1String("operator") || name.at(0).isDigit()) return;

        Kind kind = Kind::Method;
        if (invokable)                                         kind = Kind::Invokable;
        else if (section == Section::Signals || qsignal)       kind = Kind::Signal;
        else if (section == Section::Slots || qslot)           kind = Kind::Slot;

        const int line = m_lines->line(start + (invokable ? im.capturedStart() : paren));
        out.push_back({ name, kind, kind == Kind::Signal ? Access::Public : access, line });
    }
};

// ── W1 — timers and connections gate on `active` ─────────────────────────────────────────────

struct GateRow
{
    QString where, type, id, prop, expr;
    bool    visible = false, currentStep = false, active = false;
};

// The extent of a QML property's value starting at `from`: to the end of its line, through any
// brackets it opens, and across lines that continue it with a leading or trailing operator.
QString qmlValueAt(const QString &s, int from, int limit)
{
    static const QRegularExpression trailingOp(QStringLiteral(R"((&&|\|\||[?:(+\-*/!,.=<>])\s*$)"));
    static const QRegularExpression leadingOp(QStringLiteral(R"(^\s*(&&|\|\||\?|:|\.|\+))"));
    int depth = 0;
    int i     = from;
    for (; i < limit; ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) {
            if (depth == 0) break;
            --depth;
        } else if (depth == 0 && c == QLatin1Char(';')) {
            break;
        } else if (depth == 0 && c == QLatin1Char('\n')) {
            const QString soFar = s.mid(from, i - from);
            int           eol   = s.indexOf(QLatin1Char('\n'), i + 1);
            if (eol < 0 || eol > limit) eol = limit;
            const QString next = s.mid(i + 1, eol - i - 1);
            if (!trailingOp.match(soFar).hasMatch() && !leadingOp.match(next).hasMatch()) break;
        }
    }
    return s.mid(from, i - from).simplified();
}

QVector<GateRow> gateRows(const QString &file)
{
    QVector<GateRow> rows;
    QString          raw;
    if (!readText(file, &raw)) return rows;
    const QString   s = strip(raw, Lang::Qml);
    const LineIndex lines(s);

    static const QRegularExpression blockRe(QStringLiteral(R"((?<![\w$.])(Timer|Connections)\s*\{)"));
    static const QRegularExpression propRe(QStringLiteral(R"((running|enabled|id)\s*:(?!:))"));
    static const QRegularExpression visRe(QStringLiteral(R"(\bvisible\b)"));
    static const QRegularExpression stepRe(QStringLiteral(R"(\bcurrentStep\b)"));
    static const QRegularExpression actRe(QStringLiteral(R"(\bactive\b)"));

    auto it = blockRe.globalMatch(s);
    while (it.hasNext()) {
        const auto m     = it.next();
        const int  open  = m.capturedEnd() - 1;
        int        depth = 0;
        int        close = s.size();
        for (int k = open; k < s.size(); ++k) {
            if (s.at(k) == QLatin1Char('{')) ++depth;
            else if (s.at(k) == QLatin1Char('}') && --depth == 0) {
                close = k;
                break;
            }
        }

        GateRow row;
        row.where = QStringLiteral("%1:%2").arg(relPath(file)).arg(lines.line(m.capturedStart()));
        row.type  = m.captured(1);
        const QString wanted = row.type == QLatin1String("Timer") ? QStringLiteral("running")
                                                                  : QStringLiteral("enabled");
        bool found = false;
        depth      = 0;
        for (int k = open; k < close; ++k) {
            const QChar c = s.at(k);
            if (c == QLatin1Char('{') || c == QLatin1Char('(') || c == QLatin1Char('[')) { ++depth; continue; }
            if (c == QLatin1Char('}') || c == QLatin1Char(')') || c == QLatin1Char(']')) { --depth; continue; }
            if (depth != 1 || !(c.isLetter() || c == QLatin1Char('_'))) continue;
            if (isIdentChar(s.at(k - 1)) || s.at(k - 1) == QLatin1Char('.')) continue;
            const auto pm = propRe.match(s, k, QRegularExpression::NormalMatch,
                                         QRegularExpression::AnchorAtOffsetMatchOption);
            if (!pm.hasMatch()) continue;
            const QString value = qmlValueAt(s, pm.capturedEnd(), close);
            if (pm.captured(1) == QLatin1String("id")) {
                row.id = value;
            } else if (pm.captured(1) == wanted && !found) {
                found    = true;
                row.prop = wanted;
                row.expr = value;
            }
            k = pm.capturedEnd() - 1;
        }
        if (!found) {
            row.prop = wanted;
            row.expr = QStringLiteral("none");
        } else {
            row.visible     = visRe.match(row.expr).hasMatch();
            row.currentStep = stepRe.match(row.expr).hasMatch();
            row.active      = actRe.match(row.expr).hasMatch();
        }
        if (row.id.isEmpty()) row.id = QStringLiteral("-");
        rows.push_back(row);
    }
    return rows;
}

void ruleW1()
{
    QStringList files = filesUnder(QStringLiteral("calibration"), { QStringLiteral("*.qml") }, true);
    const QStringList setup = filesUnder(QStringLiteral("setup"), { QStringLiteral("*.qml") }, true);
    if (setup.isEmpty()) skip(QStringLiteral("setup/ has no QML yet — only calibration/ is read"));
    files += setup;

    QVector<GateRow> rows;
    for (const QString &f : files) rows += gateRows(f);

    info(QStringLiteral("%1 Timer / Connections block(s) in %2 file(s)%3")
             .arg(rows.size())
             .arg(files.size())
             .arg(kEnforceW1 ? QString() : QStringLiteral(" — report only (kEnforceW1 = false)")));
    info(QStringLiteral("where | type | id | gate | visible | currentStep | active | expression"));
    QStringList offenders;
    for (const GateRow &r : rows) {
        const QString yes = QStringLiteral("Y"), no = QStringLiteral("-");
        const QString yn  = QStringLiteral("%1 | %2 | %3")
                               .arg(r.visible ? yes : no, r.currentStep ? yes : no, r.active ? yes : no);
        info(QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6").arg(r.where, r.type, r.id, r.prop, yn, r.expr));
        if (r.visible || r.currentStep || !r.active)
            offenders << QStringLiteral("%1: %2 %3 — %4: %5")
                             .arg(r.where, r.type, r.id, r.prop, r.expr);
    }
    if (kEnforceW1) {
        check(!rows.isEmpty(), QStringLiteral("W1 found Timer / Connections blocks to judge"));
        for (const QString &o : offenders) detail(o);
        check(offenders.isEmpty(),
              QStringLiteral("every Timer `running:` / Connections `enabled:` gates on `active`, "
                             "not `visible` or `currentStep`"));
    } else {
        info(QStringLiteral("%1 of %2 would fail W1 when enforced").arg(offenders.size()).arg(rows.size()));
    }
}

// ── W5 — the private-Q_INVOKABLE trap ─────────────────────────────────────────────────────────

struct ContextObject
{
    QString object;
    QString header;      // relative to src/Gui; empty = no header yet
    QString className;   // empty = every class in the header
};

QVector<ContextObject> contextObjects()
{
    QString appLogHeader;
    for (const char *cand : kAppLogHeaderCandidates)
        if (QFileInfo::exists(absPath(QString::fromLatin1(cand)))) {
            appLogHeader = QString::fromLatin1(cand);
            break;
        }
    return {
        { QStringLiteral("imuManager"),    QStringLiteral("imu/imu_manager.h"),        QStringLiteral("ImuManager") },
        { QStringLiteral("cameraManager"), QStringLiteral("cameras/camera_manager.h"), QStringLiteral("CameraManager") },
        { QStringLiteral("liveWrist"),     QStringLiteral("viz/live_wrist_angles.h"),  QStringLiteral("LiveWristAngles") },
        { QStringLiteral("launchMonitor"), QStringLiteral("launchmonitor/launch_monitor_controller.h"),
          QStringLiteral("LaunchMonitorController") },
        { QStringLiteral("appLog"),        appLogHeader,                               QString() },
        // setup/SetupContext.qml holds the managers under INJECTABLE names (so the tests can hand it
        // fakes without a child context). Same classes, same rule: a renamed handle must not be a
        // way round the private-Q_INVOKABLE check.
        { QStringLiteral("imuMgr"),        QStringLiteral("imu/imu_manager.h"),        QStringLiteral("ImuManager") },
        { QStringLiteral("cameraMgr"),     QStringLiteral("cameras/camera_manager.h"), QStringLiteral("CameraManager") },
        { QStringLiteral("launchMon"),     QStringLiteral("launchmonitor/launch_monitor_controller.h"),
          QStringLiteral("LaunchMonitorController") },
    };
}

// Device headers whose Q_INVOKABLEs are reached from QML through instanceFor() & co.
const char *const kExtraInvokableHeaders[] = {
    "imu/imu_instance.h",
    "imu/hm_instance.h",
};

struct Use
{
    QString file;
    int     line;
    QString object;
    QString name;
    bool    call;
};

QStringList w5QmlFiles(QStringList *missing)
{
    QStringList files;
    for (const char *f : { "imu/PpImuPanel.qml", "viz/ArmVizView.qml" }) {
        const QString p = absPath(QString::fromLatin1(f));
        if (QFileInfo::exists(p)) files << p;
        else                      *missing << QString::fromLatin1(f);
    }
    const QStringList calib = filesUnder(QStringLiteral("calibration"), { QStringLiteral("*.qml") }, false);
    if (calib.isEmpty()) *missing << QStringLiteral("calibration/*.qml");
    files += calib;
    files += filesUnder(QStringLiteral("setup"), { QStringLiteral("*.qml") }, true);
    return files;
}

QVector<Use> collectUses(const QStringList &files, const QStringList &objects)
{
    QVector<Use>             uses;
    const QRegularExpression re(QStringLiteral(R"((?<![\w$.])(%1)\s*\??\.\s*([A-Za-z_$][\w$]*))")
                                    .arg(objects.join(QLatin1Char('|'))));
    for (const QString &file : files) {
        QString raw;
        if (!readText(file, &raw)) continue;
        const QString   s = strip(raw, Lang::Qml);
        const LineIndex lines(s);
        auto            it = re.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            int        k = m.capturedEnd();
            while (k < s.size() && s.at(k).isSpace()) ++k;
            if (k + 1 < s.size() && s.at(k) == QLatin1Char('?') && s.at(k + 1) == QLatin1Char('.')) k += 2;
            const bool call = k < s.size() && s.at(k) == QLatin1Char('(');
            uses.push_back({ relPath(file), lines.line(m.capturedStart()), m.captured(1), m.captured(2), call });
        }
    }
    return uses;
}

// QObject's own QML-visible members; every context object inherits them.
QVector<Member> qobjectBuiltins()
{
    return {
        { QStringLiteral("objectName"),        Kind::Property, Access::Public, 0 },
        { QStringLiteral("objectNameChanged"), Kind::Signal,   Access::Public, 0 },
        { QStringLiteral("destroyed"),         Kind::Signal,   Access::Public, 0 },
        { QStringLiteral("deleteLater"),       Kind::Slot,     Access::Public, 0 },
    };
}

bool reachable(const Member &m, bool call)
{
    const bool publicCallable = m.access == Access::Public
                             && (m.kind == Kind::Invokable || m.kind == Kind::Slot || m.kind == Kind::Signal);
    if (call) return publicCallable;
    return m.kind == Kind::Property || publicCallable;
}

void ruleW5()
{
    QStringList       missingFiles;
    const QStringList files = w5QmlFiles(&missingFiles);
    for (const QString &f : missingFiles) detail(QStringLiteral("missing: %1").arg(f));
    check(missingFiles.isEmpty(), QStringLiteral("the setup / calibration / IMU panel QML is where W5 expects it"));
    if (filesUnder(QStringLiteral("setup"), { QStringLiteral("*.qml") }, true).isEmpty())
        skip(QStringLiteral("setup/ has no QML yet"));

    const QVector<ContextObject> objects = contextObjects();
    QStringList                  objectNames;
    for (const ContextObject &o : objects) objectNames << o.object;
    const QVector<Use> uses = collectUses(files, objectNames);

    check(uses.size() > kW5MinUses,
          QStringLiteral("W5 found a non-trivial number of context-object uses (%1 > %2) in %3 QML file(s)")
              .arg(uses.size())
              .arg(kW5MinUses)
              .arg(files.size()));

    for (const ContextObject &obj : objects) {
        QVector<Use> mine;
        for (const Use &u : uses)
            if (u.object == obj.object) mine.push_back(u);

        if (obj.header.isEmpty()) {
            QStringList looked;
            for (const char *cand : kAppLogHeaderCandidates) looked << QString::fromLatin1(cand);
            skip(QStringLiteral("%1: no header yet (looked for %2) — %3 QML use(s) not checked")
                     .arg(obj.object, looked.join(QStringLiteral(", ")))
                     .arg(mine.size()));
            continue;
        }

        HeaderParser hp;
        if (!hp.parse(obj.header)) {
            check(false, QStringLiteral("%1: header %2 is readable").arg(obj.object, obj.header));
            continue;
        }
        QVector<Member> members = qobjectBuiltins();
        bool            classFound = false;
        for (const ClassInfo &ci : hp.classes)
            if (obj.className.isEmpty() ? !ci.name.contains(QStringLiteral("::")) : ci.name == obj.className) {
                classFound = true;
                members += ci.members;
            }
        if (!classFound) {
            check(false, QStringLiteral("%1: class %2 found in %3").arg(obj.object, obj.className, obj.header));
            continue;
        }

        QStringList offenders;
        QStringList distinct;
        for (const Use &u : mine) {
            if (!distinct.contains(u.name)) distinct << u.name;
            QStringList seenAs;
            bool        ok = false;
            for (const Member &m : members) {
                if (m.name != u.name) continue;
                if (reachable(m, u.call)) {
                    ok = true;
                    break;
                }
                seenAs << QStringLiteral("%1 %2 (%3:%4)")
                              .arg(accessName(m.access), kindName(m.kind), obj.header)
                              .arg(m.line);
            }
            if (ok) continue;
            const QString what = u.call ? QStringLiteral("called — needs a public Q_INVOKABLE or slot")
                                        : QStringLiteral("read — needs a Q_PROPERTY, a signal or a public invokable");
            const QString why  = seenAs.isEmpty()
                                   ? QStringLiteral("MISSING from %1").arg(obj.className.isEmpty() ? obj.header : obj.className)
                                   : QStringLiteral("found only as: %1").arg(seenAs.join(QStringLiteral("; ")));
            offenders << QStringLiteral("%1:%2: %3.%4%5 %6; %7")
                             .arg(u.file)
                             .arg(u.line)
                             .arg(u.object, u.name, u.call ? QStringLiteral("()") : QString(), what, why);
        }
        for (const QString &o : offenders) detail(o);
        check(offenders.isEmpty(),
              QStringLiteral("every %1 member the QML uses is reachable from QML (%2 use(s), %3 name(s))")
                  .arg(obj.object)
                  .arg(mine.size())
                  .arg(distinct.size()));
    }

    // Independent of QML usage: a Q_INVOKABLE outside a public section is always a trap.
    QStringList headers;
    for (const ContextObject &o : objects)
        if (!o.header.isEmpty()) headers << o.header;
    for (const char *h : kExtraInvokableHeaders) headers << QString::fromLatin1(h);

    QStringList trapped;
    int         invokables = 0;
    bool        sawNonPublic = false;
    for (const QString &h : headers) {
        HeaderParser hp;
        if (!hp.parse(h)) {
            check(false, QStringLiteral("header %1 is readable").arg(h));
            continue;
        }
        for (const ClassInfo &ci : hp.classes)
            for (const Member &m : ci.members) {
                if (m.access != Access::Public) sawNonPublic = true;
                if (m.kind != Kind::Invokable) continue;
                ++invokables;
                if (m.access != Access::Public)
                    trapped << QStringLiteral("%1:%2: %3::%4 is Q_INVOKABLE in a %5 section — QML cannot call it")
                                   .arg(h)
                                   .arg(m.line)
                                   .arg(ci.name, m.name, accessName(m.access));
            }
    }
    // Canaries: a parser that saw no sections would pass the trap check vacuously.
    check(invokables > 20 && sawNonPublic,
          QStringLiteral("the header reader saw Q_INVOKABLEs (%1) and non-public sections").arg(invokables));
    for (const QString &t : trapped) detail(t);
    check(trapped.isEmpty(),
          QStringLiteral("no Q_INVOKABLE sits in a non-public section of %1").arg(headers.join(QStringLiteral(", "))));
}

// ── W6 — the enum stand-ins cannot drift ──────────────────────────────────────────────────────

using Enumerators = QVector<QPair<QString, int>>;

// Every `enum <head> { … }` in `stripped`; `ok` false when an enumerator's value is not a literal.
QVector<QPair<int, Enumerators>> findEnums(const QString &stripped, const QString &headPattern, bool *ok)
{
    QVector<QPair<int, Enumerators>> out;
    const QRegularExpression         re(QStringLiteral(R"(\benum\s+%1\s*(?::\s*[\w:]+\s*)?\{([^}]*)\})")
                                     .arg(headPattern));
    static const QRegularExpression  itemRe(QStringLiteral(R"(^([A-Za-z_]\w*)\s*(?:=\s*([-+]?)\s*(\d+))?$)"));
    auto                             it = re.globalMatch(stripped);
    while (it.hasNext()) {
        const auto  m = it.next();
        Enumerators e;
        int         next = 0;
        for (const QString &raw : m.captured(1).split(QLatin1Char(','))) {
            const QString item = raw.simplified();
            if (item.isEmpty()) continue;
            const auto im = itemRe.match(item);
            if (!im.hasMatch()) {
                *ok = false;
                e.push_back({ item, 0 });
                continue;
            }
            int value = next;
            if (!im.captured(3).isEmpty()) {
                value = im.captured(3).toInt();
                if (im.captured(2) == QLatin1String("-")) value = -value;
            }
            e.push_back({ im.captured(1), value });
            next = value + 1;
        }
        out.push_back({ m.capturedStart(), e });
    }
    return out;
}

QString describe(const Enumerators &e)
{
    QStringList parts;
    for (const auto &p : e) parts << QStringLiteral("%1=%2").arg(p.first).arg(p.second);
    return parts.join(QStringLiteral(", "));
}

void checkEnum(const QString &label, const QString &realHeader, const QString &cppHead,
               const QString &qmlHead, const Enumerators &expected)
{
    QString raw;
    if (!readText(absPath(realHeader), &raw)) {
        check(false, QStringLiteral("%1: %2 is readable").arg(label, realHeader));
        return;
    }
    bool       ok    = true;
    const auto found = findEnums(strip(raw, Lang::Cpp), cppHead, &ok);
    check(found.size() == 1 && ok,
          QStringLiteral("%1 is declared exactly once in %2, with literal values").arg(label, realHeader));
    if (found.isEmpty()) return;
    const Enumerators real = found.first().second;
    check(real == expected,
          QStringLiteral("%1 still reads {%2} (expected {%3}) — a change here must reach the stand-ins")
              .arg(label, describe(real), describe(expected)));

    const QStringList standIns = filesUnder(
        QStringLiteral("tests/setup"),
        { QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.cpp"), QStringLiteral("*.qml"),
          QStringLiteral("*.js"), QStringLiteral("*.mjs") },
        true);
    int compared = 0;
    for (const QString &f : standIns) {
        QString text;
        if (!readText(f, &text)) continue;
        const bool isQml = f.endsWith(QLatin1String(".qml"));
        const bool isJs  = isQml || f.endsWith(QLatin1String(".js")) || f.endsWith(QLatin1String(".mjs"));
        const QString stripped = strip(text, isJs ? Lang::Qml : Lang::Cpp);
        bool          okS      = true;
        const auto    hits     = findEnums(stripped, isQml ? qmlHead : cppHead, &okS);
        const LineIndex lines(stripped);
        for (const auto &h : hits) {
            ++compared;
            check(okS && h.second == real,
                  QStringLiteral("stand-in %1:%2 matches %3 {%4}%5")
                      .arg(relPath(f))
                      .arg(lines.line(h.first))
                      .arg(label, describe(real),
                           h.second == real ? QString() : QStringLiteral(" — it reads {%1}").arg(describe(h.second))));
        }
    }
    if (compared == 0)
        skip(QStringLiteral("%1: no stand-in under tests/setup/ yet").arg(label));
}

void ruleW6()
{
    checkEnum(QStringLiteral("CameraInstance::Perspective"), QStringLiteral("cameras/camera_instance.h"),
              QStringLiteral("Perspective"), QStringLiteral("Perspective"),
              { { QStringLiteral("None"), 0 }, { QStringLiteral("DownTheLine"), 1 }, { QStringLiteral("FaceOn"), 2 },
                { QStringLiteral("Other"), 3 }, { QStringLiteral("Impact"), 4 } });
    // QML has no `enum class`; a QML stand-in declares `enum Type`.
    checkEnum(QStringLiteral("SessionController::Type"), QStringLiteral("session/session_controller.h"),
              QStringLiteral("class\\s+Type"), QStringLiteral("Type"),
              { { QStringLiteral("None"), -1 }, { QStringLiteral("Swing"), 0 }, { QStringLiteral("Wrist"), 1 },
                { QStringLiteral("Grf"), 2 }, { QStringLiteral("Coach"), 3 } });
}

// ── W7 — the routines' clock is the app's, outside tests/ ─────────────────────────────────────

void ruleW7()
{
    const QStringList all = filesUnder(QString(), { QStringLiteral("*.qml"), QStringLiteral("*.js"),
                                                     QStringLiteral("*.mjs") }, true);
    QStringList production, tests;
    for (const QString &f : all)
        (relPath(f).startsWith(QLatin1String("tests/")) ? tests : production) << f;

    static const QRegularExpression scaleAssign(QStringLiteral(R"(\bpace\s*\.\s*scale\s*=(?!=))"));
    static const QRegularExpression paceBind(QStringLiteral(R"((?<![\w$.])pace\s*:(?!:))"));
    static const QRegularExpression scaleLiteral(QStringLiteral(R"((?<![\w$.])scale\s*:\s*([0-9][0-9.]*)\b)"));
    static const QRegularExpression forward(QStringLiteral(R"(^flow\.pace\b)"));

    QStringList offenders;
    int         paceBindings = 0;
    for (const QString &f : production) {
        QString raw;
        if (!readText(f, &raw)) continue;
        const QString   s = strip(raw, Lang::Qml);
        const LineIndex lines(s);
        const QString   rel = relPath(f);
        const bool      clockDir = rel.startsWith(QLatin1String("calibration/")) || rel.startsWith(QLatin1String("setup/"));

        auto a = scaleAssign.globalMatch(s);
        while (a.hasNext()) {
            const auto m = a.next();
            offenders << QStringLiteral("%1:%2: assigns pace.scale").arg(rel).arg(lines.line(m.capturedStart()));
        }
        auto b = paceBind.globalMatch(s);
        while (b.hasNext()) {
            const auto m = b.next();
            ++paceBindings;
            const int     lineStart = s.lastIndexOf(QLatin1Char('\n'), m.capturedStart()) + 1;
            const QString before    = s.mid(lineStart, m.capturedStart() - lineStart);
            const QString value     = qmlValueAt(s, m.capturedEnd(), s.size());
            const bool    declaration = before.contains(QRegularExpression(QStringLiteral(R"(\bproperty\s+var\s*$)")));
            if (declaration || forward.match(value).hasMatch()) continue;
            offenders << QStringLiteral("%1:%2: binds pace to `%3` — only a declaration or `flow.pace` "
                                        "is allowed outside tests/")
                             .arg(rel).arg(lines.line(m.capturedStart())).arg(value);
        }
        if (clockDir) {
            auto c = scaleLiteral.globalMatch(s);
            while (c.hasNext()) {
                const auto m = c.next();
                if (m.captured(1).toDouble() == 1.0) continue;
                offenders << QStringLiteral("%1:%2: scale: %3 (must be 1 outside tests/)")
                                 .arg(rel).arg(lines.line(m.capturedStart())).arg(m.captured(1));
            }
        }
    }
    int testOverrides = 0;
    for (const QString &f : tests) {
        QString raw;
        if (!readText(f, &raw)) continue;
        const QString s = strip(raw, Lang::Qml);
        testOverrides += int(s.count(QRegularExpression(QStringLiteral(R"(\bpace\s*=|\bscale\s*:)"))));
    }
    info(QStringLiteral("%1 production `pace:` binding(s) in %2 file(s); %3 pace/scale override site(s) under tests/")
             .arg(paceBindings).arg(production.size()).arg(testOverrides));
    check(paceBindings > 0, QStringLiteral("W7 found the routines' pace declarations to judge"));
    for (const QString &o : offenders) detail(o);
    check(offenders.isEmpty(), QStringLiteral("pace.scale is overridden only under tests/"));
}

// ── W9 — the slot-letter API is gone from QML (enforced at Stage 8) ─────────────────────────────

void ruleW9()
{
    // Code only: comments and strings are stripped, so a note explaining the old API is not a use.
    static const QRegularExpression re(QStringLiteral(
        R"((?<![\w$])(instanceForSlot|deviceForSlot|deviceIdForSlot|unitLabelForSlot|placementKeyForSlot|setPlacementForDevice|imuPlacement)(?![\w$]))"));

    const QStringList all = filesUnder(QString(), { QStringLiteral("*.qml") }, true);
    QStringList       sites, outsideAllowed;
    int               files = 0;
    for (const QString &f : all) {
        const QString rel = relPath(f);
        if (rel.startsWith(QLatin1String("tests/"))) continue;
        ++files;
        QString raw;
        if (!readText(f, &raw)) continue;
        const QString   s = strip(raw, Lang::Qml);
        const LineIndex lines(s);
        QVector<int>    seen;
        auto            it = re.globalMatch(s);
        while (it.hasNext()) {
            const auto m    = it.next();
            const int  line = lines.line(m.capturedStart());
            if (seen.contains(line)) continue;
            seen.push_back(line);
            const QString site = QStringLiteral("%1:%2").arg(rel).arg(line);
            sites << QStringLiteral("%1 (%2)").arg(site, m.captured(1));
            bool allowed = false;
            for (const char *a : kW9Allowed) {
                const QString entry = QString::fromLatin1(a);
                if (entry == rel || entry == site) allowed = true;
            }
            if (!allowed) outsideAllowed << site;
        }
    }
    info(QStringLiteral("%1 slot-letter use(s) in %2 production QML file(s)%3")
             .arg(sites.size()).arg(files)
             .arg(kEnforceW9 ? QStringLiteral(" — enforced outside the allow-list")
                             : QStringLiteral(" — report only (kEnforceW9 = false)")));
    for (const QString &l : sites) detail(l);
    check(files > 0, QStringLiteral("W9 found production QML to read"));
    for (const QString &o : outsideAllowed) detail(QStringLiteral("not allowed: %1").arg(o));
    if (kEnforceW9)
        check(outsideAllowed.isEmpty(),
              QStringLiteral("no production QML outside the allow-list (PpDataViewer.qml:96) "
                             "uses the slot-letter API"));
    else
        check(outsideAllowed.isEmpty(),
              QStringLiteral("only PpDataViewer.qml:96 still uses the slot-letter API"));
}

// ── W2 / W3 / W4 — the pages (Stage 5) ─────────────────────────────────────────────────────────

QStringList pageFiles() { return filesUnder(QStringLiteral("setup/pages"), { QStringLiteral("*.qml") }, false); }

// The root object type of a QML file: the first `Identifier {` after the imports and pragmas.
QString qmlRootType(const QString &file)
{
    QString raw;
    if (!readText(file, &raw)) return QString();
    const QString s = strip(raw, Lang::Qml);
    static const QRegularExpression rootRe(QStringLiteral(R"(^\s*([A-Z][\w.]*)\s*\{)"),
                                           QRegularExpression::MultilineOption);
    const auto m = rootRe.match(s);
    return m.hasMatch() ? m.captured(1) : QString();
}

void ruleW2()
{
    const QStringList pages = pageFiles();
    if (pages.isEmpty()) {
        skip(QStringLiteral("setup/pages/ has no QML yet (Stage 5)"));
        return;
    }
    QStringList offenders;
    for (const QString &f : pages) {
        const QString root = qmlRootType(f);
        if (root != QLatin1String("WizardPage")) offenders << QStringLiteral("%1: root is `%2`").arg(relPath(f), root);
    }
    for (const QString &o : offenders) detail(o);
    check(offenders.isEmpty(), QStringLiteral("every page under setup/pages/ (%1) has WizardPage as its root").arg(pages.size()));
}

// The registry's keys, from CODE only (the commented-out trunk entries do not count).
QStringList registryKeys()
{
    QString raw;
    if (!readText(absPath(QStringLiteral("setup/SetupSteps.qml")), &raw)) return {};
    const QString s = strip(raw, Lang::Qml, true);
    static const QRegularExpression keyRe(QStringLiteral(R"re((?<![\w$.])key\s*:\s*"(\w+)")re"));
    QStringList out;
    auto it = keyRe.globalMatch(s);
    while (it.hasNext()) out << it.next().captured(1);
    return out;
}

void ruleW3()
{
    const QStringList pages = pageFiles();
    if (pages.isEmpty()) {
        skip(QStringLiteral("setup/pages/ has no QML yet (Stage 5) — the registry's page names are not checked"));
        return;
    }
    QString raw;
    if (!readText(absPath(QStringLiteral("setup/SetupSteps.qml")), &raw)) {
        check(false, QStringLiteral("setup/SetupSteps.qml is readable"));
        return;
    }
    // Page names as written in the registry (strings survive here: they are the data). Code only:
    // the commented-out trunk entries name pages that exist only once the trunk work registers them.
    static const QRegularExpression pageRe(QStringLiteral(R"re(\bpage\s*:\s*"(pages/[^"]+\.qml)")re"));
    QStringList named;
    auto it = pageRe.globalMatch(strip(raw, Lang::Qml, true));
    while (it.hasNext()) named << it.next().captured(1);
    QStringList missing, unreferenced;
    for (const QString &n : named)
        if (!QFileInfo::exists(absPath(QStringLiteral("setup/") + n))) missing << n;
    for (const QString &f : pages) {
        const QString rel = QStringLiteral("pages/") + QFileInfo(f).fileName();
        if (!named.contains(rel)) unreferenced << rel;
    }
    for (const QString &m : missing) detail(QStringLiteral("named but missing: %1").arg(m));
    for (const QString &u : unreferenced) detail(QStringLiteral("not in the registry: %1").arg(u));
    check(missing.isEmpty() && unreferenced.isEmpty(),
          QStringLiteral("every registry page exists and every page file is registered (%1 named)").arg(named.size()));

    // Every key is exercised by at least one wizard-level or engine test file.
    const QStringList keys  = registryKeys();
    const QStringList tests = filesUnder(QStringLiteral("tests/setup"), { QStringLiteral("tst_setup_*.qml") }, false);
    check(keys.size() >= 8, QStringLiteral("W3 read the registry's keys (%1)").arg(keys.join(QStringLiteral(", "))));
    check(!tests.isEmpty(), QStringLiteral("W3 found tests/setup/tst_setup_*.qml"));
    QStringList untested;
    for (const QString &k : keys) {
        const QRegularExpression lit(QStringLiteral(R"re(["']%1["'])re").arg(QRegularExpression::escape(k)));
        bool hit = false;
        for (const QString &t : tests) {
            QString raw;
            if (readText(t, &raw) && lit.match(strip(raw, Lang::Qml, true)).hasMatch()) { hit = true; break; }
        }
        if (!hit) untested << k;
    }
    for (const QString &u : untested) detail(QStringLiteral("no tst_setup_*.qml names: %1").arg(u));
    check(untested.isEmpty(), QStringLiteral("every registry key appears in at least one tst_setup_*.qml"));
}

void ruleW4()
{
    const QStringList pages = pageFiles();
    if (pages.isEmpty()) {
        skip(QStringLiteral("setup/pages/ has no QML yet (Stage 5)"));
        return;
    }
    static const QRegularExpression re(QStringLiteral(R"((?<![\w$.])(imuManager|cameraManager)\s*\??\.)"));
    QStringList offenders;
    for (const QString &f : pages) {
        QString raw;
        if (!readText(f, &raw)) continue;
        const QString   s = strip(raw, Lang::Qml);
        const LineIndex lines(s);
        auto            it = re.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            offenders << QStringLiteral("%1:%2: %3").arg(relPath(f)).arg(lines.line(m.capturedStart())).arg(m.captured(1));
        }
    }
    for (const QString &o : offenders) detail(o);
    check(offenders.isEmpty(), QStringLiteral("no page reads imuManager. or cameraManager. directly (%1 page(s))").arg(pages.size()));
}

// ── W8 — no step's applicability reads the session type (§4.12) ─────────────────────────────────

void ruleW8()
{
    const QString file = absPath(QStringLiteral("setup/SetupSteps.qml"));
    QString       raw;
    if (!readText(file, &raw)) {
        skip(QStringLiteral("setup/SetupSteps.qml does not exist yet"));
        return;
    }
    // Code only: the commented-out trunk entries and every string are blanked.
    const QString   s = strip(raw, Lang::Qml);
    const LineIndex lines(s);
    static const QRegularExpression propRe(QStringLiteral(R"((?<![\w$.])(applies|gate)\s*:(?!:))"));
    static const QRegularExpression banned(QStringLiteral(R"(\b(sessionType|preset)\b)"));
    int         judged = 0;
    QStringList offenders;
    auto        it = propRe.globalMatch(s);
    while (it.hasNext()) {
        const auto    m     = it.next();
        const QString value = qmlValueAt(s, m.capturedEnd(), s.size());
        ++judged;
        const auto b = banned.match(value);
        if (b.hasMatch())
            offenders << QStringLiteral("SetupSteps.qml:%1: %2 mentions `%3`: %4")
                             .arg(lines.line(m.capturedStart())).arg(m.captured(1), b.captured(1), value);
    }
    info(QStringLiteral("%1 applies/gate binding(s) in setup/SetupSteps.qml").arg(judged));
    check(judged >= 4, QStringLiteral("W8 found the registry's applies/gate bindings to judge"));
    for (const QString &o : offenders) detail(o);
    check(offenders.isEmpty(), QStringLiteral("no applies/gate mentions sessionType or preset"));
}

// ── W10 — the shell names no step (R1) ────────────────────────────────────────────────────────────

void ruleW10()
{
    const QStringList keys = registryKeys();
    check(keys.size() >= 8, QStringLiteral("W10 read the registry's keys (%1)").arg(keys.size()));
    const char *const files[] = { "setup/ScreenSessionSetup.qml", "components/PpFlowIndicator.qml" };
    QStringList offenders;
    int         read = 0;
    for (const char *f : files) {
        QString raw;
        if (!readText(absPath(QString::fromLatin1(f)), &raw)) {
            detail(QStringLiteral("missing: %1").arg(QString::fromLatin1(f)));
            continue;
        }
        ++read;
        const QString   code = strip(raw, Lang::Qml);          // identifiers: strings and comments blanked
        const QString   lits = strip(raw, Lang::Qml, true);    // literals kept, comments blanked
        const LineIndex lines(code);
        for (const QString &k : keys) {
            const QString e = QRegularExpression::escape(k);
            const QRegularExpression ident(QStringLiteral(R"((?<![\w$])%1(?![\w$]))").arg(e));
            const QRegularExpression lit(QStringLiteral(R"re(["'`]%1["'`])re").arg(e));
            for (auto it = ident.globalMatch(code); it.hasNext();) {
                const auto m = it.next();
                offenders << QStringLiteral("%1:%2: identifier `%3`").arg(QString::fromLatin1(f)).arg(lines.line(m.capturedStart())).arg(k);
            }
            for (auto it = lit.globalMatch(lits); it.hasNext();) {
                const auto m = it.next();
                offenders << QStringLiteral("%1:%2: string \"%3\"").arg(QString::fromLatin1(f)).arg(lines.line(m.capturedStart())).arg(k);
            }
        }
    }
    check(read == 2, QStringLiteral("W10 read the shell and the indicator"));
    for (const QString &o : offenders) detail(o);
    check(offenders.isEmpty(), QStringLiteral("no registry key appears in the shell or the indicator"));
}

// ── The rule table ────────────────────────────────────────────────────────────────────────────

struct Rule
{
    const char *id;
    const char *title;
    void (*run)();
};

const Rule kRules[] = {
    { "W1", "Timers and Connections gate on `active`", ruleW1 },
    { "W2", "every setup page's root is WizardPage", ruleW2 },
    { "W3", "the registry and the page files agree", ruleW3 },
    { "W4", "pages reach hardware through ctx only", ruleW4 },
    { "W5", "QML reaches only public invokables, slots, signals and properties", ruleW5 },
    { "W6", "the enum stand-ins match the real enums", ruleW6 },
    { "W7", "the routines' pace is overridden only under tests/", ruleW7 },
    { "W8", "no step's applies/gate reads the session type or preset", ruleW8 },
    { "W9", "no QML outside the allow-list uses the slot-letter API", ruleW9 },
    { "W10", "the shell and the indicator name no step", ruleW10 },
};

} // namespace

int main()
{
    if (!QDir(guiRoot()).exists()) {
        std::printf("  [FAIL] the Gui tree is where the build says it is (%s)\n", qPrintable(guiRoot()));
        return 1;
    }
    for (const Rule &r : kRules) {
        std::printf("=== %s — %s ===\n", r.id, r.title);
        r.run();
        std::printf("\n");
    }
    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
