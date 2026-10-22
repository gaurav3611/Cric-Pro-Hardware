/*
  Stage 5 - Send the readings over Bluetooth

  Hardware: the same as stages 2 to 4. Still on the USB cable.

  What it does
    - turns the board into a Bluetooth Low Energy device called "SwingLab-Bat"
    - 50 times a second it reads the sensor, packs the six numbers into 14 bytes,
      and sends them to whoever is connected

  What you learn
    - advertising: the board shouts its name a few times a second so others can find it
    - connecting: a phone or a browser (the "central") picks the board (the "peripheral")
    - a service is a folder, a characteristic is a value inside it, each named by a long id
    - notify: the board pushes a new value whenever it has one, nobody has to ask
    - the 14-byte packet that the SSwinGG dashboard reads

  Light on the board
    slow blinking   waiting for a connection
    steady on       connected and sending

  Test it with tools/ble_test.html. The README of this stage says how.
*/

#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "bmi160.h"        // the sensor steps of stage 3, in a tidy form
#include "swing_packet.h"  // packs the numbers into 14 bytes

const int SDA_PIN = 3;
const int SCL_PIN = 4;
const int LED_PIN = 8;
const int LED_ON = LOW;

// The dashboard only lists devices whose name starts with "SwingLab".
#define DEVICE_NAME "SwingLab-Bat"

// These two ids are the address of the data inside the device. The dashboard looks for
// exactly these, so they must not change. They are random numbers picked once.
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SENSOR_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

const uint32_t SAMPLE_PERIOD_MS = 20;  // 20 ms between readings = 50 readings a second

BMI160 imu;
BLECharacteristic *sensorCharacteristic = nullptr;

// Set from the Bluetooth code, read in loop(). "volatile" tells the compiler they can
// change at any moment.
volatile bool connected = false;
volatile bool advertiseAgain = false;

int32_t gyroBias[3] = {0, 0, 0};          // rotation at rest, in counts
const uint8_t AXIS_SOURCE[3] = {0, 1, 2};  // no axis swapping in this stage
const int8_t AXIS_SIGN[3] = {+1, +1, +1};
uint8_t packet[SWING_PACKET_SIZE];
uint32_t nextSampleAt = 0, lastReportAt = 0, sentThisSecond = 0, failedReads = 0;

// The Bluetooth code calls these two when somebody connects or leaves.
class ConnectionEvents : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    (void)server;
    connected = true;
  }
  void onDisconnect(BLEServer *server) override {
    (void)server;
    connected = false;
    advertiseAgain = true;  // a device stops advertising once connected, so start again
  }
};

void startBluetooth() {
  BLEDevice::init(DEVICE_NAME);                         // switch the radio on, with our name
  BLEServer *server = BLEDevice::createServer();        // we hold the data, so we are the server
  server->setCallbacks(new ConnectionEvents());

  BLEService *service = server->createService(SERVICE_UUID);   // the folder
  sensorCharacteristic = service->createCharacteristic(        // the value in it
      SENSOR_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);  // may be read, and is pushed
#if defined(CONFIG_BLUEDROID_ENABLED)
  // The small switch a client flips to say "yes, push me the values". Some Bluetooth
  // software needs it added by hand. The one used on the ESP32-C3 adds it by itself.
  sensorCharacteristic->addDescriptor(new BLE2902());
#endif
  memset(packet, 0, sizeof(packet));
  sensorCharacteristic->setValue(packet, SWING_PACKET_SIZE);
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);  // say in the advert which service we offer
  advertising->setScanResponse(true);         // the name travels in a second, on-request advert
  advertising->setMinPreferred(0x06);         // ask the central for a short gap between exchanges
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.printf("Bluetooth is on. Look for \"%s\".\n", DEVICE_NAME);
}

void measureStillPosition() {
  Serial.println("Keep the board still for one second...");
  int64_t sum[3] = {0, 0, 0};
  int good = 0;
  for (int i = 0; i < 100; i++) {
    BMI160::Sample s;
    if (imu.read(s)) {
      sum[0] += s.gx;
      sum[1] += s.gy;
      sum[2] += s.gz;
      good++;
    }
    delay(10);
  }
  for (int k = 0; k < 3; k++) {
    gyroBias[k] = good > 0 ? (int32_t)(sum[k] / good) : 0;
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // do not wait for a computer that is not listening
#endif
  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }
  pinMode(LED_PIN, OUTPUT);

  Serial.println();
  Serial.println("===== Stage 5: Bluetooth stream =====");
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  Wire.setTimeOut(50);

  BMI160::Status status = imu.begin();
  while (status != BMI160::OK) {
    Serial.printf("Sensor did not start: %s. Trying again in 2 seconds.\n", BMI160::describe(status));
    delay(2000);
    status = imu.begin();
  }
  Serial.printf("BMI160 found at 0x%02X\n", imu.address());

  measureStillPosition();
  startBluetooth();
  nextSampleAt = millis();
  lastReportAt = millis();
}

void loop() {
  uint32_t now = millis();

  // ---- every 20 ms: read, pack, send ----
  if ((int32_t)(now - nextSampleAt) >= 0) {
    nextSampleAt += SAMPLE_PERIOD_MS;
    if ((int32_t)(now - nextSampleAt) > 200) {
      nextSampleAt = now + SAMPLE_PERIOD_MS;  // fell far behind, do not try to catch up
    }

    BMI160::Sample s;
    if (imu.read(s)) {
      const int16_t accel[3] = {s.ax, s.ay, s.az};
      const int16_t gyro[3] = {s.gx, s.gy, s.gz};
      swingBuildPacket(accel, gyro, gyroBias, AXIS_SOURCE, AXIS_SIGN, 0, packet);
      if (connected) {
        sensorCharacteristic->setValue(packet, SWING_PACKET_SIZE);
        sensorCharacteristic->notify();  // push it to the connected device
        sentThisSecond++;
      }
    } else {
      failedReads++;
    }
  }

  // ---- once a second: one line, and the packet as the other side will see it ----
  if (now - lastReportAt >= 1000) {
    lastReportAt = now;
    Serial.printf("%s  sent %lu/s  failed reads %lu  last packet:", connected ? "connected" : "waiting  ",
                  (unsigned long)sentThisSecond, (unsigned long)failedReads);
    for (size_t i = 0; i < SWING_PACKET_SIZE; i++) {
      Serial.printf(" %02X", packet[i]);
    }
    Serial.println();
    sentThisSecond = 0;
  }

  if (advertiseAgain) {
    advertiseAgain = false;
    delay(200);  // give the Bluetooth code a moment after a disconnect
    BLEDevice::startAdvertising();
    Serial.println("Disconnected. Advertising again.");
  }

  // Light: steady when connected, slow blink while waiting.
  bool lightOn = connected ? true : (now / 500) % 2 == 0;
  digitalWrite(LED_PIN, lightOn ? LED_ON : !LED_ON);

  delay(1);
}
