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

// WHAT MEASURED THE BALL, APART FROM HOW THE NUMBERS GOT HERE. Header-only, Qt Core only.
//
// Two facts, never one:
//   CONNECTOR — the software route a reading arrived by: FSX2020's LastShot.CSV, Foresight
//               Premier's session files, a GSPro Open Connect client. It is the settings token
//               (`launchmonitor/kind`) and `launchMonitor.kind` in a swing document, and it
//               decides how a reading is parsed, what the import can drop, and which gates apply.
//   DEVICE    — the hardware that measured the shot: a GC Quad, an Approach R10. It is
//               `launchMonitor.device`, and it is what every name, tag and filter shows.
//
// One device reaches PinPoint through several connectors (a GC Quad through FSX2020, through
// Premier, through GSPro), and one connector carries many devices (GSPro Open Connect relays
// R10s, MLM2PROs, SkyTraks and Quads). So a device is never inferred from a connector's NAME
// anywhere else: every place that names, tags or filters on a device asks this file.
//
// ADDING ONE: a new device is a row in kDevices; a new connector is a row in kConnectors (with
// its fixed device, or none if its clients name themselves). Nothing else changes.
//
// THE KEY SPACES SHARE ONE NAMESPACE, deliberately: a device key and a connector token are both
// short lower-case words, and a stored key may be either — a summary block written before
// `launchMonitor.device` existed holds the connector ("lm:premier"), and a reading whose device
// could not be identified is keyed by its connector ("lm:gspro": some device, via GSPro).
// canonicalDeviceKey() maps both onto the device. That only holds while a connector token that is
// ALSO a device key reads exactly that device ("gcquad" → gcquad); lm_device_test asserts it.

#include <QJsonObject>
#include <QString>

#include <iterator>

namespace pinpoint::lm {

struct DeviceDef {
    const char *key;      // stable: stored in launchMonitor.device and the "lm:<key>" device token
    const char *name;     // what the app calls it, everywhere
    const char *maker;    // "Foresight"; "" when the table does not say
    const char *markers;  // the club-sticker layout the panel draws: "four", "one", "" for none
    const char *aliases;  // '|'-separated, in normaliseDeviceName() form, matched as substrings of
                          // what a client calls itself (GSPro's DeviceID)
};

// ORDER MATTERS: the first row whose alias appears in a reported name wins, so a name that
// contains another's alias comes first ("quadmax" before "quad", "skytrak+" before "skytrak").
// Every row's own `name` must identify that row (lm_device_test), so the table also reads back
// the names it hands out — the live board identifies its device from the controller's name.
inline constexpr DeviceDef kDevices[] = {
    // Foresight. Club-marker layouts: four dots on the Quad family, one on the GC3 and an HMT.
    { "quadmax",     "QuadMAX",        "Foresight",   "four", "quadmax" },
    { "gcquad",      "GC Quad",        "Foresight",   "four", "gcquad|quad" },
    { "falcon",      "Falcon",         "Foresight",   "four", "falcon" },
    { "gchawk",      "GCHawk",         "Foresight",   "four", "gchawk|hawk" },
    { "gc3",         "GC3",            "Foresight",   "one",  "gc3" },
    { "gc2hmt",      "GC2 + HMT",      "Foresight",   "one",  "hmt" },
    { "gc2",         "GC2",            "Foresight",   "",     "gc2" },
    { "launchpro",   "Launch Pro",     "Bushnell",    "",     "launchpro|bushnell" },
    // The Open Connect bridges (launch_monitor_factory.cpp's GSPro label names the first three).
    { "r10",         "Approach R10",   "Garmin",      "",     "r10" },
    { "r50",         "Approach R50",   "Garmin",      "",     "r50" },
    { "mlm2pro",     "MLM2PRO",        "Rapsodo",     "",     "mlm2pro" },
    { "skytrakplus", "SkyTrak+",       "SkyTrak",     "",     "skytrak+|skytrakplus" },
    { "skytrak",     "SkyTrak",        "SkyTrak",     "",     "skytrak" },
    { "mevoplus",    "Mevo+",          "FlightScope", "",     "mevo+|mevoplus" },
    { "squaregolf",  "Square Golf",    "Square Golf", "",     "squaregolf" },
    { "nova",        "Nova",           "Open Launch", "",     "nova" },   // docs/design/nova_launch_monitor.md
};

struct ConnectorDef {
    const char *kind;     // the settings token, launchMonitor.kind
    const char *name;     // the program, for "Launch monitor (GSPro)" when the device is unknown
    const char *device;   // the device it always reads; "" when each client names its own
};

inline constexpr ConnectorDef kConnectors[] = {
    { "gcquad",  "FSX2020",           "gcquad" },
    { "premier", "Foresight Premier", "gcquad" },
    { "gspro",   "GSPro",             ""       },   // any Open Connect client: it says which per shot
};

inline const DeviceDef *findDevice(const QString &key)
{
    for (const DeviceDef &d : kDevices)
        if (key == QLatin1String(d.key)) return &d;
    return nullptr;
}

inline const ConnectorDef *findConnector(const QString &kind)
{
    for (const ConnectorDef &c : kConnectors)
        if (kind == QLatin1String(c.kind)) return &c;
    return nullptr;
}

// "Garmin R10 (GSPro Connect)" → "garminr10gsproconnect": lower case, letters, digits and '+' only,
// so spacing, case and punctuation never decide a match and "SkyTrak+" stays apart from "SkyTrak".
inline QString normaliseDeviceName(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s.toLower())
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'+')
            out.append(c);
    return out;
}

// The device a client's own name for itself identifies, or "" when the table knows none.
inline QString deviceKeyFromReported(const QString &reported)
{
    const QString n = normaliseDeviceName(reported);
    if (n.isEmpty()) return {};
    for (const DeviceDef &d : kDevices)
        for (const QString &a : QString::fromLatin1(d.aliases).split(u'|', Qt::SkipEmptyParts))
            if (n.contains(a)) return QString::fromLatin1(d.key);
    return {};
}

// AT IMPORT: the device to record for a reading. What the client said it is, when the table
// recognises it; else the device the connector always reads; else "" — not known, and nothing is
// written rather than a guess.
inline QString resolveDeviceKey(const QString &connector, const QString &reported = {})
{
    const QString fromName = deviceKeyFromReported(reported);
    if (!fromName.isEmpty()) return fromName;
    const ConnectorDef *c = findConnector(connector);
    return c ? QString::fromLatin1(c->device) : QString();
}

// AT READ: a stored key — a device key, or a connector token from an older summary or a reading
// whose device was not identified — onto the device it means. A connector with no fixed device
// stays itself ("gspro": some device, via GSPro); anything unknown passes through unchanged.
inline QString canonicalDeviceKey(const QString &key)
{
    if (key.isEmpty() || findDevice(key)) return key;
    if (const ConnectorDef *c = findConnector(key); c && *c->device)
        return QString::fromLatin1(c->device);
    return key;
}

// The device of a swing document's `launchMonitor` block: its recorded device, else what its
// connector implies. "" when the block is empty.
inline QString deviceKeyOfBlock(const QJsonObject &lm)
{
    const QString device = lm.value(QStringLiteral("device")).toString();
    return canonicalDeviceKey(device.isEmpty() ? lm.value(QStringLiteral("kind")).toString() : device);
}

// The name every panel, tag, chip and report shows. "" for "" (no device), so a caller can join
// it into a list unguarded; "Launch monitor (GSPro)" for a device known only by its connector.
inline QString deviceName(const QString &key)
{
    const QString k = canonicalDeviceKey(key);
    if (k.isEmpty()) return {};
    if (const DeviceDef *d = findDevice(k)) return QString::fromUtf8(d->name);
    if (const ConnectorDef *c = findConnector(k))
        return QStringLiteral("Launch monitor (%1)").arg(QString::fromUtf8(c->name));
    return QStringLiteral("Launch monitor");
}

// True when the key names a device in the table — false for "" and for a device known only by its
// connector. For sentences that read "measured by a GC Quad" and must not read "by a Launch monitor
// (GSPro)": they say "the launch monitor" instead.
inline bool isIdentifiedDevice(const QString &key)
{
    return findDevice(canonicalDeviceKey(key)) != nullptr;
}

inline QString deviceMaker(const QString &key)
{
    const DeviceDef *d = findDevice(canonicalDeviceKey(key));
    return d ? QString::fromUtf8(d->maker) : QString();
}

inline QString deviceMarkers(const QString &key)
{
    const DeviceDef *d = findDevice(canonicalDeviceKey(key));
    return d ? QString::fromLatin1(d->markers) : QString();
}

// The session lists' device token ("lm:<key>", session_summary.h) onto its device; any other
// token unchanged. Applied where shots are gathered into a session, so every list and filter agrees.
inline QString canonicalDeviceToken(const QString &token)
{
    if (!token.startsWith(QLatin1String("lm:"))) return token;
    return QStringLiteral("lm:") + canonicalDeviceKey(token.mid(3));
}

} // namespace pinpoint::lm
