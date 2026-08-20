#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#ifndef WIFI_SSID
#error "WIFI_SSID must be supplied by the PlatformIO build environment"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be supplied by the PlatformIO build environment"
#endif

namespace {
constexpr char kDeviceName[] = "MilluBoard";
constexpr char kHostName[] = "milluboard";
constexpr char kAccessPointPassword[] = "milluboard";
constexpr uint8_t kLedPin = 2;

WebServer server(80);
String boardMessage = "Hello from MilluBoard!";
bool ledOn = false;
bool fallbackAccessPoint = false;
constexpr uint8_t kMaxDashboardClients = 8;
constexpr uint32_t kClientActiveWindowMs = 30000;
IPAddress dashboardClientIps[kMaxDashboardClients];
uint32_t dashboardClientSeenAt[kMaxDashboardClients] = {};

const char kPage[] PROGMEM = R"HTML(
<!doctype html><html lang="en" class="scheme-only-dark antialiased"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#000000"><title>MilluBoard</title>
<script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4"></script></head>
<body class="min-h-dvh bg-black font-sans text-white selection:bg-white selection:text-black"><main class="isolate mx-auto max-w-4xl p-5 sm:p-8 lg:py-12">
<header class="flex items-center justify-between gap-4 border-b border-white/15 pb-5"><a href="/" aria-label="Homepage" class="min-w-0"><h1 class="truncate text-xl font-semibold tracking-tight text-balance">MilluBoard</h1></a><div class="flex shrink-0 items-center gap-2 text-base/7 sm:text-sm/6"><span id="connection-dot" class="size-2 shrink-0 rounded-full bg-white" aria-hidden="true"></span><span id="connection-text">Online</span></div></header>

<section aria-label="System status" class="@container py-8 sm:py-10"><dl class="grid grid-cols-2 gap-y-8 @2xl:grid-cols-4">
<div class="pr-4"><dt class="truncate text-base/7 font-medium text-zinc-400 sm:text-sm/6">Uptime</dt><dd id="uptime" class="pt-1 text-2xl font-semibold tracking-tight tabular-nums">—</dd></div>
<div class="border-l border-white/15 pl-4 @2xl:px-5"><dt class="truncate text-base/7 font-medium text-zinc-400 sm:text-sm/6">Free memory</dt><dd id="heap" class="pt-1 text-2xl font-semibold tracking-tight tabular-nums">—</dd></div>
<div class="pr-4 @2xl:border-l @2xl:border-white/15 @2xl:px-5"><dt class="truncate text-base/7 font-medium text-zinc-400 sm:text-sm/6">Connected clients</dt><dd id="clients" class="pt-1 text-2xl font-semibold tracking-tight tabular-nums">—</dd></div>
<div class="border-l border-white/15 pl-4 @2xl:pl-5"><dt class="truncate text-base/7 font-medium text-zinc-400 sm:text-sm/6">LED</dt><dd id="led" class="pt-1 text-2xl font-semibold tracking-tight">—</dd></div>
</dl></section>

<section class="border-t border-white/15 py-8 sm:py-10"><h2 class="text-lg font-semibold">Message</h2><p id="message" class="min-h-8 pt-3 font-mono text-base/7 text-zinc-300">Loading...</p><form id="message-form" class="flex min-w-0 flex-col gap-3 pt-4 sm:max-w-xl sm:flex-row"><input id="message-input" name="message" aria-label="Board message" maxlength="80" required placeholder="Write a local message" class="min-w-0 flex-1 rounded-none border-b border-white/30 bg-transparent px-0 py-2.5 text-base/7 text-white placeholder:text-zinc-600 focus:border-white focus:outline-none sm:py-2 sm:text-sm/6"><button id="message-button" type="submit" class="rounded-md bg-white px-3 py-2.5 text-base/7 font-medium text-black ring-1 ring-white hover:bg-zinc-200 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-white disabled:cursor-wait disabled:opacity-60 sm:py-2 sm:text-sm/6">Set message</button></form><p id="form-status" aria-live="polite" class="pt-3 text-base/7 text-zinc-500 sm:text-sm/6"></p></section>

<section class="flex flex-col gap-4 border-t border-white/15 py-8 sm:flex-row sm:items-center sm:justify-between sm:py-10"><div><h2 class="text-lg font-semibold">Onboard LED</h2><p id="led-description" class="pt-1 text-base/7 text-zinc-500 sm:text-sm/6">Currently off.</p></div><button id="led-button" type="button" aria-pressed="false" class="relative rounded-md px-3 py-2.5 text-base/7 font-medium ring-1 ring-white/30 hover:bg-white hover:text-black focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-white sm:py-2 sm:text-sm/6">Toggle LED<span class="pointer-fine:hidden absolute top-1/2 left-1/2 size-[max(100%,3rem)] -translate-1/2" aria-hidden="true"></span></button></section>

<footer class="flex items-center justify-between gap-4 border-t border-white/15 pt-5 text-base/7 text-zinc-600 sm:text-sm/6"><p id="network-address" class="truncate font-mono">milluboard.local</p><button id="refresh-button" type="button" class="relative shrink-0 text-zinc-400 hover:text-white focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-white">Refresh<span class="pointer-fine:hidden absolute top-1/2 left-1/2 size-[max(100%,3rem)] -translate-1/2" aria-hidden="true"></span></button></footer></main>
<script>
const $=id=>document.getElementById(id);const formatUptime=ms=>{const s=Math.floor(ms/1000),d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?`${d}d ${h}h`:h?`${h}h ${m}m`:`${m}m ${s%60}s`};
function connection(ok){$('connection-text').textContent=ok?'Online':'Offline';$('connection-dot').classList.toggle('bg-white',ok);$('connection-dot').classList.toggle('bg-zinc-600',!ok)}
async function refresh(){try{const s=await fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()});$('message').textContent=s.message;$('uptime').textContent=formatUptime(s.uptime_ms);$('heap').textContent=Math.round(s.free_heap/1024)+' KB';$('clients').textContent=s.clients;$('led').textContent=s.led?'On':'Off';$('led-description').textContent=s.led?'Currently on.':'Currently off.';$('led-button').setAttribute('aria-pressed',s.led);$('network-address').textContent=s.ip+' · '+s.hostname;connection(true)}catch(e){connection(false)}}
$('refresh-button').addEventListener('click',refresh);$('message-form').addEventListener('submit',async e=>{e.preventDefault();const button=$('message-button');button.disabled=true;$('form-status').textContent='Saving...';try{await fetch('/api/message',{method:'POST',headers:{'Content-Type':'text/plain'},body:$('message-input').value});$('message-input').value='';$('form-status').textContent='Saved.';await refresh()}catch(e){$('form-status').textContent='Could not save.'}finally{button.disabled=false}});$('led-button').addEventListener('click',async()=>{await fetch('/api/led',{method:'POST'});refresh()});refresh();setInterval(refresh,10000);
</script></body></html>)HTML";

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
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", kPage); });
  server.on("/api/status", HTTP_GET, sendJsonStatus);
  server.on("/api/message", HTTP_POST, [] {
    const String nextMessage = server.arg("plain");
    if (nextMessage.isEmpty() || nextMessage.length() > 80) {
      server.send(400, "application/json", "{\"error\":\"message must contain 1-80 characters\"}");
      return;
    }
    boardMessage = nextMessage;
    sendJsonStatus();
  });
  server.on("/api/led", HTTP_POST, [] {
    ledOn = !ledOn;
    digitalWrite(kLedPin, ledOn ? HIGH : LOW);
    sendJsonStatus();
  });
  server.onNotFound([] { server.send(404, "application/json", "{\"error\":\"not found\"}"); });
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  WiFi.mode(WIFI_STA);
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
  const bool mdnsStarted = MDNS.begin(kHostName);
  configureRoutes();
  server.begin();
  if (mdnsStarted) MDNS.addService("http", "tcp", 80);
  Serial.println();
  const IPAddress address = fallbackAccessPoint ? WiFi.softAPIP() : WiFi.localIP();
  Serial.println("MilluBoard playground started");
  Serial.printf("Mode: %s\n", fallbackAccessPoint ? "fallback access point" : "home Wi-Fi");
  Serial.printf("Open: http://%s.local or http://%s\n", kHostName, address.toString().c_str());
  Serial.printf("mDNS: %s\n", mdnsStarted ? "ok" : "failed");
}

void loop() {
  server.handleClient();
  delay(2);
}
