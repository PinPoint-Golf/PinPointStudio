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

#include "session_diagnostics_model.h"

#include "../../Diagnostics/measures/live_measure_source.h"
#include "../../Diagnostics/measures/session_measure_source.h"
#include "../../Diagnostics/measures/measure_facets.h"
#include "../../Diagnostics/norms/norm_provider.h"
#include "../../Diagnostics/engine/condition_reading.h"
#include "../../Diagnostics/engine/pack_io.h"
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Diagnostics/packs/screen_pack.h"
#include "../../Export/swing_doc.h"
#include "../../Coaching/sequence_checks.h"
#include "../../Coaching/work_ons_pack.h"
#include "../../Metrics/gate_catalogue.h"
#include "../../Core/pp_debug.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

using namespace pinpoint::analysis;

namespace {

// The envelope's own schema tag, separate from kDiagSchemaVersion.
//
// TWO VERSIONS BECAUSE THERE ARE TWO CONTRACTS. kDiagSchemaVersion tags the ROW shape, which
// diagnostic_ledger.h owns and which a corpus tool reads without knowing this class exists.
// This one tags what the MODEL wraps around it — the focus contract, the declared miss, the
// screen answers, the session meta. They change on completely different schedules, and
// sharing one number would force a bump on every consumer whenever a panel gained a field.
constexpr int kEnvelopeVersion = 1;

// WHAT A NOT-JUDGED ROW SAYS, versioned apart from the row shape: the shape did not change when the rows
// began naming the engine rule behind a refusal and the capture gate behind an excluded shot (trust stage 6
// session 2), but a ledger written before did not fill them. Hashed into the content stamp, so such a
// ledger regrades once on activation.
constexpr int kRowWordsVersion = 1;

const QString kDiagnosticsFile = QStringLiteral("diagnostics.json");
const QString kProfileFile     = QStringLiteral("fault_profile.json");

// The true minus, U+2212. Brief §6: the value slot and the corridor slot must agree, and a
// hyphen-minus beside a typographic one in the same cell is the kind of thing a golfer reads
// as a bug in the numbers.
const QChar kMinus = QChar(0x2212);

QString fmtNumber(double v, int decimals = 1)
{
    if (!std::isfinite(v)) return QStringLiteral("—");
    QString s = QString::number(std::fabs(v), 'f', decimals);
    return (v < 0.0) ? QString(kMinus) + s : s;
}

// The corridor's shape as the ledger row records it — the one translation between
// MeasureEvidence's two open-side flags and CorridorShape, so there is exactly one place
// that decides what a missing scale means.
CorridorShape corridorShapeOf(const MeasureEvidence &e)
{
    if (!e.hasCorridor) return CorridorShape::None;              // graded against a number
    if (!(e.corridorHi - e.corridorLo > 0.0)) return CorridorShape::None;   // degenerate band
    if (e.lowOpen && e.highOpen) return CorridorShape::None;     // nothing to normalise by
    if (e.lowOpen)  return CorridorShape::Ceiling;
    if (e.highOpen) return CorridorShape::Floor;
    return CorridorShape::TwoSided;
}

// ── The strength meter's fields, written in ONE place ───────────────────────────────────
//
// Four surfaces draw this meter — the pattern card, the chain node, the review cell and the
// this-shot chip — and they draw it about the same row. Published once, as a block, so a
// card and the cell under it cannot end up disagreeing about how far out a swing was; the
// same rule that gives recurrence exactly one wording.
//
// THE MODEL DECIDES THE STEP, THE PANEL PAINTS IT. severityLevel() is the ladder and it
// lives in diagnostic_ledger.h beside the z it reads, not in QML JS (§6.2). The panel gets
// an integer 0..5, a bool saying whether there is a reading behind it, and one sentence.
//
// `strengthKnown` false is drawn as NOTHING, never as a zero: a zero is "dead centre of the
// corridor", which is the best news on the meter, and a measure the capture could not
// assess must never be able to read as good news. Same rule as rule 1, one layer up.
QString strengthPhrase(int level)
{
    switch (level) {
    case 0:  return QStringLiteral("middle of the corridor");
    case 1:  return QStringLiteral("inside the corridor");
    case 2:  return QStringLiteral("at the corridor edge");
    case 3:  return QStringLiteral("outside the corridor");
    case 4:  return QStringLiteral("well outside the corridor");
    default: return QStringLiteral("far outside the corridor");
    }
}

// ── A composite part's share, in words (posture_metrics_design.md §8) ───────────────────
//
// The composite's own number is never shown, so what a golfer reads of how the finding is made up
// is each part's SHARE of it — wᵢsᵢzᵢ / Σ (coherent) or wᵢzᵢ² / Q (distance) — as one ordinal word.
// THE LADDER LIVES HERE, beside the strength meter's, and not in QML JS: the panel gets the word.
//   ≥ kShareMost   "most of it"   — this part alone carries at least half of the finding
//   ≥ kShareSome   "some"         — a fifth or more
//   > 0            "a little"
//   < 0            "against it"   — a coherent part read the OTHER way (a compensation, §4.3)
//   not a number   ""             — a share over a zero sum, or a part not read: the cell says "-"
constexpr double kShareMost = 0.5;
constexpr double kShareSome = 0.2;

QString compositeShareWord(double share)
{
    if (!std::isfinite(share)) return QString();
    if (share < 0.0)          return QStringLiteral("against it");
    if (share >= kShareMost)  return QStringLiteral("most of it");
    if (share >= kShareSome)  return QStringLiteral("some");
    return QStringLiteral("a little");
}

// Why a composite part was not read (CompositeUnreadRow::reason), in the trust fold's words.
QString compositeUnreadWords(const QString &reason, const QString &status = QString())
{
    // A part whose measure is not live (held, planned) is not graded at all: say it is held back,
    // which is the honest reason, rather than "not measured" — the series may well be on the swing.
    if (reason == QLatin1String("notLive"))
        return status == QLatin1String("held") ? QStringLiteral("held back until it is trusted to grade")
                                               : QStringLiteral("not graded yet");
    if (reason == QLatin1String("notMeasured")) return QStringLiteral("not measured on this swing");
    if (reason == QLatin1String("noCorridor"))  return QStringLiteral("no range to judge it against");
    if (reason == QLatin1String("implausible")) return QStringLiteral("set aside as implausible");
    if (reason == QLatin1String("withheld"))    return QStringLiteral("drawn but not judged");
    if (reason == QLatin1String("nested"))      return QStringLiteral("a combined reading cannot be a part");
    return reason.isEmpty() ? QStringLiteral("not read") : reason;
}

void addStrength(QVariantMap &m, const ConditionRow *r)
{
    const RowStrength st = r ? rowStrength(*r) : RowStrength();
    const int level = st.known ? severityLevel(st.excess) : 0;

    m[QStringLiteral("strengthKnown")] = st.known;
    m[QStringLiteral("strength")]      = level;
    m[QStringLiteral("strengthExcess")]= st.known ? st.excess : 0.0;
    m[QStringLiteral("strengthSharp")] = r && r->sharp && r->state == ShotState::Fired;
    // A sharp firing has no distance to quote — the line itself is the fault (Condition::sharp).
    if (r && r->sharp && r->state == ShotState::Fired) {
        m[QStringLiteral("strengthText")] =
            QStringLiteral("%1 of 5 · a sharp finding: crossing this line is serious in itself").arg(level);
        return;
    }
    // A COMPOSITE's row keeps the meter's step and loses the number (posture_metrics_design.md §8,
    // session diagnostics principle 5): the multiple of the distance to the edge IS the composite's
    // z, and that number is never shown. The step is a position, which the meter already draws.
    const bool composite = r && r->composite.present();
    m[QStringLiteral("composite")] = composite;
    if (composite) {
        m[QStringLiteral("strengthText")] =
            st.known ? QStringLiteral("%1 of 5 · %2").arg(level).arg(strengthPhrase(level)) : QString();
        return;
    }
    // The one sentence, and it carries the number as well as the step: a meter read off a
    // tooltip should not be less precise than the receipt two lines above it. 1.0 is the
    // graded edge exactly, which is why the distance is quoted TO the edge rather than past
    // it — the same quantity either side of the band, and no sign to misread.
    m[QStringLiteral("strengthText")] =
        st.known ? QStringLiteral("%1 of 5 · %2 · %3× the distance to the corridor edge")
                       .arg(level).arg(strengthPhrase(level), fmtNumber(st.excess))
                 : QString();
}

// ── THE GATE OR RULE BEHIND A NOT-JUDGED ROW (trust stage 6 session 2) ──────────────────────
//
// A NotAssessable row names what kept it from being judged in one of two ways, and every surface shows
// the same words for it in the slot that would otherwise read "-" / "not measurable":
//   • a GATE RECORD (`sequence.sightedBand`, `shot.captureHoles`) with the reading it acted on
//     (notJudgedSubject "ks.pelvis", "shot") — the words in the link colour, a click opening that reading
//     in the Trust panel (TrustFocus), else in place;
//   • an ENGINE RULE (`rule.diag.noCorridor` …, Finding::refusal) with NO subject — a rule is decided on
//     every read and leaves no per-swing record, so the Trust panel holds no reading for it: its words,
//     quiet, and nothing to open.
// A row with neither (a plain absence, a swing analysed before records existed) keeps today's text.
struct RowGate {
    bool    on = false;
    bool    rule = false;
    QString gate, subject, words;
};

RowGate rowGate(const ConditionRow *r)
{
    RowGate g;
    if (!r || r->state != ShotState::NotAssessable || r->notJudgedGate.isEmpty()) return g;
    const pinpoint::gates::GateEntry *e = pinpoint::gates::gateEntry(r->notJudgedGate);
    if (!e) return g;
    g.on      = true;
    g.rule    = e->rule;
    g.gate    = r->notJudgedGate;
    g.subject = e->rule ? QString() : r->notJudgedSubject;
    g.words   = pinpoint::gates::claimWords(*e);
    return g;
}

// PpGateNote's shape: { subject, gate, words, swingDir } — empty map when nothing gated the row.
QVariantMap gateNoteOf(const RowGate &g, const QString &swingDir)
{
    if (!g.on) return QVariantMap();
    return { { QStringLiteral("subject"),  g.subject },
             { QStringLiteral("gate"),     g.gate },
             { QStringLiteral("words"),    g.words },
             { QStringLiteral("swingDir"), swingDir } };
}

// A cell's gate fields: `gated`, `gateWords`, `gateNote` (above), and for a record `trustSubject` /
// `trustSwingDir`, the pair the Analyse register's "Not judged … Why ›" groups key on.
void addGate(QVariantMap &m, const ConditionRow *r, const QString &swingDir)
{
    const RowGate g = rowGate(r);
    m[QStringLiteral("gated")] = g.on;
    if (!g.on) return;
    m[QStringLiteral("gateWords")] = g.words;
    m[QStringLiteral("gateNote")]  = gateNoteOf(g, swingDir);
    if (!g.subject.isEmpty()) {
        m[QStringLiteral("trustSubject")]  = g.subject;
        m[QStringLiteral("trustSwingDir")] = swingDir;
    }
}

// The clause a count over shots names a gated row by: a record on one reading by its own clause
// ("pelvis peak out of sight"); the whole shot or a rule by the short words ("frames missing", "no range").
QString gatedClause(const ConditionRow &r, const RowGate &g)
{
    if (g.rule || g.subject == QLatin1String("shot") || r.notAssessableReason.isEmpty()) return g.words;
    return r.notAssessableReason;
}

// The reason a row or a sequence check reads when the engine refused a reading it HAD (Finding::refusal)
// — the sequence checks' existing sentences where they have one, else a mechanical one of the same kind.
// Empty for a refusal that is not one of the engine's rules (signalRefusalRuleId).
QString refusalReason(SignalRefusal k, const QString &measureId)
{
    switch (k) {
    case SignalRefusal::NoCorridor:             return QStringLiteral("no range to judge it against on this shot");
    case SignalRefusal::NonPositiveDenominator: return QStringLiteral("the ratio could not be formed on this swing");
    case SignalRefusal::MixedScale:             return sequenceGapReason(SequenceGap::MixedScale, measureId);
    case SignalRefusal::Implausible:            return sequenceGapReason(SequenceGap::Implausible, measureId);
    case SignalRefusal::PeakAfterImpact:        return sequenceGapReason(SequenceGap::PeakAfterImpact, measureId);
    case SignalRefusal::CompositeCoverage:      return missingReasonText(MissingKind::CompositeCoverage);
    default:                                    return QString();
    }
}

// A composite's parts and coverage, from the engine's evidence into the ledger's row (schema 9). The
// two structs are copies of one shape — the ledger header stands apart from the Diagnostics module —
// and this is the one place the copy is made. Empty for any measure that is not a composite.
CompositeRowEvidence compositeRowOf(const MeasureEvidence &e)
{
    CompositeRowEvidence c;
    c.weightRead  = e.partsWeightRead;
    c.weightTotal = e.partsWeightTotal;
    c.parts.reserve(e.parts.size());
    for (const CompositePart &p : e.parts)
        c.parts.push_back({ p.name, p.measureId, p.value, p.unit, p.z, p.weight, p.share, p.sign,
                            p.atStart, p.atEnd });
    for (const CompositeUnread &u : e.partsUnread)
        c.unread.push_back({ u.name, u.measureId, u.reason, u.status });
    return c;
}

// "4 measures not assessable on this capture", and of those, the ones a gate or rule accounts for, by
// their words: "4 measures not assessable on this capture · 3 not judged: out of sight". `topWords` = the
// words most of the gated ones share. Empty when `na` is 0.
QString notAssessableNote(int na, int gated, const QString &topWords)
{
    if (na <= 0) return QString();
    QString out = QStringLiteral("%1 %2 not assessable on this capture")
                      .arg(na).arg(na == 1 ? QStringLiteral("measure") : QStringLiteral("measures"));
    if (gated > 0)
        out += topWords.isEmpty() ? QStringLiteral(" · %1 not judged").arg(gated)
                                  : QStringLiteral(" · %1 not judged: %2").arg(gated).arg(topWords);
    return out;
}

// A gate RECORD on one reading — not a rule (decided on every read, and common: most of the seed pack has
// no corridor yet) and not the whole-shot exclusion (which withholds every condition the pack holds). Only
// these widen what a shot's grid lists and keep a cell out of the folded "clean all session" tail; a rule
// or the shot's gate still says its words in the cell, wherever the cell is.
bool recordGated(const RowGate &g)
{
    return g.on && !g.rule && !g.subject.isEmpty() && g.subject != QLatin1String("shot");
}

// The words the most gated rows share, of a tally kept as they were met (first met wins a tie).
QString topOf(const QStringList &order, const QHash<QString, int> &count)
{
    QString best;
    for (const QString &w : order)
        if (best.isEmpty() || count.value(w) > count.value(best)) best = w;
    return best;
}

QString withUnit(const QString &s, const QString &unit)
{
    return unit.isEmpty() ? s : s + unit;
}

// A reading on the HOW-FAR surfaces (spreadFor, historyFor): `decimals` from the axis it is drawn
// on (spreadDecimals — never from σ), a "-" where there is no number, the true minus, and a space
// before a worded unit ("13 % hand rise", "92 mph") but not before a degree sign ("13°"). A value
// that rounds to zero is printed as 0 rather than as a signed zero: "−0 %" reads as a direction
// the reading does not have.
QString fmtSpread(double v, int decimals, const QString &unit = QString())
{
    if (!std::isfinite(v)) return QStringLiteral("-");
    const double scale = std::pow(10.0, decimals);
    double r = std::round(v * scale) / scale;
    if (r == 0.0) r = 0.0;
    const QString n = fmtNumber(r, decimals);
    if (unit.isEmpty()) return n;
    if (unit.startsWith(QChar(0x00B0))) return n + unit;
    return n + QLatin1Char(' ') + unit;
}

// THE BOX A PpBoxPlot DRAWS, from the one box implementation (typeset/box_stats.h): its edges as axis
// fractions (the strip's scale maps those, compressed bands and all), the outliers as shot ids, and the
// numbers its examined reading labels, at the axis's precision with a symbol unit (°, %) set tight and a
// worded one left off (PpFigureRow's unitShort rule). Until 18 Oct 2026 the Spread box and the
// across-sessions strip each worked these out again in QML.
QVariantMap boxOf(const std::vector<double> &values, const std::vector<int> &shotIds, const SpreadAxis &a,
                  const QString &unit)
{
    const pinpoint::box::BoxStats b = pinpoint::box::boxStats(values);
    QVariantMap m;
    m[QStringLiteral("n")] = b.n;
    if (!b.n) return m;
    const bool sym = !unit.isEmpty() && unit.size() <= 2
                     && std::none_of(unit.begin(), unit.end(), [](QChar ch) { return ch.isLetter(); });
    const auto text = [&](double v) { return fmtSpread(v, a.decimals) + (sym ? unit : QString()); };
    m[QStringLiteral("fLo")] = a.fraction(b.lo);   m[QStringLiteral("fHi")]  = a.fraction(b.hi);
    m[QStringLiteral("fQ1")] = a.fraction(b.q1);   m[QStringLiteral("fQ3")]  = a.fraction(b.q3);
    m[QStringLiteral("fMed")] = a.fraction(b.med);
    QVariantList out;
    for (int i : b.outliers) out << (size_t(i) < shotIds.size() ? shotIds[size_t(i)] : i);
    m[QStringLiteral("outliers")]     = out;
    m[QStringLiteral("outlierCount")] = int(b.outliers.size());
    m[QStringLiteral("texts")] = QVariantMap{ { QStringLiteral("lo"), text(b.lo) }, { QStringLiteral("q1"), text(b.q1) },
                                              { QStringLiteral("med"), text(b.med) }, { QStringLiteral("q3"), text(b.q3) },
                                              { QStringLiteral("hi"), text(b.hi) } };
    return m;
}

// The same, for a list of numbers sharing one unit: "22 → 19 → 13 % hand rise".
QString fmtSpreadRun(const QStringList &numbers, const QString &unit)
{
    const QString joined = numbers.join(QStringLiteral(" → "));
    if (unit.isEmpty() || numbers.isEmpty()) return joined;
    if (unit.startsWith(QChar(0x00B0))) return joined + unit;
    return joined + QLatin1Char(' ') + unit;
}

// What the corridor behind the shots IS, in words — the one thing that stops it being read as the
// golfer's own spread. A heuristic norm is somebody's coaching judgement, and saying so in the tag
// is the difference between "your swings sit far outside this corridor" and "your swings sit far
// outside a corridor somebody measured".
QString normTagOf(bool fromNorm, NormSource src, int n)
{
    if (!fromNorm) return QStringLiteral("norm · from the stored corridor");
    switch (src) {
    case NormSource::Heuristic:  return QStringLiteral("norm · coaching judgement");
    case NormSource::Seated:     return n > 0 ? QStringLiteral("norm · data, %1 swings").arg(n)
                                              : QStringLiteral("norm · data");
    case NormSource::Literature: return QStringLiteral("norm · published data");
    case NormSource::Imported:   return QStringLiteral("norm · imported");
    case NormSource::Measured:   return n > 0 ? QStringLiteral("norm · measured, %1 swings").arg(n)
                                              : QStringLiteral("norm · measured");
    }
    return QStringLiteral("norm");
}

QString normTagLongOf(bool fromNorm, NormSource src, int n)
{
    if (!fromNorm)
        return QStringLiteral("The corridor is the one this session stored — no norm resolved for "
                              "it today. It is not fitted to your shots.");
    switch (src) {
    case NormSource::Heuristic:
        return QStringLiteral("The corridor is the norm's claim, and the norm is coaching judgement "
                              "(heuristic), not measured data. It is not fitted to your shots.");
    case NormSource::Seated:
        return QStringLiteral("The corridor is the norm's claim, fitted to %1 well-positioned swings. "
                              "It is not fitted to your shots.").arg(n);
    case NormSource::Literature:
        return QStringLiteral("The corridor is the norm's claim, from published data. It is not "
                              "fitted to your shots.");
    case NormSource::Imported:
        return QStringLiteral("The corridor is the norm's claim, imported from another norm pack. "
                              "It is not fitted to your shots.");
    case NormSource::Measured:
        return QStringLiteral("The corridor is the norm's claim, measured on %1 swings of the library. "
                              "It is not fitted to your shots.").arg(n);
    }
    return QString();
}

// The last few shots on the strip are drawn a little louder — where the golfer is NOW, beside
// where the session has been. Five, which is the resolving window's default: the same stretch
// of swings the panel already treats as "recent" when it says a pattern is resolving.
constexpr int kSpreadRecent = 5;
// Beeswarm columns across the strip. ~8 px each at the 396 split, so a dot (≈5 px) only stacks
// on a genuine neighbour.
constexpr int kSpreadBins = 40;
// Across-sessions columns. More than this at the 396 split and each column is narrower than its
// own date label; the rest are counted, never silently dropped.
constexpr int kHistoryMaxColumns = 8;

QString stageName(Stage s)
{
    switch (s) {
    case Stage::Cold:        return QStringLiteral("cold");
    case Stage::Forming:     return QStringLiteral("forming");
    case Stage::Established: return QStringLiteral("established");
    case Stage::Closing:     break;
    }
    return QStringLiteral("closing");
}

Stage stageFromName(const QString &s)
{
    if (s == QLatin1String("forming"))     return Stage::Forming;
    if (s == QLatin1String("established")) return Stage::Established;
    if (s == QLatin1String("closing"))     return Stage::Closing;
    return Stage::Cold;
}

QString shotStateKind(ShotState s)
{
    switch (s) {
    case ShotState::Fired: return QStringLiteral("fired");
    case ShotState::Clean: return QStringLiteral("clean");
    case ShotState::NotAssessable: break;
    }
    return QStringLiteral("notAssessable");
}

QString tierTag(Tier t)
{
    switch (t) {
    case Tier::Pattern:  return QStringLiteral("pattern");
    case Tier::Watching: return QStringLiteral("watching");
    case Tier::Clean:    break;
    }
    return QStringLiteral("clean all session");
}

// The link's word and its stroke, straight off the brief's §4.1 table. ONE place, because the
// word and the stroke encode the same claim and a surface that could pick them independently
// would eventually draw "Coherent" with an arrowhead.
struct LinkStyle {
    const char *word;
    const char *stroke;   // "solidAccent" | "solid" | "dashed" | "dotted" | "dottedFaint"
    double      width;
    bool        arrow;
    double      opacity;
};

LinkStyle styleOf(LinkGrade g)
{
    switch (g) {
    case LinkGrade::MovedTogether:          return { "Moved together",           "solidAccent", 2.0, true,  1.0 };
    case LinkGrade::ConditionallyDependent: return { "Conditionally dependent",  "solid",       1.5, true,  1.0 };
    case LinkGrade::Coherent:               return { "Coherent",                 "dashed",      1.5, false, 1.0 };
    case LinkGrade::PresentTogether:        return { "Present together",         "dotted",      1.0, false, 1.0 };
    case LinkGrade::Unanchored:             break;
    }
    return { "Unanchored", "dottedFaint", 1.0, false, 0.5 };
}

QString gradeName(LinkGrade g)
{
    switch (g) {
    case LinkGrade::MovedTogether:          return QStringLiteral("movedTogether");
    case LinkGrade::ConditionallyDependent: return QStringLiteral("conditionallyDependent");
    case LinkGrade::Coherent:               return QStringLiteral("coherent");
    case LinkGrade::PresentTogether:        return QStringLiteral("presentTogether");
    case LinkGrade::Unanchored:             break;
    }
    return QStringLiteral("unanchored");
}

QString chainNodeKindName(ChainNodeKind k)
{
    switch (k) {
    case ChainNodeKind::LiveCard:     return QStringLiteral("live");
    case ChainNodeKind::Ghost:        return QStringLiteral("ghost");
    case ChainNodeKind::ScreenedRoot: return QStringLiteral("screenedRoot");
    case ChainNodeKind::Watched:      return QStringLiteral("watched");
    case ChainNodeKind::Outcome:      break;
    }
    return QStringLiteral("outcome");
}

// MOST-EVIDENCED RAIL FIRST, and the ordering is decided in C++ rather than left to the panel.
// A session's authored neighbourhood can yield more rails than a 1168 px panel can draw — this
// one yields eighteen over six shots — so something has to choose, and a panel that took "the
// first few" off an arbitrary order would be showing a different chain each launch. The key is
// the count of LIVE cards (how much of this rail the session actually measured), then length,
// then the first node's id so ties are stable.
//
// ONE RULE, TWO CALLERS. The chain rail sorts its chains with it and the condition detail sorts
// its cause and effect paths with it, because "which of these is the one to draw first" is the
// same question asked of the same kind of object, and two rules would let the panel and the
// drill-in disagree about which path the session is most sure of.
bool railBefore(const QVariantMap &x, const QVariantMap &y)
{
    const int lx = x.value(QStringLiteral("liveCount")).toInt();
    const int ly = y.value(QStringLiteral("liveCount")).toInt();
    if (lx != ly) return lx > ly;
    const QVariantList nx = x.value(QStringLiteral("nodes")).toList();
    const QVariantList ny = y.value(QStringLiteral("nodes")).toList();
    if (nx.size() != ny.size()) return nx.size() > ny.size();
    const QString ix = nx.isEmpty() ? QString() : nx.first().toMap().value(QStringLiteral("id")).toString();
    const QString iy = ny.isEmpty() ? QString() : ny.first().toMap().value(QStringLiteral("id")).toString();
    return ix < iy;
}

// "would explain ___": how many of the session's patterns a cause accounts for, in words a person
// would say. "1 of your pattern" read as a typo; one is "one of your patterns". Shared by the
// driver footer, the screen CTA and the detail's cause headline so the three cannot drift.
QString patternsExplained(int n)
{
    return n == 1 ? QStringLiteral("one of your patterns")
                  : QStringLiteral("%1 of your patterns").arg(n);
}

// HOW MANY PATHS THE DETAIL MAY PUBLISH PER SIDE, and how deep it walks. A pack condition can sit
// under a fan of ancestors whose simple-path count is exponential in the fan; the panel draws one
// path and collapses the rest to a line each, so publishing two hundred of them would cost a
// scroll region nobody reads and a walk nobody wants between balls. What is cut is COUNTED
// (`causesHidden` / `effectsHidden`) rather than dropped silently, exactly as the card row counts
// what it could not draw.
constexpr int kDetailMaxPaths = 8;
constexpr int kDetailMaxDepth = 7;

// THE LEDGER'S SHOT ID IS THE SWING FOLDER'S NUMBER (swing_0007 -> 7), on every path in. The
// back-fill has always read it from there; the live path used to take ShotProcessor's id, which
// is the carousel's own counter and agrees with the folder only in a session whose carousel was
// never reloaded. Extend today's session and the next shot arrived as "91" beside folders 1–30:
// filed under a number no reload would give it, so the next activation graded swing_0031 a
// second time as 31, and in other orders the counter landed ON a number already held and the
// shot was dropped as a repeat. −1 when the folder carries no number.
int folderShotId(const QString &swingDir)
{
    const QString name = QFileInfo(QDir::cleanPath(swingDir)).fileName();
    const int us = name.lastIndexOf(QLatin1Char('_'));
    if (us < 0) return -1;
    bool ok = false;
    const int id = name.mid(us + 1).toInt(&ok);
    return (ok && id >= 0) ? id : -1;
}

} // namespace

// ── Construction ────────────────────────────────────────────────────────────────────────

SessionDiagnosticsModel::SessionDiagnosticsModel(QObject *parent)
    : QObject(parent)
    , m_packProv(makeCharacteristicPackProvider())
    , m_norms(sharedNormProvider())
{
    // ONE worker thread, not "a worker thread". Two would reduce shot 8 before shot 7 on an
    // unlucky schedule, and while conditionLedgers() sorts by nothing that would care, the
    // TICK RUN is drawn in vector order and the run is the panel's most literal claim about
    // what happened when. A pool of one is also the honest expression of the load: one swing
    // arrives every twenty seconds and takes well under a second to reduce.
    m_pool.setMaxThreadCount(1);
    m_historyPool.setMaxThreadCount(1);
    rebuild();
}

SessionDiagnosticsModel::~SessionDiagnosticsModel()
{
    // The pool holds lambdas that capture `this`. Draining before the members go is not
    // optional, and there is no cancellation to offer — a detection in flight is a second of
    // work, so waiting is cheaper than a cancellation flag every worker would have to check.
    m_pool.waitForDone();
    // The history scan captures `this` too. Its result is delivered by a queued call whose
    // context is this object, so a scan that lands after destruction is simply dropped — but the
    // worker itself must not outlive the members it was started from.
    m_historyPool.waitForDone();
    // A batch still coalescing (decision 1) has rows placed and not yet written. Write them:
    // they are graded and stamped, and on disk they will not be detected again. The deliveries
    // still queued die with this object, as they always did, and the next activation re-does them.
    if (m_owedRebuild) persist();
}

// ── Simple inputs ───────────────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::setCadence(const QString &mode)
{
    // The same fall-back regime AppSettings applies: anything unrecognised is bandwidth, the
    // quieter of the two. See app_settings.h for why that is the safe direction.
    const QString m = (mode == QLatin1String("everyShot")) ? mode : QStringLiteral("bandwidth");
    if (m_cadence == m) return;
    m_cadence = m;
    emit cadenceChanged();
    // Cadence changes what is SURFACED and nothing that is stored, so this rebuilds the
    // surface and never touches a row.
    rebuild();
}

void SessionDiagnosticsModel::setGradePolicy(const QString &name)
{
    if (m_policyName == name) return;
    m_policyName = name;
    emit gradePolicyChanged();
    // Deliberately NOT a re-detection of the shots already in the ledger. The rows record
    // what a corridor said at the moment the swing was graded; re-grading history under a
    // policy the golfer chose mid-session would silently rewrite the ticks they have already
    // been shown. The new policy applies from the next shot, and a full re-read is what
    // activateSession() on a fresh model is for.
}

void SessionDiagnosticsModel::setReviewing(bool on)
{
    if (m_reviewing == on) return;
    m_reviewing = on;
    emit reviewingChanged();
    rebuild();
}

void SessionDiagnosticsModel::setSelectedShotId(int id)
{
    if (m_selectedShotId == id) return;
    m_selectedShotId = id;
    emit selectedShotIdChanged();
    rebuild();
}

void SessionDiagnosticsModel::setSelectedSwingDir(const QString &dir)
{
    if (m_selectedSwingDir == dir) return;
    m_selectedSwingDir = dir;
    emit selectedSwingDirChanged();
    if (resolveSelectedSwingDir()) rebuild();
}

bool SessionDiagnosticsModel::resolveSelectedSwingDir()
{
    const int id = m_selectedSwingDir.isEmpty() ? -1 : shotIdForSwingDir(m_selectedSwingDir);
    if (id == m_selectedShotId) return false;
    m_selectedShotId = id;
    emit selectedShotIdChanged();
    return true;
}

int SessionDiagnosticsModel::shotIdForSwingDir(const QString &dir) const
{
    if (dir.isEmpty()) return -1;
    const QString want = QDir::cleanPath(dir);
    for (auto it = m_swingDirs.constBegin(); it != m_swingDirs.constEnd(); ++it)
        if (QDir::cleanPath(it.value()) == want && indexOfShot(it.key()) >= 0)
            return it.key();

    const QString name = QFileInfo(want).fileName();
    for (auto it = m_swingDirs.constBegin(); it != m_swingDirs.constEnd(); ++it)
        if (QFileInfo(QDir::cleanPath(it.value())).fileName() == name && indexOfShot(it.key()) >= 0)
            return it.key();

    if (name.startsWith(QLatin1String("swing_"))) {
        bool ok = false;
        const int n = name.mid(name.lastIndexOf(QLatin1Char('_')) + 1).toInt(&ok);
        if (ok && indexOfShot(n) >= 0) return n;
    }
    return -1;
}

Stage SessionDiagnosticsModel::effectiveStage() const
{
    return (m_closed || m_reviewing) ? Stage::Closing : m_stage;
}

QString SessionDiagnosticsModel::stage() const { return stageName(effectiveStage()); }

int SessionDiagnosticsModel::patternCount() const
{
    int n = 0;
    for (const ConditionLedger &l : m_ledgers)
        if (l.tier == Tier::Pattern) ++n;
    return n;
}

// ── Ingest ──────────────────────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::ingestShot(int shotId, const QString &swingDir)
{
    // IDEMPOTENCE IS DECIDED HERE, ON THE CALLING THREAD, AND BEFORE ANY WORK IS QUEUED.
    // Doing it in applyIngested() would still produce one row per shot, but it would parse
    // the same 30 MB document twice when a back-fill scan raced a live shotProcessed — which
    // is exactly the moment the app can least afford it.
    if (swingDir.isEmpty()) return;
    // The folder's number, whatever the caller numbered it (folderShotId above). The caller's id
    // stands only for a folder that carries none.
    if (const int fromFolder = folderShotId(swingDir); fromFolder >= 0) shotId = fromFolder;
    if (shotId < 0) return;
    // A CLOSED SESSION IS FROZEN. Closing is the one lifecycle transition that is an event
    // rather than a threshold, and the panel it produces is the session's summary — a shot
    // arriving afterwards belongs to the next session, not to the one whose bookends and
    // fault-profile contribution have already been written.
    if (m_closed) return;
    if (m_ingested.contains(shotId)) return;
    m_ingested.insert(shotId);
    m_swingDirs.insert(shotId, swingDir);
    queueDetect(shotId, swingDir, false);
}

void SessionDiagnosticsModel::regradeShot(const QString &swingDir)
{
    if (m_sessionDir.isEmpty() || swingDir.isEmpty()) return;
    const QFileInfo fi(swingDir);
    if (QDir::cleanPath(fi.absolutePath()) != QDir::cleanPath(QFileInfo(m_sessionDir).absoluteFilePath()))
        return;
    const QString name = fi.fileName();
    bool ok = false;
    const int id = name.mid(name.lastIndexOf(QLatin1Char('_')) + 1).toInt(&ok);
    if (!ok || id < 0) return;
    if (!m_ingested.contains(id)) { ingestShot(id, swingDir); return; }
    m_swingDirs.insert(id, swingDir);
    queueDetect(id, swingDir, true);
}

bool SessionDiagnosticsModel::dropShot(int shotId)
{
    const int at = indexOfShot(shotId);
    if (at >= 0) {
        m_shots.erase(m_shots.begin() + at);
        // The focus split is a row count — "the shots before this are baseline" — so a baseline
        // row leaving moves it down one, and the swing it named is still the first intervention.
        if (m_focusFromShot > at) --m_focusFromShot;
    }
    const bool held = at >= 0 || m_ingested.contains(shotId);
    m_ingested.remove(shotId);
    m_lmShots.remove(shotId);
    m_gradedFrom.remove(shotId);
    m_swingDirs.remove(shotId);
    if (m_owedAfterShot == shotId) m_owedAfterShot = -1;
    return held;
}

void SessionDiagnosticsModel::forgetShot(const QString &swingDir)
{
    if (m_sessionDir.isEmpty() || swingDir.isEmpty()) return;
    const QFileInfo fi(swingDir);
    if (QDir::cleanPath(fi.absolutePath()) != QDir::cleanPath(QFileInfo(m_sessionDir).absoluteFilePath()))
        return;
    const int id = folderShotId(swingDir);
    if (id < 0 || !dropShot(id)) return;
    resolveSelectedSwingDir();
    // Reduced and written as any arrival is — at once, or with the batch still landing.
    m_owedRebuild = true;
    settleBatch();
}

void SessionDiagnosticsModel::queueDetect(int shotId, const QString &swingDir, bool regrade)
{
    if (m_synchronous) {
        Ingested in = detectShot(shotId, swingDir);
        in.regrade = regrade;
        in.generation = m_activationGen;
        applyIngested(in);
        return;
    }

    ++m_pending;
    emit busyChanged();
    // QThreadPool::start() rather than QtConcurrent::run(): there is no QFuture to wait on
    // here — the result comes back through the queued invocation below — and run()'s future
    // is [[nodiscard]] precisely so that a caller who drops it says why.
    m_pool.start([this, shotId, swingDir, regrade, generation = m_activationGen]() {
        Ingested in = detectShot(shotId, swingDir);
        in.regrade = regrade;
        in.generation = generation;
        // Queued, so the row vector and every signal below are only ever touched by the
        // thread that owns this object. The worker holds no reference to anything mutable.
        QMetaObject::invokeMethod(this, [this, in]() { applyIngested(in); }, Qt::QueuedConnection);
    });
}

QString SessionDiagnosticsModel::computeContentStamp() const
{
    if (!m_packProv || !m_norms) return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QJsonDocument(savePack(m_packProv->pack())).toJson(QJsonDocument::Compact));
    h.addData(QJsonDocument(saveNormPack(m_norms->norms())).toJson(QJsonDocument::Compact));
    h.addData(QJsonDocument(saveContextTree(m_norms->contexts())).toJson(QJsonDocument::Compact));
    // The row shape too: a row written before a field existed regrades to fill it.
    h.addData(QByteArray::number(kDiagSchemaVersion));
    h.addData(QByteArrayLiteral("words") + QByteArray::number(kRowWordsVersion));
    return QString::fromLatin1(h.result().toHex().left(16));
}

SessionDiagnosticsModel::Ingested SessionDiagnosticsModel::detectShot(int shotId,
                                                                     const QString &swingDir) const
{
    Ingested out;
    // STAMPED BEFORE THE READ. A re-analysis that rewrites the document after this point leaves
    // a stamp that no longer matches, so the next activation regrades it — the opposite order
    // would record the new stamp against the old reading and never look again.
    const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(swingDir);
    out.from.docSize    = doc.size;
    out.from.docMtimeMs = doc.mtimeMs;
    out.from.content    = m_contentStamp;
    if (!m_packProv || !m_norms) return out;

    const CharacteristicPack &pack = m_packProv->pack();

    // writeSidecar true: the phase grid is cached beside the swing, so a re-activation of the
    // same session costs a small read rather than a second fat parse of every document.
    LiveMeasureSource src(swingDir, pack);
    src.setWithSigma(m_withSigma);
    // The kinematic sequence checks' signals ride the SAME call, so each is evaluated on the graded
    // source detect() read (§10.6 E4: the tile and the diagnosis cannot disagree).
    const LiveDetection     d = detectForSwing(src, pack, m_norms, gradePolicyByName(m_policyName),
                                               {}, QString(), sequenceCheckSignalIds());

    ShotRecord rec;
    rec.shotId    = shotId;
    rec.club      = d.club;
    rec.contextId = d.contextId;
    // ⚠ THE INSTANT THE DOCUMENT RECORDS, NOT THE INSTANT THE FILE WAS LAST TOUCHED.
    // This read the file's mtime, which is only incidentally the capture time: copying, moving or
    // restoring a session directory rewrites every shot's timestamp, and the corpus tooling moves
    // swing dirs between hosts as a matter of course. A re-analysis that rewrites swing.json bumps
    // it too. clock.wallclock is the capture instant and survives all of that.
    //
    // writeSidecar TRUE, and load-bearing rather than copied from the phase grid above: the summary
    // sidecar's guard is the source document's size AND mtime, so a copied directory invalidates
    // it — the very case this fixes is the case that misses the cache. Passing false would take the
    // miss and fall straight back to the mtime that is wrong. One lean parse per swing, once, and
    // it re-seats the stale sidecar on the way through.
    //
    // The mtime stays as the fallback, for a document written before clock.wallclock existed or one
    // whose clock block will not parse — wallclockMs is 0 in both cases, never a plausible date.
    const pinpoint::SwingSummary summary =
        pinpoint::SwingDocReader::readSwingSummary(swingDir, /*writeSidecar=*/true);
    rec.timestampMs =
        summary.wallclockMs > 0
            ? summary.wallclockMs
            : pinpoint::SwingStore::info(swingDir).mtimeMs;
    // Warm-up is the FIRST-N rule only, applied inside shotWeight(). A declared warm-up flag
    // has no producer yet and inventing one from the shot index would double-count the rule.
    rec.warmUp = false;

    rec.rows.reserve(d.result.findings.size());
    for (const Finding &f : d.result.findings) {
        // A SESSION-scope condition has no per-swing verdict (§10.8 E2, NR-04/NR-11): its measures
        // are refused on every swing, and a row here would draw a tick, a pip and a "not assessable"
        // for a question no swing is asked. rebuild() reads it over the session instead.
        if (isSessionScopeCondition(pack, f.conditionId)) continue;
        ConditionRow r;
        r.conditionId      = f.conditionId;
        r.confidence       = f.confidence;
        r.material         = f.material;
        r.sharp            = f.sharp;
        r.pFire            = f.pFire;        // §A8.4 — −1 when not formed
        r.grossRisk        = f.grossRisk;
        r.quantified       = f.quantified;
        r.contextId        = d.contextId;
        // LiveMeasureSource::club() applies the house-wide DRIVER stub for a shot that
        // declares none, so this seam cannot tell an undeclared club from a declared driver
        // and never trips the inferred-context demotion. Stated rather than faked: a `true`
        // here would demote every shot's ranking on a fact we do not have.
        r.contextInferred  = false;

        switch (f.state) {
        case FindingState::Fired:
            r.state     = ShotState::Fired;
            r.direction = (f.direction == Direction::High) ? 1 : -1;
            break;
        case FindingState::NotFired:
            r.state     = ShotState::Clean;
            r.direction = 0;
            break;
        case FindingState::Unavailable:
            r.state = ShotState::NotAssessable;
            // Never blank (ConditionRow's contract): the review strip prints this where a
            // corridor would go. The source knows WHY for the measure that failed; where a
            // condition names several, the first is the one that stopped it.
            if (!f.missingMeasures.isEmpty())
                r.notAssessableReason = src.missingReason(f.missingMeasures.first());
            // A value the producer measured but marked not for judging (KB-18): its own reason.
            if (r.notAssessableReason.isEmpty() && !f.missingMeasures.isEmpty())
                if (const auto w = src.value(f.missingMeasures.first()); w && !w->withheld.isEmpty())
                    r.notAssessableReason = w->withheld;
            if (r.notAssessableReason.isEmpty())
                r.notAssessableReason = missingReasonText(MissingKind::MetricNotProduced);
            // A REFUSAL THE ENGINE MADE ON A READING IT HAD (trust stage 6 session 2): no range to judge
            // against, a reading not believed, two scales, no ratio, a peak after impact. The measure
            // read, so the absence reasons above say nothing true ("metric not produced") — the row says
            // the refusal and carries the engine RULE behind it (rule.diag.*), with no subject: a rule
            // leaves no per-swing record, so there is no Trust-panel reading to open, only its words.
            // Re-derived on every read, so it holds on a swing analysed before records existed too.
            if (const char *rule = signalRefusalRuleId(f.refusal)) {
                r.notAssessableReason = refusalReason(f.refusal, f.missingMeasures.value(0));
                r.notJudgedGate       = QString::fromLatin1(rule);
                r.notJudgedSubject.clear();
            } else {
                // THE GATE THAT STOPPED IT (trust_and_gates_design.md §4.4): a missing or withheld measure
                // standing on a sequence node a gate refused says which gate, in its words, and the row
                // carries the gate and the reading so the Trust panel can list what each gate blocked.
                for (const QString &mid : f.missingMeasures) {
                    const LiveMeasureSource::GateCause gc = src.gateCause(mid);
                    if (gc.gate.isEmpty()) continue;
                    r.notJudgedGate       = gc.gate;
                    r.notJudgedSubject    = gc.subject;
                    r.notAssessableReason = gc.clause;      // "pelvis peak out of sight"
                    break;
                }
            }
            break;
        }

        if (f.evidence.hasEvidence) {
            r.drivingMeasureId = f.evidence.drivingMeasureId;
            r.value            = f.evidence.value;
            r.corridorLo       = f.evidence.corridorLo;
            r.corridorHi       = f.evidence.corridorHi;
            r.z                = f.evidence.z;
            // WHICH SIDE THE BAND IS OPEN, recorded here because this is the only place it is
            // known: MeasureEvidence carries the flags, the ledger row carries the number they
            // scale, and the strength meter downstream cannot read one without the other.
            // Every branch that left z at 0 for want of a scale records None, so a meter is
            // never drawn over a reading that was graded against an authored number.
            r.corridorShape    = corridorShapeOf(f.evidence);
            // Schema 9: a composite's parts and coverage, the evidence its detail page draws.
            r.composite        = compositeRowOf(f.evidence);
        }
        // Every signal's measure, when there is more than one to show (schema 4).
        if (f.readings.size() > 1) {
            for (const MeasureEvidence &e : f.readings) {
                MeasureRow m;
                m.measureId     = e.drivingMeasureId;
                m.signalId      = e.drivingSignalId;
                m.value         = e.value;
                m.corridorLo    = e.corridorLo;
                m.corridorHi    = e.corridorHi;
                m.z             = e.z;
                m.corridorShape = corridorShapeOf(e);
                m.fired         = e.fired;
                m.composite     = compositeRowOf(e);
                r.readings.push_back(std::move(m));
            }
        }
        rec.rows.push_back(std::move(r));
    }

    // Each sequence check's own verdict (schema 5), and why one could not be assessed — in golfer
    // words, naming the missing piece where the engine's refusal says which it was.
    rec.signalRows.reserve(d.signalOutcomes.size());
    for (const SignalOutcome &o : d.signalOutcomes) {
        SignalRow g;
        g.signalId = o.signalId;
        if (o.available) {
            g.state      = o.fired ? ShotState::Fired : ShotState::Clean;
            g.pFire      = o.pFire;
            g.quantified = o.quantified;
            g.comparedWith = o.comparedWith;
        } else {
            g.state = ShotState::NotAssessable;
            const QString first = o.missing.value(0);
            SequenceGap gap = SequenceGap::Unknown;
            switch (o.refusal) {
            case SignalRefusal::MeasureMissing:
                // The KS keys are emitted only for placed nodes (§10.9 P3), so "not produced" on
                // one of them IS an unplaced node. Every other reason (no analysis, no impact)
                // is the pipeline's, and says so in its own words below.
                if (src.missingKind(first) == MissingKind::MetricNotProduced) gap = SequenceGap::NotPlaced;
                break;
            case SignalRefusal::MixedScale:  gap = SequenceGap::MixedScale;  break;
            case SignalRefusal::Implausible: gap = SequenceGap::Implausible; break;
            case SignalRefusal::PeakAfterImpact: gap = SequenceGap::PeakAfterImpact; break;
            case SignalRefusal::Withheld:    gap = SequenceGap::Withheld;    break;
            default: break;
            }
            g.reason = sequenceGapReason(gap, first);
            if (gap == SequenceGap::Unknown && o.refusal == SignalRefusal::MeasureMissing) {
                const MissingKind k = src.missingKind(first);
                if (k != MissingKind::None && k != MissingKind::MetricNotProduced)
                    g.reason = missingReasonText(k);
            }
            // the gate behind an unplaced or withheld peak, as for the condition rows above
            if (o.refusal == SignalRefusal::MeasureMissing || o.refusal == SignalRefusal::Withheld)
                for (const QString &mid : o.missing) {
                    const LiveMeasureSource::GateCause gc = src.gateCause(mid);
                    if (gc.gate.isEmpty()) continue;
                    g.notJudgedGate    = gc.gate;
                    g.notJudgedSubject = gc.subject;
                    g.reason           = gc.clause;
                    break;
                }
            // ...and the engine rule behind a refusal on a reading it had, as for the rows above: the
            // sentence the gap already chose (or the rule's own where the gap had none), no subject
            if (const char *rule = signalRefusalRuleId(o.refusal)) {
                if (gap == SequenceGap::Unknown) {
                    const QString why = refusalReason(o.refusal, first);
                    if (!why.isEmpty()) g.reason = why;
                }
                g.notJudgedGate = QString::fromLatin1(rule);
                g.notJudgedSubject.clear();
            }
        }
        rec.signalRows.push_back(std::move(g));
    }
    // The placed peak instants the session's timing spread is reduced from (schema 6, §10.9 P8). Only
    // placed nodes emit, so an absent value IS an unplaced node; a time a signal refused as
    // implausible on this swing is a capture fault, not a reading, and stays out of the spread too.
    for (const SequenceSpreadSegmentDef &seg : sequenceSpreadSegments()) {
        const QString mid = QLatin1String(seg.measureId);
        bool implausible = false;
        for (const SignalOutcome &o : d.signalOutcomes)
            if (o.refusal == SignalRefusal::Implausible && o.missing.contains(mid)) implausible = true;
        if (implausible) continue;
        // Nor does a time its producer marked not for judging (the camera club peak, KB-18).
        if (const auto v = src.value(mid); v && std::isfinite(v->value) && v->withheld.isEmpty()) {
            PeakTimeRow t;
            t.measureId = mid;
            t.valueMs   = v->value;
            t.sigmaMs   = v->sigma;
            rec.peakTimes.push_back(std::move(t));
        }
    }

    // The ranking's last tie-break: the measured clubhead speed where there is one, else ours.
    for (const char *mid : { "m_lmClubheadSpeed", "m_clubheadSpeedImpact" }) {
        if (const auto v = src.value(QLatin1String(mid)); v && std::isfinite(v->value)) {
            rec.clubheadSpeed = v->value;
            break;
        }
    }

    // Data-integrity exclusion. A shot whose recording is known broken — frames lost
    // during capture (captureIntegrity), or an IMU record that does not reproduce
    // (imuIntegrity) — is still drawn on the strip, still carries the ⚠ the card shows,
    // but says nothing to the session: every row becomes NotAssessable, which rule 1
    // already keeps out of every denominator (diagnostic_ledger.h tierAtPrefix). Done
    // here rather than in the ledger arithmetic so the reason reads in the corridor
    // slot like any other withheld row, and the ledger needs no new rule.
    // A refused CLUB track (dataWarningDetail.clubRefused, 2026-10-01) also carries
    // the ⚠ but withholds nothing here: its club rows are already absent (the
    // track is invalid, so no club-derived producer ran) and the body/wrist rows
    // are untouched by it. Only the two recording-integrity facts exclude the shot.
    if (summary.dataWarning) rec.dataWarning = true;
    if (summary.dataWarning
        && (summary.dataWarningDetail.value(QStringLiteral("capture")).toBool()
            || summary.dataWarningDetail.value(QStringLiteral("imu")).toBool())) {
        // THE GATE IS THE SHOT'S, NOT WHATEVER THE ROW NAMED BEFORE (trust stage 6 session 2): the
        // integrity record on `shot` (frames missing, sensor recording inconsistent) replaces a gate or
        // rule the detection left on the row, which no longer explains it. Empty on a swing analysed
        // before records existed — the stale names are cleared, never kept.
        const LiveMeasureSource::GateCause shotGate = src.gateForSubject(QStringLiteral("shot"));
        for (ConditionRow &r : rec.rows) {
            r.state               = ShotState::NotAssessable;
            r.direction           = 0;
            r.notAssessableReason = missingReasonText(MissingKind::CaptureDataIssue);
            r.notJudgedGate       = shotGate.gate;
            r.notJudgedSubject    = shotGate.subject;
        }
        for (SignalRow &g : rec.signalRows) {
            g.state            = ShotState::NotAssessable;
            g.pFire            = -1.f;
            g.quantified       = false;
            g.reason           = missingReasonText(MissingKind::CaptureDataIssue);
            g.notJudgedGate    = shotGate.gate;
            g.notJudgedSubject = shotGate.subject;
        }
        rec.peakTimes.clear();
    }

    out.record           = std::move(rec);
    out.hasLaunchMonitor = src.hasLaunchMonitor();
    out.ok               = true;
    return out;
}

void SessionDiagnosticsModel::applyIngested(const Ingested &in)
{
    // busyChanged BEFORE the row is placed and reduced, as it always was. That is the order
    // WorkOnsController relies on by connecting QUEUED: its handler runs after this call has
    // returned, so when it sees busy go false the batch below has been reduced and written.
    if (!m_synchronous) {
        m_pending = std::max(0, m_pending - 1);
        emit busyChanged();
    }
    // A result for a session that is no longer loaded: counted off above, dropped here. It may
    // still be the delivery the current batch was waiting on to drain, so settle regardless.
    if (in.generation != m_activationGen) {
        settleBatch();
        return;
    }
    if (!in.ok) {
        // A swing that could not be read is not a shot with no findings — it is a shot we
        // never looked at, and putting an all-NotAssessable row set in the ledger for it
        // would draw a full column of outlined ticks for a capture that may be fine. Drop
        // the reservation so a later re-activation can try again. A failed REGRADE keeps the
        // row it already had: the last good reading beats none.
        if (!in.regrade) m_ingested.remove(in.record.shotId);
        // ...and a failure can be the last of a batch: what the others placed is owed.
        settleBatch();
        return;
    }
    // A swing trashed while it was being read (forgetShot): its row would count a swing that
    // has gone. Every ingest and regrade in flight is in m_ingested until forgetShot takes it out.
    if (!m_ingested.contains(in.record.shotId)) {
        settleBatch();
        return;
    }

    // Shots are held in the order they were struck. The pool is single-threaded so arrivals
    // are already ordered, but a back-fill can interleave with a live shot, and the tick run
    // IS shot order — so the insertion point is found rather than assumed.
    const int id = in.record.shotId;
    auto pos = std::lower_bound(m_shots.begin(), m_shots.end(), id,
                                [](const ShotRecord &s, int v) { return s.shotId < v; });
    const bool present = pos != m_shots.end() && pos->shotId == id;
    if (present && !in.regrade) { settleBatch(); return; }   // belt and braces — and it may be the batch's last
    if (present) *pos = in.record;
    else         m_shots.insert(pos, in.record);
    if (in.hasLaunchMonitor) m_lmShots.insert(id);
    else if (in.regrade)     m_lmShots.remove(id);
    m_gradedFrom.insert(id, in.from);
    // The selected swing may be the one that just arrived (a back-fill landing after the pick).
    resolveSelectedSwingDir();

    m_owedRebuild = true;
    // A regrade is the same shot read again, not a shot arriving: no after-shot moment.
    if (!in.regrade) m_owedAfterShot = id;
    settleBatch();
}

// THE BATCH'S ONE REDUCTION (decision 1 in the header).
//
// m_pending counts every detection queued and not yet delivered — a back-fill or regrade
// queues the whole session up front in activateSession() — so "nothing pending" is exactly
// "this was the last of the batch". Until then a delivery only places its row: n reductions
// in a row would each be thrown away by the next, and each one is the full rebuild() plus a
// rewrite of diagnostics.json on the GUI thread. The batch of one — a live shot with nothing
// else in flight — settles on its own delivery, so the live path is unchanged: rebuild,
// persist, shotIngested, in that order.
//
// WHAT A BATCH DOES NOT REPRODUCE: the per-shot intermediate states. The stage ratchet and
// the card order's hysteresis now step once, from the state before the batch to the state
// after it, instead of once per row. For a regrade the intermediates were a mix of old and
// new gradings and meant nothing; for a cold back-fill they were a replay of the session
// shot by shot, and a session that established part way through and then thinned out is
// the one case where the replay could latch a stage the final evidence does not reach. The
// rows themselves — the evidence, and everything the ledger reduces from it — are identical.
//
// ONE shotIngested for the batch, carrying its newest arriving shot: the panel's after-shot
// pulse reads afterShotDelta, which only ever describes the newest shot, so n pulses in one
// drain were n repaints of the same cue. None when the batch was regrades alone.
//
// Synchronous mode delivers inline with nothing ever pending, so every delivery settles at
// once — tests and the report tools see the per-shot behaviour they always did.
void SessionDiagnosticsModel::settleBatch()
{
    if (!m_owedRebuild) return;
    if (!m_synchronous && m_pending > 0) return;       // more of this batch still to land
    m_owedRebuild = false;
    const int afterShot = std::exchange(m_owedAfterShot, -1);
    rebuild();
    persist();
    if (afterShot >= 0) emit shotIngested(afterShot, !m_quiet);
}

bool SessionDiagnosticsModel::waitForIdle(int msTimeout)
{
    if (m_synchronous) return true;
    QDeadlineTimer deadline(msTimeout);
    while (m_pending > 0 && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    // One last drain: the worker may have finished while the queued delivery was still in the
    // event queue.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return m_pending == 0;
}

// ── Session lifecycle ───────────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::activateSession(const QString &sessionDir)
{
    m_pool.waitForDone();

    // The previous activation's batch, if one was still coalescing: the rows it placed are
    // graded and stamped, so they are written to THAT session's file before it is let go
    // (persist() reads m_sessionDir, still the old one here). Not reduced — nobody is left to
    // read the reduction, and the next load re-reduces from the rows anyway. Its deliveries
    // still queued carry the old generation and are dropped on arrival; their swings remain
    // stale on disk and are graded again the next time that session is activated.
    if (m_owedRebuild) persist();
    m_owedRebuild   = false;
    m_owedAfterShot = -1;
    ++m_activationGen;

    m_sessionDir = sessionDir;
    m_shots.clear();
    m_ingested.clear();
    m_lmShots.clear();
    m_swingDirs.clear();
    m_gradedFrom.clear();
    m_contentStamp = computeContentStamp();
    m_displayOrder.clear();
    m_patternSet.clear();
    m_explanation = Explanation();
    m_screens.clear();
    m_focusConditionId.clear();
    m_focusFromShot = -1;
    m_declaredMiss.clear();
    m_stage  = Stage::Cold;
    m_reachedEstablished = false;
    m_closed = false;
    m_profile = QJsonObject();
    m_expectations.clear();
    // A detail is a page being read, not a fact about a session. Pointing the panel at a
    // different ledger closes it rather than re-answering it against evidence it was not opened
    // over — and the id may not even exist in the new session's neighbourhood.
    m_detailConditionId.clear();
    m_detailMeasureId.clear();
    m_detail = QVariantMap();

    emit sessionChanged();
    emit detailChanged();

    if (sessionDir.isEmpty()) {
        scanHistory();
        rebuild();
        emit intentChanged();
        return;
    }

    // ── 1. The file, if there is one ────────────────────────────────────────────────
    QFile f(diagnosticsPath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        // The ledger half is diagnostic_ledger.h's own, read through its own reader — the
        // gates travel with the file so a session records what it was read under.
        const QJsonObject ledgerObj = root.value(QStringLiteral("ledger")).toObject();
        m_shots = fromJson(ledgerObj, &m_opt);
        for (const ShotRecord &s : m_shots) m_ingested.insert(s.shotId);

        const QJsonObject session = root.value(QStringLiteral("session")).toObject();
        m_closed = session.value(QStringLiteral("closed")).toBool(false);
        m_stage  = stageFromName(session.value(QStringLiteral("stage")).toString());
        m_reachedEstablished =
            session.value(QStringLiteral("reachedEstablished")).toBool(m_stage == Stage::Established);
        for (const QJsonValue &v : session.value(QStringLiteral("lmShots")).toArray())
            m_lmShots.insert(v.toInt());
        const QJsonObject dirs = session.value(QStringLiteral("swingDirs")).toObject();
        for (auto it = dirs.constBegin(); it != dirs.constEnd(); ++it)
            m_swingDirs.insert(it.key().toInt(), it.value().toString());
        const QJsonObject graded = session.value(QStringLiteral("gradedFrom")).toObject();
        for (auto it = graded.constBegin(); it != graded.constEnd(); ++it) {
            const QJsonObject g = it.value().toObject();
            GradedFrom from;
            from.docSize    = qint64(g.value(QStringLiteral("size")).toDouble());
            from.docMtimeMs = qint64(g.value(QStringLiteral("mtimeMs")).toDouble());
            from.content    = g.value(QStringLiteral("content")).toString();
            m_gradedFrom.insert(it.key().toInt(), from);
        }

        // A row filed under a number that is not its folder's was written by the live path
        // before it took the folder's number (folderShotId). It is dropped here and the
        // reconcile below grades the swing again under the right one — kept, the same swing
        // would stand in the ledger twice.
        for (auto it = m_swingDirs.begin(); it != m_swingDirs.end(); ) {
            const int id = it.key();
            const int fromFolder = folderShotId(it.value());
            if (fromFolder < 0 || fromFolder == id) { ++it; continue; }
            m_shots.erase(std::remove_if(m_shots.begin(), m_shots.end(),
                                         [id](const ShotRecord &s) { return s.shotId == id; }),
                          m_shots.end());
            m_ingested.remove(id);
            m_lmShots.remove(id);
            m_gradedFrom.remove(id);
            it = m_swingDirs.erase(it);
        }

        const QJsonObject intent = root.value(QStringLiteral("intent")).toObject();
        m_focusConditionId = intent.value(QStringLiteral("focusConditionId")).toString();
        m_focusFromShot    = intent.value(QStringLiteral("focusFromShot")).toInt(-1);
        m_declaredMiss     = intent.value(QStringLiteral("declaredMiss")).toString();

        const QJsonObject screens = root.value(QStringLiteral("screens")).toObject();
        for (auto it = screens.constBegin(); it != screens.constEnd(); ++it)
            m_screens.insert(it.key(), it.value().toBool());
    }

    // ── 2. The athlete's history ────────────────────────────────────────────────────
    loadProfile();

    // ── 3. Reconcile against what is actually on disk ───────────────────────────────
    //
    // The swing directories are the truth about which shots EXIST; diagnostics.json is the
    // truth about which have been reduced. The difference is work, and doing it here is what
    // makes three separate situations one code path: a crash mid-session, a session the panel
    // was switched on half way through, and a past session opened for review that predates
    // this feature entirely (no file at all — every shot back-fills).
    //
    // Shot ids come from the directory name (swing_0007 -> 7) rather than from a re-parse of
    // each document: the id has to agree with the carousel's, the carousel takes it from the
    // same place, and reading 30 MB to learn a number that is in the filename is not a trade
    // worth making.
    // Publish what the file gave us before the back-fill starts. NOT redundant with the rebuild
    // at the end, though off-thread it looks it: until here the reduction is the PREVIOUS
    // session's, and ingestShot() below emits busyChanged — a direct listener reading the model
    // then would see this session's rows against that session's ledgers. Synchronously it is
    // also the state the back-fill's per-shot reductions ratchet from (stage, card order).
    const QDir dir(sessionDir);
    QStringList swings = dir.entryList(QStringList{ QStringLiteral("swing_*") },
                                       QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    // The other half of the reconcile (#56): a row whose swing has LEFT the folder — moved to
    // the trash, here or in another copy of the app — is not a shot of this session any more.
    // Without this the file kept counting it on every load. Three guards, each against dropping
    // a row that is still a shot:
    //   · only when the session folder is there to ask — an unreachable share answers "no
    //     swings", which is not the same thing;
    //   · only a row FILED UNDER a swing folder (swingDirs) — a ledger written by a tool, or
    //     before rows carried their folder, names none, and is kept;
    //   · the folder is looked for BY NUMBER IN THIS SESSION, never at the path it was filed
    //     under: a session recorded on the cabin PC and opened here carries that machine's paths.
    if (dir.exists()) {
        QSet<int> onDisk;
        for (const QString &name : std::as_const(swings))
            if (const int id = folderShotId(dir.filePath(name)); id >= 0) onDisk.insert(id);
        int dropped = 0;
        for (const int id : QSet<int>(m_ingested))
            if (m_swingDirs.contains(id) && !onDisk.contains(id) && dropShot(id)) ++dropped;
        if (dropped > 0) {
            ppInfo() << "[Diagnostics]" << dropped << "row(s) left the ledger: their swings are no"
                     << "longer in" << sessionDir;
            m_owedRebuild = true;            // written once the reconcile below has settled
        }
    }

    rebuild();

    for (const QString &name : swings) {
        bool ok = false;
        const int id = name.mid(name.lastIndexOf(QLatin1Char('_')) + 1).toInt(&ok);
        if (!ok) continue;
        const QString swingDir = dir.filePath(name);
        if (!m_ingested.contains(id)) { ingestShot(id, swingDir); continue; }

        // ── 4. Regrade what went stale ─────────────────────────────────────────────
        //
        // A row graded from a document that has since been rewritten — a re-analysis, in the
        // app with this panel closed or by swinglab on another host — or against content
        // (pack, norms, contexts) that has since changed, is re-detected and REPLACED. A ledger
        // written before rows carried a stamp has none, so it regrades once and is stamped.
        const auto it = m_gradedFrom.constFind(id);
        const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(swingDir);
        if (!doc.exists()) continue;
        const bool stale = it == m_gradedFrom.constEnd()
                        || it->content != m_contentStamp
                        || it->docSize != doc.size
                        || it->docMtimeMs != doc.mtimeMs;
        if (stale) {
            m_swingDirs.insert(id, swingDir);
            queueDetect(id, swingDir, true);
        }
    }

    // ── 5. The same golfer's other sessions, for the across-sessions columns ─────────
    // Off the GUI thread and read-only; see readHistory(). Started after the reconcile so the
    // scan's own disk reads queue behind nothing this session needs first.
    scanHistory();

    emit intentChanged();
    resolveSelectedSwingDir();
    rebuild();
    // Rows the reconcile dropped are written now when nothing was queued behind them; with a
    // batch still landing, its own settle writes them.
    settleBatch();
}

void SessionDiagnosticsModel::closeSession()
{
    if (m_closed) return;
    m_pool.waitForDone();
    m_closed = true;
    rebuild();
    persist();
    // The profile is updated ONCE, at close, and from the frozen ledger. Updating it per shot
    // would let a session that ended after four swings claim the same standing as one that
    // ran to thirty, and the whole value of the profile is that a "pattern in 8 of your last
    // 10 sessions" line is countable in sessions rather than in moments.
    updateProfile();
    emit surfaceChanged();
}

// ── Intent ──────────────────────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::declareFocus(const QString &conditionId)
{
    // The split is AFTER the shots already struck: FocusSplit::baselineEnd is the index of
    // the last baseline shot, and the shot count is exactly one past it.
    declareFocusFrom(conditionId, int(m_shots.size()));
}

void SessionDiagnosticsModel::declareFocusFrom(const QString &conditionId, int fromShot)
{
    if (conditionId.isEmpty()) { clearFocus(); return; }
    // No validation against the pack: a focus the pack does not author is recorded and simply
    // matches no condition. The split cannot sit outside the rows the ledger holds.
    fromShot = std::clamp(fromShot, 0, int(m_shots.size()));
    if (m_focusConditionId == conditionId && m_focusFromShot == fromShot) return;
    m_focusConditionId = conditionId;
    m_focusFromShot = fromShot;
    emit intentChanged();
    rebuild();
    persist();
}

void SessionDiagnosticsModel::clearFocus()
{
    if (m_focusConditionId.isEmpty() && m_focusFromShot < 0) return;
    m_focusConditionId.clear();
    m_focusFromShot = -1;
    emit intentChanged();
    rebuild();
    persist();
}

void SessionDiagnosticsModel::declareMiss(const QString &missId)
{
    if (m_declaredMiss == missId) return;
    m_declaredMiss = missId;
    emit intentChanged();
    rebuild();
    persist();
}

QVariantList SessionDiagnosticsModel::missCandidates() const
{
    QVariantList out;
    if (!m_packProv) return out;
    // Pack order, never hash order — the picker must not reshuffle between launches, for the
    // same reason buildGraph() walks the pack rather than a QSet.
    for (const Condition &c : m_packProv->pack().conditions) {
        if (c.kind != ConditionKind::Outcome) continue;
        out.append(QVariantMap{
            { QStringLiteral("id"),   c.id },
            { QStringLiteral("name"), conditionName(c.id) },
        });
    }
    return out;
}

void SessionDiagnosticsModel::recordScreenResult(const QString &conditionId, bool present)
{
    if (conditionId.isEmpty()) return;
    m_screens.insert(conditionId, present);
    // A screen answered mid-session counts immediately (§A5) — the whole point of a screened
    // root is that one thirty-second test settles what no number of swings can.
    m_patternSet.clear();       // force explain() to re-run: knownScreenResults changed
    rebuild();
    persist();
}

// ── The reduction ───────────────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::rebuild()
{
    m_ledgers  = conditionLedgers(m_shots, m_opt);
    appendSessionScope();
    m_coverage = sessionCoverage(m_ledgers, m_packProv ? int(m_packProv->pack().conditions.size()) : 0);

    buildGraph();

    FocusSplit focus;
    focus.declared    = !m_focusConditionId.isEmpty() && m_focusFromShot >= 0;
    focus.baselineEnd = m_focusFromShot - 1;

    m_links = gradeLinks(m_nodes, m_edges, m_ledgers, focus, m_opt);
    m_rails = extractChains(m_nodes, m_edges, m_ledgers, m_links, m_opt);

    // The stage ratchet. `m_stage` is both the input and the output — sessionStage() is a
    // pure function of (previous, evidence, closed), so the one-way behaviour lives in the
    // header rather than in a state machine here.
    //
    // REVIEW IS NOT PASSED AS `sessionClosed`, deliberately. Looking at a session must not be
    // able to close it: the ratchet is permanent, so a live model that showed a past session
    // and then went back to live would be stuck at Closing forever. Review freezes the
    // DISPLAYED stage instead — effectiveStage() — which is the same picture and a reversible
    // fact.
    m_stage = sessionStage(m_stage, m_ledgers, m_links, m_closed, m_opt);

    // ...and the one thing the ratchet CANNOT carry across a close. sessionStage() short-
    // circuits to Closing for a closed session, so after the close `m_stage` no longer says
    // whether the session ever earned a chain — and that is precisely the question the panel's
    // composition turns on. Asked of the evidence directly (closed = false, previous = Cold),
    // so a session loaded from disk in its closed state answers it as well as a live one, and
    // latched, so a session that established and then thinned out keeps the answer the ratchet
    // gave it.
    if (m_stage == Stage::Established
        || sessionStage(Stage::Cold, m_ledgers, m_links, false, m_opt) == Stage::Established)
        m_reachedEstablished = true;

    refreshExplanation();

    // Display order, with rank-band hysteresis, over the pattern set. The previous order IS
    // the state — hystereticOrder() holds none, by design.
    {
        const std::vector<QString> prev(m_displayOrder.constBegin(), m_displayOrder.constEnd());
        const std::vector<QString> next = hystereticOrder(rankScores(), prev, m_opt.rankBandWidth);
        m_displayOrder.clear();
        for (const QString &id : next) m_displayOrder.push_back(id);
    }

    // buildThisShot() BEFORE buildHeader(): it is the one function that reads cadence, and
    // the header quotes its verdict ("BANDWIDTH · QUIET").
    buildThisShot();
    buildHeader();
    buildCards();
    buildChains();
    buildDriver();
    buildBookends();
    // LAST, because it reads the same reduction the zones do and there is no order in which it
    // could be right first. A detail left open across a shot re-derives with everything else, so
    // the page the golfer is reading accumulates evidence rather than going stale behind them.
    buildDetail();

    emit surfaceChanged();
    if (!m_detailConditionId.isEmpty()) emit detailChanged();
}

// THE SESSION-SCOPE CONDITIONS (trunk_imu_sequence_grf_design.md §10.8 E2, F10).
//
// Reduced from the shot records — the persisted order-check verdicts and placed peak instants —
// by sessionSequenceConsistency() (Coaching/sequence_checks.h), handed to the engine as the
// session-scope measures' readings, and evaluated by the SAME detect() every swing goes through, so
// the threshold, σ → pFire and ANY rules are the engine's own. Each session-scope finding becomes one
// ledger that says what the session decided and nothing about any swing: its run is all
// NotAssessable, it never sits on a ShotRecord (detectShot drops it), and every per-shot surface
// skips it. Its magnitude is 0, so it ranks after every per-swing pattern that has a gradeable
// excess — dispersion is shown, never ranked over a measured breakdown — and it enters the
// explanation exactly like any other ledger, with its pFire as P(Pattern).
void SessionDiagnosticsModel::appendSessionScope()
{
    m_sequenceConsistency.clear();
    if (!m_packProv) return;
    const CharacteristicPack &pack = m_packProv->pack();

    const SessionSequenceConsistency c = sessionSequenceConsistency(m_shots, m_opt);

    SessionMeasureSource src(pack);
    if (c.share.available) {
        MeasureReading r;
        r.value = c.share.value;   // the Wilson bound IS its uncertainty: no σ, a hard verdict (P7)
        src.set(QStringLiteral("m_ksOrderShare"), r);
    }
    if (c.spread.available) {
        MeasureReading r;
        r.value = c.spread.value;
        if (m_withSigma && std::isfinite(c.spread.sigma) && c.spread.sigma > 0.0) {
            r.measSigma = c.spread.sigma;
            r.sigmaSrc  = SigmaSource::Reading;
        }
        src.set(QStringLiteral("m_ksTimingSpread"), r);
    }

    const DetectionResult det = detect(pack, src);
    const int n = int(m_shots.size());
    bool published = false;
    for (const Finding &f : det.findings) {
        if (!isSessionScopeCondition(pack, f.conditionId)) continue;

        ConditionLedger L;
        L.id           = f.conditionId;
        L.sessionScope = true;
        L.run.assign(size_t(n), ShotState::NotAssessable);
        L.zRun.assign(size_t(n), std::numeric_limits<double>::quiet_NaN());
        L.drivingMeasureId = f.evidence.hasEvidence ? f.evidence.drivingMeasureId
                                                    : QStringLiteral("m_ksOrderShare");
        const bool assessed = f.state != FindingState::Unavailable;
        if (assessed) {
            L.assessable = c.share.available
                               ? c.share.resolved
                               : (c.spread.available ? c.spread.segments[size_t(c.spread.best)].n : 0);
            L.fired         = c.share.available ? c.share.resolved - c.share.inOrder : 0;
            L.effAssessable = c.share.effN;
            L.effFired      = c.share.effOut;
            L.wilsonLower   = c.share.lowerOut;
            L.tier          = f.state == FindingState::Fired ? Tier::Pattern : Tier::Clean;
            const double hard = L.tier == Tier::Pattern ? 1.0 : 0.0;
            L.pPattern = (m_opt.softTier && f.pFire >= 0.f) ? double(f.pFire) : hard;
        }
        L.recurrence = sequenceConsistencyLine(c);
        m_ledgers.push_back(std::move(L));

        if (!published) {
            const QString state = f.state == FindingState::Fired    ? QStringLiteral("fired")
                                : f.state == FindingState::NotFired ? QStringLiteral("clean")
                                                                    : QStringLiteral("na");
            m_sequenceConsistency = sequenceConsistencyToVariant(c, state, assessed ? double(f.pFire) : -1.0);
            m_sequenceConsistency.insert(QStringLiteral("conditionId"), f.conditionId);
            published = true;
        }
    }
    // The readings are published even with no session-scope condition in the pack: the line is a
    // count, and a surface may show it without a verdict.
    if (!published) m_sequenceConsistency = sequenceConsistencyToVariant(c, QStringLiteral("na"), -1.0);
}

QVariantMap SessionDiagnosticsModel::sequenceConsistency() const { return m_sequenceConsistency; }

// WHICH CONDITIONS ARE MARSHALLED INTO THE GRAPH, and why it is not all of them.
//
// extractChains() treats every unmeasurable node as PARTICIPATING — that is the honesty
// device: a chain that quietly omitted its unmeasurable middle would claim a continuity it
// does not have. Handing it all ~150 pack conditions would therefore hand it ~150
// participating nodes and every authored edge between them, and the rail would fill with
// paths that have nothing to do with this session.
//
// So the node set is the SESSION'S OWN NEIGHBOURHOOD, defined by three rules and nothing
// else:
//   · every pattern-tier condition (the session's assertions), plus the declared miss;
//   · everything authored UPSTREAM of those, transitively — this is what puts Chain B's
//     screened root and its two ghosts on the rail, three hops above the one live node;
//   · everything on a directed path from one of those down to the declared miss, so the
//     outcome node is reachable rather than orphaned.
// Nothing else. A condition that neither reached pattern tier nor lies between one and an
// outcome is not part of this session's picture, and drawing it would be inventing context
// rather than reporting it.
void SessionDiagnosticsModel::buildGraph()
{
    m_nodes.clear();
    m_edges.clear();
    m_missChain.clear();
    if (!m_packProv) return;

    const CharacteristicPack &pack = m_packProv->pack();

    QSet<QString> seeds;
    for (const ConditionLedger &l : m_ledgers)
        if (l.tier == Tier::Pattern) seeds.insert(l.id);
    if (!m_declaredMiss.isEmpty() && pack.condition(m_declaredMiss)) seeds.insert(m_declaredMiss);

    QSet<QString> keep = seeds;
    for (const QString &id : std::as_const(seeds))
        keep.unite(causalClosure(pack, id, /*downstream*/ false));

    if (!m_declaredMiss.isEmpty() && pack.condition(m_declaredMiss)) {
        const QSet<QString> missAncestors = causalClosure(pack, m_declaredMiss, false);
        for (const QString &id : std::as_const(seeds)) {
            const QSet<QString> below = causalClosure(pack, id, /*downstream*/ true);
            for (const QString &d : below)
                if (missAncestors.contains(d) || d == m_declaredMiss) keep.insert(d);
        }
        // The pre-armed chain, published for the panel: everything authored upstream of the
        // declared outcome, whether or not the capture can see it. Intent shapes ATTENTION.
        QStringList arm(missAncestors.constBegin(), missAncestors.constEnd());
        std::sort(arm.begin(), arm.end());
        for (const QString &id : std::as_const(arm)) {
            const ConditionLedger *l = ledger(id);
            m_missChain.append(QVariantMap{
                { QStringLiteral("id"),         id },
                { QStringLiteral("name"),       conditionName(id) },
                { QStringLiteral("measurable"), l && l->assessable >= 1 },
                { QStringLiteral("tier"),       l ? tierTag(l->tier) : QStringLiteral("clean all session") },
            });
        }
    }

    marshalGraph(keep, m_nodes, m_edges);
}

// buildGraph()'s second half, on any node set. See the header for why it is shared.
void SessionDiagnosticsModel::marshalGraph(const QSet<QString> &keep,
                                           std::vector<NodeSpec> &nodes,
                                           std::vector<EdgeSpec> &edges) const
{
    nodes.clear();
    edges.clear();
    if (!m_packProv) return;

    const CharacteristicPack &pack = m_packProv->pack();

    // Pack order, never hash order — the rail must not reshuffle between launches.
    for (const Condition &c : pack.conditions) {
        if (!keep.contains(c.id)) continue;
        const ConditionLedger *l = ledger(c.id);

        NodeSpec n;
        n.id = c.id;
        // MEASURABLE MEANS "THIS CAPTURE ANSWERED IT", not "the pack authored a measure".
        // A condition with a signal whose producer never lands is exactly as unanswerable as
        // one with no signal at all, and the session's own rows are the only honest evidence
        // of which is which on THIS capture.
        n.measurable    = l && l->assessable >= 1;
        // The three ways a node can be unmeasurable, and they are read off the pack's own
        // epistemics rather than guessed from the rows: ConfirmedBy is the field that says
        // HOW a condition can be established today.
        n.screened      = c.confirmedBy == ConfirmedBy::Screened;
        n.screenEntered = m_screens.contains(c.id);
        n.asserted      = c.confirmedBy == ConfirmedBy::Asserted;
        if (!m_declaredMiss.isEmpty() && c.id == m_declaredMiss) n.outcomeId = m_declaredMiss;
        // TEMPORAL ORDER IS APPROXIMATED BY CONDITION GROUP, which is gradeLink()'s own
        // stated approximation: the group order IS the swing (setup, posture, lateral, arms
        // and club, release, sequence, impact, finish, ball flight), and it is the finest
        // ordering available until the detector timestamps a finding within a shot.
        const auto &groups = allConditionGroups();
        n.phaseOrder = int(std::find(groups.begin(), groups.end(), c.group) - groups.begin());
        nodes.push_back(std::move(n));
    }

    for (const Edge &e : pack.edges) {
        if (e.type != EdgeType::Causes) continue;         // rule 3: only authored causation
        if (!keep.contains(e.from) || !keep.contains(e.to)) continue;
        EdgeSpec s;
        s.from = e.from;
        s.to   = e.to;
        // The pack's Edge carries no directional SENSE — it says A causes B, not "A high
        // implies B high". Type 0 is the honest marshalling: gradeLink() then rests
        // coherence on temporal order alone rather than on a sign nobody authored.
        s.type     = 0;
        s.strength = int(e.strength);
        // Computed by the pack at load (stampSharedEvidence): both ends read one measure, so the
        // link caps at Coherent untested (posture_metrics_design.md §6.5).
        s.sharedEvidence = e.sharedEvidence;
        edges.push_back(std::move(s));
    }
}

void SessionDiagnosticsModel::refreshExplanation()
{
    QStringList patterns;
    for (const ConditionLedger &l : m_ledgers)
        if (l.tier == Tier::Pattern) patterns.push_back(l.id);
    patterns.sort();

    if (patterns == m_patternSet) return;      // membership unchanged: no re-rank (§A5)
    m_patternSet = patterns;

    if (!m_packProv) { m_explanation = Explanation(); return; }

    // explain() takes a DetectionResult, and what it is handed here is deliberately not one
    // shot's. It is the SESSION's assertion — every pattern-tier condition presented as
    // Fired, every other assessed condition as NotFired — so the greedy set cover runs over
    // what the session believes rather than over what the last ball happened to do. That is
    // the whole of §A5's "over the pattern-tier fired set, not per shot".
    //
    // §A8.6: each condition also carries its P(Pattern) as soft evidence (pFire), so a pattern that
    // only just survives its readings' uncertainty weighs less than a certain one, and an assessed
    // non-pattern that nearly made it is not read as a clean absence. With softTier off pPattern is
    // the hard tier, and the evidence is exactly the pre-§A8 assertion.
    DetectionResult det;
    for (const ConditionLedger &l : m_ledgers) {
        if (l.tier == Tier::Pattern) {
            Finding f;
            f.conditionId = l.id;
            f.state       = FindingState::Fired;
            f.confidence  = 1.0f;
            f.pFire       = float(l.pPattern);
            det.findings.push_back(std::move(f));
        } else if (l.assessable >= 1) {
            Finding f;
            f.conditionId = l.id;
            f.state       = FindingState::NotFired;
            f.pFire       = float(l.pPattern);
            det.findings.push_back(std::move(f));
        }
    }
    m_explanation = explain(m_packProv->pack(), det, m_screens, m_explainOpt);

    // Root stability (§A8.6): the same draws that gave pPattern, each explained as a HARD session
    // (the drawn Pattern set Fired, every other assessed condition NotFired). A session whose rows
    // carry no probability draws its hard set every time, and every root is then "firm".
    if (m_opt.softTier) {
        // Most draws repeat a handful of Pattern sets — only borderline conditions ever vary — so
        // each DISTINCT set is explained once and the draw list holds copies.
        std::vector<Explanation> drawn;
        QHash<QString, int>      seen;
        // patternDraws() knows only the per-swing rows, so each session-scope ledger joins every
        // draw by its own deterministic Bernoulli(P(Pattern)) — the same share of draws the row
        // draws would have given it, on a stream of its own so the row draws are untouched.
        std::vector<std::pair<QString, double>> sessionP;
        for (const ConditionLedger &l : m_ledgers)
            if (l.sessionScope && l.assessable >= 1) sessionP.push_back({ l.id, l.pPattern });
        DetRng sessionRng(m_opt.mcSeed ^ 0x5E5510A5E5510A5Eull);
        for (QSet<QString> pat : patternDraws(m_shots, m_opt)) {
            for (const auto &sp : sessionP)
                if (sessionRng.uniform() < sp.second) pat.insert(sp.first);
            QStringList key(pat.begin(), pat.end());
            key.sort();
            const QString k = key.join(QLatin1Char('|'));
            if (const auto it = seen.constFind(k); it != seen.constEnd()) {
                drawn.push_back(drawn[size_t(it.value())]);
                continue;
            }
            DetectionResult dd;
            for (const ConditionLedger &l : m_ledgers) {
                if (l.assessable < 1) continue;
                Finding f;
                f.conditionId = l.id;
                f.state       = pat.contains(l.id) ? FindingState::Fired : FindingState::NotFired;
                f.confidence  = 1.0f;
                dd.findings.push_back(std::move(f));
            }
            seen.insert(k, int(drawn.size()));
            drawn.push_back(explain(m_packProv->pack(), dd, m_screens, m_explainOpt));
        }
        stampStability(m_explanation, drawn);
    }
}

// THE ONLY READER OF THE FAULT PROFILE. hystereticOrder()'s comment names this as the one
// place a prevalence bias may legitimately enter, and the reason it is safe is structural:
// this function returns a display SCORE, the score reaches nothing but an ordering, and there
// is no path from an ordering to a tier, a corridor or a firing.
std::vector<RankedCondition> SessionDiagnosticsModel::rankScores() const
{
    std::vector<RankedCondition> out;
    for (const ConditionLedger &l : m_ledgers) {
        if (l.tier != Tier::Pattern) continue;
        RankedCondition r;
        r.id = l.id;
        // ⚠ THE GATE IS THE EVIDENCE TEST; THE ORDER IS THE MAGNITUDE.
        //
        // This ranked on the Wilson bound — how sure the session is that a condition RECURS —
        // and on a real capture that key carries almost no information, because the conditions
        // that reach this row mostly fire on every shot. Twelve patterns on the 9 Sep session
        // produced EIGHT with an identical 0.589, so the sort had nothing to separate them and
        // fell back on pack order, which hysteresis then froze. The card with the worst reading
        // in the session sat fourth behind three milder ones. That is not a mis-sort, it is an
        // unsorted row wearing a sort.
        //
        // Only pattern-tier conditions are ranked here, so every one of them has ALREADY passed
        // the recurrence test — asking the order to re-litigate evidence is what produced the
        // tie. So the primary key is how far out it goes when it goes, and the bound becomes
        // the tiebreaker it is good at being.
        //
        // ⚠ AND THIS IS THE STRENGTH LADDER REACHING SOMETHING. The meter was published on the
        // explicit promise that it could not move a tier, a recurrence, a trend or a link —
        // and it still cannot; a display ORDER is none of those, and there is no path from one
        // to any of them. But it is a deliberate loosening of "it reaches nothing" and it is
        // written down here rather than left for a reader to discover.
        r.score = l.firingExcess;
        // The bound, scaled so it settles a near-tie on magnitude and never overturns a real
        // difference in it: excess runs to several band-widths, this term to a tenth of one.
        r.score += 0.10 * l.wilsonLower;
        // Then history, at the same weight: enough to break a tie between two equally placed
        // patterns in favour of the one this golfer keeps producing, never enough to lift a
        // mild one over a severe one.
        r.score += 0.10 * profileBias(l.id);
        // And the two demotions the pack already carries. Immateriality is RANKING ONLY by
        // ContextBinding's own contract, and this is where it is allowed to act.
        const ConditionRow *latest = m_shots.empty() ? nullptr : rowFor(m_shots.back(), l.id);
        if (latest && !latest->material)        r.score -= 0.05;
        if (latest && latest->contextInferred)  r.score -= 0.05;
        out.push_back(std::move(r));
    }
    return out;
}

double SessionDiagnosticsModel::profileBias(const QString &conditionId) const
{
    const QJsonObject conds = m_profile.value(QStringLiteral("conditions")).toObject();
    const QJsonObject c     = conds.value(conditionId).toObject();
    const double seen    = c.value(QStringLiteral("sessionsSeen")).toDouble(0.0);
    const double pattern = c.value(QStringLiteral("sessionsPattern")).toDouble(0.0);
    if (!(seen > 0.0)) return 0.0;
    return std::clamp(pattern / seen, 0.0, 1.0);
}

// ── Persistence ─────────────────────────────────────────────────────────────────────────

QString SessionDiagnosticsModel::diagnosticsPath() const
{
    return m_sessionDir.isEmpty() ? QString() : QDir(m_sessionDir).filePath(kDiagnosticsFile);
}

QString SessionDiagnosticsModel::faultProfilePath() const
{
    // The ATHLETE folder, which is the session directory's parent —
    // <athleteLibraryPath>/<athlete>/<session>/. A profile written inside the session folder
    // would be a profile with a sample size of one, which is the one thing it must not be.
    if (m_sessionDir.isEmpty()) return QString();
    QDir d(m_sessionDir);
    if (!d.cdUp()) return QString();
    return d.filePath(kProfileFile);
}

QJsonObject SessionDiagnosticsModel::envelope() const
{
    QJsonArray lm;
    QList<int> lmIds(m_lmShots.constBegin(), m_lmShots.constEnd());
    std::sort(lmIds.begin(), lmIds.end());
    for (int id : std::as_const(lmIds)) lm.append(id);

    QJsonObject dirs;
    for (auto it = m_swingDirs.constBegin(); it != m_swingDirs.constEnd(); ++it)
        dirs.insert(QString::number(it.key()), it.value());

    QJsonObject session;
    session[QStringLiteral("sessionDir")] = m_sessionDir;
    session[QStringLiteral("sessionId")]  = QFileInfo(m_sessionDir).fileName();
    session[QStringLiteral("closed")]     = m_closed;
    session[QStringLiteral("stage")]      = stageName(m_stage);
    session[QStringLiteral("reachedEstablished")] = m_reachedEstablished;
    session[QStringLiteral("shotCount")]  = int(m_shots.size());
    session[QStringLiteral("gradePolicy")]= m_policyName;
    session[QStringLiteral("lmShots")]    = lm;
    session[QStringLiteral("swingDirs")]  = dirs;
    QJsonObject graded;
    for (auto it = m_gradedFrom.constBegin(); it != m_gradedFrom.constEnd(); ++it)
        graded.insert(QString::number(it.key()),
                      QJsonObject{ { QStringLiteral("size"),    double(it->docSize) },
                                   { QStringLiteral("mtimeMs"), double(it->docMtimeMs) },
                                   { QStringLiteral("content"), it->content } });
    session[QStringLiteral("gradedFrom")] = graded;
    session[QStringLiteral("writtenAtMs")]= double(QDateTime::currentMSecsSinceEpoch());

    QJsonObject intent;
    intent[QStringLiteral("focusConditionId")] = m_focusConditionId;
    intent[QStringLiteral("focusFromShot")]    = m_focusFromShot;
    intent[QStringLiteral("declaredMiss")]     = m_declaredMiss;

    QJsonObject screens;
    for (auto it = m_screens.constBegin(); it != m_screens.constEnd(); ++it)
        screens.insert(it.key(), it.value());

    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kEnvelopeVersion;
    root[QStringLiteral("session")]       = session;
    root[QStringLiteral("intent")]        = intent;
    root[QStringLiteral("screens")]       = screens;
    // The ledger is nested rather than merged, so diagnostic_ledger.h's reader takes exactly
    // the object its writer produced — including ITS schemaVersion, which is a different
    // number about a different contract.
    root[QStringLiteral("ledger")]        = toJson(m_shots, m_opt);
    return root;
}

void SessionDiagnosticsModel::persist()
{
    const QString path = diagnosticsPath();
    if (path.isEmpty() || !m_persistEnabled) return;
    // The house's write-to-temp-then-rename helper (pack_io.h), not a bare QFile: an
    // interrupted write must not truncate the session's evidence, and a half-written
    // diagnostics.json would load, report what survived, and look exactly like a session in
    // which nothing happened after shot 6.
    atomicWrite(path, QJsonDocument(envelope()).toJson(QJsonDocument::Compact));
    ++m_persistCount;
}

void SessionDiagnosticsModel::loadProfile()
{
    m_profile = QJsonObject();
    m_expectations.clear();

    const QString path = faultProfilePath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    m_profile = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    // The Cold state's "usually yours" list. EXPECTATIONS TO TEST, and the wording has to
    // keep saying so — these are dashed cards in the design precisely because they are the
    // one thing on the panel that is not evidence from this session.
    const QJsonObject conds = m_profile.value(QStringLiteral("conditions")).toObject();
    struct Row { QString id; int seen; int pattern; QString last; QString trend; };
    std::vector<Row> rows;
    for (auto it = conds.constBegin(); it != conds.constEnd(); ++it) {
        const QJsonObject c = it.value().toObject();
        Row r;
        r.id      = it.key();
        r.seen    = c.value(QStringLiteral("sessionsSeen")).toInt();
        r.pattern = c.value(QStringLiteral("sessionsPattern")).toInt();
        r.last    = c.value(QStringLiteral("lastSeenSession")).toString();
        r.trend   = c.value(QStringLiteral("trend")).toString();
        if (r.seen < 1 || r.pattern < 1) continue;    // nothing to expect
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        const double pa = double(a.pattern) / double(a.seen);
        const double pb = double(b.pattern) / double(b.seen);
        if (pa != pb) return pa > pb;
        if (a.pattern != b.pattern) return a.pattern > b.pattern;
        return a.id < b.id;
    });

    for (const Row &r : rows) {
        m_expectations.append(QVariantMap{
            { QStringLiteral("id"),   r.id },
            { QStringLiteral("name"), conditionName(r.id) },
            // A COUNT over sessions, for the same reason recurrence is a count over shots: at
            // this n a percentage is fabricated precision.
            { QStringLiteral("text"), QStringLiteral("pattern in %1 of your last %2 sessions")
                                          .arg(r.pattern).arg(r.seen) },
            { QStringLiteral("trend"), r.trend },
            { QStringLiteral("lastSeenSession"), r.last },
            // The sentence the design puts on the panel and the implementation has to keep
            // true. It ships as data so no surface can drop it.
            { QStringLiteral("caveat"),
              QStringLiteral("an expectation to test, not a finding") },
        });
    }
}

void SessionDiagnosticsModel::updateProfile()
{
    const QString path = faultProfilePath();
    if (path.isEmpty()) return;

    const QString sessionId = QFileInfo(m_sessionDir).fileName();
    QJsonObject conds = m_profile.value(QStringLiteral("conditions")).toObject();

    for (const ConditionLedger &l : m_ledgers) {
        // Only conditions this session could actually SEE update their history. A condition
        // the capture never assessed is not evidence that it was absent, and counting it as a
        // session-seen-but-not-pattern would quietly drive its prevalence to zero every time
        // the camera could not answer it.
        if (l.assessable < 1) continue;

        QJsonObject c = conds.value(l.id).toObject();
        const int seenBefore    = c.value(QStringLiteral("sessionsSeen")).toInt();
        const int patternBefore = c.value(QStringLiteral("sessionsPattern")).toInt();
        const QString lastSession = c.value(QStringLiteral("lastSeenSession")).toString();
        if (lastSession == sessionId) continue;        // idempotent: closing twice counts once

        const int seen    = seenBefore + 1;
        const int pattern = patternBefore + (l.tier == Tier::Pattern ? 1 : 0);

        // A three-valued trend and nothing finer. "rising"/"falling"/"steady" over the
        // prevalence before and after this session is all two numbers can support, and a
        // slope over a handful of sessions dressed as a percentage would be the same
        // fabricated precision the recurrence caption refuses.
        QString trend = QStringLiteral("steady");
        if (seenBefore > 0) {
            const double before = double(patternBefore) / double(seenBefore);
            const double after  = double(pattern) / double(seen);
            if (after > before + 1e-9)      trend = QStringLiteral("rising");
            else if (after < before - 1e-9) trend = QStringLiteral("falling");
        }

        c[QStringLiteral("sessionsSeen")]    = seen;
        c[QStringLiteral("sessionsPattern")] = pattern;
        c[QStringLiteral("trend")]           = trend;
        if (l.tier == Tier::Pattern) c[QStringLiteral("lastPatternSession")] = sessionId;
        c[QStringLiteral("lastSeenSession")] = sessionId;
        conds.insert(l.id, c);
    }

    QJsonObject root = m_profile;
    root[QStringLiteral("schemaVersion")] = kEnvelopeVersion;
    root[QStringLiteral("conditions")]    = conds;
    root[QStringLiteral("updatedAtMs")]   = double(QDateTime::currentMSecsSinceEpoch());
    m_profile = root;
    atomicWrite(path, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ── Work-ons ────────────────────────────────────────────────────────────────────────────

// The pack's facts, marshalled into work_ons.h's pack-agnostic inputs — which conditions are
// movements a golfer can work on, the authored causal edges, where each is read — and the list
// reduced from them: work_ons_pack.h, the ONE function the Charts Posture section's order reads too.
SessionWorkOns SessionDiagnosticsModel::sessionWorkOns() const
{
    SessionWorkOns out = sessionWorkOnsForPack(m_packProv ? &m_packProv->pack() : nullptr, m_shots, m_ledgers);
    out.sessionId = QFileInfo(m_sessionDir).fileName();
    for (WorkOnEntry &e : out.entries) e.name = conditionName(e.id);
    return out;
}

// ── Lookups and formatters ──────────────────────────────────────────────────────────────

const ConditionLedger *SessionDiagnosticsModel::ledger(const QString &id) const
{
    return ledgerFor(m_ledgers, id);
}

QString SessionDiagnosticsModel::conditionName(const QString &id) const
{
    if (!m_packProv) return id;
    const Condition *c = m_packProv->pack().condition(id);
    return (c && !c->label.isEmpty()) ? c->label : id;
}

QString SessionDiagnosticsModel::consequenceOf(const QString &id) const
{
    if (!m_packProv) return QString();
    const Condition *c = m_packProv->pack().condition(id);
    return c ? c->consequence.text() : QString();
}

QString SessionDiagnosticsModel::measureLabelOf(const QString &measureId) const
{
    if (measureId.isEmpty() || !m_packProv) return QString();
    const Measure *m = m_packProv->pack().measure(measureId);
    return m ? measureDisplayLabel(*m) : measureId;
}

QString SessionDiagnosticsModel::measureUnitOf(const QString &measureId) const
{
    if (measureId.isEmpty() || !m_packProv) return QString();
    const Measure *m = m_packProv->pack().measure(measureId);
    return m ? m->unit : QString();
}

QString SessionDiagnosticsModel::measurePhaseOf(const QString &measureId) const
{
    if (measureId.isEmpty() || !m_packProv) return QString();
    // Where the reduction LANDS, which is the phase a card should name: the anchor for an
    // `at`, the window's end for a delta, a rate or an extremum. One rule, condition_reading.h's,
    // so the card and the charts' condition → reading link cannot name different instants.
    const ConditionReading r = measureReading(m_packProv->pack(), measureId);
    return r.found ? phaseLabel(Phase(r.phase)) : QString();
}

int SessionDiagnosticsModel::indexOfShot(int shotId) const
{
    for (int i = 0; i < int(m_shots.size()); ++i)
        if (m_shots[size_t(i)].shotId == shotId) return i;
    return -1;
}

int SessionDiagnosticsModel::focusIndex() const
{
    if (m_shots.empty()) return -1;
    if (m_reviewing && m_selectedShotId >= 0) {
        const int i = indexOfShot(m_selectedShotId);
        if (i >= 0) return i;
    }
    return int(m_shots.size()) - 1;
}

int SessionDiagnosticsModel::detailFocusIndex() const
{
    if (m_selectedShotId >= 0) {
        const int i = indexOfShot(m_selectedShotId);
        if (i >= 0) return i;
    }
    return focusIndex();
}

QVariantList SessionDiagnosticsModel::ticksFor(const ConditionLedger &l, int selectedIndex) const
{
    QVariantList out;
    for (int i = 0; i < int(l.run.size()); ++i) {
        QVariantMap t{
            { QStringLiteral("state"),  shotStateKind(l.run[size_t(i)]) },
            { QStringLiteral("shotId"), i < int(m_shots.size()) ? m_shots[size_t(i)].shotId : -1 },
            // The selected shot's tick is drawn wide and outlined (brief §6). Which tick that
            // is, is decided here rather than by the QML comparing ids in a delegate.
            { QStringLiteral("selected"), i == selectedIndex },
        };
        // a not-assessable tick a gate or rule accounts for: `gated`, its words, and the reading (§7.0)
        if (i < int(m_shots.size()))
            addGate(t, rowFor(m_shots[size_t(i)], l.id), m_swingDirs.value(m_shots[size_t(i)].shotId));
        out.append(t);
    }
    return out;
}

// ── How far, not only whether (session_spread.h) ────────────────────────────────────────
//
// The tick run beside these says WHETHER each shot was outside its corridor; everything from here
// to historyFor() says HOW FAR, in the measure's own units, against the bands that grade. It is
// arithmetic over the rows already in the ledger and the norm already resolved for them — no new
// analysis, no new verdict, nothing that could move a tier.

SpreadCorridor SessionDiagnosticsModel::corridorForRow(const ConditionRow &r) const
{
    // The norm for the row's own measure and context, as it grades today; the row's stored Ideal
    // band only when none resolves (spreadCorridorFromRow). A row graded against an authored
    // number has neither, and says so.
    const GradePolicy policy = gradePolicyByName(m_policyName);
    if (m_norms && !r.drivingMeasureId.isEmpty()) {
        Shape shape = Shape::Target;
        if (m_packProv)
            if (const Measure *m = m_packProv->pack().measure(r.drivingMeasureId)) shape = m->shape;
        const NormResolution res = m_norms->resolve(r.drivingMeasureId, r.contextId);
        if (res.found()) return spreadCorridorFromNorm(*res.norm, shape, policy);
    }
    return spreadCorridorFromRow(r, policy);
}

SpreadCorridor SessionDiagnosticsModel::spreadCorridorOf(const ConditionLedger &l, int fi,
                                                         QVariantMap *normInfo,
                                                         const QString &measure) const
{
    const QString measureId = measure.isEmpty() ? l.drivingMeasureId : measure;
    auto usable = [&](const ConditionRow *r) {
        if (!r || r->state == ShotState::NotAssessable) return false;
        if (measureId.isEmpty()) return std::isfinite(r->value);
        const std::optional<MeasureRow> m = rowReadingOf(*r, measureId);
        return m && std::isfinite(m->value);
    };

    // WHICH CONTEXT'S NORM. The focus shot's, when it read this condition — that is the swing the
    // ringed dot is, and its corridor is the one it was graded against. Otherwise the session's
    // commonest, so a strip over a one-club session does not change its bands because the newest
    // swing happened to be unmeasurable.
    const ConditionRow *rep = nullptr;
    if (fi >= 0 && fi < int(m_shots.size())) {
        const ConditionRow *r = rowFor(m_shots[size_t(fi)], l.id);
        if (usable(r)) rep = r;
    }
    if (!rep) {
        QHash<QString, int> seen;
        int best = 0;
        for (const ShotRecord &s : m_shots) {
            const ConditionRow *r = rowFor(s, l.id);
            if (!usable(r)) continue;
            const int k = ++seen[r->contextId];
            if (k > best) { best = k; rep = r; }
        }
    }

    const GradePolicy policy = gradePolicyByName(m_policyName);
    SpreadCorridor c;
    if (m_norms && !measureId.isEmpty()) {
        Shape shape = Shape::Target;
        if (m_packProv)
            if (const Measure *m = m_packProv->pack().measure(measureId)) shape = m->shape;
        const NormResolution res = m_norms->resolve(measureId, rep ? rep->contextId : QString());
        if (res.found()) {
            c = spreadCorridorFromNorm(*res.norm, shape, policy);
            if (normInfo) {
                (*normInfo)[QStringLiteral("source")]   = normSourceName(res.norm->source);
                (*normInfo)[QStringLiteral("tag")]      = normTagOf(true, res.norm->source, res.norm->n);
                (*normInfo)[QStringLiteral("tagLong")]  = normTagLongOf(true, res.norm->source, res.norm->n);
                (*normInfo)[QStringLiteral("citation")] = res.norm->citation;
            }
        }
    }
    if (!c.known && rep) {
        // The stored band of THIS measure's reading, not the driving one's.
        ConditionRow band = *rep;
        if (!measureId.isEmpty())
            if (const std::optional<MeasureRow> m = rowReadingOf(*rep, measureId)) {
                band.drivingMeasureId = m->measureId;
                band.value            = m->value;
                band.corridorLo       = m->corridorLo;
                band.corridorHi       = m->corridorHi;
                band.z                = m->z;
                band.corridorShape    = m->corridorShape;
            }
        c = spreadCorridorFromRow(band, policy);
        if (c.known && normInfo) {
            (*normInfo)[QStringLiteral("source")]   = QStringLiteral("stored");
            (*normInfo)[QStringLiteral("tag")]      = normTagOf(false, NormSource::Heuristic, 0);
            (*normInfo)[QStringLiteral("tagLong")]  = normTagLongOf(false, NormSource::Heuristic, 0);
            (*normInfo)[QStringLiteral("citation")] = QString();
        }
    }
    return c;
}

namespace {

// The bands and the fault lines as the QML takes them: every position a fraction of
// the axis, every edge in units beside it for whoever wants to quote it. Shared by the strip and
// the across-sessions columns so the two cannot disagree about where a band is.
void publishCorridor(QVariantMap &out, const SpreadCorridor &c, const SpreadAxis &a,
                     const QString &unit)
{
    out[QStringLiteral("axisLo")]        = a.lo;
    out[QStringLiteral("axisHi")]        = a.hi;
    out[QStringLiteral("decimals")]      = a.decimals;
    out[QStringLiteral("corridorKnown")] = c.known;
    out[QStringLiteral("shape")]         = c.known ? corridorShapeToString(c.shape) : QStringLiteral("none");

    QVariantList bands;
    for (const SpreadBand &b : spreadBands(c, a))
        bands.append(QVariantMap{
            { QStringLiteral("grade"), b.grade },
            { QStringLiteral("lo"),    b.lo },
            { QStringLiteral("hi"),    b.hi },
            { QStringLiteral("f0"),    a.fraction(b.lo) },
            { QStringLiteral("f1"),    a.fraction(b.hi) },
        });
    out[QStringLiteral("bands")] = bands;

    // THE FAULT LINE IS WHERE ACTION BEGINS — the line an author states when they say "more than
    // 13 % of the hand rise is the fault" (norms.json). Not the signal edge, where Watch begins and
    // the condition starts to fire: that one is a band boundary and is drawn as one.
    QVariantList faults;
    auto fault = [&](double v) {
        if (v < a.lo || v > a.hi) return;
        faults.append(QVariantMap{
            { QStringLiteral("value"), v },
            { QStringLiteral("f"),     a.fraction(v) },
            { QStringLiteral("text"),  QStringLiteral("fault at %1").arg(fmtSpread(v, a.decimals, unit)) },
            // The number alone, for a strip too narrow for the sentence — two fault lines on a
            // two-sided corridor at the 396 split leave each label a hundred pixels at most.
            { QStringLiteral("shortText"), fmtSpread(v, a.decimals, unit) },
        });
    };
    if (c.known && !c.lowOpen)  fault(c.faultLo);
    if (c.known && !c.highOpen) fault(c.faultHi);
    out[QStringLiteral("faultLines")] = faults;

    // THE Y LABELS for the value run and the across-sessions columns: round values on the axis's
    // own decimals (trailing zeros dropped), the unit printed once by the chart, not on every tick.
    QVariantList ticks;
    for (double v : spreadTickValues(a))
        ticks.append(QVariantMap{
            { QStringLiteral("value"), v },
            { QStringLiteral("f"),     a.fraction(v) },
            { QStringLiteral("text"),  spreadNumber(v, a.decimals, /*trimZeros*/ true) },
        });
    out[QStringLiteral("yTicks")] = ticks;

    out[QStringLiteral("muF")]    = c.known ? a.fraction(c.mu) : -1.0;
    out[QStringLiteral("muText")] = c.known ? fmtSpread(c.mu, a.decimals, unit) : QString();
}

} // namespace

QVariantMap SessionDiagnosticsModel::spreadFor(const ConditionLedger &l, int fi, int selectedTick,
                                               const QString &measure) const
{
    const QString measureId = measure.isEmpty() ? l.drivingMeasureId : measure;
    // A COMPOSITE draws no strip, run or box: those are its number, per shot (§8, §12).
    if (readsComposite(l, measureId)) return compositeSpreadFor(l, fi, measureId);
    QVariantMap out;
    const QString unit      = measureUnitOf(measureId);
    const int n             = int(m_shots.size());

    // Every shot's reading OF THIS MEASURE, in shot order — NaN where there is none to place.
    // A condition read by several measures records each (ConditionRow::readings), so a shot
    // decided by a sibling measure still has its place on this ruler, drawn by THIS measure's own
    // verdict: a dot is "fired" only where this measure fired. A row from before the readings
    // were recorded holds its driving measure alone, and a shot decided by another is not
    // placeable here — the honest reading of "not on this ruler"; the ledger's counts are
    // untouched either way.
    std::vector<double> values(size_t(n), std::numeric_limits<double>::quiet_NaN());
    std::vector<QString> states(size_t(n), QStringLiteral("notAssessable"));
    // the gate or rule behind each shot's missing reading, where one accounts for it (rowGate)
    std::vector<RowGate> gatesOf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const ConditionRow *r = rowFor(m_shots[size_t(i)], l.id);
        if (!r) continue;
        gatesOf[size_t(i)] = rowGate(r);
        states[size_t(i)] = shotStateKind(r->state);
        if (r->state == ShotState::NotAssessable) continue;
        if (measureId.isEmpty()) {
            if (std::isfinite(r->value)) values[size_t(i)] = r->value;
            continue;
        }
        const std::optional<MeasureRow> m = rowReadingOf(*r, measureId);
        if (!m || !std::isfinite(m->value)) {
            states[size_t(i)] = QStringLiteral("notAssessable");
            continue;
        }
        states[size_t(i)] = m->fired ? QStringLiteral("fired") : QStringLiteral("clean");
        values[size_t(i)] = m->value;
    }

    std::vector<double> placed;
    std::vector<int>    placedIdx;
    for (int i = 0; i < n; ++i)
        if (std::isfinite(values[size_t(i)])) { placed.push_back(values[size_t(i)]); placedIdx.push_back(i); }

    QVariantMap normInfo;
    const SpreadCorridor c = spreadCorridorOf(l, fi, &normInfo, measureId);
    const SpreadAxis     a = spreadAxisFor(c, placed);

    out[QStringLiteral("measure")] = measureLabelOf(measureId);
    out[QStringLiteral("unit")]    = unit;
    publishCorridor(out, c, a, unit);

    out[QStringLiteral("normSource")]   = normInfo.value(QStringLiteral("source"));
    out[QStringLiteral("normTag")]      = normInfo.value(QStringLiteral("tag"));
    out[QStringLiteral("normTagLong")]  = normInfo.value(QStringLiteral("tagLong"));
    out[QStringLiteral("normCitation")] = normInfo.value(QStringLiteral("citation"));

    // ── the dots: assessable shots only, in shot order ───────────────────────────────
    std::vector<double> fractions;
    std::vector<int>    clips;
    fractions.reserve(placed.size());
    for (double v : placed) { int cl = 0; fractions.push_back(a.fraction(v, &cl)); clips.push_back(cl); }
    const std::vector<int> stacks = spreadStacks(fractions, kSpreadBins);

    int stackMax = 0, pastFault = 0;
    QVariantList dots;
    for (size_t k = 0; k < placed.size(); ++k) {
        const int i = placedIdx[k];
        const int shotId = m_shots[size_t(i)].shotId;
        stackMax = std::max(stackMax, stacks[k]);
        if (c.known && ((!c.highOpen && placed[k] > c.faultHi) || (!c.lowOpen && placed[k] < c.faultLo)))
            ++pastFault;
        dots.append(QVariantMap{
            { QStringLiteral("index"),     i },
            { QStringLiteral("shotId"),    shotId },
            { QStringLiteral("swingDir"),  m_swingDirs.value(shotId) },
            { QStringLiteral("value"),     placed[k] },
            { QStringLiteral("f"),         fractions[k] },
            { QStringLiteral("clipped"),   clips[k] },
            { QStringLiteral("stack"),     stacks[k] },
            { QStringLiteral("state"),     states[size_t(i)] },
            // THE CURRENT SHOT is the one the panel is talking about — the newest live, the
            // carousel's pick in review (focusIndex()) — and it is drawn ringed and large.
            { QStringLiteral("current"),   i == fi },
            { QStringLiteral("recent"),    k + size_t(kSpreadRecent) >= placed.size() },
            { QStringLiteral("valueText"), fmtSpread(placed[k], a.decimals, unit) },
        });
    }
    out[QStringLiteral("dots")]     = dots;
    out[QStringLiteral("stackMax")] = stackMax;
    {
        std::vector<int> ids;
        for (int i : placedIdx) ids.push_back(m_shots[size_t(i)].shotId);
        out[QStringLiteral("box")] = boxOf(placed, ids, a, unit);
    }

    // THE EDGE MARKERS' WORDS. A pinned dot says "further than this", and the marker beside it
    // says how much further: the most extreme reading on that side, and how many share the edge.
    // One label per side, decided here — two dots pinned at one pixel would otherwise draw two
    // labels on top of each other.
    for (int side : { -1, +1 }) {
        int count = 0;
        double extreme = side > 0 ? -std::numeric_limits<double>::infinity()
                                  :  std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < placed.size(); ++k) {
            if (clips[k] != side) continue;
            ++count;
            extreme = side > 0 ? std::max(extreme, placed[k]) : std::min(extreme, placed[k]);
        }
        const QString key = side > 0 ? QStringLiteral("clipHi") : QStringLiteral("clipLo");
        out[key + QStringLiteral("Count")] = count;
        out[key + QStringLiteral("Far")]   = count ? fmtSpread(extreme, a.decimals, unit) : QString();
        out[key + QStringLiteral("Text")] =
            count == 0 ? QString()
          : count == 1 ? fmtSpread(extreme, a.decimals, unit)
                       : QStringLiteral("%1 off scale, to %2").arg(count)
                             .arg(fmtSpread(extreme, a.decimals, unit));
    }

    // ── the readout: one line per shot, for the line between the strip and the run ────────
    //
    // WHAT THE HOVERED SHOT IS, in words, on one ruler: its number in the session, its reading
    // at the axis's own precision, where that sits against the bands drawn above it, and the
    // verdict the ledger recorded. A shot nobody measured reads "-" in every slot that would hold
    // a number — the panel's NA marker — and says so, rather than a zero or a blank.
    auto readoutOf = [&](int i) -> QString {
        const double v = values[size_t(i)];
        if (!std::isfinite(v))
            return QStringLiteral("shot %1 · - · %2").arg(i + 1)
                .arg(gatesOf[size_t(i)].on ? gatesOf[size_t(i)].words : QStringLiteral("not measured"));
        const QString verdict = states[size_t(i)] == QLatin1String("fired") ? QStringLiteral("fired")
                              : states[size_t(i)] == QLatin1String("clean") ? QStringLiteral("clean")
                                                                            : QStringLiteral("-");
        return QStringLiteral("shot %1 · %2 · %3 · %4")
            .arg(i + 1)
            .arg(fmtSpread(v, a.decimals, unit), spreadPositionWords(c, v, a.decimals), verdict);
    };
    out[QStringLiteral("currentReadout")] = (fi >= 0 && fi < n) ? readoutOf(fi) : QString();

    // ── the card's two figures ────────────────────────────────────────────────────────
    //
    // THE SESSION MEDIAN AND THIS SHOT, as bare numbers for the card to print large with the unit
    // once beside them. The MEDIAN, not the mean, and for the reason the strip's caption gives:
    // one wild swing drags a mean (27.9 % in 2026-10-07's 50 took it from 12 to 13), and a card
    // saying 13 over a detail page saying "median 12" would be the panel contradicting itself.
    // This shot is the focus shot — the newest live, the carousel's pick in review — with "-"
    // when it was not measured, never 0, and its fired/clean state for the colour.
    {
        const double cur = (fi >= 0 && fi < n) ? values[size_t(fi)] : std::numeric_limits<double>::quiet_NaN();
        out[QStringLiteral("medianNumber")]  = fmtSpread(spreadQuantile(placed, 0.5), a.decimals);
        out[QStringLiteral("currentNumber")] = fmtSpread(cur, a.decimals);
        out[QStringLiteral("currentState")]  = (fi >= 0 && fi < n && std::isfinite(cur))
                                               ? states[size_t(fi)] : QStringLiteral("notAssessable");
        // THE GATE BEHIND THIS SHOT'S "-": the figure stays "-" (a figure slot never holds words), and
        // beside it the gate's words and the reading a click opens — empty when nothing gated it
        const RowGate cg = (fi >= 0 && fi < n && !std::isfinite(cur)) ? gatesOf[size_t(fi)] : RowGate();
        out[QStringLiteral("currentGated")] = cg.on;
        out[QStringLiteral("currentWords")] = cg.words;
        out[QStringLiteral("currentGate")]  =
            gateNoteOf(cg, cg.on ? m_swingDirs.value(m_shots[size_t(fi)].shotId) : QString());
    }

    // ── which way is better, off the corridor's SHAPE ─────────────────────────────────
    //
    // Never "high is bad": a floor is the mirror of a ceiling, and a two-sided corridor is worse
    // both ways. The shape is the norm's own statement of which tail grades, so it is the only
    // honest source for the arrows; the measure's authored highMeans travels with them for the
    // hover, so "worse →" can say what worse IS on this measure.
    QString dirL, dirC, dirR;
    if (c.known) {
        if (c.lowOpen)       { dirL = QStringLiteral("← better"); dirR = QStringLiteral("worse →"); }
        else if (c.highOpen) { dirL = QStringLiteral("← worse");  dirR = QStringLiteral("better →"); }
        else { dirL = QStringLiteral("← worse"); dirC = QStringLiteral("better"); dirR = QStringLiteral("worse →"); }
    }
    out[QStringLiteral("dirLeft")]   = dirL;
    out[QStringLiteral("dirCentre")] = dirC;
    out[QStringLiteral("dirRight")]  = dirR;
    {
        const Measure *mm = (m_packProv && !measureId.isEmpty()) ? m_packProv->pack().measure(measureId) : nullptr;
        out[QStringLiteral("highMeans")] = mm ? mm->highMeans : QString();
    }

    // ── the run: EVERY shot, a not-assessable one included (PpTickRun's rule: never a gap) ─
    const int window = m_opt.resolvingWindow > 0 ? m_opt.resolvingWindow : 5;
    const std::vector<double> medians = trailingMedians(values, window);
    QVariantList run, median;
    int notAssessable = 0;
    for (int i = 0; i < n; ++i) {
        const double v = values[size_t(i)];
        const bool   ok = std::isfinite(v);
        if (!ok) ++notAssessable;
        int cl = 0;
        const double fy = ok ? a.fraction(v, &cl) : 0.0;
        const double fx = n > 0 ? (double(i) + 0.5) / double(n) : 0.5;
        const int shotId = m_shots[size_t(i)].shotId;
        run.append(QVariantMap{
            { QStringLiteral("index"),      i },
            { QStringLiteral("shotId"),     shotId },
            { QStringLiteral("swingDir"),   m_swingDirs.value(shotId) },
            { QStringLiteral("state"),      states[size_t(i)] },
            { QStringLiteral("assessable"), ok },
            { QStringLiteral("value"),      ok ? QVariant(v) : QVariant() },
            { QStringLiteral("fx"),         fx },
            { QStringLiteral("fy"),         fy },
            { QStringLiteral("clipped"),    cl },
            { QStringLiteral("current"),    i == fi },
            // The tick run's own selection, on the same terms (ticksFor): the wide outlined mark
            // means "the shot being reviewed", and only while reviewing or closed.
            { QStringLiteral("selected"),   i == selectedTick },
            { QStringLiteral("valueText"),  ok ? fmtSpread(v, a.decimals, unit) : QStringLiteral("-") },
            { QStringLiteral("readout"),    readoutOf(i) },
            // a missing reading a gate or rule accounts for: its words, and the reading a click opens
            { QStringLiteral("gated"),      !ok && gatesOf[size_t(i)].on },
            { QStringLiteral("gateWords"),  !ok ? gatesOf[size_t(i)].words : QString() },
            { QStringLiteral("gateNote"),   !ok ? gateNoteOf(gatesOf[size_t(i)], m_swingDirs.value(shotId)) : QVariantMap() },
        });
        if (std::isfinite(medians[size_t(i)]))
            median.append(QVariantMap{
                { QStringLiteral("index"), i },
                { QStringLiteral("fx"),    fx },
                { QStringLiteral("fy"),    a.fraction(medians[size_t(i)]) },
                { QStringLiteral("value"), medians[size_t(i)] },
            });
    }
    out[QStringLiteral("run")]           = run;
    out[QStringLiteral("median")]        = median;
    out[QStringLiteral("medianWindow")]  = window;
    out[QStringLiteral("notAssessable")] = notAssessable;
    out[QStringLiteral("pastFault")]     = pastFault;
    out[QStringLiteral("placed")]        = int(placed.size());

    // ── the caption, in words ────────────────────────────────────────────────────────
    //
    // The median FIRST, because it is the number that moved: 46 of 50 outside is the same on a
    // session that improved by nine points as on one that did not. "Outside" is the ledger's own
    // fired count over its own assessable count — the recurrence line's numbers, so the card
    // cannot say 46 in one place and 45 in another. Not-assessable shots are not on the axis and
    // are COUNTED here instead, which is the strip's version of the short outlined tick.
    const double med = spreadQuantile(placed, 0.5);
    QStringList parts, shortParts;
    if (!placed.empty()) {
        parts << QStringLiteral("median %1").arg(fmtSpread(med, a.decimals, unit));
        shortParts << parts.last();
    } else {
        parts << QStringLiteral("no measurable shot yet");
        shortParts << parts.last();
    }
    if (c.known && !placed.empty()) {
        parts << QStringLiteral("%1 past the fault line").arg(pastFault);
        shortParts << parts.last();
    }
    parts << QStringLiteral("%1 of %2 outside").arg(l.fired).arg(l.assessable);
    // JUDGED ON n OF m, AND WHY NOT THE REST (trust_and_gates_design.md §4.4, §7.3): of the shots it could
    // not be measured on, those a gate refused the reading for are counted by the reading and its gate —
    // "not judged on 6: pelvis peak out of sight" — and only the remainder reads as plainly not measured.
    if (notAssessable > 0) {
        QStringList clauses;
        QHash<QString, int> byClause;
        int gated = 0;
        for (int i = 0; i < n; ++i) {
            const ConditionRow *r = rowFor(m_shots[size_t(i)], l.id);
            const RowGate &g = gatesOf[size_t(i)];
            if (!r || !g.on) continue;
            // only shots off this ruler: a row of another measure's verdict is not this measure's gate
            if (std::isfinite(values[size_t(i)])) continue;
            const QString clause = gatedClause(*r, g);
            if (!byClause.contains(clause)) clauses << clause;
            byClause[clause] += 1;
            ++gated;
        }
        if (gated > 0) {
            QStringList bits;
            for (const QString &c : clauses) bits << QStringLiteral("%1 on %2").arg(c).arg(byClause.value(c));
            parts << QStringLiteral("judged on %1 of %2; not judged where %3").arg(l.assessable).arg(n).arg(bits.join(QStringLiteral(", ")));
            shortParts << QStringLiteral("judged on %1 of %2").arg(l.assessable).arg(n);
            out[QStringLiteral("notJudged")] = gated;
        }
        if (notAssessable - gated > 0) {
            parts << QStringLiteral("%1 not measured").arg(notAssessable - gated);
            shortParts << parts.last();
        }
    }
    // THE OFF-SCALE READINGS ARE NAMED HERE, not beside their chevrons. A label inside the plot
    // sat on top of the dots it was describing; the caption has nothing under it.
    for (int side : { +1, -1 }) {
        const QString key = side > 0 ? QStringLiteral("clipHi") : QStringLiteral("clipLo");
        const int k = out.value(key + QStringLiteral("Count")).toInt();
        if (k <= 0) continue;
        const QString far = out.value(key + QStringLiteral("Far")).toString();
        parts << (k == 1 ? QStringLiteral("1 off scale, at %1").arg(far)
                         : QStringLiteral("%1 off scale, to %2").arg(k).arg(far));
        shortParts << parts.last();
    }
    out[QStringLiteral("medianValue")]  = std::isfinite(med) ? QVariant(med) : QVariant();
    out[QStringLiteral("medianText")]   = fmtSpread(med, a.decimals, unit);
    out[QStringLiteral("caption")]      = parts.join(QStringLiteral(" · "));
    // The card's recurrence line already says "46 of 50", so the card quotes the rest.
    out[QStringLiteral("captionShort")] = shortParts.join(QStringLiteral(" · "));
    return out;
}

// ── A composite's evidence, part by part (posture_metrics_design.md §8) ────────────────────
//
// THE COMPOSITE'S NUMBER IS NEVER SHOWN (session diagnostics principle 5). It is a weighted sum of
// z-scores with no golfer-facing unit, and "2.8" next to "loss of posture" is a statement nobody can
// act on. What the golfer is owed instead is what changed, each part in its own unit — "forward bend
// lost 9°, hips 7 cm toward the ball" — and which part carried the finding. Everything below turns
// the ledger's CompositeRowEvidence (schema 9) into that, so the three registers draw one answer.

bool SessionDiagnosticsModel::isCompositeMeasure(const QString &measureId) const
{
    if (measureId.isEmpty() || !m_packProv) return false;
    const Measure *m = m_packProv->pack().measure(measureId);
    return m && m->kind == MeasureKind::Composite;
}

bool SessionDiagnosticsModel::readsComposite(const ConditionLedger &l, const QString &measureId) const
{
    // The pack's word first; then the ROWS' — a row graded as a composite carries its parts whatever the
    // pack says today, and its stored value is still the composite's number.
    if (isCompositeMeasure(measureId)) return true;
    for (const ShotRecord &s : m_shots) {
        const ConditionRow *r = rowFor(s, l.id);
        if (!r) continue;
        if (r->drivingMeasureId == measureId && r->composite.present()) return true;
        for (const MeasureRow &mr : r->readings)
            if (mr.measureId == measureId && mr.composite.present()) return true;
    }
    return false;
}

SessionDiagnosticsModel::PartBand SessionDiagnosticsModel::partBandOf(const QString &measureId,
                                                                      const QString &contextId,
                                                                      double value) const
{
    PartBand out;
    if (!m_norms || measureId.isEmpty() || !std::isfinite(value)) return out;
    Shape shape = Shape::Target;
    QString unit;
    if (m_packProv)
        if (const Measure *m = m_packProv->pack().measure(measureId)) { shape = m->shape; unit = m->unit; }
    const NormResolution res = m_norms->resolve(measureId, contextId);
    if (!res.found()) return out;
    const SpreadCorridor c = spreadCorridorFromNorm(*res.norm, shape, gradePolicyByName(m_policyName));
    if (!c.known) return out;
    // The axis the card would draw this reading on — its decimals are how the app quotes it.
    const SpreadAxis a = spreadAxisFor(c, { value });
    out.decimals = a.decimals;
    const std::vector<SpreadBand> bands = spreadBands(c, a);
    for (const SpreadBand &b : bands)
        if (value >= b.lo && value <= b.hi) { out.grade = b.grade; break; }
    if (out.grade.isEmpty() && !bands.empty())     // past the axis: the outermost band on that side
        out.grade = value < bands.front().lo ? bands.front().grade : bands.back().grade;
    // The Ideal band, for the setup table's "against the ideal band" (§8).
    double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
    for (const SpreadBand &b : bands)
        if (b.grade == QLatin1String("ideal")) { lo = std::min(lo, b.lo); hi = std::max(hi, b.hi); }
    if (std::isfinite(lo) && std::isfinite(hi))
        out.idealText = QStringLiteral("%1 to %2").arg(fmtSpread(lo, a.decimals),
                                                       fmtSpread(hi, a.decimals, unit.size() <= 3 ? unit : unit.section(QLatin1Char(' '), 0, 0)));
    return out;
}

QVariantMap SessionDiagnosticsModel::whatChangedOf(const ConditionRow &r) const
{
    QVariantMap out;
    const CompositeRowEvidence &ev = r.composite;
    if (!ev.present()) return out;

    const Measure *cm = m_packProv ? m_packProv->pack().measure(r.drivingMeasureId) : nullptr;
    const bool hasSpec = cm && cm->kind == MeasureKind::Composite;
    // The mode is the composite's own; a row read under a pack that no longer knows it falls back on
    // the evidence: a coherent part carries a sign, a distance part never does.
    bool coherent = hasSpec ? cm->composite.combine == CompositeCombine::Coherent
                            : std::any_of(ev.parts.begin(), ev.parts.end(), [](const CompositePartRow &p) { return p.sign != 0; });

    // WHERE THE CHANGE ENDS: the instant the parts are read at (they are all read at one, §4.4) —
    // impact, the top, or address for the setup composite.
    auto instantWord = [&](const QString &measureId) -> QString {
        if (!m_packProv || measureId.isEmpty()) return QString();
        const ConditionReading cr = measureReading(m_packProv->pack(), measureId);
        if (!cr.found) return QString();
        switch (Phase(cr.phase)) {
        case Phase::Address: return QStringLiteral("address");
        case Phase::Top:     return QStringLiteral("top");
        case Phase::Impact:  return QStringLiteral("impact");
        default:             return phaseLabel(Phase(cr.phase));
        }
    };
    const auto golferNameOf = [&](const QString &measureId, const QString &fallback) {
        const Measure *m = (m_packProv && !measureId.isEmpty()) ? m_packProv->pack().measure(measureId) : nullptr;
        if (m && !m->golferName.isEmpty()) return m->golferName;
        if (m) return measureDisplayLabel(*m).toLower();
        return fallback;
    };
    const auto golferSignOf = [&](const QString &measureId) {
        const Measure *m = (m_packProv && !measureId.isEmpty()) ? m_packProv->pack().measure(measureId) : nullptr;
        return (m && m->golferSign < 0) ? -1.0 : 1.0;
    };

    // The authored order when the pack still holds the composite, so a part not read keeps its place;
    // else the parts read, then the parts not.
    QStringList order;
    if (hasSpec) for (const CompositePartSpec &ps : cm->composite.parts) order << ps.name;
    for (const CompositePartRow &p : ev.parts)    if (!order.contains(p.name)) order << p.name;
    for (const CompositeUnreadRow &u : ev.unread) if (!order.contains(u.name)) order << u.name;

    // The dominant part: the largest share FOR the finding (a share against it never carries it).
    int dominant = -1;
    for (int i = 0; i < int(ev.parts.size()); ++i) {
        const double s = ev.parts[size_t(i)].share;
        if (std::isfinite(s) && s > 0.0 && (dominant < 0 || s > ev.parts[size_t(dominant)].share)) dominant = i;
    }

    QString endWord;
    QVariantList rows;
    QStringList unreadClauses;
    int partsRead = 0;
    QVariantMap dom;
    for (const QString &name : order) {
        const auto pit = std::find_if(ev.parts.begin(), ev.parts.end(), [&](const CompositePartRow &p) { return p.name == name; });
        QVariantMap row;
        if (pit != ev.parts.end()) {
            const CompositePartRow &p = *pit;
            ++partsRead;
            if (endWord.isEmpty()) endWord = instantWord(p.measureId);
            const QString unit = p.unit.isEmpty() ? measureUnitOf(p.measureId) : p.unit;
            // A SHORT unit rides on every figure ("27°", "7 cm"); a worded one ("% stance width") is said
            // once, after the part's name, and the figures carry its symbol alone — a table column cannot
            // hold "−3 % stance width" three times over.
            const bool shortUnit = unit.size() <= 3;
            const QString figUnit = shortUnit ? unit : unit.section(QLatin1Char(' '), 0, 0);
            const PartBand band = partBandOf(p.measureId, r.contextId, p.value);
            const int dec = band.decimals;
            const QString pname = golferNameOf(p.measureId, p.name);
            // THE CHANGE in the golfer's direction: raw × golferSign, so "forward bend lost 9°" reads as
            // a magnitude in the direction the words name (MeasureRow golferSign, the report's rule).
            const double change = p.value * golferSignOf(p.measureId);
            row[QStringLiteral("name")]       = pname;
            row[QStringLiteral("part")]       = p.name;
            row[QStringLiteral("measureId")]  = p.measureId;
            row[QStringLiteral("read")]       = true;
            row[QStringLiteral("unit")]       = unit;
            row[QStringLiteral("unitNote")]   = shortUnit ? QString() : unit;
            // the two ends only where the measure names them (a change between two instants); a reading
            // at one instant has no address value to quote, and inventing "0 →" would be this panel's
            row[QStringLiteral("startText")]  = std::isfinite(p.atStart) ? fmtSpread(p.atStart, dec, figUnit) : QString();
            row[QStringLiteral("endText")]    = std::isfinite(p.atEnd)   ? fmtSpread(p.atEnd, dec, figUnit)   : QString();
            row[QStringLiteral("changeText")] = coherent ? fmtSpread(change, dec, figUnit) : QString();
            row[QStringLiteral("valueText")]  = fmtSpread(p.value, dec, figUnit);   // the reading itself (distance mode)
            row[QStringLiteral("idealText")]  = band.idealText;
            row[QStringLiteral("grade")]      = band.grade;
            row[QStringLiteral("share")]      = std::isfinite(p.share) ? QVariant(p.share) : QVariant();
            row[QStringLiteral("shareWord")]  = compositeShareWord(p.share);
            const bool isDom = dominant >= 0 && &ev.parts[size_t(dominant)] == &p;
            row[QStringLiteral("dominant")]   = isDom;
            if (isDom) {
                const double fig = coherent ? change : p.value;
                dom = QVariantMap{
                    { QStringLiteral("name"),       pname },
                    { QStringLiteral("measureId"),  p.measureId },
                    { QStringLiteral("unit"),       unit },
                    { QStringLiteral("figure"),     fmtSpread(fig, dec) },
                    { QStringLiteral("changeText"), fmtSpread(fig, dec, unit) },
                    // the card's value slot: the figure in its unit, as every other row's reads ("7 cm") — the
                    // part's name is What changed's to say, and the detail's key figures name it once
                    { QStringLiteral("valueText"),  fmtSpread(fig, dec, unit) },
                };
            }
        } else {
            const auto uit = std::find_if(ev.unread.begin(), ev.unread.end(), [&](const CompositeUnreadRow &u) { return u.name == name; });
            const QString mid = uit != ev.unread.end() ? uit->measureId : QString();
            const QString pname = golferNameOf(mid, name);
            const QString why = compositeUnreadWords(uit != ev.unread.end() ? uit->reason : QString(),
                                                     uit != ev.unread.end() ? uit->status : QString());
            // "-" in every figure slot: not read is not zero (missing-metrics-show-dash); the reason is
            // the trust fold's, said once there
            row[QStringLiteral("name")]       = pname;
            row[QStringLiteral("part")]       = name;
            row[QStringLiteral("measureId")]  = mid;
            row[QStringLiteral("read")]       = false;
            row[QStringLiteral("startText")]  = QStringLiteral("-");
            row[QStringLiteral("endText")]    = QStringLiteral("-");
            row[QStringLiteral("changeText")] = QStringLiteral("-");
            row[QStringLiteral("valueText")]  = QStringLiteral("-");
            row[QStringLiteral("idealText")]  = QString();
            row[QStringLiteral("grade")]      = QString();
            row[QStringLiteral("shareWord")]  = QStringLiteral("-");
            row[QStringLiteral("dominant")]   = false;
            row[QStringLiteral("reason")]     = why;
            unreadClauses << QStringLiteral("%1, %2").arg(pname, why);
        }
        rows.append(row);
    }

    const int total = int(order.size());
    // COVERAGE, SAID ONCE — in the trust fold, never on the table (§4.6, §8).
    QString coverage = partsRead == total
                           ? QStringLiteral("Read from all %1 of its parts").arg(total)
                           : QStringLiteral("Read from %1 of %2 parts").arg(partsRead).arg(total);
    if (!unreadClauses.isEmpty()) coverage += QStringLiteral(": ") + unreadClauses.join(QStringLiteral("; "));
    coverage += QLatin1Char('.');

    out[QStringLiteral("mode")]         = coherent ? QStringLiteral("coherent") : QStringLiteral("distance");
    out[QStringLiteral("startWord")]    = QStringLiteral("address");
    out[QStringLiteral("endWord")]      = endWord.isEmpty() ? QStringLiteral("impact") : endWord;
    out[QStringLiteral("rows")]         = rows;
    out[QStringLiteral("dominant")]     = dom;
    out[QStringLiteral("partsRead")]    = partsRead;
    out[QStringLiteral("partsTotal")]   = total;
    out[QStringLiteral("coverageText")] = coverage;
    out[QStringLiteral("trustLines")]   = QVariantList{ QVariantMap{ { QStringLiteral("kind"), QStringLiteral("coverage") },
                                                                     { QStringLiteral("text"), coverage } } };
    return out;
}

QVariantMap SessionDiagnosticsModel::compositeSpreadFor(const ConditionLedger &l, int fi,
                                                        const QString &measure) const
{
    const QString measureId = measure.isEmpty() ? l.drivingMeasureId : measure;
    const int n = int(m_shots.size());
    QVariantMap out;
    out[QStringLiteral("composite")] = true;

    // The composite's verdicts per shot, as the strip draws them: COUNTS only, no positions. Past the
    // fault line against the corridor the card's strip would have drawn (spreadFor's count), never printed.
    const SpreadCorridor c = spreadCorridorOf(l, fi, nullptr, measureId);
    int placed = 0, pastFault = 0, notAssessable = 0;
    for (int i = 0; i < n; ++i) {
        const ConditionRow *r = rowFor(m_shots[size_t(i)], l.id);
        if (!r || r->state == ShotState::NotAssessable) { ++notAssessable; continue; }
        const std::optional<MeasureRow> mr = rowReadingOf(*r, measureId);
        const double v = mr ? mr->value : r->value;
        if (!std::isfinite(v)) { ++notAssessable; continue; }
        ++placed;
        if (c.known && ((!c.highOpen && v > c.faultHi) || (!c.lowOpen && v < c.faultLo))) ++pastFault;
    }
    out[QStringLiteral("placed")]        = placed;
    out[QStringLiteral("pastFault")]     = pastFault;
    out[QStringLiteral("notAssessable")] = notAssessable;
    out[QStringLiteral("corridorKnown")] = false;

    // THE DOMINANT PART, AS THE CARD'S FIGURES: this shot's largest share for the finding, and the
    // session median of THAT part's reading (the same alternative, so one unit), so the card and the
    // page quote the same reading (§8 — defect 1 fixed by construction for these rows).
    const ConditionRow *here = (fi >= 0 && fi < n) ? rowFor(m_shots[size_t(fi)], l.id) : nullptr;
    QVariantMap dom;
    if (here && here->state != ShotState::NotAssessable) dom = whatChangedOf(*here).value(QStringLiteral("dominant")).toMap();
    if (dom.isEmpty()) {
        // no reading here: the part that dominated most often across the session names the figures
        QHash<QString, int> times;
        QString best;
        for (const ShotRecord &s : m_shots) {
            const ConditionRow *r = rowFor(s, l.id);
            if (!r || r->state == ShotState::NotAssessable) continue;
            const QString id = whatChangedOf(*r).value(QStringLiteral("dominant")).toMap().value(QStringLiteral("measureId")).toString();
            if (id.isEmpty()) continue;
            if (++times[id] > times.value(best)) best = id;
        }
        if (!best.isEmpty())
            dom = QVariantMap{ { QStringLiteral("measureId"), best }, { QStringLiteral("unit"), measureUnitOf(best) } };
    }
    const QString partId = dom.value(QStringLiteral("measureId")).toString();
    const QString unit   = dom.value(QStringLiteral("unit")).toString();
    std::vector<double> vals;
    int decimals = 1;
    QString partName = dom.value(QStringLiteral("name")).toString();
    for (const ShotRecord &s : m_shots) {
        const ConditionRow *r = rowFor(s, l.id);
        if (!r || partId.isEmpty()) continue;
        const QVariantList rows = whatChangedOf(*r).value(QStringLiteral("rows")).toList();
        for (const QVariant &rv : rows) {
            const QVariantMap pr = rv.toMap();
            if (pr.value(QStringLiteral("measureId")).toString() != partId || !pr.value(QStringLiteral("read")).toBool()) continue;
            if (partName.isEmpty()) partName = pr.value(QStringLiteral("name")).toString();
            const auto pit = std::find_if(r->composite.parts.begin(), r->composite.parts.end(),
                                          [&](const CompositePartRow &p) { return p.measureId == partId; });
            if (pit == r->composite.parts.end() || !std::isfinite(pit->value)) continue;
            const bool coherent = pit->sign != 0;
            const Measure *pm = m_packProv ? m_packProv->pack().measure(partId) : nullptr;
            vals.push_back(coherent ? pit->value * ((pm && pm->golferSign < 0) ? -1.0 : 1.0) : pit->value);
            decimals = partBandOf(partId, r->contextId, pit->value).decimals;
        }
    }
    std::sort(vals.begin(), vals.end());
    out[QStringLiteral("measure")]       = partName;
    out[QStringLiteral("unit")]          = unit;
    out[QStringLiteral("decimals")]      = decimals;
    out[QStringLiteral("currentNumber")] = dom.contains(QStringLiteral("figure")) ? dom.value(QStringLiteral("figure")).toString()
                                                                                  : QStringLiteral("-");
    out[QStringLiteral("medianNumber")]  = vals.empty() ? QStringLiteral("-") : fmtSpread(spreadQuantile(vals, 0.5), decimals);
    out[QStringLiteral("medianText")]    = vals.empty() ? QStringLiteral("-") : fmtSpread(spreadQuantile(vals, 0.5), decimals, unit);
    out[QStringLiteral("currentState")]  = here ? shotStateKind(here->state) : QStringLiteral("notAssessable");
    const RowGate cg = (here && here->state == ShotState::NotAssessable) ? rowGate(here) : RowGate();
    out[QStringLiteral("currentGated")]  = cg.on;
    out[QStringLiteral("currentWords")]  = cg.words;
    out[QStringLiteral("currentGate")]   = gateNoteOf(cg, cg.on && fi >= 0 ? m_swingDirs.value(m_shots[size_t(fi)].shotId) : QString());
    // The caption in counts: how often, never how far in the composite's own terms.
    QStringList parts;
    if (!vals.empty() && !partName.isEmpty()) parts << QStringLiteral("%1, median %2").arg(partName, out.value(QStringLiteral("medianText")).toString());
    parts << QStringLiteral("%1 of %2 outside").arg(l.fired).arg(l.assessable);
    if (notAssessable > 0) parts << QStringLiteral("%1 not measured").arg(notAssessable);
    out[QStringLiteral("caption")]      = parts.join(QStringLiteral(" · "));
    out[QStringLiteral("captionShort")] = parts.join(QStringLiteral(" · "));
    return out;
}

QVariantMap SessionDiagnosticsModel::spreadOf(const QString &conditionId) const
{
    const ConditionLedger *l = ledger(conditionId);
    if (!l || l->sessionScope) return QVariantMap();   // no per-swing readings to spread
    const int fi = focusIndex();
    return spreadFor(*l, fi, (m_reviewing || m_closed) ? fi : -1);
}

// ── Across sessions ─────────────────────────────────────────────────────────────────────

std::vector<SessionDiagnosticsModel::HistorySession>
SessionDiagnosticsModel::readHistory(const QString &sessionDir, int *missing)
{
    std::vector<HistorySession> out;
    if (missing) *missing = 0;
    if (sessionDir.isEmpty()) return out;

    // THE SAME GOLFER is the session folder's PARENT — the athlete folder, the one place the fault
    // profile already treats as "this golfer's history" (faultProfilePath()). Siblings only: a
    // session in another athlete's folder is another golfer, whatever its condition ids say.
    const QFileInfo self(QDir::cleanPath(sessionDir));
    const QString selfCanon = self.canonicalFilePath().isEmpty() ? self.absoluteFilePath()
                                                                 : self.canonicalFilePath();
    const QDir athlete = self.absoluteDir();
    const QStringList dirs = athlete.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : dirs) {
        const QString dir = athlete.filePath(name);
        const QFileInfo fi(dir);
        const QString canon = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath()
                                                               : fi.canonicalFilePath();
        if (canon == selfCanon) continue;           // this session comes from memory, not disk

        QFile f(QDir(dir).filePath(kDiagnosticsFile));
        if (!f.open(QIODevice::ReadOnly)) {
            // A session with swings and no ledger yet — never opened with the panel. Counted, so
            // the caption can say so; reducing it here would be running detection on somebody
            // else's behalf from a read-only view.
            if (missing && !QDir(dir).entryList(QStringList{ QStringLiteral("swing_*") },
                                                QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
                ++*missing;
            continue;
        }
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        // READ THROUGH THE LEDGER'S OWN READER, so a schema the panel can load is a schema this
        // can load, and nothing here re-implements the row format.
        const std::vector<ShotRecord> shots = fromJson(root.value(QStringLiteral("ledger")).toObject());
        if (shots.empty()) continue;

        HistorySession hs;
        hs.sessionDir = dir;
        hs.firstMs = std::numeric_limits<qint64>::max();
        for (const ShotRecord &s : shots) {
            if (s.timestampMs > 0) hs.firstMs = std::min(hs.firstMs, s.timestampMs);
            for (const ConditionRow &r : s.rows) {
                if (r.state == ShotState::NotAssessable) continue;
                // One reading per MEASURE per shot: two signals on one measure (its two tails)
                // read the same number, and counting it twice would weigh that shot double.
                QSet<QString> seen;
                for (const MeasureRow &m : rowReadings(r)) {
                    if (!std::isfinite(m.value) || seen.contains(m.measureId)) continue;
                    seen.insert(m.measureId);
                    hs.byCondition[r.conditionId].push_back(HistoryReading{ m.measureId, m.value });
                }
            }
        }
        if (hs.firstMs == std::numeric_limits<qint64>::max()) hs.firstMs = 0;
        out.push_back(std::move(hs));
    }
    return out;
}

void SessionDiagnosticsModel::scanHistory()
{
    const int gen = ++m_historyGen;
    m_history.clear();
    m_historyMissing = 0;
    m_historyLoading = !m_sessionDir.isEmpty();
    if (m_sessionDir.isEmpty()) return;

    const QString dir = m_sessionDir;
    if (m_synchronous) {
        int missing = 0;
        const std::vector<HistorySession> got = readHistory(dir, &missing);
        applyHistory(gen, got, missing);
        return;
    }
    m_historyPool.start([this, gen, dir]() {
        int missing = 0;
        const std::vector<HistorySession> got = readHistory(dir, &missing);
        QMetaObject::invokeMethod(this, [this, gen, got, missing]() { applyHistory(gen, got, missing); },
                                  Qt::QueuedConnection);
    });
}

void SessionDiagnosticsModel::applyHistory(int generation, const std::vector<HistorySession> &sessions,
                                           int missing)
{
    if (generation != m_historyGen) return;     // a scan for a session no longer loaded
    m_history        = sessions;
    m_historyMissing = missing;
    m_historyLoading = false;
    // Only the detail draws history, so only the detail is republished — the zones did not
    // change and a surfaceChanged here would re-run every card for nothing.
    if (!m_detailConditionId.isEmpty()) {
        buildDetail();
        emit detailChanged();
    }
}

QVariantMap SessionDiagnosticsModel::historyFor(const ConditionLedger &l, int fi,
                                                const QString &measure) const
{
    QVariantMap out;
    const QString measureId = measure.isEmpty() ? l.drivingMeasureId : measure;
    // A composite has no across-sessions columns: each would be its number, per session (§8, §12).
    if (readsComposite(l, measureId)) return out;
    const QString unit      = measureUnitOf(measureId);

    struct Col { QString dir, label; qint64 firstMs = 0; bool current = false; std::vector<double> v; };
    std::vector<Col> cols;
    auto onRuler = [&](const QString &m) { return measureId.isEmpty() || m == measureId; };

    for (const HistorySession &hs : m_history) {
        Col c;
        c.dir = hs.sessionDir;
        c.firstMs = hs.firstMs;
        const auto it = hs.byCondition.constFind(l.id);
        if (it != hs.byCondition.constEnd())
            for (const HistoryReading &r : *it)
                if (onRuler(r.measureId)) c.v.push_back(r.value);
        cols.push_back(std::move(c));
    }
    {
        Col c;
        c.dir = m_sessionDir;
        c.current = true;
        c.firstMs = std::numeric_limits<qint64>::max();
        for (const ShotRecord &s : m_shots) {
            if (s.timestampMs > 0) c.firstMs = std::min(c.firstMs, s.timestampMs);
            const ConditionRow *r = rowFor(s, l.id);
            if (!r || r->state == ShotState::NotAssessable) continue;
            if (measureId.isEmpty()) {
                if (std::isfinite(r->value)) c.v.push_back(r->value);
            } else if (const std::optional<MeasureRow> m = rowReadingOf(*r, measureId)) {
                if (std::isfinite(m->value)) c.v.push_back(m->value);
            }
        }
        if (c.firstMs == std::numeric_limits<qint64>::max()) c.firstMs = QDateTime::currentMSecsSinceEpoch();
        cols.push_back(std::move(c));
    }

    // OLDEST FIRST, by when the session's first swing was struck; the folder name breaks a tie
    // (and orders sessions whose ledgers carry no timestamps at all).
    std::sort(cols.begin(), cols.end(), [](const Col &a, const Col &b) {
        if (a.firstMs != b.firstMs) return a.firstMs < b.firstMs;
        return a.dir < b.dir;
    });

    // The label is the session's date: off the folder name the library writes (yyyy-MM-dd_…), and
    // off the first swing only when the folder does not carry one. Two sessions on one day are
    // told apart by the folder's trailing number.
    for (Col &c : cols) {
        const QString name = QFileInfo(QDir::cleanPath(c.dir)).fileName();
        QDate d = QDate::fromString(name.left(10), QStringLiteral("yyyy-MM-dd"));
        if (!d.isValid() && c.firstMs > 0) d = QDateTime::fromMSecsSinceEpoch(c.firstMs).date();
        c.label = d.isValid() ? d.toString(QStringLiteral("d MMM")) : name;
    }
    for (size_t i = 0; i < cols.size(); ++i) {
        int same = 0;
        for (const Col &o : cols) same += (o.label == cols[i].label);
        if (same > 1) {
            const QString name = QFileInfo(QDir::cleanPath(cols[i].dir)).fileName();
            cols[i].label += QStringLiteral(" #") + name.section(QLatin1Char('_'), -1);
        }
    }

    // At most kHistoryMaxColumns, the newest — but never a window that leaves THIS session out,
    // which is the one column the reader came for. What is cut is counted on each side.
    const int total = int(cols.size());
    int curIdx = 0;
    for (int i = 0; i < total; ++i) if (cols[size_t(i)].current) curIdx = i;
    int start = std::max(0, total - kHistoryMaxColumns);
    if (curIdx < start) start = curIdx;
    const int end = std::min(total, start + kHistoryMaxColumns);

    std::vector<double> pooled;
    for (int i = start; i < end; ++i)
        pooled.insert(pooled.end(), cols[size_t(i)].v.begin(), cols[size_t(i)].v.end());

    const SpreadCorridor c = spreadCorridorOf(l, fi, nullptr, measureId);
    const SpreadAxis     a = spreadAxisFor(c, pooled);
    publishCorridor(out, c, a, unit);
    out[QStringLiteral("measure")] = measureLabelOf(measureId);
    out[QStringLiteral("unit")]    = unit;

    QVariantList columns;
    QStringList medians;
    for (int i = start; i < end; ++i) {
        const Col &col = cols[size_t(i)];
        std::vector<double> fr;
        std::vector<int> cl;
        for (double v : col.v) { int k = 0; fr.push_back(a.fraction(v, &k)); cl.push_back(k); }
        const std::vector<int> st = spreadStacks(fr, kSpreadBins);
        QVariantList vs;
        int stackMax = 0;
        for (size_t k = 0; k < fr.size(); ++k) {
            stackMax = std::max(stackMax, st[k]);
            vs.append(QVariantMap{ { QStringLiteral("f"),       fr[k] },
                                   { QStringLiteral("clipped"), cl[k] },
                                   { QStringLiteral("stack"),   st[k] },
                                   { QStringLiteral("value"),   col.v[k] } });
        }
        const double q1 = spreadQuantile(col.v, 0.25);
        const double md = spreadQuantile(col.v, 0.5);
        const double q3 = spreadQuantile(col.v, 0.75);
        const bool any = !col.v.empty();
        if (any) medians << fmtSpread(md, a.decimals);
        columns.append(QVariantMap{
            { QStringLiteral("label"),      col.label },
            { QStringLiteral("sessionDir"), col.dir },
            { QStringLiteral("current"),    col.current },
            { QStringLiteral("n"),          int(col.v.size()) },
            { QStringLiteral("values"),     vs },
            { QStringLiteral("stackMax"),   stackMax },
            { QStringLiteral("median"),     any ? QVariant(md) : QVariant() },
            { QStringLiteral("q1"),         any ? QVariant(q1) : QVariant() },
            { QStringLiteral("q3"),         any ? QVariant(q3) : QVariant() },
            { QStringLiteral("fMedian"),    any ? a.fraction(md) : -1.0 },
            { QStringLiteral("fQ1"),        any ? a.fraction(q1) : -1.0 },
            { QStringLiteral("fQ3"),        any ? a.fraction(q3) : -1.0 },
            { QStringLiteral("box"),        boxOf(col.v, {}, a, unit) },
            { QStringLiteral("medianText"), fmtSpread(any ? md : std::numeric_limits<double>::quiet_NaN(),
                                                      a.decimals, unit) },
        });
    }
    out[QStringLiteral("columns")]       = columns;
    out[QStringLiteral("hiddenEarlier")] = start;
    out[QStringLiteral("hiddenLater")]   = total - end;
    out[QStringLiteral("loading")]       = m_historyLoading;
    out[QStringLiteral("missing")]       = m_historyMissing;

    QStringList cap;
    if (m_historyLoading)
        cap << QStringLiteral("reading this golfer's other sessions…");
    else if (total <= 1)
        cap << QStringLiteral("no other session of this golfer has a ledger yet");
    else if (!medians.isEmpty())
        cap << QStringLiteral("median %1").arg(fmtSpreadRun(medians, unit));
    if (start > 0)
        cap << QStringLiteral("%1 earlier %2 not shown").arg(start)
                   .arg(start == 1 ? QStringLiteral("session") : QStringLiteral("sessions"));
    if (total - end > 0)
        cap << QStringLiteral("%1 later not shown").arg(total - end);
    if (m_historyMissing > 0)
        cap << QStringLiteral("%1 %2 not yet read by the panel").arg(m_historyMissing)
                   .arg(m_historyMissing == 1 ? QStringLiteral("session") : QStringLiteral("sessions"));
    out[QStringLiteral("caption")] = cap.join(QStringLiteral(" · "));
    return out;
}

QVariantMap SessionDiagnosticsModel::historyOf(const QString &conditionId) const
{
    const ConditionLedger *l = ledger(conditionId);
    // A session-scope condition has no per-swing rows for a past session's ledger to re-derive
    // from (conditionLedgers() never synthesises it), so there is no history to draw yet.
    return (l && !l->sessionScope) ? historyFor(*l, focusIndex()) : QVariantMap();
}

// ── Zone 1: the header ──────────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::buildHeader()
{
    const int n        = int(m_shots.size());
    const int patterns = patternCount();
    const int fi       = focusIndex();
    const Stage st     = effectiveStage();

    QVariantMap h;
    h[QStringLiteral("stage")]        = stageName(st);
    h[QStringLiteral("stageLabel")]   = stageName(st).toUpper();
    h[QStringLiteral("shotCount")]    = n;
    h[QStringLiteral("patternCount")] = patterns;
    h[QStringLiteral("reviewing")]    = m_reviewing;
    h[QStringLiteral("closed")]       = m_closed;

    // THE RECORDED STAGE, BESIDE THE DISPLAYED ONE. `stage` above is what the panel DRAWS —
    // frozen at Closing while a session is closed or under review. That freeze is a fact about
    // the tense and says nothing about what the session achieved, and a surface that read it
    // as "Established, therefore draw the rail" would draw a chain rail over a session that
    // never got past Forming. So the ratcheted stage is published in its own right, and with
    // it the one bit the composition actually turns on.
    h[QStringLiteral("recordedStage")]      = stageName(m_stage);
    h[QStringLiteral("recordedStageLabel")] = stageName(m_stage).toUpper();
    h[QStringLiteral("reachedEstablished")] = m_reachedEstablished;

    h[QStringLiteral("shotLabel")] =
        n == 0 ? QStringLiteral("no shots yet")
               : (m_reviewing && fi >= 0
                      ? QStringLiteral("shot %1 of %2").arg(fi + 1).arg(n)
                      : QStringLiteral("shot %1").arg(n));

    // A COUNT over all shots, and it says so — the review header's whole job is stating the
    // tense, because the panel deliberately does NOT rewind to what it knew at the selected
    // shot (brief §6).
    h[QStringLiteral("countLine")] =
        QStringLiteral("%1 %2 · counted over all %3 %4")
            .arg(patterns)
            .arg(patterns == 1 ? QStringLiteral("pattern") : QStringLiteral("patterns"))
            .arg(n)
            .arg(n == 1 ? QStringLiteral("shot") : QStringLiteral("shots"));

    h[QStringLiteral("reviewBadge")] =
        m_reviewing && fi >= 0 ? QStringLiteral("REVIEWING · shot %1 of %2").arg(fi + 1).arg(n)
                               : QString();
    h[QStringLiteral("reviewNote")] =
        m_reviewing ? QStringLiteral("final session state · this shot read inside the finished ledger")
                    : QString();
    h[QStringLiteral("cadenceNote")] = m_quiet ? QStringLiteral("BANDWIDTH · QUIET") : QString();

    // THE PANEL'S TENSE, IN WORDS, ALONG THE BOTTOM (13a's footer). The devices above it —
    // the picked shot's mark, the session counts under every node — are only unambiguous to a reader
    // who already knows the panel does not rewind. This sentence is what tells them, and it
    // is the reason the deliberate non-rewind is a design decision rather than a surprise.
    // The closing words (#17) name what the counts cover and which shot is this one without saying how
    // the panel draws it (a shaded column, a ring), and why a count is not a percentage. Two forms: a
    // surface that names no shot (the report appendix with no reference swing) takes the one without it.
    const QString covers = n == 1 ? QStringLiteral("Every count covers the one shot")
                                  : QStringLiteral("Every count covers all %1 shots").arg(n);
    const QString notRate = QStringLiteral("At this number of shots a percentage would look more exact "
                                           "than it is, so each pattern says how many shots it came back on.");
    const QString shotClause = QStringLiteral(", and this shot is highlighted wherever it appears. ");
    h[QStringLiteral("reviewFootLine")] =
        m_reviewing ? QStringLiteral("The panel stays at the finished session: ") + covers.at(0).toLower()
                          + covers.mid(1) + (fi >= 0 ? shotClause : QStringLiteral(". ")) + notRate
                    : QString();

    // The Cold body line. One sentence, and it is the em-dash philosophy: say the n is too
    // small, do not fill the space with something that looks like a finding.
    h[QStringLiteral("coldLine")] =
        st == Stage::Cold ? QStringLiteral("Too few swings to call a pattern.") : QString();
    // ⚠ THE FORMING LINE IS GONE, and its absence is the point.
    //
    // It read "No chain is drawn: the model authors no edge between these patterns", and it was
    // true while the chain rail was the Established body: a flat card row THERE meant the model
    // had found no edge, and without the sentence a reader would blame the panel. The rail is no
    // longer a body — the chain is one tap behind a card — so a flat card row is now simply what
    // the panel looks like. The sentence would be stating a fact about the LAYOUT in the voice
    // the panel uses for facts about the MODEL, which is the one thing the header must never do.
    //
    // Nothing is lost by dropping it: the UNCHAINED PATTERN row one region below already names
    // each pattern the model authors no peer edge for, per pattern, in words, and that row is
    // where the claim belongs — it is a statement about conditions, not about a composition.
    h[QStringLiteral("formingLine")] = QString();
    h[QStringLiteral("closingLine")] =
        st == Stage::Closing ? covers + (fi >= 0 ? shotClause : QStringLiteral(". ")) + notRate : QString();
    h[QStringLiteral("closingLineNoShot")] =
        st == Stage::Closing ? covers + QStringLiteral(". ") + notRate : QString();

    m_headerInfo = h;
}

// ── Zone 1: the this-shot strip, and the ONE place cadence is read ──────────────────────

void SessionDiagnosticsModel::buildThisShot()
{
    m_thisShot.clear();
    m_afterShotDelta = QVariantMap();
    m_quiet = false;

    const int fi = focusIndex();
    if (fi < 0) return;

    const ShotRecord &shot = m_shots[size_t(fi)];

    int fired = 0, clean = 0, na = 0, firedPatterns = 0;
    int naGated = 0;                         // of `na`, those a gate or rule accounts for
    QStringList gatedOrder;
    QHash<QString, int> gatedWords;
    QVariantList firedList, newPatterns;
    for (const ConditionRow &r : shot.rows) {
        const ConditionLedger *l = ledger(r.conditionId);
        switch (r.state) {
        case ShotState::Fired: {
            ++fired;
            const bool pattern = l && l->tier == Tier::Pattern;
            if (pattern) ++firedPatterns;
            QVariantMap chip{
                { QStringLiteral("id"),    r.conditionId },
                { QStringLiteral("name"),  conditionName(r.conditionId) },
                { QStringLiteral("kind"),  QStringLiteral("fired") },
                { QStringLiteral("tier"),  l ? tierTag(l->tier) : QStringLiteral("watching") },
                { QStringLiteral("focus"), r.conditionId == m_focusConditionId },
            };
            addStrength(chip, &r);
            firedList.append(chip);
            m_thisShot.append(chip);
            if (l && l->freshThisShot) {
                newPatterns.append(QVariantMap{ { QStringLiteral("id"), r.conditionId },
                                                { QStringLiteral("name"), conditionName(r.conditionId) } });
            }
            break;
        }
        case ShotState::Clean:         ++clean; break;
        case ShotState::NotAssessable: {
            ++na;
            const RowGate g = rowGate(&r);
            if (g.on) {
                ++naGated;
                if (!gatedWords.contains(g.words)) gatedOrder << g.words;
                gatedWords[g.words] += 1;
            }
            break;
        }
        }
    }
    const QString naNote = notAssessableNote(na, naGated, topOf(gatedOrder, gatedWords));

    // An explicit CLEAN chip, never an empty strip. "Nothing fired" and "the panel has not
    // caught up" look identical on an empty row, and only one of them is good news.
    if (fired == 0 && clean > 0) {
        m_thisShot.append(QVariantMap{ { QStringLiteral("kind"), QStringLiteral("clean") },
                                       { QStringLiteral("name"), QStringLiteral("clean on every measurable condition") } });
    }
    if (na > 0) {
        // those a gate or rule kept from being judged are said by its words, apart from the plain gaps
        m_thisShot.append(QVariantMap{
            { QStringLiteral("kind"), QStringLiteral("notAssessable") },
            { QStringLiteral("name"), QStringLiteral("%1 not assessable on this capture").arg(na)
                                          + (naGated > 0 ? naNote.mid(naNote.indexOf(QStringLiteral(" · "))) : QString()) },
            { QStringLiteral("notJudgedCount"), naGated },
        });
    }

    // ── The clean-streak test, defined here because it has to be defined somewhere ──
    //
    // A CLEAN STREAK is a run of consecutive shots on which NOTHING fired; it BREAKS on the
    // first shot that fires anything after at least one such shot. Not "a pattern's own run
    // of clean shots ended" — that fires on every ordinary shot in a session with six
    // patterns and would make bandwidth mode indistinguishable from every-shot. The streak
    // is a property of the SHOT, which is what the golfer between balls is actually asking
    // about: "was that one different?".
    bool streakBroken = false;
    if (fired > 0 && fi > 0) {
        const ShotRecord &prev = m_shots[size_t(fi - 1)];
        bool prevFiredAny = false;
        for (const ConditionRow &r : prev.rows)
            if (r.state == ShotState::Fired) { prevFiredAny = true; break; }
        streakBroken = !prevFiredAny;
    }

    // THE ONE PLACE m_cadence IS READ. Bandwidth surfaces the after-shot moment only when a
    // pattern-tier condition fired on this shot or a clean streak broke; every-shot always
    // surfaces. Post-session there is no gating at all (brief §6) — a finished session is
    // read, not fed back.
    const bool everyShot = m_cadence == QLatin1String("everyShot");
    const bool surfaced  = m_reviewing || m_closed || everyShot
                        || firedPatterns > 0 || streakBroken;
    m_quiet = !surfaced;

    QVariantMap d;
    d[QStringLiteral("shotId")]              = shot.shotId;
    d[QStringLiteral("shotIndex")]           = fi;
    d[QStringLiteral("firedCount")]          = fired;
    d[QStringLiteral("cleanCount")]          = clean;
    d[QStringLiteral("notAssessableCount")]  = na;
    d[QStringLiteral("firedPatternCount")]   = firedPatterns;
    d[QStringLiteral("fired")]               = firedList;
    d[QStringLiteral("newPatterns")]         = newPatterns;
    d[QStringLiteral("streakBroken")]        = streakBroken;
    d[QStringLiteral("surfaced")]            = surfaced;
    // ONE POPULATION in the headline, per brief §6: how many of the conditions this capture
    // could speak to fired, and how many of those the session already calls patterns.
    d[QStringLiteral("headline")] =
        QStringLiteral("%1 of %2 conditions fired on this swing · %3 of them %4")
            .arg(fired).arg(fired + clean).arg(firedPatterns)
            .arg(firedPatterns == 1 ? QStringLiteral("pattern") : QStringLiteral("patterns"));
    d[QStringLiteral("notJudgedCount")]      = naGated;
    d[QStringLiteral("note")]                = naNote;

    // Under a focus contract the focused node's marker is the headline of the moment (§B3).
    if (!m_focusConditionId.isEmpty()) {
        const ConditionRow *r = rowFor(shot, m_focusConditionId);
        const QString state = r ? shotStateKind(r->state) : QStringLiteral("notAssessable");
        d[QStringLiteral("focusId")]    = m_focusConditionId;
        d[QStringLiteral("focusState")] = state;
        d[QStringLiteral("focusHeadline")] =
            QStringLiteral("%1 — %2").arg(conditionName(m_focusConditionId),
                                          state == QLatin1String("fired")   ? QStringLiteral("fired here")
                                        : state == QLatin1String("clean")   ? QStringLiteral("clean here")
                                                                            : QStringLiteral("not measured here"));
        // a focused condition a gate or rule kept from being judged says its words instead
        const RowGate fg = rowGate(r);
        if (fg.on) {
            d[QStringLiteral("focusHeadline")] = QStringLiteral("%1 — %2 here").arg(conditionName(m_focusConditionId), fg.words);
            d[QStringLiteral("focusGate")]     = gateNoteOf(fg, m_swingDirs.value(shot.shotId));
        }
    }

    m_afterShotDelta = d;
}

// ── Zone 2: pattern cards and the watching row ──────────────────────────────────────────

void SessionDiagnosticsModel::buildCards()
{
    m_cards.clear();
    m_watching.clear();
    m_coverageLine.clear();

    const int fi = focusIndex();
    const int selectedTick = (m_reviewing || m_closed) ? fi : -1;

    // Display order is hystereticOrder()'s, not the ledger's first-seen order: which
    // conditions exist and which one goes first churn on completely different schedules.
    QStringList ordered = m_displayOrder;
    for (const ConditionLedger &l : m_ledgers)
        if (l.tier == Tier::Pattern && !ordered.contains(l.id)) ordered.push_back(l.id);

    // ── the two filters: does it reach the ball, and is it a root here ──────────────
    //
    // IMPACT IS NOT SCORED, IT IS ASKED IN TWO BINARY QUESTIONS, and that is the whole design.
    // The three quantities a ranked "impact" score would have to be built from are each unfit
    // for it: Prominence is an unseated editorial prior about EVERYONE (its own header says so),
    // a descendant count measures how much has been WRITTEN about a condition rather than how
    // much it matters — reverse_spine_p4 authors zero effects, which is a gap and not a mild
    // fault — and the pack itself calls ranking by graph topology a failure mode. A HIGH/MEDIUM/
    // LOW badge over those inputs would be a number nobody could defend when asked why one fault
    // outranked another, which is the one thing this panel may never publish.
    //
    // Two facts it CAN state plainly:
    //
    //   reachesBall — the pack authors a causal path from this condition to a ball-flight
    //                 outcome. Structural, and deliberately so: an edge is an independent claim
    //                 somebody wrote down. The WORDING must stay "no authored path to the ball"
    //                 rather than "low impact", because a zero can be an authoring gap.
    //   rootHere    — nothing among THIS session's own patterns is authored as causing it. The
    //                 coach-useful split: roots are where work starts, symptoms follow them.
    //
    // Neither is evidence about this golfer, and neither pretends to be. The measure that WOULD
    // be — an authored edge into an outcome graded Conditionally-dependent or Moved-together over
    // this session's own shots — needs outcome data the capture does not always carry, and is
    // gradeLinks()' job on the day it does.
    m_reachesBall.clear();
    m_rootHere.clear();
    if (m_packProv) {
        const CharacteristicPack &pack = m_packProv->pack();

        QSet<QString> patternSet;
        for (const ConditionLedger &l : m_ledgers)
            if (l.tier == Tier::Pattern) patternSet.insert(l.id);

        for (const QString &id : std::as_const(patternSet)) {
            bool reaches = false;
            const QSet<QString> below = causalClosure(pack, id, /*downstream*/ true);
            for (const QString &d : below) {
                const Condition *c = pack.condition(d);
                if (c && c->kind == ConditionKind::Outcome) { reaches = true; break; }
            }
            m_reachesBall.insert(id, reaches);

            bool caused = false;
            for (const Edge &e : pack.edges) {
                if (e.type != EdgeType::Causes || e.to != id) continue;
                if (patternSet.contains(e.from)) { caused = true; break; }
            }
            m_rootHere.insert(id, !caused);
        }
    }

    // ── the rank badge (#1, #2, #3=, #3=, #5) ───────────────────────────────────────
    //
    // COMPETITION RANKING off the SCORES, not the positions — competitionRanks() owns the rule
    // and the reasoning (diagnostic_ledger.h). Ties are rare now, which is itself the point:
    // they were everywhere while the Wilson bound was the key, and the whole reason it changed.
    QHash<QString, double> scoreOf;
    for (const RankedCondition &r : rankScores()) scoreOf.insert(r.id, r.score);

    m_rankText.clear();
    {
        QStringList patterns;
        for (const QString &id : std::as_const(ordered)) {
            const ConditionLedger *l = ledger(id);
            if (l && l->tier == Tier::Pattern) patterns << id;
        }
        std::vector<double> scores;
        scores.reserve(size_t(patterns.size()));
        for (const QString &id : std::as_const(patterns)) scores.push_back(scoreOf.value(id, 0.0));

        const std::vector<QString> badges = competitionRanks(scores);
        for (int i = 0; i < patterns.size() && i < int(badges.size()); ++i)
            m_rankText.insert(patterns.at(i), badges[size_t(i)]);
    }

    for (const QString &id : std::as_const(ordered)) {
        const ConditionLedger *l = ledger(id);
        if (!l || l->tier != Tier::Pattern) continue;
        m_cards.append(cardMap(*l, fi, selectedTick));
    }

    // The collapsed Watching row: seen, not yet evidence, never headlines.
    for (const ConditionLedger &l : m_ledgers) {
        if (l.tier != Tier::Watching) continue;
        m_watching.append(QVariantMap{
            { QStringLiteral("id"),         l.id },
            { QStringLiteral("name"),       conditionName(l.id) },
            { QStringLiteral("recurrence"), l.recurrence },
        });
    }

    // The coverage line — the quietest and most load-bearing sentence on the panel.
    if (m_coverage.total > 0) {
        m_coverageLine = QStringLiteral("%1 of %2 characteristics measurable with current capture")
                             .arg(m_coverage.measurable).arg(m_coverage.total);
        // Named only when it actually contributed. "Launch monitor" on a camera-only session
        // would be the panel claiming a device that is not there.
        if (!m_lmShots.isEmpty())
            m_coverageLine += QStringLiteral(" · launch monitor contributing on %1 of %2 shots")
                                  .arg(m_lmShots.size()).arg(int(m_shots.size()));
    }

    // NOT JUDGED, AND WHY (trust_and_gates_design.md §4.4, §7.3): each condition a gate kept from
    // being judged on some shot, with its count and the reading the gate refused — "Sequence order:
    // judged on 0 of 6; pelvis peak out of sight on 6". The coverage line counts what the capture can
    // measure; this says what a gate refused on the swings that were captured.
    m_notJudged.clear();
    m_notJudgedLine.clear();
    {
        // grouped by the reading the gate refused, as This shot groups them: "pelvis peak out of sight
        // on 6 of 6 shots: Out-of-order sequence, Hips don't brake … judged on none"
        struct Group { QString clause, subject; int shots = 0; QStringList names; QList<int> judged; };
        QList<Group> groups;
        const int n = int(m_shots.size());
        for (const ConditionLedger &l : m_ledgers) {
            if (l.sessionScope) continue;
            QHash<QString, int> byClause;
            QString clause, subject;
            for (const ShotRecord &s : m_shots) {
                const ConditionRow *r = rowFor(s, l.id);
                // a gate RECORD only: an engine rule is not "a gate refused the reading" (rowGate)
                const RowGate g = rowGate(r);
                if (!g.on || g.rule) continue;
                const int k = ++byClause[r->notAssessableReason];
                if (clause.isEmpty() || k > byClause.value(clause)) { clause = r->notAssessableReason; subject = r->notJudgedSubject; }
            }
            if (clause.isEmpty()) continue;
            int gi = -1;
            for (int i = 0; i < groups.size(); ++i) if (groups[i].clause == clause) gi = i;
            if (gi < 0) { groups.append(Group{ clause, subject, 0, {}, {} }); gi = int(groups.size()) - 1; }
            groups[gi].shots = std::max(groups[gi].shots, byClause.value(clause));
            groups[gi].names << conditionName(l.id);
            groups[gi].judged << l.assessable;
            m_notJudged.append(QVariantMap{ { QStringLiteral("id"), l.id },
                                            { QStringLiteral("name"), conditionName(l.id) },
                                            { QStringLiteral("judged"), l.assessable },
                                            { QStringLiteral("of"), n },
                                            { QStringLiteral("subject"), subject },
                                            { QStringLiteral("clause"), clause } });
        }
        QStringList parts;
        for (const Group &g : std::as_const(groups)) {
            // the count each was judged on, unless the gate refused the reading on every shot (then
            // every one of them was judged on none, and the clause already says so)
            QStringList named;
            for (int i = 0; i < g.names.size(); ++i)
                named << (g.shots == n && g.judged[i] == 0 ? g.names[i]
                          : QStringLiteral("%1 (judged on %2)").arg(g.names[i])
                                .arg(g.judged[i] == 0 ? QStringLiteral("none") : QString::number(g.judged[i])));
            QString part = QStringLiteral("%1 on %2 of %3 shots, so not judged there: %4")
                               .arg(g.clause).arg(g.shots).arg(n).arg(named.join(QStringLiteral(", ")));
            if (!part.isEmpty()) part[0] = part[0].toUpper();
            parts << part;
        }
        if (!parts.isEmpty())
            m_notJudgedLine = QStringLiteral("A gate refused the reading behind some findings. ") + parts.join(QStringLiteral(". "));
    }
}

// ONE PATTERN CARD. Every string on it, off the ledger and the pack and nothing else — which is
// what lets the condition detail put the same card at the head of its own page without the two
// surfaces being able to word a recurrence differently.
QVariantMap SessionDiagnosticsModel::cardMap(const ConditionLedger &l, int fi, int selectedTick) const
{
    const QString id   = l.id;
    const QString unit = measureUnitOf(l.drivingMeasureId);
    // A SESSION-scope card (§10.8 E2): the session's count and spread, no tick run, no strength
    // meter, no this-shot state — there is no swing whose verdict it could show (NR-04, NR-11).
    if (l.sessionScope) {
        QVariantMap c;
        c[QStringLiteral("id")]                 = id;
        c[QStringLiteral("name")]               = conditionName(id);
        c[QStringLiteral("consequence")]        = consequenceOf(id);
        c[QStringLiteral("tier")]               = tierTag(l.tier);
        c[QStringLiteral("measureId")]          = l.drivingMeasureId;
        c[QStringLiteral("measure")]            = measureLabelOf(l.drivingMeasureId);
        c[QStringLiteral("phase")]              = QString();
        c[QStringLiteral("recurrence")]         = l.recurrence;
        c[QStringLiteral("fired")]              = l.fired;
        c[QStringLiteral("assessable")]         = l.assessable;
        c[QStringLiteral("fresh")]              = false;
        c[QStringLiteral("resolving")]          = false;
        c[QStringLiteral("focused")]            = id == m_focusConditionId;
        c[QStringLiteral("ticks")]              = QVariantList();
        c[QStringLiteral("spread")]             = QVariantMap();
        c[QStringLiteral("sessionScope")]       = true;
        c[QStringLiteral("consistency")]        = m_sequenceConsistency;
        c[QStringLiteral("directionClaimed")]   = true;
        c[QStringLiteral("directionAgreement")] = 1.0;
        c[QStringLiteral("directionText")]      = QString();
        c[QStringLiteral("trendKnown")]         = false;
        c[QStringLiteral("trend")]              = QStringLiteral("stable");
        c[QStringLiteral("trendArrow")]         = QString();
        c[QStringLiteral("trendText")]          = m_sequenceConsistency.value(QStringLiteral("whose")).toString();
        c[QStringLiteral("recencyText")]        = m_sequenceConsistency.value(QStringLiteral("spreadLine")).toString();
        c[QStringLiteral("thisShot")]           = QStringLiteral("session");
        c[QStringLiteral("statePill")]          = QStringLiteral("SESSION");
        c[QStringLiteral("valueText")]          = l.recurrence;
        c[QStringLiteral("corridorText")]       = m_sequenceConsistency.value(QStringLiteral("spreadLine")).toString();
        c[QStringLiteral("sessionExcess")]      = l.firingExcess;
        c[QStringLiteral("rankText")]           = m_rankText.value(id);
        c[QStringLiteral("reachesBall")]        = m_reachesBall.value(id, false);
        c[QStringLiteral("rootHere")]           = m_rootHere.value(id, false);
        c[QStringLiteral("evidence")]           = consequenceOf(id);
        return c;
    }
    {
        QVariantMap c;
        c[QStringLiteral("id")]          = id;
        c[QStringLiteral("name")]        = conditionName(id);
        c[QStringLiteral("consequence")] = consequenceOf(id);
        c[QStringLiteral("tier")]        = tierTag(l.tier);
        c[QStringLiteral("measureId")]   = l.drivingMeasureId;
        c[QStringLiteral("measure")]     = measureLabelOf(l.drivingMeasureId);
        c[QStringLiteral("phase")]       = measurePhaseOf(l.drivingMeasureId);
        // ONE wording for recurrence, and it comes out of the ledger header so an export and
        // the screen cannot disagree about what recurrence looks like.
        c[QStringLiteral("recurrence")]  = l.recurrence;
        c[QStringLiteral("fired")]       = l.fired;
        c[QStringLiteral("assessable")]  = l.assessable;
        c[QStringLiteral("fresh")]       = l.freshThisShot;
        c[QStringLiteral("resolving")]   = l.resolving;
        c[QStringLiteral("focused")]     = id == m_focusConditionId;
        c[QStringLiteral("ticks")]       = ticksFor(l, selectedTick);
        // HOW FAR, beside whether: the corridor strip and the value-by-shot run, drawn under the
        // card's recurrence line in place of the bare tick run. See spreadFor().
        c[QStringLiteral("spread")]      = spreadFor(l, fi, selectedTick);

        // DIRECTION OR DISPERSION. Below the agreement gate the condition is still a pattern —
        // inconsistency is a finding — but the direction claim is suppressed, and the sentence
        // that says so is fixed by the brief to the character.
        c[QStringLiteral("directionClaimed")] = l.directionClaimed;
        c[QStringLiteral("directionAgreement")] = l.directionAgreement;
        // The consistent case is worded as a COUNT, never a percentage — the agreement
        // percentage is disclosed only in the dispersion sentence, where the number is the
        // finding. "92% of 6 firings" is exactly the small-n pseudo-precision rule 1 exists
        // to keep off a card.
        const int modalCount = int(std::lround(l.directionAgreement * l.fired));
        const QString side = l.modalDirection > 0 ? QStringLiteral("The high side")
                                                   : QStringLiteral("The low side");
        c[QStringLiteral("directionText")] =
            l.directionClaimed
                ? (l.modalDirection == 0
                       ? QString()
                       : (modalCount >= l.fired
                              ? QStringLiteral("%1, every firing.").arg(side)
                              : QStringLiteral("%1 on %2 of its %3 firings.")
                                    .arg(side).arg(modalCount).arg(l.fired)))
                : QStringLiteral("Direction agreement %1%, below the %2% gate: dispersion, not a direction.")
                      .arg(int(std::lround(l.directionAgreement * 100.0)))
                      .arg(int(std::lround(m_opt.directionAgreementGate * 100.0)));

        // TREND + RECENCY. `resolving` wins over the plain recency line (brief §3.2).
        c[QStringLiteral("trendKnown")] = l.trendKnown;
        c[QStringLiteral("trend")]      = l.trendKnown ? (l.trend == Trend::Improving
                                                               ? QStringLiteral("improving")
                                                               : QStringLiteral("worsening"))
                                                        : QStringLiteral("stable");
        c[QStringLiteral("trendArrow")] = l.trendKnown ? (l.trend == Trend::Improving
                                                               ? QStringLiteral("↓")
                                                               : QStringLiteral("↑"))
                                                        : QString();
        // ⚠ SAY WHAT THE TREND IS OF. The test is Kendall on |z| — whether the MISS grows or
        // shrinks across the session — and it is silent on whether the condition recurs, which
        // the recurrence line above it already states. "no clear trend" under "15 of 15
        // measurable shots" read as the panel doubting the pattern; it meant the size held.
        c[QStringLiteral("trendText")] =
            l.trendKnown ? (l.trend == Trend::Improving
                                ? QStringLiteral("improving — the misses are shrinking")
                                : QStringLiteral("worsening — the misses are growing"))
                          : (l.trendPoints < m_opt.minPointsForTrend
                                 ? QStringLiteral("trend after %1 measurable shots").arg(m_opt.minPointsForTrend)
                                 : (l.rangeRestricted
                                        ? QStringLiteral("every firing, holding steady")
                                        : QStringLiteral("steady — the miss isn't growing or shrinking")));
        c[QStringLiteral("recencyText")] =
            l.resolving
                ? QStringLiteral("resolving · none in the last %1").arg(l.sinceLastFiring)
                : (l.sinceLastFiring < 0
                       ? QStringLiteral("not seen this session")
                       : l.sinceLastFiring == 0
                             ? QStringLiteral("fired on the last measurable shot")
                             : QStringLiteral("last fired %1 measurable shots ago").arg(l.sinceLastFiring));

        // This shot's marker on the card, and the review extras.
        const ConditionRow *here = (fi >= 0) ? rowFor(m_shots[size_t(fi)], id) : nullptr;
        const QString hereState = here ? shotStateKind(here->state) : QStringLiteral("notAssessable");
        c[QStringLiteral("thisShot")] = hereState;
        // THE PICKED TENSE — "FIRED HERE", "3 more firings after this shot" — whenever the card is
        // about a picked swing rather than simply the newest: in review, on a closed session, and
        // on the condition detail of a live session with a swing picked (selectedTick >= 0; see
        // detailFocusIndex()). The live panel's own cards pass -1 and keep the present tense.
        const bool pickedTense = m_reviewing || m_closed || selectedTick >= 0;
        // a shot a gate or rule kept this from being judged on says the gate's words, not NOT MEASURED
        const RowGate hereGate = rowGate(here);
        const QString unmeasuredPill = hereGate.on ? hereGate.words.toUpper() : QStringLiteral("NOT MEASURED");
        c[QStringLiteral("statePill")] =
            pickedTense
                ? (hereState == QLatin1String("fired") ? QStringLiteral("FIRED HERE")
                 : hereState == QLatin1String("clean") ? QStringLiteral("CLEAN HERE")
                                                       : unmeasuredPill)
                : (hereState == QLatin1String("fired") ? QStringLiteral("FIRED")
                 : hereState == QLatin1String("clean") ? QStringLiteral("CLEAN")
                                                       : unmeasuredPill);
        if (here && here->state != ShotState::NotAssessable && here->composite.present()) {
            // A COMPOSITE's value slot quotes its dominant part's change in that part's own unit and
            // words, never the composite's number, and it has no corridor in a golfer's units (§8).
            const QVariantMap wc = whatChangedOf(*here);
            c[QStringLiteral("valueText")]    = wc.value(QStringLiteral("dominant")).toMap().value(QStringLiteral("valueText")).toString();
            c[QStringLiteral("corridorText")] = QString();
            c[QStringLiteral("whatChanged")]  = wc;
        } else if (here && here->state != ShotState::NotAssessable) {
            c[QStringLiteral("valueText")]    = withUnit(fmtNumber(here->value), unit);
            // THE PASS BAND AND THE FAULT LINE — the two edges the strip under it draws — and not
            // the stored Ideal band, which quoted "pass 0.0 to 4.3" over a strip whose fault line
            // sat at 13. One sentence, from session_spread.h, for every surface that states it.
            c[QStringLiteral("corridorText")] = spreadCorridorWords(corridorForRow(*here), unit);
        } else {
            // the gate's or rule's words in the value slot where one accounts for it (§7.0), the reason
            // in the corridor slot as before; a record's reading opens in the Trust panel (addGate)
            c[QStringLiteral("valueText")]    = hereGate.on ? hereGate.words : QStringLiteral("not measurable");
            c[QStringLiteral("corridorText")] = here ? here->notAssessableReason : QString();
        }
        addGate(c, here, fi >= 0 ? m_swingDirs.value(m_shots[size_t(fi)].shotId) : QString());
        // HOW FAR OUT, beside WHETHER it was out. The pill says fired or clean and the two
        // numbers above say what was read and what it was tested against; the meter is the
        // third question a golfer asks and the only one the card could not answer — marginal,
        // or a mile.
        addStrength(c, here);
        // WHAT THE ROW IS ORDERED ON, published beside what this swing did. The meter's step is
        // a fact about THIS shot; this is the session-level median behind the card's position,
        // and a surface that wanted to explain the order — or a test that wants to assert it —
        // should not have to re-derive it from the ticks.
        c[QStringLiteral("sessionExcess")] = l.firingExcess;
        // Its place in that order, as the badge draws it. Empty for a condition that is not on
        // the ranked row at all — the detail's header card for a watched or unmeasured
        // condition has no rank to show, and must not borrow one.
        c[QStringLiteral("rankText")] = m_rankText.value(id);
        // The two filter facts. Absent from both hashes for anything off the ranked row, which
        // reads as false — a condition with no place in the row cannot be filtered into it.
        c[QStringLiteral("reachesBall")] = m_reachesBall.value(id, false);
        c[QStringLiteral("rootHere")]    = m_rootHere.value(id, false);
        if (pickedTense) {
            int after = 0;
            for (int i = fi + 1; i < int(l.run.size()); ++i)
                if (l.run[size_t(i)] == ShotState::Fired) ++after;
            c[QStringLiteral("firingsAfter")] = after;
            c[QStringLiteral("firingsAfterText")] =
                after == 0 ? QStringLiteral("no firings after this shot")
                           : QStringLiteral("%1 more %2 after this shot")
                                 .arg(after).arg(after == 1 ? QStringLiteral("firing")
                                                            : QStringLiteral("firings"));
        }

        c[QStringLiteral("evidence")] = evidenceProse(id, m_links);

        return c;
    }
}

// ONE LINE OF EVIDENCE PROSE (§4.2). The contingency sentence when an authored parent was
// actually tested — which is the design's own coach sentence — and the pack's consequence line
// otherwise. Never both, and never a manufactured one.
//
// The LINK SET IS A PARAMETER because the condition detail grades a different neighbourhood from
// the panel's: a condition drawn on the detail's downstream may have a tested parent the session
// graph never marshalled, and quoting the session's links there would print a sentence about an
// edge that is not on the page.
QString SessionDiagnosticsModel::evidenceProse(const QString &id,
                                               const std::vector<LinkEvidence> &links) const
{
    for (const LinkEvidence &e : links) {
        if (e.to != id || !e.fisherTested) continue;
        return QStringLiteral("%1 on %2 of the %3 swings where %4 fired — on %5 of the %6 where it did not.")
            .arg(conditionName(id))
            .arg(e.a).arg(e.a + e.b)
            .arg(conditionName(e.from))
            .arg(e.c).arg(e.c + e.d);
    }
    return consequenceOf(id);
}

// ── Zone 2: the chain rail ──────────────────────────────────────────────────────────────

void SessionDiagnosticsModel::buildChains()
{
    m_chains.clear();
    m_unchained.clear();
    m_unchainedLine.clear();

    const int fi = focusIndex();
    const int selectedTick = (m_reviewing || m_closed) ? fi : -1;

    for (const Chain &chain : m_rails.chains) {
        QVariantList nodes, links;
        for (size_t k = 0; k < chain.nodes.size(); ++k) {
            const ChainNode &cn = chain.nodes[k];
            nodes.append(nodeMap(cn.id, nodeFor(m_nodes, cn.id), cn.kind, m_links, fi, selectedTick));
            if (cn.hasLink) links.append(linkMap(cn.link));
        }

        int live = 0;
        for (const ChainNode &cn : chain.nodes)
            if (cn.kind == ChainNodeKind::LiveCard) ++live;

        m_chains.append(QVariantMap{
            { QStringLiteral("nodes"),     nodes },
            { QStringLiteral("links"),     links },
            { QStringLiteral("anyLive"),   chain.anyLive },
            { QStringLiteral("liveCount"), live },
        });
    }

    // Most-evidenced rail first. The rule is railBefore() at the top of this file, shared with
    // the condition detail; how many to draw, and what the "more" affordance is, remains the
    // panel's decision.
    std::sort(m_chains.begin(), m_chains.end(), [](const QVariant &a, const QVariant &b) {
        return railBefore(a.toMap(), b.toMap());
    });

    // A pattern with no authored edge to any other pattern gets its OWN line — reported,
    // never forced onto a chain to tidy the picture (brief §5.3).
    QStringList names;
    for (const QString &id : m_rails.unchainedPatterns) {
        const ConditionLedger *l = ledger(id);
        m_unchained.append(QVariantMap{
            { QStringLiteral("id"),         id },
            { QStringLiteral("name"),       conditionName(id) },
            { QStringLiteral("recurrence"), l ? l->recurrence : QString() },
        });
        names << conditionName(id);
    }
    if (!names.isEmpty()) {
        m_unchainedLine =
            names.size() == 1
                ? QStringLiteral("%1 is a pattern the model authors no edge for this session — "
                                 "reported on its own.").arg(names.first())
                : QStringLiteral("%1 are patterns the model authors no edge between this session — "
                                 "reported on their own.").arg(names.join(QStringLiteral(", ")));
    }
}

// THE MARK IS THE HONESTY DEVICE. Each of the four says a different thing and they must not be
// blurred: a screened root is not a ghost, and a ghost the pack ASSERTS is not a ghost nobody has
// written a measure for.
//
// GENERALISED PAST THE DECLARED MISS. The rail only ever draws one outcome node — the miss the
// golfer declared — but the condition detail's downstream ends at whatever outcomes the pack
// authors, declared or not. So the outcome mark now names WHICH outcome it is: the declared miss
// keeps its sub-label (it is the session's own question), and any other outcome says it is an
// outcome. The launch-monitor clause is the same clause in both cases, because it is the same
// fact — this session carried a monitor, so a ball-flight claim is verifiable rather than
// authored. The recurrence line beside it carries the count ("N of M measurable shots").
// WHY THERE IS NO READING HERE — and it is FIVE answers, not one.
//
// ⚠ THIS USED TO SAY "ghost · measure planned" ABOUT EVERY NODE THAT WAS NOT ASSERTED, and that
// is a claim about the MODEL made out of a fact about the CAPTURE. NodeSpec::measurable means
// "this capture answered it" (marshalGraph says so deliberately) — which is the right test for
// whether a node can carry a count, and no test at all of whether anybody has written the
// measure. Hanging back and reverse pivot are the cases that exposed it: both are authored
// `confirmedBy: measured` against live producers (pelvis lateral sway, thorax drift, secondary
// axis tilt at P4 and P7), and both were being drawn as work somebody has yet to do.
//
// The difference matters because the READER CAN ACT ON ONE AND NOT THE OTHER. "The measure is
// planned" is a roadmap item and nothing the golfer can do changes it. "This capture did not
// answer it" is a face-on camera, a segmented P5, a launch monitor — a thing about today's
// session, and the ledger rows already carry the mechanical reason in the vocabulary the review
// strip prints. Saying the first when the second is true tells a golfer their equipment is fine
// and the software is behind, when the truth is the exact reverse.
//
// The walk is the pack's own: condition → its detecting signals → their measures → the status
// each measure carries. MeasureStatus already draws every distinction needed, so nothing is
// invented here — it is read.
QString SessionDiagnosticsModel::ghostMark(const QString &id, const NodeSpec *ns) const
{
    // The pack says outright that this one cannot be measured. Nothing below can override it.
    if (ns && ns->asserted)
        return QStringLiteral("no measure · authored as asserted");
    if (!m_packProv)
        return QStringLiteral("no reading on this capture");

    const CharacteristicPack &pack = m_packProv->pack();
    const Condition *c = pack.condition(id);

    bool anyLive = false, anyExternal = false, anyNotCapturable = false, anySignal = false;
    if (c) {
        for (const QString &sid : c->detectedBy) {
            const Signal *sig = pack.signal(sid);
            if (!sig) continue;
            for (const QString &mid : sig->measures) {
                const Measure *m = pack.measure(mid);
                if (!m) continue;
                anySignal = true;
                switch (m->status) {
                case MeasureStatus::Live:           anyLive = true;          break;
                case MeasureStatus::ExternalDevice: anyExternal = true;      break;
                case MeasureStatus::NotCapturable:  anyNotCapturable = true; break;
                case MeasureStatus::Held:
                case MeasureStatus::Planned:
                case MeasureStatus::NoProducer:     break;
                }
            }
        }
    }

    // THE CASE THIS FUNCTION EXISTS FOR. A live producer exists and this capture still answered
    // nothing — so the honest mark names the capture, and names the REASON when the session
    // carried rows that said one. The reason is the same mechanical sentence the review strip
    // prints in the corridor slot, so the two surfaces cannot word it differently.
    if (anyLive) {
        const QString why = withheldReason(id);
        return why.isEmpty() ? QStringLiteral("measured today · not answered on this capture")
                             : QStringLiteral("measured today · %1").arg(why);
    }
    if (anyExternal)
        return QStringLiteral("no measure · needs a device this session did not carry");
    if (anyNotCapturable)
        return QStringLiteral("no measure · no sensor here can resolve it");
    // Either every measure behind it is Planned/NoProducer, or the pack authors no signal at
    // all. Both are roadmap, and only now is that word the truth.
    return anySignal ? QStringLiteral("no measure · a producer is planned")
                     : QStringLiteral("no measure · none authored yet");
}

// The first reason this session gave for withholding a condition, or empty. Rows carry it per
// shot (ConditionRow::notAssessableReason, never blank on a NotAssessable row); a node with no
// reading has one on every shot it was tried, and they are the same sentence, so the first is
// the answer. Linear over a session's shots, which is tens of rows.
QString SessionDiagnosticsModel::withheldReason(const QString &id) const
{
    for (const ShotRecord &s : m_shots) {
        const ConditionRow *r = rowFor(s, id);
        if (r && !r->notAssessableReason.isEmpty()) return r->notAssessableReason;
    }
    return QString();
}

QString SessionDiagnosticsModel::markFor(const QString &id, ChainNodeKind kind,
                                         const NodeSpec *ns) const
{
    switch (kind) {
    case ChainNodeKind::ScreenedRoot:
        return (ns && ns->screenEntered) ? QStringLiteral("screened root · screen entered")
                                         : QStringLiteral("screened root · needs a physical screen");
    case ChainNodeKind::Ghost:
        return ghostMark(id, ns);
    case ChainNodeKind::Outcome: {
        const bool lm       = !m_lmShots.isEmpty();
        const bool declared = !m_declaredMiss.isEmpty() && id == m_declaredMiss;
        if (declared)
            return lm ? QStringLiteral("declared miss · launch-monitor verified")
                      : QStringLiteral("declared miss");
        return lm ? QStringLiteral("outcome · launch-monitor verified")
                  : QStringLiteral("outcome · authored");
    }
    case ChainNodeKind::Watched: {
        // MEASURED, AND THE EVIDENCE DOES NOT REACH THE CLAIM. Both halves are said, because
        // the count beside it ("3 of 7 measurable shots") is a fact the golfer can act on and
        // the absent card is one they would otherwise have to explain to themselves.
        const ConditionLedger *l = ledger(id);
        if (l && l->fired == 0)
            return QStringLiteral("measured · clean on every measurable shot");
        return QStringLiteral("measured · not yet a pattern");
    }
    case ChainNodeKind::LiveCard:
        break;
    }
    return QString();
}

// ONE CHAIN NODE. Shared by the rail and by the condition detail's cause and effect rails, so a
// node drawn on both surfaces is the same node — same mark, same recurrence, same run, same
// review tense.
QVariantMap SessionDiagnosticsModel::nodeMap(const QString &id, const NodeSpec *ns,
                                             ChainNodeKind kind,
                                             const std::vector<LinkEvidence> &links,
                                             int fi, int selectedTick) const
{
    const ConditionLedger *l = ledger(id);

    QVariantMap n;
    n[QStringLiteral("id")]      = id;
    n[QStringLiteral("name")]    = conditionName(id);
    n[QStringLiteral("kind")]    = chainNodeKindName(kind);
    n[QStringLiteral("measure")] = measureLabelOf(l ? l->drivingMeasureId : QString());
    n[QStringLiteral("phase")]   = measurePhaseOf(l ? l->drivingMeasureId : QString());
    n[QStringLiteral("focused")] = id == m_focusConditionId;
    n[QStringLiteral("mark")]    = markFor(id, kind, ns);
    // Whether this node's counts are a launch-monitor reading rather than a camera one. Read by
    // the detail's outcome headline, which must not say "measurable shots" about a session that
    // never carried a monitor.
    n[QStringLiteral("lmAnchored")] = !m_lmShots.isEmpty() && l && l->assessable >= 1;

    if (l) {
        n[QStringLiteral("recurrence")] = l->recurrence;
        n[QStringLiteral("ticks")]      = ticksFor(*l, selectedTick);
        n[QStringLiteral("trend")]      = l->trendKnown
                                             ? (l->trend == Trend::Improving
                                                    ? QStringLiteral("improving")
                                                    : QStringLiteral("worsening"))
                                             : QStringLiteral("stable");
        n[QStringLiteral("trendArrow")] = l->trendKnown
                                             ? (l->trend == Trend::Improving ? QStringLiteral("↓")
                                                                             : QStringLiteral("↑"))
                                             : QString();
        const ConditionRow *here = (fi >= 0) ? rowFor(m_shots[size_t(fi)], id) : nullptr;
        const QString hs = here ? shotStateKind(here->state) : QStringLiteral("notAssessable");
        // as the card: a gate's or rule's words where one kept this node from being judged here
        const RowGate hereGate = rowGate(here);
        const QString unmeasuredPill = hereGate.on ? hereGate.words.toUpper() : QStringLiteral("NOT MEASURED");
        n[QStringLiteral("state")] = hs;
        n[QStringLiteral("statePill")] =
            (m_reviewing || m_closed)
                ? (hs == QLatin1String("fired") ? QStringLiteral("FIRED HERE")
                 : hs == QLatin1String("clean") ? QStringLiteral("CLEAN HERE")
                                                : unmeasuredPill)
                : (hs == QLatin1String("fired") ? QStringLiteral("FIRED")
                 : hs == QLatin1String("clean") ? QStringLiteral("CLEAN")
                                                : unmeasuredPill);
        addStrength(n, here);
        addGate(n, here, fi >= 0 ? m_swingDirs.value(m_shots[size_t(fi)].shotId) : QString());

        // WHAT HAPPENED TO THIS NODE AFTER THE SELECTED SWING. The question review asks is
        // whether the shot was typical, and typical is only defined against what came next — a
        // condition that fired here and never again is a different fact from one that fired here
        // and eight more times. Published on the same terms as the pattern cards' (cardMap), so
        // the two rows agree.
        if (m_reviewing || m_closed) {
            int after = 0;
            for (int i = fi + 1; i < int(l->run.size()); ++i)
                if (l->run[size_t(i)] == ShotState::Fired) ++after;
            n[QStringLiteral("firingsAfter")] = after;
            n[QStringLiteral("firingsAfterText")] =
                after == 0 ? QStringLiteral("no firings after this shot")
                           : QStringLiteral("%1 more %2 after this shot")
                                 .arg(after).arg(after == 1 ? QStringLiteral("firing")
                                                            : QStringLiteral("firings"));
        }
    } else {
        // A node with no ledger was never assessed at all — no recurrence, no ticks, and the
        // pill says so rather than showing an empty count.
        n[QStringLiteral("recurrence")] = QString();
        n[QStringLiteral("ticks")]      = QVariantList();
        n[QStringLiteral("state")]      = QStringLiteral("notAssessable");
        n[QStringLiteral("statePill")]  = QStringLiteral("NOT MEASURED");
        addStrength(n, nullptr);
        n[QStringLiteral("trend")]      = QStringLiteral("stable");
        n[QStringLiteral("trendArrow")] = QString();
    }

    n[QStringLiteral("evidence")] = evidenceProse(id, links);
    return n;
}

// ONE GRADED LINK — the word, the stroke and the note under it, off the one §4.1 table.
QVariantMap SessionDiagnosticsModel::linkMap(const LinkEvidence &e) const
{
    const LinkStyle st = styleOf(e.grade);

    // THE NOTE UNDER THE LINK. The brief fixes four of these; the fifth (a Coherent link that is
    // NOT range restricted) it does not, because in the mock every Coherent link happens to be
    // range restricted. Saying "no variance to test" there would be a claim about the data that
    // is simply untrue, so the honest answer is the count that stopped the test.
    QString note;
    switch (e.grade) {
    case LinkGrade::MovedTogether:
        note = QStringLiteral("baseline %1 · %2 since focus").arg(e.baselineN).arg(e.interventionN);
        break;
    case LinkGrade::ConditionallyDependent:
        note = QStringLiteral("Fisher exact · %1 paired shots").arg(e.pairs);
        break;
    case LinkGrade::Coherent:
        note = e.sharedEvidence
                   ? QStringLiteral("measured from the same readings, so the link is not tested")
             : e.rangeRestricted
                   ? QStringLiteral("no variance to test")
                   : (e.pairs < m_opt.minPairsForDependence
                          ? QStringLiteral("%1 paired shots · too few to test").arg(e.pairs)
                          : QStringLiteral("tested · not dependent"));
        break;
    case LinkGrade::PresentTogether: {
        const ConditionLedger *lf = ledger(e.from);
        const ConditionLedger *lt = ledger(e.to);
        const bool observedBoth = lf && lt && lf->assessable >= 1 && lt->assessable >= 1;
        note = observedBoth ? QStringLiteral("no coherent order")
                            : QStringLiteral("neither measured");
        break;
    }
    case LinkGrade::Unanchored:
        note = e.screenOutstanding ? QStringLiteral("screen would confirm")
                                   : QStringLiteral("not established");
        break;
    }

    return QVariantMap{
        { QStringLiteral("from"),    e.from },
        { QStringLiteral("to"),      e.to },
        { QStringLiteral("grade"),   gradeName(e.grade) },
        { QStringLiteral("word"),    QString::fromLatin1(st.word) },
        { QStringLiteral("stroke"),  QString::fromLatin1(st.stroke) },
        { QStringLiteral("width"),   st.width },
        { QStringLiteral("arrow"),   st.arrow },
        { QStringLiteral("opacity"), st.opacity },
        { QStringLiteral("note"),    note },
        { QStringLiteral("pairs"),   e.pairs },
    };
}

// ── Zone 3: the driver footer ───────────────────────────────────────────────────────────

void SessionDiagnosticsModel::buildDriver()
{
    m_driver = QVariantMap();

    // Debounced by pattern-set stability WHILE THE SESSION IS RUNNING. Better absent than
    // flickering (§B8) — and the eligibility test lives in the ledger header so the footer and
    // a replay agree.
    //
    // THE DEBOUNCE IS A LIVE-ONLY DEVICE, AND AT THE CLOSE IT IS BYPASSED. Its whole argument
    // is that the pattern set may still move; on a finished session it cannot, and there is no
    // later shot for the driver to appear on. "The driver appears once the pattern set has
    // held still for 3 shots" printed under a closed session is a promise about a future that
    // does not exist. §B7: at the close the footer is definitive — it names the driver, or it
    // says in as many words that there is not one.
    const bool finished = m_closed || m_reviewing;
    const bool hasRoot  = !m_explanation.roots.empty();
    const bool eligible = hasRoot && (finished || driverFooterEligible(m_shots, m_opt));
    m_driver[QStringLiteral("eligible")] = eligible;
    m_driver[QStringLiteral("final")]    = finished;
    if (!eligible) {
        if (finished) {
            // Worded from what explain() ACTUALLY returned, and no further. It ranked no root:
            // that is either because there was nothing to rank, or because the patterns it had
            // are not joined by any authored cause. Neither is "your swing is fine", and
            // neither is dressed up as a finding.
            const int patterns = patternCount();
            m_driver[QStringLiteral("finalText")] =
                patterns == 0
                    ? QStringLiteral("No driver: this session found no pattern to explain.")
                : patterns == 1
                    ? QStringLiteral("No driver: this session's one pattern has no authored "
                                     "cause above it.")
                    : QStringLiteral("No driver: this session's patterns share no authored cause.");
        } else {
            m_driver[QStringLiteral("waitingText")] =
                QStringLiteral("the driver appears once the pattern set has held still for %1 shots")
                    .arg(m_opt.driverDebounceShots);
        }
        return;
    }

    const RankedCause &top = m_explanation.roots.front();
    m_driver[QStringLiteral("rootId")]   = top.conditionId;
    m_driver[QStringLiteral("rootName")] = conditionName(top.conditionId);
    m_driver[QStringLiteral("coverage")] = top.coverage;
    // The one sentence that says WHY it is the driver, and it is a count of what it accounts
    // for rather than a score — the score is ordinal and publishing it would imply a
    // probability it is not.
    m_driver[QStringLiteral("whyText")] =
        QStringLiteral("%1 — would explain %2")
            .arg(conditionName(top.conditionId), patternsExplained(top.coverage));

    // INFERRED, NOT MEASURED. A root this session never assessed (no assessable shot — its measure
    // has no producer, or it is latent) and that no entered screen confirms is a conclusion the
    // model REACHED from its effects, not one it SAW. Said on the footer, because the posterior
    // ranking (session_diagnostics_design.md §A8.6) can put such a cause first — S-posture, whose
    // lumbar-curve measure has no producer, led three of four library sessions on 1 Oct 2026.
    {
        const ConditionLedger *own = ledgerFor(m_ledgers, top.conditionId);
        const bool measured  = own && own->assessable >= 1;
        const bool screened  = m_screens.value(top.conditionId, false);
        const bool inferred  = !measured && !screened;
        m_driver[QStringLiteral("inferred")] = inferred;
        if (inferred)
            m_driver[QStringLiteral("inferredText")] = QStringLiteral("inferred, not measured");
    }

    // HOW MUCH THE DRIVER DEPENDS ON READINGS NEAR THEIR EDGES (session_diagnostics_design.md
    // §A8.6) — the share of measurement-uncertainty draws that name it again, in WORDS. Never a
    // percentage: principle 5. Absent when not stamped (softTier off).
    if (top.stability >= 0.0) {
        m_driver[QStringLiteral("stability")] = top.stabilityWord;
        m_driver[QStringLiteral("stabilityText")] =
            top.stabilityWord == QLatin1String("firm")
                ? QStringLiteral("firm: it holds whichever way the borderline readings fall")
            : top.stabilityWord == QLatin1String("likely")
                ? QStringLiteral("likely: a borderline reading or two could change it")
                : QStringLiteral("fragile: it rests on readings close to their edges");
    }

    QVariantList explains;
    for (const QString &id : top.explains)
        explains.append(QVariantMap{ { QStringLiteral("id"), id },
                                     { QStringLiteral("name"), conditionName(id) } });
    m_driver[QStringLiteral("explains")] = explains;

    // THE SCREEN CTA, with its reason. The highest-value output of the whole model and the
    // only one that costs no hardware: a thirty-second physical test that anchors a chain.
    if (!m_explanation.recommendations.empty()) {
        const TestRecommendation &rec = m_explanation.recommendations.front();
        const Screen *sc = sharedScreenSet().screen(rec.screenRef);
        m_driver[QStringLiteral("screenConditionId")] = rec.conditionId;
        m_driver[QStringLiteral("screenRef")]         = rec.screenRef;
        m_driver[QStringLiteral("screenLabel")]       = sc ? sc->label : rec.screenRef;
        m_driver[QStringLiteral("screenCta")] =
            QStringLiteral("%1 would anchor this chain").arg(sc ? sc->label : conditionName(rec.conditionId));
        m_driver[QStringLiteral("screenReason")] =
            QStringLiteral("it would explain %1").arg(patternsExplained(rec.coverage));
        m_driver[QStringLiteral("screenProtocol")] = sc ? sc->protocol : QString();
    }

    // THE RIVAL-PARENT DISCLOSURE. Named, and explicitly NOT adjudicated when the rival is
    // unmeasurable or under-paired — the panel shows the most consistent chain and never
    // asserts uniqueness (§A4).
    if (!m_rails.rivals.empty())
        m_driver[QStringLiteral("rival")] = rivalMap(m_rails.rivals.front());

    // Asserted causes are OFFERED and phrased as questions, never concluded (relation_resolver
    // rule 2). The question mark is not decoration — it is the whole epistemic status.
    QVariantList offered;
    for (const RankedCause &o : m_explanation.offered) {
        offered.append(QVariantMap{
            { QStringLiteral("id"),   o.conditionId },
            { QStringLiteral("name"), conditionName(o.conditionId) },
            { QStringLiteral("question"),
              QStringLiteral("%1?").arg(conditionName(o.conditionId)) },
            { QStringLiteral("coverage"), o.coverage },
        });
    }
    m_driver[QStringLiteral("offered")] = offered;
}

// ONE RIVAL DISCLOSURE. The footer prints the session's top one; the condition detail prints the
// ones over the ancestry it is drawing. The same sentence either way, because it is the same
// claim — and the same refusal to adjudicate, which is the part that must not drift.
QVariantMap SessionDiagnosticsModel::rivalMap(const RivalParent &rp) const
{
    return QVariantMap{
        { QStringLiteral("childId"),        rp.childId },
        { QStringLiteral("childName"),      conditionName(rp.childId) },
        { QStringLiteral("chosenParentId"), rp.chosenParentId },
        { QStringLiteral("chosenName"),     conditionName(rp.chosenParentId) },
        { QStringLiteral("rivalParentId"),  rp.rivalParentId },
        { QStringLiteral("rivalName"),      conditionName(rp.rivalParentId) },
        { QStringLiteral("adjudicated"),    rp.adjudicated },
        { QStringLiteral("text"),
          rp.adjudicated
              ? QStringLiteral("%1 could also explain %2; this session's evidence favours %3.")
                    .arg(conditionName(rp.rivalParentId), conditionName(rp.childId),
                         conditionName(rp.chosenParentId))
              : QStringLiteral("%1 could also explain %2 — not adjudicated: it is not measurable "
                               "on this capture.")
                    .arg(conditionName(rp.rivalParentId), conditionName(rp.childId)) },
    };
}

// ── Closing: session bookends (§B7) ─────────────────────────────────────────────────────

void SessionDiagnosticsModel::buildBookends()
{
    m_bookends.clear();
    if (effectiveStage() != Stage::Closing) return;   // the closing strip replaces this-shot

    for (const ConditionLedger &l : m_ledgers) {
        if (l.tier != Tier::Pattern) continue;
        const Bookends bk = conditionBookends(m_shots, l.id);
        if (!bk.has) continue;
        m_bookends.append(QVariantMap{
            { QStringLiteral("id"),   l.id },
            { QStringLiteral("name"), conditionName(l.id) },
            { QStringLiteral("worstShotId"),  bk.worstShotId },
            { QStringLiteral("bestShotId"),   bk.bestShotId },
            { QStringLiteral("representativeShotId"), bk.representativeShotId },
            // ⚠ THE SHOT NUMBER LEADS. These three read in a cell the closing row divides
            // between every pattern, and at a real session's twelve the label ran on long
            // enough that elision ate the number — "most representative · sh…" — which is
            // the one thing on the card a golfer can act on. Leading with it means the tail
            // is what gets cut, and the tail is the word for what the number means.
            { QStringLiteral("worstText"),
              QStringLiteral("worst %1").arg(bk.worstShotId) },
            { QStringLiteral("bestText"),
              QStringLiteral("best %1").arg(bk.bestShotId) },
            // The one that is the point: shown their most representative swing, a golfer
            // learns what their swing IS rather than what its range is.
            { QStringLiteral("representativeText"),
              QStringLiteral("shot %1 · most representative").arg(bk.representativeShotId) },
        });
    }
}

// ── Review payloads (brief §6 / option 13a) ─────────────────────────────────────────────

QVariantMap SessionDiagnosticsModel::shotReadout(int shotId) const
{
    const int idx = indexOfShot(shotId);
    if (idx < 0) return QVariantMap();
    const ShotRecord &shot = m_shots[size_t(idx)];

    int fired = 0, clean = 0, na = 0, firedPatterns = 0;
    // FOUR BUCKETS, ORDERED BY HOW MUCH THEY SAY ABOUT THIS SWING. On a real capture the
    // measurable set runs to thirty-odd conditions, and published in pack order the four that
    // fired arrive scattered among twenty-six cells reading "not measurable · clean all
    // session" — two facts that, together, are the definition of nothing having happened. The
    // information is not withheld (§8: nothing withheld, one tap away); it is SORTED, and the
    // silent tail is marked so a surface can put it behind one line instead of five rows.
    QVariantList firedCells, cleanCells, watchedCells, tailCells;

    // The POPULATION is the session's measurable set — conditions this capture answered at
    // least once. Listing all ~150 pack conditions would bury the nine that mean something
    // under a hundred and forty rows of "not measurable", which is not honesty, it is noise
    // wearing honesty's clothes. The coverage line is where the other hundred and forty are
    // accounted for, in one sentence, as the design intends.
    // …AND A CONDITION A GATE KEPT FROM BEING JUDGED, even when it was judged on no shot at all
    // (trust_and_gates_design.md §4.4): "sequence order: not judged, pelvis peak out of sight" on all
    // six shots of a face-on session is not noise — it is the finding the golfer is owed.
    // A RECORD on one reading only: a rule (no subject) and the whole-shot exclusion ("shot", which
    // withholds every condition the pack holds) say why a measurable condition was not judged, but would
    // flood the grid with conditions this session never measured if they widened the population too.
    const auto gatedHere = [&](const QString &id) { return recordGated(rowGate(rowFor(shot, id))); };
    int naGated = 0;
    QStringList gatedOrder;
    QHash<QString, int> gatedWords;
    const QString shotDir = m_swingDirs.value(m_shots[size_t(idx)].shotId);
    QStringList ordered = m_displayOrder;
    for (const ConditionLedger &l : m_ledgers)
        if ((l.assessable >= 1 || gatedHere(l.id)) && !ordered.contains(l.id)) ordered.push_back(l.id);

    for (const QString &id : std::as_const(ordered)) {
        const ConditionLedger *l = ledger(id);
        if (!l || l->sessionScope) continue;                          // no per-swing verdict (§10.8 E2)
        if (l->assessable < 1 && !gatedHere(id)) continue;

        const ConditionRow *r = rowFor(shot, id);
        const ShotState st = r ? r->state : ShotState::NotAssessable;
        if (st == ShotState::Fired) {
            ++fired;
            if (l->tier == Tier::Pattern) ++firedPatterns;
        } else if (st == ShotState::Clean) {
            ++clean;
        } else {
            ++na;
        }

        const QString unit = measureUnitOf(l->drivingMeasureId);
        int after = 0;
        for (int i = idx + 1; i < int(l->run.size()); ++i)
            if (l->run[size_t(i)] == ShotState::Fired) ++after;

        QVariantMap c;
        c[QStringLiteral("id")]        = id;
        c[QStringLiteral("name")]      = conditionName(id);
        c[QStringLiteral("stateKind")] = shotStateKind(st);
        // OUT / IN / — in the state colour. The em dash is the third state and it is not a
        // blank: "we did not look" is a fact the golfer is owed.
        c[QStringLiteral("state")] = st == ShotState::Fired ? QStringLiteral("OUT")
                                   : st == ShotState::Clean ? QStringLiteral("IN")
                                                            : QStringLiteral("—");
        if (st != ShotState::NotAssessable && r && r->composite.present()) {
            // A composite: its dominant part's change, never its own number (§8), and its parts for
            // the registers' What changed fold.
            const QVariantMap wc = whatChangedOf(*r);
            c[QStringLiteral("valueText")]    = wc.value(QStringLiteral("dominant")).toMap().value(QStringLiteral("valueText")).toString();
            c[QStringLiteral("corridorText")] = QString();
            c[QStringLiteral("reason")]       = QString();
            c[QStringLiteral("whatChanged")]  = wc;
        } else if (st != ShotState::NotAssessable && r) {
            c[QStringLiteral("valueText")] = withUnit(fmtNumber(r->value), unit);
            // The same sentence the card prints (spreadCorridorWords) — pass band and fault line.
            const SpreadCorridor sc = corridorForRow(*r);
            c[QStringLiteral("corridorText")] = spreadCorridorWords(sc, unit);
            // the fault line alone, beside the reading in the index (unitless: the reading carries it)
            int side = 0;                                      // the condition's own tail, from its signal
            if (m_packProv)
                if (const Condition *pc = m_packProv->pack().condition(id))
                    for (const QString &sid : pc->detectedBy)
                        if (const Signal *sig = m_packProv->pack().signal(sid); sig && sig->direction
                            && (r->drivingMeasureId.isEmpty() || sig->measures.contains(r->drivingMeasureId))) {
                            side = *sig->direction == Direction::High ? +1 : -1;
                            break;
                        }
            c[QStringLiteral("faultText")] = spreadFaultWords(sc, side);
            c[QStringLiteral("reason")] = QString();
        } else {
            // Never a blank in either slot: the value reads "not measurable" — or, where a gate or rule
            // accounts for it, that gate's words (§7.0) — and the corridor slot carries THE REASON. A
            // blank there reads as a bug.
            const RowGate g = rowGate(r);
            c[QStringLiteral("valueText")]    = g.on ? g.words : QStringLiteral("not measurable");
            c[QStringLiteral("corridorText")] = r ? r->notAssessableReason
                                                  : missingReasonText(MissingKind::MetricNotProduced);
            c[QStringLiteral("reason")]       = c[QStringLiteral("corridorText")];
            if (g.on) {
                ++naGated;
                if (!gatedWords.contains(g.words)) gatedOrder << g.words;
                gatedWords[g.words] += 1;
            }
        }
        // the gate's words and, for a record, the reading a click opens in the Trust panel (§7.0)
        addGate(c, r, shotDir);
        addStrength(c, r);
        c[QStringLiteral("tierTag")]      = tierTag(l->tier);
        c[QStringLiteral("recurrence")]   = l->recurrence;
        c[QStringLiteral("ticks")]        = ticksFor(*l, idx);
        c[QStringLiteral("selectedIndex")]= idx;
        c[QStringLiteral("firingsAfter")] = after;
        c[QStringLiteral("firingsAfterText")] =
            after == 0 ? QStringLiteral("no firings after this shot")
                       : QStringLiteral("%1 more %2 after this shot")
                             .arg(after).arg(after == 1 ? QStringLiteral("firing")
                                                        : QStringLiteral("firings"));

        // The tail is the conjunction of two silences: this capture could not answer it HERE,
        // and it never fired anywhere in the session. Either one alone is worth a cell — a
        // condition that fired on other swings but not this one is exactly what review is for
        // — so it takes both to be tail.
        const bool silentAllSession = (l->tier != Tier::Pattern && l->tier != Tier::Watching);
        // ...and a cell a gate record on its reading kept from being judged is never folded into "clean
        // all session": it was refused here, and keeps its own row with the gate's words (recordGated).
        const bool isTail = (st == ShotState::NotAssessable) && silentAllSession && !recordGated(rowGate(r));
        c[QStringLiteral("tail")] = isTail;

        if (st == ShotState::Fired)      firedCells.append(c);
        else if (st == ShotState::Clean) cleanCells.append(c);
        else if (!isTail)                watchedCells.append(c);
        else                             tailCells.append(c);
    }

    QVariantList conditions;
    conditions += firedCells;
    conditions += cleanCells;
    conditions += watchedCells;
    conditions += tailCells;

    QVariantMap out;
    out[QStringLiteral("shotId")]     = shotId;
    out[QStringLiteral("shotIndex")]  = idx;
    out[QStringLiteral("shotCount")]  = int(m_shots.size());
    out[QStringLiteral("club")]       = shot.club;
    out[QStringLiteral("swingDir")]   = m_swingDirs.value(shotId);   // the trust summary's shot (§7.4)
    out[QStringLiteral("contextId")]  = shot.contextId;
    out[QStringLiteral("firedCount")] = fired;
    out[QStringLiteral("cleanCount")] = clean;
    out[QStringLiteral("notAssessableCount")] = na;
    // ONE POPULATION, AND IT IS THE ONE THE GRID DRAWS. The denominator used to be fired+clean
    // — the conditions this swing actually answered — while the grid below it showed every
    // condition the SESSION can measure, so the headline said "4 of 6" over twenty-six visible
    // cells. Two counts of the same thing that do not match read as a bug in the panel, not as
    // a distinction; the measurable set is the honest denominator because it is what is on
    // screen, and the not-assessable count is still stated separately underneath so "we did
    // not look" is never folded into "it was fine".
    const int measurableSet = int(conditions.size());
    out[QStringLiteral("measurableSetCount")] = measurableSet;
    out[QStringLiteral("headline")] =
        QStringLiteral("%1 of %2 conditions fired on this swing · %3 of them %4")
            .arg(fired).arg(measurableSet).arg(firedPatterns)
            .arg(firedPatterns == 1 ? QStringLiteral("pattern") : QStringLiteral("patterns"));
    // those a gate or rule kept from being judged said apart, by its words; the old sentence when none
    out[QStringLiteral("notJudgedCount")] = naGated;
    out[QStringLiteral("note")] = na > 0 ? notAssessableNote(na, naGated, topOf(gatedOrder, gatedWords))
                                         : QStringLiteral("0 measures not assessable on this capture");

    // The tail's own line, in the model's words rather than the strip's, so an export and the
    // screen say the same thing about what was folded away. It names BOTH silences, because a
    // reader deciding whether to open it is owed the reason it was closed.
    const int tailN = int(tailCells.size());
    out[QStringLiteral("tailCount")] = tailN;
    out[QStringLiteral("tailSummary")] =
        tailN == 0 ? QString()
                   : QStringLiteral("%1 more · clean all session, not measurable on this swing")
                         .arg(tailN);
    out[QStringLiteral("conditions")] = conditions;
    return out;
}

QVariantList SessionDiagnosticsModel::pipsFor(int shotId) const
{
    QVariantList out;
    const int idx = indexOfShot(shotId);
    if (idx < 0) return out;
    const ShotRecord &shot = m_shots[size_t(idx)];

    // One pip per TRACKED condition, in the same order and over the same population as the
    // review strip — the pip row and the strip are the same nine facts at two sizes, and a
    // carousel that ordered them differently would make them impossible to read together.
    QStringList ordered = m_displayOrder;
    for (const ConditionLedger &l : m_ledgers)
        if (l.assessable >= 1 && !ordered.contains(l.id)) ordered.push_back(l.id);

    for (const QString &id : std::as_const(ordered)) {
        const ConditionLedger *l = ledger(id);
        if (!l || l->assessable < 1 || l->sessionScope) continue;   // no per-swing verdict (§10.8 E2)
        const ConditionRow *r = rowFor(shot, id);
        out.append(QVariantMap{
            { QStringLiteral("id"),    id },
            { QStringLiteral("state"), shotStateKind(r ? r->state : ShotState::NotAssessable) },
        });
    }
    return out;
}

QVariantMap SessionDiagnosticsModel::sequenceChecks(int shotId) const
{
    const int idx = indexOfShot(shotId);
    if (idx < 0) return {};
    return sequenceChecksToVariant(sequenceChecksOf(m_shots[size_t(idx)]));
}

QVariantList SessionDiagnosticsModel::sequenceRanking() const
{
    std::vector<SequenceRankEntry> entries;
    entries.reserve(m_shots.size());
    for (const ShotRecord &s : m_shots) entries.push_back(sequenceRankEntryOf(s));
    QVariantList out;
    for (int id : rankBySequence(std::move(entries))) out << id;
    return out;
}

int SessionDiagnosticsModel::firedCountFor(int shotId) const
{
    const int idx = indexOfShot(shotId);
    if (idx < 0) return 0;
    int n = 0;
    for (const ConditionRow &r : m_shots[size_t(idx)].rows)
        if (r.state == ShotState::Fired) ++n;
    return n;
}

// ── The condition detail (user-requested drill-in) ──────────────────────────────────────
//
// WHAT MIGHT HAVE CAUSED THIS, AND WHAT IT LEADS TO — one condition, its authored ancestry to the
// roots and its authored descent to the outcomes, with this session's evidence on every link.
//
// THE NEIGHBOURHOOD IS THIS CONDITION'S, NOT THE SESSION'S, and that is the whole reason it is
// marshalled again here rather than read off m_nodes/m_edges. buildGraph() keeps the session's
// picture: every pattern, everything upstream of one, and the descent to the DECLARED MISS and no
// further — which is right for a rail about the session and wrong for a page about a condition,
// because the question "what does this lead to" has an answer whether or not the golfer declared
// a miss, and a page that could only answer it after a declaration would answer it almost never.
// So the detail computes its own `keep` and hands it to the SAME marshaller, is graded by the
// SAME gradeLinks over the SAME rows, and draws the SAME node and link maps.
//
// PURE. It reads members and returns a map. No cadence, no ratchet, no persistence, no signal,
// no mutation — which is what makes it identical in live and in review for one ledger, and what
// makes openDetail() a panel action rather than a session event.
QVariantMap SessionDiagnosticsModel::conditionDetail(const QString &conditionId) const
{
    QVariantMap out;
    if (conditionId.isEmpty() || !m_packProv) return out;

    const CharacteristicPack &pack = m_packProv->pack();
    const Condition *self = pack.condition(conditionId);
    if (!self) return out;                       // a condition the pack does not author

    // ⚠ THE DETAIL FOLLOWS THE PICKED SWING, LIVE AS WELL AS IN REVIEW. It used to take
    // focusIndex(), which on a LIVE session is always the newest shot whatever the carousel has
    // picked — so with the panel open on the replay view, picking another swing rebuilt and
    // republished the detail, faithfully, about the wrong shot: header figure, chip, reading, the
    // ringed dot and the wide mark all stayed on the newest swing. detailFocusIndex() is the pick
    // when there is one the ledger holds and the newest otherwise, so a live session with nothing
    // picked (or the newest picked, which the post-shot replay does) still follows each new shot.
    //
    // The highlight marks that swing whenever it is a PICK — in review, closed, or live with a
    // swing selected — and not when the detail is simply following the newest, live.
    const int fi           = detailFocusIndex();
    const bool picked      = m_selectedShotId >= 0 && indexOfShot(m_selectedShotId) >= 0;
    const int selectedTick = (m_reviewing || m_closed || picked) ? fi : -1;

    // ── the neighbourhood, marshalled and graded exactly as the rail's is ───────────
    const QSet<QString> up   = causalClosure(pack, conditionId, /*downstream*/ false);
    const QSet<QString> down = causalClosure(pack, conditionId, /*downstream*/ true);
    QSet<QString> keep = up;
    keep.unite(down);
    keep.insert(conditionId);

    std::vector<NodeSpec> nodes;
    std::vector<EdgeSpec> edges;
    marshalGraph(keep, nodes, edges);

    FocusSplit focus;
    focus.declared    = !m_focusConditionId.isEmpty() && m_focusFromShot >= 0;
    focus.baselineEnd = m_focusFromShot - 1;
    const std::vector<LinkEvidence> links = gradeLinks(nodes, edges, m_ledgers, focus, m_opt);

    // The node's DRAWN kind. chainNodeKind()'s answer, with one addition the rail does not need:
    // an outcome-kind condition is drawn as an outcome whether or not it is the declared miss.
    // This is a DISPLAY statement only — NodeSpec::outcomeId is left alone, because nodePresent()
    // treats an outcome as observed and setting it on an undeclared outcome would upgrade every
    // link into it on a fact nobody established.
    auto kindOf = [&](const QString &id) {
        const Condition *c = pack.condition(id);
        if (c && c->kind == ConditionKind::Outcome) return ChainNodeKind::Outcome;
        const NodeSpec *ns = nodeFor(nodes, id);
        return ns ? chainNodeKind(*ns, ledger(id)) : ChainNodeKind::Ghost;
    };

    // ── the header: the same pattern card the panel draws, for this condition ───────
    const ConditionLedger *l = ledger(conditionId);
    QVariantMap header;
    if (l) {
        header = cardMap(*l, fi, selectedTick);
    } else {
        // A condition this capture never assessed — a ghost or a screened root reached from the
        // ancestry. The card DEGRADES rather than blanking: every slot the panel's card fills is
        // filled here too, with the honest answer in it.
        header[QStringLiteral("id")]               = conditionId;
        header[QStringLiteral("name")]             = conditionName(conditionId);
        header[QStringLiteral("consequence")]      = consequenceOf(conditionId);
        header[QStringLiteral("tier")]             = QString();
        header[QStringLiteral("recurrence")]       = QString();
        header[QStringLiteral("fired")]            = 0;
        header[QStringLiteral("assessable")]       = 0;
        header[QStringLiteral("fresh")]            = false;
        header[QStringLiteral("resolving")]        = false;
        header[QStringLiteral("focused")]          = conditionId == m_focusConditionId;
        header[QStringLiteral("ticks")]            = QVariantList();
        header[QStringLiteral("directionClaimed")] = false;
        header[QStringLiteral("directionText")]    = QString();
        header[QStringLiteral("trendKnown")]       = false;
        header[QStringLiteral("trend")]            = QStringLiteral("stable");
        header[QStringLiteral("trendArrow")]       = QString();
        header[QStringLiteral("trendText")]        = QString();
        header[QStringLiteral("recencyText")]      = QStringLiteral("not measurable on this capture");
        header[QStringLiteral("thisShot")]         = QStringLiteral("notAssessable");
        header[QStringLiteral("statePill")]        = QStringLiteral("NOT MEASURED");
        header[QStringLiteral("valueText")]        = QStringLiteral("not measurable");
        header[QStringLiteral("corridorText")]     = QString();
        addStrength(header, nullptr);
    }
    const ChainNodeKind selfKind = kindOf(conditionId);
    header[QStringLiteral("kind")] = chainNodeKindName(selfKind);
    header[QStringLiteral("mark")] = markFor(conditionId, selfKind, nodeFor(nodes, conditionId));
    // Off the DETAIL's links rather than the session's: a condition off the session's own graph
    // has parents the panel never graded, and the sentence on this page must quote the edges the
    // page is drawing.
    header[QStringLiteral("evidence")] = evidenceProse(conditionId, links);

    // ── path enumeration ───────────────────────────────────────────────────────────
    //
    // Every simple path from a ROOT down to this condition, and from this condition out to every
    // SINK below it (which is where the outcomes are — the pack forbids an outcome that causes
    // something, so an outcome is always a sink). Branch order is the rail's own: grade, then the
    // authored strength, then id — so the walk is deterministic and the page does not reshuffle
    // between launches, exactly as extractChains() promises for the rail.
    auto neighbours = [&](const QString &id, bool upstream) {
        std::vector<const EdgeSpec *> ns;
        for (const EdgeSpec &e : edges) {
            const QString other = upstream ? (e.to == id ? e.from : QString())
                                           : (e.from == id ? e.to : QString());
            if (other.isEmpty()) continue;
            // Stay on the side of the condition we are walking. An ancestor's OTHER children are
            // not on the way to this condition, and drawing them would answer a question nobody
            // asked with a claim about a sibling.
            if (upstream ? !up.contains(other) : !down.contains(other)) continue;
            ns.push_back(&e);
        }
        std::stable_sort(ns.begin(), ns.end(), [&](const EdgeSpec *x, const EdgeSpec *y) {
            const LinkEvidence *lx = linkFor(links, x->from, x->to);
            const LinkEvidence *ly = linkFor(links, y->from, y->to);
            const int gx = lx ? int(lx->grade) : 0;
            const int gy = ly ? int(ly->grade) : 0;
            if (gx != gy) return gx > gy;
            if (x->strength != y->strength) return x->strength > y->strength;
            return upstream ? x->from < y->from : x->to < y->to;
        });
        return ns;
    };

    std::vector<std::vector<QString>> upPaths, downPaths;
    bool upCapped = false, downCapped = false;

    std::function<void(const QString &, std::vector<QString> &, bool,
                       std::vector<std::vector<QString>> &, bool &)> walk =
        [&](const QString &cur, std::vector<QString> &path, bool upstream,
            std::vector<std::vector<QString>> &sink, bool &capped) {
            if (int(sink.size()) >= kDetailMaxPaths) { capped = true; return; }
            bool advanced = false;
            if (int(path.size()) < kDetailMaxDepth) {
                for (const EdgeSpec *e : neighbours(cur, upstream)) {
                    const QString next = upstream ? e->from : e->to;
                    // The pack is a DAG, but a path may still re-reach a node through a diamond;
                    // a node twice on one rail would draw the same card twice.
                    if (std::find(path.begin(), path.end(), next) != path.end()) continue;
                    advanced = true;
                    path.push_back(next);
                    walk(next, path, upstream, sink, capped);
                    path.pop_back();
                    if (int(sink.size()) >= kDetailMaxPaths) { capped = true; return; }
                }
            }
            // A root (walking up) or a sink (walking down). A one-node "path" is not a path.
            if (!advanced && path.size() >= 2) sink.push_back(path);
        };

    {
        std::vector<QString> path{ conditionId };
        walk(conditionId, path, /*upstream*/ true, upPaths, upCapped);
    }
    {
        std::vector<QString> path{ conditionId };
        walk(conditionId, path, /*upstream*/ false, downPaths, downCapped);
    }
    // ── the likely driver's path is ALWAYS drawn ───────────────────────────────────
    //
    // The cause headline names the resolver's ranked root when it accounts for this condition, but
    // the walk above ranks and caps the paths on its own rule, so the driver's path could be cut
    // and the page would open on a cause nothing beneath it shows (issue #15: Over the top named
    // Across the line at the top over eight paths that never touched it). So the driver is decided
    // here, and when no walked path runs through it one is built: up from this condition through
    // conditions the driver sits above, then on up from the driver to a root. railsOf() then puts
    // it first and lets the cap give way for it.
    QString driverId;
    int driverCoverage = 0;
    for (const RankedCause &r : m_explanation.roots) {
        if (!r.explains.contains(conditionId)) continue;
        driverId       = r.conditionId;
        driverCoverage = r.coverage;
        break;
    }
    auto through = [](const std::vector<QString> &p, const QString &id) {
        return std::find(p.begin(), p.end(), id) != p.end();
    };
    if (!driverId.isEmpty() && driverId != conditionId && up.contains(driverId)
        && std::none_of(upPaths.begin(), upPaths.end(),
                        [&](const std::vector<QString> &p) { return through(p, driverId); })) {
        const QSet<QString> belowDriver = causalClosure(pack, driverId, /*downstream*/ true);
        std::vector<QString> path{ conditionId };
        // Below the driver: every step stays on a condition the driver is an ancestor of, so the
        // walk cannot miss it. The first such branch in rail order, as the walk would take it.
        while (path.back() != driverId) {
            QString next;
            for (const EdgeSpec *e : neighbours(path.back(), /*upstream*/ true))
                if ((e->from == driverId || belowDriver.contains(e->from)) && !through(path, e->from)) {
                    next = e->from;
                    break;
                }
            if (next.isEmpty()) break;            // unreachable over a DAG; never loop
            path.push_back(next);
        }
        // Above the driver: the strongest branch to a root, to the walk's own depth.
        while (through(path, driverId) && int(path.size()) < kDetailMaxDepth) {
            QString next;
            for (const EdgeSpec *e : neighbours(path.back(), /*upstream*/ true))
                if (!through(path, e->from)) { next = e->from; break; }
            if (next.isEmpty()) break;
            path.push_back(next);
        }
        if (through(path, driverId)) upPaths.push_back(path);
    }
    // A driver with no drawn path is not named: the headline falls through to the next answer.
    if (std::none_of(upPaths.begin(), upPaths.end(),
                     [&](const std::vector<QString> &p) { return through(p, driverId); }))
        driverId.clear();

    // Walked upward the path came out condition-first; a rail reads in the swing's own direction.
    for (std::vector<QString> &p : upPaths) std::reverse(p.begin(), p.end());

    auto railFor = [&](const std::vector<QString> &path) {
        QVariantList ns, ls;
        int live = 0;
        for (size_t k = 0; k < path.size(); ++k) {
            const ChainNodeKind kind = kindOf(path[k]);
            if (kind == ChainNodeKind::LiveCard) ++live;
            ns.append(nodeMap(path[k], nodeFor(nodes, path[k]), kind, links, fi, selectedTick));
            if (k + 1 < path.size())
                if (const LinkEvidence *le = linkFor(links, path[k], path[k + 1]))
                    ls.append(linkMap(*le));
        }
        const QVariantMap last = ns.isEmpty() ? QVariantMap() : ns.last().toMap();
        return QVariantMap{
            { QStringLiteral("nodes"),     ns },
            { QStringLiteral("links"),     ls },
            { QStringLiteral("liveCount"), live },
            { QStringLiteral("anyLive"),   live > 0 },
            { QStringLiteral("primary"),   false },
            // The end of the rail, hoisted so a headline does not have to walk the node list.
            { QStringLiteral("endId"),         last.value(QStringLiteral("id")) },
            { QStringLiteral("endName"),       last.value(QStringLiteral("name")) },
            { QStringLiteral("endKind"),       last.value(QStringLiteral("kind")) },
            { QStringLiteral("endRecurrence"), last.value(QStringLiteral("recurrence")) },
            { QStringLiteral("endLmAnchored"), last.value(QStringLiteral("lmAnchored")) },
        };
    };

    auto railsOf = [&](const std::vector<std::vector<QString>> &paths, const QString &pin,
                       bool &capped) {
        QVariantList rails;
        for (const std::vector<QString> &p : paths) rails.append(railFor(p));
        // MOST-EVIDENCED FIRST, on the rail's own rule — see railBefore().
        std::sort(rails.begin(), rails.end(), [](const QVariant &a, const QVariant &b) {
            return railBefore(a.toMap(), b.toMap());
        });
        // …EXCEPT THE DRIVER'S. The best-ranked path through the named driver goes first, because
        // the headline above the list is about it, and the cap gives way for it: the lowest-ranked
        // of the rest is dropped rather than the one the headline names.
        if (!pin.isEmpty()) {
            for (int i = 0; i < rails.size(); ++i) {
                const QVariantList ns = rails.at(i).toMap().value(QStringLiteral("nodes")).toList();
                if (std::none_of(ns.begin(), ns.end(), [&](const QVariant &n) {
                        return n.toMap().value(QStringLiteral("id")).toString() == pin; }))
                    continue;
                rails.move(i, 0);
                break;
            }
            while (rails.size() > kDetailMaxPaths) { rails.removeLast(); capped = true; }
        }
        // The one the panel draws in full; the rest collapse to a line each. A flag rather than
        // a truncation, because nothing here is withheld — brief §8.
        if (!rails.isEmpty()) {
            QVariantMap first = rails.first().toMap();
            first[QStringLiteral("primary")] = true;
            rails[0] = first;
        }
        return rails;
    };

    const QVariantList causes  = railsOf(upPaths, driverId, upCapped);
    const QVariantList effects = railsOf(downPaths, QString(), downCapped);

    // ── the cause headline ─────────────────────────────────────────────────────────
    //
    // Four answers in strict order, and the order is an order of EVIDENCE. The resolver's ranked
    // root when it accounts for this condition; else the strongest authored parent this session
    // actually tested; else the screen that would settle the question; else the absence of an
    // answer, said out loud. Never forced: three of the four are refusals of a kind.
    QString causeHeadline;
    if (!driverId.isEmpty())
        causeHeadline = QStringLiteral("Likely driver: %1 — would explain %2.")
                            .arg(conditionName(driverId), patternsExplained(driverCoverage));
    if (causeHeadline.isEmpty()) {
        // A DIRECT PARENT ONLY COUNTS FROM COHERENT UP. Present-together means neither end was
        // measurable and Unanchored means the root was never screened — naming either as "the
        // strongest cause this session" would dress the absence of evidence as a finding, and
        // the screen CTA below is the honest thing to say in exactly those cases.
        const LinkEvidence *best = nullptr;
        for (const LinkEvidence &e : links) {
            if (e.to != conditionId || e.grade < LinkGrade::Coherent) continue;
            if (!best || e.grade > best->grade
                || (e.grade == best->grade && e.strength > best->strength))
                best = &e;
        }
        if (best) {
            const QVariantMap lm = linkMap(*best);
            causeHeadline = QStringLiteral("Strongest authored cause this session: %1 — %2, %3.")
                                .arg(conditionName(best->from),
                                     lm.value(QStringLiteral("word")).toString().toLower(),
                                     lm.value(QStringLiteral("note")).toString());
        }
    }
    if (causeHeadline.isEmpty()) {
        for (const NodeSpec &n : nodes) {          // pack order, so the same root every launch
            if (!up.contains(n.id) || !n.screened || n.screenEntered) continue;
            const Condition *c = pack.condition(n.id);
            const Screen *sc = c ? sharedScreenSet().screen(c->screenRef) : nullptr;
            causeHeadline = QStringLiteral("%1 would anchor this chain — not yet screened.")
                                .arg(sc ? sc->label : conditionName(n.id));
            break;
        }
    }
    if (causeHeadline.isEmpty())
        causeHeadline = QStringLiteral("No cause the capture can see today.");

    // ── the outcome headline ───────────────────────────────────────────────────────
    //
    // The outcome reached by the strongest-evidenced downstream path, and its evidence is a
    // COUNT or it is nothing. There is deliberately no probability language anywhere in this
    // sentence: "most likely" is the ordering's claim (this is the best-evidenced path the model
    // authors), and the only number that follows it is the one the ledger counted.
    QString outcomeHeadline;
    for (const QVariant &rv : effects) {
        const QVariantMap rail = rv.toMap();
        if (rail.value(QStringLiteral("endKind")).toString() != QLatin1String("outcome")) continue;
        const QString name = rail.value(QStringLiteral("endName")).toString();
        const QString rec  = rail.value(QStringLiteral("endRecurrence")).toString();
        outcomeHeadline =
            (rail.value(QStringLiteral("endLmAnchored")).toBool() && !rec.isEmpty())
                ? QStringLiteral("Most likely outcome: %1 — %2.").arg(name, rec)
                : QStringLiteral("Most likely outcome: %1 — authored; not measurable with this "
                                 "capture.").arg(name);
        break;
    }
    if (outcomeHeadline.isEmpty()) {
        outcomeHeadline =
            selfKind == ChainNodeKind::Outcome
                ? QStringLiteral("This is the outcome — the model authors nothing downstream of it.")
                : QStringLiteral("No authored outcome downstream of this condition.");
    }

    // ── rivals: named, never adjudicated ───────────────────────────────────────────
    // extractChains()' own rival pass over this neighbourhood, so the page discloses what the
    // footer would have, on the same terms. Scoped to the ANCESTRY: a rival parent of something
    // downstream is a fact about that condition's page, not this one's.
    const ChainRails localRails = extractChains(nodes, edges, m_ledgers, links, m_opt);
    QVariantList rivals;
    for (const RivalParent &rp : localRails.rivals) {
        if (rp.childId != conditionId && !up.contains(rp.childId)) continue;
        rivals.append(rivalMap(rp));
    }

    out[QStringLiteral("id")]              = conditionId;
    out[QStringLiteral("name")]            = conditionName(conditionId);
    out[QStringLiteral("header")]          = header;
    out[QStringLiteral("causeHeadline")]   = causeHeadline;
    out[QStringLiteral("causeDriverId")]   = driverId;   // empty unless the headline names one
    out[QStringLiteral("outcomeHeadline")] = outcomeHeadline;
    out[QStringLiteral("causes")]          = causes;
    out[QStringLiteral("effects")]         = effects;
    out[QStringLiteral("causesCapped")]    = upCapped;
    out[QStringLiteral("effectsCapped")]   = downCapped;
    out[QStringLiteral("rivals")]          = rivals;
    // ACROSS SESSIONS, in review only. A live session is the one being made; the reader between
    // balls wants this session's strip, and a column of last month's swings beside it is a
    // different conversation. A finished session is exactly where "and compared to before?"
    // is the next question.
    // ── ONE TAB PER MEASURE, when the condition is read by more than one ─────────────
    // Early extension is the pelvis toward the ball OR the spine standing up; the header card's
    // one ruler shows whichever decided most shots, and the other measure would never be seen.
    // Each tab redraws the shot-by-shot and across-sessions pictures on its own measure. The
    // default is the measure that fired on most shots (then the one read most, then pack order),
    // so the page opens on what is doing the work.
    QString measureId;
    if (l) {
        const QStringList ms = conditionMeasures(conditionId);
        if (ms.size() > 1) {
            QVariantList tabs;
            int bestFired = -1, bestRead = -1;
            QString best;
            for (const QString &mid : ms) {
                int fired = 0, read = 0;
                for (const ShotRecord &sh : m_shots) {
                    const ConditionRow *r = rowFor(sh, conditionId);
                    if (!r || r->state == ShotState::NotAssessable) continue;
                    const std::optional<MeasureRow> m = rowReadingOf(*r, mid);
                    if (!m || !std::isfinite(m->value)) continue;
                    ++read;
                    if (m->fired) ++fired;
                }
                if (fired > bestFired || (fired == bestFired && read > bestRead)) {
                    bestFired = fired; bestRead = read; best = mid;
                }
                tabs.append(QVariantMap{
                    { QStringLiteral("id"),    mid },
                    { QStringLiteral("label"), measureLabelOf(mid) },
                    { QStringLiteral("fired"), fired },
                    { QStringLiteral("read"),  read },
                    { QStringLiteral("countText"),
                      read == 0 ? QStringLiteral("not read")
                                : QStringLiteral("fired %1 of %2").arg(fired).arg(read) },
                });
            }
            measureId = ms.contains(m_detailMeasureId) ? m_detailMeasureId : best;
            out[QStringLiteral("measures")] = tabs;
            header[QStringLiteral("spread")] = spreadFor(*l, fi, selectedTick, measureId);
            out[QStringLiteral("header")]    = header;
        }
    }
    out[QStringLiteral("measureId")]     = measureId;
    out[QStringLiteral("measurePicked")] = m_detailMeasureId;   // the reader's pick, for the nav save
    // WHAT CHANGED (posture_metrics_design.md §8): a composite condition's page opens on its parts, read
    // off the swing the page is about. Empty for every other condition, and on a shot it was not read.
    {
        const ConditionRow *fr = (l && fi >= 0 && fi < int(m_shots.size())) ? rowFor(m_shots[size_t(fi)], conditionId) : nullptr;
        const QVariantMap wc = fr ? whatChangedOf(*fr) : QVariantMap();
        out[QStringLiteral("whatChanged")] = wc;
        out[QStringLiteral("composite")]   = !wc.isEmpty() || (l && readsComposite(*l, measureId.isEmpty() ? l->drivingMeasureId : measureId));
        if (!wc.isEmpty())
            out[QStringLiteral("whatChangedSwingDir")] = m_swingDirs.value(m_shots[size_t(fi)].shotId);
    }
    out[QStringLiteral("history")] = (m_reviewing && l) ? historyFor(*l, fi, measureId) : QVariantMap();
    // The two absences, in words, so the panel never has to invent a sentence for an empty list.
    out[QStringLiteral("noCausesLine")] =
        causes.isEmpty() ? QStringLiteral("The model authors no cause above this condition.")
                         : QString();
    out[QStringLiteral("noEffectsLine")] =
        effects.isEmpty() ? QStringLiteral("The model authors nothing downstream of this condition.")
                          : QString();
    return out;
}

QStringList SessionDiagnosticsModel::conditionMeasures(const QString &conditionId) const
{
    QStringList out;
    if (!m_packProv) return out;
    const CharacteristicPack &pack = m_packProv->pack();
    const Condition *c = pack.condition(conditionId);
    if (!c) return out;
    for (const QString &sid : c->detectedBy)
        if (const Signal *sig = pack.signal(sid)) {
            const QString mid = sig->measures.value(0);     // the driving measure, as the engine reads it
            if (!mid.isEmpty() && !out.contains(mid)) out << mid;
        }
    return out;
}

void SessionDiagnosticsModel::setDetailMeasure(const QString &measureId)
{
    if (m_detailConditionId.isEmpty() || measureId == m_detailMeasureId) return;
    if (!measureId.isEmpty() && !conditionMeasures(m_detailConditionId).contains(measureId)) return;
    m_detailMeasureId = measureId;
    buildDetail();
    emit detailChanged();
}

void SessionDiagnosticsModel::buildDetail()
{
    m_detail = m_detailConditionId.isEmpty() ? QVariantMap()
                                             : conditionDetail(m_detailConditionId);
}

void SessionDiagnosticsModel::openDetail(const QString &conditionId)
{
    if (conditionId.isEmpty()) { closeDetail(); return; }
    // DECLINED SILENTLY for a condition the pack does not author: the panel reads the state back
    // off this object, so a request the model does not accept leaves the surface as it was rather
    // than opening an empty page. (declareFocus() does not validate: an unknown focus is recorded
    // and matches nothing.)
    if (!m_packProv || !m_packProv->pack().condition(conditionId)) return;
    if (m_detailConditionId == conditionId) return;
    m_detailConditionId = conditionId;
    m_detailMeasureId.clear();
    buildDetail();
    emit detailChanged();
}

void SessionDiagnosticsModel::closeDetail()
{
    if (m_detailConditionId.isEmpty()) return;
    m_detailConditionId.clear();
    m_detailMeasureId.clear();
    m_detail = QVariantMap();
    emit detailChanged();
}
