# ESP32 AC Gateway R&D

Small Node.js gateway for testing browser-to-server WebSocket communication before an ESP32-S3 is connected. The Drawbook is a communication test client; the same `/ws` layer can later route AC commands.

## Requirements

- Node.js 20 or newer
- npm

## Run locally

```sh
cp -n .env.example .env
# Replace both example values with separate outputs from: openssl rand -hex 32
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000); the root redirects to `/login`. After login, the server redirects to `/drawbook`. Opening `/drawbook` without a valid session also redirects to `/login`. Run `npm run dev` for development. Its Bash launcher stops the previous instance started by the launcher, then starts Node's watch mode, which restarts the server when source files change. Press `Ctrl+C` to stop it. For a production-style local start, use `npm start`.

Login submits the `SECRET_KEY` to the server over HTTP(S). A successful login sets a signed, HTTP-only, same-site session cookie that expires after eight hours. The Drawbook route and its browser WebSocket both require this session. On Railway, use HTTPS so the session cookie is Secure and the browser uses `wss://<railway-domain>/ws`; local HTTP uses `ws://localhost:3000/ws`.

## Check endpoints

- `GET http://localhost:3000/health` returns `{ "status": "ok", "uptime": ... }`.
- `GET http://localhost:3000/status` returns browser/device counts, device IDs, and uptime. It requires the secret key in the `x-secret-key` header:

  ```sh
  curl -H "x-secret-key: $SECRET_KEY" http://localhost:3000/status
  ```

- `GET /health` stays public for service health checks and returns no device details.

## Device registration

Set both values in `.env`. The browser authenticates through `/login`; the cookie session authenticates its WebSocket connection. A device connects to `/ws` and sends:

```json
{"type":"device_register","device_id":"esp32-ac-01","token":"your-device-token"}
```

The server rejects missing or incorrect keys/tokens. Keep `.env` and real secrets out of Git; `.env` is ignored. Draw frames contain 4096 ASCII `0`/`1` characters. Frames are sent when a stroke ends or when Send is clicked. The server forwards them to `esp32-ac-01`; while it is offline an authenticated browser receives `{"type":"error","message":"Device offline"}`.

## Deploy to Railway

1. Push this project to a GitHub repository.
2. In Railway, create a project and deploy the repository as a Node.js service.
3. Set `SECRET_KEY` and `DEVICE_TOKEN` in Railway's Variables panel to separate long random values. Do not commit `.env` or put secrets in source code. Railway supplies `PORT`; the server listens on that port at `0.0.0.0`.
4. Deploy with the default start command `npm start` (also set the service start command to `npm start` if Railway does not detect it automatically).
5. In the service's Networking settings, generate a public domain. Use that domain to open `https://<railway-domain>/` and check `https://<railway-domain>/health`. To check `/status`, send the `x-secret-key` header as shown above.
6. The WebSocket URL is `wss://<railway-domain>/ws`; Drawbook builds this URL from the current page automatically.

No database or Dockerfile is required. The browser-only test works without the
firmware; use the sketch below when you are ready to connect the ESP32-S3.

## ESP32-S3 firmware

An Arduino sketch that connects the ESP32-S3 to the Railway gateway and draws
received frames on the SSD1306 OLED is in `firmware/esp32_drawbook/`.

Install the Arduino libraries
`WebSockets` by Markus Sattler (Links2004), `Adafruit GFX Library`,
`Adafruit SSD1306`, `ArduinoJson`, and `IRremoteESP8266` 2.9.0 in Arduino IDE.
Select the same ESP32-S3 partition scheme with OTA support used by the device.
The sketch uses TLS certificate validation for the Railway WSS connection and
registers as `esp32-ac-01`.

The HX1838 infrared receiver OUT pin connects to GPIO7. Firmware 1.0.6 uses
IRremoteESP8266 2.9.0 in receive-only mode, with a 4096-entry raw capture buffer
and a 100 ms inter-signal timeout. This preserves long spaces between sections
of AC frames and keeps unknown-protocol timing data. The full latest raw capture
remains in RAM until the next IR capture or reboot. Serial at 115200 baud logs
decoded protocol/address/command/bit/repeat fields and all raw timings; unknown
frames still print their complete raw timings. A two-second heartbeat reports
the GPIO7 level and capture count so a monitor opened after boot can confirm
that the firmware is running. Its heartbeat also reports loop-sampled GPIO7
transitions and low-level samples to diagnose missing captures. The existing
Drawbook, Wi-Fi, Railway WebSocket, OLED, device configuration, and online
firmware update features remain enabled.
No joystick or IR transmitter is used in this receive-only test stage.

### Test the IR receiver

With firmware 1.0.6 running, point an AC remote at the receiver and press a
button once. The OLED briefly shows `IR DETECTED / Decoding...`, then the
protocol and bit count or an `UNKNOWN IR` raw-count page. Serial Monitor at
115200 baud prints the capture, including repeat status and every raw timing.
The test firmware only receives and learns IR signals; it does not transmit
them. The existing OTA mechanism remains enabled and can distribute firmware
1.0.6 from the manifest after the gateway deployment is updated.

On the first USB flash, open Serial Monitor at 115200 baud, set line ending to
**Newline**, and send one JSON line with the Wi-Fi and Railway device token,
for example:

```json
{"ssid":"YOUR_WIFI_SSID","password":"YOUR_WIFI_PASSWORD","device_token":"YOUR_RAILWAY_DEVICE_TOKEN"}
```

The device stores this configuration in NVS and restarts. OTA updates replace
the application partition and preserve the saved configuration, so secrets do
not need to be compiled into or published with firmware binaries.

After it connects, the Drawbook should report the device as online. A frame is
128 × 32 pixels, sent as 4096 `0`/`1` characters, then rendered on the OLED.
When no new Drawbook frame or IR capture arrives, the OLED keeps its current
waiting screen or last Drawbook frame. There is no inactivity running text.

## Firmware auto-update

The existing Drawbook behavior remains the default firmware function. The
firmware checks the public release manifest at
`/firmware/esp32-ac-01/manifest.json` at startup and every six hours. It only
installs a newer `MAJOR.MINOR.PATCH` release for the `esp32-s3` board. The
download uses the same verified HTTPS root certificate as the gateway and
checks the binary's SHA-256 from the manifest before flashing. The current
manifest targets release 1.0.6, `HX1838 GPIO activity diagnostics`. Deploy the
release files and gateway changes before restarting the device to receive it.
To install it online, commit the source, release binary, and manifest together,
then deploy the existing Railway service. Restart the ESP32 while it has Wi-Fi
and a valid clock, or let its six-hour check run. It validates the manifest and
SHA-256, installs the newer firmware, and restarts using the existing updater.

To publish a release:

1. Increase `FIRMWARE_VERSION` in `firmware/esp32_drawbook/esp32_drawbook.ino`
   and compile for the same ESP32-S3 board and OTA-capable partition scheme
   used for the device. Enable **USB CDC On Boot** so `Serial` logs appear on
   the native USB serial port. With Arduino CLI, use:

   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32s3 \
     --board-options 'USBMode=hwcdc,CDCOnBoot=cdc' \
     --output-dir /tmp/esp32-drawbook-build firmware/esp32_drawbook
   ```

   The application image is `esp32_drawbook.ino.bin`.
2. In Arduino IDE, use **Sketch → Export compiled Binary** and locate the
   resulting `.bin` file, with **USB CDC On Boot** enabled in the board options.
3. Run `npm run firmware:release -- <version> /absolute/path/to/compiled.bin "HX1838 GPIO activity diagnostics"`.
   The command checks the sketch version, copies the versioned binary into
   `firmware/releases/esp32-ac-01/`, and writes its name, SHA-256, and version
   to the manifest.
4. Commit the source version, manifest, and generated release binary, then
   deploy the service. For an immediate check, restart the device after the
   deployment; otherwise it checks again within six hours. To test OTA, export
   the `.bin` without uploading that new version over USB.

The first install still needs to be flashed over USB with an OTA-capable
partition layout and provisioned once over Serial. Later releases can be
installed by the firmware updater.

For implementation details, known build constraints, the current release
state, and a continuation handoff, see
[`firmware/PROJECT_CONTEXT.md`](firmware/PROJECT_CONTEXT.md).
