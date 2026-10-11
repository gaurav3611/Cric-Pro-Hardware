# Stage 9, optional: send readings to the backend over WiFi

**You add:** nothing.

**You learn:** that the chip has a second radio, and how the same readings travel as text over HTTP.

The dashboard does not need this stage. It talks to the bat over Bluetooth. This stage shows the other road into the system: the board joins your WiFi and posts readings straight to the API.

## Steps

1. **Start the API** on the computer. It is in the [java-cricket-backend](https://github.com/gaurav3611/java-cricket-backend) repository and listens on port 5050.
2. **Find the computer's address** on the network. On a Mac: `ipconfig getifaddr en0`.
3. **Fill in three settings** at the top of [09_wifi_to_api.ino](09_wifi_to_api.ino): `WIFI_NAME`, `WIFI_PASSWORD`, and the address inside `API_URL`. The network must be 2.4 GHz, and the computer must be on the same one.
4. Upload and open the Serial Monitor.
5. Open `http://localhost:5050/docs` on the computer and press Send on `GET /api/esp32/status`.

## What you should see

In the Serial Monitor, every two seconds, a line of this form:

```
accepted 20  failed 0  last answer 200  slowest 38 ms
```

The numbers here are an illustration. About 20 are accepted in two seconds, and the slowest time depends on your network.

On the API page, the device `SwingLab-Bat` is listed as online.

**The stage is passed when** `accepted` counts up and `failed` stays 0.

This is the one stage that was not run in the simulator, because it needs a real network. It was only compiled.

## What is going on

- **WiFi and Bluetooth are two radios in the same chip.** This sketch uses only WiFi.
- **HTTP** is one request and one answer. The board sends a `POST` to `/api/sensor-data`. The API answers with a status number. 200 means accepted.
- **JSON** is text. One reading looks like this:

  ```
  {"device_id":"SwingLab-Bat","ax":0.012,"ay":-0.020,"az":1.004,"gx":0.31,"gy":-0.52,"gz":0.11,"ts":12345}
  ```

  Here the numbers travel in real units, g and degrees per second, not as counts. That is about 110 characters for what Bluetooth sends in 14 bytes.
- **10 readings a second**, not 50. Each one is a full request and answer, which takes far longer than a Bluetooth notification.

## Before you share this file

Put `YOUR_WIFI_NAME` and `YOUR_WIFI_PASSWORD` back. `tools/check.mjs` fails while a real name or password is in the sketch.

## If it does not work

See [WiFi](../../docs/troubleshooting.md#wifi-stage-9).
