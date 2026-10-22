/*
  Battery arithmetic for one LiPo cell. No Arduino code in it, so it can be tested on a computer.

  A LiPo cell is 4.20 V when full and should not be used below about 3.3 V. The voltage does
  not fall in a straight line, so the charge is looked up in a small table. The result is a
  rough guide, not a fuel gauge: the voltage dips while the radio sends and rises again
  afterwards, and it differs from cell to cell.
*/
#pragma once

#include <stdint.h>

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

enum BatteryState {
  BATTERY_NOT_SENSED,  // under 1 V: the switch is off, or the two resistors are not fitted
  BATTERY_EMPTY,       // stop now and charge
  BATTERY_LOW,         // charge soon
  BATTERY_OK,
};

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
