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

// Architecture lint — docs/implementation/swe-audit/03-target-architecture.md §2 (the rings) and §11
// (enforcement); refactor Step G (04-remediation-plan.md §3).
//
// The refactor moves the code towards the target architecture one step at a time. This test is what
// stops the old shapes coming back while it does: it reads the SOURCE of everything under src/ (C++ and
// QML; tests/, Testing/, third_party/ and build/ are skipped) and fails on:
//
//   A1  an #include "…" from an inner ring into an outer one. The rings, innermost first:
//         0 Platform     logging, profiling, machine queries — any ring may include these (see below)
//         1 Vocabulary   value types, ids, metric definitions + catalogue, club vocabulary, units, and
//                        the Recording contract's types (§0.1)
//         2 Rules        pure measurement algorithms, detection, diagnostics engine + packs/norms,
//                        coaching rules
//         3 DomainModel  src/DomainModel (created in step 1 for RecordingRepository)
//         4 Application  orchestration: the analysis pipeline, re-analysis, AppState, use cases
//         5 Adapters     devices, the Recording store and other persistence, UI (all of src/Gui)
//       A file may include its own ring or any ring inside it, never one outside.
//   A2  QML creating a C++ type that is session- or athlete-scoped (scopedTypeRe() below — from
//       data/seam_inventory.md). Its owner is the domain/application (03 §9), never a view.
//   A3  folder-name parsing — the shot number after the last '_' (lastIndexOf / section / split), the
//       date in a folder name's first 10 characters or by a \d{4}-\d{2}-\d{2} regex, a "swing_" prefix
//       test or a "swing_*" name glob — outside SwingPaths and (once it exists) SessionIdentity.
//   A4  a session or athlete file name spelled as a literal ("diagnostics.json", "work_ons.json", …)
//       outside the one file that owns it (ownedFiles() below; 03 §7 "one door per file").
//   A5  file I/O (QFile, QSaveFile, QDir, QFileInfo, QDirIterator, QTemporaryFile, std::fstream,
//       fopen) in a rules directory: src/Coaching, src/Diagnostics/engine|norms|measures, and the
//       Rules-ring files of src/Analysis. One finding per line that names one. (Diagnostics/packs is
//       left out for now: its loader is step 3's.)
//   A6  a Q_INVOKABLE that returns or takes a QVariantMap / QVariantList, or a Q_PROPERTY of that
//       type, or a public slot / signal that carries one, in any header under src/ (the QML boundary,
//       03 §9: values become Q_GADGETs).
//   A7  a C++ function longer than 300 lines in rings 1–2 (Vocabulary, Rules). The function finder is
//       docs/implementation/swe-audit/tools/size_table.py's brace-depth heuristic ported to C++ (a '{'
//       after `…)` [const|noexcept|override|final] that is not a control statement, lambda or
//       initialiser, outside any other function body; length = the lines from its '{' to its '}'). It reads
//       the signature back to the previous statement boundary rather than size_table.py's fixed 240
//       characters, which missed long signatures.
//       Ported, not called: this test must run wherever the tests tree is built, with no Python.
//   A8  src/Analysis reaching past the Recording contract (§0.1) into Acquisition — the ring buffer,
//       camera / IMU / launch-monitor device classes — or into application state (any src/Gui header,
//       the settings file, the studio profile).
//
// THE RING TABLE. A file's ring is its directory's (dirRings(), longest prefix wins) unless it is
// named in fileRings(), which exists because four directories straddle rings today (03 §2) — Analysis,
// Core, LaunchMonitor, Export — and three more hold a device half or a Recording-contract half
// (Buffer, IMU, Pose); and 03 §2 puts the capture and session services in src/Gui/shot in
// Application (ShotProcessor, ShotController, ReanalysisController). Files are named by their path under src/ without the extension, so the .h and
// the .cpp share one entry. A NEW file defaults to its directory's ring; a file in a directory that is
// in neither table fails the test, so a new directory (src/Recording, say) has to be
// placed on purpose. Platform (ring 0) is a decision of this test, not of 03: logging and profiling
// are used by every ring and no step removes them, so they sit inside Vocabulary.
//
// THE ALLOW-LIST. Today's violations live in architecture_allowlist.txt beside this file, generated
// once from the tree at the start of Step G — one line per file and rule, with the number of findings
// allowed, WHY, and the refactor step that removes it (`# step 3`; `# unplanned` when no step 1-8
// does — those go to Mark; `# kept` for a deliberate one). A file that grows past its count fails; one that
// drops below it is reported so the count can be lowered. The list can only shrink: adding a NEW
// entry is an architecture decision — ask Mark first.
//
// Before the scan, every rule runs on fixtures, so a rule that silently stops matching fails here
// rather than passing everything.
//
// KNOWN LIMITS (checked against the tree when they were written down; re-check before relying on one):
//   - A1 reads only `#include "…"`: a project header in <…> is not seen (if added, resolve by exact
//     path only — the basename fallback would map the HackMotion SDK's <wrist/types.h> onto
//     Buffer/types.h). An include whose basename is ambiguous resolves to nothing and is skipped.
//   - A2 misses a module-qualified creation (`X.SessionSeriesModel {`).
//   - A4 misses a file name built by concatenation ("club_baseline" + ".json").
//   - A5 does not look for std::filesystem or QStandardPaths.
//   - A7 misses trailing-return functions (`auto f() -> T {`), as size_table.py does.
//
//   cmake --build build/tests --target architecture_lint_test -j 6
//   ctest --test-dir build/tests -R '^architecture_lint_test$' --output-on-failure

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdio>

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

struct Finding {
    QString rule;
    int     line = 0;
    QString text;
};

// ── The rings ──────────────────────────────────────────────────────────────────────────────────
enum Ring { Platform = 0, Vocabulary = 1, Rules = 2, DomainModel = 3, Application = 4, Adapters = 5 };

const char *ringName(int r)
{
    static const char *n[] = { "Platform", "Vocabulary", "Rules", "DomainModel", "Application", "Adapters" };
    return r >= 0 && r <= 5 ? n[r] : "no ring";
}

// Directory → ring (03 §2 "Today's directories, mapped"). Longest prefix wins.
const QVector<QPair<QString, int>> &dirRings()
{
    static const QVector<QPair<QString, int>> t{
        { QStringLiteral("Metrics/"), Vocabulary },
        { QStringLiteral("Recording/"), Vocabulary },     // the Recording contract's types (step 1, IM-6)
        { QStringLiteral("Core/"), Vocabulary },          // device / logging files below
        { QStringLiteral("Pose/"), Rules },
        { QStringLiteral("Club/"), Rules },
        { QStringLiteral("Ball/"), Rules },
        { QStringLiteral("Body/"), Rules },
        { QStringLiteral("IMU/"), Rules },                // device back-ends below
        { QStringLiteral("Kinematics/"), Rules },
        { QStringLiteral("Wrist/"), Rules },
        { QStringLiteral("Skeleton3d/"), Rules },
        { QStringLiteral("Timeline/"), Rules },
        { QStringLiteral("Diagnostics/"), Rules },
        { QStringLiteral("Coaching/"), Rules },
        { QStringLiteral("DomainModel/"), DomainModel },  // repository interfaces (step 1, IM-6)
        { QStringLiteral("Analysis/"), Application },
        { QStringLiteral("App/"), Application },          // AppState's home in 03 §2 (not created yet)     // the stages' shared types and rules below
        { QStringLiteral("Buffer/"), Adapters },          // the Recording contract's stream types below
        { QStringLiteral("Video/"), Adapters },
        { QStringLiteral("Audio/"), Adapters },
        { QStringLiteral("Export/"), Adapters },          // the Recording store; frame decode below
        { QStringLiteral("LaunchMonitor/"), Adapters },   // connectors; readings and inferred reads below
        { QStringLiteral("Ppcp/"), Adapters },
        { QStringLiteral("LLM/"), Adapters },
        { QStringLiteral("STT/"), Adapters },
        { QStringLiteral("TTS/"), Adapters },
        { QStringLiteral("Secrets/"), Adapters },
        { QStringLiteral("Update/"), Adapters },
        { QStringLiteral("Gui/"), Adapters },             // presenters, QML, and today's app services
    };
    return t;
}

// The files whose ring is not their directory's. Path under src/ without the extension.
const QHash<QString, int> &fileRings()
{
    static const QHash<QString, int> t{
        // Core: logging, profiling and machine queries are Platform; settings, the studio profile,
        // app info and the device enumerator are adapters; the Kalman smoother is an algorithm.
        { QStringLiteral("Core/pp_debug"), Platform },
        { QStringLiteral("Core/pp_log_stream"), Platform },
        { QStringLiteral("Core/PpMessageLog"), Platform },
        { QStringLiteral("Core/PpStatsLog"), Platform },
        { QStringLiteral("Core/AnalysisProfileLog"), Platform },
        { QStringLiteral("Core/pp_profiler"), Platform },
        { QStringLiteral("Core/ort_log"), Platform },
        { QStringLiteral("Core/cpu_topology"), Platform },
        { QStringLiteral("Core/pp_gpu_metrics"), Platform },
        { QStringLiteral("Core/pp_gpu_metrics_mac"), Platform },
        { QStringLiteral("Core/pp_os_metrics"), Platform },
        { QStringLiteral("Core/pp_model_path"), Platform },
        { QStringLiteral("Core/kf3_smoother"), Rules },
        { QStringLiteral("Core/device_enumerator"), Adapters },
        { QStringLiteral("Core/app_info"), Adapters },
        { QStringLiteral("Core/pp_settings"), Adapters },
        { QStringLiteral("Core/studio_profile"), Adapters },
        // Analysis: the per-shot stages' shared vocabulary and their pure rules; reading a recorded MP4 and rebuilding products from a document are the store's.
        { QStringLiteral("Analysis/swing_analysis"), Vocabulary },
        { QStringLiteral("Analysis/gate_record"), Vocabulary },
        { QStringLiteral("Analysis/analysis_tuning"), Vocabulary },
        { QStringLiteral("Analysis/analysis_versions"), Vocabulary },
        { QStringLiteral("Analysis/uncertainty_config"), Vocabulary },
        { QStringLiteral("Analysis/uncertainty_json"), Vocabulary },
        { QStringLiteral("Analysis/segment_role"), Vocabulary },
        { QStringLiteral("Analysis/det_rng"), Vocabulary },
        { QStringLiteral("Analysis/shot_analysis_job"), Rules },      // the job carries SkeletonCalib (Rules)
        { QStringLiteral("Analysis/effective_context"), Rules },      // the effective-club rule (IM-3)
        { QStringLiteral("Analysis/analysis_job_builder"), Rules },   // the one job builder
        { QStringLiteral("Analysis/route_substitution"), Rules },
        { QStringLiteral("Analysis/seg_resolve"), Rules },
        { QStringLiteral("Analysis/stream_trim"), Rules },
        { QStringLiteral("Analysis/capture_integrity_check"), Rules },
        { QStringLiteral("Analysis/manifest_gates"), Rules },
        { QStringLiteral("Analysis/camera_pose_sticks"), Rules },
        { QStringLiteral("Analysis/frame_store"), Rules },
        { QStringLiteral("Analysis/mp4_frame_reader"), Adapters },
        { QStringLiteral("Analysis/recorded_products"), Adapters },
        // Export: one decode of a recorded frame is part of the Recording contract.
        { QStringLiteral("Export/frame_decode"), Vocabulary },
        // LaunchMonitor: a reading as delivered and what measured it are vocabulary; the inferred
        // reads, gates, pairing and reductions are rules; the connectors stay adapters.
        { QStringLiteral("LaunchMonitor/launch_monitor_reading"), Vocabulary },
        { QStringLiteral("LaunchMonitor/lm_device"), Vocabulary },
        { QStringLiteral("LaunchMonitor/lm_inferred_reads"), Rules },
        { QStringLiteral("LaunchMonitor/lm_flight_path"), Rules },
        { QStringLiteral("LaunchMonitor/lm_enclosing_ellipse"), Rules },
        { QStringLiteral("LaunchMonitor/lm_club_evidence"), Rules },
        { QStringLiteral("LaunchMonitor/lm_gates"), Rules },
        { QStringLiteral("LaunchMonitor/lm_session_reductions"), Rules },
        { QStringLiteral("LaunchMonitor/shot_pairing"), Rules },
        { QStringLiteral("LaunchMonitor/standalone_gate"), Rules },
        // Buffer: the streams a Recording carries are vocabulary; the ring itself is an adapter.
        { QStringLiteral("Buffer/types"), Vocabulary },
        { QStringLiteral("Buffer/format_descriptor"), Vocabulary },
        { QStringLiteral("Buffer/source_descriptor"), Vocabulary },
        { QStringLiteral("Buffer/swing_window"), Vocabulary },
        { QStringLiteral("Buffer/swing_payload_source"), Vocabulary },
        { QStringLiteral("Buffer/imu_sample"), Vocabulary },
        { QStringLiteral("Buffer/pixel_format_names"), Vocabulary },
        // IMU: the device back-ends (BLE, Witmotion, HackMotion session, the I/O thread) are adapters.
        { QStringLiteral("IMU/ble_adapter_pool"), Adapters },
        { QStringLiteral("IMU/ble_imu_transport"), Adapters },
        { QStringLiteral("IMU/imu_base"), Adapters },
        { QStringLiteral("IMU/imu_capabilities"), Adapters },
        { QStringLiteral("IMU/imu_factory"), Adapters },
        { QStringLiteral("IMU/imu_io_worker"), Adapters },
        { QStringLiteral("IMU/imu_sample_tap"), Adapters },
        { QStringLiteral("IMU/hm_session_worker"), Adapters },
        { QStringLiteral("IMU/wt9011dcl"), Adapters },
        { QStringLiteral("IMU/wt9011dcl_ble"), Adapters },
        { QStringLiteral("IMU/wt9011dcl_base"), Adapters },
        { QStringLiteral("IMU/wt901_config_check"), Adapters },
        // Gui/shot: 03 §2 puts "capture and session services now in src/Gui/shot" in Application —
        // capture + the analysis job, the shot trigger and its source map, re-analysis; the shot arbiter
        // and the shot outcome are pure rules.
        { QStringLiteral("Gui/shot/shot_processor"), Application },
        { QStringLiteral("Gui/shot/shot_controller"), Application },
        { QStringLiteral("Gui/shot/reanalysis_controller"), Application },
        { QStringLiteral("Gui/shot/shot_arbiter"), Rules },
        { QStringLiteral("Gui/shot/shot_outcome"), Rules },               // pure: a shot's composed outcome
        { QStringLiteral("Gui/shot/shot_source_map"), Application },      // the shot trigger's source table
        // Pose: the hardware probe behind the "Motion capture quality" setting.
        { QStringLiteral("Pose/motion_capture_probe"), Adapters },
    };
    return t;
}

QString stemOf(const QString &rel)
{
    const int slash = rel.lastIndexOf(QLatin1Char('/'));
    const int dot   = rel.lastIndexOf(QLatin1Char('.'));
    return dot > slash ? rel.left(dot) : rel;
}

int ringOf(const QString &rel)
{
    const auto &files = fileRings();
    const auto  f     = files.constFind(stemOf(rel));
    if (f != files.cend()) return f.value();
    int best = -1, bestLen = -1;
    for (const auto &d : dirRings())
        if (rel.startsWith(d.first) && d.first.size() > bestLen) { best = d.second; bestLen = d.first.size(); }
    return best;
}

// ── A2: the session- and athlete-scoped C++ types QML must not create ──────────────────────────
const QRegularExpression &scopedTypeRe()
{
    // data/seam_inventory.md "C++ types QML instantiates": the ones whose data is a session's or an
    // athlete's. View-local helpers (ChartMetrics, TimelineLabels, ShotFilterProxyModel, …) stay.
    static const QRegularExpression re(QStringLiteral(
        R"((?:^|[^\w.])(SessionDiagnosticsModel|SessionSeriesModel|LmSessionModel|Charts\w*Presenter|TrustPresenter|SwingDataSource)\s*\{)"));
    return re;
}

// ── A3: folder-name parsing, allowed only in the identity owners ──────────────────────────────
bool a3Allowed(const QString &rel)
{
    const QString stem = stemOf(rel);
    return stem == QLatin1String("Export/swing_paths")
        || stem.endsWith(QLatin1String("/session_identity"));     // step 2's SessionIdentity
}

// ── A4: session / athlete file names and their one owner (03 §7) ───────────────────────────────
struct OwnedFile {
    const char *name;    // as printed
    const char *re;      // the file name; a literal that IS it, or a path ending in it, is a finding
    const char *owner;   // the file (stem under src/) allowed to spell it; "" = none yet
};
const QVector<OwnedFile> &ownedFiles()
{
    static const QVector<OwnedFile> t{
        { "diagnostics.json",   R"(diagnostics\.json)",                       "Gui/diagnostics/session_diagnostics_model" },
        { "fault_profile.json", R"(fault_profile\.json)",                     "Gui/diagnostics/session_diagnostics_model" },
        { "club_baseline.json", R"(club_baseline\.json)",                     "Gui/diagnostics/session_diagnostics_model" },
        { "work_ons.json",      R"(work_ons\.json)",                          "Gui/diagnostics/work_ons_controller" },
        { "tendencies.json",    R"(tendencies\.json)",                        "Gui/diagnostics/work_ons_controller" },
        { "swing_themes*.json", R"(swing_themes(?:\.[^/]*)?\.json|swing_themes)","Gui/diagnostics/work_ons_controller" },
        { "session_input.json", R"(session_input\.json)",                     "" },   // step 2: SessionRepository
        { "athlete.json",       R"(athlete\.json)",                           "" },   // step 2: AthleteRepository
    };
    return t;
}

// ── A5 / A8: which files the rule reads, and what it looks for ─────────────────────────────────
bool isRulesIoDir(const QString &rel)
{
    if (rel.startsWith(QLatin1String("Analysis/"))) return ringOf(rel) == Rules;     // 03 §11 "Analysis rules"
    return rel.startsWith(QLatin1String("Coaching/")) || rel.startsWith(QLatin1String("Diagnostics/engine/"))
        || rel.startsWith(QLatin1String("Diagnostics/norms/")) || rel.startsWith(QLatin1String("Diagnostics/measures/"));
}

// An include that takes src/Analysis past the Recording contract (A8).
bool isAcquisitionOrAppState(const QString &target)
{
    const int r = ringOf(target);
    if (target.startsWith(QLatin1String("Gui/"))) return true;            // application state, live controllers
    if (r != Adapters) return false;
    static const QStringList acq{ QStringLiteral("Buffer/"), QStringLiteral("Video/"), QStringLiteral("Audio/"),
                                  QStringLiteral("Ppcp/"), QStringLiteral("IMU/"), QStringLiteral("LaunchMonitor/") };
    for (const QString &d : acq)
        if (target.startsWith(d)) return true;
    const QString s = stemOf(target);
    return s == QLatin1String("Core/pp_settings") || s == QLatin1String("Core/studio_profile")
        || s == QLatin1String("Core/device_enumerator");
}

// ── Source text ───────────────────────────────────────────────────────────────────────────────
struct Source {
    QString                      rel;      // path under src/
    bool                         qml = false;
    QString                      code;     // comments removed, literal contents blanked (structure kept)
    QString                      lit;      // comments removed, literals kept
    QStringList                  codeLines, litLines;
    QVector<QPair<int, QString>> strings;  // string literals with their line
};

bool isIdent(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Blank comments (both copies) and the contents of string / char literals (the `code` copy), keeping
// every newline so line numbers survive. C++: raw strings and digit separators; QML/JS: template
// strings.
void strip(const QString &src, bool cpp, Source *s)
{
    QString &code = s->code;
    QString &lit  = s->lit;
    code.reserve(src.size());
    lit.reserve(src.size());
    int line = 1;
    auto both = [&](QChar c) { code += c; lit += c; if (c == QLatin1Char('\n')) ++line; };
    const int n = src.size();
    for (int i = 0; i < n; ++i) {
        const QChar c  = src.at(i);
        const QChar nx = i + 1 < n ? src.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && nx == QLatin1Char('/')) {
            while (i < n && src.at(i) != QLatin1Char('\n')) { code += QLatin1Char(' '); lit += QLatin1Char(' '); ++i; }
            if (i < n) both(QLatin1Char('\n'));
            continue;
        }
        if (c == QLatin1Char('/') && nx == QLatin1Char('*')) {
            i += 2; code += QStringLiteral("  "); lit += QStringLiteral("  ");
            while (i < n && !(src.at(i) == QLatin1Char('*') && i + 1 < n && src.at(i + 1) == QLatin1Char('/'))) {
                both(src.at(i) == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' '));
                ++i;
            }
            if (i < n) { code += QStringLiteral("  "); lit += QStringLiteral("  "); ++i; }
            continue;
        }
        // C++ raw string R"delim( … )delim"
        if (cpp && c == QLatin1Char('"') && i > 0 && src.at(i - 1) == QLatin1Char('R')
            && (i < 2 || !isIdent(src.at(i - 2)) || QStringLiteral("uUL8").contains(src.at(i - 2)))) {
            const int open = src.indexOf(QLatin1Char('('), i);
            if (open > 0) {
                const QString delim = src.mid(i + 1, open - i - 1);
                const QString close = QLatin1Char(')') + delim + QLatin1Char('"');
                int end = src.indexOf(close, open);
                if (end < 0) end = n - close.size();
                const int startLine = line;
                const QString body = src.mid(open + 1, end - open - 1);
                code += c; lit += c;
                for (int k = i + 1; k < end + close.size() - 1 && k < n; ++k) {
                    const QChar ch = src.at(k);
                    lit += ch;
                    code += ch == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' ');
                    if (ch == QLatin1Char('\n')) ++line;
                }
                code += QLatin1Char('"'); lit += QLatin1Char('"');
                s->strings.append({ startLine, body });
                i = end + close.size() - 1;
                continue;
            }
        }
        const bool quote = c == QLatin1Char('"') || c == QLatin1Char('\'') || (!cpp && c == QLatin1Char('`'));
        if (quote) {
            // C++14 digit separator (1'000'000): a ' inside a number token is not a literal
            if (cpp && c == QLatin1Char('\'')) {
                int j = i - 1;
                while (j >= 0 && (isIdent(src.at(j)) || src.at(j) == QLatin1Char('\'') || src.at(j) == QLatin1Char('.'))) --j;
                if (j + 1 < i && src.at(j + 1).isDigit()) { both(c); continue; }
            }
            const QChar q = c;
            const int   startLine = line;
            QString     body;
            code += q; lit += q;
            ++i;
            while (i < n && src.at(i) != q) {
                const QChar ch = src.at(i);
                if (ch == QLatin1Char('\\') && i + 1 < n) {
                    body += src.at(i + 1);
                    lit += ch; lit += src.at(i + 1);
                    code += QStringLiteral("  ");
                    i += 2;
                    continue;
                }
                if (ch == QLatin1Char('\n')) {
                    if (q != QLatin1Char('`')) break;                // an unterminated literal ends at the line
                    both(QLatin1Char('\n'));
                } else {
                    lit += ch; code += QLatin1Char(' ');
                }
                body += ch;
                ++i;
            }
            if (i < n && src.at(i) == q) { code += q; lit += q; }
            else if (i < n) both(src.at(i));
            s->strings.append({ startLine, body });
            continue;
        }
        both(c);
    }
    s->codeLines = s->code.split(QLatin1Char('\n'));
    s->litLines  = s->lit.split(QLatin1Char('\n'));
}

Source makeSource(const QString &rel, const QString &text)
{
    Source s;
    s.rel = rel;
    s.qml = rel.endsWith(QLatin1String(".qml")) || rel.endsWith(QLatin1String(".js"));
    strip(text, !s.qml, &s);
    return s;
}

int lineAt(const QString &text, int offset)
{
    return text.left(offset).count(QLatin1Char('\n')) + 1;
}

// ── Include resolution (A1, A8) ────────────────────────────────────────────────────────────────
struct Tree {
    QSet<QString>               files;      // every C++ path under src/
    QHash<QString, QStringList> byName;     // basename → paths
};

QString resolve(const Tree &t, const QString &from, const QString &inc)
{
    const QString dir = from.contains(QLatin1Char('/')) ? from.left(from.lastIndexOf(QLatin1Char('/'))) : QString();
    const QStringList tries{ QDir::cleanPath(dir + QLatin1Char('/') + inc), QDir::cleanPath(inc),
                             QDir::cleanPath(QStringLiteral("Gui/") + inc) };
    for (const QString &c : tries)
        if (t.files.contains(c)) return c;
    // bare includes resolve through the build's include paths: by basename, when it is unique
    const QStringList same = t.byName.value(inc.mid(inc.lastIndexOf(QLatin1Char('/')) + 1));
    return same.size() == 1 ? same.first() : QString();
}

const QRegularExpression &includeRe()
{
    static const QRegularExpression re(QStringLiteral(R"re(^\s*#\s*include\s*"([^"]+)")re"));
    return re;
}

// ── A7: function lengths (size_table.py's heuristic) ──────────────────────────────────────────
struct Function {
    QString name;
    int     line = 0, length = 0;
};

QVector<Function> functions(const QString &code)
{
    static const QRegularExpression sigEnd(QStringLiteral(R"(\)(?:\s|const|noexcept|override|final)*$)"));
    static const QRegularExpression ctrl(QStringLiteral(R"(\b(?:if|for|while|switch|catch|return|else|do)\s*$)"));
    static const QRegularExpression opener(QStringLiteral(R"([=,(]\s*$)"));
    static const QRegularExpression nameRe(QStringLiteral(R"(([~\w:]+)\s*$)"));
    static const QStringList notNames{ QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"),
                                       QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return"),
                                       QStringLiteral("sizeof"), QStringLiteral("decltype"), QStringLiteral("Q_ASSERT") };
    QVector<Function>               out;
    QVector<QPair<QString, int>>    stack;      // (function name or "", offset of '{')
    QVector<int>                    lineOf(code.size() + 1);
    int ln = 1;
    for (int k = 0; k < code.size(); ++k) { lineOf[k] = ln; if (code.at(k) == QLatin1Char('\n')) ++ln; }
    lineOf[code.size()] = ln;
    auto inFunction = [&] { for (const auto &s : stack) if (!s.first.isEmpty()) return true; return false; };
    for (int i = 0; i < code.size(); ++i) {
        const QChar ch = code.at(i);
        if (ch == QLatin1Char('{')) {
            QString fn;
            // the signature: back to the previous statement boundary (; { or } outside parentheses), so a
            // long parameter list is never cut off (size_table.py looks back a fixed 240 characters)
            int from = i - 1;
            for (int paren = 0; from >= 0; --from) {
                const QChar c = code.at(from);
                if (c == QLatin1Char(')')) ++paren;
                else if (c == QLatin1Char('(')) --paren;
                else if (paren <= 0 && (c == QLatin1Char(';') || c == QLatin1Char('{') || c == QLatin1Char('}'))) break;
            }
            const QString before = code.mid(from + 1, i - from - 1);
            int e = before.size();
            while (e > 0 && before.at(e - 1).isSpace()) --e;
            const QString stripped = before.left(e);
            const QChar last = stripped.isEmpty() ? QChar() : stripped.back();
            if ((last == QLatin1Char(')') || last == QLatin1Char('t') || last == QLatin1Char('e') || last == QLatin1Char('l'))
                && sigEnd.match(stripped.right(60)).hasMatch() && !inFunction()) {
                const int close = stripped.lastIndexOf(QLatin1Char(')'));
                int d = 0, j = close;
                for (; j >= 0; --j) {
                    if (stripped.at(j) == QLatin1Char(')')) ++d;
                    else if (stripped.at(j) == QLatin1Char('(') && --d == 0) break;
                }
                QString tail = j > 0 ? stripped.left(j) : QString();
                while (!tail.isEmpty() && tail.back().isSpace()) tail.chop(1);
                if (tail.endsWith(QLatin1Char(']'))) tail = QStringLiteral("if");         // a lambda
                if (!ctrl.match(tail).hasMatch() && !opener.match(tail).hasMatch()) {
                    const auto m = nameRe.match(tail);
                    if (m.hasMatch() && !notNames.contains(m.captured(1))) fn = m.captured(1);
                }
            }
            stack.append({ fn, i });
        } else if (ch == QLatin1Char('}')) {
            if (stack.isEmpty()) continue;
            const auto top = stack.takeLast();
            if (!top.first.isEmpty() && !inFunction())
                out.append({ top.first, lineOf[top.second], lineOf[i] - lineOf[top.second] + 1 });
        }
    }
    return out;
}

constexpr int kMaxFunctionLines = 300;

// ── The rules ──────────────────────────────────────────────────────────────────────────────────
QVector<Finding> scan(const Source &s, const Tree &tree)
{
    QVector<Finding> out;
    const QString   &rel = s.rel;

    if (s.qml) {
        // A2 — a scoped C++ type created in QML
        auto it = scopedTypeRe().globalMatch(s.code);
        while (it.hasNext()) {
            const auto m = it.next();
            out.append({ QStringLiteral("A2"), lineAt(s.code, m.capturedStart(1)),
                         m.captured(1) + QStringLiteral(" is session/athlete-scoped: QML reads it from its owner, never creates it") });
        }
    } else {
        const int ring = ringOf(rel);
        // A1 / A8 — includes
        for (int li = 0; li < s.litLines.size(); ++li) {
            const auto m = includeRe().match(s.litLines.at(li));
            if (!m.hasMatch()) continue;
            const QString target = resolve(tree, rel, m.captured(1));
            if (target.isEmpty()) continue;                       // a system / third-party / generated header
            const int tr = ringOf(target);
            if (ring >= 0 && tr > ring)
                out.append({ QStringLiteral("A1"), li + 1,
                             QStringLiteral("%1 → %2: includes %3").arg(QLatin1String(ringName(ring)),
                                                                         QLatin1String(ringName(tr)), target) });
            if (rel.startsWith(QLatin1String("Analysis/")) && isAcquisitionOrAppState(target))
                out.append({ QStringLiteral("A8"), li + 1,
                             QStringLiteral("Analysis reaches past the Recording contract: includes %1").arg(target) });
        }
        // A5 — file I/O in a rules directory
        if (isRulesIoDir(rel)) {
            static const QRegularExpression io(QStringLiteral(
                R"(\b(?:QFile|QSaveFile|QDir|QFileInfo|QDirIterator|QTemporaryFile|QTemporaryDir)\b|\bstd::[io]?fstream\b|\bfopen\s*\()"));
            for (int li = 0; li < s.codeLines.size(); ++li) {
                const auto m = io.match(s.codeLines.at(li));
                if (m.hasMatch())
                    out.append({ QStringLiteral("A5"), li + 1,
                                 m.captured(0) + QStringLiteral(": file I/O in a rules directory (repositories load, rules compute)") });
            }
        }
        // A6 — QVariantMap / QVariantList on the QML boundary
        if (rel.endsWith(QLatin1String(".h")) || rel.endsWith(QLatin1String(".hpp"))) {
            static const QRegularExpression inv(QStringLiteral(R"(\bQ_INVOKABLE\b)"));
            static const QRegularExpression var(QStringLiteral(R"(\bQVariant(?:Map|List)\b)"));
            static const QRegularExpression prop(QStringLiteral(R"(\bQ_PROPERTY\s*\(\s*(?:const\s+)?[\w:<>]*QVariant(?:Map|List)\b)"));
            auto it = inv.globalMatch(s.code);
            while (it.hasNext()) {
                const auto m   = it.next();
                int        end = m.capturedEnd();
                while (end < s.code.size() && s.code.at(end) != QLatin1Char(';') && s.code.at(end) != QLatin1Char('{')) ++end;
                if (var.match(s.code.mid(m.capturedEnd(), end - m.capturedEnd())).hasMatch())
                    out.append({ QStringLiteral("A6"), lineAt(s.code, m.capturedStart()),
                                 QStringLiteral("Q_INVOKABLE with a QVariantMap/QVariantList: a typed value (Q_GADGET) or a list model") });
            }
            auto pt = prop.globalMatch(s.code);
            while (pt.hasNext()) {
                const auto m = pt.next();
                out.append({ QStringLiteral("A6"), lineAt(s.code, m.capturedStart()),
                             QStringLiteral("Q_PROPERTY of a QVariantMap/QVariantList: a typed value (Q_GADGET) or a list model") });
            }
            // public slots and signals: QML calls and receives them too. A section runs from its label to
            // the next access label or the end of the class; only its declarations (depth 0) count.
            static const QRegularExpression label(QStringLiteral(
                R"(\b(?:(public|protected|private)\s*(slots|Q_SLOTS)?|(signals|Q_SIGNALS))\s*:(?!:))"));
            QVector<QRegularExpressionMatch> labels;
            for (auto lt = label.globalMatch(s.code); lt.hasNext();) labels.append(lt.next());
            for (int k = 0; k < labels.size(); ++k) {
                const auto &m = labels.at(k);
                const bool qmlSeen = !m.captured(3).isEmpty()
                                  || (m.captured(1) == QLatin1String("public") && !m.captured(2).isEmpty());
                if (!qmlSeen) continue;
                const int stop = k + 1 < labels.size() ? labels.at(k + 1).capturedStart() : s.code.size();
                int       depth = 0, segStart = m.capturedEnd();
                auto      judge = [&](int a, int b) {
                    const QString seg = s.code.mid(a, b - a);
                    if (!seg.contains(QLatin1Char('(')) || seg.contains(QLatin1String("Q_INVOKABLE"))
                        || seg.contains(QLatin1String("Q_PROPERTY")) || !var.match(seg).hasMatch())
                        return;
                    int first = a;
                    while (first < b && s.code.at(first).isSpace()) ++first;
                    out.append({ QStringLiteral("A6"), lineAt(s.code, first),
                                 QStringLiteral("%1 with a QVariantMap/QVariantList: a typed value (Q_GADGET) or a list model")
                                     .arg(m.captured(3).isEmpty() ? QStringLiteral("public slot") : QStringLiteral("signal")) });
                };
                for (int c = m.capturedEnd(); c < stop; ++c) {
                    const QChar ch = s.code.at(c);
                    if (ch == QLatin1Char('{')) {
                        if (depth == 0) judge(segStart, c);
                        ++depth;
                    } else if (ch == QLatin1Char('}')) {
                        if (depth == 0) break;                          // the end of the class
                        if (--depth == 0) segStart = c + 1;
                    } else if (ch == QLatin1Char(';') && depth == 0) {
                        judge(segStart, c);
                        segStart = c + 1;
                    }
                }
            }
        }
        // A7 — a function over 300 lines in rings 1–2
        if (ring == Vocabulary || ring == Rules)
            for (const Function &f : functions(s.code))
                if (f.length > kMaxFunctionLines)
                    out.append({ QStringLiteral("A7"), f.line,
                                 QStringLiteral("%1 is %2 lines (over %3)").arg(f.name).arg(f.length).arg(kMaxFunctionLines) });
    }

    // A3 — folder-name parsing (C++ and QML/JS)
    if (!a3Allowed(rel)) {
        static const QString      wrap = QStringLiteral(R"((?:(?:QLatin1Char|QChar|QStringLiteral|QLatin1String|QLatin1StringView)\(\s*)?u?)");
        static const QRegularExpression us(QStringLiteral(R"(\b(lastIndexOf|section|split)\(\s*)") + wrap + QStringLiteral(R"(['"]_['"])"));
        static const QRegularExpression swingPrefix(QStringLiteral(R"(\bstartsWith\(\s*)") + wrap + QStringLiteral(R"("swing_")"));
        static const QRegularExpression left10(QStringLiteral(R"((\w+)(\(\))?\s*\.(?:left|first)\(\s*10\s*\))"));
        static const QRegularExpression nameish(QStringLiteral(R"((?i)name|session|dir)"));
        static const QRegularExpression swingGlob(QStringLiteral(R"re("swing_\*)re"));
        static const QRegularExpression dateRe(QStringLiteral(R"re(\\{1,2}d\{4\}-\\{1,2}d\{2\}-\\{1,2}d\{2\})re"));
        for (int li = 0; li < s.litLines.size(); ++li) {
            const QString &l = s.litLines.at(li);
            auto u = us.globalMatch(l);
            while (u.hasNext()) {
                const auto m = u.next();
                out.append({ QStringLiteral("A3"), li + 1,
                             m.captured(1) + QStringLiteral("('_'): a number parsed out of a folder name — SwingPaths / SessionIdentity") });
            }
            auto p = swingPrefix.globalMatch(l);
            while (p.hasNext()) {
                p.next();
                out.append({ QStringLiteral("A3"), li + 1,
                             QStringLiteral("startsWith(\"swing_\"): a folder name tested by its prefix — SwingPaths") });
            }
            auto g = swingGlob.globalMatch(l);
            while (g.hasNext()) {
                g.next();
                out.append({ QStringLiteral("A3"), li + 1,
                             QStringLiteral("\"swing_*\": swings found by their folder-name pattern — SwingPaths") });
            }
            auto r = dateRe.globalMatch(l);
            while (r.hasNext()) {
                r.next();
                out.append({ QStringLiteral("A3"), li + 1,
                             QStringLiteral("\\d{4}-\\d{2}-\\d{2}: a date parsed out of a folder name by a regex — SessionIdentity") });
            }
            auto d = left10.globalMatch(l);
            while (d.hasNext()) {
                const auto m = d.next();
                const bool dated = l.contains(QLatin1String("fileName()")) || l.contains(QLatin1String("dirName()"))
                                || l.contains(QLatin1String("QDate::fromString")) || nameish.match(m.captured(1)).hasMatch();
                if (dated)
                    out.append({ QStringLiteral("A3"), li + 1,
                                 QStringLiteral("left(10): a date read from a folder name — SessionIdentity") });
            }
        }
    }

    // A4 — a session / athlete file name outside its owner
    {
        const QString stem = stemOf(rel);
        for (const OwnedFile &o : ownedFiles()) {
            if (stem == QLatin1String(o.owner)) continue;
            const QRegularExpression re(QStringLiteral("^(?:.*/)?(?:%1)$").arg(QLatin1String(o.re)));
            for (const auto &str : s.strings)
                if (re.match(str.second).hasMatch())
                    out.append({ QStringLiteral("A4"), str.first,
                                 QStringLiteral("\"%1\" spelled outside its owner (%2)")
                                     .arg(str.second, *o.owner ? QString::fromLatin1(o.owner) : QStringLiteral("its repository, step 2")) });
        }
    }
    return out;
}

int countRule(const QVector<Finding> &fs, const char *rule)
{
    int n = 0;
    for (const auto &f : fs) n += f.rule == QLatin1String(rule) ? 1 : 0;
    return n;
}

// ── Fixtures: every rule must still bite ───────────────────────────────────────────────────────
void selfTest()
{
    std::printf("=== the rules still catch what they are for ===\n");
    Tree t;
    for (const char *f : { "Gui/shot/shot_processor.h", "Gui/athlete/athlete_controller.h", "Coaching/shot_factors.h",
                           "Metrics/metric_catalogue.h", "Buffer/source_ring.h", "Buffer/types.h", "Core/pp_debug.h",
                           "Analysis/swing_analysis.h", "Analysis/analysis_pipeline.h" }) {
        t.files.insert(QString::fromLatin1(f));
        t.byName[QFileInfo(QString::fromLatin1(f)).fileName()].append(QString::fromLatin1(f));
    }
    auto run = [&](const char *rel, const QString &text) { return scan(makeSource(QString::fromLatin1(rel), text), t); };

    check(countRule(run("Coaching/x.cpp", QStringLiteral("#include \"../Gui/shot/shot_processor.h\"\n")), "A1") == 1,
          QStringLiteral("A1 catches a rule including a Gui header"));
    check(countRule(run("Metrics/x.h", QStringLiteral("#include \"shot_factors.h\"\n")), "A1") == 1,
          QStringLiteral("A1 resolves a bare include by its unique basename (Vocabulary → Rules)"));
    check(countRule(run("Coaching/x.cpp", QStringLiteral("#include \"../Metrics/metric_catalogue.h\"\n#include \"pp_debug.h\"\n"
                                                         "#include \"../Buffer/types.h\"\n#include <QFile>\n")), "A1") == 0,
          QStringLiteral("A1 passes inward includes, Platform logging, the Recording's types and system headers"));
    check(countRule(run("Coaching/x.cpp", QStringLiteral("// #include \"../Gui/shot/shot_processor.h\"\n")), "A1") == 0,
          QStringLiteral("A1 ignores a commented-out include"));
    check(countRule(run("Analysis/swing_analysis.h", QStringLiteral("#include \"../Analysis/analysis_pipeline.h\"\n")), "A1") == 1,
          QStringLiteral("A1 uses the per-file ring of a straddling directory (Vocabulary → Application)"));
    check(countRule(run("Analysis/x.cpp", QStringLiteral("#include \"source_ring.h\"\n#include \"../Gui/athlete/athlete_controller.h\"\n"
                                                         "#include \"types.h\"\n")), "A8") == 2,
          QStringLiteral("A8 catches Analysis including the ring buffer and app state, passes the Recording's types"));
    check(countRule(run("Gui/x/PpY.qml", QStringLiteral("Item {\n  SessionSeriesModel { id: m }\n  ChartsTracesPresenter {\n  }\n"
                                                        "  property SessionSeriesModel ref: null\n  ChartMetrics { }\n}\n")), "A2") == 2,
          QStringLiteral("A2 catches QML creating scoped types; passes a typed property and a view-local helper"));
    check(countRule(run("Gui/x/PpY.qml", QStringLiteral("Item { // SessionSeriesModel { }\n}\n")), "A2") == 0,
          QStringLiteral("A2 ignores comments"));
    const QString parse = QStringLiteral(
        "int a = name.lastIndexOf(QLatin1Char('_'));\nauto b = n.section(QLatin1Char('_'), -1);\n"
        "if (name.startsWith(QStringLiteral(\"swing_\"))) {}\nQDate d = QDate::fromString(fi.fileName().left(10));\n"
        "auto h = QCryptographicHash::hash(x).toHex().left(10);\n");
    check(countRule(run("Gui/x/y.cpp", parse), "A3") == 4,
          QStringLiteral("A3 catches '_' parsing, a swing_ prefix test and a dated left(10); passes a hash's left(10)"));
    check(countRule(run("Export/swing_paths.cpp", parse), "A3") == 0, QStringLiteral("A3 allows SwingPaths"));
    check(countRule(run("Gui/x/y.cpp", QStringLiteral(
              "auto e = d.entryList({ QStringLiteral(\"swing_*\") }, QDir::Dirs);\n"
              "static const QRegularExpression re(QStringLiteral(\"^(\\\\d{4}-\\\\d{2}-\\\\d{2})\"));\n"
              "auto parts = name.split(QLatin1Char('_'));\nauto t = QStringLiteral(\"swing_count\");\n")), "A3") == 3,
          QStringLiteral("A3 catches a swing_* glob, a regex date parse and split('_'); passes another swing_ word"));
    check(countRule(run("Gui/x/y.js", QStringLiteral("var n = dir.lastIndexOf(\"_\")\n")), "A3") == 1,
          QStringLiteral("A3 reads QML/JS too"));
    const QString names = QStringLiteral(
        "const QString a = QStringLiteral(\"diagnostics.json\");\nauto b = dir + \"/work_ons.json\";\n"
        "auto c = QStringLiteral(\"swing_themes.%1.json\");\nppWarn() << \"diagnostics.json could not be read\";\n"
        "// \"tendencies.json\" in a comment\n#include \"../../Coaching/swing_themes.h\"\n");
    check(countRule(run("Gui/x/y.cpp", names), "A4") == 3,
          QStringLiteral("A4 catches a name or a path ending in it; passes a log sentence, a comment and a header"));
    check(countRule(run("Gui/diagnostics/work_ons_controller.cpp", QStringLiteral("auto b = \"work_ons.json\";\n")), "A4") == 0,
          QStringLiteral("A4 allows the owner"));
    check(countRule(run("Coaching/x.cpp", QStringLiteral("#include <QFile>\nQFile f(p);\nQString s = \"QDir\";\nstd::ifstream in(p);\n")), "A5") == 3,
          QStringLiteral("A5 catches file I/O lines in a rules directory, not a word in a string"));
    check(countRule(run("Gui/x/y.cpp", QStringLiteral("QFile f(p);\n")), "A5") == 0, QStringLiteral("A5 reads only the rules directories"));
    check(countRule(run("Analysis/seg_resolve.h", QStringLiteral("QFile f(p);\n")), "A5") == 1
              && countRule(run("Analysis/swing_reanalyzer.cpp", QStringLiteral("QFile f(p);\n")), "A5") == 0,
          QStringLiteral("A5 reads Analysis's rule files, not its orchestration"));
    const QString api = QStringLiteral(
        "class X : public QObject {\n  Q_PROPERTY(QVariantMap shot READ shot NOTIFY changed)\n"
        "  Q_PROPERTY(int n READ n NOTIFY changed)\n  Q_INVOKABLE QVariantList rows() const;\n"
        "  Q_INVOKABLE void set(const QVariantMap &m);\n  Q_INVOKABLE int count() const;\n  QVariantMap notInvokable() const;\n"
        "public slots:\n  void chat(const QVariantList &h);\n  void stop() { QVariantMap inBody; }\n"
        "signals:\n  void prepared(bool ok,\n                const QVariantMap &report);\n  void done();\n"
        "private slots:\n  void privateSlot(const QVariantMap &m);\nprivate:\n  QVariantMap m_cache;\n};\n");
    check(countRule(run("Gui/x/y.h", api), "A6") == 5,
          QStringLiteral("A6 catches QVariant properties, returns, arguments, public slots and signals; passes typed, "
                         "private and non-QML members and slot bodies"));
    check(countRule(run("Gui/x/y.cpp", api), "A6") == 0, QStringLiteral("A6 reads headers only"));
    QString longFn = QStringLiteral("namespace n {\nint big(int a) const\n{\n");
    for (int i = 0; i < 300; ++i) longFn += QStringLiteral("  if (a) { a += 1; }  // }\n");
    longFn += QStringLiteral("  auto f = [&](int x) {\n    return x;\n  };\n  return a;\n}\nint small() { return 1; }\n}\n");
    const auto a7 = run("Coaching/x.cpp", longFn);
    check(countRule(a7, "A7") == 1 && a7.first().text.startsWith(QLatin1String("big is 306 lines")),
          QStringLiteral("A7 finds a 306-line function in a rules ring and measures it (got: %1)")
              .arg(a7.isEmpty() ? QStringLiteral("none") : a7.first().text));
    check(countRule(run("Gui/x/y.cpp", longFn), "A7") == 0, QStringLiteral("A7 reads rings 1–2 only"));
    QString longSig = QStringLiteral("struct S { int a; };\ninline int composed(");
    for (int i = 0; i < 12; ++i) longSig += QStringLiteral("const std::vector<SomeLongTypeName> &argumentNumber%1, ").arg(i);
    longSig += QStringLiteral("const Options &o = {})\n{\n");
    for (int i = 0; i < 300; ++i) longSig += QStringLiteral("  ++x;\n");
    longSig += QStringLiteral("  return 0;\n}\n");
    const auto a7s = run("Coaching/x.h", longSig);
    check(countRule(a7s, "A7") == 1 && a7s.first().text.startsWith(QLatin1String("composed is")),
          QStringLiteral("A7 finds a long function whose signature is longer than 240 characters and has a {} default (got: %1)")
              .arg(a7s.isEmpty() ? QStringLiteral("none") : a7s.first().text));
    check(ringOf(QStringLiteral("Analysis/analysis_pipeline.cpp")) == Application && ringOf(QStringLiteral("Analysis/gate_record.h")) == Vocabulary
              && ringOf(QStringLiteral("Core/pp_debug.cpp")) == Platform && ringOf(QStringLiteral("Gui/diagnostics/x.h")) == Adapters
              && ringOf(QStringLiteral("NoSuchDir/x.h")) == -1 && ringOf(QStringLiteral("Gui/shot/shot_processor.cpp")) == Application,
          QStringLiteral("the ring table: per-file entries over directory defaults; unknown directories have no ring"));
}

// ── The allow-list ─────────────────────────────────────────────────────────────────────────────
struct Allow {
    int     count = 0;
    QString reason;
    QString step;
    int     line = 0;
};

QMap<QString, Allow> readAllowList(const QString &path, bool *ok, QMap<QString, int> *perStep)
{
    QMap<QString, Allow> out;
    QFile                f(path);
    *ok = f.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!*ok) return out;
    static const QRegularExpression tag(QStringLiteral(R"(#\s*(step\s+[1-8]|unplanned|kept)\s*$)"));
    static const QRegularExpression ruleRe(QStringLiteral(R"(^A[1-8]$)"));
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString l = lines.at(i).trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = l.split(QLatin1Char('|'));
        const auto        t     = tag.match(l);
        const QString     rest  = parts.mid(3).join(QLatin1Char('|'));
        const QString     why   = rest.left(rest.lastIndexOf(QLatin1Char('#'))).trimmed();
        const bool        okLine = parts.size() >= 4 && ruleRe.match(parts.at(1).trimmed()).hasMatch()
                            && parts.at(2).trimmed().toInt() > 0 && t.hasMatch() && !why.isEmpty();
        if (!okLine) {
            check(false, QStringLiteral("allow-list line %1 is path | rule | count | WHY # step N (or # unplanned / # kept): %2").arg(i + 1).arg(l));
            continue;
        }
        Allow a;
        a.count  = parts.at(2).trimmed().toInt();
        a.reason = parts.mid(3).join(QLatin1Char('|')).trimmed();
        a.step   = t.captured(1);
        a.line   = i + 1;
        const QString key = parts.at(0).trimmed() + QLatin1Char('|') + parts.at(1).trimmed();
        if (out.contains(key)) {
            check(false, QStringLiteral("allow-list line %1 repeats %2 (line %3): one line per file and rule")
                             .arg(i + 1).arg(key).arg(out.value(key).line));
            continue;
        }
        out.insert(key, a);
        (*perStep)[a.step] += 1;
    }
    return out;
}

} // namespace

int main()
{
    selfTest();

    const QString root = QStringLiteral(PP_SRC_ROOT);
    if (!QDir(root).exists()) {
        check(false, QStringLiteral("the source tree is where the build says it is (%1)").arg(root));
        return 1;
    }
    bool               allowOk = false;
    QMap<QString, int> perStep;
    const auto         allow = readAllowList(QStringLiteral(PP_ARCH_ALLOWLIST), &allowOk, &perStep);
    std::printf("\n=== the code keeps to the target architecture (03-target-architecture.md §2, §11) ===\n");
    check(allowOk, QStringLiteral("the allow-list was read (%1)").arg(QStringLiteral(PP_ARCH_ALLOWLIST)));

    // Read the tree once.
    static const QStringList skipParts{ QStringLiteral("tests"), QStringLiteral("Testing"),
                                        QStringLiteral("third_party"), QStringLiteral("build") };
    static const QStringList cppExt{ QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("h"),
                                     QStringLiteral("hpp"), QStringLiteral("mm") };
    Tree           tree;
    QVector<QPair<QString, QString>> texts;      // rel, text
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel  = QDir(root).relativeFilePath(path);
        bool          skip = false;
        for (const QString &p : rel.split(QLatin1Char('/'))) skip = skip || skipParts.contains(p);
        if (skip) continue;
        const QString ext = QFileInfo(rel).suffix();
        const bool    cpp = cppExt.contains(ext);
        if (!cpp && ext != QLatin1String("qml") && ext != QLatin1String("js")) continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        texts.append({ rel, QString::fromUtf8(f.readAll()) });
        if (cpp) {
            tree.files.insert(rel);
            tree.byName[QFileInfo(rel).fileName()].append(rel);
        }
    }
    check(tree.files.size() > 500, QStringLiteral("the C++ sources were found and read (%1 files)").arg(tree.files.size()));
    check(texts.size() - tree.files.size() > 100,
          QStringLiteral("the QML/JS sources were found and read (%1 files)").arg(texts.size() - tree.files.size()));

    QStringList unplaced;
    QMap<QString, QVector<Finding>> byKey;     // "path|rule" → findings
    for (const auto &t : texts) {
        if (!t.first.endsWith(QLatin1String(".qml")) && !t.first.endsWith(QLatin1String(".js")) && ringOf(t.first) < 0)
            unplaced << t.first;
        for (const Finding &fd : scan(makeSource(t.first, t.second), tree))
            byKey[t.first + QLatin1Char('|') + fd.rule].append(fd);
    }
    for (const QString &u : unplaced) std::printf("      %s\n", qPrintable(u));
    check(unplaced.isEmpty(), QStringLiteral("every C++ file's directory has a ring in the table (a new directory is placed on purpose)"));

    static const char *ruleNames[] = { "A1 includes from an inner ring into an outer one",
                                       "A2 QML creating a session/athlete-scoped type",
                                       "A3 folder-name parsing outside SwingPaths / SessionIdentity",
                                       "A4 session/athlete file names outside their owner",
                                       "A5 file I/O in a rules directory",
                                       "A6 QVariantMap/QVariantList on the QML boundary",
                                       "A7 functions over 300 lines in rings 1-2",
                                       "A8 Analysis past the Recording contract" };
    int totalEntries = 0, totalAllowed = 0;
    std::printf("\n");
    for (int r = 0; r < 8; ++r) {
        const QString rule = QStringLiteral("A%1").arg(r + 1);
        QStringList   over, slack;
        int           allowed = 0, entries = 0;
        for (auto k = byKey.cbegin(); k != byKey.cend(); ++k) {
            if (!k.key().endsWith(QLatin1Char('|') + rule)) continue;
            const int n = k.value().size();
            const int a = allow.value(k.key()).count;
            allowed += qMin(n, a);
            if (n > a) {
                const QString file = k.key().section(QLatin1Char('|'), 0, 0);
                over << QStringLiteral("%1: %2 finding%3, %4 allowed").arg(file).arg(n).arg(n == 1 ? "" : "s").arg(a);
                for (const Finding &fd : k.value())
                    over << QStringLiteral("    %1:%2  %3").arg(file).arg(fd.line).arg(fd.text);
            }
        }
        for (auto a = allow.cbegin(); a != allow.cend(); ++a) {
            if (!a.key().endsWith(QLatin1Char('|') + rule)) continue;
            ++entries;
            const int n = byKey.value(a.key()).size();
            if (n < a.value().count)
                slack << QStringLiteral("%1 allows %2, finds %3 — lower the count (allow-list line %4)")
                             .arg(a.key().section(QLatin1Char('|'), 0, 0)).arg(a.value().count).arg(n).arg(a.value().line);
        }
        totalEntries += entries;
        totalAllowed += allowed;
        for (const QString &o : over) std::printf("      %s\n", qPrintable(o));
        check(over.isEmpty(), QStringLiteral("%1: none outside the allow-list (%2 allowed in %3 file%4)")
                                  .arg(QLatin1String(ruleNames[r])).arg(allowed).arg(entries).arg(entries == 1 ? "" : "s"));
        for (const QString &s : slack) std::printf("      note: %s\n", qPrintable(s));
    }
    QStringList steps;
    for (auto s = perStep.cbegin(); s != perStep.cend(); ++s) steps << QStringLiteral("%1: %2").arg(s.key()).arg(s.value());
    std::printf("\n  Today's debt: %d findings allowed in %d allow-list lines (%s).\n", totalAllowed, totalEntries,
                qPrintable(steps.join(QStringLiteral(", "))));
    if (g_fail)
        std::printf("\n  Follow the target architecture (docs/implementation/swe-audit/03-target-architecture.md).\n"
                    "  A new entry in src/Gui/tests/architecture_allowlist.txt is an architecture decision: ask Mark first.\n");

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
