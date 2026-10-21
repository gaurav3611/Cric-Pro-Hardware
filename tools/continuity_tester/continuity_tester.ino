/*
  Continuity tester, for when there is no multimeter.

  Two wires are the probes: one on the probe pin, one on GND. Touch them to two points.
  If the two points are joined, the blue light comes on and the Serial Monitor says JOINED.

  Works on two boards, picked automatically by the board chosen in Tools > Board:
    ESP32-C3 Super Mini:  probe pin GPIO10, light on GPIO8 (on when LOW)
    ESP32 DevKit (ESP32 Dev Module):  probe pin D23, light on D2 (on when HIGH)
  Some DevKits have no blue light, only the red power light. Then watch the Serial Monitor.

  Only for parts and boards that have NO power of their own: a loose switch, a wire, the
  zero PCB with the ESP32 and the sensor taken out. Never touch a battery or a powered
  circuit with the probes. This cannot measure a voltage.
*/

#if CONFIG_IDF_TARGET_ESP32C3
const int PROBE_PIN = 10;   // a free pin. The other probe goes to any GND pin.
const int LED_PIN = 8;      // the blue light on the Super Mini
const int LED_ON = LOW;
#else
const int PROBE_PIN = 23;   // D23 on an ESP32 DevKit: no boot role, has a pull-up
const int LED_PIN = 2;      // the blue light on a DevKit V1
const int LED_ON = HIGH;
#endif

bool wasJoined = false;

void setup() {
  Serial.begin(115200);
  pinMode(PROBE_PIN, INPUT_PULLUP);  // held high by a resistor inside the chip, about 45 kilohm
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, !LED_ON);    // light off
  delay(1500);
  Serial.printf("Continuity tester. Probes: GPIO%d and GND. Touch them together: the light must come on.\n", PROBE_PIN);
}

void loop() {
  bool joined = digitalRead(PROBE_PIN) == LOW;  // pulled to ground through whatever the probes touch
  digitalWrite(LED_PIN, joined ? LED_ON : !LED_ON);
  if (joined != wasJoined) {
    Serial.println(joined ? "JOINED" : "open");
    wasJoined = joined;
  }
  delay(20);
}
