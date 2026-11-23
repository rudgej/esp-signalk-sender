#pragma once

// ArduinoOTA wrapper for the closed ESP32 <-> N100 deployment loop.
//
// The OTA listener is only serviced while the caller says the device is on the
// primary network. That gate is "the network the N100 hosts", not an IP or a
// MAC, so replacing the N100 hardware does not invalidate the rule. The
// password is a fleet token, not a security boundary: anyone who can join the
// boat LAN and knows it can flash. That is deliberate - this is a closed loop,
// not an internet-facing update service.
//
// Kept separate from NetSupervisor on purpose: the state machine stays
// host-testable, this file is the Arduino-only edge.

#include <Arduino.h>
#include <ArduinoOTA.h>

namespace sks {

class EspOta {
 public:
  // Configure the listener. Does not open the port: the first poll(true) does
  // that, so a device that never reaches the primary never advertises OTA.
  void begin(const char *hostname, const char *password);

  // Call every loop(). `allowed` should be true only while associated to the
  // primary (N100) network. An update already in progress is allowed to finish
  // even if the link state flickers.
  void poll(bool allowed);

  bool updating() const { return updating_; }
  bool listening() const { return listening_; }

 private:
  bool started_ = false;
  bool listening_ = false;
  volatile bool updating_ = false;
};

}  // namespace sks
