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

// THE TRUST VIEWS' TYPED RESULTS (refactor 5.5b; lint A6). TrustPresenter's `shot` and `session`, and TrustWords'
// `gateFor`, hand QML these Q_GADGET values instead of maps. Every property keeps the name of the key it replaces
// (trust_readings.h, which still says each shape once, as maps, for its C++ callers), so QML reads a field as it read
// the key. A list is a QList of the gadget, which QML reads as a sequence (`length`, `[i]`, `filter`, `map`, a
// Repeater's `modelData`).
//
// Each value is built from trust_readings.h's map by `fromMap`, the one place the map becomes the type. A key the map
// leaves out keeps the gadget's default ("" / false / 0 / empty list): every QML reader of these fields already
// treats an absent key and an empty one alike (`|| ""`, `|| []`, `!!x.words`).

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace pinpoint::trust {

// One gate behind a reading, in the catalogue's words (gate_catalogue.h gateItem).
struct TrustGateItem
{
    Q_GADGET
    QML_VALUE_TYPE(trustGateItem)
    Q_PROPERTY(QString gate       MEMBER gate       FINAL)
    Q_PROPERTY(QString effect     MEMBER effect     FINAL)
    Q_PROPERTY(QString effectWord MEMBER effectWord FINAL)
    Q_PROPERTY(QString title      MEMBER title      FINAL)
    Q_PROPERTY(QString evidence   MEMBER evidence   FINAL)   // the filled sentence
    Q_PROPERTY(QString explain    MEMBER explain    FINAL)
    Q_PROPERTY(QString tuning     MEMBER tuning     FINAL)   // "Threshold 20°"
    Q_PROPERTY(QString lift       MEMBER lift       FINAL)   // "Timed on swings with …"
    Q_PROPERTY(bool    primary    MEMBER primary    FINAL)   // the record the claim shows
public:
    QString gate, effect, effectWord, title, evidence, explain, tuning, lift;
    bool    primary = false;

    static TrustGateItem fromMap(const QVariantMap &m)
    {
        TrustGateItem g;
        g.gate       = m.value(QStringLiteral("gate")).toString();
        g.effect     = m.value(QStringLiteral("effect")).toString();
        g.effectWord = m.value(QStringLiteral("effectWord")).toString();
        g.title      = m.value(QStringLiteral("title")).toString();
        g.evidence   = m.value(QStringLiteral("evidence")).toString();
        g.explain    = m.value(QStringLiteral("explain")).toString();
        g.tuning     = m.value(QStringLiteral("tuning")).toString();
        g.lift       = m.value(QStringLiteral("lift")).toString();
        g.primary    = m.value(QStringLiteral("primary")).toBool();
        return g;
    }

    friend bool operator==(const TrustGateItem &, const TrustGateItem &) = default;
};

// One reading a gate touched (trust_readings.h READING).
struct TrustReading
{
    Q_GADGET
    QML_VALUE_TYPE(trustReading)
    Q_PROPERTY(QString              subject      MEMBER subject      FINAL)   // "ks.pelvis"
    // a kinematic-sequence node's reading (its subject is "ks.<segment>"): the readings a sequence claim opens when it
    // names no one subject (Charts' This shot, rule C4). Not `area`: a route's reading shares that crosshead.
    Q_PROPERTY(bool                 sequenceNode MEMBER sequenceNode FINAL)
    Q_PROPERTY(QString              area         MEMBER area         FINAL)
    Q_PROPERTY(QStringList          covers       MEMBER covers       FINAL)   // a cascade's readings it took down
    Q_PROPERTY(QString              label        MEMBER label        FINAL)
    Q_PROPERTY(QString              key          MEMBER key          FINAL)   // the series key, for the reading's hue
    Q_PROPERTY(QString              level        MEMBER level        FINAL)
    Q_PROPERTY(QString              levelWord    MEMBER levelWord    FINAL)
    Q_PROPERTY(QString              levelTip     MEMBER levelTip     FINAL)
    Q_PROPERTY(QString              figure       MEMBER figure       FINAL)   // "after −129 ms" | "-"
    Q_PROPERTY(QString              shortText    MEMBER shortText    FINAL)
    Q_PROPERTY(QList<TrustGateItem> gates        MEMBER gates        FINAL)
    Q_PROPERTY(QStringList          blocks       MEMBER blocks       FINAL)   // what it kept from being judged
public:
    QString              subject;
    bool                 sequenceNode = false;
    QString              area;
    QStringList          covers;
    QString              label, key, level, levelWord, levelTip, figure, shortText;
    QList<TrustGateItem> gates;
    QStringList          blocks;

    static TrustReading fromMap(const QVariantMap &m)
    {
        TrustReading r;
        r.subject      = m.value(QStringLiteral("subject")).toString();
        r.sequenceNode = m.value(QStringLiteral("sequenceNode")).toBool();
        r.area         = m.value(QStringLiteral("area")).toString();
        r.covers       = m.value(QStringLiteral("covers")).toStringList();
        r.label        = m.value(QStringLiteral("label")).toString();
        r.key          = m.value(QStringLiteral("key")).toString();
        r.level        = m.value(QStringLiteral("level")).toString();
        r.levelWord    = m.value(QStringLiteral("levelWord")).toString();
        r.levelTip     = m.value(QStringLiteral("levelTip")).toString();
        r.figure       = m.value(QStringLiteral("figure")).toString();
        r.shortText    = m.value(QStringLiteral("shortText")).toString();
        for (const QVariant &g : m.value(QStringLiteral("gates")).toList()) r.gates << TrustGateItem::fromMap(g.toMap());
        r.blocks       = m.value(QStringLiteral("blocks")).toStringList();
        return r;
    }

    friend bool operator==(const TrustReading &, const TrustReading &) = default;
};

// One line of "How far can I trust this?" (trust_readings.h SUMMARY's lines). `subject` / `swingDir` on a gated line
// name the reading its "Why" opens.
struct TrustLine
{
    Q_GADGET
    QML_VALUE_TYPE(trustLine)
    Q_PROPERTY(QString text     MEMBER text     FINAL)
    Q_PROPERTY(QString kind     MEMBER kind     FINAL)   // "routes" | "passed" | "gated" | "masked" | "old"
    Q_PROPERTY(QString subject  MEMBER subject  FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
public:
    QString text, kind, subject, swingDir;

    static TrustLine fromMap(const QVariantMap &m)
    {
        TrustLine l;
        l.text     = m.value(QStringLiteral("text")).toString();
        l.kind     = m.value(QStringLiteral("kind")).toString();
        l.subject  = m.value(QStringLiteral("subject")).toString();
        l.swingDir = m.value(QStringLiteral("swingDir")).toString();
        return l;
    }

    friend bool operator==(const TrustLine &, const TrustLine &) = default;
};

// "HOW FAR CAN I TRUST THIS?" for a shot or a session (trust_readings.h SUMMARY).
struct TrustSummary
{
    Q_GADGET
    QML_VALUE_TYPE(trustSummary)
    Q_PROPERTY(bool             recorded MEMBER recorded FINAL)
    Q_PROPERTY(QString          tail     MEMBER tail     FINAL)   // the closed fold's few words
    Q_PROPERTY(QList<TrustLine> lines    MEMBER lines    FINAL)
public:
    bool             recorded = false;
    QString          tail;
    QList<TrustLine> lines;

    static TrustSummary fromMap(const QVariantMap &m)
    {
        TrustSummary s;
        s.recorded = m.value(QStringLiteral("recorded")).toBool();
        s.tail     = m.value(QStringLiteral("tail")).toString();
        for (const QVariant &l : m.value(QStringLiteral("lines")).toList()) s.lines << TrustLine::fromMap(l.toMap());
        return s;
    }

    friend bool operator==(const TrustSummary &, const TrustSummary &) = default;
};

// How many of a shot's readings stand at each level a gate brought them down to.
struct TrustCounts
{
    Q_GADGET
    QML_VALUE_TYPE(trustCounts)
    Q_PROPERTY(int bounded   MEMBER bounded   FINAL)
    Q_PROPERTY(int notSeen   MEMBER notSeen   FINAL)
    Q_PROPERTY(int notJudged MEMBER notJudged FINAL)
public:
    int bounded = 0, notSeen = 0, notJudged = 0;

    static TrustCounts fromMap(const QVariantMap &m)
    {
        TrustCounts c;
        c.bounded   = m.value(QStringLiteral("bounded")).toInt();
        c.notSeen   = m.value(QStringLiteral("notSeen")).toInt();
        c.notJudged = m.value(QStringLiteral("notJudged")).toInt();
        return c;
    }

    friend bool operator==(const TrustCounts &, const TrustCounts &) = default;
};

// The picked shot, for the panel (TrustPresenter.shot): trust_readings.h trustShot, and the presenter's own
// `available`, `ordinal`, `kicker` ("Shot 4 of 6  ·  7 iron") and `swingDir`. Nothing picked: `available` false and
// every other field at its default.
struct TrustShot
{
    Q_GADGET
    QML_VALUE_TYPE(trustShot)
    Q_PROPERTY(bool                available MEMBER available FINAL)
    Q_PROPERTY(int                 ordinal   MEMBER ordinal   FINAL)
    Q_PROPERTY(QString             kicker    MEMBER kicker    FINAL)
    Q_PROPERTY(QString             swingDir  MEMBER swingDir  FINAL)
    Q_PROPERTY(bool                recorded  MEMBER recorded  FINAL)
    Q_PROPERTY(QList<TrustReading> readings  MEMBER readings  FINAL)
    Q_PROPERTY(QString             head      MEMBER head      FINAL)   // the summary's lines as one paragraph
    Q_PROPERTY(TrustSummary        summary   MEMBER summary   FINAL)
    Q_PROPERTY(TrustCounts         counts    MEMBER counts    FINAL)
public:
    bool                available = false;
    int                 ordinal = 0;
    QString             kicker, swingDir;
    bool                recorded = false;
    QList<TrustReading> readings;
    QString             head;
    TrustSummary        summary;
    TrustCounts         counts;

    // trust_readings.h trustShot's fields; the presenter sets its own four.
    static TrustShot fromMap(const QVariantMap &m)
    {
        TrustShot s;
        s.recorded = m.value(QStringLiteral("recorded")).toBool();
        for (const QVariant &r : m.value(QStringLiteral("readings")).toList()) s.readings << TrustReading::fromMap(r.toMap());
        s.head     = m.value(QStringLiteral("head")).toString();
        s.summary  = TrustSummary::fromMap(m.value(QStringLiteral("summary")).toMap());
        s.counts   = TrustCounts::fromMap(m.value(QStringLiteral("counts")).toMap());
        return s;
    }

    friend bool operator==(const TrustShot &, const TrustShot &) = default;
};

// One reading a gate touched across the session, and on how many shots.
struct TrustSessionSubject
{
    Q_GADGET
    QML_VALUE_TYPE(trustSessionSubject)
    Q_PROPERTY(QString subject MEMBER subject FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(int     shots   MEMBER shots   FINAL)
public:
    QString subject, label;
    int     shots = 0;

    static TrustSessionSubject fromMap(const QVariantMap &m)
    {
        TrustSessionSubject s;
        s.subject = m.value(QStringLiteral("subject")).toString();
        s.label   = m.value(QStringLiteral("label")).toString();
        s.shots   = m.value(QStringLiteral("shots")).toInt();
        return s;
    }

    friend bool operator==(const TrustSessionSubject &, const TrustSessionSubject &) = default;
};

// One gate across the session: how often it fired, per reading.
struct TrustSessionGate
{
    Q_GADGET
    QML_VALUE_TYPE(trustSessionGate)
    Q_PROPERTY(QString                    gate      MEMBER gate      FINAL)
    Q_PROPERTY(QString                    title     MEMBER title     FINAL)
    Q_PROPERTY(QString                    area      MEMBER area      FINAL)
    Q_PROPERTY(bool                       acted     MEMBER acted     FINAL)   // false: it only marked the chart
    Q_PROPERTY(QList<TrustSessionSubject> subjects  MEMBER subjects  FINAL)
    Q_PROPERTY(int                        shots     MEMBER shots     FINAL)   // shots it fired on (any subject)
    Q_PROPERTY(int                        of        MEMBER of        FINAL)   // shots carrying records
    Q_PROPERTY(QString                    countText MEMBER countText FINAL)   // "3 of 5"
public:
    QString                    gate, title, area;
    bool                       acted = false;
    QList<TrustSessionSubject> subjects;
    int                        shots = 0, of = 0;
    QString                    countText;

    static TrustSessionGate fromMap(const QVariantMap &m)
    {
        TrustSessionGate g;
        g.gate      = m.value(QStringLiteral("gate")).toString();
        g.title     = m.value(QStringLiteral("title")).toString();
        g.area      = m.value(QStringLiteral("area")).toString();
        g.acted     = m.value(QStringLiteral("acted")).toBool();
        for (const QVariant &s : m.value(QStringLiteral("subjects")).toList()) g.subjects << TrustSessionSubject::fromMap(s.toMap());
        g.shots     = m.value(QStringLiteral("shots")).toInt();
        g.of        = m.value(QStringLiteral("of")).toInt();
        g.countText = m.value(QStringLiteral("countText")).toString();
        return g;
    }

    friend bool operator==(const TrustSessionGate &, const TrustSessionGate &) = default;
};

// The session, for the panel (TrustPresenter.session): trust_readings.h trustSession. No session read: `n` 0.
struct TrustSession
{
    Q_GADGET
    QML_VALUE_TYPE(trustSession)
    Q_PROPERTY(int                     n        MEMBER n        FINAL)
    Q_PROPERTY(int                     recorded MEMBER recorded FINAL)   // shots carrying records
    Q_PROPERTY(QList<TrustSessionGate> gates    MEMBER gates    FINAL)
    Q_PROPERTY(QString                 head     MEMBER head     FINAL)
    Q_PROPERTY(TrustSummary            summary  MEMBER summary  FINAL)
public:
    int                     n = 0, recorded = 0;
    QList<TrustSessionGate> gates;
    QString                 head;
    TrustSummary            summary;

    static TrustSession fromMap(const QVariantMap &m)
    {
        TrustSession s;
        s.n        = m.value(QStringLiteral("n")).toInt();
        s.recorded = m.value(QStringLiteral("recorded")).toInt();
        for (const QVariant &g : m.value(QStringLiteral("gates")).toList()) s.gates << TrustSessionGate::fromMap(g.toMap());
        s.head     = m.value(QStringLiteral("head")).toString();
        s.summary  = TrustSummary::fromMap(m.value(QStringLiteral("summary")).toMap());
        return s;
    }

    friend bool operator==(const TrustSession &, const TrustSession &) = default;
};

// THE GATE BEHIND ONE READING, for a surface with no presenter of its own (TrustWords.gateFor; trust_readings.h
// gateFor). No gate acted on the reading: every field empty, so `words` is "" where the map had no key.
struct TrustGateHit
{
    Q_GADGET
    QML_VALUE_TYPE(trustGateHit)
    Q_PROPERTY(QString subject  MEMBER subject  FINAL)   // the record's: "track.club" for a cascade
    Q_PROPERTY(QString gate     MEMBER gate     FINAL)
    Q_PROPERTY(QString effect   MEMBER effect   FINAL)
    Q_PROPERTY(QString words    MEMBER words    FINAL)   // the gate's short words for a claim: "out of sight"
    Q_PROPERTY(QString title    MEMBER title    FINAL)
    Q_PROPERTY(QString sentence MEMBER sentence FINAL)   // the evidence, filled
public:
    QString subject, gate, effect, words, title, sentence;

    static TrustGateHit fromMap(const QVariantMap &m)
    {
        TrustGateHit h;
        h.subject  = m.value(QStringLiteral("subject")).toString();
        h.gate     = m.value(QStringLiteral("gate")).toString();
        h.effect   = m.value(QStringLiteral("effect")).toString();
        h.words    = m.value(QStringLiteral("words")).toString();
        h.title    = m.value(QStringLiteral("title")).toString();
        h.sentence = m.value(QStringLiteral("sentence")).toString();
        return h;
    }

    friend bool operator==(const TrustGateHit &, const TrustGateHit &) = default;
};

} // namespace pinpoint::trust
