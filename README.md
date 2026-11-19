# esp-signalk-sender

Shared helpers for ESP32 sensor firmware that pushes data to a **Signal K**
server over UDP as NMEA 0183.

Extracted from [Wind-Sensor](https://github.com/rudgej/Wind-Sensor) and
[water-level-sensor](https://github.com/rudgej/water-level-sensor), which had
drifted into near-identical copies of the same routines.

## Why

Both firmwares checked that their data was reaching Signal K by calling
`HTTPClient::GET()` inline in `loop()`, every 5 seconds, with a 3 second
timeout. `HTTPClient` blocks, so each check froze the loop — and the transmit
with it. Wind went silent for up to 3 seconds roughly every 5 seconds, which is
visible as a sensor activity light that blinks, pauses, then resumes, and shows
up in Signal K as gaps of several seconds.

**The diagnostic was interrupting the data it was diagnosing.**

`Probe` moves that request onto its own FreeRTOS task, so the loop never blocks.

## What's in it

| Item | Purpose |
|---|---|
| `sks::nmea(body)` | Assemble an NMEA 0183 sentence with `$` and checksum |
| `sks::extractNumber(body, key, out)` | Pull the first number after `"key"` from a JSON body |
| `sks::Probe` | Periodic non-blocking GET of a Signal K REST path |

## Use

```cpp
#include <SignalKProbe.h>

static sks::Probe probe;

// once connected:
probe.begin("http://" + skIp.toString() +
            ":3000/signalk/v1/api/vessels/self/environment/wind", 5000, 1500);

// every loop — never blocks:
sks::ProbeResult r;
if (probe.take(r)) {
  if (r.ok) { float v; sks::extractNumber(r.body, "speedApparent", v); }
}
```

## Build

Local development uses a symlink so changes are picked up without publishing:

```ini
lib_deps = symlink://../../esp-signalk-sender
```

Once published, switch to the git URL so any machine can build:

```ini
lib_deps = https://github.com/rudgej/esp-signalk-sender.git
```
