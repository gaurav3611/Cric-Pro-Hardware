# Stage 4: from sensor numbers to a swing

**You add:** nothing. Still on the USB cable.

**You learn:** why a still position has to be measured, and how rotation becomes bat speed.

## Steps

1. Lay the board still.
2. Open [04_swing_speed.ino](04_swing_speed.ino), upload, and open the Serial Monitor.
3. Wait for "Swing the board", then hold the board like the handle of a bat and swing your arm. Mind the cable.

## What you should see

```
===== Stage 4: swing speed =====
Keep the board still for one second...
Rotation at rest: x +0.30  y -0.49  z +0.12 deg/s. This is now subtracted.

Swing the board. One line is printed for each swing. Send r to measure the still position again.
  now   0.0 km/h   1.00 g
  now   0.0 km/h   1.00 g
Swing 1:  peak speed  49.8 km/h   peak acceleration  4.5 g   lasted 0.34 s   mostly around y   ended turning leg side
  now   0.0 km/h   1.00 g
Swing 2:  peak speed  83.0 km/h   peak acceleration  6.3 g   lasted 0.33 s   mostly around y   ended turning off side
  now   0.0 km/h   1.00 g
Swing 3:  peak speed 111.1 km/h   peak acceleration  7.6 g   lasted 0.27 s   mostly around y   ended turning straight
          The gyroscope hit its limit of 2000 degrees per second. The real swing was faster.
```

**The stage is passed when** the speed reads 0.0 while the board is still, and each swing prints a line.

## What is going on

**The still position.** A gyroscope at rest reports a small rotation that is different for every chip. The sketch averages it for one second and subtracts it from every later reading. Without that, a still board would show a speed.

**Bat speed.** The sketch uses the dashboard's own formula:

```
total rotation = square root of (gx squared + gy squared + gz squared)    degrees per second
below 5 it counts as 0                                                    the dead zone
times pi / 180                                                            radians per second
times 0.85                                                                metres per second, 0.85 m from the pivot
times 3.6                                                                 km/h
```

A point 0.85 metres from the pivot moves 0.85 metres for each radian the bat turns. That distance, from the wrists to the sweet spot of the bat, is the dashboard's assumption. It is the same for every bat and every player.

**The side.** The dashboard looks at rotation around z: above +1 is leg side, below -1 is off side, in between is straight.

**The limit.** The sensor cannot report more than 2000 degrees per second on one axis. That is 106.8 km/h for a swing around a single axis. Faster swings read flat, and the sketch says so.

**The swing finder is only in this sketch.** It starts a swing above 15 km/h and ends it after 0.2 seconds below 8. The dashboard does not find swings by itself: there you press START SHOT and STOP.

## Try this

- Change `BAT_LENGTH_M` to `0.60`. The same swing now reads slower. The sensor did not change, only the assumption.
- Send `r` while moving the board, then hold it still. It now shows a speed at rest. Send `r` again while still to fix it.
- Lower `SWING_START_KMH` to `5` and see small movements count as swings.

## If it does not work

See [The sensor](../../docs/troubleshooting.md#the-sensor-stages-2-to-4).

Next: [stage 5](../05_ble_stream/).
