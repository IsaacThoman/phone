/*
  Waveshare 2.13-inch e-Paper HAT V4 pattern test

  The SSD1680 initialization and full-refresh sequence follows Waveshare's
  MIT-licensed epd2in13_V4 Arduino example:
  https://github.com/waveshareteam/e-Paper/tree/master/Arduino/epd2in13_V4
*/

#include <Arduino.h>
#include <SPI.h>

constexpr int EPD_MOSI = 11;  // DIN
constexpr int EPD_SCLK = 12;  // CLK
constexpr int EPD_CS = 10;
constexpr int EPD_DC = 9;
constexpr int EPD_RST = 8;
constexpr int EPD_BUSY = 13;

constexpr int NATIVE_WIDTH = 122;
constexpr int NATIVE_HEIGHT = 250;
constexpr int BYTES_PER_ROW = (NATIVE_WIDTH + 7) / 8;
constexpr int FRAME_BYTES = BYTES_PER_ROW * NATIVE_HEIGHT;

// Landscape drawing coordinates after a 90-degree clockwise rotation.
constexpr int SCREEN_WIDTH = NATIVE_HEIGHT;
constexpr int SCREEN_HEIGHT = NATIVE_WIDTH;
constexpr uint32_t BUSY_TIMEOUT_MS = 20000;
constexpr uint32_t REFRESH_FALLBACK_MS = 5000;

uint8_t frameBuffer[FRAME_BYTES];

void transferByte(uint8_t value) {
  digitalWrite(EPD_CS, LOW);
  SPI.transfer(value);
  digitalWrite(EPD_CS, HIGH);
}

void sendCommand(uint8_t command) {
  digitalWrite(EPD_DC, LOW);
  transferByte(command);
}

void sendData(uint8_t data) {
  digitalWrite(EPD_DC, HIGH);
  transferByte(data);
}

bool waitUntilIdle(const char* operation, bool* sawBusy = nullptr) {
  const uint32_t startedAt = millis();
  bool busyObserved = false;

  while (digitalRead(EPD_BUSY) == HIGH) {
    busyObserved = true;
    if (millis() - startedAt >= BUSY_TIMEOUT_MS) {
      Serial.printf("ERROR: BUSY timeout during %s\n", operation);
      if (sawBusy != nullptr) {
        *sawBusy = busyObserved;
      }
      return false;
    }
    delay(10);
  }

  if (sawBusy != nullptr) {
    *sawBusy = busyObserved;
  }
  Serial.printf("%s complete in %lu ms\n", operation, millis() - startedAt);
  return true;
}

void hardwareReset() {
  digitalWrite(EPD_RST, HIGH);
  delay(20);
  digitalWrite(EPD_RST, LOW);
  delay(2);
  digitalWrite(EPD_RST, HIGH);
  delay(20);
  Serial.printf("BUSY after hardware reset: %s\n",
                digitalRead(EPD_BUSY) ? "HIGH" : "LOW");
}

void setWindowAndCursor() {
  sendCommand(0x44);  // RAM X start/end, in bytes
  sendData(0x00);
  sendData(0x0F);

  sendCommand(0x45);  // RAM Y start/end
  sendData(0x00);
  sendData(0x00);
  sendData(0xF9);
  sendData(0x00);

  sendCommand(0x4E);  // RAM X counter
  sendData(0x00);

  sendCommand(0x4F);  // RAM Y counter
  sendData(0x00);
  sendData(0x00);
}

bool initializeDisplay() {
  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  // The weak pull-down makes a disconnected BUSY lead read LOW instead of
  // floating HIGH. A connected SSD1680 output easily overrides it.
  pinMode(EPD_BUSY, INPUT_PULLDOWN);
  digitalWrite(EPD_CS, HIGH);
  Serial.printf("BUSY after GPIO setup: %s\n",
                digitalRead(EPD_BUSY) ? "HIGH" : "LOW");

  SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

  hardwareReset();
  if (!waitUntilIdle("hardware reset")) {
    return false;
  }

  sendCommand(0x12);  // Software reset
  delay(10);
  if (!waitUntilIdle("software reset")) {
    return false;
  }

  sendCommand(0x01);  // Driver output control: 250 gates
  sendData(0xF9);
  sendData(0x00);
  sendData(0x00);

  sendCommand(0x11);  // Increment X, then Y
  sendData(0x03);

  setWindowAndCursor();

  sendCommand(0x3C);  // Border waveform
  sendData(0x05);

  sendCommand(0x21);  // Display update control
  sendData(0x00);
  sendData(0x80);

  sendCommand(0x18);  // Use the built-in temperature sensor
  sendData(0x80);
  delay(100);

  return waitUntilIdle("display initialization");
}

void clearFrame() {
  memset(frameBuffer, 0xFF, sizeof(frameBuffer));
}

void setPixel(int x, int y, bool black = true) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }

  // Rotate landscape coordinates clockwise into the panel's native 122x250
  // portrait memory layout.
  const int nativeX = y;
  const int nativeY = NATIVE_HEIGHT - 1 - x;
  const int index = nativeY * BYTES_PER_ROW + nativeX / 8;
  const uint8_t mask = 0x80 >> (nativeX % 8);

  if (black) {
    frameBuffer[index] &= ~mask;
  } else {
    frameBuffer[index] |= mask;
  }
}

void fillRect(int x, int y, int width, int height, bool black = true) {
  for (int py = y; py < y + height; ++py) {
    for (int px = x; px < x + width; ++px) {
      setPixel(px, py, black);
    }
  }
}

void drawRect(int x, int y, int width, int height, int thickness = 1) {
  fillRect(x, y, width, thickness);
  fillRect(x, y + height - thickness, width, thickness);
  fillRect(x, y, thickness, height);
  fillRect(x + width - thickness, y, thickness, height);
}

void drawLine(int x0, int y0, int x1, int y1) {
  const int dx = abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while (true) {
    setPixel(x0, y0);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int doubledError = 2 * error;
    if (doubledError >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubledError <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void drawCircle(int centerX, int centerY, int radius) {
  int x = radius;
  int y = 0;
  int error = 1 - radius;

  while (x >= y) {
    setPixel(centerX + x, centerY + y);
    setPixel(centerX + y, centerY + x);
    setPixel(centerX - y, centerY + x);
    setPixel(centerX - x, centerY + y);
    setPixel(centerX - x, centerY - y);
    setPixel(centerX - y, centerY - x);
    setPixel(centerX + y, centerY - x);
    setPixel(centerX + x, centerY - y);
    ++y;
    if (error < 0) {
      error += 2 * y + 1;
    } else {
      --x;
      error += 2 * (y - x + 1);
    }
  }
}

void drawCheckerBand(int y) {
  constexpr int CELL = 10;
  for (int x = 0; x < SCREEN_WIDTH; x += CELL) {
    if ((x / CELL) % 2 == 0) {
      fillRect(x, y, CELL, CELL);
    }
  }
}

void drawPattern() {
  clearFrame();

  drawCheckerBand(0);
  drawCheckerBand(SCREEN_HEIGHT - 10);
  drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 2);
  drawRect(14, 14, SCREEN_WIDTH - 28, SCREEN_HEIGHT - 28, 2);

  const int centerX = SCREEN_WIDTH / 2;
  const int centerY = SCREEN_HEIGHT / 2;
  drawCircle(centerX, centerY, 34);
  drawCircle(centerX, centerY, 22);
  drawCircle(centerX, centerY, 10);
  fillRect(centerX - 3, centerY - 3, 7, 7);

  for (int x = 24; x <= SCREEN_WIDTH - 24; x += 25) {
    drawLine(centerX, centerY, x, 18);
    drawLine(centerX, centerY, x, SCREEN_HEIGHT - 19);
  }

  // Solid corner markers make orientation and clipping easy to spot.
  fillRect(17, 17, 12, 12);
  fillRect(SCREEN_WIDTH - 29, SCREEN_HEIGHT - 29, 12, 12);
  drawRect(SCREEN_WIDTH - 29, 17, 12, 12, 2);
  drawRect(17, SCREEN_HEIGHT - 29, 12, 12, 2);
}

bool displayFrame() {
  setWindowAndCursor();
  sendCommand(0x24);  // Write black/white image RAM
  for (int i = 0; i < FRAME_BYTES; ++i) {
    sendData(frameBuffer[i]);
  }

  Serial.println("Starting full e-paper refresh...");
  sendCommand(0x22);
  sendData(0xF7);
  sendCommand(0x20);

  // BUSY normally rises almost immediately. Give it a short window to assert
  // before falling back to a conservative delay for a missing BUSY lead.
  const uint32_t busyStartedAt = millis();
  while (digitalRead(EPD_BUSY) == LOW &&
         millis() - busyStartedAt < 100) {
    delay(1);
  }

  if (digitalRead(EPD_BUSY) == LOW) {
    Serial.println(
        "WARNING: no HIGH pulse observed on BUSY (check GPIO13 wiring)");
    Serial.printf("Using %lu ms refresh fallback delay...\n",
                  REFRESH_FALLBACK_MS);
    delay(REFRESH_FALLBACK_MS);
    Serial.println("Fallback refresh delay complete.");
    return true;
  }

  bool sawBusy = false;
  const bool completed = waitUntilIdle("full refresh", &sawBusy);
  return completed;
}

void sleepDisplay() {
  sendCommand(0x10);
  sendData(0x01);
  delay(200);
  digitalWrite(EPD_RST, LOW);
  SPI.endTransaction();
  Serial.println("Display asleep; the image will remain without power.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Waveshare 2.13-inch V4 e-paper pattern test");

  drawPattern();
  if (!initializeDisplay()) {
    Serial.println("Display initialization failed.");
    return;
  }
  if (!displayFrame()) {
    Serial.println("Display refresh failed.");
    return;
  }

  sleepDisplay();
  Serial.println("PATTERN TEST COMPLETE");
}

void loop() {
  delay(1000);
}
