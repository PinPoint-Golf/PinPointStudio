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
//         0 Platform     logging, profiling, machine queries, the persistence helpers, the one worker pool —
//                        any ring may include these (see below)
//         1 Vocabulary   value types, ids, metric definitions + catalogue, club vocabulary, units, and
//                        the Recording contract's types (§0.1)
//         2 Rules        pure measurement algorithms, detection, diagnostics engine + packs/norms,
//                        coaching rules
//         3 DomainModel  src/DomainModel (created in step 1 for RecordingRepository)
//         4 Application  orchestration: the analysis pipeline, re-analysis, AppState, use cases
//         5 Adapters     devices, the Recording store and other persistence, UI (all of src/Gui)
//       A file may include its own ring or any ring inside it, never one outside. Also (step 2, S1-g;
//       Mark, 15 Nov 2026, DEC-146): an Adapters-ring file outside src/Gui (Stores, Buffer, Video, the
//       connectors, the device back-ends, …) or a src/Vocabulary/Recording file never includes a src/Gui header —
//       an include inside the Adapters ring that the ring check alone does not see.
//   A2  QML creating a C++ type that is session- or athlete-scoped (scopedTypeRe() below — from
//       data/seam_inventory.md). Its owner is the domain/application (03 §9), never a view.
//   A3  folder-name parsing — the shot number after the last '_' (lastIndexOf / section / split), the
//       date in a folder name's first 10 characters or by a \d{4}-\d{2}-\d{2} regex, a "swing_" prefix
//       test or a "swing_*" name glob — outside SwingPaths and (once it exists) SessionIdentity.
//   A4  a session or athlete file name spelled as a literal ("diagnostics.json", "work_ons.json", …)
//       outside the one file that owns it (ownedFiles() below; 03 §7 "one door per file").
//   A5  P11 (Mark, 15 Nov 2026; DEC-161): an object's persistence is its own concern. Widened in step 2
//       (S1-g) from "file I/O in a rules directory" to every C++ file outside the repositories:
//       (a) file or directory access — QFile, QSaveFile, QDir, QFileInfo, QDirIterator, QTemporaryFile,
//           QTemporaryDir, std::fstream, fopen, atomicWrite, FolderLock — one finding per line that names
//           one. Pure path text is not access and is not counted: QDir's path-string statics (cleanPath,
//           separator, the native-separator pair, isAbsolutePath / isRelativePath) and its enum values
//           (QDir::Files, …), a temporary QDir(…) asked only for a path or its name (dirName, path,
//           absolutePath, filePath, absoluteFilePath, relativeFilePath), and a temporary QFileInfo(…) asked
//           only for a path or a name part (fileName, baseName, completeBaseName, suffix, completeSuffix, path,
//           absolutePath, filePath, absoluteFilePath). An #include <QFile> line is not access either (its
//           uses are); before S1-g it counted, so the rules directories' counts fell with the widening.
//       (b) an #include "…" of a repository: the repository interfaces (isRepositoryHeader() below) and
//           the stores and file doors that implement them.
//       Allowed: src/Stores (the stores and file doors), the Platform ring's own helpers (atomic_write,
//       folder_lock, logging), and, for (b) only, src/DomainModel (the interfaces and the owning
//       objects). The tools (tools/) and the tests are not scanned. The rules directories keep their
//       own message; everything else is reported as a P11 finding. A store door (a store's file work in a
//       file of its own: session_list_reads and session_index_summary, the session store's; rule_data_reads,
//       RuleDataStore's) is part of its store, named so in the information model (§1.4, A5), and exempt with
//       the rest of src/Stores (R9, the Solution Architect, 24 Nov 2026). From the close of steps 5–7 (J-88)
//       to B1.17 the lint counted the session list's two doors by name; that list is deleted.
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
//       the settings file, the studio profile); and any Vocabulary, Rules or Domain-model file including
//       the settings handle, Core/pp_settings, which is Platform, so A1 no longer stops it (R3, the
//       Solution Architect, 24 Nov 2026). The named files are matched by name, whatever their ring.
//   A9  R-CTX-1 (information_model.md §2; platform refactor B1.21): an #include "…" that crosses a context, by
//       IM §4.1's Context column (dirContexts(), fileContexts() below, read like the ring tables), when
//       (1) an earlier context includes a later one (the order is Acquisition → Diagnostics → Coaching);
//       (2) a context includes a Domain + Application header that is not one of the hub's contracts
//           (hubContracts(): R13's five and the Rig's commands, each with why it meets Q26's test: only interfaces and value types,
//           no owner class or service; a header joins by a ruling, when a context first needs it); or
//       (3) a Shared Vocabulary file (or a Platform file) includes any context's file (Q27).
//       Allowed: the hub including every context (it composes them), any include of Platform or Shared, a
//       later context including an earlier one. A C++ file with no context fails, so a new directory is
//       placed on purpose; a stale table entry fails too. A9 has NO allow-list line, and the list refuses one.
//       The deferrals are in the tables, not in the rule: Gui's capture code stays in the hub until Stage D,
//       and the drill pack stays Diagnostics until no Diagnostics file includes it.
//   A10 an #include "…" of the Rig's header, DomainModel/rig.h, from any file but the three kinds that may hold the
//       Rig (docs/design/rig_design.md §6 rule 8; platform refactor C-4): the composition root (Gui/main, App/),
//       app.rig (Gui/app/rig_presenter) and the Rig's store (Stores/rig_store); rig.cpp is its own. The managers
//       reach the Rig through its contracts, never this header. No allow-list line: the list refuses one.
//
// THE RING TABLE. A file's ring is its directory's (dirRings(), longest prefix wins) unless it is
// named in fileRings(), which exists because four directories straddled rings (03 §2) — Analysis,
// Core, LaunchMonitor, Export — and three more hold a device half or a Recording-contract half
// (Buffer, IMU, Pose); and 03 §2 puts the capture and session services in src/Gui/shot in
// Application (ShotProcessor, ShotController, ReanalysisController). Files are named by their path under src/ without the extension, so the .h and
// the .cpp share one entry. A NEW file defaults to its directory's ring; a file in a directory that is
// in neither table fails the test, so a new directory (src/Vocabulary/Recording, say) has to be
// placed on purpose. Platform (ring 0) is a decision of this test, not of 03: logging and profiling
// are used by every ring and no step removes them, so they sit inside Vocabulary. src/Core is
// Platform by its directory rule (R4), so a new file in Core is Platform unless it is named.
// LaunchMonitor's inferred reads are in their own folder, LaunchMonitor/inferred/, Rules by its directory
// rule (R5), so src/LaunchMonitor itself holds only the connectors.
//
// THE ALLOW-LIST. Today's violations live in architecture_allowlist.txt beside this file, generated
// once from the tree at the start of Step G — one line per file and rule, with the number of findings
// allowed, WHY, and the refactor step that removes it (`# step 3`, or a sub-step such as `# step 3.0`;
// `# platform refactor (capture)` or `# platform refactor (settings)` for the refactor after step 8, 05 §1 / §2;
// `# platform refactor (#224)` for one left when steps 5–7 closed, which the platform refactor and its hygiene issue
// take; `# #222` for a long function the old step 8 split, now issue #222;
// `# not domain (§8.3)` for a file the information model classes as not domain, kept allowlisted by Mark's answer;
// `# unplanned` when nothing places it yet — those go to Mark; `# kept` or `# kept (reason)` for a deliberate one). A file that grows past its count fails; one that
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
//   - A5 does not look for std::filesystem or QStandardPaths, nor at QML/JS. It cannot tell a library
//     path from any other: a model, log or download file counts like a session's file, so such a line
//     is allowlisted with its reason. A QFileInfo variable used only for its name still counts.
//   - A7 misses trailing-return functions (`auto f() -> T {`), as size_table.py does.
//   - A9 reads direct includes only: a contract's own includes are not followed (recording_reader.h's reach every
//     file that includes the contract), and a type used through a forward declaration is not seen.
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

#include "lint_source_text.h"

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
        // The Vocabulary ring's one root folder, an area per subfolder; Analysis's shared types are its first area
        // (Solution Architect, 23 Nov 2026). The other areas below move under it in the platform refactor.
        { QStringLiteral("Vocabulary/"), Vocabulary },
        { QStringLiteral("Core/"), Platform },            // the base layer; the device enumerator below (R4)
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
        { QStringLiteral("Coaching/"), Rules },         // the Coaching context's folder, empty: its first file has a ring (IM §4, "The Coaching folder")
        { QStringLiteral("DomainModel/"), DomainModel },  // repository interfaces (step 1, IM-6)
        { QStringLiteral("Analysis/"), Application },
        { QStringLiteral("App/"), Application },          // AppState's home in 03 §2 (created in step 3.4a: app)     // the stages' shared types and rules below
        { QStringLiteral("Buffer/"), Adapters },          // the Recording contract's stream types below
        { QStringLiteral("Video/"), Adapters },
        { QStringLiteral("Audio/"), Adapters },
        { QStringLiteral("Stores/"), Adapters },          // the stores and file doors (R6)
        { QStringLiteral("LaunchMonitor/"), Adapters },   // the connectors (the readings are in Vocabulary/LaunchMonitor/)
        { QStringLiteral("LaunchMonitor/inferred/"), Rules },  // the inferred reads, gates, pairing and reductions (R5)
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
        // Core: the directory is Platform (dirRings(), R4); the device enumerator is its one recorded exception,
        // an adapter until Stage D moves it out (the Solution Architect, 24 Nov 2026: R4).
        { QStringLiteral("Core/device_enumerator"), Adapters },
        // Analysis: the per-shot stages' pure rules (their shared vocabulary is Vocabulary/Analysis/); reading a recorded MP4 is the store's (the recorded products' JSON form is Vocabulary/Analysis/, R10).
        { QStringLiteral("Analysis/shot_analysis_job"), Rules },      // the job carries SkeletonCalib (Rules)
        { QStringLiteral("Analysis/effective_context"), Rules },      // the effective-club rule (IM-3)
        { QStringLiteral("Analysis/analysis_job_builder"), Rules },   // the one job builder
        { QStringLiteral("Analysis/recording_integrity"), Rules },    // the one integrity rule (step 1.5)
        { QStringLiteral("Analysis/reuse_gate"), Rules },             // the one reuse gate + input-stamp staleness (step 1.6)
        { QStringLiteral("Analysis/route_substitution"), Rules },
        { QStringLiteral("Analysis/seg_resolve"), Rules },
        { QStringLiteral("Analysis/stream_trim"), Rules },
        { QStringLiteral("Analysis/capture_integrity_check"), Rules },
        { QStringLiteral("Analysis/manifest_gates"), Rules },
        { QStringLiteral("Analysis/camera_pose_sticks"), Rules },
        { QStringLiteral("Analysis/frame_store"), Rules },
        { QStringLiteral("Analysis/mp4_frame_reader"), Adapters },
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

// ── The contexts (A9) ──────────────────────────────────────────────────────────────────────────
// information_model.md §2 (the four contexts, R-CTX-1) and §4.1's Context column, with R13's placements of the rows
// once marked "to place in Stage B" (docs/implementation/platform_b1_placements.md; the Solution Architect, 24 Nov
// 2026). Platform is the base, not a context; Shared is the Vocabulary areas every context may use (Q27). Contexts are
// tagged, not foldered, so these tables sit beside the ring tables and are read the same way: a file's own entry,
// else its directory's (longest prefix wins); a C++ file in neither has no context and fails the test.
enum Context { NoContext = -1, CtxPlatform = 0, CtxShared, CtxAcquisition, CtxDiagnostics, CtxCoaching, CtxHub };

const char *contextName(int c)
{
    static const char *n[] = { "Platform", "Shared", "Acquisition", "Diagnostics", "Coaching", "Domain + Application" };
    return c >= 0 && c <= 5 ? n[c] : "no context";
}

// R-CTX-1's order: Acquisition → Diagnostics → Coaching. The hub, Platform and Shared are not in it.
int contextOrder(int c)
{
    return c == CtxAcquisition ? 0 : c == CtxDiagnostics ? 1 : c == CtxCoaching ? 2 : -1;
}

// Directory → context (IM §4.1's Context column). Longest prefix wins.
const QVector<QPair<QString, int>> &dirContexts()
{
    static const QVector<QPair<QString, int>> t{
        // The Vocabulary areas, one by one: a new area has no context until it is placed.
        { QStringLiteral("Vocabulary/Analysis/"), CtxDiagnostics },
        { QStringLiteral("Vocabulary/Metrics/"), CtxDiagnostics },
        { QStringLiteral("Vocabulary/Recording/"), CtxAcquisition },     // the Recording contract's types, frame_decode with them
        { QStringLiteral("Vocabulary/Rig/"), CtxAcquisition },           // the rig's value types, part of its contract (RING-VOCABULARY)
        { QStringLiteral("Vocabulary/LaunchMonitor/"), CtxAcquisition }, // the reading types
        { QStringLiteral("Vocabulary/Identity/"), CtxShared },
        { QStringLiteral("Vocabulary/Club/"), CtxShared },
        { QStringLiteral("Vocabulary/Common/"), CtxShared },
        { QStringLiteral("Core/"), CtxPlatform },                       // Platform by its directory (R4); the device enumerator below
        { QStringLiteral("Pose/"), CtxDiagnostics },
        { QStringLiteral("Club/"), CtxDiagnostics },
        { QStringLiteral("Ball/"), CtxDiagnostics },
        { QStringLiteral("Body/"), CtxDiagnostics },                    // R13
        { QStringLiteral("Wrist/"), CtxDiagnostics },                   // R13
        { QStringLiteral("Skeleton3d/"), CtxDiagnostics },              // R13
        { QStringLiteral("Kinematics/"), CtxDiagnostics },              // with the Kalman smoother (R2, R13)
        { QStringLiteral("Timeline/"), CtxDiagnostics },
        { QStringLiteral("IMU/"), CtxDiagnostics },                     // golf fusion; the signal-level files and device back-ends below
        { QStringLiteral("Analysis/"), CtxDiagnostics },                // the pipeline and the stages; the MP4 reader below
        // The whole of Diagnostics, its derived facts and their series/ folder with it: one entry, so a re-cut of its
        // folders (B1.19) re-keys nothing here. The drill pack is named below.
        { QStringLiteral("Diagnostics/"), CtxDiagnostics },
        { QStringLiteral("Coaching/"), CtxCoaching },                   // the Coaching context's folder, empty until its first file
        { QStringLiteral("LaunchMonitor/"), CtxAcquisition },           // the connectors (R5, R13)
        { QStringLiteral("LaunchMonitor/inferred/"), CtxDiagnostics },  // the inferred reads (R5, R13)
        { QStringLiteral("Buffer/"), CtxAcquisition },                  // the ring and the stream types
        { QStringLiteral("Video/"), CtxAcquisition },
        { QStringLiteral("Ppcp/"), CtxAcquisition },
        { QStringLiteral("Audio/"), CtxAcquisition },                   // the input and the acoustic shot detector (R13); the output below
        { QStringLiteral("DomainModel/"), CtxHub },
        { QStringLiteral("App/"), CtxHub },
        { QStringLiteral("Stores/"), CtxHub },                          // the stores (R6)
        // The whole of Gui: presentation, with Gui/cameras, Gui/imu and Gui/shot's capture services, which stay in the
        // hub until Stage D splits capture (the deferral IM §4.1 records); app_info with them (R13).
        { QStringLiteral("Gui/"), CtxHub },
        { QStringLiteral("LLM/"), CtxHub },                             // R13, with STT, TTS, Secrets and Update
        { QStringLiteral("STT/"), CtxHub },
        { QStringLiteral("TTS/"), CtxHub },
        { QStringLiteral("Secrets/"), CtxHub },
        { QStringLiteral("Update/"), CtxHub },
    };
    return t;
}

// The files whose context is not their directory's. Path under src/ without the extension.
const QHash<QString, int> &fileContexts()
{
    static const QHash<QString, int> t{
        { QStringLiteral("Core/device_enumerator"), CtxAcquisition },        // R13
        { QStringLiteral("Analysis/mp4_frame_reader"), CtxHub },             // R13
        { QStringLiteral("Pose/motion_capture_probe"), CtxHub },             // R13: the hardware probe behind a setting
        { QStringLiteral("Audio/audio_output"), CtxHub },                    // R13: Audio's output
        { QStringLiteral("Audio/audio_output_base"), CtxHub },
        { QStringLiteral("Audio/ting_player"), CtxHub },
        // The drill pack stays Diagnostics until no Diagnostics file includes it; then Coaching (IM §2, "The packs").
        // Named, though its directory says the same, so that day is a one-line change.
        { QStringLiteral("Diagnostics/content/drill_pack"), CtxDiagnostics },
        // IMU: the device-level, calibration-procedure and signal-level files (IM §2's Acquisition row, L2; RING-RULES).
        { QStringLiteral("IMU/imu_calibration"), CtxAcquisition },
        { QStringLiteral("IMU/trunk_angles"), CtxAcquisition },
        { QStringLiteral("IMU/trunk_stand_tall"), CtxAcquisition },
        { QStringLiteral("IMU/orientation_filter"), CtxAcquisition },
        { QStringLiteral("IMU/iorientation_filter"), CtxAcquisition },
        { QStringLiteral("IMU/eskf_orientation_filter"), CtxAcquisition },
        { QStringLiteral("IMU/impact_detector"), CtxAcquisition },
        { QStringLiteral("IMU/hm_unit_id"), CtxAcquisition },
        { QStringLiteral("IMU/hm_session_types"), CtxAcquisition },
        { QStringLiteral("IMU/hm_capture_provenance"), CtxAcquisition },
        { QStringLiteral("IMU/hm_sample_convert"), CtxAcquisition },
        { QStringLiteral("IMU/hm_skew_stats"), CtxAcquisition },
        { QStringLiteral("IMU/trunk_ceremony"), CtxAcquisition },
        { QStringLiteral("IMU/trunk_ceremony_file"), CtxAcquisition },
        { QStringLiteral("IMU/imu_role_map"), CtxAcquisition },              // the role rules (rig_design.md §5: Rules, Acquisition)
        // IMU: the device back-ends (R13).
        { QStringLiteral("IMU/ble_adapter_pool"), CtxAcquisition },
        { QStringLiteral("IMU/ble_imu_transport"), CtxAcquisition },
        { QStringLiteral("IMU/imu_base"), CtxAcquisition },
        { QStringLiteral("IMU/imu_capabilities"), CtxAcquisition },
        { QStringLiteral("IMU/imu_factory"), CtxAcquisition },
        { QStringLiteral("IMU/imu_io_worker"), CtxAcquisition },
        { QStringLiteral("IMU/imu_sample_tap"), CtxAcquisition },
        { QStringLiteral("IMU/hm_session_worker"), CtxAcquisition },
        { QStringLiteral("IMU/wt9011dcl"), CtxAcquisition },
        { QStringLiteral("IMU/wt9011dcl_ble"), CtxAcquisition },
        { QStringLiteral("IMU/wt9011dcl_base"), CtxAcquisition },
        { QStringLiteral("IMU/wt901_config_check"), CtxAcquisition },
    };
    return t;
}

int contextOf(const QString &rel)
{
    const auto &files = fileContexts();
    const auto  f     = files.constFind(stemOf(rel));
    if (f != files.cend()) return f.value();
    int best = NoContext, bestLen = -1;
    for (const auto &d : dirContexts())
        if (rel.startsWith(d.first) && d.first.size() > bestLen) { best = d.second; bestLen = d.first.size(); }
    return best;
}

// The hub's contracts: the Domain model headers a context may include (R-CTX-1; Q26, "contracts, not owners"; R13's
// five, ruled by the Solution Architect, 24 Nov 2026; and rig_commands.h, by the Rig design addendum's decision 1,
// ruled 25 Nov 2026). Q26's test for each: it declares only interfaces and value types, and no owner class or service.
// Any other header joins by a ruling, when a context first needs it.
struct HubContract {
    const char *stem;
    const char *why;
};
const QVector<HubContract> &hubContracts()
{
    static const QVector<HubContract> t{
        { "DomainModel/recording_source", "RecordingSource: an immutable value, a ShotRef and the reader the Shot was given" },
        { "DomainModel/recording_reader", "RecordingReader, an interface, and its value types (LoadedRecording, RecordingLoadOptions)" },
        { "DomainModel/recording_repository", "RecordingRepository, an interface, and the value types its calls carry" },
        { "DomainModel/shot_command_values", "the Shot's commands' value types (LabelCorrectionNote, SessionIntentEdit, …)" },
        { "DomainModel/session_input", "the session's typed input, value types (DeclaredFocus, IntentRun, SessionInput)" },
        // Joined by the Rig design addendum's decision 1 (the Solution Architect, 25 Nov 2026; platform refactor C-5a.1).
        { "DomainModel/rig_commands", "the Rig's commands: interfaces the Rig implements (RigCameraCommands, RigSensorCommands, RigLearningCommands), and a value type (RigDeviceLabel); it includes only Vocabulary/Rig (the sensor commands' SensorFactsTable and RoleClaim among them), Vocabulary/Common/segment_role.h for the role type, Identity and Qt" },
    };
    return t;
}

bool isHubContract(const QString &target)
{
    const QString s = stemOf(target);
    for (const auto &c : hubContracts())
        if (s == QLatin1String(c.stem)) return true;
    return false;
}

// A9: why an include from context `from` into context `to` breaks R-CTX-1, or empty when it doesn't.
QString contextBreach(int from, int to, const QString &target)
{
    if (from == to || to == CtxPlatform || to == CtxShared) return {};   // the base is everyone's
    if (from == CtxPlatform || from == CtxShared)
        return QStringLiteral("R-CTX-1 (Q27): a %1 file includes %2's %3; Shared and Platform never depend outward")
            .arg(QLatin1String(contextName(from)), QLatin1String(contextName(to)), target);
    if (from == CtxHub) return {};                                        // the hub composes every context (Q26)
    if (to == CtxHub)
        return isHubContract(target)
                   ? QString()
                   : QStringLiteral("R-CTX-1 (Q26): %1 reaches Domain + Application past its contracts: includes %2, "
                                    "which is not in hubContracts()").arg(QLatin1String(contextName(from)), target);
    if (contextOrder(from) < contextOrder(to))
        return QStringLiteral("R-CTX-1: %1 includes the later context %2: %3")
            .arg(QLatin1String(contextName(from)), QLatin1String(contextName(to)), target);
    return {};
}

int g_contextCrossings = 0;   // includes that cross a context (A9 reads each); reset after the fixtures

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
    return stem == QLatin1String("Stores/swing_paths")
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
    // EVERY FILE-SESS AND FILE-ATH NAME (information_model.md §8.1), owned by SessionFiles / AthleteFiles
    // (IM-S2-6: DEC-160; complete since step 2.8b). A writer outside the repositories (the report, the trunk
    // ceremony, the diagnostics model) calls the owner's name function; it never spells the name itself.
    static const QVector<OwnedFile> t{
        // the session folder (FILE-SESS-*)
        { "session.json",       R"(session\.json)",                           "Stores/session_files" },   // FILE-SESS-01 (step 2.2)
        { "session_input.json", R"(session_input\.json)",                     "Stores/session_files" },   // FILE-SESS-02 (step 2.5: SessionDiagnosticsFiles' names)
        { "diagnostics.json",   R"(diagnostics\.json)",                       "Stores/session_files" },   // FILE-SESS-03, -12 (step 2.5)
        { "club_baseline.json", R"(club_baseline\.json)",                     "Stores/session_files" },   // FILE-SESS-04 (step 2.5)
        { "work_ons.json",      R"(work_ons\.json)",                          "Stores/session_files" },   // FILE-SESS-05 (step 2.5)
        { "archived.json",      R"(archived\.json)",                          "Stores/session_files" },   // FILE-SESS-06 (step 2.6)
        { "report_meta.json",   R"(report_meta\.json)",                       "Stores/session_files" },   // FILE-SESS-07 (D9: names now; writes through SessionRepository since 5.6a)
        { "report_input.json",  R"(report_input\.json)",                      "Stores/session_files" },   // FILE-SESS-13 (step 5.6a)
        { "*_session-report.pdf", R"((?:[^/]*_)?session-report\.pdf)",        "Stores/session_files" },   // FILE-SESS-08 (D9)
        { "report-stills/",     R"(report-stills/?)",                          "Stores/session_files" },   // FILE-SESS-09 (D9; in the cache)
        { ".pinpoint-last-swing", R"(\.pinpoint-last-swing)",                 "Stores/session_files" },   // FILE-SESS-10 (step 2.2)
        { "trunk_ceremony*.json", R"(trunk_ceremony(?:_[^/]*)?\.json)",       "Stores/session_files" },   // FILE-SESS-11 (names only; Studio/Rig writes it)
        // the athlete folder (FILE-ATH-*)
        { "athlete.json",       R"(athlete\.json)",                           "Stores/athlete_files" },   // FILE-ATH-01 (step 2.3: AthleteRepository's names)
        { "tendencies.json",    R"(tendencies\.json)",                        "Stores/athlete_files" },   // FILE-ATH-02 (step 2.5: names only; written by the work-ons controller until step 3)
        { "swing_themes*.json", R"(swing_themes(?:\.[^/]*)?\.json|swing_themes)","Stores/athlete_files" },   // FILE-ATH-03 (step 2.5: likewise)
        { "outcome_corrections.jsonl", R"(outcome_corrections\.jsonl)",       "Stores/athlete_files" },   // FILE-ATH-04 (the append: AthleteFiles::appendTuningLine)
        // the machine's rig (FILE-APP-07): its one store spells it (rig_design.md §4.4; platform refactor C-4)
        { "rig.json",           R"(rig\.json)",                               "Stores/rig_store" },
    };
    return t;
}

// ── A5 / A8: which files the rule reads, and what it looks for ─────────────────────────────────
bool isRulesIoDir(const QString &rel)
{
    if (rel.startsWith(QLatin1String("Analysis/"))) return ringOf(rel) == Rules;     // 03 §11 "Analysis rules"
    // B1.19 re-keyed the old folders' image: content/'s files from engine/ and norms/ by name (the packs' loaders,
    // from packs/, were never in it).
    static const char *const contentStems[] = {
        "Diagnostics/content/provider_leaf_p.", "Diagnostics/content/condition_reading.", "Diagnostics/content/norm_pack.",
        "Diagnostics/content/norm_provider.", "Diagnostics/content/file_norm_provider.", "Diagnostics/content/memory_norm_provider.",
        "Diagnostics/content/merged_norm_provider.", "Diagnostics/content/resource_norm_provider." };
    for (const char *s : contentStems)
        if (rel.startsWith(QLatin1String(s))) return true;
    return rel.startsWith(QLatin1String("Diagnostics/model/")) || rel.startsWith(QLatin1String("Diagnostics/derived/"))
        || rel.startsWith(QLatin1String("Diagnostics/engine/"));
}

// A5 (P11): the files allowed to touch the disk — the stores and file doors of src/Stores, and the Platform
// ring's own helpers (the atomic write, the folder lock, logging). A store door is part of its store, so it is
// exempt with the rest of src/Stores (R9); the IM names each door with its store.
bool a5Exempt(const QString &rel)
{
    return rel.startsWith(QLatin1String("Stores/")) || ringOf(rel) == Platform;
}

// A5 (P11): the repositories — the interfaces the domain model defines (P4) and the stores and file doors in
// src/Stores that implement them. Only src/Stores and src/DomainModel (the owning objects) include one.
bool isRepositoryHeader(const QString &target)
{
    static const QSet<QString> t{
        QStringLiteral("DomainModel/recording_repository"), QStringLiteral("DomainModel/recording_reader"),
        QStringLiteral("DomainModel/session_repository"),   QStringLiteral("DomainModel/athlete_repository"),
        QStringLiteral("Stores/swing_store"),               QStringLiteral("Stores/swing_store_repository"),
        QStringLiteral("Stores/recording_loader"),          QStringLiteral("Stores/swing_doc"),
        QStringLiteral("Stores/session_store"),             QStringLiteral("Stores/session_archiver"),
        QStringLiteral("Stores/session_files"),             QStringLiteral("Stores/session_diagnostics_files"),
        // The diagnostics files' file work, moved out of session_diagnostics_files.h for the store (step 3.4a, J-42).
        QStringLiteral("Stores/session_diagnostics_io"),
        QStringLiteral("Stores/athlete_store"),             QStringLiteral("Stores/athlete_files"),
        // The packs and norms' door (refactor step 3.2; D5: DEC-170): RuleData includes the interface,
        // the store implements it. A composition root reaches the owner through Stores/rule_data_install.h.
        QStringLiteral("DomainModel/rule_data_repository"), QStringLiteral("Stores/rule_data_store"),
        // The phase grid's file door (FILE-SHOT-07; platform refactor P-3): the model browser's folder read includes it (B1.14, R11).
        QStringLiteral("Stores/phase_grid_store"),
        // The Rig's repository (FILE-APP-07; platform refactor C-4): only the Rig and its store include it.
        QStringLiteral("DomainModel/rig_repository"),
    };
    return t.contains(stemOf(target));
}

// A10 (rig_design.md §6 rule 8): the files that may include the Rig's header, DomainModel/rig.h.
bool mayIncludeRig(const QString &rel)
{
    const QString s = stemOf(rel);
    return s == QLatin1String("DomainModel/rig") || s == QLatin1String("Gui/main") || rel.startsWith(QLatin1String("App/"))
        || s == QLatin1String("Gui/app/rig_presenter") || s == QLatin1String("Stores/rig_store");
}

// A5: the line with its pure path text blanked (not access): QDir's path-string statics and enum values, a
// temporary QDir asked only for a path or its name, and a temporary QFileInfo asked only for a path or a name part.
QString withoutPathText(const QString &line)
{
    static const QRegularExpression dirPure(QStringLiteral(
        R"(\bQDir::(?:cleanPath|separator|toNativeSeparators|fromNativeSeparators|isAbsolutePath|isRelativePath|listSeparator|[A-Z]\w*)\b)"));
    static const QRegularExpression infoName(QStringLiteral(
        R"(\bQFileInfo\s*\((?:[^()]|\([^()]*\))*\)\s*\.\s*(?:fileName|baseName|completeBaseName|suffix|completeSuffix|path|absolutePath|filePath|absoluteFilePath)\s*\(\s*\))"));
    static const QRegularExpression dirName(QStringLiteral(
        R"(\bQDir\s*\((?:[^()]|\([^()]*\))+\)\s*\.\s*(?:dirName|path|absolutePath|filePath|absoluteFilePath|relativeFilePath)\s*\()"));
    QString l = line;
    l.replace(infoName, QStringLiteral(" "));
    l.replace(dirName, QStringLiteral(" ("));
    l.replace(dirPure, QStringLiteral(" "));
    return l;
}

// A8: the settings handle. It is Platform (R3), so the ring test below would pass it; it is matched by name.
bool isSettingsHandle(const QString &target)
{
    return stemOf(target) == QLatin1String("Core/pp_settings");
}

// An include that takes src/Analysis past the Recording contract (A8).
bool isAcquisitionOrAppState(const QString &target)
{
    const QString s = stemOf(target);
    // named application state, matched before the ring test so that a file's ring never hides it
    if (isSettingsHandle(target) || s == QLatin1String("App/studio_profile")
        || s == QLatin1String("Core/device_enumerator"))
        return true;
    const int r = ringOf(target);
    if (target.startsWith(QLatin1String("Gui/"))) return true;            // application state, live controllers
    if (r != Adapters) return false;
    static const QStringList acq{ QStringLiteral("Buffer/"), QStringLiteral("Video/"), QStringLiteral("Audio/"),
                                  QStringLiteral("Ppcp/"), QStringLiteral("IMU/"), QStringLiteral("LaunchMonitor/") };
    for (const QString &d : acq)
        if (target.startsWith(d)) return true;
    return false;
}

// ── Source text: Source, strip(), makeSource(), lineAt() are in lint_source_text.h (shared with namespace_collision_test)
using pp_lint::Source;
using pp_lint::isIdent;
using pp_lint::strip;
using pp_lint::makeSource;
using pp_lint::lineAt;

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
        // A1 / A5 / A8 / A9 — includes
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
            else if ((ring == Adapters || rel.startsWith(QLatin1String("Vocabulary/Recording/")))
                     && !rel.startsWith(QLatin1String("Gui/")) && target.startsWith(QLatin1String("Gui/")))
                out.append({ QStringLiteral("A1"), li + 1,
                             QStringLiteral("an adapter outside Gui reaches the UI (DEC-146): includes %1").arg(target) });
            if (!a5Exempt(rel) && !rel.startsWith(QLatin1String("DomainModel/")) && isRepositoryHeader(target))
                out.append({ QStringLiteral("A5"), li + 1,
                             QStringLiteral("includes %1: a repository outside its owner (P11: reach the object, never its repository)").arg(target) });
            if (rel.startsWith(QLatin1String("Analysis/")) && isAcquisitionOrAppState(target))
                out.append({ QStringLiteral("A8"), li + 1,
                             QStringLiteral("Analysis reaches past the Recording contract: includes %1").arg(target) });
            else if ((ring == Vocabulary || ring == Rules || ring == DomainModel) && isSettingsHandle(target))
                out.append({ QStringLiteral("A8"), li + 1,
                             QStringLiteral("%1 reads application state: includes %2 (the settings handle; R3)")
                                 .arg(QLatin1String(ringName(ring)), target) });
            // A9 — R-CTX-1: the contexts' order, the hub's contracts, and a base that never depends outward
            const int cs = contextOf(rel), ct = contextOf(target);
            if (cs != NoContext && ct != NoContext && cs != ct) {
                ++g_contextCrossings;
                const QString why = contextBreach(cs, ct, target);
                if (!why.isEmpty()) out.append({ QStringLiteral("A9"), li + 1, why });
            }
            // A10 — the Rig's header, only where the Rig is held
            if (stemOf(target) == QLatin1String("DomainModel/rig") && !mayIncludeRig(rel))
                out.append({ QStringLiteral("A10"), li + 1,
                             QStringLiteral("includes %1: only the composition root, app.rig and the Rig's store hold the Rig "
                                            "(rig_design.md §6 rule 8)").arg(target) });
        }
        // A5 — file or directory access outside the repositories (P11); in a rules directory, its old message
        if (!a5Exempt(rel)) {
            static const QRegularExpression io(QStringLiteral(
                R"(\b(?:QFile|QSaveFile|QDir|QFileInfo|QDirIterator|QTemporaryFile|QTemporaryDir|atomicWrite\w*|FolderLock)\b|\bstd::[io]?fstream\b|\bfopen\s*\()"));
            static const QRegularExpression includeLine(QStringLiteral(R"(^\s*#\s*include\b)"));
            const bool rules = isRulesIoDir(rel);
            for (int li = 0; li < s.codeLines.size(); ++li) {
                if (includeLine.match(s.codeLines.at(li)).hasMatch()) continue;     // <QFile> is not access; its uses are
                const auto m = io.match(withoutPathText(s.codeLines.at(li)));
                if (m.hasMatch())
                    out.append({ QStringLiteral("A5"), li + 1,
                                 m.captured(0) + (rules ? QStringLiteral(": file I/O in a rules directory (repositories load, rules compute)")
                                                        : QStringLiteral(": file access outside the repositories (P11: an object's persistence is its own)")) });
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
    for (const char *f : { "Gui/shot/shot_processor.h", "Gui/athlete/athlete_controller.h", "Diagnostics/derived/shot_factors.h",
                           "Vocabulary/Metrics/metric_catalogue.h", "Buffer/source_ring.h", "Buffer/types.h", "Core/pp_debug.h",
                           "Vocabulary/Analysis/swing_analysis.h", "Analysis/analysis_pipeline.h", "Stores/session_store.h",
                           "Stores/swing_paths.h", "DomainModel/session_repository.h", "DomainModel/athlete.h",
                           "Core/pp_settings.h", "Vocabulary/Recording/recording.h", "Vocabulary/Common/figure_format.h",
                           "Vocabulary/Analysis/phase_grid.h", "DomainModel/session.h", "DomainModel/recording_source.h",
                           "Coaching/plan.h", "IMU/imu_base.h", "DomainModel/rig.h", "DomainModel/rig_repository.h",
                           "DomainModel/rig_commands.h" }) {
        t.files.insert(QString::fromLatin1(f));
        t.byName[QFileInfo(QString::fromLatin1(f)).fileName()].append(QString::fromLatin1(f));
    }
    auto run = [&](const char *rel, const QString &text) { return scan(makeSource(QString::fromLatin1(rel), text), t); };

    check(countRule(run("Diagnostics/derived/x.cpp", QStringLiteral("#include \"../Gui/shot/shot_processor.h\"\n")), "A1") == 1,
          QStringLiteral("A1 catches a rule including a Gui header"));
    check(countRule(run("Vocabulary/Metrics/x.h", QStringLiteral("#include \"shot_factors.h\"\n")), "A1") == 1,
          QStringLiteral("A1 resolves a bare include by its unique basename (Vocabulary → Rules)"));
    check(countRule(run("Diagnostics/derived/x.cpp", QStringLiteral("#include \"../Vocabulary/Metrics/metric_catalogue.h\"\n#include \"pp_debug.h\"\n"
                                                         "#include \"../Buffer/types.h\"\n#include <QFile>\n")), "A1") == 0,
          QStringLiteral("A1 passes inward includes, Platform logging, the Recording's types and system headers"));
    check(countRule(run("Diagnostics/derived/x.cpp", QStringLiteral("// #include \"../Gui/shot/shot_processor.h\"\n")), "A1") == 0,
          QStringLiteral("A1 ignores a commented-out include"));
    check(countRule(run("Vocabulary/Analysis/swing_analysis.h", QStringLiteral("#include \"../../Analysis/analysis_pipeline.h\"\n")), "A1") == 1,
          QStringLiteral("A1 catches the Vocabulary folder including an Analysis stage (Vocabulary → Application)"));
    const auto exportToGui = run("Stores/x.cpp", QStringLiteral("#include \"../Gui/athlete/athlete_controller.h\"\n#include \"swing_paths.h\"\n"));
    check(countRule(exportToGui, "A1") == 1 && exportToGui.first().text.contains(QLatin1String("Gui/athlete/athlete_controller.h")),
          QStringLiteral("A1 catches Stores including a Gui header inside the Adapters ring (DEC-146); passes Stores → Stores"));
    check(countRule(run("Buffer/source_ring.cpp", QStringLiteral("#include \"athlete_controller.h\"\n")), "A1") == 1
              && countRule(run("Gui/x/y.cpp", QStringLiteral("#include \"../athlete/athlete_controller.h\"\n")), "A1") == 0,
          QStringLiteral("A1 catches any other adapter outside Gui including Gui; passes Gui → Gui"));
    check(countRule(run("Analysis/x.cpp", QStringLiteral("#include \"source_ring.h\"\n#include \"../Gui/athlete/athlete_controller.h\"\n"
                                                         "#include \"types.h\"\n")), "A8") == 2,
          QStringLiteral("A8 catches Analysis including the ring buffer and app state, passes the Recording's types"));
    check(countRule(run("Analysis/x.cpp", QStringLiteral("#include \"../Core/pp_settings.h\"\n")), "A8") == 1
              && countRule(run("Analysis/seg_resolve.cpp", QStringLiteral("#include \"../Core/pp_settings.h\"\n")), "A8") == 1
              && countRule(run("Vocabulary/Metrics/x.h", QStringLiteral("#include \"../../Core/pp_settings.h\"\n")), "A8") == 1
              && countRule(run("Diagnostics/derived/x.cpp", QStringLiteral("#include \"../../Core/pp_settings.h\"\n")), "A8") == 1
              && countRule(run("DomainModel/x.cpp", QStringLiteral("#include \"../Core/pp_settings.h\"\n")), "A8") == 1,
          QStringLiteral("A8 catches the settings handle (Platform, R3) included from Analysis and from the Vocabulary, Rules "
                         "and Domain-model rings"));
    const auto settingsOuter = run("Gui/x/y.cpp", QStringLiteral("#include \"../../Core/pp_settings.h\"\n"));
    check(countRule(settingsOuter, "A8") == 0 && countRule(settingsOuter, "A1") == 0
              && countRule(run("Stores/x.cpp", QStringLiteral("#include \"../Core/pp_settings.h\"\n")), "A8") == 0
              && countRule(run("Core/x.cpp", QStringLiteral("#include \"pp_settings.h\"\n")), "A1") == 0,
          QStringLiteral("A8 and A1 pass the settings handle from the adapters and from Platform"));
    const auto coreNew = run("Core/x.cpp", QStringLiteral("#include \"../Vocabulary/Metrics/metric_catalogue.h\"\n"));
    check(countRule(coreNew, "A1") == 1 && coreNew.first().text.contains(QStringLiteral("Platform → Vocabulary"))
              && countRule(run("Core/device_enumerator.cpp", QStringLiteral("#include \"../Buffer/source_ring.h\"\n")), "A1") == 0,
          QStringLiteral("A1 treats a new Core file as Platform (it may not include Vocabulary); the device enumerator, "
                         "Core's one recorded exception, is an adapter (R4)"));
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
    check(countRule(run("Stores/swing_paths.cpp", parse), "A3") == 0, QStringLiteral("A3 allows SwingPaths"));
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
        "// \"tendencies.json\" in a comment\n#include \"../../Diagnostics/derived/swing_themes.h\"\n");
    check(countRule(run("Gui/x/y.cpp", names), "A4") == 3,
          QStringLiteral("A4 catches a name or a path ending in it; passes a log sentence, a comment and a header"));
    check(countRule(run("Stores/session_files.h", QStringLiteral("auto b = \"work_ons.json\";\n")), "A4") == 0,
          QStringLiteral("A4 allows the owner"));
    check(countRule(run("Gui/app/x.cpp", QStringLiteral("auto p = folder + QStringLiteral(\"/rig.json\");\n")), "A4") == 1
              && countRule(run("Stores/rig_store.cpp", QStringLiteral("const QString n = QStringLiteral(\"rig.json\");\n")), "A4") == 0,
          QStringLiteral("A4 catches rig.json spelled outside the Rig's store (FILE-APP-07), and allows the store"));
    // A10: the Rig's header only where the Rig is held (rig_design.md §6 rule 8)
    const QString incRig = QStringLiteral("#include \"../../DomainModel/rig.h\"\n");
    check(countRule(run("Gui/cameras/camera_manager.cpp", incRig), "A10") == 1
              && countRule(run("Gui/app/app_settings.cpp", incRig), "A10") == 1
              && countRule(run("DomainModel/session.cpp", QStringLiteral("#include \"rig.h\"\n")), "A10") == 1,
          QStringLiteral("A10 catches rig.h included by a manager, a presenter that is not app.rig, and another domain object"));
    check(countRule(run("Gui/main.cpp", QStringLiteral("#include \"../DomainModel/rig.h\"\n")), "A10") == 0
              && countRule(run("Gui/app/rig_presenter.cpp", incRig), "A10") == 0
              && countRule(run("Stores/rig_store.h", QStringLiteral("#include \"../DomainModel/rig.h\"\n")), "A10") == 0
              && countRule(run("App/app.cpp", QStringLiteral("#include \"../DomainModel/rig.h\"\n")), "A10") == 0
              && countRule(run("DomainModel/rig.cpp", QStringLiteral("#include \"rig.h\"\n")), "A10") == 0
              && countRule(run("Gui/cameras/camera_manager.cpp", QStringLiteral("#include \"../../DomainModel/rig_repository.h\"\n")), "A10") == 0,
          QStringLiteral("A10 passes the composition root, app.rig, the store and rig.cpp, and reads only rig.h"));
    check(countRule(run("Gui/cameras/camera_manager.cpp", QStringLiteral("#include \"../../DomainModel/rig_repository.h\"\n")), "A5") == 1
              && countRule(run("Stores/rig_store.h", QStringLiteral("#include \"../DomainModel/rig_repository.h\"\n")), "A5") == 0,
          QStringLiteral("A5 catches the Rig's repository included outside the Rig and its store"));
    check(countRule(run("Gui/x/y.cpp", QStringLiteral(
              "auto a = QStringLiteral(\"session.json\");\nauto b = QStringLiteral(\"_session-report.pdf\");\n"
              "auto c = dir.filePath(QStringLiteral(\"trunk_ceremony_%1.json\"));\nauto d = QStringLiteral(\"report-stills/\");\n"
              "auto e = QStringLiteral(\"outcome_corrections.jsonl\");\nauto f = QStringLiteral(\"pinpoint.session/1\");\n"
              "auto g = QStringLiteral(\"pinpoint.trunk_ceremony/1\");\n")), "A4") == 5,
          QStringLiteral("A4 covers every session and athlete file (session.json, the report PDF, the ceremony, the stills, "
                         "the tuning log); passes a schema name"));
    check(countRule(run("Stores/athlete_files.h", QStringLiteral("auto e = \"outcome_corrections.jsonl\";\n")), "A4") == 0
              && countRule(run("Gui/diagnostics/session_diagnostics_model.cpp", QStringLiteral("auto p = \"tendencies.json\";\n")), "A4") == 1,
          QStringLiteral("A4: the athlete files' owner is AthleteFiles (the diagnostics model spells no athlete file; FILE-ATH-05 is retired, 3.8-ii)"));
    check(countRule(run("Diagnostics/derived/x.cpp", QStringLiteral("#include <QFile>\nQFile f(p);\nQString s = \"QDir\";\nstd::ifstream in(p);\n")), "A5") == 2,
          QStringLiteral("A5 catches file I/O lines in a rules directory, not a word in a string or the <QFile> include"));
    const auto charts = run("Gui/charts/x.cpp", QStringLiteral(
        "QFile f(p);\nQDir(d).mkpath(\".\");\natomicWrite(p, bytes);\nFolderLock lock(dir);\n#include \"../../Stores/session_store.h\"\n"
        "auto n = QFileInfo(p).fileName();\nauto c = QDir::cleanPath(a + QDir::separator() + b);\nauto e = d.entryList(QDir::Files);\n"
        "auto f = QDir(d).filePath(QFileInfo(p).absolutePath());\nauto g = QDir(QFileInfo(p).path()).dirName();\n"
        "if (QFileInfo::exists(p)) {}\n"));
    check(countRule(charts, "A5") == 6 && charts.first().text.contains(QLatin1String("P11")),
          QStringLiteral("A5 (P11) catches a Charts file opening a QFile, making a folder, writing atomically, taking the lock, "
                         "asking whether a file exists and including a store; passes a name part, path text and a QDir enum"));
    check(countRule(run("Stores/session_store.cpp", QStringLiteral("QFile f(p);\natomicWrite(p, b);\n#include \"../DomainModel/session_repository.h\"\n")), "A5") == 0
              && countRule(run("Core/atomic_write.h", QStringLiteral("QSaveFile f(p);\n")), "A5") == 0,
          QStringLiteral("A5 (P11) allows a store and the Platform ring's own helpers"));
    check(countRule(run("Stores/session_list_reads.cpp", QStringLiteral("auto t = QFileInfo(p).lastModified();\n")), "A5") == 0
              && countRule(run("Stores/session_index_summary.cpp", QStringLiteral("#include \"session_store.h\"\nQFile f(p);\n")), "A5") == 0
              && countRule(run("Stores/rule_data_reads.cpp", QStringLiteral("QFile f(path);\nQDir(d).entryList(QDir::Files);\n")), "A5") == 0
              && countRule(run("Gui/session/session_list_presenter.cpp", QStringLiteral("auto t = QFileInfo(p).lastModified();\n")), "A5") == 1
              && countRule(run("Gui/session/x.cpp", QStringLiteral("#include \"../../Stores/session_store.h\"\n")), "A5") == 1,
          QStringLiteral("A5 (R9): a store door is part of its store, so the session list's two doors and the rule data's reads "
                         "pass a file read and a repository include; the same read or include in a view still fails"));
    check(countRule(run("DomainModel/athlete.cpp", QStringLiteral("#include \"session_repository.h\"\n#include \"athlete.h\"\n")), "A5") == 0
              && countRule(run("DomainModel/athlete.cpp", QStringLiteral("QFile f(p);\n")), "A5") == 1
              && countRule(run("Gui/x/y.cpp", QStringLiteral("#include \"../../DomainModel/session_repository.h\"\n#include \"../../DomainModel/athlete.h\"\n")), "A5") == 1,
          QStringLiteral("A5 (P11): the owning objects include their repository but touch no file; a view reaches the object, "
                         "never its repository"));
    const auto reanalyzer = run("Analysis/swing_reanalyzer.cpp", QStringLiteral("QFile f(p);\n"));
    check(countRule(run("Analysis/seg_resolve.h", QStringLiteral("QFile f(p);\n")), "A5") == 1
              && countRule(reanalyzer, "A5") == 1 && reanalyzer.first().text.contains(QLatin1String("P11")),
          QStringLiteral("A5 reads Analysis's rule files as a rules directory, its orchestration as P11"));
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
    const auto a7 = run("Diagnostics/derived/x.cpp", longFn);
    check(countRule(a7, "A7") == 1 && a7.first().text.startsWith(QLatin1String("big is 306 lines")),
          QStringLiteral("A7 finds a 306-line function in a rules ring and measures it (got: %1)")
              .arg(a7.isEmpty() ? QStringLiteral("none") : a7.first().text));
    check(countRule(run("Gui/x/y.cpp", longFn), "A7") == 0, QStringLiteral("A7 reads rings 1–2 only"));
    QString longSig = QStringLiteral("struct S { int a; };\ninline int composed(");
    for (int i = 0; i < 12; ++i) longSig += QStringLiteral("const std::vector<SomeLongTypeName> &argumentNumber%1, ").arg(i);
    longSig += QStringLiteral("const Options &o = {})\n{\n");
    for (int i = 0; i < 300; ++i) longSig += QStringLiteral("  ++x;\n");
    longSig += QStringLiteral("  return 0;\n}\n");
    const auto a7s = run("Diagnostics/derived/x.h", longSig);
    check(countRule(a7s, "A7") == 1 && a7s.first().text.startsWith(QLatin1String("composed is")),
          QStringLiteral("A7 finds a long function whose signature is longer than 240 characters and has a {} default (got: %1)")
              .arg(a7s.isEmpty() ? QStringLiteral("none") : a7s.first().text));
    // A9: one passing and one failing case per clause; each failing case must fail for its own clause.
    const auto a9 = [&](const char *rel, const char *text) { return run(rel, QString::fromLatin1(text)); };
    const auto a9Says = [](const QVector<Finding> &fs, const char *words) {
        int n = 0;
        for (const auto &f : fs)
            if (f.rule == QLatin1String("A9")) n += f.text.contains(QLatin1String("R-CTX-1")) && f.text.contains(QLatin1String(words)) ? 1 : 0;
        return n;
    };
    // (1) the order: an earlier context never includes a later one
    const auto acqToDiag  = a9("Video/x.cpp", "#include \"../Diagnostics/derived/shot_factors.h\"\n");
    const auto diagToCoa  = a9("Diagnostics/derived/x.cpp", "#include \"../../Coaching/plan.h\"\n");
    check(countRule(acqToDiag, "A9") == 1 && a9Says(acqToDiag, "includes the later context Diagnostics") == 1
              && countRule(diagToCoa, "A9") == 1 && a9Says(diagToCoa, "includes the later context Coaching") == 1,
          QStringLiteral("A9 catches an earlier context including a later one (Acquisition → Diagnostics, Diagnostics → Coaching)"));
    check(countRule(a9("Diagnostics/derived/x.cpp", "#include \"../../Vocabulary/Recording/recording.h\"\n"
                                                    "#include \"../../IMU/imu_base.h\"\n"), "A9") == 0
              && countRule(a9("Coaching/x.cpp", "#include \"../Diagnostics/derived/shot_factors.h\"\n"
                                                "#include \"../Vocabulary/Recording/recording.h\"\n"), "A9") == 0
              && countRule(a9("Video/x.cpp", "// #include \"../Diagnostics/derived/shot_factors.h\"\n"), "A9") == 0,
          QStringLiteral("A9 passes a later context including an earlier one, and ignores a commented-out include"));
    // (2) the hub: reached only through its contracts; the hub itself composes every context
    const auto pastContract = a9("Diagnostics/derived/x.cpp", "#include \"../../DomainModel/session.h\"\n");
    check(countRule(pastContract, "A9") == 1 && a9Says(pastContract, "past its contracts") == 1,
          QStringLiteral("A9 catches a context including a hub header that is not a contract (DomainModel/session.h)"));
    check(countRule(a9("Video/x.cpp", "#include \"../DomainModel/rig_commands.h\"\n"), "A9") == 0
              && countRule(a9("Video/x.cpp", "#include \"../DomainModel/rig.h\"\n"), "A9") == 1,
          QStringLiteral("A9 passes a context including the Rig's commands (a contract, decision 1), and catches one including rig.h"));
    check(countRule(a9("Analysis/x.cpp", "#include \"../DomainModel/recording_source.h\"\n"), "A9") == 0
              && countRule(a9("Gui/x/y.cpp", "#include \"../../Diagnostics/derived/shot_factors.h\"\n"
                                             "#include \"../../IMU/imu_base.h\"\n#include \"../../Coaching/plan.h\"\n"), "A9") == 0,
          QStringLiteral("A9 passes a context including a hub contract, and the hub including every context"));
    // (3) the base: Shared and Platform include no context's file; every context may include them
    const auto sharedOut   = a9("Vocabulary/Common/x.h", "#include \"../Analysis/phase_grid.h\"\n");
    const auto platformOut = a9("Core/x.cpp", "#include \"../IMU/imu_base.h\"\n");
    check(countRule(sharedOut, "A9") == 1 && a9Says(sharedOut, "never depend outward") == 1
              && countRule(platformOut, "A9") == 1 && a9Says(platformOut, "never depend outward") == 1,
          QStringLiteral("A9 catches a Shared file including a context's file (Vocabulary/Common → Vocabulary/Analysis), and Platform the same"));
    check(countRule(a9("Vocabulary/Common/x.h", "#include \"figure_format.h\"\n#include \"../../Core/pp_debug.h\"\n"), "A9") == 0
              && countRule(a9("IMU/imu_base.cpp", "#include \"../Vocabulary/Common/figure_format.h\"\n#include \"../Core/pp_debug.h\"\n"), "A9") == 0,
          QStringLiteral("A9 passes Shared including Shared and Platform, and a context including both"));
    // the context table: a file's entry over its directory's; a directory in neither table has no context
    check(contextOf(QStringLiteral("IMU/imu_calibration.h")) == CtxAcquisition && contextOf(QStringLiteral("IMU/hm_frame.h")) == CtxDiagnostics
              && contextOf(QStringLiteral("IMU/wt9011dcl.cpp")) == CtxAcquisition && contextOf(QStringLiteral("Analysis/mp4_frame_reader.cpp")) == CtxHub
              && contextOf(QStringLiteral("Audio/ting_player.h")) == CtxHub && contextOf(QStringLiteral("Audio/audio_input.h")) == CtxAcquisition
              && contextOf(QStringLiteral("Core/device_enumerator.h")) == CtxAcquisition && contextOf(QStringLiteral("Core/pp_settings.h")) == CtxPlatform
              && contextOf(QStringLiteral("LaunchMonitor/inferred/lm_gates.h")) == CtxDiagnostics
              && contextOf(QStringLiteral("LaunchMonitor/gcquad_monitor.cpp")) == CtxAcquisition
              && contextOf(QStringLiteral("Diagnostics/content/drill_pack.h")) == CtxDiagnostics
              && contextOf(QStringLiteral("Diagnostics/derived/series/session_curves.h")) == CtxDiagnostics
              && contextOf(QStringLiteral("Gui/shot/shot_arbiter.h")) == CtxHub && contextOf(QStringLiteral("Vocabulary/Rig/rig_state.h")) == CtxAcquisition
              && contextOf(QStringLiteral("Vocabulary/Identity/identity.h")) == CtxShared
              && contextOf(QStringLiteral("Vocabulary/NewArea/x.h")) == NoContext && contextOf(QStringLiteral("NoSuchDir/x.h")) == NoContext,
          QStringLiteral("the context table: per-file entries over directory defaults; a new directory or Vocabulary area has no context"));
    check(ringOf(QStringLiteral("Analysis/analysis_pipeline.cpp")) == Application && ringOf(QStringLiteral("Vocabulary/Analysis/gate_record.h")) == Vocabulary
              && ringOf(QStringLiteral("Core/pp_debug.cpp")) == Platform && ringOf(QStringLiteral("Gui/diagnostics/x.h")) == Adapters
              && ringOf(QStringLiteral("Core/pp_settings.h")) == Platform && ringOf(QStringLiteral("Core/version.h")) == Platform
              && ringOf(QStringLiteral("Core/zz_new_file.h")) == Platform && ringOf(QStringLiteral("Core/device_enumerator.h")) == Adapters
              && ringOf(QStringLiteral("Kinematics/kf3_smoother.h")) == Rules && ringOf(QStringLiteral("Gui/app/app_info.cpp")) == Adapters
              && ringOf(QStringLiteral("LaunchMonitor/inferred/lm_gates.h")) == Rules && ringOf(QStringLiteral("LaunchMonitor/gcquad_monitor.cpp")) == Adapters
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
    static const QRegularExpression tag(QStringLiteral(
        R"(#\s*(step\s+[1-8](?:\.\d+)?|platform refactor \((?:capture|settings|#224)\)|#222|not domain \(§8\.3\)|unplanned|kept(?: \([^()]+\))?)\s*$)"));
    // A9 and A10 are not in it: each lands with no allow-list line (IM §1.4), so a line for either is refused as malformed.
    static const QRegularExpression ruleRe(QStringLiteral(R"(^A[1-8]$)"));
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString l = lines.at(i).trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = l.split(QLatin1Char('|'));
        const auto        t     = tag.match(l);
        // The WHY is everything between the count and the tag; the tag may hold a '#' of its own ("(#224)", "#222").
        const QString     why   = t.hasMatch() ? l.left(t.capturedStart(0)).split(QLatin1Char('|')).mid(3).join(QLatin1Char('|')).trimmed()
                                               : QString();
        const bool        okLine = parts.size() >= 4 && ruleRe.match(parts.at(1).trimmed()).hasMatch()
                            && parts.at(2).trimmed().toInt() > 0 && t.hasMatch() && !why.isEmpty();
        if (!okLine) {
            check(false, QStringLiteral("allow-list line %1 is path | rule | count | WHY # step N (or # step N.n / # platform refactor (capture|settings|#224) / "
                                 "# #222 / # not domain (§8.3) / # unplanned / # kept [(reason)]): %2").arg(i + 1).arg(l));
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
    g_contextCrossings = 0;   // the fixtures' crossings are not the tree's

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

    // A9's tables: every C++ file has a context or is Platform, and no entry is stale (a stale key checks nothing).
    QStringList noContext, stale;
    for (const QString &f : tree.files)
        if (contextOf(f) == NoContext) noContext << f;
    noContext.sort();
    for (const QString &u : noContext) std::printf("      %s\n", qPrintable(u));
    check(noContext.isEmpty(), QStringLiteral("every C++ file has a context, or is Platform, in A9's tables (IM §4.1's Context "
                                              "column, as R13 placed it): a new directory is placed on purpose, by a ruling"));
    for (const auto &d : dirContexts()) {
        if (d.first == QLatin1String("Coaching/")) continue;   // the Coaching context's folder: empty until its first file
        bool any = false;
        for (const QString &f : tree.files) any = any || f.startsWith(d.first);
        if (!any) stale << d.first;
    }
    QStringList keys = fileContexts().keys();
    for (const auto &c : hubContracts()) keys << QString::fromLatin1(c.stem);
    for (const QString &k : keys) {
        bool any = false;
        for (const QString &f : tree.files) any = any || stemOf(f) == k;
        if (!any) stale << k;
    }
    for (const QString &k : stale) std::printf("      %s\n", qPrintable(k));
    check(stale.isEmpty(), QStringLiteral("every entry of A9's tables and of the hub's contracts names a directory or file in the tree"));

    static const char *ruleNames[] = { "A1 includes from an inner ring into an outer one, or from an adapter outside Gui into Gui",
                                       "A2 QML creating a session/athlete-scoped type",
                                       "A3 folder-name parsing outside SwingPaths / SessionIdentity",
                                       "A4 session/athlete file names outside their owner",
                                       "A5 file access or a repository include outside the repositories (P11)",
                                       "A6 QVariantMap/QVariantList on the QML boundary",
                                       "A7 functions over 300 lines in rings 1-2",
                                       "A8 Analysis past the Recording contract",
                                       "A9 a context including a later one, the hub past its contracts, or Shared including a context (R-CTX-1)",
                                       "A10 the Rig's header included outside the composition root, app.rig and the Rig's store" };
    int totalEntries = 0, totalAllowed = 0;
    std::printf("\n");
    for (int r = 0; r < 10; ++r) {    // A9 and A10 have no allow-list line: readAllowList() refuses one
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
    std::printf("\n  A9 read %d includes that cross a context (tools/refactor/context_check.py counts the same on the ruled tags).\n",
                g_contextCrossings);
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
