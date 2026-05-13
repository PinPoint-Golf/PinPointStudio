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
#include <QHash>
#include <QString>
#include <QVector>

// Converts plain text to Kokoro token IDs via espeak-ng phonemisation.
//
// Pipeline:
//   text  ──▶  espeak_SetPhonemeTrace (IPA mode)  ──▶  IPA string  ──▶  token IDs
//
// Requires libespeak-ng (linked at build time via CMake) and its runtime data.

class PhonemeTokenizer
{
public:
    static constexpr int kMaxTokens = 512;

    PhonemeTokenizer() = default;
    ~PhonemeTokenizer();

    bool    initialise(const QString &tokensJsonPath);
    bool    isInitialised() const { return m_initialised; }
    QString lastError()     const { return m_lastError; }

    QVector<int64_t> tokenise(const QString &text) const;

private:
    QString          textToPhonemes(const QString &text) const;
    QVector<int64_t> phonemesToIds(const QString &phonemes) const;

    QHash<QString, int64_t> m_vocab;
    bool                    m_initialised = false;
    QString                 m_lastError;
};
