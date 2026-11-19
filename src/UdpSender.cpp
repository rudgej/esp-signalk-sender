#include "UdpSender.h"

namespace sks {

bool UdpSender::begin(uint16_t localPort) {
  open_ = udp_.begin(localPort) == 1;
  return open_;
}

void UdpSender::end() {
  if (open_) udp_.stop();
  open_ = false;
}

void UdpSender::setDestination(const IPAddress &ip, uint16_t port) {
  ip_ = ip;
  port_ = port;
  set_ = (port != 0) && (static_cast<uint32_t>(ip) != 0);
}

void UdpSender::clearDestination() {
  set_ = false;
  port_ = 0;
  ip_ = IPAddress(0, 0, 0, 0);
}

bool UdpSender::send(const char *data, size_t len) {
  if (!data || !len || !set_) return false;
  if (!open_) {
    // Opening lazily means a device that has never had a destination does not
    // hold a socket open for the whole time it is unconfigured.
    open_ = udp_.begin(0) == 1;
    if (!open_) return false;
  }
  if (!udp_.beginPacket(ip_, port_)) return false;
  udp_.write(reinterpret_cast<const uint8_t *>(data), len);
  return udp_.endPacket() == 1;
}

bool UdpSender::send(const String &sentence) {
  return send(sentence.c_str(), sentence.length());
}

}  // namespace sks
