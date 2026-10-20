#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

class DeviceConfig {
 public:
  String wifiSSID;
  String wifiPassword;
  String deviceToken;

  bool load() {
    Preferences preferences;
    if (!preferences.begin("drawbook", true)) return false;

    wifiSSID = preferences.getString("ssid", "");
    wifiPassword = preferences.getString("password", "");
    deviceToken = preferences.getString("token", "");
    preferences.end();
    return isValid();
  }

  void provisionFromSerial() {
    Serial.println("[SETUP] No device configuration found in NVS.");
    Serial.println("[SETUP] At 115200 baud, send one JSON line in this format:");
    Serial.println("{\"ssid\":\"YOUR_WIFI_SSID\",\"password\":\"YOUR_WIFI_PASSWORD\",\"device_token\":\"YOUR_RAILWAY_DEVICE_TOKEN\"}");

    while (true) {
      if (!Serial.available()) {
        delay(25);
        continue;
      }

      String line = Serial.readStringUntil('\n');
      line.trim();

      StaticJsonDocument<512> document;
      const DeserializationError error = deserializeJson(document, line);
      if (error) {
        Serial.println("[SETUP] Invalid JSON. Send the configuration as one JSON line.");
        continue;
      }

      const char* ssid = document["ssid"];
      const char* password = document["password"];
      const char* token = document["device_token"];
      if (ssid == nullptr || password == nullptr || token == nullptr ||
          strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(password) > 64 ||
          strlen(token) == 0 || strlen(token) > 128) {
        Serial.println("[SETUP] Missing or invalid fields. Check SSID, password, and device_token.");
        continue;
      }

      Preferences preferences;
      if (!preferences.begin("drawbook", false)) {
        Serial.println("[SETUP] Could not open NVS for writing.");
        continue;
      }
      const size_t ssidBytes = preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      const size_t tokenBytes = preferences.putString("token", token);
      preferences.end();

      if (ssidBytes == 0 || tokenBytes == 0) {
        Serial.println("[SETUP] Could not save configuration to NVS.");
        continue;
      }

      Serial.println("[SETUP] Configuration saved in NVS. Restarting device.");
      delay(500);
      ESP.restart();
    }
  }

 private:
  bool isValid() const {
    return wifiSSID.length() > 0 && wifiSSID.length() <= 32 &&
           wifiPassword.length() <= 64 && deviceToken.length() > 0 &&
           deviceToken.length() <= 128;
  }
};
