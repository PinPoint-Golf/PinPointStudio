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

// Does the inspector actually REDRAW after a write?
//
// model_browser_test proves the façade answers correctly — inspect() returns the new cause the
// instant addLink() lands. That is one link in a chain of four, and the other three are QML:
//
//   1. the command emits modelChanged()                        (C++, covered there)
//   2. the panel's Connections bumps `_revision`                 ← here
//   3. `_inspectorDetail` re-evaluates because it reads it       ← here
//   4. the value crosses into ModelInspector's own `var detail`  ← here
//
// Every one of those is invisible in a unit test of the façade and invisible in a screenshot of a
// pane that happens to be right. The failure they produce — a details pane showing yesterday's
// answer while the table shows today's — is exactly the failure this panel's premise cannot afford,
// because "no modals, no Edit button, selecting a row IS opening its editor" only works if what is
// on screen is what is in the model.
//
// So this builds the REAL chain in a QQmlEngine: a revision counter driven by the same signal, a
// binding written the same way, and a second component taking the value through a `var` property of
// its own exactly as ModelInspector does. Then it writes and asks what QML thinks.
//
//   cmake --build build/analyzer-tests --target inspector_refresh_test
//   ctest --test-dir build/analyzer-tests -R inspector_refresh --output-on-failure

#include "model_browser.h"
#include "model_browser_test_support.h"
#include "../../Stores/rule_data_install.h"
#include "../../Stores/rule_data_store.h"   // the shipped pack factory, for the typed-value checks
#include "../../DomainModel/rule_data.h"   // the packs and norms: RuleData's snapshot (refactor step 3.2)

#include "../content/pack_provider.h"
#include "../content/norm_provider.h"
#include "../../Gui/diagnosticmodel/layout/corridor_plot.h"
#include "../content/drill_pack.h"
#include "../content/screen_pack.h"
#include "../model/anatomy_vocabulary.h"
#include "../model/measure_facets.h"

#include <QCoreApplication>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using namespace pinpoint::analysis;
using model_browser_test::asMap;
using model_browser_test::asMaps;
using model_browser_test::graphOptions;
using model_browser_test::plotOptions;
using model_browser_test::rowQuery;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// The panel's chain, written the way DiagnosticModel.qml writes it. Deliberately a COPY of the
// idiom rather than an import of the real file: the real one needs the whole app module, and what
// is under test is the idiom — a Q_INVOKABLE result, nudged by a signal, handed across a component
// boundary through a `var` property.
static const char *kQml = R"QML(
import QtQml
import QtQuick
import Probe

Item {
    id: root

    property ModelBrowser browser: ModelBrowser {}

    // "The façade is read through Q_INVOKABLEs, which are not properties and so cannot be bound to.
    //  This is nudged by modelChanged and every list binding depends on it."
    property int revision: 0
    property Connections conn: Connections {
        target: root.browser
        function onModelChanged() { root.revision++ }
    }

    property string selectedType: "characteristics"
    property string selectedId:   ""

    readonly property var inspectorDetail: {
        root.revision
        if (root.selectedId === "") return ({})
        return root.browser.inspect(root.selectedType, root.selectedId)
    }

    // The hop that only the inspector makes: the map crosses into another component's own `var`
    // property, and everything that pane draws is read back off THAT.
    property Item pane: Item {
        property var detail: root.inspectorDetail

        readonly property string title: detail.label || ""

        // What the map SAYS the pane should hold.
        readonly property int expectedRows: {
            var n = 0
            var s = detail.sections || []
            for (var i = 0; i < s.length; i++) n += (s[i].rows || []).length
            return n
        }

        // …and what the delegate tree ACTUALLY instantiated. ModelInspector nests a Repeater over
        // `detail.sections` inside which a second Repeater runs over that section's `rows`, with
        // `modelData` passed down as a required property. A map that updates while the delegates do
        // not is a stale pane, and it is invisible to any assertion that reads the map instead of
        // counting what was built — which is the whole point of doing this in a live QQmlEngine.
        property int builtRows: 0

        Repeater {
            model: pane.detail.sections || []
            delegate: Item {
                id: sectionItem
                required property var modelData

                Repeater {
                    model: sectionItem.modelData.rows
                    delegate: Item {
                        required property var modelData
                        // Counted on construction and uncounted on destruction, so the total tracks
                        // the live delegate set rather than how many were ever made.
                        Component.onCompleted:  pane.builtRows++
                        Component.onDestruction: pane.builtRows--
                    }
                }
            }
        }
    }
}
)QML";

// THE TYPED VALUES, READ BY QML (refactor step 3.9-ii). The editor hands QML Q_GADGET value types
// now, and two of their readers draw nothing a render can catch: the graph asks a drag's refusal set
// per candidate (ModelGraph.qml `_refusalOf`: a gadget held in a `var`, its Q_INVOKABLEs called, reset
// to null on release), and the mint reads its live preview (ModelMint.qml: `hasExactMatch`,
// `exactMatch.label`, `nearDuplicates.length`). The ring collar (RingMenu.qml) reads `current` off
// ringValues(). Each is written here the way those files write it, and asked through QML.
static const char *kTypedQml = R"QML(
import QtQml
import Probe

QtObject {
    id: root
    property ModelBrowser browser: ModelBrowser {}

    // ModelGraph.qml's idiom.
    property var refusals: null
    function arm(fixed, ids, end) { root.refusals = root.browser.linkRefusals(fixed, ids, end) }
    function disarm() { root.refusals = null }
    function refusalOf(id) {
        return root.refusals && root.refusals.refuses(id) ? root.refusals.refusalOf(id) : null
    }
    function reasonOf(id) { var r = root.refusalOf(id); return r === null ? "<null>" : r.reason }
    function textOf(id)   { var r = root.refusalOf(id); return r === null ? "" : r.text }

    // ModelMint.qml's idiom.
    property string what: ""
    property string quantity: ""
    property string reference: ""
    property string anchor: "p1"
    readonly property var preview: root.browser.previewMeasure(root.what, root.quantity, root.reference,
                                                               root.anchor)
    readonly property bool   exactShown: root.preview.hasExactMatch === true
    readonly property string exactLabel: root.preview.hasExactMatch === true ? root.preview.exactMatch.label : ""
    readonly property bool   createEnabled: root.preview.valid === true && root.preview.hasExactMatch !== true
    readonly property string previewText: root.preview.valid === true ? root.preview.label
                                                                      : (root.preview.reason || "")
    readonly property int    nearCount: root.preview.nearDuplicates !== undefined
                                        ? root.preview.nearDuplicates.length : -1

    // RingMenu.qml's idiom: the cell marked current.
    function ringCurrent(type, id, field) {
        var v = root.browser.ringValues(type, id, field)
        for (var i = 0; i < v.length; i++) if (v[i].current === true) return v[i].value
        return ""
    }
    function ringSize(type, id, field) { return root.browser.ringValues(type, id, field).length }

    // The second run (3.9-ii): what QML passes IN is a plain object, converted to a structured gadget.
    // DiagnosticModel.qml's rows() idiom — the facet picks held as { key: [values] }, passed as a list.
    function idsOf(rows) { var out = []; for (var i = 0; i < rows.length; i++) out.push(rows[i].id); return out.join(",") }
    function rowsFaceted(type, key, value, sort) {
        var facets = {}
        facets[key] = [ value ]
        return root.idsOf(root.browser.rows(type, { sort: sort, descending: true,
                                                     facets: Object.keys(facets).map(
                                                         k => ({ key: k, values: facets[k] })),
                                                     search: "" }))
    }
    function rowsByIds(type, a, b) { return root.idsOf(root.browser.rows(type, { ids: [ a, b ] })) }
    function rowsAll(type) { return root.idsOf(root.browser.rows(type, {})) }
    // The graph call's idiom: a metrics object, the bound and the switches.
    function graphShape(type, id, depth) {
        var g = root.browser.graph(type, id, { nodeH: 34, gapX: 165, gapY: 39, depth: depth,
                                               expanded: [], includeMeasures: false,
                                               includeReferences: false, includeScreened: true,
                                               hideWeak: false, hideProposed: false })
        var linked = 0
        for (var i = 0; i < g.edges.length; i++) if (!!g.edges[i].rowId) linked++
        return g.nodes.length + "/" + g.edges.length + "/" + linked + "/" + Math.round(g.width)
    }
    function graphDefaultWidth(type, id) { return String(Math.round(root.browser.graph(type, id, {}).width)) }
    // ModelCorridorPlot.qml's idiom: the canvas size, and a drag's preview only while dragging.
    function plotAt(m, c, previewMu) {
        var o = { width: 400, height: 150 }
        if (!isNaN(previewMu)) o.previewMu = previewMu
        var p = root.browser.corridorPlot(m, c, o)
        return p.found === true ? p.mu + "/" + Math.round(p.width) : "not found"
    }
    function plotDefaultWidth(m, c) { var p = root.browser.corridorPlot(m, c, {}); return String(Math.round(p.width)) }
    // ModelInspector.qml's idiom: a report field is checked live; an ordinary field is not.
    function checkedFields(type, id, title) {
        var d = root.browser.inspect(type, id), n = 0, all = 0
        for (var s = 0; s < d.sections.length; s++) {
            if (d.sections[s].title !== title) continue
            for (var r = 0; r < d.sections[s].rows.length; r++) {
                all++
                if (d.sections[s].rows[r].wordingChecked === true) n++
            }
        }
        return n + "/" + all
    }
}
)QML";

static QString callString(QObject *o, const char *fn, const QVariant &a = {}, const QVariant &b = {},
                          const QVariant &c = {})
{
    QVariant out;
    const bool ok = c.isValid() ? QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, a),
                                                            Q_ARG(QVariant, b), Q_ARG(QVariant, c))
                  : QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, a));
    return ok ? out.toString() : QStringLiteral("<call failed>");
}

static void typedValuesThroughQml(QQmlEngine &engine)
{
    std::printf("=== the typed values, read by QML (3.9-ii) ===\n");
    QQmlComponent component(&engine);
    component.setData(QByteArray(kTypedQml), QUrl(QStringLiteral("typed.qml")));
    if (component.isError()) {
        for (const QQmlError &e : component.errors())
            std::printf("  QML ERROR: %s\n", qPrintable(e.toString()));
        check(false, "the typed-value probe loads");
        return;
    }
    std::unique_ptr<QObject> root(component.create());
    check(root != nullptr, "the typed-value probe loads");
    if (!root) return;
    auto *browser = root->property("browser").value<ModelBrowser *>();
    if (!browser) { check(false, "and reaches its façade"); return; }

    // A drag held on a condition that already has a cause: the condition itself is refused as self,
    // its existing cause as already linked, and a legal candidate answers null.
    const CharacteristicPack &p = *[&]() { static auto core = makeResourcePackProvider(); return &core->pack(); }();
    QString effect, cause;
    for (const Edge &e : p.edges)
        if (e.type == EdgeType::Causes) { effect = e.to; cause = e.from; break; }
    const auto legalList = browser->linkCandidates(QStringLiteral("causes"), effect, QString(), QStringLiteral("to"));
    const QString legal = legalList.isEmpty() ? QString() : legalList.first().id;
    check(!effect.isEmpty() && !legal.isEmpty(), "a held condition, its cause and a legal candidate");
    const QVariant ids = QStringList{ effect, cause, legal };
    QMetaObject::invokeMethod(root.get(), "arm", Q_ARG(QVariant, effect), Q_ARG(QVariant, ids),
                              Q_ARG(QVariant, QStringLiteral("to")));
    check(callString(root.get(), "reasonOf", effect) == QStringLiteral("self"),
          "QML reads the held condition's refusal as self");
    check(callString(root.get(), "reasonOf", cause) == QStringLiteral("exists")
              && !callString(root.get(), "textOf", cause).isEmpty(),
          "its existing cause as already linked, with the words to show");
    check(callString(root.get(), "reasonOf", legal) == QStringLiteral("<null>"),
          "and a legal candidate as no refusal at all");
    QMetaObject::invokeMethod(root.get(), "disarm");
    check(callString(root.get(), "reasonOf", effect) == QStringLiteral("<null>"),
          "released, the drag refuses nothing");

    // The mint, on a shipped measure's own facets: the exact match shows and Create is off.
    const Measure *existing = nullptr;
    for (const Measure &m : p.measures)
        if (m.kind == MeasureKind::Composed && m.reducer.kind == ReducerKind::At && m.reducer.anchor.has_value()) {
            existing = &m;
            break;
        }
    check(existing != nullptr, "a shipped composed measure to mint against");
    if (existing) {
        root->setProperty("what", roleName(existing->series.what));
        root->setProperty("quantity", quantityName(existing->series.quantity));
        root->setProperty("reference", roleName(existing->series.reference));
        root->setProperty("anchor", phaseToken(existing->reducer.anchor.value_or(Phase::Address)));
        check(root->property("exactShown").toBool(), "QML shows the mint's exact match");
        check(root->property("exactLabel").toString() == measureDisplayLabel(*existing),
              "naming the measure it already is");
        check(!root->property("createEnabled").toBool(), "and Create is off");
        check(root->property("nearCount").toInt() >= 0, "the near-duplicates read as a list");
        root->setProperty("what", QStringLiteral("nonsense"));
        check(!root->property("exactShown").toBool() && root->property("createEnabled").toBool() == false
                  && !root->property("previewText").toString().isEmpty(),
              "an unknown role shows its reason, no match, and Create stays off");
    }

    // The ring collar marks the strength a link holds.
    QString linkId, strength;
    for (const Edge &e : p.edges)
        if (e.type == EdgeType::Causes && e.from == cause && e.to == effect) {
            linkId   = cause + QStringLiteral("|") + effect + QStringLiteral("|causes");
            strength = strengthName(e.strength);
            break;
        }
    check(!linkId.isEmpty(), "a causal link for the collar");
    check(callString(root.get(), "ringSize", QStringLiteral("links"), linkId, QStringLiteral("strength")) == QStringLiteral("5")
              && callString(root.get(), "ringCurrent", QStringLiteral("links"), linkId, QStringLiteral("strength"))
                     == strength,
          "QML reads five collar cells and the one marked current is the link's strength");

    // ── What QML passes IN (3.9-ii, second run) ──────────────────────────────
    std::printf("=== plain objects into the typed arguments (3.9-ii) ===\n");
    const auto joinIds = [](const QList<ModelRow> &rows) {
        QStringList ids;
        for (const ModelRow &r : rows) ids << r.id;
        return ids.join(QLatin1Char(','));
    };
    const QString chars = QStringLiteral("characteristics");
    const QList<ModelFacet> fs = browser->facets(chars);
    QString facetKey, facetValue;
    for (const ModelFacet &f : fs)
        if (f.kind == QStringLiteral("value") && f.options.size() > 1) {
            facetKey   = f.key;
            facetValue = f.options.at(1).value;
            break;
        }
    check(!facetKey.isEmpty(), "a value facet with a second option to pick");
    ModelRowQuery q;
    q.sort       = QStringLiteral("name");
    q.descending = true;
    ModelFacetPick pick;
    pick.key    = facetKey;
    pick.values = QStringList{ facetValue };
    q.facets.append(pick);
    const QString wantFaceted = joinIds(browser->rows(chars, q));
    QVariant got;
    QMetaObject::invokeMethod(root.get(), "rowsFaceted", Q_RETURN_ARG(QVariant, got), Q_ARG(QVariant, chars),
                              Q_ARG(QVariant, facetKey), Q_ARG(QVariant, facetValue),
                              Q_ARG(QVariant, QStringLiteral("name")));
    check(!wantFaceted.isEmpty() && got.toString() == wantFaceted
              && wantFaceted != joinIds(browser->rows(chars)),
          "the panel's rows() object — sort, direction and the facet picks as a list — reaches the query");
    const QList<ModelRow> all = browser->rows(chars);
    check(all.size() > 2, "rows to pick by id");
    if (all.size() > 2) {
        ModelRowQuery byIds;
        byIds.ids = QStringList{ all.at(2).id, all.at(0).id };
        check(callString(root.get(), "rowsByIds", chars, all.at(2).id, all.at(0).id) == joinIds(browser->rows(chars, byIds))
                  && joinIds(browser->rows(chars, byIds)).split(QLatin1Char(',')).size() == 2,
              "an ids-only object keeps exactly those rows");
        check(callString(root.get(), "rowsAll", chars) == joinIds(all), "and an empty object is the default query");
    }

    ModelGraphOptions go;
    go.nodeH = 34; go.gapX = 165; go.gapY = 39; go.depth = 1;
    go.includeMeasures = false; go.includeReferences = false; go.includeScreened = true;
    const ModelGraph g1 = browser->graph(chars, effect, go);
    int linked = 0;
    for (const ModelGraphEdge &e : g1.edges) if (!e.rowId.isEmpty()) ++linked;
    const QString wantGraph = QStringLiteral("%1/%2/%3/%4").arg(g1.nodes.size()).arg(g1.edges.size()).arg(linked)
                                  .arg(qRound(g1.width));
    QMetaObject::invokeMethod(root.get(), "graphShape", Q_RETURN_ARG(QVariant, got), Q_ARG(QVariant, chars),
                              Q_ARG(QVariant, effect), Q_ARG(QVariant, 1));
    check(!g1.nodes.isEmpty() && got.toString() == wantGraph,
          "the graph's options object lays out exactly as the typed options do, links marked by rowId");
    QMetaObject::invokeMethod(root.get(), "graphDefaultWidth", Q_RETURN_ARG(QVariant, got), Q_ARG(QVariant, chars),
                              Q_ARG(QVariant, effect));
    check(got.toString() == QString::number(qRound(browser->graph(chars, effect).width)),
          "and an empty one keeps the layout's defaults");

    QString plotMeasure, plotContext;
    for (const ModelRow &r : browser->rows(QStringLiteral("corridors")))
        if (!r.measureId.isEmpty()) { plotMeasure = r.measureId; plotContext = r.contextId; break; }
    check(!plotMeasure.isEmpty(), "a corridor to plot");
    if (!plotMeasure.isEmpty()) {
        ModelCorridorPlotOptions po;
        po.width = 400; po.height = 150;
        const ModelCorridorPlot rest = browser->corridorPlot(plotMeasure, plotContext, po);
        QMetaObject::invokeMethod(root.get(), "plotAt", Q_RETURN_ARG(QVariant, got), Q_ARG(QVariant, plotMeasure),
                                  Q_ARG(QVariant, plotContext), Q_ARG(QVariant, qQNaN()));
        // JS prints a number shortest-round-trip; so does FloatingPointShortest.
        const auto js = [](double v) { return QString::number(v, 'g', QLocale::FloatingPointShortest); };
        check(rest.found && got.toString() == QStringLiteral("%1/400").arg(js(rest.mu)),
              "the plot's size object lays out at that size, at the stored centre");
        QMetaObject::invokeMethod(root.get(), "plotAt", Q_RETURN_ARG(QVariant, got), Q_ARG(QVariant, plotMeasure),
                                  Q_ARG(QVariant, plotContext), Q_ARG(QVariant, rest.mu + 1.5));
        check(got.toString() == QStringLiteral("%1/400").arg(js(rest.mu + 1.5)),
              "a drag's previewMu, given only while dragging, moves the centre");
        QMetaObject::invokeMethod(root.get(), "plotDefaultWidth", Q_RETURN_ARG(QVariant, got),
                                  Q_ARG(QVariant, plotMeasure), Q_ARG(QVariant, plotContext));
        check(got.toString() == QString::number(qRound(CorridorPlotOptions{}.width)),
              "and an empty object keeps CorridorPlotOptions' size");
    }

    const QString habit = QStringLiteral("s_posture");
    const QString reportChecked = callString(root.get(), "checkedFields", chars, habit, QStringLiteral("Report"));
    const QStringList rc = reportChecked.split(QLatin1Char('/'));
    check(rc.size() == 2 && rc.at(0) == rc.at(1) && rc.at(0).toInt() > 0,
          "QML reads every report field as checked live");
    check(callString(root.get(), "checkedFields", chars, habit, QStringLiteral("Fields")).startsWith(QStringLiteral("0/")),
          "and no ordinary field");
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    pinpoint::installProcessRuleData();   // this process's packs and norms (refactor step 3.2)

    QFile::remove(userPackPath());
    QFile::remove(userNormPath());
    QFile::remove(userScreenSetPath());
    QFile::remove(userDrillSetPath());
    // The browser reads RuleData's snapshot, never the files (refactor step 3.9): read them again.
    pinpoint::RuleData::installed()->reload();

    qmlRegisterType<ModelBrowser>("Probe", 1, 0, "ModelBrowser");
    // The editor's value types (model_browser.h; the app registers them through its QML module).
    qmlRegisterTypesAndRevisions<ModelLinkRefusals, ModelLinkRefusal, ModelMeasurePreview, ModelCandidate,
                                 ModelOption>("Probe", 1);
    qmlRegisterTypesAndRevisions<ModelColumn, ModelCell, ModelRow, ModelFacetOption, ModelFacet, ModelFacetPick,
                                 ModelRowQuery, ModelFieldIssue, ModelInspectorRow, ModelInspectorSection,
                                 ModelInspection>("Probe", 1);
    qmlRegisterTypesAndRevisions<ModelGraphMeasure, ModelGraphReference, ModelGraphNode, ModelGraphEdge,
                                 ModelGraphHeading, ModelGraph, ModelGraphOptions, ModelPlotPoint, ModelPlotBand,
                                 ModelCorridorPlot, ModelCorridorPlotOptions>("Probe", 1);

    QQmlEngine    engine;
    QQmlComponent component(&engine);
    component.setData(QByteArray(kQml), QUrl(QStringLiteral("probe.qml")));
    if (component.isError()) {
        for (const QQmlError &e : component.errors())
            std::printf("  QML ERROR: %s\n", qPrintable(e.toString()));
        std::printf("\nFAIL (component would not load)\n");
        return 1;
    }

    QObject *root = component.create();
    check(root != nullptr, "the panel's binding chain loads");
    if (!root) return 1;

    auto *browser = root->property("browser").value<ModelBrowser *>();
    check(browser != nullptr, "and the façade is reachable from it");
    if (!browser) return 1;

    QObject *pane = root->property("pane").value<QObject *>();
    check(pane != nullptr, "and the pane that reads it");
    if (!pane) return 1;

    const auto rowCount = [pane] { return pane->property("expectedRows").toInt(); };
    const auto builtRows = [pane] { return pane->property("builtRows").toInt(); };
    const auto title    = [pane] { return pane->property("title").toString(); };

    // Somewhere with room for one more cause.
    QString target, cause;
    for (const QVariant &v : asMaps(browser->rows(QStringLiteral("characteristics")))) {
        const QString      id    = v.toMap().value(QStringLiteral("id")).toString();
        const auto cands = browser->linkCandidates(QStringLiteral("causes"), id);
        if (cands.isEmpty()) continue;
        target = id;
        cause  = cands.first().id;
        break;
    }
    check(!target.isEmpty(), "there is a characteristic to add a cause to");

    root->setProperty("selectedId", target);
    const int before = rowCount();
    check(before > 0, "selecting it fills the pane");
    check(builtRows() == before, "and the delegate tree built exactly those rows");

    // ── The thing that was reported ─────────────────────────────────────────
    check(browser->addLink(cause, target, QStringLiteral("causes"))
              .ok, "a cause is added");
    check(rowCount() == before + 1, "and the PANE redraws with one more row");
    check(builtRows() == before + 1, "and the DELEGATES were rebuilt to match — not just the map");

    check(browser->removeLink(cause, target, QStringLiteral("causes"))
              .ok, "removing it succeeds");
    check(rowCount() == before, "and the pane follows that too");
    check(builtRows() == before, "delegates included");

    // A field edit has to reach the pane's header, not only its rows.
    const QString renamed = QStringLiteral("Renamed while the pane was open");
    check(browser->setField(QStringLiteral("characteristics"), target, QStringLiteral("label"),
                            renamed).ok, "a rename lands");
    check(title() == renamed, "and the pane's title is the new name");

    check(browser->undo().ok, "undo puts it back");
    check(title() != renamed, "and the pane follows the undo");

    // Undo/redo are writes like any other as far as the pane is concerned.
    check(browser->redo().ok, "redo re-applies it");
    check(title() == renamed, "and the pane follows the redo");
    while (browser->canUndo()) browser->undo();

    // ── An instrument ladder redraws the measure's own pane ─────────────────
    //
    // Same question as the screen case below, on the verb added with Measure::preferKeys: the write
    // goes to the measure in front of the reader, so if the binding chain is right the "Prefers"
    // section grows a row without anything else being touched. A delta, never an absolute — the
    // section list will keep growing and a pinned row count would be wrong the first time it did.
    {
        const QString aa = QStringLiteral("m_lowPointAhead");   // m_attackAngle lost its ladder 2026-09-14
        root->setProperty("selectedType", QStringLiteral("measures"));
        root->setProperty("selectedId", aa);
        const int before = rowCount();
        if (before > 0) {
            check(browser->removePreferKey(aa, QStringLiteral("lm.lowPointAhead"))
                      .ok,
                  "a rung comes off the ladder");
            check(rowCount() == before - 1, "and the measure's pane loses the row");
            check(browser->undo().ok, "undo puts it back");
            check(rowCount() == before, "and the pane follows the undo");
        }
    }

    // ── The same question for the panes added in ADDENDUM-02 ────────────────
    const QVariantList screens = asMaps(browser->rows(QStringLiteral("screens")));
    if (!screens.isEmpty()) {
        const QString screenId = screens.first().toMap().value(QStringLiteral("id")).toString();
        root->setProperty("selectedType", QStringLiteral("screens"));
        root->setProperty("selectedId", screenId);
        const int settlesBefore = rowCount();

        const auto cands = browser->screenCandidates(screenId);
        if (!cands.isEmpty()) {
            const QString cond = cands.first().id;
            check(browser->addScreenSettles(screenId, cond).ok,
                  "a screen is made to settle a characteristic");
            check(rowCount() == settlesBefore + 1,
                  "and the SCREEN's pane redraws, though the write went to the condition");
            check(browser->undo().ok, "undo detaches it");
            check(rowCount() == settlesBefore, "and the pane follows the undo");
        }

        // Editing a field of the screen itself.
        check(browser->setField(QStringLiteral("screens"), screenId, QStringLiteral("name"),
                                QStringLiteral("Renamed screen"))
                  .ok, "a screen renames");
        check(title() == QStringLiteral("Renamed screen"), "and its pane's title follows");
        while (browser->canUndo()) browser->undo();
    }

    const QVariantList drills = asMaps(browser->rows(QStringLiteral("drills")));
    if (!drills.isEmpty()) {
        const QString drillId = drills.first().toMap().value(QStringLiteral("id")).toString();
        root->setProperty("selectedType", QStringLiteral("drills"));
        root->setProperty("selectedId", drillId);
        const int answersBefore = rowCount();

        const auto cands = browser->drillCandidates(drillId);
        if (!cands.isEmpty()) {
            const QString cond = cands.first().id;
            check(browser->addDrillAnswers(drillId, cond).ok,
                  "a drill is made to answer a characteristic");
            check(rowCount() == answersBefore + 1, "and the DRILL's pane redraws");
            check(browser->undo().ok, "undo detaches it");
            check(rowCount() == answersBefore, "and the pane follows the undo");
        }
    }

    // A corridor is edited through the plot, which reads a different binding — but its pane's rows
    // come through the same one.
    const QVariantList corridors = asMaps(browser->rows(QStringLiteral("corridors")));
    if (!corridors.isEmpty()) {
        const QString cid = corridors.first().toMap().value(QStringLiteral("id")).toString();
        root->setProperty("selectedType", QStringLiteral("corridors"));
        root->setProperty("selectedId", cid);
        const QString titleBefore = title();
        check(browser->setField(QStringLiteral("corridors"), cid, QStringLiteral("source"),
                                QStringLiteral("heuristic")).ok,
              "a corridor's source is set");
        check(rowCount() > 0, "and its pane still answers");
        Q_UNUSED(titleBefore)
        while (browser->canUndo()) browser->undo();
    }

    delete root;

    typedValuesThroughQml(engine);

    QFile::remove(userPackPath());
    QFile::remove(userNormPath());
    QFile::remove(userScreenSetPath());
    QFile::remove(userDrillSetPath());

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail,
                g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
