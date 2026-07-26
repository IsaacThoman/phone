/*
  Waveshare 2.13-inch e-Paper HAT V4 portrait typing demo

  The SSD1680 full- and partial-refresh sequences follow Waveshare's
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

constexpr int SCREEN_WIDTH = 122;
constexpr int SCREEN_HEIGHT = 250;
constexpr int BYTES_PER_ROW = (SCREEN_WIDTH + 7) / 8;
constexpr int FRAME_BYTES = BYTES_PER_ROW * SCREEN_HEIGHT;

constexpr uint32_t BUSY_TIMEOUT_MS = 20000;
constexpr uint32_t FULL_REFRESH_FALLBACK_MS = 5000;
constexpr uint32_t PARTIAL_REFRESH_FALLBACK_MS = 1000;
constexpr uint32_t PHRASE_HOLD_MS = 2500;
constexpr uint32_t CLEAR_HOLD_MS = 1500;

constexpr int FONT_SCALE = 1;
constexpr int GLYPH_WIDTH = 5 * FONT_SCALE;
constexpr int GLYPH_HEIGHT = 7 * FONT_SCALE;
constexpr int CHAR_ADVANCE = GLYPH_WIDTH + 1;
constexpr int LINE_ADVANCE = GLYPH_HEIGHT + 6;
constexpr int TEXT_TOP = 95;

const char TYPING_TEXT[] =
    "the quick\n"
    "brown fox\n"
    "jumps over\n"
    "the lazy\n"
    "dog.";

// Five columns per glyph, least-significant bit at the top. Only lowercase
// letters are needed for the pangram.
const uint8_t LOWERCASE_FONT[26][5] PROGMEM = {
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x7F, 0x48, 0x44, 0x44, 0x38},  // b
    {0x38, 0x44, 0x44, 0x44, 0x20},  // c
    {0x38, 0x44, 0x44, 0x48, 0x7F},  // d
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x08, 0x7E, 0x09, 0x01, 0x02},  // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E},  // g
    {0x7F, 0x08, 0x04, 0x04, 0x78},  // h
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // i
    {0x20, 0x40, 0x44, 0x3D, 0x00},  // j
    {0x7F, 0x10, 0x28, 0x44, 0x00},  // k
    {0x00, 0x41, 0x7F, 0x40, 0x00},  // l
    {0x7C, 0x04, 0x18, 0x04, 0x78},  // m
    {0x7C, 0x08, 0x04, 0x04, 0x78},  // n
    {0x38, 0x44, 0x44, 0x44, 0x38},  // o
    {0x7C, 0x14, 0x14, 0x14, 0x08},  // p
    {0x08, 0x14, 0x14, 0x18, 0x7C},  // q
    {0x7C, 0x08, 0x04, 0x04, 0x08},  // r
    {0x48, 0x54, 0x54, 0x54, 0x20},  // s
    {0x04, 0x3F, 0x44, 0x40, 0x20},  // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C},  // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C},  // w
    {0x44, 0x28, 0x10, 0x28, 0x44},  // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C},  // y
    {0x44, 0x64, 0x54, 0x4C, 0x44},  // z
};

uint8_t frameBuffer[FRAME_BYTES];
uint32_t cycleNumber = 0;
bool displayReady = false;

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

bool waitUntilIdle(const char* operation) {
  const uint32_t startedAt = millis();
  while (digitalRead(EPD_BUSY) == HIGH) {
    if (millis() - startedAt >= BUSY_TIMEOUT_MS) {
      Serial.printf("ERROR: BUSY timeout during %s\n", operation);
      return false;
    }
    delay(5);
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

bool waitForRefresh(const char* operation, uint32_t fallbackMs) {
  const uint32_t busyStartedAt = millis();
  while (digitalRead(EPD_BUSY) == LOW &&
         millis() - busyStartedAt < 100) {
    delay(1);
  }

  if (digitalRead(EPD_BUSY) == LOW) {
    Serial.printf("WARNING: BUSY did not assert during %s; delaying %lu ms\n",
                  operation, fallbackMs);
    delay(fallbackMs);
    return true;
  }
  return waitUntilIdle(operation);
}

bool initializeFullRefresh() {
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

  sendCommand(0x18);  // Built-in temperature sensor
  sendData(0x80);
  delay(100);
  return waitUntilIdle("full-refresh initialization");
}

void initializePartialRefresh() {
  digitalWrite(EPD_RST, LOW);
  delay(1);
  digitalWrite(EPD_RST, HIGH);
  delay(10);

  sendCommand(0x3C);  // Border waveform for partial updates
  sendData(0x80);

  sendCommand(0x01);
  sendData(0xF9);
  sendData(0x00);
  sendData(0x00);

  sendCommand(0x11);
  sendData(0x03);
  setWindowAndCursor();
}

void clearFrame() {
  memset(frameBuffer, 0xFF, sizeof(frameBuffer));
}

void setPixel(int x, int y, bool black = true) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }
  const int index = y * BYTES_PER_ROW + x / 8;
  const uint8_t mask = 0x80 >> (x % 8);
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

void drawGlyph(char character, int x, int y) {
  uint8_t columns[5] = {0, 0, 0, 0, 0};

  if (character >= 'a' && character <= 'z') {
    const int glyphIndex = character - 'a';
    for (int column = 0; column < 5; ++column) {
      columns[column] = pgm_read_byte(&LOWERCASE_FONT[glyphIndex][column]);
    }
  } else if (character == '.') {
    columns[1] = 0x60;
    columns[2] = 0x60;
  }

  for (int column = 0; column < 5; ++column) {
    for (int row = 0; row < 7; ++row) {
      if ((columns[column] & (1U << row)) == 0) {
        continue;
      }
      fillRect(x + column * FONT_SCALE, y + row * FONT_SCALE,
               FONT_SCALE, FONT_SCALE);
    }
  }
}

void drawCursor(int x, int y, bool black) {
  fillRect(x, y, 2, GLYPH_HEIGHT, black);
}

void writeFrameToRam(uint8_t command) {
  setWindowAndCursor();
  sendCommand(command);
  for (int i = 0; i < FRAME_BYTES; ++i) {
    sendData(frameBuffer[i]);
  }
}

bool fullRefreshWhite() {
  clearFrame();
  writeFrameToRam(0x24);  // Current image
  writeFrameToRam(0x26);  // Previous image for clean partial updates
  sendCommand(0x22);
  sendData(0xF7);
  sendCommand(0x20);
  return waitForRefresh("full white refresh", FULL_REFRESH_FALLBACK_MS);
}

bool partialRefresh(uint32_t stepNumber) {
  writeFrameToRam(0x24);
  sendCommand(0x22);
  sendData(0xFF);
  sendCommand(0x20);

  char operation[32];
  snprintf(operation, sizeof(operation), "partial step %lu", stepNumber);
  return waitForRefresh(operation, PARTIAL_REFRESH_FALLBACK_MS);
}

int lineWidth(const char* lineStart) {
  int characters = 0;
  while (lineStart[characters] != '\0' &&
         lineStart[characters] != '\n') {
    ++characters;
  }
  return characters == 0 ? 0 : characters * CHAR_ADVANCE - 1;
}

bool typePhrase() {
  int cursorY = TEXT_TOP;
  const char* lineStart = TYPING_TEXT;
  int cursorX = (SCREEN_WIDTH - lineWidth(lineStart)) / 2;
  int previousCursorX = cursorX;
  int previousCursorY = cursorY;
  bool cursorVisible = false;
  uint32_t visibleStep = 0;

  for (const char* character = TYPING_TEXT; *character != '\0'; ++character) {
    if (cursorVisible) {
      drawCursor(previousCursorX, previousCursorY, false);
      cursorVisible = false;
    }

    if (*character == '\n') {
      cursorY += LINE_ADVANCE;
      lineStart = character + 1;
      cursorX = (SCREEN_WIDTH - lineWidth(lineStart)) / 2;
      continue;
    }

    drawGlyph(*character, cursorX, cursorY);
    cursorX += CHAR_ADVANCE;
    drawCursor(cursorX, cursorY, true);
    previousCursorX = cursorX;
    previousCursorY = cursorY;
    cursorVisible = true;

    ++visibleStep;
    Serial.printf("Typing '%c' (%lu)\n", *character, visibleStep);
    if (!partialRefresh(visibleStep)) {
      return false;
    }
  }

  delay(700);
  if (cursorVisible) {
    drawCursor(previousCursorX, previousCursorY, false);
    if (!partialRefresh(++visibleStep)) {
      return false;
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Waveshare 2.13-inch V4 portrait typing demo");

  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT_PULLDOWN);
  digitalWrite(EPD_CS, HIGH);

  SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

  clearFrame();
}

void loop() {
  if (!displayReady &&
      (!initializeFullRefresh() || !fullRefreshWhite())) {
    Serial.println("Full-refresh setup failed; retrying in 5 seconds.");
    delay(5000);
    return;
  }
  displayReady = true;

  ++cycleNumber;
  Serial.printf("\n=== typing cycle %lu ===\n", cycleNumber);
  initializePartialRefresh();
  if (!typePhrase()) {
    Serial.println("Partial-refresh typing failed; retrying in 5 seconds.");
    displayReady = false;
    delay(5000);
    return;
  }

  Serial.println("Phrase complete.");
  delay(PHRASE_HOLD_MS);

  if (!initializeFullRefresh() || !fullRefreshWhite()) {
    Serial.println("Full clear failed; retrying in 5 seconds.");
    displayReady = false;
    delay(5000);
    return;
  }

  Serial.println("Full reset complete; starting again shortly.");
  delay(CLEAR_HOLD_MS);
}
