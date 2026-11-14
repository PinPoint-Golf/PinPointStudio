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

#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

// The interface's language: Settings → General → Language, made real.
//
// The setting (AppSettings::language, "General/language") was stored from the day the combo was
// drawn and read by nothing, so every choice in it left the interface in English. This is the one
// reader. main.cpp calls install() once, straight after AppSettings exists and before any controller
// or the QML engine builds a string — a translator installed later reaches qsTr() bindings only on
// a retranslate, and never the tr() text a C++ model has already cached, which is why the setting
// says "Restart required to apply" and why this runs exactly once.
//
// The catalogues are the .ts files in cmake/PinPointTranslations.cmake, compiled by lrelease and
// embedded at ":/i18n/PinPointStudio_<tag>.qm" (qt_add_translations in the root CMakeLists). The
// source text is British English, so en_GB and en_US need no catalogue and get none.
//
// It deliberately leaves QLocale alone. Dates, figures and every parser in the app name their
// locale explicitly, and a default locale with a decimal comma would change what the loft and
// exposure fields accept (docs/developer/translations_developer_guide.md §6).
//
// Qt Core only, and it logs nothing: the outcome comes back to the caller, so the test that guards
// it links no logging and main.cpp writes the one line.
class AppTranslations
{
public:
    enum class Outcome {
        Installed,       // a catalogue for the tag was found and is now translating
        SourceLanguage,  // English, which is what the source is written in: nothing to load
        NoCatalogue,     // a language the app offers or was handed, with no catalogue behind it
        InvalidTag       // not a language tag at all (a hand-edited settings file)
    };

    struct Result {
        Outcome outcome = Outcome::SourceLanguage;
        QString catalogue;   // the resource that was loaded, when one was
    };

    // Where the catalogues are compiled in, and the base name each one carries before "_<tag>.qm".
    static QString defaultDirectory();
    static QString baseName();

    // Loads the catalogue for `languageTag` ("zh_CN") from `directory` and installs it on `app`,
    // which then owns the translator. A tag with a territory falls back to its bare language
    // ("pt_BR" finds "_pt" when there is no "_pt_BR") and no further.
    // Anything other than Installed leaves the application exactly as it was.
    static Result install(QCoreApplication &app, const QString &languageTag,
                          const QString &directory = defaultDirectory());

    // The tags that have a catalogue in `directory`, sorted: what a translated build really offers.
    static QStringList shippedLanguages(const QString &directory = defaultDirectory());

    // True for a tag whose language is English ("en", "en_GB", "en-US").
    static bool isSourceLanguage(const QString &languageTag);
};
