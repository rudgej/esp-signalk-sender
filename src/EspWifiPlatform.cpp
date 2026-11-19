#include "EspWifiPlatform.h"

namespace sks {

void EspWifiPlatform::begin() {
  // The radio is reconfigured often (mode changes, SSID changes). Persisting
  // that to NVS on every change wears the flash out and, worse, makes a failed
  // fallback into a boot-time surprise.
  WiFi.persistent(false);
  // Modem sleep adds tens of milliseconds of latency and causes the access
  // point to miss beacons while the station is idle. Not acceptable on a
  // device that has to stay reachable.
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // the supervisor owns reconnection policy
}

void EspWifiPlatform::staBegin(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return;
  WiFi.begin(ssid, pass);
}

void EspWifiPlatform::staStop() { WiFi.disconnect(false, true); }

bool EspWifiPlatform::staUp() { return WiFi.status() == WL_CONNECTED; }

uint32_t EspWifiPlatform::staIp() {
  const IPAddress ip = WiFi.localIP();
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
         ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

const char *EspWifiPlatform::staSsid() {
  static char buf[33];
  const String s = WiFi.SSID();
  strncpy(buf, s.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}

void EspWifiPlatform::apStart(const char *ssid, const char *pass,
                              uint8_t channel, uint8_t maxClients,
                              uint32_t ip, uint32_t mask) {
  if (!ssid || !ssid[0]) return;
  // AP+STA on purpose: the station keeps trying to reach the primary while the
  // access point serves the devices that fell back. Both share one radio, so
  // the AP channel must match the primary - see NetConfig::apChannel.
  WiFi.mode(WIFI_AP_STA);

  if (ip != 0) {
    // Built from octets on purpose. IPAddress(uint32_t) takes the value in
    // network byte order, so casting the host-order integer straight in gives
    // 1.50.168.192 instead of 192.168.50.1 - an access point on a subnet
    // nobody expects, with a DHCP pool to match.
    const IPAddress local((uint8_t)(ip >> 24), (uint8_t)(ip >> 16),
                          (uint8_t)(ip >> 8), (uint8_t)ip);
    const uint32_t m = mask ? mask : 0x00FFFFFFu;
    const IPAddress subnet((uint8_t)(m >> 24), (uint8_t)(m >> 16),
                           (uint8_t)(m >> 8), (uint8_t)m);
    // The lease start matters as much as the address. Moving the AP to a new
    // subnet without moving the DHCP pool leaves clients able to associate but
    // never able to get an address - they sit there looking connected while
    // sending nothing, which is a far worse failure than not joining at all.
    const uint32_t leaseHost = ip + 1;
    const IPAddress lease((uint8_t)(leaseHost >> 24), (uint8_t)(leaseHost >> 16),
                          (uint8_t)(leaseHost >> 8), (uint8_t)leaseHost);
    // Without this the AP comes up on 192.168.4.1 regardless of what the
    // application configured, and anything told to send to the configured
    // address sends to a machine that is not there.
    WiFi.softAPConfig(local, local, subnet, lease);
  }

  WiFi.softAP(ssid, pass, channel, /*hidden=*/false, maxClients);
  WiFi.setSleep(false);
}

void EspWifiPlatform::apStop() {
  WiFi.softAPdisconnect(true);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);  // keep the station link, drop the AP personality
  } else {
    WiFi.mode(WIFI_STA);
  }
}

uint8_t EspWifiPlatform::apClients() { return WiFi.softAPgetStationNum(); }

void EspWifiPlatform::scanStart(const char *targetSsid, uint8_t channel) {
  scanTarget_ = targetSsid;
  // Asynchronous: a blocking scan stalls the AP and drops its clients.
  // Restricted to one channel when known - ~100 ms instead of ~2 s, which is
  // what makes it affordable to look every few seconds.
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false,
                    /*max_ms_per_chan=*/120, channel);
}

ScanResult EspWifiPlatform::scanPoll(uint8_t *channelOut) {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return ScanResult::Running;
  if (n == WIFI_SCAN_FAILED) return ScanResult::Idle;  // never started

  bool found = false;
  for (int16_t i = 0; i < n; ++i) {
    if (scanTarget_ && WiFi.SSID(i) == scanTarget_) {
      found = true;
      if (channelOut) *channelOut = (uint8_t)WiFi.channel(i);
      break;
    }
  }
  WiFi.scanDelete();
  return found ? ScanResult::FoundPrimary : ScanResult::NotFound;
}

uint8_t EspWifiPlatform::staChannel() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  return (uint8_t)WiFi.channel();
}

}  // namespace sks
