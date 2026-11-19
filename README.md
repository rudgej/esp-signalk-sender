# esp-signalk-sender

Shared helpers for ESP32 sensor firmware that pushes readings to a **Signal K**
server over UDP as NMEA 0183.

Used by [Wind-Sensor](https://github.com/rudgej/Wind-Sensor) and
[water-level-sensor](https://github.com/rudgej/water-level-sensor), which had
drifted into near-identical copies of the same four routines.

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

## What is deliberately *not* in it

There is no Wi-Fi management, no UDP sender, no configuration handling. Those
differ enough between projects (AP fallback rules, NVS keys, PGN versus sentence
formats) that sharing them would cost more than it saves. This library covers
only what was genuinely duplicated **and** was causing a bug.

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

Bump the tag deliberately in each consumer when you want a change to flow, and
note what changed here.
