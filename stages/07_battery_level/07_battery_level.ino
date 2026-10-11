/*
  Stage 7 (optional) - Measure the battery

  Hardware: stage 6, plus two 100 kilohm resistors and one 100 nF capacitor.

      switch output (battery +) ---[ R1 100k ]---+---[ R2 100k ]--- GND
                                                 |
                                                 +--- GPIO0
                                                 |
                                             [ C1 100 nF ]
                                                 |
                                                GND

  What it does
    Once a second it measures the voltage on GPIO0, works out the battery voltage and
    prints how full the cell roughly is.

  What you learn
    - an ADC (analog to digital converter) turns a voltage into a number
    - the pin must stay under about 2.5 V, so two equal resistors halve the battery voltage
    - why a measurement needs calibrating against a multimeter

  Skip this stage if you do not need a battery reading. The bat works without it.

  Calibrate: measure the battery with a multimeter, compare with what this sketch prints,
  and set CALIBRATION = multimeter volts / printed volts. Use the same number in stage 8.
*/

#include "battery.h"

const int BATTERY_PIN = 0;          // GPIO0 can measure a voltage. So can GPIO1 to GPIO4.
const float DIVIDER_RATIO = 2.0f;   // two equal resistors halve the voltage
const float CALIBRATION = 1.000f;   // see the note above

const int LOW_MILLIVOLTS = 3600;    // "charge soon"
const int EMPTY_MILLIVOLTS = 3450;  // "stop now". Just above where this circuit stops working.

// Averages many quick measurements. One alone is noisy, more so while the radio is on.
int readPinMillivolts() {
  long sum = 0;
  const int samples = 32;
  for (int i = 0; i < samples; i++) {
    sum += analogReadMilliVolts(BATTERY_PIN);  // millivolts at the pin, already corrected by the chip's factory data
    delay(1);
  }
  return (int)(sum / samples);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }
  // The widest measuring range of the pin. On the ESP32-C3 that reaches about 2.5 V.
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  Serial.println();
  Serial.println("===== Stage 7: battery level =====");
  Serial.printf("Measuring on GPIO%d. Divider ratio %.1f, calibration %.3f\n", BATTERY_PIN, DIVIDER_RATIO, CALIBRATION);
  Serial.println("Switch the battery on. Compare the volts below with a multimeter on the battery.");
  Serial.println();
}

void loop() {
  int pin = readPinMillivolts();
  int battery = batteryMillivoltsFromPin(pin, DIVIDER_RATIO, CALIBRATION);

  switch (batteryState(battery, LOW_MILLIVOLTS, EMPTY_MILLIVOLTS)) {
    case BATTERY_NOT_SENSED:
      Serial.printf("pin %4d mV   no battery seen. Is the switch on, and are R1 and R2 fitted?\n", pin);
      break;
    case BATTERY_EMPTY:
      Serial.printf("pin %4d mV   battery %.2f V   %3d%%   EMPTY: switch off and charge\n", pin, battery / 1000.0f, batteryPercent(battery));
      break;
    case BATTERY_LOW:
      Serial.printf("pin %4d mV   battery %.2f V   %3d%%   low: charge soon\n", pin, battery / 1000.0f, batteryPercent(battery));
      break;
    case BATTERY_OK:
      Serial.printf("pin %4d mV   battery %.2f V   %3d%%   ok\n", pin, battery / 1000.0f, batteryPercent(battery));
      break;
  }
  delay(1000);
}
