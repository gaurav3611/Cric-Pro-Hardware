/*
  Stage 1 - Is the board alive?

  Hardware: the ESP32-C3 Super Mini and a USB-C cable that carries data. Nothing else.

  What it does
    - blinks the blue light on the board
    - prints one line every half second to the Serial Monitor
    - tells you when the BOOT button is pressed

  What you learn
    - how to upload a sketch and read the Serial Monitor
    - a pin can be an OUTPUT (the light) or an INPUT (the button)
    - on this board the light is ON when its pin is LOW

  Arduino IDE
    Tools > Board > esp32 > Nologo ESP32C3 Super Mini
    Tools > Port  > the port that appears when the board is plugged in
    Tools > Serial Monitor, 115200 baud
*/

// ---- Pins on the ESP32-C3 Super Mini -------------------------------------------------
const int LED_PIN = 8;       // the blue light is soldered to GPIO8
const int LED_ON = LOW;      // LOW switches it on. If yours is the other way round, write HIGH.
const int BUTTON_PIN = 9;    // the BOOT button is soldered to GPIO9. Pressed reads LOW.

uint32_t tick = 0;

void setup() {
  // The board talks to the computer through its own USB socket.
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);  // give the Serial Monitor up to 3 seconds to open
  }

  pinMode(LED_PIN, OUTPUT);           // we drive this pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // we read this pin. The pull-up keeps it HIGH until the button pulls it LOW.

  Serial.println();
  Serial.println("===== Stage 1: board test =====");
  Serial.printf("Chip          %s, revision %d\n", ESP.getChipModel(), (int)ESP.getChipRevision());
  Serial.printf("Processor     %d MHz\n", (int)ESP.getCpuFreqMHz());
  Serial.printf("Flash memory  %d MB\n", (int)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("Light on GPIO%d, BOOT button on GPIO%d\n", LED_PIN, BUTTON_PIN);
  Serial.println("The light should now blink. Press and hold BOOT to see the last column change.");
  Serial.println();
}

void loop() {
  tick++;
  bool lightOn = (tick % 2 == 1);
  digitalWrite(LED_PIN, lightOn ? LED_ON : !LED_ON);

  bool pressed = (digitalRead(BUTTON_PIN) == LOW);
  Serial.printf("tick %4lu   light %-3s   BOOT button %s\n",
                (unsigned long)tick, lightOn ? "ON" : "off", pressed ? "PRESSED" : "not pressed");

  delay(500);
}
