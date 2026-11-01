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

// Design-system lint — docs/design/system/README.md, issue #133.
//
// The GUI was built screen by screen by copying the nearest sibling, and the copies drifted (81
// duplicated controls by 31 Oct). Two runs collapsed them onto one component per control; this
// test is what stops the copying starting again. It reads the SOURCE of every screen — everything
// under src/Gui/ except the design system itself (components/, typeset/, theme/) and tests/ — and
// fails on:
//
//   D1  a hand-drawn control: a Rectangle with a MouseArea / TapHandler / PpPressable child and a
//       Text inside it. Use PpButton, PpChip, PpChoiceChip, PpRowAction, PpLink, PpToolPill … .
//   D2  a raw text input (TextField, TextInput, TextArea, TextEdit). Use PpTextField, PpSearchField
//       or PpTextArea.
//   D3  a Popup / Menu / Dialog / Drawer / ToolTip whose background is not PpPopoverCard.
//   D4  a literal colour ("#rrggbb"). Use a Theme.color* token, Theme.metricColor() or
//       Theme.chartSeriesColor().
//   D5  space that does not follow Density: spacing, margins and layout padding set from
//       Theme.sp(n), a typeset u(n) or a literal n >= 4. Space is Theme.gap(n) (typeset: g(n)).
//       Negative margins (hit areas) and nudges under 4 px are size, not space, and pass.
//
// Deliberate exceptions live in design_system_allowlist.txt beside this file — one line per
// file and rule, with the number of findings allowed and the REASON. A file that grows past its
// count fails; a file that drops below it is reported so the count can be tightened.
//
// Before the scan, the rules are run on fixtures (a hand-drawn button, a PpButton, …) so a rule
// that silently stops matching fails here rather than passing everything.
//
//   cmake --build build/tests --target design_system_lint_test
//   ctest --test-dir build/tests -R design_system_lint --output-on-failure

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdio>

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

struct Finding {
    QString rule;
    int     line = 0;
    QString text;
};

// ── A small QML element walker ─────────────────────────────────────────────────────────────────
// Enough structure for the rules: each `Type {` opens an element (with the property it is bound
// to, if any — `background: PpPopoverCard {`), every other `{` opens a JS/object block, and string
// literals and comments never count as braces.
struct Element {
    QString     type;      // "" for a JS block
    QString     boundTo;   // "background" for `background: X {`
    int         line = 0;
    int         parent = -1;
    QStringList childTypes;                // direct child elements
    int         start = 0, end = 0;          // offsets of its braces in the stripped code
    bool        hasTextDescendant = false;
};

const QRegularExpression &openerRe()
{
    // [prop:] Type [.Type] {      — Type starts with a capital letter
    static const QRegularExpression re(QStringLiteral(
        R"((?:([A-Za-z_][\w.]*)\s*:\s*)?([A-Z][\w]*(?:\.[A-Z][\w]*)*)\s*$)"));
    return re;
}

bool isTextType(const QString &t)
{
    return t == QLatin1String("Text") || t == QLatin1String("Label");
}
bool isPointerType(const QString &t)
{
    return t == QLatin1String("MouseArea") || t == QLatin1String("TapHandler")
        || t == QLatin1String("PpPressable");
}
bool isPopupType(const QString &t)
{
    static const QStringList p{ QStringLiteral("Popup"), QStringLiteral("Menu"),
                                QStringLiteral("Dialog"), QStringLiteral("Drawer"),
                                QStringLiteral("ToolTip") };
    return p.contains(t);
}

// Strip comments and replace string contents with spaces (keeping the quotes and the line
// structure), collecting the string literals with their line numbers for D4.
QString stripCode(const QString &src, QVector<QPair<int, QString>> *strings)
{
    QString out;
    out.reserve(src.size());
    int line = 1;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src.at(i);
        const QChar n = i + 1 < src.size() ? src.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && n == QLatin1Char('/')) {
            while (i < src.size() && src.at(i) != QLatin1Char('\n')) ++i;
            if (i < src.size()) { out += QLatin1Char('\n'); ++line; }
            continue;
        }
        if (c == QLatin1Char('/') && n == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < src.size() && !(src.at(i) == QLatin1Char('*') && src.at(i + 1) == QLatin1Char('/'))) {
                if (src.at(i) == QLatin1Char('\n')) { out += QLatin1Char('\n'); ++line; }
                ++i;
            }
            ++i;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            const QChar q = c;
            QString lit;
            out += q;
            const int startLine = line;
            ++i;
            while (i < src.size() && src.at(i) != q) {
                if (src.at(i) == QLatin1Char('\\') && i + 1 < src.size()) {
                    lit += src.at(i + 1); out += QStringLiteral("  "); i += 2; continue;
                }
                if (src.at(i) == QLatin1Char('\n')) { out += QLatin1Char('\n'); ++line; }
                else out += QLatin1Char(' ');
                lit += src.at(i);
                ++i;
            }
            out += q;
            if (strings) strings->append({ startLine, lit });
            continue;
        }
        if (c == QLatin1Char('\n')) ++line;
        out += c;
    }
    return out;
}

QVector<Element> walk(const QString &code)
{
    QVector<Element> els;
    QVector<int>     stack;
    int              line = 1;
    int              lineStart = 0;
    for (int i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('\n')) { ++line; lineStart = i + 1; continue; }
        if (c == QLatin1Char('{')) {
            // the text since the last statement boundary on this line decides what opened
            int from = i - 1;
            while (from >= lineStart && code.at(from) != QLatin1Char(';')
                   && code.at(from) != QLatin1Char('{') && code.at(from) != QLatin1Char('}'))
                --from;
            QString head = code.mid(from + 1, i - from - 1).trimmed();
            // `foo: [ Type {` and `, Type {` (an element in a list property) are elements too
            head.remove(QLatin1Char('[')).remove(QLatin1Char(','));
            head = head.trimmed();
            // `property Component foo: Type {` — drop the declaration
            static const QRegularExpression decl(QStringLiteral(
                R"(^(?:(?:readonly|required|default)\s+)*property\s+[\w<>.]+\s+)"));
            head.remove(decl);
            // `component Name: Type {` — an inline component is an element of that type
            static const QRegularExpression inlineComp(QStringLiteral(R"(^component\s+\w+\s*:\s*)"));
            head.remove(inlineComp);
            Element e;
            e.line   = line;
            e.start  = i;
            e.parent = stack.isEmpty() ? -1 : stack.last();
            const auto m = openerRe().match(head);
            // `Type {` must be the whole head (or `prop: Type {`); `if (x) {`, `=> {`, `({` are JS
            if (m.hasMatch() && m.capturedStart() == 0) {
                e.type    = m.captured(2);
                e.boundTo = m.captured(1);
            }
            els.append(e);
            const int idx = els.size() - 1;
            if (!e.type.isEmpty()) {
                // register as a direct child of the nearest enclosing element
                for (int p = e.parent; p >= 0; p = els[p].parent) {
                    if (!els[p].type.isEmpty()) { els[p].childTypes << e.type; break; }
                }
                if (isTextType(e.type))
                    for (int p = e.parent; p >= 0; p = els[p].parent) els[p].hasTextDescendant = true;
            }
            stack.append(idx);
        } else if (c == QLatin1Char('}')) {
            if (!stack.isEmpty()) { els[stack.last()].end = i; stack.removeLast(); }
        }
    }
    return els;
}

int nearestElement(const QVector<Element> &els, int idx)
{
    for (int p = idx; p >= 0; p = els[p].parent)
        if (!els[p].type.isEmpty()) return p;
    return -1;
}

// ── The rules ──────────────────────────────────────────────────────────────────────────────────
QVector<Finding> scan(const QString &src)
{
    QVector<Finding>             out;
    QVector<QPair<int, QString>> strings;
    const QString                code  = stripCode(src, &strings);
    const QVector<Element>       els   = walk(code);
    const QStringList            lines = code.split(QLatin1Char('\n'));

    for (int i = 0; i < els.size(); ++i) {
        const Element &e = els[i];
        if (e.type.isEmpty()) continue;
        // D1 — a Rectangle that is pressed and carries words: a hand-made button or chip
        if (e.type == QLatin1String("Rectangle")) {
            bool pointer = false;
            for (const QString &t : e.childTypes) pointer = pointer || isPointerType(t);
            // a picture surface (video, still, canvas) with a click area and a caption is not a control
            static const QStringList pictures{ QStringLiteral("VideoOutput"), QStringLiteral("BayerVideoItem"),
                                               QStringLiteral("Image"), QStringLiteral("Canvas") };
            bool picture = false;
            for (const QString &t : e.childTypes) picture = picture || pictures.contains(t);
            // a table's cell delegate is drawn by the table, not a control
            static const QStringList tables{ QStringLiteral("TableView"), QStringLiteral("HorizontalHeaderView"),
                                             QStringLiteral("VerticalHeaderView") };
            const int  host  = nearestElement(els, e.parent);
            const bool cell  = e.boundTo == QLatin1String("delegate") && host >= 0 && tables.contains(els[host].type);
            if (pointer && e.hasTextDescendant && !picture && !cell)
                out.append({ QStringLiteral("D1"), e.line,
                             QStringLiteral("Rectangle + pointer handler + Text: a hand-drawn control "
                                            "(PpButton, PpChip, PpChoiceChip, PpRowAction, PpLink, PpToolPill)") });
        }
        // D2 — a raw text input
        static const QStringList inputs{ QStringLiteral("TextField"), QStringLiteral("TextInput"),
                                         QStringLiteral("TextArea"), QStringLiteral("TextEdit") };
        // a read-only TextEdit / TextArea is selectable text (a log, a transcript), not an input
        const bool readOnly = (e.type == QLatin1String("TextEdit") || e.type == QLatin1String("TextArea"))
                              && code.mid(e.start, e.end - e.start).contains(QRegularExpression(QStringLiteral(R"(\breadOnly\s*:\s*true\b)")));
        if (inputs.contains(e.type) && !readOnly)
            out.append({ QStringLiteral("D2"), e.line,
                         e.type + QStringLiteral(": use PpTextField, PpSearchField or PpTextArea") });
        // D3 — a popup surface that is not PpPopoverCard
        if (e.boundTo == QLatin1String("background") && e.type != QLatin1String("PpPopoverCard")) {
            const int owner = nearestElement(els, e.parent);
            if (owner >= 0 && isPopupType(els[owner].type))
                out.append({ QStringLiteral("D3"), e.line,
                             els[owner].type + QStringLiteral(" background is ") + e.type
                                 + QStringLiteral(": use PpPopoverCard") });
        }
    }

    // D4 — literal colours
    static const QRegularExpression hex(QStringLiteral(R"(^#(?:[0-9A-Fa-f]{3}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$)"));
    // a hex string shown as words (text: "#1E88E5" in a colour editor) is content, not a colour
    static const QRegularExpression asText(QStringLiteral(R"(\b(?:text|placeholderText)\s*:\s*["'])"));
    for (const auto &s : strings)
        if (hex.match(s.second).hasMatch()
            && !(s.first - 1 < lines.size() && asText.match(lines.at(s.first - 1)).hasMatch()))
            out.append({ QStringLiteral("D4"), s.first,
                         QStringLiteral("\"%1\": use a Theme.color* token").arg(s.second) });

    // D5 — space that does not follow Density
    static const QRegularExpression spaceProp(QStringLiteral(
        R"((?:^|[\s;{])((?:anchors\.|Layout\.)?(?:spacing|rowSpacing|columnSpacing|margins|leftMargin|rightMargin|topMargin|bottomMargin|padding|leftPadding|rightPadding|topPadding|bottomPadding|horizontalPadding|verticalPadding))\s*:\s*([^;\n]+))"));
    static const QRegularExpression sizeCall(QStringLiteral(R"((?:Theme\.sp|\bu)\(\s*(\d+(?:\.\d+)?)\s*\))"));
    static const QRegularExpression literal(QStringLiteral(R"(^(\d+(?:\.\d+)?)\s*$)"));
    // padding on a control is part of the control's size (a button's text padding), not layout space
    static const QStringList layoutTypes{ QStringLiteral("Column"), QStringLiteral("Row"),
        QStringLiteral("Grid"), QStringLiteral("Flow"), QStringLiteral("ColumnLayout"),
        QStringLiteral("RowLayout"), QStringLiteral("GridLayout"), QStringLiteral("Pane"),
        QStringLiteral("Frame"), QStringLiteral("Page"), QStringLiteral("ScrollView"),
        QStringLiteral("Flickable"), QStringLiteral("ListView"), QStringLiteral("GridView"),
        QStringLiteral("Repeater"), QStringLiteral("Item"), QStringLiteral("Rectangle"),
        QStringLiteral("Popup") };
    // element owning each line (the innermost element open at the start of the line)
    QVector<int> ownerAtLine(lines.size() + 2, -1);
    {
        QVector<int> stack;
        int          line = 1, ei = 0;
        for (int i = 0; i < code.size(); ++i) {
            const QChar c = code.at(i);
            if (c == QLatin1Char('\n')) {
                ++line;
                if (line < ownerAtLine.size())
                    ownerAtLine[line] = stack.isEmpty() ? -1 : nearestElement(els, stack.last());
            } else if (c == QLatin1Char('{')) {
                stack.append(ei++);
            } else if (c == QLatin1Char('}')) {
                if (!stack.isEmpty()) stack.removeLast();
            }
        }
    }
    for (int li = 0; li < lines.size(); ++li) {
        auto it = spaceProp.globalMatch(lines.at(li));
        while (it.hasNext()) {
            const auto    m     = it.next();
            const QString prop  = m.captured(1);
            QString       value = m.captured(2).trimmed();
            if (value.startsWith(QLatin1Char('-'))) continue;                 // a hit area
            if (value.contains(QStringLiteral("gap(")) || value.contains(QStringLiteral(".g(")))
                continue;                                                      // follows Density
            if (prop.contains(QStringLiteral("adding")) && !prop.startsWith(QStringLiteral("Layout"))) {
                const int owner = li + 1 < ownerAtLine.size() ? ownerAtLine[li + 1] : -1;
                if (owner >= 0 && !layoutTypes.contains(els[owner].type)) continue;
            }
            double n = -1;
            const auto lm = literal.match(value);
            if (lm.hasMatch()) n = lm.captured(1).toDouble();
            else {
                const auto sm = sizeCall.match(value);
                if (sm.hasMatch() && sm.capturedStart() == 0) n = sm.captured(1).toDouble();
            }
            if (n >= 4)
                out.append({ QStringLiteral("D5"), li + 1,
                             QStringLiteral("%1: %2 does not follow Density: use Theme.gap(n) (typeset: g(n))")
                                 .arg(prop, value.left(40)) });
        }
    }
    return out;
}

int countRule(const QVector<Finding> &fs, const char *rule)
{
    int n = 0;
    for (const auto &f : fs) n += f.rule == QLatin1String(rule) ? 1 : 0;
    return n;
}

// ── Fixtures: every rule must still bite ───────────────────────────────────────────────────────
void selfTest()
{
    std::printf("=== the rules still catch what they are for ===\n");
    const QString handButton = QStringLiteral(
        "Item {\n  Rectangle {\n    radius: 4; color: Theme.colorAccent\n"
        "    Text { text: qsTr(\"Save\") }\n    MouseArea { anchors.fill: parent; onClicked: save() }\n  }\n}\n");
    check(countRule(scan(handButton), "D1") == 1, QStringLiteral("D1 catches a hand-drawn button"));
    const QString nestedLabel = QStringLiteral(
        "Rectangle {\n  Row { Text { text: \"a\" } }\n  TapHandler { onTapped: go() }\n}\n");
    check(countRule(scan(nestedLabel), "D1") == 1, QStringLiteral("D1 catches a pressable card with a nested label"));
    const QString realButton = QStringLiteral(
        "Item {\n  PpButton { text: qsTr(\"Save\"); onClicked: save() }\n"
        "  Rectangle { color: Theme.colorBg2; Text { text: \"no press\" } }\n}\n");
    check(scan(realButton).isEmpty(), QStringLiteral("a PpButton and an unpressable panel pass"));
    check(countRule(scan(QStringLiteral("Rectangle {\n  VideoOutput { anchors.fill: parent }\n  Text { text: \"cam\" }\n"
                                        "  MouseArea { anchors.fill: parent }\n}\n")), "D1") == 0,
          QStringLiteral("D1 passes a video surface with a click area and a caption"));
    check(countRule(scan(QStringLiteral("TableView {\n  delegate: Rectangle {\n    Text { text: display }\n"
                                        "    TapHandler { }\n  }\n}\n")), "D1") == 0,
          QStringLiteral("D1 passes a table's cell delegate"));
    check(countRule(scan(QStringLiteral("Item {\n  component Pill: Rectangle {\n    Text { text: \"x\" }\n"
                                        "    MouseArea { }\n  }\n}\n")), "D1") == 1,
          QStringLiteral("D1 sees inside an inline component"));
    check(countRule(scan(QStringLiteral("Item { TextInput { } }\n")), "D2") == 1,
          QStringLiteral("D2 catches a raw TextInput"));
    check(countRule(scan(QStringLiteral("Item {\n  TextEdit {\n    readOnly: true\n  }\n}\n")), "D2") == 0,
          QStringLiteral("D2 passes read-only selectable text"));
    check(countRule(scan(QStringLiteral("Popup {\n  background: Rectangle { color: Theme.colorSurface }\n}\n")), "D3") == 1,
          QStringLiteral("D3 catches a Popup drawing its own surface"));
    check(countRule(scan(QStringLiteral("Popup {\n  background: PpPopoverCard { }\n}\n")), "D3") == 0,
          QStringLiteral("D3 passes a PpPopoverCard background"));
    check(countRule(scan(QStringLiteral("Rectangle { color: \"#FF0000\" }\n")), "D4") == 1,
          QStringLiteral("D4 catches a literal colour"));
    check(countRule(scan(QStringLiteral("Rectangle { // \"#FF0000\" in a comment\n color: Theme.colorBg }\n")), "D4") == 0,
          QStringLiteral("D4 ignores comments"));
    check(countRule(scan(QStringLiteral("Text { text: \"#1E88E5\" }\n")), "D4") == 0,
          QStringLiteral("D4 passes a hex value shown as words"));
    const QString spaces = QStringLiteral(
        "Column {\n  spacing: Theme.sp(12)\n  anchors.margins: 16\n  topPadding: Theme.gap(8)\n"
        "  anchors.leftMargin: -6\n  Layout.topMargin: 2\n  bottomPadding: roles.g(6)\n}\n");
    check(countRule(scan(spaces), "D5") == 2,
          QStringLiteral("D5 catches sp() and literal space, passes gap()/g(), hit areas and nudges"));
    check(countRule(scan(QStringLiteral("PpButton {\n  horizontalPadding: Theme.sp(24)\n}\n")), "D5") == 0,
          QStringLiteral("D5 leaves a control's own padding (its size) alone"));
}

// ── The allow-list ─────────────────────────────────────────────────────────────────────────────
struct Allow {
    int     count = 0;
    QString reason;
    int     line  = 0;
};

QMap<QString, Allow> readAllowList(const QString &path, bool *ok)
{
    QMap<QString, Allow> out;
    QFile                f(path);
    *ok = f.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!*ok) return out;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString l = lines.at(i).trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = l.split(QLatin1Char('|'));
        if (parts.size() < 4 || parts.at(3).trimmed().isEmpty()) {
            check(false, QStringLiteral("allow-list line %1 has path | rule | count | reason: %2").arg(i + 1).arg(l));
            continue;
        }
        Allow a;
        a.count  = parts.at(2).trimmed().toInt();
        a.reason = parts.mid(3).join(QLatin1Char('|')).trimmed();
        a.line   = i + 1;
        // a file may have a deliberate line and a held line for the same rule: they add up
        const QString key = parts.at(0).trimmed() + QLatin1Char('|') + parts.at(1).trimmed();
        if (out.contains(key)) {
            a.count += out.value(key).count;
            a.reason = out.value(key).reason + QStringLiteral(" + ") + a.reason;
        }
        out.insert(key, a);
    }
    return out;
}

} // namespace

int main()
{
    selfTest();

    const QString root = QStringLiteral(PP_QML_ROOT);
    if (!QDir(root).exists()) {
        check(false, QStringLiteral("the QML tree is where the build says it is (%1)").arg(root));
        return 1;
    }
    bool       allowOk = false;
    const auto allow   = readAllowList(QStringLiteral(PP_DS_ALLOWLIST), &allowOk);
    std::printf("\n=== screens use the design system (docs/design/system/README.md) ===\n");
    check(allowOk, QStringLiteral("the allow-list was read (%1)").arg(QStringLiteral(PP_DS_ALLOWLIST)));

    // The design system itself, and the tests, are not screens.
    static const QStringList exempt{ QStringLiteral("components/"), QStringLiteral("typeset/"),
                                     QStringLiteral("theme/"), QStringLiteral("tests/") };
    QMap<QString, QVector<Finding>> byKey;   // "path|rule" → findings
    int                             scanned = 0;
    QDirIterator it(root, QStringList{ QStringLiteral("*.qml") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel  = QDir(root).relativeFilePath(path);
        bool          skip = false;
        for (const QString &e : exempt) skip = skip || rel.startsWith(e);
        if (skip) continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        ++scanned;
        for (const Finding &fd : scan(QString::fromUtf8(f.readAll())))
            byKey[rel + QLatin1Char('|') + fd.rule].append(fd);
    }
    check(scanned > 100, QStringLiteral("the screens were found and read (%1 files)").arg(scanned));

    static const char *ruleNames[] = { "D1 hand-drawn controls", "D2 raw text inputs",
                                       "D3 popup surfaces", "D4 literal colours",
                                       "D5 space that ignores Density" };
    for (int r = 0; r < 5; ++r) {
        const QString rule = QStringLiteral("D%1").arg(r + 1);
        QStringList   over, slack;
        int           allowed = 0;
        for (auto k = byKey.cbegin(); k != byKey.cend(); ++k) {
            if (!k.key().endsWith(QLatin1Char('|') + rule)) continue;
            const int n = k.value().size();
            const int a = allow.value(k.key()).count;
            allowed += qMin(n, a);
            if (n > a) {
                const QString file = k.key().section(QLatin1Char('|'), 0, 0);
                over << QStringLiteral("%1: %2 finding%3, %4 allowed")
                            .arg(file).arg(n).arg(n == 1 ? "" : "s").arg(a);
                for (const Finding &fd : k.value())
                    over << QStringLiteral("    %1:%2  %3").arg(file).arg(fd.line).arg(fd.text);
            }
        }
        for (auto a = allow.cbegin(); a != allow.cend(); ++a) {
            if (!a.key().endsWith(QLatin1Char('|') + rule)) continue;
            const int n = byKey.value(a.key()).size();
            if (n < a.value().count)
                slack << QStringLiteral("%1 allows %2, finds %3 — lower the count (allow-list line %4)")
                             .arg(a.key().section(QLatin1Char('|'), 0, 0)).arg(a.value().count).arg(n).arg(a.value().line);
        }
        for (const QString &o : over) std::printf("      %s\n", qPrintable(o));
        check(over.isEmpty(), QStringLiteral("%1: none outside the allow-list (%2 allowed)")
                                  .arg(QLatin1String(ruleNames[r])).arg(allowed));
        for (const QString &s : slack) std::printf("      note: %s\n", qPrintable(s));
    }
    if (g_fail)
        std::printf("\n  Use the shared component (docs/design/system/components/) or the Theme token.\n"
                    "  If none fits, that is a design-system change: ask Mark first — never a local copy.\n"
                    "  A deliberate exception goes in src/Gui/tests/design_system_allowlist.txt WITH its reason.\n");

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
