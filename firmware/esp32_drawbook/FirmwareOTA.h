#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

class FirmwareOTA {
 public:
  FirmwareOTA(
    const char* host,
    uint16_t port,
    const char* manifestPath,
    const char* board,
    const char* currentVersion,
    const char* rootCA
  )
      : host_(host),
        port_(port),
        manifestPath_(manifestPath),
        board_(board),
        currentVersion_(currentVersion),
        rootCA_(rootCA) {}

  void loop() {
    if (WiFi.status() != WL_CONNECTED) return;

    const unsigned long now = millis();
    if (hasChecked_ && now - lastCheck_ < CHECK_INTERVAL_MS) return;

    time_t currentTime = 0;
    time(&currentTime);
    if (currentTime < 1735689600) {
      if (!hasClockWarning_ || now - lastClockWarning_ >= CLOCK_RETRY_INTERVAL_MS) {
        Serial.println("[OTA] Waiting for a valid clock before checking HTTPS release.");
        hasClockWarning_ = true;
        lastClockWarning_ = now;
      }
      return;
    }

    hasChecked_ = true;
    lastCheck_ = now;
    hasClockWarning_ = false;
    checkForUpdate();
  }

 private:
  static constexpr unsigned long CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
  static constexpr size_t MAX_MANIFEST_SIZE = 1024;
  static constexpr size_t DOWNLOAD_BUFFER_SIZE = 1024;
  static constexpr unsigned long DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
  static constexpr unsigned long CLOCK_RETRY_INTERVAL_MS = 60000;

  const char* host_;
  uint16_t port_;
  const char* manifestPath_;
  const char* board_;
  const char* currentVersion_;
  const char* rootCA_;
  bool hasChecked_ = false;
  unsigned long lastCheck_ = 0;
  bool hasClockWarning_ = false;
  unsigned long lastClockWarning_ = 0;

  String baseUrl() const {
    String url = "https://";
    url += host_;
    if (port_ != 443) {
      url += ':';
      url += String(port_);
    }
    return url;
  }

  static bool parseVersion(const char* text, uint32_t parts[3]) {
    if (text == nullptr || *text == '\0') return false;

    const char* cursor = text;
    for (size_t i = 0; i < 3; ++i) {
      if (!isdigit(static_cast<unsigned char>(*cursor))) return false;

      char* end = nullptr;
      const unsigned long value = strtoul(cursor, &end, 10);
      if (end == cursor || value > 65535UL) return false;
      parts[i] = static_cast<uint32_t>(value);
      cursor = end;

      if (i < 2) {
        if (*cursor != '.') return false;
        ++cursor;
      }
    }
    return *cursor == '\0';
  }

  static bool isVersion(const char* text) {
    uint32_t parts[3];
    return parseVersion(text, parts);
  }

  static int compareVersions(const char* left, const char* right) {
    uint32_t leftParts[3];
    uint32_t rightParts[3];
    if (!parseVersion(left, leftParts) || !parseVersion(right, rightParts)) return 0;

    for (size_t i = 0; i < 3; ++i) {
      if (leftParts[i] > rightParts[i]) return 1;
      if (leftParts[i] < rightParts[i]) return -1;
    }
    return 0;
  }

  static bool isSha256(const char* value) {
    if (value == nullptr || strlen(value) != 64) return false;
    for (size_t i = 0; i < 64; ++i) {
      if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
  }

  static bool isAllowedFirmwarePath(const char* path) {
    static const char allowedPrefix[] = "/firmware/esp32-ac-01/";
    if (path == nullptr || strncmp(path, allowedPrefix, sizeof(allowedPrefix) - 1) != 0) return false;
    if (strstr(path, "..") != nullptr || strchr(path, '?') != nullptr || strchr(path, '#') != nullptr) return false;
    return true;
  }

  bool checkForUpdate() {
    Serial.printf("[OTA] Current firmware: %s (%s)\n", currentVersion_, board_);

    WiFiClientSecure tlsClient;
    tlsClient.setCACert(rootCA_);
    HTTPClient http;

    const String manifestUrl = baseUrl() + manifestPath_;
    if (!http.begin(tlsClient, manifestUrl)) {
      Serial.println("[OTA] Could not start manifest request.");
      return false;
    }
    http.setTimeout(15000);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
      Serial.printf("[OTA] Manifest request failed: HTTP %d\n", statusCode);
      http.end();
      return false;
    }

    const int manifestSize = http.getSize();
    if (manifestSize <= 0 || static_cast<size_t>(manifestSize) > MAX_MANIFEST_SIZE) {
      Serial.println("[OTA] Manifest has an invalid content length.");
      http.end();
      return false;
    }

    const String manifestJson = http.getString();
    http.end();
    if (manifestJson.length() == 0 || manifestJson.length() > MAX_MANIFEST_SIZE) {
      Serial.println("[OTA] Manifest is empty or too large.");
      return false;
    }

    StaticJsonDocument<MAX_MANIFEST_SIZE> manifest;
    const DeserializationError jsonError = deserializeJson(manifest, manifestJson);
    if (jsonError) {
      Serial.printf("[OTA] Invalid manifest JSON: %s\n", jsonError.c_str());
      return false;
    }

    const char* manifestBoard = manifest["board"];
    const char* releaseName = manifest["name"];
    const char* latestVersion = manifest["version"];
    const bool enabled = manifest["enabled"] | false;
    if (manifestBoard == nullptr || strcmp(manifestBoard, board_) != 0) {
      Serial.println("[OTA] Manifest board does not match this device.");
      return false;
    }
    if (!isVersion(currentVersion_) || !isVersion(latestVersion)) {
      Serial.println("[OTA] Firmware version must use MAJOR.MINOR.PATCH format.");
      return false;
    }
    if (!enabled) {
      Serial.println("[OTA] No firmware release is enabled.");
      return false;
    }
    if (compareVersions(latestVersion, currentVersion_) <= 0) {
      Serial.printf("[OTA] Firmware is up to date (latest: %s).\n", latestVersion);
      return false;
    }

    const char* firmwarePath = manifest["firmware_path"];
    const char* expectedSha256 = manifest["sha256"];
    if (!isAllowedFirmwarePath(firmwarePath) || !isSha256(expectedSha256)) {
      Serial.println("[OTA] Manifest firmware path or SHA-256 is invalid.");
      return false;
    }

    String firmwareUrl = baseUrl();
    firmwareUrl += firmwarePath;
    Serial.printf("[OTA] Updating %s (version %s).\n", releaseName == nullptr ? "firmware" : releaseName, latestVersion);
    return downloadAndInstall(firmwareUrl, expectedSha256);
  }

  bool downloadAndInstall(const String& firmwareUrl, const char* expectedSha256) {
    WiFiClientSecure tlsClient;
    tlsClient.setCACert(rootCA_);
    HTTPClient http;

    if (!http.begin(tlsClient, firmwareUrl)) {
      Serial.println("[OTA] Could not start firmware download.");
      return false;
    }
    http.setTimeout(15000);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
      Serial.printf("[OTA] Firmware download failed: HTTP %d\n", statusCode);
      http.end();
      return false;
    }

    const int contentLength = http.getSize();
    if (contentLength <= 0) {
      Serial.println("[OTA] Firmware binary must include Content-Length.");
      http.end();
      return false;
    }
    if (!Update.begin(static_cast<size_t>(contentLength))) {
      const String error = Update.errorString();
      Serial.printf("[OTA] Could not reserve update partition: %s\n", error.c_str());
      http.end();
      return false;
    }

    auto* stream = http.getStreamPtr();
    if (stream == nullptr) {
      Serial.println("[OTA] Firmware response stream is unavailable.");
      Update.abort();
      http.end();
      return false;
    }

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    mbedtls_sha256_starts(&shaContext, 0);

    uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
    size_t written = 0;
    unsigned long lastProgress = millis();
    bool writeFailed = false;

    while (written < static_cast<size_t>(contentLength) && millis() - lastProgress < DOWNLOAD_IDLE_TIMEOUT_MS) {
      const int available = stream->available();
      if (available <= 0) {
        if (!http.connected()) break;
        delay(10);
        continue;
      }

      const size_t remaining = static_cast<size_t>(contentLength) - written;
      const size_t chunkSize = min(static_cast<size_t>(available), min(sizeof(buffer), remaining));
      const size_t bytesRead = stream->readBytes(buffer, chunkSize);
      if (bytesRead == 0) continue;

      if (Update.write(buffer, bytesRead) != bytesRead) {
        const String error = Update.errorString();
        Serial.printf("[OTA] Flash write failed: %s\n", error.c_str());
        writeFailed = true;
        break;
      }

      mbedtls_sha256_update(&shaContext, buffer, bytesRead);
      written += bytesRead;
      lastProgress = millis();
    }

    http.end();

    unsigned char digest[32];
    mbedtls_sha256_finish(&shaContext, digest);
    mbedtls_sha256_free(&shaContext);

    if (writeFailed || written != static_cast<size_t>(contentLength)) {
      Serial.printf("[OTA] Incomplete firmware download: %u/%d bytes.\n", static_cast<unsigned>(written), contentLength);
      Update.abort();
      return false;
    }

    char actualSha256[65];
    for (size_t i = 0; i < sizeof(digest); ++i) {
      snprintf(actualSha256 + i * 2, 3, "%02x", digest[i]);
    }
    actualSha256[64] = '\0';
    if (strcasecmp(actualSha256, expectedSha256) != 0) {
      Serial.println("[OTA] Firmware SHA-256 mismatch; update discarded.");
      Update.abort();
      return false;
    }

    if (!Update.end()) {
      const String error = Update.errorString();
      Serial.printf("[OTA] Firmware update could not be finalized: %s\n", error.c_str());
      return false;
    }
    if (!Update.isFinished()) {
      Serial.println("[OTA] Firmware update did not finish.");
      return false;
    }

    Serial.println("[OTA] Update installed. Restarting device.");
    delay(500);
    ESP.restart();
    return true;
  }
};
