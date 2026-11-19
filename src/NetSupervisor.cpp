#include "NetSupervisor.h"

#include <string.h>

namespace sks {
namespace {

// Wrap-safe elapsed time. All comparisons below use this rather than raw
// subtraction so the module survives millis() rollover (49 days - a boat is
// powered for longer than that).
inline uint32_t el(uint32_t now, uint32_t then) { return now - then; }

inline bool reached(uint32_t now, uint32_t at) {
  return (int32_t)(now - at) >= 0;
}

}  // namespace

void NetSupervisor::begin(NetConfig config, NetPlatform *platform) {
  cfg_ = config;
  plat_ = platform;
  // A hub needs a name to advertise; without one it is just a client.
  if (!cfg_.fallbackSsid || !cfg_.fallbackSsid[0]) {
    cfg_.role = NetRole::ClientOnly;
  }
  if (!cfg_.flapLimit) cfg_.flapLimit = 1;
  if (!cfg_.scanMs) cfg_.scanMs = 20000;
  if (!cfg_.retryMs) cfg_.retryMs = 5000;
  if (!cfg_.retryMaxMs) cfg_.retryMaxMs = cfg_.retryMs;

  retryInterval_ = cfg_.retryMs;
  channel_ = cfg_.primaryChannel;
  state_ = NetState::Boot;
  stateSince_ = 0;
  nextActionAt_ = 0;  // first poll joins immediately
}

void NetSupervisor::setVerify(VerifyFn fn, void *ctx) {
  verify_ = fn;
  verifyCtx_ = ctx;
}

bool NetSupervisor::verified() {
  if (!plat_ || !plat_->staUp()) return false;
  if (!verify_) return true;
  return verify_(verifyCtx_);
}

void NetSupervisor::scheduleRetry(uint32_t now) {
  nextActionAt_ = now + retryInterval_;
  uint32_t next = retryInterval_ * 2;
  retryInterval_ = (next > cfg_.retryMaxMs || next < retryInterval_)
                       ? cfg_.retryMaxMs
                       : next;
}

void NetSupervisor::raiseAp() {
  if (!plat_ || apUp_) return;
  plat_->apStart(cfg_.fallbackSsid, cfg_.fallbackPass, cfg_.apChannel,
                 cfg_.apMaxClients, cfg_.apIp, cfg_.apMask);
  apUp_ = true;
}

void NetSupervisor::lowerAp() {
  if (!plat_ || !apUp_) return;
  plat_->apStop();
  apUp_ = false;
  apClients_ = 0;
}

void NetSupervisor::noteTransition(uint32_t now) {
  // Flap detection: repeatedly *falling back* is a fault, not a decision.
  // Counting every state change would trip the detector on the very first
  // legitimate failover (Boot -> PRIMARY -> LOST -> FALLBACK is one event, not
  // three), and would then leave the AP up for the rest of the window.
  if (now != 0 &&
      (flapWindowStart_ == 0 || el(now, flapWindowStart_) > cfg_.flapWindowMs)) {
    flapWindowStart_ = now;
    flapCount_ = 0;
    degraded_ = false;
  }
  if (state_ == NetState::Fallback && ++flapCount_ >= cfg_.flapLimit) {
    degraded_ = true;
  }

  if (transitions_ < 255) ++transitions_;
}

void NetSupervisor::enter(NetState next, uint32_t now) {
  const NetState prev = state_;
  if (prev == next) return;

  state_ = next;
  stateSince_ = now;

  switch (next) {
    case NetState::Fallback:
      nextScanAt_ = now;  // look for the primary straight away
      nextSwitchAt_ = now;
      nextBlindAt_ = now + cfg_.primaryProbeMs;
      if (cfg_.role == NetRole::Hub) raiseAp();
      break;
    case NetState::Recovering:
      // Nothing to do: the AP stays up through the overlap window on purpose.
      break;
    case NetState::PrimaryUp:
      retryInterval_ = cfg_.retryMs;
      // Only tidy the AP away when the network has been behaving. After a
      // burst of transitions the AP is left as the safety net (see poll()).
      if (!degraded_) lowerAp();
      break;
    default:
      break;
  }

  events_[eventHead_] = Event{now, prev, next, reason_};
  eventHead_ = (uint8_t)((eventHead_ + 1) % kEventLog);
  if (eventCount_ < kEventLog) ++eventCount_;

  noteTransition(now);
}

const char *NetSupervisor::stateName() const {
  switch (state_) {
    case NetState::Boot: return "BOOT";
    case NetState::PrimaryUp: return "PRIMARY";
    case NetState::PrimaryLost: return "LOST";
    case NetState::Fallback: return "FALLBACK";
    case NetState::Recovering: return "RECOVER";
  }
  return "?";
}

const char *NetSupervisor::reasonName() const {
  switch (reason_) {
    case NetReason::None: return "-";
    case NetReason::LinkLost: return "link";
    case NetReason::ServerSilent: return "server";
  }
  return "?";
}

void NetSupervisor::poll(uint32_t nowMs) {
  if (!plat_) return;
  const uint32_t now = nowMs;

  // Grace periods count from the first poll, not from millis()==0, otherwise a
  // slow boot would arrive already out of patience.
  if (!polled_) {
    polled_ = true;
    stateSince_ = now;
  }

  // ---------------------------------------------------------------- inputs
  // Asynchronous scan bookkeeping (never a blocking scan: this may be the AP).
  uint8_t foundChannel = 0;
  const ScanResult sr = plat_->scanPoll(&foundChannel);
  if (sr == ScanResult::FoundPrimary) {
    if (!primarySeen_) {
      // Seen it again: try now rather than serving out a backoff that was
      // sized for a network that was not there.
      nextActionAt_ = now;
      retryInterval_ = cfg_.retryMs;
    }
    primarySeen_ = true;
    scanning_ = false;
    ++scanResults_;
    if (foundChannel) channel_ = foundChannel;
  } else if (sr == ScanResult::NotFound) {
    primarySeen_ = false;
    scanning_ = false;
    ++scanResults_;
  } else if (scanning_ && el(now, scanStartedAt_) > 6000) {
    // The request was accepted and never answered - driver hiccup, radio busy,
    // or a scan that was silently dropped. Without this the flag stays set and
    // the device stops looking for the primary for ever, which looks exactly
    // like a sender that has given up.
    scanning_ = false;
    ++scanAborts_;
  }

  const bool up = plat_->staUp();
  if (!up && staUp_) linkDownSince_ = now;   // link just dropped
  if (up) linkDownSince_ = 0;
  staUp_ = up;
  primaryIp_ = up ? plat_->staIp() : 0;
  apClients_ = apUp_ ? plat_->apClients() : 0;

  // Which network are we actually on? Being associated is not the same as
  // being on the primary. A client that has fallen back is associated to the
  // hub, and counting that as recovery sends its data to a server that is not
  // running - the exact failure this module exists to prevent.
  const char *ssid = up ? plat_->staSsid() : nullptr;
  const bool onPrimaryNet = up && ssid != nullptr && cfg_.primarySsid != nullptr &&
                            strcmp(ssid, cfg_.primarySsid) == 0;

  // Once seen, the primary's channel is remembered so the frequent look can be
  // a single-channel scan. Learning it from the live link is free.
  if (onPrimaryNet) {
    const uint8_t ch = plat_->staChannel();
    if (ch) channel_ = ch;
  }

  const bool ok = onPrimaryNet && verified();
  if (ok) {
    reason_ = NetReason::None;
    retryInterval_ = cfg_.retryMs;  // healthy: forget the backoff
  }

  // Effective grace doubles once the network has proven itself unreliable.
  const uint32_t grace = degraded_ ? cfg_.graceMs * 2u : cfg_.graceMs;

  switch (state_) {
    case NetState::Boot: {
      if (ok) {
        enter(NetState::PrimaryUp, now);
        break;
      }
      if (reached(now, nextActionAt_)) {
        plat_->staBegin(cfg_.primarySsid, cfg_.primaryPass);
        scheduleRetry(now);
      }
      const uint32_t since = linkDownSince_ ? linkDownSince_ : stateSince_;
      if (el(now, since) >= grace) {
        reason_ = NetReason::LinkLost;
        enter(NetState::Fallback, now);
      }
      break;
    }

    case NetState::PrimaryUp: {
      // A burst of transitions expires on its own: after the window with no
      // further trouble the AP is tidied away and full reliability restored.
      if (degraded_ && el(now, flapWindowStart_) > cfg_.flapWindowMs) {
        degraded_ = false;
        lowerAp();
      }
      if (!ok) {
        reason_ = up ? NetReason::ServerSilent : NetReason::LinkLost;
        enter(NetState::PrimaryLost, now);
      }
      break;
    }

    case NetState::PrimaryLost: {
      if (ok) {
        enter(NetState::PrimaryUp, now);
        break;
      }
      if (onPrimaryNet) {
        // Link is healthy, the server is not. Reconnecting the radio would
        // change nothing, and an AP of our own would help nobody: every
        // client already has a working network. The application owns the
        // WebSocket/probe retry; we just stop claiming to be on the primary.
        reason_ = NetReason::ServerSilent;
        break;
      }
      reason_ = NetReason::LinkLost;
      if (reached(now, nextActionAt_)) {
        plat_->staBegin(cfg_.primarySsid, cfg_.primaryPass);
        scheduleRetry(now);
      }
      const uint32_t since = linkDownSince_ ? linkDownSince_ : stateSince_;
      if (el(now, since) >= grace) enter(NetState::Fallback, now);
      break;
    }

    case NetState::Fallback: {
      if (ok) {
        enter(NetState::Recovering, now);
        break;
      }
      reason_ = up ? NetReason::ServerSilent : NetReason::LinkLost;

      // Look for the primary often, but cheaply. With the channel known a scan
      // is a single-channel sweep - a tenth of the airtime, and short enough
      // that attached clients do not drop - so recovery can be measured in
      // seconds. A full-band sweep still happens occasionally as a backstop
      // in case the plotter comes back on a different channel.
      if (!scanning_ && reached(now, nextScanAt_)) {
        const bool full = (channel_ == 0) || reached(now, nextFullScanAt_);
        plat_->scanStart(cfg_.primarySsid, full ? 0 : channel_);
        scanning_ = true;
        scanStartedAt_ = now;
        ++scanStarts_;
        if (full) nextFullScanAt_ = now + cfg_.scanMs;
        nextScanAt_ = now + (channel_ ? cfg_.scanFastMs : cfg_.scanMs);
      }

      // What is the radio actually attached to? Between association and the
      // SSID becoming readable there is a moment where it is neither, and
      // treating that moment as "not on the primary" causes a reconnect to the
      // network already in use - which knocks the device off it and repeats.
      const char *here = up ? plat_->staSsid() : nullptr;
      const bool onFallbackNet = here != nullptr && cfg_.fallbackSsid != nullptr &&
                                 strcmp(here, cfg_.fallbackSsid) == 0;

      if (!up && reached(now, nextActionAt_)) {
        if (primarySeen_ || !cfg_.fallbackSsid) {
          plat_->staBegin(cfg_.primarySsid, cfg_.primaryPass);
        } else if (cfg_.role == NetRole::ClientOnly) {
          plat_->staBegin(cfg_.fallbackSsid, cfg_.fallbackPass);
        }
        scheduleRetry(now);
      }

      // Sitting on the hub's network: leave for the primary, but only when
      // there is a reason to and not more often than the switch interval. The
      // blind probe is the backstop for the case where scanning never works.
      const bool blindDue =
          cfg_.role == NetRole::ClientOnly && reached(now, nextBlindAt_);
      if (onFallbackNet && reached(now, nextSwitchAt_) &&
          (primarySeen_ || blindDue)) {
        if (!primarySeen_) nextBlindAt_ = now + cfg_.primaryProbeMs;
        nextSwitchAt_ = now + cfg_.switchMs;
        plat_->staBegin(cfg_.primarySsid, cfg_.primaryPass);
      }
      break;
    }

    case NetState::Recovering: {
      if (!ok) {
        reason_ = up ? NetReason::ServerSilent : NetReason::LinkLost;
        enter(NetState::Fallback, now);
        break;
      }
      if (el(now, stateSince_) < cfg_.holdAfterRecoveryMs) break;

      // Overlap expired. Tear the AP down unless a device is still on it -
      // stranding a device to keep the state machine tidy is the wrong trade,
      // and the brief is explicit that fallback capability is retained rather
      // than a device sacrificed for it.
      if (cfg_.role == NetRole::Hub && apUp_ && apClients_ > 0) {
        if (apExtensions_ < 255) ++apExtensions_;
        stateSince_ = now;  // hold on, and re-check after another window
        break;
      }
      enter(NetState::PrimaryUp, now);
      break;
    }
  }
}

}  // namespace sks
