/*
  Prints test cases for the two files that have no Arduino code in them, swing_packet.h and
  battery.h. tools/check.mjs builds this for the computer, runs it, and compares every line
  with its own, separately written, idea of the right answer.
*/
#include <cstdio>

#include "battery.h"
#include "swing_packet.h"

static void packetCase(const int16_t accel[3], const int16_t gyro[3], const int32_t bias[3],
                       const uint8_t source[3], const int8_t sign[3], int16_t temperature) {
  uint8_t out[SWING_PACKET_SIZE];
  swingBuildPacket(accel, gyro, bias, source, sign, temperature, out);
  printf("packet %d %d %d  %d %d %d  %d %d %d  %d %d %d  %d %d %d  %d  ", accel[0], accel[1], accel[2],
         gyro[0], gyro[1], gyro[2], (int)bias[0], (int)bias[1], (int)bias[2], source[0], source[1], source[2],
         sign[0], sign[1], sign[2], temperature);
  for (size_t i = 0; i < SWING_PACKET_SIZE; i++) {
    printf("%02x", out[i]);
  }
  printf("\n");
}

int main() {
  const uint8_t same[3] = {0, 1, 2}, turned[3] = {2, 0, 1}, swapped[3] = {1, 0, 2};
  const int8_t plus[3] = {1, 1, 1}, flipped[3] = {1, -1, -1}, allMinus[3] = {-1, -1, -1};
  const int32_t noBias[3] = {0, 0, 0}, someBias[3] = {5, -8, 2}, bigBias[3] = {-300, 300, 1};

  const int16_t rows[][6] = {
      {48, -82, 4114, 4, -9, 3},                        // lying still
      {-4096, 0, 0, 16400, -16400, 164},                // -1 g on x, 1000 degrees per second
      {32767, -32768, 1, 32767, -32768, -1},            // the limits of the sensor
      {1234, -2345, 3456, -4567, 5678, -6789},
      {0, 0, 0, 0, 0, 0},
  };
  for (const auto &row : rows) {
    const int16_t accel[3] = {row[0], row[1], row[2]};
    const int16_t gyro[3] = {row[3], row[4], row[5]};
    packetCase(accel, gyro, noBias, same, plus, 0);
    packetCase(accel, gyro, someBias, same, plus, 4096);
    packetCase(accel, gyro, someBias, turned, flipped, -512);
    packetCase(accel, gyro, bigBias, swapped, allMinus, 32767);   // bias and sign push values past the limits
  }

  for (int millivolts = 3000; millivolts <= 4400; millivolts += 10) {
    printf("battery %d %d %d\n", millivolts, batteryPercent(millivolts), (int)batteryState(millivolts, 3600, 3450));
  }
  printf("battery 0 %d %d\n", batteryPercent(0), (int)batteryState(0, 3600, 3450));
  printf("battery 999 %d %d\n", batteryPercent(999), (int)batteryState(999, 3600, 3450));
  printf("pin 2100 %d\n", batteryMillivoltsFromPin(2100, 2.0f, 1.000f));
  printf("pin 1850 %d\n", batteryMillivoltsFromPin(1850, 2.0f, 1.020f));
  return 0;
}
