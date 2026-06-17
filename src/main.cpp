#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

#ifndef AP_SSID
#define AP_SSID "ESP32-C3-Setup"
#endif

#ifndef AP_PASSWORD
#define AP_PASSWORD "12345678"
#endif

#ifndef CONFIG_PORTAL_TIMEOUT_SEC
#define CONFIG_PORTAL_TIMEOUT_SEC 300
#endif

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 9
#endif

#ifndef BOOT_HOLD_MS
#define BOOT_HOLD_MS 3000
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 8000
#endif

#ifndef AP_WIFI_CHANNEL
#define AP_WIFI_CHANNEL 1
#endif

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

const byte DNS_PORT = 53;
volatile bool portalRunning = false;
bool apStarted = false;
bool portalRoutesRegistered = false;

void handleConfigPage();
void handleSave();
void handleCaptiveRedirect();

const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-C3 WiFi</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: system-ui, sans-serif; margin: 0; padding: 24px 16px; background: #f3f4f6; }
    .card { max-width: 420px; margin: 0 auto; background: #fff; padding: 24px; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,.08); }
    h1 { font-size: 1.25rem; margin: 0 0 8px; }
    p { color: #666; font-size: 0.92rem; line-height: 1.4; }
    label { display: block; margin-top: 14px; font-weight: 600; font-size: 0.9rem; }
    input { width: 100%; margin-top: 6px; padding: 12px; border: 1px solid #ccc; border-radius: 8px; font-size: 16px; }
    button { width: 100%; margin-top: 12px; padding: 14px; border: 0; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; }
    .btn-primary { background: #1565c0; color: #fff; margin-top: 20px; }
    .btn-secondary { background: #e8eef5; color: #1565c0; }
    .networks { margin-top: 16px; }
    .networks h2 { font-size: 0.95rem; margin: 0 0 8px; }
    .status { color: #666; font-size: 0.88rem; margin-bottom: 8px; }
    .net-list { list-style: none; padding: 0; margin: 0; max-height: 220px; overflow-y: auto; border: 1px solid #e5e7eb; border-radius: 8px; }
    .net-item { display: flex; justify-content: space-between; align-items: center; padding: 12px; border-bottom: 1px solid #f0f0f0; cursor: pointer; }
    .net-item:last-child { border-bottom: 0; }
    .net-item:hover { background: #f8fafc; }
    .net-item.active { background: #e3f2fd; }
    .net-name { font-weight: 600; font-size: 0.92rem; word-break: break-all; padding-right: 8px; }
    .net-meta { font-size: 0.78rem; color: #666; white-space: nowrap; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Cau hinh WiFi</h1>
    <p>Tat 4G/5G tren dien thoai. Giu nut BOOT 3 giay khi bat ESP32 de reset WiFi.</p>

    <form method="POST" action="/save">
      <label>Ten WiFi (SSID)</label>
      <input id="ssid" name="ssid" required autocomplete="off" placeholder="Go ten WiFi nha">
      <label>Mat khau WiFi</label>
      <input id="pass" name="pass" type="password" autocomplete="off" placeholder="Mat khau">
      <button class="btn-primary" type="submit">Luu va ket noi</button>
    </form>

    <div class="networks">
      <h2>Hoac chon tu danh sach</h2>
      <div class="status" id="scanStatus">Bam "Quet mang" de tim WiFi gan day</div>
      <ul class="net-list" id="netList"></ul>
      <button type="button" class="btn-secondary" id="rescanBtn">Quet mang</button>
    </div>
  </div>
  <script>
    const netList = document.getElementById('netList');
    const scanStatus = document.getElementById('scanStatus');
    const ssidInput = document.getElementById('ssid');
    const rescanBtn = document.getElementById('rescanBtn');

    function renderNetworks(networks) {
      netList.innerHTML = '';
      if (!networks || networks.length === 0) {
        scanStatus.textContent = 'Khong tim thay mang nao. Hay go tay SSID.';
        return;
      }
      scanStatus.textContent = 'Tim thay ' + networks.length + ' mang. Bam de chon.';
      networks.forEach(function(net) {
        const li = document.createElement('li');
        li.className = 'net-item';
        li.innerHTML = '<span class="net-name"></span><span class="net-meta"></span>';
        li.querySelector('.net-name').textContent = net.ssid || '(an)';
        li.querySelector('.net-meta').textContent = net.rssi + ' dBm · ' + (net.secure ? 'Co mat khau' : 'Mo');
        li.onclick = function() {
          document.querySelectorAll('.net-item').forEach(function(el) { el.classList.remove('active'); });
          li.classList.add('active');
          ssidInput.value = net.ssid || '';
          document.getElementById('pass').focus();
        };
        netList.appendChild(li);
      });
    }

    async function waitForScan() {
      for (let i = 0; i < 40; i++) {
        const res = await fetch('/scan');
        const data = await res.json();
        if (data.status === 'scanning') {
          scanStatus.textContent = 'Dang quet mang... (' + (i + 1) + 's)';
          await new Promise(function(r) { setTimeout(r, 1000); });
          continue;
        }
        if (data.status === 'error') {
          scanStatus.textContent = 'Quet mang loi. Hay go tay SSID.';
          return;
        }
        renderNetworks(data.networks);
        return;
      }
      scanStatus.textContent = 'Quet mang qua lau. Hay go tay SSID.';
    }

    async function startScan(refresh) {
      rescanBtn.disabled = true;
      scanStatus.textContent = 'Dang quet mang...';
      netList.innerHTML = '';
      if (refresh) {
        await fetch('/scan?refresh=1');
      }
      await waitForScan();
      rescanBtn.disabled = false;
    }

    rescanBtn.addEventListener('click', function() { startScan(true); });
  </script>
</body>
</html>
)rawliteral";

const char *getApPassword() {
  if (strlen(AP_PASSWORD) >= 8) {
    return AP_PASSWORD;
  }
  return "12345678";
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }

  return escaped;
}

void prepareWiFiRadio() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
}

void applyWiFiCountry() {
  wifi_country_t country{};
  strncpy(reinterpret_cast<char *>(country.cc), "01", 2);
  country.cc[2] = '\0';
  country.schan = 1;
  country.nchan = 13;
  country.policy = WIFI_COUNTRY_POLICY_AUTO;
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_country(&country);
}

void printConnectionInfo() {
  Serial.println();
  Serial.println("Ket noi WiFi thanh cong!");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.println("(AP cau hinh da tat vi da ket noi WiFi nha)");
}

bool isBootButtonHeld(uint32_t holdMs) {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_BUTTON_PIN) != LOW) {
    return false;
  }

  Serial.println("Giu nut BOOT de xoa WiFi...");
  const uint32_t start = millis();
  while (millis() - start < holdMs) {
    if (digitalRead(BOOT_BUTTON_PIN) != LOW) {
      Serial.println("Tha BOOT som, khong xoa WiFi");
      return false;
    }
    delay(50);
  }

  return true;
}

void clearWiFiCredentials() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
  Serial.println("Da xoa cau hinh WiFi");
}

void stopAccessPoint() {
  if (!apStarted) {
    return;
  }

  WiFi.softAPdisconnect(true);
  apStarted = false;
  Serial.println("Da tat AP cau hinh");
}

bool startAccessPoint() {
  prepareWiFiRadio();

  for (int attempt = 1; attempt <= 5; attempt++) {
    WiFi.mode(WIFI_AP_STA);
    delay(200);
    applyWiFiCountry();

    WiFi.softAPdisconnect(true);
    delay(100);

    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));

    if (WiFi.softAP(AP_SSID, getApPassword(), AP_WIFI_CHANNEL, 0, 8)) {
      apStarted = true;
      Serial.print("AP dang phat: ");
      Serial.print(AP_SSID);
      Serial.print(" | IP: ");
      Serial.print(WiFi.softAPIP());
      Serial.print(" | kenh: ");
      Serial.println(AP_WIFI_CHANNEL);
      return true;
    }

    Serial.print("Loi bat AP, thu lai lan ");
    Serial.println(attempt);
    delay(800);
  }

  apStarted = false;
  Serial.println("Khong phat duoc WiFi AP!");
  return false;
}

void handleCaptiveRedirect() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Connection", "close");
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void startAsyncScan() {
  const int scanState = WiFi.scanComplete();

  if (scanState == WIFI_SCAN_RUNNING) {
    return;
  }

  WiFi.scanDelete();
  Serial.println("Bat dau quet mang WiFi (async)...");
  WiFi.scanNetworks(true);
}

void handleConfigPage() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Connection", "close");
  server.send_P(200, "text/html; charset=utf-8", CONFIG_PAGE);
}

void handleScan() {
  if (server.hasArg("refresh")) {
    startAsyncScan();
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }

  const int scanState = WiFi.scanComplete();

  if (scanState == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }

  if (scanState < 0) {
    startAsyncScan();
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }

  String json = "{\"status\":\"done\",\"networks\":[";
  bool first = true;

  for (int i = 0; i < scanState; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) {
      continue;
    }

    bool duplicate = false;
    for (int j = 0; j < i; j++) {
      if (WiFi.SSID(j) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    if (!first) {
      json += ',';
    }
    first = false;

    json += "{\"ssid\":\"";
    json += jsonEscape(ssid);
    json += "\",\"rssi\":";
    json += WiFi.RSSI(i);
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "false" : "true";
    json += '}';
  }

  json += "]}";
  server.send(200, "application/json", json);
  WiFi.scanDelete();
}

bool connectToWiFi(const String &ssid, const String &password,
                   uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS,
                   bool keepAp = false) {
  Serial.println();
  Serial.print("Dang ket noi toi: ");
  Serial.println(ssid);

  if (keepAp) {
    startAccessPoint();
  } else {
    stopAccessPoint();
    WiFi.mode(WIFI_STA);
  }

  WiFi.disconnect(false, false);
  delay(200);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= timeoutMs) {
      Serial.println();
      Serial.println("Loi: Het thoi gian ket noi WiFi");
      return false;
    }
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  return true;
}

void saveWiFiCredentials(const String &ssid, const String &password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  preferences.end();
}

bool getSavedWiFi(String &ssid, String &pass) {
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  pass = preferences.getString("pass", "");
  preferences.end();
  return !ssid.isEmpty();
}

void registerPortalRoutes() {
  if (portalRoutesRegistered) {
    return;
  }

  server.on("/", HTTP_GET, handleConfigPage);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/canonical.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/success.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/redirect", HTTP_GET, handleCaptiveRedirect);
  server.onNotFound(handleCaptiveRedirect);
  portalRoutesRegistered = true;
}

void handleSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Thieu SSID");
    return;
  }

  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");

  server.send(200, "text/html; charset=utf-8",
              "<html><body style='font-family:sans-serif;padding:24px'>"
              "<h2>Dang ket noi WiFi...</h2>"
              "<p>Vui long doi 10-20 giay.</p></body></html>");

  delay(300);

  dnsServer.stop();
  server.stop();
  portalRunning = false;

  if (connectToWiFi(ssid, pass, 20000, false)) {
    saveWiFiCredentials(ssid, pass);
    printConnectionInfo();
  } else {
    Serial.println("Khong ket noi duoc, mo lai AP cau hinh...");
    delay(500);
  }
}

void startConfigPortal() {
  while (!apStarted) {
    if (startAccessPoint()) {
      break;
    }
    Serial.println("Cho 2 giay roi thu bat AP lai...");
    delay(2000);
  }

  Serial.println("Mo trang cau hinh: http://192.168.4.1");

  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  registerPortalRoutes();
  server.begin();
  portalRunning = true;

  const uint32_t portalStart = millis();
  const uint32_t portalTimeout = CONFIG_PORTAL_TIMEOUT_SEC * 1000UL;

  while (portalRunning) {
    for (int i = 0; i < 8; i++) {
      dnsServer.processNextRequest();
      server.handleClient();
    }

    if (millis() - portalStart >= portalTimeout) {
      Serial.println("Het thoi gian cau hinh, khoi dong lai...");
      ESP.restart();
    }

    yield();
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("ESP32-C3 WiFi Setup");

  prepareWiFiRadio();

  if (isBootButtonHeld(BOOT_HOLD_MS)) {
    clearWiFiCredentials();
  }

  // Bat AP ngay de luon thay WiFi ESP32
  startAccessPoint();

  String savedSsid;
  String savedPass;
  const bool hasSavedWiFi = getSavedWiFi(savedSsid, savedPass);

  if (hasSavedWiFi) {
    Serial.println("Co WiFi da luu, thu ket noi...");
    if (connectToWiFi(savedSsid, savedPass, WIFI_CONNECT_TIMEOUT_MS, true)) {
      stopAccessPoint();
      printConnectionInfo();
      return;
    }
    Serial.println("Khong ket noi duoc WiFi da luu, giu AP de cau hinh");
  }

  startConfigPortal();

  if (WiFi.status() != WL_CONNECTED) {
    startConfigPortal();
  }
}

void loop() {
  static uint32_t lastCheckMs = 0;

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastCheckMs < 10000) {
    return;
  }
  lastCheckMs = millis();

  Serial.println("Mat ket noi WiFi...");

  String savedSsid;
  String savedPass;
  if (getSavedWiFi(savedSsid, savedPass) &&
      connectToWiFi(savedSsid, savedPass, WIFI_CONNECT_TIMEOUT_MS, true)) {
    stopAccessPoint();
    printConnectionInfo();
    return;
  }

  startConfigPortal();
}
