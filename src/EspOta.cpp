#include "EspOta.h"

namespace sks {

void EspOta::begin(const char *hostname, const char *password) {
  if (started_) return;

  if (hostname && hostname[0]) ArduinoOTA.setHostname(hostname);
  if (password && password[0]) ArduinoOTA.setPassword(password);
  // The application owns mDNS for Signal K host resolution. ArduinoOTA would
  // call MDNS.end() when the listener is closed on leaving the primary, which
  // would take that service down with it. OTA is addressed by IP by the N100.
  ArduinoOTA.setMdnsEnabled(false);

  ArduinoOTA.onStart([this]() {
    updating_ = true;
    Serial.printf("[OTA] start (%s)\n",
                  ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });
  ArduinoOTA.onEnd([this]() {
    updating_ = false;
    Serial.println("[OTA] complete, rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (!total) return;
    const unsigned int pct = progress * 100u / total;
    static unsigned int last = 200;  // force the first report
    if (pct / 10u != last / 10u) {
      last = pct;
      Serial.printf("[OTA] %u%%\n", pct);
    }
  });
  ArduinoOTA.onError([this](ota_error_t err) {
    updating_ = false;
    Serial.printf("[OTA] error %u\n", (unsigned)err);
  });

  started_ = true;
}

void EspOta::poll(bool allowed) {
  if (!started_) return;

  // Leaving the primary network closes the listener. Staying bound on the
  // fallback - and, on the CYD, while both networks are up - would leave an
  // OTA path that the N100 is not supposed to have.
  if (!allowed && !updating_) {
    if (listening_) {
      ArduinoOTA.end();
      listening_ = false;
      Serial.println("[OTA] closed");
    }
    return;
  }

  if (!listening_) {
    ArduinoOTA.begin();
    listening_ = true;
    Serial.println("[OTA] listening");
  }
  ArduinoOTA.handle();
}

}  // namespace sks
