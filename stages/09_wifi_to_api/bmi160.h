/*
  A small driver for the BMI160 motion sensor. I2C only, no library to install.

  It does the steps you did by hand in stage 3: find the sensor, wake it, choose the ranges,
  and read 12 bytes into six numbers. Register numbers and values come from the Bosch
  BMI160 data sheet.

  The same file sits in every sketch folder that uses the sensor, because the Arduino IDE
  only compiles files that are inside the sketch's own folder. If you change one copy,
  copy it over the others. tools/check.sh tells you when the copies differ.
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>

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
