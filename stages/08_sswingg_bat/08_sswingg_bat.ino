/*
  Stage 8 - The firmware for the bat

  Hardware: the complete circuit. See diagrams/full-circuit.svg.

  This is the sketch that stays on the bat. It is stage 5 with everything a real bat needs:
    - it measures the still position at start, and again when you hold BOOT for a second
    - it starts the sensor again by itself if a knock makes it stop answering
    - it can turn the sensor's axes to match how the board sits on the bat (config.h)
    - with the stage 7 parts fitted, it reports the battery level and protects the cell

  Settings are in config.h. The sensor steps are in bmi160.h, the 14-byte packet is in
  swing_packet.h and the battery arithmetic is in battery.h.

  Light on the board
    fast blinking                 measuring the still position. Keep the bat still.
    slow blinking                 waiting for the dashboard to connect
    steady on                     connected and sending
    two quick blinks, pause       sensor not found. Check the wiring.
    three quick blinks, pause     battery low. Charge soon.   (needs the stage 7 parts)
    five quick blinks, then off   battery empty. The bat sleeps until it is switched off and on.
*/

#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_sleep.h>
#include "config.h"
#include "bmi160.h"
#include "swing_packet.h"
#include "battery.h"

BMI160 imu;
BLECharacteristic *sensorCharacteristic = nullptr;
BLECharacteristic *batteryCharacteristic = nullptr;

volatile bool connected = false;
volatile bool advertiseAgain = false;

int32_t gyroBias[3] = {0, 0, 0};
int16_t lastTemperature = 0;
uint32_t nextSampleAt = 0;
uint32_t packetsThisSecond = 0;
uint32_t failedReads = 0;
uint32_t lastReportAt = 0;
uint32_t buttonDownSince = 0;
uint8_t lastPacket[SWING_PACKET_SIZE];

int batteryMillivolts = 0;
BatteryState battery = BATTERY_NOT_SENSED;
uint8_t batteryPercentSent = 255;
int emptyReadings = 0;

void setLight(bool on) {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, on ? LED_ON : !LED_ON);
#else
  (void)on;
#endif
}

void blink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    setLight(true);
    delay(onMs);
    setLight(false);
    delay(offMs);
  }
}

class ConnectionEvents : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    (void)server;
    connected = true;
  }
  void onDisconnect(BLEServer *server) override {
    (void)server;
    connected = false;
    advertiseAgain = true;  // restarted from loop(), not from inside this callback
  }
};

// Starts the sensor. Blinks and retries until it answers.
void startSensor() {
  BMI160::Status status = imu.begin();
  while (status != BMI160::OK) {
    Serial.printf("Sensor did not start: %s\n", BMI160::describe(status));
    blink(2, 120, 120);
    delay(1500);
    status = imu.begin();
  }
  Serial.printf("BMI160 found at 0x%02X\n", imu.address());
}

/*
  Measures the rotation the sensor reports while the bat is still, so it can be
  subtracted from every reading. Without this a still bat would show a small speed.
  Tries again when the bat moved during the measurement.
*/
void measureStillPosition() {
  Serial.println("Keep the bat still. Measuring the still position...");
  for (int attempt = 1; attempt <= 5; attempt++) {
    int64_t sum[3] = {0, 0, 0};
    int16_t lowest[3] = {32767, 32767, 32767};
    int16_t highest[3] = {-32768, -32768, -32768};
    int good = 0;

    for (int i = 0; i < 100; i++) {  // one second
      setLight((i / 5) % 2 == 0);
      BMI160::Sample s;
      if (imu.read(s)) {
        const int16_t g[3] = {s.gx, s.gy, s.gz};
        for (int k = 0; k < 3; k++) {
          sum[k] += g[k];
          lowest[k] = min(lowest[k], g[k]);
          highest[k] = max(highest[k], g[k]);
        }
        good++;
      }
      delay(10);
    }

    int widest = 0;
    for (int k = 0; k < 3; k++) {
      widest = max(widest, highest[k] - lowest[k]);
    }
    // 100 counts is about 6 degrees per second. A still sensor stays well inside that.
    bool still = good >= 80 && widest < 100;
    if (still || attempt == 5) {
      for (int k = 0; k < 3; k++) {
        gyroBias[k] = good > 0 ? (int32_t)(sum[k] / good) : 0;
      }
      Serial.printf("Still position: %.2f %.2f %.2f deg/s%s\n",
                    gyroBias[0] / BMI160_GYR_LSB_PER_DPS, gyroBias[1] / BMI160_GYR_LSB_PER_DPS,
                    gyroBias[2] / BMI160_GYR_LSB_PER_DPS,
                    still ? "" : "  (the bat was moving, so this may be off. Hold BOOT for 1 second to redo it.)");
      break;
    }
    Serial.println("The bat moved. Trying again...");
  }
  setLight(false);
}

// ----------------------------------------------------------------------------------
// Battery. Everything here does nothing unless HAS_BATTERY_SENSE is 1 in config.h.
// ----------------------------------------------------------------------------------

void readBattery() {
#if HAS_BATTERY_SENSE
  long sum = 0;
  const int samples = 16;
  for (int i = 0; i < samples; i++) {
    sum += analogReadMilliVolts(BATTERY_PIN);
  }
  int now = batteryMillivoltsFromPin((int)(sum / samples), BATTERY_DIVIDER_RATIO, BATTERY_CALIBRATION);
  // Smooth it over a few seconds: the voltage dips for a moment each time the radio sends.
  batteryMillivolts = batteryMillivolts == 0 ? now : (batteryMillivolts * 3 + now) / 4;
  battery = batteryState(batteryMillivolts, BATTERY_LOW_MILLIVOLTS, BATTERY_EMPTY_MILLIVOLTS);

  // Tell a connected phone or computer, through the standard Bluetooth battery value.
  uint8_t percent = (uint8_t)batteryPercent(batteryMillivolts);
  if (batteryCharacteristic != nullptr && percent != batteryPercentSent) {
    batteryPercentSent = percent;
    batteryCharacteristic->setValue(&percent, 1);
    if (connected) {
      batteryCharacteristic->notify();
    }
  }
#endif
}

// An empty cell must not be drained further. Stop everything and sleep.
// The chip then uses almost nothing. Switching the bat off and on wakes it.
void sleepBecauseEmpty() {
  Serial.printf("Battery empty at %.2f V. Going to sleep. Charge it, then switch off and on.\n", batteryMillivolts / 1000.0f);
  blink(5, 80, 120);
  setLight(false);
  delay(100);
  esp_deep_sleep_start();
}

void startBluetooth() {
  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ConnectionEvents());

  // The sensor readings, in the service the dashboard looks for.
  BLEService *service = server->createService(SERVICE_UUID);
  sensorCharacteristic = service->createCharacteristic(
      SENSOR_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
  // The descriptor a client writes to switch notifications on. The NimBLE stack adds it by itself.
  sensorCharacteristic->addDescriptor(new BLE2902());
#endif
  memset(lastPacket, 0, sizeof(lastPacket));
  sensorCharacteristic->setValue(lastPacket, SWING_PACKET_SIZE);
  service->start();

#if HAS_BATTERY_SENSE
  // The battery level, in the service every Bluetooth device uses for it (0x180F, value 0x2A19).
  // Phones and computers show it without any special app. The dashboard ignores it.
  BLEService *batteryService = server->createService(BLEUUID((uint16_t)0x180F));
  batteryCharacteristic = batteryService->createCharacteristic(
      BLEUUID((uint16_t)0x2A19), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
  batteryCharacteristic->addDescriptor(new BLE2902());
#endif
  uint8_t percent = (uint8_t)batteryPercent(batteryMillivolts);
  batteryPercentSent = percent;
  batteryCharacteristic->setValue(&percent, 1);
  batteryService->start();
#endif

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);  // the name travels in the scan response
  advertising->setMinPreferred(0x06);  // ask for a short connection interval
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.printf("Bluetooth is on. Look for \"%s\".\n", DEVICE_NAME);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // never wait for a computer that is not there, for example on battery
#endif
  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }

#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
#endif
#if BUTTON_PIN >= 0
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif
  setLight(false);

  Serial.println();
  Serial.println("===== SSwinGG bat sensor =====");

#if HAS_BATTERY_SENSE
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  readBattery();
  if (battery == BATTERY_NOT_SENSED) {
    Serial.println("Battery: not seen. Running from USB, or the switch is off.");
  } else {
    Serial.printf("Battery: %.2f V, about %d%%\n", batteryMillivolts / 1000.0f, batteryPercent(batteryMillivolts));
  }
  if (battery == BATTERY_EMPTY) {
    sleepBecauseEmpty();
  }
#endif

  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  Wire.setTimeOut(50);

  startSensor();
  measureStillPosition();
  startBluetooth();

  nextSampleAt = millis();
  lastReportAt = millis();
}

void sendReading() {
  BMI160::Sample s;
  if (!imu.read(s)) {
    failedReads++;
    return;
  }
  const int16_t accel[3] = {s.ax, s.ay, s.az};
  const int16_t gyro[3] = {s.gx, s.gy, s.gz};
  swingBuildPacket(accel, gyro, gyroBias, AXIS_SOURCE, AXIS_SIGN, lastTemperature, lastPacket);

  if (connected) {
    sensorCharacteristic->setValue(lastPacket, SWING_PACKET_SIZE);
    sensorCharacteristic->notify();
    packetsThisSecond++;
  }
}

// Once a second: check the battery, and print one line to show that it is alive.
void report() {
#if HAS_BATTERY_SENSE
  readBattery();
  // Five empty readings in a row, 5 seconds, so one dip during a swing does not stop the bat.
  emptyReadings = battery == BATTERY_EMPTY ? emptyReadings + 1 : 0;
  if (emptyReadings >= 5) {
    sleepBecauseEmpty();
  }
#endif

  int16_t v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = (int16_t)(lastPacket[2 * i] | (lastPacket[2 * i + 1] << 8));
  }
  Serial.printf("%s  sent %lu/s  accel %+.2f %+.2f %+.2f g  gyro %+.0f %+.0f %+.0f deg/s  failed reads %lu",
                connected ? "connected" : "waiting  ", (unsigned long)packetsThisSecond,
                v[0] / BMI160_ACC_LSB_PER_G, v[1] / BMI160_ACC_LSB_PER_G, v[2] / BMI160_ACC_LSB_PER_G,
                v[3] / BMI160_GYR_LSB_PER_DPS, v[4] / BMI160_GYR_LSB_PER_DPS, v[5] / BMI160_GYR_LSB_PER_DPS,
                (unsigned long)failedReads);
#if HAS_BATTERY_SENSE
  if (battery == BATTERY_NOT_SENSED) {
    Serial.print("  battery not seen");
  } else {
    Serial.printf("  battery %.2f V %d%%%s", batteryMillivolts / 1000.0f, batteryPercent(batteryMillivolts),
                  battery == BATTERY_LOW ? " LOW" : battery == BATTERY_EMPTY ? " EMPTY" : "");
  }
#endif
  Serial.println();
  packetsThisSecond = 0;

  int16_t raw;
  if (imu.readTemperature(raw)) {
    lastTemperature = raw;
  }
  // If the sensor stopped answering, for example after a knock on a loose wire, start it again.
  if (failedReads > 25) {
    Serial.println("Too many failed reads. Starting the sensor again.");
    failedReads = 0;
    startSensor();
  }
}

void loop() {
  uint32_t now = millis();

  if ((int32_t)(now - nextSampleAt) >= 0) {
    nextSampleAt += SAMPLE_PERIOD_MS;
    if ((int32_t)(now - nextSampleAt) > 200) {
      nextSampleAt = now + SAMPLE_PERIOD_MS;  // fell far behind, do not try to catch up
    }
    sendReading();
  }

  if (now - lastReportAt >= 1000) {
    lastReportAt = now;
    report();
  }

  if (advertiseAgain) {
    advertiseAgain = false;
    delay(200);  // give the Bluetooth stack a moment after a disconnect
    BLEDevice::startAdvertising();
    Serial.println("Disconnected. Waiting for a new connection.");
  }

  // Light: steady when connected, slow blink while waiting, three quick blinks when the battery is low.
  if (battery == BATTERY_LOW) {
    uint32_t t = now % 2000;
    setLight(t < 80 || (t >= 200 && t < 280) || (t >= 400 && t < 480));
  } else {
    setLight(connected ? true : (now / 500) % 2 == 0);
  }

#if BUTTON_PIN >= 0
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (buttonDownSince == 0) {
      buttonDownSince = now;
    } else if (now - buttonDownSince > 1000) {
      buttonDownSince = 0;
      measureStillPosition();
      nextSampleAt = millis();
    }
  } else {
    buttonDownSince = 0;
  }
#endif

  delay(1);
}
