/*
  Cellular message phone

  Polls the Signal Note service over the onboard SIM7670G every 30 seconds,
  renders changed messages on a Waveshare 2.13-inch V4 e-paper HAT, and reports
  non-sensitive connection telemetry to the web dashboard.
*/

#include <Arduino.h>
#include <SPI.h>

#include "font5x7.h"

constexpr char BASE_URL[] = "https://phone-msg-testserver.deathgrips.org";
constexpr char CELLULAR_APN[] = "hologram";
constexpr char FIRMWARE_VERSION[] = "1.0.0";
constexpr char DEVICE_ID[] = "phone-01";
constexpr uint32_t POLL_INTERVAL_MS = 30000;

constexpr int MODEM_RX = 17;
constexpr int MODEM_TX = 18;
constexpr int MODEM_BAUD = 115200;

constexpr int EPD_MOSI = 11;
constexpr int EPD_SCLK = 12;
constexpr int EPD_CS = 10;
constexpr int EPD_DC = 9;
constexpr int EPD_RST = 8;
constexpr int EPD_BUSY = 13;

constexpr int SCREEN_WIDTH = 122;
constexpr int SCREEN_HEIGHT = 250;
constexpr int BYTES_PER_ROW = (SCREEN_WIDTH + 7) / 8;
constexpr int FRAME_BYTES = BYTES_PER_ROW * SCREEN_HEIGHT;
constexpr int GLYPH_WIDTH = 5;
constexpr int GLYPH_HEIGHT = 7;
constexpr int CHARACTER_ADVANCE = 6;
constexpr int LINE_ADVANCE = 13;
constexpr int MAX_CHARACTERS_PER_LINE =
    (SCREEN_WIDTH + 1) / CHARACTER_ADVANCE;
constexpr int MAX_LINES =
    (SCREEN_HEIGHT - GLYPH_HEIGHT) / LINE_ADVANCE + 1;

constexpr uint32_t MODEM_COMMAND_TIMEOUT_MS = 5000;
constexpr uint32_t HTTP_TIMEOUT_MS = 120000;
constexpr uint32_t EPD_BUSY_TIMEOUT_MS = 20000;

HardwareSerial SerialAT(1);
uint8_t frameBuffer[FRAME_BYTES];
int lastMessageRevision = 0;
uint32_t nextPollAt = 0;
String lastLocalStatus;

struct ConnectionStatus {
  int rssiDbm = 0;
  int signalPercent = 0;
  bool hasSignal = false;
  String operatorName;
  String networkType;
  String ipAddress;
};

void drainModem() {
  while (SerialAT.available()) {
    SerialAT.read();
  }
}

bool readUntil(const String& token, uint32_t timeoutMs, String& response) {
  const uint32_t startedAt = millis();
  response = "";
  while (millis() - startedAt < timeoutMs) {
    while (SerialAT.available()) {
      const char value = static_cast<char>(SerialAT.read());
      response += value;
      if (response.length() > 8192) {
        response.remove(0, response.length() - 8192);
      }
      if (response.indexOf(token) >= 0) {
        return true;
      }
    }
    delay(2);
  }
  return false;
}

bool sendAT(const String& command, const String& expected, uint32_t timeoutMs,
            String& response) {
  drainModem();
  Serial.printf("MODEM > %s\n", command.c_str());
  SerialAT.print(command);
  SerialAT.print("\r");
  const bool found = readUntil(expected, timeoutMs, response);
  Serial.printf("MODEM < %s\n", response.c_str());
  return found && response.indexOf("\r\nERROR\r\n") < 0;
}

bool sendAT(const String& command, const String& expected = "\r\nOK\r\n",
            uint32_t timeoutMs = MODEM_COMMAND_TIMEOUT_MS) {
  String response;
  return sendAT(command, expected, timeoutMs, response);
}

bool waitForModem() {
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (sendAT("AT")) {
      sendAT("ATE0");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool waitForNetwork() {
  for (int attempt = 0; attempt < 60; ++attempt) {
    String response;
    if (sendAT("AT+CPSI?", "\r\nOK\r\n", 10000, response) &&
        response.indexOf(",Online,") >= 0) {
      return true;
    }
    Serial.println("Waiting for cellular registration...");
    delay(2000);
  }
  return false;
}

bool configureCellular() {
  // Hologram requires APN "hologram", no credentials, and roaming. Automatic
  // operator selection allows the SIM to register with a supported partner.
  if (!sendAT("AT+CGDCONT=1,\"IP\",\"" + String(CELLULAR_APN) + "\"",
              "\r\nOK\r\n", 10000)) {
    return false;
  }
  if (!sendAT("AT+COPS=0", "\r\nOK\r\n", HTTP_TIMEOUT_MS)) {
    return false;
  }
  return sendAT("AT+CGATT=1", "\r\nOK\r\n", HTTP_TIMEOUT_MS);
}

String valueAfterPrefix(const String& response, const String& prefix) {
  const int start = response.indexOf(prefix);
  if (start < 0) {
    return "";
  }
  int end = response.indexOf("\r", start + prefix.length());
  if (end < 0) {
    end = response.length();
  }
  String value = response.substring(start + prefix.length(), end);
  value.trim();
  return value;
}

String quotedField(const String& value) {
  const int firstQuote = value.indexOf('"');
  const int secondQuote = value.indexOf('"', firstQuote + 1);
  if (firstQuote < 0 || secondQuote < 0) {
    return "";
  }
  return value.substring(firstQuote + 1, secondQuote);
}

ConnectionStatus readConnectionStatus() {
  ConnectionStatus status;
  String response;

  if (sendAT("AT+CSQ", "\r\nOK\r\n", 5000, response)) {
    const String value = valueAfterPrefix(response, "+CSQ:");
    const int raw = value.substring(0, value.indexOf(',')).toInt();
    if (raw >= 0 && raw <= 31) {
      status.hasSignal = true;
      status.rssiDbm = -113 + 2 * raw;
      status.signalPercent = (raw * 100 + 15) / 31;
    }
  }

  if (sendAT("AT+COPS?", "\r\nOK\r\n", 10000, response)) {
    status.operatorName = quotedField(valueAfterPrefix(response, "+COPS:"));
  }

  if (sendAT("AT+CPSI?", "\r\nOK\r\n", 10000, response)) {
    const String value = valueAfterPrefix(response, "+CPSI:");
    const int comma = value.indexOf(',');
    status.networkType = comma < 0 ? value : value.substring(0, comma);
  }

  if (sendAT("AT+CGPADDR=1", "\r\nOK\r\n", 10000, response)) {
    const String value = valueAfterPrefix(response, "+CGPADDR:");
    const int comma = value.indexOf(',');
    if (comma >= 0) {
      status.ipAddress = value.substring(comma + 1);
      status.ipAddress.replace("\"", "");
    }
  }

  return status;
}

bool parseHttpAction(const String& response, int method, int& statusCode,
                     int& bodyLength) {
  const String marker = "+HTTPACTION: " + String(method) + ",";
  const int start = response.indexOf(marker);
  if (start < 0) {
    return false;
  }
  const int statusStart = start + marker.length();
  const int comma = response.indexOf(',', statusStart);
  const int end = response.indexOf('\r', comma + 1);
  if (comma < 0) {
    return false;
  }
  statusCode = response.substring(statusStart, comma).toInt();
  bodyLength =
      response.substring(comma + 1, end < 0 ? response.length() : end).toInt();
  return true;
}

bool beginHttp() {
  sendAT("AT+HTTPTERM");
  if (!sendAT("AT+CSSLCFG=\"sslversion\",0,4", "\r\nOK\r\n", 10000)) {
    return false;
  }
  // Certificate verification is disabled for this prototype because the modem
  // has no CA bundle installed. The transport is still encrypted.
  if (!sendAT("AT+CSSLCFG=\"authmode\",0,0", "\r\nOK\r\n", 10000)) {
    return false;
  }
  // The reverse proxy hosts multiple HTTPS domains on one address, so the
  // modem must send the requested hostname during the TLS handshake.
  if (!sendAT("AT+CSSLCFG=\"enableSNI\",0,1", "\r\nOK\r\n", 10000)) {
    return false;
  }
  if (!sendAT("AT+HTTPINIT", "\r\nOK\r\n", HTTP_TIMEOUT_MS)) {
    return false;
  }
  return sendAT("AT+HTTPPARA=\"SSLCFG\",0", "\r\nOK\r\n", 10000);
}

bool setHttpUrl(const String& path) {
  return sendAT("AT+HTTPPARA=\"URL\",\"" + String(BASE_URL) + path + "\"",
                "\r\nOK\r\n", 10000);
}

bool performHttpAction(int method, int& statusCode, int& bodyLength) {
  drainModem();
  const String command = "AT+HTTPACTION=" + String(method);
  Serial.printf("MODEM > %s\n", command.c_str());
  SerialAT.print(command);
  SerialAT.print("\r");
  String response;
  if (!readUntil("+HTTPACTION:", HTTP_TIMEOUT_MS, response)) {
    return false;
  }
  String tail;
  readUntil("\r\n", 1000, tail);
  response += tail;
  Serial.printf("MODEM < %s\n", response.c_str());
  return parseHttpAction(response, method, statusCode, bodyLength);
}

bool readHttpBody(int bodyLength, String& body) {
  String response;
  if (!sendAT("AT+HTTPREAD=0," + String(bodyLength), "+HTTPREAD:",
              HTTP_TIMEOUT_MS, response)) {
    return false;
  }

  String remainder;
  readUntil("+HTTPREAD: 0", HTTP_TIMEOUT_MS, remainder);
  response += remainder;
  Serial.printf("HTTP BODY RAW: %s\n", response.c_str());

  const int jsonStart = response.indexOf('{');
  const int jsonEnd = response.lastIndexOf('}');
  if (jsonStart < 0 || jsonEnd < jsonStart) {
    return false;
  }
  body = response.substring(jsonStart, jsonEnd + 1);
  return true;
}

bool getMessage(String& message, int& revision) {
  if (!setHttpUrl("/api/message") ||
      !sendAT("AT+HTTPPARA=\"ACCEPT\",\"application/json\"")) {
    return false;
  }

  int statusCode = 0;
  int bodyLength = 0;
  if (!performHttpAction(0, statusCode, bodyLength) || statusCode != 200 ||
      bodyLength <= 0) {
    Serial.printf("Message GET failed: HTTP %d, length %d\n", statusCode,
                  bodyLength);
    return false;
  }

  String body;
  if (!readHttpBody(bodyLength, body)) {
    return false;
  }

  const String messageKey = "\"message\":\"";
  int position = body.indexOf(messageKey);
  if (position < 0) {
    return false;
  }
  position += messageKey.length();
  message = "";
  bool escaped = false;
  for (; position < static_cast<int>(body.length()); ++position) {
    const char value = body[position];
    if (escaped) {
      if (value == 'n') {
        message += '\n';
      } else if (value == 'r') {
        message += '\r';
      } else if (value == 't') {
        message += '\t';
      } else {
        message += value;
      }
      escaped = false;
    } else if (value == '\\') {
      escaped = true;
    } else if (value == '"') {
      break;
    } else {
      message += value;
    }
  }

  const String revisionKey = "\"revision\":";
  position = body.indexOf(revisionKey);
  if (position < 0) {
    return false;
  }
  revision = body.substring(position + revisionKey.length()).toInt();
  return revision > 0;
}

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\r", "");
  value.replace("\n", " ");
  return value;
}

bool postTelemetry(const ConnectionStatus& connection, bool pollOk,
                   bool displayUpdated, const String& lastError) {
  String body = "{\"deviceId\":\"" + String(DEVICE_ID) + "\"";
  body += ",\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\"";
  body += ",\"uptimeSeconds\":" + String(millis() / 1000);
  body += ",\"signalRssiDbm\":";
  body += connection.hasSignal ? String(connection.rssiDbm) : "null";
  body += ",\"signalPercent\":";
  body += connection.hasSignal ? String(connection.signalPercent) : "null";
  body += ",\"operator\":\"" + jsonEscape(connection.operatorName) + "\"";
  body += ",\"networkType\":\"" + jsonEscape(connection.networkType) + "\"";
  body += ",\"ipAddress\":\"" + jsonEscape(connection.ipAddress) + "\"";
  body += ",\"lastMessageRevision\":" + String(lastMessageRevision);
  body += ",\"displayUpdated\":" + String(displayUpdated ? "true" : "false");
  body += ",\"lastPollOk\":" + String(pollOk ? "true" : "false");
  body += ",\"lastError\":\"" + jsonEscape(lastError) + "\"}";

  if (!setHttpUrl("/api/device/status") ||
      !sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"")) {
    return false;
  }

  String response;
  if (!sendAT("AT+HTTPDATA=" + String(body.length()) + ",30", "DOWNLOAD",
              10000, response)) {
    return false;
  }
  Serial.printf("TELEMETRY > %s\n", body.c_str());
  SerialAT.print(body);
  if (!readUntil("\r\nOK\r\n", 35000, response)) {
    return false;
  }

  int statusCode = 0;
  int bodyLength = 0;
  const bool actionOk = performHttpAction(1, statusCode, bodyLength);
  Serial.printf("Telemetry POST: HTTP %d\n", statusCode);
  return actionOk && statusCode >= 200 && statusCode < 300;
}

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

bool waitUntilIdle() {
  const uint32_t startedAt = millis();
  while (digitalRead(EPD_BUSY) == HIGH) {
    if (millis() - startedAt >= EPD_BUSY_TIMEOUT_MS) {
      return false;
    }
    delay(5);
  }
  return true;
}

void setWindowAndCursor() {
  sendCommand(0x44);
  sendData(0x00);
  sendData(0x0F);
  sendCommand(0x45);
  sendData(0x00);
  sendData(0x00);
  sendData(0xF9);
  sendData(0x00);
  sendCommand(0x4E);
  sendData(0x00);
  sendCommand(0x4F);
  sendData(0x00);
  sendData(0x00);
}

bool initializeDisplay() {
  digitalWrite(EPD_RST, HIGH);
  delay(20);
  digitalWrite(EPD_RST, LOW);
  delay(2);
  digitalWrite(EPD_RST, HIGH);
  delay(20);
  if (!waitUntilIdle()) {
    return false;
  }

  sendCommand(0x12);
  delay(10);
  if (!waitUntilIdle()) {
    return false;
  }
  sendCommand(0x01);
  sendData(0xF9);
  sendData(0x00);
  sendData(0x00);
  sendCommand(0x11);
  sendData(0x03);
  setWindowAndCursor();
  sendCommand(0x3C);
  sendData(0x05);
  sendCommand(0x21);
  sendData(0x00);
  sendData(0x80);
  sendCommand(0x18);
  sendData(0x80);
  delay(100);
  return waitUntilIdle();
}

void setPixel(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }
  frameBuffer[y * BYTES_PER_ROW + x / 8] &= ~(0x80 >> (x % 8));
}

void drawGlyph(char character, int x, int y) {
  if (character < 32 || character > 126) {
    character = '?';
  }
  const int glyphIndex = character - 32;
  for (int column = 0; column < GLYPH_WIDTH; ++column) {
    const uint8_t pixels =
        pgm_read_byte(&PHONE_FONT_5X7[glyphIndex][column]);
    for (int row = 0; row < GLYPH_HEIGHT; ++row) {
      if (pixels & (1U << row)) {
        setPixel(x + column, y + row);
      }
    }
  }
}

int wrapMessage(const String& message, String lines[MAX_LINES]) {
  int lineCount = 0;
  int paragraphStart = 0;
  while (paragraphStart <= static_cast<int>(message.length()) &&
         lineCount < MAX_LINES) {
    int newline = message.indexOf('\n', paragraphStart);
    if (newline < 0) {
      newline = message.length();
    }
    String remaining = message.substring(paragraphStart, newline);
    if (remaining.length() == 0) {
      lines[lineCount++] = "";
    } else {
      while (remaining.length() > MAX_CHARACTERS_PER_LINE &&
             lineCount < MAX_LINES) {
        int breakAt = remaining.lastIndexOf(' ', MAX_CHARACTERS_PER_LINE);
        if (breakAt <= 0) {
          breakAt = MAX_CHARACTERS_PER_LINE;
        }
        String line = remaining.substring(0, breakAt);
        line.trim();
        lines[lineCount++] = line;
        remaining = remaining.substring(breakAt);
        remaining.trim();
      }
      if (lineCount < MAX_LINES) {
        lines[lineCount++] = remaining;
      }
    }
    if (newline >= static_cast<int>(message.length())) {
      break;
    }
    paragraphStart = newline + 1;
  }
  return lineCount;
}

void drawMessage(const String& message) {
  memset(frameBuffer, 0xFF, sizeof(frameBuffer));
  String lines[MAX_LINES];
  const int lineCount = wrapMessage(message, lines);
  const int blockHeight =
      GLYPH_HEIGHT + max(0, lineCount - 1) * LINE_ADVANCE;
  const int startY = (SCREEN_HEIGHT - blockHeight) / 2;
  for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
    const int lineWidth =
        lines[lineIndex].length() == 0
            ? 0
            : lines[lineIndex].length() * CHARACTER_ADVANCE - 1;
    int x = (SCREEN_WIDTH - lineWidth) / 2;
    const int y = startY + lineIndex * LINE_ADVANCE;
    for (char character : lines[lineIndex]) {
      drawGlyph(character, x, y);
      x += CHARACTER_ADVANCE;
    }
  }
}

void writeFrameToRam(uint8_t command) {
  setWindowAndCursor();
  sendCommand(command);
  for (int index = 0; index < FRAME_BYTES; ++index) {
    sendData(frameBuffer[index]);
  }
}

bool refreshDisplay(const String& message) {
  if (!initializeDisplay()) {
    return false;
  }
  drawMessage(message);
  writeFrameToRam(0x24);
  writeFrameToRam(0x26);
  sendCommand(0x22);
  sendData(0xF7);
  sendCommand(0x20);
  delay(100);
  return waitUntilIdle();
}

void showLocalStatus(const String& message) {
  if (message == lastLocalStatus) {
    return;
  }
  Serial.printf("DISPLAY STATUS: %s\n", message.c_str());
  if (refreshDisplay(message)) {
    lastLocalStatus = message;
  } else {
    Serial.println("WARNING: could not draw connection status.");
  }
}

void pollOnce() {
  Serial.println("\n--- cellular poll ---");
  String message;
  int revision = 0;
  bool pollOk = false;
  bool displayUpdated = false;
  String lastError;

  if (!beginHttp()) {
    lastError = "could not initialize modem HTTP service";
  } else if (!getMessage(message, revision)) {
    lastError = "message request failed";
  } else {
    pollOk = true;
    if (revision != lastMessageRevision || !lastLocalStatus.isEmpty()) {
      if (refreshDisplay(message)) {
        lastMessageRevision = revision;
        lastLocalStatus = "";
        displayUpdated = true;
      } else {
        pollOk = false;
        lastError = "e-paper refresh timed out";
      }
    }
  }

  const ConnectionStatus connection = readConnectionStatus();
  if (!pollOk) {
    if (connection.networkType == "NO SERVICE" ||
        connection.networkType.isEmpty()) {
      showLocalStatus(
          "cellular\nno service\nhologram activating\nretrying...");
    } else {
      showLocalStatus("cellular online\nserver unavailable\nretrying...");
    }
  }
  if (!postTelemetry(connection, pollOk, displayUpdated, lastError)) {
    Serial.println("Telemetry report failed.");
  }
  sendAT("AT+HTTPTERM");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSignal Note cellular phone starting");

  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  digitalWrite(EPD_CS, HIGH);
  SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
  showLocalStatus("cellular\nstarting...");

  SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  if (!waitForModem()) {
    Serial.println("FATAL: SIM7670G did not respond.");
    showLocalStatus("cellular\nmodem not found\ncheck device");
    return;
  }
  if (!configureCellular()) {
    Serial.println("WARNING: Hologram APN/roaming configuration failed.");
  }
  if (!waitForNetwork()) {
    Serial.println("WARNING: cellular registration timed out; polling anyway.");
    showLocalStatus(
        "cellular\nno service\nhologram activating\nretrying...");
  } else {
    showLocalStatus("cellular connected\ncontacting server...");
  }
  pollOnce();
  nextPollAt = millis() + POLL_INTERVAL_MS;
}

void loop() {
  if (static_cast<int32_t>(millis() - nextPollAt) >= 0) {
    pollOnce();
    nextPollAt = millis() + POLL_INTERVAL_MS;
  }
  delay(20);
}
