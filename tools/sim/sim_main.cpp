/*
  Runs one sketch of this kit on a computer, in a pretend world.

      tools/sim/run.sh <stage folder> "<scenario>"

  The scenario is a list of things that happen, separated by commas. Times are in
  milliseconds since power-on.

      end@12000                   stop the run after 12 seconds (default 10000)
      connect@3000                a dashboard connects over Bluetooth
      disconnect@9000             and leaves
      swing@5000:400:1100:1       a swing: start, how long, peak rotation around y in
                                  degrees per second, side (+1 leg, -1 off, 0 straight).
                                  A fifth number is the extra acceleration at the peak, in g.
      wobble@0:2000               the board is moved gently for this long
      button@6000:1500            the BOOT button is held
      type@4000:r                 the letter r is sent from the Serial Monitor
      battery@0:3900              the cell is at 3.9 V from this moment (0 = not connected)
      unplug@7000:800             the sensor stops answering for this long
      nosensor                    there is no sensor at all

  Environment variables
      SIM_PACKET_LOG   file that receives every Bluetooth notification: time, id, bytes
      SIM_SENSOR_LOG   file that receives every sensor reading: time, then 6 counts
      SIM_LIGHT_LOG    file that receives every change of the light: time, 1 or 0
      SIM_FLASH_FILE   file that plays the role of flash memory between runs
      SIM_RESET_REASON brownout, to pretend the chip restarted because of low voltage
*/
#include <string>
#include <vector>

#include "Arduino.h"
#include "BLEDevice.h"
#include "Wire.h"
#include "esp_sleep.h"
#include "esp_system.h"

SerialPort Serial;
EspInfo ESP;
TwoWire Wire;

namespace sim {

uint32_t nowMs = 0;
bool bleIsAdvertising = false;

struct Span { uint32_t start, length; };
struct Swing { uint32_t start, length; float peak; int side; float accel; };
struct Timed { uint32_t at; int value; bool done; };

static uint32_t endAt = 10000;
static std::vector<Swing> swings;
static std::vector<Span> wobbles, presses, unplugs;
static std::vector<Timed> connects, disconnects, typed, batteries;
static bool noSensor = false;
static int batteryMillivoltsNow = 0;
static BLEServer *server = nullptr;
static FILE *packetLog = nullptr, *sensorLog = nullptr, *lightLog = nullptr;
static int lightLevel = -1;
static uint32_t noiseState = 12345;

static bool inside(const std::vector<Span> &spans) {
  for (const Span &s : spans) {
    if (nowMs >= s.start && nowMs < s.start + s.length) return true;
  }
  return false;
}

// A small repeatable random number between -1 and +1.
static float noise() {
  noiseState = noiseState * 1664525u + 1013904223u;
  return ((noiseState >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

static int16_t toCounts(float value, float countsPerUnit) {
  float counts = roundf(value * countsPerUnit);
  if (counts > 32767) counts = 32767;      // the real chip also stops at its limit
  if (counts < -32768) counts = -32768;
  return (int16_t)counts;
}

// What the pretend sensor measures at this moment.
void sensorCounts(int16_t out[6]) {
  // At rest: gravity on z, and a gyroscope that is slightly off zero like every real one.
  float accel[3] = {0.012f, -0.020f, 1.004f};
  float gyro[3] = {0.30f, -0.49f, 0.12f};

  for (const Swing &s : swings) {
    if (nowMs >= s.start && nowMs < s.start + s.length) {
      float k = sinf((float)(nowMs - s.start) / s.length * (float)PI);   // rises to 1 and falls back
      gyro[0] += -0.25f * s.peak * k;
      gyro[1] += s.peak * k;
      gyro[2] += s.side * (0.10f * s.peak * k + 3.0f);
      accel[0] += s.accel * k;
      accel[1] += s.accel * 0.75f * k;
    }
  }
  if (inside(wobbles)) {
    float phase = nowMs / 90.0f;
    gyro[0] += 25.0f * sinf(phase);
    gyro[1] += 18.0f * cosf(phase * 1.3f);
  }
  for (int i = 0; i < 3; i++) {
    out[i] = toCounts(gyro[i] + 0.06f * noise(), 16.4f);
    out[3 + i] = toCounts(accel[i] + 0.0007f * noise(), 4096.0f);
  }
}

bool sensorAnswers() { return !noSensor && !inside(unplugs); }

void logSensorRead(const int16_t c[6]) {
  if (sensorLog) fprintf(sensorLog, "%u %d %d %d %d %d %d\n", nowMs, c[0], c[1], c[2], c[3], c[4], c[5]);
}

bool buttonPressed() { return inside(presses); }

int batteryPinMillivolts() {
  if (batteryMillivoltsNow == 0) return 3 + (int)(2 * noise());   // an open pin reads a few millivolts
  return batteryMillivoltsNow / 2 + (int)(4 * noise());            // behind two equal resistors
}

bool serialHasChar() {
  for (const Timed &t : typed) {
    if (!t.done && t.at <= nowMs) return true;
  }
  return false;
}

int nextSerialChar() {
  for (Timed &t : typed) {
    if (!t.done && t.at <= nowMs) {
      t.done = true;
      return t.value;
    }
  }
  return -1;
}

void ledChanged(int level) {
  if (level == lightLevel) return;
  lightLevel = level;
  if (lightLog) fprintf(lightLog, "%u %d\n", nowMs, level == LOW ? 1 : 0);   // on this board LOW is on
}

void bleRegisterServer(BLEServer *created) { server = created; }

void bleAdvertising(const char *) { bleIsAdvertising = true; }

void bleNotify(const std::string &uuid, const uint8_t *data, size_t size) {
  if (!packetLog) return;
  fprintf(packetLog, "%u %s ", nowMs, uuid.c_str());
  for (size_t i = 0; i < size; i++) fprintf(packetLog, "%02x", data[i]);
  fputc('\n', packetLog);
}

static void finish() {
  fflush(stdout);
  if (packetLog) fclose(packetLog);
  if (sensorLog) fclose(sensorLog);
  if (lightLog) fclose(lightLog);
  exit(0);
}

void tick(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    nowMs++;
    for (Timed &t : batteries) {
      if (!t.done && t.at <= nowMs) { t.done = true; batteryMillivoltsNow = t.value; }
    }
    for (Timed &t : connects) {
      // A dashboard can only connect to a device that is advertising.
      if (!t.done && t.at <= nowMs && server && bleIsAdvertising) { t.done = true; server->simConnect(); }
    }
    for (Timed &t : disconnects) {
      if (!t.done && t.at <= nowMs && server) { t.done = true; server->simDisconnect(); }
    }
    if (nowMs >= endAt) finish();
  }
}

static void parse(const char *scenario) {
  std::string text = scenario ? scenario : "";
  size_t position = 0;
  while (position < text.size()) {
    size_t comma = text.find(',', position);
    std::string item = text.substr(position, comma == std::string::npos ? std::string::npos : comma - position);
    position = comma == std::string::npos ? text.size() : comma + 1;
    if (item.empty()) continue;

    char name[32] = "";
    double a = 0, b = 0, c = 0, d = 0, e = 0;
    char letters[32] = "";
    if (item == "nosensor") { noSensor = true; continue; }
    if (sscanf(item.c_str(), "type@%lf:%31s", &a, letters) == 2) {
      for (size_t i = 0; letters[i]; i++) typed.push_back({(uint32_t)a + (uint32_t)i * 50, letters[i], false});
      continue;
    }
    int fields = sscanf(item.c_str(), "%31[a-z]@%lf:%lf:%lf:%lf:%lf", name, &a, &b, &c, &d, &e);
    std::string kind = name;
    if (kind == "end" && fields >= 2) endAt = (uint32_t)a;
    else if (kind == "connect" && fields >= 2) connects.push_back({(uint32_t)a, 0, false});
    else if (kind == "disconnect" && fields >= 2) disconnects.push_back({(uint32_t)a, 0, false});
    else if (kind == "swing" && fields >= 5) swings.push_back({(uint32_t)a, (uint32_t)b, (float)c, (int)d, fields >= 6 ? (float)e : 2.0f});
    else if (kind == "wobble" && fields >= 3) wobbles.push_back({(uint32_t)a, (uint32_t)b});
    else if (kind == "button" && fields >= 3) presses.push_back({(uint32_t)a, (uint32_t)b});
    else if (kind == "unplug" && fields >= 3) unplugs.push_back({(uint32_t)a, (uint32_t)b});
    else if (kind == "battery" && fields >= 3) batteries.push_back({(uint32_t)a, (int)b, false});
    else { fprintf(stderr, "simulator: cannot read scenario item \"%s\"\n", item.c_str()); exit(2); }
  }
  // Things that are true from the first instant.
  for (Timed &t : batteries) {
    if (t.at == 0) { t.done = true; batteryMillivoltsNow = t.value; }
  }
}

}  // namespace sim

esp_reset_reason_t esp_reset_reason() {
  const char *reason = getenv("SIM_RESET_REASON");
  return reason && std::string(reason) == "brownout" ? ESP_RST_BROWNOUT : ESP_RST_POWERON;
}

void esp_deep_sleep_start() {
  printf("[simulator] the chip is in deep sleep at %.1f s. Nothing runs until power is switched off and on.\n", sim::nowMs / 1000.0);
  sim::finish();
}

// ---- the sketch itself, exactly as the Arduino IDE would compile it ----
#include SKETCH

int main(int argc, char **argv) {
  sim::parse(argc > 1 ? argv[1] : "");
  const char *path;
  if ((path = getenv("SIM_PACKET_LOG"))) sim::packetLog = fopen(path, "w");
  if ((path = getenv("SIM_SENSOR_LOG"))) sim::sensorLog = fopen(path, "w");
  if ((path = getenv("SIM_LIGHT_LOG"))) sim::lightLog = fopen(path, "w");
  setup();
  for (;;) {
    loop();
  }
}
