# Parts to buy

Everything the bat sensor needs, with the name to search for in a shop and what to check before paying. The first table is the sensor itself. The second is only for the optional battery reading of stage 7.

## The sensor, stages 1 to 6 and 8

| # | Part | Name to search for | Qty | Check before buying |
|---|---|---|---|---|
| 1 | Microcontroller board | **ESP32-C3 Super Mini** development board | 1 | USB-C socket, eight pins on each long side, pins labelled `5V G 3.3 4 3 2 1 0` on one side. It comes with two loose 8-pin strips to solder on. |
| 2 | Motion sensor | **GY-BMI160** 6-axis module (Bosch BMI160) | 1 | "BMI160" in the name. An MPU6050 looks similar and does not work with this firmware. |
| 3 | Battery | **3.7 V 1500 mAh LiPo cell, size 803450**, with protection board | 1 | One cell, 3.7 V. About 8 by 34 by 50 mm. Two leads, red and black. Any plug is fine, it can be cut off. |
| 4 | Charger | **TP4056 charger module with protection**, 1 A, USB Type-C | 1 | Six pads: `IN+ IN- B+ B- OUT+ OUT-`. The version with only four pads has no protection: do not buy that one. |
| 5 | Power switch | **SS12D00G3** slide switch, SPDT, 3 pins | 1 | Pins 2.54 mm apart, so that it fits the zero PCB. Rated 0.5 A. |
| 6 | Diode | **1N5817** Schottky diode, 1 A, 20 V | 1 | The through-hole kind with two wire leads and a stripe at one end. A 1N5819 also works. |
| 7 | Board | **Zero PCB (perfboard), 5 by 7 cm**, holes 2.54 mm apart | 1 | At least 20 by 16 holes. Double-sided boards are easier to solder. |
| 8 | Sockets for the two boards | **Female header strip, single row, 2.54 mm** | two 8-pin, one 7-pin | A 40-pin strip can be cut instead. Each cut destroys one pin, so cut at the ninth and the eighth. |
| 9 | Wire | **Insulated hook-up wire**, 26 to 30 AWG, single strand | a few metres | Several colours help: red, black, and two others. |
| 10 | Cable to the computer | **USB-C cable that carries data** | 1 | Many cheap cables only charge. With those the board never shows up as a port. |
| 11 | Charger for the wall | **USB phone charger, 5 V, 1 A or more** | 1 | With a cable that fits the TP4056 module's socket. |
| 12 | Mounting | **Double-sided foam tape**, insulating tape or heat-shrink tube | a little | Foam under the board and the cell softens the knocks. |

It is worth buying two or three of items 5 and 6. They cost little and are easy to damage with a soldering iron.

## Only for the battery reading, stage 7

| # | Part | Name to search for | Qty | Check before buying |
|---|---|---|---|---|
| 13 | Two resistors | **100 kilohm resistor, 1/4 W, 1 %**, metal film | 2 | Both from the same strip, so they are as equal as possible. Colour bands: brown, black, black, orange, brown. |
| 14 | Capacitor | **100 nF ceramic capacitor, 50 V** | 1 | Also written 0.1 µF. Usually printed `104`. |

## Only if stage 6 reports brownouts

| # | Part | Name to search for | Qty | Check before buying |
|---|---|---|---|---|
| 15 | Capacitor | **220 µF electrolytic capacitor, 10 V or more** | 1 | It has a plus and a minus lead. The minus side is marked with a stripe. |

## Tools

| Tool | Notes |
|---|---|
| Soldering iron and solder | A fine tip, and thin solder with a flux core, 0.6 to 0.8 mm. |
| Multimeter | For continuity (the beep setting) and DC volts. It is not optional: the checks before first power need it. |
| Wire cutters and tweezers | |
| Computer with Chrome or Edge | For the Arduino IDE and for the Bluetooth test page. |

## Boards that look right and are not

- **ESP32-C3 Zero, ESP32-C3 DevKitM, LOLIN C3 Mini.** The firmware runs on them, because the chip is the same, but their pins sit in other places. The drawings and the zero PCB layout of this kit are for the Super Mini only.
- **Seeed XIAO ESP32-C3.** It has a charger built in, so the TP4056 part of this kit does not apply to it.
- **TP4056 module with four pads.** No protection for the cell.
- **Two cells, or a "7.4 V" pack.** The charger and the firmware are for one cell.
- **A 3.7 V cylindrical 18650 cell.** It works electrically, but it is heavy and hard to fix to a bat.

## Why these and not others

- **1500 mAh** runs the sensor for roughly a day of continuous use, and the cell is thin enough to tape flat to a bat. A 1000 mAh cell also works and is lighter.
- **The diode** is the cheapest way to make it safe to plug in the ESP32's USB cable while the battery is switched on. [stages/06_battery_power](../stages/06_battery_power/) explains it.
- **The switch goes in the plus wire**, after the charger. Then the cell can be charged with the bat switched off.
