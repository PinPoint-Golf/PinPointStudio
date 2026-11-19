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

// app_translations_test — Settings → General → Language does something, and what it loads is sound.
//
// The Language combo stored its choice from the day it was drawn and nothing read it: there was no
// loader and no catalogue, so Chinese (or any language) left the interface in English and no test
// could have said so. This suite is the thing that says so. It compiles the SAME catalogues the app
// embeds (cmake/PinPointTranslations.cmake) into itself at :/i18n and drives the app's own loader
// (app/app_translations.h) — so a catalogue that is listed but will not load, or a loader that looks
// in the wrong place, fails here rather than on a golfer's screen.
//
// Two halves:
//
//   the loader     — which tag installs what, what an unknown or malformed tag does (nothing), and
//                    that installing a language leaves the default QLocale alone;
//   each catalogue — the things a translation can break with nothing failing to build: an
//                    unfinished entry, a placeholder dropped or doubled, a fragment's leading space
//                    lost, a phrase the code searches for translated two ways. Then the compiled
//                    .qm is asked for every message and must answer with what the .ts says: that
//                    lrelease dropped nothing, and that what the loader installed is this file.
//
// What it cannot see is a string the code looks up under a context lupdate never filed it under
// (a tr() behind a lambda, a QT_TR_NOOP with no class): that entry is simply absent, and absent is
// not wrong. lupdate says so when it runs — docs/developer/translations_developer_guide.md §4.

#include "app/app_translations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QLocale>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdio>

#ifndef PP_TS_DIR
#  error "PP_TS_DIR must name the directory holding the PinPointStudio_<tag>.ts files"
#endif
#ifndef PP_QML_ROOT
#  error "PP_QML_ROOT must name src/Gui"
#endif

static int g_fail = 0;
static int g_run  = 0;

// Quiet on success: a FAIL line names the case, the summary counts them.
static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", qUtf8Printable(label));
        ++g_fail;
    }
}

// A whole catalogue has thousands of messages. One failing rule is reported once, with how many
// messages broke it and the first few, so a bad catalogue reads as a list of rules, not a wall.
struct Rule {
    QString     name;
    int         broken = 0;
    QStringList examples;
    void fail(const QString &what) { ++broken; if (examples.size() < 5) examples << what; }
    void report(const QString &tag)
    {
        // Joined, not .arg()'d: a rule's own name may say "%1".
        check(tag + QStringLiteral(": ") + name + QStringLiteral(" (") + QString::number(broken)
                  + QStringLiteral(" message(s))"),
              broken == 0);
        for (const QString &e : std::as_const(examples))
            std::printf("           %s\n", qUtf8Printable(e));
    }
};

static void uninstallAll(QCoreApplication &app)
{
    const QList<QTranslator *> installed = app.findChildren<QTranslator *>();
    for (QTranslator *t : installed) {
        app.removeTranslator(t);
        delete t;
    }
}

static int translatorCount(const QCoreApplication &app)
{
    return int(app.findChildren<QTranslator *>().size());
}

// ── The loader ──────────────────────────────────────────────────────────────

static void testSourceLanguage(QCoreApplication &app)
{
    for (const char *tag : { "en_GB", "en_US", "en", "en-GB" }) {
        const AppTranslations::Result r = AppTranslations::install(app, QString::fromLatin1(tag));
        check(QStringLiteral("%1 is the source language").arg(QLatin1String(tag)),
              r.outcome == AppTranslations::Outcome::SourceLanguage && r.catalogue.isEmpty());
        check(QStringLiteral("%1 installs nothing").arg(QLatin1String(tag)), translatorCount(app) == 0);
    }
    check(QStringLiteral("with nothing installed a string is its source"),
          QCoreApplication::translate("GeneralPanel", "Language") == QLatin1String("Language"));
    check(QStringLiteral("zh_CN is not English"), !AppTranslations::isSourceLanguage(QStringLiteral("zh_CN")));
}

static void testUnknownAndMalformed(QCoreApplication &app)
{
    // A real tag for a language nobody will ship: the combo can offer a language ahead of its
    // catalogue, and that must leave English, untouched, with the outcome saying why.
    const AppTranslations::Result none = AppTranslations::install(app, QStringLiteral("tlh_AQ"));
    check(QStringLiteral("a language with no catalogue reports NoCatalogue"),
          none.outcome == AppTranslations::Outcome::NoCatalogue && none.catalogue.isEmpty());
    check(QStringLiteral("a language with no catalogue installs nothing"), translatorCount(app) == 0);

    // The tag becomes part of a file name, and comes from a settings file anyone can edit.
    for (const char *tag : { "", " ", "..", "../zh_CN", "zh_CN.qm", "zh CN", "*", "zh_CN/..", "z", ":/i18n/PinPointStudio_zh_CN" }) {
        const AppTranslations::Result r = AppTranslations::install(app, QString::fromLatin1(tag));
        check(QStringLiteral("'%1' is refused as a tag").arg(QLatin1String(tag)),
              r.outcome == AppTranslations::Outcome::InvalidTag);
        check(QStringLiteral("'%1' installs nothing").arg(QLatin1String(tag)), translatorCount(app) == 0);
    }
}

static void testFallbackStopsAtTheLanguage(QCoreApplication &app, const QString &shippedTag)
{
    const QString base     = AppTranslations::baseName();
    const QString language = QRegularExpression(QStringLiteral("^([A-Za-z]{2,3})")).match(shippedTag).captured(1);
    const QString shipped  = QDir(AppTranslations::defaultDirectory())
                                 .filePath(base + QLatin1Char('_') + shippedTag + QStringLiteral(".qm"));

    // A territory with no catalogue of its own finds its language's.
    QTemporaryDir byLanguage;
    const QString bare = QDir(byLanguage.path()).filePath(base + QLatin1Char('_') + language + QStringLiteral(".qm"));
    check(QStringLiteral("a scratch catalogue could be written"), QFile::copy(shipped, bare));
    const AppTranslations::Result r = AppTranslations::install(app, language + QStringLiteral("_ZZ"), byLanguage.path());
    check(QStringLiteral("%1_ZZ falls back to %1").arg(language),
          r.outcome == AppTranslations::Outcome::Installed
              && QFileInfo(r.catalogue).fileName() == QFileInfo(bare).fileName());
    uninstallAll(app);

    // And no further: a catalogue with no language in its name is nobody's fallback.
    QTemporaryDir nameless;
    check(QStringLiteral("a nameless scratch catalogue could be written"),
          QFile::copy(shipped, QDir(nameless.path()).filePath(base + QStringLiteral(".qm"))));
    const AppTranslations::Result none = AppTranslations::install(app, shippedTag, nameless.path());
    check(QStringLiteral("a catalogue named for no language is never loaded"),
          none.outcome == AppTranslations::Outcome::NoCatalogue && translatorCount(app) == 0);
    uninstallAll(app);
}

// ── A catalogue, as the .ts says it ─────────────────────────────────────────

struct Message {
    QString     context;
    QString     source;
    QString     comment;
    QString     type;       // "", "unfinished", "vanished", "obsolete"
    bool        numerus = false;
    QStringList forms;      // one translation, or one per plural form
};

static QList<Message> readCatalogue(const QString &path, QString *language, QString *error)
{
    QList<Message> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return out;
    }
    QXmlStreamReader xml(&f);
    QString context;
    Message cur;
    bool inMessage = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("TS")) {
                *language = xml.attributes().value(QLatin1String("language")).toString();
            } else if (name == QLatin1String("name") && !inMessage) {
                context = xml.readElementText();
            } else if (name == QLatin1String("message")) {
                cur = Message();
                cur.context = context;
                cur.numerus = xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
                inMessage = true;
            } else if (inMessage && name == QLatin1String("source")) {
                cur.source = xml.readElementText();
            } else if (inMessage && name == QLatin1String("comment")) {
                cur.comment = xml.readElementText();
            } else if (inMessage && name == QLatin1String("translation")) {
                cur.type = xml.attributes().value(QLatin1String("type")).toString();
                if (!cur.numerus)
                    cur.forms << xml.readElementText();
            } else if (inMessage && name == QLatin1String("numerusform")) {
                cur.forms << xml.readElementText();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
            out << cur;
            inMessage = false;
        }
    }
    if (xml.hasError())
        *error = QStringLiteral("%1: %2").arg(path, xml.errorString());
    return out;
}

static QStringList placeholders(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("%(?:L?\\d+|n)"));
    QStringList found;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        found << it.next().captured(0);
    found.sort();
    return found;
}

static int leadingSpaces(const QString &s)
{
    int n = 0;
    while (n < s.size() && s.at(n) == QLatin1Char(' ')) ++n;
    return n;
}

static int trailingSpaces(const QString &s)
{
    int n = 0;
    while (n < s.size() && s.at(s.size() - 1 - n) == QLatin1Char(' ')) ++n;
    return n;
}

static QString shown(const Message &m, const QString &form)
{
    return QStringLiteral("[%1] \"%2\" -> \"%3\"").arg(m.context, m.source, form);
}

// The tags the Language combo offers, read from the panel itself.
static QStringList offeredLanguages()
{
    QFile f(QStringLiteral(PP_QML_ROOT "/settings/GeneralPanel.qml"));
    QStringList tags;
    if (!f.open(QIODevice::ReadOnly))
        return tags;
    const QString qml = QString::fromUtf8(f.readAll());
    const int from = qml.indexOf(QLatin1String("languageModel"));
    const int to   = qml.indexOf(QLatin1Char(']'), from);
    static const QRegularExpression re(QStringLiteral("tag:\\s*\"([^\"]+)\""));
    auto it = re.globalMatch(qml.mid(from, to - from));
    while (it.hasNext())
        tags << it.next().captured(1);
    return tags;
}

static void testCatalogue(QCoreApplication &app, const QString &tag)
{
    const QString path = QStringLiteral(PP_TS_DIR "/") + AppTranslations::baseName()
                         + QLatin1Char('_') + tag + QStringLiteral(".ts");
    QString language, error;
    const QList<Message> all = readCatalogue(path, &language, &error);
    check(QStringLiteral("%1: the .ts reads (%2)").arg(tag, error), error.isEmpty() && !all.isEmpty());
    check(QStringLiteral("%1: the .ts declares language=\"%1\", not \"%2\"").arg(tag, language), language == tag);

    QList<Message> live;
    for (const Message &m : all)
        if (m.type != QLatin1String("vanished") && m.type != QLatin1String("obsolete"))
            live << m;

    // What the row's "Settings" link looks for inside an error sentence (PpSessionsTableRow.qml):
    // every sentence that names Settings → Studio has to name it in these exact words.
    QString settingsStudio;
    for (const Message &m : std::as_const(live))
        if (m.context == QLatin1String("PpSessionsTableRow") && m.source == QStringLiteral(u"Settings → Studio"))
            settingsStudio = m.forms.value(0);

    static const QStringList endonyms = {
        QStringLiteral("English (UK)"), QStringLiteral("English (US)"), QStringLiteral(u"Français"),
        QStringLiteral("Deutsch"), QStringLiteral(u"日本語"), QStringLiteral(u"한국어"), QStringLiteral(u"中文（简体）")
    };

    Rule finished     { QStringLiteral("every message is translated and finished") };
    Rule holders      { QStringLiteral("placeholders (%1, %n) survive, each exactly as often") };
    Rule edges        { QStringLiteral("leading and trailing spaces survive (they join fragments)") };
    Rule lines        { QStringLiteral("line breaks survive") };
    Rule studio       { QStringLiteral("\"Settings → Studio\" is said one way everywhere") };
    Rule names        { QStringLiteral("a language's own name is left alone") };
    Rule spoken       { QStringLiteral("spoken instructions stay in the voice's language") };
    Rule plurals      { QStringLiteral("a plural message has a form, and only plural messages have several") };
    int  different = 0;

    for (const Message &m : std::as_const(live)) {
        if (m.type == QLatin1String("unfinished") || m.forms.isEmpty()) {
            finished.fail(shown(m, QString()));
            continue;
        }
        if (!m.numerus && m.forms.size() != 1)
            plurals.fail(shown(m, m.forms.join(QLatin1String(" | "))));
        const QStringList want = placeholders(m.source);
        for (const QString &form : m.forms) {
            if (form.isEmpty()) { finished.fail(shown(m, form)); continue; }
            if (placeholders(form) != want) holders.fail(shown(m, form));
            if (leadingSpaces(form) != leadingSpaces(m.source) || trailingSpaces(form) != trailingSpaces(m.source))
                edges.fail(shown(m, form));
            if (form.count(QLatin1Char('\n')) != m.source.count(QLatin1Char('\n'))) lines.fail(shown(m, form));
            if (!settingsStudio.isEmpty() && m.source.contains(QStringLiteral(u"Settings → Studio"))
                    && !form.contains(settingsStudio))
                studio.fail(shown(m, form));
            if (m.context == QLatin1String("GeneralPanel") && endonyms.contains(m.source) && form != m.source)
                names.fail(shown(m, form));
            // TrunkRoutine speaks these through a voice that only reads English (PhonemeTokenizer,
            // AzureTTSEngine). Until a language has a voice, its lines are the source's.
            if (m.context == QLatin1String("spoken_instructions") && form != m.source)
                spoken.fail(shown(m, form));
            if (form != m.source) ++different;
        }
    }
    check(QStringLiteral("%1: \"Settings → Studio\" has a translation for the row's link to find").arg(tag),
          !settingsStudio.isEmpty());
    finished.report(tag); holders.report(tag); edges.report(tag); lines.report(tag);
    studio.report(tag);   names.report(tag);   spoken.report(tag); plurals.report(tag);
    check(QStringLiteral("%1: it is a translation — %2 of %3 messages differ from the English")
              .arg(tag).arg(different).arg(live.size()),
          different * 2 > live.size());

    // The compiled catalogue, through the app's loader, answers as the .ts says.
    const AppTranslations::Result r = AppTranslations::install(app, tag);
    check(QStringLiteral("%1: installs from %2").arg(tag, r.catalogue),
          r.outcome == AppTranslations::Outcome::Installed
              && r.catalogue.endsWith(QLatin1Char('_') + tag + QStringLiteral(".qm")));
    check(QStringLiteral("%1: exactly one translator is installed").arg(tag), translatorCount(app) == 1);

    Rule answers { QStringLiteral("the compiled catalogue gives each message the .ts's translation") };
    for (const Message &m : std::as_const(live)) {
        if (m.forms.isEmpty() || m.forms.first().isEmpty() || m.type == QLatin1String("unfinished"))
            continue;
        if (m.numerus && m.forms.size() != 1)
            continue;   // which form a count picks is the language's plural rule, not ours to restate
        const QByteArray ctx = m.context.toUtf8(), src = m.source.toUtf8(), cmt = m.comment.toUtf8();
        const QString got = QCoreApplication::translate(ctx.constData(), src.constData(),
                                                        m.comment.isEmpty() ? nullptr : cmt.constData(),
                                                        m.numerus ? 3 : -1);
        QString want = m.forms.first();
        if (m.numerus)
            want.replace(QLatin1String("%n"), QStringLiteral("3"));
        if (got != want)
            answers.fail(shown(m, got));
    }
    answers.report(tag);

    // Both tag spellings reach the same catalogue.
    uninstallAll(app);
    QString dashed = tag;
    dashed.replace(QLatin1Char('_'), QLatin1Char('-'));
    check(QStringLiteral("%1 loads the same catalogue as %2").arg(dashed, tag),
          AppTranslations::install(app, dashed).catalogue == r.catalogue);
    uninstallAll(app);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("app_translations_test — Settings → General → Language (app/app_translations.h)\n");

    const QLocale before;

    testSourceLanguage(app);
    testUnknownAndMalformed(app);

    const QStringList shipped = AppTranslations::shippedLanguages();
    const QStringList offered = offeredLanguages();
    check(QStringLiteral("at least one catalogue is compiled in at %1").arg(AppTranslations::defaultDirectory()),
          !shipped.isEmpty());
    check(QStringLiteral("the Language combo's tags could be read from GeneralPanel.qml"), !offered.isEmpty());

    for (const QString &tag : shipped) {
        check(QStringLiteral("%1 has a catalogue, so the Language combo offers it").arg(tag), offered.contains(tag));
        check(QStringLiteral("%1 is not the source language").arg(tag), !AppTranslations::isSourceLanguage(tag));
        testCatalogue(app, tag);
    }
    if (!shipped.isEmpty())
        testFallbackStopsAtTheLanguage(app, shipped.first());

    // Said, not failed: the combo may offer a language ahead of its catalogue, and choosing one
    // leaves English with a warning in the log. This is the list of those.
    QStringList waiting;
    for (const QString &tag : offered)
        if (!AppTranslations::isSourceLanguage(tag) && !shipped.contains(tag))
            waiting << tag;
    if (!waiting.isEmpty())
        std::printf("  [note] offered in Settings with no catalogue yet: %s\n", qUtf8Printable(waiting.join(QLatin1String(", "))));

    // Dates, figures and every decimal field name their own locale; a language must not move them.
    check(QStringLiteral("installing a language leaves the default QLocale alone"), QLocale() == before);

    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
