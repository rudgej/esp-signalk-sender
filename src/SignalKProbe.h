#pragma once

/*
 * esp-signalk-sender — shared helpers for ESP32 sensors that push data to a
 * Signal K server over UDP as NMEA 0183.
 *
 * Extracted from winasend (Wind-Sensor) and water-level-sensor, which had
 * drifted into near-identical copies of the same four routines.
 *
 * The important piece is Probe. Both firmwares used to call HTTPClient::GET()
 * inline every few seconds to check that their data was reaching Signal K.
 * HTTPClient is blocking, so each check froze the main loop - and with it the
 * 2 Hz transmit - for up to the 3 s timeout. The sender went silent, Signal K
 * showed gaps up to 10 s, and the sensor's activity light blinked, paused, and
 * resumed. The diagnostic was interrupting the data it was diagnosing.
 *
 * Probe moves that request onto its own task, so the loop never blocks.
 */

#include <Arduino.h>

namespace sks {

// Build an NMEA 0183 sentence from a bare body, adding '$' and the checksum.
//   nmea("WIMWV,000,R,0.0,N,A") -> "$WIMWV,000,R,0.0,N,A*3D\r\n"
String nmea(const String &body);

// Pull the first numeric value that follows "key" (JSON-lite; no parser).
//   extractNumber(body, "speedApparent", out)
bool extractNumber(const String &body, const char *key, float &out);

struct ProbeResult {
  bool ok = false;          // HTTP 200 and a body was read
  int code = 0;             // HTTP status, 0 if the request never started
  String body;              // response body, empty unless ok
  unsigned long atMs = 0;   // millis() when the request completed
};

// Periodic, non-blocking GET of a Signal K REST path.
//
//   Probe probe;
//   probe.begin("http://192.168.10.1:3000/signalk/v1/api/vessels/self/environment/wind",
//               5000);
//   // in loop():
//   ProbeResult r;
//   if (probe.take(r)) { ... }
class Probe {
 public:
  void begin(const String &url, unsigned long periodMs,
             unsigned long timeoutMs = 1500);
  void setUrl(const String &url);     // e.g. after the server address changes
  void setPeriod(unsigned long periodMs);
  bool take(ProbeResult &out);        // true once per completed request
  bool running() const;
  void stop();

 private:
  bool _started = false;
};

}  // namespace sks
