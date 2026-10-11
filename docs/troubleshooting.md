# Troubleshooting

Find what you see in the left column. The stages are in order, because a fault in an early stage shows up in every later one.

## Uploading and the Serial Monitor

| What you see | Likely cause | What to do |
|---|---|---|
| No port appears under Tools > Port | The cable only charges, or the board is stuck | Try another cable. Then hold BOOT while plugging in. |
| "Failed to connect" during upload | The board did not enter upload mode by itself | Hold BOOT, press and release RESET, release BOOT, upload again. Press RESET afterwards. |
| The Serial Monitor stays empty | Wrong speed, or the wrong board is selected | Set 115200 baud. Select "Nologo ESP32C3 Super Mini". Press RESET with the monitor open. |
| Garbage characters | Wrong speed | Set 115200 baud. |
| The light is on when the sketch says off | Your board's light works the other way round | Change `LED_ON` from `LOW` to `HIGH`. |

## The sensor, stages 2 to 4

| What you see | Likely cause | What to do |
|---|---|---|
| Stage 2 finds nothing | A wire is open or two are swapped | Check each wire with the multimeter. SDA and SCL are the usual pair. |
| Still nothing | The sensor is waiting in its other mode | Connect the sensor's `CS` pin to `3V3`. |
| "This looks like an MPU6050" | The board is a different sensor | This firmware needs a BMI160. See [make-changes.md](make-changes.md#use-another-sensor). |
| The address is 0x68 one time and 0x69 the next | `SAO` is not connected | Join `SAO` to `GND`. |
| Stage 3: "did not wake up" | Poor power to the sensor | Check `3V3` to `VIN` and `GND` to `GND`. Measure 3.3 volts across them. |
| Stage 3 fails the gravity check | The board moved, or a joint is poor | Send `r` with the board untouched. Reflow the joints of SDA and SCL. |
| Readings stop now and then | A loose wire | Press on each joint while watching the monitor. |
| Speed is not 0 when the board is still | The still position was measured while it moved | Send `r`, or in stage 8 hold BOOT for a second, with the board still. |
| Stage 4 says the gyroscope hit its limit | The swing was faster than 2000 degrees per second | Nothing is wrong. That is the top of the sensor's range. |

## Bluetooth, stages 5 and 8

| What you see | Likely cause | What to do |
|---|---|---|
| The test page says the browser blocks Bluetooth | The page was opened as a file | Serve it from `localhost`. The README of stage 5 has the command. |
| No device appears in the list | The sketch is not running, or something else is connected | Look for the slow blink. Close other tabs and phone apps that use the bat. Switch the computer's Bluetooth off and on. |
| It connects and nothing arrives | Notifications did not start | Disconnect, press RESET on the board, connect again. |
| Fewer than 40 readings a second | Weak signal, or a busy computer | Move closer. Close other Bluetooth devices. |
| The range is only a metre or two | Some Super Mini boards have a weak antenna | Keep the bat near the computer or phone. Another board may do better. |
| The dashboard connects and shows nothing | It only reads while a shot is being recorded | Press START SHOT. |
| leg and off side are swapped, or speed is low on a real swing | The board sits in a different direction than the dashboard expects | See "Mount it on the bat" in the README of stage 8. |

## The battery, stages 6 and 7

| What you see | Likely cause | What to do |
|---|---|---|
| Nothing happens when switched on | Switch wired to the wrong pins, diode the wrong way round, or the charger's protection has cut off | Measure, with the switch on: charger `OUT+` to `OUT-`, then the `5V` pin to `GND`. The first should read the cell voltage, the second about 0.3 volts less. |
| `OUT+` to `OUT-` reads 0 with a charged cell | The protection has tripped | Plug the charger into USB for a few seconds. That resets it. |
| Stage 6 counts brownouts | The path from the cell is too weak | Shorten and thicken the wires. Reflow the joints. Add a 220 µF capacitor from `5V` to `GND`, plus lead to `5V`. |
| It restarts during a hard swing | A joint opens for a moment | The same. Also fix the cell and the board so they cannot move against each other. |
| The charger gets warm | It is charging at 1 amp | Normal. It should not be too hot to touch. |
| The charger's light never changes to "full" | The bat is on while charging | Switch off while charging. |
| Stage 7 reads "no battery seen" | The switch is off, or R1 is not joined to the switch output | Switch on. Measure the middle point of R1 and R2: it should be half the cell voltage. |
| Stage 7 reads a little too high or low | The resistors are not exactly equal | Set `CALIBRATION` as the stage describes. |
| The bat blinks five times and goes dark | The battery is empty | Charge it, then switch off and on. |

## WiFi, stage 9

| What you see | Likely cause | What to do |
|---|---|---|
| It never joins the network | Wrong name or password, or a 5 GHz network | Check both. The chip only joins 2.4 GHz networks. |
| "The API was not reached" | Wrong address, a firewall, or different networks | Check the address in `API_URL`. Allow port 5050. Put the computer on the same WiFi. |

## When nothing explains it

Go back to the last stage that worked and upload its sketch again. If that stage still works, the fault is in what you added since.
