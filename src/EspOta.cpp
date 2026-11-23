#include "EspOta.h"

namespace sks {

void EspOta::begin(const char *hostname, const char *password) {
  if (started_) return;

  if (hostname && hostname[0]) ArduinoOTA.setHostname(hostname);
  if (password && password[0]) ArduinoOTA.setPassword(password);

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

  // Not on the N100's network and not mid-update: stay silent. The UDP port
  // is not even opened, so a device parked on the CYD's fallback AP cannot be
  // reached by an uploader.
  if (!allowed && !updating_) return;

  if (!listening_) {
    ArduinoOTA.begin();
    listening_ = true;
    Serial.println("[OTA] listening");
  }
  ArduinoOTA.handle();
}

}  // namespace sks
