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

#include "app_translations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTranslator>

namespace {

// A language tag as the Language combo stores it: a language, then optional script and territory
// subtags, joined by "_" (or "-", which is normalised). The tag becomes part of a file name, so
// anything else — a path, a wildcard, an empty string — is refused rather than looked up.
QString normalisedTag(const QString &languageTag)
{
    static const QRegularExpression shape(
        QStringLiteral("^[A-Za-z]{2,3}(_[A-Za-z0-9]{2,8}){0,2}$"));
    QString tag = languageTag.trimmed();
    tag.replace(QLatin1Char('-'), QLatin1Char('_'));
    return shape.match(tag).hasMatch() ? tag : QString();
}

} // namespace

QString AppTranslations::defaultDirectory()
{
    return QStringLiteral(":/i18n");
}

QString AppTranslations::baseName()
{
    return QStringLiteral("PinPointStudio");
}

bool AppTranslations::isSourceLanguage(const QString &languageTag)
{
    const QString tag = normalisedTag(languageTag);
    return tag.section(QLatin1Char('_'), 0, 0).compare(QLatin1String("en"), Qt::CaseInsensitive) == 0;
}

AppTranslations::Result AppTranslations::install(QCoreApplication &app, const QString &languageTag,
                                                 const QString &directory)
{
    Result result;

    const QString tag = normalisedTag(languageTag);
    if (tag.isEmpty()) {
        result.outcome = Outcome::InvalidTag;
        return result;
    }

    // The search is done here, by name, and load() is only ever handed a file that exists:
    // "<base>_zh_CN.qm", then "<base>_zh.qm", then nothing. Left to search for itself, load() walks
    // a missing name all the way back to the bare "<base>.qm", and its QLocale form walks the
    // locale's whole uiLanguages() list — which depends on what the platform's locale data expands
    // a tag to, and on a system that lists a second language would quietly install that one.
    auto *translator = new QTranslator(&app);
    const QDir dir(directory);
    QString candidate = tag;
    bool loaded = false;
    while (!candidate.isEmpty()) {
        const QString path = dir.filePath(baseName() + QLatin1Char('_') + candidate + QStringLiteral(".qm"));
        if (QFileInfo::exists(path)) {
            // A catalogue that is there and will not load is reported, not papered over with a
            // shorter name's.
            loaded = translator->load(path);
            if (loaded)
                result.catalogue = path;
            break;
        }
        const int cut = candidate.lastIndexOf(QLatin1Char('_'));
        candidate = cut > 0 ? candidate.left(cut) : QString();
    }

    if (!loaded) {
        delete translator;
        result.outcome = isSourceLanguage(tag) ? Outcome::SourceLanguage : Outcome::NoCatalogue;
        return result;
    }

    app.installTranslator(translator);
    result.outcome = Outcome::Installed;
    return result;
}

QStringList AppTranslations::shippedLanguages(const QString &directory)
{
    const QString prefix = baseName() + QLatin1Char('_');
    QStringList tags;
    const QStringList files = QDir(directory).entryList({ prefix + QStringLiteral("*.qm") },
                                                        QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString tag = normalisedTag(file.mid(prefix.size(), file.size() - prefix.size() - 3));
        if (!tag.isEmpty())
            tags << tag;
    }
    return tags;
}
