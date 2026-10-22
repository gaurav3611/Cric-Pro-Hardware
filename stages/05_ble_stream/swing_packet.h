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
#pragma once

#include <stddef.h>
#include <stdint.h>

const size_t SWING_PACKET_SIZE = 14;

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
