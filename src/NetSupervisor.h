#pragma once

/*
 * NetSupervisor - non-blocking primary/fallback network handover.
 *
 * Design rule, learned from the brief: be slow to abandon the primary network
 * and quick to return to it. The primary is the operating environment; our own
 * AP is a safety net, never a second permanent network.
 *
 * Two consequences that shape the state machine:
 *
 *  1. An AP is only raised when the *link* is gone. Signal K dying while the
 *     Wi-Fi is healthy is a ServerSilent fault - raising an AP then helps
 *     nobody, because every client already has a working network.
 *
 *  2. The AP is only torn down after the primary has held stable for a while,
 *     not on a migration timer. In the real world the plotter is off overnight
 *     and back in the morning; anything faster than that is a network fault,
 *     so flapping is treated as a fault to be ridden out, not as a signal to
 *     switch again.
 *
 * poll() never blocks and never waits. Call it from loop().
 */

#include <stdint.h>

#include "NetPlatform.h"
#include "NetTypes.h"

namespace sks {

class NetSupervisor {
 public:
  // Supplies the app's definition of "the data path is genuinely working"
  // (a Signal K WebSocket, a successful round-trip probe...). When null, a
  // station holding an IP counts as verified.
  using VerifyFn = bool (*)(void *ctx);

  void begin(NetConfig config, NetPlatform *platform);
  void setVerify(VerifyFn fn, void *ctx);

  // The whole state machine. `nowMs` is millis(); passing it in keeps the
  // logic testable without a clock.
  void poll(uint32_t nowMs);

  NetState state() const { return state_; }
  NetReason reason() const { return reason_; }
  const char *stateName() const;
  const char *reasonName() const;

  // Can data reach the primary-side server right now?
  bool onPrimary() const {
    return state_ == NetState::PrimaryUp || state_ == NetState::Recovering;
  }
  bool apUp() const { return apUp_; }
  uint8_t apClients() const { return apClients_; }
  uint32_t primaryIp() const { return primaryIp_; }

  // True once the network has flapped enough to be treated as faulty. In this
  // mode the grace period doubles and the AP is kept up rather than tidied.
  bool degraded() const { return degraded_; }
  uint8_t transitions() const { return transitions_; }
  // How many extra overlap windows were granted because a device had not
  // left the AP yet. Non-zero means something is not migrating.
  uint8_t apExtensions() const { return apExtensions_; }

  struct Event {
    uint32_t ms;
    NetState from;
    NetState to;
    NetReason reason;
  };
  static const uint8_t kEventLog = 16;
  const Event *events() const { return events_; }
  uint8_t eventCount() const { return eventCount_; }
  uint8_t eventHead() const { return eventHead_; }  // next write index

  // Diagnostics.
  uint8_t channel() const { return channel_; }
  bool scanning() const { return scanning_; }
  uint16_t scanStarts() const { return scanStarts_; }
  uint16_t scanResults() const { return scanResults_; }
  // Non-zero means the driver accepted a scan request and never answered it.
  // Without a bound on that, one bad request stops the device looking for the
  // primary for ever.
  uint16_t scanAborts() const { return scanAborts_; }

  // Client-only recovery diagnostics.
  uint16_t assocResets() const { return assocResets_; }
  bool primarySeen() const { return primarySeen_; }
  const char *lastBeginSsid() const { return lastBeginSsid_; }

 private:
  void enter(NetState next, uint32_t now);
  void noteTransition(uint32_t now);
  bool verified();
  void scheduleRetry(uint32_t now);
  void raiseAp();
  void lowerAp();
  bool beginTo(const char *ssid, const char *pass, uint32_t now);

  NetConfig cfg_;
  NetPlatform *plat_ = nullptr;
  VerifyFn verify_ = nullptr;
  void *verifyCtx_ = nullptr;

  NetState state_ = NetState::Boot;
  NetReason reason_ = NetReason::None;
  bool polled_ = false;
  uint32_t stateSince_ = 0;
  uint32_t linkDownSince_ = 0;
  uint32_t nextActionAt_ = 0;
  uint32_t retryInterval_ = 0;

  bool staUp_ = false;
  bool apUp_ = false;
  uint8_t apClients_ = 0;
  uint32_t primaryIp_ = 0;

  bool degraded_ = false;
  uint8_t transitions_ = 0;
  uint8_t flapCount_ = 0;
  uint8_t apExtensions_ = 0;
  uint32_t flapWindowStart_ = 0;

  bool scanning_ = false;
  uint32_t nextScanAt_ = 0;
  uint32_t nextFullScanAt_ = 0;
  uint32_t scanStartedAt_ = 0;
  uint32_t nextSwitchAt_ = 0;
  uint32_t nextBlindAt_ = 0;
  uint16_t scanStarts_ = 0;
  uint16_t scanResults_ = 0;
  uint16_t scanAborts_ = 0;
  bool primarySeen_ = false;
  // Where the primary was last seen. Learned from the station link and from
  // scans, so the frequent look can be a single-channel one.
  uint8_t channel_ = 0;
  uint32_t lastBeginMs_ = 0;
  uint32_t lastScanOkMs_ = 0;
  uint32_t nextFallbackAt_ = 0;
  const char *lastBeginSsid_ = nullptr;
  bool beginPending_ = false;
  uint16_t assocResets_ = 0;
  uint8_t scanFails_ = 0;

  Event events_[kEventLog] = {};
  uint8_t eventCount_ = 0;
  uint8_t eventHead_ = 0;
};

}  // namespace sks
