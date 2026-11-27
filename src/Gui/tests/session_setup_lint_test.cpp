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
//   W6  The test enum stand-ins under tests/setup/ match SessionController::Type, and the real
//       enum still holds the values the stand-in was cut from. The camera perspective has no
//       stand-in: the one perspective type (Vocabulary/Recording/perspective.h) still holds its
//       codes, and no file under tests/setup/ mirrors it again.
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
//   W11 TrunkRoutine.qml's mirrored gates (its still / upright / turn properties and the turn
//       durations in `defaultPace`) equal their constants in Vocabulary/Common/pp_tuned_constants.h
//       (tuned::trunkRoutine, tuned::calib), and WitmotionArmRoutine.qml's mount-gate literals
//       equal ImuInstance::kMountDeviationMaxDeg / kMountGravityErrorMaxDeg (imu/imu_instance.h)
//       — the QML cannot read the headers, so this is the guard.
//   W9  No QML outside tests/ uses the slot-letter API (the …ForSlot shims,
//       setPlacementForDevice, appSettings.imuPlacement), except the site in kW9Allowed
//       (PpDataViewer.qml:96, which labels old swings). Enforced (kEnforceW9); the shims were
//       deleted from ImuManager at Stage 5c.
//   W12 No rule is built in QML (AD-52, governance/controls/build.md; R-QML-4): a production QML or JS
//       file (every .qml and .js under src/Gui outside tests/) holds no decision. R-QML-4 has three
//       exceptions: the calibration screens (calibration/, #291), the Consistency grade words (#219,
//       the Design Lead's) and the frozen files on the list. Every file this check
//       counts rules in is frozen at its count in the exception list beside this file,
//       qml_rule_allowlist.txt (its owner is the Solution Architect), each line naming the issues that
//       empty it (`path | W12 | count | WHY  # #issue [#issue …]`; a calibration line names #291). A
//       count above its line fails; one below is reported so the line can be lowered; the list only
//       shrinks. A file with no line is allowed none.
//       WHAT COUNTS: a relational comparison (<, <=, >, >=; never part of <<, >>, >>>, => or a shift-
//       assign) in script (a function, a handler, a property binding, a .js file's top level) whose one
//       side is a constant (numbers and arithmetic only: 15.0, -900, 10 * 1024) or a named literal (a
//       `readonly property int|real|double|var NAME: <number>` or a `const NAME = <number>` in the same
//       file, read bare or as `id.NAME`), and whose other side is not a constant. The script is the
//       binding, handler or named function that holds the comparison; an anonymous function or a
//       plain block belongs to the script it sits in.
//       WHAT DOES NOT COUNT, in this order (each is reported as a total under its name):
//         layout binding   the script is a layout binding: width, height, x, y, z, opacity, spacing,
//                          radius, rotation, scale, implicit and content sizes, margins and padding,
//                          anchors.*, Layout.*, font sizes, border.width, rows, columns, cell sizes,
//                          sourceSize, the alignments; or any binding named …Width, …Height, …PixelSize,
//                          …FontSize, …IconSize, …Margin, …Padding, …Spacing, …Gap, …Radius, …Inset
//         layout script    a function or binding named lay, layout, geom, geometry, place, paginate,
//                          ticks, or …Layout, …Geom, …Geometry, …Place, …Scale, …Ticks
//         paint            a Canvas onPaint handler, or a function in the same file it reaches by calls
//         pointer          a pointer handler (onPressed, onReleased, onPositionChanged, onClicked,
//                          onDoubleClicked, onWheel, onPressAndHold, onEntered, onExited, onTapped, the
//                          drag and hover changes), or a function it reaches: hit-testing
//         selection        a selected / checked / current / currentIndex / highlighted binding: which
//                          option a control shows
//         sentinel         the constant is 0, 1 or -1: emptiness, sign and index tests
//         numerical guard  |constant| < 1e-3, or within 1e-2 of ±1 (a divide-by-zero or unit-vector
//                          guard: 1e-9, 0.9995)
//         loop bound       the comparison sits in a for (…) or while (…) condition on its line
//         unit boundary    1000, 1024, 3600, 10000, or 999 < constant / 1000^k < 1000: where a number's
//                          unit or rounding changes as it is written
//         size             the other side ends in .length or .count: a collection's size
//         index            every variable on the other side is i, j, k, index, idx, row, col, column
//         geometry         a name on the other side is a size or a position (…Width, …Height, …W, …H,
//                          width, height, x, y, z, W, H, em, px, py, cx, cy, dx, dy, mx, my, rx, ry, rw,
//                          rh, w, h, x0…y9, mouseX, mouseY, contentX, contentY, avail, room, zoom, scale,
//                          opacity): a comparison used only for layout
//       Equality (==, !=, ===, !==) is not counted: against a literal it is an enum, state or index check
//       (the routines mirror C++ enums as named ints). Math.* calls are not counted: measured on the
//       tree, after the exclusions above, about nine in ten outside the calibration files were drawing,
//       easing, nearest-sample search or float matching, not maths on a reading
//       (build/platform/E/NOTES-AD52.md).
//       KNOWN LIMITS: maths on a reading with no threshold in it (a smoothing filter, a slerp) is not
//       seen; a threshold held in a mutable property, read from another file, or compared with == is not
//       seen; a rule inside a function named like layout, or with a geometry-named operand, is not seen;
//       an `x > 0` sign test on a reading is not seen; a threshold inside paint code (a Canvas that colours
//       by a band) is not seen. A call is matched by name alone, so a handler calling `timer.start()`
//       also marks a same-file function named `start` as pointer code. A review still catches them
//       (AD-52's review).
//       Before the scan, planted cases run: each shape counts once and each exclusion keeps its case
//       out, so a scanner that stops matching fails rather than passing everything; and a planted list
//       checks the ratchet itself (a frozen file that gains a rule fails, one that loses one is
//       reported, a file with no line that gains one fails, a line with no issue is refused).
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

#include <QHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "Vocabulary/Common/pp_tuned_constants.h"

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
        // The trunk ceremony recorder (W2.2): the calibrate pages and TrunkRoutine hold it under the
        // context object's own name, so every member they use is checked here.
        { QStringLiteral("trunkCeremony"), QStringLiteral("imu/trunk_ceremony_recorder.h"),
          QStringLiteral("TrunkCeremonyRecorder") },
        // The app's voice (TtsController): TrunkRoutine holds it under the context object's own name
        // for its one EXPERIMENTAL spoken instruction (27 Oct 2026), so every member it uses is checked.
        { QStringLiteral("ttsController"), QStringLiteral("media/tts_controller.h"), QStringLiteral("TtsController") },
        // setup/SetupContext.qml holds the managers under INJECTABLE names (so the tests can hand it
        // fakes without a child context). Same classes, same rule: a renamed handle must not be a
        // way round the private-Q_INVOKABLE check.
        { QStringLiteral("imuMgr"),        QStringLiteral("imu/imu_manager.h"),        QStringLiteral("ImuManager") },
        { QStringLiteral("cameraMgr"),     QStringLiteral("cameras/camera_manager.h"), QStringLiteral("CameraManager") },
        { QStringLiteral("launchMon"),     QStringLiteral("launchmonitor/launch_monitor_controller.h"),
          QStringLiteral("LaunchMonitorController") },
        // The work-ons controller (the app's `workOns`), held by SetupContext as `workOnsSource` for
        // the Intent step's focus choices (phase 2 C5).
        { QStringLiteral("workOnsSource"), QStringLiteral("diagnostics/work_ons_controller.h"),
          QStringLiteral("WorkOnsController") },
        // The Rig (`app.rig`), held by SetupContext as `rig` since the cameras' cut-over (platform refactor C-5b):
        // each camera's fixed flag, saved box and enabled flag.
        { QStringLiteral("rig"),           QStringLiteral("app/rig_presenter.h"),      QStringLiteral("RigPresenter") },
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

// `mirrorsAllowed` false: the real enum is registered in every QML module, so a stand-in under
// tests/setup/ is itself the fault (a second declaration of the name, which can drift).
void checkEnum(const QString &label, const QString &realHeader, const QString &cppHead,
               const QString &qmlHead, const Enumerators &expected, bool mirrorsAllowed = true)
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
            if (!mirrorsAllowed) {
                check(false, QStringLiteral("no stand-in mirrors %1: %2:%3 declares it again — use the registered type")
                                 .arg(label, relPath(f))
                                 .arg(lines.line(h.first)));
                continue;
            }
            check(okS && h.second == real,
                  QStringLiteral("stand-in %1:%2 matches %3 {%4}%5")
                      .arg(relPath(f))
                      .arg(lines.line(h.first))
                      .arg(label, describe(real),
                           h.second == real ? QString() : QStringLiteral(" — it reads {%1}").arg(describe(h.second))));
        }
    }
    if (compared == 0) {
        if (mirrorsAllowed)
            skip(QStringLiteral("%1: no stand-in under tests/setup/ yet").arg(label));
        else
            check(true, QStringLiteral("no stand-in under tests/setup/ mirrors %1").arg(label));
    }
}

void ruleW6()
{
    // The one perspective type: QML names it Perspective.FaceOn in every module (perspective_qml.h), so the
    // set-up suite has no mirror of it; a C++ stand-in, scoped or not, or a QML `enum Perspective` fails.
    checkEnum(QStringLiteral("recording::Perspective"), QStringLiteral("../Vocabulary/Recording/perspective.h"),
              QStringLiteral("(?:class\\s+)?Perspective"), QStringLiteral("Perspective"),
              { { QStringLiteral("None"), 0 }, { QStringLiteral("DownTheLine"), 1 }, { QStringLiteral("FaceOn"), 2 },
                { QStringLiteral("Other"), 3 }, { QStringLiteral("Impact"), 4 } },
              /*mirrorsAllowed=*/false);
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

// ── W11 — the routines' mirrored gates equal their constants ───────────────────────────────────

void ruleW11()
{
    namespace tr = pinpoint::tuned::trunkRoutine;
    namespace tc = pinpoint::tuned::calib;
    const QString rel = QStringLiteral("calibration/TrunkRoutine.qml");
    QString raw;
    if (!readText(absPath(rel), &raw)) {
        check(false, QStringLiteral("W11 read %1").arg(rel));
        return;
    }
    const QString s = strip(raw, Lang::Qml);
    struct Mirror { const char *qml; double want; bool property; };
    const Mirror mirrors[] = {
        { "_stillDps",       tr::kStillGyroDps,              true  },
        { "_uprightMaxDeg",  tc::kTrunkLongAxisMaxFromUpDeg, true  },
        { "_turnStartDeg",   tr::kTurnStartDeg,              true  },
        { "_turnReturnDeg",  tr::kTurnReturnDeg,             true  },
        { "_turnBackCueDeg", tr::kTurnBackCueDeg,            true  },
        { "_armDriftFollowDps", tr::kArmDriftFollowDps,      true  },
        { "arm",             tr::kArmStillMs,                false },   // defaultPace
        { "returnStill",     tr::kReturnStillMs,             false },
        { "turnTimeout",     tr::kTurnTimeoutMs,             false },
    };
    for (const Mirror &m : mirrors) {
        const QString name = QString::fromLatin1(m.qml);
        const QRegularExpression re(m.property
            ? QStringLiteral(R"(readonly\s+property\s+real\s+%1\s*:\s*([0-9][0-9.]*))").arg(name)
            : QStringLiteral(R"((?<![\w$.])%1\s*:\s*([0-9][0-9.]*))").arg(name));
        QStringList found;
        auto it = re.globalMatch(s);
        while (it.hasNext()) found << it.next().captured(1);
        if (found.size() != 1) {
            check(false, QStringLiteral("W11 found %1 once in %2 (found %3)").arg(name, rel).arg(found.size()));
            continue;
        }
        const double got = found.first().toDouble();
        check(std::abs(got - m.want) < 1e-6,
              QStringLiteral("%1 %2 = %3 equals its constant %4").arg(rel, name, found.first()).arg(m.want));
    }

    // WitmotionArmRoutine.qml's mount gate (phase 2): `mountDeviationDeg <= N && mountGravityErrorDeg
    // <= M` restates ImuInstance::kMountDeviationMaxDeg / kMountGravityErrorMaxDeg. imu_instance.h is
    // a QObject header this Core-only test cannot include, so both sides are read as text: each
    // literal once in the QML, each constant once in the header, and equal.
    const QString wRel = QStringLiteral("calibration/WitmotionArmRoutine.qml");
    const QString hRel = QStringLiteral("imu/imu_instance.h");
    QString wRaw, hRaw;
    if (!readText(absPath(wRel), &wRaw) || !readText(absPath(hRel), &hRaw)) {
        check(false, QStringLiteral("W11 read %1 and %2").arg(wRel, hRel));
        return;
    }
    const QString w = strip(wRaw, Lang::Qml);
    const QString h = strip(hRaw, Lang::Cpp);
    struct Gate { const char *qml; const char *cpp; };
    const Gate gates[] = {
        { "mountDeviationDeg",    "kMountDeviationMaxDeg"    },
        { "mountGravityErrorDeg", "kMountGravityErrorMaxDeg" },
    };
    for (const Gate &g : gates) {
        const QString qmlName = QString::fromLatin1(g.qml), cppName = QString::fromLatin1(g.cpp);
        const auto all = [](const QString &text, const QRegularExpression &re) {
            QStringList out;
            auto it = re.globalMatch(text);
            while (it.hasNext()) out << it.next().captured(1);
            return out;
        };
        const QStringList inQml = all(w, QRegularExpression(
            QStringLiteral(R"((?<![\w$])%1\s*<=\s*([0-9][0-9.]*))").arg(qmlName)));
        const QStringList inCpp = all(h, QRegularExpression(
            QStringLiteral(R"(\b%1\s*=\s*([0-9][0-9.]*))").arg(cppName)));
        if (inQml.size() != 1 || inCpp.size() != 1) {
            check(false, QStringLiteral("W11 found `%1 <= N` once in %2 (found %3) and %4 once in %5 (found %6)")
                             .arg(qmlName, wRel).arg(inQml.size()).arg(cppName, hRel).arg(inCpp.size()));
            continue;
        }
        check(std::abs(inQml.first().toDouble() - inCpp.first().toDouble()) < 1e-6,
              QStringLiteral("%1 %2 <= %3 equals ImuInstance::%4 (%5)")
                  .arg(wRel, qmlName, inQml.first(), cppName, inCpp.first()));
    }
}

// ── W12 — no rule is built in QML (AD-52, R-QML-4) ─────────────────────────────────────────────
//
// The rule is written down in this file's header. The scanner reads the stripped text (comments and
// string literals blanked, offsets kept), finds the script that holds each relational comparison
// (the binding, handler or function it sits in), and keeps a comparison against a literal or a named
// literal unless one of the header's exclusions says it is not a decision.

namespace w12 {

const QString kId = QStringLiteral("[A-Za-z_$][\\w$]*");
const QString kNum = QStringLiteral("(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?");

struct Scope
{
    enum Kind { Obj, Group, Bind, Func, Anon, Js };
    int     open = 0, close = 0, parent = -1;
    Kind    kind = Js;
    QString name;
};

struct Owner
{
    enum Kind { None, Bind, Func, File };
    Kind    kind = None;
    QString name;
};

struct Hit
{
    int     line = 0;
    QString text;   // the comparison as written (strings blanked)
    QString why;    // empty: counted; else the exclusion that removed it
};

QString lastSegment(const QString &dotted) { return dotted.section(QLatin1Char('.'), -1); }

// The brace scopes of a file, each classified by the text between it and the previous statement.
QVector<Scope> scopesOf(const QString &c)
{
    static const QRegularExpression funcRe(QStringLiteral("\\bfunction\\b(?:\\s+(%1))?\\s*\\([^()]*\\)\\s*$").arg(kId));
    static const QRegularExpression arrowRe(QStringLiteral("=>\\s*$"));
    static const QRegularExpression bindRe(QStringLiteral("(?:^|[^\\w$.?])(%1(?:\\.%1)*)\\s*:\\s*$").arg(kId));
    static const QRegularExpression ternaryRe(QStringLiteral("\\?[^:]*:\\s*$"));
    static const QRegularExpression objRe(QStringLiteral("(?:^|[\\s:])(?:%1\\.)*[A-Z][\\w$]*\\s*$|\\bon\\s+%1(?:\\.%1)*\\s*$").arg(kId));
    static const QRegularExpression groupRe(QStringLiteral("^\\s*([a-z][\\w$]*)\\s*$"));
    static const QStringList keywords = { QStringLiteral("else"), QStringLiteral("try"), QStringLiteral("finally"),
                                          QStringLiteral("do"), QStringLiteral("return"), QStringLiteral("default") };
    QVector<Scope> out;
    QVector<int>   stack;
    for (int i = 0; i < c.size(); ++i) {
        const QChar ch = c.at(i);
        if (ch == QLatin1Char('{')) {
            int h = i - 1;
            while (h >= 0 && !QStringLiteral("\n{};").contains(c.at(h))) --h;
            const QString head = c.mid(h + 1, i - h - 1);
            Scope s;
            s.open   = i;
            s.close  = c.size();
            s.parent = stack.isEmpty() ? -1 : stack.last();
            const Scope::Kind pk = s.parent < 0 ? Scope::Obj : out.at(s.parent).kind;
            const bool qmlParent = s.parent < 0 || pk == Scope::Obj || pk == Scope::Group;
            QRegularExpressionMatch m;
            if ((m = funcRe.match(head)).hasMatch()) {
                s.kind = m.captured(1).isEmpty() ? Scope::Anon : Scope::Func;
                s.name = m.captured(1);
            } else if (arrowRe.match(head).hasMatch()) {
                s.kind = Scope::Anon;
            } else if (qmlParent && s.parent >= 0 && (m = bindRe.match(head)).hasMatch()
                       && !ternaryRe.match(head).hasMatch() && !keywords.contains(m.captured(1))) {
                s.kind = Scope::Bind;
                s.name = m.captured(1);
            } else if ((qmlParent || pk == Scope::Bind) && objRe.match(head).hasMatch()) {
                s.kind = Scope::Obj;
            } else if (qmlParent && s.parent >= 0 && (m = groupRe.match(head)).hasMatch()
                       && !keywords.contains(m.captured(1))) {
                s.kind = Scope::Group;
                s.name = m.captured(1);
            }
            out.append(s);
            stack.append(out.size() - 1);
        } else if (ch == QLatin1Char('}') && !stack.isEmpty()) {
            out[stack.takeLast()].close = i;
        }
    }
    return out;
}

class FileModel
{
public:
    FileModel(const QString &code, bool js) : c(code), m_js(js), sc(scopesOf(code))
    {
        for (int k = 0; k < sc.size(); ++k) {
            m_openOf.insert(sc.at(k).close, sc.at(k).open);
            if (sc.at(k).kind == Scope::Func)
                m_calls.append({ k, QRegularExpression(QStringLiteral("(?<![\\w$])%1\\s*\\(").arg(QRegularExpression::escape(sc.at(k).name))) });
        }
    }

    int innermost(int pos) const
    {
        int best = -1;
        for (int k = 0; k < sc.size(); ++k)
            if (sc.at(k).open < pos && pos < sc.at(k).close && (best < 0 || sc.at(k).open > sc.at(best).open)) best = k;
        return best;
    }

    // The binding, handler or function whose script holds `pos`. An anonymous function or a plain
    // block is transparent: its owner is the script it sits in. A statement directly in an object
    // body is owned by the binding that starts the statement.
    Owner ownerAt(int pos) const
    {
        static const QRegularExpression stmtRe(
            QStringLiteral("[ \\t]*(?:(?:readonly|required|default)[ \\t]+)*(?:property[ \\t]+[\\w$.<>]+[ \\t]+)?(%1(?:\\.%1)*)[ \\t]*:(?!:)")
                .arg(kId));
        int idx = innermost(pos), anchor = pos;
        while (true) {
            if (idx < 0) return m_js ? Owner{ Owner::File, QString() } : Owner{};
            const Scope &s = sc.at(idx);
            const QString prefix = s.parent >= 0 && sc.at(s.parent).kind == Scope::Group
                ? sc.at(s.parent).name + QLatin1Char('.') : QString();
            switch (s.kind) {
            case Scope::Func: return { Owner::Func, s.name };
            case Scope::Bind: return { Owner::Bind, prefix + s.name };
            case Scope::Anon:
            case Scope::Js:
                anchor = s.open;
                idx    = s.parent;
                continue;
            case Scope::Obj:
            case Scope::Group: {
                const QString group = s.kind == Scope::Group ? s.name + QLatin1Char('.') : QString();
                auto tryAt = [&](int at, Owner *o) {
                    const auto m = stmtRe.match(c, at, QRegularExpression::NormalMatch,
                                                QRegularExpression::AnchorAtOffsetMatchOption);
                    if (!m.hasMatch()) return false;
                    *o = { Owner::Bind, group + m.captured(1) };
                    return true;
                };
                Owner o;
                for (int j = anchor - 1; j > s.open; --j) {
                    const QChar ch = c.at(j);
                    if (ch == QLatin1Char('}') && m_openOf.contains(j)) { j = m_openOf.value(j); continue; }
                    if ((ch == QLatin1Char('\n') || ch == QLatin1Char(';')) && tryAt(j + 1, &o)) return o;
                }
                if (tryAt(s.open + 1, &o)) return o;
                return {};
            }
            }
        }
    }

    // The named functions reachable, by a call `name(`, from the handlers `isRoot` names.
    QSet<QString> reachableFrom(const std::function<bool(const QString &)> &isRoot) const
    {
        static const QRegularExpression lineHandlerRe(QStringLiteral("^[ \\t]*(on[A-Z][\\w$]*)[ \\t]*:(?![ \\t]*\\{)"),
                                                      QRegularExpression::MultilineOption);
        QVector<QPair<int, int>> todo;
        for (const Scope &s : sc) {
            if (s.kind == Scope::Bind && isRoot(lastSegment(s.name))) todo.append({ s.open, s.close });
            if (s.kind == Scope::Anon) {
                const Owner o = ownerAt(s.open);
                if (o.kind == Owner::Bind && isRoot(lastSegment(o.name))) todo.append({ s.open, s.close });
            }
        }
        auto it = lineHandlerRe.globalMatch(c);
        while (it.hasNext()) {
            const auto m = it.next();
            if (!isRoot(m.captured(1))) continue;
            const int eol = c.indexOf(QLatin1Char('\n'), m.capturedEnd());
            todo.append({ m.capturedStart(), eol < 0 ? int(c.size()) : eol });
        }
        QSet<QString> seen;
        while (!todo.isEmpty()) {
            const auto span = todo.takeLast();
            const QString body = c.mid(span.first, span.second - span.first);
            for (const auto &f : m_calls) {
                if (seen.contains(sc.at(f.first).name) || !f.second.match(body).hasMatch()) continue;
                seen.insert(sc.at(f.first).name);
                todo.append({ sc.at(f.first).open, sc.at(f.first).close });
            }
        }
        return seen;
    }

    const QString &c;

private:
    bool           m_js;
    QVector<Scope> sc;
    QHash<int, int> m_openOf;
    QVector<QPair<int, QRegularExpression>> m_calls;   // each named function, and the call that reaches it
};

// The operand on one side of an operator: out to the nearest , ; ? : = ! { } newline, && or ||, or an
// unmatched bracket, at this bracket depth.
QString operandLeft(const QString &c, int end)
{
    int i = end, d = 0;
    while (i > 0) {
        const QChar ch = c.at(i - 1);
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) ++d;
        else if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { if (d == 0) break; --d; }
        else if (d == 0 && (QStringLiteral(",;?:=!{}\n").contains(ch)
                            || (i >= 2 && (c.mid(i - 2, 2) == QLatin1String("&&") || c.mid(i - 2, 2) == QLatin1String("||")))))
            break;
        --i;
    }
    static const QRegularExpression kw(QStringLiteral("^\\s*(?:return|typeof|case|else)\\b"));
    QString s = c.mid(i, end - i);
    s.remove(kw);
    return s.trimmed();
}

QString operandRight(const QString &c, int start)
{
    int j = start, d = 0;
    while (j < c.size()) {
        const QChar ch = c.at(j);
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) ++d;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (d == 0) break; --d; }
        else if (d == 0 && (QStringLiteral(",;?:=!{}\n").contains(ch) || c.mid(j, 2) == QLatin1String("&&")
                            || c.mid(j, 2) == QLatin1String("||")))
            break;
        ++j;
    }
    return c.mid(start, j - start).trimmed();
}

// A constant: numbers and arithmetic only. Its value is its first number, with a leading sign.
bool constantValue(const QString &s, double *v)
{
    static const QRegularExpression num(kNum);
    static const QRegularExpression rest(QStringLiteral("^[\\s+\\-*/%()]*$"));
    QString r = s;
    r.replace(num, QStringLiteral(" "));
    if (!rest.match(r).hasMatch()) return false;
    const auto m = num.match(s);
    if (!m.hasMatch()) return false;
    *v = m.captured(0).toDouble();
    if (s.trimmed().startsWith(QLatin1Char('-'))) *v = -*v;
    return true;
}

// Identifiers in an expression, last segment of a dotted name; calls (`f(`) left out on request.
QStringList identsOf(const QString &e, bool skipCalls)
{
    static const QRegularExpression re(QStringLiteral("(?<![\\w$.])(?:%1\\.)*(%1)(\\s*\\()?").arg(kId));
    QStringList out;
    auto it = re.globalMatch(e);
    while (it.hasNext()) {
        const auto m = it.next();
        if (skipCalls && !m.captured(2).isEmpty()) continue;
        out << m.captured(1);
    }
    return out;
}

bool layoutBinding(const QString &name)
{
    static const QRegularExpression exact(QStringLiteral(
        "^(?:width|height|x|y|z|opacity|spacing|rowSpacing|columnSpacing|radius|rotation|scale|implicitWidth|implicitHeight"
        "|contentWidth|contentHeight|contentX|contentY|margins|padding|(?:left|right|top|bottom|horizontal|vertical)(?:Margin|Padding|Inset)"
        "|anchors\\..*|Layout\\..*|font\\.(?:pixelSize|pointSize|letterSpacing|wordSpacing)|lineHeight|border\\.width"
        "|columns|rows|cellWidth|cellHeight|sourceSize(?:\\..*)?|horizontalAlignment|verticalAlignment)$"));
    static const QRegularExpression suffix(QStringLiteral("(?:Width|Height|PixelSize|FontSize|IconSize|Margin|Padding|Spacing|Gap|Radius|Inset)$"));
    return exact.match(name).hasMatch() || suffix.match(lastSegment(name)).hasMatch();
}

bool layoutScript(const QString &name)   // a function or binding whose name says it lays out
{
    static const QRegularExpression re(QStringLiteral(
        "^_*(?:lay|layout|geom|geometry|place|paginate|ticks)$|(?:Layout|Geom|Geometry|Place|Scale|Ticks)$"));
    return re.match(lastSegment(name)).hasMatch();
}

bool pointerHandler(const QString &n)
{
    static const QRegularExpression re(QStringLiteral(
        "^on(?:Pressed|Released|PositionChanged|Clicked|DoubleClicked|Wheel|PressAndHold|Entered|Exited|Tapped"
        "|TranslationChanged|CentroidChanged|PointChanged|ActiveChanged|HoveredChanged)$"));
    return re.match(n).hasMatch();
}

bool selectionBinding(const QString &n)
{
    static const QRegularExpression re(QStringLiteral("^(?:selected|checked|current|currentIndex|highlighted|isCurrent|isSelected)$"));
    return re.match(n).hasMatch();
}

bool geometryIdent(const QString &id)
{
    static const QRegularExpression re(QStringLiteral(
        "^_*(?:[A-Za-z][\\w$]*(?:Width|Height|W|H)|width|height|x|y|z|W|H|em|px|py|cx|cy|dx|dy|mx|my|rx|ry|rw|rh|w|h"
        "|[xy][0-9]|mouseX|mouseY|contentX|contentY|avail|room|zoom|userZoom|scale|opacity)$"));
    return re.match(id).hasMatch();
}

bool indexIdent(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^_*(?:i|j|k|index|idx|row|col|column)$"));
    return re.match(id).hasMatch();
}

bool unitBoundary(double v)
{
    const double a = std::abs(v);
    if (a == 1000 || a == 1024 || a == 3600 || a == 10000) return true;
    if (a < 999) return false;
    double r = a;
    while (r >= 1000) r /= 1000;
    return r > 999 && r < 1000;
}

bool inLoopHead(const QString &c, int pos)
{
    static const QRegularExpression re(QStringLiteral("\\b(?:for|while)\\s*\\("));
    const int ls = c.lastIndexOf(QLatin1Char('\n'), pos - 1) + 1;
    const QString head = c.mid(ls, pos - ls);
    int at = -1;
    auto it = re.globalMatch(head);
    while (it.hasNext()) at = it.next().capturedEnd();
    if (at < 0) return false;
    int d = 1;
    for (int k = at; k < head.size(); ++k) {
        if (head.at(k) == QLatin1Char('(')) ++d;
        else if (head.at(k) == QLatin1Char(')') && --d == 0) return false;
    }
    return true;
}

// The literals a file names: `readonly property <int|real|double|var> NAME: <number>` and
// `const NAME = <number>`.
QHash<QString, double> namedLiterals(const QString &c)
{
    static const QRegularExpression prop(QStringLiteral(
        "\\breadonly\\s+property\\s+(?:real|int|double|var)\\s+(%1)\\s*:\\s*(-?%2)[ \\t]*(?=[;}\\n]|$)").arg(kId, kNum));
    static const QRegularExpression cnst(QStringLiteral("\\bconst\\s+(%1)\\s*=\\s*(-?%2)[ \\t]*(?=[;}\\n]|$)").arg(kId, kNum));
    QHash<QString, double> out;
    for (const QRegularExpression *re : { &prop, &cnst }) {
        auto it = re->globalMatch(c);
        while (it.hasNext()) {
            const auto m = it.next();
            out.insert(m.captured(1), m.captured(2).toDouble());
        }
    }
    return out;
}

// Every relational comparison in a file's script, each counted or excluded with its reason.
QVector<Hit> scan(const QString &rel, const QString &raw)
{
    static const QRegularExpression relRe(QStringLiteral("(?<![<>=!])(<=|>=|<|>)(?![<>=])"));
    static const QRegularExpression sizeRe(QStringLiteral("\\.(?:length|count)\\s*$"));
    const bool    js = rel.endsWith(QLatin1String(".js"));
    const QString c  = strip(raw, Lang::Qml);
    const LineIndex lines(c);
    const FileModel fm(c, js);
    const QSet<QString> paint   = fm.reachableFrom([](const QString &n) { return n == QLatin1String("onPaint"); });
    const QSet<QString> pointer = fm.reachableFrom(pointerHandler);
    const QHash<QString, double> named = namedLiterals(c);
    const QRegularExpression namedRe(QStringLiteral("^(?:%1\\.)*(%1)$").arg(kId));
    auto namedValue = [&](const QString &s, double *v) {
        const auto m = namedRe.match(s);
        if (!m.hasMatch() || !named.contains(m.captured(1))) return false;
        *v = named.value(m.captured(1));
        return true;
    };

    QVector<Hit> out;
    auto it = relRe.globalMatch(c);
    while (it.hasNext()) {
        const auto m  = it.next();
        const QString L = operandLeft(c, m.capturedStart());
        const QString R = operandRight(c, m.capturedEnd());
        double  v = 0;
        QString other;
        if (constantValue(R, &v) || namedValue(R, &v)) other = L;
        else if (constantValue(L, &v) || namedValue(L, &v)) other = R;
        else continue;   // no literal on either side: not this rule's shape
        double ignored = 0;
        if (other.isEmpty() || constantValue(other, &ignored)) continue;   // two constants: not a decision

        Hit h;
        h.line = lines.line(m.capturedStart());
        h.text = (L + QLatin1Char(' ') + m.captured(1) + QLatin1Char(' ') + R).simplified();
        const Owner o = fm.ownerAt(m.capturedStart());
        const QString shortName = lastSegment(o.name);
        const QStringList ids = identsOf(other, false), vars = identsOf(other, true);
        if (o.kind == Owner::None) h.why = QStringLiteral("not script");
        else if (o.kind == Owner::Bind && layoutBinding(o.name)) h.why = QStringLiteral("layout binding");
        else if (o.kind != Owner::File && layoutScript(o.name)) h.why = QStringLiteral("layout script");
        else if ((o.kind == Owner::Bind && shortName == QLatin1String("onPaint")) || (o.kind == Owner::Func && paint.contains(o.name)))
            h.why = QStringLiteral("paint");
        else if ((o.kind == Owner::Bind && pointerHandler(shortName)) || (o.kind == Owner::Func && pointer.contains(o.name)))
            h.why = QStringLiteral("pointer");
        else if (o.kind == Owner::Bind && selectionBinding(shortName)) h.why = QStringLiteral("selection");
        else if (v == 0 || v == 1 || v == -1) h.why = QStringLiteral("sentinel");
        else if ((std::abs(v) < 1e-3) || std::abs(std::abs(v) - 1) < 1e-2) h.why = QStringLiteral("numerical guard");
        else if (inLoopHead(c, m.capturedStart())) h.why = QStringLiteral("loop bound");
        else if (unitBoundary(v)) h.why = QStringLiteral("unit boundary");
        else if (sizeRe.match(other).hasMatch()) h.why = QStringLiteral("size");
        else if (!vars.isEmpty() && std::all_of(vars.cbegin(), vars.cend(), indexIdent)) h.why = QStringLiteral("index");
        else if (std::any_of(ids.cbegin(), ids.cend(), geometryIdent)) h.why = QStringLiteral("geometry");
        out.append(h);
    }
    return out;
}

int counted(const QVector<Hit> &hits)
{
    return int(std::count_if(hits.cbegin(), hits.cend(), [](const Hit &h) { return h.why.isEmpty(); }));
}

// The exception list (qml_rule_allowlist.txt): one line per file, `path | W12 | count | WHY  # #issue`,
// or `# #a #b` when the file's count spreads over more than one issue. It freezes every file the check
// counted rules in on 26 Nov 2026 (R-QML-4's exceptions: the calibration screens, #291; the Consistency
// grade words, #219; the other files); every line names the issues that empty it.
const char kCalibrationDir[] = "calibration/";
const int  kCalibrationIssue = 291;

struct Allowed
{
    int        count = 0;
    int        line  = 0;
    QList<int> issues;
};

// Parses the list. A line that is not `path | W12 | count (> 0) | WHY  # #issue [#issue …]`, once per
// file, goes to `bad`; so does a calibration line that does not name #291, and (when `mustExist`) a file
// that is gone.
QHash<QString, Allowed> parseAllow(const QString &text, bool mustExist, QStringList *bad)
{
    static const QRegularExpression tagRe(QStringLiteral("^(.*\\S)\\s+#\\s*((?:#\\d+\\s*)+)$"));
    static const QRegularExpression numRe(QStringLiteral("#(\\d+)"));
    QHash<QString, Allowed> allow;
    const QStringList ls = text.split(QLatin1Char('\n'));
    for (int k = 0; k < ls.size(); ++k) {
        const QString l = ls.at(k).trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#'))) continue;
        const QRegularExpressionMatch tm = tagRe.match(l);
        const QStringList parts = tm.hasMatch() ? tm.captured(1).split(QLatin1Char('|')) : QStringList();
        bool okCount = false;
        const int n = parts.size() == 4 ? parts.at(2).trimmed().toInt(&okCount) : 0;
        QList<int> issues;
        const QString tags = tm.captured(2);
        for (auto it = numRe.globalMatch(tags); it.hasNext();) {
            const int i = it.next().captured(1).toInt();
            if (i > 0 && !issues.contains(i)) issues << i;
        }
        const QString file = parts.value(0).trimmed();
        if (!(parts.size() == 4 && parts.at(1).trimmed() == QLatin1String("W12") && okCount && n > 0
              && !parts.at(3).trimmed().isEmpty() && !issues.isEmpty() && !allow.contains(file)))
            *bad << QStringLiteral("line %1 is not path | W12 | count (> 0) | WHY  # #issue, once per file").arg(k + 1);
        else if (file.startsWith(QLatin1String(kCalibrationDir)) && !issues.contains(kCalibrationIssue))
            *bad << QStringLiteral("line %1 (%2): a calibration screen's line names #%3").arg(k + 1).arg(file).arg(kCalibrationIssue);
        else if (mustExist && !QFileInfo::exists(absPath(file)))
            *bad << QStringLiteral("line %1 names a file that does not exist (%2)").arg(k + 1).arg(file);
        else
            allow.insert(file, { n, k + 1, issues });
    }
    return allow;
}

// Each file's count against its line: above it fails (`over`, or `unlisted` for a file with no line,
// which is allowed none); below it is reported so the line can be lowered (`slack`).
struct Tally
{
    QStringList over, slack, unlisted;
    int frozen = 0, unlistedHits = 0;
};

Tally tally(const QMap<QString, int> &counts, const QHash<QString, Allowed> &allow)
{
    Tally t;
    for (auto c = counts.cbegin(); c != counts.cend(); ++c) {
        const QString &rel = c.key();
        const int n = c.value();
        const bool listed = allow.contains(rel);
        const int a = allow.value(rel).count;
        if (listed) t.frozen += qMin(n, a);
        if (n > a) {
            (listed ? t.over : t.unlisted) << rel;
            if (!listed) t.unlistedHits += n;
        } else if (listed && n < a) {
            t.slack << rel;
        }
    }
    return t;
}

} // namespace w12

void ruleW12()
{
    using namespace w12;

    // 1. Planted cases: each shape counts once, and each exclusion keeps its case out, so a scanner that
    //    stops matching fails here instead of passing the tree.
    struct Plant { const char *rel; const char *text; int want; const char *what; };
    const Plant plants[] = {
        { "plant/A.qml", "Item {\n    function ok(r) { return r.deviationDeg <= 15.0 }\n}\n", 1,
          "a literal threshold in a function" },
        { "plant/B.qml", "Item {\n    visible: 20 < imu.batteryPct\n}\n", 1, "a literal on the left, in a binding" },
        { "plant/C.qml", "Item {\n    readonly property real _gateDeg: 12.0\n    function f(t) { if (t > _gateDeg) go() }\n"
                         "    property bool b: t.tiltDeg >= root._gateDeg\n}\n", 2,
          "a named threshold, bare and qualified" },
        { "plant/D.qml", "Item {\n    Timer { onTriggered: { if (s.gyroDps > 10) pass() } }\n}\n", 1, "a threshold in a handler" },
        { "plant/E.qml", "Item {\n    readonly property bool passed: reading.score >= 75\n"
                         "    property color tone: pct > 60 ? \"green\"\n                       : pct > 20 ? \"amber\" : \"red\"\n}\n", 3,
          "a pass/fail binding, and a two-line colour band" },
        { "plant/f.js", ".pragma library\nfunction grade(s) { return s < 25 ? \"poor\" : \"ok\" }\n", 1,
          "a threshold in a .js library" },
        { "plant/G.qml", "Item {\n    Rectangle { visible: kf.score > 3; width: 4 }\n}\n", 1, "a binding on one line with others" },
        { "plant/Layout.qml",
          "Item {\n    width: parent.width > 600 ? 300 : 200\n    Layout.preferredWidth: n > 3 ? 120 : 80\n"
          "    font { pixelSize: n > 9 ? 12 : 14 }\n    property int cols: root.width / Theme.sp(40) > 4 ? 4 : 2\n"
          "    function shotLayout(n) { return n > 70 ? 2 : 1 }\n}\n", 0,
          "layout (layout bindings, a size operand, a layout function) must not count" },
        { "plant/Other.qml",
          "Item {\n    visible: list.length > 0 && rows.length >= 3\n    property bool a: state === 3 && (n >> 2) > k\n"
          "    function f() { for (var i = 0; i < 48; i++) {}  if (Math.abs(d) < 1e-9) return; q.forEach((x) => x) }\n"
          "    function fmt(ms) { return ms < 1000 ? ms + \" ms\" : ms / 1000 + \" s\" }\n"
          "    property bool b: index < 2\n    Canvas { onPaint: { if (v > 2) draw() } }\n"
          "    MouseArea { onPositionChanged: if (Math.abs(mouse.x - cx) < 8) hit() }\n"
          "    Chip { selected: Math.abs(setting - modelData) < 0.5 }\n    property bool c: dot > 0.9995\n}\n", 0,
          "sentinels, sizes, equality, shifts, loop bounds, guards, unit boundaries, indices, paint, pointer, selection" },
    };
    for (const Plant &p : plants) {
        const QVector<Hit> hits = scan(QString::fromLatin1(p.rel), QString::fromUtf8(p.text));
        const int n = counted(hits);
        check(n == p.want, QStringLiteral("W12 planted %1: %2 counted (want %3)").arg(QString::fromLatin1(p.what)).arg(n).arg(p.want));
        if (n != p.want)
            for (const Hit &h : hits) detail(QStringLiteral("%1:%2 %3 [%4]").arg(QString::fromLatin1(p.rel)).arg(h.line).arg(h.text, h.why.isEmpty() ? QStringLiteral("counted") : h.why));
    }

    // 2. Planted exception lists: the ratchet itself. A frozen file that gains a rule fails, one that
    //    loses one is reported, a file with no line that gains a rule fails, and a line with no issue,
    //    or a calibration line naming another issue, is refused.
    {
        const char *frozenText = "Item {\n    property color tone: pct > 60 ? \"green\" : pct > 20 ? \"amber\" : \"red\"\n"
                                 "    function ok(r) { return r.sigmaMs > 5.0 }\n}\n";               // 3 counted
        const char *newText    = "Item {\n    visible: imu.batteryPct < 30\n}\n";                    // 1 counted
        QStringList bad;
        const QHash<QString, Allowed> list = parseAllow(QStringLiteral(
            "# planted\n"
            "plant/Grew.qml | W12 | 2 | two bands, frozen  # #1\n"
            "plant/Shrank.qml | W12 | 4 | four rules, frozen  # #1\n"
            "plant/Held.qml | W12 | 3 | three rules on two issues, frozen  # #1 #2\n"), false, &bad);
        check(bad.isEmpty() && list.size() == 3 && list.value(QStringLiteral("plant/Held.qml")).issues == QList<int>{ 1, 2 },
              QStringLiteral("W12 planted list: three good lines read, one naming two issues (%1)").arg(list.size()));
        QMap<QString, int> counts;
        counts.insert(QStringLiteral("plant/Grew.qml"), counted(scan(QStringLiteral("plant/Grew.qml"), QString::fromLatin1(frozenText))));
        counts.insert(QStringLiteral("plant/Shrank.qml"), counted(scan(QStringLiteral("plant/Shrank.qml"), QString::fromLatin1(frozenText))));
        counts.insert(QStringLiteral("plant/Held.qml"), counted(scan(QStringLiteral("plant/Held.qml"), QString::fromLatin1(frozenText))));
        counts.insert(QStringLiteral("plant/New.qml"), counted(scan(QStringLiteral("plant/New.qml"), QString::fromLatin1(newText))));
        const Tally t = tally(counts, list);
        check(t.over == QStringList{ QStringLiteral("plant/Grew.qml") },
              QStringLiteral("W12 planted: a new rule in a frozen file fails (%1)").arg(t.over.join(QStringLiteral(", "))));
        check(t.slack == QStringList{ QStringLiteral("plant/Shrank.qml") },
              QStringLiteral("W12 planted: a lowered count is reported (%1)").arg(t.slack.join(QStringLiteral(", "))));
        check(t.unlisted == QStringList{ QStringLiteral("plant/New.qml") } && t.unlistedHits == 1,
              QStringLiteral("W12 planted: a new file with a rule fails (%1)").arg(t.unlisted.join(QStringLiteral(", "))));
        QStringList refused;
        parseAllow(QStringLiteral("plant/NoIssue.qml | W12 | 2 | no issue named\n"
                                  "calibration/Plant.qml | W12 | 2 | a calibration screen on another issue  # #7\n"
                                  "plant/Zero.qml | W12 | 0 | an empty line  # #1\n"), false, &refused);
        check(refused.size() == 3, QStringLiteral("W12 planted list: a line with no issue, a calibration line off #%1 and a zero count are refused (%2 of 3)")
                                      .arg(kCalibrationIssue).arg(refused.size()));
    }

    // 3. The exception list.
    QHash<QString, Allowed> allow;
    {
        QString text;
        const QString path = QStringLiteral(PP_QML_RULE_ALLOWLIST);
        if (!readText(path, &text)) {
            check(false, QStringLiteral("W12 read the exception list (%1)").arg(path));
            return;
        }
        QStringList bad;
        allow = parseAllow(text, true, &bad);
        for (const QString &b : bad) detail(b);
        int calibration = 0;
        for (auto a = allow.cbegin(); a != allow.cend(); ++a)
            if (a.key().startsWith(QLatin1String(kCalibrationDir))) ++calibration;
        check(bad.isEmpty() && calibration > 0,
              QStringLiteral("W12 the exception list: each line a file, a count, a reason and an issue (%1 lines, %2 calibration)")
                  .arg(allow.size()).arg(calibration));
    }

    // 4. The tree: every production .qml and .js under src/Gui.
    QStringList files;
    for (const QString &f : filesUnder(QString(), { QStringLiteral("*.qml"), QStringLiteral("*.js") }, true)) {
        const QString rel = relPath(f);
        if (rel.startsWith(QLatin1String("tests/")) || rel.contains(QLatin1String("/tests/"))) continue;
        files << rel;
    }
    check(files.size() >= 300, QStringLiteral("W12 read the production QML (%1 files)").arg(files.size()));

    QMap<QString, int> excluded;
    QMap<QString, int> counts;
    QHash<QString, QVector<Hit>> hitsOf;
    for (const QString &rel : files) {
        QString raw;
        if (!readText(absPath(rel), &raw)) {
            check(false, QStringLiteral("W12 read %1").arg(rel));
            continue;
        }
        const QVector<Hit> hits = scan(rel, raw);
        for (const Hit &h : hits)
            if (!h.why.isEmpty()) ++excluded[h.why];
        counts.insert(rel, counted(hits));
        hitsOf.insert(rel, hits);
    }
    const Tally t = tally(counts, allow);
    const auto listHits = [&](const QString &rel) {
        detail(QStringLiteral("%1: %2 counted, %3 allowed").arg(rel).arg(counts.value(rel)).arg(allow.value(rel).count));
        for (const Hit &h : hitsOf.value(rel))
            if (h.why.isEmpty()) detail(QStringLiteral("    %1:%2  %3").arg(rel).arg(h.line).arg(h.text));
    };
    for (const QString &o : t.over) listHits(o);
    check(t.over.isEmpty(), QStringLiteral("W12 no listed file holds more rules than its line allows (%1 frozen in %2 files)")
                                .arg(t.frozen).arg(allow.size()));
    for (const QString &s : t.slack)
        info(QStringLiteral("%1 allows %2, counts %3 — lower the count (exception list line %4)")
                 .arg(s).arg(allow.value(s).count).arg(counts.value(s)).arg(allow.value(s).line));
    for (const QString &u : t.unlisted) listHits(u);
    check(t.unlisted.isEmpty(), QStringLiteral("W12 no rule in a QML file without a line (%1 found)").arg(t.unlistedHits));
    QStringList ex;
    for (auto e = excluded.cbegin(); e != excluded.cend(); ++e) ex << QStringLiteral("%1 %2").arg(e.key()).arg(e.value());
    info(QStringLiteral("W12 comparisons against a literal not counted: %1").arg(ex.join(QStringLiteral(", "))));
}

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
    { "W11", "the routines' mirrored gates equal their C++ constants", ruleW11 },
    { "W12", "no rule is built in QML; the exception list's files are frozen", ruleW12 },
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
