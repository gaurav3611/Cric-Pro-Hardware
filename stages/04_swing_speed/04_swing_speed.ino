/*
  Stage 4 - From sensor numbers to a swing

  Hardware: the same as stages 2 and 3. Still on the USB cable.

  What it does
    1. measures what the gyroscope reports while the board is still, and removes it from every reading
    2. turns rotation into bat speed with the same formula the SSwinGG dashboard uses
    3. notices when a swing starts and ends, and prints one line for each swing

  What you learn
    - why a "still position" has to be measured (a gyroscope at rest never reads exactly zero)
    - how degrees per second become kilometres per hour
    - what the dashboard will later compute from the same numbers

  Try it: hold the board like the handle of a bat and swing your arm.
  Send r in the Serial Monitor to measure the still position again.

  The dashboard does not find swings by itself. There you press START SHOT and STOP.
  The swing finder in this sketch is only here so you can test without the dashboard.
*/

#include <Wire.h>

const int SDA_PIN = 3;
const int SCL_PIN = 4;

const float COUNTS_PER_G = 4096.0f;   // range +-8 g
const float COUNTS_PER_DPS = 16.4f;   // range +-2000 degrees per second

// ---- The dashboard's numbers. Keep them equal to frontend/dashboard.html. ----
const float BAT_LENGTH_M = 0.85f;        // from the wrists to the sweet spot of the bat
const float DEAD_ZONE_DPS = 5.0f;        // slower rotation than this counts as standing still
const float MAX_SPEED_KMH = 180.0f;

// ---- The swing finder of this sketch. Change these and see what happens. ----
const float SWING_START_KMH = 15.0f;     // faster than this: a swing has started
const float SWING_END_KMH = 8.0f;        // slower than this for a while: the swing is over
const uint32_t SWING_QUIET_MS = 200;     // "a while"

uint8_t sensorAddress = 0;
float gyroAtRest[3] = {0, 0, 0};

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t *buffer, int count) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((int)sensorAddress, count) != count) {
    return false;
  }
  for (int i = 0; i < count; i++) {
    buffer[i] = Wire.read();
  }
  return true;
}

// The same steps as stage 3, without the commentary.
bool startSensor() {
  sensorAddress = 0;
  for (uint8_t address : {(uint8_t)0x68, (uint8_t)0x69}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      sensorAddress = address;
      break;
    }
  }
  uint8_t chipId = 0;
  if (sensorAddress == 0 || !readRegisters(0x00, &chipId, 1) || chipId != 0xD1) {
    return false;
  }
  writeRegister(0x7E, 0xB6);  // reset
  delay(100);
  writeRegister(0x7E, 0x11);  // accelerometer on
  delay(10);
  writeRegister(0x7E, 0x15);  // gyroscope on
  delay(100);
  writeRegister(0x41, 0x08);  // +-8 g
  writeRegister(0x43, 0x00);  // +-2000 degrees per second
  writeRegister(0x40, 0x28);  // 100 readings a second
  writeRegister(0x42, 0x28);
  delay(10);
  uint8_t power = 0;
  readRegisters(0x03, &power, 1);
  return (power & 0x3C) == 0x14;
}

bool readSensor(float accel[3], float gyro[3]) {
  uint8_t raw[12];
  if (!readRegisters(0x0C, raw, 12)) {
    return false;
  }
  for (int i = 0; i < 3; i++) {
    gyro[i] = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8)) / COUNTS_PER_DPS;
    accel[i] = (int16_t)(raw[6 + 2 * i] | (raw[7 + 2 * i] << 8)) / COUNTS_PER_G;
  }
  return true;
}

/*
  A gyroscope that is not moving still reports a small rotation, for example +0.3 degrees
  per second. It is different for every chip and drifts with temperature. We average it
  for one second and subtract it from every later reading.
*/
void measureStillPosition() {
  Serial.println("Keep the board still for one second...");
  double sum[3] = {0, 0, 0};
  int good = 0;
  for (int i = 0; i < 100; i++) {
    float accel[3], gyro[3];
    if (readSensor(accel, gyro)) {
      for (int k = 0; k < 3; k++) {
        sum[k] += gyro[k];
      }
      good++;
    }
    delay(10);
  }
  for (int k = 0; k < 3; k++) {
    gyroAtRest[k] = good > 0 ? (float)(sum[k] / good) : 0;
  }
  Serial.printf("Rotation at rest: x %+.2f  y %+.2f  z %+.2f deg/s. This is now subtracted.\n",
                gyroAtRest[0], gyroAtRest[1], gyroAtRest[2]);
}

/*
  Bat speed, the way the dashboard works it out.

    1. total rotation = sqrt(gx*gx + gy*gy + gz*gz)       degrees per second
    2. below 5 it counts as zero                           the dead zone
    3. times pi/180                                        now radians per second
    4. times 0.85                                          metres per second at the sweet spot,
                                                           because a point 0.85 m from the pivot
                                                           moves 0.85 m for every radian
    5. times 3.6                                           kilometres per hour
    6. never more than 180
*/
float batSpeedKmh(const float gyro[3]) {
  float rotation = sqrtf(gyro[0] * gyro[0] + gyro[1] * gyro[1] + gyro[2] * gyro[2]);
  if (rotation < DEAD_ZONE_DPS) {
    rotation = 0;
  }
  float speed = rotation * (PI / 180.0f) * BAT_LENGTH_M * 3.6f;
  return speed > MAX_SPEED_KMH ? MAX_SPEED_KMH : speed;
}

// The dashboard's rule for the side of a shot, from rotation around z.
const char *directionOf(float gz) {
  if (gz > 1.0f) {
    return "leg side";
  }
  if (gz < -1.0f) {
    return "off side";
  }
  return "straight";
}

// ---- state of the swing finder ----
bool inSwing = false;
int swingNumber = 0;
uint32_t swingStartedAt = 0, lastFastAt = 0, lastLineAt = 0;
float peakSpeed = 0, peakAcceleration = 0, lastGz = 0;
float peakAxis[3] = {0, 0, 0};

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }
  Wire.begin(SDA_PIN, SCL_PIN, 400000);  // 400000 is "fast mode". The sensor supports it.
  Wire.setTimeOut(50);

  Serial.println();
  Serial.println("===== Stage 4: swing speed =====");
  while (!startSensor()) {
    Serial.println("The sensor did not start. Go back to stage 2. Trying again in 3 seconds.");
    delay(3000);
  }
  measureStillPosition();
  Serial.println();
  Serial.println("Swing the board. One line is printed for each swing. Send r to measure the still position again.");
}

void loop() {
  if (Serial.available() && Serial.read() == 'r') {
    measureStillPosition();
  }

  float accel[3], gyro[3];
  if (!readSensor(accel, gyro)) {
    Serial.println("Reading failed. A wire may be loose.");
    delay(500);
    return;
  }
  for (int k = 0; k < 3; k++) {
    gyro[k] -= gyroAtRest[k];
  }

  float speed = batSpeedKmh(gyro);
  float acceleration = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
  uint32_t now = millis();

  if (!inSwing && speed > SWING_START_KMH) {
    inSwing = true;
    swingStartedAt = now;
    peakSpeed = 0;
    peakAcceleration = 0;
    peakAxis[0] = peakAxis[1] = peakAxis[2] = 0;
  }
  if (inSwing) {
    if (speed > peakSpeed) {
      peakSpeed = speed;
    }
    if (acceleration > peakAcceleration) {
      peakAcceleration = acceleration;
    }
    for (int k = 0; k < 3; k++) {
      peakAxis[k] = max(peakAxis[k], fabsf(gyro[k]));
    }
    if (speed > SWING_END_KMH) {
      lastFastAt = now;
      lastGz = gyro[2];
    } else if (now - lastFastAt > SWING_QUIET_MS) {
      inSwing = false;
      swingNumber++;
      int main = peakAxis[0] >= peakAxis[1] && peakAxis[0] >= peakAxis[2] ? 0 : (peakAxis[1] >= peakAxis[2] ? 1 : 2);
      Serial.printf("Swing %d:  peak speed %5.1f km/h   peak acceleration %4.1f g   lasted %.2f s   mostly around %c   ended turning %s\n",
                    swingNumber, peakSpeed, peakAcceleration, (lastFastAt - swingStartedAt) / 1000.0f, "xyz"[main], directionOf(lastGz));
      if (peakAxis[main] > 1990.0f) {
        Serial.println("          The gyroscope hit its limit of 2000 degrees per second. The real swing was faster.");
      }
    }
  }

  // A line every half second while nothing happens, so you can see it is alive.
  if (!inSwing && now - lastLineAt >= 500) {
    lastLineAt = now;
    Serial.printf("  now %5.1f km/h   %4.2f g\n", speed, acceleration);
  }
  delay(10);  // about 100 readings a second
}
