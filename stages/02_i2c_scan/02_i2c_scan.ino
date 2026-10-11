/*
  Stage 2 - Can the board see the sensor?

  Hardware: stage 1, plus the GY-BMI160 on five wires.

      ESP32-C3      GY-BMI160
      3V3      ->   VIN      power
      GND      ->   GND      ground
      GPIO4    ->   SCL      I2C clock
      GPIO3    ->   SDA      I2C data
      GND      ->   SAO      fixes the sensor's address at 0x68

  What it does
    Every 3 seconds it calls every possible I2C address, 1 to 126, and lists who answers.
    Then it asks the device at 0x68 or 0x69 for its chip id.

  What you learn
    - I2C is two wires shared by every device: a clock (SCL) and data (SDA)
    - every device has an address, and only the device with that address answers
    - a sensor is a set of numbered boxes called registers. Register 0x00 of a BMI160 always holds 0xD1.

  Nothing else in this kit can work until this stage says PASS.
*/

#include <Wire.h>

// I2C pins. These must match your wiring.
const int SDA_PIN = 3;
const int SCL_PIN = 4;

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  // 100000 is the standard I2C speed: 100 thousand clock ticks a second. Slow and forgiving.
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);

  Serial.println();
  Serial.println("===== Stage 2: I2C scan =====");
  Serial.printf("SDA = GPIO%d, SCL = GPIO%d\n", SDA_PIN, SCL_PIN);
}

// Reads one register of one device. Returns false when the device does not answer.
bool readRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);       // START, then the address with "I want to write"
  Wire.write(reg);                       // which register we want
  if (Wire.endTransmission(false) != 0)  // false = keep the bus, a read follows
    return false;
  if (Wire.requestFrom((int)address, 1) != 1)  // the address again, with "I want to read", then 1 byte
    return false;
  value = Wire.read();
  return true;
}

void loop() {
  int found = 0;
  bool bmi160 = false;

  Serial.println();
  Serial.println("Scanning addresses 0x01 to 0x7E...");
  for (uint8_t address = 1; address < 127; address++) {
    // Calling an address and sending nothing is enough: a device that is there says "yes" (ACK).
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
      continue;  // nobody home at this address
    }
    found++;
    Serial.printf("  0x%02X answered", address);

    if (address == 0x68 || address == 0x69) {
      uint8_t id = 0;
      if (readRegister(address, 0x00, id) && id == 0xD1) {
        Serial.print("   chip id 0xD1: this is the BMI160");
        bmi160 = true;
      } else if (readRegister(address, 0x75, id) && (id == 0x68 || id == 0x70 || id == 0x71 || id == 0x73)) {
        Serial.printf("   id 0x%02X: this looks like an MPU6050 or MPU6500, not a BMI160", id);
      } else {
        Serial.print("   but it is not a BMI160");
      }
    }
    Serial.println();
  }

  if (bmi160) {
    Serial.println("RESULT: PASS. The sensor is wired correctly. Go on to stage 3.");
  } else if (found == 0) {
    Serial.println("RESULT: FAIL. Nobody answered. Check, in this order:");
    Serial.println("  1. Sensor VIN goes to 3V3 and sensor GND goes to GND.");
    Serial.printf("  2. Sensor SDA goes to GPIO%d and SCL to GPIO%d. They are easy to swap.\n", SDA_PIN, SCL_PIN);
    Serial.println("  3. Every solder joint is shiny, and no two neighbouring pads touch.");
    Serial.println("  4. Still nothing? Connect the sensor's CS pin to 3V3. That forces it into I2C mode.");
  } else {
    Serial.println("RESULT: FAIL. Something answered, but there is no BMI160 at 0x68 or 0x69.");
  }
  delay(3000);
}
