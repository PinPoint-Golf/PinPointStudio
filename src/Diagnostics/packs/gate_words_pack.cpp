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

#include "gate_words_pack.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>

namespace pinpoint::analysis {

namespace {

QString userGateWordsPath() { return diagnosticsFilePath(QStringLiteral("gate_words.json")); }

const GateWord *findWord(const std::vector<GateWord> &list, const QString &wid)
{
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const GateWord &w) { return w.id == wid; });
    return it == list.end() ? nullptr : &*it;
}

std::vector<GateWord> readWords(const QJsonValue &v)
{
    std::vector<GateWord> out;
    for (const QJsonValue &e : v.toArray()) {
        const QJsonObject o = e.toObject();
        out.push_back({ o.value(QStringLiteral("id")).toString(),
                        o.value(QStringLiteral("golfer")).toString() });
    }
    return out;
}

QJsonArray writeWords(const std::vector<GateWord> &list)
{
    QJsonArray arr;
    for (const GateWord &w : list) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), w.id);
        o.insert(QStringLiteral("golfer"), w.golfer);
        arr.append(o);
    }
    return arr;
}

// One list's ids and words. `what` is the noun the messages use ("gate", "subject"), so an author
// reading Health knows which list the duplicate is in. Duplicates are looked for WITHIN a list only:
// a subject sharing a gate's id is wrong in the shipped file, and core_pack_test says so, but it is
// not a load error, because the two lookups are separate and one cannot shadow the other.
void validateWords(ValidationReport &r, const std::vector<GateWord> &list, const char *what)
{
    const QString noun = QString::fromLatin1(what);
    QSet<QString> seen;
    for (const GateWord &w : list) {
        if (w.id.isEmpty()) {
            add(r, IssueSeverity::Error, QStringLiteral("duplicateId"), w.golfer,
                QStringLiteral("A %1 in the gate words has no id.").arg(noun));
            continue;
        }
        if (seen.contains(w.id))
            add(r, IssueSeverity::Error, QStringLiteral("duplicateId"), w.id,
                QStringLiteral("Duplicate %1 id '%2' in the gate words. Each id has one set of words, "
                               "or the report would print whichever came first.").arg(noun, w.id));
        seen.insert(w.id);

        const QString problem = gateWordsProblem(w.golfer);
        if (!problem.isEmpty())
            warn(r, QStringLiteral("gateWordsWording"), w.id, gateWordsMessage(w.id, w.golfer, problem));
    }
}

} // namespace

const GateWord *GateWordSet::gate(const QString &gateId) const { return findWord(gates, gateId); }
const GateWord *GateWordSet::subject(const QString &subjectId) const { return findWord(subjects, subjectId); }

QString gateWordsFor(const GateWordSet &set, const QString &gateId)
{
    const GateWord *w = set.gate(gateId);
    return w ? w->golfer : QString();
}

QString subjectWordsFor(const GateWordSet &set, const QString &subjectId)
{
    const GateWord *w = set.subject(subjectId);
    return w ? w->golfer : QString();
}

// The two rules report_model_test held the compiled table to, and nothing more. A full stop, a length
// or a causal word were each considered and left out: the shipped clauses were written to the
// header's rules and to no others, and a rule that fires on shipped content nobody has reviewed is a
// rule that gets switched off rather than obeyed.
QString gateWordsProblem(const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty()) return QStringLiteral("no words");
    if (s.front().isUpper()) return QStringLiteral("a capital first letter");
    for (const QChar ch : s)
        if (ch.isDigit()) return QStringLiteral("a number");
    return QString();
}

QString gateWordsMessage(const QString &id, const QString &text, const QString &problem)
{
    if (problem.isEmpty()) return QString();
    if (problem == QLatin1String("no words"))
        return QStringLiteral("'%1' has no golfer words, so the report leaves out the line that says "
                              "why it could not be checked.").arg(id);
    return QStringLiteral("'%1' golfer words \"%2\" have %3. They follow the reading's name in the "
                          "report's own sentence, so they start in lower case and carry no numbers: "
                          "\"the cameras lost track of you for a moment just there\".")
        .arg(id, text, problem);
}

ValidationReport validateGateWordSet(const GateWordSet &set)
{
    ValidationReport r;
    validateWords(r, set.gates, "gate");
    validateWords(r, set.subjects, "subject");
    return r;
}

GateWordLoadResult loadGateWordSet(const QByteArray &json, const QString &sourceLabel)
{
    GateWordLoadResult out;

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        add(out.report, IssueSeverity::Error, QStringLiteral("parse"), sourceLabel,
            QStringLiteral("Could not parse the gate words '%1': %2")
                .arg(sourceLabel, pe.errorString()));
        return out;
    }

    const QJsonObject root   = doc.object();
    const int         schema = root.value(QStringLiteral("schemaVersion")).toInt(kGateWordSchemaVersion);

    // Refused rather than partially read — see the same gate in loadScreenSet(), and loadPack()
    // before it. `parsed` stays false: a caller merging layers must not take a file this build
    // could not read for one it did.
    if (schema > kGateWordSchemaVersion) {
        err(out.report, QStringLiteral("schemaTooNew"), sourceLabel,
            QStringLiteral("Gate words '%1' declare schema %2; this build understands %3. "
                           "Refusing to load rather than silently dropping content.")
                .arg(sourceLabel.isEmpty() ? QStringLiteral("(unnamed)") : sourceLabel)
                .arg(schema).arg(kGateWordSchemaVersion));
        return out;
    }

    out.pack.id            = root.value(QStringLiteral("id")).toString();
    out.pack.version       = root.value(QStringLiteral("version")).toString();
    out.pack.schemaVersion = schema;
    out.pack.sourceLabel   = sourceLabel;
    out.pack.gates         = readWords(root.value(QStringLiteral("gates")));
    out.pack.subjects      = readWords(root.value(QStringLiteral("subjects")));

    out.parsed = true;
    out.report = validateGateWordSet(out.pack);
    out.loaded = out.report.ok();
    return out;
}

// The shipped file's `_comment` is not carried: it is the authors' note on the shipped content, and a
// user layer written by this function holds only the user's own entries — copying the note into it
// would describe content the file does not contain.
QByteArray saveGateWordSet(const GateWordSet &set)
{
    QJsonObject root;
    root.insert(QStringLiteral("id"), set.id);
    root.insert(QStringLiteral("version"), set.version);
    root.insert(QStringLiteral("schemaVersion"), set.schemaVersion);
    root.insert(QStringLiteral("gates"), writeWords(set.gates));
    root.insert(QStringLiteral("subjects"), writeWords(set.subjects));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

namespace {

GateWordSet loadCoreGateWordSet()
{
    const QByteArray override = qgetenv("PINPOINT_CORE_GATE_WORDS");
    const QString    corePath = override.isEmpty() ? QStringLiteral(":/diagnostics/gate_words.json")
                                                   : QString::fromLocal8Bit(override);

    GateWordSet out;
    QFile       f(corePath);
    if (f.open(QIODevice::ReadOnly)) {
        GateWordLoadResult res = loadGateWordSet(f.readAll(), corePath);
        out = std::move(res.pack);
    }
    out.readOnly = true;
    return out;
}

// Each list merged on its own, by id: a user entry replaces core's, and one core does not carry is
// appended. Two lists rather than one keyed space, because a subject and a gate are looked up by
// different callers for different halves of one line.
void overlay(std::vector<GateWord> &into, const std::vector<GateWord> &from)
{
    for (const GateWord &w : from) {
        auto it = std::find_if(into.begin(), into.end(),
                               [&](const GateWord &x) { return x.id == w.id; });
        if (it != into.end()) *it = w;
        else                  into.push_back(w);
    }
}

GateWordSet assembleGateWordSet()
{
    GateWordSet       out  = loadCoreGateWordSet();
    const GateWordSet user = loadUserGateWordSet();
    if (!user.gates.empty() || !user.subjects.empty()) {
        overlay(out.gates, user.gates);
        overlay(out.subjects, user.subjects);
        out.readOnly = false;
    }
    return out;
}

GateWordSet *g_gateWords = nullptr;
GateWordSet *g_core      = nullptr;

} // namespace

// The two layers apart — see screen_pack.h for why an editor needs them that way.
const GateWordSet &coreGateWordSet()
{
    if (!g_core) g_core = new GateWordSet(loadCoreGateWordSet());
    return *g_core;
}

QString userGateWordSetPath() { return userGateWordsPath(); }

GateWordSet loadUserGateWordSet()
{
    // `parsed`, not `loaded`, for the same reason the screen layer is read that way.
    const QString path = userGateWordsPath();
    QFile         f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    GateWordLoadResult res = loadGateWordSet(f.readAll(), path);
    return res.parsed ? std::move(res.pack) : GateWordSet{};
}

bool saveUserGateWordSet(const GateWordSet &set, QString *whyNot)
{
    return atomicWrite(userGateWordSetPath(), saveGateWordSet(set), whyNot);
}

const GateWordSet &sharedGateWordSet()
{
    if (!g_gateWords) g_gateWords = new GateWordSet(assembleGateWordSet());
    return *g_gateWords;
}

void resetSharedGateWordSet()
{
    delete g_gateWords;
    g_gateWords = nullptr;
}

} // namespace pinpoint::analysis
