# Stage 8: the firmware for the bat

**You add:** nothing. This is the sketch that stays on the bat.

**You learn:** how the pieces of stages 2 to 7 fit into one program, and how to mount and use the sensor.

![The whole circuit](../../diagrams/full-circuit.svg)

## What is new compared with stage 5

- It notices when the bat moved during the still measurement, and measures again.
- Holding the BOOT button for a second measures the still position again.
- It starts the sensor again by itself when a knock makes it stop answering.
- The sensor's axes can be turned to match how the board sits on the bat.
- With the stage 7 parts, it reports the battery level, warns when it is low, and sleeps when it is empty.

## Settings

Everything you may want to change is in [config.h](config.h).

| Setting | As delivered | Change it when |
|---|---|---|
| `SDA_PIN`, `SCL_PIN` | 3, 4 | You wired the sensor to other pins. |
| `HAS_BATTERY_SENSE` | 0 | You built stage 7. Set it to 1. |
| `BATTERY_CALIBRATION` | 1.000 | You built stage 7. Use the number you found there. |
| `AXIS_SOURCE`, `AXIS_SIGN` | no turning | The board sits differently on the bat. See below. |
| `DEVICE_NAME` | `SwingLab-Bat` | You have several bats. It must start with `SwingLab`. |

The two long ids and the 20 millisecond period are fixed by the dashboard. Leave them alone.

## Steps

1. Edit [config.h](config.h) if you built stage 7.
2. Open [08_swinglab_bat.ino](08_swinglab_bat.ino) and upload.
3. Keep the board still while the light blinks fast. That is the still measurement.
4. Test it with the test page, exactly as in [stage 5](../05_ble_stream/).
5. Unplug USB, switch the battery on, and test again.
6. Mount it on the bat and use it with the dashboard.

## What you should see

```
===== SwingLab bat sensor =====
BMI160 found at 0x68
Keep the bat still. Measuring the still position...
Still position: 0.24 -0.49 0.06 deg/s
Bluetooth is on. Look for "SwingLab-Bat".
waiting    sent 0/s  accel +0.01 -0.02 +1.00 g  gyro +0 +0 +0 deg/s  failed reads 0
connected  sent 12/s  accel +0.01 -0.02 +1.00 g  gyro +0 +0 +0 deg/s  failed reads 0
connected  sent 50/s  accel +0.48 +0.33 +1.00 g  gyro -43 +172 +20 deg/s  failed reads 0
connected  sent 50/s  accel +0.01 -0.02 +1.00 g  gyro +0 +0 +0 deg/s  failed reads 0
```

With the stage 7 parts and `HAS_BATTERY_SENSE` set to 1, it starts with the battery and adds it to every line:

```
Battery: 3.92 V, about 63%
...
connected  sent 50/s  accel +0.01 -0.02 +1.00 g  gyro +0 +0 +0 deg/s  failed reads 0  battery 3.92 V 63%
```

**The stage is passed when** the dashboard records a shot from the bat running on its battery.

## The light

| Light | Meaning |
|---|---|
| Fast blinking | Measuring the still position. Keep the bat still. |
| Slow blinking | Waiting for a connection. |
| Steady on | Connected and sending. |
| Two quick blinks, pause | Sensor not found. Check the wiring. |
| Three quick blinks, pause | Battery low, under 3.6 volts. Charge soon. |
| Five quick blinks, then off | Battery empty. The bat sleeps until it is switched off and on. |

The last two need the stage 7 parts.

## Use it with the dashboard

1. Open the SwingLab dashboard in Chrome or Edge and sign in.
2. Switch the bat on, keep it still while it blinks fast, and wait for the slow blink.
3. Press **Connect Sensor** and choose `SwingLab-Bat`.
4. Press **START SHOT**, swing, then press **STOP & SAVE SHOT**.

The dashboard reads the sensor only between START and STOP. Only one program can be connected at a time, so close the test page first.

## Mount it on the bat

- **Fix it rigidly.** Tape or strap the board to the back of the bat near the top of the handle, away from where the ball hits, with a little foam under it. If it can wobble, the readings are useless.
- **Fix the cell too.** Flat against the bat, with foam under it, and with no strain on its two leads.
- **Position along the bat does not change the speed.** Every point of a rigid bat turns at the same rate, so the rotation is the same wherever the board sits.
- **Direction does matter.** The dashboard treats rotation around y as the main swing, and uses rotation around z to decide leg side or off side. The mounting of the original sensor was never written down, so start like this and then check:

  1. Mount the board so that the sensor's Y arrow points across the face of the bat, from edge to edge, and its Z axis runs along the handle.
  2. Connect the test page and play a straight drive. `gy` should be the largest of the three rotation numbers.
  3. If another axis is largest, change `AXIS_SOURCE` in `config.h` so that axis feeds y. If leg and off come out swapped, flip the third number of `AXIS_SIGN`.

- **Measure the still position on the bat.** After mounting, hold the bat still and hold BOOT for a second, or switch off and on.

## What is going on

The sketch is short because the work is in four small files:

| File | Holds |
|---|---|
| `config.h` | Every setting. |
| `bmi160.h` | The sensor steps of stage 3: find, wake, set ranges, read. |
| `swing_packet.h` | The 14-byte packet of stage 5, with axis turning and the still value. |
| `battery.h` | The arithmetic of stage 7: pin voltage to cell voltage to percent. |

[how-it-works.md](../../docs/how-it-works.md#10-what-the-firmware-does-in-order) walks through what the sketch does at power-on and then every 20 milliseconds.

## Limits

The speed tops out near 107 km/h for a swing around one axis, impacts are cut off at about 14 g, and the dashboard's control score reads 0 on a real swing. These come from the sensor's ranges and from how the dashboard scores. [how-it-works.md](../../docs/how-it-works.md#limits-you-will-run-into) explains each.

## If it does not work

See [Bluetooth](../../docs/troubleshooting.md#bluetooth-stages-5-and-8) and [The battery](../../docs/troubleshooting.md#the-battery-stages-6-and-7).

Next, optional: [stage 9](../09_wifi_to_api/).
