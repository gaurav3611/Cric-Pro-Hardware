/*
  Continuity tester, for when there is no multimeter.

  Two wires are the probes: one on GPIO10, one on GND. Touch them to two points.
  If the two points are joined, the blue light comes on and the Serial Monitor says JOINED.

  Only for parts and boards that have NO power of their own: a loose switch, a wire, the
  zero PCB with the ESP32 and the sensor taken out. Never touch a battery or a powered
  circuit with the probes. This cannot measure a voltage.
*/

const int PROBE_PIN = 10;   // a free pin. The other probe goes to any GND pin.
const int LED_PIN = 8;      // the blue light on the Super Mini, on when LOW

bool wasJoined = false;

void setup() {
  Serial.begin(115200);
  pinMode(PROBE_PIN, INPUT_PULLUP);  // held high by a resistor inside the chip, about 45 kilohm
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);       // light off
  delay(1500);
  Serial.println("Continuity tester. Probes: GPIO10 and GND. Touch them together: the light must come on.");
}

void loop() {
  bool joined = digitalRead(PROBE_PIN) == LOW;  // pulled to ground through whatever the probes touch
  digitalWrite(LED_PIN, joined ? LOW : HIGH);
  if (joined != wasJoined) {
    Serial.println(joined ? "JOINED" : "open");
    wasJoined = joined;
  }
  delay(20);
}
