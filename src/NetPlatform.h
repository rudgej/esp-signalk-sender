#pragma once

/*
 * The bits of the radio the supervisor is allowed to touch.
 *
 * This seam exists so the failover logic carries no WiFi.h, no Arduino and no
 * display: a mock implementation can drive the whole state machine on a bench,
 * and the CYD and a sensor can share one copy of the rules while differing in
 * how (or whether) they run an access point.
 */

#include <stddef.h>
#include <stdint.h>

#include "NetTypes.h"

namespace sks {

class NetPlatform {
 public:
  virtual ~NetPlatform() {}

  // Station side.
  virtual void staBegin(const char *ssid, const char *pass) = 0;
  virtual void staStop() = 0;
  virtual bool staUp() = 0;        // associated AND holding an IP
  virtual uint32_t staIp() = 0;    // host byte order, 0 when down
  virtual const char *staSsid() = 0;

  // Access point side. No-ops in a client-only implementation.
  virtual void apStart(const char *ssid, const char *pass, uint8_t channel,
                       uint8_t maxClients) = 0;
  virtual void apStop() = 0;
  virtual uint8_t apClients() = 0;

  // Scanning. Must be asynchronous - a blocking scan in AP mode interrupts
  // the access point, which is exactly what a fallback hub must not do.
  virtual void scanStart(const char *targetSsid) = 0;
  virtual ScanResult scanPoll() = 0;
};

}  // namespace sks
