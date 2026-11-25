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

#include "screen_pack.h"
#include "layered_set.h"

#include "../engine/pack_io.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace pinpoint::analysis {

const Screen *ScreenSet::screen(const QString &sid) const &
{
    const auto it = std::find_if(screens.begin(), screens.end(),
                                 [&](const Screen &s) { return s.id == sid; });
    return it == screens.end() ? nullptr : &*it;
}

namespace {

QString userScreenPath() { return diagnosticsFilePath(QLatin1String(kScreenSetFileName)); }

} // namespace

ValidationReport validateScreenSet(const ScreenSet &set)
{
    ValidationReport r;
    QSet<QString>    seen;

    for (const Screen &s : set.screens) {
        if (s.id.isEmpty()) {
            add(r, IssueSeverity::Error, QStringLiteral("duplicateId"), s.label,
                QStringLiteral("A screen has no id."));
            continue;
        }
        if (seen.contains(s.id))
            add(r, IssueSeverity::Error, QStringLiteral("duplicateId"), s.id,
                QStringLiteral("Duplicate screen id '%1'. Ids are permanent and must be unique.")
                    .arg(s.id));
        seen.insert(s.id);

        // The join with Condition::screenRef is an exact string match, so an id outside the
        // namespace matches nothing and fails silently — which is the worst kind of content bug,
        // because the screen LOOKS authored.
        if (!s.id.startsWith(QStringLiteral("screen.")))
            add(r, IssueSeverity::Error, QStringLiteral("screenIdNamespace"), s.id,
                QStringLiteral("Screen id '%1' is outside the `screen.` namespace, so nothing in "
                               "the library can point at it.").arg(s.id));

        if (s.passAtLeast.has_value() && s.unit.isEmpty())
            add(r, IssueSeverity::Error, QStringLiteral("screenUnitMissing"), s.id,
                QStringLiteral("Screen '%1' states a pass floor of %2 with no unit.")
                    .arg(s.id).arg(*s.passAtLeast));

        if (s.protocol.trimmed().isEmpty())
            add(r, IssueSeverity::Warning, QStringLiteral("screenNoProtocol"), s.id,
                QStringLiteral("'%1' does not say how to run it, so nobody can.").arg(s.id));

        if (s.passCriterion.trimmed().isEmpty())
            add(r, IssueSeverity::Warning, QStringLiteral("screenNoPass"), s.id,
                QStringLiteral("'%1' does not say what passing looks like, so the answer cannot be "
                               "recorded either way.").arg(s.id));
    }
    return r;
}

ScreenLoadResult loadScreenSet(const QByteArray &json, const QString &sourceLabel)
{
    ScreenLoadResult out;

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        add(out.report, IssueSeverity::Error, QStringLiteral("parse"), sourceLabel,
            QStringLiteral("Could not parse the screen set '%1': %2")
                .arg(sourceLabel, pe.errorString()));
        return out;
    }

    const QJsonObject root   = doc.object();
    const int         schema = root.value(QStringLiteral("schemaVersion")).toInt(kScreenSchemaVersion);

    // Refused rather than partially read, exactly as loadPack() refuses a newer characteristic
    // pack: dropping keys this build has never heard of would let a newer set round-trip through an
    // older one and lose them silently. `parsed` stays false — the bytes were JSON, but nothing
    // here is a screen set this build can claim to have read, and a caller merging layers must not
    // take it for one.
    if (schema > kScreenSchemaVersion) {
        err(out.report, QStringLiteral("schemaTooNew"), sourceLabel,
            QStringLiteral("Screen set '%1' declares schema %2; this build understands %3. "
                           "Refusing to load rather than silently dropping content.")
                .arg(sourceLabel.isEmpty() ? QStringLiteral("(unnamed)") : sourceLabel)
                .arg(schema).arg(kScreenSchemaVersion));
        return out;
    }

    out.pack.id            = root.value(QStringLiteral("id")).toString();
    out.pack.version       = root.value(QStringLiteral("version")).toString();
    out.pack.schemaVersion = schema;
    out.pack.sourceLabel   = sourceLabel;

    for (const QJsonValue &v : root.value(QStringLiteral("screens")).toArray()) {
        const QJsonObject o = v.toObject();
        Screen            s;
        s.id            = o.value(QStringLiteral("id")).toString();
        s.label         = o.value(QStringLiteral("label")).toString();
        s.bodyRegion    = o.value(QStringLiteral("bodyRegion")).toString();
        s.protocol      = o.value(QStringLiteral("protocol")).toString();
        s.passCriterion = o.value(QStringLiteral("passCriterion")).toString();
        s.unit          = o.value(QStringLiteral("unit")).toString();
        s.citation      = o.value(QStringLiteral("citation")).toString();
        s.note          = o.value(QStringLiteral("note")).toString();
        if (o.value(QStringLiteral("passAtLeast")).isDouble())
            s.passAtLeast = o.value(QStringLiteral("passAtLeast")).toDouble();
        out.pack.screens.push_back(std::move(s));
    }

    out.parsed = true;
    out.report = validateScreenSet(out.pack);
    out.loaded = out.report.ok();
    return out;
}

QByteArray saveScreenSet(const ScreenSet &set)
{
    QJsonObject root;
    root.insert(QStringLiteral("id"), set.id);
    root.insert(QStringLiteral("version"), set.version);
    root.insert(QStringLiteral("schemaVersion"), set.schemaVersion);

    QJsonArray arr;
    for (const Screen &s : set.screens) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), s.id);
        o.insert(QStringLiteral("label"), s.label);
        if (!s.bodyRegion.isEmpty()) o.insert(QStringLiteral("bodyRegion"), s.bodyRegion);
        o.insert(QStringLiteral("protocol"), s.protocol);
        o.insert(QStringLiteral("passCriterion"), s.passCriterion);
        if (s.passAtLeast.has_value()) o.insert(QStringLiteral("passAtLeast"), *s.passAtLeast);
        if (!s.unit.isEmpty())     o.insert(QStringLiteral("unit"), s.unit);
        if (!s.citation.isEmpty()) o.insert(QStringLiteral("citation"), s.citation);
        if (!s.note.isEmpty())     o.insert(QStringLiteral("note"), s.note);
        arr.append(o);
    }
    root.insert(QStringLiteral("screens"), arr);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ContentLayers<ScreenSet> assembleScreenLayers(const RuleFile &core, const RuleFile &user)
{
    ContentLayers<ScreenSet> out;
    out.core   = detail::shippedContent<ScreenSet>(core, &loadScreenSet, QStringLiteral("screen set"), out.report);
    out.user   = detail::userContent<ScreenSet>(user, &loadScreenSet, QStringLiteral("user screen set"), out.report);
    out.merged = out.core;
    // The user's own layer, by id. A user row REPLACES the shipped one rather than merging field by
    // field: a half-overridden protocol describing one movement with another's pass criterion would be
    // worse than either.
    if (!out.user.screens.empty()) {
        overlayById(out.merged.screens, out.user.screens);
        out.merged.readOnly = false;
    }
    return out;
}

QString userScreenSetPath() { return userScreenPath(); }

} // namespace pinpoint::analysis
