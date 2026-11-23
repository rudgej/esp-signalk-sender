// Host test for the NetSupervisor failover fix (no Arduino required).
#include <cstdio>
#include <cstring>
#include <string>

#include "NetSupervisor.h"

using namespace sks;

struct Mock : NetPlatform {
  bool up = false;
  bool okPrimary = false;
  bool okFallback = true;
  const char *ssid = nullptr;
  int beginPrimary = 0, beginFallback = 0, resets = 0, stops = 0, scanStarts = 0;
  int overlaps = 0;
  bool attemptActive = false;
  ScanResult nextScan = ScanResult::NotFound;
  bool scanPending = false;
  bool foundDelivered = false;

  void staBegin(const char *s, const char *) override {
    if (scanPending) ++overlaps;
    attemptActive = true;
    if (s && std::strcmp(s, "BoatNetwork") == 0) {
      ++beginPrimary;
      up = okPrimary;
    } else if (s && std::strcmp(s, "BoatNetwork2") == 0) {
      ++beginFallback;
      up = okFallback;
    } else {
      up = false;
    }
    ssid = up ? s : nullptr;
  }
  void staStop() override { ++stops; attemptActive = false; up = false; ssid = nullptr; }
  void staReset() override { ++resets; attemptActive = false; up = false; ssid = nullptr; }
  bool staUp() override {
    if (up) attemptActive = false;
    return up;
  }
  uint32_t staIp() override { return up ? 0xC0A83201u : 0; }
  const char *staSsid() override { return up ? ssid : nullptr; }
  void apStart(const char *, const char *, uint8_t, uint8_t, uint32_t,
               uint32_t) override {}
  void apStop() override {}
  uint8_t apClients() override { return 0; }
  void scanStart(const char *, uint8_t) override {
    if (attemptActive) ++overlaps;
    scanPending = true;
    ++scanStarts;
  }
  ScanResult scanPoll(uint8_t *) override {
    if (!scanPending) return ScanResult::Idle;
    scanPending = false;
    if (nextScan == ScanResult::FoundPrimary) foundDelivered = true;
    return nextScan;
  }
  uint8_t staChannel() override { return 6; }
};

static NetConfig cfg() {
  NetConfig c;
  c.primarySsid = "BoatNetwork";
  c.primaryPass = "pw";
  c.fallbackSsid = "BoatNetwork2";
  c.fallbackPass = "pw";
  c.role = NetRole::ClientOnly;
  c.graceMs = 60000;
  c.retryMs = 5000;
  c.retryMaxMs = 30000;
  c.scanMs = 60000;
  c.scanFastMs = 5000;
  c.primaryChannel = 6;  // the sensor learned the primary's channel before it vanished
  c.assocTimeoutMs = 20000;
  c.fallbackRetryMs = 5000;
  c.blindProbeMs = 0;
  return c;
}

static int g_failures = 0;

static void result(const char *name, bool ok) {
  std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) ++g_failures;
}

int main() {
  // 1. Fallback association never succeeds (e.g. auth failure). The watchdog
  //    must reset the radio and keep trying, not sit in "connecting" forever.
  {
    Mock m;
    m.okPrimary = false;
    m.okFallback = false;
    NetSupervisor s;
    s.begin(cfg(), &m);
    for (uint32_t t = 0; t <= 120000; t += 250) s.poll(t);
    std::printf("1: fallbackBegins=%d resets=%d state=%s\n", m.beginFallback,
                m.resets, s.stateName());
    result("fallback attempted", m.beginFallback > 0);
    result("stuck association reset", m.resets > 0);
    result("no scan/join overlap", m.overlaps == 0);
  }

  // 2. Fallback joins successfully; with blindProbeMs=0 it must stay there.
  {
    Mock m;
    m.okPrimary = false;
    m.okFallback = true;
    NetSupervisor s;
    s.begin(cfg(), &m);
    for (uint32_t t = 0; t <= 90000; t += 250) s.poll(t);
    const int prim = m.beginPrimary;
    const int resets0 = m.resets;
    for (uint32_t t = 90000; t <= 360000; t += 250) s.poll(t);
    std::printf("2: fallbackBegins=%d primaryBefore=%d primaryAfter=%d resets0=%d resetsAfter=%d state=%s\n",
                m.beginFallback, prim, m.beginPrimary, resets0, m.resets, s.stateName());
    result("joined fallback", m.beginFallback > 0);
    result("no blind probe away", m.beginPrimary == prim);
    result("no reset after good join", m.resets == resets0);
    result("no scan/join overlap", m.overlaps == 0);
  }

  // 3. On the fallback, the primary is seen once and then scans fail forever.
  //    The stale flag must clear so the client stops chasing a ghost.
  {
    Mock m;
    m.okPrimary = false;
    m.okFallback = true;
    m.nextScan = ScanResult::NotFound;
    NetSupervisor s;
    s.begin(cfg(), &m);
    for (uint32_t t = 0; t <= 90000; t += 250) s.poll(t);  // join fallback
    m.nextScan = ScanResult::FoundPrimary;
    for (uint32_t t = 90250; t <= 95000; t += 250) s.poll(t);
    const bool seen = s.primarySeen();
    m.nextScan = ScanResult::Idle;  // scan requests now never complete
    for (uint32_t t = 95250; t <= 220000; t += 250) s.poll(t);
    std::printf("3: foundDelivered=%d seenAfterFound=%d seenAfterFails=%d fallbackBegins=%d state=%s\n",
                (int)m.foundDelivered, (int)seen, (int)s.primarySeen(),
                m.beginFallback, s.stateName());
    result("primary seen initially", seen);
    result("stale primary cleared", !s.primarySeen());
    result("fallback attempted after scan failure", m.beginFallback > 0);
  }

  // 4. A hub whose station hangs must be disconnected, but never have its own
  //    access point powered down: staStop(), not the client's staReset().
  {
    Mock m;
    m.okPrimary = false;
    NetConfig c = cfg();
    c.role = NetRole::Hub;
    NetSupervisor s;
    s.begin(c, &m);
    for (uint32_t t = 0; t <= 120000; t += 250) s.poll(t);
    std::printf("4: primaryBegins=%d stops=%d resets=%d state=%s\n",
                m.beginPrimary, m.stops, m.resets, s.stateName());
    result("hub retries stuck station", m.beginPrimary > 1);
    result("hub disconnects without radio reset", m.stops > 0 && m.resets == 0);
  }

  // 5. Fallback is unavailable, then the primary comes back. The supervisor
  //    must stop trying only the fallback and take the primary once a scan
  //    proves it is there.
  {
    Mock m;
    m.okPrimary = false;
    m.okFallback = false;
    NetSupervisor s;
    s.begin(cfg(), &m);
    for (uint32_t t = 0; t <= 90000; t += 250) s.poll(t);
    const int before = m.beginPrimary;
    m.okPrimary = true;
    m.nextScan = ScanResult::FoundPrimary;
    for (uint32_t t = 90250; t <= 400000; t += 250) s.poll(t);
    std::printf("5: primaryBefore=%d primaryAfter=%d fallback=%d state=%s onPrimary=%d overlaps=%d\n",
                before, m.beginPrimary, m.beginFallback, s.stateName(),
                (int)s.onPrimary(), m.overlaps);
    result("primary retried after it returned", m.beginPrimary > before);
    result("recovered onto primary", s.onPrimary());
    result("no scan/join overlap", m.overlaps == 0);
  }

  std::printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
