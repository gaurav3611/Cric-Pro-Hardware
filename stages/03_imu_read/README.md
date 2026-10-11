# Stage 3: talk to the sensor and read real numbers

**You add:** nothing. The wiring is that of stage 2.

**You learn:** how a sensor is set up by writing registers and measured by reading them, and how raw bytes become g and degrees per second.

## Steps

1. Lay the board flat on a table.
2. Open [03_imu_read.ino](03_imu_read.ino), upload, and open the Serial Monitor.
3. Do not touch the board for the first five seconds.

## What you should see

```
===== Stage 3: read the BMI160 =====
SDA = GPIO3, SCL = GPIO4
Step 1: who is there?
  address 0x68 answered
  register 0x00 (chip id) = 0xD1   this is a BMI160
Step 2: wake it up and choose the ranges
  wrote 0xB6 to register 0x7E   reset, so it starts from a known state
  wrote 0x11 to register 0x7E   accelerometer on
  wrote 0x15 to register 0x7E   gyroscope on
  wrote 0x08 to register 0x41   acceleration range +-8 g
  wrote 0x00 to register 0x43   rotation range +-2000 degrees per second
  wrote 0x28 to register 0x40   100 acceleration readings a second
  wrote 0x28 to register 0x42   100 rotation readings a second
  register 0x03 (power status) = 0x14   accelerometer and gyroscope are both awake
Step 3: one reading, byte by byte
  12 bytes from register 0x0C:  04 00 F7 FF 03 00 30 00 AE FF 12 10
  rotation x      bytes 04 00  =      4 counts  / 16.4  =    +0.24 deg/s
  rotation y      bytes F7 FF  =     -9 counts  / 16.4  =    -0.55 deg/s
  rotation z      bytes 03 00  =      3 counts  / 16.4  =    +0.18 deg/s
  acceleration x  bytes 30 00  =     48 counts  / 4096  =   +0.012 g
  acceleration y  bytes AE FF  =    -82 counts  / 4096  =   -0.020 g
  acceleration z  bytes 12 10  =   4114 counts  / 4096  =   +1.004 g
  Low byte first: bytes 12 10 mean 0x1012, which is 4114.
Step 4: still check. Lay the board flat and do not touch it for 3 seconds...
  acceleration  x +0.012  y -0.020  z +1.004 g   total 1.004 g
  rotation      x +0.30  y -0.49  z +0.12 deg/s   noise 0.04 deg/s
  every reading arrived                                  PASS
  total acceleration is gravity, 0.90 to 1.10 g          PASS
  rotation at rest is small, under 10 deg/s per axis     PASS
  rotation is steady, noise under 1 deg/s                PASS
RESULT: PASS. The sensor works. Go on to stage 4.

Live readings, 10 a second. Tilt the board and turn it by hand.
acceleration  +0.01  -0.02  +1.00 g    rotation     +0.2     -0.5     +0.1 deg/s
```

Your numbers will differ a little. **The stage is passed when** all four checks say PASS.

Then check these by hand:

- **Flat on the table**, acceleration z is close to +1 g. Turned upside down, close to -1 g.
- **Standing on an edge**, x or y takes the 1 g instead.
- **Turning it by hand** makes the rotation numbers jump. They return to near 0 when it stops.

Type a letter in the Serial Monitor and press Enter: `b` shows one reading byte by byte again, `r` repeats the still check, `p` switches to a format for Tools > Serial Plotter.

## What is going on

- **Writing a register sets the sensor up.** After a reset both of its halves are asleep. Writing 0x11 and 0x15 to the command register wakes them. Two more registers choose the ranges.
- **Reading registers measures.** The newest reading sits in twelve registers starting at 0x0C. One request fetches all twelve bytes.
- **Two bytes make one number.** The low byte comes first. `F7 FF` is 0xFFF7, and as a signed 16-bit number that is -9.
- **Counts become units by dividing.** The range decides the divisor: 4096 counts per g at 8 g, 16.4 counts per degree per second at 2000.
- **Gravity is the test signal.** A still accelerometer must read 1 g in total. That one number checks the wiring, the range and the arithmetic at once.
- **The gyroscope is never exactly 0 at rest.** In the example it reads +0.30, -0.49 and +0.12. Stage 4 removes that.

This sketch does everything by hand on purpose. Later stages use `bmi160.h`, which holds the same steps in a tidier form.

## Try this

- Send `p` and open Tools > Serial Plotter. Tilt the board slowly and watch the three acceleration lines swap places.
- In the sketch, change `0x08` to `0x03` in the line that sets the acceleration range, and upload. That is 2 g. Lying flat, z now reads about 4 g and the still check fails, because the divisor no longer fits the range. Put it back.

## If it does not work

See [The sensor](../../docs/troubleshooting.md#the-sensor-stages-2-to-4).

Next: [stage 4](../04_swing_speed/).
