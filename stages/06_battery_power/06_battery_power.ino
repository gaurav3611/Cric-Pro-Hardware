/*
  Stage 6 - Run from the battery

  Hardware: everything so far, plus the LiPo cell, the TP4056 charger module, the slide
  switch and the diode. Build and check the power circuit with the README of this stage
  BEFORE the battery is connected to the ESP32.

  What it does
    - blinks a short "heartbeat" every 2 seconds, so you can see it run with no cable
    - switches Bluetooth on, because the radio draws current in sharp bursts and that is
      what a weak power circuit cannot deliver
    - remembers, across restarts, how often the board started and WHY it started

  The one number that matters: brownout resets. A brownout is the chip restarting itself
  because its supply voltage dipped too low. On a good power circuit it stays 0.

  What you learn
    - where the power flows: cell -> TP4056 -> switch -> diode -> 5V pin -> regulator -> 3.3 V
    - the chip can tell you why it restarted
    - values can be kept through power-off in the chip's flash memory ("Preferences")

  Light on the board
    one short flash every 2 seconds    running normally
    ten fast flashes at start          there has been at least one brownout reset

  Serial Monitor: send c to clear the counters.
*/

#include <Preferences.h>
#include <BLEDevice.h>
#include <esp_system.h>

const int LED_PIN = 8;
const int LED_ON = LOW;

Preferences memory;          // a small key-value store in flash. It survives power-off.
uint32_t starts = 0;         // how many times the board has started
uint32_t brownouts = 0;      // how many of those were brownout resets
uint32_t previousRunSeconds = 0;
uint32_t lastSavedAt = 0, lastLineAt = 0;

void light(bool on) {
  digitalWrite(LED_PIN, on ? LED_ON : !LED_ON);
}

// Why did the chip start this time?
const char *startReason(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:  return "power was switched on";
    case ESP_RST_BROWNOUT: return "BROWNOUT: the supply voltage dipped too low";
    case ESP_RST_SW:       return "the program asked for a restart";
    case ESP_RST_PANIC:    return "the program crashed";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "the program hung and the watchdog restarted it";
    case ESP_RST_DEEPSLEEP: return "it woke from deep sleep";
    case ESP_RST_EXT:      return "the RESET button was pressed";
    default:               return "restart from the USB side, for example an upload or the Serial Monitor opening";
  }
}

void printTime(uint32_t seconds) {
  Serial.printf("%lu h %02lu min %02lu s", (unsigned long)(seconds / 3600), (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  light(false);

  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // on battery there is no computer. Never wait for one.
#endif
  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }

  esp_reset_reason_t reason = esp_reset_reason();

  memory.begin("power", false);  // open the store named "power", for reading and writing
  starts = memory.getUInt("starts", 0) + 1;
  brownouts = memory.getUInt("brownouts", 0) + (reason == ESP_RST_BROWNOUT ? 1 : 0);
  previousRunSeconds = memory.getUInt("runtime", 0);
  memory.putUInt("starts", starts);
  memory.putUInt("brownouts", brownouts);
  memory.putUInt("runtime", 0);

  Serial.println();
  Serial.println("===== Stage 6: battery power test =====");
  Serial.printf("This start       %s\n", startReason(reason));
  Serial.printf("Starts so far    %lu\n", (unsigned long)starts);
  Serial.printf("Brownout resets  %lu   %s\n", (unsigned long)brownouts,
                brownouts == 0 ? "good, this must stay 0" : "<- the power circuit is too weak, see the README of this stage");
  Serial.print("Previous run     ");
  printTime(previousRunSeconds);
  Serial.println();

  if (brownouts > 0) {
    for (int i = 0; i < 10; i++) {  // visible without a cable
      light(true);
      delay(60);
      light(false);
      delay(60);
    }
  }

  // The radio is what stresses the supply, so switch it on and advertise.
  BLEDevice::init("SSwinGG-PowerTest");
  BLEDevice::startAdvertising();
  Serial.println("Bluetooth is advertising as \"SSwinGG-PowerTest\", only to load the battery.");
  Serial.println("Send c to clear the counters.");
}

void loop() {
  uint32_t now = millis();

  // Heartbeat: 50 ms on, every 2 seconds.
  light(now % 2000 < 50);

  // Save the running time twice a minute, so the next start can report it.
  if (now - lastSavedAt >= 30000) {
    lastSavedAt = now;
    memory.putUInt("runtime", now / 1000);
  }

  if (now - lastLineAt >= 5000) {
    lastLineAt = now;
    Serial.print("running for ");
    printTime(now / 1000);
    Serial.printf("   brownout resets %lu\n", (unsigned long)brownouts);
  }

  if (Serial.available() && Serial.read() == 'c') {
    memory.putUInt("starts", 0);
    memory.putUInt("brownouts", 0);
    memory.putUInt("runtime", 0);
    starts = 0;
    brownouts = 0;
    Serial.println("Counters cleared.");
  }
  delay(10);
}
