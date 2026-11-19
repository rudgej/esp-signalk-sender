# esp-signalk-sender

Shared helpers for ESP32 boat sensors that push readings to a **Signal K**
server over UDP as NMEA 0183, and for the display that takes over when that
server is not running.

Used by [Wind-Sensor](https://github.com/rudgej/Wind-Sensor) and
[water-level-sensor](https://github.com/rudgej/water-level-sensor), which had
drifted into near-identical copies of the same four routines, and by
[CYD-Display](https://github.com/rudgej/CYD-Display), which hosts the fallback
network.

## The problem it solves

Both firmwares checked that their data was reaching Signal K by calling
`HTTPClient::GET()` **inline in `loop()`**, every 5 seconds, with a 3 second
timeout. `HTTPClient` blocks, so each call froze the loop — and the transmit
with it.

The result was visible from the outside: the sensor's activity light blinked,
paused, then resumed; Signal K recorded gaps of several seconds; the wind
sender produced 42 packets in 45 seconds when it should have produced 90.

**The diagnostic was interrupting the data it was diagnosing.**

`Probe` moves that request onto its own FreeRTOS task, pinned to core 0, so the
Arduino loop only ever collects a finished result. Measured after the change,
the same sender produced 90 packets in 45 s with its worst gap cut from 4.42 s
to about 1.2 s.

## What's in it

| Item | Purpose |
|---|---|
| `sks::nmea(body)` | Assemble an NMEA 0183 sentence, adding `$` and the checksum |
| `sks::extractNumber(body, key, out)` | Pull the first number following `"key"` from a JSON body |
| `sks::Probe` | Periodic, non-blocking GET of a Signal K REST path |
| `sks::UdpSender` | One UDP socket with a destination that can change at runtime |
| `sks::NetSupervisor` | Non-blocking primary/fallback network handover. No radio code |
| `sks::EspWifiPlatform` | The Arduino/ESP32 side of `NetSupervisor` |

## Network failover

When the plotter is switched off, the Wi-Fi network and Signal K go with it, even
though the sensors are still running. The CYD therefore raises a fallback access
point, and the sensors have to move to it and back again without being touched.

Two things make that harder than it looks, and both are handled here rather than
in each firmware:

- **One radio, one channel.** An access point and a station share the radio, so a
  hub's AP must sit on the primary network's channel or every recovery drags the
  AP to a new channel and drops its clients.
- **Losing the network and losing the server are different faults.** If the link
  is gone, an AP of our own replaces it. If the link is fine and Signal K is dead,
  raising an AP helps nobody - every client already has a working network; it is
  the destination that is missing.

The rules are deliberately asymmetric: **slow to leave the primary, quick to
return.** A brief interruption never starts a network. Sustained failure does.
On the way back, the primary is verified by data - not by seeing the SSID - and
the AP is only torn down once the primary has held stable, because in the real
world the plotter is off overnight and back in the morning. Anything faster than
that is a network fault, so repeated fallbacks switch the supervisor into a
degraded mode that doubles the grace period and keeps the safety net up rather
than flapping with the fault.

The whole state machine is driven with `poll(millis())`, never blocks, and has no
Arduino dependency, so it can be tested against a mock platform. See
`NetPlatform` for the seam and `CYD-Display`'s `net_selftest` environment for the
scenarios it is checked against.

## What is deliberately *not* in it

No NVS or configuration handling, no setup portal, and no knowledge of what the
sentences mean. The sensor projects keep those because they differ: NVS keys,
calibration, PGN versus sentence payloads. This library covers the things that
were duplicated **and** causing a bug, or that two devices must agree on to
interoperate.

## Use

```cpp
#include <SignalKProbe.h>

static sks::Probe probe;

// once the server address is known:
probe.begin("http://" + skIp.toString() +
            ":3000/signalk/v1/api/vessels/self/environment/wind",
            5000,    // period  - how often to check
            1500);   // timeout - per request, on the task, never the loop

// in loop() - never blocks, returns true once per completed request:
sks::ProbeResult r;
if (probe.take(r)) {
  if (r.ok) {
    float v;
    sks::extractNumber(r.body, "speedApparent", v);
  }
}
```

`ProbeResult` carries `ok`, the HTTP `code`, the `body`, and `atMs`.

Call `probe.setUrl()` when the server address changes — for example after
re-resolving a hostname that may have moved.

## Consuming it from a project

Pin a tag, so a change here cannot silently alter a build:

```ini
lib_deps = https://github.com/rudgej/esp-signalk-sender.git#v0.1.0
```

For local development against a working copy, point at the directory instead:

```ini
lib_deps = symlink://../esp-signalk-sender
```

PlatformIO records a `.pio-link` file and compiles straight from that directory,
so there is one copy of the source and edits take effect on the next build.
Note that this is build-time only: already-flashed devices are unaffected until
they are rebuilt and reflashed.

## Versioning

- `v0.1.0` — `nmea()`, `extractNumber()`, non-blocking `Probe`
- `v0.2.0` — `UdpSender`, `NetSupervisor` + `NetPlatform` seam, `EspWifiPlatform`
- `v0.2.1` — the AP address is part of `apStart`; being associated is not recovery

Bump the tag deliberately in each consumer when you want a change to flow, and
note what changed here.
