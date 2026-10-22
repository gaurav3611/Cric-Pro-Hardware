/*
  Stage 8 - The firmware for the bat

  Hardware: the complete circuit. See diagrams/full-circuit.svg.

  This is the sketch that stays on the bat. It is stage 5 with everything a real bat needs:
    - it measures the still position at start, and again when you hold BOOT for a second
    - it starts the sensor again by itself if a knock makes it stop answering
    - it can turn the sensor's axes to match how the board sits on the bat (config.h)
    - with the stage 7 parts fitted, it reports the battery level and protects the cell

  This is the whole firmware in one file. The kit keeps the same code in five files:
  08_sswingg_bat.ino, config.h, bmi160.h, swing_packet.h and battery.h.
  Settings are in the first section below.

  Light on the board
    fast blinking                 measuring the still position. Keep the bat still.
    slow blinking                 waiting for the dashboard to connect
    steady on                     connected and sending
    two quick blinks, pause       sensor not found. Check the wiring.
    three quick blinks, pause     battery low. Charge soon.   (needs the stage 7 parts)
    five quick blinks, then off   battery empty. The bat sleeps until it is switched off and on.
*/
#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_sleep.h>
#include <stdint.h>
#include <stddef.h>

// =====================================================================================
// 1. Settings (config.h in the kit)
// =====================================================================================
/*
  Every setting of the bat firmware, in one place.

  Change things here, not in the other files. docs/make-changes.pdf explains what each
  change does to the rest of the system.
*/


// ----------------------------------------------------------------------------------
// Wiring. These must match what you soldered.
// ----------------------------------------------------------------------------------
#define SDA_PIN 3       // I2C data, to the sensor's SDA
#define SCL_PIN 4       // I2C clock, to the sensor's SCL
#define LED_PIN 8       // the light on the Super Mini. -1 if your board has none.
#define LED_ON LOW      // LOW switches the Super Mini's light on
#define BUTTON_PIN 9    // the BOOT button. Hold 1 second to measure the still position again. -1 to switch off.

// ----------------------------------------------------------------------------------
// Battery measurement (stage 7). Leave at 0 if you did not fit R1, R2 and C1.
// ----------------------------------------------------------------------------------
#ifndef HAS_BATTERY_SENSE   // tools/check.sh builds the firmware both ways
#define HAS_BATTERY_SENSE 0
#endif
#define BATTERY_PIN 0                  // GPIO0, the middle of the two resistors
#define BATTERY_DIVIDER_RATIO 2.0f     // two equal resistors halve the voltage
#define BATTERY_CALIBRATION 1.000f     // the number you found in stage 7
#define BATTERY_LOW_MILLIVOLTS 3600    // below this the light warns you
#define BATTERY_EMPTY_MILLIVOLTS 3450  // below this the bat stops and sleeps, to spare the cell

// ----------------------------------------------------------------------------------
// How the sensor sits on the bat.
// For each value the dashboard reads (x, y, z), choose which sensor axis feeds it
// (0 = x, 1 = y, 2 = z) and whether its sign is flipped. The same mapping is used for
// acceleration and rotation. See the README of stage 8, "Mount it on the bat".
// ----------------------------------------------------------------------------------
const uint8_t AXIS_SOURCE[3] = {0, 1, 2};
const int8_t AXIS_SIGN[3] = {+1, +1, +1};

// ----------------------------------------------------------------------------------
// The name people see when they connect. The dashboard only lists names that start
// with "SwingLab". With several bats, give each its own ending, for example SwingLab-Bat2.
// ----------------------------------------------------------------------------------
#define DEVICE_NAME "SwingLab-Bat"

// ----------------------------------------------------------------------------------
// Fixed by the dashboard. Changing any of these breaks the connection to it, unless the
// web app is changed in the same way (its file sensor-packet.ts).
// ----------------------------------------------------------------------------------
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SENSOR_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
const uint32_t SAMPLE_PERIOD_MS = 20;  // 50 readings a second

// Types used by the functions further down. They come first, because the Arduino IDE
// declares every function before the first one in the file.
const size_t SWING_PACKET_SIZE = 14;
enum BatteryState {
  BATTERY_NOT_SENSED,  // under 1 V: the switch is off, or the two resistors are not fitted
  BATTERY_EMPTY,       // stop now and charge
  BATTERY_LOW,         // charge soon
  BATTERY_OK,
};

// =====================================================================================
// 2. Battery arithmetic (battery.h in the kit)
// =====================================================================================
/*
  Battery arithmetic for one LiPo cell. No Arduino code in it, so it can be tested on a computer.

  A LiPo cell is 4.20 V when full and should not be used below about 3.3 V. The voltage does
  not fall in a straight line, so the charge is looked up in a small table. The result is a
  rough guide, not a fuel gauge: the voltage dips while the radio sends and rises again
  afterwards, and it differs from cell to cell.
*/


struct BatteryPoint {
  int millivolts;
  int percent;
};

// Voltage of the cell under a light load, and how full that roughly is.
// 0% is set where this circuit stops working, not where the cell is damaged.
const BatteryPoint BATTERY_CURVE[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 60}, {3800, 40}, {3700, 20}, {3600, 10}, {3500, 5}, {3400, 0},
};
const int BATTERY_CURVE_POINTS = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);

// Percent for a cell voltage, with straight lines between the points of the table.
inline int batteryPercent(int millivolts) {
  if (millivolts >= BATTERY_CURVE[0].millivolts) {
    return 100;
  }
  for (int i = 1; i < BATTERY_CURVE_POINTS; i++) {
    const BatteryPoint &high = BATTERY_CURVE[i - 1];
    const BatteryPoint &low = BATTERY_CURVE[i];
    if (millivolts >= low.millivolts) {
      int span = high.millivolts - low.millivolts;
      return low.percent + (millivolts - low.millivolts) * (high.percent - low.percent) / span;
    }
  }
  return 0;
}

/*
  The battery voltage from the voltage at the ADC pin.

  The pin must never see more than about 2.5 V, and a full cell is 4.2 V. Two equal
  resistors in a row halve it: 4.2 V becomes 2.1 V. So the cell voltage is the pin voltage
  times 2. `ratio` is that 2, and `calibration` corrects for resistors that are not exactly
  equal: it is the voltage your multimeter shows divided by the voltage this code shows.
*/
inline int batteryMillivoltsFromPin(int pinMillivolts, float ratio, float calibration) {
  return (int)(pinMillivolts * ratio * calibration + 0.5f);
}


inline BatteryState batteryState(int millivolts, int lowMillivolts, int emptyMillivolts) {
  if (millivolts < 1000) {
    return BATTERY_NOT_SENSED;
  }
  if (millivolts < emptyMillivolts) {
    return BATTERY_EMPTY;
  }
  if (millivolts < lowMillivolts) {
    return BATTERY_LOW;
  }
  return BATTERY_OK;
}

// =====================================================================================
// 3. The 14-byte Bluetooth packet (swing_packet.h in the kit)
// =====================================================================================
/*
  The Bluetooth packet the SSwinGG dashboard reads.

  14 bytes: seven 16-bit signed numbers, low byte first, in this order:

      byte   0  1    2  3    4  5    6  7    8  9   10 11   12 13
             ax      ay      az      gx      gy      gz     temperature

  The dashboard turns them back into units like this:
      acceleration in g      = number / 4096
      rotation in degrees/s  = number / 16.4
  It ignores the temperature.

  This file has no Arduino code in it, so it can also be compiled and tested on a computer.
  tools/check.sh does that, and compares the result with the code the web app uses to read it.
*/



inline int16_t swingClamp16(int32_t value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return (int16_t)value;
}

/*
  Builds one packet.

  accel, gyro   raw sensor counts for the sensor's own x, y, z
  gyroBias      rotation counts measured while the bat was still. It is subtracted.
  axisSource    for each output axis, which sensor axis feeds it: 0 = x, 1 = y, 2 = z
  axisSign      +1 or -1 for each output axis
*/
inline void swingBuildPacket(const int16_t accel[3], const int16_t gyro[3], const int32_t gyroBias[3],
                             const uint8_t axisSource[3], const int8_t axisSign[3],
                             int16_t temperature, uint8_t out[SWING_PACKET_SIZE]) {
  int16_t values[7];
  for (int i = 0; i < 3; i++) {
    uint8_t source = axisSource[i];
    values[i] = swingClamp16((int32_t)accel[source] * axisSign[i]);
    values[3 + i] = swingClamp16(((int32_t)gyro[source] - gyroBias[source]) * axisSign[i]);
  }
  values[6] = temperature;

  for (int i = 0; i < 7; i++) {
    uint16_t bits = (uint16_t)values[i];
    out[2 * i] = (uint8_t)(bits & 0xFF);
    out[2 * i + 1] = (uint8_t)(bits >> 8);
  }
}

// =====================================================================================
// 4. The BMI160 sensor driver (bmi160.h in the kit)
// =====================================================================================
/*
  A small driver for the BMI160 motion sensor. I2C only, no library to install.

  It does the steps you did by hand in stage 3: find the sensor, wake it, choose the ranges,
  and read 12 bytes into six numbers. Register numbers and values come from the Bosch
  BMI160 data sheet.

  The same file sits in every sketch folder that uses the sensor, because the Arduino IDE
  only compiles files that are inside the sketch's own folder. If you change one copy,
  copy it over the others. tools/check.sh tells you when the copies differ.
*/


// Raw counts per unit for the ranges this driver selects.
// The SSwinGG dashboard divides by exactly these two numbers, so the ranges must stay
// at +-8 g and +-2000 degrees per second.
const float BMI160_ACC_LSB_PER_G = 4096.0f;
const float BMI160_GYR_LSB_PER_DPS = 16.4f;

class BMI160 {
 public:
  enum Status : uint8_t { OK = 0, NOT_FOUND, WRONG_CHIP, NOT_AWAKE, CONFIG_FAILED };

  // One reading, in raw counts.
  struct Sample {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
  };

  // Finds the sensor at 0x68 or 0x69, resets it, wakes it and sets the ranges.
  // Call Wire.begin(SDA, SCL) before this.
  Status begin(TwoWire &wire = Wire) {
    _wire = &wire;
    _address = 0;
    _chipId = 0;

    for (uint8_t candidate : {(uint8_t)0x68, (uint8_t)0x69}) {
      _wire->beginTransmission(candidate);
      if (_wire->endTransmission() == 0) {
        _address = candidate;
        break;
      }
    }
    if (_address == 0) {
      return NOT_FOUND;
    }
    if (!readRegisters(REG_CHIP_ID, &_chipId, 1) || _chipId != CHIP_ID) {
      return WRONG_CHIP;
    }

    // Start from a known state. After a reset both sensors are asleep.
    writeRegister(REG_CMD, CMD_SOFT_RESET);
    delay(100);
    writeRegister(REG_CMD, CMD_ACC_NORMAL);
    delay(10);   // the accelerometer needs about 4 ms to start
    writeRegister(REG_CMD, CMD_GYR_NORMAL);
    delay(100);  // the gyroscope needs about 80 ms to start

    if ((powerStatus() & 0x3C) != 0x14) {  // accelerometer and gyroscope both "normal"
      return NOT_AWAKE;
    }

    writeRegister(REG_ACC_RANGE, ACC_RANGE_8G);
    writeRegister(REG_GYR_RANGE, GYR_RANGE_2000DPS);
    writeRegister(REG_ACC_CONF, CONF_100HZ_NORMAL);
    writeRegister(REG_GYR_CONF, CONF_100HZ_NORMAL);
    delay(10);

    uint8_t accRange = 0, gyrRange = 0xFF, accConf = 0, gyrConf = 0;
    bool readBack = readRegisters(REG_ACC_RANGE, &accRange, 1) && readRegisters(REG_GYR_RANGE, &gyrRange, 1)
                    && readRegisters(REG_ACC_CONF, &accConf, 1) && readRegisters(REG_GYR_CONF, &gyrConf, 1);
    if (!readBack || (accRange & 0x0F) != ACC_RANGE_8G || (gyrRange & 0x07) != GYR_RANGE_2000DPS
        || accConf != CONF_100HZ_NORMAL || gyrConf != CONF_100HZ_NORMAL) {
      return CONFIG_FAILED;
    }
    return OK;
  }

  // Reads acceleration and rotation in one go. Returns false when the sensor did not answer.
  bool read(Sample &out) {
    uint8_t raw[12];
    if (!readRegisters(REG_GYR_DATA, raw, sizeof(raw))) {
      return false;
    }
    out.gx = (int16_t)(raw[0] | (raw[1] << 8));
    out.gy = (int16_t)(raw[2] | (raw[3] << 8));
    out.gz = (int16_t)(raw[4] | (raw[5] << 8));
    out.ax = (int16_t)(raw[6] | (raw[7] << 8));
    out.ay = (int16_t)(raw[8] | (raw[9] << 8));
    out.az = (int16_t)(raw[10] | (raw[11] << 8));
    return true;
  }

  // Raw temperature. Use toCelsius() to convert.
  bool readTemperature(int16_t &raw) {
    uint8_t bytes[2];
    if (!readRegisters(REG_TEMPERATURE, bytes, 2)) {
      return false;
    }
    raw = (int16_t)(bytes[0] | (bytes[1] << 8));
    return true;
  }

  static float toCelsius(int16_t raw) {
    return 23.0f + raw / 512.0f;
  }

  uint8_t address() const { return _address; }
  uint8_t chipId() const { return _chipId; }

  // Power status register. 0x14 means accelerometer and gyroscope are both running.
  uint8_t powerStatus() {
    uint8_t value = 0;
    readRegisters(REG_PMU_STATUS, &value, 1);
    return value;
  }

  static const char *describe(Status status) {
    switch (status) {
      case OK:            return "ok";
      case NOT_FOUND:     return "no device answered at 0x68 or 0x69 (wiring, power, or CS pin)";
      case WRONG_CHIP:    return "a device answered but its chip id is not 0xD1, so it is not a BMI160";
      case NOT_AWAKE:     return "the sensor did not switch to normal power mode";
      case CONFIG_FAILED: return "the sensor did not keep the range settings";
    }
    return "unknown";
  }

 private:
  static const uint8_t CHIP_ID = 0xD1;

  static const uint8_t REG_CHIP_ID = 0x00;
  static const uint8_t REG_PMU_STATUS = 0x03;
  static const uint8_t REG_GYR_DATA = 0x0C;     // 6 bytes of rotation, then 6 bytes of acceleration
  static const uint8_t REG_TEMPERATURE = 0x20;
  static const uint8_t REG_ACC_CONF = 0x40;
  static const uint8_t REG_ACC_RANGE = 0x41;
  static const uint8_t REG_GYR_CONF = 0x42;
  static const uint8_t REG_GYR_RANGE = 0x43;
  static const uint8_t REG_CMD = 0x7E;

  static const uint8_t CMD_ACC_NORMAL = 0x11;
  static const uint8_t CMD_GYR_NORMAL = 0x15;
  static const uint8_t CMD_SOFT_RESET = 0xB6;

  static const uint8_t ACC_RANGE_8G = 0x08;        // 4096 counts per g
  static const uint8_t GYR_RANGE_2000DPS = 0x00;   // 16.4 counts per degree per second
  static const uint8_t CONF_100HZ_NORMAL = 0x28;   // 100 readings per second, normal filter

  TwoWire *_wire = nullptr;
  uint8_t _address = 0;
  uint8_t _chipId = 0;

  bool writeRegister(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->write(value);
    return _wire->endTransmission() == 0;
  }

  bool readRegisters(uint8_t reg, uint8_t *buffer, int count) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) {
      return false;
    }
    if (_wire->requestFrom((int)_address, count) != count) {
      return false;
    }
    for (int i = 0; i < count; i++) {
      buffer[i] = _wire->read();
    }
    return true;
  }
};

// =====================================================================================
// 5. The firmware (08_sswingg_bat.ino in the kit)
// =====================================================================================
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
