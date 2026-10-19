# Resource monitor

*Directory:* `src/Gui/monitor/`  
*10 components · 4,250 lines of QML*

The Resource Monitor screen and its device cards, source/stat/profiler/analysis-run rows, PPCP phone and camera rows, timeline chart.

**C++ in this area:** `ProfilerController`, `ResourceMonitorController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [RmAnalysisRunRow](#rmanalysisrunrow) | 333 | `Rectangle` | 1 | 0 |  |
| [RmDeviceCard](#rmdevicecard) | 341 | `Rectangle` | 1 | 0 |  |
| [RmPpcpCameraRow](#rmppcpcamerarow) | 177 | `Rectangle` | 1 | 0 | 1 local comp. |
| [RmPpcpPhoneCard](#rmppcpphonecard) | 465 | `Rectangle` | 1 | 1 | 1 local comp. |
| [RmProfilerRow](#rmprofilerrow) | 118 | `Rectangle` | 1 | 0 |  |
| [RmSourceRow](#rmsourcerow) | 144 | `Rectangle` | 1 | 0 |  |
| [RmStatRow](#rmstatrow) | 85 | `Rectangle` | 1 | 0 |  |
| [RmTimelineChart](#rmtimelinechart) | 125 | `Rectangle` | 1 | 0 |  |
| [RmWarningNotice](#rmwarningnotice) | 72 | `Rectangle` | 1 | 0 |  |
| [ScreenResourceMonitor](#screenresourcemonitor) | 2390 | `Item` | 1 | 8 |  |

### RmAnalysisRunRow
`src/Gui/monitor/RmAnalysisRunRow.qml` · 333 lines · root `Rectangle` · 9 hard-coded colour(s)

> One ANALYSIS RUNS row: a summary line (time · session · ok · frames/total/score)
> that expands on tap to the run's timeline — the critical path's summary, one lane
> per pool thread, and a waterfall of the stages in the order they started. Values
> come pre-formatted from ProfilerController (the critical path is marked when the
> run is recorded, analysis_profiling.cpp) — pure bindings and geometry here.

- **Properties:** `var runData`, `bool isAlternate`, `bool expanded`, `readonly color okColor`, `readonly real spanMs`, `readonly int laneCount`, `readonly real trackX`, `readonly real trackW`, `readonly color pathColor`, `readonly color otherColor`, `readonly var ticks`
- **Functions:** `barX(ms)`, `barW(s)`, `fmtOffset(ms)`
- **Used by (1):** `ScreenResourceMonitor`

### RmDeviceCard
`src/Gui/monitor/RmDeviceCard.qml` · 341 lines · root `Rectangle` · 3 hard-coded colour(s)

Resource-monitor card for one device: header strip with status, then rows of readings (identifier, resolution, crop size, frame rate, events/bytes written, ring size, address, connection/protocol e.g. GSPro Open Connect); button to jump to its settings (openSettingsRequested).  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var deviceData`
- **Signals:** `openSettingsRequested()`
- **Used by (1):** `ScreenResourceMonitor`

### RmPpcpCameraRow
`src/Gui/monitor/RmPpcpCameraRow.qml` · 177 lines · root `Rectangle`

> One PPCP camera (one PPCP `Source`) inside a phone's card on the
> PINPOINTCAPTURE tab of the resource monitor.
> ⛔ KEYED ON THE SOURCE, NEVER ON `serialNumber`.  A PPCP camera's
> `serialNumber` IS the owning peer's id, so both cameras of a two-camera phone
> collapse onto one identity wherever that is used as a key — a known open
> defect in the folded device stats, and CR-02 CB5 forbids repeating it here.
> `camData` is grouped by the wire's `source_id` and carries the full device id
> ("ppcp:<peer_id>/<source_id>") that VideoInputPpcp::deviceIdFor() builds, so a
> phone offering two Sources always shows two rows.

- **Properties:** `var camData`, `bool isAlternate`, `readonly bool haveData`, `readonly string errText`
- **Local components:** `Cell`
- **Used by (1):** `RmPpcpPhoneCard`

### RmPpcpPhoneCard
`src/Gui/monitor/RmPpcpPhoneCard.qml` · 465 lines · root `Rectangle`

> One PinPointCapture phone on the resource monitor's PINPOINTCAPTURE tab: the
> link's readings across the top, then one row per camera it offers.
> `rowData` is the merge ScreenResourceMonitor builds every refresh —
> `{ phone: <a ppcpHost.phones row>, stat: <ppcpStats().perPhone entry|null>,
> cameras: [<one entry per source_id>] }`.  A remembered-but-absent phone
> has a `phone` and no `stat`, which is why every read below is guarded and
> every missing reading renders "—".
> ⚠ -1 AND "" ARE READINGS, NOT STUBS.  `batteryPct: -1` and `thermal: ""` mean
> "no `heartbeat_ack` has arrived yet", which is a different answer from 0% and
> from "nominal".  Rendering the sentinel itself would put a -1 in front of an
> operator, so every formatter below maps it to an em dash.

- **Properties:** `var rowData`, `readonly var phone`, `readonly var stat`, `readonly var cameras`, `readonly var buffers`, `readonly bool live`
- **Functions:** `fmtBattery()`, `fmtThermal()`, `fmtSigma()`, `fmtStorage()`, `fmtArm()`, `fmtMs(ms)`, `fmtSessionFor()`, `fmtTransport()`, `thermalTint()`, `batteryTint()`
- **Local components:** `Reading`
- **Uses:** `RmPpcpCameraRow`
- **Used by (1):** `ScreenResourceMonitor`

### RmProfilerRow
`src/Gui/monitor/RmProfilerRow.qml` · 118 lines · root `Rectangle`

> One scope row: NAME / CALLS / TOTAL / AVG / MAX / CPU. All values are
> pre-formatted in ProfilerController — pure bindings here. Deep-only scopes
> (those carrying per-scope CPU) grey out while the deep toggle is off.

- **Properties:** `var scopeData`, `bool isAlternate`, `bool deepOn`, `readonly color primaryColor`
- **Used by (1):** `ScreenResourceMonitor`

### RmSourceRow
`src/Gui/monitor/RmSourceRow.qml` · 144 lines · root `Rectangle`

Resource-monitor table row for one data source, with alternate-row shading.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var sourceData`, `bool isAlternate`
- **Used by (1):** `ScreenResourceMonitor`

### RmStatRow
`src/Gui/monitor/RmStatRow.qml` · 85 lines · root `Rectangle` · 2 hard-coded colour(s)

> One STATS HISTORY row: TIME / CATEGORY badge / MESSAGE. Values come
> pre-formatted from ProfilerController — pure bindings here.

- **Properties:** `var statData`, `bool isAlternate`, `readonly color catColor`
- **Used by (1):** `ScreenResourceMonitor`

### RmTimelineChart
`src/Gui/monitor/RmTimelineChart.qml` · 125 lines · root `Rectangle`

Resource-monitor bar-timeline: a rolling history of values (and buffer state) drawn as barCount bars scaled to maxVal.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var historyData`, `string bufferState`
- **Used by (1):** `ScreenResourceMonitor`

### RmWarningNotice
`src/Gui/monitor/RmWarningNotice.qml` · 72 lines · root `Rectangle` · 1 hard-coded colour(s)

Inline warning notice: warn-tinted rounded strip with a 'Warning' label and message. Used on the home screen to surface resource problems.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string message`
- **Used by (1):** `ScreenHome`

### ScreenResourceMonitor
`src/Gui/monitor/ScreenResourceMonitor.qml` · 2390 lines · root `Item` · 22 hard-coded colour(s)

The Resource Monitor screen (2.4k lines): Devices (scan), Event buffer, Timeline, Sources and registered-sources tables, PPCP phone/camera stats, and a tabbed bottom area (Message Log, Stats History, Analysis Runs, PinPointCapture).  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `int logTab`, `readonly bool ppcpAvailable`, `var ppcpStatsData`, `var ppcpRows`
- **Signals:** `navigateToSettings(int panelIndex)`
- **Functions:** `ppcpCamerasFor(peerId, stat)`, `refreshPpcp()`
- **Uses:** `PpToast`, `RmAnalysisRunRow`, `RmDeviceCard`, `RmPpcpPhoneCard`, `RmProfilerRow`, `RmSourceRow`, `RmStatRow`, `RmTimelineChart`
- **Used by (1):** `Main`
