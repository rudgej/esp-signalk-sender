#pragma once

/*
 * Arduino/ESP32 implementation of NetPlatform.
 *
 * Kept apart from NetSupervisor.cpp so the rules stay testable and so a port
 * to another radio only has to reimplement this file.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "NetPlatform.h"

namespace sks {

class EspWifiPlatform : public NetPlatform {
 public:
  // Sets the mode and the power/behaviour flags the supervisor depends on.
  // Call once before NetSupervisor::begin().
  void begin();

  void staBegin(const char *ssid, const char *pass) override;
  void staStop() override;
  void staReset() override;
  bool staUp() override;
  uint32_t staIp() override;
  const char *staSsid() override;

  void apStart(const char *ssid, const char *pass, uint8_t channel,
               uint8_t maxClients, uint32_t ip, uint32_t mask) override;
  void apStop() override;
  uint8_t apClients() override;

  void scanStart(const char *targetSsid, uint8_t channel) override;
  ScanResult scanPoll(uint8_t *channelOut) override;
  uint8_t staChannel() override;

 private:
  const char *scanTarget_ = nullptr;
};

}  // namespace sks
