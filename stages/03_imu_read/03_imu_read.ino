/*
  Stage 3 - Talk to the sensor and read real numbers

  Hardware: the same as stage 2. ESP32-C3 and GY-BMI160 on five wires.

  What it does
    1. asks the sensor who it is
    2. wakes it up and chooses its measuring ranges, and prints every byte it writes
    3. takes one reading and shows how 12 raw bytes become 6 numbers with units
    4. checks the numbers while the board lies still: PASS or FAIL
    5. prints live readings 10 times a second

  What you learn
    - a sensor is configured by WRITING to registers and measured by READING registers
    - one measurement is a 16-bit number sent as two bytes, low byte first
    - "counts" become g and degrees per second by dividing by a fixed scale

  Type a letter in the Serial Monitor and press Enter:
    b   show one reading byte by byte again
    r   repeat the still check
    p   switch to numbers for Tools > Serial Plotter, and back

  This sketch has no library and no extra file on purpose, so every step is in front of you.
  Later stages use bmi160.h, which holds the same steps in a tidier form.
*/

#include <Wire.h>

const int SDA_PIN = 3;
const int SCL_PIN = 4;

// ---- The BMI160 registers this kit uses. Numbers from the Bosch BMI160 data sheet. ----
const uint8_t REG_CHIP_ID = 0x00;     // always holds 0xD1
const uint8_t REG_PMU_STATUS = 0x03;  // which parts of the sensor are awake
const uint8_t REG_DATA = 0x0C;        // 12 bytes: rotation x, y, z, then acceleration x, y, z
const uint8_t REG_ACC_CONF = 0x40;    // how often acceleration is measured
const uint8_t REG_ACC_RANGE = 0x41;   // the largest acceleration it can report
const uint8_t REG_GYR_CONF = 0x42;    // how often rotation is measured
const uint8_t REG_GYR_RANGE = 0x43;   // the fastest rotation it can report
const uint8_t REG_CMD = 0x7E;         // commands: reset, wake up

// ---- Scale. A reading is a number from -32768 to +32767, called counts. ----
// With the range set to +-8 g, 32768 counts mean 8 g, so 1 g is 4096 counts.
// With the range set to +-2000 degrees per second, 1 degree per second is 16.4 counts.
// The SwingLab dashboard divides by exactly these two numbers. If you change a range,
// the dashboard shows wrong values. See docs/make-changes.md.
const float COUNTS_PER_G = 4096.0f;
const float COUNTS_PER_DPS = 16.4f;

uint8_t sensorAddress = 0;  // 0x68 or 0x69, found at start
bool plotterFormat = false;

// ---------------------------------------------------------------------------------------
// The two I2C conversations everything else is built from
// ---------------------------------------------------------------------------------------

// WRITE: "device, put this value in this register".
// On the wires: START, address + write, register number, value, STOP.
bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;  // 0 means the device said "got it" (ACK) every time
}

// READ: "device, send me `count` bytes starting at this register".
// On the wires: START, address + write, register number,
//               START again, address + read, the bytes, STOP.
// The sensor moves to the next register by itself after every byte.
bool readRegisters(uint8_t reg, uint8_t *buffer, int count) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {  // false = no STOP yet, the read follows
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

// Two bytes, low byte first, make one signed 16-bit number.
int16_t toNumber(uint8_t lowByte, uint8_t highByte) {
  return (int16_t)(lowByte | (highByte << 8));
}

// ---------------------------------------------------------------------------------------
// Step 1 and 2: find the sensor, wake it, choose the ranges
// ---------------------------------------------------------------------------------------

void say(uint8_t value, uint8_t reg, const char *meaning) {
  bool ok = writeRegister(reg, value);
  Serial.printf("  wrote 0x%02X to register 0x%02X   %s%s\n", value, reg, meaning, ok ? "" : "   <- NO ANSWER");
}

bool startSensor() {
  Serial.println("Step 1: who is there?");
  sensorAddress = 0;
  for (uint8_t address : {(uint8_t)0x68, (uint8_t)0x69}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      sensorAddress = address;
      break;
    }
  }
  if (sensorAddress == 0) {
    Serial.println("  Nobody answered at 0x68 or 0x69. Go back to stage 2.");
    return false;
  }
  Serial.printf("  address 0x%02X answered\n", sensorAddress);

  uint8_t chipId = 0;
  if (!readRegisters(REG_CHIP_ID, &chipId, 1) || chipId != 0xD1) {
    Serial.printf("  register 0x00 (chip id) = 0x%02X. A BMI160 says 0xD1, so this is another sensor.\n", chipId);
    return false;
  }
  Serial.println("  register 0x00 (chip id) = 0xD1   this is a BMI160");

  Serial.println("Step 2: wake it up and choose the ranges");
  say(0xB6, REG_CMD, "reset, so it starts from a known state");
  delay(100);
  say(0x11, REG_CMD, "accelerometer on");
  delay(10);   // the accelerometer needs about 4 ms to start
  say(0x15, REG_CMD, "gyroscope on");
  delay(100);  // the gyroscope needs about 80 ms to start
  say(0x08, REG_ACC_RANGE, "acceleration range +-8 g");
  say(0x00, REG_GYR_RANGE, "rotation range +-2000 degrees per second");
  say(0x28, REG_ACC_CONF, "100 acceleration readings a second");
  say(0x28, REG_GYR_CONF, "100 rotation readings a second");
  delay(10);

  uint8_t power = 0;
  readRegisters(REG_PMU_STATUS, &power, 1);
  if ((power & 0x3C) != 0x14) {
    Serial.printf("  register 0x03 (power status) = 0x%02X. Expected 0x14. The sensor did not wake up.\n", power);
    return false;
  }
  Serial.println("  register 0x03 (power status) = 0x14   accelerometer and gyroscope are both awake");
  return true;
}

// ---------------------------------------------------------------------------------------
// Step 3: one reading, byte by byte
// ---------------------------------------------------------------------------------------

void showBytes() {
  Serial.println("Step 3: one reading, byte by byte");
  uint8_t raw[12];
  if (!readRegisters(REG_DATA, raw, 12)) {
    Serial.println("  The sensor did not answer.");
    return;
  }
  Serial.print("  12 bytes from register 0x0C: ");
  for (int i = 0; i < 12; i++) {
    Serial.printf(" %02X", raw[i]);
  }
  Serial.println();

  const char *names[6] = {"rotation x", "rotation y", "rotation z", "acceleration x", "acceleration y", "acceleration z"};
  for (int i = 0; i < 6; i++) {
    int16_t counts = toNumber(raw[2 * i], raw[2 * i + 1]);
    if (i < 3) {
      Serial.printf("  %-15s bytes %02X %02X  = %6d counts  / 16.4  = %+8.2f deg/s\n",
                    names[i], raw[2 * i], raw[2 * i + 1], counts, counts / COUNTS_PER_DPS);
    } else {
      Serial.printf("  %-15s bytes %02X %02X  = %6d counts  / 4096  = %+8.3f g\n",
                    names[i], raw[2 * i], raw[2 * i + 1], counts, counts / COUNTS_PER_G);
    }
  }
  // The last pair again, to show the byte order with the real numbers of this reading.
  Serial.printf("  Low byte first: bytes %02X %02X mean 0x%02X%02X, which is %d.\n",
                raw[10], raw[11], raw[11], raw[10], toNumber(raw[10], raw[11]));
}

// Reads one sample into real units. accel in g, gyro in degrees per second.
bool readSensor(float accel[3], float gyro[3]) {
  uint8_t raw[12];
  if (!readRegisters(REG_DATA, raw, 12)) {
    return false;
  }
  for (int i = 0; i < 3; i++) {
    gyro[i] = toNumber(raw[2 * i], raw[2 * i + 1]) / COUNTS_PER_DPS;
    accel[i] = toNumber(raw[6 + 2 * i], raw[7 + 2 * i]) / COUNTS_PER_G;
  }
  return true;
}

// ---------------------------------------------------------------------------------------
// Step 4: are the numbers believable?
// ---------------------------------------------------------------------------------------

void printCheck(const char *label, bool pass) {
  Serial.printf("  %-54s %s\n", label, pass ? "PASS" : "FAIL");
}

// Averages 300 readings over 3 seconds and judges them. The board must not move.
bool stillCheck() {
  Serial.println("Step 4: still check. Lay the board flat and do not touch it for 3 seconds...");
  delay(1000);

  const int count = 300;
  double sum[6] = {0, 0, 0, 0, 0, 0};
  double gyroSquares[3] = {0, 0, 0};
  int good = 0;

  for (int i = 0; i < count; i++) {
    float accel[3], gyro[3];
    if (readSensor(accel, gyro)) {
      for (int k = 0; k < 3; k++) {
        sum[k] += accel[k];
        sum[3 + k] += gyro[k];
        gyroSquares[k] += (double)gyro[k] * gyro[k];
      }
      good++;
    }
    delay(10);
  }
  if (good < count / 2) {
    Serial.printf("  Only %d of %d readings arrived. Check the SDA and SCL joints.\n", good, count);
    return false;
  }

  float accel[3], gyro[3], noise = 0;
  for (int k = 0; k < 3; k++) {
    accel[k] = (float)(sum[k] / good);
    double mean = sum[3 + k] / good;
    gyro[k] = (float)mean;
    double variance = gyroSquares[k] / good - mean * mean;
    noise = max(noise, (float)sqrt(max(variance, 0.0)));
  }
  float gravity = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
  float worstOffset = max(fabsf(gyro[0]), max(fabsf(gyro[1]), fabsf(gyro[2])));

  Serial.printf("  acceleration  x %+.3f  y %+.3f  z %+.3f g   total %.3f g\n", accel[0], accel[1], accel[2], gravity);
  Serial.printf("  rotation      x %+.2f  y %+.2f  z %+.2f deg/s   noise %.2f deg/s\n", gyro[0], gyro[1], gyro[2], noise);

  bool readsOk = good == count;
  bool gravityOk = gravity > 0.90f && gravity < 1.10f;  // a still sensor feels gravity and nothing else
  bool offsetOk = worstOffset < 10.0f;                  // a still gyroscope reads close to zero, never exactly zero
  bool noiseOk = noise < 1.0f;
  printCheck("every reading arrived", readsOk);
  printCheck("total acceleration is gravity, 0.90 to 1.10 g", gravityOk);
  printCheck("rotation at rest is small, under 10 deg/s per axis", offsetOk);
  printCheck("rotation is steady, noise under 1 deg/s", noiseOk);

  bool pass = readsOk && gravityOk && offsetOk && noiseOk;
  if (pass) {
    Serial.println("RESULT: PASS. The sensor works. Go on to stage 4.");
  } else {
    Serial.println("RESULT: FAIL. If the board moved, send r to try again. Otherwise see docs/troubleshooting.md.");
  }
  return pass;
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);

  Serial.println();
  Serial.println("===== Stage 3: read the BMI160 =====");
  Serial.printf("SDA = GPIO%d, SCL = GPIO%d\n", SDA_PIN, SCL_PIN);

  while (!startSensor()) {
    Serial.println("Trying again in 3 seconds.");
    Serial.println();
    delay(3000);
  }
  showBytes();
  stillCheck();

  Serial.println();
  Serial.println("Live readings, 10 a second. Tilt the board and turn it by hand.");
  Serial.println("Flat on the table, acceleration z is close to +1 g. Upside down it is close to -1 g.");
  Serial.println("Send b for bytes, r for the still check, p for Serial Plotter format.");
}

void loop() {
  if (Serial.available()) {
    char command = Serial.read();
    if (command == 'b') {
      showBytes();
    } else if (command == 'r') {
      stillCheck();
    } else if (command == 'p') {
      plotterFormat = !plotterFormat;
    }
  }

  float accel[3], gyro[3];
  if (!readSensor(accel, gyro)) {
    Serial.println("Reading failed. A wire may be loose.");
    delay(500);
    return;
  }
  if (plotterFormat) {
    Serial.printf("ax:%.3f,ay:%.3f,az:%.3f,gx:%.1f,gy:%.1f,gz:%.1f\n", accel[0], accel[1], accel[2], gyro[0], gyro[1], gyro[2]);
  } else {
    Serial.printf("acceleration %+6.2f %+6.2f %+6.2f g    rotation %+8.1f %+8.1f %+8.1f deg/s\n",
                  accel[0], accel[1], accel[2], gyro[0], gyro[1], gyro[2]);
  }
  delay(100);
}
