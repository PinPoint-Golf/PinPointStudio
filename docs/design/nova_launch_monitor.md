# Open Launch Nova launch monitor connector

**Audience**: whoever picks this up when a Nova is available, or a user asks for one
**Code**: none yet. Would sit beside `src/LaunchMonitor/gspro_monitor.{h,cpp}` and `gspro_reading.{h,cpp}`
**Source**: <https://github.com/OpenLaunchLabs/nova-developer-guide> (README, two Python example clients, `tools/nova_simulator.py`), read 2026-09-22
**Status**: DEFERRED 2026-09-22. We do not have a Nova. Build only when there is hardware to test against, or when a user asks for it
**Written**: 2026-09-22

---

## Contents

1. [Why this is parked](#1-why-this-is-parked)
2. [What the Nova exposes](#2-what-the-nova-exposes)
3. [Why the existing GSPro connector cannot reach it](#3-why-the-existing-gspro-connector-cannot-reach-it)
4. [What a Nova is worth to PinPoint](#4-what-a-nova-is-worth-to-pinpoint)
5. [The design](#5-the-design)
6. [What the simulator cannot tell us](#6-what-the-simulator-cannot-tell-us)
7. [Before building](#7-before-building)
8. [Definition of done](#8-definition-of-done)
9. [Open decisions](#9-open-decisions)

---

## 1. Why this is parked

The protocol is simple and most of the parsing already exists (§3). What we cannot do is check
it. Without a Nova, the only test partner is the vendor's simulator. It produces random angles
and spin axes, so a sign error in start direction or spin axis would pass every test.

A launch monitor is in PinPoint to be a **reference instrument**: something we check our own
optical estimates against (see `launch_monitor_reading.h`, "WHY EVERY KEY IS `lm.`-PREFIXED").
A connector that has never been seen working is the wrong thing to hold that role. We have the
GC Quad for our own work, so nothing is blocked by waiting.

## 2. What the Nova exposes

The guide is short: a 1.2 KB README, two example clients and a simulator. There is no schema
document, so the simulator is effectively the specification.

**The Nova is the server.** It advertises itself by mDNS and listens. Clients connect and only
receive. It accepts no commands.

| API | Port | mDNS type | Format |
|---|---|---|---|
| OpenAPI | TCP 2921 | `_openapi-nova._tcp.local.` | GSPro Open Connect v1 JSON. Not guaranteed to be newline-terminated |
| WebSocket | 2920 | `_openlaunch-ws._tcp.local.` | Open Launch's own JSON, SI units |

**mDNS record.** The instance is named `NOVA <serial>`, for example `NOVA B0100214`. The TXT
record carries `model`, `manufacturer`, `serial`, `hostname` and `version` (firmware).

**OpenAPI shot**, as the simulator sends it:

```json
{"DeviceID": "OpenLaunch NOVA v1.0", "Units": "Yards", "ShotNumber": 7, "APIversion": "1",
 "BallData": {"Speed": 142.3, "SpinAxis": -3.1, "TotalSpin": 4210, "HLA": 1.8, "VLA": 14.2},
 "ShotDataOptions": {"ContainsBallData": true, "ContainsClubData": false}}
```

`Speed` is in mph even though `Units` says "Yards". This is the same ambiguity that
`gspro_reading.h` records as unknown U3.

**WebSocket shot and status messages**:

```json
{"type": "shot", "timestamp_ns": 1764633600000000000, "shot_number": 7,
 "ball_speed_meters_per_second": 63.6, "vertical_launch_angle_degrees": 14.2,
 "horizontal_launch_angle_degrees": 1.8, "total_spin_rpm": 4210, "spin_axis_degrees": -3.1}

{"type": "status", "uptime_seconds": 312, "firmware_version": "…", "shot_count": 7}
```

A status message arrives every 5 s. `timestamp_ns` comes from the Nova's own clock, which
nothing synchronises with ours.

**The Nova measures the ball only.** It reports no club data on either API.

## 3. Why the existing GSPro connector cannot reach it

`GsProMonitor` is a **server**: it waits for launch monitors, or bridges for them, to connect to
it. The Nova is also a server. Two listeners never meet, so the Nova needs a connector that dials
out.

Most of the parsing is already written, though. libgspro's codec is independent of its server
state machine:

- `gsp_frame_find()` finds one JSON object in a byte buffer (`codec.h`)
- `gsp_message_decode()` turns it into a `gsp_message`
- `readingFromGsProMessage()` (`gspro_reading.h`) already maps a `gsp_message` onto a
  `LaunchMonitorReading`

So a TCP client connector is a socket, a buffer and three calls that already exist and are tested.

**Possible no-code route (unverified).** `docs/reference/foresight_gcquad_data_sources.md` records
GSPconnect listening on ports 12495, 12321 and 12485 for "rebranded" Open API clients, including
Nova/OpenLaunch. That suggests Open Launch's own software may also connect to GSPro as a client,
in which case pointing the existing GSPro listener at 12495 could work with no new code. This is
a single test to run if a Nova ever turns up. Do not plan around it.

## 4. What a Nova is worth to PinPoint

| Nova field | Reading field | Our own estimate to compare against |
|---|---|---|
| ball speed | `lm.ballSpeed` | impact camera ball speed |
| VLA | `lm.launchAngle` | impact camera launch angle (currently reads about 9° low; still open) |
| HLA | `lm.launchDirection` | none yet |
| total spin | `lm.spinRate` | none |
| spin axis | `lm.spinAxis` | none |

It gives nothing for `lm.clubheadSpeed`, `lm.attackAngle` or `lm.clubPath`, which are the GC
Quad's main reason for being here. Do not calculate `lm.smashFactor` from the Nova's ball speed
and our camera's clubhead speed: that would mix a measurement with an estimate under the `lm.`
key, which is exactly what the prefix exists to prevent.

## 5. The design

### 5.1 Which API: OpenAPI over TCP, not the WebSocket

Without hardware, **use the TCP OpenAPI**. Every Nova owner who plays GSPro depends on that
format, and it runs through libgspro's existing decoder and mapping. The WebSocket format
probably has far fewer users and is more likely to change without notice; that is a guess, but
it is the safer one to make blind.

What that choice gives up, and what would win the WebSocket back once there is hardware:

- **A heartbeat.** The 5 s status message is the only way to tell a sleeping Nova from a dead
  link. The TCP port is silent between shots.
- **Stated units.** The WebSocket declares m/s, which settles U3.
- **A per-shot timestamp.** It is on the Nova's clock and not needed for pairing (§5.4), but
  worth recording.

Qt WebSockets is already linked (`CMakeLists.txt`), so switching later costs a second mapping
function and nothing else.

### 5.2 The pieces

1. **`Kind::Nova`**, settings key `"nova"`, label "Open Launch Nova (untested with hardware)",
   short label "Nova". Add it to `makeLaunchMonitor()`, `availableKinds()`, `kindLabel()` and
   `kindShortLabel()`.
2. **`NovaMonitor : LaunchMonitorBase`**: a `QTcpSocket` client.
   - `setSourcePath()` accepts `""` (find by mDNS), `host` (port 2921) or `host:port`.
   - It reconnects with backoff. Waiting means configured but not connected; Ready means a
     reading has arrived; Error means the host cannot be resolved or actively refuses. Report
     the platform's own reason, following the rule in `gspro_monitor.h`.
   - Reads go into a buffer, `gsp_frame_find()` pulls out each object, `gsp_message_decode()`
     decodes it, and `readingFromGsProMessage()` maps it. Set `deviceKind = "nova"`.
   - Keep the serial and firmware from mDNS for the device card and the log.
3. **Discovery** (convenience only; manual host entry is the primary way). Generalise the PPCP
   browser (`src/Ppcp/ppcp_discovery.h`: mDNSResponder on macOS, Avahi on Linux, its own port
   5353 engine on Windows) to take any service type rather than only PPCP's. Store the chosen
   **serial**, not the address, so a studio with two Novas reconnects to the right one after a
   DHCP change. mDNS fails on many networks (client isolation, VLANs, rate-limited access
   points), and PPCP's own discovery is believed not to work at the moment, so the connector
   must never depend on it.
4. **Wire recording from day one.** Use libgspro's recorder (`record.h`: `gsp_recorder_open` /
   `gsp_recorder_write`) behind a setting or an environment variable. The first real session
   then produces a recording that becomes the test fixture and settles §6.
5. **Mark every reading as unverified.** Put a flag in the `swing.json` `launchMonitor` block,
   and show it on the device card, until §8 is met. Nothing should be graded against Nova numbers
   before then.

### 5.3 Consequences worth noting

- **No inbound firewall rule.** PinPoint dials out, unlike the GSPro listener. See
  `windows-firewall-installer-rule` for the problem this avoids.
- **Nothing to reject at startup.** The base-class rule "MUST NOT emit a reading for data that
  already existed when it started" is met for free, because the Nova only pushes new shots.
- **Duplicate detection.** Shot numbers restart when the Nova reboots. Key on serial plus shot
  number, and with the WebSocket, uptime as well.

### 5.4 Pairing

Nothing changes. `ShotPairing` pairs a reading with whichever swing is waiting for one; it needs
no timestamps. The Nova's clock is not ours, so its timestamps must not be used for pairing.

### 5.5 Tests, all without hardware

- `nova_monitor_test`: the connector against an in-process `QTcpServer` on an ephemeral port
  (the same approach as `gspro_monitor_test`). Cover frames split across reads, several frames
  in one read, missing newlines, connection drop and reconnect, and a refused connection.
- The mapping is already covered by `gspro_reading_test`. Add a Nova-shaped message to it,
  including `ContainsClubData: false`.
- A manual full run against `tools/nova_simulator.py` (needs `pip install zeroconf websockets`;
  use `--interval 3`), including mDNS.

## 6. What the simulator cannot tell us

| Unknown | Why it matters | What settles it |
|---|---|---|
| HLA sign (positive = right?) | `lm.launchDirection` would be mirrored | One deliberate push or pull, or Open Launch's answer |
| Spin axis sign | `lm.spinAxis` mirrored; see `pinpoint_sign_conventions.md` | Same |
| `Speed` really mph under `"Units": "Yards"` | Wrong by a factor of 2.24 if not | A real shot, or the WebSocket's stated m/s |
| Message framing on real firmware | Split or joined frames | A wire recording |
| Extra message types or fields | Silently dropped data | A wire recording |
| Sleep/wake and link behaviour | Reconnect logic tuned for nothing | A day's session with the recorder on |

## 7. Before building

1. **Ask Open Launch** whether HLA and spin axis follow GSPro's convention, and whether the
   OpenAPI `Speed` is always mph. One email answers the biggest risk in §6.
2. Re-read the developer guide for changes since 2026-09-22. It has no versioning beyond
   `"APIversion": "1"`.
3. Run a spec audit before writing code (see `spec-audits-catch-real-drift`).

## 8. Definition of done

- The connector, its test and the discovery changes are built and green (§5.5).
- A real Nova has been connected, and a wire recording from it is committed as a fixture.
- On the same real shots, Nova ball speed, launch angle and start direction agree in **sign and
  scale** with a trusted reference (the GC Quad, or our impact camera for ball speed). Then the
  unverified flag and "(untested with hardware)" come off.

## 9. Open decisions

- **Two monitors at once.** PinPoint allows one launch monitor connector at a time (`Kind` is a
  single setting). Running a GC Quad and a Nova together on the same swings would mean
  supporting several connectors, and deciding what `lm.*` means when two devices disagree.
  That is a bigger change than this connector, and not needed for a single-device studio.
- **Switching to the WebSocket** once hardware shows its format is stable (§5.1).
