#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DeviceConfig.h"
#include "FirmwareOTA.h"
#include "IRReceiverFeature.h"

constexpr int SDA_PIN = 8;
constexpr int SCL_PIN = 9;

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 32;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr size_t FRAME_SIZE = OLED_WIDTH * OLED_HEIGHT;
constexpr char FIRMWARE_VERSION[] = "1.0.4";
constexpr char FIRMWARE_BOARD[] = "esp32-s3";
constexpr unsigned long BOOT_VERSION_SCREEN_MS = 5000;

const char* DEVICE_ID = "esp32-ac-01";
const char* GATEWAY_HOST = "railway-websocket-production.up.railway.app";
constexpr uint16_t GATEWAY_PORT = 443;
const char* GATEWAY_PATH = "/ws";

// ISRG Root X1, the root CA for the current Let's Encrypt certificate served
// by the Railway domain. TLS certificate verification remains enabled.
static const char LETS_ENCRYPT_ROOT_CA[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
IRReceiverFeature irReceiver(display);
WebSocketsClient webSocket;
DeviceConfig deviceConfig;
FirmwareOTA firmwareOTA(
  GATEWAY_HOST,
  GATEWAY_PORT,
  "/firmware/esp32-ac-01/manifest.json",
  FIRMWARE_BOARD,
  FIRMWARE_VERSION,
  LETS_ENCRYPT_ROOT_CA
);

unsigned long lastWiFiAttempt = 0;
bool wasWiFiConnected = false;
bool gatewayOnline = false;
bool drawbookVisible = false;
bool holdBootVersionScreen = false;
bool versionScreenStarted = false;
unsigned long versionScreenStartedAt = 0;
bool pendingDrawFrame = false;
String pendingDrawPixels;

bool bootVersionScreenActive() {
  return holdBootVersionScreen ||
         (versionScreenStarted && millis() - versionScreenStartedAt < BOOT_VERSION_SCREEN_MS);
}

void showStatus(const char* line1, const char* line2 = "") {
  if (bootVersionScreenActive()) {
    irReceiver.invalidateDisplay();
    return;
  }

  drawbookVisible = false;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.setCursor(0, 12);
  display.println(line2);
  display.display();
  irReceiver.invalidateDisplay();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(deviceConfig.wifiSSID.c_str(), deviceConfig.wifiPassword.c_str());
  lastWiFiAttempt = millis();
  Serial.printf("Connecting to WiFi: %s\n", deviceConfig.wifiSSID.c_str());
  showStatus("Connecting WiFi", "Please wait...");
}

void syncClockForTLS() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Syncing clock for TLS certificate validation...");

  const unsigned long started = millis();
  time_t now = 0;
  while (now < 1735689600 && millis() - started < 15000) {
    time(&now);
    delay(250);
  }

  if (now >= 1735689600) {
    Serial.println("Clock synchronized.");
  } else {
    Serial.println("Clock not synchronized yet; TLS reconnect will retry.");
  }
}

// DIAGNOSTIC ADDED: test TLS independently from the WebSocket library.
void diagnoseGatewayTLS() {
  WiFiClientSecure probe;
  probe.setCACert(LETS_ENCRYPT_ROOT_CA);

  const int connected = probe.connect(GATEWAY_HOST, GATEWAY_PORT, 15000);
  if (connected) {
    Serial.println("[DIAG] Gateway TLS handshake succeeded.");
    probe.stop();
    return;
  }

  char error[160] = {0};
  const int errorCode = probe.lastError(error, sizeof(error));
  Serial.printf("[DIAG] Gateway TLS failed (%d): %s\n", errorCode, error);
  probe.stop();
}

void drawFrame(const String& pixels) {
  if (bootVersionScreenActive()) {
    pendingDrawPixels = pixels;
    pendingDrawFrame = true;
    Serial.println("Draw frame queued until startup version screen finishes.");
    return;
  }

  irReceiver.dismissCapture();
  drawbookVisible = true;
  display.clearDisplay();
  for (int y = 0; y < OLED_HEIGHT; ++y) {
    for (int x = 0; x < OLED_WIDTH; ++x) {
      const size_t index = static_cast<size_t>(y) * OLED_WIDTH + x;
      if (pixels[index] == '1') {
        display.drawPixel(x, y, SSD1306_WHITE);
      }
    }
  }
  display.display();
}

void handleWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      Serial.printf("Connected to WSS %s:%u%s\n", GATEWAY_HOST, GATEWAY_PORT, GATEWAY_PATH);
      gatewayOnline = false;
      showStatus("Gateway connected", "Registering...");

      String registration;
      registration.reserve(160);
      registration = "{\"type\":\"device_register\",\"device_id\":\"";
      registration += DEVICE_ID;
      registration += "\",\"token\":\"";
      registration += deviceConfig.deviceToken;
      registration += "\"}";
      webSocket.sendTXT(registration);
      break;
    }

    case WStype_DISCONNECTED:
      Serial.println("Disconnected from gateway; reconnect will be retried.");
      gatewayOnline = false;
      showStatus("Gateway offline", "Retrying...");
      break;

    case WStype_TEXT: {
      String message(reinterpret_cast<const char*>(payload), length);
      Serial.printf("Gateway message (%u bytes)\n", static_cast<unsigned>(length));

      if (message.indexOf("\"type\":\"error\"") >= 0) {
        Serial.println(message);
        gatewayOnline = false;
        showStatus("Gateway error", "Check token/logs");
        break;
      }

      if (message.indexOf("\"type\":\"device_status\"") >= 0) {
        if (message.indexOf("\"status\":\"online\"") >= 0) {
          gatewayOnline = true;
          Serial.println("Device registered and online.");
          showStatus("Gateway online", "Ready for Drawbook");
        } else {
          gatewayOnline = false;
          showStatus("Gateway offline", "Retrying...");
        }
        break;
      }

      if (message.indexOf("\"type\":\"draw\"") < 0) {
        Serial.println("Ignored unsupported gateway message.");
        break;
      }

      const String marker = "\"data\":\"";
      const int dataStart = message.indexOf(marker);
      if (dataStart < 0) {
        Serial.println("Draw message has no data field.");
        break;
      }

      const int pixelsStart = dataStart + marker.length();
      if (pixelsStart + static_cast<int>(FRAME_SIZE) >= message.length() ||
          message.charAt(pixelsStart + FRAME_SIZE) != '"') {
        Serial.println("Draw frame must contain exactly 4096 pixels.");
        break;
      }

      String pixels = message.substring(pixelsStart, pixelsStart + FRAME_SIZE);
      for (size_t i = 0; i < FRAME_SIZE; ++i) {
        if (pixels[i] != '0' && pixels[i] != '1') {
          Serial.println("Draw frame contains an invalid pixel value.");
          return;
        }
      }

      drawFrame(pixels);
      Serial.println("Draw frame displayed on OLED.");
      break;
    }

    case WStype_ERROR:
      Serial.println("WebSocket/TLS error.");
      break;

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed.");
    while (true) delay(1000);
  }
  irReceiver.begin(FIRMWARE_VERSION);
  irReceiver.showStartup(FIRMWARE_VERSION);
  holdBootVersionScreen = true;
  Serial.println("OLED: 128x32");
  Serial.println("OLED Address: 0x3C");

  if (!deviceConfig.load()) {
    holdBootVersionScreen = false;
    showStatus("Configure by USB", "Serial 115200");
    deviceConfig.provisionFromSerial();
  }

  connectWiFi();
  const unsigned long wifiStarted = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStarted < 20000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    wasWiFiConnected = true;
    Serial.print("WiFi connected; IP address: ");
    Serial.println(WiFi.localIP());
    syncClockForTLS();
    diagnoseGatewayTLS();
  } else {
    Serial.println("WiFi connection timed out; background retry will continue.");
  }

  webSocket.onEvent(handleWebSocketEvent);
  webSocket.setReconnectInterval(5000);
  // CHANGED: ESP32 is a device client; remove the library's default Origin: file://.
  webSocket.setExtraHeaders("");
  webSocket.beginSslWithCA(
    GATEWAY_HOST,
    GATEWAY_PORT,
    GATEWAY_PATH,
    LETS_ENCRYPT_ROOT_CA
  );

  versionScreenStartedAt = millis();
  versionScreenStarted = true;
  holdBootVersionScreen = false;
  irReceiver.showStartup(FIRMWARE_VERSION);
  Serial.printf("Startup OLED firmware version screen shown for %lu ms.\n",
                BOOT_VERSION_SCREEN_MS);
}

void loop() {
  const bool showVersionScreen = bootVersionScreenActive();
  if (irReceiver.poll(!showVersionScreen)) {
    drawbookVisible = false;
  }
  irReceiver.logStatusIfDue();

  webSocket.loop();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasWiFiConnected) {
      wasWiFiConnected = true;
      Serial.print("WiFi connected; IP address: ");
      Serial.println(WiFi.localIP());

      // TLS certificate validation needs a valid clock.
      syncClockForTLS();
      diagnoseGatewayTLS();
      showStatus("WiFi connected", "Connecting gateway");
    }
  } else {
    wasWiFiConnected = false;
    if (millis() - lastWiFiAttempt >= 15000) {
      Serial.println("WiFi disconnected; retrying.");
      WiFi.disconnect();
      WiFi.begin(deviceConfig.wifiSSID.c_str(), deviceConfig.wifiPassword.c_str());
      lastWiFiAttempt = millis();
      showStatus("WiFi offline", "Retrying...");
    }
  }

  // Check only after WiFi reconnect handling has had a chance to sync the clock for TLS.
  firmwareOTA.loop();

  if (!bootVersionScreenActive() && pendingDrawFrame) {
    String pixels = pendingDrawPixels;
    pendingDrawPixels = "";
    pendingDrawFrame = false;
    drawFrame(pixels);
  }

  irReceiver.updateDisplay(
    WiFi.status() == WL_CONNECTED,
    gatewayOnline,
    !drawbookVisible && !bootVersionScreenActive(),
    !bootVersionScreenActive()
  );
  delay(2);
}
