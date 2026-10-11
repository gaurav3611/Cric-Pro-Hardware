/*
  The I2C bus of the simulator, with one device on it: a pretend BMI160 at address 0x68.

  The pretend sensor keeps the registers this kit uses, obeys the reset and wake-up
  commands, and reports motion that the scenario describes. It also keeps a log of every
  reading that was taken, which the tests compare with what came out over Bluetooth.
*/
#pragma once

#include "Arduino.h"

namespace sim {
// Raw counts the pretend sensor reports right now: gyro x, y, z then accel x, y, z.
void sensorCounts(int16_t out[6]);
bool sensorAnswers();   // false while the scenario says the sensor is "unplugged"
void logSensorRead(const int16_t counts[6]);
}  // namespace sim

class TwoWire {
 public:
  void begin(int, int, uint32_t = 100000) {}
  void setTimeOut(int) {}

  void beginTransmission(uint8_t address) {
    _address = address;
    _txCount = 0;
  }
  void write(uint8_t value) {
    if (_txCount < 2) {
      _tx[_txCount] = value;
    }
    _txCount++;
  }
  // Returns 0 when the device said "yes", like the real one.
  int endTransmission(bool = true) {
    if (!present(_address)) {
      return 2;
    }
    if (_txCount >= 1) {
      _pointer = _tx[0];
    }
    if (_txCount >= 2) {
      writeRegister(_tx[0], _tx[1]);
    }
    return 0;
  }
  int requestFrom(int address, int count) {
    _rxCount = 0;
    _rxNext = 0;
    if (!present((uint8_t)address) || count > (int)sizeof(_rx)) {
      return 0;
    }
    for (int i = 0; i < count; i++) {
      _rx[i] = readRegister((uint8_t)(_pointer + i));
    }
    if (_pointer == 0x0C && count == 12) {
      sim::logSensorRead(_latched);
    }
    _rxCount = count;
    return count;
  }
  int read() { return _rxNext < _rxCount ? _rx[_rxNext++] : -1; }

 private:
  uint8_t _address = 0, _tx[2] = {0, 0}, _rx[32], _pointer = 0;
  int _txCount = 0, _rxCount = 0, _rxNext = 0;
  uint8_t _pmu = 0, _accRange = 0x03, _gyrRange = 0x00, _accConf = 0x28, _gyrConf = 0x28;
  int16_t _latched[6] = {0, 0, 0, 0, 0, 0};

  bool present(uint8_t address) { return address == 0x68 && sim::sensorAnswers(); }

  void writeRegister(uint8_t reg, uint8_t value) {
    switch (reg) {
      case 0x7E:
        if (value == 0xB6) { _pmu = 0; _accRange = 0x03; _gyrRange = 0x00; }   // reset: everything asleep, default ranges
        if (value == 0x11) _pmu = (uint8_t)((_pmu & ~0x30) | 0x10);            // accelerometer to normal mode
        if (value == 0x15) _pmu = (uint8_t)((_pmu & ~0x0C) | 0x04);            // gyroscope to normal mode
        break;
      case 0x40: _accConf = value; break;
      case 0x41: _accRange = value; break;
      case 0x42: _gyrConf = value; break;
      case 0x43: _gyrRange = value; break;
    }
  }

  uint8_t readRegister(uint8_t reg) {
    if (reg == 0x00) return 0xD1;
    if (reg == 0x03) return _pmu;
    if (reg == 0x40) return _accConf;
    if (reg == 0x41) return _accRange;
    if (reg == 0x42) return _gyrConf;
    if (reg == 0x43) return _gyrRange;
    if (reg == 0x20) return 0x00;   // temperature: raw 0x1000 = 4096 counts = 23 + 8 = 31 C
    if (reg == 0x21) return 0x10;
    if (reg >= 0x0C && reg <= 0x17) {
      if (reg == 0x0C) {            // a new reading is taken when the first data byte is read
        sim::sensorCounts(_latched);
        // A sleeping part reports zeros, as the real chip does.
        if ((_pmu & 0x0C) != 0x04) _latched[0] = _latched[1] = _latched[2] = 0;
        if ((_pmu & 0x30) != 0x10) _latched[3] = _latched[4] = _latched[5] = 0;
      }
      int index = (reg - 0x0C) / 2;
      uint16_t bits = (uint16_t)_latched[index];
      return (reg - 0x0C) % 2 == 0 ? (uint8_t)(bits & 0xFF) : (uint8_t)(bits >> 8);
    }
    return 0;
  }
};
extern TwoWire Wire;
