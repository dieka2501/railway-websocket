#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <stdio.h>
#include <string.h>

class IRReceiverFeature {
 public:
  static constexpr uint16_t RECEIVER_PIN = 7;
  static constexpr uint16_t CAPTURE_BUFFER_SIZE = 1024;
  static constexpr size_t MAX_RAW_TIMINGS = CAPTURE_BUFFER_SIZE - 1;

  explicit IRReceiverFeature(Adafruit_SSD1306& display)
      : display_(display), receiver_(RECEIVER_PIN, CAPTURE_BUFFER_SIZE, 50, true) {}

  void begin() {
#if DECODE_HASH
    receiver_.setUnknownThreshold(12);
#endif
    receiver_.enableIRIn();
    Serial.printf("HX1838 IR receiver started on GPIO %u.\n", RECEIVER_PIN);
    delay(10);
    Serial.printf("[IR] GPIO%u idle level: %s\n", RECEIVER_PIN,
                  digitalRead(RECEIVER_PIN) == HIGH ? "HIGH" : "LOW (check wiring)");
    Serial.println("[IR] Press a remote button; decoded frames print as IR RECEIVED.");
  }

  void showStartup(const char* firmwareVersion) {
    display_.clearDisplay();
    prepareDisplay();
    printLine(0, "AC REMOTE");
    printLine(8, "IR RECEIVER");
    printLine(16, "HX1838 GPIO7");

    char versionLine[22];
    snprintf(versionLine, sizeof(versionLine), "FW: %.15s", firmwareVersion);
    printLine(24, versionLine);
    display_.display();
    waitingRendered_ = false;
  }

  // Poll once on every main-loop pass. IRrecv copies its interrupt buffer and
  // resumes capture before decoding, so Wi-Fi/WebSocket work can keep running.
  bool poll(bool showIntro = true) {
    decode_results result;
    if (!receiver_.decode(&result)) return false;

    rawTotalCount_ = result.rawlen > 0 ? result.rawlen - 1 : 0;
    rawCount_ = rawTotalCount_ < MAX_RAW_TIMINGS ? rawTotalCount_ : MAX_RAW_TIMINGS;
    for (size_t i = 0; i < rawCount_; ++i) {
      rawTimings_[i] = result.rawbuf[i + 1];
    }

    protocol_ = result.decode_type;
    bits_ = result.decode_type == UNKNOWN ? 0 : result.bits;
    usesACState_ = hasACState(result.decode_type);
    address_ = usesACState_ ? 0 : result.address;
    command_ = usesACState_ ? 0 : result.command;
    overflow_ = result.overflow;
    const String protocolName = typeToString(protocol_);
    protocolName.toCharArray(protocolName_, sizeof(protocolName_));

    captured_ = true;
    captureStartedAt_ = millis();
    renderedCapturePage_ = -1;
    waitingRendered_ = false;
    if (showIntro) renderCaptureIntro();
    logCapture();
    return true;
  }

  void updateDisplay(bool wifiConnected, bool gatewayOnline, bool allowWaiting,
                     bool allowCaptureDisplay = true) {
    if (captured_) {
      if (!allowCaptureDisplay) return;
      const unsigned long elapsed = millis() - captureStartedAt_;
      if (elapsed < INTRO_DURATION_MS) return;

      const uint8_t page = static_cast<uint8_t>(((elapsed - INTRO_DURATION_MS) / PAGE_INTERVAL_MS) % 3);
      if (renderedCapturePage_ != page || displayDirty_) {
        if (page == 0) renderCaptureSummary();
        else renderRawPage(page - 1);
        renderedCapturePage_ = page;
      }
      return;
    }

    if (!allowWaiting) return;
    if (!waitingRendered_ || displayDirty_ ||
        wifiConnected != lastWaitingWifi_ || gatewayOnline != lastWaitingGateway_) {
      renderWaiting(wifiConnected, gatewayOnline);
    }
  }

  void invalidateDisplay() { displayDirty_ = true; }

  void dismissCapture() {
    captured_ = false;
    renderedCapturePage_ = -1;
    waitingRendered_ = false;
    displayDirty_ = true;
  }

  bool hasCapture() const { return captured_; }
  size_t rawTimingCount() const { return rawCount_; }
  size_t totalRawTimingCount() const { return rawTotalCount_; }
  const uint16_t* rawTimingTicks() const { return rawTimings_; }
  bool captureOverflowed() const { return overflow_; }
  const char* protocolName() const { return protocolName_; }
  uint16_t bitCount() const { return bits_; }

 private:
  static constexpr unsigned long INTRO_DURATION_MS = 500;
  static constexpr unsigned long PAGE_INTERVAL_MS = 2500;

  Adafruit_SSD1306& display_;
  IRrecv receiver_;
  uint16_t rawTimings_[MAX_RAW_TIMINGS] = {};
  size_t rawCount_ = 0;
  size_t rawTotalCount_ = 0;
  decode_type_t protocol_ = UNKNOWN;
  uint16_t bits_ = 0;
  uint32_t address_ = 0;
  uint32_t command_ = 0;
  bool usesACState_ = false;
  bool overflow_ = false;
  bool captured_ = false;
  bool waitingRendered_ = false;
  bool displayDirty_ = true;
  bool lastWaitingWifi_ = false;
  bool lastWaitingGateway_ = false;
  unsigned long captureStartedAt_ = 0;
  int8_t renderedCapturePage_ = -1;
  char protocolName_[32] = "UNKNOWN";

  void prepareDisplay() {
    display_.setTextSize(1);
    display_.setTextColor(SSD1306_WHITE);
    display_.setTextWrap(false);
  }

  void printLine(uint8_t y, const char* text) {
    display_.setCursor(0, y);
    display_.print(text);
  }

  void renderCaptureIntro() {
    display_.clearDisplay();
    prepareDisplay();
    printLine(0, "IR DETECTED");
    printLine(8, "Decoding...");
    printLine(16, "HX1838 GPIO7");
    printLine(24, "RAW CAPTURE ACTIVE");
    display_.display();
    displayDirty_ = false;
  }

  void renderWaiting(bool wifiConnected, bool gatewayOnline) {
    display_.clearDisplay();
    prepareDisplay();
    printLine(0, "IR READY");
    printLine(8, "Waiting...");
    printLine(16, "HX1838 OK GPIO7");
    printLine(24, wifiConnected ? (gatewayOnline ? "WiFi:ON GW:ON" : "WiFi:ON GW:OFF")
                                : "WiFi:OFF GW:OFF");
    display_.display();

    lastWaitingWifi_ = wifiConnected;
    lastWaitingGateway_ = gatewayOnline;
    waitingRendered_ = true;
    displayDirty_ = false;
  }

  void renderCaptureSummary() {
    display_.clearDisplay();
    prepareDisplay();

    char line[22];
    if (protocol_ == UNKNOWN) {
      printLine(0, "UNKNOWN IR");
      snprintf(line, sizeof(line), "RAW: %u", static_cast<unsigned>(rawTotalCount_));
      printLine(8, line);
      if (bits_ == 0) printLine(16, "BITS: --");
      else {
        snprintf(line, sizeof(line), "BITS: %u", bits_);
        printLine(16, line);
      }
      printLine(24, overflow_ ? "DATA OK BUF FULL" : "DATA OK");
    } else {
      snprintf(line, sizeof(line), "IR: %.17s", protocolName_);
      printLine(0, line);
      snprintf(line, sizeof(line), "BITS:%u RAW:%u", bits_, static_cast<unsigned>(rawTotalCount_));
      printLine(8, line);
      if (usesACState_) {
        printLine(16, "AC STATE CAPTURED");
        printLine(24, overflow_ ? "RAW OK BUF FULL" : "RAW DATA OK");
      } else {
        snprintf(line, sizeof(line), "ADDR:0x%lX", static_cast<unsigned long>(address_));
        printLine(16, line);
        snprintf(line, sizeof(line), "CMD:0x%lX", static_cast<unsigned long>(command_));
        printLine(24, line);
      }
    }
    display_.display();
    displayDirty_ = false;
  }

  void renderRawPage(uint8_t page) {
    display_.clearDisplay();
    prepareDisplay();

    char line[22];
    snprintf(line, sizeof(line), "RAW %u/%u TIMINGS", static_cast<unsigned>(page + 1), 2U);
    printLine(0, line);
    const size_t first = static_cast<size_t>(page) * 3;
    for (size_t row = 0; row < 3; ++row) {
      const size_t index = first + row;
      if (index >= rawCount_) break;
      const uint32_t microseconds = static_cast<uint32_t>(rawTimings_[index]) * kRawTick;
      snprintf(line, sizeof(line), "T%u: %lu us", static_cast<unsigned>(index),
               static_cast<unsigned long>(microseconds));
      printLine(static_cast<uint8_t>(8 + row * 8), line);
    }
    display_.display();
    displayDirty_ = false;
  }

  void logCapture() {
    Serial.println("IR RECEIVED");
    Serial.printf("Protocol: %s\n", protocolName_);
    Serial.printf("Bits: %u\n", bits_);
    Serial.printf("Raw length: %u\n", static_cast<unsigned>(rawTotalCount_));
    Serial.printf("Capture buffer: %s\n", overflow_ ? "OVERFLOW" : "OK");
    if (protocol_ != UNKNOWN && !usesACState_) {
      Serial.printf("Address: 0x%lX\nCommand: 0x%lX\n",
                    static_cast<unsigned long>(address_),
                    static_cast<unsigned long>(command_));
    }
    const size_t previewCount = rawCount_ < 6 ? rawCount_ : 6;
    Serial.print("Raw timings preview (us):");
    for (size_t i = 0; i < previewCount; ++i) {
      Serial.printf(" %lu", static_cast<unsigned long>(rawTimings_[i]) * kRawTick);
    }
    Serial.println();
  }
};
