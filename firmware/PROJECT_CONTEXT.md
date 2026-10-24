# Firmware project context

This file records implementation details and the current handoff state for
future firmware work. The repository README remains the user-facing setup and
release guide.

## Project intent

- The current product is an ESP32-S3 WebSocket gateway test using Drawbook and
  a 128 × 32 SSD1306 OLED.
- Drawbook remains the default behavior while the larger AC-control idea is
  developed. Keep future firmware features compatible with that workflow.
- OTA updates are intended to let the ESP32 firmware change without requiring
  a USB flash for every release.

## Hardware and gateway

- Device ID: `esp32-ac-01`; firmware board identifier: `esp32-s3`.
- OLED: SSD1306, 128 × 32, I2C address `0x3C`, SDA pin 8, SCL pin 9.
- Gateway: Railway production host `railway-websocket-production.up.railway.app`,
  secure WebSocket path `/ws`, port 443.
- The gateway serves release files under `/firmware`; manifest responses use
  no-cache headers. The device expects
  `/firmware/esp32-ac-01/manifest.json`.

## Provisioning and secrets

- `DeviceConfig.h` reads Wi-Fi credentials and the gateway device token from a
  newline-terminated JSON object over Serial at 115200 baud on first boot:

  ```json
  {"ssid":"...","password":"...","device_token":"..."}
  ```

- The ESP32 saves these values in NVS. OTA application updates preserve NVS,
  so credentials should not be embedded in or published with firmware binaries.
- Keep `.env`, Wi-Fi credentials, and the Railway `DEVICE_TOKEN` out of Git.

## OTA implementation

- `FirmwareOTA.h` checks the manifest as soon as Wi-Fi and a valid NTP clock
  are available, then no more often than every six hours. Restarting the board
  triggers a fresh check.
- OTA requires an OTA-capable ESP32 partition scheme. The firmware validates
  the board identifier, `MAJOR.MINOR.PATCH` version, allowed firmware path,
  HTTPS certificate, content length, and SHA-256 before finalizing the update.
- Manifest shape:

  ```json
  {
    "board": "esp32-s3",
    "enabled": true,
    "name": "release name",
    "version": "1.0.1",
    "firmware_path": "/firmware/esp32-ac-01/drawbook-v1.0.1.bin",
    "sha256": "64-character-hex-digest"
  }
  ```

- The Arduino-ESP32 core installed on this machine is 3.3.12. Its Mbed TLS
  headers expose `mbedtls_sha256_starts`, `mbedtls_sha256_update`, and
  `mbedtls_sha256_finish`; the `_ret` variants failed to compile here. Keep the
  non-`_ret` calls in `FirmwareOTA.h` for this core unless the core API changes.

## Current firmware: IR diagnostics and boot version screen 1.0.3

- Firmware version in `esp32_drawbook.ino`: `1.0.3`.
- Release name: `IR diagnostics and boot version screen`.
- IR receive pin: GPIO7. `IRReceiverFeature.h` uses IRremoteESP8266 2.9.0,
  a 1024-entry capture buffer, and a 50 ms timeout. The saved raw timing data
  remains available in RAM through the feature's accessors until another
  capture replaces it. Unknown protocols use the library's hash fallback and
  keep the raw timing buffer.
- OLED: shows the firmware version for at least five seconds on every boot,
  including during Wi-Fi connection. It then shows waiting, decoded/unknown
  results, or two raw timing pages. Drawbook frames still render normally; an
  IR capture temporarily takes the display. No inactivity running text is used.
- IR diagnostics: startup logs the GPIO7 idle level; a successful capture logs
  `IR RECEIVED`, protocol, bit count, raw length, overflow status, and a timing
  preview. A receiver idle level of LOW can indicate a wiring or module issue.
- Joystick: no joystick integration exists in this firmware, so IR capture is
  independent of it.
- The OLED keeps its current waiting screen or last Drawbook frame indefinitely
  until another event changes it.
- Release binary: `firmware/releases/esp32-ac-01/drawbook-v1.0.3.bin`.
- Release manifest:
  `firmware/releases/esp32-ac-01/manifest.json` (currently enabled for 1.0.3).
- The binary was compiled with Arduino-ESP32 core 3.3.12 and FQBN
  `esp32:esp32:esp32s3`, using the default 4 MB partition scheme. The binary is
  1,208,224 bytes; the compile report used 1,208,081 of 1,310,720 app bytes for
  firmware 1.0.3, leaving about 102 KB before the OTA app slot limit.
  Match the board and OTA partition settings used by the installed device when
  building later firmware; do not publish a binary built for a different
  partition layout.
- Firmware 1.0.2 was built and prepared for OTA. Firmware 1.0.3 is built and
  prepared for deployment to Railway.

## Publish and verify a future OTA release

1. Update `FIRMWARE_VERSION` in `firmware/esp32_drawbook/esp32_drawbook.ino`.
2. Compile for the same ESP32-S3 board settings and OTA partition scheme as
   the installed firmware. Export the application `.bin`; do not use the
   merged flash image.
3. Publish it with:

   ```sh
   npm run firmware:release -- <version> /absolute/path/to/compiled.bin "Release name"
   ```

   The script requires the argument version to match the sketch and be newer
   than the current manifest. It copies a versioned binary, computes SHA-256,
   and updates the manifest.
4. Commit the source, manifest, and versioned binary, then deploy the gateway
   so its `/firmware` route serves the release. Keep the device on its older
   firmware until the release is deployed.
5. Restart the board. Monitor Serial at 115200 baud for an OTA log such as
   `[OTA] Updating IR diagnostics and boot version screen (version 1.0.3).` After reboot,
   confirm the expected firmware behavior. If not restarted, the next check can
   take up to six hours.

## Build environment and troubleshooting

- Arduino IDE's bundled CLI on the development Mac is at:
  `/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli`.
- A local compile can use:

  ```sh
  "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" \
    compile --fqbn esp32:esp32:esp32s3 --output-dir /tmp/esp32-drawbook-build \
    firmware/esp32_drawbook
  ```

- Installed libraries used successfully: WebSockets by Links2004, Adafruit GFX,
  Adafruit SSD1306, ArduinoJson, and IRremoteESP8266 2.9.0.
- If compile errors mention missing `mbedtls_sha256_*_ret` functions, use the
  non-`_ret` calls described above for the installed core.
