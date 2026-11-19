#pragma once

/*
 * UdpSender - one UDP socket, a destination that can change at runtime.
 *
 * Both sensor firmwares had their own copy of "beginPacket/write/endPacket to
 * the resolved Signal K address". With a fallback network there are now two
 * possible destinations for the same sentence - the server on the primary
 * network, or the CYD acting as collector on the fallback network - so the
 * destination has to be settable rather than baked in at startup.
 */

#include <Arduino.h>
#include <WiFiUdp.h>

namespace sks {

class UdpSender {
 public:
  // localPort 0 is fine for send-only; use a port if you also want to receive
  // (the CYD's collector does).
  bool begin(uint16_t localPort = 0);
  void end();

  void setDestination(const IPAddress &ip, uint16_t port);
  void clearDestination();
  bool haveDestination() const { return set_ && port_ != 0; }
  IPAddress destination() const { return ip_; }
  uint16_t destinationPort() const { return port_; }

  // Returns false when there is nowhere to send or the packet could not be
  // queued. Callers treat that as "no collector", not as an error to retry in
  // a tight loop.
  bool send(const char *data, size_t len);
  bool send(const String &sentence);

 private:
  WiFiUDP udp_;
  IPAddress ip_{0, 0, 0, 0};
  uint16_t port_ = 0;
  bool set_ = false;
  bool open_ = false;
};

}  // namespace sks
