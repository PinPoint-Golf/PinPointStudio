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

#include "../../Diagnostics/content/drill_pack.h"
#include "../../Diagnostics/content/gate_words_pack.h"
#include "../../Diagnostics/model/measure_sample.h"
#include "gate_counts.h"   // ModelGateCounts and the count's door to the session indexes (platform refactor P-3b)
#include "../../Diagnostics/content/norm_provider.h"
#include "../../Diagnostics/content/pack_provider.h"
#include "../../Diagnostics/content/screen_pack.h"
#include "../../Vocabulary/Metrics/metric_catalogue.h"

#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <limits>
#include <memory>
#include <vector>

// Forward-declared rather than included: only the two layout marshallers below name it, and both
// are private. dag_layout.h is a heavy header for a signature.
namespace pinpoint::analysis { struct DagLayout; }
// The packs and norms' owner and its snapshot (src/DomainModel/rule_data.h; refactor step 3.9).
namespace pinpoint { class RuleData; struct RuleDataSnapshot; }

// ModelBrowser — the whole Diagnostic Model panel's model: one connected content graph, read and
// edited through one object.
//
// ── Reuse; do not reimplement ───────────────────────────────────────────────────────────────────
//
// The brief's rule, obeyed where it counts: every RULE here is a call into the layer that owns it —
// causesOf/effectsOf/coverageOf/hasCausalPath and tailsOfAxis (characteristic_pack.h), validatePack,
// diagnosticsHealth, layoutDag, measureDisplayLabel, the shared screen/drill/reference registries,
// and the enum label tables. What is reimplemented is MARSHALLING, and only marshalling.
//
// It replaced an earlier façade that read the pack AS SAVED, which is correct for a read-only panel
// and fatal for this one. Editing here accumulates in an unsaved WORKING COPY of the user pack (rule
// 8: "Save is once, not per field"), and every surface has to show the library as it would be if you
// saved now — the table, the inspector, the graph, and above all the validation strip, which is
// worthless if it grades the file rather than the draft.
//
// ── The layering ────────────────────────────────────────────────────────────────────────────────
//
//   m_core        the shipped pack, read-only, and the ONLY way to answer "does this ship?"
//   m_savedUser   the user pack as it is on disk — the baseline every dirty mark is measured from
//   m_working     the user pack as edited. Unsaved.
//   m_assembled   core + memory(m_working). Rebuilt after every mutation; everything reads this.
//
// The NORM set is layered identically — m_savedNorms / m_workingNorms / m_norms — because corridors
// are edited here too, and ONE undo history spans both registries. A command therefore snapshots
// both packs, and one Save writes both files. Two histories was the alternative and it was rejected
// as too complex to reason about: an author who edits a characteristic, then its corridor, then hits
// ⌘Z twice has one mental model of what should happen, not two.
//
// Copy-on-write falls out of the layering rather than being coded: editing a field of a SHIPPED
// object copies that object into m_working, and undoing that first edit removes the copy — which is
// exactly the reset, with no separate "take theirs" path to keep in step. That is ADDENDUM-01's
// third point, satisfied structurally.
//
// ── QML holds no rules ──────────────────────────────────────────────────────────────────────────
//
// Sorting, filtering, faceting, search, legality of a proposed link, which cell may be edited and
// with what control, and which of two values is "yours" — all decided here. QML is handed columns()
// and rows() and renders them; it does not know what a Measure is. A rule written in a delegate is
// a rule nothing can test.

// HOW OFTEN EACH GATE FIRED ACROSS THE LIBRARY (trust_and_gates_design.md §7.5): ModelGateCounts, gate_counts.h — the sum
// over every athlete's session index, each session's summary carrying its own counts (platform refactor P-3b).

// ── The editor's QML API, typed (refactor step 3.9-ii; P9, R-QML-2; DEC-177) ──────────────────────
//
// The values ModelBrowser hands QML, one Q_GADGET each, every field declared. QML reads them by the
// same names the maps it was handed before carried, so a binding reads `r.ok`, `row.label` exactly as
// it did. One difference QML has to know: a field a map left OUT is here a default — "" for a string,
// false, 0 — so a test of absence (`x.id === undefined`) is a test of the default instead.
//
// A list is a QList of the gadget, which QML reads as a sequence (`length`, `[i]`, `modelData`). A
// QVariant member (a cell's or a field's stored `value`) is invalid where the map had no such key, and
// reads `undefined` in QML as the absent key did.
//
// What QML passes IN — a rows() query, the graph's and the corridor plot's options — is a
// QML_STRUCTURED_VALUE gadget, so QML still writes a plain object (`{ sort: "name", ids: [id] }`); a key
// left out keeps the gadget's default, which for the layout metrics is NaN ("not given").

// The answer every write gives: whether it landed and, in the author's own terms, what happened or
// why not. The other fields are set only by the writes that have them to say.
struct ModelEditResult
{
    Q_GADGET
    QML_VALUE_TYPE(modelEditResult)
    Q_PROPERTY(bool        ok       MEMBER ok       FINAL)
    Q_PROPERTY(QString     message  MEMBER message  FINAL)
    // duplicate(), createObject(), createAndAttach(), addCorridor(), mintMeasure(): the new object,
    // so the panel can select it.
    Q_PROPERTY(QString     type     MEMBER type     FINAL)
    Q_PROPERTY(QString     id       MEMBER id       FINAL)
    // addLink(), repointCause(), createAndAttach() of a cause: the claim made.
    Q_PROPERTY(QString     edgeId   MEMBER edgeId   FINAL)
    // resetToStandard(): the backup files written (RuleData::backupUserLayers).
    Q_PROPERTY(QStringList backups  MEMBER backups  FINAL)
    // setBinding(): the narrower rows the write cleared.
    Q_PROPERTY(int         cascaded MEMBER cascaded FINAL)
    // exportRoadmap(), exportReferences(): the file written.
    Q_PROPERTY(QString     path     MEMBER path     FINAL)
public:
    bool        ok = false;
    QString     message;
    QString     type;
    QString     id;
    QString     edgeId;
    QStringList backups;
    int         cascaded = 0;
    QString     path;

    static ModelEditResult accepted(const QString &message) { ModelEditResult r; r.ok = true; r.message = message; return r; }
    static ModelEditResult refused(const QString &message)  { ModelEditResult r; r.message = message; return r; }

    friend bool operator==(const ModelEditResult &, const ModelEditResult &) = default;
};

// One row a picker offers: an object that may legally be linked, attached or added. `detail` is the
// right-hand words; `tone` (measureCandidates) and `reachLabel` (linkCandidates) are set by the lists
// that have them.
struct ModelCandidate
{
    Q_GADGET
    QML_VALUE_TYPE(modelCandidate)
    Q_PROPERTY(QString id         MEMBER id         FINAL)
    Q_PROPERTY(QString label      MEMBER label      FINAL)
    Q_PROPERTY(QString detail     MEMBER detail     FINAL)
    Q_PROPERTY(QString tone       MEMBER tone       FINAL)
    Q_PROPERTY(QString reachLabel MEMBER reachLabel FINAL)
public:
    QString id;
    QString label;
    QString detail;
    QString tone;
    QString reachLabel;

    friend bool operator==(const ModelCandidate &, const ModelCandidate &) = default;
};

// One choice of a closed vocabulary: the stored token and its words. `current` marks the one the
// object holds (ringValues() only).
struct ModelOption
{
    Q_GADGET
    QML_VALUE_TYPE(modelOption)
    Q_PROPERTY(QString value   MEMBER value   FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    QString value;
    QString label;
    bool    current = false;

    friend bool operator==(const ModelOption &, const ModelOption &) = default;
};

// One content type on the rail: its key, what a collection and ONE of it are called, its hint, and
// how many rows it has.
struct ModelTypeEntry
{
    Q_GADGET
    QML_VALUE_TYPE(modelTypeEntry)
    Q_PROPERTY(QString key   MEMBER key   FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(QString one   MEMBER one   FINAL)
    Q_PROPERTY(QString hint  MEMBER hint  FINAL)
    Q_PROPERTY(int     count MEMBER count FINAL)
public:
    QString key;
    QString label;
    QString one;
    QString hint;
    int     count = 0;

    friend bool operator==(const ModelTypeEntry &, const ModelTypeEntry &) = default;
};

// One command on the undo stack, as the Edits list shows it.
struct ModelEditEntry
{
    Q_GADGET
    QML_VALUE_TYPE(modelEditEntry)
    Q_PROPERTY(int     index  MEMBER index  FINAL)
    Q_PROPERTY(QString label  MEMBER label  FINAL)
    Q_PROPERTY(QString detail MEMBER detail FINAL)
    Q_PROPERTY(bool    undone MEMBER undone FINAL)
    Q_PROPERTY(bool    saved  MEMBER saved  FINAL)
public:
    int     index = 0;
    QString label;
    QString detail;
    bool    undone = false;
    bool    saved  = false;

    friend bool operator==(const ModelEditEntry &, const ModelEditEntry &) = default;
};

// One entry of a table's row-dot legend: the tone a dot wears and what it means.
struct ModelLegendEntry
{
    Q_GADGET
    QML_VALUE_TYPE(modelLegendEntry)
    Q_PROPERTY(QString tone  MEMBER tone  FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
public:
    QString tone;
    QString label;

    friend bool operator==(const ModelLegendEntry &, const ModelLegendEntry &) = default;
};

// One grade policy (SET-15) the pack can be read under: its stored name, its word and its hint.
struct ModelGradePolicy
{
    Q_GADGET
    QML_VALUE_TYPE(modelGradePolicy)
    Q_PROPERTY(QString name  MEMBER name  FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(QString hint  MEMBER hint  FINAL)
public:
    QString name;
    QString label;
    QString hint;

    friend bool operator==(const ModelGradePolicy &, const ModelGradePolicy &) = default;
};

// One norm set the corridors resolve through.
struct ModelNormSet
{
    Q_GADGET
    QML_VALUE_TYPE(modelNormSet)
    Q_PROPERTY(QString id        MEMBER id        FINAL)
    Q_PROPERTY(QString label     MEMBER label     FINAL)
    Q_PROPERTY(int     normCount MEMBER normCount FINAL)
    Q_PROPERTY(bool    readOnly  MEMBER readOnly  FINAL)
    Q_PROPERTY(bool    shipped   MEMBER shipped   FINAL)
public:
    QString id;
    QString label;
    int     normCount = 0;
    bool    readOnly  = false;
    bool    shipped   = false;

    friend bool operator==(const ModelNormSet &, const ModelNormSet &) = default;
};

// An object by type and id: where the graph stands (graphFocus()).
struct ModelObjectRef
{
    Q_GADGET
    QML_VALUE_TYPE(modelObjectRef)
    Q_PROPERTY(QString type MEMBER type FINAL)
    Q_PROPERTY(QString id   MEMBER id   FINAL)
public:
    QString type;
    QString id;

    friend bool operator==(const ModelObjectRef &, const ModelObjectRef &) = default;
};

// Why one candidate end of a proposed link is refused: `reason` is self · exists · cycle, for the view
// to key a tone off; `text` is the words to show. Empty `reason`: not refused.
struct ModelLinkRefusal
{
    Q_GADGET
    QML_VALUE_TYPE(modelLinkRefusal)
    Q_PROPERTY(QString reason MEMBER reason FINAL)
    Q_PROPERTY(QString text   MEMBER text   FINAL)
public:
    QString reason;
    QString text;

    friend bool operator==(const ModelLinkRefusal &, const ModelLinkRefusal &) = default;
};

// Every refusal for one armed link drag (linkRefusals()), looked up by candidate id. Only refused ids
// are held; refusalOf() an id that is legal answers an empty refusal.
struct ModelLinkRefusals
{
    Q_GADGET
    QML_VALUE_TYPE(modelLinkRefusals)
    Q_PROPERTY(int count READ count FINAL)
public:
    QHash<QString, ModelLinkRefusal> byId;

    int                          count() const { return int(byId.size()); }
    Q_INVOKABLE bool             refuses(const QString &id) const { return byId.contains(id); }
    Q_INVOKABLE ModelLinkRefusal refusalOf(const QString &id) const { return byId.value(id); }
};

// One context a measure could carry a corridor at, marked with what already resolves there.
// `mu` and `unit` are the resolving corridor's (0 and "" when `found` is false).
struct ModelCorridorContext
{
    Q_GADGET
    QML_VALUE_TYPE(modelCorridorContext)
    Q_PROPERTY(QString id            MEMBER id            FINAL)
    Q_PROPERTY(QString label         MEMBER label         FINAL)
    Q_PROPERTY(int     depth         MEMBER depth         FINAL)
    Q_PROPERTY(bool    found         MEMBER found         FINAL)
    Q_PROPERTY(bool    own           MEMBER own           FINAL)
    Q_PROPERTY(bool    inherited     MEMBER inherited     FINAL)
    Q_PROPERTY(QString inheritedFrom MEMBER inheritedFrom FINAL)
    Q_PROPERTY(double  mu            MEMBER mu            FINAL)
    Q_PROPERTY(QString unit          MEMBER unit          FINAL)
public:
    QString id;
    QString label;
    int     depth = 0;
    bool    found = false;
    bool    own   = false;
    bool    inherited = false;
    QString inheritedFrom;
    double  mu = 0.0;
    QString unit;

    friend bool operator==(const ModelCorridorContext &, const ModelCorridorContext &) = default;
};

// What a typed phrase seeds the mint's chips with. Empty fields: the phrase named nothing for them.
struct ModelFacetSeed
{
    Q_GADGET
    QML_VALUE_TYPE(modelFacetSeed)
    Q_PROPERTY(QString what      MEMBER what      FINAL)
    Q_PROPERTY(QString quantity  MEMBER quantity  FINAL)
    Q_PROPERTY(QString reference MEMBER reference FINAL)
public:
    QString what;
    QString quantity;
    QString reference;

    friend bool operator==(const ModelFacetSeed &, const ModelFacetSeed &) = default;
};

// The mint's live preview of the measure its facets name: valid or why not, its generated id and
// label, the measure it already is (`hasExactMatch`, `exactMatch`) and those one facet away.
struct ModelMeasurePreview
{
    Q_GADGET
    QML_VALUE_TYPE(modelMeasurePreview)
    Q_PROPERTY(bool                  valid          MEMBER valid          FINAL)
    Q_PROPERTY(QString               reason         MEMBER reason         FINAL)
    Q_PROPERTY(QString               id             MEMBER id             FINAL)
    Q_PROPERTY(QString               label          MEMBER label          FINAL)
    Q_PROPERTY(bool                  hasExactMatch  MEMBER hasExactMatch  FINAL)
    Q_PROPERTY(ModelCandidate        exactMatch     MEMBER exactMatch     FINAL)
    Q_PROPERTY(QList<ModelCandidate> nearDuplicates MEMBER nearDuplicates FINAL)
public:
    bool                  valid = false;
    QString               reason;
    QString               id;
    QString               label;
    bool                  hasExactMatch = false;
    ModelCandidate        exactMatch;
    QList<ModelCandidate> nearDuplicates;

    friend bool operator==(const ModelMeasurePreview &, const ModelMeasurePreview &) = default;
};

// ── The table: columns(), rows(), facets(), searchAll() (refactor step 3.9-ii, second run) ──────────
//
// The table delegate renders any type without knowing which one it has: a row's `cells` are parallel to
// the type's columns().

// One column of a type's table. `width` is in unscaled design pixels (QML runs it through Theme.sp());
// `flex` marks the one column that takes the slack — the name column, which is what gets starved.
struct ModelColumn
{
    Q_GADGET
    QML_VALUE_TYPE(modelColumn)
    Q_PROPERTY(QString key      MEMBER key      FINAL)
    Q_PROPERTY(QString title    MEMBER title    FINAL)
    Q_PROPERTY(int     width    MEMBER width    FINAL)
    Q_PROPERTY(bool    flex     MEMBER flex     FINAL)
    Q_PROPERTY(QString align    MEMBER align    FINAL)
    Q_PROPERTY(bool    mono     MEMBER mono     FINAL)
    Q_PROPERTY(bool    sortable MEMBER sortable FINAL)
public:
    QString key;
    QString title;
    int     width = 0;
    bool    flex = false;
    QString align = QStringLiteral("left");
    bool    mono = false;
    bool    sortable = true;

    friend bool operator==(const ModelColumn &, const ModelColumn &) = default;
};

// One table cell. `kind` is what the EDITOR is, not what the value is: "text" types in place, "enum"
// opens a one-click list of `options`, "number" is a numeric field, "none" cannot be edited. An editable
// cell carries the `field` it writes and the stored `value` (invalid, so `undefined` in QML, on an inert
// cell). `own` says the value is the user's own rather than the shipped one.
struct ModelCell
{
    Q_GADGET
    QML_VALUE_TYPE(modelCell)
    Q_PROPERTY(QString            text     MEMBER text     FINAL)
    Q_PROPERTY(QString            tone     MEMBER tone     FINAL)
    Q_PROPERTY(bool               mono     MEMBER mono     FINAL)
    Q_PROPERTY(QString            align    MEMBER align    FINAL)
    Q_PROPERTY(QString            kind     MEMBER kind     FINAL)
    Q_PROPERTY(bool               editable MEMBER editable FINAL)
    Q_PROPERTY(bool               own      MEMBER own      FINAL)
    Q_PROPERTY(QString            field    MEMBER field    FINAL)
    Q_PROPERTY(QVariant           value    MEMBER value    FINAL)
    Q_PROPERTY(QList<ModelOption> options  MEMBER options  FINAL)
public:
    QString            text;
    QString            tone;
    bool               mono = false;
    QString            align = QStringLiteral("left");
    QString            kind = QStringLiteral("none");
    bool               editable = false;
    bool               own = false;
    QString            field;
    QVariant           value;
    QList<ModelOption> options;

    friend bool operator==(const ModelCell &, const ModelCell &) = default;
};

// One row of a type's table, or of the cross-type search result (searchAll(), which sets `resultType`).
// The other fields are set by the types that have them to say: a link's two ends (`fromId`, `toId`),
// a corridor's measure and context, a gate's `reading` flag, a health finding's `subject`, `code` and
// `severity`.
struct ModelRow
{
    Q_GADGET
    QML_VALUE_TYPE(modelRow)
    Q_PROPERTY(QString          id          MEMBER id          FINAL)
    Q_PROPERTY(QString          type        MEMBER type        FINAL)
    Q_PROPERTY(QString          label       MEMBER label       FINAL)
    // The row dot's tone name; empty: no dot.
    Q_PROPERTY(QString          dot         MEMBER dot         FINAL)
    Q_PROPERTY(bool             dirty       MEMBER dirty       FINAL)
    // shipped · yours · both, and its words
    Q_PROPERTY(QString          source      MEMBER source      FINAL)
    Q_PROPERTY(QString          sourceLabel MEMBER sourceLabel FINAL)
    Q_PROPERTY(QList<ModelCell> cells       MEMBER cells       FINAL)
    Q_PROPERTY(QString          resultType  MEMBER resultType  FINAL)
    Q_PROPERTY(QString          fromId      MEMBER fromId      FINAL)
    Q_PROPERTY(QString          toId        MEMBER toId        FINAL)
    Q_PROPERTY(QString          measureId   MEMBER measureId   FINAL)
    Q_PROPERTY(QString          contextId   MEMBER contextId   FINAL)
    Q_PROPERTY(bool             reading     MEMBER reading     FINAL)
    Q_PROPERTY(QString          subject     MEMBER subject     FINAL)
    Q_PROPERTY(QString          code        MEMBER code        FINAL)
    Q_PROPERTY(QString          severity    MEMBER severity    FINAL)
public:
    QString          id;
    QString          type;
    QString          label;
    QString          dot;
    bool             dirty = false;
    QString          source;
    QString          sourceLabel;
    QList<ModelCell> cells;
    QString          resultType;
    QString          fromId;
    QString          toId;
    QString          measureId;
    QString          contextId;
    bool             reading = false;
    QString          subject;
    QString          code;
    QString          severity;
    // C++ only, not on the QML boundary: what rows() sorts, filters and facets on, and the words a search
    // matches. Sort keys are typed per key (an ordinal, a count, a label) and compared as such.
    QVariantMap sortKeys;
    QString     searchText;

    friend bool operator==(const ModelRow &, const ModelRow &) = default;
};

// One value of a facet, and how many of the type's rows hold it. A quantile bucket also carries its
// range: rows whose count is in [lo, hi] (`hasHi` false: no upper end).
struct ModelFacetOption
{
    Q_GADGET
    QML_VALUE_TYPE(modelFacetOption)
    Q_PROPERTY(QString value MEMBER value FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(int     count MEMBER count FINAL)
    Q_PROPERTY(int     lo    MEMBER lo    FINAL)
    Q_PROPERTY(int     hi    MEMBER hi    FINAL)
    Q_PROPERTY(bool    hasHi MEMBER hasHi FINAL)
public:
    QString value;
    QString label;
    int     count = 0;
    int     lo = 0;
    int     hi = 0;
    bool    hasHi = false;

    friend bool operator==(const ModelFacetOption &, const ModelFacetOption &) = default;
};

// One facet of the rail under the type list. `kind` is value (a column's text), tags (a list per row) or
// quantile (a count bucketed by its spread; `max` is the largest count).
struct ModelFacet
{
    Q_GADGET
    QML_VALUE_TYPE(modelFacet)
    Q_PROPERTY(QString                 key     MEMBER key     FINAL)
    Q_PROPERTY(QString                 label   MEMBER label   FINAL)
    Q_PROPERTY(QString                 kind    MEMBER kind    FINAL)
    Q_PROPERTY(int                     max     MEMBER max     FINAL)
    Q_PROPERTY(QList<ModelFacetOption> options MEMBER options FINAL)
public:
    QString                 key;
    QString                 label;
    QString                 kind;
    int                     max = 0;
    QList<ModelFacetOption> options;

    friend bool operator==(const ModelFacet &, const ModelFacet &) = default;
};

// The values picked on one facet, as rows() is asked for them. QML passes a plain object
// `{ key, values }` (a structured value).
struct ModelFacetPick
{
    Q_GADGET
    QML_VALUE_TYPE(modelFacetPick)
    QML_STRUCTURED_VALUE
    Q_PROPERTY(QString     key    MEMBER key    FINAL)
    Q_PROPERTY(QStringList values MEMBER values FINAL)
public:
    QString     key;
    QStringList values;

    friend bool operator==(const ModelFacetPick &, const ModelFacetPick &) = default;
};

// What rows() is asked for: a search, a sort (a column key; empty is the type's default sort) and its
// direction, the facet picks, and the ids to keep (empty: all). QML passes a plain object with any of
// these keys (a structured value); a key left out keeps its default.
struct ModelRowQuery
{
    Q_GADGET
    QML_VALUE_TYPE(modelRowQuery)
    QML_STRUCTURED_VALUE
    Q_PROPERTY(QString               search     MEMBER search     FINAL)
    Q_PROPERTY(QString               sort       MEMBER sort       FINAL)
    Q_PROPERTY(bool                  descending MEMBER descending FINAL)
    Q_PROPERTY(QList<ModelFacetPick> facets     MEMBER facets     FINAL)
    Q_PROPERTY(QStringList           ids        MEMBER ids        FINAL)
public:
    QString               search;
    QString               sort;
    bool                  descending = false;
    QList<ModelFacetPick> facets;
    QStringList           ids;

    friend bool operator==(const ModelRowQuery &, const ModelRowQuery &) = default;
};

// ── The inspector: inspect(), fieldIssues() ────────────────────────────────────────────────────

// What the pack loader would say about one field's words: its code, "error" | "warning", and the
// message (the same strings Health shows).
struct ModelFieldIssue
{
    Q_GADGET
    QML_VALUE_TYPE(modelFieldIssue)
    Q_PROPERTY(QString code     MEMBER code     FINAL)
    Q_PROPERTY(QString severity MEMBER severity FINAL)
    Q_PROPERTY(QString message  MEMBER message  FINAL)
public:
    QString code;
    QString severity;
    QString message;

    friend bool operator==(const ModelFieldIssue &, const ModelFieldIssue &) = default;
};

// One row of an inspector section, or one badge. Every row is the same shape: a related object one
// click away (`type`, `id`, `navigable`), with its words (`label`, `detail`) and `tone`. A FIELD row also
// carries the `field` it writes, its editor `kind`, its stored `value` (invalid on a non-field row) and
// its `options`; a citation field the reference it resolves to (`linkType`, `linkId`, `linkLabel`); a
// report field its `group` ("report"), `wordingChecked` (the façade checks its words as they are typed)
// and `issues`. A binding row says where a characteristic applies (`depth`, `own`, `applicable`,
// `material`); a measure row the swing's value (`pill`, `pillTone`, `pillHint`); a claim row on a
// reference the link's `relation` and editable `strength` (its words `strengthLabel`, its `options`).
struct ModelInspectorRow
{
    Q_GADGET
    QML_VALUE_TYPE(modelInspectorRow)
    Q_PROPERTY(QString                type           MEMBER type           FINAL)
    Q_PROPERTY(QString                id             MEMBER id             FINAL)
    Q_PROPERTY(QString                label          MEMBER label          FINAL)
    Q_PROPERTY(QString                detail         MEMBER detail         FINAL)
    Q_PROPERTY(QString                tone           MEMBER tone           FINAL)
    Q_PROPERTY(bool                   navigable      MEMBER navigable      FINAL)
    Q_PROPERTY(QString                field          MEMBER field          FINAL)
    Q_PROPERTY(QString                kind           MEMBER kind           FINAL)
    Q_PROPERTY(QVariant               value          MEMBER value          FINAL)
    Q_PROPERTY(QList<ModelOption>     options        MEMBER options        FINAL)
    Q_PROPERTY(QString                linkType       MEMBER linkType       FINAL)
    Q_PROPERTY(QString                linkId         MEMBER linkId         FINAL)
    Q_PROPERTY(QString                linkLabel      MEMBER linkLabel      FINAL)
    Q_PROPERTY(QString                group          MEMBER group          FINAL)
    Q_PROPERTY(bool                   wordingChecked MEMBER wordingChecked FINAL)
    Q_PROPERTY(QList<ModelFieldIssue> issues         MEMBER issues         FINAL)
    Q_PROPERTY(int                    depth          MEMBER depth          FINAL)
    Q_PROPERTY(bool                   own            MEMBER own            FINAL)
    Q_PROPERTY(bool                   applicable     MEMBER applicable     FINAL)
    Q_PROPERTY(bool                   material       MEMBER material       FINAL)
    Q_PROPERTY(QString                pill           MEMBER pill           FINAL)
    Q_PROPERTY(QString                pillTone       MEMBER pillTone       FINAL)
    Q_PROPERTY(QString                pillHint       MEMBER pillHint       FINAL)
    Q_PROPERTY(QString                relation       MEMBER relation       FINAL)
    Q_PROPERTY(QString                strength       MEMBER strength       FINAL)
    Q_PROPERTY(QString                strengthLabel  MEMBER strengthLabel  FINAL)
public:
    QString                type;
    QString                id;
    QString                label;
    QString                detail;
    QString                tone;
    bool                   navigable = true;
    QString                field;
    QString                kind;
    QVariant               value;
    QList<ModelOption>     options;
    QString                linkType;
    QString                linkId;
    QString                linkLabel;
    QString                group;
    bool                   wordingChecked = false;
    QList<ModelFieldIssue> issues;
    int                    depth = 0;
    bool                   own = false;
    bool                   applicable = false;
    bool                   material = false;
    QString                pill;
    QString                pillTone;
    QString                pillHint;
    QString                relation;
    QString                strength;
    QString                strengthLabel;

    friend bool operator==(const ModelInspectorRow &, const ModelInspectorRow &) = default;
};

// One section of the inspector: its `title`, how it is drawn (`kind`: list, fields, prose, quote,
// actions, claims, bindings, …), a `note` under it, its `rows` and their `count`, and the `action` its
// add affordance names (empty: none).
struct ModelInspectorSection
{
    Q_GADGET
    QML_VALUE_TYPE(modelInspectorSection)
    Q_PROPERTY(QString                  title  MEMBER title  FINAL)
    Q_PROPERTY(QString                  kind   MEMBER kind   FINAL)
    Q_PROPERTY(QString                  note   MEMBER note   FINAL)
    Q_PROPERTY(QList<ModelInspectorRow> rows   MEMBER rows   FINAL)
    Q_PROPERTY(int                      count  MEMBER count  FINAL)
    Q_PROPERTY(QString                  action MEMBER action FINAL)
public:
    QString                  title;
    QString                  kind = QStringLiteral("list");
    QString                  note;
    QList<ModelInspectorRow> rows;
    int                      count = 0;
    QString                  action;

    friend bool operator==(const ModelInspectorSection &, const ModelInspectorSection &) = default;
};

// The inspector for one object: a RELATIONSHIP HUB, not a property sheet. `found` false: the id resolves
// to nothing (only `type` and `id` are set). `metricKey` is a measure's metric; `source` and `dirty` say
// whose content it is and whether it is unsaved.
struct ModelInspection
{
    Q_GADGET
    QML_VALUE_TYPE(modelInspection)
    Q_PROPERTY(bool                         found     MEMBER found     FINAL)
    Q_PROPERTY(QString                      type      MEMBER type      FINAL)
    Q_PROPERTY(QString                      id        MEMBER id        FINAL)
    Q_PROPERTY(QString                      label     MEMBER label     FINAL)
    Q_PROPERTY(QString                      eyebrow   MEMBER eyebrow   FINAL)
    Q_PROPERTY(QString                      subtitle  MEMBER subtitle  FINAL)
    Q_PROPERTY(QList<ModelInspectorRow>     badges    MEMBER badges    FINAL)
    Q_PROPERTY(QList<ModelInspectorSection> sections  MEMBER sections  FINAL)
    Q_PROPERTY(QString                      metricKey MEMBER metricKey FINAL)
    Q_PROPERTY(QString                      source    MEMBER source    FINAL)
    Q_PROPERTY(bool                         dirty     MEMBER dirty     FINAL)
public:
    bool                         found = false;
    QString                      type;
    QString                      id;
    QString                      label;
    QString                      eyebrow;
    QString                      subtitle;
    QList<ModelInspectorRow>     badges;
    QList<ModelInspectorSection> sections;
    QString                      metricKey;
    QString                      source;
    bool                         dirty = false;

    friend bool operator==(const ModelInspection &, const ModelInspection &) = default;
};

// ── The graph: graph() (and the C++-only dag()) ────────────────────────────────────────────────
//
// Every coordinate is dag_layout.h's; QML positions nothing.

// A measure drawn as a row INSIDE its condition's box, at absolute y.
struct ModelGraphMeasure
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraphMeasure)
    Q_PROPERTY(QString     id                MEMBER id                FINAL)
    Q_PROPERTY(QString     label             MEMBER label             FINAL)
    Q_PROPERTY(QString     statusLabel       MEMBER statusLabel       FINAL)
    Q_PROPERTY(QString     metricKey         MEMBER metricKey         FINAL)
    Q_PROPERTY(QString     detail            MEMBER detail            FINAL)
    Q_PROPERTY(QStringList keyLadder         MEMBER keyLadder         FINAL)
    Q_PROPERTY(bool        available         MEMBER available         FINAL)
    Q_PROPERTY(QString     unavailableReason MEMBER unavailableReason FINAL)
    Q_PROPERTY(double      y                 MEMBER y                 FINAL)
    Q_PROPERTY(double      h                 MEMBER h                 FINAL)
public:
    QString     id;
    QString     label;
    QString     statusLabel;
    QString     metricKey;
    QString     detail;
    QStringList keyLadder;
    bool        available = true;
    QString     unavailableReason;
    double      y = 0.0;
    double      h = 0.0;

    friend bool operator==(const ModelGraphMeasure &, const ModelGraphMeasure &) = default;
};

// A citation drawn as a row inside its box. `id` is empty when the citation resolves to nothing.
struct ModelGraphReference
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraphReference)
    Q_PROPERTY(QString id          MEMBER id          FINAL)
    Q_PROPERTY(QString citation    MEMBER citation    FINAL)
    Q_PROPERTY(QString label       MEMBER label       FINAL)
    Q_PROPERTY(QString detailLabel MEMBER detailLabel FINAL)
    Q_PROPERTY(bool    resolved    MEMBER resolved    FINAL)
    Q_PROPERTY(double  y           MEMBER y           FINAL)
    Q_PROPERTY(double  h           MEMBER h           FINAL)
public:
    QString id;
    QString citation;
    QString label;
    QString detailLabel;
    bool    resolved = true;
    double  y = 0.0;
    double  h = 0.0;

    friend bool operator==(const ModelGraphReference &, const ModelGraphReference &) = default;
};

// One box. The layout's fields (dag_layout.h's DagNode), then the decoration graph() adds: the object's
// type (`nodeType`) and `glyph`, whose content it is (`source`), `ubiquitous` (a condition almost everyone
// shows) and a measure's `note` (its corridor).
struct ModelGraphNode
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraphNode)
    Q_PROPERTY(QString                    id                MEMBER id                FINAL)
    Q_PROPERTY(QString                    kind              MEMBER kind              FINAL)
    Q_PROPERTY(QString                    label             MEMBER label             FINAL)
    Q_PROPERTY(int                        rank              MEMBER rank              FINAL)
    Q_PROPERTY(double                     x                 MEMBER x                 FINAL)
    Q_PROPERTY(double                     y                 MEMBER y                 FINAL)
    Q_PROPERTY(double                     w                 MEMBER w                 FINAL)
    Q_PROPERTY(double                     h                 MEMBER h                 FINAL)
    Q_PROPERTY(bool                       latent            MEMBER latent            FINAL)
    Q_PROPERTY(bool                       offeredOnly       MEMBER offeredOnly       FINAL)
    Q_PROPERTY(QString                    reach             MEMBER reach             FINAL)
    Q_PROPERTY(QString                    reachLabel        MEMBER reachLabel        FINAL)
    Q_PROPERTY(bool                       available         MEMBER available         FINAL)
    Q_PROPERTY(QString                    unavailableReason MEMBER unavailableReason FINAL)
    Q_PROPERTY(QString                    detection         MEMBER detection         FINAL)
    Q_PROPERTY(QString                    detectionLabel    MEMBER detectionLabel    FINAL)
    Q_PROPERTY(double                     captionH          MEMBER captionH          FINAL)
    Q_PROPERTY(int                        coverage          MEMBER coverage          FINAL)
    Q_PROPERTY(int                        hiddenCauses      MEMBER hiddenCauses      FINAL)
    Q_PROPERTY(int                        hiddenEffects     MEMBER hiddenEffects     FINAL)
    Q_PROPERTY(bool                       expanded          MEMBER expanded          FINAL)
    Q_PROPERTY(QList<ModelGraphMeasure>   measures          MEMBER measures          FINAL)
    Q_PROPERTY(QList<ModelGraphReference> references        MEMBER references        FINAL)
    Q_PROPERTY(QString                    groupLabel        MEMBER groupLabel        FINAL)
    Q_PROPERTY(bool                       dirty             MEMBER dirty             FINAL)
    Q_PROPERTY(QString                    nodeType          MEMBER nodeType          FINAL)
    Q_PROPERTY(QString                    glyph             MEMBER glyph             FINAL)
    Q_PROPERTY(QString                    source            MEMBER source            FINAL)
    Q_PROPERTY(bool                       ubiquitous        MEMBER ubiquitous        FINAL)
    Q_PROPERTY(QString                    note              MEMBER note              FINAL)
public:
    QString                    id;
    QString                    kind;
    QString                    label;
    int                        rank = 0;
    double                     x = 0.0;
    double                     y = 0.0;
    double                     w = 0.0;
    double                     h = 0.0;
    bool                       latent = false;
    bool                       offeredOnly = false;
    QString                    reach;
    QString                    reachLabel;
    bool                       available = true;
    QString                    unavailableReason;
    QString                    detection;
    QString                    detectionLabel;
    double                     captionH = 0.0;
    int                        coverage = 0;
    int                        hiddenCauses = 0;
    int                        hiddenEffects = 0;
    bool                       expanded = false;
    QList<ModelGraphMeasure>   measures;
    QList<ModelGraphReference> references;
    QString                    groupLabel;
    bool                       dirty = false;
    QString                    nodeType;
    QString                    glyph;
    QString                    source;
    bool                       ubiquitous = false;
    QString                    note;

    friend bool operator==(const ModelGraphNode &, const ModelGraphNode &) = default;
};

// One line (dag_layout.h's DagEdge). `rowId` is the Causal links table's id for it — empty when the line
// is not a link of the pack — with that row's `dirty` and `source`.
struct ModelGraphEdge
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraphEdge)
    Q_PROPERTY(QString from        MEMBER from        FINAL)
    Q_PROPERTY(QString to          MEMBER to          FINAL)
    Q_PROPERTY(QString strength    MEMBER strength    FINAL)
    Q_PROPERTY(double  weight      MEMBER weight      FINAL)
    Q_PROPERTY(bool    offeredOnly MEMBER offeredOnly FINAL)
    Q_PROPERTY(QString relation    MEMBER relation    FINAL)
    Q_PROPERTY(bool    symmetric   MEMBER symmetric   FINAL)
    Q_PROPERTY(int     segment     MEMBER segment     FINAL)
    Q_PROPERTY(int     segments    MEMBER segments    FINAL)
    Q_PROPERTY(double  x1          MEMBER x1          FINAL)
    Q_PROPERTY(double  y1          MEMBER y1          FINAL)
    Q_PROPERTY(double  c1x         MEMBER c1x         FINAL)
    Q_PROPERTY(double  c1y         MEMBER c1y         FINAL)
    Q_PROPERTY(double  c2x         MEMBER c2x         FINAL)
    Q_PROPERTY(double  c2y         MEMBER c2y         FINAL)
    Q_PROPERTY(double  x2          MEMBER x2          FINAL)
    Q_PROPERTY(double  y2          MEMBER y2          FINAL)
    Q_PROPERTY(QString label       MEMBER label       FINAL)
    Q_PROPERTY(double  labelX      MEMBER labelX      FINAL)
    Q_PROPERTY(double  labelY      MEMBER labelY      FINAL)
    Q_PROPERTY(bool    tip         MEMBER tip         FINAL)
    Q_PROPERTY(double  tipAx       MEMBER tipAx       FINAL)
    Q_PROPERTY(double  tipAy       MEMBER tipAy       FINAL)
    Q_PROPERTY(double  tipBx       MEMBER tipBx       FINAL)
    Q_PROPERTY(double  tipBy       MEMBER tipBy       FINAL)
    Q_PROPERTY(double  tipCx       MEMBER tipCx       FINAL)
    Q_PROPERTY(double  tipCy       MEMBER tipCy       FINAL)
    Q_PROPERTY(QString rowId       MEMBER rowId       FINAL)
    Q_PROPERTY(bool    dirty       MEMBER dirty       FINAL)
    Q_PROPERTY(QString source      MEMBER source      FINAL)
public:
    QString from;
    QString to;
    QString strength;
    double  weight = 2.0;
    bool    offeredOnly = false;
    QString relation;
    bool    symmetric = false;
    int     segment = 0;
    int     segments = 1;
    double  x1 = 0.0;
    double  y1 = 0.0;
    double  c1x = 0.0;
    double  c1y = 0.0;
    double  c2x = 0.0;
    double  c2y = 0.0;
    double  x2 = 0.0;
    double  y2 = 0.0;
    QString label;
    double  labelX = 0.0;
    double  labelY = 0.0;
    bool    tip = false;
    double  tipAx = 0.0;
    double  tipAy = 0.0;
    double  tipBx = 0.0;
    double  tipBy = 0.0;
    double  tipCx = 0.0;
    double  tipCy = 0.0;
    QString rowId;
    bool    dirty = false;
    QString source;

    friend bool operator==(const ModelGraphEdge &, const ModelGraphEdge &) = default;
};

// A column heading above the band.
struct ModelGraphHeading
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraphHeading)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(double  x     MEMBER x     FINAL)
    Q_PROPERTY(double  y     MEMBER y     FINAL)
    Q_PROPERTY(double  w     MEMBER w     FINAL)
public:
    QString label;
    double  x = 0.0;
    double  y = 0.0;
    double  w = 0.0;

    friend bool operator==(const ModelGraphHeading &, const ModelGraphHeading &) = default;
};

// The laid-out picture around one object: boxes, lines and headings, its bounding box, the centre of
// the focus node and whether a bound cut it short. Empty (no nodes) for an object with no graph.
struct ModelGraph
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraph)
    Q_PROPERTY(QList<ModelGraphNode>    nodes     MEMBER nodes     FINAL)
    Q_PROPERTY(QList<ModelGraphEdge>    edges     MEMBER edges     FINAL)
    Q_PROPERTY(QList<ModelGraphHeading> headings  MEMBER headings  FINAL)
    Q_PROPERTY(double                   width     MEMBER width     FINAL)
    Q_PROPERTY(double                   height    MEMBER height    FINAL)
    Q_PROPERTY(double                   focusX    MEMBER focusX    FINAL)
    Q_PROPERTY(double                   focusY    MEMBER focusY    FINAL)
    Q_PROPERTY(bool                     truncated MEMBER truncated FINAL)
public:
    QList<ModelGraphNode>    nodes;
    QList<ModelGraphEdge>    edges;
    QList<ModelGraphHeading> headings;
    double                   width = 0.0;
    double                   height = 0.0;
    double                   focusX = 0.0;
    double                   focusY = 0.0;
    bool                     truncated = false;

    friend bool operator==(const ModelGraph &, const ModelGraph &) = default;
};

// How graph() is asked to lay out: the pane's metrics (dag_layout.h's DagLayoutOptions, in layout units;
// NaN: not given, so the layout's own default stands), the reader's bounds (`depth`, `maxPerRank`,
// `maxPerExpand`; -1: not given), the boxes opened (`expanded`) and the switches. QML passes a plain
// object (a structured value). The switches' defaults are DagLayoutOptions' (model_browser_test checks).
struct ModelGraphOptions
{
    Q_GADGET
    QML_VALUE_TYPE(modelGraphOptions)
    QML_STRUCTURED_VALUE
    Q_PROPERTY(double      nodeH             MEMBER nodeH             FINAL)
    Q_PROPERTY(double      gapX              MEMBER gapX              FINAL)
    Q_PROPERTY(double      gapY              MEMBER gapY              FINAL)
    Q_PROPERTY(double      rowH              MEMBER rowH              FINAL)
    Q_PROPERTY(double      captionH          MEMBER captionH          FINAL)
    Q_PROPERTY(double      rowCharW          MEMBER rowCharW          FINAL)
    Q_PROPERTY(double      rowMaxW           MEMBER rowMaxW           FINAL)
    Q_PROPERTY(double      padX              MEMBER padX              FINAL)
    Q_PROPERTY(double      charW             MEMBER charW             FINAL)
    Q_PROPERTY(double      minW              MEMBER minW              FINAL)
    Q_PROPERTY(double      maxW              MEMBER maxW              FINAL)
    Q_PROPERTY(int         depth             MEMBER depth             FINAL)
    Q_PROPERTY(int         maxPerRank        MEMBER maxPerRank        FINAL)
    Q_PROPERTY(int         maxPerExpand      MEMBER maxPerExpand      FINAL)
    Q_PROPERTY(QStringList expanded          MEMBER expanded          FINAL)
    Q_PROPERTY(bool        includeMeasures   MEMBER includeMeasures   FINAL)
    Q_PROPERTY(bool        includeReferences MEMBER includeReferences FINAL)
    Q_PROPERTY(bool        includeScreened   MEMBER includeScreened   FINAL)
    Q_PROPERTY(bool        hideWeak          MEMBER hideWeak          FINAL)
    Q_PROPERTY(bool        hideProposed      MEMBER hideProposed      FINAL)
public:
    double      nodeH = std::numeric_limits<double>::quiet_NaN();
    double      gapX = std::numeric_limits<double>::quiet_NaN();
    double      gapY = std::numeric_limits<double>::quiet_NaN();
    double      rowH = std::numeric_limits<double>::quiet_NaN();
    double      captionH = std::numeric_limits<double>::quiet_NaN();
    double      rowCharW = std::numeric_limits<double>::quiet_NaN();
    double      rowMaxW = std::numeric_limits<double>::quiet_NaN();
    double      padX = std::numeric_limits<double>::quiet_NaN();
    double      charW = std::numeric_limits<double>::quiet_NaN();
    double      minW = std::numeric_limits<double>::quiet_NaN();
    double      maxW = std::numeric_limits<double>::quiet_NaN();
    int         depth = -1;
    int         maxPerRank = -1;
    int         maxPerExpand = -1;
    QStringList expanded;
    bool        includeMeasures = true;
    bool        includeReferences = false;
    bool        includeScreened = true;
    bool        hideWeak = false;
    bool        hideProposed = false;
};

// ── The corridor plot: corridorPlot() ──────────────────────────────────────────────────────────

// A point of the plot, in pixels.
struct ModelPlotPoint
{
    Q_GADGET
    QML_VALUE_TYPE(modelPlotPoint)
    Q_PROPERTY(double x MEMBER x FINAL)
    Q_PROPERTY(double y MEMBER y FINAL)
public:
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const ModelPlotPoint &, const ModelPlotPoint &) = default;
};

// One grade band as a region: its grade's WORD (ideal · good · watch · action), so a delegate colours by
// meaning rather than by position, and its x and width in pixels.
struct ModelPlotBand
{
    Q_GADGET
    QML_VALUE_TYPE(modelPlotBand)
    Q_PROPERTY(QString grade MEMBER grade FINAL)
    Q_PROPERTY(double  x     MEMBER x     FINAL)
    Q_PROPERTY(double  w     MEMBER w     FINAL)
public:
    QString grade;
    double  x = 0.0;
    double  w = 0.0;

    friend bool operator==(const ModelPlotBand &, const ModelPlotBand &) = default;
};

// The distribution a norm CLAIMS, drawn over the readings the library holds (corridor_plot.h's layout).
// `found` false: the key resolves to nothing. Then the norm's own numbers (under any drag preview), the
// values the edge handles sit on, the unit's quantum (`step`, `decimals`), the plausibility bounds with
// their presence flags (absent is not zero), and whether the readings are this measure's (`scanned`).
struct ModelCorridorPlot
{
    Q_GADGET
    QML_VALUE_TYPE(modelCorridorPlot)
    Q_PROPERTY(bool                  found          MEMBER found          FINAL)
    Q_PROPERTY(double                width          MEMBER width          FINAL)
    Q_PROPERTY(double                height         MEMBER height         FINAL)
    Q_PROPERTY(double                xMin           MEMBER xMin           FINAL)
    Q_PROPERTY(double                xMax           MEMBER xMax           FINAL)
    Q_PROPERTY(QString               unit           MEMBER unit           FINAL)
    Q_PROPERTY(QList<ModelPlotBand>  bands          MEMBER bands          FINAL)
    Q_PROPERTY(QList<ModelPlotPoint> curve          MEMBER curve          FINAL)
    Q_PROPERTY(QList<ModelPlotPoint> samples        MEMBER samples        FINAL)
    Q_PROPERTY(QList<double>         rug            MEMBER rug            FINAL)
    Q_PROPERTY(double                muX            MEMBER muX            FINAL)
    Q_PROPERTY(double                idealLoX       MEMBER idealLoX       FINAL)
    Q_PROPERTY(double                idealHiX       MEMBER idealHiX       FINAL)
    Q_PROPERTY(bool                  lowOpen        MEMBER lowOpen        FINAL)
    Q_PROPERTY(bool                  highOpen       MEMBER highOpen       FINAL)
    Q_PROPERTY(int                   n              MEMBER n              FINAL)
    Q_PROPERTY(int                   ideal          MEMBER ideal          FINAL)
    Q_PROPERTY(int                   good           MEMBER good           FINAL)
    Q_PROPERTY(int                   watch          MEMBER watch          FINAL)
    Q_PROPERTY(int                   action         MEMBER action         FINAL)
    Q_PROPERTY(int                   implausible    MEMBER implausible    FINAL)
    Q_PROPERTY(bool                  truncated      MEMBER truncated      FINAL)
    Q_PROPERTY(QString               note           MEMBER note           FINAL)
    Q_PROPERTY(double                mu             MEMBER mu             FINAL)
    Q_PROPERTY(double                sigmaLo        MEMBER sigmaLo        FINAL)
    Q_PROPERTY(double                sigmaHi        MEMBER sigmaHi        FINAL)
    Q_PROPERTY(double                idealLo        MEMBER idealLo        FINAL)
    Q_PROPERTY(double                idealHi        MEMBER idealHi        FINAL)
    Q_PROPERTY(double                step           MEMBER step           FINAL)
    Q_PROPERTY(int                   decimals       MEMBER decimals       FINAL)
    Q_PROPERTY(bool                  hasPlausibleLo MEMBER hasPlausibleLo FINAL)
    Q_PROPERTY(double                plausibleLo    MEMBER plausibleLo    FINAL)
    Q_PROPERTY(bool                  hasPlausibleHi MEMBER hasPlausibleHi FINAL)
    Q_PROPERTY(double                plausibleHi    MEMBER plausibleHi    FINAL)
    Q_PROPERTY(bool                  scanned        MEMBER scanned        FINAL)
public:
    bool                  found = false;
    double                width = 0.0;
    double                height = 0.0;
    double                xMin = 0.0;
    double                xMax = 0.0;
    QString               unit;
    QList<ModelPlotBand>  bands;
    QList<ModelPlotPoint> curve;
    QList<ModelPlotPoint> samples;
    QList<double>         rug;
    double                muX = 0.0;
    double                idealLoX = 0.0;
    double                idealHiX = 0.0;
    bool                  lowOpen = false;
    bool                  highOpen = false;
    int                   n = 0;
    int                   ideal = 0;
    int                   good = 0;
    int                   watch = 0;
    int                   action = 0;
    int                   implausible = 0;
    bool                  truncated = false;
    QString               note;
    double                mu = 0.0;
    double                sigmaLo = 0.0;
    double                sigmaHi = 0.0;
    double                idealLo = 0.0;
    double                idealHi = 0.0;
    double                step = 0.0;
    int                   decimals = 0;
    bool                  hasPlausibleLo = false;
    double                plausibleLo = 0.0;
    bool                  hasPlausibleHi = false;
    double                plausibleHi = 0.0;
    bool                  scanned = false;

    friend bool operator==(const ModelCorridorPlot &, const ModelCorridorPlot &) = default;
};

// How corridorPlot() is asked: the pane's own size, the axis window (frozen during a drag), the curve's
// resolution and the scatter's cap, and a drag in progress (`previewMu`, `previewIdealLo`,
// `previewIdealHi`, in the measure's units). NaN or -1: not given — CorridorPlotOptions' default stands,
// and no preview. QML passes a plain object (a structured value).
struct ModelCorridorPlotOptions
{
    Q_GADGET
    QML_VALUE_TYPE(modelCorridorPlotOptions)
    QML_STRUCTURED_VALUE
    Q_PROPERTY(double width           MEMBER width           FINAL)
    Q_PROPERTY(double height          MEMBER height          FINAL)
    Q_PROPERTY(double windowMin       MEMBER windowMin       FINAL)
    Q_PROPERTY(double windowMax       MEMBER windowMax       FINAL)
    Q_PROPERTY(int    curveSteps      MEMBER curveSteps      FINAL)
    Q_PROPERTY(int    maxSamplePoints MEMBER maxSamplePoints FINAL)
    Q_PROPERTY(double previewMu       MEMBER previewMu       FINAL)
    Q_PROPERTY(double previewIdealLo  MEMBER previewIdealLo  FINAL)
    Q_PROPERTY(double previewIdealHi  MEMBER previewIdealHi  FINAL)
public:
    double width = std::numeric_limits<double>::quiet_NaN();
    double height = std::numeric_limits<double>::quiet_NaN();
    double windowMin = std::numeric_limits<double>::quiet_NaN();
    double windowMax = std::numeric_limits<double>::quiet_NaN();
    int    curveSteps = -1;
    int    maxSamplePoints = -1;
    double previewMu = std::numeric_limits<double>::quiet_NaN();
    double previewIdealLo = std::numeric_limits<double>::quiet_NaN();
    double previewIdealHi = std::numeric_limits<double>::quiet_NaN();
};


class ModelBrowser : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The type rail: a ModelTypeEntry for every content type, count derived by asking
    // the row list rather than by a stored figure that can go stale.
    Q_PROPERTY(QList<ModelTypeEntry> types READ types NOTIFY modelChanged)

    // Summed across the types, never hardcoded — the brief is explicit, and a literal 676 would be
    // wrong the first time anybody authored anything.
    Q_PROPERTY(int totalObjects READ totalObjects NOTIFY modelChanged)

    // "core v1.0.0 · schema 1" for the header line.
    Q_PROPERTY(QString packLabel READ packLabel NOTIFY modelChanged)

    // ── Draft state ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool dirty READ dirty NOTIFY modelChanged)
    // Objects that differ from the saved pack, NOT commands on the stack. Three edits to one label
    // are one unsaved object, and the status bar says what would be written.
    Q_PROPERTY(int unsavedCount READ unsavedCount NOTIFY modelChanged)

    // ── How far this install has drifted from what shipped ──────────────────
    //
    // Two counts because they are two different things to lose, and the reset prompt says both.
    // `overridden` is shipped objects this install has changed — the ones that make its grading
    // differ from an unmodified install, which is what the first-save warning is about. `authored`
    // is objects written here that core has never heard of; those extend the model rather than
    // contradicting it, so they do not trigger that warning, but a reset still takes them.
    Q_PROPERTY(int overriddenCount READ overriddenCount NOTIFY modelChanged)
    Q_PROPERTY(int authoredCount   READ authoredCount   NOTIFY modelChanged)

    // ── Undo ────────────────────────────────────────────────────────────────
    Q_PROPERTY(bool    canUndo   READ canUndo   NOTIFY modelChanged)
    Q_PROPERTY(bool    canRedo   READ canRedo   NOTIFY modelChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY modelChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY modelChanged)
    // The Edits history: a ModelEditEntry per command, oldest first. Clicking a row
    // calls undoTo(), which is what makes the stack navigable rather than a counter.
    Q_PROPERTY(QList<ModelEditEntry> edits READ edits NOTIFY modelChanged)
    // Session-scoped, and SAID SO in the UI. A stack that silently empties teaches an author not to
    // trust it — see the panel's Edits header. (ADDENDUM-01's open question; recorded in the code
    // because a decision nobody can find is a decision that gets remade.)
    Q_PROPERTY(bool undoIsSessionScoped READ undoIsSessionScoped CONSTANT)

    // What is wrong with the DRAFT: the validation chip's two counts. The rows themselves are the
    // Health table's (rows("health")); validation() is the C++ list the counts read.
    Q_PROPERTY(int validationErrorCount   READ validationErrorCount   NOTIFY modelChanged)
    Q_PROPERTY(int validationWarningCount READ validationWarningCount NOTIFY modelChanged)

    // ── The corridor sample scan ────────────────────────────────────────────
    Q_PROPERTY(bool    corridorScanning READ corridorScanning NOTIFY corridorSamplesChanged)
    // Which measure the samples in hand belong to. Reported rather than assumed: a plot rendered
    // from another measure's readings would be a confident, wrong picture.
    Q_PROPERTY(QString corridorScanned  READ corridorScanned  NOTIFY corridorSamplesChanged)
    Q_PROPERTY(int     corridorSwings   READ corridorSwings   NOTIFY corridorSamplesChanged)
    // The library root to scan, set from appSettings.athleteLibraryPath by the panel — the same
    // seam every other holder of this setting uses, so this object stays free of the dependency.
    Q_PROPERTY(QString libraryRoot READ libraryRoot WRITE setLibraryRoot NOTIFY corridorSamplesChanged)

    // The pack-wide grade policy, by NAME. Load-bearing for the corridor picture: every band edge
    // is policy-dependent, so drawing under the default while the user has chosen Strict would
    // paint a corridor the app does not grade against — which norm.h records as a bug that already
    // happened once. Bound to AppSettings by the panel, so this object keeps no settings dependency.
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY modelChanged)

    // ── The swing on screen ─────────────────────────────────────────────────
    //
    // The library is authored ABOUT swings, and until now it could not show one. An author editing
    // a corridor could see the whole library's distribution (the corridor scan) and the numbers
    // they were typing, but not the reading in front of them on the session screen — so "is this
    // corridor right?" was answerable only in aggregate, one measure at a time.
    //
    // Bound by the panel from app.state (the replay, else the selection), as `libraryRoot` and
    // `gradePolicy` use, so this object keeps no dependency on what a session screen is.
    Q_PROPERTY(QString currentSwingDir READ currentSwingDir WRITE setCurrentSwingDir
               NOTIFY currentSwingChanged)
    // "swing 3 · DRIVER" — what the measures column's header says it is showing. Empty until a grid
    // has actually been read, so it can never name a swing whose values are not on screen.
    Q_PROPERTY(QString currentSwingLabel READ currentSwingLabel NOTIFY currentSwingChanged)
    // ── Who the swing belongs to ────────────────────────────────────────────
    //
    // The two demographic fields of the athlete, RAW: an ISO date of birth and a sex token, exactly
    // as AthleteController holds them. Not a resolved cohort, because the age band has to be taken
    // ON THE DAY OF THE SWING — an athlete ages across their own history, and a band computed
    // anywhere else would grade a swing from four years ago against the band they are in now. This
    // object knows the swing's date; the panel knows the athlete; so the panel hands over the facts
    // and the derivation happens here, once, through the same cohortFor() everything else uses.
    //
    // Both are optional and unset is a first-class answer: a golfer who has told us nothing is
    // graded by the corridor that describes everyone, never NotMeasured.
    Q_PROPERTY(QString athleteDob READ athleteDob WRITE setAthleteDob NOTIFY currentSwingChanged)
    Q_PROPERTY(QString athleteSex READ athleteSex WRITE setAthleteSex NOTIFY currentSwingChanged)
    // WHO THE GOLFER IS, in words — "men 55–64", or empty when neither axis is known. Reported for
    // the reason the context is: a golfer whose grades improve after entering their date of birth
    // has to read that as the corridor becoming right for them, not as the app going soft.
    //
    // The athlete's cohort, NOT the answering row's. They differ — the shipped women's speed row is
    // qualified on sex alone and is reached by the probe order dropping the age axis — and only the
    // first has a single answer for a whole table, because which row answers is per measure. If a
    // surface ever needs the second, that is NormResolution::cohort(), one per cell.
    Q_PROPERTY(QString currentSwingCohort READ currentSwingCohort NOTIFY currentSwingChanged)

    // The context the readings are GRADED at, derived from the swing's club — "driver", "wedge".
    // Reported rather than assumed: a value coloured against the full-swing corridor when the shot
    // was a wedge is a confident, wrong colour, and the reader has to be able to see which corridor
    // answered. Empty when no swing is loaded.
    Q_PROPERTY(QString currentSwingContext READ currentSwingContext NOTIFY currentSwingChanged)
    // A phase grid is being read. The read is off the UI thread (a swing with no sidecar, or a
    // stale one, costs a full swing.json parse), so the column arrives a beat after the swing does.
    Q_PROPERTY(bool currentSwingLoading READ currentSwingLoading NOTIFY currentSwingChanged)
    // Does this swing produce a value for anything the library measures? False for a swing whose
    // analysis carries none of the metrics any measure reads — which is a capture gap and says so,
    // rather than a column of dashes with no explanation.
    Q_PROPERTY(bool currentSwingHasValues READ currentSwingHasValues NOTIFY currentSwingChanged)

    // ── The Gates view's library count ──────────────────────────────────────
    // True while the library's gate records are being counted (scanGates()).
    Q_PROPERTY(bool gatesScanning READ gatesScanning NOTIFY gateCountsChanged)

public:
    // The QML-made one (DiagnosticModel.qml) reaches the process's RuleData (RuleData::installed(), the
    // interim door until step 4's `app`); a test hands it one. It reads that RuleData's snapshot and writes
    // through its commands, never a file (refactor step 3.9; DEC-170).
    explicit ModelBrowser(QObject *parent = nullptr);
    explicit ModelBrowser(pinpoint::RuleData *ruleData, QObject *parent = nullptr);
    ~ModelBrowser() override;

    QList<ModelTypeEntry> types() const;
    int          totalObjects() const;
    QString      packLabel() const;
    bool         dirty() const;
    int          unsavedCount() const;
    int          overriddenCount() const;
    int          authoredCount() const;
    bool         canUndo() const;
    bool         canRedo() const;
    QString      undoLabel() const;
    QString      redoLabel() const;
    QList<ModelEditEntry> edits() const;
    bool         undoIsSessionScoped() const { return true; }
    QList<ModelRow> validation() const;
    int          validationErrorCount() const;
    int          validationWarningCount() const;

    bool    corridorScanning() const { return m_corridorScanning; }
    QString corridorScanned() const { return m_corridorScanned; }
    int     corridorSwings() const { return int(m_corridorValues.size()); }
    QString libraryRoot() const { return m_libraryRoot; }
    void    setLibraryRoot(const QString &root);
    QString gradePolicy() const { return m_policyName; }
    void    setGradePolicy(const QString &name);

    QString currentSwingDir() const { return m_swingDir; }
    void    setCurrentSwingDir(const QString &dir);
    QString currentSwingLabel() const;
    QString currentSwingContext() const { return m_swingContextId; }
    QString athleteDob() const { return m_athleteDob; }
    void    setAthleteDob(const QString &iso);
    QString athleteSex() const { return m_athleteSex; }
    void    setAthleteSex(const QString &token);
    QString currentSwingCohort() const;
    bool    currentSwingLoading() const { return m_swingLoading; }
    bool    currentSwingHasValues() const { return !m_swingGrid.isEmpty(); }
    bool    gatesScanning() const { return m_gatesScanning; }

    // ── Reading ─────────────────────────────────────────────────────────────

    // The column spec for one type, as ModelColumn values. Rows come back with `cells` parallel to
    // this, so the table delegate renders any type without knowing which one it has.
    Q_INVOKABLE QList<ModelColumn> columns(const QString &type) const;

    // Rows for one type, as asked by a ModelRowQuery: { search, sort, descending, facets:[{key,
    // values}], ids:[…] } from QML.
    //
    // Sorting and filtering happen HERE. `sort` names a column key and falls back to the type's
    // default sort — the one that answers the question the author arrived with (measures by status
    // then least-read, so the 42 nothing reads are on screen without asking).
    //
    // Each row is a ModelRow, each cell a ModelCell.
    Q_INVOKABLE QList<ModelRow> rows(const QString &type, const ModelRowQuery &query = {}) const;

    // The facet rail under the type list, as ModelFacet values (each option a ModelFacetOption),
    // counted over the type's rows BEFORE its own facet is applied, so a count never reads zero for
    // the thing you are looking at.
    Q_INVOKABLE QList<ModelFacet> facets(const QString &type) const;

    // One search across every registry at once (the brief's 4b). Rows carry a Type column and the
    // same cell shape as everything else, so "hip" returns characteristics, measures, signals,
    // screens and drills in one list. Cannot be assembled in QML — it spans nine registries.
    Q_INVOKABLE QList<ModelRow> searchAll(const QString &query) const;

    // The inspector: a RELATIONSHIP HUB, not a property sheet — a ModelInspection whose sections are
    // ModelInspectorSection values of ModelInspectorRow rows, so every related object is one click away
    // and QML never decides what relates to what.
    Q_INVOKABLE ModelInspection inspect(const QString &type, const QString &id) const;

    // What the row dot means in a type's table, as ModelLegendEntry rows in the order a reader ranks
    // them, for the legend in the context bar above it. Empty for a type whose rows carry no dot.
    Q_INVOKABLE QList<ModelLegendEntry> dotLegend(const QString &type) const;

    // The graph around ANY object, not only a condition.
    //
    // A condition gets the causal DAG (dag_layout.h) because it has ranks — causes to the left,
    // effects to the right — and that ordering is the thing the picture is for. Nothing else does:
    // a measure has readers and corridors, a screen has the conditions it would settle, and there is
    // no "distance" to rank those by. Those get a NEIGHBOURHOOD instead — the object in the middle,
    // what points at it on the left, what it points to on the right — which is the honest shape for
    // a relation that is one hop and has no direction to iterate.
    //
    // Same node and edge shape either way (a ModelGraph), so the renderer does not branch. `options`
    // is a ModelGraphOptions (a plain object from QML); what it leaves out keeps the layout's default.
    Q_INVOKABLE ModelGraph graph(const QString &type, const QString &id,
                                 const ModelGraphOptions &options = {}) const;

    // Which object the GRAPH should centre on when this one is selected, as a ModelObjectRef.
    //
    // Almost always the selection itself. A LINK is the exception: it has a neighbourhood of its
    // own — two nodes and the line between them — and drawing that in place of the causal picture
    // replaces the very drawing the reader picked the line out of. Everything that marks a selected
    // link on the canvas — the heavier stroke, the muting of everything else — is invisible if
    // selecting it changes the picture. So a claim centres on its cause and stays drawn.
    Q_INVOKABLE ModelObjectRef graphFocus(const QString &type, const QString &id) const;

    // The laid-out causal DAG around one condition — every coordinate from dag_layout.h, over the
    // WORKING assembly so an unsaved edge is drawn. Same shape as
    // the graph pane renders.
    // C++ only since 3.9-ii: no view calls it (the graph reads graph(); NOTES-3.9-ii J3).
    ModelGraph dag(const QString &conditionId, const ModelGraphOptions &options = {}) const;

    // Legal targets for a link from `fromId`, pre-filtered: no self, no existing edge, no cycle,
    // and for `corroborates` no pair that already has a causal path (which the validator forbids).
    // The filtering is the point — an illegal edit cannot be CONSTRUCTED, rather than being built
    // and refused afterwards.
    //
    // `end` says which end `fixedId` occupies, and defaults to the ordinary "what could this
    // cause". "to" asks the mirror — what could cause this — which is what re-pointing the `from`
    // end of an existing claim needs, and which no amount of swapping the arguments would give,
    // because the acyclicity question is directed.
    Q_INVOKABLE QList<ModelCandidate> linkCandidates(const QString &relation, const QString &fixedId,
                                            const QString &search = QString(),
                                            const QString &end = QStringLiteral("from")) const;

    // Measures a characteristic could be detected by, minus the ones it already reads.
    Q_INVOKABLE QList<ModelCandidate> measureCandidates(const QString &conditionId,
                                               const QString &search = QString()) const;

    // Metrics this measure could read INSTEAD of its own where a swing carries one — the rungs its
    // instrument ladder does not already hold. Filtered to the measure's UNIT, because a ladder is
    // one quantity measured twice and a rung in another unit would grade against the wrong corridor.
    Q_INVOKABLE QList<ModelCandidate> metricKeyCandidates(const QString &measureId,
                                                 const QString &search = QString()) const;

    // Would this link be legal, and if not, why? Returns { ok, reason }.
    //
    // Asked DURING a drag in the graph, not on release: an illegal target has to refuse while the
    // pointer is still moving, with the reason stated ("X already leads to Y, so this would create
    // a cycle"). By release the author has already committed to the gesture, and a refusal then
    // reads as the tool being broken. addLink() asks the same function, so the drag cannot say yes
    // to something the write would then reject.
    // C++ only since 3.9-ii: the drag asks linkRefusals(), the writes ask this (NOTES-3.9-ii J3).
    QVariantMap linkLegality(const QString &fromId, const QString &toId,
                             const QString &relation = QStringLiteral("causes")) const;

    // Every refusal for a whole candidate set at once, computed ONCE when a link drag arms.
    //
    // `fixedId` is the endpoint that is not moving and `end` says which end of the proposed edge it
    // occupies — "to" for `Add cause of this…` (the target becomes a cause of the held node), "from"
    // for the mirrored case, and both for re-point, where the endpoint staying put can be either.
    // That parameter is why this is not linkLegality() in a loop with the arguments swapped: the
    // caller states the geometry once and the traversal follows it.
    //
    // Returns a ModelLinkRefusals holding the REFUSED ids only — absent means legal. `reason` is
    // one of self · exists · cycle for the view to key a tone off; `text` is the words to show,
    // written here because a phrase assembled in a delegate is a phrase nothing can translate.
    //
    // One traversal, then O(1) per candidate. The cycle case is the expensive one and it is the
    // reason this exists: `X → fixed` closes a cycle exactly when fixed already reaches X, so one
    // causalClosure() of the fixed end answers it for every candidate at once. Per-hover was the
    // shape this replaces.
    Q_INVOKABLE ModelLinkRefusals linkRefusals(const QString &fixedId, const QStringList &ids,
                                         const QString &end = QStringLiteral("to")) const;

    // At most THREE options for one enum field, ordered, with the one this object already has
    // marked: ModelOption rows with `current` set on that one.
    //
    // This is the graph ring's collar, and the cap is the whole reason it is a model call rather
    // than a filter in the delegate. A collar is one level and three cells, full stop, so which
    // three is a decision — for `strength` it is the whole ladder in ladder order, and for `group`
    // it is the three most used in this library, with the object's own kept in whatever happens.
    // A field with more options than fit does not get a longer collar; it gets the inspector, and
    // the ring says so by opening it.
    Q_INVOKABLE QList<ModelOption> ringValues(const QString &type, const QString &id,
                                        const QString &field) const;

    // ── Editing ─────────────────────────────────────────────────────────────
    //
    // Every one of these is ONE command on the undo stack, and every one returns
    // a ModelEditResult — a refusal has to reach the author in their own terms, never a log.
    //
    // A cascade is ONE command (ADDENDUM-01's second point): the stack holds whole before/after
    // copies of the working pack, so an edit that touches rows the author never named undoes as a
    // unit. A cascade that undid partially would be worse than one that could not be undone,
    // because the author would believe they were back where they started.

    Q_INVOKABLE ModelEditResult setField(const QString &type, const QString &id, const QString &field,
                                     const QVariant &value);

    // Multi-select bulk-set: one command, whatever N is. Re-tiering twenty links one at a time is
    // the single biggest time sink in the old panel.
    Q_INVOKABLE ModelEditResult setFieldOnAll(const QString &type, const QStringList &ids,
                                          const QString &field, const QVariant &value);

    Q_INVOKABLE ModelEditResult addLink(const QString &fromId, const QString &toId,
                                    const QString &relation = QStringLiteral("causes"),
                                    const QString &strength = QStringLiteral("moderate"));
    Q_INVOKABLE ModelEditResult removeLink(const QString &fromId, const QString &toId,
                                       const QString &relation = QStringLiteral("causes"));

    // Move one END of an existing causal claim to another condition. `end` is "from" or "to".
    //
    // Not a remove plus an add, and the difference is the whole point: the claim keeps its strength,
    // its provenance tier and its citations, because re-pointing is a correction to WHO the claim is
    // about, not a retraction of it. It is also ONE command, so a re-point that the author changes
    // their mind about undoes in one step rather than leaving a deleted edge behind.
    //
    // Refused through linkLegality() like every other write, so the drag and the write cannot
    // disagree.
    Q_INVOKABLE ModelEditResult repointCause(const QString &edgeId, const QString &end,
                                         const QString &newId);

    // List forms, so a marquee selection is ONE undo step. Singular calls exist for both and are
    // still the right thing for a single row; these exist because twelve undos to reverse one
    // gesture is a stack that punishes the shortcut it is there to enable.
    Q_INVOKABLE ModelEditResult removeCause(const QStringList &edgeIds);
    Q_INVOKABLE ModelEditResult removeObjects(const QString &type, const QStringList &ids);

    // Strength over a list. A thin name over setFieldOnAll(), because the ring spoke says "Strength"
    // and a caller should not have to know the field is spelled the same way.
    Q_INVOKABLE ModelEditResult setCauseStrength(const QStringList &edgeIds, const QString &strength);

    // A new object AND its attachment to `otherId`, as ONE command — the graph drag released over
    // empty canvas. `end` says which end of the new causal link the existing condition occupies,
    // and is ignored by the types that do not attach with one.
    //
    // What "attached" means is per type, because it is per type everywhere else too: a
    // characteristic becomes a cause, a screen becomes what settles it, a drill becomes what
    // answers it. A measure is not here — it is minted through previewMeasure/mintMeasure, which
    // asks questions a name cannot answer, and the caller routes that chip there.
    //
    // Two commands would let an author undo the object and keep an attachment to nothing. That is
    // not a half-finished edit; it is a pack that fails validation with a dangling reference.
    Q_INVOKABLE ModelEditResult createAndAttach(const QString &type, const QString &name,
                                            const QString &otherId, const QString &end);

    // Attach a measure to a characteristic at a tail, minting the signal that reads it.
    Q_INVOKABLE ModelEditResult addMeasureTo(const QString &conditionId, const QString &measureId,
                                         const QString &direction = QStringLiteral("high"));
    Q_INVOKABLE ModelEditResult removeMeasureFrom(const QString &conditionId, const QString &measureId);

    // Append to / drop from a measure's instrument ladder (Measure::preferKeys). Append, not
    // insert: the list is ordered best-first and nothing here reorders one — see addPreferKey().
    Q_INVOKABLE ModelEditResult addPreferKey(const QString &measureId, const QString &metricKey);
    Q_INVOKABLE ModelEditResult removePreferKey(const QString &measureId, const QString &metricKey);

    // Duplicate beats blank: a new object pre-filled from an existing one. Returns the new id in
    // `id` so the table can select it and the author can type over the label.
    Q_INVOKABLE ModelEditResult duplicate(const QString &type, const QString &id);

    Q_INVOKABLE ModelEditResult removeObject(const QString &type, const QString &id);

    // ── Screens and drills ──────────────────────────────────────────────────
    //
    // Both registries have had rows, facets and an inspector section since this panel landed, and no
    // write path at all — so every cell was inert and the inspector was a dead end. They are ordinary
    // editable types now: setField(), createObject(), duplicate() and removeObject() all dispatch to
    // them, so the shell's inline edit, ⌘D, dirty gutter, Source badge and bulk-set need no changes.
    //
    // The relationships are the exception, because they are not stored where you would guess. A
    // screen does not list what it settles and a drill does not list what it answers — the CONDITION
    // names them (`Condition::screenRef`, `Condition::drills`), which is the join every reader in the
    // app already uses. So these four write the condition, and they live here rather than on the
    // condition because "what does this screen settle" is the question an author has open.
    Q_INVOKABLE ModelEditResult addScreenSettles(const QString &screenId, const QString &conditionId);
    Q_INVOKABLE ModelEditResult removeScreenSettles(const QString &screenId, const QString &conditionId);
    Q_INVOKABLE ModelEditResult addDrillAnswers(const QString &drillId, const QString &conditionId);
    Q_INVOKABLE ModelEditResult removeDrillAnswers(const QString &drillId, const QString &conditionId);

    // Legal, pre-filtered candidates, as linkCandidates() is: conditions this screen does not already
    // settle / this drill does not already answer. An illegal edit cannot be CONSTRUCTED.
    Q_INVOKABLE QList<ModelCandidate> screenCandidates(const QString &screenId,
                                              const QString &search = QString()) const;
    Q_INVOKABLE QList<ModelCandidate> drillCandidates(const QString &drillId,
                                             const QString &search = QString()) const;
    // The same join read from the HABIT's end: drills in the draft registry — shipped, overridden or
    // authored here, saved or not — that this condition does not already list. The habit's Drills
    // section adds from this, so a drill written in this panel can reach a work-on page without the
    // author having to go and find the drill first.
    Q_INVOKABLE QList<ModelCandidate> conditionDrillCandidates(const QString &conditionId,
                                                      const QString &search = QString()) const;

    // ── Report wording, checked while it is typed ───────────────────────────
    //
    // What the pack loader would say about ONE field if it held `value` — ModelFieldIssue values with
    // severity "error" | "warning", empty when the words are clean. Asked of a temporary copy of
    // the draft object, so nothing is written and nothing reaches the undo stack: this is the line
    // under the editor while the author types, and a keystroke is not an edit.
    //
    // The messages are reportFieldIssues()' (characteristic_pack.h) for a habit's and a measure's
    // report fields, and gateWordsMessage()'s (gate_words_pack.h) for a gate's or a reading's golfer
    // words — the SAME strings validatePack() and validateGateWordSet() put in Health, so the inspector
    // and the health list cannot word one fault two ways. Any other field answers empty.
    Q_INVOKABLE QList<ModelFieldIssue> fieldIssues(const QString &type, const QString &id, const QString &field,
                                         const QVariant &value) const;

    // Every link resting on one reference. The citation is imported and stays read-only; the CLAIM
    // resting on it is ours, and its strength is editable through the ordinary setField("links", …).
    // C++ only since 3.9-ii: no view calls it; the reference pane reads inspect() (NOTES-3.9-ii J3).
    QList<ModelInspectorRow> linksCitingReference(const QString &refId) const;

    // One record as CSL-JSON, for the reference pane's copy action — the same format the whole-set
    // export writes, because a coach pasting one citation into Zotero wants what the file would have
    // given them. Empty for an id that resolves to nothing.
    Q_INVOKABLE QString referenceCsl(const QString &refId) const;
    // The resolver URL for a DOI, or empty when the record has none. Built here rather than in QML:
    // which of three identifiers a record carries is a fact about the record.
    Q_INVOKABLE QString referenceDoiUrl(const QString &refId) const;

    // ── Corridors ───────────────────────────────────────────────────────────
    //
    // A corridor is a norm row keyed on (measureId, contextId, cohort). It is edited through
    // setField() like everything else — the fields are `mu`, `sigmaLo`, `sigmaHi`, `plausibleLo`,
    // `plausibleHi`, `unit`, `source` and `citation` — and the row id is the norm key.

    // Every context this measure could carry a corridor at, marked with what already resolves
    // there: a ModelCorridorContext per context.
    Q_INVOKABLE QList<ModelCorridorContext> corridorContexts(const QString &measureId) const;

    // Start a corridor of your own at this context. Seeded from whatever currently resolves there,
    // so the common edit is a nudge rather than authoring five numbers from nothing.
    Q_INVOKABLE ModelEditResult addCorridor(const QString &measureId, const QString &contextId);

    // Take another context's numbers wholesale. `adoptFrom` in the old editor.
    Q_INVOKABLE ModelEditResult adoptCorridor(const QString &measureId, const QString &contextId,
                                          const QString &fromContextId);

    // Drop your row. Where core ships one this restores it; where it does not, the parent context's
    // corridor resolves again. Those are different outcomes and the message says which.
    Q_INVOKABLE ModelEditResult resetCorridor(const QString &measureId, const QString &contextId);

    // ── The corridor, as a picture ──────────────────────────────────────────
    //
    // The distribution the norm CLAIMS, drawn over the readings the library actually holds. All
    // geometry from corridor_plot.h — QML draws these points and positions nothing.
    //
    // `options` (a ModelCorridorPlotOptions; a plain object from QML) carries the pane's own metrics
    // (width, height) and any drag preview; anything omitted keeps the CorridorPlotOptions default.
    // Returns `found` false and an empty `curve` for a key that resolves to nothing.
    Q_INVOKABLE ModelCorridorPlot corridorPlot(const QString &measureId, const QString &contextId,
                                               const ModelCorridorPlotOptions &options = {}) const;

    // Read every swing in the library and reduce this measure over each. Asynchronous, because it
    // walks the whole library; the plot renders from whatever has been read so far and says so.
    Q_INVOKABLE void scanCorridor(const QString &measureId);

    // Count, per gate, the swings in the library whose records name it: a sum over every athlete's
    // session index, each session's summary carrying its own counts, and a session not summarised yet
    // read swing by swing (gate_counts.h; platform refactor P-3b). With no index (no source installed,
    // another library), the whole library is walked. Asynchronous (on a worker); the Gates rows say
    // "counting" until gateCountsChanged. A count already in memory for this library is shown at once
    // and refreshed by the count. A count in progress is not restarted.
    Q_INVOKABLE void scanGates();

    // ── Context bindings — where a characteristic applies ───────────────────
    //
    // A binding is an EXCEPTION: a context with no row anywhere on its chain applies. Rows carry
    // both what is authored HERE and what is inherited, because a control that cannot tell one from
    // the other teaches the author that every row is an assertion.
    // C++ only since 3.9-ii: the inspector reads the bindings through inspect() (NOTES-3.9-ii J3).
    QVariantList bindingsOf(const QString &conditionId) const;
    Q_INVOKABLE ModelEditResult setBinding(const QString &conditionId, const QString &contextId,
                                        bool applicable, bool material);
    Q_INVOKABLE ModelEditResult clearBinding(const QString &conditionId, const QString &contextId);

    // ── Minting a measure ───────────────────────────────────────────────────
    //
    // A typed phrase SEEDS facet selections; it is not a query. Wrong guesses are corrected by
    // tapping chips, which is far easier than rephrasing a search that returned nothing.
    Q_INVOKABLE ModelFacetSeed seedFacetsFromPhrase(const QString &phrase) const;
    Q_INVOKABLE QList<ModelOption> anatomyRoles() const;
    Q_INVOKABLE QList<ModelOption> quantitiesFor(const QString &whatRole) const;
    Q_INVOKABLE QList<ModelOption> referencesFor(const QString &whatRole, const QString &quantity) const;
    Q_INVOKABLE QList<ModelOption> reducerKinds() const;
    Q_INVOKABLE QList<ModelOption> phases() const;
    // The measure four facets name: what is measured (an anatomy role), the quantity, what it is
    // measured against (a role) and the phase it is read at ("p1"…). The near-duplicate check is the
    // defence against a library filling with almost-identical measures, and it has to fire AT
    // CREATION — after the fact nobody merges them.
    Q_INVOKABLE ModelMeasurePreview previewMeasure(const QString &what, const QString &quantity,
                                                   const QString &reference,
                                                   const QString &anchor = QStringLiteral("p1")) const;
    Q_INVOKABLE ModelEditResult     mintMeasure(const QString &what, const QString &quantity,
                                                const QString &reference,
                                                const QString &anchor = QStringLiteral("p1"));

    // A blank object of this type. Duplicate is the fast path and this is the second one, but
    // "second" is not "absent" — there has to be a way to author something unlike anything here.
    //
    // `name` is optional and names it on creation rather than leaving "New characteristic" to be
    // typed over: the graph's create-on-drop asks for a name inside the gesture, and landing a row
    // called "New characteristic" that the author has already named would be a second edit and a
    // second undo step for one thing they did once. The id is minted from the name when there is
    // one, so it reads as what it is in every file that later mentions it.
    Q_INVOKABLE ModelEditResult createObject(const QString &type, const QString &name = QString());

    // ── Pack-wide settings ──────────────────────────────────────────────────
    //
    // Not content: these change how everything GRADES, so they are stated apart from the draft and
    // they are not undoable — they are not edits to the library, they are how it is being read.
    Q_INVOKABLE QList<ModelGradePolicy> gradePolicies() const;
    Q_INVOKABLE QList<ModelNormSet> normSets() const;

    // ── Artefacts that leave the app ────────────────────────────────────────
    Q_INVOKABLE ModelEditResult exportRoadmap() const;
    Q_INVOKABLE ModelEditResult exportReferences() const;

    // The roadmap and its two companions, which have no other home once Diagnostics retires. C++ only
    // since 3.9-ii: the exports and the tests read them; no view does (NOTES-3.9-ii J3).
    QVariantList roadmap() const;
    QVariantList captureGaps() const;
    QVariantList causeCoverage() const;
    QVariantList glossary(const QString &search = QString()) const;

    // ── Undo / save ─────────────────────────────────────────────────────────
    Q_INVOKABLE ModelEditResult undo();
    Q_INVOKABLE ModelEditResult redo();
    // Wind the stack to just after command `index` (-1 = before everything). This is what the Edits
    // list clicks into.
    Q_INVOKABLE ModelEditResult undoTo(int index);

    // Write the working pack to the user pack file. Does NOT clear the stack — saving and
    // immediately spotting the mistake is the common case (ADDENDUM-01's fourth point).
    Q_INVOKABLE ModelEditResult save();
    // Discard every unsaved edit, back to the file. Itself undoable.
    Q_INVOKABLE ModelEditResult revert();

    // Put this install back on the model that ships: every override AND everything authored here,
    // across both registries. NOT revert() — that one discards the session's UNSAVED edits back to
    // the file; this discards what the file itself holds, and it writes.
    //
    // Recoverable twice over, because the panel's rule is that nothing here is unrecoverable:
    //   · it is a command like every other write, so ⌘Z restores the whole draft for the session
    //   · the user layers are COPIED ASIDE before they are replaced (`*-<stamp>.backup.json`, by
    //     RuleData::backupUserLayers), so an author who resets in error still has the file after the
    //     session's undo has expired
    // The returned map carries `backups` — the paths written — because a backup nobody is told about
    // is a backup nobody can use.
    Q_INVOKABLE ModelEditResult resetToStandard();

signals:
    void modelChanged();
    void corridorSamplesChanged();
    void currentSwingChanged();
    // The Gates view's library count landed (or a walk started). Rows refresh on it.
    void gateCountsChanged();
    // The library this panel shows changed: a save or reset of its own landed, or a snapshot RuleData
    // published moved a layer it shows (EVT-12 / EVT-13). The panel re-reads on it. The rest of the app
    // follows RuleData's events, not this (refactor step 3.9).
    void libraryChanged();

private:
    // ── RuleData (refactor step 3.9) ────────────────────────────────────────────────────────────
    pinpoint::RuleData                              *m_ruleData = nullptr;   // null: no RuleData installed
    std::shared_ptr<const pinpoint::RuleDataSnapshot> m_snap;               // every read of the rule data
    bool                                             m_writing  = false;    // inside a WriteScope

    const pinpoint::analysis::ReferenceSet &refs() const;   // the snapshot's bibliography
    void takeUserLayers(const pinpoint::RuleDataSnapshot &snap);
    void onRuleDataPublished();
    bool adoptSnapshot(std::shared_ptr<const pinpoint::RuleDataSnapshot> next, bool userLayersToo, bool announce);
    bool savedLayersMatch(const pinpoint::RuleDataSnapshot &snap) const;
    bool writeUserPack(QString *whyNot);
    bool writeUserNorms(QString *whyNot);
    bool writeUserScreens(QString *whyNot);
    bool writeUserDrills(QString *whyNot);
    bool writeUserGateWords(QString *whyNot);
    // The editor's own writes: RuleData's publishes are taken once, when the scope ends.
    class WriteScope {
    public:
        explicit WriteScope(ModelBrowser *b);
        ~WriteScope();
        bool finish();   // ends the scope and takes the snapshot, emitting nothing; true when it rebuilt
        WriteScope(const WriteScope &)            = delete;
        WriteScope &operator=(const WriteScope &) = delete;

    private:
        ModelBrowser *m_b;
        bool          m_done = false;
    };

    // ── Assembly ────────────────────────────────────────────────────────────
    void rebuild();                       // m_assembled = core + memory(m_working)
    const pinpoint::analysis::CharacteristicPack &pack() const;

    // ── Undo stack ──────────────────────────────────────────────────────────
    //
    // Whole-pack snapshots rather than field diffs. The user pack holds overrides only, so a copy is
    // small — and it is the only representation under which "a cascade is one command" is true by
    // construction rather than by remembering to capture the right extra rows.
    struct Command {
        QString                                label;    // "Strength → strong"
        QString                                detail;   // "Early extension → Loss of posture"
        // EVERY registry, every time — five since the gate words joined. A command that
        // snapshotted only the one it happened to touch would restore correctly on its own and
        // corrupt the others as soon as the stack was wound past it — the state a position on the
        // stack describes is the whole draft, not a diff.
        pinpoint::analysis::CharacteristicPack before;
        pinpoint::analysis::CharacteristicPack after;
        pinpoint::analysis::NormPack           normsBefore;
        pinpoint::analysis::NormPack           normsAfter;
        pinpoint::analysis::ScreenSet          screensBefore;
        pinpoint::analysis::ScreenSet          screensAfter;
        pinpoint::analysis::DrillSet           drillsBefore;
        pinpoint::analysis::DrillSet           drillsAfter;
        // The gate words joined the draft for #60 — the fifth layer, snapshotted for the same reason
        // as the other four.
        pinpoint::analysis::GateWordSet        gateWordsBefore;
        pinpoint::analysis::GateWordSet        gateWordsAfter;
    };
    // Push the current state as a command, dropping any redo tail. `before` is the pack state the
    // command started from; the norm baseline is captured by the caller in the same breath.
    //
    // The four-argument form is for every write that cannot touch a screen or a drill, which is most
    // of them: it captures the CURRENT screen and drill layers as the before-state, which is correct
    // precisely because they did not change. Writes that do touch them use the long form.
    void        pushCommand(const QString &label, const QString &detail,
                            const pinpoint::analysis::CharacteristicPack &before,
                            const pinpoint::analysis::NormPack           &normsBefore);
    void        pushCommand(const QString &label, const QString &detail,
                            const pinpoint::analysis::CharacteristicPack &before,
                            const pinpoint::analysis::NormPack           &normsBefore,
                            const pinpoint::analysis::ScreenSet          &screensBefore,
                            const pinpoint::analysis::DrillSet           &drillsBefore);
    // …and the longest, for the few writes that can touch the gate words as well: a gate's golfer
    // words in setField(), revert() and resetToStandard(). The six-argument form captures the
    // CURRENT gate-word layer as its before-state, for the same reason the four-argument one does
    // the screens and drills.
    void        pushCommand(const QString &label, const QString &detail,
                            const pinpoint::analysis::CharacteristicPack &before,
                            const pinpoint::analysis::NormPack           &normsBefore,
                            const pinpoint::analysis::ScreenSet          &screensBefore,
                            const pinpoint::analysis::DrillSet           &drillsBefore,
                            const pinpoint::analysis::GateWordSet        &gateWordsBefore);
    ModelEditResult applyStackPosition(int newIndex);

    // ── Working-copy helpers ────────────────────────────────────────────────
    //
    // Copy-on-write: fetch the working row for an id, copying it out of the assembled library on
    // first touch. Null only when no such object exists anywhere.
    pinpoint::analysis::Condition *workingCondition(const QString &id);
    pinpoint::analysis::Measure   *workingMeasure(const QString &id);
    pinpoint::analysis::Signal    *workingSignal(const QString &id);
    pinpoint::analysis::Edge      *workingEdge(const QString &fromId, const QString &toId,
                                               pinpoint::analysis::EdgeType type);
    // The causal edge set of `id` as the user pack must hold it. An override REPLACES a condition's
    // whole incoming causal set, so removing a shipped cause means writing back all the others.
    void materialiseCausesOf(const QString &conditionId);

    // Which ids differ from the saved pack, by comparing the two packs' serialised entities. Derived
    // rather than tracked: undo and revert move the working copy wholesale, and a hand-kept touched
    // set would drift the first time either of them ran.
    const QSet<QString> &dirtyIds() const;
    void                 invalidateDerived();

    // How many objects in the DRAFT sourceOf() answers `want` for, across all four registries. The
    // two counts the reset prompt states are the same question asked twice.
    int countBySource(const QString &want) const;

    // Rows for one type, unsorted and unfiltered. Everything else composes over this.
    QList<ModelRow> rawRows(const QString &type) const;

    // Marshalling helpers shared by rows() and searchAll().
    ModelRow conditionRow(const pinpoint::analysis::Condition &c, bool asCause) const;
    ModelRow measureRow(const pinpoint::analysis::Measure &m) const;
    ModelRow signalRow(const pinpoint::analysis::Signal &s) const;
    ModelRow edgeRow(const pinpoint::analysis::Edge &e) const;

    // Every writable field of one object, as editor rows. THE list — the inspector renders exactly
    // these, so "what can I edit" has one answer per type rather than one per surface.
    QList<ModelInspectorRow> fieldsOf(const QString &type, const QString &id) const;

    // "shipped" | "yours" | "both" for one id — the Source column, and the thing that tells an
    // author whose content they are about to change.
    QString sourceOf(const QString &id) const;

    // The neighbourhood of a non-condition object. Gathers the domain data, hands it to
    // layoutHub(), and marshals the result through marshalLayout() — the SAME function the causal
    // DAG goes through, because QML reads one schema and a second hand-written copy of it is how
    // this view once shipped lines drawn between two points belonging to neither box.
    ModelGraph neighbourhood(const QString &type, const QString &id,
                             const ModelGraphOptions &options) const;

    // A DagLayout as the graph delegate reads it. ONE marshaller for both pictures: the causal DAG
    // and the hub are two layouts, and every key QML reads is declared here exactly once.
    //
    // `hideWeak` and `hideProposed` are the two view switches that drop whole EDGES, and they are
    // applied here rather than by the caller because dropping an edge after marshalling would mean
    // filtering a list on a string it had just finished writing.
    ModelGraph  marshalLayout(const pinpoint::analysis::DagLayout &l,
                              bool hideWeak, bool hideProposed) const;
    // The type's own colour key and glyph, and whatever one line a node can say about itself.
    void        decorateNode(ModelGraphNode &node, const QString &type, const QString &id) const;

    // ── How far a condition reaches, and how much reaches it ────────────────
    //
    // The three derived counts every condition row carries, and the two condition lists rank on.
    // Direct out-degree (`coverageOf`) buries a long chain — "settled tempo habit" has ONE arrow out
    // of it and reaches 29 conditions and eleven distinct bad shots — so a list sorted on it puts
    // the root causes at the bottom. See ADDENDUM-03.
    //
    // Counts, never a score. Each is the size of a set the inspector can list, which is what lets
    // the ranking answer "why is this above that"; a strength-weighted magnitude could not.
    struct Reach {
        int leadsTo  = 0;   // causalClosure(downstream): everything this eventually causes
        int outcomes = 0;   // of those, the BallFlight ones — the bad shots it explains
        int causedBy = 0;   // causalClosure(upstream): everything that eventually causes this
    };
    // Memoised, because rows(), facets() and types() would each pay the closures again. The walks
    // read ONE pair of adjacencies built on the first miss after a mutation, rather than the pair
    // causalClosure(pack, …) builds per call — three closures of every condition each re-reading
    // the edge list was most of what listing the characteristics cost after an edit. All three are
    // cleared by invalidateDerived() with m_dirtyIds, off the one invalidation point every mutation
    // already goes through — a second place to remember would be a stale count on screen.
    const Reach &reachOf(const QString &conditionId) const;
    mutable QHash<QString, Reach>               m_reach;
    mutable pinpoint::analysis::CausalAdjacency m_reachDown, m_reachUp;
    mutable bool                                m_reachGraphValid = false;

    // ── Facets cut from the data, rather than from a closed vocabulary ──────
    //
    // A numeric facet ("leads to a lot of things") has no vocabulary to enumerate, so its chips are
    // quantile buckets over the CURRENT distribution: top tenth, top quarter, the rest, and zero —
    // which is always its own bucket, because "leads nowhere" is a category and not a low score.
    //
    // Bounds travel INSIDE each option map, and rows() filters by calling this rather than by
    // re-deriving them. That is what keeps "a chip cannot say twelve and return nine" true by
    // construction for a kind whose values are ranges instead of strings.
    QList<ModelFacet> quantileFacets(const QString &type) const;

    int  measureUsers(const QString &measureId) const;     // what it detects, as a count
    QList<ModelInspectorRow> measureUserRows(const QString &measureId) const;   // and as the actual list

    // The three columns the flat cross-type list adds — what an object is connected to, how many
    // links it has, and its status word. Per type, because "connected to" means something different
    // for a measure than for a screen, and the result list has one column for both.
    QVariantMap searchExtras(const QString &type, const QString &id) const;

    // The norm key, as the corridor table's row id. Round-trips through splitCorridorId().
    static QString corridorId(const QString &measureId, const QString &contextId);
    static bool    splitCorridorId(const QString &id, QString &measureId, QString &contextId);

    // Fetch the working norm row for a key, copying whatever currently resolves there on first
    // touch — the same copy-on-write the pack side uses, for the same reason.
    pinpoint::analysis::Norm *workingNorm(const QString &measureId, const QString &contextId);

    // The characteristics riding on one measure. Shared by usageOfMeasure (the count) and roadmap

    // The shipped pack and norm set, read ONCE per browser. Shipped content cannot change under a
    // running browser, and rebuild() used to re-read and re-validate both from the file on every
    // edit, undo and redo — two thirds of a rebuild's cost. Shared, because each assembly holds a
    // view of them (see rebuild()).
    std::shared_ptr<const pinpoint::analysis::ICharacteristicPackProvider> m_core;
    std::shared_ptr<const pinpoint::analysis::INormProvider>               m_coreNorms;
    std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_assembled;
    std::shared_ptr<const pinpoint::analysis::INormProvider>         m_norms;
    pinpoint::analysis::MetricCatalogue                              m_cat;

    pinpoint::analysis::CharacteristicPack m_savedUser;
    pinpoint::analysis::CharacteristicPack m_working;

    // The user norm set, layered exactly as the pack is.
    pinpoint::analysis::NormPack m_savedNorms;
    pinpoint::analysis::NormPack m_workingNorms;

    // Screens and drills, layered exactly as the pack is — saved layer, working layer, and the
    // assembly everything reads. They are flat sets rather than providers (screen_pack.h says why),
    // so the assembly is done here instead of by a merged provider, but the shape is the same one:
    // core, plus the user's own by id, rebuilt after every mutation.
    pinpoint::analysis::ScreenSet m_savedScreens;
    pinpoint::analysis::ScreenSet m_workingScreens;
    pinpoint::analysis::ScreenSet m_screens;
    pinpoint::analysis::DrillSet  m_savedDrills;
    pinpoint::analysis::DrillSet  m_workingDrills;
    pinpoint::analysis::DrillSet  m_drills;

    // Copy-on-write for the two flat sets, mirroring workingCondition() and friends: fetch the row
    // from the working layer, copying it out of the assembly on first touch. Null only when no such
    // screen/drill exists anywhere.
    pinpoint::analysis::Screen *workingScreen(const QString &id);
    pinpoint::analysis::Drill  *workingDrill(const QString &id);

    // The gate words (gate_words_pack.h), layered the same way again: what the user file holds,
    // the draft of it, and core with the draft over it by id — each list on its own, because a
    // gate's clause and a reading's words are two lists that share nothing but a file.
    //
    // A gate stays part of the analysis. These are the one thing about it a studio may change: the
    // words a client report prints when the gate keeps a reading from being checked (#60).
    pinpoint::analysis::GateWordSet m_savedGateWords;
    pinpoint::analysis::GateWordSet m_workingGateWords;
    pinpoint::analysis::GateWordSet m_gateWords;

    // Copy-on-write for one entry: the draft's own, else a copy of the assembled one, else — for a
    // catalogue gate that has no words anywhere yet — a new empty entry. `subject` picks the list.
    // Null only for an id that is neither a catalogue gate nor a known reading.
    pinpoint::analysis::GateWord *workingGateWord(const QString &id, bool subject);

    // "shipped" | "yours" | "both" for a gate or reading id, read off the gate-word layers alone.
    // Apart from sourceOf() because a gate id has no prefix to route on, and asking the pack about it
    // would be asking the wrong registry.
    QString gateWordsSourceOf(const QString &id) const;

    // The corridor rows this session has explicitly RETIRED — a reset of a user override back to
    // whatever core says. Tracked rather than inferred from absence, because "no user row" is also
    // the state of every corridor nobody has touched.
    QSet<QString> m_resetCorridors;

    std::vector<Command> m_stack;
    int                  m_stackIndex = -1;   // index of the last APPLIED command
    int                  m_savedIndex = -1;   // stack position the file on disk corresponds to

    // types() — the rail, with a row count per type, built by asking every type's row list — is
    // kept until the next modelChanged, which is the only thing its readers re-read it on. Without
    // it every QML read recomputed all twelve lists: `types.length` and `types[i]` in a loop are a
    // read each, and following the focused swing (setCurrentSwingDir → modelChanged) on every shot
    // change, session open and return to Capture spent 1–3 s here, with this screen not even shown
    // (#173 follow-up: 561 of 642 main-thread samples in types()). Cleared in the constructor's first
    // connection to modelChanged, so it is gone before any reader hears the signal — except on a
    // change to the focused swing alone (emitSwingModelChanged), which moves no count.
    mutable QList<ModelTypeEntry> m_typesCache;
    mutable bool         m_typesValid = false;
    bool                 m_swingOnlyChange = false;   // set across emitSwingModelChanged()
    void emitSwingModelChanged();

    // Recomputed lazily; both are cleared by invalidateDerived() on every mutation.
    mutable QSet<QString> m_dirtyIds;
    mutable bool          m_dirtyIdsValid = false;

    // The readings behind the corridor picture. Held for ONE measure at a time: the scan is a walk
    // over the whole library, and keeping every measure's readings would be a cache nobody asked
    // for and nobody could invalidate.
    QString                          m_policyName;
    QString                          m_libraryRoot;
    QString                          m_corridorScanned;
    std::vector<double>              m_corridorValues;
    bool                             m_corridorScanning = false;
    QFutureWatcher<QVariantList>    *m_corridorWatcher  = nullptr;
    void onCorridorScanFinished();

    // ── The loaded swing's readings ─────────────────────────────────────────
    //
    // The GRID, not the measure values — the same choice measure_sample.h argues for and for the
    // identical reason, one level in: the pack here is not merely editable, it is being edited, so
    // a cache of "measureId -> value" would be stale on every keystroke that changes a reducer. The
    // grid is a property of the SWING and survives every edit; reducing it is cheap enough to do
    // per row.
    QString                          m_swingDir;
    QString                          m_swingContextId;   // derived from the swing's club
    QString                          m_athleteDob;       // ISO, empty when unknown
    QString                          m_athleteSex;       // "male" | "female" | "declined" | ""

    // The athlete's cohort ON THE DAY OF THIS SWING. Derived, never stored on the swing: the grid
    // carries the wallclock, the record carries the date of birth, and the band is a reading of the
    // two taken together. Unqualified whenever either is unknown — which yields exactly one probe,
    // so a golfer we know nothing about costs precisely what everybody cost before cohorts existed.
    pinpoint::analysis::Cohort swingCohort() const;
    pinpoint::analysis::SwingPhaseGrid m_swingGrid;
    bool                             m_swingLoading = false;
    QFutureWatcher<pinpoint::analysis::SwingPhaseGrid> *m_swingWatcher = nullptr;
    void onSwingGridLoaded();

    // ── The Gates view's library count ──────────────────────────────────────
    // This panel's copy of the count; the process keeps the last one per library (model_browser.cpp,
    // gateCountCache) so a reopened panel shows numbers at once while it counts again.
    ModelGateCounts                  m_gateCounts;
    bool                             m_gatesScanning = false;
    bool                             m_gateScanWanted = false;  // the view has asked once
    QString                          m_gateWalked;              // the library this panel has counted
    QFutureWatcher<ModelGateCounts> *m_gateWatcher = nullptr;
    void onGateScanFinished();

    // This swing's reading for one measure, already reduced. nullopt means the swing cannot produce
    // it — no such metric, or a phase the reducer needs was never segmented — which the column
    // renders as a dash and NEVER as a zero (measure_sample.h's rule, and the whole module's).
    std::optional<double> swingValueFor(const pinpoint::analysis::Measure &m) const;

    // The reading as a CELL: the number, and the one thing the colour says — inside the corridor or
    // outside it.
    //
    // Two tones and no more, which is a deliberate narrowing of a four-band grade. This column is
    // read by SCANNING it, next to a library the author is editing; Ideal-vs-Good is a distinction
    // about the corridor's shape, not about the swing, and rendering it here would make every
    // ordinary reading carry a colour and the column stop meaning anything. `warn` is therefore
    // exactly `isDeviation()` — Watch or Action — which is the app's own answer to "did something
    // deviate", shared rather than redefined.
    //
    // Three states are NOT deviations and must not be coloured as though they were: no value at
    // all, no corridor authored to judge it against, and a reading the norm refuses to believe
    // (norm.h's plausibility rule — grading a mis-tracked ball in EITHER direction launders a
    // capture fault into a diagnosis). Each of them renders muted, not warned.
    ModelCell   swingCell(const pinpoint::analysis::Measure &m,
                          const std::optional<double>      &value) const;

    // The tone alone, so the table cell and the inspector's corridor pill reach ONE grading rule.
    // "" ordinary · "warn" outside · "dim" not graded (no value, no corridor, or a reading the norm
    // refuses to believe).
    QString swingTone(const pinpoint::analysis::Measure &m,
                      const std::optional<double>      &value) const;

    // How far outside the corridor the reading is, in tolerances — the swing column's sort key.
    // Negative when there is nothing to rank (no value, or no corridor), so those sort below every
    // graded row rather than above them.
    double swingDeviation(const pinpoint::analysis::Measure &m,
                          const std::optional<double>      &value) const;

    // Does the measures table carry the swing column right now? ONE answer, because columns() and
    // rows() build POSITIONAL arrays against each other — a column list and a cell list that
    // disagree about this is the transposition bug the developer guide calls invisible to any check
    // that counts.
    bool showSwingColumn() const { return !m_swingGrid.isEmpty(); }
};
