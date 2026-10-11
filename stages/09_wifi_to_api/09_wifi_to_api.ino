/*
  Stage 9 (optional) - Send readings to the backend over WiFi

  Hardware: the same as stage 5. USB or battery.

  The dashboard does not need this. It talks to the bat over Bluetooth (stages 5 and 8).
  This stage shows the other road into the system: the board joins your WiFi and posts
  readings straight to the API, 10 times a second, as text in the JSON format.

      POST http://<computer>:5050/api/sensor-data
      {"device_id":"SwingLab-Bat","ax":0.012,"ay":-0.020,"az":1.004,"gx":0.31,"gy":-0.52,"gz":0.11,"ts":12345}

  What you learn
    - WiFi and Bluetooth are two different radios in the same chip
    - HTTP: one request, one answer with a status number. 200 means accepted.
    - here the numbers travel as text with units, not as 14 packed bytes

  Before uploading, fill in the three settings below.
  The ESP32-C3 joins 2.4 GHz WiFi only. The computer and the board must be on the same network.
  Find the computer's address on a Mac with:   ipconfig getifaddr en0

  Do not share this file with your WiFi password in it.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "bmi160.h"

const char *WIFI_NAME = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *API_URL = "http://192.168.1.10:5050/api/sensor-data";  // the computer running the Java API

const char *DEVICE_ID = "SwingLab-Bat";
const uint32_t SEND_PERIOD_MS = 100;  // 10 readings per second

const int SDA_PIN = 3;
const int SCL_PIN = 4;

BMI160 imu;
WiFiClient network;
HTTPClient http;
float gyroBias[3] = {0, 0, 0};
uint32_t nextSendAt = 0;
uint32_t sentOk = 0, sentFailed = 0, lastReportAt = 0, slowestMs = 0;
int lastCode = 0;

void connectWifi() {
  Serial.printf("Joining WiFi \"%s\"", WIFI_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println();
      Serial.println("Could not join. Check the name and password, and that it is a 2.4 GHz network. Trying again.");
      WiFi.disconnect();
      WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
      start = millis();
    }
  }
  Serial.println();
  Serial.printf("Joined. This board is %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }
  Serial.println();
  Serial.println("===== Stage 9: WiFi to the API =====");

  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  Wire.setTimeOut(50);
  BMI160::Status status = imu.begin();
  while (status != BMI160::OK) {
    Serial.printf("Sensor did not start: %s. Trying again in 2 seconds.\n", BMI160::describe(status));
    delay(2000);
    status = imu.begin();
  }

  // Measure the rotation at rest, so a still board reports no speed.
  Serial.println("Keep the board still for one second...");
  double sum[3] = {0, 0, 0};
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
    gyroBias[k] = good > 0 ? (float)(sum[k] / good) : 0;
  }

  connectWifi();
  http.setReuse(true);  // keep one connection open instead of opening one per reading
  http.setTimeout(2000);
  Serial.printf("Posting to %s\n", API_URL);
  nextSendAt = millis();
  lastReportAt = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  uint32_t now = millis();
  if ((int32_t)(now - nextSendAt) >= 0) {
    nextSendAt = now + SEND_PERIOD_MS;

    BMI160::Sample s;
    if (imu.read(s)) {
      // The API wants real units: acceleration in g, rotation in degrees per second.
      char body[240];
      snprintf(body, sizeof(body),
               "{\"device_id\":\"%s\",\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,\"ts\":%lu}",
               DEVICE_ID,
               s.ax / BMI160_ACC_LSB_PER_G, s.ay / BMI160_ACC_LSB_PER_G, s.az / BMI160_ACC_LSB_PER_G,
               (s.gx - gyroBias[0]) / BMI160_GYR_LSB_PER_DPS, (s.gy - gyroBias[1]) / BMI160_GYR_LSB_PER_DPS,
               (s.gz - gyroBias[2]) / BMI160_GYR_LSB_PER_DPS, (unsigned long)now);

      uint32_t started = millis();
      http.begin(network, API_URL);
      http.addHeader("Content-Type", "application/json");
      lastCode = http.POST(body);
      http.end();
      uint32_t took = millis() - started;
      slowestMs = max(slowestMs, took);
      if (lastCode == 200) {
        sentOk++;
      } else {
        sentFailed++;
      }
    }
  }

  if (now - lastReportAt >= 2000) {
    lastReportAt = now;
    Serial.printf("accepted %lu  failed %lu  last answer %d  slowest %lu ms\n",
                  (unsigned long)sentOk, (unsigned long)sentFailed, lastCode, (unsigned long)slowestMs);
    if (sentFailed > 0 && lastCode < 0) {
      Serial.println("  The API was not reached. Is it running, is the address right, and does the firewall allow port 5050?");
    }
    sentOk = 0;
    sentFailed = 0;
    slowestMs = 0;
  }
  delay(2);
}
