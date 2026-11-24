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

// The source text the tree-reading lints share (architecture_lint_test, namespace_collision_test): a file's text
// with its comments removed and its literals blanked, every newline kept so line numbers survive. Header-only, so
// no library: each test includes it. Moved here unchanged from architecture_lint_test.cpp (platform refactor B1.20a).

#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pp_lint {

struct Source {
    QString                      rel;      // path under src/
    bool                         qml = false;
    QString                      code;     // comments removed, literal contents blanked (structure kept)
    QString                      lit;      // comments removed, literals kept
    QStringList                  codeLines, litLines;
    QVector<QPair<int, QString>> strings;  // string literals with their line
};

inline bool isIdent(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Blank comments (both copies) and the contents of string / char literals (the `code` copy), keeping
// every newline so line numbers survive. C++: raw strings and digit separators; QML/JS: template
// strings.
inline void strip(const QString &src, bool cpp, Source *s)
{
    QString &code = s->code;
    QString &lit  = s->lit;
    code.reserve(src.size());
    lit.reserve(src.size());
    int line = 1;
    auto both = [&](QChar c) { code += c; lit += c; if (c == QLatin1Char('\n')) ++line; };
    const int n = src.size();
    for (int i = 0; i < n; ++i) {
        const QChar c  = src.at(i);
        const QChar nx = i + 1 < n ? src.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && nx == QLatin1Char('/')) {
            while (i < n && src.at(i) != QLatin1Char('\n')) { code += QLatin1Char(' '); lit += QLatin1Char(' '); ++i; }
            if (i < n) both(QLatin1Char('\n'));
            continue;
        }
        if (c == QLatin1Char('/') && nx == QLatin1Char('*')) {
            i += 2; code += QStringLiteral("  "); lit += QStringLiteral("  ");
            while (i < n && !(src.at(i) == QLatin1Char('*') && i + 1 < n && src.at(i + 1) == QLatin1Char('/'))) {
                both(src.at(i) == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' '));
                ++i;
            }
            if (i < n) { code += QStringLiteral("  "); lit += QStringLiteral("  "); ++i; }
            continue;
        }
        // C++ raw string R"delim( … )delim"
        if (cpp && c == QLatin1Char('"') && i > 0 && src.at(i - 1) == QLatin1Char('R')
            && (i < 2 || !isIdent(src.at(i - 2)) || QStringLiteral("uUL8").contains(src.at(i - 2)))) {
            const int open = src.indexOf(QLatin1Char('('), i);
            if (open > 0) {
                const QString delim = src.mid(i + 1, open - i - 1);
                const QString close = QLatin1Char(')') + delim + QLatin1Char('"');
                int end = src.indexOf(close, open);
                if (end < 0) end = n - close.size();
                const int startLine = line;
                const QString body = src.mid(open + 1, end - open - 1);
                code += c; lit += c;
                for (int k = i + 1; k < end + close.size() - 1 && k < n; ++k) {
                    const QChar ch = src.at(k);
                    lit += ch;
                    code += ch == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' ');
                    if (ch == QLatin1Char('\n')) ++line;
                }
                code += QLatin1Char('"'); lit += QLatin1Char('"');
                s->strings.append({ startLine, body });
                i = end + close.size() - 1;
                continue;
            }
        }
        const bool quote = c == QLatin1Char('"') || c == QLatin1Char('\'') || (!cpp && c == QLatin1Char('`'));
        if (quote) {
            // C++14 digit separator (1'000'000): a ' inside a number token is not a literal
            if (cpp && c == QLatin1Char('\'')) {
                int j = i - 1;
                while (j >= 0 && (isIdent(src.at(j)) || src.at(j) == QLatin1Char('\'') || src.at(j) == QLatin1Char('.'))) --j;
                if (j + 1 < i && src.at(j + 1).isDigit()) { both(c); continue; }
            }
            const QChar q = c;
            const int   startLine = line;
            QString     body;
            code += q; lit += q;
            ++i;
            while (i < n && src.at(i) != q) {
                const QChar ch = src.at(i);
                if (ch == QLatin1Char('\\') && i + 1 < n) {
                    body += src.at(i + 1);
                    lit += ch; lit += src.at(i + 1);
                    code += QStringLiteral("  ");
                    i += 2;
                    continue;
                }
                if (ch == QLatin1Char('\n')) {
                    if (q != QLatin1Char('`')) break;                // an unterminated literal ends at the line
                    both(QLatin1Char('\n'));
                } else {
                    lit += ch; code += QLatin1Char(' ');
                }
                body += ch;
                ++i;
            }
            if (i < n && src.at(i) == q) { code += q; lit += q; }
            else if (i < n) both(src.at(i));
            s->strings.append({ startLine, body });
            continue;
        }
        both(c);
    }
    s->codeLines = s->code.split(QLatin1Char('\n'));
    s->litLines  = s->lit.split(QLatin1Char('\n'));
}

inline Source makeSource(const QString &rel, const QString &text)
{
    Source s;
    s.rel = rel;
    s.qml = rel.endsWith(QLatin1String(".qml")) || rel.endsWith(QLatin1String(".js"));
    strip(text, !s.qml, &s);
    return s;
}

inline int lineAt(const QString &text, int offset)
{
    return text.left(offset).count(QLatin1Char('\n')) + 1;
}

} // namespace pp_lint
