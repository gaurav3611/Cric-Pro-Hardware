/*
  Every setting of the bat firmware, in one place.

  Change things here, not in the other files. docs/make-changes.pdf explains what each
  change does to the rest of the system.
*/
#pragma once

#include <stdint.h>

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
// with "SSwinGG". With several bats, give each its own ending, for example SSwinGG-Bat2.
// ----------------------------------------------------------------------------------
#define DEVICE_NAME "SSwinGG-Bat"

// ----------------------------------------------------------------------------------
// Fixed by the dashboard. Changing any of these breaks the connection to it, unless the
// web app is changed in the same way (its file sensor-packet.ts).
// ----------------------------------------------------------------------------------
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SENSOR_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
const uint32_t SAMPLE_PERIOD_MS = 20;  // 50 readings a second
