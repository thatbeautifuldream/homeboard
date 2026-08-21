#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <MD_Parola.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#ifndef WIFI_SSID
#error "WIFI_SSID must be supplied by the PlatformIO build environment"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be supplied by the PlatformIO build environment"
#endif
#ifndef API_TOKEN
#error "API_TOKEN must be supplied by the PlatformIO build environment"
#endif

namespace {
constexpr char kDeviceName[] = "MilluBoard";
constexpr char kHostName[] = "milluboard";
constexpr char kAccessPointPassword[] = "milluboard";
constexpr char kApiToken[] = API_TOKEN;
constexpr uint8_t kLedPin = 2;
constexpr uint8_t kMatrixDataPin = 23;  // DIN
constexpr uint8_t kMatrixClockPin = 18; // CLK
constexpr uint8_t kMatrixChipSelectPin = 15; // CS
constexpr uint8_t kMatrixModuleCount = 4;
constexpr uint32_t kMessageDisplayDurationMs = 10000;
constexpr uint32_t kBootDisplayDurationMs = 5000;

WebServer server(80);
String boardMessage = "MILLU BOARD";
String displayTextBuffer;
bool ledOn = false;
bool fallbackAccessPoint = false;
bool networkServicesStarted = false;
IPAddress networkServicesIp;
uint32_t messageDisplayUntil = 0;
bool bootBannerPending = true;
MD_Parola matrix(MD_MAX72XX::FC16_HW, kMatrixDataPin, kMatrixClockPin,
                 kMatrixChipSelectPin, kMatrixModuleCount);
constexpr uint8_t kMaxDashboardClients = 8;
constexpr uint32_t kClientActiveWindowMs = 30000;
IPAddress dashboardClientIps[kMaxDashboardClients];
uint32_t dashboardClientSeenAt[kMaxDashboardClients] = {};

void showBoardMessage() {
  matrix.setZone(0, 0, kMatrixModuleCount - 1);
  displayTextBuffer = boardMessage;
  matrix.displayText(displayTextBuffer.c_str(), PA_CENTER, 35, 0,
                     PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void showClock() {
  struct tm localTime;
  char clockText[6] = "--:--";
  if (getLocalTime(&localTime, 100)) {
    strftime(clockText, sizeof(clockText), "%I:%M", &localTime);
  }
  matrix.setZone(0, 0, kMatrixModuleCount - 1);
  displayTextBuffer = clockText;
  matrix.displayText(displayTextBuffer.c_str(), PA_CENTER, 35, 0, PA_PRINT, PA_NO_EFFECT);
}

void updateDisplayContent() {
  if (messageDisplayUntil != 0 && millis() < messageDisplayUntil) return;
  if (messageDisplayUntil != 0) {
    messageDisplayUntil = 0;
    showClock();
    return;
  }
  struct tm localTime;
  if (!getLocalTime(&localTime, 0)) return;
  char clockText[6];
  strftime(clockText, sizeof(clockText), "%H:%M", &localTime);
  if (displayTextBuffer != clockText) showClock();
}

const char kOpenApi[] PROGMEM = R"JSON({"openapi":"3.0.3","info":{"title":"MilluBoard API","version":"1.0.0","description":"Authenticated local HTTP API for MilluBoard."},"servers":[{"url":"http://milluboard.local"}],"components":{"securitySchemes":{"bearerAuth":{"type":"http","scheme":"bearer","bearerFormat":"API token"}},"schemas":{"MessageRequest":{"type":"string","minLength":1,"maxLength":80}}},"security":[{"bearerAuth":[]}],"paths":{"/api/v1/status":{"get":{"summary":"Get device status","responses":{"200":{"description":"Current status"},"401":{"description":"Missing or invalid token"}}}},"/api/v1/display/message":{"post":{"summary":"Set a persistent display message","requestBody":{"required":true,"content":{"text/plain":{"schema":{"$ref":"#/components/schemas/MessageRequest"}}}},"responses":{"200":{"description":"Updated status"},"400":{"description":"Invalid message"},"401":{"description":"Missing or invalid token"}}}},"/api/v1/led":{"post":{"summary":"Toggle the onboard LED","responses":{"200":{"description":"Updated status"},"401":{"description":"Missing or invalid token"}}}}}})JSON";
const char kDocsPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><title>MilluBoard API Docs</title></head><body><script id="api-reference" data-url="/openapi.json"></script><script src="https://cdn.jsdelivr.net/npm/@scalar/api-reference"></script></body></html>)HTML";

bool authorized() {
  const String header = server.header("Authorization");
  const String expected = String("Bearer ") + kApiToken;
  if (header == expected) return true;
  server.sendHeader("WWW-Authenticate", "Bearer");
  server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

bool validTextBody(String &value) {
  if (server.hasHeader("Content-Length") && server.header("Content-Length").toInt() > 80) return false;
  if (server.header("Content-Type") != "text/plain") return false;
  value = server.arg("plain");
  if (value.length() < 1 || value.length() > 80) return false;
  for (const char character : value) if (static_cast<uint8_t>(character) < 0x20 && character != '\n' && character != '\r' && character != '\t') return false;
  return true;
}

uint8_t trackDashboardClient() {
  const uint32_t now = millis();
  const IPAddress remoteIp = server.client().remoteIP();
  int8_t availableSlot = -1;
  uint8_t activeClients = 0;
  bool clientAlreadyTracked = false;

  for (uint8_t index = 0; index < kMaxDashboardClients; index++) {
    const bool active = dashboardClientSeenAt[index] != 0 && now - dashboardClientSeenAt[index] <= kClientActiveWindowMs;
    if (active) {
      activeClients++;
      if (dashboardClientIps[index] == remoteIp) {
        dashboardClientSeenAt[index] = now;
        clientAlreadyTracked = true;
      }
    } else if (availableSlot < 0) {
      availableSlot = index;
    }
  }

  if (!clientAlreadyTracked && availableSlot >= 0) {
    dashboardClientIps[availableSlot] = remoteIp;
    dashboardClientSeenAt[availableSlot] = now;
    activeClients++;
  }
  return activeClients;
}

void sendJsonStatus() {
  const uint8_t activeClients = trackDashboardClient();
  String json;
  json.reserve(180 + boardMessage.length());
  json += F("{\"device\":\""); json += kDeviceName;
  json += F("\",\"hostname\":\""); json += kHostName;
  json += F(".local\",\"message\":\"");
  for (const char character : boardMessage) {
    if (character == '"' || character == '\\') json += '\\';
    if (static_cast<uint8_t>(character) >= 0x20) json += character;
  }
  json += F("\",\"uptime_ms\":"); json += millis();
  json += F(",\"free_heap\":"); json += ESP.getFreeHeap();
  json += F(",\"clients\":"); json += activeClients;
  json += F(",\"network_mode\":\""); json += fallbackAccessPoint ? F("access_point") : F("home_wifi"); json += '"';
  json += F(",\"ip\":\""); json += fallbackAccessPoint ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); json += '"';
  json += F(",\"led\":"); json += ledOn ? F("true") : F("false");
  json += '}';
  server.send(200, "application/json", json);
}

void configureRoutes() {
  server.on("/", HTTP_GET, [] {
    File index = LittleFS.open("/index.html", "r");
    if (!index) {
      server.send(500, "application/json", "{\"error\":\"index unavailable\"}");
      return;
    }
    server.streamFile(index, "text/html");
    index.close();
  });
  server.on("/docs", HTTP_GET, [] { server.send_P(200, "text/html", kDocsPage); });
  server.on("/openapi.json", HTTP_GET, [] { server.send_P(200, "application/json", kOpenApi); });
  server.on("/api/v1/status", HTTP_GET, [] {
    if (authorized()) sendJsonStatus();
  });
  server.on("/api/v1/display/message", HTTP_POST, [] {
    if (!authorized()) return;
    String nextMessage;
    if (!validTextBody(nextMessage)) {
      server.send(400, "application/json", "{\"error\":\"message must contain 1-80 characters\"}");
      return;
    }
    boardMessage = nextMessage;
    messageDisplayUntil = millis() + kMessageDisplayDurationMs;
    showBoardMessage();
    sendJsonStatus();
  });
  server.on("/api/v1/led", HTTP_POST, [] {
    if (!authorized()) return;
    ledOn = !ledOn;
    digitalWrite(kLedPin, ledOn ? HIGH : LOW);
    sendJsonStatus();
  });
  server.onNotFound([] { server.send(404, "application/json", "{\"error\":\"not found\"}"); });
}

void configureOta() {
  ArduinoOTA.setHostname(kHostName);
  ArduinoOTA.setPassword(kApiToken);
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setTimeout(10000);
  ArduinoOTA.onStart([] { Serial.println("OTA: update started"); });
  ArduinoOTA.onEnd([] { Serial.println("OTA: update complete"); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA: error %u\n", static_cast<unsigned>(error));
  });
  ArduinoOTA.begin();
}

void refreshNetworkServices() {
  if (fallbackAccessPoint || WiFi.status() != WL_CONNECTED) return;
  const IPAddress currentIp = WiFi.localIP();
  if (currentIp == IPAddress(0, 0, 0, 0) ||
      (networkServicesStarted && currentIp == networkServicesIp)) return;

  if (networkServicesStarted) {
    ArduinoOTA.end();
    MDNS.end();
  }
  if (!MDNS.begin(kHostName)) {
    networkServicesStarted = false;
    Serial.println("mDNS: start failed; will retry");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  configureOta();
  networkServicesIp = currentIp;
  networkServicesStarted = true;
  Serial.printf("mDNS/OTA: active at %s (%s)\n", kHostName, currentIp.toString().c_str());
}
}  // namespace

void setup() {
  Serial.begin(115200);
  const char *requiredHeaders[] = {"Authorization", "Content-Type", "Content-Length"};
  server.collectHeaders(requiredHeaders, 3);
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  matrix.begin();
  matrix.setIntensity(2);
  matrix.displayClear();
  showClock();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(kHostName);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to %s", WIFI_SSID);
  const uint32_t connectionStartedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectionStartedAt < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    fallbackAccessPoint = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kDeviceName, kAccessPointPassword);
  }
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS: mount failed");
  }
  configureRoutes();
  server.begin();
  server.enableDelay(false);
  refreshNetworkServices();
  Serial.println();
  const IPAddress address = fallbackAccessPoint ? WiFi.softAPIP() : WiFi.localIP();
  Serial.println("MilluBoard playground started");
  Serial.printf("Mode: %s\n", fallbackAccessPoint ? "fallback access point" : "home Wi-Fi");
  Serial.printf("Open: http://%s.local or http://%s\n", kHostName, address.toString().c_str());
  Serial.println("OTA: enabled");
  Serial.printf("mDNS: %s\n", networkServicesStarted ? "ok" : "pending");

}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  refreshNetworkServices();
  matrix.displayAnimate();
  if (bootBannerPending) {
    bootBannerPending = false;
    messageDisplayUntil = millis() + kBootDisplayDurationMs;
    showBoardMessage();
  }
  static uint32_t lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = millis();
    updateDisplayContent();
  }
  static uint32_t lastReconnectAttempt = 0;
  if (!fallbackAccessPoint && WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnectAttempt >= 5000) {
    lastReconnectAttempt = millis();
    WiFi.reconnect();
  }
  yield();
}
