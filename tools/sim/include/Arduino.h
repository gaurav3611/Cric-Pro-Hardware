/*
  A stand-in for the Arduino core, so the sketches of this kit can run on a computer.

  It is not an emulator of the chip. It gives the sketches a clock, a Serial port that
  prints to the terminal, pins, a pretend BMI160 on the I2C wires (Wire.h), a pretend
  Bluetooth radio that writes every packet to a file (BLEDevice.h), and a pretend battery.
  What the pretend world does and when is set by a scenario, see sim_main.cpp.

  Time only moves when the sketch calls delay(), so a run is exactly repeatable.
*/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using std::max;
using std::min;

#define PI 3.1415926535897932384626433832795
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define ADC_11db 3
#define ARDUINO_USB_CDC_ON_BOOT 1

namespace sim {
extern uint32_t nowMs;             // the pretend clock
void tick(uint32_t ms);            // moves the clock and runs scenario events that are due
bool buttonPressed();              // BOOT button, from the scenario
int batteryPinMillivolts();        // what the ADC pin sees, from the scenario
int nextSerialChar();              // -1 when nothing was "typed"
bool serialHasChar();
void ledChanged(int level);
}  // namespace sim

inline uint32_t millis() { return sim::nowMs; }
inline void delay(uint32_t ms) { sim::tick(ms); }

inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int level) { if (pin == 8) sim::ledChanged(level); }
inline int digitalRead(int pin) { return (pin == 9 && sim::buttonPressed()) ? LOW : HIGH; }
inline void analogSetPinAttenuation(int, int) {}
inline uint32_t analogReadMilliVolts(int) { return (uint32_t)sim::batteryPinMillivolts(); }

class SerialPort {
 public:
  void begin(long) {}
  void setTxTimeoutMs(int) {}
  explicit operator bool() const { return true; }
  int available() { return sim::serialHasChar() ? 1 : 0; }
  int read() { return sim::nextSerialChar(); }
  void printf(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
  void print(const char *text) { fputs(text, stdout); }
  void println(const char *text) { puts(text); }
  void println() { putchar('\n'); }
};
extern SerialPort Serial;

class EspInfo {
 public:
  const char *getChipModel() { return "ESP32-C3 (simulated)"; }
  int getChipRevision() { return 4; }
  int getCpuFreqMHz() { return 160; }
  uint32_t getFlashChipSize() { return 4u * 1024 * 1024; }
};
extern EspInfo ESP;
