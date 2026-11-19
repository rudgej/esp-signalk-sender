#pragma once

/*
 * Shared vocabulary for the network supervisor.
 *
 * Deliberately free of Arduino headers so the state machine can be exercised
 * on a host or in a mock build without a radio.
 */

#include <stdint.h>

namespace sks {

// A sensor only ever joins a network. The CYD additionally has to *become*
// one when the primary disappears.
enum class NetRole : uint8_t { ClientOnly, Hub };

// Boot is a distinct state so the first join gets the same grace treatment as
// any later loss - a cold boot on the mooring must not race into fallback.
enum class NetState : uint8_t { Boot, PrimaryUp, PrimaryLost, Fallback, Recovering };

// Why we are not on the primary. Two very different faults that look alike
// from a "is it working?" check:
//   LinkLost     - the access point is gone. An AP of our own can replace it.
//   ServerSilent - the network is fine, Signal K is not. Our own AP would help
//                  nobody: every client is already on a working network, it is
//                  the destination that is dead.
enum class NetReason : uint8_t { None, LinkLost, ServerSilent };

enum class ScanResult : int8_t { Idle, Running, FoundPrimary, NotFound };

struct NetConfig {
  // ---------------------------------------------------------------------
  // Networks. fallbackSsid is both the network a client falls back to and
  // the SSID the hub advertises, so there is only one name to teach devices.
  // ---------------------------------------------------------------------
  const char *primarySsid = nullptr;
  const char *primaryPass = nullptr;
  const char *fallbackSsid = nullptr;
  const char *fallbackPass = nullptr;

  NetRole role = NetRole::ClientOnly;

  // ---------------------------------------------------------------------
  // Hub access point. Channel must match the primary network: one radio
  // serves both roles, so any mismatch moves the AP (and drops its clients)
  // the moment the STA associates.
  // ---------------------------------------------------------------------
  uint8_t apChannel = 6;
  uint8_t apMaxClients = 4;
  uint32_t apIp = 0;    // host byte order, e.g. (192u<<24)|(168<<16)|(50<<8)|1
  uint32_t apMask = 0;  // host byte order

  // ---------------------------------------------------------------------
  // Timing. Slow to leave the primary, quick-ish to return.
  // ---------------------------------------------------------------------
  uint32_t graceMs = 30000;            // sustained failure before fallback
  uint32_t retryMs = 5000;             // first retry interval
  uint32_t retryMaxMs = 30000;         // retry backoff ceiling
  // Scanning costs the access point airtime, and a hub scanning every 20 s
  // while its own clients are associated drops them repeatedly. Long is
  // better than frequent here: the plotter is off overnight, not for 20 s.
  uint32_t scanMs = 60000;             // full-band scan, also the backstop
  // Once the primary's channel is known a scan costs about a tenth as much,
  // so it becomes affordable to look often and recover in seconds rather
  // than in a minute.
  uint32_t scanFastMs = 5000;          // single-channel look
  uint8_t primaryChannel = 0;          // hint; 0 = learn it from the link
  // Leaving the hub's network for the primary is a disruptive act, so it is
  // rate limited. Blind probe: even if scanning is broken, a client must not
  // live on the fallback network for ever.
  uint32_t switchMs = 30000;
  uint32_t primaryProbeMs = 60000;
  uint32_t holdAfterRecoveryMs = 180000;  // AP overlap window, see below
  uint32_t flapWindowMs = 1800000;     // 3 transitions in 30 min == a fault
  uint8_t flapLimit = 3;
};

}  // namespace sks
