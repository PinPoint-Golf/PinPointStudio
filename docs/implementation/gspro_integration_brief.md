# GSPro Open Connect integration — implementation brief

**Status:** the dependency, the connector, the settings, the controller wiring and the device
list have landed — `120d4dd` (dependency and link check), `79bc926` (connector) and this commit
(settings, enumeration, club/session out). **Selectable in Settings → Devices → Launch monitor
and testable with synthetic shots today**; §4 is what remains.
**Scope:** add a second launch-monitor `Kind` that receives shots from *any* device speaking the
GSPro Open Connect v1 protocol, over TCP, and turns them into `LaunchMonitorReading`s the shot
pairing and the metric catalogue already understand.

**Read first**
- [`testing_developer_guide.md`](../developer/testing_developer_guide.md) — the suites are not
  part of the app build; `pp_add_test`; §9's conventions.
- The library's own `docs/protocol.md`, `docs/design.md` and `docs/conformance.md` in
  `../libgspro`. Bare **§x** below is the *protocol* document; **L §x** is the library design.
  **L §6** is written against *this* repository's classes and is the specification this brief
  implements.
- [`foresight_gcquad_data_sources.md`](../reference/foresight_gcquad_data_sources.md) — the other
  connector, for the shape a `LaunchMonitorReading` is expected to arrive in.

---

## 1. Why one connector reaches most devices

⚠ **Almost no launch monitor speaks this protocol itself.** Somebody wrote a **bridge** that
speaks it on the device's behalf, and libgspro's survey (`../libgspro/docs/protocol.md` §0) found
seventeen: five independent bridges for the **Garmin R10**, two for the **Rapsodo MLM2PRO**, one
for a **SkyTrak+** via OpenSkyPlus, one off a **Foresight GC2**'s serial feed, a proxy for
**Swinglogic SLX**, and two DIY monitors that are native clients because they were built that way
— **PiTrac** and **OpenFlight**. Every one of them is a **client** of GSPro; PinPoint plays the
**server** they connect to, and the bridge cannot tell it is not GSPro. So this is one connector
with no per-device work, against a protocol documented from those clients' source.

⚠ **It does not reach Uneekor, Bushnell or Foresight, and those are the ones a reader assumes.**
None of them ships an Open Connect client: their connectors are closed and speak their vendors'
own simulators, which is why the library's conformance matrix records them as unread rather than
unsupported. A Uneekor is reachable only through a third-party bridge that watches Uneekor VIEW's
shot folder, or one that OCRs its screen.

**So the GCQuad connector is not superseded by this one.** A Foresight GCQuad is still reached by
watching a CSV file FSX2020 rewrites in place — one device, one vendor application, one operating
system and a share — and a Uneekor, if it is ever wanted, would be reached the same way. What
this connector removes is the per-device work for everything that *does* have a bridge.

## 2. What landed

| Piece | Where | What it is |
|---|---|---|
| The dependency | `CMakeLists.txt`, `tests/cmake/PinPointTests.cmake` | libgspro embedded exactly as libwrist is — sibling `../libgspro` wins, else `main` from GitHub — in the app *and* in the test infrastructure, so a standalone suite configure resolves it too |
| `Kind::GsPro` | `launch_monitor_base.h/.cpp`, `launch_monitor_factory.cpp` | The settings token is `"gspro"`; the combo label names the **protocol**, not a device |
| The mapping | `gspro_reading.h/.cpp` | `gsp_message` → `LaunchMonitorReading`, pure, no socket — the same split as `gcquad_csv_parser` |
| The listener | `gspro_monitor.h/.cpp` | `GsProMonitor : LaunchMonitorBase` — a `QTcpServer`, the library handle, the connection table and one timer |
| Settings | `app_settings.h`, `LaunchMonitorPanel.qml` | A port and an interface, kept apart from the GCQuad's folder key; the device list; the per-platform bind hints |
| Enumeration | `gspro_monitor.h`, `launch_monitor_controller.h` | `clients()` → `devices` — who is on the link, named once they speak, with their shot counts |
| Club and session out | `GsProMonitor::setPlayerClub/setSessionActive` | Reachable from the controller; not yet called by the app (§5) |
| Tests | `src/LaunchMonitor/tests/` | The link/ABI check, the mapping against real clients' byte patterns, the listener over a loopback socket, the enumeration, and the settings→address decision (6 targets in the suite) |

**The split is the point.** libgspro owns no socket, thread, timer, clock or file: it takes bytes
and a `now` and hands back replies to write and events to handle. Everything below the JSON is
the library's and is tested there against sixteen clients' byte patterns; everything about
sockets is `GsProMonitor`'s and is tested here against a real one. Neither of the library's
optional modules is built — its reference `select()` transport and its `.gswire` recorder — because
PinPoint has an event loop and storage of its own.

## 3. Decisions, and why

**A chunk of the protocol travels the other way, and that is not a detail.** The shot message has
**no club field**. GSPro tells the *device* which club is in play so it can switch to putting
mode, and an [OSP]-style client does not arm at all until it has seen a 202 "GSPro ready". So
`deviceClub` is left empty on every reading — inventing one would be a guess sitting beside
measurements — and `setPlayerClub()` / `setSessionActive()` are how PinPoint's own club selection
and session state reach the device. §5.2, L §6.

**Nothing is announced until PinPoint says something.** Until the controller calls
`setPlayerClub()`, the connector knows neither the club nor the golfer's handedness, so it sends
no player information at all rather than announcing a right-hander with no club to every device
that connects. Same rule as `LaunchMonitorReading`'s: never invent a value you did not receive.

**Presence is a bitmask and zero is not absence.** The library reports which fields were *on the
wire* separately from their values, because some clients omit a quantity they did not measure and
others send `0.0`. Every line of the mapping gates on the bit. A putt at 0.0° of attack and a
square face are measurements, and a mapping that tested `!= 0.0` would report the most
interesting numbers in a putting session as "not reported" — `gspro_reading_test` fails on
exactly that sabotage.

**⚠ Port 921 is privileged on macOS and Linux.** It is below 1024, so those kernels reserve it
for root; the vendor chose it on Windows, where no such rule exists. Two of the three platforms
PinPoint ships on therefore cannot bind the protocol's own default port, and the answer is **not**
elevated privileges — this listener is unauthenticated by design — but a port above 1024 on both
sides, which every client can be told. Found by this connector on macOS and now recorded in the
library (`../libgspro` `9e1939a`). `GsProMonitor` tells "permission denied" and "address already
in use" apart, because they have opposite fixes.

**Units are half-converted, deliberately.** The message declares `Yards` or `Meters` and what that
governs is not stated by the vendor (§3.6, unknown U3). Every client that can be read sends **mph**
for speeds whatever it declares, and no client is known to send `Meters` at all. So the mapping
converts distances under `Meters` and leaves speeds exactly as they arrived: converting a speed on
a guess would corrupt the one number this connector exists to check our camera estimate against.

**A heartbeat is answered but is not a shot.** Every client sends them, some every second, and
GSPro answers all of them with the same 200 as a real shot (§4.2) — so the acknowledgement is the
library's business and the *distinction* is ours. A connector that conflated them would attribute
an empty reading to a swing every few seconds.

## 4. Testing it with synthetic shots

⚠ **No launch monitor is needed and none has ever been used.** libgspro ships a launch-monitor
simulator that sends the conformance fixtures — real clients' byte patterns — over a real socket,
and it drives this connector exactly as a device would.

```sh
# 1. In PinPoint: Settings → Devices → Launch monitor
#      Device    = GSPro Connect (R10, MLM2PRO, SkyTrak+, …)
#      Port      = 921   (or 922, or anything above 1024)
#      Interface = All interfaces
#      Enabled   = on
#    The panel's status row should read "Listening — no launch monitor yet".

# 2. Play a launch monitor at it. In THIS repository, needing nothing built:
python3 tools/launchmonitor/fake_gspro.py                     # a shot every 15 s, forever
python3 tools/launchmonitor/fake_gspro.py --shots 5           # five, then disconnect
python3 tools/launchmonitor/fake_gspro.py --club driver --interval 5
python3 tools/launchmonitor/fake_gspro.py --wait 0            # skip the silent spell

# 3. Or, from a libgspro checkout, replay REAL CLIENTS' byte patterns — which is the
#    thing fake_gspro.py cannot do, because its shots are ours rather than a device's:
cd ../libgspro
python3 tools/gsp_shoot.py --port 921 --fixture osp_shot.json          # a SkyTrak+ shot
python3 tools/gsp_shoot.py --port 921 --style mlm --repeat 3           # as an MLM2PRO writes them
python3 tools/gsp_shoot.py --port 921 --bytewise --gap 0.01            # one byte at a time
python3 tools/gsp_shoot.py --port 921 --heartbeat 5                    # connect and idle
```

⚠ **The two are for different questions.** `fake_gspro.py` is the sibling of
`fake_shot.py` and lives here: no dependencies at all, plausible shots, and the tool to
reach for when the connector is not receiving and you need a half of the pair you can
trust without building anything. `gsp_shoot.py` replays the **conformance fixtures** —
sixteen real clients' byte patterns, including the ones that misspell a key or omit a
block — and that is what proves the decoder rather than the plumbing.

Each run connects, sends, reads the reply back through the library's own decoder and disconnects,
so it exercises the whole path including the acknowledgement the device waits for. The panel's
**Connected launch monitors** list shows the client while it is connected — unnamed at first,
then by its `DeviceID` once it has spoken, with its shot count — and each shot lands as a reading
the same way a GCQuad's would.

⚠ **`gsp_shoot.py` refuses port 921 on a NON-loopback address** unless told `--yes-i-mean-it`,
because that combination means somebody else's real GSPro. Aiming it at `127.0.0.1` is always
fine; aiming it across the network at a PinPoint bound to `0.0.0.0` needs the flag.

Verified against the running app: connect → silent (amber, *connecting…* in the home
screen's device list) → named on the first message → shots acknowledged 200 and counted →
disconnect drops the row, with the listener surviving all of it. And against a harness
built from these exact sources: `carryDistance` absent where a fixture omits it, a measured
`0.0` where the vendor's own example has zeros, and `smashFactor` derived only where both
speeds are real.

### What the panel offers

| Setting | Key | Default | Note |
|---|---|---|---|
| Port | `launchmonitor/gsproPort` | 921 | The device's app must be set to the same one |
| Interface | `launchmonitor/gsproInterface` | `any` | `any` \| `loopback`. ⚠ `any` is also what makes 921 bind on macOS |

Kept apart from `launchmonitor/path`, which holds the GCQuad's folder: sharing one key would mean
choosing one connector destroyed the other's configuration.

⚠ **Port 921 is below 1024.** Windows does not care; macOS binds it on all interfaces and refuses
it on any single address; Linux wants `sudo sysctl net.ipv4.ip_unprivileged_port_start=921`, which
is preferred over granting the binary `CAP_NET_BIND_SERVICE` and far preferred over running as
root. Any port above 1024 works everywhere with no administrator at all. The connector reports
each failure with the fix for *that* failure.

## 5. What remains

1. **Club and session out of the app.** `LaunchMonitorController::setPlayerClub()` and
   `setSessionActive()` exist, are `Q_INVOKABLE`, and reach the device — but nothing calls them
   yet. They want binding to PinPoint's own club selection and to session start/stop, which is
   where a device switches to putting mode and where an [OSP]-style client arms.
2. **Windows firewall** — an inbound rule for the executable, or every device on another machine
   fails with "connection refused" and nothing distinguishes that from a device nobody switched
   on. The installer adds it; the panel should say whether it is present.
3. **A `virtual` for enumeration, if a third connector ever needs it.** `devices` reaches
   `GsProMonitor` through a `qobject_cast` in the controller, deliberately: a folder does not
   connect, and putting `connectedDevices()` on `LaunchMonitorBase` would give every future
   connector an empty override to write. If a second enumerating connector appears, that is the
   moment to promote it — and it is a base-class change, so it is a conversation.
4. **First contact.** ⚠ Nothing in either repository has met a launch monitor. The library's
   suite is 115 socket-free cases plus a host-transport family run against two adapters, and
   every fixture in it is a transcription of a client's *source*, not a capture off a wire. Three
   questions can only be answered by a device on the mat: whether `Units: "Meters"` changes
   anything (U3), the sign convention of `SpinAxis` (U6), and whether an MLM2PRO really puts
   face-to-path in `HorizontalFaceImpact` (§3.3) — which would need a per-device exception in the
   mapping. libgspro's `.gswire` recorder exists to take exactly that capture and replay it.
