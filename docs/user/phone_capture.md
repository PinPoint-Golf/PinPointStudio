# Phone capture (PPCP)

A phone running **PinPoint Capture** joins a session as a camera. It pairs by scanning a code from the home screen (*Pair a device*), is remembered afterwards, and its cameras then appear in the devices list beside the USB and industrial ones. Pairing, the name this computer shows, per-phone health (battery / thermal) and *Forget* live in **Settings → Phones**.

The link is the **PinPoint Capture Protocol (PPCP)**: TLS 1.3 with an external pre-shared key, carried either over WiFi (the phone dials the host) or over a **USB cable** (the host dials the phone through Apple's usbmux tunnel). There is no unencrypted mode — a build made without the PPCP dependencies simply has no phone support, and says so in Settings → Phones.

## What each platform needs

Whether the feature is *compiled in* is decided by build-time dependencies — see [BUILDING.md](../../BUILDING.md). The table below is what must be present on the **user's** machine at run time for an installed build.

| Capability | macOS | Windows | Linux |
|---|---|---|---|
| **Pairing + WiFi link** (the baseline) | Nothing to install | Nothing to install | Nothing to install beyond the system `libssl` |
| **Reconnect discovery** (optional — a remembered phone finds this computer again without a new code) | Built into the OS | `dnssd.dll` from Apple's **Bonjour** — installed by Bonjour Print Services, iTunes, or the *Apple Devices* app | `libavahi-compat-libdnssd1` with `avahi-daemon` running |
| **Wired (USB) capture** (optional) | Built in — Apple's own `usbmuxd` at `/var/run/usbmuxd` | The **Apple Devices** app (Microsoft Store) or iTunes, which provides `AppleMobileDeviceService` on `127.0.0.1:27015` | The `usbmuxd` daemon (`apt install usbmuxd`) and its udev rules; the socket may need a group membership |
| **Firewall** | Allow incoming connections when macOS first prompts | Allow PinPoint Studio on **Private** networks — the first-run Windows Security alert gates pairing | Allow the listener port if a firewall is active |

Every one of the optional rows is *absent, not broken*, when the dependency is missing: no reconnection discovery still leaves pairing by code working, and no usbmux provider just means the cable is never offered. Nothing raises a banner; the reason is written to the application log.

## Troubleshooting — a wired link that keeps dropping (macOS)

**Symptom.** A cabled phone session runs for a few minutes and then every PPCP channel closes at once — cleanly from the phone's side, with broken pipes on the Mac's. It reconnects, then drops again, sooner when the link is busy. The cable, the phone and the app all look innocent.

**Cause — an OS setting, not the app and not `usbmuxd`.** macOS creates an **"iPhone USB"** network service for USB tethering whenever an iPhone is plugged in. If the phone is not actually sharing a hotspot, that service can sit stuck on a self-assigned `169.254.x.x` address, endlessly renegotiating a link that isn't there. Each renegotiation makes macOS perform a full USB `SetConfiguration` on the phone, which tears down **every** interface on the device — the usbmux tunnels along with it — and rebuilds them ~150 ms later. No electrical disconnect ever occurs, which is exactly why the hardware looks fine.

**Fix.** Turn the tethering service off. It is reversible, and it affects neither charging, nor syncing, nor PPCP:

```bash
networksetup -setnetworkserviceenabled "iPhone USB" off
# and to put it back:
networksetup -setnetworkserviceenabled "iPhone USB" on
```

**Confirming it**, if drops ever return:

```bash
/usr/bin/log stream --predicate 'eventMessage CONTAINS "setConfigurationGated" OR eventMessage CONTAINS "updateLinkStatus"' --info
```

Each drop lines up to the millisecond with an `AppleUSBNCMData::updateLinkStatus: linkStatus 0` followed by `IOUSBHostDevice::setConfigurationGated: … selected configuration 6`. A second tell is the usbmux device index climbing fast — ids incrementing through the twenties in a morning means the device is being re-enumerated, not that anything reconnected.

> ⚠ **Use the full path `/usr/bin/log`.** In `zsh`, `log` is a shell builtin that lists login records, so a bare `log show` / `log stream` returns nothing at all, with no error — which reads exactly like "the logs are empty" and is how this fault stayed hidden for a session.
