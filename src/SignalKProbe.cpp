#include "SignalKProbe.h"

#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace sks {

String nmea(const String &body) {
  uint8_t cs = 0;
  for (size_t i = 0; i < body.length(); i++) cs ^= (uint8_t)body[i];
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
  return String("$") + body + tail;
}

bool extractNumber(const String &body, const char *key, float &out) {
  int k = body.indexOf(String("\"") + key + "\"");
  if (k < 0) return false;
  int v = body.indexOf("\"value\"", k);
  if (v < 0) return false;
  int colon = body.indexOf(':', v);
  if (colon < 0) return false;
  int start = colon + 1;
  while (start < (int)body.length() &&
         (body[start] == ' ' || body[start] == '\t'))
    start++;
  int end = start;
  while (end < (int)body.length() &&
         (isdigit((unsigned char)body[end]) || body[end] == '-' ||
          body[end] == '+' || body[end] == '.' || body[end] == 'e' ||
          body[end] == 'E'))
    end++;
  if (end == start) return false;
  out = body.substring(start, end).toFloat();
  return true;
}

namespace {

SemaphoreHandle_t g_mutex = nullptr;
String g_url;
unsigned long g_period = 5000;
unsigned long g_timeout = 1500;
ProbeResult g_result;
bool g_pending = false;
TaskHandle_t g_task = nullptr;

void lock() {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}

void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

void probeTask(void *) {
  for (;;) {
    lock();
    unsigned long period = g_period;
    unlock();

    vTaskDelay(pdMS_TO_TICKS(period ? period : 1000));

    String url;
    unsigned long timeout;
    lock();
    url = g_url;
    timeout = g_timeout;
    unlock();

    if (url.isEmpty()) continue;

    ProbeResult r;
    HTTPClient http;
    http.setTimeout(timeout);
    if (http.begin(url)) {
      r.code = http.GET();
      if (r.code == HTTP_CODE_OK) {
        r.body = http.getString();   // blocking, but only on this task
        r.ok = true;
      }
      http.end();
    }
    r.atMs = millis();

    lock();
    g_result = r;
    g_pending = true;
    unlock();
  }
}

}  // namespace

void Probe::begin(const String &url, unsigned long periodMs,
                  unsigned long timeoutMs) {
  lock();
  g_url = url;
  g_period = periodMs;
  g_timeout = timeoutMs;
  g_pending = false;
  unlock();

  if (!_started) {
    // Pin to core 0 and keep off the Arduino loop task (core 1). 6 KB is
    // enough for HTTPClient plus a small response body.
    xTaskCreatePinnedToCore(probeTask, "skprobe", 6144, nullptr, 1, &g_task, 0);
    _started = true;
  }
}

void Probe::setUrl(const String &url) {
  lock();
  g_url = url;
  unlock();
}

void Probe::setPeriod(unsigned long periodMs) {
  lock();
  g_period = periodMs;
  unlock();
}

bool Probe::take(ProbeResult &out) {
  bool have = false;
  lock();
  if (g_pending) {
    out = g_result;
    g_pending = false;
    have = true;
  }
  unlock();
  return have;
}

bool Probe::running() const { return _started; }

void Probe::stop() {
  if (g_task) {
    vTaskDelete(g_task);
    g_task = nullptr;
  }
  _started = false;
}

}  // namespace sks
