# Stage 1: is the board alive?

**You add:** the ESP32-C3 Super Mini and a USB-C cable. Nothing else is connected.

**You learn:** how to upload a sketch and read the Serial Monitor, and that a pin can be an output or an input.

## Steps

1. Set up the Arduino IDE as described in the [main README](../../README.md#set-up-the-arduino-ide).
2. Plug the board into the computer.
3. Open [01_blink.ino](01_blink.ino), choose the board and the port, and press Upload.
4. Open Tools > Serial Monitor at 115200 baud.
5. Press and hold the BOOT button on the board for a second or two.

## What you should see

The blue light on the board blinks, on for half a second and off for half a second. The Serial Monitor shows:

```
===== Stage 1: board test =====
Chip          ESP32-C3 (simulated), revision 4
Processor     160 MHz
Flash memory  4 MB
Light on GPIO8, BOOT button on GPIO9
The light should now blink. Press and hold BOOT to see the last column change.

tick    1   light ON    BOOT button not pressed
tick    2   light off   BOOT button not pressed
tick    3   light ON    BOOT button not pressed
tick    4   light off   BOOT button PRESSED
tick    5   light ON    BOOT button PRESSED
tick    6   light off   BOOT button not pressed
```

This output, like every example in this guide, comes from running the sketch on a computer in the simulator. On your board the chip line reads a little differently.

**The stage is passed when** the light blinks, the lines keep coming, and the last column changes while you hold BOOT.

## What is going on

- **Upload.** The Arduino IDE turns the sketch into a program for the chip and writes it into the board's flash memory through the USB cable. The program then starts by itself every time the board gets power.
- **`setup()` runs once, `loop()` runs for ever.** Every Arduino sketch has these two.
- **The Serial Monitor** shows the text the board sends back over the same USB cable. It is the main way to see what a sketch is doing.
- **Output.** `pinMode(LED_PIN, OUTPUT)` lets the sketch drive GPIO8. The light is soldered between 3.3 volts and that pin, so it lights when the pin is LOW. That is why the sketch has `LED_ON = LOW`.
- **Input.** `pinMode(BUTTON_PIN, INPUT_PULLUP)` lets the sketch read GPIO9. A small resistor inside the chip pulls the pin HIGH. The button joins it to ground, so pressed reads LOW.

## Try this

- Change `delay(500)` to `delay(100)` and upload. The light blinks five times as fast.
- Change `LED_ON` to `HIGH`. The words ON and off in the monitor no longer match the light.

## If it does not work

See [Uploading and the Serial Monitor](../../docs/troubleshooting.md#uploading-and-the-serial-monitor).

Next: [stage 2](../02_i2c_scan/).
